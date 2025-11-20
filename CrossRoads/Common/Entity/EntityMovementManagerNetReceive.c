
#include "EntityMovementManagerPrivate.h"
#include "net/net.h"
#include "net/netpacketutil.h"
#include "StructNet.h"
#include "wininclude.h"
#include "cmdparse.h"
#include "BlockEarray.h"
#include "autogen/EntityMovementManager_h_ast.h"
#include "StringCache.h"

//#define MM_ASSERT_ON_BAD_CLIENT_INPUT 1

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

static S32 mmClientInputStepHasManagerFG(	MovementClientInputStep* mciStep,
											MovementManager* mm)
{
	EARRAY_CONST_FOREACH_BEGIN(mciStep->miSteps, i, isize);
	{
		MovementInputStep* miStep = mciStep->miSteps[i];
		
		if(miStep->mm == mm){
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

static S32 mmInputStepReadPacket(	MovementClient* mc,
									MovementManager* mm,
									MovementInputStep** miStepOut,
									Packet* pak,
									const MovementInputStepProcessCount* pc)
{
	MovementInputStep*	miStep;
	U32					stepCount;
	
	if(	!miStepOut ||
		!pak)
	{
		return 0;
	}
	
	mmInputStepCreate(mc, miStepOut);
	
	miStep = *miStepOut;
	miStep->pc = *pc;
	
	stepCount = pktGetBitsPack(pak, 1);
	
	FOR_BEGIN(i, (S32)stepCount);
	{
		MovementInputEvent*		mie = NULL;
		MovementInputValueIndex mivi;
		U32						eventHeader;
		
		if(pktEnd(pak)){
			break;
		}
		
		eventHeader = pktGetBits(pak, 8);
		
		mivi = eventHeader & BIT_RANGE(0, 4);
		
		mmInputEventCreateNonZeroed(&mie,
									mc,
									NULL,
									mivi,
									0);

		if(INRANGE(mivi, MIVI_BIT_LOW, MIVI_BIT_HIGH)){
			mie->value.bit = !!(eventHeader & BIT(5));
			mie->value.flags.isDoubleTap =	mie->value.bit &&
											eventHeader & BIT(6);

			if(MMLOG_IS_ENABLED(mm)){
				const char* name;
				
				mmGetInputValueIndexName(mivi, &name);

				mmLog(	mm,
						NULL,
						"[net.input] c%u,s%u,ss%u: Received input bit[%s] = %d.",
						pc->client,
						pc->server,
						pc->serverSync,
						name,
						mie->value.bit);
			}
		}
		else if(INRANGE(mivi, MIVI_F32_LOW, MIVI_F32_HIGH)){
			mie->value.f32 = pktGetF32(pak);

			mmSanitizeInputValueF32(mivi, &mie->value.f32);

			if(MMLOG_IS_ENABLED(mm)){
				const char* name;
				
				mmGetInputValueIndexName(mivi, &name);

				mmLog(	mm,
						NULL,
						"[net.input] c%u,s%u,ss%u: Received input f32[%s] = %1.3f [%8.8x].",
						pc->client,
						pc->server,
						pc->serverSync,
						name,
						mie->value.f32,
						*(S32*)&mie->value.f32);
			}
		}
		else if(mivi == MIVI_DEBUG_COMMAND){
			mie->commandMutable = pktMallocString(pak);
			mie->value.command = mie->commandMutable;

			mmLog(	mm,
					NULL,
					"[net.input] c%u,s%u,ss%u: Received debug cmd: \"%s\".",
					pc->client,
					pc->server,
					pc->serverSync,
					mie->value.command);
		}
		else if(mivi == MIVI_RESET_ALL_VALUES){
			mie->value.u32 = pktGetU32(pak);

			mmLog(	mm,
					NULL,
					"[net.input] c%u,s%u,ss%u: Received RESET_ALL_VALUES (setPosVersion %u).",
					pc->client,
					pc->server,
					pc->serverSync,
					mie->value.u32);
		}else{
			mmInputEventDestroy(&mie);

			mmLog(	mm,
					NULL,
					"[net.input] c%u,s%u,ss%u: Received invalid mivi %u.",
					pc->client,
					pc->server,
					pc->serverSync,
					mivi);
		}

		// Add to the event list.

		if(mie){
			if(miStep->mieList.tail){
				assert(miStep->mieList.head);
				assert(!miStep->mieList.tail->next);
				miStep->mieList.tail->next = mie;
			}else{
				assert(!miStep->mieList.head);
				miStep->mieListMutable.head = mie;
			}
			
			mie->prev = miStep->mieList.tail;
			mie->next = NULL;
			
			miStep->mieListMutable.tail = mie;
		}
	}
	FOR_END;

	return 1;
}

static void mmStatsPacketUpdateReceiveTime(	MovementClientStatsPackets* packets,
											MovementClientStatsPacket* packet,
											const U32 msCurTime)
{
	if(packet){
		U32 msExpectedReceiveTime =	packets->msFirstReceiveLocalTime + 
									packets->msAccReceiveDeltaTime;

		packet->msOffsetFromExpectedTime =	subS32(	msCurTime,
													msExpectedReceiveTime);
		
		if(packet->msOffsetFromExpectedTime < 0){
			packets->msFirstReceiveLocalTime += packet->msOffsetFromExpectedTime;
		}
											
		packet->msLocalOffsetFromLastPacket =	msCurTime -
												packets->msPreviousReceiveLocalTime;
	}
	
	packets->msPreviousReceiveLocalTime = msCurTime;
}

void mmReceiveFromClient(	MovementClient* mc,
							Packet* pak)
{
	static MovementManager**	managers = NULL;
	U32							stepCount;
	U32							startProcessCount;
	U32							mmCount;
	U32							msTimeSentDelta;
	MovementClientStatsPackets*	packets = mc->stats.packets;
	MovementClientStatsPacket*	packet = NULL;
	
	mc->packetCount++;
	
	msTimeSentDelta = pktGetBitsAuto(pak);
	
	if(packets){
		U32 msCurTime = timeGetTime();

		if(!packets->msFirstReceiveLocalTime){
			packets->msFirstReceiveLocalTime = msCurTime;
			packets->msPreviousReceiveLocalTime = msCurTime;
		}else{
			packets->msAccReceiveDeltaTime += msTimeSentDelta;
		}

		if(packets->fromClient.count < beaUSize(&packets->fromClient.packets)){
			packet =	packets->fromClient.packets +
						packets->fromClient.count++;

			ZeroStruct(packet);
		}
		
		mmStatsPacketUpdateReceiveTime(packets, packet, msCurTime);
	}

	if(!pktGetBits(pak, 1)){
		return;
	}
	
	startProcessCount = pktGetBits(pak, 32);
	stepCount = pktGetBitsAuto(pak);
	mmCount = pktGetBitsAuto(pak);
	
	FOR_BEGIN(i, (S32)mmCount);
	{
		S32					index;
		Entity*				e;
		MovementManager*	mm;

		if(pktEnd(pak)){
			break;
		}

		index = pktGetBitsAuto(pak);
		e = SAFE_ENTITY_FROM_INDEX(index);
		mm = SAFE_MEMBER(e, mm.movement);
		
		if(mc == SAFE_MEMBER2(mm, fg.mcma, mc)){
			eaPush(&managers, mm);
		}else{
			eaPush(&managers, NULL);
		}
	}
	FOR_END;
	
	FOR_BEGIN(i, (S32)stepCount);
		MovementClientInputStep*	mciStep;
		const U32					clientProcessCount = startProcessCount + i * 2;
		
		if(pktEnd(pak)){
			break;
		}
		
		// Allocate an mciStep.
		
		if(mc->available.mciStepList.head){
			mciStep = mc->available.mciStepList.head;
			mc->available.mciStepListMutable.head = mciStep->next;
			
			if(!mc->available.mciStepList.head){
				mc->available.mciStepListMutable.tail = NULL;
			}

			--mc->available.mciStepListMutable.count;
			
			ZeroStruct(&mciStep->flags);
			assert(!eaSize(&mciStep->miSteps));
		}else{
			mciStep = callocStruct(MovementClientInputStep);
		}
		
		// Add mciStep to the mciStepList.
		
		if(mc->mciStepList.tail){
			assert(mc->mciStepList.head);
			assert(!mc->mciStepList.tail->next);
			mc->mciStepList.tail->next = mciStep;
		}else{
			assert(!mc->mciStepList.head);
			mc->mciStepListMutable.head = mciStep;
		}
		
		mciStep->prev = mc->mciStepList.tail;
		mciStep->next = NULL;
		
		mc->mciStepListMutable.tail = mciStep;
		mc->mciStepListMutable.count++;
		
		// Read the sync process count.
		
		mciStep->pc.client = clientProcessCount;
		
		if(pktGetBits(pak, 1)){
			// Has a sync.
			
			mciStep->pc.serverSync = pktGetBits(pak, 32);
		}
		
		EARRAY_CONST_FOREACH_BEGIN(managers, j, jsize);
		{
			MovementManager*	mm = managers[j];
			MovementInputStep*	miStep;
			
			if(pktEnd(pak)){
				break;
			}
			
			if(!mmInputStepReadPacket(mc, mm, &miStep, pak, &mciStep->pc)){
				break;
			}
			
			#if MM_ASSERT_ON_BAD_CLIENT_INPUT
			{
				assertmsgf(	!mmClientInputStepHasManagerFG(mciStep, mm),
							"Manager %d (in 0x%8.8p, %d) is already in the step!",
							j,
							managers,
							mmCount);
			}
			#endif
			
			if(	!mm ||
				mmClientInputStepHasManagerFG(mciStep, mm))
			{
				mmInputStepReclaim(mc, miStep);
			}else{
				eaPush(	&mciStep->miStepsMutable,
						miStep);
						
				miStep->mm = mm;
				miStep->mciStep = mciStep;

				mmLog(	mm,
						NULL,
						"[net.receive] Received input step %d, sync %d",
						miStep->pc.client,
						miStep->pc.serverSync);
			}
		}
		EARRAY_FOREACH_END;
	FOR_END;
	
	eaSetSize(&managers, 0);
}

AUTO_COMMAND;
void mmNetAutoDebugStartCollectingData(void){
	if(FALSE_THEN_SET(mgState.fg.netReceiveMutable.autoDebug.flags.collectingData)){
		mgState.fg.netReceiveMutable.autoDebug.msTimeStarted = timeGetTime();
		mgState.fg.mc.netSend.flags.autoSendStats = 1;
		mmClientSendFlagToServer("StatsSetFramesEnabled", 1);
		mmClientSendFlagToServer("StatsSetPacketTimingEnabled", 1);
	}
}

static void mmNetAutoDebugStopCollectingData(void){
	if(TRUE_THEN_RESET(mgState.fg.netReceiveMutable.autoDebug.flags.collectingData)){
		mgState.fg.netReceiveMutable.autoDebug.msTimeStarted = 0;
		mgState.fg.mc.netSend.flags.autoSendStats = 0;

		if(!mgState.fg.mc.netSend.flags.sendStatsFrames){
			mmClientSendFlagToServer("StatsSetFramesEnabled", 0);
		}
		
		if(!mgState.fg.mc.netSend.flags.sendStatsPacketTiming){
			mmClientSendFlagToServer("StatsSetPacketTimingEnabled", 0);
		}
	}
}

static void mmNetAutoDebugSendResults(void){
	if(mgState.fg.netReceive.autoDebug.flags.collectingData){
		MovementClientStatsStored	stats = {0};
		char*						statsString = NULL;
		Packet*						pak;
		
		mmNetAutoDebugStopCollectingData();
		
		stats.frames = mgState.fg.mc.stats.frames;
		
		if(mgState.fg.mc.stats.packets){
			stats.packetsFromClient = &mgState.fg.mc.stats.packets->fromClient;
			stats.packetsFromServer = &mgState.fg.mc.stats.packets->fromServer;
		}
		
		if(mmClientPacketToServerCreate(&pak, "AutoDebugResults")){
			pktSendStruct(pak, &stats, parse_MovementClientStatsStored);
			mmClientPacketToServerSend(&pak);
		}
	}
}

static void mmUpdateClientToServerSync(void){
	S32 newOffset = mgState.fg.netReceive.cur.pc.serverSync -
					mgState.fg.netReceive.cur.pc.client;

	CopyStructsFromOffset(	mgState.fg.netReceiveMutable.history.clientToServerSync + 1,
							-1,
							ARRAY_SIZE(mgState.fg.netReceive.history.clientToServerSync) - 1);

	mgState.fg.netReceiveMutable.history.clientToServerSync[0] = newOffset;

	if(newOffset != mgState.fg.netReceive.cur.offset.clientToServerSync){
		U32 msTimeCur = timeGetTime();

		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.mc.mcmas, i, isize);
		{
			MovementManager* mm = mgState.fg.mc.mcmas[i]->mm;
			
			mmLog(	mm,
					NULL,
					"[net.sync] New offset, client to server sync: %d",
					newOffset);
		}
		EARRAY_FOREACH_END;
		
		if(!mgState.fg.netReceive.autoDebug.msTimeStarted){
			// Check if we should start auto debug.
			
			#if 0
			{
				// Removed temporarily until server impact of receiving data is reduced.
				if(	msTimeCur - mgState.fg.netReceive.msTimeConnected >= 10 * 1000 &&
					abs(mgState.fg.netReceive.cur.offset.clientToServerSync - newOffset) >=
						MM_PROCESS_COUNTS_PER_STEP * 3)
				{
					mgState.fg.netReceiveMutable.autoDebug.msTimeStarted = msTimeCur;
					mgState.fg.netReceiveMutable.autoDebug.changeCount = 0;
					mgState.fg.netReceiveMutable.autoDebug.flags.collectingData = 0;
				}
			}
			#endif
		}
		else if(!mgState.fg.netReceive.autoDebug.flags.collectingData){
			// Count how many stalls happen.

			if(	abs(mgState.fg.netReceive.cur.offset.clientToServerSync - newOffset) >
				MM_PROCESS_COUNTS_PER_STEP * 3)
			{
				if(++mgState.fg.netReceiveMutable.autoDebug.changeCount >= 5){
					mmNetAutoDebugStartCollectingData();
				}
			}
			
			if( !mgState.fg.netReceive.autoDebug.flags.collectingData &&
				msTimeCur - mgState.fg.netReceive.autoDebug.msTimeStarted >= 30 * 1000)
			{
				mgState.fg.netReceiveMutable.autoDebug.msTimeStarted = 0;
			}
		}

		mgState.fg.netReceiveMutable.cur.offset.clientToServerSync = newOffset;
	}
	
	if(	mgState.fg.netReceive.autoDebug.flags.collectingData &&
		timeGetTime() - mgState.fg.netReceive.autoDebug.msTimeStarted >= 30 * 1000)
	{
		// Send results to errortracker.
		
		mmNetAutoDebugSendResults();
	}
}

static void mmUpdateServerSyncToServer(void){
	U32 newOffset = mgState.fg.netReceive.cur.pc.server -
					mgState.fg.netReceive.cur.pc.serverSync;

	CopyStructsFromOffset(	mgState.fg.netReceiveMutable.history.serverSyncToServer + 1,
							-1,
							ARRAY_SIZE(mgState.fg.netReceive.history.serverSyncToServer) - 1);

	mgState.fg.netReceiveMutable.history.serverSyncToServer[0] = newOffset;

	if(newOffset != mgState.fg.netReceive.cur.offset.serverSyncToServer){
		EARRAY_CONST_FOREACH_BEGIN(mgState.fg.mc.mcmas, i, isize);
		{
			MovementManager* mm = mgState.fg.mc.mcmas[i]->mm;

			mmLog(	mm,
					NULL,
					"[net.sync] New offset, server sync to server: %u",
					newOffset);
		}
		EARRAY_FOREACH_END;
		
		mgState.fg.netReceiveMutable.cur.offset.serverSyncToServer = newOffset;
		
		
	}
}

static void mmUpdateServerSyncOffsets(void){
	mmUpdateClientToServerSync();
	mmUpdateServerSyncToServer();
}

static void mmReceiveLocalManagerList(Packet* pak){
	S32*	newManagerIndexes = NULL;
	S32		count;
	
	count = pktGetBitsAuto(pak);
	
	FOR_BEGIN(i, count);
	{
		S32 index = pktGetBitsAuto(pak);
		S32 isNew = pktGetBits(pak, 1);

		eaiPush(&newManagerIndexes, index);
		
		if(	isNew &&
			eaiFind(&mgState.fg.netReceive.managerIndexes, index) >= 0)
		{
			Entity*				e = ENTITY_FROM_INDEX(index);
			MovementManager*	mm = SAFE_MEMBER(e, mm.movement);
			
			mmInputEventResetAllValues(mm);
		}
	}
	FOR_END;
	
	EARRAY_INT_CONST_FOREACH_BEGIN(mgState.fg.netReceive.managerIndexes, i, isize);
	{
		S32 index = mgState.fg.netReceive.managerIndexes[i];
		
		if(eaiFind(&newManagerIndexes, index) < 0){
			Entity*				e = ENTITY_FROM_INDEX(index);
			MovementManager*	mm = SAFE_MEMBER(e, mm.movement);
			
			// Existing index not found in new list.
			
			mmDetachFromClient(	mm,
								NULL);
		}
	}
	EARRAY_FOREACH_END;

	eaiDestroy(&mgState.fg.netReceiveMutable.managerIndexes);
	
	mgState.fg.netReceiveMutable.managerIndexes = newManagerIndexes;
}

static void mmReceiveSyncHeaderFromServer(Packet* pak){
	U32 firstByte;
	
	MM_CHECK_STRING_READ(pak, "syncHeaderStart");
	
	START_BIT_COUNT(pak, "firstByte");
	firstByte = pktGetBits(pak, MM_CLIENT_NET_SYNC_HEADER_BIT_COUNT);
	STOP_BIT_COUNT(pak);
	
	mgState.fg.netReceiveMutable.flags.hasStateBG = !!(firstByte & BIT(0));

	if(mgState.fg.netReceive.flags.hasStateBG){
		U32 cpcDelta = firstByte >> 4;
		
		if(cpcDelta == BIT_RANGE(0, 3)){
			mgState.fg.netReceiveMutable.cur.pc.client = pktGetBits(pak, 32);
		}else{
			mgState.fg.netReceiveMutable.cur.pc.client += cpcDelta * MM_PROCESS_COUNTS_PER_STEP;
		}
		
		if(firstByte & BIT(1)){
			mgState.fg.netReceiveMutable.cur.pc.serverSync = mgState.fg.netReceive.cur.pc.server;
		}else{
			U32 spcDelta;
			
			START_BIT_COUNT(pak, "spcDelta");
			spcDelta = pktGetBitsAuto(pak);
			STOP_BIT_COUNT(pak);
			
			mgState.fg.netReceiveMutable.cur.pc.serverSync = 
														mgState.fg.netReceive.cur.pc.server -
														spcDelta;
		}
		
		// Check for a forcedStepCount.
		
		if(firstByte & BIT(3)){
			mgState.fg.netReceiveMutable.cur.forcedStepCount = pktGetBitsAuto(pak);
		}else{
			mgState.fg.netReceiveMutable.cur.forcedStepCount = 0;
		}
	}

	mmUpdateServerSyncOffsets();

	// Check for a local manager list update.

	if(firstByte & BIT(2)){
		mmReceiveLocalManagerList(pak);
	}
	
	MM_CHECK_STRING_READ(pak, "syncHeaderStop");
}

static S32 mmFindRequesterByNetHandleFG(MovementRequester** mrOut,
										MovementManager* mm,
										U32 netHandle)
{
	if(!mrOut){
		return 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester* mr = mm->fg.requesters[i];

		if(	mr->fg.netHandle == netHandle &&
			!mr->fg.flags.destroyed)
		{
			*mrOut = mr;
			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 mmFindRequesterWithoutNetHandleByClassID(MovementRequester** mrOut,
													MovementManager* mm,
													U32 mrcID)
{
	if(!mrOut){
		return 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester* mr = mm->fg.requesters[i];

		if(	!mr->fg.netHandle &&
			!mr->fg.flags.destroyed &&
			mr->mrc->id == mrcID)
		{
			*mrOut = mr;
			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 mmGetClassFromID(MovementRequesterClass** mrcOut,
							U32 id)
{
	if(!mrcOut){
		return 0;
	}

	if(id >= eaUSize(&mgState.mr.idToClass)){
		return 0;
	}

	*mrcOut = mgState.mr.idToClass[id];

	return !!*mrcOut;
}

static void mrSendMsgTranslateServerToClient(	MovementRequester* mr,
												void* bg,
												void* sync,
												void* syncPublic)
{
	MovementRequesterMsgPrivateData pd;
	
	mmRequesterMsgInitFG(&pd, NULL, mr, MR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT);
	
	ZeroStruct(&pd.msg.in.userStruct);
	pd.msg.in.userStruct.bg = bg;
	pd.msg.in.userStruct.sync = sync;
	pd.msg.in.userStruct.syncPublic = syncPublic;

	mmRequesterMsgSend(&pd);	
}

static void mrReceiveSetUpdatedToBG(MovementRequester* mr,
									MovementThreadData* td,
									MovementRequesterThreadData* mrtd)
{
	td->toBG.flagsMutable.hasToBG = 1;
	td->toBG.flagsMutable.mrHasUpdate = 1;

	if(	!mr->fg.flags.sentRemoveToBG &&
		FALSE_THEN_SET(mrtd->toBG.flagsMutable.hasUpdate))
	{
		mmExecListAddHead(	&td->toBG.melRequesters,
							&mrtd->toBG.execNode);
	}
}

static void mmReceiveRequesterSyncDataFromServer(	MovementManager* mm,
													MovementRequester* mr,
													Packet* pak,
													const S32 receiveBGStates,
													const S32 receiveOwnedDataClassBits,
													const S32 receiveHandledMsgs,
													const S32 receiveBG,
													const S32 receiveSyncUpdated,
													const S32 receiveSyncStructs)
{
	MovementThreadData*							td = MM_THREADDATA_FG(mm);
	MovementRequesterThreadData*				mrtd = MR_THREADDATA_FG(mr);
	MovementRequesterClass*						mrc = mr->mrc;
	MovementRequesterThreadDataToBGPredict*		predict = mrtd->toBG.predict;

	if(!predict){
		predict = mrtd->toBG.predict = callocStruct(MovementRequesterThreadDataToBGPredict);
	}

	if(receiveBGStates){
		mrReceiveSetUpdatedToBG(mr, td, mrtd);

		// Receive the owned data class bits.
		
		if(receiveOwnedDataClassBits){
			predict->ownedDataClassBits = pktGetBits(pak, MDC_COUNT);
			
			mr->fg.net.prev.ownedDataClassBits = predict->ownedDataClassBits;
		}else{
			predict->ownedDataClassBits = mr->fg.net.prev.ownedDataClassBits;
		}
		
		// Receive the handled msgs mask.
		
		if(receiveHandledMsgs){
			predict->handledMsgs = pktGetBits(pak, MR_HANDLED_MSGS_BIT_COUNT);
			
			mr->fg.net.prev.handledMsgs = predict->handledMsgs;
		}else{
			predict->handledMsgs = mr->fg.net.prev.handledMsgs;
		}

		// Receive the BG struct.
		
		mmStructAllocIfNull(mrc->pti.bg,
							mr->fg.net.prev.userStruct.bg,
							mm);
							
		if(MMLOG_IS_ENABLED(mm)){
			mmLogSyncUpdate(mr,
							mrc->pti.bg,
							mr->fg.net.prev.userStruct.bg,
							"Previous received",
							"bg");
		}

		if(receiveBG){
			#if MM_VERIFY_SENT_SYNC_STRUCTS
				S32		receivedPrevBG = 0;
				void*	bgTest = StructCreateVoid(mrc->pti.bg);

				if(pktGetBits(pak, 1)){
					receivedPrevBG = 1;

					ParserRecv(	mrc->pti.bg,
								pak,
								bgTest,
								0);
				}
					
				if(StructCompare(	mrc->pti.bg,
									bgTest,
									mr->fg.net.prev.userStruct.bg,
									0, 0, 0))
				{
					assertmsg(0, "Previously received struct doesn't match the server.");
				}
			#endif
			
			ParserRecv(	mrc->pti.bg,
						pak,
						mr->fg.net.prev.userStruct.bg,
						0);
						
			#if MM_VERIFY_SENT_SYNC_STRUCTS
				StructResetVoid(mrc->pti.bg, bgTest);
					
				ParserRecv(	mrc->pti.bg,
							pak,
							bgTest,
							0);

				if(StructCompare(	mrc->pti.bg,
									bgTest,
									mr->fg.net.prev.userStruct.bg,
									0, 0, 0))
				{
					assertmsg(0, "New received struct doesn't match the server.");
				}
			#endif

			if(MMLOG_IS_ENABLED(mm)){
				mmLogSyncUpdate(mr,
								mrc->pti.bg,
								mr->fg.net.prev.userStruct.bg,
								"Current received",
								"bg");
			}
		}

		mmStructAllocAndCopy(	mrc->pti.bg,
								mrtd->toBG.predict->userStruct.serverBG,
								mr->fg.net.prev.userStruct.bg,
								mm);
	}
		
	// Receive the sync struct.

	mmStructAllocIfNull(mrc->pti.sync,
						mr->fg.net.prev.userStruct.sync,
						mm);

	if(MMLOG_IS_ENABLED(mm)){
		mmLogSyncUpdate(mr,
						mrc->pti.sync,
						mr->fg.net.prev.userStruct.sync,
						"Previous received",
						"sync");
	}

	if(receiveSyncUpdated){
		mgState.fg.netReceiveMutable.flags.setSyncProcessCount = 1;
		mgState.fg.netReceiveMutable.sync.spc = mgState.fg.netReceive.cur.pc.server + 2;

		mrtd->toBG.flagsMutable.hasSync = 1;

		mrReceiveSetUpdatedToBG(mr, td, mrtd);

		if(receiveSyncStructs){
			ParserRecv(	mrc->pti.sync,
						pak,
						mr->fg.net.prev.userStruct.sync,
						0);
					
			if(MMLOG_IS_ENABLED(mm)){
				mmLogSyncUpdate(mr,
								mrc->pti.sync,
								mr->fg.net.prev.userStruct.sync,
								"Current received",
								"sync");
			}
		}
	}
	
	mmStructAllocAndCopy(	mrc->pti.sync,
							mrtd->toBG.userStruct.sync,
							mr->fg.net.prev.userStruct.sync,
							mm);

	// Receive the syncPublic struct.
	
	if(mrc->pti.syncPublic){
		mmStructAllocIfNull(mrc->pti.syncPublic,
							mr->fg.net.prev.userStruct.syncPublic,
							mm);

		if(MMLOG_IS_ENABLED(mm)){
			mmLogSyncUpdate(mr,
							mrc->pti.syncPublic,
							mr->fg.net.prev.userStruct.syncPublic,
							"Previous received",
							"syncPublic");
		}

		if(receiveSyncStructs){
			ParserRecv(	mrc->pti.syncPublic,
						pak,
						mr->fg.net.prev.userStruct.syncPublic,
						0);

			if(MMLOG_IS_ENABLED(mm)){
				mmLogSyncUpdate(mr,
								mrc->pti.syncPublic,
								mr->fg.net.prev.userStruct.syncPublic,
								"Current received",
								"syncPublic");
			}
		}
		
		mmStructAllocAndCopy(	mrc->pti.syncPublic,
								mrtd->toBG.userStruct.syncPublic,
								mr->fg.net.prev.userStruct.syncPublic,
								mm);
	}
	
	mrSendMsgTranslateServerToClient(	mr,
										receiveBGStates ? predict->userStruct.serverBG : NULL,
										mrtd->toBG.userStruct.sync,
										mrtd->toBG.userStruct.syncPublic);
}

static void mmReceiveSyncPosRotFace(MovementManager* mm,
									Packet* pak,
									Vec3 posOut,
									S32 receivePos,
									Quat rotOut,
									S32 receiveRot,
									Vec2 pyFaceOut,
									S32 receiveFace)
{
	if(receivePos){
		pktGetVec3(	pak,
					posOut);
					
		mmLog(	mm,
				NULL,
				"[net.sync] Received sync pos (%1.2f, %1.2f, %1.2f).",
				vecParamsXYZ(posOut));

		//printf("sync pos: %1.2f, %1.2f, %1.2f\n", vecParamsXYZ(pos));
		
		copyVec3(	posOut,
					mm->fg.net.sync.pos);
	}else{
		copyVec3(	mm->fg.net.sync.pos,
					posOut);
	}
	
	if(receiveRot){
		pktGetQuat(	pak,
					rotOut);
					
		mmLog(	mm,
				NULL,
				"[net.sync] Received sync rot (%1.2f, %1.2f, %1.2f, %1.2f).",
				quatParamsXYZW(rotOut));

		copyQuat(	rotOut,
					mm->fg.net.sync.rot);
	}else{
		copyQuat(	mm->fg.net.sync.rot,
					rotOut);
	}
	
	if(receiveFace){
		pktGetVec2(	pak,
					pyFaceOut);
					
		mmLog(	mm,
				NULL,
				"[net.sync] Received face pitch yaw (%1.2f, %1.2f).",
				vecParamsXY(pyFaceOut));

		copyVec2(	pyFaceOut,
					mm->fg.net.sync.pyFace);
	}else{
		copyVec2(	mm->fg.net.sync.pyFace,
					pyFaceOut);
	}
}

static S32 mmRequesterReceiveSyncUpdate(MovementManager* mm,
										Packet* pak)
{
	U32							updateFlags;
	MovementRequester*			mr;
	MovementRequesterClass*		mrc = NULL;
	U32							mrNetHandle;
	U32							mrcID = 0;
	S32							destroyed;
	S32							receiveBGStates;
	S32							receiveOwnedDataClassBits;
	S32							receiveHandledMsgs;
	S32							receiveBG;
	S32							receiveSyncUpdated;
	S32							receiveSyncStructs;
	char						destroyedText[200];
	
	START_BIT_COUNT(pak, "updateFlags");
	updateFlags = pktGetBits(pak, MM_NET_REQUESTER_UPDATE_FLAGS_BIT_COUNT);
	STOP_BIT_COUNT(pak);
	
	if(!updateFlags){
		return 0;
	}

	destroyed = !!(updateFlags & BIT(0));
	receiveBGStates = !!(updateFlags & BIT(1));
	receiveOwnedDataClassBits = !!(updateFlags & BIT(2));
	receiveHandledMsgs = !!(updateFlags & BIT(3));
	receiveBG = !!(updateFlags & BIT(4));
	receiveSyncUpdated = !!(updateFlags & BIT(5));
	receiveSyncStructs = !!(updateFlags & BIT(6));
	
	START_BIT_COUNT(pak, "handleAndID");
		mrNetHandle = pktGetBits(pak, 32);
	STOP_BIT_COUNT(pak);

	#if MM_VERIFY_SENT_REQUESTERS
	{
		estrConcatf(&mm->fg.net.verifySent.estrLog,
					"Received handle %u (%s).\n",
					mrNetHandle,
					destroyed ? "destroyed" : "not destroyed");
	}
	#endif

	if(destroyed){
		pktGetString(pak, SAFESTR(destroyedText));
	}else{
		mrcID = pktGetBits(pak, 8);

		if(!mmGetClassFromID(&mrc, mrcID)){
			assertmsgf(0, "Invalid requester ID from server: %d", mrcID);
		}

		START_BIT_COUNT_STATIC(	pak,
								&mrc->perfInfo[MRC_PT_BITS_RECEIVED].perfInfo,
								mrc->perfInfo[MRC_PT_BITS_RECEIVED].name);
	}

	if(mmFindRequesterByNetHandleFG(&mr, mm, mrNetHandle)){
		if(!destroyed){
			assert(mr->mrc == mrc);
		}
	}else{
		if(destroyed){
			assertmsgf(	0,
						"Received destroy for unknown requester %d: %s",
						mrNetHandle,
						destroyedText);
		}

		if(mmFindRequesterWithoutNetHandleByClassID(&mr, mm, mrcID)){
			MovementThreadData*				td = MM_THREADDATA_FG(mm);
			MovementRequesterThreadData*	mrtd = MR_THREADDATA_FG(mr);

			mr->fg.netHandle = mrNetHandle;

			mrtd->toBG.flagsMutable.receivedNetHandle = 1;

			mrReceiveSetUpdatedToBG(mr, td, mrtd); 

			mrLog(	mr,
					NULL,
					"[net.mrSync] Set netHandle on existing requester.");
		}else{
			MovementRequesterThreadData* mrtd;
			
			// Doesn't already exist, so we have to create it.

			mmRequesterCreate(	mm,
								&mr,
								NULL,
								NULL,
								mrcID);

			mr->fg.flagsMutable.createdFromServer = 1;
			mr->fg.netHandle = mrNetHandle;

			mrLog(	mr,
					NULL,
					"[net.mrSync] Server create received.");
					
			mrtd = MR_THREADDATA_FG(mr);
			
			mrtd->toBG.flagsMutable.createdFromServer = 1;

			mmSendMsgRequesterCreatedFG(mm, mr);
		}
	}

	mr->fg.flagsMutable.receivedUpdateJustNow = 1;

	if(destroyed){
		mrLog(	mr,
				NULL,
				"[net.mrSync] Server destroy received.");
		
		ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.destroyedFromServer);

		mrDestroy(&mr);
	}else{
		assert(!mm->fg.flags.destroyed);
		
		mmReceiveRequesterSyncDataFromServer(	mm,
												mr,
												pak,
												receiveBGStates,
												receiveOwnedDataClassBits,
												receiveHandledMsgs,
												receiveBG,
												receiveSyncUpdated,
												receiveSyncStructs);
	}
	
	STOP_BIT_COUNT(pak);
	
	return 1;
}

#if !MM_VERIFY_SENT_REQUESTERS
	#define mmReceiveRequesterSyncVerify(mm, pak)
#else
static void mmReceiveRequesterSyncVerify(	MovementManager* mm,
											Packet* pak)
{
	static const MovementRequester**	mrs;
	static U32*							handles;
	static U32*							mrcIDs;
	static S32							doCheck = 1;

	eaClear(&mrs);

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		const MovementRequester* mr = mm->fg.requesters[i];

		if(	mr->fg.netHandle &&
			!mr->fg.flags.destroyedFromServer)
		{
			eaPush(&mrs, mr);
		}
	}
	EARRAY_FOREACH_END;

	eaiClear(&handles);
	eaiClear(&mrcIDs);

	while(1){
		U32 handle = pktGetBitsAuto(pak);
		U32 mrcID;

		if(!handle){
			break;
		}

		mrcID = pktGetBitsAuto(pak);

		eaiPush(&handles, handle);
		eaiPush(&mrcIDs, mrcID);
	}

	if(!doCheck){
		return;
	}

	EARRAY_INT_CONST_FOREACH_BEGIN(handles, j, jsize);
	{
		U32 handle = handles[j];
		U32 mrcID = mrcIDs[j];
		S32 found = 0;

		EARRAY_CONST_FOREACH_BEGIN(mrs, i, isize);
		{
			const MovementRequester* mr = mrs[i];

			if(mr->fg.netHandle == handle){
				assert(mrcID == mr->mrc->id);
				eaRemove(&mrs, i);
				found = 1;
				break;
			}
		}
		EARRAY_FOREACH_END;

		assert(found);
	}
	EARRAY_FOREACH_END;

	assert(!eaSize(&mrs));
}
#endif

static void mmReceiveSyncDataFromServer(MovementManager* mm,
										MovementThreadData* td,
										Packet* pak,
										S32 fullUpdate)
{
	MovementOutput* 	o = NULL;
	Vec3				pos;
	Quat				rot;
	Vec2				pyFace;
	S32					hasStateBG =	fullUpdate ||
										mgState.fg.netReceive.flags.hasStateBG;

	if(hasStateBG){
		U32 flags;
		S32 receiveForcedSetPos;
		S32 receiveForcedSetRot;
		S32 receivePos;
		S32 receiveRot;
		S32 receiveFace;
		
		MM_CHECK_STRING_READ(pak, "hasStateBG");
		
		START_BIT_COUNT(pak, "hasStateBG.flags");
		flags = pktGetBits(pak, MM_NET_SYNC_FLAGS_BIT_COUNT);
		STOP_BIT_COUNT(pak);
		
		receiveForcedSetPos = !!(flags & BIT(0));
		receiveForcedSetRot = !!(flags & BIT(1));
		receivePos = !!(flags & BIT(2));
		receiveRot = !!(flags & BIT(3));
		receiveFace = !!(flags & BIT(4));

		if(	fullUpdate ||
			receiveForcedSetPos)
		{
			mm->fg.mcma->setPos.versionReceived = pktGetU32(pak);

			td->toBG.setPosVersionMutable = mm->fg.mcma->setPos.versionReceived;
			td->toBG.flagsMutable.hasSetPosVersion = 1;
			td->toBG.flagsMutable.hasToBG = 1;

			mmInputEventResetAllValues(mm);

			if(!mgState.fg.flags.noSyncWithServer){
				mmLog(	mm,
						NULL,
						"[net.sync] Received instant setpos.");
										
				if(	!mgState.flags.noLocalProcessing &&
					FALSE_THEN_SET(mm->fg.flagsMutable.posNeedsForcedSetAck))
				{
					mmGetForcedSetCountFG(mm, td, &mm->fg.forcedSetCount.pos);
				}
			}
		}

		if(receiveForcedSetRot){
			td->toBG.flagsMutable.hasForcedSetRot = 1;
			td->toBG.flagsMutable.hasToBG = 1;
		}

		START_BIT_COUNT(pak, "posRotFace");
		{
			mmReceiveSyncPosRotFace(mm,
									pak,
									pos,
									receivePos,
									rot,
									receiveRot,
									pyFace,
									receiveFace);
		}
		STOP_BIT_COUNT(pak);
	}
	
	// Receive requester updates.

	#if MM_VERIFY_SENT_REQUESTERS
	{
		estrClear(&mm->fg.net.verifySent.estrLog);
	}
	#endif

	START_BIT_COUNT(pak, "requesters");
	while(mmRequesterReceiveSyncUpdate(mm, pak));
	STOP_BIT_COUNT(pak);

	mmReceiveRequesterSyncVerify(mm, pak);

	// Check which ones didn't receive updates.

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester* mr = mm->fg.requesters[i];
		
		if(!TRUE_THEN_RESET(mr->fg.flagsMutable.receivedUpdateJustNow)){
			mmReceiveRequesterSyncDataFromServer(	mm,
													mr,
													NULL,
													!!mr->fg.net.prev.userStruct.bg,
													0,
													0,
													0,
													0,
													0);
		}
	}
	EARRAY_FOREACH_END;

	if(	hasStateBG &&
		!mgState.fg.flags.noSyncWithServer &&
		!mgState.flags.noLocalProcessing)
	{
		// Tell the BG that a repredict is needed.
		
		MovementThreadDataToBGRepredict* r = td->toBG.repredict;
		
		if(!r){
			r = td->toBG.repredict = callocStruct(MovementThreadDataToBGRepredict);
		}
	
		mmLog(	mm,
				NULL,
				"[net.sync] Received client process count: %u",
				mgState.fg.netReceive.cur.pc.client);

		td->toBG.flagsMutable.doRepredict = 1;

		r->cpc = mgState.fg.netReceive.cur.pc.client;
		r->spc = mgState.fg.netReceive.cur.pc.serverSync;
		r->forcedStepCount = mgState.fg.netReceive.cur.forcedStepCount;

		copyVec3(pos, r->pos);
		copyQuat(rot, r->rot);
		copyVec2(pyFace, r->pyFace);

		mmLastAnimCopy(&r->lastAnim, &mm->fg.net.lastAnim);
	}
}

static void mmReceivePublicSyncDataFromServer(	MovementManager* mm,
												Packet* pak,
												S32 fullUpdate)
{
	while(1){
		S32							hasMore;
		U32 						mrNetHandle;
		U32 						mrcID;
		MovementRequester*			mr;
		MovementRequesterClass*		mrc;
		
		START_BIT_COUNT(pak, "hasMore");
			hasMore = pktGetBits(pak, 1);
		STOP_BIT_COUNT(pak);
		
		if(!hasMore){
			break;
		}

		START_BIT_COUNT(pak, "handleAndID");
			mrNetHandle = pktGetBits(pak, 32);
			mrcID = pktGetBits(pak, 8);
		STOP_BIT_COUNT(pak);
		
		if(!mmGetClassFromID(&mrc, mrcID)){
			assertmsgf(0, "Invalid requester ID from server: %d", mrcID);
		}
		
		assert(mrc->pti.syncPublic);

		if(mmFindRequesterByNetHandleFG(&mr, mm, mrNetHandle)){
			assert(mr->mrc->id == mrcID);
		}
		else if(mmFindRequesterWithoutNetHandleByClassID(&mr, mm, mrcID)){
			mr->fg.netHandle = mrNetHandle;
		}else{
			// Doesn't already exist, so we have to create it.

			mmRequesterCreate(	mm,
								&mr,
								NULL,
								NULL,
								mrcID);
			
			mr->fg.flagsMutable.createdFromServer = 1;
			mr->fg.netHandle = mrNetHandle;

			mmSendMsgRequesterCreatedFG(mm, mr);
		}

		if(!pktGetBits(pak, 1)){
			ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.destroyedFromServer);
			
			mrDestroy(&mr);
		}else{
			MovementRequesterThreadData*	mrtd = MR_THREADDATA_FG(mr);
			MovementThreadData*				td = MM_THREADDATA_FG(mm);

			mrtd->toBG.flagsMutable.hasSync = 1;

			mrReceiveSetUpdatedToBG(mr, td, mrtd);

			// Receive the public sync struct.

			mmStructAllocIfNull(mrc->pti.syncPublic,
								mr->fg.net.prev.userStruct.syncPublic,
								mm);

			ParserRecv(	mrc->pti.syncPublic,
						pak,
						mr->fg.net.prev.userStruct.syncPublic,
						0);
						
			mmStructAllocAndCopy(	mrc->pti.syncPublic,
									mrtd->toBG.userStruct.syncPublic,
									mr->fg.net.prev.userStruct.syncPublic,
									mm);
		}
	}
}

// Common code shared between mmClientReceiveHeaderFromServer and mmReceiveHeaderFromReplay.

void mmProcessHeader(void){
	if(	mgState.fg.netView.spcOffsetFromEnd.skip <= 0.f &&
		!mgState.fg.netView.spcOffsetFromEnd.normal)
	{
		// Ran out of buffer, so add some lag.
		
		mgState.fg.netViewMutable.spcOffsetFromEnd.lag += MM_PROCESS_COUNTS_PER_STEP;
	}
	
	if(	!mgState.flags.disableNetBufferAdjustment &&
		mgState.fg.netView.spcOffsetFromEnd.normal > 2 * MM_PROCESS_COUNTS_PER_STEP)
	{
		if(mgState.fg.netView.spcOffsetFromEnd.fast > MM_PROCESS_COUNTS_PER_SECOND * 3){
			mgState.fg.netViewMutable.spcOffsetFromEnd.fast = 0;
		}

		if(mgState.fg.netView.spcOffsetFromEnd.normal > MM_PROCESS_COUNTS_PER_SECOND){
			mgState.fg.netViewMutable.spcOffsetFromEnd.normal = 0;
		}else{
			mgState.fg.netViewMutable.spcOffsetFromEnd.normal -= MM_PROCESS_COUNTS_PER_STEP;
			mgState.fg.netViewMutable.spcOffsetFromEnd.fast += MM_PROCESS_COUNTS_PER_STEP;
		}
		
		if(mgState.debug.activeLogCount){
			EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
			{
				mmLog(	mgState.fg.managers[i],
						NULL,
						"[net.buffer] Adjusting net buffering: spc %d, offset normal %1.3f, fast %1.3f",
						mgState.fg.netReceive.cur.pc.server,
						mgState.fg.netView.spcOffsetFromEnd.normal,
						mgState.fg.netView.spcOffsetFromEnd.fast);
			}
			EARRAY_FOREACH_END;
		}
	}

	if(mgState.fg.netReceive.prev.pc.server){
		S32 spcDiff =	mgState.fg.netReceive.cur.pc.server -
						mgState.fg.netReceive.prev.pc.server;

		if(	spcDiff < 0 ||
			spcDiff > MM_PROCESS_COUNTS_PER_SECOND * 10)
		{
			// This only happens when connecting to a new server or in UDP networking.

			mgState.fg.netViewMutable.spcOffsetFromEnd.normal = 0;
		}
		else if(spcDiff > 0) {
			mgState.fg.netViewMutable.spcOffsetFromEnd.normal += spcDiff;
		}
	}
	
	// Check for skipping some buffer.
	
	if(mgState.fg.netView.spcOffsetFromEnd.skip >= mgState.fg.netView.spcOffsetFromEnd.normal){
		mgState.fg.netViewMutable.spcOffsetFromEnd.skip -= mgState.fg.netView.spcOffsetFromEnd.normal;
		mgState.fg.netViewMutable.spcOffsetFromEnd.normal = 0.f;
	}else{
		mgState.fg.netViewMutable.spcOffsetFromEnd.normal -= mgState.fg.netView.spcOffsetFromEnd.skip;
		mgState.fg.netViewMutable.spcOffsetFromEnd.skip = 0.f;
	}
	
	mgState.fg.netViewMutable.spcOffsetFromEnd.total =	mgState.fg.netView.spcOffsetFromEnd.normal +
														mgState.fg.netView.spcOffsetFromEnd.fast;

	mgState.fg.flagsMutable.sendNetReceiveToBG = 1;
}

static void mmClearAllNetOutputAnims(void){
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.managers, i, isize);
	{
		MovementManager*	mm = mgState.fg.managers[i];
		MovementNetOutput*	no;
		
		for(no = mm->fg.net.outputList.head;
			no;
			no = no->next)
		{
			no->animBitCombo = NULL;
		}
	}
	EARRAY_FOREACH_END;
}

static void mmReceiveAnimBitAndGeometryUpdate(Packet* pak){
	U32 updateFlags;
	
	START_BIT_COUNT(pak, "updateFlags");
		updateFlags = pktGetBits(pak, MM_NET_ANIM_GEO_HEADER_BIT_COUNT);
	STOP_BIT_COUNT(pak);
	
	if(!(updateFlags & BIT(0))){
		return;
	}
	
	if(updateFlags & BIT(1)){
		// Reset things received from server.
		
		mmAnimBitRegistryClear(&mgState.fg.netReceiveMutable.animBitRegistry);
		
		mmClearAllNetOutputAnims();

		mmClearGeometry();
	}
	
	// Receive new anim bits.
	
	if(updateFlags & BIT(2)){
		START_BIT_COUNT(pak, "bits");
		{
			U32 count = pktGetBitsAuto(pak) + 1;
			
			while(count--){
				char	bitName[MAX_PATH];
				S32		isFlashBit;
				
				pktGetString(pak, SAFESTR(bitName));
				isFlashBit = pktGetBits(pak, 1);
				
				mgState.fg.netReceiveMutable.animBitRegistry.flags.isServerRegistry = 1;
				
				mmRegisteredAnimBitCreate(	&mgState.fg.netReceiveMutable.animBitRegistry,
											bitName,
											isFlashBit,
											NULL);
			}
		}
		STOP_BIT_COUNT(pak);
	}
			
	// Receive new anim bit combos.
	
	if(updateFlags & BIT(3)){
		START_BIT_COUNT(pak, "combos");
		{
			U32 count = pktGetBitsAuto(pak) + 1;
			
			while(count--){
				U32								bitCount = pktGetBitsAuto(pak);
				static U32*						bits = NULL;
				MovementRegisteredAnimBitCombo* combo;
				
				eaiSetSize(&bits, 0);

				while(bitCount--){
					U32 bitHandle = pktGetBitsAuto(pak);
					
					assert(bitHandle < eaUSize(&mgState.fg.netReceive.animBitRegistry.handleToBit));
					
					eaiPush(&bits, bitHandle);
				}
				
				mmRegisteredAnimBitComboFind(	&mgState.fg.netReceiveMutable.animBitRegistry,
												&combo,
												NULL,
												bits);
				
				//printf("Received combo: \"%s\"\n", combo->keyName);
			}
		}
		STOP_BIT_COUNT(pak);
	}
		
	// Receive new geos.
		
	if(updateFlags & BIT(4)){
		START_BIT_COUNT(pak, "geos");
		{
			U32 count = pktGetBitsAuto(pak) + 1;
			
			while(count--){
				MovementGeometry* geo;
				
				geo = callocStruct(MovementGeometry);

				geo->index = eaPush(&mgState.fg.geosMutable, geo);

				geo->geoType = pktGetBitsAuto(pak);

				switch(geo->geoType){
					xcase MM_GEO_MESH:{
						geo->mesh.vertCount = pktGetBitsAuto(pak);
						geo->mesh.triCount = pktGetBitsAuto(pak);
				
						if(geo->mesh.vertCount){
							geo->mesh.verts = callocStructs(F32, geo->mesh.vertCount * 3);
						}
				
						FOR_BEGIN(i, (S32)geo->mesh.vertCount);
							pktGetVec3(pak, geo->mesh.verts + i * 3);
						FOR_END;
				
						if(geo->mesh.triCount){
							geo->mesh.tris = callocStructs(S32, geo->mesh.triCount * 3);
						}

						FOR_BEGIN(i, (S32)geo->mesh.triCount);
							pktGetIVec3(pak, geo->mesh.tris + i * 3);
						FOR_END;
					}

					xcase MM_GEO_GROUP_MODEL:{
						geo->model.modelName = allocAddString(pktGetStringTemp(pak));
					}

					xcase MM_GEO_WL_MODEL:{
						if(pktGetBitsAuto(pak)){
							geo->model.fileName = allocAddString(pktGetStringTemp(pak));
						}

						geo->model.modelName = allocAddString(pktGetStringTemp(pak));
					}
				}
			}
		}
		STOP_BIT_COUNT(pak);
	}
	
	// Receive new bodies.
	
	if(updateFlags & BIT(5)){
		START_BIT_COUNT(pak, "bodies");
		{
			U32 count = pktGetBitsAuto(pak) + 1;
			
			while(count--){
				MovementBody*	b;
				S32				partCount;
				S32				capsuleCount;
				
				b = callocStruct(MovementBody);
				
				mmBodyLockEnter();
				{
					b->index = eaPush(&mgState.bodiesMutable, b);
				}
				mmBodyLockLeave();
				
				b->radius = pktGetF32(pak);
				
				partCount = pktGetBitsAuto(pak);
				capsuleCount = pktGetBitsAuto(pak);
				
				FOR_BEGIN(i, partCount);
					MovementBodyPart*	p;
					U32					geoIndex;
					
					p = callocStruct(MovementBodyPart);
					
					eaPush(&b->parts, p);
					
					geoIndex = pktGetBitsAuto(pak);
					
					assert(geoIndex < eaUSize(&mgState.fg.geos));
					
					p->geo = mgState.fg.geos[geoIndex];

					pktGetVec3(pak, p->pos);
					pktGetVec3(pak, p->pyr);
				FOR_END;
				
				FOR_BEGIN(i, capsuleCount);
					Capsule* c;
					
					c = callocStruct(Capsule);
					
					eaPush(&b->capsules, c);
					
					pktGetVec3(pak, c->vStart);
					pktGetVec3(pak, c->vDir);
					c->fLength = pktGetF32(pak);
					c->fRadius = pktGetF32(pak);
					c->iType = pktGetU32(pak);
				FOR_END;
			}
		}
		STOP_BIT_COUNT(pak);
	}
}

static void mmClientReceiveStatsFrames(Packet* pak){
	START_BIT_COUNT(pak, "statsFrames");
	{
		MovementClientStatsFrames*	frames = mgState.fg.mc.stats.frames;
		S32							frameCount = pktGetBitsAuto(pak);
		
		if(!frames){
			frames = mgState.fg.mc.stats.frames = callocStruct(MovementClientStatsFrames);
			beaSetSize(&frames->frames, 2000);
		}
		
		FOR_BEGIN(i, frameCount);
		{
			MovementClientStatsFrame	fTemp;
			MovementClientStatsFrame*	f;
			U32							flags = pktGetBits(pak, 8);
			
			if(mgState.flags.netStatsPaused){
				f = &fTemp;
			}else{
				f = frames->frames + frames->count++;

				frames->count %= beaSize(&frames->frames);
			}
			
			ZeroStruct(f);
			ANALYSIS_ASSUME(f);
			
			#define GET(x, y) if(flags & BIT(x))y = pktGetBitsAuto(pak)
			GET(0, f->serverStepCount);
			GET(1, f->leftOverSteps);
			GET(2, f->behind);
			GET(3, f->usedSteps);
			GET(4, f->skipSteps);
			GET(5, f->consolidateStepsEarly);
			GET(6, f->consolidateStepsLate);
			#undef GET
			
			if(flags & BIT(7)){
				f->forcedSteps = pktGetBitsAuto(pak);
				f->flags.isCorrectionFrame = f->forcedSteps & 1;
				f->forcedSteps >>= 1;
			}
		}
		FOR_END;
	}
	STOP_BIT_COUNT(pak);
}

void mmClientRecordPacketSizeFromClient(MovementClient* mc,
										S32 useCurrentPacket,
										U32 size)
{
	MovementClientStatsPackets*	packets = SAFE_MEMBER(mc, stats.packets);
	MovementClientStatsPacket*	packet;
	
	if(	!packets ||
		mgState.flags.netStatsPaused)
	{
		return;
	}
	
	if(useCurrentPacket){
		packet =	packets->fromClient.packets
					+
					(	packets->fromClient.count +
						beaSize(&packets->fromClient.packets) - 
						1) %
					beaSize(&packets->fromClient.packets);

		packet->size = size;
	}
	else if(packets->fromClient.count < beaUSize(&packets->fromClient.packets)){
		packet =	packets->fromClient.packets +
					packets->fromClient.count++;
					
		ZeroStruct(packet);
		ANALYSIS_ASSUME(packet);

		packet->flags.notMovementPacket = 1;
		packet->size = size;
	}
}

void mmClientRecordPacketSizeFromServer(S32 useCurrentPacket,
										U32 size)
{
	MovementClientStatsPackets*	packets = mgState.fg.mc.stats.packets;
	MovementClientStatsPacket*	packet;
	
	if(	!packets ||
		mgState.flags.netStatsPaused)
	{
		return;
	}
	
	if(useCurrentPacket){
		packet =	packets->fromServer.packets
					+
					(	packets->fromServer.count +
						ARRAY_SIZE(packets->fromServer.packets) - 
						1) %
					beaSize(&packets->fromServer.packets);
	}else{
		packet =	packets->fromServer.packets +
					packets->fromServer.count++;
					
		packets->fromServer.count %= beaSize(&packets->fromServer.packets);

		ZeroStruct(packet);
		ANALYSIS_ASSUME(packet);

		packet->flags.notMovementPacket = 1;
	}

	packet->size = size;
}

static void mmClientReceiveStatsPackets(Packet* pak){
	START_BIT_COUNT(pak, "statsPackets");
	{
		MovementClientStatsPackets*	packets = mgState.fg.mc.stats.packets;
		MovementClientStatsPacket*	packet;
		MovementClientStatsPacket	packetTemp;
		U32							msCurTime = timeGetTime();
		U32							msTimeSentDelta = pktGetBitsAuto(pak);
		U32							packetCount = pktGetBitsAuto(pak);
		
		if(!packets){
			packets = mgState.fg.mc.stats.packets = callocStruct(MovementClientStatsPackets);
			beaSetSize(&packets->fromServer.packets, 2000);
			beaSetSize(&packets->fromClient.packets, 2000);
		}

		// Store fromServer stats.

		if(!packets->msFirstReceiveLocalTime){
			packets->msFirstReceiveLocalTime = msCurTime;
			packets->msPreviousReceiveLocalTime = msCurTime;
		}else{
			packets->msAccReceiveDeltaTime += msTimeSentDelta;
		}

		if(mgState.flags.netStatsPaused){
			packet = &packetTemp;
		}else{
			packet =	packets->fromServer.packets +
						packets->fromServer.count++;
						
			packets->fromServer.count %= beaSize(&packets->fromServer.packets);
		}

		ZeroStruct(packet);
		ANALYSIS_ASSUME(packet);
		
		packet->spcOffset = mgState.fg.frame.cur.pcStart +
							mgState.fg.netReceive.cur.offset.clientToServerSync -
							mgState.fg.netReceive.cur.pc.server;
		
		mmStatsPacketUpdateReceiveTime(packets, packet, msCurTime);
		
		// Store fromClient stats from the server.

		FOR_BEGIN(i, (S32)packetCount);
		{
			if(mgState.flags.netStatsPaused){
				packet = &packetTemp;
			}else{
				packet = packets->fromClient.packets + packets->fromClient.count++;
				packets->fromClient.count %= beaSize(&packets->fromClient.packets);
			}
			
			packet->size = pktGetBitsAuto(pak);
			packet->msLocalOffsetFromLastPacket = pktGetBitsAuto(pak);
			packet->msOffsetFromExpectedTime = pktGetBitsAuto(pak);
			packet->flags.notMovementPacket = pktGetBits(pak, 1);
		}
		FOR_END;
	}
	STOP_BIT_COUNT(pak);
}

static void mmClientReceiveLogList(Packet* pak){
	EntityRef er;
	
	eaiClearFast(&mgState.debug.serverLogList);
	
	while(er = pktGetBitsAuto(pak)){
		eaiPush(&mgState.debug.serverLogList, er);
	}
}

void mmNetDestroyStatsFrames(MovementClient* mc){
	if(!mc){
		mc = &mgState.fg.mc;
	}

	if(mc->stats.frames){
		beaDestroy(&mc->stats.frames->frames);
		SAFE_FREE(mc->stats.frames);
	}
}

void mmNetDestroyStatsPackets(MovementClient* mc){
	if(!mc){
		mc = &mgState.fg.mc;
	}

	if(mc->stats.packets){
		beaDestroy(&mc->stats.packets->fromServer.packets);
		beaDestroy(&mc->stats.packets->fromClient.packets);
		SAFE_FREE(mc->stats.packets);
	}
}

void mmClientReceiveHeaderFromServer(	Packet* pak,
										U32* processCountOut)
{
	U32 firstByte;
	S32 receiveExtraFlags;
	
	#define nr mgState.fg.netReceiveMutable

	nr.prevPrev = nr.prev;
	nr.prev = nr.cur;
	
	START_BIT_COUNT(pak, "mmHeader");

	START_BIT_COUNT(pak, "firstByte");
	firstByte = pktGetBits(pak, MM_CLIENT_NET_HEADER_BIT_COUNT);
	STOP_BIT_COUNT(pak);
	
	nr.flags.fullUpdate = !!(firstByte & BIT(0));
	receiveExtraFlags = !!(firstByte & BIT(1));
	
	firstByte >>= 2;
	
	if(firstByte == BIT_RANGE(0, 5)){
		START_BIT_COUNT(pak, "processCountFull");
		nr.cur.pc.server = pktGetBits(pak, 32);
		nr.cur.pc.serverDelta = pktGetBitsAuto(pak);
		STOP_BIT_COUNT(pak);
	}else{
		nr.cur.pc.server += firstByte;
		nr.cur.pc.serverDelta = firstByte;
	}
	
	nr.cur.normalOutputCount = nr.cur.pc.serverDelta / MM_PROCESS_COUNTS_PER_STEP;
	
	if(processCountOut){
		*processCountOut = nr.cur.pc.server;
	}

	START_BIT_COUNT(pak, "syncHeader");
	mmReceiveSyncHeaderFromServer(pak);
	STOP_BIT_COUNT(pak);
	
	if(receiveExtraFlags){
		U32 extraFlags;
		S32 receiveStatsFrames;
		S32 receiveStatsPackets;
		S32 receiveLogList;
		
		START_BIT_COUNT(pak, "extraFlags");
		extraFlags = pktGetBits(pak, MM_CLIENT_NET_EXTRA_FLAGS_BIT_COUNT);
		STOP_BIT_COUNT(pak);

		receiveStatsFrames = !!(extraFlags & BIT(1));
		receiveStatsPackets = !!(extraFlags & BIT(2));
		receiveLogList = !!(extraFlags & BIT(3));

		nr.flags.receiveFullRotations = !!(extraFlags & BIT(0));
		
		if(receiveStatsFrames){
			mmClientReceiveStatsFrames(pak);
		}else{
			mmNetDestroyStatsFrames(NULL);
		}
		
		if(receiveStatsPackets){
			mmClientReceiveStatsPackets(pak);
		}else{
			mmNetDestroyStatsPackets(NULL);
		}
		
		if(receiveLogList){
			mmClientReceiveLogList(pak);
		}
	}else{
		nr.flags.receiveFullRotations = 0;

		mmNetDestroyStatsFrames(NULL);
		mmNetDestroyStatsPackets(NULL);
	}

	START_BIT_COUNT(pak, "animBitAndGeometryUpdates");
	mmReceiveAnimBitAndGeometryUpdate(pak);
	STOP_BIT_COUNT(pak);

	if(nr.flags.fullUpdate){
		mmClientDetachManagers(&mgState.fg.mc);
	}
	
	#undef nr
	
	STOP_BIT_COUNT(pak);

	mmProcessHeader();
}

static void mmWriteAnimBitToDemo(	RecordedEntityPos* recordedPos,
									const char* bitName)
{
	extern ParseTable parse_RecordedAnimBit[];
	RecordedAnimBit* b = StructCreate(parse_RecordedAnimBit);
	b->bitName = allocAddString(bitName);
	eaPush(&recordedPos->animBits, b);
}

static void mmWriteAnimBitComboToDemo(	RecordedEntityPos* recordedPos,
										const MovementRegisteredAnimBitCombo* combo)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(combo->bits, i, size);
	{
		// If the demo recorder is recording, push this to the list of recorded bits.

		MovementRegisteredAnimBit*	bit;
		
		if(mmRegisteredAnimBitGetByHandle(	&mgState.fg.netReceiveMutable.animBitRegistry,
											&bit,
											combo->bits[i]))
		{
			mmWriteAnimBitToDemo(recordedPos, bit->bitName);
		}
	}
	EARRAY_FOREACH_END;
}

void mmClientReceiveFooterFromServer(Packet* pak){
}

static void mmWritePrefixedAnimBitHandleToDemo(	RecordedEntityPos* recordedPos,
												const char* prefix,
												U32 bitHandle,
												bool hasHandleId)
{
	const MovementRegisteredAnimBit*	b;
	char								buffer[100];

	if (hasHandleId) {
		mmGetLocalAnimBitFromHandle(&b, MM_ANIM_HANDLE_WITHOUT_ID(bitHandle), 0);
	} else {
		mmGetLocalAnimBitFromHandle(&b, bitHandle, 0);
	}
	sprintf(buffer, "%s%s", prefix, b->bitName);
	mmWriteAnimBitToDemo(recordedPos, buffer);
}

static void mmWritePrefixedAnimBitHandleArrayToDemo(RecordedEntityPos* recordedPos,
													const char* prefix,
													const U32* bitHandles,
													bool hasHandleId)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(bitHandles, i, isize);
	{
		mmWritePrefixedAnimBitHandleToDemo(	recordedPos,
											prefix,
											bitHandles[i],
											hasHandleId);
	}
	EARRAY_FOREACH_END;
}

static void mmWriteAnimStateToDemo(	MovementManager* mm,
									const MovementNetOutput* no,
									RecordedEntityPos* recordedPos)
{
	S32 foundAnim = 0;

	mmWritePrefixedAnimBitHandleArrayToDemo(recordedPos,
											"Stance:",
											mm->fg.net.stanceBits,
											0);

	if(no){
		EARRAY_INT_CONST_FOREACH_BEGIN(no->data.anim.values, i, isize);
		{
			switch(MM_ANIM_VALUE_GET_TYPE(no->data.anim.values[i])){
				xcase MAVT_LASTANIM_ANIM:{
					// Skip PC.
					i++;
				}
				xcase MAVT_ANIM_TO_START:{
					ASSERT_FALSE_AND_SET(foundAnim);

					mmWritePrefixedAnimBitHandleToDemo(	recordedPos,
														"Anim:",
														MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]),
														1);
				}
				xcase MAVT_FLAG:{
					if(!foundAnim){
						break;
					}

					mmWritePrefixedAnimBitHandleToDemo(	recordedPos,
														"Flag:",
														MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]),
														1);
				}
				xcase MAVT_DETAIL_ANIM_TO_START:{
					mmWritePrefixedAnimBitHandleToDemo(	recordedPos,
														"Detail:",
														MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]),
														1);
				}
				xcase MAVT_DETAIL_FLAG:{
					mmWritePrefixedAnimBitHandleToDemo(	recordedPos,
														"DetailFlag:",
														MM_ANIM_VALUE_GET_INDEX(no->data.anim.values[i]),
														1);
				}
			}
		}
		EARRAY_FOREACH_END;
	}

	if(!foundAnim){
		mmWritePrefixedAnimBitHandleToDemo(	recordedPos,
											"LastAnim:",
											mm->fg.net.lastAnim.anim,
											1);

		mmWritePrefixedAnimBitHandleArrayToDemo(recordedPos,
												"Flag:",
												mm->fg.net.lastAnim.flags,
												1);
	}
}

