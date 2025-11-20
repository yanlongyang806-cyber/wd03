///***************************************************************************
//*     Copyright (c) 2006-2007, Cryptic Studios
//*     All Rights Reserved
//*     Confidential Property of Cryptic Studios
//***************************************************************************/

#include "aiStruct.h"
#include "AutoTransDefs.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "cmdparse.h"
#include "contact_common.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityLib.h"
#include "estring.h"
#include "GameEvent.h"
#include "gslEncounter.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslInteractable.h"
#include "gslMission.h"
#include "gslMissionEvents.h"
#include "gslOldEncounter.h"
#include "gslOldEncounter_events.h"
#include "gslSendToClient.h"
#include "gslVolume.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "Powers.h"
#include "Reward.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "team.h"
#include "wlInteraction.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "nemesis_common.h"
#include "utilitiesLib.h"

#include "AutoGen/GameEvent_h_ast.h"
#include "AutoGen/mission_common_h_ast.h" // for parse_GameEvent
#include "Autogen/oldencounter_common_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"


// ----------------------------------------------------------------------------------
// Static data
// ----------------------------------------------------------------------------------

static GameEvent s_event;

// Stored GameEventParticipants to prevent extra allocations
static GameEventParticipant** s_FreeGameEventParticipants = NULL;
static GameEventParticipant** s_InUseGameEventParticipants = NULL;

// Prototypes
static GameEventParticipant* eventsend_AllocParticipantCopy(GameEventParticipant *pInfo);
static void eventsend_FreeAllParticipantCopies(void);


// ----------------------------------------------------------------------------------
// Utility Functions
// ----------------------------------------------------------------------------------

// Frees resources allocated in eventsend_AddEntSource/Target, etc.
static void eventsend_ClearParticipantInfo(GameEvent *pEvent)
{
	PERFINFO_AUTO_START_FUNC();

	eaClearFast(&pEvent->eaSources);
	eaClearFast(&pEvent->eaTargets);
	eventsend_FreeAllParticipantCopies();

	PERFINFO_AUTO_STOP();
}


static void eventsend_ClearEvent(GameEvent *pEvent)
{
	void *tmpSources, *tmpTargets, *tmpItemCats;

	PERFINFO_AUTO_START_FUNC();
	
	// clear Source and Target arrays
	eventsend_ClearParticipantInfo(pEvent);
	eaiClear(&pEvent->eaItemCategories);
	
	// Memset struct to 0, but preserve earrays (since they weren't freed)
	tmpSources = pEvent->eaSources;
	tmpTargets = pEvent->eaTargets;
	tmpItemCats = pEvent->eaItemCategories;
	memset(pEvent, 0, sizeof(GameEvent));
	pEvent->eaSources = tmpSources;
	pEvent->eaTargets = tmpTargets;
	pEvent->eaItemCategories = tmpItemCats;

	// set Enum fields to defaults
	pEvent->eSourceRegionType = -1;
	pEvent->eTargetRegionType = -1;
	pEvent->missionState = -1;
	pEvent->missionType = -1;
	pEvent->missionLockoutState = -1;
	pEvent->encState = -1;
	pEvent->nemesisState = -1;
	pEvent->healthState = -1;
	pEvent->eMinigameType = -1;
	pEvent->ePvPQueueMatchResult = -1;
	pEvent->ePvPEvent = -1;

	pEvent->tPartOfUGCProject = TriState_DontCare;
	pEvent->tUGCFeaturedCurrently = TriState_DontCare;
	pEvent->tUGCFeaturedPreviously = TriState_DontCare;
	pEvent->tUGCProjectQualifiesForReward = TriState_DontCare;

	PERFINFO_AUTO_STOP();
}


