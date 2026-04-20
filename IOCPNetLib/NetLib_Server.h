#pragma once
#include "pch.h"

#include "Session.h"
#include "../Utils/SimpleEncoder.h"
#include "../Utils/LockFreeStack.h"
#include "../Utils/ObjectPool.h"
#include "Packet.h"
#include "../Utils/Profiler.h"
#include "NetLibDefine.h"
#include "NetLibraryProtocol.h"
#include "../Utils/LockFreeQueue.h"

#include "NetLib_Content.h"
#include "NetLib_Helper.h"
class NetLib_Server
{
public:
	NetLib_Server(
		const WCHAR* openIP, // 리슨 소켓에 바인드 할 IP
		unsigned short openPort, // 리슨 소켓에 바인드 할 Port
		int opt_workerTH_Pool_size, //IOCP에서 사용할 워커 스레드의 총 개수
		int opt_concurrentTH_size, // IOCP에서 사용할 동시 동작 가능한 워커 스레드 개수
		unsigned short opt_maxOfSession, // 최대 세션 수
		bool opt_zerocpy, // 제로카피 옵션
		Opt_Encryption* opt_encryption, // 난독화 옵션
		int opt_maxOfSendPackets); // 송신 버퍼 사이즈 제한 (패킷 개수)

	virtual ~NetLib_Server();
	// 서버 가동
	void Start();
	// 서버 중단
	void Stop();

	// 현재 패킷 풀 사용량 읽기
	__inline int GetPacketUseSize() { return Packet::GetPoolUseSize(); }
	// 현재 연결된 세션 사이즈 읽기
	__inline int GetSessionCount() { return m_sessionCount; }

	/// <summary>
	/// 패킷 송신
	/// </summary>
	/// <param name="sessionHandle">송신할 세션 핸들</param>
	/// <param name="packet">송신할 패킷</param>
	/// <returns>송신 실패 시 false 반환</returns>
	bool SendPacket(unsigned long long sessionHandle, Packet* packet);

	/// <summary>
	/// 패킷 브로드캐스트
	/// </summary>
	/// <param name="sessionsHandles">브로드캐스트 할 세션 핸들 배열 포인터</param>
	/// <param name="sessionCount">브로드캐스트 할 세션 핸들 갯수</param>
	/// <param name="packet">브로드 캐스트 할 패킷</param>
	void SendPacketMulticast(unsigned long long* sessionsHandles, long sessionCount, Packet* packet);

	/// <summary>
	/// 세션 연결 끊기 요청
	/// </summary>
	/// <param name="sessionID"></param>
	/// <returns></returns>
	bool Disconnect(unsigned long long sessionHandle);

	/// <summary>
	/// 연결 수락을 결정하는 함수
	/// </summary>
	/// <param name="IP">입장한 세션의 아이피. 리틀엔디안으로 전달.</param>
	/// <param name="port">입장한 세션의 포트번호, 리틀엔디안으로 전달.</param>
	/// <returns> true면 입장을 수락, false면 입장을 거부</returns>
	virtual bool OnConnectionRequest(unsigned long IP, unsigned short port) { return false; }

	/// <summary>
	/// 세션 입장 처리가 완료된 후 최초로 호출되는 이벤트
	/// </summary>
	/// <param name="sessionHandle">입장한 세션의 핸들</param>
	/// <param name="IP">입장한 세션의 아이피. 리틀 엔디안으로 전달.</param>
	/// <param name="port">입장한 세션의 포트. 리틀 엔디안으로 전달.</param>
	virtual void OnClientJoin(unsigned long long sessionHandle, unsigned long IP, unsigned short port) {}

	/// <summary>
	/// 세션 끊김 처리가 완료된 후 호출되는 이벤트
	/// </summary>
	/// <param name="sessionHandle">연결 끊긴 세션의 핸들</param>
	/// <param name="code">연결 끊김 사유 코드</param>
	/// <param name="IP"></param>
	/// <param name="port"></param>
	virtual void OnClientLeave(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port) {}
	