static void mmReceiveAnimUpdate(MovementManager* mm,
								MovementNetOutput* no,
								const U32 pcOffset,
								Packet* pak,
								const MovementNetUpdateHeader* header,
								RecordedEntityPos* recordedPos)
{
	if(gConf.bNewAnimationSystem){
		S32 diffAgainstLastAnimUpdate = 0;
		S32 diffLastAnim = 0;
		S32 hasAnimToStart = 0;
		S32 hasFlags = 0;

		if(	no &&
			TRUE_THEN_RESET(mm->fg.net.flags.lastAnimUpdateWasNotStored))
		{
			no->flagsMutable.diffedLastStances = 1;
			diffAgainstLastAnimUpdate = 1;

			// Check if lastAnim changed.

			if(	mm->fg.net.lastAnim.pc != mm->fg.net.lastStored.lastAnim.pc ||
				mm->fg.net.lastAnim.anim != mm->fg.net.lastStored.lastAnim.anim ||
				eaiSize(&mm->fg.net.lastAnim.flags) < eaiSize(&mm->fg.net.lastStored.lastAnim.flags) ||
				CompareStructs(	mm->fg.net.lastAnim.flags,
								mm->fg.net.lastStored.lastAnim.flags,
								eaiSize(&mm->fg.net.lastStored.lastAnim.flags)))
			{
				// Too different, start fresh.

				mmLastAnimCopyToValues(	&no->dataMutable.anim,
										&mm->fg.net.lastStored.lastAnimMutable);

				diffLastAnim = 1;
			}
		}

		if(!header->flags.hasAnimUpdate){
			MM_CHECK_STRING_READ(pak, "no anim");
		}else{
			MM_CHECK_STRING_READ(pak, "anim");

			if(	!no &&
				FALSE_THEN_SET(mm->fg.net.flags.lastAnimUpdateWasNotStored))
			{
				eaiCopy(&mm->fg.net.lastStored.stanceBitsMutable,
						&mm->fg.net.stanceBits);

				mmLastAnimCopy(	&mm->fg.net.lastStored.lastAnimMutable,
								&mm->fg.net.lastAnim);
			}

			while(1){
				U32						readU32 = pktGetBitsAuto(pak);
				U32						indexAndType = readU32 >> 1;
				MovementAnimValueType	mavType = MM_ANIM_VALUE_GET_TYPE(indexAndType);
				U32						index = MM_ANIM_VALUE_GET_INDEX(indexAndType);

				if(!readU32){
					break;
				}

				if (mavType == MAVT_STANCE_OFF	||
					mavType == MAVT_STANCE_ON) {
					mmTranslateAnimBitServerToClient(&index,0);
				} else {
					mmTranslateAnimBitServerToClient(&index,1);
				}

				switch(mavType){
					xcase MAVT_STANCE_OFF:{
						if(	no &&
							!diffAgainstLastAnimUpdate)
						{
							eaiPush(&no->dataMutable.anim.values,
									MM_ANIM_VALUE(index, mavType));
						}

						if(eaiFindAndRemove(&mm->fg.net.stanceBitsMutable, index) < 0){
							mmHandleBadAnimData(mm);
						}
					}
					xcase MAVT_STANCE_ON:{
						if(	no &&
							!diffAgainstLastAnimUpdate)
						{
							eaiPush(&no->dataMutable.anim.values,
									MM_ANIM_VALUE(index, mavType));
						}

						if(eaiFind(&mm->fg.net.stanceBits, index) >= 0){
							mmHandleBadAnimData(mm);
						}
					
						eaiPush(&mm->fg.net.stanceBitsMutable, index);
					}
					xcase MAVT_ANIM_TO_START:{
						assert(!hasFlags);
						ASSERT_FALSE_AND_SET(hasAnimToStart);

						if(no){
							mmLastAnimCopyToValues(	&no->dataMutable.anim,
													&mm->fg.net.lastAnim);

							eaiPush(&no->dataMutable.anim.values,
									MM_ANIM_VALUE(index, mavType));
						}
				
						if(mm->fg.flags.isAttachedToClient){
							mm->fg.net.lastAnimMutable.pc =	mgState.fg.netReceive.cur.pc.client -
															pcOffset;
						}else{
							mm->fg.net.lastAnimMutable.pc =	mgState.fg.netReceive.cur.pc.server -
															pcOffset;
						}
						mm->fg.net.lastAnimMutable.anim = index;
						eaiClearFast(&mm->fg.net.lastAnimMutable.flags);
					}
					xcase MAVT_FLAG:{
						if(	!mm->fg.net.lastAnim.anim ||
							mm->fg.net.lastAnim.anim == mgState.animBitHandle.animOwnershipReleased)
						{
							mmHandleBadAnimData(mm);
						}

						if(	FALSE_THEN_SET(hasFlags) &&
							diffLastAnim &&
							!hasAnimToStart)
						{
							// Make the current lastAnim start here.

							eaiPush(&no->dataMutable.anim.values,
									MM_ANIM_VALUE(mm->fg.net.lastAnim.anim, MAVT_ANIM_TO_START));

							EARRAY_INT_CONST_FOREACH_BEGIN(mm->fg.net.lastAnim.flags, i, isize);
							{
								eaiPush(&no->dataMutable.anim.values,
										MM_ANIM_VALUE(mm->fg.net.lastAnim.flags[i], MAVT_FLAG));
							}
							EARRAY_FOREACH_END;
						}

						if(no){
							eaiPush(&no->dataMutable.anim.values,
									MM_ANIM_VALUE(index, mavType));
						}

						eaiPush(&mm->fg.net.lastAnimMutable.flags,
								index);
					}
					xcase MAVT_DETAIL_ANIM_TO_START:{
						if(no){
							eaiPush(&no->dataMutable.anim.values,
									MM_ANIM_VALUE(index, mavType));
						}
					}
					xcase MAVT_DETAIL_FLAG:{
						if(no){
							eaiPush(&no->dataMutable.anim.values,
									MM_ANIM_VALUE(index, mavType));
						}
					}
					xdefault:{
						mmHandleBadAnimData(mm);
					}
				}

				if(!(readU32 & 1)){
					break;
				}
			}
		}

		if(	diffAgainstLastAnimUpdate &&
			!hasFlags &&
			!hasAnimToStart &&
			mm->fg.net.lastAnim.anim)
		{
			if(diffLastAnim){
				eaiPush(&no->dataMutable.anim.values,
						MM_ANIM_VALUE(mm->fg.net.lastAnim.anim, MAVT_ANIM_TO_START));

				EARRAY_INT_CONST_FOREACH_BEGIN(mm->fg.net.lastAnim.flags, i, isize);
				{
					eaiPush(&no->dataMutable.anim.values,
							MM_ANIM_VALUE(mm->fg.net.lastAnim.flags[i], MAVT_FLAG));
				}
				EARRAY_FOREACH_END;
			}else{
				// Just push the new flags.

				EARRAY_INT_CONST_FOREACH_BEGIN_FROM(mm->fg.net.lastAnim.flags,
													i,
													isize,
													eaiSize(&mm->fg.net.lastStored.lastAnim.flags));
				{
					eaiPush(&no->dataMutable.anim.values,
							MM_ANIM_VALUE(mm->fg.net.lastAnim.flags[i], MAVT_FLAG));
				}
				EARRAY_FOREACH_END;
			}
		}

		if(recordedPos){
			mmWriteAnimStateToDemo(mm, no, recordedPos);
		}

		// Check if this is a diff.
		
		if(diffAgainstLastAnimUpdate){
			EARRAY_INT_CONST_FOREACH_BEGIN(mm->fg.net.stanceBits, i, isize);
			{
				if(eaiFind(&mm->fg.net.lastStored.stanceBits, mm->fg.net.stanceBits[i]) < 0){
					eaiPush(&no->dataMutable.anim.values,
							MM_ANIM_VALUE(mm->fg.net.stanceBits[i], MAVT_STANCE_ON));
				}
			}
			EARRAY_FOREACH_END;
			
			EARRAY_INT_CONST_FOREACH_BEGIN(mm->fg.net.lastStored.stanceBits, i, isize);
			{
				if(eaiFind(&mm->fg.net.stanceBits, mm->fg.net.lastStored.stanceBits[i]) < 0){
					eaiPush(&no->dataMutable.anim.values,
							MM_ANIM_VALUE(mm->fg.net.lastStored.stanceBits[i], MAVT_STANCE_OFF));
				}
			}
			EARRAY_FOREACH_END;
			
			eaiDestroy(&mm->fg.net.lastStored.stanceBitsMutable);
		}
		
		#if MM_VERIFY_RECEIVED_ANIM
		{
			if(no){
				U32*						stances = NULL;
				const MovementNetOutput*	noCur;
			
				eaiStackCreate(&stances, MM_STANCE_STACK_SIZE_LARGE);
				mmCopyAnimValueToSizedStack(&stances,
											mm->fg.net.stanceBits,
											__FUNCTION__);
				
				for(noCur = mm->fg.net.outputList.tail;
					noCur;
					noCur = noCur->prev)
				{
					mmAnimValuesApplyStanceDiff(mm,
												&noCur->data.anim,
												1,
												&stances,
												__FUNCTION__, 1);
				}
				
				eaiDestroy(&stances);
			}
		}
		#endif
	}else{
		const MovementRegisteredAnimBitCombo* combo = NULL;
		
		if(header->flags.hasAnimUpdate){
			U32 comboIndex;
			
			MM_CHECK_STRING_READ(pak, "animbits");

			START_BIT_COUNT(pak, "anim bits");
				comboIndex = pktGetBitsAuto(pak);
			STOP_BIT_COUNT(pak);

			if(comboIndex >= eaUSize(&mgState.fg.netReceive.animBitRegistry.allCombos)){
				assertmsgf(	0,
							"Invalid anim bit combo %d (max %d)",
							comboIndex,
							eaSize(&mgState.fg.netReceive.animBitRegistry.allCombos) - 1);
			}else{
				combo = mgState.fg.netReceive.animBitRegistry.allCombos[comboIndex];
				
				mm->fg.net.cur.animBits.combo = combo;
			}
		}else{
			MM_CHECK_STRING_READ(pak, "no animbits");
			
			combo = mm->fg.net.cur.animBits.combo;
		}

		if(combo){
			if(no){
				no->animBitCombo = combo;
			}

			if(recordedPos){
				mmWriteAnimBitComboToDemo(recordedPos, combo);
			}
		}
	}
}

