/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "Character_Combat.h"
#include "DamageTracker.h"
#include "EntityGrid.h"
#include "EntityIterator.h"
#include "EntityMovementDoor.h"
#include "EntityMovementInteraction.h"
#include "EntityMovementManager.h"
#include "EntityServer.h"
#include "Expression.h"
#include "gslCostume.h"
#include "gslCritter.h"
#include "gslEntity.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslInteractionManager.h"
#include "gslMechanics.h"
#include "gslVolume.h"
#include "interaction_common.h"
#include "InteractionManager_common.h"
#include "mapstate_common.h"
#include "PowerApplication.h"
#include "rand.h"
#include "wlEncounter.h"
#include "WorldGrid.h"


// ----------------------------------------------------------------------------------
// Type Defintions
// ----------------------------------------------------------------------------------

#define FADEIN_TIME 1.5f

#define DBGINTERACTION_printf(format, ...) if(g_bInteractionDebug) printf(format, __VA_ARGS__)

typedef struct SingleRespawnTimer
{
	int iPartitionIdx;
	REF_TO(WorldInteractionNode) hNode;
	EntityRef eEntity;
	F32 fTimeRemaining;
	F32 fTimeOriginal; 
	bool bCleanup;
} SingleRespawnTimer;

typedef enum SingleMotionStatus
{
	kTimerStatus_None = 0,
	kTimerStatus_MotionOpening,				// in transition to destination
	kTimerStatus_MotionOpen,				// at destination
	kTimerStatus_MotionClosing,				// returning to start point
	kTimerStatus_MotionClosed,				// at start point
} SingleMotionStatus;

typedef struct MotionTarget
{
	GameInteractable *pStartInteractable;
	GameInteractable *pDestInteractable;
	EntityRef eEntity;
	Vec3 vDestPos;
	Quat qDestRot;
} MotionTarget;

typedef struct MotionTracker
{
	int iPartitionIdx;

	WorldInteractionNode *pNode;
	SingleMotionStatus eStatus;
	int iInitiatingEntryIndex;

	// moving parts
	MotionTarget **eaTargets;

	// timer data
	F32 fTimeRemaining;
	F32 fTimeOriginal;
	F32 fPercentRemaining;

	// flags
	int bIsGate : 1;
	int bTransDuringUse : 1;

	// queue for using the interactable
	EntityRef *eEntityQueue;
	int *iEntityQueueInteractIndex;
} MotionTracker;

typedef struct InteractionPathingData {
	Entity*			e;
	char*			keyName;
	EntityRef		erTarget;
	S32				index;
	GlobalType		teammateType;
	ContainerID		teammateID;
} InteractionPathingData;


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

bool g_bInteractionDebug = 0;

static SingleRespawnTimer** s_eaRespawnTimers;
static SingleRespawnTimer** s_eaEntityTimers;
EntityRef *perEntObjects;

static F32 ConvertTime  = 10.0f;

static MotionTracker **s_eaMotionTrackers;


// ----------------------------------------------------------------------------------
// Chained Destructible Logic
// ----------------------------------------------------------------------------------

void im_DestroyedChainedDestructible( EntityRef erKiller, Entity *pTarget )
{
	Character *pChar = pTarget->pChar;
	
	//add some damage so this entity gets credit for the kill
	F32 fDamage = pChar->pattrBasic->fHitPoints;

	DamageTracker *pTracker = damageTracker_AddTick(entGetPartitionIdx(pTarget),
													pChar,
													erKiller,
													erKiller,
													entGetRef(pTarget),
													fDamage,
													fDamage,
													kAttribType_HitPoints,
													0,NULL,0,NULL,0);

	pChar->bKill = true;
	character_Wake(pChar);
}


void im_DestroyChainedDestructibles_QueryNodes(int iPartitionIdx, WorldInteractionNode* pNode, Vec3 vMin, Vec3 vMax, EntityRef erKiller )
{
	static U32 iMaskDes;
	int i;
	WorldInteractionNode** eaNodes = NULL;
	
	Vec3 vBoxCenter;
	F32 fRadius, fExtentX, fExtentZ;

	PERFINFO_AUTO_START_FUNC();

	if ( !iMaskDes ) {
		iMaskDes = wlInteractionClassNameToBitMask("Destructible");
	}

	addVec3( vMin, vMax, vBoxCenter );
	scaleByVec3( vBoxCenter, 0.5f );

	fExtentX = vMax[0]-vBoxCenter[0];
	fExtentZ = vMax[2]-vBoxCenter[2];

	fRadius = MAX( fExtentX, fExtentZ );
	
	wlInteractionQuerySphere(iPartitionIdx,iMaskDes,NULL,vBoxCenter,fRadius,false,false,true,&eaNodes);

	for ( i = 0; i < eaSize( &eaNodes ); i++ ) {
		Vec3 vNodeMin, vNodeMax;

		if ( pNode == eaNodes[i] ) {
			continue;
		}

		wlInteractionNodeGetWorldMin(eaNodes[i],vNodeMin);
		wlInteractionNodeGetWorldMax(eaNodes[i],vNodeMax);

		if ( boxBoxCollision( vMin, vMax, vNodeMin, vNodeMax ) && (vNodeMin[1]+vNodeMax[1])*0.5f > vMin[1] ) {
			Entity* pEnt = im_InteractionNodeToEntity(iPartitionIdx, eaNodes[i] );

			im_DestroyedChainedDestructible( erKiller, pEnt );
		}
	}

	PERFINFO_AUTO_STOP();
}


void im_DestroyChainedDestructibles_QueryEntities( int iPartitionIdx, Entity* pEnt, Vec3 vMin, Vec3 vMax, EntityRef erKiller )
{
	Entity *pCurrEnt;
	EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW, GLOBALTYPE_ENTITYCRITTER);

	PERFINFO_AUTO_START_FUNC();

	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		WorldInteractionNode *pEntNode;
		Vec3 vNodeMin, vNodeMax;

		if ( pCurrEnt == pEnt ) {
			continue;
		}

		if ( !IS_HANDLE_ACTIVE(pCurrEnt->hCreatorNode) ) {
			continue;
		}

		if ( !pCurrEnt->pChar ) {
			continue;
		}

		pEntNode = GET_REF(pCurrEnt->hCreatorNode);

		if ( !pEntNode ) {
			continue;
		}

		wlInteractionNodeGetWorldMin(pEntNode,vNodeMin);
		wlInteractionNodeGetWorldMax(pEntNode,vNodeMax);

		if ( boxBoxCollision( vMin, vMax, vNodeMin, vNodeMax ) && (vNodeMin[1]+vNodeMax[1])*0.5f > vMin[1] ) {
			im_DestroyedChainedDestructible( erKiller, pCurrEnt );
		}
	}

	EntityIteratorRelease(pIter);

	PERFINFO_AUTO_STOP();
}


