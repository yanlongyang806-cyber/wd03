
/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManagerPrivate.h"
#include "EntityMovementDefault.h"
#include "EntityMovementProjectile.h"
#include "entCritter.h" // for faction matrix
#include "MemoryPool.h"
#include "mutex.h"
#include "PhysicsSDK.h"
#include "qsortG.h"
#include "stringcache.h"
#include "strings_opt.h"
#include "ThreadSafeMemoryPool.h"
#include "WorldColl.h"
#include "net.h"
#include "BlockEarray.h"
#include "WorldGrid.h"
#include "StaticWorld/WorldGridPrivate.h"
#include "../wcoll/entworldcoll.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/EntityMovementManager_h_ast.c"
#include "AutoGen/EntityMovementManagerDemo_h_ast.c"
#include "FolderCache.h"
#include "WorldBounds.h"

#ifdef GAMECLIENT
#include "gclDemo.h"
#endif

// MS: Remove these #includes.
#include "wlBeacon.h"
#include "dynSkeleton.h"
#include "dynAnimInterface.h"
#include "dynBitField.h"
#include "dynNodeInline.h"

MovementManagerConfig g_MovementManagerConfig = { 0 };

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

MovementGlobalState			mgState;
MovementGlobalPublicState	mgPublic;

MovementAnimBitHandles		mmAnimBitHandles;

#define MAX_AVAILABLE_MCI_STEPS  32

#define MM_DEATH_PREDICTION_ANIM_ALLOWED_STEPS 2

AUTO_RUN;
void mmRegisterBits(void)
{
	ARRAY_FOREACH_BEGIN(mmAnimBitHandles.all, i);
	{
		mmAnimBitHandles.all[i] = ~0;
	}
	ARRAY_FOREACH_END;
	
	#define GET(name, flash) mmRegisteredAnimBitCreate(&mgState.animBitRegistry, name, flash, NULL)

	#define ANIM_BIT(var, name) var = GET(name, 0)
	ANIM_BIT(mgState.animBitHandle.animOwnershipReleased, "Ownership_Released");
	#undef ANIM_BIT

	#define ANIM_BIT(var, name) mmAnimBitHandles.var = GET(name, 0)
	ANIM_BIT(flight, "Flight");
	ANIM_BIT(move, "Move");
	ANIM_BIT(moving, "Moving");
	ANIM_BIT(forward, "Forward");
	ANIM_BIT(bankLeft, "BankLeft");
	ANIM_BIT(bankRight, "BankRight");
	ANIM_BIT(run, "Run");
	ANIM_BIT(running, "Running");
	ANIM_BIT(trot, "Trot");
	ANIM_BIT(trotting, "Trotting");
	ANIM_BIT(sprint, "Sprint");
	ANIM_BIT(air, "Air");
	ANIM_BIT(jump, "Jump");
	ANIM_BIT(jumping, "Jumping");
	ANIM_BIT(falling, "Falling");
	ANIM_BIT(fallFast, "FallFast");
	ANIM_BIT(rising, "Rising");
	ANIM_BIT(jumpApex, "JumpApex");
	ANIM_BIT(landed, "Landed");
	ANIM_BIT(lunge, "Lunge");
	ANIM_BIT(lurch, "Lurch");
	ANIM_BIT(left, "Left");
	ANIM_BIT(right, "Right");
	ANIM_BIT(backward, "Backward");
	ANIM_BIT(lockon, "Lockon");
	ANIM_BIT(idle, "Idle");
	ANIM_BIT(knockback, "Knockback");
	ANIM_BIT(knockdown, "Knockdown");
	ANIM_BIT(pushback, "Pushback");
	ANIM_BIT(death_impact, "Death_Impact");
	ANIM_BIT(neardeath_impact, "NearDeath_Impact");
	ANIM_BIT(runtimeFreeze, "RuntimeFreeze");
	ANIM_BIT(moveLeft, "MoveLeft");
	ANIM_BIT(moveRight, "MoveRight");
	ANIM_BIT(repel, "Repel");
	ANIM_BIT(getUp, "Getup");
	ANIM_BIT(getUpBack, "Back");
	ANIM_BIT(prone, "Prone");
	ANIM_BIT(crouch, "Crouch");
	ANIM_BIT(aim, "AimMode");
	ANIM_BIT(death, "Death");
	ANIM_BIT(neardeath, "NearDeath");
	ANIM_BIT(dying, "Dying");
	ANIM_BIT(revive, "Revive");
	ANIM_BIT(exitDeath, "ExitDeath");
	ANIM_BIT(aiming, "Aiming");
	ANIM_BIT(dragonTurn, "DragonTurn");
	ANIM_BIT(turnLeft, "TurnLeft");
	ANIM_BIT(turnRight, "TurnRight");
	ANIM_BIT(impact, "Impact");
	ANIM_BIT(hitPause, "HitPause");
	ANIM_BIT(hitTop, "HitTop");
	ANIM_BIT(hitBottom, "HitBottom");
	ANIM_BIT(hitLeft, "HitLeft");
	ANIM_BIT(hitRight, "HitRight");
	ANIM_BIT(hitFront, "HitFront");
	ANIM_BIT(hitRear, "HitRear");
	ANIM_BIT(interrupt, "Interrupt");
	ANIM_BIT(flourish, "Flourish");
	ANIM_BIT(exit, "Exit");
	#undef ANIM_BIT

	#define ANIM_BIT(var, name) mmAnimBitHandles.flash.var = GET(name, 1)
	ANIM_BIT(dodgeRoll, "Dodge_Roll");
	#undef ANIM_BIT

	#undef GET
	
	{
		U32 count = 0;

		ARRAY_FOREACH_BEGIN(mmAnimBitHandles.all, i);
		{
			if(mmAnimBitHandles.all[i] == ~0){
				count++;
			}
		}
		ARRAY_FOREACH_END;
		
		assertmsgf(	!count,
					"mmAnimBitHandles has %u handles not set.",
					count);
	}
}

U32 mmTimerGroupSizes[] = {
	500,
	100,
	10,
};

S32 mmStartPerEntityTimer(MovementManager* mm){
	MovementPerEntityTimers*	perEntityTimers = mgState.debug.perEntityTimers;
	U32 						index = INDEX_FROM_REFERENCE(mm->entityRef);
	U32 						level = ARRAY_SIZE(mmTimerGroupSizes);
	U32 						size = 1;
	
	if(	!perEntityTimers ||
		index >= ARRAY_SIZE(mgState.bg.entIndexToManager))
	{
		return 0;
	}

	ARRAY_FOREACH_BEGIN(mmTimerGroupSizes, j);
	{
		U32 timerIndex = index / mmTimerGroupSizes[j];

		if(!perEntityTimers->isOpen[j][timerIndex]){
			level = j;
			index = timerIndex;
			size = mmTimerGroupSizes[j];
			break;
		}
	}
	ARRAY_FOREACH_END;

	if(!perEntityTimers->name[level][index]){
		char buffer[100];

		if(size == 1){
			sprintf(buffer, "ent[%d]", index);
		}else{
			sprintf(buffer, "ents[%d-%d]", index * size, (index + 1) * size - 1);
		}

		perEntityTimers->name[level][index] = strdup(buffer);
	}

	PERFINFO_AUTO_START_STATIC(	perEntityTimers->name[level][index],
								&perEntityTimers->perfInfo[level][index],
								1);
	
	return 1;
}

void mmGetInputValueIndexName(	MovementInputValueIndex mivi,
								const char** nameOut)
{
	const char* name;
	
	if(!nameOut){
		return;
	}
	
	switch(mivi){
		// Bits.
		
		xcase MIVI_BIT_FORWARD:			name = "FORWARD";
		xcase MIVI_BIT_BACKWARD:		name = "BACKWARD"; 
		xcase MIVI_BIT_LEFT:			name = "LEFT";
		xcase MIVI_BIT_RIGHT:			name = "RIGHT";
		xcase MIVI_BIT_UP:				name = "UP";
		xcase MIVI_BIT_DOWN:			name = "DOWN";
		xcase MIVI_BIT_SLOW:			name = "SLOW";
		xcase MIVI_BIT_TURN_LEFT:		name = "TURN_LEFT";
		xcase MIVI_BIT_TURN_RIGHT:		name = "TURN_RIGHT";
		xcase MIVI_BIT_RUN:				name = "RUN";
		xcase MIVI_BIT_ROLL:			name = "ROLL";
		xcase MIVI_BIT_AIM:				name = "AIM";
		xcase MIVI_BIT_CROUCH:			name = "CROUCH";

		// F32s.
		
		xcase MIVI_F32_DIRECTION_SCALE:	name = "DIRECTION_SCALE";
		xcase MIVI_F32_FACE_YAW:		name = "FACE_YAW";
		xcase MIVI_F32_ROLL_YAW:		name = "ROLL_YAW";
		xcase MIVI_F32_MOVE_YAW:		name = "MOVE_YAW";
		xcase MIVI_F32_PITCH:			name = "PITCH";
		xcase MIVI_F32_TILT:			name = "TILT";
		
		// Others.
		
		xcase MIVI_RESET_ALL_VALUES:	name = "RESET_ALL_VALUES";
		xcase MIVI_DEBUG_COMMAND:		name = "DEBUG_COMMAND";
		
		// Unknown.
		
		xdefault:						name = "UNKNOWN";
	}
	
	*nameOut = name;
}

U32 mmGetReleaseBit(void) {
	return mgState.animBitHandle.animOwnershipReleased;
}

S32 mmIsForegroundThread(void){
	if(mgState.fg.flags.notThreaded){
		return !mgState.bg.flags.threadIsBG;
	}else{
		return GetCurrentThreadId() == mgState.fg.threadID;
	}
}

S32 mmIsBackgroundThread(void){
	if(mgState.fg.flags.notThreaded){
		return mgState.bg.flags.threadIsBG;
	}else{
		return GetCurrentThreadId() != mgState.fg.threadID;
	}
}

U32 mmGetCurrentThreadSlot(void){
	return mmIsForegroundThread() ? MM_FG_SLOT : MM_BG_SLOT;
}

void mmLogAllSkeletons(void){
	if(!mgState.fg.flags.logSkeletons){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, size);
	{
		MovementManager*	mm = mgState.fg.managers[i];

		if(	mm->fg.flags.destroyed ||
			!MMLOG_IS_ENABLED(mm) ||
			!mm->msgHandler ||
			!mm->userPointer)
		{
			continue;
		}else{
			MovementManagerMsgPrivateData pd = {0};

			PERFINFO_AUTO_START("userLogSkeleton", 1);
			
			pd.mm = mm;
			pd.msg.msgType = MM_MSG_FG_LOG_SKELETON;
			pd.msg.userPointer = mm->userPointer;

			mm->msgHandler(&pd.msg);
			
			PERFINFO_AUTO_STOP();
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

void mmInputEventListReclaim(	MovementClient* mc,
								MovementInputEventList* mieList)
{
	if(!mieList->head){
		return;
	}

	assert(!mieList->head->prev);
	assert(mieList->tail);
	assert(!mieList->tail->next);

	if(mc->available.mieList.tail){
		assert(mc->available.mieList.head);
		assert(!mc->available.mieList.tail->next);
		mc->available.mieList.tail->next = mieList->head;
		mieList->head->prev = mc->available.mieList.tail;
	}else{
		assert(!mc->available.mieList.head);
		mc->available.mieListMutable.head = mieList->head;
	}
	
	mc->available.mieListMutable.tail = mieList->tail;
	
	mieList->head = NULL;
	mieList->tail = NULL;
}

static void mmHandleZeroClientsFG(void){
	PERFINFO_AUTO_START_FUNC();

	// Kick all the resources that were waiting to destroy after a net send.
	
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager* mm = mgState.fg.managers[i];
		
		if(mm->fg.flags.mmrHasUnsentStates){
			mmResourcesSetNeedsSetStateIfHasUnsentStatesFG(mm);
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

void mmClientCreate(MovementClient** mcOut,
					MovementClientMsgHandler msgHandler,
					void* userPointer)
{
	MovementClient* mc;

	if(!mcOut){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
		
	mc = callocStruct(MovementClient);
	
	mc->msgHandler = msgHandler;
	mc->userPointer = userPointer;
	
	eaPush(&mgState.fg.clients, mc);
			
	mc->netSend.frameLastSent = mgState.frameCount - 1;
	
	*mcOut = mc;
	
	PERFINFO_AUTO_STOP();
}

void mmClientDestroy(MovementClient** mcInOut){
	MovementClient* mc = SAFE_DEREF(mcInOut);
	S32				index = eaFind(&mgState.fg.clients, mc);
	
	PERFINFO_AUTO_START_FUNC();
	
	if(	mc &&
		index >= 0)
	{
		mmClientDetachManagers(mc);
		
		assert(!eaSize(&mc->mcmas));
		eaDestroy(&mc->mcmas);
		
		// Free the available mciSteps.
		{
			MovementClientInputStep* mciStep = mc->available.mciStepList.head;
			
			while(mciStep){
				MovementClientInputStep* next = mciStep->next;
				
				assert(!eaSize(&mciStep->miSteps));
				eaDestroy(&mciStep->miStepsMutable);
				SAFE_FREE(mciStep);
				mciStep = next;
			}
		}
		
		// Free the available miSteps.
		{
			EARRAY_CONST_FOREACH_BEGIN(mc->available.miSteps, i, isize);
			{
				MovementInputStep* miStep = mc->available.miSteps[i];
				
				mmInputStepDestroy(&miStep);
			}
			EARRAY_FOREACH_END;
			
			eaDestroy(&mc->available.miStepsMutable);
		}
		
		// Free the available mie-s.
		{
			MovementInputEvent* mie = mc->available.mieList.head;
			
			while(mie){
				MovementInputEvent* next = mie->next;
				
				mmInputEventDestroy(&mie);
				mie = next;
			}
			
			ZeroStruct(&mc->available.mieListMutable);
		}
		
		// Remove client from the list.

		eaRemove(	&mgState.fg.clients,
					index);
					
		if(!eaSize(&mgState.fg.clients)){
			eaDestroy(&mgState.fg.clients);
			mmHandleZeroClientsFG();
		}
		
		SAFE_FREE(mc);
		*mcInOut = NULL;
	}
	
	PERFINFO_AUTO_STOP();
}

void mmClientConnectedToServer(void){
	mgState.fg.netReceiveMutable.msTimeConnected = timeGetTime();

	ZeroStruct(&mgState.fg.netReceiveMutable.autoDebug);

	if(mgState.fg.mc.netSend.flags.sendLogListUpdates){
		mmClientSendFlagToServer("SendLogListUpdates", 1);
	}

	if(	mgState.fg.mc.netSend.flags.sendStatsFrames ||
		mgState.fg.mc.netSend.flags.autoSendStats)
	{
		mmClientSendFlagToServer("StatsSetFramesEnabled", 1);
	}

	if(	mgState.fg.mc.netSend.flags.sendStatsPacketTiming ||
		mgState.fg.mc.netSend.flags.autoSendStats)
	{
		mmClientSendFlagToServer("StatsSetPacketTimingEnabled", 1);
	}
}

void mmClientDisconnectedFromServer(void){
	mgState.fg.mc.netSend.flags.autoSendStats = 0;

	mmNetDestroyStatsFrames(NULL);
	mmNetDestroyStatsPackets(NULL);
}

void mmClientLogString(	MovementClient* mc,
						const char* text,
						const char* error)
{
	MovementClientMsg msg = {0};
	
	if(	!SAFE_MEMBER(mc, msgHandler) ||
		!SAFE_DEREF(text))
	{
		return;
	}
	
	msg.msgType = MC_MSG_LOG_STRING;
	msg.mc = mc;
	msg.userPointer = mc->userPointer;
	
	msg.logString.text = text;
	msg.logString.error = error;
	
	mc->msgHandler(&msg);
}

S32 mmClientPacketToClientCreate(	MovementClient* mc,
									Packet** pakOut,
									const char* pakTypeName)
{
	MovementClientMsg msg = {0};
	
	if(	!SAFE_MEMBER(mc, msgHandler) ||
		!pakOut)
	{
		return 0;
	}
	
	*pakOut = NULL;
	
	msg.msgType = MC_MSG_CREATE_PACKET_TO_CLIENT;
	msg.mc = mc;
	msg.userPointer = mc->userPointer;
	
	msg.createPacketToClient.pakOut = pakOut;
	
	mc->msgHandler(&msg);
	
	if(*pakOut){
		pktSendString(*pakOut, pakTypeName);
	}
	
	return !!*pakOut;
}

S32 mmClientPacketToClientSend(	MovementClient* mc,
								Packet** pakInOut)
{
	MovementClientMsg	msg = {0};
	Packet*				pak = SAFE_DEREF(pakInOut);
	
	if(	!SAFE_MEMBER(mc, msgHandler) ||
		!pak)
	{
		return 0;
	}
	
	*pakInOut = NULL;
	
	msg.msgType = MC_MSG_SEND_PACKET_TO_CLIENT;
	msg.mc = mc;
	msg.userPointer = mc->userPointer;
	
	msg.sendPacketToClient.pak = pak;
	
	mc->msgHandler(&msg);
	
	return 1;
}

S32 mmClientPacketToServerCreate(	Packet** pakOut,
									const char* pakTypeName)
{
	MovementGlobalMsg msg = {0};
	
	if(	!mgState.msgHandler ||
		!pakOut)
	{
		return 0;
	}
	
	*pakOut = NULL;
	
	msg.msgType = MG_MSG_CREATE_PACKET_TO_SERVER;
	msg.createPacketToServer.pakOut = pakOut;
	mgState.msgHandler(&msg);
	
	if(*pakOut){
		pktSendString(*pakOut, pakTypeName);
	}
	
	return !!*pakOut;
}

S32 mmClientPacketToServerSend(Packet** pakInOut){
	MovementGlobalMsg	msg = {0};
	Packet*				pak = SAFE_DEREF(pakInOut);
	
	if(	!mgState.msgHandler ||
		!pak)
	{
		return 0;
	}
	
	*pakInOut = NULL;
	
	msg.msgType = MG_MSG_SEND_PACKET_TO_SERVER;
	msg.sendPacketToServer.pak = pak;
	mgState.msgHandler(&msg);
	
	return 1;
}

S32 mmClientSendFlagToClient(	MovementClient* mc,
								const char* pakTypeName,
								S32 enabled)
{
	Packet* pak;
	
	if(!mmClientPacketToClientCreate(mc, &pak, pakTypeName)){
		return 0;
	}

	pktSendBits(pak, 1, !!enabled);
	mmClientPacketToClientSend(mc, &pak);
	return 1;
}

S32 mmClientSendFlagToServer(	const char* pakTypeName,
								S32 enabled)
{
	Packet* pak;
	
	if(!mmClientPacketToServerCreate(&pak, pakTypeName)){
		return 0;
	}

	pktSendBits(pak, 1, !!enabled);
	mmClientPacketToServerSend(&pak);
	return 1;
}

void mmClientSetSendLogListUpdates(S32 enabled){
	enabled = !!enabled;
	mgState.fg.mc.netSend.flags.sendLogListUpdates = enabled;
	mmClientSendFlagToServer("SendLogListUpdates", enabled);
}

void mmClientReceiveFromServer(Packet* pak){
	char pakTypeName[100];
	
	pktGetString(pak, SAFESTR(pakTypeName));
	
	if(!stricmp(pakTypeName, "Log")){
		mmLogReceive(pak);
	}
	else if(!stricmp(pakTypeName, "NetAutoDebug")){
		mmNetAutoDebugStartCollectingData();
	}
}

static void mmClientStatsReceiveAutoDebugResults(	MovementClient* mc,
													Packet* pak)
{
	MovementClientStatsStored*	stats;
	char*						statsString = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	
	stats = pktGetStruct(pak, parse_MovementClientStatsStored);
	
	estrReserveCapacity(&statsString, SQR(1024));
	ParserWriteText(&statsString, parse_MovementClientStatsStored, stats, 0, 0, 0);
	StructDestroySafe(parse_MovementClientStatsStored, &stats);
	
	mmClientLogString(mc, statsString, "AutoDebug received");
	
	estrDestroy(&statsString);
	
	PERFINFO_AUTO_STOP();
}

void mmClientReceiveFromClient(	MovementClient* mc,
								Packet* pak)
{
	char pakTypeName[100];

	PERFINFO_AUTO_START_FUNC();
	
	pktGetString(pak, SAFESTR(pakTypeName));
	
	if(!stricmp(pakTypeName, "SendLogListUpdates")){
		mc->netSend.flags.sendLogListUpdates = pktGetBits(pak, 1);
	}
	else if(!stricmp(pakTypeName, "StatsSetFramesEnabled")){
		mmClientStatsSetFramesEnabled(mc, pktGetBits(pak, 1));
	}
	else if(!stricmp(pakTypeName, "StatsSetPacketTimingEnabled")){
		mmClientStatsSetPacketTimingEnabled(mc, pktGetBits(pak, 1));
	}
	else if(!stricmp(pakTypeName, "AutoDebugResults")){
		mmClientStatsReceiveAutoDebugResults(mc, pak);
	}
	
	PERFINFO_AUTO_STOP();
}

void mmClientRequestNetAutoDebug(MovementClient* mc){
	PERFINFO_AUTO_START_FUNC();
	mmClientSendFlagToClient(mc, "NetAutoDebug", 1);
	PERFINFO_AUTO_STOP();
}

void mmClientStatsSetFramesEnabled(	MovementClient* mc,
									bool enabled)
{
	PERFINFO_AUTO_START_FUNC();

	enabled = !!enabled;

	if(!mc){
		mgState.fg.mc.netSend.flags.sendStatsFrames = enabled;
		mmClientSendFlagToServer(	"StatsSetFramesEnabled",
									enabled || mgState.fg.mc.netSend.flags.autoSendStats);
	}
	else if(enabled != (bool)mc->netSend.flags.sendStatsFrames){
		mc->netSend.flags.sendStatsFrames = enabled;
		
		mmNetDestroyStatsFrames(mc);

		if(enabled){
			mc->stats.frames = callocStruct(MovementClientStatsFrames);
			beaSetSize(&mc->stats.frames->frames, 200);
		}
	}

	PERFINFO_AUTO_STOP();
}

void mmClientStatsSetPacketTimingEnabled(	MovementClient* mc,
											bool enabled)
{
	PERFINFO_AUTO_START_FUNC();
	
	enabled = !!enabled;

	if(!mc){
		mgState.fg.mc.netSend.flags.sendStatsPacketTiming = enabled;
		mmClientSendFlagToServer(	"StatsSetPacketTimingEnabled",
									enabled || mgState.fg.mc.netSend.flags.autoSendStats);
	}
	else if(enabled != (bool)mc->netSend.flags.sendStatsPacketTiming){
		mc->netSend.flags.sendStatsPacketTiming = enabled;

		mmNetDestroyStatsPackets(mc);

		if(enabled){
			mc->stats.packets = callocStruct(MovementClientStatsPackets);
			beaSetSize(&mc->stats.packets->fromClient.packets, 200);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

static void mmSetUpdatedToBG(MovementManager* mm){
	MovementThreadData* td = MM_THREADDATA_FG(mm);
	
	if(FALSE_THEN_SET(td->toBG.flagsMutable.inUpdatedList)){
		MovementGlobalStateThreadData* mgtd = mgState.threadData + MM_FG_SLOT;
		
		eaPush(&mgtd->toBG.updatedManagers, mm);
		mgtd->toBG.flags.hasUpdatedManagers = 1;
	}
}

static void mmRemoveFromAfterSimWakesListFG(MovementManager* mm,
											S32 useClientList)
{
	MovementManager***	managers = useClientList ?
									&mgState.fg.managersAfterSimWakes.clientMutable :
									&mgState.fg.managersAfterSimWakes.nonClientMutable;
	const U32			i = mm->fg.afterSimWakes.index;

	assert(i < eaUSize(managers));
	assert((*managers)[i] == mm);

	mm->fg.afterSimWakes.index = 0;

	if(mgState.fg.flags.managersAfterSimWakesLocked){
		(*managers)[i] = NULL;

		if(useClientList){
			mgState.fg.flagsMutable.managersAfterSimWakesClientChanged = 1;
		}else{
			mgState.fg.flagsMutable.managersAfterSimWakesNonClientChanged = 1;
		}
	}else{
		U32 size;

		eaRemoveFast(managers, i);
		size = eaUSize(managers);

		if(i < size){
			MovementManager* mmOther = (*managers)[i];
			assert(mm != mmOther);
			assert(mmOther->fg.afterSimWakes.index == size);
			mmOther->fg.afterSimWakes.index = i;
		}
	}
}

S32 mmAttachToClient(	MovementManager* mm,
						MovementClient* mc)
{
	if(	!mm ||
		!mc ||
		mm->fg.flags.destroyed)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	if(	!mm->fg.mcma ||
		mc != mm->fg.mcma->mc)
	{
		MovementThreadData*					td = MM_THREADDATA_FG(mm);
		MovementClientManagerAssociation*	mcma;
				
		mmDetachFromClient(mm, NULL);
		
		// Double check that this mm isn't already attached.
		
		EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
		{
			assert(mc->mcmas[i]->mm != mm);
		}
		EARRAY_FOREACH_END;
		
		assert(!mm->fg.mcma);
		mcma = mm->fg.mcmaMutable = callocStruct(MovementClientManagerAssociation);
		ASSERT_FALSE_AND_SET(mm->fg.flagsMutable.isAttachedToClient);

		if(mm->fg.afterSimWakes.count){
			// Move from nonClient to client list.

			mmRemoveFromAfterSimWakesListFG(mm, 0);

			mm->fg.afterSimWakes.index = eaPush(&mgState.fg.managersAfterSimWakes.clientMutable, mm);

			if(mgState.fg.flags.managersAfterSimWakesLocked){
				mgState.fg.flagsMutable.managersAfterSimWakesClientChanged = 1;
			}
		}
		
		// Tell the BG thread that the client state changed.
		
		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.clientWasChanged = 1;
		td->toBG.flagsMutable.isAttachedToClient = 1;

		mmSetUpdatedToBG(mm);
		
		eaPush(	&mc->mcmas,
				mcma);
		
		mcma->mm = mm;
		mcma->mc = mc;
		
		// Update forcedSim.
		
		if(mm->fg.mrForcedSimCount){
			mc->mmForcedSimCount++;
		}
		
		// Tell client to send a manager update in the next packet.
		
		if(mgState.flags.isServer){
			mc->netSend.flags.updateManagers = 1;
		}else{
			if(!mcma->inputUnsplitQueue){
				mcma->inputUnsplitQueue = callocStruct(MovementInputUnsplitQueue);
			}

			mmInputEventResetAllValues(mm);
		}
		
		if(mgState.flags.logOnClientAttach){
			mmSetDebugging(mm, 1);
		}

		// Force requesters to do a full sync.

		EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, isize);
		{
			MovementRequester* mr = mm->fg.requesters[i];
		
			mr->fg.flagsMutable.sentCreateToOwner = 0;
		}
		EARRAY_FOREACH_END;
	}
	
	PERFINFO_AUTO_STOP();
		
	return 1;
}

S32 mmDetachFromClient(	MovementManager* mm,
						MovementClient* mc)
{
	MovementThreadData*					td;
	MovementClientManagerAssociation*	mcma = SAFE_MEMBER(mm, fg.mcma);
	
	if(!mcma){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	assert(mcma->mm == mm);
	
	mm->fg.mcmaMutable = NULL;
	ASSERT_TRUE_AND_RESET(mm->fg.flagsMutable.isAttachedToClient);

	if(mm->fg.afterSimWakes.count){
		mmRemoveFromAfterSimWakesListFG(mm, 1);

		mm->fg.afterSimWakes.index = eaPush(&mgState.fg.managersAfterSimWakes.nonClientMutable, mm);

		if(mgState.fg.flags.managersAfterSimWakesLocked){
			mgState.fg.flagsMutable.managersAfterSimWakesNonClientChanged = 1;
		}
	}

	if(mc){
		assert(mcma->mc == mc);
	}else{
		mc = mcma->mc;
	}
	
	if(mcma->inputUnsplitQueue){
		mmInputEventListReclaim(mc, &mcma->inputUnsplitQueue->mieListMutable);
		
		SAFE_FREE(mcma->inputUnsplitQueue);
	}
	
	assert(mc->mcmas);
	
	if(eaFindAndRemove(&mc->mcmas, mcma) < 0){
		// Should have been in the list.
		
		assert(0);
	}
	
	SAFE_FREE(mcma);
	
	// Queue an update on the next net send.
	
	if(mgState.flags.isServer){
		mc->netSend.flags.updateManagers = 1;
	}
	
	td = MM_THREADDATA_FG(mm);
	
	// Updated forcedSim.
	
	if(mm->fg.mrForcedSimCount){
		assert(mc->mmForcedSimCount);
		mc->mmForcedSimCount--;
	}
	
	// Tell BG thread that client changed.
	
	td->toBG.flagsMutable.hasToBG = 1;
	td->toBG.flagsMutable.clientWasChanged = 1;
	td->toBG.flagsMutable.isAttachedToClient = 0;

	mmSetUpdatedToBG(mm);
	
	// Cancel any queued reprediction.
	
	td->toBG.flagsMutable.doRepredict = 0;

	// Clear control steps owned by this mm.
	
	{
		MovementClientInputStep* mciStep;
		
		for(mciStep = mc->mciStepList.head;
			mciStep;
			mciStep = mciStep->next)
		{
			EARRAY_CONST_FOREACH_BEGIN(mciStep->miSteps, j, jsize);
			{
				MovementInputStep* miStep = mciStep->miSteps[j];
				
				if(miStep->mm == mm){
					miStep->mm = NULL;
					miStep->mciStep = NULL;

					eaRemove(&mciStep->miStepsMutable, j);

					if(!mciStep->flags.sentToBG){
						mmInputStepReclaim(mc, miStep);
					}
					
					break;
				}
			}
			EARRAY_FOREACH_END;
		}
	}
		
	// Clear the requester data that's specific to clients.
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->fg.requesters[i];
		MovementRequesterClass*			mrc = mr->mrc;
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_FG(mr);

		if(	!mgState.flags.isServer &&
			!mrc->pti.syncPublic)
		{
			mr->fg.flagsMutable.destroyedFromServer = 1;
			mrDestroy(&mr);
			continue;
		}
		
		// Destroy the previously sent net sync data.
		
		mmStructDestroy(mrc->pti.bg,
						mr->fg.net.prev.userStruct.bg,
						mm);
							
		mmStructDestroy(mrc->pti.sync,
						mr->fg.net.prev.userStruct.sync,
						mm);
							
		mr->fg.net.prev.ownedDataClassBits = 0;
		
		// Destroy the queued repredict states.
		
		if(!mgState.flags.isServer){
			if(mrtd->toBG.predict){
				mmStructDestroy(mrc->pti.bg,
								mrtd->toBG.predict->userStruct.serverBG,
								mm);
			}
								
			mmStructDestroy(mrc->pti.sync,
							mrtd->toBG.userStruct.sync,
							mm);
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
	
	return 1;
}

S32	mmIsAttachedToClient(MovementManager *mm)
{
	return (mm && mm->fg.mcma != NULL);
}

S32 mmClientDetachManagers(MovementClient* mc){
	if(!mc){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	while(eaSize(&mc->mcmas)){
		mmDetachFromClient(mc->mcmas[0]->mm, mc);
	}
	
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mmClientGetManagerCount(MovementClient* mc){
	if(!mc){
		return 0;
	}
	
	return eaSize(&mc->mcmas);
}

S32 mmClientGetManager(	MovementManager** mmOut,
						MovementClient* mc,
						U32 index)
{
	if(	!mc ||
		!mmOut ||
		index >= eaUSize(&mc->mcmas))
	{
		return 0;
	}
	
	*mmOut = mc->mcmas[index]->mm;
	
	return !!*mmOut;
}

S32 mmGetLocalManagerByIndex(	MovementManager** mmOut,
								U32 index)
{
	if(index >= eaUSize(&mgState.fg.mc.mcmas)){
		return 0;
	}
	
	*mmOut = mgState.fg.mc.mcmas[index]->mm;
	
	assert(*mmOut);
	
	return 1;
}

S32	mmClientSetSendFullRotations(	MovementClient* mc,
									S32 enabled)
{
	if(!mc){
		return 0;
	}
	
	mc->netSend.flags.sendFullRotations = !!enabled;
	
	return 1;
}

S32 mmClientResetSendState(MovementClient* mc){
	if(!mc){
		return 0;
	}
	
	mc->netSend.flags.updateWasSentPreviously = 0;
	ZeroStruct(&mc->netSend.sync);
	
	return 1;
}

S32 mmSetOwnershipConflictResolver(MovementConflictResolverCB cb){
	if(!verify(!mgState.cb.conflictResolver)){
		return 0;
	}

	mgState.cb.conflictResolver = cb;

	return 1;
}

void mmGlobalSetMsgHandler(MovementGlobalMsgHandler msgHandler){
	mgState.msgHandler = msgHandler;
}

void mmGetForcedSetCountFG(	MovementManager* mm,
							MovementThreadData* td,
							U32* countOut)
{
	if(FALSE_THEN_SET(td->toBG.flagsMutable.hasForcedSet)){
		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.forcedSetCount = ++mm->fg.forcedSetCount.shared;
	}
	
	*countOut = mm->fg.forcedSetCount.shared;
}

S32 mmSetPositionFG(MovementManager* mm,
					const Vec3 pos,
					const char* reason)
{
	MovementThreadData* td;

	if(	!mm ||
		!pos)
	{
		return 0;
	}
	
	mmLog(	mm,
			NULL,
			"[fg.pos] Setting position (%s)"
			" (%1.2f, %1.2f, %1.2f)"
			" [%8.8x, %8.8x, %8.8x]",
			reason,
			vecParamsXYZ(pos),
			vecParamsXYZ((S32*)pos));

	// Save some information about the last setpos.
	{
		char buffer[1000];
		
		sprintf(buffer,
				"(%1.2f, %1.2f, %1.2f) Reason: %s",
				vecParamsXYZ(pos),
				reason);
		
		mmRareLockEnter(mm);
		{
			SAFE_FREE(mm->lastSetPosInfoString);
			mm->lastSetPosInfoString = strdup(buffer);
		}
		mmRareLockLeave(mm);
	}
	
	MM_CHECK_DYNPOS_DEVONLY(pos);

	td = MM_THREADDATA_FG(mm);

	copyVec3(pos, mm->fg.posMutable);
	
	if(	!mgState.flags.noLocalProcessing &&
		FALSE_THEN_SET(mm->fg.flagsMutable.posNeedsForcedSetAck))
	{
		mmGetForcedSetCountFG(mm, td, &mm->fg.forcedSetCount.pos);
	}
	
	if(	mgState.flags.isServer &&
		mm->fg.mcma &&
		FALSE_THEN_SET(mm->fg.mcma->flags.forcedSetPos))
	{
		mm->fg.mcma->setPos.versionToSend++;
		
		td->toBG.setPosVersionMutable = mm->fg.mcma->setPos.versionToSend;
		td->toBG.flagsMutable.hasSetPosVersion = 1;
		td->toBG.flagsMutable.hasToBG = 1;
	}

	// Send new position to BG.
	
	copyVec3(	mm->fg.pos,
				td->toBG.newPos);
	td->toBG.flagsMutable.useNewPos = 1;
	td->toBG.flagsMutable.hasToBG = 1;

	if(!mm->fg.net.outputList.head){
		// No net outputs have been sent yet, so initialize to the current position.
		mmConvertVec3ToIVec3(	pos,
								mm->fg.net.cur.encoded.pos);
	}

	return 1;
}

S32 mmSetRotationFG(MovementManager* mm,
					const Quat rot,
					const char* reason)
{
	MovementThreadData* td;
	Vec3				zVec;
	Vec2				pyFace;
	
	if(	!mm ||
		!rot)
	{
		return 0;
	}
	
	mmLog(	mm,
			NULL,
			"[fg.pos] Setting rotation (%s)"
			" (%1.2f, %1.2f, %1.2f, %1.2f)"
			" [%8.8x, %8.8x, %8.8x, %8.8x]",
			reason,
			quatParamsXYZW(rot),
			quatParamsXYZW((S32*)rot));

	assert(FINITEQUAT(rot));

	devassertmsgf(	quatIsNormalized(rot),
					"Invalid rot length %1.3f"
					" (%1.3f, %1.3f, %1.3f, %1.3f)"
					" [%8.8x, %8.8x, %8.8x, %8.8x]"
					" passed to %s (reason: %s).",
					lengthVec4(rot),
					quatParamsXYZW(rot),
					quatParamsXYZW((S32*)rot),
					__FUNCTION__,
					reason);

	td = MM_THREADDATA_FG(mm);

	copyQuat(	rot,
				mm->fg.rotMutable);
	
	quatToMat3_2(rot, zVec);
	getVec3YP(zVec, &pyFace[1], &pyFace[0]);
	
	copyVec2(	pyFace,
				mm->fg.pyFaceMutable);

	if(	!mgState.flags.noLocalProcessing &&
		FALSE_THEN_SET(mm->fg.flagsMutable.rotNeedsForcedSetAck))
	{
		mmGetForcedSetCountFG(mm, td, &mm->fg.forcedSetCount.rot);
	}

	if(	mgState.flags.isServer &&
		mm->fg.mcma)
	{
		mm->fg.mcma->flags.forcedSetRot = 1;
	}

	// Send update to BG.

	copyQuat(	mm->fg.rot,
				td->toBG.newRot);
	td->toBG.flagsMutable.useNewRot = 1;
	td->toBG.flagsMutable.hasToBG = 1;
	
	if(!mm->fg.net.outputList.head){
		// No net outputs have been sent yet, so initialize to the current rotation
		mmEncodeQuatToPyr(rot, mm->fg.net.cur.encoded.pyr);
		copyVec2(	mm->fg.net.cur.encoded.pyr,
					mm->fg.net.cur.encoded.pyFace);
	}
	
	return 1;
}

static void mmSetCurrentViewFG(	MovementManager* mm,
								MovementThreadData* td);

#define mmDataViewIsCurrentFG(mm, atRest)										\
		(	!mm->fg.flags.atRest &&												\
			mm->fg.frameWhenViewSet == mgState.frameCount						\
			||																	\
			mm->fg.flags.atRest &&												\
			subS32(mm->fg.frameWhenViewSet, mm->fg.frameWhenViewChanged) >= 0)

#define mmSetCurrentViewIfNotCurrentFG(mm, atRest)								\
		(	!mmDataViewIsCurrentFG(mm, atRest) &&								\
			(mmSetCurrentViewFG(mm, MM_THREADDATA_FG(mm)),0))

S32 mmGetPositionFG(MovementManager* mm,
					Vec3 posOut)
{
	if(	!mm ||
		!posOut)
	{
		return 0;
	}
	
	mmSetCurrentViewIfNotCurrentFG(mm, posViewIsAtRest);
	copyVec3(mm->fg.pos, posOut);
	
	return 1;
}

S32 mmGetDebugLatestServerPositionFG(	MovementManager* mm,
										Vec3 posOut)
{
	if(	!mm ||
		!posOut ||
		!mm->fg.net.outputList.tail)
	{
		return 0;
	}

	copyVec3(	mm->fg.net.outputList.tail->data.pos,
				posOut);
	
	return 1;
}

S32 mmGetLatestServerPosFaceFG(	MovementManager* mm,
								Vec3 posOut,
								Vec2 pyOut)
{
	if(	!mm ||
		!posOut ||
		!mm->fg.net.outputList.tail)
	{
		return 0;
	}

	copyVec3(	mm->fg.net.outputList.tail->data.pos,
				posOut);
	copyVec2(	mm->fg.net.outputList.tail->data.pyFace,
				pyOut);

	return 1;
}

S32 mmGetPositionRotationAtTimeFG(	MovementManager* mm,
									U32 timeStamp,
									Vec3 posOut,
									Quat qRotOut)
{
	MovementNetOutput *pNetOutput, *pPrevOutput = NULL;

	if (!mm ||
		!posOut ||
		!timeStamp)
	{
		return 0;
	}

	// todo: sanity check the timestamp

	pNetOutput = mm->fg.net.outputList.tail;
	while (pNetOutput)
	{
		if (timeStamp >= pNetOutput->pc.server)
		{	
			// should we need to interp the positions?
			if (!pPrevOutput || timeStamp == pNetOutput->pc.server)
			{
				copyVec3(pNetOutput->data.pos, posOut);
			}
			else
			{
				interpVec3(0.5f, pNetOutput->data.pos, pPrevOutput->data.pos, posOut);
			}
			if (qRotOut)
				copyQuat(pNetOutput->data.rot, qRotOut);
			return 1;
		}

		pPrevOutput = pNetOutput;
		pNetOutput = pNetOutput->prev;

	}

	return 0;
}

S32 mmGetRotationFG(MovementManager* mm,
					Quat rotOut)
{
	if(	!mm ||
		!rotOut)
	{
		return 0;
	}

	mmSetCurrentViewIfNotCurrentFG(mm, rotViewIsAtRest);
	copyQuat(mm->fg.rot, rotOut);

	return 1;
}

S32 mmGetFacePitchYawFG(MovementManager* mm,
						Vec2 pyFaceOut)
{
	if(	!mm ||
		!pyFaceOut)
	{
		return 0;
	}

	mmSetCurrentViewIfNotCurrentFG(mm, pyFaceViewIsAtRest);
	copyVec2(mm->fg.pyFace, pyFaceOut);

	return 1;
}

S32 mmGetUserPointer(	const MovementManager* mm,
						void** userPointerOut)
{
	if(	!mm ||
		!mm->userPointer ||
		!userPointerOut)
	{
		return 0;
	}
	
	*userPointerOut = mm->userPointer;
	
	return 1;
}

S32 mmGetUserThreadData(const MovementManager* mm,
						void** userThreadDataOut)
{
	if(	!mm ||
		!mm->userThreadData[MM_FG_SLOT] ||
		!userThreadDataOut)
	{
		return 0;
	}
	
	*userThreadDataOut = mm->userThreadData[MM_FG_SLOT];
	
	return 1;
}

void mmSendMsgUserThreadDataUpdatedToBG(MovementManager* mm){
	if (mm)
	{
		//begin added error checking code
		if (mm->fg.flags.sentUserThreadDataUpdateToBG != mm->fg.flags.sentUserThreadDataUpdateToBGbit)
		{
			Errorf(	"MM Flag Bug : before setting, sentUserThreadDataUpdateToBG = %u when sentUserThreadDataUpdateToBGbit = %u",
					mm->fg.flags.sentUserThreadDataUpdateToBG,
					mm->fg.flags.sentUserThreadDataUpdateToBGbit);
		}
		//end added error checking code

		if (FALSE_THEN_SET(mm->fg.flagsMutable.sentUserThreadDataUpdateToBG))
		{
			MovementThreadData* td = MM_THREADDATA_FG(mm);
	
			//begin added error checking code
			mm->fg.flagsMutable.sentUserThreadDataUpdateToBGbit = 1;
			if (!mmIsForegroundThread()) {
				Errorf("MM Flag Bug : Caught sentUserThreadDataUpdateToBG being set on a different thread than the FG!");
			}
			//end added error checking code

			td->toBG.flagsMutable.hasToBG = 1;
			td->toBG.flagsMutable.userThreadDataHasUpdate = 1;

			mmHandleAfterSimWakesIncFG(mm, "sentUserThreadDataUpdateToBG", __FUNCTION__);
		}
	}
}

S32	mrGetManagerUserPointer(const MovementRequester* mr,
							void** userPointerOut)
{
	return mmGetUserPointer(SAFE_MEMBER(mr, mm), userPointerOut);
}

S32	mrmGetManagerUserPointerFG(	const MovementRequesterMsg* msg,
								void** userPointerOut)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		!MR_MSG_TYPE_IS_FG(pd->msgType))
	{
		return 0;
	}
	
	return mmGetUserPointer(pd->mm, userPointerOut);
}

S32 mmHasProcessed(MovementManager* mm){
	return SAFE_MEMBER(mm, fg.flags.didProcessInBG);
}

S32 mmSetIgnoreActorCreateFG(	MovementManager *mm,
								S32 ignore)
{
	if(	!mm ||
		!mmIsForegroundThread())
	{
		return 0;
	}

	mm->fg.flagsMutable.ignoreActorCreate = !!ignore;

	return 1;
}

U32 mmCollisionSetGetNextID(void){
	static U32 collId;
	while(!++collId){}
	return collId;
}

S32 mmCollisionSetHandleCreateFG(	MovementManager* mm,
									MovementCollSetHandle** mcshOut,
									const char* fileName, 
									U32 fileLine,
									S32 setID)
{
	MovementCollSetHandle* mcsh;

	if(	!mm ||
		mm->fg.flags.destroyed ||
		!mcshOut ||
		*mcshOut ||
		eaSize(&mm->fg.collisionSetHandles) ||
		!mmIsForegroundThread())
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	*mcshOut = mcsh = callocStruct(MovementCollSetHandle);
	
	mcsh->mm = mm;
	mcsh->owner.fileLine = fileLine;
	mcsh->owner.fileName = fileName;
	mcsh->setID = setID;

	eaPush(&mm->fg.collisionSetHandlesMutable, mcsh);

	mm->fg.collisionSetMutable = mcsh->setID;

	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mmCollisionSetHandleDestroyFG(MovementCollSetHandle** mcshInOut){
	MovementCollSetHandle*	mcsh = SAFE_DEREF(mcshInOut);
	MovementManager*		mm;

	if(	!mcsh ||
		!mmIsForegroundThread())
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	mm = mcsh->mm;

	if(eaFindAndRemove(&mm->fg.collisionSetHandlesMutable, mcsh) < 0){
		devassertmsgf(0, "Failed to remove CollSetHandle.  Owner was: %s:%d", mcsh->owner.fileName, mcsh->owner.fileLine);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(!eaSize(&mm->fg.collisionSetHandles)){
		eaDestroy(&mm->fg.collisionSetHandlesMutable);
		mm->fg.collisionSetMutable = 0;
	}else{
		mm->fg.collisionSetMutable = eaTail(&mm->fg.collisionSetHandles)->setID;
	}

	SAFE_FREE(*mcshInOut);
	mcsh = NULL;

	PERFINFO_AUTO_STOP();

	return 1;
}

void mmSetNetReceiveCollisionSetFG(	MovementManager* mm,
									S32 set)
{
	if(	!mm ||
		!mmIsForegroundThread())
	{
		return;
	}

	if(set != mm->fg.net.collisionSet){
		mm->fg.net.collisionSetMutable = set;
		mm->fg.collisionSetMutable = set;
	}
}


S32 mmCollisionBitsHandleCreateFG(	MovementManager* mm,
									MovementCollBitsHandle** mcbhOut,
									const char* fileName,
									U32 fileLine,
									U32 groupBits)
{
	MovementCollBitsHandle *mcbh;

	if(	!mm ||
		mm->fg.flags.destroyed ||
		!mcbhOut ||
		*mcbhOut ||
		!mmIsForegroundThread())
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	*mcbhOut = mcbh = callocStruct(MovementCollBitsHandle);

	mcbh->mm = mm;
	mcbh->owner.fileName = fileName;
	mcbh->owner.fileLine = fileLine;
	mcbh->groupBits = groupBits;

	eaPush(&mm->fg.collisionGroupBitsHandlesMutable, mcbh);

	mm->fg.collisionGroupBitsMutable = 0;
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.collisionGroupBitsHandles, i, isize);
	{
		const MovementCollBitsHandle* mcbhCur = mm->fg.collisionGroupBitsHandles[i];

		mm->fg.collisionGroupBitsMutable |= mcbhCur->groupBits;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mmCollisionBitsHandleDestroyFG(MovementCollBitsHandle** mcbhInOut){
	MovementCollBitsHandle*	mcbh = SAFE_DEREF(mcbhInOut);
	MovementManager*		mm;

	if(	!mcbh ||
		!mmIsForegroundThread())
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	mm = mcbh->mm;

	if(eaFindAndRemoveFast(&mm->fg.collisionGroupBitsHandlesMutable, mcbh) < 0){
		devassertmsgf(0, "Failed to remove CollGroupHandle.  Owner was: %s:%d", mcbh->owner.fileName, mcbh->owner.fileLine);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	SAFE_FREE(*mcbhInOut);
	mcbh = NULL;

	mm->fg.collisionGroupBitsMutable = 0;
	EARRAY_FOREACH_REVERSE_BEGIN(mm->fg.collisionGroupBitsHandles, i);
	{
		MovementCollBitsHandle*const mcbhConst = mm->fg.collisionGroupBitsHandles[i];

		mm->fg.collisionGroupBitsMutable |= mcbhConst->groupBits;
	}
	EARRAY_FOREACH_END;

	if(!eaSize(&mm->fg.collisionGroupBitsHandles)){
		eaDestroy(&mm->fg.collisionGroupBitsHandlesMutable);
	}

	PERFINFO_AUTO_STOP();

	return 1;
}

void mmSetNetReceiveCollisionBitsFG(MovementManager* mm,
									U32 bits)
{
	if(	!mm ||
		!mmIsForegroundThread())
	{
		return;
	}

	if(bits != mm->fg.net.collisionGroupBits){
		mm->fg.net.collisionGroupBitsMutable = bits;

		mm->fg.collisionGroupBitsMutable = bits;

		EARRAY_FOREACH_REVERSE_BEGIN(mm->fg.collisionGroupBitsHandles, i);
		{
			const MovementCollBitsHandle *mcbh = mm->fg.collisionGroupBitsHandles[i];

			mm->fg.collisionGroupBitsMutable |= mcbh->groupBits;
		}
		EARRAY_FOREACH_END;
	}
}

S32 mmCollisionGroupHandleCreateFG(	MovementManager* mm,
									MovementCollGroupHandle** mcghOut,
									const char* fileName,
									U32 fileLine,
									U32 groupBit)
{
	MovementCollGroupHandle* mcgh;

	if(	!mm ||
		mm->fg.flags.destroyed ||
		!mcghOut ||
		*mcghOut ||
		!mmIsForegroundThread())
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	*mcghOut = mcgh = callocStruct(MovementCollGroupHandle);

	mcgh->mm = mm;
	mcgh->owner.fileName = fileName;
	mcgh->owner.fileLine = fileLine;
	mcgh->groupBit = groupBit;

	eaPush(&mm->fg.collisionGroupHandlesMutable, mcgh);

	mm->fg.collisionGroupMutable = 0;
	EARRAY_FOREACH_REVERSE_BEGIN(mm->fg.collisionGroupHandles, i);
	{
		const MovementCollGroupHandle *mcghConst = mm->fg.collisionGroupHandles[i];

		mm->fg.collisionGroupMutable |= mcghConst->groupBit;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mmCollisionGroupHandleDestroyFG(MovementCollGroupHandle** mcghInOut){
	MovementCollGroupHandle*	mcgh = SAFE_DEREF(mcghInOut);
	MovementManager*			mm;

	if(	!mcgh ||
		!mmIsForegroundThread())
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	mm = mcgh->mm;

	if(eaFindAndRemoveFast(&mm->fg.collisionGroupHandlesMutable, mcgh) < 0){
		devassertmsgf(0, "Failed to remove CollGroupHandle.  Owner was: %s:%d", mcgh->owner.fileName, mcgh->owner.fileLine);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(!eaSize(&mm->fg.collisionGroupHandles)){
		eaDestroy(&mm->fg.collisionGroupHandlesMutable);
	}

	SAFE_FREE(*mcghInOut);
	mcgh = NULL;

	mm->fg.collisionGroupMutable = 0;
	EARRAY_FOREACH_REVERSE_BEGIN(mm->fg.collisionGroupHandles, i);
	{
		const MovementCollGroupHandle* mcghConst = mm->fg.collisionGroupHandles[i];

		mm->fg.collisionGroupMutable |= mcghConst->groupBit;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return 1;
}

void mmSetNetReceiveCollisionGroupFG(	MovementManager* mm,
										U32 bits)
{
	if(	!mm ||
		!mmIsForegroundThread())
	{
		return;
	}

	if(bits != mm->fg.net.collisionGroup){
		mm->fg.net.collisionGroupMutable = bits;

		mm->fg.collisionGroupMutable = bits;

		EARRAY_CONST_FOREACH_BEGIN(mm->fg.collisionGroupHandles, i, isize);
		{
			const MovementCollGroupHandle* mcgh = mm->fg.collisionGroupHandles[i];

			mm->fg.collisionGroupMutable |= mcgh->groupBit;
		}
		EARRAY_FOREACH_END;
	}
}

static void mmNoCollEnableFG(	MovementManager* mm,
								bool enabled)
{
	if((bool)mm->fg.flags.noCollision != enabled){
		MovementThreadData* td = MM_THREADDATA_FG(mm);
		
		mm->fg.flagsMutable.noCollision = enabled;
		
		td->toBG.flagsMutable.noCollisionChanged = 1;
		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.noCollision = enabled;
	}
}

S32	mmNoCollHandleCreateFG(	MovementManager* mm,
							MovementNoCollHandle** nchOut,
							const char* fileName,
							U32 fileLine)
{
	MovementNoCollHandle* nch;
	
	if(	!mm ||
		!nchOut ||
		*nchOut ||
		!mmIsForegroundThread())
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	*nchOut = nch = callocStruct(MovementNoCollHandle);

	nch->mm = mm;
	nch->owner.fileName = fileName;
	nch->owner.fileLine = fileLine;
	
	eaPush(&mm->fg.noCollHandlesMutable, nch);

	mmNoCollEnableFG(mm, 1);
	
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mmNoCollHandleDestroyFG(MovementNoCollHandle** nchInOut)
{
	MovementNoCollHandle*	nch = SAFE_DEREF(nchInOut);
	MovementManager*		mm;

	if(	!nch ||
		!mmIsForegroundThread())
	{
		return 0;
	}
	
	mm = nch->mm;
	
	if(eaFindAndRemove(&mm->fg.noCollHandlesMutable, nch) < 0){
		assert(0);
	}

	SAFE_FREE(*nchInOut);
	nch = NULL;
	
	if(!eaSize(&mm->fg.noCollHandles)){
		eaDestroy(&mm->fg.noCollHandlesMutable);
		
		mmNoCollEnableFG(mm, mm->fg.net.flags.noCollision);
	}

	return 1;
}

void mmSetNetReceiveNoCollFG(	MovementManager* mm,
								bool enabled)
{
	if(	!mm ||
		!mmIsForegroundThread())
	{
		return;
	}

	enabled = !!enabled;
	
	if(enabled != (bool)mm->fg.net.flags.noCollision){
		mm->fg.net.flags.noCollision = enabled;
		
		if(enabled){
			mmNoCollEnableFG(mm, 1);
		}else{
			mmNoCollEnableFG(mm, !!eaSize(&mm->fg.noCollHandles));
		}
	}
}

void mmSetInDeathPredictionFG(MovementManager* mm, bool enabled)
{
	if ( !mm || 
		 !mmIsForegroundThread())
	{
		return;
	}
	else
	{
		MovementThreadData* td = MM_THREADDATA_FG(mm);
		enabled = !!enabled;
		if ((bool)mm->fg.flags.isInDeathPrediction != enabled)
		{
			td->toBG.flagsMutable.hasToBG = true;
			td->toBG.flagsMutable.hasUpdatedDeathPrediction = true;
			td->toBG.flagsMutable.isInDeathPrediction = enabled;
			mm->fg.flagsMutable.isInDeathPrediction = enabled;
		}
	}
}

// Sets the use movement orientation for capsule collisions flag in the FG
void mmSetUseRotationForCapsuleOrientationFG(MovementManager* mm, bool enabled)
{
	enabled = !!enabled;
	if((bool)mm->fg.flags.capsuleOrientationUseRotation != enabled)
	{
		MovementThreadData* td = MM_THREADDATA_FG(mm);

		mm->fg.flagsMutable.capsuleOrientationUseRotation = enabled;

		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.capsuleOrientationMethodChanged = 1;
		td->toBG.flagsMutable.capsuleOrientationUseRotation = enabled;
	}
}

S32 mmCreateOffsetInstanceFG(	MovementManager* mm,
								F32 rotationOffset,
								MovementOffsetInstance** instanceOut)
{
	MovementOffsetInstance* instance;
	
	if(!instanceOut){
		return 0;
	}
	
	instance = callocStruct(MovementOffsetInstance);
	
	instance->rotationOffset = rotationOffset;
	
	eaPush(	&mm->fg.offsetInstancesMutable,
			instance);
	
	mm->fg.flagsMutable.hasOffsetInstances = 1;
			
	*instanceOut = instance;
	
	return 1;
}

S32 mmDestroyOffsetInstanceFG(	MovementManager* mm,
								MovementOffsetInstance** instanceInOut)
{
	MovementOffsetInstance* instance = SAFE_DEREF(instanceInOut);
	
	if(!instance){
		return 0;
	}
	
	if(eaFindAndRemove(&mm->fg.offsetInstancesMutable, instance) < 0){
		return 0;
	}
	
	if(!eaSize(&mm->fg.offsetInstances)){
		eaDestroy(&mm->fg.offsetInstancesMutable);
		
		mm->fg.flagsMutable.hasOffsetInstances = 0;
	}
	
	SAFE_FREE(instance);
	*instanceInOut = NULL;

	return 1;
}

S32 mmGetVelocityFG(MovementManager* mm,
					Vec3 velOut)
{
	if(!velOut)
		return 0;

	if(!mm)
	{
		zeroVec3(velOut);
		return 0;
	}
	
	if(	mgState.flags.isServer
		||
		mm->fg.flags.isAttachedToClient &&
		!mgState.fg.flags.predictDisabled)
	{
		MovementThreadData*			td;
		const MovementOutputList*	ol;
		
		td = MM_THREADDATA_FG(mm);
		ol = &td->toFG.outputList;

		if(ol->tail){
			if(ol->head != ol->tail){
				subVec3(ol->tail->data.pos,
						ol->tail->prev->data.pos,
						velOut);
				
				scaleVec3(	velOut,
							MM_STEPS_PER_SECOND,
							velOut);
			}else{
				zeroVec3(velOut);
			}

			return 1;
		}
	}

	// Fallback is to use net positions.
	{
		const MovementNetOutputList* nol = &mm->fg.net.outputList;
		
		if(nol->tail){
			if(nol->tail->prev){
				subVec3(nol->tail->data.pos,
						nol->tail->prev->data.pos,
						velOut);
				
				scaleVec3(	velOut,
							MM_STEPS_PER_SECOND,
							velOut);
			}else{
				zeroVec3(velOut);
			}
			
			return 1;
		}
	}
	
	zeroVec3(velOut);
	
	return 0;
}

static void mmFindNetOutputsFG(	const MovementNetOutputList* nol,
								const MovementNetOutput** olderOut,
								const MovementNetOutput** newerOut,
								F32* outputInterpInverseOut)
{
	const MovementNetOutput*	older = NULL;
	const MovementNetOutput*	newer = NULL;
	F32							outputInterpInverse = 0.f;
	const MovementNetOutput*	no;
	
	*olderOut = NULL;
	*newerOut = NULL;

	// Find the newest net output that is approaching the spc view ceiling.

	for(no = nol->tail;
		no;
		no = (no == nol->head ? NULL : no->prev))
	{
		S32 diff =	subS32(	mgState.fg.netView.spcCeiling,
							no->pc.server);

		if(	diff > 0 ||
			no == nol->head)
		{
			older = no;

			if(no != nol->tail){
				newer = no->next;

				if(newer->flags.notInterped){
					older = newer;
					outputInterpInverse = 0.f;
				}else{
					// Clip the spc between newer and latest from the view offset,
					// and then interp the remaining spc between older and newer.

					const S32	diffToLatest =	mgState.fg.netReceive.cur.pc.server -
												newer->pc.server;
					F32			interp =	mgState.fg.netView.spcOffsetFromEnd.total -
											diffToLatest;
					const S32	spcDiff = subS32(	newer->pc.server,
													older->pc.server);

					if(spcDiff > 0){
						interp /= (F32)spcDiff;
						MINMAX1(interp, 0.f, 1.f);
					}else{
						interp = 1.f;
					}

					outputInterpInverse = interp;
				}
			}else{
				newer = older;
				outputInterpInverse = 0.f;
			}

			break;
		}
	}

	*olderOut = older;
	*newerOut = newer;
	*outputInterpInverseOut = outputInterpInverse;
}

void mmSetSyncWithServer(S32 set){
	mgState.fg.flagsMutable.noSyncWithServer = !set;
}

void mmSetNoDisable(S32 set){
	mgState.fg.flagsMutable.noDisable = !!set;
}

S32 mmIsSyncWithServerEnabled(void){
	return !mgState.fg.flags.noSyncWithServer;
}

void mmGetNetOffsetsFromEnd(F32* normalOut,
							F32* fastOut,
							F32* lagOut)
{
	if(normalOut){
		*normalOut = mgState.fg.netView.spcOffsetFromEnd.normal;
	}
	
	if(fastOut){
		*fastOut = mgState.fg.netView.spcOffsetFromEnd.fast;
	}
	
	if(lagOut){
		*lagOut = mgState.fg.netView.spcOffsetFromEnd.lag;
	}
}

MP_DEFINE(MovementInputEvent);

void mmInputEventCreateNonZeroed(	MovementInputEvent** mieOut,
									MovementClient* mc,
									MovementInputUnsplitQueue* q,
									MovementInputValueIndex mivi,
									U32 msTime)
{
	MovementInputEvent* mie;

	if(mc->available.mieList.head){
		mie = mc->available.mieList.head;
		mc->available.mieListMutable.head = mie->next;
		if(!mie->next){
			mc->available.mieListMutable.tail = NULL;
		}
		if(mie->value.mivi == MIVI_DEBUG_COMMAND){
			SAFE_FREE(mie->commandMutable);
		}
	}else{
		MP_CREATE_COMPACT(MovementInputEvent, 100, 256, 0.80);
		mie = MP_ALLOC(MovementInputEvent);
	}
	
	mie->value.mivi = mivi;

	// Adjust the msTime.
	
	if(q){
		if(	q->msTime.lastControlUpdate
			&&
			(	!msTime ||
				subS32(msTime, q->msTime.lastControlUpdate) < 0))
		{
			msTime = q->msTime.lastControlUpdate;
		}

		q->msTime.lastControlUpdate = msTime;

		// Add to list.
		
		if(!q->mieList.head){
			assert(!q->mieList.tail);
			q->mieListMutable.head = mie;
		}else{
			assert(q->mieList.tail);
			assert(!q->mieList.tail->next);
			q->mieList.tail->next = mie;
		}
		
		mie->prev = q->mieList.tail;
		mie->next = NULL;
		
		q->mieListMutable.tail = mie;
	}else{
		msTime = 0;
	}
	
	mie->msTime = msTime;

	*mieOut = mie;
}

S32 mmInputEventDestroy(MovementInputEvent** mieInOut){
	MovementInputEvent* mie = SAFE_DEREF(mieInOut);
	
	if(!mie){
		return 0;
	}
	
	if(mie->value.mivi == MIVI_DEBUG_COMMAND){
		SAFE_FREE(mie->commandMutable);
	}

	MP_FREE(MovementInputEvent, *mieInOut);

	return 1;
}

S32 mmInputEventSetValueBitTracked(	MovementManager* mm,
									MovementInputValueIndex mivi,
									S32	value,
									U32 msTime,
									S32 canDoubleTap,
									S32 *pIsDoubleTap,
									const char* fileName,
									U32 fileLine)
{
	MovementInputUnsplitQueue*	q = SAFE_MEMBER2(mm, fg.mcma, inputUnsplitQueue);
	const U32					arrayIndex = mivi - MIVI_BIT_LOW;
	MovementInputEvent*			mie;
	S32							isDoubleTap = 0;
	
	if(	!q ||
		arrayIndex >= MIVI_BIT_COUNT)
	{
		return 0;
	}

	value = !!value;

	// Check if this is a double tap.
	
	if(value){
		if(!canDoubleTap){
			if(!q->values.lastQueued.bit[arrayIndex]){
				q->lastBit.flags.isSet = 0;
			}
		}
		else if(q->lastBit.flags.isSet &&
				mivi == q->lastBit.mivi &&
				subS32(msTime, q->lastBit.msTime) <= g_MovementManagerConfig.iDoubleTapInterval)
		{
			isDoubleTap = 1;
			q->lastBit.flags.isSet = 0;
		}else{
			q->lastBit.msTime = msTime;
			q->lastBit.mivi = mivi;
			q->lastBit.flags.isSet = 1;
		}
	}
	
	if (pIsDoubleTap) 
		(*pIsDoubleTap) = isDoubleTap;

	if(	!isDoubleTap &&
		q->values.lastQueued.bit[arrayIndex] == value)
	{
		return 0;
	}

	mmInputEventCreateNonZeroed(&mie,
								mm->fg.mcma->mc,
								q,
								mivi,
								msTime);
	
	msTime = mie->msTime;

	mie->value.bit = value;
	mie->value.flags.isDoubleTap = isDoubleTap;
	
	// Change the current value.

	q->values.lastQueued.bit[arrayIndex] = value;
	
	if(MMLOG_IS_ENABLED(mm)){
		const char* inputName;
		
		mmGetInputValueIndexName(mivi, &inputName);
		
		mmLog(	mm,
				NULL,
				"[fg.input] Queueing input bit[%s] = %d, %ums%s (%s:%u)",
				inputName,
				value,
				msTime,
				mie->value.flags.isDoubleTap ?
					", DOUBLE TAP" :
					canDoubleTap ?
						"" :
						", NO DOUBLE TAP ALLOWED",
				getFileNameConst(fileName),
				fileLine);
	}
	
	return 1;
}

void mmSanitizeInputValueF32(	MovementInputValueIndex mivi,
								F32* valueInOut)
{
	if(!FINITE(*valueInOut)){
		*valueInOut = 0;
		return;
	}
	
	switch(mivi){
		xcase MIVI_F32_DIRECTION_SCALE:{
			MINMAX1(*valueInOut, 0.f, 1.f);
		}

		xcase MIVI_F32_ROLL_YAW:
		xcase MIVI_F32_FACE_YAW:
		acase MIVI_F32_MOVE_YAW:{
			MINMAX1(*valueInOut, -PI, PI);
		}
		
		xcase MIVI_F32_PITCH:
		acase MIVI_F32_TILT:{
			MINMAX1(*valueInOut, -PI * 0.5f, PI * 0.5f);
		}
		
		xdefault:{
			assertmsgf(0, "Please add a sanity check for f32 input index %d", mivi);
		}
	}
}

S32 mmInputEventSetValueF32Tracked(	MovementManager* mm,
									MovementInputValueIndex mivi,
									F32	value,
									U32 msTime,
									const char* fileName,
									U32 fileLine)
{
	MovementInputUnsplitQueue*	q = SAFE_MEMBER2(mm, fg.mcma, inputUnsplitQueue);
	const U32					arrayIndex = mivi - MIVI_F32_LOW;
	MovementInputEvent*			mie;
	
	if(	!q ||
		arrayIndex >= MIVI_F32_COUNT)
	{
		return 0;
	}

	mmSanitizeInputValueF32(mivi, &value);

	if(q->values.lastQueued.f32[arrayIndex] == value){
		return 0;
	}

	if(MMLOG_IS_ENABLED(mm)){
		const char* inputName;
		
		mmGetInputValueIndexName(mivi, &inputName);
		
		mmLog(	mm,
				NULL,
				"[fg.input] Queueing input f32[%s] = %1.3f [%8.8x], %ums (%s:%u)",
				inputName,
				value,
				*(S32*)&value,
				msTime,
				getFileNameConst(fileName),
				fileLine);
	}

	mmInputEventCreateNonZeroed(&mie,
								mm->fg.mcma->mc,
								q,
								mivi,
								msTime);

	mie->value.f32 = value;
	
	// Change the current value.

	q->values.lastQueued.f32[arrayIndex] = value;
	
	return 1;
}

S32 mmInputEventResetAllValues(MovementManager* mm){
	MovementInputUnsplitQueue*	q = SAFE_MEMBER2(mm, fg.mcma, inputUnsplitQueue);
	MovementInputEvent*			mie;
	
	if(!q){
		return 0;
	}

	mmInputEventCreateNonZeroed(&mie,
								mm->fg.mcma->mc,
								q,
								MIVI_RESET_ALL_VALUES,
								0);

	ZeroStruct(&q->values.lastQueued);
	ZeroStruct(&q->lastBit);
	
	mie->value.u32 = mm->fg.mcma->setPos.versionReceived;

	return 1;
}

S32 mmInputEventDebugCommand(	MovementManager* mm,
								const char* command,
								U32 msTime)
{
	MovementInputUnsplitQueue*	q = SAFE_MEMBER2(mm, fg.mcma, inputUnsplitQueue);
	MovementInputEvent*			mie;
	
	if(	!q ||
		!command)
	{
		return 0;
	}

	mmInputEventCreateNonZeroed(&mie,
								mm->fg.mcma->mc,
								q,
								MIVI_DEBUG_COMMAND,
								msTime);

	mie->commandMutable = strdup(command);
	mie->value.command = mie->commandMutable;

	return 1;
}

S32 mmGetLastQueuedInputValueBitFG(	MovementManager* mm, 
									MovementInputValueIndex mivi)
{
	// MS: This is hacky crap that can't be used correctly.

	const MovementInputUnsplitQueue*	q = SAFE_MEMBER2(mm, fg.mcma, inputUnsplitQueue);
	const U32							arrayIndex = mivi - MIVI_BIT_LOW;

	if(	!q ||
		arrayIndex >= MIVI_BIT_COUNT)
	{
		return 0;
	}

	return q->values.lastQueued.bit[arrayIndex];
}

MP_DEFINE(MovementInputStep);

void mmInputStepCreate(	MovementClient* mc,
						MovementInputStep** miStepOut)
{
	MovementInputStep* miStep = NULL;
	
	if(mc){
		miStep = eaPop(&mc->available.miStepsMutable);
	}
	
	if(miStep){
		assert(!miStep->mieList.head);
		assert(!miStep->mieList.tail);
		
		ZeroStruct(&miStep->fg);
		ZeroStruct(&miStep->bg);
		
		ZeroStruct(&miStep->pc);
		
		*miStepOut = miStep;
	}else{
		MP_CREATE_COMPACT(MovementInputStep, 64, 256, 0.80);

		*miStepOut = MP_ALLOC(MovementInputStep);
	}
}

void mmInputStepReclaim(MovementClient* mc,
						MovementInputStep* miStep)
{
	eaPush(	&mc->available.miStepsMutable,
			miStep);

	mmInputEventListReclaim(mc, &miStep->mieListMutable);
}

void mmInputStepDestroy(MovementInputStep** miStepInOut){
	MovementInputStep* miStep = SAFE_DEREF(miStepInOut);

	if(!miStep){
		return;
	}
	
	assert(!miStep->mciStep);
	
	while(miStep->mieList.head){
		MovementInputEvent* next = miStep->mieList.head->next;
		
		mmInputEventDestroy(&miStep->mieListMutable.head);
		miStep->mieListMutable.head = next;
	}
	
	miStep->mieListMutable.tail = NULL;
	
	//ecStepDestroy(&miStep->ecStep);

	MP_FREE(MovementInputStep, *miStepInOut);
}

S32	mmRequesterGetByNameFG(	MovementManager* mm,
							const char* name,
							MovementRequester** mrOut)
{
	if(	!mm ||
		!name ||
		!mrOut)
	{
		return 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester* mr = mm->fg.requesters[i];
		
		if(	!mr->fg.flags.destroyed &&
			!stricmp(mr->mrc->name, name))
		{
			*mrOut = mr;
			return 1;
		}
	}
	EARRAY_FOREACH_END;

	*mrOut = NULL;

	return 0;
}

U32 mmGetFrameCount(void){
	return mgState.frameCount;
}

static void mmOutputPoolLock(void){
	csEnter(&mgState.cs.movementOutputPool);
}

static void mmOutputPoolUnlock(void){
	csLeave(&mgState.cs.movementOutputPool);
}

static S32 mmAnimValuesAddBit(	MovementAnimValues* anim,
								S32 bitHandle)
{
	if(eaiFind(&anim->values, bitHandle) >= 0){
		return 0;
	}

	eaiPush(&anim->values, bitHandle);
	
	return 1;
}

static S32 mmAnimValuesContains(	const MovementAnimValues* anim,
									U32 bitHandle)
{
	return eaiFind(&anim->values, bitHandle) >= 0;
}

void mmAnimBitListDestroyAll(MovementAnimValues* anim){
	if(anim->values){
		eaiDestroy(&anim->values);
	}
}

void EnterRegisteredAnimBitCS(void){
	csEnter(&mgState.cs.registeredAnimBits);
}

void LeaveRegisteredAnimBitCS(void){
	csLeave(&mgState.cs.registeredAnimBits);
}

void mmMovementRegisteredAnimBitComboDestroy(MovementRegisteredAnimBitCombo *pCombo)
{
	eaiDestroy(&pCombo->bits);
	free(pCombo);
}

void mmAnimBitRegistryClear(MovementAnimBitRegistry* abr){
	eaDestroyEx(&abr->handleToBitMutable, NULL);
	eaDestroyEx(&abr->allCombos, mmMovementRegisteredAnimBitComboDestroy);
	stashTableDestroySafe(&abr->nameToBit);
	stashTableDestroySafe(&abr->namesToCombo);
	abr->noBitsCombo = NULL;
}

U32 mmGetAnimBitHandleByName(	const char* bitName,
								S32 isFlashBit)
{
	return mmRegisteredAnimBitCreate(	&mgState.animBitRegistry,
										bitName,
										isFlashBit,
										NULL);
}

U32 mmRegisteredAnimBitCreate(	MovementAnimBitRegistry* abr,
								const char* bitName,
								S32 isFlashBit,
								const MovementRegisteredAnimBit** bitOut)
{
	MovementRegisteredAnimBit*	bit = NULL;
	char						keyNameBuffer[MAX_PATH];
	const char*					keyName;
	
	PERFINFO_AUTO_START_FUNC();

	if(gConf.bNewAnimationSystem){
		keyName = bitName;
		isFlashBit = 0;
	}else{
		isFlashBit = !!isFlashBit;

		assert(strlen(bitName) <= ARRAY_SIZE(keyNameBuffer) - 4);

		if(isFlashBit){
			STR_COMBINE_SS(keyNameBuffer, bitName, ".1");
			keyName = keyNameBuffer;
		}else{
			keyName = bitName;
		}
	}

	readLockU32(&abr->bitLock);
	{
		if(!abr->nameToBit){
			writeLockU32(&abr->bitLock, 1);
			{
				if(!abr->nameToBit){
					abr->nameToBit = stashTableCreateWithStringKeys(100, StashDefault);
				}
			}
			writeUnlockU32(&abr->bitLock);	
		}
		
		if(!stashFindPointer(abr->nameToBit, keyName, &bit)){
			writeLockU32(&abr->bitLock, 1);
			{
				PERFINFO_AUTO_START("Adding bit", 1);

				if(!abr->flags.isServerRegistry){
					if(!eaSize(&abr->handleToBit)){
						bit = callocStruct(MovementRegisteredAnimBit);

						bit->bitName = allocAddString("");
						bit->keyName = allocAddString("");
						
						bit->index = eaPush(&abr->handleToBitMutable, bit);

						assert(!bit->index);

						if(!stashAddPointer(abr->nameToBit, bit->keyName, bit, false)){
							assert(0);
						}

						abr->bitCount++;
					}
				}

				if(!stashFindPointer(abr->nameToBit, keyName, &bit)){
					bit = callocStruct(MovementRegisteredAnimBit);

					bit->bitName = allocAddString(bitName);
					bit->keyName = allocAddString(keyName);
					bit->flags.isFlashBit = isFlashBit;

					bit->index = eaPush(&abr->handleToBitMutable, bit);

					if(!stashAddPointer(abr->nameToBit, bit->keyName, bit, false)){
						assert(0);
					}

					abr->bitCount++;
					
					if(	!gConf.bNewAnimationSystem &&
						!abr->flags.isServerRegistry)
					{
						if(isFlashBit){
							MovementRegisteredAnimBit* bitNonFlash;

							if(stashFindPointer(abr->nameToBit, bitName, &bitNonFlash)){
								bit->flags.hasNonFlashHandle = 1;
								bit->bitHandleLocalNonFlash = bitNonFlash->index;
							}
						}else{
							MovementRegisteredAnimBit* bitFlash;

							STR_COMBINE_SS(keyNameBuffer, bitName, ".1");
							
							if(stashFindPointer(abr->nameToBit, keyNameBuffer, &bitFlash)){
								bitFlash->flags.hasNonFlashHandle = 1;
								bitFlash->bitHandleLocalNonFlash = bit->index;
							}
						}
					}
				}
			}
			writeUnlockU32(&abr->bitLock);

			#if 0
			{
				printf(	"Added net anim bit %d: %s (%d)\n",
						bit->index,
						bit->bitName,
						bit->flags.isFlashBit);
			}
			#endif

			PERFINFO_AUTO_STOP();
		}
	}
	readUnlockU32(&abr->bitLock);

	if(bitOut){
		*bitOut = bit;
	}

	PERFINFO_AUTO_STOP();

	return bit->index;
}

static S32 mmRegisteredAnimBitTranslateHandle(	MovementAnimBitRegistry* abrSource,
												U32 bitHandleSource,
												MovementAnimBitRegistry* abrTarget,
												const MovementRegisteredAnimBit** bitOut)
{
	MovementRegisteredAnimBit* sourceBit = NULL;
	
	readLockU32(&abrSource->bitLock);
	{
		if(bitHandleSource < eaUSize(&abrSource->handleToBit)){
			sourceBit = abrSource->handleToBit[bitHandleSource];
		}
	}
	readUnlockU32(&abrSource->bitLock);

	if(!sourceBit){
		return 0;
	}
	
	if(!sourceBit->flags.foundLocalHandle){
		EnterRegisteredAnimBitCS();

		if(!sourceBit->flags.foundLocalHandle){
			sourceBit->bitHandleLocal = mmRegisteredAnimBitCreate(	abrTarget,
																	sourceBit->bitName,
																	sourceBit->flags.isFlashBit,
																	&sourceBit->bitLocal);
		}

		LeaveRegisteredAnimBitCS();

		sourceBit->flags.foundLocalHandle = 1;
	}
	
	if(bitOut){
		*bitOut = sourceBit->bitLocal;
	}

	return 1;
}

void mmGetLocalAnimBitHandleFromServerHandle(	U32 bitHandleServer,
												U32* bitHandleLocalOut,
												U32 hasHandleId)
{
	MovementRegisteredAnimBit* bitLocal;
	U32 uid = 0;

	if (hasHandleId) {
		uid = MM_ANIM_HANDLE_GET_ID(bitHandleServer);
		bitHandleServer = MM_ANIM_HANDLE_WITHOUT_ID(bitHandleServer);
	}
	
	mmRegisteredAnimBitTranslateHandle(	&mgState.fg.netReceiveMutable.animBitRegistry,
										bitHandleServer,
										&mgState.animBitRegistry,
										&bitLocal);
	
	if (hasHandleId) {
		*bitHandleLocalOut = MM_ANIM_HANDLE_WITH_ID(bitLocal->index,uid);
	} else {
		*bitHandleLocalOut = bitLocal->index;
	}
}

void mmTranslateAnimBitServerToClient(	U32* animBitHandle,
										U32 hasHandleId)
{
	mmGetLocalAnimBitHandleFromServerHandle(*animBitHandle,
											animBitHandle,
											hasHandleId);
}

void mmTranslateAnimBitsServerToClient(	U32* animBitHandles,
										U32 hasHandleId)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(animBitHandles, i, isize);
	{
		mmGetLocalAnimBitHandleFromServerHandle(animBitHandles[i],
												&animBitHandles[i],
												hasHandleId);
	}
	EARRAY_FOREACH_END;
}

S32 mmRegisteredAnimBitGetByHandle(	MovementAnimBitRegistry* abr,
									const MovementRegisteredAnimBit**const bitOut,
									U32 bitHandle)
{
	readLockU32(&abr->bitLock);
	{
		if(bitHandle >= eaUSize(&abr->handleToBit)){
			readUnlockU32(&abr->bitLock);

			return 0;
		}else{
			ANALYSIS_ASSUME(abr->handleToBit);

			if(bitOut){
				*bitOut = abr->handleToBit[bitHandle];
			}
		}
	}
	readUnlockU32(&abr->bitLock);

	return 1;
}

void mmRegisteredAnimBitComboFind(	MovementAnimBitRegistry* abr,
									const MovementRegisteredAnimBitCombo** comboOut,
									const MovementRegisteredAnimBitCombo* comboPrev,
									const U32* bits)
{
	MovementRegisteredAnimBitCombo*	combo;
	char							keyName[MAX_PATH];
	
	PERFINFO_AUTO_START_FUNC();

	combo = NULL;
	keyName[0] = 0;
	
	if(	comboPrev &&
		eaiUSize(&bits) == eaiUSize(&comboPrev->bits))
	{
		S32 same = 1;

		EARRAY_INT_CONST_FOREACH_BEGIN(bits, i, isize);
		{
			if(bits[i] != comboPrev->bits[i]){
				same = 0;
				break;
			}
		}
		EARRAY_FOREACH_END;
		
		if(same){
			*comboOut = comboPrev;
			PERFINFO_AUTO_STOP();
			return;
		}
	}
	
	if(eaiUSize(&bits)){
		PERFINFO_AUTO_START("makeKeyName", 1);
		{
			STR_COMBINE_BEGIN(keyName);
			{
				EARRAY_INT_CONST_FOREACH_BEGIN(bits, i, isize);
				{
					if(i){
						STR_COMBINE_CAT(".");
					}
					
					assert(bits[i] < abr->bitCount);

					{
						U32 b = bits[i];
						if(!b){
							STR_COMBINE_CAT("0");
						}else{
							char	buffer[20];
							S32		j = ARRAY_SIZE(buffer) - 1;
							
							buffer[j] = 0;
							
							while(b){
								buffer[--j] = '0' + (b % 10);
								b /= 10;
							}
							
							STR_COMBINE_CAT(buffer + j);
						}
					}
				}
				EARRAY_FOREACH_END;
			}	
			STR_COMBINE_END(keyName);
		}
		PERFINFO_AUTO_STOP();
	}
				
	readLockU32(&abr->comboLock);
	{
		if(!abr->namesToCombo){
			writeLockU32(&abr->comboLock, 1);
			{
				if(!abr->namesToCombo){
					abr->namesToCombo = stashTableCreateWithStringKeys(100, StashDefault|StashCaseSensitive);
				}
			}
			writeUnlockU32(&abr->comboLock);
		}else{
			if(!eaiUSize(&bits)){
				combo = abr->noBitsCombo;
			}else{
				stashFindPointer(abr->namesToCombo, keyName, &combo);
			}
		}

		if(!combo){
			writeLockU32(&abr->comboLock, 1);
			{
				if(!eaiUSize(&bits)){
					combo = abr->noBitsCombo;
				}else{
					stashFindPointer(abr->namesToCombo, keyName, &combo);
				}
				if(!combo){
					PERFINFO_AUTO_START("createCombo", 1);
					{
						combo = callocStruct(MovementRegisteredAnimBitCombo);

						combo->keyName = allocAddString(keyName);
						combo->index = eaPush(&abr->allCombos, combo);
						
  						eaiSetSize(&combo->bits, eaiUSize(&bits));
						CopyStructs(combo->bits, bits, eaiUSize(&bits));

						if(!stashAddPointer(abr->namesToCombo, combo->keyName, combo, false)){
							assert(0);
						}

						if(!eaiUSize(&bits)){
							abr->noBitsCombo = combo;
						}
					}
					PERFINFO_AUTO_STOP();
				}
			}
			writeUnlockU32(&abr->comboLock);
		}
	}
	readUnlockU32(&abr->comboLock);
	
	if(comboOut){
		*comboOut = combo;
	}
	
	PERFINFO_AUTO_STOP();
}

void mmRequesterMsgInit(MovementRequesterMsgPrivateData* pd,
						MovementRequesterMsgOut* out,
						MovementRequester* mr,
						MovementRequesterMsgType msgType,
						U32 toSlot)
{
	if(	!pd ||
		toSlot > 1)
	{
		return;
	}

	ZeroStruct(pd);
	ZeroStruct(out);

	pd->msg.pd = pd;

	pd->msg.out = out;

	pd->msg.in.msgType = msgType;

	pd->msgType = msgType;

	if(mr){
		if(toSlot == MM_FG_SLOT){
			pd->msg.in.fg.mr = mr;
			pd->msg.in.userStruct.fg = mr->userStruct.fg;
			pd->msg.in.userStruct.sync = mr->userStruct.sync.fg;
			pd->msg.in.userStruct.syncPublic = mr->userStruct.syncPublic.fg;
		}else{
			pd->msg.in.userStruct.bg = mr->userStruct.bg;
			pd->msg.in.userStruct.localBG = mr->userStruct.localBG;
			pd->msg.in.userStruct.sync = mr->userStruct.sync.bg;
			pd->msg.in.userStruct.syncPublic = mr->userStruct.syncPublic.bg;
		}

		if(MMLOG_IS_ENABLED(mr->mm)){
			pd->msg.in.flags.debugging = 1;
		}

		pd->mm = mr->mm;
		pd->mrc = mr->mrc;
		pd->mr = mr;
	}
}

void mmRequesterMsgSend(MovementRequesterMsgPrivateData* pd){
	if(!SAFE_MEMBER2(pd, mrc, msgHandler)){
		return;
	}

	pd->mrc->msgHandler(&pd->msg);
}

void mmRequesterMsgInitFG(	MovementRequesterMsgPrivateData* pd,
							MovementRequesterMsgOut* out,
							MovementRequester* mr,
							MovementRequesterMsgType msgType)
{
	mmRequesterMsgInit(pd, out, mr, msgType, MM_FG_SLOT);
}

static S32 mmFindRequesterClassByName(	MovementRequesterClass** mrcOut,
										const char* name,
										MovementRequesterClass***const classArray,
										S32 remove)
{
	if(!name){
		return 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(classArray[0], i, size);
	{
		MovementRequesterClass* mrc = classArray[0][i];

		if(	mrc &&
			!stricmp(name, mrc->name))
		{
			*mrcOut = mrc;

			if(remove){
				eaRemoveFast(classArray, i);
			}

			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

S32 mrNameRegisterID(	const char* name,
						U32 id)
{
	MovementRequesterClass* mrc;

	assert(!mgState.fg.flags.classesFinalized);

	if(!name){
		return 0;
	}

	if(!id){
		Errorf("Invalid MovementRequesterClass id 0.");
		return 0;
	}

	if(id >= 1000){
		Errorf("Seriously, there can't be this many (%d) requester classses.", id);
		return 0;
	}

	if(id >= eaUSize(&mgState.mr.idToClass)){
		eaSetSize(&mgState.mr.idToClass, id + 1);
	}

	if(mgState.mr.idToClass[id]){
		Errorf(	"MovementRequesterClass %d is already registered as \"%s\".",
				id,
				mgState.mr.idToClass[id]->name);
		return 0;
	}

	// Create the requester class.

	if(!mmFindRequesterClassByName(	&mrc,
									name,
									&mgState.mr.unregisteredClasses,
									1))
	{
		mrc = callocStruct(MovementRequesterClass);
		mrc->name = allocAddCaseSensitiveString(name);
	}

	mrc->id = id;

	mgState.mr.idToClass[id] = mrc;

	return 1;
}

static S32 mmGetClassIDFromMsgHandler(	U32* idOut,
										MovementRequesterMsgHandler msgHandler)
{
	if(	!msgHandler ||
		!idOut ||
		!mgState.mr.msgHandlerToID)
	{
		return 0;
	}

	stashFindInt(mgState.mr.msgHandlerToID, msgHandler, idOut);

	return !!*idOut;
}

static S32 mmSetRequesterClassID(	MovementRequesterMsgHandler msgHandler,
									U32 id)
{
	if(	!msgHandler ||
		!id)
	{
		return 0;
	}

	if(!mgState.mr.msgHandlerToID){
		mgState.mr.msgHandlerToID = stashTableCreateAddress(10);
	}

	stashAddInt(mgState.mr.msgHandlerToID, msgHandler, id, 1);

	return 1;
}

void mmSetIsServer(void){
	mgState.flagsMutable.isServer = 1;
	mgPublic.isServer = 1;
}

static void mmFinalizeClasses(void){
	if(!FALSE_THEN_SET(mgState.fg.flagsMutable.classesFinalized)){
		return;
	}

	EARRAY_CONST_FOREACH_BEGIN(mgState.mr.idToClass, i, size);
	{
		MovementRequesterClass* mrc = mgState.mr.idToClass[i];

		if(!mrc){
			continue;
		}

		if(!mrc->msgHandler){
			Errorf(	"MovementRequesterClass \"%s\" (id %d) has no msgHandler.",
					mrc->name,
					i);

			continue;
		}

		mmSetRequesterClassID(mrc->msgHandler, i);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mgState.mr.unregisteredClasses, i, size);
	{
		MovementRequesterClass* mrc = mgState.mr.unregisteredClasses[i];

		Errorf(	"MovementRequesterClass \"%s\" was never registered with an ID.",
				mrc->name);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mgState.mmr.idToClass, i, size);
	{
		MovementManagedResourceClass* mmrc = mgState.mmr.idToClass[i];

		if(!mmrc){
			continue;
		}

		if(!mmrc->msgHandler){
			Errorf(	"MovementManagedResourceClass \"%s\" (id %d) has no msgHandler.",
					mmrc->name,
					i);

			continue;
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mgState.mmr.unregisteredClasses, i, size);
	{
		MovementManagedResourceClass* mmrc = mgState.mmr.unregisteredClasses[i];

		Errorf(	"MovementManagedResourceClass \"%s\" was never registered with an ID.",
				mmrc->name);
	}
	EARRAY_FOREACH_END;
}

S32 mmRequesterMsgHandlerRegisterName(	MovementRequesterMsgHandler msgHandler,
										const char* name,
										const MovementRequesterClassParseTables* ptis,
										S32 syncToClient)
{
	MovementRequesterClass* mrc;

	assert(!mgState.fg.flags.classesFinalized);

	if(	!msgHandler ||
		!name ||
		!ptis->fg ||
		!ptis->bg ||
		!ptis->localBG ||
		!ptis->toFG ||
		!ptis->toBG ||
		!ptis->sync)
	{
		Errorf("Requester registration is missing a required value.");
		return 0;
	}

	if(!mmFindRequesterClassByName(	&mrc,
									name,
									&mgState.mr.idToClass,
									0))
	{
		mrc = callocStruct(MovementRequesterClass);

		eaPush(&mgState.mr.unregisteredClasses, mrc);
	}

	mrc->msgHandler = msgHandler;
	mrc->name = allocAddCaseSensitiveString(name);
	mrc->pti = *ptis;
	mrc->flags.syncToClient = !!syncToClient;

	FOR_BEGIN(i, MRC_PT_COUNT);
		char		buffer[100];
		const char* timerName;

		switch(i){
			xcase MRC_PT_COPY_LATEST_FROM_BG:
				timerName = "StructCopy";
			xcase MRC_PT_CREATE_TOBG:
				timerName = "CreateToBG";
			xcase MRC_PT_UPDATED_TOFG:
				timerName = "UpdatedToFG";
				
			xcase MRC_PT_UPDATED_TOBG:
				timerName = "UpdatedToBG";
			xcase MRC_PT_UPDATED_SYNC:
				timerName = "UpdatedSync";
			xcase MRC_PT_INPUT_EVENT:
				timerName = "InputEvent";
			xcase MRC_PT_BEFORE_DISCUSSION:
				timerName = "BeforeDiscussion";
			xcase MRC_PT_DISCUSS_DATA_OWNERSHIP:
				timerName = "DiscussDataOwnership";

			xcase MRC_PT_OUTPUT_POSITION_TARGET:
				timerName = "OutputPositionTarget";
			xcase MRC_PT_OUTPUT_POSITION_CHANGE:
				timerName = "OutputPositionChange";
			xcase MRC_PT_OUTPUT_ROTATION_TARGET:
				timerName = "OutputRotationTarget";
			xcase MRC_PT_OUTPUT_ROTATION_CHANGE:
				timerName = "OutputRotationChange";
			xcase MRC_PT_OUTPUT_ANIMATION:
				timerName = "OutputAnimation";

			xcase MRC_PT_OUTPUT_DETAILS:
				timerName = "OutputDetails";

			xcase MRC_PT_BITS_SENT:
				timerName = "BitsSent";
			xcase MRC_PT_BITS_RECEIVED:
				timerName = "BitsReceived";
				
			xcase MRC_PT_COPY_STATE_TOFG:
				timerName = "CopyStateToFG";

			xdefault:{
				sprintf(buffer, "mrc_pt[%d]", i);
				timerName = buffer;
			}
		}

		assert(i < ARRAY_SIZE(mrc->perfInfo));

		mrc->perfInfo[i].name = strdupf("%s - %s",
										timerName,
										mrc->name);
	FOR_END;

	return 1;
}

static U32 mmGetNextRequesterHandle(MovementManager* mm){
	while(1){
		S32 found = 0;

		if(!++mm->lastRequesterHandle){
			mm->lastRequesterHandle = 1;
		}

		EARRAY_CONST_FOREACH_BEGIN(mm->allRequesters, i, size);
		{
			MovementRequester* mr = mm->allRequesters[i];

			if(mr->handle == mm->lastRequesterHandle){
				found = 1;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!found){
			break;
		}
	}

	return mm->lastRequesterHandle;
}

MP_DEFINE(MovementRequester);

S32 mmRequesterCreateBasic(	MovementManager* mm,
							MovementRequester** mrOut,
							MovementRequesterMsgHandler msgHandler)
{
	return mmRequesterCreate(	mm,
								mrOut,
								NULL,
								msgHandler,
								0);
}

S32 mmRequesterCreateBasicByName(	MovementManager* mm,
									MovementRequester** mrOut,
									const char* name)
{
	MovementRequesterClass* mrc;

	if(!mmFindRequesterClassByName(&mrc, name, &mgState.mr.idToClass, 0)){
		return 0;
	}

	return mmRequesterCreateBasic(mm, mrOut, mrc->msgHandler);
}

S32 mmRequesterCreate(	MovementManager* mm,
						MovementRequester** mrOut,
						U32* mrHandleOut,
						MovementRequesterMsgHandler msgHandler,
						U32 id)
{
	MovementRequester*		mr;
	MovementRequesterClass* mrc;

	if(	!mm
		||
		mm->fg.flags.destroyed &&
		mmIsForegroundThread()
		||
		!msgHandler &&
		!id)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("start", 1);
	if(!id){
		mmGetClassIDFromMsgHandler(&id, msgHandler);

		if(!id){
			Errorf("Invalid msgHandler.");
			PERFINFO_AUTO_STOP();// start.
			PERFINFO_AUTO_STOP();// FUNC.
			return 0;
		}
	}

	if(	id >= eaUSize(&mgState.mr.idToClass) ||
		!mgState.mr.idToClass[id])
	{
		Errorf("Invalid requester ID (%d).", id);
		PERFINFO_AUTO_STOP();// start.
		PERFINFO_AUTO_STOP();// FUNC.
		return 0;
	}

	mrc = mgState.mr.idToClass[id];

	if(	msgHandler &&
		msgHandler != mrc->msgHandler)
	{
		Errorf("Mismatched message handler and ID.");
		PERFINFO_AUTO_STOP();// start.
		PERFINFO_AUTO_STOP();// FUNC.
		return 0;
	}

	PERFINFO_AUTO_STOP_START("allocs", 1);// start

	mmRequesterLockAcquire();
	{
		MP_CREATE_COMPACT(MovementRequester, 10, 20, 0.80);

		mr = MP_ALLOC(MovementRequester);

		mr->mrc = mrc;

		mrc->instanceCount++;

		mr->handle = mmGetNextRequesterHandle(mm);

		eaPush(&mm->allRequesters, mr);
	}
	mmRequesterLockRelease();

	mr->mm = mm;

	mrLog(mr, NULL, "Created requester.");

	mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
	mr->bg.handledMsgsMutable = MR_HANDLED_MSGS_DEFAULT;

	// Allocate the user memory.

	mmStructAllocIfNull(mrc->pti.fg,
						mr->userStruct.fg,
						mm);

	mmStructAllocIfNull(mrc->pti.bg,
						mr->userStruct.bg,
						mm);
						
	mmStructAllocIfNull(mrc->pti.localBG,
						mr->userStruct.localBG,
						mm);

	ARRAY_FOREACH_BEGIN(mr->userStruct.toFG, i);
	{
		mmStructAllocIfNull(mrc->pti.toFG,
							mr->userStruct.toFG[i],
							mm);
							
		mmStructAllocIfNull(mrc->pti.toBG,
							mr->userStruct.toBG[i],
							mm);
	}
	ARRAY_FOREACH_END;

	mmStructAllocIfNull(mrc->pti.sync,
						mr->userStruct.sync.fg,
						mm);

	mmStructAllocIfNull(mrc->pti.sync,
						mr->userStruct.sync.bg,
						mm);

	if(mrc->pti.syncPublic){
		mmStructAllocIfNull(mrc->pti.syncPublic,
							mr->userStruct.syncPublic.fg,
							mm);

		mmStructAllocIfNull(mrc->pti.syncPublic,
							mr->userStruct.syncPublic.bg,
							mm);
	}

	PERFINFO_AUTO_STOP();// allocs.

	// Add requester to the appropriate FG or BG list.

	if(mmIsForegroundThread()){
		MovementThreadData* td = MM_THREADDATA_FG(mm);

		mm->fg.flagsMutable.mrIsNewToSend = 1;
		mm->fg.flagsMutable.mrNeedsAfterSend = 1;

		ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.inList);
		eaPush(&mm->fg.requestersMutable, mr);
		
		assert(!mr->fg.flags.forcedSimIsEnabled);

		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.hasNewRequesters = 1;
		eaPush(&td->toBG.newRequestersMutable, mr);

		ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.inListBG);

		ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.createdInFG);
		ASSERT_FALSE_AND_SET(mr->bg.flagsMutable.createdInFG);

		mr->pcLocalWhenCreated = mgState.fg.frame.next.pcStart;
	}else{
		mmRequesterAddNewToListBG(mm, mr);
	}

	if(mrOut){
		*mrOut = mr;
	}

	if(mrHandleOut){
		*mrHandleOut = mr->handle;
	}

	PERFINFO_AUTO_STOP();// FUNC.

	return 1;
}

static void mmRequesterDestroyInternal(	MovementManager* mm,
										MovementRequester* mr)
{
	MovementRequesterClass* mrc = mr->mrc;

	assert(mr->fg.flags.destroyed);
	ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.destroying);

	#if MM_VERIFY_SENT_REQUESTERS
	{
		if(	mgState.flags.isServer &&
			mm->fg.mcma)
		{
			if(!mr->fg.flags.sentCreate){
				printf(	"Destroying requester that was never sent (%d,%d), handle %u, id %u.\n",
						mr->fg.flags.didSendCreate,
						mr->fg.flags.didSendDestroy,
						mr->handle,
						mr->mrc->id);
			}else{
				printf(	"Destroying requester that was sent (%d,%d), handle %u, id %u.\n",
						mr->fg.flags.didSendCreate,
						mr->fg.flags.didSendDestroy,
						mr->handle,
						mr->mrc->id);
			}
		}

		assert(mr->fg.flags.didSendCreate == mr->fg.flags.didSendDestroy);
	}
	#endif

	assert(!mr->fg.flags.inList);
	assert(!mr->bg.flags.inList);

	// Tell the requester that it's being destroyed.

	FOR_BEGIN(i, 2);
	{
		MovementRequesterMsgPrivateData pd;

		mmRequesterMsgInitFG(&pd, NULL, mr, MR_MSG_FG_BEFORE_DESTROY);
		
		pd.msg.in.userStruct.bg = mr->userStruct.bg;
		pd.msg.in.userStruct.localBG = mr->userStruct.localBG;
		pd.msg.in.userStruct.sync = i ? mr->userStruct.sync.bg : mr->userStruct.sync.fg;
		pd.msg.in.userStruct.toFG = mr->userStruct.toFG[i];
		pd.msg.in.userStruct.toBG = mr->userStruct.toBG[i];
		
		mmRequesterMsgSend(&pd);
	}
	FOR_END;
	
	// Tell the mm owner that it's being destroyed.
	
	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_REQUESTER_DESTROYED;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.fg.requesterDestroyed.mr = mr;
		
		mm->msgHandler(&pd.msg);
	}
	
	// Destroy the user structs.

	FOR_BEGIN(i, 2);
	{
		MovementRequesterThreadData* mrtd = &mr->threadData[i];

		// toBG.

		mmStructDestroy(mrc->pti.sync,
						mrtd->toBG.userStruct.sync,
						mm);

		mmStructDestroy(mrc->pti.syncPublic,
						mrtd->toBG.userStruct.syncPublic,
						mm);

		if(mrtd->toBG.predict){
			mmStructDestroy(mrc->pti.bg,
							mrtd->toBG.predict->userStruct.serverBG,
							mm);

			SAFE_FREE(mrtd->toBG.predict);
		}
		
		// toFG.

		if(mrtd->toFG.predict){
			mmStructDestroy(mrc->pti.bg,
							mrtd->toFG.predict->userStruct.bg,
							mm);

			SAFE_FREE(mrtd->toFG.predict);
		}
	}
	FOR_END;
	
	eaDestroy(&mr->bg.resourcesMutable);

	mmStructDestroy(mrc->pti.fg,
					mr->userStruct.fg,
					mm);

	mmStructDestroy(mrc->pti.bg,
					mr->userStruct.bg,
					mm);

	mmStructDestroy(mrc->pti.localBG,
					mr->userStruct.localBG,
					mm);

	ARRAY_FOREACH_BEGIN(mr->userStruct.toFG, i);
	{
		mmStructDestroy(mrc->pti.toFG,
						mr->userStruct.toFG[i],
						mm);
						
		mmStructDestroy(mrc->pti.toBG,
						mr->userStruct.toBG[i],
						mm);
	}
	ARRAY_FOREACH_END;

	mmStructDestroy(mrc->pti.sync,
					mr->userStruct.sync.fg,
					mm);
					
	mmStructDestroy(mrc->pti.sync,
					mr->userStruct.sync.bg,
					mm);

	mmStructDestroy(mrc->pti.syncPublic,
					mr->userStruct.syncPublic.fg,
					mm);

	mmStructDestroy(mrc->pti.syncPublic,
					mr->userStruct.syncPublic.fgToQueue,
					mm);

	mmStructDestroy(mrc->pti.syncPublic,
					mr->userStruct.syncPublic.bg,
					mm);

	mmStructDestroy(mrc->pti.bg,
					mr->fg.net.prev.userStruct.bg,
					mm);
					
	mmStructDestroy(mrc->pti.sync,
					mr->userStruct.sync.fgToQueue,
					mm);
					
	mmStructDestroy(mrc->pti.syncPublic,
					mr->userStruct.syncPublic.fgToQueue,
					mm);

	mmStructDestroy(mrc->pti.sync,
					mr->fg.net.prev.userStruct.sync,
					mm);

	mmStructDestroy(mrc->pti.syncPublic,
					mr->fg.net.prev.userStruct.syncPublic,
					mm);

	mmRequesterLockAcquire();
	{
		if(eaFindAndRemove(&mm->allRequesters, mr) < 0){
			assertmsg(0, "Requester missing from allRequesters array.");
		}

		MP_FREE(MovementRequester, mr);
	}
	mmRequesterLockRelease();
}

static void mmRequesterDestroyFG(MovementRequester* mr){
	if(FALSE_THEN_SET(mr->fg.flagsMutable.destroyed)){
		MovementManager*	mm = mr->mm;
		MovementThreadData* td = MM_THREADDATA_FG(mr->mm);
		
		// Flag mm to send requester update.
		
		mm->fg.flagsMutable.mrHasDestroyToSend = 1;
		mm->fg.flagsMutable.mrNeedsAfterSend = 1;
		
		// Flag mm to check if mr is ready to be destroyed.

		mm->fg.flagsMutable.mrNeedsDestroy = 1;

		if(	mr->fg.flags.inListBG &&
			FALSE_THEN_SET(mr->fg.flagsMutable.sentRemoveToBG))
		{
			// Send remove to BG.

			MovementRequesterThreadData* mrtd = MR_THREADDATA_FG(mr);

			ASSERT_FALSE_AND_SET(mrtd->toBG.flagsMutable.removeFromList);
			td->toBG.flagsMutable.hasToBG = 1;
			td->toBG.flagsMutable.mrHasUpdate = 1;
			
			if(FALSE_THEN_SET(mrtd->toBG.flagsMutable.hasUpdate)){
				mmExecListAddHead(	&td->toBG.melRequesters,
									&mrtd->toBG.execNode);
			}
		}
	}
}

S32 wrapped_mrDestroy(	MovementRequester** mrInOut,
						const char* fileName,
						const U32 fileLine)
{
	MovementRequester* mr = SAFE_DEREF(mrInOut);

	PERFINFO_AUTO_START("mrDestroy", 1);

	if(!mr){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	mrLog(mr, NULL, "mrDestroy called from %s:%u.", fileName, fileLine);

	if(mmIsForegroundThread()){
		if(	!mr->fg.netHandle ||
			mr->fg.flags.destroyedFromServer)
		{
			// mr has no net handle if it's not synced from the server or if this is the server.
			
			mmRequesterDestroyFG(mr);
		}
	}
	else if(FALSE_THEN_SET(mr->bg.flagsMutable.destroyed)){
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
		MovementThreadData*				td = MM_THREADDATA_BG(mr->mm);
		MovementManager*				mm = mr->mm;

		// Save the local pc when I was destroyed.

		mr->bg.pc.destroyed = mgState.bg.pc.local.cur;

		// Set a flag to know that a requester was destroyed during this step.

		mr->mm->bg.flagsMutable.mrWasDestroyedOnThisStep = 1;
		
		// Tell the FG that I'm destroyed.

		ASSERT_FALSE_AND_SET(mrtd->toFG.flagsMutable.destroyed);

		MM_TD_SET_HAS_TOFG(mm, td);
		td->toFG.flagsMutable.mrHasUpdate = 1;
		
		mmRequesterDestroyPipesBG(mm, mr);
	}

	*mrInOut = NULL;

	PERFINFO_AUTO_STOP();
	return 1;
}

S32 mrmDestroySelf(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequester*					mr = pd->mr;
	
	mrmLog(	msg,
			NULL,
			"Requester destroying self.");

	return mrDestroy(&mr);
}

S32 mrEnableMsgCreateToBG(MovementRequester* mr){
	if(!mr){
		return 0;
	}
	
	mr->mm->fg.flagsMutable.mrHasHandleCreateToBG = 1;
	mr->fg.flagsMutable.handleCreateToBG = 1;
	
	mrLog(	mr,
			NULL,
			"Enabling msg CREATE_TOBG.");

	return 1;
}

static S32 mrEnableMsgUpdatedSyncHelper(MovementRequester* mr,
										S32 needsAfterSync)
{
	if(!mr){
		return 0;
	}
	
	mr->mm->fg.flagsMutable.mrHasSyncToBG = 1;
	mr->mm->fg.flagsMutable.mrHasSyncToSend = 1;
	mr->mm->fg.flagsMutable.mrNeedsAfterSend = 1;

	mr->fg.flagsMutable.hasSyncToBG = 1;
	mr->fg.flagsMutable.hasSyncToSend = 1;

	if(needsAfterSync){
		mr->fg.flagsMutable.needsAfterSync = 1;
	}

	mrLog(	mr,
			NULL,
			"Enabling msg UPDATED_SYNC.");

	return 1;
}

S32 mrEnableMsgUpdatedSync(MovementRequester* mr){
	return mrEnableMsgUpdatedSyncHelper(mr, 0);
}

S32 mrEnableMsgUpdatedSyncWithAfterSync(MovementRequester* mr){
	return mrEnableMsgUpdatedSyncHelper(mr, 1);
}

S32 mrGetFG(MovementRequester* mr,
			MovementRequesterMsgHandler msgHandler,
			void** fgOut)
{
	if(	mr &&
		mr->mrc->msgHandler == msgHandler &&
		!mr->fg.flags.destroyed &&
		fgOut)
	{
		*fgOut = mr->userStruct.fg;

		return 1;
	}

	return 0;
}

S32 mrGetSyncFG(MovementRequester* mr,
				MovementRequesterMsgHandler msgHandler,
				void** syncFGOut,
				void** syncPublicFGOut)
{
	if(	mr &&
		mr->mrc->msgHandler == msgHandler &&
		!mr->fg.flags.destroyed)
	{
		if(syncFGOut){
			*syncFGOut = mr->userStruct.sync.fg;
		}

		if(syncPublicFGOut){
			*syncPublicFGOut = mr->userStruct.syncPublic.fg;
		}

		return 1;
	}

	return 0;
}

S32 mrGetHandleFG(	MovementRequester* mr,
					U32* handleOut)
{
	if(	!mr ||
		!handleOut)
	{
		return 0;
	}
	
	*handleOut = mr->handle;
	
	return 1;
}

void mmRequesterGetSyncDebugString(	MovementRequester* mr,
									char* buffer,
									U32 bufferLen,
									void* useThisSync,
									void* useThisSyncPublic)
{
	MovementRequesterMsgPrivateData pd;
	S32								len;

	if(!mr){
		strcpy_s(buffer, bufferLen, "none");
		return;
	}

	mmRequesterMsgInit(	&pd,
						NULL,
						mr,
						MR_MSG_GET_SYNC_DEBUG_STRING,
						mmIsForegroundThread() ? MM_FG_SLOT : MM_BG_SLOT);

	if(mr->fg.netHandle){
		len = snprintf_s(	buffer,
							bufferLen,
							"%s[%u/%u] SYNC:\n",
							mr->mrc->name,
							mr->handle,
							mr->fg.netHandle);
	}else{
		len = snprintf_s(	buffer,
							bufferLen,
							"%s[%u] SYNC:\n",
							mr->mrc->name,
							mr->handle);
	}

	pd.msg.in.getSyncDebugString.buffer = buffer + len;
	pd.msg.in.getSyncDebugString.bufferLen = bufferLen - len;

	if(useThisSync){
		pd.msg.in.userStruct.sync = useThisSync;
	}

	if(useThisSyncPublic){
		pd.msg.in.userStruct.syncPublic = useThisSyncPublic;
	}

	mmRequesterMsgSend(&pd);

	if(!buffer[len]){
		char* estr = NULL;
		
		estrStackCreate(&estr);
		estrConcatf(&estr, "SYNC:");
		ParserWriteText(&estr, mr->mrc->pti.sync, pd.msg.in.userStruct.sync, 0, 0, 0);

		if(pd.msg.in.userStruct.syncPublic){
			estrConcatf(&estr, "\nSYNC PUBLIC:");
			ParserWriteText(&estr, mr->mrc->pti.syncPublic, pd.msg.in.userStruct.syncPublic, 0, 0, 0);
		}

		len += snprintf_s(buffer + len, bufferLen - len, "%s", estr);
		estrDestroy(&estr);
	}
}

void mmRequesterLockAcquire(void){
	csEnter(&mgState.cs.requesterCreate);
}

void mmRequesterLockRelease(void){
	csLeave(&mgState.cs.requesterCreate);
}

U32 mmGetProcessCountAfterMillisecondsFG(S32 deltaMilliseconds){
	U32 stepOffset = deltaMilliseconds ? deltaMilliseconds * 60 / 1000 : 0;

	if(mmIsForegroundThread()){
		return	mgState.fg.frame.next.pcStart +
				mgState.fg.netReceive.cur.offset.clientToServerSync +
				stepOffset;
	}else{
		return	mgState.bg.pc.local.cur +
				mgState.bg.netReceive.cur.offset.clientToServer +
				stepOffset;
	}
}

U32 mmGetProcessCountAfterSecondsFG(F32 deltaSeconds){
	return mmGetProcessCountAfterMillisecondsFG(deltaSeconds * 1000);
}

MP_DEFINE(MovementOutputRepredict);

S32 mmOutputRepredictCreate(MovementOutputRepredict** morOut){
	MovementOutputRepredict* mor;

	if(!morOut){
		return 0;
	}

	mmOutputPoolLock();
	{
		MP_CREATE_COMPACT(MovementOutputRepredict, 64, 256, 0.80);
		mor = MP_ALLOC(MovementOutputRepredict);
	}
	mmOutputPoolUnlock();

	*morOut = mor;

	return 1;
}

S32 mmOutputRepredictDestroy(MovementOutputRepredict** morInOut){
	MovementOutputRepredict* mor = SAFE_DEREF(morInOut);

	if(!mor){
		return 0;
	}

	mmAnimBitListDestroyAll(&mor->dataMutable.anim);

	mmOutputPoolLock();
	{
		MP_FREE(MovementOutputRepredict, *morInOut);
	}
	mmOutputPoolUnlock();

	return 1;
}

S32 mmOutputRepredictDestroyUnsafe(MovementOutputRepredict* mor){
	return mmOutputRepredictDestroy(&mor);
}

MP_DEFINE(MovementOutput);

#ifndef _WIN64
	STATIC_ASSERT(sizeof(MovementOutput) <= 64 + 28);
#endif

S32 mmOutputCreate(MovementOutput** outputOut){
	MovementOutput* o;

	if(!outputOut){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	mmOutputPoolLock();
	{
		#if 1
		{
			if(!MP_NAME(MovementOutput)){
				MP_NAME(MovementOutput) = createMemoryPoolNamed("MovementOutput", __FILE__, __LINE__);
				mpSetChunkAlignment(MP_NAME(MovementOutput), 64);
				initMemoryPool(MP_NAME(MovementOutput), (sizeof(MovementOutput) + 63) & ~63, 100);
				mpSetMode(MP_NAME(MovementOutput), ZeroMemoryBit);
				mpSetCompactParams(MP_NAME(MovementOutput), 200, 0.80);
			}
		}
		#else
		{
			MP_CREATE_COMPACT(MovementOutput, 100, 256, 0.80);
		}
		#endif

		o = MP_ALLOC(MovementOutput);
	}
	mmOutputPoolUnlock();

	*outputOut = o;
	
	PERFINFO_AUTO_STOP();

	return 1;
}

void mmOutputDataReset(MovementOutputData* data){
	mmAnimBitListDestroyAll(&data->anim);
	ZeroStruct(data);
}

void mmOutputDestroy(MovementOutput** outputInOut){
	MovementOutput* o = SAFE_DEREF(outputInOut);

	if(!o){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	mmOutputDataReset(&o->dataMutable);

	mmOutputPoolLock();
	{
		MP_FREE(MovementOutput, o);
	}
	mmOutputPoolUnlock();

	*outputInOut = NULL;

	PERFINFO_AUTO_STOP();
}

static ThreadSafeMemoryPool MovementNetOutputPool;

S32 mmNetOutputCreate(MovementNetOutput** noOut){
	if(!noOut){
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	ATOMIC_INIT_BEGIN;
	{
		threadSafeMemoryPoolInit(&MovementNetOutputPool, 32, sizeof(**noOut), "MovementNetOutputPool");
	}
	ATOMIC_INIT_END;

	*noOut = threadSafeMemoryPoolAlloc(&MovementNetOutputPool);
	ZeroStruct(*noOut);

	PERFINFO_AUTO_STOP();

	return 1;
}

static void mmNetOutputDestroy(MovementNetOutput** noInOut){
	MovementNetOutput* no = SAFE_DEREF(noInOut);

	if(!no){
		return;
	}

	PERFINFO_AUTO_START_FUNC();
		
	mmOutputDataReset(&no->dataMutable);
	threadSafeMemoryPoolFree(&MovementNetOutputPool, no);
	*noInOut = NULL;

	PERFINFO_AUTO_STOP();
}

void mmNetOutputListAddTail(MovementNetOutputList* nol,
							MovementNetOutput* no)
{
	if(!nol->head){
		assert(!nol->tail);
		assert(!no->prev);
		
		nol->head = no;
	}else{
		assert(nol->tail);
		assert(!nol->tail->next);
		
		no->prev = nol->tail;
		nol->tail->next = no;
	}
	
	nol->tail = no;
}

void mmNetOutputListSetTail(MovementNetOutputList* nol,
							MovementNetOutput* no)
{
	if(!nol->tail){
		nol->head = no;
	}
	
	nol->tail = no;
}

S32 mmNetOutputListRemoveHead(	MovementNetOutputList* nol,
								MovementNetOutput** noOut)
{
	MovementNetOutput* no;
	
	if(!nol->head){
		return 0;
	}
	
	no = nol->head;
	
	assert(!no->prev);
	
	nol->head = no->next;
	
	if(!nol->head){
		assert(no == nol->tail);
		nol->tail = NULL;
	}else{
		nol->head->prev = NULL;
		no->next = NULL;
	}

	*noOut = no;
	
	return 1;
}

void mmNetOutputCreateAndAddTail(	MovementManager* mm,
									MovementThreadData* td,
									MovementNetOutput** noOut)
{
	MovementNetOutput* no;
	
	if(mmNetOutputListRemoveHead(&mm->fg.net.available.outputListMutable, &no)){
		MovementOutputData data = no->data;
		ZeroStructForce(no);
		no->dataMutable = data;
		if(eaiSize(&no->data.anim.values)){
			eaiClearFast(&no->dataMutable.anim.values);
		}
	}else{
		mmNetOutputCreate(&no);
	}

	// Put in FG list.

	mmNetOutputListAddTail(	&mm->fg.net.outputListMutable,
							no);

	*noOut = no;
}

static ThreadSafeMemoryPool MovementNetOutputEncodedPool;

S32 mmNetOutputEncodedCreate(MovementNetOutputEncoded** noeOut){
	if(!noeOut){
		return 0;
	}

	ATOMIC_INIT_BEGIN;
	{
		threadSafeMemoryPoolInit(&MovementNetOutputEncodedPool, 32, sizeof(**noeOut), "MovementNetOutputEncodedPool");
	}
	ATOMIC_INIT_END;

	*noeOut = threadSafeMemoryPoolAlloc(&MovementNetOutputEncodedPool);
	ZeroStruct(*noeOut);

	return 1;
}

void mmNetOutputEncodedDestroy(MovementNetOutputEncoded** noeInOut){
	MovementNetOutputEncoded* noe = SAFE_DEREF(noeInOut);

	if(!noe){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	threadSafeMemoryPoolFree(&MovementNetOutputEncodedPool, noe);
	
	*noeInOut = NULL;

	PERFINFO_AUTO_STOP();
}

static void mmInterpOutputsFG(	MovementManager* mm,
								const char* outputTypeName,
								const Vec3 posPrev,
								const Quat rotPrev,
								const Vec2 pyFacePrev,
								const U32 pcPrev,
								const Vec3 posNext,
								const Quat rotNext,
								const Vec2 pyFaceNext,
								const U32 pcNext,
								S32 notInterped,
								F32 outputInterpInverse,
								Vec3 posOut,
								Quat rotOut,
								Vec2 pyFaceOut)
{
	if(posOut){
		if(mm->fg.flags.posNeedsForcedSetAck){
			mmLog(mm, NULL, "Using current pos because waiting for forcedSetAck.");

			copyVec3(	mm->fg.pos,
						posOut);
		}else{
			PERFINFO_AUTO_START("pos", 1);

			if(sameVec3((S32*)posPrev, (S32*)posNext)){
				copyVec3(posPrev, posOut);
			}else{
				Vec3 posDiff;

				subVec3(posPrev,
						posNext,
						posDiff);

				scaleAddVec3(	posDiff,
								outputInterpInverse,
								posNext,
								posOut);
			}
				
			PERFINFO_AUTO_STOP();
		}
	}

	if(rotOut){
		if(mm->fg.flags.rotNeedsForcedSetAck){
			mmLog(mm, NULL, "Using current rot because waiting for forcedSetAck.");

			copyQuat(	mm->fg.rot,
						rotOut);
		}else{
			Vec3	yVecPrev;
			Vec3	yVecNext;
			Vec3	zVecPrev;
			Vec3	zVecPrevProj;
			Vec3	zVecNext;
			Vec3	zVecNextProj;
			Mat3	matNew;
			F32		radTotal;
			
			PERFINFO_AUTO_START("rot", 1);
			
			quatToMat3_1(rotPrev, yVecPrev);
			quatToMat3_2(rotPrev, zVecPrev);
			quatToMat3_1(rotNext, yVecNext);
			quatToMat3_2(rotNext, zVecNext);
			
			//assert(FINITEVEC3(yVecPrev));
			//assert(FINITEVEC3(yVecNext));

			if(sameVec3((S32*)yVecNext, (S32*)yVecPrev)){
				copyVec3((S32*)yVecNext, (S32*)matNew[1]);
				copyVec3((S32*)zVecPrev, (S32*)zVecPrevProj);
				copyVec3((S32*)zVecNext, (S32*)zVecNextProj);
			}else{
				PERFINFO_AUTO_START("yVec different", 1);
				
				rotateUnitVecTowardsUnitVec(yVecNext,
											yVecPrev,
											outputInterpInverse,
											matNew[1]);

				projectVecOntoPlane(zVecNext,
									matNew[1],
									zVecNextProj);

				normalVec3(zVecNextProj);
				
				projectVecOntoPlane(zVecPrev,
									matNew[1],
									zVecPrevProj);

				normalVec3(zVecPrevProj);

				PERFINFO_AUTO_STOP();
			}

			radTotal = acosf(CLAMPF32(dotVec3(zVecNextProj, zVecPrevProj), -1.f, 1.f));
			
			if(radTotal <= 0.001f){
				copyVec3(zVecNextProj, matNew[2]);
			}else{
				F32 radRemainder;

				if(radTotal <= HALFPI){
					radRemainder = radTotal;
				}else{
					radRemainder = fmod(radTotal, QUARTERPI);

					if(radRemainder >= 0.5f * QUARTERPI){
						radRemainder -= QUARTERPI;
					}
				}

				radRemainder *= outputInterpInverse;
				
				rotateUnitVecTowardsUnitVec(zVecNextProj,
											zVecPrevProj,
											radRemainder / radTotal,
											matNew[2]);
			}

			if(normalVec3(matNew[2]) <= 0.f){
				// This is a really lazy way to handle this condition.
				
				copyMat3(unitmat, matNew);
			}else{
				crossVec3(matNew[1], matNew[2], matNew[0]);
			}
			
			mat3ToQuat(matNew, rotOut);
			
			//assert(FINITEQUAT(rotOut));

			if(MMLOG_IS_ENABLED(mm)){
				const F32* pos = FIRST_IF_SET(posOut, mm->fg.pos);
				
				mmLogSegmentOffset(	mm,
									NULL,
									"fg.viewInterp",
									0xff00ff00,
									pos,
									yVecPrev);
				
				mmLogSegmentOffset(	mm,
									NULL,
									"fg.viewInterp",
									0xffff0000,
									pos,
									yVecNext);

				mmLogSegmentOffset2(mm,
									NULL,
									"fg.viewInterp",
									0xff0000ff,
									mm->fg.pos,
									yVecPrev,
									pos,
									matNew[1]);

				mmLogSegmentOffset2(mm,
									NULL,
									"fg.viewInterp",
									0xff0000ff,
									mm->fg.pos,
									matNew[1],
									pos,
									yVecNext);
			}
			
			PERFINFO_AUTO_STOP();
		}
	}
	
	if(pyFaceOut){
		if(mm->fg.flags.rotNeedsForcedSetAck){
			mmLog(mm, NULL, "Using current pyFace because waiting for forcedSetAck.");

			copyVec2(	mm->fg.pyFace,
						pyFaceOut);
		}
		else if(sameVec2((S32*)pyFaceNext, (S32*)pyFacePrev)){
			copyVec2(pyFaceNext, pyFaceOut);
		}else{
			PERFINFO_AUTO_START("pyFace", 1);

			interpPY(	outputInterpInverse,
						pyFaceNext,
						pyFacePrev,
						pyFaceOut);
							
			PERFINFO_AUTO_STOP();
		}
	}

	if(notInterped){
		zeroVec3(mm->fg.repredict.offset);
		mm->fg.repredict.secondsRemaining = 0.f;
	}

	if(MMLOG_IS_ENABLED(mm)){
		if(pcNext == pcPrev){
			mmLog(	mm,
					NULL,
					"[fg.view] WARNING: INTERPING A SINGLE STEP!!!");
		}

		mmLog(	mm,
				NULL,
				"[fg.view] Interping %s outputs %1.2f from (%d, %d) (dp %d, ppc %d, ppr %1.2f)\n"
				"Pos (%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
				"Rot(%1.2f, %1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x, %8.8x]",
				outputTypeName,
				outputInterpInverse,
				pcNext,
				pcPrev,
				mgState.fg.frame.cur.deltaProcesses,
				mgState.fg.frame.cur.pcPrev,
				mgState.fg.frame.cur.prevProcessRatio,
				vecParamsXYZ(FIRST_IF_SET(posOut, mm->fg.pos)),
				vecParamsXYZ((S32*)FIRST_IF_SET(posOut, mm->fg.pos)),
				quatParamsXYZW(FIRST_IF_SET(rotOut, mm->fg.rot)),
				quatParamsXYZW((S32*)FIRST_IF_SET(rotOut, mm->fg.rot)));
	}
}

void mmRemoveAnimBit(	MovementManager* mm,
						MovementAnimBitRegistry* abr,
						MovementAnimValues* from,
						U32 bitHandle,
						const DynSkeletonPreUpdateParams* params)
{
	MovementRegisteredAnimBit* bit;

	if(!mmRegisteredAnimBitGetByHandle(abr, &bit, bitHandle)){
		return;
	}

	EARRAY_INT_CONST_FOREACH_BEGIN(from->values, i, isize);
	{
		if(bitHandle != from->values[i]){
			continue;
		}
		
		eaiRemoveFast(&from->values, i);
		
		if(SAFE_MEMBER(params, targetBitField)){
			// Make sure a flash bit isn't turning off a non-flash bit.

			if(	!bit->flags.isFlashBit ||
				!bit->flags.hasNonFlashHandle ||
				eaiFind(&from->values, bit->bitHandleLocalNonFlash) < 0)
			{
				if(!bit->flags.foundDynBit){
					EnterRegisteredAnimBitCS();
					if(!bit->flags.foundDynBit){
						bit->dynBit = dynBitFromName(bit->bitName);
					}
					LeaveRegisteredAnimBitCS();
					bit->flags.foundDynBit = 1;
				}
					
				if(bit->dynBit){
					mmLog(	mm,
							NULL,
							"[fg.curanim] Clearing dyn bit: %s",
							bit->bitName);

					dynBitFieldBitClear(params->targetBitField, bit->dynBit);
				}
			}
		}
		
		break;
	}
	EARRAY_FOREACH_END;
}

void mmAddAnimBit(	MovementManager* mm,
					MovementAnimBitRegistry* abr,
					MovementAnimValues* to,
					U32 bitHandle,
					const DynSkeletonPreUpdateParams* params)
{
	MovementRegisteredAnimBit* bit;

	if(!mmRegisteredAnimBitGetByHandle(abr, &bit, bitHandle)){
		return;
	}

	if(	mmAnimValuesAddBit(to, bitHandle) &&
		SAFE_MEMBER(params, targetBitField))
	{
		if(FALSE_THEN_SET(bit->flags.foundDynBit)){
			bit->dynBit = dynBitFromName(bit->bitName);
		}
			
		if(bit->dynBit){
			mmLog(	mm,
					NULL,
					"[fg.curanim] Setting dyn bit: %s",
					bit->bitName);

			dynBitFieldBitSet(params->targetBitField, bit->dynBit);
		}
	}
}

static void mmLogAnimBitArrayCount(	MovementManager* mm,
									const U32* bitArray,
									const U32 bitCount,
									const S32 isNet,
									const U32 cpc,
									const U32 spc,
									const char* tags,
									const char* prefix)
{
	char buffer[1000];
	char pcString[100];

	buffer[0] = 0;
	pcString[0] = 0;

	FOR_BEGIN(i, (S32)bitCount);
	{
		const MovementRegisteredAnimBit* bit;

		if(mmRegisteredAnimBitGetByHandle(	isNet ?
												&mgState.fg.netReceiveMutable.animBitRegistry :
												&mgState.animBitRegistry,
											&bit,
											bitArray[i]))
		{
			strcatf(buffer,
					"(%u)%s%s, ",
					bit->index,
					bit->bitName,
					bit->flags.isFlashBit ? "(f)" : "");
		}
	}
	EARRAY_FOREACH_END;
	
	if(	cpc ||
		spc)
	{
		sprintf(pcString, "c%u/s%u: ", cpc, spc);
	}

	mmLog(	mm,
			NULL,
			"[%s] %s%s: %s",
			FIRST_IF_SET(tags, "fg.curanim"),
			pcString,
			prefix,
			buffer);
}

static void mmLogAnimBitArray(	MovementManager* mm,
								const U32* bitArray,
								const S32 isNet,
								const U32 cpc,
								const U32 spc,
								const char* tags,
								const char* prefix)
{
	mmLogAnimBitArrayCount(	mm,
							bitArray,
							eaiUSize(&bitArray),
							isNet,
							cpc,
							spc,
							tags,
							prefix);
}

static void mmAnimValuesLogType(MovementManager* mm,
								const MovementAnimValues* anim,
								const MovementAnimValueType mavType,
								const U32 cpc,
								const U32 spc,
								const char* tags,
								const char* prefix)
{
	U32* values = NULL;
	int iStackOverflow = 0;

	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		MovementAnimValueType mavTypeCur = MM_ANIM_VALUE_GET_TYPE(anim->values[i]);

		if(mavTypeCur == mavType){
			if(!values){
				eaiStackCreate(&values, MM_ANIM_VALUE_STACK_SIZE_MODEST);
			}

			if (eaiSize(&values) < eaiCapacity(&values))
				eaiPush(&values, MM_ANIM_VALUE_GET_INDEX(anim->values[i]));
			else
				iStackOverflow++;
		}

		if(mavTypeCur == MAVT_LASTANIM_ANIM){
			// Skip PC.
			i++;
		}
	}
	EARRAY_FOREACH_END;

	if (iStackOverflow > 0) {
		const char *pcTypeName = "UNKNOWN";
		switch (mavType) {
			xcase MAVT_STANCE_ON:				pcTypeName = "MAVT_STANCE_ON";
			xcase MAVT_STANCE_OFF:				pcTypeName = "MAVT_STANCE_OFF";
			xcase MAVT_ANIM_TO_START:			pcTypeName = "MAVT_ANIM_TO_START";
			xcase MAVT_FLAG:					pcTypeName = "MAVT_FLAG";
			xcase MAVT_DETAIL_ANIM_TO_START:	pcTypeName = "MAVT_DETAIL_ANIM_TO_START";
			xcase MAVT_DETAIL_FLAG:				pcTypeName = "MAVT_DETAIL_FLAG";
			xcase MAVT_LASTANIM_ANIM:			pcTypeName = "MAVT_LASTANIM_ANIM";
			xcase MAVT_LASTANIM_FLAG:			pcTypeName = "MAVT_LASTANIM_FLAG";
		}
		Errorf("Movement Manager: attempted value stack overflow with %i extra values of type %s in %s", iStackOverflow, pcTypeName, __FUNCTION__);
	}

	if(values){
		mmLogAnimBitArray(mm, values, 0, cpc, spc, tags, prefix);
		eaiDestroy(&values);
	}
}

static void mmAnimValuesLogLastAnim(MovementManager* mm,
									const MovementAnimValues* anim,
									S32 isNet,
									const char* tag,
									const char* name)
{
	MovementLastAnim lastAnim = {0};
	int iStackOverflow = 0;

	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				assert(!lastAnim.anim);
				assert(i + 1 < isize);
				lastAnim.anim = MM_ANIM_VALUE_GET_INDEX(anim->values[i]);
				assert(lastAnim.anim);
				lastAnim.pc = anim->values[++i];
			}
			xcase MAVT_LASTANIM_FLAG:{
				assert(lastAnim.anim);

				if(!lastAnim.flags){
					eaiStackCreate(&lastAnim.flags, MM_ANIM_VALUE_STACK_SIZE_SMALL);
				}

				if (eaiSize(&lastAnim.flags) < eaiCapacity(&lastAnim.flags))
					eaiPush(&lastAnim.flags, MM_ANIM_VALUE_GET_INDEX(anim->values[i]));
				else
					iStackOverflow++;
			}
		}
	}
	EARRAY_FOREACH_END;

	if (iStackOverflow)
		Errorf("Movement Manager: attempted flag stack overflow with %i extra values in %s", iStackOverflow, __FUNCTION__);

	wrapped_mmLastAnimLog(mm, &lastAnim, isNet, tag, name);

	if(lastAnim.flags){
		eaiDestroy(&lastAnim.flags);
	}
}

static void mmAnimValuesLogAll(	MovementManager* mm,
								const MovementAnimValues* anim,
								S32 isNet,
								const char* tags)
{
	mmAnimValuesLogType(mm,
						anim,
						MAVT_STANCE_OFF,
						0,
						0,
						tags,
						"Stances Off");

	mmAnimValuesLogType(mm,
						anim,
						MAVT_STANCE_ON,
						0,
						0,
						tags,
						"Stances On");

	mmAnimValuesLogType(mm,
						anim,
						MAVT_ANIM_TO_START,
						0,
						0,
						tags,
						"AnimToStart");

	mmAnimValuesLogType(mm,
						anim,
						MAVT_FLAG,
						0,
						0,
						tags,
						"Flags");

	mmAnimValuesLogLastAnim(mm, anim, isNet, tags, "LastAnimStarted");
}

static void mmLogAnimBitCombo(	MovementManager* mm,
								const MovementRegisteredAnimBitCombo* combo,
								const S32 isNet,
								const U32 cpc,
								const U32 spc,
								const char* prefix)
{
	char buffer[1000];
	
	if(!combo){
		return;
	}

	buffer[0] = 0;

	EARRAY_INT_CONST_FOREACH_BEGIN(combo->bits, i, size);
	{
		const MovementRegisteredAnimBit* bit;

		if(mmRegisteredAnimBitGetByHandle(	isNet ?
												&mgState.fg.netReceiveMutable.animBitRegistry :
												&mgState.animBitRegistry,
											&bit,
											combo->bits[i]))
		{
			strcatf(buffer,
					"%s%s, ",
					bit->bitName,
					bit->flags.isFlashBit ? "(f)" : "");
		}
	}
	EARRAY_FOREACH_END;

	mmLog(	mm,
			NULL,
			"[fg.curanim] c%d/s%d: %s: %s",
			cpc,
			spc,
			prefix,
			buffer);
}

static void mmSkeletonPreUpdateAddOutputBits(	MovementManager* mm,
												MovementOutput* o,
												S32* hasFlashBitsToViewOut,
												const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView* v = mm->fg.view;

	EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, j, jsize);
	{
		U32									bitHandle;
		const MovementRegisteredAnimBit*	bit;

		bitHandle = o->data.anim.values[j];

		if(!mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle)){
			continue;
		}

		if(bit->flags.isFlashBit){
			*hasFlashBitsToViewOut = 1;

			if(o->flags.hasFlashBitsToView){
				mmAddAnimBit(	mm,
								&mgState.animBitRegistry,
								&v->animValuesMutable,
								bitHandle,
								params);
			}else{
				mmRemoveAnimBit(mm,
								&mgState.animBitRegistry,
								&v->animValuesMutable,
								bitHandle,
								params);
			}
		}else{
			mmAddAnimBit(	mm,
							&mgState.animBitRegistry,
							&v->animValuesMutable,
							bitHandle,
							params);
		}
	}
	EARRAY_FOREACH_END;
}

S32 mmGetLocalAnimBitFromHandle(const MovementRegisteredAnimBit** bitOut,
								U32 handle,
								S32 isServerHandle)
{
	if(isServerHandle){
		return mmRegisteredAnimBitTranslateHandle(	&mgState.fg.netReceiveMutable.animBitRegistry,
													handle,
													&mgState.animBitRegistry,
													bitOut);
	}else{
		return mmRegisteredAnimBitGetByHandle(	&mgState.animBitRegistry,
												bitOut,
												handle);
	}
}

void mmLastAnimReset(MovementLastAnim* lastAnim){
	eaiDestroy(&lastAnim->flags);
	ZeroStruct(lastAnim);
}

void mmLastAnimCopy(MovementLastAnim* d,
					const MovementLastAnim* s)
{
	d->pc = s->pc;
	d->anim = s->anim;
	eaiCopy(&d->flags, &s->flags);
}

void mmLastAnimCopyLimitFlags(	MovementLastAnim* d,
								const MovementLastAnim* s,
								const char *pcFuncName)
{
	d->pc = s->pc;
	d->anim = s->anim;
	mmCopyAnimValueToSizedStack(&d->flags,
								s->flags,
								pcFuncName);
}

void mmLastAnimCopyToValues(MovementAnimValues* anim,
							const MovementLastAnim* s)
{
	if(!s->anim){
		return;
	}

	eaiPush(&anim->values,
			MM_ANIM_VALUE(s->anim, MAVT_LASTANIM_ANIM));

	eaiPush(&anim->values, s->pc);

	EARRAY_INT_CONST_FOREACH_BEGIN(s->flags, i, isize);
	{
		eaiPush(&anim->values, MM_ANIM_VALUE(s->flags[i], MAVT_LASTANIM_FLAG));
	}
	EARRAY_FOREACH_END;
}

void mmLastAnimCopyFromValues(	MovementLastAnim* s,
								const MovementAnimValues* anim)
{
	S32 found = 0;

	if(s->flags){
		eaiClearFast(&s->flags);
	}

	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				found = 1;
				assert(i + 1 < isize);
				s->anim = MM_ANIM_VALUE_GET_INDEX(anim->values[i]);
				s->pc = anim->values[++i];

				while(++i < isize){
					if(MM_ANIM_VALUE_GET_TYPE(anim->values[i]) == MAVT_LASTANIM_FLAG){
						eaiPush(&s->flags, MM_ANIM_VALUE_GET_INDEX(anim->values[i]));
					}
				}

				break;
			}

			xcase MAVT_LASTANIM_FLAG:{
				assert(0);
			}
		}
	}
	EARRAY_FOREACH_END;

	if(!found){
		s->anim = 0;
		s->pc = 0;
	}
}

void mmLastAnimCopyFromValuesLimitFlags(MovementLastAnim* s,
										const MovementAnimValues* anim,
										const char *pcFuncName)
{
	S32 found = 0;
	int iStackOverflow = 0;

	if(s->flags){
		eaiClearFast(&s->flags);
	}

	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				found = 1;
				assert(i + 1 < isize);
				s->anim = MM_ANIM_VALUE_GET_INDEX(anim->values[i]);
				s->pc = anim->values[++i];

				while(++i < isize){
					if(MM_ANIM_VALUE_GET_TYPE(anim->values[i]) == MAVT_LASTANIM_FLAG){
						if (eaiSize(&s->flags) < eaiCapacity(&s->flags))
							eaiPush(&s->flags, MM_ANIM_VALUE_GET_INDEX(anim->values[i]));
						else
							iStackOverflow++;
					}
				}

				break;
			}

			xcase MAVT_LASTANIM_FLAG:{
				assert(0);
			}
		}
	}
	EARRAY_FOREACH_END;

	if (iStackOverflow)
		Errorf("Movement Manager: attempted value stack overflow with %i extra values in %s", iStackOverflow, pcFuncName);

	if(!found){
		s->anim = 0;
		s->pc = 0;
	}
}

void wrapped_mmLastAnimLog(	MovementManager* mm,
							const MovementLastAnim* lastAnim,
							S32 isNet,
							const char* tag,
							const char* name)
{
	U32 spcOffset;

	if(	!mm ||
		!MMLOG_IS_ENABLED(mm) ||
		!SAFE_MEMBER(lastAnim, anim))
	{
		return;
	}

	if(isNet){
		spcOffset = 0;
	}
	else if(mmIsForegroundThread()){
		spcOffset = mgState.fg.netReceive.cur.offset.clientToServerSync;
	}else{
		spcOffset = mgState.bg.netReceive.cur.offset.clientToServer;
	}

	mmLogAnimBitArrayCount(	mm,
							&lastAnim->anim,
							1,
							0,
							isNet ? 0 : lastAnim->pc,
							lastAnim->pc + spcOffset,
							tag,
							name);

	if(eaiUSize(&lastAnim->flags)){
		char flagsName[100];
		
		strcpy(flagsName, name);
		strcat(flagsName, "Flags");
		
		mmLogAnimBitArray(	mm,
							lastAnim->flags,
							0,
							0,
							0,
							tag,
							flagsName);
	}
}

static void mmAnimViewLogLastAnim(	MovementManager* mm,
									const char* tag)
{
	const MovementManagerFGView* v = mm->fg.view;
	
	if(v->flags.lastAnimIsLocal){
		mmLastAnimLogLocal(mm, &v->lastAnim, tag, "View.LastAnim.Local");
	}else{
		mmLastAnimLogNet(mm, &v->lastAnim, tag, "View.LastAnim.Net");
	}
}

static void mmApplyStanceToView(MovementManager* mm,
								const U32 stance,
								const S32 on,
								U32**const stancesInOut,
								const DynSkeletonPreUpdateParams* params,
								const U32*const stancesOther)
{
	if(on){
		if(eaiFind(stancesInOut, stance) >= 0){
			mmHandleBadAnimData(mm);
		}
		eaiPush(stancesInOut, stance);

		if(params){
			const MovementRegisteredAnimBit* bit;
			
			mmGetLocalAnimBitFromHandle(&bit, stance, 0);

			if(	params &&
				eaiFind(&stancesOther, stance) < 0)
			{
				params->func.setStance(params->skeleton, bit->bitName);
			}
		}
	}else{
		if(eaiFindAndRemove(stancesInOut, stance) < 0){
			mmHandleBadAnimData(mm);
		}

		if(params){
			const MovementRegisteredAnimBit* bit;
			
			mmGetLocalAnimBitFromHandle(&bit, stance, 0);

			if(	params &&
				eaiFind(&stancesOther, stance) < 0)
			{
				params->func.clearStance(params->skeleton, bit->bitName);
			}
		}
	}
}

static void mmApplyStancesToView(	MovementManager* mm,
									const U32*const stancesOff,
									const U32*const stancesOn,
									U32**const stancesInOut,
									const DynSkeletonPreUpdateParams* params,
									const U32*const stancesOther)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(stancesOff, i, isize);
	{
		mmApplyStanceToView(mm, stancesOff[i], 0, stancesInOut, params, stancesOther);
	}
	EARRAY_FOREACH_END;

	EARRAY_INT_CONST_FOREACH_BEGIN(stancesOn, i, isize);
	{
		mmApplyStanceToView(mm, stancesOn[i], 1, stancesInOut, params, stancesOther);
	}
	EARRAY_FOREACH_END;
}

static void mmAnimValuesApplyStancesToView(	MovementManager* mm,
											const MovementAnimValues*const anim,
											S32 invert,
											U32**const stancesInOut,
											const DynSkeletonPreUpdateParams* params,
											const U32*const stancesOther)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				// Skip PC.
				i++;
			}
			xcase MAVT_STANCE_ON:{
				mmApplyStanceToView(mm,
									MM_ANIM_VALUE_GET_INDEX(anim->values[i]),
									!invert,
									stancesInOut,
									params,
									stancesOther);
			}
			xcase MAVT_STANCE_OFF:{
				mmApplyStanceToView(mm,
									MM_ANIM_VALUE_GET_INDEX(anim->values[i]),
									invert,
									stancesInOut,
									params,
									stancesOther);
			}
		}
	}
	EARRAY_FOREACH_END;
}

void mmApplyStanceDiff(	const U32*const stancesOff,
						const U32*const stancesOn,
						U32**const stancesInOut)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(stancesOff, i, isize);
	{
		if(eaiFindAndRemove(stancesInOut, stancesOff[i]) < 0){
			assert(0);
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_INT_CONST_FOREACH_BEGIN(stancesOn, i, isize);
	{
		const U32 stance = stancesOn[i];
		assert(eaiFind(stancesInOut, stance) < 0);
		eaiPush(stancesInOut, stance);
	}
	EARRAY_FOREACH_END;
}

void mmAnimValuesApplyStanceDiff(	MovementManager* mm,
									const MovementAnimValues*const anim,
									S32 invert,
									U32**const stancesInOut,
									const char *pcCallingFunc,
									U32 reportStackErrorAndPreventCrash)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		MovementAnimValueType mavType = MM_ANIM_VALUE_GET_TYPE(anim->values[i]);

		switch(mavType){
			xcase MAVT_LASTANIM_ANIM:{
				// Skip PC.
				i++;
			}
			xcase MAVT_STANCE_ON:
			acase MAVT_STANCE_OFF:{
				const U32 stance = MM_ANIM_VALUE_GET_INDEX(anim->values[i]);

				if(!invert == (mavType == MAVT_STANCE_ON)){
					if(eaiFind(stancesInOut, stance) >= 0){
						mmHandleBadAnimData(mm);
					}
					
					if (reportStackErrorAndPreventCrash &&
						eaiSize(stancesInOut) == eaiCapacity(stancesInOut)) {
						Errorf("Movement Manager: attempted stack overflow with %i extra values in %s", 1, pcCallingFunc);
					} else {
						eaiPush(stancesInOut, stance);
					}
				}
				else if(eaiFindAndRemove(stancesInOut, stance) < 0){
					if (!reportStackErrorAndPreventCrash)
						mmHandleBadAnimData(mm);
				}
			}
		}
	}
	EARRAY_FOREACH_END;
}

static void mmAnimViewSetOverrideTime(	MovementManager *mm,
										const DynSkeletonPreUpdateParams *params,
										F32 fTime,
										U32 uiApply)
{
	if (!params) {
		return;
	}

	mmLog(	mm,
			NULL,
			"[fg.curanim] Setting override time %f%s",
			fTime,
			uiApply ? " Applied" : "");

	params->func.setOverrideTime(params->skeleton, fTime, uiApply);
}

static void mmAnimViewStartAnim(MovementManager* mm,
								const DynSkeletonPreUpdateParams* params,
								const U32 bitHandle)
{
	if(!params){
		return;
	}

	if(bitHandle == mgState.animBitHandle.animOwnershipReleased){
		mmLog(	mm,
				NULL,
				"[fg.curanim] Starting default anim graph.");
#if MM_DEBUG_PRINTANIMWORDS
			printfColor(COLOR_RED, "%s ", __FUNCTION__);
			printfColor(COLOR_RED|COLOR_BRIGHT, "NULL\n");
#endif
		params->func.startGraph(params->skeleton, NULL, 0);
	}else{
		const MovementRegisteredAnimBit* bit;

		if(mmGetLocalAnimBitFromHandle(	&bit,
										MM_ANIM_HANDLE_WITHOUT_ID(bitHandle),
										0))
		{
			mmLog(	mm,
					NULL,
					"[fg.curanim] Starting anim graph: %s",
					bit->bitName);
#if MM_DEBUG_PRINTANIMWORDS
			printfColor(COLOR_RED, "%s ", __FUNCTION__);
			printfColor(COLOR_RED|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, MM_ANIM_HANDLE_GET_ID(bitHandle));
#endif
			params->func.startGraph(params->skeleton,
									bit->bitName,
									MM_ANIM_HANDLE_GET_ID(bitHandle));
		}else{
			mmLog(	mm,
					NULL,
					"[fg.curanim] Failed to start anim graph: %u",
					bitHandle);
		}
	}
}

static void mmAnimViewStartDetailAnim(	MovementManager* mm,
										const DynSkeletonPreUpdateParams* params,
										const U32 bitHandle)
{
	if(!params){
		return;
	}

	if(bitHandle != mgState.animBitHandle.animOwnershipReleased){
		const MovementRegisteredAnimBit* bit;

		if(mmGetLocalAnimBitFromHandle(	&bit,
										MM_ANIM_HANDLE_WITHOUT_ID(bitHandle),
										0))
		{
			mmLog(	mm,
					NULL,
					"[fg.curanim] Starting detail anim graph: %s",
					bit->bitName);
#if MM_DEBUG_PRINTANIMWORDS
			printfColor(COLOR_RED, "%s ", __FUNCTION__);
			printfColor(COLOR_RED|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, MM_ANIM_HANDLE_GET_ID(bitHandle));
#endif
			params->func.startDetailGraph(	params->skeleton,
											bit->bitName,
											MM_ANIM_HANDLE_GET_ID(bitHandle));
		}
	}
}

static void mmAnimViewPlayFlag(	MovementManager* mm,
								const DynSkeletonPreUpdateParams* params,
								const U32 bitHandle)
{
	if(params){
		const MovementRegisteredAnimBit* bit;

		if (mmGetLocalAnimBitFromHandle(&bit,
										MM_ANIM_HANDLE_WITHOUT_ID(bitHandle),
										0))
		{
			mmLog(	mm,
					NULL,
					"[fg.curanim] Playing flag: %s",
					bit->bitName);
#if MM_DEBUG_PRINTANIMWORDS
			printfColor(COLOR_RED, "%s ", __FUNCTION__);
			printfColor(COLOR_RED|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, MM_ANIM_HANDLE_GET_ID(bitHandle));
#endif
			params->func.playFlag(	params->skeleton,
									bit->bitName,
									MM_ANIM_HANDLE_GET_ID(bitHandle));
		} else {
			mmLog(	mm,
					NULL,
					"[fg.curanim] Failed to play flag: %u",
					bitHandle);
		}
	}
}

static void mmAnimViewPlayDetailFlag(	MovementManager *mm,
										const DynSkeletonPreUpdateParams *params,
										const U32 bitHandle)
{
	if (params) {
		const MovementRegisteredAnimBit *bit;

		if (mmGetLocalAnimBitFromHandle(&bit,
										MM_ANIM_HANDLE_WITHOUT_ID(bitHandle),
										0))
		{
			mmLog(	mm,
					NULL,
					"[fg.curanim] Playing detail flag: %s",
					bit->bitName);
#if MM_DEBUG_PRINTANIMWORDS
			printfColor(COLOR_RED, "%s ", __FUNCTION__);
			printfColor(COLOR_RED|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, MM_ANIM_HANDLE_GET_ID(bitHandle));
#endif
			params->func.playDetailFlag(params->skeleton,
										bit->bitName,
										MM_ANIM_HANDLE_GET_ID(bitHandle));
		}
	}
}

#define subSPC_CPC(spc, cpc) subS32((spc), (cpc) + mgState.fg.netReceive.cur.offset.clientToServerSync)

static void mmAnimViewLocalPlayLastAnim(MovementManager* mm,
										const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView*	v = mm->fg.view;
	U32 flagCount, localFlagCount;
	bool bLocalPCMismatch;
	bool bOverwriteNet;
	bool bAnimMismatch;
	bool bFlagCountRestart;
	bool bFlagContentRestart;
	
	if(!params){
		return;
	}

	// Check if the server anim state is newer.

	if(	!v->flags.lastAnimIsLocal &&
		subSPC_CPC(mgState.fg.netReceive.cur.pc.server, v->local.lastAnim.pc) >= 0)
	{
		return;
	}

	flagCount = eaiUSize(&v->lastAnim.flags);
	localFlagCount = eaiUSize(&v->local.lastAnim.flags);

	bLocalPCMismatch = v->flags.lastAnimIsLocal &&
							(v->lastAnim.isLocal && v->lastAnim.pc != v->local.lastAnim.pc ||
							!v->lastAnim.isLocal && v->lastAnim.pc != v->local.lastAnim.pc + mgState.fg.netReceive.cur.offset.clientToServerSync);

	bOverwriteNet = !v->flags.lastAnimIsLocal &&
						(v->lastAnim.isLocal && v->lastAnim.pc == v->local.lastAnim.pc ||
						!v->lastAnim.isLocal && v->lastAnim.pc == v->local.lastAnim.pc + mgState.fg.netReceive.cur.offset.clientToServerSync);

	bAnimMismatch = v->local.lastAnim.anim != v->lastAnim.anim;

	bFlagCountRestart = flagCount > localFlagCount;

	bFlagContentRestart = CompareStructs(v->lastAnim.flags, v->local.lastAnim.flags, MIN(flagCount,localFlagCount));

	if(bLocalPCMismatch || bOverwriteNet || bAnimMismatch || bFlagContentRestart) // || bFlagCountRestart)
	{
		U32 uiOverrideTimeApply = 0;
		F32 fOverrideTime = 0.f;

		// Start fresh.
		
		/*
		printf("Reset to Local: %s%s%s%s%s\n",
			bLocalPCMismatch	? "LocalPCMismatch, "	:"",
			bOverwriteNet		? "OverwriteNet, "		:"",
			bAnimMismatch		? "AnimMismatch, "		:"",
			bFlagCountRestart	? "FlagCountRestart, "	:"",
			bFlagContentRestart	? "FlagContentRestart, ":"");

		if (bFlagCountRestart) {
			printf("\tlastAnim: %u\n", v->lastAnim.anim);
			printf("\tlastAnimFlags: ");
			EARRAY_INT_CONST_FOREACH_BEGIN(v->lastAnim.flags, i, iSize);
				printf("%u ",v->lastAnim.flags[i]);
			FOR_EACH_END;
			printf("\n");

			printf("\tlocalAnim: %u\n", v->local.lastAnim.anim);
			printf("\tlocalAnimFlags: ");
			EARRAY_INT_CONST_FOREACH_BEGIN(v->local.lastAnim.flags, i, iSize);
				printf("%u ",v->local.lastAnim.flags[i]);
			FOR_EACH_END;
			printf("\n");
		}
		*/

		if(MMLOG_IS_ENABLED(mm)){
			mmLog(	mm,
					NULL,
					"[fg.curanim] lastAnim mismatch, starting local anim.");
			
			mmLastAnimLogLocal(mm, &v->local.lastAnim, NULL, "ViewLocal.LastAnim");
			mmAnimViewLogLastAnim(mm, NULL);
		}

		if (!bLocalPCMismatch && !bOverwriteNet && !bAnimMismatch && (bFlagCountRestart || bFlagContentRestart))
		{
			uiOverrideTimeApply = 1;
		}
		else if(bLocalPCMismatch && !bOverwriteNet && !bAnimMismatch && !bFlagContentRestart && v->lastAnim.anim > 1)
		{
			if (v->lastAnim.isLocal) {
				if (v->local.lastAnim.pc > v->lastAnim.pc) {
					fOverrideTime = -1 * MM_SECONDS_PER_PROCESS_COUNT * (v->local.lastAnim.pc - v->lastAnim.pc);
				} else {
					fOverrideTime = MM_SECONDS_PER_PROCESS_COUNT * (v->lastAnim.pc - v->local.lastAnim.pc);
				}
			} else {
				if (v->local.lastAnim.pc + mgState.fg.netReceive.cur.offset.clientToServerSync > v->lastAnim.pc) {
					fOverrideTime = -1 * MM_SECONDS_PER_PROCESS_COUNT * (v->local.lastAnim.pc + mgState.fg.netReceive.cur.offset.clientToServerSync - v->lastAnim.pc);
				} else {
					fOverrideTime = MM_SECONDS_PER_PROCESS_COUNT * (v->lastAnim.pc - v->local.lastAnim.pc - mgState.fg.netReceive.cur.offset.clientToServerSync);
				}
			}
			uiOverrideTimeApply = 1;
		}

		if(	v->flags.localAnimIsPredicted ||
			v->local.lastAnim.anim != mgState.animBitHandle.animOwnershipReleased)
		{
			v->flagsMutable.lastAnimIsLocal = 1;
			mmAnimViewSetOverrideTime(mm, params, fOverrideTime, uiOverrideTimeApply);
			mmAnimViewStartAnim(mm, params, v->local.lastAnim.anim);
			v->lastAnimMutable.pc = v->local.lastAnim.pc;
			v->lastAnimMutable.anim = v->local.lastAnim.anim;
			v->lastAnimMutable.isLocal = 1;
			eaiClearFast(&v->lastAnimMutable.flags);
			flagCount = 0;
		}else{
			v->flagsMutable.lastAnimIsLocal = 0;

			mmLog(	mm,
					NULL,
					"[fg.curanim] Not playing local lastAnim, it's a release while not predicting.");
		}
	}

	// Play the rest of the flags.
	
	if(v->flags.lastAnimIsLocal){
		if(MMLOG_IS_ENABLED(mm)){
			mmLog(	mm,
					NULL,
					"[fg.curanim] Before playing remaining local lastAnim flags.");

			mmLastAnimLogLocal(mm, &v->local.lastAnim, NULL, "ViewLocal.LastAnim");
			mmAnimViewLogLastAnim(mm, NULL);
		}

		EARRAY_INT_CONST_FOREACH_BEGIN_FROM(v->local.lastAnim.flags, i, isize, flagCount);
		{
			U32 flag = v->local.lastAnim.flags[i];
			mmAnimViewPlayFlag(mm, params, flag);
			eaiPush(&v->lastAnimMutable.flags, flag);
		}
		EARRAY_FOREACH_END;

		if(MMLOG_IS_ENABLED(mm)){
			mmLog(	mm,
					NULL,
					"[fg.curanim] After playing remaining local lastAnim flags.");

			mmLastAnimLogLocal(mm, &v->local.lastAnim, NULL, "ViewLocal.LastAnim");
			mmAnimViewLogLastAnim(mm, NULL);
		}
	}
}

static void mmAnimViewLocalFindReplayStart(	MovementManager* mm,
											MovementThreadData* td,
											const DynSkeletonPreUpdateParams* params,
											const MovementOutput** oFirstReplayOut)
{
	MovementManagerFGView*	v = mm->fg.view;
	const MovementOutput*	oFirstReplay = td->toFG.outputList.head;
	MovementOutput*			o;

	for(o = td->toFG.outputList.tail;
		o;
		o = (o == td->toFG.outputList.head ? NULL : o->prev))
	{
		if(TRUE_THEN_RESET(o->flagsMutable.needsAnimReplayLocal)){
			oFirstReplay = o;
		}
	}
		
	if(!oFirstReplay->flags.animViewedLocal){
		oFirstReplay = NULL;
	}else{
		// Intialize a stance-undo.

		int iOffStackOverflow = 0;
		int iOnStackOverflow = 0;

		U32* stancesOff = NULL;
		U32* stancesOn = NULL;

		eaiStackCreate(&stancesOff, MM_ANIM_VALUE_STACK_SIZE_MEDIUM);
		eaiStackCreate(&stancesOn, MM_ANIM_VALUE_STACK_SIZE_MEDIUM);

		mmLog(	mm,
				NULL,
				"[fg.curanim] Anim replay starting at c%u/s%u.",
				oFirstReplay->pc.client,
				oFirstReplay->pc.server);

		EARRAY_INT_CONST_FOREACH_BEGIN(v->local.stanceBits, i, isize);
		{
			U32 bit = v->local.stanceBits[i];

			if(eaiFind(&td->toFG.stanceBits, bit) < 0){
				if (eaiSize(&stancesOff) < eaiCapacity(&stancesOff))
					eaiPush(&stancesOff, bit);
				else
					iOffStackOverflow++;
			}
		}
		EARRAY_FOREACH_END;

		EARRAY_INT_CONST_FOREACH_BEGIN(td->toFG.stanceBits, i, isize);
		{
			U32 bit = td->toFG.stanceBits[i];

			if(eaiFind(&v->local.stanceBits, bit) < 0){
				if (eaiSize(&stancesOn) < eaiCapacity(&stancesOn))
					eaiPush(&stancesOn, bit);
				else
					iOnStackOverflow++;
			}
		}
		EARRAY_FOREACH_END;

		if (iOffStackOverflow)
			Errorf("Movement Manager: attempted StanceOff stack overflow with %i extra values in %s", iOffStackOverflow, __FUNCTION__);

		if (iOnStackOverflow)
			Errorf("Movement Manager: attempted StanceOn stack overflow with %i extra values in %s", iOnStackOverflow, __FUNCTION__);

		mmApplyStancesToView(	mm,
								stancesOff,
								stancesOn,
								&v->local.stanceBitsMutable,
								params,
								v->netUsed.stanceBits);

		eaiDestroy(&stancesOff);
		eaiDestroy(&stancesOn);
	}

	*oFirstReplayOut = oFirstReplay;
}

void mmAnimValuesRemoveFromFlags(	const MovementAnimValues* anim,
									U32** flagsInOut)
{
	U32* flagsFromValues = NULL;
	int iStackOverflow = 0;

	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				// Skip PC.
				i++;
			}
			xcase MAVT_FLAG:{
				if(!flagsFromValues){
					eaiStackCreate(&flagsFromValues, MM_ANIM_VALUE_STACK_SIZE_SMALL);
				}

				if (eaiSize(&flagsFromValues) < eaiCapacity(&flagsFromValues))
					eaiPush(&flagsFromValues, MM_ANIM_VALUE_GET_INDEX(anim->values[i]));
				else
					iStackOverflow++;
			}
		}
	}
	EARRAY_FOREACH_END;

	if (iStackOverflow)
		Errorf("Movement Manager: attempted Flag stack overflow with %i extra values in %s", iStackOverflow, __FUNCTION__);

	if(flagsFromValues)
	{
		while (iStackOverflow > 0) {
			const U32 uncheckedFlag = eaiPop(flagsInOut);
			assert(uncheckedFlag);
			iStackOverflow--;
		}

		EARRAY_INT_FOREACH_REVERSE_BEGIN(flagsFromValues, i);
		{
			const U32 flag = eaiPop(flagsInOut);
			assert(flag);
			assert(flag == flagsFromValues[i]);
		}
		EARRAY_FOREACH_END;

		eaiDestroy(&flagsFromValues);
	}
}

static void mmAnimViewLocalUndoNewOutputs(	MovementManager* mm,
											MovementThreadData* td,
											const DynSkeletonPreUpdateParams* params,
											const MovementOutput* oFirstReplay,
											const U32 cpcCeiling,
											MovementOutput** oOut)
{
	MovementManagerFGView*	v = mm->fg.view;
	MovementOutput*			o;

	mmLastAnimCopy(	&v->local.lastAnimMutable,
					&td->toFG.lastAnim);

	mmLastAnimLogLocal(mm, &v->local.lastAnim, NULL, "ViewLocal.LastAnim.BeforeUndo");

	// Find the first unviewed output, and undo changes in reverse until the first played output.

	for(o = td->toFG.outputList.tail;
		o;
		o = (o == td->toFG.outputList.head ? NULL : o->prev))
	{
		// Skip everything that's newer than the view time.
	
		if(	o != td->toFG.outputList.head &&
			subS32(o->pc.client, cpcCeiling) > 0)
		{
			continue;
		}
		
		// Update the stance-undo.
		
		if(oFirstReplay){
			mmAnimValuesApplyStancesToView(	mm,
											&o->data.anim,
											1,
											&v->local.stanceBitsMutable,
											params,
											v->netUsed.stanceBits);
		}

		// Update the stance-undo.
		
		if(o->flags.animViewedLocal){
			if(!oFirstReplay){
				// Start playing from the next node.

				if(o == td->toFG.outputList.tail){
					o = NULL;
				}else{
					o = o->next;
				}
				break;
			}
		}else{
			// Remove o's flags from local lastAnim.

			mmAnimValuesRemoveFromFlags(&o->data.anim,
										&v->local.lastAnimMutable.flags);

			if(MMLOG_IS_ENABLED(mm)){
				mmLog(	mm,
						NULL,
						"[fg.curanim] Local: Removed flags from c%u/s%u.",
						o->pc.client,
						o->pc.server);

				mmLastAnimLogLocal(	mm,
									&v->local.lastAnim,
									NULL,
									"ViewLocal.LastAnim.AfterRemoveFlags");
			}

			EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
			{
				switch(MM_ANIM_VALUE_GET_TYPE(o->data.anim.values[i])){
					xcase MAVT_LASTANIM_ANIM:{
						// Skip PC.
						i++;
					}
					xcase MAVT_ANIM_TO_START:{
						// Restore the lastAnim state from before this anim.

						assert(!eaiUSize(&v->local.lastAnim.flags));

						mmLastAnimCopyFromValues(	&v->local.lastAnimMutable,
													&o->data.anim);

						if(MMLOG_IS_ENABLED(mm)){
							mmLog(	mm,
									NULL,
									"[fg.curanim] Local: Copying previous lastAnim from c%u/s%u.",
									o->pc.client,
									o->pc.server);

							mmLastAnimLogLocal(	mm,
												&v->local.lastAnim,
												NULL,
												"ViewLocal.LastAnim.AfterCopyPrevious");
						}
					}
				}
			}
			EARRAY_FOREACH_END;
		}

		if(	o == oFirstReplay ||
			o == td->toFG.outputList.head)
		{
			break;
		}
	}

	mmLastAnimLogLocal(mm, &v->local.lastAnim, NULL, "ViewLocal.LastAnim.AfterUndo");

	// Make sure the spcPerFlag matches (doesn't have to be perfect, it's just a cleanup).

	while(eaiSize(&v->local.spcPerFlag) < eaiSize(&v->local.lastAnim.flags)){
		eaiPush(&v->local.spcPerFlagMutable, cpcCeiling);
	}

	while(eaiSize(&v->local.spcPerFlag) > eaiSize(&v->local.lastAnim.flags)){
		eaiPop(&v->local.spcPerFlagMutable);
	}

	*oOut = o;
}

static void mmAnimViewLocalPlayOutputs(	MovementManager* mm,
										MovementThreadData* td,
										const DynSkeletonPreUpdateParams* params,
										MovementOutput* o,
										const U32 cpcCeiling)
{
	MovementManagerFGView*	v = mm->fg.view;
	S32						checkedLastAnim = 0;

	for(;
		o && subS32(o->pc.client, cpcCeiling) <= 0;
		o = (o == td->toFG.outputList.tail ? NULL : o->next))
	{
		S32 doApplyAnim = FALSE_THEN_SET(o->flagsMutable.animViewedLocal);
		S32 doApplyAnimDirectly = doApplyAnim && !FALSE_THEN_SET(checkedLastAnim);
		
		mmLog(	mm,
				NULL,
				"[fg.curanim] Playing local output c%u/s%u%s%s.",
				o->pc.client,
				o->pc.server,
				doApplyAnim ? ", doApplyAnim" : "",
				doApplyAnimDirectly ? ", doApplyAnimDirectly" : "");

		mmAnimValuesApplyStancesToView(	mm,
										&o->data.anim,
										0,
										&v->local.stanceBitsMutable,
										params,
										v->netUsed.stanceBits);

		if(!doApplyAnim){
			continue;
		}

		EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
		{
			switch(MM_ANIM_VALUE_GET_TYPE(o->data.anim.values[i])){
				xcase MAVT_LASTANIM_ANIM:{
					// Skip PC.
					i++;
				}
				xcase MAVT_ANIM_TO_START:{
					const U32 animToStart = MM_ANIM_VALUE_GET_INDEX(o->data.anim.values[i]);

					v->flagsMutable.localAnimIsPredicted = o->flags.isPredicted;
					v->local.lastAnimMutable.pc = o->pc.client;
					v->local.lastAnimMutable.anim = animToStart;
					eaiClearFast(&v->local.lastAnimMutable.flags);
					eaiClearFast(&v->local.spcPerFlagMutable);
			
					if(	params &&
						doApplyAnimDirectly &&
						(	v->flags.lastAnimIsLocal ||
							subSPC_CPC(v->lastAnim.pc, v->local.lastAnim.pc) < 0)
						)
					{
						if(	v->flags.localAnimIsPredicted ||
							animToStart != mgState.animBitHandle.animOwnershipReleased)
						{
							v->flagsMutable.lastAnimIsLocal = 1;
							mmAnimViewStartAnim(mm, params, animToStart);
							v->lastAnimMutable.pc = o->pc.client;
							v->lastAnimMutable.anim = animToStart;
							v->lastAnimMutable.isLocal = 1;
							eaiClearFast(&v->lastAnimMutable.flags);
						}
					}
				}
				xcase MAVT_FLAG:{
					const U32 flag = MM_ANIM_VALUE_GET_INDEX(o->data.anim.values[i]);

					eaiPush(&v->local.lastAnimMutable.flags, flag);
					eaiPush(&v->local.spcPerFlagMutable, o->pc.server);
				
					if(	params &&
						doApplyAnimDirectly &&
						v->flags.lastAnimIsLocal)
					{
						mmAnimViewPlayFlag(mm, params, flag);
						eaiPush(&v->lastAnimMutable.flags, flag);
					}
				}
				xcase MAVT_DETAIL_ANIM_TO_START:{
					if(params){
						const U32 anim = MM_ANIM_VALUE_GET_INDEX(o->data.anim.values[i]);
						mmAnimViewStartDetailAnim(mm, params, anim);
					}
				}
				xcase MAVT_DETAIL_FLAG:{
					if (params) {
						const U32 flag = MM_ANIM_VALUE_GET_INDEX(o->data.anim.values[i]);
						mmAnimViewPlayDetailFlag(mm, params, flag);
					}
				}
			}
		}
		EARRAY_FOREACH_END;

		if(!doApplyAnimDirectly){
			// Check if the anim is the same.
				
			mmAnimViewLocalPlayLastAnim(mm, params);
		}
	}
	
	if(FALSE_THEN_SET(checkedLastAnim)){
		mmAnimViewLocalPlayLastAnim(mm, params);
	}
}

static void mmAnimViewUpdateLocal(	MovementManager* mm,
									MovementThreadData* td,
									const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView*	v = mm->fg.view;
	MovementOutput*			oStartPlaying;
	const U32				cpcCeiling = mgState.fg.localView.pcCeiling;
	const MovementOutput*	oFirstReplay = NULL;

	mmLog(mm, NULL, "[fg.curanim] BEGIN local anim view update.");

	if(!v->flags.appliedViewLocal){
		params = NULL;
	}
	
	if(	TRUE_THEN_RESET(mm->fg.flagsMutable.needsAnimReplayLocal)
		||
		td->toFG.outputList.head &&
		!td->toFG.outputList.head->flags.animViewedLocal)
	{
		mmAnimViewLocalFindReplayStart(mm, td, params, &oFirstReplay);
	}
	
	mmAnimViewLocalUndoNewOutputs(mm, td, params, oFirstReplay, cpcCeiling, &oStartPlaying);

	mmAnimViewLocalPlayOutputs(mm, td, params, oStartPlaying, cpcCeiling);

	mmLog(mm, NULL, "[fg.curanim] END local anim view update.");
}

static void mmAnimViewInitializeLocalNet(	MovementManager* mm,
											const MovementThreadData* td)
{
	MovementManagerFGView*	v = mm->fg.view;
	MovementOutput*			o;

	eaiCopy(&v->localNet.stanceBitsMutable,
			&td->toFG.stanceBits);

	for(o = td->toFG.outputList.tail;
		o;
		o = (o == td->toFG.outputList.head ? NULL : o->prev))
	{
		o->flagsMutable.animViewedLocalNet = 0;

		mmAnimValuesApplyStanceDiff(mm,
									&o->data.anim,
									1,
									&v->localNet.stanceBitsMutable,
									__FUNCTION__, 0);
	}
}

static void mmAnimViewUpdateLocalNet(	MovementManager* mm,
										const MovementThreadData* td,
										const U32 spcCeiling,
										const MovementOutput** oLocalNetOut)
{
	MovementManagerFGView*	v = mm->fg.view;
	MovementOutput*			o;
	const U32				cpcCeiling =	spcCeiling -
											mgState.fg.netReceive.cur.offset.clientToServerSync;

	if(TRUE_THEN_RESET(mm->fg.flagsMutable.needsAnimReplayLocalNet)){
		mmAnimViewInitializeLocalNet(mm, td);
	}

	// Find the first unviewed output.

	for(o = td->toFG.outputList.tail;
		o;
		o = (o == td->toFG.outputList.head ? NULL : o->prev))
	{
		if(	o != td->toFG.outputList.head &&
			subS32(o->pc.client, cpcCeiling) > 0)
		{
			continue;
		}

		if(!*oLocalNetOut){
			*oLocalNetOut = o;
		}
		
		if(o->flags.animViewedLocalNet){
			o = NULL;
			break;
		}
		
		if(o == td->toFG.outputList.head){
			mmAnimViewInitializeLocalNet(mm, td);
			break;
		}

		if(o->prev->flags.animViewedLocalNet){
			break;
		}
	}
	
	// Play unviewed outputs.
	
	for(;
		o && subS32(o->pc.client, cpcCeiling) <= 0;
		o = (o == td->toFG.outputList.tail ? NULL : o->next))
	{
		o->flagsMutable.animViewedLocalNet = 1;
		
		mmAnimValuesApplyStanceDiff(mm,
									&o->data.anim,
									0,
									&v->localNet.stanceBitsMutable,
									__FUNCTION__, 0);
	}
}

static void mmAnimViewInitializeNet(MovementManager* mm,
									const MovementThreadData* td)
{
	MovementManagerFGView*	v = mm->fg.view;
	MovementNetOutput*		no;

	if(mm->fg.net.flags.lastAnimUpdateWasNotStored){
		mmLastAnimCopy(	&v->net.lastAnimMutable,
						&mm->fg.net.lastStored.lastAnim);

		eaiCopy(&v->net.stanceBitsMutable,
				&mm->fg.net.lastStored.stanceBits);
	}else{
		mmLastAnimCopy(	&v->net.lastAnimMutable,
						&mm->fg.net.lastAnim);

		eaiCopy(&v->net.stanceBitsMutable,
				&mm->fg.net.stanceBits);
	}

	for(no = mm->fg.net.outputList.tail;
		no;
		no = no->prev)
	{
		S32 foundAnim = 0;

		no->flagsMutable.animBitsViewed = 0;

		mmAnimValuesApplyStanceDiff(mm,
									&no->data.anim,
									1,
									&v->net.stanceBitsMutable,
									__FUNCTION__, 0);

		EARRAY_INT_CONST_FOREACH_BEGIN(no->data.anim.values, i, isize);
		{
			switch(MM_ANIM_VALUE_GET_TYPE(no->data.anim.values[i])){
				xcase MAVT_LASTANIM_ANIM:{
					// Skip PC.
					i++;
				}
				xcase MAVT_ANIM_TO_START:{
					foundAnim = 1;
					mmLastAnimCopyFromValues(	&v->net.lastAnimMutable,
												&no->data.anim);
					break;
				}
			}
		}
		EARRAY_FOREACH_END;

		if(!foundAnim){
			mmAnimValuesRemoveFromFlags(&no->data.anim,
										&v->net.lastAnimMutable.flags);
		}
	}
}

static void mmAnimViewUpdateStancesNetUsed(	MovementManager* mm,
											const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView* v = mm->fg.view;
	
	if(!v->flags.appliedViewNet){
		return;
	}

	// Set stances that weren't predicted.

	EARRAY_INT_CONST_FOREACH_BEGIN(v->net.stanceBits, i, isize);
	{
		const MovementRegisteredAnimBit* bit;

		mmGetLocalAnimBitFromHandle(&bit, v->net.stanceBits[i], 0);

		// If it's not predicted and not in the used list, then add it.

		if(	(	!v->flags.appliedViewLocal ||
				eaiFind(&v->localNet.stanceBits, bit->index) < 0)
			&&
			eaiFind(&v->netUsed.stanceBits, bit->index) < 0)
		{
			eaiPush(&v->netUsed.stanceBitsMutable, bit->index);
			
			if(	!v->flags.appliedViewLocal ||
				eaiFind(&v->local.stanceBits, bit->index) < 0)
			{
				params->func.setStance(params->skeleton, bit->bitName);
			}
		}
	}
	EARRAY_FOREACH_END;
	
	// Remove any current used stances that are not present in the net view or that were predicted.
	
	EARRAY_INT_CONST_FOREACH_BEGIN(v->netUsed.stanceBits, i, isize);
	{
		const MovementRegisteredAnimBit* bit;

		mmGetLocalAnimBitFromHandle(&bit, v->netUsed.stanceBits[i], 0);

		if(	eaiFind(&v->net.stanceBits, bit->index) < 0
			||
			v->flags.appliedViewLocal &&
			eaiFind(&v->localNet.stanceBits, bit->index) >= 0)
		{
			if(	!v->flags.appliedViewLocal ||
				eaiFind(&v->local.stanceBits, bit->index) < 0)
			{
				params->func.clearStance(params->skeleton, bit->bitName);
			}

			eaiRemove(&v->netUsed.stanceBitsMutable, i);
			i--;
			isize--;
		}
	}
	EARRAY_FOREACH_END;
}

static S32 mmAnimValuesContainsDetailAnim(	const MovementAnimValues* anim,
											U32 detailAnim)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				// Skip PC.
				i++;
			}
			xcase MAVT_DETAIL_ANIM_TO_START:{
				if(detailAnim == MM_ANIM_VALUE_GET_INDEX(anim->values[i])){
					return 1;
				}
			}
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 mmAnimValuesContainsDetailFlag(	const MovementAnimValues *anim,
											U32 detailFlag)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])) {
			xcase MAVT_LASTANIM_ANIM:{
				//Skip PC
				i++;
			}
			xcase MAVT_DETAIL_FLAG:{
				if (detailFlag == MM_ANIM_VALUE_GET_INDEX(anim->values[i])){
					return 1;
				}
			}
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static void mmAnimViewNetCheckApply(MovementManager* mm,
									const DynSkeletonPreUpdateParams* params,
									MovementNetOutput* no,
									const MovementOutput* oLocalNet)
{
	MovementManagerFGView*	v = mm->fg.view;
	U32						flagCount = eaiUSize(&v->lastAnim.flags);
	U32						netFlagCount = eaiUSize(&v->net.lastAnim.flags);
	S32						startAnim = 0;
	U32						uiOverrideTimeApply = 0;

	if(v->flags.lastAnimIsLocal)
	{
		//if(v->flags.localAnimIsPredicted){
		//	assert(flagCount == eaiUSize(&v->local.spcPerFlag));
		//}

		// Check if predicted anim should be stomped.
		if(	!v->lastAnim.isLocal && subS32(v->net.lastAnim.pc, v->lastAnim.pc) > 0 ||
			 v->lastAnim.isLocal && subSPC_CPC(v->net.lastAnim.pc, v->lastAnim.pc) > 0)
		{
			//printfColor(COLOR_RED,"%s: anim is more recent than local anim.\n",__FUNCTION__);
			startAnim = 1;
		}
		else if(!v->lastAnim.isLocal && subS32(v->net.lastAnim.pc, v->lastAnim.pc) < 0 ||
				 v->lastAnim.isLocal && subSPC_CPC(v->net.lastAnim.pc, v->lastAnim.pc) < 0)
		{
			// Server anim is older than local anim, see if local anim is mis-predicted
			S32 limitPCDiff = 0;
			if (gConf.bCombatDeathPrediction && MM_ANIM_HANDLE_WITHOUT_ID(v->lastAnim.anim) == mmAnimBitHandles.death) {
				limitPCDiff = (MM_DEATH_PREDICTION_ANIM_ALLOWED_STEPS+1) * MM_PROCESS_COUNTS_PER_STEP;
			}
			if (!v->lastAnim.isLocal && subS32(no->pc.server, v->lastAnim.pc)     >= limitPCDiff ||
				 v->lastAnim.isLocal && subSPC_CPC(no->pc.server, v->lastAnim.pc) >= limitPCDiff )
			{
				//printfColor(COLOR_RED,"%s: View is later than local view's lastAnim, so stomp it.\n",__FUNCTION__);
				startAnim = 1;
			}
		}
		else if(v->net.lastAnim.anim != v->lastAnim.anim)
		{
			//printfColor(COLOR_RED,"%s: anim has same pc but is different anim.\n",__FUNCTION__);
			startAnim = 1;
		}
		else if(netFlagCount <= flagCount)
		{
			// Same anim, same spc, check if the flags match.
			if(CompareStructs(	v->net.lastAnim.flags,
								v->lastAnim.flags,
								netFlagCount))
			{
				//printfColor(COLOR_RED,"%s: Predicted flags are different.\n",__FUNCTION__);
				startAnim = 1;
				uiOverrideTimeApply = 1;
			}
			//else if(netFlagCount < flagCount &&
			//		subS32(no->pc.server, v->local.spcPerFlag[netFlagCount]) >= 0)
			//{
				// Predicted too many flags for this SPC.
				//startAnim = 1;
				//bOverrideTime = true;
			//}
		}
		else if(CompareStructs(	v->net.lastAnim.flags,
								v->lastAnim.flags,
								flagCount))
		{
			//printfColor(COLOR_RED,"%s: Predicted flags are different. (2)\n",__FUNCTION__);
			startAnim = 1;
			uiOverrideTimeApply = 1;
		}
		else
		{
			// Predicted flags are the same, just not enough for this SPC.
			// Switch to net-view to play the rest of the flags.
			v->flagsMutable.lastAnimIsLocal = 0;
		}
	}
	else
	{
		// v->lastAnim is net, so just check if there's a newer anim to play.
		// note: if you see a pc of zero here, it may have been blasted on skeleton recreation so the bits would be resent
		if(	!v->lastAnim.isLocal && subS32(v->net.lastAnim.pc, v->lastAnim.pc) > 0 ||
			 v->lastAnim.isLocal && subSPC_CPC(v->net.lastAnim.pc, v->lastAnim.pc) > 0)
		{
			//printfColor(COLOR_RED,"%s: Anim is newer.\n",__FUNCTION__);
			startAnim = 1;
		}
#ifdef GAMECLIENT
		else if (subS32(v->net.lastAnim.pc, v->lastAnim.pc) == 0 && v->lastAnim.anim == 0 && v->net.lastAnim.anim > 0 && demo_playingBack())
		{
			//printfColor(COLOR_RED,"%s: Special case for first frame anim initialization of demo playback.\n",__FUNCTION__);
			startAnim = 1;
		}
#endif
		else if(netFlagCount < flagCount ||
				CompareStructs(	v->net.lastAnim.flags,
								v->lastAnim.flags,
								flagCount))
		{
			//printfColor(COLOR_RED,"%s: flags are different (2)\n",__FUNCTION__);
			startAnim = 1;
			if (v->lastAnim.anim == v->net.lastAnim.anim)
				uiOverrideTimeApply = 1;
		}
	}

	if(startAnim){
		v->flagsMutable.lastAnimIsLocal = 0;
		mmAnimViewSetOverrideTime(mm, params, 0.f, uiOverrideTimeApply);               
		mmAnimViewStartAnim(mm, params, v->net.lastAnim.anim);
		v->lastAnimMutable.pc = v->net.lastAnim.pc;
		v->lastAnimMutable.anim = v->net.lastAnim.anim;
		v->lastAnimMutable.isLocal = 0;
		eaiClearFast(&v->lastAnimMutable.flags);
		flagCount = 0;
	}

	if(!v->flags.lastAnimIsLocal){
		EARRAY_INT_CONST_FOREACH_BEGIN_FROM(v->net.lastAnim.flags, i, isize, flagCount);
		{
			mmAnimViewPlayFlag(mm, params, v->net.lastAnim.flags[i]);
			eaiPush(&v->lastAnimMutable.flags, v->net.lastAnim.flags[i]);
		}
		EARRAY_FOREACH_END;
	}

	EARRAY_INT_CONST_FOREACH_BEGIN(no->data.anim.values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(no->data.anim.values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				// Skip PC.
				i++;
			}
			xcase MAVT_DETAIL_ANIM_TO_START:{
				const U32 anim = MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]);

				if(	!oLocalNet ||
					!v->flags.appliedViewLocal ||
					!mmAnimValuesContainsDetailAnim(&oLocalNet->data.anim, anim))
				{
					mmAnimViewStartDetailAnim(mm, params, anim);
				}
			}
			xcase MAVT_DETAIL_FLAG:{
				const U32 flag = MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]);

				if(	!oLocalNet ||
					!v->flags.appliedViewLocal ||
					!mmAnimValuesContainsDetailFlag(&oLocalNet->data.anim, flag))
				{
					mmAnimViewPlayDetailFlag(mm, params, flag);
				}
			}
		}
	}
	EARRAY_FOREACH_END;
}

static void mmAnimViewUpdateNet(MovementManager* mm,
								const MovementThreadData* td,
								const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView*	v = mm->fg.view;
	MovementNetOutput*		no;
	S32						didInitialize = 0;
	
	mmLog(mm, NULL, "[fg.curanim] BEGIN net anim view update.");

	if(!v->flags.appliedViewNet){
		params = NULL;
	}
	
	// Find the first unviewed output.
	
	for(no = mm->fg.net.outputList.tail;
		no;
		no = no->prev)
	{
		if(	no != mm->fg.net.outputList.head &&
			subS32(no->pc.server, mgState.fg.netView.spcCeiling) > 0)
		{
			continue;
		}
		
		if(no->flags.animBitsViewed){
			no = NULL;
			break;
		}
		
		if(!no->prev){
			didInitialize = 1;
			mmAnimViewInitializeNet(mm, td);
			break;
		}
		else if(no->prev->flags.animBitsViewed){
			break;
		}
	}
	
	// Play all outputs until the end.
	
	mmLastAnimLogNet(mm, &v->net.lastAnim, NULL, "ViewNet.BeforePlaying");

	for(;
		no && subS32(no->pc.server, mgState.fg.netView.spcCeiling) <= 0;
		no = no->next)
	{			
		const MovementOutput* oLocalNet = NULL;

		no->flagsMutable.animBitsViewed = 1;
		
		mmAnimViewUpdateLocalNet(mm, td, no->pc.server, &oLocalNet);
		
		mmAnimValuesApplyStanceDiff(mm,
									&no->data.anim,
									0,
									&v->net.stanceBitsMutable,
									__FUNCTION__, 0);

		mmAnimViewUpdateStancesNetUsed(mm, params);

		EARRAY_INT_CONST_FOREACH_BEGIN(no->data.anim.values, i, isize);
		{
			switch(MM_ANIM_VALUE_GET_TYPE(no->data.anim.values[i])){
				xcase MAVT_LASTANIM_ANIM:{
					// Skip PC.
					i++;
				}
				xcase MAVT_ANIM_TO_START:{
					if(mm->fg.flags.isAttachedToClient){
						v->net.lastAnimMutable.pc = no->pc.client +
													mgState.fg.netReceive.cur.offset.clientToServerSync;
					}else{
						v->net.lastAnimMutable.pc = no->pc.server;
					}
					v->net.lastAnimMutable.anim = MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]);
					eaiClearFast(&v->net.lastAnimMutable.flags);
				}
				xcase MAVT_FLAG:{
					eaiPush(&v->net.lastAnimMutable.flags,
							MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]));
				}
			}
		}
		EARRAY_FOREACH_END;

		// Check if the net anim should be applied.

		if(params){
			mmAnimViewNetCheckApply(mm, params, no, oLocalNet);
		}
	}

	mmLastAnimLogNet(mm, &v->net.lastAnim, NULL, "ViewNet.AfterPlaying");

	mmLog(mm, NULL, "[fg.curanim] END net anim view update.");
}

void mmAnimViewQueueResetFG(MovementManager* mm){
	if(SAFE_MEMBER(mm, fg.view)){
		mm->fg.view->flagsMutable.needsReset = 1;
	}
}

static void mmAnimViewClearStancesOnSkeleton(	const U32*const stances,
												const DynSkeletonPreUpdateParams* params)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(stances, i, isize);
	{
		const MovementRegisteredAnimBit* bit;

		if(mmRegisteredAnimBitGetByHandle(	&mgState.animBitRegistry,
											&bit,
											stances[i]))
		{
			params->func.clearStance(params->skeleton, bit->bitName);
		}
	}
	EARRAY_FOREACH_END;
}

static void mmAnimViewSetStancesOnSkeleton(	const U32*const stances,
											const DynSkeletonPreUpdateParams* params)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(stances, i, isize);
	{
		const MovementRegisteredAnimBit* bit;

		if(mmRegisteredAnimBitGetByHandle(	&mgState.animBitRegistry,
											&bit,
											stances[i]))
		{
			params->func.setStance(params->skeleton, bit->bitName);
		}
	}
	EARRAY_FOREACH_END;
}

static void mmAnimViewReset(MovementManager* mm,
							const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView* v = mm->fg.view;

	mmLog(mm, NULL, "[fg.curanim] Resetting anim view.");

	if(TRUE_THEN_RESET(v->flagsMutable.appliedViewLocal)){
		mmAnimViewClearStancesOnSkeleton(v->local.stanceBits, params);
	}

	if(TRUE_THEN_RESET(v->flagsMutable.appliedViewNet)){
		mmAnimViewClearStancesOnSkeleton(v->netUsed.stanceBits, params);
		eaiClearFast(&v->netUsed.stanceBitsMutable);
	}

	v->lastAnimMutable.pc = 0;
	v->lastAnimMutable.anim = 0;
	v->lastAnimMutable.isLocal = 0;
	eaiClear(&v->lastAnimMutable.flags);

	v->flagsMutable.lastAnimIsLocal = 0;
}

static void mmAnimViewLogAfterUpdate(MovementManager* mm){
	const MovementManagerFGView*	v = mm->fg.view;
	const MovementThreadData*		td = MM_THREADDATA_FG(mm);
	const MovementOutput*			oLocal;
	const MovementOutput*			oLocalNet;
	const MovementNetOutput*		no;
	
	for(oLocal = td->toFG.outputList.tail;
		oLocal;
		oLocal = (oLocal == td->toFG.outputList.head ? NULL : oLocal->prev))
	{
		if(oLocal->flags.animViewedLocal){
			break;
		}
	}

	for(oLocalNet = td->toFG.outputList.tail;
		oLocalNet;
		oLocalNet = (oLocalNet == td->toFG.outputList.head ? NULL : oLocalNet->prev))
	{
		if(oLocalNet->flags.animViewedLocalNet){
			break;
		}
	}
	
	for(no = mm->fg.net.outputList.tail;
		no;
		no = no->prev)
	{
		if(no->flags.animBitsViewed){
			break;
		}
	}

	if(oLocal){
		mmLog(	mm,
				NULL,
				"[fg.curanim] Local output c%u/s%u: %1.2f, %1.2f, %1.2f",
				oLocal->pc.client,
				oLocal->pc.server,
				vecParamsXYZ(oLocal->data.pos));
		
		mmLogSegmentOffset(mm, NULL, "fg.curanim", 0xffff0000, oLocal->data.pos, unitmat[0]);
	}
	
	if(oLocalNet){
		mmLog(	mm,
				NULL,
				"[fg.curanim] LocalNet output c%u/s%u: %1.2f, %1.2f, %1.2f",
				oLocalNet->pc.client,
				oLocalNet->pc.server,
				vecParamsXYZ(oLocalNet->data.pos));

		mmLogSegmentOffset(mm, NULL, "fg.curanim", 0xff00ff00, oLocalNet->data.pos, unitmat[1]);
	}

	if(no){
		mmLog(	mm,
				NULL,
				"[fg.curanim] Net output c%u/s%u: %1.2f, %1.2f, %1.2f",
				no->pc.client,
				no->pc.server,
				vecParamsXYZ(no->data.pos));

		mmLogSegmentOffset(mm, NULL, "fg.curanim", 0xff0000ff, no->data.pos, unitmat[2]);
	}

	mmLogAnimBitArray(	mm,
						v->local.stanceBits,
						0,
						mgState.fg.localView.pcCeiling,
						mgState.fg.netView.spcCeiling,
						NULL,
						"Local stances");

	mmLogAnimBitArray(	mm,
						v->net.stanceBits,
						0,
						mgState.fg.localView.pcCeiling,
						mgState.fg.netView.spcCeiling,
						NULL,
						"Net stances");

	mmLogAnimBitArray(	mm,
						v->localNet.stanceBits,
						0,
						mgState.fg.localView.pcCeiling,
						mgState.fg.netView.spcCeiling,
						NULL,
						"LocalNet stances");

	mmLogAnimBitArray(	mm,
						v->netUsed.stanceBits,
						0,
						mgState.fg.localView.pcCeiling,
						mgState.fg.netView.spcCeiling,
						NULL,
						"NetUsed stances");
	
	mmLogAnimBitArray(	mm,
						mm->fg.net.stanceBits,
						0,
						mgState.fg.localView.pcCeiling,
						mgState.fg.netView.spcCeiling,
						NULL,
						"Latest received stances");
	
	if(mm->fg.net.flags.lastAnimUpdateWasNotStored){
		mmLogAnimBitArray(	mm,
							mm->fg.net.lastStored.stanceBits,
							0,
							mgState.fg.localView.pcCeiling,
							mgState.fg.netView.spcCeiling,
							NULL,
							"Latest used received stances");
	}
}

static void mmAnimViewCheckIfApplyingLocal(	MovementManager* mm,
											const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView*	v = mm->fg.view;
	const bool				applyLocalView = !mgState.fg.flags.predictDisabled;

	if((bool)v->flags.appliedViewLocal != applyLocalView){
		v->flagsMutable.appliedViewLocal = applyLocalView;
		
		if(applyLocalView){
			if(v->flags.appliedViewNet){
				mmAnimViewClearStancesOnSkeleton(v->netUsed.stanceBits, params);
				eaiClearFast(&v->netUsed.stanceBitsMutable);
			}

			mmAnimViewSetStancesOnSkeleton(v->local.stanceBits, params);
			mmAnimViewUpdateStancesNetUsed(mm, params);
		}else{
			mmAnimViewClearStancesOnSkeleton(v->local.stanceBits, params);
		}
	}
}
	
static void mmAnimViewCheckIfApplyingNet(	MovementManager* mm,
											const DynSkeletonPreUpdateParams* params)
{
	MovementManagerFGView*	v = mm->fg.view;
	const bool				applyNetView =	!v->flags.appliedViewLocal ||
											!mgState.fg.flags.noSyncWithServer ||
											!mm->fg.flags.isAttachedToClient;

	if((bool)v->flags.appliedViewNet != applyNetView){
		v->flagsMutable.appliedViewNet = applyNetView;
		
		if(applyNetView){
			mmAnimViewUpdateStancesNetUsed(mm, params);
		}else{
			mmAnimViewClearStancesOnSkeleton(v->netUsed.stanceBits, params);
			eaiClearFast(&v->netUsed.stanceBitsMutable);
		}
	}
}

S32 mmSkeletonPreUpdateCallbackNew(const DynSkeletonPreUpdateParams* params){
	MovementManager*		mm = params->userData;
	MovementThreadData*		td = MM_THREADDATA_FG(mm);
	MovementManagerFGView*	v = mm->fg.view;

	if(MMLOG_IS_ENABLED(mm)){
		mmSetIsForegroundThreadForLogging();
		mmLog(mm, NULL, "[fg.curanim] BEGIN anim view update.");
	}

	// Create or reset the view.
	
	if(!v){
		v = mm->fg.view = callocStruct(MovementManagerFGView);
	}
	else if(TRUE_THEN_RESET(v->flagsMutable.needsReset) ||
			params->doReset)
	{
		mmAnimViewReset(mm, params);
	}
	
	// Check if predict or net sync were toggled.
	
	mmAnimViewCheckIfApplyingLocal(mm, params);
	mmAnimViewCheckIfApplyingNet(mm, params);
	
	// Update the local and net view.
	
	mmAnimViewUpdateLocal(mm, td, params);
	mmAnimViewUpdateNet(mm, td, params);
	mmResourcesPlayDetailAnimsFG(mm, params);

	// Log the current state.
	
	if(MMLOG_IS_ENABLED(mm)){
		mmAnimViewLogAfterUpdate(mm);
		mmLog(mm, NULL, "[fg.curanim] END anim view update.");
	}

	return 0;
}

static S32 mmDoResetAnims = 1;
AUTO_CMD_INT(mmDoResetAnims, mmDoResetAnims);

S32 mmSkeletonPreUpdateCallbackOld(const DynSkeletonPreUpdateParams* params){
	MovementManager*		mm = params->userData;
	MovementThreadData*		td = MM_THREADDATA_FG(mm);
	MovementOutput*			oLatest = NULL;
	MovementNetOutput*		noLatest = NULL;
	S32						returnMoreStateChanges = 0;
	MovementOutput*			o;
	MovementManagerFGView*	v = mm->fg.view;
	
	// Create or reset the view.
	
	if(!v){
		v = mm->fg.view = callocStruct(MovementManagerFGView);
	}
	else if(TRUE_THEN_RESET(v->flagsMutable.needsReset)){
		if(mmDoResetAnims){
			while(eaiSize(&v->animValues.values)){
				mmRemoveAnimBit(mm,
								&mgState.animBitRegistry,
								&v->animValuesMutable,
								v->animValues.values[0],
								params);
			}
		}
	}
	
	if(MMLOG_IS_ENABLED(mm)){
		mmSetIsForegroundThreadForLogging();

		mmLog(	mm,
				NULL,
				"[fg.curanim] Updating anim bits (local %d, net %d).",
				mgState.fg.localView.pcCeiling,
				mgState.fg.netView.spcCeiling);
	
		if(!params->callCountOnThisFrame){
			// Log the outputs.
			{
				const MovementOutput* oHead = td->toFG.outputList.head;
				const MovementOutput* oTail = td->toFG.outputList.tail;

				if(oHead){
					assert(oTail);
					
					mmLog(	mm,
							NULL,
							"[fg.curanim] Local outputs: c(%d-%d)",
							oHead->pc.client,
							oTail->pc.client);
				}
			}
			
			// Log the net outputs.

			{
				const MovementNetOutput* noHead = mm->fg.net.outputList.head;
				const MovementNetOutput* noTail = mm->fg.net.outputList.tail;

				if(noHead){
					assert(noTail);
					
					mmLog(	mm,
							NULL,
							"[fg.curanim] Net outputs s(%d-%d) c(%d-%d)",
							noHead->pc.server,
							noTail->pc.server,
							noHead->pc.client,
							noTail->pc.client);
				}
			}
		}
	}
	
	for(o = td->toFG.outputList.tail;
		o;
		o = (o == td->toFG.outputList.head ? NULL : o->prev))
	{
		if(subS32(	o->pc.client,
					mgState.fg.localView.pcCeiling) > 0)
		{
			continue;
		}
		
		if(!mgState.fg.flags.predictDisabled){
			oLatest = o;
		}

		if(!o->flags.animBitsViewed){
			// My bits haven't been viewed yet.
			
			S32 hasFlashBitsToViewOnNextCall = 0;

			// Check if the previous frame was viewed yet, and if not then use it instead.

			if( o != td->toFG.outputList.head &&
				!o->prev->flags.animBitsViewed)
			{
				continue;
			}

			if(!mgState.fg.flags.predictDisabled){
				// Use my bits, because predict is on.
				
				if(MMLOG_IS_ENABLED(mm)){
					mmLogAnimBitArray(	mm,
										v->animValues.values,
										0,
										mgState.fg.localView.pcCeiling,
										mgState.fg.netView.spcCeiling,
										NULL,
										"Bits before");

					mmLogAnimBitArray(	mm,
										o->data.anim.values,
										0,
										o->pc.client,
										0,
										NULL,
										o->flags.hasFlashBitsToView ?
											"Output bits (will flash)" :
											"Output bits");
				}

				// Add all the bits from o.

				mmSkeletonPreUpdateAddOutputBits(	mm,
													o,
													&hasFlashBitsToViewOnNextCall,
													params);
			}

			if(	hasFlashBitsToViewOnNextCall &&
				!o->flags.hasFlashBitsToView &&
				!o->flags.flashBitsViewed)
			{
				o->flagsMutable.hasFlashBitsToView = 1;
			}else{
				o->flagsMutable.animBitsViewed = 1;
				o->flagsMutable.flashBitsViewed = 1;
			}

			if(	subS32(	o->pc.client,
						mgState.fg.localView.pcCeiling) < 0 &&
				o != td->toFG.outputList.tail
				||
				!o->flags.flashBitsViewed)
			{
				returnMoreStateChanges = 1;
			}
		}

		break;
	}

	if(	!mgState.fg.flags.predictDisabled &&
		!oLatest &&
		td->toFG.outputList.tail)
	{
		oLatest = td->toFG.outputList.tail;
	}

	if(	oLatest &&
		MMLOG_IS_ENABLED(mm))
	{
		mmLogAnimBitArray(	mm,
							oLatest->data.anim.values,
							0,
							oLatest->pc.client,
							oLatest->pc.server,
							NULL,
							"Latest local");
	}

	// Deal with net outputs.

	if(	!mgState.fg.flags.noSyncWithServer ||
		!SAFE_MEMBER(oLatest, flags.posIsPredicted))
	{
		MovementNetOutput* no;
		
		for(no = mm->fg.net.outputList.tail;
			no;
			no = no->prev)
		{
			S32 hasFlashBitsToViewOnNextCall;

			if(	no != mm->fg.net.outputList.head &&
				subS32(	no->pc.server,
						mgState.fg.netView.spcCeiling) > 0)
			{
				continue;
			}
			
			noLatest = no;

			if(no->flags.animBitsViewed){
				break;
			}
			
			if( no->prev &&
				!no->prev->flags.animBitsViewed)
			{
				continue;
			}

			hasFlashBitsToViewOnNextCall = 0;

			if(no->animBitCombo){
				if(MMLOG_IS_ENABLED(mm)){
					mmLogAnimBitCombo(	mm,
										no->animBitCombo,
										1,
										no->pc.client,
										no->pc.server,
										no->flags.hasFlashBitsToView ?
											"NetOutput bits (will flash)" :
											"NetOutput bits");
				}

				// Add all the bits from this net output.

				EARRAY_INT_CONST_FOREACH_BEGIN(no->animBitCombo->bits, j, jsize);
				{
					const MovementRegisteredAnimBit*	bitLocal;
					S32									foundLocalOutput = 0;

					mmRegisteredAnimBitTranslateHandle(	&mgState.fg.netReceiveMutable.animBitRegistry,
														no->animBitCombo->bits[j],
														&mgState.animBitRegistry,
														&bitLocal);

					if(!mgState.fg.flags.predictDisabled){
						const MovementOutput*	oLocal = NULL;
						S32						moveDir = 0;
						
						if(!td->toFG.lastViewedOutput){
							td->toFG.lastViewedOutputMutable = td->toFG.outputList.tail;
						}
						
						// Find the local output with closest pc match to no.
						
						if(td->toFG.lastViewedOutput){
							MovementOutput* oViewed = td->toFG.lastViewedOutput;
							
							while(1){
								S32 diff;
								
								if(mm->fg.flags.isAttachedToClient){
									diff = subS32(	no->pc.client,
													oViewed->pc.client);
								}else{
									diff = subS32(	no->pc.server,
													oViewed->pc.server);
								}
							
								if(!diff){
									// Found the right one, see if the bit was predicted.
									
									if(!j){
										mmLogAnimBitArray(	mm,
															oViewed->data.anim.values,
															0,
															oViewed->pc.client,
															oViewed->pc.server,
															NULL,
															"Found matching local");
									}

									if(mmAnimValuesContains(	&oViewed->data.anim,
																bitLocal->index))
									{
										foundLocalOutput = 1;
									}

									break;
								}

								if(diff < 0){
									// Goto prev output.
									
									if(	!oViewed->prev ||
										oViewed == td->toFG.outputList.head ||
										moveDir > 0)
									{
										if(!j){
											mmLogAnimBitArray(	mm,
																oViewed->data.anim.values,
																0,
																oViewed->pc.client,
																oViewed->pc.server,
																NULL,
																"Found closest matching local");
										}
										
										break;
									}

									moveDir = -1;
									oViewed = oViewed->prev;
								}else{
									// Goto next output.
									
									if(	!oViewed->next ||
										oViewed == td->toFG.outputList.tail ||
										moveDir < 0)
									{
										if(!j){
											mmLogAnimBitArray(	mm,
																oViewed->data.anim.values,
																0,
																oViewed->pc.client,
																oViewed->pc.server,
																NULL,
																"Found closest matching local");
										}

										break;
									}
									
									moveDir = 1;
									oViewed = oViewed->next;
								}
							}
							
							td->toFG.lastViewedOutputMutable = oViewed;
						}
					}

					if(!foundLocalOutput){
						// Anim bit was not predicted, so play it.
						
						mmLog(	mm,
								NULL,
								"[fg.curanim] %s unpredicted anim bit: %s.",
								bitLocal->flags.isFlashBit ?
									no->flags.hasFlashBitsToView ?
										"Flashing" :
										"Removing flashed" :
									"Playing",
								bitLocal->bitName);

						if(bitLocal->flags.isFlashBit){
							hasFlashBitsToViewOnNextCall = 1;

							if(no->flags.hasFlashBitsToView){
								// Now add it (was removed in previous call).
								
								mmAddAnimBit(	mm,
												&mgState.animBitRegistry,
												&v->animValuesMutable,
												bitLocal->index,
												params);
							}else{
								// Remove the bit first, then add it next frame.
								
								mmRemoveAnimBit(mm,
												&mgState.animBitRegistry,
												&v->animValuesMutable,
												bitLocal->index,
												params);
							}
						}else{
							mmAddAnimBit(	mm,
											&mgState.animBitRegistry,
											&v->animValuesMutable,
											bitLocal->index,
											params);
						}
					}
				}
				EARRAY_FOREACH_END;
			}
			
			if(	hasFlashBitsToViewOnNextCall &&
				!no->flags.hasFlashBitsToView &&
				!no->flags.flashBitsViewed)
			{
				no->flagsMutable.hasFlashBitsToView = 1;
			}else{
				no->flagsMutable.animBitsViewed = 1;
				no->flagsMutable.flashBitsViewed = 1;
			}

			if(	subS32(	no->pc.server,
						mgState.fg.netView.spcCeiling) < 0 &&
				no != mm->fg.net.outputList.tail
				||
				!no->flags.flashBitsViewed)
			{
				returnMoreStateChanges = 1;
			}

			break;
		}
	}
	
	if(	noLatest &&
		MMLOG_IS_ENABLED(mm))
	{
		mmLogAnimBitCombo(	mm,
							noLatest->animBitCombo,
							1,
							noLatest->pc.client,
							noLatest->pc.server,
							"Latest net");
	}

	// Remove any bits not in this o.

	EARRAY_INT_CONST_FOREACH_BEGIN(v->animValues.values, j, jsize);
	{
		U32 localBitHandle = v->animValues.values[j];

		if( !oLatest ||
			!mmAnimValuesContains(	&oLatest->data.anim,
									localBitHandle))
		{
			S32 removeBit = 1;
			S32 foundLocalBit = 0;
			
			if(SAFE_MEMBER(noLatest, animBitCombo)){
				EARRAY_INT_CONST_FOREACH_BEGIN(noLatest->animBitCombo->bits, k, ksize);
				{
					const MovementRegisteredAnimBit* bitLocal;
					
					mmRegisteredAnimBitTranslateHandle(	&mgState.fg.netReceiveMutable.animBitRegistry,
														noLatest->animBitCombo->bits[k],
														&mgState.animBitRegistry,
														&bitLocal);
					
					if(bitLocal->index == localBitHandle){
						foundLocalBit = 1;
						break;
					}
				}
				EARRAY_FOREACH_END;
			}

			if(foundLocalBit){
				if(mgState.fg.flags.predictDisabled){
					removeBit = 0;
				}else{
					const MovementOutput*	oLocal = NULL;
					S32						moveDir = 0;
					
					if(!td->toFG.lastViewedOutput){
						td->toFG.lastViewedOutputMutable = td->toFG.outputList.tail;
					}
					
					if(td->toFG.lastViewedOutput){
						MovementOutput* oViewed = td->toFG.lastViewedOutput;
						
						while(1){
							S32 diff;
							
							if(mm->fg.flags.isAttachedToClient){
								diff = subS32(	noLatest->pc.client,
												oViewed->pc.client);
							}else{
								diff = subS32(	noLatest->pc.server,
												oViewed->pc.server);
							}
						
							if(!diff){
								oLocal = oViewed;
								break;
							}
							if(diff < 0){
								// Goto prev output.
								
								if(	!oViewed->prev ||
									oViewed == td->toFG.outputList.head ||
									moveDir > 0)
								{
									break;
								}

								moveDir = -1;
								oViewed = oViewed->prev;
							}else{
								// Goto next output.
								
								if(	!oViewed->next ||
									oViewed == td->toFG.outputList.tail ||
									moveDir < 0)
								{
									break;
								}
								
								moveDir = 1;
								oViewed = oViewed->next;
							}
						}
						
						td->toFG.lastViewedOutputMutable = oViewed;
					}
					
					if(	!oLocal ||
						!mmAnimValuesContains(	&oLocal->data.anim,
												localBitHandle))
					{
						// Bit wasn't predicted.
						
						removeBit = 0;
					}
				}
			}

			if(removeBit){
				// The net output didn't have the bit OR it matched a predicted bit, so remove it.

				if(MMLOG_IS_ENABLED(mm)){
					const MovementRegisteredAnimBit* bit;

					if(mmRegisteredAnimBitGetByHandle(	&mgState.animBitRegistry,
														&bit,
														localBitHandle))
					{
						mmLog(	mm,
								NULL,
								"[fg.curanim] Removing bit %s%s (handle %d).",
								bit->bitName,
								bit->flags.isFlashBit ? "(f)" : "",
								localBitHandle);
					}else{
						mmLog(	mm,
								NULL,
								"[fg.curanim] Removing bit handle \"%d\".",
								localBitHandle);
					}
				}
				
				mmRemoveAnimBit(mm,
								&mgState.animBitRegistry,
								&v->animValuesMutable,
								localBitHandle,
								params);

				j--;
				jsize--;
			}
		}
	}
	EARRAY_FOREACH_END;

	// Check if resources have any bits to set.

	if(	mm->fg.flags.mmrHasDetailAnim &&
		mmResourcesPlayDetailAnimsFG(mm, params))
	{
		returnMoreStateChanges = 1;
	}

	// Set to idle if nothing is set.

	if(!eaiUSize(&v->animValues.values)){
		mmAddAnimBit(	mm,
						&mgState.animBitRegistry,
						&v->animValuesMutable,
						mmAnimBitHandles.idle,
						params);
	}
	
	if(MMLOG_IS_ENABLED(mm)){
		mmLogAnimBitArray(	mm,
							v->animValues.values,
							0,
							mgState.fg.localView.pcCeiling,
							mgState.fg.netView.spcCeiling,
							NULL,
							"Current bits");

		if(	!gConf.bNewAnimationSystem &&
			params->skeleton)
		{
			mmLog(	mm,
					NULL,
					"[fg.curanim] Current action: %s (next: %s, prev: %s)\n",
					dynSeqGetCurrentActionName(params->skeleton->eaSqr[0]),
					dynSeqGetNextActionName(params->skeleton->eaSqr[0]),
					dynSeqGetPreviousActionName(params->skeleton->eaSqr[0]));
		}
	}

	return returnMoreStateChanges;
}

S32 mmSkeletonPreUpdateCallback(const DynSkeletonPreUpdateParams* params){
	if(gConf.bNewAnimationSystem){
		return mmSkeletonPreUpdateCallbackNew(params);
	}

	return mmSkeletonPreUpdateCallbackOld(params);
}

void mmSendMsgRequesterCreatedFG(	MovementManager* mm,
									MovementRequester* mr)
{
	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};

		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_REQUESTER_CREATED;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.fg.requesterCreated.mr = mr;

		mm->msgHandler(&pd.msg);
	}
}

static void mmHandleNewRequestersFromBG(MovementManager* mm,
										MovementThreadData*	td)
{
	PERFINFO_AUTO_START_FUNC();

	mm->fg.flagsMutable.mrIsNewToSend = 1;
	mm->fg.flagsMutable.mrNeedsAfterSend = 1;

	EARRAY_CONST_FOREACH_BEGIN(td->toFG.newRequesters, i, size);
	{
		MovementRequester* mr = td->toFG.newRequesters[i];

		ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.inList);
		eaPush(&mm->fg.requestersMutable, mr);
		
		assert(!mr->fg.flags.forcedSimIsEnabled);
		
		ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.inListBG);
		
		mmSendMsgRequesterCreatedFG(mm, mr);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&td->toFG.newRequestersMutable);
	
	PERFINFO_AUTO_STOP_FUNC();
}

static void mmLogOutputRepredictFG(	MovementManager* mm,
									MovementOutputRepredict* mor)
{
	const MovementOutput* o = mor->o;

	if(!sameVec3(o->data.pos, mor->data.pos)){
		mmLog(	mm,
				NULL,
				"[mispredict] Mispredicted %u by %1.3f:"
				" pOld(%1.2f, %1.2f, %1.2f)"
				" [%8.8x, %8.8x, %8.8x]"
				" pNew(%1.2f, %1.2f, %1.2f)"
				" [%8.8x, %8.8x, %8.8x]",
				o->pc.client,
				distance3(o->data.pos, mor->data.pos),
				vecParamsXYZ(o->data.pos),
				vecParamsXYZ((S32*)o->data.pos),
				vecParamsXYZ(mor->data.pos),
				vecParamsXYZ((S32*)mor->data.pos));

		mmLogSegment(	mm,
						NULL,
						"mispredict",
						0,
						o->data.pos,
						mor->data.pos);
	}
}

void mmSendMsgViewStatusChangedFG(	MovementManager* mm,
									const char* reason)
{
	if(mm->msgHandler){
		PERFINFO_AUTO_START_FUNC();
		{
			MovementManagerMsgPrivateData pd = {0};
			
			mmLog(	mm,
					NULL,
					"[fg.view] Sending msg VIEW_STATUS_CHANGED (%s) (atRest: %s%s%s%s).",
					FIRST_IF_SET(reason, "no reason given"),
					mm->fg.flags.posViewIsAtRest ? "pos," : "",
					mm->fg.flags.rotViewIsAtRest ? "rot," : "",
					mm->fg.flags.pyFaceViewIsAtRest ? "pyFace," : "",
					!(	mm->fg.flags.posViewIsAtRest ||
						mm->fg.flags.rotViewIsAtRest ||
						mm->fg.flags.pyFaceViewIsAtRest) ?
						"none" : "");
			
			pd.mm = mm;
			pd.msg.msgType = MM_MSG_FG_VIEW_STATUS_CHANGED;
			pd.msg.userPointer = mm->userPointer;
			pd.msg.fg.viewStatusChanged.flags.posIsAtRest = mm->fg.flags.posViewIsAtRest;
			pd.msg.fg.viewStatusChanged.flags.rotIsAtRest= mm->fg.flags.rotViewIsAtRest;
			pd.msg.fg.viewStatusChanged.flags.pyFaceIsAtRest = mm->fg.flags.pyFaceViewIsAtRest;
		
			mm->msgHandler(&pd.msg);
		}
		PERFINFO_AUTO_STOP();
	}
}

static void mmAdjustRepredictOffsetFG(	MovementManager* mm,
										MovementOutput* o,
										const Vec3 repredictOffset)
{
	Vec3 curOffset;
	
	mmLog(	mm,
			NULL,
			"[fg.repredict] Repredicted c%u off by (%1.2f, %1.2f, %1.2f) (%1.2fft)",
			o->pc.client,
			vecParamsXYZ(repredictOffset),
			lengthVec3(repredictOffset));
	
	scaleVec3(	mm->fg.repredict.offset,
				mm->fg.repredict.secondsRemaining / SECONDS_TO_INTERP_PREDICT_ERRORS,
				curOffset);

	addVec3(repredictOffset,
			curOffset,
			mm->fg.repredict.offset);

	if(!vec3IsZero(mm->fg.repredict.offset)){
		mmLog(	mm,
				NULL,
				"[fg.repredict] Adding to current offset"
					" (%1.2f, %1.2f, %1.2f)"
					" to get (%1.2f, %1.2f, %1.2f)",
				vecParamsXYZ(curOffset),
				vecParamsXYZ(mm->fg.repredict.offset));

		mm->fg.repredict.secondsRemaining = SECONDS_TO_INTERP_PREDICT_ERRORS;
	}
}

static S32 mmIsOutputInListFG(	const MovementThreadData* td,
								const MovementOutput* o)
{
	const MovementOutput* oCheck;
	
	for(oCheck = td->toFG.outputList.head;
		oCheck;
		oCheck = (oCheck == td->toFG.outputList.tail) ? NULL : oCheck->next)
	{
		if(oCheck == o){
			return 1;
		}
	}
	
	return 0;
}

static void mmVerifyRepredictsFromBG(	MovementManager* mm,
										MovementThreadData* td)
{
	const MovementOutput* o;
	
	if(mm->fg.flags.destroyed){
		return;
	}
	
	o = td->toFG.repredicts[0]->o;
	
	assert(o);
	assert(mmIsOutputInListFG(td, o));
	
	#if MM_VERIFY_REPREDICTS
		assert(eaSize(&td->toFG.repredicts) == eaiUSize(&td->toFG.repredictPCs));
	#endif
	
	EARRAY_CONST_FOREACH_BEGIN(td->toFG.repredicts, i, isize);
	{
		assert(td->toFG.repredicts[i]->o == o);
		
		if(o == td->toFG.outputList.tail){
			assert(i == isize - 1);
		}else{
			o = o->next;
		}
	}
	EARRAY_FOREACH_END;
}

static void mmHandleRepredictsFromBG(	MovementManager* mm,
										MovementThreadData* td)
{
	S32 notInterped = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	assert(eaSize(&td->toFG.repredicts));
	
	mmLog(	mm,
			NULL,
			"[fg.repredict] Receiving %u repredicts c%u/s%u - c%u/s%u.",
			eaSize(&td->toFG.repredicts),
			td->toFG.repredicts[0]->o->pc.client,
			td->toFG.repredicts[0]->o->pc.server,
			td->toFG.repredicts[eaSize(&td->toFG.repredicts) - 1]->o->pc.client,
			td->toFG.repredicts[eaSize(&td->toFG.repredicts) - 1]->o->pc.server);
	
	// 4/30/2012 AM Removal to because of earlier assert also being removed, seems to cause no downsides
	//mmVerifyRepredictsFromBG(mm, td);
	
	mm->fg.flagsMutable.needsAnimReplayLocal = 1;
	mm->fg.flagsMutable.needsAnimReplayLocalNet = 1;
	
	EARRAY_CONST_FOREACH_BEGIN(td->toFG.repredicts, i, isize);
	{
		MovementOutputRepredict*	mor = td->toFG.repredicts[i];
		MovementOutput*				o = mor->o;
		
		o->flagsMutable.needsAnimReplayLocal = 1;
		
		if(mm->fg.flags.destroyed){
			mmOutputRepredictDestroy(&td->toFG.repredictsMutable[i]);
			continue;
		}
			
		if(TRUE_THEN_RESET(mor->flags.disableRepredictOffset)){
			mmLog(	mm,
					NULL,
					"[fg.repredict] Clearing repredict offset because of output c%u/s%u.",
					o->pc.client,
					o->pc.server);

			mm->fg.repredict.secondsRemaining = 0;
			zeroVec3(mm->fg.repredict.offset);

			o->flagsMutable.notInterped = 1;
			notInterped = 1;
		}
		
		if(mor->flags.notInterped){
			o->flagsMutable.notInterped = 1;
			notInterped = 1;
		}

		if(	i == isize - 1 &&
			!notInterped)
		{
			Vec3 repredictOffset;
			
			if(MMLOG_IS_ENABLED(mm)){
				mmLogOutputRepredictFG(mm, mor);
			}

			subVec3(o->data.pos,
					mor->data.pos,
					repredictOffset);

			mmAdjustRepredictOffsetFG(mm, o, repredictOffset);
		}

		// Copy the data from BG (swap anim bit lists so BG can reuse the old one).

		{
			MovementOutputData dataTemp = o->data;

			if(TRUE_THEN_RESET(mor->flags.noAnimBitUpdate)){
				MovementAnimValues morAnim = mor->data.anim;
				
				o->dataMutable = mor->data;
				mor->dataMutable = dataTemp;

				o->dataMutable.anim = dataTemp.anim;
				mor->dataMutable.anim = morAnim;
			}else{
				o->dataMutable = mor->data;
				mor->dataMutable = dataTemp;
			}
		}
		
		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.hasRepredicts = 1;
		
		if(!td->toBG.repredict){
			td->toBG.repredict = callocStruct(MovementThreadDataToBGRepredict);
		}
		
		eaPush(&td->toBG.repredict->repredictsMutable, mor);
	}
	EARRAY_FOREACH_END;
	
	eaSetSize(&td->toFG.repredictsMutable, 0);
	
	#if MM_VERIFY_REPREDICTS
		eaiSetSize(&td->toFG.repredictPCs, 0);
	#endif
	
	PERFINFO_AUTO_STOP();
}

static void mmSendMsgCollRadiusChangedFG(MovementManager* mm){
	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_COLL_RADIUS_CHANGED;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.fg.collRadiusChanged.radius = mm->fg.bodyRadius;
	
		mm->msgHandler(&pd.msg);
	}
}

static void mmSendMsgUserThreadDataUpdatedFG(MovementManager* mm){
	if(mm->msgHandler){
		PERFINFO_AUTO_START_FUNC();
		{
			MovementManagerMsgPrivateData pd = {0};
			
			pd.mm = mm;
			pd.msg.msgType = MM_MSG_FG_UPDATE_FROM_BG;
			pd.msg.userPointer = mm->userPointer;
			pd.msg.fg.updateFromBG.threadData = mm->userThreadData[MM_FG_SLOT];
		
			mm->msgHandler(&pd.msg);
		}
		PERFINFO_AUTO_STOP();
	}
}

static void mmSendMsgAfterUpdatedUserThreadDataToBG(MovementManager* mm){
	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_AFTER_SEND_UPDATE_TO_BG;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.fg.afterSendUpdateToBG.threadData = mm->userThreadData[MM_FG_SLOT];
	
		mm->msgHandler(&pd.msg);
	}
}

static void mmApplyRepredictOffsetFG(	MovementManager* mm,
										Vec3 posOut)
{
	if(mm->fg.repredict.secondsRemaining <= 0.f){
		copyVec3(mm->fg.pos, posOut);
	}
	else if(mgState.fg.frame.cur.prevSecondsDelta >= mm->fg.repredict.secondsRemaining){
		mm->fg.repredict.secondsRemaining = 0.f;

		copyVec3(mm->fg.pos, posOut);

		zeroVec3(mm->fg.repredict.curOffset);
	}else{
		mm->fg.repredict.secondsRemaining -= mgState.fg.frame.cur.prevSecondsDelta;

		scaleVec3(	mm->fg.repredict.offset,
					mm->fg.repredict.secondsRemaining / SECONDS_TO_INTERP_PREDICT_ERRORS,
					mm->fg.repredict.curOffset);

		if(!mgState.fg.flags.predictDisabled){
			mmLog(	mm,
					NULL,
					"[fg.repredict] Adding repredict offset"
						" (%1.2f, %1.2f, %1.2f)"
						" [%8.8x, %8.8x, %8.8x]",
					vecParamsXYZ(mm->fg.repredict.curOffset),
					vecParamsXYZ((S32*)mm->fg.repredict.curOffset));
			
			addVec3(mm->fg.pos,
					mm->fg.repredict.curOffset,
					posOut);
		}else{
			copyVec3(	mm->fg.pos,
						posOut);
		}
	}
}

void mmAllTriggerSend(const MovementTrigger* t){
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager* mm = mgState.fg.managers[i];
		
		mmLog(	mm,
				NULL,
				"[resource] Sending trigger %s 0x%8.8x (%swaiting for trigger)",
				t->flags.isEntityID ? "entity" : "event",
				t->triggerID,
				mm->fg.flags.mmrWaitingForTrigger ? "" : "not ");
		
		if(TRUE_THEN_RESET(mm->fg.flagsMutable.mmrWaitingForTrigger)){
			mmResourcesSendMsgTrigger(mm, t);
		}
	}
	EARRAY_FOREACH_END;
}

static void mmSendMsgFirstSetViewFG(MovementManager* mm){
	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_FIRST_VIEW_SET;
		pd.msg.userPointer = mm->userPointer;
	
		mm->msgHandler(&pd.msg);
	}
}

static void mmLogSetCurrentViewBeforeFG(MovementManager* mm,
										MovementThreadData* td)
{
	const MovementOutputList*	ol = mm->fg.flags.destroyed ? NULL : &td->toFG.outputList;
	const MovementNetOutput*	no;
	const MovementOutput*		o;
	
	mmLog(	mm,
			NULL,
			"[fg.view] Before setting FG view, frame delta %1.3fs.\n"
			"pos (%1.3f, %1.3f, %1.3f)\n"
			"rot (%1.3f, %1.3f, %1.3f, %1.3f)\n"
			"pyFace (%1.3f, %1.3f)\n"
			"local outputs c[%u-%u]\n"
			"local outputs s[%u-%u]\n"
			"net outputs s[%u-%u]).\n"
			"LocalView: ceiling c%u s%u interp %1.3f\n"
			"NetView: s%u-%u interp %1.3f\n"
			"NetBuffer: latest %u, offset from end normal = %1.3f, fast = %1.3f\n"
			"AtRest: %s%s%s\n"
			"Flags: %s%s"
			,
			mgState.fg.frame.cur.secondsDelta,
			vecParamsXYZ(mm->fg.pos),
			quatParamsXYZW(mm->fg.rot),
			vecParamsXY(mm->fg.pyFace),
			SAFE_MEMBER2(ol, head, pc.client),
			SAFE_MEMBER2(ol, tail, pc.client),
			SAFE_MEMBER2(ol, head, pc.server),
			SAFE_MEMBER2(ol, tail, pc.server),
			SAFE_MEMBER(mm->fg.net.outputList.head, pc.server),
			SAFE_MEMBER(mm->fg.net.outputList.tail, pc.server),
			mgState.fg.localView.pcCeiling,
			mgState.fg.localView.spcCeiling,
			mgState.fg.localView.outputInterp.forward,
			mgState.fg.netView.spcFloor,
			mgState.fg.netView.spcCeiling,
			mgState.fg.netView.spcInterpFloorToCeiling,
			mgState.fg.netReceive.cur.pc.server,
			mgState.fg.netView.spcOffsetFromEnd.normal,
			mgState.fg.netView.spcOffsetFromEnd.fast,
			mm->fg.flags.posViewIsAtRest ? "pos, " : "",
			mm->fg.flags.rotViewIsAtRest ? "rot, " : "",
			mm->fg.flags.pyFaceViewIsAtRest ? "face" : "",
			mm->fg.flags.posNeedsForcedSetAck ? "posNeedsForcedSetAck, " : "",
			mm->fg.flags.rotNeedsForcedSetAck ? "rotNeedsForcedSetAck, " : "");

	if(mgState.debug.flags.logNetOutputs3D){
		for(no = mm->fg.net.outputList.head; no; no = no->next){
			const MovementNetOutput* noNext = no->next;
		
			if(noNext){
				mmLogSegment(	mm,
								NULL,
								"fg.netOutputs",
								0xffff0000,
								no->data.pos,
								noNext->data.pos);
			}
		}
	}

	if(	mgState.debug.flags.logOutputs3D &&
		ol)
	{
		for(o = ol->head; o; o = (o == ol->tail ? NULL : o->next)){
			const MovementOutput* oNext = o->next;
			
			if(oNext){
				mmLogSegment(	mm,
								NULL,
								"fg.outputs",
								0xff00ff00,
								o->data.pos,
								oNext->data.pos);
			}
		}
	}
}

static void mmFindLocalOutputsFG(	MovementManager* mm,
									MovementThreadData* td,
									F32* outputInterpInverseOut,
									const MovementOutput** oNextOut,
									const MovementOutput** oPrevOut)
{
	const MovementOutput* oTail = td->toFG.outputList.tail;

	if(	mgState.flags.isServer ||
		oTail->flags.isPredicted)
	{
		*outputInterpInverseOut = mgState.fg.localView.outputInterp.inverse;
		*oNextOut = oTail;
		*oPrevOut = oTail;

		// Check if prev exists and is actually the previous output on client.

		if(	!mgState.flags.isServer &&
			!oTail->flags.notInterped &&
			oTail->prev &&
			oTail != td->toFG.outputList.head &&
			oTail->prev->flags.isPredicted &&
			oTail->prev->pc.client == mgState.fg.localView.pcCeiling - MM_PROCESS_COUNTS_PER_STEP)
		{
			*oPrevOut = oTail->prev;
		}
	}
}

static void mmLogCurrentViewFG(	MovementManager* mm,
								const Vec3 pos)
{
	Mat3 mat;
	Vec3 dirFace;
	
	quatToMat(mm->fg.rot, mat);
	createMat3_2_YP(dirFace, mm->fg.pyFace);

	FOR_BEGIN(i, 3);
	{
		U32 argb = 0xff000000 | (0xff << ((2 - i) * 8));
		
		mmLogSegmentOffset(	mm,
							NULL,
							"fg.pos",
							argb,
							pos,
							mat[i]);
	}
	FOR_END;

	{
		Vec3 posTemp;
		
		addVec3(pos, mat[1], posTemp);
		
		mmLogSegmentOffset(	mm,
							NULL,
							"fg.pos",
							0xffffff00,
							posTemp,
							dirFace);
	}
}

static void mmSetCurrentViewFG(	MovementManager* mm,
								MovementThreadData* td)
{
	#define FULLY_LOCAL 0.f
	#define FULLY_NET	1.f

	const MovementOutput*		oNext;
	const MovementOutput*		oPrev;
	const MovementNetOutput*	noNext;
	const MovementNetOutput*	noPrev;
	F32							outputInterpInverse = 0;
	F32							netOutputInterpInverse = 0;
	F32							interpLocalToNet;
	Vec3						posToSet;
	Vec3						posCur;
	Quat						rotCur;
	Vec2						pyFaceCur;
	S32							hasOutput;
	S32							posViewIsCurrent;
	S32							rotViewIsCurrent;
	S32							pyFaceViewIsCurrent;
	
	if(mgState.flags.isServer){
		posViewIsCurrent = mmDataViewIsCurrentFG(mm, posViewIsAtRest);
		rotViewIsCurrent = mmDataViewIsCurrentFG(mm, rotViewIsAtRest);
		pyFaceViewIsCurrent = mmDataViewIsCurrentFG(mm, pyFaceViewIsAtRest);
	}else{
		posViewIsCurrent = 0;
		rotViewIsCurrent = 0;
		pyFaceViewIsCurrent = 0;
	}

	if(PERFINFO_RUN_CONDITIONS){
		PERFINFO_AUTO_START_FUNC();

		if(!posViewIsCurrent){
			if(!rotViewIsCurrent){
				if(!pyFaceViewIsCurrent){
					PERFINFO_AUTO_START("pos+rot+face", 1);
				}else{
					PERFINFO_AUTO_START("pos+rot", 1);
				}
			}
			else if(!pyFaceViewIsCurrent){
				PERFINFO_AUTO_START("pos+face", 1);
			}else{
				PERFINFO_AUTO_START("pos", 1);
			}
		}
		else if(!rotViewIsCurrent){
			if(!pyFaceViewIsCurrent){
				PERFINFO_AUTO_START("rot+face", 1);
			}else{
				PERFINFO_AUTO_START("rot", 1);
			}
		}else{
			PERFINFO_AUTO_START("face", 1);
		}
	}
	
	if(MMLOG_IS_ENABLED(mm)){
		mmLogSetCurrentViewBeforeFG(mm, td);
	}

	mm->fg.frameWhenViewSet = mgState.frameCount;

	oNext = NULL;
	oPrev = NULL;
	noNext = NULL;
	noPrev = NULL;
	hasOutput = 0;

	interpLocalToNet = mgState.flags.isServer ?
							FULLY_LOCAL :
							FULLY_NET;
	
	// Find predicted view.

	if(	td->toFG.outputList.tail &&
		!mgState.fg.flags.predictDisabled &&
		!mm->fg.flags.destroyed)
	{
		mmFindLocalOutputsFG(	mm,
								td,
								&outputInterpInverse,
								&oNext,
								&oPrev);
	}

	if(!mgState.flags.isServer){
		mmFindNetOutputsFG(	&mm->fg.net.outputList,
							&noPrev,
							&noNext,
							&netOutputInterpInverse);
	}

	PERFINFO_AUTO_START("interp", 1);
	{
		// Calculate the current position on server, or predicted position on client.
	
		if(	oNext
			&&
			(	mgState.flags.isServer
				||
				oNext->flags.isPredicted &&
				!mgState.fg.flags.predictDisabled))
		{
			hasOutput = 1;

			if(!mgState.flags.isServer){
				// Client interps predicted position back to unpredicted.
				
				F32 diff =	mgState.fg.localView.pcCeiling
							-
							mm->fg.predict.pcStart
							-
							outputInterpInverse *
							MM_PROCESS_COUNTS_PER_STEP;

				if(diff <= 0){
					interpLocalToNet = FULLY_LOCAL;
				}
				else if(diff < MM_PROCESS_COUNTS_PER_SECOND){
					interpLocalToNet =	(F32)diff /
										(F32)MM_PROCESS_COUNTS_PER_SECOND;
				}else{
					interpLocalToNet = FULLY_NET;
				}
			}

			// Get the predicted data if necessary.

			if(!mgState.flags.isServer){
				if(interpLocalToNet != FULLY_NET){
					mmInterpOutputsFG(	mm,
										"LOCAL",
										oPrev->data.pos,
										oPrev->data.rot,
										oPrev->data.pyFace,
										mgState.flags.isServer ?
											oPrev->pc.server :
											oPrev->pc.client,
										oNext->data.pos,
										oNext->data.rot,
										oNext->data.pyFace,
										mgState.flags.isServer ?
											oNext->pc.server :
											oNext->pc.client,
										oNext->flags.notInterped,
										outputInterpInverse,
										posViewIsCurrent ? NULL : posCur,
										rotViewIsCurrent ? NULL : rotCur,
										pyFaceViewIsCurrent ? NULL : pyFaceCur);
				}
			}else{
				if(!posViewIsCurrent){
					if(mm->fg.flags.posNeedsForcedSetAck){
						copyVec3(mm->fg.pos, posCur);
					}else{
						copyVec3(oNext->data.pos, posCur);
					}
				}
				
				if(!rotViewIsCurrent){
					if(mm->fg.flags.rotNeedsForcedSetAck){
						copyQuat(mm->fg.rot, rotCur);
					}else{
						copyQuat(oNext->data.rot, rotCur);
					}
				}
				
				if(!pyFaceViewIsCurrent){
					if(mm->fg.flags.rotNeedsForcedSetAck){
						copyVec2(mm->fg.pyFace, pyFaceCur);
					}else{
						copyVec2(oNext->data.pyFace, pyFaceCur);
					}
				}
			}
		}

		// Calculate the net position on client, if not fully predicted.

		if(	noNext &&
			interpLocalToNet != FULLY_LOCAL)
		{
			Vec3 posNet;
			Quat rotNet;
			Vec2 pyFaceNet;
			
			hasOutput = 1;
			
			// Interp the net position first.

			mmInterpOutputsFG(	mm,
								"NET",
								noPrev->data.pos,
								noPrev->data.rot,
								noPrev->data.pyFace,
								noPrev->pc.server,
								noNext->data.pos,
								noNext->data.rot,
								noNext->data.pyFace,
								noNext->pc.server,
								noNext->flags.notInterped,
								netOutputInterpInverse,
								posViewIsCurrent ? NULL : posNet,
								rotViewIsCurrent ? NULL : rotNet,
								pyFaceViewIsCurrent ? NULL : pyFaceNet);

			if(interpLocalToNet == FULLY_NET){
				copyVec3(posNet, posCur);
				copyQuat(rotNet, rotCur);
				copyVec2(pyFaceNet, pyFaceCur);
			}else{
				Vec3 diffLocalToNet;
				Vec3 origPos;

				if(!posViewIsCurrent){
					copyVec3(posCur, origPos);

					subVec3(posNet, posCur, diffLocalToNet);
					scaleVec3(diffLocalToNet, interpLocalToNet, diffLocalToNet);
					addVec3(posCur, diffLocalToNet, posCur);
				
					if(MMLOG_IS_ENABLED(mm)){
						mmLogSegment(	mm,
										NULL,
										"fg.interp",
										0xffff0000,
										posNet,
										posCur);

						mmLogSegment(	mm,
										NULL,
										"fg.interp",
										0xff00ff00,
										posCur,
										origPos);
					}
				}
				
				if(!rotViewIsCurrent){
					quatInterp(	interpLocalToNet,
								rotCur,
								rotNet,
								rotCur);
				}
				
				if(!pyFaceViewIsCurrent){							
					interpPY(	interpLocalToNet,
								pyFaceCur,
								pyFaceNet,
								pyFaceCur);
				}
			}
		}

		// Apply the current repredict offset.

		if(hasOutput){
			if(!posViewIsCurrent){
				mmLog(	mm,
						NULL,
						"[fg.view] Setting pos (%1.3f, %1.3f, %1.3f) to (%1.3f, %1.3f, %1.3f).",
						vecParamsXYZ(mm->fg.pos),
						vecParamsXYZ(posCur));

				MM_CHECK_DYNPOS_DEVONLY(posCur);

				copyVec3(posCur, mm->fg.posMutable);

				if(!mgState.flags.isServer){
					mmApplyRepredictOffsetFG(mm, posToSet);
				}else{
					copyVec3(mm->fg.pos, posToSet);
				}
			}
			
			if(!rotViewIsCurrent){
				copyQuat(rotCur, mm->fg.rotMutable);
			}
			
			if(!pyFaceViewIsCurrent){
				copyVec2(pyFaceCur, mm->fg.pyFaceMutable);
			}
		}
		
		// Log some stuff.

		if(MMLOG_IS_ENABLED(mm)){
			mmLogCurrentViewFG(	mm,
								hasOutput && !posViewIsCurrent ?
									posToSet :
									mm->fg.pos);
		}
	}
	PERFINFO_AUTO_STOP();

	if(!hasOutput){
		mmLog(	mm,
				NULL,
				"[fg.view] No local c(%u - %u) s(%u - %u) or net s(%u - %u) outputs found.",
				SAFE_MEMBER(td->toFG.outputList.head, pc.client),
				SAFE_MEMBER(td->toFG.outputList.tail, pc.client),
				SAFE_MEMBER(td->toFG.outputList.head, pc.server),
				SAFE_MEMBER(td->toFG.outputList.tail, pc.server),
				SAFE_MEMBER(mm->fg.net.outputList.head, pc.server),
				SAFE_MEMBER(mm->fg.net.outputList.tail, pc.server));
	}
	else if(mm->msgHandler){
		PERFINFO_AUTO_START("sendMsg:SET_VIEW", 1);
		{
			MovementManagerMsgPrivateData	pd = {0};
			Vec3							offset;

			pd.msg.msgType = MM_MSG_FG_SET_VIEW;
			pd.msg.userPointer = mm->userPointer;
			
#if GAMECLIENT
			// on the gameClient, we'd like to know if we have an entity that hasn't started with it's net view movements
			// and is remaining stationary. We are using this for projectiles so we only draw them when they first start moving.
			
			// this checks if we are at the head of the list and our netView has not caught up with the netOutput's timestamp
			if (noPrev && !noPrev->prev && mgState.fg.netView.spcCeiling < noPrev->pc.server){
				pd.msg.fg.setView.netViewInitUnmoving = true;
			} else {
				pd.msg.fg.setView.netViewInitUnmoving = false;
			}
#endif		

			if(!posViewIsCurrent){
				// this is a very lax assert, but it should fire before the one in dynNode, giving us better info.
				//MM_CHECK_DYNPOS_DEVONLY(posToSet);
				CHECK_DYNPOS(posToSet);

				pd.msg.fg.setView.vec3Pos = posToSet;
			}
			
			if(!rotViewIsCurrent){
				pd.msg.fg.setView.quatRot = mm->fg.rot;
			}
			
			if(!pyFaceViewIsCurrent){
				pd.msg.fg.setView.vec2FacePitchYaw = mm->fg.pyFace;
			}
			
			if(mm->fg.flags.hasOffsetInstances){
				F32		yOffset = mm->fg.offsetInstances[0]->rotationOffset;
				Mat3	mat;
				
				quatToMat(	mm->fg.rot,
							mat);

				scaleVec3(	mat[1],
							-yOffset,
							offset);
				
				offset[1] += yOffset;

				pd.msg.fg.setView.vec3Offset = offset;
			}
			
			mm->msgHandler(&pd.msg);
		}
		PERFINFO_AUTO_STOP();
		
		if(FALSE_THEN_SET(mm->fg.flagsMutable.hasSetView)){
			mmSendMsgFirstSetViewFG(mm);
		}
	}

	if(MMLOG_IS_ENABLED(mm)){
		mmLog(	mm,
				NULL,
				"[fg.view] Current times: %u (+%u), next: %u, offset: %u, nextServer: %u",
				mgState.fg.frame.cur.pcStart,
				mgState.fg.frame.cur.stepCount,
				mgState.fg.frame.next.pcStart,
				mgState.fg.netReceive.cur.offset.clientToServerSync,
				mgState.fg.frame.next.pcStart +
					mgState.fg.netReceive.cur.offset.clientToServerSync);
					
		mmLog(	mm,
				NULL,
				"[fg.view] netSendProcessCount cur = %u, prev = %u",
				mgState.fg.frame.cur.pcNetSend,
				mgState.fg.frame.prev.pcNetSend);
	}

	if(PERFINFO_RUN_CONDITIONS){
		PERFINFO_AUTO_STOP();// (pos,rot,face) permutation.
		PERFINFO_AUTO_STOP();// FUNC.
	}

	#undef FULLY_LOCAL
	#undef FULLY_NET
}

void mmUpdateCurrentViewFG(MovementManager* mm){
	if(	!mm
		||
		mm->fg.flags.posViewIsAtRest &&
		mm->fg.flags.rotViewIsAtRest &&
		mm->fg.flags.pyFaceViewIsAtRest &&
		subS32(mm->fg.frameWhenViewSet, mm->fg.frameWhenViewChanged) >= 0
		||
		(	!mm->fg.flags.posViewIsAtRest ||
			!mm->fg.flags.rotViewIsAtRest ||
			!mm->fg.flags.pyFaceViewIsAtRest) &&
		mm->fg.frameWhenViewSet == mgState.frameCount)
	{
		return;
	}
	
	mmLog(	mm,
			NULL,
			"[fg.view] Setting view from %s (AtRest: %s%s%s)",
			__FUNCTION__,
			mm->fg.flags.posViewIsAtRest ? "pos, " : "",
			mm->fg.flags.rotViewIsAtRest ? "rot, " : "",
			mm->fg.flags.pyFaceViewIsAtRest ? "pyFace, " : "");
	
	mmSetCurrentViewFG(mm, MM_THREADDATA_FG(mm));
}

static void mmSendMsgsUpdatedToFG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester*				mr = mm->fg.requesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_FG(mr);
		MovementRequesterMsgPrivateData	pd;

		if(!TRUE_THEN_RESET(mrtd->toFG.flagsMutable.hasUserToFG)){
			continue;
		}
		
		if(mr->fg.flags.destroyed){
			continue;
		}
		
		mrLog(	mr,
				NULL,
				"Sending msg UPDATED_TOFG.");

		MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_UPDATED_TOFG);
		{
			mmRequesterMsgInitFG(&pd, NULL, mr, MR_MSG_FG_UPDATED_TOFG);

			pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_FG_SLOT);

			mmRequesterMsgSend(&pd);
		}
		MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_UPDATED_TOFG);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

static void mmHandleRequesterUpdatesFromBG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester*				mr = mm->fg.requesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_FG(mr);

		// Check if the BG destroyed me.

		if(TRUE_THEN_RESET(mrtd->toFG.flagsMutable.destroyed)){
			mr->fg.flagsMutable.destroyedFromBG = 1;
			
			if(	mgState.flags.isServer ||
				!mr->fg.netHandle)
			{
				// Server or unsynced requester is removed from BG immediately.
				
				mmRequesterDestroyFG(mr);
			}
		}

		// Check if removed from the BG list.

		if(TRUE_THEN_RESET(mrtd->toFG.flagsMutable.removedFromList)){
			assert(mr->fg.flags.sentRemoveToBG);
			
			ASSERT_TRUE_AND_RESET(mr->fg.flagsMutable.inListBG);

			assert(mr->fg.flags.destroyedFromBG);
			assert(mr->fg.flags.destroyed);
			assert(!mr->bg.ownedDataClassBits);
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP_FUNC();
}

static void mmHandleFinishedInputStepsFG(	MovementManager* mm,
											MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(td->toFG.finishedInputSteps, i, isize);
	{
		MovementInputStep* miStep = td->toFG.finishedInputSteps[i];

		assert(miStep->bg.flags.finished);
		assert(!miStep->bg.flags.inRepredict);

		ASSERT_FALSE_AND_SET(miStep->fg.flags.removedFromBG);
		
		if(!miStep->mciStep){
			// mm was detached from client.
			
			mmInputStepDestroy(&miStep);
		}
	}
	EARRAY_FOREACH_END;

	eaSetSize(&td->toFG.finishedInputStepsMutable, 0);

	PERFINFO_AUTO_STOP_FUNC();
}

static void mmClientHandleFinishedInputStepsFG(MovementClient* mc){
	MovementClientInputStep*	mciStep;
	MovementClientInputStep*	mciStepLastFinished = NULL;
	U32							stepCountToRemove = 0;
	
	for(mciStep = mc->mciStepList.head;
		mciStep;
		mciStep = mciStep->next)
	{
		S32 allRemovedFromBG = 1;
		
		if(	!mgState.flags.isServer &&
			!mciStep->flags.sentToServer)
		{
			break;
		}
		
		EARRAY_CONST_FOREACH_BEGIN(mciStep->miSteps, j, jsize);
		{
			MovementInputStep* miStep = mciStep->miSteps[j];
			
			if(!miStep->fg.flags.removedFromBG){
				allRemovedFromBG = 0;
				continue;
			}
			
			// Reclaim the control step.
			
			miStep->mciStep = NULL;

			mmInputStepReclaim(mc, miStep);
			
			// Remove from array.
			
			eaRemove(&mciStep->miStepsMutable, j);
			j--;
			jsize--;
		}
		EARRAY_FOREACH_END;
		
		if(!allRemovedFromBG){
			break;
		}
		
		mciStepLastFinished = mciStep;
		stepCountToRemove++;
	}
	
	if(mciStepLastFinished){
		MovementClientInputStep *mciFinishStep = mciStepLastFinished->next;

		// Add to the available list and remove from the main list.
		while(mc->mciStepList.head != mciFinishStep) {

			// Move head forward on main list
			mciStep = mc->mciStepList.head;
			mc->mciStepListMutable.head = mc->mciStepList.head->next;

			// Only put on available list up to some limit
			if (mc->available.mciStepList.count >= MAX_AVAILABLE_MCI_STEPS) {
				assert(!eaSize(&mciStep->miSteps));
				eaDestroy(&mciStep->miStepsMutable);
				SAFE_FREE(mciStep);
				continue;
			}

			// Put removed step on available list
			if(!mc->available.mciStepList.head){
				assert(!mc->available.mciStepList.tail);
				mc->available.mciStepListMutable.head = mciStep;
			}else{
				assert(mc->available.mciStepList.tail);
				mc->available.mciStepList.tail->next = mciStep;
			}
			mciStep->next = NULL;
			mciStep->prev = mc->available.mciStepList.tail;
			mc->available.mciStepListMutable.tail = mciStep;

			++mc->available.mciStepListMutable.count;
		}
		
		assert(stepCountToRemove <= mc->mciStepList.count);
		mc->mciStepListMutable.count -= stepCountToRemove;

		if(mc->mciStepList.head){
			mc->mciStepList.head->prev = NULL;
		}else{
			mc->mciStepListMutable.tail = NULL;
		}
	}
}	

static void mmHandleUpdatesFromBG(	MovementManager* mm,
									MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();

	// Check if predicton was enabled.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.startPredict)){
		mm->fg.predict.pcStart = td->toFG.predict->pcStart;
	}

	// Handle new requesters that were created in BG.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.hasNewRequesters)){
		mmHandleNewRequestersFromBG(mm, td);
	}

	// Handle repredicts.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.hasRepredicts)){
		mmHandleRepredictsFromBG(mm, td);
	}
	
	// Handle resource updates.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.mmrHasUpdate)){
		mmHandleResourceUpdatesFromBG(mm, td);
	}

	// Handle requester updates.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.mrHasUpdate)){
		mmHandleRequesterUpdatesFromBG(mm);
	}

	// Handle finished input steps.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.hasFinishedInputSteps)){
		mmHandleFinishedInputStepsFG(mm, td);
	}
	
	// Check for an instantSet ack.
	
	if(TRUE_THEN_RESET(td->toFG.flagsMutable.hasForcedSetCount)){
		if(	mm->fg.flags.posNeedsForcedSetAck &&
			subS32(td->toFG.forcedSetCount, mm->fg.forcedSetCount.pos) >= 0)
		{
			mm->fg.flagsMutable.posNeedsForcedSetAck = 0;
		}

		if(	mm->fg.flags.rotNeedsForcedSetAck &&
			subS32(td->toFG.forcedSetCount, mm->fg.forcedSetCount.rot) >= 0)
		{
			mm->fg.flagsMutable.rotNeedsForcedSetAck = 0;
		}
	}

	// Flag if any BG processing happened.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.didProcess)){
		mm->fg.flagsMutable.didProcessInBG = 1;
	}

	if (TRUE_THEN_RESET(td->toFG.flagsMutable.capsuleOrientationMethodChanged))
	{
		mm->fg.flagsMutable.capsuleOrientationUseRotation = td->toFG.flags.capsuleOrientationUseRotation;
	}
	
	// Notify requesters if a ToFG is available.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.mrHasUserToFG)){
		mmSendMsgsUpdatedToFG(mm);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void mmDestroyInternal(MovementManager* mm);

static void mmAllHandleManagerDestroyQueueFG(void){
	if(TRUE_THEN_RESET(mgState.fg.flagsMutable.mmNeedsDestroy)){
		int startSize;
		
		PERFINFO_AUTO_START_FUNC();

		startSize = eaSize(&mgState.fg.managersToDestroy);

		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managersToDestroy, i, isize);
		{
			MovementManager* mm = mgState.fg.managersToDestroy[i];

			assert(mm->fg.flags.destroyed);
			assert(mm->fg.flags.destroyedFromBG);
			
			if(mm->fg.bodyInstances){
				// Still waiting for body instances to be destroyed.

				mmDestroyAllResourcesFG(mm);

				mgState.fg.flagsMutable.mmNeedsDestroy = 1;
			}else{
				if(eaFindAndRemove(&mgState.fg.managersMutable, mm) < 0){
					assert(0);
				}

				mgState.fg.managersToDestroyMutable[i] = NULL;

				mmDestroyInternal(mm);
			}
		}
		EARRAY_FOREACH_END;

		assert(startSize == eaSize(&mgState.fg.managersToDestroy));

		if(mgState.fg.flags.mmNeedsDestroy){
			U32 size = 0;

			EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managersToDestroy, i, isize);
			{
				MovementManager* mm = mgState.fg.managersToDestroy[i];

				if(mm){
					mgState.fg.managersToDestroyMutable[size++] = mm;
				}
			}
			EARRAY_FOREACH_END;

			assert(size);
			eaSetSize(&mgState.fg.managersToDestroyMutable, size);
		}
		else if(eaCapacity(&mgState.fg.managersToDestroy) > 100){
			eaDestroy(&mgState.fg.managersToDestroyMutable);
		}else{
			eaClearFast(&mgState.fg.managersToDestroyMutable);
		}

		PERFINFO_AUTO_STOP_FUNC();
	}
}

static void mmLogAfterSimWakesEndFG(MovementManager* mm){
	MovementThreadData*				td = MM_THREADDATA_FG(mm);
	const MovementManagerFGView*	v = mm->fg.view;

	#define FLAG(x) (mm->fg.flags.x ? #x", " : "")
	
	mmLog(	mm,
			NULL,
			"[fg.afterSimWakes] After sim wakes:\n"
			"Flags: "
			"%s" // posViewIsAtRest
			"%s" // rotViewIsAtRest
			"%s" // pyFaceViewIsAtRest
			"%s" // noCollision
			"%s" // net.flags.noCollision
			,
			FLAG(posViewIsAtRest),
			FLAG(rotViewIsAtRest),
			FLAG(pyFaceViewIsAtRest),
			FLAG(noCollision),
			mm->fg.net.flags.noCollision ? "net.noCollision" : ""
			);

	#undef FLAG

	if(mm->msgHandler){
		MovementManagerMsgPrivateData	pd = {0};
		MovementManagerMsgOut			out = {0};
		char							buffer[1000];

		buffer[0] = 0;
		
		pd.msg.msgType = MM_MSG_FG_GET_DEBUG_STRING_AFTER_SIMULATION_WAKES;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.out = &out;

		out.fg.getDebugString.buffer = buffer;
		out.fg.getDebugString.bufferLen = sizeof(buffer);
		
		mm->msgHandler(&pd.msg);
		
		if(buffer[0]){
			mmLog(	mm,
					NULL,
					"[fg.afterSimWakes] Owner state: %s",
					buffer);
		}
	}
	
	mmLogResource(mm, NULL, "Resources after sim wakes");
	
	mmLogResourceStatesFG(mm);

	if(v){
		if(v->flags.lastAnimIsLocal){
			mmLastAnimLogLocal(mm, &v->lastAnim, NULL, "View.LastAnim(local)");
		}else{
			mmLastAnimLogNet(mm, &v->lastAnim, NULL, "View.LastAnim(net)");
		}
	}

	mmLastAnimLogLocal(mm, &td->toFG.lastAnim, NULL, "toFG.LastAnim");
	mmLastAnimLogNet(mm, &mm->fg.net.lastAnim, NULL, "Net.LastAnim");

	// Log all the local outputs.

	if(mgState.debug.flags.logOutputsText){
		const MovementOutput* o;

		mmLog(mm, NULL, "[fg.outputs] Local outputs begin -------------------------");
			
		for(o = td->toFG.outputList.tail;
			o;
			o = (o == td->toFG.outputList.head ? NULL : o->prev))
		{
			Vec3 diff = {0};
			
			if(o != td->toFG.outputList.head){
				subVec3(o->data.pos,
						o->prev->data.pos,
						diff);
			}
			
			mmLog(	mm,
					NULL,
					"[fg.outputs] c%u/s%u: p(%1.3f, %1.3f, %1.3f) d(%1.3f, %1.3f, %1.3f)%s%s%s",
					o->pc.client,
					o->pc.server,
					vecParamsXYZ(o->data.pos),
					vecParamsXYZ(diff),
					o->flags.animViewedLocal ? ",VL" : "",
					o->flags.animViewedLocalNet ? ",VLN" : "",
					o->flags.needsAnimReplayLocal ? ",NARL" : "");
					
			if(!gConf.bNewAnimationSystem){
				if(eaiUSize(&o->data.anim.values)){
					mmLogAnimBitArray(	mm,
										o->data.anim.values,
										0,
										0,
										0,
										"fg.outputs",
										"Anim Bits");
				}
			}else{
				mmAnimValuesLogAll(mm, &o->data.anim, 0, "fg.outputs");
			}
		}

		mmLog(mm, NULL, "[fg.outputs] Local outputs end -------------------------");
	}

	// Log all the net  outputs.

	if(mgState.debug.flags.logNetOutputsText){
		const MovementNetOutput* no;
			
		mmLog(mm, NULL, "[fg.netOutputs] Net outputs begin -------------------------");

		for(no = mm->fg.net.outputList.tail;
			no;
			no = no->prev)
		{
			Vec3 diff = {0};
			
			if(no->prev){
				subVec3(no->data.pos,
						no->prev->data.pos,
						diff);
			}
			
			mmLog(	mm,
					NULL,
					"[fg.netOutputs] c%u/s%u: p(%1.3f, %1.3f, %1.3f) d(%1.3f, %1.3f, %1.3f)%s",
					no->pc.client,
					no->pc.server,
					vecParamsXYZ(no->data.pos),
					vecParamsXYZ(diff),
					no->flags.animBitsViewed ? ",V" : "");
					
			if(!gConf.bNewAnimationSystem){
				if(eaiUSize(&no->data.anim.values)){
					mmLogAnimBitArray(	mm,
										no->data.anim.values,
										1,
										0,
										0,
										"fg.netOutputs",
										"Anim Bits");
				}

				if(no->animBitCombo){
					mmLogAnimBitArray(	mm,
										no->animBitCombo->bits,
										1,
										0,
										0,
										"fg.netOutputs",
										"Anim Bit Combo");
				}
			}else{
				mmAnimValuesLogAll(mm, &no->data.anim, 1, "fg.netOutputs");
			}
		}
	
		mmLog(mm, NULL, "[fg.netOutputs] Net outputs end -------------------------");
	}
}

static void mmAllUpdateLoggingStateToOwners(void){
	if(	mgPublic.activeLogCount ||
		eaSize(&mgState.debug.mmsUserNotified))
	{
		static MovementManager** mmsCur = NULL;

		PERFINFO_AUTO_START_FUNC();

		mmCopyLocalLogList(&mmsCur);

		if(!eaSize(&mmsCur)){
			eaDestroy(&mmsCur);
		}

		EARRAY_CONST_FOREACH_BEGIN(mmsCur, i, isize);
		{
			MovementManager*				mm = mmsCur[i];
			MovementManagerMsgPrivateData	pd = {0};

			eaFindAndRemoveFast(&mgState.debug.mmsUserNotified, mm);

			pd.mm = mm;
			pd.msg.msgType = MM_MSG_FG_LOGGING_ENABLED;
			pd.msg.userPointer = mm->userPointer;

			mm->msgHandler(&pd.msg);
		}
		EARRAY_FOREACH_END;

		EARRAY_CONST_FOREACH_BEGIN(mgState.debug.mmsUserNotified, i, isize);
		{
			MovementManager*				mm = mgState.debug.mmsUserNotified[i];
			MovementManagerMsgPrivateData	pd = {0};

			pd.mm = mm;
			pd.msg.msgType = MM_MSG_FG_LOGGING_DISABLED;
			pd.msg.userPointer = mm->userPointer;

			mm->msgHandler(&pd.msg);
		}
		EARRAY_FOREACH_END;

		if(!eaSize(&mmsCur)){
			eaDestroy(&mgState.debug.mmsUserNotified);
		}else{
			eaCopy(&mgState.debug.mmsUserNotified, &mmsCur);
		}

		PERFINFO_AUTO_STOP();
	}
}

static void mmHandleViewStatusChangedFromBG(MovementManager* mm,
											MovementThreadData* td)
{
	mm->fg.frameWhenViewChanged = mgState.frameCount;
	mm->fg.flagsMutable.posViewIsAtRest = !FALSE_THEN_SET(td->toFG.flagsMutable.posIsAtRest);
	mm->fg.flagsMutable.rotViewIsAtRest = !FALSE_THEN_SET(td->toFG.flagsMutable.rotIsAtRest);
	mm->fg.flagsMutable.pyFaceViewIsAtRest = !FALSE_THEN_SET(td->toFG.flagsMutable.pyFaceIsAtRest);

	mmSendMsgViewStatusChangedFG(mm, "bg or next frame");
}

bool mmHandleAfterSimWakesHasReason(	MovementManager* mm,
										const char *reason)
{
	#if MM_TRACK_AFTER_SIM_WAKES
	{
		char *findReasonValue = NULL;
		PERFINFO_AUTO_START("debug track afterSimWakes reasons on verify", 1);
		if(stashFindPointer(mm->fg.afterSimWakes.stReasons, reason, &findReasonValue)) {
			PERFINFO_AUTO_STOP();
			return true;
		} else {
			PERFINFO_AUTO_STOP();
			return false;
		}		
	}
	#endif

	return false;
}

void mmHandleAfterSimWakesIncFG(MovementManager* mm,
								const char* reason,
								const char* storeReasonValue)
{
	#if MM_TRACK_AFTER_SIM_WAKES
	{
		char *findReasonValue = NULL;
		PERFINFO_AUTO_START("debug track afterSimWakes reasons on inc", 1);
		if(stashFindPointer(mm->fg.afterSimWakes.stReasons, reason, &findReasonValue)){
			assertmsgf(0, "afterSimWakes reason \"%s\" is already in the list.", NULL_TO_EMPTY(reason));
		}
		if(!mm->fg.afterSimWakes.stReasons){
			mm->fg.afterSimWakes.stReasons = stashTableCreateWithStringKeys(10, StashDefault);
		}
		assert(	mm->fg.afterSimWakes.count ==
				stashGetCount(mm->fg.afterSimWakes.stReasons));
		if(!stashAddPointer(mm->fg.afterSimWakes.stReasons, reason, storeReasonValue, false)){
			assert(0);
		}
		PERFINFO_AUTO_STOP();
	}
	#endif

	if(!mm->fg.afterSimWakes.count++){
		#if MM_TRACK_AFTER_SIM_WAKES
		{
			PERFINFO_AUTO_START("debug check afterSimWakes reason count", 1);
			assert(stashGetCount(mm->fg.afterSimWakes.stReasons) == 1);
			PERFINFO_AUTO_STOP();
		}
		#endif

		assert(!mm->fg.afterSimWakes.index);

		csEnter(&mgState.fg.managersAfterSimWakes.cs);
		if(mm->fg.flags.isAttachedToClient){
			mm->fg.afterSimWakes.index = eaPush(&mgState.fg.managersAfterSimWakes.clientMutable, mm);
		}else{
			mm->fg.afterSimWakes.index = eaPush(&mgState.fg.managersAfterSimWakes.nonClientMutable, mm);
		}
		csLeave(&mgState.fg.managersAfterSimWakes.cs);
	}
}

void mmHandleAfterSimWakesDecFG(MovementManager* mm,
								const char* reason)
{
	assert(mm->fg.afterSimWakes.count);

	#if MM_TRACK_AFTER_SIM_WAKES
	{
		char *removeReasonValue = NULL;
		PERFINFO_AUTO_START("debug track afterSimWakes reasons on dec", 1);
		assert(	mm->fg.afterSimWakes.count ==
				stashGetCount(mm->fg.afterSimWakes.stReasons));

		if(!stashRemovePointer(mm->fg.afterSimWakes.stReasons, reason, &removeReasonValue)){
			assertmsgf(0, "afterSimWakes reason \"%s\" is not in the list.", NULL_TO_EMPTY(reason));
		}
		PERFINFO_AUTO_STOP();
	}
	#endif

	if(!--mm->fg.afterSimWakes.count){
		csEnter(&mgState.fg.managersAfterSimWakes.cs);
		mmRemoveFromAfterSimWakesListFG(mm, mm->fg.flags.isAttachedToClient);
		csLeave(&mgState.fg.managersAfterSimWakes.cs);
	}
}

static void mmHandleAfterSimWakesFG(MovementManager* mm){
	MovementThreadData*	td;
	S32					startedTimer = 0;
	S32					sendUserThreadDataMsg = 0;

	#if MM_TRACK_AFTER_SIM_WAKES
	{
		static StashTable	st;
		StashTableIterator	it;
		StashElement		el;

		PERFINFO_AUTO_START("debug count afterSimWakes reasons", 1);
		
		stashGetIterator(mm->fg.afterSimWakes.stReasons, &it);
		while(stashGetNextElement(&it, &el)){
			const char*		reason = stashElementGetKey(el);
			StaticCmdPerf*	perf;

			if(!stashFindPointer(st, reason, &perf)){
				perf = callocStruct(StaticCmdPerf);
				perf->name = reason;
				if(!st){
					st = stashTableCreateWithStringKeys(100, StashDefault);
				}
				stashAddPointer(st, reason, perf, false);
			}

			START_MISC_COUNT_STATIC(0, perf->name, &perf->pi);
			STOP_MISC_COUNT(1);
		}

		PERFINFO_AUTO_STOP();
	}
	#endif
	
	if(mgState.debug.flags.perEntityTimers){
		startedTimer = mmStartPerEntityTimer(mm);
	}

	#if MM_TRACK_AFTER_SIM_WAKES
	{
		//version with error checking
		if (mm->fg.flags.afterSimOnceFromBG != mm->fg.flags.afterSimOnceFromBGbit)
		{
			Errorf(	"MM Flag Bug : before clearing, afterSimOnceFromBG = %u when afterSimOnceFromBGbit = %u",
					mm->fg.flags.afterSimOnceFromBG,
					mm->fg.flags.afterSimOnceFromBGbit);
		}

		if(TRUE_THEN_RESET(mm->fg.flagsMutable.afterSimOnceFromBG)){
			mm->fg.flagsMutable.afterSimOnceFromBGbit = 0;
			if (mmHandleAfterSimWakesHasReason(mm, "afterSimOnceFromBG")) {
				mmHandleAfterSimWakesDecFG(mm, "afterSimOnceFromBG");
			} else {
				Errorf("MM Flag Bug : Caught afterSimOnceFromBG flag set without a reason also being in the afterSimWakes stash table! (afterSimWakes count = %u, s.t. size = %u)",
						mm->fg.afterSimWakes.count,
						stashGetCount(mm->fg.afterSimWakes.stReasons));
			}
		} else if (mmHandleAfterSimWakesHasReason(mm, "afterSimOnceFromBG")) {
			Errorf("MM Flag Bug : Found afterSimOnceFromBG in reason stash table when flag not set at time it should be decremented (afterSimWakes count = %u, s.t. size = %u)",
					mm->fg.afterSimWakes.count,
					stashGetCount(mm->fg.afterSimWakes.stReasons));
		}
	}
	#else
	{
		//original version
		if(TRUE_THEN_RESET(mm->fg.flagsMutable.afterSimOnceFromBG)){
			mmHandleAfterSimWakesDecFG(mm, "afterSimOnceFromBG");
		}
	}
	#endif
	
	
	#if MM_TRACK_AFTER_SIM_WAKES
	{
		//version with error checking
		if (mm->fg.flags.sentUserThreadDataUpdateToBG != mm->fg.flags.sentUserThreadDataUpdateToBGbit)
		{
			Errorf(	"MM Flag Bug : before clearing, sentUserThreadDataUpdateToBG = %u when sentUserThreadDataUpdateToBGbit = %u",
					mm->fg.flags.sentUserThreadDataUpdateToBG,
					mm->fg.flags.sentUserThreadDataUpdateToBGbit);
		}

		if(TRUE_THEN_RESET(mm->fg.flagsMutable.sentUserThreadDataUpdateToBG)) {
			mm->fg.flagsMutable.sentUserThreadDataUpdateToBGbit = 0;
			if (mmHandleAfterSimWakesHasReason(mm, "sentUserThreadDataUpdateToBG")) {
				sendUserThreadDataMsg = 1;
				mmHandleAfterSimWakesDecFG(mm, "sentUserThreadDataUpdateToBG");
			} else {
				Errorf("MM Flag Bug : Caught sentUserThreadDataUpdateToBG flag set without a reason also being in the afterSimWakes stash table! (afterSimWakes count = %u, s.t. size = %u)",
						mm->fg.afterSimWakes.count,
						stashGetCount(mm->fg.afterSimWakes.stReasons));
			}
		} else if (mmHandleAfterSimWakesHasReason(mm, "sentUserThreadDataUpdateToBG")) {
			Errorf("MM Flag Bug : Found sentUserThreadDataUpdateToBG in reason stash table when flag not set at time it should be decremented! (afterSimWakes count = %u, s.t. size = %u)",
					mm->fg.afterSimWakes.count,
					stashGetCount(mm->fg.afterSimWakes.stReasons));
		}
	}
	#else
	{
		//original version
		if(TRUE_THEN_RESET(mm->fg.flagsMutable.sentUserThreadDataUpdateToBG)){
			sendUserThreadDataMsg = 1;
			mmHandleAfterSimWakesDecFG(mm, "sentUserThreadDataUpdateToBG");
		}
	}
	#endif

	td = MM_THREADDATA_FG(mm);

	// Remove stuff that's been repredicted.

	if(	!mgState.flags.isServer &&
		eaSize(&mm->fg.resources))
	{
		mmRemoveRepredictedResourceStatesFG(mm, td);
	}

	// Handle stuff from BG.

	if(TRUE_THEN_RESET(td->toFG.flagsMutable.hasToFG)){
		mmHandleUpdatesFromBG(mm, td);
	}

	// Check for view changes.
	
	if(TRUE_THEN_RESET(td->toFG.flagsMutable.viewStatusChanged)){
		mmHandleViewStatusChangedFromBG(mm, td);
	}

	// Set the current view if necessary.

	if(mgState.fg.flags.alwaysSetCurrentView){
		mmLog(	mm,
				NULL,
				"[fg.view] Setting view from %s (AtRest: %s%s%s)",
				__FUNCTION__,
				mm->fg.flags.posViewIsAtRest ? "pos, " : "",
				mm->fg.flags.rotViewIsAtRest ? "rot, " : "",
				mm->fg.flags.pyFaceViewIsAtRest ? "pyFace, " : "");

		mmSetCurrentViewFG(mm, td);
	}

	// Manage resource states.

	if(mm->fg.flags.mmrWaitingForWake){
		mmResourcesSendWakeFG(mm);
	}

	if(mm->fg.flags.mmrNeedsSetState){
		mmResourcesSetStateFG(mm, td);
	}

	// Send AFTER_SYNC to requesters if something is going to happen in the BG.
	
	if(TRUE_THEN_RESET(mm->fg.flagsMutable.mrNeedsAfterSync)){
		mmHandleAfterSimWakesDecFG(mm, "mrNeedsAfterSync");
		mmSendMsgsAfterSyncFG(mm);
	}
	
	// Send msgs if userPointer is updated.
	
	if(TRUE_THEN_RESET(td->toFG.flagsMutable.userThreadDataHasUpdate)){
		mmSendMsgUserThreadDataUpdatedFG(mm);
	}
	
	if(sendUserThreadDataMsg){
		mmSendMsgAfterUpdatedUserThreadDataToBG(mm);
	}
	
	if(MMLOG_IS_ENABLED(mm)){
		mmLogAfterSimWakesEndFG(mm);
	}

	if(startedTimer){
		PERFINFO_AUTO_STOP();// id group.
	}
}

static void mmAllSetViewFG(const FrameLockedTimer* flt){
	#define nv (mgState.fg.netViewMutable)
	{
		U32 offsetToCeiling;
		U32 offsetToFloor;
		F32 spcDelta =	mgState.fg.frame.cur.prevSecondsDelta *
						MM_PROCESS_COUNTS_PER_SECOND;
		
		// Set the net view.

		if(spcDelta >= nv.spcOffsetFromEnd.lag){
			const F32 spcFastScale = 1.5f;
			F32 spcFastDelta;
			
			spcDelta -= nv.spcOffsetFromEnd.lag;
			nv.spcOffsetFromEnd.lag = 0.f;
			
			spcFastDelta = spcDelta * spcFastScale;
			
			if(spcFastDelta >= nv.spcOffsetFromEnd.fast){
				spcFastDelta -= nv.spcOffsetFromEnd.fast;
				nv.spcOffsetFromEnd.fast = 0;
				spcDelta = spcFastDelta / spcFastScale;

				if(spcDelta >= nv.spcOffsetFromEnd.normal){
					nv.spcOffsetFromEnd.normal = 0.f;
				}else{
					nv.spcOffsetFromEnd.normal -= spcDelta;
				}
			}else{
				nv.spcOffsetFromEnd.fast -=	spcFastDelta;
				spcDelta = 0.f;
			}
			
			nv.spcOffsetFromEnd.total =	nv.spcOffsetFromEnd.normal +
										nv.spcOffsetFromEnd.fast;
		}else{
			nv.spcOffsetFromEnd.lag -= spcDelta;
			spcDelta = 0.f;
		}

		offsetToCeiling = (S32)nv.spcOffsetFromEnd.total;
		offsetToCeiling -= offsetToCeiling % MM_PROCESS_COUNTS_PER_STEP;

		offsetToFloor = (S32)ceilf(nv.spcOffsetFromEnd.total);
		if(offsetToFloor % MM_PROCESS_COUNTS_PER_STEP){
			// Goto next one.
			offsetToFloor -= offsetToFloor % MM_PROCESS_COUNTS_PER_STEP;
			offsetToFloor += MM_PROCESS_COUNTS_PER_STEP;
		}

		nv.spcFloor =	mgState.fg.netReceive.cur.pc.server -
						offsetToFloor;
		
		nv.spcCeiling = mgState.fg.netReceive.cur.pc.server -
						offsetToCeiling;

		if(nv.spcCeiling == nv.spcFloor){
			nv.spcInterpFloorToCeiling = 0.f;
		}else{
			nv.spcInterpFloorToCeiling =	1.f
											-
											(	nv.spcOffsetFromEnd.total -
												(F32)offsetToCeiling) /
											(F32)MM_PROCESS_COUNTS_PER_STEP;

			MINMAX1(nv.spcInterpFloorToCeiling, 0.f, 1.f);
		}
	}
	#undef nv

	// Set the local view process count.

	#define lv (mgState.fg.localViewMutable)
	{
		F32 prevProcessRatio60;

		frameLockedTimerGetProcesses(	flt,
										NULL,
										NULL,
										&lv.pcCeiling,
										NULL);

		frameLockedTimerGetProcessRatio(flt,
										NULL,
										&prevProcessRatio60);

		lv.outputInterp.forward =	(	(lv.pcCeiling %
										MM_PROCESS_COUNTS_PER_STEP) +
										prevProcessRatio60) /
									(F32)MM_PROCESS_COUNTS_PER_STEP;

		if(mgState.flags.isServer){
			lv.outputInterp.forward = 1.f;
			lv.outputInterp.inverse = 0.f;
		}else{
			MINMAX1(lv.outputInterp.forward, 0.f, 1.f);

			lv.outputInterp.inverse =	1.f -
										lv.outputInterp.forward;
		}

		lv.pcCeiling &= ~1;
		lv.spcCeiling = lv.pcCeiling +
						mgState.fg.netReceive.cur.offset.clientToServerSync;
	}
	#undef lv
}

static void mmClientHandleAfterSimWakesFG(MovementClient* mc){
	MovementClientThreadData* mctd = mc->threadData + MM_FG_SLOT;
	
	PERFINFO_AUTO_START_FUNC();

	if(TRUE_THEN_RESET(mctd->fg.netSend.flags.hasStateBG)){
		mc->netSend.flags.sendStateBG = 1;
		mc->netSend.sync.cur.cpc = mctd->fg.netSend.sync.cpc;
		mc->netSend.sync.cur.spc = mctd->fg.netSend.sync.spc;
		mc->netSend.sync.cur.forcedStepCount = mctd->fg.netSend.sync.forcedStepCount;
	}

	mmClientHandleFinishedInputStepsFG(mc);

	PERFINFO_AUTO_STOP();
}

static void mmAllUpdateTimersAfterSimWakesFG(const FrameLockedTimer* flt){
	frameLockedTimerGetPrevTimes(	flt,
									&mgState.fg.frame.cur.prevSecondsDelta,
									NULL,
									NULL);

	frameLockedTimerGetCurTimes(	flt,
									&mgState.fg.frame.cur.secondsDelta,
									NULL,
									NULL);
	
	frameLockedTimerGetProcesses(	flt,
									NULL,
									&mgState.fg.frame.cur.deltaProcesses,
									&mgState.fg.frame.cur.pcPrev,
									NULL);

	frameLockedTimerGetProcessRatio(flt,
									NULL,
									&mgState.fg.frame.cur.prevProcessRatio);
}

static void mmAllLogViewFG(void){
	if(!mgState.debug.activeLogCount){
		return;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager* mm = mgState.fg.managers[i];

		if(!MMLOG_IS_ENABLED(mm)){
			continue;
		}
		
		if(mm->msgHandler){
			MovementManagerMsgPrivateData pd = {0};

			pd.mm = mm;
			pd.msg.msgType = MM_MSG_FG_LOG_VIEW;
			pd.msg.userPointer = mm->userPointer;

			mm->msgHandler(&pd.msg);
		}
	}
	EARRAY_FOREACH_END;
}

static void mmAllHandleThreadResultsFromBG(MovementGlobalStateThreadData* mgtd)
{
	bool bSetThisFrame = false;

	if(!TRUE_THEN_RESET(mgtd->toFG.flags.hasThreadResults)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mgtd->toFG.threadResults, i, isize);
	{
		MovementProcessingThreadResults* r = mgtd->toFG.threadResults[i];

		EARRAY_CONST_FOREACH_BEGIN(r->managersAfterSimWakes, j, jsize);
		{
			MovementManager*	mm = r->managersAfterSimWakes[j];
			MovementThreadData* td = MM_THREADDATA_FG(mm);

			//begin error checking
			if (mm->fg.flagsMutable.afterSimOnceFromBG)
			{
				U32 reportCount;
				char *extra;

				//When this is true, it causes a crash on ASSERT_FALSE_AND_SET(mm->fg.flagsMutable.afterSimOnceFromBG)
				//I haven't figured out how this is happening yet, perhaps a memory stomp?
				//This behavior doesn't appear to be related to the MM appearing here 2x on the same frame
				//The last time I observed this, the flag was true at the end of mmAllHandleAfterSimWakesFG (where it should only ever be false) then when that flag got passed back into mmAllHandleAfterSimWakesFG as true on the next frame it caused a crash here on the assert
				//One way to trigger this is to force the player to re-spawn every frame (by adjusting the height in GameServerLib.C on the line "if(ent_mat[3][1] <= 50.f - MAX_PLAYABLE_COORDINATE){")
				//This only happens on the game server

				mm->fg.flagsMutable.afterSimOnceFromBG = 0;
				reportCount = mm->fg.afterSimWakes.count;
				extra = "";

				if (mm->fg.afterSimWakes.count) {
					#if MM_TRACK_AFTER_SIM_WAKES
					{
						char *findReasonValue = NULL;
						if (stashFindPointer(mm->fg.afterSimWakes.stReasons, "afterSimOnceFromBG", &findReasonValue))
						{
							mmHandleAfterSimWakesDecFG(mm, "afterSimOnceFromBG");
							extra = ", FOUND reason in table";
						} else {
							extra = ", did NOT find reason in table";
						}
					}
					#else
					{
						extra = ", NOT TRACKING but has count"
					}
					#endif
				}

				Errorf(	"MM Flag Bug : afterSimOnceFromBG set when unexpected in %s! (bSetThisFrame = %u, count = %u%s)\n",
						__FUNCTION__,
						bSetThisFrame ? 1 : 0,
						reportCount,
						extra);
			}
			//end error checking

			#if MM_TRACK_AFTER_SIM_WAKES
			{
				//error checking version
				if (mm->fg.flags.afterSimOnceFromBG != mm->fg.flags.afterSimOnceFromBGbit)
				{
					Errorf(	"MM Flag Bug : before setting, afterSimOnceFromBG = %u when afterSimeOnceFromBGbit = %u",
							mm->fg.flags.afterSimOnceFromBG,
							mm->fg.flags.afterSimOnceFromBGbit);
				}

				ASSERT_TRUE_AND_RESET(td->toFG.flagsMutable.afterSimOnce);
				ASSERT_FALSE_AND_SET(mm->fg.flagsMutable.afterSimOnceFromBG);
				mm->fg.flagsMutable.afterSimOnceFromBGbit = 1;
				if (mmHandleAfterSimWakesHasReason(mm, "afterSimOnceFromBG")) {
					Errorf("MM Flag Bug : Found afterSimOnceFromBG in reason stash table right before it should normally be added! (afterSimWakes count = %u, s.t. size = %u)",
							mm->fg.afterSimWakes.count,
							stashGetCount(mm->fg.afterSimWakes.stReasons));
				} else {
					mmHandleAfterSimWakesIncFG(mm, "afterSimOnceFromBG", __FUNCTION__);
				}
			}
			#else
			{
				//original version
				ASSERT_TRUE_AND_RESET(td->toFG.flagsMutable.afterSimOnce);
				ASSERT_FALSE_AND_SET(mm->fg.flagsMutable.afterSimOnceFromBG);
				mmHandleAfterSimWakesIncFG(mm, "afterSimOnceFromBG", __FUNCTION__);
			}
			#endif
			
			bSetThisFrame = true;
		}
		EARRAY_FOREACH_END;

		mgtd->toFG.threadResults[i] = NULL;
		eaPush(&mgtd->toBG.threadResults, r);

		eaClearFast(&r->managersAfterSimWakesMutable);
	}
	EARRAY_FOREACH_END;

	eaClearFast(&mgtd->toFG.threadResults);

	PERFINFO_AUTO_STOP();
}

static void mmAllHandleUpdatedManagersFromBG(MovementGlobalStateThreadData* mgtd){
	if(!TRUE_THEN_RESET(mgtd->toFG.flags.hasUpdatedManagers)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mgtd->toFG.updatedManagers, i, isize);
	{
		MovementManager*	mm = mgtd->toFG.updatedManagers[i];
		MovementThreadData* td = MM_THREADDATA_FG(mm);
			
		ASSERT_TRUE_AND_RESET(td->toFG.flagsMutable.inUpdatedList);
			
		if(TRUE_THEN_RESET(td->toFG.flagsMutable.destroyed)){
			assert(mm->fg.flags.destroyed);
				
			mm->fg.flagsMutable.destroyedFromBG = 1;
			mgState.fg.flagsMutable.mmNeedsDestroy = 1;

			assert(eaFind(&mgState.fg.managersToDestroy, mm) < 0);
			eaPush(&mgState.fg.managersToDestroyMutable, mm);
		}
	}
	EARRAY_FOREACH_END;
		
	if(eaSize(&mgtd->toFG.updatedManagers) > 100){
		eaDestroy(&mgtd->toFG.updatedManagers);
	}else{
		eaClearFast(&mgtd->toFG.updatedManagers);
	}

	PERFINFO_AUTO_STOP();
}

static void mmAllCompactAfterSimWakesListFG(MovementManager*** managers){
	U32 size = 0;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(*managers, i, isize);
	{
		MovementManager* mm = (*managers)[i];

		if(mm){
			if((U32)i > size){
				(*managers)[size] = mm;
				assert(mm->fg.afterSimWakes.index == (U32)i);
				mm->fg.afterSimWakes.index = size;
			}

			size++;
		}
	}
	EARRAY_FOREACH_END;

	eaSetSize(managers, size);

	PERFINFO_AUTO_STOP();
}

static void mmAllHandleAfterSimWakesFG(const WorldCollIntegrationMsg* msg){
	MovementGlobalStateThreadData* mgtd = mgState.threadData + MM_FG_SLOT;

	mgState.fg.flagsMutable.alwaysSetCurrentView = !mgState.flags.isServer;

	mmAllUpdateTimersAfterSimWakesFG(msg->fg.afterSimWakes.flt);
	
	mmAllSetViewFG(msg->fg.afterSimWakes.flt);
	
	mmAllHandleThreadResultsFromBG(mgtd);
	mmAllHandleUpdatedManagersFromBG(mgtd);

	if(	eaSize(&mgState.fg.managersAfterSimWakes.client) ||
		eaSize(&mgState.fg.managersAfterSimWakes.nonClient))
	{
		ASSERT_FALSE_AND_SET(mgState.fg.flagsMutable.managersAfterSimWakesLocked);
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managersAfterSimWakes.client, i, isize);
		{
			MovementManager* mm = mgState.fg.managersAfterSimWakes.client[i];

			if(mm){
				PERFINFO_AUTO_START("mmHandleAfterSimWakesFG:client", 1);
				mmHandleAfterSimWakesFG(mm);
				PERFINFO_AUTO_STOP();
			}
		}
		EARRAY_FOREACH_END;
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managersAfterSimWakes.nonClient, i, isize);
		{
			MovementManager* mm = mgState.fg.managersAfterSimWakes.nonClient[i];

			if(mm){
				PERFINFO_AUTO_START("mmHandleAfterSimWakesFG:non-client", 1);
				mmHandleAfterSimWakesFG(mm);
				PERFINFO_AUTO_STOP();
			}
		}
		EARRAY_FOREACH_END;
		ASSERT_TRUE_AND_RESET(mgState.fg.flagsMutable.managersAfterSimWakesLocked);

		if(TRUE_THEN_RESET(mgState.fg.flagsMutable.managersAfterSimWakesClientChanged)){
			mmAllCompactAfterSimWakesListFG(&mgState.fg.managersAfterSimWakes.clientMutable);
		}

		if(TRUE_THEN_RESET(mgState.fg.flagsMutable.managersAfterSimWakesNonClientChanged)){
			mmAllCompactAfterSimWakesListFG(&mgState.fg.managersAfterSimWakes.nonClientMutable);
		}
	}
	else
	{
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
		{
			MovementManager* mm = mgState.fg.managers[i];
			if (mm->fg.flags.afterSimOnceFromBG) {
				//this should be impossible, if it's happening, I'd like to know
				Errorf("when client & nonClient lists are empty: afterSimOnceFromBG set after handling sim wakes calls in %s!\n",__FUNCTION__);
			}
		}
		EARRAY_FOREACH_END;
	}

	if(mgState.flags.isServer){
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.clients, i, isize);
		{
			mmClientHandleAfterSimWakesFG(mgState.fg.clients[i]);
		}
		EARRAY_FOREACH_END;
	}else{
		mmClientHandleAfterSimWakesFG(&mgState.fg.mc);
	}
	
#if MM_VERIFY_TOFG_VIEW_STATUS
	{
		PERFINFO_AUTO_START("debug verify toFG view status", 1);
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
		{
			MovementManager*	mm = mgState.fg.managers[i];
			MovementThreadData*	td = MM_THREADDATA_FG(mm);

			assert(!td->toFG.flags.viewStatusChanged);
			assert(!td->toFG.flags.afterSimOnce);
		}
		EARRAY_FOREACH_END;
		PERFINFO_AUTO_STOP();
	}
#endif

	mmAllLogViewFG();
	
	// Destroy MovementManagers.

	mmAllHandleManagerDestroyQueueFG();
}

static void mmBodyInstanceDestroyFG(MovementManager* mm,
									MovementBodyInstance** biInOut)
{
	MovementBodyInstance* bi = SAFE_DEREF(biInOut);
	
	if(!bi){
		return;
	}

	*biInOut = NULL;

	if(eaFindAndRemove(&mm->fg.bodyInstancesMutable, bi) < 0){
		assert(0);
	}
	
	if(!eaSize(&mm->fg.bodyInstances)){
		eaDestroy(&mm->fg.bodyInstancesMutable);
	}
	
	if(eaFindAndRemove(&mgState.fg.bodyInstancesMutable, bi) < 0){
		assert(0);
	}

	SAFE_FREE(bi);
}

void mmKinematicObjectMsgHandler(const WorldCollObjectMsg* msg){
	MovementBodyInstance* bi = msg->userPointer;
	
	if(msg->msgType == WCO_MSG_GET_DEBUG_STRING){
		snprintf_s(	msg->in.getDebugString.buffer,
					msg->in.getDebugString.bufferLen,
					"mmKinematic (userPointer 0x%p)",
					SAFE_MEMBER(bi, mm->userPointer));
	}
		
	if(!bi){
		return;
	}
	
	switch(msg->msgType){
		xcase WCO_MSG_DESTROYED:{
			assert(bi->wco == msg->wco);
			bi->wco = NULL;
			
			if(bi->flags.freeOnWCODestroy){
				mmBodyInstanceDestroyFG(bi->mm, &bi);
			}
		}
		
		xcase WCO_MSG_GET_NEW_MAT:{
			quatToMat(bi->rot, msg->out.getNewMat->mat);
			copyVec3(bi->pos, msg->out.getNewMat->mat[3]);
		}
		
		xcase WCO_MSG_GET_SHAPE:{
			WorldCollObjectMsgGetShapeOut* getShape = msg->out.getShape;
			
			if(!bi->body){
				break;
			}
			
			getShape->flags.isKinematic = 1;
			getShape->flags.hasOneWayCollision = bi->flags.hasOneWayCollision;

			quatToMat(bi->rot, getShape->mat);
			copyVec3(bi->pos, getShape->mat[3]);

			EARRAY_CONST_FOREACH_BEGIN(bi->body->parts, i, isize);
			{
				MovementBodyPart*					p = bi->body->parts[i];
				MovementGeometry*					g = p->geo;
				WorldCollObjectMsgGetShapeOutInst*	shape;
				char								buffer[100];

				wcoAddShapeInstance(getShape, &shape);

				shape->filter.shapeGroup = WC_SHAPEGROUP_ENTITY;
				shape->filter.filterBits = WC_FILTER_BITS_ENTITY;

				createMat3YPR(shape->mat, p->pyr);
				copyVec3(p->pos, shape->mat[3]);
				
				sprintf(buffer,
						"(%1.2f, %1.2f, %1.2f)",
						vecParamsXYZ(bi->pos));
				
				mmGeometryGetTriangleMesh(g, &shape->mesh, buffer);
			}
			EARRAY_FOREACH_END;
		}
	}
}

static void mmRefreshBodies(void){
	//if(!TRUE_THEN_RESET(mgState.fg.flagsMutable.needsBodyRefresh)){
	//	return;
	//}

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.bodyInstances, i, isize);
	{
		MovementBodyInstance* bi = mgState.fg.bodyInstances[i];
		
		if(bi->body->parts){
			MovementManager*	mm = bi->mm;
			Vec3				pos;
			Quat				rot;
			Vec3 				aabbMin;
			Vec3 				aabbMax;
			Vec3				radiusXYZ;
			
			setVec3same(radiusXYZ,
						bi->body->radius);
						
			mmGetPositionFG(mm, pos);
			
			addVec3(pos,
					radiusXYZ,
					aabbMax);
			
			subVec3(pos,
					radiusXYZ,
					aabbMin);

			mmGetRotationFG(mm, rot);

			if(!bi->wco){
				wcoCreate(	&bi->wco,
							SAFE_MEMBER(mm->space, wc),
							mmKinematicObjectMsgHandler,
							bi,
							aabbMin,
							aabbMax,
							1,
							bi->flags.isShell);

				copyVec3(pos, bi->pos);
				copyQuat(rot, bi->rot);
			}
			else if(!sameVec3(pos, bi->pos) ||
					!sameQuat(rot, bi->rot))
			{
				wcoChangeBounds(bi->wco,
								aabbMin,
								aabbMax);

				copyVec3(pos, bi->pos);
				copyQuat(rot, bi->rot);
			}
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

static void mmCreateTestWCO(const Vec3 pos,
							S32 isDynamic)
{
	WorldCollObject*	wco = NULL;
	Vec3				aabbMin;
	Vec3				aabbMax;
	Vec3				radiusXYZ = {50, 50, 50};
	MovementSpace*		space = eaGet(&mgState.fg.spaces, 0);
	WorldColl*			wc = SAFE_MEMBER(space, wc);

	addVec3(pos, radiusXYZ, aabbMax);
	subVec3(pos, radiusXYZ, aabbMin);

	wcoCreate(	&wco,
				wc,
				mmKinematicObjectMsgHandler,
				NULL,
				aabbMin,
				aabbMax,
				isDynamic,
				0);
}

AUTO_COMMAND;
void mmCreateTestStaticWCO(	S32 count,
							const Vec3 pos)
{
	FOR_BEGIN(i, count);
		mmCreateTestWCO(pos, 0);
	FOR_END;
}

AUTO_COMMAND;
void mmCreateTestDynamicWCO(S32 count,
							const Vec3 pos)
{
	FOR_BEGIN(i, count);
		mmCreateTestWCO(pos, 1);
	FOR_END;
}

static void mmGlobalSendMsgFrameUpdated(void){
	MovementGlobalMsg msg = {0};
	
	if(!mgState.msgHandler){
		return;
	}
	
	msg.msgType = MG_MSG_FRAME_UPDATED;
	msg.frameUpdated.frameCount = mgState.frameCount;

	mgState.msgHandler(&msg);
}

static void mmAllHandleWhileSimSleepsNOBG(const WorldCollIntegrationMsg* msg){
	PERFINFO_AUTO_START_FUNC();
	
	if(mgState.flags.printAllocationCounts){
		static U32 msLastPrintTime;
		
		if(	!msLastPrintTime ||
			timeGetTime() - msLastPrintTime >= 1000)
		{
			printf("miSteps: %u\n", mpGetAllocatedCount(MP_NAME(MovementInputStep)));
			
			msLastPrintTime = timeGetTime();
		}
	}

	// Swap the FG and BG slots.

	mgState.fg.threadDataSlotMutable = !MM_FG_SLOT;
	mgState.bg.threadDataSlotMutable = !MM_FG_SLOT;

	assert(MM_BG_SLOT == (U32)!MM_FG_SLOT);
	assert(MM_FG_SLOT == (U32)!MM_BG_SLOT);
	
	// Increment the frame count.

	mgState.frameCount++;
	
	// Initialize the doProcessIfValidStep flag.
	
	mgState.bg.flagsMutable.doProcessIfValidStep = mgState.fg.flags.doProcessIfValidStepInit;

	// Update the step counters.

	mgState.bg.frame.prev = mgState.bg.frame.cur;

	mgState.bg.frame.cur.stepCount.cur = 0;
	mgState.bg.frame.cur.stepCount.total = mgState.fg.frame.cur.stepCount;

	mgState.bg.frame.cur.pcStart = mgState.fg.frame.cur.pcStart;

	mgState.bg.frame.next.pcStart = mgState.fg.frame.next.pcStart;

	// Set the sync PC.

	if(mgState.flags.isServer){
		mgState.bg.pc.local.sync =	mgState.fg.netSendToClient.cur.pc +
									MM_PROCESS_COUNTS_PER_STEP;
	}else{
		mgState.bg.pc.local.sync = mgState.fg.netReceive.sync.spc;
	}

	// Initialize the PCs.

	mgState.bg.pc.local.cur = mgState.fg.frame.cur.pcCatchup;
	
	mgState.bg.netReceive.cur.offset.clientToServer =
												mgState.fg.netReceive.cur.offset.clientToServerSync;
	
	mgState.bg.pc.server.cur =	mgState.bg.pc.local.cur +
								mgState.fg.netReceive.cur.offset.clientToServerSync;

	mgState.bg.pc.server.curView =	mgState.bg.pc.server.cur -
									MM_BG_PROCESS_COUNT_OFFSET_TO_CUR_VIEW;

	// Set the current and previous net receive PCs.

	mgState.bg.netReceive.prevPrev = mgState.bg.netReceive.prev;
	mgState.bg.netReceive.prev = mgState.bg.netReceive.cur;
	
	mgState.bg.netReceive.cur.pc.client = mgState.fg.netReceive.cur.pc.client;
	mgState.bg.netReceive.cur.pc.server = mgState.fg.netReceive.cur.pc.server;
	mgState.bg.netReceive.cur.pc.serverSync = mgState.fg.netReceive.cur.pc.serverSync;
	
	mgState.bg.netReceive.cur.forcedStepCount = mgState.fg.netReceive.cur.forcedStepCount;

	mmRefreshBodies();

	mmGlobalSendMsgFrameUpdated();

	PERFINFO_AUTO_STOP();
}

static void mmCheckForMovementKeysFG(	MovementManager* mm,
										const MovementInputStep* miStep)
{
	const MovementInputEvent* mie;
	
	if(	!mm->msgHandler ||
		!mm->userPointer)
	{
		return;
	}

	for(mie = miStep->mieList.head;
		mie;
		mie = mie->next)
	{
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_INPUT_VALUE_CHANGED;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.fg.inputValueChanged.value = mie->value;

		mm->msgHandler(&pd.msg);
	}
}

static void mmLogConsolidatedEvents(MovementManager* mm,
									MovementInputStep* miStep)
{
	const MovementInputEvent* mie;
	
	for(mie = miStep->mieList.head;
		mie;
		mie = mie->next)
	{
		const char* indexName;
		
		mmGetInputValueIndexName(	mie->value.mivi,
									&indexName);
		
		if(INRANGE(mie->value.mivi, MIVI_BIT_LOW, MIVI_BIT_HIGH)){
			mmLog(	mm,
					NULL,
					"[fg.input] c%u, s%u, ss%u: Consolidating bit change bit[%s] = %d",
					miStep->pc.client,
					miStep->pc.server,
					miStep->pc.serverSync,
					indexName,
					mie->value.bit);
		}
		else if(INRANGE(mie->value.mivi, MIVI_F32_LOW, MIVI_F32_HIGH)){
			mmLog(	mm,
					NULL,
					"[fg.input] c%d, s%d, ss%d: Consolidating F32 change f[%s] = %f [%8.8x]",
					miStep->pc.client,
					miStep->pc.server,
					miStep->pc.serverSync,
					indexName,
					mie->value.f32,
					*(U32*)&mie->value.f32);
		}
	}
}

static void mmClientConsolidateInputSteps(	MovementClient* mc,
											MovementClientInputStep* mciStep,
											const U32 stepCountToConsolidate,
											const char* reason)
{
	U32 stepCount = mc->mciStepList.count;

	FOR_BEGIN(k, (S32)stepCountToConsolidate);
	{
		MovementClientInputStep* mciStepNext = mciStep->next;
		
		assert(mciStepNext);

		EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
		{
			MovementManager*		mm = mc->mcmas[i]->mm;
			MovementInputStep*		miStepCur = NULL;
			MovementInputStep*		miStepNext = NULL;
			
			// Find the miStep for this mm.
			
			EARRAY_CONST_FOREACH_BEGIN(mciStep->miSteps, j, jsize);
			{
				MovementInputStep* miStep = mciStep->miSteps[j];

				if(miStep->mm == mm){
					miStepCur = miStep;
					break;
				}
			}
			EARRAY_FOREACH_END;
			
			// Find the next miStep for this mm, and remove it.

			EARRAY_CONST_FOREACH_BEGIN(mciStepNext->miSteps, j, jsize);
			{
				MovementInputStep* miStep = mciStepNext->miSteps[j];

				if(miStep->mm == mm){
					miStepNext = miStep;
					
					miStepNext->mciStep = NULL;
					
					eaRemove(&mciStepNext->miStepsMutable, j);
								
					break;
				}
			}
			EARRAY_FOREACH_END;

			// Log the consolidation.

			if(	!k &&
				MMLOG_IS_ENABLED(mm))
			{
				const MovementClientInputStep* mciStepLast = mciStep;
				
				FOR_BEGIN(j, (S32)stepCountToConsolidate);
				{
					mciStepLast = mciStepLast->next;
				}
				FOR_END;
				
				assert(mciStepLast);
				
				mmLog(	mm,
						NULL,
						"[net.sync] Consolidating %d/%d steps (%d, %d) (%s)",
						stepCountToConsolidate,
						mc->mciStepList.count,
						mciStepNext->pc.client,
						mciStepLast->pc.client,
						reason);
			}
			
			// Create a step to consolidate into if there isn't already one.
			
			if(!miStepCur){
				mmInputStepCreate(mc, &miStepCur);
				
				miStepCur->mm = mm;
				miStepCur->mciStep = mciStep;
				
				eaPush(	&mciStep->miStepsMutable,
						miStepCur);
			}
			
			if(miStepNext){
				// Log some more.

				if(MMLOG_IS_ENABLED(mm)){
					mmLogConsolidatedEvents(mm, miStepNext);
				}
													
				// Consolidate controls.

				if(miStepNext->mieList.head){
					if(miStepCur->mieList.tail){
						miStepCur->mieList.tail->next = miStepNext->mieList.head;
						miStepNext->mieList.head->prev = miStepCur->mieList.tail;
					}else{
						miStepCur->mieListMutable.head = miStepNext->mieList.head;
					}
					
					miStepCur->mieListMutable.tail = miStepNext->mieList.tail;
					
					ZeroStruct(&miStepNext->mieListMutable);
				}

				// Destroy the old step.
				
				mmInputStepReclaim(mc, miStepNext);
			}
			
			miStepCur->pc.client = mciStepNext->pc.client;
		}
		EARRAY_FOREACH_END;
		
		// Remove mciStepNext.
		
		assert(!eaSize(&mciStepNext->miSteps));
		
		mciStep->next = mciStepNext->next;
		
		if(mciStep->next){
			mciStep->next->prev = mciStep;
		}else{
			mc->mciStepListMutable.tail = mciStep;
		}
		
		assert(mc->mciStepList.count);
		mc->mciStepListMutable.count--;
		
		mciStep->pc.client = mciStepNext->pc.client;

		if (mc->available.mciStepList.count < MAX_AVAILABLE_MCI_STEPS) {
			// Add to available list.
		
			if(!mc->available.mciStepList.head){
				mc->available.mciStepListMutable.head = mciStepNext;
			}else{
				mc->available.mciStepList.tail->next = mciStepNext;
			}

			mciStepNext->prev = mc->available.mciStepList.tail;
			mc->available.mciStepListMutable.tail = mciStepNext;
			mciStepNext->next = NULL;
			++mc->available.mciStepListMutable.count;
		} else {
			// Simply clean it up
			assert(!eaSize(&mciStepNext->miSteps));
			eaDestroy(&mciStepNext->miStepsMutable);
			SAFE_FREE(mciStepNext);
		}
	}
	FOR_END;
}

static void mmClientCheckForInputOverBuffering(	MovementClient* mc,
												const U32 unsentStepCount,
												MovementClientInputStep* mciStepFirstUnsent,
												MovementClientStats* stats,
												MovementClientStatsFrame* frame)
{
	U32 spcBehind;
	
	if(!mgState.flags.isServer){
		return;
	}

	spcBehind =	stats->spcNext ?
					mgState.fg.frame.next.pcStart -
						stats->spcNext +
						stats->forcedStepCount * MM_PROCESS_COUNTS_PER_STEP:
					U32_MAX;

	if(mgState.debug.activeLogCount){
		EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
		{
			MovementManager* mm = mc->mcmas[i]->mm;
			
			mmLog(	mm,
					NULL,
					"[fg.beforeSimSleeps] Queueing steps: unsentStepCount %u, spcBehind %u",
					unsentStepCount,
					spcBehind);
		}
		EARRAY_FOREACH_END;
	}

	MIN1(stats->minUnsentSteps, unsentStepCount);
	MIN1(stats->spcMinBehind, spcBehind);
	
	if(unsentStepCount){
		stats->flags.hadLeftOverSteps = 1;
	}
	
	if(spcBehind){
		stats->flags.wasBehind = 1;
	}

	if(frame){
		frame->serverStepCount = mgState.fg.frame.cur.stepCount;
		frame->leftOverSteps = unsentStepCount;
		frame->behind = spcBehind == U32_MAX ? 100 : spcBehind;
	}

	stats->stepAcc += mgState.fg.frame.cur.stepCount;

	if(stats->stepAcc >= MM_STEPS_PER_SECOND){
		const U32 maxAllowedUnsentSteps = 1 + stats->inputBufferSize;
		
		stats->stepAcc = 0;

		if(frame){
			frame->flags.isCorrectionFrame = 1;
		}
		
		if(stats->flags.wasBehind){
			if(++stats->wasBehindPeriodCount >= 2){
				stats->skipSteps++;
				stats->wasBehindPeriodCount = 0;
			}
		}else{
			stats->wasBehindPeriodCount = 0;
		}
		
		if(	stats->spcMinBehind
			||
			stats->flags.wasBehind &&
			!stats->flags.hadLeftOverSteps)
		{
			// Ran out of steps every frame, so re-sync to the server time.
			
			stats->spcNext = 0;
		}

		// Reset the accumulation.

		stats->spcMinBehind = U32_MAX;
		stats->flags.wasBehind = 0;
		stats->flags.hadLeftOverSteps = 0;

		if(stats->minUnsentSteps > maxAllowedUnsentSteps){
			if(++stats->hadUnsentStepsEachFramePeriodCount >= 1){
				// Every frame had unsent steps, so consolidate some of them.
				
				U32 stepCountToConsolidate = stats->minUnsentSteps - maxAllowedUnsentSteps;
				
				MIN1(stepCountToConsolidate, unsentStepCount - 1);

				mmClientConsolidateInputSteps(	mc,
												mciStepFirstUnsent,
												stepCountToConsolidate,
												"over buffered");

				if(frame){
					frame->consolidateStepsLate = stepCountToConsolidate;
				}
			}
		}else{
			stats->hadUnsentStepsEachFramePeriodCount = 0;

			if(	stats->inputBufferSize &&
				stats->minUnsentSteps < stats->inputBufferSize / 2)
			{
				// Dipped under the buffer size, so skip a few steps to force it back up.
				
				if(FALSE_THEN_SET(stats->flags.justSkippedSteps)){
					stats->skipSteps = stats->inputBufferSize - stats->minUnsentSteps;
				}
			}else{
				stats->flags.justSkippedSteps = 0;
			}
		}

		stats->minUnsentSteps = U32_MAX;
	}

	if(frame){
		frame->skipSteps = stats->skipSteps;
	}
}

static S32 mmClientCheckForWayTooManyInputSteps(MovementClient* mc,
												const MovementClientStats* stats,
												U32 firstUnsentStepIndex,
												MovementClientInputStep* mciStepFirstUnsent,
												MovementClientStatsFrame* frame)
{
	const U32	unsentStepCount = mc->mciStepList.count - firstUnsentStepIndex;
	const U32	maxUnsentStepCount = MM_STEPS_PER_SECOND + stats->inputBufferSize;
	U32			stepCountToConsolidate;
	
	if(unsentStepCount <= maxUnsentStepCount){
		return 0;
	}
	
	stepCountToConsolidate = unsentStepCount - maxUnsentStepCount;

	if(frame){
		frame->consolidateStepsEarly = stepCountToConsolidate;
	}
	
	mmClientConsolidateInputSteps(	mc,
									mciStepFirstUnsent,
									stepCountToConsolidate,
									"way too many");
	
	return 1;
}

static void mmClientSendManagerInputStepsToBG(	MovementClient* mc,
												MovementClientInputStep* mciStep,
												MovementClientStats* stats)
{
	if(mciStep){
		mciStep->flags.sentToBG = 1;
	}

	EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
	{
		MovementClientManagerAssociation*	mcma = mc->mcmas[i];
		MovementManager*					mm = mcma->mm;
		MovementThreadData*					td = MM_THREADDATA_FG(mm);
		MovementInputStep*					miStep = NULL;
		
		// Find the miStep for this mm.
		
		if(mciStep){
			EARRAY_CONST_FOREACH_BEGIN(mciStep->miSteps, j, jsize);
			{
				MovementInputStep* miStepCur = mciStep->miSteps[j];
				
				if(miStepCur->mm == mm){
					miStep = miStepCur;
				}
			}
			EARRAY_FOREACH_END;
		}
			
		// If it didn't exist, create it and connect it to this mciStep.
		
		if(!miStep){
			mmInputStepCreate(mc, &miStep);
		
			miStep->mm = mm;
			miStep->mciStep = mciStep;
			
			if(mciStep){
				eaPush(&mciStep->miStepsMutable, miStep);
			}
		}else{
			// Check for movement key presses.

			mmCheckForMovementKeysFG(mm, miStep);
		}
		
		// Copy the process count struct.
		
		if(mciStep){
			miStep->pc = mciStep->pc;
		}else{
			miStep->pc.client = stats->cpcLastSent;
			miStep->pc.server = stats->spcLastSent;
			miStep->fg.flags.isForced = 1;
		}

		// Check on some flags.

		assert(!miStep->bg.flags.finished);
		assert(!miStep->bg.flags.inRepredict);
		assert(!miStep->fg.flags.removedFromBG);
		
		// Send step to the BG.

		if(!mgState.flags.noLocalProcessing){
			eaPush(	&td->toBG.miStepsMutable,
					miStep);
		}else{
			miStep->fg.flags.removedFromBG = 1;
		}
	}
	EARRAY_FOREACH_END;
}

static S32 mmClientGetNextInputStepSPC(	MovementClient* mc,
										MovementClientStats* stats,
										MovementClientStatsFrame* frame,
										U32* spcToSendInOut,
										U32* remainingStepsToSendInOut,
										MovementClientInputStep* mciStep)
{
	S32 done = 0;
	
	if(!mgState.flags.isServer){
		// On the client, just use the current spc.

		mciStep->pc.server = *spcToSendInOut;

		*spcToSendInOut += MM_PROCESS_COUNTS_PER_STEP;
		(*remainingStepsToSendInOut)--;
		
		return 1;
	}

	// On the server, use the correct SPC for this step (not necessarily same as current).

	while(1){
		U32 spcNext = stats->spcNext;

		// Check if spcNext was never set or it's more than a second behind.

		if(	!spcNext ||
			subS32(mgState.fg.frame.cur.pcStart, spcNext) > MM_PROCESS_COUNTS_PER_SECOND)
		{
			stats->spcNext = spcNext = mgState.fg.frame.cur.pcStart;
		}

		// Check if the client's SPC has caught up to the server's SPC.

		if(spcNext == *spcToSendInOut){
			if(*remainingStepsToSendInOut <= 0){
				return 0;
			}

			*spcToSendInOut += MM_PROCESS_COUNTS_PER_STEP;
			(*remainingStepsToSendInOut)--;
		}

		stats->spcNext += MM_PROCESS_COUNTS_PER_STEP;
		
		if(stats->skipSteps){
			stats->skipSteps--;
			continue;
		}

		// Update the catchup PC.

		if(subS32(spcNext, mgState.fg.frame.cur.pcCatchup) < 0){
			mgState.fg.frame.cur.pcCatchup = spcNext;
		}

		// Set the SPC for this step.

		if(mciStep){
			mciStep->pc.server = spcNext;
		}
		
		// Log used steps.
		
		if(frame){
			if(mciStep){
				frame->usedSteps++;
			}else{
				frame->forcedSteps++;
			}
		}
		
		return 1;
	}
}

static void mmClientUpdateSyncInputValues(	MovementClient* mc,
											MovementClientThreadData* mctd,
											MovementClientInputStep* mciStep,
											MovementClientStats* stats)
{
	if(!mgState.flags.isServer){
		return;
	}
	
	mctd->fg.netSend.flags.hasStateBG = 1;

	if(mciStep){
		stats->forcedStepCount = 0;
		stats->cpcLastSent = mciStep->pc.client;
		stats->spcLastSent = mciStep->pc.server;
	}else{
		stats->forcedStepCount++;
		stats->spcLastSent = stats->spcNext - MM_PROCESS_COUNTS_PER_STEP;
	}

	mctd->fg.netSend.sync.cpc = stats->cpcLastSent;
	mctd->fg.netSend.sync.spc = stats->spcLastSent;
	mctd->fg.netSend.sync.forcedStepCount = stats->forcedStepCount;
}

static void mmClientSendInputStepsToBG(MovementClient* mc){
	U32							firstUnsentStepIndex;
	U32							spcToSend;
	U32							remainingStepsToSend = mgState.fg.frame.cur.stepCount;
	U32							stepCount = mc->mciStepList.count;
	MovementClientStats*		stats = mgState.flags.isServer ? &mc->stats : NULL;
	MovementClientThreadData*	mctd = mc->threadData + MM_FG_SLOT;
	MovementClientInputStep*	mciStep;
	MovementClientInputStep*	mciStepFirstUnsent = mc->mciStepList.head;
	MovementClientStatsFrames*	frames = SAFE_MEMBER(stats, frames);
	MovementClientStatsFrame*	frame = NULL;
	const S32					doForceSim =	mgState.flags.isServer &&
												(	mc->mmForcedSimCount ||
													mgState.fg.flags.forceSimEnabled);

	PERFINFO_AUTO_START_FUNC();
	
	if(	frames &&
		frames->count < beaUSize(&frames->frames))
	{
		frame = frames->frames + frames->count++;
		ZeroStruct(frame);
	}

	mctd->fg.netSend.flags.hasStateBG = 0;

	spcToSend =	mgState.fg.frame.cur.pcStart +
				mgState.fg.netReceive.cur.offset.clientToServerSync;

	// Find the first unsent step.

	for(mciStep = mc->mciStepList.tail, firstUnsentStepIndex = mc->mciStepList.count;
		mciStep;
		mciStep = mciStep->prev, firstUnsentStepIndex--)
	{
		if(mciStep->flags.sentToBG){
			mciStepFirstUnsent = mciStep->next;
			break;
		}
	}
	
	// Reset the miSteps array for each mm.
	
	EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
	{
		MovementManager*	mm = mc->mcmas[i]->mm;
		MovementThreadData* td = MM_THREADDATA_FG(mm);
		
		eaSetSize(&td->toBG.miStepsMutable, 0);

		mmLog(	mm,
				NULL,
				"[net.sync] Sending %u input steps to BG,"
				" %u/%u steps unsent c(%u, %u),"
				" next s%u",
				remainingStepsToSend,
				stepCount - firstUnsentStepIndex,
				stepCount,
				SAFE_MEMBER(mc->mciStepList.head, pc.client),
				SAFE_MEMBER(mc->mciStepList.tail, pc.client),
				stats ? stats->spcNext : spcToSend);
	}
	EARRAY_FOREACH_END;
	
	// Conslidate inputs before sending to BG.
	
	if(mgState.flags.isServer){
		// Check if there were forced steps.
		
		while(	stats->forcedStepCount &&
				SAFE_MEMBER(mciStepFirstUnsent, next))
		{
			stats->forcedStepCount--;

			mmClientConsolidateInputSteps(	mc,
											mciStepFirstUnsent,
											1,
											"forced step");

			stepCount = mc->mciStepList.count;
		}
		
		// Consolidate input steps if there are way too many.

		if(mmClientCheckForWayTooManyInputSteps(mc,
												stats,
												firstUnsentStepIndex,
												mciStepFirstUnsent,
												frame))
		{
			stepCount = mc->mciStepList.count;
		}
	}

	// Send the steps to the BG.

	for(mciStep = mciStepFirstUnsent;
		mciStep ||
			doForceSim;
		mciStep = SAFE_MEMBER(mciStep, next))
	{
		// Set the spc for this step.

		if(!mmClientGetNextInputStepSPC(mc,
										stats,
										frame,
										&spcToSend,
										&remainingStepsToSend,
										mciStep))
		{
			break;
		}
		
		// Set sync values so in two frames we'll know we have a BG state to send to client.

		mmClientUpdateSyncInputValues(mc, mctd, mciStep, stats);
		
		// Send input steps to each mm.

		mmClientSendManagerInputStepsToBG(mc, mciStep, stats);

		// Goto next unsent mciStep.

		if(mciStepFirstUnsent){
			firstUnsentStepIndex++;
			mciStepFirstUnsent = mciStepFirstUnsent->next;
		}

		// Check if done.

		if(	!remainingStepsToSend &&
			(	!stats ||
				stats->spcNext == spcToSend))
		{
			break;
		}
	}

	// Check if there has been too much buffered for a while.

	mmClientCheckForInputOverBuffering(	mc,
										stepCount - firstUnsentStepIndex,
										mciStepFirstUnsent,
										stats,
										frame);
	
	PERFINFO_AUTO_STOP();
}

static void mmSendMsgsCreateToBG(	MovementManager* mm,
									MovementThreadData* td)
{
	S32 isFirst = 1;
	U32 spcFirst = 0;
	U32 spcLast = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	mmLog(	mm,
			NULL,
			"[fg.toBG] Sending CREATE_TOBG to requesters.");

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester* mr = mm->fg.requesters[i];

		if(mr->fg.flags.destroyed){
			continue;
		}

		if(TRUE_THEN_RESET(mr->fg.flagsMutable.handleCreateToBG)){
			if(TRUE_THEN_RESET(isFirst)){
				if(mm->fg.flags.isAttachedToClient){
					S32 mcStepCount = eaSize(&td->toBG.miSteps);

					assert(mcStepCount);

					spcFirst = td->toBG.miSteps[0]->pc.server;
					spcLast = td->toBG.miSteps[mcStepCount - 1]->pc.server;
				}else{
					assert(mgState.fg.frame.cur.stepCount);

					spcFirst =	mgState.fg.frame.cur.pcStart +
								mgState.fg.netReceive.cur.offset.clientToServerSync;

					spcLast =	spcFirst
								+
								(mgState.fg.frame.cur.stepCount - 1) *
								MM_PROCESS_COUNTS_PER_STEP;
				}
			}
			
			MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_CREATE_TOBG);
			{
				MovementRequesterMsgPrivateData	pd;
				
				mrLog(mr, NULL, "Sending msg CREATE_TOBG.");

				mmRequesterMsgInitFG(&pd, NULL, mr, MR_MSG_FG_CREATE_TOBG);

				pd.msg.in.fg.createToBG.spc.first = spcFirst;
				pd.msg.in.fg.createToBG.spc.last = spcLast;

				pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_FG_SLOT);

				// Think of this like an abstracted handler system.  Someone is on the other end of this request, probably aiMovementMsgHandler.
				// This message will be handled immediately, like a virtual function call.
				mmRequesterMsgSend(&pd);
			}
			MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_CREATE_TOBG);
		}
	}
	EARRAY_FOREACH_END;

	mmLog(	mm,
			NULL,
			"[fg.toBG] Done sending CREATE_TOBG to requesters.");

	PERFINFO_AUTO_STOP();
}

static void mmCopySyncToBG(	MovementManager* mm,
							MovementThreadData* td)
{
	// There are 3 scenarios for this function:
	// 1. Server: Non-client mm with hasRequesterSync=1 set.
	// 2. Server: Non-client mm with hasRequesterSync=0 and mrHasSyncToQueue=1.
	//    This is a formerly client mm that had a sync queued and then was detached.
	// 3. Server: Client mm with mrHasSyncToQueue=1.
	
	const S32 useQueueing = mm->fg.flags.isAttachedToClient &&
							!td->toBG.flags.applySyncNow;
	
	if(	!mm->fg.flags.mrHasSyncToQueue &&
		(	useQueueing ||
			!mm->fg.flags.mrHasSyncToBG))
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	mmLog(	mm,
			NULL,
			"[fg.beforeSimSleeps] Copying requester syncs to BG.");
	
	if(mm->fg.flags.isAttachedToClient){
		mm->fg.flagsMutable.mrHasSyncToQueue = 0;
	}else{
		mm->fg.flagsMutable.mrHasSyncToBG = 0;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester*	mr = mm->fg.requesters[i];
		void*				syncToBG;
		void*				syncPublicToBG;
		
		if(	mr->fg.flags.sentRemoveToBG
			||
			!mr->fg.flags.hasSyncToQueue &&
			(	useQueueing ||
				!mr->fg.flags.hasSyncToBG))
		{
			mrLog(	mr,
					NULL,
					"[fg.beforeSimSleeps] Not copying sync to BG (%d, %d).",
					mr->fg.flags.hasSyncToQueue,
					mr->fg.flags.hasSyncToBG);

			continue;
		}

		if(mm->fg.flags.isAttachedToClient){
			mr->fg.flagsMutable.hasSyncToQueue = 0;
		}else{
			mr->fg.flagsMutable.hasSyncToBG = 0;
		}
		
		mrLog(	mr,
				NULL,
				"[fg.beforeSimSleeps] Copying sync to BG.");

		// Copy sync state to BG.

		if(useQueueing){
			// Client mm.
			
			syncToBG = mr->userStruct.sync.fgToQueue;
			syncPublicToBG = mr->userStruct.syncPublic.fgToQueue;
		}else{
			// Non-client mm.
			
			syncToBG = mr->userStruct.sync.fg;
			syncPublicToBG = mr->userStruct.syncPublic.fg;
		}
		
		if(syncToBG){
			MovementRequesterThreadData* mrtd = MR_THREADDATA_FG(mr);

			td->toBG.flagsMutable.hasToBG = 1;
			td->toBG.flagsMutable.mrHasUpdate = 1;

			if(	FALSE_THEN_SET(mrtd->toBG.flagsMutable.hasSync) &&
				FALSE_THEN_SET(mrtd->toBG.flagsMutable.hasUpdate))
			{
				mmExecListAddHead(	&td->toBG.melRequesters,
									&mrtd->toBG.execNode);
			}

			if(	!useQueueing &&
				mr->fg.flags.needsAfterSync &&
				FALSE_THEN_SET(mm->fg.flagsMutable.mrNeedsAfterSync))
			{
				mmHandleAfterSimWakesIncFG(mm, "mrNeedsAfterSync", __FUNCTION__);
			}

			mmStructAllocAndCopy(	mr->mrc->pti.sync,
									mrtd->toBG.userStruct.sync,
									syncToBG,
									mm);

			if(syncPublicToBG){
				mmStructAllocAndCopy(	mr->mrc->pti.syncPublic,
										mrtd->toBG.userStruct.syncPublic,
										syncPublicToBG,
										mm);
			}
		}
	}
	EARRAY_FOREACH_END;

	mmLog(	mm,
			NULL,
			"[fg.beforeSimSleeps] Done copying requester syncs to BG.");

	PERFINFO_AUTO_STOP();
}

static void mmFindClientInputStep(	MovementClientInputStep** mciStepOut,
									U32 cpc)
{
	MovementClientInputStep* mciStep;
	
	for(mciStep = mgState.fg.mc.mciStepList.tail;
		mciStep;
		mciStep = mciStep->prev)
	{
		if(mciStep->pc.client == cpc){
			*mciStepOut = mciStep;
			return;
		}
	}
	
	assert(0);
}

static void mmProcessUnsplitInputsIntoInputSteps(	MovementManager* mm,
													const FrameLockedTimer* flt)
{
	MovementInputUnsplitQueue*	q = mm->fg.mcma->inputUnsplitQueue;
	U32							milliseconds;
	U32							deltaMilliseconds;
	F32							deltaSeconds;
	
	frameLockedTimerGetCurTimes(flt,
								NULL,
								&milliseconds,
								&deltaMilliseconds);

	deltaSeconds = mgState.fg.frame.cur.stepCount * MM_SECONDS_PER_STEP + 0.f;
	
	FOR_BEGIN(i, (S32)mgState.fg.frame.cur.stepCount);
	{
		MovementInputStep* miStep;

		mmInputStepCreate(mm->fg.mcma->mc, &miStep);

		miStep->mm = mm;
		
		miStep->pc.client =	mgState.fg.frame.cur.pcStart +
							i * MM_PROCESS_COUNTS_PER_STEP;
		
		mmFindClientInputStep(	&miStep->mciStep,
								miStep->pc.client);

		miStep->pc.serverSync = miStep->mciStep->pc.serverSync;

		assert(!miStep->mciStep->flags.sentToBG);
		assert(!miStep->mciStep->flags.sentToServer);
		
		eaPush(	&miStep->mciStep->miStepsMutable,
				miStep);
				
		// Add events that are in the right timeframe.
		{
			MovementInputEvent* mieHead = q->mieList.head;
			U32					msProcessUntil;
			F32					secondsBeforeCur =	MM_SECONDS_PER_STEP *
													(mgState.fg.frame.cur.stepCount - i - 1);

			msProcessUntil =	milliseconds -
								(S32)(	deltaSeconds ?
											deltaMilliseconds * secondsBeforeCur / deltaSeconds :
											0);

			while(q->mieList.head){
				MovementInputEvent* mie = q->mieList.head;
				
				S32 timeDiff = subS32(mie->msTime, msProcessUntil);
				
				if(	timeDiff > 0 &&
					i != (S32)mgState.fg.frame.cur.stepCount - 1)
				{
					break;
				}
				
				q->mieListMutable.head = q->mieList.head->next;
			}
			
			if(	mieHead &&
				mieHead != q->mieList.head)
			{
				mieHead->prev = NULL;
				miStep->mieListMutable.head = mieHead;
				miStep->mieListMutable.tail = q->mieList.head ?
												q->mieList.head->prev :
												q->mieList.tail;
				miStep->mieList.tail->next = NULL;
			}

			if(!q->mieList.head){
				q->mieListMutable.tail = NULL;
			}
		}
	}
	FOR_END;
}

static void mmClientCreateInputSteps(const FrameLockedTimer* flt){
	MovementClient* mc = &mgState.fg.mc;
	
	if(!mgState.fg.frame.cur.stepCount){
		return;
	}

	mgState.fg.mc.mciStepListMutable.unsentCount += mgState.fg.frame.cur.stepCount;

	FOR_BEGIN(i, (S32)mgState.fg.frame.cur.stepCount);
	{
		MovementClientInputStep* mciStep;
		
		if(mc->available.mciStepList.head){
			mciStep = mc->available.mciStepList.head;
			mc->available.mciStepListMutable.head = mciStep->next;
			
			if(!mc->available.mciStepList.head){
				mc->available.mciStepListMutable.tail = NULL;
			}
			
			ZeroStruct(&mciStep->flags);

			assert(mc->available.mciStepListMutable.count);
			--mc->available.mciStepListMutable.count;
		}else{
			mciStep = callocStruct(MovementClientInputStep);
		}
		
		ZeroStruct(&mciStep->pc);

		mciStep->pc.client =	mgState.fg.frame.cur.pcStart +
								i * MM_PROCESS_COUNTS_PER_STEP;

		if(TRUE_THEN_RESET(mgState.fg.netReceiveMutable.flags.setSyncProcessCount)){
			mciStep->pc.serverSync = mgState.fg.netReceive.sync.spc;

			if(mgState.debug.activeLogCount){
				EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, j, jsize);
				{
					MovementManager* mm = mc->mcmas[j]->mm;
					
					mmLog(	mm,
							NULL,
							"[fg.input] Setting server sync: c%d/s%d",
							mciStep->pc.client,
							mciStep->pc.serverSync);
				}
				EARRAY_FOREACH_END;
			}
		}

		if(!mc->mciStepList.head){
			mgState.fg.mc.mciStepListMutable.head = mciStep;
			mciStep->prev = NULL;
		}else{
			mciStep->prev = mc->mciStepList.tail;
			mc->mciStepList.tail->next = mciStep;
		}
		
		mciStep->next = NULL;
		mc->mciStepListMutable.tail = mciStep;
		mc->mciStepListMutable.count++;
	}
	FOR_END;

	EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
	{
		MovementManager* mm = mc->mcmas[i]->mm;
		
		mmProcessUnsplitInputsIntoInputSteps(mm, flt);
	}
	EARRAY_FOREACH_END;
}

void mmSetLocalProcessing(S32 enabled){
	mgState.flagsMutable.noLocalProcessing = !enabled;
	
	if(mgState.flagsMutable.noLocalProcessing){
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
		{
			MovementManager* mm = mgState.fg.managers[i];
			
			mm->fg.flagsMutable.posNeedsForcedSetAck = 0;
			mm->fg.flagsMutable.rotNeedsForcedSetAck = 0;
		}
		EARRAY_FOREACH_END;
	}
}

static void mmUpdateInternalTimersCallback(	const FrameLockedTimer* flt,
											U32* pcDeltaOut,
											U32* pcPrevOut)
{
	frameLockedTimerGetProcesses(flt, NULL, pcDeltaOut, pcPrevOut, NULL);
}

static void mmSingleFrame(	const FrameLockedTimer* flt,
							U32* pcDeltaOut,
							U32* pcPrevOut)
{
	static U32 current = 0;

	*pcDeltaOut = MM_PROCESS_COUNTS_PER_STEP;
	*pcPrevOut = current;

	current += *pcDeltaOut;
}

static void mmUpdateInternalTimersBeforeSwap(const FrameLockedTimer* flt){
	U32 prevProcesses;
	U32 deltaProcesses;
	U32 deltaSteps;
	U32 startProcessCount;
	U32 totalFrames;

	frameLockedTimerGetTotalFrames(flt, &totalFrames);
	
	if(totalFrames == mgState.fg.frame.cur.frameIndex){
		return;
	}

	if(!mgState.cb.getProcesses){
		// This preserves functionality in non beacon stuff.

		if(beaconIsBeaconizer()){
			mgState.cb.getProcesses = mmSingleFrame;
		}else{
			mgState.cb.getProcesses = mmUpdateInternalTimersCallback;
		}
	}

	mgState.cb.getProcesses(flt, &deltaProcesses, &prevProcesses);
	
	// "prevProcesses" is the last process count to be processed, so set it up so that
	// the next set will be prevProcesses + 1 to prevProcesses + deltaProcesses

	if(prevProcesses % MM_PROCESS_COUNTS_PER_STEP){
		mgState.fg.flagsMutable.doProcessIfValidStepInit = 0;
	}else{
		mgState.fg.flagsMutable.doProcessIfValidStepInit = 1;
	}

	deltaSteps =	(prevProcesses % MM_PROCESS_COUNTS_PER_STEP + deltaProcesses) /
					MM_PROCESS_COUNTS_PER_STEP;

	#if 0
	// This is a test of the timer.
	{
		static U32 spcLast;
		static F32 lastInterp;

		if(spcLast){
			if(spcLast == prevProcesses){
				assert(mgState.fg.localView.outputInterp.forward >= lastInterp);
			}else{
				assert(prevProcesses > spcLast);
			}
		}

		spcLast = prevProcesses;
		lastInterp = mgState.fg.localView.outputInterp.forward;
	}
	#endif

	startProcessCount = (prevProcesses + MM_PROCESS_COUNTS_PER_STEP);
	startProcessCount -= (startProcessCount % MM_PROCESS_COUNTS_PER_STEP);

	mgState.fg.frame.prev = mgState.fg.frame.cur;

	mgState.fg.frame.cur.pcStart = startProcessCount;
	mgState.fg.frame.cur.stepCount = deltaSteps;
	
	if(mgState.fg.frame.cur.stepCount){
		mgState.fg.frame.cur.pcNetSend =	mgState.fg.frame.cur.pcStart
											+
											(mgState.fg.frame.cur.stepCount - 1) *
											MM_PROCESS_COUNTS_PER_STEP;
	}

	mgState.fg.frame.cur.pcCatchup = mgState.fg.frame.cur.pcStart;

	//assert(	!mgState.fg.frame.next.pcStart ||
	//		mgState.fg.frame.cur.pcStart == mgState.fg.frame.next.pcStart);

	mgState.fg.frame.next.pcStart =	startProcessCount
									+
									deltaSteps *
									MM_PROCESS_COUNTS_PER_STEP;

	//printf(	"prev60 %d, delta60 %d, start %d, next %d\n",
	//		prevProcesses,
	//		deltaProcesses,
	//		mgState.fg.frame.cur.pcStart,
	//		mgState.fg.frame.next.pcStart);
}

static void mmRequestersProcessQueuedDestroysFG(MovementManager* mm,
												MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();

	mm->fg.flagsMutable.mrNeedsDestroy = 0;

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester* mr = mm->fg.requesters[i];

		if(!mr->fg.flags.destroyed){
			continue;
		}
		
		if(	mr->fg.flags.inListBG
			||
			mgState.flags.isServer &&
			mgState.fg.clients &&
			mr->fg.flags.sentCreate &&
			!mr->fg.flags.sentDestroy)
		{
			mm->fg.flagsMutable.mrNeedsDestroy = 1;
		}else{
			MovementRequesterThreadData* mrtd = MR_THREADDATA_FG(mr);

			ASSERT_TRUE_AND_RESET(mr->fg.flagsMutable.inList);
			eaRemove(&mm->fg.requestersMutable, i);

			i--;
			size--;

			assert(!mrtd->toBG.flagsMutable.hasUpdate);
			
			mrForcedSimEnableFG(mr, 0);

			mmRequesterDestroyInternal(mm, mr);
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

static void mmReclaimNetOutputs(MovementManager* mm,
								MovementThreadData* td)
{
	MovementThreadData*	tdBG = MM_THREADDATA_BG(mm);

	PERFINFO_AUTO_START_FUNC();

	if(	tdBG->toBG.net.outputList.head &&
		tdBG->toBG.net.outputList.head != mm->fg.net.outputList.head)
	{
		MovementNetOutput*const noHead = tdBG->toBG.net.outputList.head;
		MovementNetOutput*const noTail = noHead->prev;
		
		assert(noTail);
		assert(noTail->next == noHead);
		assert(mm->fg.net.outputList.head);
		assert(!mm->fg.net.outputList.head->prev);
		
		mmNetOutputListAddTail(	&mm->fg.net.available.outputListMutable,
								mm->fg.net.outputList.head);
								
		noTail->next = NULL;
								
		mmNetOutputListSetTail( &mm->fg.net.available.outputListMutable,
								noTail);
								
		noHead->prev = NULL;
		mm->fg.net.outputListMutable.head = noHead;
	}
	
	td->toBG.net.outputListMutable = mm->fg.net.outputList;
	
	while(td->toBG.net.outputList.head != mm->fg.net.outputList.tail){
		if(	subS32(	td->toBG.net.outputList.head->pc.server,
					mgState.fg.frame.cur.spcOldestToKeep) >= 0
			||
			!mgState.flags.isServer &&
			subS32(	td->toBG.net.outputList.head->next->pc.server,
					mgState.fg.netView.spcFloor) > 0)
		{
			break;
		}
		
		td->toBG.net.outputListMutable.head = td->toBG.net.outputList.head->next;
	}
	
	PERFINFO_AUTO_STOP();
}

static void mmLogBeforeSimSleepsFG(MovementManager* mm){
	mmLog(	mm,
			NULL,
			"[fg.beforeSimSleeps] BeforeSimSleeps"
			" (prev steps %d, cur steps %d, cur time %ums).",
			mgState.fg.frame.prev.stepCount,
			mgState.fg.frame.cur.stepCount,
			timeGetTime());

	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_LOG_BEFORE_SIMULATION_SLEEPS;
		pd.msg.userPointer = mm->userPointer;
		
		mm->msgHandler(&pd.msg);
	}

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.noCollHandles, i, isize);
	{
		MovementNoCollHandle* nch = mm->fg.noCollHandles[i];

		mmLog(	mm,
				NULL,
				"[fg.beforeSimSleeps] NoCollHandle: %s:%d",
				nch->owner.fileName,
				nch->owner.fileLine);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.collisionSetHandles, i, isize);
	{
		MovementCollSetHandle* mcsh = mm->fg.collisionSetHandles[i];

		mmLog(	mm,
				NULL,
				"[fg.beforeSimSleeps] CollSetHandle: %s:%d",
				mcsh->owner.fileName,
				mcsh->owner.fileLine);
	}
	EARRAY_FOREACH_END;
	
	if(mm->fg.net.flags.noCollision){
		mmLog(	mm,
				NULL,
				"[fg.beforeSimSleeps] NoCollFromServer: enabled");
	}
	
	if(mm->fg.flags.mrHasSyncToQueue){
		mmLog(	mm,
				NULL,
				"[fg.beforeSimSleeps] mrHasSyncToQueue:");

		EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, isize);
		{
			MovementRequester* mr = mm->fg.requesters[i];
			
			if(!mr->fg.flags.hasSyncToQueue){
				continue;
			}
			
			mrLog(	mr,
					NULL,
					"[fg.beforeSimSleeps] \thasSyncToQueue.");
		}
		EARRAY_FOREACH_END;
	}
}

static void mmHandleBeforeSimSleepsFG(MovementManager* mm){
	MovementThreadData* td = MM_THREADDATA_FG(mm);
	S32					startedTimer = 0;

	PERFINFO_AUTO_START_FUNC();

	if(mgState.debug.flags.perEntityTimers){
		startedTimer = mmStartPerEntityTimer(mm);
	}

	// Log some stuff.

	if(MMLOG_IS_ENABLED(mm)){
		mmLogBeforeSimSleepsFG(mm);
	}
	
	mmReclaimNetOutputs(mm, td);
	
	// Destroy old requesters.

	if(mm->fg.flags.mrNeedsDestroy){
		mmRequestersProcessQueuedDestroysFG(mm, td);
	}

	// Update the tail of the net output list.
	
	if(mm->fg.net.outputList.tail){
		mmNetOutputListSetTail(	&td->toBG.net.outputListMutable,
								mm->fg.net.outputList.tail);
	}
	
	// Do repredict stuff.

	if(td->toBG.flags.doRepredict){
		EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
		{
			MovementRequester* mr = mm->fg.requesters[i];
			
			if(mr->fg.flags.createdInFG){
				if(!mr->fg.flags.sentRemoveToBG){
					mr->fg.flagsMutable.destroyedFromBG = 0;
				}
			}else{
				mrDestroy(&mr);
			}
		}
		EARRAY_FOREACH_END;
	}
	
	if(	!mm->fg.flags.destroyed &&
		(	!mm->fg.flags.isAttachedToClient &&
			mgState.fg.frame.cur.stepCount
			||
			mm->fg.flags.isAttachedToClient &&
			(	eaSize(&td->toBG.miSteps) ||
				td->toBG.flags.applySyncNow)
			)
		)
	{
		if(	!mm->fg.flags.isAttachedToClient ||
			eaSize(&td->toBG.miSteps))
		{
			if(TRUE_THEN_RESET(mm->fg.flagsMutable.mrHasHandleCreateToBG)){
				mmSendMsgsCreateToBG(mm, td);
			}
		}
		
		if(	mgState.flags.isServer ||
			mm->flags.isLocal ||
			mm->fg.flags.isInDeathPrediction)
		{
			mmCopySyncToBG(mm, td);
		}
	}

	if(startedTimer){
		PERFINFO_AUTO_STOP();// id group.
	}

	PERFINFO_AUTO_STOP();// FUNC.
}

void mmProcessRawInputOnClientFG(const FrameLockedTimer* flt){
	if(mgState.flags.isServer){
		return;
	}

	PERFINFO_AUTO_START_FUNC_PIX();

	mmUpdateInternalTimersBeforeSwap(flt);

	mmClientCreateInputSteps(flt);

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

static void mmAllHandleBeforeSimSleepsFG(const WorldCollIntegrationMsg* msg){
	PERFINFO_AUTO_START_FUNC();

	mmFinalizeClasses();

	mmAllUpdateLoggingStateToOwners();

	// Update the timers.

	mmUpdateInternalTimersBeforeSwap(msg->fg.beforeSimSleeps.flt);
	
	// Send controls to BG.
	
	if(mgState.flags.isServer){
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.clients, i, isize);
		{
			mmClientSendInputStepsToBG(mgState.fg.clients[i]);
		}
		EARRAY_FOREACH_END;
	}else{
		// Create the client control steps.
		
		mmClientSendInputStepsToBG(&mgState.fg.mc);
	}

	// 

	mgState.fg.frame.cur.spcOldestToKeep =	mgState.fg.frame.cur.pcStart +
											mgState.fg.netReceive.cur.offset.clientToServerSync -
											3 * MM_PROCESS_COUNTS_PER_SECOND;
											
	if(	!mgState.flags.isServer &&
		subS32(	mgState.fg.frame.cur.spcOldestToKeep,
				mgState.fg.netReceive.cur.pc.server) > 0)
	{
		mgState.fg.frame.cur.spcOldestToKeep =	mgState.fg.netReceive.cur.pc.server -
												MM_PROCESS_COUNTS_PER_SECOND;
	}

	// Process all the managers.

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, size);
	{
		MovementManager* mm = mgState.fg.managers[i];

		mmHandleBeforeSimSleepsFG(mm);
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();// FUNC.
}

S32 mmSpaceGetByWorldCollFG(MovementSpace** spaceOut,
							const WorldColl* wc)
{
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.spaces, i, isize);
	{
		MovementSpace* space = mgState.fg.spaces[i];
		
		if(space->wc == wc){
			if(spaceOut){
				*spaceOut = space;
			}
			
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

void mmSetWorldColl(MovementManager* mm,
					const WorldColl* wc)
{
	MovementSpace* space;
	
	if(mmSpaceGetByWorldCollFG(&space, wc)){
		mm->space = space;
	}
}

static void mmWorldCollIntegrationMsgHandler(const WorldCollIntegrationMsg* msg){
	#define FG_MSG_BEGIN	{assert(!mgState.fg.wciMsg);mgState.fg.wciMsg = msg;}((void)0)
	#define FG_MSG_END		{assert(mgState.fg.wciMsg == msg);mgState.fg.wciMsg = NULL;}((void)0)
	#define BG_MSG_BEGIN	{assert(!mgState.bg.wciMsg);mgState.bg.wciMsg = msg;}((void)0)
	#define BG_MSG_END		{assert(mgState.bg.wciMsg == msg);mgState.bg.wciMsg = NULL;}((void)0)
	#define NOBG_MSG_BEGIN	FG_MSG_BEGIN;BG_MSG_BEGIN
	#define NOBG_MSG_END	BG_MSG_END;FG_MSG_END

	switch(msg->msgType){
		xcase WCI_MSG_FG_WORLDCOLL_EXISTS:{
			MovementSpace* space = callocStruct(MovementSpace);
			
			FG_MSG_BEGIN;

			assert(!mmSpaceGetByWorldCollFG(NULL, msg->fg.worldCollExists.wc));
			
			space->wc = msg->fg.worldCollExists.wc;
			
			ARRAY_FOREACH_BEGIN(space->mmGrids, i);
			{
				space->mmGrids[i].space = space;
			}
			ARRAY_FOREACH_END;
			
			eaPush(&mgState.fg.spacesMutable, space);

			FG_MSG_END;
		}
		
		xcase WCI_MSG_NOBG_WORLDCOLL_DESTROYED:{
			MovementSpace* space;

			FG_MSG_BEGIN;

			if(!mmSpaceGetByWorldCollFG(&space, msg->nobg.worldCollDestroyed.wc)){
				FG_MSG_END;
				break;
			}

			// Remove all mms from this space.
			
			EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
			{
				MovementManager* mm = mgState.fg.managers[i];
				
				if(mm->space == space){
					mm->space = NULL;
				}
				
				if(space == SAFE_MEMBER(mm->bg.gridEntry.cell, grid->space)){
					ASSERT_FALSE_AND_SET(mgState.bg.flagsMutable.gridIsWritable);
					mmRemoveFromGridBG(mm);
					ASSERT_TRUE_AND_RESET(mgState.bg.flagsMutable.gridIsWritable);
				}
			}
			EARRAY_FOREACH_END;
			
			// Clean up the grid.
			
			ARRAY_FOREACH_BEGIN(space->mmGrids, j);
			{
				assert(!stashGetCount(space->mmGrids[j].stCells));
				stashTableDestroySafe(&space->mmGrids[j].stCells);
			}
			ARRAY_FOREACH_END;
			
			// Done.
			
			if(eaFindAndRemove(&mgState.fg.spacesMutable, space) < 0){
				assert(0);
			}
			
			SAFE_FREE(space);

			FG_MSG_END;
		}
		
		xcase WCI_MSG_FG_BEFORE_SIM_SLEEPS:{
			FG_MSG_BEGIN;
			mmAllHandleBeforeSimSleepsFG(msg);
			FG_MSG_END;
		}
		
		xcase WCI_MSG_FG_AFTER_SIM_WAKES:{
			FG_MSG_BEGIN;
			mmAllHandleAfterSimWakesFG(msg);
			FG_MSG_END;
		}

		xcase WCI_MSG_NOBG_WHILE_SIM_SLEEPS:{
			NOBG_MSG_BEGIN;
			mmAllHandleWhileSimSleepsNOBG(msg);
			NOBG_MSG_END;
		}
		
		xcase WCI_MSG_NOBG_ACTOR_CREATED:{
			NOBG_MSG_BEGIN;
			mmHandleActorCreatedBG(msg);
			NOBG_MSG_END;
		}

		xcase WCI_MSG_NOBG_ACTOR_DESTROYED:{
			NOBG_MSG_BEGIN;
			mmHandleActorDestroyedBG(msg);
			NOBG_MSG_END;
		}

		xcase WCI_MSG_BG_BETWEEN_SIM:{
			BG_MSG_BEGIN;
			
			mgState.bg.betweenSim.count++;

			mgState.bg.betweenSim.instanceThisFrame = msg->bg.betweenSim.instanceThisFrame;
			mgState.bg.betweenSim.deltaSeconds = msg->bg.betweenSim.deltaSeconds;

			mgState.bg.betweenSim.flags.noProcessThisFrame =
												msg->bg.betweenSim.flags.noProcessThisFrame;
			
			mmAllHandleBetweenSimBG();
										
			BG_MSG_END;
		}
	}
}

void mmCreateWorldCollIntegration(void){
	if(!mgState.wci){
		wcIntegrationCreate(&mgState.wci,
							mmWorldCollIntegrationMsgHandler,
							NULL,
							"Movement");
	}
}

void mmSendMsgsAfterSyncFG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester* mr = mm->fg.requesters[i];
		
		if(TRUE_THEN_RESET(mr->fg.flagsMutable.needsAfterSync)){
			MovementRequesterMsgPrivateData	pd;
			
			mmRequesterMsgInitFG(&pd, NULL, mr, MR_MSG_FG_AFTER_SYNC);
			mmRequesterMsgSend(&pd);
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

// MovementManager create and destroy.

S32 mmCreate(	MovementManager** mmOut,
				MovementManagerMsgHandler msgHandler,
				void* userPointer,
				EntityRef entityRef,
				U32 threadDataSize,
				const Vec3 pos,
				WorldColl* wc)
{
	MovementManager*	mm;
	U32					currentThreadID = GetCurrentThreadId();
	U32					entityIndex = INDEX_FROM_REFERENCE(entityRef);

	mmFinalizeClasses();

	if(!mmOut){
		return 0;
	}

	// Store the current thread as the foreground thread.

	if(!mgState.fg.threadID){
		mmSetIsForegroundThreadForLogging();
		
		mgState.fg.threadID = currentThreadID;
		mgState.fg.flagsMutable.notThreaded = !wcIsThreaded();
	}
	else if(mgState.fg.threadID != currentThreadID){
		devassertmsg(0, "Can't create MovementManager from this thread.");
		return 0;
	}

	// Allocate the mm.

	mm = callocStruct(MovementManager);

	mm->msgHandler = msgHandler;
	mm->userPointer = userPointer;

	if(entityIndex >= ARRAY_SIZE(mgState.bg.entIndexToManager)){
		mm->flagsMutable.isLocal = 1;
	}

	mm->entityRef = entityRef;
	
	if(threadDataSize){
		mmSetUserThreadDataSize(mm, threadDataSize);
	}

	// Set the initial position and rotation.

	ARRAY_FOREACH_BEGIN(mm->threadData, i);
	{
		MovementThreadData* td = mm->threadData + i;

		td->toFG.flagsMutable.posIsAtRest = 1;
		td->toFG.flagsMutable.rotIsAtRest = 1;
		td->toFG.flagsMutable.pyFaceIsAtRest = 1;
	}
	ARRAY_FOREACH_END;
	
	if(	wc &&
		!mmSpaceGetByWorldCollFG(&mm->space, wc))
	{
		assertmsgf(0, "No MovementSpace found for WorldColl 0x%p.", wc);
	}

	if(	mgState.flags.isServer ||
		mm->flags.isLocal)
	{
		if(pos){
			mmSetPositionFG(mm, pos, "mmManagerCreate.initializer");
			mmSetRotationFG(mm, unitquat, "mmManagerCreate.initializer");
		}
		else if(mm->msgHandler){
			MovementManagerMsgPrivateData	pd = {0};
			MovementManagerMsgOut			out = {0};
			
			pd.mm = mm;
			pd.msg.msgType = MM_MSG_FG_QUERY_POS_AND_ROT;
			pd.msg.userPointer = mm->userPointer;
			pd.msg.out = &out;
			
			mm->msgHandler(&pd.msg);
			
			if(out.fg.queryPosAndRot.flags.didSet){
				mmSetPositionFG(mm, out.fg.queryPosAndRot.pos, "mmManagerCreate.cur");
				mmSetRotationFG(mm, out.fg.queryPosAndRot.rot, "mmManagerCreate.cur");
			}
		}
	}

	// Store mm in the global list and send to the background thread.

	eaPush(&mgState.fg.managersMutable, mm);
	mmSetUpdatedToBG(mm);
	
	// Check if logging should be started automatically.

	if(	mgState.flags.logOnCreate
		&&
		(	mgState.debug.logOnCreate.radius <= 0.f ||
			distance3(mgState.debug.logOnCreate.pos, mm->fg.pos) <
				mgState.debug.logOnCreate.radius)
		)
	{
		mmSetDebugging(mm, 1);
	}

	*mmOut = mm;

	return 1;
}

static void mmManagerDestroyVerifyClean(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();

	// Check that the client is cleaned up.

	assert(!mm->fg.mcma);

	// Check that the FG is cleaned up.

	assert(!mm->fg.requesters);
	assert(!mm->fg.resources);
	assert(!mm->fg.net.outputList.head);
	assert(!mm->fg.net.outputList.tail);
	assert(!mm->fg.net.available.outputList.head);
	assert(!mm->fg.net.available.outputList.tail);
	assert(!mm->fg.netSend.outputsEncoded);
	if(mm->fg.disabledHandles){
		printf("Never freed MovementDisabledHandles created at:\n");

		EARRAY_CONST_FOREACH_BEGIN(mm->fg.disabledHandles, i, isize);
		{
			printf(	"%d. %s:%d.\n",
					i + 1,
					mm->fg.disabledHandles[i]->owner.fileName,
					mm->fg.disabledHandles[i]->owner.fileLine);
		}
		EARRAY_FOREACH_END;

		assertmsg(!mm->fg.disabledHandles, "See console for details");
	}
	assert(!mm->fg.noCollHandles);
	assert(!mm->fg.flags.mmrHandlesAlwaysDraw);
	assert(!mm->fg.flags.sentUserThreadDataUpdateToBG);
	assert(!mm->fg.flags.sentUserThreadDataUpdateToBGbit);
	assert(!mm->fg.flags.afterSimOnceFromBG);
	assert(!mm->fg.flags.afterSimOnceFromBGbit);
	assert(!mm->fg.flags.mrNeedsAfterSync);
	assert(!mm->fg.flags.needsNetOutputViewUpdate);
	assert(!mm->fg.flags.mmrNeedsSetState);
	assert(!mm->fg.flags.mmrWaitingForWake);
	assert(!mm->fg.bodyInstances);
	assert(!mm->fg.collisionSetHandles);
	assert(!mm->fg.collisionGroupHandles);
	assert(!mm->fg.collisionGroupBitsHandles);
	assert(!mm->fg.afterSimWakes.count);
	#if MM_TRACK_AFTER_SIM_WAKES
	{
		PERFINFO_AUTO_START("debug free afterSimWakes tracking", 1);
		assert(!stashGetCount(mm->fg.afterSimWakes.stReasons));
		stashTableDestroySafe(&mm->fg.afterSimWakes.stReasons);
		PERFINFO_AUTO_STOP();
	}
	#endif

	// Check that the BG is cleaned up.

	assert(!mm->bg.outputList.head);
	assert(!mm->bg.outputList.tail);
	assert(!mm->bg.available.outputList.head);
	assert(!mm->bg.available.outputList.tail);
	assert(!mm->bg.requesters);
	assert(!mm->bg.gridEntry.cell);
	assert(!mm->bg.resources);
	assert(!mm->bg.miState);
	assert(!mm->bg.predictedSteps);
	assert(!mm->bg.bodyInstances);
	
	// Check that the toFG and toBG are cleaned up.

	ARRAY_FOREACH_BEGIN(mm->threadData, i);
	{
		MovementThreadData* td = mm->threadData + i;

		// toBG.

		assert(!td->toBG.newRequesters);
		assert(!td->toBG.miSteps);
		assert(!td->toBG.updatedResources);
		assert(!td->toBG.net.outputList.head);
		assert(!td->toBG.net.outputList.tail);

		// toFG.

		assert(!td->toFG.newRequesters);
		assert(!td->toFG.outputList.head);
		assert(!td->toFG.outputList.tail);
		assert(!td->toFG.finishedInputSteps);
		assert(!td->toFG.updatedResources);
		assert(!td->toFG.finishedResourceStates);
		assert(!td->toFG.repredicts);
	}
	ARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
	{
		assert(!mm->bg.dataOwner[i]);
	}
	ARRAY_FOREACH_END;

	assert(!mm->fg.offsetInstances);

	ARRAY_FOREACH_BEGIN(mm->userThreadData, i);
	{
		assert(!mm->userThreadData[i]);
	}
	ARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();// FUNC.
}

static void mmDestroyAllInNetOutputList(MovementNetOutputList* nol){
	while(nol->head){
		MovementNetOutput* next = nol->head->next;
		
		mmNetOutputDestroy(&nol->head);
		nol->head = next;
	}
	
	nol->tail = NULL;
}

static void mmDestroyInternal(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	mm->flagsMutable.destroying = 1;

	if(mgState.debug.mmsUserNotified){
		eaFindAndRemove(&mgState.debug.mmsUserNotified, mm);
	}
	
	// Clear the client.
	
	mmDetachFromClient(mm, NULL);

	// Destroy anim bits.
	
	if(mm->fg.view){
		MovementManagerFGView* v = mm->fg.view;

		mmAnimBitListDestroyAll(&v->animValuesMutable);
		mmLastAnimReset(&v->lastAnimMutable);

		// local.

		eaiDestroy(&v->local.stanceBitsMutable);
		mmLastAnimReset(&v->local.lastAnimMutable);
		eaiDestroy(&v->local.spcPerFlagMutable);

		// localNet.

		eaiDestroy(&v->localNet.stanceBitsMutable);

		// net.

		eaiDestroy(&v->net.stanceBitsMutable);
		mmLastAnimReset(&v->net.lastAnimMutable);

		// netUsed.

		eaiDestroy(&v->netUsed.stanceBitsMutable);

		// Done.
		
		SAFE_FREE(mm->fg.view);
	}

	// Destroy all the requesters.

	PERFINFO_AUTO_START("requesters", 1);
	{
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
		{
			MovementRequester* mr = mm->bg.requesters[i];

			ASSERT_TRUE_AND_RESET(mr->bg.flagsMutable.inList);
		}
		EARRAY_FOREACH_END;

		EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, isize);
		{
			MovementRequester* mr = mm->fg.requesters[i];

			ASSERT_TRUE_AND_RESET(mr->fg.flagsMutable.inList);
			mr->fg.flagsMutable.destroyed = 1;

			mmRequesterDestroyInternal(mm, mr);
		}
		EARRAY_FOREACH_END;

		eaDestroy(&mm->fg.requestersMutable);
		eaDestroy(&mm->bg.requestersMutable);
		eaDestroy(&mm->allRequesters);
	}
	PERFINFO_AUTO_STOP();

	// Destroy all the resources.

	mmDestroyAllResourcesFG(mm);

	// Destroy net outputs.

	PERFINFO_AUTO_START("net outputs", 1);
	{
		if(TRUE_THEN_RESET(mm->fg.flagsMutable.needsNetOutputViewUpdate)){
			mmHandleAfterSimWakesDecFG(mm, "needsNetOutputViewUpdate");
		}

		mmDestroyAllInNetOutputList(&mm->fg.net.outputListMutable);
		mmDestroyAllInNetOutputList(&mm->fg.net.available.outputListMutable);

		EARRAY_CONST_FOREACH_BEGIN(mm->fg.netSend.outputsEncoded, i, isize);
		{
			mmNetOutputEncodedDestroy(&mm->fg.netSend.outputsEncodedMutable[i]);
		}
		EARRAY_FOREACH_END;

		eaDestroy(&mm->fg.netSend.outputsEncodedMutable);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("net", 1);
	{
		eaiDestroy(&mm->fg.net.stanceBitsMutable);
		mmLastAnimReset(&mm->fg.net.lastAnimMutable);
		eaiDestroy(&mm->fg.net.lastStored.stanceBitsMutable);
		mmLastAnimReset(&mm->fg.net.lastStored.lastAnimMutable);
	}
	PERFINFO_AUTO_STOP();

	// Destroy the threadData.

	PERFINFO_AUTO_START("threadData", 1);
	{
		ARRAY_FOREACH_BEGIN(mm->threadData, i);
		{
			MovementThreadData* td = mm->threadData + i;

			// toBG.

			eaDestroy(&td->toBG.newRequestersMutable);
			eaDestroy(&td->toBG.miStepsMutable);
			eaDestroy(&td->toBG.updatedResourcesMutable);
			ZeroStruct(&td->toBG.net.outputListMutable);

			if(td->toBG.repredict){
				MovementThreadDataToBGRepredict* r = td->toBG.repredict;

				eaDestroyEx(&r->repredictsMutable,
							mmOutputRepredictDestroyUnsafe);

				mmLastAnimReset(&r->lastAnim);

				SAFE_FREE(td->toBG.repredict);
			}

			// toFG.

			ZeroStruct(&td->toFG.outputListMutable);
			eaDestroy(&td->toFG.repredictsMutable);
			eaDestroy(&td->toFG.newRequestersMutable);
			eaDestroy(&td->toFG.finishedInputStepsMutable);
			eaDestroy(&td->toFG.updatedResourcesMutable);
			eaDestroy(&td->toFG.finishedResourceStatesMutable);
			eaiDestroy(&td->toFG.stanceBitsMutable);
			mmLastAnimReset(&td->toFG.lastAnimMutable);
			SAFE_FREE(td->toFG.predict);

			#if MM_VERIFY_REPREDICTS
				eaiDestroy(&td->toFG.repredictPCs);
			#endif
		}
		ARRAY_FOREACH_END;
	}
	PERFINFO_AUTO_STOP();

	// Stop logging.

	mmSetDebugging(mm, 0);
	
	// Free userThreadData.
	
	PERFINFO_AUTO_START("userThreadData", 1);
	{
		if(mm->userThreadData[0]){
			if(mm->msgHandler){
				ARRAY_FOREACH_BEGIN(mm->userThreadData, i);
				{
					MovementManagerMsgPrivateData pd = {0};
					
					pd.mm = mm;
					pd.msg.msgType = MM_MSG_FG_DESTROY;

					mm->msgHandler(&pd.msg);
				}
				ARRAY_FOREACH_END;
			}

			SAFE_FREE(mm->userThreadData[0]);
			ZeroArray(mm->userThreadData);
		}
	}
	PERFINFO_AUTO_STOP();
	
	// Free some misc stuff.

	SAFE_FREE(mm->lastSetPosInfoString);

	// Check that everything is clean.

	mmManagerDestroyVerifyClean(mm);

	// Remove from animError list.

	if(mgState.animError.st){
		csEnter(&mgState.animError.cs);
		stashRemovePointer(mgState.animError.st, mm, NULL);
		csLeave(&mgState.animError.cs);
	}

	// And free me.

	SAFE_FREE(mm);
	
	PERFINFO_AUTO_STOP();// FUNC.
}

S32 mmDestroy(MovementManager** mmInOut){
	MovementManager* mm = SAFE_DEREF(mmInOut);

	if(mm){
		if(mmIsForegroundThread()){
			PERFINFO_AUTO_START_FUNC();
			
			if(FALSE_THEN_SET(mm->fg.flagsMutable.destroyed)){
				MovementThreadData* td = MM_THREADDATA_FG(mm);
				
				mmDetachFromClient(mm, NULL);

				mm->userPointer = NULL;

				mmSetUpdatedToBG(mm);
				
				mm->fg.debug.frameWhenDestroyed = mgState.frameCount;

				td->toBG.flagsMutable.destroyed = 1;
			}
			
			PERFINFO_AUTO_STOP();
		}

		*mmInOut = NULL;
	}

	return 1;
}

S32 mmSetMsgHandler(MovementManager* mm,
					MovementManagerMsgHandler msgHandler)
{
	// You're never allowed to unset this once you set it.  You CAN change it to a non-NULL.

	if(	!mm ||
		!msgHandler)
	{
		return 0;
	}
	
	mm->msgHandler = msgHandler;
	
	mmSendMsgCollRadiusChangedFG(mm);
	
	return 1;
}

S32 mmSetUserPointer(	MovementManager* mm,
						void* userPointer)
{
	if(!mm){
		return 0;
	}
	
	mm->userPointer = userPointer;
	
	mmSendMsgCollRadiusChangedFG(mm);
	
	return 1;
}

S32 mmSetUserThreadDataSize(MovementManager* mm,
							U32 size)
{
	if(	!mm ||
		!size)
	{
		return 0;
	}
	
	assert(!mm->userThreadData[0]);

	mm->userThreadData[0] = calloc(size * 2, 1);
	mm->userThreadData[1] = (U8*)mm->userThreadData[0] + size;

	return 1;
}

S32 mmGetManagerCountFG(void){
	return eaSize(&mgState.fg.managers);
}

S32 mmGetManagerFG(	U32 index,
					MovementManager** mmOut)
{
	if( !mmOut ||
		index >= eaUSize(&mgState.fg.managers))
	{
		return 0;
	}
	
	*mmOut = mgState.fg.managers[index];
	
	return 1;
}
					
S32 mmDisabledHandleCreate(	MovementDisabledHandle** dhOut,
							MovementManager* mm,
							const char* fileName,
							U32 fileLine)
{
	MovementDisabledHandle* dh;
	
	if(	!mm ||
		mm->fg.flags.destroyed ||
		!dhOut ||
		*dhOut ||
		!mmIsForegroundThread())
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	dh = callocStruct(MovementDisabledHandle);
	
	dh->mm = mm;
	dh->owner.fileName = fileName;
	dh->owner.fileLine = fileLine;

	if(!eaPush(&mm->fg.disabledHandlesMutable, dh)){
		MovementThreadData* td = MM_THREADDATA_FG(mm);

		ASSERT_FALSE_AND_SET(mm->fg.flagsMutable.hasDisabledHandles);
		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.isInactiveUpdated = 1;
		td->toBG.flagsMutable.isInactive = 1;
	}

	mmLog(	mm,
			NULL,
			"[fg.disabledHandles] Creating disabled handle 0x%p (%d now).",
			dh,
			eaSize(&mm->fg.disabledHandles));
	
	*dhOut = dh;
	
	PERFINFO_AUTO_STOP();
	
	return 1;
}
								
S32 mmDisabledHandleDestroy(MovementDisabledHandle** dhInOut){
	MovementDisabledHandle* dh = SAFE_DEREF(dhInOut);
	MovementManager*		mm;
	MovementThreadData*		td;

	if(	!dh ||
		!mmIsForegroundThread())
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	mm = dh->mm;
	td = MM_THREADDATA_FG(mm);

	if(eaFindAndRemove(&mm->fg.disabledHandlesMutable, dh) < 0){
		assert(0);
	}
	
	if(!eaSize(&mm->fg.disabledHandles)){
		eaDestroy(&mm->fg.disabledHandlesMutable);
		
		ASSERT_TRUE_AND_RESET(mm->fg.flagsMutable.hasDisabledHandles);
		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.isInactiveUpdated = 1;
		td->toBG.flagsMutable.isInactive = 0;
	}
	
	td->toBG.flagsMutable.applySyncNow = 1;
	
	mmLog(	mm,
			NULL,
			"[fg.disabledHandles] Destroying disabled handle 0x%p (%d now).",
			dh,
			eaSize(&mm->fg.disabledHandles));

	SAFE_FREE(*dhInOut);

	PERFINFO_AUTO_STOP();
	
	return 1;
}

void mmDebugDraw(	const MovementDrawFuncs* funcs,
					const Vec3 posCamera,
					S32 doDrawBodies,
					S32 doDrawBounds,
					S32 doDrawNetOutputs,
					S32 doDrawOutputs)
{
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager*	mm = mgState.fg.managers[i];
		MovementThreadData*	td = MM_THREADDATA_FG(mm);
		Mat4				mat;
		S32					doDrawOnSelf = 1;
		Vec3				posMM;
		Quat				rotMM;
		
		mmGetPositionFG(mm, posMM);
		
		if(distance3(posCamera, posMM) > mm->fg.bodyRadius + 100){
			doDrawOnSelf = 0;
		}else{
			mmResourcesDebugDrawFG(mm, funcs);
		}
		
		mmGetRotationFG(mm, rotMM);
		
		quatToMat(rotMM, mat);
		copyVec3(posMM, mat[3]);
		
		if(doDrawBodies){
			EARRAY_CONST_FOREACH_BEGIN(mm->fg.bodyInstances, j, jsize);
			{
				const MovementBodyInstance*	bi = mm->fg.bodyInstances[j];
				Mat4						matActor;
				U32							argb = mm->fg.flags.noCollision ?
														0x80ffbbbb :
														0x80bbbbff;

				if(doDrawOnSelf){
					mmBodyDraw(	funcs,
								bi->body,
								mat,
								argb,
								doDrawBounds);
				}
				
				if(	wcoGetMat(bi->wco, matActor)
					&&
					distance3Squared(posCamera, matActor[3]) <= SQR(100)
					&&
					(	distance3Squared(mat[0], matActor[0]) > SQR(0.01f) ||
						distance3Squared(mat[1], matActor[1]) > SQR(0.01f) ||
						distance3Squared(mat[2], matActor[2]) > SQR(0.01f) ||
						distance3Squared(mat[3], matActor[3]) > SQR(0.01f))
					)
				{
					mmBodyDraw(	funcs,
								bi->body,
								matActor,
								argb,
								doDrawBounds);
				}
			}
			EARRAY_FOREACH_END;
		}
		
		if(	doDrawNetOutputs &&
			doDrawOnSelf)
		{
			const MovementNetOutput* no;
			
			for(no = mm->fg.net.outputList.head;
				no;
				no = no->next)
			{
				const MovementNetOutput* noNext = no->next;
				
				if(noNext){
					if(subS32(	noNext->pc.server,
								no->pc.server) != MM_PROCESS_COUNTS_PER_STEP)
					{
						funcs->drawLine3D(	no->data.pos,
											0xffffffff,
											noNext->data.pos,
											0xff000000 |
												(0xff << ((mgState.frameCount % 3) * 8)));
					}
					else if(noNext->flags.notInterped){
						funcs->drawLine3D(	no->data.pos,
											0xffffffff,
											noNext->data.pos,
											0xffffff00);
					}else{
						funcs->drawLine3D(	no->data.pos,
											0xffffffff,
											noNext->data.pos,
											0xffff0000);
					}
				}
				else if(!no->prev){
					Vec3 p1;
					
					copyVec3(no->data.pos, p1);
					p1[1] += 1.f;
					
					funcs->drawLine3D(	no->data.pos,
										0xffffffff,
										p1,
										0xff0000ff);
				}
			}
		}

		if(	doDrawOutputs &&
			doDrawOnSelf)
		{
			const MovementOutput* o;
			
			for(o = td->toFG.outputList.head;
				o;
				o = (o == td->toFG.outputList.tail) ? NULL : o->next)
			{
				const MovementOutput* oNext = (o == td->toFG.outputList.tail) ? NULL : o->next;
				
				if(oNext){
					if(oNext->flags.notInterped){
						funcs->drawLine3D(	o->data.pos,
											0xffffffff,
											oNext->data.pos,
											0xffffff00);
					}else{
						funcs->drawLine3D(	o->data.pos,
											0xffffffff,
											oNext->data.pos,
											0xff00ff00);
					}
				}
				else if(o->prev == td->toFG.outputList.head){
					Vec3 p1;
					
					copyVec3(o->data.pos, p1);
					p1[1] += 1.f;
					
					funcs->drawLine3D(	o->data.pos,
										0xffffffff,
										p1,
										0xff0000ff);
				}
			}
		}

		#if 0
		{
			mmRareLockEnter(mm);
			{
				EARRAY_CONST_FOREACH_BEGIN(mm->bg.simBodyInstances, j, jsize);
				{
					const MovementSimBodyInstance* sbi = mm->bg.simBodyInstances[j];

					mmBodyDraw(funcs, sbi->body, mat, 0x80bbffbb, doDrawBounds);
				}
				EARRAY_FOREACH_END;
			}
			mmRareLockLeave(mm);
		}
		#endif
	}
	EARRAY_FOREACH_END;
}

void mmAlwaysDraw(	const MovementDrawFuncs* funcs,
					const Mat4 matCamera)
{
	#if 0
	{
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
		{
			MovementManager*	mm = mgState.fg.managers[i];
			Entity*				e = mm->entityProbably;
			Vec3				a;
			Vec3				b;

			if(!e){
				continue;
			}

			copyVec3(e->serverPosFG, a);
			a[1] += 10;
			funcs->drawLine3D(e->serverPosFG, 0xffffffff, a, 0xffff0000);
			entGetPos(e, b);
			funcs->drawLine3D(b, 0xffffffff, e->serverPosFG, 0xff00ff00);
		}
		EARRAY_FOREACH_END;
	}
	#endif

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.alwaysDrawManagers, i, isize);
	{
		MovementManager* mm = mgState.fg.alwaysDrawManagers[i];
		
		mmResourcesAlwaysDrawFG(mm, funcs);
	}
	EARRAY_FOREACH_END;
}

void mmDrawResourceDebug(	const MovementDrawFuncs* funcs,
							const Mat4 matCamera)
{
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager*	mm = mgState.fg.managers[i];
		Vec3				pos;
		
		mmGetPositionFG(mm, pos);
		
		if(distance3Squared(pos, matCamera[3]) < SQR(100)){
			EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, j, jsize);
			{
				MovementManagedResource*	mmr = mm->fg.resources[j];
				U32							argb = 0x80ffff44;
				Vec3						base;
				Vec3						a;
				Vec3						b;

				if(mmr->fg.flags.sentDestroyToBG){
					argb = 0x8044ff44;
				}
				else if(mmr->fg.flags.didSetState){
					argb = 0x8044ff44;
				}
				
				copyVec3(pos, base);
				base[1] += 7.f + j * 1.5f;
				copyVec3(base, a);
				copyVec3(base, b);
				b[1] += 1.f;

				funcs->drawLine3D(a, argb, b, argb);
				
				if(mmr->handle){
					copyVec3(base, a);
					copyVec3(base, b);
					b[0] += 0.3f;

					funcs->drawLine3D(a, argb, b, argb);
				}
				
				if(mmr->fg.flags.hasNetState){
					copyVec3(base, a);
					a[1] += 0.1f;
					copyVec3(a, b);
					b[0] += 0.3f;

					funcs->drawLine3D(a, 0x804444ff, b, 0x804444ff);
				}

				if(mmr->fg.flags.hadLocalState){
					copyVec3(base, a);
					a[1] += 0.2f;
					copyVec3(a, b);
					b[0] += 0.3f;

					funcs->drawLine3D(a, 0x80ff44ff, b, 0x80ff44ff);
				}

				if(mmr->fg.flags.waitingForTrigger){
					copyVec3(base, a);
					a[1] += 0.3f;
					copyVec3(a, b);
					b[0] += 0.3f;

					funcs->drawLine3D(a, 0x80ff4444, b, 0x80ff4444);
				}

				if(mmr->fg.flags.waitingForWake){
					copyVec3(base, a);
					a[1] += 0.4f;
					copyVec3(a, b);
					b[0] += 0.3f;

					funcs->drawLine3D(a, 0x80ffff44, b, 0x80ffff44);
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;
}

S32 mmIsCollisionEnabled(MovementManager* mm)
{
	return	mm && !mm->fg.flags.noCollision;
}

S32 mmDoesCapsuleOrientationUseRotation(MovementManager* mm)
{
	return mm && mm->fg.flags.capsuleOrientationUseRotation;
}


S32	mmGetCapsules(	MovementManager* mm,
					const Capsule*const** capsulesOut)
{
	if(	!mm ||
		!capsulesOut)
	{
		return 0;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.bodyInstances, i, isize);
	{
		const MovementBodyInstance* bi = mm->fg.bodyInstances[i];
		
		if(bi->body->capsules){
			*capsulesOut = bi->body->capsules;
			
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

S32 mmGetCapsuleBounds(MovementManager *mm, 
					   Vec3 boundsMinOut,
					   Vec3 boundsMaxOut)
{
	if(	!mm ||
		!boundsMinOut ||
		!boundsMaxOut)
	{
		return 0;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.bodyInstances, i, isize);
	{
		const MovementBodyInstance* bi = mm->fg.bodyInstances[i];
		
		if(bi->body->capsules){
			Vec3 tempMin;
			Vec3 tempMax;
			
			EARRAY_CONST_FOREACH_BEGIN(bi->body->capsules, j, jsize);
			{
				const Capsule* c = bi->body->capsules[j];

				if(c->iType){
					continue;
				}

				CapsuleGetBounds(c, tempMin, tempMax);

				if(!j){
					copyVec3(tempMin, boundsMinOut);
					copyVec3(tempMax, boundsMaxOut);
				}else{
					MINVEC3(boundsMinOut, tempMin, boundsMinOut);
					MAXVEC3(boundsMaxOut, tempMax, boundsMaxOut);
				}
			}
			EARRAY_FOREACH_END;
						
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

S32	mmGetCollisionRadius(	MovementManager* mm,
							F32* radiusOut)
{
	if( !mm ||
		!radiusOut)
	{
		return 0;
	}
	
	*radiusOut = mm->fg.bodyRadius;
	
	return 1;
}

static void mmUpdateBodyRadiusFG(MovementManager* mm){
	F32 radius = 0.f;

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.bodyInstances, i, isize);
	{
		const MovementBodyInstance* bi = mm->fg.bodyInstances[i];
		
		MAX1(radius, bi->body->radius);
	}
	EARRAY_FOREACH_END;
	
	if(mm->fg.bodyRadius != radius){
		mm->fg.bodyRadiusMutable = radius;
		
		mmSendMsgCollRadiusChangedFG(mm);
	}
}

static void mmUpdateBodyRadiusBG(MovementManager* mm){
	F32 radius = 0.f;

	mm->bg.flagsMutable.hasKinematicBody = 0;

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.bodyInstances, i, isize);
	{
		const MovementBodyInstance* bi = mm->bg.bodyInstances[i];
		
		MAX1(radius, bi->body->radius);
		
		if(bi->body->parts){
			mm->bg.flagsMutable.hasKinematicBody = 1;
		}
	}
	EARRAY_FOREACH_END;
	
	if(mm->bg.bodyRadius != radius){
		mm->bg.bodyRadiusMutable = radius;
		
		mmUpdateGridSizeIndexBG(mm);
	}
}

S32 mrmEnableMsgCreateToBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd
		||
		pd->msgType != MR_MSG_FG_CREATE_TOBG &&
		pd->msgType != MR_MSG_FG_UPDATED_TOFG)
	{
		return 0;
	}
	
	pd->mm->fg.flagsMutable.mrHasHandleCreateToBG = 1;
	pd->mr->fg.flagsMutable.handleCreateToBG = 1;
	
	mrLog(	pd->mr,
			NULL,
			"Enabling msg CREATE_TOBG.");

	return 1;
}

S32 mrmEnableMsgUpdatedToBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequesterThreadData*		mrtd;
	MovementThreadData*					td;

	if(	!pd ||
		pd->msgType != MR_MSG_FG_CREATE_TOBG)
	{
		return 0;
	}
	
	td = MM_THREADDATA_FG(pd->mm);
	td->toBG.flagsMutable.mrHasUserToBG = 1;

	mrtd = MR_THREADDATA_FG(pd->mr);
	mrtd->toBG.flagsMutable.hasUserToBG = 1;

	mrLog(	pd->mr,
			NULL,
			"Enabling msg UPDATED_TOBG.");

	return 1;
}

static S32 mrmEnableMsgUpdatedSyncFGHelper(	const MovementRequesterMsg* msg,
											S32 needsAfterSync)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd
		||
		(	pd->msgType != MR_MSG_FG_CREATE_TOBG &&
			pd->msgType != MR_MSG_FG_AFTER_SYNC &&
			pd->msgType != MR_MSG_FG_UPDATED_TOFG))
	{
		return 0;
	}
	
	return mrEnableMsgUpdatedSyncHelper(pd->mr, needsAfterSync);
}

S32 mrmEnableMsgUpdatedSyncFG(const MovementRequesterMsg* msg){
	return mrmEnableMsgUpdatedSyncFGHelper(msg, 0);
}

S32 mrmEnableMsgUpdatedSyncWithAfterSyncFG(const MovementRequesterMsg* msg){
	return mrmEnableMsgUpdatedSyncFGHelper(msg, 1);
}

S32 mrmGetManagerFG(const MovementRequesterMsg* msg,
					MovementManager** mmOut)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		!mmOut ||
		!MR_MSG_TYPE_IS_FG(pd->msgType))
	{
		return 0;
	}

	*mmOut = pd->mm;

	return 1;
}

S32	mrmRequesterCreateFG(	const MovementRequesterMsg* msg,
							MovementRequester** mrOut,
							const char* name,
							MovementRequesterMsgHandler msgHandler,
							U32 id)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		!mrOut ||
		!MR_MSG_TYPE_IS_FG(pd->msgType))
	{
		return 0;
	}

	if(name){
		return mmRequesterCreateBasicByName(pd->mm, mrOut, name);
	}else{
		return mmRequesterCreateBasic(pd->mm, mrOut, msgHandler);
	}
}

S32 mmGetFromWCO(	const WorldCollObject* wco,
					MovementManager** mmOut)
{
	MovementBodyInstance* bi;

	if(	!mmOut ||
		!wcoIsDynamic(wco) ||
		!wcoGetUserPointer(wco, mmKinematicObjectMsgHandler, &bi) ||
		!bi->mm)
	{
		return 0;
	}
	
	*mmOut = bi->mm;
	
	return 1;
}

S32	mmGetUserPointerFromWCO(const WorldCollObject* wco,
							void** userPointerOut)
{
	MovementManager* mm;
	
	if(	!userPointerOut ||
		!mmGetFromWCO(wco, &mm) ||
		!mm->userPointer)
	{
		return 0;
	}

	*userPointerOut = mm->userPointer;
	
	return 1;
}

static S32 mmGeometryCreateDataVerify(const MovementGeometryDesc* geoDesc){
	switch(geoDesc->geoType){
		xcase MM_GEO_MESH:{
			FOR_BEGIN(i, (S32)geoDesc->mesh.vertCount * 3);
			{
				if(!FINITE(geoDesc->mesh.verts[i])){
					return 0;
				}
			}
			FOR_END;
	
			FOR_BEGIN(i, (S32)geoDesc->mesh.triCount * 3);
			{
				if(geoDesc->mesh.tris[i] > geoDesc->mesh.vertCount){
					return 0;
				}
			}
			FOR_END;
		}

		xcase MM_GEO_GROUP_MODEL:{
			if(!SAFE_DEREF(geoDesc->model.modelName)){
				return 0;
			}
		}

		xcase MM_GEO_WL_MODEL:{
			if(!SAFE_DEREF(geoDesc->model.modelName)){
				return 0;
			}
		}

		xdefault:{
			return 0;
		}
	}
	
	return 1;
}

S32 mmGeometryCreate(	MovementGeometry** geoOut,
						const MovementGeometryDesc* geoDesc)
{
	PERFINFO_AUTO_START_FUNC();

	if(!mmGeometryCreateDataVerify(geoDesc)){
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	// Look for an existing geo that's the same.
	
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.geos, i, isize);
	{
		MovementGeometry* geo = mgState.fg.geos[i];

		if(geo->geoType != geoDesc->geoType){
			continue;
		}

		switch(geo->geoType){
			xcase MM_GEO_MESH:{
				if(	geo->mesh.triCount == geoDesc->mesh.triCount &&
					geo->mesh.vertCount == geoDesc->mesh.vertCount &&
					!memcmp(geo->mesh.tris,
							geoDesc->mesh.tris,
							sizeof(geo->mesh.tris[0]) * geo->mesh.triCount * 3) &&
					!memcmp(geo->mesh.verts,
							geoDesc->mesh.verts,
							sizeof(geo->mesh.verts[0]) * geo->mesh.vertCount * 3))
				{
					*geoOut = geo;
					PERFINFO_AUTO_STOP();
					return 1;
				}
			}

			xcase MM_GEO_GROUP_MODEL:{
				if(	sameVec3(geo->model.scale, geoDesc->model.scale) &&
					!stricmp(geo->model.modelName, geoDesc->model.modelName))
				{
					*geoOut = geo;
					PERFINFO_AUTO_STOP();
					return 1;
				}
			}

			xcase MM_GEO_WL_MODEL:{
				if(	sameVec3(geo->model.scale, geoDesc->model.scale) &&
					!stricmp(geo->model.modelName, geoDesc->model.modelName) &&
					!!geo->model.fileName == !!geoDesc->model.fileName &&
					(	!geo->model.fileName ||
						!stricmp(geo->model.fileName, geoDesc->model.fileName))
					)
				{
					*geoOut = geo;
					PERFINFO_AUTO_STOP();
					return 1;
				}
			}
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_START("new", 1);
	{
		MovementGeometry* geo;
		
		geo = callocStruct(MovementGeometry);

		geo->geoType = geoDesc->geoType;
		geo->index = eaPush(&mgState.fg.geosMutable, geo);

		switch(geo->geoType){
			xcase MM_GEO_MESH:{
				geo->mesh.vertCount = geoDesc->mesh.vertCount;
				geo->mesh.verts = callocStructs(F32, 3 * geo->mesh.vertCount);
				CopyStructs(geo->mesh.verts, geoDesc->mesh.verts, 3 * geo->mesh.vertCount);

				geo->mesh.triCount = geoDesc->mesh.triCount;
				geo->mesh.tris = callocStructs(U32, 3 * geo->mesh.triCount);
				CopyStructs(geo->mesh.tris, geoDesc->mesh.tris, 3 * geo->mesh.triCount);
			}

			xcase MM_GEO_GROUP_MODEL:{
				geo->model.modelName = allocAddString(geoDesc->model.modelName);
				copyVec3(geoDesc->model.scale, geo->model.scale);
			}

			xcase MM_GEO_WL_MODEL:{
				if(geoDesc->model.fileName){
					geo->model.fileName = allocAddString(geoDesc->model.fileName);
				}
				geo->model.modelName = allocAddString(geoDesc->model.modelName);
				copyVec3(geoDesc->model.scale, geo->model.scale);
			}
		}
	
		*geoOut = geo;
	}
	PERFINFO_AUTO_STOP();
	
	PERFINFO_AUTO_STOP();

	return 1;
}

static void mmGeometryGetDataCB(MovementGeometry* g,
								const U32* tris,
								U32 triCount,
								const F32* verts,
								U32 vertCount)
{
	assert(!g->mesh.verts);
	g->mesh.vertCount = vertCount;
	g->mesh.verts = callocStructs(F32, 3 * g->mesh.vertCount);
	CopyStructs(g->mesh.verts, verts, 3 * g->mesh.vertCount);

	g->mesh.triCount = triCount;
	g->mesh.tris = callocStructs(U32, 3 * g->mesh.triCount);
	CopyStructs(g->mesh.tris, tris, 3 * g->mesh.triCount);
}

S32 mmGeometryGetData(MovementGeometry* g){
	MovementGlobalMsg msg = {0};

	if(g->mesh.verts){
		return 1;
	}

	if(	!FALSE_THEN_SET(g->flags.requestedGeoemetryData) ||
		!mgState.msgHandler)
	{
		return 0;
	}

	msg.msgType = MG_MSG_GET_GEOMETRY_DATA;
	msg.getGeometryData.geoType = g->geoType;
	msg.getGeometryData.fileName = g->model.fileName;
	msg.getGeometryData.modelName = g->model.modelName;
	copyVec3(g->model.scale, msg.getGeometryData.scale);

	msg.getGeometryData.cb.cb = mmGeometryGetDataCB;
	msg.getGeometryData.cb.userPointer = g;

	mgState.msgHandler(&msg);

	return !!g->mesh.verts;
}

S32 mmGeometryGetTriangleMesh(	MovementGeometry* g,
								PSDKCookedMesh** meshOut,
								const char* name)
{
	if(	!g ||
		!meshOut ||
		!mmGeometryGetData(g))
	{
		return 0;
	}

	if(	!g->cookedMesh.triangle &&
		FALSE_THEN_SET(g->flags.triedCookingTriangleMesh))
	{
		#if !PSDK_DISABLED
		{
			PSDKMeshDesc	meshDesc = {0};
			char			buffer[200];

			meshDesc.triCount = g->mesh.triCount;
			meshDesc.triArray = g->mesh.tris;

			meshDesc.vertCount = g->mesh.vertCount;
			meshDesc.vertArray = (const Vec3*)g->mesh.verts;

			sprintf(buffer,
					"%d verts, %d tris, %s",
					meshDesc.vertCount,
					meshDesc.triCount,
					name);

			meshDesc.name = buffer;

			psdkCookedMeshCreate(	&g->cookedMesh.triangle,
									&meshDesc);
		}
		#endif
	}
	
	*meshOut = g->cookedMesh.triangle;
	
	return !!*meshOut;
}

S32 mmGeometryGetConvexMesh(MovementGeometry* g,
							PSDKCookedMesh** meshOut,
							const char* name)
{
	if(	!g ||
		!meshOut ||
		!mmGeometryGetData(g))
	{
		return 0;
	}

	if(	!g->cookedMesh.convex &&
		FALSE_THEN_SET(g->flags.triedCookingConvexMesh))
	{
		#if !PSDK_DISABLED
		{
			PSDKMeshDesc meshDesc = {0};

			meshDesc.vertCount = g->mesh.vertCount;
			meshDesc.vertArray = (const Vec3*)g->mesh.verts;

			while(!g->cookedMesh.convex){
				char buffer[200];

				sprintf(buffer,
						"%d verts, %s",
						meshDesc.vertCount,
						name);

				meshDesc.name = buffer;

				psdkCookedMeshCreate(	&g->cookedMesh.convex,
										&meshDesc);
				
				if(!g->cookedMesh.convex){
					S32 reduction = g->mesh.vertCount / 10;

					if( !reduction ||
						meshDesc.vertCount < reduction)
					{
						break;
					}
					
					meshDesc.vertCount -= reduction;
				}
			}
		}
		#endif
	}
	
	*meshOut = g->cookedMesh.convex;
	
	return !!*meshOut;
}

static void mmBodyCalculateRadius(MovementBody* b){
	b->radius = 0.f;

	EARRAY_CONST_FOREACH_BEGIN(b->capsules, i, isize);
	{
		const Capsule*	c = b->capsules[i];
		F32				r = lengthVec3(c->vStart);
		F32				d;
		Vec3			end;
		
		// Find the farthest endpoint, then add the radius.
		
		scaleAddVec3(	c->vDir,
						c->fLength,
						c->vStart,
						end);
		
		d = lengthVec3(end);

		MAX1(r, d);
		
		r += c->fRadius;
		
		MAX1(b->radius, r);
	}
	EARRAY_FOREACH_END;
	
	EARRAY_CONST_FOREACH_BEGIN(b->parts, i, isize);
	{
		const MovementBodyPart* p = b->parts[i];
		Mat4					matPart;
		F32						r = 0.f;

		createMat3YPR(matPart, p->pyr);
		copyVec3(p->pos, matPart[3]);
		
		FOR_BEGIN(j, (S32)p->geo->mesh.vertCount);
			Vec3	v;
			F32		d;
			
			mulVecMat4(	p->geo->mesh.verts + j * 3,
						matPart,
						v);
			
			d = lengthVec3(v);
			
			MAX1(r, d);
		FOR_END;
		
		MAX1(b->radius, r);
	}
	EARRAY_FOREACH_END;
}

void mmBodyLockEnter(void){
	csEnter(&mgState.cs.bodies);
}

void mmBodyLockLeave(void){
	csLeave(&mgState.cs.bodies);
}

S32 mmBodyDescCreate(MovementBodyDesc** bdOut){
	*bdOut = callocStruct(MovementBodyDesc);
	
	return 1;
}

S32 mmBodyCreate(	MovementBody** bOut,
					MovementBodyDesc** bdInOut)
{
	MovementBody*		b;
	MovementBodyDesc*	bd = SAFE_DEREF(bdInOut);
	S32					foundIndex = -1;

	if(	!bOut ||
		!bd)
	{
		return 0;
	}
	
	// Look for an existing body that's the same.
	
	EARRAY_CONST_FOREACH_BEGIN(mgState.bodies, i, isize);
	{
		const MovementBody* bCheck = mgState.bodies[i];
		
		if(	eaSize(&bCheck->capsules) != eaSize(&bd->capsules) ||
			eaSize(&bCheck->parts) != eaSize(&bd->parts))
		{
			continue;
		}else{
			S32 isSame = 1;
			
			EARRAY_CONST_FOREACH_BEGIN(bCheck->capsules, j, jsize);
			{
				const Capsule* c0 = bCheck->capsules[j];
				const Capsule* c1 = bd->capsules[j];

				if(memcmp(c0, c1, sizeof(*c0))){
					isSame = 0;
					break;
				}
			}
			EARRAY_FOREACH_END;
			
			if(!isSame){
				continue;
			}
			
			EARRAY_CONST_FOREACH_BEGIN(bCheck->parts, j, jsize);
			{
				const MovementBodyPart* p0 = bCheck->parts[j];
				const MovementBodyPart* p1 = bd->parts[j];
				
				if(	p0->geo != p1->geo ||
					!sameVec3(p0->pos, p1->pos) ||
					!sameVec3(p0->pyr, p1->pyr))
				{
					isSame = 0;
					break;
				}
			}
			EARRAY_FOREACH_END;

			if(!isSame){
				continue;
			}
			
			foundIndex = i;
			break;
		}
	}
	EARRAY_FOREACH_END;
	
	if(foundIndex < 0){
		if(!mgState.flags.isServer){
			b = NULL;
		}else{
			// Create a new body.

			b = callocStruct(MovementBody);
			
			mmBodyLockEnter();
			{
				b->index = eaPush(&mgState.bodiesMutable, b);
			}
			mmBodyLockLeave();
			
			b->capsules = bd->capsules;
			b->parts = bd->parts;
			
			// Calculate the body radius.
			
			mmBodyCalculateRadius(b);
		}
	}else{
		b = mgState.bodies[foundIndex];
		
		eaDestroyEx(&bd->capsules, NULL);
		eaDestroyEx(&bd->parts, NULL);
	}
	
	*bOut = b;
	
	SAFE_FREE(*bdInOut);
	
	return !!b;
}

void mmBodyGetDebugString(	MovementBody* b,
							char** estrBufferInOut)
{
	if(	!b ||
		!estrBufferInOut)
	{
		return;
	}
	
	estrConcatf(estrBufferInOut,
				"Body[%d], Radius %1.3f\n",
				b->index,
				b->radius);

	EARRAY_CONST_FOREACH_BEGIN(b->capsules, i, isize);
	{
		const Capsule* c = b->capsules[i];

		estrConcatf(estrBufferInOut,
					"Capsule("
					"Length %1.3f"
					", Radius %1.3f"
					", Type %d"
					", Start(%1.3f, %1.3f, %1.3f)"
					", Dir(%1.3f, %1.3f, %1.3f)"
					"%s"
					,
					c->fLength,
					c->fRadius,
					c->iType,
					vecParamsXYZ(c->vStart),
					vecParamsXYZ(c->vDir),
					i + 1 == isize ? "" : "\n");
	}
	EARRAY_FOREACH_END;
}

S32 mmBodyGetIndex(	MovementBody* b,
					U32* indexOut)
{
	if(	!b ||
		!indexOut)
	{
		return 0;
	}
	
	*indexOut = b->index;
	
	return 1;
}

S32 mmBodyGetByIndex(	MovementBody** bOut,
						U32 index)
{
	if(!bOut){
		return 0;
	}
	
	mmBodyLockEnter();
	{
		if(index < eaUSize(&mgState.bodies)){
			*bOut = mgState.bodies[index];
		}else{
			*bOut = NULL;
		}
	}
	mmBodyLockLeave();
	
	return !!*bOut;
}

S32 mmBodyDescAddGeometry(	MovementBodyDesc* bd,
							MovementGeometry* geo,
							const Vec3 pos,
							const Vec3 pyr)
{
	MovementBodyPart* p;
	
	p = callocStruct(MovementBodyPart);
	
	p->geo = geo;
	copyVec3(pos, p->pos);
	copyVec3(pyr, p->pyr);
	
	eaPush(&bd->parts, p);
	
	return 1;
}

S32 mmBodyDescAddCapsule(	MovementBodyDesc* bd,
							const Capsule* capsuleToCopy)
{
	Capsule* c;
	
	c = callocStruct(Capsule);

	*c = *capsuleToCopy;
	
	eaPush(&bd->capsules, c);
	
	return 1;
}

void mmBodyDraw(const MovementDrawFuncs* funcs,
				const MovementBody* body,
				const Mat4 matWorld,
				U32 argb,
				S32 doDrawBounds)
{
	static StashTable st;
	
	if(!st){
		st = stashTableCreateInt(100);
	}else{
		stashTableClear(st);
	}

	if(doDrawBounds){
		funcs->drawCapsule3D(	matWorld[3],
								unitmat[1],
								0.f,
								body->radius,
								0x4000ff00);
	}

	EARRAY_CONST_FOREACH_BEGIN(body->parts, i, isize);
	{
		const MovementBodyPart* p = body->parts[i];
		Mat4					matPart;
		Mat4					mat;
		
		createMat3YPR(matPart, p->pyr);
		copyVec3(p->pos, matPart[3]);
		
		mulMat4(matWorld, matPart, mat);
		
		FOR_BEGIN(j, (S32)p->geo->mesh.triCount);
		{
			const S32*	tri = p->geo->mesh.tris + j * 3;
			Vec3		v[3];
			
			FOR_BEGIN(k, 3);
			{
				mulVecMat4(p->geo->mesh.verts + tri[k] * 3, mat, v[k]);
			}
			FOR_END;
			
			FOR_BEGIN(k, 3);
			{
				U32 i0 = tri[k];
				U32 i1 = tri[(k + 1) % 3];
				U32 key;
				
				if(i0 < i1){
					SWAP32(i0, i1);
				}
				else if(i0 == i1){
					continue;
				}
				
				key = (i0 << 16) | i1;
				
				if(	key &&
					stashIntAddInt(st, key, 0, 0))
				{
					funcs->drawLine3D(v[k], argb, v[(k + 1) % 3], argb);
				}
			}
			FOR_END;
		}
		FOR_END;
	}
	EARRAY_FOREACH_END;
	
	EARRAY_CONST_FOREACH_BEGIN(body->capsules, i, isize);
	{
		const Capsule*	c = body->capsules[i];
		Vec3			p;
		Vec3			dir;
		
		mulVecMat4(c->vStart, matWorld, p);
		mulVecMat3(c->vDir, matWorld, dir);
		
		funcs->drawCapsule3D(p, dir, c->fLength, c->fRadius, argb);
	}
	EARRAY_FOREACH_END;
}

AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	mmrBodyMsgHandler,
											"Body",
											MMRBody,
											MDC_BIT_POSITION_CHANGE);

AUTO_STRUCT;
typedef struct MMRBodyConstant {
	U32 bodyIndex;

	U32 isShell : 1;
	U32 hasOneWayCollision : 1;
} MMRBodyConstant;

AUTO_STRUCT;
typedef struct MMRBodyConstantNP {
	U32 asdf;
} MMRBodyConstantNP;

AUTO_STRUCT;
typedef struct MMRBodyActivatedFG {
	MovementBodyInstance*	bi;		NO_AST
	U32						unused;
} MMRBodyActivatedFG;

AUTO_STRUCT;
typedef struct MMRBodyActivatedBG {
	MovementBodyInstance*	bi;		NO_AST
	U32						unused;
} MMRBodyActivatedBG;

AUTO_STRUCT;
typedef struct MMRBodyState {
	S32	unused;
} MMRBodyState;

static void mmrBodyDestroyFG(const MovementManagedResourceMsg* msg){
	MMRBodyActivatedFG*		activated = msg->in.activatedStruct;
	MovementBodyInstance*	bi = activated->bi;
	MovementManager*		mm = msg->in.mm;
	
	if(!bi){
		return;
	}
	
	activated->bi = NULL;

	assert(bi->mm == msg->in.mm);
	
	if(bi->wco){
		// Don't lose the pointer until the swap so it's safe to check if a wco is a mm in BG.
		
		assert(eaFind(&mm->fg.bodyInstances, bi) >= 0);
		assert(eaFind(&mgState.fg.bodyInstances, bi) >= 0);

		bi->flags.freeOnWCODestroy = 1;
		wcoDestroyAndNotify(bi->wco);
	}else{
		mmBodyInstanceDestroyFG(mm, &bi);
	}

	mmUpdateBodyRadiusFG(mm);
}

static void mmrBodyDestroyBG(const MovementManagedResourceMsg* msg){
	MMRBodyActivatedBG*		activated = msg->in.activatedStruct;
	MovementBodyInstance*	bi = activated->bi;
	MovementManager*		mm = msg->in.mm;

	if(bi){
		assert(bi->mm == msg->in.mm);
		
		if(eaFindAndRemove(&mm->bg.bodyInstancesMutable, bi) < 0){
			assert(0);
		}

		if(!eaSize(&mm->bg.bodyInstances)){
			eaDestroy(&mm->bg.bodyInstancesMutable);
		}

		SAFE_FREE(activated->bi);

		mmUpdateBodyRadiusBG(mm);
	}
}

void mmrBodyMsgHandler(const MovementManagedResourceMsg* msg){
	MovementManager*		mm = msg->in.mm;
	const MMRBodyConstant*	constant = msg->in.constant;
	
	switch(msg->in.msgType){
		// FG msg handlers.

		xcase MMR_MSG_GET_CONSTANT_DEBUG_STRING:{
			char** estrBuffer = msg->in.getDebugString.estrBuffer;
		}
		
		xcase MMR_MSG_FG_GET_STATE_DEBUG_STRING:{
			char**					estrBuffer = msg->in.fg.getStateDebugString.estrBuffer;
			MMRBodyActivatedFG*		activated = msg->in.activatedStruct;
			MovementBodyInstance*	bi = SAFE_MEMBER(activated, bi);
			
			mmBodyGetDebugString(	SAFE_MEMBER(bi, body),
									estrBuffer);
		}
		
		xcase MMR_MSG_FG_SET_STATE:{
			MMRBodyActivatedFG* activated = msg->in.activatedStruct;
			
			if(	activated &&
				!activated->bi &&
				constant->bodyIndex < eaUSize(&mgState.bodies))
			{
				MovementBodyInstance* bi;

				// Create FG body.
				
				activated->bi = bi = callocStruct(MovementBodyInstance);
				
				bi->mm = mm;
				bi->body = mgState.bodies[constant->bodyIndex];
				bi->flags.isShell = constant->isShell;
				bi->flags.hasOneWayCollision = constant->hasOneWayCollision;
				
				eaPush(&mm->fg.bodyInstancesMutable, bi);
				eaPush(&mgState.fg.bodyInstancesMutable, bi);
				
				mgState.fg.flagsMutable.needsBodyRefresh = 1;
				
				mmUpdateBodyRadiusFG(mm);
			}
		}

		xcase MMR_MSG_FG_DESTROYED:{
			mmrBodyDestroyFG(msg);
		}
		
		xcase MMR_MSG_FG_ALL_BODIES_DESTROYED:{
			mmrBodyDestroyFG(msg);
		}

		// BG msg handlers.

		xcase MMR_MSG_BG_GET_STATE_DEBUG_STRING:{
			MMRBodyActivatedBG*		activated = msg->in.activatedStruct;
			MovementBodyInstance*	bi = SAFE_MEMBER(activated, bi);
			char**					estrBuffer = msg->in.bg.getStateDebugString.estrBuffer;
			
			estrConcatf(estrBuffer,
						"Body instance 0x%p, Index %u.\n",
						bi,
						eaFind(&mm->bg.bodyInstances, bi));
			
			mmBodyGetDebugString(	SAFE_MEMBER(bi, body),
									estrBuffer);
		}

		xcase MMR_MSG_BG_SET_STATE:{
			MMRBodyActivatedBG* activated = msg->in.activatedStruct;
			
			if(!activated->bi){
				// Create BG body.
				
				MovementBodyInstance* bi;
				
				activated->bi = bi = callocStruct(MovementBodyInstance);
				
				bi->mm = mm;
				
				mmBodyLockEnter();
				{
					bi->body = eaGet(&mgState.bodies, constant->bodyIndex);
				}
				mmBodyLockLeave();
				
				if(bi->body){
					eaPush(&mm->bg.bodyInstancesMutable, bi);

					mmUpdateBodyRadiusBG(mm);
				}else{
					SAFE_FREE(activated->bi);
				}
			}
		}

		xcase MMR_MSG_BG_DESTROYED:{
			mmrBodyDestroyBG(msg);
		}
		
		xcase MMR_MSG_BG_ALL_BODIES_DESTROYED:{
			mmrBodyDestroyBG(msg);
		}
	}
}

static U32 mmrBodyGetID(void){
	static U32 id;
	
	if(!id){
		if(!mmGetManagedResourceIDByMsgHandler(mmrBodyMsgHandler, &id)){
			assert(0);
		}
	}
	
	return id;
}

S32 mmrBodyCreateFG(MovementManager* mm,
					U32* handleOut,
					MovementBody* body,
					U32 isShell,
					U32 hasOneWayCollision)
{
	MMRBodyConstant constant = {0};
	
	assert(body->index >= 0);
	
	constant.bodyIndex = body->index;
	constant.isShell = !!isShell;
	constant.hasOneWayCollision = !!hasOneWayCollision;

	return mmResourceCreateFG(mm, handleOut, mmrBodyGetID(), &constant, NULL, NULL);
}

S32 mmBodyGetCapsules(	MovementBody* body,
						const Capsule*const** capsulesOut)
{
	*capsulesOut = body->capsules;
	return 1;
}						


void mmDestroyBodies(MovementManager* mm)
{
	if (!mm)
		return;

	// Tell resources to free stuff.
	mmResourcesSendMsgBodiesDestroyedFG(mm);
}

void mmRareLockEnter(MovementManager* mm){
	U32 sleepCount = 0;
	
	while(1){
		if(InterlockedIncrement(&mm->rareLock) == 1){
			break;
		}
		InterlockedDecrement(&mm->rareLock);
		if(sleepCount < 1000){
			sleepCount++;
			Sleep(0);
		}else{
			Sleep(1);
		}
	}
}

void mmRareLockLeave(MovementManager* mm){
	InterlockedDecrement(&mm->rareLock);
}

void mmDynFxHitReactCallback(	U32 triggerID,
								const Vec3 pos,
								const Vec3 vel)
{
	if(triggerID){
		MovementTrigger t = {0};
		
		t.triggerID = triggerID;
		//copyVec3(pos, t.pos);
		copyVec3(vel, t.vel);
		
		mmAllTriggerSend(&t);
	}
}

void mmDynAnimHitReactCallback(	EntityRef er,
								U32 uid,
								const Vec3 pos,
								const Vec3 vel)
{
	if(er){
		MovementTrigger t = {0};
		
		t.triggerID = MM_ENTITY_HIT_REACT_ID(er,uid);
		t.flags.isEntityID = 1;
		//copyVec3(pos, t.pos);
		copyVec3(vel, t.vel);
		
		mmAllTriggerSend(&t);
	}
}

#if 0
// You can't call this because it can cause an infinite loop.
void mmExecListAddTail(	MovementExecList* mel,
						MovementExecNode* node)
{
	//printf(	"Adding node 0x%8.8p to mel 0x%8.8p.\n",
	//		node,
	//		mel);

	node->next = NULL;

	if(!mel->head){
		node->prev = NULL;
		mel->head = node;
	}else{
		node->prev = mel->tail;
		mel->tail->next = node;
	}

	mel->tail = node;
}
#endif

void mmExecListAddHead(	MovementExecList* mel,
						MovementExecNode* node)
{
	//printf(	"Adding node 0x%8.8p to mel 0x%8.8p.\n",
	//		node,
	//		mel);

	node->prev = NULL;

	if(!mel->head){
		node->next = NULL;
		mel->tail = node;
	}else{
		node->next = mel->head;
		mel->head->prev = node;
	}

	mel->head = node;
}

void mmExecListRemove(	MovementExecList* mel,
						MovementExecNode* node)
{
	if(node->prev){
		node->prev->next = node->next;
	}else{
		assert(mel->head == node);
		mel->head = node->next;
	}
	
	if(node->next){
		node->next->prev = node->prev;
	}else{
		assert(mel->tail == node);
		mel->tail = node->prev;
	}
	
	MEL_NODE_REMOVED(*mel, node);
}

S32 mmExecListContains(	MovementExecList* mel,
						MovementExecNode* node)
{
	S32					found = 0;
	MovementExecNode*	nodeCur;
	
	for(nodeCur = mel->head;
		nodeCur;
		nodeCur = nodeCur->next)
	{
		assert(	nodeCur->prev ||
				nodeCur == mel->head);
		
		assert(	nodeCur->next ||
				nodeCur == mel->tail);
		
		if(nodeCur == node){
			found = 1;
		}
	}
	
	return found;
}

void mmClearGeometry(void){
	wcWaitForSimulationToEndFG(1, NULL, false);

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager* mm = mgState.fg.managers[i];
		
		// Tell resources to free stuff.
		
		mmResourcesSendMsgBodiesDestroyedFG(mm);

		ASSERT_FALSE_AND_SET(mgState.bg.flagsMutable.gridIsWritable);
		mmResourcesSendMsgBodiesDestroyedBG(mm);
		ASSERT_TRUE_AND_RESET(mgState.bg.flagsMutable.gridIsWritable);		

		// Free SimBodyInstances.
		
		mmDestroyAllSimBodyInstancesBG(mm);
	}
	EARRAY_FOREACH_END;
	
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.geos, i, isize);
	{
		MovementGeometry* g = mgState.fg.geos[i];
		
		#if !PSDK_DISABLED
			psdkCookedMeshDestroy(&g->cookedMesh.convex);
			psdkCookedMeshDestroy(&g->cookedMesh.triangle);
		#endif
		
		SAFE_FREE(g->mesh.tris);
		SAFE_FREE(g->mesh.verts);
	}
	EARRAY_FOREACH_END;
	
	eaDestroyEx(&mgState.fg.geosMutable, NULL);

	EARRAY_CONST_FOREACH_BEGIN(mgState.bodies, i, isize);
	{
		MovementBody* b = mgState.bodies[i];
		
		eaDestroyEx(&b->parts, NULL);
		eaDestroyEx(&b->capsules, NULL);
	}
	EARRAY_FOREACH_END;
	
	eaDestroyEx(&mgState.bodiesMutable, NULL);
}

F32 mmGetLocalViewSecondsSinceSPC(U32 spc){
	F32 seconds =	subS32(mgState.fg.localView.spcCeiling, spc) *
					MM_SECONDS_PER_PROCESS_COUNT;
	
	seconds -= mgState.fg.localView.outputInterp.inverse * MM_SECONDS_PER_STEP;
	
	return seconds;
}

void mrForcedSimEnableFG(	MovementRequester* mr,
							bool enabled)
{
	MovementManager*					mm;
	MovementClientManagerAssociation*	mcma;
	
	enabled = !!enabled;

	if(	!mr ||
		(bool)mr->fg.flags.forcedSimIsEnabled == enabled)
	{
		return;
	}
	
	mr->fg.flagsMutable.forcedSimIsEnabled = enabled;
	
	mm = mr->mm;
	mcma = mm->fg.mcma;
		
	if(enabled){
		if(!mm->fg.mrForcedSimCount++){
			if(mcma){
				mcma->mc->mmForcedSimCount++;
			}
		}
	}else{
		assert(mm->fg.mrForcedSimCount);
		
		if(!--mm->fg.mrForcedSimCount){
			if(mcma){
				assert(mcma->mc->mmForcedSimCount);
				mcma->mc->mmForcedSimCount--;
			}
		}
	}
}

static S32 mmResourcesHasFailedSetStateFG(MovementManager* mm){
	if(!mm){
		return 0;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->fg.resources[i];

		if(!mmr->fg.flags.didSetState){
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

void mmDebugValidateResources(void){
	static S32	wasValid = 1;
	S32			isValid = 1;

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager* mm = mgState.fg.managers[i];

		if(mmResourcesHasFailedSetStateFG(mm)){
			isValid = 0;
			break;
		}
	}
	EARRAY_FOREACH_END;
	
	if(wasValid != isValid){
		wasValid = isValid;
		
		if(isValid){
			printfColor(COLOR_BRIGHT|COLOR_GREEN,
						"Movement state is valid.\n");
		}else{
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"Movement state is invalid.\n");
		}
	}
}

static S32 mmGetWorldCollGridCellFG(MovementManager* mm,
									const Vec3 pos,
									WorldCollGridCell** wcCellOut)
{
	WorldCollGridCell* wcCell = NULL;

	if(!wcGetGridCellByWorldPosFG(	SAFE_MEMBER(mm->space, wc),
									&wcCell,
									pos))
	{
		// Will let you move in empty space.
	}
	else if(!wcCellHasScene(wcCell, __FUNCTION__)){
		// Scene isn't created yet, so stall movement until it is.

		wcForceSimulationUpdate();

		if(!wcCellHasScene(wcCell, __FUNCTION__)){
			return 0;
		}
	}

	if(wcCellOut){
		*wcCellOut = wcCell;
	}

	return 1;
}

S32 mmTranslatePosInSpaceFG(MovementManager* mm,
							const Vec3 posStart,
							const Vec3 vecOffset,
							Vec3 posReachedOut,
							S32* hitGroundOut)
{
	MotionState		motion = {0};
	F32				lenSQR = lengthVec3Squared(vecOffset);
	U32				stepCount = 1;
	Vec3			vecOffsetScaled;
	const F32		maxStepSize = 1.2f;

	if(	!posStart ||
		!vecOffset ||
		!posReachedOut)
	{
		return 0;
	}

	if(lenSQR > SQR(maxStepSize)){
		F32 len = sqrt(lenSQR);
		F32 scale;
		
		stepCount = ceilf(len / maxStepSize);
		
		if(!stepCount){
			stepCount = 1;
		}

		scale = 1.f / (F32)stepCount;
		
		scaleVec3(vecOffset, scale, vecOffsetScaled);
	}else{
		copyVec3(vecOffset, vecOffsetScaled);
	}
	
	copyVec3(posStart, posReachedOut);

	FOR_BEGIN(i, (S32)stepCount);
	{
		Vec3 vecOffsetStep;
		
		copyVec3(vecOffsetScaled, vecOffsetStep);
		
		copyVec3(posReachedOut, motion.last_pos);
		addVec3(posReachedOut, vecOffsetStep, motion.pos);
		copyVec3(vecOffsetStep, motion.vel);

		motion.filterBits = WC_QUERY_BITS_ENTITY_MOVEMENT;
		
		if(!mmGetWorldCollGridCellFG(mm, motion.last_pos, &motion.wcCell)){
			break;
		}

		PERFINFO_AUTO_START("worldMoveMe", 1);
			worldMoveMe(&motion);
		PERFINFO_AUTO_STOP();
		
		assert(FINITEVEC3(motion.pos));
		
		copyVec3(	motion.pos,
					posReachedOut);

		if(hitGroundOut){
			*hitGroundOut = motion.hit_ground;
		}
	}
	FOR_END;

	return 1;
}

void mmFactionUpdateFactionMatrix(const CritterFactionMatrix* factionMatrix){
	if(factionMatrix){
		if (!mgState.factionMatrix){
			mgState.factionMatrix = malloc(sizeof(CritterFactionMatrix));
		}

		*mgState.factionMatrix = *factionMatrix;
	}
}


void mmSetEntityFactionIndex(	MovementManager* mm,
								S32 factionIndex)
{
	if(mm){
		mm->fg.factionIndexMutable = (U32)factionIndex;
	}
}

static void mmPrintAnimValues(	const MovementAnimValues* anim,
								U32 spc,
								U32 cpc)
{
	printf("s%u/c%u: ", spc, cpc);

	EARRAY_INT_CONST_FOREACH_BEGIN(anim->values, i, isize);
	{
		U32							index = MM_ANIM_VALUE_GET_INDEX(anim->values[i]);
		MovementRegisteredAnimBit*	bit;

		if(mmGetLocalAnimBitFromHandle(&bit, index, 0)){
			U32 color = COLOR_RED | COLOR_GREEN | COLOR_BLUE;
			U32 pc = 0;

			switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
				xcase MAVT_STANCE_OFF:{
					color = COLOR_BRIGHT | COLOR_RED;
				}
				xcase MAVT_STANCE_ON:{
					color = COLOR_BRIGHT | COLOR_GREEN;
				}
				xcase MAVT_ANIM_TO_START:{
					color = COLOR_BRIGHT | COLOR_BLUE;
				}
				xcase MAVT_FLAG:{
					color = COLOR_BLUE;
				}
				xcase MAVT_LASTANIM_ANIM:{
					pc = anim->values[++i];
					color = COLOR_BRIGHT | COLOR_RED | COLOR_GREEN;
				}
				xcase MAVT_LASTANIM_FLAG:{
					color = COLOR_RED | COLOR_GREEN;
				}
			}

			printfColor(color, "%s%s", bit->bitName, i == isize - 1 ? "" : ", ");
		}
	}
	EARRAY_FOREACH_END;

	printf("\n");
}

static void mmPrintOutputsFG(MovementManager* mm){
	if(	!mm ||
		!mmIsForegroundThreadForLogging())
	{
		return;
	}

	{
		MovementNetOutput* no;

		printf("Net stances: ");
		EARRAY_INT_CONST_FOREACH_BEGIN(mm->fg.net.stanceBits, i, isize);
		{
			MovementRegisteredAnimBit*	bit;

			if(mmGetLocalAnimBitFromHandle(&bit, mm->fg.net.stanceBits[i], 0)){
				printf("%s%s", i ? ", " : "", bit->bitName);
			}
		}
		EARRAY_FOREACH_END;
		printf("\n");

		for(no = mm->fg.net.outputList.tail;
			no;
			no = (no == mm->fg.net.outputList.head ? NULL : no->prev))
		{
			mmPrintAnimValues(	&no->data.anim,
								no->pc.server,
								no->pc.client);
		}
	}

	{
		MovementThreadData* td = MM_THREADDATA_FG(mm);
		MovementOutput*		o;

		printf("Local stances: ");
		EARRAY_INT_CONST_FOREACH_BEGIN(td->toFG.stanceBits, i, isize);
		{
			MovementRegisteredAnimBit*	bit;

			if(mmGetLocalAnimBitFromHandle(&bit, td->toFG.stanceBits[i], 0)){
				printf("%s%s", i ? ", " : "", bit->bitName);
			}
		}
		EARRAY_FOREACH_END;
		printf("\n");

		if(mm->fg.view){
			const MovementManagerFGView* v = mm->fg.view;
			printf("View stances: ");
			EARRAY_INT_CONST_FOREACH_BEGIN(v->local.stanceBits, i, isize);
			{
				MovementRegisteredAnimBit*	bit;

				if(mmGetLocalAnimBitFromHandle(&bit, v->local.stanceBits[i], 0)){
					printf("%s%s", i ? ", " : "", bit->bitName);
				}
			}
			EARRAY_FOREACH_END;
			printf("\n");
		}

		for(o = td->toFG.outputList.tail;
			o;
			o = (o == td->toFG.outputList.head ? NULL : o->prev))
		{
			printf("%s", o->flags.animViewedLocal ? "v" : " ");

			mmPrintAnimValues(	&o->data.anim,
								o->pc.server,
								o->pc.client);
		}
	}
}

void wrapped_mmHandleBadAnimData(	MovementManager* mm,
									const char* fileName,
									U32 fileLine)
{
	if(!mgState.flags.disableAnimAssert){
		assert(0);
	}

	if(!mm){
		return;
	}

	csEnter(&mgState.animError.cs);
	{
		if(!mgState.animError.st){
			mgState.animError.st = stashTableCreateAddress(100);
		}

		if(!stashAddPointer(mgState.animError.st, mm, NULL, false)){
			mm = NULL;
		}
	}
	csLeave(&mgState.animError.cs);

	if(!mm){
		return;
	}

	if(mm->flags.debugging){
		mmPrintOutputsFG(mm);
		printfColor(COLOR_BRIGHT|COLOR_RED,
					"mm 0x%p had bad anim data (%s:%u).\n",
					mm,
					fileName,
					fileLine);

		mmSetDebugging(mm, 0);
	}
}

void mmCopyAnimValueToSizedStack(	U32** dest,
									const U32*const src,
									const char *pcFuncName)
{
	if (eaiSize(&src) > eaiCapacity(dest))
	{
		int i;

		Errorf("Movement Manager: attempted stack overflow with %i extra values in %s",
			eaiSize(&src)-eaiCapacity(dest),
			pcFuncName);

		for (i = 0; i < eaiCapacity(dest); i++)
			eaiPush(dest, src[i]);
	}
	else
	{
		eaiCopy(dest, &src);
	}
}

static void mmConfigLoadInternal(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading MovementManagerConfig... ");

	StructReset(parse_MovementManagerConfig, &g_MovementManagerConfig);

	ParserLoadFiles(NULL, 
		"defs/config/MovementManagerConfig.def", 
		"MovementManagerConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_MovementManagerConfig,
		&g_MovementManagerConfig);

	loadend_printf(" done.");
}

AUTO_STARTUP(MovementManagerConfig);
void mmConfigLoad(void)
{
	mmConfigLoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/MovementManagerConfig.def", mmConfigLoadInternal);
}

#include "autogen/EntityMovementManager_c_ast.c"
