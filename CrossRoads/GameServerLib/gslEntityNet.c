/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslEntity.h"
#include "gslEntityNet.h"
#include "gslExtern.h"
#include "net/net.h"
#include "GameServerLib.h"
#include "Entity.h"
#include "EntityNet.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "structnet.h"
#include "dynnode.h"
#include "EntityMovementManager.h"
#include "CostumeCommonEntity.h"
#include "timing.h"
#include "testclient_comm.h"
#include "gslEntity.h"
#include "gslPartition.h"
#include "AttribMod.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "stringutil.h"
#include "InfoTracker.h"
#include "entityiterator.h"
#include "EntityGrid.h"
#include "qsortg.h"
#include "Team.h"
#include "winutil.h"
#include "cutscene.h"
#include "Player.h"
#include "gslTeamUp.h"
#include "TeamUpCommon.h"
#include "logging.h"

#include "entitysysteminternal.h"
#include "ImbeddedList.h"

#if 1
	#define FORCE_INLINE __forceinline
#else
	#define FORCE_INLINE
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

//#define CAN_SEND_VERIFY_STRINGS 1

#define ENT_SEND_EXCLUDE_NORMAL		(TOK_EDIT_ONLY | TOK_CLIENT_ONLY | TOK_SERVER_ONLY | TOK_SELF_ONLY | TOK_SELF_AND_TEAM_ONLY)
#define ENT_SEND_EXCLUDE_SPECIAL	(TOK_EDIT_ONLY | TOK_CLIENT_ONLY | TOK_SERVER_ONLY | TOK_SELF_ONLY)
#define ENT_SEND_EXCLUDE_LOCAL		(TOK_EDIT_ONLY | TOK_CLIENT_ONLY | TOK_SERVER_ONLY)

#define ENT_SEND_MATCH_NORMAL		(0)
#define ENT_SEND_MATCH_SPECIAL		(0)
#define ENT_SEND_MATCH_LOCAL		(0)

typedef struct EntitySendTypeFlags {
	StructTypeField		includeFlags;
	StructTypeField		excludeFlags;
} EntitySendTypeFlags;

// Status of entity send per net link
typedef enum EntitySendInfo
{
	ENT_SEND_INFO_NO_SEND = 0,
	ENT_SEND_INFO_NORMAL,
	ENT_SEND_INFO_SPECIAL,
	ENT_SEND_INFO_LOCAL,
	ENT_SEND_INFO_COUNT,
		
	ENT_SEND_INFO_MASK_NO_FLAGS = 0x7f,
	
	ENT_SEND_INFO_FLAG_FULL_DIFF = 0x80,
} EntitySendInfo;

static const EntitySendTypeFlags includeExcludeFlags[ENT_SEND_INFO_COUNT - 1] = {
	{ ENT_SEND_MATCH_NORMAL,	ENT_SEND_EXCLUDE_NORMAL },
	{ ENT_SEND_MATCH_SPECIAL,	ENT_SEND_EXCLUDE_SPECIAL },
	{ ENT_SEND_MATCH_LOCAL,		ENT_SEND_EXCLUDE_LOCAL },
};

typedef struct EntitySendPerf {
	const char*			name;
	PERFINFO_TYPE*		pi;
} EntitySendPerf;

const U32 bucketDistSQR[] = {
	SQR(30),
	SQR(50),
	SQR(75),
	SQR(100),
	SQR(150),
	SQR(300),
	SQR(500),
	SQR(750),
};

static const U32 s_uNearbyPlayerDistThresholdSQR = SQR(75);

static S32 didPushFullPacketIndexFromSomeThread;

typedef struct SendEntityBucketData
{
	U32* entBuckets[ARRAY_SIZE(bucketDistSQR)];
	U32* distancesSQR[ARRAY_SIZE(bucketDistSQR)];
} SendEntityBucketData;

typedef struct SendEntityUpdateThreadData {
	U8				entSentThisFrameInfo[MAX_ENTITIES_PRIVATE];
	U32*			newEntIndices;
	U32*			deletedEntIndices;
	U32*			fullPacketIndexes;
	
	struct {
		Entity*					ents[MAX_ENTITIES_PRIVATE];
		Entity**				proxEntsToPlayer;
		U32*					entOrder;
		SendEntityBucketData	entData;
		SendEntityBucketData	civData;
	} nearby;
} SendEntityUpdateThreadData;

typedef enum EntitySendPerfType {
	ENT_SEND_PERF_SEND_DIFF,
	ENT_SEND_PERF_SEND_FULL,
	ENT_SEND_PERF_CREATE_DIFF,
	ENT_SEND_PERF_CREATE_FULL,
	ENT_SEND_PERF_COUNT,
} EntitySendPerfType;

static EntitySendPerf entSendPerf[ENT_SEND_PERF_COUNT][ENT_SEND_INFO_COUNT - 1] = {
	{	{ "SendNormalDiff" },
		{ "SendSpecialDiff" },
		{ "SendLocalDiff" } },
		
	{	{ "SendNormalFull" },
		{ "SendSpecialFull" },
		{ "SendLocalFull" } },
		
	{	{ "CreateNormalDiff" },
		{ "CreateSpecialDiff" },
		{ "CreateLocalDiff" } },
		
	{	{ "CreateNormalFull" },
		{ "CreateSpecialFull" },
		{ "CreateLocalFull" } },
};

enum {
	LOCK_CREATE_DIFF,
	LOCK_CREATE_FULL,
	LOCK_CREATE_COUNT,
};

static Entity**				entsToSendToAll;
static Packet*				entFullPackets[MAX_ENTITIES_PRIVATE];
static Packet*				entDiffPackets[MAX_ENTITIES_PRIVATE][ENT_SEND_INFO_COUNT - 1];
static Packet*				mmDiffPackets[MAX_ENTITIES_PRIVATE];
static U8					deleteFlags[MAX_ENTITIES_PRIVATE];
static S32					deleteFlagsWereSet;
static U8					lockCreate[MAX_ENTITIES_PRIVATE][LOCK_CREATE_COUNT];
static CRITICAL_SECTION		csChangeEntInfo;

enum {
	LOCK_CREATE_ENTITY_IN_PROGRESS_BASE_BIT 		= BIT(0),
	LOCK_CREATE_ENTITY_FINISHED_BASE_BIT			= BIT(3),
	LOCK_CREATE_MM_IN_PROGRESS_BIT					= BIT(6),
	LOCK_CREATE_MM_FINISHED_BIT						= BIT(7),

	LOCK_CREATE_ENTITY_FINISHED_OR_IN_PROGRESS_BITS	=	LOCK_CREATE_ENTITY_IN_PROGRESS_BASE_BIT |
														LOCK_CREATE_ENTITY_FINISHED_BASE_BIT,

	LOCK_CREATE_MM_FINISHED_OR_IN_PROGRESS_BITS		=	LOCK_CREATE_MM_IN_PROGRESS_BIT |
														LOCK_CREATE_MM_FINISHED_BIT,
};

#if CAN_SEND_VERIFY_STRINGS
	static void gslSendCheckString(	ClientLink* link,
									Packet* pak,
									const char* s)
	{
		MM_CHECK_STRING_WRITE(pak, s);

		if(link->doSendPacketVerifyData)
		{
			pktSendString(pak, s);
		}
	}
#else
	#define gslSendCheckString(link, pak, string) MM_CHECK_STRING_WRITE(pak, string)
#endif
	
void gslEntityUpdateThreadDataCreate(SendEntityUpdateThreadData** tdOut)
{
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&csChangeEntInfo);
	}
	ATOMIC_INIT_END;
	
	*tdOut = callocStruct(SendEntityUpdateThreadData);
}

void gslEntityUpdateThreadDataDestroy(SendEntityUpdateThreadData** tdInOut)
{
	SendEntityUpdateThreadData*const td = SAFE_DEREF(tdInOut);
	
	if(td){
		ARRAY_FOREACH_BEGIN(td->nearby.entData.entBuckets, i);
			eaiDestroy(&td->nearby.entData.entBuckets[i]);
			eaiDestroy(&td->nearby.entData.distancesSQR[i]);
		ARRAY_FOREACH_END;
		ARRAY_FOREACH_BEGIN(td->nearby.civData.entBuckets, i);
			eaiDestroy(&td->nearby.civData.entBuckets[i]);
			eaiDestroy(&td->nearby.civData.distancesSQR[i]);
		ARRAY_FOREACH_END;
		
		eaDestroy(&td->nearby.proxEntsToPlayer);
		eaiDestroy(&td->fullPacketIndexes);
		
		SAFE_FREE(*tdInOut);
	}
}

