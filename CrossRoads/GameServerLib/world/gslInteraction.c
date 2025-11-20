/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiAnimList.h"
#include "animlist_common.h"
#include "aiLib.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CommandQueue.h"
#include "contact_common.h"
#include "CombatEvents.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityInteraction.h"
#include "EntityInteraction_h_ast.h"
#include "EntityLib.h"
#include "EntityMovementDoor.h"
#include "EntityMovementManager.h"
#include "EntityMovementInteraction.h"
#include "EntitySavedData.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gslContact.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslGameAction.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslInteractionManager.h"
#include "gslInteractLoot.h"
#include "gslLogSettings.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslSpawnPoint.h"
#include "gslVolume.h"
#include "gslWorldVariable.h"
#include "Guild.h"
#include "interaction_common.h"
#include "InteractionManager_common.h"
#include "logging.h"
#include "MapDescription.h"
#include "mapstate_common.h"
#include "mechanics_common.h"
#include "MemoryPool.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"
#include "PowerActivation.h"
#include "qsortG.h"
#include "queue_common.h"
#include "RegionRules.h"
#include "rand.h"
#include "reward.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "wlGroupPropertyStructs.h"
#include "wlInteraction.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "TriCube/vec.h"
#include "TeamCommands.h"
#include "gslTeamCorral.h"
#include "gslOpenMission.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/mechanics_common_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

// ----------------------------------------------------------------------------------
// Data Definitions
// ----------------------------------------------------------------------------------

// Used for game action transaction
typedef struct InteractionTransactionData
{
	int iPartitionIdx;
	const char *pcInteractableName;
	EntityRef targetEntRef;
	const char *pcVolumeName;
	int iIndex;
	GlobalType eTeammateType;
	ContainerID uTeammateID;
} InteractionTransactionData;

// Callback when requesting map information during an interaction
typedef struct InteractionMapLookupData
{
	const char *pcInteractableName;
	EntityRef targetEntRef;
	const char *pcVolumeName;
	int iIndex;
	GlobalType eTeammateType;
	ContainerID uTeammateID;
	EntityRef playerEntRef;
	int iSeed;
} InteractionMapLookupData;

// Used for movement manager callback
typedef struct InteractionMovementRequestData {
	int iPartitionIdx;
	DoorTarget target;
	EntityRef PlayerEntRef;
	ContainerID iEntityContainerID;
	MovementRequester *pRequester;
	WorldScope *pScope;
	WorldVariable **eaVariables;
	DoorTransitionSequenceDef *pTransOverride;
	GlobalType eOwnerType;
	ContainerID uOwnerID;		// Container ID of the owner, if any
	const char *pcDoorIdentifier;
	bool bIncludeTeammates;
} InteractionMovementRequestData;

static bool interaction_CanEntityInteractWithEnt(Entity *pPlayerEnt, Entity *pCritterEnt, S32 iIndex, GlobalType eTeammateType, ContainerID uTeammateID );

// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

extern StashTable s_lootEventFromTeamId;

bool g_bEnableInteractionDebugLog = false;

#define INTERACTION_SYSTEM_TICK 10
#define INTERACTION_SYSTEM_SLOW_TICK 60

#define MAX_CLICKABLES_TRACKED 50

typedef struct PartitionInteracted
{
	int iPartitionIdx;
	InteractedObjectState **interacted_list;
	F32 fTimeSinceUpdate[INTERACTION_SYSTEM_TICK];
	U32 interactionTick;
} PartitionInteracted;

static PartitionInteracted **s_partition_interacted_list = NULL; // indexed by partition index

MP_DEFINE(InteractedObjectState);


// The bMovedSinceInteractTick flag only breaks interaction if there's been a new keypress.
// This solves an edge case where the player can interact while running past an object.
#define INTERACT_BREAKONMOVE_DIST_SQ (9)

void interaction_PartitionLoad(int iPartitionIdx)
{
	PartitionInteracted *partn = eaGet(&s_partition_interacted_list,iPartitionIdx);
	if(!partn) {
		partn = calloc(sizeof(PartitionInteracted),1);
		partn->iPartitionIdx = iPartitionIdx;
		eaSet(&s_partition_interacted_list,partn,iPartitionIdx);
	}
}


void interaction_PartitionUnload(int iPartitionIdx)
{
	PartitionInteracted *partn = eaGet(&s_partition_interacted_list,iPartitionIdx);
	if(partn) {
		free(partn);
		eaSet(&s_partition_interacted_list,NULL,iPartitionIdx);
	}
}

PartitionInteracted *interaction_GetPartitionState(int iPartitionIdx)
{
	PartitionInteracted *partn = eaGet(&s_partition_interacted_list,iPartitionIdx);
	assertmsgf(partn, "Partition %d does not exist", iPartitionIdx);
	devassertmsgf(partn->iPartitionIdx == iPartitionIdx, "The partition data index doesn't match!");
	return partn;
}


// ----------------------------------------------------------------------------------
// Entity Test Functions
// ----------------------------------------------------------------------------------

bool interaction_IsLootEntity(Entity *pCritterEnt)
{
	if (pCritterEnt && inv_HasLoot(pCritterEnt)) {
		FOR_EACH_IN_CONST_EARRAY(pCritterEnt->pCritter->eaLootBags, InventoryBag, pLootBag)
		{
			if (pLootBag && (pLootBag->pRewardBagInfo->PickupType == kRewardPickupType_Interact ))
				return true;
		}
		FOR_EACH_END
	}
	return false;
}


bool interaction_IsLootEntityOwned(Entity *pCritterEnt, Entity *pPlayerEnt)
{
	if (pCritterEnt && inv_HasLoot(pCritterEnt)) {
		FOR_EACH_IN_CONST_EARRAY(pCritterEnt->pCritter->eaLootBags, InventoryBag, pLootBag)
		{
			if (pLootBag && 
				(pLootBag->pRewardBagInfo->PickupType == kRewardPickupType_Interact ) &&
				reward_MyDrop(pPlayerEnt, pCritterEnt))
				return true;
		}
		FOR_EACH_END
	}
	return false;
}


bool interaction_EvaluateCritterInteractCond(Expression *pExpr, Entity *pPlayerEnt, Entity *pCritterEnt)
{
	static ExprContext *pContext = NULL;
	static ExprFuncTable *pFuncTable = NULL;

	MultiVal mval = {0};
	bool bResult;

	PERFINFO_AUTO_START_FUNC();

	if (!pContext) {
		pContext = exprContextCreate();
		exprContextSetFuncTable(pContext, encounter_CreateInteractExprFuncTable());	// this expression context must be the same as the critterdef and encounter actor expression context
	}

	exprContextSetPointerVarPooled(pContext, g_PlayerVarName, pPlayerEnt, NULL, false, true);
	exprContextSetSelfPtr(pContext, pCritterEnt);
	exprContextSetPartition(pContext, entGetPartitionIdx(pCritterEnt));

	if (pCritterEnt->pCritter->encounterData.pGameEncounter) {
		exprContextSetPointerVarPooled(pContext, g_Encounter2VarName, pCritterEnt->pCritter->encounterData.pGameEncounter, parse_GameEncounter, false, true);
	} else {
		exprContextRemoveVarPooled(pContext, g_Encounter2VarName);
	}

	if (pCritterEnt->pCritter->encounterData.parentEncounter && gConf.bAllowOldEncounterData) {
		exprContextSetPointerVarPooled(pContext, g_EncounterVarName, pCritterEnt->pCritter->encounterData.parentEncounter, parse_OldEncounter, false, true);
	} else {
		exprContextRemoveVarPooled(pContext, g_EncounterVarName);
	}

	exprEvaluate(pExpr, pContext, &mval);

	bResult = (bool)!!MultiValGetInt(&mval, NULL);

	PERFINFO_AUTO_STOP();
	return bResult;
}

static void interaction_AddCritterInteractInfo(Entity *pCritterEnt, WorldInteractionPropertyEntry *pEntry, CritterInteractInfo ***peaInteractCritters)
{
	CritterInteractInfo *pCritterInfo = eaIndexedGetUsingInt(peaInteractCritters, pCritterEnt->myRef);
	if (!pCritterInfo)
	{
		pCritterInfo = StructCreate(parse_CritterInteractInfo);
		pCritterInfo->erRef = pCritterEnt->myRef;
		eaIndexedEnable(peaInteractCritters, parse_CritterInteractInfo);
		eaPush(peaInteractCritters, pCritterInfo);
	}
	if (pEntry->pActionProperties)
	{
		int i;
		for (i = eaSize(&pEntry->pActionProperties->successActions.eaActions)-1; i >= 0; i--)
		{
			WorldGameActionProperties *pGameAction = pEntry->pActionProperties->successActions.eaActions[i];
			if (pGameAction->eActionType == WorldGameActionType_Contact &&
				pGameAction->pContactProperties)
			{
				COPY_HANDLE(pCritterInfo->hActionContactDef, pGameAction->pContactProperties->hContactDef);
				break;
			}
		}
	}
}

void interaction_GetCritterContacts(Entity *pPlayerEnt, Entity *pCritterEnt, bool bTestRange, ContactDef ***peaContacts, CritterInteractInfo ***peaInteractCritters)
{
	CritterDef* pDef;
	PlayerDebug *pDebug;
	int iPartitionIdx;

	if (!pCritterEnt || !pCritterEnt->pCritter) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	pDef = GET_REF(pCritterEnt->pCritter->critterDef);
	pDebug = entGetPlayerDebug(pPlayerEnt, false);
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	if (pCritterEnt->pCritter->encounterData.pGameEncounter) {
		int iNumEntries = interaction_GetNumActorAndCritterEntries(pCritterEnt->pCritter->encounterData.pGameEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, pCritterEnt->pCritter);
		int i;

		for(i=0; i<iNumEntries; ++i) {
			WorldInteractionPropertyEntry *pEntry = interaction_GetActorOrCritterEntry(pCritterEnt->pCritter->encounterData.pGameEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, pCritterEnt->pCritter, i);
			ContactDef *pContactDef = NULL;

			if (pEntry &&
				(!bTestRange || entity_VerifyInteractTarget(iPartitionIdx, pPlayerEnt, pCritterEnt, NULL, 0, 0, 0, false, NULL)) &&
				interaction_CanEntityInteractWithEnt(pPlayerEnt, pCritterEnt, i, 0, 0))
			{
				if ((interaction_GetEffectiveClass(pEntry) == pcPooled_Contact) && (pContactDef = interaction_GetContactDef(pEntry)))
				{
					eaPush(peaContacts, pContactDef);
				}
				else if (peaInteractCritters && entIsAlive(pCritterEnt))
				{
					interaction_AddCritterInteractInfo(pCritterEnt, pEntry, peaInteractCritters);
				}
			}
		}

	} else if(pDef && GET_REF(pDef->hInteractionDef)) {
		int iNumEntries = critter_GetNumInteractionEntries(pCritterEnt->pCritter);
		int i;

		for(i=0; i<iNumEntries; ++i) {
			WorldInteractionPropertyEntry *pEntry = critter_GetInteractionEntry(pCritterEnt->pCritter, i);
			ContactDef *pContactDef = NULL;

			if ((!bTestRange || entity_VerifyInteractTarget(iPartitionIdx, pPlayerEnt, pCritterEnt, NULL, 0, 0, 0, false, NULL)) &&
				interaction_CanEntityInteractWithEnt(pPlayerEnt, pCritterEnt, i, 0, 0))
			{
				if ((interaction_GetEffectiveClass(pEntry) == pcPooled_Contact) && (pContactDef = interaction_GetContactDef(pEntry)))
				{
					eaPush(peaContacts, pContactDef);
				}
				else if (peaInteractCritters && entIsAlive(pCritterEnt))
				{
					interaction_AddCritterInteractInfo(pCritterEnt, pEntry, peaInteractCritters);
				}
			}
		}
	} else if (GET_REF(pCritterEnt->pCritter->encounterData.hContactDefOverride) && gConf.bAllowOldEncounterData) {
		if ((!bTestRange || entity_VerifyInteractTarget(iPartitionIdx, pPlayerEnt, pCritterEnt, NULL, 0, 0, 0, false, NULL)) &&
			interaction_CanEntityInteractWithEnt(pPlayerEnt, pCritterEnt, 0, 0, 0)
			) {
			eaPush(peaContacts, GET_REF(pCritterEnt->pCritter->encounterData.hContactDefOverride));
		}

	} else if (pCritterEnt->pCritter->encounterData.parentEncounter && gConf.bAllowOldEncounterData) {
		if (pCritterEnt->pCritter->encounterData.sourceActor && 
			pCritterEnt->pCritter->encounterData.sourceActor->details.info &&
			GET_REF(pCritterEnt->pCritter->encounterData.sourceActor->details.info->contactScript) &&
			(!bTestRange || entity_VerifyInteractTarget(iPartitionIdx, pPlayerEnt, pCritterEnt, NULL, 0, 0, 0, false, NULL)) &&
			interaction_CanEntityInteractWithEnt(pPlayerEnt, pCritterEnt, 0, 0, 0)
			) {
			eaPush(peaContacts, GET_REF(pCritterEnt->pCritter->encounterData.sourceActor->details.info->contactScript));
		}
	}

	PERFINFO_AUTO_STOP();
}


static bool interaction_IsPlayerInInteractRange(Entity *pPlayerEnt)
{
	WorldInteractionNode *pInteractionNode;
	F32 fDist = 0;
	F32 fRange = 0;

	if (pPlayerEnt->pPlayer->InteractStatus.interactTarget.entRef) {
		Entity *pTargetEnt = entFromEntityRef(entGetPartitionIdx(pPlayerEnt), pPlayerEnt->pPlayer->InteractStatus.interactTarget.entRef);
		if (pTargetEnt) {
			fDist = entGetDistance(pPlayerEnt, NULL, pTargetEnt, NULL, NULL);
		} else {
			fDist = 100000.f;
		}
		fRange = gslEntity_GetInteractRange(pPlayerEnt, pTargetEnt, NULL);
	} else if (pInteractionNode = GET_REF(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode)) {
		fRange = gslEntity_GetInteractRange(pPlayerEnt, NULL, pInteractionNode);
		if (ISZEROVEC3(pPlayerEnt->pPlayer->InteractStatus.interactTarget.vNodeNearPoint)) {
			return entity_IsNodeInRange(pPlayerEnt, NULL, pInteractionNode, fRange, 0, 0, pPlayerEnt->pPlayer->InteractStatus.interactTarget.vNodeNearPoint, &fDist, false);
		} else {
			fDist = entGetDistance( pPlayerEnt, NULL, NULL, pPlayerEnt->pPlayer->InteractStatus.interactTarget.vNodeNearPoint, NULL );
		}
	} else {
		fRange = gslEntity_GetInteractRange(pPlayerEnt, NULL, NULL);
	}
	
	return ( fDist <= fRange );
}


// This function is used to know the range to check for possible entity
// interaction on the server.
U32 interaction_GetEntInteractMaxRange(Entity *pPlayerEnt)
{
	F32 fMaxInteractRange = encounter_GetMaxInteractRange();
	Vec3 vPos;
	WorldRegion *pRegion;

	entGetPos(pPlayerEnt,vPos);

	pRegion = worldGetWorldRegionByPos(vPos);

	if (pRegion) {
		RegionRules *pRules = getRegionRulesFromRegionType(worldRegionGetType(pRegion));

		if ( pRules && pRules->fDefaultInteractDistForEnts > fMaxInteractRange ) {
			return pRules->fDefaultInteractDistForEnts;
		}
	}

	if (fMaxInteractRange > INTERACT_RANGE_FAR) {
		return fMaxInteractRange;
	}

	return INTERACT_RANGE_FAR;
}


// ----------------------------------------------------------------------------------
// Tracking Objects During and After Interaction
// ----------------------------------------------------------------------------------


WorldInteractionPropertyEntry *interaction_GetActorOrCritterEntry(GameEncounter* pEncounter, int iActorIndex, Critter* pCritter, int iInteractIndex)
{
	int numActorEntries;
	CritterDef* pCritDef;
	InteractionDef* pCritInteraction;
	int numCritterEntries; 
	WorldInteractionPropertyEntry* pEntry = NULL;

	if (iInteractIndex < 0) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	numActorEntries = pEncounter && iActorIndex >= 0 ? encounter_GetActorNumInteractionEntries(pEncounter, iActorIndex, pCritter) : 0;

	if(iInteractIndex < numActorEntries) {
		pEntry = encounter_GetActorInteractionEntry(pEncounter, iActorIndex, iInteractIndex);
	} else {
		//Implemented to enable easy implementation of multiple interaction defs on a critter
		pCritDef = pCritter ? GET_REF(pCritter->critterDef) : NULL;
		pCritInteraction = pCritDef ? GET_REF(pCritDef->hInteractionDef) : NULL;
		numCritterEntries = pCritInteraction && pCritInteraction->pEntry ? 1 : 0;

		iInteractIndex -= numActorEntries;
		if(iInteractIndex >= 0 && iInteractIndex < numCritterEntries) {
			pEntry = pCritInteraction->pEntry;
		}
	}

	PERFINFO_AUTO_STOP();

	return pEntry;
}


int interaction_GetNumActorAndCritterEntries(GameEncounter* pEncounter, int iActorIndex, Critter* pCritter)
{
	int numActorEntries;
	CritterDef* pCritDef;
	InteractionDef* pCritInteraction;
	int numCritterEntries;

	PERFINFO_AUTO_START_FUNC();

	numActorEntries = pEncounter && iActorIndex >= 0 ? encounter_GetActorNumInteractionEntries(pEncounter, iActorIndex, pCritter) : 0;
	pCritDef = pCritter ? GET_REF(pCritter->critterDef) : NULL;
	pCritInteraction = pCritDef ? GET_REF(pCritDef->hInteractionDef) : NULL;
	numCritterEntries = pCritInteraction && pCritInteraction->pEntry ? 1 : 0;

	PERFINFO_AUTO_STOP();

	return numActorEntries + numCritterEntries;
}


// Callback used when the list needs to get wiped, either during an encounter layer reset
// or when a world node is getting wiped out.
void interaction_ClearInteractedList(void)
{
	int i;
	for(i = 0; i < eaSize(&s_partition_interacted_list); ++i)
	{
		PartitionInteracted *p = s_partition_interacted_list[i];
		// Don't do anything if there's no list, since (at the moment) this gets called every time an
		// interaction node gets wiped out.
		if(p && p->interacted_list) {
			// Force any objects to exit the list first (to make hidden nodes reappear, etc.).
			interaction_OncePerFrameTimerUpdate(0.0f, true);
			eaDestroy(&p->interacted_list);
		}
	}
}


void interaction_CopyInteractTarget(InteractTarget *pDest, const InteractTarget *pSource)
{
	COPY_HANDLE(pDest->hInteractionNode, pSource->hInteractionNode);
	pDest->bLoot = pSource->bLoot;
	pDest->entRef = pSource->entRef;
	pDest->pcVolumeNamePooled = pSource->pcVolumeNamePooled;
	pDest->iInteractionIndex = pSource->iInteractionIndex;
	pDest->uTeammateID = pSource->uTeammateID;
	pDest->eTeammateType = pSource->eTeammateType;
	copyVec3(pSource->vNodeNearPoint, pDest->vNodeNearPoint);
}


void interaction_AddInteractedObject(int iPartitionIdx, const InteractTarget *pTarget, EntityRef playerEntRef, U32 uiTeamID, F32 fActiveTime, F32 fCooldownTime, bool bNoRespawn, bool bStartInCooldown, bool bTeamUsableWhenActive)
{
	PartitionInteracted *partn = interaction_GetPartitionState(iPartitionIdx);
	MP_CREATE(InteractedObjectState, 50);


	// Remove existing state if already in list
	interaction_RemoveInteractedObject(iPartitionIdx, pTarget);

	// If there's no activeFor or cooldown, this function didn't need to be called in the first place
	if (fActiveTime || fCooldownTime || bNoRespawn) {
		InteractedObjectState *pNewState = MP_ALLOC(InteractedObjectState);
		int i, n;

		interaction_CopyInteractTarget(&pNewState->target, pTarget);
		pNewState->uiTeamID = uiTeamID;
		pNewState->state = bStartInCooldown?InteractedState_Cooldown:InteractedState_Active;
		pNewState->bTeamUsableWhenActive = bTeamUsableWhenActive;
		pNewState->bRespawn = !bNoRespawn;
		pNewState->fActiveTimeRemaining = fActiveTime;
		pNewState->fCooldownTimeRemaining = fCooldownTime;
		pNewState->playerEntRef = playerEntRef;

		// Push object into an empty spot in the list
		n = eaSize(&partn->interacted_list);
		for (i = 0; i < n; i++) {
			if (partn->interacted_list[i] == NULL) {
				partn->interacted_list[i] = pNewState;
				break;
			}
		}
		if (i == n) { 
			// no empty spot was found
			eaPush(&partn->interacted_list, pNewState);
		}
	}
}


void interaction_RemoveInteractedObject(int iPartitionIdx, const InteractTarget *pTarget)
{
	int i, n;
	PartitionInteracted *partn = interaction_GetPartitionState(iPartitionIdx);

	n = eaSize(&partn->interacted_list);
	for (i = 0; i < n; i++) {
		if (partn->interacted_list[i] && INTERACTTARGET_EQUAL(pTarget, &partn->interacted_list[i]->target)) {
			REMOVE_HANDLE(partn->interacted_list[i]->target.hInteractionNode);
			MP_FREE(InteractedObjectState, partn->interacted_list[i]);
			partn->interacted_list[i] = NULL;
			return;
		}
	}
}


void interaction_ResetInteractedNode(int iPartitionIdx, const WorldInteractionNode *pNode)
{
	int i, n;
	PartitionInteracted *partn = interaction_GetPartitionState(iPartitionIdx);

	n = eaSize(&partn->interacted_list);
	for (i = 0; i < n; i++) {
		if (partn->interacted_list[i] && (GET_REF(partn->interacted_list[i]->target.hInteractionNode) == pNode)) {
			partn->interacted_list[i]->bForceReset = true;
			return;
		}
	}
}


const InteractedObjectState* interaction_GetInteractedObjectFromNode(int iPartitionIdx, WorldInteractionNode *pNode)
{
	PartitionInteracted *partn = interaction_GetPartitionState(iPartitionIdx);

	if (pNode) {
		int i, n = eaSize(&partn->interacted_list);
		for (i = 0; i < n; i++) {
			if (partn->interacted_list[i] && (GET_REF(partn->interacted_list[i]->target.hInteractionNode) == pNode)) {
				return partn->interacted_list[i];
			}
		}
	}
	return NULL;
}


// Returns true if the target is in the list of active/cooling down entities
bool interaction_IsInteractTargetBusy2(int iPartitionIdx, const Entity *pPlayerEnt, EntityRef entRef, WorldInteractionNode *pNode, const char *pcVolumeNamePooled, int iIndex)
{
	int i, n;
	PartitionInteracted *partn;

	PERFINFO_AUTO_START_FUNC();
	
	partn = interaction_GetPartitionState(iPartitionIdx);

	n = eaSize(&partn->interacted_list);
	for(i=0; i<n; i++) {
		if (partn->interacted_list[i] && (partn->interacted_list[i]->target.entRef == entRef) && (GET_REF(partn->interacted_list[i]->target.hInteractionNode) == pNode) && (partn->interacted_list[i]->target.pcVolumeNamePooled == pcVolumeNamePooled) && (partn->interacted_list[i]->target.iInteractionIndex == iIndex)) {
			if (partn->interacted_list[i]->state == InteractedState_Active && partn->interacted_list[i]->bTeamUsableWhenActive && pPlayerEnt) {
				ContainerID uiTeamID = team_GetTeamID(pPlayerEnt);
				bool bResult = !(pPlayerEnt->myRef != partn->interacted_list[i]->playerEntRef && uiTeamID && uiTeamID == partn->interacted_list[i]->uiTeamID);

				PERFINFO_AUTO_STOP();
				return bResult;
			} else {
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}


// Returns true if the target interaction node is in the Cooldown state
bool interaction_IsNodeOnCooldown(int iPartitionIdx, WorldInteractionNode *pNode)
{
	int i, n;
	PartitionInteracted *partn = interaction_GetPartitionState(iPartitionIdx);

	n = eaSize(&partn->interacted_list);
	for(i=0; i<n; i++) {
		if (partn->interacted_list[i] && (GET_REF(partn->interacted_list[i]->target.hInteractionNode) == pNode) && partn->interacted_list[i]->state == InteractedState_Cooldown) {
			return true;
		}
	}
	return false;
}


bool interaction_IsExclusive(WorldInteractionPropertyEntry *pEntry, GameInteractable *pInteractable, Entity *pTargetEnt)
{
	const char *pcClass = interaction_GetEffectiveClass(pEntry);

	if (!pEntry) {
		return false;
	}

	if (pEntry->pcInteractionClass == pcPooled_FromDefinition) {
		InteractionDef *pDef = GET_REF(pEntry->hInteractionDef);
		if (pDef) {
			WorldInteractionPropertyEntry *pDefEntry = pDef->pEntry;
			if (pDefEntry) {
				return interaction_EntryGetExclusive(pDefEntry, !!pInteractable);
			} else {
				return false;
			}
		}
		return false;
	} else {
		return interaction_EntryGetExclusive(pEntry, !!pInteractable);
	}
}


const OldInteractionProperties *interaction_GetPropsFromInteractTarget(const InteractTarget *pTarget, Entity *pPlayerEnt)
{
	const OldInteractionProperties *pInteractProps = NULL;

	if (pTarget->entRef == 0 && !IS_HANDLE_ACTIVE(pTarget->hInteractionNode)) {
		return NULL;
	}

	if(pTarget->entRef) {
		Entity *pTargetEnt = entFromEntityRef(entGetPartitionIdx(pPlayerEnt), pTarget->entRef);
		if(pTargetEnt) {
			Critter *pCritter = pTargetEnt->pCritter;
			if( pCritter ) {
				CritterDef *pDef = GET_REF(pCritter->critterDef);
				if (pDef) {
					pInteractProps = &pDef->oldInteractProps;
				}
			}
		}
	}

	return pInteractProps;
}


WorldInteractionPropertyEntry *interaction_GetPropertyEntryFromTarget(int iPartitionIdx, SA_PARAM_NN_VALID const InteractTarget *pTarget, SA_PARAM_OP_VALID GameInteractable **ppInteractableTargetOut, SA_PARAM_OP_VALID Entity **ppEntTargetOut, SA_PARAM_OP_VALID GameNamedVolume **ppVolumeOut)
{
	WorldInteractionPropertyEntry *pEntry = NULL;
	GameInteractable *pInteractable = NULL;
	Entity *pEntTarget = NULL;
	GameNamedVolume *pVolume = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (IS_HANDLE_ACTIVE(pTarget->hInteractionNode)) {
		pInteractable = interactable_GetByNode(GET_REF(pTarget->hInteractionNode));
		if (pInteractable) {
			pEntry = interactable_GetPropertyEntry(pInteractable, pTarget->iInteractionIndex);
		}
	}
	else if (pTarget->entRef) {
		pEntTarget = entFromEntityRef(iPartitionIdx, pTarget->entRef);
		if (SAFE_MEMBER(pEntTarget, pCritter)) {
			if (pEntTarget->pCritter->encounterData.pGameEncounter) {
				pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, pTarget->iInteractionIndex);
			} else {
				pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, pTarget->iInteractionIndex);
			}
		}
	}
	else if (pTarget->pcVolumeNamePooled) {
		pVolume = volume_GetByName(pTarget->pcVolumeNamePooled, NULL);
		if (pVolume) {
			pEntry = volume_GetInteractionPropEntry(pVolume, pTarget->iInteractionIndex);
		}
	}
	if (ppInteractableTargetOut) {
		*ppInteractableTargetOut = pInteractable;
	}
	if (ppEntTargetOut) {
		*ppEntTargetOut = pEntTarget;
	}
	if (ppVolumeOut) {
		*ppVolumeOut = pVolume;
	}

	PERFINFO_AUTO_STOP();
	return pEntry;
}


int interaction_FillLootBag(Entity *pPlayerEnt, InventoryBag ***peaBags, WorldInteractionPropertyEntry *pEntry)
{
	WorldRewardInteractionProperties *pRewardProps = interaction_GetRewardProperties(pEntry);
	InventoryBag **peaLootBags = NULL;
	Team *pTeam = team_GetTeam(pPlayerEnt);
    RewardTable *table;
	U32 iLevel;
	int iNumItems = 0;
	int j;
	int iPartitionIdx;
    
	if (!pRewardProps) {
        return 0;
	}
    
    table = GET_REF(pRewardProps->hRewardTable);
	if (!table) {
        return 0;
	}

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	iLevel = interactable_RewardPropsGetRewardLevel(pPlayerEnt, pRewardProps);
	reward_GenerateInteractableBag(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, table, &peaLootBags, iLevel?iLevel:1, inv_GetNumericItemValue(pPlayerEnt, "SkillLevel"), NULL, NULL);
	if (gConf.bEnableLootModesForInteractables && table->OwnerType == kRewardOwnerType_Team)
	{	
		for(j = eaSize(&peaLootBags)-1; j>=0; --j) {
			InventoryBag *b = peaLootBags[j];
			iNumItems += inv_bag_CountItems(b,0);
			if (pTeam && b && b->pRewardBagInfo) {
				if (pTeam->loot_mode == LootMode_RoundRobin)
				{
					// Round robin is not a valid choice for interaction
					b->pRewardBagInfo->loot_mode = LootMode_FreeForAll;
				}
				else if (pTeam->loot_mode != LootMode_FreeForAll) {
					b->uiTeamOwner = pTeam->iContainerID;
					b->pRewardBagInfo->loot_mode = pTeam->loot_mode;
					b->pRewardBagInfo->eLootModeThreshold = StaticDefineIntGetInt(ItemQualityEnum, pTeam->loot_mode_quality);
				}
			}
		}
	}
	else
	{
		// check that the bags actually contain something.
		for(j = eaSize(&peaLootBags)-1; j>=0; --j) {
			InventoryBag *b = peaLootBags[j];
			iNumItems += inv_bag_CountItems(b,0);
			if (pTeam && b && b->pRewardBagInfo && b->pRewardBagInfo->PickupType == kRewardPickupType_Clickable && b->pRewardBagInfo->OwnerType == kRewardOwnerType_Team) {

				reward_SetupTeamLootBag(iPartitionIdx, b, pTeam, false);

				if (b->pRewardBagInfo->loot_mode == LootMode_RoundRobin)
					b->pRewardBagInfo->loot_mode = LootMode_NeedOrGreed;
				if (b->pRewardBagInfo->loot_mode != LootMode_FreeForAll) {
					b->uiTeamOwner = pTeam->iContainerID;
				}
			}
		}
	}

	eaPushEArray(peaBags, &peaLootBags);
	eaDestroy(&peaLootBags);

	return iNumItems;
}

static void interactionEntry_GenerateLootBags(Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pEntTarget, WorldInteractionPropertyEntry *pEntry)
{
	Team *pTeam = team_GetTeam(pPlayerEnt);
	InteractionLootTracker *pTracker = NULL;
	int iNumItems = 0;
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	if (pInteractable) {
		pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, true);
	} else if (pEntTarget && pEntTarget->pCritter) {
		if (!pEntTarget->pCritter->encounterData.pLootTracker) {
			pEntTarget->pCritter->encounterData.pLootTracker = StructCreate(parse_InteractionLootTracker);
		}
		pTracker = pEntTarget->pCritter->encounterData.pLootTracker;
	}

	if (pTracker) {
		InventoryBag *pCopiedBag = NULL;

		if (eaSize(&pTracker->eaLootBags) > 0) {
			pCopiedBag = StructClone(parse_InventoryBag, pTracker->eaLootBags[0]);
		}
		if (pCopiedBag) {
			if (pCopiedBag->pRewardBagInfo && 
				pCopiedBag->pRewardBagInfo->PickupType == kRewardPickupType_Clickable) {
				BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pCopiedBag));

				// reset ownership on all items and the bag
				for(;!bagiterator_Stopped(iter);bagiterator_Next(iter))
				{
					NOCONST(Item) *pItem = bagiterator_GetItem(iter);
					if (pItem) {
						pItem->owner = 0;
					}
				}
				bagiterator_Destroy(iter);

				pCopiedBag->pRewardBagInfo->loot_mode = pTeam ? pTeam->loot_mode : LootMode_FreeForAll;
				pCopiedBag->pRewardBagInfo->eLootModeThreshold = pTeam ? StaticDefineIntGetInt(ItemQualityEnum, pTeam->loot_mode_quality) : eaSize(&g_ItemQualities.ppQualities);
				pCopiedBag->uiTeamOwner = 0;
				if (pTeam) {
					reward_SetupTeamLootBag(iPartitionIdx, pCopiedBag, pTeam, false);
					pCopiedBag->uiTeamOwner = pTeam->iContainerID;
				}

				eaPush(&pTracker->eaLootBags, pCopiedBag);

			} else {
				RewardPickupType eType = SAFE_MEMBER(pCopiedBag->pRewardBagInfo, PickupType);
				const char* pchType = StaticDefineIntRevLookup(RewardPickupTypeEnum, eType);
				const char* pchInteractableName = SAFE_MEMBER(pInteractable, pcName);
				const char* pchCritterDef = NULL;
				if (pEntTarget && pEntTarget->pCritter) {
					pchCritterDef = REF_STRING_FROM_HANDLE(pEntTarget->pCritter->critterDef);
				}
				if (!pchCritterDef || !pchCritterDef[0]) {
					pchCritterDef = "<None>";
				}
				if (!pchType || !pchType[0]) {
					pchType = "<None>";
				}
				ErrorDetailsf("GameInteractable: %s, Target CritterDef: %s", pchInteractableName, pchCritterDef);
				Errorf("Interaction Loot Bag Generation: Unsupported reward pickup type (%s)", pchType);
			}

		} else {

			iNumItems = interaction_FillLootBag(pPlayerEnt, &pTracker->eaLootBags, pEntry);
			//need to do a special pass for interactables to make sure all the IDs are unique
			reward_FixupRewardItemIDs(&pTracker->eaLootBags);
		}

		if (pInteractable) {
			if (g_bEnableInteractionDebugLog) {
				estrPrintf(&pInteractable->estrDebugLog,"added %i rewards;\n", iNumItems);
			}

			if (!eaSize(&pTracker->eaLootBags)) {
				// Clean up now if no actual loot created
				interactable_ClearLootTracker(iPartitionIdx, pInteractable);
			}
		}
	}
}