static void mmReceivePosUpdate(	MovementManager* mm,
								Packet* pak,
								const MovementNetUpdateHeader* header,
								S32* notInterpedOut)
{
	U32		encOffsetLen;
	IMat3	encMat;
	U32		noHeader;

	if(header->flags.hasNotInterpedOutput){
		START_BIT_COUNT(pak, "notInterped");

		if(pktGetBits(pak, 1)){
			*notInterpedOut = 1;
		}

		STOP_BIT_COUNT(pak);
	}

	mmMakeBasisFromOffset(	mm,
							mm->fg.net.cur.encoded.posOffset,
							encMat,
							&encOffsetLen,
							NULL);
							
	copyVec3(	mm->fg.net.cur.encoded.pos,
				mm->fg.net.prev.encoded.pos);
							
	nprintf("prevOffset: %d, %d, %d\n",
			vecParamsXYZ(mm->fg.net.cur.encoded.posOffset));
			
	START_BIT_COUNT(pak, "pos");
	
	START_BIT_COUNT(pak, "header");
		noHeader = pktGetBits(pak, MM_NET_OUTPUT_HEADER_BIT_COUNT);
	STOP_BIT_COUNT(pak);
			
	if(!(noHeader & BIT(0))){
		U32 xyzMask = (noHeader >> 1) & BIT_RANGE(0, 2);
		
		MM_CHECK_STRING_READ(pak, "partial");
		
		START_BIT_COUNT(pak, "partial");
		
		FOR_BEGIN(i, 3);
			nprintf("mat[%d]: %d, %d, %d\n",
					i,
					vecParamsXYZ(encMat[i]));
		FOR_END;
		
		if(xyzMask){
			switch(xyzMask){
				xcase 1:	START_BIT_COUNT(pak, "x");
				xcase 2:	START_BIT_COUNT(pak, "y");
				xcase 1|2:	START_BIT_COUNT(pak, "xy");
				xcase 4:	START_BIT_COUNT(pak, "z");
				xcase 1|4:	START_BIT_COUNT(pak, "xz");
				xcase 2|4:	START_BIT_COUNT(pak, "yz");
				xcase 1|2|4:START_BIT_COUNT(pak, "xyz");
			}
			
			FOR_BEGIN(i, 3);
			{
				if(xyzMask & BIT(i)){
					U32 firstByte;
					U32 byteCount;
					
					switch(i){
						xcase 0:START_BIT_COUNT(pak, "x");
						xcase 1:START_BIT_COUNT(pak, "y");
						xcase 2:START_BIT_COUNT(pak, "z");
					}
					
					START_BIT_COUNT(pak, "firstByte");
						firstByte = pktGetBits(pak, 8);
					STOP_BIT_COUNT(pak);
					
					byteCount = firstByte & 3;
					
					if(byteCount){
						S32		isNegative = !!(firstByte & 4);
						S32		signScale = isNegative ? -1 : 1;
						S32		offsetDeltaScale = firstByte >> 3;
						IVec3	curOffsetDelta;
						
						assert(byteCount != 3);
						
						if(byteCount == 1){
							nprintf("offsetDeltaScale[%d,1] = %d\n",
									i,
									offsetDeltaScale);
									
							switch(offsetDeltaScale + 1){
								#define CASE(x) xcase x:if(isNegative){ADD_MISC_COUNT(1000*1000, "-"#x);}else{ADD_MISC_COUNT(1000*1000, #x);}
								#define CASE4(a,b,c,d) CASE(a);CASE(b);CASE(c);CASE(d)
								#define CASE8(a,b,c,d,e,f,g,h) CASE4(a,b,c,d);CASE4(e,f,g,h)
								CASE8(1, 2, 3, 4, 5, 6, 7, 8);
								CASE8(9, 10, 11, 12, 13, 14, 15, 16);
								CASE8(17, 18, 19, 20, 21, 22, 23, 24);
								CASE8(25, 26, 27, 28, 29, 30, 31, 32);
								#undef CASE8
								#undef CASE4
								#undef CASE
							}

							#if MM_NET_USE_LINEAR_ENCODING
							{
								if(isNegative){
									offsetDeltaScale *= -1;
								}

								FOR_BEGIN(j, 3);
								{
									curOffsetDelta[j] = encMat[i][j] * offsetDeltaScale;
									
									switch(byteCount){
										curOffsetDelta[j] /= MM_NET_SMALL_CHANGE_VALUE_COUNT;
									}
								}
								FOR_END;
							}
							#else
							{
								if(offsetDeltaScale < MM_NET_SMALL_CHANGE_SHIFT_BITS){
									const U32 shift =	MM_NET_SMALL_CHANGE_MAX_SHIFT -
														offsetDeltaScale;

									ARRAY_FOREACH_BEGIN(curOffsetDelta, j);
									{
										curOffsetDelta[j] = signScale * (encMat[i][j] >> shift);
									}
									ARRAY_FOREACH_END;
								}else{
									S32 scale = offsetDeltaScale +
												1 -
												MM_NET_SMALL_CHANGE_SHIFT_BITS;
									
									if(isNegative){
										scale *= -1;
									}

									ARRAY_FOREACH_BEGIN(curOffsetDelta, j);
									{
										const U32 shift =	MM_NET_SMALL_CHANGE_MAX_SHIFT -
															MM_NET_SMALL_CHANGE_SHIFT_BITS;
													
										curOffsetDelta[j] = encMat[i][j] * scale;
										curOffsetDelta[j] /= MM_NET_SMALL_CHANGE_LINEAR_VALUE_COUNT;
										curOffsetDelta[j] += signScale * (encMat[i][j] >> shift);
									}
									ARRAY_FOREACH_END;
								}
							}
							#endif
						}else{
							START_BIT_COUNT(pak, "secondByte");
								offsetDeltaScale |= pktGetBits(pak, 8) << 5;
							STOP_BIT_COUNT(pak);

							nprintf("offsetDeltaScale[%d,2] = %d\n",
									i,
									offsetDeltaScale);

							if(isNegative){
								offsetDeltaScale *= -1;
							}

							FOR_BEGIN(j, 3);
							{
								curOffsetDelta[j] = encMat[i][j] * offsetDeltaScale;
								curOffsetDelta[j] *= 99;
								curOffsetDelta[j] /= (S32)BIT(13) - 1;
								curOffsetDelta[j] += signScale * encMat[i][j];
							}
							FOR_END;
						}
						
						addVec3(curOffsetDelta,
								mm->fg.net.cur.encoded.posOffset,
								mm->fg.net.cur.encoded.posOffset);

						nprintf("curOffsetDelta[%d]: %d, %d, %d\n",
								i,
								vecParamsXYZ(curOffsetDelta));
					}
					
					STOP_BIT_COUNT(pak);
				}
			}
			FOR_END;
			
			STOP_BIT_COUNT(pak);
			
			nprintf("\n");
		}
		
		addVec3(mm->fg.net.cur.encoded.posOffset,
				mm->fg.net.cur.encoded.pos,
				mm->fg.net.cur.encoded.pos);

		STOP_BIT_COUNT(pak);
	}else{
		START_BIT_COUNT(pak, "absolute");

		if(noHeader & BIT(1)){
			U32 xyzMask = (noHeader >> 2) & BIT_RANGE(0, 2);

			MM_CHECK_STRING_READ(pak, "small");
			
			if(xyzMask){
				START_BIT_COUNT(pak, "small");
				
				FOR_BEGIN(i, 3);
				{
					if(xyzMask & BIT(i)){
						U32 update;
						S32 encMag;
						S32 decMag;
						S32 isNegative;
						
						switch(i){
							xcase 0:START_BIT_COUNT(pak, "x");
							xcase 1:START_BIT_COUNT(pak, "y");
							xcase 2:START_BIT_COUNT(pak, "z");
						}
						
						update = pktGetBits(pak, 8);
						
						STOP_BIT_COUNT(pak);
						
						encMag = update >> 1;
						isNegative = update & 1;
						
						nprintf("encMag[%d]: %s%d\n",
								i,
								isNegative ? "-" : "+",
								encMag);

						decMag = mmConvertS32ToS24_8(5 * encMag) / 127;
						
						if(isNegative){
							decMag *= -1;
						}

						mm->fg.net.cur.encoded.posOffset[i] = decMag;
					}else{
						mm->fg.net.cur.encoded.posOffset[i] = 0;
					}
				}
				FOR_END;
				
				STOP_BIT_COUNT(pak);
				
				addVec3(mm->fg.net.cur.encoded.posOffset,
						mm->fg.net.cur.encoded.pos,
						mm->fg.net.cur.encoded.pos);
			}else{
				zeroVec3(mm->fg.net.cur.encoded.posOffset);
			}
			
			nprintf("absolute small: %d, %d, %d\n",
					vecParamsXYZ(mm->fg.net.cur.encoded.posOffset));
		}else{
			MM_CHECK_STRING_READ(pak, "full");
			
			START_BIT_COUNT(pak, "full");
				FOR_BEGIN(j, 3);
					mm->fg.net.cur.encoded.pos[j] = pktGetBits(pak, 32);
				FOR_END;
			STOP_BIT_COUNT(pak);
			
			nprintf("absolute full: %d, %d, %d\n",
					vecParamsXYZ(mm->fg.net.cur.encoded.pos));
			
			zeroVec3(mm->fg.net.cur.encoded.posOffset);
		}

		STOP_BIT_COUNT(pak);
	}

	STOP_BIT_COUNT(pak);

	if(!sameVec3(	mm->fg.net.prev.encoded.pos,
					mm->fg.net.cur.encoded.pos))
	{	
		mmConvertIVec3ToVec3(	mm->fg.net.cur.encoded.pos,
								mm->fg.net.cur.decoded.pos);
	}

	nprintf("newPos: %f, %f, %f\n",
			vecParamsXYZ(mm->fg.net.cur.decoded.pos));

	nprintf("newEncPos: %d, %d, %d\n",
			vecParamsXYZ(mm->fg.net.cur.encoded.pos));
					
	nprintf("newEncOffset: %d, %d, %d\n",
			vecParamsXYZ(mm->fg.net.cur.encoded.posOffset));
}

