#pragma comment(lib, "Winmm.lib")
#include <cpp_redis/cpp_redis>
#include <queue>
#include <unordered_map>
#include <stack>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>

#include "conio.h"
#include "CommonProtocol.h"
#include "Define.h"
#include "NetLibDefine.h"
#include "NetLib_Server.h"
#include "MonitorClient.h"
#include "Profiler.h"
#include "Parser.h"
#include "RingBuffer.h"
#include "CrashDump.h"
#include "DBConnector.h"
#include "SystemMonitor.h"
float WaitForTime(int tick, DWORD* prevTime);
void ServerControl();

void Monitoring(float deltaTime);
void Login_Timeout(DWORD currentTime);
void Succ_Timeout(DWORD currentTime);
void Content_Proc(unsigned long long sessionHandle, WORD contentType, Packet* packet);
SRWLOCK lock_waiting;
std::unordered_map<unsigned long long, WaitingSession> waiting;
SRWLOCK lock_succ;
std::unordered_map<unsigned long long, WaitingSession> succ;
SRWLOCK lock_blacklist;
std::unordered_map<unsigned long, unsigned short> blacklist;
SRWLOCK lock_tps_accept_list;
std::unordered_map<unsigned long, Stat_AcceptTPS> tps_accept_list;
WCHAR GameServerIP[16];
unsigned short GameServerPort;
WCHAR ChatServerIP[16];
unsigned short ChatServerPort;

WCHAR ChatServerIP_DUMMY_1[16];
WCHAR ChatServerIP_DUMMY_2[16];
WCHAR ChatServerIP_DUMMY_3[16];

unsigned long dummyIP_1;
unsigned long dummyIP_2;
unsigned long dummyIP_3;

const char* TEMP_USER_PASS = "97d7563773fed1fcb8896a35d82a2ba143e3d0c74481c2740e29836a69a1c1f7dc483701bf53918ca01fc71be825e81e1c6a786dcdb54a8e724854ecb01dd649";
TLSDBConnector* db;
cpp_redis::client* redisClient;
MonitorClient* monitorClient;
SystemMonitor system_monitor;

bool controlMode;
bool control_monitor = true;
long isRunning = true;


HANDLE ContentEvent;
class LoginServer :public NetLib_Server
{
public:
	bool use_blacklist;
	LoginServer(const WCHAR* openIP, unsigned short openPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, int opt_maxOfSession, bool opt_zerocpy, Opt_Encryption* opt_encryption, int opt_maxOfSendPackets)
		:NetLib_Server(openIP, openPort, opt_workerTH_Pool_size, opt_concurrentTH_size, opt_maxOfSession, opt_zerocpy, opt_encryption, opt_maxOfSendPackets)
	{
		use_blacklist = true;
		tps_auth = 0;
		InitializeSRWLock(&lock_tps_accept_list);

	}

