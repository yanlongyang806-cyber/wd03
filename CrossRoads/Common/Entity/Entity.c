/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "WorldLib.h"
#include "WorldGrid.h"

#include "Entity.h"
#include "ArmamentSwapCommon.h"
#include "EntityAttach.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "EntityInteraction.h"
#include "EntityIterator.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "Character_mods.h"
#include "CombatDebug.h"
#include "CombatReactivePower.h"
#include "EntityBuild.h"
#include "CostumeCommonEntity.h"
#include "EntityExtern.h"
#include "EntityLib.h"
#include "GameStringFormat.h"
#include "logging.h"
#include "MapDescription.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerHelpers.h"
#include "Powers_h_ast.h"
#include "PowerTree.h"
#include "PowersEnums_h_ast.h"
#include "StringCache.h"
#include "WorldColl.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "character_target.h"
#include "cmdparse.h"
#include "dynAnimInterface.h"
#include "dynFxInterface.h"
#include "dynSkeleton.h"
#include "dynFxManager.h"
#include "entCritter.h"
#include "ItemArt.h"
#include "ItemAssignments.h"
#include "itemCommon.h"
#include "tradeCommon.h"
#include "PowerModes.h"
#include "PowerReplace.h"
#include "PowerTreeHelpers.h"
#include "mission_common.h"
#include "progression_common.h"
#include "nemesis_common.h"
#include "oldencounter_common.h"
#include "playerstats_common.h"
#include "wlEncounter.h"
#include "wlVolumes.h"
#include "../wlModelLoad.h"
#include "LineDist.h"
#include "Tray.h"
#include "AutoTransDefs.h"
#include "MapRevealCommon.h"
#include "ControlScheme.h"
#include "PowerVars.h"
#include "RewardCommon.h"
#include "SavedPetCommon.h"
#include "Team.h"
#include "TransformationCommon.h"
#include "Guild.h"
#include "cutscene_common.h"
#include "encounter_common.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "dynNodeInline.h"
#include "Player.h"
#include "GamePermissionsCommon.h"
#include "GameAccountDataCommon.h"
#include "microtransactions_common.h"
#include "contact_common.h"
#include "InteriorCommon.h"
#include "ActivityLogCommon.h"
#include "Leaderboard.h"
#include "CombatConfig.h"
#include "GamePermissionsCommon.h"
#include "UGCProjectUtils.h"
#include "GlobalExpressions.h"
#include "interaction_common.h"
#include "MicroTransactions.h"
#include "NotifyCommon.h"
#include "NotifyCommon_h_ast.h"
#include "NumericConversionCommon.h"
#include "TeamUpCommon.h"
#include "SimpleCpuUsage.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "EntityMovementDefault.h"
	#include "EntityMovementProjectile.h"
	#include "CombatPowerStateSwitching.h"
	#include "PowersMovement.h"
	#include "EntityMovementDead.h"
	#include "EntityGrid.h"
#endif

#if GAMESERVER
	#include "aiLib.h"
	#include "aiPowers.h"
	#include "gslpetcommand.h"
	#include "gslEncounter.h"
	#include "gslEntity.h"
	#include "gslControlScheme.h"
	#include "gslinteractionManager.h"
	#include "RegionRules.h"
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#elif GAMECLIENT
	#include "gclEntity.h"
	#include "gclMapState.h"
	#include "EntityClient.h"
	#include "dynFxManager.h"
	#include "GameClientLib.h"
	#include "RegionRules.h"
	#include "ClientTargeting.h"
	#include "InteractionUI.h"
#endif

#include "Entity_h_ast.h"
#include "EntityInteraction_h_ast.h"
#include "Player_h_ast.h"
#include "PowerTree_h_ast.h"

#include "EntEnums_h_ast.h"
#include "MapDescription_h_ast.h"
#include "AutoGen/GamePermissionsCommon_h_ast.h"
#include "AutoGen/Leaderboard_h_ast.h"
#include "AutoGen/TeamUpCommon_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("EntitySavedData.h", BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("Player.h", BUDGET_GameSystems););

static REF_TO(WorldInteractionNode) s_OldGlowyNodeRef;

extern GlobalExpressions g_GlobalExpressions;
extern bool gbEntSystemInitted;

U32 frameFromMovementSystem;

void entGetPosDir(Entity *e, Vec3 vOutputPos, Vec3 vOutputDir)
{
	if(vOutputPos)
	{
		entGetPos(e,vOutputPos);
	}
	if(vOutputDir)
	{
		Vec2 pyFace;
		entGetFacePY(e,pyFace);
		createMat3_2_YP(vOutputDir, pyFace);

		if (!SAFE_MEMBER2(e, pPlayer, bUseFacingPitch))
		{
			vOutputDir[1] = 0;
		}
		normalVec3(vOutputDir);
	}
}

//gets the region of an entity. 
WorldRegion* entGetWorldRegionOfEnt(Entity* ent){
	int i;
	WorldRegion** regions = zmapInfoGetAllWorldRegions(NULL);
	for (i=0; i<eaSize(&regions); i++){
		if(ent->astrRegion == worldRegionGetRegionName(regions[i])){
			return regions[i];
		}	
	}
	return NULL;
}

//gets the region type of an entity.
WorldRegionType entGetWorldRegionTypeOfEnt(Entity* ent){
	WorldRegion* region = entGetWorldRegionOfEnt(ent);
	if (region){
		return worldRegionGetType(region);
	} 
	// This is part of the hack to tick offline pets.  See gslSavedPet.c and 
	// character_TickOffline().
	if (!stricmp(ent->astrRegion, "FAKE_SPACE_REGION")){
		return WRT_Space;
	} else if (!stricmp(ent->astrRegion, "FAKE_GROUND_REGION")){
		return WRT_Ground;
	} else {
		// This should never happen, because astrRegion is NULL'd every server change and set 
		// as soon as the player arrives on the map.  HOWEVER, it does happen sometimes on the 
		// client during UGC preview (map changes with no server change) if the network command to 
		// change maps arrives before the command to leave the old map (async).
		// In this case, use the type of the default region of whatever map we have now, until the 
		// client updates catch up.
		Errorf("Entity has a cached region name that doesn't match a region on this map.  This is okay if it happens on the client for a few frames after map moving, but bad otherwise.");
		return worldRegionGetType(zmapGetWorldRegionByName(NULL, NULL));
	}
}

// Note that this matrix uses the facing PY for the rotation.
// The pitch is ignored if the player is using a control scheme
// in which the UseFacingPitch flag is on. This flag sets the pitch
// based on the camera and that's definitely not what we want to use
// for the body orientation.
void entGetBodyMat(Entity* e, Mat4 mat)
{
	Vec3 pyr;

	entGetFacePY(e, pyr);
	pyr[2] = 0;

	if (e->pPlayer && e->pPlayer->bUseFacingPitch)
	{
		// Ignore the pitch set by the camera as it does not affect the body facing
		pyr[0] = 0.f;
	}

	createMat3YPR(mat, pyr);
	
	entGetPos(e, mat[3]);
}

// This matrix uses the root of the skeleton, which is constructed from the facing and movement component of the entity rotation
void entGetVisualMat(Entity* e, Mat4 mat)
{
	DynSkeleton* pSkeleton = dynSkeletonFromGuid(e->dyn.guidSkeleton);
	if (pSkeleton)
	{
		dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mat, false);
	}
	else
	{
		entGetBodyMat(e, mat);
	}
}

void entSetPos(Entity* e, const Vec3 vPos, bool bUpdateMM, const char* reason)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	assert(FINITEVEC3(vPos)); // If this ends up being infinite, things will crash later

	copyVec3(vPos, e->pos_use_accessor);

	if (e->dyn.guidLocation && (!e->pAttach || !e->pAttach->erAttachedTo))
		dtNodeSetPos(e->dyn.guidLocation, vPos);

	if (bUpdateMM)
		mmSetPositionFG(e->mm.movement, vPos, reason);

	#if GAMESERVER
		entGridUpdate(e, true);
	#endif
#endif
}

void entSetRot(Entity* e, const Quat rot, bool bUpdateMM, const char* reason)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	assert(FINITEVEC4(rot)); // If this ends up being infinite, things will crash later

	copyQuat(rot, e->rot_use_accessor);

	if (e->dyn.guidLocation && (!e->pAttach || !e->pAttach->erAttachedTo))
	{
		dtNodeSetRot(e->dyn.guidLocation, rot);
	}
	
	if (bUpdateMM)
	{
		mmSetRotationFG(e->mm.movement, rot, reason);

		#ifdef GAMESERVER
		if (e->pPlayer)
		{
			Vec3 pyr;
			quatToPYR(rot, pyr);
			ClientCmd_setGameCamYaw(e, addAngle(pyr[1], PI));
		}
		#endif
	}
#endif
}

void entSetPosRotFace(	Entity* e,
						const Vec3 pos,
						const Quat rot,
						const Vec2 pyFace,
						bool bUpdateMM,
						bool bUpdateClientCam,
						const char* reason)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(pos){
		assert(FINITEVEC3(pos)); // If this ends up being infinite, things will crash later
		copyVec3(pos, e->pos_use_accessor);
	}
	
	if(rot){
		assert(FINITEQUAT(rot)); // If this ends up being infinite, things will crash later
		copyQuat(rot, e->rot_use_accessor);
	}
	
	if(pyFace){
		assert(FINITEVEC2(pyFace)); // If this ends up being infinite, things will crash later
		copyVec2(pyFace, e->pyFace_use_accessor);
	}

	if(	(	pos ||
			rot) &&
		e->dyn.guidLocation &&
		(	!e->pAttach ||
			!e->pAttach->erAttachedTo))
	{
		dtNodeSetPosAndRot(e->dyn.guidLocation, pos, rot);
	}

#if GAMECLIENT
	if (pyFace && eaSize(&e->dyn.eaTargetFXNodes))
	{
		int i;
		Vec3 vPos, vTargetDir, vTargetPos, vResult;
		entGetCombatPosDir(e, NULL, vPos, vTargetDir);
		for(i = eaSize(&e->dyn.eaTargetFXNodes)-1; i >= 0; i--){
			EntityClientTargetFXNode* pTargetNode = e->dyn.eaTargetFXNodes[i];
			scaleAddVec3(vTargetDir, pTargetNode->fRange, vPos, vTargetPos);
			if (!combat_CheckLoS(PARTITION_CLIENT, vPos, vTargetPos, e, NULL, NULL, false, false, vResult)){
				copyVec3(vResult, vTargetPos);
			}
			dtNodeSetPos(pTargetNode->guidTarget, vTargetPos);
		}
	}
#endif

	if (bUpdateMM)
	{
		mmSetPositionFG(e->mm.movement, pos, reason);
		mmSetRotationFG(e->mm.movement, rot, reason);

#ifdef GAMESERVER
		if(bUpdateClientCam && rot && e->pPlayer)
		{
			Vec3 pyr;
			quatToPYR(rot, pyr);
			ClientCmd_setGameCamYaw(e, addAngle(pyr[1], PI));
		}
#endif

	}
#endif
}

void entSetFacePY(Entity* e, const Vec2 pyFace, const char* reason)
{
	assert(FINITEVEC2(pyFace)); // If this ends up being infinite, things will crash later

	copyVec2(pyFace, e->pyFace_use_accessor);
}

void entGetFaceSpaceMat3(SA_PARAM_NN_VALID Entity* e, Mat3 mat)
{
	Vec3 vNorm, vForward;
	{
		Vec3 vPYR;
		Mat3 mTemp;
		entGetFacePY(e, vPYR);
		vPYR[2] = 0.0f;
		createMat3YPR(mTemp, vPYR);
		copyVec3(mTemp[2], vForward);
	}
	quatRotateVec3Inline(e->rot_use_accessor, upvec, vNorm);
	orientMat3ToNormalAndForward(mat, vNorm, vForward);
}

void entGetFaceSpaceQuat(SA_PARAM_NN_VALID Entity* e, Quat qRot)
{
	Mat3 mat;
	entGetFaceSpaceMat3(e, mat);
	mat3ToQuat(mat, qRot);

}

bool entGetBoneMat(SA_PARAM_NN_VALID Entity* e, const char *bone, Mat4 mat)
{
	if(bone && bone[0]) {
		DynSkeleton *skeleton = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if(skeleton) {
			const DynNode *node = dynSkeletonFindNode(skeleton, bone);
			if(node) {
				dynNodeGetWorldSpaceMat(node, mat, false);
				return true;
			}
		}
	}
	return false;	
}

bool entGetBonePos(SA_PARAM_NN_VALID Entity* e, const char *bone, Vec3 pos)
{
	if(bone && bone[0]) {
		DynSkeleton *skeleton = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if(skeleton) {
			const DynNode *node = dynSkeletonFindNode(skeleton, bone);
			if(node) {
				dynNodeGetWorldSpacePos(node, pos);
				return true;
			}
		}
	}
	return false;	
}

void entUpdateView(SA_PARAM_NN_VALID Entity* e)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	mmUpdateCurrentViewFG(e->mm.movement);
#endif
}

void entSetDynOffset(Entity* e, const Vec3 posOffset)
{
	Vec3 pos;
	
	entGetPos(e, pos);

	addVec3(pos,
			posOffset,
			pos);
			
	if (e->dyn.guidLocation && (!e->pAttach || !e->pAttach->erAttachedTo))
		dtNodeSetPos(e->dyn.guidLocation, pos);
}

void entSetLanguage(Entity* e, int langID)
{
	NOCONST(Entity)* pEnt = CONTAINER_NOCONST(Entity, e);
	pEnt->pPlayer->langID = langID;
	entity_SetDirtyBit(e, parse_Player, pEnt->pPlayer, false);
}

static void entGenericLogReceiverMsgHandler(const GenericLogReceiverMsg* msg){
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	Entity* e = msg->userPointer;

	switch(msg->msgType){
		xcase GLR_MSG_LOG_TEXT:{
			wrapped_mmLogv(	e->mm.movement,
							NULL,
							NULL,
							msg->logText.format,
							msg->logText.va);
		}

		xcase GLR_MSG_LOG_SEGMENTS:{
			mmLogSegmentList(	e->mm.movement,
								NULL,
								msg->logSegments.tags,
								msg->logSegments.argb,
								msg->logSegments.segments,
								msg->logSegments.count);
		}
	}
#endif
}

void entDestroyLogReceiver(Entity* e){
#if GAMESERVER || GAMECLIENT
	if(e->mm.glr){
		DynSkeleton* s = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		dynSkeletonSetLogReceiver(s, NULL);
		glrDestroy(&e->mm.glr);
	}
#endif
}

static void entMovementGatherRequesters(Entity* e){
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	#define GET(mr, name) if(!mr){mmRequesterGetByNameFG(e->mm.movement, name, &mr);}
	GET(e->mm.mrFlight, "FlightMovement");
	GET(e->mm.mrSurface, "SurfaceMovement");
	GET(e->mm.mrTactical, "TacticalMovement");
	GET(e->mm.mrInteraction, "InteractionMovement");
	GET(e->mm.mrEmote, "EmoteMovement");
	if(e->pChar){
		GET(e->pChar->pPowersMovement, "PowersMovement");
	}
	#undef GET
#endif
}

#if GAMESERVER || GAMECLIENT
static void entMovementGetGeometryDataCB(	const MovementGlobalMsg* msg,
											const GeoMeshTempData* meshTempData)
{
	msg->getGeometryData.cb.cb(	msg->getGeometryData.cb.userPointer,
								meshTempData->tris,
								meshTempData->tri_count,
								(const F32*)meshTempData->verts,
								meshTempData->vert_count);
}

void entCommonMovementGlobalMsgHandler(const MovementGlobalMsg* msg){
	switch(msg->msgType){
		xcase MG_MSG_FRAME_UPDATED:{
			frameFromMovementSystem = msg->frameUpdated.frameCount;
		}

		xcase MG_MSG_GET_GEOMETRY_DATA:{
			Model* model = NULL;

			switch(msg->getGeometryData.geoType){
				xcase MM_GEO_GROUP_MODEL:{
					model = groupModelFind(msg->getGeometryData.modelName, 0);	
				}

				xcase MM_GEO_WL_MODEL:{
					model = modelFindEx(msg->getGeometryData.fileName,
										msg->getGeometryData.modelName, 
										true,
										WL_FOR_ENTITY);
				}
			}

			if(!model){
				Errorf(	"Failed to find model for entity geo, type %u, file \"%s\", model \"%s\"",
						msg->getGeometryData.geoType,
						msg->getGeometryData.fileName,
						msg->getGeometryData.modelName);
				break;
			}

			model = modelGetCollModel(model);

			geoProcessTempData(	entMovementGetGeometryDataCB,
								(void*)msg,
								model,
								0,
								sameVec3(msg->getGeometryData.scale, unitvec3) ||
									sameVec3(msg->getGeometryData.scale, zerovec3) ?
										NULL :
										msg->getGeometryData.scale,
								1,
								0,
								0,
								0,
								NULL);
		}
	}
}

