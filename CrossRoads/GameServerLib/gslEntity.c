/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiExtern.h"
#include "aiLib.h"
#include "aiMovement.h"
#include "aiStruct.h"
#include "AttribMod.h"
#include "AutoTransDefs.h"
#include "beacon.h"
#include "beaconPath.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "Character_combat.h"
#include "Character_Target.h"
#include "cmdServerCombat.h"
#include "CombatConfig.h"
#include "CommandQueue.h"
#include "contact_common.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "crypt.h"
#include "cutscene.h"
#include "cutscene_common.h"
#include "DamageTracker.h"
#include "dynDraw.h"
#include "dynNode.h"
#include "dynSkeleton.h"
#include "earray.h"
#include "Entity.h"
#include "EntityAttach.h"
#include "EntityBuild.h"
#include "EntityGrid.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntityMovementDefault.h"
#include "EntityMovementDisableMovement.h"
#include "EntityMovementEmote.h"
#include "EntityMovementFlight.h"
#include "EntityMovementManager.h"
#include "EntityMovementTactical.h"
#include "EntityNet.h"
#include "EntitySavedData.h"
#include "entitysysteminternal.h"
#include "Expression.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslActivityLog.h"
#include "gslChat.h"
#include "gslCommandParse.h"
#include "gslContact.h"
#include "gslControlScheme.h"
#include "gslCostume.h"
#include "gslcritter.h"
#include "gslCurrencyExchange.h"
#include "gslDiary.h"
#include "gslDoorTransition.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEntityNet.h"
#include "gslEntityPresence.h"
#include "gslEventSend.h"
#include "gslExtern.h"
#include "gslGroupProject.h"
#include "gslHUDOptions.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslInteractionManager.h"
#include "gslItemAssignments.h"
#include "LoggedTransactions.h"
#include "gslMailNPC.h"
#include "gslmaptransfer.h"
#include "gslMission.h"
#include "gslNumericConversion.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslPetCommand.h"
#include "gslPlayerFSM.h"
#include "gslPlayerMatchStats.h"
#include "gslPowerTransactions.h"
#include "gslProjectileEntity.h"
#include "gslQueue.h"
#include "gslSavedPet.h"
#include "gslScoreboard.h"
#include "gslSendToClient.h"
#include "gslSpawnPoint.h"
#include "gslTeamUp.h"
#include "gslTransactions.h"
#include "gslTray.h"
#include "gslUserExperience.h"
#include "Guild.h"
#include "ImbeddedList.h"
#include "interaction_common.h"
#include "inventoryCommon.h"
#include "inventoryTransactions.h"
#include "itemCommon.h"
#include "itemServer.h"
#include "Leaderboard.h"
#include "logging.h"
#include "Materials.h"
#include "microtransactions_common.h"
#include "mission_common.h"
#include "nemesis.h"
#include "nemesis_common.h"
#include "NotifyCommon.h"
#include "objContainer.h"
#include "Player.h"
#include "powerActivation.h"
#include "PowerModes.h"
#include "Powers.h"
#include "PowerSlots.h"
#include "PowerTree.h"
#include "PowerTreeTransactions.h"
#include "ProjectileEntity.h"
#include "Quat.h"
#include "Rand.h"
#include "RegionRules.h"
#include "SavedPetCommon.h"
#include "serverlib.h"
#include "Skills_DD.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "structnet.h"
#include "Team.h"
#include "TeamUpCommon.h"
#include "testclient_comm.h"
#include "TextFilter.h"
#include "timing.h"
#include "tradeCommon.h"
#include "transactionsystem.h"
#include "TransformationCommon.h"
#include "Tray.h"
#include "wlSkelInfo.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "WebRequestServer/wrContainerSubs.h"
#include "progression_common.h"
#include "gslActivity.h"
#include "itemupgrade.h"
#include "gslSuperCritterPet.h"
#include "Gateway/gslGatewayServer.h"

#include "net/net.h"
#include "Character_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "EntityAttach_h_ast.h"
#include "GameAccountData_h_ast.h"
#include "../StaticWorld/group.h"
#include "Player_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntityBuild_h_ast.h"
#include "AutoGen/gslEntity_h_ast.h"
#include "AutoGen/PowerSlots_h_ast.h"
#include "AutoGen/MapDescription_h_ast.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "../common/autogen/GameClientLib_autogen_clientcmdwrappers.h"
#include "../common/autoGen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/AppserverLib_autogen_remotefuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#define PRINT_PLAYER_ENTITY_INIT_AND_DEINIT 0

int gslEntGetPartitionID(Entity *ent)
{
	if (ent)
	{
		U32 idx = entGetPartitionIdx(ent);
		if (idx > 0)
			return partition_IDFromIdx(idx);
	}
	return 0;
}

AUTO_COMMAND ACMD_NAME(setpos) ACMD_LIST(gEntConCmdList) ACMD_GLOBAL ACMD_SERVERCMD ACMD_ACCESSLEVEL(4);
void gslEntSetPos(Entity* e, const Vec3 vPos)
{
	Entity* eMount;
	if (eMount = entGetMount(e))
	{
		entSetPos(eMount, vPos, 1, "ec");
	}
	entSetPos(e, vPos, 1, "ec");
}

AUTO_COMMAND ACMD_NAME(setpyr) ACMD_LIST(gEntConCmdList) ACMD_GLOBAL ACMD_SERVERCMD ACMD_ACCESSLEVEL(4);
void gslEntSetPYR(Entity* e, const Vec3 pyr)
{
	Quat rot;
	Entity* eMount;
	
	PYRToQuat(pyr, rot);

	if (eMount = entGetMount(e))
	{
		entSetRot(eMount, rot, 1, "ec");
	}
	entSetRot(e, rot, 1, "ec");
}

//log's the player's state. This happens in two ways: (1) when the player enters a level, and (2)
//during periodic player updating. 
void gslEntityPlayerLogStatePeriodic(Entity* e, bool bDoFullLog, float fSecondsPassed)
{
	const char *pClassName = "unknown";
	char *pPowersString = NULL;
	int i;
	Vec3 pos;
	Team *pTeam;
	Message *message;
	
	PERFINFO_AUTO_START_FUNC();

	pTeam = team_GetTeam(e);

	message = entGetClassNameMsg(e);

	if(message)
		pClassName = message->pcDefaultString;	

	//writing list of all player's powers like this: +power1+power2+power3+, etc. That makes it very easy
	//to do a simple strstr to see if a power is in the list without having to worry about powers whose names
	//are substrings of other powers' names

	if (bDoFullLog)
	{
		estrStackCreate(&pPowersString);

		estrPrintf(&pPowersString, "Powers \"+");

		for (i=0; i < eaSize(&e->pChar->ppPowers); i++)
		{
			estrConcatf(&pPowersString, "%s+", REF_STRING_FROM_HANDLE(e->pChar->ppPowers[i]->hDef));
		}

		estrConcatf(&pPowersString, "\"");
	}


	devassert(e->pPlayer);
	entGetPos(e, pos);

	entLog(LOG_PLAYER, e, "periodicPlayerData", "PlayTime %f AccessLevel %d Class \"%s\" Exp %u %s TeamSize %d Location <%d;%d;%d>", e->pPlayer->fTotalPlayTime + fSecondsPassed,
		   entGetAccessLevel(e), pClassName, item_GetLevelingNumeric(e), bDoFullLog ? pPowersString : "", pTeam ? team_NumPresentMembers(pTeam) : 1, (int)pos[0], (int)pos[1], (int)pos[2]);

	// Horrible hardcoded stat logging
	if(!bDoFullLog && e->pChar && e->pChar->pattrBasic)
	{
		estrClear(&pPowersString);
		estrAppend2(&pPowersString,"StatHealth "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatHealth);
		estrAppend2(&pPowersString,"StatPower "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatPower);
		estrAppend2(&pPowersString,"StatStrength "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatStrength);
		estrAppend2(&pPowersString,"StatAgility "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatAgility);
		estrAppend2(&pPowersString,"StatIntelligence "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatIntelligence);
		estrAppend2(&pPowersString,"StatEgo "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatEgo);
		estrAppend2(&pPowersString,"StatPresence "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatPresence);
		estrAppend2(&pPowersString,"StatRecovery "); estrConcatf(&pPowersString,"%d, ",(int)e->pChar->pattrBasic->fStatRecovery);

		entLog(LOG_PLAYER, e, "periodicPlayerStatData", "%s", pPowersString);
	}
	

	estrDestroy(&pPowersString);
	
	PERFINFO_AUTO_STOP();
}

F32 fSendDistanceMultDebug = 1.0;
AUTO_CMD_FLOAT(fSendDistanceMultDebug, DetectionMult);

void gslEntityUpdateSendDistance(Entity* e)
{
	if (!e->bFakeEntity && !e->costumeRef.pcDestructibleObjectCostume) // World objects get set elsewhere
	{	
		if (e->pCritter && e->pCritter->fOverrideSendDistance > FLT_EPSILON)
		{
			e->fEntitySendDistance = e->pCritter->fOverrideSendDistance;
		}
		else
		{
			F32					fRegionSendDistanceMin = ENTITY_DEFAULT_SEND_DISTANCE;
			F32					fRegionSendDistanceMax = -1.0f;
			F32					fRegionSendDistanceBaseHeight = 6.0f;
			Vec3				vEntPos;
			WorldRegion*		pRegion;
			RegionRules*		pRegionRules;

			entGetPos(e,vEntPos);
			pRegion = worldGetWorldRegionByPos(vEntPos);
			pRegionRules = pRegion ? getRegionRulesFromRegion(pRegion) : NULL;

			if (pRegionRules && pRegionRules->fSendDistanceMin)
			{
				fRegionSendDistanceMin = pRegionRules->fSendDistanceMin;
			}
			if (pRegionRules && pRegionRules->fSendDistanceMax)
			{
				fRegionSendDistanceMax = pRegionRules->fSendDistanceMax;
			}
			if (pRegionRules && pRegionRules->fSendDistanceBaseHeight)
			{
				fRegionSendDistanceBaseHeight = pRegionRules->fSendDistanceBaseHeight;
			}

			if (e->collRadiusCached > fRegionSendDistanceBaseHeight)
			{
				// If entity is bigger than the base height, then scale up the
				// send distance based on base height equaling min distance.
				e->fEntitySendDistance = (fRegionSendDistanceMin / fRegionSendDistanceBaseHeight) * e->collRadiusCached * fSendDistanceMultDebug;
			}
			else
			{
				e->fEntitySendDistance = fRegionSendDistanceMin * fSendDistanceMultDebug;
			}

			if(e->pCritter)
			{
				CritterDef *pCritterDef = GET_REF(e->pCritter->critterDef);
				F32 fSendRange = cutscene_GetActiveHighSendRange();
				if(pCritterDef && pCritterDef->fEntityMinSeeAtDistance > e->fEntitySendDistance)
				{
					e->fEntitySendDistance = pCritterDef->fEntityMinSeeAtDistance;
					e->fEntityMinSeeAtDistance = pCritterDef->fEntityMinSeeAtDistance;
				}

				// if the send range for a cut scene is greater use it
				if(fSendRange > e->fEntitySendDistance)
				{	
					e->fEntitySendDistance = fSendRange;
				}

			}

			if ( fRegionSendDistanceMax > 0.01f )
			{
				MIN1(e->fEntitySendDistance,fRegionSendDistanceMax);
			}
		}

		entGridUpdate(e, 0);
		entity_SetDirtyBit(e, parse_Entity, e, false);
	}
}

void gslEntMovementCreateSurfaceRequester(Entity* e){
	if(e && !e->mm.mrSurface){
		mrSurfaceCreate(e->mm.movement, &e->mm.mrSurface);
		entity_UpdateForCurrentControlScheme(e);
	}
}

void gslEntMovementDestroySurfaceRequester(Entity* e){
	if (e){
		mrDestroy(&e->mm.mrSurface);
	}
}

void gslEntMovementCreateFlightRequester(Entity* e){
	if(e && !e->mm.mrFlight){
		mrFlightCreate(e->mm.movement, &e->mm.mrFlight);
		entity_UpdateForCurrentControlScheme(e);
	}
}

void gslEntMovementCreateTacticalRequester(Entity* e){
	if(e && !e->mm.mrTactical){
		mrTacticalCreate(e->mm.movement, &e->mm.mrTactical);
		entity_UpdateForCurrentControlScheme(e);
	}
}

void gslEntMovementCreateEmoteRequester(Entity* e){
	if(e && !e->mm.mrEmote){
		mrEmoteCreate(e->mm.movement, &e->mm.mrEmote);
	}
}

void gslEntityMovementManagerMsgHandler(const MovementManagerMsg* msg){
	Entity* e = msg->userPointer;

	if(	!e &&
		MM_MSG_IS_FG(msg->msgType))
	{
		return;
	}
	
	switch(msg->msgType){
		xcase MM_MSG_FG_SET_VIEW:{
			e->frameWhenViewSet = frameFromMovementSystem;

			entSetPosRotFace(	e,
								msg->fg.setView.vec3Pos,
								msg->fg.setView.quatRot,
								msg->fg.setView.vec2FacePitchYaw,
								false,
								true,
								__FUNCTION__);
		}

		xcase MM_MSG_FG_FIND_UNOBSTRUCTED_POS:{
			Beacon* b = NULL;
			
			beaconSetPathFindEntity(entGetRef(e), 0, 0);
			b = beaconGetClosestCombatBeacon(	entGetPartitionIdx(e),
												msg->fg.findUnobstructedPos.posStart,
												NULL,
												1,
												NULL,
												GCCB_IGNORE_LOS,
												NULL);

			if(b){
				msg->out->fg.findUnobstructedPos.flags.found = 1;
				copyVec3(b->pos, msg->out->fg.findUnobstructedPos.pos);
			}
		}
		
		xcase MM_MSG_FG_MOVE_ME_SOMEWHERE_SAFE:{
			if(e->pPlayer){
				spawnpoint_MovePlayerToNearestSpawn(e, true, false);
			}
		}
		
		xcase MM_MSG_FG_COLL_RADIUS_CHANGED:{
			entMovementDefaultMsgHandler(msg);

			gslEntityUpdateSendDistance(e);
		}
		
		xcase MM_MSG_FG_QUERY_IS_POS_VALID:{
			if(beaconIsPositionValid(entGetPartitionIdx(e), msg->fg.queryIsPosValid.vec3Pos, NULL)){
				msg->out->fg.queryIsPosValid.flags.posIsValid = 1;
			}
		}
		
		xcase MM_MSG_FG_REQUESTER_DESTROYED:{
			const MovementRequester*const mr = msg->fg.requesterDestroyed.mr;
			
			ANALYSIS_ASSUME(mr);

			if(mr == SAFE_MEMBER(e->aibase, movement)){
				e->aibase->movement = NULL;
			}
			
			if(entCheckFlag(e, ENTITYFLAG_CIVILIAN)){
				aiCivilian_ReportRequesterDestroyed(e, mr);
			}

			entMovementDefaultMsgHandler(msg);
		}
		
		xdefault:{
			entMovementDefaultMsgHandler(msg);
		}
	}
}

static void gslEntityUpdateInvisibleState(Entity* e){
	if(	e->isInvisibleTransient ||
		e->isInvisiblePersistent)
	{
		entSetCodeFlagBits(e, ENTITYFLAG_DONOTSEND);
	}else{
		entClearCodeFlagBits(e, ENTITYFLAG_DONOTSEND);
	}
}

bool OVERRIDE_LATELINK_objTransactions_MaybeLocallyCopyBackupDataDuringReceive(GlobalType eType, void *pMainObject, void *pBackupObject)
{
	if (eType == GLOBALTYPE_ENTITYPLAYER ||
		eType == GLOBALTYPE_ENTITYSAVEDPET ||
		eType == GLOBALTYPE_ENTITYPUPPET)
	{
		Entity *pMainEntity = pMainObject;
		Entity *pBackupEntity = pBackupObject;

		if (pMainEntity->pSaved && !pMainEntity->pSaved->pEntityBackup)
		{
			pMainEntity->pSaved->pEntityBackup = pBackupEntity;
			return true;
		}
	}

	return false;
}

// Initializes an entity on the server
void gslInitializeEntity(Entity* e, bool bIsReloading)
{
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("FindPartition", 1);

	if (gGSLState.gbGatewayServer || gGSLState.gbWebRequestServer)
	{
		e->iPartitionIdx_UseAccessor = 1;
	} else if (entGetPartitionIdx_NoAssert(e)) {
		// Index already set means we don't need to modify it
	} else if (entGetType(e) == GLOBALTYPE_ENTITYPLAYER) {
		// Players should be pre-announced via map manager
		e->iPartitionIdx_UseAccessor = partition_PopUpcomingTransferPartitionIdx(e->myContainerID);
		partition_DebugLogInternal(PARTITION_PLAYER_ENTERED, e->iPartitionIdx_UseAccessor, "%u (%s) entered",
			e->myContainerID, ENTDEBUGNAME(e));
	} else if (entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET) {
		// Pets should use the owner's index
		Entity* pOwner = NULL;
		if (e->pSaved) {
			pOwner = entFromContainerIDAnyPartition(e->pSaved->conOwner.containerType, e->pSaved->conOwner.containerID);
		}
		if (pOwner) {
			e->iPartitionIdx_UseAccessor = entGetPartitionIdx_NoAssert(pOwner);
		} else {
			e->iPartitionIdx_UseAccessor = PARTITION_ORPHAN_PET;
		}
	} // else GLOBALTYPE_ENTITYCRITTER should be set in entity create before it gets here

	// Safety check
	iPartitionIdx = entGetPartitionIdx_NoAssert(e);
	assertmsgf((iPartitionIdx == PARTITION_IN_TRANSACTION) || (iPartitionIdx == PARTITION_ORPHAN_PET) || partition_ExistsByIdx(iPartitionIdx), 
				"Partition %d does not exist on entity create", iPartitionIdx);

	PERFINFO_AUTO_STOP(); // FindPartition

	// Initialization shared for client/server
	PERFINFO_AUTO_START("entInitializeCommon", 1);
	entInitializeCommon(e, (iPartitionIdx != PARTITION_IN_TRANSACTION) && (iPartitionIdx != PARTITION_ORPHAN_PET));
	PERFINFO_AUTO_STOP();

	// NOTE: DO NOT add code here that modifies any persisted data. The backup must be created
	// before doing so, or any changes made here will not be correctly sent to the DB

	if (entGetType(e) == GLOBALTYPE_ENTITYPLAYER ||
		entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET ||
		entGetType(e) == GLOBALTYPE_ENTITYPUPPET)
	{
		PERFINFO_AUTO_START("CreateEntityBackups", 1);

		//this may already exist, because it may have been stuck here by objTransactions_MaybeLocallyCopyBackupDataDuringReceive
		if (!e->pSaved->pEntityBackup)
		{
			// StructCreate() instead of StructAlloc so that we get correct Entity allocation tracking
			e->pSaved->pEntityBackup = StructCreateWithComment(parse_Entity,"EntityBackup created in gslInitializeEntity");
		}
	
		StructCopyFields(parse_Entity,e,e->pSaved->pEntityBackup,TOK_PERSIST,0);
		e->pSaved->pAutoTransBackup = StructCloneWithComment(parse_Entity, e->pSaved->pEntityBackup, "AutoTransBackup created in gslInitializeEntity");
		
		assertmsg((e->pChar || e->pNemesis) && e->pSaved, "New persisted entities must be created by the ObjectDB!");	

		if(e->pChar && !e->pChar->pattrBasic)
		{
			e->pChar->pattrBasic = StructAlloc(parse_CharacterAttribs);
			//TODO(JW): HACK: Brilliant!  Use a negative hp to mark that this Character is new.
			// This mark is caught in character_LoadNoTransact() and used to do a reset, rather
			// than a load.
			e->pChar->pattrBasic->fHitPoints = -1;
		}

		costumeEntity_ResetStoredCostume(e);
		costumeEntity_SetCostumeRefDirty(e);

		PERFINFO_AUTO_STOP(); // CreateEntityBackups
	}

	// Set up movement 
	PERFINFO_AUTO_START("MovementSetup", 1);
	
	// don't create the default surface requester for GLOBALTYPE_ENTITYPROJECTILE
	if (entGetType(e) != GLOBALTYPE_ENTITYPROJECTILE)
	{
		gslEntMovementCreateSurfaceRequester(e);
	}
	mmSetMsgHandler(e->mm.movement, gslEntityMovementManagerMsgHandler);
	mmSetUserThreadDataSize(e->mm.movement, sizeof(EntityMovementThreadData));
	// 
	
	if(entGetAccessLevel(e) < ACCESS_GM)
	{
		e->isInvisiblePersistent = 0;
	}

	gslEntityUpdateInvisibleState(e);

	PERFINFO_AUTO_STOP(); // MovementSetup

	// Generate costume for all entity types
	PERFINFO_AUTO_START("CostumeSetup", 1);
	costumeEntity_RegenerateCostume(e);
	PERFINFO_AUTO_STOP(); // CostumeSetup

	switch (entGetType(e))
	{
		case GLOBALTYPE_ENTITYPLAYER:
		{
			MissionInfo* missionInfo;

			PERFINFO_AUTO_START("PlayerAIInit", 1);
			aiInit(e, NULL, NULL);
			aiInitTeam(e, NULL);
			PERFINFO_AUTO_STOP(); // PlayerAIInit

		#if PRINT_PLAYER_ENTITY_INIT_AND_DEINIT
			printfColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
						"Initializing player entity: c%d:i%d:%s:0x%8.8p\n",
						e->myContainerID,
						INDEX_FROM_REFERENCE(e->myRef),
						e->debugName,
						e);
		#endif

			PERFINFO_AUTO_START("PlayerMissionInit", 1);
			missionInfo = mission_GetInfoFromPlayer(e);
			missionInfo->bMissionsNeedVerification = 1;
			mission_PostEntityCreateMissionInit(e, true);
			PERFINFO_AUTO_STOP(); // PlayerMissionInit

			PERFINFO_AUTO_START("PlayerControlScheme", 1);
			entity_UpdateForCurrentControlScheme(e);
			PERFINFO_AUTO_STOP(); // PlayerControlScheme
			
			// initialize anything based on the CharacterClass
			if (e->pChar)
			{
				CharacterClass *pClass = character_GetClassCurrent(e->pChar);
				
				if (pClass && g_CombatConfig.bCharacterClassSpecifiesStrafing)
				{
					gslEntitySetIsStrafing(e, pClass->bStrafing);
				}
			}

			if (!bIsReloading)
			{
				entSetCodeFlagBits(e,ENTITYFLAG_PLAYER_DISCONNECTED);
				entSetCodeFlagBits(e,ENTITYFLAG_PLAYER_LOGGING_IN);
			}

			PERFINFO_AUTO_START("PlayerMovement", 1);
			entSetCodeFlagBits(e,ENTITYFLAG_IGNORE);
			mmDisabledHandleCreate(&e->mm.mdhIgnored, e->mm.movement, __FILE__, __LINE__);
			gslEntitySetInvisibleTransient(e, 1);

			if(e->pPlayer->iStasis >= timeSecondsSince2000()) {
				// Player is still in stasis. Disable movement.
				mrDisableCreate(e->mm.movement, &e->mm.mrDisabledCSR);
				gslEntitySetInvisibleTransient(e, 1);
			}

			gslEntity_UpdateMovementMangerFaction(iPartitionIdx, e);
			PERFINFO_AUTO_STOP(); // PlayerMovement
		}


		xcase GLOBALTYPE_ENTITYSAVEDPET:
		{
			PERFINFO_AUTO_START("SavedPetMovement", 1);

			entSetCodeFlagBits(e,ENTITYFLAG_PET_LOGGING_IN);
			entSetCodeFlagBits(e,ENTITYFLAG_IGNORE);
			entClearCodeFlagBits(e, ENTITYFLAG_PET_LOGGING_OUT);
			mmDisabledHandleCreate(&e->mm.mdhIgnored, e->mm.movement, __FILE__, __LINE__);
			gslEntitySetInvisibleTransient(e, 1);

			PERFINFO_AUTO_STOP(); // SavedPetMovement
		}

		xcase GLOBALTYPE_ENTITYCRITTER:
		{
			if (!e->pCritter)
			{
				NOCONST(Entity) *pNoConstEntity = CONTAINER_NOCONST(Entity, e);
				pNoConstEntity->pCritter = StructCreateNoConst(parse_Critter);
				entity_SetDirtyBit(e, parse_Entity, e, false);
			}
		}

		xcase GLOBALTYPE_ENTITYPROJECTILE:
		{
			if (!e->pProjectile)
			{
				NOCONST(Entity) *pNoConstEntity = CONTAINER_NOCONST(Entity, e);
				pNoConstEntity->pProjectile = calloc(1, sizeof(ProjectileEntity));
				entity_SetDirtyBit(e, parse_Entity, e, false);
			}
		}
		xdefault:
		{
			Errorf("Initializing Entity with unknown GlobalType.");
		}
	}
	
	// Project specific initialization
	gslExternInitializeEntity(e);

	if (entGetType(e) == GLOBALTYPE_ENTITYPLAYER)
	{
		// Turn the loose UI data back into a structure.
		gslExternPlayerLoadLooseUI(e);
	}

	if(SAFE_MEMBER2(e, pChar, pattrBasic))
	{
		// also called in OVERRIDE_LATELINK_gslCritterInitialize in c:\src\CrossRoads\GameServerLib\gslCritter.c for critters
		eventsend_RecordNewHealthState(e, e->pChar->pattrBasic->fHitPointsMax);
	}

	if (entGetType(e) == GLOBALTYPE_ENTITYPLAYER)
	{
		gslEntityPlayerLogStatePeriodic(e, true, 0.0f);
		RemoteCommand_GameServerReportsPlayerBeganLogin(GLOBALTYPE_MAPMANAGER, 0, gServerLibState.containerID, partition_IDFromIdx(iPartitionIdx));
	}

	e->astrRegion = NULL;

	entGridUpdate(e, true);

	PERFINFO_AUTO_STOP();
}

