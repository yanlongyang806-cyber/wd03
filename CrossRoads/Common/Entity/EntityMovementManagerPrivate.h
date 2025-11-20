#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

#include "EntityMovementManager.h"
#include "dynSequencer.h"
#include "ReferenceSystem.h"
#include "Entity.h"
#include "EntitySystemInternal.h"
#include "Capsule.h"
#include "MemAlloc.h"
#include "windefinclude.h"
#include "mutex.h"

// DEBUG FLAGS BEGIN -------------------------------------------------------------------------------
//
// Sending raw decoded data to the client for verification.
#define MM_NET_VERIFY_DECODED						0
//
// Log net encoding to movement log.
#define MM_LOG_NET_ENCODING							0
//
// Track resource removes states through thread handoffs in a really slow thread safe way.
#define MM_TRACK_ALL_RESOURCE_REMOVE_STATES_TOBG	0
//
// Track all mmrs states in a separate table.
#define MM_TRACK_RESOURCE_STATE_FLAGS				0
//
// Use linear small net velocity changes.
#define MM_NET_USE_LINEAR_ENCODING					0
//
// Check that all the flags add up to the past state list count.
#define MM_VERIFY_PAST_STATE_LIST_COUNT				0
//
// Check that the received sync structs are the same as the sent ones.
#define MM_VERIFY_SENT_SYNC_STRUCTS					0
//
// Send whole requester list to client for verification.
#define MM_VERIFY_SENT_REQUESTERS					0
//
// Add extra info for verifying repredicts across threads.
#define MM_VERIFY_REPREDICTS						0
//
// Make sure anim diffs are okay after receive.
#define MM_VERIFY_RECEIVED_ANIM						0
//
// Make sure anim diffs are okay after receive.
#define MM_TRACK_AFTER_SIM_WAKES					1
//
// Verify that the toFG predict state is the same when not copying it.
#define MM_VERIFY_TOFG_PREDICT_STATE				0
//
// Verify that the toFG view status matches the BG status.
#define MM_VERIFY_TOFG_VIEW_STATUS					0
//
// Print animation words to the consol windows when processing them
#define MM_DEBUG_PRINTANIMWORDS						0
//
// DEBUG FLAGS END ---------------------------------------------------------------------------------

#undef MMLOG_IS_ENABLED
#define MMLOG_IS_ENABLED(mm) (mgState.debug.activeLogCount && (mm)->flags.debugging)

#undef MRLOG_IS_ENABLED
#define MRLOG_IS_ENABLED(mr) MMLOG_IS_ENABLED((mr)->mm)

#undef MRMLOG_IS_ENABLED
#define MRMLOG_IS_ENABLED(msg) MMLOG_IS_ENABLED(MR_MSG_TO_PD(msg)->mm)

#undef MMRMLOG_IS_ENABLED
#define MMRMLOG_IS_ENABLED(msg) MMLOG_IS_ENABLED(MMR_MSG_TO_PD(msg)->mm)

#define SECONDS_TO_INTERP_PREDICT_ERRORS (0.25f)

#define MM_BG_PROCESS_COUNT_OFFSET_TO_CUR_VIEW (MM_PROCESS_COUNTS_PER_SECOND / 4)

#define MM_LOG_SECTION_PADDING_BEGIN	"---\\\n"\
										"----------\\\n"\
										"---------------------\\\n"\
										"-----------------------------------\\\n"
#define MM_LOG_SECTION_PADDING_END		"\n"\
										"-----------------------------------/\n"\
										"---------------------/\n"\
										"----------/\n"\
										"---/"

typedef struct CritterFactionMatrix						CritterFactionMatrix;
typedef struct MovementClient							MovementClient;
typedef struct MovementClientInputStep					MovementClientInputStep;
typedef struct MovementOutput							MovementOutput;
typedef struct MovementNetOutput						MovementNetOutput;
typedef struct MovementInputStep						MovementInputStep;
typedef struct MovementInputEvent						MovementInputEvent;
typedef struct MovementDefaultFG						MovementDefaultFG;
typedef struct MovementLogs								MovementLogs;
typedef struct MovementManagedResource					MovementManagedResource;
typedef struct MovementSpace							MovementSpace;
typedef struct MovementManagerGrid						MovementManagerGrid;
typedef struct MovementExecNode							MovementExecNode;
typedef struct MovementExecList							MovementExecList;
typedef struct MovementExecListIter						MovementExecListIter;
typedef struct MovementOverrideValueGroup				MovementOverrideValueGroup;
typedef struct MovementProcessingThread					MovementProcessingThread;

typedef struct WorldColl								WorldColl;
typedef struct WorldCollGridCell						WorldCollGridCell;
typedef struct WorldCollIntegration						WorldCollIntegration;
typedef struct WorldCollIntegrationMsg					WorldCollIntegrationMsg;
typedef struct WorldCollScene							WorldCollScene;
typedef struct WorldCollActor							WorldCollActor;

typedef struct PSDKCookedMesh							PSDKCookedMesh;

typedef struct StashTableImp*							StashTable;
typedef struct DynSkeletonPreUpdateParams				DynSkeletonPreUpdateParams;
typedef struct ManagedThread							ManagedThread;

typedef U32												EntityRef;

#define MM_FG_SLOT										(mgState.fg.threadDataSlot)
#define MM_BG_SLOT										(mgState.bg.threadDataSlot)

#define MM_STATIC_ASSERT(x)								(0?(x),0:1)
#define MM_VERIFY_TYPE(ptype, p)						MM_STATIC_ASSERT((p)==(ptype*)NULL)

#define STATICASSERT_IS_MM(mm)							MM_VERIFY_TYPE(MovementManager, mm)
#define MM_THREADDATA_FG(mm)							(STATICASSERT_IS_MM(mm),((mm)->threadData + MM_FG_SLOT))
#define MM_THREADDATA_BG(mm)							(STATICASSERT_IS_MM(mm),((mm)->threadData + MM_BG_SLOT))

#define STATICASSERT_IS_MR(mr)							MM_VERIFY_TYPE(MovementRequester, mr)
#define MR_USERSTRUCT_TOFG(mr, slot) 					(STATICASSERT_IS_MR(mr),(mr)->userStruct.toFG[slot])
#define MR_USERSTRUCT_TOBG(mr, slot) 					(STATICASSERT_IS_MR(mr),(mr)->userStruct.toBG[slot])

#define MR_THREADDATA_FG(mr)							(STATICASSERT_IS_MR(mr),((mr)->threadData + MM_FG_SLOT))
#define MR_THREADDATA_BG(mr)							(STATICASSERT_IS_MR(mr),((mr)->threadData + MM_BG_SLOT))

#define STATICASSERT_IS_MMR(mmr)						MM_VERIFY_TYPE(MovementManagedResource, mmr)
#define MMR_TOFG_FG(mmr)								(STATICASSERT_IS_MMR(mmr),((mmr)->toFG + MM_FG_SLOT))
#define MMR_TOBG_FG(mmr)								(STATICASSERT_IS_MMR(mmr),((mmr)->toBG + MM_FG_SLOT))
#define MMR_TOFG_BG(mmr)								(STATICASSERT_IS_MMR(mmr),((mmr)->toFG + MM_BG_SLOT))
#define MMR_TOBG_BG(mmr)								(STATICASSERT_IS_MMR(mmr),((mmr)->toBG + MM_BG_SLOT))

#define MM_PARENT_FROM_MEMBER(type, memberName, memberPtr)	(MM_STATIC_ASSERT(memberPtr==&((type*)0)->memberName),((type*)((intptr_t)memberPtr - offsetof(type, memberName))))
#define MRTD_FROM_MEMBER(memberName, memberPtr)				MM_PARENT_FROM_MEMBER(MovementRequesterThreadData, memberName, memberPtr)
#define MR_FROM_MEMBER(memberName, memberPtr)				MM_PARENT_FROM_MEMBER(MovementRequester, memberName, memberPtr)
#define MR_FROM_EN_BG(nodeIndex, iter)						MR_FROM_MEMBER(bg.execNode[nodeIndex], (iter).node)

//if you notice errors about stack overflow at runtime
//either increase these values or simplify the animation graph & stance data
#define MM_ANIM_VALUE_STACK_SIZE_SMALL   10
#define MM_ANIM_VALUE_STACK_SIZE_MODEST  25
#define MM_ANIM_VALUE_STACK_SIZE_MEDIUM  50
#define MM_ANIM_VALUE_STACK_SIZE_LARGE  100

typedef struct MovementInputEvent {
	MovementInputEvent*									next;
	MovementInputEvent*									prev;
	
	U32													msTime;
	MovementInputValue									value;
	char*												commandMutable;
} MovementInputEvent;

typedef struct MovementInputEventList {
	MovementInputEvent*									head;
	MovementInputEvent*									tail;
} MovementInputEventList;

typedef struct MovementInputState {
	S32													bit[MIVI_BIT_COUNT];
	F32													f32[MIVI_F32_COUNT];
} MovementInputState;

typedef struct MovementInputUnsplitQueue {
	struct {
		MovementInputState								lastQueued;
	} values;
	
	struct {
		MovementInputValueIndex							mivi;
		U32												msTime;
		
		struct {
			U32											isSet : 1;
		} flags;
	} lastBit;

	union {
		MovementInputEventList							mieListMutable;
		const MovementInputEventList					mieList;
	};

	struct {
		U32												lastControlUpdate;
	} msTime;
} MovementInputUnsplitQueue;

typedef struct MovementExecNode {
	MovementExecNode*		next;
	MovementExecNode*		prev;
} MovementExecNode;

typedef struct MovementExecList {
	MovementExecListIter*	iters;
	MovementExecNode*		head;
	MovementExecNode*		tail;
} MovementExecList;

typedef struct MovementExecListIter {
	MovementExecListIter*	next;
	MovementExecListIter*	prev;
	MovementExecNode*		node;
	S32						skipNext;
} MovementExecListIter;

#define MEL_ITER_INIT_REAL(iter, mel)(		\
		(iter.next = mel.iters),			\
		(mel.iters = &iter),				\
		(iter.prev = NULL),					\
		(iter.node = mel.head),				\
		(iter.skipNext = 0)					\
	)
#define MEL_ITER_DEINIT_REAL(iter, mel)(	\
		(iter.prev?							\
			(iter.prev->next = iter.next) :	\
			(mel.iters = iter.next)			\
		),									\
		(iter.next?							\
			(iter.next->prev = iter.prev) :	\
			(0)								\
		)									\
	)
#define MEL_ITER_GOTO_NEXT_REAL(iter)		\
	(TRUE_THEN_RESET(iter.skipNext)?0:(iter.node = iter.node->next))
#define MEL_NODE_REMOVED_REAL(mel, nodeRemoved){		\
	MovementExecListIter* iter;							\
	for(iter = mel.iters; iter; iter = iter->next){		\
		if(iter->node == nodeRemoved){					\
			iter->node = iter->node->next;				\
			iter->skipNext = 1;							\
		}												\
	}}
#define MEL_FOREACH_BEGIN_REAL(iter, mel){				\
	MovementExecListIter iter;							\
	for(MEL_ITER_INIT(iter, mel);						\
		iter.node?1:(MEL_ITER_DEINIT(iter, mel),0);		\
		MEL_ITER_GOTO_NEXT(iter))						\
	{FORCED_FOREACH_BEGIN_SEMICOLON

#define MEL_ITER_INIT(iter, mel)		MEL_ITER_INIT_REAL((iter), (mel))
#define MEL_ITER_DEINIT(iter, mel)		MEL_ITER_DEINIT_REAL((iter), (mel))
#define MEL_ITER_GOTO_NEXT(iter)		MEL_ITER_GOTO_NEXT_REAL((iter))
#define MEL_NODE_REMOVED(mel, node)		MEL_NODE_REMOVED_REAL((mel), (node))
#define MEL_FOREACH_BEGIN(iter, mel)	MEL_FOREACH_BEGIN_REAL((iter), (mel))
#define MEL_FOREACH_END					}}FORCED_FOREACH_END_SEMICOLON

typedef struct MovementClassPerfInfo {
	PERFINFO_TYPE*										perfInfo;
	char*												name;
} MovementClassPerfInfo;

typedef enum MovementRequesterClassPerfType {
	MRC_PT_COPY_LATEST_FROM_BG,
	MRC_PT_CREATE_TOBG,
	MRC_PT_UPDATED_TOFG,

	MRC_PT_UPDATED_TOBG,
	MRC_PT_UPDATED_SYNC,
	MRC_PT_INPUT_EVENT,
	MRC_PT_BEFORE_DISCUSSION,
	MRC_PT_DISCUSS_DATA_OWNERSHIP,

	MRC_PT_OUTPUT_POSITION_TARGET,
	MRC_PT_OUTPUT_POSITION_CHANGE,
	MRC_PT_OUTPUT_ROTATION_TARGET,
	MRC_PT_OUTPUT_ROTATION_CHANGE,
	MRC_PT_OUTPUT_ANIMATION,

	MRC_PT_OUTPUT_DETAILS,

	MRC_PT_BITS_SENT,
	MRC_PT_BITS_RECEIVED,

	MRC_PT_COPY_STATE_TOFG,

	MRC_PT_COUNT,
} MovementRequesterClassPerfType;

#define MR_PERFINFO_AUTO_START(mr, x)										\
	PERFINFO_AUTO_START_STATIC(	mr->mrc->perfInfo[x].name,					\
								&mr->mrc->perfInfo[x].perfInfo,				\
								1)

#define MR_PERFINFO_AUTO_START_GUARD(mr, x){								\
	PerfInfoGuard* piGuard;													\
	PERFINFO_AUTO_START_STATIC_GUARD(	mr->mrc->perfInfo[x].name,			\
										&mr->mrc->perfInfo[x].perfInfo,		\
										1,									\
										&piGuard)

#define MR_PERFINFO_AUTO_STOP(mr, x)										\
	PERFINFO_AUTO_STOP()

#define MR_PERFINFO_AUTO_STOP_GUARD(mr, x)									\
	PERFINFO_AUTO_STOP_GUARD(&piGuard);}((void)0)

