/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "contact_common.h"
#include "encounter_common.h"
#include "Entity.h"
#include "EntityGrid.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "GameAccountDataCommon.h"
#include "gametimer.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslCommandParse.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslInteractable.h"
#include "gslMapReveal.h"
#include "gslMechanics.h"
#include "gslMechanics_h_ast.h"
#include "gslMission.h"
#include "gslMissionEvents.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslPatrolRoute.h"
#include "gslPowerTransactions.h"
#include "gslSendToClient.h"
#include "gslSpawnPoint.h"
#include "gslTransactions.h"
#include "gslTriggerCondition.h"
#include "gslVolume.h"
#include "gslMapVariable.h"
#include "gslWorldVariable.h"
#include "Guild.h"
#include "MapDescription.h"
#include "gslMapState.h"
#include "interaction_common.h"
#include "mechanics_common.h"
#include "mission_common.h"
#include "Player.h"
#include "queue_common.h"
#include "RoomConn.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "StructInternals.h"
#include "svrGlobalInfo.h"
#include "team.h"
#include "utilitiesLib.h"
#include "UGCCommon.h"
#include "wlBeacon.h"
#include "wlEncounter.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "../StaticWorld/WorldGridPrivate.h"
#include "progression_common.h"
#include "gslProgression.h"
#include "gslTeam.h"
#include "gslQueue.h"
#include "UGCProjectUtils.h"
#include "EntitySavedData.h"
#include "gslSavedPet.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/mechanics_common_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/svrGlobalInfo_h_ast.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

#define MAP_REVEAL_SYSTEM_TICK		30
#define LOGOUT_TIMER_UPDATE_TICK	30
#define TIMER_UPDATE_TICK			300

bool g_EncounterNoErrorCheck = false;

int g_EnableDynamicRespawn = true;
F32 g_fDynamicRespawnScale = 5.0f;

static U32 s_MechanicsTick = 0;

MapSummaryList* s_pDoorDestStatusRequestData = NULL;
static NodeSummaryList* s_pDoorNodeData = NULL; //here to speed lookup time of nodes

//Entities that are logging out of the game	
EntityTimedLogoff **g_eaLogoffEnts = NULL;

LogoffConfig g_pLogoffConfig = {0};

static int g_sbServerIgnoreLogoutTimer=0;
AUTO_CMD_INT(g_sbServerIgnoreLogoutTimer, ServerIgnoreLogoutTimer) ACMD_ACCESSLEVEL(9);


// ----------------------------------------------------------------------------------
// Utility Functions
// ----------------------------------------------------------------------------------

// Utilizes map variables and the zone map
U32 mechanics_GetMapLevel(int iPartitionIdx)
{
	MapVariable *pMapVar = mapvariable_GetByName(iPartitionIdx, "MapLevelOverrideHack");
	if (pMapVar && pMapVar->pVariable && (pMapVar->pVariable->iIntVal > 0)){
		return pMapVar->pVariable->iIntVal;
	} else {
		U32 level = zmapInfoGetMapLevel(NULL);
		return (level?level:1);
	}
}

NodeSummary* mechanics_GetNodeSummaryFromNode(WorldInteractionNode *pNode)
{
	if (s_pDoorNodeData && pNode) {
		const char *pchNodeKey = wlInteractionNodeGetKey(pNode);
		return eaIndexedGetUsingString(&s_pDoorNodeData->eaNodes, pchNodeKey);
	}
	return NULL;
}


MapSummary* mechanics_FindMapSummaryFromMapInfo(const char *pcMapName, const char *pcMapVars)
{
	if (s_pDoorDestStatusRequestData && pcMapName) {
		S32 i;
		const char *pcFindName = allocFindString(pcMapName);
		const char *pcFindVars = allocFindString(pcMapVars);
		
		for (i = eaSize(&s_pDoorDestStatusRequestData->eaList) - 1; i >= 0; i--) {
			MapSummary *pMapSummary = s_pDoorDestStatusRequestData->eaList[i];
			if (pMapSummary->pchMapName == pcFindName &&
				pMapSummary->pchMapVars == pcFindVars) {
				return pMapSummary;
			}
		}
	}
	return NULL;
}


// ----------------------------------------------------------------------------------
// Sound
// ----------------------------------------------------------------------------------

void mechanics_playMusicAtLocation(int iPartitionIdx, const Vec3 vLoc, const char *pcSoundName, const char *pcBlameFile)
{
	Entity **eaEnts = NULL;
	int i;

	// Find all players near the point
	entGridProximityLookupEArray(iPartitionIdx, vLoc, &eaEnts, true);

	// Play for all players in range
	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		ClientCmd_sndPlayMusic(eaEnts[i], pcSoundName, pcBlameFile, -1);
	}
	eaDestroy(&eaEnts);
}


void mechanics_clearMusicAtLocation(int iPartitionIdx, const Vec3 vLoc)
{
	Entity **eaEnts = NULL;
	int i;

	// Find all players near the point
	entGridProximityLookupEArray(iPartitionIdx, vLoc, &eaEnts, true);

	// Play for all players in range
	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		ClientCmd_sndClearMusic(eaEnts[i]);
	}
	eaDestroy(&eaEnts);
}


void mechanics_replaceMusicAtLocation(int iPartitionIdx, const Vec3 vLoc, const char *pcSoundName, const char *pcBlameFile)
{
	Entity **eaEnts = NULL;
	int i;

	// Find all players near the point
	entGridProximityLookupEArray(iPartitionIdx, vLoc, &eaEnts, true);

	// Play for all players in range
	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		ClientCmd_sndReplaceMusic(eaEnts[i], pcSoundName, pcBlameFile, -1);
	}
	eaDestroy(&eaEnts);
}


void mechanics_endMusicAtLocation(int iPartitionIdx, const Vec3 vLoc)
{
	Entity **eaEnts = NULL;
	int i;

	// Find all players near the point
	entGridProximityLookupEArray(iPartitionIdx, vLoc, &eaEnts, true);

	// Play for all players in range
	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		ClientCmd_sndEndMusic(eaEnts[i]);
	}
	eaDestroy(&eaEnts);
}


void mechanics_playOneShotSoundAtLocation(int iPartitionIdx, const Vec3 vLoc, Entity **eaEntsIn, const char *pcSoundName, const char *pcBlameFile)
{
	Entity **eaEnts = NULL;
	Vec3 entPos;
	int i;
	const F32 *usePos;

	if (eaEntsIn) {
		eaEnts = eaEntsIn;
	} else { // Find all players near the point
		entGridProximityLookupEArray(iPartitionIdx, vLoc, &eaEnts, true);
	}

	// Play for all players in range
	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		if (!vLoc) {
			entGetPos(eaEnts[i], entPos);
			usePos = entPos;
		} else {
			usePos = vLoc;
		}

		ClientCmd_sndPlayRemote3dV2(eaEnts[i], pcSoundName, vecX(usePos), vecY(usePos), vecZ(usePos), pcBlameFile, -1);
	}

	if(!eaEntsIn)
		eaDestroy(&eaEnts);
}


void mechanics_playOneShotSoundFromEntity(int iPartitionIdx, Entity* e, const char *pcSoundName, const char *pcBlameFile)
{
	Entity **eaEnts = NULL;
	int i;
	Vec3 entityPos;

	entGetPos(e, entityPos);

	// Find all players near the point
	entGridProximityLookupEArray(iPartitionIdx, entityPos, &eaEnts, true);

	// Play for all players in range
	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		ClientCmd_sndPlayRemote3dFromEntity(eaEnts[i], pcSoundName, e->myRef, pcBlameFile);
	}
	eaDestroy(&eaEnts);
}

