#pragma comment(lib, "Winmm.lib")
#include <cpp_redis/cpp_redis>
#include <queue>
#include <unordered_map>
#include <stack>
#include "conio.h"
#include "MonitorClient.h"
#include "NetLibDefine.h"
#include "NetLib_Server.h"
#include "Profiler.h"
#include "Parser.h"
#include "RingBuffer.h"
#include "SystemProtocol.h"
#include "Define.h"
#include "Player.h"
#include "SectorMap.h"
#include "CommonProtocol.h"
#include "CrashDump.h"
#include "SystemMonitor.h"
float WaitForTime(int tick, DWORD* prevTime);
void ServerControl();

void Monitoring(float deltaTime);
void Login_Timeout(DWORD currentTime);
void Timeout(DWORD currentTime);
void Message_Proc(unsigned long long sessionHandle, WORD messageType, Packet* packet);
void Content_Proc(unsigned long long sessionHandle, WORD contentType, Packet* packet);
__inline void SendAroundSector(Player* player, Packet* packet, bool sendMe);

SRWLOCK lock_waiting;
std::unordered_map<unsigned long long, WaitingSession> waiting;

SRWLOCK lock_players_sessionHandle;
std::unordered_map<unsigned long long, Player*> players_sessionHandle;

SRWLOCK lock_players_accNo;
std::unordered_map<long long, unsigned long long> sessions_accNo;

SRWLOCK lock_blacklist;
std::unordered_map<unsigned long, unsigned short> blacklist;

SectorMap sectorMap;

cpp_redis::client* redisClient;
MonitorClient* monitorClient;
HANDLE ContentEvent;
SystemMonitor system_monitor;
long tps_msg_req_max;
long tps_mov_req_max;