void gslEntityAddDeleteFlags(Entity* e, U8 flags)
{
	if(e)
	{
		U32 entIndex = INDEX_FROM_ENTITY(e);
		
		if(entIndex < ARRAY_SIZE(deleteFlags)){
			deleteFlags[entIndex] |= flags;
			
			if(flags){
				deleteFlagsWereSet = 1;
			}
		}
	}
}

static void gslSendEntityStruct(Packet*const pak,
								const ClientLink*const link,
								const S32 entIndex,
								const Entity*const e,
								const Entity*const ePrevSent,
								const enumSendDiffFlags sendDiffFlags,
								const EntitySendInfo entSendInfo)
{
	gslSendCheckString(link, pak, "eb");
	
	if(	ENTACTIVE_BY_INDEX(entIndex) ||
		ENTINFO_BY_INDEX(entIndex).regularDiffThisFrame)
	{
		ParserSend(	parse_Entity,
					pak,
					ePrevSent,
					e,
					sendDiffFlags,
					includeExcludeFlags[entSendInfo - 1].includeFlags,
					includeExcludeFlags[entSendInfo - 1].excludeFlags,
					NULL);
	}else{
		ParserSendEmptyDiff(parse_Entity, pak);
	}

	gslSendCheckString(link, pak, "ee");
}

static void gslSendMovement(const ClientLink* link,
							MovementManager* mm,
							Packet* pak,
							S32 fullUpdate)
{
	gslSendCheckString(link, pak, "mb");

	mmSendToClient(	link->movementClient,
					mm,
					pak,
					fullUpdate);

	gslSendCheckString(link, pak, "me");
}

static void gslSendEntityBucket(Packet*const pak,
								const ClientLink*const link,
								const S32 entIndex,
								const Entity*const e,
								int fullSend)
{
	const Entity*const oldEnt = fullSend ? NULL : LASTFRAME_ENTITY_FROM_INDEX(entIndex);

	U32 bucketFlags = 0;

	if(	ENTACTIVE_BY_INDEX(entIndex) ||
		ENTINFO_BY_INDEX(entIndex).regularDiffThisFrame)
	{
		bucketFlags |= ENT_BUCKET_SEND;

		if(!oldEnt || oldEnt->myEntityFlags != e->myEntityFlags)
			bucketFlags |= ENT_BUCKET_ENTITYFLAGS;

		if(e->pChar)
		{
			if(character_ModsNetCheckUpdate(e->pChar, oldEnt ? oldEnt->pChar : NULL))
				bucketFlags |= ENT_BUCKET_MODSNET;

			if(character_AttribsNetCheckUpdate(e->pChar))
				bucketFlags |= ENT_BUCKET_ATTRIBS;

			if(character_AttribsInnateNetCheckUpdate(e->pChar))
				bucketFlags |= ENT_BUCKET_ATTRIBS_INNATE;

			if(character_LimitedUseCheckUpdate(e->pChar))
				bucketFlags |= ENT_BUCKET_LIMITEDUSE;

			if(e->pChar->bChargeDataDirty)
				bucketFlags |= ENT_BUCKET_CHARGEDATA;
		}
	}

	gslSendCheckString(link, pak, "bb");

	pktSendBits(pak, ENT_BUCKET_BITS, bucketFlags);

	if(bucketFlags)
	{
		if(bucketFlags & ENT_BUCKET_ENTITYFLAGS)
		{
			pktSendBitsAuto(pak, e->myEntityFlags);
			pktSendBitsAuto(pak, e->myCodeEntityFlags);
			pktSendBitsAuto(pak, e->myDataEntityFlags);
		}

		if(bucketFlags & ENT_BUCKET_MODSNET)
			character_ModsNetSend(e->pChar, pak);

		if(bucketFlags & ENT_BUCKET_ATTRIBS)
			character_AttribsNetSend(e->pChar, pak);

		if(bucketFlags & ENT_BUCKET_ATTRIBS_INNATE)
			character_AttribsInnateNetSend(e->pChar, pak);

		if(bucketFlags & ENT_BUCKET_LIMITEDUSE)
			character_LimitedUseSend(e->pChar, pak);

		if(bucketFlags & ENT_BUCKET_CHARGEDATA)
			character_ChargeDataSend(e->pChar, pak);
	}

	gslSendCheckString(link, pak, "be");
}

static void gslUpdateEntityBucket(Entity* e, Entity* oldEnt)
{
	if(oldEnt)
	{
		oldEnt->myEntityFlags = e->myEntityFlags;
		oldEnt->myCodeEntityFlags = e->myCodeEntityFlags;
		oldEnt->myDataEntityFlags = e->myDataEntityFlags;

		if(e->pChar)
		{
			character_ModsNetUpdate(e->pChar, oldEnt->pChar);
			character_LimitedUseUpdate(e->pChar);
			e->pChar->bChargeDataDirty = false;
		}
	}
}

static void gslEntityClearDeleteFlags(void)
{
	if(TRUE_THEN_RESET(deleteFlagsWereSet)){
		assert(gHighestActiveEntityIndex + 1 <= ARRAY_SIZE(deleteFlags));
		
		ZeroStructs(deleteFlags, gHighestActiveEntityIndex + 1);
	}
}

static FORCE_INLINE void gslCreateEntityDiffPacket(	const ClientLink*const link,
													const S32 entIndex,
													const Entity*const e,
													const Entity*const ePrevSent,
													const EntitySendInfo entSendInfo)
{
	Packet* pak = entDiffPackets[entIndex][entSendInfo - 1];

	if(!pak){
		pak = entDiffPackets[entIndex][entSendInfo - 1] = pktCreateTemp(link->netLink);
		pktSetSendable(pak, 1);
	}
	
	assert(!pktGetWriteIndex(pak));
	
	PERFINFO_AUTO_START_STATIC(	entSendPerf[ENT_SEND_PERF_CREATE_DIFF][entSendInfo - 1].name,
								&entSendPerf[ENT_SEND_PERF_CREATE_DIFF][entSendInfo - 1].pi,
								1);

	gslSendEntityStruct(pak,
						link,
						entIndex,
						e,
						ePrevSent,
						SENDDIFF_FLAG_COMPAREBEFORESENDING,
						entSendInfo);

	PERFINFO_AUTO_STOP();
}

static FORCE_INLINE void gslCreateEntityFullPacket(	const ClientLink*const link,
													const U32 entIndex,
													const Entity*const e)
{
	Packet* pak = entFullPackets[entIndex];

	assert(!pak);

	pak = entFullPackets[entIndex] = pktCreateTemp(link->netLink);
	pktSetSendable(pak, 1);
	
	assert(!pktGetWriteIndex(pak));
	
	PERFINFO_AUTO_START_STATIC(	entSendPerf[ENT_SEND_PERF_CREATE_FULL][ENT_SEND_INFO_NORMAL - 1].name,
								&entSendPerf[ENT_SEND_PERF_CREATE_FULL][ENT_SEND_INFO_NORMAL - 1].pi,
								1);

	gslSendEntityStruct(pak,
						link,
						entIndex,
						e,
						NULL,
						SENDDIFF_FLAG_FORCEPACKALL,
						ENT_SEND_INFO_NORMAL);

	PERFINFO_AUTO_STOP();
}

static FORCE_INLINE void gslCreateMovementDiffPacket(	const ClientLink*const link,
														const S32 entIndex,
														const Entity*const e)
{
	Packet* pak = mmDiffPackets[entIndex];

	if(!pak){
		pak = mmDiffPackets[entIndex] = pktCreateTemp(link->netLink);
		pktSetSendable(pak, 1);
	}
	
	assert(!pktGetWriteIndex(pak));

	gslSendMovement(link,
					e->mm.movement,
					pak,
					0);
}