	/// <summary>
	/// 패킷 수신 이벤트
	/// </summary>
	/// <param name="sessionHandle">패킷을 수신한 세션의 핸들</param>
	/// <param name="packet">수신한 패킷</param>
	virtual void OnRecv(unsigned long long sessionHandle, Packet* packet) {}

	/// <summary>
	/// 세션 찾기
	/// </summary>
	/// <param name="sessionHandle">검색할 세션 핸들</param>
	/// <returns>검색 성공한 세션. nullptr 반환 시 세션이 존재하지 않음.</returns>
	__inline Session* FindSession(unsigned long long sessionHandle);

	/// <summary>
	/// 세션 반환
	/// </summary>
	/// <param name="session">반환할 세션</param>
	/// <returns>해당 세션이 해제될 예정이라면 true 반환.</returns>
	__inline bool ReturnSession(Session* session) { return Decrement_IOCount(session); }

	int GetTotal_Accept() { return m_total_accept; }
	int GetTPS_Accept() { return m_tps_accept; }
	int GetTPS_Recv() { return m_tps_recv_last; }
	int GetTPS_Send() { return m_tps_send_last; }

	int GetSessionTPS_Send(unsigned long long sessionHandle);
	int GetSessionTPS_Recv(unsigned long long sessionHandle);


	// 아래는 통합 컨텐츠 구동 환경

	// 컨텐츠 등록
	void RegistContent(NetLib_Content* content);
	// 컨텐츠 해제
	void UnRegistContent(NetLib_Content* content);
	// 컨텐츠로 세션 이동
	bool Move_Content(NetLib_Content* content, unsigned long long sessionHandle, void* completionKey = nullptr);
private:
	friend class NetLib_Content;
	__inline void CreateSessionHandle(unsigned short index, unsigned long long sessionID, unsigned long long* outSessionHandle);
	__inline void DecodeSessionHandle(unsigned long long sessionHandle, unsigned short* outIndex, unsigned long long* outSessionID);

	/// <summary>
	///  Release됐다면 true반환
	/// </summary>
	/// <param name="session"></param>
	/// <returns></returns>
	__inline bool Increment_IOCount(Session* session);

	/// <summary>
	/// Release됐다면 true반환
	/// </summary>
	/// <param name="session"></param>
	/// <returns></returns>
	__inline bool Decrement_IOCount(Session* session);
	void ReleaseSession(Session* session);


	static unsigned int __stdcall WorkerThread(void* argv);
	static unsigned int __stdcall AcceptThread(void* argv);
	static unsigned int __stdcall MonitorThread(void* argv);
	static unsigned int __stdcall SchedulerThread(void* argv);
	float WaitForTime(int tick, DWORD* prevTime);
	Session* m_sessions;
	HANDLE m_iocpHandle;
	HANDLE m_acceptThread;
	HANDLE m_monitorThread;
	HANDLE m_frequencyThread;
	HANDLE m_threadPool[16];
	SOCKET m_listen_socket;
	SOCKADDR_IN m_listen_addr;
	LockFreeStack<unsigned short> m_sessionIndexPool;
	LockFreeQueue <TPS_SET> m_tpsSets;
	unsigned long long m_idPool;
	unsigned short m_sessionCount;
	unsigned long m_disconnectCount;
	int m_errCode;
	bool m_isListening;
	bool m_isRunning;

	long m_total_accept;

	long m_cumulate_accept;
	long m_tps_accept;
	long m_tps_recv_cumulate;
	long m_tps_send_cumulate;
	long m_tps_recv_last;
	long m_tps_send_last;
	int m_opt_workerTH_count;
	int m_opt_concurrentTH_size;
	unsigned short m_opt_maxOfSession;
	bool m_opt_encryption;
	bool m_opt_zerocpy;
	int m_opt_maxOfSendPackets;
	char m_header_code;
	char m_fixed_key;
	long max_send;
	long max_recvPostCnt;

	//std::unordered_map<unsigned long long, NetLib_Content*> m_registered_contents; // <id, ptr>
	//SRWLOCK m_lock_registered_contents;


};