static void interaction_StopBeingActive(int iPartitionIdx, const InteractTarget *pTarget, EntityRef playerEntRef)
{
	WorldInteractionPropertyEntry *pEntry = NULL;
	WorldActionInteractionProperties *pActionProps = NULL;
	GameInteractable *pInteractable = NULL;
	GameNamedVolume *pVolume = NULL;
	Entity *pPlayerEnt;
	Entity *pEntTarget = NULL;

	PERFINFO_AUTO_START_FUNC();

	pPlayerEnt = entFromEntityRef(iPartitionIdx, playerEntRef);
	pEntry = interaction_GetPropertyEntryFromTarget(iPartitionIdx, pTarget, &pInteractable, &pEntTarget, &pVolume);

	// execute no-longer-active expression
	pActionProps = SAFE_MEMBER(pEntry, pActionProperties);
	if (pActionProps && pActionProps->pNoLongerActiveExpr) {
		if (pInteractable) {
			interactable_EvaluateNonPlayerExpr(iPartitionIdx, pInteractable, pActionProps->pNoLongerActiveExpr);
		}
		else if (pEntTarget) {
			encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pEntTarget, pActionProps->pNoLongerActiveExpr, NULL);
		}
		else if (pVolume) {
			volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pActionProps->pNoLongerActiveExpr);
		}
	}

	// handle node-specific behavior
	if (pInteractable) {
		interactable_SetNoLongerActive(iPartitionIdx, GET_REF(pTarget->hInteractionNode), pTarget->iInteractionIndex, playerEntRef);
	}

	PERFINFO_AUTO_STOP();
}


static void interaction_CooldownFinished(int iPartitionIdx, SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_NN_VALID const InteractTarget *pTarget)
{
	WorldInteractionPropertyEntry *pEntry = NULL;
	WorldActionInteractionProperties *pActionProps = NULL;
	GameInteractable *pInteractable = NULL;
	GameNamedVolume *pVolume = NULL;
	Entity *pEntTarget = NULL;

	PERFINFO_AUTO_START_FUNC();
	
	pEntry = interaction_GetPropertyEntryFromTarget(iPartitionIdx, pTarget, &pInteractable, &pEntTarget, &pVolume);

	// execute cooldown expression
	pActionProps = SAFE_MEMBER(pEntry, pActionProperties);
	if (pPlayerEnt && pActionProps && pActionProps->pCooldownExpr) {
		if (pInteractable) {
			interactable_EvaluateNonPlayerExpr(iPartitionIdx, pInteractable, pActionProps->pCooldownExpr);
		} else if (pEntTarget) {
			encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pEntTarget, pActionProps->pCooldownExpr, NULL);
		} else if (pVolume) {
			volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pActionProps->pCooldownExpr);
		}
	}

	// handle node-specific behavior
	if (pInteractable) {
		interactable_FinishCooldown(iPartitionIdx, pInteractable, GET_REF(pTarget->hInteractionNode), pTarget->iInteractionIndex, pPlayerEnt);
	}

	PERFINFO_AUTO_STOP();
}


static F32 interaction_GetCooldownMultiplier(int iPartitionIdx, SA_PARAM_NN_VALID const InteractTarget *pTarget)
{
	if (IS_HANDLE_ACTIVE(pTarget->hInteractionNode)) {
		return interactable_GetCooldownMultiplier(iPartitionIdx, GET_REF(pTarget->hInteractionNode), pTarget->iInteractionIndex);
	}
	return 0.f;
}

static bool interaction_IsTeamCorral(int iPartitionIdx, const InteractTarget *pTarget)
{
	WorldInteractionPropertyEntry *pEntry = NULL;
	GameInteractable *pInteractable = NULL;
	GameNamedVolume *pVolume = NULL;
	Entity *pEntTarget = NULL;

	if (!pTarget)
	{
		return false;
	}

	pEntry = interaction_GetPropertyEntryFromTarget(iPartitionIdx, pTarget, &pInteractable, &pEntTarget, &pVolume);

	return (pEntry ? pEntry->pcInteractionClass == pcPooled_TeamCorral : false);
}

static void interaction_OncePerFrameTimerUpdatePartition(PartitionInteracted *partn, F32 fTimeStep, bool bForce)
{
	F32 fTimeStepThisTick;
	int i, n;
	int iPartitionIdx = SAFE_MEMBER(partn,iPartitionIdx);

	// Skip if time paused on partition
	if (mapState_IsMapPausedForPartition(iPartitionIdx)) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	n = eaSize(&partn->interacted_list);

	for (i = 0; i < INTERACTION_SYSTEM_TICK; i++) {
		partn->fTimeSinceUpdate[i] += fTimeStep;
	}

	PERFINFO_AUTO_START("UpdateDynamicRate", 1);

	// Slow tick updates the dynamic spawn rates
	for(i=partn->interactionTick%INTERACTION_SYSTEM_SLOW_TICK; i<n; i+=INTERACTION_SYSTEM_SLOW_TICK) {
		InteractedObjectState *pObject = eaGet(&partn->interacted_list,i);

		if (pObject && pObject->state == InteractedState_Cooldown) {
			pObject->fCooldownMultiplier = interaction_GetCooldownMultiplier(iPartitionIdx, &pObject->target);
		} else if (pObject) {
			pObject->fCooldownMultiplier = 0.f;
		}
	}

	PERFINFO_AUTO_STOP(); // UpdateDynamicRate

	PERFINFO_AUTO_START("UpdateTimer", 1);

	// See if each object is done with whatever it's doing
	fTimeStepThisTick = partn->fTimeSinceUpdate[partn->interactionTick % INTERACTION_SYSTEM_TICK];
	for(i=partn->interactionTick%INTERACTION_SYSTEM_TICK; i<n; i+=INTERACTION_SYSTEM_TICK) {
		// There are NULL objects in this list!  This is necessary for the time to remain
		// accurate when objects are inserted/removed from the list
		// TODO: We need to clean these up eventually, though.
		InteractedObjectState *pObject = eaGet(&partn->interacted_list,i);
		if (pObject) {
			bool bHandledThisFrame = false;
			bool bForceRemoval = pObject->bForceReset || bForce;
			if (pObject->state == InteractedState_Active) {

				//If this is a team corral interact we need to check whether the countdown can start
				if (interaction_IsTeamCorral(iPartitionIdx, &pObject->target))
				{
					Entity *pEnt = entFromEntityRef(partn->iPartitionIdx, pObject->playerEntRef);
					TeamCorralInfo *pCorral = gslTeam_FindTeamCorralInfo(pEnt);

					if(!pCorral)
					{
						pObject->fActiveTimeRemaining = 0;
						pObject->fCooldownTimeRemaining = 0;
					}
					else if (pCorral->bIsTeamReady)//If we are ready
					{
						pObject->fActiveTimeRemaining = 0;
						if (pCorral->uiCountdownTimer == 0)
						{
							pCorral->uiCountdownTimer = pObject->fCooldownTimeRemaining + timeSecondsSince2000();
						}
						pObject->state = InteractedState_Cooldown;
					}
					else
					{
						pObject->state = InteractedState_Active;
						pObject->fCooldownTimeRemaining = gslTeam_GetTeamTransferTimeDefault();
						pCorral->uiCountdownTimer = 0;
					}

				}
				else
				{
					pObject->fActiveTimeRemaining -= fTimeStepThisTick;
				}

				// if object is usable by the team in this state, postpone cooldown until no one is interacting
				// with the object
				if (!bForceRemoval && pObject->bTeamUsableWhenActive) {
					GameInteractable *pInteractable = interactable_GetByNode(GET_REF(pObject->target.hInteractionNode));
					if (interactable_GetPlayerOwner(iPartitionIdx, pInteractable)) {
						continue;
					}
				}

				if (bForceRemoval || pObject->fActiveTimeRemaining <= 0) {
					// Object was active but isn't any longer
					// Note: This may remove the object from the interacted list
					interaction_StopBeingActive(iPartitionIdx, &pObject->target, pObject->playerEntRef);
					bHandledThisFrame = true;

					// Change to cooldown state, unless this object never respawns (it never leaves cooldown)
					if (eaGet(&partn->interacted_list,i) == pObject) {
						if(pObject->bRespawn) {
							pObject->state = InteractedState_Cooldown;
						} else {
							pObject->state = InteractedState_NoRespawn;
						}
					} else {
						// object_StopBeingActive modified the list; it's probably best to stop processing this object
						continue;
					}
				}
			}

			if (pObject->state == InteractedState_Cooldown)
			{
				if (interaction_IsTeamCorral(iPartitionIdx, &pObject->target))
				{
					Entity *pEnt = entFromEntityRef(partn->iPartitionIdx, pObject->playerEntRef);
					TeamCorralInfo *pCorral = gslTeam_FindTeamCorralInfo(pEnt);

					if(!pCorral)
					{
						pObject->fActiveTimeRemaining = 0;
						pObject->fCooldownTimeRemaining = 0;
					}
					else
					{
						if (pCorral->uiCountdownTimer == 0)
						{
							pCorral->uiCountdownTimer = pObject->fCooldownTimeRemaining + timeSecondsSince2000();
						}

						if (!pCorral->bIsTeamReady)
						{
							pCorral->uiCountdownTimer = 0;
							pObject->state = InteractedState_Active;
							pObject->fCooldownTimeRemaining = gslTeam_GetTeamTransferTimeDefault();
						}
					}
				}
			}
			// NoRespawn is like cooldown, but never gets evaluated unless we're force-resetting all objects
			if (((!bHandledThisFrame || bForceRemoval) && pObject->state == InteractedState_Cooldown)
				|| (bForceRemoval && pObject->state == InteractedState_NoRespawn)) {
				Entity *pEnt = entFromEntityRef(partn->iPartitionIdx, pObject->playerEntRef);
				if (pObject->fCooldownMultiplier > 0) {
					pObject->fCooldownTimeRemaining -= fTimeStepThisTick*pObject->fCooldownMultiplier;
				} else {
					pObject->fCooldownTimeRemaining -= fTimeStepThisTick;
				}
				if (bForceRemoval || (pObject->fCooldownTimeRemaining <= 0) && (!interaction_IsTeamCorral(iPartitionIdx, &pObject->target) || (gslTeam_FindTeamCorralInfo(pEnt) && gslTeam_FindTeamCorralInfo(pEnt)->bIsCountdownComplete))) {
					Entity *pPlayerEnt = entFromEntityRef(partn->iPartitionIdx, pObject->playerEntRef);
					interaction_CooldownFinished(partn->iPartitionIdx, pPlayerEnt, &pObject->target);

					// Object is removed from this list
					REMOVE_HANDLE(pObject->target.hInteractionNode);
					MP_FREE(InteractedObjectState, pObject);
					eaSet(&partn->interacted_list,NULL,i);
				}
				else if (bForceRemoval || (pObject->fCooldownTimeRemaining <= 0) && interaction_IsTeamCorral(iPartitionIdx, &pObject->target) && !gslTeam_FindTeamCorralInfo(pEnt))
				{
					gslTeam_DestroyTeamCorral(gslTeam_FindTeamCorralInfo(pEnt));
					// Object is removed from this list
					REMOVE_HANDLE(pObject->target.hInteractionNode);
					MP_FREE(InteractedObjectState, pObject);
					eaSet(&partn->interacted_list,NULL,i);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP(); // UpdateTimer

	partn->fTimeSinceUpdate[partn->interactionTick % INTERACTION_SYSTEM_TICK] = 0.0f;
	partn->interactionTick++;

	PERFINFO_AUTO_STOP();
}


void interaction_OncePerFrameTimerUpdate(F32 fTimeStep, bool bForce)
{
	int i;
	int n = eaSize(&s_partition_interacted_list);
	for (i = 0; i < n; ++i) {
		PartitionInteracted *p = s_partition_interacted_list[i];
		if (p) {
			devassert(p->iPartitionIdx == i);
			interaction_OncePerFrameTimerUpdatePartition(p,fTimeStep,bForce);
		}
	}
}

// ----------------------------------------------------------------------------------
// Performing Interaction
// ----------------------------------------------------------------------------------

static ContactDef *interaction_GetCritterContactDef(Critter *pCritter)
{
	ContactDef *pContactDef = NULL;

	if (!pCritter) {
		return NULL;
	}

	pContactDef = GET_REF(pCritter->encounterData.hContactDefOverride);
	if (!pContactDef && pCritter->encounterData.sourceActor && pCritter->encounterData.sourceActor->details.info) {
		pContactDef = GET_REF(pCritter->encounterData.sourceActor->details.info->contactScript);
	}

	return pContactDef;
}
void interaction_SetCooldownForPlayer(Entity* pPlayerEnt, GameInteractable *pLootInteractable, int idxEntry, int iPartitionIdx)
{
	WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pLootInteractable, idxEntry);
	if (pEntry) {
		WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
		WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
		WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);
		WorldChairInteractionProperties *pChairProps = interaction_GetChairProperties(pEntry);
		F32 fCooldownTime = interaction_GetCooldownTime(pEntry);
		InteractTarget fakeTarget;

		fakeTarget.entRef = 0;
		fakeTarget.iInteractionIndex = idxEntry;
		fakeTarget.bLoot = true;
		fakeTarget.pcVolumeNamePooled = NULL;
		fakeTarget.uTeammateID = 0;
		fakeTarget.eTeammateType = 0;
		REF_HANDLE_COPY(fakeTarget.hInteractionNode, pLootInteractable->pWorldEntry->hInteractionNode);

		// Add to interacted object list if has active time or cooldown time
		if (pPlayerEnt && pPlayerEnt->pPlayer && pTimeProps && ((pTimeProps->fActiveTime > 0) || (fCooldownTime > 0) || pTimeProps->bNoRespawn)) {
			interaction_AddInteractedObject(iPartitionIdx, &fakeTarget, pPlayerEnt->myRef, team_GetTeamID(pPlayerEnt), pTimeProps->fActiveTime, fCooldownTime, pTimeProps->bNoRespawn, false, pTimeProps->bTeamUsableWhenActive);
		}

		REF_HANDLE_REMOVE(fakeTarget.hInteractionNode);
	}

}

void interaction_LootResolved(GameInteractable *pLootInteractable, int idxEntry, int iPartitionIdx)
{
	WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pLootInteractable, idxEntry);
	if (pEntry) {
		WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
		WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
		WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);
		WorldChairInteractionProperties *pChairProps = interaction_GetChairProperties(pEntry);
		F32 fCooldownTime = interaction_GetCooldownTime(pEntry);

		// Add to interacted object list if has active time or cooldown time
		if (pTimeProps && ((pTimeProps->fActiveTime > 0) || (fCooldownTime > 0) || pTimeProps->bNoRespawn)) {

			// If the interactable has an active time, mark it as active
			if (pLootInteractable && pTimeProps && pTimeProps->fActiveTime > 0 && !pTimeProps->bTeamUsableWhenActive) {
				interactable_SetActive(iPartitionIdx, pLootInteractable, true);
			}
		}
		// Refresh loot (if succeeded and it can possibly respawn)
		if ((!pTimeProps || !pTimeProps->bNoRespawn)) {
			if (pLootInteractable) {
				InteractionLootTracker **ppLootTracker = interactable_GetLootTrackerAddress(iPartitionIdx, pLootInteractable);
				LootTracker_Cleanup(ppLootTracker);
			}
		}
	}
}

// Not to be confused with player_EndInteractionAndDialog below.  This clears the interact bar, but doesn't
// clear any dialogs (contacts, stores, tailors) that might be up.
void interaction_DoneInteracting(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bSucceeded, bool bInterrupt)
{
	InteractTarget *pTarget;
	WorldInteractionPropertyEntry *pEntry = NULL;
	const OldInteractionProperties *pIntProps = NULL;
	GameInteractable *pInteractable = NULL;
	Entity *pEntTarget = NULL;
	GameNamedVolume *pVolume = NULL;
	ContactDef *pContactDef = NULL;

	PERFINFO_AUTO_START_FUNC();

	pTarget = &pPlayerEnt->pPlayer->InteractStatus.interactTarget;

	// Destroy the pathing since we're done with the interaction
	im_InteractDestroyPathing(pPlayerEnt);

	if (GET_REF(pTarget->hInteractionNode)) {
		pInteractable = interactable_GetByNode(GET_REF(pTarget->hInteractionNode));

		if (pInteractable) {
			if (pTarget->bLoot) {
				interactloot_EndInteract(pPlayerEnt, 0, pInteractable->pcName);
			}

			pEntry = interactable_GetPropertyEntry(pInteractable, pTarget->iInteractionIndex);
			pContactDef = interaction_GetContactDef(pEntry);
		}
	} else if (pTarget->entRef) {
		pEntTarget = entFromEntityRef(iPartitionIdx, pTarget->entRef);

		// notify loot system of interaction ending
		if (pTarget->bLoot) {
			interactloot_EndInteract(pPlayerEnt, pTarget->entRef, NULL);
		}

		if (pEntTarget && pEntTarget->pCritter) {
			if (pEntTarget->pCritter->encounterData.pGameEncounter) {
				pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, pTarget->iInteractionIndex);
				pContactDef = interaction_GetContactDef(pEntry);
			} else if(critter_GetNumInteractionEntries(pEntTarget->pCritter) > 0) {
				pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, pTarget->iInteractionIndex);
			} else if (GET_REF(pEntTarget->pCritter->encounterData.hContactDefOverride) && gConf.bAllowOldEncounterData) {
				pContactDef = GET_REF(pEntTarget->pCritter->encounterData.hContactDefOverride);
			} else if (pEntTarget->pCritter->encounterData.parentEncounter && gConf.bAllowOldEncounterData) {
				pIntProps = interaction_GetPropsFromInteractTarget(pTarget, pPlayerEnt);
				pContactDef = interaction_GetCritterContactDef(pEntTarget->pCritter);
			}
		}
	} else if (pTarget->pcVolumeNamePooled) {
		pVolume = volume_GetByName(pTarget->pcVolumeNamePooled, NULL);
		pEntry = pVolume ? volume_GetInteractionPropEntry(pVolume, pTarget->iInteractionIndex) : NULL;
	}

	if (pEntry) {
		WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
		WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
		WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);
		WorldChairInteractionProperties *pChairProps = interaction_GetChairProperties(pEntry);
		F32 fCooldownTime = interaction_GetCooldownTime(pEntry);

		// Execute interaction motion
		im_EndInteract(iPartitionIdx, pInteractable, pEntTarget, pVolume, pEntry, pTarget->iInteractionIndex, pPlayerEnt, bInterrupt);

		if (pEntry->pcInteractionClass == pcPooled_TeamCorral)
		{
			pTimeProps = StructCreate(parse_WorldTimeInteractionProperties);
			pTimeProps->bTeamUsableWhenActive = false;
			pTimeProps->fActiveTime = 1;
			pTimeProps->eCooldownTime = WorldCooldownTime_Custom;
			fCooldownTime = gslTeam_GetTeamTransferTimeDefault();
		}
		// Add to interacted object list if has active time or cooldown time
		if (bSucceeded && pPlayerEnt && pPlayerEnt->pPlayer && pTimeProps && ((pTimeProps->fActiveTime > 0) || (fCooldownTime > 0) || pTimeProps->bNoRespawn)) {
			interaction_AddInteractedObject(iPartitionIdx, &pPlayerEnt->pPlayer->InteractStatus.interactTarget, pPlayerEnt->myRef, team_GetTeamID(pPlayerEnt), pTimeProps->fActiveTime, fCooldownTime, pTimeProps->bNoRespawn, false, pTimeProps->bTeamUsableWhenActive);
			
			// If the interactable has an active time, mark it as active
			if (pInteractable && pTimeProps && pTimeProps->fActiveTime > 0 && !pTimeProps->bTeamUsableWhenActive) {
				interactable_SetActive(iPartitionIdx, pInteractable, true);
			}
		}

		// End crafting if necessary
		if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pInteractInfo && (interaction_GetEffectiveClass(pEntry) == pcPooled_CraftingStation)) {
			pPlayerEnt->pPlayer->pInteractInfo->bCrafting = false;
			pPlayerEnt->pPlayer->pInteractInfo->eCraftingTable = kSkillType_None;
			pPlayerEnt->pPlayer->pInteractInfo->iCraftingMaxLevel = 0;
			entity_SetDirtyBit(pPlayerEnt, parse_InteractInfo, pPlayerEnt->pPlayer->pInteractInfo, false);
			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
		}

		// Refresh loot (if succeeded and it can possibly respawn)
		if (bSucceeded && (!pTimeProps || !pTimeProps->bNoRespawn)) {
			if (pInteractable) {
				InteractionLootTracker **ppLootTracker = interactable_GetLootTrackerAddress(iPartitionIdx, pInteractable);
				LootTracker_Cleanup(ppLootTracker);
			} else if (SAFE_MEMBER2(pEntTarget, pCritter, encounterData.pLootTracker)) {
				LootTracker_Cleanup(&pEntTarget->pCritter->encounterData.pLootTracker);
			}
		}

		// Finish sitting, if necessary
		if (pChairProps && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->InteractStatus.bSittingInChair) {
			pPlayerEnt->pPlayer->InteractStatus.bSittingInChair = false;
			entity_SetDirtyBit(pPlayerEnt, parse_EntInteractStatus, &pPlayerEnt->pPlayer->InteractStatus, true);
			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
		}
	} else if (pIntProps) {
		if (bSucceeded) {
			if (pIntProps->uInteractActiveFor || pIntProps->uInteractCoolDown || (pIntProps->eInteractType & InteractType_NoRespawn)) {
				interaction_AddInteractedObject(iPartitionIdx, pTarget, pPlayerEnt->myRef, team_GetTeamID(pPlayerEnt), pIntProps->uInteractActiveFor, pIntProps->uInteractCoolDown, (pIntProps->eInteractType & InteractType_NoRespawn), false, false);
			}
		}
	}

	// Send event for interaction success or interrupt
	if (pInteractable || pEntTarget || pVolume) {
		if (bSucceeded) {
			eventsend_RecordInteractSuccess(pPlayerEnt, pEntTarget, pContactDef, pInteractable, pVolume);
		} else if (bInterrupt) {
			eventsend_RecordInteractInterrupted(pPlayerEnt, pEntTarget, pContactDef, pInteractable, pVolume);
			ClientCmd_NotifySend(pPlayerEnt, kNotifyType_InteractionInterrupted, entTranslateMessageKey(pEntTarget, "Interaction.Interrupted"), NULL, NULL);
		} else {
			// Champions depends on dialogs "failing" in order to function properly.  [CO-80714]  So we can't send this notify without reworking something.
			//ClientCmd_NotifySend(pPlayerEnt, kNotifyType_InteractionFailed, entTranslateMessageKey(pEntTarget, "MechanicsUI.InteractFailed"), NULL, NULL);
		}
	}

	if ( pPlayerEnt->pPlayer->InteractStatus.bInteracting ) {
		pPlayerEnt->pPlayer->InteractStatus.bInteracting = false;
		entity_SetDirtyBit(pPlayerEnt, parse_EntInteractStatus, &pPlayerEnt->pPlayer->InteractStatus, false);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}

	// Clear the interaction state
	// DO THIS LAST BECAUSE IT DESTROYS STATE INFORMATION
	if (pInteractable) {
		interactable_SetPlayerOwner(iPartitionIdx, pInteractable, 0);
		interactable_SetLastInteractIndex(iPartitionIdx, pInteractable, 0);
	} else if (pEntTarget && pEntTarget->pCritter) {
		pEntTarget->pCritter->encounterData.iPlayerOwnerID = 0;

		// Remove the player from the list of interacting players
		ea32FindAndRemove(&pEntTarget->pCritter->encounterData.perInteractingPlayers, pPlayerEnt->myContainerID);
	}
	interaction_ClearPlayerInteractState(pPlayerEnt);
	entDeleteUIVar(pPlayerEnt, "Interact");

	PERFINFO_AUTO_STOP();
}