void im_DestroyChainedDestructibles( Entity* pEnt, WorldInteractionNode* pNode, EntityRef erKiller )
{
	F32 fExtrudeHeight = 1.5f; //how far to extrude bounds upwards
	int iPartitionIdx;
	
	Vec3 vMin, vMax;

	if ( pEnt==NULL ) {
		return;
	}

	if ( pNode==NULL ) {
		pNode = GET_REF(pEnt->hCreatorNode);
		if ( pNode==NULL ) {
			return;
		}
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);

	PERFINFO_AUTO_START_FUNC();

	wlInteractionNodeGetWorldMin(pNode,vMin);
	wlInteractionNodeGetWorldMax(pNode,vMax);

	vMin[1] = vMax[1];
	vMax[1] += fExtrudeHeight;

	im_DestroyChainedDestructibles_QueryEntities( iPartitionIdx, pEnt, vMin, vMax, erKiller );
	im_DestroyChainedDestructibles_QueryNodes( iPartitionIdx, pNode, vMin, vMax, erKiller );

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Destructible Logic
// ----------------------------------------------------------------------------------

void im_onDeath(Entity *pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	WorldInteractionNode *pNode = GET_REF(pEnt->hCreatorNode);
	Entity *eKiller = character_FindKiller(iPartitionIdx, pEnt->pChar,NULL);

	if (pNode && eKiller) {
		EntityRef erKiller = entGetRef(eKiller);
		CharacterClass *pClass = im_GetCharacterClass(pNode);
		S32 iLevel = im_GetLevel(pNode);

		//find "chained" destructible objects
		im_DestroyChainedDestructibles( pEnt, pNode, erKiller );

		if (pClass) {
			ApplyUnownedPowerDefParams applyParams = {0};
			PowerDef **eaDefs = NULL;
			int i,s;

			applyParams.erTarget =  entGetRef(pEnt);
			applyParams.pcharSourceTargetType = eKiller->pChar;
			applyParams.pclass = pClass;
			applyParams.iLevel = iLevel;
			applyParams.fTableScale = 1.f;
			applyParams.erModOwner = entGetRef(eKiller);

			if (0!=(s=im_GetDeathPowerDefs(pNode, &eaDefs))) {

				// Swap who is holding to the killer, so during targeting it thinks it's the killer's friend
				EntityRef erHeldByOld = 0;
				if (pEnt->pChar) {
					// doesn't actually change pEnt->pChar as far as networking's concerned
					// so no dirty bit
					erHeldByOld = pEnt->pChar->erHeldBy;
					pEnt->pChar->erHeldBy = erKiller;
				}
				
				// Apply the Powers
				for(i=0; i<s; i++) {
					applyParams.uiApplyID = powerapp_NextID();
					applyParams.fHue = powerapp_GetHue(NULL,NULL,NULL,eaDefs[i]);
					character_ApplyUnownedPowerDef(iPartitionIdx, pEnt->pChar, eaDefs[i], &applyParams);
					
				}

				// Swap back, just in case
				if (pEnt->pChar) {
					pEnt->pChar->erHeldBy = erHeldByOld;
				}
			}
			eaDestroy(&eaDefs);
		}
	}
}


void im_EntityDestroyed(Entity *pEnt)
{
	F32 fRespawnTime;
	int iEntPos;
	WorldInteractionEntry *pEntry = NULL;
	WorldInteractionEntry *pParentEntry = NULL;
	WorldInteractionNode *pNode = GET_REF(pEnt->hCreatorNode);
	GameInteractable *pInteractable;
	GameInteractable *pParentInteractable = NULL;
	int iPartitionIdx;

	if (!pEnt || !IS_HANDLE_ACTIVE(pEnt->hCreatorNode)) {
		return;
	}

	if (!pNode) {
		return;
	}

	pEntry = wlInteractionNodeGetEntry(pNode);
	if (!pEntry) {
		return;
	}

	pInteractable = interactable_GetByEntry(pEntry);
	if (!pInteractable) {
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);
	if (!interactable_PartitionExists(iPartitionIdx, pInteractable)) {
		// This happens if entity is destroyed after partition shutdown starts
		return;
	}

	if(pEntry->base_entry_data.parent_entry) {
		pParentEntry = pEntry->base_entry_data.parent_entry;
		pParentInteractable = interactable_GetByEntry(pParentEntry);
	}

	fRespawnTime = pEntry->full_interaction_properties ? wlInteractionGetDestructibleRespawnTime(pEntry->full_interaction_properties) : 0;

	if (fRespawnTime > 0.0f) {
		SingleRespawnTimer *pTimer = calloc(1, sizeof(SingleRespawnTimer));
		pTimer->iPartitionIdx = iPartitionIdx;
		pTimer->fTimeOriginal = fRespawnTime;
		pTimer->fTimeRemaining = fRespawnTime;
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pTimer->hNode);

		eaPush(&s_eaRespawnTimers,pTimer);

		if (g_bInteractionDebug) {
			Vec3 ventPos;

			entSetPos(pEnt,ventPos, 1, __FUNCTION__);
			DBGINTERACTION_printf("IM: Ent destroyed at location %f %f %f\nRespawn timer for node %s set to %f seconds\n",ventPos[0],ventPos[1],ventPos[2],REF_STRING_FROM_HANDLE(pTimer->hNode),pTimer->fTimeOriginal);
		}

	} else if (g_bInteractionDebug) {
		Vec3 ventPos;

		entGetPos(pEnt,ventPos);
		DBGINTERACTION_printf("IM: Ent destroyed at location %f %f %f\nNo respawn timer set for node %s\n",ventPos[0],ventPos[1],ventPos[2],REF_STRING_FROM_HANDLE(pEnt->hCreatorNode));
	}
	iEntPos = ea32Find(&perEntObjects,pEnt->myRef);

	//Set dead object
	
	if (entCheckFlag(pEnt,ENTITYFLAG_DEAD)) { //Make sure the ent is actually dead

		if (pEntry->visible_child_count > 1) {
			//Show the death state of the object
			interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_SHOW, 0, false);
			interactable_SetVisibleChild(iPartitionIdx, pInteractable, 1, false);
			interactable_SetDisabledState(iPartitionIdx, pInteractable, true);
		} else {
			interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_HIDE, 0, false);
		}

		if(pParentInteractable) {
			if (pParentEntry->visible_child_count > 1) {
				interactable_SetHideState(iPartitionIdx, pParentInteractable, I_STATE_SHOW, 0, false);
				interactable_SetVisibleChild(iPartitionIdx, pParentInteractable, 1, false);
			} else {
				interactable_SetHideState(iPartitionIdx, pParentInteractable, I_STATE_HIDE, 0, false);
			}
		}
	}
	
	if (iEntPos > -1) {
		ea32RemoveFast(&perEntObjects,iEntPos);
	}
}


Entity *im_FindCritterforObject(int iPartitionIdx, const char *pchName)
{
	int i;
	Entity *pEntRtn = NULL;
	WorldInteractionNode *pNode;

	if (!pchName) {
		return NULL;
	}

	pNode = RefSystem_ReferentFromString(INTERACTION_DICTIONARY, pchName);
	if (!pNode) {
		return NULL;
	}

	for(i=ea32Size(&perEntObjects)-1;i>=0;i--) {
		pEntRtn = entFromEntityRef(iPartitionIdx, perEntObjects[i]);

		if (pEntRtn && GET_REF(pEntRtn->hCreatorNode) == pNode) {
			break;
		}
	}

	if (i == -1) {
		return NULL;
	} else {
		return pEntRtn;
	}
}


Entity *im_InteractionNodeToEntity(int iPartitionIdx, WorldInteractionNode *pNode)
{
	Entity *pNewEnt = NULL;
	WorldInteractionEntry *pEntry = NULL;
	GameInteractable *pInteractable;
	CritterDef *pCritterDef;
	CritterOverrideDef *pCritterOverrideDef;
	U32 iCritterlevel;

	assertmsgf(pNode,"Interaction Manager requesting touch on invalid node");

	PERFINFO_AUTO_START_FUNC();

	//Find if this object has already been turned into a critter

	pNewEnt = im_FindCritterforObject(iPartitionIdx, wlInteractionNodeGetKey(pNode));

	if (pNewEnt) {
		PERFINFO_AUTO_STOP();
		return pNewEnt;
	}

	pEntry = wlInteractionNodeGetEntry(pNode);
	if (!pEntry) {
		PERFINFO_AUTO_STOP();
		return NULL;
	}
	pInteractable = interactable_GetByEntry(pEntry);
	if (!pInteractable) {
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if (interactable_IsHidden(iPartitionIdx, pInteractable)) {
		Alertf("Attempting to hide node %s [%s] while spawning an entity from the node, but the node is already hidden!\n",wlInteractionNodeGetDisplayName(pNode), wlInteractionNodeGetKey(pNode));
	}

	pCritterDef = wlInteractionGetDestructibleCritterDef(pEntry->full_interaction_properties);
	pCritterOverrideDef = wlInteractionGetDestructibleCritterOverride(pEntry->full_interaction_properties);
	iCritterlevel = wlInteractionGetDestructibleCritterLevel(pEntry->full_interaction_properties);
	
	pNewEnt = critter_Create(SAFE_MEMBER(pCritterDef,pchName),SAFE_MEMBER(pCritterOverrideDef,pchName),GLOBALTYPE_ENTITYCRITTER,iPartitionIdx,NULL,iCritterlevel ? iCritterlevel : 1,1,0,0,0,wlInteractionGetDestructibleDisplayName(pEntry->full_interaction_properties),0,0,0,NULL,pNode);

	if (pNewEnt) {
		Mat4 mPos;
		Quat qRot;

		SingleRespawnTimer *pTimer = calloc(1, sizeof(SingleRespawnTimer));
		pTimer->iPartitionIdx = iPartitionIdx;
		pTimer->fTimeOriginal = ConvertTime;
		pTimer->fTimeRemaining = ConvertTime;
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pTimer->hNode);
		pTimer->eEntity = entGetRef(pNewEnt);
		pTimer->bCleanup = wlInteractionGetDestructibleRespawnTime(pEntry->full_interaction_properties) > 0;

		eaPush(&s_eaEntityTimers,pTimer);

		interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_HIDE, entGetRef(pNewEnt), false);
		
		// Destroy default requester so it can't ever move.
		gslEntMovementDestroySurfaceRequester(pNewEnt);

		// Set position
		copyMat4(wlInteractionNodeGetEntry(pNode)->base_entry.bounds.world_matrix,mPos);
		wlInteractionNodeGetRot(pNode,qRot);
		entSetPos(pNewEnt, mPos[3], 1, __FUNCTION__);
		entSetRot(pNewEnt, qRot, 1, __FUNCTION__);
		entity_SetDirtyBit(pNewEnt, parse_Entity, pNewEnt, false);

		// Set Costume
		costumeEntity_SetDestructibleObjectCostumeByName(pNewEnt,wlInteractionNodeGetKey(pNode));
		pNewEnt->fEntitySendDistance = pEntry->base_entry.shared_bounds->far_lod_far_dist;

		ea32Push(&perEntObjects,pNewEnt->myRef);

		if (g_bInteractionDebug) {
			Vec3 vPosEnt;

			entGetPos(pNewEnt,vPosEnt);
			DBGINTERACTION_printf("IM: Node %s touched, converting into ent %d\nNode Pos %f %f %f, Ent Pos %f %f %f\n",wlInteractionNodeGetKey(pNode),entGetRef(pNewEnt),mPos[3][0],mPos[3][1],mPos[3][2],vPosEnt[0],vPosEnt[1],vPosEnt[2]);
		}

		//Disable turn to face
		pNewEnt->pChar->bDisableFaceActivate = 1;
		entity_SetDirtyBit(pNewEnt, parse_Character, pNewEnt->pChar, false);

	} else if (pCritterDef) {
		Errorf("Unable to Create Critter: %s",pCritterDef->pchName);
	}

	PERFINFO_AUTO_STOP();
	return pNewEnt;
}


