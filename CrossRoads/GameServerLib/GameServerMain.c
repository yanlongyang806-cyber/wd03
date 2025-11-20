/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameServerLib.h"
#include "gslPartition.h"

#include "utils.h"
#include "referencesystem.h"
#include "EntityIterator.h"
#include "dynnode.h"
#include "rand.h"
#include "timing.h"
#include "entitylib.h"
#include "Entity.h"
#include "objSchema.h"
#include "sysutil.h"
#include "file.h"
#include "FolderCache.h"
#include "AutoGen/GameServerLib_AutoTransactions_Autogen_Wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "objPath.h"
#include "GameAccountDataCommon.h"
#include "gslCallout.h"
#include "gslChat.h"
#include "gslCombatDeathPrediction.h"
#include "gslEncounter.h"
#include "gslEncounterDebug.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "gslMechanics.h"
#include "gslQueue.h"
#include "gslPvPGame.h"
#include "gslScoreboard.h"
#include "ShardVariableCommon.h"
#include "gslSpawnPoint.h"
#include "gslTransactions.h"
#include "gslTriggerCondition.h"
#include "gslLayerFSM.h"
#include "gslPlayerFSM.h"
#include "gslProjectileEntity.h"
#include "gslPVP.h"
#include "gslTeam.h"
#include "gslTimeControl.h"
#include "gslUGC.h"
#include "aiTeam.h"
#include "Team.h"
#include "AttribMod.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CombatDebug.h"
#include "CombatEval.h"
#include "CombatGlobal.h"
#include "CombatSensitivity.h"
#include "CoreServerCombatCallbacks.h"
#include "cutscene.h"
#include "EntitySavedData.h"
#include "PowerAnimFX.h"
#include "PowerVars.h"
#include "Powers.h"
#include "CombatMods.h"
#include "PowerTree.h"
#include "PowerTreeTransactions.h"
#include "SavedPetCommon.h"
#include "Character_tick.h"
#include "Character_target.h"
#include "Character_combat.h"
#include "entCritter.h"
#include "Reward.h"
#include "NotifyCommon.h"
#include "aiPowers.h"
#include "aiDebug.h"
#include "aiLib.h"
#include "winutil.h"
#include "Player.h"
#include "gslDiary.h"
#include "gslInterior.h"
#include "gslHandleMsg.h"
#include "gslCurrencyExchange.h"
#include "gslActivity.h"
#include "gslItemUpgrade.h"

#include "itemCommon.h"
#include "inventoryCommon.h"
#include "itemServer.h"
#include "gslContact.h"
#include "timedeventqueue.h"
#include "gslMission.h"
#include "gslProgression.h"
#include "gslMapState.h"
#include "gslInteractionManager.h"
#include "gslBugReport.h"
#include "gslOldEncounter.h"
#include "Guild.h"
#include "PowerTreeHelpers.h"
#include "AutoGen/Player_h_ast.h"

#define	PHYSX_SRC_FOLDER "../../3rdparty"
#include "PhysicsSDK.h"
#include "AutoGen/Entity_h_ast.h"
#include "objTransactions.h"
#include "gslEntity.h"
#include "gslEntityPresence.h"
#include "globalStateMachine.h"
#include "gslBaseStates.h"
#include "logcomm.h"
#include "earray.h"
#include "AutoGen/controller_autogen_remotefuncs.h"
#include "serverlib.h"
#include "CharacterAttribs.h"
#include "gslSavedPet.h"
#include "gslSendToClient.h"
#include "Color.h"
#include "StringFormat.h"

#include "EntityMovementDisableMovement.h"
#include "EntityMovementFlight.h"
#include "EntityMovementManager.h"

void gslGroupProject_PerFrame(void);
void gslMapPopulationCache_BeginFrame(void);

extern ParseTable parse_PowerActivation[];
#define TYPE_parse_PowerActivation PowerActivation
extern ParseTable parse_CombatDebug[];
#define TYPE_parse_CombatDebug CombatDebug

extern int g_errorClientCount; // From SuperAssert.c, sent upon error/crash

bool gbTestingMode = false;
AUTO_CMD_INT(gbTestingMode, SetTestingMode) ACMD_CMDLINE;

bool s_bCombatSleep = true;
AUTO_CMD_INT(s_bCombatSleep,CombatSleep);

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	if (strstri(GetCommandLine(), "WebRequestServer"))
	{
		gGSLState.gbWebRequestServer = true;
		SetAppGlobalType(GLOBALTYPE_WEBREQUESTSERVER);
	}
	else if (strstri(GetCommandLine(), "GatewayServer"))
	{
		gGSLState.gbGatewayServer = true;
		SetAppGlobalType(GLOBALTYPE_GATEWAYSERVER);
	}
	else if (strstri(GetCommandLine(), "ServerBinner"))
	{
		gGSLState.gbServerBinner = true;
		SetAppGlobalType(GLOBALTYPE_SERVERBINNER);
	}
	else if(strstri(GetCommandLine(), "BeaconMasterServer"))
	{
		SetAppGlobalType(GLOBALTYPE_BCNMASTERSERVER);
	}
	else if(strstri(GetCommandLine(), "BeaconServer") ||
			strstri(GetCommandLine(), "BeaconAutoServer") ||
			strstri(GetCommandLine(), "BeaconRequestServer"))
	{
		SetAppGlobalType(GLOBALTYPE_BCNSUBSERVER);
	}
	else if(strstri(GetCommandLine(), "BeaconClient"))
	{
		SetAppGlobalType(GLOBALTYPE_BCNCLIENTSENTRY);
	}
	else
	{
		SetAppGlobalType(GLOBALTYPE_GAMESERVER);
	}

	FolderCacheAllowCoreExclusions(true);
}

S32 setPriorityAtStartup = 1;
AUTO_CMD_INT(setPriorityAtStartup, setPriorityAtStartup);