GameEventParticipant* eventsend_GetOrCreateEntityInfo(Entity *pEnt)
{
	CritterFaction *pFaction;
	AllegianceDef *pAllegiance;
	OldStaticEncounter *staticEnc;
	char *pcLogString;

	if (!pEnt) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	// If the Entity doesn't have a cached version, create it
	if (!pEnt->pGameEventInfo) {
		GameEventParticipant *pEntInfo;

		PERFINFO_AUTO_START("CreatingNewInfo", 1);
		
		pEntInfo = StructCreate(parse_GameEventParticipant);

		pEntInfo->entRef = pEnt->myRef;
		pEntInfo->bIsPlayer = (pEnt->pPlayer != NULL);

		pEntInfo->eContainerType = entGetType(pEnt);
		pEntInfo->iContainerID = entGetContainerID(pEnt);

		pEntInfo->pchDebugName = StructAllocString(pEnt->debugName);
		pEntInfo->pchAccountName = StructAllocString(SAFE_MEMBER2(pEnt, pPlayer, privateAccountName));

		pEntInfo->eRegionType = entGetWorldRegionTypeOfEnt(pEnt);

		// critter info
		if (pEnt->pCritter) {
			Critter *critter = pEnt->pCritter;
			CritterDef *critterDef = GET_REF(critter->critterDef);
			CritterGroup *critterGroup = (critterDef?GET_REF(critterDef->hGroup):NULL);
				
			// Critter rank
			pEntInfo->pchRank = critter->pcRank;
			// Critter name
			if (critterDef) {
				pEntInfo->pchCritterName = critterDef->pchName;
				pEntInfo->piCritterTags = critterDef->piTags;
			}
			// Critter Group name
			if (critterGroup) {
				pEntInfo->pchCritterGroupName = critterGroup->pchName;
			}

			if(critter->pcNemesisMinionCostumeSet)
			{
				// set the nemesis type here
				pEntInfo->pchNemesisType = critter->pcNemesisMinionCostumeSet;
			}

			// Actor name
			if (critter->encounterData.pGameEncounter) {
				const char *pcName = encounter_GetActorName(critter->encounterData.pGameEncounter, critter->encounterData.iActorIndex);
				pEntInfo->pchActorName = allocAddString(pcName);
			}
			if (gConf.bAllowOldEncounterData && critter->encounterData.sourceActor) {
				char *estrBuffer = NULL;
				estrStackCreate(&estrBuffer);
				oldencounter_GetActorName(critter->encounterData.sourceActor, &estrBuffer);
				pEntInfo->pchActorName = allocAddString(estrBuffer);
				estrDestroy(&estrBuffer);
			}
		}
		pEnt->pGameEventInfo = pEntInfo;

		PERFINFO_AUTO_STOP();
	}

	// Update any data that may have changed
	pEnt->pGameEventInfo->pEnt = pEnt;
	pEnt->pGameEventInfo->pTeam = team_GetTeam(pEnt);
	pEnt->pGameEventInfo->teamID = pEnt->pGameEventInfo->pTeam ? pEnt->pGameEventInfo->pTeam->iContainerID : 0;
	pEnt->pGameEventInfo->iLevelCombat = pEnt->pChar?pEnt->pChar->iLevelCombat:0;
	pEnt->pGameEventInfo->iLevelReal = entity_GetSavedExpLevel(pEnt);
		
	pFaction = entGetFaction(pEnt);
	pEnt->pGameEventInfo->pchFactionName = (pFaction?pFaction->pchName:NULL);
	pAllegiance = GET_REF(pEnt->hAllegiance);
	pEnt->pGameEventInfo->pchAllegianceName = (pAllegiance && pAllegiance->pcName ? allocAddString(pAllegiance->pcName) : NULL);
		
	if (pEnt->pChar) {
		CharacterClass *pClass = character_GetClassCurrent(pEnt->pChar);
		if (pClass) {
			pEnt->pGameEventInfo->pchClassName = allocAddString(pClass->pchName);
		}
	}

	if (SAFE_MEMBER(pEnt->pCritter, encounterData.pGameEncounter)) {
		EncounterTemplate *pTemplate = encounter_GetTemplate(pEnt->pCritter->encounterData.pGameEncounter);
		pEnt->pGameEventInfo->eaStaticEncScopeNames = SAFE_MEMBER(pEnt->pCritter->encounterData.pGameEncounter, pWorldEncounter) ? worldEncObjGetScopeNames((WorldEncounterObject*) pEnt->pCritter->encounterData.pGameEncounter->pWorldEncounter) : NULL;
		pEnt->pGameEventInfo->pchEncounterName = pTemplate ? pTemplate->pcName : NULL;
		pEnt->pGameEventInfo->eaEncGroupScopeNames = SAFE_MEMBER2(pEnt->pCritter->encounterData.pGameEncounter, pEncounterGroup, pWorldGroup) ? worldEncObjGetScopeNames((WorldEncounterObject*) pEnt->pCritter->encounterData.pGameEncounter->pEncounterGroup->pWorldGroup) : NULL;

	} else if (gConf.bAllowOldEncounterData && pEnt->pCritter && pEnt->pCritter->encounterData.parentEncounter && (staticEnc = GET_REF(pEnt->pCritter->encounterData.parentEncounter->staticEnc))) {
		OldEncounter *encounter = pEnt->pCritter->encounterData.parentEncounter;
		pEnt->pGameEventInfo->eaStaticEncScopeNames = oldencounter_GetEncounterScopeNames(staticEnc);
		pEnt->pGameEventInfo->pchEncounterName = staticEnc->spawnRule->name;
		pEnt->pGameEventInfo->eaEncGroupScopeNames = staticEnc->groupOwner ? oldencounter_GetGroupScopeNames(staticEnc->groupOwner) : NULL;

	} else {
		// This should be hit for players, player pets, 
		// and only very rarely for critters, like when the pGameEncounter 
		// gets set to NULL on the critter when RemoveActor is called
		pEnt->pGameEventInfo->eaStaticEncScopeNames = NULL;
		pEnt->pGameEventInfo->pchEncounterName = NULL;
		pEnt->pGameEventInfo->eaEncGroupScopeNames = NULL;

		if (IS_HANDLE_ACTIVE(pEnt->hCreatorNode)) {
			// destructible object info
			WorldInteractionNode *pNode = GET_REF(pEnt->hCreatorNode);
			if (pNode){
				WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
				GameInteractable *pInteractable = interactable_GetByNode(pNode);
				if (pEntry) {
					pEnt->pGameEventInfo->pchObjectName = allocAddString(wlInteractionGetDestructibleEntityName(pEntry->full_interaction_properties));
				}
				if(SAFE_MEMBER2(pInteractable, pWorldInteractable, common_data.parent_group)) {
					pEnt->pGameEventInfo->eaEncGroupScopeNames = worldEncObjGetScopeNames((WorldEncounterObject*) pInteractable->pWorldInteractable->common_data.parent_group);
				}
			}
		}
	}
	
	PERFINFO_AUTO_START("UpdateLogString", 1);
	pcLogString = entity_GetProjSpecificLogString(pEnt);
	if (pEnt->pGameEventInfo->pchLogString && (!pcLogString || strcmp(pcLogString, pEnt->pGameEventInfo->pchLogString)!=0)){
		StructFreeString(pEnt->pGameEventInfo->pchLogString);
		pEnt->pGameEventInfo->pchLogString = NULL;
	}
	if (pcLogString && !pEnt->pGameEventInfo->pchLogString) {
		pEnt->pGameEventInfo->pchLogString = StructAllocString(pcLogString);
	}
	PERFINFO_AUTO_STOP(); // UpdateLogString

	PERFINFO_AUTO_STOP(); // Func

	// return cached GameEventParticipant
	return pEnt->pGameEventInfo;
}


static GameEventParticipant* eventsend_GetOrCreateEncounterInfo(OldEncounter *pEncounter)
{
	OldStaticEncounter *pStaticEnc;

	if (!pEncounter) {
		return NULL;
	}

	pStaticEnc = GET_REF(pEncounter->staticEnc);
	if (!pStaticEnc) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!pEncounter->pGameEventInfo) {
		pEncounter->pGameEventInfo = StructCreate(parse_GameEventParticipant);
	}

	pEncounter->pGameEventInfo->eaStaticEncScopeNames = oldencounter_GetEncounterScopeNames(pStaticEnc);
	pEncounter->pGameEventInfo->pchEncounterName = pStaticEnc->spawnRule->name;
	pEncounter->pGameEventInfo->eaEncGroupScopeNames = pStaticEnc->groupOwner ? oldencounter_GetGroupScopeNames(pStaticEnc->groupOwner) : NULL;
	pEncounter->pGameEventInfo->pEncounter = pEncounter;
	pEncounter->pGameEventInfo->bHasCredit = true;

	PERFINFO_AUTO_STOP();

	return pEncounter->pGameEventInfo;
}


static GameEventParticipant* eventsend_GetOrCreateEncounter2Info(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	if (!pEncounter || !pState) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!pState->eventData.pGameEventInfo) {
		EncounterTemplate *pTemplate = encounter_GetTemplate(pEncounter);
		pState->eventData.pGameEventInfo = StructCreate(parse_GameEventParticipant);

		// Encounter/StaticEncounter names
		pState->eventData.pGameEventInfo->pchEncounterName = pTemplate ? pTemplate->pcName : NULL;
	}

	// Update these every time because editing can invalidate the scope name structures
	pState->eventData.pGameEventInfo->eaStaticEncScopeNames = pEncounter->pWorldEncounter ? worldEncObjGetScopeNames((WorldEncounterObject*) pEncounter->pWorldEncounter) : NULL;
	pState->eventData.pGameEventInfo->eaEncGroupScopeNames = pEncounter->pEncounterGroup ? worldEncObjGetScopeNames((WorldEncounterObject*) pEncounter->pEncounterGroup->pWorldGroup) : NULL;

	pState->eventData.pGameEventInfo->pEncounter2 = pEncounter;
	pState->eventData.pGameEventInfo->bHasCredit = true;

	PERFINFO_AUTO_STOP();

	return pState->eventData.pGameEventInfo;
}


