#pragma once
#include <algorithm>
#include <vector>
#include "Sector.h"
#include "windows.h"
#include "Profiler.h"
//const int         SECTOR_MAX_Y = 32;
//const int         SECTOR_MAX_X = 32;
//const int         SECTOR_SIZE_X = 200;
//const int         SECTOR_SIZE_Y = 200;

const int         SECTOR_MAX_Y = 50;
const int         SECTOR_MAX_X = 50;
const int         SECTOR_SIZE_X = 128;
const int         SECTOR_SIZE_Y = 128;
class SectorMap
{
public:
    Sector sectors[SECTOR_MAX_Y][SECTOR_MAX_X];

    Sector voidSector;

    SectorMap()
    {

        ConnectArounds();
    }

    __inline Sector* GetSectorByPosition(float x, float y)
    {
        int sectorX = x / SECTOR_SIZE_X;
        int sectorY = y / SECTOR_SIZE_Y;
        return &sectors[sectorY][sectorX];
    }

    __inline Sector* GetSectorByCoord(int x, int y)
    {
        return &sectors[y][x];
    }

    void ConnectArounds();
    void GetPlayerSectorChanges(Player* player, SECTORS* outRemoveSectors, SECTORS* outAddSectors);
    bool PlayerSectorUpdate(Player* player);
    bool PlayerSectorUpdate(Player* player, int sectorCoordX, int sectorCoordY);

    // 인접 9섹터 공유 취득
    void AcquireAroundSectorsShared(Sector* center) 
    {
        PROFILING("AcquireAroundSectorsShared");
        Sector* arr[9];
        int count = 0;
        for (auto it = center->aroundSectors.begin(); it != center->aroundSectors.end(); ++it)
        {
            arr[count++] = *it;
        }

        BubbleSortByID(arr, count);

        for (int i = 0; i < count; ++i)
        {
            AcquireSRWLockShared(&arr[i]->lock);
        }

    }

    // 인접 9섹터 공유 반환
    void ReleaseAroundSectorsShared(Sector* center)
    {
        for (auto it = center->aroundSectors.begin(); it != center->aroundSectors.end(); ++it)
        {
            ReleaseSRWLockShared(&(*it)->lock);
        }
    }

    void BubbleSortByID(Sector** arr, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            for (size_t j = i + 1; j < count; ++j)
            {
                if (arr[i]->ID > arr[j]->ID)
                {
                    Sector* tmp = arr[i];
                    arr[i] = arr[j];
                    arr[j] = tmp;
                }
            }
        }
    }

private:
    int sectorIDPool;
};