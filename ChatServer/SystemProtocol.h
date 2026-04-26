#pragma once
enum SystemPacketType
{
	SESSION_JOIN = 0,
	SESSION_LEAVE,
	CREATE_PLAYER,
	DELETE_PLAYER,
	CONTENT_PROC,

};

enum ContentPacketProtocol
{
	LOGIN = 0,
	SECTOR_MOVE,
	MESSAGE,
	HEARTBEAT,
};