static void mmReceiveRotAndFaceUpdate(	MovementManager* mm,
										Packet* pak)
{
	U32 rotAndFaceMask;

	START_BIT_COUNT(pak, "rotAndFaceMask");
		rotAndFaceMask = pktGetBits(pak, 3 + 2);
	STOP_BIT_COUNT(pak);
	
	{
		U32		pyrMask = rotAndFaceMask & BIT_RANGE(0, 2);
		Vec3	pyr;
		Quat	rotOrig;

		if(mgState.fg.netReceive.flags.receiveFullRotations){
			pktGetQuat(pak, rotOrig);
		}

		if(pyrMask){
			START_BIT_COUNT(pak, "rot");
			
			FOR_BEGIN(j, 3);
				if(pyrMask & BIT(j)){
					S32 encMag;
					S32 isNegative;

					START_BIT_COUNT(pak, "encMag");
						encMag = pktGetBits(pak, MM_NET_ROTATION_ENCODED_BIT_COUNT + 1);
					STOP_BIT_COUNT(pak);

					isNegative = encMag & 1;

					encMag >>= 1;

					pyr[j] =	(isNegative ? -1 : 1) *
								PI *
								(F32)encMag /
								(F32)MM_NET_ROTATION_ENCODED_MAX;

					mm->fg.net.cur.decoded.pyr[j] = pyr[j];
					
					//printf("pyr = %f\n", pyr[j]);
				}else{
					pyr[j] = mm->fg.net.cur.decoded.pyr[j];
				}
			FOR_END;
			
			PYRToQuat(	pyr,
						mm->fg.net.cur.decoded.rot);
			
			STOP_BIT_COUNT(pak);
		}

		if(mgState.fg.netReceive.flags.receiveFullRotations){
			copyQuat(	rotOrig,
						mm->fg.net.cur.decoded.rot);
		}
	}

	{
		U32		pyMask = (rotAndFaceMask >> 3) & BIT_RANGE(0, 1);
		Vec2	pyFace;

		if(pyMask){
			START_BIT_COUNT(pak, "face");
			
			FOR_BEGIN(j, 2);
				if(pyMask & BIT(j)){
					S32 encMag;
					S32 isNegative;
					
					START_BIT_COUNT(pak, "encMag");
						encMag = pktGetBits(pak, MM_NET_ROTATION_ENCODED_BIT_COUNT + 1);
					STOP_BIT_COUNT(pak);
					
					isNegative = encMag & 1;

					encMag >>= 1;
					
					pyFace[j] =	(isNegative ? -1 : 1) *
								PI *
								(F32)encMag /
								(F32)MM_NET_ROTATION_ENCODED_MAX;

					mm->fg.net.cur.encoded.pyFace[j] = encMag * (isNegative ? -1 : 1);
					
					mm->fg.net.cur.decoded.pyFace[j] = pyFace[j];
					
					mmLog(	mm,
							NULL,
							"[net.receive] Received pyFace[%d]: %1.3f [%8.8x] as %d",
							j,
							pyFace[j],
							*(S32*)&pyFace[j],
							mm->fg.net.cur.encoded.pyFace[j]);

					//printf("pyr = %f\n", pyr[j]);
				}else{
					pyFace[j] = mm->fg.net.cur.decoded.pyFace[j];
				}
			FOR_END;
			
			STOP_BIT_COUNT(pak);
		}

		mmLog(	mm,
				NULL,
				"[net.receive] Received pyFace: (%1.3f, %1.3f) [%8.8x, %8.8x] ",
				mm->fg.net.cur.decoded.pyFace[0],
				mm->fg.net.cur.decoded.pyFace[1],
				*(S32*)&mm->fg.net.cur.decoded.pyFace[0],
				*(S32*)&mm->fg.net.cur.decoded.pyFace[1]);
	}
}

