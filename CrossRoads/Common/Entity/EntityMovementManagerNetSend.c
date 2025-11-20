
#include "EntityMovementManagerPrivate.h"
#include "net/net.h"
#include "net/netpacketutil.h"
#include "StructNet.h"
#include "wininclude.h"
#include "mutex.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

void mmInputStepWritePacket(const MovementInputStep* miStep,
							Packet* pak)
{
	if(!miStep){
		START_BIT_COUNT(pak, "no step");
		pktSendBitsAuto(pak, 0);
		STOP_BIT_COUNT(pak);
	}else{
		const MovementInputEvent*	mie;
		U32							mieCount = 0;
		
		START_BIT_COUNT(pak, "step");

		for(mie = miStep->mieList.head;
			mie;
			mie = mie->next)
		{
			mieCount++;
		}
		
		pktSendBitsAuto(pak, mieCount);

		for(mie = miStep->mieList.head;
			mie;
			mie = mie->next)
		{
			START_BIT_COUNT(pak, "event");
			
			assert(mie->value.mivi < BIT(5));
			
			if(INRANGE(mie->value.mivi, MIVI_BIT_LOW, MIVI_BIT_HIGH)){
				pktSendBits(pak,
							8,
							mie->value.mivi |
								(mie->value.bit ? BIT(5) : 0) |
								(mie->value.flags.isDoubleTap ? BIT(6) : 0));
			}
			else if(INRANGE(mie->value.mivi, MIVI_F32_LOW, MIVI_F32_HIGH)){
				pktSendBits(pak, 8, mie->value.mivi);
				pktSendF32(pak, mie->value.f32);
			}
			else if(mie->value.mivi == MIVI_DEBUG_COMMAND){
				pktSendBits(pak, 8, mie->value.mivi);
				pktSendString(pak, mie->commandMutable);
			}
			else if(mie->value.mivi == MIVI_RESET_ALL_VALUES){
				pktSendBits(pak, 8, mie->value.mivi);
				pktSendU32(pak, mie->value.u32);
			}else{
				pktSendBits(pak, 8, mie->value.mivi);
			}
			
			STOP_BIT_COUNT(pak);
		}

		STOP_BIT_COUNT(pak);
	}
}