static FORCE_INLINE void gslSendEntityDiff(	Packet* pak,
											const ClientLink*const link,
											const S32 entIndex,
											const Entity*const e,
											const Entity*const ePrevSent,
											const EntitySendInfo entSendInfo)
{
	switch(entSendInfo){
		xcase ENT_SEND_INFO_NORMAL:
			START_BIT_COUNT(pak, "diff:normal");
		xcase ENT_SEND_INFO_SPECIAL:
			START_BIT_COUNT(pak, "diff:special");
		xcase ENT_SEND_INFO_LOCAL:
			START_BIT_COUNT(pak, "diff:local");
	}

	if(link->doSendPacketVerifyData){
		PERFINFO_AUTO_START("sendUniqueEntity", 1);

		gslSendEntityStruct(pak,
							link,
							entIndex,
							e,
							ePrevSent,
							SENDDIFF_FLAG_COMPAREBEFORESENDING,
							entSendInfo);

		PERFINFO_AUTO_STOP();
	}else{
		const U32	shift = entSendInfo - 1;
		Packet*		pakCached;
		
		PERFINFO_AUTO_START("sendCachedEntity", 1);

		if(!(	lockCreate[entIndex][LOCK_CREATE_DIFF] &
				(LOCK_CREATE_ENTITY_FINISHED_BASE_BIT << shift)))
		{
			U32 sleepCount = 0;
			
			PERFINFO_AUTO_START("waitForFinish", 1);
			while(!(lockCreate[entIndex][LOCK_CREATE_DIFF] &
					(LOCK_CREATE_ENTITY_FINISHED_BASE_BIT << shift)))
			{
				if(sleepCount < 1000){
					sleepCount++;
					Sleep(0);
				}else{
					Sleep(1);
				}
			}
			PERFINFO_AUTO_STOP();
		}
				
		pakCached = entDiffPackets[entIndex][entSendInfo - 1];
		
		assert(pakCached);

		PERFINFO_AUTO_START("pktAppend", 1);
			pktAppend(pak, pakCached, 0);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_STOP();
	}
	
	if(!mmSendToClientShouldCache(link->movementClient, e->mm.movement, 0)){
		gslSendMovement(link, e->mm.movement, pak, 0);
	}else{
		Packet* pakCached;
		
		PERFINFO_AUTO_START("sendCachedMovement", 1);

		if(!(lockCreate[entIndex][LOCK_CREATE_DIFF] & LOCK_CREATE_MM_FINISHED_BIT)){
			U32 sleepCount = 0;

			PERFINFO_AUTO_START("waitForFinish", 1);
			while(!(lockCreate[entIndex][LOCK_CREATE_DIFF] & LOCK_CREATE_MM_FINISHED_BIT)){
				if(sleepCount < 1000){
					sleepCount++;
					Sleep(0);
				}else{
					Sleep(1);
				}
			}
			PERFINFO_AUTO_STOP();
		}
		
		pakCached = mmDiffPackets[entIndex];
		
		assert(pakCached);

		PERFINFO_AUTO_START("pktAppend", 1);
			pktAppend(pak, pakCached, 0);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_STOP();
	}

	gslSendEntityBucket(pak, link, entIndex, e, false);
	
	STOP_BIT_COUNT(pak);
}

static FORCE_INLINE void gslSendEntityFull(	Packet* pak,
											const ClientLink*const link,
											const U32 entIndex,
											const Entity*const e,
											const Entity*const ePrevSent,
											const EntitySendInfo entSendInfo,
											const S32 localEntityIndex)
{
	switch(entSendInfo){
		xcase ENT_SEND_INFO_NORMAL:
			START_BIT_COUNT(pak, "full:normal");
		xcase ENT_SEND_INFO_SPECIAL:
			START_BIT_COUNT(pak, "full:special");
		xcase ENT_SEND_INFO_LOCAL:
			START_BIT_COUNT(pak, "full:local");
	}
	
	//then send the entire entity ref ID
	pktSendBits(pak, ENTITY_REF_ID_BITS, ID_FROM_REFERENCE(e->myRef));

	// Send local player number (1 is added, so 0 means not local player)
	pktSendBitsAuto(pak, localEntityIndex + 1);

	//send the ent type, as the client might need it to create a new entity to unpack this
	//entity into
	pktSendBitsAuto(pak, e->myEntityType);

	if(	entSendInfo == ENT_SEND_INFO_NORMAL &&
		!ePrevSent)
	{
		Packet* pakCached;
		
		PERFINFO_AUTO_START("sendCachedEntity", 1);

		if(!(lockCreate[entIndex][LOCK_CREATE_FULL] & LOCK_CREATE_ENTITY_FINISHED_BASE_BIT)){
			U32 sleepCount = 0;

			PERFINFO_AUTO_START("waitForFinish", 1);
			while(!(lockCreate[entIndex][LOCK_CREATE_FULL] & LOCK_CREATE_ENTITY_FINISHED_BASE_BIT)){
				if(sleepCount < 1000){
					sleepCount++;
					Sleep(0);
				}else{
					Sleep(1);
				}
			}
			PERFINFO_AUTO_STOP();
		}
				
		pakCached = entFullPackets[entIndex];
		
		assert(pakCached);

		PERFINFO_AUTO_START("pktAppend", 1);
			pktAppend(pak, pakCached, 0);
		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_STOP();
	}else{
		gslSendEntityStruct(pak,
							link,
							entIndex,
							e,
							ePrevSent,
							SENDDIFF_FLAG_FORCEPACKALL,
							entSendInfo);
	}

	gslSendMovement(link,
					e->mm.movement,
					pak,
					1);

	gslSendEntityBucket(pak, link, entIndex, e, true);
	
	STOP_BIT_COUNT(pak);
}

static int disableEntityInactive;
AUTO_CMD_INT(disableEntityInactive, disableEntityInactive);

static void gslCreateSendToAllList(void){
	if(entsToSendToAll){
		eaSetSize(&entsToSendToAll, 0);
	}

	partition_FillTransportEnts(&entsToSendToAll);
}

void gslEntityUpdateBegin(void)
{
	PERFINFO_AUTO_START("Ent Info update", 1);
	{
		EntityIterator* iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE | ENTITYFLAG_IS_PLAYER);
		Entity* e;
		while(e = EntityIteratorGetNext(iter))
		{
			EntityInfo* info = &ENTINFO(e);
			Character*	pChar = e->pChar;

			if(disableEntityInactive)
			{
				info->active = true;
				continue;
			}

			if(	info->active &&
				ABS_TIME_SINCE(e->lastActive) > SEC_TO_ABS_TIME(5) &&
				(	!pChar ||
					(	nearf(pChar->pattrBasic->fHitPoints, pChar->pattrBasic->fHitPointsMax) &&
						character_ModsShieldsFull(pChar))) &&
				!entIsPrimaryPet(e))
			{
				info->active = false;
			}
			info->regularDiffThisFrame = TRUE_THEN_RESET(info->regularDiffNextFrame);
		}
		EntityIteratorRelease(iter);
	}
	PERFINFO_AUTO_STOP();

	gslCreateSendToAllList();

	mmAllBeforeSendingToClients(gGSLState.flt);

	// Costume may be required to change on the entity if assets change
	costumeEntity_TickCheckEntityCostumes();
}

static void gslEntityFreeFullUpdatePackets(SendEntityUpdateThreadData*const*const tds){
	if(!TRUE_THEN_RESET(didPushFullPacketIndexFromSomeThread)){
		return;
	}

	PERFINFO_AUTO_START("free full update packets", 1);
	{
		EARRAY_CONST_FOREACH_BEGIN(tds, i, isize);
			SendEntityUpdateThreadData* td = tds[i];
			
			EARRAY_INT_CONST_FOREACH_BEGIN(td->fullPacketIndexes, j, jsize);
				S32 entIndex = td->fullPacketIndexes[j];
				
				pktFree(&entFullPackets[entIndex]);
			EARRAY_FOREACH_END;
			
			eaiSetSize(&td->fullPacketIndexes, 0);
		EARRAY_FOREACH_END;
	}
	PERFINFO_AUTO_STOP();
}

void dirtyBitLogInit(void);

