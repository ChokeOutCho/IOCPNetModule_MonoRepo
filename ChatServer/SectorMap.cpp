#include "SectorMap.h"
void SectorMap::ConnectArounds()
{
	const int dir[8][2] =
	{
			{-1, -1}, {-1, 0}, {-1, 1},
			{ 0, -1},          { 0, 1},
			{ 1, -1}, { 1, 0}, { 1, 1}
	};



	for (int y = 0; y < SECTOR_MAX_Y; ++y)
	{
		for (int x = 0; x < SECTOR_MAX_X; ++x)
		{
			Sector* current = &sectors[y][x];
			current->ID = sectorIDPool++;
			current->X = x * SECTOR_SIZE_X;
			current->Y = y * SECTOR_SIZE_Y;
			current->CoordX = x;
			current->CoordY = y;
			current->aroundSectors.push_back(current);
			InitializeSRWLock(&current->lock);
			for (int i = 0; i < 8; ++i)
			{
				int aroundY = y + dir[i][0];
				int aroundX = x + dir[i][1];

				if (aroundX >= 0 && aroundX < SECTOR_MAX_X && aroundY >= 0 && aroundY < SECTOR_MAX_Y)
				{
					current->aroundSectors.push_back(&sectors[aroundY][aroundX]);

				}
			}
		}
	}

	voidSector.X = -1;
	voidSector.Y = -1;
}

void SectorMap::GetPlayerSectorChanges(Player* player, SECTORS* outRemoveSectors, SECTORS* outAddSectors)
{
	Sector* oldSector = player->oldSector;
	Sector* curSector = player->curSector;

	{
		std::list<Sector*>::iterator iter, end;
		outRemoveSectors->count = 0;
		oldSector->GetAroundSectors(iter, end);
		for (iter; iter != end; ++iter)
		{
			Sector* sec = *iter;
			if (abs(curSector->X - sec->X) > SECTOR_SIZE_X
				|| abs(curSector->Y - sec->Y) > SECTOR_SIZE_Y)
			{
				outRemoveSectors->sectors[outRemoveSectors->count++] = sec;
			}
		}

	}

	{
		std::list<Sector*>::iterator iter, end;
		outAddSectors->count = 0;
		curSector->GetAroundSectors(iter, end);
		for (iter; iter != end; ++iter)
		{
			Sector* sec = *iter;
			if (abs(oldSector->X - sec->X) > SECTOR_SIZE_X
				|| abs(oldSector->Y - sec->Y) > SECTOR_SIZE_Y)
			{
				outAddSectors->sectors[outAddSectors->count++] = sec;
			}
		}
	}
}

bool SectorMap::PlayerSectorUpdate(Player* player)
{
	Sector* sector = GetSectorByPosition(player->X, player->Y);
	if (player->curSector != sector)
	{
		player->oldSector = player->curSector;
		player->curSector = sector;
		sector->players.push_back(player);
		player->oldSector->players.remove(player);


		return true;
	}

	return false;
}

bool SectorMap::PlayerSectorUpdate(Player* player, int sectorCoordX, int sectorCoordY)
{
	Sector* sector = GetSectorByCoord(sectorCoordX, sectorCoordY);
	if (player->curSector == sector)
		return false;

	// 플레이어의 섹터에 플레이어가 존재한다면 제거
	Sector* curSector = player->curSector;

	if (sector->ID > curSector->ID)
	{
		AcquireSRWLockExclusive(&curSector->lock);
		AcquireSRWLockExclusive(&sector->lock);
	}
	else
	{
		AcquireSRWLockExclusive(&sector->lock);
		AcquireSRWLockExclusive(&curSector->lock);
	}

	auto it = std::find(curSector->players.begin(), curSector->players.end(), player);
	if (it != curSector->players.end())
	{
		curSector->players.erase(it);
	}
		sector->players.push_back(player);
		player->oldSector = curSector;
		player->curSector = sector;

	ReleaseSRWLockExclusive(&curSector->lock);
	ReleaseSRWLockExclusive(&sector->lock);

	return true;
}