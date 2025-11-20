/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManagerPrivate.h"
#include "EntityMovementDefault.h"
#include "EntityMovementInteraction.h"
#include "cmdparse.h"
#include "WorldGrid.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

static void mmPerEntityTimers(S32 enabled){
	if(	enabled &&
		!mgState.debug.perEntityTimers)
	{
		SAFE_FREE(mgState.debug.perEntityTimers);

		mgState.debug.perEntityTimers = callocStruct(MovementPerEntityTimers);
	}

	mgState.debug.flags.perEntityTimers = !!enabled;
}

static void mmOpenPerEntityTimer(U32 index){
	if(mgState.debug.flags.perEntityTimers){
		ARRAY_FOREACH_BEGIN(mgState.debug.perEntityTimers->isOpen, i);
			U32 timerIndex = index / mmTimerGroupSizes[i];

			if(!mgState.debug.perEntityTimers->isOpen[i][timerIndex]){
				mgState.debug.perEntityTimers->isOpen[i][timerIndex] = 1;
				break;
			}
		ARRAY_FOREACH_END;
	}
}

AUTO_COMMAND ACMD_NAME("mmPerEntityTimersServer") ACMD_SERVERONLY;
void mmCmdPerEntityTimersServer(S32 enabled){
	mmPerEntityTimers(enabled);
}

AUTO_COMMAND ACMD_NAME("mmPerEntityTimersClient") ACMD_CLIENTONLY;
void mmCmdPerEntityTimersClient(S32 enabled){
	mmPerEntityTimers(enabled);
}

AUTO_COMMAND ACMD_NAME("mmOpenPerEntityTimerServer") ACMD_SERVERONLY;
void mmCmdOpenPerEntityTimerServer(U32 index){
	mmOpenPerEntityTimer(index);
}

AUTO_COMMAND ACMD_NAME("mmOpenPerEntityTimerClient") ACMD_CLIENTONLY;
void mmCmdOpenPerEntityTimerClient(U32 index){
	mmOpenPerEntityTimer(index);
}

AUTO_COMMAND ACMD_NAME("Physics.Predict", "mmPredict") ACMD_CLIENTONLY;
void mmCmdSetPrediction(S32 enabled){
	mgState.fg.flagsMutable.predictDisabled = !enabled;
}

AUTO_COMMAND ACMD_NAME("mmLogSkeletons") ACMD_CLIENTONLY;
void mmCmdSetSkeletonLogging(S32 enabled){
	mgState.fg.flagsMutable.logSkeletons = !!enabled;
}

AUTO_COMMAND ACMD_NAME("Physics.NoSync", "mmNoSync") ACMD_CLIENTONLY;
void mmCmdSetNoSyncWithServer(S32 enabled){
	mmSetSyncWithServer(!enabled);
	
	if(!enabled){
		globCmdParse("forceFullEntityUpdate");
	}
}

AUTO_COMMAND ACMD_NAME("Physics.NoDisable", "mmNoDisable") ACMD_CLIENTONLY;
void mmCmdSetNoDisable(S32 enabled){
	mmSetNoDisable(enabled);
}

AUTO_COMMAND ACMD_NAME("mmSetInputBufferSize") ACMD_SERVERONLY;
void mmCmdSetInputBufferSize(	Entity* e,
								U32 size)
{
	MovementManager* mm = SAFE_MEMBER(e, mm.movement);
	
	if(SAFE_MEMBER(mm, fg.mcma)){
		mm->fg.mcma->mc->stats.inputBufferSize = size;
	}
}

AUTO_COMMAND ACMD_NAME("mmLogUnmanagedResourcesServer") ACMD_SERVERONLY;
void mmCmdLogUnmanagedResourcesServer(S32 enabled){
	mgState.flagsMutable.logUnmanagedResources = !!enabled;
}

