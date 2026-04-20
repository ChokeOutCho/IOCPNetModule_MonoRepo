#pragma once
#include "pch.h"
struct MSG_CONTENT
{
	unsigned long long SessionHandle;
	void* Packet;
};

struct TPS_SET
{
	int send;
	int recv;
};

enum OVERLAPPED_TYPE
{
	SESSION = 0,
	CONTENT,
};

struct CUSTOM_OVERLAPPED
{
	WSAOVERLAPPED overlapped;
	OVERLAPPED_TYPE type;
};