#define MMR_PERFINFO_AUTO_START(mmr, x)										\
	PERFINFO_AUTO_START_STATIC(	mmr->mmrc->perfInfo[x].name,				\
								&mmr->mmrc->perfInfo[x].perfInfo,			\
								1)

#define MMR_PERFINFO_AUTO_START_GUARD(mmr, x){								\
	PerfInfoGuard* piGuard;													\
	PERFINFO_AUTO_START_STATIC_GUARD(	mmr->mmrc->perfInfo[x].name,		\
										&mmr->mmrc->perfInfo[x].perfInfo,	\
										1,									\
										&piGuard)

#define MMR_PERFINFO_AUTO_STOP(mmr, x)										\
	PERFINFO_AUTO_STOP()

#define MMR_PERFINFO_AUTO_STOP_GUARD(mmr, x)								\
	PERFINFO_AUTO_STOP_GUARD(&piGuard);}((void)0)

typedef struct MovementRequesterClass {
	U32													id;
	const char*											name;
	MovementRequesterMsgHandler							msgHandler;
	U32													instanceCount;
	MovementRequesterClassParseTables					pti;
	MovementClassPerfInfo								perfInfo[MRC_PT_COUNT];
	
	struct {
		U32												syncToClient : 1;
	} flags;
} MovementRequesterClass;

typedef struct MovementEntityAttachment {
	EntityRef											erTarget;
} MovementEntityAttachment;

typedef enum MovementManagedResourceClassPerfType {
	MMRC_PT_SET_STATE,
	MMRC_PT_NET_RECEIVE,

	MMRC_PT_COUNT,
} MovementManagedResourceClassPerfType;

typedef struct MovementManagedResourceClass {
	U32													id;
	const char*											name;
	MovementManagedResourceMsgHandler					msgHandler;
	U32													approvingMDC;
	MovementManagedResourceClassParseTables				pti;
	MovementClassPerfInfo								perfInfo[MMRC_PT_COUNT];
} MovementManagedResourceClass;

typedef enum MovementManagedResourceStateType {
	MMRST_CREATED,
	MMRST_STATE_CHANGE,
	MMRST_OUR_DEMO_SYSTEM_SUCKS_FOR_BACKWARD_COMPATIBILITY_SO_I_HAD_TO_CREATE_THIS_ENUM_VALUE,
	MMRST_DESTROYED,
	MMRST_CLEARED,

	MMRST_COUNT,
} MovementManagedResourceStateType;

typedef struct MovementManagedResourceStateFGFlags {
	U32													inList						: 1;
	U32													isNetState					: 1;
	U32													sentToClients				: 1;
	U32													gotRemoveRequestFromBG		: 1;
	U32													sentRemoveRequestToBG		: 1;
} MovementManagedResourceStateFGFlags;

typedef struct MovementManagedResourceStateBGFlags {
	U32													inList						: 1;
	U32													isNetState					: 1;
	U32													createdLocally				: 1;
	U32													sentRemoveRequestToFG		: 1;
	U32													sentFinishedStateToFG		: 1;
} MovementManagedResourceStateBGFlags;

typedef struct MovementManagedResourceState {
	MovementManagedResource*							mmr;
	
	MovementManagedResourceStateType					mmrsType;

	struct {
		void*											state;
	} userStruct;
	
	U32													spc;

	struct {
		struct {
			U32											frameCount;

			struct {
				U32										client;
				U32										server;
				U32										serverSync;
			} pc;
		} received;

		union {
			MovementManagedResourceStateFGFlags			flagsMutable;
			const MovementManagedResourceStateFGFlags	flags;
		};
	} fg;

	struct {
		union {
			MovementManagedResourceStateBGFlags			flagsMutable;
			const MovementManagedResourceStateBGFlags	flags;
		};
	} bg;
} MovementManagedResourceState;

typedef struct MovementManagedResourceToFGFlags {
	U32													updated					: 1;
	U32													destroyed				: 1;
	U32													cleared					: 1;
} MovementManagedResourceToFGFlags;

typedef struct MovementManagedResourceToFG {
	union {
		MovementManagedResourceState**					statesMutable;
		MovementManagedResourceState*const*const		states;
	};
	
	union {
		MovementManagedResourceState**					removeStatesMutable;
		MovementManagedResourceState*const*const		removeStates;
	};
	
	union {
		MovementManagedResourceToFGFlags				flagsMutable;
		const MovementManagedResourceToFGFlags			flags;
	};
} MovementManagedResourceToFG;

typedef struct MovementManagedResourceToBGFlags {
	U32													updated					: 1;
	U32													receivedServerCreate	: 1;
	U32													destroyed				: 1;
	U32													cleared					: 1;
} MovementManagedResourceToBGFlags;

typedef struct MovementManagedResourceToBG {
	U32													newHandle;

	union {
		MovementManagedResourceState**					statesMutable;
		MovementManagedResourceState*const*const		states;
	};
	
	union {
		MovementManagedResourceState**					removeStatesMutable;
		MovementManagedResourceState*const*const		removeStates;
	};
	
	union {
		MovementManagedResourceToBGFlags				flagsMutable;
		const MovementManagedResourceToBGFlags			flags;
	};
} MovementManagedResourceToBG;

typedef struct MovementManagedResourceFGFlags {
	U32													inList				: 1;
	U32													inListBG			: 1;

	U32													didSetState			: 1;
	U32													needsSetState		: 1;

	U32													sentCreate			: 1;
	U32													sentDestroy			: 1;

	U32													sentDestroyToBG		: 1;
	U32													destroyAfterSetState: 1;
	U32													clearAfterSetState	: 1;

	U32													hasNetState			: 1;
	U32													hadLocalState		: 1;
	
	U32													hasUnsentStates		: 1;

	U32													handlesAlwaysDraw	: 1;
	
	U32													waitingForTrigger	: 1;
	
	U32													hasDetailAnim		: 1;
	
	U32													waitingForWake		: 1;
	
	U32													noPredictedDestroy	: 1;
} MovementManagedResourceFGFlags;

typedef struct MovementManagedResourceBGFlags {
	U32													inList				: 1;

	U32													hadLocalState		: 1;
	U32													hasNetState			: 1;

	U32													didSetState			: 1;
	U32													needsSetState		: 1;
	
	U32													noAutoDestroy		: 1;
	
	U32													clearedFromFG		: 1;
	U32													destroyedFromFG		: 1;

	U32													mrDestroyed			: 1;
} MovementManagedResourceBGFlags;

typedef struct MovementManagedResource {
	MovementManagedResourceClass*						mmrc;

	MovementRequester*									mr;
	
	U32													handle;
		
	MovementManagedResourceToFG							toFG[2];
	MovementManagedResourceToBG							toBG[2];

	struct {
		void*											constant;
		void*											constantNP;

		void*											activatedFG;
		void*											activatedBG;
	} userStruct;

	// Struct fg.

	struct {
		U32												spcWake;
		U32												spcNetCreate;

		union {
			MovementManagedResourceState**				statesMutable;
			MovementManagedResourceState*const*const 	states;
		};
		
		union {
			MovementManagedResourceFGFlags				flagsMutable;
			const MovementManagedResourceFGFlags		flags;
		};
	} fg;

	// Struct bg.

	struct {
		U32												handle;

		union {
			MovementManagedResourceState**				statesMutable;
			MovementManagedResourceState*const*const	states;
		};

		union {
			MovementManagedResourceBGFlags				flagsMutable;
			const MovementManagedResourceBGFlags		flags;
		};
	} bg;
} MovementManagedResource;

typedef struct MovementRequesterThreadDataToFGFlags {
	U32													destroyed			: 1;
	U32													removedFromList		: 1;
	U32													hasUserToFG			: 1;
} MovementRequesterThreadDataToFGFlags;

typedef struct MovementRequesterThreadDataToFGPredict {
	#if MM_VERIFY_TOFG_PREDICT_STATE
		struct {
			U32											frameWhenUpdated;
		} debug;
	#endif

	struct {
		void*											bg;
	} userStruct;

	U32													ownedDataClassBits;
	U32													handledMsgs;
} MovementRequesterThreadDataToFGPredict;

typedef struct MovementRequesterThreadDataToFG {
	MovementRequesterThreadDataToFGPredict*				predict;

	union {
		MovementRequesterThreadDataToFGFlags			flagsMutable;
		const MovementRequesterThreadDataToFGFlags		flags;
	};
} MovementRequesterThreadDataToFG;

typedef struct MovementRequesterThreadDataToBGFlags {
	U32													removeFromList			: 1;

	U32													hasUpdate				: 1;
	U32													hasSync					: 1;
	
	U32													hasUserToBG				: 1;
	
	U32													hasOwnedDataClassBits	: 1;
	U32													hasHandledMsgs			: 1;

	U32													createdFromServer		: 1;
	U32													receivedNetHandle		: 1;
} MovementRequesterThreadDataToBGFlags;

typedef struct MovementRequesterThreadDataToBGPredict {
	struct {
		void*											serverBG;
	} userStruct;

	U32													ownedDataClassBits;
	U32													handledMsgs;
} MovementRequesterThreadDataToBGPredict;

typedef struct MovementRequesterThreadDataToBG {
	MovementExecNode									execNode;
	MovementRequesterThreadDataToBGPredict*				predict;

	struct {
		void*											sync;
		void*											syncPublic;
	} userStruct;

	union {
		MovementRequesterThreadDataToBGFlags			flagsMutable;
		const MovementRequesterThreadDataToBGFlags		flags;
	};
} MovementRequesterThreadDataToBG;

typedef struct MovementRequesterThreadData {
	MovementRequesterThreadDataToFG 				toFG;
	MovementRequesterThreadDataToBG 				toBG;
} MovementRequesterThreadData;

typedef struct MovementQueuedSync {
	U32												spc;
	void*											sync;
	void*											syncPublic;
} MovementQueuedSync;

typedef struct MovementRequesterPipeMsg {
	MovementRequesterPipeMsgType					msgType;
	
	union {
		char*										string;
	};
} MovementRequesterPipeMsg;

typedef struct MovementRequesterPipe {
	U32												handle;
	MovementRequester*								mrSource;
	MovementRequester*								mrTarget;
	MovementRequesterPipeMsg**						msgs;
	
	struct {
		U32											destroy : 1;
	} flagsSource;

	struct {
		U32											destroy : 1;
	} flagsTarget;
} MovementRequesterPipe;

typedef struct MovementRequesterFGFlags {
	U32												createdInFG					: 1;
	U32												createdFromServer			: 1;
	U32												destroyedFromServer			: 1;

	U32												inList						: 1;
	U32												inListBG					: 1;

	U32												handleCreateToBG			: 1;

	U32												hasSyncToBG					: 1;
	U32												hasSyncToSend				: 1;
	U32												hasSyncToQueue				: 1;
	U32												needsAfterSync				: 1;

	U32												destroyed					: 1;
	U32												destroying					: 1;
	U32												destroyedFromBG				: 1;
	U32												sentRemoveToBG				: 1;

	U32												sentCreate					: 1;
	U32												sentCreateToOwner			: 1;
	U32												sentDestroy					: 1;

	U32												receivedUpdateJustNow		: 1;
	
	U32												debugSentCreateToClient		: 1;
	U32												debugSentDestroyToClient	: 1;
	
	U32												forcedSimIsEnabled			: 1;
	
	#if MM_VERIFY_SENT_SYNC_STRUCTS
		U32											sentCreateTest				: 1;
	#endif

	#if MM_VERIFY_SENT_REQUESTERS
		U32											didSendCreate				: 1;
		U32											didSendDestroy				: 1;
	#endif
} MovementRequesterFGFlags;

typedef struct MovementRequesterFG {
	U32												netHandle;
	
	struct {
		struct {
			struct {
				void*								bg;
				void*								sync;
				void*								syncPublic;

				#if MM_VERIFY_SENT_SYNC_STRUCTS
					void*							bgTest;
				#endif
			} userStruct;

			U32										ownedDataClassBits;
			U32										handledMsgs;
			
			struct {
				U32									receivedBG					: 1;
			} flags;
		} prev;
	} net;
	
	struct {
		U32											sentToClientSPC;
	} debug;

	union {
		MovementRequesterFGFlags					flagsMutable;
		const MovementRequesterFGFlags				flags;
	};
} MovementRequesterFG;

typedef struct MovementRequesterBGFlags {
	U32												inList						: 1;
	U32												destroyed					: 1;
	
	U32												createdInFG					: 1;
	
	U32												wroteHistory				: 1;
	U32												receivedNetHandle			: 1;

	U32												repredictDidRestoreInput	: 1;
	U32												repredictNotCreatedYet		: 1;
	
	U32												hasNewSync					: 1;
	
	U32												needsPostStepMsg			: 1;

	U32												bgUnchangedSinceCopyToFG	: 1;
	U32												copyToFGNextFrame			: 1;
} MovementRequesterBGFlags;