static GameEventParticipant* eventsend_FindParticipantByEntRef(GameEventParticipant ***peaParticipants, EntityRef uRef)
{
	int i;
	for (i = 0; i < eaSize(peaParticipants); i++){
		if ((*peaParticipants)[i]->entRef == uRef) {
			return (*peaParticipants)[i];
		}
	}
	return NULL;
}


static GameEventParticipant* eventsend_AllocParticipantCopy(GameEventParticipant *pInfo)
{
	GameEventParticipant *pCopy;

	PERFINFO_AUTO_START_FUNC();
	
	pCopy = eaPop(&s_FreeGameEventParticipants);
	if (!pCopy) {
		pCopy = StructAlloc(parse_GameEventParticipant);
	}
	if (pCopy) {
		eaPush(&s_InUseGameEventParticipants, pCopy);
		StructCopyAll(parse_GameEventParticipant, pInfo, pCopy);
	}

	PERFINFO_AUTO_STOP();
	return pCopy;
}


// This "frees" all GameEventParticipants allocated with eventsend_AllocParticipantCopy
static void eventsend_FreeAllParticipantCopies(void)
{
	eaPushEArray(&s_FreeGameEventParticipants, &s_InUseGameEventParticipants);
	eaClearFast(&s_InUseGameEventParticipants);
}


// Utility function for sending Events.  Populates the Event with the actor name, 
// encounter name, static encounter name, destructible object name, etc. of a source and target
#define eventsend_AddEntSource(ev, iPartitionIdx, source) eventsend_AddEntSourceEx(ev, iPartitionIdx, source, 0.f, true, 0.f, false)
#define eventsend_AddEntTarget(ev, iPartitionIdx, target) eventsend_AddEntTargetEx(ev, iPartitionIdx, target, 0.f, true, 0.f, false)
static void eventsend_AddEntSourceEx(GameEvent *pEvent, int iPartitionIdx, Entity *pEnt, F32 fCreditPercentage, bool bHasCredit, F32 fTeamCreditPercentage, bool bHasTeamCredit)
{
	GameEventParticipant *pInfo;

	if (!pEnt || (iPartitionIdx != entGetPartitionIdx(pEnt))) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pInfo = eventsend_FindParticipantByEntRef(&pEvent->eaSources, pEnt->myRef);
	if (pInfo) {
		// if this ent has already been added to the event, update to use the most generous credit possible
		MAX1(pInfo->fCreditPercentage, fCreditPercentage);
		pInfo->bHasCredit |= bHasCredit;
		MAX1(pInfo->fTeamCreditPercentage, fTeamCreditPercentage);
		pInfo->bHasTeamCredit |= bHasTeamCredit;
	} else {
		// Get the cached GameEventParticipant from the entity
		pInfo = eventsend_GetOrCreateEntityInfo(pEnt);
			
		// If the cached version is already a Target, we can't use the cached copy - we'll have to make a new one
		if (eaFind(&pEvent->eaTargets, pInfo) != -1) {
			pInfo = eventsend_AllocParticipantCopy(pInfo);
		}

		pInfo->fCreditPercentage = fCreditPercentage;
		pInfo->bHasCredit = bHasCredit;
		pInfo->fTeamCreditPercentage = fTeamCreditPercentage;
		pInfo->bHasTeamCredit = bHasTeamCredit;
		eaPush(&pEvent->eaSources, pInfo);
	}

	PERFINFO_AUTO_STOP();
}


void eventsend_AddEntSourceExtern(GameEvent *pEvent, int iPartitionIdx, Entity *pEnt, F32 fCreditPercentage, bool bHasCredit, F32 fTeamCreditPercentage, bool bHasTeamCredit)
{
	eventsend_AddEntSourceEx(pEvent, iPartitionIdx, pEnt, fCreditPercentage, bHasCredit, fTeamCreditPercentage, bHasTeamCredit);
}


static void eventsend_AddEntTargetEx(GameEvent *pEvent, int iPartitionIdx, Entity *pEnt, F32 fCreditPercentage, bool bHasCredit, F32 fTeamCreditPercentage, bool bHasTeamCredit)
{
	GameEventParticipant *pInfo;

	if (!pEnt || (iPartitionIdx != entGetPartitionIdx(pEnt))) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	pInfo = eventsend_FindParticipantByEntRef(&pEvent->eaTargets, pEnt->myRef);
	if (pInfo){
		// if this ent has already been added to the event, update to use the most generous credit possible
		MAX1(pInfo->fCreditPercentage, fCreditPercentage);
		pInfo->bHasCredit |= bHasCredit;
		MAX1(pInfo->fTeamCreditPercentage, fTeamCreditPercentage);
		pInfo->bHasTeamCredit |= bHasTeamCredit;
	} else {
		// Get the cached GameEventParticipant from the entity
		pInfo = eventsend_GetOrCreateEntityInfo(pEnt);
			
		// If the cached version is already a Source, we can't use the cached copy - we'll have to make a new one
		if (eaFind(&pEvent->eaSources, pInfo) != -1){
			pInfo = eventsend_AllocParticipantCopy(pInfo);
		}

		pInfo->fCreditPercentage = fCreditPercentage;
		pInfo->bHasCredit = bHasCredit;
		pInfo->fTeamCreditPercentage = fTeamCreditPercentage;
		pInfo->bHasTeamCredit = bHasTeamCredit;
		eaPush(&pEvent->eaTargets, pInfo);
	}

	PERFINFO_AUTO_STOP();
}


void eventsend_AddEntTargetExtern(GameEvent *pEvent, int iPartitionIdx, Entity *pEnt, F32 fCreditPercentage, bool bHasCredit, F32 fTeamCreditPercentage, bool bHasTeamCredit)
{
	eventsend_AddEntTargetEx(pEvent, iPartitionIdx, pEnt, fCreditPercentage, bHasCredit, fTeamCreditPercentage, bHasTeamCredit);
}


// Utility function for sending Events.  Populates the Event with the actor name, 
// encounter name, static encounter name, destructible object name, etc. of a source and target
static void eventsend_AddEntSources(GameEvent *pEvent, int iPartitionIdx, Entity ***peaSources)
{
	// Get Source info
	if (peaSources) {
		int i, n = eaSize(peaSources);
		for (i = 0; i < n; i++){
			eventsend_AddEntSource(pEvent, iPartitionIdx, (*peaSources)[i]);
		}
	}
}


static void eventsend_AddEntTargets(GameEvent *pEvent, int iPartitionIdx, Entity **eaTargets)
{
	// Get Target info
	int i, n = eaSize(&eaTargets);
	for (i = 0; i < n; i++){
		eventsend_AddEntTarget(pEvent, iPartitionIdx, eaTargets[i]);
	}
}


// ----------------------------------------------------------------------------------
// Sending Death Events
// ----------------------------------------------------------------------------------