	virtual void OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port)
	{
		AcquireSRWLockExclusive(&lock_tps_accept_list);
		auto it_ip = tps_accept_list.find(IP);
		if (it_ip != tps_accept_list.end())
		{
			it_ip->second.CurrTPS++;
		}
		else
		{
			tps_accept_list[IP] = { 1, INT_MIN };
		}
		ReleaseSRWLockExclusive(&lock_tps_accept_list);

		AcquireSRWLockExclusive(&lock_waiting);
		waiting[sessionHandle] = { timeGetTime(), IP, port };

		ReleaseSRWLockExclusive(&lock_waiting);

		return;
	}

	virtual void OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
	{
		AcquireSRWLockExclusive(&lock_waiting);
		auto itWating = waiting.find(sessionHandle);
		if (itWating != waiting.end())
		{
			itWating = waiting.erase(itWating);
		}
		ReleaseSRWLockExclusive(&lock_waiting);

		AcquireSRWLockExclusive(&lock_succ);
		auto itSucc = succ.find(sessionHandle);
		if (itSucc != succ.end())
		{
			itSucc = succ.erase(itSucc);
		}
		ReleaseSRWLockExclusive(&lock_succ);
		if (code != SESSION_LEAVE_CODE::NONE)
		{
			if (code == WRONG_HEADER_LEN || code == WRONG_HEADER_CODE || code == WRONG_HEADER_CHECKSUM || code == WRONG_RECVPOST_COUNT)
			{
				InsertBlacklist(IP, port);
			}

		}
	}

	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port)
	{
		if (use_blacklist)
		{
			AcquireSRWLockShared(&lock_blacklist);
			auto it = blacklist.find(IP);
			if (it != blacklist.end())
			{
				ReleaseSRWLockShared(&lock_blacklist);
				return false;
			}
			ReleaseSRWLockShared(&lock_blacklist);
		}

		return true;
	}

	virtual void OnRecv(unsigned long long sessionHandle, Packet* packet)
	{
		PROFILING("CONTENT");


		WORD contentType;
		*packet >> contentType;
		Content_Proc(sessionHandle, contentType, packet);

	}
	long tps_auth;
	void InsertBlacklist(unsigned long IP, unsigned short Port)
	{
		if (use_blacklist == false) return;
		WCHAR IPPort[32];
		NetLib_Helper::IPPortToWstring(IP, Port, IPPort, 32);
		LOG(L"BlackList_Insert", LEVEL_DEBUG, L"Insert Blacklist. %s", IPPort);

		AcquireSRWLockExclusive(&lock_blacklist);
		blacklist[IP] = Port;
		ReleaseSRWLockExclusive(&lock_blacklist);
	}

	void ClearBlacklist()
	{
		LOG(L"BlackList_Clear", LEVEL_DEBUG, L"Clearing entire blacklist.");

		AcquireSRWLockExclusive(&lock_blacklist);
		blacklist.clear();
		ReleaseSRWLockExclusive(&lock_blacklist);

	}
	SRWLOCK lock_tps_accept_list;
	std::unordered_map<unsigned long, Stat_AcceptTPS> tps_accept_list;
	void Collect_AcceptTPS()
	{
		//////////////////////////////////////////
		/////////////////////// accept tps 감지
		AcquireSRWLockExclusive(&lock_tps_accept_list);
		for (auto it = tps_accept_list.begin(); it != tps_accept_list.end(); )
		{
			long curr = it->second.CurrTPS;
			it->second.CurrTPS = 0;

			if (curr > it->second.MaxTPS)
			{
				it->second.MaxTPS = curr;
				WCHAR IPstr[32];
				NetLib_Helper::IPToWstring(it->first, IPstr, 32); // 문자열은 여기서만 생성
				LOG(L"IP_ACCEPT_TPS", LEVEL_DEBUG,
					L"update max tps IP: %s    accept_tps: %d", IPstr, curr);

				if (curr > MAX_ACCEPT_TPS)
				{
					LOG(L"WRONG_ACCEPT_TPS", LEVEL_DEBUG,
						L"Wrong accept ip: %s    accept_tps: %d", IPstr, curr);
					InsertBlacklist(it->first, 0);
					it = tps_accept_list.erase(it);
					continue; // erase해서 다음으로 이동
				}
			}

			++it;
		}

		ReleaseSRWLockExclusive(&lock_tps_accept_list);
	}
};
LoginServer* server;