void interaction_EndInteraction(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bSucceeded, bool bInterrupt, bool bPersistCleanup)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer) {

		// If interrupted, play interrupt sound and run interrupt expression
		if (bInterrupt) {
			InteractTarget *pTarget = &pPlayerEnt->pPlayer->InteractStatus.interactTarget;
			WorldInteractionPropertyEntry *pEntry = NULL;
			GameInteractable *pInteractable = NULL;
			GameNamedVolume *pVolume = NULL;
			Entity *pEntTarget = NULL;

			pEntry = interaction_GetPropertyEntryFromTarget(iPartitionIdx, pTarget, &pInteractable, &pEntTarget, &pVolume);

			if (pEntry) {
				WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);
				if(pSoundProps && pSoundProps->pchInterruptSound) {
					if(pInteractable || pVolume) {		
						Vec3 vPos = {0,0,0};
						if (pInteractable) {
							interactable_GetPosition(pInteractable, vPos);
						} else if (pVolume) {
							volume_GetCenterPosition(iPartitionIdx, pTarget->pcVolumeNamePooled, NULL, vPos);
						}
						mechanics_playOneShotSoundAtLocation(iPartitionIdx, vPos, NULL, pSoundProps->pchInterruptSound, NULL);
					} else if (pEntTarget) {
						mechanics_playOneShotSoundFromEntity(iPartitionIdx, pEntTarget, pSoundProps->pchInterruptSound, NULL);
					}
				}

				if (pEntry->pActionProperties && pEntry->pActionProperties->pInterruptExpr) {
					if (pInteractable) {
						interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pEntry->pActionProperties->pInterruptExpr);
					} else if (pEntTarget) {
						encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pEntTarget, pEntry->pActionProperties->pInterruptExpr, NULL);
					} else if (pVolume) {
						volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pEntry->pActionProperties->pInterruptExpr);
					}
				}

				if(pSoundProps && pSoundProps->pchAttemptSound) {
					if(pInteractable || pVolume) {		
						Vec3 vPos = {0,0,0};
						if (pInteractable) {
							interactable_GetPosition(pInteractable, vPos);
						} else if (pVolume) {
							volume_GetCenterPosition(iPartitionIdx, pTarget->pcVolumeNamePooled, NULL, vPos);
						}
						mechanics_stopOneShotSoundAtLocation(iPartitionIdx, vPos, NULL, pSoundProps->pchAttemptSound);
					} else if (pEntTarget) {
						mechanics_stopOneShotSoundFromEntity(iPartitionIdx, pEntTarget, pSoundProps->pchAttemptSound);
					}
				}
			}
		}

		interaction_DoneInteracting(iPartitionIdx, pPlayerEnt, bSucceeded, bInterrupt);
	}
}

// This should be called if interaction is forcibly broken; it will stop the interact bar from filling and
// will close any open dialogs (as far as the server controls which dialogs are open).
void interaction_EndInteractionAndDialog(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bSucceeded, bool bInterrupt, bool bPersistCleanup)
{
	PERFINFO_AUTO_START_FUNC();

	interaction_EndInteraction(iPartitionIdx, pPlayerEnt, bSucceeded, bInterrupt, bPersistCleanup);

	if (pPlayerEnt && pPlayerEnt->pPlayer) 
	{
		// End any "dialogs" that were on-screen
		contact_InteractEnd(pPlayerEnt, bPersistCleanup);

		// Only check for queued contacts it not destroying a player
		if (!entCheckFlag(pPlayerEnt,ENTITYFLAG_DESTROY) && (iPartitionIdx != PARTITION_ENT_BEING_DESTROYED)) {
			contact_MaybeBeginQueuedContact(pPlayerEnt);
		}
	}

	PERFINFO_AUTO_STOP();
}


static ExprContext *interaction_GetCritterInteractContext(Entity *pPlayer, Entity *pTarget)
{
	static ExprContext *pContext = NULL;
	Critter *pCritter = pTarget?pTarget->pCritter:NULL;
	S32 iPartition;

	if (!pContext)
	{
		ExprFuncTable* stTable;
		pContext = exprContextCreate();

		// Functions
		//  Generic, Self, Character, ApplicationSimple
		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable, "util");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsSelf");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsCharacter");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsApplicationSimple");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsAttribMod");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsAffects");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsPowerDef");
		exprContextAddFuncsToTableByTag(stTable, "CEFuncsTrigger");
		exprContextAddFuncsToTableByTag(stTable, "entity");
		exprContextAddFuncsToTableByTag(stTable, "entityutil");
		exprContextAddFuncsToTableByTag(stTable, "ai");
		exprContextAddFuncsToTableByTag(stTable, "encounter_action");
		exprContextAddFuncsToTableByTag(stTable, "player");
		exprContextAddFuncsToTableByTag(stTable, "gameutil");
		exprContextSetFuncTable(pContext, stTable);

	}

	exprContextSetSelfPtr(pContext, pTarget);
	exprContextSetPointerVarPooled(pContext, g_PlayerVarName, pPlayer, NULL, false, true);

	iPartition = entGetPartitionIdx(pPlayer);
	exprContextSetPartition(pContext, iPartition);

	if (pCritter && pCritter->encounterData.pGameEncounter) {
		exprContextSetPointerVarPooled(pContext, g_Encounter2VarName, pCritter->encounterData.pGameEncounter, parse_GameEncounter, false, true);
	} else {
		exprContextRemoveVarPooled(pContext, g_Encounter2VarName);
	}

	if (pCritter && pCritter->encounterData.parentEncounter && gConf.bAllowOldEncounterData) {
		exprContextSetPointerVarPooled(pContext, g_EncounterVarName, pCritter->encounterData.parentEncounter, parse_OldEncounter, false, true);
	} else {
		exprContextRemoveVarPooled(pContext, g_EncounterVarName);
	}

	return pContext;
}


static void interaction_ExecuteCritterInteraction(Entity *pPlayerEnt, Entity *pTargetEnt)
{
	const OldInteractionProperties *pInteractProps = NULL;
	ExprContext *pContext = interaction_GetCritterInteractContext(pPlayerEnt, pTargetEnt);
	Critter *pCritter = pTargetEnt ? pTargetEnt->pCritter : NULL;
	ContactDef *pContactDef = interaction_GetCritterContactDef(pCritter);
	Player *pPlayer = pPlayerEnt ? pPlayerEnt->pPlayer : NULL;
	InteractTarget *pTarget = pPlayer ? (&pPlayer->InteractStatus.interactTarget) : NULL;
	MultiVal mval;
    
	if ( pCritter ) {
		CritterDef *pDef = GET_REF(pCritter->critterDef);
		if (pDef) {
			pInteractProps = &pDef->oldInteractProps;
		}
	}

	// Do the "on interact" action
	if (pInteractProps && pInteractProps->interactAction) {
		exprEvaluate(pInteractProps->interactAction, pContext, &mval);
	}

	// If this is a critter, send a message to its FSM that the interaction took place.
	// TODO: Events are probably a more robust way of doing this, but don't currently support
	// listening for "self" events for non-players 
	if (pTargetEnt && pTargetEnt->pCritter) {
		aiNotifyInteracted(pTargetEnt, pPlayerEnt);
	}

	if (pPlayerEnt && pContactDef) {
		// If this is a contact, start contact interaction
		contact_InteractBegin(pPlayerEnt, pTargetEnt, pContactDef, NULL, NULL);
	} 

	interaction_DoneInteracting(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, true, false);
}


static void interaction_ExecuteCritterInteractionCB(enumTransactionOutcome eOutcome, Entity *pPlayerEnt, void *entRefPtr)
{
	EntityRef targetEntRef = PTR_TO_U32(entRefPtr);
	Entity *pTargetEnt = entFromEntityRef(entGetPartitionIdx(pPlayerEnt), targetEntRef);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		interaction_ExecuteCritterInteraction(pPlayerEnt, pTargetEnt);

	} else if (pPlayerEnt) {
		// The actions failed for some reason; cancel interaction
		interaction_DoneInteracting(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, false);
	}
}


bool DEFAULT_LATELINK_GameSpecific_InteractableGrantReward(Entity* pPlayerEnt, 
														   GameInteractable* pInteractable, 
														   WorldInteractionPropertyEntry* pEntry)
{
	return false;
}

void interaction_DoorAnimationCompleteCB(InteractionMovementRequestData *pData)
{
	ZoneMapInfo *pTargetMapInfo = NULL;
	MapVariable *pVar;
	int i;
	Entity *pEntity = entFromEntityRef(pData->iPartitionIdx, pData->PlayerEntRef);
	
	if(!pEntity || pEntity->myContainerID != pData->iEntityContainerID)
	{
		// no entity or wrong entity (player gone by the time this executed)
		return;
	}

	pVar = mapvariable_GetByNameIncludingCodeOnly(pData->iPartitionIdx, FORCEMISSIONRETURN_MAPVAR);

	// If the player had been in a door-using animation before, they are no longer
	pEntity->pChar->bUsingDoor = false;

	// set the door identifier
	if (pData->target.mapName) {
		pTargetMapInfo = zmapInfoGetByPublicName(pData->target.mapName);
	}
	if (pTargetMapInfo) {
		ZoneMapType eMapType = zmapInfoGetMapType(pTargetMapInfo);
		switch (eMapType)
		{
			case ZMTYPE_MISSION:
			case ZMTYPE_OWNED:
				if (pData->pcDoorIdentifier && pData->pcDoorIdentifier[0]) {
					pEntity->pPlayer->pchLastUsedDoorIdentifier = allocAddString(pData->pcDoorIdentifier);
				}
				break;
			default:
				pEntity->pPlayer->pchLastUsedDoorIdentifier = NULL;
		}
	}

	// If the "ForceMissionReturn" mapvar is set, anything that changes maps should instead just leave the current map
	if (pData->target.mapName && pVar && pVar->pVariable && pVar->pVariable->iIntVal != 0)
	{
		LeaveMapEx(pEntity, pData->pTransOverride);
	}
	// Do not move the player  if there is an active team transfer. This only applies to NNO, the move will happen for all other games.
	else
	{
		SavedMapDescription *pSaved = entity_GetLastMap(pEntity);
		const ZoneMapInfo *pLastInfo = (pSaved ? pSaved->pZoneMapInfo : NULL);
		bool bMapTargetIsSame = false;
		
		if(pTargetMapInfo)
		{
			if (pLastInfo)
			{
				bMapTargetIsSame = zmapInfoGetCurrentName(pLastInfo) == zmapInfoGetCurrentName(pTargetMapInfo);
			}
		}
		else
			bMapTargetIsSame = true;

		if (!gConf.bEnableNNOTeamWarp || (gslTeam_FindAwayTeamTransfer(pEntity) == NULL || bMapTargetIsSame) || gslTeam_FindAwayTeamTransfer(pEntity) == NULL)
		{
			bool bRequested = false;
			
			if (gConf.bInteractMapLeaveConfirm)
			{
				// We might have to adjust the map type here?
				if (!bMapTargetIsSame && zmapInfoGetMapType(NULL) == ZMTYPE_MISSION && pEntity->pPlayer)
				{
					OpenMission * pOpenMission = openmission_GetFromName(entGetPartitionIdx(pEntity),pEntity->pPlayer->missionInfo->pchCurrentOpenMission);

					if (pOpenMission && pOpenMission->pMission->state == MissionState_InProgress)
					{
						// we can add this if we need it.  Leaving a dungeon generally does not involve a partition being specified
						devassert(pData->target.mapPartitionID == 0);

						// Register a PlayerMapMoveConfirm request
						spawnpoint_RequestMoveConfirm(pEntity, pData->target.mapName, pData->target.spawnTarget, pData->target.queueName, pData->eOwnerType, pData->uOwnerID, pData->target.mapContainerID, pData->eaVariables, pData->pScope, pData->pTransOverride,0,pData->bIncludeTeammates);
						bRequested = true;
					}
				}
			}
			
			if (!bRequested)
			{
				spawnpoint_MovePlayerToMapAndSpawn(pEntity, pData->target.mapName, pData->target.spawnTarget, pData->target.queueName, pData->eOwnerType, pData->uOwnerID, pData->target.mapContainerID, pData->target.mapPartitionID, pData->eaVariables, pData->pScope, pData->pTransOverride,0,pData->bIncludeTeammates);
			}
		}
	}

	for(i=eaSize(&pData->eaVariables)-1; i>=0; --i) {
		StructDestroy(parse_WorldVariable, pData->eaVariables[i]);
	}
	eaDestroy(&pData->eaVariables);
	free(pData);
}


bool interaction_DoorGetDestination(WorldDoorInteractionProperties *pDoorProps, Entity *pPlayerEnt, GlobalType eOwnerType, ContainerID uOwnerID, int iSeed, DoorTarget *pDoorTarget)
{
	if (!pDoorTarget) {
		return false;
	}
	ZeroStruct(pDoorTarget);

	if (pDoorProps->eDoorType == WorldDoorType_QueuedInstance) {
		QueueDef *pDef = GET_REF(pDoorProps->hQueueDef);
		if (pDef) {
			pDoorTarget->queueName = pDef->pchName;
			pDoorTarget->mapName = eaGet(&pDef->QueueMaps.ppchMapNames, 0); //TODO(MK): get the correct map name
		}

	} else if (pDoorProps->eDoorType == WorldDoorType_JoinTeammate) {
		if (SAFE_MEMBER(pPlayerEnt, pPlayer)) {
			Team *pTeam = team_GetTeam(pPlayerEnt);
			Entity *pOwnerEnt = entFromContainerIDAnyPartition(eOwnerType, uOwnerID);
			int j;
			for (j = 0; j < eaSize(&pTeam->eaMembers); j++) {
				Entity *pTeammate = GET_REF(pTeam->eaMembers[j]->hEnt);
				if (pTeammate && pTeammate->pPlayer && pTeammate->myEntityType == eOwnerType && pTeammate->myContainerID == uOwnerID &&
					pTeammate->pTeam && pTeammate->pTeam->eState == TeamState_Member &&
					(entGetType(pPlayerEnt) != entGetType(pTeammate) || entGetContainerID(pPlayerEnt) != entGetContainerID(pTeammate)) &&
					pTeammate->pPlayer->pchLastUsedDoorIdentifier && pTeammate->pPlayer->pchLastUsedDoorIdentifier && pTeammate->pPlayer->pchLastUsedDoorIdentifier == pDoorProps->pcDoorIdentifier)
				{
					pDoorTarget->mapName = pTeammate->pPlayer->pchLastUsedDoorMapName;
					pDoorTarget->spawnTarget = pTeammate->pPlayer->pchLastUsedDoorSpawnPointName;
					pDoorTarget->mapContainerID = pTeammate->pPlayer->uLastUsedDoorMapID;
					pDoorTarget->mapPartitionID = pTeammate->pPlayer->uLastUsedDoorPartitionID;
					break;
				}
			}
		}

	} else {
		WorldVariable* dest = worldVariableCalcVariableAndAlloc(entGetPartitionIdx(pPlayerEnt), &pDoorProps->doorDest, pPlayerEnt, iSeed, 0);

		if (!dest) {
			return false;
		} else if (dest->eType != WVAR_MAP_POINT) {
			StructDestroy(parse_WorldVariable, dest);
			return false;
		}

		pDoorTarget->mapName = allocAddString(dest->pcZoneMap);
		pDoorTarget->spawnTarget = allocAddString(dest->pcStringVal);
		StructDestroy(parse_WorldVariable, dest);
	}

	return true;
}


void interaction_EntityUseDoor(Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pTargetEnt, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry, GlobalType eOwnerType, ContainerID uOwnerID, int iSeed, bool bEntryIsOverride)
{
	MovementRequester *pRequester = NULL;
	InteractionMovementRequestData *pData;
	WorldDoorInteractionProperties *pDoorProps;
	WorldTimeInteractionProperties *pTimeProps;
	int i;
	int iPartitionIdx;
	
	if (!pPlayerEnt) {
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	pDoorProps = interaction_GetDoorProperties(pEntry);
	pTimeProps = interaction_GetTimeProperties(pEntry);


	if (pDoorProps) {
		bool bUseCachedDestination = pInteractable && !bEntryIsOverride? !!interactable_GetLastDoorTarget(iPartitionIdx, pInteractable) : false;
		mmRequesterCreateBasicByName(pPlayerEnt->mm.movement, &pRequester, "DoorMovement");

		pData = calloc(1, sizeof(InteractionMovementRequestData));

		if (pInteractable && bUseCachedDestination) {
			DoorTarget *pTarget = interactable_GetLastDoorTarget(iPartitionIdx, pInteractable);
			StructCopyAll(parse_DoorTarget, pTarget, &pData->target);
		} else if (!interaction_DoorGetDestination(pDoorProps, pPlayerEnt, eOwnerType, uOwnerID, iSeed, &pData->target)) {
			free(pData);
			return;
		} else if (pData->target.queueName) {
			interaction_RecordQueueDefInteraction(pPlayerEnt, pData->target.queueName);
		}
		if (pInteractable && !bUseCachedDestination && !bEntryIsOverride && pTimeProps && pTimeProps->fActiveTime > 0.0f) {
			interactable_SetLastDoorTarget(iPartitionIdx, pInteractable, &pData->target);
		}

		if (pInteractable) {
			pData->pScope = pInteractable->pWorldInteractable->common_data.closest_scope;
		} else if (pVolume) {
			pData->pScope = pVolume->pNamedVolume->common_data.closest_scope;
		} else {
			pData->pScope = (WorldScope*) zmapGetScope(NULL);
		}

		pData->iPartitionIdx = iPartitionIdx;
		pData->PlayerEntRef = SAFE_MEMBER(pPlayerEnt, myRef);
		pData->iEntityContainerID = pPlayerEnt->myContainerID;
		pData->pRequester = pRequester;
		pData->eOwnerType = eOwnerType;
		pData->uOwnerID = uOwnerID;
		pData->pcDoorIdentifier = pDoorProps->pcDoorIdentifier;
		pData->bIncludeTeammates = pDoorProps->bIncludeTeammates;

        if ( pDoorProps->bDestinationSameOwner )
        {
            // Use the current map's owner for the map search if the bDestinationSameOwner flag is set.
            pData->eOwnerType = partition_OwnerTypeFromIdx(iPartitionIdx);
            pData->uOwnerID = partition_OwnerIDFromIdx(iPartitionIdx);
        }

		if (pInteractable && bUseCachedDestination) {
			WorldVariable **eaDoorVars = interactable_GetLastDoorVariables(iPartitionIdx, pInteractable);
			eaCopyStructs(&eaDoorVars, &pData->eaVariables, parse_WorldVariable);
		} else if (pDoorProps->eDoorType != WorldDoorType_JoinTeammate) {
			pData->eaVariables = worldVariableCalcVariablesAndAlloc(iPartitionIdx, pDoorProps->eaVariableDefs, pPlayerEnt, iSeed, 0);
			
			if (pInteractable && pTimeProps && pTimeProps->fActiveTime > 0.0f) {
				interactable_SetLastDoorVariables(iPartitionIdx, pInteractable, pData->eaVariables);
			}
		}
		for(i=0; i<eaSize(&pDoorProps->eaOldVariables); ++i) {
			eaPush(&pData->eaVariables, StructClone(parse_WorldVariable, pDoorProps->eaOldVariables[i]));
		}
		pData->pTransOverride = GET_REF(pDoorProps->hTransSequence);

		// Set team cached door destination
		if(team_IsMember(pPlayerEnt)) {
			if(pDoorProps && pDoorProps->eDoorType != WorldDoorType_JoinTeammate && (pDoorProps->bPerPlayer || pDoorProps->bSinglePlayer) && (pDoorProps->eaVariableDefs || pDoorProps->eaOldVariables)) {
				Team *pTeam = team_GetTeam(pPlayerEnt);
				const char* pchDoorKey = NULL;
				for(i = eaSize(&pDoorProps->eaVariableDefs)-1; i>=0; i--) {
					if(stricmp(pDoorProps->eaVariableDefs[i]->pcName, ITEM_DOOR_KEY_MAP_VAR) == 0) {
						break;
					}
				}

				if(i < 0) {
					for(i = eaSize(&pDoorProps->eaOldVariables)-1; i>=0; i--) {
						if(stricmp(pDoorProps->eaOldVariables[i]->pcName, ITEM_DOOR_KEY_MAP_VAR) == 0) {
							break;
						}
					}
					if(i >= 0) {
						pchDoorKey = pDoorProps->eaOldVariables[i]->pcStringVal;
					}

				} else {
					pchDoorKey = pDoorProps->eaVariableDefs[i]->pSpecificValue ? pDoorProps->eaVariableDefs[i]->pSpecificValue->pcStringVal : NULL;
				}

				if(!pDoorProps->bSinglePlayer && pchDoorKey && pTeam && !pTeam->pCachedDestination) {
					WorldInteractionPropertyEntry* pNewEntry = StructClone(parse_WorldInteractionPropertyEntry, pEntry);
					if(!pNewEntry->pDoorProperties->doorDest.pSpecificValue) {
						pNewEntry->pDoorProperties->doorDest.pSpecificValue = StructCreate(parse_WorldVariable);
					}
					
					pNewEntry->pInteractCond = NULL;
					pNewEntry->pAttemptableCond = NULL;
					pNewEntry->pSuccessCond = NULL;
					pNewEntry->pDoorProperties->doorDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
					pNewEntry->pDoorProperties->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
					pNewEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap = StructAllocString(pData->target.mapName);
					pNewEntry->pDoorProperties->doorDest.pSpecificValue->pcStringVal = StructAllocString(pData->target.spawnTarget);
					pNewEntry->pDoorProperties->eaOldVariables = NULL;
					pNewEntry->pDoorProperties->eaVariableDefs = NULL;
					for(i=0; i<eaSize(&pData->eaVariables); ++i) {
						eaPush(&pNewEntry->pDoorProperties->eaOldVariables, StructClone(parse_WorldVariable, pData->eaVariables[i]));
					}
					pTeam->pCachedDestination = StructCreate(parse_CachedDoorDestination);
					pTeam->pCachedDestination->pchDoorKey = StructAllocString(pchDoorKey);
					pTeam->pCachedDestination->pEntry = pNewEntry;
					pTeam->pCachedDestination->uiExpireTime = timeSecondsSince2000() + TEAM_CACHED_DOOR_LIFESPAN;
				}
			}
		}

		if (gConf.bDelayDoorInteraction)
		{
			// STO needs to wait one server tick after this interaction is complete to allow for certain operations to run to completion
			mrDoorStartWithDelay(pRequester, interaction_DoorAnimationCompleteCB, pData, 1, 1);

			//Don't let the entity start interacting again until the door anim is done.  Not sure what to do about moving door tech
			if (pPlayerEnt->pPlayer->InteractStatus.fTimeUntilNextInteract > 0.f) {
				pPlayerEnt->pPlayer->InteractStatus.fTimeUntilNextInteract += MM_SECONDS_PER_STEP;
			}

			// Flag the player as moving through a door, which makes them invincible.  Make sure to clear this in the callback!
			pPlayerEnt->pChar->bUsingDoor = true;
		}
		else
		{
			interaction_DoorAnimationCompleteCB(pData);
		}
	}
}

static void interaction_AddRecentCompletedInteract(Entity *pEntity, GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume)
{
	InteractInfo *pInfo = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);
	int i;

	if(!pInfo || (!pInteractable && !pEntTarget && !pVolume))
		return;

	for (i = eaSize(&pInfo->recentlyCompletedInteracts)-1; i >= 0; i--)
	{
		InteractionInfo *pInteractionInfo = pInfo->recentlyCompletedInteracts[i];
		if ((pInteractable && pInteractionInfo->pchInteractableName == pInteractable->pcName) ||
			(pEntTarget && pInteractionInfo->erTarget == entGetRef(pEntTarget)) ||
			(pVolume && pInteractionInfo->pchVolumeName == pVolume->pcName))
		{
			break;
		}
	}

	if (i < 0)
	{
		InteractionInfo *pNewInteractionInfo;

		if (eaSize(&pInfo->recentlyCompletedInteracts) >= MAX_CLICKABLES_TRACKED)
		{
			pNewInteractionInfo = eaRemove(&pInfo->recentlyCompletedInteracts, 0);
			pNewInteractionInfo->pchInteractableName = NULL;
			pNewInteractionInfo->pchVolumeName = NULL;
			pNewInteractionInfo->erTarget = 0;
		}
		else
		{
			pNewInteractionInfo = StructCreate(parse_InteractionInfo);
		}

		if (pInteractable)
			pNewInteractionInfo->pchInteractableName = allocAddString(pInteractable->pcName);
		if (pEntTarget)
			pNewInteractionInfo->erTarget = entGetRef(pEntTarget);
		if (pVolume)
			pNewInteractionInfo->pchVolumeName = allocAddString(pVolume->pcName);

		eaPush(&pInfo->recentlyCompletedInteracts, pNewInteractionInfo);
		entity_SetDirtyBit(pEntity, parse_InteractInfo, pInfo, false);
	}
}


static void interaction_ContinueInteraction_ProcessProperties(GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID, Entity *pPlayerEnt,
															  WorldInteractionPropertyEntry *pEntry, ZoneMapInfo *pDestZmap, int iSeed)
{
	bool bDoneInteracting = true;
	ContactDef *pContactDef = NULL;
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	if (pEntry) {
		// Perform successful interact action (if any)
		WorldActionInteractionProperties *pActionProps = interaction_GetActionProperties(pEntry);
		WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pEntry);
		WorldCraftingInteractionProperties *pCraftingProps = interaction_GetCraftingProperties(pEntry);
		WorldDoorInteractionProperties *pDoorProps = interaction_GetDoorProperties(pEntry);
		WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);
		WorldRewardInteractionProperties *pRewardProps = interaction_GetRewardProperties(pEntry);
		WorldTextInteractionProperties *pTextProps = interaction_GetTextProperties(pEntry);
		WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
		char *estrBuffer = NULL;

		estrStackCreate(&estrBuffer);

		pContactDef = interaction_GetContactDef(pEntry);

		if (pActionProps && pActionProps->pSuccessExpr && (!team_IsMember(pPlayerEnt) || pEntry->pcInteractionClass != pcPooled_TeamCorral)) {
			if (pInteractable) {
				interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pActionProps->pSuccessExpr);
			} else if (pEntTarget) {
				encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pEntTarget, pActionProps->pSuccessExpr, NULL);
			} else if (pVolume) {
				volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pActionProps->pSuccessExpr);
			}
		}

		if (pEntry->pcInteractionClass == pcPooled_TeamCorral)
		{
			if (gslTeam_FindTeamCorralInfo(pPlayerEnt))
			{
				bDoneInteracting = true;
			} 
			else
			{
				gslTeam_CreateTeamCorral(pPlayerEnt, true);
			}
		}

		// Perform reward action (if any)
		if (pPlayerEnt && pRewardProps && GET_REF(pRewardProps->hRewardTable)) {
			InteractionLootTracker *pTracker = NULL;
			InventoryBag** eaOwnedBags = NULL;

			if (pInteractable && GameSpecific_InteractableGrantReward(pPlayerEnt, pInteractable, pEntry)) {
				bDoneInteracting = true;
			} else if (pInteractable) {
				pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable,false);
				if (!pTracker || !LootTracker_FindOwnedLootBags(pTracker, pPlayerEnt, &eaOwnedBags)) {
					interactionEntry_GenerateLootBags(pPlayerEnt, pInteractable, pEntTarget, pEntry);
					pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable,false);
				}
				LootTracker_FindOwnedLootBags(pTracker, pPlayerEnt, &eaOwnedBags);
			} else if (pEntTarget && pEntTarget->pCritter) {
				if (!pEntTarget->pCritter->encounterData.pLootTracker || !LootTracker_FindOwnedLootBags(pEntTarget->pCritter->encounterData.pLootTracker, pPlayerEnt, &eaOwnedBags)) {
					interactionEntry_GenerateLootBags(pPlayerEnt, pInteractable, pEntTarget, pEntry);
					LootTracker_FindOwnedLootBags(pEntTarget->pCritter->encounterData.pLootTracker, pPlayerEnt, &eaOwnedBags);
				}
				pTracker = pEntTarget->pCritter->encounterData.pLootTracker;
			}

			if (pTracker && eaSize(&pTracker->eaLootBags)) {
				Vec3 vPlayerPos;
				int i;

				entGetPos(pPlayerEnt, vPlayerPos);

				for (i = eaSize(&pTracker->eaLootBags) - 1; i >= 0; --i) {
					// For non-clickable bags, immediately remove and give them to the interacting player
					if (pTracker->eaLootBags[i]->pRewardBagInfo->PickupType != kRewardPickupType_Clickable) {
						InventoryBag *pBag = eaRemove(&pTracker->eaLootBags, i);
						int iCount = inv_bag_CountItems(pBag, NULL);

						//skip empty bags
						if (iCount < 1) {
							StructDestroy(parse_InventoryBag, pBag);
						} else {
							ItemChangeReason reason = {0};
							inv_FillItemChangeReason(&reason, pPlayerEnt, "Interact:GetItemFromBag", NULL);
							reward_GiveBag(pPlayerEnt, pBag, vPlayerPos, false, &reason);
						}
					}
				}

				// Display inventory interaction UI
				if (eaSize(&eaOwnedBags) > 0) {
					interactloot_PerformInteract(pPlayerEnt, pEntTarget, pInteractable, eaOwnedBags);
					bDoneInteracting = false;
				}
			}
		}

		interaction_AddRecentCompletedInteract(pPlayerEnt, pInteractable, pEntTarget, pVolume);

		// Check by type
		// CONTACT
		if (pContactProps) {
			if (pContactDef = GET_REF(pContactProps->hContactDef)) {
				contact_InteractBegin(pPlayerEnt, pEntTarget, pContactDef, pContactProps->pcDialogName, NULL);
				bDoneInteracting = false;
			}
		}

		// CRAFTING STATION
		if (pCraftingProps) {
			if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pInteractInfo) {
				pPlayerEnt->pPlayer->pInteractInfo->bCrafting = true;
				pPlayerEnt->pPlayer->pInteractInfo->eCraftingTable = pCraftingProps->eSkillFlags;
				pPlayerEnt->pPlayer->pInteractInfo->iCraftingMaxLevel = pCraftingProps->iMaxSkill;
				entity_SetDirtyBit(pPlayerEnt, parse_InteractInfo, pPlayerEnt->pPlayer->pInteractInfo, false);
				entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
				bDoneInteracting = false;
			}
		}
		
		// DOOR
		if (pDoorProps) {
			bool bEntryIsOverride = pInteractable && iIndex >= 1000;
			// request team-owned map for non-OWNED maps for per-player doors because OWNED maps must ALWAYS have singleton ownership
			if (pDestZmap && zmapInfoGetMapType(pDestZmap) != ZMTYPE_OWNED && pDoorProps->bPerPlayer && !pDoorProps->bSinglePlayer && team_IsMember(pPlayerEnt) && eTeammateType == GLOBALTYPE_ENTITYPLAYER && team_OnSameTeamID(pPlayerEnt, uTeammateID)) {
				interaction_EntityUseDoor(pPlayerEnt, pInteractable, pEntTarget, pVolume, pEntry, GLOBALTYPE_TEAM, team_GetTeamID(pPlayerEnt), iSeed, bEntryIsOverride);
			} else {
                GlobalType ownerType;
                ContainerID ownerID;
                if ( zmapInfoGetIsGuildOwned(pDestZmap) && guild_IsMember(pPlayerEnt) )
                {
                    ownerType = GLOBALTYPE_GUILD;
                    ownerID = guild_GetGuildID(pPlayerEnt);
                }
                else
                {
                    ownerType = eTeammateType;
                    ownerID = uTeammateID;
                }
				interaction_EntityUseDoor(pPlayerEnt, pInteractable, pEntTarget, pVolume, pEntry, ownerType, ownerID, iSeed, bEntryIsOverride);
			}
		}

		// GATE
		if (pInteractable && pGateProps) {
			interactable_ChangeGateOpenState(iPartitionIdx, pInteractable, pPlayerEnt, !interactable_IsGateOpen(iPartitionIdx, pInteractable));
		}

		// If has a success message, display it
		if (pTextProps && GET_REF(pTextProps->successConsoleText.hMessage)) {
			if (pPlayerEnt){
				Entity *pMapOwner = partition_GetPlayerMapOwner(iPartitionIdx);
				WorldVariable **eaMapVars = NULL;
				estrClear(&estrBuffer);
				mapState_GetAllPublicVars(mapState_FromPartitionIdx(iPartitionIdx),&eaMapVars);
				langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &estrBuffer, &pTextProps->successConsoleText, STRFMT_ENTITY_KEY("MapOwner", pMapOwner), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				if (estrBuffer && estrBuffer[0]) {
					ClientCmd_NotifySend(pPlayerEnt, kNotifyType_InteractionSuccess, estrBuffer, NULL, NULL);
				}
				eaDestroy(&eaMapVars);
			}
		}

		estrDestroy(&estrBuffer);
	}

	// Notify critter of interact success
	if (pEntTarget && pEntTarget->pCritter) {
		aiNotifyInteracted(pEntTarget, pPlayerEnt);
	}

	// Clean up the interaction bar and and set the clickable to be unowned so that other players can use it
	// If the clickable opens up a dialog window, that window should call interaction_EndInteractionAndDialog
	// when it closes.
	if (pPlayerEnt && bDoneInteracting) {
		interaction_DoneInteracting(iPartitionIdx, pPlayerEnt, true, false);
	}
}

