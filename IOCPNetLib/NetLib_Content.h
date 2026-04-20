#pragma once
#include "pch.h"
#include "NetLib_Server.h"

class NetLib_Content
{
private:
	friend class NetLib_Server;

	std::unordered_map<unsigned long long, Session*> sessions;
	LockFreeQueue<ContentMSG> m_msgQueue;
	unsigned long long m_contentID;
	DWORD m_lastUpdate;
	int m_msFrequency;
	inline static unsigned long long contentIDPool;
	static const long invalidContentID = 0;
	long m_sessionCount;
	float m_lastDeltaTime;
	CUSTOM_OVERLAPPED* overlapped_content_update;
	CUSTOM_OVERLAPPED* overlapped_content_begin;

	void Update(float deltaTime);
	void Enter(unsigned long long sessionHandle);
	void Leave(Session* session);
	void Release(Session* session);
	static const DWORD REQUEST_CONTENT_BEGIN = 0x20260116;
	static const DWORD  REQUEST_CONTENT_UPDATE = 0x51885188;

public:
	NetLib_Server* m_server;

	NetLib_Content(int msFrequency)
	{
		m_sessionCount = 0;
		m_msFrequency = msFrequency;
		m_contentID = InterlockedIncrement64((long long*) & contentIDPool);
		if(m_contentID == invalidContentID)
			m_contentID = InterlockedIncrement64((long long*) & contentIDPool);
		overlapped_content_update = new CUSTOM_OVERLAPPED;
		overlapped_content_begin = new CUSTOM_OVERLAPPED;
		overlapped_content_update->type = OVERLAPPED_TYPE::CONTENT;
		overlapped_content_begin->type = OVERLAPPED_TYPE::CONTENT;
		m_lastDeltaTime = 0;
		m_lastUpdate = 0;
		m_server = 0;
		
	}

	virtual void OnBegin()
	{

	}

	virtual void OnEnd()
	{

	}

	virtual void OnUpdate(float deltaTime)
	{

	}

	//virtual void OnRecv(unsigned long long sessionHandle, Packet* packet)
	//{

	//}

	virtual void OnRecv(unsigned long long sessionHandle, char* payload)
	{

	}

	virtual void OnEnter(unsigned long long sessionHandle, void* completionKey)
	{

	}

	virtual void OnLeave(unsigned long long sessionHandle)
	{

	}

	virtual void OnRelease(unsigned long long sessionHandle, SESSION_LEAVE_CODE code, unsigned long IP, unsigned short port)
	{

	}

	long GetSessionCount() { return m_sessionCount; }
	__inline unsigned long long GetID() { return m_contentID; }
	__inline long GetTick() { return m_msFrequency; }


};