extern GlobalTypeMapping productTypeMapping[];
int main(int argc, char **argv)
{
	#if 0
	{
		HANDLE hMutex = CreateMutex(NULL, FALSE, "Local\\OneServerPlease");
		assert(hMutex);
		switch(WaitForSingleObject(hMutex, 0)){
			xcase WAIT_OBJECT_0:
			acase WAIT_ABANDONED:
				// Yay, keep going.
			xdefault:
				return 0;
		}
	}
	#endif

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS

	if(	isProductionMode() &&
		setPriorityAtStartup)
	{
		SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
	}

	// First, call the universal setup gameserverlib stuff
	gslPreMain(GetProjectName());

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'S', 0x008800);

	// Call the game server loop. Note, we'll either need to pass some game-specific callbacks in here or restructure this
	gslMain(argc, argv);

	svrLogFlush(1);
	commFlushAndCloseAllComms(1.0f);

	EXCEPTION_HANDLER_END
	return 0;
}

void gslExternInit(void)
{
	coreserverInitCombatCallbacks();
}

void gslExternLoadPostMapLoad(void)
{
}







static void player_RecordLastGoodPosition(Entity *e)
{
	#define		GOOD_POS_MINSTEP 4
	#define		GOOD_POS_MAXSTEP 16
	#define		GOOD_POS_MAX ARRAY_SIZE(pl->last_good_pos)
	int			i;
	Vec3		pos;
	Player		*pl = e->pPlayer;

	entGetPos(e,pos);
	for(i=0;i<GOOD_POS_MAX;i++)
	{
		if (distance3Squared(pos,pl->last_good_pos[i]) < SQR(GOOD_POS_MINSTEP))
			return;
	}
	memmove(&pl->last_good_pos[1],&pl->last_good_pos[0],(GOOD_POS_MAX-1) * sizeof(pl->last_good_pos[0]));
	copyVec3(pos,pl->last_good_pos[0]);
}

#include "WorldLib.h"
#include "WorldColl.h"
#include "oldencounter_common.h"

static void player_ResetLastGoodPositions(Entity* e)
{
	Player* pl = e->pPlayer;
	int	i;
	Vec3 pos;
	entGetPos(e,pos);
	for(i=0;i<GOOD_POS_MAX;i++)
		copyVec3(pos, pl->last_good_pos[i]);
}

// Attempt to fix your character that is currently stuck inside something
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(stuck, unstuck);
void player_Stuck(Entity *e)
{
	int			i;
	F32			d2;
	Vec3		pos;
	Vec3		newpos;
	Player		*pl = e->pPlayer;
	WorldCollCollideResults	results = {0};
	Vec3		start,end;
	int			iPartitionIdx = entGetPartitionIdx(e);

	entGetPos(e,pos);

	//use a ray cast to determine if there is ground beneath the player
	if (!mrFlightGetEnabled(e->mm.mrFlight))
	{
		copyVec3(pos,start);
		copyVec3(pos,end);
		start[1] += 1.5;
		end[1] -= 10000;

		if (!worldCollideRay(iPartitionIdx, start, end, WC_QUERY_BITS_ENTITY_MOVEMENT, &results))
		{
			//no ground beneath player, move to a spawn point
			spawnpoint_MovePlayerToNearestSpawn(e, true, false);

			entGetPos(e,newpos);
			if (!sameVec3(pos, newpos) )
				goto success;
		}
	}
	//player is in flight or found ground beneath player or can't find spawn
	//scan for a last good point
	for(i=0;i<GOOD_POS_MAX;i++)
	{
		d2 = distance3Squared(pos,pl->last_good_pos[i]);
		if (d2 > SQR(GOOD_POS_MINSTEP) && d2 < SQR(GOOD_POS_MAXSTEP))
		{
			if (!mrFlightGetEnabled(e->mm.mrFlight))
			{
				copyVec3(pl->last_good_pos[i],start);
				copyVec3(pl->last_good_pos[i],end);
				start[1] += 1.5;
				end[1] -= 10000;
				if (!worldCollideRay(iPartitionIdx, start, end, WC_QUERY_BITS_ENTITY_MOVEMENT, &results))
					continue;
			}
			entSetPos(e,pl->last_good_pos[i],1,"stuck");
			goto success;
		}
	}

	//have ground under me but could not find a good last point
	//send a message to the player to notify them to use killme if they are still stuck
	{
		char * tmpS = NULL;
		estrStackCreate(&tmpS);
		FormatMessageKey(&tmpS, "StuckWarning", STRFMT_END);
		notify_NotifySend(e, kNotifyType_StuckWarning, tmpS, NULL, NULL);
		estrDestroy(&tmpS);
	}
success:
	//trash all of the last good points, set them = to new pos
	player_ResetLastGoodPositions(e);
}

static void player_StuckRespawn_LockMovement(Entity* e)
{
	if(!e->mm.mrDisabled)
	{
		mrDisableCreate(e->mm.movement, &e->mm.mrDisabled);
	}
}

static void player_StuckRespawn_UnlockMovement(Entity* e)
{
	mrDisableSetDestroySelfFlag(&e->mm.mrDisabled);
}

