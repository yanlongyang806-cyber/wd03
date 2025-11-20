
#include "cmdparse.h"
#include "EntityLib.h"
#include "GameEvent.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslSendToClient.h"
#include "mission_common.h"
#include "stringcache.h"


// ----------------------------------------------------------------------------------
// Sending Mission Events
// ----------------------------------------------------------------------------------

AUTO_COMMAND_REMOTE;
void eventsend_RemoteRecordMissionState(const char *pcMissionRefString, MissionType eType, MissionState eState, const char *pcMissionCategoryName, bool bIsRoot, UGCMissionData *pUGCMissionData, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt && pcMissionRefString) {
			eventsend_RecordMissionState(entGetPartitionIdx(pEnt), pEnt, allocAddString(pcMissionRefString), eType, eState, allocAddString(pcMissionCategoryName), bIsRoot, pUGCMissionData);
		}
	}
}


AUTO_COMMAND ACMD_NAME(recordevent);
void eventsend_RecordEvent(Entity *pPlayerEnt, char *pcEventString)
{
	GameEvent *pEvent = gameevent_EventFromString(pcEventString);
	if (pEvent) {
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		pEvent->iPartitionIdx = iPartitionIdx;
		eventtracker_SendEvent(iPartitionIdx, pEvent, 1, EventLog_Add, true);
	} else {
		gslSendPrintf(pPlayerEnt, "Invalid Event");
	}
	StructDestroy(parse_GameEvent, pEvent);
}


AUTO_COMMAND_REMOTE;
void eventsend_RemoteRecordNemesisState(U32 iNemesisID, NemesisState eState, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt) {
			eventsend_RecordNemesisState(pEnt, iNemesisID, eState);
		}
	}
}


AUTO_COMMAND_REMOTE;
void eventsend_RemoteRecordBagGetsItem(InvBagIDs eBagType, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt) {
			eventsend_RecordBagGetsItem(pEnt, eBagType);
		}
	}
}