void Content_Proc(unsigned long long sessionHandle, WORD contentType, Packet* packet)
{
	switch (contentType)
	{
	case en_PACKET_CS_LOGIN_REQ_LOGIN:
	{
		unsigned long IP;
		AcquireSRWLockExclusive(&lock_waiting);
		auto it = waiting.find(sessionHandle);
		if (it != waiting.end())
		{
			IP = it->second.IP;
			waiting.erase(it);
			ReleaseSRWLockExclusive(&lock_waiting);

		}
		else
		{
			ReleaseSRWLockExclusive(&lock_waiting);
			server->Disconnect(sessionHandle);
			return;
		}

		// 토큰 생성(플랫폼에서 인증받은걸로 가정)
		int64_t AccountNo;
		char SessionKey[SESSIONKEY_LENGTH];
		*packet >> AccountNo;
		packet->GetData(SessionKey, sizeof(char) * SESSIONKEY_LENGTH);

		// 로그인 요청
		// 계정DB에 접근해서 읽기.
		//std::wstring query = L"SELECT * FROM account WHERE accountno = " + std::to_wstring(AccountNo) + L";";
		//DBConnector* connector = db->GetInstance();
		//if (connector->TryPingAndConnect() == false)
		//{
		//	LOG(L"DB", LEVEL_DEBUG, L"threadID: %d, errcode: %d, msg: %s, errno: %d, query: %s",
		//		GetCurrentThreadId(), connector->GetLastError(), connector->GetLastErrorMsg().c_str(), connector->GetLastErrorNo(), query.c_str());
		//	server->Disconnect(sessionHandle);
		//	return;
		//}

		//MYSQL_RES* sql_res = connector->Read(query);
		//if (sql_res == nullptr)
		//{
		//	LOG(L"DB", LEVEL_DEBUG, L"threadID: %d, errcode: %d, msg: %s, errno: %d, query: %s",
		//		GetCurrentThreadId(), connector->GetLastError(), connector->GetLastErrorMsg().c_str(), connector->GetLastErrorNo(), query.c_str());
		//	server->Disconnect(sessionHandle);
		//	return;
		//}

		//MYSQL_ROW sql_row;
		//bool correct = false;
		//sql_row = mysql_fetch_row(sql_res);
		//if (memcmp(TEMP_USER_PASS, sql_row[2], SESSIONKEY_LENGTH) == 0)
		//	correct = true;
		//mysql_free_result(sql_res);

		//if (correct == false)
		//{
		//	LOG(L"LOGIN_SessionKey", LEVEL_DEBUG, L"Invalid Token");
		//	server->Disconnect(sessionHandle);
		//	return;
		//}

		// Redis에 삽입
		// 
		redisClient->send({ "SET", std::to_string(AccountNo), std::string(SessionKey, SESSIONKEY_LENGTH), "EX", "30" },
			[](cpp_redis::reply& reply)
			{
				//std::cout << reply << std::endl;
			});
		redisClient->sync_commit();
		//    client.get("hello", [](cpp_redis::reply& reply) {
		//	std::cout << "get: " << reply << std::endl;
		//	});

		// 로그인 응답
		bool res = true;
		unsigned char status = en_PACKET_SC_LOGIN_RES_LOGIN::dfLOGIN_STATUS_OK;
		WCHAR ID[ID_LENGTH];
		WCHAR Nickname[NICKNAME_LENGTH];

		Packet* sendpacket = Packet::Alloc();
		*sendpacket << (WORD)en_PACKET_CS_LOGIN_RES_LOGIN << AccountNo << status;
		sendpacket->PutData((char*)ID, sizeof(WCHAR) * ID_LENGTH);
		sendpacket->PutData((char*)Nickname, sizeof(WCHAR) * NICKNAME_LENGTH);
		sendpacket->PutData((char*)GameServerIP, sizeof(WCHAR) * 16);
		*sendpacket << GameServerPort;

		if (IP == dummyIP_1)
			sendpacket->PutData((char*)ChatServerIP_DUMMY_1, sizeof(WCHAR) * 16);
		else if (IP == dummyIP_2)
			sendpacket->PutData((char*)ChatServerIP_DUMMY_2, sizeof(WCHAR) * 16);
		else if (IP == dummyIP_3)
			sendpacket->PutData((char*)ChatServerIP_DUMMY_3, sizeof(WCHAR) * 16);
		else
			sendpacket->PutData((char*)ChatServerIP, sizeof(WCHAR) * 16);

		*sendpacket << ChatServerPort;
		server->SendPacket(sessionHandle, sendpacket);
		Packet::Free(sendpacket);

		AcquireSRWLockExclusive(&lock_succ);
		succ[sessionHandle] = { timeGetTime(), 0, 0};
		ReleaseSRWLockExclusive(&lock_succ);

		InterlockedIncrement(&server->tps_auth);
	}
	break;

	default:
		break;
	}
}
unsigned int __stdcall ContentThread(void* params)
{
	while (1)
	{
		WaitForSingleObject(ContentEvent, 1000);
		DWORD currentTime = timeGetTime();

		// 타임아웃
		Login_Timeout(currentTime);
		Succ_Timeout(currentTime);
		//server->Collect_AcceptTPS();
	}

}