// Cleans up an entity on the server
void gslCleanupEntityEx(int iPartitionIdx, Entity *e, bool bIsReloading, bool bDoCleanup)
{
	PERFINFO_AUTO_START_FUNC();

	//iPartitionIdx -1 is used for gateway offline entity cleanup
	if(iPartitionIdx != -1 && entGetType(e) == GLOBALTYPE_ENTITYPLAYER)
	{
		partition_DebugLogInternal(PARTITION_PLAYER_LEFT, entGetPartitionIdx(e), "CleanupEntity for %u(%s). Reloading: %d", 
			e->myContainerID, ENTDEBUGNAME(e), bIsReloading);

		#if PRINT_PLAYER_ENTITY_INIT_AND_DEINIT
			printfColor(COLOR_RED|COLOR_GREEN,
						"Cleaning player entity: c%d:i%d:%s:0x%8.8p\n",
						e->myContainerID,
						INDEX_FROM_REFERENCE(e->myRef),
						e->debugName,
						e);
		#endif
	}
	
	// Do cleanup that needs a full entity before freeing anything
	entPreCleanupCommon(e, bIsReloading);

	gslExternCleanupEntity(iPartitionIdx, e);

	if (SAFE_MEMBER(e->pChar, bIsPrimaryPet))
	{
		Entity* eCreator = entFromEntityRef(iPartitionIdx, e->erOwner);
		if (eCreator)
		{
			gslEntSetPrimaryPet(eCreator, NULL);
		}
	}
	if (SAFE_MEMBER(e->pChar, primaryPetRef))
	{
		gslEntSetPrimaryPet(e, NULL);
	}

	if (e->pSaved)
	{
		PERFINFO_AUTO_START("CleanupPetAndMount", 1);

		if (entGetMount(e))
		{
			gslEntCancelRide(e);
		}
		else if (entGetRider(e))
		{
			gslEntCancelRide(entGetRider(e));
		}
		
		if(entGetType(e) == GLOBALTYPE_ENTITYSAVEDPET)
		{
			gslSavedPetLogout(iPartitionIdx, e);
		}
		
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("FreeBackupEnts", 1);

		StructDestroySafe(parse_Entity, &e->pSaved->pEntityBackup);
		StructDestroySafe(parse_Entity, &e->pSaved->pAutoTransBackup);

		PERFINFO_AUTO_STOP();
	}

	if (e->pAttach)
	{
		Vec3 tempVec = {0};
		if (e->pAttach->erAttachedTo)
		{
			gslEntAttachToEnt(e, NULL, NULL, NULL, tempVec, unitquat);
		}
		while (eaiSize(&e->pAttach->eaiAttached))
		{
			Entity *pChild = entFromEntityRef(iPartitionIdx, e->pAttach->eaiAttached[0]);
			if (pChild)
			{
				gslEntAttachToEnt(pChild, NULL, NULL, NULL, tempVec, unitquat);
			}
			else
			{
				eaiRemove(&e->pAttach->eaiAttached, 0);
			}
		}
	}

	if (e->erCreator)
	{
		Entity* eCreator = entFromEntityRef(iPartitionIdx, e->erCreator);
		if (eCreator && eCreator->pChar)
		{
			character_CreatedEntityDestroyed(eCreator->pChar,e);
		}
	}

	if (e->aibase)
		aiDestroy(e);

	if (e->pNodeNearbyPlayer)
		gslEntityStopReceivingNearbyPlayerFlag(e);
	
	if (e->pProjectile)
		gslProjectile_Destroy(e);

	// kind of gross to do this here, but don't see a player cleanup function that runs on the server [RMARR - 7/14/11]
	if (e->pPlayer && e->pPlayer->pPresenceInfo)
	{
		gslEntityPresenceRelease(e->pPlayer);
	}

	entCleanupCommon(iPartitionIdx, e, bIsReloading, bDoCleanup);

	if (!bIsReloading && e->pPlayer){
		e->pPlayer->clientLink = NULL;
		pktFree(&e->pPlayer->msgPak);
	}

	PERFINFO_AUTO_STOP();
}


void *entServerCreateCB(ContainerSchema *sc)
{
	Entity* e;

	e = entCreateNew(sc->containerType, "Created in entity system by entServerCreateCB");
	return e;
}

void entServerInitCB(ContainerSchema *sc, void *obj)
{
	coarseTimerAddInstance(NULL, "gslInitializeEntity");
	gslInitializeEntity(obj, false);
	coarseTimerStopInstance(NULL, "gslInitializeEntity");
}

void entServerDeInitCB(ContainerSchema *sc, void *obj)
{
	gslCleanupEntity(obj);
}

void entServerDestroyCB(ContainerSchema *sc, void *obj, const char* file, int line)
{
	entDestroyEx(obj, file, line);
}

void entServerRegisterCB(ContainerSchema *sc, void *obj)
{
	entRegisterExisting(obj);

	{
		Entity * pEnt = (Entity *)obj;
		if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER)
		{
			gslRequestEntityPresenceUpdate();
		}
	}
}

// Creates and initializes a NON-PERSISTED entity.
Entity *gslCreateEntity(GlobalType entityType, int iPartitionIdx)
{
	char diffBuf[256];
	char *pcDiffStr = NULL;
	Container *con;

	PERFINFO_AUTO_START_FUNC();

	assertmsg(GlobalTypeParent(entityType) == GLOBALTYPE_ENTITY,"Invalid entity type!");
	assertmsg(GlobalTypeSchemaType(entityType) != SCHEMATYPE_PERSISTED && 
		GlobalTypeSchemaType(entityType) != SCHEMATYPE_TRANSACTED,"You must create transacted entities through transaction system!");

	if (iPartitionIdx) {
		sprintf(diffBuf, "set .Ipartitionidx_Useaccessor = %d\n", iPartitionIdx);
		pcDiffStr = diffBuf;
	}
	con = objAddToRepositoryFromString(entityType,0,pcDiffStr);
	objChangeContainerState(con, CONTAINERSTATE_OWNED, objServerType(), objServerID());

	PERFINFO_AUTO_STOP();

	if (!con)
	{
		return NULL;
	}
	return con->containerData;
}

void gslQueueEntityDestroy(Entity * e)
{
	if (e)
	{
		if(entCheckFlag(e,ENTITYFLAG_IS_PLAYER))
		{
			ErrorfForceCallstack("Attempting to set DESTROY flag on Player Entity");
			return;
		}
		
		PERFINFO_AUTO_START_FUNC();

		entSetCodeFlagBits(e, ENTITYFLAG_DESTROY);

		// Remove the entity from its encounter.
		// This is necessary in case the encounter is being destroyed (during encounter reloading or
		// if an encounter layer is destroyed).
		// If we ever need to know the parent encounter of a destroyed entity, we'll probably need to
		// queue encounter destruction as well.
		encounter_RemoveActor(e); // New encounter system
		if (gConf.bAllowOldEncounterData)
			oldencounter_RemoveActor(e); // Old encounter system
			
		PERFINFO_AUTO_STOP();
	}
}


// Destroys a NON-PERSISTED entity
int gslDestroyEntity(Entity * e)
{
	GlobalType entityType;
	ContainerID containerID;
	int retVal;
	int iPartitionIdx;

	if (!e)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	// Whenever an entity is destroyed, tell the AI that all prox ent lists may be bad.
	// This is a bit heavy handed, but there's not a good solution otherwise.
	// It will last till the end of the frame after which it is reset. [Adam Mitchell - 11/30/2011]

	// In order for this to be effective, ANY code that runs through the proxents array MUST call aiUpdateProxEnts first.
	// In fact, it has to do that anyway in case it didn't get updated this frame.  [RMARR - 4/9/13]
	aiForceProxUpdates(1);					

	iPartitionIdx = entGetPartitionIdx(e);
	assertmsgf((iPartitionIdx == PARTITION_IN_TRANSACTION) || !partition_IsDestroyed(iPartitionIdx), 
			"Attempting to destroy entity in non-existent partition %d", iPartitionIdx);
	
	if(entCheckFlag(e, ENTITYFLAG_DONOTFADE))
		gslEntityAddDeleteFlags(e, ENTITY_DELETE_NOFADE);
	
	// add the destroy flag if not existing
	if (!entCheckFlag(e, ENTITYFLAG_DESTROY))
		entSetCodeFlagBits(e, ENTITYFLAG_DESTROY);

	entityType = entGetType(e);
	containerID = entGetContainerID(e);

	// Some critters' defs are refcounted; decrement the ref count on the critter def (if necessary)
//	if(e->pCritter)
//		critterdef_TryRemoveSubstituteFromDictionary(e->pCritter);

	im_EntityDestroyed(e);

	if(e->erOwner)
		gslCritterPetCleanup(e);

	assertmsg(GlobalTypeParent(entityType) == GLOBALTYPE_ENTITY,"Invalid entity type!");
	assertmsg(GlobalTypeSchemaType(entityType) != SCHEMATYPE_PERSISTED && 
		GlobalTypeSchemaType(entityType) != SCHEMATYPE_TRANSACTED,"You must create transacted entities through transaction system!");

	retVal = objRemoveContainerFromRepository(entityType,containerID);
	
	PERFINFO_AUTO_STOP();
	
	return retVal;
}

void gslEntityForceFullSend(Entity* e)
{
	e->mySendFlags |= ENT_SEND_FLAG_FULL_NEEDED;
	entSetActive(e);
}

bool gslLinkOwnsRef(ClientLink *clientLink, EntityRef entRef)
{
	int i;
	if (clientLink->disconnected)
		return false;	
	if (entRef <= 0)
		return false;
	for (i = 0; i < eaiSize(&clientLink->localEntities); i++)
	{
		if (clientLink->localEntities[i] == entRef)
			return true;
	}
	return false;
}

Entity *gslPrimaryEntity(ClientLink *clientLink)
{
	if (!clientLink)
	{
		return NULL;
	}
	if (clientLink->disconnected)
	{
		return NULL;
	}
	if (eaiSize(&clientLink->localEntities))
	{
		return entFromEntityRefAnyPartition(clientLink->localEntities[0]);
	}
	return NULL;
}

void gslAddEntityToLink(ClientLink *clientLink, Entity* e, bool isPrimary)
{
	if (clientLink->disconnected)
		return;

	mmAttachToClient(	e->mm.movement,
						clientLink->movementClient);
	
	if (isPrimary)
	{
		eaiInsert(&clientLink->localEntitiesMutable, e->myRef, 0);
	}
	else
	{
		eaiPush(&clientLink->localEntitiesMutable, e->myRef);
	}
	if (e->pPlayer)
	{
		e->pPlayer->clientLink = clientLink;
		if (clientLink->netLink)
		{
			linkSetDebugName(clientLink->netLink, STACK_SPRINTF("Client link for %s@%s", 
				e->debugName, e->pPlayer->privateAccountName));
		}
	}
	gslEntityForceFullSend(e);
}

void gslRemoveEntityFromLink(ClientLink *clientLink, Entity* e)
{
	int i;

	for (i = 0; i < eaiSize(&clientLink->localEntities); i++)
	{
		if (clientLink->localEntities[i] == e->myRef)
		{
			mmDetachFromClient(	e->mm.movement,
								clientLink->movementClient);
								
			eaiRemove(&clientLink->localEntitiesMutable, i);
			
			if (e->pPlayer)
			{
				assert(e->pPlayer->clientLink == clientLink);
				e->pPlayer->clientLink = NULL;
				pktFree(&e->pPlayer->msgPak);
			}
			
			break;
		}
	}
	gslEntityForceFullSend(e);
}

void gslAddDebugEntityToLinkByRef(ClientLink *clientLink, EntityRef ref)
{
	if(!clientLink)
		return;

	if(clientLink->disconnected)
		return;

	eaiPushUnique(&clientLink->debugEntities, ref);
}


void gslAddDebugEntityToLink(ClientLink *clientLink, Entity* e)
{
	if(!e)
		return;

	gslAddDebugEntityToLinkByRef(clientLink, entGetRef(e));
}

void gslRemoveDebugEntityFromLink(ClientLink *clientLink, Entity* e)
{
	if(!clientLink || !e)
		return;

	if(clientLink->disconnected)
		return;

	eaiRemove(&clientLink->debugEntities, entGetRef(e));
}

const char *gslGetAccountNameForLink(ClientLink *clientLink)
{
	Entity* e = gslPrimaryEntity(clientLink);
	if (!e)
		return "UNKNOWN";
	if (!e->pPlayer)
		return "UNKNOWN";
	return e->pPlayer->privateAccountName;
}


// Returns an Entity that is visible to all players, so it can e used to transport otherwise
//  unattached effects (like world FX).

Entity* gslGetTransportEnt(int iPartitionIdx)
{
	Entity* e;
	EntityRef er = partition_erTransportFromIdx(iPartitionIdx);
	if (!er || !(e = entFromEntityRef(iPartitionIdx, er)) || !entIsAlive(e))
	{
		Vec3 worldPos = {-5000, -5000, -5000};
		e = critter_Create("TransportEntity",NULL,GLOBALTYPE_ENTITYCRITTER,iPartitionIdx,NULL,1,1,0,0,0,0,0,0,0,NULL,NULL);
		if(!e || !e->pChar)
		{
			Errorf("Unable to create proper TransportEntity critter, defaulting to random critter");
			e = critter_FindAndCreate(NULL, 0, NULL, NULL, NULL, 0, 0, 0, 0, GLOBALTYPE_ENTITYCRITTER, iPartitionIdx, NULL, 0, 0, 0, 0,NULL, 0, 0, NULL, NULL, NULL, NULL);
			// Above can return NULL if partition is already destroyed
		}
		if (e) 
		{
			strcpy(CONTAINER_NOCONST(Entity, e)->debugName, "GSL_Transport_Ent");
			entSetPos(e, worldPos, true, __FUNCTION__);
			entSetRot(e, unitquat, true, __FUNCTION__);
			entSetCodeFlagBits(e, ENTITYFLAG_DONOTDRAW|ENTITYFLAG_IGNORE);
			mmDisabledHandleCreate(&e->mm.mdhIgnored, e->mm.movement, __FILE__, __LINE__);
			if(e->pChar)
			{
				e->pChar->bUnkillable = true;
				entity_SetDirtyBit(e, parse_Character, e->pChar, false);
			}
			mmSetNetReceiveNoCollFG(e->mm.movement, 1);
			gslEntMovementDestroySurfaceRequester(e);
			partition_erTransportSetFromIdx(iPartitionIdx,entGetRef(e));
		}
	}
	ANALYSIS_ASSUME(e != NULL);
	return e;
}

static void
ConvertOldMapHistory(NOCONST(Entity) *e)
{
	int oldHistorySize;
	int i;
	int lastStaticIndex = -1;
	int lastNonStaticIndex = -1;

	if ( (e == NULL ) || ( e->pSaved == NULL ) )
	{
		return;
	}

	oldHistorySize = eaSize(&e->pSaved->obsolete_mapHistory);
	if ( ( e->pSaved->lastStaticMap == NULL ) && ( e->pSaved->lastNonStaticMap == NULL ) && ( oldHistorySize != 0 ) )
	{
		ZoneMapInfo *zoneMapInfo;
		ZoneMapType mapType;
		// new history is not present, and old history is, so convert old history

		// find last static map in old history
		for ( i = oldHistorySize - 1; i >= 0; i-- )
		{
			zoneMapInfo = zmapInfoGetByPublicName(e->pSaved->obsolete_mapHistory[i]->mapDescription);

			if ( zoneMapInfo != NULL )
			{
				// don't trust the type in map history if we can avoid it
				mapType = zmapInfoGetMapType(zoneMapInfo);
			}
			else
			{
				mapType = e->pSaved->obsolete_mapHistory[i]->eMapType;
			}

			if ( mapType == ZMTYPE_STATIC )
			{
				lastStaticIndex = i;
				break;
			}
		}

		// find last non-static map in old history
		for ( i = oldHistorySize - 1; i >= 0; i-- )
		{
			zoneMapInfo = zmapInfoGetByPublicName(e->pSaved->obsolete_mapHistory[i]->mapDescription);

			if ( zoneMapInfo != NULL )
			{
				// don't trust the type in map history if we can avoid it
				mapType = zmapInfoGetMapType(zoneMapInfo);
			}
			else
			{
				mapType = e->pSaved->obsolete_mapHistory[i]->eMapType;
			}

			if ( mapType != ZMTYPE_STATIC )
			{
				lastNonStaticIndex = i;
				break;
			}
		}

		if ( lastStaticIndex >= 0 )
		{
			// convert last static map
			e->pSaved->lastStaticMap = CopyOldSavedMapDescription((obsolete_SavedMapDescription *)e->pSaved->obsolete_mapHistory[lastStaticIndex]);
		}

		if ( lastNonStaticIndex >= 0 )
		{
			// convert last non-static map
			e->pSaved->lastNonStaticMap = CopyOldSavedMapDescription((obsolete_SavedMapDescription *)e->pSaved->obsolete_mapHistory[lastNonStaticIndex]);
		}

		e->pSaved->bLastMapStatic = lastStaticIndex > lastNonStaticIndex;
	}

	// clean up old history
	if ( oldHistorySize != 0 )
	{
		eaClearStructNoConst(&e->pSaved->obsolete_mapHistory, parse_obsolete_SavedMapDescription);
	}
}

// If this map has not been visited yet, mark it as such
void gslEntityUpdateVisitedMaps(NOCONST(Entity)* pPlayerEnt, const char *pchMapName, const char *pchMapVars)
{
	S32 i, iVisitedIndex = eaIndexedFindUsingString(&pPlayerEnt->pPlayer->pVisitedMaps->eaMaps,pchMapName);

	// Convert empty string map vars into NULL
	if (pchMapVars && !pchMapVars[0]) {
		pchMapVars = NULL;
	}

	if ( iVisitedIndex == -1 ) {
		NOCONST(PlayerVisitedMap)* pVisited = StructCreateNoConst( parse_PlayerVisitedMap );
		pVisited->pchMapName = allocAddString(pchMapName);
		if (pchMapVars) {
			// Only push non-NULL vars
			eaPush(&pVisited->eaMapVariables, StructAllocString(pchMapVars));
		}
		eaPush(&pPlayerEnt->pPlayer->pVisitedMaps->eaMaps,pVisited);
	} else {
		// Get here if the map has been visited before and we're already tracking it
		NOCONST(PlayerVisitedMap)* pVisitedMap = pPlayerEnt->pPlayer->pVisitedMaps->eaMaps[iVisitedIndex];

		// Only add if vars are non-null, or if already have non-null vars
		if (pchMapVars || eaSize(&pVisitedMap->eaMapVariables)) {
			for ( i = eaSize(&pVisitedMap->eaMapVariables) - 1; i >= 0; i-- ) {
				// Match NULL with NULL or "", or non-NULL with string
				if ((!pchMapVars && ((pVisitedMap->eaMapVariables[i] == NULL) || (pVisitedMap->eaMapVariables[i][0] == '\0'))) ||
					(pchMapVars && stricmp(pVisitedMap->eaMapVariables[i], pchMapVars) == 0)) {
						break;
				}
			}
			if ( i < 0 ) {
				// If this is the first non-null vars pushed, then add a NULL entry as well
				if (!eaSize(&pVisitedMap->eaMapVariables)) {
					eaPush(&pVisitedMap->eaMapVariables, NULL);
				}
				eaPush(&pVisitedMap->eaMapVariables, StructAllocString(pchMapVars));
			}
		}
	}
}

void gslEntityMapHistoryEnteredMap(NOCONST(Entity)* e)
{
	MapDescription* lastMapDesc;
	bool notLastMap = false;
	NOCONST(SavedMapDescription)* newMapDesc;
	NOCONST(SavedEntityData)* noConstPlayer = e->pSaved;
	const char* pchMapName;
	ZoneMapInfo* pZoneMapInfo;
	int iPartitionIdx;

	if (DontModifyMapHistory((Entity*)e))
	{
		return;
	}
	
	iPartitionIdx = partition_GetUpcomingTransferToPartitionIdx(entGetContainerID((Entity*)e));

	//
	// Update visited maps
	//
	pchMapName = zmapInfoGetPublicName(NULL);
	pZoneMapInfo = worldGetZoneMapByPublicName( pchMapName );

	if ( pZoneMapInfo && !zmapInfoGetEffectiveDisableVisitedTracking(pZoneMapInfo))
	{
		const char* pchMapVars = partition_MapVariablesFromIdx(iPartitionIdx);

		gslEntityUpdateVisitedMaps(e, pchMapName, pchMapVars);
	}

	//
	// Update map history
	//
	ConvertOldMapHistory(e);

	lastMapDesc = (MapDescription *)entity_GetLastMap((Entity *)e);

    if ( ( lastMapDesc != NULL ) && ( lastMapDesc->eMapType != ZMTYPE_STATIC ) )
    {
        if ( lastMapDesc->containerID && lastMapDesc->containerID != GetAppGlobalID() )
        {
            // If our previous map was not a static map, then ask it to test for its own death.  If there are no players it will shut down immediately.
            RemoteCommand_TestPartitionForImmediateDeath(GLOBALTYPE_GAMESERVER, lastMapDesc->containerID, lastMapDesc->iPartitionID, PARTITIONDEATH_PLAYER_TRANSFERRED_OFF);
        }

        if ( gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_STATIC )
        {
            // If current map is static, then we want to compare to the last static map below, rather
            //  than the last map, which might be a mission map that doesn't exist anymore.
            // This is so that when we return to a static map from a mission map, we return to our last position on that map,
            //  rather than to the spawn point.
            lastMapDesc = (MapDescription *)entity_GetLastStaticMap((Entity *)e);

            notLastMap = true;
        }
    }

	if ( ( lastMapDesc == NULL ) || !IsSameMapDescription(lastMapDesc, partition_GetMapDescription(iPartitionIdx)) )
	{
		// The last map was a different map than this one, so we need to copy the current maps MapDescription and
        //  save it to the map history
		newMapDesc = StructCloneVoid(parse_SavedMapDescription, partition_GetMapDescription(iPartitionIdx));
		if (!newMapDesc)
		{
			return;
		}

        // If this map is the destination of a map transfer with a specified spawn point, then copy the spawn point over
        if ( GetAppGlobalID() == e->pSaved->transferDestinationMapID )
        {
            newMapDesc->spawnPoint = e->pSaved->transferDestinationSpawnPoint;
			copyVec3(e->pSaved->transferDestinationSpawnPos, newMapDesc->spawnPos);
			copyVec3(e->pSaved->transferDestinationSpawnPYR, newMapDesc->spawnPYR);
			newMapDesc->bSpawnPosSkipBeaconCheck = e->pSaved->bSpawnPosSkipBeaconCheck;
        }

		if(!newMapDesc->spawnPoint || !newMapDesc->spawnPoint[0])
			newMapDesc->spawnPoint = (char*)allocAddString(START_SPAWN);

		newMapDesc->iPartitionID = partition_GetUpcomingTransferToPartitionID(entGetContainerID((Entity*)e));
		newMapDesc->mapInstanceIndex = partition_PublicInstanceIndexFromID(newMapDesc->iPartitionID);

		entity_SetCurrentMap(e, (SavedMapDescription *)newMapDesc);
	}
	else 
	{
		if ( notLastMap )
		{
            // lastMapDesc is not the actual last map in the map history, but rather the last static map, so we need to
            //  clone it and add it to the map history.
			newMapDesc = StructCloneVoid(parse_SavedMapDescription, lastMapDesc);

            // If this map is the destination of a map transfer with a specified spawn point, then copy the spawn point over
            if ( GetAppGlobalID() == e->pSaved->transferDestinationMapID )
            {
                newMapDesc->spawnPoint = e->pSaved->transferDestinationSpawnPoint;
				copyVec3(e->pSaved->transferDestinationSpawnPos, newMapDesc->spawnPos);
				copyVec3(e->pSaved->transferDestinationSpawnPYR, newMapDesc->spawnPYR);
				newMapDesc->bSpawnPosSkipBeaconCheck = e->pSaved->bSpawnPosSkipBeaconCheck;
            }

			newMapDesc->iPartitionID = partition_GetUpcomingTransferToPartitionID(entGetContainerID((Entity*)e));
			newMapDesc->mapInstanceIndex = partition_PublicInstanceIndexFromID(newMapDesc->iPartitionID);
			entity_SetCurrentMap(e, (SavedMapDescription *)newMapDesc);
			lastMapDesc = (MapDescription *)newMapDesc;
		}
		else if (e->pSaved->forceTransferDestinationSpawnPoint && GetAppGlobalID() == e->pSaved->transferDestinationMapID)
		{
			lastMapDesc->spawnPoint = e->pSaved->transferDestinationSpawnPoint;
			copyVec3(e->pSaved->transferDestinationSpawnPos, lastMapDesc->spawnPos);
			copyVec3(e->pSaved->transferDestinationSpawnPYR, lastMapDesc->spawnPYR);
			lastMapDesc->bSpawnPosSkipBeaconCheck = e->pSaved->bSpawnPosSkipBeaconCheck;
		}
		//should only hit in weird cases with no static maps involved, like NNO's tutorial map... if we were last in 
		//a particular non-static map and are now in a different instance of it, clear out the spawn data
		else if (!(lastMapDesc->containerID == gGSLState.gameServerDescription.baseMapDescription.containerID && 
			lastMapDesc->iPartitionID == partition_GetUpcomingTransferToPartitionID(entGetContainerID((Entity*)e)))
			&& gGSLState.gameServerDescription.baseMapDescription.eMapType != ZMTYPE_STATIC)
		{
			lastMapDesc->spawnPoint = allocAddString(START_SPAWN);
		}

		//Make sure the non-generic stuff is correct
		//NOTE! This modified map history in place
		lastMapDesc->containerID = gGSLState.gameServerDescription.baseMapDescription.containerID;
		lastMapDesc->eMapType = gGSLState.gameServerDescription.baseMapDescription.eMapType;
		lastMapDesc->iPartitionID = partition_GetUpcomingTransferToPartitionID(entGetContainerID((Entity*)e));
		lastMapDesc->mapInstanceIndex = partition_PublicInstanceIndexFromID(lastMapDesc->iPartitionID);
	}

	//
	// Clear transfer destination info
	//
    e->pSaved->transferDestinationSpawnPoint = NULL;
    e->pSaved->transferDestinationMapID = 0;
	e->pSaved->forceTransferDestinationSpawnPoint = false;
	e->pSaved->bSpawnPosSkipBeaconCheck = false;
	zeroVec3(e->pSaved->transferDestinationSpawnPos);
	zeroVec3(e->pSaved->transferDestinationSpawnPYR);
}

