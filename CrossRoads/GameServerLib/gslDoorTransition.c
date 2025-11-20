/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiAnimList.h"
#include "AnimList_Common.h"
#include "CommandQueue.h"
#include "cutscene.h"
#include "cutscene_common.h"
#include "DoorTransitionCommon.h"
#include "EntityMovementDoor.h"
#include "EntityMovementManager.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslMapTransfer.h"
#include "logging.h"
#include "MapDescription.h"
#include "Player.h"
#include "PowerActivation.h"
#include "rand.h"
#include "RegionRules.h"
#include "SavedPetCommon.h"
#include "WorldColl.h"
#include "WorldLib.h"
#include "AutoGen/gslDoorTransition_c_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#define ENTITY_MAPEXIT_RAYLENGTH 10000.0f

AUTO_STRUCT;
typedef struct RegionAnimationActionData
{
	int							iPartitionIdx;
	EntityRef					erEntRef;		
	DoorTransitionAnimation*	pAnim;			AST(UNOWNED)
	UserDataCallback			pCallback;		NO_AST
	void*						pCallbackData;	NO_AST
} RegionAnimationActionData;

AUTO_STRUCT;
typedef struct EntityCommandQueueData
{
	EntityRef		erEntRef;
	CommandQueue*	pCmdQueue; NO_AST
} EntityCommandQueueData;

AUTO_STRUCT;
typedef struct EntityAnimDoorData
{
	int							iPartitionIdx;
	EntityCommandQueueData**	eaCmdQueueData;
	AIAnimList*					pAnimList;		AST(UNOWNED)
	UserDataCallback			pCallback;		NO_AST
	void*						pCallbackData;	NO_AST
} EntityAnimDoorData;

AUTO_STRUCT;
typedef struct AnimMapMoveData
{
	int									iPartitionIdx;
	EntityRef							iEntRef;
	MapDescription*						pMapDescription;
	REF_TO(DoorTransitionSequenceDef)	hTransSequence;
	int									iTransSeqIndex;
	U32									eFlags;
} AnimMapMoveData;


static void gslEntitySetTransitionSequenceFlagsInternal(int iPartitionIdx, Entity* e, S32* piFlags)
{
	entSetCodeFlagBits(e,(*piFlags));
}

static void gslEntityClearTransitionSequenceFlagsInternal(int iPartitionIdx, Entity* e, S32* piFlags)
{
	entClearCodeFlagBits(e,(*piFlags));
}

void gslEntitySetTransitionSequenceFlags(Entity* e, S32 iEntFlags, bool bIncludePets)
{
	int iPartitionIdx = entGetPartitionIdx(e);
	gslEntitySetTransitionSequenceFlagsInternal(iPartitionIdx, e, &iEntFlags);

	if (bIncludePets)
	{
		Entity_ForEveryPet(iPartitionIdx, e, gslEntitySetTransitionSequenceFlagsInternal, &iEntFlags, true, true);
	}
}

void gslEntityClearTransitionSequenceFlags(Entity* e, S32 iEntFlags, bool bIncludePets)
{
	int iPartitionIdx = entGetPartitionIdx(e);
	gslEntityClearTransitionSequenceFlagsInternal(iPartitionIdx, e, &iEntFlags);

	if (bIncludePets)
	{
		Entity_ForEveryPet(iPartitionIdx, e, gslEntityClearTransitionSequenceFlagsInternal, &iEntFlags, true, true);
	}
}

static void gslEntityPlayAnimationListWithDoor_CB(EntityAnimDoorData* pData)
{
	if (pData)
	{
		S32 i;
		for (i = eaSize(&pData->eaCmdQueueData)-1; i >= 0; i--)
		{
			Entity* e = entFromEntityRef(pData->iPartitionIdx, pData->eaCmdQueueData[i]->erEntRef);
			if (e)
			{
				CommandQueue_ExecuteAllCommands(pData->eaCmdQueueData[i]->pCmdQueue);
			}
			if (pData->eaCmdQueueData[i]->pCmdQueue)
			{
				CommandQueue_Destroy(pData->eaCmdQueueData[i]->pCmdQueue);
			}
		}
		if (pData->pCallback)
		{
			pData->pCallback(pData->pCallbackData);
		}
		StructDestroy(parse_EntityAnimDoorData, pData);
	}
}