void Login_Timeout(DWORD currentTime)
{
	std::vector<unsigned long long> loginTimeoutSessions;

	AcquireSRWLockShared(&lock_waiting);
	for (const auto& [sessionHandle, session] : waiting)
	{
		DWORD acceptTime = session.acceptTime;
		if (currentTime < acceptTime) continue;

		DWORD elapsed = currentTime - acceptTime;
		if (elapsed > LOGIN_TIME_OUT_MS)
		{
			LOG(L"LOGIN_TIMEOUT", LEVEL_DEBUG,
				L"LoginTimeout session=%llu acceptTime=%u elapsed=%u",
				sessionHandle, acceptTime, elapsed);

			loginTimeoutSessions.push_back(sessionHandle);
		}
	}
	ReleaseSRWLockShared(&lock_waiting);

	for (auto session : loginTimeoutSessions)
	{
		server->Disconnect(session);
	}
}
void Succ_Timeout(DWORD currentTime)
{
	std::vector<unsigned long long> succTimeoutSessions;

	AcquireSRWLockShared(&lock_succ);
	for (const auto& [sessionHandle, session] : succ)
	{
		DWORD acceptTime = session.acceptTime;
		if (currentTime < acceptTime) continue;

		DWORD elapsed = currentTime - acceptTime;
		if (elapsed > SUCC_TIME_OUT_MS)
		{
			succTimeoutSessions.push_back(sessionHandle);
		}
	}
	ReleaseSRWLockShared(&lock_succ);

	for (auto session : succTimeoutSessions)
	{
		server->Disconnect(session);
	}
}
int main()
{
	// 초기화
	DWORD prevTime = timeGetTime();

	InitializeSRWLock(&lock_waiting);
	InitializeSRWLock(&lock_blacklist);

	IN_ADDR addr;
	InetPtonA(AF_INET, "10.0.1.2", &addr);
	dummyIP_1 = ntohl(addr.S_un.S_addr);
	InetPtonA(AF_INET, "10.0.2.2", &addr);
	dummyIP_2 = ntohl(addr.S_un.S_addr);
	InetPtonA(AF_INET, "127.0.0.1", &addr);
	dummyIP_3 = ntohl(addr.S_un.S_addr);

	int opt_serverport;
	int opt_poolsize;
	int opt_concurrentsize;
	int opt_maxofsession;
	bool opt_encryption;
	bool opt_zerocpy;
	int opt_encryption_header_code;
	int opt_encryption_fixed_key;

	char db_ip[20];
	int db_port;
	char db_user[30];
	char db_password[30];
	{
		Parser parser;
		if (parser.loadFromFile("login_config.cfg"))
		{
			opt_serverport = parser.GetInt("port");
			opt_poolsize = parser.GetInt("workerTH_Pool_size");
			opt_concurrentsize = parser.GetInt("concurrentTH_size");
			opt_maxofsession = parser.GetInt("maxofsession");
			opt_encryption = parser.GetBool("encryption");

			opt_encryption_header_code = (char)parser.GetInt("encryption_header_code");
			opt_encryption_fixed_key = (char)parser.GetInt("encryption_fixed_key");

			opt_zerocpy = parser.GetBool("zerocopy");

			parser.CopyString("db_ip", db_ip, 30);
			db_port = parser.GetInt("db_port");
			parser.CopyString("db_user", db_user, 30);
			parser.CopyString("db_password", db_password, 30);

			char gameServerIP[16];
			char chatServerIP[16];
			char chatServer_Dummy_1_IP[16];
			char chatServer_Dummy_2_IP[16];
			char chatServer_Dummy_3_IP[16];

			parser.CopyString("GameServerIP", gameServerIP, 16);
			parser.CopyString("ChatServerIP", chatServerIP, 16);
			parser.CopyString("ChatServerIP_Dummy_1", chatServer_Dummy_1_IP, 16);
			parser.CopyString("ChatServerIP_Dummy_2", chatServer_Dummy_2_IP, 16);
			parser.CopyString("ChatServerIP_Dummy_3", chatServer_Dummy_3_IP, 16);

			size_t converted = 0;
			mbstowcs_s(&converted, GameServerIP, gameServerIP, _TRUNCATE);
			mbstowcs_s(&converted, ChatServerIP, chatServerIP, _TRUNCATE);
			mbstowcs_s(&converted, ChatServerIP_DUMMY_1, chatServer_Dummy_1_IP, _TRUNCATE);
			mbstowcs_s(&converted, ChatServerIP_DUMMY_2, chatServer_Dummy_2_IP, _TRUNCATE);
			mbstowcs_s(&converted, ChatServerIP_DUMMY_3, chatServer_Dummy_3_IP, _TRUNCATE);


			GameServerPort = parser.GetInt("GameServerPort");
			ChatServerPort = parser.GetInt("ChatServerPort");
		}
		else
		{
			printf("config파일이 없습니다!!!");
			wint_t key = _getwch();
			return -1;
		}
	}
	db = new TLSDBConnector("127.0.0.1", "root", "whtjdcks", "accountdb");

	ContentEvent = CreateEvent(0, 0, 0, 0);
	HANDLE contentTh = (HANDLE)_beginthreadex(nullptr, 0, ContentThread, 0, 0, 0);
	Opt_Encryption opt_encrypt = { opt_encryption_header_code , opt_encryption_fixed_key };
	server = new LoginServer(L"0.0.0.0", opt_serverport, opt_poolsize, opt_concurrentsize, opt_maxofsession, opt_zerocpy, &opt_encrypt, 100);

	server->Start();
	// redis 연결
	redisClient = new cpp_redis::client;
	printf("Wait redis..");
	while (1)
	{
		try
		{
			redisClient->connect();
			break;
		}
		catch (...)
		{
			printf(".");
		}
	}
	printf("\nRedis has Connect!\n");

	printf("Wait monitor server..");

	Opt_Encryption opt_monitor_encrypt = { 109 , 30 };
	monitorClient = new MonitorClient(SERVER_NO, L"127.0.0.1", 21107, 2, 2, true, false, &opt_monitor_encrypt);
	while (1)
	{
		if (monitorClient->Connect() == true)
		{
			printf("\nMonitor Server has connect!\n");
			break;
		}

		printf(".");
	}

	while (isRunning)
	{
		float deltaTime = WaitForTime(500, &prevTime);
		Monitoring(deltaTime);
		ServerControl();


	} // end while
}