static WorldInteractionPropertyEntry* interaction_GetEntryFromInteractionData(GameInteractable* pInteractable, Entity* pEntTarget, GameNamedVolume* pVolume, int iIndex, Entity* pEntPlayer)
{
	WorldInteractionPropertyEntry* pEntry = NULL;
	if (pInteractable) {
		pEntry = interactable_GetPropertyEntryForPlayer(pEntPlayer, pInteractable, iIndex);
	} else if (pEntTarget && pEntTarget->pCritter) {
		if (pEntTarget->pCritter->encounterData.pGameEncounter) {
			pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, iIndex);
		} else {
			pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, iIndex);
		}
	} else if (pVolume) {
		pEntry = volume_GetInteractionPropEntry(pVolume, iIndex);
	}
	return pEntry;
}


static void interaction_RequestZoneMapInfoByPublicNameCB(TransactionReturnVal* pReturn, InteractionMapLookupData* pCBData)
{
	ZoneMapInfoRequest* pZoneMapInfoRequest = NULL;
	ZoneMapInfo* pZoneMapInfo = NULL;
	WorldInteractionPropertyEntry* pEntry;
	GameInteractable* pInteractable;
	GameNamedVolume* pVolume;
	Entity* pEntTarget;
	Entity* pEntPlayer;
	enumTransactionOutcome eOutcome;
	
	eOutcome = RemoteCommandCheck_aslMapManagerRequestZoneMapInfoByPublicName(pReturn, &pZoneMapInfoRequest);
	
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && pZoneMapInfoRequest) {
		pZoneMapInfo = zmapInfoGetFromRequest(pZoneMapInfoRequest);
	}

	pInteractable = interactable_GetByName(pCBData->pcInteractableName, NULL);
	pVolume = volume_GetByName(pCBData->pcVolumeName, NULL);
	pEntTarget = entFromEntityRefAnyPartition(pCBData->targetEntRef);
	pEntPlayer = entFromEntityRefAnyPartition(pCBData->playerEntRef);
	pEntry = interaction_GetEntryFromInteractionData(pInteractable, pEntTarget, pVolume, pCBData->iIndex, pEntPlayer);
	
	if (pEntry && pEntPlayer) {
		interaction_ContinueInteraction_ProcessProperties(pInteractable,
														  pEntTarget,
														  pVolume,
														  pCBData->iIndex,
														  pCBData->eTeammateType,
														  pCBData->uTeammateID,
														  pEntPlayer,
														  pEntry,
														  pZoneMapInfo,
														  pCBData->iSeed);
	}
	StructDestroySafe(parse_ZoneMapInfoRequest, &pZoneMapInfoRequest);
	free(pCBData);
}


void interaction_ContinueInteraction(GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID, Entity *pPlayerEnt)
{
	WorldInteractionPropertyEntry *pEntry = interaction_GetEntryFromInteractionData(pInteractable, pEntTarget, pVolume, iIndex, pPlayerEnt);
	ZoneMapInfo *pDestZmap = NULL;
	int iSeed = randomInt();

	if (pEntry) {
 		WorldDoorInteractionProperties* pDoorProps = interaction_GetDoorProperties(pEntry);
		if (pDoorProps) {
			DoorTarget *pDoorTarget = StructCreate(parse_DoorTarget);

			// get door destination
			if (interaction_DoorGetDestination(pDoorProps, pPlayerEnt, eTeammateType, uTeammateID, iSeed, pDoorTarget)) {
				pDestZmap = zmapInfoGetByPublicName(pDoorTarget->mapName);
			}
			if (!pDestZmap && pDoorTarget->mapName && pDoorTarget->mapName[0]) {
				// If the ZoneMapInfo is not available, request a copy from the map manager before proceeding
				TransactionReturnVal* pReturn;
				InteractionMapLookupData* pCBData = calloc(1, sizeof(InteractionMapLookupData));
				pCBData->pcInteractableName = SAFE_MEMBER(pInteractable, pcName);
				pCBData->targetEntRef = SAFE_MEMBER(pEntTarget, myRef);
				pCBData->pcVolumeName = SAFE_MEMBER(pVolume, pcName);
				pCBData->iIndex = iIndex;
				pCBData->eTeammateType = eTeammateType;
				pCBData->uTeammateID = uTeammateID;
				pCBData->playerEntRef = SAFE_MEMBER(pPlayerEnt, myRef);
				pCBData->iSeed = iSeed;
				pReturn = objCreateManagedReturnVal(interaction_RequestZoneMapInfoByPublicNameCB, pCBData);
				RemoteCommand_aslMapManagerRequestZoneMapInfoByPublicName(pReturn, GLOBALTYPE_MAPMANAGER, 0, pDoorTarget->mapName);
				StructDestroy(parse_DoorTarget, pDoorTarget);
				return;
			}
			StructDestroy(parse_DoorTarget, pDoorTarget);
		}
	}
	interaction_ContinueInteraction_ProcessProperties(pInteractable, pEntTarget, pVolume, iIndex, eTeammateType, uTeammateID, pPlayerEnt, pEntry, pDestZmap, iSeed);
}


static void interaction_InteractPostGameActionCB(enumTransactionOutcome eOutcome, Entity *pPlayerEnt, InteractionTransactionData *pData)
{
	PlayerDebug* pDebug = entGetPlayerDebug(pPlayerEnt, false);

	// This gets called if there are transactional Game Actions to perform when interacting.
	// This is called at the end of the transaction.

	if (pDebug && pDebug->allowAllInteractions || eOutcome == TRANSACTION_OUTCOME_SUCCESS) {
		if (pData) {
			// Continue the non-transactional interaction behaviors
			interaction_ContinueInteraction(interactable_GetByName(pData->pcInteractableName, NULL), entFromEntityRef(pData->iPartitionIdx, pData->targetEntRef), volume_GetByName(pData->pcVolumeName, NULL), pData->iIndex, pData->eTeammateType, pData->uTeammateID, pPlayerEnt);
		}
	} else {
		// The actions failed for some reason, so cancel interaction
		interaction_DoneInteracting(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, false, false);
	}

	// Clean up
	if (pData) {
		free(pData);
	}
}

static bool interaction_EntryIsEnabled(int iPartitionIdx, WorldInteractionPropertyEntry *pEntry, Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pCritterEnt, GameNamedVolume *pVolume) 
{
	PlayerDebug* pDebug;
	const char *pcClass;
	Expression *pInteractCond;

	PERFINFO_AUTO_START_FUNC();

	pcClass = interaction_GetEffectiveClass(pEntry);
	if (pcClass == pcPooled_NamedObject) {
		PERFINFO_AUTO_STOP();
		return false; // Named object properties are never enabled
	}

	pDebug = entGetPlayerDebug(pPlayerEnt, false);
	pInteractCond = interaction_GetInteractCond(pEntry);

	if (pInteractable) {
		if ((!pDebug || !pDebug->allowAllInteractions) && pInteractCond && !interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pInteractCond)) {
			PERFINFO_AUTO_STOP();
			return false;
		}

		// Can interact with a destructible only if it's not otherwise interactable
		if (pcClass == pcPooled_Destructible) {
			bool bResult = (!pPlayerEnt->pChar || !IS_HANDLE_ACTIVE(pPlayerEnt->pChar->hHeldNode)) && 
				interactable_IsDestructibleAndNotInteractable(pInteractable);
			PERFINFO_AUTO_STOP();
			return bResult;
		}
	} else if (pCritterEnt) {
		if ((!pDebug || !pDebug->allowAllInteractions) && pInteractCond && !encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pCritterEnt, pInteractCond, NULL)) {
			PERFINFO_AUTO_STOP();
			return false;
		}
	} else if (pVolume) {
		// No checks for now; most of the checks are in volume_AddInteractOptions
	}

	// Can not interact with a contact if it is a single dialog with no options
	if (pcClass == pcPooled_Contact) {
		WorldContactInteractionProperties *pContactProperties = interaction_GetContactProperties(pEntry);
		ContactDef* pContact;

		if (pContactProperties && !pContactProperties->pcDialogName) {
			pContact = GET_REF(pContactProperties->hContactDef);

			if (pContact && pContact->type == ContactType_SingleDialog && !contact_HasSpecialDialogForPlayer(pPlayerEnt, pContact)) {
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	// If the interactable doesn't do anything, then we should skip it, too
	// Setting "g_InteractableDebug" overrides this hiding of uninteresting interactables
	if (!g_InteractableDebug &&
		(pcClass == pcPooled_Clickable) &&
		!interaction_GetActionProperties(pEntry) && 
		!interaction_GetTimeProperties(pEntry) && 
		!interaction_GetTextProperties(pEntry) && 
		!interaction_GetRewardProperties(pEntry)) {
			PERFINFO_AUTO_STOP();
			return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}


// Returns TRUE if the player can interact with the properties specified by the Index and Teammate ID
static bool interaction_CanEntityInteractWithNode(Entity *pPlayerEnt, GameInteractable *pInteractable, S32 iIndex, GlobalType eTeammateType, ContainerID uTeammateID, bool bForce )
{
	WorldInteractionPropertyEntry *pEntry;
	bool bCanInteract = false;
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	MapState *state = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pInteractable) {
		// No interactable for this node so return false
		return false;
	}

	if (interactable_IsBusy(iPartitionIdx, pInteractable, pPlayerEnt)){
		// Node is in use or on cooldown
		return false;
	}

	// If player has a sync dialog open, block all interaction
	if (pPlayerEnt && team_IsMember(pPlayerEnt) && mapState_GetSyncDialogForTeam(state, team_GetTeamID(pPlayerEnt))) {
		return false;
	}

	//if the entry is disabled, then don't interact
	pEntry = interactable_GetPropertyEntryForPlayer(pPlayerEnt, pInteractable, iIndex);
	if (pEntry) {
		WorldDoorInteractionProperties *pDoorProps = interaction_GetDoorProperties(pEntry);

		// See if we're allowed to interact with this in combat
		if (!gConf.bAlwaysAllowInteractsInCombat && !bForce)
		{
			if (!pEntry->bAllowDuringCombat && entIsInCombat(pPlayerEnt))
			{
				return false;
			}
		}

		if (pDoorProps) {
			if (pDoorProps->bPerPlayer || pDoorProps->bSinglePlayer) {
				// Per-player doors: Evaluate using the teammate specified by iOwnerID
				Entity *pTeammate = NULL;
				int i;

				if (uTeammateID == entGetContainerID(pPlayerEnt) && eTeammateType == entGetType(pPlayerEnt)) {
					pTeammate = pPlayerEnt;
				} else if (!pDoorProps->bSinglePlayer) {
					// Find the correct teammate
					Team *pTeam = team_GetTeam(pPlayerEnt);
					if (pTeam) {
						for (i = 0; i < eaSize(&pTeam->eaMembers); i++){
							if (pTeam->eaMembers[i]->iEntID == uTeammateID){
								pTeammate = GET_REF(pTeam->eaMembers[i]->hEnt);
							}
						}
					}
				}

				// If the specified player is not on this player's team, return false
				if (!pTeammate) {
					return false;
				}

				// If the specified teammate can't interact with this property entry, return false
				if (!interaction_EntryIsEnabled(iPartitionIdx, pEntry, pTeammate, pInteractable, NULL, NULL)) {
					return false;
				}
			} else {
				// Normal behavior: Evaluate using the player
				if (!interaction_EntryIsEnabled(iPartitionIdx, pEntry, pPlayerEnt, pInteractable, NULL, NULL)) {
					return false;
				}
			}
		}
	} else {
		// No property entry found
		return false;
	}

	if (!interactable_EvaluateVisibilityForEntity(pPlayerEnt, pInteractable)) {
		return false;
	}

	return true;
}


static bool interaction_CanEntityInteractWithEnt(Entity *pPlayerEnt, Entity *pCritterEnt, S32 iIndex, GlobalType eTeammateType, ContainerID uTeammateID )
{
	int i;
	bool bCanPickup = false;
	bool bAddedOption = false;
	int iPartitionIdx;

	if (!pCritterEnt || !pCritterEnt->pCritter || !pCritterEnt->pCritter->bIsInteractable) {
		// Critter has no properties that can be interacted with
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	// Loot interaction is handled special
	if ( interaction_IsLootEntityOwned(pCritterEnt, pPlayerEnt)) {
		PERFINFO_AUTO_STOP();
		return true;
	} else if (interaction_IsLootEntity(pCritterEnt)) {
		// Someone else's loot
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (!entIsAlive(pCritterEnt)) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	if (pCritterEnt->pCritter->encounterData.iPlayerOwnerID) {
		Entity *pOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pCritterEnt->pCritter->encounterData.iPlayerOwnerID);
		if (pOwnerEnt && pOwnerEnt->pPlayer && pOwnerEnt->pPlayer->InteractStatus.bInteracting && (pOwnerEnt != pPlayerEnt)) {
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	// If player has a sync dialog open, block all interactions except the current dialog
	if (pPlayerEnt && team_IsMember(pPlayerEnt) && mapState_GetSyncDialogForTeam(mapState_FromPartitionIdx(iPartitionIdx), team_GetTeamID(pPlayerEnt)) && !interaction_IsPlayerInDialog(pPlayerEnt)) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	// See if any Property Entries are valid
	if (pCritterEnt->pCritter) {
		int iNumEntries = 0;
		if(pCritterEnt->pCritter->encounterData.pGameEncounter) {
			iNumEntries = interaction_GetNumActorAndCritterEntries(pCritterEnt->pCritter->encounterData.pGameEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, pCritterEnt->pCritter);
		} else {
			iNumEntries = critter_GetNumInteractionEntries(pCritterEnt->pCritter);
		}

		// Check if entity is on cooldown
		for(i=0; i<iNumEntries; ++i) {
			if (interaction_IsInteractTargetBusy2(iPartitionIdx, pPlayerEnt, entGetRef(pCritterEnt), NULL, NULL, i)) {
				PERFINFO_AUTO_STOP();
				return false;
			}
		}

		// Check option
		if(iIndex >= 0 && iIndex < iNumEntries) {
			WorldInteractionPropertyEntry *pEntry = NULL;
			bool bResult;
			if(pCritterEnt->pCritter->encounterData.pGameEncounter) {
				pEntry = interaction_GetActorOrCritterEntry(pCritterEnt->pCritter->encounterData.pGameEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, pCritterEnt->pCritter, iIndex);
			} else {
				pEntry = critter_GetInteractionEntry(pCritterEnt->pCritter, iIndex);
			}
			bResult = interaction_AddOptionToInteract(iPartitionIdx, NULL, pEntry, NULL, NULL, pCritterEnt, NULL, iIndex, pPlayerEnt, false);
			PERFINFO_AUTO_STOP();
			return bResult;

		} else if (gConf.bAllowOldEncounterData) {
			PlayerDebug* pDebug = entGetPlayerDebug(pPlayerEnt, false);
			OldActorInfo *pInfo = NULL;
			Expression *pExpr = NULL;
			ContactDef *pContactDef = NULL;
			CritterDef *pCritterDef = NULL;

			// Check if is on cooldown
			if (interaction_IsInteractTargetBusy2(iPartitionIdx, pPlayerEnt, entGetRef(pCritterEnt), NULL, NULL, 0)) {
				PERFINFO_AUTO_STOP();
				return false;
			}

			// Does the critter have an interaction condition?
			pCritterDef = GET_REF(pCritterEnt->pCritter->critterDef);
			if( pCritterEnt->pCritter->encounterData.sourceActor ) {
				pInfo = oldencounter_GetActorInfo(pCritterEnt->pCritter->encounterData.sourceActor);
			}
			if( pInfo && pInfo->oldActorInteractProps.interactCond ) {
				pExpr = pInfo->oldActorInteractProps.interactCond;
			} else if( pCritterDef && pCritterDef->oldInteractProps.interactCond ) {
				pExpr = pCritterDef->oldInteractProps.interactCond;
			}
			if ((!pDebug || !pDebug->allowAllInteractions) && pExpr && !interaction_EvaluateCritterInteractCond(pExpr, pPlayerEnt, pCritterEnt)) {
				PERFINFO_AUTO_STOP();
				return false;
			}

			// Add contact option as applicable
			if (GET_REF(pCritterEnt->pCritter->encounterData.hContactDefOverride)) {
				pContactDef = GET_REF(pCritterEnt->pCritter->encounterData.hContactDefOverride);
			} else if (pCritterEnt->pCritter->encounterData.sourceActor && 
				pCritterEnt->pCritter->encounterData.sourceActor->details.info &&
				GET_REF(pCritterEnt->pCritter->encounterData.sourceActor->details.info->contactScript)) {
					pContactDef = GET_REF(pCritterEnt->pCritter->encounterData.sourceActor->details.info->contactScript);
			}
			if (pContactDef) {
				bool bResult;
				
				bResult = (pDebug && pDebug->allowAllInteractions) || contact_CanInteract(pContactDef, pPlayerEnt);

				PERFINFO_AUTO_STOP();
				return bResult;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}


void interaction_ProcessInteraction(Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry)
{
	WorldActionInteractionProperties *pActionProps = interaction_GetActionProperties(pEntry);
	WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);
	WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
	InteractTarget *pTarget = NULL;

	if (!pPlayerEnt || !pPlayerEnt->pPlayer || !pEntry) {
		return;
	}
	pTarget = &pPlayerEnt->pPlayer->InteractStatus.interactTarget;

	// play success sound
	if (pSoundProps && pSoundProps->pchSuccessSound) {
		if (pInteractable) {
			Vec3 vPos = {0,0,0};
			interactable_GetPosition(pInteractable, vPos);
			mechanics_playOneShotSoundAtLocation(entGetPartitionIdx(pPlayerEnt), vPos, NULL, pSoundProps->pchSuccessSound, NULL);
		} else if (pEntTarget) {
			mechanics_playOneShotSoundFromEntity(entGetPartitionIdx(pPlayerEnt), pEntTarget, pSoundProps->pchSuccessSound, NULL);
		}
	}

	// run all actions; all other behavior will happen in the callback
	if (pPlayerEnt->pPlayer && pActionProps && eaSize(&pActionProps->successActions.eaActions)) {
		InteractionTransactionData *pData = calloc(1, sizeof(InteractionTransactionData));
		ItemChangeReason reason = {0};

		pData->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		pData->pcInteractableName = SAFE_MEMBER(pInteractable,pcName);
		pData->pcVolumeName = SAFE_MEMBER(pVolume, pcName);
		pData->targetEntRef = SAFE_MEMBER(pEntTarget, myRef);
		pData->iIndex = pTarget->iInteractionIndex;
		pData->eTeammateType = pTarget->eTeammateType;
		pData->uTeammateID = pTarget->uTeammateID;

		inv_FillItemChangeReason(&reason, pPlayerEnt, "Interact:SuccessGameAction", SAFE_MEMBER(pInteractable,pcName));

		gameaction_RunActions(pPlayerEnt, &pActionProps->successActions, &reason, interaction_InteractPostGameActionCB, pData);

	} else {
		// no game actions; just finish the interaction
		interaction_ContinueInteraction(pInteractable, pEntTarget, pVolume, pTarget->iInteractionIndex, pTarget->eTeammateType, pTarget->uTeammateID, pPlayerEnt);
	}
}


// Extract portion of interactionEval that involves sending failure message, anims, etc.
// Currently shared by EvalInteract and EvalInteractAttemptable.
//   pInteractable, pVolume and pEntTarget are only needed for their names for the failure resolution and for recording the failure.
//   pContactDef is only needed to record the failure
static void interaction_FailInteractionPropertyEntry(SA_PARAM_NN_VALID Entity *pPlayerEnt, WorldInteractionPropertyEntry *pEntry,
													 GameInteractable* pInteractable, GameNamedVolume *pVolume, Entity *pEntTarget,
													 ContactDef *pContactDef)
{
	PlayerDebug *pDebug;
	int iPartitionIdx;
	InteractTarget *pTarget;
	Player *pPlayer = pPlayerEnt->pPlayer;

	pDebug = entGetPlayerDebug(pPlayerEnt, false);
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	pTarget = &pPlayer->InteractStatus.interactTarget;
	
	// Interaction failed. Take appropriate steps
	if (pEntry)
	{
		char *estrBuffer = NULL;
		
		WorldActionInteractionProperties *pActionProps = interaction_GetActionProperties(pEntry);
		WorldTextInteractionProperties *pTextProps = interaction_GetTextProperties(pEntry);
		WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);
		const char *pcWindowTitle = langTranslateMessageKey(entGetLanguage(pPlayerEnt), "MechanicsUI.InteractFailed");
		const char *pcText = NULL;
			
		// Send an error message
		if (pPlayerEnt && pTextProps && GET_REF(pTextProps->failureConsoleText.hMessage)) {
			Entity *pMapOwner = partition_GetPlayerMapOwner(iPartitionIdx);
			WorldVariable **eaMapVars = NULL;
			estrStackCreate(&estrBuffer);
			mapState_GetAllPublicVars(mapState_FromPartitionIdx(iPartitionIdx),&eaMapVars);
			langFormatGameDisplayMessage(entGetLanguage(pPlayerEnt), &estrBuffer, &pTextProps->failureConsoleText, STRFMT_ENTITY_KEY("MapOwner", pMapOwner), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
			if (estrBuffer && estrBuffer[0]){
				pcText = estrBuffer;
			}
			eaDestroy(&eaMapVars);
		}
		if (!pcText) {
			if (interaction_GetEffectiveClass(pEntry) == pcPooled_Door) {
				pcText = langTranslateMessageKey(entGetLanguage(pPlayerEnt), "MechanicsUI.DoorLocked");
			} else {
				pcText = langTranslateMessageKey(entGetLanguage(pPlayerEnt), "MechanicsUI.ClickableFailed");
			}
		}
		if (pcText) {
			notify_NotifySend(pPlayerEnt, kNotifyType_InteractionFailed, pcText, NULL, NULL);
		}

		// run all failure actions
		if (pPlayerEnt && pPlayerEnt->pPlayer && pActionProps && eaSize(&pActionProps->failureActions.eaActions)) {
			InteractionTransactionData *pData = calloc(1, sizeof(InteractionTransactionData));
			ItemChangeReason reason = {0};

			pData->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
			pData->pcInteractableName = SAFE_MEMBER(pInteractable, pcName);
			pData->pcVolumeName = SAFE_MEMBER(pVolume, pcName);
			pData->targetEntRef = SAFE_MEMBER(pEntTarget, myRef);
			pData->iIndex = pTarget->iInteractionIndex;
			pData->eTeammateType = pTarget->eTeammateType;
			pData->uTeammateID = pTarget->uTeammateID;

			inv_FillItemChangeReason(&reason, pPlayerEnt, "Interact:FailureGameAction", SAFE_MEMBER(pInteractable, pcName));

			gameaction_RunActions(pPlayerEnt, &pActionProps->failureActions, &reason, NULL, pData);
		}

		// Perform failure action
		if (pActionProps && pActionProps->pFailureExpr) {
			if (pInteractable) {
				interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pActionProps->pFailureExpr);
			} else if (pEntTarget) {
				encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pEntTarget, pActionProps->pFailureExpr, NULL);
			} else if (pVolume) {
				volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pActionProps->pFailureExpr);
			}
		}

		// Log failure event
		eventsend_RecordInteractFailure(pPlayerEnt, pEntTarget, pContactDef, pInteractable, pVolume);

		// Play failure sound
		if (pSoundProps && pSoundProps->pchFailureSound) {
			if(pInteractable || pVolume) {
				Vec3 vPos;
				if (pInteractable) {
					interactable_GetPosition(pInteractable, vPos);
				} else {
					volume_GetCenterPosition(iPartitionIdx, pTarget->pcVolumeNamePooled, NULL, vPos);
				}
				mechanics_playOneShotSoundAtLocation(iPartitionIdx, vPos, NULL, pSoundProps->pchFailureSound, NULL);
			} else if(pEntTarget) {
				mechanics_playOneShotSoundFromEntity(iPartitionIdx, pEntTarget, pSoundProps->pchFailureSound, NULL);
			}
		}

		// End the interaction
		interaction_DoneInteracting(iPartitionIdx, pPlayerEnt, false, false);
		estrDestroy(&estrBuffer);
	}
}



// Test the attemptable condition on an interact (if appropriate) and immediately fail if we should.
// For now we will send the same error message we would from a SuccessCond failure.
static bool interaction_EvalInteractAttemptable(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	Player *pPlayer = pPlayerEnt->pPlayer;
	InteractTarget *pTarget;
	GameInteractable *pInteractable = NULL;
	GameNamedVolume *pVolume = NULL;
	Entity *pEntTarget = NULL;
	WorldInteractionPropertyEntry *pEntry = NULL;
	ContactDef *pContactDef = NULL;
	PlayerDebug *pDebug;
	int iPartitionIdx;

	// This is mostly duplicate code from EvalInteract. Mostly it's just initialization stuff.
	// The big goal for this function is to get hold of a pEntry.

	pDebug = entGetPlayerDebug(pPlayerEnt, false);
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	pTarget = &pPlayer->InteractStatus.interactTarget;

	if (GET_REF(pTarget->hInteractionNode)) {
		pInteractable = interactable_GetByNode(GET_REF(pTarget->hInteractionNode));
		if (pInteractable) {
			pEntry = interactable_GetPropertyEntryForPlayer(pPlayerEnt, pInteractable, pTarget->iInteractionIndex);
			pContactDef = interaction_GetContactDef(pEntry);
		}
	} else if (pTarget->entRef) {
		pEntTarget = entFromEntityRef(iPartitionIdx, pTarget->entRef);
		if (pEntTarget && pEntTarget->pCritter) {
			if (pEntTarget->pCritter->encounterData.pGameEncounter && !pEntry) {
				pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, pTarget->iInteractionIndex);
			} else if(critter_GetNumInteractionEntries(pEntTarget->pCritter) > 0 && !pEntry) {
				pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, pTarget->iInteractionIndex);
			}
			// No pEntry for other cases. The real EvalInteract has more code here.
		}
	} else if (pTarget->pcVolumeNamePooled) {
		pVolume = volume_GetByName(pTarget->pcVolumeNamePooled, NULL);
		if (pVolume) {
			pEntry = volume_GetInteractionPropEntry(pVolume, pTarget->iInteractionIndex);
		}
	}

	if (pEntTarget && interaction_IsLootEntityOwned(pEntTarget, pPlayerEnt))
	{
		// Loot is always attemptable
	}
	else if (pEntry)
	{
		Expression *pAttemptableCond = interaction_GetAttemptableCond(pEntry);

		// Check the Attemptable Condition
		if ((pDebug && pDebug->allowAllInteractions) || !pAttemptableCond || 
			(pInteractable && interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pAttemptableCond)) ||
			(pEntTarget && encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pEntTarget, pAttemptableCond, NULL)) ||
			(pVolume && volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pAttemptableCond))
			)
		{
			// Attemptable. 
		}
		else
		{
			// We failed. Immediately do the same thing that failing SuccessCond would do.
			interaction_FailInteractionPropertyEntry(pPlayerEnt, pEntry, pInteractable, pVolume, pEntTarget, pContactDef);
			return(false);
		}
	}
	return(true);
}

