/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityClient.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntitySavedData.h"
#include "EntityAttach.h"
#include "gclCivilian.h"
#include "Character.h"
#include "SavedPetCommon.h"
#include "dynDraw.h"
#include "dynFxInterface.h"
#include "dynSkeleton.h"
#include "gclCamera.h"
#include "gclProjectileEntity.h"
#include "entCritter.h"
#include "PowerModes.h"

#include "Character.h"
#include "Character_target.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonEntity.h"
#include "dynAnimInterface.h"
#include "dynFxDamage.h"
#include "dynFxInfo.h"
#include "dynFx.h"
#include "EntityMovementManager.h"
#include "EntityMovementProjectile.h"
#include "character_combat.h"
#include "gclCombatAdvantage.h"
#include "gclCommandParse.h"
#include "gclControlScheme.h"
#include "gclDeadBodies.h"
#include "gclEntity.h"
#include "gclPVP.h"
#include "gclSpectator.h"
#include "gclVisionModeEffects.h"
#include "EntityLib.h"
#include "RegionRules.h"
#include "gclTransformation.h"
#include "wlVolumes.h"
#include "wlCostume.h"
#include "WorldLib.h"
#include "gclDemo.h"

#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "itemArt.h"
#include "Player.h"
#include "contact_common.h"
#include "mission_common.h"
#include "species_common.h"

#include "WorldGrid.h"
#include "AnimList_Common.h"
#include "GameClientLib.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern ParseTable parse_DynNode[];
#define TYPE_parse_DynNode DynNode
extern ParseTable parse_Costume[];
#define TYPE_parse_Costume Costume
extern ParseTable parse_DynDefineParam[];
#define TYPE_parse_DynDefineParam DynDefineParam

extern int character_HasMode(const Character *pchar, PowerMode eMode);

#define FADEINSPEED 0.5

typedef struct DamageFXEntry
{
	dtFx guidFx;
	DynFxDamageRangeInfo* pSrcInfo;
} DamageFXEntry;

typedef struct EntityClientDamageFXData
{
	F32 lastHitPoints, lastMaxHitPoints;
	struct 
	{
		DamageFXEntry* data;
		int count, size;
	} dynActiveFX;
} EntityClientDamageFXData;

static U32 uiFxVolumeType;
U32 playableVolumeType = 0;
static U32 duelEnableVolumeType = 0;
static U32 duelDisableVolumeType = 0;
static U32 ignoreSoundVolumeType = 0;

int entClientMaxCostumeSkeletonCreationsPerFrame = 2;
int entClientCostumeSkeletonCreationsThisFrame = 0;

//Limit the number of entity skeletons that can be created in a single frame. -1 is no limit
AUTO_CMD_INT(entClientMaxCostumeSkeletonCreationsPerFrame, entClientMaxCostumeSkeletonCreationsPerFrame) ACMD_CMDLINE;

AUTO_RUN;
void entClientSetupVolumeTypes( void )
{
    uiFxVolumeType = wlVolumeTypeNameToBitMask("FX");
	playableVolumeType = wlVolumeTypeNameToBitMask("Playable");
	duelEnableVolumeType = wlVolumeTypeNameToBitMask("DuelEnable");
	duelDisableVolumeType = wlVolumeTypeNameToBitMask("DuelDisable");
	ignoreSoundVolumeType = wlVolumeTypeNameToBitMask("IgnoreSound");
}

// Initialize client-specific data
void gclExternInitializeEntity(Entity * ent, bool isReloading)
{
	// why are the entity flags not set by this time?
	if (ent)
	{
		gclVisionModeEffects_InitEntity(ent);

		if (gclCivilian_IsCivilian(ent))
			gclCivilian_Initialize(ent);
	}
}

void gclExternCleanupEntity(Entity * ent, bool isReloading)
{
	if (ent && gclCivilian_IsCivilian(ent))
	{
		gclCivilian_CleanUp(ent);
	}

	if (ent && ent->pPlayer && ent->pPlayer->pInteractInfo)
	{
		eaDestroy(&ent->pPlayer->pInteractInfo->eaInteractableContacts);
	}

	gclPet_DestroyRallyPoints(ent);
}

void entClientEnteredFXVolume(WorldVolume* pVolume, WorldVolumeQueryCache* pCache)
{
	if (wlVolumeIsType(pVolume, uiFxVolumeType))
	{
		WorldVolumeEntry* pEntry = wlVolumeGetVolumeData(pVolume);
		WorldFXVolumeProperties* pFXProps = pEntry->client_volume.fx_volume_properties;
		Entity* pEnt = wlVolumeQueryCacheGetData(pCache);
		if (pFXProps)
		{
			if (pFXProps->fx_filter == WorldFXVolumeFilter_AllEntities || ( pFXProps->fx_filter == WorldFXVolumeFilter_LocalPlayer && entIsLocalPlayer(pEnt) ) )
			{
				if (GET_REF(pFXProps->fx_entrance))
				{
					dtAddFx(pEnt->dyn.guidFxMan, REF_STRING_FROM_HANDLE(pFXProps->fx_entrance), NULL, 0, 0, pFXProps->fx_entrance_hue, 0, NULL, eDynFxSource_Volume, NULL, NULL);
				}
				if (GET_REF(pFXProps->fx_maintained))
				{
					dtFxManAddMaintainedFx(pEnt->dyn.guidFxMan, REF_STRING_FROM_HANDLE(pFXProps->fx_maintained), NULL, pFXProps->fx_maintained_hue, 0, eDynFxSource_Volume);
				}
			}
		}
	}
}

