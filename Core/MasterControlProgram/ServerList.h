#pragma once

#include "GlobalTypeEnum.h"
#include "uiwindow.h"

typedef struct Packet Packet;

typedef struct
{
	GlobalType eContainerType;
	U32 iContainerID;
	U32 iIP;
	bool bCrashed;
	bool bWaiting;
	char stateString[256];
} ServerListEntry;

//earray of servers received from controller
extern ServerListEntry **gppServerList;
	
void ServerList_HandleUpdate(Packet *pPak);

U32 ServerList_GetServerIP(GlobalType eServerType);