void mmSendToServer(Packet* pak){
	MovementClient*				mc = &mgState.fg.mc;
	S32							found = 0;
	MovementClientInputStep*	mciStep;
	S32							i;
	
	mc->packetCount++;
	
	// Send the time delta.

	START_BIT_COUNT(pak, "msDelta");
	{
		static U32	msLastSendTime;
		U32			msCurTime = timeGetTime();
		U32			msDelta = msLastSendTime ? msCurTime - msLastSendTime : 0;

		pktSendBitsAuto(pak, msDelta);

		msLastSendTime = msCurTime;
	}
	STOP_BIT_COUNT(pak);
	
	START_BIT_COUNT(pak, "steps");
	for(mciStep = mc->mciStepList.head, i = 0;
		mciStep;
		mciStep = mciStep->next, i++)
	{
		if(FALSE_THEN_SET(mciStep->flags.sentToServer)){
			if(FALSE_THEN_SET(found)){
				START_BIT_COUNT(pak, "header");
				
				pktSendBits(pak, 1, 1);
				pktSendBits(pak, 32, mciStep->pc.client);
				assert(mc->mciStepList.count - i == mc->mciStepList.unsentCount);
				pktSendBitsAuto(pak, mc->mciStepList.unsentCount);
				mc->mciStepListMutable.unsentCount = 0;

				pktSendBitsAuto(pak,
								eaSize(&mc->mcmas));
				
				EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, j, jsize);
				{
					const MovementManager*	mm = mc->mcmas[j]->mm;
					S32						index = INDEX_FROM_REFERENCE(mm->entityRef);
					
					pktSendBitsAuto(pak, index);
				}
				EARRAY_FOREACH_END;
				
				STOP_BIT_COUNT(pak);
			}
		}

		if(found){
			#if 0
			{
				printf(	"sending mciStep: %d (%d - %d)\n",
						mciStep->pc.client,
						mc->mciStepList.head->pc.client,
						mc->mciStepList.tail->pc.client);
			}
			#endif
						
			if(mciStep->pc.serverSync){
				START_BIT_COUNT(pak, "serverSync");
				pktSendBits(pak, 1, 1);
				pktSendBits(pak, 32, mciStep->pc.serverSync);
				STOP_BIT_COUNT(pak);
			}else{
				START_BIT_COUNT(pak, "noServerSync");
				pktSendBits(pak, 1, 0);
				STOP_BIT_COUNT(pak);
			}

			// For each manager, find the matching input step.

			EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, j, jsize);
			{
				const MovementManager*	mm = mc->mcmas[j]->mm;
				S32						foundStep = 0;
				
				EARRAY_CONST_FOREACH_BEGIN(mciStep->miSteps, k, ksize);
				{
					const MovementInputStep* miStep = mciStep->miSteps[k];
					
					if(miStep->mm == mm){
						foundStep = 1;
						mmInputStepWritePacket(miStep, pak);
						break;
					}
				}
				EARRAY_FOREACH_END;
				
				if(!foundStep){
					mmInputStepWritePacket(NULL, pak);
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	STOP_BIT_COUNT(pak);
	
	if(!found){
		START_BIT_COUNT(pak, "not found header");
		pktSendBits(pak, 1, 0);
		STOP_BIT_COUNT(pak);
	}
}

static void mmLogSyncUpdateBoth(MovementRequester* mr,
								S32 fullUpdate,
								ParseTable* pti,
								void* structPrev,
								void* structCur,
								const char* structName)
{
	mrLog(	mr,
			NULL,
			"[net.mrSync] Sending %s %s.",
			structName,
			fullUpdate ? "full" : "diff");

	if(	!fullUpdate &&
		structPrev)
	{
		mmLogSyncUpdate(mr,
						pti,
						structPrev,
						"Previously sent",
						structName);
	}
	
	if(structCur){
		mmLogSyncUpdate(mr,
						pti,
						structCur,
						"Currently sending",
						structName);
	}
}

static void mmLogSyncUpdates(	MovementRequester* mr,
								MovementRequesterThreadData* mrtd,
								S32 fullUpdate)
{
	mrLog(	mr,
			NULL,
			"[net.mrSync] Sending updates with sync s%u.",
			mgState.fg.frame.prev.pcNetSend +
				MM_PROCESS_COUNTS_PER_STEP);

	mmLogSyncUpdateBoth(mr,
						fullUpdate,
						mr->mrc->pti.bg,
						mr->fg.net.prev.userStruct.bg,
						SAFE_MEMBER(mrtd->toFG.predict, userStruct.bg),
						"bg");
	
	mmLogSyncUpdateBoth(mr,
						fullUpdate,
						mr->mrc->pti.sync,
						mr->fg.net.prev.userStruct.sync,
						mr->userStruct.sync.fg,
						"sync");

	mmLogSyncUpdateBoth(mr,
						fullUpdate,
						mr->mrc->pti.syncPublic,
						mr->fg.net.prev.userStruct.syncPublic,
						mr->userStruct.syncPublic.fg,
						"syncPublic");
}

static void mmSendSyncPosRotFace(	MovementManager* mm,
									MovementClientManagerAssociation* mcma,
									const Vec3 pos,
									const Quat rot,
									const Vec2 pyFace,
									S32 fullUpdate,
									Packet* pak)
{
	S32 sendPos =	fullUpdate ||
					!sameVec3(	pos,
								mm->fg.net.sync.pos);
	S32 sendRot =	fullUpdate ||
					!sameQuat(	rot,
								mm->fg.net.sync.rot);
	S32 sendFace =	fullUpdate ||
					!sameVec2(	pyFace,
								mm->fg.net.sync.pyFace);
	U32 flags = (mcma->flags.forcedSetPos ? BIT(0) : 0) |
				(mcma->flags.forcedSetRot ? BIT(1) : 0) |
				(sendPos ? BIT(2) : 0) |
				(sendRot ? BIT(3) : 0) |
				(sendFace ? BIT(4) : 0);
				
	mmLog(	mm,
			NULL,
			"[net.sync] Sending sync (%s%s%s):\n"
			"%s pos(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
			"%s rot(%1.3f, %1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x, %8.8x]\n"
			"%s pyFace(%1.3f, %1.3f) [%8.8x, %8.8x]\n"
			"Previous sync values:\n"
			"pos(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
			"rot(%1.3f, %1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x, %8.8x]\n"
			"pyFace(%1.3f, %1.3f) [%8.8x, %8.8x]"
			,
			fullUpdate ? "full update, " : "diff, ",
			mcma->flags.forcedSetPos ? "forcedSetPos, " : "",
			mcma->flags.forcedSetRot ? "forcedSetRot, " : "",
			sendPos ? "new" : "same",
			vecParamsXYZ(pos),
			vecParamsXYZ((S32*)pos),
			sendRot ? "new" : "same",
			quatParamsXYZW(rot),
			quatParamsXYZW((S32*)rot),
			sendFace ? "new" : "same",
			vecParamsXY(pyFace),
			vecParamsXY((S32*)pyFace),
			vecParamsXYZ(mm->fg.net.sync.pos),
			vecParamsXYZ((S32*)mm->fg.net.sync.pos),
			quatParamsXYZW(mm->fg.net.sync.rot),
			quatParamsXYZW((S32*)mm->fg.net.sync.rot),
			vecParamsXY(mm->fg.net.sync.pyFace),
			vecParamsXY((S32*)mm->fg.net.sync.pyFace));

	mcma->flags.forcedSetRot = 0;

	pktSendBits(pak, MM_NET_SYNC_FLAGS_BIT_COUNT, flags);

	if(	TRUE_THEN_RESET(mcma->flags.forcedSetPos) ||
		fullUpdate)
	{
		pktSendU32(pak, mcma->setPos.versionToSend);
	}

	if(sendPos){
		pktSendVec3(pak, pos);

		copyVec3(	pos,
					mm->fg.net.sync.pos);
	}
	
	if(sendRot){
		pktSendQuat(pak, rot);
		
		copyQuat(	rot,
					mm->fg.net.sync.rot);
	}
	
	if(sendFace){
		pktSendVec2(pak, pyFace);

		copyVec2(	pyFace,
					mm->fg.net.sync.pyFace);
	}
}

static void mmLogSendingDataClassBits(	MovementRequester* mr,
										MovementRequesterThreadData* mrtd)
{
	char bufferNow[100];
	char bufferOld[100];
	
	mmGetDataClassNames(SAFESTR(bufferNow), SAFE_MEMBER(mrtd->toFG.predict, ownedDataClassBits));
	mmGetDataClassNames(SAFESTR(bufferOld), mr->fg.net.prev.ownedDataClassBits);

	mrLog(	mr,
			NULL,
			"[net.mrSync] Owned bits: %s (prev %s)",
			bufferNow,
			bufferOld);
}

static void mmLogSendingHandledMsgs(MovementRequester* mr,
									MovementRequesterThreadData* mrtd)
{
	char bufferNow[100];
	char bufferOld[100];
	
	mmGetHandledMsgsNames(SAFESTR(bufferNow), SAFE_MEMBER(mrtd->toFG.predict, handledMsgs));
	mmGetDataClassNames(SAFESTR(bufferOld), mr->fg.net.prev.handledMsgs);

	mrLog(	mr,
			NULL,
			"[net.mrSync] Handled msgs: %s (prev %s)",
			bufferNow,
			bufferOld);
}

static void mmRequesterSendSync(MovementManager* mm,
								MovementClientManagerAssociation* mcma,
								MovementRequester* mr,
								S32 fullUpdate,
								Packet* pak)
{
	MovementRequesterThreadData*	mrtd = MR_THREADDATA_FG(mr);
	MovementRequesterClass* 		mrc	= mr->mrc;
	S32								hasStateFromBG = !!SAFE_MEMBER(mrtd->toFG.predict, userStruct.bg);
	S32								sendMe = 0;
	S32								sendOwnedDataClassBits = 0;
	S32								sendHandledMsgs = 0;
	S32								sendBG = 0;
	S32								sendSyncStructs = 0;
	S32								sendSyncUpdated = 0;
	
	ANALYSIS_ASSUME(mm);
	
	if(!mrc->flags.syncToClient){
		return;
	}
	
	if(	mr->fg.flags.sentCreateToOwner
		||
		mrc->pti.syncPublic &&
		mr->fg.flags.sentCreate)
	{
		if(mr->fg.flags.destroyed){
			// Only send destroys in a diff packet.

			if(	!fullUpdate &&
				!mr->fg.flags.sentDestroy)
			{
				sendMe = 1;
			}
		}
		else if(fullUpdate ||
				!mr->fg.flags.sentCreateToOwner)
		{
			// Send everything that is available (no BG unless it's there).
			
			sendOwnedDataClassBits = hasStateFromBG;
			sendHandledMsgs = hasStateFromBG;
			sendBG = hasStateFromBG;
			sendSyncStructs = 1;
			sendSyncUpdated = 1;
			sendMe = 1;
		}else{
			// Check if anything has changed.

			if(	mcma->mc->netSend.flags.sendStateBG &&
				hasStateFromBG)
			{
				sendOwnedDataClassBits = mrtd->toFG.predict->ownedDataClassBits !=
											mr->fg.net.prev.ownedDataClassBits;
							
				sendHandledMsgs = mrtd->toFG.predict->handledMsgs !=
									mr->fg.net.prev.handledMsgs;

				sendBG = 	StructCompare(	mrc->pti.bg,
											mrtd->toFG.predict->userStruct.bg,
											mr->fg.net.prev.userStruct.bg,
											0, 0, 0);
			}
			
			if(mr->fg.flags.hasSyncToBG){
				sendSyncUpdated = 1;
				
				sendSyncStructs =	StructCompare(	mrc->pti.sync,
													mr->userStruct.sync.fg,
													mr->fg.net.prev.userStruct.sync,
													0, 0, 0)
									||
									StructCompare(	mrc->pti.syncPublic,
													mr->userStruct.syncPublic.fg,
													mr->fg.net.prev.userStruct.syncPublic,
													0, 0, 0);
			}
			
			sendMe =	sendOwnedDataClassBits ||
						sendHandledMsgs ||
						sendBG ||
						sendSyncUpdated;
		}
	}
	else if(!mr->fg.flags.destroyed){
		// This will be a full update.
		
		sendOwnedDataClassBits = hasStateFromBG;
		sendHandledMsgs = hasStateFromBG;
		sendBG = hasStateFromBG;
		sendSyncStructs = 1;
		sendSyncUpdated = 1;
		sendMe = 1;
	}

	if(!sendMe){
		return;
	}

	if(mr->fg.flags.destroyed){
		#if MM_VERIFY_SENT_REQUESTERS
			mr->fg.flagsMutable.didSendDestroy = 1;
		#endif

		pktSendBits(pak,
					MM_NET_REQUESTER_UPDATE_FLAGS_BIT_COUNT,
					BIT(0));
	}else{
		#if MM_VERIFY_SENT_REQUESTERS
			mr->fg.flagsMutable.didSendCreate = 1;
		#endif

		pktSendBits(pak,
					MM_NET_REQUESTER_UPDATE_FLAGS_BIT_COUNT,
					0 |
						(hasStateFromBG ? BIT(1) : 0) |
						(sendOwnedDataClassBits ? BIT(2) : 0) |
						(sendHandledMsgs ? BIT(3) : 0) |
						(sendBG ? BIT(4) : 0) |
						(sendSyncUpdated ? BIT(5) : 0) |
						(sendSyncStructs ? BIT(6) : 0));
	}

	pktSendBits(pak,
				32,
				mr->handle);

	if(mr->fg.flags.destroyed){
		char text[200];
		sprintf(text,
				"handle %d, Created at %d, mrcID=%d, sentCreate=%d(spc=%d), sentDestroy=%d",
				mr->handle,
				mr->pcLocalWhenCreated,
				mr->mrc->id,
				mr->fg.flags.debugSentCreateToClient,
				mr->fg.debug.sentToClientSPC,
				mr->fg.flags.debugSentDestroyToClient);
		pktSendString(pak, text);
		ASSERT_FALSE_AND_SET(mr->fg.flagsMutable.debugSentDestroyToClient);
		return;
	}
	
	if(FALSE_THEN_SET(mr->fg.flagsMutable.debugSentCreateToClient)){
		mr->fg.debug.sentToClientSPC = mgState.fg.frame.cur.pcStart;
	}

	pktSendBits(pak, 8, mr->mrc->id);

	if(	MMLOG_IS_ENABLED(mm) &&
		(	sendBG ||
			sendSyncStructs))
	{
		mmLogSyncUpdates(mr, mrtd, fullUpdate);
	}

	// Send the owned data class bits.

	if(sendOwnedDataClassBits){
		if(MMLOG_IS_ENABLED(mm)){
			mmLogSendingDataClassBits(mr, mrtd);
		}

		pktSendBits(pak,
					MDC_COUNT,
					mrtd->toFG.predict->ownedDataClassBits);
	}
	
	// Send the handled msgs.
	
	if(sendHandledMsgs){
		if(MMLOG_IS_ENABLED(mm)){
			mmLogSendingHandledMsgs(mr, mrtd);
		}
		
		pktSendBits(pak,
					MR_HANDLED_MSGS_BIT_COUNT,
					mrtd->toFG.predict->handledMsgs);
	}

	// Send the latest BG struct.
		
	if(sendBG){
		#if MM_VERIFY_SENT_SYNC_STRUCTS
		{
			if(	fullUpdate ||
				!mr->fg.net.prev.userStruct.bg)
			{
				if(!fullUpdate){
					//assert(!mr->fg.flags.sentCreate);
					assert(!mr->fg.flags.sentCreateTest);
				}

				pktSendBits(pak, 1, 0);
			}else{
				pktSendBits(pak, 1, 1);
				
				ParserSend(	mrc->pti.bg,
							pak,
							NULL,
							mr->fg.net.prev.userStruct.bg,
							0,
							0,
							0,
							NULL);

				assert(mr->fg.net.prev.userStruct.bgTest);

				if(StructCompare(	mr->mrc->pti.bg,
									mr->fg.net.prev.userStruct.bgTest,
									mr->fg.net.prev.userStruct.bg,
									0, 0, 0))
				{
					assertmsg(0, "Prev BG is different than last sent BG.");
				}
			}
		}
		#endif

		ParserSend(	mrc->pti.bg,
					pak,
					fullUpdate ? NULL : mr->fg.net.prev.userStruct.bg,
					mrtd->toFG.predict->userStruct.bg,
					0,
					0,
					0,
					NULL);
		
		#if MM_VERIFY_SENT_SYNC_STRUCTS
		{
			mr->fg.flagsMutable.sentCreateTest = 1;
			
			assert(mrtd->toFG.userStruct.bg);

			mmStructAllocAndCopy(	mr->mrc->pti.bg,
									mr->fg.net.prev.userStruct.bgTest,
									mrtd->toFG.userStruct.bg,
									mm);

			ParserSend(	mrc->pti.bg,
						pak,
						NULL,
						mrtd->toFG.userStruct.bg,
						0,
						0,
						0,
						NULL);
		}
		#endif
	}
	
	// Send the private sync struct.

	mr->fg.flagsMutable.hasSyncToBG = 0;
	
	if(sendSyncUpdated){
		// Send sync structs and queue them to BG.
		
		if(	mr->fg.flags.needsAfterSync &&
			FALSE_THEN_SET(mm->fg.flagsMutable.mrNeedsAfterSync))
		{
			mmHandleAfterSimWakesIncFG(mm, "mrNeedsAfterSync", __FUNCTION__);
		}

		mm->fg.flagsMutable.mrHasSyncToQueue = 1;
		mr->fg.flagsMutable.hasSyncToQueue = 1;
		
		if(sendSyncStructs){
			ParserSend(	mrc->pti.sync,
						pak,
						fullUpdate ? NULL : mr->fg.net.prev.userStruct.sync,
						mr->userStruct.sync.fg,
						0,
						0,
						0,
						NULL);
		}
					
		mmStructAllocAndCopy(	mrc->pti.sync,
								mr->userStruct.sync.fgToQueue,
								mr->userStruct.sync.fg,
								mm);

		// Send the public sync struct.
		
		if(mr->userStruct.syncPublic.fg){
			if(sendSyncStructs){
				ParserSend(	mrc->pti.syncPublic,
							pak,
							fullUpdate ? NULL : mr->fg.net.prev.userStruct.syncPublic,
							mr->userStruct.syncPublic.fg,
							0,
							0,
							0,
							NULL);
			}
						
			mmStructAllocAndCopy(	mrc->pti.syncPublic,
									mr->userStruct.syncPublic.fgToQueue,
									mr->userStruct.syncPublic.fg,
									mm);
		}
	}
}

#if !MM_VERIFY_SENT_REQUESTERS
	#define mmSendRequesterSyncVerify(mm, pak)
#else
static void mmSendRequesterSyncVerify(	MovementManager* mm,
										Packet* pak)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		const MovementRequester* mr = mm->fg.requesters[i];

		if(	!mr->mrc->flags.syncToClient ||
			mr->fg.flags.destroyed)
		{
			continue;
		}

		pktSendBitsAuto(pak, mr->handle);
		pktSendBitsAuto(pak, mr->mrc->id);
	}
	EARRAY_FOREACH_END;

	pktSendBitsAuto(pak, 0);
}
#endif