void mechanics_stopOneShotSoundAtLocation(int iPartitionIdx, const Vec3 vLoc, Entity **eaEntsIn, const char* pcSoundName)
{
	Entity **eaEnts = NULL;

	if (eaEntsIn) {
		eaEnts = eaEntsIn;
	} else { // Find all players near the point
		entGridProximityLookupEArray(iPartitionIdx, vLoc, &eaEnts, true);
	}

	FOR_EACH_IN_EARRAY(eaEnts, Entity, e)
	{
		ClientCmd_sndStopOneShot(e, pcSoundName);
	}
	FOR_EACH_END;
	
	if(!eaEntsIn)
		eaDestroy(&eaEnts);
}

void mechanics_stopOneShotSoundFromEntity(int iPartitionIdx, Entity* e, const char* pcSoundName)
{
	Entity **eaEnts = NULL;
	int i;
	Vec3 entityPos;

	entGetPos(e, entityPos);

	// Find all players near the point
	entGridProximityLookupEArray(iPartitionIdx, entityPos, &eaEnts, true);

	// Stop for all players in range
	for(i=eaSize(&eaEnts)-1; i>=0; --i) {
		ClientCmd_sndStopOneShot(eaEnts[i], pcSoundName);
	}
	eaDestroy(&eaEnts);
}


// ----------------------------------------------------------------------------------
// Logout Timers
// ----------------------------------------------------------------------------------

static ExprContext *s_pLogoffContext = NULL;
static int s_hContextVarPlayer;

static ExprContext* gslLogoff_CreateAndGetContext(Entity *pEnt)
{
	if (! s_pLogoffContext) {
		ExprFuncTable* stTable;

		s_pLogoffContext = exprContextCreate();
		
		exprContextSetAllowRuntimePartition(s_pLogoffContext);
		
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "entityutil");
		exprContextSetFuncTable(s_pLogoffContext, stTable);
		
	}
	
	exprContextSetPointerVarPooledCached(s_pLogoffContext, g_PlayerVarName, pEnt, parse_Entity, true, false, &s_hContextVarPlayer);

	return s_pLogoffContext;
}

static S32 gslLogoff_ShouldDoQuickLogout(Entity *pEnt)
{
	if (g_pLogoffConfig.pExprDoQuickLogout) {
		MultiVal mv = {0};
		ExprContext *pContext = gslLogoff_CreateAndGetContext(pEnt);

		exprEvaluate(g_pLogoffConfig.pExprDoQuickLogout, pContext, &mv);
		return MultiValToBool(&mv);
	}

	return false;
}

// Logout of the game timers

void gslLogoff_StartTimer(Entity *pEnt, F32 fTime, LogoffType eType)
{
	
	EntityTimedLogoff *pLogoff = NULL;
	
	if (!pEnt || !entIsPlayer(pEnt) || !entGetContainerID(pEnt)) {
		return;
	}

	//Add them to the Logoff "queue"
	pLogoff = eaIndexedGetUsingInt(&g_eaLogoffEnts, entGetContainerID(pEnt));
	if (!pLogoff) {
		pLogoff = StructCreate(parse_EntityTimedLogoff);
		pLogoff->cid = entGetContainerID(pEnt);
		pLogoff->fTimeToLogoff = fTime;
		pLogoff->eType = eType;
		eaPush(&g_eaLogoffEnts, pLogoff);
		eaIndexedEnable(&g_eaLogoffEnts, parse_EntityTimedLogoff);
	} 
	else if (pLogoff->fTimeToLogoff < fTime && 
			!(pLogoff->eType == kLogoffType_Normal && eType == kLogoffType_Disconnect) ) 
	{
		//If my previous time to logoff was less than the new AND the type isn't changing from normal to disconnect
		pLogoff->fTimeToLogoff = fTime;
	}

	if ( eType == kLogoffType_Disconnect ) {
		pLogoff->bDisconnected = true;
	}

	if (gslLogoff_ShouldDoQuickLogout(pEnt)) {
		pLogoff->fTimeToLogoff = 0;
	} 

	pEnt->pPlayer->fLogoffTime = MAX(0.f, ceilf(pLogoff->fTimeToLogoff - pLogoff->fTimeSpent));
	

	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}


void gslLogoff_RemoveTimer(Entity *pEnt)
{
	int iIdx;
	if (!pEnt || !entIsPlayer(pEnt) || !entGetContainerID(pEnt)) {
		return;
	}
	iIdx = eaIndexedFindUsingInt(&g_eaLogoffEnts, entGetContainerID(pEnt));
	if (iIdx >= 0) {
		EntityTimedLogoff *pLogoff = eaGet(&g_eaLogoffEnts, iIdx);
		if (!pLogoff->bRanPostLogoff) {
			pEnt->pPlayer->fLogoffTime = 0.f;
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			eaRemove(&g_eaLogoffEnts, iIdx);
			StructDestroy(parse_EntityTimedLogoff, pLogoff);
		}
	}
}


static bool gslLogoff_CancelIgnored( LogoffCancelType eType )
{
	return (eaiFind(&g_pLogoffConfig.eaiCancelTypes, eType) == -1);
}


bool gslLogoff_Cancel(Entity *pEnt, LogoffCancelType eType)
{
	int iIdx;

	if (!pEnt || !entIsPlayer(pEnt) || !entGetContainerID(pEnt)) {
		return false;
	}

	if (!(pEnt && pEnt->pPlayer) || pEnt->pPlayer->fLogoffTime <= 0.f) {
		return false;
	}

	//If this type of logoff cancellation is ignored
	if (gslLogoff_CancelIgnored(eType)) {
		return false;
	}

	iIdx = eaIndexedFindUsingInt(&g_eaLogoffEnts, entGetContainerID(pEnt));

	if (iIdx >= 0) 
	{
		EntityTimedLogoff *pLogoff = eaGet(&g_eaLogoffEnts, iIdx);
		if (pLogoff && !pLogoff->bRanPostLogoff && !pLogoff->bDisconnected) 
		{
			pEnt->pPlayer->fLogoffTime = 0.f;
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			
			ClientCmd_CancelLogOutCmd(pEnt, eType);
			eaRemove(&g_eaLogoffEnts, iIdx);
			StructDestroy(parse_EntityTimedLogoff, pLogoff);
			return true;
		}
	}

	return false;
}

static void gslCreateTicketForOnlineAccountCB(TransactionReturnVal *returnStruct, void *userData)
{
	// Get the entity ID
	ContainerID iEntID = (ContainerID)userData;

	S32 iIdx = eaIndexedFindUsingInt(&g_eaLogoffEnts, iEntID);

	if (iIdx >= 0) 
	{
		EntityTimedLogoff *pLogoff = eaGet(&g_eaLogoffEnts, iIdx);
		if (pLogoff && !pLogoff->bRanPostLogoff && !pLogoff->bDisconnected) 
		{
			U32 iAuthTicket;
			if (RemoteCommandCheck_aslAPCmdCreateTicketForOnlineAccount(returnStruct, &iAuthTicket) != TRANSACTION_OUTCOME_SUCCESS)
				return;
			pLogoff->iAuthTicket = iAuthTicket;
			pLogoff->bGettingAuthTicket = false;
		}
	}
}