void entMovementDefaultMsgHandler(const MovementManagerMsg* msg){
	Entity* e = msg->userPointer;
	
	if(	!e &&
		MM_MSG_IS_FG(msg->msgType))
	{
		return;
	}

	switch(msg->msgType){
		xcase MM_MSG_FG_VIEW_STATUS_CHANGED:{
			e->frameWhenViewChanged = frameFromMovementSystem;
			e->posViewIsAtRest = msg->fg.viewStatusChanged.flags.posIsAtRest;
			e->rotViewIsAtRest = msg->fg.viewStatusChanged.flags.rotIsAtRest;
			e->pyFaceViewIsAtRest = msg->fg.viewStatusChanged.flags.pyFaceIsAtRest;
		}
		
		xcase MM_MSG_FG_AFTER_SEND_UPDATE_TO_BG:{
			EntityMovementThreadData* td = msg->fg.afterSendUpdateToBG.threadData;
			
			if(!td){
				break;
			}
			
			if(e->egNode){
				entGridCopyGridPos(e->egNode, &td->entGridPosCopy);
			}
			
			if(e->egNodePlayer){
				entGridCopyGridPos(e->egNodePlayer, &td->entGridPosCopyPlayer);
			}
		}
		
		xcase MM_MSG_FG_UPDATE_FROM_BG:{
			entGridUpdate(e, 0);
		}

		xcase MM_MSG_FG_COLL_RADIUS_CHANGED:{
			e->collRadiusCached = msg->fg.collRadiusChanged.radius;
		}

		xcase MM_MSG_FG_QUERY_POS_AND_ROT:{
			msg->out->fg.queryPosAndRot.flags.didSet = 1;
			entGetPos(e, msg->out->fg.queryPosAndRot.pos);
			entGetRot(e, msg->out->fg.queryPosAndRot.rot);
		}
		
		xcase MM_MSG_FG_INPUT_VALUE_CHANGED:{
			if(	INRANGE(msg->fg.inputValueChanged.value.mivi, MIVI_BIT_LOW, MIVI_BIT_HIGH) &&
				msg->fg.inputValueChanged.value.mivi != MIVI_BIT_TURN_LEFT &&
				msg->fg.inputValueChanged.value.mivi != MIVI_BIT_TURN_RIGHT)
			{
				if(	e->pPlayer &&
					msg->fg.inputValueChanged.value.bit)
				{
					e->pPlayer->InteractStatus.bMovedSinceInteractTick = true;

					if(g_CombatConfig.bMovementAttemptInterrupt
						&& e->pChar
						&& (e->pChar->eChargeMode==kChargeMode_Current || e->pChar->eChargeMode==kChargeMode_CurrentMaintain))
					{
						character_ActInterrupt(entGetPartitionIdx(e),e->pChar,kPowerInterruption_Movement);
					}
				}
			}

			if (g_CombatConfig.tactical.bTacticalAimCancelsQueuedPowers &&
					msg->fg.inputValueChanged.value.mivi == MIVI_BIT_AIM)
			{
				e->pPlayer->bTacticalInputSinceLastCharacterTickQueue = true;
			}
			if (g_CombatConfig.tactical.bTacticalSprintCancelsQueuedPowers &&
				(msg->fg.inputValueChanged.value.mivi == MIVI_BIT_TACTICAL || 
					msg->fg.inputValueChanged.value.mivi == MIVI_BIT_RUN))
			{
				e->pPlayer->bTacticalInputSinceLastCharacterTickQueue = true;
			}
			if (g_CombatConfig.tactical.bTacticalRollCancelsQueuedPowers &&
				msg->fg.inputValueChanged.value.mivi == MIVI_BIT_ROLL)
			{
				e->pPlayer->bTacticalInputSinceLastCharacterTickQueue = true;
			}
		}
		
		xcase MM_MSG_FG_GET_DEBUG_STRING_AFTER_SIMULATION_WAKES:{
			snprintf_s(	msg->out->fg.getDebugString.buffer,
						msg->out->fg.getDebugString.bufferLen,
						"Entity flags: %s%s%s"
						,
						e->posViewIsAtRest ? "posViewIsAtRest, " : "",
						e->rotViewIsAtRest ? "rotViewIsAtRest, " : "",
						e->pyFaceViewIsAtRest ? "pyFaceViewIsAtRest, " : "");
		}

		xcase MM_MSG_FG_QUERY_IS_POS_VALID:{
			msg->out->fg.queryIsPosValid.flags.posIsValid = 1;
		}
		
		xcase MM_MSG_FG_REQUESTER_CREATED:{
			entMovementGatherRequesters(e);
		}
		
		xcase MM_MSG_FG_REQUESTER_DESTROYED:{
			const MovementRequester*const mr = msg->fg.requesterDestroyed.mr;

			ANALYSIS_ASSUME(mr);

			if(mr == SAFE_MEMBER(e->pChar, pPowersMovement)){
				e->pChar->pPowersMovement = NULL;
			}
			
			#define CLEAR(x) if(x == mr){x = NULL;}
			CLEAR(e->mm.mrDisabled);
			CLEAR(e->mm.mrDisabledCSR);
			CLEAR(e->mm.mrDoorGeo);
			CLEAR(e->mm.mrFlight);
			CLEAR(e->mm.mrTactical);
			CLEAR(e->mm.mrDead);
			CLEAR(e->mm.mrSurface);
			CLEAR(e->mm.mrInteraction);
			CLEAR(e->mm.mrEmote);
			CLEAR(e->mm.mrGrab);
			#undef CLEAR

			entMovementGatherRequesters(e);
		}

		xcase MM_MSG_BG_POS_CHANGED:{
			EntityMovementThreadData* td = msg->bg.posChanged.threadData;
			
			if(!td){
				break;
			}
			
			if(	td->entGridPosCopy.bitsRadius &&
				entGridIsGridPosDifferent(&td->entGridPosCopy, msg->bg.posChanged.pos)
				||
				td->entGridPosCopyPlayer.bitsRadius &&
				entGridIsGridPosDifferent(&td->entGridPosCopyPlayer, msg->bg.posChanged.pos))
			{
				mmMsgSetUserThreadDataUpdatedBG(msg);
			}
		}

		xcase MM_MSG_FG_LOGGING_ENABLED:{
			if(!e->mm.glr){
				DynSkeleton* s = dynSkeletonFromGuid(e->dyn.guidSkeleton);
				glrCreate(&e->mm.glr, entGenericLogReceiverMsgHandler, e);
				dynSkeletonSetLogReceiver(s, e->mm.glr);
			}
		}

		xcase MM_MSG_FG_LOGGING_DISABLED:{
			entDestroyLogReceiver(e);
		}
	}
}
#endif

void entInitializeCommon(Entity* e, bool bCreateMovement)
{
	devassert(s_EntityVolumeQueryType);

	if(e->pChar)
	{
		e->pChar->pEntParent = e;
	}

	entSetRot(e, unitquat, false, "initialize");

	#if GAMECLIENT
	{
		char cBuffer[256];
		sprintf(cBuffer, "%s-Location", e->debugName);
		e->dyn.guidLocation = dtNodeCreate();
		dtNodeSetTag(e->dyn.guidLocation, allocAddString(cBuffer));

		sprintf(cBuffer, "%s-Root", e->debugName);
		e->dyn.guidRoot = dtNodeCreate();
		dtNodeSetTag(e->dyn.guidRoot, allocAddString(cBuffer));
	}
	#endif

	if (bCreateMovement) {
		#if !(GAMESERVER || GAMECLIENT)
			assert(0);
		#else
			mmCreate(	&e->mm.movement,
						entMovementDefaultMsgHandler,
						e,
						entGetRef(e),
						0,
						NULL,
						worldGetActiveColl(entGetPartitionIdx(e)));
		#endif
	}

	// This should probably go after the mmManagerCreate
	costumeEntity_RegenerateCostume(e);

	#if GAMESERVER || GAMECLIENT
	if(entGetType(e) == GLOBALTYPE_ENTITYPLAYER)
	{
		U32 bits = ~MCG_PLAYER_PET;
		if (gConf.bEnemiesDontCollideWithPlayer)
		{
			bits &= ~MCG_OTHER;
		}
		entSetCodeFlagBits(e, ENTITYFLAG_IS_PLAYER);

		if (bCreateMovement) 
		{
			mmCollisionGroupHandleCreateFG(e->mm.movement, &e->mm.mcgHandle, __FILE__, __LINE__, MCG_PLAYER);
			mmCollisionBitsHandleCreateFG(e->mm.movement, &e->mm.mcbHandle, __FILE__, __LINE__, bits);
		}

		if (e->pChar)
		{
			CharacterClass *pClass = GET_REF(e->pChar->hClass);
			if (pClass)
			{   // if the character has a reactive power, initialize it 
				if (pClass->pchCombatReactivePowerDef)
				{
					CombatReactivePower_InitCharacter(e->pChar, pClass->pchCombatReactivePowerDef);
				}
				if (pClass->pchCombatPowerStateSwitchingDef)
				{
					CombatPowerStateSwitching_InitCharacter(e->pChar, pClass->pchCombatPowerStateSwitchingDef);
				}
			}
		}
	}
	#endif

	e->volumeCache = wlVolumeQueryCacheCreate(entGetPartitionIdx(e), s_EntityVolumeQueryType, e);

	#if GAMECLIENT
		wlVolumeQuerySetCallbacks(e->volumeCache, entClientEnteredFXVolume, entClientExitedFXVolume, NULL);
	#endif

	entExternInitializeCommon(e);
}

void entPreCleanupCommon(Entity* e, bool isReloading)
{
	// Free the volume query caches.  This is part of cleanup, but should happen before the
	// entity starts to get freed (because leaving volumes may trigger callbacks which need
	// to run on a whole entity, not a partially-cleaned-up entity).
	wlVolumeQueryCacheFree(e->volumeCache);
	e->volumeCache = NULL;
	if(e->externalInnate && e->externalInnate->pPowerVolumeCache)
	{
		wlVolumeQueryCacheFree(e->externalInnate->pPowerVolumeCache);
		e->externalInnate->pPowerVolumeCache = NULL;
	}
}

void entCleanupCommon(int iPartitionIdx, Entity* e, bool isReloading, bool bDoCleanup)
{
	PERFINFO_AUTO_START_FUNC();
	entExternCleanupCommon(e);

	entDestroyLogReceiver(e);

	if (e->pChar)
	{
		GameAccountDataExtract *pExtract;
		PERFINFO_AUTO_START("CharacterDestroy",1);
		pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_Cleanup(iPartitionIdx, e->pChar, isReloading, pExtract, bDoCleanup);
		if (!isReloading)
		{		
			StructDestroy(parse_Character,e->pChar);
			CONTAINER_NOCONST(Entity, e)->pChar = NULL;
		}
		PERFINFO_AUTO_STOP();
	}

	Transformation_Destroy(&e->costumeRef.pTransformation);

	PERFINFO_AUTO_START("Dynamics",1);

#if GAMECLIENT
	entClientFreeDamageFxData(e->dyn.pDamageFXData);
	e->dyn.pDamageFXData = NULL;
	eaDestroyStruct(&e->dyn.eaTargetFXNodes, parse_EntityClientTargetFXNode);
#else
	assert(e->dyn.pDamageFXData == NULL); //this struct is only used on the client
#endif

	dtSkeletonDestroy(e->dyn.guidSkeleton);
	e->dyn.guidSkeleton = 0;

	dtDrawSkeletonDestroy(e->dyn.guidDrawSkeleton);
	e->dyn.guidDrawSkeleton = 0;

	dtNodeDestroy(e->dyn.guidLocation);
	e->dyn.guidLocation = 0;

	dtNodeDestroy(e->dyn.guidRoot);
	e->dyn.guidRoot = 0;
	PERFINFO_AUTO_STOP();

	#if GAMESERVER || GAMECLIENT
		PERFINFO_AUTO_START("mm",1);
		if (e->pChar)
		{
			mrDestroy(&e->pChar->pPowersMovement);
		}
		mrDestroy(&e->mm.mrSurface);
		mrDestroy(&e->mm.mrFlight);
		mrDestroy(&e->mm.mrDoorGeo);
		mrDestroy(&e->mm.mrTactical);
		mrDestroy(&e->mm.mrDead);
		mrDestroy(&e->mm.mrInteraction);
		mrDestroy(&e->mm.mrEmote);
		mrDestroy(&e->mm.mrGrab);
		mrDestroy(&e->mm.mrDisabled);	
		mrDestroy(&e->mm.mrDisabledCSR);
		mrDestroy(&e->mm.mrDragon);

		mmDisabledHandleDestroy(&e->mm.mdhIgnored);
		mmDisabledHandleDestroy(&e->mm.mdhDisconnected);
		mmDisabledHandleDestroy(&e->mm.mdhPaused);

		mmNoCollHandleDestroyFG(&e->mm.mnchAttach);
		mmNoCollHandleDestroyFG(&e->mm.mnchCostume);
		mmNoCollHandleDestroyFG(&e->mm.mnchExpression);
		mmNoCollHandleDestroyFG(&e->mm.mnchPowers);
		mmNoCollHandleDestroyFG(&e->mm.mnchVanity);

		mmCollisionSetHandleDestroyFG(&e->mm.mcsHandle);
		mmCollisionSetHandleDestroyFG(&e->mm.mcsHandleDbg);

		mmCollisionGroupHandleDestroyFG(&e->mm.mcgHandle);
		mmCollisionGroupHandleDestroyFG(&e->mm.mcgHandleDbg);

		mmCollisionBitsHandleDestroyFG(&e->mm.mcbHandle);
		mmCollisionBitsHandleDestroyFG(&e->mm.mcbHandleDbg);

		mmDestroy(&e->mm.movement);
	#endif

	// Clear these three in case entity is re-used
	e->posViewIsAtRest = 0;
	e->rotViewIsAtRest= 0;
	e->pyFaceViewIsAtRest = 0;
	e->frameWhenViewChanged = 0;
	e->frameWhenViewSet = 0;

	PERFINFO_AUTO_STOP();

	// Note that costume memory is freed automatically when last reference is removed
	REMOVE_HANDLE(e->hWLCostume);

	#if GAMESERVER || GAMECLIENT
		entGridFree(e);
	#endif
	
	PERFINFO_AUTO_STOP();
}

static void refreshAllEntMovement(void)
{	
#if GAMESERVER || GAMECLIENT
	if (gbEntSystemInitted) 
	{
		EntityIterator * iter = entGetIteratorAllTypesAllPartitions(0,0);
		Entity *e;
		while(e = EntityIteratorGetNext(iter))
		{
			mmSetWorldColl(	e->mm.movement,
							worldGetActiveColl(entGetPartitionIdx(e)));
		}
		EntityIteratorRelease( iter );
	}
#endif
}


#if GAMECLIENT
#include "GraphicsLib.h"
#endif

// If the entity has any splats, invalidate them such that they regenerate before rendering.
void entInvalidateSplats(Entity *e)
{
#if GAMECLIENT
	DynFxManager *pManager;
	if (!e)
		return;

	pManager = dynFxManFromGuid(e->dyn.guidFxMan);
	if (pManager)
	{
		dynFxManInvalidateSplats(pManager);
	}
#endif
}

// Sets the entity's death bits and movement restrictions
void entity_DeathAnimationUpdate(Entity * pEnt, int bDead, U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	MovementRequester*	mr;

	if(bDead)
	{
		if (pEnt->pChar && g_CombatConfig.fOnDeathCapsuleLingerTime > 0.f)
		{
			character_UpdateDeathCapsuleLinger(pEnt);
		}
		else
		{
			pmSetCollisionsDisabled(pEnt);
		}
	}
	else
	{
		if (pEnt->pChar)
		{
			pEnt->pChar->uiDeathCollisionTimer = 0;
		}
		pmSetCollisionsEnabled(pEnt);
	}

	if(	mmRequesterGetByNameFG(pEnt->mm.movement, "RagdollMovement", &mr) &&
		!mrRagdollEnded(mr))
	{
		// A ragdoll already exists, so do stuff to it.

		mrRagdollSetDead(mr,bDead);
		mrRagdollSetVelocity(mr, pEnt, zerovec3, zerovec3, 20, uiTime);
	}
	else if(bDead &&
			pmShouldUseRagdoll(pEnt) &&
			mmRequesterCreateBasicByName(pEnt->mm.movement,&mr,"RagdollMovement") &&
			mrRagdollSetup(mr, pEnt, uiTime))
	{
		// Create a new ragdoll.

		mrRagdollSetDead(mr,true);
		mrRagdollSetVelocity(mr, pEnt, zerovec3, zerovec3, 20, uiTime);
	}else{
		if(bDead && !pEnt->mm.mrDead) {
			mmRequesterCreateBasicByName(pEnt->mm.movement, &pEnt->mm.mrDead, "DeadMovement");	
		}
		mrDeadSetEnabled(pEnt->mm.mrDead,bDead);

		if (bDead) {
			if (mmRequesterGetByNameFG(pEnt->mm.movement, "ProjectileMovement", &mr)){
				mrDeadSetFromKnockback(pEnt->mm.mrDead,1);
			} else {
				mrDeadSetFromKnockback(pEnt->mm.mrDead,0);
			}
		}

		if (gConf.bNewAnimationSystem && pEnt->pChar) {
			CharacterClass *pClass = character_GetClassCurrent(pEnt->pChar);
			if (pClass) {
				if (bDead) {	
					if (SAFE_MEMBER(pEnt->pChar,pNearDeath))  {
						if (eaSize(&pClass->pNearDeathConfig->pchAnimStanceWords))
							character_StanceWordOn(pEnt->pChar,1,0,kPowerAnimFXType_Death,0,pClass->pNearDeathConfig->pchAnimStanceWords,pmTimestamp(0.f));
						mrDeadSetFromNearDeath(pEnt->mm.mrDead, true);
					} else {
						mrDeadSetFromNearDeath(pEnt->mm.mrDead, false);
					}
				}
				else if (mrDeadWasFromNearDeath(pEnt->mm.mrDead)) {
					if (eaSize(&pClass->pNearDeathConfig->pchAnimStanceWords))
						character_StanceWordOff(pEnt->pChar,1,0,kPowerAnimFXType_Death,0,pClass->pNearDeathConfig->pchAnimStanceWords,pmTimestamp(0.f));
				}
			}
		}
	}
#endif
}

// Causes the ent to die if it is not already dead
void entDieEx(Entity *e, F32 timeToLinger, int giveRewards, int giveKillCredit, GameAccountDataExtract *pExtract, const char* file, int line)
{
	Character *pchar;
	Critter *pCritter;

	if(!e)
		return;

	PERFINFO_AUTO_START_FUNC();

	pCritter = e->pCritter;
	if(pCritter)
	{
		CritterDef *pCritterDef = GET_REF(pCritter->critterDef);
		if (timeToLinger >= 0)
		{
			pCritter->timeToLinger = timeToLinger;
			pCritter->StartingTimeToLinger = timeToLinger;
		}
		else if(pCritterDef)
		{
			pCritter->timeToLinger = pCritterDef->lingerDuration;
			pCritter->StartingTimeToLinger = pCritterDef->lingerDuration;
		}
	}

	if(!entIsAlive(e))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

 	entSetCodeFlagBits(e,ENTITYFLAG_DEAD);

	pchar = e->pChar;

	// Character death
	if(pchar)
	{
		character_Die(entGetPartitionIdx(e), pchar, timeToLinger, giveRewards, giveKillCredit, pExtract);
	}
#if GAMESERVER || GAMECLIENT
	else
	{
		entity_DeathAnimationUpdate(e,true,mmGetProcessCountAfterSecondsFG(0));
	}
#endif

#ifdef GAMESERVER 
	if(IS_HANDLE_ACTIVE(e->hCreatorNode))
	{
		im_onDeath(e);
	}

	aiOnDeathCleanup(e);

	if(pchar)
		pchar->erRingoutCredit = 0;
#endif
	PERFINFO_AUTO_STOP();
}

bool entIsDisconnected(Entity *e)
{
	if (!e || (entGetFlagBits(e) & ENTITYFLAG_PLAYER_DISCONNECTED))
		return true;
	return false;
}

// Returns the height of the entity from the ground, up to the maximum distance
F32 entHeight(Entity *e, F32 fMaxDistance)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
	return 0;
#else
	Vec3 vecSrc, vecTarget;
	WorldCollCollideResults wcResults;

	S32 bFlying = (e->pChar && e->pChar->pattrBasic->fFlight > 0);

	if(!bFlying && mrSurfaceGetOnGround(e->mm.mrSurface))
	{
		return 0;
	}

	// Move the source point up a tad in case the ent is actually on the ground plane
	entGetPos(e,vecSrc);
	vecSrc[1] += 0.001f;

	copyVec3(vecSrc,vecTarget);
	vecTarget[1] -= fMaxDistance;

	// Throw a entity collision capsule down
	//if(worldCollideRay(vecSrc, vecTarget, WC_QUERY_BITS_WORLD_ALL, &wcResults))
	if(wcCapsuleCollide(worldGetActiveColl(entGetPartitionIdx(e)),vecSrc,vecTarget,WC_QUERY_BITS_WORLD_ALL | WC_QUERY_BITS_ENTITY_MOVEMENT,&wcResults)) 
	{
		return vecSrc[1] - wcResults.posWorldEnd[1];
	}
	else
	{
		return fMaxDistance;
	}
#endif
}

// Copies the instantaneous foreground velocity of the entity into the vector
void entCopyVelocityFG(Entity *e, Vec3 vecVelocity)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	mmGetVelocityFG(e->mm.movement,vecVelocity);
#endif
}


bool entGetClientSelectedTargetPos( Entity* ePlayer, Vec3 vPos )
{
	if(IS_HANDLE_ACTIVE(ePlayer->pChar->currentTargetHandle))
	{
		WorldInteractionNode *pTarget = GET_REF(ePlayer->pChar->currentTargetHandle);

		if(pTarget)
		{
			wlInteractionNodeGetWorldMid(pTarget,vPos);
			return true;
		}
	}
	else if(ePlayer->pChar->currentTargetRef)
	{
		Entity *eTarget = entFromEntityRef(entGetPartitionIdx(ePlayer), ePlayer->pChar->currentTargetRef);

		if(eTarget && IS_HANDLE_ACTIVE(eTarget->hCreatorNode))
		{
			WorldInteractionNode *pTarget = GET_REF(eTarget->hCreatorNode);

			if(pTarget)
			{
				wlInteractionNodeGetWorldMid(pTarget,vPos);
				return true;
			}
		}
		else if (eTarget)
		{
			entGetPos(eTarget, vPos);
			return true;
		}
	}

	return false;
}