AUTO_COMMAND;
void tpsEnableLog(void)
{
	extern int do_struct_send_logging;
	extern StashTable parse_struct_names;
	extern void parserPopulateStructName(ParseTable* tpi,char *rootname);

	//if (!parse_struct_names)
	parse_struct_names = stashTableCreateAddress(1);
	parserPopulateStructName(parse_Entity,"");
	dirtyBitLogInit();
	do_struct_send_logging=1;
}

AUTO_COMMAND;
void tpsDisableLog(void)
{
	extern int do_struct_send_logging;

	do_struct_send_logging=0;
}

static int g_total_ents_considered, g_total_ents_active, g_total_ents_regulardiff;

AUTO_COMMAND;
void tpsStructs(void)
{
	extern void tpsStructsInternal(void);

	tpsStructsInternal();
	printf("ents - active: %d   sent: %d   total: %d\n",g_total_ents_active,g_total_ents_regulardiff + g_total_ents_active, g_total_ents_considered);
}

AUTO_COMMAND;
void tpsBytes(void)
{
	extern void tpsBytesInternal(void);

	tpsBytesInternal();
}

static U32 maxEntityDiffPacketBytesToKeep = 1024;
AUTO_CMD_INT(maxEntityDiffPacketBytesToKeep, maxEntityDiffPacketBytesToKeep);

S32 gslEntityUpdateEndInThread(	void* unused,
								S32 entityIndex,
								SendEntityUpdateThreadData** tds)
{
	Entity*		e;
	EntityInfo* eInfo;
	extern int do_struct_send_logging;

	if(!entityIndex){
		gslEntityFreeFullUpdatePackets(tds);

		ZeroStructs(lockCreate, gHighestActiveEntityIndex + 1);

		gslEntityClearDeleteFlags();
	}
	
	if(entityIndex > gHighestActiveEntityIndex){
		return 0;
	}
	
	e = ENTITY_FROM_INDEX(entityIndex);
	
	if(!e){
		return 1;
	}

	eInfo = &ENTINFO(e);

	if (do_struct_send_logging)
	{
		g_total_ents_considered++;
		if (eInfo->active)
			g_total_ents_active++;
		if (eInfo->regularDiffThisFrame)
			g_total_ents_regulardiff++;
	}
	if(	eInfo->active ||
		eInfo->regularDiffThisFrame)
	{
		Entity* ePrevSent = LASTFRAME_ENTITY_FROM_INDEX(entityIndex);
		PERFINFO_AUTO_START("Clear Dirty Bits", 1);

		if(TRUE_THEN_RESET(e->dirtyBitSet)){
			FixupStructLeafFirst(parse_Entity, e, FIXUPTYPE__BUILTIN__CLEAR_DIRTY_BITS, NULL);

			if(ePrevSent){
				FixupStructLeafFirst(parse_Entity, ePrevSent, FIXUPTYPE__BUILTIN__CLEAR_DIRTY_BITS, NULL);
			}
		}
		PERFINFO_AUTO_STOP();
	}
		
	ARRAY_FOREACH_BEGIN(entDiffPackets[entityIndex], i);
		Packet* pak = entDiffPackets[entityIndex][i];

		if(pak){
			if(	maxEntityDiffPacketBytesToKeep &&
				pktGetSize(pak) >= maxEntityDiffPacketBytesToKeep)
			{
				switch(i + 1){
					xcase ENT_SEND_INFO_NORMAL:
						PERFINFO_AUTO_START("freeBigDiffPacket:normal", 1);
					xcase ENT_SEND_INFO_SPECIAL:
						PERFINFO_AUTO_START("freeBigDiffPacket:special", 1);
					xcase ENT_SEND_INFO_LOCAL:
						PERFINFO_AUTO_START("freeBigDiffPacket:local", 1);
					xdefault:
						assert(0);
				}
				pktFree(&entDiffPackets[entityIndex][i]);
				PERFINFO_AUTO_STOP();
			}else{
				pktSetWriteIndex(pak, 0);
			}
		}
	ARRAY_FOREACH_END;
	
	if(mmDiffPackets[entityIndex]){
		if(	maxEntityDiffPacketBytesToKeep &&
			pktGetSize(mmDiffPackets[entityIndex]) >= maxEntityDiffPacketBytesToKeep)
		{
			PERFINFO_AUTO_START("freeBigDiffPacket:movement", 1);
			pktFree(&mmDiffPackets[entityIndex]);
			PERFINFO_AUTO_STOP();
		}else{
			pktSetWriteIndex(mmDiffPackets[entityIndex], 0);
		}
	}

	return 1;
}

void gslEntityUpdateEnd(void){

	//for debug logging to try to catch entities send with different-level packets on consecutive frames
	static int siFrameCount = 0;

	PERFINFO_AUTO_START_FUNC();
	
	siFrameCount++;

	FOR_BEGIN(entityIndex, gHighestActiveEntityIndex + 1);
	{
		Entity* e = ENTITY_FROM_INDEX(entityIndex);
		Entity*	ePrevSent;

		if(!e){
			continue;
		}

		ePrevSent = LASTFRAME_ENTITY_FROM_INDEX(entityIndex);

		// THESE ARE NOT GUARANTEED TO BE REAL ENTITIES

		if(TRUE_THEN_RESET(e->sentThisFrame))
		{
			int usedDiffPacket = false;

			if(!(e->mySendFlags & ENT_SEND_FLAG_FULL_NEEDED))
			{
				S32 i;

				// Apply highest quality diff packet.
				
				for(i = ARRAY_SIZE(entDiffPackets[entityIndex]) - 1; i >= 0; i--){
					Packet* pak = entDiffPackets[entityIndex][i];
					
					if(pak){
						if(pktGetWriteIndex(pak)){
							switch(i + 1){
								xcase ENT_SEND_INFO_NORMAL:
									PERFINFO_AUTO_START("applyDiff:normal", 1);
								xcase ENT_SEND_INFO_SPECIAL:
									PERFINFO_AUTO_START("applyDiff:special", 1);
								xcase ENT_SEND_INFO_LOCAL:
									PERFINFO_AUTO_START("applyDiff:local", 1);
								xdefault:
									assert(0);
							}
							pktSetIndex(pak, 0);
							if(!ePrevSent){
								ePrevSent = EntSystem_CreateLastFrameEntityFromIndex(entityIndex);
							}
					
							MM_CHECK_STRING_READ(pak, "eb");
							pktSetAssertOnError(pak, 1);
							ParserRecv(	parse_Entity,
										pak,
										ePrevSent,
										RECVDIFF_FLAG_COMPAREBEFORESENDING);
							MM_CHECK_STRING_READ(pak, "ee");
							usedDiffPacket = true;
							PERFINFO_AUTO_STOP();
							break;
						}
					}
				}
			}

			if(!usedDiffPacket)
			{
				// No diff packet was found, so copy directly to last-frame Entity.
				
				PERFINFO_AUTO_START("copy full entity", 1);
				if (!ePrevSent)
				{
					ePrevSent = EntSystem_CreateLastFrameEntityFromIndex(entityIndex);
				}

				StructCopyFields(parse_Entity, e, ePrevSent, 0, 0);
				PERFINFO_AUTO_STOP();
			}

			e->mySendFlags = ENT_SEND_FLAG_LASTFRAME_COPY_EXISTS;

			gslUpdateEntityBucket(e, ePrevSent);
		}
		else
		{
			// Not sent this frame, so no longer need it
			if (e->mySendFlags & ENT_SEND_FLAG_LASTFRAME_COPY_EXISTS)
			{
				PERFINFO_AUTO_START("StructDeInit old entity", 1);
				if(ePrevSent)
				{
					EntSystem_DeleteLastFrameEntityFromIndex(entityIndex);
					ePrevSent = NULL;
				}
				PERFINFO_AUTO_STOP();
			}
			e->mySendFlags = 0;

			ARRAY_FOREACH_BEGIN(entDiffPackets[entityIndex], i);
				if(entDiffPackets[entityIndex][i]){
					switch(i + 1){
						xcase ENT_SEND_INFO_NORMAL:
							PERFINFO_AUTO_START("freeDiffPacket:normal", 1);
						xcase ENT_SEND_INFO_SPECIAL:
							PERFINFO_AUTO_START("freeDiffPacket:special", 1);
						xcase ENT_SEND_INFO_LOCAL:
							PERFINFO_AUTO_START("freeDiffPacket:local", 1);
						xdefault:
							assert(0);
					}
					pktFree(&entDiffPackets[entityIndex][i]);
					PERFINFO_AUTO_STOP();
				}
			ARRAY_FOREACH_END;
			
			if(mmDiffPackets[entityIndex]){
				PERFINFO_AUTO_START("freeDiffPacket:movement", 1);
				pktFree(&mmDiffPackets[entityIndex]);
				PERFINFO_AUTO_STOP();
			}
		}
	}
	FOR_END;
	
	PERFINFO_AUTO_STOP();
}

