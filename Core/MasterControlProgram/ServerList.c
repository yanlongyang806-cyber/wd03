#include "serverlist.h"
#include "uilist.h"	
#include "estring.h"

#include "textParser.h"
#include "mastercontrolprogram.h"
#include "GlobalTypes.h"


typedef enum
{
	COLUMN_SERVERTYPE,
	COLUMN_SERVERID,
	COLUMN_SERVERSTATUS,
	COLUMN_SERVERSTATE,
} enumServerListColumn;

ServerListEntry **gppServerList = NULL;
UIList *gpServerListWidget = NULL;

static void ServerList_ListMakeText(UIList *list, UIListColumn *column, int row, intptr_t iColumn, char **output)
{
	switch(iColumn)
	{
	case COLUMN_SERVERTYPE:
		estrConcatf(output, "%s", GlobalTypeToName(gppServerList[row]->eContainerType));
		break;

	case COLUMN_SERVERID:
		estrConcatf(output, "%d", gppServerList[row]->iContainerID);
		break;

	case COLUMN_SERVERSTATUS:
		if (gppServerList[row]->bCrashed)
		{
			estrConcatf(output, "Crashed...");
		}
		else if (gppServerList[row]->bWaiting)
		{
			estrConcatf(output, "Waiting...");
		}
		else
		{
			estrConcatf(output, "Running...");
		}
		break;

	case COLUMN_SERVERSTATE:
		estrConcatf(output, "%s", gppServerList[row]->stateString);
		break;

	}
}



void ServerList_HandleUpdate(Packet *pPak)
{
	int i;
	GlobalType eContainerType;

	for (i=0; i < eaSize(&gppServerList); i++)
	{
		free(gppServerList[i]);
	}

	eaDestroy(&gppServerList);

	while ((eContainerType = GetContainerTypeFromPacket(pPak)) != GLOBALTYPE_NONE)
	{
		ServerListEntry *pNewEntry = (ServerListEntry*)calloc(sizeof(ServerListEntry), 1);
		pNewEntry->eContainerType = eContainerType;
		pNewEntry->iContainerID = GetContainerIDFromPacket(pPak);

		pNewEntry->iIP = pktGetBits(pPak, 32);
		pNewEntry->bCrashed = pktGetBits(pPak, 1);
		pNewEntry->bWaiting = pktGetBits(pPak, 1);
		pktGetString(pPak,pNewEntry->stateString,sizeof(pNewEntry->stateString));
		eaPush(&gppServerList, pNewEntry);

		if (eContainerType == GLOBALTYPE_LAUNCHER && !pNewEntry->bCrashed && !pNewEntry->bWaiting)
		{
			gbControllerHasALauncher = true;
		}
	}
}




U32 ServerList_GetServerIP(GlobalType eServerType)
{
	int i;

	for (i=0; i < eaSize(&gppServerList); i++)
	{
		if (gppServerList[i]->eContainerType == eServerType)
		{
			return gppServerList[i]->iIP;
		}
	}

	return 0;
}