void entClientExitedFXVolume(WorldVolume* pVolume, WorldVolumeQueryCache* pCache)
{
	if (wlVolumeIsType(pVolume, uiFxVolumeType))
	{
		WorldVolumeEntry* pEntry = wlVolumeGetVolumeData(pVolume);
		WorldFXVolumeProperties* pFXProps = pEntry->client_volume.fx_volume_properties;
		Entity* pEnt = wlVolumeQueryCacheGetData(pCache);
		if (pFXProps)
		{
			if (pFXProps->fx_filter == WorldFXVolumeFilter_AllEntities || ( pFXProps->fx_filter == WorldFXVolumeFilter_LocalPlayer && entIsLocalPlayer(pEnt) ) )
			{
				if (GET_REF(pFXProps->fx_exit))
				{
					dtAddFx(pEnt->dyn.guidFxMan, REF_STRING_FROM_HANDLE(pFXProps->fx_exit), NULL, 0, 0, 0.0f, 0, NULL, eDynFxSource_Volume, NULL, NULL);
				}
				if (GET_REF(pFXProps->fx_maintained))
				{
					dtFxManRemoveMaintainedFx(pEnt->dyn.guidFxMan, REF_STRING_FROM_HANDLE(pFXProps->fx_maintained));
				}
			}
		}
	}
}

static S32 gclEntityTickDoFade(const Vec3 vSourcePos, Entity* e, ClientOnlyEntity* coe, F32 elapsed)
{
	const WorldRegionLODSettings* pLODSettings = worldLibGetLODSettings();
	F32 fFadeSpeed = 0.5f; // per second
	const F32 fBaseSendDistance = 300.0f; // we might want to hook this up to some global constant instead of just copy and pasting 300 everywhere
	// rescale the distances based on the new e->fEntitySendDistance
	const F32 fScaleFactor = e->fEntitySendDistance / 300.0f;

	// This is the distance that determines LOD level 4 given a base e->fEntitySendDistance of 300 feet
	const F32 fFadeOutStart = pLODSettings->LodDistance[pLODSettings->uiMaxLODLevel] * fScaleFactor; // Level 1 starts at this distance	

	Vec3 vTargetPos;
	F32 fDistance;
	F32 fAlphaGoal;

	bool bImperceptible;
	bool bInCutscene = false;

	DynFxManager *pManager = dynFxManFromGuid(e->dyn.guidFxMan);

	if (gConf.bNewAnimationSystem &&
		SAFE_MEMBER2(e, pCritter, bSetSpawnAnim) &&
		e->fHideTime > 0.f)
	{
		DynSkeleton *pSkeleton = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if (SAFE_MEMBER(pSkeleton, bStartedKeywordGraph))
			e->fHideTime = 0.f;
	}

	// Determine total FX Entity fade speed override.
	if(pManager) {

		int i;
		float fFadeTimeMultiplier = 1;
		for(i = 0; i < eaSize(&pManager->eaDynFx); i++) {
			if(pManager->eaDynFx[i] && pManager->eaDynFx[i]->fEntityFadeSpeedOverride) {
				fFadeTimeMultiplier *= pManager->eaDynFx[i]->fEntityFadeSpeedOverride;
			}
		}

		fFadeSpeed *= fFadeTimeMultiplier;
	}

	//avoid fading if the entity is waiting for a door transition sequence
	if ( entCheckFlag(e,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS) )
		return 0;

	if (gclAnyCutsceneCameraActive())
	{
		// Ignore character perception in cut-scenes
		bImperceptible = false;

		bInCutscene = true;
	}
	else
	{
		bImperceptible = e->bImperceptible;
	}

	entGetPos(e,vTargetPos);
	fDistance = distance3Squared(vSourcePos, vTargetPos);

	if (fDistance > SQR(e->fEntitySendDistance)) // Is further than the send distance, which probably is because the camera is on the far side of the entity from the player, which the server send distance is based off of
		fAlphaGoal = 0.0f;
	else if (fDistance > SQR(fFadeOutStart)) // Is at the lowest visible LOD level. This is the LOD level at which fading in and out occurs, based on how far along the LOD level the ent is
		fAlphaGoal = calcInterpParam(sqrtf(fDistance), e->fEntitySendDistance, fFadeOutStart);
	else
		fAlphaGoal = 1.0f;

	//fAlphaGoal *= e->fCameraCollisionFade; // don't apply this directly to the alpha, because we don't want to fade out FX due to camera collision.

	if (bImperceptible || e->bFadeOutAndThenRemove || e->bForceFadeOut || e->bInCutscene || e->bDeadBodyFaded)
		fAlphaGoal = 0.0f;

	if (bImperceptible && gConf.bClientImperceptibleFadesImmediately)
		e->fAlpha = 0;

	// Animate fading from fAlphaGoal -> fAlpha
	if (e->fAlpha != fAlphaGoal && !e->bPreserveAlpha)
	{
		if (e->bNoInterpAlpha || (bInCutscene && e->bImperceptible)) // Do not interpolate alpha for imperceptible entities in a cut-scene.
		{
			e->bNoInterpAlpha = false;
			e->fAlpha = fAlphaGoal;
		}
		else
		{		
			F32 fDist = fAlphaGoal - e->fAlpha;
			e->fAlpha += SIGN(fDist) * MIN(fabsf(fDist), fFadeSpeed * elapsed);
			e->fAlpha = CLAMP(e->fAlpha, 0.0f, 1.0f);
		}
	}
	e->bPreserveAlpha = false;

	if (gConf.bNewAnimationSystem &&
		e->fHideTime > 0.f)
	{
		dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, e->fAlpha, 0.f);
		e->fHideTime -= elapsed;
	}
	else
	{
		dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, e->fAlpha, e->fCameraCollisionFade);
	}

	//If alpha is 0, place a do not draw flag on the entity if this isn't space
	if(e->fAlpha <= 0.0f && !entCheckFlag(e,ENTITYFLAG_DONOTDRAW))
		entSetCodeFlagBits(e,ENTITYFLAG_DONOTDRAW);
	else if(e->fAlpha > 0.0f && entCheckFlag(e,ENTITYFLAG_DONOTDRAW))
		entClearCodeFlagBits(e,ENTITYFLAG_DONOTDRAW);
	
	if (e->bFadeOutAndThenRemove && e->fAlpha <= 0.0f)
	{
		gclClientOnlyEntityDestroy(&coe);
		return 1;
	}
	else
	{
		return 0;
	}
}