static __forceinline void mmSendSyncDataToClient(	MovementManager* mm,
													MovementClientManagerAssociation* mcma,
													Packet* pak,
													S32 fullUpdate)
{
	MovementThreadData* td;
	S32					sendStateBG;

	PERFINFO_AUTO_START_FUNC();
	
	td = MM_THREADDATA_FG(mm);

	sendStateBG =	fullUpdate ||
					mcma->mc->netSend.flags.sendStateBG;

	if(sendStateBG){
		const MovementOutput* oTail = td->toFG.outputList.tail;
		
		mcma->flags.sentStateBG = 1;

		MM_CHECK_STRING_WRITE(pak, "hasStateBG");

		if(oTail){
			mmLog(	mm,
					NULL,
					"[net.sync] Sending sync using output c%u/s%u (%s%s).",
					oTail->pc.client,
					oTail->pc.server,
					mm->fg.flags.posNeedsForcedSetAck ? "posNeedsForcedSetAck, " : "",
					mm->fg.flags.rotNeedsForcedSetAck ? "rotNeedsForcedSetAck, " : "");

			mmSendSyncPosRotFace(	mm,
									mm->fg.mcma,
									mm->fg.flags.posNeedsForcedSetAck ?
										mm->fg.pos :
										oTail->data.pos,
									mm->fg.flags.rotNeedsForcedSetAck ?
										mm->fg.rot :
										oTail->data.rot,
									mm->fg.flags.rotNeedsForcedSetAck ?
										mm->fg.pyFace :
										oTail->data.pyFace,
									fullUpdate,
									pak);
		}else{
			mmLog(	mm,
					NULL,
					"[net.sync] Sending sync using FG values.");

			mmSendSyncPosRotFace(	mm,
									mm->fg.mcma,
									mm->fg.pos,
									mm->fg.rot,
									mm->fg.pyFace,
									fullUpdate,
									pak);
		}
	}
	
	// Send requesters.
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		mmRequesterSendSync(mm,
							mm->fg.mcma,
							mm->fg.requesters[i],
							fullUpdate,
							pak);
	}
	EARRAY_FOREACH_END;

	pktSendBits(pak,
				MM_NET_REQUESTER_UPDATE_FLAGS_BIT_COUNT,
				0);

	mmSendRequesterSyncVerify(mm, pak);

	PERFINFO_AUTO_STOP();
}

