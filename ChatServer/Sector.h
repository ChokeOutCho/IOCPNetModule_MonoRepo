#pragma once
#include <list>
#include "Player.h"
#include "windows.h"
class Player;

class Sector
{
public:
	int ID;
	SRWLOCK lock;

	std::list<Player*> players;
	std::list<Sector*> aroundSectors;
	int X;
	int Y;
	int CoordX;
	int CoordY;
	__inline void GetPlayers(std::list<Player*>::iterator& beginIter, std::list<Player*>::iterator& endIter)
	{
		beginIter = players.begin();
		endIter = players.end();
	}
	__inline void GetAroundSectors(std::list<Sector*>::iterator& beginIter, std::list<Sector*>::iterator& endIter)
	{
		beginIter = aroundSectors.begin();
		endIter = aroundSectors.end();
	}

};

struct SECTORS
{
	int count;
	Sector* sectors[9];
};