void im_ForceNodesToEntities(Entity* pSource, F32 fRange, const char *pcName, Entity ***peaEntsOut)
{
	RefDictIterator iterator;
	WorldInteractionNode *pNode;
	int iPartitionIdx = entGetPartitionIdx(pSource);

	if (peaEntsOut) {
		eaClear(peaEntsOut);
	}

	RefSystem_InitRefDictIterator(INTERACTION_DICTIONARY, &iterator);
	while (pNode = RefSystem_GetNextReferentFromIterator(&iterator)) {
		F32 dummyDist;
		Vec3 dummyPos;
		WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
		WorldInteractionProperties *pProperties = SAFE_MEMBER(pEntry, full_interaction_properties);
		GameInteractable *pInteractable = interactable_GetByNode(pNode);
		const char *entity_name = wlInteractionGetDestructibleEntityName(pProperties);

		if (entity_name && 
			stricmp(pcName, entity_name) == 0 &&
			interactable_IsSelectable(pInteractable) &&
			!interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable) &&
			(!fRange || !pSource || entity_IsNodeInRange(pSource, NULL, pNode, fRange, 0, 0, dummyPos, &dummyDist, true)))
		{
			Entity *e = im_InteractionNodeToEntity(iPartitionIdx, pNode);
			if (e && peaEntsOut) {
				eaPush(peaEntsOut, e);
			}
		}
	}
}


Entity *im_NodeToEnt(int iPartitionIdx, WorldInteractionNode *pNode, bool bWaitForEnt)
{
	Entity *e = gslCreateEntity(GLOBALTYPE_ENTITYCRITTER, iPartitionIdx);
	WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
	GameInteractable *pInteractable = interactable_GetByEntry(pEntry);

	if (e) {
		Mat4 mPos;
		Quat qRot;

		interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_HIDE, bWaitForEnt ? entGetRef(e) : 0, false);

		// Destroy default requester so it can't ever move.
		gslEntMovementDestroySurfaceRequester(e);

		// Set position
		copyMat4(pEntry->base_entry.bounds.world_matrix, mPos);
		wlInteractionNodeGetRot(pNode, qRot);
		entSetPos(e, mPos[3], 1, __FUNCTION__);
		entSetRot(e, qRot, 1, __FUNCTION__);
		SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, e->hCreatorNode);
		entity_SetDirtyBit(e, parse_Entity, e, false);

		// Set Costume
		costumeEntity_SetDestructibleObjectCostumeByName(e, wlInteractionNodeGetKey(pNode));
		e->fEntitySendDistance = pEntry->base_entry.shared_bounds->far_lod_far_dist;

		ea32Push(&perEntObjects,e->myRef);

		if (g_bInteractionDebug) {
			Vec3 vPosEnt;

			entGetPos(e,vPosEnt);
			DBGINTERACTION_printf("IM: Interactable %s requested to open, converting into ent %d\nNode Pos %f %f %f, Ent Pos %f %f %f\n", wlInteractionNodeGetKey(pNode), entGetRef(e),
				mPos[3][0], mPos[3][1], mPos[3][2], vPosEnt[0], vPosEnt[1], vPosEnt[2]);
		}

		entSetCodeFlagBits(e, ENTITYFLAG_IGNORE);
	}

	return e;
}


// ----------------------------------------------------------------------------------
// Respawn Timer Logic
// ----------------------------------------------------------------------------------

static void im_FreeSingleRespawnTimer(SA_PRE_NN_VALID SA_POST_P_FREE SingleRespawnTimer *pTimer)
{
	REMOVE_HANDLE(pTimer->hNode);
	free(pTimer);
}


void im_RemoveNodeFromTimers(int iPartitionIdx, SingleRespawnTimer **eaTimers,WorldInteractionNode *pNode)
{
	int i;

	for(i=eaSize(&eaTimers)-1; i>=0; i--) {
		SingleRespawnTimer *pTimer = eaTimers[i];
		if ((pTimer->iPartitionIdx == iPartitionIdx) && (GET_REF(pTimer->hNode) == pNode)) {
			eaRemove(&eaTimers,i);
			im_FreeSingleRespawnTimer(pTimer);
		}
	}
}


void im_RemoveNodeFromRespawnTimers(int iPartitionIdx, WorldInteractionNode *pNode)
{
	im_RemoveNodeFromTimers(iPartitionIdx, s_eaRespawnTimers, pNode);
}


void im_RemoveNodeFromEntityTimers(int iPartitionIdx, WorldInteractionNode *pNode)
{
	im_RemoveNodeFromTimers(iPartitionIdx, s_eaEntityTimers, pNode);
}

// remove timers in this partition
void interactionManager_PartitionUnload(int iPartitionIdx)
{
	S32 i;

	for(i = eaSize(&s_eaRespawnTimers) - 1; i>=0; --i)
	{
		SingleRespawnTimer *pTimer = s_eaRespawnTimers[i];
		if(pTimer && pTimer->iPartitionIdx == iPartitionIdx)
		{
			eaRemove(&s_eaRespawnTimers,i);
			im_FreeSingleRespawnTimer(pTimer);
		}
	}

	for(i = eaSize(&s_eaEntityTimers) - 1; i>=0; --i)
	{
		SingleRespawnTimer *pTimer = s_eaEntityTimers[i];
		if(pTimer && pTimer->iPartitionIdx == iPartitionIdx)
		{
			eaRemove(&s_eaEntityTimers,i);
			im_FreeSingleRespawnTimer(pTimer);
		}
	}
}

void im_InteractionTimerTick(F32 fRate)
{
	int i;

	for(i=eaSize(&s_eaRespawnTimers)-1; i>=0; i--) {
		SingleRespawnTimer *pTimer = s_eaRespawnTimers[i];

		// Skip if partition is paused
		if (mapState_IsMapPausedForPartition(pTimer->iPartitionIdx)) {
			continue;
		}

		pTimer->fTimeRemaining -= fRate;

		PERFINFO_AUTO_START("s_eaRespawnTimers[i]", 1);
		if (pTimer->fTimeRemaining <= 0.0f) {
			Vec3 vSource;
			WorldInteractionNode *pNode = GET_REF(pTimer->hNode);
			F32 fRadius = pNode ? wlInteractionNodeGetRadius(pNode) + 5 : 0;

			if (pNode) {
				WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
				WorldInteractionEntry *pParentEntry = pEntry->base_entry_data.parent_entry;
				GameInteractable *pInteractable = interactable_GetByEntry(pEntry);

				wlInteractionNodeGetWorldMid(pNode,vSource);

				if (pParentEntry) {
					GameInteractable *pParentInteractable = interactable_GetByEntry(pParentEntry);
					if (pParentInteractable) {
						if (pParentEntry->visible_child_count > 0) {
							interactable_SetVisibleChild(pTimer->iPartitionIdx, pParentInteractable, 0, false);
						}
						interactable_SetHideState(pTimer->iPartitionIdx, pParentInteractable, I_STATE_SHOW, 0, false);
					}
				}

				if (pEntry->visible_child_count > 0) {
					interactable_SetVisibleChild(pTimer->iPartitionIdx, pInteractable, 0, false);
				}
				interactable_SetDisabledState(pTimer->iPartitionIdx, pInteractable, false);
				interactable_SetHideState(pTimer->iPartitionIdx, pInteractable, I_STATE_SHOW, 0, false);

				DBGINTERACTION_printf("IM: Node %s respawn timer reached 0, showing node\n", REF_STRING_FROM_HANDLE(pTimer->hNode));
			}
			
			eaRemove(&s_eaRespawnTimers,i);
			im_FreeSingleRespawnTimer(pTimer);
		}
		PERFINFO_AUTO_STOP();
	}

	for(i=eaSize(&s_eaEntityTimers)-1; i>=0; i--) {
		SingleRespawnTimer *pTimer = s_eaEntityTimers[i];
		Entity *pEnt = entFromEntityRef(pTimer->iPartitionIdx, pTimer->eEntity);		
		static Entity **eaCloseEntities;

		PERFINFO_AUTO_START("s_eaEntityTimers[i]", 1);

		if (!pEnt) {
			eaRemove(&s_eaEntityTimers,i);
			im_FreeSingleRespawnTimer(pTimer);
			PERFINFO_AUTO_STOP();
			continue;
		}

		// Skip entity if partition is paused
		if (mapState_IsMapPausedForPartition(entGetPartitionIdx(pEnt))) {
			continue;
		}

		pTimer->fTimeRemaining -= fRate;

		if (pTimer->fTimeRemaining <= 0.0f) {
			WorldInteractionNode *pNode = GET_REF(pTimer->hNode);
			GameInteractable *pInteractable;
			Vec3 vSource;
			F32 fRadius;

			if (!pNode) {
				eaRemove(&s_eaEntityTimers,i);
				im_FreeSingleRespawnTimer(pTimer);
				PERFINFO_AUTO_STOP();
				continue;
			}

			pInteractable = interactable_GetByNode(pNode);
			if (!pInteractable) {
				eaRemove(&s_eaEntityTimers,i);
				im_FreeSingleRespawnTimer(pTimer);
				PERFINFO_AUTO_STOP();
				continue;
			}

			if (!pTimer->bCleanup && im_EntityCleanupCheck(pEnt)) {
				//If the ent not flagged to be cleaned up, and is not a full health never clean up this ent
				eaRemove(&s_eaEntityTimers,i);
				im_FreeSingleRespawnTimer(pTimer);
				PERFINFO_AUTO_STOP();
				continue;
			}

			if (im_EntityInCombatCheck(pEnt)) {
				pTimer->fTimeRemaining = ConvertTime;
				PERFINFO_AUTO_STOP();
				continue;
			}

			wlInteractionNodeGetWorldMid(pNode,vSource);
			fRadius = wlInteractionNodeGetRadius(pNode) + 5.f;
			eaClear(&eaCloseEntities);

			//Check to see if any ents are colliding with the object
			if (entGridProximityLookupExEArray(pTimer->iPartitionIdx, vSource, &eaCloseEntities, fRadius, 0, ENTITYFLAG_DEAD,NULL) > 1) { //More than 1 because it will pick up itself
				int j;
				bool bFoundEnt = false;
				for (j = 0; j < eaSize(&eaCloseEntities); j++) {
					Entity *pOtherEnt = eaCloseEntities[j];
					if (!GET_REF(pOtherEnt->hCreatorNode) && pEnt != pOtherEnt) {
						// Only abort if non-destructables around
						bFoundEnt = true; 
						break;
					}
				}
				if (bFoundEnt) {
					pTimer->fTimeRemaining = ConvertTime; //Check again in 2 seconds
					DBGINTERACTION_printf("IM: Node %s unable to convert to ent, entities are in the way\n",REF_STRING_FROM_HANDLE(pTimer->hNode));
					PERFINFO_AUTO_STOP();
					continue;
				}
			}

			gslQueueEntityDestroy(entFromEntityRef(pTimer->iPartitionIdx, pTimer->eEntity));

			im_RemoveNodeFromTimers(pTimer->iPartitionIdx, s_eaRespawnTimers, pNode);

			interactable_SetHideState(pTimer->iPartitionIdx, pInteractable, I_STATE_SHOW, 0, false);

			DBGINTERACTION_printf("IM: Node %s inactive for %f seconds, converting back into an interaction node\n",REF_STRING_FROM_HANDLE(pTimer->hNode),(F32)round(ConvertTime));

			eaRemove(&s_eaEntityTimers,i);
			im_FreeSingleRespawnTimer(pTimer);
		}
		PERFINFO_AUTO_STOP();
	}
}