static void mmCreateFakeNetOutput(	MovementManager* mm,
									MovementThreadData* td,
									S32 isLocalManager)
{
	MovementNetOutput* no;
	
	if(MMLOG_IS_ENABLED(mm)){
		if(isLocalManager){
			mmLog(	mm,
					NULL,
					"[net.output] Creating fake net output from sync pos"
					" (%1.2f, %1.2f, %1.2f).",
					vecParamsXYZ(mm->fg.net.sync.pos));
		}else{
			mmLog(	mm,
					NULL,
					"[net.output] Creating fake net output from decoded init pos"
					" (%1.2f, %1.2f, %1.2f).",
					vecParamsXYZ(mm->fg.net.cur.decoded.pos));
		}
	}

	mmNetOutputCreateAndAddTail(mm, td, &no);

	// Set the PCs.

	no->pc.server = mgState.fg.netReceive.cur.pc.server - 2;
									
	no->pc.client = mgState.fg.netReceive.cur.pc.client - 2;
									
	if(isLocalManager){
		MM_CHECK_DYNPOS_DEVONLY(mm->fg.net.sync.pos);

		copyVec3(	mm->fg.net.sync.pos,
					no->dataMutable.pos);
					
		copyQuat(	mm->fg.net.sync.rot,
					no->dataMutable.rot);
	}else{
		MM_CHECK_DYNPOS_DEVONLY(mm->fg.net.cur.decoded.pos);

		copyVec3(	mm->fg.net.cur.decoded.pos,
					no->dataMutable.pos);
					
		copyQuat(	mm->fg.net.cur.decoded.rot,
					no->dataMutable.rot);
	}
	
	no->animBitCombo = mm->fg.net.cur.animBits.combo;
}