// get the value of an entities UI var
bool entGetUIVar(Entity *e, const char* VarName, MultiVal* pMultiVal)
{
	bool found=false;
	UIVar* pUIVar;


	pUIVar = eaIndexedGetUsingString(&e->UIVars, VarName);

	if ( pUIVar )
	{
		*pMultiVal = pUIVar->Value;
		found = true;
	}

	return found;
}

//bool playerIsIgnoring(SA_PARAM_NN_VALID Player *pPlayer, ContainerID ignore)
//{
//	if (sizeof(ContainerID) == sizeof(U32))
//		return eaiFind(&((U32*)pPlayer->pIgnoreList), (U32)ignore) >= 0;
//	else if (sizeof(ContainerID) == sizeof(U64))
//		return eai64Find(&((U64*)pPlayer->pIgnoreList), (U64)ignore) >= 0;
//	else
//		assert("Unknown ContainerID Size!"==0);
//}

// For now, use the first capsule for the source, and check against all capsules for dest
F32 entGetDistanceInternal(	Entity *eSource, 
							const Vec3 pointSource, 
							const Vec3 offsetSource, 
							Entity *eTarget, 
							const Vec3 pointTarget,
							Vec3 sourceOut,
							Vec3 targetOut, 
							int xzOnly, 
							int useGivenPositions)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	Vec3 posSource, posTarget;
	Quat rotSource, rotTarget;
	const Capsule*const* capsSource = NULL;
	const Capsule*const* capsTarget = NULL;
	F32 fDistance;

	if(eSource && eTarget && eSource == eTarget)
	{
		return 0.f;
	}
	
	if(!eTarget && !pointTarget)
	{
		// no targets at all ...
		return 0.f;
	}

	PERFINFO_AUTO_START_FUNC();

	if(	eSource &&
		mmGetCapsules(eSource->mm.movement, &capsSource))
	{
		if (useGivenPositions && pointSource)
		{
			copyVec3(pointSource, posSource);
		}
		else
		{
			entGetPos(eSource, posSource);
		}

		entGetRot(eSource, rotSource);
	}
	else if(eSource)
	{
		// Probably geometry collision, this needs to get improved
		entGetPos(eSource, posSource);
	}
	else
	{
		copyVec3(pointSource, posSource);
	}

	if(offsetSource)
	{
		addVec3(posSource,offsetSource,posSource);
	}

	if(	eTarget &&
		mmGetCapsules(eTarget->mm.movement, &capsTarget))
	{
		if (useGivenPositions && pointTarget)
		{
			copyVec3(pointTarget, posTarget);
		}
		else
		{
			entGetPos(eTarget, posTarget);
		}

		entGetRot(eTarget, rotTarget);
	}
	else if(eTarget)
	{
		if(IS_HANDLE_ACTIVE(eTarget->hCreatorNode))
		{
			WorldInteractionNode *pTargetNode = GET_REF(eTarget->hCreatorNode);
			character_FindNearestPointForObject(eSource?eSource->pChar:NULL,posSource,pTargetNode,posTarget,true);
		}
		else
		{
			// Probably geometry collision, this needs to get improved
			entGetPos(eTarget, posTarget);
		}
	}
	else if (pointTarget)
	{
		copyVec3(pointTarget, posTarget);
	}
	else
	{
		return 0.0f;
	}

	fDistance = CapsuleGetDistance(capsSource, posSource, rotSource, capsTarget, posTarget, rotTarget, sourceOut, targetOut, xzOnly, 0);

	PERFINFO_AUTO_STOP();

	return fDistance;
#endif
}

F32 entGetDistance(Entity *eSource, const Vec3 pointSource, Entity *eTarget, const Vec3 pointTarget, Vec3 targetOut)
{
	return entGetDistanceInternal(eSource, pointSource, NULL, eTarget, pointTarget, NULL, targetOut, false, false);
}

F32 entGetDistanceSourcePos(Entity *eSource, const Vec3 pointSource, Entity *eTarget, const Vec3 pointTarget, Vec3 sourceOut, Vec3 targetOut)
{
	return entGetDistanceInternal(eSource, pointSource, NULL, eTarget, pointTarget, sourceOut, targetOut, false, false);
}

// Same as entGetDistance, but will use the pointSource and/or pointTarget as the entities position if they are valid
F32 entGetDistanceAtPositions(Entity *eSource, const Vec3 pointSource, Entity *eTarget, const Vec3 pointTarget, Vec3 targetOut)
{
	return entGetDistanceInternal(eSource, pointSource, NULL, eTarget, pointTarget, NULL, targetOut, false, true);
}

// Same as entGetDistance, but with a position offset for the source
F32 entGetDistanceOffset(Entity *eSource, const Vec3 pointSource, const Vec3 offsetSource, Entity *eTarget, const Vec3 pointTarget, Vec3 targetOut)
{
	return entGetDistanceInternal(eSource, pointSource, offsetSource, eTarget, pointTarget, NULL, targetOut, false, false);
}

F32 entGetDistanceXZ(Entity *eSource, const Vec3 pointSource, Entity *eTarget, const Vec3 pointTarget, Vec3 targetOut)
{
	return entGetDistanceInternal(eSource, pointSource, NULL, eTarget, pointTarget, NULL, targetOut, true, false);
}

F32 entLineDistanceEx(const Vec3 pointSource, F32 sourceRadius, const Vec3 pointDir, F32 length, Entity *eTarget, Vec3 targetOut, bool bAdjustForCrouch)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	Capsule sourceCapsule;
	Vec3 posTarget;
	Quat rotTarget;
	Vec3 nullVec = {0};
	//WLCostume *cTarget = NULL;
	F32 minDist = 9e9;

	PERFINFO_AUTO_START_FUNC();

	copyVec3(pointSource, sourceCapsule.vStart);
	copyVec3(pointDir, sourceCapsule.vDir);
	sourceCapsule.fLength = length;
	sourceCapsule.fRadius = sourceRadius;

#if 0
	{
		Vec3 srcEnd;
		scaleAddVec3(sourceCapsule.vDir, sourceCapsule.fLength, sourceCapsule.vStart, srcEnd);
		wlAddClientLine(NULL, sourceCapsule.vStart, srcEnd, 0xFF00FF00);
	}
#endif

	if (eTarget)
	{
		const Capsule*const* capsules;
		Vec3 minTargetOut = {0};
		entGetPos(eTarget, posTarget);
		entGetRot(eTarget, rotTarget);
		if(mmGetCapsules(eTarget->mm.movement, &capsules))
		{
			if (bAdjustForCrouch && SAFE_MEMBER2(eTarget, pChar, bIsCrouching))
			{
				const Capsule *capTarget = capsules[0];
				CapsuleCapsuleCollide(&sourceCapsule, nullVec, unitquat, NULL, capTarget, posTarget, rotTarget, minTargetOut, &minDist, 0);
			}
			else
			{
				Vec3 tempTargetOut;
				F32 tempDist;
				int i;
				for (i = 0; i < eaSize(&capsules); i++)
				{
					const Capsule *capTarget = capsules[i];
#if 0
					{
						Vec3 tgtStart, tgtEnd;
						addVec3(capTarget->vStart, posTarget, tgtStart);
						addVec3(capTarget->vStart, posTarget, tgtEnd);
						scaleAddVec3(capTarget->vDir, capTarget->fLength, tgtEnd, tgtEnd);
						wlAddClientLine(NULL, tgtStart, tgtEnd, 0xFF00FFFF);
					}
#endif

					if (capTarget->iType != 0) continue;
					CapsuleCapsuleCollide(&sourceCapsule, nullVec, unitquat, NULL, capTarget, posTarget, rotTarget, tempTargetOut, &tempDist, 0);

					tempDist = tempDist - capTarget->fRadius;
					if (tempDist < minDist)
					{
						minDist = tempDist;
						copyVec3(tempTargetOut, minTargetOut);
					}
				}
			}
		}
		else
		{
			F32 tempDist, height;
			Vec3 entPos, colVec;
			entGetPos(eTarget, entPos);

			tempDist = PointLineDistSquared(entPos, sourceCapsule.vStart, sourceCapsule.vDir, sourceCapsule.fLength, colVec);
			tempDist = sqrt(tempDist);
			height = entGetHeight(eTarget);
			if(tempDist < sourceCapsule.fRadius + height)
			{
				copyVec3(colVec, minTargetOut);
				tempDist = tempDist-height-sourceCapsule.fRadius;
				minDist = MAX(0, tempDist);
			}
		}
		if (targetOut)
		{
			copyVec3(minTargetOut, targetOut);
		}
	}
	else if (eTarget && IS_HANDLE_ACTIVE(eTarget->hCreatorNode))
	{
		WorldInteractionNode *node = GET_REF(eTarget->hCreatorNode);

		if (node)
		{
			F32 radius = wlInteractionNodeGetRadius(node);
			entGetPos(eTarget, posTarget);
			minDist = sqrt(PointLineDistSquared(posTarget, pointSource, pointDir, length, targetOut));
			minDist -= radius;
			if (minDist < 0.0f)
				minDist = 0.0f;
		}
	}

	PERFINFO_AUTO_STOP();

	return minDist;
#endif
}

EntityRef entity_GetTargetRef(SA_PARAM_OP_VALID Entity *e)
{
	return (e && e->pChar) ? e->pChar->currentTargetRef : 0;
}

SA_RET_OP_VALID Entity* entity_GetTarget(SA_PARAM_OP_VALID Entity *e)
{
	EntityRef erTarget = (e && e->pChar) ? e->pChar->currentTargetRef : 0;
	return erTarget ? entFromEntityRef(entGetPartitionIdx(e), erTarget) : NULL;
}

SA_RET_OP_VALID Entity* entity_GetTargetDual(SA_PARAM_OP_VALID Entity *e)
{
	EntityRef erTarget = (e && e->pChar) ? e->pChar->erTargetDual : 0;
	return erTarget ? entFromEntityRef(entGetPartitionIdx(e), erTarget) : NULL;
}

SA_RET_OP_VALID Entity* entity_GetAssistTarget(SA_PARAM_OP_VALID Entity *e)
{
	EntityRef erTarget = (e && e->pChar) ? e->pChar->erProxAssistTaget : 0;
	return erTarget ? entFromEntityRef(entGetPartitionIdx(e), erTarget) : NULL;
}

SA_RET_OP_VALID Entity* entity_GetFocusTarget(SA_PARAM_OP_VALID Entity *e)
{
	EntityRef erTarget = (e && e->pChar) ? e->pChar->erTargetFocus : 0;
	return erTarget ? entFromEntityRef(entGetPartitionIdx(e), erTarget) : NULL;
}

bool entIsInCombat(const Entity* e)
{
	if(SAFE_MEMBER(e, pChar))
		return character_HasMode(e->pChar, kPowerMode_Combat);

	return false;
}

bool entIsSprinting(const Entity *e)
{
#if !(GAMESERVER || GAMECLIENT)
	return 0;
#else
	return e->mm.isSprinting;
#endif
}

void entGetSprintTimes(	const Entity* e,
						F32* secondsUsedOut,
						F32* secondsTotalOut,
						S32* sprintUsesFuelOut,
						F32* sprintFuelOut)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(!e->mm.isSprinting){
		if(secondsUsedOut){
			*secondsUsedOut = 0.f;
		}
		
		if(secondsTotalOut){
			*secondsTotalOut = 0.f;
		}
	}else{
		F32 secondsUsed = mmGetLocalViewSecondsSinceSPC(e->mm.spcSprintStart);
		
		MINMAX1(secondsUsed, 0.f, e->mm.maxSprintDurationSeconds);
	
		if(secondsUsedOut){
			*secondsUsedOut = secondsUsed;
		}

		if(secondsTotalOut){
			*secondsTotalOut = e->mm.maxSprintDurationSeconds;
		}
	}

	if(sprintUsesFuelOut){
		*sprintUsesFuelOut = e->mm.sprintUsesFuel;
	}
	
	if(	e->mm.sprintUsesFuel &&
		secondsTotalOut)
	{
		*secondsTotalOut = e->mm.maxSprintDurationSeconds;
	}

	if(sprintFuelOut){
		*sprintFuelOut = e->mm.sprintFuel;
	}
#endif
}

void entGetSprintCooldownTimes(	SA_PARAM_NN_VALID const Entity* e,
								F32* secondsUsedOut,
								F32* secondsTotalOut)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	F32 secondsUsed = mmGetLocalViewSecondsSinceSPC(e->mm.spcCooldownStart);
	
	MINMAX1(secondsUsed, 0.f, e->mm.sprintCooldownSeconds);
	
	if(secondsUsedOut){
		*secondsUsedOut = secondsUsed;
	}
	
	if(secondsTotalOut){
		*secondsTotalOut = e->mm.sprintCooldownSeconds;
	}
#endif
}

void entGetAimCooldownTimes(	SA_PARAM_NN_VALID const Entity* e,
	F32* secondsUsedOut,
	F32* secondsTotalOut)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	F32 secondsUsed = mmGetLocalViewSecondsSinceSPC(e->mm.spcAimCooldownStart);

	MINMAX1(secondsUsed, 0.f, e->mm.aimCooldownSeconds);

	if(secondsUsedOut){
		*secondsUsedOut = secondsUsed;
	}

	if(secondsTotalOut){
		*secondsTotalOut = e->mm.aimCooldownSeconds;
	}
#endif
}

void entGetRollCooldownTimes(	SA_PARAM_NN_VALID const Entity* e,
								F32* secondsUsedOut,
								F32* secondsTotalOut)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	F32 secondsUsed = mmGetLocalViewSecondsSinceSPC(e->mm.spcCooldownStart);
	
	MINMAX1(secondsUsed, 0.f, e->mm.rollCooldownSeconds);
	
	if(secondsUsedOut){
		*secondsUsedOut = secondsUsed;
	}
	
	if(secondsTotalOut){
		*secondsTotalOut = e->mm.rollCooldownSeconds;
	}
#endif
}

bool entIsCrouching(Entity *e)
{
	return e->pChar && e->pChar->bIsCrouching;
}

bool entIsAiming(Entity *e)
{
	return e->pChar && e->pChar->bIsAiming;
}

bool entIsRolling(Entity *e)
{
#if !(GAMESERVER || GAMECLIENT)
	return 0;
#else
	return e->mm.isRolling;
#endif
}

bool entIsUsingShooterControls(Entity *e)
{
	return e->pChar && e->pChar->bShooterControls;
}

// Get the height based on the skeleton
F32 entGetHeightBasedOnSkeleton(Entity* e)
{
	F32 height;

	#if GAMECLIENT
		if (gProjectGameClientConfig.bUseFixedOverHead
			&& e
			&& e->pEntUI
			&& e->pEntUI->uiLastTime + 1000 * gProjectGameClientConfig.fHeightResetTime >= gGCLState.totalElapsedTimeMs)
		{
			height = e->pEntUI->vBoxMax[1];
		}
		else
		{
			height = entGetHeight(e);
		}
	#else
		height = entGetHeight(e);
	#endif
		
	return height;
}

F32 entGetHeightEx(Entity* e, bool bAdjustForCrouch)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	const Capsule*const* capsules;
	F32 maxHeight = 0.0f;
	int i;

	if (!mmGetCapsules(SAFE_MEMBER(e, mm.movement), &capsules))
		// TODO: add geometry case
		return 6.0f;

	if (bAdjustForCrouch && SAFE_MEMBER2(e, pChar, bIsCrouching))
	{
		const Capsule *capTarget = capsules[0];
		Vec3 p1, p2;
		copyVec3(capTarget->vStart, p1);
		scaleVec3(capTarget->vDir, capTarget->fLength, p2);
		addVec3(p2, p1, p2);
		maxHeight = (MAX(p1[1], p2[1]) + capTarget->fRadius) * g_CombatConfig.tactical.aim.fCrouchEntityHeightRatio;
	}
	else
	{
		for (i = 0; i < eaSize(&capsules); i++)
		{
			const Capsule *capTarget = capsules[i];		
			Vec3 p1, p2;
			if (capTarget->iType != 0) continue;
			copyVec3(capTarget->vStart, p1);
			scaleVec3(capTarget->vDir, capTarget->fLength, p2);
			addVec3(p2, p1, p2);

			if (p1[1] + capTarget->fRadius > maxHeight)
			{
				maxHeight = p1[1] + capTarget->fRadius;
			}
			if (p2[1] + capTarget->fRadius > maxHeight)
			{
				maxHeight = p2[1] + capTarget->fRadius;
			}

		}
	}
	return maxHeight;
#endif
}

F32 entGetWidth(Entity *e)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	const Capsule*const* capsules;
	F32 maxWidth = 0.0f;
	int i;

	if (!mmGetCapsules(SAFE_MEMBER(e, mm.movement), &capsules))
		// TODO: add geometry case
		return 6.0f;

	for (i = 0; i < eaSize(&capsules); i++)
	{
		const Capsule *capTarget = capsules[i];		
		Vec3 p1, p2;
		if (capTarget->iType != 0) continue;
		copyVec3(capTarget->vStart, p1);
		scaleVec3(capTarget->vDir, capTarget->fLength, p2);
		addVec3(p2, p1, p2);

		if (p1[0] + capTarget->fRadius > maxWidth)
		{
			maxWidth = p1[0] + capTarget->fRadius;
		}
		if (p2[0] + capTarget->fRadius > maxWidth)
		{
			maxWidth = p2[0] + capTarget->fRadius;
		}
		if (p1[2] + capTarget->fRadius > maxWidth)
		{
			maxWidth = p1[2] + capTarget->fRadius;
		}
		if (p2[2] + capTarget->fRadius > maxWidth)
		{
			maxWidth = p2[2] + capTarget->fRadius;
		}
	}
	return maxWidth;
#endif
}

const Capsule* entGetPrimaryCapsule(Entity *e)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	const Capsule*const* capsules;
	
	if(!mmGetCapsules(SAFE_MEMBER(e, mm.movement), &capsules))
	{
		return NULL;
	}

	return capsules[0];
#endif
}

const Capsule*const* entGetCapsules(Entity *e)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	const Capsule*const* capsules = NULL;

	if(!mmGetCapsules(SAFE_MEMBER(e, mm.movement), &capsules))
	{
		return NULL;
	}

	return capsules;
#endif
}


bool entGetPrimaryCapsuleWorldSpaceBounds(Entity *pEnt, Vec3 vCapBoundMinOut, Vec3 vCapBoundMaxOut)
{
	const Capsule *pCap;
	pCap = entGetPrimaryCapsule(pEnt);
	if(pCap)
	{
		Vec3 vEntPos;
		Quat qRot;
		entGetPos(pEnt, vEntPos);
		entGetRot(pEnt, qRot);
		CapsuleGetWorldSpaceBounds(pCap, vEntPos, qRot, vCapBoundMinOut, vCapBoundMaxOut);
		return true;
	}
	return false;
}


F32 entGetPrimaryCapsuleRadius(Entity *e)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	const Capsule*const* capsules;
	
	if(!mmGetCapsules(e->mm.movement, &capsules)){
		if(e->collRadiusCached > 0.f){
			return e->collRadiusCached;
		}else{
			return 1.5f;
		}
	}

	return capsules[0]->fRadius;
#endif
}

// adjust bounds based on both skeleton pRoot height changes as well as shifting of the
// bounds box. Use the larger of the bounds boxes to prevent it from 'breathing'.