void gslEntityMapHistoryLeftMap(NOCONST(Entity)* e, MapDescription* targetMapDesc)
{
	NOCONST(SavedEntityData)* noConstPlayer = e->pSaved;
	NOCONST(SavedMapDescription)* newMapDesc = NULL;
	Quat spawnRot;
	MapDescription* currMapDesc = (MapDescription *)entity_GetLastMap((Entity *)e);

	if (DontModifyMapHistory((Entity*)e))
	{
		return;
	}

	if (currMapDesc)
	{
		int iPartitionIdx = entGetPartitionIdx((Entity*)e);
		MapDescription *partitionMapDescription;

		// When ent kicked because partition is destroyed, need to handle it cleanly
		if (iPartitionIdx == PARTITION_ENT_BEING_DESTROYED) {
			iPartitionIdx = 0;
		}

		partitionMapDescription = iPartitionIdx ? partition_GetMapDescription(iPartitionIdx) : NULL;
		if ( partitionMapDescription == NULL )
		{
			// If we can't get the partition map description, then punt and use the global one for this gameserver.
			// This should only happen if the partition is being destroyed while entities are still on it.
			partitionMapDescription = &gGSLState.gameServerDescription.baseMapDescription;
		}

		if (IsSameMapDescription(currMapDesc, partitionMapDescription))
		{
			newMapDesc = MapDescription_to_SavedMapDescription_DeConst(currMapDesc);
		}
		else
		{
			newMapDesc = StructCloneVoid(parse_SavedMapDescription, partitionMapDescription);
			newMapDesc->iPartitionID = iPartitionIdx ? partition_IDFromIdx(iPartitionIdx) : 0;
			entity_SetCurrentMap(e, (SavedMapDescription *)newMapDesc);
		}
	}
	
	if (!newMapDesc)
	{
		return;
	}

	//on PvP maps, always return to the spawn point, so never reset the position
	if (gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_PVP)
	{
		return;
	}

	entGetPos((Entity *)e, newMapDesc->spawnPos);
	entGetRot((Entity *)e, spawnRot);
	quatToPYR(spawnRot, newMapDesc->spawnPYR);
	SavedMapUpdateRotationForPersistence(newMapDesc);

	newMapDesc->spawnPoint = (char*)allocAddString(POSITION_SET);
}

char const * GetExitMap(Entity* e)
{
	if ( e )
	{
		SavedMapDescription* pPossibleDest = entity_GetMapLeaveDestination( e );
		RegionRules* pCurrRules = getRegionRulesFromEnt( e );

		if (EMPTY_TO_NULL(zmapInfoGetParentMapName(NULL))) {
			return zmapInfoGetParentMapName(NULL);
		} else if ( pPossibleDest )	{
			// If parent map not set, return to last map
			return pPossibleDest->mapDescription;
		}
	}
	return NULL;
}

bool LeaveMapEx(Entity* e, DoorTransitionSequenceDef* pTransOverride)
{
	if ( e )
	{
		SavedMapDescription* pPossibleDest = entity_GetMapLeaveDestination( e );
		RegionRules* pCurrRules = getRegionRulesFromEnt( e );

		//If the combat config forces respawn on "map leave", respawn before mapmoving
		if (g_CombatConfig.bForceRespawnOnMapLeave)
		{
			gslPlayerRespawn(e, true, false);
		}
		if (EMPTY_TO_NULL(zmapInfoGetParentMapName(NULL))) {
			// If parent map set, go there instead of to previous location (if any)
			RegionRules* pNextRules = MapTransferGetRegionRulesFromMapName(zmapInfoGetParentMapName(NULL));
			MapDescription* pDestination = StructCreate(parse_MapDescription);
			MapMoveFillMapDescription(pDestination, zmapInfoGetParentMapName(NULL), ZMTYPE_UNSPECIFIED, zmapInfoGetParentMapSpawnPoint(NULL), 0, 0, 0, 0, NULL);

			// If the entity's last static map is the same as our parent map, use the entity's last saved position instead
			if (pPossibleDest && pPossibleDest->mapDescription == pDestination->mapDescription)
			{
				copyVec3(pPossibleDest->spawnPos, pDestination->spawnPos);
				copyVec3(pPossibleDest->spawnPYR, pDestination->spawnPYR);
				pDestination->spawnPoint = pPossibleDest->spawnPoint;
			}

			gslEntityPlayTransitionSequenceThenMapMove(e, pDestination, pCurrRules, pNextRules, pTransOverride,0);
			StructDestroy(parse_MapDescription, pDestination);
		} else if ( pPossibleDest )	{
			// If parent map not set, return to last map
			RegionRules* pNextRules = MapTransferGetRegionRulesFromMapName(pPossibleDest->mapDescription);
			gslEntityPlayTransitionSequenceThenMapMove(e,(MapDescription*)pPossibleDest,pCurrRules,pNextRules,pTransOverride,0);
		} else {
			// If we ever get here, the player is stuck.  Log it along with the player name and name of the current map.
			ErrorDetailsf("%s:%s", e->debugName, zmapInfoGetPublicName(NULL));
			Errorf("Player unable to leave map.  No parent or return map.");
			return false;
		}
		return true;
	}
	return false;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void LeaveMap(Entity* e)
{
	LeaveMapEx(e, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void ReturnFromMissionMap(Entity* e)
{
	MissionReturnErrorType eMissionReturnErrorType;
	if (!e)
		return;

	eMissionReturnErrorType = entity_GetReturnFromMissionMapError(e);

	if ( eMissionReturnErrorType != MissionReturnErrorType_None )
	{
		const char* pchDisplayString = StaticDefineGetTranslatedMessage(MissionReturnErrorTypeEnum,eMissionReturnErrorType);
		ClientCmd_NotifySend( e, kNotifyType_MissionReturnError, pchDisplayString, NULL, NULL );
		return;
	}

	if (LeaveMapEx(e, NULL))
	{
		gslQueue_HandleLeaveMap(e);
	}
}

static void gslHandleEntRegionRules(Entity* e)
{
	RegionRules *pRules = getRegionRulesFromEnt(e);

	if(pRules)
	{
		if(pRules->bAlwaysCollideWithPets && e->pPlayer)
		{
			mmCollisionBitsHandleDestroyFG(&e->mm.mcbHandle);
			mmCollisionBitsHandleCreateFG(e->mm.movement, &e->mm.mcbHandle, __FILE__, __LINE__, ~0);
		}
		if(pRules->fGravityMulti != 1.0)
		{
			mrSurfaceSetGravity(e->mm.mrSurface,MR_SURFACE_DEFAULT_GRAVITY * pRules->fGravityMulti);
		}
		if(e->pPlayer)
			e->pPlayer->fMovementThrottle = pRules->fLoginThrottle;
		if(e->pChar)
			e->pChar->bUpdateFlightParams = true;
	}
}

//Caches the name of the entity's new region.  Should be called every time
//an entity changes regions.  Has side effects.
void gslCacheEntRegion(Entity* e, GameAccountDataExtract *pExtract)
{
	WorldRegion *pRegion;
	Vec3 vPlayerPos;
	WorldRegionType ePrevRegionType, eNewRegionType;
	int iPartitionIdx = entGetPartitionIdx(e);

	PERFINFO_AUTO_START_FUNC();

	entGetPos(e,vPlayerPos);

	pRegion = worldGetWorldRegionByPos(vPlayerPos);

	ePrevRegionType = entGetWorldRegionTypeOfEnt(e);
	eNewRegionType = worldRegionGetType(pRegion);

	//This is the important cache.  It gets set here, and NULLed in gslPlayerLeftMap().
	e->astrRegion = worldRegionGetRegionName(pRegion);

	if ((ePrevRegionType != eNewRegionType) && (iPartitionIdx != PARTITION_IN_TRANSACTION))
	{
		aiHandleRegionChange(e, ePrevRegionType, eNewRegionType);
	}
	entity_SetDirtyBit(e, parse_Entity, e, false);

	if(e->pChar)
	{
		character_RefreshPassives(iPartitionIdx,e->pChar,pExtract);
		character_RefreshToggles(iPartitionIdx,e->pChar,pExtract);
	}

	gslHandleEntRegionRules(e);

	PERFINFO_AUTO_STOP();
}

static bool gslPlayerIsLoginPositionValid(int iPartitionIdx, const Vec3 pos, const Capsule *cap)
{
	return beaconIsPositionValid(iPartitionIdx, pos, cap);
}

static void gslInitPlayerLoginPosition(Entity* e)
{
	SavedMapDescription *spawnDescription = NULL;
	char spawnPointToUse[1024];
	static char *estrBuffer = NULL;
	Entity *pEntRecruit = NULL;
	U32 iCurrentTime = timeSecondsSince2000();
	Vec3 vecWarpTarget, vecWarpPYR;
	bool bUseVecTarget = false;

	PERFINFO_AUTO_START_FUNC();

	spawnPointToUse[0] = 0;

	if(!e->pSaved)
		return;

	if(	e->pPlayer && e->pPlayer->pWarp )
	{
		MapDescription *pMapDesc = &gGSLState.gameServerDescription.baseMapDescription;
		PlayerWarpToData *pData = e->pPlayer->pWarp;
		bool bValid = false;

		if(pData->bRecruitWarp)
		{
			pEntRecruit = entFromContainerID(entGetPartitionIdx(e), GLOBALTYPE_ENTITYPLAYER, e->pPlayer->pWarp->iEntID);
			if(pEntRecruit)
			{
				Quat recruitQuat;
				bValid = true;
				entGetPos(pEntRecruit, vecWarpTarget);
				entGetRot(pEntRecruit, recruitQuat);
				quatToPYR(recruitQuat, vecWarpPYR);
				bUseVecTarget = true;
			}
		}
		else if(pData->pchSpawn)
		{
			bValid = true;
			sprintf(spawnPointToUse, "%s", pData->pchSpawn);
		}
		else if(pData->vecTarget)
		{
			bUseVecTarget = true;
			bValid = true;
			copyVec3(pData->vecTarget, vecWarpTarget);
			zeroVec3(vecWarpPYR);
		}

		//this is the wrong map (the e isn't here) or they took too long to log back in after a crash
		if(	!bValid ||
			(pData->iMapID && pData->iMapID != pMapDesc->containerID) ||
			(pData->uPartitionID && pData->uPartitionID != partition_IDFromIdx(entGetPartitionIdx(e))) ||
			stricmp(pData->pchMap, zmapInfoGetPublicName(NULL)) ||
			(iCurrentTime > pData->iTimestamp &&
			iCurrentTime - pData->iTimestamp > 3600))
		{
			//Redundant check to see if the entity has a place to go from here
			SavedMapDescription* pPossibleDest = entity_GetMapLeaveDestination( e );

			if ( pPossibleDest )
			{
				//Then leave the map
				LeaveMap(e);	
				//Destroy the warp data
				StructDestroySafe(parse_PlayerWarpToData, &e->pPlayer->pWarp);
				
				return;
			}
		}
		else if(pData->uiItemId)
		{
			//Increment the number of charges that have been used
			Item *pItem = inv_GetItemByID(e, pData->uiItemId);
			if(pItem)
			{
				gslItem_ChargeForWarp(e, pItem);
			}
		}
	}
	if(bUseVecTarget)
	{
		sprintf(spawnPointToUse, "%s", POSITION_SET);
		if(pEntRecruit)
		{
			AutoTrans_gslWarp_RecruitCompleteTime(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(e), e->pPlayer->pWarp->iTimestamp);
		}
	}
	else if(spawnPointToUse && *spawnPointToUse)
	{
		entLog(LOG_LOGIN, e, "EnteredMap by Warp to Spawn", "Sending to Spawn point %s", spawnPointToUse);
	}

	// Find the spawn location on this map and move the player to it

	// If the player was last on this map and this is a static map, start them at their last location

	//note that entityGetLastMap() at this point will be returning the _current_ map, oddly, because it was
	//already set to the current map in gslEntityMapHistoryEnteredMap
	else
	{
		spawnDescription = entity_GetLastMap(e);

		if (spawnDescription)
		{
			if (spawnDescription->spawnPoint && spawnDescription->spawnPoint[0])
			{
				strcpy(spawnPointToUse, spawnDescription->spawnPoint);
			}
			else
			{
				Errorf("Error - map transfer without spawn type specified. Defaulting to START_SPAWN but you might want POSITION_SET. Talk to Alex. Map name: %s",
					spawnDescription->mapDescription);
				sprintf(spawnPointToUse, START_SPAWN);
			}

				
			estrClear(&estrBuffer);
			ParserWriteText(&estrBuffer, parse_SavedMapDescription, spawnDescription, 0, 0, 0);
			estrReplaceOccurrences(&estrBuffer, "\r", "");
			estrReplaceOccurrences(&estrBuffer, "\n", " ");
			estrReplaceOccurrences(&estrBuffer, "\t", " ");
			entLog(LOG_LOGIN, e, "EnteredMap", "SavedMapDescription: %s", estrBuffer);
		}
		else
		{
			entLog(LOG_LOGIN, e, "EnteredMap", "No SavedMapDescription, sending player to Start Spawn");
		}
	}

	e->initPlayerLoginPositionRun = 1;

	if (g_CombatConfig.iForceBuildOnRespawn != -1)
	{	// RP: I expect this to change, but for (CN) now we're wanting to login into servers in a particular build
		entity_BuildSetCurrentEx(e, g_CombatConfig.iForceBuildOnRespawn, false, true);
	}

	// If they have no spawn point set, move them to the start spawn.
	// Otherwise, move them to the specified spawn point
	if (( !spawnDescription && !(*spawnPointToUse) && !bUseVecTarget ) || 
		stricmp(spawnPointToUse, START_SPAWN) == 0)
	{
		spawnpoint_MovePlayerToStartSpawn(e, false);
	}
	else if (stricmp(spawnPointToUse, SPAWN_AT_NEAR_RESPAWN) == 0)
	{
		spawnpoint_MovePlayerToNearestSpawn(e, true, false);
	}
	else if (stricmp(spawnPointToUse, POSITION_SET) == 0)
	{
		Beacon *beacon;
		Entity* eMount;
		Vec3 spawnPos;
		Quat spawnRot;
		const Capsule *cap = entGetPrimaryCapsule(e);
		int iPartitionIdx = entGetPartitionIdx(e);
        bool bSpawnPosSkipBeaconCheck;

		if(!cap)
			cap = &defaultCapsule;

		//Use the warp target
		if(bUseVecTarget)
		{
			copyVec3(vecWarpTarget, spawnPos);
			spawnPos[0] += 0.01f * randomF32();
			spawnPos[2] += 0.01f * randomF32();
			PYRToQuat(vecWarpPYR, spawnRot);
		}
		// else use the spawn description
		else
		{
			ANALYSIS_ASSUME(spawnDescription!=NULL);
			copyVec3(spawnDescription->spawnPos, spawnPos);
			PYRToQuat(spawnDescription->spawnPYR, spawnRot);
		}
	
		entLog(LOG_LOGIN, e, "EnteredMap", "Setting player's position to: <%f %f %f>", spawnPos[0], spawnPos[1], spawnPos[2]);

        // Not all paths above set spawnDescription.
        if ( spawnDescription != NULL )
        {
            bSpawnPosSkipBeaconCheck = spawnDescription->bSpawnPosSkipBeaconCheck;
        }
        else
        {
            bSpawnPosSkipBeaconCheck = false;
        }

		//If the spawn position is invalid, attempt to move to a nearby beacon
		if (!bSpawnPosSkipBeaconCheck && !gslPlayerIsLoginPositionValid(iPartitionIdx, spawnPos, cap)){
			beacon = beaconGetNearestBeacon(iPartitionIdx, spawnPos);
			if (beacon){
				entLog(LOG_LOGIN, e, "EnteredMap", "Position <%f %f %f> seems to be an invalid position.  Moving player to: <%f %f %f>", spawnPos[0], spawnPos[1], spawnPos[2], beacon->pos[0], beacon->pos[1], beacon->pos[2]);
				copyVec3(beacon->pos, spawnPos);
			} else {
				entLog(LOG_LOGIN, e, "EnteredMap", "Position <%f %f %f> seems to be an invalid position, but no nearby beacon was found.", spawnPos[0], spawnPos[1], spawnPos[2]);
			}
		}

		if (eMount = entGetMount(e))
		{
			entSetPos(eMount, spawnPos, true, __FUNCTION__);
			entSetRot(eMount, spawnRot, true, __FUNCTION__);
		}
		entSetPos(e, spawnPos, true, __FUNCTION__);
		entSetRot(e, spawnRot, true, __FUNCTION__);
	}	
	else
	{
		spawnpoint_MovePlayerToNamedSpawn(e, spawnPointToUse, NULL, true);
	}

	if(e && e->pPlayer && e->pPlayer->pWarp)
		StructDestroySafe(parse_PlayerWarpToData, &e->pPlayer->pWarp);

	PERFINFO_AUTO_STOP();
}

static void gslEntityUnlockPetMovementCB(int iPartitionIdx, Entity *ePet, void* pUserData)
{
	mrDisableSetDestroySelfFlag(&ePet->mm.mrDisabled);
}

void gslEntity_UnlockMovement(SA_PARAM_NN_VALID Entity* e)
{
	// Enable player movement
	mrDisableSetDestroySelfFlag(&e->mm.mrDisabled);

	// Enable pet movement
	Entity_ForEveryPet(entGetPartitionIdx(e), e, gslEntityUnlockPetMovementCB, NULL, true, true);
}

static void gslEntityLockPetMovementCB(int iPartitionIdx, Entity *ePet, void* pUserData)
{
	if(!ePet->mm.mrDisabled)
	{
		mrDisableCreate(ePet->mm.movement,&ePet->mm.mrDisabled);
	}
}

void gslEntity_LockMovement(SA_PARAM_NN_VALID Entity* e, bool bIncludePets)
{
	if (e->pPlayer==NULL)
		return;

	// Lock the player's movement
	if(!e->mm.mrDisabled)
	{
		mrDisableCreate(e->mm.movement, &e->mm.mrDisabled);
	}
	// Lock pet movement
	if (bIncludePets) 
	{
		Entity_ForEveryPet(entGetPartitionIdx(e), e, gslEntityLockPetMovementCB, NULL, true, true);
	}
}

//this function does fixup on persisted non-transacted fields on the entity
static void entity_NonTransactedFixup(Entity* e)
{
	PERFINFO_AUTO_START_FUNC();

	//this modifies pLooseUI, which isn't directly non-transacted, 
	//however the structure is saved out as a non-transacted string
	gslHUDOptions_Fixup(e);

	PERFINFO_AUTO_STOP();
}

static void gslEntity_LoginMinigames(Entity* e)
{
	PERFINFO_AUTO_START_FUNC();

	gslGameSpecific_Minigame_Login(e);

	PERFINFO_AUTO_STOP();
}

void gslPlayerEnteredMap(Entity* e, bool bInitPosition)
{
	GameAccountDataExtract *pExtract;
	char *pTempString = NULL;

	PERFINFO_AUTO_START_FUNC();
	coarseTimerAddInstance(NULL, "PlayerEnteredMap");

	pExtract = entity_GetCachedGameAccountDataExtract(e);

	// save the time this player entered the map
	if ( e->pSaved != NULL )
	{
		e->pSaved->timeEnteredMap = timeSecondsSince2000();
		e->pSaved->bValidatedOwnedContainers = false;
		entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, 1);
	}
	if (bInitPosition) {
		gslInitPlayerLoginPosition(e);
	}
	mission_PlayerEnteredMap(e);
	ServerChat_PlayerEnteredMap(e);
	gslCacheEntRegion(e,pExtract);
	gslHandlePetsAtLogin(e);
	entity_TrayFixup(e); //This needs to be called after gslHandlePetsAtLogin
	entity_NonTransactedFixup(e);
	cutscene_PlayerEnteredMap(e);
	gslHandleDoorTransitionSequenceSetup(e);
	entity_PowerTreeAutoBuy(entGetPartitionIdx(e),e,NULL);
	gslEntity_LoginMinigames(e);
	entity_validateLeaderboardStats(e);
	pfsm_PlayerEnterMap(e);
	playermatchstats_RegisterPlayer(e);
	gslItemAssignments_PlayerUpdateNextProcessTime(e);
	gslItemAssignments_UpdatePlayerAssignments(e);
	gslActivity_UpdateEvents(e);
	gslCheckForDeprecatedCostumeParts(e);
	gslScoreboard_CreatePlayerScoreData(e);
	gslDoAutoNumericConversion(e);
	gslEvent_AddAllPlayerSubscriptions(e);
	gslItemAssignments_CheckExpressionSlots(e,NULL);

	// Make sure pet overflow items move to player overflow if required
	ItemCollectPetOverflow(e, pExtract);

	PERFINFO_AUTO_START("SendingClientCommands", 1);
	estrStackCreate(&pTempString);
	estrPrintf(&pTempString, "%u(%u)", GetAppGlobalID(), partition_IDFromIdx(entGetPartitionIdx(e)));
	ClientCmd_SetServerAndPartitionInfoDebugString(e, pTempString);
	estrDestroy(&pTempString);

	ClientCmd_SetInstanceIndex(e, partition_PublicInstanceIndexFromIdx(entGetPartitionIdx(e)));

	ClientCmd_SetInstanceSwitchingAllowed(e, 
		!zmapInfoGetDisableInstanceChanging(NULL) && 
			(gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_STATIC
				|| gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_SHARED
				|| gGSLState.gameServerDescription.bAllowInstanceSwitchingBetweenOwnedMaps && partition_OwnerTypeFromIdx(entGetPartitionIdx(e))));
	PERFINFO_AUTO_STOP();
	
	UserExp_LogMapTransferArrival(e); // Do this after all other setup

	coarseTimerStopInstance(NULL, "PlayerEnteredMap");
	PERFINFO_AUTO_STOP();
}

void gslPlayerLeftMap(Entity* e, bool bRemovePets)
{
	if (e->pPlayer && e->pPlayer->pUI)
		e->pPlayer->pUI->eLastRegion = entGetWorldRegionTypeOfEnt(e);

	UserExp_LogMapTransferDeparture(e); // Do this before unsetting any data

	e->astrRegion = NULL;	//clear the cached region, because it won't be valid on the next map.

	playermatchstats_PlayerLeaving(e);
	mission_PlayerLeftMap(e);
	ServerChat_PlayerLeftMap(e);
	gslScoreboard_DestroyPlayerScoreData(e);

	if(bRemovePets)
	{
		PetCommands_Targeting_Cleanup(e);
		gslHandleSavedPetsAtLogout(e);	
		gslHandleCritterPetsAtLogout(e);
		gslHandleSuperCritterPetsAtLogout(e);
	}
	
	cutscene_PlayerLeftMap(e);
	pfsm_PlayerLeaveMap(e);
	gslTeamUp_LeaveTeam(e,true);
	gslEvent_RemoveAllPlayerSubscriptions(e);
}

void gslEntityStartReceivingNearbyPlayerFlag(Entity* e)
{
	if (! e->pNodeNearbyPlayer)
	{
		e->pNodeNearbyPlayer = ImbeddedList_NodeAlloc(e);	
		e->nearbyPlayer = false;
	}
}

void gslEntityStopReceivingNearbyPlayerFlag(Entity* e)
{
	if (e->pNodeNearbyPlayer)
	{
		ImbeddedList_NodeFree(&e->pNodeNearbyPlayer);
		e->nearbyPlayer = false;
	}
}

// ----------------------------------------------------------------------------
// Hack to make offline CSR commands work
// Track players who were only brought online for a CSR command, so we can
// avoid certain data fix-ups
// ----------------------------------------------------------------------------
static GlobalType* s_eaOfflineCSREntTypes = NULL;
static ContainerID* s_eaOfflineCSREntIDs = NULL;

void gslOfflineCSREntAdd(GlobalType eType, ContainerID uID)
{
	eaiPush(&s_eaOfflineCSREntTypes, eType);
	ea32Push(&s_eaOfflineCSREntIDs, uID);
	assert(eaiSize(&s_eaOfflineCSREntTypes) == ea32Size(&s_eaOfflineCSREntIDs));
}

void gslOfflineCSREntRemove(GlobalType eType, ContainerID uID)
{
	int i;
	assert(eaiSize(&s_eaOfflineCSREntTypes) == ea32Size(&s_eaOfflineCSREntIDs));
	for (i = eaiSize(&s_eaOfflineCSREntTypes)-1; i>=0; --i){
		if (s_eaOfflineCSREntTypes[i] == eType && s_eaOfflineCSREntIDs[i] == uID){
			eaiRemove(&s_eaOfflineCSREntTypes, i);
			ea32Remove(&s_eaOfflineCSREntIDs, i);
			assert(eaiSize(&s_eaOfflineCSREntTypes) == ea32Size(&s_eaOfflineCSREntIDs));
			break;
		}
	}
}

bool gslIsOfflineCSREnt(GlobalType eType, ContainerID uID)
{
	int i;
	assert(eaiSize(&s_eaOfflineCSREntTypes) == ea32Size(&s_eaOfflineCSREntIDs));
	for (i = eaiSize(&s_eaOfflineCSREntTypes)-1; i>=0; --i){
		if (s_eaOfflineCSREntTypes[i] == eType && s_eaOfflineCSREntIDs[i] == uID){
			return true;
		}
	}
	return false;
}


//FIXME make this function actually do something
bool EntityCanGetAccessLevel(Entity *pPlayerEntity, int iNewLevel)
{
	if (iNewLevel < 0)
	{
		return false;
	}

	if (iNewLevel > pPlayerEntity->pPlayer->accountAccessLevel)
	{
		return false;
	}

	return true;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(AccessLevel) ACMD_HIDE;
void SetAccessLevel(Entity *pPlayerEntity, int iNewLevel, CmdContext *pCmdContext)
{
	if (EntityCanGetAccessLevel(pPlayerEntity, iNewLevel))
	{
		objRequestTransactionSimplef(NULL,entGetType(pPlayerEntity), entGetContainerID(pPlayerEntity),
			"SetAccessLevel", "set pPlayer.AccessLevel = %d",iNewLevel);

		ClientCmd_EnableDevModeKeybinds(pPlayerEntity, iNewLevel==9);



		estrPrintf(pCmdContext->output_msg, "Access level %d being granted", iNewLevel);
	}
	else
	{
		estrPrintf(pCmdContext->output_msg, "Unknown command \"accesslevel\"");
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(AccessLevel);
void GetAccessLevel(Entity *pPlayerEntity, CmdContext *pCmdContext)
{
	estrPrintf(pCmdContext->output_msg, "%d", entGetAccessLevel(pPlayerEntity));
}



AUTO_COMMAND ACMD_PRIVATE;
void SetAccessLevelByContainerID(ContainerID iID, int iNewLevel, CmdContext *pCmdContext)
{
	Entity *pPlayerEntity;

	if (!iID)
	{
		return;
	}

	pPlayerEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iID);


	if (pPlayerEntity)
	{
		if (EntityCanGetAccessLevel(pPlayerEntity, iNewLevel))
		{			
			objRequestTransactionSimplef(NULL,entGetType(pPlayerEntity), entGetContainerID(pPlayerEntity),
				"SetAccessLevelByContID", "set pPlayer.AccessLevel = %d",iNewLevel);

			estrPrintf(pCmdContext->output_msg, "Access level %d being granted", iNewLevel);
		}
		else
		{
			estrPrintf(pCmdContext->output_msg, "Access level %d not allowed for entity %u", iNewLevel, 
				entGetContainerID(pPlayerEntity));
		}

	}
	else
	{
		estrPrintf(pCmdContext->output_msg, "entity %u doesn't seem to exist, somehow. Wtf?", iID);
	}

}

void gslEntSetPrimaryPet(Entity* eOwner, Entity* ePet)
{
	PERFINFO_AUTO_START_FUNC();
	if (eOwner && eOwner->pChar)
	{
		Entity* eOldPet = entFromEntityRef(entGetPartitionIdx(eOwner), eOwner->pChar->primaryPetRef);
		if (eOldPet && eOldPet != ePet)
		{
			eOwner->pChar->primaryPetRef = 0;
			entity_SetDirtyBit(eOwner, parse_Character, eOwner->pChar, false);
			eOldPet->pChar->bIsPrimaryPet = 0;
			eOldPet->erOwner = 0;
			entity_SetTarget(eOldPet, 0);
		}
		if (ePet)
		{
			encounter_RemoveActor(ePet); // New encounter system
			if (gConf.bAllowOldEncounterData)
				oldencounter_RemoveActor(ePet); // Old encounter system
			eOwner->pChar->primaryPetRef = entGetRef(ePet);
			entity_SetDirtyBit(eOwner, parse_Character, eOwner->pChar, false);
			ePet->pChar->bIsPrimaryPet = 1;
			ePet->erOwner = entGetRef(eOwner);
			entity_AssistTarget(ePet,ePet->erOwner);
		}
	}
	PERFINFO_AUTO_STOP();
}

bool gslEntAttachToEnt(Entity* e, Entity* eAttach, const char *pBoneName, const char *pExtraBit, const Vec3 posOffset, const Quat rotOffset)
{
	if (!eAttach)
	{
		if (e->pAttach)
		{
			Entity *parent;
			if (e->pAttach->erAttachedTo &&
				(parent = entFromEntityRef(entGetPartitionIdx(e), e->pAttach->erAttachedTo)))
			{
				// Remove if valid
				if (parent->pAttach)
				{					
					eaiFindAndRemove(&parent->pAttach->eaiAttached, e->myRef);
				}
				gslEntityForceFullSend(parent);
			}
			e->pAttach->erAttachedTo = 0;			
		}
		// HACK to disable collision for now
		mmNoCollHandleDestroyFG(&e->mm.mnchAttach);
		gslEntUpdateAttach(e);
		gslEntityForceFullSend(e);
		entity_SetDirtyBit(e, parse_Entity, e, false);

		return true;
	}

	if (e->pAttach && e->pAttach->erAttachedTo && eAttach->myRef != e->pAttach->erAttachedTo)
	{
		// Clear old attachment
		gslEntAttachToEnt(e, NULL, NULL, NULL, posOffset, rotOffset);
	}

	if (!e->pAttach)
	{
		DECONST(void *,e->pAttach) = StructCreate(parse_SavedEntityData);
	}

	e->pAttach->erAttachedTo = eAttach->myRef;
	e->pAttach->pBoneName = allocAddString(pBoneName);
	e->pAttach->pExtraBit = allocAddString(pExtraBit);
	entity_SetDirtyBit(e, parse_Entity, e, false);

	copyVec3(posOffset, e->pAttach->posOffset);
	copyQuat(rotOffset, e->pAttach->rotOffset);

	if (!eAttach->pAttach)
	{
		DECONST(void *,eAttach->pAttach) = StructCreate(parse_SavedEntityData);
	}

	eaiPushUnique(&eAttach->pAttach->eaiAttached, e->myRef);
	entity_SetDirtyBit(eAttach, parse_Entity, eAttach, false);

	mmNoCollHandleCreateFG(e->mm.movement, &e->mm.mnchAttach, __FILE__, __LINE__);
	gslEntUpdateAttach(e);

	gslEntityForceFullSend(e);
	gslEntityForceFullSend(eAttach);
	return true;
}


void gslEntUpdateAttach(Entity* e)
{
	if (!e->pAttach)
	{
		return;
	}
	if (e->pAttach->erAttachedTo)
	{
		Vec3 tempVec;
		Entity* eAttach = entFromEntityRef(entGetPartitionIdx(e), e->pAttach->erAttachedTo);
		if (!eAttach)
		{
			return;
		}
		// TODO: make this case use bone position
		entGetPos(eAttach, tempVec);
		entSetPos(e, tempVec, true, __FUNCTION__);
	}	
}


AUTO_COMMAND ACMD_NAME(Ride) ACMD_SERVERONLY;
void Ride(Entity* e)
{	
	if(!SAFE_MEMBER(e, pChar)){
		return;
	}

	if (entGetMount(e))
	{
		gslEntCancelRide(e);
	}
	else if (entFromEntityRef(entGetPartitionIdx(e), e->pChar->currentTargetRef))
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		gslEntRideCritter(e, entFromEntityRef(entGetPartitionIdx(e), e->pChar->currentTargetRef), pExtract);
	}
}


bool gslEntIsRidable(Entity *beRider, Entity *beMount)
{
	CritterDef *pCritterDef;
	static ExprContext* context = NULL;
	MultiVal mval;

	if (!beMount || !beMount->pCritter)
	{
		return false;
	}
	if (beMount->erOwner && beMount->erOwner != entGetRef(beRider))
	{
		// Can't steal a mount
		return false;
	}
	pCritterDef = GET_REF(beMount->pCritter->critterDef);

	if(!context)
		context = exprContextCreate();

	if (!pCritterDef || !pCritterDef->pExprRidable)
	{
		return false;
	}

	exprContextSetPointerVar(context, "Player", beRider, NULL, false, true);
	exprContextSetSelfPtr(context,beMount);
	exprEvaluate(pCritterDef->pExprRidable,context,&mval);
	return (bool)!!MultiValGetInt(&mval, NULL);
}

void gslEntControlCritter(Entity* e, Entity* eTarget)
{

	if(!e || !eTarget)
		return;

	// HORRIBLE HACK. Replace this with proper build support
	entity_TraySetUITrayIndex(eTarget, 0, 1);

	{
		MovementClient *mc = SAFE_MEMBER3(e, pPlayer, clientLink, movementClient);

		mmAttachToClient(eTarget->mm.movement, mc);
		mmDetachFromClient(e->mm.movement, NULL);
	}

	if(eTarget->aibase)
		eTarget->aibase->disableAI = 1;

	//e->pSaved->eControlingPet = entGetRef(eTarget);

	gslEntSetPrimaryPet(e, eTarget);

	gslEntityForceFullSend(e);
	gslEntityForceFullSend(eTarget);
}

void gslEntEndControlCritter(Entity* e, Entity* eTarget)
{
	if(!e || !eTarget)
		return;

	mmDetachFromClient(eTarget->mm.movement, NULL);

	eTarget->aibase->disableAI = 0;
	aiMovementResetPath(eTarget, eTarget->aibase);

	{
		MovementClient *mc = SAFE_MEMBER3(e, pPlayer, clientLink, movementClient);

		mmAttachToClient(e->mm.movement, mc);	

		eTarget->aibase->disableAI = 0;
	}

	//e->pSaved->eControlingPet = 0;

	gslEntityForceFullSend(eTarget);
	gslEntityForceFullSend(e);
}

void gslEntRideCritter(Entity* e, Entity* eTarget, GameAccountDataExtract *pExtract)
{	
	Vec3 tempVec = {0, 0, 0};
	Entity*			beRider = e;
	Entity*			beMount = eTarget;

	CritterDef *pCritterDef;

	if (!gslEntIsRidable(beRider, beMount))
	{
		return;
	}

	pCritterDef = GET_REF(beMount->pCritter->critterDef);
	if (!pCritterDef)
	{
		return;
	}
	
	// HORRIBLE HACK. Replace this with proper build support
	entity_TraySetUITrayIndex(beRider, 0, 1);	

	entSetCodeFlagBits(beRider, ENTITYFLAG_UNTARGETABLE);
	entSetCodeFlagBits(beRider, ENTITYFLAG_UNSELECTABLE);

	gslEntAttachToEnt(beRider, beMount, "Ride", pCritterDef->pchRidingBit ? pCritterDef->pchRidingBit : "Riding", tempVec, unitquat);
	if(!beRider->pAttach)
		DECONST(EntityAttach*, beRider->pAttach) = StructCreate(parse_EntityAttach);
	beRider->pAttach->bRiding = true;

	{		
		MovementClient*	mc = SAFE_MEMBER3(beRider, pPlayer, clientLink, movementClient);

		mmAttachToClient(beMount->mm.movement, mc);
		mmDetachFromClient(beRider->mm.movement, NULL);

		beMount->aibase->disableAI = 1;
	}

	if (pCritterDef->pchRidingPower)
	{
		PowerDef *pdef = powerdef_Find(pCritterDef->pchRidingPower);
		if(pdef)
		{
			character_AddPowerPersonal(entGetPartitionIdx(beRider), beRider->pChar, pdef, 0, true, pExtract);
		}
	}
	if (pCritterDef->pchRidingItem)
	{
		GiveAndEquipItem(beRider, pCritterDef->pchRidingItem);
	}

	entity_SetTarget(beRider, 0);

	// Set mount as primary pet.  Don't disable it later for now.
	gslEntSetPrimaryPet(beRider, beMount);

	entity_TrayAutoFillPet(beRider, NULL);

	gslEntityForceFullSend(beRider);
	gslEntityForceFullSend(beMount);
}

void gslEntCancelRide(Entity* e)
{
	Vec3 tempVec = {0, 0, 0};
	Entity*			beRider = e;
	Entity*			beMount = NULL;

	CritterDef *pCritterDef;
	beMount = entFromEntityRef(entGetPartitionIdx(e), beRider->pAttach->erAttachedTo);

	if (beMount)
	{
		pCritterDef = GET_REF(beMount->pCritter->critterDef);

		if (pCritterDef && pCritterDef->pchRidingPower)
		{
			PowerDef *pdef = powerdef_Find(pCritterDef->pchRidingPower);
			if(pdef)
			{
				Power *ppow = character_FindNewestPowerByDef(beRider->pChar, pdef);
				if(ppow)
				{
					character_RemovePowerPersonal(beRider->pChar,ppow->uiID);
				}
			}
		}
		if (pCritterDef && pCritterDef->pchRidingItem)
		{
			RemoveItem(beRider, pCritterDef->pchRidingItem);
		}

		mmDetachFromClient(beMount->mm.movement, NULL);

		beMount->aibase->disableAI = 0;
		aiMovementResetPath(beMount, beMount->aibase);

		gslEntityForceFullSend(beMount);
	}

	{
		MovementClient*	mc = SAFE_MEMBER3(beRider, pPlayer, clientLink, movementClient);

		mmAttachToClient(beRider->mm.movement, mc);
	}

	entClearCodeFlagBits(beRider, ENTITYFLAG_UNTARGETABLE);
	entClearCodeFlagBits(beRider, ENTITYFLAG_UNSELECTABLE);	

	// HORRIBLE HACK. Replace this with proper build support
	entity_TraySetUITrayIndex(beRider, 0, 0);

	gslEntAttachToEnt(e, NULL, NULL, NULL, tempVec, unitquat);

	beRider->pAttach->bRiding = false;

	costumeEntity_RegenerateCostume(beRider);
	gslEntityForceFullSend(beRider);
}

extern ParseTable parse_Team[];
#define TYPE_parse_Team Team
extern ParseTable parse_Guild[];
#define TYPE_parse_Guild Guild
extern ParseTable parse_CurrencyExchangeAccountData[];
#define TYPE_parse_CurrencyExchangeAccountData CurrencyExchangeAccountData

static void entity_PlayerDictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, Entity* e, void *userData)
{
	if (!e)
		return;

	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		// A new player was just added to the dictionary of player copies; this is probably a teammate
		// who is on a different map.
		// Initialize the player's missions to fixup back pointers, etc.  Don't actually track events or
		// prepare to process missions, since this is only a copy of the real player.
		if (e->pPlayer)
		{
			mission_PostEntityCreateMissionInit(e, false);
		}

		if (eType == RESEVENT_RESOURCE_ADDED)
		{
			if(GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER)
			{
				wrCSub_EntitySubscribed(e);
			}
		}
	}

	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		gslGatewayServer_ContainerSubscriptionUpdate(eType, e->myEntityType, e->myContainerID);
	}

}