static void gslQueueEntitySend(	Entity*const e,
								const U8*const entSentLastFrameInfo,
								const EntitySendInfo entSendInfo,
								S32**const sentEntIndices, 
								SendEntityUpdateThreadData*const td)
{
	S32 entIndex;
	
	if(!e){
		return;
	}
	
	entIndex = INDEX_FROM_REFERENCE(e->myRef);

	// If we already queued this entity, then skip it.
	
	if(	td->entSentThisFrameInfo[entIndex]
		||
		!mmHasProcessed(e->mm.movement) &&
		entSendInfo != ENT_SEND_INFO_LOCAL)
	{
		return;
	}

	if(	!ENTACTIVE_BY_INDEX(entIndex) &&
		!ENTINFO_BY_INDEX(entIndex).regularDiffThisFrame &&
		!(e->mySendFlags & ENT_SEND_FLAG_FULL_NEEDED))
	{
		if(	e->mySendFlags & ENT_SEND_FLAG_NEVER_SENT ||
			!entSentLastFrameInfo[entIndex] ||
			entSentLastFrameInfo[entIndex] != entSendInfo)
		{
			EnterCriticalSection(&csChangeEntInfo);
			{
				// Only skip it if force full send isn't set
				ENTINFO_BY_INDEX(entIndex).regularDiffNextFrame = 1;
			}
			LeaveCriticalSection(&csChangeEntInfo);
			return;
		}
	}

	// remember that we sent this entity this frame, and how
	
	td->entSentThisFrameInfo[entIndex] = entSendInfo;

	if(	e->mySendFlags & ENT_SEND_FLAG_NEVER_SENT ||
		!entSentLastFrameInfo[entIndex])
	{
		// New entity for me.
		
		eaiPush(&td->newEntIndices, entIndex);
		
		//printf("adding entity: %d\n", entIndex);

		if(entSentLastFrameInfo[entIndex])
		{
			// Remove from the current list if I had been sent this entity entIndex before.
			
			if(eaiFindAndRemove(sentEntIndices, entIndex) < 0)
			{
				// Entity should have been in the list.
				
				assert(0);
			}
		}
	}
	else
	{
		// Was sent to me before...
		
		assert(e->mySendFlags & ENT_SEND_FLAG_LASTFRAME_COPY_EXISTS);
		
		if(	entSentLastFrameInfo[entIndex] != entSendInfo ||
			e->mySendFlags & ENT_SEND_FLAG_FULL_NEEDED)
		{
			// ...but either used different send info OR it is forcing a full update.
			
			eaiPush(&td->newEntIndices, entIndex);
			
			//printf("re-adding entity: %d\n", entIndex);

			td->entSentThisFrameInfo[entIndex] |= ENT_SEND_INFO_FLAG_FULL_DIFF;

			if(eaiFindAndRemove(sentEntIndices, entIndex) < 0)
			{
				// Entity should have been in the list.
				
				assert(0);
			}
		}
		else
		{
			// ...and was sent to me, so this is just a normal diff.
			
			assert(eaiFind(sentEntIndices, entIndex) >= 0);
		}
	}

	//set the sent-this-frame flag.
	e->sentThisFrame = 1;
}