void entAdjustBounds(Entity* e, Vec3 vBoundMin, Vec3 vBoundMax)
{

#if GAMECLIENT

	if(gProjectGameClientConfig.bUseFixedOverHead && e->pEntUI)
	{
		F32 fElaspedTime = (gGCLState.totalElapsedTimeMs - e->pEntUI->uiLastTime) / 1000.0;
		S32 i;
		Vec3 vVel;
		bool bMoving = false;
		F32 fVelocity;

		if(fElaspedTime < 0.01f)
		{
			// no time has passed
			copyVec3(e->pEntUI->vBoxMin, vBoundMin);
			copyVec3(e->pEntUI->vBoxMax, vBoundMax);
			return;		
		}
		else if(fElaspedTime > gProjectGameClientConfig.fHeightResetTime)
		{
			copyVec3(vBoundMin, e->pEntUI->vBoxMin);
			copyVec3(vBoundMax, e->pEntUI->vBoxMax);
			zeroVec3(e->pEntUI->vuiLowerAtTime);
			e->pEntUI->uiLastTime = gGCLState.totalElapsedTimeMs;
		}
		else
		{
			Entity *ePlayer;
			e->pEntUI->uiLastTime = gGCLState.totalElapsedTimeMs;
			ePlayer = entActivePlayerPtr();
			if(ePlayer)
			{
				entCopyVelocityFG(ePlayer, vVel);
				fVelocity = normalVec3(vVel);

				if(fVelocity > 0.01f)
				{
					bMoving = true;
				}
			}

			if(!bMoving)			
			{
				entCopyVelocityFG(e, vVel);
				fVelocity = normalVec3(vVel);

				if(fVelocity > 0.01f)
				{
					bMoving = true;
				}
			}

			if(bMoving)
			{
				copyVec3(vBoundMin, e->pEntUI->vBoxMin);
				copyVec3(vBoundMax, e->pEntUI->vBoxMax);
				zeroVec3(e->pEntUI->vuiLowerAtTime);
			}
			else
			{
				// expand box or minimize it?
				for(i = 0; i < 3; ++i)		
				{
					F32 dif = vBoundMax[i] - vBoundMin[i];
					F32 oldDif = fabs(e->pEntUI->vBoxMax[i] - e->pEntUI->vBoxMin[i]);
					if(dif < gProjectGameClientConfig.fLowHeightThreshold * oldDif || 
						(e->pEntUI->vuiLowerAtTime[i] > 0
							&& gGCLState.totalElapsedTimeMs >= e->pEntUI->vuiLowerAtTime[i]
							&& dif < oldDif))
					{
						if(e->pEntUI->vuiLowerAtTime[i] <= 1)
						{
							F32 fHoldTime = gProjectGameClientConfig.fLowHeightHoldTime * (dif * dif) / (oldDif * oldDif + 0.001f);
							e->pEntUI->vuiLowerAtTime[i] = gGCLState.totalElapsedTimeMs + fHoldTime * 1000;
						}
						else if(gGCLState.totalElapsedTimeMs >= e->pEntUI->vuiLowerAtTime[i])
						{
							// change to smaller box
							F32 fAmount = oldDif * gProjectGameClientConfig.fHeightAdjustMentRate * fElaspedTime;
							if(vBoundMin[i] < e->pEntUI->vBoxMin[i])
							{
								e->pEntUI->vBoxMin[i] -= min(fAmount, e->pEntUI->vBoxMin[i] - vBoundMin[i]);	
							}
							else
							{
								e->pEntUI->vBoxMin[i] += min(fAmount, vBoundMin[i] - e->pEntUI->vBoxMin[i]);	
							}
							if(vBoundMax[i] < e->pEntUI->vBoxMax[i])
							{
								e->pEntUI->vBoxMax[i] -= min(fAmount, e->pEntUI->vBoxMax[i] - vBoundMax[i]);	
							}
							else
							{
								e->pEntUI->vBoxMax[i] += min(fAmount, vBoundMax[i] - e->pEntUI->vBoxMax[i]);	
							}
						}
					}
					else
					{
						if(vBoundMin[i] < e->pEntUI->vBoxMin[i])
						{
							e->pEntUI->vBoxMin[i] = vBoundMin[i];
						}
						if(vBoundMax[i] > e->pEntUI->vBoxMax[i])
						{
							e->pEntUI->vBoxMax[i] = vBoundMax[i];
						}
						e->pEntUI->vuiLowerAtTime[i] = 0;
					}
				}
				
				// current bounds for this entity
				copyVec3(e->pEntUI->vBoxMin, vBoundMin);
				copyVec3(e->pEntUI->vBoxMax, vBoundMax);
				
			}
		}
	}
	
#endif	// GAMECLIENT
	
}

// This returns local space min and max bounds. You must transform by the entity matrix to get world space bounds
void entGetLocalBoundingBox(Entity* e, Vec3 vBoundMin, Vec3 vBoundMax, bool bGetTargetingBounds)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	//F32 maxHeight = 0.0f;
	int i;
	const Capsule*const* capsules = NULL;
	PlayerCostume* pCostume;
	PCSkeletonDef* pPCSkel;
	DynSkeleton* pSkeleton;
	bool bGotSkeletonBounds = false;
	bool bUseSkeletonBounds = false;
	Vec3 vCapMin, vCapMax;
	F32 fHeight = 0.0f;
	
	if (!e)
	{
		zeroVec3(vBoundMin);
		zeroVec3(vBoundMax);
		return;
	}

	// only use this on non-moving entities, otherwise the capsules look better
	// First, try visibility extents on the skeleton
	pSkeleton = dynSkeletonFromGuid(e->dyn.guidSkeleton);
	pCostume = costumeEntity_GetEffectiveCostume(e);
	pPCSkel = pCostume ? GET_REF(pCostume->hSkeleton) : NULL;
	if (pSkeleton && (!pPCSkel || !pPCSkel->bUseCapsuleBoundsForTargeting || !bGetTargetingBounds))
	{
		bool bSnap = dynSkeletonIsForceVisible(pSkeleton);

		bGotSkeletonBounds = true;
	
		dynSkeletonGetVisibilityExtents(pSkeleton, vBoundMin, vBoundMax);
		
#if GAMECLIENT
		if (gProjectGameClientConfig.pScreenBoundingAccelConfig)
		{
			gclEntityScreenBounding_UpdateAndGetAdjustedTargetBoxBounds(e, vBoundMin, vBoundMax, bSnap);
			return;
		}
		else if(gProjectGameClientConfig.bUseFixedOverHead)
		{
			fHeight = vBoundMax[1] - vBoundMin[1];

			// adjust bounds based on both skeleton pRoot height changes as well as shifting of the
			// bounds box. Use the larger of the bounds boxes to prevent it from 'breathing'.
			entAdjustBounds(e, vBoundMin, vBoundMax);
			bUseSkeletonBounds = true;
		}
		else if (!gProjectGameClientConfig.bUseCapsuleBounds)
#endif
		{
			return;
		}		
	}

	if (!mmGetCapsules(SAFE_MEMBER(e, mm.movement), &capsules) && !bGotSkeletonBounds)
	{
		WorldInteractionNode *pNode = GET_REF(e->hCreatorNode);

		if (pNode)
		{
			wlInteractionNodeGetLocalBounds(pNode, vBoundMin, vBoundMax, NULL);
		}
		else
		{
			F32 wd, ht;
			zeroVec3(vBoundMin);
			zeroVec3(vBoundMax);
			wd = 3.0f;
			ht = 6.0f;
			vBoundMin[0] -= wd/2;
			vBoundMin[2] -= wd/2;
			vBoundMax[0] += wd/2;
			vBoundMax[2] += wd/2;
			vBoundMax[1] += ht;
		}
		return;
	}

	zeroVec3(vCapMin);
	zeroVec3(vCapMax);

	for (i = 0; i < eaSize(&capsules); i++)
	{
		const Capsule *capTarget = capsules[i];
		Vec3 p1, p2;
		int j;

		if (capTarget->iType != 0) continue;

		copyVec3(capTarget->vStart, p1);
		scaleVec3(capTarget->vDir, capTarget->fLength, p2);
		addVec3(p2, p1, p2);

		for (j = 0; j < 3; j++)
		{
			if (p1[j] + capTarget->fRadius > vCapMax[j])
			{
				vCapMax[j] = p1[j] + capTarget->fRadius;
			}
			if (p2[j] + capTarget->fRadius > vCapMax[j])
			{
				vCapMax[j] = p2[j] + capTarget->fRadius;
			}
			if (p1[j] - capTarget->fRadius < vCapMin[j])
			{
				vCapMin[j] = p1[j] - capTarget->fRadius;
			}
			if (p2[j] - capTarget->fRadius < vCapMin[j])
			{
				vCapMin[j] = p2[j] - capTarget->fRadius;
			}
		}
	}
	
#if GAMECLIENT
	if(gProjectGameClientConfig.bUseFixedOverHead)
	{
		if(bGotSkeletonBounds && !bUseSkeletonBounds)
		{
			// if skeleton Y is higher than bounding cap the increase cap	
			if(vCapMax[1] < vBoundMax[1])
			{
				vCapMax[1] = vBoundMax[1]; 
			}
			
			entAdjustBounds(e, vCapMin, vCapMax);
		}
		else if(bUseSkeletonBounds)
		{
			bool bOk = true;
			F32 capSize = vCapMax[1] - vCapMin[1];
				
			if(capSize < 0.01f)
			{
				capSize = 0.01f;
			}
			
			if
			(
				fHeight / capSize < gProjectGameClientConfig.fBadSkeletonY
			)
			{
				bOk = false;
			}
			
			if(bOk)
			{
				return;
			}
			// else		
			// something is wrong with the skeleton bounds
			// use the capsules and mark height as such
			if(e->pEntUI)
			{
				e->pEntUI->vBoxMax[1] = vCapMax[1];
			}
		}
	}
#endif
	
	copyVec3(vCapMin, vBoundMin);
	copyVec3(vCapMax, vBoundMax);
#endif
}


F32 entGetBoundingSphere(Entity* e, Vec3 vCenterOut)
{
	F32 fRadius = 0;
	
	Vec3 vMin, vMax, vLocalCenter, vCenterToMax;

	Mat4 mEntWorld;

	
	entGetLocalBoundingBox(e, vMin, vMax, false);
	entGetBodyMat(e, mEntWorld);

	addVec3( vMin, vMax, vLocalCenter );
	scaleByVec3( vLocalCenter, 0.5f );
	mulVecMat4(vLocalCenter, mEntWorld, vCenterOut);

	subVec3(vMax, vLocalCenter, vCenterToMax);
	return lengthVec3(vCenterToMax);
}

char *DEFAULT_LATELINK_entity_CreateProjSpecificLogString(Entity *e)
{
	return NULL;
}

void DEFAULT_LATELINK_GameSpecific_HolsterRequest(Entity* pEnt, Entity* pOwner, bool bUnholster)
{

}

void DEFAULT_LATELINK_GameSpecific_CreateShieldFX(Entity* pEnt)
{

}

int entLog_vprintf(int eCategory, Entity *e, const char *action, FORMAT_STR char const *oldFmt, va_list ap)
{
	int result;
	const char *owner;
	ContainerID owner_id;
	Vec3 pos;

	if (e->pPlayer)
	{
		owner = e->pPlayer->privateAccountName;
		owner_id = e->pPlayer->accountID;
	}
	else
	{
		owner = "";
		owner_id = 0;
	}

	entGetPos(e, pos);

	result = objLog_vprintf(eCategory, entGetType(e), entGetContainerID(e), owner_id,
		e->debugName, &pos, owner , action, entity_GetProjSpecificLogString(e), oldFmt, ap);
	return result;

}

#undef entLog
int entLog(int eCategory, Entity *e, const char *action, FORMAT_STR char const *fmt, ...)
{
	int result;
	va_list ap;
	const char *owner;
	ContainerID owner_id;
	Vec3 pos;

    if(!e)
    {
        Errorf("invalid call to entLog, NULL entity");
        return 0;
    }

	PERFINFO_AUTO_START_FUNC();

	if (e->pPlayer)
	{
		owner = e->pPlayer->privateAccountName;
		owner_id = e->pPlayer->accountID;
	}
	else
	{
		owner = "";
		owner_id = 0;
	}

	entGetPos(e, pos);


	va_start(ap, fmt);
	result = objLog_vprintf(eCategory, entGetType(e), entGetContainerID(e), owner_id, 
		e->debugName, &pos, owner , action, entity_GetProjSpecificLogString(e), fmt, ap);
	va_end(ap);

	PERFINFO_AUTO_STOP();

	return result;
}

int entLogWithStruct(int eCategory, Entity *e, const char *action, void *pStruct, ParseTable *pTPI)
{
	char *pTemp = NULL;
	char *pTempEscaped = NULL;
	int result;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pTemp);
	estrStackCreate(&pTempEscaped);

	ParserWriteText(&pTemp, pTPI, pStruct, 0, 0, TOK_NO_LOG);
	estrAppendEscaped(&pTempEscaped, pTemp);

	result = entLog(eCategory, e, action, "%s", pTempEscaped );

	estrDestroy(&pTemp);
	estrDestroy(&pTempEscaped);

	PERFINFO_AUTO_STOP();

	return result;
}

// entLog() using logPair()s
int entLogPairs(int eCategory, Entity *entity, const char *action, ...)
{
	va_list args;
	char *estrPairs = NULL;
	int result;

	// Create name-value pairs list.
	estrStackCreate(&estrPairs);
	va_start(args, action);
	logvAppendPairs(&estrPairs, args);
	va_end(args);

	// Send entLog().
	result = entLog(eCategory, entity, action, "%s", estrPairs);
	estrDestroy(&estrPairs);
	return result;
}

#undef entPrintf
void entPrintf(Entity *e, FORMAT_STR char const *fmt, ...)
{
	if (e)
	{	
		va_list ap;
		va_start(ap, fmt);
		objvPrintf(entGetType(e), entGetContainerID(e), fmt, ap);
		va_end(ap);
	}
}

void entExternInitializeCommon(Entity *ent)
{

}

void entExternCleanupCommon(Entity *ent)
{

}


#if GAMESERVER || GAMECLIENT
static void entMovementConflictResolver(MovementOwnershipConflict* conflict){
	MovementConflictResolution	resolution = conflict->out.resolution;
	U32							ownerClassID = conflict->in.mrOwnerClassID;
	U32							requesterClassID = conflict->in.mrRequesterClassID;
	S32							checkOwner = 0;
	S32							checkRequester = 0;

	switch(requesterClassID){
		xcase MR_CLASS_ID_TEST:
		acase MR_CLASS_ID_DISABLE:
		acase MR_CLASS_ID_DOOR_GEO:
		acase MR_CLASS_ID_DOOR:{
			resolution = MCR_RELEASE_ALLOWED;
		}
		xdefault:{
			checkOwner = 1;
		}
	}

	if(checkOwner){
		switch(ownerClassID){
			xcase MR_CLASS_ID_DISABLE:
			acase MR_CLASS_ID_DOOR_GEO:
			acase MR_CLASS_ID_DOOR:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_DEAD:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_AI_CIVILIAN:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_PROJECTILE:
					acase MR_CLASS_ID_POWERS:
					acase MR_CLASS_ID_DEAD:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xcase MR_CLASS_ID_EMOTE:{
						resolution = MCR_ASK_OWNER;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_DEAD:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_PROJECTILE:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_RAGDOLL:{
				switch(requesterClassID){
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_PROJECTILE:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_PROJECTILE:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xcase MR_CLASS_ID_DEAD:{
						resolution = MCR_ASK_OWNER;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_PUSH:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_PROJECTILE:
					acase MR_CLASS_ID_PUSH:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_TARGETED_ROTATION:
			acase MR_CLASS_ID_FLIGHT:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_SURFACE:{
						resolution = MCR_RELEASE_DENIED;
					}
					xcase MR_CLASS_ID_EMOTE:{
						resolution = MCR_ASK_OWNER;
					}
					xdefault:{
						resolution = MCR_RELEASE_ALLOWED;
					}
				}
			}
			
			xcase MR_CLASS_ID_SWING:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_SURFACE:{
						resolution = MCR_RELEASE_DENIED;
					}
					xdefault:{
						resolution = MCR_RELEASE_ALLOWED;
					}
				}
			}

			xcase MR_CLASS_ID_TACTICAL:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_DEAD:{
						// Let tactical say no if it's rolling.

						resolution = MCR_ASK_OWNER;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_AI:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_POWERS:
					acase MR_CLASS_ID_PROJECTILE:
					acase MR_CLASS_ID_PUSH:
					acase MR_CLASS_ID_RAGDOLL:
					acase MR_CLASS_ID_DEAD:
					acase MR_CLASS_ID_INTERACTION:
					acase MR_CLASS_ID_GRAB:
					acase MR_CLASS_ID_AI_CIVILIAN:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xcase MR_CLASS_ID_SURFACE:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_EMOTE:{
						resolution = MCR_ASK_OWNER;
					}
					
					xdefault:{
						resolution = MCR_RELEASE_ALLOWED;
					}
				}
			}

			xcase MR_CLASS_ID_TEST:{
				resolution = MCR_RELEASE_DENIED;
			}

			xcase MR_CLASS_ID_POWERS:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_AI:
					acase MR_CLASS_ID_AI_CIVILIAN:
					acase MR_CLASS_ID_SURFACE:
					acase MR_CLASS_ID_EMOTE:{
						resolution = MCR_RELEASE_DENIED;
					}
					xdefault:{
						resolution = MCR_RELEASE_ALLOWED;
					}
				}
			}
			
			xcase MR_CLASS_ID_INTERACTION:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_DEAD:{
						// If you die during an interaction, the interaction has to be allowed to
						// clean up (put you in a valid position, etc).

						resolution = MCR_ASK_OWNER;
					}
					xcase MR_CLASS_ID_PROJECTILE:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}
			
			xcase MR_CLASS_ID_GRAB:{
				switch(requesterClassID){
					xcase MR_CLASS_ID_DEAD:
					acase MR_CLASS_ID_PROJECTILE:{
						resolution = MCR_RELEASE_ALLOWED;
					}
					xdefault:{
						resolution = MCR_RELEASE_DENIED;
					}
				}
			}

			xdefault:{
				resolution = MCR_RELEASE_ALLOWED;
			}
		}
	}

	conflict->out.resolution = resolution;
}
#endif

AUTO_RUN;
void entStartupSetupMovement(void)
{
#if GAMESERVER || GAMECLIENT
	mmSetOwnershipConflictResolver(entMovementConflictResolver);

	worldLibSetEntRefreshCallback(refreshAllEntMovement);

	// Register movement requester class IDs.
#endif
}

bool entIsTargetable(Entity *e)
{
	if (!e->pChar)
	{
		return 0;
	}
	if (entGetFlagBits(e) & ENTITYFLAG_UNTARGETABLE ||
		entGetFlagBits(e) & ENTITYFLAG_UNSELECTABLE )
	{
		return 0;
	}
	return 1;
}

bool entIsSelectable(Character *source, Entity *eTarget)
{
	if(!eTarget)
		return 0;
	
	if (!eTarget->pChar)
	{
		if (source && source->pEntParent && source->pEntParent->pPlayer)
		{	// allowing players to be able to select Civilians
			return entCheckFlag(eTarget, ENTITYFLAG_CIVILIAN) == ENTITYFLAG_CIVILIAN;
		}

		return 0;
	}

	// Can't target mount while you're riding it
	if (source && entGetRider(eTarget) == source->pEntParent)	
		return 0;


	if(source && ea32Find(&source->perUntargetable,eTarget->myRef) != -1)
		return 0;

	if(entGetFlagBits(eTarget) & ENTITYFLAG_UNSELECTABLE)
	{
		return 0;
	}
	return 1;
}


bool entReportsOnTargetClick(Entity *e)
{
	return entIsCivilian(e);
}

