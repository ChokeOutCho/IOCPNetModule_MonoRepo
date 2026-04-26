#pragma comment(lib, "Winmm.lib")

#include <queue>
#include <unordered_map>
#include <stack>

#include <mysql/errmsg.h>

#include "conio.h"
#include "CommonProtocol.h"
#include "Define.h"
#include "NetLibDefine.h"
#include "NetLib_Server.h"
#include "Profiler.h"
#include "Parser.h"
#include "RingBuffer.h"
#include "CrashDump.h"
#include "DBConnector.h"
#include "Client.h";
#include "ClientContents.h"
#include "MonitorTool.h"
#include "MonitorData.h"
#include "SystemMonitor.h"
float WaitForTime(int tick, DWORD* prevTime);
void ServerControl();

int update_tps;
void Monitoring(float deltaTime);
void Login_Timeout(DWORD currentTime);
void Content_Proc(unsigned long long sessionHandle, WORD contentType, Packet* packet);



bool controlMode;
bool control_monitor = true;
long isRunning = true;
SystemMonitor system_monitor;
HANDLE ContentEvent;
class MonitorServer :public NetLib_Server
{
public:
	SRWLOCK lock_waiting;
	std::unordered_map<unsigned long long, WaitingSession> waiting;
	SRWLOCK lock_blacklist;
	std::unordered_map<unsigned long, unsigned short> blacklist;

	SRWLOCK lock_clients_sessionHandle;
	std::unordered_map<unsigned long long, Client*> clients_sessionHandle;
	LockFreeQueue<Packet*> clientContentQueue;
	SRWLOCK lock_monitors_sessionHandle;
	std::unordered_map<unsigned long long, MonitorTool*> monitors;
	SRWLOCK lock_sessions_serverNo;
	std::unordered_map<long, unsigned long long> sessions_ServerNo;
	bool use_blacklist;
	Client* monitorHardware;
	MonitorServer(const WCHAR* openIP, unsigned short openPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, int opt_maxOfSession, bool opt_zerocpy, Opt_Encryption* opt_encryption, int opt_maxOfSendPackets)
		:NetLib_Server(openIP, openPort, opt_workerTH_Pool_size, opt_concurrentTH_size, opt_maxOfSession, opt_zerocpy, opt_encryption, opt_maxOfSendPackets)
	{
		use_blacklist = true;
		InitializeSRWLock(&lock_clients_sessionHandle);
		InitializeSRWLock(&lock_tps_accept_list);
		InitializeSRWLock(&lock_sessions_serverNo);
		InitializeSRWLock(&lock_monitors_sessionHandle);
		InitializeSRWLock(&lock_blacklist);
		InitializeSRWLock(&lock_waiting);

		monitorHardware = new Client(-1, MONITOR_SERVER_NO);
		clients_sessionHandle[monitorHardware->SessionHandle] = monitorHardware;
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
	}