static S32 mmHasPublicRequesterDataToSend(MovementManager* mm){
	return	mm->fg.flags.mrIsNewToSend ||
			mm->fg.flags.mrHasSyncToSend ||
			mm->fg.flags.mrHasDestroyToSend;
}

static __forceinline void mmSendPublicSyncDataToClient(	MovementManager* mm,
														Packet* pak,
														S32 fullUpdate)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
	{
		MovementRequester*		mr = mm->fg.requesters[i];
		MovementRequesterClass* mrc = mr->mrc;
		S32						sendMe = 0;
		
		if(!mrc->pti.syncPublic){
			continue;
		}
		
		if(mr->fg.flags.sentCreate){
			if(mr->fg.flags.destroyed){
				// Only send destroys in a diff packet.

				if(	!fullUpdate &&
					!mr->fg.flags.sentDestroy)
				{
					sendMe = 1;
				}
			}
			else if(fullUpdate){
				sendMe = 1;
			}
			else if(mr->fg.flags.hasSyncToSend &&
					StructCompare(	mrc->pti.syncPublic,
									mr->fg.net.prev.userStruct.syncPublic,
									mr->userStruct.syncPublic.fg, 0, 0, 0))
			{
				sendMe = 1;
			}				
		}
		else if(!mr->fg.flags.destroyed){
			// This will be a full update.
			
			sendMe = 1;
		}

		if(!sendMe){
			continue;
		}
		
		pktSendBits(pak, 1, 1);

		pktSendBits(pak, 32, mr->handle);
		pktSendBits(pak, 8, mr->mrc->id);

		if(mr->fg.flags.destroyed){
			pktSendBits(pak, 1, 0);

			continue;
		}
		
		pktSendBits(pak, 1, 1);

		// Send the public sync struct.
		
		ParserSend(	mr->mrc->pti.syncPublic,
					pak,
					fullUpdate ?
						NULL :
						mr->fg.net.prev.userStruct.syncPublic,
					mr->userStruct.syncPublic.fg,
					0,
					0,
					0,
					NULL);
	}
	EARRAY_FOREACH_END;
	
	pktSendBits(pak, 1, 0);

	PERFINFO_AUTO_STOP();
}

static void mmClientSendSyncHeaderToClient(	MovementClient* mc,
											Packet* pak,
											S32 fullUpdate)
{
	const U32 updateManagersFlag = TRUE_THEN_RESET(mc->netSend.flags.updateManagers) ? BIT(2) : 0;
	
	MM_CHECK_STRING_WRITE(pak, "syncHeaderStart");

	if(!mc->netSend.flags.sendStateBG){
		pktSendBits(pak,
					8,
					updateManagersFlag);
	}else{
		const U32	cpc = mc->netSend.sync.cur.cpc;
		const U32	spc = mc->netSend.sync.cur.spc;
		const U32	forcedStepCount = mc->netSend.sync.cur.forcedStepCount;
		U32			cpcDelta;
		U32			spcOffset;
		
		MM_EXTRA_ASSERT(!(cpc % MM_PROCESS_COUNTS_PER_STEP));
		MM_EXTRA_ASSERT(!(mc->netSend.sync.prev.cpc % MM_PROCESS_COUNTS_PER_STEP));
		
		cpcDelta =	(	cpc -
						mc->netSend.sync.prev.cpc) /
					MM_PROCESS_COUNTS_PER_STEP;

		mc->netSend.sync.prev.cpc = cpc;
		
		spcOffset =	mgState.fg.netSendToClient.cur.pc -
					spc;
										
		if(mgState.debug.activeLogCount){
			EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
			{
				MovementManager* mm = mc->mcmas[i]->mm;
				
				mmLog(	mm,
						NULL,
						"[net.sync] Sending sync (slot %u): c%u/s%u (clientToServer %d)",
						MM_FG_SLOT,
						cpc,
						spc,
						spc - cpc);

				if(spcOffset){
					mmLog(	mm,
							NULL,
							"[net.sync] Sending spcOffset %u (s%u, c%u)",
							spcOffset,
							mgState.fg.netSendToClient.cur.pc,
							spc);
				}
				
				if(forcedStepCount){
					mmLog(	mm,
							NULL,
							"[net.sync] Sending forcedStepCount %u",
							forcedStepCount);
				}
			}
			EARRAY_FOREACH_END;
		}

		if(	fullUpdate ||
			cpcDelta >= BIT_RANGE(0, 3))
		{
			pktSendBits(pak,
						MM_CLIENT_NET_SYNC_HEADER_BIT_COUNT,
						BIT(0) |
							(spcOffset ? 0 : BIT(1)) |
							updateManagersFlag |
							(forcedStepCount ? BIT(3) : 0) |
							(BIT_RANGE(0, 3) << 4)
						);
						
			pktSendBits(pak,
						32,
						cpc);
		}else{
			pktSendBits(pak,
						MM_CLIENT_NET_SYNC_HEADER_BIT_COUNT,
						BIT(0) |
							(spcOffset ? 0 : BIT(1)) |
							updateManagersFlag |
							(forcedStepCount ? BIT(3) : 0) |
							(cpcDelta << 4)
						);
		}
			
		if(spcOffset){
			pktSendBitsAuto(pak, spcOffset);
		}
		
		if(forcedStepCount){
			pktSendBitsAuto(pak, forcedStepCount);
		}
	}
	
	if(updateManagersFlag){
		pktSendBitsAuto(pak, eaSize(&mc->mcmas));
		
		EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, i, isize);
		{
			MovementClientManagerAssociation*	mcma = mc->mcmas[i];
			S32									index = INDEX_FROM_REFERENCE(mcma->mm->entityRef);
			
			pktSendBitsAuto(pak, index);
			pktSendBits(pak, 1, FALSE_THEN_SET(mcma->flags.sentToClient));
		}
		EARRAY_FOREACH_END;
	}

	MM_CHECK_STRING_WRITE(pak, "syncHeaderStop");
}