// ----------------------------------------------------------------------------------
// Interaction Pathing Functions
// ----------------------------------------------------------------------------------

static void im_MRInteractionOwnerMsgHandler(const MRInteractionOwnerMsg *pMsg)
{
	InteractionPathingData *pPathData = pMsg->userPointer;

	switch(pMsg->msgType) 
	{
		xcase MR_INTERACTION_OWNER_MSG_DESTROYED:
		{
			SAFE_FREE(pPathData->keyName);
			SAFE_FREE(pPathData);
		}
		
		xcase MR_INTERACTION_OWNER_MSG_REACHED_WAYPOINT:
		{
			U32 uIndex = pMsg->reachedWaypoint.index;
			
			// Invalid waypoint
			if (uIndex != 0) {
				break;
			}
		}

		acase MR_INTERACTION_OWNER_MSG_FINISHED:
		acase MR_INTERACTION_OWNER_MSG_FAILED:
		{
			GlobalType						eTeammateType = pPathData->teammateType;
			ContainerID						uTeammateID = pPathData->teammateID;
			S32								iIndex = pPathData->index;
			Entity*							pEnt = pPathData->e;
			Entity*							pTargetEnt = entFromEntityRefAnyPartition(pPathData->erTarget);
			GameInteractable*				pInteractable = NULL;
			WorldInteractionPropertyEntry*	pEntry = NULL;

			if (!pPathData->keyName && !pPathData->erTarget) {
				break;
			}

			if (pPathData->keyName) {
				Referent pReferent = RefSystem_ReferentFromString(INTERACTION_DICTIONARY, pPathData->keyName);
				pInteractable = interactable_GetByNode(pReferent);
				pEntry = interactable_GetPropertyEntry(pInteractable, pPathData->index);

			} else if(SAFE_MEMBER(pTargetEnt, pCritter)) {
				if (pTargetEnt->pCritter->encounterData.pGameEncounter) {
					pEntry = interaction_GetActorOrCritterEntry(pTargetEnt->pCritter->encounterData.pGameEncounter, pTargetEnt->pCritter->encounterData.iActorIndex, pTargetEnt->pCritter, pPathData->index);
				} else if(critter_GetNumInteractionEntries(pTargetEnt->pCritter) > 0) {
					pEntry = critter_GetInteractionEntry(pTargetEnt->pCritter, pPathData->index);
				}
			}

			if (mrInteractionGetOwner(pEnt->mm.mrInteraction, im_MRInteractionOwnerMsgHandler, &pPathData)) {
				mrInteractionSetOwner(pEnt->mm.mrInteraction, NULL, NULL);
				SAFE_FREE(pPathData->keyName);
				SAFE_FREE(pPathData);
			}

			interaction_FinishPathing(pEnt, pInteractable, pTargetEnt, NULL, pEntry, iIndex, eTeammateType, uTeammateID);
		}
	}
}


void im_InteractDestroyPathing(Entity *pEnt)
{
	InteractionPathingData*	pPathData;

	if (mrInteractionGetOwner(pEnt->mm.mrInteraction, im_MRInteractionOwnerMsgHandler, &pPathData)) {
		mrInteractionSetOwner(pEnt->mm.mrInteraction, NULL, NULL);
		SAFE_FREE(pPathData->keyName);
		SAFE_FREE(pPathData);
	}
	mrInteractionDestroy(&pEnt->mm.mrInteraction);
}


bool im_InteractBeginPathing(Entity *pEnt, GameInteractable *pInteractable, Entity *pTargetEnt, WorldInteractionPropertyEntry *pEntry, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID)
{
	WorldTimeInteractionProperties *pTimeProperties = interaction_GetTimeProperties(pEntry);
	WorldInteractionNode*	pNode;
	const char*				pcKeyName;
	MRInteractionPath*		pPath = NULL;
	InteractionPathingData*	pPathData;
	WorldRegionType			eMyRegionType;

	PERFINFO_AUTO_START_FUNC();

	// Destroy the old mrInteraction

	im_InteractDestroyPathing(pEnt);

	// Exit if we don't path in this case
	if (pTargetEnt && !pEntry) {
		PERFINFO_AUTO_STOP();
		return false;
	}
	// If this is a player, do not do pathing in space
	eMyRegionType = entGetWorldRegionTypeOfEnt(pEnt);
	if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER &&
		(eMyRegionType == WRT_Space || 
		 eMyRegionType == WRT_SectorSpace))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}
	
	// Extract the path data.
	
	if (pTargetEnt) {
		Vec3					vPos;
		MRInteractionWaypoint*	pWaypoint;

		pPath = StructAlloc(parse_MRInteractionPath);

		// Make a "face the target from where you're at" waypoint
		pWaypoint = StructAlloc(parse_MRInteractionWaypoint);
		eaPush(&pPath->wps, pWaypoint);
		entGetPos(pEnt, pWaypoint->pos);
		entGetPos(pTargetEnt, vPos);
		quatLookAt(pWaypoint->pos, vPos, pWaypoint->rot);

		pWaypoint->seconds = (pTimeProperties ? pTimeProperties->fUseTime : 0) + 0.5f;
		pWaypoint->flags.releaseOnInput = 1;
		pWaypoint->flags.notifyWhenReached = 1;

	} else if (pInteractable && eaSize(&pInteractable->eaInteractLocations)) {
		WorldChairInteractionProperties *pChairProps = interaction_GetChairProperties(pEntry);
		if (pChairProps) {
			WorldInteractionEntry *pIntEntry;
			WorldAltPivotEntry *pKneePivot = NULL;
			Mat4 mPivotMat;
			Vec3 vPosFeet, vPosKnees;
			Quat qRot;
			U32 *eaiBitHandlesPre = NULL, *eaiBitHandlesHold = NULL;
			int i;

			pNode = interactable_GetWorldInteractionNode(pInteractable);
			pIntEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;

			if (!pEnt || !pInteractable || !pIntEntry || !pChairProps) {
				PERFINFO_AUTO_STOP();
				return false;
			}

			// get foot position
			copyVec3(pInteractable->eaInteractLocations[0]->pWorldInteractLocationProperties->vPos, vPosFeet);

			// get knee position
			for (i = eaSize(&pIntEntry->child_entries) - 1; i >= 0; i--) {
				WorldCellEntry *pChildEntry = pIntEntry->child_entries[i];
				if (pChildEntry->type == WCENT_ALTPIVOT) {
					WorldAltPivotEntry *pPivotEntry = (WorldAltPivotEntry*) pChildEntry;
					pKneePivot = pPivotEntry;
					break;
				}
			}

			if (!pKneePivot) {
				PERFINFO_AUTO_STOP();
				return false;
			}
			copyMat4(((WorldCellEntry*) pKneePivot)->bounds.world_matrix, mPivotMat);

			copyVec3(mPivotMat[3], vPosKnees);
			mat3ToQuat(mPivotMat, qRot);

			// get anim bit handles
			for (i = eaSize(&pChairProps->eaBitHandlesPre) - 1; i >= 0; i--) {
				eaiPush(&eaiBitHandlesPre, mmGetAnimBitHandleByName(pChairProps->eaBitHandlesPre[i], 0));
			}
			for (i = eaSize(&pChairProps->eaBitHandlesHold) - 1; i >= 0; i--) {
				eaiPush(&eaiBitHandlesHold, mmGetAnimBitHandleByName(pChairProps->eaBitHandlesHold[i], 0));
			}

			mrInteractionCreatePathForSit(&pPath, vPosFeet, vPosKnees, qRot, eaiBitHandlesPre, eaiBitHandlesHold, pChairProps->fTimeToMove, pChairProps->fTimePostHold);

			eaiDestroy(&eaiBitHandlesPre);
			eaiDestroy(&eaiBitHandlesHold);

		} else {
			const GameInteractLocation*				pLocation = pInteractable->eaInteractLocations[0];
			const WorldInteractLocationProperties*	pProps = pLocation->pWorldInteractLocationProperties;
			MRInteractionWaypoint*					pWaypoint;

			pPath = StructAlloc(parse_MRInteractionPath);
			pWaypoint = StructAlloc(parse_MRInteractionWaypoint);
			eaPush(&pPath->wps, pWaypoint);
			copyVec3(pProps->vPos, pWaypoint->pos);
			copyQuat(pProps->qOrientation, pWaypoint->rot);
			pWaypoint->seconds = 0.5f;
			pWaypoint->flags.notifyWhenReached = 1;
			pWaypoint->flags.releaseOnInput = 1;

			EARRAY_CONST_FOREACH_BEGIN(pProps->eaAnims, j, jsize);
				eaiPush(&pWaypoint->animBitHandles, mmGetAnimBitHandleByName(pProps->eaAnims[j], 0));
			EARRAY_FOREACH_END;
		}
	}
	
	if (!pPath) {
		PERFINFO_AUTO_STOP();
		return false;
	}
	
	// Create a new mrInteraction.

	if (!mrInteractionCreate(pEnt->mm.movement, &pEnt->mm.mrInteraction)) {
		StructDestroySafe(parse_MRInteractionPath, &pPath);
		PERFINFO_AUTO_STOP();
		return false;
	}
	
	pPathData = callocStruct(InteractionPathingData);
	pPathData->index = iIndex;
	pPathData->e = pEnt;
	pPathData->teammateType = eTeammateType;
	pPathData->teammateID = uTeammateID;

	if (pTargetEnt) {
		pPathData->erTarget = entGetRef(pTargetEnt);
	} else {
		pNode = interactable_GetWorldInteractionNode(pInteractable);
		pcKeyName = wlInteractionNodeGetKey(pNode);
		pPathData->keyName = strdup(pcKeyName);
	}
	
	mrInteractionSetOwner(	pEnt->mm.mrInteraction,
							im_MRInteractionOwnerMsgHandler,
							pPathData);
	
	if (!mrInteractionSetPath(pEnt->mm.mrInteraction, &pPath)) {
		StructDestroySafe(parse_MRInteractionPath, &pPath);
		PERFINFO_AUTO_STOP();
		return false;
	}

	PERFINFO_AUTO_STOP();
	return true;
}