	virtual void OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
	{
		// 클라면 맵에서 빼기
		AcquireSRWLockExclusive(&lock_waiting);
		auto itWating = waiting.find(sessionHandle);
		if (itWating != waiting.end())
		{
			itWating = waiting.erase(itWating);
		}
		ReleaseSRWLockExclusive(&lock_waiting);

		AcquireSRWLockExclusive(&lock_monitors_sessionHandle);
		auto itMonitors = monitors.find(sessionHandle);
		if (itMonitors != monitors.end())
		{
			MonitorTool* monitor = itMonitors->second;
			monitors.erase(itMonitors);
			ReleaseSRWLockExclusive(&lock_monitors_sessionHandle);

			delete monitor;
		}
		else
		{
			ReleaseSRWLockExclusive(&lock_monitors_sessionHandle);

			Packet* msg = Packet::Alloc();
			*msg << (unsigned short)CLIENT_CONTENT_TYPE::LEAVE << sessionHandle;
			clientContentQueue.Enqueue(msg);
		}


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

		update_tps++;

	}
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
MonitorServer* server;

void Content_Proc(unsigned long long sessionHandle, WORD contentType, Packet* packet)
{
	switch (contentType)
	{
	case en_PACKET_SS_MONITOR_LOGIN:
	{

		// waiting에서 제거
		AcquireSRWLockExclusive(&server->lock_waiting);
		auto itWating = server->waiting.find(sessionHandle);
		if (itWating != server->waiting.end())
		{
			itWating = server->waiting.erase(itWating);
		}
		else
		{
			ReleaseSRWLockExclusive(&server->lock_waiting);
			server->Disconnect(sessionHandle);
			return;
		}
		ReleaseSRWLockExclusive(&server->lock_waiting);


		int serverNo;
		*packet >> serverNo;
		if ((serverNo != CHAT_SERVER_NO) &&
			(serverNo != LOGIN_SERVER_NO) &&
			(serverNo != GAME_SERVER_NO))
		{
			server->Disconnect(sessionHandle);
			LOG(L"WRONG_SERVER_NO", LEVEL_DEBUG,
				L"Wrong server no: %d", serverNo);
			return;
		}

		AcquireSRWLockExclusive(&server->lock_sessions_serverNo);
		auto itServerNo = server->sessions_ServerNo.find(serverNo);
		if (itServerNo != server->sessions_ServerNo.end())
		{
			server->Disconnect(sessionHandle);
			ReleaseSRWLockExclusive(&server->lock_sessions_serverNo);
			return;
		}
		else
		{
			server->sessions_ServerNo[serverNo] = sessionHandle;
		}
		ReleaseSRWLockExclusive(&server->lock_sessions_serverNo);

		Client* newClient = new Client(sessionHandle, serverNo);
		AcquireSRWLockExclusive(&server->lock_clients_sessionHandle);
		server->clients_sessionHandle[sessionHandle] = newClient;
		ReleaseSRWLockExclusive(&server->lock_clients_sessionHandle);

		break;
	}

	case en_PACKET_SS_MONITOR_DATA_UPDATE:
	{
		BYTE dataType;
		int dataValue;
		int timestamp;
		*packet >> dataType >> dataValue >> timestamp;
		// 데이터 범위 검사
		if (dataType < dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN || dataType > dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY)
		{
			server->Disconnect(sessionHandle);
			return;
		}
		// 클라이언트가 존재하는 세션인지 검사
		Client* client;
		AcquireSRWLockShared(&server->lock_clients_sessionHandle);
		auto itClients = server->clients_sessionHandle.find(sessionHandle);
		if (itClients != server->clients_sessionHandle.end())
		{
			client = itClients->second;
		}
		else
		{
			server->Disconnect(sessionHandle);
			ReleaseSRWLockShared(&server->lock_clients_sessionHandle);
			return;
		}
		ReleaseSRWLockShared(&server->lock_clients_sessionHandle);

		// 세션이 본인의 클라이언트로 요청했는지 검사
		AcquireSRWLockExclusive(&server->lock_sessions_serverNo);
		auto itSessionsServerNo = server->sessions_ServerNo.find(client->ServerNo);
		if (itSessionsServerNo != server->sessions_ServerNo.end())
		{
			unsigned long long requestSessionHandle = itSessionsServerNo->second;
			if (requestSessionHandle != sessionHandle)
			{
				server->Disconnect(sessionHandle);
				ReleaseSRWLockExclusive(&server->lock_sessions_serverNo);
				return;
			}
		}
		else
		{
			server->Disconnect(sessionHandle);
			ReleaseSRWLockExclusive(&server->lock_sessions_serverNo);
			return;
		}
		ReleaseSRWLockExclusive(&server->lock_sessions_serverNo);

		// 모니터링 툴에 전달
		AcquireSRWLockShared(&server->lock_monitors_sessionHandle);
		for (auto itMonitor = server->monitors.begin(); itMonitor != server->monitors.end(); ++itMonitor)
		{
			MonitorTool* monitor = itMonitor->second;
			Packet* sendpacket = Packet::Alloc();
			*sendpacket << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE << (BYTE)client->ServerNo << dataType << dataValue << timestamp;
			server->SendPacket(monitor->sessionHandle, sendpacket);
			Packet::Free(sendpacket);
		}
		ReleaseSRWLockShared(&server->lock_monitors_sessionHandle);

		// 그대로 큐에 전달
		client->sampleQueue.Enqueue({ dataType, dataValue });
		break;
	}

	case en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN:
	{
		// waiting에서 제거
		AcquireSRWLockExclusive(&server->lock_waiting);
		auto itWating = server->waiting.find(sessionHandle);
		if (itWating != server->waiting.end())
		{
			itWating = server->waiting.erase(itWating);
		}
		else
		{
			ReleaseSRWLockExclusive(&server->lock_waiting);
			server->Disconnect(sessionHandle);
			return;
		}
		ReleaseSRWLockExclusive(&server->lock_waiting);

		char sessionkey[32];
		packet->GetData(sessionkey, 32);
		if (memcmp(sessionkey, MONITOR_KEY, 32) == 0)
		{
			// 성공

			// 근데 이미 있으면?
			// 실패로 하자
			if (server->monitors.size() > 3)
			{
				server->Disconnect(sessionHandle);
				return;
			}

			MonitorTool* newMonitor = new MonitorTool;
			newMonitor->Connect = true;
			newMonitor->sessionHandle = sessionHandle;
			AcquireSRWLockExclusive(&server->lock_monitors_sessionHandle);
			server->monitors[sessionHandle] = newMonitor;
			ReleaseSRWLockExclusive(&server->lock_monitors_sessionHandle);

			Packet* login = Packet::Alloc();
			*login << (WORD)en_PACKET_CS_MONITOR_TOOL_RES_LOGIN << (BYTE)1;
			server->SendPacket(sessionHandle, login);
			Packet::Free(login);


		}
		else
		{
			// 실패
			server->Disconnect(sessionHandle);
		}
		break;
	}
	default:
		server->Disconnect(sessionHandle);
		break;
	}
}

unsigned int __stdcall ClientContentThread(void* params)
{
	DBConnector db("127.0.0.1", "root", "goqudeo2@", "logdb");
	float db_delta_cumulative = 0;
	DWORD prevTime = timeGetTime();
	MonitorDataSample samples[MONITOR_SAMPLE_MAX];
	for (int i = 0; i < MONITOR_SAMPLE_MAX; i++)
	{
		MonitorDataSample* sample = &samples[i];
		sample->Total = 0;
		sample->Count = 0;
		sample->Max = INT_MIN;
		sample->Min = INT_MAX;
	}

	while (1)
	{
		float deltaTime = WaitForTime(1000, &prevTime);
		// 타임아웃

		DWORD currentTime = timeGetTime();
		Login_Timeout(currentTime);

		// 스레드 큐 처리기
		while (1)
		{
			Packet* msg;
			if (server->clientContentQueue.Dequeue(msg) == -1)
				break;

			unsigned short type;
			*msg >> type;
			unsigned long long sessionHandle;
			*msg >> sessionHandle;
			switch (type)
			{
			case CLIENT_CONTENT_TYPE::ENTER:
			{
				break;
			}
			// 퇴장만 관리
			case CLIENT_CONTENT_TYPE::LEAVE:
			{
				Client* client = nullptr;
				AcquireSRWLockExclusive(&server->lock_clients_sessionHandle);
				auto itClients = server->clients_sessionHandle.find(sessionHandle);
				if (itClients != server->clients_sessionHandle.end())
				{
					client = itClients->second;
					server->clients_sessionHandle.erase(itClients);
				}
				ReleaseSRWLockExclusive(&server->lock_clients_sessionHandle);

				if (client != nullptr)
				{
					AcquireSRWLockExclusive(&server->lock_sessions_serverNo);
					server->sessions_ServerNo.erase(client->ServerNo);
					ReleaseSRWLockExclusive(&server->lock_sessions_serverNo);
					delete client;
				}
				break;
			}
			default:
				server->Disconnect(sessionHandle);
				break;
			}

			Packet::Free(msg);
		}

		AcquireSRWLockShared(&server->lock_clients_sessionHandle);
		// 클라이언트 샘플큐 처리기
		for (auto it = server->clients_sessionHandle.begin(); it != server->clients_sessionHandle.end(); ++it)
		{
			// 데이터 집계
			Client* client = it->second;

			int remain = client->sampleQueue.GetUseSize();
			while (remain-- > 0)
			{
				MonitorData monitordata;
				it->second->sampleQueue.Dequeue(monitordata);

				MonitorDataSample* sample = &samples[monitordata.Type];
				int value = monitordata.Value;
				sample->Total += value;
				if (value < sample->Min)
					sample->Min = value;
				else if (value > sample->Max)
					sample->Max = value;
				sample->Count++;
			}
		}
		ReleaseSRWLockShared(&server->lock_clients_sessionHandle);

		// DB에 작성
		db_delta_cumulative += deltaTime;
		if (db_delta_cumulative >= DB_WRITE_FREQUENCY)
		{
			db_delta_cumulative = 0;
			for (int i = 1; i < MONITOR_SAMPLE_MAX; i++)
			{
				MonitorDataSample* sample = &samples[i];
				if (sample->Total == 0)
					continue;

				int sum_min_max = sample->Min + sample->Max;
				double avg;
				if (sample->Count > 2)
					avg = (sample->Total - sum_min_max) / (sample->Count - 2);
				else
					avg = sample->Total / sample->Count;

				int serverNo;
				if (i <= 6)
					serverNo = 20;
				else if (i >= 10 && i <= 23)
					serverNo = 30;
				else if (i >= 30 && i <= 37)
					serverNo = 10;
				else if (i >= 40 && i <= 44)
					serverNo = 40;
				// 에러뜨면 테이블만들고
				// 다시 인서트
				char buf[16];
				time_t t = time(nullptr);
#pragma warning(push)
#pragma warning(disable:4996)
				strftime(buf, sizeof(buf), "%Y%m", localtime(&t));
#pragma warning(pop)
				std::string tableName = "monitorlog_" + std::string(buf);

				std::string query = "INSERT INTO " + tableName + " (logtime, serverno, type, avr, min, max) VALUES(NOW(), " +
					std::to_string(serverNo) + ", " +
					std::to_string(i) + ", " +
					std::to_string(avg) + ", " +
					std::to_string(sample->Min) + ", " +
					std::to_string(sample->Max) + ")";

				if (db.Write(query) < 0)
				{
					long err = db.GetLastErrorNo();
					if (err == 1146)
					{
						std::string createQuery = "CREATE TABLE " + tableName + " LIKE monitorlog_template";
						if (db.Write(createQuery) < 0)
						{
							printf("Create Failed: %d", db.GetLastErrorNo());
						}
						if (db.Write(query) < 0)
						{
							DebugBreak();
						}

					}
				}

				sample->Total = 0;
				sample->Count = 0;
				sample->Max = INT_MIN;
				sample->Min = INT_MAX;
			}
		}

	}
}


int main()
{
	// 초기화
	DWORD prevTime = timeGetTime();


	int opt_serverport;
	int opt_poolsize;
	int opt_concurrentsize;
	int opt_maxofsession;
	bool opt_encryption;
	bool opt_zerocpy;
	int opt_encryption_header_code;
	int opt_encryption_fixed_key;
	{
		Parser parser;
		if (parser.loadFromFile("monitor_config.txt"))
		{
			opt_serverport = parser.GetInt("port");
			opt_poolsize = parser.GetInt("workerTH_Pool_size");
			opt_concurrentsize = parser.GetInt("concurrentTH_size");
			opt_maxofsession = parser.GetInt("maxofsession");
			opt_encryption = parser.GetBool("encryption");

			opt_encryption_header_code = (char)parser.GetInt("encryption_header_code");
			opt_encryption_fixed_key = (char)parser.GetInt("encryption_fixed_key");

			opt_zerocpy = parser.GetBool("zerocopy");
		}
		else
		{
			printf("config파일이 없습니다!!!");
			wint_t key = _getwch();
			return -1;
		}
	}


	ContentEvent = CreateEvent(0, 0, 0, 0);
	HANDLE contentTh = (HANDLE)_beginthreadex(nullptr, 0, ClientContentThread, 0, 0, 0);
	Opt_Encryption opt_encrypt = { opt_encryption_header_code , opt_encryption_fixed_key };
	server = new MonitorServer(L"0.0.0.0", opt_serverport, opt_poolsize, opt_concurrentsize, opt_maxofsession, opt_zerocpy, &opt_encrypt, 100);

	server->Start();


	while (isRunning)
	{
		float deltaTime = WaitForTime(1000, &prevTime);
		Monitoring(deltaTime);
		ServerControl();


	} // end while
}

void Login_Timeout(DWORD currentTime)
{
	std::vector<unsigned long long> loginTimeoutSessions;

	AcquireSRWLockShared(&server->lock_waiting);
	for (const auto& [sessionHandle, session] : server->waiting)
	{
		DWORD acceptTime = session.acceptTime;
		if (currentTime < acceptTime) continue;

		DWORD elapsed = currentTime - acceptTime;
		if (elapsed > LOGIN_TIME_OUT)
		{
			LOG(L"LOGIN_TIMEOUT", LEVEL_DEBUG,
				L"LoginTimeout session=%llu acceptTime=%u elapsed=%u",
				sessionHandle, acceptTime, elapsed);

			loginTimeoutSessions.push_back(sessionHandle);
		}
	}
	ReleaseSRWLockShared(&server->lock_waiting);

	for (auto session : loginTimeoutSessions)
	{
		server->Disconnect(session);
	}
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
		long long npMem = system_monitor.GetNonPagedMB();
		int netSend = (int)(system_monitor.GetTotalSendKBps());
		int netRecv = (int)(system_monitor.GetTotalRecvKBps());
		float processorTotal = system_monitor.ProcessorTotal();
		float processorUser = system_monitor.ProcessorKernel();
		float processorKernel = system_monitor.ProcessorKernel();
		float processTotal = system_monitor.ProcessTotal();
		float processUser = system_monitor.ProcessUser();
		float processKernel = system_monitor.ProcessKernel();

		long sessionCount = server->GetSessionCount();
		long packetPoolSize = server->GetPacketUseSize();
		long tps_recv = server->GetTPS_Recv();

		Packet* cputotal = Packet::Alloc();
		Packet* npmem = Packet::Alloc();
		Packet* netrecv = Packet::Alloc();
		Packet* netsend = Packet::Alloc();
		Packet* availmem = Packet::Alloc();

		*cputotal << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE << (BYTE)SERVER_NO << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL << (int)processorTotal << timestamp;
		*npmem << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE << (BYTE)SERVER_NO << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_NONPAGED_MEMORY << (int)npMem << timestamp;
		*netrecv << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE << (BYTE)SERVER_NO << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_NETWORK_RECV << (int)netRecv << timestamp;
		*netsend << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE << (BYTE)SERVER_NO << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_NETWORK_SEND << (int)netSend << timestamp;
		*availmem << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE << (BYTE)SERVER_NO << (BYTE)dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY << (int)availMem << timestamp;

		int cnt = 0;
		unsigned long long handlesForMulticast[10];
		AcquireSRWLockShared(&server->lock_monitors_sessionHandle);
		for (auto itMonitor = server->monitors.begin(); itMonitor != server->monitors.end(); ++itMonitor)
		{
			handlesForMulticast[cnt++] = itMonitor->second->sessionHandle;

		}
		ReleaseSRWLockShared(&server->lock_monitors_sessionHandle);

		if (cnt != 0)
		{
			server->SendPacketMulticast(handlesForMulticast, cnt, cputotal);
			server->SendPacketMulticast(handlesForMulticast, cnt, npmem);
			server->SendPacketMulticast(handlesForMulticast, cnt, netrecv);
			server->SendPacketMulticast(handlesForMulticast, cnt, netsend);
			server->SendPacketMulticast(handlesForMulticast, cnt, availmem);
		}

		Packet::Free(cputotal);
		Packet::Free(npmem);
		Packet::Free(netrecv);
		Packet::Free(netsend);
		Packet::Free(availmem);

		server->monitorHardware->sampleQueue.Enqueue({ dfMONITOR_DATA_TYPE_MONITOR_CPU_TOTAL, (int)processorTotal });
		server->monitorHardware->sampleQueue.Enqueue({ dfMONITOR_DATA_TYPE_MONITOR_NONPAGED_MEMORY, (int)npMem });
		server->monitorHardware->sampleQueue.Enqueue({ dfMONITOR_DATA_TYPE_MONITOR_NETWORK_RECV, (int)netRecv });
		server->monitorHardware->sampleQueue.Enqueue({ dfMONITOR_DATA_TYPE_MONITOR_NETWORK_SEND, (int)netSend });
		server->monitorHardware->sampleQueue.Enqueue({ dfMONITOR_DATA_TYPE_MONITOR_AVAILABLE_MEMORY, (int)availMem });

		if (control_monitor)
		{
			printf("\n-------------------------------------------------------------------------\n");
			printf("\n\n**   MONITOR SERVER   **\n\n");
			printf("Running Time:  %20.2f  sec\n", runningtime);
			printf("\nSession Count:              %7d", sessionCount);
			printf("\nPacket Pool Use:            %7d", packetPoolSize);
			printf("\nTotal_Accept:               %7d", server->GetTotal_Accept());
			printf("\nTPS_Accept:                 %7d", server->GetTPS_Accept());
			printf("\nTPS_Send:                   %7d", server->GetTPS_Send());
			printf("\nTPS_Recv:                   %7d", tps_recv);
			printf("\nWaiting Queue:              %7lld", server->waiting.size());

			printf("\n\n\n**   Hardware   **\n");
			printf("\nProcessorTotal: %6.2f%%      ProcessorUser: %6.2f%%     ProcessorKernel: %6.2f%%", processorTotal, processorUser, processorKernel);
			printf("\nProcessTotal:   %6.2f%%      ProcessUser: %6.2f%%       ProcessKernel: %6.2f%%", processTotal, processorUser, processKernel);
			printf("\nAvaliable Mem:  %8d MB       Process Mem:  %8lld MB     NonPagedMem: %8lld MB", availMem, processMem, npMem);
			printf("\nNet Send: %8d KB/s         Net Recv: %8d KB/s", netSend, netRecv);

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