static void mmSendAnimBitAndGeometryUpdate(	MovementClient* mc,
											Packet* pak)
{
	readLockU32(&mgState.animBitRegistry.bitLock);
	readLockU32(&mgState.animBitRegistry.comboLock);

	{
		U32 bitCount = eaSize(&mgState.animBitRegistry.handleToBit);
		U32 needsBits = bitCount != mc->netSend.nextAnimBit;
		U32 comboCount = eaSize(&mgState.animBitRegistry.allCombos);
		U32 needsCombos = comboCount != mc->netSend.nextAnimBitCombo;
		U32 geometryCount = eaSize(&mgState.fg.geos);
		U32 needsGeometry = geometryCount != mc->netSend.nextGeometry;
		U32 bodyCount = eaSize(&mgState.bodies);
		U32 needsBodies = bodyCount != mc->netSend.nextBody;
		S32 fullUpdate = FALSE_THEN_SET(mc->netSend.flags.animAndGeoWereSent);
		
		readUnlockU32(&mgState.animBitRegistry.comboLock);
		readUnlockU32(&mgState.animBitRegistry.bitLock);

		if(	!fullUpdate &&
			!needsBits &&
			!needsCombos &&
			!needsGeometry &&
			!needsBodies)
		{
			pktSendBits(pak, MM_NET_ANIM_GEO_HEADER_BIT_COUNT, 0);
		}else{
			pktSendBits(pak,
						MM_NET_ANIM_GEO_HEADER_BIT_COUNT,
						1 |
						(fullUpdate ? BIT(1) : 0) |
						(needsBits ? BIT(2) : 0) |
						(needsCombos ? BIT(3) : 0) |
						(needsGeometry ? BIT(4) : 0) |
						(needsBodies ? BIT(5) : 0));
			
			// Send anim bit updates.
			
			if(needsBits){
				readLockU32(&mgState.animBitRegistry.bitLock);

				pktSendBitsAuto(pak, bitCount - mc->netSend.nextAnimBit - 1);
				
				for(; mc->netSend.nextAnimBit != bitCount; mc->netSend.nextAnimBit++){
					const MovementRegisteredAnimBit* bit;
					
					assert(mgState.animBitRegistry.handleToBit);
					
					bit = mgState.animBitRegistry.handleToBit[mc->netSend.nextAnimBit];
					
					START_BIT_COUNT(pak, "bitName");
						pktSendString(pak, bit->bitName);
					STOP_BIT_COUNT(pak);
					
					pktSendBits(pak, 1, bit->flags.isFlashBit);
				}
				
				readUnlockU32(&mgState.animBitRegistry.bitLock);
			}
					
			// Send combo updates.
				
			if(needsCombos){
				readLockU32(&mgState.animBitRegistry.comboLock);

				pktSendBitsAuto(pak, comboCount - mc->netSend.nextAnimBitCombo - 1);
				
				for(; mc->netSend.nextAnimBitCombo != comboCount; mc->netSend.nextAnimBitCombo++){
					MovementRegisteredAnimBitCombo* combo;
					
					assert(mgState.animBitRegistry.allCombos);
					
					combo = mgState.animBitRegistry.allCombos[mc->netSend.nextAnimBitCombo];
					
					//printf("Sending combo: \"%s\"\n", combo->keyName);
					
					pktSendBitsAuto(pak, eaiSize(&combo->bits));
					
					EARRAY_INT_CONST_FOREACH_BEGIN(combo->bits, i, isize);
					{
						assert(combo->bits[i] < bitCount);
						pktSendBitsAuto(pak, combo->bits[i]);
					}
					EARRAY_FOREACH_END;
				}

				readUnlockU32(&mgState.animBitRegistry.comboLock);
			}

			if(needsGeometry){
				assert(mgState.fg.geos);
				
				pktSendBitsAuto(pak, geometryCount - mc->netSend.nextGeometry - 1);
				
				for(; mc->netSend.nextGeometry != geometryCount; mc->netSend.nextGeometry++){
					const MovementGeometry* geo = mgState.fg.geos[mc->netSend.nextGeometry];
					
					assert(geo->index == mc->netSend.nextGeometry);

					pktSendBitsAuto(pak, geo->geoType);

					switch(geo->geoType){
						xcase MM_GEO_MESH:{
							pktSendBitsAuto(pak, geo->mesh.vertCount);
							pktSendBitsAuto(pak, geo->mesh.triCount);
					
							FOR_BEGIN(i, (S32)geo->mesh.vertCount);
								pktSendVec3(pak, geo->mesh.verts + i * 3);
							FOR_END;
					
							FOR_BEGIN(i, (S32)geo->mesh.triCount);
								pktSendIVec3(pak, geo->mesh.tris + i * 3);
							FOR_END;
						}

						xcase MM_GEO_GROUP_MODEL:{
							pktSendString(pak, geo->model.modelName);
						}

						xcase MM_GEO_WL_MODEL:{
							if(!geo->model.fileName){
								pktSendBitsAuto(pak, 0);
							}else{
								pktSendBitsAuto(pak, 1);
								pktSendString(pak, geo->model.fileName);
							}

							pktSendString(pak, geo->model.modelName);
						}
					}
				}
			}
			
			if(needsBodies){
				assert(mgState.bodies);
				
				pktSendBitsAuto(pak, bodyCount - mc->netSend.nextBody - 1);
				
				for(; mc->netSend.nextBody != bodyCount; mc->netSend.nextBody++){
					MovementBody* b = mgState.bodies[mc->netSend.nextBody];
					
					assert(b->index == mc->netSend.nextBody);
					
					pktSendF32(pak, b->radius);
					pktSendBitsAuto(pak, eaSize(&b->parts));
					pktSendBitsAuto(pak, eaSize(&b->capsules));
					
					EARRAY_CONST_FOREACH_BEGIN(b->parts, i, isize);
					{
						const MovementBodyPart* p = b->parts[i];
						
						assert(p->geo->index < mc->netSend.nextGeometry);
						
						pktSendBitsAuto(pak, p->geo->index);
						pktSendVec3(pak, p->pos);
						pktSendVec3(pak, p->pyr);
					}
					EARRAY_FOREACH_END;
					
					EARRAY_CONST_FOREACH_BEGIN(b->capsules, i, isize);
					{
						const Capsule* c = b->capsules[i];
						
						pktSendVec3(pak, c->vStart);
						pktSendVec3(pak, c->vDir);
						pktSendF32(pak, c->fLength);
						pktSendF32(pak, c->fRadius);
						pktSendU32(pak, c->iType);
					}
					EARRAY_FOREACH_END;
				}
			}
		}
	}
}

static void mmClientSendStatsFrames(MovementClient* mc,
									Packet* pak)
{
	pktSendBitsAuto(pak, mc->stats.frames->count);
	
	FOR_BEGIN(i, (S32)mc->stats.frames->count);
	{
		const MovementClientStatsFrame* f = mc->stats.frames->frames + i;

		pktSendBits(pak,
					8,
					0 |
						(f->serverStepCount ? BIT(0) : 0) |
						(f->leftOverSteps ? BIT(1) : 0) |
						(f->behind ? BIT(2) : 0) |
						(f->usedSteps ? BIT(3) : 0) |
						(f->skipSteps ? BIT(4) : 0) |
						(f->consolidateStepsEarly ? BIT(5) : 0) |
						(f->consolidateStepsLate ? BIT(6) : 0) |
						(f->flags.isCorrectionFrame || f->forcedSteps ? BIT(7) : 0));
					
		#define SEND(x) if(x)pktSendBitsAuto(pak, x)
		SEND(f->serverStepCount);
		SEND(f->leftOverSteps);
		SEND(f->behind);
		SEND(f->usedSteps);
		SEND(f->skipSteps);
		SEND(f->consolidateStepsEarly);
		SEND(f->consolidateStepsLate);
		SEND((f->forcedSteps << 1) | f->flags.isCorrectionFrame);
		#undef SEND
	}
	FOR_END;

	mc->stats.frames->count = 0;
}

