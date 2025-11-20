/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManagerPrivate.h"
#include "EString.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

void mmLogSyncUpdate(	MovementRequester* mr,
						ParseTable* pti,
						void* structPtr,
						const char* prefix,
						const char* structName)
{
	char* buffer = NULL;
	
	estrStackCreate(&buffer);
	
	ParserWriteText(&buffer,
					pti,
					structPtr,
					0,
					0,
					0);
					
	mrLog(	mr,
			NULL,
			"[net.mrSync] %s(%s %s):%s",
			mr->mrc->name,
			prefix,
			structName,
			buffer);

	estrDestroy(&buffer);
}

S32 mmGetClientStatsFrames(const MovementClientStatsFrames** framesOut){
	if(	!framesOut ||
		!mgState.fg.mc.stats.frames)
	{
		return 0;
	}
	
	*framesOut = mgState.fg.mc.stats.frames;
	
	return 1;
}

S32 mmGetClientStatsPacketsFromClient(const MovementClientStatsPacketArray** packetsOut){
	if(	!packetsOut ||
		!mgState.fg.mc.stats.packets)
	{
		return 0;
	}
	
	*packetsOut = &mgState.fg.mc.stats.packets->fromClient;
	
	return 1;
}

S32 mmGetClientStatsPacketsFromServer(const MovementClientStatsPacketArray** packetsOut){
	if(	!packetsOut ||
		!mgState.fg.mc.stats.packets)
	{
		return 0;
	}
	
	*packetsOut = &mgState.fg.mc.stats.packets->fromServer;
	
	return 1;
}

void mmGetOffsetHistoryClientToServerSync(	S32* buffer,
											S32 bufferSize,
											S32* bufferSizeOut)
{
	MIN1(bufferSize, ARRAY_SIZE(mgState.fg.netReceive.history.clientToServerSync));

	CopyStructs(buffer, mgState.fg.netReceive.history.clientToServerSync, bufferSize);

	*bufferSizeOut = bufferSize;
}

void mmGetOffsetHistoryServerSyncToServer(	S32* buffer,
											S32 bufferSize,
											S32* bufferSizeOut)
{
	MIN1(bufferSize, ARRAY_SIZE(mgState.fg.netReceive.history.serverSyncToServer));

	CopyStructs(buffer, mgState.fg.netReceive.history.serverSyncToServer, bufferSize);

	*bufferSizeOut = bufferSize;
}