const char* entGetLangNameUntranslated(const Entity* pEnt, bool* pbIsMessageKey)
{
	Entity* pRider;
	(*pbIsMessageKey) = false;
	if (pRider = entGetRider(pEnt))
	{
		return entGetLangNameUntranslated(pRider, pbIsMessageKey);
	}
	else if (pEnt->pCritter && pEnt->pCritter->displayNameOverride && pEnt->pCritter->displayNameOverride[0])
	{
		return pEnt->pCritter->displayNameOverride;
	}
	else if (pEnt->pSaved && pEnt->pSaved->savedName && pEnt->pSaved->savedName[0])
	{
		return pEnt->pSaved->savedName;
	}
	else if (pEnt->pCritter) 
	{
		Message* pMsg = GET_REF(pEnt->pCritter->hDisplayNameMsg);
		if (pMsg)
		{
			(*pbIsMessageKey) = true;
			return pMsg->pcMessageKey;
		}
		return pEnt->debugName;
	}
	return pEnt->debugName;
}

const char* entGetLangName(const Entity* pEnt, Language eLang)
{
	bool bIsMessageKey = false;
	const char* pchResult = entGetLangNameUntranslated(pEnt, &bIsMessageKey);
	if (bIsMessageKey)
	{
		pchResult = langTranslateMessageKey(eLang, pchResult);
	}
	return pchResult;
}

bool entGetGenderNameFromString(const Entity* pEnt, Language eLangID, const char* pchString, char** pestrOut)
{
	if (pEnt && pestrOut)
	{
		langFormatGameString(eLangID, pestrOut, pchString, STRFMT_PLAYER(pEnt), STRFMT_END);
		return true;
	}	
	return false;
}

const char *entGetLangSubName(const Entity *ent, Language eLang)
{
	if (ent->pSaved && ent->pSaved->savedSubName && ent->pSaved->savedSubName[0])
	{
		return ent->pSaved->savedSubName;
	}
	else if (ent->pCritter) 
	{
		const char *pName = langTranslateMessageRef(eLang, ent->pCritter->hDisplaySubNameMsg);
		return pName ? pName : ent->debugName;
	}
	else
	{
		return ent->debugName;
	}
}

// Why would a translate function ever return a debug string?  I don't have time to find
// out if anything in STO relies on that behavior right now; hopefully entGetLangSubName()
// can be removed later.	-SIP 22 FEB 2013
const char *entGetLangSubNameNoDebug(const Entity *ent, Language eLang)
{
	if (ent->pSaved && ent->pSaved->savedSubName && ent->pSaved->savedSubName[0])
	{
		return ent->pSaved->savedSubName;
	}
	else if (ent->pCritter) 
	{
		return langTranslateMessageRef(eLang, ent->pCritter->hDisplaySubNameMsg);
	}
	return NULL;
}
const char *entGetAccountOrLangName(const Entity *ent, Language eLang)
{
    if (ent && ent->pPlayer && ent->pPlayer->publicAccountName && *ent->pPlayer->publicAccountName)
    {
        return ent->pPlayer->publicAccountName;
    } 
	else
		return ent ? entGetLangName(ent, eLang) : "(null)";
}

const char *entGetPersistedName(const Entity *ent)
{
	if (devassertmsgf(ent->pSaved, "Attempted to get persisted name on non-persisted entity %s", ent->debugName))
	{
		return (ent->pSaved->savedName && *ent->pSaved->savedName) ? ent->pSaved->savedName : ent->debugName;
	}
	else
		return ent->debugName;
}

Message *entGetClassNameMsg(const Entity *pEnt)
{
	Message *classMessage = NULL;

	if (devassertmsgf(pEnt->pChar, "Attempted to get class name on entity %s\n", pEnt->debugName))
	{
		CharacterClass *pClass = character_GetClassCurrent(pEnt->pChar);

		if(pClass)
			return GET_REF(pClass->msgDisplayName.hMessage);
	}
	return NULL;
}

Entity *entGetOwner(Entity *e)
{
	if (e && e->erOwner)
	{
		return entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
	}
	return NULL;
}

Entity *entGetCreator(Entity *e)
{
	if (e && e->erCreator)
	{
		return entFromEntityRef(entGetPartitionIdx(e), e->erCreator);
	}
	return NULL;
}

bool entIsPrimaryPet(Entity *e)
{
	if (e && e->pChar && e->pChar->bIsPrimaryPet)
	{
		return true;
	}
	return false;
}

Entity *entGetPrimaryPet(Entity *e)
{
	if (e && e->pChar && e->pChar->primaryPetRef)
	{
		return entFromEntityRef(entGetPartitionIdx(e), e->pChar->primaryPetRef);
	}
	return NULL;
}

Entity *entGetMount(Entity *e)
{
	if (e && e->pAttach && e->pAttach->bRiding)
	{
		return entFromEntityRef(entGetPartitionIdx(e), e->pAttach->erAttachedTo);
	}
	return NULL;
}

Entity *entGetRider(const Entity *e)
{
	if (e && e->pAttach && ea32Size(&e->pAttach->eaiAttached))
	{
		Entity *eRider = entFromEntityRef(entGetPartitionIdx(e), e->pAttach->eaiAttached[0]);
		if (eRider && eRider->pAttach && eRider->pAttach->bRiding)
			return eRider;
	}
	return NULL;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt1, ".Psaved.Conowner.Containertype, .Psaved.Conowner.Containerid")
ATR_LOCKS(pEnt2, ".Psaved.Conowner.Containertype, .Psaved.Conowner.Containerid");
bool entity_trh_IsOwnerSame(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt1, ATH_ARG NOCONST(Entity) *pEnt2)
{
	if (ISNULL(pEnt1) || ISNULL(pEnt2))
		return false;

	if (ISNULL(pEnt1->pSaved) || ISNULL(pEnt2->pSaved))
		return false;

	// check if they're the same
	if (pEnt1->myEntityType == pEnt2->myEntityType && pEnt1->myContainerID == pEnt2->myContainerID)
		return true;

	// check if one owns the other
	if (pEnt1->pSaved->conOwner.containerType == GLOBALTYPE_NONE)
		return (pEnt2->pSaved->conOwner.containerType == pEnt1->myEntityType && pEnt2->pSaved->conOwner.containerID == pEnt1->myContainerID);
	if (pEnt2->pSaved->conOwner.containerType == GLOBALTYPE_NONE)
		return (pEnt1->pSaved->conOwner.containerType == pEnt2->myEntityType && pEnt1->pSaved->conOwner.containerID == pEnt2->myContainerID);

	// check if owners are the same
	return (pEnt1->pSaved->conOwner.containerType == pEnt2->pSaved->conOwner.containerType &&
		pEnt1->pSaved->conOwner.containerID == pEnt2->pSaved->conOwner.containerID);
}