static void mmClientSendStatsPackets(	MovementClient* mc,
										Packet* pak)
{
	const MovementClientStatsPacketArray*	packets = &mc->stats.packets->fromClient;
	U32										msCurTime = timeGetTime();
	U32										msDeltaTime = 0;
	
	if(mc->stats.packets->msPreviousSendLocalTime){
		msDeltaTime = msCurTime - mc->stats.packets->msPreviousSendLocalTime;
	}

	mc->stats.packets->msPreviousSendLocalTime = msCurTime;
	
	pktSendBitsAuto(pak, msDeltaTime);

	pktSendBitsAuto(pak, packets->count);
	
	FOR_BEGIN(i, (S32)packets->count);
	{
		const MovementClientStatsPacket* packet = packets->packets + i;

		pktSendBitsAuto(pak, packet->size);
		pktSendBitsAuto(pak, packet->msLocalOffsetFromLastPacket);
		pktSendBitsAuto(pak, packet->msOffsetFromExpectedTime);
		pktSendBits(pak, 1, packet->flags.notMovementPacket);
	}
	FOR_END;

	mc->stats.packets->fromClient.count = 0;
}

static void mmClientSendLogListUpdate(	MovementClient* mc,
										Packet* pak)
{
	PERFINFO_AUTO_START_FUNC();

	readLockU32(&mgState.debug.managersLock);
	{
		mc->netSend.logListSentCount = mgState.debug.changeCount;
		
		EARRAY_CONST_FOREACH_BEGIN(mgState.debug.mmsActive, i, isize);
		{
			MovementManager* mm = mgState.debug.mmsActive[i];

			if(!mm->entityRef){
				continue;
			}

			pktSendBitsAuto(pak, mm->entityRef);
		}
		EARRAY_FOREACH_END;

		pktSendBitsAuto(pak, 0);
	}
	readUnlockU32(&mgState.debug.managersLock);
	
	PERFINFO_AUTO_STOP();
}

void mmClientSendHeaderToClient(MovementClient* mc,
								Packet* pak)
{
	const U32	pcDelta = mgState.fg.netSendToClient.cur.pcDelta;
	const S32	fullUpdate = FALSE_THEN_SET(mc->netSend.flags.updateWasSentPreviously);
	const S32	sendLogListUpdate = mc->netSend.flags.sendLogListUpdates &&
									mc->netSend.logListSentCount != mgState.debug.changeCount;
	const U32	extraFlags =	(mc->netSend.flags.sendFullRotations << 0) |
								(mc->stats.frames ? BIT(1) : 0) |
								(mc->stats.packets ? BIT(2) : 0) |
								(sendLogListUpdate ? BIT(3) : 0);
	const U32	flags = fullUpdate |
						(extraFlags ? BIT(1) : 0);
						
	assert(mc->netSend.frameLastSent != mgState.frameCount);
	mc->netSend.frameLastSent = mgState.frameCount;
	
	if(fullUpdate){
		mc->netSend.flags.isFullUpdate = 1;
	}
		
	if(	fullUpdate ||
		mgState.fg.netSendToClient.cur.pcDelta >= BIT_RANGE(0, 5))
	{
		pktSendBits(pak,
					MM_CLIENT_NET_HEADER_BIT_COUNT,
					flags | (BIT_RANGE(0, 5) << 2));

		pktSendBits(pak, 32, mgState.fg.netSendToClient.cur.pc);
		pktSendBitsAuto(pak, pcDelta);
	}else{
		pktSendBits(pak,
					MM_CLIENT_NET_HEADER_BIT_COUNT,
					flags | (pcDelta << 2));
	}
	
	mmClientSendSyncHeaderToClient(mc, pak, fullUpdate);

	if(extraFlags){
		pktSendBits(pak,
					MM_CLIENT_NET_EXTRA_FLAGS_BIT_COUNT,
					extraFlags);

		if(mc->stats.frames){
			mmClientSendStatsFrames(mc, pak);
		}
		
		if(mc->stats.packets){
			mmClientSendStatsPackets(mc, pak);
		}
		
		if(sendLogListUpdate){
			mmClientSendLogListUpdate(mc, pak);
		}
	}
	
	mmSendAnimBitAndGeometryUpdate(mc, pak);
}

void mmClientSendFooterToClient(MovementClient* mc,
								Packet* pak)
{
	mc->netSend.flags.isFullUpdate = 0;
}

static void mmSendRegisteredAnimBitCombo(	MovementManager* mm,
											const MovementRegisteredAnimBitCombo* combo,
											Packet* pak)
{
	if(!combo){
		mmRegisteredAnimBitComboFind(	&mgState.animBitRegistry,
										&combo,
										NULL,
										NULL);
	}

	pktSendBitsAuto(pak, combo->index);
}

static void mmSendAnimUpdate(	MovementManager* mm,
								const MovementNetOutputEncoded* noe,
								Packet* pak)
{
	const MovementOutput*	o = noe->o;
	U32						prev = 0;

	if(!eaiSize(&o->data.anim.values)){
		pktSendBitsAuto(pak, 0);
		return;
	}

	EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
	{
		switch(MM_ANIM_VALUE_GET_TYPE(o->data.anim.values[i])){
			xcase MAVT_LASTANIM_ANIM:{
				// Skip PC.
				i++;
			}
			xcase MAVT_LASTANIM_FLAG:{
				// Ignored.
			}
			xdefault:{
				if(prev){
					pktSendBitsAuto(pak, 1 | (prev << 1));
				}

				prev = o->data.anim.values[i];
			}
		}
	}
	EARRAY_FOREACH_END;

	assert(prev);
	pktSendBitsAuto(pak, 0 | (prev << 1));
}

S32 mmSendToClientShouldCache(	MovementClient* mc,
								MovementManager* mm,
								S32 fullUpdate)
{
	return	!fullUpdate &&
			(	!mm->fg.flags.isAttachedToClient ||
				mm->fg.mcma->mc != mc);
}