void interaction_EvalInteract(SA_PARAM_NN_VALID Entity *pPlayerEnt)
{
	Player *pPlayer = pPlayerEnt->pPlayer;
	InteractTarget *pTarget;
	GameInteractable *pInteractable = NULL;
	GameNamedVolume *pVolume = NULL;
	Entity *pEntTarget = NULL;
	WorldInteractionPropertyEntry *pEntry = NULL;
	ContactDef *pContactDef = NULL;
	const OldInteractionProperties *pIntProps = NULL;
	PlayerDebug *pDebug;
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	pDebug = entGetPlayerDebug(pPlayerEnt, false);
	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	pTarget = &pPlayer->InteractStatus.interactTarget;

	if (GET_REF(pTarget->hInteractionNode)) {
		pInteractable = interactable_GetByNode(GET_REF(pTarget->hInteractionNode));
		if (pInteractable) {
			pEntry = interactable_GetPropertyEntryForPlayer(pPlayerEnt, pInteractable, pTarget->iInteractionIndex);
			pContactDef = interaction_GetContactDef(pEntry);
		}
	} else if (pTarget->entRef) {
		pEntTarget = entFromEntityRef(iPartitionIdx, pTarget->entRef);
		if (pEntTarget && pEntTarget->pCritter) {
			if (pEntTarget->pCritter->encounterData.pGameEncounter && !pEntry) {
				pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, pTarget->iInteractionIndex);
			} else if(critter_GetNumInteractionEntries(pEntTarget->pCritter) > 0 && !pEntry) {
				pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, pTarget->iInteractionIndex);
			} else if (GET_REF(pEntTarget->pCritter->encounterData.hContactDefOverride) && gConf.bAllowOldEncounterData) {
				pContactDef = GET_REF(pEntTarget->pCritter->encounterData.hContactDefOverride);
			} else if (gConf.bAllowOldEncounterData) {
				pIntProps = interaction_GetPropsFromInteractTarget(pTarget, pPlayerEnt);
			}
		}
	} else if (pTarget->pcVolumeNamePooled) {
		pVolume = volume_GetByName(pTarget->pcVolumeNamePooled, NULL);
		if (pVolume) {
			pEntry = volume_GetInteractionPropEntry(pVolume, pTarget->iInteractionIndex);
		}
	}

	if 	(pEntTarget && interaction_IsLootEntityOwned(pEntTarget, pPlayerEnt)) {
		interactloot_PerformLootEntInteract(pPlayerEnt, pEntTarget);

		// Don't mark interact is done when doing loot interact.  That happens later.
		// Don't send successful interact for loot interacts.

	} else if (pEntry) {
		Expression *pSuccessCond = interaction_GetSuccessCond(pEntry);

		// Check the Success Condition
		if ((pDebug && pDebug->allowAllInteractions) || !pSuccessCond || 
			(pInteractable && interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pSuccessCond)) ||
			(pEntTarget && encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pEntTarget, pSuccessCond, NULL)) ||
			(pVolume && volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pSuccessCond))
			)
		{
			char *estrBuffer = NULL;
			
			WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
			WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);
			// Interaction succeeded

			// Pass the interaction through the motion manager first
			if (pMotionProps && !pMotionProps->bTransDuringUse && !pGateProps) {
				im_Interact(iPartitionIdx, pInteractable, pEntTarget, pVolume, pEntry, pTarget->iInteractionIndex, pPlayerEnt);
			} else {
				// ...unless motion is supposed to happen simultaneously with the use time, in which case motion should not occur
				interaction_ProcessInteraction(pPlayerEnt, pInteractable, pEntTarget, pVolume, pEntry);
			}
			
			estrDestroy(&estrBuffer);
			
		} else {
			// Interaction failed

			interaction_FailInteractionPropertyEntry(pPlayerEnt, pEntry, pInteractable, pVolume, pEntTarget, pContactDef);
		}
		
	} else if (pIntProps) {
		ExprContext *pContext = interaction_GetCritterInteractContext(pPlayerEnt, pEntTarget);
		bool bResult = true;
		MultiVal mval = {0};

		// Check whether interaction succeeded
		if ((!pDebug || !pDebug->allowAllInteractions) && pIntProps->interactSuccessCond) {
			exprEvaluate(pIntProps->interactSuccessCond,pContext,&mval);
			bResult = MultiValGetInt(&mval, NULL);
		}

		if (bResult) {
			if (pIntProps->interactGameActions && eaSize(&pIntProps->interactGameActions->eaActions)) {
				ItemChangeReason reason = {0};
				inv_FillItemChangeReason(&reason, pPlayerEnt, "Interact:InteractGameAction", SAFE_MEMBER(pInteractable,pcName));
				gameaction_RunActions(pPlayerEnt, pIntProps->interactGameActions, &reason, interaction_ExecuteCritterInteractionCB, U32_TO_PTR(entGetRef(pEntTarget)));
			} else {
				// No actions to run; execute the interaction immediately
				interaction_ExecuteCritterInteraction(pPlayerEnt, pEntTarget);
			}
		} else {
			interaction_DoneInteracting(iPartitionIdx, pPlayerEnt, false, false);
			eventsend_RecordInteractFailure(pPlayerEnt, pEntTarget, NULL, NULL, NULL);
		}

	} else if (pContactDef) {
		// Simply a contact
		contact_InteractBegin(pPlayerEnt, pEntTarget, pContactDef, NULL, NULL);

		// Finish the interact
		interaction_DoneInteracting(iPartitionIdx, pPlayerEnt, false, false);

	} else {
		// No properties so simply end the interaction
		interaction_DoneInteracting(iPartitionIdx, pPlayerEnt, false, false);
	}

	PERFINFO_AUTO_STOP();
}


void interaction_SetInteractTarget(Entity *pPlayerEnt, WorldInteractionNode *pNodeTarget, Entity *pEntTarget, GameNamedVolume *pVolume, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID)
{
	Player *pPlayer = SAFE_MEMBER(pPlayerEnt, pPlayer);
	if (!pPlayer)
		return;

	REMOVE_HANDLE(pPlayer->InteractStatus.interactTarget.hInteractionNode);
	pPlayer->InteractStatus.interactTarget.entRef = 0;
	pPlayer->InteractStatus.interactTarget.iInteractionIndex = iIndex;
	pPlayer->InteractStatus.interactTarget.eTeammateType = eTeammateType;
	pPlayer->InteractStatus.interactTarget.uTeammateID = uTeammateID;
	pPlayer->InteractStatus.interactTarget.bLoot = false;
	zeroVec3(pPlayer->InteractStatus.interactTarget.vNodeNearPoint);

	pPlayer->InteractStatus.bInteracting = true;
	entity_SetDirtyBit(pPlayerEnt, parse_EntInteractStatus, &pPlayer->InteractStatus, true);
	entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayer, false);

	if (pNodeTarget) {
		SET_HANDLE_FROM_REFERENT("InteractionNode", pNodeTarget, pPlayer->InteractStatus.interactTarget.hInteractionNode);
	} else if (pEntTarget) {
		pPlayer->InteractStatus.interactTarget.entRef = entGetRef(pEntTarget);
	} else if (pVolume) {
		pPlayer->InteractStatus.interactTarget.pcVolumeNamePooled = pVolume->pcName;
	}
}


void interaction_FinishPathing(Entity *pPlayerEnt, GameInteractable *pInteractable, Entity *pTargetEnt, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID)
{
	Player *pPlayer = pPlayerEnt->pPlayer;
	WorldAnimationInteractionProperties *pAnimProps = interaction_GetAnimationProperties(pEntry);
	WorldTimeInteractionProperties *pTimeProps = interaction_GetTimeProperties(pEntry);
	WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
	WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);
	WorldActionInteractionProperties *pActionProps = interaction_GetActionProperties(pEntry);
	WorldChairInteractionProperties *pChairProps = interaction_GetChairProperties(pEntry);
	AIAnimList *pAnimList = pAnimProps ? GET_REF(pAnimProps->hInteractAnim) : NULL;
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	// If this target has become busy by the time pathing has finished, do not begin interaction
	// TODO (JDJ): uncomment below once mid-path messages are implemented by the movement requester
/*	
	WorldInteractionNode *pNode = pInteractable ? interactable_GetWorldInteractionNode(pInteractable) : NULL;
	if ((pInteractable && !interaction_CanEntityInteractWithNode(pPlayerEnt, pNode, iIndex, eTeammateType, uTeammateID)) ||
		(pTargetEnt && !interaction_CanEntityInteractWithEnt(pPlayerEnt, pTargetEnt, iIndex, eTeammateType, uTeammateID)))
	{
		interaction_DoneInteracting(pPlayerEnt, false);
		return;
	}

	// Set interaction target on player
	interaction_SetInteractTarget(pPlayerEnt, pNode, pTargetEnt, NULL, iIndex, eTeammateType, uTeammateID);

	// If this is an exclusive interaction, mark this interaction as "owned"
	if (interaction_IsExclusive(pEntry, pInteractable, pTargetEnt))
	{
		if (pInteractable)
			pInteractable->iPlayerOwnerID = pPlayerEnt->myContainerID;
		else if (pTargetEnt && pTargetEnt->pCritter && !interaction_IsLootEntity(pTargetEnt, pPlayerEnt))
			pTargetEnt->pCritter->encounterData.iPlayerOwnerID = pPlayerEnt->myContainerID;
	}
*/

	// Set up animation
	if (pPlayer->InteractStatus.pEndInteractCommandQueue) {
		CommandQueue_ExecuteAllCommands(pPlayer->InteractStatus.pEndInteractCommandQueue);
	}
	if (pAnimList) {
		if(gConf.bNewAnimationSystem){
			MRInteractionPath*		p = StructAlloc(parse_MRInteractionPath);
			MRInteractionWaypoint*	wp = StructAlloc(parse_MRInteractionWaypoint);
			Vec3					posTarget = {0};
			S32						hasPosTarget = 0;

			if(pInteractable){
				hasPosTarget = interactable_GetPosition(pInteractable, posTarget);
			}
			else if(pVolume){
				hasPosTarget = volume_GetCenterPosition(iPartitionIdx, pVolume->pcName, NULL, posTarget);
			}

			entGetPos(pPlayerEnt, wp->pos);

			if(hasPosTarget){
				Vec3 vecToTarget;
				Vec3 pyrFace = {0};

				subVec3(posTarget, wp->pos, vecToTarget);
				vecToTarget[1] = 0;
				getVec3YP(vecToTarget, &pyrFace[1], NULL);
				PYRToQuat(pyrFace, wp->rot);
			}else{
				Vec3 pyrFace = {0};

				entGetFacePY(pPlayerEnt, pyrFace);
				pyrFace[0] = 0.f;
				PYRToQuat(pyrFace, wp->rot);
			}

			wp->animToStart = mmGetAnimBitHandleByName(pAnimList->animKeyword, 0);
			wp->flags.releaseOnInput = 1;
			eaPush(&p->wps, wp);

			mrInteractionDestroy(&pPlayerEnt->mm.mrInteraction);

			mrInteractionCreate(pPlayerEnt->mm.movement,
								&pPlayerEnt->mm.mrInteraction);

			if(!mrInteractionSetPath(pPlayerEnt->mm.mrInteraction, &p)){
				StructDestroySafe(parse_MRInteractionPath, &p);
			}
		}else{
			aiAnimListSet(pPlayerEnt, pAnimList, &pPlayer->InteractStatus.pEndInteractCommandQueue);
		}
	}

	// Perform the interact attempt action
	if (pActionProps && pActionProps->pAttemptExpr) {
		if (pInteractable) {
			interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pActionProps->pAttemptExpr);
		} else if (pTargetEnt) {
			encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pTargetEnt, pActionProps->pAttemptExpr, NULL);
		} else if (pVolume) {
			volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pActionProps->pAttemptExpr);
		}
	}

	// Play interact attempt sound
	if (pSoundProps && EMPTY_TO_NULL(pSoundProps->pchAttemptSound)) {
		if (pInteractable || pVolume) {
			Vec3 vPos;
			if (pInteractable) {
				interactable_GetPosition(pInteractable, vPos);
			} else {
				volume_GetCenterPosition(iPartitionIdx, pVolume->pcName, NULL, vPos);
			}
			mechanics_playOneShotSoundAtLocation(iPartitionIdx, vPos, NULL, pSoundProps->pchAttemptSound, NULL);
		} else if (pTargetEnt) {
			mechanics_playOneShotSoundFromEntity(iPartitionIdx, pTargetEnt, pSoundProps->pchAttemptSound, NULL);
		}
	}

	// Check if we are attemptable. If not, we immediately fail as if SuccessCond had failed
	if (interaction_EvalInteractAttemptable(pPlayerEnt))
	{
		if (pTimeProps && (pTimeProps->fUseTime > 0)) {
			// There is a use time, so set up tracking for it
			pPlayer->InteractStatus.fTimerInteract = pTimeProps->fUseTime;
			pPlayer->InteractStatus.fTimerInteractMax = pTimeProps->fUseTime;
			pPlayer->InteractStatus.bInteractBreakOnDamage = pTimeProps->bInterruptOnDamage;
			pPlayer->InteractStatus.bInteractBreakOnMove = pTimeProps->bInterruptOnMove;
			pPlayer->InteractStatus.bInteractBreakOnPower = pTimeProps->bInterruptOnPower;
			COPY_HANDLE(pPlayer->InteractStatus.hInteractUseTimeMsg, pTimeProps->msgUseTimeText.hMessage);
			entGetPos(pPlayerEnt, pPlayerEnt->pPlayer->InteractStatus.interactStartPos);
			entity_SetDirtyBit(pPlayerEnt, parse_EntInteractStatus, &pPlayer->InteractStatus, true);
			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayer, false);
	
			// If motion is supposed to happen simultaneously, initiate it now
			if (pMotionProps && pMotionProps->bTransDuringUse) {
				im_Interact(iPartitionIdx, pInteractable, pTargetEnt, NULL, pEntry, iIndex, pPlayerEnt);
			}
		} else {
			// Interact immediately if no use time
			interaction_EvalInteract(pPlayerEnt);
		}
	}

	if (pChairProps && pPlayer->InteractStatus.bSittingInChair) {
		pPlayer->InteractStatus.bSittingInChair = false;
		entity_SetDirtyBit(pPlayerEnt, parse_EntInteractStatus, &pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayer, false);
	}
}


void interaction_StartInteracting(Entity *pPlayerEnt, WorldInteractionNode *pNodeTarget, Entity *pEntTarget, GameNamedVolume *pVolume, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID, bool bBeginPathing)
{
	Player *pPlayer = pPlayerEnt->pPlayer;
	GameInteractable *pInteractable = NULL;
	WorldInteractionPropertyEntry *pEntry = NULL;
	ContactDef *pContactDef = NULL;
	const OldInteractionProperties *pIntProps = NULL;
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	
	// Set up additional properties by type and gather info
	if (pNodeTarget) {
		// vNodeNearPoint will be populated later if needed
		
		pInteractable = interactable_GetByNode(pNodeTarget);
		if (pInteractable) {
			if (g_bEnableInteractionDebugLog) {
	            estrConcatf(&pInteractable->estrDebugLog,"player %s starting interaction;\n", pPlayerEnt->debugName);
			}
 			interactable_Activate(pPlayerEnt, pInteractable, iIndex);
		}
		pEntry = interactable_GetPropertyEntryForPlayer(pPlayerEnt, pInteractable, iIndex);

	} else if (pEntTarget && pEntTarget->pCritter) {
		if (pEntTarget->pCritter->encounterData.pGameEncounter) {
			pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, iIndex);
			if (pEntry) {
				pContactDef = interaction_GetContactDef(pEntry);
			}
		} else if(critter_GetNumInteractionEntries(pEntTarget->pCritter) > 0) {
			pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, iIndex);
			if (pEntry) {
				pContactDef = interaction_GetContactDef(pEntry);
			}
		} else if (GET_REF(pEntTarget->pCritter->encounterData.hContactDefOverride) && gConf.bAllowOldEncounterData) {
			pContactDef = GET_REF(pEntTarget->pCritter->encounterData.hContactDefOverride);
		} else if (gConf.bAllowOldEncounterData) {
			interaction_SetInteractTarget(pPlayerEnt, pNodeTarget, pEntTarget, NULL, iIndex, eTeammateType, uTeammateID);
			pIntProps = interaction_GetPropsFromInteractTarget(&pPlayer->InteractStatus.interactTarget, pPlayerEnt);
			pContactDef = interaction_GetCritterContactDef(pEntTarget->pCritter);
		}

	} else if (pVolume) {
		pEntry = volume_GetInteractionPropEntry(pVolume, iIndex);
	}

	if (!pEntry || !pEntry->bDisablePowersInterrupt) {
		character_ActInterrupt(iPartitionIdx,pPlayerEnt->pChar,kPowerInterruption_Interact);
		if (pContactDef)
		{
			character_ActInterrupt(iPartitionIdx,pPlayerEnt->pChar,kPowerInterruption_ContactInteract);
		}
		else
		{
			character_ActInterrupt(iPartitionIdx,pPlayerEnt->pChar,kPowerInterruption_NonContactInteract);
		}
	}
	gslLogoff_Cancel(pPlayerEnt, kLogoffCancel_Interact);
	character_CombatEventTrack(pPlayerEnt->pChar,kCombatEvent_InteractStart);

	if (pEntTarget && interaction_IsLootEntityOwned(pEntTarget, pPlayerEnt)) {
		// Interact immediately for loot
		interaction_SetInteractTarget(pPlayerEnt, pNodeTarget, pEntTarget, NULL, iIndex, eTeammateType, uTeammateID);
		interaction_EvalInteract(pPlayerEnt);

		// Clear the auto loot interact flag after initial interaction
		if (pEntTarget->pCritter) {
			pEntTarget->pCritter->bAutoInteract = false;
		}
		return;
	}

	// Send Events for start of interact
	eventsend_RecordInteractBegin(pPlayerEnt, pEntTarget, pContactDef, pInteractable, pVolume);

	if (pEntry) {
		pPlayer->InteractStatus.bInteracting = true;
		entity_SetDirtyBit(pPlayerEnt, parse_EntInteractStatus, &pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayer, false);

		// Set interaction target on player
		// TODO (JDJ): probably need to set this mid-path, after pathing to foot location (once msimpson implements mid-path messages/callbacks);
		// TODO (JDJ): also need to move the exclusivity back to the interaction_FinishPathing function
		interaction_SetInteractTarget(pPlayerEnt, pNodeTarget, pEntTarget, pVolume, iIndex, eTeammateType, uTeammateID);

		// If this is an exclusive interaction, mark this interaction as "owned"
		if (interaction_IsExclusive(pEntry, pInteractable, pEntTarget)) {
			if (pInteractable) {
				interactable_SetPlayerOwner(iPartitionIdx, pInteractable, pPlayerEnt->myContainerID);
			} else if (pEntTarget && pEntTarget->pCritter && !interaction_IsLootEntity(pEntTarget)) {
				pEntTarget->pCritter->encounterData.iPlayerOwnerID = pPlayerEnt->myContainerID;
			}
		}
		// TODO END

		// Add the player to the list of interacting players
		if (pEntTarget && pEntTarget->pCritter) {
			ea32PushUnique(&pEntTarget->pCritter->encounterData.perInteractingPlayers, pPlayerEnt->myContainerID);
		}

		if (bBeginPathing)
		{
			if (!im_InteractBeginPathing(pPlayerEnt, pInteractable, pEntTarget, pEntry, iIndex, eTeammateType, uTeammateID)) {
				interaction_FinishPathing(pPlayerEnt, pInteractable, pEntTarget, pVolume, pEntry, iIndex, eTeammateType, uTeammateID);
			} else if (interaction_GetChairProperties(pEntry)) {
				pPlayer->InteractStatus.bSittingInChair = true;
			}
		}

	} else if (pIntProps) {
		// This is old encounter system interaction
		AIAnimList *pAnimList = GET_REF(pIntProps->hInteractAnim);

		// Set up animation
		if (pPlayer->InteractStatus.pEndInteractCommandQueue){
			CommandQueue_ExecuteAllCommands(pPlayer->InteractStatus.pEndInteractCommandQueue);
		}
		if (pAnimList){
			aiAnimListSet(pPlayerEnt, pAnimList, &pPlayer->InteractStatus.pEndInteractCommandQueue);
		}

		if(pIntProps->uInteractTime > 0) {
			// This will take some time; set up time values
			pPlayer->InteractStatus.fTimerInteract = pIntProps->uInteractTime;
			pPlayer->InteractStatus.bInteractBreakOnDamage = (pIntProps->eInteractType & InteractType_BreakOnDamage)!=0;
			pPlayer->InteractStatus.bInteractBreakOnMove = (pIntProps->eInteractType & InteractType_BreakOnMove)!=0;
			pPlayer->InteractStatus.bInteractBreakOnPower = (pIntProps->eInteractType & InteractType_BreakOnPower)!=0;
			pPlayer->InteractStatus.fTimerInteractMax = pIntProps->uInteractTime;
			entGetPos(pPlayerEnt, pPlayerEnt->pPlayer->InteractStatus.interactStartPos);
		} else {
			// Interact immediately if no use time
			interaction_EvalInteract(pPlayerEnt);
		}
	} else if (pContactDef) {
		// Contact override
		interaction_SetInteractTarget(pPlayerEnt, pNodeTarget, pEntTarget, pVolume, iIndex, eTeammateType, uTeammateID);
		interaction_EvalInteract(pPlayerEnt);
	} else {
		interaction_SetInteractTarget(pPlayerEnt, pNodeTarget, pEntTarget, pVolume, iIndex, eTeammateType, uTeammateID);
		interaction_DoneInteracting(iPartitionIdx, pPlayerEnt, false, false);
	}
}


#define TIME_BETWEEN_INTERACTS 0.75f

bool interaction_PerformInteract(Entity *pPlayerEnt, EntityRef entRef, const char *pchNodeKey, const char *pcVolumeName, int iIndex, int eTeammateType, U32 uTeammateID, bool bForced)
{
	MapState *state;

	if (!pPlayerEnt || !pPlayerEnt->pPlayer) {
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	state = mapState_FromEnt(pPlayerEnt);

	// Can't interact if dead
	if (entCheckFlag(pPlayerEnt, ENTITYFLAG_DEAD)) {
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player interact DENIED. (entref %d node %s volume %s reason \"Player is dead\")", entRef, pchNodeKey, pcVolumeName);
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	//Can't interact if not quite dead
	if (pPlayerEnt->pChar && pPlayerEnt->pChar->pNearDeath) {
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player interact DENIED. (entref %d node %s volume %s reason \"Player is nearly dead\")", entRef, pchNodeKey, pcVolumeName);
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Can't interact if already interacting
	if (pPlayerEnt->pPlayer->InteractStatus.bInteracting) {
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player interact DENIED. (entref %d node %s volume %s reason\"Player is already interacting\")", entRef, pchNodeKey, pcVolumeName);
		}
		PERFINFO_AUTO_STOP();
		return false;
	}
	
	// Can't interact if interacted already in TIME_BETWEEN_INTERACTS time (unless forced)
	if (!bForced && (pPlayerEnt->pPlayer->InteractStatus.fTimeUntilNextInteract > 0)) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Can't interact if Held or Disabled (unless forced),
	if (!bForced && pPlayerEnt->pChar && pPlayerEnt->pChar->pattrBasic && 
		((pPlayerEnt->pChar->pattrBasic->fOnlyAffectSelf > 0) || 
		 character_IsHeld(pPlayerEnt->pChar) || 
		 (pPlayerEnt->pChar->pattrBasic->fDisable > 0))
		) {
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player interact DENIED. (entref %d node %s volume %s reason\"Player is Held or Confused\")", entRef, pchNodeKey, pcVolumeName);
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Can't interact if waiting on sync dialog
	if (team_IsMember(pPlayerEnt) && mapState_GetSyncDialogForTeam(state, team_GetTeamID(pPlayerEnt))) {
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player interact DENIED. (entref %d node %s volume %s reason\"Player is waiting on expiration of Synchronized Dialog\")", entRef, pchNodeKey, pcVolumeName);
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	pPlayerEnt->pPlayer->InteractStatus.fTimeUntilNextInteract = TIME_BETWEEN_INTERACTS;

	if (entRef) {
		InteractTarget intTarget = {0};
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		Entity *pEntTarget = entFromEntityRef(iPartitionIdx, entRef);

		if (!pEntTarget) {
			if (gbEnableGamePlayDataLogging) {
				entLog(LOG_GSL, pPlayerEnt, "Interact", "Player FAIL interact. (entref %d reason \"Target entity not found\")", entRef);
			}
			PERFINFO_AUTO_STOP();
			return false;
		}

		intTarget.entRef = entRef;
		intTarget.iInteractionIndex = iIndex;
		intTarget.bLoot = 0;

		//Distance and LoS checks
		if (!entity_VerifyInteractTarget(iPartitionIdx, pPlayerEnt, pEntTarget, NULL, 0, 0, 0, false, NULL)) {
			if (gbEnableGamePlayDataLogging) {
				entLog(LOG_GSL, pPlayerEnt, "Interact", "Player FAIL interact. (entref %d reason \"Player Failed Verify Check\")", entRef);
			}
			PERFINFO_AUTO_STOP();
			return false;
		}

		if (!interaction_CanEntityInteractWithEnt(pPlayerEnt, pEntTarget, iIndex, eTeammateType, uTeammateID)) {
			if (gbEnableGamePlayDataLogging) {
				entLog(LOG_GSL, pPlayerEnt, "Interact", "Player FAIL interact. (entref %d reason \"Entity will not allow interaction with the player\")", entRef);
			}
			PERFINFO_AUTO_STOP();
			return false;
		}

		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player PASS interact. (entref %d)", entRef);
		}
		interaction_StartInteracting(pPlayerEnt, NULL, pEntTarget, NULL, iIndex, eTeammateType, uTeammateID, true);

	} else if (pchNodeKey && pchNodeKey[0]) {
		WorldInteractionNode *pNodeTarget = RefSystem_ReferentFromString(INTERACTION_DICTIONARY, pchNodeKey);
		InteractTarget intTarget = {0};
		GameInteractable *pInteractable;

		if (!pNodeTarget) {
			if (gbEnableGamePlayDataLogging) {
				entLog(LOG_GSL, pPlayerEnt, "Interact", "Player FAIL interact. (node %s reason \"Node not found\")", pchNodeKey);
			}
			PERFINFO_AUTO_STOP();
			return false;
		}

		// Distance and LoS checks
		if (!entity_VerifyInteractTarget(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, NULL, pNodeTarget, 0, 0, 0, false, NULL)) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player FAIL interact. (node %s reason \"Player Failed Verify Check\")", pchNodeKey);
			PERFINFO_AUTO_STOP();
			return false;
		}

		pInteractable = interactable_GetByNode(pNodeTarget);
		if (!interaction_CanEntityInteractWithNode( pPlayerEnt, pInteractable, iIndex, eTeammateType, uTeammateID, bForced )) {
			if (gbEnableGamePlayDataLogging) {
				entLog(LOG_GSL, pPlayerEnt, "Interact", "Player FAIL interact. (node %s reason \"Node will not allow interaction with the player\")", pchNodeKey);
			}
			PERFINFO_AUTO_STOP();
			return false;
		}

		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player PASS interact. (node %s)", pchNodeKey);
		}
		interaction_StartInteracting(pPlayerEnt, pNodeTarget, NULL, NULL, iIndex, eTeammateType, uTeammateID, true);

	} else if (pcVolumeName && pcVolumeName[0]) {
		GameNamedVolume *pVolume = volume_GetByName(pcVolumeName, NULL); // TODO_PARTITION: 

		if (!volume_VerifyInteract(pPlayerEnt, pcVolumeName, iIndex)) {
			if (gbEnableGamePlayDataLogging) {
				entLog(LOG_GSL, pPlayerEnt, "Interact", "Player FAIL interact. (volume %s reason \"Volume will not allow interaction with the player\")", pcVolumeName);
			}
			PERFINFO_AUTO_STOP();
			return false;
		}

		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_GSL, pPlayerEnt, "Interact", "Player PASS interact. (volume %s)", pcVolumeName);
		}
		interaction_StartInteracting(pPlayerEnt, NULL, NULL, pVolume, iIndex, eTeammateType, uTeammateID, true);
	}

	PERFINFO_AUTO_STOP();

	return true;
}


