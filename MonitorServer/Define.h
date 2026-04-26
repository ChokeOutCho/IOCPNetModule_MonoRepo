#pragma once

const unsigned long LOGIN_TIME_OUT = 10000;
const int ID_LENGTH = 20;
const int NICKNAME_LENGTH = 20;
const int MSG_LENGTH = 128;
const int SESSIONKEY_LENGTH = 64;

const int MONITOR_SAMPLE_MAX = 50;
const int MONITOR_TOOL_NO = 9999;
const int DB_WRITE_FREQUENCY = 60;
const char* MONITOR_KEY = "ajfw@!cv980dSZ[fje#@fdj123948djf";

const int SERVER_NO = 40;
const int MAX_ACCEPT_TPS = 5;

const int MONITOR_SERVER_NO = 40;
const int CHAT_SERVER_NO = 10;
const int LOGIN_SERVER_NO = 20;
const int GAME_SERVER_NO = 30;
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