static void gslEntityAnimListSet_Internal(Entity* e, EntityAnimDoorData *pAnimData)
{
	 //Create the command queue and set the animation bits for the entity
	EntityCommandQueueData* pData = StructCreate(parse_EntityCommandQueueData);
	pData->pCmdQueue = CommandQueue_Create(8, false);
	pData->erEntRef = entGetRef(e);
	eaPush(&pAnimData->eaCmdQueueData, pData); 
	aiAnimListSet(e,pAnimData->pAnimList,&pData->pCmdQueue); 
}

static void gslEntitySetAnimList_CB(int iPartitionIdx, Entity* e, EntityAnimDoorData* pData)
{
	if (e->aibase)
	{
		gslEntityAnimListSet_Internal(e, pData);
	}
}

static void gslEntityPlayAnimationList_Internal(Entity* e, EntityAnimDoorData *pAnimData, bool bIncludePets)
{
	if (pAnimData)
	{
		gslEntityAnimListSet_Internal(e, pAnimData);

		//Set bits for pets, if requested
		if (bIncludePets)
		{
			Entity_ForEveryPet(entGetPartitionIdx(e), e, gslEntitySetAnimList_CB, pAnimData, true, true);
		}
	}
}

static DoorTransitionSequenceDef* gslTransition_GetBestDef(Entity* pEnt,
														   DoorTransitionSequenceRef** eaTransSequenceDefs,
														   WorldRegionType eSrcRegionType,
														   WorldRegionType eDstRegionType,
														   DoorTransitionType eType)
{
	DoorTransitionSequenceDef* pBestDef = NULL;
	AllegianceDef* pEntAllegianceDef = GET_REF(pEnt->hAllegiance);
	AllegianceDef* pEntSubAllegianceDef = GET_REF(pEnt->hSubAllegiance);
	S32* piMatches = NULL;
	S32 i, j;

	for (i = 0; i < eaSize(&eaTransSequenceDefs); i++)
	{
		DoorTransitionSequenceDef* pDef = GET_REF(eaTransSequenceDefs[i]->hTransSequence);

		if (!pDef)
			continue;

		for (j = 0; j < eaSize(&pDef->eaSequences); j++)
		{
			DoorTransitionSequence* pTransSequence = pDef->eaSequences[j];
			AllegianceDef* pSeqAllegianceDef = GET_REF(pTransSequence->hAllegiance);
			
			if ((pTransSequence->eTransitionType == kDoorTransitionType_Unspecified || pTransSequence->eTransitionType == eType) &&
				(!eaiSize(&pTransSequence->piSrcRegionTypes) || eaiFind(&pTransSequence->piSrcRegionTypes, eSrcRegionType)!=-1) &&
				(!eaiSize(&pTransSequence->piDstRegionTypes) || eaiFind(&pTransSequence->piDstRegionTypes, eDstRegionType)!=-1) &&	
				(!pSeqAllegianceDef || pEntAllegianceDef == pSeqAllegianceDef || pEntSubAllegianceDef == pSeqAllegianceDef))
			{
				ea32Push(&piMatches,i);
				break;
			}
		}
	}

	if (ea32Size(&piMatches))
	{
		// If there is more than one match, randomly select the index to use
		S32 iMatchSize = ea32Size(&piMatches);
		S32 iMatchIndex = iMatchSize > 1 ? randomIntRange(0, iMatchSize-1) : 0;
		S32 iIndex = piMatches[iMatchIndex];

		pBestDef = GET_REF(eaTransSequenceDefs[iIndex]->hTransSequence);
		ea32Destroy(&piMatches);
	}
	return pBestDef;
}

/************************************************************************
// Arrival Transitions
 ************************************************************************/

static void PlaySpawnAnimation_CB(RegionAnimationActionData* pData)
{
	Entity* e = entFromEntityRef(pData->iPartitionIdx, pData->erEntRef);

	if (e)
	{
		gslEntityClearTransitionSequenceFlags(e, ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS, true);
	}

	StructDestroySafe(parse_RegionAnimationActionData, &pData);
}

