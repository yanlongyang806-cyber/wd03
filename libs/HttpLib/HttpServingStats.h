#pragma once
#include "HttpServing.h"

AUTO_STRUCT;
typedef struct HttpIPStats
{
	U32 iIP;
	U64 iTotalBytesSent;
	U64 iBytesLast15Secs;
} HttpIPStats;

AUTO_STRUCT;
typedef struct HttpStats
{
	U64 iTotalBytesSent;
	U64 iBytesSentLast15Secs;
	HttpIPStats **ppIPStats;
} HttpStats;


void HttpStats_NewLink(NetLink *pLink);
void HttpStats_LinkClosed(NetLink *pLink);

HttpStats *GetHttpStats(U32 iPort);

void HttpStats_Init(void);

//needs to be called every second
void HttpStats_Update(U32 iCurTime);