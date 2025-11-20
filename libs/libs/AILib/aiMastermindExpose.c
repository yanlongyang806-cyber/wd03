#include "aiMastermindExpose.h"
#include "aiMastermind.h"
#include "Entity.h"
#include "entCritter.h"
#include "WorldLib.h"
#include "WorldColl.h"
#include "beaconPath.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "aiFormation.h"
#include "aiLib.h"
#include "aiTeam.h"
#include "StateMachine.h"
#include "gslEncounter.h"
#include "wlEncounter.h"
#include "wlGroupPropertyStructs.h"
#include "EntityIterator.h"
#include "rand.h"
#include "aiMovement.h"
#include "structDefines.h"
#include "aiMovement_h_ast.h"
#include "CharacterAttribs.h"
#include "Character.h"
#include "AILib_autogen_QueuedFuncs.h"



static void aiMastermindExpose_UpdateAllPlayerInfo(const AIMastermindExposeDef *pDef);
static void aiMastermind_AddPlayer(Entity *e);
static void aiMastermindExpose_FindAndAddAllPlayers();
static MMPlayer* aiMastermind_GetPlayerInfo(Entity *e);

void aiMastermind_DebugPrint(const char *format, ...);

static AIMMExposeManager	s_exposeManager = {0};

static int g_bMastermindExposeDebug = false;
static int g_bMastermindExposeIgnoreLOS = false;

AUTO_CMD_INT(g_bMastermindExposeDebug, aiMMDebug);

AUTO_CMD_INT(g_bMastermindExposeIgnoreLOS, aiMMExposeIgnoreLOS);

// -----------------------------------------------------------------------------------------------------------
__forceinline static S32 getExposeAttribEnum(const char *pchExposeAttribName)
{
	S32 eAttrib;

	if (!pchExposeAttribName)
		return -1;

	eAttrib = StaticDefineIntGetInt(AttribTypeEnum, pchExposeAttribName);
	if(eAttrib < 0 || !IS_NORMAL_ATTRIB(eAttrib))
		return -1;

	return eAttrib;
}

// -----------------------------------------------------------------------------------------------------------
__forceinline static F32 getExposeMaxValue(Entity *e, S32 eExposeAttrib)
{
	if (s_exposeManager.fExposeMax == 0.f)
	{
		AttribPool* pAttribPool = attrib_getAttribPoolByCur(eExposeAttrib);
		if (pAttribPool && pAttribPool->eAttribMax >= 0 && IS_NORMAL_ATTRIB(pAttribPool->eAttribMax))
		{
			s_exposeManager.fExposeMax = *F32PTR_OF_ATTRIB(e->pChar->pattrBasic,pAttribPool->eAttribMax);
			if (s_exposeManager.fExposeMax == 0)
				s_exposeManager.fExposeMax = -1.f;
			return s_exposeManager.fExposeMax;
		}
		
		s_exposeManager.fExposeMax = -1.f;
	}

	return s_exposeManager.fExposeMax;
}

// -----------------------------------------------------------------------------------------------------------
static F32 getExposeForPlayerEnt(const AIMastermindExposeDef *pDef, Entity *e)
{
	if (!s_exposeManager.bOverrideExpose)
	{
		S32 eAttrib = getExposeAttribEnum(pDef->pchExposeAttribName);
		F32 exposeVal;
		

		if (eAttrib < 0)
			return 0.f;
		
		exposeVal = *F32PTR_OF_ATTRIB(e->pChar->pattrBasic,eAttrib);
#ifdef NORMALIZE_EXPOSE
		{
			F32 exposeMax;
			exposeMax = getExposeMaxValue(e, eAttrib);
					
			if (exposeMax > 0.f)
			{	// if we have a valid max, normalize and then multiply by the number of expose levels
				exposeVal = exposeVal / exposeMax;
				exposeVal *= eaSize(&pDef->eaExposeLevels);
			}
		}
#endif	
		return exposeVal;
	}
	else
	{
		return s_exposeManager.fOverrideExpose;
	}
}


// -----------------------------------------------------------------------------------------------------------
static void queuedSpawn_AllocQueuedSpawnPools(S32 count)
{
	S32 i;
	for (i = 0; i < count; ++i)
	{
		MMQueuedSpawn *pQueuedSpawn = calloc(1, sizeof(MMQueuedSpawn));

		if (pQueuedSpawn)
		{
			eaPush(&s_exposeManager.eaQueuedSpawnPool, pQueuedSpawn);
		}
	}
}

// -----------------------------------------------------------------------------------------------------------
static void queuedSpawn_Free(MMQueuedSpawn *p)
{
	if (p)
	{
		eaDestroy(&p->eaPotentialEncounters);
		eaiDestroy(&p->earNearbyEnts);
		free(p);
	}
}

// -----------------------------------------------------------------------------------------------------------
// puts the queued spawn struct back in your pool
static void queuedSpawn_ReleaseQueuedSpawn(MMQueuedSpawn *p)
{
	eaPush(&s_exposeManager.eaQueuedSpawnPool, p);
}


// -----------------------------------------------------------------------------------------------------------
static MMQueuedSpawn* queuedSpawn_GetQueuedSpawn()
{
	if (!eaSize(&s_exposeManager.eaQueuedSpawnPool))
	{
		S32 count = eaSize(&s_exposeManager.eaActiveQueuedSpawns) + 1;
		queuedSpawn_AllocQueuedSpawnPools(count);
		if (!eaSize(&s_exposeManager.eaQueuedSpawnPool))
			return NULL;
	}

	// 
	{
		MMQueuedSpawn *p = eaPop(&s_exposeManager.eaQueuedSpawnPool);
		eaClear(&p->eaPotentialEncounters);
		eaiClear(&p->earNearbyEnts);

		devassert(p);
		return p;
	} 
}

// -----------------------------------------------------------------------------------------------------------
static void MMSentEncounter_Release(MMExposeSentEncounter *p)
{
	free(p);
}

// -----------------------------------------------------------------------------------------------------------
void aiMastermindExpose_Initialize()
{
	if (s_exposeManager.bInitialized)
	{
		return;
	}

	ZeroStruct(&s_exposeManager);

	s_exposeManager.bInitialized = true;
	s_exposeManager.bEnabled = true;
	queuedSpawn_AllocQueuedSpawnPools(8);
	s_exposeManager.timeatLastUpdate = ABS_TIME;

	aiMastermindExpose_FindAndAddAllPlayers();
	
}

// -----------------------------------------------------------------------------------------------------------
void aiMastermindExpose_Shutdown()
{
	eaDestroyEx(&s_exposeManager.eaQueuedSpawnPool, queuedSpawn_Free);
	eaDestroyEx(&s_exposeManager.eaActiveQueuedSpawns, queuedSpawn_Free);

	ZeroStruct(&s_exposeManager);
}