static FORCE_INLINE void gslCreateCachedPackets(const ClientLink*const link,
												SendEntityUpdateThreadData*const td)
{
	S32 waitForDiffs = 0;
	
	if(link->doSendPacketVerifyData){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	if(eaiSize(&link->sentEntIndices)){
		PERFINFO_AUTO_START("diffs", 1);
		
		EARRAY_INT_CONST_FOREACH_BEGIN(link->sentEntIndices, i, isize);
			const U32		entIndex = link->sentEntIndices[i];
			EntitySendInfo	entSendInfo = td->entSentThisFrameInfo[entIndex];
			const U32		shift = (entSendInfo - 1);
			
			// Create the entity diff packet.
			
			if(	!(	lockCreate[entIndex][LOCK_CREATE_DIFF] &
					(LOCK_CREATE_ENTITY_FINISHED_OR_IN_PROGRESS_BITS << shift)))
			{
				U8 prevState = _InterlockedOr8(	&lockCreate[entIndex][LOCK_CREATE_DIFF],
												(LOCK_CREATE_ENTITY_IN_PROGRESS_BASE_BIT << shift));
												
				if(!(prevState & (LOCK_CREATE_ENTITY_FINISHED_OR_IN_PROGRESS_BITS << shift))){
					// Got the lock, so create the packet.
					
					gslCreateEntityDiffPacket(	link,
												entIndex,
												ENTITY_FROM_INDEX(entIndex),
												LASTFRAME_ENTITY_FROM_INDEX(entIndex),
												entSendInfo);

					prevState = _InterlockedOr8(&lockCreate[entIndex][LOCK_CREATE_DIFF],
												(LOCK_CREATE_ENTITY_FINISHED_BASE_BIT << shift));
												
					assert(!(prevState & (LOCK_CREATE_ENTITY_FINISHED_BASE_BIT << shift)));
				}
			}
			
			// Create the movement diff packet.

			if(!(	lockCreate[entIndex][LOCK_CREATE_DIFF] &
					LOCK_CREATE_MM_FINISHED_OR_IN_PROGRESS_BITS))
			{
				Entity* e = ENTITY_FROM_INDEX(entIndex);
				
				if(mmSendToClientShouldCache(link->movementClient, e->mm.movement, 0)){
					U8 prevState = _InterlockedOr8(	&lockCreate[entIndex][LOCK_CREATE_DIFF],
													LOCK_CREATE_MM_IN_PROGRESS_BIT);

					if(!(prevState & LOCK_CREATE_MM_FINISHED_OR_IN_PROGRESS_BITS)){
						gslCreateMovementDiffPacket(link,
													entIndex,
													ENTITY_FROM_INDEX(entIndex));

						prevState = _InterlockedOr8(&lockCreate[entIndex][LOCK_CREATE_DIFF],
													LOCK_CREATE_MM_FINISHED_BIT);
													
						assert(!(prevState & LOCK_CREATE_MM_FINISHED_BIT));
					}
				}else{
					mmCreateNetOutputsFG(e->mm.movement);
				}
			}

			// If it's not finished, then wait for it to finish later.

			if(!(lockCreate[entIndex][LOCK_CREATE_DIFF] & LOCK_CREATE_MM_FINISHED_BIT)){
				waitForDiffs = 1;
			}
		EARRAY_FOREACH_END;
		
		PERFINFO_AUTO_STOP();
	}
	
	if(eaiSize(&td->newEntIndices)){
		PERFINFO_AUTO_START("fulls", 1);

		EARRAY_INT_CONST_FOREACH_BEGIN(td->newEntIndices, i, isize);
			const U32		entIndex = td->newEntIndices[i];
			S32				doSendDiff;
			EntitySendInfo	entSendInfo;
			
			doSendDiff = !!(td->entSentThisFrameInfo[entIndex] & ENT_SEND_INFO_FLAG_FULL_DIFF);
			entSendInfo = td->entSentThisFrameInfo[entIndex] & ENT_SEND_INFO_MASK_NO_FLAGS;

			if(	entSendInfo != ENT_SEND_INFO_NORMAL ||
				doSendDiff)
			{
				continue;
			}
			
			// Create the entity full packet.
			
			if(!(	lockCreate[entIndex][LOCK_CREATE_FULL] &
					LOCK_CREATE_ENTITY_FINISHED_OR_IN_PROGRESS_BITS))
			{
				U8 prevState = _InterlockedOr8(	&lockCreate[entIndex][LOCK_CREATE_FULL],
												LOCK_CREATE_ENTITY_IN_PROGRESS_BASE_BIT);
												
				if(!(prevState & LOCK_CREATE_ENTITY_FINISHED_OR_IN_PROGRESS_BITS)){
					// Got the lock, so create the packet.
					
					eaiPush(&td->fullPacketIndexes, entIndex);
					didPushFullPacketIndexFromSomeThread = 1;
					
					gslCreateEntityFullPacket(	link,
												entIndex,
												ENTITY_FROM_INDEX(entIndex));

					prevState = _InterlockedOr8(&lockCreate[entIndex][LOCK_CREATE_FULL],
												LOCK_CREATE_ENTITY_FINISHED_BASE_BIT);
												
					assert(!(prevState & LOCK_CREATE_ENTITY_FINISHED_BASE_BIT));
				}
			}
		EARRAY_FOREACH_END;
		
		PERFINFO_AUTO_STOP();
	}
		
	// Make sure the movement net outputs are there.
		
	if(waitForDiffs){
		PERFINFO_AUTO_START("waitForDiffs", 1);
		
		EARRAY_INT_CONST_FOREACH_BEGIN(link->sentEntIndices, i, isize);
			const U32 entIndex = link->sentEntIndices[i];
			
			if(!(lockCreate[entIndex][LOCK_CREATE_DIFF] & LOCK_CREATE_MM_FINISHED_BIT)){
				mmCreateNetOutputsFG(ENTITY_FROM_INDEX(entIndex)->mm.movement);
			}
		EARRAY_FOREACH_END;

		PERFINFO_AUTO_STOP();
	}

	if(eaiSize(&td->newEntIndices)){
		PERFINFO_AUTO_START("waitForFulls", 1);

		EARRAY_INT_CONST_FOREACH_BEGIN(td->newEntIndices, i, isize);
			const U32 entIndex = td->newEntIndices[i];
			
			mmCreateNetOutputsFG(ENTITY_FROM_INDEX(entIndex)->mm.movement);
		EARRAY_FOREACH_END;

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();// FUNC
}

static FORCE_INLINE void gslSendKnownEntities(	ClientLink* link,
												Packet* pak,
												SendEntityUpdateThreadData*const td)
{
	const U32 deleteCount = eaiSize(&td->deletedEntIndices);
	const U32 newEntCount = eaiSize(&td->newEntIndices);

	PERFINFO_AUTO_START_FUNC();

	// Send flags.

	pktSendBits(pak,
				3,
				(FALSE_THEN_SET(link->isExpectingEntityDiffs) ? 1 : 0) |
				(deleteCount ? 2 : 0) |
				(newEntCount ? 4 : 0));

	if(deleteCount){
		gslSendCheckString(link, pak, "deletes");
		pktSendBitsAuto(pak, deleteCount);

		EARRAY_INT_CONST_FOREACH_BEGIN(td->deletedEntIndices, i, size);
			U32 entIndex = td->deletedEntIndices[i];
			U32 value = ((deleteFlags[entIndex] & ENTITY_DELETE_NOFADE) ? 1 : 0) |
						(entIndex << 1);
						
			pktSendBitsAuto(pak, value);
		EARRAY_FOREACH_END;
	}
	
	if(newEntCount){
		gslSendCheckString(link, pak, "creates");
		pktSendBitsAuto(pak, newEntCount);

		EARRAY_INT_CONST_FOREACH_BEGIN(td->newEntIndices, i, size);
			pktSendBitsAuto(pak, td->newEntIndices[i]);
		EARRAY_FOREACH_END;
	}
	
	// Send diffed entity data.
	
	gslSendCheckString(link, pak, "diffs");
	
	EARRAY_INT_CONST_FOREACH_BEGIN(link->sentEntIndices, i, isize);
		S32				entIndex = link->sentEntIndices[i];
		EntitySendInfo	entSendInfo = td->entSentThisFrameInfo[entIndex];

		PERFINFO_AUTO_START_STATIC(	entSendPerf[ENT_SEND_PERF_SEND_DIFF][entSendInfo - 1].name,
									&entSendPerf[ENT_SEND_PERF_SEND_DIFF][entSendInfo - 1].pi,
									1);
		{
			gslSendEntityDiff(	pak,
								link,
								entIndex,
								ENTITY_FROM_INDEX(entIndex),
								LASTFRAME_ENTITY_FROM_INDEX(entIndex),
								entSendInfo);
		}
		PERFINFO_AUTO_STOP();
	EARRAY_FOREACH_END;

	// Send new entity data.

	gslSendCheckString(link, pak, "fulls");
	
	EARRAY_INT_CONST_FOREACH_BEGIN(td->newEntIndices, i, isize);
		S32				localEntityIndex = -1;
		S32				entIndex = td->newEntIndices[i];
		S32				doSendDiff;
		EntitySendInfo	entSendInfo;
		
		assert(entIndex < MAX_ENTITIES_PRIVATE);

		doSendDiff = !!(td->entSentThisFrameInfo[entIndex] & ENT_SEND_INFO_FLAG_FULL_DIFF);

		td->entSentThisFrameInfo[entIndex] &= ENT_SEND_INFO_MASK_NO_FLAGS;

		entSendInfo = td->entSentThisFrameInfo[entIndex];
		
		assert(	entSendInfo > 0 &&
				entSendInfo < ENT_SEND_INFO_COUNT);

		if(entSendInfo == ENT_SEND_INFO_LOCAL){
			EARRAY_INT_CONST_FOREACH_BEGIN(link->localEntities, j, jsize);
				if((S32)INDEX_FROM_REFERENCE(link->localEntities[j]) == entIndex){
					localEntityIndex = j;
				}
			EARRAY_FOREACH_END;
		}
		
		PERFINFO_AUTO_START_STATIC(	entSendPerf[ENT_SEND_PERF_SEND_FULL][entSendInfo - 1].name,
									&entSendPerf[ENT_SEND_PERF_SEND_FULL][entSendInfo - 1].pi,
									1);
		{
			gslSendEntityFull(	pak,
								link,
								entIndex,
								ENTITY_FROM_INDEX(entIndex),
								doSendDiff ? LASTFRAME_ENTITY_FROM_INDEX(entIndex) : NULL,
								entSendInfo,
								localEntityIndex);
		}
		PERFINFO_AUTO_STOP();
	EARRAY_FOREACH_END;
	
	gslSendCheckString(link, pak, "done");
	
	eaiPushEArray(&link->sentEntIndices, &td->newEntIndices);
	PERFINFO_AUTO_STOP();
}
								
static void gslSendHeader(ClientLink* link, Packet* pak)
{
	U32 flags = 0;

	#if CAN_SEND_VERIFY_STRINGS
		flags |= (link->doSendPacketVerifyData ? 1 : 0);
	#else
		link->doSendPacketVerifyData = 0;
	#endif
				
	pktSendBits(pak, 1, flags);

	// Send the movement header.
	
	mmClientSendHeaderToClient(link->movementClient, pak);
}

static void gslSendFooter(ClientLink* link, Packet* pak)
{
	mmClientSendFooterToClient(link->movementClient, pak);
}

static S32 compareEntOrderByDist(	const U32* distancesSQR,
									const U32* i0,
									const U32* i1)
{
	return subS32(distancesSQR[*i0], distancesSQR[*i1]);
}

static void gslQueueSendNearbyEntities(ClientLink* link, SendEntityUpdateThreadData*const td, SendEntityBucketData* bd, U32 entSendCountMax, U32* entSendCountOut)
{
	ARRAY_FOREACH_BEGIN(bd->entBuckets, i);
		if((*entSendCountOut) < entSendCountMax){
			if((*entSendCountOut) + eaiSize(&bd->entBuckets[i]) > entSendCountMax){
				PERFINFO_AUTO_START("sort", 1);
				{
					EARRAY_INT_CONST_FOREACH_BEGIN(bd->entBuckets[i], j, jsize);
					eaiPush(&td->nearby.entOrder, j);
					EARRAY_FOREACH_END;

					qsortG_s(td->nearby.entOrder,
							 eaiSize(&td->nearby.entOrder),
							 sizeof(td->nearby.entOrder[0]),
							 compareEntOrderByDist,
							 bd->distancesSQR[i]);
				}
				PERFINFO_AUTO_STOP();

				EARRAY_INT_CONST_FOREACH_BEGIN(td->nearby.entOrder, j, jsize);
				const U32		entIndexInBucket = td->nearby.entOrder[j];
				Entity*const 	e = td->nearby.ents[bd->entBuckets[i][entIndexInBucket]];

				if(bd->distancesSQR[i][entIndexInBucket] <= SQR(e->fEntitySendDistance) &&
					gslExternEntityDetectable(link, e))
				{
					gslQueueEntitySend(	e,
						link->entSentLastFrameInfo,
						ENT_SEND_INFO_NORMAL,
						&link->sentEntIndices,
						td);

					// If this entity has an owner, make sure to send the owner,
					// unless it has the do not send flag set.
					// The owner is intentionally excluded from the entSendCount.
					if (e->erOwner)
					{
						Entity*const eOwner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
						if (eOwner && (!(eOwner->myEntityFlags & ENTITYFLAG_DONOTSEND)))
						{
							gslQueueEntitySend(	eOwner,
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_NORMAL,
									&link->sentEntIndices,
									td);
						}
					}

					if(++(*entSendCountOut) >= entSendCountMax){
						break;
					}
				}
				EARRAY_FOREACH_END;

				eaiSetSize(&td->nearby.entOrder, 0);
			}else{
				EARRAY_INT_CONST_FOREACH_BEGIN(bd->entBuckets[i], j, jsize);
				Entity*const e = td->nearby.ents[bd->entBuckets[i][j]];

				if(bd->distancesSQR[i][j] <= SQR(e->fEntitySendDistance) &&
					gslExternEntityDetectable(link, e))
				{
					(*entSendCountOut)++;
					gslQueueEntitySend(e,
									   link->entSentLastFrameInfo,
									   ENT_SEND_INFO_NORMAL,
									   &link->sentEntIndices,
									   td);

					// If this entity has an owner, make sure to send the owner,
					// unless it has the do not send flag set.
					// The owner is intentionally excluded from the entSendCount.
					if (e->erOwner)
					{
						Entity*const eOwner = entFromEntityRef(entGetPartitionIdx(e), e->erOwner);
						if (eOwner && (!(eOwner->myEntityFlags & ENTITYFLAG_DONOTSEND)))
						{
							gslQueueEntitySend(	eOwner,
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_NORMAL,
									&link->sentEntIndices,
									td);
						}
					}
				}
				EARRAY_FOREACH_END;
			}
		}
		eaiSetSize(&bd->entBuckets[i], 0);
		eaiSetSize(&bd->distancesSQR[i], 0);
	ARRAY_FOREACH_END;
}

#define ENTITY_SEND_COUNT_SOFT_CAP 100
#define ENTITY_SEND_MIN_CIV_COUNT 25

static void gslQueueNearbyEntities(	ClientLink* link,
									Entity* eLocal,
									SendEntityUpdateThreadData*const td)
{
	U32 entSendCountMax = link->entSendCountMax ? link->entSendCountMax : ENTITY_SEND_COUNT_SOFT_CAP;
	
	if(eLocal){
		Vec3	posLocal;
		U32		sentCount = 0;
		S32		entCount;

		entGetPos(eLocal, posLocal);

		entCount = entGridProximityLookupEx(entGetPartitionIdx(eLocal),
											posLocal,
											td->nearby.ents,
											0,
											0,
											ENTITYFLAG_DONOTSEND,
											NULL);

		PERFINFO_AUTO_START("bucketize", 1);
		{
			FOR_BEGIN(i, entCount);
			{
				Vec3	pos;
				U32		distSQR;
				U32		bucketIndex = ARRAY_SIZE(bucketDistSQR) - 1;
				Entity* e = td->nearby.ents[i];

				if(e == eLocal){
					continue;
				}
				
				entGetPos(e, pos);
				
				distSQR = (U32)distance3Squared(pos, posLocal);
				
				ARRAY_FOREACH_BEGIN(bucketDistSQR, j);
					if(distSQR <= bucketDistSQR[j]){
						bucketIndex = j;
						break;
					}
				ARRAY_FOREACH_END;

				if(distSQR <= s_uNearbyPlayerDistThresholdSQR){
					if(e->pNodeNearbyPlayer){
						// This entity requests to get the nearby player flag.
						
						eaPush(&td->nearby.proxEntsToPlayer, e);
					}
				}
				
				if (e->myEntityFlags & ENTITYFLAG_CIVILIAN)
				{
					eaiPush(&td->nearby.civData.entBuckets[bucketIndex], i);
					eaiPush(&td->nearby.civData.distancesSQR[bucketIndex], distSQR);
				}
				else
				{
					eaiPush(&td->nearby.entData.entBuckets[bucketIndex], i);
					eaiPush(&td->nearby.entData.distancesSQR[bucketIndex], distSQR);
				}
			}
			FOR_END;
		}
		PERFINFO_AUTO_STOP();
		
		PERFINFO_AUTO_START("queue from buckets", 1);
		{
			// Send normal entities first so that if this reaches the max send count, we don't lose important entities
			gslQueueSendNearbyEntities(link, td, &td->nearby.entData, entSendCountMax, &sentCount);

			// Raise the max send count if necessary
			if (sentCount + ENTITY_SEND_MIN_CIV_COUNT > entSendCountMax)
			{
				entSendCountMax = sentCount + ENTITY_SEND_MIN_CIV_COUNT;
			}

			// Send civilian entities with lower priority
			gslQueueSendNearbyEntities(link, td, &td->nearby.civData, entSendCountMax, &sentCount);
		}
		PERFINFO_AUTO_STOP();
	}
}

static void gslQueueSpecialEntities(ClientLink*const link,
									Entity*const eLocal,
									SendEntityUpdateThreadData*const td)
{
	const Team*const				team = team_GetTeam(eLocal);
	const TeamMember*const*const	tms = SAFE_MEMBER(team, eaMembers);
	TeamUpMember**					teamUpMembers = NULL;

	teamup_getMembers(eLocal,&teamUpMembers);
	
	if(	eaSize(&tms) || eaSize(&entsToSendToAll) || (eLocal && ea32Size(&eLocal->pSaved->ppAwayTeamPetID)) || eaSize(&teamUpMembers) )
	{
		int iPartitionIdx = entGetPartitionIdx(eLocal);

		PERFINFO_AUTO_START("Queue special ents", 1);
		{
			EARRAY_FOREACH_BEGIN(teamUpMembers,i);
			{
				Entity *pEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER, teamUpMembers[i]->iEntID);

				gslQueueEntitySend(pEnt,
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_SPECIAL,
									&link->sentEntIndices,
									td);
			}
			EARRAY_FOREACH_END;

			EARRAY_CONST_FOREACH_BEGIN(entsToSendToAll, i, isize);
			{
				// Don't send stuff in the all list if they're not on your partition
				if(entGetPartitionIdx(entsToSendToAll[i])!=iPartitionIdx)
					continue;

				gslQueueEntitySend(	entsToSendToAll[i],
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_SPECIAL,
									&link->sentEntIndices,
									td);
			}
			EARRAY_FOREACH_END;

			EARRAY_INT_CONST_FOREACH_BEGIN(eLocal->pSaved->ppAwayTeamPetID, i, isize);
				Entity* ePet = entFromContainerID(	iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET,
													eLocal->pSaved->ppAwayTeamPetID[i]);

				gslQueueEntitySend(	ePet,
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_SPECIAL,
									&link->sentEntIndices,
									td);
			EARRAY_FOREACH_END;

			EARRAY_CONST_FOREACH_BEGIN(tms, i, isize);
			{
				const TeamMember*	tm = tms[i];
				Entity*				e = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, tm->iEntID);

				// Don't send on-map TeamMembers if they're not on your partition for some reason
				if(e && entGetPartitionIdx(e)!=iPartitionIdx)
					continue;
				
				gslQueueEntitySend(	e,
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_SPECIAL,
									&link->sentEntIndices,
									td);

				if(SAFE_MEMBER(e, pSaved)){
					EARRAY_INT_CONST_FOREACH_BEGIN(e->pSaved->ppAwayTeamPetID, j, jsize);
						Entity* ePet = entFromContainerID(	iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET,
															e->pSaved->ppAwayTeamPetID[j]);

						gslQueueEntitySend(	ePet,
											link->entSentLastFrameInfo,
											ENT_SEND_INFO_SPECIAL,
											&link->sentEntIndices,
											td);
					EARRAY_FOREACH_END;
				}
			}
			EARRAY_FOREACH_END;
		}
		PERFINFO_AUTO_STOP();
	}
}