void gslLogoff_Tick(F32 fTimeElapsed)
{
	S32 iIdx;
	U32 iCurrentTime = timeSecondsSince2000();

	//Run through everyone attempting to logoff
	for(iIdx = eaSize(&g_eaLogoffEnts)-1; iIdx >= 0; iIdx--) {
		EntityTimedLogoff *pLogoff = eaGet(&g_eaLogoffEnts, iIdx);
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pLogoff->cid);
		
		if (pEnt && pEnt->pPlayer) {
			ClientLink *pClientLink = entGetClientLink(pEnt);

			if (pClientLink && pClientLink->netLink &&
				(pLogoff->eType == kLogoffType_GoToCharacterSelect || pLogoff->eType == kLogoffType_MeetPartyInLobby) && 
				pLogoff->iAuthTicket == 0 &&
				(!pLogoff->bGettingAuthTicket || iCurrentTime - pLogoff->iLastGetAuthTicketTimestamp > 2))
			{				
				// Get the auth ticket from the account server
				VerifyServerTypeExistsInShard( GLOBALTYPE_ACCOUNTPROXYSERVER );
				RemoteCommand_aslAPCmdCreateTicketForOnlineAccount(objCreateManagedReturnVal(gslCreateTicketForOnlineAccountCB, (void*)((intptr_t)entGetContainerID(pEnt))), 
					GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pEnt->pPlayer->accountID, linkGetIp(pClientLink->netLink));
				
				pLogoff->iLastGetAuthTicketTimestamp = timeSecondsSince2000();
				pLogoff->bGettingAuthTicket = true;
			}

			pLogoff->fTimeSpent += fTimeElapsed;

			// Wait until we receive the authentication ticket or until 10 second passes
			if ((pLogoff->eType == kLogoffType_GoToCharacterSelect || pLogoff->eType == kLogoffType_MeetPartyInLobby) &&
				pLogoff->iAuthTicket == 0 && pLogoff->fTimeSpent < 10.0f)
			{
				continue;
			}

			//Inform the player how long they have until they logoff
			pEnt->pPlayer->fLogoffTime = MAX(0.f, ceilf(pLogoff->fTimeToLogoff - pLogoff->fTimeSpent));
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

			if (pLogoff->fTimeSpent >= pLogoff->fTimeToLogoff) {
				if (!!pLogoff->bRanPostLogoff ) {
					if (pLogoff->bDisconnected || (pLogoff->eType != kLogoffType_GoToCharacterSelect && pLogoff->eType != kLogoffType_MeetPartyInLobby && pLogoff->fTimeSpent >= pLogoff->fTimeToLogoff + 2.0f)) {
						if (entGetClientLink(pEnt)) {
							//Likely will never get called but, you never know
							char *estrLogoffText = NULL;
							
							entFormatGameMessageKey(pEnt, &estrLogoffText, "Logoff_Time_Expired",
								STRFMT_FLOAT("LogoffTime", pLogoff->fTimeToLogoff),
								STRFMT_END);
							
							gslSendForceLogout(entGetClientLink(pEnt), estrLogoffText);
							
							estrDestroy(&estrLogoffText);
						}
						
						gslLogOutEntityEx(pEnt, 0, 0, pLogoff->eType);
						
						//Logged off now, destroy the structure
						eaRemove(&g_eaLogoffEnts, iIdx);
						StructDestroy(parse_EntityTimedLogoff, pLogoff);
					}
				} else {
					//Send the client side commands
					PostLogOutPlayer(pEnt, pLogoff->iAuthTicket, pLogoff->eType);
					
					//Set the post Logoff bool to true
					pLogoff->bRanPostLogoff = true;
				}
			}			
		} else {
			//The entity is gone, remove them
			eaRemove(&g_eaLogoffEnts, iIdx);
			StructDestroy(parse_EntityTimedLogoff, pLogoff);
		}
	}
}


AUTO_STARTUP(LogoffConfig) ASTRT_DEPS(PowerModes); 
void gslLogoff_ConfigLoad(void)
{
	loadstart_printf("Loading Logoff Config... ");

	ParserLoadFiles(NULL, "defs/config/LogoffConfig.def", "LogoffConfig.bin", PARSER_OPTIONALFLAG, parse_LogoffConfig, &g_pLogoffConfig);

	if (g_pLogoffConfig.pExprDoQuickLogout)
	{
		ExprContext* pContext = gslLogoff_CreateAndGetContext(NULL);
		if (!exprGenerate(g_pLogoffConfig.pExprDoQuickLogout, pContext))
		{
			Errorf("LogoffConfig.def: Error generating expression QuickLogout.\n");
		}
	}

	loadend_printf(" done.");
}


// Mission/Teaming logout from map timers
#define LOGOUT_TIMER_LENGTH 60

void mechanics_LogoutTimerStartEx(Entity* pEnt, LogoutTimerType eLogoutType, U32 uLogoutTime)
{
	LogoutTimer *pNewTimer;

	if (pEnt->pPlayer->pLogoutTimer) {
		// Figure out whether to create a new timer or not
		LogoutTimer *pOldTimer = pEnt->pPlayer->pLogoutTimer;

		// Force logout has the highest priority
		if (pOldTimer->eType == eLogoutType) {
			return;
		}
		if (pOldTimer->eType == LogoutTimerType_MissionReturn) {
			return;
		}

		// Use the new timer and destroy the old one
		StructDestroy(parse_LogoutTimer, pOldTimer);
	}

	if (!uLogoutTime) {
		uLogoutTime = LOGOUT_TIMER_LENGTH;
	}

	pNewTimer = StructCreate(parse_LogoutTimer);
	pNewTimer->eType = eLogoutType;
	pNewTimer->timeRemaining = uLogoutTime;
	pNewTimer->expirationTime = timeSecondsSince2000() + uLogoutTime;

	pEnt->pPlayer->pLogoutTimer = pNewTimer;
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
}


