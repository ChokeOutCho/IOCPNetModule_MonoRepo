#pragma once
enum CLIENT_CONTENT_TYPE
{
	/*
		ENTER

		unsigned short		type,

		unsigned long long	sessionHandle,
		int					ServerNo
	*/
	ENTER = 0,

	/*
	LEAVE

	unsigned short		type,

	unsigned long long	sessionHandle
	*/
	LEAVE,

};