static void entity_SavedPetDictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, Entity* e, void *userData)
{
	if (!e)
		return;

	if (eType == RESEVENT_RESOURCE_ADDED)
	{
		// When a Nemesis loads, some data on the player must be updated
		if (e->pNemesis){
			nemesis_DictionaryLoadCB(e);
		}
	}

	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		gslGatewayServer_ContainerSubscriptionUpdate(eType, e->myEntityType, e->myContainerID);
	}
}

static void guildDictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, Guild* guild, void *userData)
{
	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER && guild)
	{
		gslGatewayServer_ContainerSubscriptionUpdate(eType, GLOBALTYPE_GUILD, guild->iContainerID);
	}
}


static void entity_DictPreContainerSendCB(	const char *dictName,
											const char *resourceName,
											const Entity* eNew,
											const Entity* ePlayer,
											StructTypeField *excludeFlagsInOut,
											StructTypeField *includeFlagsInOut)
{
	// Start by adding flags to restrict sending
	*excludeFlagsInOut |= TOK_SELF_ONLY | TOK_SELF_AND_TEAM_ONLY;

	// Note that we don't have a player entity when sending data from the login server
	if (ePlayer && eNew) {
		if (eNew->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
			if (eNew->myContainerID == ePlayer->myContainerID) {
				// This is the current player
				*excludeFlagsInOut &= ~(TOK_SELF_ONLY | TOK_SELF_AND_TEAM_ONLY);
			}
			else if(team_GetTeamID(ePlayer) &&
					team_GetTeamID(ePlayer) == team_GetTeamID(eNew))
			{
				// Player is on same team, so allow team data
				*excludeFlagsInOut &= ~TOK_SELF_AND_TEAM_ONLY;
			}
			else if(ePlayer->pTeamUpRequest && eNew->pTeamUpRequest && 
				ePlayer->pTeamUpRequest->eState == kTeamUpState_Member &&
				eNew->pTeamUpRequest->eState == kTeamUpState_Member &&
				ePlayer->pTeamUpRequest->uTeamID == eNew->pTeamUpRequest->uTeamID)
			{
				*excludeFlagsInOut &= ~TOK_SELF_AND_TEAM_ONLY;
			}
		}
		else if (eNew->pSaved) {
			if ((eNew->pSaved->conOwner.containerType == GLOBALTYPE_ENTITYPLAYER) &&
				(eNew->pSaved->conOwner.containerID == ePlayer->myContainerID)) {
				// This pet entity is owned by current player
				*excludeFlagsInOut &= ~(TOK_SELF_ONLY | TOK_SELF_AND_TEAM_ONLY);
			}
			else if(team_GetTeamID(ePlayer) &&
					team_GetTeamID(ePlayer) == team_GetTeamID(eNew))
			{
				// This pet's owner is on a team with current player
				*excludeFlagsInOut &= ~TOK_SELF_AND_TEAM_ONLY;
			}
		}
		else if(eNew->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK)
		{
			if (eNew->myContainerID == ePlayer->pPlayer->accountID)
			{
				// This is the current player
				*excludeFlagsInOut &= ~(TOK_SELF_ONLY);
			}
		}
		else if(eNew->myEntityType == GLOBALTYPE_ENTITYGUILDBANK)
		{
			if (ePlayer->pPlayer->pGuild && eNew->myContainerID == ePlayer->pPlayer->pGuild->iGuildID)
			{
				// This is the current player
				*excludeFlagsInOut &= ~(TOK_SELF_ONLY);
			}
		}
	}
}

static void teamDictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, Team* pTeam, void *userData)
{
	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		if (pTeam->eMode != pTeam->eCachedMode || team_NumPresentMembers(pTeam) != pTeam->iCachedMembers || strcmp_safe(pTeam->pchCachedStatusMessage, pTeam->pchStatusMessage))
		{
			Entity* e = team_GetTeamLeaderAnyPartition(pTeam);
			pTeam->eCachedMode = pTeam->eMode;
			pTeam->iCachedMembers = team_NumPresentMembers(pTeam);
			estrCopy2(&pTeam->pchCachedStatusMessage, pTeam->pchStatusMessage);
			if (e)
			{
				// We need to tell the chatserver when the leader changes team mode, to update team search
				ServerChat_PlayerUpdate(e, CHATUSER_UPDATE_SHARD);
			}

		}
	}
}

static int sEntCopyDictCacheSize = RES_DICT_KEEP_NONE;
AUTO_CMD_INT(sEntCopyDictCacheSize, CopyDictCacheSize) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

AUTO_RUN_LATE;
int RegisterEntityContainers(void)
{
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (GlobalTypeParent(i) == GLOBALTYPE_ENTITY)
		{
			const char *pcDictName = GlobalTypeToCopyDictionaryName(i);
			objRegisterNativeSchema(i,parse_Entity, entServerCreateCB,entServerDestroyCB,entServerInitCB,entServerDeInitCB, entServerRegisterCB);
			RefSystem_RegisterSelfDefiningDictionary(pcDictName, false, parse_Entity, false, false, NULL);
			resDictRequestMissingResources(pcDictName, GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER ? 1024 : sEntCopyDictCacheSize, false, objCopyDictHandleRequest);
			resDictProvideMissingResources(pcDictName);

			if (i == GLOBALTYPE_ENTITYPLAYER) {
				resDictRegisterEventCallback(pcDictName, entity_PlayerDictionaryChangeCB, NULL);
			}
			else if (i == GLOBALTYPE_ENTITYSAVEDPET) {
				resDictRegisterEventCallback(pcDictName, entity_SavedPetDictionaryChangeCB, NULL);
			}

			resRegisterPreContainerSendCB(pcDictName, entity_DictPreContainerSendCB);
		}
	}
	
	// Register team while we're at it
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), false, parse_Team, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM));
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), teamDictionaryChangeCB, NULL);
	
	// Register guild while we're at it
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), false, parse_Guild, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD));
	resDictRegisterEventCallback(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), guildDictionaryChangeCB, NULL);
	
	// Register diary entries while we're at it
	gslDiary_SchemaInit();

	// Register currency exchange while we're at it
	gslCurrencyExchange_SchemaInit();

    // Register group project schema.
    gslGroupProject_SchemaInit();

	mrNameRegisterID("AIMovement",			MR_CLASS_ID_AI);
	mrNameRegisterID("AICivilianMovement",	MR_CLASS_ID_AI_CIVILIAN);
	mrNameRegisterID("ProjectileEntMovement",	MR_CLASS_ID_PROJECTILEENTITY);

	return 1;
}

Entity* entExternGetCommandEntity(CmdContext *context)
{
	if (context->eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC)
	{
		return context->data;
	}
	else
	{
		if(!context->svr_context)
		{
			return NULL;
		}
		else
		{
			return context->svr_context->clientEntity;
		}
	}
}