static S32 noRunAndGun;
AUTO_CMD_INT(noRunAndGun, noRunAndGun);

// The list of entities the player talked to
static EntityRef* s_eaiContactedEntRefs = NULL;
static EntityRef s_iInitialContactEnt = 0;

// Sets the animlist override for a contact
static void gclEntitySetAnimlistOverride(SA_PARAM_NN_VALID Entity* pEntContact, SA_PARAM_NN_VALID ContactDialog *pContactDialog, bool bActiveEntity)
{
	DynSkeleton* pSkeleton;
	AIAnimList *pAnimList;

	if (!pEntContact || !pContactDialog)
	{
		return;
	}

	// We need to set the client side animation bit overrides for the entity whose being talked to
	pSkeleton = dynSkeletonFromGuid(pEntContact->dyn.guidSkeleton);
	if (!pSkeleton)
	{
		return;
	}

	if(bActiveEntity){
		pAnimList = GET_REF(pContactDialog->hAnimListToPlayForActiveEntity);
	}else{
		pAnimList = GET_REF(pContactDialog->hAnimListToPlayForPassiveEntity);
	}

	if(gConf.bNewAnimationSystem){
		if(pAnimList){
			if(!pEntContact->dyn.contactAnimOverrideHandle){
				pEntContact->dyn.contactAnimOverrideHandle = dynSkeletonAnimOverrideCreate(pSkeleton);
			}

			dynSkeletonAnimOverrideStartGraph(	pSkeleton,
												pEntContact->dyn.contactAnimOverrideHandle,
												pAnimList->animKeyword,
												1);
		}else{
			dynSkeletonAnimOverrideDestroy(	pSkeleton,
											&pEntContact->dyn.contactAnimOverrideHandle);
		}
	}else{
		if (pAnimList)
		{
			S32 i;

			// Clear all anim bits
			DynBitField bitField = { 0 };
			memset(&bitField, 0, sizeof(bitField));
				
			for (i = 0; i < eaSize(&pAnimList->bits); i++)
			{
				const char *pchAnimBitName = pAnimList->bits[i];
				if (SAFE_DEREF(pchAnimBitName))
				{
					DynBit dynBit = dynBitFromName(pchAnimBitName);
					dynBitFieldBitSet(&bitField, dynBit);
				}							
			}

			// Copy the bitfield to the entity
			dynBitFieldCopy(&bitField, &pSkeleton->entityBitsOverride);
		}
		else
		{
			// Clear the entityBitsOverride
			dynBitFieldClear(&pSkeleton->entityBitsOverride);
		}
	}
}