static void gslQueueCutsceneEntitiesByPos(	ClientLink*const link,
											Entity*const eLocal,
											SendEntityUpdateThreadData*const td,
											const Vec3 posCenter)
{
	S32 entCount;
	
	entCount = entGridProximityLookupEx(entGetPartitionIdx(eLocal),
										posCenter,
										td->nearby.ents,
										0,
										0,
										ENTITYFLAG_DONOTSEND,
										NULL);

	FOR_BEGIN(j, entCount);
	{
		Entity*	e = td->nearby.ents[j];		
		Vec3	pos;

		entGetPos(e, pos);

		if(distance3Squared(posCenter, pos) < SQR(e->fEntitySendDistance) && gslExternEntityDetectable(link, e)){
			gslQueueEntitySend(	e,
								link->entSentLastFrameInfo,
								ENT_SEND_INFO_NORMAL,
								&link->sentEntIndices,
								td);
		}
	}
	FOR_END;
}

static void gslQueueCutsceneEntities(	ClientLink*const link,
										Entity*const eLocal,
										SendEntityUpdateThreadData*const td)
{
	ActiveCutscene *pActiveCutscene = cutscene_ActiveCutsceneFromPlayer(eLocal);

	if (pActiveCutscene && !vec3IsZero(pActiveCutscene->estimatedCameraPos)){
		gslQueueCutsceneEntitiesByPos(	link,
										eLocal,
										td,
										pActiveCutscene->estimatedCameraPos);
	}
}