// -----------------------------------------------------------------------------------------------------------
static const AIMMExposureLevelDef* aiMastermindExpose_GetExposeLevel(const AIMastermindExposeDef *pDef, F32 expose, S32 *pExposeIndex)
{
	S32 count = eaSize(&pDef->eaExposeLevels);
	F32 fExposureMin = 0.f;
	S32 i;
	if (!count) 
		return NULL;
	
	for (i = 0; i < count; ++i)
	{
		AIMMExposureLevelDef * pLvlDef = pDef->eaExposeLevels[i];
		if (expose >= fExposureMin && expose <= fExposureMin + pLvlDef->fExposureRange)
		{
			if (pExposeIndex) *pExposeIndex = i; 
			return pLvlDef;
		}
		fExposureMin += pLvlDef->fExposureRange;
	}

	if (pExposeIndex) *pExposeIndex = count - 1; 
	return eaTail(&pDef->eaExposeLevels);
}

// -----------------------------------------------------------------------------------------------------------
static MMExposeSentEncounter** aiMastermindExpose_GetSentEncountersForPlayer(EntityRef erPlayer)
{
	static MMExposeSentEncounter** s_eaSentEncs = NULL;

	eaClear(&s_eaSentEncs);

	FOR_EACH_IN_EARRAY(s_exposeManager.eaSentEncounters, MMExposeSentEncounter, pSentEnc)
	{
		if (pSentEnc->erPlayer == erPlayer)
		{
			eaPush(&s_eaSentEncs, pSentEnc);
		}
	}
	FOR_EACH_END

	return s_eaSentEncs;
}

// -----------------------------------------------------------------------------------------------------------
static S32 aiMastermindExpose_CountSentEncountersForPlayer(EntityRef erPlayer)
{
	S32 count = 0;
	FOR_EACH_IN_EARRAY(s_exposeManager.eaSentEncounters, MMExposeSentEncounter, pSentEnc)
	{
		if (pSentEnc->erPlayer == erPlayer)
			count++;
	}
	FOR_EACH_END

	return count;
}

// -----------------------------------------------------------------------------------------------------------
static MMExposeSentEncounter* aiMastermindExpose_GetSentEncounterInfoForTeam(AITeam *pTeam)
{
	// todo maybe make faster: hash or save a handle to this on the team
	FOR_EACH_IN_EARRAY(s_exposeManager.eaSentEncounters, MMExposeSentEncounter, pSentEnc)
		if (pSentEnc->pTeam == pTeam)
			return pSentEnc;
	FOR_EACH_END

	return NULL;
}

// -----------------------------------------------------------------------------------------------------------
static Entity** aiMastermindExpose_GetNearbyPlayers(int iPartitionIdx, const Vec3 vPos, F32 fDist)
{
	static Entity **s_eaNearbyPlayers = NULL;
	#define MAX_SEARCH_RADIUS
	if (fDist > 300.f)
		fDist = 300.f;

	eaClear(&s_eaNearbyPlayers);
	entGridProximityLookupExEArray(iPartitionIdx, vPos, &s_eaNearbyPlayers, fDist, ENTITYFLAG_IS_PLAYER, 0, NULL);
	return s_eaNearbyPlayers;
}

// -----------------------------------------------------------------------------------------------------------
#define CAST_DISTANCE_THRESHOLD	250.f
#define CAST_HEIGHT	2.5f
__forceinline static bool _checkLOSToPlayer(Entity* ent, const Vec3 vCastFrom)
{
	WorldCollCollideResults results = {0};
	Vec3 vCastTo;

	entGetPos(ent, vCastTo);

	if (distance3Squared(vCastFrom, vCastTo) > SQR(CAST_DISTANCE_THRESHOLD))
		return true;

	vCastTo[1] += CAST_HEIGHT;

	if(! worldCollideRay(entGetPartitionIdx(ent), vCastFrom, vCastTo, WC_QUERY_BITS_WORLD_ALL, &results))
	{	// hit something
		return false;
	}
	return true;
}

/*
// -----------------------------------------------------------------------------------------------------------
// for each entity in the array, does a single raycast from the given position to the entity
//  returns false if any entities are in LOS
// (entities further than a threshold assume no LOS)
static bool aiMastermindExpose_CheckLOSToPlayers(Entity** eaEntities, const Vec3 vEncPos)
{
	

	FOR_EACH_IN_EARRAY(eaEntities, Entity, ent)
	{
		if (!_checkLOSToPlayer(ent, vCastFrom))
		{
			return false;
		}
	}
	FOR_EACH_END

	return true;
}
*/

// ------------------------------------------------------------------------------------------------------------------
static bool _sendAtTargetEntity(SA_PARAM_NN_VALID Entity *critter, const Vec3 vPos)
{
	return aiMovementSetTargetPosition(critter, critter->aibase, vPos, NULL, 0);
}


// ------------------------------------------------------------------------------------------------------------------
static bool aiMastermind_UpdateSentEncToPos(MMExposeSentEncounter* pSentEnc, const Vec3 vPos)
{
	// todo: just send leader and everyone else should just follow in formation
	pSentEnc->timeatLastPlayerPos = ABS_TIME;
	if (nearSameVec3(vPos, pSentEnc->vLastSentPos))
		return true;

	copyVec3(vPos, pSentEnc->vLastSentPos);
	
	if (!pSentEnc->FSMControlled)
	{
		bool bPathFound = true;
		FOR_EACH_IN_EARRAY(pSentEnc->pTeam->members, AITeamMember, pMember)
		{
			Entity *pEntity = pMember->memberBE; 
			if (!aiIsEntAlive(pEntity))
				continue;
			if (!aiIsInCombat(pEntity))
			{
				if (!_sendAtTargetEntity(pEntity, pSentEnc->vLastSentPos))
					bPathFound = false;
			}
		}
		FOR_EACH_END

		pSentEnc->pathFailed = !bPathFound;
		return bPathFound;
	}
	

	return true;
}


// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_SendAITeam(const AIMastermindExposeDef *pDef, MMExposeSentEncounter *pEnc, 
										  Entity *ePlayer, const Vec3 vSendToPos)
{
	if (!aiTeamInCombat(pEnc->pTeam))
	{
		devassert(pDef->pParentMMDef);
		pEnc->FSMControlled = pDef->pParentMMDef->pchSentEncounterFSMOverride != NULL;

		FOR_EACH_IN_EARRAY(pEnc->pTeam->members, AITeamMember, pMember)
		{
			Entity *e = pMember->memberBE;
			aiMastermind_PrimeEntityForSending(e, ePlayer, pDef->pParentMMDef, true);
		}
		FOR_EACH_END

		{
			Vec3 vPos;

			if (vSendToPos)
				copyVec3(vSendToPos, vPos);
			else
				entGetPos(ePlayer, vPos);
			
			aiMastermind_UpdateSentEncToPos(pEnc, vPos);
		}
	}
}

// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_RevertAITeam(const AIMastermindExposeDef *pDef, MMExposeSentEncounter *pEnc)
{
	FOR_EACH_IN_EARRAY(pEnc->pTeam->members, AITeamMember, pMember)
	{
		Entity *e = pMember->memberBE;
		aiMastermind_UndoMastermindPriming(e, pEnc->erPlayer, pDef->pParentMMDef);
	}
	FOR_EACH_END
}

// -----------------------------------------------------------------------------------------------------------
static MMExposeSentEncounter* aiMastermindExpose_AddTrackedEncounter(int iPartitionIdx,
																	 GameEncounter *pEncounter, 
																	 AITeam *pTeam, Entity *ePlayer,
																	 S32 exposeLevel, const Vec3 vOverrideSpawnPos)
{
	MMExposeSentEncounter *pSentEncounter = calloc(1, sizeof(MMExposeSentEncounter));
	if (pSentEncounter)
	{
		pSentEncounter->pEncounter = pEncounter;
		pSentEncounter->iPartitionIdx = iPartitionIdx;
		pSentEncounter->pTeam = pTeam;
		pSentEncounter->erPlayer = entGetRef(ePlayer);
		pSentEncounter->eState = MMSentEncounterState_SEARCHING;
	
		pSentEncounter->exposeLevelSpawn = exposeLevel;
		pSentEncounter->staticEncounter = vOverrideSpawnPos != NULL;
		
		if (!vOverrideSpawnPos)
		{
			encounter_GetPosition(pEncounter, pSentEncounter->vSpawnPosition);
		}
		else
		{
			copyVec3(vOverrideSpawnPos, pSentEncounter->vSpawnPosition);
		}

		pSentEncounter->timeatSent = ABS_TIME;
		pSentEncounter->timeatLastPlayerPos = ABS_TIME;
		
		eaPush(&s_exposeManager.eaSentEncounters, pSentEncounter);
		return pSentEncounter;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static AITeam* MMSentEncounter_getAITeam(MMExposeSentEncounter *pSentEnc)
{
	GameEncounterPartitionState *pState = encounter_GetPartitionState(pSentEnc->iPartitionIdx, pSentEnc->pEncounter);
	FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, pEntity)
	{
		AITeam *pTeam = aiTeamGetAmbientTeam(pEntity, pEntity->aibase);
		if (pTeam && pSentEnc->pTeam == pTeam)
		{
			return pTeam;
		}
	}
	FOR_EACH_END

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_ReturnAmbushTeamToSpawnPos(MMExposeSentEncounter *pSentEnc, AITeam *pTeam)
{
	FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
	{
		Entity *pEntity = pMember->memberBE;
		if (!aiIsEntAlive(pEntity))
			continue;

		if (!aiIsInCombat(pEntity))
		{
			Vec3 vSendToPos;
			addVec3(pEntity->aibase->spawnOffset, pSentEnc->vSpawnPosition, vSendToPos);
			aiMovementSetTargetPosition(pEntity, pEntity->aibase, vSendToPos, NULL, AI_MOVEMENT_TARGET_CRITICAL);
		}
	}
	FOR_EACH_END

}


// ------------------------------------------------------------------------------------------------------------------
static void _destroySentEncounter(MMExposeSentEncounter *pSentEnc, AITeam *pTeam)
{
	if (!pSentEnc->staticEncounter)
	{	// 
		FOR_EACH_IN_EARRAY(pTeam->members, AITeamMember, pMember)
			entDie(pMember->memberBE, 0, false, false, NULL);
		FOR_EACH_END
	}
}

// -----------------------------------------------------------------------------------------------------------
static void MMExposeSentEncounter_SetEndPursue(MMExposeSentEncounter *pSentEnc, MMEndPursueReason eReason)
{
	if (!pSentEnc->shouldEndPursue)
	{
		pSentEnc->shouldEndPursue = true;
		pSentEnc->endPursueReason = eReason;
	}
}

// -----------------------------------------------------------------------------------------------------------
static bool aiMastermindExpose_UpdateSentEncounter(const AIMastermindExposeDef *pDef, MMExposeSentEncounter *pSentEnc)
{
	Entity *ePlayer = entFromEntityRef(PARTITION_UNINITIALIZED, pSentEnc->erPlayer);
	AITeam *pAITeam = MMSentEncounter_getAITeam(pSentEnc);
	AITeam *pCombatTeam = NULL;
	S32 entsAlive = 0;
	if (!pAITeam)
		return false;
	
	if (!eaSize(&pAITeam->members))
	{
		_destroySentEncounter(pSentEnc, pAITeam);
		return false;
	}

	FOR_EACH_IN_EARRAY(pAITeam->members, AITeamMember, pMember)
	{
		if (!pCombatTeam)
		{
			pCombatTeam = aiTeamGetCombatTeam(pMember->memberBE, pMember->memberBE->aibase);
		}
		if (aiIsEntAlive(pMember->memberBE))
			entsAlive++;
	}
	FOR_EACH_END

	if (!pCombatTeam)
	{
		pCombatTeam = pAITeam;
	}
	
	if (!ePlayer || !aiIsEntAlive(ePlayer))
		pSentEnc->playerHasDied = true;

	if (pSentEnc->waitingForDespawnCount >= entsAlive)
	{
		if (pSentEnc->staticEncounter)
		{	// revert the encounter back to it's normal FSM	
			aiMastermindExpose_RevertAITeam(pDef, pSentEnc);
		}
		else
		{
			_destroySentEncounter(pSentEnc, pAITeam);
		}
		return false;
	}

	if (!pSentEnc->shouldEndPursue && 
		pDef->fSentEncounterDespawnTimeout > 0 &&
		ABS_TIME_SINCE(pSentEnc->timeatSent) > SEC_TO_ABS_TIME(pDef->fSentEncounterDespawnTimeout))
	{
		MMExposeSentEncounter_SetEndPursue(pSentEnc, MMEndPursueReason_TIMEOUT);
	}

	switch (pSentEnc->eState)
	{
		xcase MMSentEncounterState_SEARCHING:
		{
			if (pSentEnc->shouldEndPursue)
			{
				pSentEnc->eState = MMSentEncounterState_DONE;
				return true;
			}

			/*
			if (pSentEnc->hadCombatWithPlayer && pCombatTeam->combatState == AITEAM_COMBAT_STATE_LEASH)
			{	// we are leashing, and were in combat with the player
				// he must have gotten away, end the pursue
				pSentEnc->eState = MMSentEncounterState_DONE;
				MMExposeSentEncounter_SetEndPursue(pSentEnc, MMEndPursueReason_PLAYER_LEASH);
				return true;
			}
			*/

			if (pSentEnc->playerHasDied)
			{	// player we are pursuing is dead
				pSentEnc->eState = MMSentEncounterState_DONE;
				MMExposeSentEncounter_SetEndPursue(pSentEnc, MMEndPursueReason_PLAYER_DEAD);

				if (!pSentEnc->FSMControlled)
				{
					aiMastermindExpose_ReturnAmbushTeamToSpawnPos(pSentEnc, pAITeam);
				}
				return true;
			}

			if (getExposeForPlayerEnt(pDef, ePlayer) == 0)
			{
				pSentEnc->eState = MMSentEncounterState_DONE;
				MMExposeSentEncounter_SetEndPursue(pSentEnc, MMEndPursueReason_EXPOSE_WIPE);
				return true;
			}
			
			// check to see if the sent encounter is in combat
			if (aiTeamInCombat(pCombatTeam))
			{	// may or may not be in combat with intended target
				pSentEnc->eState = MMSentEncounterState_COMBAT;
				return true;
			}

			// check if we should update the position that the encounter is going to
			//if (ePlayer->pChar->pattrBasic->fPerceptionStealth == 0 && 
			//	ABS_TIME_PASSED(pSentEnc->timeatLastPlayerPos, pDef->fPlayerPosUpdateFrequency))
			
			{
				MMPlayer* pMMPlayer = aiMastermind_GetPlayerInfo(ePlayer);
				
				if (pMMPlayer)
				{
					aiMastermind_UpdateSentEncToPos(pSentEnc, pMMPlayer->vMenacePos);
				}
			}
		}

		xcase MMSentEncounterState_COMBAT:
		{
			if (!aiTeamInCombat(pCombatTeam))
			{
				pSentEnc->eState = MMSentEncounterState_SEARCHING;
				return true;
			}

			if (!pSentEnc->hadCombatWithPlayer)
			{	// see if we have the seek target as a legal target
				if (aiTeamIsTargetLegalTarget(pCombatTeam, ePlayer))
					pSentEnc->hadCombatWithPlayer = true;
			}
		}
		
		xcase MMSentEncounterState_DONE:
		{
			// dispose of this encounter properly
			// if it was an already spawned encounter, assign the old FSM
			if (!pSentEnc->FSMControlled)
			{
				if (pCombatTeam->combatState != AITEAM_COMBAT_STATE_LEASH)
				{
					_destroySentEncounter(pSentEnc, pAITeam);
					return false;
				}
			}
			
		}
	}

	return true;
}

// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_UpdateSentEncounters(const AIMastermindExposeDef *pDef)
{
	FOR_EACH_IN_EARRAY(s_exposeManager.eaSentEncounters, MMExposeSentEncounter, pSentEnc)
	{
		if (! aiMastermindExpose_UpdateSentEncounter(pDef, pSentEnc))
		{
			eaRemoveFast(&s_exposeManager.eaSentEncounters, ipSentEncIndex);
			MMSentEncounter_Release(pSentEnc);
		}
	}
	FOR_EACH_END
}


// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_SpawnEncounterForPlayer(const AIMastermindExposeDef *pDef,
													   int iPartitionIdx,
													   GameEncounter *pEncounter, 
													   Entity *ePlayer, 
													   S32 exposeLevel,
													   const char *pcEncounterGroupName)
{
	GameEncounterPartitionState *pState;
	MMPlayer *pMMPlayer;
	F32 *pvLastPlayerPos = NULL;

	pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
	encounter_SpawnEncounter(pEncounter, pState);

	pMMPlayer = aiMastermind_GetPlayerInfo(ePlayer);
	if (pMMPlayer)
	{
		pMMPlayer->timeLastSpawnForPlayer = ABS_TIME;
		pvLastPlayerPos = pMMPlayer->vMenacePos;
	}

	// get all the spawned entities and set them up for the ambush
	// (Make this group go in formation)
	{

		FOR_EACH_IN_EARRAY(pState->eaEntities, Entity, ent)
		{
			// only the ones that we just spawned. 
			if (!ent->aibase->hadFirstTick)
			{
				AITeam *pTeam = aiTeamGetAmbientTeam(ent, ent->aibase);
				MMExposeSentEncounter* pSentEnc;
				pSentEnc = aiMastermindExpose_AddTrackedEncounter(iPartitionIdx, pEncounter, pTeam, ePlayer, exposeLevel, NULL);
			
				if (pSentEnc)
					aiMastermindExpose_SendAITeam(pDef, pSentEnc, ePlayer, pvLastPlayerPos);
				break;
			}
		}
		FOR_EACH_END
	}

	aiMastermind_DebugPrint("Sending Wave: Spawned a new encounter at loc (%.1f,%.1f,%.1f) from group %s.\n", 
							vecParamsXYZ(pEncounter->pWorldEncounter->encounter_pos),
							pcEncounterGroupName ? pcEncounterGroupName : "not-specified" );
}


// -----------------------------------------------------------------------------------------------------------
#define MAX_CASTS	5
// returns true if the LOS check passes or fails, returns false if it is not done casting
static bool aiMastermindExpose_ContinueLOSChecks(const AIMastermindExposeDef *pDef, 
												 MMQueuedSpawn *pQueuedSpawn, Entity *ePlayer, S32 *didSpawnOut)
{
	GameEncounter *pEncounter = eaGet(&pQueuedSpawn->eaPotentialEncounters, pQueuedSpawn->iCurrentEnc);
	Vec3 vCastFrom;
	S32 x, count;
	S32	numCasts = 0;

	if (didSpawnOut)
		*didSpawnOut = false;

	if (!pEncounter)
		return true;

	encounter_GetPosition(pEncounter, vCastFrom);
	vCastFrom[1] += CAST_HEIGHT;

	ANALYSIS_ASSUME(pQueuedSpawn->earNearbyEnts);

	// we are in the middle of checking LOS from players to encounters
	count = eaiSize(&pQueuedSpawn->earNearbyEnts);
	for (x = pQueuedSpawn->iCurrentEnt; x < count; ++x, ++pQueuedSpawn->iCurrentEnt)
	{
		Entity *e = entFromEntityRef(PARTITION_UNINITIALIZED, pQueuedSpawn->earNearbyEnts[x]);

		if (!e)
			continue;

		numCasts++;
		if (!_checkLOSToPlayer(e, vCastFrom))
		{
			pQueuedSpawn->iCurrentEnt = 0;
			eaiClear(&pQueuedSpawn->earNearbyEnts);
			return false;
		}
		if (numCasts >= MAX_CASTS)
			break;
	}

	if (pQueuedSpawn->iCurrentEnt != count)
		return true;

	// if we get here, this encounter is a perfect place to spawn!
	aiMastermindExpose_SpawnEncounterForPlayer(pDef, entGetPartitionIdx(ePlayer), pEncounter, ePlayer, 
												pQueuedSpawn->exposeLevel,
												pQueuedSpawn->pchEncGroupName);
	if (didSpawnOut)
		*didSpawnOut = true;
	
	return true;
}

static void MMQueuedSpawn_GetNearbyEncountersToQuery(const AIMastermindExposeDef *pDef, MMQueuedSpawn* pSpawn);
static bool aiMastermindExpose_FindSpawnedEncounter(const AIMastermindExposeDef *pDef, const char *pchEncGroupName, Entity *ePlayer, S32 exposeLevel);

// -----------------------------------------------------------------------------------------------------------
// Returns true if this queued spawn is done for whatever reason and should be removed from the queued spawn list
static bool aiMastermindExpose_UpdateQueuedSpawn(const AIMastermindExposeDef *pDef,
												 MMQueuedSpawn *pQueuedSpawn, Entity *ePlayer)
{
#define MAX_ENCOUNTER_UPDATES	5
	S32	numCasts = 0;

	// check if the queued spawn request should timeout 
	if (ABS_TIME_PASSED(pQueuedSpawn->timeatSearchStart, 7.5f))
		return true;

	if (eaSize(&pQueuedSpawn->eaPotentialEncounters) == 0)
	{
		if (!pQueuedSpawn->didCheckForSpawnedEncounters)
		{
			pQueuedSpawn->didCheckForSpawnedEncounters = true;

			// check for nearby patrolling encounters first
			if (aiMastermindExpose_FindSpawnedEncounter(pDef, pQueuedSpawn->pchEncGroupName, 
														ePlayer, pQueuedSpawn->exposeLevel))
				return true;

			return false;
		}
				
		MMQueuedSpawn_GetNearbyEncountersToQuery(pDef, pQueuedSpawn);	
		if (eaSize(&pQueuedSpawn->eaPotentialEncounters) == 0)
		{	// no spawns found
			return true;
		}
	}


	// check if the queued spawn is still checking nearby players for an encounter
	if (pQueuedSpawn->iCurrentEnt >= 0 && eaiSize(&pQueuedSpawn->earNearbyEnts))
	{
		S32 didSpawn = false;
		if (aiMastermindExpose_ContinueLOSChecks(pDef, pQueuedSpawn, ePlayer, &didSpawn))
		{
			return (didSpawn);
		}
	}

	// 
	entGetPos(ePlayer, pQueuedSpawn->vLastPlayerPos);
	
	// start checking for the next encounter
	{
		S32 i, encounterCount;

		encounterCount = eaSize(&pQueuedSpawn->eaPotentialEncounters);
		if (pQueuedSpawn->iCurrentEnc >= encounterCount)
			pQueuedSpawn->iCurrentEnc = 0;

		ANALYSIS_ASSUME(pQueuedSpawn->eaPotentialEncounters);

		for (i = pQueuedSpawn->iCurrentEnc; i < encounterCount; ++i, ++pQueuedSpawn->iCurrentEnc)
		{
			GameEncounter *pEncounter = pQueuedSpawn->eaPotentialEncounters[i];
			Vec3 vCastFrom;
			Vec3 vEncPos;
			Entity** eaPlayers;

			encounter_GetPosition(pEncounter, vEncPos);
			if (pDef->fSpawnMinLockoutRadius)
			{
				F32 fEncDistSQ;
				// check to see if the encounter is too close to the player
				fEncDistSQ = distance3Squared(vEncPos, pQueuedSpawn->vLastPlayerPos);
				if (fEncDistSQ <= SQR(pDef->fSpawnMinLockoutRadius))
					continue; // too close to spawn
			}

			copyVec3(vEncPos, vCastFrom);
			vCastFrom[1] += CAST_HEIGHT;

			// check to see if any nearby players can "see" the encounter
			// 
			if (!g_bMastermindExposeIgnoreLOS && pDef->bUseLOSChecksByDefault)
			{
				S32 x, playerCount;
				bool bLOSFailed = false;
				eaPlayers = aiMastermindExpose_GetNearbyPlayers(entGetPartitionIdx(ePlayer), vEncPos, CAST_DISTANCE_THRESHOLD);
				playerCount = eaSize(&eaPlayers);
				for (x = 0; x < playerCount; ++x)
				{
					Entity *e = eaPlayers[x];
					numCasts++;
					if (!_checkLOSToPlayer(e, vCastFrom))
					{
						bLOSFailed = true;
						break;
					}
				
					if (numCasts >= MAX_CASTS)
						break;
				}
				
				if (bLOSFailed)
					continue;

				if (x != playerCount)
				{	// did not finish casting vs all the players
					// save the rest of the entities to test vs later
					S32 numEnts = playerCount - x;
					pQueuedSpawn->iCurrentEnt = 0;
					eaiClear(&pQueuedSpawn->earNearbyEnts);
					eaiSetSize(&pQueuedSpawn->earNearbyEnts, numEnts);
					for (x; x < playerCount; ++x)
					{	
						EntityRef entRef = entGetRef(eaPlayers[x]);
						eaiPush(&pQueuedSpawn->earNearbyEnts, entRef);
					}

					break;
				}
			}

			// if we get here, this encounter is a perfect place to spawn!
			{
				aiMastermindExpose_SpawnEncounterForPlayer(pDef, entGetPartitionIdx(ePlayer), pEncounter, ePlayer, 
															pQueuedSpawn->exposeLevel,
															pQueuedSpawn->pchEncGroupName);
				return true;
			}

		}

		
	}


	return false;
}

// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_UpdateQueueSpawns(const AIMastermindExposeDef *pDef)
{
	S32 i = 0, count = eaSize(&s_exposeManager.eaActiveQueuedSpawns);
	for (i = 0; i < count;)
	{
		MMQueuedSpawn *pQueuedSpawn = s_exposeManager.eaActiveQueuedSpawns[i];

		// check if the player still exists and is alive
		Entity *ePlayer = entFromEntityRef(PARTITION_UNINITIALIZED, pQueuedSpawn->erPlayer);
		if (!ePlayer)
		{	// remove this 
			count--;
			queuedSpawn_ReleaseQueuedSpawn(pQueuedSpawn);
			eaRemove(&s_exposeManager.eaQueuedSpawnPool, i);
			continue;
		}
		
		if (aiMastermindExpose_UpdateQueuedSpawn(pDef, pQueuedSpawn, ePlayer))
		{
			count--;
			queuedSpawn_ReleaseQueuedSpawn(pQueuedSpawn);
			eaRemove(&s_exposeManager.eaActiveQueuedSpawns, i);
			continue;
		}
		break;
	}
}

// -----------------------------------------------------------------------------------------------------------
__forceinline static S32 aiMastermindExpose_IsEncounterGroupCorrectTier(GameEncounter *pEncounter, const char *pchEncGroupName)
{
	return	pEncounter->pWorldEncounter->properties && 
			pEncounter->pWorldEncounter->properties->pSpawnProperties && 
			pEncounter->pWorldEncounter->properties->pSpawnProperties->pcSpawnTag == pchEncGroupName;
}

// -----------------------------------------------------------------------------------------------------------
static void MMQueuedSpawn_GetNearbyEncountersToQuery(const AIMastermindExposeDef *pDef, MMQueuedSpawn* pSpawn)
{
	GameEncounter **eaEncs;
	S32 count;

	eaEncs = encounter_GetEncountersWithinDistance(pSpawn->vSearchCenter, pDef->fSpawnSearchRadius);
	
	eaClear(&pSpawn->eaPotentialEncounters);
	
	FOR_EACH_IN_EARRAY(eaEncs, GameEncounter, pEnc)
	{
		if (aiMastermindExpose_IsEncounterGroupCorrectTier(pEnc, pSpawn->pchEncGroupName))
		{
			if (pEnc->pWorldEncounter->properties->pSpawnProperties->eMastermindSpawnType != WorldEncounterMastermindSpawnType_None)
			{
				eaPush(&pSpawn->eaPotentialEncounters, pEnc);
			}
		}
	}
	FOR_EACH_END


	count = eaSize(&pSpawn->eaPotentialEncounters);
	if (count)
		pSpawn->iCurrentEnc = rand()%count;
}

// -----------------------------------------------------------------------------------------------------------
// tries to find an already spawned encounter to send at the player
// returns true if one was found and sent at the player
static bool aiMastermindExpose_FindSpawnedEncounter(const AIMastermindExposeDef *pDef, const char *pchEncGroupName,
													Entity *ePlayer, S32 exposeLevel)
{
	Vec3 vPlayerPos;
	static Entity **s_eaProxEnts = NULL;
	entGetPos(ePlayer, vPlayerPos);
		
	eaClear(&s_eaProxEnts);

	entGridProximityLookupExEArray(entGetPartitionIdx(ePlayer), vPlayerPos, &s_eaProxEnts, 
									pDef->fSpawnSearchRadius, 0, 
									ENTITYFLAG_CIVILIAN | ENTITYFLAG_IS_PLAYER | ENTITYFLAG_DEAD | 
									ENTITYFLAG_DONOTDRAW | ENTITYFLAG_DONOTSEND | ENTITYFLAG_IGNORE, 
									ePlayer);
		
	FOR_EACH_IN_EARRAY(s_eaProxEnts, Entity, e)
	{
		GameEncounter *pEncounter;
		AITeam *team;
		AITeam *combatTeam;
		if (!e->pCritter)
			continue;
		
		pEncounter = e->pCritter->encounterData.pGameEncounter;

		if (!pEncounter || !encounter_isMastermindType(pEncounter) ||
			!aiMastermindExpose_IsEncounterGroupCorrectTier(pEncounter, pchEncGroupName))
			continue;

		// find if this entity team is currently being sent 
		team = aiTeamGetAmbientTeam(e, e->aibase);
		if (team)
		{
			MMExposeSentEncounter *pSentEnc;
			combatTeam = aiTeamGetCombatTeam(e, e->aibase);
			if (aiTeamInCombat(combatTeam))
				continue; // 
		
			pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
			if (!pSentEnc)
			{	// not already sent, create a sent encounter for this team
				Vec3 vPos;
				F32 *pvSendToPos = NULL;
				MMPlayer *pMMPlayer = aiMastermind_GetPlayerInfo(ePlayer);

				if (pMMPlayer)
					pvSendToPos = pMMPlayer->vMenacePos;

				entGetPos(e, vPos);
				pSentEnc = aiMastermindExpose_AddTrackedEncounter(entGetPartitionIdx(ePlayer), pEncounter, team, ePlayer, exposeLevel, vPos);
				
				if(pSentEnc)
				{
					
					aiMastermindExpose_SendAITeam(pDef, pSentEnc, ePlayer, pvSendToPos);
					return true;
				}
			}
		}
	}
	FOR_EACH_END

	return false;
}

// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_QueueSpawnForPlayer(const AIMastermindExposeDef *pDef, const char *pchEncGroupName,
												   Entity *ePlayer, S32 exposeLevel)
{
	MMQueuedSpawn* pSpawn;
	if (eaSize(&s_exposeManager.eaActiveQueuedSpawns) > 20)
		return;
	
	// if none are found, then queue a spawn
	pSpawn = queuedSpawn_GetQueuedSpawn();
	if (!pSpawn)
		return;

	pSpawn->exposeLevel = exposeLevel;
	pSpawn->erPlayer = entGetRef(ePlayer);
	pSpawn->iCurrentEnt = 0;
	pSpawn->iCurrentEnc = 0;
	pSpawn->didCheckForSpawnedEncounters = false;

	entGetPos(ePlayer, pSpawn->vSearchCenter);
	copyVec3(pSpawn->vSearchCenter, pSpawn->vLastPlayerPos);
	pSpawn->timeatLastPlayerPos = ABS_TIME;
	pSpawn->timeatSearchStart = ABS_TIME;
	pSpawn->pchEncGroupName = pchEncGroupName;
	eaClear(&pSpawn->eaPotentialEncounters);

	eaPush(&s_exposeManager.eaActiveQueuedSpawns, pSpawn);

}