U32 im_FindAllEntsWithName(int iPartitionIdx, const char *pcObjectName)
{
	U32 iReturn = 0;
	int i;

	RefDictIterator iterator;
	WorldInteractionNode *pNode;

	RefSystem_InitRefDictIterator(INTERACTION_DICTIONARY, &iterator);
	while (pNode = RefSystem_GetNextReferentFromIterator(&iterator)) {
		WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
		WorldInteractionProperties *pProperties = SAFE_MEMBER(pEntry, full_interaction_properties);
		const char *pcEntityName = pProperties ? wlInteractionGetDestructibleEntityName(pProperties) : NULL;
		GameInteractable *pInteractable = interactable_GetByEntry(pEntry);

		if (pcEntityName && stricmp(pcObjectName, pcEntityName) == 0) {
			//Found the object, see if its alive
			if (interactable_IsSelectable(pInteractable) && !interactable_IsHiddenOrDisabled(iPartitionIdx, pInteractable)) {
				iReturn++;
			}
		}
	}

	//Find objects that have been turned into entities
	for(i=eaSize(&s_eaEntityTimers)-1; i>=0; i--) {
		pNode = GET_REF(s_eaEntityTimers[i]->hNode);
		if (pNode) {
			WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
			WorldInteractionProperties *pProperties = SAFE_MEMBER(pEntry, full_interaction_properties);
			const char *pcEntityName = pProperties ? wlInteractionGetDestructibleEntityName(pProperties) : NULL;

			if (pcEntityName && stricmp(pcObjectName, pcEntityName) == 0) {
				iReturn++;
			}
		}
	}

	return iReturn;
}


// ----------------------------------------------------------------------------------
// Motion Tracking Logic
// ----------------------------------------------------------------------------------

static MotionTracker *im_FindMotionTrackerForNode(int iPartitionIdx, WorldInteractionNode *pNode)
{
	int i;

	if (!pNode) {
		return NULL;
	}

	for (i = 0; i < eaSize(&s_eaMotionTrackers); i++) {
		MotionTracker *pTracker = s_eaMotionTrackers[i];
		if ((pTracker->iPartitionIdx == iPartitionIdx) && (pTracker->pNode == pNode)) {
			return pTracker;
		}
	}

	return NULL;
}


static void im_FreeMotionTarget(SA_PRE_OP_VALID SA_POST_P_FREE MotionTarget *pMotionTarget)
{
	if (!pMotionTarget) {
		return;
	}
	free(pMotionTarget);
}


static void im_FreeMotionTracker(SA_PRE_NN_VALID SA_POST_P_FREE MotionTracker *pMotionTracker)
{
	eaDestroyEx(&pMotionTracker->eaTargets, im_FreeMotionTarget);
	ea32Destroy(&pMotionTracker->eEntityQueue);
	eaiDestroy(&pMotionTracker->iEntityQueueInteractIndex);
	free(pMotionTracker);
}


void im_ForceEndMotionForNode(int iPartitionIdx, WorldInteractionNode *pNode, bool bCleanupOnly)
{
	MotionTracker *pMotionTracker = im_FindMotionTrackerForNode(iPartitionIdx, pNode);
	int i;

	if (!pMotionTracker) {
		return;
	}

	// remove from maintained list
	eaFindAndRemove(&s_eaMotionTrackers, pMotionTracker);

	// destroy the ent motion targets, show start nodes, hide dest nodes
	for (i = eaSize(&pMotionTracker->eaTargets) - 1; i >= 0; i--) {
		Entity *pObjectEnt = entFromEntityRef(iPartitionIdx, pMotionTracker->eaTargets[i]->eEntity);
		GameInteractable *pStartInteractable = pMotionTracker->eaTargets[i]->pStartInteractable;
		GameInteractable *pDestInteractable = pMotionTracker->eaTargets[i]->pDestInteractable;
		if (pObjectEnt) {
			gslQueueEntityDestroy(pObjectEnt);
			gslDestroyEntity(pObjectEnt);
		}
		pMotionTracker->eaTargets[i]->eEntity = 0;

		// Don't call back to interactable if we're in "cleanup only" mode
		if (!bCleanupOnly) {
			if (pStartInteractable) {
				interactable_SetHideState(iPartitionIdx, pStartInteractable, I_STATE_SHOW, 0, false);
			}
			if (pDestInteractable) {
				interactable_SetHideState(iPartitionIdx, pDestInteractable, I_STATE_HIDE, 0, false);
			}
		}
	}

	// free the motion tracker
	im_FreeMotionTracker(pMotionTracker);
}


void im_HandleNodeDestroy(WorldInteractionNode *pNode)
{
	int i;

	if (!pNode) {
		return;
	}

	for (i=eaSize(&s_eaMotionTrackers)-1; i >=0; --i) {
		MotionTracker *pMotionTracker = s_eaMotionTrackers[i];
		if (pMotionTracker->pNode == pNode) {
			// remove from maintained list
			eaRemove(&s_eaMotionTrackers, i);

			// destroy the ent motion targets, show start nodes, hide dest nodes
			for (i = eaSize(&pMotionTracker->eaTargets) - 1; i >= 0; i--) {
				Entity *pObjectEnt = entFromEntityRef(pMotionTracker->iPartitionIdx, pMotionTracker->eaTargets[i]->eEntity);
				if (pObjectEnt) {
					gslQueueEntityDestroy(pObjectEnt);
					gslDestroyEntity(pObjectEnt);
				}
				pMotionTracker->eaTargets[i]->eEntity = 0;
			}

			// free the motion tracker
			im_FreeMotionTracker(pMotionTracker);
		}
	}
}