typedef enum MovementRequesterBGExecNodeType {
	MR_BG_EN_INPUT_EVENT,
	MR_BG_EN_BEFORE_DISCUSSION,
	MR_BG_EN_DISCUSS_DATA_OWNERSHIP,
	MR_BG_EN_CREATE_DETAILS,
	MR_BG_EN_REJECT_OVERRIDE,
	MR_BG_EN_COUNT,
} MovementRequesterBGExecNodeType;

typedef struct MovementRequesterStance {
	MovementRequester*								mr;
	U32												animBitHandle;
	U32												isPredicted	: 1;
} MovementRequesterStance;

typedef struct MovementRequesterHistory {
	struct {
		void*											toBG;
		void*											sync;
		void*											syncPublic;
		
		struct {
			U32											isFirstHistory	: 1;
			U32											hasUserToBG		: 1;
			U32											hasNewSync		: 1;
		} flags;
	} in;

	struct {
		void*											bg;
		void*											localBG;
		U32												ownedDataClassBits;
		U32												handledMsgs;
	} out;

	U32													cpc;
} MovementRequesterHistory;

typedef struct MovementRequesterBGPredict {
	union {
		MovementRequesterHistory**					historyMutable;
		MovementRequesterHistory*const*const		history;
	};
	
	struct {
		struct {
			void*									toBG;
			void*									syncBG;

			struct {
				void*								bg;
			} syncPublic;
		} userStruct;
				
		struct {
			U32										hasNewSync	: 1;
			U32										hasUserToBG	: 1;
		} flags;
	} latestBackup;
} MovementRequesterBGPredict;

typedef struct MovementRequesterBG {
	union {
		MovementQueuedSync**						queuedSyncsMutable;
		MovementQueuedSync*const*const				queuedSyncs;
	};

	union {
		MovementManagedResource**					resourcesMutable;
		MovementManagedResource*const*const			resources;
	};

	union {
		MovementRequesterStance**					stancesMutable;
		MovementRequesterStance*const*const			stances;
	};
	
	union {
		U32											ownedDataClassBitsMutable;
		const U32									ownedDataClassBits;
	};

	union {
		U32											handledMsgsMutable;
		const U32									handledMsgs;
	};

	union {
		MovementRequesterBGPredict*					predictMutable;
		MovementRequesterBGPredict*const			predict;
	};

	MovementExecNode								execNode[MR_BG_EN_COUNT];
	
	U32												betweenSimCountOfLastCreateOutput;
	
	U32												overrideHandlePrev;

	struct {
		U32											destroyed;
	} pc;

	union {
		MovementRequesterBGFlags					flagsMutable;
		const MovementRequesterBGFlags				flags;
	};
} MovementRequesterBG;

typedef struct MovementRequesterUserStruct {
	void*											fg;
	void*											bg;
	void*											localBG;
	void*											toFG[2];
	void*											toBG[2];

	struct {
		void*										fg;
		void*										fgToQueue;
		void*										bg;
	} sync;

	struct {
		void*										fg;
		void*										fgToQueue;
		void*										bg;
	} syncPublic;
} MovementRequesterUserStruct;

typedef struct MovementRequester {
	U32												handle;
	MovementManager*								mm;
	MovementRequesterClass*							mrc;

	U32												pcLocalWhenCreated;

	MovementRequesterThreadData						threadData[2];

	MovementRequesterUserStruct						userStruct;

	MovementRequesterFG								fg;
	
	MovementRequesterBG								bg;
} MovementRequester;

typedef struct MovementRegisteredAnimBit MovementRegisteredAnimBit;

typedef struct MovementRegisteredAnimBit {
	const char*										bitName;
	const char*										keyName;
	U32												index;
	union {
		U32											bitHandleLocal;
		U32											bitHandleLocalNonFlash;
	};
	MovementRegisteredAnimBit*						bitLocal;
	U16												dynBit;
	
	struct {
		U32											foundDynBit			: 1;
		U32											foundLocalHandle	: 1;
		U32											isFlashBit			: 1;
		U32											hasNonFlashHandle	: 1;
	} flags;
} MovementRegisteredAnimBit;

typedef struct MovementRegisteredAnimBitCombo {
	const char*										keyName;
	U32*											bits;
	U32												index;
	U32												calls;
} MovementRegisteredAnimBitCombo;

typedef struct MovementAnimBitRegistry {
	// Bit stuff.
	
	U32												bitLock;
	U32												bitCount;
	StashTable										nameToBit;

	union {
		MovementRegisteredAnimBit**					handleToBitMutable;
		MovementRegisteredAnimBit*const*const		handleToBit;
	};

	// Combo stuff.	
	
	U32												comboLock;
	MovementRegisteredAnimBitCombo*					noBitsCombo;
	MovementRegisteredAnimBitCombo**				allCombos;
	StashTable										namesToCombo;
	
	struct {
		U32											isServerRegistry : 1;
	} flags;
} MovementAnimBitRegistry;

typedef struct MovementAnimValues {
	U32*											values;
} MovementAnimValues;

typedef enum MovementAnimValueType {
	MAVT_STANCE_ON,
	MAVT_STANCE_OFF,
	MAVT_ANIM_TO_START,
	MAVT_FLAG,
	MAVT_DETAIL_ANIM_TO_START,
	MAVT_DETAIL_FLAG,
	MAVT_LASTANIM_ANIM,
	MAVT_LASTANIM_FLAG,

	MAVT_COUNT,
} MovementAnimValueType;

#define MAVT_BIT_COUNT 3
STATIC_ASSERT(BIT(MAVT_BIT_COUNT) >= MAVT_COUNT);

#define MM_ANIM_VALUE(index, type)			(((index) << MAVT_BIT_COUNT) | (type))
#define MM_ANIM_VALUE_GET_INDEX(value)		((value) >> MAVT_BIT_COUNT)
#define MM_ANIM_VALUE_GET_TYPE(value)		((value) & BIT_RANGE(0, MAVT_BIT_COUNT - 1))

#define MM_ANIM_ID_BIT_COUNT 8
#define MM_ANIM_HANDLE_WITH_ID(value, id)	(((value) << MM_ANIM_ID_BIT_COUNT) | (id & BIT_RANGE(0, MM_ANIM_ID_BIT_COUNT-1)))
#define MM_ANIM_HANDLE_WITHOUT_ID(value)	((value) >> MM_ANIM_ID_BIT_COUNT)
#define MM_ANIM_HANDLE_GET_ID(value)		((value) & BIT_RANGE(0, MM_ANIM_ID_BIT_COUNT-1))

typedef struct MovementLastAnim {
	U32												pc;
	U32												anim;
	U32*											flags;
	U32												isLocal : 1;
} MovementLastAnim;

typedef struct MovementOutputData {
	Vec3											pos;
	Quat											rot;
	Vec2											pyFace;
	MovementAnimValues								anim;
} MovementOutputData;

typedef struct MovementOutputList {
	MovementOutput*									head;
	MovementOutput*									tail;
} MovementOutputList;

typedef struct MovementOutputProcessCount {
	U32												client;
	U32												server;
} MovementOutputProcessCount;

typedef struct MovementOutputFlags {
	U32												sentToClients			: 1;
	U32												sentToBG				: 1;

	U32												addedAnimValue			: 1;
	// if OLD_ANIM_SYSTEM
	// {
		U32											animBitsViewed			: 1;
		U32											flashBitsViewed			: 1;
		U32											hasFlashBitsToView		: 1;
	// }ELSE{
		U32											animViewedLocal			: 1;
		U32											animViewedLocalNet		: 1;
		U32											needsAnimReplayLocal	: 1;
	// }
		
	U32												notInterped				: 1;

	U32												posIsPredicted			: 1;
	U32												isPredicted				: 1;
} MovementOutputFlags;

typedef struct MovementOutput {
	union {
		MovementOutput*								nextMutable;
		MovementOutput*const						next;
	};
	
	union {
		MovementOutput*								prevMutable;
		MovementOutput*const						prev;
	};
	
	MovementOutputProcessCount						pc;

	union {
		MovementOutputData							dataMutable;
		const MovementOutputData					data;
	};

	union {
		MovementOutputFlags							flagsMutable;
		const MovementOutputFlags					flags;
	};
} MovementOutput;

typedef struct MovementOutputRepredict {
	MovementOutput*									o;

	U32												frameCount;

	union {
		MovementOutputData							dataMutable;
		const MovementOutputData					data;
	};
	
	struct {
		U32											disableRepredictOffset	: 1;
		U32											notInterped				: 1;
		U32											noAnimBitUpdate			: 1;
	} flags;
} MovementOutputRepredict;

typedef struct MovementPredictedStep {
	MovementOutputProcessCount						pc;
	MovementOutput*									o;

	struct {
		MovementInputStep*							miStep;
		U32											setPosVersion;
		U32*										stanceBits;
		MovementLastAnim							lastAnim;

		struct {
			U32										mrHasNewSync			: 1;
			U32										mrHasUserToBG			: 1;
			U32										needsSetPosVersion		: 1;
		} flags;
	} in;

	struct {
		MovementInputState							miState;
		Vec3										pos;
		Quat										rot;
		Vec2										pyFace;
	} out;
} MovementPredictedStep;

typedef struct MovementNetOutputEncoded {
	MovementNetOutput*								no;
	MovementOutput*									o;
	
	struct {
		struct {
			union {
				U32									offsetDeltaScale;
				S32									value;
			};
			
			struct {
				U32									isNegative				: 1;
				U32									sendByteCount			: 3;
			} flags;				
		} component[3];
		
		#if MM_NET_VERIFY_DECODED
			struct {
				struct {
					IVec3							posOffset;
					IVec3							pos;
				} encoded;
			} debug;
		#endif

		struct {
			U32										xyzMask					: 3;
			U32										noChange				: 1;
			U32										isAbsolute				: 1;
			U32										isAbsoluteFull			: 1;
			U32										notInterped				: 1;
		} flags;
	} pos;

	struct {
		IVec3										pyr;
		Quat										rotOrig;

		struct {
			U32										pyrMask					: 3;
		} flags;
	} rot;
	
	struct {
		IVec2										py;
		
		struct {
			U32										pyMask					: 2;
		} flags;
	} pyFace;
} MovementNetOutputEncoded;

typedef struct MovementNetOutputList {
	MovementNetOutput*								head;
	MovementNetOutput*								tail;
} MovementNetOutputList;

typedef struct MovementNetOutputFlags {
	U32											animBitsViewed			: 1;

	// if OLD_ANIM_SYSTEM
	// {
		U32										flashBitsViewed			: 1;
		U32										hasFlashBitsToView		: 1;
	// }else{
		U32										diffedLastStances		: 1;
	// }

	U32											notInterped				: 1;
} MovementNetOutputFlags;

typedef struct MovementNetOutput {
	MovementNetOutput*								next;
	MovementNetOutput*								prev;
	
	MovementOutputProcessCount						pc;
	
	const MovementRegisteredAnimBitCombo*			animBitCombo;
	
	union {
		MovementOutputData							dataMutable;
		const MovementOutputData					data;
	};

	union {
		MovementNetOutputFlags						flagsMutable;
		const MovementNetOutputFlags				flags;
	};
} MovementNetOutput;

typedef struct MovementInputStepProcessCount {
	U32												client;
	U32												serverSync;
	U32												server;
} MovementInputStepProcessCount;

typedef struct MovementClientInputStep {
	MovementClientInputStep*						next;
	MovementClientInputStep*						prev;

	union {
		MovementInputStep**							miStepsMutable;
		MovementInputStep*const*const				miSteps;
	};
	
	MovementInputStepProcessCount					pc;
	
	struct {
		U32											sentToServer			: 1;
		U32											sentToBG				: 1;
	} flags;
} MovementClientInputStep;

typedef struct MovementClientInputStepList {
	MovementClientInputStep*						head;
	MovementClientInputStep*						tail;
	U32												count;
	U32												unsentCount;
} MovementClientInputStepList;

typedef struct MovementClientThreadData {
	struct {
		struct {
			struct {
				U32									cpc;
				U32									spc;
				U32									forcedStepCount;
			} sync;
			
			struct {
				U32									hasStateBG				: 1;
			} flags;
		} netSend;
	} fg;
} MovementClientThreadData;

typedef struct MovementClientStatsPackets {
	MovementClientStatsPacketArray					fromClient;
	MovementClientStatsPacketArray					fromServer;

	U32												msFirstReceiveLocalTime;
	U32												msPreviousReceiveLocalTime;
	U32												msAccReceiveDeltaTime;

	U32												msPreviousSendLocalTime;
} MovementClientStatsPackets;

typedef struct MovementClientStats {
	U32												spcNext;

	U32												inputBufferSize;
	U32												skipSteps;

	U32												stepAcc;
	U32												minUnsentSteps;
	U32												hadUnsentStepsEachFramePeriodCount;
	U32												spcMinBehind;
	U32												wasBehindPeriodCount;
	U32												lateStepCount;
	U32												forcedStepCount;
	U32												cpcLastSent;
	U32												spcLastSent;
	
	struct {
		U32											hadLeftOverSteps	: 1;
		U32											wasBehind			: 1;
		U32											justSkippedSteps	: 1;
	} flags;
	
	MovementClientStatsFrames*						frames;
	MovementClientStatsPackets*						packets;
} MovementClientStats;