static void gclEntityTickUpdateSkeleton(SA_PARAM_NN_VALID Entity* e)
{
	Vec3 pyFace;
	Mat3 mat;
	Vec3 pos;
	Entity *pActivePlayer = entActivePlayerPtr();
	ContactDialog *pContactDialog = SAFE_MEMBER3(e, pPlayer, pInteractInfo, pContactDialog);
	
	entGetFacePY(e, pyFace);
	pyFace[2] = 0.f;
	createMat3YPR(mat, pyFace);
	entGetPos(e, pos);
	scaleAddVec3(mat[2], 1000, pos, pos);

	dtSkeletonSetTarget(e->dyn.guidSkeleton,
						!noRunAndGun,
						pos);
							
	dtSkeletonSetSendDistance(	e->dyn.guidSkeleton,
								e->fEntitySendDistance);
	
	if (pActivePlayer)
	{
		if (pActivePlayer->myRef == e->myRef && pContactDialog) // The current player is in a contact dialog
		{
			S32 i;
			// Add the contact entity to the list if they are not already there
			Entity *pCurrentContact = NULL;
			if (pContactDialog->cameraSourceEnt != 0 &&
				(pCurrentContact = entFromEntityRefAnyPartition(pContactDialog->cameraSourceEnt)) != NULL)
			{
				eaiPushUnique(&s_eaiContactedEntRefs, pContactDialog->cameraSourceEnt);
			}
			else if ((pCurrentContact = entFromEntityRefAnyPartition(pContactDialog->headshotEnt)) != NULL)
			{
				eaiPushUnique(&s_eaiContactedEntRefs, pContactDialog->headshotEnt);
			}
			else if (pCurrentContact == NULL && s_iInitialContactEnt != 0)
			{
				pCurrentContact = entFromEntityRefAnyPartition(s_iInitialContactEnt);
			}

			// Set the initial contact entity
			if (s_iInitialContactEnt == 0 && pCurrentContact)
			{
				s_iInitialContactEnt = entGetRef(pCurrentContact);
			}

			// Iterate through all the contacts spoken so far and set the passive animlist for them
			for (i = 0; i < eaiSize(&s_eaiContactedEntRefs); i++)
			{
				if (pCurrentContact == NULL || s_eaiContactedEntRefs[i] != pCurrentContact->myRef)
				{
					Entity *pEntContact = entFromEntityRefAnyPartition(s_eaiContactedEntRefs[i]);
					if (pEntContact)
						gclEntitySetAnimlistOverride(pEntContact, pContactDialog, false);
				}				
			}

			// Set the animlist for the active entity
			if (pCurrentContact)
			{
				gclEntitySetAnimlistOverride(pCurrentContact, pContactDialog, true);
			}
		}
		else if (pActivePlayer->myRef == e->myRef && pContactDialog == NULL && eaiSize(&s_eaiContactedEntRefs) > 0) // The player is not in a contact dialog
		{
			S32 i;

			// Revert all contacts to their original state
			for (i = 0; i < eaiSize(&s_eaiContactedEntRefs); i++)
			{
				Entity *pEntContact = entFromEntityRefAnyPartition(s_eaiContactedEntRefs[i]);
				DynSkeleton* pSkeleton = pEntContact ? dynSkeletonFromGuid(pEntContact->dyn.guidSkeleton) : NULL;
				if (pSkeleton)
				{
					if(gConf.bNewAnimationSystem){
						dynSkeletonAnimOverrideDestroy(	pSkeleton,
														&pEntContact->dyn.contactAnimOverrideHandle);
					}else{
						// Clear the entityBitsOverride
						dynBitFieldClear(&pSkeleton->entityBitsOverride);
					}
				}
			}

			// Clear the list of contacted entities
			eaiClear(&s_eaiContactedEntRefs);
			s_iInitialContactEnt = 0;
		}
	}
	else
	{
		// Clear the list of contacted entities
		eaiClear(&s_eaiContactedEntRefs);
		s_iInitialContactEnt = 0;
	}
}


static void gclEntityTickUpdateCostume(Entity* pEnt)
{
	//switch the loot ent to the "yours" costume
	Entity *player = entActivePlayerPtr();
	bool bMyDrop;

	if(!pEnt
		|| !pEnt->pCritter
		|| pEnt->pCritter->bDoNotAutoSetLootCostume
		|| !inv_HasLoot(pEnt))
	{
		return;
	}

	PERFINFO_AUTO_START("gclEntityTickUpdateCostume: Loot Costume Fixup", 1);

	bMyDrop = reward_MyDrop(player, pEnt);
	if (bMyDrop && !REF_COMPARE_HANDLES(pEnt->costumeRef.hReferencedCostume, pEnt->pCritter->hYoursCostumeRef))
	{
		COPY_HANDLE(pEnt->costumeRef.hReferencedCostume, pEnt->pCritter->hYoursCostumeRef);
		costumeGenerate_FixEntityCostume(pEnt);
	}
	else if (!bMyDrop && !REF_COMPARE_HANDLES(pEnt->costumeRef.hReferencedCostume, pEnt->pCritter->hNotYoursCostumeRef))
	{
		COPY_HANDLE(pEnt->costumeRef.hReferencedCostume, pEnt->pCritter->hNotYoursCostumeRef);
		costumeGenerate_FixEntityCostume(pEnt);
	}

	PERFINFO_AUTO_STOP();
}

static void gclEntityTickUpdateVolumeStuff(Entity* e)
{
	if (e->volumeCache)
	{
		static Vec3 box_min = {-1.5, 0, -1.5}, box_max = {1.5, 6, 1.5};
		const Capsule *cap;
		Quat rot;
		Mat4 world_mat;

		cap = entGetPrimaryCapsule(e);
		if(cap)
		{
			entGetRot(e, rot);
			quatToMat(rot, world_mat);
			entGetPos(e, world_mat[3]);
			wlVolumeCacheQueryCapsuleByType(e->volumeCache, cap, world_mat, uiFxVolumeType | playableVolumeType | duelEnableVolumeType | duelDisableVolumeType);
		}
		else
		{
			copyMat3(unitmat, world_mat);
			entGetPos(e, world_mat[3]);
			wlVolumeCacheQueryBoxByType(e->volumeCache, world_mat, box_min, box_max, uiFxVolumeType | playableVolumeType | duelEnableVolumeType | duelDisableVolumeType);
		}
	}
}

void gclEntityCritterUpdate(Entity *e)
{
	if (!e->pCritter)
		return;

	switch (e->pCritter->eCritterSubType)
	{
		acase CritterSubType_CIVILIAN_CAR:
			gclCivilian_Tick(e);
	}

	if (!e->pCritter->bSetStance && e->pCritter->ppchStanceWords)
	{
		DynSkeleton *pSkel = dynSkeletonFromGuid(e->dyn.guidSkeleton);

		if (pSkel)
		{
			S32 i;

			e->pCritter->bSetStance = true;

			for (i = eaSize(&e->pCritter->ppchStanceWords) - 1; i >= 0; --i)
			{
				dynSkeletonSetCritterStanceWord(pSkel, e->pCritter->ppchStanceWords[i]);
			}
		}
	}
}