static void player_UpdateStuckRespawn(Entity* e)
{
	bool bDirty = false;

	if ( e->pPlayer->bStuckRespawn )
	{
		if ( !entCheckFlag(e, ENTITYFLAG_DEAD) && !e->pPlayer->iStasis )
		{
			if ( e->pPlayer->uiRespawnTime <= timeSecondsSince2000() )
			{
				player_StuckRespawn_UnlockMovement(e);

				spawnpoint_MovePlayerToNearestSpawn(e, true, true);

				player_ResetLastGoodPositions(e);

				e->pPlayer->bStuckRespawn = false;

				bDirty = true;
			}
			else
			{
				player_StuckRespawn_LockMovement(e);
			}
		}
		else
		{
			if ( !e->pPlayer->iStasis )
			{
				player_StuckRespawn_UnlockMovement(e);
			}
			e->pPlayer->bStuckRespawn = false;

			bDirty = true;
		}
	}

	if ( bDirty )
	{
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
}


#define KILLME_RESPAWN_TIME 30
#define KILLME_RESPAWN_REUSE_TIME 300
// Will kill your character. Only use as a last resort if there is no other way to get unstuck
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void player_killme(Entity *e)
{
	if ( !e || !e->pChar || !e->pPlayer )
		return;

	if ( gConf.bKillMeCommandRespawns )
	{
		if ( !e->pPlayer->bStuckRespawn && !e->pPlayer->iStasis && !entCheckFlag(e, ENTITYFLAG_DEAD) )
		{
			U32 uiValidRespawnTime = timeSecondsSince2000() - KILLME_RESPAWN_REUSE_TIME;
			char* tmpS = NULL;
			estrStackCreate(&tmpS);
			if ( e->pPlayer->uiRespawnTime < uiValidRespawnTime )
			{
				FormatMessageKey(&tmpS, "KillmeRespawnSuccess", STRFMT_INT("Seconds",KILLME_RESPAWN_TIME), STRFMT_END);
				notify_NotifySend(e, kNotifyType_StuckWarning, tmpS, NULL, NULL);
				e->pPlayer->uiRespawnTime = timeSecondsSince2000() + KILLME_RESPAWN_TIME;
				e->pPlayer->bStuckRespawn = true;
				player_StuckRespawn_LockMovement(e);
				entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
			}
			else
			{
				U32 uiWaitTime = e->pPlayer->uiRespawnTime - uiValidRespawnTime;
				FormatMessageKey(&tmpS, "KillmeRespawnWarning", STRFMT_INT("Seconds",uiWaitTime), STRFMT_END);
				notify_NotifySend(e, kNotifyType_StuckWarning, tmpS, NULL, NULL);
			}
			estrDestroy(&tmpS);
		}
	}
	else
	{
		if(e->pChar)
		{
			e->pChar->bKill = true;
			character_Wake(e->pChar);
		}
		else
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			entDie(e, -1, false, false, pExtract);
		}
	}
}

//Will kill your character without a message prompt
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(KillPlayer);
void player_killplayer(Entity *e)
{
	if(e->pChar)
	{
		e->pChar->bKill = true;
		character_Wake(e->pChar);
	}
	else
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		entDie(e, -1, false, false, pExtract);
	}
}

// Sets the stasis timer for a player.
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(Stasis);
void player_Stasis(Entity* pEnt) {

	S32 currentTime = timeSecondsSince2000();
	S32 stasisEndTime = currentTime + gProjectGameServerConfig.iStasisTime * 60;

	if(pEnt && pEnt->pChar)
	{
		// Check to see if movement already disabled by mrDisabledCSR
		if(!pEnt->mm.mrDisabledCSR)
		{
			// Disable Movement
			mrDisableCreate(pEnt->mm.movement, &pEnt->mm.mrDisabledCSR);
		}

		// Make player invisible
		gslEntitySetInvisibleTransient(pEnt, 1);

		// Set the ending time
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), "SetPlayerStasis", "set pPlayer.iStasis = %u", stasisEndTime);
	}
}

// Frees a player from stasis.
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(Unstasis);
void player_Unstasis(Entity* pEnt) {

	if(pEnt && pEnt->pChar)
	{
		//Re-enable movement
		mrDisableSetDestroySelfFlag(&pEnt->mm.mrDisabledCSR);

		// Make player visible
		gslEntitySetInvisibleTransient(pEnt, 0);

		// Set the ending time
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), "SetPlayerStasis", "set pPlayer.iStasis = %u", 0);
	}
}

static void player_UpdateStasis(Entity *pEnt) {
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->iStasis && pEnt->pPlayer->iStasis < timeSecondsSince2000()) {
		// Drop out of stasis.
		PERFINFO_AUTO_START("ExitStasis", 1);

		//Re-enable movement
		mrDisableSetDestroySelfFlag(&pEnt->mm.mrDisabledCSR);

		//Make player visible
		gslEntitySetInvisibleTransient(pEnt, 0);

		// Clear the stasis timer.
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), "SetPlayerStasis", "set pPlayer.iStasis = %u", 0);

		PERFINFO_AUTO_STOP();
	}
}