static void mmReceiveNetOutputInitializer(	MovementManager* mm,
											MovementThreadData* td,
											Packet* pak)
{
	MM_CHECK_STRING_READ(pak, "prev");
	
	// Receive the pos initializer.
	
	pktGetIVec3(pak,
				mm->fg.net.cur.encoded.pos);
				
	mmConvertIVec3ToVec3(	mm->fg.net.cur.encoded.pos,
							mm->fg.net.cur.decoded.pos);

	MM_CHECK_DYNPOS_DEVONLY(mm->fg.net.cur.decoded.pos);

	copyVec3(	mm->fg.net.cur.decoded.pos,
				mm->fg.posMutable);

	pktGetIVec3(pak,
				mm->fg.net.cur.encoded.posOffset);
	
	nprintf("full prev: %d, %d, %d\n",
			vecParamsXYZ(mm->fg.net.cur.encoded.pos));

	// Receive the rot initializer.

	pktGetIVec3(pak,
				mm->fg.net.cur.encoded.pyr);
				
	ARRAY_FOREACH_BEGIN(mm->fg.net.cur.encoded.pyr, i);
	{
		mm->fg.net.cur.decoded.pyr[i] = PI *
										(F32)mm->fg.net.cur.encoded.pyr[i] /
										(F32)MM_NET_ROTATION_ENCODED_MAX;
	}
	ARRAY_FOREACH_END;
				
	PYRToQuat(	mm->fg.net.cur.decoded.pyr,
				mm->fg.net.cur.decoded.rot);

	copyQuat(	mm->fg.net.cur.decoded.rot,
				mm->fg.rotMutable);

	// Receive the face initializer.

	pktGetIVec2(pak,
				mm->fg.net.cur.encoded.pyFace);

	ARRAY_FOREACH_BEGIN(mm->fg.net.cur.encoded.pyFace, i);
	{
		mm->fg.net.cur.decoded.pyFace[i] =	PI *
											(F32)mm->fg.net.cur.encoded.pyFace[i] /
											(F32)MM_NET_ROTATION_ENCODED_MAX;
	}
	ARRAY_FOREACH_END;

	copyVec2(	mm->fg.net.cur.decoded.pyFace,
				mm->fg.pyFaceMutable);
	
	// Receive the anim initializer.
	
	if(gConf.bNewAnimationSystem){
		S32 count = pktGetBitsAuto(pak);
		
		FOR_BEGIN(i, count);
		{
			eaiPush(&mm->fg.net.stanceBitsMutable,
					pktGetBitsAuto(pak));
		}
		FOR_END;

		mmTranslateAnimBitsServerToClient(mm->fg.net.stanceBitsMutable,0);
		
		mm->fg.net.lastAnimMutable.anim = pktGetBitsAuto(pak);

		mmGetLocalAnimBitHandleFromServerHandle(mm->fg.net.lastAnim.anim,
												&mm->fg.net.lastAnimMutable.anim,
												1);
		
		if(mm->fg.net.lastAnim.anim){
			S32 flagCount;

			mm->fg.net.lastAnimMutable.pc = pktGetBitsAuto(pak);
			flagCount = pktGetBitsAuto(pak);
			
			FOR_BEGIN(i, flagCount);
			{
				eaiPush(&mm->fg.net.lastAnimMutable.flags,
						pktGetBitsAuto(pak));
			}
			FOR_END;

			mmTranslateAnimBitsServerToClient(mm->fg.net.lastAnimMutable.flags,1);
		}
	}else{
		if(pktGetBits(pak, 1)){
			U32 comboIndex = pktGetBitsAuto(pak);

			if(comboIndex >= eaUSize(&mgState.fg.netReceive.animBitRegistry.allCombos)){
				assertmsgf(	0,
							"Invalid anim bit combo %d (max %d)",
							comboIndex,
							eaSize(&mgState.fg.netReceive.animBitRegistry.allCombos) - 1);
			}else{
				mm->fg.net.cur.animBits.combo = mgState.fg.netReceive.animBitRegistry.allCombos[comboIndex];
			}
		}
	}
	
	mmLog(	mm,
			NULL,
			"[net.receive] Received pyFace init: (%1.3f, %1.3f) [%8.8x, %8.8x] from (%d, %d)",
			mm->fg.net.cur.decoded.pyFace[0],
			mm->fg.net.cur.decoded.pyFace[1],
			*(S32*)&mm->fg.net.cur.decoded.pyFace[0],
			*(S32*)&mm->fg.net.cur.decoded.pyFace[1],
			mm->fg.net.cur.encoded.pyFace[0],
			mm->fg.net.cur.encoded.pyFace[1]);
}											