static void gclFxDamageUpdateEntity(Entity* e);
static void gclFxTargetNodesUpdateEntity(Entity* e);

void DEFAULT_LATELINK_gclEntityTick_GameSpecific(Entity *pEntity)
{
	//Does nothing. Overridden per-project.
	return;
}


S32 gclEntityTick(const Vec3 vSourcePos, Entity *e, ClientOnlyEntity* coe, F32 elapsed)
{
	if(gclEntityTickDoFade(vSourcePos, e, coe, elapsed)){
		// Entity was destroyed.
		
		return 1;
	}

	gclEntityTickUpdateSkeleton(e);

	gclEntityTickUpdateVolumeStuff(e);

	gclEntityTickUpdateCostume(e);
	
	gclEntityCritterUpdate(e);

	gclFxDamageUpdateEntity(e);

	gclFxTargetNodesUpdateEntity(e);

	gclVisionModeEffects_UpdateEntity(e);

	gclTransformation_Update(e, elapsed);

	gclDeadBodies_EntityTick(e);

	gclPVP_EntityUpdate(e);

	gclEntityTick_GameSpecific(e);

	gclProjectile_UpdateProjectile(elapsed, e);

	gclCombatAdvantage_EntityTick(elapsed, e);
	return 0;
}


S32 gclExternEntityDoorSequenceTick(Entity *e, F32 fElapsed)
{
	//make this entity invisible
	if ( entCheckFlag(e,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS) )
	{
		if ( entCheckFlag(e,ENTITYFLAG_DONOTDRAW) )
		{
			e->bNoInterpAlpha = true;
			e->fAlpha = 0.0f;
			e->fHideTime = 0.5f;
		}
		else
		{
			if ( e->fHideTime > 0.0f )
			{
				e->fHideTime -= fElapsed;
			}
			else
			{
				const F32 fFadeInTime = 0.5f;
				e->bNoInterpAlpha = false;
				e->fAlpha += (1/fFadeInTime) * fElapsed;
				MIN1F(e->fAlpha, 1.0f);
			}
		}
		dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, e->fAlpha, e->fCameraCollisionFade);
		return 1;
	}
	return 0;
}

void gclExternEntityDetectable(Entity *ePlayer, Entity *eTarget)
{
	{
		int i, j;
		S32 iOwnedPetsSize = ePlayer->pSaved?ea32Size(&ePlayer->pSaved->ppAwayTeamPetID):0;
		Team *pTeam;

		for ( j = 0; j < iOwnedPetsSize; j++ )
		{
			Entity *pPetEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET,ePlayer->pSaved->ppAwayTeamPetID[j]);
			if (pPetEnt)
			{
				if (entGetType(pPetEnt) == entGetType(eTarget) && entGetContainerID(pPetEnt) == entGetContainerID(eTarget))
				{
					eTarget->bImperceptible = false;
					return;
				}
			}
		}
		
		pTeam = team_GetTeam(ePlayer);
		if (pTeam)
		{
			int iSize = eaSize(&pTeam->eaMembers);

			for (i = 0; i < iSize; i++)
			{
				Entity *pMember = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);

				if (!pMember)
				{
					continue; //non-local
				}

				if ((entGetType(pMember) == entGetType(eTarget) && entGetContainerID(pMember) == entGetContainerID(eTarget)))
				{
					eTarget->bImperceptible = false;
					return;
				}

				if(pMember->pSaved)
				{
					iOwnedPetsSize = ea32Size(&pMember->pSaved->ppAwayTeamPetID);
					for ( j = 0; j < iOwnedPetsSize; j++ )
					{
						Entity *pPetEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET,pMember->pSaved->ppAwayTeamPetID[j]);
						if (pPetEnt)
						{
							if (entGetType(pPetEnt) == entGetType(eTarget) && entGetContainerID(pPetEnt) == entGetContainerID(eTarget))
							{
								eTarget->bImperceptible = false;
								return;
							}
						}
					}
				}
			}
		}
	}

	if(ePlayer && ePlayer->pChar && eTarget->pChar)
	{
		Entity* pSpectatingEnt = gclSpectator_GetSpectatingEntity();
		if (!pSpectatingEnt)
		{
			eTarget->bImperceptible = !character_CanPerceive(PARTITION_CLIENT,ePlayer->pChar,eTarget->pChar);
		}
		else 
		{
			F32 fPerceptionDist = character_GetPerceptionDist(ePlayer->pChar, eTarget->pChar);
			F32 fDist = entGetDistance(pSpectatingEnt, NULL, eTarget, NULL, NULL);
			eTarget->bImperceptible = (fDist > fPerceptionDist);
		}
	}
}

bool gclExternEntityTargetable(Entity *ePlayer, Entity *eTarget)
{
	if(ePlayer && ePlayer->pChar && eTarget->pChar)
	{
		if(eTarget->bImperceptible || ea32Find(&ePlayer->pChar->perUntargetable,eTarget->myRef) > -1)
			return false; //Cannot perceive, no matter the characters perception
		else
			return true;
	}

	return true;
}