void mechanics_LogoutTimerRemove(Entity *pEnt)
{
	if (pEnt->pPlayer->pLogoutTimer) {
		StructDestroy(parse_LogoutTimer, pEnt->pPlayer->pLogoutTimer);
		pEnt->pPlayer->pLogoutTimer = NULL;
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

bool bProgressionValidateMaps = 10; //< Magic value that means unset.  See the AUTO_RUN.
AUTO_CMD_INT(bProgressionValidateMaps, ProgressionValidateMaps) ACMD_COMMANDLINE;

void mechanics_LogoutTimersProcess(Entity* pEnt)
{
	U32 curTime = timeSecondsSince2000();
	MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;
	Player *pPlayer = pEnt->pPlayer;
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	// If player doesn't want to be bothered (debugging dev), just destroy the timer
	if ((pPlayer && pPlayer->pLogoutTimer && pPlayer->bIgnoreBootTimer) || g_sbServerIgnoreLogoutTimer) {
		mechanics_LogoutTimerRemove(pEnt);
		return;
	}

	// Is the player on a team-owned instance but not on the team?  If so, start a new
	// logout timer (this is safe if the player already has a logout timer)
	// Check this if the player is on a mission map that has a detail string
	if (pPlayer && 
		(pMapDescription->eMapType == ZMTYPE_MISSION ||
		 pMapDescription->eMapType == ZMTYPE_OWNED ||
		 pMapDescription->eMapType == ZMTYPE_QUEUED_PVE ||
		 pMapDescription->eMapType == ZMTYPE_PVP)
		)
	{
		bool bAllowedOnMap = true;
		LogoutTimerType eLogoutType = LogoutTimerType_None;
		U32 uLogoutTime = LOGOUT_TIMER_LENGTH;
		bool bDoMapOwnershipChecks=true;

		if (pMapDescription->eMapType == ZMTYPE_QUEUED_PVE || pMapDescription->eMapType == ZMTYPE_PVP)
		{
			// If we're on a PVP or QUEUED_PVE map we need to check if we don't belong here, but only if we were queue created
			// This will most likely happen when someone disconnects while on the map, then gets kicked.
			// They will return to the map since it's their last map, but should not be there anymore.
			// Also can happen if a map is sent too many people to fit in groups. Or other such things.

			// This is much more active a check than we used to use. We will be very sad if we are in a case
			//   where Entities get to the map before the group information gets to it. (cross fingers)

			QueuePartitionInfo *pInfo = gslQueuePartitionInfoFromIdx(iPartitionIdx);
			
			// pInfo's bAllowNonGroupedEntities indicates if we are controlled by a queue or not. (Assume no pInfo means non-queue)
			//  Note that we want to run the regular ownership code if we're a non-queue-started PvE map (but not PVP)
			
			if (pInfo!=NULL && !pInfo->bAllowNonGroupedEntities)
			{
				// This is a queue controlled partition. Boot any non-grouped members
				if (!gslQueue_EntIDIsInMatchGroup(pInfo, pEnt->myContainerID))
				{
					bAllowedOnMap = false;
					eLogoutType = LogoutTimerType_NotOnInstanceTeam;
					uLogoutTime = 5;	// We want them out of here as quickly as possible.
										// But we do not want to run afoul of bugs caused by communication latency.
										// If this is too short, we run the risk of people being booted when they
										//  arrive on the map and they don't get matchGrouped quickly enough.
	
					// Do what we used to do in gslQueue's version of the kick code. (Log things on the queue server, clean up pendings etc).
					// Only do it once per logout application. (This isn't quite the right test if we have competing logout types).
					if (!pPlayer->pLogoutTimer)
					{
						glsQueue_DoLogoutKickForNonMembership(pInfo, pEnt->myContainerID);
					}
				}

				// We did Queue-created map checking. Don't do ownership
				bDoMapOwnershipChecks=false;
			}
			else
			{
				// We're not queue created.
				// For PvP don't do any checking at all (Devs may be manually moving people around)
				// For PvE we want to behave like a Mission map (we got here through a dungeon door or something and no the queue

				if (pMapDescription->eMapType == ZMTYPE_PVP)
				{
					bDoMapOwnershipChecks=false;
				}
			}
		}

		// Do ownership-style checking 
		if (bDoMapOwnershipChecks)
		{
			if (partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_GUILD) {
				// On a guild-owned map, boot the player if the map requires guild members and they aren't in the guild
				bool bGuildNotRequired = zmapInfoGetGuildNotRequired(pMapDescription->pZoneMapInfo);
				if (!bGuildNotRequired && (!guild_IsMember(pEnt) || guild_GetGuildID(pEnt) != partition_OwnerIDFromIdx(iPartitionIdx))) {
					bAllowedOnMap = false;
					eLogoutType = LogoutTimerType_NotOnInstanceGuild;
				}
			} else if (partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_TEAM) {
				// On a team-owned map, boot the player if they're not on the right team or not on any team
				if (!team_IsMember(pEnt) || pEnt->pTeam->iTeamID != partition_OwnerIDFromIdx(iPartitionIdx)) {
					bAllowedOnMap = false;
					eLogoutType = LogoutTimerType_NotOnInstanceTeam;
				}
			} else if (partition_OwnerTypeFromIdx(iPartitionIdx) == GLOBALTYPE_ENTITYPLAYER) {
				// For an Owned map, I should still be allowed on the map if I'm on the same team as the owner.
				// Also allow non-team members if the map is marked TeamNotRequired.
				Entity *pMapOwner = partition_GetPlayerMapOwner(entGetPartitionIdx(pEnt));
				bool bOnSameTeam = pMapOwner ? team_OnSameTeam(pMapOwner, pEnt) : team_OnSameTeamID(pEnt, partition_OwnerIDFromIdx(iPartitionIdx));
				if ( ( zmapInfoGetTeamNotRequired(pMapDescription->pZoneMapInfo) == false ) &&
						( pEnt->myContainerID != partition_OwnerIDFromIdx(iPartitionIdx) ) && ( !bOnSameTeam ) ) {
					bAllowedOnMap = false;
					eLogoutType = LogoutTimerType_NotOnInstanceTeam;
				}
			}
		}

		if (pPlayer->bIsGM) {
			// Don't kick GMs off maps.
			bAllowedOnMap = true;
		}

		if (!bAllowedOnMap) {
			// Player is not allowed on this map; start a logout timer for them
			mechanics_LogoutTimerStartEx(pEnt, eLogoutType, uLogoutTime);
		} else {
			// Player is on the right team.  If they have a team or guild logout timer running, end it
			if (pPlayer->pLogoutTimer) {
				switch (pPlayer->pLogoutTimer->eType) {
					xcase LogoutTimerType_NotOnInstanceTeam:
					acase LogoutTimerType_NotOnInstanceGuild:
					{
						mechanics_LogoutTimerRemove(pEnt);
					}
				}
			}
		}
	}

	if (bProgressionValidateMaps && pPlayer && pMapDescription->eMapType == ZMTYPE_MISSION && !pMapDescription->bUGC && !pMapDescription->bUGCEdit)
	{
		bool bAllowedOnMap = gslProgression_PlayerIsAllowedOnMap(pEnt, pMapDescription->mapDescription);

		if (!bAllowedOnMap) 
		{
			// Player is not allowed on this map; start a logout timer for them
			mechanics_LogoutTimerStart(pEnt, LogoutTimerType_MapDoesNotMatchProgression);
		} 
		else 
		{
			// Player is allowed to be on this map.  If they have a logout timer running, end it.
			if (pPlayer->pLogoutTimer && pPlayer->pLogoutTimer->eType == LogoutTimerType_MapDoesNotMatchProgression) 
			{
				mechanics_LogoutTimerRemove(pEnt);
			}
		}
	}

	if (pPlayer && pPlayer->pLogoutTimer) {
		LogoutTimer *pTimer = pPlayer->pLogoutTimer;

		if (pTimer->expirationTime <= curTime) {
			// Boot the player from the map
			switch(pTimer->eType)
			{
			case LogoutTimerType_NotOnInstanceTeam:
			case LogoutTimerType_MissionReturn:
				LeaveMap(pEnt);
				break;
				
			case LogoutTimerType_MapDoesNotMatchProgression:
				if( bProgressionValidateMaps && entGetAccessLevel(pEnt) < ACCESS_GM ) {
					LeaveMap(pEnt);
				}
				break;
			default:
				Errorf("Unknown logout type %d!\n", pTimer->eType);
			}

			mechanics_LogoutTimerRemove(pEnt);
		} else {
			pTimer->timeRemaining = pTimer->expirationTime - curTime;
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}


// ----------------------------------------------------------------------------------
// Entity Cleanup
// ----------------------------------------------------------------------------------

// Cleanup any map-specific information on the player
void mechanics_LeaveMapEntityCleanup(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer) {
		PERFINFO_AUTO_START_FUNC();

		// Clear interaction FX and anything else interactables track
		interactable_ClearPlayerInteractableTrackingData(pPlayerEnt);

		// Clear all timers on the player
		gametimer_ClearAllTimersForPlayer(pPlayerEnt);

		// Clear hood data and anything else volumes track
		volume_ClearPlayerVolumeTrackingData(pPlayerEnt);

		PERFINFO_AUTO_STOP();
	}
}

static void mechanics_DismissAllPlayerPets(SA_PARAM_NN_VALID Entity *pOwner)
{
	if (pOwner && pOwner->pSaved)
	{
		// Dismiss all critter pets for this player
		FOR_EACH_IN_EARRAY_FORWARDS(pOwner->pSaved->ppCritterPets, CritterPetRelationship, pRelationShip)
		{
			if (pRelationShip && pRelationShip->pEntity)
			{
				U32 uiPetID = pRelationShip->uiPetID;

				// We only want to destroy the entity. Let the relationship stay so we can restore the relationship
				gslQueueEntityDestroy(pRelationShip->pEntity);

				pRelationShip->pEntity = NULL;
				pRelationShip->erPet = 0;

				entity_SetDirtyBit(pOwner, parse_SavedEntityData, pOwner->pSaved, false);
			}
		}
		FOR_EACH_END
	}
}

// Called during InitEncounters to reset some player state
void mechanics_CleanupAllPlayers(void)
{
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity *pEnt = NULL;

	// Clean up map-specific information on the player
	while (pEnt = EntityIteratorGetNext(pIter)) {
		if (pEnt->pPlayer) {
			// Turn off all trigger conditions on the client
			triggercondition_ResetClientTriggerConditions(pEnt);

			// Turn off all visible timers on the client
			gametimer_ClearAllTimersForPlayer(pEnt);

			// Clear interactable tracking
			interactable_ClearPlayerInteractableTrackingData(pEnt);

			// Clear contact tracking
			contact_ClearPlayerContactTrackingData(pEnt);

			// Dismiss all pets
			mechanics_DismissAllPlayerPets(pEnt);
		}
	}
	EntityIteratorRelease(pIter);
}


// ----------------------------------------------------------------------------------
// Beacon Logic
// ----------------------------------------------------------------------------------

void OVERRIDE_LATELINK_beaconGatherDoors(DoorConn ***peaDoors)
{
	interactable_GetDoorConnections(peaDoors);
	volume_GetWarpConnections(peaDoors);
}


void OVERRIDE_LATELINK_beaconGatherSpawnPositions(int bSpawnOnly)
{
	if (!bSpawnOnly) {
		g_EncounterNoErrorCheck = true;
	}

	// Add spawn point information
	spawnpoint_GatherBeaconPositions();

	// Add patrol route information
	patrolroute_GatherBeaconPositions();

	// Add interactables (eg, Ambient Job Positions)
	interactable_GatherBeaconPositions();

	if (!bSpawnOnly) {
		// Add the spawn information from all the layers
		encounter_GatherBeaconPositions();
		if (gConf.bAllowOldEncounterData) {
			oldencounter_GatherBeaconPositions();
		}
	}
}


// ----------------------------------------------------------------------------------
// UGC Logic that spans systems
// ----------------------------------------------------------------------------------

// Merge in any relevant info that would be in a UGC file
static void gslMergeUGCInfo(ZoneMapEncounterObjectInfo* pInfo, ZoneMapEncounterUGCInfo* ugcInfo)
{
	int groupIt;
	int it;
	for( groupIt = -1; groupIt != eaSize( &ugcInfo->groups ); ++groupIt ) {
		ZoneMapEncounterObjectUGCGroup* group;
		if( groupIt < 0 ) {
			group = &ugcInfo->default_group;
		} else {
			group = ugcInfo->groups[ groupIt ];
		}
		
		for( it = 0; it != eaSize( &group->objects ); ++it ) {
			ZoneMapEncounterObjectUGCInfo* ugcObject = group->objects[ it ];
			if( stricmp( ugcObject->logicalName, pInfo->logicalName ) == 0 ) {
				StructCopyAll( parse_WorldUGCRestrictionProperties, &group->restrictions, &pInfo->restrictions );
				COPY_HANDLE( pInfo->displayName, ugcObject->displayName );
				COPY_HANDLE( pInfo->displayDetails, ugcObject->displayDetails );
				pInfo->interactType = ugcObject->interactType;
				pInfo->ugcContactName = ugcObject->ugcContactName;
				if( !nullStr( ugcObject->ugcContactName )) {
					ContactDef* contact = RefSystem_ReferentFromString( g_ContactDictionary, ugcObject->ugcContactName );
					if( contact && contact->costumePrefs.eCostumeType == ContactCostumeType_Specified ) {
						COPY_HANDLE( pInfo->ugcContactCostume, contact->costumePrefs.costumeOverride );
					}
				}
			
				return;
			}
		}
	}
	
}

// Export encounter info, move this as appropriate
void gslMapWriteEncounterInfo(ZoneMap *zmap, const char *info_filename)
{
	WorldZoneMapScope *pScope = zmapGetScope(zmap); 
	int i;

	if (pScope) {
		const char* zmapFilename = zmapGetFilename( zmap );
		ZoneMapEncounterInfo *pEncounterInfo = StructCreate(parse_ZoneMapEncounterInfo);
		ResourceActionList actions = { 0 };
		ZoneMapEncounterUGCInfo ugcInfo = { 0 };
		char ugcFilename[ MAX_PATH ];

		if( zmapFilename ) {
			changeFileExt( zmapFilename, ".ugcinfo", ugcFilename );
			ParserReadTextFile( ugcFilename, parse_ZoneMapEncounterUGCInfo, &ugcInfo, 0 );
		}

		pEncounterInfo->version = ZENI_CURRENT_VERSION;
		pEncounterInfo->map_name = zmap->map_info.map_name;
		pEncounterInfo->filename = allocAddString(info_filename);

		if( ugcInfo.deprecated_map_new_map_name ) {
			pEncounterInfo->deprecated_map_new_map_name = StructAllocString( ugcInfo.deprecated_map_new_map_name );
		} else {
			{
				int it;
				for( it = 0; it != eaSize( &ugcInfo.volume_logical_name ); ++it ) {
					eaPush( &pEncounterInfo->volume_logical_name, StructAllocString( ugcInfo.volume_logical_name[ it ]));
				}
			}
		
			FileListInsert( &pEncounterInfo->deps, ugcFilename, 0 );
			{
				char buffer[ MAX_PATH ];
				worldGetTempBaseDir( zmapGetFilename( zmap ), SAFESTR( buffer ));
				strcat( buffer, "/server_world_cells_deps.bin" );
				FileListInsert( &pEncounterInfo->deps, buffer, 0 );
			}

			for (i = 0; i < eaSize(&pScope->encounters); i++) {
				const char *objectName = worldScopeGetObjectName(&pScope->scope, &pScope->encounters[i]->common_data);
				if (objectName && strncmp(GROUP_UNNAMED_PREFIX, objectName, strlen(GROUP_UNNAMED_PREFIX)) != 0) {
					WorldEncounterProperties *pProps = pScope->encounters[i]->properties;
					EncounterTemplate *pTemplate = GET_REF(pProps->hTemplate);
					ZoneMapEncounterObjectInfo *pInfo = StructCreate(parse_ZoneMapEncounterObjectInfo);
					int k,j;
					pInfo->type = WL_ENC_ENCOUNTER;
					pInfo->logicalName = strdup(objectName);
					if (pScope->encounters[i]->common_data.layer && pScope->encounters[i]->common_data.layer->region_name) {
						pInfo->regionName = pScope->encounters[i]->common_data.layer->region_name;
					}
				
					copyVec3(pScope->encounters[i]->encounter_pos, pInfo->pos);
					copyQuat(pScope->encounters[i]->encounter_rot, pInfo->qOrientation);

					// Copy the contact properties over
					if (pTemplate) {
						bool bFoundContact = false;
						// Scan all actors
						for(k=eaSize(&pProps->eaActors)-1; k>=0 && !bFoundContact; --k) {
							WorldActorProperties *pWorldActor = pProps->eaActors[k];
							EncounterActorProperties *pActor;

							pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pWorldActor);
							if (pActor && pActor->pInteractionProperties && pActor->pInteractionProperties) {
								// Scan all interaction properties for the actor
								for(j=eaSize(&pActor->pInteractionProperties->eaEntries)-1; j>=0; --j) {
									WorldInteractionPropertyEntry *pEntry = pActor->pInteractionProperties->eaEntries[j];
									WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pEntry);
									if (pContactProps) {
										bFoundContact = true;
										pInfo->interactType = WL_ENC_CONTACT;
										break;
									}
								}
							}
						}
					}

					gslMergeUGCInfo(pInfo, &ugcInfo);
					eaPush(&pEncounterInfo->objects, pInfo);
				}
			}

			for (i = 0; i < eaSize(&pScope->spawn_points); i++) {
				bool bIsStartSpawn = false;
				const char *objectName = worldScopeGetObjectName(&pScope->scope, &pScope->spawn_points[i]->common_data);
				const char* startSpawnName = zmapInfoGetStartSpawnName(&zmap->map_info);

				if (startSpawnName && stricmp(startSpawnName, objectName) == 0) {
					bIsStartSpawn = true;
				} else if (pScope->spawn_points[i]->properties && pScope->spawn_points[i]->properties->spawn_type == SPAWNPOINT_STARTSPAWN) {
					bIsStartSpawn = true;
				}

				if (bIsStartSpawn || (objectName && strncmp(GROUP_UNNAMED_PREFIX, objectName, strlen(GROUP_UNNAMED_PREFIX)) != 0)) {
					ZoneMapEncounterObjectInfo *pInfo = StructCreate(parse_ZoneMapEncounterObjectInfo);
				
					pInfo->type = WL_ENC_SPAWN_POINT;
					if (bIsStartSpawn) {
						pInfo->interactType = WL_ENC_STARTSPAWN;
					} else {
						pInfo->interactType = WL_ENC_DOOR;
					}

					pInfo->logicalName = strdup(objectName);
				
					if (pScope->spawn_points[i]->common_data.layer && pScope->spawn_points[i]->common_data.layer->region_name) {
						pInfo->regionName = pScope->spawn_points[i]->common_data.layer->region_name;
					}				

 					copyVec3(pScope->spawn_points[i]->spawn_pos, pInfo->pos);
 					copyQuat(pScope->spawn_points[i]->spawn_rot, pInfo->qOrientation);
				
					gslMergeUGCInfo(pInfo, &ugcInfo);
					eaPush(&pEncounterInfo->objects, pInfo);
				}
			}

			for (i = 0; i < eaSize(&pScope->named_points); i++) {
				const char *objectName = worldScopeGetObjectName(&pScope->scope, &pScope->named_points[i]->common_data);
				if (objectName && strncmp(GROUP_UNNAMED_PREFIX, objectName, strlen(GROUP_UNNAMED_PREFIX)) != 0) {
					ZoneMapEncounterObjectInfo *pInfo = StructCreate(parse_ZoneMapEncounterObjectInfo);
					pInfo->type = WL_ENC_NAMED_POINT;
					pInfo->logicalName = strdup(objectName);
					if (pScope->named_points[i]->common_data.layer && pScope->named_points[i]->common_data.layer->region_name) {
						pInfo->regionName = pScope->named_points[i]->common_data.layer->region_name;
					}

					copyVec3(pScope->named_points[i]->point_pos, pInfo->pos);
					copyQuat(pScope->named_points[i]->point_rot, pInfo->qOrientation);

					gslMergeUGCInfo(pInfo, &ugcInfo);
					eaPush(&pEncounterInfo->objects, pInfo);
				}
			}

			for (i = 0; i < eaSize(&pScope->interactables); i++) {
				const char *objectName = worldScopeGetObjectName(&pScope->scope, &pScope->interactables[i]->common_data);
				if (objectName && strncmp(GROUP_UNNAMED_PREFIX, objectName, strlen(GROUP_UNNAMED_PREFIX)) != 0 && pScope->interactables[i]->entry) {
					WorldInteractionEntry *pEntry = pScope->interactables[i]->entry;
					if (pEntry->full_interaction_properties)
					{
						int j;
						ZoneMapEncounterObjectInfo *pInfo = StructCreate(parse_ZoneMapEncounterObjectInfo);
					
						pInfo->type = WL_ENC_INTERACTABLE;

						for(j=eaSize(&pEntry->full_interaction_properties->eaEntries)-1; j>=0; --j) {
							WorldInteractionPropertyEntry *pPropertyEntry = pEntry->full_interaction_properties->eaEntries[j];
							WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pPropertyEntry);
							if (pContactProps) {
								pInfo->interactType = WL_ENC_CONTACT;
								break;
							}
							if (interaction_GetEffectiveClass(pPropertyEntry) == pcPooled_Door) {
								pInfo->interactType = WL_ENC_DOOR;						
								break;
							}
							if (interaction_GetEffectiveClass(pPropertyEntry) == pcPooled_Clickable) {
								pInfo->interactType = WL_ENC_CLICKIE;
								break;
							}
						}

						pInfo->logicalName = strdup(objectName);
						if (pScope->interactables[i]->common_data.layer && pScope->interactables[i]->common_data.layer->region_name) {
							pInfo->regionName = pScope->interactables[i]->common_data.layer->region_name;
						}

						copyVec3(pEntry->base_entry.bounds.world_mid, pInfo->pos);
						// No rotation

						gslMergeUGCInfo(pInfo, &ugcInfo);
						eaPush(&pEncounterInfo->objects, pInfo);
					}
				}
			}
			for (i = 0; i < eaSize(&pScope->named_volumes); i++) {
				const char *objectName = worldScopeGetObjectName(&pScope->scope, &pScope->named_volumes[i]->common_data);
				if (objectName && pScope->named_volumes[i]->entry) {
					WorldVolumeEntry *pEntry = pScope->named_volumes[i]->entry;

					if(   strncmp(GROUP_UNNAMED_PREFIX, objectName, strlen(GROUP_UNNAMED_PREFIX)) != 0
						  || ugcPowerPropertiesIsUsedInUGC( pEntry->server_volume.power_volume_properties )) {
						ZoneMapEncounterObjectInfo *pInfo = StructCreate(parse_ZoneMapEncounterObjectInfo);
						pInfo->volume = StructCreate( parse_ZoneMapEncounterVolumeInfo );

						pInfo->type = WL_ENC_NAMED_VOLUME;
						pInfo->logicalName = strdup(objectName);
						if (pScope->named_volumes[i]->common_data.layer && pScope->named_volumes[i]->common_data.layer->region_name) {
							pInfo->regionName = pScope->named_volumes[i]->common_data.layer->region_name;
						}

						if( eaSize( &pEntry->elements ) == 1 ) {
							WorldVolumeElement* volumeElem = pEntry->elements[ 0 ];
							pInfo->volume->shape = volumeElem->volume_shape;
							copyVec3( volumeElem->world_mat[ 3 ], pInfo->pos );
							mat3ToQuat( volumeElem->world_mat, pInfo->qOrientation );
							copyVec3( volumeElem->local_min, pInfo->volume->boxMin );
							copyVec3( volumeElem->local_max, pInfo->volume->boxMax );
							pInfo->volume->sphereRadius = volumeElem->radius;
							if( pEntry->server_volume.power_volume_properties ) {
								pInfo->volume->power_properties = StructClone( parse_WorldPowerVolumeProperties, pEntry->server_volume.power_volume_properties );
							}
						} else {
							copyVec3(pEntry->base_entry.bounds.world_mid, pInfo->pos);
							// No Rotation
						}

						gslMergeUGCInfo(pInfo, &ugcInfo);
						eaPush(&pEncounterInfo->objects, pInfo);
					}
				}
			}
		}

		// save out region info
		{
			ZoneMapInfo* zmapInfo = zmapGetInfo( zmap );
			WorldRegion** regions = zmapInfo ? zmapInfoGetWorldRegions( zmapInfo ) : NULL;
			for (i = 0; i < eaSize(&regions); i++) {
				const WorldRegion* region = regions[i];
				const RoomConnGraph* regionGraph = worldRegionGetRoomConnGraph( (WorldRegion*)region );
				
				ZoneMapEncounterRegionInfo* regionInfo = StructCreate( parse_ZoneMapEncounterRegionInfo );
				regionInfo->regionName = region->name;
				regionInfo->fGroundFocusHeight = region->mapsnap_data.fGroundFocusHeight;
				regionInfo->type = region->type;
				if(!worldRegionGetBounds(region, regionInfo->min, regionInfo->max))
				{
					// implicit empty region -- ignore it
					StructDestroy( parse_ZoneMapEncounterRegionInfo, regionInfo );
					continue;
				}

				if (regionGraph) {
					FOR_EACH_IN_EARRAY(regionGraph->rooms, Room, room) {
						ZoneMapEncounterRoomInfo* roomInfo = StructCreate( parse_ZoneMapEncounterRoomInfo );
						copyVec3(room->bounds_min, roomInfo->min);
						copyVec3(room->bounds_max, roomInfo->max);
						eaPush(&regionInfo->rooms, roomInfo);
					}
					FOR_EACH_END;
				}

				eaPush( &pEncounterInfo->regions, regionInfo );
			}
		}

		// save out secondary maps
		{
			ZoneMapInfo *zmapInfo = zmapGetInfo(zmap);
			SecondaryZoneMap **secondary_maps = zmapInfo ? zmapInfo->secondary_maps : NULL;
			for(i = 0; i < eaSize(&secondary_maps); i++)
			{
				const SecondaryZoneMap *secondary_map = secondary_maps[i];
				SecondaryZoneMap *secondary_map_clone = StructClone(parse_SecondaryZoneMap, secondary_map);
				eaPush(&pEncounterInfo->secondary_maps, secondary_map_clone);
			}
		}

		// Find playable region
		{
			WorldVolumeEntry **playable_volumes = gslPlayableGet();
			FOR_EACH_IN_EARRAY(playable_volumes, WorldVolumeEntry, entry) {
				ZoneMapEncounterRoomInfo* roomInfo = StructCreate(parse_ZoneMapEncounterRoomInfo);
				addVec3(entry->base_entry.shared_bounds->local_min, entry->base_entry.bounds.world_matrix[3], roomInfo->min);
				addVec3(entry->base_entry.shared_bounds->local_max, entry->base_entry.bounds.world_matrix[3], roomInfo->max);
				eaPush(&pEncounterInfo->playable_volumes, roomInfo);	
			}
			FOR_EACH_END;
		}

		resSetDictionaryEditMode( g_ZoneMapEncounterInfoDictionary, true );
		resSetDictionaryEditMode( gMessageDict, true );

		resAddRequestLockResource( &actions, g_ZoneMapEncounterInfoDictionary, pEncounterInfo->map_name, pEncounterInfo);
		resAddRequestSaveResource( &actions, g_ZoneMapEncounterInfoDictionary, pEncounterInfo->map_name, pEncounterInfo );

		resRequestResourceActions( &actions );

		StructDeInit(parse_ResourceActionList, &actions);

		StructDestroy(parse_ZoneMapEncounterInfo, pEncounterInfo);
		StructDeInit(parse_ZoneMapEncounterUGCInfo, &ugcInfo);
	}
}