class ChatServer :public NetLib_Server
{
public:
	bool use_blacklist;
	long update_msg;
	ChatServer(const WCHAR* openIP, unsigned short openPort, int opt_workerTH_Pool_size, int opt_concurrentTH_size, int opt_maxOfSession, bool opt_zerocpy, Opt_Encryption* opt_encryption, int opt_maxOfSendPackets)
		:NetLib_Server(openIP, openPort, opt_workerTH_Pool_size, opt_concurrentTH_size, opt_maxOfSession, opt_zerocpy, opt_encryption, opt_maxOfSendPackets)
	{
		m_sendfull_total = 0;
		InitializeSRWLock(&lock_tps_accept_list);
		use_blacklist = true;
		update_msg = 0;
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
		PROFILING("SESSION_LEAVE");

		unsigned long long accNo = 0;

		// 로그인 전인가?
		AcquireSRWLockExclusive(&lock_waiting);
		auto itWating = waiting.find(sessionHandle);
		if (itWating != waiting.end())
		{
			itWating = waiting.erase(itWating);
		}
		ReleaseSRWLockExclusive(&lock_waiting);

		// 로그인 후인가?
		AcquireSRWLockExclusive(&lock_players_sessionHandle);
		auto it = players_sessionHandle.find(sessionHandle);
		if (it != players_sessionHandle.end())
		{
			Player* player = it->second;
			players_sessionHandle.erase(it);
			// 섹터 맵에서도 제거
			ReleaseSRWLockExclusive(&lock_players_sessionHandle);

			Sector* playerSector = player->curSector;
			AcquireSRWLockExclusive(&playerSector->lock);
			playerSector->players.remove(player);
			ReleaseSRWLockExclusive(&playerSector->lock);

			// players_accNo에서도 제거
			AcquireSRWLockExclusive(&lock_players_accNo);
			auto it2 = sessions_accNo.find(player->AccNo);
			if (it2 != sessions_accNo.end())
			{
				if (it2->second == sessionHandle)
				{
					accNo = player->AccNo;
					sessions_accNo.erase(it2);
				}
			}
			ReleaseSRWLockExclusive(&lock_players_accNo);


			Player::DeletePlayer(player);
		}
		else
		{
			ReleaseSRWLockExclusive(&lock_players_sessionHandle);
		}

		if (code != SESSION_LEAVE_CODE::NONE)
		{
			if (code == SESSION_LEAVE_CODE::SEND_FULL)
			{
				long sendfullTotal = InterlockedIncrement(&m_sendfull_total);
				LOG(L"SendFull", LEVEL_DEBUG, L"Send queue has full. acc: %llu, total: %d", accNo, sendfullTotal);
			}
			else if (code == WRONG_HEADER_LEN || code == WRONG_HEADER_CODE || code == WRONG_HEADER_CHECKSUM || code == WRONG_RECVPOST_COUNT)
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
		InterlockedIncrement(&update_msg);
	}
	long m_sendfull_total;

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
ChatServer* server;

bool controlMode;
bool control_monitor = true;
long isRunning = true;


unsigned int __stdcall ContentThread(void* params)
{
	while (1)
	{
		WaitForSingleObject(ContentEvent, 1000);
		DWORD currentTime = timeGetTime();
		// 인증 타임아웃
		Login_Timeout(currentTime);
		// 타임아웃
		Timeout(currentTime);
		server->Collect_AcceptTPS();
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

void Timeout(DWORD currentTime)
{
	std::vector<unsigned long long> timeoutSessions;

	AcquireSRWLockShared(&lock_players_sessionHandle);
	for (const auto& [sessionHandle, player] : players_sessionHandle)
	{
		DWORD lastRecv = player->lastRecv;
		if (currentTime < lastRecv) continue;

		DWORD elapsed = currentTime - lastRecv;
		if (elapsed > HEARTBEAT_TIME_OUT_MS)
		{
			LOG(L"TIMEOUT", LEVEL_DEBUG,
				L"Timeout session=%llu accNo=%lld elapsed=%u",
				sessionHandle, player->AccNo, elapsed);

			timeoutSessions.push_back(sessionHandle);
		}

		long tps_msg_req = InterlockedExchange(&player->tps_req_chat, 0);
		long tps_mov_req = InterlockedExchange(&player->tps_req_sectorMove, 0);
		if (tps_msg_req > tps_msg_req_max)
		{
			tps_msg_req_max = tps_msg_req;
			LOG(L"TPS_MSG_REQ", LEVEL_DEBUG,
				L"update max message acc: %llu req: %d", player->AccNo,
				tps_msg_req);
			if (tps_msg_req > MAX_CONTENT_MSG)
			{
				WCHAR IPstr[32];
				NetLib_Helper::IPToWstring(player->IP, IPstr, 32);
				LOG(L"WRONG_CONTENT_TPS_MSG", LEVEL_DEBUG,
					L"Wrong accNo: %d   ip: %s    ,msg_tps: %d", player->AccNo, IPstr, tps_msg_req);
				server->Disconnect(sessionHandle);
				//server->InsertBlacklist(player->IP, 0);
				continue;
			}
		}
		if (tps_mov_req > tps_mov_req_max)
		{
			tps_mov_req_max = tps_mov_req;
			LOG(L"TPS_MOV_REQ", LEVEL_DEBUG,
				L"update max message acc: %llu req: %d", player->AccNo,
				tps_mov_req);
			if (tps_mov_req > MAX_CONTENT_MOV)
			{
				WCHAR IPstr[32];
				NetLib_Helper::IPToWstring(player->IP, IPstr, 32);
				LOG(L"WRONG_CONTENT_TPS_MOV", LEVEL_DEBUG,
					L"Wrong accNo: %d   ip: %s    ,mov_tps: %d", player->AccNo, IPstr, tps_msg_req);
				server->Disconnect(sessionHandle);
				//server->InsertBlacklist(player->IP, 0);
				continue;
			}
		}
	}
	ReleaseSRWLockShared(&lock_players_sessionHandle);

	for (auto session : timeoutSessions)
	{
		server->Disconnect(session);
	}

}

void Message_Proc(unsigned long long sessionHandle, WORD messageType, Packet* packet)
{
	switch (messageType)
	{
	case SystemPacketType::SESSION_JOIN:
	{
	}
	break;

	case SystemPacketType::SESSION_LEAVE:
	{

	}
	break;

	case SystemPacketType::CONTENT_PROC:
	{
		PROFILING("CONTENT");

		WORD contentType;
		*packet >> contentType;
		Content_Proc(sessionHandle, contentType, packet);


	}
	break;

	default:
		server->Disconnect(sessionHandle);
		break;
	}
}
// RPC로 설계하면 좋겠는데 카피가 너무 많다.
// 이 단에서 끊을 때에는 Disconnect로 세션떠남을 유도해서 플레이어를 제거하도록 한다.
void Content_Proc(unsigned long long sessionHandle, WORD contentType, Packet* packet)
{
	DWORD currentTime = timeGetTime();

	switch (contentType)
	{
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_LOGIN:
	{
		PROFILING("CONTENT_LOGIN");
		unsigned long long accNo;
		WCHAR id[ID_LENGTH];
		WCHAR nickname[NICKNAME_LENGTH];
		char sessionKey[SESSIONKEY_LENGTH];
		*packet >> accNo;
		packet->GetData((char*)id, sizeof(WCHAR) * ID_LENGTH);
		packet->GetData((char*)nickname, sizeof(WCHAR) * NICKNAME_LENGTH);
		packet->GetData((char*)sessionKey, SESSIONKEY_LENGTH);

		unsigned char res_login = 0;
		unsigned long IP;
		unsigned short port;
		// 대기열에서 제거
		AcquireSRWLockExclusive(&lock_waiting);
		auto it = waiting.find(sessionHandle);
		if (it != waiting.end())
		{
			IP = it->second.IP;
			port = it->second.port;
			waiting.erase(it);
			ReleaseSRWLockExclusive(&lock_waiting);
		}
		else
		{
			// 대기열에 없는데 로그인?
			// 한 세션에서 로그인패킷 두 개 (중복이거나 다른 계정으로)
			// 끊어내자.
			ReleaseSRWLockExclusive(&lock_waiting);
			server->Disconnect(sessionHandle);
			return;
		}

		// Redis 접근
		auto reply_future = redisClient->get(std::to_string(accNo));
		redisClient->sync_commit();
		auto reply = reply_future.get();
		if (reply.is_null())
		{
			// 없으면 끊기
			server->Disconnect(sessionHandle);
			return;
		}

		std::string redis_data = reply.as_string();

		if (redis_data.size() == 64 &&
			memcmp(redis_data.data(), sessionKey, SESSIONKEY_LENGTH) == 0)
		{
			//std::cout << "같다\n";
		}
		else
		{
			server->Disconnect(sessionHandle);
			return;
		}



		// 중복 로그인 검사 후 플레이어 생성
		AcquireSRWLockExclusive(&lock_players_accNo);
		auto it_acc = sessions_accNo.find(accNo);
		if (it_acc != sessions_accNo.end())
		{
			// 중복 로그인패킷 감지
			unsigned long long playerSessionHandle = it_acc->second;
			Player* player = Player::CreatePlayer(sessionHandle, accNo);
			sessions_accNo[accNo] = sessionHandle;
			ReleaseSRWLockExclusive(&lock_players_accNo);
			server->Disconnect(playerSessionHandle);
			//LOG(L"LOGIN", LEVEL_DEBUG,
			//	L"Try duplicate login. req sessionHandle: %llu / player sessionHandle: %llu / playeraccNo: %llu",
			//	sessionHandle, playerSessionHandle, accNo);

			wcscpy_s(player->ID, ID_LENGTH, id);
			wcscpy_s(player->Nickname, ID_LENGTH, nickname);
			memcpy_s(player->SessionKey, SESSIONKEY_LENGTH, sessionKey, SESSIONKEY_LENGTH);
			player->AccNo = accNo;
			player->oldSector = &sectorMap.voidSector;
			player->curSector = &sectorMap.voidSector;
			player->lastRecv = currentTime;
			player->IP = IP;
			player->port = port;
			res_login = 1;
			Packet* resPacket = Packet::Alloc();
			*resPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << res_login << accNo;
			server->SendPacket(sessionHandle, resPacket);
			Packet::Free(resPacket);

			AcquireSRWLockExclusive(&lock_players_sessionHandle);
			players_sessionHandle[sessionHandle] = player;
			ReleaseSRWLockExclusive(&lock_players_sessionHandle);
		}
		else // 더미환경 특수 처리. 후에 요청한 로그인을 받는다.
		{
			Player* player = Player::CreatePlayer(sessionHandle, accNo);
			sessions_accNo[accNo] = sessionHandle;
			ReleaseSRWLockExclusive(&lock_players_accNo);
			wcscpy_s(player->ID, ID_LENGTH, id);
			wcscpy_s(player->Nickname, ID_LENGTH, nickname);
			memcpy_s(player->SessionKey, SESSIONKEY_LENGTH, sessionKey, SESSIONKEY_LENGTH);
			player->AccNo = accNo;
			player->oldSector = &sectorMap.voidSector;
			player->curSector = &sectorMap.voidSector;
			player->lastRecv = currentTime;
			player->IP = IP;
			player->port = port;
			res_login = 1;
			Packet* resPacket = Packet::Alloc();
			*resPacket << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << res_login << accNo;
			server->SendPacket(sessionHandle, resPacket);
			Packet::Free(resPacket);

			AcquireSRWLockExclusive(&lock_players_sessionHandle);
			players_sessionHandle[sessionHandle] = player;
			ReleaseSRWLockExclusive(&lock_players_sessionHandle);

		}
	}

	break;
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
	{
		// 요구하는 섹터로 넣어주기
		PROFILING("CONTENT_SECTORMOVE");

		unsigned long long accNo;
		*packet >> accNo;

		AcquireSRWLockShared(&lock_players_sessionHandle);
		auto it = players_sessionHandle.find(sessionHandle);
		if (it != players_sessionHandle.end())
		{
			Player* player = it->second;
			ReleaseSRWLockShared(&lock_players_sessionHandle);

			if (accNo != player->AccNo)
			{
				server->Disconnect(sessionHandle);
				break;
			}

			WORD x, y;
			*packet >> x >> y;
			if (x >= SECTOR_MAX_X || x < 0 || y >= SECTOR_MAX_Y || y < 0)
			{
				server->Disconnect(sessionHandle);
				break;
			}

			sectorMap.PlayerSectorUpdate(player, x, y);

			Packet* resPacket = Packet::Alloc();
			*resPacket << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE << accNo << x << y;
			server->SendPacket(sessionHandle, resPacket);
			Packet::Free(resPacket);
			player->lastRecv = currentTime;
			InterlockedIncrement(&player->tps_req_sectorMove);

		}
		else
		{
			ReleaseSRWLockShared(&lock_players_sessionHandle);
			server->Disconnect(sessionHandle);
		}

	}
	break;
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_MESSAGE:
	{
		// 요구하는 채팅 뿌려주기
		PROFILING("CONTENT_MESSAGE");

		unsigned long long accNo;
		*packet >> accNo;
		AcquireSRWLockShared(&lock_players_sessionHandle);

		auto it = players_sessionHandle.find(sessionHandle);
		if (it != players_sessionHandle.end())
		{
			Player* player = it->second;
			ReleaseSRWLockShared(&lock_players_sessionHandle);
			if (accNo != player->AccNo)
			{
				server->Disconnect(sessionHandle);
				break;
			}
			WORD messageLen;
			WCHAR message[MSG_LENGTH];
			*packet >> messageLen;
			if (messageLen > MSG_LENGTH)
			{
				server->Disconnect(sessionHandle);
				LOG(L"DetectAttack_MSG", LEVEL_DEBUG, L"Detected attack - MSG_LENGTH: %d", messageLen);
				break;
			}
			packet->GetData((char*)message, sizeof(WCHAR) * messageLen);

			Packet* resPacket = Packet::Alloc();
			*resPacket << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE;
			*resPacket << player->AccNo;
			resPacket->PutData((char*)player->ID, sizeof(WCHAR) * ID_LENGTH);
			resPacket->PutData((char*)player->Nickname, sizeof(WCHAR) * NICKNAME_LENGTH);
			*resPacket << messageLen;
			resPacket->PutData((char*)message, sizeof(WCHAR) * messageLen);

			SendAroundSector(player, resPacket, true);
			Packet::Free(resPacket);
			player->lastRecv = currentTime;
			InterlockedIncrement(&player->tps_req_chat);
		}
		else
		{
			ReleaseSRWLockShared(&lock_players_sessionHandle);
			server->Disconnect(sessionHandle);

		}



	}
	break;
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_HEARTBEAT:
	{
		auto it = players_sessionHandle.find(sessionHandle);
		if (it != players_sessionHandle.end())
		{
			Player* player = it->second;
			player->lastRecv = currentTime;

		}
		else
		{
			server->Disconnect(sessionHandle);
		}

	}

	break;

	default:
		server->Disconnect(sessionHandle);
		break;
	}
}

void SendAroundSector(Player* player, Packet* packet, bool sendMe)
{
	PROFILING("SEND_AROUND_SECTOR");

	unsigned long long handlesForMulticast[500];

	Sector* curSector = player->curSector;
	std::list<Sector*>::iterator sectorBegin, sectorEnd;

	sectorMap.AcquireAroundSectorsShared(curSector);
	curSector->GetAroundSectors(sectorBegin, sectorEnd);
	int cnt = 0;
	{
		PROFILING("PLAYER_COPY");
		for (; sectorBegin != sectorEnd; ++sectorBegin)
		{
			std::list<Player*>::iterator playerBegin, playerEnd;
			(*sectorBegin)->GetPlayers(playerBegin, playerEnd);
			for (; playerBegin != playerEnd; ++playerBegin)
			{
				if (sendMe == false && *playerBegin == player)
					continue;

				handlesForMulticast[cnt] = (*playerBegin)->GetSessionHandle();
				cnt++;
			}
			ReleaseSRWLockShared(&(*sectorBegin)->lock);
		}
	}

	if (cnt >= 500)
	{
		LOG(L"HandleForMulticast", LEVEL_DEBUG, L"사람이 너무 많이 모임 핸들 버퍼를 늘려야 함.");
		DebugBreak();
	}

	if (cnt != 0)
		server->SendPacketMulticast(handlesForMulticast, cnt, packet);


}

int main()
{
	InitializeSRWLock(&Packet::lock_debuglist);
	InitializeSRWLock(&lock_players_accNo);
	InitializeSRWLock(&lock_players_sessionHandle);
	InitializeSRWLock(&lock_waiting);
	InitializeSRWLock(&lock_blacklist);

	DWORD prevTime = timeGetTime();
	int opt_serverport;
	int opt_poolsize;
	int opt_concurrentsize;
	int opt_maxofsession;
	bool opt_encryption;
	bool opt_zerocpy;
	int opt_encryption_header_code;
	int opt_encryption_fixed_key;
	int opt_sendbuf;
	{
		Parser parser;
		if (parser.loadFromFile("chat_config.cfg"))
		{
			opt_serverport = parser.GetInt("port");
			opt_poolsize = parser.GetInt("workerTH_Pool_size");
			opt_concurrentsize = parser.GetInt("concurrentTH_size");
			opt_maxofsession = parser.GetInt("maxofsession");
			opt_encryption = parser.GetBool("encryption");

			opt_encryption_header_code = (char)parser.GetInt("encryption_header_code");
			opt_encryption_fixed_key = (char)parser.GetInt("encryption_fixed_key");

			opt_zerocpy = parser.GetBool("zerocopy");

			opt_sendbuf = parser.GetInt("sendbuf");
		}
		else
		{
			printf("config파일이 없습니다!!!");
			wint_t key = _getwch();
			return -1;
		}
	}

	ContentEvent = CreateEvent(0, 0, 0, 0);
	HANDLE contentTh = (HANDLE)_beginthreadex(nullptr, 0, ContentThread, 0, 0, 0);
	Opt_Encryption opt_encrypt = { opt_encryption_header_code , opt_encryption_fixed_key };
	server = new ChatServer(L"0.0.0.0", opt_serverport, opt_poolsize, opt_concurrentsize, opt_maxofsession, opt_zerocpy, &opt_encrypt, opt_sendbuf);

	server->Start();
	printf("Server Start\n");
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
	delete redisClient;
	delete monitorClient;
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
		//////////////////////////////////////////
		/////////////////////// 모니터링 서버 전송
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		int timestamp = static_cast<int>(t);

		system_monitor.UpdateCpuTime();
		runningtime += delta_cumulative;
		delta_cumulative = 0;
		long availMem = system_monitor.GetAvailMemMB();
		long long processMem = system_monitor.GetProcessPrivateMB();
		long npMem = system_monitor.GetNonPagedMB();
		long netSend = (long)system_monitor.GetTotalSendKBps();
		long netRecv = (long)system_monitor.GetTotalRecvKBps();
		float processorTotal = system_monitor.ProcessorTotal();
		float processorUser = system_monitor.ProcessorKernel();
		float processorKernel = system_monitor.ProcessorKernel();
		float processTotal = system_monitor.ProcessTotal();
		float processUser = system_monitor.ProcessUser();
		float processKernel = system_monitor.ProcessKernel();

		long sessionCount = server->GetSessionCount();
		long packetPoolSize = server->GetPacketUseSize();
		long playerCount = players_sessionHandle.size();
		long tps_recv = server->GetTPS_Recv();
		long tps_send = server->GetTPS_Send();
		long updatemsg = InterlockedExchange(&server->update_msg, 0);
		if (monitorClient->IsConnect() == true)
		{
			Packet* run = Packet::Alloc();
			*run << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN << 1 << timestamp;
			monitorClient->SendPacket(run);
			Packet::Free(run);

			Packet* cpu = Packet::Alloc();
			Packet* mem = Packet::Alloc();
			Packet* session = Packet::Alloc();
			Packet* player = Packet::Alloc();
			Packet* updateTPS = Packet::Alloc();
			Packet* packetPool = Packet::Alloc();
			Packet* sendTPS = Packet::Alloc();

			*cpu << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU << (int)processTotal << timestamp;
			*mem << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM << (int)processMem << timestamp;
			*session << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SESSION << (int)sessionCount << timestamp;
			*player << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_PLAYER << (int)playerCount << timestamp;
			*updateTPS << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS << (int)updatemsg << timestamp;
			*packetPool << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL << (int)packetPoolSize << timestamp;
			*sendTPS << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << (BYTE)en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL << (int)tps_send << timestamp;

			monitorClient->SendPacket(cpu);
			monitorClient->SendPacket(mem);
			monitorClient->SendPacket(session);
			monitorClient->SendPacket(player);
			monitorClient->SendPacket(updateTPS);
			monitorClient->SendPacket(packetPool);
			monitorClient->SendPacket(sendTPS);

			Packet::Free(cpu);
			Packet::Free(mem);
			Packet::Free(session);
			Packet::Free(player);
			Packet::Free(updateTPS);
			Packet::Free(packetPool);
			Packet::Free(sendTPS);
		}

		

		//////////////////////////////////////////
		/////////////////////// 콘솔 출력
		if (control_monitor)
		{
			printf("\n-------------------------------------------------------------------------");
			printf("\n\n**   CHAT SERVER   **\n\n");
			printf("Running Time:  %20.2f  sec\n", runningtime);
			printf("\nMonitor Server Connect: ");

			if (monitorClient->IsConnect() == true) printf("TRUE");
			else printf("FALSE");

			printf("\n\nSession Count:              %7d", sessionCount);
			printf("\nPacket Pool Use:            %7d", packetPoolSize);
			printf("\nTotal_Accept:               %7d", server->GetTotal_Accept());
			printf("\nTPS_Accept:                 %7d", server->GetTPS_Accept());
			printf("\nTPS_Send:                   %7d", tps_send);
			printf("\nTPS_Recv:                   %7d", tps_recv);
			printf("\nWaiting Queue:              %7lld", waiting.size());

			printf("\n\nPlayer Count Auth/Session:    %7lld       /%7ld", sessions_accNo.size(), playerCount);
			printf("\nPlayer Pool Use:              %7d", Player::PlayerPoolCurrentSize());

			printf("\n\n\n**   Hardware   **\n");
			printf("\nProcessorTotal: %6.2f%%      ProcessorUser: %6.2f%%     ProcessorKernel: %6.2f%%", processorTotal, processorUser, processorKernel);
			printf("\nProcessTotal:   %6.2f%%      ProcessUser: %6.2f%%       ProcessKernel: %6.2f%%", processTotal, processorUser, processKernel);
			printf("\nAvaliable Mem:  %8d MB       Process Mem:  %8lld MB     NonPagedMem: %8d MB", availMem, processMem, npMem);
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