static void gslTransitionSequenceCleanup(SA_PARAM_NN_VALID Entity* e, const char* pchErrorReason)
{
	if (pchErrorReason && pchErrorReason[0])
	{
		Errorf("Transition Sequence Error - Reason: %s", pchErrorReason);
	}
	gslEntityClearTransitionSequenceFlags(e,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS|ENTITYFLAG_DONOTDRAW,true);
}

static void gslEntityPlayAnimationListWithDoor(SA_PARAM_NN_VALID Entity* e, DoorTransitionAnimation* pAnim, 
											   UserDataCallback pCallback, void* pCallbackData)
{
	if (e->pPlayer && pAnim && pCallback)
	{
		EntityAnimDoorData* pAnimDoorData = StructCreate(parse_EntityAnimDoorData);
		pAnimDoorData->iPartitionIdx = entGetPartitionIdx(e);
		pAnimDoorData->pCallback = NULL;
		pAnimDoorData->pCallbackData = NULL;
		pAnimDoorData->pAnimList = GET_REF(pAnim->hAnimList);
		gslEntityClearTransitionSequenceFlags(e, ENTITYFLAG_DONOTDRAW, true);
		gslEntityPlayAnimationList_Internal(e, pAnimDoorData, true);

		if (!e->pPlayer->pCutscene)
		{
			pAnimDoorData->pCallback = pCallback;
			pAnimDoorData->pCallbackData = pCallbackData;

			if (pAnim->fDuration > 0.0f)
			{
				MovementRequester *mr = NULL;
				mmRequesterCreateBasicByName(e->mm.movement, &mr, "DoorMovement");
				mrDoorStartWithTime(mr, gslEntityPlayAnimationListWithDoor_CB, pAnimDoorData, 1, pAnim->fDuration);
			}
			else
			{
				gslEntityPlayAnimationListWithDoor_CB(pAnimDoorData);
			}
		}
		else
		{
			ActiveCutscene* pActiveCutscene = cutscene_ActiveCutsceneFromPlayer(e);

			if (pAnim->fDuration != 0.0f)
			{
				//If fActivateTime is < 0, unset the anim bits at the end of the transition
				F32 fActivateTime = pAnim->fDuration > 0.0f ? pAnim->fPreDelay + pAnim->fDuration : -1;

				if (fActivateTime < 0)
				{
					pAnimDoorData->pCallback = pCallback;
					pAnimDoorData->pCallbackData = pCallbackData;
				}
				else
				{
					cutscene_CreateAction(e, pActiveCutscene, pCallback, pCallbackData, -1);
				}
				
				cutscene_CreateAction(e, pActiveCutscene,gslEntityPlayAnimationListWithDoor_CB,pAnimDoorData,fActivateTime);
			}
			else
			{
				cutscene_CreateAction(e, pActiveCutscene, pCallback, pCallbackData, -1);
				gslEntityPlayAnimationListWithDoor_CB(pAnimDoorData);
			}
		}
	}
	else
	{
		gslTransitionSequenceCleanup(e, "Failed to get entity, animation, or callback.");
	}
}

static void gslEntityPlayAnimationList_CB(RegionAnimationActionData* pData)
{
	if (pData)
	{
		Entity* e = entFromEntityRef(pData->iPartitionIdx, pData->erEntRef);

		if (e)
		{
			gslEntityPlayAnimationListWithDoor(e,pData->pAnim,pData->pCallback,pData->pCallbackData);
		}

		StructDestroy(parse_RegionAnimationActionData, pData);
	}	
}

void gslEntityPlayAnimationList(Entity* e, 
								DoorTransitionAnimation* pAnim,
								UserDataCallback pCallback, 
								void* pCallbackData)
{
	if (GET_REF(pAnim->hAnimList))
	{
		F32 fPreDelay = pAnim->fPreDelay;

		if (fPreDelay > 0.0f)
		{
			RegionAnimationActionData* pData = StructCreate(parse_RegionAnimationActionData);

			pData->iPartitionIdx = entGetPartitionIdx(e);
			pData->erEntRef = entGetRef(e);
			pData->pAnim = pAnim;
			pData->pCallback = pCallback;
			pData->pCallbackData = pCallbackData;

			if (!e->pPlayer->pCutscene)
			{
				MovementRequester *mr = NULL;
				mmRequesterCreateBasicByName(e->mm.movement, &mr, "DoorMovement");

				mrDoorStartWithTime(mr, gslEntityPlayAnimationList_CB, pData, 1, fPreDelay);
			}
			else
			{
				ActiveCutscene* pActiveCutscene = cutscene_ActiveCutsceneFromPlayer(e);

				cutscene_CreateAction(e, pActiveCutscene, gslEntityPlayAnimationList_CB, pData, fPreDelay);
			}
		}
		else
		{
			gslEntityPlayAnimationListWithDoor(e, pAnim, pCallback, pCallbackData);
		}
	}
	else // This should never happen
	{
		char pchBuffer[MAX_PATH];
		sprintf(pchBuffer, "Couldn't find AnimList (%s)", REF_STRING_FROM_HANDLE(pAnim->hAnimList));
		gslTransitionSequenceCleanup(e, pchBuffer);
	}
}

