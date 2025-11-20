#include "aiFormation.h"
#include "aiFormation_h_ast.h"
#include "aiDebugShared.h"
#include "aiDebugShared_h_ast.h"

#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiTeam.h"
#include "Entity.h"
#include "file.h"
#include "gslMapState.h"
#include "GameServerLib.h"
#include "MemoryPool.h"
#include "RegionRules.h"
#include "ResourceManager.h"
#include "WorldLib.h"
#include "WorldGrid.h"

AIFormationDef* AIFormation_CreateDefFromTeamPositions(AITeam *pTeam);

const char *g_pcAIFormationDef = "AIFormationDef";

typedef struct FormationFidelitySettings
{
	F32		updateFreqInSeconds;
	F32		walkGranularity;
	bool	useCapsuleCasts;
	bool	doWalkCheck;
} FormationFidelitySettings;

static FormationFidelitySettings s_aFidelitySettings[AIFormationFidelity_COUNT] = {0};

// ---------------------------------------------------------------------------------------------------------
MP_DEFINE(AIFormation);
MP_DEFINE(AIFormationSlot);
MP_DEFINE(AIFormationData);


// ---------------------------------------------------------------------------------------------------------
static AIFormation* aiFormation_Alloc()
{
	MP_CREATE(AIFormation, 16);

	return MP_ALLOC(AIFormation);
}

static void aiFormation_Free(AIFormation* formation)
{
	if (formation)
	{
		// only when a formation def was created especially for this formation instance (patrols)
		if (formation->pOwnedFormationDef) 
			StructDestroy(parse_AIFormationDef, formation->pOwnedFormationDef);
		if (IS_HANDLE_ACTIVE(formation->hFormationDef))
		{
			REMOVE_HANDLE(formation->hFormationDef);
		}

		MP_FREE(AIFormation, formation);
	}
}

// ---------------------------------------------------------------------------------------------------------
static AIFormationSlot* aiFormationSlot_Alloc()
{
	MP_CREATE(AIFormationSlot, 16);

	return MP_ALLOC(AIFormationSlot);
}

static void aiFormationSlot_Free(AIFormationSlot* slot)
{
	if (slot) MP_FREE(AIFormationSlot, slot);
}

// ---------------------------------------------------------------------------------------------------------
static AIFormationData* aiFormationData_Alloc(AIFormation * pFormation)
{
	AIFormationData * pData;

	MP_CREATE(AIFormationData, 16);

	pData = MP_ALLOC(AIFormationData);
	pData->pFormation = pFormation;

	pData->pFormation->uRefCount++;

	return pData;
}

void aiFormationData_Free(AIFormationData* formationData)
{
	if (formationData)
	{
		assert(formationData->pFormation->uRefCount);
		formationData->pFormation->uRefCount--;
		MP_FREE(AIFormationData, formationData);
	}
}


// ---------------------------------------------------------------------------------------------------
static int aiFormationDef_Validate(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, void* pResource, U32 userID)
{
	AIFormationDef* formationDef = pResource;

	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			if (formationDef->fidelity <= AIFormationFidelity_NULL || 
				formationDef->fidelity >= AIFormationFidelity_COUNT)
			{
				ErrorFilenamef(pResourceName,"Formation fidelity is an invalid value");
				formationDef->fidelity = AIFormationFidelity_LOW;
			}
			return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}


// ------------------------------------------------------------------------------------------------------------------
AUTO_FIXUPFUNC;
TextParserResult fixupAIFormationDef(AIFormationDef* formation, enumTextParserFixupType eType, void *pExtraData)
{
	int success = true;
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ:
		case FIXUPTYPE_POST_BIN_READ:
		{
			if (eaSize(&formation->eaSlots) == 0)
			{
				int i;
				int maxSlots = 10;

				for(i=0; i < maxSlots; i++)
				{
					AIFormationSlotDef *slot = StructCreate(parse_AIFormationSlotDef);
					F32 yaw = -HALFPI + ((i%2) ? 1 : -1) * (i/2+1) * PI/(maxSlots/2+1);
					Vec3 vPos;

					setVec3(vPos, cosf(yaw), 0, sinf(yaw));
					// position->rank = i/2;
					slot->x = vPos[0];
					slot->z = vPos[2];

					eaPush(&formation->eaSlots, slot);
				}
				formation->autoSlotted = true;
			}
		}

	}
	return success;
}

// ---------------------------------------------------------------------------------------------------------
void aiFormation_Startup()
{
	// set up the fidelity settings here instead in the static block, just so it's more readable 
	{
		s_aFidelitySettings[AIFormationFidelity_LOW].updateFreqInSeconds = 0.6f;
		s_aFidelitySettings[AIFormationFidelity_LOW].useCapsuleCasts = false;
		s_aFidelitySettings[AIFormationFidelity_LOW].doWalkCheck = false;
				
		s_aFidelitySettings[AIFormationFidelity_MED].updateFreqInSeconds = 0.6f;
		s_aFidelitySettings[AIFormationFidelity_MED].useCapsuleCasts = false;
		s_aFidelitySettings[AIFormationFidelity_MED].doWalkCheck = true;
		s_aFidelitySettings[AIFormationFidelity_MED].walkGranularity = 2.f;
		

		s_aFidelitySettings[AIFormationFidelity_HIGH].updateFreqInSeconds = 0.25f;
		s_aFidelitySettings[AIFormationFidelity_HIGH].useCapsuleCasts = true;
		s_aFidelitySettings[AIFormationFidelity_HIGH].doWalkCheck = true;
		s_aFidelitySettings[AIFormationFidelity_HIGH].walkGranularity = 2.f;
	}

	RefSystem_RegisterSelfDefiningDictionary(g_pcAIFormationDef, false, parse_AIFormationDef, true, false, NULL);
	resDictManageValidation(g_pcAIFormationDef, aiFormationDef_Validate);

	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(g_pcAIFormationDef, ".Name", NULL, NULL, NULL, NULL);
	}

	resLoadResourcesFromDisk(g_pcAIFormationDef, "ai/formation", ".form", "AIFormations.bin", PARSER_FORCEREBUILD | RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

	//resDictProvideMissingResources(g_pcAIFormationDef);
	//resDictProvideMissingRequiresEditMode(g_pcAIFormationDef);
}