static Entity *im_MotionTrackerInitTarget(int iPartitionIdx, WorldInteractionNode *pMainNode, MotionTarget *pMotionTarget, WorldMoveDescriptorProperties *pDescriptor, bool bWaitForEnt)
{
	Entity *pObjectEnt = NULL;
	WorldInteractionEntry *pMainEntry = wlInteractionNodeGetEntry(pMainNode);
	WorldInteractionNode *pMoveNode = NULL;
	WorldInteractionNode *pDestNode = NULL;
	Vec3 vEntPos;
	Vec3 vPYR;
	Quat qEntRot;
	Mat4 mEntRot;
	int k;

	for (k = eaSize(&pMainEntry->child_entries) - 1; k >= 0; k--) {
		WorldCellEntry *pChildEntry = pMainEntry->child_entries[k];
		WorldCellEntryData *pChildData = worldCellEntryGetData(pChildEntry);

		if (pChildEntry->type == WCENT_INTERACTION) {
			if (pChildData->interaction_child_idx == pDescriptor->iStartChildIdx) {
				pMoveNode = GET_REF(((WorldInteractionEntry*) pChildEntry)->hInteractionNode);
			} else if (pChildData->interaction_child_idx == pDescriptor->iDestChildIdx && pDescriptor->iDestChildIdx != pDescriptor->iStartChildIdx) {
				pDestNode = GET_REF(((WorldInteractionEntry*) pChildEntry)->hInteractionNode);
			}
		}
	}

	if (pMoveNode) {
		pObjectEnt = im_NodeToEnt(iPartitionIdx, pMoveNode, bWaitForEnt);
		pMotionTarget->eEntity = entGetRef(pObjectEnt);
		pMotionTarget->pStartInteractable = interactable_GetByNode(pMoveNode);
		if (pDestNode) {
			pMotionTarget->pDestInteractable = interactable_GetByNode(pDestNode);
		}

		if (pObjectEnt) {
			entGetPos(pObjectEnt, vEntPos);
			entGetRot(pObjectEnt, qEntRot);

			// find destination position
			quatToMat(qEntRot, mEntRot);
			mulVecMat3(pDescriptor->vDestPos, mEntRot, pMotionTarget->vDestPos);
			addVec3(vEntPos, pMotionTarget->vDestPos, pMotionTarget->vDestPos);

			// find destination rotation
			vPYR[0] = RAD(pDescriptor->vDestRot[0]);
			vPYR[1] = RAD(pDescriptor->vDestRot[1]);
			vPYR[2] = RAD(pDescriptor->vDestRot[2]);
			quatToPYR(qEntRot, vEntPos);
			addVec3(vPYR, vEntPos, vPYR);
			PYRToQuat(vPYR, pMotionTarget->qDestRot);
		}
	}

	return pObjectEnt;
}


