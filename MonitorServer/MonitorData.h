#pragma once

struct MonitorData
{
	unsigned char Type;
	int Value;
};
struct MonitorDataSample
{
	int Total;
	int Count;
	int Min;
	int Max;
};