static void mmReceiveNetOutput(	MovementManager* mm,
								Packet* pak,
								const MovementNetUpdateHeader* header,
								MovementNetOutput* no,
								const U32 pcOffset,
								RecordedEntityUpdate* recUpdate)
{
	RecordedEntityPos* recPos = NULL;

	if(!header->flags.hasPosUpdate){
		MM_CHECK_STRING_READ(pak, "no pos");
		
		if(!vec3IsZero(mm->fg.net.cur.encoded.posOffset)){
			addVec3(mm->fg.net.cur.encoded.posOffset,
					mm->fg.net.cur.encoded.pos,
					mm->fg.net.cur.encoded.pos);

			mmConvertIVec3ToVec3(	mm->fg.net.cur.encoded.pos,
									mm->fg.net.cur.decoded.pos);
		}
	}else{
		S32 notInterped = 0;
		
		MM_CHECK_STRING_READ(pak, "pos");

		mmReceivePosUpdate(	mm,
							pak,
							header,
							&notInterped);
		
		if(	notInterped &&
			no)
		{
			no->flagsMutable.notInterped = 1;
		}
	}

	if(!header->flags.hasRotFaceUpdate){
		MM_CHECK_STRING_READ(pak, "no rotAndFace");
	}else{
		MM_CHECK_STRING_READ(pak, "rotAndFace");

		mmReceiveRotAndFaceUpdate(mm, pak);
	}
	
	if(no){
		MM_CHECK_DYNPOS_DEVONLY(mm->fg.net.cur.decoded.pos);

		copyVec3(	mm->fg.net.cur.decoded.pos,
					no->dataMutable.pos);
					
		copyQuat(	mm->fg.net.cur.decoded.rot,
					no->dataMutable.rot);
					
		copyVec2(	mm->fg.net.cur.decoded.pyFace,
					no->dataMutable.pyFace);
	}
	
	if(recUpdate){
		extern ParseTable parse_RecordedEntityPos[];
		
		// If we're recording, add a new position to the list of positions
		recPos = StructCreate(parse_RecordedEntityPos);
		copyVec3(mm->fg.net.cur.decoded.pos, recPos->pos);
		copyQuat(mm->fg.net.cur.decoded.rot, recPos->rot);
		copyVec2(mm->fg.net.cur.decoded.pyFace, recPos->pyFace);
		eaPush(&recUpdate->positions, recPos);
	}

	// Get anim bits.

	mmReceiveAnimUpdate(mm, no, pcOffset, pak, header, recPos);
	
	// Receive debug verification data.

	#if MM_NET_VERIFY_DECODED
	{
		IVec3	testPosEncoded;
		IVec3	testOffsetEncoded;
		Vec3	testPos;
		Vec3	testOffset;
		
		START_BIT_COUNT(pak, "verifyPos");
			pktGetIVec3(pak, testPosEncoded);
			pktGetIVec3(pak, testOffsetEncoded);
		STOP_BIT_COUNT(pak);
		
		if(	!sameVec3(testPosEncoded, mm->fg.net.cur.encoded.pos) ||
			!sameVec3(testOffsetEncoded, mm->fg.net.cur.encoded.posOffset))
		{
			mmConvertIVec3ToVec3(testPosEncoded, testPos);
			mmConvertIVec3ToVec3(testOffsetEncoded, testOffset);
			
			globCmdParsef(	"mmAddDebugSegmentOffset %f %f %f 0 100 0 0xff00ff00",
							vecParamsXYZ(testPos));
							
			mmConvertIVec3ToVec3(mm->fg.net.cur.encoded.pos, testPos);
			mmConvertIVec3ToVec3(mm->fg.net.cur.encoded.posOffset, testOffset);
			
			globCmdParsef(	"mmAddDebugSegmentOffset %f %f %f 0 100 0 0xffff0000",
							vecParamsXYZ(testPos));
							
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"Bad position decode at (%d/%d) %f, %f, %f!\n",
						mgState.fg.netReceive.cur.pc.client - pcOffset,
						mgState.fg.netReceive.cur.pc.server - pcOffset,
						vecParamsXYZ(testPos));
		}			
		//assert(sameVec3(testPos, mm->fg.net.cur.encoded.pos));
		//assert(sameVec3(testPosOffset, mm->fg.net.cur.encoded.posOffset));
	}
	#endif
}

static void mmReceiveNetOutputs(MovementManager* mm,
								MovementThreadData* td,
								Packet* pak,
								S32 fullUpdate,
								S32 isLocalManager,
								const MovementNetUpdateHeader* header,
								RecordedEntityUpdate* recUpdate)
{
	S32 outputCount;
	
	MM_CHECK_STRING_READ(pak, "outputs");
	
	if(header->flags.getOutputCount){
		START_BIT_COUNT(pak, "outputCount");
			outputCount = pktGetBitsPack(pak, 1);
		STOP_BIT_COUNT(pak);

		#if MM_CHECK_STRING_ENABLED
		{
			FOR_BEGIN(i, outputCount);
				MM_CHECK_STRING_READ(pak, "explicitOutput");
			FOR_END;
		}
		#endif
	}else{
		outputCount = mgState.fg.netReceive.cur.normalOutputCount;

		#if MM_CHECK_STRING_ENABLED
		{
			FOR_BEGIN(i, outputCount);
				MM_CHECK_STRING_READ(pak, "normalOutput");
			FOR_END;
		}
		#endif
	}
	
	MM_CHECK_STRING_READ(pak, "afterOutputCount");
	
	if(fullUpdate){
		MovementNetOutput* 	noHead = NULL;
		mmReceiveNetOutputInitializer(mm, td, pak);

		mmNetOutputCreateAndAddTail(mm, td, &noHead);

		MM_CHECK_DYNPOS_DEVONLY(mm->fg.net.cur.decoded.pos);

		copyVec3(	mm->fg.net.cur.decoded.pos,
					noHead->dataMutable.pos);
		copyVec4(	mm->fg.net.cur.decoded.rot,
					noHead->dataMutable.rot);
		copyVec2(	mm->fg.net.cur.decoded.pyFace,
					noHead->dataMutable.pyFace);
		noHead->flagsMutable.notInterped = 1;
		
		noHead->pc.server = mgState.fg.netReceive.cur.pc.server - (outputCount * MM_PROCESS_COUNTS_PER_STEP);
	}

	FOR_BEGIN(i, outputCount);
	{
		MovementNetOutput* 	no = NULL;
		const U32			pcOffset = (outputCount - i - 1) * MM_PROCESS_COUNTS_PER_STEP;

		if(	!mm->fg.net.outputList.head
			||
			subS32(	mm->fg.net.outputList.tail->pc.server,
					mm->fg.net.outputList.head->pc.server) <
				4 * MM_PROCESS_COUNTS_PER_SECOND)
		{
			mmNetOutputCreateAndAddTail(mm, td, &no);
			
			mmNetOutputListSetTail(	&mm->fg.net.outputListMutable,
									no);

			if(FALSE_THEN_SET(mm->fg.flagsMutable.needsNetOutputViewUpdate)){
				mmHandleAfterSimWakesIncFG(mm, "needsNetOutputViewUpdate", __FUNCTION__);
			}

			// Set the PCs.

			no->pc.server =	mgState.fg.netReceive.cur.pc.server -
							pcOffset;

			nprintf("\nReceiving output with SPC: %d\n", no->pc.server);

			no->pc.client =	mgState.fg.netReceive.cur.pc.client -
							pcOffset;
		}

		START_BIT_COUNT(pak, "output");

		mmReceiveNetOutput(	mm,
							pak,
							header,
							no,
							pcOffset,
							recUpdate);

		if (fullUpdate && no) {
			no->flagsMutable.notInterped = 0;
		}
		
		STOP_BIT_COUNT(pak);
	}
	FOR_END;
	
	if(	!td->toFG.outputList.tail &&
		!mm->fg.net.outputList.tail)
	{
		mmCreateFakeNetOutput(mm, td, isLocalManager);
	}
}

static void mmReceiveRareFlagsFromServer(	MovementManager* mm,
											Packet* pak)
{
	U32 rareFlags;
	
	MM_CHECK_STRING_READ(pak, "rareFlags");

	START_BIT_COUNT(pak, "rareFlags");
		rareFlags = pktGetBits(pak, MM_NET_RARE_FLAGS_BIT_COUNT);
	STOP_BIT_COUNT(pak);

	mmSetNetReceiveNoCollFG(mm, !!(rareFlags & BIT(0)));

	mm->fg.flagsMutable.ignoreActorCreate = !!(rareFlags & BIT(1));

	mmSetUseRotationForCapsuleOrientationFG(mm, !!(rareFlags & BIT(2)));

	START_BIT_COUNT(pak, "collBits");
		if(rareFlags & BIT(3)){
			U32 collGroup = pktGetU32(pak);

			mm->fg.collisionGroupMutable = collGroup;
			mmSetNetReceiveCollisionGroupFG(mm, collGroup);
		}

		if(rareFlags & BIT(4)){
			U32 myCollBits = pktGetU32(pak);

			mm->fg.collisionGroupBitsMutable = myCollBits;
			mmSetNetReceiveCollisionBitsFG(mm, myCollBits);
		}

		if(rareFlags & BIT(5)){
			U32 myCollSet = pktGetU32(pak);

			mm->fg.collisionSetMutable = myCollSet;
			mmSetNetReceiveCollisionSetFG(mm, myCollSet);
		}
	STOP_BIT_COUNT(pak);
}