static int gslTransition_GetSequenceToPlay(Entity* pEnt, 
										   DoorTransitionSequenceDef* pDef, 
										   WorldRegionType eSrcRegionType,
										   WorldRegionType eDstRegionType,
										   DoorTransitionType eType,
										   bool bDebug)
{
	if (pEnt && pDef)
	{
		int i;
		for (i = 0; i < eaSize(&pDef->eaSequences); i++)
		{
			DoorTransitionSequence* pSeq = pDef->eaSequences[i];
			AllegianceDef* pAllegianceDef = GET_REF(pSeq->hAllegiance);

			if ((pSeq->eTransitionType == kDoorTransitionType_Unspecified || pSeq->eTransitionType == eType) &&
				(bDebug || !eaiSize(&pSeq->piSrcRegionTypes) || eaiFind(&pSeq->piSrcRegionTypes, eSrcRegionType)!=-1) &&
				(bDebug || !eaiSize(&pSeq->piDstRegionTypes) || eaiFind(&pSeq->piDstRegionTypes, eDstRegionType)!=-1) &&	
				(!pAllegianceDef || pAllegianceDef == GET_REF(pEnt->hAllegiance) || pAllegianceDef == GET_REF(pEnt->hSubAllegiance)))
			{
				return i;
			}
		}
	}
	if (bDebug)
	{
		return 0;
	}
	return -1;
}

// Attempts to play a transition sequence for the spawning player
void gslEntityPlaySpawnTransitionSequence(Entity* e, bool bDebug)
{
	DoorTransitionSequence* pSequence = NULL;

	if (!e->pPlayer)
		return;

	if (!e->pPlayer->pCutscene && !e->pPlayer->pMovieMapTransfer)
	{
		DoorTransitionSequenceDef* pSequenceDef = DoorTransitionSequence_DefFromName(e->pPlayer->pchTransitionSequence);
		int iIndex = gslTransition_GetSequenceToPlay(e, 
													 pSequenceDef, 
													 e->pPlayer->iPrevDoorRegion, 
													 entGetWorldRegionTypeOfEnt(e), 
													 kDoorTransitionType_Arrival,
													 bDebug);
		if (iIndex >= 0)
		{
			pSequence = pSequenceDef->eaSequences[iIndex];
		}
	}
	// Special case for playing a movie
	if (pSequence && pSequence->pchMovie)
	{
		gslEntity_LockMovement(e, true);
		eventsend_RecordVideoStarted(e, pSequence->pchMovie);
		ClientCmd_PlayVideo_Start(e, pSequence->pchMovie, kDoorTransitionType_Arrival, true);
		StructCopyString(&e->pPlayer->pchActiveMovieName, pSequence->pchMovie);
		gslTransitionSequenceCleanup(e, NULL);
	}
	else if (pSequence)
	{
		CutsceneDef* pCutsceneDef = GET_REF(pSequence->hCutscene);
		
		if (pCutsceneDef)
		{
			cutscene_StartOnServer(pCutsceneDef, e, true);
		}

		if (pSequence->pAnimation)
		{
			RegionAnimationActionData* pData = StructCreate(parse_RegionAnimationActionData);
			pData->iPartitionIdx = entGetPartitionIdx(e);
			pData->erEntRef = entGetRef(e);
			gslEntityPlayAnimationList(e, pSequence->pAnimation, PlaySpawnAnimation_CB, pData);
		}
		else
		{
			gslTransitionSequenceCleanup(e, NULL);
		}
	}
	else
	{
		gslTransitionSequenceCleanup(e, NULL);
	}

	// Reset the last region and the player's arrival sequence
	e->pPlayer->iPrevDoorRegion = -1;
	e->pPlayer->pchTransitionSequence = NULL;
}

