#pragma once
#include "LockFreeQueue.h"
#include "MonitorData.h"
class Client
{
public:
	Client(unsigned long long sessionHandle, int serverNo)
	{
		SessionHandle = sessionHandle;
		ServerNo = serverNo;
	}
	unsigned long long SessionHandle;
	int ServerNo;
	// 락프리일 필요가 전혀 없는데 노드 큐를 만들어둔게 없어서 그냥 쓰기로 함
	LockFreeQueue<MonitorData> sampleQueue;
	~Client()
	{
		while (1)
		{
			MonitorData sample;
			if (sampleQueue.Dequeue(sample) == -1)
				break;
		}
	}
};