// ----------------------------------------------------------------------------------
// Entity Processing
// ----------------------------------------------------------------------------------

void mechanics_OncePerFrame(F32 fTimeStep)
{
	static int *s_eaiPlayerCount;

	Entity *pCurrEnt;
	U32 uWhichPlayer = 0;
	U32 uCurrGameTimerTickModded = s_MechanicsTick % TIMER_UPDATE_TICK;
	U32 uCurrLogoutTimerTickModded = s_MechanicsTick % LOGOUT_TIMER_UPDATE_TICK;
	U32 uCurrRevealTickModded = s_MechanicsTick % MAP_REVEAL_SYSTEM_TICK;
	EntityIterator* pIter;
	bool bNotBootingAllPlayers[MAX_LEGAL_PARTITION_IDX + 1] = {0};
	Entity *pLastEntPerPartition[MAX_LEGAL_PARTITION_IDX + 1] = {0};
	int iPartitionIdx;

	// Clear previous player counts
	partition_ResetPlayerCounts();

	// Process all players
	pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		iPartitionIdx = entGetPartitionIdx(pCurrEnt);

		// Increment player count on this partition
		// Count even ENTITYFLAG_IGNORE ents, since they are probably either loading the map or waiting for the player to press a key on the loading screen
		partition_IncPlayerCount(iPartitionIdx);

		// Don't process ENTITYFLAG_IGNORE ents here, just to save time; they're probably still loading the map
		if ( FlagsMatchNone(pCurrEnt->myEntityFlags,ENTITYFLAG_IGNORE) )
		{
			if (!gConf.bDisableMapRevealData && ((uWhichPlayer % MAP_REVEAL_SYSTEM_TICK) == uCurrRevealTickModded)) {
				PERFINFO_AUTO_START("MapRevealTick", 1);
				gslMapRevealCurrentLocation(pCurrEnt);
				PERFINFO_AUTO_STOP();
			}
			if ((uWhichPlayer % LOGOUT_TIMER_UPDATE_TICK) == uCurrLogoutTimerTickModded) {
				PERFINFO_AUTO_START("LogoutTimerTick", 1);
				mechanics_LogoutTimersProcess(pCurrEnt);	// And logout timers
				if (!pCurrEnt->pPlayer->pLogoutTimer) {
					bNotBootingAllPlayers[iPartitionIdx] = true;
				}
				PERFINFO_AUTO_STOP();
			} else {
				bNotBootingAllPlayers[iPartitionIdx] = true;
			}
			if ((uWhichPlayer % TIMER_UPDATE_TICK) == uCurrGameTimerTickModded)	{
				gametimer_RefreshGameTimersForPlayer(pCurrEnt);
			}
		
			pLastEntPerPartition[iPartitionIdx] = pCurrEnt;
			uWhichPlayer++;
		}
	}
	EntityIteratorRelease(pIter);

	for (iPartitionIdx=partition_GetCurNumPartitionsCeiling()-1; iPartitionIdx >=0; iPartitionIdx--)
	{
		// If we're booting all players, assign map ownership to the last player (or their team)
		if (!bNotBootingAllPlayers[iPartitionIdx] && pLastEntPerPartition[iPartitionIdx]) 
		{
			if (team_IsMember(pLastEntPerPartition[iPartitionIdx])) 
			{
				partition_SetOwnerTypeAndIDFromIdx(iPartitionIdx, GLOBALTYPE_TEAM, team_GetTeamID(pLastEntPerPartition[iPartitionIdx]));
			} 
			else 
			{
				partition_SetOwnerTypeAndIDFromIdx(iPartitionIdx, pLastEntPerPartition[iPartitionIdx]->myEntityType, pLastEntPerPartition[iPartitionIdx]->myContainerID);
			}

			//any time we change the map's ownership and there's only one guy on the map, 
			//recheck him for logout timers to avoid a timer flickering on and off
			mechanics_LogoutTimersProcess(pLastEntPerPartition[iPartitionIdx]);
		}
	}
	
	s_MechanicsTick++;
}

