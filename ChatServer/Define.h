#pragma once

const unsigned long LOGIN_TIME_OUT_MS = 20000;
const unsigned long HEARTBEAT_TIME_OUT_MS = 60000;
const int ID_LENGTH = 20;
const int NICKNAME_LENGTH = 20;
const int MSG_LENGTH = 128;
const int SESSIONKEY_LENGTH = 64;
const int MAX_ACCEPT_TPS = 5001;
const int MAX_CONTENT_MOV = 30;
const int MAX_CONTENT_MSG = 30;
const int SERVER_NO = 10;
struct WaitingSession
{
	unsigned long acceptTime;
	unsigned long IP;
	unsigned short port;
};

struct Stat_AcceptTPS
{
	long CurrTPS;
	long MaxTPS;
};