AUTO_COMMAND ACMD_NAME("mmLogManagedResourcesServer") ACMD_SERVERONLY;
void mmCmdPrintManagedResourceLogServer(S32 enabled){
	mgState.flagsMutable.logManagedResources = !!enabled;
}

AUTO_COMMAND ACMD_NAME("mmLogUnmanagedResourcesClient") ACMD_CLIENTONLY;
void mmCmdLogUnmanagedResourcesClient(S32 enabled){
	mgState.flagsMutable.logUnmanagedResources = !!enabled;
}

AUTO_COMMAND ACMD_NAME("mmLogManagedResourcesClient") ACMD_CLIENTONLY;
void mmCmdLogManagedResourcesClient(S32 enabled){
	mgState.flagsMutable.logManagedResources = !!enabled;
}

AUTO_COMMAND ACMD_NAME("mmAlwaysSetCurrentView") ACMD_CLIENTONLY;
void mmCmdAlwaysSetCurrentView(S32 enabled){
	mgState.fg.flagsMutable.alwaysSetCurrentView = !!enabled;
}

AUTO_RUN;
void mmRegisterMsgHandlers(void)
{
	// Register movement requester class IDs.

	mrNameRegisterID("SurfaceMovement",				MR_CLASS_ID_SURFACE);
	mrNameRegisterID("PowersMovement",				MR_CLASS_ID_POWERS);
	mrNameRegisterID("TestMovement",				MR_CLASS_ID_TEST);
	mrNameRegisterID("PlatformMovement",			MR_CLASS_ID_PLATFORM);
	mrNameRegisterID("FlightMovement",				MR_CLASS_ID_FLIGHT);
	mrNameRegisterID("BeaconMovement",				MR_CLASS_ID_BEACON);
	mrNameRegisterID("DoorMovement",				MR_CLASS_ID_DOOR);
	mrNameRegisterID("DoorGeoMovement",				MR_CLASS_ID_DOOR_GEO);
	mrNameRegisterID("ProjectileMovement",			MR_CLASS_ID_PROJECTILE);
	mrNameRegisterID("PushMovement",				MR_CLASS_ID_PUSH);
	mrNameRegisterID("TargetedRotationMovement",	MR_CLASS_ID_TARGETED_ROTATION);
	mrNameRegisterID("DisableMovement",				MR_CLASS_ID_DISABLE);
	mrNameRegisterID("SimBodyMovement",				MR_CLASS_ID_SIMBODY);
	mrNameRegisterID("RagDollMovement",				MR_CLASS_ID_RAGDOLL);
	mrNameRegisterID("SwingMovement",				MR_CLASS_ID_SWING);
	mrNameRegisterID("TacticalMovement",			MR_CLASS_ID_TACTICAL);
	mrNameRegisterID("DeadMovement",				MR_CLASS_ID_DEAD);
	mrNameRegisterID("InteractionMovement",			MR_CLASS_ID_INTERACTION);
	mrNameRegisterID("GrabMovement",				MR_CLASS_ID_GRAB);
	mrNameRegisterID("EmoteMovement",				MR_CLASS_ID_EMOTE);
	mrNameRegisterID("DragonMovement",				MR_CLASS_ID_DRAGON);
	
	// Register movement managed resource class IDs.

	mmRegisterManagedResourceClassID("Fx",			MMR_CLASS_ID_FX);
	mmRegisterManagedResourceClassID("Attachment",	MMR_CLASS_ID_ATTACHMENT);
	mmRegisterManagedResourceClassID("Offset",		MMR_CLASS_ID_OFFSET);
	mmRegisterManagedResourceClassID("Body",		MMR_CLASS_ID_BODY);
	mmRegisterManagedResourceClassID("Skeleton",	MMR_CLASS_ID_SKELETON);
	mmRegisterManagedResourceClassID("Draw",		MMR_CLASS_ID_DRAW);
	mmRegisterManagedResourceClassID("HitReact",	MMR_CLASS_ID_HIT_REACT);
}