typedef struct MovementClientManagerAssociation {
	MovementClient*									mc;
	MovementManager*								mm;

	MovementInputUnsplitQueue*						inputUnsplitQueue;
	
	struct {
		U32											versionToSend;
		U32											versionReceived;
	} setPos;

	struct {
		U32											forcedSetPos		: 1;
		U32											forcedSetRot		: 1;
		U32											sentToClient		: 1;
		U32											sentStateBG			: 1;
	} flags;
} MovementClientManagerAssociation;

typedef struct MovementClient {
	MovementClientMsgHandler						msgHandler;
	void*											userPointer;

	MovementClientManagerAssociation**				mcmas;
	
	MovementClientThreadData						threadData[2];
	
	U32												packetCount;

	MovementClientStats								stats;
	
	union {
		MovementClientInputStepList					mciStepListMutable;
		const MovementClientInputStepList			mciStepList;
	};

	union {
		U32											mmForcedSimCountMutable;
		U32											mmForcedSimCount;
	};

	struct {
		union {
			MovementClientInputStepList				mciStepListMutable;
			const MovementClientInputStepList		mciStepList;
		};

		union {
			MovementInputStep**						miStepsMutable;
			MovementInputStep*const*const			miSteps;
		};

		union {
			MovementInputEventList					mieListMutable;
			const MovementInputEventList			mieList;
		};
	} available;

	struct {
		U32											nextAnimBit;
		U32											nextAnimBitCombo;
		
		U32											nextGeometry;
		U32											nextBody;

		U32											frameLastSent;
		
		U32											logListSentCount;

		struct {
			struct {
				U32									cpc;
				U32									spc;
				U32									forcedStepCount;
			} cur;
			
			struct {
				U32									cpc;
			} prev;
		} sync;

		struct {
			U32										sendStateBG				: 1;
			
			U32										updateWasSentPreviously	: 1;
			U32										isFullUpdate			: 1;
			U32										updateManagers			: 1;
			
			U32										animAndGeoWereSent		: 1;

			U32										sendFullRotations		: 1;
			
			U32										sendLogListUpdates		: 1;
			
			U32										autoSendStats			: 1;
			U32										sendStatsFrames			: 1;
			U32										sendStatsPacketTiming	: 1;
		} flags;
	} netSend;
} MovementClient;

typedef struct MovementInputStep {
	MovementManager*								mm;
	MovementClientInputStep*						mciStep;

	union {
		MovementInputEventList						mieListMutable;
		const MovementInputEventList				mieList;
	};
	
	MovementInputStepProcessCount					pc;

	struct {
		struct {
			U32										removedFromBG	: 1;
			U32										isForced		: 1;
		} flags;
	} fg;

	struct {
		MovementInputStep*							next;

		struct {
			U32										finished		: 1;
			U32										inRepredict		: 1;
		} flags;
	} bg;
} MovementInputStep;

typedef struct MovementThreadDataToBGFlags {
	U32												hasToBG									: 1;

	U32												hasNewRequesters						: 1;
	U32												hasRepredicts							: 1;

	U32												inUpdatedList							: 1;
	U32												isAttachedToClient						: 1;
	U32												clientWasChanged						: 1;
	U32												destroyed								: 1;
	U32												doRepredict								: 1;
	U32												useNewPos								: 1;
	U32												useNewRot								: 1;
	U32												applySyncNow							: 1;

	U32												hasForcedSet							: 1;
	U32												hasSetPosVersion						: 1;
	U32												hasForcedSetRot							: 1;

	U32												isInactive								: 1;
	U32												isInactiveUpdated						: 1;

	U32												noCollisionChanged						: 1;
	U32												noCollision								: 1;

	U32												capsuleOrientationUseRotation			: 1;
	U32												capsuleOrientationMethodChanged			: 1;
	
	U32												mrHasUserToBG							: 1;
	U32												mrHasUpdate								: 1;

	U32												mmrHasUpdate							: 1;

	U32												userThreadDataHasUpdate					: 1;

	U32												hasUpdatedDeathPrediction				: 1;
	U32												isInDeathPrediction						: 1;
	
} MovementThreadDataToBGFlags;

typedef struct MovementThreadDataToBGRepredict {
	U32												cpc;
	U32												spc;
	U32												forcedStepCount;

	Vec3											pos;
	Quat											rot;
	Vec2											pyFace;

	MovementLastAnim								lastAnim;

	union {
		MovementOutputRepredict**					repredictsMutable;
		MovementOutputRepredict*const*const			repredicts;
	};
} MovementThreadDataToBGRepredict;

typedef struct MovementThreadDataToBG {
	union {
		MovementRequester**							newRequestersMutable;
		MovementRequester*const*const				newRequesters;
	};

	union {
		MovementManagedResource**					updatedResourcesMutable;
		MovementManagedResource*const*const 		updatedResources;
	};

	union {
		MovementInputStep**							miStepsMutable;
		MovementInputStep*const*const				miSteps;
	};

	struct {
		union {
			MovementNetOutputList					outputListMutable;
			const MovementNetOutputList				outputList;
		};
	} net;

	Vec3											newPos;
	Quat											newRot;
	U32												forcedSetCount;

	union {
		U32											setPosVersionMutable;
		const U32									setPosVersion;
	};
	
	MovementExecList								melRequesters;

	MovementThreadDataToBGRepredict*				repredict;
	
	union {
		MovementThreadDataToBGFlags					flagsMutable;
		const MovementThreadDataToBGFlags			flags;
	};
} MovementThreadDataToBG;

typedef struct MovementThreadDataToFGFlags {
	U32												inUpdatedList			: 1;
	U32												destroyed				: 1;
	U32												didProcess				: 1;
	U32												startPredict			: 1;
	U32												afterSimOnce			: 1;

	U32												hasToFG					: 1;
	U32												hasNewRequesters		: 1;
	U32												hasRepredicts			: 1;
	U32												hasFinishedInputSteps	: 1;
	U32												hasForcedSetCount		: 1;

	U32												mrHasUserToFG			: 1;
	U32												mrHasUpdate				: 1;
	
	U32												mmrHasUpdate			: 1;
	
	U32												viewStatusChanged		: 1;
	U32												posIsAtRest				: 1;
	U32												rotIsAtRest				: 1;
	U32												pyFaceIsAtRest			: 1;
	
	U32												userThreadDataHasUpdate	: 1;

	U32												capsuleOrientationUseRotation	: 1;
	U32												capsuleOrientationMethodChanged	: 1;
} MovementThreadDataToFGFlags;

typedef struct MovementThreadDataToFGPredict {
	struct {
		U32											spc;
		U32											spcPrev;
	} repredict;

	U32												pcStart;
} MovementThreadDataToFGPredict;

typedef struct MovementThreadDataToFG {
	union {
		MovementOutputList							outputListMutable;
		const MovementOutputList					outputList;
	};

	union {
		MovementOutput*								lastViewedOutputMutable;
		MovementOutput*const						lastViewedOutput;
	};
	
	union {
		MovementOutputRepredict**					repredictsMutable;
		MovementOutputRepredict*const*const			repredicts;
	};
	
	#if MM_VERIFY_REPREDICTS
		U32*										repredictPCs;
	#endif
	
	union {
		MovementRequester**							newRequestersMutable;
		MovementRequester*const*const				newRequesters;
	};

	union {
		MovementManagedResource**					updatedResourcesMutable;
		MovementManagedResource*const*const			updatedResources;
	};

	union {
		MovementInputStep**							finishedInputStepsMutable;
		MovementInputStep*const*const				finishedInputSteps;
	};

	union {
		MovementManagedResourceState**				finishedResourceStatesMutable;
		MovementManagedResourceState*const*const	finishedResourceStates;
	};

	union {
		U32*										stanceBitsMutable;
		const U32*const								stanceBits;
	};
	
	union {
		MovementLastAnim							lastAnimMutable;
		const MovementLastAnim						lastAnim;
	};

	U32												spcOldestToKeep;
	
	U32												forcedSetCount;

	MovementThreadDataToFGPredict*					predict;

	#if MM_VERIFY_TOFG_VIEW_STATUS
		U32											frameWhenViewStatusChanged;
	#endif

	union {
		MovementThreadDataToFGFlags					flagsMutable;
		const MovementThreadDataToFGFlags			flags;
	};
} MovementThreadDataToFG;

typedef struct MovementThreadData {
	MovementThreadDataToFG 							toFG;
	MovementThreadDataToBG 							toBG;
} MovementThreadData;

typedef struct MovementRequesterMsgPrivateData {
	MovementRequesterMsg							msg;
	MovementManager*								mm;
	MovementRequester*								mr;
	MovementRequesterClass*							mrc;
	MovementRequesterMsgType						msgType;
	
	MovementOutput*									o;

	union {
		union {
			struct {
				MovementRequester*					mr;
				U32									mrClassID;
			} dataReleaseRequested;

			struct {
				U32									dataClassBit;
			} createOutput;
			
			struct {
				MovementRequester*					mrOldOwner;
			} shouldRequestOldData;

			struct {
				U32									dataClassBits;
			} dataWasReleased;
			
			struct {
				U32									handle;
			} initRepredictSimBody;
		} bg;
	} in;
} MovementRequesterMsgPrivateData;

#define MR_MSG_TO_PD(m) (MM_VERIFY_TYPE(MovementRequesterMsg, m),				\
							((MovementRequesterMsgPrivateData*)((U8*)(m) -		\
							OFFSETOF(MovementRequesterMsgPrivateData, msg))))

typedef struct MovementManagedResourceMsgPrivateData {
	MovementManagedResourceMsg						msg;
	MovementManager*								mm;
	MovementManagedResource*						mmr;
	MovementManagedResourceClass*					mmrc;
	MovementManagedResourceMsgType					msgType;
	
	union {
		union {
			struct {
				const DynSkeletonPreUpdateParams*	params;
				
				struct {
					U32								doClearBits : 1;
				} flags;
			} setAnimBits;
		} fg;
	} in;
} MovementManagedResourceMsgPrivateData;

#define MMR_MSG_TO_PD(m) ((MovementManagedResourceMsgPrivateData*)((U8*)(m) - \
							OFFSETOF(MovementManagedResourceMsgPrivateData, msg)))

typedef struct MovementManagerGridCell {
	MovementManagerGrid*							grid;
	IVec3											posGrid;

	U32												lock;
	MovementManager**								managers;
} MovementManagerGridCell;

typedef struct MovementManagerGridEntry {
	Vec3											pos;
	MovementManagerGridCell*						cell;
	U32												cellIndex;
	U32												gridSizeIndex;
} MovementManagerGridEntry;

typedef struct MovementManagerGrid {
	MovementSpace*									space;
	U32												lock;
	StashTable										stCells;
} MovementManagerGrid;

typedef struct MovementOffsetInstance {
	F32												rotationOffset;
} MovementOffsetInstance;

typedef struct MovementDisabledHandle {
	MovementManager*								mm;

	struct {
		const char*									fileName;
		U32											fileLine;
	} owner;
} MovementDisabledHandle;

typedef struct MovementSpace {
	WorldColl*										wc;
	MovementManagerGrid								mmGrids[6];
} MovementSpace;

typedef struct MovementManagerGridSizeGroup {
	F32 cellSize;
	F32 maxBodyRadius;
} MovementManagerGridSizeGroup;

extern MovementManagerGridSizeGroup mmGridSizeGroups[TYPE_ARRAY_SIZE(MovementSpace, mmGrids)];

typedef struct MovementGeometryMesh {
	F32*											verts;
	U32												vertCount;
	U32*											tris;
	U32												triCount;
} MovementGeometryMesh;

typedef struct MovementGeometry {
	U32												index;
	MovementGeometryType							geoType;
	MovementGeometryMesh							mesh;

	struct {
		const char*									fileName;
		const char*									modelName;
		Vec3										scale;
	} model;

	struct {
		PSDKCookedMesh*								triangle;
		PSDKCookedMesh*								convex;
	} cookedMesh;
	
	struct {
		U32											requestedGeoemetryData		: 1;
		U32											triedCookingTriangleMesh	: 1;
		U32											triedCookingConvexMesh		: 1;
	} flags;
} MovementGeometry;

typedef struct MovementBodyPart {
	MovementGeometry*								geo;
	Vec3											pos;
	Vec3											pyr;
} MovementBodyPart;

typedef struct MovementBody {
	U32												index;
	F32												radius;
	MovementBodyPart**								parts;
	Capsule**										capsules;
} MovementBody;

typedef struct MovementBodyDesc {
	MovementBodyPart**								parts;
	Capsule**										capsules;
} MovementBodyDesc;

typedef struct MovementBodyInstance {
	MovementManager*								mm;
	MovementBody*									body;
	WorldCollObject*								wco;
	Vec3											pos;
	Quat											rot;

	struct {
		U32											hasOneWayCollision	: 1;
		U32											isShell				: 1;
		U32											freeOnWCODestroy	: 1;
	} flags;
} MovementBodyInstance;

typedef struct MovementSimBodyInstance {
	MovementManager*								mm;
	MovementRequester*								mr;
	MovementBody*									body;
	WorldCollActor*									wcActor;
	IVec3											posGrid;
	
	struct {
		U32											destroyed						: 1;
	} flags;
} MovementSimBodyInstance;

typedef struct MovementNoCollHandle {
	MovementManager*								mm;
	
	struct {
		const char*									fileName;
		U32											fileLine;
	} owner;
} MovementNoCollHandle;

typedef struct MovementCollSetHandle {
	MovementManager*								mm;

	S32												setID;

	struct {
		const char*									fileName;
		U32											fileLine;
	} owner;
} MovementCollSetHandle;