static void mmSendNetOutputEncoded(	const MovementClient* mc,
									MovementManager* mm,
									MovementNetOutputEncoded* noe,
									Packet* pak,
									S32 fullUpdate)
{
	if(!mm->fg.netSend.flags.hasPosUpdate){
		MM_CHECK_STRING_WRITE(pak, "no pos");
	}else{
		MM_CHECK_STRING_WRITE(pak, "pos");
		
		if(mm->fg.netSend.flags.hasNotInterpedOutput){
			pktSendBits(pak, 1, noe->pos.flags.notInterped);
		}

		if(noe->pos.flags.isAbsolute){
			if(noe->pos.flags.isAbsoluteFull){
				// Send the full encoded position.
				
				pktSendBits(pak, MM_NET_OUTPUT_HEADER_BIT_COUNT, 1);
				
				MM_CHECK_STRING_WRITE(pak, "full");

				ARRAY_FOREACH_BEGIN(noe->pos.component, j);
					pktSendBits(pak, 32, noe->pos.component[j].value);
				ARRAY_FOREACH_END;
			}else{
				// Send a small non-offset-relative offset.
				
				pktSendBits(pak,
							MM_NET_OUTPUT_HEADER_BIT_COUNT,
							1 |
								2 |
								(noe->pos.flags.xyzMask << 2));
				
				MM_CHECK_STRING_WRITE(pak, "small");

				if(noe->pos.flags.xyzMask){
					FOR_BEGIN(j, 3);
					{
						if(noe->pos.flags.xyzMask & BIT(j)){
							nprintf("%d: sending encMag[%d]: %s%d\n",
									noe->no->pc.server,
									j,
									noe->pos.component[j].flags.isNegative ? "-" : "",
									noe->pos.component[j].offsetDeltaScale);

							pktSendBits(pak,
										8,
										noe->pos.component[j].flags.isNegative |
											(noe->pos.component[j].offsetDeltaScale << 1));
						}
					}
					FOR_END;
				}
			}
		}else{
			pktSendBits(pak,
						MM_NET_OUTPUT_HEADER_BIT_COUNT,
						0 |
							(noe->pos.flags.xyzMask << 1));

			MM_CHECK_STRING_WRITE(pak, "partial");
			
			FOR_BEGIN(j, 3);
			{
				if(noe->pos.flags.xyzMask & BIT(j)){
					U32 offsetDeltaScale = noe->pos.component[j].offsetDeltaScale;
					U32 sendByteCount = noe->pos.component[j].flags.sendByteCount;
					U32 isNegative = noe->pos.component[j].flags.isNegative;
					
					switch(j){
						xcase 0:START_BIT_COUNT(pak, "x");
						xcase 1:START_BIT_COUNT(pak, "y");
						xcase 2:START_BIT_COUNT(pak, "z");
					}

					pktSendBits(pak,
								8,
								sendByteCount |
									(isNegative ? 4 : 0) |
									((offsetDeltaScale & BIT_RANGE(0, 4)) << 3));
								
					if(sendByteCount > 1){
						pktSendBits(pak,
									8,
									(offsetDeltaScale >> 5) & BIT_RANGE(0, 7));
					}

					STOP_BIT_COUNT(pak);
				}
			}
			FOR_END;
		}
	}
	
	// Send rotation update.
	
	if(!mm->fg.netSend.flags.hasRotFaceUpdate){
		MM_CHECK_STRING_WRITE(pak, "no rotAndFace");
	}else{
		MM_CHECK_STRING_WRITE(pak, "rotAndFace");
		
		if(mc->netSend.flags.sendFullRotations){
			pktSendQuat(pak, noe->rot.rotOrig);
		}
		
		pktSendBits(pak,
					3 + 2,
					noe->rot.flags.pyrMask |
						(noe->pyFace.flags.pyMask << 3));
		
		if(noe->rot.flags.pyrMask){
			FOR_BEGIN(j, 3);
				if(noe->rot.flags.pyrMask & BIT(j)){
					pktSendBits(pak,
								MM_NET_ROTATION_ENCODED_BIT_COUNT + 1,
								(noe->rot.pyr[j] < 0 ? 1 : 0) |
									(abs(noe->rot.pyr[j]) << 1));
				}
			FOR_END;
		}
		
		if(noe->pyFace.flags.pyMask){
			FOR_BEGIN(j, 2);
				if(noe->pyFace.flags.pyMask & BIT(j)){
					pktSendBits(pak,
								MM_NET_ROTATION_ENCODED_BIT_COUNT + 1,
								(noe->pyFace.py[j] < 0 ? 1 : 0) |
									(abs(noe->pyFace.py[j]) << 1));
				}
			FOR_END;
		}
	}

	mmLog(	mm,
			NULL,
			"[net.sync] Sent pos:"
			" c%d/s%d"
			" p(%1.2f, %1.2f, %1.2f)"
			" p[%8.8x, %8.8x, %8.8x]"
			" r(%1.2f, %1.2f, %1.2f, %1.2f)"
			" r[%8.8x, %8.8x, %8.8x, %8.8x]"
			" f(%1.2f, %1.2f)"
			" f[%8.8x, %8.8x]"
			,
			noe->no->pc.client,
			noe->no->pc.server,
			vecParamsXYZ(noe->no->data.pos),
			vecParamsXYZ((S32*)noe->no->data.pos),
			quatParamsXYZW(noe->no->data.rot),
			quatParamsXYZW((S32*)noe->no->data.rot),
			vecParamsXY(noe->no->data.pyFace),
			vecParamsXY((S32*)noe->no->data.pyFace));

	if(!mm->fg.netSend.flags.hasAnimUpdate){
		if(gConf.bNewAnimationSystem){
			MM_CHECK_STRING_WRITE(pak, "no anim");
		}else{
			MM_CHECK_STRING_WRITE(pak, "no animbits");
		}
	}else{
		if(gConf.bNewAnimationSystem){
			MM_CHECK_STRING_WRITE(pak, "anim");
			mmSendAnimUpdate(mm, noe, pak);
		}else{
			MM_CHECK_STRING_WRITE(pak, "animbits");
			mmSendRegisteredAnimBitCombo(	mm,
											noe->no->animBitCombo,
											pak);
		}
	}

	#if MM_NET_VERIFY_DECODED
	{
		pktSendIVec3(pak, noe->pos.debug.encoded.pos);
		pktSendIVec3(pak, noe->pos.debug.encoded.posOffset);
	}
	#endif
}

static void mmSendHeaderToClient(	MovementClient* mc,
									MovementManager* mm,
									Packet* pak,
									S32 fullUpdate,
									S32* sendOutputCountOut)
{
	U32 header = 0;
	S32 sendRareFlags = 0;

	// Build and send the header.

	if(mm->fg.netSend.outputCount != mgState.fg.netSendToClient.cur.normalOutputCount){
		*sendOutputCountOut = 1;
		header |= BIT(0);
	}

	if(fullUpdate){
		sendRareFlags = 1;
	}else{
		if(	mm->fg.netSend.flags.collisionSetDoSend ||
			mm->fg.netSend.flags.collisionGroupDoSend ||
			mm->fg.netSend.flags.collisionGroupBitsDoSend ||
			mm->fg.netSend.flags.noCollisionDoSend ||
			mm->fg.netSend.flags.ignoreActorCreateDoSend || 
			mm->fg.netSend.flags.capsuleOrientationDoSend)
		{
			sendRareFlags = 1;
			header |= BIT(1);
		}
		
		if(mmHasPublicRequesterDataToSend(mm)){
			header |= BIT(6);
		}
	
		if(mm->fg.flags.mmrHasUnsentStates){
			header |= BIT(7);
		}
	}

	if(mm->fg.netSend.flags.hasPosUpdate){
		header |= BIT(2);
	}
	
	if(mm->fg.netSend.flags.hasRotFaceUpdate){
		header |= BIT(3);
	}
	
	if(mm->fg.netSend.flags.hasAnimUpdate){
		header |= BIT(4);
	}

	if(mm->fg.netSend.flags.hasNotInterpedOutput){
		header |= BIT(5);
	}
	
	MM_CHECK_STRING_WRITE(pak, "header");

	assert(header < BIT(MM_NET_HEADER_BIT_COUNT));

	pktSendBits(pak, MM_NET_HEADER_BIT_COUNT, header);
	
	if(sendRareFlags){
		U32 rareFlags = 0;
		
		if(mm->fg.flags.noCollision){
			rareFlags |= BIT(0);
		}

		if(mm->fg.flags.ignoreActorCreate){
			rareFlags |= BIT(1);
		}

		if(mm->fg.flags.capsuleOrientationUseRotation){
			rareFlags |= BIT(2);
		}

		if(fullUpdate){
			rareFlags |= BIT_RANGE(3, 5);
		}else{
			if(mm->fg.netSend.flags.collisionGroupDoSend){
				rareFlags |= BIT(3);
			}

			if(mm->fg.netSend.flags.collisionGroupBitsDoSend){
				rareFlags |= BIT(4);
			}

			if(mm->fg.netSend.flags.collisionSetDoSend){
				rareFlags |= BIT(5);
			}
		}

		MM_CHECK_STRING_WRITE(pak, "rareFlags");

		pktSendBits(pak, MM_NET_RARE_FLAGS_BIT_COUNT, rareFlags);

		if(rareFlags & BIT(3)){
			pktSendU32(pak, mm->fg.collisionGroup);
		}

		if(rareFlags & BIT(4)){
			pktSendU32(pak, mm->fg.collisionGroupBits);
		}

		if(rareFlags & BIT(5)){
			pktSendU32(pak, mm->fg.collisionSet);
		}
	}

	MM_CHECK_STRING_WRITE(pak, "headerEnd");
}