// Sets up whatever is needed before the sequence begins
void gslHandleDoorTransitionSequenceSetup(Entity* e)
{
	if (!e->pPlayer)
		return;

	PERFINFO_AUTO_START_FUNC();

	// If there is a previous door region and no transition sequence override, 
	// then determine which default transition to play
	if (!e->pPlayer->pchTransitionSequence && e->pPlayer->iPrevDoorRegion >= 0)
	{
		RegionRules* pCurrRules = getRegionRulesFromEnt(e);
		RegionRules* pPrevRules = e->pPlayer ? getRegionRulesFromRegionType(e->pPlayer->iPrevDoorRegion) : NULL;

		if (pCurrRules && pPrevRules)
		{
			DoorTransitionSequenceDef* pDef;
			pDef = gslTransition_GetBestDef(e,
											pCurrRules->eaArriveSequences, 
											pPrevRules->eRegionType,
											pCurrRules->eRegionType,
											kDoorTransitionType_Arrival);

			if (pDef)
			{
				e->pPlayer->pchTransitionSequence = pDef->pchName;
			}
		}
	}

	if (e->pPlayer->pchTransitionSequence)
	{
		gslEntitySetTransitionSequenceFlags(e,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS|ENTITYFLAG_DONOTDRAW,true);
	}
	else if (entCheckFlag(e, ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS))
	{
		gslEntityClearTransitionSequenceFlags(e,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS|ENTITYFLAG_DONOTDRAW,true);
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(PlayTransitionSequenceByName) ACMD_ACCESSLEVEL(9);
void gslEntityPlayTransitionSequenceByName(Entity* e, ACMD_NAMELIST("DoorTransitionSequenceDef", REFDICTIONARY) const char* pchName)
{
	DoorTransitionSequenceDef* pSequenceDef = RefSystem_ReferentFromString("DoorTransitionSequenceDef", pchName);

	if (e && e->pPlayer && pSequenceDef)
	{
		e->pPlayer->pchTransitionSequence = pSequenceDef->pchName;

		gslHandleDoorTransitionSequenceSetup(e);

		gslEntityPlaySpawnTransitionSequence(e, true);
	}
}

/************************************************************************
// Departure Transitions
 ************************************************************************/

static void PlayAnimationThenMapMove_CB(AnimMapMoveData* pData)
{
	Entity* pEnt = entFromEntityRef(pData->iPartitionIdx, pData->iEntRef);

	if (pEnt && pData->pMapDescription)
	{
		gslEntitySetTransitionSequenceFlags(pEnt, ENTITYFLAG_DONOTDRAW|ENTITYFLAG_DONOTFADE, true);
		MapMoveOrSpawnWithDescription(pEnt, pData->pMapDescription, GetVerboseMapMoveComment(pEnt, "PlayAnimationThenMapMove_CB, Anim %s", GET_REF(pData->hTransSequence) ? GET_REF(pData->hTransSequence)->pchName : "unknown"), pData->eFlags);
	}

	StructDestroySafe(parse_AnimMapMoveData, &pData);
}

static void PlayDespawnAnimation_CB(void* pData)
{
	// Do nothing
}

static void CreateAnimationMapMoveDoor(Entity *pEnt, DoorTransitionAnimation* pAnim, MapDescription* pMapDesc, U32 eFlags)
{
	AnimMapMoveData* pData = StructCreate(parse_AnimMapMoveData);

	pData->pMapDescription = StructClone(parse_MapDescription, pMapDesc); 
	pData->iPartitionIdx = entGetPartitionIdx(pEnt);
	pData->iEntRef = entGetRef(pEnt);
	pData->eFlags = eFlags;

	gslEntityPlayAnimationList(pEnt, pAnim, PlayAnimationThenMapMove_CB, pData);
}

static bool PlayTransitionThenMapMove_Internal(Entity *pEnt, 
											   MapDescription* pMapDesc, 
											   RegionRules* pCurrRules, 
											   DoorTransitionSequenceDef* pSequenceDef,
											   int iTransSeqIndex,
											   U32 eFlags)
{
	DoorTransitionSequence* pSequence = eaGet(&pSequenceDef->eaSequences, iTransSeqIndex);

	if (pSequence)
	{
		CutsceneDef* pCutDef = GET_REF(pSequence->hCutscene);

		if (pCutDef)
		{
			if (!pCutDef->bSinglePlayer)
			{
				ErrorFilenamef(pCutDef->filename, "Warning: Cutscene '%s' is being used for a map transfer but it is not defined as single player!", pCutDef->name);
			}
			cutscene_StartOnServerEx(pCutDef, pEnt, entGetPartitionIdx(pEnt), pMapDesc, eFlags, false); 
		}

		if (pSequence->pAnimation)
		{
			if (pCutDef)
			{
				gslEntityPlayAnimationList(pEnt, pSequence->pAnimation, PlayDespawnAnimation_CB, NULL);
			}
			else
			{
				CreateAnimationMapMoveDoor(pEnt, pSequence->pAnimation, pMapDesc, eFlags);
			}
			return true;
		}
		else if (pCutDef)
		{
			return true;
		}
	}
	return false;
}

static void PlayTransitionThenMapMove_CB(AnimMapMoveData* pData)
{
	if (pData)
	{
		Entity* pEnt = entFromEntityRef(pData->iPartitionIdx, pData->iEntRef);
		RegionRules* pCurrRules = pEnt ? getRegionRulesFromEnt(pEnt) : NULL;
		MapDescription* pMapDesc = pData->pMapDescription;
		DoorTransitionSequenceDef* pSequenceDef = GET_REF(pData->hTransSequence);

		if (pMapDesc && pCurrRules && pSequenceDef)
		{
			PlayTransitionThenMapMove_Internal(pEnt, pMapDesc, pCurrRules, pSequenceDef, pData->iTransSeqIndex, pData->eFlags);
		}
	}
	StructDestroySafe(parse_AnimMapMoveData, &pData);
}

static bool gslEntity_FindBestMapExitDirection_Internal(int iPartitionIdx, Vec3 vEntPos, Vec3 vEntDir, F32 fEntRadius, 
														Vec3 vDirOut, F32* pfAngle)
{
	WorldCollCollideResults Results;
	S32 i, j, iNumSamples = 10;
	F32 fArc = PI/(F32)iNumSamples;
	F32 fConeAngleInc = RAD(15);
	Vec3 vRight, vTarget;
	Vec3 vUp = {0,1,0};
	Vec3 vForward = {0,0,0};
	
	PERFINFO_AUTO_START_FUNC();
	
	copyVec3XZ(vEntDir, vForward);
	normalVec3(vForward);
	crossVec3(vUp, vForward, vRight);
	for (i = 0; i < iNumSamples; i++)
	{
		for (j = 1; j >= -1; j-=2)
		{
			Vec3 vConeDir = {0, 0, 0};
			S32 iArcMult = j > 0 ? i : i+1;
			F32 fX, fY, fCurAngle = j * iArcMult * fArc;
			sincosf(fCurAngle, &fY, &fX);
			scaleAddVec3(vRight, fY, vConeDir, vConeDir);
			scaleAddVec3(vForward, fX, vConeDir, vConeDir);
			scaleAddVec3(vConeDir, ENTITY_MAPEXIT_RAYLENGTH, vEntPos, vTarget);
			worldCollideCapsuleEx(iPartitionIdx,vEntPos,vTarget,WC_QUERY_BITS_CAMERA_BLOCKING,0,fEntRadius,&Results);
			if (!Results.hitSomething)
			{
				if (pfAngle)
				{
					(*pfAngle) = getAngleBetweenNormalizedVec3(vEntDir, vConeDir);
				}
				copyVec3(vConeDir, vDirOut);
				PERFINFO_AUTO_STOP();
				return true;
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return false;
}

bool gslEntity_FindBestMapExitDirection(int iPartitionIdx, Entity* e, Vec3 vDir, F32* pfAngle, bool* pbMatchesFacing)
{
	WorldCollCollideResults Results;
	Vec3 vEntPos, vEntDir, vTarget;
	F32 fEntRadius;
	bool result;
	
	PERFINFO_AUTO_START_FUNC();
	
	fEntRadius = entGetBoundingSphere(e, vEntPos) * 1.5f;
	entGetCombatPosDir(e, NULL, vEntPos, vEntDir);
	scaleAddVec3(vEntDir, ENTITY_MAPEXIT_RAYLENGTH, vEntPos, vTarget);
	worldCollideCapsuleEx(iPartitionIdx,vEntPos,vTarget,WC_QUERY_BITS_CAMERA_BLOCKING,0,fEntRadius,&Results);
	if (!Results.hitSomething)
	{
		copyVec3(vEntDir, vDir);
		if (pfAngle)
		{
			(*pfAngle) = 0.0f;
		}
		if (pbMatchesFacing)
		{
			(*pbMatchesFacing) = true;
		}
		PERFINFO_AUTO_STOP();
		return true;
	}
	if (pbMatchesFacing)
	{
		(*pbMatchesFacing) = false;
	}
	result = gslEntity_FindBestMapExitDirection_Internal(iPartitionIdx, vEntPos, vEntDir, fEntRadius, vDir, pfAngle);
	PERFINFO_AUTO_STOP();
	return result;
}

static bool PlayTransitionThenMapMove(Entity *pEnt, 
									  MapDescription* pMapDesc, 
									  RegionRules* pCurrRules,
									  RegionRules* pNextRules,
									  DoorTransitionSequenceDef* pSequenceDef,
									  TransferFlags eFlags)
{
	WorldRegionType eCurrRegionType = pCurrRules ? pCurrRules->eRegionType : WRT_None;
	WorldRegionType eNextRegionType = pNextRules ? pNextRules->eRegionType : WRT_None;
	int iSeqIndex = gslTransition_GetSequenceToPlay(pEnt, pSequenceDef, eCurrRegionType, eNextRegionType, kDoorTransitionType_Departure, false);
	DoorTransitionSequence* pSequence = pSequenceDef ? eaGet(&pSequenceDef->eaSequences, iSeqIndex) : NULL;

	if (!pSequence)
		return false;

	// Special case for playing movies
	if (pSequence->pchMovie)
	{
		if (pEnt->pPlayer->pMovieMapTransfer)
		{
			return false;
		}
		pEnt->pPlayer->pMovieMapTransfer = StructClone(parse_MapDescription, pMapDesc);
		gslEntity_LockMovement(pEnt, true);
		ClientCmd_PlayVideo_Start(pEnt, pSequence->pchMovie, kDoorTransitionType_Departure, true);
		StructCopyString(&pEnt->pPlayer->pchActiveMovieName, pSequence->pchMovie);
		return true;
	}

	if (pSequence->bOrientToMapExit)
	{
		Vec3 vDir;
		F32 fAngle;
		bool bMatchesFacing = false;
		if (gslEntity_FindBestMapExitDirection(entGetPartitionIdx(pEnt), pEnt, vDir, &fAngle, &bMatchesFacing))
		{
			if (!bMatchesFacing)
			{
				MovementRequester *mr = NULL;
				if (mmRequesterCreateBasicByName(pEnt->mm.movement, &mr, "DoorMovement"))
				{
					bool bRoll = pCurrRules->bSpaceFlight;
					const F32 fVelocity = RAD(30);
					F32 fTime = fAngle / fVelocity;
					AnimMapMoveData* pData = StructCreate(parse_AnimMapMoveData);
					pData->iPartitionIdx = entGetPartitionIdx(pEnt);
					pData->iEntRef = entGetRef(pEnt);
					pData->pMapDescription = StructClone(parse_MapDescription, pMapDesc);
					pData->eFlags = eFlags;
					SET_HANDLE_FROM_REFERENT("DoorTransitionSequenceDef", pSequenceDef, pData->hTransSequence);
					pData->iTransSeqIndex = iSeqIndex;
					mrDoorStartWithFacingDirection(mr,PlayTransitionThenMapMove_CB,pData,vDir,fTime,bRoll);
					return true;
				}
			}
		}
		else
		{
			entLog(LOG_PLAYER, pEnt, "PlayTransitionThenMapMove", "Couldn't find a valid map exit");
		}
	}
	
	return PlayTransitionThenMapMove_Internal(pEnt, pMapDesc, pCurrRules, pSequenceDef, iSeqIndex, eFlags);
}

static void DoorDepartureSequenceInit(Entity* pEnt)
{
	if (pEnt->pPlayer)
	{
		// Update the last region
		pEnt->pPlayer->iPrevDoorRegion = entGetWorldRegionTypeOfEnt(pEnt);
		// Set the "in progress" flag
		gslEntitySetTransitionSequenceFlags(pEnt, ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS, true);
	}
}

void gslEntityPlayTransitionSequenceThenMapMove(Entity* pEnt, 
												MapDescription* pMapDesc, 
												RegionRules* pCurrRules, 
												RegionRules* pNextRules,
												DoorTransitionSequenceDef* pTransitionOverride,
												TransferFlags eFlags)
{
	if (characterIsTransferring(pEnt) || entCheckFlag(pEnt, ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS))
	{
		return;
	}
	if (pCurrRules)
	{
		if (pTransitionOverride)
		{
			DoorDepartureSequenceInit(pEnt);

			if (PlayTransitionThenMapMove(pEnt,pMapDesc,pCurrRules,pNextRules,pTransitionOverride,eFlags))
				return;
		}
		else if (pNextRules)
		{
			DoorTransitionSequenceDef* pDef;
			
			DoorDepartureSequenceInit(pEnt);
			
			pDef = gslTransition_GetBestDef(pEnt,
											pCurrRules->eaDepartSequences, 
											pCurrRules->eRegionType,
											pNextRules->eRegionType,
											kDoorTransitionType_Departure);
			if (pDef)
			{
				PlayTransitionThenMapMove(pEnt,pMapDesc,pCurrRules,pNextRules,pDef,eFlags);
				return;
			}
		}
	}

	//do the map transfer right away
	MapMoveOrSpawnWithDescription(pEnt, 
								  pMapDesc, 
								  GetVerboseMapMoveComment(pEnt, "gslEntityPlayTransitionSequenceThenMapMove. pCurrRules: %s. pNextRules: %s. pTransitionOverride: %s",
								  pCurrRules ? StaticDefineIntRevLookup(WorldRegionTypeEnum, pCurrRules->eRegionType) : "(none)", 
								  pNextRules ? StaticDefineIntRevLookup(WorldRegionTypeEnum, pNextRules->eRegionType) : "(none)",  
								  pTransitionOverride ? pTransitionOverride->pchName: "(none)"), 
								  eFlags);
}

// If applicable, play outro animation or cutscene, then map move - or just map move.
void gslEntityPlayTransitionSequenceThenMapMoveEx(Entity *pEnt, 
												  const char* pcMap, 
												  ZoneMapType eMapType,
												  const char* pcSpawn,
												  int iMapIndex,
												  ContainerID uMapContainerID,
												  U32 uPartitionID,
												  GlobalType eOwnerType, ContainerID uOwnerID, 
												  const char* pchMapVars, 
												  RegionRules* pCurrRules, RegionRules* pNextRules,
												  DoorTransitionSequenceDef* pTransOverride,
												  TransferFlags eFlags)
{
	MapDescription MapDesc = {0};
	MapMoveFillMapDescriptionEx(&MapDesc, 
								pcMap, 
								eMapType, 
								pcSpawn, 
								iMapIndex, 
								uMapContainerID, 
								uPartitionID,
								eOwnerType, 
								uOwnerID, 
								pchMapVars);
	gslEntityPlayTransitionSequenceThenMapMove(pEnt, &MapDesc, pCurrRules, pNextRules, pTransOverride, eFlags);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(PlayVideo_Finished) ACMD_PRIVATE;
void gslPlayVideo_Finished(Entity* pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		if (pEnt->pPlayer->pMovieMapTransfer)
		{
			MapMoveOrSpawnWithDescription(pEnt,pEnt->pPlayer->pMovieMapTransfer,GetVerboseMapMoveComment(pEnt, "gslPlayVideoFinished"), 0);
			StructDestroySafe(parse_MapDescription, &pEnt->pPlayer->pMovieMapTransfer);
		}
		gslEntity_UnlockMovement(pEnt);
		eventsend_RecordVideoEnded(pEnt, pEnt->pPlayer->pchActiveMovieName);
		StructFreeStringSafe(&pEnt->pPlayer->pchActiveMovieName);
	}
}

#include "AutoGen/gslDoorTransition_c_ast.c"