void aiFormation_OnMapLoad()
{
	
}

// ---------------------------------------------------------------------------------------------------
AIFormationDef* aiFormation_GetDefByName(const char *pchDefName)
{
	if (pchDefName)
	{
		AIFormationDef *pDef = RefSystem_ReferentFromString(g_pcAIFormationDef, pchDefName);
		return pDef;
	}
	return NULL;
}

// updates:

// ---------------------------------------------------------------------------------------------------------
void AIFormation_AssignFormation(const AIFormationDef *pDef, AITeam *pTeam)
{
	// create and initialize the AIFormation
	
}

// ---------------------------------------------------------------------------------------------------------
__forceinline static AIFormationDef* aiFormation_GetFormationDef(AIFormation *pFormation)
{
	return (! pFormation->pOwnedFormationDef) ? GET_REF(pFormation->hFormationDef) : pFormation->pOwnedFormationDef;
}

// ---------------------------------------------------------------------------------------------------------
AIFormationDef* aiTeamGetFormationDef(SA_PARAM_NN_VALID AITeam* team)
{
	return team->pTeamFormation ? aiFormation_GetFormationDef(team->pTeamFormation) : NULL;
}

static bool _initializeFormation(AIFormationDef *pDef, AIFormation *pFormation, AITeam *pTeam, Entity *pOwner)
{
	S32 i;
	S32 numSlots = eaSize(&pDef->eaSlots);
	if (!numSlots)
	{
		return false;
	}

	if (pTeam->pTeamFormation == NULL && aiTeamGetLeader(pTeam) == pOwner)
		pTeam->pTeamFormation = pFormation;

	pFormation->bIsDirty = true;
	devassert(pTeam && pFormation->pTeam == NULL);
	pFormation->pTeam = pTeam;
	pFormation->updateFreqInSeconds = 0.5f;

	if (pOwner)
	{
		pFormation->erLeader = entGetRef(pOwner);
		pFormation->bIsFlying = !!aiMovementGetFlying(pOwner, pOwner->aibase);
		if(!pOwner->aibase->pFormationData)
			pOwner->aibase->pFormationData = aiFormationData_Alloc(pFormation);
		else
		{
			if (pOwner->aibase->pFormationData->pFormation)
				pOwner->aibase->pFormationData->pFormation->uRefCount--;
			pOwner->aibase->pFormationData->pFormation = pFormation;
			pOwner->aibase->pFormationData->pFormation->uRefCount++;
		}
	}

	// pre-alloc the slots
	for (i = 0; i < numSlots; ++i)
	{
		AIFormationSlot *pSlot = aiFormationSlot_Alloc();
		eaPush(&pFormation->eaFormationSlots, pSlot);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------------------
static AIFormationDef* _createFormationDefForPatrol(AITeam* team)
{
	// determine how many formation positions we'll need
	AIFormationDef *pFormationDef = StructCreate(parse_AIFormationDef);
	Vec3 vLeaderPos;
	Entity *pLeader;
	if (!pFormationDef)
		return NULL;

	pLeader = aiTeamGetLeader(team);
	if (!pLeader)
		return NULL;
	
	entGetPos(pLeader, vLeaderPos);

	// create the slots from the members on the team
	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
	{
		Vec3 patrolOffset, offset;
		Quat inverseSpawnRot;
		AIFormationSlotDef *pSlotDef;
		Vec3 vPos;
		
		if (pLeader == pMember->memberBE)
			continue;

		entGetPos(pMember->memberBE, vPos);
		subVec3(vPos, vLeaderPos, offset);

		quatInverse(pLeader->aibase->spawnRot, inverseSpawnRot);
		quatRotateVec3(inverseSpawnRot, offset, patrolOffset);
		
		pSlotDef = StructCreate(parse_AIFormationSlotDef);
		if (!pSlotDef)
		{
			StructDestroy(parse_AIFormationDef, pFormationDef);
			return NULL;
		}

		pSlotDef->x = patrolOffset[0];
		pSlotDef->z = patrolOffset[2];

		eaPush(&pFormationDef->eaSlots, pSlotDef);
	}
	FOR_EACH_END
	
	pFormationDef->distUpdateThreshold = 1.f;
	pFormationDef->rotUpdateThreshold = RAD(5.f);
	pFormationDef->fidelity = AIFormationFidelity_LOW;
	pFormationDef->bDoCollisionSlotTruncating = true;
	pFormationDef->fMinSlotTruncate = 6.f;
		
	return pFormationDef;
}

// ---------------------------------------------------------------------------------------------------------
// creating a formation for the use for patrols
bool aiFormation_CreateFormationForPatrol(SA_PARAM_NN_VALID AITeam* team)
{
	AIFormation *pFormation = NULL;
	AIFormationDef *pDef;

	// if we already have a formation, do nothing
	if (team->pTeamFormation)
		return true;
				

	pFormation = aiFormation_Alloc();
	if (!pFormation)
		return false;


	pDef = _createFormationDefForPatrol(team);
	if (!pDef)
	{
		aiFormation_Free(pFormation);
		return false;
	}

	pFormation->pOwnedFormationDef = pDef;
	pFormation->bIsPatrol = true;

	if (!_initializeFormation(pDef, pFormation, team, aiTeamGetLeader(team)))
	{
		aiFormation_Free(pFormation);
		return false;
	}

	// add the rest of the patrol to the formation
	FOR_EACH_IN_EARRAY(team->members, AITeamMember, pMember)
	{
		if (pMember->memberBE->aibase->pFormationData == NULL)
		{
			pMember->memberBE->aibase->pFormationData = aiFormationData_Alloc(pFormation);
		}
		else
		{
			devassert(pMember->memberBE->aibase->pFormationData->pFormation == pFormation);
		}
	}
	FOR_EACH_END

	return true;
}

// ---------------------------------------------------------------------------------------------------------
bool aiFormation_AssignDefaultFormation(AITeam *pTeam, Entity * pOwner)
{
	return aiFormation_AssignFormationByName("default", pTeam, pOwner);
}

// ---------------------------------------------------------------------------------------------------------
bool aiFormation_AssignFormationByName(const char *pszFormationDefName, AITeam *pTeam, Entity * pOwner)
{
	AIFormationDef  *pDef = NULL;
	AIFormation *pFormation = NULL;
	pFormation = aiFormation_Alloc();
	if (!pFormation)
		return false;

	SET_HANDLE_FROM_STRING(g_pcAIFormationDef, pszFormationDefName, pFormation->hFormationDef);
	pDef = GET_REF(pFormation->hFormationDef);
	if (!pDef)
	{
		aiFormation_Free(pFormation);
		return false;
	}


	if (!_initializeFormation(pDef, pFormation, pTeam, pOwner))
	{
		aiFormation_Free(pFormation);
		return false;
	}

	return true;
	
}

// ---------------------------------------------------------------------------------------------------------
void aiFormation_Destroy(AIFormation **ppFormation)
{
	if (*ppFormation)
	{
		AIFormation *pFormation = *ppFormation;
		assert((*ppFormation)->uRefCount == 0);
		*ppFormation = NULL;
		
		FOR_EACH_IN_EARRAY(pFormation->eaFormationSlots, AIFormationSlot, pSlot)
		{
			aiFormationSlot_Free(pSlot);
		}
		FOR_EACH_END
		eaDestroy(&pFormation->eaFormationSlots);
		
		aiFormation_Free(pFormation);
	}

}

static int _getFormationMembers(const AIFormation * pFormation,Entity ** pEntMembers) // <-- not an earray
{
	int iMembers=0;
	FOR_EACH_IN_EARRAY(pFormation->pTeam->members, AITeamMember, pMember)
	{
		if (pMember->memberBE->aibase->pFormationData && 
			pMember->memberBE->aibase->pFormationData->pFormation == pFormation &&
			!entIsPlayer(pMember->memberBE))
		{
			pEntMembers[iMembers++] = pMember->memberBE;
		}
	}
	FOR_EACH_END

	return iMembers;
}

// ---------------------------------------------------------------------------------------------------------
static bool AIFormation_ShouldUpdateFormation(const AIFormation *pFormation, 
											  const AIFormationDef *pFormationDef, 
											  Entity *pLeader,
											  const Vec3 vNewPos, 
											  const Vec3 newPYR)
{
	// a regular old array
	Entity ** pEntMembers=NULL;
	int iEnts;

	F32 formationDistSQR;
	if (pFormation->bIsDirty)
		return true;

	pEntMembers = alloca(sizeof(Entity *)*eaSize(&pFormation->pTeam->members));

	iEnts = _getFormationMembers(pFormation,pEntMembers);

	if (pFormation->bIsPatrol)
	{
		bool bIsPatrolling = false;
		int i;
		for (i=0;i<iEnts;i++)
		{
			if (pLeader == pEntMembers[i])
				continue;
			
			if (!pEntMembers[i]->aibase->pFormationData)
				return true; // a patroller has no formation data, we need to update

			if (pEntMembers[i]->aibase->pFormationData->bIsPatrolling)
			{
				bIsPatrolling = true;
				break;
			}
		}

		if (!bIsPatrolling)
			return false;
	}

	if (gConf.bNewAnimationSystem)
	{
		formationDistSQR = distance3Squared(vNewPos, pFormation->vFormationPos);
	}
	else
	{
		//the use of XZ only when not flying appears to have originated due to a hack, see http://jira:8080/browse/NNO-2135
		//I wasn't able to reproduce the bad behavior mentioned in NNO-2135, so the actual cause has probably since been fixed..
		//if we see the mentioned issue starting to fire up again, then we probably just need to use a larger distance for the y-value check when not flying (egg-shaped instead of sphere),
		//otherwise, the game will think you're settled when you're really falling straight down and such
		if(pFormation->bIsFlying)
			formationDistSQR = distance3Squared(vNewPos, pFormation->vFormationPos);
		else
			formationDistSQR = distance3SquaredXZ(vNewPos, pFormation->vFormationPos);
	}

	if (!pFormation->bSettled)
	{	// check if we've moved or rotated beyond some small hard thresholds
		// (we might really want to check velocities)
		if (formationDistSQR <= SQR(0.25f) && 
			fabsf(newPYR[1] - pFormation->vFormationPYR[1]) <= RAD(20.f) )
		{
			return false;
		}
	}
	else
	{
		// check if the pos/pyr hasn't moved past a threshold, if so do not update the formation
		if (formationDistSQR <= SQR(pFormationDef->distUpdateThreshold))
		{
			if (pFormationDef->rotUpdateThreshold == 0 ||  
				abs(newPYR[1] - pFormation->vFormationPYR[1]) <= RAD(pFormationDef->rotUpdateThreshold))
				return false;
		}
		
	}

	return true;
}

static void _assignMembersToSlots(AIFormation *pFormation, Entity **eaMemebersNeedingSlots);

// ---------------------------------------------------------------------------------------------------------
__forceinline static bool _findGroundDistance(int iPartitionIdx, AIFormationFidelity fidelity, const Vec3 vFormationOrigin, 
											  const Vec3 vPos, Vec3 vOutPos)
{
#define GROUND_OFFSET_THRESHOLD 3.f
	
	if (fidelity == AIFormationFidelity_HIGH)
	{
		S32 found = false;
		Vec3 vTmpPos;
		copyVec3(vPos, vTmpPos);
		worldSnapPosToGround(iPartitionIdx, vTmpPos, 6.f, -10.f, &found);
		if (found && abs(vTmpPos[1] - vFormationOrigin[1]) <= GROUND_OFFSET_THRESHOLD)
		{
			copyVec3(vTmpPos, vOutPos);
			return true;
		}
	}
	else
	{
		Vec3 vTmpPos;
		if (aiFindGroundDistance(worldGetActiveColl(iPartitionIdx), (F32*)vPos, vTmpPos) != -FLT_MAX)
		{
			if (abs(vTmpPos[1] - vFormationOrigin[1]) <= GROUND_OFFSET_THRESHOLD)
			{
				copyVec3(vTmpPos, vOutPos);
				return true;
			}
		}
	}

	return false;
}


// ---------------------------------------------------------------------------------------------------------

// todo: allow high fidelity to actually raycast downwards and find the ground slope, so we do not have 
//	pets standing on very steep slopes. 

static bool _validateFormationSlotPosition(int iPartitionIdx, const AIFormation *pFormation,
										   AIFormationDef *pFormationDef, Entity *e, 
											const Vec3 vFormationOrigin, Vec3 vInOutPos)
{
	AICollideRayFlag	collideFlags;
	Vec3 hitPt;
	AICollideRayResult result = AICollideRayResult_NONE;


	collideFlags = AICollideRayFlag_DOAVOIDCHECK;
	if (s_aFidelitySettings[pFormationDef->fidelity].useCapsuleCasts)
	{
		collideFlags |= AICollideRayFlag_DOCAPSULECHECK | AICollideRayFlag_SKIPRAYCAST;
	}
	
	if (!pFormation->bIsFlying && s_aFidelitySettings[pFormationDef->fidelity].doWalkCheck)
	{
		collideFlags |= AICollideRayFlag_DOWALKCHECK;
	}

	if(!aiCollideRayEx(iPartitionIdx, e, vFormationOrigin, NULL, vInOutPos, collideFlags, 
						s_aFidelitySettings[pFormationDef->fidelity].walkGranularity, 
						&result, hitPt))
	{
		if (pFormation->bIsFlying || 
			_findGroundDistance(iPartitionIdx, pFormationDef->fidelity, vFormationOrigin, vInOutPos, vInOutPos))
		{	
			return true;
		}
	}
	else
	{
		// hit something, check if we allow the position to be moved in towards the formation origin
		if (!pFormationDef->bDoCollisionSlotTruncating || 
			result == AICollideRayResult_AVOID)
			return false;

		{
			Vec3 vDir, vTmpPos;
			F32 len;

			subVec3(hitPt, vFormationOrigin, vDir);
			len = normalVec3(vDir);
			if(len < pFormationDef->fMinSlotTruncate)
				return false;
			
			#define COLLISION_PUSHBACK_DIST	3
			len -= COLLISION_PUSHBACK_DIST;
			scaleAddVec3(vDir, len, vFormationOrigin, vTmpPos);

			if (pFormation->bIsFlying)
			{
				copyVec3(vTmpPos, vInOutPos);
				return true;
			}

			
			if (_findGroundDistance(iPartitionIdx, pFormationDef->fidelity, vFormationOrigin, vTmpPos, vInOutPos))
			{
				return true;
			}
		}
	}

	return false;
}

// ---------------------------------------------------------------------------------------------------------
__forceinline static void _setFormationMovement(Entity * e, AIFormation *pFormation, 
												Entity *pLeader, bool bForceFollowLeader)
{
	AIVarsBase *aib = e->aibase;
	AIFormationDef *pFormationDef = NULL;
	
	if (!pFormation)
	{
		return;
	}

	pFormationDef = aiFormation_GetFormationDef(pFormation);

	if (!pFormationDef)
	{
		return;
	}

	if(!bForceFollowLeader && 
		(aiMovementGetMovementOrderType(e, aib)!=AI_MOVEMENT_ORDER_ENT ||
		aiMovementGetMovementOrderEntDetail(e, aib)!=AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET ||
		aiMovementGetMovementTargetEnt(e, aib) != entGetRef(pLeader)))
		return;

	if (aib->pFormationData->pCurrentSlot)
	{
		Quat q;
		Vec3 vOffset;
		//Rotate the offsets back to their original position because the movement system will rotate them to match the player's facing position
		quatInverse(aib->pFormationData->pFormation->qFormationRot, q);
		quatRotateVec3(q, aib->pFormationData->pCurrentSlot->vOffset, vOffset);
		aiMovementSetTargetEntity(e, aib, 
									pLeader, vOffset, 
									true, AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET, 
									AI_MOVEMENT_TARGET_CRITICAL);
	}
	else
	{
		AIConfig* config = aiGetConfig(e, aib);
		Vec3 vDirToFormationPos;
		
		// fallback to just going to an offset from 
		entGetPos(e, vDirToFormationPos);
		if (gConf.bNewAnimationSystem && fabsf(config->movementParams.movementYOffset) > 0.000001f) {
			//doing this here to prevent feedback since the movementYOffset is introduced when determining the entity's target position -> change in the entity's position -> the position when you then grab it here
			vDirToFormationPos[1] -= config->movementParams.movementYOffset;
		}
		subVec3(vDirToFormationPos, pFormation->vFormationPos, vDirToFormationPos);
		normalVec3(vDirToFormationPos);
		scaleVec3(vDirToFormationPos, 6.f, vDirToFormationPos);

		aiMovementSetTargetEntity(e, aib, 
									pLeader, vDirToFormationPos, 
									false, AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET, 
									AI_MOVEMENT_TARGET_CRITICAL);
	}

	aiMovementSetFinalFaceRot(e, aib, pFormation->qFormationRot);
	if (pFormationDef->bDoCheatingMovement || (pFormationDef->fWalkDistance > 0.f && pFormationDef->fRunDistance > 0.f))
		aiMovementSetWalkRunDist(e, aib, pFormationDef->fWalkDistance, pFormationDef->fRunDistance, pFormationDef->bDoCheatingMovement);
}


// ---------------------------------------------------------------------------------------------------------
void aiFormation_UpdateFormation(int iPartitionIdx, AIFormation *pFormation)
{
	AIFormationDef *pFormationDef;
	Entity *pLeader;
	S32 numSlotsNeeded = 0;

	Entity ** pEntMembers=NULL;
	int iEnts;
	RegionRules *rrNoOverride = NULL;
	
	static Entity **s_eaMembersToSlot = NULL;

	if (!pFormation || !pFormation->erLeader || !pFormation->pTeam)
		return;

	
	if (!pFormation->bIsDirty &&
		(ABS_TIME_SINCE_PARTITION(iPartitionIdx, pFormation->lastUpdate) < SEC_TO_ABS_TIME(pFormation->updateFreqInSeconds)))
	{	// too soon for update

		return;
	}

	// get the formation def
	pFormationDef = aiFormation_GetFormationDef(pFormation);
	if (!pFormationDef)
		return;

	pEntMembers = alloca(sizeof(Entity *)*eaSize(&pFormation->pTeam->members));

	pFormation->updateFreqInSeconds = s_aFidelitySettings[pFormationDef->fidelity].updateFreqInSeconds;

	pFormation->lastUpdate = ABS_TIME_PARTITION(iPartitionIdx);
	pFormation->lastUpdateNumMembers = eaSize(&pFormation->pTeam->members);
	
	if (!pFormation->bIsPatrol)
	{
		pLeader = entFromEntityRef(iPartitionIdx, pFormation->erLeader);
	}
	else
	{
		pLeader = aiTeamGetLeader(pFormation->pTeam);
	}	

	if (!pLeader)
	{	
		// config -> should we get a new leader if the current is dead
		// see aiMovementDoPatrol
		// we might need to be told if this is a player/pet situation or a critter patrol
		return;
	}
	pFormation->bIsFlying = !!aiMovementGetFlying(pLeader, pLeader->aibase);

	// Get the leader's current position, orientation and check if we should update
	{
		Vec3 vLeaderPos, pyrLeader;
		
		entGetPos(pLeader, vLeaderPos);
		entGetFacePY(pLeader, pyrLeader);
		pyrLeader[2] = 0.f;
		pyrLeader[0] = 0.f;
		rrNoOverride = RegionRulesFromVec3NoOverride(vLeaderPos);
				
		if (!AIFormation_ShouldUpdateFormation(pFormation, pFormationDef, pLeader, vLeaderPos, pyrLeader))
		{
			if (!pFormation->bSettled)
			{
				int i;
				iEnts = _getFormationMembers(pFormation,pEntMembers);

				for (i=0;i<iEnts;i++)
				{
					if (pEntMembers[i] != pLeader)
					{
						AIConfig* config = aiGetConfig(pEntMembers[i], pEntMembers[i]->aibase);

						// Don't assign positions to immobile critters
						if(config->movementParams.immobile)
							continue;
						if(!pFormation->bUpdatingForSpawnLocations && !aiIsEntAlive(pEntMembers[i]))
							continue;

						aiMovementSetTargetEntitySettledFlag(pEntMembers[i], pEntMembers[i]->aibase, pFormation->erLeader, true, vLeaderPos);
					}
				}
			}
			pFormation->bSettled = true;
			return;
		}

		if (pFormation->bSettled)
		{
			int i;
			iEnts = _getFormationMembers(pFormation,pEntMembers);

			for (i=0;i<iEnts;i++)
			{
				if (pEntMembers[i] != pLeader)
				{
					AIConfig* config = aiGetConfig(pEntMembers[i], pEntMembers[i]->aibase);

					// Don't assign positions to immobile critters
					if(config->movementParams.immobile)
						continue;
					if(!pFormation->bUpdatingForSpawnLocations && !aiIsEntAlive(pEntMembers[i]))
						continue;

					aiMovementSetTargetEntitySettledFlag(pEntMembers[i], pEntMembers[i]->aibase, pFormation->erLeader, false, NULL);
				}
			}
		}
			
		pFormation->bIsDirty = false;
		pFormation->bSettled = false;

		copyVec3(vLeaderPos, pFormation->vFormationPos); 
		copyVec3(pyrLeader, pFormation->vFormationPYR);
		PYRToQuat(pFormation->vFormationPYR, pFormation->qFormationRot);

		if (!pFormation->bIsFlying)
			worldSnapPosToGround(iPartitionIdx, pFormation->vFormationPos, 6.f, -10.f, NULL);
	}

	// clear all the assignees first 
	{
		S32 i, count = eaSize(&pFormationDef->eaSlots);

		for(i = 0; i < count; ++i)
		{			
			AIFormationSlot	*pSlot = pFormation->eaFormationSlots[i];
			pSlot->erAssignee = 0;
		}
	}

	// determine how many positions are needed
	{
		int i;
		numSlotsNeeded = 0;
		eaClear(&s_eaMembersToSlot);
		
		// go through the members and find out who is alive and needs a formation slot
		iEnts = _getFormationMembers(pFormation,pEntMembers);

		for (i=0;i<iEnts;i++)
		{
			Entity *pEnt = pEntMembers[i];
			if (pEnt != pLeader)
			{
				AIConfig* config = aiGetConfig(pEnt, pEnt->aibase);

				// Don't assign positions to immobile critters
				if(config->movementParams.immobile)
					continue;
				if(!pFormation->bUpdatingForSpawnLocations && !aiIsEntAlive(pEnt))
				{	// make sure we clear the current slot this guy has when dead
					pEnt->aibase->pFormationData->pCurrentSlot = NULL;
					continue;
				}

				
				devassert(pEnt->aibase->pFormationData);
				if (!pFormation->bUpdatingForSpawnLocations && pFormationDef->bAlwaysReassignSlots)
				{
					pEnt->aibase->pFormationData->pCurrentSlot = NULL;
				}
				else if (pEnt->aibase->pFormationData->pCurrentSlot)
				{
					pEnt->aibase->pFormationData->pCurrentSlot->erAssignee = entGetRef(pEnt);
				}
				
				eaPush(&s_eaMembersToSlot, pEnt);
				numSlotsNeeded++;
			}
		}
	
		if (numSlotsNeeded <= 0)
			return;
	}
	
	// validate each position with raycasts (level of detail on formation, pets vs patrols)
	{
		S32 numValid = 0;
		S32 i, count = eaSize(&pFormationDef->eaSlots);

		for(i = 0; i < count; ++i)
		{			
			AIFormationSlot	*pSlot = pFormation->eaFormationSlots[i];
			Vec3 pos;

			// This optimization is a little sketchy.  I'm going to make sure we get at least 4 valid slots, since if the character does a 180,
			// we still want to make sure there is a nearby valid slot for one of my pets.  It might be a better idea to find the slots that a pet
			// would like to use, and check if they are valid as we go.  [RMARR - 11/8/12]
			if (numValid < MAX(numSlotsNeeded,4))
			{
				AIFormationSlotDef	*pSlotDef = pFormationDef->eaSlots[i];

				setVec3(pos, pSlotDef->x, 0, pSlotDef->z);
				if(pFormationDef->autoSlotted)
				{
					F32 scale = 10;

					if(rrNoOverride && rrNoOverride->fFormationSlotScale!=0)
						scale = rrNoOverride->fFormationSlotScale;

					vecX(pos) *= scale;
					vecZ(pos) *= scale;
				}
				//subVec3(pos, pFormation->vFormationPos, pos);

				quatRotateVec3(pFormation->qFormationRot, pos, pSlot->vOffset);
				addVec3(pSlot->vOffset, pFormation->vFormationPos, pSlot->vPos);

				// cast a ray from the formation leader to this position. if it passes, get the ground pos
				// if those pass, this position is kosher
										
				if (_validateFormationSlotPosition(iPartitionIdx, pFormation, pFormationDef, pLeader, 
													pFormation->vFormationPos, 
													pSlot->vPos))
				{	
					pSlot->isValid = true;
					
					subVec3(pSlot->vPos, pFormation->vFormationPos, pSlot->vOffset);
										
					numValid ++;
				}
				else
				{
					pSlot->isValid = false;
				}
			}
			else
			{
				pSlot->isValid = false;
			}
		}
		
		// if the valid positions haven't changed, and have already been assigned do nothing
		_assignMembersToSlots(pFormation, s_eaMembersToSlot);

		if(!pFormation->bIsPatrol)
		{
			FOR_EACH_IN_EARRAY(s_eaMembersToSlot, Entity, pMember)
			{
				_setFormationMovement(pMember, pFormation, pLeader, false);
			}
			FOR_EACH_END
		}
		else
		{
			FOR_EACH_IN_EARRAY(s_eaMembersToSlot, Entity, pMember)
			{
				if (pMember->aibase->pFormationData)
				{
					pMember->aibase->pFormationData->bIsPatrolling = false;
				}
			}
			FOR_EACH_END
		}
	

	}

	pFormation->bUpdatingForSpawnLocations = false;
		
}

// ---------------------------------------------------------------------------------------------------
void aiFormation_DoFormationMovementForMember(int iPartitionIdx, AITeam *pTeam, Entity *pMember)
{
	Entity *pLeader;
	AIFormation* pFormation;
	
	if (!pMember->aibase->pFormationData)
		return;

	pFormation = pMember->aibase->pFormationData->pFormation;
	devassert(pFormation);
	
	if (!pFormation->bIsPatrol)
	{
		pLeader = entFromEntityRef(iPartitionIdx, pFormation->erLeader);
	}
	else
	{
		pLeader = aiTeamGetLeader(pFormation->pTeam);
	}	
	
	if (!pLeader || pLeader == pMember)
		return;

	if (pMember->aibase->pFormationData)
	{
		pMember->aibase->pFormationData->bIsPatrolling = true;
		_setFormationMovement(pMember, pFormation, pLeader, true);
	}
}

// ---------------------------------------------------------------------------------------------------
void aiFormation_GetDebugFormationPositions(int iPartitionIdx, AIFormation * pFormation, AIDebugFormation *pDebugFormation)
{
	Entity *pLeader;
	if (!pFormation)
		return;
	
	pLeader = entFromEntityRef(iPartitionIdx, pFormation->erLeader);
	if (pLeader)
	{
		entGetPos(pLeader, pDebugFormation->formationPos);
	}
	else
	{
		copyVec3(pFormation->vFormationPos, pDebugFormation->formationPos);
	}


	FOR_EACH_IN_EARRAY_FORWARDS(pFormation->eaFormationSlots, AIFormationSlot, pSlot)
	{
		if (pSlot->isValid)
		{
			AIDebugFormationPosition *dfp = StructCreate(parse_AIDebugFormationPosition);
			Quat q;
			
			quatInverse(pFormation->qFormationRot, q);
			quatRotateVec3(q, pSlot->vOffset, dfp->offset);
			
			dfp->blocked = !!pSlot->isValid;
			dfp->assignee = pSlot->erAssignee;

			eaPush(&pDebugFormation->positions, dfp);
		}
		
	}
	FOR_EACH_END
}

void aiFormation_AddMember(AIFormation * pFormation, Entity * pEntity)
{
	AIConfig* config = aiGetConfig(pEntity, pEntity->aibase);

	assert(pEntity->aibase->pFormationData == NULL);
	pEntity->aibase->pFormationData = aiFormationData_Alloc(pFormation);
	// Entities that are immobile will not be given spots.  This is not really a comprehensive fix to the problem that
	// summoned pets that don't really participate in formations are still getting added.  Also, if a creature is immobile,
	// and becomes mobile, it's not clear that the formation will be updated.  [RMARR - 9/29/11]
	if (!config->movementParams.immobile)
	{
		pFormation->bIsDirty = true;
	}
}

void aiFormation_TryToAddToFormation(int iPartitionIdx, AITeam * pTeam, Entity * pEntity)
{
	if (pEntity->pPlayer)
	{
		// we really shouldn't expect to get a valid formation from a player.  This will just use the default critter def, which isn't necessarily right,
		// so no formations for players.  (The pets should create one later)
		return;
	}

	if (!pTeam->combatTeam)
	{
		Entity *pOwnerEnt = NULL;

		// make sure my existing formation data is a valid one for the team I am joining
		if (pEntity->aibase->pFormationData)
		{
			if (pEntity->aibase->pFormationData->pFormation->pTeam != pTeam)
			{
				// It's not.  A designer probably called SetMyOwner.  All bets are off.
				aiFormationData_Free(pEntity->aibase->pFormationData);
				pEntity->aibase->pFormationData = NULL;
			}
		}
		
		if (pEntity->aibase->pFormationData == NULL)
		{
			if(pEntity->erOwner)
			{
				pOwnerEnt = entFromEntityRef(iPartitionIdx, pEntity->erOwner);
			}

			// It is entirely possible for an owner to have been inherited, but already be dead, or have otherwise left the server.
			// STO sometimes assigns NPCs to people, who could then conceivably log off, but the NPC would stay in the party even if the
			// assigned owner left. Then, if the NPC fires a power, the "owner" of that power would be the guy who already left the server.
			if (pOwnerEnt)
			{
				// I should be added to my owner's formation.
				if (pOwnerEnt->aibase->pFormationData == NULL)
				{
					// My owner doesn't have a formation, so create one for him, based on my def.  There is room for trouble here.
					// The first pet gets to define his owner's formation
					AIConfig *config = aiGetConfig(pEntity, pEntity->aibase);
					if (config && config->pchFormationDef)
					{
						aiFormation_AssignFormationByName(config->pchFormationDef,pTeam,pOwnerEnt);
					}
					else
					{
						aiFormation_AssignDefaultFormation(pTeam,pOwnerEnt);
					}
				}
				else
				{
					aiFormation_AddMember(pOwnerEnt->aibase->pFormationData->pFormation,pEntity);
				}

				// my owner should own his own formation, if he's a player
				devassert(pOwnerEnt->aibase->pFormationData && (!pOwnerEnt->pPlayer || pOwnerEnt->aibase->pFormationData->pFormation->erLeader == pOwnerEnt->myRef));
			}
			else
			{
				// since I'm not owned, just join the aiTeam formation
				if (pTeam->pTeamFormation == NULL)
				{
					AIConfig *config = aiGetConfig(pEntity, pEntity->aibase);
					if (config && config->pchFormationDef)
					{
						aiFormation_AssignFormationByName(config->pchFormationDef,pTeam,aiTeamGetLeader(pTeam));
					}
					else
					{
						aiFormation_AssignDefaultFormation(pTeam,aiTeamGetLeader(pTeam));
					}
					devassert(pTeam->pTeamFormation);
				}
				else
				{
					aiFormation_AddMember(pTeam->pTeamFormation,pEntity);
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------
static AIFormationSlot* _findClosestSlotToPos(AIFormationSlot **eaFormationSlots, 
														const Vec3 vPos, F32 *pfClosestDistSQR)
{
	F32 fClosestSQR = FLT_MAX;
	AIFormationSlot *pClosestSlot = NULL;

	FOR_EACH_IN_EARRAY_FORWARDS(eaFormationSlots, AIFormationSlot, pSlot)
	{
		F32 fDistSQR;

		if (!pSlot->isValid || pSlot->erAssignee)
			continue;

		fDistSQR = distance3Squared(pSlot->vPos, vPos);

		if (fDistSQR < fClosestSQR)
		{
			pClosestSlot = pSlot;
			fClosestSQR = fDistSQR;
		}
	}
	FOR_EACH_END

	*pfClosestDistSQR = fClosestSQR;
	return pClosestSlot;
}

// ---------------------------------------------------------------------------------------------------
// this will assign the best member to the best slot
// by finding the closest member to each slot, then finding the slot that has the furthest closest member
// and that slot gets assigned that member
static Entity * _matchNextToSlot(AIFormation *pFormation, Entity **eaTeamMemberList)
{
	// first, for each member find the closest valid slot to each member
	FOR_EACH_IN_EARRAY(eaTeamMemberList, Entity, pEnt)
	{
		AIFormationData *pFormationData = pEnt->aibase->pFormationData;
		Vec3 vMemberPos; 
		entGetPos(pEnt, vMemberPos);
		if (vec3IsZero(vMemberPos))
		{	// if it is 0, this entity isn't initialized yet, treat him as if he were at the center of the formation
			copyVec3(pFormation->vFormationPos, vMemberPos);
		}
		
		pFormationData->pClosestSlot = _findClosestSlotToPos(pFormation->eaFormationSlots, 
																vMemberPos, &pFormationData->fClosestSlotDist);
		
	}
	FOR_EACH_END

	// next find the member that is furthest away from a slot, and give them that slot
	{
		F32 fFurthestDist = -FLT_MAX;
		Entity *pBestMember = NULL;

		FOR_EACH_IN_EARRAY(eaTeamMemberList, Entity, pEnt)
			// assign the member that has the furthest slot
			AIFormationData *pFormationData = pEnt->aibase->pFormationData;
			if (pFormationData && pFormationData->pClosestSlot && pFormationData->fClosestSlotDist > fFurthestDist )
			{
				fFurthestDist = pFormationData->fClosestSlotDist;
				pBestMember = pEnt;
			}
		FOR_EACH_END

		if (!pBestMember)
			return NULL;

		pBestMember->aibase->pFormationData->pCurrentSlot = pBestMember->aibase->pFormationData->pClosestSlot;
		pBestMember->aibase->pFormationData->pCurrentSlot->erAssignee  = entGetRef(pBestMember);
		return pBestMember;
	}

}

// ---------------------------------------------------------------------------------------------------
static void _assignMembersToSlots(AIFormation *pFormation, Entity **eaMembersNeedingSlots)
{
	static Entity **s_eaTeamMemberList = NULL;

	eaClear(&s_eaTeamMemberList);

	FOR_EACH_IN_EARRAY(eaMembersNeedingSlots, Entity, pEnt)
	{
		if (!pEnt->aibase->pFormationData->pCurrentSlot || 
			!pEnt->aibase->pFormationData->pCurrentSlot->isValid ||
			pEnt->aibase->pFormationData->pCurrentSlot->erAssignee != entGetRef(pEnt))
		{
			pEnt->aibase->pFormationData->pCurrentSlot = NULL;
			eaPush(&s_eaTeamMemberList, pEnt);			
		}
	}
	FOR_EACH_END


	while(eaSize(&s_eaTeamMemberList))
	{
		Entity *pMatchedEnt = _matchNextToSlot(pFormation, s_eaTeamMemberList);
		if (pMatchedEnt)
		{
			eaFindAndRemoveFast(&s_eaTeamMemberList, pMatchedEnt);
		}
		else
		{
			return;
		}
	}

}

// ---------------------------------------------------------------------------------------------------
bool aiFormation_GetSpawnLocationForEntity(Entity *pEntity, Vec3 vPos, Quat qRot)
{
	if (pEntity->aibase && pEntity->aibase->pFormationData &&
		pEntity->aibase->pFormationData->pFormation)
	{
		devassert(pEntity->aibase->pFormationData->pFormation->pTeam);
		if (pEntity->aibase->pFormationData->pFormation->pTeam)
		{
			AIFormation *pFormation = pEntity->aibase->pFormationData->pFormation;
			S32 iPartitionIdx = entGetPartitionIdx(pEntity);
			S32 iNumMembers = eaSize(&pFormation->pTeam->members);
		

			// see if we need to update the formation
			if (pFormation->lastUpdate != ABS_TIME_PARTITION(iPartitionIdx) || 
				pFormation->lastUpdateNumMembers != iNumMembers ||
				gGSLState.bCurrentlyInUGCPreviewMode)
			{
				pFormation->bUpdatingForSpawnLocations = true;
				pFormation->bIsDirty = true;
				aiFormation_UpdateFormation(iPartitionIdx, pFormation);
			}
				
			if (pEntity->aibase->pFormationData->pCurrentSlot)
			{
				//might need to modify code here if we don't want to spawn on ground when movementYOffset is set
				copyVec3(pEntity->aibase->pFormationData->pCurrentSlot->vPos, vPos);
				copyQuat(pFormation->qFormationRot, qRot);
				return true;
			}
		}
	}
	
	return false;
}

#include "aiFormation_h_ast.c"