void im_MotionTrackerTick(F32 fRate)
{
	int i, j;

	PERFINFO_AUTO_START_FUNC();

	for (i = eaSize(&s_eaMotionTrackers) - 1; i >= 0; i--) {
		MotionTracker *pMotionTracker = s_eaMotionTrackers[i];
		int iPartitionIdx = pMotionTracker->iPartitionIdx;

		// Skip is partition is paused
		if (mapState_IsMapPausedForPartition(iPartitionIdx)) {
			continue;
		}

		pMotionTracker->fTimeRemaining -= fRate;
		if (pMotionTracker->fTimeRemaining <= 0.0f || pMotionTracker->eStatus == kTimerStatus_None) {
			WorldInteractionNode *pMainNode = pMotionTracker->pNode;
			WorldInteractionEntry *pMainEntry = pMainNode ? wlInteractionNodeGetEntry(pMainNode) : NULL;
			GameInteractable *pInteractable = interactable_GetByNode(pMainNode);

			int iInitiatingIndex = pMotionTracker->iInitiatingEntryIndex;
			WorldInteractionPropertyEntry *pEntry = interactable_GetPropertyEntry(pInteractable, iInitiatingIndex);
			WorldMotionInteractionProperties *pMotionProps = interaction_GetMotionProperties(pEntry);
			WorldSoundInteractionProperties *pSoundProps = interaction_GetSoundProperties(pEntry);

			Quat qEntRot;
			Mat4 mNodeMat;
			Vec3 vMainNodePos = {0, 0, 0};
			float fTime = 0.0f;
			float fTotalTime = 0.0f;

			// this node doesn't have any motion properties somehow or failed to record the correct index;
			// this should never happen
			if (!pMotionProps) {
				Errorf("Error: Could not find motion properties for node %s, index %i", pMainNode ? wlInteractionNodeGetKey(pMainNode) : NULL, iInitiatingIndex);
				eaRemove(&s_eaMotionTrackers,i);
				im_FreeMotionTracker(pMotionTracker);
				continue;
			}

			// change to next state
			pMotionTracker->eStatus++;

			// get position of main node
			if (pMainEntry) {
				copyVec3(pMainEntry->base_entry.bounds.world_matrix[3], vMainNodePos);
			}

			switch (pMotionTracker->eStatus)
			{
				// Invalid
				case kTimerStatus_None:
					// this should not get hit, since eStatus is incremented right above this
					break; 

				// when opening motion begins...
				case kTimerStatus_MotionOpening:
					{
						// in case somehow the node was destroyed, set timers to transition to closed state immediately
						if (!pMainNode) {
							pMotionTracker->eStatus = kTimerStatus_MotionClosing;
							pMotionTracker->fTimeOriginal = 0.0f;
							pMotionTracker->fTimeRemaining = 0.0f;
							break;
						}

						fTotalTime = pMotionProps->fTransitionTime;

						// ensure that all motion tracker movement descriptor nodes have been ent-ified
						for (j = 0; j < eaSize(&pMotionProps->eaMoveDescriptors); j++) {
							WorldMoveDescriptorProperties *pDescriptor = pMotionProps->eaMoveDescriptors[j];
							MotionTarget *pMotionTarget;
							Entity *pObjectEnt;

							while (j >= eaSize(&pMotionTracker->eaTargets)) {
								pMotionTarget = calloc(1, sizeof(*pMotionTarget));
								eaPush(&pMotionTracker->eaTargets, pMotionTarget);
							}

							pMotionTarget = pMotionTracker->eaTargets[j];
							assert(pMotionTarget);
							pObjectEnt = entFromEntityRef(iPartitionIdx, pMotionTarget->eEntity);

							// ent-ify the node belonging to this descriptor
							if (!pObjectEnt) {
								pObjectEnt = im_MotionTrackerInitTarget(iPartitionIdx, pMainNode, pMotionTarget, pDescriptor, true);
							}
							if (!pObjectEnt) {
								continue;
							}

							// calculate time needed to move to fully open state
							if (!fTime) {
								if (pMotionTracker->fPercentRemaining) {
									fTime = (1.0f - pMotionTracker->fPercentRemaining) * fTotalTime;
								} else {
									fTime = fTotalTime;
								}
							}

							// send movement request
							if (!pObjectEnt->mm.mrDoorGeo) {
								mmRequesterCreateBasicByName(pObjectEnt->mm.movement, &pObjectEnt->mm.mrDoorGeo, "DoorGeoMovement");
							}
							mrDoorGeoSetTarget(pObjectEnt->mm.mrDoorGeo, pMotionTarget->vDestPos, pMotionTarget->qDestRot, fTime);
						}

						// play transition starting sound
						if (pSoundProps && pSoundProps->pchMovementTransStartSound) {
							mechanics_playOneShotSoundAtLocation(iPartitionIdx, vMainNodePos, NULL, pSoundProps->pchMovementTransStartSound, NULL);
						}

						// set timer (pad this to ensure the entities have reached destination before converting back to nodes)
						pMotionTracker->fTimeOriginal = (fTotalTime + 0.5);
						pMotionTracker->fTimeRemaining += (fTime + 0.5);
					}

					break;

				// when motion reaches destination...
				case kTimerStatus_MotionOpen:
					{
						EntityRef *eEntityQueue = pMotionTracker->eEntityQueue;
						int *iEntityQueueInteractIndex = pMotionTracker->iEntityQueueInteractIndex;
						bool bTransDuringUse = pMotionTracker->bTransDuringUse;

						pMotionTracker->eEntityQueue = NULL;
						pMotionTracker->iEntityQueueInteractIndex = NULL;

						// destroy the ent motion target, keeping the start nodes hidden, and show the dest children
						for (j = eaSize(&pMotionTracker->eaTargets) - 1; j >= 0; j--) {
							Entity *pObjectEnt = entFromEntityRef(iPartitionIdx, pMotionTracker->eaTargets[j]->eEntity);
							GameInteractable *pStartInteractable = pMotionTracker->eaTargets[j]->pStartInteractable;
							GameInteractable *pDestInteractable = pMotionTracker->eaTargets[j]->pDestInteractable;

							if (pObjectEnt) {
								gslQueueEntityDestroy(pObjectEnt);
							}
							pMotionTracker->eaTargets[j]->eEntity = 0;
							if (pStartInteractable) {
								interactable_SetHideState(iPartitionIdx, pStartInteractable, I_STATE_HIDE, 0, false);
							}
							if (pDestInteractable) {
								interactable_SetHideState(iPartitionIdx, pDestInteractable, I_STATE_SHOW, 0, false);
							}
						}

						// play transition ending sound
						if (pSoundProps && pSoundProps->pchMovementTransEndSound) {
							mechanics_playOneShotSoundAtLocation(iPartitionIdx, vMainNodePos, NULL, pSoundProps->pchMovementTransEndSound, NULL);
						}

						// terminate this motion immediately
						eaRemove(&s_eaMotionTrackers, i);
						im_FreeMotionTracker(pMotionTracker);

						// evaluate queued interactions (if transition isn't happening simultaneously with use time)
						if (!bTransDuringUse) {
							for (j = 0; j < ea32Size(&eEntityQueue); j++) {
								Entity *pPlayerEnt = entFromEntityRef(iPartitionIdx, eEntityQueue[j]);
								int index = iEntityQueueInteractIndex[j];

								interaction_ProcessInteraction(pPlayerEnt, pInteractable, NULL, NULL, pEntry);
							}
						}

						ea32Destroy(&eEntityQueue);
						eaiDestroy(&iEntityQueueInteractIndex);

						break;
					}

				// when return motion begins...
				case kTimerStatus_MotionClosing:
					{
						// in case somehow the node was destroyed, set timers to transition to closed state immediately
						if (!pMainNode) {
							pMotionTracker->fTimeOriginal = 0.0f;
							pMotionTracker->fTimeRemaining = 0.0f;
							break;
						}

						fTotalTime = pMotionProps->fReturnTime;

						// ensure that all motion tracker movement descriptor nodes have been ent-ified
						for (j = 0; j < eaSize(&pMotionProps->eaMoveDescriptors); j++) {
							WorldMoveDescriptorProperties *pDescriptor = pMotionProps->eaMoveDescriptors[j];
							WorldInteractionNode *pStartNode;
							MotionTarget *pMotionTarget;
							Entity *pObjectEnt;

							// create motion target if it does not yet exist
							while (j >= eaSize(&pMotionTracker->eaTargets)) {
								pMotionTarget = calloc(1, sizeof(*pMotionTarget));
								eaPush(&pMotionTracker->eaTargets, pMotionTarget);
							}

							pMotionTarget = pMotionTracker->eaTargets[j];
							assert(pMotionTarget);
							pObjectEnt = entFromEntityRef(iPartitionIdx, pMotionTarget->eEntity);

							// ent-ify the node belonging to this descriptor
							if (!pObjectEnt) {
								pObjectEnt = im_MotionTrackerInitTarget(iPartitionIdx, pMainNode, pMotionTarget, pDescriptor, false);

								// set initial object entity position/rotation
								if (pObjectEnt) {
									entSetPos(pObjectEnt, pMotionTarget->vDestPos, true, "Closing motion");
									entSetRot(pObjectEnt, pMotionTarget->qDestRot, true, "Closing motion");
								}
							}
							if (!pObjectEnt) {
								continue;
							}

							pStartNode = interactable_GetWorldInteractionNode(pMotionTarget->pStartInteractable);
							assert(pStartNode);

							// hide the destination node belonging to this descriptor
							if (pMotionTarget->pDestInteractable) {
								interactable_SetHideState(iPartitionIdx, pMotionTarget->pDestInteractable, I_STATE_HIDE, pMotionTarget->eEntity, false);
							}

							// get destination position/rotation
							copyMat4(wlInteractionNodeGetEntry(pStartNode)->base_entry.bounds.world_matrix, mNodeMat);
							wlInteractionNodeGetRot(pStartNode, qEntRot);

							// calculate time needed to move to fully open state
							if (!fTime) {
								if (pMotionTracker->fPercentRemaining) {
									fTime = (1.0f - pMotionTracker->fPercentRemaining) * fTotalTime;
								} else {
									fTime = fTotalTime;
								}
							}

							// send movement request
							if (!pObjectEnt->mm.mrDoorGeo) {
								mmRequesterCreateBasicByName(pObjectEnt->mm.movement, &pObjectEnt->mm.mrDoorGeo, "DoorGeoMovement");
							}
							mrDoorGeoSetTarget(pObjectEnt->mm.mrDoorGeo, mNodeMat[3], qEntRot, fTime);
						}

						if(pSoundProps && pSoundProps->pchMovementTransStartSound) {
							mechanics_stopOneShotSoundAtLocation(pMotionTracker->iPartitionIdx, vMainNodePos, NULL, pSoundProps->pchMovementTransStartSound);
						}

						// play return start sound
						if (pSoundProps && pSoundProps->pchMovementReturnStartSound) {
							mechanics_playOneShotSoundAtLocation(pMotionTracker->iPartitionIdx, vMainNodePos, NULL, pSoundProps->pchMovementReturnStartSound, NULL);
						}

						// set timer (pad this to ensure the entities have reached destination before converting back to nodes)
						pMotionTracker->fTimeOriginal = (fTotalTime + 0.5);
						pMotionTracker->fTimeRemaining += (fTime + 0.5);
					}
					break;

				// when motion has returned to start point...
				case kTimerStatus_MotionClosed:
					// destroy the object ents and unhide the original nodes
					for (j = eaSize(&pMotionTracker->eaTargets) - 1; j >= 0; j--) {
						Entity *pObjectEnt = entFromEntityRef(iPartitionIdx, pMotionTracker->eaTargets[j]->eEntity);
						GameInteractable *pStartInteractable = pMotionTracker->eaTargets[j]->pStartInteractable;
						GameInteractable *pDestInteractable = pMotionTracker->eaTargets[j]->pDestInteractable;

						if (pObjectEnt) {
							gslQueueEntityDestroy(pObjectEnt);
						}
						if (pStartInteractable) {
							interactable_SetHideState(iPartitionIdx, pStartInteractable, I_STATE_SHOW, 0, false);
						}
						if (pDestInteractable) {
							interactable_SetHideState(iPartitionIdx, pDestInteractable, I_STATE_HIDE, 0, false);
						}
					}

					// play return end sound
					if (pSoundProps && pSoundProps->pchMovementReturnEndSound) {
						mechanics_playOneShotSoundAtLocation(iPartitionIdx, vMainNodePos, NULL, pSoundProps->pchMovementReturnEndSound, NULL);
					}

					// evaluate queued interactions (for gates only!)
					if (pMotionTracker->bIsGate && !pMotionTracker->bTransDuringUse) {
						for (j = 0; j < ea32Size(&pMotionTracker->eEntityQueue); j++) {
							Entity *pPlayerEnt = entFromEntityRef(iPartitionIdx, pMotionTracker->eEntityQueue[j]);
							int index = pMotionTracker->iEntityQueueInteractIndex[j];

							interaction_ProcessInteraction(pPlayerEnt, pInteractable, NULL, NULL, pEntry);
						}
					}

					// remove and free the tracker
					eaRemove(&s_eaMotionTrackers, i);
					im_FreeMotionTracker(pMotionTracker);

					break;
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Interaction Lifecycle
// ----------------------------------------------------------------------------------

void im_EndInteract(int iPartitionIdx, GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry, int iIndex, Entity *pPlayerEnt, bool bInterrupt)
{
	WorldInteractionNode *pNode = interactable_GetWorldInteractionNode(pInteractable);
	WorldMotionInteractionProperties *pMotionProps = NULL;

	if (!pEntry) {
		if (pInteractable) {
			pEntry = interactable_GetPropertyEntryForPlayer(pPlayerEnt, pInteractable, iIndex);

		} else if (pEntTarget) {
			if (SAFE_MEMBER(pEntTarget, pCritter)) {
				if (pEntTarget->pCritter->encounterData.pGameEncounter) {
					pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, iIndex);
				} else {
					pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, iIndex);
				}
			}
		} else if (pVolume) {
			pEntry = volume_GetInteractionPropEntry(pVolume, iIndex);
		}
	}
	pMotionProps = interaction_GetMotionProperties(pEntry);

	// If we are an interactable with motion properties. See if we need to initiate any additional movement.
	if (pInteractable && pMotionProps) {
		MotionTracker *pMotionTracker = im_FindMotionTrackerForNode(iPartitionIdx, pNode);
		WorldGateInteractionProperties *pGateProps = interaction_GetGateProperties(pEntry);
		bool bDoClose=false;
		bool bDoOpen=false;

		if (pGateProps!=NULL)
		{
			// Gates behave in specific ways different than non-gates. 
			// When they are being opened (or closed) and the interaction finishes, they have no further motion.
			// However, if they are interrupted during motion, we need to reset them to their previous state. On interrupt:
			//   If they never started their motion we need to leave them alone.
			//   If they have started their motion (TransDuringUse is on, with pMotionTracker) we need to reverse the direction of the motion tracker.
			//   If they have completed their motion (TransDuringUse is off or on, no pMotionTracker), we need create a new motionTracker to return us to the old state.
			//
			// Note that WasGateOpeningState needs to be tracked as a tristate because it's possible to get an End (from an interrupt) before imInteract has been
			//  called. In that instance, we don't need to do any motion reset because the motion never started.

			if (bInterrupt)
			{
				if (interactable_WasGateOpeningState(iPartitionIdx, pInteractable)==1)
				{
					// We were opening. 
					// See if we finished
					if (interactable_IsGateOpen(iPartitionIdx, pInteractable))
					{
						// We finished already. (Maybe a chest?). Need to Change the Gate State
						interactable_ChangeGateOpenState(iPartitionIdx, pInteractable, pPlayerEnt, false);
						bDoClose=true;
					}
					else if (pMotionProps->bTransDuringUse)
					{
						// We only need to close if we started the anim. If we are not TransDuringUse, we don't need to do anything
						//   because the movement will not yet have started.
						bDoClose=true;
					}
				}
				else if (interactable_WasGateOpeningState(iPartitionIdx, pInteractable)==-1)
				{
					// We were closing
					// See if we finished
					if (!interactable_IsGateOpen(iPartitionIdx, pInteractable))
					{
						// We finished already. (Maybe a chest waiting for UI input). Need to Change the Gate State
						interactable_ChangeGateOpenState(iPartitionIdx, pInteractable, pPlayerEnt, true);
						bDoOpen=true;
					}
					else if (pMotionProps->bTransDuringUse)
					{
						// We only need to open if we started the anim. If we are not TransDuringUse, we don't need to do anything
						//   because the movement will not yet have started.
						bDoOpen=true;
					}
				}
			}
			
			// In any case, reset our WasOpeningState so we can detect if we were interrupted before the imInteract started.
			//  NOTE: It's possible for interactable_ChangeGateOpenState to call im_Interact which will set the GateWasOpeningState.
			//  We will override that here as the triggered im_Interact should not be interruptable (Since it is a response to an interruption)
			interactable_SetGateWasOpeningState(iPartitionIdx, pInteractable, 0);
		}
		else
		{
			// Not a gate. Close if we've completed our motion (there's no tracker) or if we were in the process of opening.
			if (!pMotionTracker)
			{
				bDoClose=true;
			}
			else if (pMotionTracker->eStatus == kTimerStatus_MotionOpening)
			{
				// if existing tracker is in the process of opening, update the motion to close
				// this case is specifically for object that open when you loot them, and then automatically close,
				// so if you are hitting it in other circumstances, it's probably a bug [RMARR - 3/15/11]
				bDoClose=true;
			}
			// WOLF[29Aug12] (I wonder if this suffers from the same problem the gates had.
			//  The problem was that if they had never started opening and TransDuringUse was off, they would snap to open, then
			//  closed when the interract was interrupted. For now we will keep the behaviour as it was, bugged or not, just in
			//  case something is depending on the 'broken' behaviour)
		}


		// Take the appropriate steps if we need to open or close
		
		if (bDoClose || bDoOpen)
		{
			if (!pMotionTracker)
			{
				// if no tracker, make a new one. We must not have been moving and need to move now.
				MotionTracker *pTempTracker;

				pTempTracker = calloc(1, sizeof(*pTempTracker));
				pTempTracker->iPartitionIdx = iPartitionIdx;
				pTempTracker->pNode = pNode;
				pTempTracker->bTransDuringUse = pMotionProps->bTransDuringUse;
				if (bDoClose)
				{
					pTempTracker->eStatus = kTimerStatus_MotionOpen; // Will force it to close
				}
				else
				{
					pTempTracker->eStatus = kTimerStatus_None; // Will force it to reopen
				}
				pTempTracker->bIsGate = (pGateProps!=NULL);
				pTempTracker->fTimeOriginal = pMotionProps->fDestinationTime;
				pTempTracker->fTimeRemaining = pMotionProps->fDestinationTime;
				eaPush(&s_eaMotionTrackers, pTempTracker);
				pMotionTracker = pTempTracker;
			}
			else 
			{
				if (pMotionTracker->fTimeOriginal)
				{
					pMotionTracker->fPercentRemaining = MAX(0.0f,pMotionTracker->fTimeRemaining / pMotionTracker->fTimeOriginal);
				}
				else
				{
					pMotionTracker->fPercentRemaining = 0.0f;
				}
				pMotionTracker->fTimeRemaining = 0.0f;
				if (bDoClose)
				{
					pMotionTracker->eStatus = kTimerStatus_MotionOpen;	// Will force it to close
				}
				else
				{
					pMotionTracker->eStatus = kTimerStatus_None;  // Will force it to reopen
				}
			}
		}
	}
}


bool im_Interact(int iPartitionIdx, GameInteractable *pInteractable, Entity *pEntTarget, GameNamedVolume *pVolume, WorldInteractionPropertyEntry *pEntry, int iIndex, Entity *pPlayerEnt)
{
	WorldInteractionNode *pNode = interactable_GetWorldInteractionNode(pInteractable);
	WorldMotionInteractionProperties *pMotionProps = NULL;
		
	if (!pEntry) {
		if (pInteractable) {
			pEntry = interactable_GetPropertyEntryForPlayer(pPlayerEnt, pInteractable, iIndex);
		} else if (pEntTarget) {
			if (SAFE_MEMBER(pEntTarget, pCritter)) {
				if (pEntTarget->pCritter->encounterData.pGameEncounter) {
					pEntry = interaction_GetActorOrCritterEntry(pEntTarget->pCritter->encounterData.pGameEncounter, pEntTarget->pCritter->encounterData.iActorIndex, pEntTarget->pCritter, iIndex);
				} else {
					pEntry = critter_GetInteractionEntry(pEntTarget->pCritter, iIndex);
				}
			}
		} else if (pVolume) {
			pEntry = volume_GetInteractionPropEntry(pVolume, iIndex);
		}
	}
	pMotionProps = interaction_GetMotionProperties(pEntry);

	if (pInteractable && pMotionProps) {
		MotionTracker *pMotionTracker = im_FindMotionTrackerForNode(iPartitionIdx, pNode);

		// Record whether we are trying to open or close the gate so we can compare if we get interrupted.
		if (interactable_IsGateOpen(iPartitionIdx, pInteractable))
		{
			// Closing
			interactable_SetGateWasOpeningState(iPartitionIdx, pInteractable, -1);
		}
		else
		{
			// Opening
			interactable_SetGateWasOpeningState(iPartitionIdx, pInteractable, 1);
		}

		// if no tracker, make a new one
		if (!pMotionTracker) {
			MotionTracker *pTempTracker = calloc(1, sizeof(*pTempTracker));
 
			pTempTracker->iPartitionIdx = iPartitionIdx;
			pTempTracker->pNode = pNode;
			pTempTracker->bIsGate = !!interaction_GetGateProperties(pEntry);
			pTempTracker->bTransDuringUse = pMotionProps->bTransDuringUse;
			pTempTracker->eStatus = (pTempTracker->bIsGate && interactable_IsGateOpen(iPartitionIdx, pInteractable)) ? kTimerStatus_MotionOpen : kTimerStatus_None;
			eaPush(&s_eaMotionTrackers, pTempTracker);
			pMotionTracker = pTempTracker;

		} else if (pMotionTracker->bIsGate) {
			// if a gate tracker exists and is in the process of moving, reverse the direction
			if (pMotionTracker->eStatus == kTimerStatus_MotionClosing || pMotionTracker->eStatus == kTimerStatus_MotionOpening) {
				// if gate is signaled to close while in opening motion, force it to close
				if (interactable_IsGateOpen(iPartitionIdx, pInteractable) && pMotionTracker->eStatus == kTimerStatus_MotionOpening) {
					pMotionTracker->fPercentRemaining = MAX(0.0f, pMotionTracker->fTimeRemaining / pMotionTracker->fTimeOriginal);
					pMotionTracker->fTimeRemaining = 0.0f;
					pMotionTracker->eStatus = kTimerStatus_MotionOpen;
				} else if (!interactable_IsGateOpen(iPartitionIdx, pInteractable) && pMotionTracker->eStatus == kTimerStatus_MotionClosing) {
					// if gate is signaled to open while in closing motion, force it to reopen
					pMotionTracker->fPercentRemaining = MAX(0.0f, pMotionTracker->fTimeRemaining / pMotionTracker->fTimeOriginal);
					pMotionTracker->fTimeRemaining = 0.0f;
					pMotionTracker->eStatus = kTimerStatus_None;
				}

			} else if (pMotionTracker->eStatus == kTimerStatus_None || pMotionTracker->eStatus == kTimerStatus_MotionOpen) {
				// if gate is signaled to close while it is queued to open, queue it to close
				if (interactable_IsGateOpen(iPartitionIdx, pInteractable) && pMotionTracker->eStatus == kTimerStatus_None) {
					pMotionTracker->fPercentRemaining = 1.0f - pMotionTracker->fPercentRemaining;
					pMotionTracker->eStatus = kTimerStatus_MotionOpen;
				} else if (!interactable_IsGateOpen(iPartitionIdx, pInteractable) && pMotionTracker->eStatus == kTimerStatus_MotionClosing) {
					// if gate is signaled to open while it is queued to close, queue it to reopen
					pMotionTracker->fPercentRemaining = 1.0f - pMotionTracker->fPercentRemaining;
					pMotionTracker->eStatus = kTimerStatus_None;
				}
			}

		} else if (pMotionTracker->eStatus == kTimerStatus_MotionOpen) {
			// if existing tracker is in the process of closing (i.e. post success of interaction), update the motion to reopen
			pMotionTracker->fPercentRemaining = 0;
			pMotionTracker->fTimeRemaining = 0.0f;
			pMotionTracker->eStatus = kTimerStatus_MotionOpening;

		} else if (pMotionTracker->eStatus == kTimerStatus_MotionClosing) {
			pMotionTracker->fPercentRemaining = MAX(0.0f,pMotionTracker->fTimeRemaining / pMotionTracker->fTimeOriginal);
			pMotionTracker->fTimeRemaining = 0.0f;
			pMotionTracker->eStatus = kTimerStatus_None;
		}

		// update the queue of entities using this node
		if (pMotionTracker) {
			pMotionTracker->iInitiatingEntryIndex = iIndex;
			if (pPlayerEnt) {
				ea32Push(&pMotionTracker->eEntityQueue, entGetRef(pPlayerEnt));
				eaiPush(&pMotionTracker->iEntityQueueInteractIndex, iIndex);
			}
		}
		return true;

	} else if (pPlayerEnt) {
		// if no motion is involved, fall through and continue interaction
		interaction_ProcessInteraction(pPlayerEnt, pInteractable, pEntTarget, pVolume, pEntry);
		return true;
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Latebind Functions
// ----------------------------------------------------------------------------------

//Returns true if the entity is allowed to be cleaned up
bool DEFAULT_LATELINK_im_EntityCleanupCheck(Entity *pEnt)
{
	return true;
}


//Returns true if the entity is in combat
bool DEFAULT_LATELINK_im_EntityInCombatCheck(Entity *pEnt)
{
	return false;
}


bool OVERRIDE_LATELINK_wlInteractionGetSelectableFromCritter(WorldInteractionNode *node)
{
	WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(node);

	if (pEntry) {
		CritterDef *pCritterDef = wlInteractionGetDestructibleCritterDef(pEntry->full_interaction_properties);
		if (pCritterDef) {
			return !pCritterDef->bUnselectable;
		}
	}
	return true;
}