// ----------------------------------------------------------------------------------
// Bolstering
// ----------------------------------------------------------------------------------

void mechanics_UpdateCombatLevels(int iPartitionIdx, int iMinLevel, int iMaxLevel)
{
	static bool bError = false;

	// Error if the MinLevel is ever greater than the Max Level
	if (!bError && iMinLevel && iMaxLevel && iMinLevel > iMaxLevel){
		bError = true;
		Errorf("Error: When trying to clamp players' combat levels, the minimum level (%d) is greater than the maximum level (%d).", iMinLevel, iMaxLevel);
	}

	if (iMinLevel || iMaxLevel)
	{
		Entity* currEnt;
		EntityIterator* iter;
		
		PERFINFO_AUTO_START_FUNC();

		//Single Type, don't bolster critters
		iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			int iDesiredLevel;
			if(!currEnt->pPlayer || !currEnt->pChar || !currEnt->pChar->bLoaded)
				continue;

			iDesiredLevel = entity_GetSavedExpLevel(currEnt);
			if (iMinLevel && iDesiredLevel < iMinLevel)
				iDesiredLevel = iMinLevel;
			if (iMaxLevel && iDesiredLevel > iMaxLevel)
				iDesiredLevel = iMaxLevel;

			if(!currEnt->pChar->pLevelCombatControl ||
				currEnt->pChar->pLevelCombatControl->cidLinkPlayer ||
				currEnt->pChar->pLevelCombatControl->erLink ||
				currEnt->pChar->pLevelCombatControl->iLevelForce != iDesiredLevel ||
				currEnt->pChar->iLevelCombat != iDesiredLevel)
			{
				GameAccountDataExtract *pExtract;

				if(currEnt->pChar->pLevelCombatControl)
				{
					// Only do this if we are sidekicking
					character_RemovePowerTemporary(currEnt->pChar,currEnt->pChar->pLevelCombatControl->uiSidekickingPowerID);	
				}
				StructDestroySafe(parse_LevelCombatControl,&currEnt->pChar->pLevelCombatControl);
				entity_SetDirtyBit(currEnt, parse_Entity, currEnt, false);

				pExtract = entity_GetCachedGameAccountDataExtract(currEnt);
				character_LevelCombatForce(iPartitionIdx, currEnt->pChar, iDesiredLevel, pExtract);
			}
		}
		EntityIteratorRelease(iter);
		
		PERFINFO_AUTO_STOP();
	}
}