void entClientRegionRules(Entity *pEnt)
{
	RegionRules *pRules = getRegionRulesFromEnt(pEnt);

	if(pRules)
	{
		ServerCmd_MovementThrottleSet(pRules->fLoginThrottle);
		g_bCombatPredictionDisabled = !!pRules->bNoPrediction;

		gclCamera_UpdateRegionSettings(pEnt);
	}
}

static S32 entSkeletonGetRagdollState(	DynSkeleton* skeleton,
									  MovementManager* mm)
{
	const MMRSkeletonConstant*		constant;
	const MMRSkeletonConstantNP*	constantNP;
	const MMRSkeletonPartStates*	partStates;
	U32								partCount;

	if(!mm || !mmrSkeletonGetStateFG(mm, &constant, &constantNP, &partStates)){
		return 0;
	}

	partCount = eaSize(&partStates->states);

	if (partCount != skeleton->ragdollState.uiNumParts){
		SAFE_FREE(skeleton->ragdollState.aParts);
		skeleton->ragdollState.uiNumParts = partCount;
		skeleton->ragdollState.aParts = calloc(sizeof(DynRagdollPartState), partCount);
	}

	EARRAY_CONST_FOREACH_BEGIN(partStates->states, i, isize);
	MMRSkeletonPartState*	state = partStates->states[i];
	MMRSkeletonPart*		part = constant->parts[i];
	skeleton->ragdollState.aParts[i].pcBoneName = part->boneName;
	skeleton->ragdollState.aParts[i].pcParentBoneName = part->parentBoneName;
	PYRToQuat(state->pyr, skeleton->ragdollState.aParts[i].qWorldSpace);
	copyVec3(state->pos, skeleton->ragdollState.aParts[i].vWorldSpace);
	if (!part->parentBoneName)
	{
		copyVec3(state->pos, skeleton->ragdollState.vHipsWorldSpace);
	}
	EARRAY_FOREACH_END;

	assert(FINITEVEC3(constantNP->pyrOffsetToAnimRoot));
	assert(FINITEVEC3(constantNP->posOffsetToAnimRoot));

	PYRToQuat(constantNP->pyrOffsetToAnimRoot, skeleton->ragdollState.qRootOffset);
	copyVec3(constantNP->posOffsetToAnimRoot, skeleton->ragdollState.vRootOffset);

	return 1;
}

static S32 entSkeletonGetAudioDebugInfo(DynSkeleton *pSkeleton)
{
	Entity *e = entFromEntityRefAnyPartition(pSkeleton->uiEntRef);
	PlayerCostume *c;
	PCSkeletonDef *s;
	SpeciesDef    *r;

	pSkeleton->debugAudioInfo.pcDebugCostume			= NULL;
	pSkeleton->debugAudioInfo.pcDebugCostumeFilename	= NULL;
	pSkeleton->debugAudioInfo.pcDebugCSkel				= NULL;
	pSkeleton->debugAudioInfo.pcDebugCSkelFilename		= NULL;
	pSkeleton->debugAudioInfo.pcDebugSpecies			= NULL;
	pSkeleton->debugAudioInfo.pcDebugSpeciesFilename	= NULL;
	
	if (e)
	{
		if      (e->costumeRef.pEffectiveCostume)	c = e->costumeRef.pEffectiveCostume;
		else if (e->costumeRef.pSubstituteCostume)	c = e->costumeRef.pSubstituteCostume;
		else if (e->costumeRef.pStoredCostume)		c = e->costumeRef.pStoredCostume;
		else										c = GET_REF(e->costumeRef.hReferencedCostume);

		if (c)
		{
			pSkeleton->debugAudioInfo.pcDebugCostume			= c->pcName;
			pSkeleton->debugAudioInfo.pcDebugCostumeFilename	= c->pcFileName;

			if (s = GET_REF(c->hSkeleton)) {
				pSkeleton->debugAudioInfo.pcDebugCSkel			= s->pcName;
				pSkeleton->debugAudioInfo.pcDebugCSkelFilename	= s->pcFileName;
			}

			if (r = GET_REF(c->hSpecies))
			{
				pSkeleton->debugAudioInfo.pcDebugSpecies		 = r->pcName;
				pSkeleton->debugAudioInfo.pcDebugSpeciesFilename = r->pcFileName;
			}
		}
	}

	return 1;
}