static void mmReceiveHeaderFromServer(	MovementManager* mm,
										Packet* pak,
										MovementNetUpdateHeader* headerOut,
										S32 fullUpdate)
{										
	U32 header;
	
	MM_CHECK_STRING_READ(pak, "header");

	START_BIT_COUNT(pak, "header");
		header = pktGetBits(pak, MM_NET_HEADER_BIT_COUNT);
	STOP_BIT_COUNT(pak);
	
	if(header & BIT(0)){
		headerOut->flags.getOutputCount = 1;
	}
	
	if(	fullUpdate ||
		header & BIT(1))
	{
		mmReceiveRareFlagsFromServer(mm, pak);
	}

	if(header & BIT(2)){
		headerOut->flags.hasPosUpdate = 1;
	}
	
	if(header & BIT(3)){
		headerOut->flags.hasRotFaceUpdate = 1;
	}
	
	if(header & BIT(4)){
		headerOut->flags.hasAnimUpdate = 1;
	}

	if(header & BIT(5)){
		headerOut->flags.hasNotInterpedOutput = 1;
	}
	
	if(header & BIT(6)){
		headerOut->flags.hasRequesterUpdate = 1;
	}
	
	if(header & BIT(7)){
		headerOut->flags.hasResourceUpdate = 1;
	}

	MM_CHECK_STRING_READ(pak, "headerEnd");
}

static void mmReceiveRequesterUpdateFromServer(	MovementManager* mm,
												MovementThreadData* td,
												Packet* pak,
												S32 fullUpdate,
												S32 isLocalManager,
												const MovementNetUpdateHeader* header)
{
	if(isLocalManager){
		MM_CHECK_STRING_READ(pak, "requestersPrivate");

		START_BIT_COUNT(pak, "sync");
			mmReceiveSyncDataFromServer(mm, td, pak, fullUpdate);
		STOP_BIT_COUNT(pak);
	}
	else if(fullUpdate ||
			header->flags.hasRequesterUpdate)
	{
		MM_CHECK_STRING_READ(pak, "requestersPublic");
		
		START_BIT_COUNT(pak, "syncPublic");
			mmReceivePublicSyncDataFromServer(mm, pak, fullUpdate);
		STOP_BIT_COUNT(pak);
	}else{
		MM_CHECK_STRING_READ(pak, "no requesters");
	}
}

static void mmReceiveResourceUpdateFromServer(	MovementManager* mm,
												MovementThreadData* td,
												Packet* pak,
												S32 fullUpdate,
												S32 isLocalManager,
												const MovementNetUpdateHeader* header,
												RecordedEntityUpdate* recUpdate)
{
	if(	fullUpdate ||
		header->flags.hasResourceUpdate)
	{
		MM_CHECK_STRING_READ(pak, "resources");
		
		START_BIT_COUNT(pak, "resources");
			mmReceiveResourcesFG(	mm,
									td,
									pak,
									isLocalManager,
									fullUpdate,
									recUpdate);
		STOP_BIT_COUNT(pak);
	}else{
		MM_CHECK_STRING_READ(pak, "not resources");
	}
}

void mmReceiveFromServer(	MovementManager* mm,
							Packet* pak,
							S32 fullUpdate,
							RecordedEntityUpdate* recUpdate)
{
	MovementThreadData*		td = MM_THREADDATA_FG(mm);
	S32						isLocalManager = 0;
	MovementNetUpdateHeader	header = {0};
	
	PERFINFO_AUTO_START_FUNC();
	
	mmLog(	mm,
			NULL,
			"[net.receive] Begin received server process count: %d",
			mgState.fg.netReceive.cur.pc.server);
	
	if(mm->fg.mcma){
		// Already attached, so just make sure everything is valid.

		assert(mm->fg.mcma->mm == mm);
		assert(mm->fg.mcma->mc == &mgState.fg.mc);
		if(eaFind(&mgState.fg.mc.mcmas, mm->fg.mcma) < 0){
			assert(0);
		}

		isLocalManager = 1;
	}else{
		// Check if this is a local entity.

		S32 entityIndex = INDEX_FROM_REFERENCE(mm->entityRef);
		S32 findIndex = eaiFind(&mgState.fg.netReceive.managerIndexes,
								entityIndex);

		assert(!mm->fg.mcma);

		if(findIndex >= 0){
			mmAttachToClient(	mm,
								&mgState.fg.mc);

			isLocalManager = 1;
		}
	}

	SET_FP_CONTROL_WORD_DEFAULT;

	if(isLocalManager){
		START_BIT_COUNT(pak, "mmReceiveFromServer:local");
	}else{
		START_BIT_COUNT(pak, "mmReceiveFromServer:non-local");
	}
	
	mmReceiveHeaderFromServer(	mm,
								pak,
								&header,
								fullUpdate);

	// MS: WTF is this?

	if(recUpdate){
		recUpdate->dead = mm->fg.flags.noCollision;
	}
	
	if(header.flags.hasPosUpdate){
		START_BIT_COUNT(pak, "outputs (with pos)");
	}else{
		START_BIT_COUNT(pak, "outputs (no pos)");
	}
	{
		mmReceiveNetOutputs(mm,
							td,
							pak,
							fullUpdate,
							isLocalManager,
							&header,
							recUpdate);
	}
	STOP_BIT_COUNT(pak);

	mmReceiveResourceUpdateFromServer(	mm,
										td,
										pak,
										fullUpdate,
										isLocalManager,
										&header,
										recUpdate);

	mmReceiveRequesterUpdateFromServer(	mm,
										td,
										pak,
										fullUpdate,
										isLocalManager,
										&header);

	MM_CHECK_STRING_READ(pak, "done");

	mmLog(	mm,
			NULL,
			"[net.receive] End received server process count: %d",
			mgState.fg.netReceive.cur.pc.server);

	STOP_BIT_COUNT(pak);
	
	PERFINFO_AUTO_STOP();
}

void mmReceiveHeaderFromDemoReplay(	U32 spc,
									U32 cpc)
{
	#define nr mgState.fg.netReceiveMutable

	nr.prevPrev = nr.prev;
	nr.prev = nr.cur;

	nr.cur.pc.client = cpc;
	nr.cur.pc.server = spc;

	nr.cur.offset.clientToServerSync = spc - cpc;
	
	#undef nr

	mmProcessHeader();
}

static void mmReceiveAnimUpdateFromDemo(MovementManager* mm,
										MovementNetOutput* no,
										const RecordedEntityPos* recPos)
{
	U32*	flags = NULL;
	U32*	stances = NULL;
	S32		hasAnimToStart = 0;
	S32		preserveOldFlags = 0;

	int		iFlagStackOverflow = 0;
	int		iStanceStackOverflow = 0;

	eaiStackCreate(&flags, MM_ANIM_VALUE_STACK_SIZE_SMALL);
	eaiStackCreate(&stances, MM_ANIM_VALUE_STACK_SIZE_MODEST);

	EARRAY_CONST_FOREACH_BEGIN(recPos->animBits, i, isize);
	{
		const char* prefix = recPos->animBits[i]->bitName;
		const char* bitName = strchr(prefix, ':');
		U32			bitHandle;

		if(	!bitName ||
			!bitName[1])
		{
			if (strStartsWith(prefix, "Anim:") ||
				strStartsWith(prefix, "LastAnim:"))
			{
				preserveOldFlags = 1;
			}
			continue;
		}

		bitHandle = mmRegisteredAnimBitCreate(	&mgState.animBitRegistry,
												bitName + 1,
												0,
												NULL);

		if(strStartsWith(prefix, "Anim:")){
			ASSERT_FALSE_AND_SET(hasAnimToStart);

			bitHandle = MM_ANIM_HANDLE_WITH_ID(bitHandle,0);
			
			mmLastAnimCopyToValues(	&no->dataMutable.anim,
									&mm->fg.net.lastAnim);

			eaiPush(&no->dataMutable.anim.values,
					MM_ANIM_VALUE(bitHandle, MAVT_ANIM_TO_START));

			mm->fg.net.lastAnimMutable.anim = bitHandle;
			eaiClearFast(&mm->fg.net.lastAnimMutable.flags);
		}
		else if(strStartsWith(prefix, "LastAnim:")){
			bitHandle = MM_ANIM_HANDLE_WITH_ID(bitHandle,0);

			if(bitHandle != mm->fg.net.lastAnim.anim){
				mm->fg.net.lastAnimMutable.anim = bitHandle;
				eaiClearFast(&mm->fg.net.lastAnimMutable.flags);
			}
		}
		else if(strStartsWith(prefix, "Flag:")){
			bitHandle = MM_ANIM_HANDLE_WITH_ID(bitHandle,0);

			preserveOldFlags = 0;

			if(hasAnimToStart){
				eaiPush(&no->dataMutable.anim.values,
						MM_ANIM_VALUE(bitHandle, MAVT_FLAG));
			}else{
				if (eaiSize(&flags) < eaiCapacity(&flags))
					eaiPush(&flags, bitHandle);
				else
					iFlagStackOverflow++;
			}
		}
		else if(strStartsWith(prefix, "Stance:")){

			if (eaiSize(&stances) < eaiCapacity(&stances))
				eaiPush(&stances, bitHandle);
			else
				iStanceStackOverflow++;

			if(eaiFind(&mm->fg.net.stanceBits, bitHandle) < 0){
				eaiPush(&mm->fg.net.stanceBitsMutable, bitHandle);
				eaiPush(&no->dataMutable.anim.values,
						MM_ANIM_VALUE(bitHandle, MAVT_STANCE_ON));
			}
		}
		else if(strStartsWith(prefix, "Detail:")){
			bitHandle = MM_ANIM_HANDLE_WITH_ID(bitHandle,0);

			eaiPush(&no->dataMutable.anim.values,
					MM_ANIM_VALUE(bitHandle, MAVT_DETAIL_ANIM_TO_START));
		}
		else if(strStartsWith(prefix, "DetailFlag:")){
			bitHandle = MM_ANIM_HANDLE_WITH_ID(bitHandle,0);

			eaiPush(&no->dataMutable.anim.values,
					MM_ANIM_VALUE(bitHandle, MAVT_DETAIL_FLAG));
		}
	}
	EARRAY_FOREACH_END;

	if (iFlagStackOverflow)
		Errorf("Movement Manager: attempted flag stack overflow with %i extra values in %s", iFlagStackOverflow, __FUNCTION__);

	if (iStanceStackOverflow)
		Errorf("Movement Manager: attempted stance stack overflow with %i extra values in %s", iStanceStackOverflow, __FUNCTION__);

	if (!hasAnimToStart &&
		!preserveOldFlags)
	{
		EARRAY_INT_CONST_FOREACH_BEGIN_FROM(flags, i, isize, eaiSize(&mm->fg.net.lastAnim.flags));
		{
			eaiPush(&no->dataMutable.anim.values,
					MM_ANIM_VALUE(flags[i], MAVT_FLAG));
		}
		EARRAY_FOREACH_END;

		eaiCopy(&mm->fg.net.lastAnimMutable.flags, &flags);
	}

	EARRAY_INT_FOREACH_REVERSE_BEGIN(mm->fg.net.stanceBits, i);
	{
		if(eaiFind(&stances, mm->fg.net.stanceBits[i]) < 0){
			eaiPush(&no->dataMutable.anim.values,
					MM_ANIM_VALUE(mm->fg.net.stanceBits[i], MAVT_STANCE_OFF));

			eaiRemove(&mm->fg.net.stanceBitsMutable, i);
		}
	}
	EARRAY_FOREACH_END;

	eaiDestroy(&flags);
	eaiDestroy(&stances);
}

static void mmReceiveAnimBitComboFromDemo(	MovementNetOutput* no,
											const RecordedEntityPos* recPos)
{
	static U32* bits;
			
	EARRAY_CONST_FOREACH_BEGIN(recPos->animBits, i, isize);
	{
		U32 bitHandle = mmRegisteredAnimBitCreate(	&mgState.fg.netReceiveMutable.animBitRegistry,
													recPos->animBits[i]->bitName,
													0,
													NULL);
															
		eaiPush(&bits, bitHandle);
	}
	EARRAY_FOREACH_END;
			
	mmRegisteredAnimBitComboFind(	&mgState.fg.netReceiveMutable.animBitRegistry,
									&no->animBitCombo,
									NULL,
									bits);

	eaiSetSize(&bits, 0);
}

void mmReceiveFromDemoReplay(	MovementManager* mm,
								const RecordedEntityUpdate* recUpdate,
								const U32 demoVersion)
{
	MovementThreadData* td = MM_THREADDATA_FG(mm);
	
	EARRAY_CONST_FOREACH_BEGIN(recUpdate->positions, i, isize);
	{
		MovementNetOutput*			no;
		S32							pcOffset = (i + 1 - isize) * MM_PROCESS_COUNTS_PER_STEP;
		const RecordedEntityPos*	recPos = recUpdate->positions[i];
		
		mmNetOutputCreateAndAddTail(mm, td, &no);

		if(FALSE_THEN_SET(mm->fg.flagsMutable.needsNetOutputViewUpdate)){
			mmHandleAfterSimWakesIncFG(mm, "needsNetOutputViewUpdate", __FUNCTION__);
		}

		no->pc.server =	mgState.fg.netReceive.cur.pc.server +
						pcOffset;

		no->pc.client =	mgState.fg.netReceive.cur.pc.client +
						pcOffset;

		MM_CHECK_DYNPOS_DEVONLY(recPos->pos);

		copyVec3(	recPos->pos,
					no->dataMutable.pos);
		copyQuat(	recPos->rot, 
					no->dataMutable.rot);
					
		if(!demoVersion){
			Vec3 z;
			quatToMat3_2(no->data.rot, z);
			getVec3YP(z, &no->dataMutable.pyFace[1], &no->dataMutable.pyFace[0]);
		}else{
			copyVec2(	recPos->pyFace,
						no->dataMutable.pyFace);
		}

		// Get anim bits.

		if(gConf.bNewAnimationSystem){
			mmReceiveAnimUpdateFromDemo(mm, no, recPos);
		}else{
			mmReceiveAnimBitComboFromDemo(no, recPos);
		}
	}
	EARRAY_FOREACH_END;

	mm->fg.flagsMutable.noCollision = recUpdate->dead;

	EARRAY_CONST_FOREACH_BEGIN(recUpdate->resources, i, isize);
	{
		mmReceiveResourceFromReplayFG(mm, recUpdate->resources[i]);
	}
	EARRAY_FOREACH_END;

	mmLog(	mm,
			NULL,
			"[demo] Received demo recorder process count: %u",
			mgState.fg.netReceive.cur.pc.server);
}

U32 mmNetGetLatestSPC(void){
	return mgState.fg.netReceive.cur.pc.server;
}

U32 mmNetGetCurrentViewSPC(F32 fSecondsOffset)
{
	S32 deltaMilliseconds = fSecondsOffset * 1000.f;
	U32 curOffset = (U32)floorf(mgState.fg.netView.spcInterpFloorToCeiling * (mgState.fg.netView.spcCeiling - mgState.fg.netView.spcFloor));
	U32 stepOffset = deltaMilliseconds ? deltaMilliseconds * 60 / 1000 : 0;

	return mgState.fg.netView.spcFloor + curOffset + stepOffset;
}