// Adds or removes an externally-owned innate Power on the Entity
S32 entity_UpdatePowerExternalInnate(Entity *pent, PowerDef *pdef, S32 bAdd)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	S32 bDirty = false;
	Character *pchar = pent->pChar;

	if(pent->pCritter && GET_REF(pent->pCritter->critterDef) && GET_REF(pent->pCritter->critterDef)->bIgnoreExternalInnates)
	{
		return bDirty;
	}

	if(verify(pdef->eType==kPowerType_Innate))
	{
		int i = -1;

		PERFINFO_AUTO_START_FUNC();

		// See if we already own it
		if(pent->externalInnate)
		{
			for(i=eaSize(&pent->externalInnate->ppPowersExternalInnate)-1; i>=0; i--)
			{
				if(pdef==GET_REF(pent->externalInnate->ppPowersExternalInnate[i]->hDef))
				{
					break;
				}
			}
		}

		if(bAdd)
		{
			if(i<0)
			{
				// Don't already own it
				Power *ppow = power_Create(pdef->pchName);
				power_InitHelper(CONTAINER_NOCONST(Power, ppow),0);

				// Make sure we've got the wrapper structure
				if(!pent->externalInnate)
				{
					pent->externalInnate = StructCreate(parse_EntityExternalInnate);
				}

				eaPush(&pent->externalInnate->ppPowersExternalInnate,ppow);

				if(pchar)
				{
					character_EnterStance(entGetPartitionIdx(pent),pchar,ppow,NULL,false,pmTimestamp(0));
				}

#ifdef GAMESERVER
				if(pent->aibase)
				{
					aiAddPower(pent, pent->aibase, ppow);
				}
#endif
				bDirty = true;
			}
			else
			{
				// TODO(JW): Hack: If you've already got it, increase the charge count
				Power *ppow = pent->externalInnate->ppPowersExternalInnate[i];
				ppow->iChargesUsed++;
			}
		}
		else
		{
			if(i>=0)
			{
				Power *ppow = pent->externalInnate->ppPowersExternalInnate[i];
				if(ppow->iChargesUsed > 0)
				{
					// TODO(JW): Hack: If you've got more than one copy, decrease the charge count
					ppow->iChargesUsed--;
				}
				else
				{
					if(pchar)
					{
						PowerAnimFX *pafx = GET_REF(pdef->hFX);
						if(pafx)
						{
  							character_ExitStance(pchar,pafx,power_AnimFXID(pent->externalInnate->ppPowersExternalInnate[i]),pmTimestamp(0));
						}
					}

					power_Destroy(pent->externalInnate->ppPowersExternalInnate[i],pchar);
					eaRemoveFast(&pent->externalInnate->ppPowersExternalInnate,i);
					bDirty = true;
				}
			}
		}

		if(bDirty)
		{
			entity_SetDirtyBit(pent, parse_Entity, pent, false);
			if(pchar)
			{
				// Regular Character, just do regular Character-based Powers processing
				character_DirtyInnatePowers(pchar);
#ifdef GAMESERVER
				ClientCmd_PowersDirtyInnates(pent,0);
#endif GAMESERVER
			}
			else
			{
				// Non-Character, do custom accrual and update required systems
				entity_AccrueModsInnate(entGetPartitionIdx(pent), pent);
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return bDirty;
#endif
}

static U32 s_uiPowerVolumeType = 0;

static void EntityExternalInnateVolumeEnter(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache)
{
	if(!s_uiPowerVolumeType)
	{
		s_uiPowerVolumeType = wlVolumeTypeNameToBitMask("Power");
	}

	if(wlVolumeIsType(pVolume,s_uiPowerVolumeType))
	{
		WorldVolumeEntry *pEntry = wlVolumeGetVolumeData(pVolume);
		Entity *pEnt = wlVolumeQueryCacheGetData(pQueryCache);
		if(pEntry->server_volume.power_volume_properties)
		{
			PowerDef *pdef = GET_REF(pEntry->server_volume.power_volume_properties->power);
			if(pdef && pdef->eType==kPowerType_Innate)
			{
				entity_UpdatePowerExternalInnate(pEnt,pdef,true);
			}
		}
	}
}

static void EntityExternalInnateVolumeExit(WorldVolume *pVolume, WorldVolumeQueryCache *pQueryCache)
{
	if(!s_uiPowerVolumeType)
	{
		s_uiPowerVolumeType = wlVolumeTypeNameToBitMask("Power");
	}

	if(wlVolumeIsType(pVolume,s_uiPowerVolumeType))
	{
		WorldVolumeEntry *pEntry = wlVolumeGetVolumeData(pVolume);
		Entity *pEnt = wlVolumeQueryCacheGetData(pQueryCache);
		if(pEntry->server_volume.power_volume_properties)
		{
			PowerDef *pdef = GET_REF(pEntry->server_volume.power_volume_properties->power);
			if(pdef && pdef->eType==kPowerType_Innate)
			{
				entity_UpdatePowerExternalInnate(pEnt,pdef,false);
			}
		}
	}
}

// Notes that the entity entered or exited a volume that grants an innate power
void entity_ExternalInnateUpdateVolumeCount(Entity *pent, S32 bEnter)
{
	PERFINFO_AUTO_START_FUNC();

	if(!pent->externalInnate)
	{
		pent->externalInnate = StructCreate(parse_EntityExternalInnate);
		entity_SetDirtyBit(pent, parse_Entity, pent, false);
	}

	if(bEnter)
	{
		pent->externalInnate->iPowerVolumes++;

		// Make sure we have a volume cache
		if(!pent->externalInnate->pPowerVolumeCache)
		{
			devassert(s_EntityVolumeQueryType);
			pent->externalInnate->pPowerVolumeCache = wlVolumeQueryCacheCreate(entGetPartitionIdx(pent), s_EntityVolumeQueryType, pent);
			wlVolumeQuerySetCallbacks(pent->externalInnate->pPowerVolumeCache,EntityExternalInnateVolumeEnter,EntityExternalInnateVolumeExit,NULL);
		}
	}
	else
	{
		if(pent->externalInnate->iPowerVolumes > 0)
		{
			pent->externalInnate->iPowerVolumes--;

			// If we're at 0, free the volume cache
			if(!pent->externalInnate->iPowerVolumes)
			{
				wlVolumeQueryCacheFree(pent->externalInnate->pPowerVolumeCache);
				pent->externalInnate->pPowerVolumeCache = NULL;
			}
		}
		else
		{
			Errorf("Entity exiting more innate power volumes than it has entered");
		}
	}

	PERFINFO_AUTO_STOP();
}

// does one of two things, to allow me to streamline the code below.  Only one of pTargetEnt and pTargetEntry should be set
// returns true if the coll object is the target
static bool _checkCollisionObject(int iPartitionIdx, WorldCollObject * pCollObject, Entity *pTargetEnt, WorldInteractionEntry *pTargetEntry)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (pTargetEnt)
	{
		Entity * pEntHit;
		mmGetUserPointerFromWCO(pCollObject, &pEntHit);
		return (pEntHit == pTargetEnt);
	}
	else if(pTargetEntry)
	{
		return wlInteractionCheckCollObject(iPartitionIdx,pTargetEntry,pCollObject);
	}

	// Both can be NULL
	return false;
#endif
}

// if bRequireBothWays is true, we demand that the raycast be blocked BOTH ways. Expensive. Regrettable.
static bool _checkBlocked(int iPartitionIdx,Vec3 vSource, Vec3 vTarget, WorldCollCollideResults * pResults, bool bRequireBothWays)
{
	WorldCollCollideResults ignoredResults;

	if (!worldCollideRay(iPartitionIdx,vSource,vTarget,WC_QUERY_BITS_COMBAT,pResults))
		return false;

	if (bRequireBothWays && !worldCollideRay(iPartitionIdx,vTarget,vSource,WC_QUERY_BITS_COMBAT,&ignoredResults))
		return false;

	return true;
}

// Verifies interaction distance and line of sight
bool entity_VerifyInteractTarget(int iPartitionIdx, Entity *ent, Entity *eTarget, WorldInteractionNode *pTargetNode, U32 uNodeInteractDist, Vec3 vNodePosFallback, F32 fNodeRadiusFallback, bool bForPickup, InteractValidity* peFailOut)
{
	Vec3 vSource, vTarget;
	F32 uInteractRange = 0;
	F32 fDist;
	WorldCollCollideResults wcResults;
	WorldInteractionEntry * pTargetEntry = NULL;
	bool bRequireBothWays;

	if (eTarget == NULL && pTargetNode == NULL)
	{
		// why did you even call this function?
		return false;
	}

	// Adam introduced a change that was designed to prevent players from exploiting AI's by standing inside of one-way collision.
	// That change created a paradigm where interacts could be used through collision.  This code maintains that behavior.
	bRequireBothWays = gConf.bLegacyInteractBehaviorInUGC && zmapIsUGCGeneratedMap(NULL);

	PERFINFO_AUTO_START_FUNC();

#if GAMECLIENT
	uInteractRange = gclEntity_GetInteractRange(ent, eTarget, uNodeInteractDist);
#elif GAMESERVER
	uInteractRange = gslEntity_GetInteractRange(ent, eTarget, pTargetNode);
#endif

	entGetCombatPosDir(ent, NULL, vSource, NULL);

	if(eTarget)
	{
		fDist = entGetDistance( ent, NULL, eTarget, NULL, NULL );

		if(fDist > uInteractRange)
		{
			if (peFailOut)
				*peFailOut = kInteractValidity_OutOfRange;
			PERFINFO_AUTO_STOP();
			return false;
		}

		// not sure if this is the right function to call here - if this is a body on the ground, for example, there's probably
		// not much benefit to picking a point near his chest height on his capsule.
		entGetCombatPosDir(eTarget, NULL, vTarget, NULL);
	}
	else
	{
		devassert(pTargetNode);

		if(!entity_IsNodeInRange(ent, vSource, pTargetNode, bForPickup ? entity_GetPickupRange(ent) : uInteractRange, vNodePosFallback, fNodeRadiusFallback, vTarget, &fDist, false))
		{
			if (peFailOut)
				*peFailOut = kInteractValidity_OutOfRange;
			PERFINFO_AUTO_STOP();
			return false;
		}

		pTargetEntry = wlInteractionNodeGetEntry(pTargetNode);
	}

	if(_checkBlocked(iPartitionIdx, vSource, vTarget, &wcResults, bRequireBothWays))
	{
		if (!_checkCollisionObject(iPartitionIdx,wcResults.wco,eTarget,pTargetEntry))
		{
			bool bCanReach = false;
			Vec3 vEntityPos;

			entGetPos(ent,vEntityPos);

			if (vTarget[1] > vEntityPos[1])
			{
				// cast another ray horizontally out to the character, in case we're an object on a shelf or something like that
				bool bCheckHigh = false;
				bool bCheckLow = false;
				Vec3 vNewSource, vNewTarget;
				copyVec3(vSource,vNewSource);
				copyVec3(vTarget,vNewTarget);
				bCanReach = true;

				// Things that are more than a foot lower than my "reach" location qualify as "down in a basket"
				if (vSource[1]-1.0f > vTarget[1])
				{
					bCheckLow = true;
					vNewTarget[1] = vSource[1];
				}
				else
				{
					// Things must be more than a foot higher than my "reach" location to qualify as "up on a shelf".  
					if (vSource[1]+1.0f < vTarget[1])
					{
						bCheckHigh = true;
					}
					vNewSource[1] = vTarget[1];
				}

				
				// Do this to compensate for the fact that  wlInterationNode_FindNearestPoint doesn't really return the nearest point
				if (bCheckLow || bCheckHigh)
				{
					// We are reaching up onto a shelf or something.  I'm going to put a limit on how long my arm can be.
					F32 fReachDistSq = distance3SquaredXZ(vNewTarget,vNewSource);
					F32 fChestHeight = vSource[1]-vEntityPos[1];
					if (fReachDistSq > fChestHeight*fChestHeight)
					{
						bCanReach = false;
					}
					else if (bCheckHigh)
					{
						// Make sure we're not trying to reach up through a ceiling or other obstacle
						if (worldCollideRay(iPartitionIdx, vSource, vNewSource, WC_QUERY_BITS_COMBAT, &wcResults))
						{
							bCanReach = false;
						}
					}
					else // bCheckLow
					{
						// Make sure we're not trying to reach down through a floor or other obstacle
						if (worldCollideRay(iPartitionIdx, vTarget, vNewTarget, WC_QUERY_BITS_COMBAT, &wcResults))
						{
							bCanReach = false;
						}
					}
				}

				if (bCanReach)
				{
					// do the horizontal check
					if (_checkBlocked(iPartitionIdx,vNewSource,vNewTarget,&wcResults,bRequireBothWays) && 
						!_checkCollisionObject(iPartitionIdx,wcResults.wco,eTarget,pTargetEntry))
					{
						bCanReach = false;
					}
				}
			}

			if (!bCanReach)
			{
				if (peFailOut)
					*peFailOut = kInteractValidity_LineOfSight;
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}


// this function performs a series of cheap tests before finally calling wlInterationNode_FindNearestPoint
// if bCheapTestsOnly is set, this will run a set of cheaper tests instead of more expensive ones
// NOTE: if this function returns false, distance and position outputs will be invalid
bool entity_IsNodeInRange(	Entity* pEntity, Vec3 vEntSource, WorldInteractionNode* pNode, F32 fRange, Vec3 vNodePosFallback, F32 fNodeRadiusFallback, 
						  Vec3 vTargetOut, F32* fDistOut, bool bCheapTestsOnly )
{
	Vec3 vEntPos, vNodePos, vDiff;
	F32 fEntRadius, fNodeRadius, fDist;
	Vec3 vPos;
#if GAMECLIENT
	Vec3 vCachedEntPos;
	wlInteractionNodeGetCachedPlayerPos(pNode, vCachedEntPos);
#endif

	if ( !pEntity || !pEntity->pChar )
		return false;

	fEntRadius = entGetBoundingSphere(pEntity,vEntPos);
	if(wlInteractionNodeGetEntry(pNode))
	{
		fNodeRadius = wlInteractionNodeGetSphereBounds(pNode,vNodePos);
	}
	else
	{
		fNodeRadius = fNodeRadiusFallback;
		if(vNodePosFallback)
			copyVec3(vNodePosFallback, vNodePos);
		else
			return false; // Per SD'Angelo: The node’s position is unknown so we cannot determine range.
	}

	subVec3(vEntPos, vNodePos, vDiff);

	fDist = lengthVec3(vDiff);

	if ( bCheapTestsOnly && fDist <= fRange )
	{
		//provide rough approximations of target position and distance
		(*fDistOut) = fDist - fEntRadius*0.5 - fNodeRadius*0.5;
		copyVec3(vNodePos,vTargetOut);
		return true;
	}

	if ( fDist - fEntRadius - fNodeRadius > fRange )
		return false;
	
	if ( vEntSource )
	{
		copyVec3( vEntSource, vPos );
	}
	else
	{
		entGetPos(pEntity,vPos);
		vPos[1] += 5.f;
	}

	if ( bCheapTestsOnly )
	{
		// For gameclient, if entry is not present or if the player has not moved, use cached 
		// nearest point.  Server should always recalculate the nearest point.
#if GAMECLIENT
		if(!wlInteractionNodeGetEntry(pNode) || sameVec3(vCachedEntPos, vEntPos))
		{
			wlInteractionNodeGetCachedNearestPoint(pNode, vTargetOut);
			
			// If no cached point is found, and we know the midpoint of the node, use the midpoint as the closest point
			if(vec3IsZero(vTargetOut) && vNodePosFallback && !vec3IsZero(vNodePosFallback))
			{
				copyVec3(vNodePosFallback, vTargetOut);
			}
		}
		else
		{
			wlInterationNode_FindNearestPointFast( vPos, pNode, vTargetOut );
			wlInteractionNodeSetCachedNearestPoint(pNode, vTargetOut);
		}
#else
		wlInterationNode_FindNearestPointFast( vPos, pNode, vTargetOut );
#endif

		(*fDistOut) = entGetDistance( pEntity, vPos, NULL, vTargetOut, NULL );

		if ( (*fDistOut) > fRange )
			return false;
	}
	else
	{
		// For gameclient, if entry is not present or if the player has not moved, use cached 
		// nearest point.  Server should always recalculate the nearest point.
#if GAMECLIENT
		if(!wlInteractionNodeGetEntry(pNode) || sameVec3(vCachedEntPos, vEntPos))
		{
			wlInteractionNodeGetCachedNearestPoint(pNode, vTargetOut);

			// If no cached point is found, and we know the midpoint of the node, use the midpoint as the closest point
			if(vec3IsZero(vTargetOut) && vNodePosFallback && !vec3IsZero(vNodePosFallback))
			{
				copyVec3(vNodePosFallback, vTargetOut);
			}
		}
		else
		{
			wlInterationNode_FindNearestPoint( vPos, pNode, vTargetOut );
			wlInteractionNodeSetCachedNearestPoint(pNode, vTargetOut);
		}
#else
		wlInterationNode_FindNearestPoint( vPos, pNode, vTargetOut );
#endif
		(*fDistOut) = entGetDistance( pEntity, vPos, NULL, vTargetOut, NULL );

		return ( (*fDistOut) <= fRange );
	}

#if GAMECLIENT
		wlInteractionNodeSetCachedPlayerPos(pNode, vEntPos);
#endif
	
	return true;
}

static int entity_FindInvisibleNode(VisibleOverrideNode** ppNodes, WorldInteractionNode* pNode)
{
	int i;
	for (i = 0; i < eaSize(&ppNodes); i++)
	{
		if (GET_REF(ppNodes[i]->hNode) == pNode) return i;
	}
	return -1;
}

static int entity_FindTargetableNode(TargetableNode** ppNodes, WorldInteractionNode* pNode)
{
	int i;
	for (i = 0; i < eaSize(&ppNodes); i++)
	{
		if (GET_REF(ppNodes[i]->hNode) == pNode) return i;
	}
	return -1;
}

void entity_UpdateTargetableNodes( Entity* ent )
{
#if GAMECLIENT
	static U32 iDestructibleMask;
	int i, n;
	Player* pPlayer = ent->pPlayer;
	RegionRules* pRules;
	F32 glowDist = -1;
	static ExprContext* context = NULL;
	static ExprFuncTable* s_FuncTable = NULL;
	MultiVal mvResult;

	// We have a list of TargetableNodes that the server determined we might be interested in.
	// These are stored in ppTargetableNodes. Each one wraps a WorldInteractionNode.
	// We keep track of a list of such TargetableNodes that we saw last frame and do stuff
	// based on if nodes have appeared or disappeared from the list.
	//  The ppTargetableNodes is at least partially set up in gslInteraction.

	if (gConf.bUseGlobalExpressionForInteractableGlowDistance)
	{

		if(!context)
		{
			context = exprContextCreate();
			s_FuncTable = exprContextCreateFunctionTable();
			exprContextAddFuncsToTableByTag(s_FuncTable, "util");
			exprContextAddFuncsToTableByTag(s_FuncTable, "player");
			exprContextAddFuncsToTableByTag(s_FuncTable, "entity");
			exprContextSetFuncTable(context, s_FuncTable);
			exprContextSetAllowRuntimeSelfPtr(context);
			exprContextSetAllowRuntimePartition(context);
		}

		exprContextSetPartition(context, entGetPartitionIdx(ent));
		exprContextSetSelfPtr(context, ent);
		exprContextSetPointerVarPooled(context, g_PlayerVarName, ent, parse_Entity, true, true);

		if (exprIsNonGenerated(g_GlobalExpressions.pInteractGlowDistance))
			exprGenerate(g_GlobalExpressions.pInteractGlowDistance, context);

		exprEvaluate(g_GlobalExpressions.pInteractGlowDistance, context, &mvResult);
		glowDist = MultiValGetFloat(&mvResult, false);

	}


	if(!pPlayer)
		return;

	pRules = getRegionRulesFromEnt(ent);

	if ( !iDestructibleMask )
	{
		iDestructibleMask = wlInteractionClassNameToBitMask("Destructible");
	}


	// Remove invalid nodes and reveal them.
	for(i=eaSize(&pPlayer->InteractStatus.ppVisibleNodesLast)-1; i>=0; --i)
	{
		WorldInteractionNode* pNode = GET_REF( pPlayer->InteractStatus.ppVisibleNodesLast[i]->hNode );

		if ( pNode==NULL )
		{
			StructDestroy( parse_VisibleOverrideNode, eaRemoveFast( &pPlayer->InteractStatus.ppVisibleNodesLast, i ) );
			continue;
		}
		if(entity_FindInvisibleNode(pPlayer->InteractStatus.ppVisibleNodes, pNode) == -1)
		{
			WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pNode);

			if ( pEntry )
			{
				mapState_SetNodeVisibleOverride(pNode, false);

				StructDestroy( parse_VisibleOverrideNode, eaRemoveFast( &pPlayer->InteractStatus.ppVisibleNodesLast, i ) );
			}
		}
	}

	// Check the new list against the list of invisible nodes last tick
	n = eaSize(&pPlayer->InteractStatus.ppVisibleNodes);
	for(i=0; i<n; i++)
	{
		WorldInteractionNode* pNode = GET_REF( pPlayer->InteractStatus.ppVisibleNodes[i]->hNode );

		if ( pNode==NULL )
			continue;

		if ( wlInteractionClassMatchesMask( pNode, iDestructibleMask ) )
			continue;

		// If the clickable is new this tick and it is a valid entry, make it glow
		if(entity_FindInvisibleNode(pPlayer->InteractStatus.ppVisibleNodesLast, pNode) == -1)
		{
			WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pNode);

			if ( pEntry )
			{
				VisibleOverrideNode* pInvisibleNode = StructCreate( parse_VisibleOverrideNode );

				COPY_HANDLE( pInvisibleNode->hNode, (pPlayer->InteractStatus.ppVisibleNodes)[i]->hNode );

				eaPush( &pPlayer->InteractStatus.ppVisibleNodesLast, pInvisibleNode );

				mapState_SetNodeVisibleOverride(pNode, true);
			}
		}
	}

	if (!pRules || !pRules->bDisableTargetableFX)
	{
		// Run through list of nodes from last frame and 
		// Remove invalid nodes, and either turn off or change old effects
		for(i=eaSize(&pPlayer->InteractStatus.ppTargetableNodesLast)-1; i>=0; --i)
		{
			WorldInteractionNode* pNode = GET_REF( pPlayer->InteractStatus.ppTargetableNodesLast[i]->hNode );
			WorldInteractionEntry* pEntry;

			if ( pNode==NULL )
			{
				StructDestroy( parse_TargetableNode, eaRemoveFast( &pPlayer->InteractStatus.ppTargetableNodesLast, i ) );
				continue;
			}
			
			pEntry = wlInteractionNodeGetEntry(pNode);
			if ( pEntry )
			{
				int iIndexInCurrentNodes = entity_FindTargetableNode(pPlayer->InteractStatus.ppTargetableNodes, pNode);
			
				if(iIndexInCurrentNodes == -1)
				{
					if (!gConf.bClickiesGlowOnMouseover)
						worldInteractionEntrySetFX(PARTITION_CLIENT, pEntry, NULL, NULL);
					
					StructDestroy( parse_TargetableNode, eaRemoveFast( &pPlayer->InteractStatus.ppTargetableNodesLast, i ) );
				}
				else
				{
					// It was around last frame and this frame. See if it's attemptable status has changed and change the FX accordingly.
					if (pPlayer->InteractStatus.ppTargetableNodesLast[i]->bIsAttemptable != pPlayer->InteractStatus.ppTargetableNodes[iIndexInCurrentNodes]->bIsAttemptable)
					{
						// We changed status. Start a new FX
						// Update the status in the node.
						
						const char* pchNodeFX = wlInteractionGetInteractFXForNode(pNode,  pPlayer->InteractStatus.ppTargetableNodes[iIndexInCurrentNodes]->bIsAttemptable);	
						const char* pchAdditionalNodeFX = wlInteractionGetAdditionalUniqueInteractFXForNode(pNode);	

						pPlayer->InteractStatus.ppTargetableNodesLast[i]->bIsAttemptable = pPlayer->InteractStatus.ppTargetableNodes[iIndexInCurrentNodes]->bIsAttemptable;
						if (!gConf.bClickiesGlowOnMouseover)
						{
							worldInteractionEntrySetFX(PARTITION_CLIENT, pEntry, pchNodeFX, pchAdditionalNodeFX);
						}
					}
				}
			}
		} 
	}

	n = eaSize(&pPlayer->InteractStatus.ppTargetableNodes);

	//don't show targetable FX for regions that have bDisableTargetableFX set
	if (!pRules || !pRules->bDisableTargetableFX)
	{
		// Check the new list against the list of interactable nodes last tick
		for(i=0; i<n; i++)
		{
			WorldInteractionNode* pNode = GET_REF( pPlayer->InteractStatus.ppTargetableNodes[i]->hNode );

			if ( pNode==NULL )
				continue; 

			if ( wlInteractionClassMatchesMask( pNode, iDestructibleMask ) )
				continue;

			// If the clickable is new this tick and it is a valid entry, make it glow
			if(entity_FindTargetableNode(pPlayer->InteractStatus.ppTargetableNodesLast, pNode) == -1)
			{
				Vec3 vClose;
				F32 fDist;		
				F32 fTargetDist = (F32)wlInteractionGetTargetDistForNode(pNode);

				if (fTargetDist <= 0.0f)
				{
					if (pRules && pRules->fDefaultInteractTargetDist > 0.0f)
					{
						fTargetDist = pRules->fDefaultInteractTargetDist;
					}
					else
					{
						fTargetDist =  DEFAULT_NODE_TARGET_DIST;
					}
				}
				
				if (entity_IsNodeInRange(ent, NULL, pNode, fTargetDist, 0, 0, vClose, &fDist, false) && 
					(glowDist <= -1 || entity_IsNodeInRange(ent, NULL, pNode, glowDist, 0, 0, vClose, &fDist, false)))
				{
					WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pNode);
					
					if (pEntry)
					{
						TargetableNode* pTargetableNode = StructCreate( parse_TargetableNode );
						const char* pchNodeFX = wlInteractionGetInteractFXForNode(pNode,  pPlayer->InteractStatus.ppTargetableNodes[i]->bIsAttemptable);
						const char* pchAdditionalNodeFX = wlInteractionGetAdditionalUniqueInteractFXForNode(pNode);	

						COPY_HANDLE( pTargetableNode->hNode, (pPlayer->InteractStatus.ppTargetableNodes)[i]->hNode );

						eaPush( &pPlayer->InteractStatus.ppTargetableNodesLast, pTargetableNode );
						if (!gConf.bClickiesGlowOnMouseover)
							worldInteractionEntrySetFX(PARTITION_CLIENT, pEntry, pchNodeFX, pchAdditionalNodeFX);
					}
				}
			}
		}
	}

	// Old-style NNO glow on mouseover
	if (gConf.bClickiesGlowOnMouseover)
	{
		WorldInteractionNode* pMouseNode = target_SelectObjectUnderMouse(ent, 0);
		REF_TO(WorldInteractionNode) hNewGlowyNodeRef;

		COPY_HANDLE(hNewGlowyNodeRef,s_OldGlowyNodeRef); // We need to initialize New to something before we use it

		// Find the node in the targetables. 
		if (pMouseNode)
		{
			int index;
			
			index = entity_FindTargetableNode(pPlayer->InteractStatus.ppTargetableNodes, pMouseNode);
			if (index >= 0)
			{
				WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(pMouseNode);
				if (pEntry)
				{
					const char* pchNodeFX = wlInteractionGetInteractFXForNode(pMouseNode, pPlayer->InteractStatus.ppTargetableNodes[index]->bIsAttemptable);
					const char* pchAdditionalNodeFX = wlInteractionGetAdditionalUniqueInteractFXForNode(pMouseNode);	

					// This should do the right thing if we pass the same FX every frame. It should only change if the FX does (I hope) WOLF[24Aug12]
					worldInteractionEntrySetFX(PARTITION_CLIENT, pEntry, pchNodeFX, pchAdditionalNodeFX);

					COPY_HANDLE(hNewGlowyNodeRef,pPlayer->InteractStatus.ppTargetableNodes[index]->hNode);
				}
			}
		}
		
		// Make sure we cancel the old glow if there was one
		if (GET_REF(s_OldGlowyNodeRef) != GET_REF(hNewGlowyNodeRef))
		{
			WorldInteractionEntry* pEntry = wlInteractionNodeGetEntry(GET_REF(s_OldGlowyNodeRef));
			if (pEntry)
			{
				worldInteractionEntrySetFX(PARTITION_CLIENT, pEntry, NULL, NULL);
			}

			// And set the old glow reference
			if (GET_REF(hNewGlowyNodeRef))
			{
				COPY_HANDLE(s_OldGlowyNodeRef, hNewGlowyNodeRef);
			}
			else
			{
				REMOVE_HANDLE(s_OldGlowyNodeRef);
			}
		}
		REMOVE_HANDLE(hNewGlowyNodeRef);
	}
#endif
}

//if iType is not GLOBALTYPE_ENTITYSAVEDPET, this function just returns entFromContainerID, otherwise it
//assumes the container id is referring to one of this entity's subscribed containers
SA_RET_OP_VALID Entity* entity_GetSubEntity(int iPartitionIdx, Entity* pOwner, GlobalType iType, ContainerID iContainerID)
{
	int i, iPetArraySize;

	if (iType != GLOBALTYPE_ENTITYSAVEDPET && iType != GLOBALTYPE_ENTITYSHAREDBANK && iType != GLOBALTYPE_ENTITYGUILDBANK)
		return entFromContainerID(iPartitionIdx, iType,iContainerID);

	if (pOwner==NULL || pOwner->pSaved==NULL)
		return NULL;

	if (iType == GLOBALTYPE_ENTITYSHAREDBANK)
	{
		Player *pPlayer = pOwner->pPlayer;
		return pPlayer ? GET_REF(pPlayer->hSharedBank) : NULL;
	}

	if (iType == GLOBALTYPE_ENTITYGUILDBANK)
	{
		Player *pPlayer = pOwner->pPlayer;
		return pPlayer && pPlayer->pGuild ? GET_REF(pPlayer->pGuild->hGuildBank) : NULL;
	}

	if (pOwner->pSaved->pPuppetMaster && 
		pOwner->pSaved->pPuppetMaster->curType == iType && 
		pOwner->pSaved->pPuppetMaster->curID == iContainerID)
	{
		return pOwner;
	}
	iPetArraySize = eaSize(&pOwner->pSaved->ppOwnedContainers);

	for (i = 0; i < iPetArraySize; i++)
	{
		PetRelationship* pPet = pOwner->pSaved->ppOwnedContainers[i];

		Entity* pEnt = GET_REF(pPet->hPetRef);

		if (pEnt && pEnt->myEntityType == iType && pEnt->myContainerID == iContainerID)
		{
			if (pEnt->pChar && !pEnt->pChar->pEntParent)
			{
				pEnt->pChar->pEntParent = pEnt;
			}
			return pEnt;
		}
	}
	return NULL;
}

SA_RET_OP_VALID PuppetEntity* entity_GetPuppetByTypeEx(SA_PARAM_NN_VALID Entity* pEnt, unsigned int uiClass, SA_PARAM_OP_VALID CharClassCategorySet *pSet, bool bGetActive)
{
	if (SAFE_MEMBER3(pEnt, pSaved, pPuppetMaster, ppPuppets))
	{
		int i, iSize = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets);
		for (i = 0; i < iSize; i++)
		{
			PuppetEntity* pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
			if (pPuppet->eType == uiClass
				&& (!bGetActive || pPuppet->eState == PUPPETSTATE_ACTIVE)
				&& (!pSet || CharClassCategorySet_checkIfPassPuppetEntity(pSet, entGetPartitionIdx(pEnt), pPuppet)))
			{
				return pPuppet;
			}
		}
	}
	return NULL;
}

SA_RET_OP_VALID Entity* entity_GetPuppetEntityByType(SA_PARAM_NN_VALID Entity* pEnt, 
												  const char* pchClassType, 
												  CharClassCategorySet *pSet, 
												  bool bGetActualEntity, 
												  bool bGetActive)
{
	PuppetEntity* pPuppet = entity_GetPuppetByType(pEnt, pchClassType, pSet, bGetActive);
	Entity* pPupEnt = NULL;

	if (bGetActualEntity && pPuppet && pPuppet->curID == pEnt->pSaved->pPuppetMaster->curID)
	{
		return pEnt;
	}
	pPupEnt = pPuppet ? GET_REF(pPuppet->hEntityRef) : NULL;
	if (pPupEnt && pPupEnt->pChar && !pPupEnt->pChar->pEntParent)
	{
		pPupEnt->pChar->pEntParent = pPupEnt;
	}
	return pPupEnt;
}