typedef struct MovementCollGroupHandle {
	MovementManager*								mm;

	U32												groupBit;

	struct {
		const char*									fileName;
		U32											fileLine;
	} owner;
} MovementCollGroupHandle;

typedef struct MovementCollBitsHandle {
	MovementManager*								mm;

	U32												groupBits;

	struct {
		const char*									fileName;
		U32											fileLine;
	} owner;
} MovementCollBitsHandle;

typedef struct MovementOverrideValue {
	MovementOverrideValueGroup*						movg;
	MovementRequester*								mr;
	U32												handle;

	MovementSharedDataType							valueType;
	MovementSharedDataValue							value;
} MovementOverrideValue;

typedef struct MovementOverrideValueGroup {
	const char*										namePooled;

	union {
		MovementOverrideValue**						movsMutable;
		MovementOverrideValue*const*const			movs;
	};
} MovementOverrideValueGroup;

typedef struct MovementManagerFGFlags {
	U32												isAttachedToClient				: 1;

	U32												destroyed						: 1;
	U32												destroyedFromBG					: 1;

	U32												posViewIsAtRest					: 1;
	U32												rotViewIsAtRest					: 1;
	U32												pyFaceViewIsAtRest				: 1;

	U32												posNeedsForcedSetAck			: 1;
	U32												rotNeedsForcedSetAck			: 1;
	
	U32												hasOffsetInstances				: 1;

	U32												ignoreActorCreate				: 1;
	U32												noCollision						: 1;
	U32												hasDisabledHandles				: 1;
	U32												needsNetOutputViewUpdate		: 1;

	U32												didProcessInBG					: 1;

	U32												afterSimOnceFromBGbit			: 1;

	U32												mrHasHandleCreateToBG			: 1;
	U32												mrHasSyncToBG					: 1;
	U32												mrIsNewToSend					: 1;
	U32												mrHasSyncToSend					: 1;
	U32												mrHasDestroyToSend				: 1;
	U32												mrHasSyncToQueue				: 1;
	U32												mrNeedsAfterSync				: 1;
	U32												mrNeedsDestroy					: 1;
	U32												mrNeedsAfterSend				: 1;

	U32												trash/*mmrNeedsSetState*/		: 1;
	U32												mmrHandlesAlwaysDraw			: 1;
	U32												mmrHasDetailAnim				: 1;
	U32												mmrHasUnsentStates				: 1;
	U32												mmrWaitingForWake				: 1;
	U32												mmrWaitingForTrigger			: 1;
	
	U32												hasSetView						: 1;
	
	U32												sentUserThreadDataUpdateToBGbit	: 1;
	
	U32												needsAnimReplayLocal			: 1;
	U32												needsAnimReplayLocalNet			: 1;
	U32												isInDeathPrediction				: 1;

	U32												capsuleOrientationUseRotation	: 1;

	U32												mmrNeedsSetState				: 1;

	U32												afterSimOnceFromBG;
	U32												sentUserThreadDataUpdateToBG;
} MovementManagerFGFlags;

typedef struct MovementManagerFGViewFlags {
	U32												needsReset						: 1;
	U32												appliedViewLocal				: 1;
	U32												appliedViewNet					: 1;
	U32												lastAnimIsLocal					: 1;
	U32												localAnimIsPredicted			: 1;
} MovementManagerFGViewFlags;

typedef struct MovementManagerFGView {
	union {
		MovementAnimValues							animValuesMutable;
		const MovementAnimValues					animValues;
	};

	struct {
		union {
			U32*									stanceBitsMutable;
			const U32*const							stanceBits;
		};

		union {
			MovementLastAnim						lastAnimMutable;
			const MovementLastAnim					lastAnim;
		};

		union {
			U32*									spcPerFlagMutable;
			const U32*const							spcPerFlag;
		};
	} local;
	
	struct {
		union {
			U32*									stanceBitsMutable;
			const U32*const							stanceBits;
		};
	} localNet;

	struct {
		union {
			U32*									stanceBitsMutable;
			const U32*const							stanceBits;
		};

		union {
			MovementLastAnim						lastAnimMutable;
			const MovementLastAnim					lastAnim;
		};
	} net;
	
	struct {
		union {
			U32*									stanceBitsMutable;
			const U32*const							stanceBits;
		};
	} netUsed;
	
	union {
		MovementLastAnim							lastAnimMutable;
		const MovementLastAnim						lastAnim;
	};

	union {
		MovementManagerFGViewFlags					flagsMutable;
		const MovementManagerFGViewFlags			flags;
	};
} MovementManagerFGView;

typedef struct MovementManagerFlags {
	U32												destroying						: 1;
	
	U32												debugging						: 1;
	U32												writeLogFiles					: 1;
	
	U32												isLocal							: 1;
} MovementManagerFlags;

typedef struct MovementManagerFG {
	union {
		MovementBodyInstance**						bodyInstancesMutable;
		MovementBodyInstance*const*const			bodyInstances;
	};
	
	union {
		F32											bodyRadiusMutable;
		F32											bodyRadius;
	};

	U32												spcWakeResource;
	U32												mrForcedSimCount;

	struct {
		U32											count;
		U32											index;

		#if MM_TRACK_AFTER_SIM_WAKES
			StashTable								stReasons;
		#endif
	} afterSimWakes;

	union {
		MovementClientManagerAssociation*			mcmaMutable;
		MovementClientManagerAssociation*const		mcma;
	};

	U32												frameWhenViewChanged;
	U32												frameWhenViewSet;

	union {
		Vec3										posMutable;
		const Vec3									pos;
	};

	union {
		Quat										rotMutable;
		const Quat									rot;
	};

	union {
		Vec2										pyFaceMutable;
		const Vec2									pyFace;
	};
	
	struct {
		U32											shared;
		U32											pos;
		U32											rot;
	} forcedSetCount;

	union {
		MovementDisabledHandle**					disabledHandlesMutable;
		MovementDisabledHandle*const*const			disabledHandles;
	};
	
	union {
		MovementNoCollHandle**						noCollHandlesMutable;
		MovementNoCollHandle*const*const			noCollHandles;
	};

	union {
		MovementCollSetHandle**						collisionSetHandlesMutable;
		MovementCollSetHandle*const*const			collisionSetHandles;
	};

	union {
		MovementCollGroupHandle**					collisionGroupHandlesMutable;
		MovementCollGroupHandle*const*const			collisionGroupHandles;
	};

	union {
		MovementCollBitsHandle**					collisionGroupBitsHandlesMutable;
		MovementCollBitsHandle*const*const			collisionGroupBitsHandles;
	};

	union {
		MovementRequester**							requestersMutable;
		MovementRequester*const*const				requesters;
	};

	union {
		MovementManagedResource**					resourcesMutable;
		MovementManagedResource*const*const			resources;
	};
	
	union {
		MovementOffsetInstance**					offsetInstancesMutable;
		MovementOffsetInstance*const*const			offsetInstances;
	};

	struct {
		U32											pcStart;
	} predict;

	MovementManagerFGView*							view;

	// Struct "net".

	struct {
		struct {
			Quat									rot;
			Vec2									pyFace;
		} preEncoded;
		
		struct {
			struct {
				IVec3								pos;
				IVec3								posOffset;
				IVec3								pyr;
				IVec2								pyFace;
			} encoded;

			struct {
				Vec3								pos;

				Quat								rot;
				Vec3								pyr;
				Vec2								pyFace;
			} decoded;

			struct {
				const MovementRegisteredAnimBitCombo* combo;
			} animBits;

			struct {
				U32									xyzMask;
			} absoluteSmall;

			struct {
				U32									isAbsoluteSmall : 1;
			} flags;
		} prev, cur;

		struct {
			Vec3									pos;
			Quat									rot;
			Vec2									pyFace;
		} sync;
		
		union {
			MovementNetOutputList					outputListMutable;
			const MovementNetOutputList				outputList;
		};

		struct {
			union {
				MovementNetOutputList				outputListMutable;
				const MovementNetOutputList			outputList;
			};
		} available;
		
		union {
			U32*									stanceBitsMutable;
			const U32*const							stanceBits;
		};
		
		union {
			MovementLastAnim						lastAnimMutable;
			const MovementLastAnim					lastAnim;
		};

		struct {
			union {
				U32*								stanceBitsMutable;
				const U32*const						stanceBits;
			};
		
			union {
				MovementLastAnim					lastAnimMutable;
				const MovementLastAnim				lastAnim;
			};
		} lastStored;
		
		union {
			S32										collisionSetMutable;
			const S32								collisionSet;
		};

		union {
			U32										collisionGroupMutable;
			const U32								collisionGroup;
		};

		union {
			U32										collisionGroupBitsMutable;
			const U32								collisionGroupBits;
		};
		
		struct {
			U32										noCollision					: 1;
			U32										lastAnimUpdateWasNotStored	: 1;
		} flags;

		#if MM_VERIFY_SENT_REQUESTERS
		struct {
			char*									estrLog;
		} verifySent;
		#endif
	} net;

	// Struct "netSend".

	struct {
		U32											prepareLock;

		union {
			MovementNetOutputEncoded**				outputsEncodedMutable;
			MovementNetOutputEncoded*const*const	outputsEncoded;
		};

		U32											outputCount;

		U32											collisionSetSent;
		U32											collisionGroupSent;
		U32											collisionGroupBitsSent;

		struct {
			U32										prepared						: 1;

			U32										noCollisionSent					: 1;
			U32										ignoreActorCreateSent			: 1;

			U32										noCollisionDoSend				: 1;
			U32										collisionSetDoSend				: 1;
			U32										collisionGroupDoSend			: 1;
			U32										collisionGroupBitsDoSend		: 1;
			U32										ignoreActorCreateDoSend			: 1;

			U32										hasNotInterpedOutput			: 1;

			U32										hasPosUpdate					: 1;
			U32										hasRotFaceUpdate				: 1;
			U32										hasAnimUpdate					: 1;
			U32										capsuleOrientationUseRotation	: 1;
			U32										capsuleOrientationDoSend		: 1;
		} flags;
	} netSend;

	// Struct "repredict".

	struct {
		Vec3										offset;
		F32											secondsRemaining;

		Vec3										curOffset;
	} repredict;
	
	struct {
		U32											frameWhenDestroyed;
	} debug;

	union {
		S32											collisionSetMutable;
		const S32									collisionSet;
	};
	
	union {
		U32											collisionGroupMutable;
		const U32									collisionGroup;
	};

	union {
		U32											collisionGroupBitsMutable;
		const U32									collisionGroupBits;
	};

	union {
		U32											factionIndexMutable;
		const U32									factionIndex;
	};

	union {
		MovementManagerFGFlags						flagsMutable;
		const MovementManagerFGFlags				flags;
	};
} MovementManagerFG;

typedef struct MovementManagerBGFlags {
	U32												inList								: 1;
	U32												destroyed							: 1;
	U32												didProcess							: 1;
	U32												isAttachedToClient					: 1;
	U32												hasPredictedSteps					: 1;
	U32												isPredicting						: 1;
	U32												doNotRestartPrediction				: 1;
	
	U32												isInactive							: 1;
	
	U32												sendForcedSetCountToFG				: 1;
	U32												sendForcedRotMsg					: 1;
	U32												needsSetPosVersion					: 1;
	U32												resetOnNextInputStep				: 1;

	U32												inPostStepList						: 1;

	U32												mrWasDestroyedOnThisStep			: 1;
	U32												mrHasNewSync						: 1;
	U32												mrHasQueuedSync						: 1;
	U32												mrHasHistory						: 1;
	U32												mrNeedsPostStepMsg					: 1;

	U32												mrHandlesMsgBeforeDiscussion		: 1;
	U32												mrHandlesMsgDiscussDataOwnership	: 1;
	U32												mrHandlesMsgCreateDetails			: 1;
	U32												mrHandlesMsgRejectOverride			: 1;
	U32												mrHandlesCollidedEntMsg				: 1;
	U32												mrIgnoresCollisionWithEnts			: 1;


	U32												mmrNeedsSetState					: 1;
	U32												mmrIsDestroyedFromFG				: 1;
	
	U32												mrPipeNeedsPostStep					: 1;
	
	U32												sendViewStatusChanged				: 1;

	U32												sentUserThreadDataUpdateToFG		: 1;

	U32												hasPostStepDestroy					: 1;
	
	U32												inSetPastStateList					: 1;
	U32												inSetPastStateChangedList			: 1;
	
	U32												hasChangedOutputDataRecently		: 1;
	
	U32												noCollision							: 1;
	
	U32												hasKinematicBody					: 1;
	
	U32												animStancesChanged					: 1;
	U32												animToStartIsSet					: 1;
	U32												animFlagIsSet						: 1;
	U32												animOwnershipWasReleased			: 1;

	U32												viewChanged							: 1;
	U32												posIsAtRest							: 1;
	U32												rotIsAtRest							: 1;
	U32												pyFaceIsAtRest						: 1;

	U32												mrIsFlying							: 1;
	U32												isInDeathPrediction					: 1;
		
	// if set the 'movement' rotation will be used to orient the capsule instead of the facing
	U32												capsuleOrientationUseRotation		: 1;

} MovementManagerBGFlags;

typedef struct MovementManagerBGNextFrameFlags {
	U32												sendStanceBitsToFG					: 1;
	U32												sendLastAnimToFG					: 1;
} MovementManagerBGNextFrameFlags;

typedef struct MovementManagerBGNextFrame {
	union {
		MovementManagerBGNextFrameFlags				flagsMutable;
		const MovementManagerBGNextFrameFlags		flags;
	};
} MovementManagerBGNextFrame;