void entClientCreateSkeletonEx(Entity* e, bool bKeepOldSkeleton)
{
	PERFINFO_AUTO_START_FUNC();
	assert((!e->dyn.guidSkeleton || bKeepOldSkeleton) && !e->dyn.guidDrawSkeleton);
	{
		WLCostume* wlc = GET_REF(e->hWLCostume);
		
		if (!wlc)
		{
			mmLog(	e->mm.movement,
					NULL,
					"[dyn] Not creating skeleton: hWLCostume is absent.");

			PERFINFO_AUTO_STOP();
			return;
		}

		mmLog(	e->mm.movement,
				NULL,
				"[dyn] Creating skeleton: hWLCostume is present.");

		PERFINFO_AUTO_START("Create skeleton", 1);
		if (!bKeepOldSkeleton || !e->dyn.guidSkeleton)
		{
			e->dyn.guidSkeleton = dtSkeletonCreate(	wlc,
													e->myRef,
													entGetType(e) == GLOBALTYPE_ENTITYPLAYER,
													e->dyn.guidFxMan,
													e->dyn.guidLocation,
													e->dyn.guidRoot);

			mmAnimViewQueueResetFG(e->mm.movement);

			dtSkeletonSetCallbacks(	e->dyn.guidSkeleton,
									e->mm.movement,
									mmSkeletonPreUpdateCallback,
									entSkeletonGetRagdollState,
									entSkeletonGetAudioDebugInfo	);

			if(e->mm.glr)
			{
				DynSkeleton* s = dynSkeletonFromGuid(e->dyn.guidSkeleton);
				dynSkeletonSetLogReceiver(s, e->mm.glr);
			}
		}
		else
		{
			DynSkeleton* pSkeleton = dynSkeletonFromGuid(e->dyn.guidSkeleton);
			DynFxManager* pFxMan = dynFxManFromGuid(e->dyn.guidFxMan);
			if (pSkeleton)
				dynSkeletonReprocessEx(pSkeleton, wlc,true);
			pSkeleton->bEverUpdated = false;
			if (pFxMan)
				pFxMan->bWaitingForSkelUpdate = true;
		}
		PERFINFO_AUTO_STOP();

		if (e->pAttach)
		{
			EARRAY_INT_CONST_FOREACH_BEGIN(e->pAttach->eaiAttached, i, isize);
			{
				Entity *pAttached = entFromEntityRefAnyPartition(e->pAttach->eaiAttached[i]);
				gclEntUpdateAttach(pAttached);
			}
			EARRAY_FOREACH_END;
		}

		gclEntUpdateAttach(e);

		if(!e->dyn.guidSkeleton)
		{
			Errorf("Failed to create skeleton from costume %s", wlc->pcName);
			PERFINFO_AUTO_STOP();
			return;
		}

		PERFINFO_AUTO_START("Create draw skeleton", 1);
		e->dyn.guidDrawSkeleton = dtDrawSkeletonCreate(	e->dyn.guidSkeleton,
														wlc,
														e->dyn.guidFxMan,
														true,
														e->fEntitySendDistance,
														entIsLocalPlayer(e));

		PERFINFO_AUTO_STOP();

		dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, e->fAlpha, e->fCameraCollisionFade);

		// Don't count interactables in the max skeleton creates per frame.

		if(	!GET_REF(e->hCreatorNode) &&
			!REF_IS_SET_BUT_ABSENT(e->hCreatorNode))
		{
			entClientCostumeSkeletonCreationsThisFrame++;
		}
	}
	PERFINFO_AUTO_STOP();
}

EntityClientDamageFXData* entClientCreateDamageFxData()
{
	return calloc(1, sizeof(EntityClientDamageFXData));
}

void entClientFreeDamageFxData(EntityClientDamageFXData* pData)
{
	if (pData)
	{
		free(pData->dynActiveFX.data);
		free(pData);
	}
}

static __forceinline bool damageRangeIsActive(DynFxDamageRangeInfo* range, F32 hp, F32 maxHP)
{
	F32 relHP = maxHP > 0.f ? hp / maxHP : 0.f;
	return (range->useAbsoluteValues &&  hp >= range->minHitPoints && hp <= range->maxHitPoints) ||
		(!range->useAbsoluteValues && relHP >= range->minHitPoints && relHP <= range->maxHitPoints);
}

static void gclFxDamageApplyToFxMan(dtFxManager guidFxMan, DynFxDamageInfo* dmgInfo, EntityClientDamageFXData* entDmgData, F32 hp, F32 maxHP, bool firstUpdate, const char*** peaAlphaPartBones)
{
	int i;
	for (i = 0 ; i < eaSize(&dmgInfo->eaDamageRanges); i++)
	{
		DynFxDamageRangeInfo* range = dmgInfo->eaDamageRanges[i];
		bool isActive = damageRangeIsActive(range, hp, maxHP);
		bool prevActive = damageRangeIsActive(range, entDmgData->lastHitPoints, entDmgData->lastMaxHitPoints);
		if (isActive && (firstUpdate || !prevActive))
		{
			//is active but was not before
			int j;
			int iNumFxList = eaSize(&range->eaFxList);
			int iNumAlphaFxList = eaSize(&range->eaAlphaFxList);
			for (j = 0; j < iNumFxList + iNumAlphaFxList; j++)
			{
				DynFxDamageRangeInfoFxRef* fxRef = (j<iNumFxList)?range->eaFxList[j]:range->eaAlphaFxList[j-iNumFxList];
				dtFx newGuid;
				DynParamBlock* params = NULL;
				
				if (!GET_REF(fxRef->hFx))
					continue;

				if (eaSize(&fxRef->eaDefineParams) > 0)
				{
					//if we have params make a param block
					params = dynParamBlockCreate();
					
					FOR_EACH_IN_EARRAY(fxRef->eaDefineParams, DynDefineParam, oldParam) {
					
						DynDefineParam *newParam = StructClone(parse_DynDefineParam, oldParam);
						eaPush(&(params->eaDefineParams), newParam);

					} FOR_EACH_END;
				}

				newGuid = dtAddFx(guidFxMan, REF_STRING_FROM_HANDLE(fxRef->hFx), params, 0, 0, fxRef->fHue, 0, NULL, eDynFxSource_Damage, NULL, NULL);
				if (!dynFxInfoSelfTerminates(REF_STRING_FROM_HANDLE(fxRef->hFx)))
				{
					DamageFXEntry* fxEntry = dynArrayAddStruct(entDmgData->dynActiveFX.data, entDmgData->dynActiveFX.count, entDmgData->dynActiveFX.size);
					fxEntry->guidFx = newGuid;
					fxEntry->pSrcInfo = range;
				}
				if (eaSize(peaAlphaPartBones) > 0)
				{
					DynFx* pFx = dynFxFromGuid(newGuid);
					if (pFx)
					{
						eaCopy(&pFx->eaEntCostumeParts, peaAlphaPartBones);
						pFx->bCostumePartsExclusive = !!(j<iNumFxList);
					}
				}
			}
		}
		else if (!isActive && prevActive && !firstUpdate)
		{
			//not active but was active before
			int j;
			for (j = 0; j < entDmgData->dynActiveFX.count; j++)
			{
				if (entDmgData->dynActiveFX.data[j].pSrcInfo == range)
				{
					dtFxKill(entDmgData->dynActiveFX.data[j].guidFx);
					entDmgData->dynActiveFX.count--;
					if (entDmgData->dynActiveFX.count)
						entDmgData->dynActiveFX.data[j] = entDmgData->dynActiveFX.data[entDmgData->dynActiveFX.count];
					j--; //need to hit this one again since its now from the end
				}
			}
		}
	}
}