// ----------------------------------------------------------------------------------
// Entity Frame Updates
// ----------------------------------------------------------------------------------


static bool interaction_FindInteractionNodesWithCheck(Entity *pEnt, U32 interactClassMask, F32 fRange, bool bTestHidden, InteractableTestCallbackEnt fTestCallback, UserData pCallbackData, GameInteractable ***peaListOut)
{
	Vec3 vEntPos;
	int i, iNumInteractables;
	F32 fCutoff;
	int iPartitionIdx = entGetPartitionIdx( pEnt );

	PERFINFO_AUTO_START_FUNC();

	eaClear(peaListOut);
	entGetPos(pEnt, vEntPos);
	vEntPos[1] += 5.0; // TODO(MM): Find chest bone

	fCutoff = interactable_GetCutoffDist(pEnt);

	interactable_QuerySphere(iPartitionIdx, interactClassMask, pEnt, vEntPos, MIN(fCutoff, fRange), false, false, bTestHidden, peaListOut);
	interactable_AddGlobalInteractables(iPartitionIdx, interactClassMask, vEntPos, fRange, fCutoff, false, false, bTestHidden, peaListOut);

	iNumInteractables = eaSize(peaListOut);
	for(i=iNumInteractables-1; i>=0; i--) {
		GameInteractable *pInteractable = (*peaListOut)[i];

		if (fTestCallback && !fTestCallback(pEnt, pInteractable, pCallbackData)) {
			eaRemoveFast(peaListOut,i);
			continue;
		}

		if (!bTestHidden) {
			if (interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable)) {
				if (!im_FindCritterforObject(iPartitionIdx, pInteractable->pcNodeName)) {
					eaRemoveFast(peaListOut,i);
					continue;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return (eaSize(peaListOut) > 0);
}


// Helper function to check whether a player meets a WorldInteractionPropertyEntry's interaction requirements
static void interaction_UpdateTargetableNodes(Entity *pEnt, U32 interactClassMask)
{
	bool bDirty = false;
	int i, n, iCount = 0, iVisCount = 0, iTooltipCount = 0;
	Player *pPlayer = pEnt->pPlayer;
	static GameInteractable **eaInteractables = NULL;

	F32 fRange = (F32)interactable_GetMaxTargetRange();

	if (!pPlayer || !pEnt->pChar) {
		return;
	}

	// Reset the dirty flag for door status node
	for (i = eaSize(&pPlayer->InteractStatus.ppDoorStatusNodes)-1; i >= 0; i--) {
		pPlayer->InteractStatus.ppDoorStatusNodes[i]->bDirty = false;
	}

	interaction_FindInteractionNodesWithCheck(pEnt, interactClassMask, fRange, true, NULL, NULL, &eaInteractables);

	n = eaSize( &eaInteractables );
	for ( i = 0; i < n; i++ ) {
		Vec3 vClose;
		F32 fDist, fTargetDist;
		GameInteractable *pInteractable = eaInteractables[i];
		WorldInteractionNode *pNode = interactable_GetWorldInteractionNode(pInteractable);
		WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);

		if ( pEntry==NULL || pEntry->full_interaction_properties->bUntargetable ) {
			continue;
		}

		fTargetDist = (F32)wlInteractionGetTargetDistForNode(pNode);

		if ( entity_IsNodeInRange( pEnt, NULL, pNode, fTargetDist, 0, 0, vClose, &fDist, true ) ) {
			TargetableNode *pTargetableNode;
			TooltipNode *pTooltipNode;
			VisibleOverrideNode *pVisibleOverrideNode;
			NodeSummary *pDoorDestStatusNode;
			bool bVisibleForThisEnt = false;
			bool bHasPerEntVis = SAFE_MEMBER(pInteractable, bVisiblePerEntity);
			S32 theAttrib = 0;
			S32 mag = 0;

			if (bHasPerEntVis) {
				//If this node has per-entity visibility, figure out if it's visible or not.
				if (interactable_EvaluateVisibilityForEntity(pEnt, pInteractable)) {
					bVisibleForThisEnt = true;

					// Find or allocate a visible override node
					if (iVisCount < eaSize(&pPlayer->InteractStatus.ppVisibleNodes)) {
						pVisibleOverrideNode = pPlayer->InteractStatus.ppVisibleNodes[iVisCount];
					} else {
						pVisibleOverrideNode = StructCreate( parse_VisibleOverrideNode );
						eaPush( &pPlayer->InteractStatus.ppVisibleNodes, pVisibleOverrideNode );
					}

					SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pVisibleOverrideNode->hNode);

					iVisCount++;
					bDirty = true;
				} else {
					continue;
				}
			}

			if ( interactable_CanEntityInteract(pEnt, pInteractable) ) {
				if (bVisibleForThisEnt || !bHasPerEntVis) {
					// We have an interactable which the client may need to know about.
					//  The client will get the list of nodes from this frame and do
					//  appropriate things like turn on/off FX on the interact.
					// entity_UpdateTargetableNodes in Entity.c handles the interactable
					//  FX. WOLF[24Aug2012]
					
					// Find or allocate a node
					if (iCount < eaSize(&pPlayer->InteractStatus.ppTargetableNodes)) {
						pTargetableNode = pPlayer->InteractStatus.ppTargetableNodes[iCount];
					} else {
						pTargetableNode = StructCreate( parse_TargetableNode );
						eaPush( &pPlayer->InteractStatus.ppTargetableNodes, pTargetableNode );
					}

					SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pTargetableNode->hNode);
					pTargetableNode->pcDetailTexture = interactable_GetDetailTexture(pInteractable);

					pTargetableNode->bIsAttemptable = interactable_CanEntityAttempt(pEnt, pInteractable);
					
					if (gConf.bNNOInteractionTooltips) {
						interactable_CheckForRelevantTooltipInfo(pInteractable, &pTargetableNode->pchRequirementName);
					}
					interactable_GetCategories(pInteractable, &pTargetableNode->eaCategories, pEnt);
					interactable_GetTags(pInteractable, &pTargetableNode->eaTags, pEnt);

					iCount++;
					bDirty = true;
				}
			} else if (wlInteractionClassMatchesMask( pNode, wlInteractionClassNameToBitMask("NamedObject")) && wlInteractionNodeGetDisplayName(pNode) && gConf.bNNOInteractionTooltips && !pInteractable->bCanBeInteractedWith) {
				if (bVisibleForThisEnt || !bHasPerEntVis) {
					// Find or allocate a node
					if (iTooltipCount < eaSize(&pPlayer->InteractStatus.ppTooltipNodes)) {
						pTooltipNode = pPlayer->InteractStatus.ppTooltipNodes[iTooltipCount];
					} else {
						pTooltipNode = StructCreate( parse_TooltipNode );
						eaPush( &pPlayer->InteractStatus.ppTooltipNodes, pTooltipNode );
					}

					SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pTooltipNode->hNode);

					iTooltipCount++;
					bDirty = true;
				}
			}

			if (im_IsNotDestructible( pNode )) {
				if (pDoorDestStatusNode = mechanics_GetNodeSummaryFromNode(pNode)) {
					// Find or allocate a node
					MapSummary** eaMapSummary = pDoorDestStatusNode->eaDestinations;
					const char* pchNodeKey = wlInteractionNodeGetKey(pNode);
					S32 iNodeIdx = eaIndexedFindUsingString(&pPlayer->InteractStatus.ppDoorStatusNodes,pchNodeKey);
					if (iNodeIdx >= 0) {
						pDoorDestStatusNode = pPlayer->InteractStatus.ppDoorStatusNodes[iNodeIdx];
					} else {
						pDoorDestStatusNode = StructCreate(parse_NodeSummary);
						SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pDoorDestStatusNode->hNode);
						eaPush(&pPlayer->InteractStatus.ppDoorStatusNodes, pDoorDestStatusNode);
					}

					eaCopyStructs(&eaMapSummary, &pDoorDestStatusNode->eaDestinations, parse_MapSummary);
					pDoorDestStatusNode->bDirty = true;
					bDirty = true;
				}
			}
		}
	}

	// Free the unused nodes
	for(i=eaSize(&pPlayer->InteractStatus.ppTargetableNodes)-1; i>=iCount; --i) {
		StructDestroy(parse_TargetableNode, pPlayer->InteractStatus.ppTargetableNodes[i]);
		eaRemove(&pPlayer->InteractStatus.ppTargetableNodes, i);
		bDirty = true;
	}

	// Free the unused visibility nodes
	for(i=eaSize(&pPlayer->InteractStatus.ppVisibleNodes)-1; i>=iVisCount; --i) {
		StructDestroy(parse_VisibleOverrideNode, pPlayer->InteractStatus.ppVisibleNodes[i]);
		eaRemove(&pPlayer->InteractStatus.ppVisibleNodes, i);
		bDirty = true;
	}

	// Free the unused tooltip nodes
	for(i=eaSize(&pPlayer->InteractStatus.ppTooltipNodes)-1; i>=iTooltipCount; --i) {
		StructDestroy(parse_TooltipNode, pPlayer->InteractStatus.ppTooltipNodes[i]);
		eaRemove(&pPlayer->InteractStatus.ppTooltipNodes, i);
		bDirty = true;
	}

	// Free the unused collect dest status nodes
	for(i=eaSize(&pPlayer->InteractStatus.ppDoorStatusNodes)-1; i>=0; --i) {
		if (!pPlayer->InteractStatus.ppDoorStatusNodes[i]->bDirty) {
			StructDestroy(parse_NodeSummary, pPlayer->InteractStatus.ppDoorStatusNodes[i]);
			eaRemove(&pPlayer->InteractStatus.ppDoorStatusNodes, i);
			bDirty = true;
		}
	}

	eaDestroy( &eaInteractables );

	if ( bDirty ) {
		entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}


// StructAlloc's a display option struct for a particular WorldInteractionPropertyEntry
static InteractOption *interaction_GetDisplayOptionForEntry(WorldInteractionPropertyEntry *pEntry, int iLanguage,
															GameInteractable *pInteractable,
															WorldInteractionNode *pNode, Entity *pCritterEnt, GameNamedVolume *pVolume, Entity *pPlayerEnt,
															Entity *pOwner, bool bCanPickup, bool bKeyed, MissionDef *pMissionDef)
{
	static char *estrBuffer = NULL;
	static char *estrBuffer2 = NULL;
	static char *estrBuffer3 = NULL;
	char* pchDifficultyOverride = NULL;

	const char *pcClass = interaction_GetEffectiveClass(pEntry);
	WorldTextInteractionProperties *pTextProps = interaction_GetTextProperties(pEntry);
	WorldDoorInteractionProperties *pDoorProps = interaction_GetDoorProperties(pEntry);
	WorldActionInteractionProperties *pActionProps = interaction_GetActionProperties(pEntry);
	const char *pcText = NULL;
	const char *pcUsabilityText = NULL;
	const char *pcDetailText = NULL;
	const char *pcDetailTexture = NULL;
	InteractOption *pOption;

	estrStackCreate(&pchDifficultyOverride);
	estrClear(&estrBuffer);
	estrClear(&estrBuffer2);
	estrClear(&estrBuffer3);

	if (pcClass == pcPooled_Destructible) {
		if (pNode && (im_IsNotDestructible( pNode ) || im_EntityCanThrowObject(pPlayerEnt, pNode, 0.0f)) ) {
			WorldDestructibleInteractionProperties *pDestructibleProps = interaction_GetDestructibleProperties(pEntry);
			if (pDestructibleProps && GET_REF(pDestructibleProps->displayNameMsg.hMessage)) {
				langFormatMessageKey( iLanguage, &estrBuffer, "MechanicsUI.PickUpObject",
					STRFMT_STRING("Name",langTranslateMessageRef(iLanguage, pDestructibleProps->displayNameMsg.hMessage)), STRFMT_END);
			} else {
				langFormatMessageKey( iLanguage, &estrBuffer, "MechanicsUI.PickUpObject",
					STRFMT_STRING("Name",langTranslateMessageKey(iLanguage,"MechanicsUI.DefaultObject")), STRFMT_END);
			}
			pOption = StructCreate(parse_InteractOption);
			pOption->bCanPickup = bCanPickup;
			assert(pOption);
			pOption->pcInteractString = StructAllocString(estrBuffer);
			
			//I don't really know why there is an early exit for this function
			// but I suppose I'll destroy this estring instead of leaking it
			estrDestroy(&pchDifficultyOverride);
			
			return pOption;
		}
	}

	// Create "Override Difficulty" text
	if(pDoorProps && pDoorProps->doorDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT &&
		pDoorProps->doorDest.pSpecificValue &&
		pDoorProps->doorDest.pSpecificValue->pcZoneMap) {

		Team *pTeam = team_GetTeam(pPlayerEnt);
		PlayerDifficultyIdx eDiffIdx = 0;
		WorldRegionType eRegion = pPlayerEnt ? entGetWorldRegionTypeOfEnt(pPlayerEnt) : WRT_None;
		if(pTeam) {
			eDiffIdx = pTeam->iDifficulty;
		} else if(pPlayerEnt && pPlayerEnt->pPlayer) {
			eDiffIdx = pPlayerEnt->pPlayer->iDifficulty;
		}

		langFormatGameMessageKey(iLanguage, &pchDifficultyOverride, "Interaction.DifficultyOverrideMessage", 
			STRFMT_STRING("Difficulty", NULL_TO_EMPTY(langTranslateMessage(iLanguage, pd_GetDifficultyNameMsg(eDiffIdx, pDoorProps->doorDest.pSpecificValue->pcZoneMap, eRegion)))), STRFMT_END);
	}

	if (pTextProps) {
		WorldVariable **eaMapVars = NULL;
		MapState *pState = pPlayerEnt ? mapState_FromEnt(pPlayerEnt) : NULL;
		if (pState) {
			mapState_GetAllPublicVars(pState, &eaMapVars);
		}
		if (GET_REF(pTextProps->usabilityOptionText.hMessage)) {
			if (pMissionDef) {
				langFormatGameMessage(iLanguage, &estrBuffer3, GET_REF(pTextProps->usabilityOptionText.hMessage), STRFMT_MISSIONDEF(pMissionDef), STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				pcUsabilityText = estrBuffer3;
			} else {
				langFormatGameMessage(iLanguage, &estrBuffer3, GET_REF(pTextProps->usabilityOptionText.hMessage), STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				pcUsabilityText = estrBuffer3;
			}
		}
		if (GET_REF(pTextProps->interactOptionText.hMessage)) {
			if (pMissionDef) {
				langFormatGameMessage(iLanguage, &estrBuffer, GET_REF(pTextProps->interactOptionText.hMessage), STRFMT_MISSIONDEF(pMissionDef), STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				pcText = estrBuffer;
			} else {
				langFormatGameMessage(iLanguage, &estrBuffer, GET_REF(pTextProps->interactOptionText.hMessage), STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				pcText = estrBuffer;
			}
		}
		
		if (GET_REF(pTextProps->interactDetailText.hMessage)) {
			if (pMissionDef) {
				langFormatGameMessage(iLanguage, &estrBuffer2, GET_REF(pTextProps->interactDetailText.hMessage), STRFMT_MISSIONDEF(pMissionDef), STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				pcDetailText = estrBuffer2;
			} else {
				langFormatGameMessage(iLanguage, &estrBuffer2, GET_REF(pTextProps->interactDetailText.hMessage), STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)), STRFMT_MAPVARS(eaMapVars), STRFMT_END);
				pcDetailText = estrBuffer2;
			}
		}
		eaDestroy(&eaMapVars);
		if (EMPTY_TO_NULL(pTextProps->interactDetailTexture)) {
			pcDetailTexture = pTextProps->interactDetailTexture;
		}
	}

	if (!pcText || !pcText[0]) {
		if ( pDoorProps &&
			    pDoorProps->doorDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT &&
			    pDoorProps->doorDest.pSpecificValue &&
			    pDoorProps->doorDest.pSpecificValue->pcStringVal && 
				!stricmp("MissionReturn", pDoorProps->doorDest.pSpecificValue->pcStringVal)) {
			pcText = langTranslateMessageKey(iLanguage, "Interaction.MissionReturnMessage");

		} else if (pDoorProps && bKeyed && pMissionDef) {
			if(pMissionDef) {
				langFormatGameMessageKey(iLanguage, &estrBuffer, "Interaction.MissionReturnKeyedDoorMessage", STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)),
					STRFMT_DISPLAYMESSAGE("Mission", pMissionDef->displayNameMsg), STRFMT_END);
			}
			if(estrBuffer && estrLength(&estrBuffer)) {
				pcText = estrBuffer;
			} else {
				pcText = langTranslateMessageKey(iLanguage, "Interaction.MissionReturnMessage");
			}

		} else if (pcClass == pcPooled_Door) {
			if(bKeyed && pDoorProps &&
				pDoorProps->doorDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT &&
				pDoorProps->doorDest.pSpecificValue &&
				pDoorProps->doorDest.pSpecificValue->pcZoneMap) {
					ZoneMapInfo* pInfo = zmapInfoGetByPublicName(pDoorProps->doorDest.pSpecificValue->pcZoneMap);
					if(pInfo) {
						DisplayMessage *pMessage = zmapInfoGetDisplayNameMessage(pInfo);
						if(pMessage && GET_REF(pMessage->hMessage)) {
							langFormatGameMessageKey(iLanguage, &estrBuffer, "Interaction.OpenKeyedDoorMessage", STRFMT_DISPLAYMESSAGE("Map", (*pMessage)), 
								STRFMT_STRING("Difficulty", NULL_TO_EMPTY(pchDifficultyOverride)), STRFMT_END);
						}
					}
					if(estrBuffer && estrLength(&estrBuffer)) {
						pcText = estrBuffer;
					} else {
						pcText = langTranslateMessageKey(iLanguage, "Interaction.OpenKeyedDoorNoMap");
					}
			} else {
				pcText = langTranslateMessageKey(iLanguage, "Interaction.OpenDoorMessage");
			}

		} else if (pCritterEnt) {
			if(pCritterEnt->pCritter->displayNameOverride) {
				entFormatGameMessageKey( pPlayerEnt, &estrBuffer, "MechanicsUI.TalkTo",
					STRFMT_STRING("Name", pCritterEnt->pCritter->displayNameOverride), STRFMT_END);
				pcText = estrBuffer;
			} else if (GET_REF(pCritterEnt->pCritter->hDisplayNameMsg)) {
				entFormatGameMessageKey( pPlayerEnt, &estrBuffer, "MechanicsUI.TalkTo",
					STRFMT_STRING("Name", entTranslateMessageRef(pPlayerEnt, pCritterEnt->pCritter->hDisplayNameMsg)), STRFMT_END);
				pcText = estrBuffer;
			} else {
				entFormatGameMessageKey( pPlayerEnt, &estrBuffer, "MechanicsUI.InteractWith",
					STRFMT_STRING("Name", entGetLangName(pCritterEnt, entGetLanguage(pPlayerEnt))), STRFMT_END);
				pcText = estrBuffer;
			}

		} else if (pVolume) {
			if (pActionProps && gameaction_GetDisplayString(pPlayerEnt, &pActionProps->successActions, &estrBuffer)) {
				pcText = estrBuffer;
			} else {
				pcText = langTranslateMessageKey(iLanguage, "Interaction.DefaultMessage");
			}

		} else {
			pcText = langTranslateMessageKey(iLanguage, "Interaction.DefaultMessage");
		}
	}

	pOption = StructCreate(parse_InteractOption);
	assert(pOption);
	if(pOwner){
		static char *estrBuffer4 = NULL;
		estrPrintf(&estrBuffer4, "%s (%s)", pcText, entGetPersistedName(pOwner));
		pOption->pcInteractString = StructAllocString(estrBuffer3);
	} else {
		pOption->pcInteractString = StructAllocString(pcText);
	}
	pOption->pcUsabilityString = StructAllocString(pcUsabilityText);
	pOption->pcDetailString = StructAllocString(pcDetailText);
	pOption->pcInteractTexture = pcDetailTexture;
	pOption->eInteractOptionType = InteractOptionType_Undefined;

	// Determine if we are attemptable. Note that this should be the same calulation that ends up on the TargetableNode if we are a Node.
	// We need to know this in the case of EntityInteractables which do not have nodes generated for them. WOLF[15Oct2012]
	{
		PlayerDebug *pDebug;
		int iPartitionIdx;
		Expression *pAttemptableCond = interaction_GetAttemptableCond(pEntry);
		pDebug = entGetPlayerDebug(pPlayerEnt, false);
		iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	
		// Check the Attemptable Condition
		if ((pDebug && pDebug->allowAllInteractions) || !pAttemptableCond || 
			(pInteractable && interactable_EvaluateExpr(iPartitionIdx, pInteractable, pPlayerEnt, pAttemptableCond)) ||
			(pCritterEnt && encounter_ExprInteractEvaluate(iPartitionIdx, pPlayerEnt, pCritterEnt, pAttemptableCond, NULL)) ||
			(pVolume && volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pAttemptableCond))
			)
		{
			// Attemptable
			pOption->bAttemptable = true;
		}
		else
		{
			pOption->bAttemptable = false;
		}
	}
	
	
	estrDestroy(&pchDifficultyOverride);
	return pOption;
}