typedef enum MovementManagerBGExecListType {
	MM_BG_EL_INPUT_EVENT,
	MM_BG_EL_BEFORE_DISCUSSION,
	MM_BG_EL_DISCUSS_DATA_OWNERSHIP,
	MM_BG_EL_CREATE_DETAILS,
	MM_BG_EL_REJECT_OVERRIDE,
	MM_BG_EL_COUNT,
} MovementManagerBGExecListType;

typedef enum MovementDataClass {
	MDC_POSITION_TARGET,
	MDC_POSITION_CHANGE,
	MDC_ROTATION_TARGET,
	MDC_ROTATION_CHANGE,
	MDC_ANIMATION,

	// Total count.

	MDC_COUNT,
} MovementDataClass;

#define VERIFY_MDC(x) STATIC_ASSERT(BIT(MDC_##x) == MDC_BIT_##x);
VERIFY_MDC(POSITION_TARGET);
VERIFY_MDC(POSITION_CHANGE);
VERIFY_MDC(ROTATION_TARGET);
VERIFY_MDC(ROTATION_CHANGE);
VERIFY_MDC(ANIMATION);
#undef VERIFY_MDC

typedef struct MovementManagerBGRepredict {
	U32												spcPrev;
	U32												cpcPrev;
} MovementManagerBGRepredict;

typedef struct MovementManagerBG {
	union {
		U32											listIndexMutable;
		const U32									listIndex;
	};

	union {
		MovementBodyInstance**						bodyInstancesMutable;
		MovementBodyInstance*const*const			bodyInstances;
	};
	
	union {
		F32											bodyRadiusMutable;
		const F32									bodyRadius;
	};
	
	union {
		MovementSimBodyInstance**					simBodyInstancesMutable;
		MovementSimBodyInstance*const*const			simBodyInstances;
	};

	union {
		MovementManagerGridEntry					gridEntryMutable;
		const MovementManagerGridEntry				gridEntry;
	};

	union {
		MovementOutputList							outputListMutable;
		const MovementOutputList					outputList;
	};
	
	union {
		MovementPredictedStep**						predictedStepsMutable;
		MovementPredictedStep*const*const			predictedSteps;
	};
	
	union {
		MovementRequesterPipe**						pipesMutable;
		MovementRequesterPipe*const*const			pipes;
	};
	
	U32												prevPipeHandle;
	
	union {
		MovementRequesterStance**					stancesMutable;
		MovementRequesterStance*const*const			stances;
	};
	
	union {
		U32*										stanceBitsMutable;
		const U32*const								stanceBits;
	};

	union {
		MovementLastAnim							lastAnimMutable;
		const MovementLastAnim						lastAnim;
	};

	MovementExecNode								execNodePostStep;

	StashTable										stOverrideValues;

	struct {
		union {
			MovementOutputList						outputListMutable;
			const MovementOutputList				outputList;
		};

		union {
			U32**									animValuesMutable;
			U32*const*const							animValues;
		};

		union {
			MovementOutputRepredict**				repredictsMutable;
			MovementOutputRepredict*const*const		repredicts;
		};
	} available;
	
	union {
		Vec3										posMutable;
		const Vec3									pos;
	};

	union {
		Quat										rotMutable;
		const Quat									rot;
	};
	
	union {
		Vec2										pyFaceMutable;
		const Vec2									pyFace;
	};
	
	U32												forcedSetCount;
	
	union {
		U32											setPosVersionMutable;
		const U32									setPosVersion;
	};

	struct {
		union {
			Vec3									posMutable;
			const Vec3								pos;
		};

		union {
			Quat									rotMutable;
			const Quat								rot;
		};
		
		union {
			Vec2									pyFaceMutable;
			const Vec2								pyFace;
		};
	} past;
	
	// "additionalVel" is used for knockback & repel.

	struct {
		Vec3										vel;
		
		struct {
			U32										isRepel			: 1;
			U32										resetBGVel		: 1;
			U32										isSet			: 1;
		} flags;
	} additionalVel;


	struct {
		Vec3										vel;

		U32											isSet			: 1;
	} constantVel;

	struct {
		MovementPositionTarget						pos;
		MovementRotationTarget						rot;
		F32											minSpeed;
	} target;

	union {
		MovementManagedResource**					resourcesMutable;
		MovementManagedResource*const*const			resources;
	};

	union {
		MovementRequester**							requestersMutable;
		MovementRequester*const*const				requesters;
	};

	union {
		MovementRequester*							dataOwnerMutable[MDC_COUNT];
		MovementRequester*const						dataOwner[MDC_COUNT];
	};
	
	union {
		U32											dataOwnerEnabledMaskMutable;
		const U32									dataOwnerEnabledMask;
	};
	
	MovementExecList								mel[MM_BG_EL_COUNT];

	union {
		U32											predictStepsRemainingMutable;
		const U32									predictStepsRemaining;
	};

	MovementInputState*								miState;

	MovementManagerBGRepredict*						repredict;
	
	struct {
		struct {
			U32										mrHasNewSync		: 1;
		} flags;
	} latestBackup;
	
	struct {
		U32											inListCount;
		U32											listIndex;
		U32											spcNewestChange;
	} setPastState;

	MovementManagerBGNextFrame						nextFrame[2];

	union {
		MovementManagerBGFlags						flagsMutable;
		const MovementManagerBGFlags				flags;
	};
} MovementManagerBG;

typedef struct MovementManager {
	// My owning entity.

	EntityRef										entityRef;
	
	// Temporarily here, until I figure out how to thread/network it.
	
	MovementSpace*									space;
	
	// Simple lock for doing spinlocks when allocating rare things.
	
	U32												rareLock;

	// Stuff owned by me.

	MovementRequester**								allRequesters;
	U32												lastRequesterHandle;
	
	// Global resource list.
	
	union {
		MovementManagedResource**					allResourcesMutable;
		MovementManagedResource*const*const			allResources;
	};
	U32												lastResourceHandle;
	
	// Threading data.

	MovementThreadData								threadData[2];

	// Log data.

	MovementLogs*									logs;
	char*											lastSetPosInfoString;

	// Struct fg.

	MovementManagerFG								fg;

	// Struct bg.

	MovementManagerBG								bg;

	// Stuff that should be near flags.

	MovementManagerMsgHandler						msgHandler;

	union {
		void*										userPointer;
		Entity*										entityProbably;
	};
	
	void*											userThreadData[2];

	// Flags.

	union {
		MovementManagerFlags						flagsMutable;
		const MovementManagerFlags					flags;
	};
} MovementManager;

typedef struct MovementManagerMsgPrivateData {
	MovementManagerMsg		msg;
	MovementManager*		mm;
	MovementManagerMsgType	msgType;
} MovementManagerMsgPrivateData;

#define MM_MSG_TO_PD(m) ((MovementManagerMsgPrivateData*)((U8*)(m) - \
							OFFSETOF(MovementManagerMsgPrivateData, msg)))

typedef struct MovementPerEntityTimers {
	U8												isOpen[3][MAX_ENTITIES_PRIVATE];
	PERFINFO_TYPE*									perfInfo[4][MAX_ENTITIES_PRIVATE];
	char*											name[4][MAX_ENTITIES_PRIVATE];
} MovementPerEntityTimers;

extern U32 mmTimerGroupSizes[TYPE_ARRAY_SIZE(MovementPerEntityTimers, isOpen)];

typedef struct MovementGlobalStateFGFlags {
	U32												classesFinalized						: 1;

	U32												notThreaded								: 1;

	U32												mmNeedsDestroy							: 1;

	U32												logSkeletons							: 1;

	U32												doProcessIfValidStepInit				: 1;
	U32												predictDisabled							: 1;

	U32												sendNetReceiveToBG						: 1;

	U32												noSyncWithServer						: 1;
	
	U32												noDisable								: 1;
	
	U32												needsBodyRefresh						: 1;
	
	U32												alwaysSetCurrentView					: 1;
	
	U32												forceSimEnabled							: 1;

	U32												managersAfterSimWakesLocked				: 1;
	U32												managersAfterSimWakesClientChanged		: 1;
	U32												managersAfterSimWakesNonClientChanged	: 1;
} MovementGlobalStateFGFlags;

typedef struct MovementGlobalStateFGNetReceive {
	U32												msTimeConnected;

	MovementAnimBitRegistry							animBitRegistry;

	S32*											managerIndexes;
	
	struct {
		U32											normalOutputCount;
		U32											forcedStepCount;

		struct {
			U32										client;
			U32										server;
			U32										serverSync;
			U32										serverDelta;
		} pc;

		struct {
			S32										clientToServerSync;
			U32										serverSyncToServer;
		} offset;
	} cur, prev, prevPrev;
	
	struct {
		U32											spc;
	} sync;

	struct {
		S32											clientToServerSync[100];
		U32											serverSyncToServer[100];
	} history;
	
	struct {
		U32											msTimeStarted;
		U32											changeCount;
		
		struct {
			U32										collectingData			: 1;
		} flags;
	} autoDebug;

	struct {
		U32											hasStateBG				: 1;
		U32											setSyncProcessCount		: 1;
		U32											receiveFullRotations	: 1;
		U32											fullUpdate				: 1;
	} flags;
} MovementGlobalStateFGNetReceive;

typedef struct MovementGlobalStateFGNetView {
	U32												spcCeiling;
	U32												spcFloor;
	F32												spcInterpFloorToCeiling;
	
	struct {
		F32											normal;
		F32											fast;
		F32											total;
		
		F32											lag;
		F32											skip;
	} spcOffsetFromEnd;
} MovementGlobalStateFGNetView;

typedef struct MovementGlobalStateFGLocalView {
	struct {
		F32											forward;
		F32											inverse;
	} outputInterp;

	U32												pcCeiling;
	U32												spcCeiling;
} MovementGlobalStateFGLocalView;

typedef struct MovementGlobalStateFG {
	U32												threadID;

	union {
		U32											threadDataSlotMutable;
		const U32									threadDataSlot;
	};
	
	MovementClient**								clients;
	MovementClient									mc;
	
	MovementManager**								alwaysDrawManagers;

	union {
		MovementManager**							managersMutable;
		MovementManager*const*const					managers;
	};
	
	struct {
		CrypticalSection							cs;

		union {
			MovementManager**						clientMutable;
			MovementManager*const*const				client;
		};

		union {
			MovementManager**						nonClientMutable;
			MovementManager*const*const				nonClient;
		};
	} managersAfterSimWakes;

	union {
		MovementManager**							managersToDestroyMutable;
		MovementManager*const*const					managersToDestroy;
	};

	union {
		MovementGeometry**							geosMutable;
		MovementGeometry*const*const				geos;
	};
	
	union {
		MovementSpace**								spacesMutable;
		MovementSpace*const*const					spaces;
	};

	union {
		MovementBodyInstance**						bodyInstancesMutable;
		MovementBodyInstance*const*const			bodyInstances;
	};
	
	union {
		MovementGlobalStateFGNetReceive				netReceiveMutable;
		const MovementGlobalStateFGNetReceive		netReceive;
	};

	const WorldCollIntegrationMsg*					wciMsg;

	struct {
		struct {
			U32										pcCatchup;
			U32										pcStart;
			U32										pcNetSend;

			U32										stepCount;
			U32										frameIndex;

			U32										spcOldestToKeep;
			
			F32										prevSecondsDelta;
			F32										secondsDelta;
			U32										pcPrev;
			U32										deltaProcesses;
			F32										prevProcessRatio;
		} cur, prev, next;
	} frame;

	struct {
		struct {
			U32										pc;
			U32										pcDelta;
			U32										normalOutputCount;
		} cur, prev;
	} netSendToClient;
	
	union {
		MovementGlobalStateFGNetView				netViewMutable;
		const MovementGlobalStateFGNetView			netView;
	};
	
	union {
		MovementGlobalStateFGLocalView				localViewMutable;
		const MovementGlobalStateFGLocalView		localView;
	};

	union {
		MovementGlobalStateFGFlags					flagsMutable;
		const MovementGlobalStateFGFlags			flags;
	};
} MovementGlobalStateFG;

typedef struct MovementGlobalStateBGFlags {
	U32												threadIsBG					: 1;
	U32												doRunCurrentStep			: 1;
	U32												doProcessIfValidStep		: 1;
	U32												isLastStepOnThisFrame		: 1;
	U32												isRepredicting				: 1;
	U32												isCatchingUp				: 1;
	U32												gridIsWritable				: 1;
} MovementGlobalStateBGFlags;

typedef void (*MovementProcessingThreadCB)(	MovementProcessingThread* t,
											void* thing);

typedef struct MovementProcessingThreadResults {
	union {
		MovementManager**							managersAfterSimWakesMutable;
		MovementManager*const*const					managersAfterSimWakes;
	};
} MovementProcessingThreadResults;

typedef struct MovementProcessingThread {
	ManagedThread*									mt;
	U32												threadID;

	union {
		MovementManager*							mmMutable;
		MovementManager*const 						mm;
	};

	struct {
		HANDLE										hEvent;
		U32											msTimeEventSet;
	} frameStart, frameDone;
	
	void*const*										things;
	U32												thingCount;
	MovementProcessingThreadCB						cb;

	MovementProcessingThreadResults*				results;

	struct {
		U32											killThread : 1;
	} flags;
} MovementProcessingThread;

