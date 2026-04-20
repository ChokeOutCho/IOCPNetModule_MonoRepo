#pragma once

const unsigned long LOGIN_TIME_OUT_MS = 20000;
const unsigned long SUCC_TIME_OUT_MS = 3000;
const int ID_LENGTH = 20;
const int NICKNAME_LENGTH = 20;
const int MSG_LENGTH = 128;
const int SESSIONKEY_LENGTH = 64;

const int SERVER_NO = 20;
const int MAX_ACCEPT_TPS = 5001;
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