static void interaction_AddOptionToInteractHelper(		int iPartitionIdx,
														InteractOption ***peaOptionList,
														GameInteractable *pInteractable,
														WorldInteractionNode *pNode, 
														Entity *pCritterEnt,
														GameNamedVolume *pVolume,
														WorldInteractionPropertyEntry *pEntry, 
														int iInteractionIndex, 
														Entity *pPlayerEnt, 
														Entity *pOptionOwner, 
														bool bAppendOwnerToDisplayString,
														bool bCanPickup,
														bool bOverrideEntry,
														MissionDef *pMissionDef)
{
	PERFINFO_AUTO_START_FUNC();

	if (peaOptionList && pEntry) {
		InteractOption *pOption;

		pOption = interaction_GetDisplayOptionForEntry(pEntry, entGetLanguage(pPlayerEnt), pInteractable, pNode, pCritterEnt, pVolume, pPlayerEnt, bAppendOwnerToDisplayString?pOptionOwner:NULL, bCanPickup, bOverrideEntry, pMissionDef);
		if (pNode) {
			WorldInteractionEntry *pWorldEntry = wlInteractionNodeGetEntry(pNode);
			
			pOption->eInteractOptionType = InteractOptionType_Node;
			if(pWorldEntry)
			{
				pOption->uNodeInteractDist = wlInteractionGetInteractDist(pWorldEntry->full_interaction_properties);
				pOption->fNodeRadiusFallback = wlInteractionNodeGetSphereBounds(pNode, pOption->vNodePosFallback);
			}
			SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pOption->hNode);
		}
		else if (pCritterEnt) {
			pOption->eInteractOptionType = InteractOptionType_CritterEntity;
			
			pOption->entRef = entGetRef(pCritterEnt);
		}
		else if (pVolume) {
			pOption->eInteractOptionType = InteractOptionType_Volume;
			
			pOption->pcVolumeName = pVolume->pcName;
			if (pEntry->pInteractCond) {
				pOption->bDisabled = !volume_EvaluateExpr(iPartitionIdx, pVolume, pPlayerEnt, pEntry->pInteractCond);
			}
		}
		pOption->iIndex = iInteractionIndex;
		pOption->iPriority = interaction_GetPriority(pEntry);
		pOption->pcCategory = interaction_GetCategoryName(pEntry);
		pOption->bAutoExecute = interaction_GetAutoExecute(pEntry);
		if (pOptionOwner){
			pOption->iTeammateID = entGetContainerID(pOptionOwner);
			pOption->iTeammateType = entGetType(pOptionOwner);
		} else {
			pOption->iTeammateID = 0;
			pOption->iTeammateType = 0;
		}

		if (bOverrideEntry) {
			if(pPlayerEnt && pPlayerEnt->pPlayer) {
				eaPush(&pPlayerEnt->pPlayer->InteractStatus.eaOverrideEntries, pEntry);
				pOption->iIndex = 1000+(eaSize(&pPlayerEnt->pPlayer->InteractStatus.eaOverrideEntries)-1);
			}
		}
		eaPush(peaOptionList, pOption);

		{
			//This probably shouldn't be here, but all of the interaction questions have been answered at this point
			//and rechecking them elsewhere would be inefficient
			ContactDef *pContact = interaction_GetContactDef(pEntry);
			if (pContact) {
				pPlayerEnt->pPlayer->InteractStatus.eNearbyContactTypes |= pContact->eContactFlags;
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


void interaction_RecordQueueDefInteraction(Entity *pEnt, const char *pchQueueName)
{
	if(!pEnt || !pEnt->pPlayer) {
		return;
	} else {
		EntInteractStatus *pStatus = &pEnt->pPlayer->InteractStatus;
		InteractedQueueDef *pInteractedQueue = eaIndexedGetUsingString(&pStatus->ppRecentQueueInteractions, pchQueueName);
		if (!pInteractedQueue) {
			pInteractedQueue = StructCreate(parse_InteractedQueueDef);
			pInteractedQueue->pchQueueName = pchQueueName;
			eaPush(&pStatus->ppRecentQueueInteractions, pInteractedQueue);
		}

		pInteractedQueue->iInteractTime = timeSecondsSince2000();
		entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pEnt->pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}


static int interaction_DoorKeyMapEq(const Item *a, const Item *b)
{
	if (!a || !b || !a->pSpecialProps || !b->pSpecialProps || !a->pSpecialProps->pDoorKey || !b->pSpecialProps->pDoorKey) {
		return 0;
	}

	return(stricmp(a->pSpecialProps->pDoorKey->pchMap, b->pSpecialProps->pDoorKey->pchMap) == 0);
}


static WorldInteractionPropertyEntry* interaction_CreateOverrideDoorEntry(WorldInteractionPropertyEntry *pBaseEntry, WorldVariable*** peaVars, WorldVariableDef*** peaVarDefs, const char* pchDestMap)
{
	WorldInteractionPropertyEntry *pNewEntry = NULL;

	// Create the new entry
	if(pBaseEntry) {
		pNewEntry = StructClone(parse_WorldInteractionPropertyEntry, pBaseEntry);
	} else {
		pNewEntry = StructCreate(parse_WorldInteractionPropertyEntry);
	}

	if (pNewEntry) {
		// Create the door properties
		if (!pNewEntry->pDoorProperties) {
			pNewEntry->pDoorProperties = StructCreate(parse_WorldDoorInteractionProperties);
		}

		// Setup the door type and variables
		pNewEntry->pDoorProperties->eDoorType = WorldDoorType_MapMove;
		if (peaVars) {
			if (pNewEntry->pDoorProperties->eaOldVariables) {
				eaDestroyStruct(&pNewEntry->pDoorProperties->eaOldVariables, parse_WorldVariable);
			}
			eaPushEArray(&pNewEntry->pDoorProperties->eaOldVariables, peaVars);
		}
		if (peaVarDefs) {
			if (pNewEntry->pDoorProperties->eaVariableDefs) {
				eaDestroyStruct(&pNewEntry->pDoorProperties->eaVariableDefs, parse_WorldVariableDef);
			}
			eaPushEArray(&pNewEntry->pDoorProperties->eaVariableDefs, peaVarDefs);
		}

		// Setup the door destination
		pNewEntry->pDoorProperties->doorDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT; 
		pNewEntry->pDoorProperties->doorDest.eType = WVAR_MAP_POINT;
		if (!pNewEntry->pDoorProperties->doorDest.pSpecificValue) {
			pNewEntry->pDoorProperties->doorDest.pSpecificValue = StructCreate(parse_WorldVariable);
		}
		pNewEntry->pDoorProperties->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
		if (pNewEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap) {
			StructFreeStringSafe(&pNewEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap);
		}
		if (EMPTY_TO_NULL(pchDestMap)) {
			pNewEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap = StructAllocString(pchDestMap);
		}

		// Clear the door key
		if (pNewEntry->pDoorProperties->pcDoorKey) {
			StructFreeStringSafe(&pNewEntry->pDoorProperties->pcDoorKey);
		}
	}

	return pNewEntry;
}


static Item* interaction_GetDoorKeyItemMatchingMap(Item** eaItemsToSearch, const char* pchMapName, const char* pchMapVars)
{
	int j;
	if(eaItemsToSearch && pchMapName) {
		for(j = eaSize(&eaItemsToSearch)-1; j >=0; --j) {
			ItemDoorKey* pDoorKey = eaItemsToSearch[j]->pSpecialProps ? eaItemsToSearch[j]->pSpecialProps->pDoorKey : NULL;
			if (pDoorKey && pDoorKey->pchMap && (stricmp(pDoorKey->pchMap, pchMapName)==0)) {
				if (pchMapVars && pDoorKey->pchMapVars) {
					if (stricmp(pchMapVars, pDoorKey->pchMapVars) == 0) {
						return eaItemsToSearch[j];
					}
				} else {
					return eaItemsToSearch[j];
				}
			}
		}
	}
	return NULL;
}


static WorldInteractionPropertyEntry* interaction_GetDoorKeyEntryMatchingMap(WorldInteractionPropertyEntry** eaEntriesToSearch, const char* pchMapName, const char* pchMapVars)
{
	int j;

	if(eaEntriesToSearch) {
		for(j = eaSize(&eaEntriesToSearch)-1; j >=0 ; --j) {
			if(eaEntriesToSearch[j]->pDoorProperties && eaEntriesToSearch[j]->pDoorProperties->doorDest.pSpecificValue
				&& (stricmp(eaEntriesToSearch[j]->pDoorProperties->doorDest.pSpecificValue->pcZoneMap, pchMapName)==0)) {

				const char* pchFallbackVars = NULL;
				if (eaEntriesToSearch[j]->pDoorProperties->eaOldVariables) {
					pchFallbackVars = worldVariableArrayToString(eaEntriesToSearch[j]->pDoorProperties->eaOldVariables);
				} 
				if(pchFallbackVars && pchMapVars) {
					if (stricmp(pchFallbackVars, pchMapVars) == 0) {
						return eaEntriesToSearch[j];
					}
				} else {
					return eaEntriesToSearch[j];
				}
			}
		}
	}

	return NULL;
}


bool interaction_AddOptionToInteract(int iPartitionIdx, InteractOption ***peaOptionList, WorldInteractionPropertyEntry *pEntry, GameInteractable *pInteractable, WorldInteractionNode *pNode, Entity *pCritterEnt, GameNamedVolume *pVolume, int iIndex, Entity *pPlayerEnt, bool bCanPickup)
{
	WorldDoorInteractionProperties *pDoorProps;
	int j, k;

	PERFINFO_AUTO_START_FUNC();

	pDoorProps = interaction_GetDoorProperties(pEntry);

	// JoinTeammate doors add an entry for each team member who is not on the same map that has last traveled through a door that has
	// the same door ID as the one for this interaction entry
	if (pDoorProps && pDoorProps->eDoorType == WorldDoorType_JoinTeammate) {
		bool ret = false;
		if (pPlayerEnt && pPlayerEnt->pPlayer) {
			Team *pTeam = team_GetTeam(pPlayerEnt);

			// Attempt to add an entry for each teammate that matches the JoinTeammate condition
			if (pTeam && pPlayerEnt->pTeam->eState == TeamState_Member) {
				for (j = 0; j < eaSize(&pTeam->eaMembers); j++) {
					Entity *pTeammate = GET_REF(pTeam->eaMembers[j]->hEnt);
					if (pTeammate && pTeammate->pTeam && pTeammate->pTeam->eState == TeamState_Member &&
						(entGetType(pPlayerEnt) != entGetType(pTeammate) || entGetContainerID(pPlayerEnt) != entGetContainerID(pTeammate)) &&
						pTeammate->pPlayer && pTeammate->pPlayer->pchLastUsedDoorIdentifier && pTeammate->pPlayer->pchLastUsedDoorIdentifier == pDoorProps->pcDoorIdentifier)
					{
						ret = true;
						if (peaOptionList) {
							interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pEntry, iIndex, pPlayerEnt, pTeammate, true, false, false, NULL);
						} else {
							PERFINFO_AUTO_STOP();
							return ret;
						}
					}
				}
			} 
		}

		PERFINFO_AUTO_STOP();
		return ret;
	} else if (pDoorProps && (pDoorProps->bPerPlayer || pDoorProps->bSinglePlayer) && pDoorProps->eDoorType != WorldDoorType_Keyed){
		bool ret = false;

		// "Per-Player" Door behavior: Create one entry for each teammate who meets the requirements
		if (pPlayerEnt){
			Team *pTeam = team_GetTeam(pPlayerEnt);

			// First attempt to add the player's entry
			if (interaction_EntryIsEnabled(iPartitionIdx, pEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)){
				ret = true;
				if (peaOptionList){
					interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pEntry, iIndex, pPlayerEnt, pPlayerEnt, false, false, false, NULL);
					if (gConf.bSinglePerPlayerOptionWhenDoorKeyIsPassed) {
						if (pDoorProps->bDestinationSameOwner) {
							PERFINFO_AUTO_STOP();
							return ret;
						}

						for (k=0; k < eaSize(&pDoorProps->eaOldVariables); k++) {
							if(stricmp(pDoorProps->eaOldVariables[k]->pcName, ITEM_DOOR_KEY_MAP_VAR) == 0) {
								PERFINFO_AUTO_STOP();
								return ret;
							}
						}
						for (k=0; k < eaSize(&pDoorProps->eaVariableDefs); k++) {
							if(stricmp(pDoorProps->eaVariableDefs[k]->pcName, ITEM_DOOR_KEY_MAP_VAR) == 0) {
								PERFINFO_AUTO_STOP();
								return ret;
							}
						}
					}
				} else {
					PERFINFO_AUTO_STOP();
					return ret;
				}
			}

			// Then attempt to add an entry from the player's teammates
			if (!pDoorProps->bSinglePlayer && pTeam && pPlayerEnt->pTeam->eState == TeamState_Member){
				for (j = 0; j < eaSize(&pTeam->eaMembers); j++){
					Entity *pTeammate = GET_REF(pTeam->eaMembers[j]->hEnt);
					if (pTeammate && (entGetType(pPlayerEnt) != entGetType(pTeammate) || entGetContainerID(pPlayerEnt) != entGetContainerID(pTeammate))) {
						if (pTeammate->pTeam && pTeammate->pTeam->eState == TeamState_Member && interaction_EntryIsEnabled(iPartitionIdx, pEntry, pTeammate, pInteractable, pCritterEnt, pVolume)){
							ret = true;
							if (peaOptionList) {
								interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pEntry, iIndex, pPlayerEnt, pTeammate, true, false, false, NULL);
								if (gConf.bSinglePerPlayerOptionWhenDoorKeyIsPassed) {
									if (pDoorProps->bDestinationSameOwner) {
										PERFINFO_AUTO_STOP();
										return ret;
									}

									for (k=0; k < eaSize(&pDoorProps->eaOldVariables); k++) {
										if (stricmp(pDoorProps->eaOldVariables[k]->pcName, ITEM_DOOR_KEY_MAP_VAR) == 0) {
											PERFINFO_AUTO_STOP();
											return ret;
										}
									}
									for (k=0; k < eaSize(&pDoorProps->eaVariableDefs); k++) {
										if (stricmp(pDoorProps->eaVariableDefs[k]->pcName, ITEM_DOOR_KEY_MAP_VAR) == 0) {
											PERFINFO_AUTO_STOP();
											return ret;
										}
									}
								}
							} else {
								PERFINFO_AUTO_STOP();
								return ret;
							}
						}
					}
				}
			} 
		}

		PERFINFO_AUTO_STOP();
		return ret;
	} else if(pDoorProps && pDoorProps->eDoorType == WorldDoorType_Keyed && pDoorProps->pcDoorKey) {
		// "Keyed" door.  Add an entry for each key the player has which matches the door.
		WorldInteractionPropertyEntry *pNewEntry = NULL;
		WorldVariable** eaMapVars = NULL;
		int i;

 		if(pDoorProps->bPerPlayer && !pDoorProps->bSinglePlayer && team_IsMember(pPlayerEnt)) {
			// Team
			Team *pTeam = team_GetTeam(pPlayerEnt);
			bool bAppendOwner = false;
			Item **eaItems = NULL;
			WorldInteractionPropertyEntry **eaFallbackEntries = NULL;
			Item** eaFoundItems = NULL;
			bool bReturn = false;

			if(pTeam) {
				for(i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--)  {
					int iItem;
					Entity *pTeammate = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);
					if (!pTeammate) {
						pTeammate = GET_REF(pTeam->eaMembers[i]->hEnt);
					}

					if (pTeammate) {
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pTeammate);
						bool bItemFound = false;
						// HiddenLocationData bag
						inv_ent_GetSimpleItemList(pTeammate, InvBagIDs_HiddenLocationData, &eaItems, false, pExtract);

						for(iItem = 0; iItem < eaSize(&eaItems); iItem++) {
							if (eaItems[iItem]->pSpecialProps && eaItems[iItem]->pSpecialProps->pDoorKey && eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey && (stricmp(eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey, pDoorProps->pcDoorKey) == 0)) {
								if (!eaFoundItems || eaFindCmp(&eaFoundItems, eaItems[iItem], interaction_DoorKeyMapEq) == -1) {
									worldVariableStringToArray(eaItems[iItem]->pSpecialProps->pDoorKey->pchMapVars, &eaMapVars);
									pNewEntry = interaction_CreateOverrideDoorEntry(pEntry, &eaMapVars, NULL, eaItems[iItem]->pSpecialProps->pDoorKey->pchMap);
									eaDestroy(&eaMapVars);

									if(interaction_EntryIsEnabled(iPartitionIdx, pNewEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)) {
										if (peaOptionList) {
											interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pNewEntry, iIndex, pPlayerEnt, pTeammate, (pPlayerEnt!=pTeammate), bCanPickup, true, SAFE_GET_REF(eaItems[iItem]->pSpecialProps->pDoorKey, hMission));
										} else {
											StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
										}
										eaPush(&eaFoundItems, eaItems[iItem]);
									} else {
										StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
									}
								}
							}
						}
						eaDestroy(&eaItems);

						// LocationData bag
						inv_ent_GetSimpleItemList(pTeammate, InvBagIDs_LocationData, &eaItems, false, pExtract);

						for(iItem = 0; iItem < eaSize(&eaItems); iItem++) {
							if (eaItems[iItem]->pSpecialProps && eaItems[iItem]->pSpecialProps->pDoorKey && eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey && (stricmp(eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey, pDoorProps->pcDoorKey) == 0)) {
								if (!eaFoundItems || eaFindCmp(&eaFoundItems, eaItems[iItem], interaction_DoorKeyMapEq) == -1) {
									worldVariableStringToArray(eaItems[iItem]->pSpecialProps->pDoorKey->pchMapVars, &eaMapVars);
									pNewEntry = interaction_CreateOverrideDoorEntry(pEntry, &eaMapVars, NULL, eaItems[iItem]->pSpecialProps->pDoorKey->pchMap);
									eaDestroy(&eaMapVars);

									if (interaction_EntryIsEnabled(iPartitionIdx, pNewEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)) {
										if (peaOptionList) {
											interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pNewEntry, iIndex, pPlayerEnt, pTeammate, (pPlayerEnt!=pTeammate), bCanPickup, true, SAFE_GET_REF(eaItems[iItem]->pSpecialProps->pDoorKey, hMission));
										} else {
											StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
										}
										eaPush(&eaFoundItems, eaItems[iItem]);
									} else {
										StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
									}
								}
							}
						}
						eaDestroy(&eaItems);
					}
				}

				// Check teammates' current maps
				for(i = eaSize(&pTeam->eaMembers)-1; i >= 0; i--)  {
					TeamMember *pTeamMember = pTeam->eaMembers[i];
					const char* pchTeammateVars = pTeamMember->pcMapVars;
					const char* pchTeammateMap = pTeamMember->pcMapName;
					bool bVarFound = false;
					Entity *pTeammateEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER,pTeamMember->iEntID);
					if (!pTeammateEnt) {
						pTeammateEnt = GET_REF(pTeamMember->hEnt);
					}

					if(	interaction_GetDoorKeyItemMatchingMap(eaFoundItems, pchTeammateMap, pchTeammateVars) ||
						interaction_GetDoorKeyEntryMatchingMap(eaFallbackEntries, pchTeammateMap, pchTeammateVars)) {
						continue;
					}

					
					if (worldVariableStringToArray(pchTeammateVars, &eaMapVars)) {
						for (j=0; j < eaSize(&eaMapVars) && !bVarFound; j++) {
							if(	eaMapVars[j]->pcName && eaMapVars[j]->pcStringVal &&
								(stricmp(eaMapVars[j]->pcName, ITEM_DOOR_KEY_MAP_VAR)==0) && 
								(stricmp(eaMapVars[j]->pcStringVal, pDoorProps->pcDoorKey)==0)) {
								bVarFound = true;
							}
						}
					}

					if (bVarFound) {
						pNewEntry = interaction_CreateOverrideDoorEntry(pEntry, &eaMapVars, NULL, pchTeammateMap);
						eaDestroy(&eaMapVars);

						if (interaction_EntryIsEnabled(iPartitionIdx, pNewEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)) {
							if (peaOptionList) {
								interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pNewEntry, iIndex, pPlayerEnt, pTeammateEnt, (pPlayerEnt!=pTeammateEnt), bCanPickup, true, NULL);
								eaPush(&eaFallbackEntries, pNewEntry);
							} else {
								StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
							}
						} else {
							StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
						}
					} else {
						eaDestroyStruct(&eaMapVars, parse_WorldVariable);
					}
				}

				// Try cached entry
				if(pTeam->pCachedDestination && pTeam->pCachedDestination->pEntry && pTeam->pCachedDestination->pchDoorKey
					&& (stricmp(pTeam->pCachedDestination->pchDoorKey, pDoorProps->pcDoorKey)==0)) {

					int iVar;
					const char* pchCachedMapVars = worldVariableArrayToString(pTeam->pCachedDestination->pEntry->pDoorProperties->eaOldVariables);
					const char* pchCachedMapName = pTeam->pCachedDestination->pEntry->pDoorProperties->doorDest.pSpecificValue ? pTeam->pCachedDestination->pEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap : NULL;

					if(	!interaction_GetDoorKeyItemMatchingMap(eaFoundItems, pchCachedMapName, pchCachedMapVars) &&
						!interaction_GetDoorKeyEntryMatchingMap(eaFallbackEntries, pchCachedMapName, pchCachedMapVars)) {

						for(iVar = 0; iVar < eaSize(&pTeam->pCachedDestination->pEntry->pDoorProperties->eaOldVariables); iVar++) {
							eaPush(&eaMapVars, StructClone(parse_WorldVariable, pTeam->pCachedDestination->pEntry->pDoorProperties->eaOldVariables[iVar]));
						}
						pNewEntry = interaction_CreateOverrideDoorEntry(pTeam->pCachedDestination->pEntry, &eaMapVars, NULL, pTeam->pCachedDestination->pEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap);
						eaDestroy(&eaMapVars);

						if (interaction_EntryIsEnabled(iPartitionIdx, pTeam->pCachedDestination->pEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)) {
							if(peaOptionList) {
								interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pNewEntry, iIndex, pPlayerEnt, pPlayerEnt, false, bCanPickup, true, NULL);
								eaPush(&eaFallbackEntries, pNewEntry);
							} else {
								StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
							}
						} else {
							StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
						}
					}
				}
			}
			bReturn = eaFoundItems || eaFallbackEntries;
			if (eaFoundItems) {
				eaDestroy(&eaFoundItems);
			}
			if (eaFallbackEntries) {
				eaDestroy(&eaFallbackEntries);  // Each struct is destroyed in interaction_OncePerFrameScanTick
			}
			PERFINFO_AUTO_STOP();
			return bReturn;

		} else {
			// Single Player
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
			int iItem;
			Item** eaItems = NULL;
			Item** eaFoundItems = NULL;

			// HiddenLocationData bag
			inv_ent_GetSimpleItemList(pPlayerEnt, InvBagIDs_HiddenLocationData, &eaItems, false, pExtract);

			for(iItem = 0; iItem < eaSize(&eaItems); iItem++) {

				if (eaItems[iItem]->pSpecialProps && eaItems[iItem]->pSpecialProps->pDoorKey && eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey && (stricmp(eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey, pDoorProps->pcDoorKey) == 0)) {
					if (!eaFoundItems || eaFindCmp(&eaFoundItems, eaItems[iItem], interaction_DoorKeyMapEq) == -1) {
						worldVariableStringToArray(eaItems[iItem]->pSpecialProps->pDoorKey->pchMapVars, &eaMapVars);
						pNewEntry = interaction_CreateOverrideDoorEntry(pEntry, &eaMapVars, NULL, eaItems[iItem]->pSpecialProps->pDoorKey->pchMap);
						eaDestroy(&eaMapVars);

						if (interaction_EntryIsEnabled(iPartitionIdx, pNewEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)) {
							if (peaOptionList) {
								interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pNewEntry, iIndex, pPlayerEnt, pPlayerEnt, false, bCanPickup, true, GET_REF(eaItems[iItem]->pSpecialProps->pDoorKey->hMission));
							} else {
								StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
							}
							eaPush(&eaFoundItems, eaItems[iItem]);
						} else {
							StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
						}
					}
				}
			}
			eaDestroy(&eaItems);

			// LocationData bag
			inv_ent_GetSimpleItemList(pPlayerEnt, InvBagIDs_LocationData, &eaItems, false, pExtract);

			for(iItem = 0; iItem < eaSize(&eaItems); iItem++) {
				if (eaItems[iItem]->pSpecialProps->pDoorKey && eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey && (stricmp(eaItems[iItem]->pSpecialProps->pDoorKey->pchDoorKey, pDoorProps->pcDoorKey) == 0)) {
					if (!eaFoundItems || eaFindCmp(&eaFoundItems, eaItems[iItem], interaction_DoorKeyMapEq) == -1) {
						worldVariableStringToArray(eaItems[iItem]->pSpecialProps->pDoorKey->pchMapVars, &eaMapVars);
						pNewEntry = interaction_CreateOverrideDoorEntry(pEntry, &eaMapVars, NULL, eaItems[iItem]->pSpecialProps->pDoorKey->pchMap);
						eaDestroy(&eaMapVars);

						if (interaction_EntryIsEnabled(iPartitionIdx, pNewEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)) {
							if (peaOptionList) {
								interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pNewEntry, iIndex, pPlayerEnt, pPlayerEnt, false, bCanPickup, true, GET_REF(eaItems[iItem]->pSpecialProps->pDoorKey->hMission));
							} else {
								StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
							}
							eaPush(&eaFoundItems, eaItems[iItem]);
						} else {
							StructDestroy(parse_WorldInteractionPropertyEntry, pNewEntry);
						}
					}
				}
			}
			eaDestroy(&eaItems);

			if (eaFoundItems) {
				eaDestroy(&eaFoundItems);
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
	} else if (pPlayerEnt) {
		// Normal behavior
		if (interaction_EntryIsEnabled(iPartitionIdx, pEntry, pPlayerEnt, pInteractable, pCritterEnt, pVolume)) {
			if (peaOptionList){
				interaction_AddOptionToInteractHelper(iPartitionIdx, peaOptionList, pInteractable, pNode, pCritterEnt, pVolume, pEntry, iIndex, pPlayerEnt, NULL, false, bCanPickup, false, NULL);
			}
			PERFINFO_AUTO_STOP();
			return true;
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}


static bool interaction_AddOptionForOldInteract(InteractOption ***peaInteractList, Entity *pCritterEnt, Entity *pPlayerEnt, ContactDef *pContactDef, CritterDef *pCritterDef)
{
	InteractOption *pOption = NULL;
	const char *tmpstr = NULL;
	char *eString = NULL; // String for formatting messages

	PERFINFO_AUTO_START_FUNC();

	// Don't add if cannot interact with contact
	if (pContactDef && !contact_CanInteract(pContactDef, pPlayerEnt)) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (peaInteractList) {
		pOption = StructCreate(parse_InteractOption);
		estrStackCreate(&eString);
		pOption->entRef = entGetRef(pCritterEnt);
		pOption->iPriority = WorldOptionalActionPriority_Normal;

		if (pCritterEnt->pCritter) {
			// Use interact string if there is one.  Otherwise, you're looting a loot bag or talking to a critter
			const char *pcText = NULL;
			InventoryBag *pLootBag = NULL;

			if (pCritterDef) {
				pcText = langTranslateMessage(entGetLanguage(pPlayerEnt),GET_REF(pCritterDef->oldInteractProps.interactText.hMessage));
			}

			if (pcText) {
				tmpstr = pcText;
			} else if (GET_REF(pCritterEnt->pCritter->hDisplayNameMsg)) {
				entFormatGameMessageKey( pPlayerEnt, &eString, "MechanicsUI.TalkTo",
					STRFMT_STRING("Name", entTranslateMessageRef(pPlayerEnt, pCritterEnt->pCritter->hDisplayNameMsg)), STRFMT_END);

				tmpstr = eString;
			} else {
				entFormatGameMessageKey( pPlayerEnt, &eString, "MechanicsUI.InteractWith",
					STRFMT_STRING("Name", entGetLangName(pCritterEnt, entGetLanguage(pPlayerEnt))), STRFMT_END);

				tmpstr = eString;
			}

			pOption->pcInteractString = StructAllocString(tmpstr);
		}

		estrDestroy(&eString);

		eaPush(peaInteractList, pOption);

		//This probably shouldn't be here, but all of the interaction questions have been answered at this point
		//and rechecking them elsewhere would be inefficient
		if ( pContactDef && pPlayerEnt->pPlayer ) {
			pPlayerEnt->pPlayer->InteractStatus.eNearbyContactTypes |= pContactDef->eContactFlags;
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}


// Does the node-specific gathering of interact options
bool interaction_GetValidInteractNodeOptions(GameInteractable *pInteractable, Entity *pPlayerEnt, 
											  InteractOption*** peaOptionList, bool bRangeCheck,
											  bool bExcludeDestructibles)
{
	int iNumEntries;
	int i;
	bool bCanPickup = false;
	bool bAddedOption = false;
	InteractionLootTracker *pTracker;
	WorldInteractionNode *pNode;
	int iPartitionIdx;

	if (!pInteractable) {
		// No interactable for this node so return false
		return false;
	}

	if (!pInteractable->bCanBeInteractedWith || pInteractable->bIsVolumeTriggeredGate) {
		// Interactable has no properties that can be interacted with
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("CheckCooldown", 1);

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	if (interactable_IsBusy(iPartitionIdx, pInteractable, pPlayerEnt)){
		// Node is in use or on cooldown
		PERFINFO_AUTO_STOP(); // CheckCooldown
		PERFINFO_AUTO_STOP(); // Func
		return false;
	}

	PERFINFO_AUTO_STOP(); // CheckCooldown

	if (pInteractable->bHasVisibleExpression) {
		PERFINFO_AUTO_START("CheckVisibility", 1);

		//If this node has per-entity visibility, figure out if it's visible or not.
		if (!interactable_EvaluateVisibilityForEntity(pPlayerEnt, pInteractable)) {
			PERFINFO_AUTO_STOP(); // CheckVisibility
			PERFINFO_AUTO_STOP(); // Func
			return false;
		}

		PERFINFO_AUTO_STOP(); // CheckVisibility
	}

	PERFINFO_AUTO_START("CheckCanPickup", 1);

	if(bRangeCheck || bExcludeDestructibles) {
		bCanPickup = interactable_IsDestructibleAndNotInteractable(pInteractable);
	}

	// If we're excluding destructibles, and this is a destructible with no other
	//  interact options (aka "CanPickup"), then we're done
	if(bExcludeDestructibles && bCanPickup) {
		PERFINFO_AUTO_STOP(); // CheckCanPickup
		PERFINFO_AUTO_STOP(); /// Func
		return false;
	}

	PERFINFO_AUTO_STOP(); // CheckCanPickup

	PERFINFO_AUTO_START("CheckLoot", 1);

	// Exclude nodes that have loot, none of which can be picked up by the player
	pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
	if (pTracker && !LootTracker_CanEntityLoot(pTracker, pPlayerEnt)) {
		PERFINFO_AUTO_STOP(); // CheckLoot
		PERFINFO_AUTO_STOP(); // Func
		return false;
	}

	PERFINFO_AUTO_STOP(); // CheckLoot

	pNode = interactable_GetWorldInteractionNode(pInteractable);

	if ( bRangeCheck ) {
		Vec3 vClose;
		Vec3 vPlayerPos;
		F32 fDist, fInteractRange;

		PERFINFO_AUTO_START("CheckRange", 1);
		entGetCombatPosDir(pPlayerEnt, NULL, vPlayerPos, NULL);

		bCanPickup = interactable_IsDestructibleAndNotInteractable(pInteractable);
		fInteractRange = (bCanPickup) ? entity_GetPickupRange(pPlayerEnt) : gslEntity_GetInteractRange(pPlayerEnt, NULL, pNode);

		if ( !entity_IsNodeInRange( pPlayerEnt, vPlayerPos, pNode, fInteractRange, 0, 0, vClose, &fDist, true ) ) {
			PERFINFO_AUTO_STOP(); // CheckRange
			PERFINFO_AUTO_STOP(); // Func
			return false;
		}

		PERFINFO_AUTO_STOP(); // CheckRange
	}

	PERFINFO_AUTO_START("CheckPropertyEntries",1);

	// See if any Property Entries are valid
	iNumEntries = interactable_GetNumPropertyEntries(pInteractable, SAFE_MEMBER3(pPlayerEnt, pPlayer, missionInfo, bHasNamespaceMission));
	for(i=0; i<iNumEntries; ++i) {
		WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, i);
		bAddedOption |= interaction_AddOptionToInteract(iPartitionIdx, peaOptionList, pEntry, pInteractable, pNode, NULL, NULL, i, pPlayerEnt, bCanPickup);
	}

	PERFINFO_AUTO_STOP(); // CheckPropertyEntries

	PERFINFO_AUTO_STOP(); // Func
	return bAddedOption;
}


// Does the entity-specific gathering of interact options
static bool interaction_GetValidInteractEntOptions(int iPartitionIdx, Entity *pCritterEnt, Entity *pPlayerEnt, 
												   InteractOption*** peaOptionList, bool bRangeCheck )
{
	PlayerDebug* pDebug;
	int i;
	bool bCanPickup = false;
	bool bAddedOption = false;
	int iNumEntries = 0;

	if (!pCritterEnt || !pCritterEnt->pCritter || !pCritterEnt->pCritter->bIsInteractable) {
		// Critter has no properties that can be interacted with
		return false;
	}

	if (pCritterEnt->pCritter->encounterData.iPlayerOwnerID) {
		Entity *pOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pCritterEnt->pCritter->encounterData.iPlayerOwnerID);
		if (pOwnerEnt && pOwnerEnt->pPlayer && pOwnerEnt->pPlayer->InteractStatus.bInteracting && (pOwnerEnt != pPlayerEnt)) {
			return false;
		}
	}

	PERFINFO_AUTO_START_FUNC();

	// Loot interaction is handled special
	if ( interaction_IsLootEntityOwned(pCritterEnt, pPlayerEnt)) {
		PERFINFO_AUTO_START("LootEntity", 1);

		// Check range
		if ( bRangeCheck ) {
			F32 fDist;
			fDist = entGetDistance( pPlayerEnt, NULL, pCritterEnt, NULL, NULL );
			if (fDist > gslEntity_GetInteractRange(pPlayerEnt, pCritterEnt, NULL)) {
				PERFINFO_AUTO_STOP(); // LootEntity
				PERFINFO_AUTO_STOP(); // Func
				return false;
			}
		}

		if (peaOptionList) {
			InteractOption *pOption = StructCreate(parse_InteractOption);
			char *eString = NULL;
			
			pOption->entRef = entGetRef(pCritterEnt);
			entFormatGameMessageKey( pPlayerEnt, &eString, "MechanicsUI.TakeLootFrom",
				STRFMT_STRING("Name", entTranslateMessageRef(pPlayerEnt, pCritterEnt->pCritter->hDisplayNameMsg)), STRFMT_END);
			pOption->pcInteractString = StructAllocString(eString);
			pOption->iPriority = WorldOptionalActionPriority_Normal;
			pOption->bAutoExecute = pCritterEnt->pCritter->bAutoInteract;
			estrDestroy(&eString);

			if(entity_ShouldAutoLootTarget(pPlayerEnt, pCritterEnt)) {
				eaCopyStructs(&pCritterEnt->pCritter->eaLootBags, &pOption->eaLootBags,parse_InventoryBag);
			}

			eaPush(peaOptionList, pOption);
		}
		PERFINFO_AUTO_STOP(); // LootEntity
		PERFINFO_AUTO_STOP(); // Func
		return true;
	} else if (inv_HasLoot(pCritterEnt)) {
		// Someone else's loot
		PERFINFO_AUTO_STOP();
		return false;
	} else if (pCritterEnt->pCritter->encounterData.pLootTracker && !LootTracker_CanEntityLoot(pCritterEnt->pCritter->encounterData.pLootTracker, pPlayerEnt)) {
		PERFINFO_AUTO_STOP();
		return false;
	}

	PERFINFO_AUTO_START("CheckProperties", 1);

	// See if any Property Entries are valid
	if (pCritterEnt->pCritter->encounterData.pGameEncounter) {
		iNumEntries = interaction_GetNumActorAndCritterEntries(pCritterEnt->pCritter->encounterData.pGameEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, pCritterEnt->pCritter);
	} else {
		iNumEntries = critter_GetNumInteractionEntries(pCritterEnt->pCritter);
	}

	PERFINFO_AUTO_STOP();

	if(iNumEntries) {
		PERFINFO_AUTO_START("CheckCooldown", 1);
		// Check if entity is on cooldown
		for(i=0; i<iNumEntries; ++i) {
			if (interaction_IsInteractTargetBusy2(iPartitionIdx, pPlayerEnt, entGetRef(pCritterEnt), NULL, NULL, i)) {
				PERFINFO_AUTO_STOP(); // CheckCooldown
				PERFINFO_AUTO_STOP(); // Func
				return false;
			}
		}
		PERFINFO_AUTO_STOP();

		// Check range
		if ( bRangeCheck ) {
			F32 fDist;
			U32 uRange;

			PERFINFO_AUTO_START("CheckRange", 1);

			fDist = entGetDistance( pPlayerEnt, NULL, pCritterEnt, NULL, NULL );
			uRange = pCritterEnt->pCritter->uInteractDist;

			if (uRange && fDist > uRange) {
				PERFINFO_AUTO_STOP(); // CheckRange
				PERFINFO_AUTO_STOP(); // Func
				return false;
			} else if (fDist > gslEntity_GetInteractRange(pPlayerEnt, pCritterEnt, NULL)) {
				PERFINFO_AUTO_STOP(); // CheckRange
				PERFINFO_AUTO_STOP(); // Func
				return false;
			}

			PERFINFO_AUTO_STOP(); // CheckRange
		}

		PERFINFO_AUTO_START("AddOptions", 1);

		// Add options
		for(i=0; i<iNumEntries; ++i) {
			WorldInteractionPropertyEntry *pEntry = NULL;
			if (pCritterEnt->pCritter->encounterData.pGameEncounter) {
				pEntry = interaction_GetActorOrCritterEntry(pCritterEnt->pCritter->encounterData.pGameEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, pCritterEnt->pCritter, i);
			} else {
				pEntry = critter_GetInteractionEntry(pCritterEnt->pCritter, i);
			}
			bAddedOption |= interaction_AddOptionToInteract(iPartitionIdx, peaOptionList, pEntry, NULL, NULL, pCritterEnt, NULL, i, pPlayerEnt, false);
		}

		PERFINFO_AUTO_STOP();

	} else if (gConf.bAllowOldEncounterData && !pCritterEnt->pCritter->encounterData.pGameEncounter) {
		OldActorInfo *pInfo = NULL;
		Expression *pExpr = NULL;
		ContactDef *pContactDef = NULL;
		CritterDef *pCritterDef = NULL;

		PERFINFO_AUTO_START("OldEncounterCheckCooldown", 1);

		// Check if is on cooldown
		if (interaction_IsInteractTargetBusy2(iPartitionIdx, pPlayerEnt, entGetRef(pCritterEnt), NULL, NULL, 0)) {
			PERFINFO_AUTO_STOP(); // OldEncounterCheckCooldown
			PERFINFO_AUTO_STOP(); // Func
			return false;
		}

		PERFINFO_AUTO_STOP();

		// Check range
		if ( bRangeCheck ) {
			F32 fDist;
			PERFINFO_AUTO_START("OldEncounterCheckRange", 1);

			fDist = entGetDistance( pPlayerEnt, NULL, pCritterEnt, NULL, NULL );
			if (fDist > gslEntity_GetInteractRange(pPlayerEnt, pCritterEnt, NULL)) {
				PERFINFO_AUTO_STOP(); // OldEncounterCheckRange
				PERFINFO_AUTO_STOP(); // Func
				return false;
			}

			PERFINFO_AUTO_STOP(); // OldEncounterCheckRange
		}

		PERFINFO_AUTO_START("OldEncounterCheckInteractCond", 1);

		// Does the critter have an interaction condition?
		pCritterDef = GET_REF(pCritterEnt->pCritter->critterDef);
		if( pCritterEnt->pCritter->encounterData.sourceActor ) {
			pInfo = oldencounter_GetActorInfo(pCritterEnt->pCritter->encounterData.sourceActor);
		}
		if( pInfo && pInfo->oldActorInteractProps.interactCond ) {
			pExpr = pInfo->oldActorInteractProps.interactCond;
		} else if( pCritterDef && pCritterDef->oldInteractProps.interactCond ) {
			pExpr = pCritterDef->oldInteractProps.interactCond;
		}
		pDebug = entGetPlayerDebug(pPlayerEnt, false);
		if ((!pDebug || !pDebug->allowAllInteractions) && pExpr && !interaction_EvaluateCritterInteractCond(pExpr, pPlayerEnt, pCritterEnt)) {
			PERFINFO_AUTO_STOP(); // OldEncounterCheckInteractCond
			PERFINFO_AUTO_STOP(); // Func
			return false;
		}

		PERFINFO_AUTO_STOP(); // OldEncounterCheckInteractCond

		PERFINFO_AUTO_START("OldEncounterAddOptions", 1);

		// Add interact option as applicable
		if (GET_REF(pCritterEnt->pCritter->encounterData.hContactDefOverride)) {
			pContactDef = GET_REF(pCritterEnt->pCritter->encounterData.hContactDefOverride);
		} else if (pCritterEnt->pCritter->encounterData.sourceActor && 
			pCritterEnt->pCritter->encounterData.sourceActor->details.info &&
			GET_REF(pCritterEnt->pCritter->encounterData.sourceActor->details.info->contactScript)) {
			pContactDef = GET_REF(pCritterEnt->pCritter->encounterData.sourceActor->details.info->contactScript);
		} 
		bAddedOption = interaction_AddOptionForOldInteract(peaOptionList, pCritterEnt, pPlayerEnt, pContactDef, pCritterDef);

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP(); // Func
	return bAddedOption;
}


bool interaction_FindInteractionEnts(Entity *pPlayerEnt, Entity ***peaEntsOut)
{
	static Entity **eaProxEnts = NULL;
	Vec3 vSourcePos;
	int i;
	
	PERFINFO_AUTO_START_FUNC();
	entGetPos(pPlayerEnt, vSourcePos);				
	eaSetSize(&eaProxEnts, 0);
	entGridProximityLookupExEArray(entGetPartitionIdx(pPlayerEnt), vSourcePos, &eaProxEnts, interaction_GetEntInteractMaxRange(pPlayerEnt), 0, ENTITYFLAG_IGNORE, pPlayerEnt);

	for (i=eaSize(&eaProxEnts)-1; i>=0; i--) {
		Entity *pCurrEnt = eaProxEnts[i];

		// Don't interact with self
		// Don't interact if critter has no interaction properties
		if (( pCurrEnt == pPlayerEnt ) || !pCurrEnt->pCritter || !pCurrEnt->pCritter->bIsInteractable ) {
			eaRemoveFast(&eaProxEnts, i);
		}
	}

	if (eaSize(&eaProxEnts) > 0) {
		eaCopy(peaEntsOut, &eaProxEnts);
		PERFINFO_AUTO_STOP();
		return true;
	} else {
		PERFINFO_AUTO_STOP();
		return false;
	}
}


static void interaction_ClearInteraction(Entity *pEnt, bool bClearOptions, bool bClearPickupTarget, bool bClearDialog)
{
	bool bDirty = false;

	PERFINFO_AUTO_START_FUNC();

	// Clear interact options
	if ( bClearOptions && pEnt->pPlayer->InteractStatus.interactOptions.eaOptions ) {
		eaClearStruct(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions, parse_InteractOption);
		pEnt->pPlayer->InteractStatus.interactTargetCounter = -1; // Force immediate check next chance
		bDirty = true;
	}

	// Clear pickup target
	if (bClearPickupTarget && pEnt->pPlayer->InteractStatus.pickupTarget) {
		StructFreeString(pEnt->pPlayer->InteractStatus.pickupTarget);
		pEnt->pPlayer->InteractStatus.pickupTarget = NULL;
		bDirty = true;
	}

	// Clear any interaction dialogs
	if (bClearDialog && (interaction_IsPlayerInteracting(pEnt) || interaction_IsPlayerInDialog(pEnt))) {
		interaction_EndInteractionAndDialog(entGetPartitionIdx(pEnt), pEnt, false, true, true);
	}

	if ( bDirty ) {
		entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pEnt->pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}

	PERFINFO_AUTO_STOP();
}


// These numbers indicate the number of frames between testing for
// interactables that can be targeted.  
// COUNT_NO_MOVE should be a multiple of COUNT.
#define INTERACT_MAX_TARGET_TEST_COUNT			30
#define INTERACT_MAX_TARGET_TEST_COUNT_NO_MOVE	90

// These numbers indicate the number of frames between testing for
// interactables that can be interacted with.
// COUNT_NO_MOVE should be a multiple of COUNT.  And both should be factors of TARGET_TEST_COUNT_NO_MOVE
#define INTERACT_MAX_TEST_COUNT					6
#define INTERACT_MAX_TEST_COUNT_NO_MOVE			30

#define MAX_RECENT_AUTO_EXECUTES				10

//This is the amount of time recent queuedef interactions remain around
#define RECENT_QUEUE_DURATION					180 //3 Minutes

int cmpInteractOption(const InteractOption **l, const InteractOption **r)
{
	const InteractOption *lhs = *l;
	const InteractOption *rhs = *r;

	WorldInteractionNode *lNode, *rNode;

	if (lhs->entRef != rhs->entRef) {
		return rhs->entRef > lhs->entRef ? -1 : 1;
	}

	if (lhs->eInteractOptionType != rhs->eInteractOptionType) {
		return rhs->eInteractOptionType > lhs->eInteractOptionType ? -1 : 1;
	}

	if (lhs->pcVolumeName != rhs->pcVolumeName) {
		return stricmp(lhs->pcVolumeName, rhs->pcVolumeName);
	}

	if (lhs->pcInteractString != rhs->pcInteractString) {
		return stricmp(lhs->pcInteractString, rhs->pcInteractString);
	}

	if (lhs->pcUsabilityString != rhs->pcUsabilityString) {
		return stricmp(lhs->pcUsabilityString, rhs->pcUsabilityString);
	}

	if (lhs->pcDetailString != rhs->pcDetailString) {
		return stricmp(lhs->pcDetailString, rhs->pcDetailString);
	}

	if (lhs->pcInteractTexture != rhs->pcInteractTexture) {
		return stricmp(lhs->pcInteractTexture, rhs->pcInteractTexture);
	}

	if (lhs->pcCategory != rhs->pcCategory) {
		return stricmp(lhs->pcCategory, rhs->pcCategory);
	}

	if (lhs->iIndex != rhs->iIndex) {
		return rhs->iIndex - lhs->iIndex;
	}

	if (lhs->iTeammateID != rhs->iTeammateID) {
		return rhs->iTeammateID - lhs->iTeammateID;
	}

	if (lhs->iTeammateType != rhs->iTeammateType) {
		return rhs->iTeammateType - lhs->iTeammateType;
	}

	if (lhs->bDisabled && !rhs->bDisabled) {
		return -1;
	}
	if (!lhs->bDisabled && rhs->bDisabled) {
		return 1;
	}

	if (lhs->bAutoExecute && !rhs->bAutoExecute) {
		return -1;
	}
	if (!lhs->bAutoExecute && rhs->bAutoExecute) {
		return 1;
	}

	lNode = GET_REF(lhs->hNode);
	rNode = GET_REF(rhs->hNode);

	if (lNode > rNode) {
		return -1;
	}
	return lNode < rNode;
}


void interaction_OncePerFrameScanTick(Entity *pEnt)
{
	static U32 iMask;
	static GameInteractable **s_ppInteractables=NULL;

	Entity **ppEnts=NULL;
	F32 fDist=0;
	int i,j;
	Vec3 vPos;
	bool bDirty = false;
	bool bMoved;
	
	if (!pEnt->pPlayer || !pEnt->pChar || mapState_IsMapPausedForPartition(entGetPartitionIdx(pEnt))) {
		return;
	}
	
	PERFINFO_AUTO_START("CheckPlayerPosition", 1);

	// Get player's position and track if player has moved
	entGetPos(pEnt, vPos);
	bMoved = !sameVec3(vPos, pEnt->pPlayer->InteractStatus.vLastInteractTargetPos);
	copyVec3(vPos, pEnt->pPlayer->InteractStatus.vLastInteractTestPos);

	PERFINFO_AUTO_STOP(); // CheckPlayerPosition

	PERFINFO_AUTO_START("CheckPlayerCachedDoorDest", 1);

	// Cleanup player's team fallback door entry if necessary
	if(team_IsMember(pEnt)) {
		Team* pTeam = team_GetTeam(pEnt);
		U32 uiCurrentTime = timeSecondsSince2000();
		if(pTeam && pTeam->pCachedDestination && (pTeam->pCachedDestination->uiExpireTime < uiCurrentTime)) {
			StructDestroySafe(parse_CachedDoorDestination, &pTeam->pCachedDestination);
		}
	}

	PERFINFO_AUTO_STOP(); // CheckPlayerCachedDoorDest

	// Initialize mask if necessary
	if (!iMask) {
		// Note that NamedObject needs to be on this list or else mission overrides on
		// interaction properties don't work properly
		iMask = wlInteractionClassNameToBitMask("Clickable") | wlInteractionClassNameToBitMask("Destructible") |
				wlInteractionClassNameToBitMask("Contact") | wlInteractionClassNameToBitMask("CraftingStation") |
				wlInteractionClassNameToBitMask("Door") | wlInteractionClassNameToBitMask("FromDefinition") | 
				wlInteractionClassNameToBitMask("NamedObject");
	}


	// Find all the interaction nodes the player can target right now.
	// Some games make these glow, others use reticles, and so on, but this is the
	// list of interactables the player is allowed to know about
	// If counter is -1 coming in (which means do it now), then the value will be zero after incrementing
	++pEnt->pPlayer->InteractStatus.interactTargetCounter; 
	if (!pEnt->pPlayer->InteractStatus.interactTargetCounter || 
		( pEnt->pPlayer->InteractStatus.interactTargetCounter >= INTERACT_MAX_TARGET_TEST_COUNT_NO_MOVE ) ||
		( (pEnt->pPlayer->InteractStatus.interactTargetCounter >= INTERACT_MAX_TARGET_TEST_COUNT) && bMoved) 
		) {
		pEnt->pPlayer->InteractStatus.interactTargetCounter = 0;

		PERFINFO_AUTO_START("Update Targetable Nodes", 1);
		interaction_UpdateTargetableNodes(pEnt, iMask);
		PERFINFO_AUTO_STOP();
	}


	// If the player is in stasis, stuck, can only affect self, is held, disabled, or holding an object
	// No interaction allowed in these states.
	if (pEnt->pPlayer->iStasis || 
		pEnt->pPlayer->bStuckRespawn ||
		pEnt->pPlayer->bMapTransferPending ||
		(pEnt->pChar->pattrBasic->fOnlyAffectSelf > 0) ||
		character_IsHeld(pEnt->pChar) ||
		(pEnt->pChar->pattrBasic->fDisable > 0.0f)
		) {
		PERFINFO_AUTO_START("PlayerCantInteract", 1);
		// Clear all interact options other than current dialog
		interaction_ClearInteraction(pEnt, true, true, false);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (interaction_IsPlayerInteracting(pEnt) || 
		(!gConf.bAllowInteractWhileContactDialogOpen && interaction_IsPlayerInDialog(pEnt))
		) {
		MultiVal mvNewVal = {0};
		MultiVal mvOrigVal = {0};
		F32 fInteractMeter;

		PERFINFO_AUTO_START("CancelInteractDueToDialog", 1);

		// Clear interact options other than dialog
		interaction_ClearInteraction(pEnt, true, true, false);

		// Set a UI var on the player to track interact time (if any)
		if(pEnt->pPlayer->InteractStatus.fTimerInteractMax > 0.f){
			fInteractMeter = pEnt->pPlayer->InteractStatus.fTimerInteract / pEnt->pPlayer->InteractStatus.fTimerInteractMax;
		}else{
			fInteractMeter = 0.f;
		}
		entGetUIVar(pEnt, "Interact", &mvOrigVal);
		if (MultiValGetFloat(&mvOrigVal, NULL) != fInteractMeter) {
			MultiValSetFloat(&mvNewVal, fInteractMeter);
			entSetUIVar(pEnt, "Interact", &mvNewVal);
			entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pEnt->pPlayer->InteractStatus, true);
		}

		// We allow the team spokesman to be out of range. Here is why:
		// The team critical dialog might be initiated by someone else thus this player was initially out of range.
		// When the teamspokesman status is passed to this player we don't want them to be kicked out of the dialog.
		if (!interaction_IsPlayerInDialogAndTeamSpokesman(pEnt))
		{
			// if the interact use time is over, the interact motion cannot be canceled by being out of range
			if (pEnt->pPlayer->InteractStatus.fTimerInteract > 0.0f)
			{
				// Check for player leaving interact range
				if (!interaction_IsPlayerInInteractRange(pEnt)) 
				{
					interaction_EndInteractionAndDialog(entGetPartitionIdx(pEnt), pEnt, false, true, true);
				}
			}

			if (pEnt->pPlayer->InteractStatus.interactTarget.entRef)
			{
				if (entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->pPlayer->InteractStatus.interactTarget.entRef) == NULL)
				{
					// The thing we are interacting with has disappeared.  (e.g., a body we are looting)
					interaction_EndInteractionAndDialog(entGetPartitionIdx(pEnt), pEnt, false, true, true);
				}
			}
		}

		// If player is in interaction state 
		// No other interaction is allowed until it ends
		PERFINFO_AUTO_STOP();
		return;
	}

	// If player has interacted recently
	// No other interaction is allowed until timer expires
	if (pEnt->pPlayer->InteractStatus.fTimeUntilNextInteract > 0 || pEnt->pChar->bUsingDoor) {
		PERFINFO_AUTO_START("PlayerNoInteractTimer", 1);
		// Clear interact options but not dialog or pickup target
		interaction_ClearInteraction(pEnt, true, false, false);
		PERFINFO_AUTO_STOP();
		return;
	}


	// If we get here, then player is allowed to interact


	// Find all the interaction nodes, entities, and volumes the player can interact with right now.
	// We do this infrequently to reduce performance impact
	// If counter is -1 coming in (which means do it now), then the value will be zero after incrementing
	++pEnt->pPlayer->InteractStatus.interactCheckCounter; 
	if (!pEnt->pPlayer->InteractStatus.interactCheckCounter ||
		( pEnt->pPlayer->InteractStatus.interactCheckCounter % INTERACT_MAX_TEST_COUNT_NO_MOVE == 0 ) ||
		( (pEnt->pPlayer->InteractStatus.interactCheckCounter % INTERACT_MAX_TEST_COUNT == 0) && bMoved) 
		) {
		static InteractOption **eaOldOptions = NULL;
		U32 eOldContactTypes;
		int iPartitionIdx = entGetPartitionIdx(pEnt);

		PERFINFO_AUTO_START("InteractCheckSetup", 1);

		// Clear the interact check counter
		pEnt->pPlayer->InteractStatus.interactCheckCounter = 0;

		// Clear previous interact state, but keep copy localy for dirty bit comparison
		eaCopy(&eaOldOptions, &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions);
		eaClearFast(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions);
		eaClearStruct(&pEnt->pPlayer->InteractStatus.eaOverrideEntries, parse_WorldInteractionPropertyEntry);
		eOldContactTypes = pEnt->pPlayer->InteractStatus.eNearbyContactTypes;
		pEnt->pPlayer->InteractStatus.eNearbyContactTypes = 0;

		if ( pEnt->pPlayer->InteractStatus.bResendInteractLists ) {
			//if we don't resend the nodes list to the client the first frame after "initmap",
			//somehow the client doesn't get valid nodes for any nodes that were in interaction 
			//range of when the initmap happened - this corrects that issue
			pEnt->pPlayer->InteractStatus.bResendInteractLists = false;

			gslEntityForceFullSend(pEnt);
		}

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("FindInteractNodes", 1);

		// Determine nodes the player can interact with
		if (interaction_FindInteractionNodesWithCheck(pEnt, iMask, interactable_GetNodeInteractMaxRange(pEnt), false, interactable_IsNotDestructibleOrCanThrowObject, NULL, &s_ppInteractables)) {
			// Push all possible nodes into the player interact targets
			for(i=eaSize(&s_ppInteractables)-1; i>=0; i--) {
				interaction_GetValidInteractNodeOptions(s_ppInteractables[i], pEnt, &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions, true, !gConf.bDestructibleInteractOption);
			}
		}

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("FindInteractEnts", 1);

		// Determine entities the player can interact with
		if (interaction_FindInteractionEnts(pEnt,&ppEnts)) {
			// Push all possible ents into the player interact targets
			for(i=eaSize(&ppEnts)-1; i>=0; i--) {
				interaction_GetValidInteractEntOptions(iPartitionIdx, ppEnts[i], pEnt, &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions, true);
			}

			eaDestroy(&ppEnts);
		}

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("FindInteractVolumes", 1);

		// Determine volumes the player can interact with
		if (eaSize(&pEnt->pPlayer->InteractStatus.eaInVolumes)) {
			for(i=eaSize(&pEnt->pPlayer->InteractStatus.eaInVolumes)-1; i>=0; --i) {
				volume_AddInteractOptions(iPartitionIdx, pEnt->pPlayer->InteractStatus.eaInVolumes[i], NULL, pEnt, &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions);
			}
		}

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("CheckQueuedInteractions", 1);

		// Handle QueueDef interactions
		if(eaSize(&pEnt->pPlayer->InteractStatus.ppRecentQueueInteractions)) {
			U32 iCurrentTime = timeSecondsSince2000();
			for(i=eaSize(&pEnt->pPlayer->InteractStatus.ppRecentQueueInteractions)-1; i>=0; --i) {
				InteractedQueueDef *pQueue = pEnt->pPlayer->InteractStatus.ppRecentQueueInteractions[i];
				if(pQueue->iInteractTime + RECENT_QUEUE_DURATION < iCurrentTime) {
					StructDestroy(parse_InteractedQueueDef, eaRemove(&pEnt->pPlayer->InteractStatus.ppRecentQueueInteractions, i));
					bDirty = true;
				}
			}
		}

		PERFINFO_AUTO_STOP();
		
		// Add option to throw a held object (if necessary)
		if (IS_HANDLE_ACTIVE(pEnt->pChar->hHeldNode)) {
			char *estrBuffer = NULL;
			int iLanguage = entGetLanguage(pEnt);
			InteractOption *pOption;

			PERFINFO_AUTO_START("AddThrowObjectOption", 1);

			pOption = StructCreate(parse_InteractOption);
			pOption->bCanThrow = true;
			langFormatMessageKey( iLanguage, &estrBuffer, "MechanicsUI.ThrowObject", STRFMT_END);
			pOption->pcInteractString = StructAllocString(estrBuffer);
			estrDestroy(&estrBuffer);

			eaPush(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions, pOption);

			PERFINFO_AUTO_STOP();
		}

		PERFINFO_AUTO_START("SortOptions", 1);

		// Sort interact options for reliable comparison
		eaQSort(pEnt->pPlayer->InteractStatus.interactOptions.eaOptions, cmpInteractOption);
		
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("CheckIfChanged", 1);

		// Check whether dirty bit needs to get set by comparing old and new data
		if (!bDirty) {
			if (pEnt->pPlayer->InteractStatus.eNearbyContactTypes != eOldContactTypes) {
				bDirty = true;
			} else if (eaSize(&eaOldOptions) != eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions)) {
				bDirty = true;
			} else {
				for(i=eaSize(&eaOldOptions)-1; i>=0; --i) {
					if (cmpInteractOption(&eaOldOptions[i], &pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[i])) {
						bDirty = true;
						break;
					}
				}
			}
		}

		PERFINFO_AUTO_STOP();

		// Only check for auto-execute when dirty since should only happen when a new option
		// appears.  We can skip this check if nothing has changed.
		if (bDirty) {
			PERFINFO_AUTO_START("CheckForAutoExecute", 1);

			// If interactions present, check for auto-execute behavior
			for(i=eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions)-1; i>=0; --i) {
				InteractOption *pOption;
				InteractOption *pRecentOption;

				if (i >= eaSize(&pEnt->pPlayer->InteractStatus.interactOptions.eaOptions)) {
					continue;
				}
				pOption = pEnt->pPlayer->InteractStatus.interactOptions.eaOptions[i];

				if (pOption->bAutoExecute && !pOption->bDisabled) {
					// Wants to auto-execute, so see if already is in executed list
					for(j=eaSize(&pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions)-1; j>=0; --j) {
						pRecentOption = pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions[j];
						if ((pRecentOption->entRef == pOption->entRef) &&
							(GET_REF(pRecentOption->hNode) == GET_REF(pOption->hNode)) &&
							(pRecentOption->pcVolumeName == pOption->pcVolumeName) && // Can do since both are pooled
							(pRecentOption->iIndex == pOption->iIndex) &&
							(pRecentOption->iTeammateType == pOption->iTeammateType) &&
							(pRecentOption->iTeammateID == pOption->iTeammateID)
							) {
							break;
						}
					}
					if (j < 0) {
						// Not recently auto-executed, so add to list of recently executed
						pRecentOption = StructClone(parse_InteractOption, pOption);
						eaPush(&pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions, pRecentOption);

						// Perform the interact now
						interaction_PerformInteract(pEnt, pOption->entRef, REF_STRING_FROM_HANDLE(pOption->hNode), pOption->pcVolumeName, pOption->iIndex, pOption->iTeammateType, pOption->iTeammateID, true);
					}
				}
			}

			// Trim down auto-execute list if it's too big
			while(eaSize(&pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions) > MAX_RECENT_AUTO_EXECUTES) {
				StructDestroy(parse_InteractOption, pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions[0]);
				eaRemove(&pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions, 0);
			}

			PERFINFO_AUTO_STOP();
		}

		eaClearStruct(&eaOldOptions,parse_InteractOption);
	}

	if ( bDirty ) {
		entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pEnt->pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}


void interaction_OncePerFrameInteractTick(Entity *pEnt, F32 fElapsed)
{
	Entity *pCurrTarget = entity_GetTarget(pEnt);
	Critter *pInteractCritter = NULL;
	Player* pPlayer;
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	// Count down time until player's interaction is complete, or until the character is allowed to try to interact again
	if ( !pEnt->pPlayer || mapState_IsMapPausedForPartition(iPartitionIdx)) {
		return;
	}

	pPlayer = pEnt->pPlayer;

	if ( interaction_IsPlayerInteracting(pEnt) || interaction_IsPlayerInDialog(pEnt) ) {
		Vec3 pos;
		PERFINFO_AUTO_START("CheckIfShouldEndInteract", 1);

		entGetPos(pEnt, pos);
		if(pPlayer->InteractStatus.bInteractBreakOnMove && (pPlayer->InteractStatus.bMovedSinceInteractTick  || distance3Squared(pos, pPlayer->InteractStatus.interactStartPos) > INTERACT_BREAKONMOVE_DIST_SQ)) {
			interaction_EndInteractionAndDialog(iPartitionIdx, pEnt, false, true, true);
		}

		PERFINFO_AUTO_STOP();
	}

	if ( interaction_IsPlayerInteracting(pEnt) ) {
		PERFINFO_AUTO_START("CheckInteractTimer", 1);

		if (pPlayer->InteractStatus.fTimerInteract > 0) {
			pPlayer->InteractStatus.fTimerInteract -= fElapsed;
			if( pPlayer->InteractStatus.fTimerInteract <= 0 && pPlayer->InteractStatus.fTimerInteract+fElapsed >= 0) {
				interaction_EvalInteract(pEnt);
			}
		}
		PERFINFO_AUTO_STOP(); // CheckInteractTimer

		if (pPlayer->InteractStatus.interactTarget.bLoot) {
			WorldInteractionNode *pTargetNode;
			Entity *pTargetEnt;

			PERFINFO_AUTO_START("Looting", 1);

			pTargetNode = GET_REF(pPlayer->InteractStatus.interactTarget.hInteractionNode);
			pTargetEnt = entFromEntityRef(iPartitionIdx, pPlayer->InteractStatus.interactTarget.entRef);

			if(pPlayer->InteractStatus.fTimeUntilNextInteract > 0) {
				pPlayer->InteractStatus.fTimeUntilNextInteract -= fElapsed;
				if (pPlayer->InteractStatus.fTimeUntilNextInteract <= 0) {
					pPlayer->InteractStatus.fTimeUntilNextInteract = 0;
					pPlayer->InteractStatus.interactCheckCounter = -1; // Force immediate interact check
				}
			}

			// if player no longer owns the loot, end interaction
			if (pTargetEnt) {
				if (interaction_IsLootEntity(pTargetEnt) && !reward_MyDrop(pEnt, pTargetEnt)) {
					interaction_DoneInteracting(iPartitionIdx, pEnt, false, false);
				} else if (SAFE_MEMBER(pTargetEnt->pCritter, encounterData.pLootTracker) && !LootTracker_CanEntityLoot(pTargetEnt->pCritter->encounterData.pLootTracker, pEnt)) {
					interaction_DoneInteracting(iPartitionIdx, pEnt, false, false);
				}
			} else if (pTargetNode) {
				GameInteractable *pTargetInteractable = interactable_GetByNode(pTargetNode);
				InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pTargetInteractable, false);
				if (pTracker && !LootTracker_CanEntityLoot(pTracker, pEnt)) {
					interaction_DoneInteracting(iPartitionIdx, pEnt, false, false);
				}
			} else if (pTargetEnt && pTargetEnt->pCritter && pTargetEnt->pCritter->StartingTimeToLinger > 0) {
				// ensure loot expiration starts AFTER player ends interaction with it and expiration
				// never gets hit during interaction
				pTargetEnt->pCritter->timeToLinger = pTargetEnt->pCritter->StartingTimeToLinger;
			}

			PERFINFO_AUTO_STOP(); // Looting;
		}
	} else if(pPlayer->InteractStatus.fTimeUntilNextInteract > 0) {
		pPlayer->InteractStatus.fTimeUntilNextInteract -= fElapsed;
		if (pPlayer->InteractStatus.fTimeUntilNextInteract <= 0) {
			pPlayer->InteractStatus.fTimeUntilNextInteract = 0;
			pPlayer->InteractStatus.interactCheckCounter = -1; // Force immediate interact check
		}
	}

	pPlayer->InteractStatus.bMovedSinceInteractTick = false;
}

// ----------------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------------

AUTO_STARTUP(Interaction) ASTRT_DEPS(AS_Messages, AnimLists);
void interactionsystem_Init(void)
{
	interactiondef_LoadDefs();
}