static void mmSendNetOutputInitializerToClient(	MovementManager* mm,
												Packet* pak)
{
	MM_CHECK_STRING_WRITE(pak, "prev");
	
	nprintf("prev: %f, %f, %f\n",
			vecParamsXYZ(mm->fg.net.prev.decoded.pos));
	
	pktSendIVec3(	pak,
					mm->fg.net.prev.encoded.pos);
					
	pktSendIVec3(	pak,
					mm->fg.net.prev.encoded.posOffset);
	
	pktSendIVec3(	pak,
					mm->fg.net.prev.encoded.pyr);

	pktSendIVec2(	pak,
					mm->fg.net.prev.encoded.pyFace);

	if(gConf.bNewAnimationSystem){
		const MovementThreadData*	td = MM_THREADDATA_FG(mm);
		U32*						stanceBits = NULL;
		MovementLastAnim			lastAnim = {0};
		
		eaiStackCreate(&stanceBits, MM_ANIM_VALUE_STACK_SIZE_MODEST);
		mmCopyAnimValueToSizedStack(&stanceBits,
									td->toFG.stanceBits,
									__FUNCTION__);

		eaiStackCreate(&lastAnim.flags, MM_ANIM_VALUE_STACK_SIZE_SMALL);
	
		// Undo all the outputs that will be sent in order to get the initial state.
		
		mmLastAnimCopyLimitFlags(	&lastAnim,
									&td->toFG.lastAnim,
									__FUNCTION__);
		
		FOR_REVERSE_BEGIN(j, mm->fg.netSend.outputCount - 1);
		{
			const MovementNetOutputEncoded*	noe = mm->fg.netSend.outputsEncoded[j];
			const MovementOutput*			o = noe->o;
			S32								foundAnimToStart = 0;

			mmAnimValuesApplyStanceDiff(mm,
										&o->data.anim,
										1,
										&stanceBits,
										__FUNCTION__, 1);

			mmAnimValuesRemoveFromFlags(&o->data.anim,
										&lastAnim.flags);

			EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
			{
				MovementAnimValueType mavType = MM_ANIM_VALUE_GET_TYPE(o->data.anim.values[i]);

				if(mavType == MAVT_LASTANIM_ANIM){
					// Skip PC.
					i++;
				}
				else if(mavType == MAVT_ANIM_TO_START){
					ASSERT_FALSE_AND_SET(foundAnimToStart);
					assert(!eaiSize(&lastAnim.flags));

					mmLastAnimCopyFromValuesLimitFlags(	&lastAnim,
														&o->data.anim,
														__FUNCTION__);
					break;
				}
			}
			EARRAY_FOREACH_END;
		}
		FOR_END;
		
		pktSendBitsAuto(pak, eaiUSize(&stanceBits));
		
		EARRAY_INT_CONST_FOREACH_BEGIN(stanceBits, i, isize);
		{
			pktSendBitsAuto(pak, stanceBits[i]);
		}
		EARRAY_FOREACH_END;
		
		if(!lastAnim.anim){
			pktSendBitsAuto(pak, 0);
		}else{
			pktSendBitsAuto(pak, lastAnim.anim);
			pktSendBitsAuto(pak, lastAnim.pc);
			pktSendBitsAuto(pak, eaiSize(&lastAnim.flags));
			
			EARRAY_INT_CONST_FOREACH_BEGIN(lastAnim.flags, i, isize);
			{
				pktSendBitsAuto(pak, lastAnim.flags[i]);
			}
			EARRAY_FOREACH_END;
		}

		eaiDestroy(&stanceBits);
		eaiDestroy(&lastAnim.flags);
	}else{
		if(!mm->fg.net.prev.animBits.combo){
			pktSendBits(pak, 1, 0);
		}else{
			pktSendBits(pak, 1, 1);
			pktSendBitsAuto(pak, mm->fg.net.prev.animBits.combo->index);
		}
	}
}

static void mmSendRequesters(	MovementManager* mm,
								Packet* pak,
								const S32 fullUpdate,
								const S32 isLocalManager)
{
	if(isLocalManager){
		MM_CHECK_STRING_WRITE(pak, "requestersPrivate");

		mmSendSyncDataToClient(	mm,
								mm->fg.mcma,
								pak,
								fullUpdate);
	}
	else if(fullUpdate ||
			mmHasPublicRequesterDataToSend(mm))
	{
		MM_CHECK_STRING_WRITE(pak, "requestersPublic");

		mmSendPublicSyncDataToClient(	mm,
										pak,
										fullUpdate);
	}else{
		MM_CHECK_STRING_WRITE(pak, "no requesters");
	}
}

static void mmSendOutputs(	const MovementClient* mc,
							MovementManager* mm,
							Packet* pak,
							const S32 fullUpdate,
							const S32 sendOutputCount)
{
	MM_CHECK_STRING_WRITE(pak, "outputs");

	if(sendOutputCount){
		pktSendBitsPack(pak, 1, mm->fg.netSend.outputCount);

		#if MM_CHECK_STRING_ENABLED
		{
			FOR_BEGIN(i, (S32)mm->fg.netSend.outputCount);
				MM_CHECK_STRING_WRITE(pak, "explicitOutput");
			FOR_END;
		}
		#endif
	}else{
		#if MM_CHECK_STRING_ENABLED
		{
			FOR_BEGIN(i, (S32)mm->fg.netSend.outputCount);
			{
				MM_CHECK_STRING_WRITE(pak, "normalOutput");
			}
			EARRAY_FOREACH_END;
		}
		#endif
	}
	
	MM_CHECK_STRING_WRITE(pak, "afterOutputCount");

	if(fullUpdate){
		mmSendNetOutputInitializerToClient(mm, pak);
	}

	FOR_BEGIN(i, (S32)mm->fg.netSend.outputCount);
		MovementNetOutputEncoded* noe = mm->fg.netSend.outputsEncoded[i];

		PERFINFO_AUTO_START("outputs[i]", 1);
			mmSendNetOutputEncoded(mc, mm, noe, pak, fullUpdate);
		PERFINFO_AUTO_STOP();
	FOR_END;
}

static void mmSendResources(MovementManager* mm,
							Packet* pak,
							S32 fullUpdate,
							S32 isLocalManager)
{
	if(	fullUpdate ||
		mm->fg.flags.mmrHasUnsentStates)
	{
		MM_CHECK_STRING_WRITE(pak, "resources");

		mmResourcesSendToClientFG(	mm,
									pak,
									isLocalManager,
									fullUpdate);
	}else{
		MM_CHECK_STRING_WRITE(pak, "not resources");
	}
}

void mmSendToClient(MovementClient* mc,
					MovementManager* mm,
					Packet* pak,
					S32 fullUpdate)
{
	S32 isLocalManager;
	S32 sendOutputCount = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	readLockU32(&mm->fg.netSend.prepareLock);
	
	if(!mm->fg.netSend.flags.prepared){
		readUnlockU32(&mm->fg.netSend.prepareLock);
		mmCreateNetOutputsFG(mm);
		readLockU32(&mm->fg.netSend.prepareLock);
	}
	
	isLocalManager = SAFE_MEMBER(mm->fg.mcma, mc) == mc;

	// Send the header.
	
	mmSendHeaderToClient(	mc,
							mm,
							pak,
							fullUpdate,
							&sendOutputCount);
	
	mmSendOutputs(mc, mm, pak, fullUpdate, sendOutputCount);
	mmSendResources(mm, pak, fullUpdate, isLocalManager);
	mmSendRequesters(mm, pak, fullUpdate, isLocalManager);

	MM_CHECK_STRING_WRITE(pak, "done");
	
	readUnlockU32(&mm->fg.netSend.prepareLock);

	PERFINFO_AUTO_STOP();// FUNC
}