static void entity_UpdateTraining(int iPartitionIdx, Entity* pEnt, U32* puiTime)
{
	GameAccountDataExtract *pExtract;
	int i;
	U32 uiTime = *puiTime;

	if (pEnt->pChar==NULL || pEnt->pSaved==NULL)
		return;

	PERFINFO_AUTO_START_FUNC();

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	for (i = eaSize(&pEnt->pChar->ppTraining)-1; i >= 0; i--)
	{
		CharacterTraining* pTraining = pEnt->pChar->ppTraining[i];

		if (uiTime < pTraining->uiCompleteTime) //list is sorted by completion time
			break;

		if (!pTraining->bCompletionPending)
		{
			Entity* pOwner = entFromContainerID(iPartitionIdx, pEnt->pSaved->conOwner.containerType, pEnt->pSaved->conOwner.containerID);
			
			if (pOwner)
			{
				character_CompleteTraining(iPartitionIdx, pOwner, pEnt, pTraining, pExtract);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void player_UpdateTraining(Entity* pEnt, F32 fTimeElapsed)
{
	U32 uiTime;
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	iPartitionIdx = entGetPartitionIdx(pEnt);
	uiTime = timeSecondsSince2000();

	entity_UpdateTraining(iPartitionIdx, pEnt, &uiTime);

	if (pEnt->pSaved)
	{
		Entity_ForEveryPet(iPartitionIdx, pEnt, entity_UpdateTraining, &uiTime, false, false);
	}

	PERFINFO_AUTO_STOP();
}

static void gslMinigame_Tick(void)
{
	gslGameSpecific_Minigame_Tick();
}

void gslRunning_BeginFrame_GameSystems(void)
{
    U32 curTime = timeSecondsSince2000();
	F32					fTickTime = 0.0f;
	const F32			secondsElapsed = gGSLState.secondsElapsed.sim.prev;
	struct {
		PerfInfoGuard*	func;
		PerfInfoGuard*	depthOne;
	} piGuard;
	
	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard.func);
	gslFrameTimerAddInstance("gslRunning_BeginFrame_GameSystems");
	
	character_AccumulateTickTime(secondsElapsed, &fTickTime);

	#define GSL_PERFINFO_SECTION_START(name)	PERFINFO_AUTO_START_GUARD(name,1,&piGuard.depthOne)
	#define GSL_PERFINFO_SECTION_STOP()			PERFINFO_AUTO_STOP_GUARD(&piGuard.depthOne)

	gslFrameTimerAddInstance("CheckSavedPets");
	GSL_PERFINFO_SECTION_START("CheckSavedPets");
	{
		EntityIterator *iter;
		Entity *pent;

		// Check for saved pets that have not logged in yet
		iter = entGetIteratorSingleTypeAllPartitions(ENTITYFLAG_PET_LOGGING_IN, 0, GLOBALTYPE_ENTITYSAVEDPET);
		while(pent = EntityIteratorGetNext(iter))
		{
			if (pent->pSaved && entGetPartitionIdx(pent) != PARTITION_ORPHAN_PET) {
				Entity* pentOwner = entFromContainerIDAnyPartition(pent->pSaved->conOwner.containerType, pent->pSaved->conOwner.containerID);
				// Pets are not allowed to log in until the owner is loaded
				if (pentOwner && pentOwner->pChar && pentOwner->pChar->bLoaded) {
					gslSavedPetLoggedIn(pent, pentOwner);
				}
			} else {
				// Orphan pets can arrive in some situations and need to be cleaned up
				gslUnSummonSavedPet(pent);
			}
		}
		EntityIteratorRelease(iter);
	}
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("CheckSavedPets");

	gslFrameTimerAddInstance("CheckPlayers");
	GSL_PERFINFO_SECTION_START("CheckPlayers");
	{
		EntityIterator *iter;
		Entity *pent;

		iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		while(pent = EntityIteratorGetNext(iter))
		{
			// Look for players who are logging in and fix them up
			if (entCheckFlag(pent, ENTITYFLAG_PLAYER_DISCONNECTED) && entCheckFlag(pent, ENTITYFLAG_PLAYER_LOGGING_IN))
			{
				// Pretty much the earliest possible time I can make these subscriptions.  They also
				//  get fixed up once-per-tick once the player is farther along the login process.
				entity_PuppetSubscribe(pent);
				entity_PetSubscribe(pent);
			}

			// Look for players who are waiting for game account data and it has arrived
			if ((SAFE_MEMBER2(pent,pPlayer,eLoginWaiting) & kLoginSuccess_GameAccount) &&
				GET_REF(pent->pPlayer->pPlayerAccountData->hData) != NULL)
			{
				//Clear the game account waiting flag
				HandlePlayerLogin_Success(pent, kLoginSuccess_GameAccount);
			}

			// Check for puppet and pet states
			Entity_PuppetMasterAndPetTick(pent);

			// Do kill credit tick
			reward_KillCreditLimitTick(pent, secondsElapsed);

            // Periodically clear the project specific log string, which will force it to be regenerated the next time it is needed.
            //  The interval(in seconds) is configured per game via gConf.uProjSpecificLogStringUpdateInterval.
            // Check lastProjSpecificLogTime here to guard against clearing and regenerating the log string every frame if someone
            //  forgets to set lastProjSpecificLogTime.
            if ( gConf.uProjSpecificLogStringUpdateInterval && pent->estrProjSpecificLogString && pent->lastProjSpecificLogTime )
            {
                if ( pent->lastProjSpecificLogTime + gConf.uProjSpecificLogStringUpdateInterval < curTime )
                {
                    estrDestroy(&pent->estrProjSpecificLogString);
                }
            }
		}
		EntityIteratorRelease(iter);
	}
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("CheckPlayers");


	// Character loops
	if(fTickTime > 0.0f)
	{
		gslFrameTimerAddInstance("PowerTicks");

		gslFrameTimerAddInstance("GlobalTick");
		// If you're on a map that has bolstering enabled, 
		// bolster characters up to the required map level before character ticks
		mapState_UpdateCombatLevelsForAllPartitions();

		combat_GlobalTick(fTickTime);
		gslFrameTimerStopInstance("GlobalTick");

		gslFrameTimerAddInstance("TickOneLoop");
		GSL_PERFINFO_SECTION_START("CharacterTickOneLoop");
		{
			EntityIterator *iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE|ENTITYFLAG_DESTROY);
			Entity *pent;
			int client_count = 0;

			characterPhase_SetPhase(ECharacterPhase_ONE);

			while(pent = EntityIteratorGetNext(iter))
			{
				int iPartitionIdx = entGetPartitionIdx(pent);
				if(pent->pChar && !mapState_IsMapPausedForPartition(iPartitionIdx))
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);

	                character_TickPrePhaseOne(iPartitionIdx, pent->pChar);

					if(PERFINFO_RUN_CONDITIONS) {
						if(pent->pChar->fTimerSleep <= fTickTime) {
							ADD_MISC_COUNT(1,"WakeNow");
						}
						else if(pent->pChar->fTimerSleep <= fTickTime*2) {
							ADD_MISC_COUNT(1,"WakeNext");
						}
						else if(pent->pChar->fTimerSleep <= fTickTime*5) {
							ADD_MISC_COUNT(1,"WakeSoon");
						}
						else {
							ADD_MISC_COUNT(1,"WakeLate");
						}
					}

					if(pent->pChar->fTimerSleep <= fTickTime)
					{
						pent->pChar->bCombatTick = true;
						pent->pChar->fTimerSleep = 2; // Reset the sleep timer to 2 seconds for now
					}
					else
					{
						pent->pChar->bCombatTick = !s_bCombatSleep;
					}

					if(pent->pChar->bCombatTick)
 						character_TickPhaseOne(iPartitionIdx, pent->pChar,fTickTime+pent->pChar->fTimeSlept,pExtract);
				}

				if( pent->pPlayer )
					client_count++;
			}
			EntityIteratorRelease(iter);
			g_errorClientCount = client_count; // For sending to the ErrorTracker upon crashing
		}
		GSL_PERFINFO_SECTION_STOP();
		gslFrameTimerStopInstance("TickOneLoop");

		gslFrameTimerAddInstance("TickTwoLoop");
		GSL_PERFINFO_SECTION_START("CharacterTickTwoLoop");
		{
			EntityIterator *iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE|ENTITYFLAG_DESTROY);
			Entity *pent;
			
			characterPhase_SetPhase(ECharacterPhase_TWO);
			
			while(pent = EntityIteratorGetNext(iter))
			{
				int iPartitionIdx = entGetPartitionIdx(pent);
				if(pent->pChar && !mapState_IsMapPausedForPartition(iPartitionIdx))
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);

					if(PERFINFO_RUN_CONDITIONS) {
						if(pent->pChar->fTimerSleep <= fTickTime) {
							ADD_MISC_COUNT(1,"WakeNow");
						}
						else if(pent->pChar->fTimerSleep <= fTickTime*2) {
							ADD_MISC_COUNT(1,"WakeNext");
						}
						else if(pent->pChar->fTimerSleep <= fTickTime*5) {
							ADD_MISC_COUNT(1,"WakeSoon");
						}
						else {
							ADD_MISC_COUNT(1,"WakeLate");
						}
					}

					if(pent->pChar->bCombatTick)
						character_TickPhaseTwo(iPartitionIdx,pent->pChar,fTickTime+pent->pChar->fTimeSlept,pExtract);

					if (entGetType(pent) == GLOBALTYPE_ENTITYPLAYER)
					{
						PERFINFO_AUTO_START("interaction_OncePerFrameInteractTick",1);
						interaction_OncePerFrameInteractTick(pent,fTickTime);
						itemUpgrade_Tick(pent,fTickTime);
						PERFINFO_AUTO_STOP();

						PERFINFO_AUTO_START("AdditionalPlayerWork",1);
						player_RecordLastGoodPosition(pent);
						player_UpdateStasis(pent);
						player_UpdateTraining(pent,fTickTime);
						player_UpdateStuckRespawn(pent);
						PERFINFO_AUTO_STOP();
					}
				}
			}
			EntityIteratorRelease(iter);
		}
		GSL_PERFINFO_SECTION_STOP();
		gslFrameTimerStopInstance("TickTwoLoop");

		gslFrameTimerAddInstance("TickThreeLoop");
		GSL_PERFINFO_SECTION_START("CharacterTickThreeLoop");
		{
			EntityIterator *iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE|ENTITYFLAG_DESTROY);
			Entity *pent;

			characterPhase_SetPhase(ECharacterPhase_THREE);
			
			while(pent = EntityIteratorGetNext(iter))
			{
				int iPartitionIdx = entGetPartitionIdx(pent);
				if(pent->pChar && !mapState_IsMapPausedForPartition(iPartitionIdx))
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);

					if(PERFINFO_RUN_CONDITIONS) {
						if(pent->pChar->fTimerSleep <= fTickTime) {
							ADD_MISC_COUNT(1,"WakeNow");
						}
						else if(pent->pChar->fTimerSleep <= fTickTime*2) {
							ADD_MISC_COUNT(1,"WakeNext");
						}
						else if(pent->pChar->fTimerSleep <= fTickTime*5) {
							ADD_MISC_COUNT(1,"WakeSoon");
						}
						else {
							ADD_MISC_COUNT(1,"WakeLate");
						}
					}

					if(pent->pChar->bCombatTick)
					{
						character_TickPhaseThree(iPartitionIdx,pent->pChar,fTickTime+pent->pChar->fTimeSlept,pExtract);
						pent->pChar->fTimeSlept = 0;
					}
					else
					{
						pent->pChar->fTimeSlept += fTickTime;
					}

					character_SetSleep(pent->pChar,pent->pChar->fTimerSleep-fTickTime);
					pent->pChar->bCombatTick = false;
				}
			}
			EntityIteratorRelease(iter);
		}
		GSL_PERFINFO_SECTION_STOP();
		gslFrameTimerStopInstance("TickThreeLoop");
				
		characterPhase_SetPhase(ECharacterPhase_NONE);

		gslFrameTimerStopInstance("PowerTicks");
	}

	gslFrameTimerAddInstance("TeamTick");
	GSL_PERFINFO_SECTION_START("TeamTick");
	gslTeam_Tick();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("TeamTick");

	gslFrameTimerAddInstance("MotionTick");
	GSL_PERFINFO_SECTION_START("MotionTick");
	im_InteractionTimerTick(fTickTime);
	im_MotionTrackerTick(fTickTime);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("MotionTick");
		
	GSL_PERFINFO_SECTION_START("GameServerLibTick");
	gslFrameTimerAddInstance("GameServerLibTick");
	{
		EntityIterator *iter = entGetIteratorAllTypesAllPartitions(0, ENTITYFLAG_DESTROY | ENTITYFLAG_IGNORE);
		Entity *pent;
		
		while(pent = EntityIteratorGetNext(iter))
		{
			if (entGetType(pent) != GLOBALTYPE_ENTITYPROJECTILE)
			{
				if(!entCheckFlag(pent, ENTITYFLAG_CIV_PROCESSING_ONLY))
				{
					if(ENTACTIVE(pent))
					{
						PERFINFO_AUTO_START("gslEFU-Active",1);
					}
					else
					{
						PERFINFO_AUTO_START("gslEFU-Inactive",1);
					}

					gslEntityFrameUpdate(pent);

					PERFINFO_AUTO_STOP();
				}
			}
			else 
			{
				// projectile entity update
				gslProjectile_UpdateProjectile(fTickTime, pent);
			}
			
		}
		EntityIteratorRelease(iter);
	}
	gslFrameTimerStopInstance("GameServerLibTick");
	GSL_PERFINFO_SECTION_STOP();

	gslFrameTimerAddInstance("AITick+AICivTick");
	GSL_PERFINFO_SECTION_START("AITick");
	aiTickBuckets();
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("AICivTick");
	aiCivilianTickBuckets();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("AITick+AICivTick");

	gslFrameTimerAddInstance("PresenceTick+PvPTick+LocalTeamTick");
	GSL_PERFINFO_SECTION_START("Entity Presence Tick");
	{
		gslEntityPresenceTick();
	}
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("PVP Tick");
	{
		gslPVPTick();
	}
	GSL_PERFINFO_SECTION_STOP();

	//Local team tick must happen after the pvp tick
	GSL_PERFINFO_SECTION_START("LocalTeamTick");
	gslQueue_LocalTeamTick(fTickTime);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("PresenceTick+PvPTick+LocalTeamTick");

	aiForceProxUpdates(1);

	gslFrameTimerAddInstance("EntityCleanup");
	GSL_PERFINFO_SECTION_START("Entity Cleanup");
	{
		EntityIterator *iter = entGetIteratorMultipleTypesAllPartitions(0, 0, GLOBALTYPE_ENTITYCRITTER, GLOBALTYPE_ENTITYPROJECTILE, GLOBALTYPE_NONE /* varargs terminator */);
		Entity *pent;

		while(pent = EntityIteratorGetNext(iter))
		{
			//DO NOT clean up dead entity pets. They will get cleaned up on their own
			if (entGetType(pent) == GLOBALTYPE_ENTITYCRITTER &&
				pent->pCritter && 
				(entCheckFlag(pent, ENTITYFLAG_DESTROY) || (!entIsAlive(pent) && (gConf.bAutomaticallyCleanUpDeadPets || !entCheckFlag(pent, ENTITYFLAG_CRITTERPET)))))
			{
				// Make sure it is less than 0 so that it doesn't cleanup on the tick it dies
				// This gives any other system a chance to revive the dead critter if necessary
				if (entCheckFlag(pent, ENTITYFLAG_DESTROY) || pent->pCritter->timeToLinger < 0.0)
					gslDestroyEntity(pent);
				else
					pent->pCritter->timeToLinger -= secondsElapsed;
			}
			else if (entGetType(pent) == GLOBALTYPE_ENTITYPROJECTILE && 
					 (entCheckFlag(pent, ENTITYFLAG_DESTROY) || gslProjectile_ShouldDestroy(pent)))
			{
				gslDestroyEntity(pent);
			}
		}
		EntityIteratorRelease(iter);
	}
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("EntityCleanup");

	aiForceProxUpdates(0);

	gslFrameTimerAddInstance("timedeventqueue_UpdateTimers");
	GSL_PERFINFO_SECTION_START("TimedEventQueueUpdate");
	timedeventqueue_UpdateTimers();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("timedeventqueue_UpdateTimers");

	gslFrameTimerAddInstance("TriggerConditions+LayerFSM+PlayerFSM");
	GSL_PERFINFO_SECTION_START("TriggerConditionsUpdate");
	triggercondition_UpdateTriggerConditions();
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("LayerFSMsUpdate");
	layerfsm_OncePerFrame();
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("PlayerFSMsUpdate");
	pfsmOncePerFrame();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("TriggerConditions+LayerFSM+PlayerFSM");

	gslFrameTimerAddInstance("ShardVariable+Diary+Mechanics");
	GSL_PERFINFO_SECTION_START("ShardVariablesUpdate");
	shardvariable_OncePerFrame();
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("DiaryUpdate");
	gslDiary_RunOncePerFrame();
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("DiaryUpdate");
	gslCurrencyExchange_OncePerFrame();
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("MechanicsUpdate");
	mechanics_OncePerFrame(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("ShardVariable+Diary+Mechanics");

	gslFrameTimerAddInstance("Interaction OncePerFrame");
	GSL_PERFINFO_SECTION_START("InteractedObjectsUpdate");
	interaction_OncePerFrameTimerUpdate(secondsElapsed, false);
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("InteractablesUpdate");
	interactable_OncePerFrame(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Interaction OncePerFrame");

	gslFrameTimerAddInstance("Mission");
	GSL_PERFINFO_SECTION_START("MissionUpdate");
	mission_OncePerFrame(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Mission");

	gslFrameTimerAddInstance("Progression");
	GSL_PERFINFO_SECTION_START("ProgressionUpdate");
	gslProgression_OncePerFrame(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Progression");

	gslFrameTimerAddInstance("Contact");
	GSL_PERFINFO_SECTION_START("ContactUpdate");
	contact_OncePerFrame(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Contact");

	gslFrameTimerAddInstance("Encounters OncePerFrame");
	GSL_PERFINFO_SECTION_START("EncounterSystemUpdate");
	encounter_OncePerFrame(secondsElapsed);
	if (gConf.bAllowOldEncounterData)
		oldencounter_OncePerFrame(secondsElapsed);
	encounterdebug_OncePerFrame();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Encounters OncePerFrame");

	gslFrameTimerAddInstance("Cutscene OncePerFrame");
	GSL_PERFINFO_SECTION_START("CutSceneUpdate");
	cutscene_OncePerFrame(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Cutscene OncePerFrame");

	gslFrameTimerAddInstance("TimeControl+MapState+NPCCallout");
	GSL_PERFINFO_SECTION_START("TimeControlUpdate");
	timecontrol_OncePerFrame();
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("MapStateUpdate");
	mapState_UpdateAllPartitions(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();

	GSL_PERFINFO_SECTION_START("NPCCalloutUpdate");
	callout_ProcessCallout();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("TimeControl+MapState+NPCCallout");

	gslFrameTimerAddInstance("gslScoreboard_Tick");
	GSL_PERFINFO_SECTION_START("ScoreboardTick");
	gslScoreboard_Tick(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("gslScoreboard_Tick");

	gslFrameTimerAddInstance("queue_TickQueues");
	GSL_PERFINFO_SECTION_START("QueueTick");
	queue_TickQueues(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("queue_TickQueues");

	gslFrameTimerAddInstance("gslPVPGame_TickGames");
	GSL_PERFINFO_SECTION_START("PVPTick");
	gslPVPGame_TickGames(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("gslPVPGame_TickGames");

	gslFrameTimerAddInstance("gslChat_Tick");
	GSL_PERFINFO_SECTION_START("ChatTick");
	gslChat_Tick();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("gslChat_Tick");

	gslFrameTimerAddInstance("gslEvent_Tick");
	GSL_PERFINFO_SECTION_START("EventTick");
	gslEvent_Tick(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("gslEvent_Tick");

	gslFrameTimerAddInstance("gslLogoff_Tick");
	GSL_PERFINFO_SECTION_START("LogoffTick");
	gslLogoff_Tick(secondsElapsed);
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("gslLogoff_Tick");

	gslFrameTimerAddInstance("Minigames+Interior");
	GSL_PERFINFO_SECTION_START("Minigames");
	gslMinigame_Tick();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Minigames+Interior");

	GSL_PERFINFO_SECTION_START("Interior");
	gslInterior_Tick();
	GSL_PERFINFO_SECTION_STOP();

	gslFrameTimerAddInstance("gslUGC_Tick");
	GSL_PERFINFO_SECTION_START("UGC");
	gslUGC_Tick();
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("gslUGC_Tick");

    gslFrameTimerAddInstance("gslGroupProject_PerFrame");
    GSL_PERFINFO_SECTION_START("GroupProject");
    gslGroupProject_PerFrame();
    GSL_PERFINFO_SECTION_STOP();
    gslFrameTimerStopInstance("gslGroupProject_PerFrame");

    gslFrameTimerAddInstance("gslMapPopulationCache_BeginFrame");
    GSL_PERFINFO_SECTION_START("MapPopulationCache");
    gslMapPopulationCache_BeginFrame();
    GSL_PERFINFO_SECTION_STOP();
    gslFrameTimerStopInstance("gslMapPopulationCache_BeginFrame");

	// not putting any perf/timers here because this should be lightweight enough to not care.
	gslCombatDeathPrediction_Tick();

	gslFrameTimerAddInstance("DebugUpdate");
	GSL_PERFINFO_SECTION_START("DebugUpdate");
	{
		EntityIterator *iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_PLAYER_DISCONNECTED, GLOBALTYPE_ENTITYPLAYER);
		Entity *pEnt;
		g_uiCombatDebugPerf = 0;
		while(pEnt = EntityIteratorGetNext(iter))
		{
			PlayerDebug* pPlayerDebug = entGetPlayerDebug(pEnt, false);

			if(pPlayerDebug)
			{
				ClientLink* pLink = entGetClientLink(pEnt);

				if(pLink)
					eaiClear(&pLink->debugEntities);

				if (pLink && pPlayerDebug->erCombatDebug)
				{
					CombatDebug *pDbg = combatdebug_GetData(pPlayerDebug->erCombatDebug);
					Packet * pPacket = pktCreate(pLink->netLink, TOCLIENT_ENTITY_DEBUG);
					pktSendF32(pPacket,fTickTime);
					pktSendStruct(pPacket, pDbg, parse_CombatDebug);
					pktSend(&pPacket);
					combatdebug_Destroy(pDbg);

					gslAddDebugEntityToLinkByRef(pLink, pPlayerDebug->erCombatDebug);
				}
				if (pPlayerDebug->combatDebugPerf)
				{
					combatdebug_PerfPlayerUpdate(pEnt);
					g_uiCombatDebugPerf++;
				}
				if (pPlayerDebug->showServerFPS)
				{
					pPlayerDebug->currServerFPS = gGSLState.server_fps;
					pPlayerDebug->clientsLoggedInCount = gGSLState.clients.loggedInCount;
					pPlayerDebug->clientsNotLoggedInCount = gGSLState.clients.notLoggedInCount;
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
				}
				encounterdebug_Update(pEnt);
				if(pPlayerDebug->aiDebugInfo)
					aiDebugUpdate(pEnt, pPlayerDebug->aiDebugInfo);
			}
		}
		EntityIteratorRelease(iter);
	}
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("DebugUpdate");

	gslFrameTimerAddInstance("Guild Stat Update");
	GSL_PERFINFO_SECTION_START("Entity Temporary Power Evaluation During Guild Stat Update");
	{
		EntityIterator *iter = entGetIteratorSingleTypeAllPartitions(0,0,GLOBALTYPE_ENTITYPLAYER);
		Entity *pent;

		while(pent = EntityIteratorGetNext(iter))
		{
			// Evaluate temporary powers whenever their guild stats change
			if (pent->pPlayer)
			{
				Guild *pPlayerGuild = guild_GetGuild(pent);
				if (pPlayerGuild)
				{
					U32 iGuildStatVersion = pPlayerGuild->pGuildStatsInfo ? pPlayerGuild->pGuildStatsInfo->uiVersion : 0;
					U32 iLastProcessedVersion = pent->pPlayer->uiLastGuildStatsInfoVersion;

					// Did the guild stats change since we last updated the temporary powers for this entity
					if (iGuildStatVersion != iLastProcessedVersion || !pent->pPlayer->bGuildInitialTemporaryPowerEvaluationDone)
					{
						int iPartitionIdx = entGetPartitionIdx(pent);

						// Set the initial update flag to true
						pent->pPlayer->bGuildInitialTemporaryPowerEvaluationDone = true;

						// Update the temporary powers for this entity
						if (character_UpdateTemporaryPowerTrees(iPartitionIdx,pent->pChar))
						{
							GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);
							character_ResetPowersArray(iPartitionIdx,pent->pChar, pExtract);
						}

						// Update the version on the entity
						pent->pPlayer->uiLastGuildStatsInfoVersion = iGuildStatVersion;

						// Set the dirty bit
						entity_SetDirtyBit(pent, parse_Player, pent->pPlayer, true);
					}
				}
				else if (pPlayerGuild == NULL && pent->pPlayer->uiLastGuildStatsInfoVersion != -1)
				{
					int iPartitionIdx = entGetPartitionIdx(pent);

					// Update the temporary powers for this entity
					if (character_UpdateTemporaryPowerTrees(iPartitionIdx,pent->pChar))
					{
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);
						character_ResetPowersArray(iPartitionIdx, pent->pChar, pExtract);
					}

					// When the player leaves a guild we process them only one time.
					// We achieve this by setting the version number to -1.					
					pent->pPlayer->uiLastGuildStatsInfoVersion = -1;

					// Set the dirty bit
					entity_SetDirtyBit(pent, parse_Player, pent->pPlayer, true);
				}

			}
		}
		EntityIteratorRelease(iter);
	}
	GSL_PERFINFO_SECTION_STOP();
	gslFrameTimerStopInstance("Guild Stat Update");
	
	gslFrameTimerStopInstance("gslRunning_BeginFrame_GameSystems");

	#undef GSL_PERFINFO_SECTION_START
	#undef GSL_PERFINFO_SECTION_STOP

	PERFINFO_AUTO_STOP_GUARD(&piGuard.func);
}

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/MapDescription_h_ast.h"

/*

AUTO_COMMAND ACMD_HIDE;
void TestAutoTrans(Entity *ent, int accessLevel)
{
	TransactionReturnVal *returnVal = objCreateManagedReturnVal(objTransactionPrintResultCB,NULL);
	int containerID = entGetContainerID(ent);
	U32 *pArrayOfIDs = NULL;
	ea32Push(&pArrayOfIDs, containerID);
	
	AutoTrans_trSetAccessLevel(returnVal, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, &pArrayOfIDs, accessLevel);

	ea32Destroy(&pArrayOfIDs);
}

AUTO_TRANS_HELPER;
void SetAccessLevelHelper(ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) pEnts, int accesslevel)
{
	int i;
	for (i = 0; i < eaSize(&pEnts); i++)
	{
		pEnts[i]->pPlayer->accessLevel = accesslevel;
	}
}


AUTO_TRANSACTION;
enumTransactionOutcome trSetAccessLevel(ATR_ARGS, CONST_EARRAY_OF(NOCONST(Entity)) pEnts, int accesslevel)
{
	SetAccessLevelHelper(pEnts, accesslevel);
	
	return TRANSACTION_OUTCOME_SUCCESS;
	
}


#include "character_h_ast.h"

AUTO_TRANS_HELPER;
void aaTestHelper(ATH_ARG NOCONST(Entity) *pEnt, int index)
{
	AttribModNet *pAttribMode = eaIndexedGetUsingInt(&pEnt->pChar->ppModsNet, index);
}


AUTO_TRANSACTION;
enumTransactionOutcome aaTest(ATR_ARGS, NOCONST(Entity) *pEnt, int x, int y)
{
	AttribModNet *pAttribMode = eaIndexedGetUsingInt(&pEnt->pChar->ppModsNet, x);
	aaTestHelper(pEnt, y);
	aaTestHelper(pEnt, 23);

	return TRANSACTION_OUTCOME_SUCCESS;
}
*/







/*AUTO_COMMAND;
void TestStructAutoTrans(Entity *ent, char *mapName)
{
	MapDescription description = {0};
	TransactionReturnVal *returnVal = objCreateManagedReturnVal(objTransactionPrintResultCB,NULL);
	int containerID = entGetContainerID(ent);
	char *queryString;

	sprintf(description.mapDescription,mapName);
	sprintf(description.mapDetail,"");
	description.mapType = MAPTYPE_NORMAL;
	description.mapInstanceIndex = 0;

	// This is basically what the wrapper should do

	estrStackCreate(&queryString,1000);
	estrConcatf(&queryString,"trChangeCurrentMap");

	estrConcatf(&queryString," %s[%d]",GlobalTypeToName(GLOBALTYPE_ENTITYPLAYER),containerID);

	estrConcatf(&queryString," \"");
	ParserWriteTextEscaped(&queryString,parse_MapDescription,&description,0,0);
	estrConcatf(&queryString,"\"");
	


	objRequestAutoTransaction(returnVal,queryString);

	estrDestroy(&queryString);
}

AUTO_TRANSACTION;
enumTransactionOutcome trChangeCurrentMap(ATR_ARGS, NOCONST(Entity) *player, MapDescription *newDescription)
{
	

	NOCONST(SavedMapDescription)* pDescription = player->pPlayer->currentMap;

	*((MapDescription *)pDescription) = *newDescription;

	return TRANSACTION_OUTCOME_SUCCESS;
	
}


AUTO_TRANSACTION;
enumTransactionOutcome TransferMoney(ATR_ARGS, NOCONST(Entity) *pFrom, NOCONST(Entity) *pTo, int iAmount)
{


 
  return TRANSACTION_OUTCOME_SUCCESS;

} */