float WaitForTime(int tick, DWORD* prevTime)
{
	DWORD delta = (timeGetTime() - *prevTime);
	*prevTime += delta;
	int t = tick - delta;
	if (t > 0)
	{
		Sleep(t);
		delta += t;
		*prevTime += t;

	}
	return delta / 1000.0f;
}

float delta_cumulative;
float runningtime;
void Monitoring(float deltaTime)
{
	delta_cumulative += deltaTime;

	if (delta_cumulative >= 1)
	{
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		int timestamp = static_cast<int>(t);

		system_monitor.UpdateCpuTime();
		runningtime += delta_cumulative;
		delta_cumulative = 0;

		long availMem = system_monitor.GetAvailMemMB();
		long long processMem = system_monitor.GetProcessPrivateMB();
		long npMem = system_monitor.GetNonPagedMB();
		long tcpSend = (long)system_monitor.GetTotalSendKBps();
		long tcpRecv = (long)system_monitor.GetTotalRecvKBps();
		float processorTotal = system_monitor.ProcessorTotal();
		float processorUser = system_monitor.ProcessorKernel();
		float processorKernel = system_monitor.ProcessorKernel();
		float processTotal = system_monitor.ProcessTotal();
		float processUser = system_monitor.ProcessUser();
		float processKernel = system_monitor.ProcessKernel();

		long sessionCount = server->GetSessionCount();
		long packetPoolSize = server->GetPacketUseSize();
		long tps_recv = server->GetTPS_Recv();
		long tps_auth = InterlockedExchange(&server->tps_auth, 0);
		if (monitorClient->IsConnect() == true)
		{
			Packet* run = Packet::Alloc();
			*run << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN << 1 << timestamp;
			monitorClient->SendPacket(run);
			Packet::Free(run);

			Packet* cpu = Packet::Alloc();
			Packet* mem = Packet::Alloc();
			Packet* session = Packet::Alloc();
			Packet* auth = Packet::Alloc();
			Packet* packetPool = Packet::Alloc();

			*cpu << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_SERVER_CPU << (int)processTotal << timestamp;
			*mem << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_SERVER_MEM << (int)processMem << timestamp;
			*session << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_SESSION << (int)sessionCount << timestamp;
			*auth << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_AUTH_TPS << (int)tps_auth << timestamp;
			*packetPool << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_LOGIN_PACKET_POOL << (int)packetPoolSize << timestamp;

			monitorClient->SendPacket(cpu);
			monitorClient->SendPacket(mem);
			monitorClient->SendPacket(session);
			monitorClient->SendPacket(auth);
			monitorClient->SendPacket(packetPool);



			Packet::Free(cpu);
			Packet::Free(mem);
			Packet::Free(session);
			Packet::Free(auth);
			Packet::Free(packetPool);
		}
		if (control_monitor)
		{
			printf("\n-------------------------------------------------------------------------\n");
			printf("\n\n**   LOGIN SERVER   **\n\n");
			printf("Running Time:  %20.2f  sec\n", runningtime);
			printf("\nMonitor Server Connect: ");

			if (monitorClient->IsConnect() == true) printf("TRUE");
			else printf("FALSE");

			printf("\n\nSession Count:              %7d", sessionCount);
			printf("\nPacket Pool Use:            %7d", packetPoolSize);
			printf("\nTotal_Accept:               %7d", server->GetTotal_Accept());
			printf("\nTPS_Accept:                 %7d", server->GetTPS_Accept());
			printf("\nTPS_Send:                   %7d", server->GetTPS_Send());
			printf("\nTPS_Recv:                   %7d", tps_recv);
			printf("\nAuth TPS:                 %7d", tps_auth);
			printf("\nWaiting Queue:              %7lld", waiting.size());
			printf("\nSucc Queue:              %7lld", succ.size());

			printf("\n\n\n**   Hardware   **\n");
			printf("\nProcessorTotal: %6.2f%%      ProcessorUser: %6.2f%%     ProcessorKernel: %6.2f%%", processorTotal, processorUser, processorKernel);
			printf("\nProcessTotal:   %6.2f%%      ProcessUser: %6.2f%%       ProcessKernel: %6.2f%%", processTotal, processorUser, processKernel);
			printf("\nAvaliable Mem:  %8d MB       Process Mem:  %8lld MB     NonPagedMem: %8d MB", availMem, processMem, npMem);
			printf("\nNet Send: %8d KB/s         Net Recv: %8d KB/s", tcpSend, tcpRecv);

			printf("\n-------------------------------------------------------------------------\n");

		}
	}

}