Entity* entity_CreateOwnerCopy( Entity* pOwner, Entity* pEnt, bool bCopyInventory, bool bCopyNumerics, bool bCopyPowerTrees, bool bCopyPuppets, bool bCopyAttribs)
{
	S32 i;
	NOCONST(Entity)* pEntCopy = StructCreateWithComment(parse_Entity, "Ent returned by entity_CreateOwnerCopy");

	pEntCopy->pChar = StructCreateNoConst(parse_Character);

	pEntCopy->pChar->pEntParent = (Entity*)pEntCopy;
	pEntCopy->myEntityType = pEnt->myEntityType;
	pEntCopy->myContainerID = pEnt->myContainerID;
	strcpy(pEntCopy->debugName, "FakeEntity");
	pEntCopy->bFakeEntity = true;

	COPY_HANDLE(pEntCopy->pChar->hClass,pEnt->pChar->hClass);
	COPY_HANDLE(pEntCopy->pChar->hPath, pEnt->pChar->hPath);
	eaCopyStructsDeConst(&pEnt->pChar->ppSecondaryPaths, &pEntCopy->pChar->ppSecondaryPaths, parse_AdditionalCharacterPath);
	COPY_HANDLE(pEntCopy->pChar->hSpecies, pEnt->pChar->hSpecies);
	COPY_HANDLE(pEntCopy->hFaction, pOwner->hFaction);
	COPY_HANDLE(pEntCopy->hSubFaction, pOwner->hSubFaction);
	COPY_HANDLE(pEntCopy->hAllegiance, pOwner->hAllegiance);
	COPY_HANDLE(pEntCopy->hSubAllegiance, pOwner->hSubAllegiance);
	pEntCopy->pPlayer = StructCreateNoConst(parse_Player);

	inv_ent_trh_VerifyInventoryData(ATR_EMPTY_ARGS,pEntCopy,true,true,NULL);

	if ( pEnt->pCritter )
	{
		pEntCopy->pCritter = StructCreateNoConst(parse_Critter);
	}

	if ( bCopyInventory )
	{
		StructCopyAllNoConst(parse_Inventory, CONTAINER_NOCONST(Inventory, pEnt->pInventoryV2), pEntCopy->pInventoryV2);
	}
	else if ( bCopyNumerics )
	{
		inv_trh_CopyNumerics( ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pOwner), pEntCopy, NULL );
	}

	if ( bCopyPowerTrees )
	{
		for ( i = 0; i < eaSize( &pEnt->pChar->ppPowerTrees ); i++ )
		{
			eaPush( &pEntCopy->pChar->ppPowerTrees,
					StructCloneDeConst( parse_PowerTree, pEnt->pChar->ppPowerTrees[i] ) );
		}

		pEntCopy->pChar->uiLastFreeRespecTime = pEnt->pChar->uiLastFreeRespecTime;
		pEntCopy->pChar->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;
	}

	//copy the combat level
	pEntCopy->pChar->iLevelExp = entity_GetSavedExpLevel(pOwner);
	pEntCopy->pChar->iLevelCombat = pEntCopy->pChar->iLevelExp;

	if ( bCopyAttribs )
	{
		pEntCopy->pChar->pattrBasic = StructCloneDeConst(parse_CharacterAttribs, pEnt->pChar->pattrBasic);
	}
	

	if ( bCopyPuppets && pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
	{
		if(!pEntCopy->pSaved)
			pEntCopy->pSaved = StructCreateNoConst(parse_SavedEntityData);
		if(!pEntCopy->pSaved->pPuppetMaster)
			pEntCopy->pSaved->pPuppetMaster = StructCreateNoConst(parse_PuppetMaster);
		for(i = 0; i < eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets); i++) {
			NOCONST(PuppetEntity)* pPup = StructCreateNoConst(parse_PuppetEntity);
			pPup->curID = pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curID;
			pPup->curType = pEnt->pSaved->pPuppetMaster->ppPuppets[i]->curType;
			pPup->eState = pEnt->pSaved->pPuppetMaster->ppPuppets[i]->eState;
			pPup->eType = pEnt->pSaved->pPuppetMaster->ppPuppets[i]->eType;
			COPY_HANDLE(pPup->hEntityRef, pEnt->pSaved->pPuppetMaster->ppPuppets[i]->hEntityRef);
			eaPush(&pEntCopy->pSaved->pPuppetMaster->ppPuppets, pPup);
		}
	}

	return (Entity*)pEntCopy;
}

int Entity_GetSeedNumber(int iPartitionIdx, Entity *pEntity, Vec3 vSpawnPos)
{
	int iSeedNumber = 0;
	int *eaSeeds = NULL;
	int iSpawnBoxes[3];

	if(!pEntity)
		return 0;

	if(pEntity->myEntityType == GLOBALTYPE_ENTITYPLAYER)
	{
		//Players only get different seed numbers if they are on a team
		ea32Create(&eaSeeds);
		if(team_IsMember(pEntity))
		{
			Team *pTeam = GET_REF(pEntity->pTeam->hTeam);

#ifdef GAMESERVER 
			if(!pTeam)
			{
				pTeam = gslTeam_GetTeam(pEntity->pTeam->iTeamID);
			}
#endif

			if(pTeam)
			{
				int i;
				for(i=eaSize(&pTeam->eaMembers)-1;i>=0;i--)
				{
					Entity *pCurrEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);

					if(pTeam->eaMembers[i]->iEntID == pEntity->myContainerID)
						continue;

					if(!pCurrEnt)
						continue;

					ea32Push(&eaSeeds,pCurrEnt->iSeedNumber);
					if(pCurrEnt->iSeedNumber == 0)
					{
						iSpawnBoxes[0] = pCurrEnt->iBoxNumber[0];
						iSpawnBoxes[1] = pCurrEnt->iBoxNumber[1];
						iSpawnBoxes[2] = pCurrEnt->iBoxNumber[2];
					}
				}
			}
			else
			{	
				EntityIterator * iter = entGetIteratorSingleType(iPartitionIdx,0,0, GLOBALTYPE_ENTITYPLAYER);
				Entity *pCurrEnt;

				while(pCurrEnt = EntityIteratorGetNext(iter))
				{
					if(pCurrEnt != pEntity && pCurrEnt->pTeam)
					{
						if(pCurrEnt->pTeam->iTeamID == pEntity->pTeam->iTeamID)
						{
							ea32Push(&eaSeeds,pCurrEnt->iSeedNumber);

							if(pCurrEnt->iSeedNumber == 0)
							{
								iSpawnBoxes[0] = pCurrEnt->iBoxNumber[0];
								iSpawnBoxes[1] = pCurrEnt->iBoxNumber[1];
								iSpawnBoxes[2] = pCurrEnt->iBoxNumber[2];
							}
						}
					}
				}

				EntityIteratorRelease( iter );
			}

			for(iSeedNumber=0;ea32Find(&eaSeeds,iSeedNumber)!=-1;iSeedNumber++)
			{
				//Do nothing
			}
			pEntity->iSeedNumber = iSeedNumber;

			if(iSeedNumber!=0)
			{
				pEntity->iBoxNumber[0] = iSpawnBoxes[0];
				pEntity->iBoxNumber[1] = iSpawnBoxes[1];
				pEntity->iBoxNumber[2] = iSpawnBoxes[2];
			}
		}
		ea32Destroy(&eaSeeds);
	}
	if(pEntity->myEntityType == GLOBALTYPE_ENTITYSAVEDPET
		|| pEntity->myEntityType == GLOBALTYPE_ENTITYCRITTER)
	{
		Entity *pOwner = pEntity->myEntityType == GLOBALTYPE_ENTITYSAVEDPET ?
			entFromContainerID(iPartitionIdx,pEntity->pSaved->conOwner.containerType,pEntity->pSaved->conOwner.containerID) :
			entFromEntityRef(iPartitionIdx,pEntity->erOwner);

		if(team_IsMember(pOwner))
		{
			Team *pTeam = GET_REF(pOwner->pTeam->hTeam);
			int i;
			
			iSeedNumber = TEAM_MAX_SIZE;

#ifdef GAMESERVER 
			if(!pTeam)
			{
				pTeam = gslTeam_GetTeam(pOwner->pTeam->iTeamID);
			}
#endif
			if(pTeam)
			{
				for(i=0;i<eaSize(&pTeam->eaMembers);i++)
				{
					Entity *pCurrEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pTeam->eaMembers[i]->iEntID);

					if (pCurrEnt==NULL)
						continue;

					if(pCurrEnt->iSeedNumber < pOwner->iSeedNumber)
					{
						iSeedNumber -= ea32Size(&pCurrEnt->pSaved->ppRequestedPetIDs);
						iSeedNumber -= ea32Size(&pCurrEnt->pSaved->ppRequestedCritterIDs);
					}
				}
			}
			else
			{	
				EntityIterator * iter = entGetIteratorSingleType(iPartitionIdx,0,0,GLOBALTYPE_ENTITYPLAYER);
				Entity *pCurrEnt;

				while(pCurrEnt = EntityIteratorGetNext(iter))
				{
					if(pCurrEnt != pEntity && pCurrEnt->pTeam)
					{
						if(pCurrEnt->pTeam->iTeamID == pOwner->pTeam->iTeamID
							&& pCurrEnt->iSeedNumber < pOwner->iSeedNumber)
						{
							iSeedNumber -= ea32Size(&pCurrEnt->pSaved->ppRequestedPetIDs);
							iSeedNumber -= ea32Size(&pCurrEnt->pSaved->ppRequestedCritterIDs);
						}
					}
				}

				EntityIteratorRelease( iter );
			}

			for(i=0;i<eaSize(&pOwner->pSaved->ppOwnedContainers);i++)
			{
				if(pOwner->pSaved->ppOwnedContainers[i]->eState == OWNEDSTATE_ACTIVE
					&& pOwner->pSaved->ppOwnedContainers[i]->bTeamRequest)
				{
					iSeedNumber--;
				}

				if(pEntity->myEntityType == GLOBALTYPE_ENTITYSAVEDPET 
					&& pOwner->pSaved->ppOwnedContainers[i]->conID == pEntity->myContainerID)
					break;
			}

			if(pEntity->myEntityType == GLOBALTYPE_ENTITYCRITTER)
			{
				for(i=0;i<eaSize(&pOwner->pSaved->ppCritterPets);i++)
				{
					if(pOwner->pSaved->ppCritterPets[i]->pEntity)
						iSeedNumber++;
					if(pOwner->pSaved->ppCritterPets[i]->pEntity == pEntity)
						break;
				}
			}
		}
		else if(pOwner)
		{
			int i;

			for(i=0;i<eaSize(&pOwner->pSaved->ppOwnedContainers);i++)
			{
				if(pOwner->pSaved->ppOwnedContainers[i]->eState == OWNEDSTATE_ACTIVE
					&& pOwner->pSaved->ppOwnedContainers[i]->bTeamRequest)
				{
					iSeedNumber++;
				}

				if(pEntity->myEntityType == GLOBALTYPE_ENTITYSAVEDPET
					&& pOwner->pSaved->ppOwnedContainers[i]->conID == pEntity->myContainerID)
					break;
			}

			if(pEntity->myEntityType == GLOBALTYPE_ENTITYCRITTER)
			{
				for(i=0;i<eaSize(&pOwner->pSaved->ppCritterPets);i++)
				{
					if(pOwner->pSaved->ppCritterPets[i]->pEntity)
						iSeedNumber++;
					if(pOwner->pSaved->ppCritterPets[i]->pEntity == pEntity)
						break;
				}
			}
		}
	}
	//printf("Entity %d[%s] got Seed Number %d",pEntity->myContainerID,pEntity->debugName,iSeedNumber);

#if GAMECLIENT || GAMESERVER
	if(iSeedNumber==0)
	{
		Entity_FindSpawnBox(pEntity,vSpawnPos);
	}
#endif

	return iSeedNumber;
}

F32 entity_GetPickupRange( Entity* e )
{
	Power *pPower = (e && e->pChar) ? character_FindPowerByCategory(e->pChar,"Pickup") : NULL;
	PowerDef *pDef = pPower ? GET_REF(pPower->hDef) : NULL;

	if ( pDef )
	{
		S32 i = eaSize(&pDef->ppOrderedCombos) - 1;
		PowerCombo* pCombo = i >= 0 ? pDef->ppOrderedCombos[i] : NULL;
		PowerDef* pPowerComboDef = pCombo ? GET_REF(pCombo->hPower) : NULL;

		if ( pPowerComboDef )
			return pPowerComboDef->fRange;
	}

	return INTERACT_PICKUP_RANGE;
}

// Attempts to get the default interaction range based on the entity's current region.
F32 entity_GetCurrentRegionInteractDist(Entity *entPlayer)
{
#if GAMESERVER || GAMECLIENT
	Vec3 vPos;
	WorldRegion *pRegion;

	entGetPos(entPlayer,vPos);
	pRegion = worldGetWorldRegionByPos(vPos);
	if(pRegion)
	{
		RegionRules* pRules = getRegionRulesFromRegionType(worldRegionGetType(pRegion));

		if ( pRules && pRules->fDefaultInteractDist > 0.0f )
		{
			return pRules->fDefaultInteractDist;
		}
	}
#endif
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// Functions to manage / track experience points.

// Most of the time people will care about their experience level,
// not their effective level.

// These functions need to be fast, so don't do stupid stuff like
// CLAMP(ExpensiveFunction(...), 1, 60).

// Not allowed to return anything less than 1
// Also will only use experience to calculate level if game account data is available. Otherwise just return current value
// Added new parameter bUseFullLevel which is used by the power tree to have correct powers and respecs regardless of capped levels
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, pInventoryV2.ppLiteBags[]");
S32 entity_trh_CalculateExpLevelSlow(ATH_ARG NOCONST(Entity) *pEnt, bool bUseFullLevel)
{
	S32 iLevelingNumeric;
	S32 iLevel;
	S32 iPermissionLevel;

	PERFINFO_AUTO_START_FUNC();

	iLevelingNumeric = item_trh_GetLevelingNumeric(ATR_EMPTY_ARGS, pEnt);
	iLevel = LevelFromLevelingNumeric(iLevelingNumeric);
	if(gamePermission_Enabled() && !bUseFullLevel)
	{
		// level restriction from new permission system
		iPermissionLevel = GamePermissions_trh_GetCachedMaxNumeric(pEnt, GAME_PERMISSION_NUMERIC_LEVEL, true);
		iLevel = MIN(iLevel, iPermissionLevel);
	}

	PERFINFO_AUTO_STOP();

	return MAX(iLevel, 1);
}

S32 entity_CalculateExpLevelSlow(Entity *pEnt) {
	S32 iResult = entity_trh_CalculateExpLevelSlow(CONTAINER_NOCONST(Entity, pEnt), false);
	return iResult;
}

// Use full levels not capped
S32 entity_CalculateFullExpLevelSlow(Entity *pEnt) {

	S32 iResult = entity_trh_CalculateExpLevelSlow(CONTAINER_NOCONST(Entity, pEnt), true);
	return iResult;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.iLevelExp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics");
S32 entity_trh_GetSavedExpLevelLimited(ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar)) {
		int iLevel = pEnt->pChar->iLevelExp;
		if (gamePermission_Enabled())
		{
			// level restriction from new permission system
			int iPermissionLevel = GamePermissions_trh_GetCachedMaxNumeric(pEnt, GAME_PERMISSION_NUMERIC_LEVEL, true);
			iLevel = MIN(iLevel, iPermissionLevel);
		}
		return iLevel;
	}
	return 1;
}

S32 entity_GetSavedExpLevelLimited(Entity *pEnt)
{
	if (pEnt && pEnt->pChar) {
		int iLevel = pEnt->pChar->iLevelExp;
		if (gamePermission_Enabled())
		{
			// level restriction from new permission system
			int iPermissionLevel = GamePermissions_trh_GetCachedMaxNumeric((NOCONST(Entity)*)pEnt, GAME_PERMISSION_NUMERIC_LEVEL, true);
			iLevel = MIN(iLevel, iPermissionLevel);
		}
		return iLevel;
	}
	return 1;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Pchar.iLevelExp");
S32 entity_trh_GetSavedExpLevel(ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar) && NONNULL(pEnt->pPlayer)) {
		// Only return the exp level for players for now
		return MAX(pEnt->pChar->iLevelExp, 1);
	}
	return 1;
}

S32 entity_GetSavedExpLevel(Entity *pEnt) 
{
	if (pEnt && pEnt->pChar && pEnt->pPlayer) {
		// Only return the exp level for players for now
		return MAX(pEnt->pChar->iLevelExp, 1);
	}
	return 1;
}

S32 entity_ExpOfCurrentExpLevel(Entity *pEnt){
	return NUMERIC_AT_LEVEL(LevelFromLevelingNumeric(item_GetLevelingNumeric(pEnt)));
}

S32 entity_ExpOfNextExpLevel(Entity *pEnt)
{
	S32 iLevelingNumeric = item_GetLevelingNumeric(pEnt);
	S32 iNextLevel = LevelFromLevelingNumeric(iLevelingNumeric) + 1;
	return NUMERIC_AT_LEVEL(iNextLevel);
}

S32 entity_ExpToNextExpLevel(Entity *pEnt)
{
	S32 iLevelingNumeric = item_GetLevelingNumeric(pEnt);
	S32 iNextLevel = LevelFromLevelingNumeric(iLevelingNumeric) + 1;
	return NUMERIC_AT_LEVEL(iNextLevel) - iLevelingNumeric;
}

F32 entity_PercentToNextExpLevel(Entity *pEnt)
{
	S32 iLevelingNumeric = item_GetLevelingNumeric(pEnt);
	S32 iExpLevel = LevelFromLevelingNumeric(iLevelingNumeric);
	S32 iCurrent = NUMERIC_AT_LEVEL(iExpLevel);
	S32 iNext = NUMERIC_AT_LEVEL(iExpLevel + 1);
	F32 fDelta = iNext - iCurrent;
	F32 fPercent = iNext && fDelta ? ((F32)(iLevelingNumeric - iCurrent)) / fDelta : 0;
	fPercent = CLAMP(fPercent, 0, 1);
	return fPercent;
}

const char *entity_GetChatBubbleDefName(Entity *pEnt)
{
	const char *pchBubble = NULL;
	if (pEnt)
	{
		if (pEnt->pCritter)
		{
			CritterDef *pCritterDef = GET_REF(pEnt->pCritter->critterDef);
			CritterGroup *pGroup = pCritterDef ? GET_REF(pCritterDef->hGroup) : NULL;
			if (pGroup)
				pchBubble = REF_STRING_FROM_HANDLE(pGroup->hChatBubbleDef);
		}
	}
	// TODO: Do something else for players.
	if (!pchBubble)
		pchBubble = "Default";
	return "Default";
}

AUTO_COMMAND;
void printEntityList(void){
	EntityIterator* iter = entGetIteratorAllTypesAllPartitions(0,0);
	Entity*			e;

	while(e = EntityIteratorGetNext(iter)){
		Vec3 pos;
		
		entGetPos(e, pos);
		
		printf(	"0x%8.8x: \"%s\" pos(%1.1f, %1.1f, %1.1f)\n",
				entGetRef(e),
				e->debugName,
				vecParamsXYZ(pos));
	}

	EntityIteratorRelease(iter);
}

void entity_SetDirtyBitInternal(Entity *ent,ParseTable table[], void *pStruct, bool bSubStructsChanged, MEM_DBG_PARMS_VOID)
{
	if(ParserSetDirtyBit_sekret(table,pStruct,bSubStructsChanged MEM_DBG_PARMS_CALL) && ent)
	{
		ent->dirtyBitSet = 1;
		// total count, I guess
		ParserSetDirtyBit_sekret(parse_Entity, ent, false MEM_DBG_PARMS_INIT);
	}
}