static void gclFxDamageResetEntity(Entity* e)
{
	int i;
	if (!e->dyn.pDamageFXData)
		return;
	
	for (i = 0; i < e->dyn.pDamageFXData->dynActiveFX.count; i++)
	{
		dtFxKill( e->dyn.pDamageFXData->dynActiveFX.data[i].guidFx);
	}
	entClientFreeDamageFxData(e->dyn.pDamageFXData);
	e->dyn.pDamageFXData = NULL;
}

static void gclFxDamageUpdateEntity(Entity* e)
{
	PERFINFO_AUTO_START_FUNC();

	if (dynFxDamageInfoReloadedThisFrame())
		gclFxDamageResetEntity(e);

	if (e->pChar && e->pChar->pattrBasic && e->pChar->pattrBasic->fHitPointsMax > 0)
	{
		PlayerCostume* pc;
		PCSkeletonDef* skelDef;
		DynFxDamageInfo* dmgInfo = NULL;
		F32 hp, maxHP;
		EntityClientDamageFXData* entDmgData = e->dyn.pDamageFXData;
		int i;
		const char** eaAlphaPartBones = NULL;

		if (!e->dyn.guidFxMan)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		pc = costumeEntity_GetEffectiveCostume(e);
		if (!pc)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

		maxHP = e->pChar->pattrBasic->fHitPointsMax;
		hp = e->pChar->pattrBasic->fHitPoints;

		if (!entDmgData)
		{
			entDmgData = entClientCreateDamageFxData();
		}
		else if (entDmgData->lastHitPoints == hp && entDmgData->lastMaxHitPoints == maxHP)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return; //same as last time so do nothing
		}
		
		demo_RecordEntityDamage(e, hp, maxHP);


		for (i = 0; i < eaSize(&pc->eaParts); i++)
		{
			PCPart* part = pc->eaParts[i];
			PCGeometryDef* geoDef = GET_REF(part->hGeoDef);
			if (!geoDef)
				continue;

			if (geoDef->bHasAlpha)
			{
				PCBoneDef* pBone = GET_REF(geoDef->hBone);
				if (pBone)
				{
					if (pBone->pcClickBoneName)
						eaPush(&eaAlphaPartBones, pBone->pcClickBoneName);
					else if (pBone->pcBoneName)
						eaPush(&eaAlphaPartBones, pBone->pcBoneName);
				}
			}
		}


		//do the skeleton first
		skelDef = GET_REF(pc->hSkeleton);
		dmgInfo = skelDef ? GET_REF(skelDef->hDamageFxInfo) : NULL;
		if (dmgInfo)
			gclFxDamageApplyToFxMan(e->dyn.guidFxMan, dmgInfo, entDmgData, hp, maxHP, !e->dyn.pDamageFXData, &eaAlphaPartBones);

		//then the PCGeometryDefs in the parts
		for (i = 0; i < eaSize(&pc->eaParts); i++)
		{
			PCPart* part = pc->eaParts[i];
			PCGeometryDef* geoDef = GET_REF(part->hGeoDef);
			if (!geoDef)
				continue;

			dmgInfo = geoDef->pOptions ? GET_REF(geoDef->pOptions->hDamageFxInfo) : NULL;
			if (dmgInfo)
				gclFxDamageApplyToFxMan(e->dyn.guidFxMan, dmgInfo, entDmgData, hp, maxHP, !e->dyn.pDamageFXData, &eaAlphaPartBones);
		}

		
		entDmgData->lastHitPoints = hp;
		entDmgData->lastMaxHitPoints = maxHP;
		e->dyn.pDamageFXData = entDmgData;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void gclFxTargetNodesUpdateEntity(Entity* e)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i = eaSize(&e->dyn.eaTargetFXNodes)-1; i >= 0; i--){
		EntityClientTargetFXNode* pTargetNode = e->dyn.eaTargetFXNodes[i];
		DynNode* pDynNode = dynNodeFromGuid(pTargetNode->guidTarget);
		if(!pDynNode || RefSystem_GetReferenceCountForReferent(pDynNode) <= 0){
			if(pDynNode){
				dynNodeFree(pDynNode);
			}
			StructDestroy(parse_EntityClientTargetFXNode, eaRemoveFast(&e->dyn.eaTargetFXNodes, i));
		}
	}
	PERFINFO_AUTO_STOP_FUNC();
}