AUTO_CMD_INT(mgState.bg.threads.desiredThreadCount, mmThreadCount);

AUTO_COMMAND;
void mmSendTriggerID(U32 triggerID){
	MovementTrigger t = {0};

	t.triggerID = triggerID;	
	
	mmAllTriggerSend(&t);
}

AUTO_COMMAND;
void mmPrintRemoveStatesToBG(void){
#if MM_TRACK_ALL_RESOURCE_REMOVE_STATES_TOBG
	StashTableIterator	it;
	StashElement		element;
	
	stashGetIterator(mgState.debug.stRemoveStatesToBG, &it);
	
	while(stashGetNextElement(&it, &element)){
		MovementManagedResource* mmr = stashElementGetPointer(element);
		
		printf(	"mmr 0x%8.8p\n",
				mmr);
	}
#endif
}

AUTO_COMMAND;
void mmDisableNetBufferAdjustment(S32 set){
	mgState.flagsMutable.disableNetBufferAdjustment = !!set;
}

AUTO_COMMAND;
void mmNetBufferSkipSeconds(F32 seconds){
	mgState.fg.netViewMutable.spcOffsetFromEnd.skip += seconds * MM_PROCESS_COUNTS_PER_SECOND;
}

static S32 netStatsPaused;
AUTO_CMD_INT(netStatsPaused, netTimingGraphPaused) ACMD_CALLBACK(mmNetTimingGraphPaused) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void mmNetTimingGraphPaused(void){
	netStatsPaused = !!netStatsPaused;
	mgState.flagsMutable.netStatsPaused = netStatsPaused;
}

AUTO_COMMAND;
void mmPrintAllocationCounts(S32 enabled){
	mgState.flagsMutable.printAllocationCounts = !!enabled;
}

AUTO_COMMAND;
void mmPrintAllocationsPerManager(void){
	U32		mrCountTotal = 0;
	U32 	mmrCountTotal = 0;
	U32		mrcCountTotal[100] = {0};
	U32		mmrcCountTotal[100] = {0};
	char	buffer[1000];

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager*	mm = mgState.fg.managers[i];
		U32					mrcCount[100] = {0};
		U32					mmrcCount[100] = {0};
		
		sprintf(buffer,
				"0x%8.8x: %5u mr, %5u mmr: ",
				mm->entityRef,
				eaSize(&mm->fg.requesters),
				eaSize(&mm->fg.resources));

		mrCountTotal += eaSize(&mm->fg.requesters);
		mmrCountTotal += eaSize(&mm->fg.resources);
		
		EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, j, jsize);
		{
			MovementManagedResource*	mmr = mm->fg.resources[j];
			U32							id = mmr->mmrc->id;

			if(id < ARRAY_SIZE(mmrcCount)){
				mmrcCount[id]++;
				mmrcCountTotal[id]++;
			}
		}
		EARRAY_FOREACH_END;
		
		ARRAY_FOREACH_BEGIN(mmrcCount, j);
		{
			if(mmrcCount[j]){
				MovementManagedResourceClass* mmrc = mgState.mmr.idToClass[j];
				
				strcatf(buffer, "%s(%u),", mmrc->name, mmrcCount[j]);
			}
		}
		ARRAY_FOREACH_END;
		
		EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, j, jsize);
		{
			MovementRequester*	mr = mm->fg.requesters[j];
			U32					id = mr->mrc->id;
			
			if(id < ARRAY_SIZE(mrcCount)){
				mrcCount[id]++;
				mrcCountTotal[id]++;
			}
		}
		EARRAY_FOREACH_END;
		
		ARRAY_FOREACH_BEGIN(mrcCount, j);
		{
			if(mrcCount[j]){
				MovementRequesterClass* mrc = mgState.mr.idToClass[j];
				
				char nameTrimmed[100];
				strcpy(nameTrimmed, mrc->name);
				if(strEndsWith(nameTrimmed, "Movement")){
					nameTrimmed[strlen(nameTrimmed) - strlen("Movement")] = 0;
				}
				strcatf(buffer, "%s(%u),", nameTrimmed, mrcCount[j]);
			}
		}
		ARRAY_FOREACH_END;

		printf("%s\n", buffer);
	}
	EARRAY_FOREACH_END;
	
	// Print the totals.
	
	sprintf(buffer,
			"Total     : %5u mr, %5u mmr: ",
			mrCountTotal,
			mmrCountTotal);
	
	ARRAY_FOREACH_BEGIN(mmrcCountTotal, j);
	{
		if(mmrcCountTotal[j]){
			MovementManagedResourceClass* mmrc = mgState.mmr.idToClass[j];
			
			strcatf(buffer, "%s(%u),", mmrc->name, mmrcCountTotal[j]);
		}
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(mrcCountTotal, j);
	{
		if(mrcCountTotal[j]){
			MovementRequesterClass* mrc = mgState.mr.idToClass[j];
			
			char nameTrimmed[100];
			strcpy(nameTrimmed, mrc->name);
			if(strEndsWith(nameTrimmed, "Movement")){
				nameTrimmed[strlen(nameTrimmed) - strlen("Movement")] = 0;
			}
			strcatf(buffer, "%s(%u),", nameTrimmed, mrcCountTotal[j]);
		}
	}
	ARRAY_FOREACH_END;

	printf("%s\n", buffer);
}