// --------------------------------------------------------------------------
// Transaction to change a pets name
// --------------------------------------------------------------------------

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Psaved.Costumedata.Eacostumeslots, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(e, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Psaved.Costumedata.Eacostumeslots, .Psaved.Savedname, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Savedsubname, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pData, ".Eakeys");
enumTransactionOutcome entity_tr_RenamePet(ATR_ARGS, NOCONST(Entity)* pOwner, NOCONST(Entity)* e, 
										   const char *newName, int bUpdateOwnerCostumes, int bGetCostAndDiscountFromOwner, 
										   const ItemChangeReason *pReason, NOCONST(GameAccountData) *pData)
{
	const char* pchCostNumeric = g_PetRestrictions.pchRenameCostNumeric;
	int i;
	
	if ((ISNULL(e)) || (ISNULL(e->pSaved)) || (!newName)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( pchCostNumeric && pchCostNumeric[0] )
	{
		//NOTE: this relies on the transaction succeeding if there are free name changes left
		S32 iCost = trhPet_GetCostToRename( ATR_PASS_ARGS, pOwner, e, true, pReason, pData );
		//S32 iCost = bGetCostAndDiscountFromOwner ? trhEnt_GetCostToRename( ATR_EMPTY_ARGS, pOwner, true ) : trhPet_GetCostToRename( ATR_EMPTY_ARGS, pOwner, e, true );

		if ( iCost > 0 )
		{
			S32 iOwnerCurrency = inv_trh_GetNumericValue( ATR_PASS_ARGS, pOwner, pchCostNumeric );
			if ( iOwnerCurrency < iCost || !inv_ent_trh_SetNumeric( ATR_PASS_ARGS, pOwner, true, pchCostNumeric, iOwnerCurrency - iCost, pReason ) ) {
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	strcpy(e->pSaved->savedName, newName);

	// Update costumes due to name change
	if (bUpdateOwnerCostumes)
	{
		//The name is on the puppet but the costume is on the owner - In STO this is the situation when a player is a ship in space
		for(i=eaSize(&pOwner->pSaved->costumeData.eaCostumeSlots)-1; i>=0; --i) {
			costumeEntity_trh_ApplyEntityInfoToCostume(ATR_PASS_ARGS, e, pOwner->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
		}
	}
	else
	{
		for(i=eaSize(&e->pSaved->costumeData.eaCostumeSlots)-1; i>=0; --i) {
			costumeEntity_trh_ApplyEntityInfoToCostume(ATR_PASS_ARGS, e, e->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pOwner, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Costumedata.Eacostumeslots, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(e, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Savedsubname, .Psaved.Costumedata.Eacostumeslots, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Savedname, .Pplayer.Pugckillcreditlimit, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pData, ".Eakeys");
enumTransactionOutcome entity_tr_SubRenamePet(ATR_ARGS, NOCONST(Entity)* pOwner, NOCONST(Entity)* e, 
											  const char *newName, int bUpdateOwnerCostumes, int bGetCostAndDiscountFromOwner, 
											  const ItemChangeReason *pReason, NOCONST(GameAccountData) *pData)
{
	const char* pchCostNumeric = g_PetRestrictions.pchChangeSubNameCostNumeric;
	int i;
	
	if ((ISNULL(e)) || (ISNULL(e->pSaved))) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( pchCostNumeric && pchCostNumeric[0] )
	{
		//NOTE: this relies on the transaction succeeding if there are free name changes left
		S32 iCost = trhPet_GetCostToChangeSubName( ATR_PASS_ARGS, pOwner, e, true, pReason, pData );

		if ( iCost > 0 )
		{
			S32 iOwnerCurrency = inv_trh_GetNumericValue( ATR_PASS_ARGS, pOwner, pchCostNumeric );
			if ( iOwnerCurrency < iCost || !inv_ent_trh_SetNumeric( ATR_EMPTY_ARGS, pOwner, true, pchCostNumeric, iOwnerCurrency - iCost, pReason ) ) {
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	if(e->pSaved->savedSubName)
		StructFreeString(e->pSaved->savedSubName);
	if (newName && *newName)
	{
		e->pSaved->savedSubName = StructAllocString(newName);
	}
	else
	{
		e->pSaved->savedSubName = NULL;
	}

	// Update costumes due to name change
	if (bUpdateOwnerCostumes)
	{
		//The name is on the puppet but the costume is on the owner - In STO this is the situation when a player is a ship in space
		for(i=eaSize(&pOwner->pSaved->costumeData.eaCostumeSlots)-1; i>=0; --i) {
			costumeEntity_trh_ApplyEntityInfoToCostume(ATR_PASS_ARGS, e, pOwner->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
		}
	}
	else
	{
		for(i=eaSize(&e->pSaved->costumeData.eaCostumeSlots)-1; i>=0; --i) {
			costumeEntity_trh_ApplyEntityInfoToCostume(ATR_PASS_ARGS, e, e->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Savedsubname, .Psaved.Costumedata.Eacostumeslots, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Savedname, .Pplayer.Pugckillcreditlimit, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pData, ".Eakeys");
enumTransactionOutcome entity_tr_SubRename(ATR_ARGS, NOCONST(Entity)* e, const char *newName, int bUpdateCostumes, const ItemChangeReason *pReason, NOCONST(GameAccountData) *pData)
{
	const char* pchCostNumeric = g_PetRestrictions.pchChangeSubNameCostNumeric;
	int i;

	if ((ISNULL(e)) || (ISNULL(e->pSaved))) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( pchCostNumeric && pchCostNumeric[0] )
	{
		//NOTE: this relies on the transaction succeeding if there are free name changes left
		S32 iCost = trhEnt_GetCostToChangeSubName( ATR_PASS_ARGS, e, true, pReason, pData );

		if ( iCost > 0 )
		{
			S32 iOwnerCurrency = inv_trh_GetNumericValue( ATR_PASS_ARGS, e, pchCostNumeric );
			if ( iOwnerCurrency < iCost || !inv_ent_trh_SetNumeric( ATR_PASS_ARGS, e, true, pchCostNumeric, iOwnerCurrency - iCost, pReason ) ) {
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	if(e->pSaved->savedSubName)
		StructFreeString(e->pSaved->savedSubName);
	if (newName && *newName)
	{
		e->pSaved->savedSubName = StructAllocString(newName);
	}
	else
	{
		e->pSaved->savedSubName = NULL;
	}

	// Update costumes due to name change
	if (bUpdateCostumes)
	{
		for(i=eaSize(&e->pSaved->costumeData.eaCostumeSlots)-1; i>=0; --i) {
			costumeEntity_trh_ApplyEntityInfoToCostume(ATR_PASS_ARGS, e, e->pSaved->costumeData.eaCostumeSlots[i]->pCostume);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


void entitytransaction_RenamePetCallback(TransactionReturnVal *pReturn, RenamePetData *pData)
{
	// We have to call "entSetActive" after the transaction or the
	// changes to the entity will not e sent to the client.
	// Only active entities (set manually or moved by the movement system) get sent
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* e = entFromEntityRefAnyPartition(pData->entRef);
		Entity *pOwnerEnt = NULL;
		if (pData->iID && e)
		{
			pOwnerEnt = e;
			e = entity_GetSubEntity(entGetPartitionIdx(pOwnerEnt), pOwnerEnt, pData->eType, pData->iID);
		}
		if (e) {
			if (!pOwnerEnt) pOwnerEnt = entGetOwner(e);
			
			// if we have all the data, then log a pet rename activity
			if ( e && pOwnerEnt && pData->oldName ) {
				gslActivity_AddPetRenameEntry(pOwnerEnt, e, pData->oldName);
			}

			if (pOwnerEnt && pData->bUpdateOwnerCostumes)
			{
				costumeEntity_ResetStoredCostume(pOwnerEnt);
				entity_SetDirtyBit(pOwnerEnt, parse_PlayerCostumeData, &pOwnerEnt->pSaved->costumeData, false);
			}
			else
			{
				costumeEntity_ResetStoredCostume(e);
				entity_SetDirtyBit(e, parse_PlayerCostumeData, &e->pSaved->costumeData, false);
			}
			entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, false);
			entSetActive(e);
		}

		{
			char *msg = NULL;
			estrStackCreate(&msg);
			if (pOwnerEnt)
			{
				if (RefSystem_ReferentFromString(gMessageDict,"NameFormat_NameChanged"))
				{
					langFormatMessageKey(entGetLanguage(pOwnerEnt), &msg, "NameFormat_NameChanged", STRFMT_END);
					if (msg && *msg) ClientCmd_NotifySend(pOwnerEnt, kNotifyType_CostumeChanged, msg, NULL, NULL);
				}
			}
			else
			{
				if (RefSystem_ReferentFromString(gMessageDict,"NameFormat_NameChanged"))
				{
					langFormatMessageKey(entGetLanguage(e), &msg, "NameFormat_NameChanged", STRFMT_END);
					ClientCmd_NotifySend(e, kNotifyType_CostumeChanged, msg, NULL, NULL);
				}
			}
			estrDestroy(&msg);
		}
	}
	if ( pData->oldName != NULL )
	{
		StructFreeString(pData->oldName);
	}
	free(pData);
}

static void entitytransaction_RenameCallback( TransactionReturnVal *pReturn, void *pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* e = entFromEntityRefAnyPartition(((RenamePetData*)pData)->entRef);
		if (e) {
			costumeEntity_ResetStoredCostume(e);
			entity_SetDirtyBit(e, parse_SavedEntityData, e->pSaved, false);
		}

		{
			char *msg = NULL;
			estrStackCreate(&msg);
			if (RefSystem_ReferentFromString(gMessageDict,"NameFormat_NameChanged"))
			{
				langFormatMessageKey(entGetLanguage(e), &msg, "NameFormat_NameChanged", STRFMT_END);
				ClientCmd_NotifySend(e, kNotifyType_CostumeChanged, msg, NULL, NULL);
			}
			estrDestroy(&msg);
		}
	}
	free(pData);
}

void entitytransaction_RenamePet(Entity* e, Entity *ePet, const char *pcNewName, bool bUpdateOwnerCostumes, bool bGetCostAndDiscountFromOwner)
{
	TransactionReturnVal *pReturn;
	RenamePetData *pData;
	ItemChangeReason reason = {0};

	if (e == ePet) return;

	pData = (RenamePetData *)calloc(1, sizeof(RenamePetData));
	pData->entRef = entGetRef(ePet);
	if (!pData->entRef)
	{
		pData->entRef = entGetRef(e);
		pData->eType = ePet->myEntityType;
		pData->iID = ePet->myContainerID;
	}
	if ( ( ePet != NULL ) && ( ePet->pSaved != NULL ) && ( ePet->pSaved->savedName != NULL ) )
	{
		pData->oldName = StructAllocString(ePet->pSaved->savedName);
	}
	pData->bUpdateOwnerCostumes = bUpdateOwnerCostumes;
	inv_FillItemChangeReason(&reason, e, "Pet:Rename", ePet->debugName);

	pReturn = objCreateManagedReturnVal(entitytransaction_RenamePetCallback, pData);
	AutoTrans_entity_tr_RenamePet(	pReturn, GetAppGlobalType(), 
									entGetType(e), entGetContainerID(e), 
									entGetType(ePet), entGetContainerID(ePet),
									pcNewName, bUpdateOwnerCostumes, bGetCostAndDiscountFromOwner, &reason, GLOBALTYPE_GAMEACCOUNTDATA, e->pPlayer->accountID);
}

void entitytransaction_SubRenamePet(Entity* e, Entity *ePet, const char *pcNewSubName, bool bUpdateOwnerCostumes, bool bGetCostAndDiscountFromOwner)
{
	TransactionReturnVal *pReturn;
	RenamePetData *pData;
	ItemChangeReason reason = {0};

	if (e == ePet) return;

	pData = (RenamePetData *)calloc(1, sizeof(RenamePetData));
	pData->entRef = entGetRef(ePet);
	if (!pData->entRef)
	{
		pData->entRef = entGetRef(e);
		pData->eType = ePet->myEntityType;
		pData->iID = ePet->myContainerID;
	}
	pData->bUpdateOwnerCostumes = bUpdateOwnerCostumes;
	inv_FillItemChangeReason(&reason, e, "Pet:SubRename", ePet->debugName);

	pReturn = objCreateManagedReturnVal(entitytransaction_RenamePetCallback, pData);
	AutoTrans_entity_tr_SubRenamePet(	pReturn, GetAppGlobalType(), 
										entGetType(e), entGetContainerID(e), 
										entGetType(ePet), entGetContainerID(ePet), 
										pcNewSubName, bUpdateOwnerCostumes, bGetCostAndDiscountFromOwner, &reason, GLOBALTYPE_GAMEACCOUNTDATA, e->pPlayer->accountID);
}

void entitytransaction_SubRename(Entity* e, const char *pcNewSubName, bool bUpdateCostumes)
{
	TransactionReturnVal *pReturn;
	RenamePetData *pData;
	ItemChangeReason reason = {0};

	pData = (RenamePetData *)calloc(1, sizeof(RenamePetData));
	pData->entRef = entGetRef(e);
	inv_FillItemChangeReason(&reason, e, "Player:SubRename", NULL);

	pReturn = objCreateManagedReturnVal(entitytransaction_RenameCallback, pData);
	AutoTrans_entity_tr_SubRename(	pReturn, GetAppGlobalType(), 
		entGetType(e), entGetContainerID(e), 
		pcNewSubName, bUpdateCostumes, &reason, GLOBALTYPE_GAMEACCOUNTDATA, e->pPlayer->accountID);
}

static void RenamePetInternal(Entity* e, ContainerID iPetID, const char *pcOldName, const char *pcNewName)
{
	int i, j, num, count = 0;
	int iPetArraySize;
	Entity *eMyPet = NULL;
	char *pEStringError = NULL;
	int strerr;
	int iPartitionIdx;

	if ((!e) || (!e->pSaved)) return;
	iPetArraySize = eaSize(&e->pSaved->ppOwnedContainers);

	if ( (!iPetID) && (!pcOldName) ) {
		//gslSendPrintf(e, "No Name or ID Entered");
		return;
	}

	if ( !pcNewName ) {
		//gslSendPrintf(e, "No New Name Entered");
		return;
	}

	if (strlen(pcNewName) >= MAX_NAME_LEN)
	{
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_TooLong", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	strerr = StringIsInvalidCommonName(pcNewName, entGetAccessLevel(e));
	if (strerr)
	{
		estrStackCreate(&pEStringError);
		entStringCreateNameError( e, &pEStringError, strerr );
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if (pcOldName && !stricmp(pcOldName,pcNewName))
	{
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_DuplicatePetName", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	iPartitionIdx = entGetPartitionIdx(e);
	for ( i = 0; i < iPetArraySize; i++ )
	{
		PetRelationship* pPet = e->pSaved->ppOwnedContainers[i];
		Entity* ePet = pPet ? SavedPet_GetEntity(iPartitionIdx, pPet) : NULL;

		if ( ePet==NULL ) continue;

		if ((!entGetLocalName(ePet)) || !strcmp(pcNewName,entGetLocalName(ePet)))
		{
			estrStackCreate(&pEStringError);
			langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_DuplicatePetName", STRFMT_END);
			ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
			estrDestroy(&pEStringError);
			return;
		}

		if (pcOldName && !strcmp(pcOldName,entGetLocalName(ePet)))
		{
			if (++count > 1)
			{
				estrStackCreate(&pEStringError);
				langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_DuplicatePetName", STRFMT_END);
				ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
				estrDestroy(&pEStringError);
				return;
			}
		}

		if (!eMyPet)
		{
			if (e->pSaved->pPuppetMaster)
			{
				num = eaSize( &e->pSaved->pPuppetMaster->ppPuppets );
				for ( j = 0; j < num; j++ )
				{
					PuppetEntity* pPuppet = e->pSaved->pPuppetMaster->ppPuppets[j];

					if ( pPuppet && (pPuppet->curID == ePet->myContainerID) && (pPuppet->curType == ePet->myEntityType) )
					{
						break;
					}
				}
			}
			else
			{
				j = 0; num = 0;
			}

			if (j >= num)
			{
				if (pcOldName)
				{
					if (!strcmp(pcOldName,entGetLocalName(ePet)))
					{
						eMyPet = ePet;
					}
				}
				else
				{
					if (ePet->myContainerID == iPetID && ePet->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
					{
						eMyPet = ePet;
					}
				}
			}
		}

	}

	if ( (!eMyPet) ) {
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_PetNameNotFound", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	// If the pet is being traded, don't allow name changes
	if ( trade_IsPetBeingTraded(eMyPet, e) ) {
		estrStackCreate(&pEStringError);
		entFormatGameMessageKey(e, &pEStringError, "NameFormat_PetInTrade", STRFMT_TARGET(eMyPet), STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if ( !Entity_CanRenamePet(e,eMyPet) ) {
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_CantAfford", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	entitytransaction_RenamePet(e, eMyPet, pcNewName, false, false);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void RenamePetByID(Entity* e, int iPetID, const char *pcNewName)
{
	RenamePetInternal(e, iPetID, NULL, pcNewName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0);
void RenamePet(Entity* e, const char *pcOldName, const char *pcNewName)
{
	RenamePetInternal(e, 0, pcOldName, pcNewName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void RenameFormal(Entity* e, const char *pcNewFormalName)
{
	char *pEStringError = NULL;

	if ( !pcNewFormalName ) {
		//gslSendPrintf(e, "No New Registry Entered");
		return;
	}

	if (strlen(pcNewFormalName) >= MAX_NAME_LEN)
	{
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_TooLong", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if (!savedpet_ValidateFormalName(e, pcNewFormalName, &pEStringError))
	{
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if ( !Entity_CanChangeSubName(e) ) {
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_CantAfford", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if (stricmp(pcNewFormalName, "NONE"))
	{
		entitytransaction_SubRename(e, pcNewFormalName, false);
	}
	else
	{
		entitytransaction_SubRename(e, NULL, false);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0);
void RenamePetFormal(Entity* e, const char *pcName, const char *pcNewFormalName)
{
	int i, j, num;
	int iPetArraySize;
	Entity *eMyPet = NULL;
	char *pEStringError = NULL;
	int iPartitionIdx;

	if ((!e) || (!e->pSaved)) return;
	iPetArraySize = eaSize(&e->pSaved->ppOwnedContainers);

	if ( !pcName ) {
		//gslSendPrintf(e, "No Name Entered");
		return;
	}

	if ( !pcNewFormalName ) {
		//gslSendPrintf(e, "No New Name Entered");
		return;
	}

	if (strlen(pcNewFormalName) >= MAX_NAME_LEN)
	{
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_TooLong", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if (!savedpet_ValidateFormalName(e, pcNewFormalName, &pEStringError))
	{
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	iPartitionIdx = entGetPartitionIdx(e);
	for ( i = 0; i < iPetArraySize; i++ )
	{
		PetRelationship* pPet = e->pSaved->ppOwnedContainers[i];
		Entity* ePet = pPet ? SavedPet_GetEntity(iPartitionIdx, pPet) : NULL;

		if ( ePet==NULL ) continue;

		if (e->pSaved->pPuppetMaster)
		{
			num = eaSize( &e->pSaved->pPuppetMaster->ppPuppets );
			for ( j = 0; j < num; j++ )
			{
				PuppetEntity* pPuppet = e->pSaved->pPuppetMaster->ppPuppets[j];

				if ( pPuppet && (pPuppet->curID == ePet->myContainerID) && (pPuppet->curType == ePet->myEntityType) )
				{
					break;
				}
			}
		}
		else
		{
			j = 0; num = 0;
		}

		if (j >= num)
		{
			if (!strcmp(pcName,entGetLocalName(ePet)))
			{
				eMyPet = ePet;
				break;
			}
		}
	}

	if ( !eMyPet ) {
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_PetNameNotFound", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	// If the pet is being traded, don't allow name changes
	if ( trade_IsPetBeingTraded(eMyPet, e) ) {
		estrStackCreate(&pEStringError);
		entFormatGameMessageKey(e, &pEStringError, "NameFormat_PetInTrade", STRFMT_TARGET(eMyPet), STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if ( !Entity_CanChangeSubNameOnPet(e,eMyPet) ) {
		estrStackCreate(&pEStringError);
		langFormatMessageKey(entGetLanguage(e), &pEStringError, "NameFormat_CantAfford", STRFMT_END);
		ClientCmd_NotifySend(e, kNotifyType_NameInvalid, pEStringError, NULL, NULL);
		estrDestroy(&pEStringError);
		return;
	}

	if (stricmp(pcNewFormalName, "NONE"))
	{
		entitytransaction_SubRenamePet(e, eMyPet, pcNewFormalName, false, false);
	}
	else
	{
		entitytransaction_SubRenamePet(e, eMyPet, NULL, false, false);
	}
}


// EntityBuild functions

AUTO_TRANS_HELPER
ATR_LOCKS(e, ".Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2, .Psaved.Ppbuilds, .Pplayer.Skilltype, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, .Pplayer.Playertype");
void entity_FixupBuilds(ATR_ARGS, ATH_ARG NOCONST(Entity) *e)
{
	S32 iBuildIdx;
	for(iBuildIdx = eaSize(&e->pSaved->ppBuilds)-1; iBuildIdx >= 0; iBuildIdx--)
	{
		NOCONST(EntityBuild) *pBuild = eaGet(&e->pSaved->ppBuilds, iBuildIdx);

		if(!eaSize(&pBuild->ppItems))
		{
			inv_ent_trh_BuildFill(ATR_PASS_ARGS,e,false,pBuild);
		}
		else
		{
			S32 iItemIdx;
			for(iItemIdx = eaSize(&pBuild->ppItems)-1; iItemIdx >= 0; iItemIdx--)
			{
				NOCONST(EntityBuildItem) *pBuildItem = eaGet(&pBuild->ppItems, iItemIdx);
				if(!pBuildItem)
				{
					eaRemove(&pBuild->ppItems, iItemIdx);
				}
				else
				{
					NOCONST(InventoryBag) *pBag = e->pInventoryV2 ? eaIndexedGetUsingInt(&e->pInventoryV2->ppInventoryBags, pBuildItem->eBagID) : NULL;
					NOCONST(Item) *pItem = inv_trh_GetItemByID(ATR_EMPTY_ARGS, e, pBuildItem->ulItemID, NULL, NULL, InvGetFlag_NoBuyBackBag);
					ItemDef *pItemDef = NULL;
					
					if(NONNULL(pItem))
						pItemDef = GET_REF(pItem->hItem);
					
					//If no bag or the bag doesn't match the flag, baleeted!
					if(!pBag || !(invbag_trh_flags(pBag) & (InvBagFlag_EquipBag | InvBagFlag_DeviceBag | InvBagFlag_ActiveWeaponBag)))
					{
						StructDestroyNoConst(parse_EntityBuildItem, eaRemove(&pBuild->ppItems, iItemIdx));
						continue;
					}

					//Or the item has been deleted or its def is invalid
					else if(pBuildItem->ulItemID && (ISNULL(pItem) || ISNULL(pItemDef)))
					{
						pBuildItem->ulItemID = 0;
						continue;
					}
					
					if(ISNULL(pItem) || ISNULL(pItemDef))
						continue;

					//TODO(MK): Check "equip limit" for builds?
					if(!inv_trh_ItemEquipValid(ATR_PASS_ARGS, e, e, pItem, pBag, pBuildItem->iSlot))
					{
						pBuildItem->ulItemID = 0;
					}
				}
			}
		}
	}
}

typedef struct EntityBuildSetData
{
	EntityRef er;
	U32 uiIndexBuildOld;
	U32 uiIndexBuildNew;
	const char *pchClass;
	U8 chCostumeIndex;
	CritterFaction *pFaction;
	S32 bSkipPlayerPower;
} EntityBuildSetData;

static EntityBuildSetData *CreateEntityBuildSetData(SA_PARAM_NN_VALID Entity* e)
{
	EntityBuildSetData *pdata = malloc(sizeof(EntityBuildSetData));

	// Fill in the data
	pdata->er = entGetRef(e);
	pdata->uiIndexBuildOld = e->pSaved->uiIndexBuild;
	pdata->pchClass = e->pChar ? REF_STRING_FROM_HANDLE(e->pChar->hClass) : NULL;
	pdata->chCostumeIndex = e->pSaved->costumeData.iActiveCostume;
	pdata->pFaction = GET_REF(e->hFaction);

	return pdata;
}

// Callback when build is set/created, to forcibly set the powerslot set (which maybe should e transacted instead)
static void EntityBuildSetCallback(TransactionReturnVal *returnVal, void *userData)
{
	EntityBuildSetData *pdata = (EntityBuildSetData*)userData;
	EntityRef er = pdata->er;
	Entity* e = entFromEntityRefAnyPartition(er);

	if(e)
	{
		if(returnVal->eOutcome==TRANSACTION_OUTCOME_SUCCESS)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			CharacterClass *pClass = RefSystem_ReferentFromString(g_hCharacterClassDict, pdata->pchClass);
			EntityBuildSlotDef* pBuildSlotDef = entity_BuildGetSlotDef(e->pSaved->uiIndexBuild);

			if (pClass)
			{
				char *pch = NULL;
				entFormatGameMessageKey(e, &pch, "Builds_ChangedSuccesfully",
					STRFMT_ENTITY(e),
					STRFMT_DISPLAYMESSAGE("Role.Name", pClass->msgDisplayName),
					STRFMT_END);
				notify_NotifySend(e, kNotifyType_BuildChanged, pch, pClass->pchName, NULL);
				estrDestroy(&pch);
			}
			if(e->pChar && e->pSaved)
			{
				character_PowerSlotSetCurrent(e->pChar,e->pSaved->uiIndexBuild);
				character_PowerSlotsValidate(e->pChar);
			}

			// If CharacterClass changed
			if(e->pChar && pdata->pchClass!=REF_STRING_FROM_HANDLE(e->pChar->hClass))
			{
				character_SetClassCallback(e,pExtract);
			}
			else if (e->pChar && e->pSaved)
			{
				character_ResetPowersArray(entGetPartitionIdx(e), e->pChar, pExtract);
			}

			// If costume changed
			if(e->pSaved)
			{
				U8 chCostumeIndex = e->pSaved->costumeData.iActiveCostume;
				
				if(chCostumeIndex != pdata->chCostumeIndex)
				{
					if (!pdata->bSkipPlayerPower && pBuildSlotDef && pBuildSlotDef->pchTransformationDef)
					{
						Transformation_SetTransformation(e, pBuildSlotDef->pchTransformationDef);
					}
					
					costumeEntity_ResetCostumeData(e);
				}
			}

			// if factions changed
			if (pdata->pFaction != GET_REF(e->hFaction))
			{
				aiCivilianUpdateIsHostile(e);
			}

			if (!pdata->bSkipPlayerPower)
			{
				PowerDef *pPowerDef;

				if (pBuildSlotDef && pBuildSlotDef->pchBuildChangedPower)
				{
					pPowerDef = powerdef_Find(pBuildSlotDef->pchBuildChangedPower);
				}
				else
				{
					pPowerDef = powerdef_Find("Build_Changed_Power");
				}

					
				if(pPowerDef && e->pChar)
				{
					character_ApplyUnownedPowerDefToSelf(entGetPartitionIdx(e),e->pChar,pPowerDef,pExtract);
				}
			}
		}
	}

	free(pdata);
}


// Sets the Entity's build
AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds, .Psaved.Uiindexbuild, .Psaved.Costumedata.Iactivecostume, .Psaved.Uitimestampbuildset, .Hfaction, .Pchar.Hclass, .Pchar.Pppowersclass, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Uivalidatetag, .Psaved.Uitimestampcostumeset, .Egender, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Pplayer.Playertype");
enumTransactionOutcome trEntity_BuildSetCurrent(ATR_ARGS, NOCONST(Entity)* e, BuildSetCurrentParam *pParam, SetActiveCostumeParam *pCostumeParam, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(EntityBuild) *pBuild;
	const char *pchClass;
	EntityBuildSlotDef* pBuildSlotDef = entity_BuildGetSlotDef(pParam->uiBuildIdx);

	// Check validate tag
	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Flag the old build as being swapped out
	if (pParam->bMarkOld)
	{
		e->pSaved->ppBuilds[e->pSaved->uiIndexBuild]->bSwappingOut = true;
	}

	pBuild = e->pSaved->ppBuilds[pParam->uiBuildIdx];

	// Set the class
	pchClass = REF_STRING_FROM_HANDLE(pBuild->hClass);
	if (pchClass)
	{
		trCharacter_SetClass(ATR_RECURSE, e, pchClass, false);
	}
	
	// Set the costume
	if ((!pBuildSlotDef || (pBuildSlotDef && !pBuildSlotDef->bIgnoreCostumeSwap)) && 
		(pBuild->chCostume != e->pSaved->costumeData.iActiveCostume)) {
		
		if (TRANSACTION_OUTCOME_FAILURE == costume_trh_SetPlayerActiveCostume(ATR_PASS_ARGS, e, pCostumeParam)) {
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}
	
	// Swap in the items
	if (!inv_ent_trh_BuildSwap(ATR_PASS_ARGS, e, false, pBuild, pReason, pExtract)) {
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, INVENTORY_FULL_MSG, kNotifyType_InventoryFull);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	//ONLY SUCCESSFUL STUFF AFTER THIS COMMENT

	if (IS_HANDLE_ACTIVE(pBuild->hFaction) && !REF_COMPARE_HANDLES(pBuild->hFaction, e->hFaction)) {
		COPY_HANDLE(e->hFaction, pBuild->hFaction);
	}

	// No longer swapping out the old build
	if (pParam->bMarkOld)
	{
		e->pSaved->ppBuilds[e->pSaved->uiIndexBuild]->bSwappingOut = false;
	}

	// Set the index
	e->pSaved->uiIndexBuild = pParam->uiBuildIdx;

	// Set the timestamp
	e->pSaved->uiTimestampBuildSet = timeSecondsSince2000();

	// Update the validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


bool entity_InitBuildSetCurrentParam(BuildSetCurrentParam *pParam, Entity *e, U32 uiIndex, S32 bSetCheck, S32 bSkipPlayerPower)
{
	// Range check
	if (uiIndex < 0 || (uiIndex >= eaUSize(&e->pSaved->ppBuilds)))
	{
		return false;
	}
	// Can't set to same as current
	if (uiIndex == e->pSaved->uiIndexBuild)
	{
		return false;
	}
	if (!e->pSaved->ppBuilds[uiIndex])
	{
		return false;
	}
	if (entGetAccountID(e) <= 0)
	{
		return false;
	}
	if (bSetCheck && !entity_BuildCanSet(e, uiIndex))
	{
		return false;
	}

	// Set the param
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;
	pParam->uiBuildIdx = uiIndex;
	pParam->bSetCheck = bSetCheck;
	pParam->bSkipPlayerPower = bSkipPlayerPower;

	if (e->pSaved->uiIndexBuild <(U32)eaSize(&e->pSaved->ppBuilds) && e->pSaved->ppBuilds[e->pSaved->uiIndexBuild] != NULL)
	{
		pParam->bMarkOld = true;
	}

	return true;
}


// Wrapper for trEntity_BuildSetCurrent
void entity_BuildSetCurrentEx(Entity* e, U32 uiIndex, S32 bSetCheck, S32 bSkipPlayerPower)
{
	if(e && e->pSaved)
	{
		BuildSetCurrentParam param = {0};
		SetActiveCostumeParam costumeParam = {0};
		EntityBuild *pBuild;

		pBuild = eaGet(&e->pSaved->ppBuilds,uiIndex);
		if (pBuild && 
			entity_InitBuildSetCurrentParam(&param, e, uiIndex, bSetCheck, bSkipPlayerPower) &&
			costumetransaction_InitSetActiveCostumeParam(&costumeParam, e, pBuild->chCostumeType, pBuild->chCostume))
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			TransactionReturnVal *pReturn;
			EntityBuildSetData *pdata;
			ItemChangeReason reason = {0};

			pdata = CreateEntityBuildSetData(e);
			pdata->uiIndexBuildNew = uiIndex;
			pdata->bSkipPlayerPower = bSkipPlayerPower;
			pReturn = LoggedTransactions_CreateManagedReturnValEnt("", e, EntityBuildSetCallback, pdata);
			inv_FillItemChangeReason(&reason, e, "Build:SetCurrent", NULL);

			AutoTrans_trEntity_BuildSetCurrent(pReturn, GetAppGlobalType(),
				entGetType(e),entGetContainerID(e), 
				&param, &costumeParam, &reason, pExtract);
		}
		else
		{
			char *estrBuffer = NULL;
			S32 iTimeRemaining = entity_BuildTimeToWait(e, uiIndex);

			estrStackCreate(&estrBuffer);

			entFormatGameMessageKey(e, &estrBuffer, "Builds_SetCurrentFailed",
				STRFMT_INT("TimeRemaining", iTimeRemaining),
				STRFMT_ENTITY(e),
				STRFMT_END);

			notify_NotifySend(e, kNotifyType_Failed, estrBuffer, NULL, NULL);

			estrDestroy(&estrBuffer);
		}
	}
}


//Copies the current build into iDestIndex
AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds, .Psaved.Uiindexbuild, .Pchar.Pslots.Ppsets");
enumTransactionOutcome trEntity_BuildCopyCurrent(ATR_ARGS, NOCONST(Entity)* e, BuildCopyCurrentParam *pParam)
{
	NOCONST(EntityBuild) *pSrcBuild;
	NOCONST(EntityBuild) *pDestBuild;

	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pDestBuild = e->pSaved->ppBuilds[pParam->uiDestIndex];
	pSrcBuild = e->pSaved->ppBuilds[e->pSaved->uiIndexBuild];

	//If you want items, I'll give you items
	if (pParam->bCopyItems)
	{
		eaCopyStructsNoConst(&pSrcBuild->ppItems, &pDestBuild->ppItems, parse_EntityBuildItem);
	}

	//if you want powers, I give you da powers
	if (pParam->bCopyPowers)
	{
		// Powers data is validated in the transaction because it is not protected by the build validate tag
		if(NONNULL(e->pChar) && NONNULL(e->pChar->pSlots))
		{
			U32 iSlotsSize = eaUSize(&e->pChar->pSlots->ppSets);
			if (pParam->uiDestIndex >= iSlotsSize || e->pSaved->uiIndexBuild >= iSlotsSize)
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
			else if(ISNULL(e->pChar->pSlots->ppSets[e->pSaved->uiIndexBuild]))
			{
				TRANSACTION_RETURN_LOG_FAILURE(
					"Couldn't copy powers from build [%d] because it was NULL.",
					e->pSaved->uiIndexBuild);
				return TRANSACTION_OUTCOME_FAILURE;
			}
			else if(ISNULL(e->pChar->pSlots->ppSets[pParam->uiDestIndex]))
			{
				TRANSACTION_RETURN_LOG_FAILURE(
					"Couldn't copy powers from build [%d] to build [%d] because the destination set was NULL.",
					e->pSaved->uiIndexBuild,
					pParam->uiDestIndex);
				return TRANSACTION_OUTCOME_FAILURE;
			}

			ea32Copy(	&e->pChar->pSlots->ppSets[pParam->uiDestIndex]->puiPowerIDs,
						&e->pChar->pSlots->ppSets[e->pSaved->uiIndexBuild]->puiPowerIDs );
		}
	}

	//Copy the class
	if (pParam->bCopyClass)
	{
		if(IS_HANDLE_ACTIVE(pSrcBuild->hClass))
		{
			COPY_HANDLE(pDestBuild->hClass,pSrcBuild->hClass);
		}
	}

	// Copy the costume and costume type
	if (pParam->bCopyCostume)
	{
		pDestBuild->chCostumeType = pSrcBuild->chCostumeType;
		pDestBuild->chCostume = pSrcBuild->chCostume;
	}

	// Update the validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


bool entity_InitBuildCopyCurrentParam(BuildCopyCurrentParam *pParam, Entity *e, U32 uiDestIndex, S32 bCopyClass, S32 bCopyCostume, S32 bCopyItems, S32 bCopyPowers)
{
	// Range check
	if (uiDestIndex < 0 || (uiDestIndex >= eaUSize(&e->pSaved->ppBuilds)))
	{
		return false;
	}
	// Can't copy over self
	if (uiDestIndex == e->pSaved->uiIndexBuild)
	{
		return false;
	}
	if (!e->pSaved->ppBuilds[uiDestIndex])
	{
		return false;
	}

	// Set up param
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;
	pParam->uiDestIndex = uiDestIndex;
	pParam->bCopyClass = bCopyClass;
	pParam->bCopyCostume = bCopyCostume;
	pParam->bCopyItems = bCopyItems;
	pParam->bCopyPowers = bCopyPowers;

	return true;
}


void entity_BuildCopyCurrent(Entity* e, U32 uiDestIndex, S32 bCopyClass, S32 bCopyCostume, S32 bCopyItems, S32 bCopyPowers)
{
	BuildCopyCurrentParam param = {0};

	if(e->pSaved && entity_InitBuildCopyCurrentParam(&param, e, uiDestIndex, bCopyClass, bCopyCostume, bCopyItems, bCopyPowers))
	{
		TransactionReturnVal *pVal = LoggedTransactions_CreateManagedReturnValEnt("Build_CopyCurrent", e, NULL, NULL);
		AutoTrans_trEntity_BuildCopyCurrent(pVal, GetAppGlobalType(),
			entGetType(e),entGetContainerID(e), &param);
	}
}


// Creates a new EntityBuild based on the Entity's current state, makes that build current.
AUTO_TRANSACTION
ATR_LOCKS(e, "pInventoryV2.ppLiteBags[], .Pchar.Hclass, .Psaved.Uibuildvalidatetag, .Psaved.Costumedata.Iactivecostume, .Psaved.Uiindexbuild, .Pchar.Pslots.Ppsets, .Psaved.Uitimestampbuildset, .Psaved.Ppbuilds, .pInventoryV2.Ppinventorybags, .Pplayer.Playertype");
enumTransactionOutcome trEntity_BuildCreate(ATR_ARGS, NOCONST(Entity)* e, BuildCreateParam *pParam)
{
	int s = eaSize(&e->pSaved->ppBuilds);
	NOCONST(EntityBuild) *pBuild;
	char *pBuildName = NULL;

	// Legal check
	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	pBuild = StructAllocNoConst(parse_EntityBuild);

	// Save the class
	if(IS_HANDLE_ACTIVE(e->pChar->hClass))
	{
		COPY_HANDLE(pBuild->hClass,e->pChar->hClass);
	}

	// Save the costume index
	pBuild->chCostume = e->pSaved->costumeData.iActiveCostume;

	// Save the items
	inv_ent_trh_BuildFill(ATR_PASS_ARGS, e, false, pBuild);

	// Copy the PowerSlots from the current Build if possible, but at least make sure PowerSlot stuff is allocated
	if (NONNULL(e->pChar->pSlots))
	{
		int iCopyFrom = e->pSaved->uiIndexBuild < (U32)s ? e->pSaved->uiIndexBuild : 0;

		if(s >= eaSize(&e->pChar->pSlots->ppSets))
		{
			// Persisted data cannot consist of sparse earrays -BZ
			while(s >= eaSize(&e->pChar->pSlots->ppSets))
			{
				eaPush(&e->pChar->pSlots->ppSets, StructCreateNoConst(parse_PowerSlotSet));
			}
		}

		ea32Copy(&e->pChar->pSlots->ppSets[s]->puiPowerIDs,&e->pChar->pSlots->ppSets[iCopyFrom]->puiPowerIDs);
	}

	eaPush(&e->pSaved->ppBuilds,pBuild);

	// Set the index
	if(e->pSaved->uiIndexBuild > (U32)s)
	{
		e->pSaved->uiIndexBuild = s;
	}

	// Set the timestamp
	e->pSaved->uiTimestampBuildSet = timeSecondsSince2000();

	// Increment the validate tag
	++e->pSaved->uiBuildValidateTag;
	
	// Give the build a default name
	estrStackCreate(&pBuildName); 
	FormatMessageKey(&pBuildName, "DefaultBuild", STRFMT_INT("Index", s+1), STRFMT_END);
	strncpy(pBuild->achName, pBuildName, MAX_NAME_LEN_ENTITYBUILD-1);
	estrDestroy(&pBuildName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

bool entity_InitBuildCreateParam(BuildCreateParam *pParam, Entity *e)
{
	// Legal check
	if (!entity_BuildCanCreate(CONTAINER_NOCONST(Entity, e)))
	{
		return false;
	}

	// Set up param data
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;

	return true;
}

// Wrapper for trEntity_BuildCreate
void entity_BuildCreate(SA_PARAM_NN_VALID Entity* e)
{
	BuildCreateParam param = {0};

	if(e->pSaved && entity_InitBuildCreateParam(&param, e))
	{
		TransactionReturnVal *pReturn;
		EntityBuildSetData *pdata = CreateEntityBuildSetData(e);
		pReturn = objCreateManagedReturnVal(EntityBuildSetCallback,pdata);
		AutoTrans_trEntity_BuildCreate(pReturn, GetAppGlobalType(),
			entGetType(e), entGetContainerID(e), &param);
	}
}


AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds[AO], .Psaved.Uiindexbuild");
enumTransactionOutcome trEntity_BuildDeleteSimple(ATR_ARGS, NOCONST(Entity)* e, BuildDeleteParam *pParam)
{
	NOCONST(EntityBuild) *pBuild;

	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Delete the build
	pBuild = eaRemove(&e->pSaved->ppBuilds, pParam->uiBuildIdx);
	if(pBuild)
	{
		StructDestroyNoConst(parse_EntityBuild, pBuild);
	}

	// Reset the index if affected
	if ((pParam->uiBuildIdx > 0) && (pParam->uiBuildIdx <= e->pSaved->uiIndexBuild))
	{
		e->pSaved->uiIndexBuild--;
	}

	// Increment validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Psaved.Uibuildvalidatetag, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Costumedata.Iactivecostume, .Psaved.Uitimestampbuildset, .Hfaction, .Pchar.Hclass, .Pchar.Pppowersclass, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Uivalidatetag, .Psaved.Uitimestampcostumeset, .Egender, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Pplayer.Playertype");
enumTransactionOutcome trEntity_BuildDeleteAndSet(ATR_ARGS, NOCONST(Entity)* e, BuildDeleteParam *pParam, BuildSetCurrentParam *pBuildSetParam, SetActiveCostumeParam *pCostumeParam, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(EntityBuild) *pBuild;

	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Delete the build
	pBuild = eaRemove(&e->pSaved->ppBuilds, pParam->uiBuildIdx);
	if(pBuild)
	{
		StructDestroyNoConst(parse_EntityBuild, pBuild);
	}

	// Reset the index if possible
	e->pSaved->uiIndexBuild = pBuildSetParam->uiBuildIdx;

	// Set to the new build
	if (trEntity_BuildSetCurrent(ATR_RECURSE, e, pBuildSetParam, pCostumeParam, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Increment validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


bool entity_InitBuildDeleteParam(BuildDeleteParam *pParam, Entity *e, U32 uiBuildIdx, int bSetCheck)
{
	int s = eaSize(&e->pSaved->ppBuilds);

	// Range check
	if(  (s < 2) ||
		 (uiBuildIdx >= (U32)s) ||
		 (e->pSaved->ppBuilds[uiBuildIdx] == NULL) )
	{
		return false;
	}

	if (uiBuildIdx == e->pSaved->uiIndexBuild)
	{
		//Cannot delete the current build while it's on cooldown.
		if (bSetCheck && entity_BuildCanSet(e, uiBuildIdx))
		{
			return false;
		}
	}

	// Set up data
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;
	pParam->uiBuildIdx = uiBuildIdx;

	return true;
}


// Wrapper for trEntity_BuildDelete
void entity_BuildDelete(SA_PARAM_NN_VALID Entity* e, U32 uiBuildIdx, S32 bSetCheck)
{
	if( e && e->pSaved && entGetAccountID(e))
	{
		BuildDeleteParam buildParam = {0};

		// Do validation
		if (!entity_InitBuildDeleteParam(&buildParam, e, uiBuildIdx, bSetCheck))
		{
			return;
		}

		if (e->pSaved->uiIndexBuild == uiBuildIdx)
		{
			BuildSetCurrentParam buildSetParam = {0};
			SetActiveCostumeParam costumeParam = {0};

			// End up here if deleting the currently in use build
			EntityBuild *pBuild = e->pSaved->ppBuilds[uiBuildIdx];
			U32 uiNewBuildIdx = (uiBuildIdx > 0 ? (uiBuildIdx - 1) : 0);

			if (costumetransaction_InitSetActiveCostumeParam(&costumeParam, e, pBuild->chCostumeType, pBuild->chCostume) &&
				entity_InitBuildSetCurrentParam(&buildSetParam, e, uiNewBuildIdx, true, false))
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
				TransactionReturnVal *pReturn;
				EntityBuildSetData *pdata;
				ItemChangeReason reason = {0};

				pdata = CreateEntityBuildSetData(e);
				pReturn = objCreateManagedReturnVal(EntityBuildSetCallback,pdata);
				inv_FillItemChangeReason(&reason, e, "Build:Delete", NULL);
				AutoTrans_trEntity_BuildDeleteAndSet(pReturn, GetAppGlobalType(),
					entGetType(e),entGetContainerID(e), 
					&buildParam, &buildSetParam, &costumeParam, &reason, pExtract);
			}
			else
			{
				char *estrBuffer = NULL;
				S32 iTimeRemaining = entity_BuildTimeToWait(e, uiBuildIdx);

				estrStackCreate(&estrBuffer);

				entFormatGameMessageKey(e, &estrBuffer, "Builds_DeleteCurrentFailed",
					STRFMT_INT("TimeRemaining", iTimeRemaining),
					STRFMT_ENTITY(e),
					STRFMT_END);

				notify_NotifySend(e, kNotifyType_Failed, estrBuffer, NULL, NULL);

				estrDestroy(&estrBuffer);
			}
		}
		else
		{
			// End up here if are deleting a build that isn't in use
			AutoTrans_trEntity_BuildDeleteSimple(NULL, GetAppGlobalType(), 
				entGetType(e), entGetContainerID(e), 
				&buildParam);
		}
	}

}


// Creates a new EntityBuild based on the Entity's current state, makes that build current.
AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds");
enumTransactionOutcome trEntity_BuildSetName(ATR_ARGS, NOCONST(Entity)* e, BuildSetNameParam *pParam)
{
	if(e->pSaved->uiBuildValidateTag != pParam->uiValidateTag || !pParam->pchName)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Set the name
	strcpy(e->pSaved->ppBuilds[pParam->uiBuildIdx]->achName, pParam->pchName);

	// Increment the validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


bool entity_InitBuildSetNameParam(BuildSetNameParam *pParam, Entity *e, U32 uiIndex, const char *pchName)
{
	int s = eaSize(&e->pSaved->ppBuilds);

	// Range check
	if (uiIndex < 0 || uiIndex >= (U32)s)
	{
		return false;
	}

	if (!e->pSaved->ppBuilds[uiIndex])
	{
		return false;
	}

	// Make sure name is valid
	if (!entity_BuildNameLegal(pchName))
	{
		return false;
	}

	// Set up param
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;
	pParam->pchName = pchName;
	pParam->uiBuildIdx = uiIndex;

	return true;
}


// Wrapper for trEntity_BuildSetName
void entity_BuildSetName(Entity* e, U32 uiIndex, const char *pchName)
{
	BuildSetNameParam param = {0};

	if(e->pSaved && entity_InitBuildSetNameParam(&param, e, uiIndex, pchName))
	{
		AutoTrans_trEntity_BuildSetName(NULL,GetAppGlobalType(),entGetType(e),entGetContainerID(e),&param);
	}
}


AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds");
enumTransactionOutcome trEntity_BuildSetCostume(ATR_ARGS, NOCONST(Entity)* e, BuildSetCostumeParam *pParam)
{
	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Set the costume
	e->pSaved->ppBuilds[pParam->uiBuildIdx]->chCostumeType = (U8)pParam->eCostumeType;
	e->pSaved->ppBuilds[pParam->uiBuildIdx]->chCostume = (U8)pParam->iCostumeIdx;

	// Increment the validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


bool entity_InitBuildSetCostumeParam(BuildSetCostumeParam *pParam, Entity *e, U32 uiIndex, PCCostumeType eCostumeType, int iCostumeIdx)
{
	int s = eaSize(&e->pSaved->ppBuilds);

	// Range check
	if (uiIndex < 0 || uiIndex >= (U32)s)
	{
		return false;
	}

	if (!e->pSaved->ppBuilds[uiIndex])
	{
		return false;
	}

	// Set up param
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;
	pParam->uiBuildIdx = uiIndex;
	pParam->eCostumeType = eCostumeType;
	pParam->iCostumeIdx = iCostumeIdx;

	return true;
}


// Wrapper for trEntity_BuildSetCostume
void entity_BuildSetCostume(Entity* e, U32 uiIndex, PCCostumeType eCostumeType, int iCostumeIdx)
{
	BuildSetCostumeParam param = {0};

	if (e->pSaved && entity_InitBuildSetCostumeParam(&param, e, uiIndex, eCostumeType, iCostumeIdx))
	{
		AutoTrans_trEntity_BuildSetCostume(NULL, GetAppGlobalType(),
			entGetType(e), entGetContainerID(e), &param);
	}
}


// Sets the class of a specific build
AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds");
enumTransactionOutcome trEntity_BuildSetClass(ATR_ARGS, NOCONST(Entity)* e, BuildSetClassParam *pParam)
{
	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Set the class
	SET_HANDLE_FROM_STRING(g_hCharacterClassDict, pParam->pchClass, e->pSaved->ppBuilds[pParam->uiBuildIdx]->hClass);

	// Increment the validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


bool entity_InitBuildSetClassParam(BuildSetClassParam *pParam, Entity *e, U32 uiIndex, const char *pchClass)
{
	CharacterClass *pClassNew = RefSystem_ReferentFromString(g_hCharacterClassDict, pchClass);
	int s = eaSize(&e->pSaved->ppBuilds);

	// Range check
	if (uiIndex < 0 || uiIndex >= (U32)s)
	{
		return false;
	}

	if (!e->pSaved->ppBuilds[uiIndex])
	{
		return false;
	}

	// Make sure class is valid
	if (!pClassNew || !entity_PlayerCanBecomeClass(e, pClassNew))
	{
		return false;
	}

	// Don't change class on current build until time runs out
	if (uiIndex == e->pSaved->uiIndexBuild)
	{
		if (entity_BuildTimeToWait(e, uiIndex) > 0)
		{
			return false;
		}
	}

	// Set up param
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;
	pParam->pchClass = pchClass;
	pParam->uiBuildIdx = uiIndex;

	return true;
}


// Wrapper for trEntity_BuildSetClass
void entity_BuildSetClass(Entity* e, U32 uiIndex, const char *pchClass)
{
	BuildSetClassParam param = {0};

	if (e->pSaved && e->pChar && entity_InitBuildSetClassParam(&param, e, uiIndex, pchClass))
	{
		if (uiIndex == e->pSaved->uiIndexBuild)
		{
			// For current build, just change the class
			character_SetClass(e->pChar, pchClass);
		}
		else
		{
			// For other builds, run the transaction
			AutoTrans_trEntity_BuildSetClass(NULL, GetAppGlobalType(),
				entGetType(e),entGetContainerID(e), &param);
		}
	}
}


AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Uibuildvalidatetag, .Psaved.Ppbuilds");
enumTransactionOutcome trEntity_BuildSetItem(ATR_ARGS, NOCONST(Entity)* e, BuildSetItemParam *pParam)
{
	NOCONST(EntityBuild) *pBuild;
	int ii;
	
	if (e->pSaved->uiBuildValidateTag != pParam->uiValidateTag)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Set the item into the build
	pBuild = e->pSaved->ppBuilds[pParam->uiBuildIdx];
	for(ii=eaSize(&pBuild->ppItems)-1; ii>=0; ii--)
	{
		if ((pBuild->ppItems[ii]->eBagID == pParam->iInvBag) && (pBuild->ppItems[ii]->iSlot == pParam->iSlot))
		{
			pBuild->ppItems[ii]->ulItemID = pParam->iItemID;
			break;
		}
	}
	if(ii<0)
	{
		NOCONST(EntityBuildItem) *pBuildItem = CONTAINER_NOCONST(EntityBuildItem, StructAlloc(parse_EntityBuildItem));
		pBuildItem->eBagID = pParam->iInvBag;
		pBuildItem->iSlot = pParam->iSlot;
		pBuildItem->ulItemID = pParam->iItemID;
		eaPush(&pBuild->ppItems, pBuildItem);
	}

	// Increment the validate tag
	++e->pSaved->uiBuildValidateTag;

	return TRANSACTION_OUTCOME_SUCCESS;
}


bool entity_InitBuildSetItemParam(BuildSetItemParam *pParam, Entity *e, U32 uiIndex, int iInvBag, int iSlot, U64 iItemID, int iSrcBag, int iSrcSlot, GameAccountDataExtract *pExtract)
{
	int s = eaSize(&e->pSaved->ppBuilds);
	Item *pItem;

	// Range check
	if (uiIndex < 0 || uiIndex >= (U32)s)
	{
		return false;
	}

	if (!e->pSaved->ppBuilds[uiIndex])
	{
		return false;
	}

	// Make sure item is valid
	pItem = inv_GetItemFromBag(e, iSrcBag, iSrcSlot, pExtract);

	if(!e || !pItem)
	{
		return false;
	}
	else if(!(inv_IsGuildBag(iSrcBag) || inv_IsGuildBag(iInvBag)))
	{
		ItemDef *pDef = GET_REF(pItem->hItem);
		if(pDef)
		{
			if(e->pSaved->uiIndexBuild == uiIndex)
			{
				if (!item_ItemMoveValid(e, pDef, false, iSrcBag, iSrcSlot, false, iInvBag, iSlot, pExtract))
				{
					return false;
				}
			}
			else
			{
				if (!item_ItemMoveDestValid( e, pDef, pItem, false, iInvBag, iSlot, false, pExtract ))
				{
					return false;
				}
			}
		}
	}

	// Set up param
	pParam->uiValidateTag = e->pSaved->uiBuildValidateTag;
	pParam->uiBuildIdx = uiIndex;
	pParam->iInvBag = iInvBag;
	pParam->iSlot = iSlot;
	pParam->iItemID = iItemID;
	pParam->iSrcBag = iSrcBag;
	pParam->iSrcSlot = iSrcSlot;

	return true;
}


// Wrapper for trEntity_BuildSetItem
void entity_BuildSetItem(SA_PARAM_NN_VALID Entity* e, U32 iBuildIdx, int iInvBag, int iSlot, U64 iItemID, int iSrcBag, int iSrcSlot)
{
	BuildSetItemParam param = {0};
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);

	if (e->pSaved && entity_InitBuildSetItemParam(&param, e, iBuildIdx, iInvBag, iSlot, iItemID, iSrcBag, iSrcSlot, pExtract))
	{
		if (iBuildIdx == e->pSaved->uiIndexBuild)
		{
			Item *pDstItem = inv_GetItemFromBag(e, iInvBag, iSlot, pExtract);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, e, "Build:SetItem", NULL);

			// For current build, just move the item
			AutoTrans_inv_ent_tr_MoveItem(NULL, GetAppGlobalType(), 
				entGetType(e), entGetContainerID(e), 
				iSrcBag, iSrcSlot, iItemID, iInvBag, iSlot, SAFE_MEMBER(pDstItem,id), 1, &reason, &reason, pExtract);
		}
		else
		{
			// For other builds, run the transaction
			AutoTrans_trEntity_BuildSetItem(NULL, GetAppGlobalType(), 
				entGetType(e), entGetContainerID(e), &param);
		}
	}
}


void entGetDescription_CB(TransactionReturnVal *returnVal, void *userData)
{
	EntityRef *entRef = (EntityRef*)userData;
	ContainerID contID;
	
	if(gslGetPlayerIDFromNameReturn(returnVal, &contID) == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pClientEnt = entFromEntityRefAnyPartition(*entRef);
		Entity *pFoundEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, contID);
		if(pClientEnt && pFoundEnt && pFoundEnt->pSaved && pFoundEnt->pSaved->savedDescription)
		{
			ClientCmd_EntDescriptionReturn(pClientEnt, pFoundEnt->myContainerID, pFoundEnt->pSaved->savedDescription);
		}
	}

	SAFE_FREE(entRef);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void entCmd_TestGemSystem(Entity *pEnt)
{
	int i;
	InventoryBag *invBag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, InvBagIDs_Inventory); // lock entire bag
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	for(i=0;i<eaSize(&invBag->ppIndexedInventorySlots);i++)
	{
		Item *pItem = invBag->ppIndexedInventorySlots[i]->pItem;
		ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
		int iBag;
		
		if(pDef && pDef->eGemType)
		{
			//Found a gem, now look for a slot
			for(iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag>=0; iBag--)
			{
				InventoryBag *bag = pEnt->pInventoryV2->ppInventoryBags[iBag];
				BagIterator *iter;

				if(!bag)
					continue;

				iter = invbag_IteratorFromEnt(pEnt,bag->BagID,pExtract);
				for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
				{
					Item *pHolder = (Item*)bagiterator_GetItem(iter);

					if(pHolder && pHolder->pSpecialProps && eaSize(&pHolder->pSpecialProps->ppItemGemSlots))
					{
						TransactionReturnVal * returnVal = LoggedTransactions_CreateManagedReturnVal("inv_ent_tr_GemItem", NULL, NULL);

						//Do transaction here
						AutoTrans_inv_ent_tr_GemItem(returnVal,GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
							invBag->BagID,i,pItem->id,
							bag->BagID,iter->i_cur,pHolder->id,0,NULL,NULL,pExtract);
					}
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void entCmd_TestItemUpgrade(Entity *pEnt, const char *pchItemDef)
{
	ItemDef *pDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pchItemDef);
	InventoryBag *invBag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, InvBagIDs_Inventory); // lock entire bag

	if(pDef)
	{
		int i;

		for(i=0;i<eaSize(&invBag->ppIndexedInventorySlots);i++)
		{
			Item *pItem = invBag->ppIndexedInventorySlots[i]->pItem;
			ItemDef *pSlotItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

			if(pSlotItemDef == pDef)
			{
				//	AutoTrans_inv_ent_tr_UpgradeItem(NULL,GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
				//		invBag->BagID,i,pItem->id,0,0,0,pExtract);
				itemUpgrade_BeginStack(pEnt,10,InvBagIDs_Inventory,i,pItem->id,0,0,0);
			}
		}
	}
}
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entCmd_UpgradeItem(Entity *pEnt, 
	int SrcBagID, int SrcSlotIdx, U64 uSrcItemID, S32 iSrcCount,
	int ModBagID, int ModSlotIdx, U64 uModItemID)
{
	itemUpgrade_BeginStack(pEnt,iSrcCount,SrcBagID,SrcSlotIdx,uSrcItemID,ModBagID,ModSlotIdx,uModItemID);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void entCmd_CancelUpgradeJob(Entity *pEnt)
{
	itemUpgrade_CancelJob(pEnt);
}

typedef struct GemItemCallbackData
{
	EntityRef entRef;
	const char *pchGemName;
	const char *pchItemName;
} GemItemCallbackData;

static void ent_GemItem_CB(TransactionReturnVal *pReturnVal, GemItemCallbackData *pData)
{
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pData->entRef);
		if (pEnt && pData->pchItemName && pData->pchGemName)
		{
			eventsend_RecordGemSlotted(pEnt, pData->pchItemName, pData->pchGemName);
		}
		ClientCmd_scp_InvalidateFakeEntities(pEnt);
	}

	free(pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entCmd_GemItem(Entity *pEnt, 
	int SrcBagID, int SrcSlotIdx, U64 uSrcItemID, 
	int DestBagID, int DestSlotIdx, U64 uDstItemID, S32 uDestGemIdx)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	GemItemCallbackData *pData = malloc(sizeof(GemItemCallbackData));
	TransactionReturnVal * returnVal;
	ItemDef* pNewGemDef = NULL;
	ItemDef* pOldGemDef = NULL;
	ItemChangeReason reason = {0};
	char* estrDetail = NULL;

	Item *pItem = inv_GetItemByID(pEnt, uSrcItemID);
	if (pItem)
	{
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		if (pItemDef)
		{
			pNewGemDef = pItemDef;
			pData->pchGemName = pItemDef->pchName;
		}
	}

	pItem = inv_GetItemByID(pEnt, uDstItemID);
	if (pItem)
	{
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		if (pItem && pItem->pSpecialProps && eaSize(&pItem->pSpecialProps->ppItemGemSlots) > uDestGemIdx)
			pOldGemDef = GET_REF(pItem->pSpecialProps->ppItemGemSlots[uDestGemIdx]->hSlottedItem);
		
		if (pItemDef)
			pData->pchItemName = pItemDef->pchName;
	}

	if (pNewGemDef == pOldGemDef)
	{
		//Abort, we're re-gemming the exact same item, which would just be a pointless waste.
		free(pData);
		return;
	}

	pData->entRef = entGetRef(pEnt);

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("inv_ent_tr_GemItem", pEnt, ent_GemItem_CB, pData);
	estrPrintf(&estrDetail, "DestItemDef %s", pData->pchItemName);
	inv_FillItemChangeReason(&reason, pEnt, "Item:GemToSlot", estrDetail);
	AutoTrans_inv_ent_tr_GemItem(returnVal,
		GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,
		SrcBagID, SrcSlotIdx, uSrcItemID,
		DestBagID,DestSlotIdx,uDstItemID,uDestGemIdx,&reason,NULL,pExtract);
	estrDestroy(&estrDetail);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void entCmd_GetDescription(Entity* e, ACMD_SENTENCE pchCharID)
{
	//Using the character ID, find the entity, return the pFoundEnt->pSaved->savedDescription;

	EntityRef *entRef = calloc(1, sizeof(EntityRef));
	*entRef = e->myRef;

	gslGetPlayerIDFromName(pchCharID, entGetVirtualShardID(e), entGetDescription_CB, entRef);
}

static void ent_SetDescription_CB(TransactionReturnVal *pReturn, void *userData)
{
	EntityRef *pRef = (EntityRef*)userData;
	Entity* e = entFromEntityRefAnyPartition(*pRef);
	if(e)
	{
		char *pch = NULL;
		if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			entFormatGameMessageKey(e, &pch, "Entity_DescriptionChanged",
				STRFMT_ENTITY(e),
				STRFMT_END);
			notify_NotifySend(e, kNotifyType_Default, pch, NULL, NULL );
		}
		else
		{
			entFormatGameMessageKey(e, &pch, "Entity_DescriptionChangeFailed",
				STRFMT_ENTITY(e),
				STRFMT_END);
			notify_NotifySend(e, kNotifyType_Failed, pch, NULL, NULL );
		}
		estrDestroy(&pch);
	}

	SAFE_FREE(pRef);
}

AUTO_TRANSACTION
ATR_LOCKS(e, " .Psaved.Saveddescription");
enumTransactionOutcome Entity_tr_SetDescription(ATR_ARGS, NOCONST(Entity)* e, char *pchDescription)
{
	size_t iLen = pchDescription ? strlen(pchDescription) : 0;
	if(NONNULL(e) &&
		NONNULL(e->pSaved) && (pchDescription || e->pSaved->savedDescription) &&
		( (!e->pSaved->savedDescription && pchDescription) 
		|| (e->pSaved->savedDescription && !pchDescription) 
		|| strcmp(pchDescription, e->pSaved->savedDescription) )
		&& iLen <= MAX_DESCRIPTION_LEN)
	{
		if(e->pSaved->savedDescription)
			StructFreeString(e->pSaved->savedDescription);
		e->pSaved->savedDescription = StructAllocString(pchDescription);
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
	else
	{
		return(TRANSACTION_OUTCOME_FAILURE);
	}
}

AUTO_COMMAND ACMD_NAME(EntSetDescription) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_SERVERCMD;
void entCmd_SetDescription(Entity* e, ACMD_SENTENCE pchDescription)
{
	if(e && e->pSaved && (pchDescription || e->pSaved->savedDescription) &&
		( (!e->pSaved->savedDescription && pchDescription) 
		|| (e->pSaved->savedDescription && !pchDescription) 
		|| strcmp(pchDescription, e->pSaved->savedDescription)) )
	{
		int iStrError = StringIsInvalidDescription(pchDescription);
		if(!iStrError)
		{
			EntityRef *pRef = calloc(1, sizeof(EntityRef));
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(ent_SetDescription_CB, pRef);
			*pRef = e->myRef;
			AutoTrans_Entity_tr_SetDescription(pReturn, GLOBALTYPE_GAMESERVER, e->myEntityType, entGetContainerID(e), pchDescription);
		}
		else
		{
			char* pcError;
			estrCreate( &pcError );

			entStringCreateDescriptionError( e, &pcError, iStrError );
			notify_NotifySend(e, kNotifyType_DescriptionInvalid, pcError, "Update Failed", NULL);
			estrDestroy(&pcError);
		}
	}
}

AUTO_COMMAND ACMD_NAME(EntSetPetDescription) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void entCmd_SetPetDescription(Entity* e, U32 petType, U32 petID, ACMD_SENTENCE pchDescription)
{
	if ( e != NULL )
	{
		Entity *petEnt = entity_GetSubEntity(entGetPartitionIdx(e), e, petType, petID);
		if ( petEnt != NULL )
		{
			// If pet is being traded, its description is not allowed to be changed
			if(trade_IsPetBeingTraded(petEnt, e))
				return;

			entCmd_SetDescription(petEnt, pchDescription);
		}
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(e, ".Psaved.Savedname");
enumTransactionOutcome entity_trh_SetName(ATR_ARGS, ATH_ARG NOCONST(Entity)* e, const char *pchNewName)
{
	size_t iLen = pchNewName ? strlen(pchNewName) : 0;
	if(ISNULL(e) ||
		ISNULL(e->pSaved))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILURE: Null Entity or Entity Saved data.");
	}

	if( pchNewName && e->pSaved->savedName &&
		strcmp(pchNewName, e->pSaved->savedName) &&
		iLen < MAX_NAME_LEN )
	{
		char *pchOldName = estrStackCreateFromStr(e->pSaved->savedName);
		strcpy(e->pSaved->savedName, pchNewName); 
		
		TRANSACTION_APPEND_LOG_SUCCESS("SUCCESS: Player changed name from %s to %s", pchOldName, pchNewName);
		
		estrDestroy(&pchOldName);
		
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILURE: Player couldn't change name from %s to %s", e->pSaved->savedName, pchNewName);
	}
}

// Sets Allegiance and also possibly Faction, Sub-Allegiance and Sub-Faction
AUTO_TRANSACTION
ATR_LOCKS(e, ".Hallegiance, .Hsuballegiance, .Hfaction, .Hsubfaction, .Pchar");
enumTransactionOutcome entity_tr_AllegianceSet(ATR_ARGS, NOCONST(Entity) *e, const char *pchAllegiance)
{
	AllegianceDef *pdef;
	AllegianceDef *polddef;

	polddef = GET_REF(e->hAllegiance);
	if (polddef && polddef->bCanBeSubAllegiance)
	{
		COPY_HANDLE(e->hSubAllegiance, e->hAllegiance);
	}

	// Set to either the provided value or the default
	if(pchAllegiance && *pchAllegiance)
		SET_HANDLE_FROM_STRING(g_hAllegianceDict, pchAllegiance, e->hAllegiance);
	else
		COPY_HANDLE(e->hAllegiance,gAllegianceDefaults->hDefaultPlayerAllegiance);

	// Copy the Allegiance's faction if it's set
	pdef = GET_REF(e->hAllegiance);
	if(pdef && IS_HANDLE_ACTIVE(pdef->hFaction))
	{
		CritterFaction *pFaction = GET_REF(e->hFaction);
		if(pFaction && pFaction->bCanBeSubFaction)
			COPY_HANDLE(e->hSubFaction, e->hFaction);

		COPY_HANDLE(e->hFaction, pdef->hFaction);
	}

	entity_trh_AllegianceSpeciesChange(ATR_PASS_ARGS, e, pdef);

	TRANSACTION_RETURN_LOG_SUCCESS("Allegiance %s, SubAllegiance %s, Faction %s, SubFaction %s",
		REF_STRING_FROM_HANDLE(e->hAllegiance), IS_HANDLE_ACTIVE(e->hSubAllegiance) ? REF_STRING_FROM_HANDLE(e->hSubAllegiance) : "None",
		REF_STRING_FROM_HANDLE(e->hFaction), IS_HANDLE_ACTIVE(e->hSubFaction) ? REF_STRING_FROM_HANDLE(e->hSubFaction) : "None");
}

static void AllegianceSet_CB(TransactionReturnVal* returnVal, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(*pRef);
	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			AllegianceDef *pDef = GET_REF(pPlayerEnt->hAllegiance);
			entity_SetDirtyBit(pPlayerEnt, parse_Entity, pPlayerEnt, false);

			if (pDef)
			{
				int i;
				for (i = 0; i < eaSize(&pPlayerEnt->pSaved->ppOwnedContainers); i++)
				{
					AutoTrans_trSavedPet_UpdateSpeciesForAllegiance(LoggedTransactions_CreateManagedReturnVal("AllegianceSet_CB", NULL, NULL),
						GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYSAVEDPET, pPlayerEnt->pSaved->ppOwnedContainers[i]->conID, pDef);
				}

				eventsend_RecordAllegianceSet(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pDef->pcName);
			}
		}
	}
	SAFE_FREE(pRef);
}

// Sets your Allegiance, also possibly modifies your faction and species to match
AUTO_COMMAND ACMD_ACCESSLEVEL(4);
void AllegianceSet(Entity* e, ACMD_NAMELIST("Allegiance", REFDICTIONARY) const char *allegianceName)
{
	if(e)
	{
		TransactionReturnVal *pReturn;
		EntityRef *pRef = calloc(1, sizeof(EntityRef));

		ANALYSIS_ASSUME(e);
		*pRef = entGetRef(e);

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("AllegianceSet", e, AllegianceSet_CB, pRef);
		AutoTrans_entity_tr_AllegianceSet(pReturn, GLOBALTYPE_GAMESERVER, e->myEntityType, entGetContainerID(e), allegianceName);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void AllegianceSetValidate(Entity *e, ACMD_NAMELIST("Allegiance", REFDICTIONARY) const char *allegianceName)
{
	AllegianceDef *pNewAllegiance = RefSystem_ReferentFromString(g_hAllegianceDict, allegianceName);
	if(e && pNewAllegiance)
	{
		AllegianceDef *pOldAllegiance = GET_REF(e->hAllegiance);
		MissionDef *pMissionDef = NULL;
		bool bFoundAllegianceChange = false;
		int i;

		if(!pOldAllegiance || !pOldAllegiance->bCanBeSubAllegiance)
		{
			return;
		}

		pMissionDef = GET_REF(pOldAllegiance->hRequiredMissionForAllegianceChange);
		if(e->pPlayer && e->pPlayer->missionInfo && pMissionDef &&
			!mission_FindMissionFromDef(e->pPlayer->missionInfo, pMissionDef))
		{
			return;
		}

		for(i = 0; i < eaSize(&pOldAllegiance->ppchAllowedAllegianceChanges); i++)
		{
			AllegianceDef *pAllowedAllegianceChangeDef = RefSystem_ReferentFromString(g_hAllegianceDict, pOldAllegiance->ppchAllowedAllegianceChanges[i]);
			if (pAllowedAllegianceChangeDef == pNewAllegiance)
			{
				bFoundAllegianceChange = true;
				break;
			}
		}

		if (bFoundAllegianceChange)
		{
			AllegianceSet(e, allegianceName);
		}
	}
}


// Runs the transaction that sets Player's Allegiance PERMANENTLY, also possibly modifies faction and species to match
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(AllegianceSet);
void exprFuncAllegianceSet(ExprContext* context, ACMD_EXPR_RES_DICT(Allegiance) const char* allegianceName)
{
	Entity* e = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	AllegianceDef *pAllegiance = RefSystem_ReferentFromString(g_hAllegianceDict, allegianceName);
	if(e && pAllegiance)
	{
		TransactionReturnVal *pReturn;
		EntityRef *pRef = calloc(1, sizeof(EntityRef));

		ANALYSIS_ASSUME(e);
		*pRef = entGetRef(e);

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("exprFuncAllegianceSet", e, AllegianceSet_CB, pRef);
		AutoTrans_entity_tr_AllegianceSet(pReturn, GLOBALTYPE_GAMESERVER, e->myEntityType, entGetContainerID(e), allegianceName);
	}
}

AUTO_COMMAND;
char* TestSpawnIn(Entity* e)
{
	Vec3 pos = {0};
	int iPartitionIdx = entGetPartitionIdx(e);

	entGetPos(e, pos);
	pos[1] += entGetHeight(e)/2.f;
	
	if (!beaconIsPositionValid(iPartitionIdx, pos, entGetPrimaryCapsule(e))){
		Beacon *beacon = beaconGetNearestBeacon(iPartitionIdx, pos);
		if (beacon){
			entSetPos(e, beacon->pos, true, __FUNCTION__);
			return "Position invalid, moved player.";
		} else {
			return "Position invalid, but no nearby beacon found.";
		}
	}
	return "Position valid.";
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Pugckillcreditlimit, .Psaved.Bbadname, .Psaved.Esoldname, .Psaved.Savedname, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Savedsubname, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pPuppetEnt, ".Psaved.Savedname");
enumTransactionOutcome gslEntity_tr_BadName(ATR_ARGS, NOCONST(Entity)* e, NOCONST(Entity)* pPuppetEnt, const ItemChangeReason *pReason)
{
	// BadName is a restricted name. This prevents players from creating the BadName xxx. I.e. we know it is safe to use.
	if (e->myEntityType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		inv_ent_trh_AddNumeric(ATR_PASS_ARGS, e, true, "FreeNameChange", 1, pReason);
	}
	estrPrintf(&e->pSaved->esOldName, "%s", e->pSaved->savedName);
	e->pSaved->bBadName = true;
	sprintf(e->pSaved->savedName, "BadName %d", e->myContainerID); 

	if (NONNULL(pPuppetEnt) && NONNULL(pPuppetEnt->pSaved))
	{
		strcpy(pPuppetEnt->pSaved->savedName, e->pSaved->savedName);
	}
	StructFreeStringSafe(&e->pSaved->savedSubName);
	inv_ent_trh_AddNumeric(ATR_PASS_ARGS, e, true, "FreeSubNameChange", 1, pReason);

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Bad Name Changed");

}

static void gslEntity_BadName_CB(TransactionReturnVal* returnVal, void *pData)
{
	Entity *pPlayerEnt = entFromEntityRefAnyPartition((EntityRef)(intptr_t)pData);
	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			// was successful
			// send mail
			if (pPlayerEnt->pPlayer){
				gslMailNPC_AddMail(pPlayerEnt,
					langTranslateMessageKeyDefault(entGetLanguage(pPlayerEnt),"Player_Bad_Name_From_Name", "[UNTRANSLATED]CSR"),
					langTranslateMessageKeyDefault(entGetLanguage(pPlayerEnt),"Player_Bad_Name_Subject", "[UNTRANSLATED]Bad Name."),
					langTranslateMessageKeyDefault(entGetLanguage(pPlayerEnt),"Player_Bad_Name_Body", "[UNTRANSLATED] Your character has a bad name. You will need to rename it next time you log in.")
					);
			}
		}
		else
		{
			// send fail
		}
	}
}

void gslEntity_BadName(GlobalType eEntType, ContainerID uiEntID, GlobalType ePupType, ContainerID uPupID)
{
	Entity* e = entFromContainerIDAnyPartition(eEntType, uiEntID);
	TransactionReturnVal* returnVal;
	ItemChangeReason reason = {0};

	returnVal = LoggedTransactions_CreateManagedReturnVal("EntityBadName", gslEntity_BadName_CB, (void *)(intptr_t)SAFE_MEMBER(e, myRef));
	inv_FillItemChangeReason(&reason, e, "Player:BadName", NULL);

	AutoTrans_gslEntity_tr_BadName(returnVal, GetAppGlobalType(), 
			eEntType, uiEntID, 
			ePupType, uPupID, &reason);
}

// Gives the character a default name and forces them to update it on login
AUTO_COMMAND ACMD_NAME(BadName, force_rename_character) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr, XMLRPC);
S32 player_BadName(Entity* e)
{
	if(e && e->pSaved)
	{
		const char* pchPuppetClassType = gConf.pchRenamePlayerPuppetClassType;
		GlobalType ePupType = GLOBALTYPE_NONE;
		ContainerID uPupID = 0;

		// do transaction to change name and set flag
		if (pchPuppetClassType && pchPuppetClassType[0])
		{
			Entity* pPupEnt = entity_GetPuppetEntityByType(e, pchPuppetClassType, NULL, false, true);
			if (pPupEnt)
			{
				ePupType = entGetType(pPupEnt);
				uPupID = entGetContainerID(pPupEnt);
			}
		}
		gslEntity_BadName(entGetType(e), entGetContainerID(e), ePupType, uPupID);

		return true;
	}

	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Hallegiance");
enumTransactionOutcome player_tr_SetAllegiance(ATR_ARGS, NOCONST(Entity)* e, const char *pcAllegiance)
{
	SET_HANDLE_FROM_STRING("Allegiance", pcAllegiance, e->hAllegiance);

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Allegiance Changed");
}

static void player_SetAllegiance_CB(TransactionReturnVal* returnVal, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(*pRef);
	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			entity_SetDirtyBit(pPlayerEnt, parse_Entity, pPlayerEnt, false);
		}
	}
	SAFE_FREE(pRef);
}

// Gives the character a default name and forces them to update it on login
AUTO_COMMAND ACMD_NAME(SetAllegiance) ACMD_ACCESSLEVEL(9);
void player_SetAllegiance(Entity* e, const char *pcAllegiance)
{
	if(e && e->pSaved)
	{
		// do transaction to change name and set flag
		TransactionReturnVal* returnVal;
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		*pRef = entGetRef(e);

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("PlayerSetAllegiance", e, player_SetAllegiance_CB, pRef);
		AutoTrans_player_tr_SetAllegiance(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), pcAllegiance);
	}
}

static void SetIsGM_CB(TransactionReturnVal* returnVal, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(*pRef);
	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			RemoteCommand_ChatServerForceAccessLevel(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pPlayerEnt), entGetAccessLevel(pPlayerEnt));
			ServerChat_PlayerUpdate(pPlayerEnt, CHATUSER_UPDATE_GLOBAL);
			ServerChat_StatusUpdate(pPlayerEnt);
		}
		else
		{
			// didn't change so do nothing
		}
	}
	SAFE_FREE(pRef);
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Pplayer.Bisgm");
enumTransactionOutcome gslEntity_tr_SetIsGM(ATR_ARGS, NOCONST(Entity)* e, S32 isGM)
{
	e->pPlayer->bIsGM = isGM;
	TRANSACTION_RETURN_LOG_SUCCESS(
		"IsGM changed");
}

// Toggle the GM icon on or off
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_NAME(SetIsGM) ACMD_CATEGORY(csr);
void gslEntity_SetIsGM(Entity* e, S32 isGM)
{
	if(e && e->pPlayer)
	{
		// do transaction to change name and set flag
		TransactionReturnVal* returnVal;
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		*pRef = entGetRef(e);

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetIsGM", e, SetIsGM_CB, pRef);		
		AutoTrans_gslEntity_tr_SetIsGM(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), isGM);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Pplayer.BignoreBootTimer");
enumTransactionOutcome gslEntity_tr_IgnoreLogoutTimers(ATR_ARGS, NOCONST(Entity)* e, S32 ignoreLogoutTimers)
{
	e->pPlayer->bIgnoreBootTimer = ignoreLogoutTimers;
	TRANSACTION_RETURN_LOG_SUCCESS(
		"IgnoreBootTimer changed");
}


static void IgnoreLogoutTimers_CB(TransactionReturnVal* returnVal, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(*pRef);
	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			// Nothing in particular
		}
		else
		{
			// didn't change so do nothing
		}
	}
	SAFE_FREE(pRef);
}

// Toggle the IngnoreBootTimer on or off. Used in gslMechanics.c
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_NAME(IgnoreLogoutTimers);
void gslEntity_IgnoreLogoutTimers(Entity* e, S32 ignoreLogoutTimers)
{
	if(e && e->pPlayer)
	{
		// do transaction to change name and set flag
		TransactionReturnVal* returnVal;
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		*pRef = entGetRef(e);

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("IgnoreLogoutTimers", e, IgnoreLogoutTimers_CB, pRef);		
		AutoTrans_gslEntity_tr_IgnoreLogoutTimers(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), ignoreLogoutTimers);
	}
}



static void SetIsDev_CB(TransactionReturnVal* returnVal, void *pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(*pRef);
	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			RemoteCommand_ChatServerForceAccessLevel(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pPlayerEnt), entGetAccessLevel(pPlayerEnt));
			ServerChat_PlayerUpdate(pPlayerEnt, CHATUSER_UPDATE_GLOBAL);
			ServerChat_StatusUpdate(pPlayerEnt);
		}
		else
		{
			// didn't change so do nothing
		}
	}
	SAFE_FREE(pRef);
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Pplayer.Bisdev");
enumTransactionOutcome gslEntity_tr_SetIsDev(ATR_ARGS, NOCONST(Entity)* e, S32 isDev)
{
	e->pPlayer->bIsDev = isDev;

	TRANSACTION_RETURN_LOG_SUCCESS(
		"IsDev changed");
}

AUTO_TRANSACTION
ATR_LOCKS(pData, ".Idayssubscribed");
enumTransactionOutcome gslGAD_tr_SetDaysSubscribed(ATR_ARGS, NOCONST(GameAccountData) *pData, U32 iDays, U32 bOnlyIfHigher)
{
	if(NONNULL(pData) && (!bOnlyIfHigher || iDays > pData->iDaysSubscribed))
	{
		pData->iDaysSubscribed = iDays;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Set number of days subscribed (override for testing rewards based on subscribed days)
AUTO_COMMAND ACMD_NAME(SetGADDaysSubscribed) ACMD_ACCESSLEVEL(7);
void gslEntity_SetGADDaysSubscribed(Entity *pEntity, U32 iDays)
{
	if ( pEntity != NULL && pEntity->pPlayer != NULL )
	{
		AutoTrans_gslGAD_tr_SetDaysSubscribed(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_GAMEACCOUNTDATA, pEntity->pPlayer->accountID, iDays, false);
	}
}

// Toggle the Dev icon on or off
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(3) ACMD_NAME(SetIsDev) ACMD_CATEGORY(csr);
void gslEntity_SetIsDev(Entity* e, S32 isDev)
{
	if(e && e->pPlayer)
	{

		// do transaction to change name and set flag
		TransactionReturnVal* returnVal;
		EntityRef *pRef = calloc(1, sizeof(EntityRef));
		*pRef = entGetRef(e);

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetIsDev", e, SetIsDev_CB, pRef);
		AutoTrans_gslEntity_tr_SetIsDev(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), isDev);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Psaved.Savedname");
enumTransactionOutcome gslEntity_tr_Change_Saved_Name(ATR_ARGS, NOCONST(Entity)* e, const char *pcNewName)
{
	if(entity_trh_SetName(ATR_PASS_ARGS, e, pcNewName) == TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"Saved Name changed");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Saved Name failed");
	}
}

// Change a player's name
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr) ACMD_NAME(ChangeName);
void gslEntity_ChangeSavedName(Entity* e, char *pcNewName)
{
	if(e && e->pSaved && pcNewName)
	{
		int strerr = StringIsInvalidCharacterName(pcNewName, 4);
		TransactionReturnVal* returnVal;

		if(strerr != 0)
		{
			char* pcError;
			estrCreate( &pcError );

			entStringCreateNameError(e, &pcError, strerr );

			notify_NotifySend(e, kNotifyType_NameInvalid, pcError, NULL, NULL);
			estrDestroy(&pcError);
			
			return;
		}

		// do transaction to change name and set flag
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("ChangeSavedName", e, NULL, NULL);
		AutoTrans_gslEntity_tr_Change_Saved_Name(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), pcNewName);
	}
	else
	{
		if(e)
		{
			notify_NotifySend(e, kNotifyType_NameInvalid, entTranslateMessageKey(e, "Change_Saved_Name_Invalid"), NULL, NULL);
		}
	}
}

// Change a player's name with no restrictions on what it can be.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(csr) ACMD_NAME(ChangeName_NoRestrictions);
void gslEntity_ChangeSavedNameNoRestrictions(Entity* e, char *pcNewName)
{
	if(e && e->pSaved && pcNewName && *pcNewName)
	{
		TransactionReturnVal* returnVal;

		// do transaction to change name and set flag
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("ChangeSavedNameNoRestrictions", e, NULL, NULL);
		AutoTrans_gslEntity_tr_Change_Saved_Name(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), pcNewName);
	}
	else
	{
		if(e)
		{
			notify_NotifySend(e, kNotifyType_NameInvalid, entTranslateMessageKey(e, "Change_Saved_Name_No_Restrictions_Invalid"), NULL, NULL);
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(e, ".Pplayer.Eskillspecialization");
enumTransactionOutcome gslEntity_tr_ChangeSkillSpecialization(ATR_ARGS, NOCONST(Entity)* e, S32 eItemTag)
{
	if(NONNULL(e) && NONNULL(e->pPlayer) && !entity_PlayerHasSkillSpecializationInternal(&e->pPlayer->eSkillSpecialization, eItemTag))
	{
		eaiPush(&e->pPlayer->eSkillSpecialization, eItemTag);
	}
		
	TRANSACTION_RETURN_LOG_SUCCESS(
		"SkillSpecialization changed");
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ChangeSkillSpecialization) ACMD_PRIVATE;
void gslEntity_ChangeSkillSpecialization(Entity* e, const char *pcItemTag)
{
	if(e && e->pPlayer)
	{
		TransactionReturnVal* returnVal;
		ItemTag eItemTag = StaticDefineIntGetInt(ItemTagEnum, pcItemTag);
		if(eItemTag >= 0)
		{
			// do transaction to change name and set skill spec
			returnVal = LoggedTransactions_CreateManagedReturnValEnt("ChangeSkillSpecialization", e, NULL, NULL);
			AutoTrans_gslEntity_tr_ChangeSkillSpecialization(returnVal, GetAppGlobalType(), entGetType(e), entGetContainerID(e), eItemTag);
		
		}
	}
}


AUTO_TRANS_HELPER
ATR_LOCKS(e, ".Pplayer.Langid");
int entity_trh_GetLanguage(ATH_ARG NOCONST(Entity) *e)
{
	if(ISNULL(e) || ISNULL(e->pPlayer))
	{
		return locGetLanguage(getCurrentLocale());
	}
	else
	{
		return(e->pPlayer->langID);
	}
}

static void RemoteCommandDBExportEntityToFile_CB(TransactionReturnVal *returnVal, void *userData)
{
	//char *result = 0;
	//estrStackCreate(&result);
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_DBExportEntityToFile(returnVal, NULL);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		//printf("Result of Export:%s", result);
	}
	//estrDestroy(&result);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void ExportEntityPlayer(Entity* e, const char* fileName)
{
	RemoteCommand_DBExportEntityToFile(
		objCreateManagedReturnVal(RemoteCommandDBExportEntityToFile_CB, NULL),
		GLOBALTYPE_OBJECTDB, 0,
		e->myEntityType, e->myContainerID, fileName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(RequestEntCopyByType) ACMD_ACCESSLEVEL(0) ACMD_SERVERONLY ACMD_PRIVATE;
void entity_RequestEntCopyByType(Entity *pClientEntity, int iEntID, int iEntType, char *pchClassType)
{
	EntityStruct eStruct = {0};
	NOCONST(Entity) *pTemp;

	if(!pClientEntity)
		return;

	pTemp = CONTAINER_NOCONST(Entity, entFromContainerID(entGetPartitionIdx(pClientEntity), iEntType, iEntID));
	if (!pTemp)
	{
		char idBuf[128];
		pTemp = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(iEntType), ContainerIDToString(iEntID, idBuf));
	}
	if (!pTemp || !pTemp->pSaved)
		return;

	if (pchClassType)
	{
		PuppetEntity* pPuppet;
		if (!pTemp->pSaved->pPuppetMaster)
			return;
		pPuppet = entity_GetPuppetByType(CONTAINER_RECONST(Entity, pTemp), pchClassType, GET_REF(pTemp->pSaved->pPuppetMaster->hPreferredCategorySet), true);
		if (!pPuppet)
			return;
		if (pPuppet->curID != pTemp->pSaved->pPuppetMaster->curID)
		{
			char idBuf[128];

			// Create references to puppets on demand
			if (pTemp->pPlayer)
			{
				S32 i;
				for (i = eaSize(&pTemp->pPlayer->eaPlayerEntCopyForInfo) - 1; i >= 0; i--)
				{
					if (pTemp->pPlayer->eaPlayerEntCopyForInfo[i]->myEntityType == GLOBALTYPE_ENTITYSAVEDPET
						&& pTemp->pPlayer->eaPlayerEntCopyForInfo[i]->myContainerID == pPuppet->curID)
					{
						break;
					}
				}
				if (i < 0)
				{
					PlayerEntCopyForInfo *pCopyInfo = StructCreate(parse_PlayerEntCopyForInfo);
					if (pCopyInfo)
					{
						pCopyInfo->myEntityType = GLOBALTYPE_ENTITYSAVEDPET;
						pCopyInfo->myContainerID = pPuppet->curID;
						SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pPuppet->curID, idBuf), pCopyInfo->hEnt);
						eaPush(&pTemp->pPlayer->eaPlayerEntCopyForInfo, pCopyInfo);
					}
				}
			}

			pTemp = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET), ContainerIDToString(pPuppet->curID, idBuf));
			if (!pTemp || !pTemp->pSaved)
				return;
		}
	}

	pTemp = StructCloneWithCommentNoConst(parse_Entity, pTemp, "Temp ent in entity_RequestEntCopyByType");
	if (!pTemp)
		return;

	//We just need stuff for entity info screen that is normally not sent to all clients
	REMOVE_HANDLE(pTemp->pSaved->hDiary);
	StructDestroyNoConstSafe(parse_Critter, &pTemp->pCritter);
	if (pTemp->pPlayer)
	{
		//Has stuff that needs to stay secret
		pTemp->pPlayer->accessLevel = 0;
		pTemp->pPlayer->accountAccessLevel = 0;
		memset(pTemp->pPlayer->privateAccountName,0,MAX_NAME_LEN);
		StructDestroyNoConstSafe(parse_PlayerUI, &pTemp->pPlayer->pUI);
		StructDestroyNoConstSafe(parse_PlayerAccountData, &pTemp->pPlayer->pPlayerAccountData);
		eaDestroyStruct(&pTemp->pPlayer->eaPlayerEntCopyForInfo, parse_PlayerEntCopyForInfo);
		if (pTemp->pPlayer->pGuild)
		{
			eaDestroyStruct(&pTemp->pPlayer->pGuild->eaOfficerComments, parse_PlayerGuildOfficerComments);
		}
	}

	eStruct.pEntity = (Entity*)pTemp;
	if (pchClassType)
	{
		ClientCmd_RecieveEntCopyWithType(pClientEntity,iEntID,iEntType,pchClassType,&eStruct);
	}
	else
	{
		ClientCmd_RecieveEntCopy(pClientEntity,iEntID,iEntType,&eStruct);
	}

	StructDestroyNoConst(parse_Entity, pTemp);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(RequestEntCopy) ACMD_ACCESSLEVEL(0) ACMD_SERVERONLY ACMD_PRIVATE;
void entity_RequestEntCopy(Entity *pClientEntity, int iEntID, int iEntType)
{
	entity_RequestEntCopyByType(pClientEntity, iEntID, iEntType, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(GetReadyToRequestEntCopy) ACMD_ACCESSLEVEL(0) ACMD_SERVERONLY ACMD_PRIVATE;
void entity_GetReadyToRequestEntCopy(Entity *pClientEntity, ContainerID iEntID, int iEntType)
{
	char idBuf[128];
	int i;
	PlayerEntCopyForInfo *pecfi;

	if (!pClientEntity->pPlayer) 
		return;

	ContainerIDToString(iEntID, idBuf);
	if (RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(iEntType), idBuf )) 
		return;

	for (i = eaSize(&pClientEntity->pPlayer->eaPlayerEntCopyForInfo)-1; i >= 0; --i)
	{
		pecfi = pClientEntity->pPlayer->eaPlayerEntCopyForInfo[i];
		if (pecfi && pecfi->myContainerID == iEntID && pecfi->myEntityType == iEntType) break;
	}
	if (i < 0)
	{
		pecfi = StructCreate(parse_PlayerEntCopyForInfo);
		pecfi->myContainerID = iEntID;
		pecfi->myEntityType = iEntType;
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(iEntType),idBuf,pecfi->hEnt);
		eaPush(&pClientEntity->pPlayer->eaPlayerEntCopyForInfo, pecfi);
		if (eaSize(&pClientEntity->pPlayer->eaPlayerEntCopyForInfo) > 5)
		{
			StructDestroy(parse_PlayerEntCopyForInfo, pClientEntity->pPlayer->eaPlayerEntCopyForInfo[0]);
			eaRemove(&pClientEntity->pPlayer->eaPlayerEntCopyForInfo, 0);
		}
	}
}

//DO NOTHING. If you want these to do something, find the game specific code written for these and do it there
void DEFAULT_LATELINK_gameSpecific_Load(void)
{

}

void DEFAULT_LATELINK_gameSpecific_EntityTick(Entity* e, F32 fTime)
{

}

void DEFAULT_LATELINK_gslGameSpecific_Minigame_Tick(void)
{

}

void DEFAULT_LATELINK_gslGameSpecific_Minigame_Login(Entity* e)
{

}

void DEFAULT_LATELINK_gslGameSpecific_Minigame_PartitionLoad(int iPartitionIdx)
{

}

void DEFAULT_LATELINK_gslGameSpecific_Minigame_PartitionUnload(int iPartitionIdx)
{

}

AUTO_STARTUP(GAMESPECIFIC) ASTRT_DEPS(Items, CritterGroups);
void gameSpecific_AutoStartup(void)
{
	loadstart_printf("Loading game specific data...");
	gameSpecific_Load();
	loadend_printf("done.");
}

// Gets the interaction range for the interactable node/critter. If no interact range is specified,
// the default interact range for the current region is returned.
F32 gslEntity_GetInteractRange(Entity* ePlayer, Entity* eCritter, WorldInteractionNode *pNode)
{
	U32 iDist = 0;

	if (pNode) {
		WorldInteractionEntry *pEntry = pNode ? wlInteractionNodeGetEntry(pNode) : NULL;
		if(pEntry)
		{
			iDist = wlInteractionGetInteractDist(pEntry->full_interaction_properties);
		}
	} else if (eCritter && eCritter->pCritter) {
		iDist = eCritter->pCritter->uInteractDist;
	}

	if(iDist)
		return iDist;

	iDist = entity_GetCurrentRegionInteractDist(ePlayer);
	if(iDist)
		return iDist;

	if (pNode) {
		return DEFAULT_NODE_INTERACT_DIST;
	} else {
		return INTERACT_RANGE;
	}
}

void gslEntitySetInvisibleTransient(Entity* e, S32 enabled){
	if(!e){
		return;
	}
	
	e->isInvisibleTransient = !!enabled;
	gslEntityUpdateInvisibleState(e);
}

void gslEntitySetInvisiblePersistent(Entity* e, S32 enabled){
	if(!e){
		return;
	}
	
	e->isInvisiblePersistent = !!enabled;
	gslEntityUpdateInvisibleState(e);
}

void gslEntitySetIsStrafing(Entity* e, S32 enabled){
	if(!e){
		return;
	}
	
	mrSurfaceSetIsStrafing(e->mm.mrSurface, enabled);
	mrFlightSetIsStrafing(e->mm.mrFlight, enabled);
}

void gslEntitySetUseThrottle(Entity* e, S32 enabled){
	if(!e){
		return;
	}
	
	mrFlightSetUseThrottle(e->mm.mrFlight, enabled);
}

void gslEntitySetUseOffsetRotation(Entity* e, S32 enabled){
	if(!e){
		return;
	}
	
	mrFlightSetUseOffsetRotation(e->mm.mrFlight, enabled);
}

void gslEntityGodMode(Entity* ent, int iSet)
{
	if (ent && ent->pChar) {
		ent->pChar->bInvulnerable = iSet;
		ent->pChar->bUnstoppable = iSet;
		entity_SetDirtyBit(ent, parse_Character, ent->pChar, false);
	}
}

void gslEntityUntargetableMode(Entity* ent, int iSet)
{
	if (ent && ent->pChar) {
		if (iSet) {
			entSetCodeFlagBits( ent, ENTITYFLAG_UNTARGETABLE );
		} else {
			entClearCodeFlagBits( ent, ENTITYFLAG_UNTARGETABLE );
		}
	}
}


void gslEntity_UpdateMovementMangerFaction(S32 iPartitionIdx, Entity *pEnt)
{
	Entity *eRelation = pEnt;
	if (iPartitionIdx != PARTITION_IN_TRANSACTION)
		eRelation = entity_EntityGetRelationEnt(iPartitionIdx, pEnt);

	if (eRelation)
	{
		CritterFaction *pFaction;
		pFaction = entGetFaction(eRelation);
		if (pFaction)
		{
			mmSetEntityFactionIndex(pEnt->mm.movement, pFaction->factionIndex);
		}
	}
}

void gslEntity_SetFactionOverrideByHandle(Entity *pEnt, FactionOverrideType type, ConstReferenceHandle *phFactionRef)
{
	switch (type)
	{
		xcase kFactionOverrideType_DEFAULT:
			if (RefSystem_CompareHandles(REF_HANDLEPTR(pEnt->hFactionOverride), phFactionRef))
				return;

			RefSystem_CopyHandle(REF_HANDLEPTR(pEnt->hFactionOverride), phFactionRef);

		xcase kFactionOverrideType_POWERS:
			RefSystem_CopyHandle(REF_HANDLEPTR(pEnt->hPowerFactionOverride), phFactionRef);

		xdefault:
			devassertmsg(0, "Unhandled FactionSetType.");
	}

	entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	pEnt->factionDirtiedCount++;
	aiCivilianUpdateIsHostile(pEnt);
	gslEntity_UpdateMovementMangerFaction(entGetPartitionIdx(pEnt), pEnt);
}

void gslEntity_SetFactionOverrideByName(Entity *pEnt, FactionOverrideType type, const char *pchFaction)
{
	switch (type)
	{
		case kFactionOverrideType_DEFAULT:
			SET_HANDLE_FROM_REFDATA(g_hCritterFactionDict, pchFaction, pEnt->hFactionOverride);

		xcase kFactionOverrideType_POWERS:
			SET_HANDLE_FROM_REFDATA(g_hCritterFactionDict, pchFaction, pEnt->hPowerFactionOverride);

		xdefault:
			devassertmsg(0, "Unhandled FactionSetType.");
	}

	entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	pEnt->factionDirtiedCount++;
	aiCivilianUpdateIsHostile(pEnt);
	gslEntity_UpdateMovementMangerFaction(entGetPartitionIdx(pEnt), pEnt);
}

// clears the faction handle of the given type
void gslEntity_ClearFaction(Entity *pEnt, FactionOverrideType type)
{
	switch (type)
	{
		xcase kFactionOverrideType_DEFAULT:
			REMOVE_HANDLE(pEnt->hFactionOverride);

		xcase kFactionOverrideType_POWERS:
			REMOVE_HANDLE(pEnt->hPowerFactionOverride);

		xdefault:	
			devassertmsg(0, "Unhandled FactionSetType.");
	}

	entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	pEnt->factionDirtiedCount++;
	aiCivilianUpdateIsHostile(pEnt);
	gslEntity_UpdateMovementMangerFaction(entGetPartitionIdx(pEnt), pEnt);

}

U32 gslEntity_CreateMacro(Entity* pEnt, const char* pchMacro, const char* pchDesc, const char* pchIcon)
{
	if (pEnt && 
		pEnt->pPlayer && 
		pEnt->pPlayer->pUI && 
		eaSize(&pEnt->pPlayer->pUI->eaMacros) < PLAYER_MACRO_COUNT_MAX)
	{
		PlayerMacro* pMacro = StructCreate(parse_PlayerMacro);
		pMacro->uMacroID = ++pEnt->pPlayer->pUI->uMacroIDMax;
		if (!pMacro->uMacroID)
			pMacro->uMacroID++;
		pMacro->pchMacro = StructAllocString(pchMacro);
		pMacro->pchDescription = StructAllocString(pchDesc);
		pMacro->pchIcon = allocAddString(pchIcon);
		eaPush(&pEnt->pPlayer->pUI->eaMacros, pMacro);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		return pMacro->uMacroID;
	}
	return 0;
}

bool gslEntity_DestroyMacro(Entity* pEnt, U32 uMacroID)
{
	S32 iIdx = entity_FindMacroByID(pEnt, uMacroID);
	if (iIdx >= 0)
	{
		StructDestroy(parse_PlayerMacro, eaRemove(&pEnt->pPlayer->pUI->eaMacros, iIdx));
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		return true;
	}
	return false;
}

#if GAMESERVER || GAMECLIENT
static void gslMovementGlobalMsgHandler(const MovementGlobalMsg* msg){
	switch(msg->msgType){
		xdefault:{
			entCommonMovementGlobalMsgHandler(msg);
		}
	}
}
#endif

AUTO_RUN;
void gslEntity_SetGlobalMovementMsgHandler(void)
{
#if GAMESERVER || GAMECLIENT
	mmGlobalSetMsgHandler(gslMovementGlobalMsgHandler);
#endif
}

// create/set an entity's UI var
void entSetUIVar(Entity *e, const char* VarName, MultiVal* pMultiVal)
{
	UIVar* pUIVar;

	entity_SetDirtyBit(e, parse_Entity, e, false);
	pUIVar = eaIndexedGetUsingString(&e->UIVars, VarName);

	if ( pUIVar )
	{
		MultiValCopy(&pUIVar->Value, pMultiVal);
	}

	else
	{
		UIVar* pTmpUIVar = StructCreate(parse_UIVar);

		pTmpUIVar->pchName = (char*)allocAddString(VarName);
		MultiValCopy(&pTmpUIVar->Value, pMultiVal);

		entSetActive(e);
		eaIndexedAdd(&e->UIVars, pTmpUIVar);
	}
}

// delete an entity UI var
bool entDeleteUIVar(Entity *e, const char* VarName)
{
	S32 index = eaIndexedFindUsingString(&e->UIVars, VarName);
	if (index >= 0)
	{
		UIVar* pUIVar = eaRemove(&e->UIVars, index);
		entSetActive(e);
		MultiValClear(&pUIVar->Value);
		StructDestroy(parse_UIVar, pUIVar);
		entity_SetDirtyBit(e, parse_Entity, e, false);
		return true;
	}
	else
		return false;
}

#include "AutoGen/gslEntity_h_ast.c"
