#pragma once

#include "TLSObjectPool.h"
#include "Sector.h"
#include "SectorMap.h"
#include "Windows.h"
#include "Define.h"
class Sector;

class Player
{
public:
	friend class ObjectPool_TLSPoolComponent<Player>;

	// CONTENT
	unsigned long long AccNo;

	float X;
	float Y;
	BYTE Front;
	CHAR HP;
	// 상태 서있음 움직이고있음
	//eState State;
	Sector* curSector;
	Sector* oldSector;

	WCHAR Nickname[NICKNAME_LENGTH];
	WCHAR ID[ID_LENGTH];
	char SessionKey[SESSIONKEY_LENGTH];

	unsigned long IP;
	unsigned short port;
	bool needSync;

	long tps_req_chat;
	long tps_req_sectorMove;
	int req_login;
	DWORD lastRecv;

	SRWLOCK lock;
	unsigned long long GetSessionHandle() const
	{
		return m_sessionHandle;
	}

	static Player* CreatePlayer(unsigned long long sessionHandle, unsigned long long accNo)
	{
		Player* newPlayer = playerPool.Alloc();
		newPlayer->m_sessionHandle = sessionHandle;
		newPlayer->AccNo = accNo;
		newPlayer->needSync = false;
		newPlayer->HP = 100;
		newPlayer->X = 1 + (rand() % 6398);
		newPlayer->Y = 1 + (rand() % 6398);

		newPlayer->tps_req_sectorMove = 0;
		newPlayer->tps_req_chat = 0;
		newPlayer->req_login = 0;
		newPlayer->lastRecv = 0;

		return newPlayer;
	}

	static void DeletePlayer(Player* player)
	{
		playerPool.Free(player);
	}

	__inline static int PlayerPoolCurrentSize()
	{
		return playerPool.GetUseSize();
	}
private:
	static unsigned long playerIDPool;
	inline static TLSObjectPool<Player> playerPool{100};
	unsigned long long m_sessionHandle;
	Player()
	{
		//ID = ++playerIDPool;
		InitializeSRWLock(&lock);

		//X = 0;
		//Y = 0;


	}

};