// Split from entity_PlayerHasSkillSpecialization to allow access by transaction code that won't lock the entire player
AUTO_TRANS_HELPER_SIMPLE;
bool entity_PlayerHasSkillSpecializationInternal(const int * const * eSkillSpecialization, ItemTag eItemTag)
{
	S32 i;
	
	for(i = 0; i < eaiSize(eSkillSpecialization); ++i)
	{
		if(*eSkillSpecialization[i] == eItemTag)
		{
			return true;
		}
	}
	
	return false;
}

// check for skill specialization (simple)
AUTO_TRANS_HELPER;
bool entity_trh_PlayerHasSkillSpecialization(ATH_ARG NOCONST(Entity) *pEnt, ItemTag eItemTag)
{
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		return false;
	}

	if(entity_PlayerHasSkillSpecializationInternal(&pEnt->pPlayer->eSkillSpecialization, eItemTag))
	{
		return true;
	}

	return false;
}

AUTO_TRANS_HELPER;
bool entity_trh_CraftingCheckTag(ATH_ARG NOCONST(Entity) *pEnt, ItemTag eTag) 
{
	if(!Item_ItemTagRequiresSpecialization(eTag)) return true;
	if(entity_trh_PlayerHasSkillSpecialization(pEnt, eTag)) return true;

	return false;
}

PlayerDebug* entGetPlayerDebug(Entity* e, bool create)
{
	if(!e || !e->pPlayer)
		return NULL;

#ifdef GAMESERVER
	if(create && !e->pPlayer->debugInfo)
	{
		e->pPlayer->debugInfo = StructCreate(parse_PlayerDebug);
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
#endif

	return e->pPlayer->debugInfo;
}

// Gets the PlayerPetInfo off of a Player based on the pet ref
PlayerPetInfo *player_GetPetInfo(Player *pPlayer, EntityRef erPet)
{
	if (pPlayer)
	{
		int i;
		for(i=eaSize(&pPlayer->petInfo)-1; i>=0; i--)
		{
			if(pPlayer->petInfo[i]->iPetRef==erPet)
				return pPlayer->petInfo[i];
		}
	}
	return NULL;
}

// Gets the cooldown timer for a power category on the specified pet.
PetCooldownTimer *player_GetPetCooldownTimerForCategory(Player *pPlayer, EntityRef erPet, S32 iCategory)
{
	PlayerPetInfo *pPetInfo = player_GetPetInfo(pPlayer, erPet);
	if(pPetInfo)
	{
		int i;
		for(i=eaSize(&pPetInfo->ppCooldownTimers)-1; i>=0; i--)
		{
			if(pPetInfo->ppCooldownTimers[i]->iPowerCategory==iCategory)
			{
				return pPetInfo->ppCooldownTimers[i];
			}
		}
	}
	return NULL;
}

// Gets the cooldown time from a PowerDef on the specified pet
F32 player_GetPetCooldownFromPowerDef(Player *pPlayer, EntityRef erPet, PowerDef *pDef)
{
	F32 fCooldown = 0.0f;
	if(pDef)
	{
		int i;
		for(i=0;i<ea32Size(&pDef->piCategories);i++)
		{
			S32 iCategory = pDef->piCategories[i];
			PetCooldownTimer *pTimer = player_GetPetCooldownTimerForCategory(pPlayer,erPet,iCategory);
			if(pTimer && pTimer->fCooldown > fCooldown)
			{
				fCooldown = pTimer->fCooldown;
			}
		}
	}
	return fCooldown;
}

// Gets the cooldown time from a power on the specified pet
F32 player_GetPetCooldownFromPower(Player *pPlayer, EntityRef erPet, Power *pPower)
{
	PowerDef *pDef = NULL;

	if(pPower && pPower->pParentPower) 
	{
		pPower = pPower->pParentPower;
	}
	if(pPower)
	{
		pDef = GET_REF(pPower->hDef);
	}
	return player_GetPetCooldownFromPowerDef(pPlayer, erPet, pDef);
}

// Gets the PetPowerState off of a Player based on the pet and PowerDef.  Should probably be in Player.c/h, if such
//  a thing existed
PetPowerState *player_GetPetPowerState(Player *pPlayer, EntityRef erPet, PowerDef *pdef)
{
	PlayerPetInfo *pPetInfo = player_GetPetInfo(pPlayer, erPet);

	if (pPetInfo)
	{
		int i;
		for(i=eaSize(&pPetInfo->ppPowerStates)-1; i>=0; i--)
		{
			if(pdef==GET_REF(pPetInfo->ppPowerStates[i]->hdef))
				return pPetInfo->ppPowerStates[i];
		}
	}
	return NULL;
}

// finds a PetPowerState by the powerDef name on a PlayerPetInfo, returns NULL if none found
PetPowerState* playerPetInfo_FindPetPowerStateByName(PlayerPetInfo *pPetInfo, const char *pchName)
{
	if (pPetInfo && (pchName = allocFindString(pchName)))
	{
		FOR_EACH_IN_EARRAY(pPetInfo->ppPowerStates, PetPowerState, pPetPowerState)
			PowerDef *pPowerDef = GET_REF(pPetPowerState->hdef);
			if (pPowerDef && pPowerDef->pchName == pchName)
				return pPetPowerState;
		FOR_EACH_END
	}
	return NULL;
}

bool entity_IsMissionReturnAvailable(void)
{
	if (!gConf.bEnableMissionReturnCmd)
		return false;
	if (zmapInfoGetMapType(NULL) == ZMTYPE_STATIC)
	{
		if (!(zmapInfoGetParentMapName(NULL) && zmapInfoGetByPublicName(zmapInfoGetParentMapName(NULL))))
		{
			return false;
		}
	}
	return true;
}

bool entity_IsMissionReturnEnabled(Entity* ent)
{
	if (!gConf.bEnableMissionReturnCmd)
		return false;
	if (!ent || !ent->pChar)
		return false;
	if (!entIsAlive(ent) && !g_CombatConfig.bForceRespawnOnMapLeave)
		return false;

	return !character_HasMode(ent->pChar,kPowerMode_Combat);
}

MissionReturnErrorType entity_GetReturnFromMissionMapError(Entity* ent)
{
	if ( !entity_IsMissionReturnAvailable() )
		return MissionReturnErrorType_InvalidMap;
	if ( !entity_IsMissionReturnEnabled(ent) )
		return MissionReturnErrorType_InCombat;

	return MissionReturnErrorType_None;
}

SavedMapDescription* entity_GetMapLeaveDestination(Entity* ent)
{
	SavedMapDescription* pPossibleDest = entity_GetLastStaticMap(ent);
	SavedMapDescription* pCurrentMapDesc = entity_GetLastMap(ent); 

	if ( pPossibleDest != pCurrentMapDesc )
	{
		return pPossibleDest;
	}
	return NULL;
}

// Get the combat level of this entity. Never returns a value of less than 1
S32 entity_GetCombatLevel(Entity* pEnt)
{
	S32 iCombatLevel = 1;

	if(pEnt)
	{
		if(pEnt->pChar)
		{
			iCombatLevel = pEnt->pChar->iLevelCombat;
			if(iCombatLevel < 1)
			{
				if (pEnt->myEntityType == GLOBALTYPE_ENTITYSAVEDPET) {
					Entity* pOwner = entFromContainerIDAnyPartition(pEnt->pSaved->conOwner.containerType,pEnt->pSaved->conOwner.containerID);
					if (pOwner && pOwner->pChar) {
						if (pOwner->pChar->iLevelCombat < 1) {
							iCombatLevel = entity_GetSavedExpLevel(pOwner);
						} else {
							iCombatLevel = pOwner->pChar->iLevelCombat;
						}
					} else {
						iCombatLevel = entity_GetSavedExpLevel(pEnt);
					}
				} else {
					iCombatLevel = entity_GetSavedExpLevel(pEnt);
				}
				// This assert is here in order to help debug combat level == 0 (which should never happen but is)
				ErrorDetailsf("Character %s, CID %d, Flags %d, ExpLevel %d, Loaded %d", CHARDEBUGNAME(pEnt->pChar), pEnt->myContainerID, pEnt->myEntityFlags, iCombatLevel, pEnt->pChar->bLoaded);
				ErrorfForceCallstack("Character combat level < 1");
			}
		}
		else
		{
			iCombatLevel = entity_GetSavedExpLevel(pEnt);
		}
		
		if(iCombatLevel < 1)
		{	
			iCombatLevel = 1;
		}
	}
	
	return iCombatLevel;
}

void DEFAULT_LATELINK_gameSpecificFixup(Entity *pEntity)
{
	//This function should never do anything. Create game specific fixup code in an OVERRIDE version of this function
	//See STFixup.c for an example (StarTrek specific file)
	return;
}

int DEFAULT_LATELINK_gameSpecificFixup_Version(void)
{
	return 0;
}

int DEFAULT_LATELINK_gameSpecificPreLoginFixup_Version(void)
{
	return 0;
}


// turned this into a function because StructParser can't descend into macros
// this way you get type checking if a NOCONST(Entity) is passed to it.
int entGetAccessLevel(Entity const *e)  
{
	if(NONNULL(e) && NONNULL(e->pPlayer) )
		return e->pPlayer->accessLevel; 
	return 0;
}

UGCAccount *entGetUGCAccount(Entity const *e)
{
	if(NONNULL(e) && NONNULL(e->pPlayer))
	{
		UGCAccount *pUGCAccount = GET_REF(e->pPlayer->hUGCAccount);
		return pUGCAccount ? pUGCAccount : e->pPlayer->pUGCAccount;
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////
// Map History access functions
//////////////////////////////////////////////////////////////////////////

//
// Get the last map, regardless of type
//
SavedMapDescription *
entity_GetLastMap(Entity *pEntity)
{
	if ( pEntity && pEntity->pSaved )
	{
		if ( pEntity->pSaved->bLastMapStatic )
		{
			return pEntity->pSaved->lastStaticMap;
		}
		else
		{
			return pEntity->pSaved->lastNonStaticMap;
		}
	}
	return NULL;
}

//
// Get the last static map
//
SavedMapDescription *
entity_GetLastStaticMap(Entity *pEntity)
{
	if ( pEntity && pEntity->pSaved )
	{
		return pEntity->pSaved->lastStaticMap;
	}
	return NULL;
}

//
// Get the last non-static map
//
SavedMapDescription *
entity_GetLastNonStaticMap(Entity *pEntity)
{
	if ( pEntity && pEntity->pSaved )
	{
		return pEntity->pSaved->lastNonStaticMap;
	}
	return NULL;
}

//
// Set the current map
//
void entity_SetCurrentMap(NOCONST(Entity) *pEnt, SavedMapDescription *pNewMapDescription)
{
	///never remember that we edited a UGC map
	if (pNewMapDescription->bUGCEdit || isProductionEditMode())
	{
		return;
	}

	// zero out Pitch and Roll on the spawn location to reduce database writes
	SavedMapUpdateRotationForPersistence(CONTAINER_NOCONST(SavedMapDescription, pNewMapDescription));

	if ( pNewMapDescription->eMapType == ZMTYPE_STATIC )
	{
		StructDestroyNoConstSafe(parse_SavedMapDescription, &pEnt->pSaved->lastStaticMap);
		pEnt->pSaved->lastStaticMap = CONTAINER_NOCONST(SavedMapDescription, pNewMapDescription);
		pEnt->pSaved->bLastMapStatic = true;
	}
	else
	{
		StructDestroyNoConstSafe(parse_SavedMapDescription, &pEnt->pSaved->lastNonStaticMap);
		pEnt->pSaved->lastNonStaticMap = CONTAINER_NOCONST(SavedMapDescription, pNewMapDescription);
		pEnt->pSaved->bLastMapStatic = false;
	}
}

bool entity_PowerCartIsRespecRequired(SA_PARAM_NN_VALID Entity* pEnt)
{
	bool bIsRequired = false;
	S32 i, j, iSize = eaSize(&pEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers);
	StashTable stCartPowers = stashTableCreateAddress(iSize);

	for (i = 0; i < iSize; i++)
	{
		SavedCartPower* pCartPower = pEnt->pPlayer->pUI->pLooseUI->ppSavedCartPowers[i];
		PTNodeDef* pNodeDef = GET_REF(pCartPower->hNodeDef);
		if (pNodeDef)
		{
			stashAddressAddInt(stCartPowers, pNodeDef, pCartPower->iRank, true);
		}
	}
	for (i = eaSize(&pEnt->pChar->ppPowerTrees)-1; i >= 0; i--)
	{
		PowerTree* pTree = pEnt->pChar->ppPowerTrees[i];
		PowerTreeDef* pTreeDef = GET_REF(pTree->hDef);
		if (!pTreeDef || pTreeDef->eRespec == kPowerTreeRespec_None)
		{
			continue;
		}
		for (j = eaSize(&pTree->ppNodes)-1; j >= 0; j--)
		{
			PTNode* pNode = pTree->ppNodes[j];
			PTNodeDef* pNodeDef = GET_REF(pNode->hDef);
			S32 iCartRank;
			if (!pNodeDef || pNodeDef->bSlave || (pNodeDef->eFlag & kNodeFlag_AutoBuy))
			{
				continue;
			}
			if (!stashAddressFindInt(stCartPowers, pNodeDef, &iCartRank))
			{
				iCartRank = -1;
			}
			if (iCartRank < pNode->iRank)
			{
				bIsRequired = true;
				break;
			}
		}
		if (bIsRequired)
		{
			break;
		}
	}
	stashTableDestroy(stCartPowers);
	return bIsRequired;
}

// Check to see if an entity has already completed a UGC project
//
bool entity_HasRecentlyCompletedUGCProject(Entity* pEnt, ContainerID iProjectID, U32 uWithinSeconds)
{
	if(pEnt && pEnt->pSaved && pEnt->pSaved->activityLogEntries && iProjectID)
	{
		int i;
		U32 uCurrentTime = timeSecondsSince2000();
		for (i=0; i < eaSize(&pEnt->pSaved->activityLogEntries); i++) 
		{
			ActivityLogEntry* pEntry = pEnt->pSaved->activityLogEntries[i];
			if(pEntry->type == ActivityLogEntryType_SimpleMissionComplete && pEntry->argString)
			{
				if(UGCProject_GetProjectContainerIDFromUGCResource(pEntry->argString) == iProjectID)
				{
					if (!uWithinSeconds || pEntry->time + uWithinSeconds >= uCurrentTime)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

bool entity_HasCompletedUGCProjectSince(Entity* pEnt, ContainerID iProjectID, U32 uStartTime)
{
	if(pEnt && pEnt->pSaved && pEnt->pSaved->activityLogEntries && iProjectID)
	{
		int i;
		for (i=0; i < eaSize(&pEnt->pSaved->activityLogEntries); i++) 
		{
			ActivityLogEntry* pEntry = pEnt->pSaved->activityLogEntries[i];
			if(pEntry->type == ActivityLogEntryType_SimpleMissionComplete && pEntry->argString)
			{
				if(UGCProject_GetProjectContainerIDFromUGCResource(pEntry->argString) == iProjectID)
				{
					if (pEntry->time >= uStartTime)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

S32 entity_FindMacroByID(Entity* pEnt, U32 uMacroID)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		return eaIndexedFindUsingInt(&pEnt->pPlayer->pUI->eaMacros, uMacroID);
	}
	return -1;
}

S32 entity_FindMacro(Entity* pEnt, const char* pchMacroString, const char* pchDesc, const char* pchIcon)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		const char* pchIconPooled = allocFindString(pchIcon);
		S32 i;
		for (i = eaSize(&pEnt->pPlayer->pUI->eaMacros)-1; i >= 0; i--)
		{
			PlayerMacro* pMacro = pEnt->pPlayer->pUI->eaMacros[i];
			if (pMacro->pchIcon == pchIconPooled &&
				stricmp(pMacro->pchMacro, pchMacroString)==0 &&
				stricmp(pMacro->pchDescription, pchDesc)==0)
			{
				return i;
			}
		}
	}
	return -1;
}

bool entity_IsMacroValid(const char* pchMacroString, const char* pchDesc, const char* pchIcon)
{
	int iMacroLen = pchMacroString ? (int)strlen(pchMacroString) : 0;
	int iDescLen = pchDesc ? (int)strlen(pchDesc) : 0;
	int iIconLen = pchIcon ? (int)strlen(pchIcon) : 0;

	//TODO: Validate the icon
	if (iMacroLen && iMacroLen <= PLAYER_MACRO_LENGTH_MAX &&
		iDescLen <= PLAYER_MACRO_DESC_LENGTH_MAX &&
		iIconLen <= MAX_PATH)
	{
		return true;
	}
	return false;
}

bool entity_IsAutoLootEnabled(Entity* pEnt)
{
	if (gConf.bEnableAutoLoot)
	{
		return true;
	}
	else if (pEnt && pEnt->pPlayer)
	{
		return pEnt->pPlayer->bEnableAutoLoot;
	}
	return false;
}

bool entity_ShouldAutoLootTarget(Entity* pEnt, Entity* pTarget)
{
	if (gConf.bEnableAutoLoot)
	{
		return true;
	}
	else if (pEnt && pEnt->pPlayer && pEnt->pPlayer->bEnableAutoLoot)
	{
		return true;
	}
	else if (pTarget && pTarget->pCritter)
	{
		return pTarget->pCritter->bAutoLootMe;
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(SendFXMessage) ACMD_PRIVATE ACMD_CLIENTCMD;
void entity_SendFXMessage(U32 er, const char* pchMessage)
{
	Entity* e = entFromEntityRef(PARTITION_CLIENT, er);
	DynFxManager* pMan = e ? dynFxManFromGuid(e->dyn.guidFxMan) : NULL;
	if (pMan)
		dynFxManBroadcastMessage(pMan, allocAddString(pchMessage));
}

// This is a wrapper for telling if a character is on a UGC shard
// It this function will need to be changed if virtual shards are used for more than just UGC in the future.
bool entity_IsUGCCharacter(Entity *pEntity)
{
	if(entGetVirtualShardID(pEntity) != 0)
	{
		return true;
	}

	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(e, ".Pchar");
enumTransactionOutcome entity_trh_AllegianceSpeciesChange(ATR_ARGS, ATH_ARG NOCONST(Entity)* e, AllegianceDef *pDef)
{
	if (ISNULL(e) || ISNULL(e->pChar))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILURE: Null Entity or Character.");
	}

	if (pDef && e->pChar && eaSize(&pDef->eaSpeciesChange) > 0)
	{
		SpeciesDef *pSpeciesDef = GET_REF(e->pChar->hSpecies);
		int i;
		for (i = 0; i < eaSize(&pDef->eaSpeciesChange); i++)
		{
			if (pSpeciesDef == GET_REF(pDef->eaSpeciesChange[i]->hFromSpecies))
			{
				COPY_HANDLE(e->pChar->hSpecies, pDef->eaSpeciesChange[i]->hToSpecies);

				TRANSACTION_APPEND_LOG_SUCCESS("SUCCESS: Entity species changed from %s to %s",
					REF_STRING_FROM_HANDLE(pDef->eaSpeciesChange[i]->hFromSpecies),
					REF_STRING_FROM_HANDLE(pDef->eaSpeciesChange[i]->hToSpecies));

				return(TRANSACTION_OUTCOME_SUCCESS);
			}
		}
	}

	return(TRANSACTION_OUTCOME_SUCCESS);
}

#include "beaconDebug.h"
#include "aiDebugShared.h"
#include "aiDebugShared_h_ast.c"
#include "AutoGen/Entity_h_ast.c"
#include "AutoGen/EntitySavedData_h_ast.c"
#include "AutoGen/Player_h_ast.c"
#include "EntityAttach_h_ast.c"