void eventsend_RecordObjectDeath(Entity *pKillerEnt, const char *pcObject)
{
	WorldInteractionNode *pNode;
	WorldInteractionEntry *pEntry = NULL;
	GameEventParticipant objectInfo = {0};
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	objectInfo.eRegionType = -1;
	objectInfo.bHasCredit = true;
	iPartitionIdx = entGetPartitionIdx(pKillerEnt);

	pNode = RefSystem_ReferentFromString(INTERACTION_DICTIONARY, pcObject);
	if (pNode) {
		pEntry = wlInteractionNodeGetEntry(pNode);
	}

	if (pEntry && pEntry->full_interaction_properties) {
		objectInfo.pchObjectName = allocAddString(wlInteractionGetDestructibleEntityName(pEntry->full_interaction_properties));
	} else {
		objectInfo.pchObjectName = NULL;
	}

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_Kills;
	s_event.iPartitionIdx = iPartitionIdx;
	entGetPos(pKillerEnt, s_event.pos);

	eaPush(&s_event.eaTargets, &objectInfo);
	eventsend_AddEntSource(&s_event, iPartitionIdx, pKillerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordDeath(KillCreditTeam ***peaCreditTeams, Entity *pDeadEnt)
{
	int iPartitionIdx;
	int i, j;

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	iPartitionIdx = entGetPartitionIdx(pDeadEnt);
	s_event.type = EventType_Kills;
	s_event.iPartitionIdx = iPartitionIdx;
	entGetPos(pDeadEnt, s_event.pos);

	// Put Target info into event
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pDeadEnt);

	// Put Source info into event from the Kill Credit info
	if (peaCreditTeams) 
	{
		for (i = 0; i < eaSize(peaCreditTeams); i++) 
		{
			for (j = 0; j < eaSize(&(*peaCreditTeams)[i]->eaMembers); j++) 
			{
				KillCreditEntity *pCreditEnt = (*peaCreditTeams)[i]->eaMembers[j];
				Entity *pEnt = entFromEntityRef(iPartitionIdx, pCreditEnt->entRef);
				
				if (!pEnt)
				{
					continue;
				}

				if (!gameevent_AreAssistsEnabled() || pCreditEnt->bFinalBlow) 
				{
					eventsend_AddEntSourceEx(&s_event, iPartitionIdx, pEnt, pCreditEnt->fPercentCreditSelf, 
												pCreditEnt->bHasCredit, pCreditEnt->fPercentCreditTeam, pCreditEnt->bHasTeamCredit);
				}
			}
		}
	}

	// Use "complex" team matching - tells matching code to use the bHasTeamCredit flag for matching
	// instead of looking for Team IDs
	s_event.bUseComplexTeamMatchingSource = true;

	// Send event
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	if (gameevent_AreAssistsEnabled()) 
	{
		eventsend_ClearEvent(&s_event);

		s_event.type = EventType_Assists;
		s_event.iPartitionIdx = entGetPartitionIdx(pDeadEnt);
		entGetPos(pDeadEnt, s_event.pos);

		eventsend_AddEntTarget(&s_event, iPartitionIdx, pDeadEnt);

		if (peaCreditTeams) 
		{
			for (i = 0; i < eaSize(peaCreditTeams); i++) 
			{
				for (j = 0; j < eaSize(&(*peaCreditTeams)[i]->eaMembers); j++) 
				{
					KillCreditEntity *pCreditEnt = (*peaCreditTeams)[i]->eaMembers[j];
					Entity *pEnt = entFromEntityRef(iPartitionIdx, pCreditEnt->entRef);

					if (!pCreditEnt->bFinalBlow) 
					{
						eventsend_AddEntSourceEx(&s_event, iPartitionIdx, pEnt, pCreditEnt->fPercentCreditSelf, 
													pCreditEnt->bHasCredit, pCreditEnt->fPercentCreditTeam, pCreditEnt->bHasTeamCredit);
					}
				}
			}
		}

		// Use "complex" team matching - tells matching code to use the bHasTeamCredit flag for matching
		// instead of looking for Team IDs
		s_event.bUseComplexTeamMatchingSource = true;

		// Send event
		eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

		eventsend_ClearParticipantInfo(&s_event);
	}

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordNearDeath(Entity *pNearlyDeadEnt)
{
	PERFINFO_AUTO_START_FUNC();

	if(pNearlyDeadEnt)
	{
		S32 iPartitionIdx = entGetPartitionIdx(pNearlyDeadEnt);
		

		eventsend_ClearEvent(&s_event);
		s_event.type = EventType_NearDeath;
		s_event.iPartitionIdx = iPartitionIdx;
		entGetPos(pNearlyDeadEnt, s_event.pos);

		eventsend_AddEntTarget(&s_event, iPartitionIdx, pNearlyDeadEnt);

		eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

		eventsend_ClearParticipantInfo(&s_event);
	}

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Encounter Events
// ----------------------------------------------------------------------------------

void eventsend_EncounterStateChange(int iPartitionIdx, OldEncounter *pEncounter, EncounterState eState)
{
	GameEventParticipant *pInfo;

	if (!pEncounter) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_EncounterState;
	s_event.encState = pEncounter->state;
	s_event.iPartitionIdx = iPartitionIdx;
		
	pInfo = eventsend_GetOrCreateEncounterInfo(pEncounter);
	if (pInfo) {
		eaPush(&s_event.eaTargets, pInfo);
	}

	if (eState == EncounterState_Success || eState == EncounterState_Failure){
		int i, n = eaiSize(&pEncounter->entsWithCredit);
		for (i = 0; i < n; i++){
			Entity *pEnt = entFromEntityRef(iPartitionIdx, pEncounter->entsWithCredit[i]);
			eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);
		}
	}

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_Encounter2StateChange(GameEncounter *pEncounter, GameEncounterPartitionState *pState, EncounterState eState)
{
	GameEventParticipant *pInfo;

	PERFINFO_AUTO_START_FUNC();

	// The Encounter is the source of the Event.  The player who tagged the encounter
	// is the "target".
	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_EncounterState;
	s_event.encState = pState->eState;
	s_event.iPartitionIdx = pState->iPartitionIdx;

	pInfo = eventsend_GetOrCreateEncounter2Info(pEncounter, pState);
	if (pInfo) {
		eaPush(&s_event.eaTargets, pInfo);
	}

	if (eState == EncounterState_Success || eState == EncounterState_Failure) {
		int i, n = eaiSize(&pState->playerData.eauEntsWithCredit);
		for (i = 0; i < n; i++) {
			Entity *pEnt = entFromEntityRef(pState->iPartitionIdx, pState->playerData.eauEntsWithCredit[i]);
			eventsend_AddEntSource(&s_event, pState->iPartitionIdx, pEnt);
		}
	}

	eventtracker_SendEvent(pState->iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending AI Events
// ----------------------------------------------------------------------------------

void eventsend_EncounterActorStateChange(Entity *pCritterEnt, char *pcStateName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pCritterEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_FSMState;
	s_event.pchFsmStateName = pcStateName;
	s_event.iPartitionIdx = iPartitionIdx;
	
	if (SAFE_MEMBER2(pCritterEnt, aibase, fsmContext)) {
		s_event.pchFSMName = fsmGetName(pCritterEnt->aibase->fsmContext, 0);
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pCritterEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, false);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordPoke(int iPartitionIdx, Entity *pSourceEnt, Entity **eaTargetEnts, const char *pcMessage)
{
	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_Poke;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchMessage = pcMessage;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pSourceEnt);
	eventsend_AddEntTargets(&s_event, iPartitionIdx, eaTargetEnts);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_EncLayerFSMStateChange(int iPartitionIdx, FSM *pFSM, char *pcStateName)
{
	if (!pFSM || !pcStateName) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_FSMState;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchFSMName = pFSM->name;
	s_event.pchFsmStateName = pcStateName;

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, false);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Mission Events
// ----------------------------------------------------------------------------------

void eventsend_RecordMissionState(int iPartitionIdx, Entity *pEnt, const char *pcPooledMissionRefString, MissionType eMissionType, MissionState eState, const char *pcPooledMissionCategoryName, bool bIsRoot, UGCMissionData *pUGCMissionData)
{
	if (!pcPooledMissionRefString) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_MissionState;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.missionState = eState;
	s_event.missionType = eMissionType;
	s_event.pchMissionRefString = pcPooledMissionRefString;
	s_event.pchMissionCategoryName = pcPooledMissionCategoryName;
	s_event.bIsRootMission = bIsRoot;
	s_event.tPartOfUGCProject = pUGCMissionData ? TriState_Yes : TriState_No;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	// Extra hook for UGC Completed events
	if( pUGCMissionData && bIsRoot && eState == MissionState_TurnedIn ) {
		eventsend_ClearEvent( &s_event );
		s_event.type = EventType_UGCProjectCompleted;
		s_event.tPartOfUGCProject = TriState_Yes;
		s_event.tUGCFeaturedCurrently = pUGCMissionData->bProjectIsFeatured ? TriState_Yes : TriState_No;
		s_event.tUGCFeaturedPreviously = pUGCMissionData->bProjectWasFeatured ? TriState_Yes : TriState_No;
		s_event.tUGCProjectQualifiesForReward = pUGCMissionData->bStatsQualifyForUGCRewards ? TriState_Yes : TriState_No;
		s_event.iPartitionIdx = iPartitionIdx;

		eventsend_AddEntSource( &s_event, iPartitionIdx, pEnt );

		eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true );

		eventsend_ClearParticipantInfo( &s_event );
	}

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordMissionStateMultipleEnts(int iPartitionIdx, Entity ***peaEnts, const char *pcPooledMissionRefString, MissionType eMissionType, MissionState eState, const char *pcPooledMissionCategoryName, bool bIsRoot, UGCMissionData *pUGCMissionData)
{
	if (!pcPooledMissionRefString) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_MissionState;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.missionState = eState;
	s_event.missionType = eMissionType;
	s_event.pchMissionRefString = pcPooledMissionRefString;
	s_event.pchMissionCategoryName = pcPooledMissionCategoryName;
	s_event.bIsRootMission = bIsRoot;
	s_event.tPartOfUGCProject = pUGCMissionData ? TriState_Yes : TriState_No;

	eventsend_AddEntSources(&s_event, iPartitionIdx, peaEnts);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	// Extra hook for UGC Completed events
	if( pUGCMissionData && bIsRoot && eState == MissionState_TurnedIn ) {
		eventsend_ClearEvent( &s_event );
		s_event.type = EventType_UGCProjectCompleted;
		s_event.tPartOfUGCProject = TriState_Yes;
		s_event.tUGCFeaturedCurrently = pUGCMissionData->bProjectIsFeatured ? TriState_Yes : TriState_No;
		s_event.tUGCFeaturedPreviously = pUGCMissionData->bProjectWasFeatured ? TriState_Yes : TriState_No;
		s_event.tUGCProjectQualifiesForReward = pUGCMissionData->bStatsQualifyForUGCRewards ? TriState_Yes : TriState_No;
		s_event.iPartitionIdx = iPartitionIdx;

		FOR_EACH_IN_EARRAY_FORWARDS( *peaEnts, Entity, pEnt ) {
			eventsend_AddEntSource( &s_event, iPartitionIdx, pEnt );
		} FOR_EACH_END;

		eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true );

		eventsend_ClearParticipantInfo( &s_event );
	}

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordMissionLockoutState(int iPartitionIdx, const char *pcPooledMissionRefString, MissionType eMissionType, MissionLockoutState eState, const char *pcPooledMissionCategoryName)
{
	if (!pcPooledMissionRefString) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_MissionLockoutState;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.missionLockoutState = eState;
	s_event.missionType = eMissionType;
	s_event.pchMissionRefString = pcPooledMissionRefString;
	s_event.pchMissionCategoryName = pcPooledMissionCategoryName;

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Powers and Player State Events
// ----------------------------------------------------------------------------------

void eventsend_RecordPowerAttribModApplied(Entity *pSourceEnt, Entity *pTargetEnt, PowerDef *pPowerDef, const char *pcEventName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	if (pSourceEnt)
	{
		iPartitionIdx = entGetPartitionIdx(pSourceEnt);
		if (pTargetEnt && (iPartitionIdx != entGetPartitionIdx(pTargetEnt)))
		{
			return; // Target isn't in partition any more
		}
	}
	else
	{
		iPartitionIdx = entGetPartitionIdx(pTargetEnt);
	}

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_PowerAttrModApplied;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchPowerName = pPowerDef->pchName;
	s_event.pchPowerEventName = pcEventName;

	// Put Source and Target info into event
	eventsend_AddEntSource(&s_event, iPartitionIdx, pSourceEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pTargetEnt);

	// Send event
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordPickupObject(Entity *pLifterEnt, Entity *pLiftedEnt)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pLifterEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_PickedUpObject;
	s_event.iPartitionIdx = iPartitionIdx;

	// Put Source and Target info into event
	eventsend_AddEntSource(&s_event, iPartitionIdx, pLifterEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pLiftedEnt);

	// Send event
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


static void eventsend_RecordHealthStateInternal(Entity *pDamagedEnt, HealthState eState)
{
	s_event.healthState = eState;
	eventtracker_SendEvent(entGetPartitionIdx(pDamagedEnt), &s_event, 1, EventLog_Add, false);
}


void eventsend_RecordHealthState(Entity *pDamagedEnt, F32 fOldHealth, F32 fNewHealth)
{
	int iPartitionIdx;
	F32 fHealthPercentage;
	F32 fHealthPercentageOld;

	if (pDamagedEnt->pChar->pattrBasic->fHitPointsMax <= 0) {
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pDamagedEnt);
	fHealthPercentage = fNewHealth / pDamagedEnt->pChar->pattrBasic->fHitPointsMax * 100.0f;
	fHealthPercentageOld = fOldHealth / pDamagedEnt->pChar->pattrBasic->fHitPointsMax * 100.0f;
		
	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_HealthState;
	s_event.iPartitionIdx = iPartitionIdx;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pDamagedEnt);

	#define SEND_HEALTHSTATE(n, x) \
			if (fHealthPercentage>(n) && fHealthPercentage<(x) && (fHealthPercentageOld<(n) || fHealthPercentageOld>(x))) \
				eventsend_RecordHealthStateInternal(pDamagedEnt, HealthState_##n##_to_##x)
		
		SEND_HEALTHSTATE(75, 100);
		SEND_HEALTHSTATE(67, 100);
		SEND_HEALTHSTATE(50, 100);
		SEND_HEALTHSTATE(50, 75);
		SEND_HEALTHSTATE(33, 67);
		SEND_HEALTHSTATE(25, 50);
		SEND_HEALTHSTATE(0, 100);
		SEND_HEALTHSTATE(0, 50);
		SEND_HEALTHSTATE(0, 33);
		SEND_HEALTHSTATE(0, 25);

	#undef SEND_HEALTHSTATE

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordNewHealthState(Entity *pEnt, F32 fHealth)
{
	F32 fHealthPercentage;
	int iPartitionIdx;

	if (pEnt->pChar->pattrBasic->fHitPointsMax <= 0) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pEnt);
	fHealthPercentage = fHealth / pEnt->pChar->pattrBasic->fHitPointsMax * 100.0f;

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_HealthState;
	s_event.iPartitionIdx = iPartitionIdx;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	#define SEND_HEALTHSTATE(var, n, x)		\
		if((var) > (n) && (var) <= (x))		\
			eventsend_RecordHealthStateInternal(pEnt, HealthState_##n##_to_##x)

		SEND_HEALTHSTATE(fHealthPercentage, 75, 100);
		SEND_HEALTHSTATE(fHealthPercentage, 67, 100);
		SEND_HEALTHSTATE(fHealthPercentage, 50, 100);
		SEND_HEALTHSTATE(fHealthPercentage, 50, 75);
		SEND_HEALTHSTATE(fHealthPercentage, 33, 67);
		SEND_HEALTHSTATE(fHealthPercentage, 25, 50);
		SEND_HEALTHSTATE(fHealthPercentage, 0, 100);
		SEND_HEALTHSTATE(fHealthPercentage, 0, 50);
		SEND_HEALTHSTATE(fHealthPercentage, 0, 33);
		SEND_HEALTHSTATE(fHealthPercentage, 0, 25);

	#undef SEND_HEALTHSTATE

	eventsend_ClearParticipantInfo(&s_event);

	// Hack fix -BF
	// Clear the cached copy of the Participant Info, since this happens
	// before the critter is fully initialized and the Participant Info is incorrect
	if (pEnt && pEnt->pGameEventInfo){
		StructDestroy(parse_GameEventParticipant, pEnt->pGameEventInfo);
		pEnt->pGameEventInfo = NULL;
	}

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordLevelUp(Entity *pEnt)
{
	int iPartitionIdx;

	if (!pEnt) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_LevelUp;
	s_event.iPartitionIdx = iPartitionIdx;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordLevelUpPet(Entity *pOwner)
{
	int iPartitionIdx;

	if (!pOwner) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pOwner);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_LevelUpPet;
	s_event.iPartitionIdx = iPartitionIdx;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pOwner);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordDamage(Entity *pDamagedEnt, Entity *pSourceEnt, int iDamageDone, const char *pcDamageType)
{
	int iPartitionIdx;

	if (!iDamageDone) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pDamagedEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_Damage;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchDamageType = pcDamageType;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pSourceEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pDamagedEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, iDamageDone, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordHealing(Entity *pHealedEnt, Entity *pSourceEnt, int iHealingDone, const char *pcAttribType)
{
	int iPartitionIdx;

	if (!iHealingDone) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pHealedEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_Healing;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchDamageType = pcAttribType;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pSourceEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pHealedEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, iHealingDone, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_ContestWin( Entity *pPlayerEnt, int iRewardTier, const char *pchMissionRefString )
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ContestWin;
	s_event.iPartitionIdx = iPartitionIdx;
	//s_event.iRewardTier = iRewardTier;
	s_event.pchMissionRefString = pchMissionRefString;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Contact Events
// ----------------------------------------------------------------------------------

void eventsend_RecordDialogStart(Entity *pPlayerEnt, ContactDef *pContactDef, const char *pcSpecialDialogName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ContactDialogStart;
	s_event.iPartitionIdx = iPartitionIdx;
	if (pContactDef) {
		s_event.pchContactName = pContactDef->name;
	}
	if (pcSpecialDialogName) {
		s_event.pchDialogName = pcSpecialDialogName;
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordDialogComplete(Entity *pPlayerEnt, ContactDef *pContactDef, const char *pcSpecialDialogName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ContactDialogComplete;
	s_event.iPartitionIdx = iPartitionIdx;
	if (pContactDef) {
		s_event.pchContactName = pContactDef->name;
	}
	if (pcSpecialDialogName) {
		s_event.pchDialogName = pcSpecialDialogName;
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordContactInfoViewed(Entity *pPlayerEnt, ContactDef *pContactDef)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ContactDialogComplete;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchDialogName = allocAddString("ContactInfo"); // The completed dialog is always called "ContactInfo"
	if (pContactDef) {
		s_event.pchContactName = pContactDef->name;
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Interaction Events
// ----------------------------------------------------------------------------------

#define eventsend_AddInteractable(pEvent, pInteractable)\
	if (SAFE_MEMBER(pInteractable, pWorldInteractable))\
		pEvent.eaClickableScopeNames = worldEncObjGetScopeNames((WorldEncounterObject*) pInteractable->pWorldInteractable);\
	if (SAFE_MEMBER2(pInteractable, pWorldInteractable, common_data.parent_group))\
		pEvent.eaClickableGroupScopeNames = worldEncObjGetScopeNames((WorldEncounterObject*) pInteractable->pWorldInteractable->common_data.parent_group);\
	interactable_GetPosition(pInteractable, pEvent.pos)

#define eventsend_AddInteractableVolume(pEvent, pVolume)\
	if (SAFE_MEMBER(pVolume, pNamedVolume))\
		pEvent.eaVolumeScopeNames = worldEncObjGetScopeNames((WorldEncounterObject*) pVolume->pNamedVolume);\
	volume_GetCenterPosition(pEvent.iPartitionIdx, pVolume->pcName, NULL, pEvent.pos)

void eventsend_RecordInteractBegin(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume)
{
	int iPartitionIdx;

	if ((!pInteractable || !pInteractable->pWorldInteractable) && (!pVolume || !pVolume->pNamedVolume) && !pTargetEnt) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_InteractBegin;
	s_event.iPartitionIdx = iPartitionIdx;
	if (pContactDef){
		s_event.pchContactName = pContactDef->name;
	}
		
	if (pInteractable) {
		eventsend_AddInteractable(s_event, pInteractable);
	} else if (pVolume) {
		eventsend_AddInteractableVolume(s_event, pVolume);
	}

	if (!pInteractable && !pVolume && pTargetEnt) {
		entGetPos(pTargetEnt, s_event.pos);
	} else if (pPlayerEnt){
		entGetPos(pPlayerEnt, s_event.pos);
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pTargetEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordInteractSuccess(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume)
{
	int iPartitionIdx;

	if ((!pInteractable || !pInteractable->pWorldInteractable) && (!pVolume || !pVolume->pNamedVolume) && !pTargetEnt) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.iPartitionIdx = iPartitionIdx;
	if (pContactDef){
		s_event.pchContactName = pContactDef->name;
	}
		
	if (pInteractable) {
		eventsend_AddInteractable(s_event, pInteractable);
	} else if (pVolume) {
		eventsend_AddInteractableVolume(s_event, pVolume);
	}

	if (!pInteractable && !pVolume && pTargetEnt) {
		entGetPos(pTargetEnt, s_event.pos);
	} else if (pPlayerEnt){
		entGetPos(pPlayerEnt, s_event.pos);
	}

	// only send ClickableActive for Clickables
	if (s_event.pchClickableName ||	s_event.pchClickableGroupName){
		s_event.type = EventType_ClickableActive;
		eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Set, true); // This is a second send
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pTargetEnt);

	s_event.type = EventType_InteractSuccess; // Type is down here in case of second send in middle
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordInteractableEndActive(int iPartitionIdx, Entity *pPlayerEnt, GameInteractable *pInteractable)
{
	if (!pInteractable || !pInteractable->pWorldInteractable) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.iPartitionIdx = iPartitionIdx;

	eventsend_AddInteractable(s_event, pInteractable);
		
	// only send ClickableActive for Clickables
	if (s_event.pchClickableName ||	s_event.pchClickableGroupName){
		s_event.type = EventType_ClickableActive;
		eventtracker_SendEvent(iPartitionIdx, &s_event, 0, EventLog_Set, true); // This is a second send
	}
		
	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	s_event.type = EventType_InteractEndActive;// Type is down here in case of second send in middle
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordInteractFailure(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume)
{
	int iPartitionIdx;

	if ((!pInteractable || !pInteractable->pWorldInteractable) && (!pVolume || !pVolume->pNamedVolume) && !pTargetEnt) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_InteractFailure;
	s_event.iPartitionIdx = iPartitionIdx;
	if (pContactDef){
		s_event.pchContactName = pContactDef->name;
	}

	if (pInteractable) {
		eventsend_AddInteractable(s_event, pInteractable);
	} else if (pVolume) {
		eventsend_AddInteractableVolume(s_event, pVolume);
	}

	if (!pInteractable && !pVolume && pTargetEnt){
		entGetPos(pTargetEnt, s_event.pos);
	} else if (pPlayerEnt){
		entGetPos(pPlayerEnt, s_event.pos);
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pTargetEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordInteractInterrupted(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume)
{
	int iPartitionIdx;

	if ((!pInteractable || !pInteractable->pWorldInteractable) && (!pVolume || !pVolume->pNamedVolume) && !pTargetEnt) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_InteractInterrupted;
	s_event.iPartitionIdx = iPartitionIdx;
	if (pContactDef){
		s_event.pchContactName = pContactDef->name;
	} 
		
	if (pInteractable) {
		eventsend_AddInteractable(s_event, pInteractable);
	} else if (pVolume) {
		eventsend_AddInteractableVolume(s_event, pVolume);
	}

	if (!pInteractable && !pVolume && pTargetEnt){
		entGetPos(pTargetEnt, s_event.pos);
	} else if (pPlayerEnt){
		entGetPos(pPlayerEnt, s_event.pos);
	}

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pTargetEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Volume Events
// ----------------------------------------------------------------------------------

void eventsend_RecordVolumeEntered(Entity *pEnt, GameNamedVolume *pVolume)
{
	int iPartitionIdx;

	if (!pVolume || !pVolume->pNamedVolume) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_VolumeEntered;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.eaVolumeScopeNames = worldEncObjGetScopeNames((WorldEncounterObject*) pVolume->pNamedVolume);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, false);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordVolumeExited(Entity *pEnt, GameNamedVolume *pVolume)
{
	int iPartitionIdx;

	if (!pVolume || !pVolume->pNamedVolume) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_VolumeExited;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.eaVolumeScopeNames = worldEncObjGetScopeNames((WorldEncounterObject*) pVolume->pNamedVolume);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, false);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Player Spawn Events
// ----------------------------------------------------------------------------------

void eventsend_PlayerSpawnIn(Entity *pPlayerEnt)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_PlayerSpawnIn;
	s_event.iPartitionIdx = iPartitionIdx;
	
	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	
	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Cut Scene Events
// ----------------------------------------------------------------------------------

void eventsend_RecordCutsceneStart(int iPartitionIdx, const char *pcSceneName, EntityRef **peauPlayerRefs)
{
	int iNumEntsWatching, i;

	PERFINFO_AUTO_START_FUNC();
	
	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_CutsceneStart;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchCutsceneName = pcSceneName;

	iNumEntsWatching = eaiSize(peauPlayerRefs);
	for(i = 0; i < iNumEntsWatching; i++) {
		Entity *pPlayerEnt = entFromEntityRef(iPartitionIdx, (*peauPlayerRefs)[i]);
		eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	}

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordCutsceneEnd(int iPartitionIdx, const char *pcSceneName, EntityRef **peauPlayerRefs)
{
	int iNumEntsWatching, i;

	PERFINFO_AUTO_START_FUNC();
	
	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_CutsceneEnd;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchCutsceneName = pcSceneName;

	iNumEntsWatching = eaiSize(peauPlayerRefs);
	for(i = 0; i < iNumEntsWatching; i++) {
		Entity *pPlayerEnt = entFromEntityRef(iPartitionIdx, (*peauPlayerRefs)[i]);
		eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);
	}

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

// ----------------------------------------------------------------------------------
// Sending Video Events
// ----------------------------------------------------------------------------------

void eventsend_RecordVideoStarted(Entity* pPlayerEnt, const char* pchVideoName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	
	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_VideoStarted;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchVideoName = pchVideoName;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordVideoEnded(Entity* pPlayerEnt, const char* pchVideoName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	
	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_VideoEnded;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchVideoName = pchVideoName;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

// ----------------------------------------------------------------------------------
// Sending Item Events
// ----------------------------------------------------------------------------------

void eventsend_RecordItemGained(Entity *pPlayerEnt, const char *pcItemName, ItemCategory *eaiCategories, int iCount)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ItemGained;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchItemName = allocAddString(pcItemName);
	ea32Copy((U32**)&s_event.eaItemCategories, (U32**)&eaiCategories);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, iCount, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordItemLost(Entity *pPlayerEnt, const char *pcItemName, ItemCategory *eaiCategories, int iCount)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ItemLost;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchItemName = allocAddString(pcItemName);
	ea32Copy((U32**)&s_event.eaItemCategories, (U32**)&eaiCategories);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, iCount, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordItemPurchased(Entity *pPlayerEnt, const char *pcItemName, const char *pcStoreName, const char *pcContactName, int iCount, int iEPValue)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ItemPurchased;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchItemName = allocAddString(pcItemName);
	s_event.pchContactName = allocAddString(pcContactName);
	s_event.pchStoreName = allocAddString(pcStoreName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, iCount, EventLog_Add, true);

	if (iCount && iEPValue){
		s_event.type = EventType_ItemPurchaseEP;
		eventtracker_SendEvent(iPartitionIdx, &s_event, iCount*iEPValue, EventLog_Add, true); // This is a second send
	}

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordItemUsed(Entity *pPlayerEnt, const char *pcItemName, const char *pcPowerName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ItemUsed;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchItemName = allocAddString(pcItemName);
	s_event.pchPowerName = allocAddString(pcPowerName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordBagGetsItem(Entity *pPlayerEnt, InvBagIDs eBagType)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_BagGetsItem;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.bagType = eBagType;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordGemSlotted(Entity *pPlayerEnt, const char *pcItemName, const char *pcGemName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_GemSlotted;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchItemName = allocAddString(pcItemName);
	s_event.pchGemName = allocAddString(pcGemName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

// ----------------------------------------------------------------------------------
// Sending Emote Events
// ----------------------------------------------------------------------------------

void eventsend_Emote(Entity *pPlayerEnt, const char *pcEmoteName)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_Emote;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchEmoteName = pcEmoteName;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Nemesis Events
// ----------------------------------------------------------------------------------

void eventsend_RecordNemesisState(Entity *pPlayerEnt, U32 iNemesisID, NemesisState eNewState)
{
	Entity *pNemesisEnt;
	const char *pcNemesisName;
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	pNemesisEnt = entSubscribedCopyFromContainerID(GLOBALTYPE_ENTITYSAVEDPET, iNemesisID);
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	pcNemesisName = pNemesisEnt ? entGetPersistedName(pNemesisEnt) : NULL;

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_NemesisState;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchNemesisName = pcNemesisName;
	s_event.nemesisState = eNewState;
	s_event.iNemesisID = iNemesisID;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pPlayerEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending PvP and Duel Events
// ----------------------------------------------------------------------------------

void eventsend_RecordDuelVictory(Entity *pVictorEnt, Entity *pDefeatedEnt, PVPDuelVictoryType eType)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pVictorEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_DuelVictory;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.victoryType = eType;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pVictorEnt);
	eventsend_AddEntTarget(&s_event, iPartitionIdx, pDefeatedEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordPvPEvent(int iPartitionIdx, PvPEvent ePvPEvent, Entity **eaSourceEnts, Entity **eaTargetEnts)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_PvPEvent;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.ePvPEvent = ePvPEvent;

	if (eaSize(&eaSourceEnts)) {
		for(i=0;i<eaSize(&eaSourceEnts);i++) {
			eventsend_AddEntSource(&s_event, iPartitionIdx, eaSourceEnts[i]);
		}
	}

	if (eaSize(&eaTargetEnts)) {
		for(i=0;i<eaSize(&eaTargetEnts);i++) {
			eventsend_AddEntTarget(&s_event, iPartitionIdx, eaTargetEnts[i]);
		}
	}

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordPvPQueueMatchResult(Entity *pEnt, PvPQueueMatchResult eResult, const char *pcMissionCategory)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_PvPQueueMatchResult;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.ePvPQueueMatchResult = eResult;
	s_event.pchMissionCategoryName = allocAddString(pcMissionCategory);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordScoreboardMetricFinish(Entity *pEnt, const char *pchMetric, S32 iRank)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ScoreboardMetricResult;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchScoreboardMetricName = pchMetric;
	s_event.iScoreboardRank = iRank;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending Minigame Events
// ----------------------------------------------------------------------------------

void eventsend_RecordMinigameBet(Entity* pEnt, MinigameType eMinigameType, const char *pcItemName, int iCount)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_MinigameBet;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.eMinigameType = eMinigameType;
	s_event.pchItemName = allocAddString(pcItemName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, iCount, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordMinigamePayout(Entity* pEnt, MinigameType eMinigameType, const char *pcItemName, int iCount)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_MinigamePayout;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.eMinigameType = eMinigameType;
	s_event.pchItemName = allocAddString(pcItemName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, iCount, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


void eventsend_RecordMinigameJackpot(int iPartitionIdx, Entity *pEnt, MinigameType eMinigameType, GameInteractable *pInteractable)
{
	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_MinigameJackpot;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.eMinigameType = eMinigameType;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventsend_AddInteractable(s_event, pInteractable);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Sending ItemAssignment Events
// ----------------------------------------------------------------------------------

void eventsend_RecordItemAssignmentStarted(int iPartitionIdx, Entity* pEnt, const char* pchName)
{
	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ItemAssignmentStarted;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchItemAssignmentName = allocAddString(pchName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

void eventsend_RecordItemAssignmentCompleted(int iPartitionIdx, Entity* pEnt, const char* pchName, const char* pchOutcome, F32 fItemAssignmentSpeedBonus)
{
	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_ItemAssignmentCompleted;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchItemAssignmentName = allocAddString(pchName);
	s_event.pchItemAssignmentOutcome = allocAddString(pchOutcome);
	s_event.fItemAssignmentSpeedBonus = fItemAssignmentSpeedBonus;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

// ----------------------------------------------------------------------------------
// Sending PowerTree Events
// ----------------------------------------------------------------------------------
void eventsend_RecordPowerTreeStepsAdded(Entity* pEnt)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();
	
	iPartitionIdx = entGetPartitionIdx(pEnt);

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_PowerTreeStepAdded;
	s_event.iPartitionIdx = iPartitionIdx;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

// ----------------------------------------------------------------------------------
// Sending GroupProject Events
// ----------------------------------------------------------------------------------
void eventsend_RecordGroupProjectTaskComplete(int iPartitionIdx, Entity *pEnt, const char *pcProjectName)
{
	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_GroupProjectTaskCompleted;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchGroupProjectName = allocAddString(pcProjectName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

// ----------------------------------------------------------------------------------
// Sending Allegiance Events
// ----------------------------------------------------------------------------------
void eventsend_RecordAllegianceSet(int iPartitionIdx, Entity *pEnt, const char *pcAllegianceName)
{
	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_AllegianceSet;
	s_event.iPartitionIdx = iPartitionIdx;
	s_event.pchAllegianceName = allocAddString(pcAllegianceName);

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}

// ----------------------------------------------------------------------------------
// Sending UGC Account Changed Events
// ----------------------------------------------------------------------------------
void eventsend_RecordUGCAccountChanged(int iPartitionIdx, Entity *pEnt)
{
	PERFINFO_AUTO_START_FUNC();

	eventsend_ClearEvent(&s_event);

	s_event.type = EventType_UGCAccountChanged;
	s_event.iPartitionIdx = iPartitionIdx;

	eventsend_AddEntSource(&s_event, iPartitionIdx, pEnt);

	eventtracker_SendEvent(iPartitionIdx, &s_event, 1, EventLog_Add, true);

	eventsend_ClearParticipantInfo(&s_event);

	PERFINFO_AUTO_STOP();
}