typedef struct MovementGlobalStateBG {
	union {
		U32											threadDataSlotMutable;
		const U32									threadDataSlot;
	};

	struct {
		union {
			MovementManager**						clientMutable;
			MovementManager*const*const				client;
		};

		union {
			MovementManager**						nonClientMutable;
			MovementManager*const*const				nonClient;
		};
	} managers;

	struct {
		CrypticalSection							cs;

		union {
			MovementManager**						managersMutable;
			MovementManager*const*const				managers;
		};
		
		struct {
			U32										managersIsReadOnly : 1;
		} flags;
	} hasPostStepDestroy;
	
	struct {
		CrypticalSection							cs;

		MovementExecList							melManagers;
	} needsPostStep;

	struct {
		CrypticalSection							cs;

		union {
			MovementManager**						managersMutable;
			MovementManager*const*const				managers;
		};
		
		union {
			MovementManager**						managersChangedMutable;
			MovementManager*const*const				managersChanged;
		};

		struct {
			U32										isNotWritable : 1;
		} flags;
	} setPastState;

	union {
		MovementSimBodyInstance**					simBodyInstancesMutable;
		MovementSimBodyInstance*const*const			simBodyInstances;
	};

	MovementManager*								entIndexToManager[MAX_ENTITIES_PRIVATE];
	StashTable										stEntIndexToManager;
	
	const WorldCollIntegrationMsg*					wciMsg;

	MovementRequester**								mrsThatNeedSimBodyCreate;

	struct {
		const char*									forcedModule;
	} log;

	struct {
		MovementProcessingThread**					threads;
		S32											desiredThreadCount;
		S32											curThingIndexShared;

		struct {
			U32										processingThread;
		} tls;
	} threads;

	struct {
		struct {
			struct {
				U32									cur;
				U32									total;
			} stepCount;

			U32										pcStart;
		} cur, prev;
		
		struct {
			U32										pcStart;
		} next;
	} frame;

	struct {
		struct {
			U32										cur;
			U32										sync;
			U32										oldestToKeep;
		} local;

		struct {
			U32										cur;
			U32										curView;
		} server;
	} pc;
	
	struct {
		U32											count;
		U32											instanceThisFrame;
		F32											deltaSeconds;
		
		struct {
			U32										noProcessThisFrame : 1;
		} flags;
	} betweenSim;

	struct {
		struct {
			U32										forcedStepCount;

			struct {
				U32									client;
				U32									server;
				U32									serverSync;
			} pc;

			struct {
				S32									clientToServer;
				U32									serverSyncToServer;
			} offset;
		} cur, prev, prevPrev;
	} netReceive;
	
	union {
		MovementGlobalStateBGFlags					flagsMutable;
		const MovementGlobalStateBGFlags			flags;
	};
} MovementGlobalStateBG;

typedef struct MovementGlobalStateThreadData {
	struct {
		MovementManager**							updatedManagers;
		MovementProcessingThreadResults**			threadResults;

		struct {
			U32										hasUpdatedManagers	: 1;
		} flags;
	} toBG;
	
	struct {
		MovementManager**							updatedManagers;
		MovementProcessingThreadResults**			threadResults;

		struct {
			U32										hasUpdatedManagers	: 1;
			U32										hasThreadResults	: 1;
		} flags;
	} toFG;
} MovementGlobalStateThreadData;

typedef struct MovementGlobalStateFlags {
	U32												isServer						: 1;

	U32												noLocalProcessing				: 1;

	U32												logUnmanagedResources			: 1;
	U32												logManagedResources				: 1;
	U32												logOnCreate						: 1;
	U32												logOnClientAttach				: 1;
	
	U32												disableNetBufferAdjustment		: 1;
	
	U32												netStatsPaused					: 1;
	
	U32												printAllocationCounts			: 1;

	U32												disableAnimAssert				: 1;
} MovementGlobalStateFlags;

typedef void (*MovementProcessesCB)(const FrameLockedTimer* flt,
									U32* deltaProcesses,
									U32* prevProcesses);

typedef struct MovementGlobalStateDebug {
	StashTable									stLogs;

	MovementPerEntityTimers*					perEntityTimers;

	#if MM_TRACK_ALL_RESOURCE_REMOVE_STATES_TOBG
		StashTable								stRemoveStatesToBG;
	#endif
		
	union {
		MovementManager**						mmsActiveMutable;
		MovementManager*const*const				mmsActive;
	};
		
	MovementManager**							mmsUserNotified;

	U32											managersLock;
	U32											activeLogCount;
	U32											changeCount;

	U32*										serverLogList;

	struct {
		Vec3									pos;
		F32										radius;
	} logOnCreate;

	struct {
		U32										drawCapsules				: 1;
		U32										perEntityTimers				: 1;
		U32										logOutputsText				: 1;
		U32										logOutputs3D				: 1;
		U32										logNetOutputsText			: 1;
		U32										logNetOutputs3D				: 1;
	} flags;
} MovementGlobalStateDebug;

typedef struct MovementGlobalState {
	MovementGlobalMsgHandler						msgHandler;

	U32												frameCount;

	WorldCollIntegration*							wci;
	WorldCollScene*									wcScene;
	
	struct {
		MovementConflictResolverCB					conflictResolver;
		MovementProcessesCB							getProcesses;
	} cb;

	MovementAnimBitRegistry							animBitRegistry;
	
	CritterFactionMatrix*							factionMatrix;

	struct {
		StashTable									msgHandlerToID;
		MovementRequesterClass**					idToClass;
		MovementRequesterClass**					unregisteredClasses;
	} mr;

	struct {
		MovementManagedResourceClass**				idToClass;
		MovementManagedResourceClass**				unregisteredClasses;
	} mmr;

	struct {
		U32											animOwnershipReleased;
	} animBitHandle;

	union {
		MovementBody**								bodiesMutable;
		MovementBody*const*const					bodies;
	};
	
	MovementGlobalStateFG							fg;
	MovementGlobalStateBG							bg;
	MovementGlobalStateThreadData					threadData[2];

	// Struct cs.

	struct {
		CrypticalSection							movementOutputPool;
		CrypticalSection							requesterCreate;
		CrypticalSection							registeredAnimBits;
		CrypticalSection							bodies;
	} cs;

	// Struct animError.

	struct {
		CrypticalSection							cs;
		StashTable									st;
	} animError;

	MovementGlobalStateDebug						debug;

	union {
		MovementGlobalStateFlags					flagsMutable;
		const MovementGlobalStateFlags				flags;
	};
} MovementGlobalState;

extern MovementGlobalState mgState;

// Anim stuff.

void	mmAnimValuesDestroyAll(MovementAnimValues* values);

void	mmAnimBitRegistryClear(MovementAnimBitRegistry* abr);

void	EnterRegisteredAnimBitCS(void);
void	LeaveRegisteredAnimBitCS(void);

U32		mmRegisteredAnimBitCreate(	MovementAnimBitRegistry* abr,
									const char* bitName,
									S32 isFlashBit,
									const MovementRegisteredAnimBit** bitOut);

S32		mmRegisteredAnimBitGetByHandle(	MovementAnimBitRegistry* abr,
										const MovementRegisteredAnimBit**const bitOut,
										U32 bitHandle);

void	mmRegisteredAnimBitComboFind(	MovementAnimBitRegistry* abr,
										const MovementRegisteredAnimBitCombo** bitComboOut,
										const MovementRegisteredAnimBitCombo* comboPrev,
										const U32* bits);

S32		mmGetLocalAnimBitFromHandle(const MovementRegisteredAnimBit** bitOut,
									U32 handle,
									S32 isServerHandle);

void	mmApplyStanceDiff(	const U32*const stancesOff,
							const U32*const stancesOn,
							U32**const stancesInOut);

void	mmAnimValuesApplyStanceDiff(MovementManager* mm,
									const MovementAnimValues*const anim,
									S32 invert,
									U32**const stancesInOut,
									const char *pcCallingFunc,
									U32 reportStackErrorAndPreventCrash);

void	mmAnimValuesRemoveFromFlags(const MovementAnimValues* anim,
									U32** flagsInOut);

//--- EntityMovementManager.c ----------------------------------------------------------------------

S32		mmStartPerEntityTimer(MovementManager* mm);

void	mmBodyLockEnter(void);
void	mmBodyLockLeave(void);

void	mmBodyGetDebugString(	MovementBody* b,
								char** estrBufferInOut);

void	mmRareLockEnter(MovementManager* mm);
void	mmRareLockLeave(MovementManager* mm);

S32		mmOutputCreate(MovementOutput** outputOut);
void	mmOutputDestroy(MovementOutput** outputInOut);

void	mmClientLogString(	MovementClient* mc,
							const char* text,
							const char* error);

S32		mmClientPacketToClientCreate(	MovementClient* mc,
										Packet** pakOut,
										const char* pakTypeName);

S32		mmClientPacketToClientSend(	MovementClient* mc,
									Packet** pakInOut);

S32		mmClientSendFlagToClient(	MovementClient* mc,
									const char* pakTypeName,
									S32 enabled);

S32		mmClientPacketToServerCreate(	Packet** pakOut,
										const char* pakTypeName);

S32		mmClientPacketToServerSend(Packet** pakInOut);

S32		mmClientSendFlagToServer(	const char* pakTypeName,
									S32 enabled);

S32		mmNetOutputCreate(MovementNetOutput** outputOut);

void	mmNetOutputListAddTail(	MovementNetOutputList* nol,
								MovementNetOutput* no);

void	mmNetOutputListSetTail(	MovementNetOutputList* nol,
								MovementNetOutput* no);
								
S32		mmNetOutputListRemoveHead(	MovementNetOutputList* nol,
									MovementNetOutput** noOut);

void	mmNetOutputCreateAndAddTail(	MovementManager* mm,
										MovementThreadData* td,
										MovementNetOutput** noOut);

S32		mmNetOutputEncodedCreate(MovementNetOutputEncoded** noeOut);
void	mmNetOutputEncodedDestroy(MovementNetOutputEncoded** noeInOut);

void	wrapped_mmHandleBadAnimData(MovementManager* mm,
									const char* fileName,
									U32 fileLine);
#define mmHandleBadAnimData(mm) wrapped_mmHandleBadAnimData(mm, __FILE__, __LINE__);

void	mmInputStepCreate(	MovementClient* mc,
							MovementInputStep** miStepOut);

void	mmInputStepReclaim(	MovementClient* mc,
							MovementInputStep* miStep);

void	mmInputStepDestroy(MovementInputStep** miStepInOut);

void	mmInputEventCreateNonZeroed(MovementInputEvent** mieOut,
									MovementClient* mc,
									MovementInputUnsplitQueue* q,
									MovementInputValueIndex mivi,
									U32 msTime);

S32		mmInputEventDestroy(MovementInputEvent** mieInOut);

void	mmSanitizeInputValueF32(MovementInputValueIndex mivi,
								F32* valueInOut);

S32		mmInputEventResetAllValues(MovementManager* mm);

void	mmResourceAlloc(MovementManagedResource** mmrOut);

void	mmResourceGetConstantDebugString(	MovementManager* mm,
											MovementManagedResource* mmr,
											MovementManagedResourceClass* mmrc,
											const void* constant,
											const void* constantNP,
											char** bufferInOut);

void	wrapped_mmLogResource(	MovementManager* mm,
								MovementManagedResource* mmr,
								FORMAT_STR const char* format,
								...);

#define mmLogResource(mm, mmr, format, ...)\
		(MMLOG_IS_ENABLED(mm)?wrapped_mmLogResource(mm, mmr, FORMAT_STRING_CHECKED(format), __VA_ARGS__),0:0)

void	mmResourceMsgInit(	MovementManagedResourceMsgPrivateData* pd,
							MovementManagedResourceMsgOut* out,
							MovementManager* mm,
							MovementManagedResource* mmr,
							MovementManagedResourceMsgType msgType,
							U32 toSlot);

void	mmRequesterMsgInitFG(	MovementRequesterMsgPrivateData* pd,
								MovementRequesterMsgOut* out,
								MovementRequester* mr,
								MovementRequesterMsgType msgType);

void	mmResourceMsgSend(MovementManagedResourceMsgPrivateData* pd);

void	mmResourceGetNewHandle(	MovementManager* mm,
								U32* handleOut);

S32		mmOutputRepredictCreate(MovementOutputRepredict** morOut);

S32		mmOutputRepredictDestroy(MovementOutputRepredict** morInOut);
S32		mmOutputRepredictDestroyUnsafe(MovementOutputRepredict* mor);

void	mmRequesterMsgInit(	MovementRequesterMsgPrivateData* pd,
							MovementRequesterMsgOut* out,
							MovementRequester* mr,
							MovementRequesterMsgType msgType,
							U32 toSlot);

void	mmRequesterMsgSend(MovementRequesterMsgPrivateData* pd);

void	mmRequesterLockAcquire(void);

void	mmRequesterLockRelease(void);

void	mmSendMsgsAfterSyncFG(MovementManager* mm);

S32		mmGetManagedResourceClassByID(	U32 id,
										MovementManagedResourceClass** mmrcOut);

void	mmRequesterGetSyncDebugString(	MovementRequester* mr,
										char* buffer,
										U32 bufferLen,
										void* useThisSync,
										void* useThisSyncPublic);

U32		mmGetCurrentThreadSlot(void);

S32		mmGetDataClassName(	const char** nameOut,
							U32 mdc);

void	mmGetDataClassNames(char* namesOut,
							S32 namesOutSize,
							U32 mdcBits);

void	mmGetHandledMsgsNames(	char* namesOut,
								S32 namesOutSize,
								U32 handledMsgs);