// -----------------------------------------------------------------------------------------------------------
static bool aiMastermindExpose_HasQueuedSpawnForPlayer(EntityRef erPlayer)
{
	FOR_EACH_IN_EARRAY(s_exposeManager.eaActiveQueuedSpawns, MMQueuedSpawn, pQueuedSpawns)
	{
		if (pQueuedSpawns->erPlayer == erPlayer)
			return true;
	}
	FOR_EACH_END

	return false;
}

// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_ProcessExposeForPlayer(const AIMastermindExposeDef *pDef)
{
	// check what exposure level
	// see if we should spawn guys,
	FOR_EACH_IN_EARRAY(s_exposeManager.eaPlayers, MMPlayer, pMMPlayer)
	{
		if (!pMMPlayer->bIsDead)
		{
			S32 exposeLvlIdx = 0;
			const AIMMExposureLevelDef *pExposeLevel = aiMastermindExpose_GetExposeLevel(pDef, pMMPlayer->fExposeLevel, &exposeLvlIdx);
			if (pExposeLevel->iNumEncounters)
			{
				if (!ABS_TIME_PASSED(pMMPlayer->timeLastSpawnForPlayer, pExposeLevel->fSpawnCooldown))
					continue;
				pMMPlayer->timeLastSpawnForPlayer = ABS_TIME + SEC_TO_ABS_TIME(4.f);

				// check to see if we have a queued spawn for this player
				// if we do, we're waiting for it to spawn
				if (aiMastermindExpose_HasQueuedSpawnForPlayer(pMMPlayer->erPlayer))
					continue; 

				// check to see if we have any sent encounters
				if (aiMastermindExpose_CountSentEncountersForPlayer(pMMPlayer->erPlayer) < pExposeLevel->iNumEncounters)
				{
					S32 iIdx = randomIntRange(pExposeLevel->iAllowedTiersMin, pExposeLevel->iAllowedTiersMax);
					const char *pch = eaGet(&pDef->eapcEncounterTiers, iIdx);
					Entity *e = entFromEntityRef(PARTITION_UNINITIALIZED, pMMPlayer->erPlayer);
					aiMastermindExpose_QueueSpawnForPlayer(pDef, pch, e, exposeLvlIdx);
				}
			}
		}
	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_FindAndAddAllPlayers()
{
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity* e;
	while(e = EntityIteratorGetNext(iter))
	{
		aiMastermind_AddPlayer(e);
	}

	EntityIteratorRelease(iter);
}

// -----------------------------------------------------------------------------------------------------------
void aiMastermindExpose_OnMapUnload()
{
	// 
	FOR_EACH_IN_EARRAY(s_exposeManager.eaActiveQueuedSpawns, MMQueuedSpawn, pQueuedSpawn)
	{
		queuedSpawn_ReleaseQueuedSpawn(pQueuedSpawn);
	}
	FOR_EACH_END

	eaClearEx(&s_exposeManager.eaSentEncounters, MMSentEncounter_Release);
	eaClear(&s_exposeManager.eaActiveQueuedSpawns);
}

// -----------------------------------------------------------------------------------------------------------
void aiMastermindExpose_OnMapLoad()
{
	aiMastermindExpose_FindAndAddAllPlayers();
}

void aiMastermindExpose_FirstTickInit(AIMastermindDef *pDef)
{
	
	
}

// -----------------------------------------------------------------------------------------------------------
void aiMastermindExpose_Update(AIMastermindDef *pDef)
{
	AIMastermindExposeDef *pCurrentDef = pDef->pExposeDef;

	if (!s_exposeManager.bInitialized)
		return;
	if (!pCurrentDef)
		return;

	// update the player information	
	aiMastermindExpose_UpdateAllPlayerInfo(pCurrentDef);

	// update queued spawns
	aiMastermindExpose_UpdateQueueSpawns(pCurrentDef);
	
	// update sent encounters
	aiMastermindExpose_UpdateSentEncounters(pCurrentDef);

	// update player expose spawning
	aiMastermindExpose_ProcessExposeForPlayer(pCurrentDef);

}


AUTO_COMMAND;
void aiMMEnable(int enable)
{
	if (!s_exposeManager.bInitialized)
		return;

	enable =!!enable;
	if (s_exposeManager.bEnabled == (U32)enable)
		return; // no change

	printf("AIMastermind Enabled\n");
	
}

AUTO_COMMAND;
void aiMMOverrideExpose(F32 expose)
{
	if (expose < 0)
	{
		printf("AIMastermind Override expose disabled\n");
		s_exposeManager.bOverrideExpose = 0.f;
		s_exposeManager.bOverrideExpose = false;
	}
	else
	{
		// clamp expose to cap

		printf("AIMastermind Override expose enabled: %.2f\n", expose);
		// any negative expose turns off override
		s_exposeManager.fOverrideExpose = expose;
		s_exposeManager.bOverrideExpose = true;
	}
}

// -----------------------------------------------------------------------------------------------------------
static bool aiMastermindExpose_UpdatePlayer(const AIMastermindExposeDef *pDef, MMPlayer *pPlayer)
{
	Entity *e = entFromEntityRef(PARTITION_UNINITIALIZED, pPlayer->erPlayer);

	if (!e)
		return false;

	pPlayer->bIsDead = !aiIsEntAlive(e);

	if (!pPlayer->bIsDead)
	{
		Vec3 vCurPos;
		entGetPos(e, vCurPos);

		pPlayer->fExposeLevel = getExposeForPlayerEnt(pDef, e);

		// check if we've increased in exposure, if so then 
		if (pPlayer->fExposeLevel > pPlayer->fPreviousExposeLevel && 
			(pDef->iHiddenMode == -1 || !character_HasMode(e->pChar, pDef->iHiddenMode)))
		{
			copyVec3(vCurPos, pPlayer->vLastPlayerPos);
			copyVec3(vCurPos, pPlayer->vMenacePos);
		}
		else
		{
 			copyVec3(vCurPos, pPlayer->vLastPlayerPos);
		}

		pPlayer->fPreviousExposeLevel = pPlayer->fExposeLevel;
	}
	else
	{
		pPlayer->fExposeLevel = 0;
		pPlayer->fPreviousExposeLevel = 0;
	}

	return true;
}

// -----------------------------------------------------------------------------------------------------------
static void aiMastermindExpose_UpdateAllPlayerInfo(const AIMastermindExposeDef *pDef)
{
	FOR_EACH_IN_EARRAY(s_exposeManager.eaPlayers, MMPlayer, pMMPlayer)
	{
		if (!aiMastermindExpose_UpdatePlayer(pDef, pMMPlayer))
		{
			eaRemoveFast(&s_exposeManager.eaPlayers, ipMMPlayerIndex);
		}
	}
	FOR_EACH_END
}


// ------------------------------------------------------------------------------------------------------------------
static MMPlayer* aiMastermind_GetPlayerInfo(Entity *e)
{
	EntityRef eref = entGetRef(e);
	FOR_EACH_IN_EARRAY(s_exposeManager.eaPlayers, MMPlayer, pMMPlayer)
		if (pMMPlayer->erPlayer == eref)
			return pMMPlayer;
	FOR_EACH_END

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMastermind_AddPlayer(Entity *e)
{
	if (!aiMastermind_GetPlayerInfo(e))
	{
		MMPlayer *pMMPlayer;
		pMMPlayer = calloc(1, sizeof(MMPlayer));

		// initialize 
		pMMPlayer->erPlayer = entGetRef(e);
		pMMPlayer->timeLastSpawnForPlayer = ABS_TIME;
		entGetPos(e, pMMPlayer->vLastPlayerPos);

		eaPush(&s_exposeManager.eaPlayers, pMMPlayer);
	}

}

// -----------------------------------------------------------------------------------------------------------
void aiMastermind_AIEntCreatedCallback(Entity *e)
{
	if (!s_exposeManager.bEnabled)
		return;

	if (entIsPlayer(e))
	{
		aiMastermind_AddPlayer(e);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiMastermind_AIEntDestroyedCallback(Entity *e)
{
	if (s_exposeManager.bEnabled)
	{
		if (entIsPlayer(e))
		{
			MMPlayer *pPlayerInfo = aiMastermind_GetPlayerInfo(e);
			if (!pPlayerInfo)
			{	// shouldn't be happening
				devassert(pPlayerInfo);
				return;
			}

			eaFindAndRemoveFast(&s_exposeManager.eaPlayers, pPlayerInfo);
			free(pPlayerInfo);
		}
	}

}

AUTO_COMMAND_QUEUED();
void exposeLeaderStoppedPursuing(ACMD_POINTER Entity* e)
{
	if (e)
	{
		AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);
		if (team)
		{
			if(e == aiTeamGetLeader(team))
			{
				MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
				if (pSentEnc)
				{
					pSentEnc->stoppedPursue = true;
					pSentEnc->reachedPursuePoint = false;
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(ai) ACMD_NAME(MastermindPursue);
ExprFuncReturnVal exprFuncMastermindPursue(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDMastermindPursue* mydata = getMyData(context, parse_FSMLDMastermindPursue, (U64)__FUNCTION__);
	AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);

	if (mydata->leaderRef)
	{	// check to see if the leader is still our leader
		Entity *leader = entFromEntityRef(iPartitionIdx, mydata->leaderRef);
		
		if(leader != aiTeamGetLeader(team))
		{
			mydata->dataSet = false;
		}
	}

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;
		EntityRef entRef = aiMovementGetMovementTargetEnt(e, e->aibase);

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			*errString = "Unable to call follow in this section - missing exit handlers";
			return ExprFuncReturnError;
		}

		QueuedCommand_exposeLeaderStoppedPursuing(exitHandlers, e);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDMastermindPursue, localData, (U64)__FUNCTION__);
		
		mydata->addedExitHandlers = true;
	}

	if (!mydata->dataSet)
	{
		Entity *leader = aiTeamGetLeader(team);
		mydata->dataSet = true;

		if (leader)
		{	
			mydata->leaderRef = entGetRef(leader);

			if (!team->pTeamFormation)
			{
				aiFormation_CreateFormationForPatrol(team);
				aiFormation_UpdateFormation(iPartitionIdx, team->pTeamFormation);
			}

			if (leader!=e)
			{	// follow the leader
				aiFormation_DoFormationMovementForMember(iPartitionIdx, team, e);
				return ExprFuncReturnFinished;
			}
		}

	}
	else if (mydata->leaderRef != entGetRef(e))
	{
		aiFormation_DoFormationMovementForMember(iPartitionIdx, team, e);
	}
	else
	{
		MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
		if (pSentEnc)
		{
			Vec3 vPos; 
			entGetPos(e, vPos);

			pSentEnc->reachedPursuePoint = (distance3SquaredXZ(vPos, pSentEnc->vLastSentPos) <= SQR(4.f));
			pSentEnc->stoppedPursue = false;
			
			aiMovementSetTargetPosition(e, e->aibase, pSentEnc->vLastSentPos, NULL, 0);
		}
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(MastermindAtPursuePoint);
int exprFuncMastermindAtPursuePoint(ACMD_EXPR_SELF Entity* e, ExprContext* context)
{
	FSMLDMastermindPursue* mydata = getMyData(context, parse_FSMLDMastermindPursue, 0);
	AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);

	if (team)
	{
		MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
		if (!pSentEnc)
			return false;

		if (mydata->leaderRef == entGetRef(e))
		{
			return pSentEnc->reachedPursuePoint;
		}

		return pSentEnc->stoppedPursue;
		
	}
	return false;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(MastermindShouldEndPursue);
int exprFuncMastermindShouldEndPursue(ACMD_EXPR_SELF Entity* e, ExprContext* context)
{
	AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);
	if (team)
	{
		MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
		if (!pSentEnc)
			return false;

		return pSentEnc->shouldEndPursue;
	}

	return true;
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(MastermindReturnToStartPoint);
void exprFuncMastermindReturnToStartPoint(ACMD_EXPR_SELF Entity* e, ExprContext* context)
{
	AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);
	if (team)
	{
		MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
		Vec3 vSendToPos;
		if (!pSentEnc)
			return;
		
		addVec3(e->aibase->spawnOffset, pSentEnc->vSpawnPosition, vSendToPos);
		aiMovementSetTargetPosition(e, e->aibase, vSendToPos, NULL, AI_MOVEMENT_TARGET_CRITICAL);
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(MastermindIsCloseToStartPoint);
int exprFuncMastermindIsCloseToStartPoint(ACMD_EXPR_SELF Entity* e, ExprContext* context)
{
	// check if we have reached close enough to the designated location
	AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);
	if (team)
	{
		MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
		Vec3 vSendToPos;
		Vec3 vPos; 
		if (!pSentEnc)
			return true;

		entGetPos(e, vPos);
		addVec3(e->aibase->spawnOffset, pSentEnc->vSpawnPosition, vSendToPos);
		if (distance3SquaredXZ(vSendToPos, vPos) <= SQR(4.f))
		{
			return true;
		}
	}
	
	return false;
}

AUTO_COMMAND_QUEUED();
void leftWaitingForDespawn(ACMD_POINTER Entity* e)
{
	if (e)
	{
		AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);
		if (team)
		{
			MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
			if (pSentEnc)
			{
				pSentEnc->waitingForDespawnCount--;
			}
		}
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(MastermindWaitingForDespawn);
ExprFuncReturnVal exprFuncMastermindWaitingForDespawn(ACMD_EXPR_SELF Entity* e, ExprContext* context)
{
	FSMLDMastermindPursue* mydata = getMyData(context, parse_FSMLDMastermindPursue, (U64)__FUNCTION__);
	AITeam *team = aiTeamGetAmbientTeam(e, e->aibase);

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;
		EntityRef entRef = aiMovementGetMovementTargetEnt(e, e->aibase);

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			//*errString = "Unable to call follow in this section - missing exit handlers";
			return ExprFuncReturnError;
		}

		QueuedCommand_leftWaitingForDespawn(exitHandlers, e);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDMastermindPursue, localData, (U64)__FUNCTION__);

		mydata->addedExitHandlers = true;

		if (team)
		{
			MMExposeSentEncounter* pSentEnc = aiMastermindExpose_GetSentEncounterInfoForTeam(team);
			if (pSentEnc)
			{
				pSentEnc->waitingForDespawnCount++;
			}
		}
	}

	return ExprFuncReturnFinished;
}