// ----------------------------------------------------------------------------------
// Player initialization after map load
// ----------------------------------------------------------------------------------

static void mechanics_RestoreAllPlayerPets(SA_PARAM_NN_VALID Entity *pOwner)
{
	if (pOwner && pOwner->pSaved)
	{
		// Dismiss all critter pets for this player
		FOR_EACH_IN_EARRAY_FORWARDS(pOwner->pSaved->ppCritterPets, CritterPetRelationship, pRelationShip)
		{
			gslCritterPetCreateEntity(pOwner, pRelationShip);
		}
		FOR_EACH_END
	}
}

// Called during InitEncounters to restore some player state
void mechanics_LoadAllPlayers(void)
{
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity *pEnt = NULL;

	// Load up map-specific information on the player
	while (pEnt = EntityIteratorGetNext(pIter)) 
	{
		if (pEnt->pPlayer) 
		{
			// Restore all pets
			mechanics_RestoreAllPlayerPets(pEnt);
		}
	}
	EntityIteratorRelease(pIter);
}

// ----------------------------------------------------------------------------------
// Map Loading Logic
// ----------------------------------------------------------------------------------

static void mechanics_DoorNodesDestructor(void)
{
	S32 i;
	for (i = eaSize(&s_pDoorNodeData->eaNodes)-1; i >= 0; i--) {
		REMOVE_HANDLE(s_pDoorNodeData->eaNodes[i]->hNode);
		eaDestroy(&s_pDoorNodeData->eaNodes[i]->eaDestinations);
		free(s_pDoorNodeData->eaNodes[i]);
	}
	eaDestroy(&s_pDoorNodeData->eaNodes);
	SAFE_FREE(s_pDoorNodeData);
}