void ServerControl()
{
	if (_kbhit())
	{
		wint_t key = _getwch();
		if (key == 'u' || key == 'U')
		{
			controlMode = true;
			printf("\n\n\n\nControl Mode: Press Q - Quit \n");
			printf("Control Mode: Press L - Key Lock \n");
			printf("Control Mode: Press F - Monitor \n");
			printf("Control Mode: Press P - PROFILE PRINT\n");
			printf("Control Mode: Press R - PROFILE RESET\n");
			printf("Control Mode: Press C - CRASH\n");
			printf("Control Mode: Press B - TOGGLE USE BLACKLIST\n");
			printf("Control Mode: Press K - CLEAR BLACKLIST\n");
		}

		if (controlMode)
		{
			switch (key)
			{
			case 's':
				[[fallthrough]];
			case 'S':
				//printf("accept스탑");
				//server->AcceptPause();
				break;
			case 'l':
				[[fallthrough]];
			case 'L':
				controlMode = false;
				printf("\n\n\n\nControl Lock: Press U - Control Unlock \n");
				break;
			case'f':
				[[fallthrough]];
			case'F':
				control_monitor = !control_monitor;
				printf("\n\n\n\nControl Monitor: %d\n \n", control_monitor);

				break;
			case'q':
				[[fallthrough]];
			case'Q':
				server->Stop();
				isRunning = false;
				break;
			case'p':
				[[fallthrough]];
			case'P':
				Profiler::FlushToFile();
				printf("\n\n\n프로파일러 쓰기 완료\n");
				break;
			case'r':
				[[fallthrough]];
			case'R':
				Profiler::Reset();
				printf("\n\n\n프로파일러 리셋 완료\n");
				break;
			case 'w':
				[[fallthrough]];
			case 'W':
				//server->DisconnectAll();
				break;
			case 'c':
				[[fallthrough]];
			case'C':
				CrashDump::Crash();
				break;
			case 'b':
				[[fallthrough]];
			case'B':
				server->use_blacklist = !server->use_blacklist;
				printf("\n\n\nNow Black List : %d", server->use_blacklist);
				break;
			case 'k':
				[[fallthrough]];
			case'K':
				server->ClearBlacklist();
				break;
			default:
				break;

			}
		}

	}
}