static void gslUpdateKnownEntitiesList(	ClientLink* link,
										SendEntityUpdateThreadData*const td)
{
	Entity*	ePrimaryLocal = gslPrimaryEntity(link);

	// Queue "local" entities (flagged as LOCAL).
	
	PERFINFO_AUTO_START("Queue local ents", 1);
	{
		EARRAY_INT_CONST_FOREACH_BEGIN(link->localEntities, i, isize);
			Entity* eLocal = entFromEntityRefAnyPartition(link->localEntities[i]);
			
			gslQueueEntitySend(	eLocal,
								link->entSentLastFrameInfo,
								ENT_SEND_INFO_LOCAL,
								&link->sentEntIndices,
								td);
								
			if(SAFE_MEMBER(eLocal, pChar->primaryPetRef)){
				gslQueueEntitySend(	entFromEntityRefAnyPartition(eLocal->pChar->primaryPetRef),
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_LOCAL,
									&link->sentEntIndices,
									td);
			}
		EARRAY_FOREACH_END;
	}
	PERFINFO_AUTO_STOP();
	
	// Queue "special" entities (flagged as SPECIAL).

	gslQueueSpecialEntities(link,
							ePrimaryLocal,
							td);

	// Queue nearby entities.
	
	PERFINFO_AUTO_START("Queue nearby ents", 1);
	{
		gslQueueNearbyEntities(	link,
								ePrimaryLocal,
								td);
	}
	PERFINFO_AUTO_STOP();

	// Queue cutscene entities.
	
	if(SAFE_MEMBER2(ePrimaryLocal, pPlayer, pCutscene)){
		PERFINFO_AUTO_START("Queue cutscene ents", 1);
		{
			gslQueueCutsceneEntities(	link,
										ePrimaryLocal,
										td);
		}
		PERFINFO_AUTO_STOP();
	}
	
	// Send all controlled entities, just in case they weren't in any of the previous groups.
	
	PERFINFO_AUTO_START("Queue controlled ents", 1);
	{
		const S32 managerCount = mmClientGetManagerCount(link->movementClient);
		
		FOR_BEGIN(i, managerCount);
			MovementManager* mm;
			Entity* e;

			if(	mmClientGetManager(&mm, link->movementClient, i) &&
				mmGetUserPointer(mm, &e))
			{
				gslQueueEntitySend(	e,
									link->entSentLastFrameInfo,
									ENT_SEND_INFO_NORMAL,
									&link->sentEntIndices,
									td);
			}
		FOR_END;
	}
	PERFINFO_AUTO_STOP();

	// Send all entities being debugged, to make sure they don't go away by distance

	if(eaiSize(&link->debugEntities)){
		PERFINFO_AUTO_START("Queue debug ents", 1);
		{
			EARRAY_INT_CONST_FOREACH_BEGIN(link->debugEntities, i, isize);
			{
				Entity* e = entFromEntityRefAnyPartition(link->debugEntities[i]);

				if(e){
					gslQueueEntitySend(	e, 
										link->entSentLastFrameInfo, 
										ENT_SEND_INFO_NORMAL, 
										&link->sentEntIndices, 
										td);
				}else{
					eaiRemoveFast(&link->debugEntities, i);
					i--;
					isize--;
				}
			}
			EARRAY_FOREACH_END;
		}
		PERFINFO_AUTO_STOP();
	}

	// Queue deletes for previously sent entities that aren't sent now.
	
	PERFINFO_AUTO_START("Queue deletes", 1);
	{
		FOR_BEGIN(i, link->iHighestActiveEntityDuringSendLastFrame + 1);
		{
			// If this entity was sent last frame but not this frame, then delete it.
			
			if(	link->entSentLastFrameInfo[i] &&
				!td->entSentThisFrameInfo[i])
			{
				eaiPush(&td->deletedEntIndices, i);
				
				if(eaiFindAndRemove(&link->sentEntIndices, i) < 0){
					assert(0);
				}
			}
		}
		FOR_END;
	}
	PERFINFO_AUTO_STOP();
}

static void gslSaveSentState(	ClientLink* link,
								SendEntityUpdateThreadData*const td)
{
	// Copy info about entities sent this frame.
	
	PERFINFO_AUTO_START_FUNC();
	{
		CopyStructs(link->entSentLastFrameInfo,
					td->entSentThisFrameInfo,
					gHighestActiveEntityIndex + 1);
					
		if(link->iHighestActiveEntityDuringSendLastFrame > gHighestActiveEntityIndex)
		{
			ZeroStructs(link->entSentLastFrameInfo + gHighestActiveEntityIndex + 1,
						link->iHighestActiveEntityDuringSendLastFrame - gHighestActiveEntityIndex);
		}

		ZeroStructs(td->entSentThisFrameInfo,
					gHighestActiveEntityIndex + 1);

		link->iHighestActiveEntityDuringSendLastFrame = gHighestActiveEntityIndex;
	}
	PERFINFO_AUTO_STOP();
}

void gslSendEntityUpdate(	ClientLink *link,
							Packet *pak,
							SendEntityUpdateThreadData*const td)
{
	PERFINFO_AUTO_START_FUNC();

	assert(!link->disconnected);

	// Reset sending state for this thread.

	if(eaiSize(&td->newEntIndices)){
		eaiSetSize(&td->newEntIndices, 0);
	}
	
	if(eaiSize(&td->deletedEntIndices)){
		eaiSetSize(&td->deletedEntIndices, 0);
	}
	
	// Update known entities list.
	
	gslUpdateKnownEntitiesList(link, td);
	
	// Cache entity packets.

	gslCreateCachedPackets(link, td);

	// Send everything.

	gslSendHeader(link, pak);
	gslSendKnownEntities(link, pak, td);
	gslSendFooter(link, pak);
	
	// Save the sent state.

	gslSaveSentState(link, td);

	// Done sending.

	PERFINFO_AUTO_STOP();
}

void gslPopulateNearbyPlayerNotifyEnts(	SendEntityUpdateThreadData* td,
										ImbeddedList *pList)
{
	assert(pList);

	EARRAY_CONST_FOREACH_BEGIN(td->nearby.proxEntsToPlayer, i, isize);
		Entity* pEnt = td->nearby.proxEntsToPlayer[i];
		
		ImbeddedList_PushFront(pList, pEnt->pNodeNearbyPlayer);
		
	EARRAY_FOREACH_END;

	eaClear(&td->nearby.proxEntsToPlayer);
}