static void mechanics_Cleanup(void)
{
	if (s_pDoorDestStatusRequestData) {
		StructDestroySafe(parse_MapSummaryList, &s_pDoorDestStatusRequestData);
	}
	if (s_pDoorNodeData) {
		mechanics_DoorNodesDestructor();
	}
}


MapDoorNodeRef* mechanics_CreateMapDoorNodeRefFromEntry(WorldInteractionEntry* pEntry)
{
	MapDoorNodeRef* pNodeRef = StructCreate(parse_MapDoorNodeRef);
	COPY_HANDLE(pNodeRef->hNode,pEntry->hInteractionNode);
	return pNodeRef;
}


static void mechanics_InitDoorDestinationStatusRequestEntries(WorldZoneMapScope* pScope)
{
	int i, j, c;

	mechanics_Cleanup();

	s_pDoorNodeData = StructCreate(parse_NodeSummaryList);
	s_pDoorDestStatusRequestData = StructCreate(parse_MapSummaryList);

	for (i = eaSize(&pScope->interactables)-1; i >= 0; i--) {
		WorldInteractionEntry* pEntry = pScope->interactables[i]->entry;
		WorldInteractionNode* pNode = GET_REF(pEntry->hInteractionNode);

		if (pNode) {
			GameInteractable* pInteractable = interactable_GetByNode(pNode);
			ANALYSIS_ASSUME(pNode != NULL);
			if (pInteractable) {
				NodeSummary* pNodeData = eaIndexedGetUsingString(&s_pDoorNodeData->eaNodes,wlInteractionNodeGetKey(pNode));

				for (j = interactable_GetNumPropertyEntries(pInteractable, true)-1; j >= 0; j--) {
					WorldInteractionPropertyEntry* pPropEntry = interactable_GetPropertyEntry(pInteractable, j);

					if (pPropEntry->pDoorProperties && pPropEntry->pDoorProperties->bCollectDestStatus) {
						WorldDoorInteractionProperties* pDoorProps = pPropEntry->pDoorProperties;
						WorldVariable** eaVars = worldVariableGetSpecificValues(pDoorProps->eaVariableDefs);
						const char* pchMapVars = worldVariableArrayToString(eaVars);
						const char* pchMapName = NULL;

						eaDestroy(&eaVars);

						if (pchMapVars && !pchMapVars[0]) {
							pchMapVars = NULL;
						}

						if (pDoorProps->eDoorType == WorldDoorType_MapMove) {
							WorldVariable* pDest = pDoorProps->doorDest.pSpecificValue;
							if (pDest) {
								pchMapName = pDest->pcZoneMap;
							}
						}

						if (!pchMapName) {
							continue;
						}

						if (!pNodeData) {
							pNodeData = StructCreate(parse_NodeSummary);
							COPY_HANDLE(pNodeData->hNode, pEntry->hInteractionNode);
							eaIndexedAdd(&s_pDoorNodeData->eaNodes, pNodeData);
						}

						for (c = eaSize(&s_pDoorDestStatusRequestData->eaList)-1; c >= 0; c--) {
							if (s_pDoorDestStatusRequestData->eaList[c]->pchMapName == pchMapName &&
								s_pDoorDestStatusRequestData->eaList[c]->pchMapVars == pchMapVars) {
									break;
							}
						}

						if (c < 0) {
							MapSummary* pData = StructCreate(parse_MapSummary);
							pData->iPropIndex = j;
							pData->pchMapName = allocAddString(pchMapName);
							pData->pchMapVars = pchMapVars;
							eaPush(&pData->eaNodes,mechanics_CreateMapDoorNodeRefFromEntry(pEntry));
							eaPush(&s_pDoorDestStatusRequestData->eaList,pData);
							eaPush(&pNodeData->eaDestinations,pData);
						} else {
							MapSummary* pData = s_pDoorDestStatusRequestData->eaList[c];
							eaPush(&pData->eaNodes, mechanics_CreateMapDoorNodeRefFromEntry(pEntry));
							eaPush(&pNodeData->eaDestinations, pData);
						}
					}
				}
			}
		}
	}

	if (eaSize(&s_pDoorDestStatusRequestData->eaList)) {
		MapList* pMapList = mechanics_CreateMapListFromMapSummaryList(s_pDoorDestStatusRequestData);
		RemoteCommand_aslMapManagerUpdateDoorDestinationStatusRequests(GLOBALTYPE_MAPMANAGER, 
																	   0, 
																	   gServerLibState.containerID, 
																	   pMapList);
		StructDestroy(parse_MapList, pMapList);
	}
}

void mechanics_MapLoad(ZoneMap *pZoneMap, bool bFullInit)
{
	WorldZoneMapScope *pScope = zmapGetScope(pZoneMap);
	ZoneMapInfo* pInfo = zmapGetInfo(pZoneMap);
	
	if (!gbMakeBinsAndExit && 
		!gServerLibState.fixupAllMapsAndExit && 
		zmapInfoGetCollectDoorDestStatus(pInfo) && 
		!beaconIsBeaconizer()) 
	{
		mechanics_InitDoorDestinationStatusRequestEntries(pScope);
	}
}


void mechanics_MapUnload(void)
{
	// Destroy static data
	mechanics_Cleanup();
}


void mechanics_MapReset(void)
{
	mechanics_MapUnload();
	mechanics_MapLoad(worldGetActiveMap(), true);
}


AUTO_RUN;
void mechanics_InitSystem(void)
{
	// Set map encounter info callback
	worldlibSetCreateEncounterInfoCallback(gslMapWriteEncounterInfo);
	if( bProgressionValidateMaps != true && bProgressionValidateMaps != false ) {
		bProgressionValidateMaps = isProductionMode();
	}
}

#include "gslMechanics_h_ast.c"