AUTO_COMMAND ACMD_SERVERONLY;
void mmForceSim(S32 enabled){
	mgState.fg.flagsMutable.forceSimEnabled = !!enabled;
}

static MovementManager* mmClient;

static void mmClientManagerMsgHandler(const MovementManagerMsg* msg){
	switch(msg->msgType){
	}
}

AUTO_COMMAND ACMD_NAME(mmClientOnlyDestroy) ACMD_CLIENTONLY;
void mmCmdClientOnlyDestroy(void){
	mmDestroy(&mmClient);
}

AUTO_COMMAND ACMD_NAME(mmClientOnlyCreate) ACMD_CLIENTONLY;
void mmCmdClientOnlyCreate(	const Vec3 pos,
							const Vec3 posTarget)
{
	MovementRequester* mrInteraction;

	mmCmdClientOnlyDestroy();

	mmCreate(	&mmClient,
				mmClientManagerMsgHandler,
				NULL,
				MAX_ENTITIES_PRIVATE,
				0,
				pos,
				worldGetActiveColl(PARTITION_CLIENT));
	
	mrSurfaceCreate(mmClient, NULL);
	
	if(mrInteractionCreate(mmClient, &mrInteraction)){
		MRInteractionPath*		p = StructAlloc(parse_MRInteractionPath);
		MRInteractionWaypoint*	wp;
		
		wp = StructAlloc(parse_MRInteractionWaypoint);
		eaPush(&p->wps, wp);
		copyVec3(posTarget, wp->pos);
		copyQuat(unitquat, wp->rot);
		
		mrInteractionSetPath(mrInteraction, &p);
	}
}

AUTO_COMMAND;
void mmNoLocalProcessing(S32 set){
	mmSetLocalProcessing(!set);
}

AUTO_COMMAND;
void mmLogOutputs(	S32 enableText,
					S32 enable3D,
					S32 enableNetText,
					S32 enableNet3D)
{
	mgState.debug.flags.logOutputsText = !!enableText;
	mgState.debug.flags.logOutputs3D = !!enable3D;
	mgState.debug.flags.logNetOutputsText = !!enableNetText;
	mgState.debug.flags.logNetOutputs3D = !!enableNet3D;
}

AUTO_COMMAND;
void mmDisableAnimAssert(S32 disable){
	mgState.flagsMutable.disableAnimAssert = !!disable;
}