void	mmAddAnimBit(	MovementManager* mm,
						MovementAnimBitRegistry* abr,
						MovementAnimValues* to,
						U32 bitHandle,
						const DynSkeletonPreUpdateParams* params);

void	mmRemoveAnimBit(	MovementManager* mm,
							MovementAnimBitRegistry* abr,
							MovementAnimValues* from,
							U32 bitHandle,
							const DynSkeletonPreUpdateParams* params);
							
void	mmAllTriggerSend(const MovementTrigger* t);

void	mmSendMsgViewStatusChangedFG(	MovementManager* mm,
										const char* reason);

void	mmExecListAddHead(	MovementExecList* mel,
							MovementExecNode* node);

void	mmExecListRemove(	MovementExecList* mel,
							MovementExecNode* node);

S32		mmExecListContains(	MovementExecList* mel,
							MovementExecNode* node);

void	mmClearGeometry(void);

void	mmSendMsgRequesterCreatedFG(MovementManager* mm,
									MovementRequester* mr);

void	mmGetForcedSetCountFG(	MovementManager* mm,
								MovementThreadData* td,
								U32* countOut);

void	mmLastAnimReset(MovementLastAnim* lastAnim);

void	mmLastAnimCopy(	MovementLastAnim* d,
						const MovementLastAnim* s);

void	mmLastAnimCopyLimitFlags(	MovementLastAnim *d,
									const MovementLastAnim *s,
									const char *pcFuncName);

void	mmLastAnimCopyToValues(	MovementAnimValues* v,
								const MovementLastAnim* s);

void	mmLastAnimCopyFromValues(	MovementLastAnim* s,
									const MovementAnimValues* anim);

void	mmLastAnimCopyFromValuesLimitFlags(	MovementLastAnim *s,
											const MovementAnimValues *anim,
											const char *pcFuncName);

void	mmCopyAnimValueToSizedStack(U32** dest,
									const U32*const src,
									const char *pcFuncName);

void	wrapped_mmLastAnimLog(	MovementManager* mm,
								const MovementLastAnim* lastAnim,
								S32 isNet,
								const char* tag,
								const char* name);
#define mmLastAnimLogLocal(mm, lastAnim, tag, name) STATEMENT(		\
			if(MMLOG_IS_ENABLED(mm)){								\
				wrapped_mmLastAnimLog(mm, lastAnim, 0, tag, name);	\
			}														\
		)
#define mmLastAnimLogNet(mm, lastAnim, tag, name) STATEMENT(		\
			if(MMLOG_IS_ENABLED(mm)){								\
				wrapped_mmLastAnimLog(mm, lastAnim, 1, tag, name);	\
			}														\
		)

void	mmHandleAfterSimWakesIncFG(	MovementManager* mm,
									const char* reason,
									const char* storeReasonValue);

void	mmHandleAfterSimWakesDecFG(	MovementManager* mm,
									const char* reason);

//--- EntityMovementManagerBG.c --------------------------------------------------------------------

#define MM_TD_SET_HAS_TOFG(mm, td)\
		(FALSE_THEN_SET(td->toFG.flagsMutable.hasToFG) ? (mmSetAfterSimWakesOnceBG(mm),1) : 0)

void	mmRequesterAddNewToListBG(	MovementManager* mm,
									MovementRequester* mr);

void	mmRemoveFromGridBG(MovementManager* mm);

void	mmAllHandleBetweenSimBG(void);

void	mmRequesterMsgInitBG(	MovementRequesterMsgPrivateData* pd,
								MovementRequesterMsgOut* out,
								MovementRequester* mr,
								MovementRequesterMsgType msgType);

void	mmRequesterDestroyPipesBG(	MovementManager* mm,
									MovementRequester* mr);

void	mmUpdateGridSizeIndexBG(MovementManager* mm);

S32		mmGridGetCellByGridPosBG(	MovementManagerGrid* grid,
									MovementManagerGridCell** cellOut,
									const IVec3 posGrid,
									S32 create);

S32		mmGetWorldCollGridCellBG(	MovementManager* mm,
									const Vec3 pos,
									WorldCollGridCell** wcCellOut);

void	mmAddToHasPostStepDestroyListBG(MovementManager* mm);

void	mmDestroyAllSimBodyInstancesBG(MovementManager* mm);

void	mmPastStateListCountIncBG(MovementManager* mm);

void	mmPastStateListCountDecBG(MovementManager* mm);

void	mmHandleActorCreatedBG(const WorldCollIntegrationMsg *msg);

void	mmHandleActorDestroyedBG(const WorldCollIntegrationMsg *msg);

S32		mmGetCapsulesBG(MovementManager* mm,
						const Capsule*const** capsulesOut);

void	mmSetAfterSimWakesOnceBG(MovementManager* mm);

#if MM_VERIFY_PAST_STATE_LIST_COUNT
	void	mmVerifyPastStateCountBG(MovementManager* mm);
#else
	#define mmVerifyPastStateCountBG(mm)
#endif

//--- EntityMovementManagerResource.c --------------------------------------------------------------

void	mmCopyNetResourcesToBG(	MovementManager* mm,
								MovementThreadData* td);

void	mmResourceStateCreate(	MovementManager* mm,
								MovementManagedResource* mmr,
								MovementManagedResourceState** mmrsOut,
								MovementManagedResourceStateType mmrsType,
								const void* state,
								U32 spc);

void	mmResourceConstantAlloc(MovementManager* mm,
								MovementManagedResourceClass* mmrc,
								void** constantOut,
								void** constantNPOut);

void	mmResourcesSendWakeFG(MovementManager* mm);

void	mmResourcesSetStateFG(	MovementManager* mm,
								MovementThreadData* td);

void	mmRemoveRepredictedResourceStatesFG(MovementManager* mm,
											MovementThreadData* td);

void	mmHandleResourceUpdatesFromBG(	MovementManager* mm,
										MovementThreadData* td);

void	mmResourcesSendToClientFG(	MovementManager* mm,
									Packet* pak,
									S32 isLocalEntity,
									S32 fullUpdate);

void	mmReceiveResourcesFG(	MovementManager* mm,
								MovementThreadData* td,
								Packet* pak,
								S32 isLocalEntity,
								S32 fullUpdate,
								RecordedEntityUpdate* recUpdate);

void	mmReceiveResourceFromReplayFG(	MovementManager* mm,
										RecordedResource* recRes);

void	mmResourcesAfterSendingToClients(MovementManager* mm);

void	mmDestroyAllResourcesFG(MovementManager* mm);

void	mmResourcesDebugDrawFG(	MovementManager* mm,
								const MovementDrawFuncs* funcs);

void	mmResourcesAlwaysDrawFG(MovementManager* mm,
								const MovementDrawFuncs* funcs);

void	mmrSetAlwaysDrawFG(	MovementManager* mm,
							MovementManagedResource* mmr,
							S32 enabled);

void	mmResourcesSendMsgTrigger(	MovementManager* mm,
									const MovementTrigger* t);

S32		mmResourcesPlayDetailAnimsFG(	MovementManager* mm,
										const DynSkeletonPreUpdateParams* params);

void	mmLogResourceStatesFG(MovementManager* mm);

void	mmResourcesSendMsgBodiesDestroyedFG(MovementManager* mm);

void	mmResourcesSetNeedsSetStateIfHasUnsentStatesFG(MovementManager* mm);

#if MM_TRACK_ALL_RESOURCE_REMOVE_STATES_TOBG
	void	mmResourceDebugAddRemoveStatesToBG(	MovementManagedResource* mmr,
												const MovementManagedResourceState*const* removeStates);

	void	mmResourceDebugRemoveRemoveStatesToBG(	MovementManagedResource* mmr,
													const MovementManagedResourceState*const* removeStates);
#else
	#define mmResourceDebugAddRemoveStatesToBG(mmr, removeStates)
	#define mmResourceDebugRemoveRemoveStatesToBG(mmr, removeStates);
#endif

#if MM_TRACK_RESOURCE_STATE_FLAGS
	enum {
		MMRS_TRACK_ALLOCED						= BIT(0),
		MMRS_TRACK_FREED						= BIT(1),
		MMRS_TRACK_IN_FG						= BIT(2),
		MMRS_TRACK_IN_BG						= BIT(3),
		MMRS_TRACK_SENT_REMOVE_TO_BG			= BIT(4),
		MMRS_TRACK_SENT_FINISHED_TO_FG			= BIT(5),
		MMRS_TRACK_REMOVE_REQUESTED_FROM_BG		= BIT(6),
		MMRS_TRACK_SENT_REMOVE_REQUEST_TO_FG	= BIT(7),
	};

	void	mmrsTrackFlags(	MovementManagedResourceState* mmrs,
							U32 flagsToAssertSet,
							U32 flagsToAssertUnset,
							U32 flagsToSet,
							U32 flagsToReset);
#else
	#define mmrsTrackFlags(	mmrs,\
							flagsToAssertSet,\
							flagsToAssertUnset,\
							flagsToSet,\
							flagsToReset)
#endif

//--- EntityMovementManagerResourceBG.c ------------------------------------------------------------

void	mmResourceGetStateDebugStringBG(MovementManager* mm,
										MovementManagedResource* mmr,
										char** estrBufferInOut);

void	mmDestroyRequesterResourcesBG(	MovementManager* mm,
										MovementRequester* mr);

void	mmUpdateResourcesEnabledBG(MovementRequester* mr);

void	mmResourcesRemoveLocalStatesBG(	MovementManager* mm,
										MovementThreadData* td);

void	mmResourcesSetStateBG(	MovementManager* mm,
								MovementThreadData* td);

void	mmDeactivateAllResourcesBG(MovementManager* mm);

void	mmHandleResourceUpdatesFromFG(	MovementManager* mm,
										MovementThreadData* td);

void	mmResourcesHandleDestroyedFromFG(	MovementManager* mm,
											MovementThreadData* td);

void	mmResourcesSendMsgBodiesDestroyedBG(MovementManager* mm);

//--- EntityMovementManagerNet.c -------------------------------------------------------------------

typedef struct MovementNetUpdateHeader {
	struct {
		U32 getOutputCount			: 1;
		U32	hasNotInterpedOutput	: 1;
		U32	hasPosUpdate			: 1;
		U32	hasRotFaceUpdate		: 1;
		U32	hasAnimUpdate			: 1;
		U32 hasRequesterUpdate		: 1;
		U32 hasResourceUpdate		: 1;
	} flags;
} MovementNetUpdateHeader;

enum {
	MM_CLIENT_NET_HEADER_BIT_COUNT				= 8,
	MM_CLIENT_NET_SYNC_HEADER_BIT_COUNT			= 8,
	MM_CLIENT_NET_EXTRA_FLAGS_BIT_COUNT			= 8,
	MM_NET_HEADER_BIT_COUNT						= 8,
	MM_NET_RARE_FLAGS_BIT_COUNT					= 6,
	MM_NET_OUTPUT_HEADER_BIT_COUNT				= 8,
	MM_NET_REQUESTER_UPDATE_FLAGS_BIT_COUNT		= 7,
	MM_NET_SYNC_FLAGS_BIT_COUNT					= 5,
	MM_NET_ANIM_GEO_HEADER_BIT_COUNT			= 6,
	MM_NET_ROTATION_ENCODED_BIT_COUNT			= 15,

	MM_NET_SMALL_CHANGE_BIT_COUNT				= 5,
	MM_NET_SMALL_CHANGE_VALUE_COUNT				= BIT(MM_NET_SMALL_CHANGE_BIT_COUNT),
	
	#if !MM_NET_USE_LINEAR_ENCODING
		MM_NET_SMALL_CHANGE_MAX_SHIFT			= 10,
		MM_NET_SMALL_CHANGE_SHIFT_BITS			= 5,
		MM_NET_SMALL_CHANGE_LINEAR_VALUE_COUNT	=	BIT(MM_NET_SMALL_CHANGE_BIT_COUNT) -
													MM_NET_SMALL_CHANGE_SHIFT_BITS,
	#endif

	MM_NET_ROTATION_ENCODED_MAX					= BIT(MM_NET_ROTATION_ENCODED_BIT_COUNT) - 1,
};

void	mmLogSyncUpdate(MovementRequester* mr,
						ParseTable* pti,
						void* structPtr,
						const char* prefix,
						const char* structName);

void	mmMakeBasisFromOffset(	MovementManager* mm,
								const IVec3 encOffset,
								IMat3 encMatOut,
								U32* encOffsetLenOut,
								Mat3 matOut);

void	mmConvertVec3ToIVec3(	const Vec3 vec,
								IVec3 vecOut);

S32		mmConvertS32ToS24_8(S32 s32);

void	mmConvertIVec3ToVec3(	const IVec3 vec,
								Vec3 vecOut);

void	mmEncodeQuatToPyr(	const Quat rot, 
							IVec3 pyrOut);

void	mmDecodePyr(const IVec3 encodedPyr, 
					Vec3 decodedPyr);


void	mmNetDestroyStatsFrames(MovementClient* mc);
void	mmNetDestroyStatsPackets(MovementClient* mc);

#if MM_LOG_NET_ENCODING
	#define nprintf(format, ...) mmLog(mm, NULL, "[net.pos]"format, __VA_ARGS__)
#else
	#define nprintf(...)
#endif

//--- EntityMovementManagerNetReceive.c ------------------------------------------------------------

void mmNetAutoDebugStartCollectingData(void);

//--- EntityMovementManagerLog.c -------------------------------------------------------------------

void mmLogReceive(Packet* pak);
