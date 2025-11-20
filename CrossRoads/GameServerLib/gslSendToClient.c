/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslSendToClient.h"
#include "GameServerLib.h"
#include "GlobalComm.h"
#include "CharacterAttribs.h"
#include "EString.h"
#include "EntityLib.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "Player.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "GameServerLib.h"
#include "net/net.h"
#include "StringCache.h"
#include "timing_profiler_interface.h"
#include "UGCProjectCommon.h"
#include "UGCCommon.h"
#include "ServerLib.h"
#include "EntitySavedData.h"
#include "MicroTransactions.h"
#include "gslMapTransfer.h"

#include "textparser.h"

#include "TimedCallback.h"
#include "gslEntityNet.h"
#include "testclient_comm.h"
#include "gslcommandparse.h"
#include "earray.h"
#include "gslEntity.h"
#include "ResourceManager.h"
#include "logging.h"
#include "ThreadManager.h"
#include "cpu_count.h"
#include "NotifyCommon.h"
#include "gslUGC.h"
#include "gslUgcTransactions.h"
#include "mutex.h"
#include "gslMapState.h"
#include "ImbeddedList.h"
#include "GameStringFormat.h"
#include "gslChat.h"
#include "gslChatConfig.h"

#include "SimpleCpuUsage.h"

#include "AutoGen/ServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "../../CrossRoads/common/autogen/GameServerLib_autogen_RemoteFuncs.h"

typedef struct GeneralUpdateThread GeneralUpdateThread;

typedef S32 (*GeneralUpdateThreadCallback)(	GeneralUpdateThread* td,
											S32 index,
											void* userPointer);

typedef struct GeneralUpdateThread {
	struct {
		HANDLE						hEvent;
		U32							msTimeEventSet;
	} frameStart, frameDone;

	ManagedThread*					mt;
	SendEntityUpdateThreadData*		td;
	
	GeneralUpdateThreadCallback		callback;
	void*							userPointer;
	
	struct {
		U32							killThread : 1;
	} flags;
} GeneralUpdateThread;

typedef struct GeneralUpdateMapStatePartition {
	struct {
		Packet*						pak;
		U32							ready;
	} mapState[2];

	CrypticalSection				cs;
} GeneralUpdateMapStatePartition;

static struct {
	GeneralUpdateThread**			threads;
	ClientLink**					clientLinks;
	S32								clientLinkCount;
	
	S32								lastIndexShared;
	
	S32								desiredThreadCount;
	
	ImbeddedList*					inPlayerProximityList;
	ImbeddedList*					outOfPlayerProximityList;
	
	struct {
		SendEntityUpdateThreadData**	tds;
	} entSend;
	
	struct {
		GeneralUpdateMapStatePartition**	mapStatesByPartition;
	} mapState;
	
	struct {
		CrypticalSection			cs;
		Packet**					queue[2];
		U32							curQueue;
		U32							senderThreadID;
	} packet;
} generalUpdate;

void gslSendLoginSuccess(ClientLink *cLink)
{
	NetLink *link = cLink ? cLink->netLink : NULL;
	Packet *pak;

	if (!cLink || cLink->disconnected)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	resServerSendNamespaceInfo(cLink->pResourceCache);

	pak = pktCreate(link,TOCLIENT_GAME_CONNECT_SUCCESS);
	pktSendBits(pak,1,!!isProductionEditMode());

	if (isProductionEditMode())
	{
		// Load the project data
		UGCProjectData *pProjectData = NULL;
		UGCProjectInfo *pProjectInfo;
		UGCProjectAutosaveData *pAutoSaveData = NULL;
		const UGCProjectVersion *pVersion;
		UGCProject *active_project = GET_REF(gGSLState.hUGCProjectFromSubscription);
		Entity* ent = gslPrimaryEntity( cLink ); 
		assert(active_project);

		// Auto connect them to the UGC chat channel.
		ServerChat_JoinSpecialChannel( ent, UGCEDIT_CHANNEL_NAME );
		ServerChatConfig_SetCurrentInputChannel( ent, "Chatwindow_Tabgroup", 0, UGCEDIT_CHANNEL_NAME );

		// Give the user lock access
		resServerGrantEditingLogin(cLink->pResourceCache);

		// Load UGC resource infos so we can validate projects
		ugcResourceInfoPopulateDictionary();

		pVersion = UGCProject_GetMostRecentVersion(active_project);

		// Set up our derived project info to replace possibly outdated stuff that will be loaded
		pProjectInfo = ugcCreateProjectInfo(active_project, pVersion);

		// Load & send the actual project data
		if (gUGCImportProjectName && gUGCImportProjectName[0])
		{
			pProjectData = gslUGC_LoadProjectDataWithInfo(gUGCImportProjectName, pProjectInfo);
			gslUGC_RenameProjectNamespace(pProjectData, pVersion->pNameSpace);
			gslUGC_DoSave(pProjectData, NULL, NULL, false, NULL, __FUNCTION__);
			pAutoSaveData = StructCreate(parse_UGCProjectAutosaveData); // Don't bring over autosaves when importing
		}
		else
		{
			pProjectData = gslUGC_LoadProjectDataWithInfo(pVersion->pNameSpace, pProjectInfo);
			pAutoSaveData = gslUGC_LoadAutosave(pVersion->pNameSpace);
		}

		assert(pProjectData);
		pktSendStruct(pak, pProjectData, parse_UGCProjectData);
		StructDestroySafe(parse_UGCProjectData, &pProjectData);

		// Load and save the latest autosave data, if any
		assert(pAutoSaveData);
		pktSendStruct(pak, pAutoSaveData, parse_UGCProjectAutosaveData);
		StructDestroy(parse_UGCProjectAutosaveData, pAutoSaveData);
	}
	
	pktSend(&pak);

	// This is also a good time to tell them what MT category the shard is
	pak = pktCreate(link, TOCLIENT_MICROTRANSACTION_CATEGORY);
	pktSendU32(pak, g_eMicroTrans_ShardCategory);
	pktSend(&pak);

	gslSendWorldUpdate(cLink,1, false);

	PERFINFO_AUTO_STOP();
}

void gslSendLoginFailure(ClientLink *cLink, const char * reason)
{
	NetLink *link = cLink->netLink;
	Packet *pak;

	if (cLink->disconnected)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	gslSendWorldUpdate(cLink,1, true);

	pak = pktCreate(link,TOCLIENT_GAME_CONNECT_FAILURE);
	pktSendString(pak,reason);
	pktSend(&pak);

	PERFINFO_AUTO_STOP();
}

void gslSendForceLogout(ClientLink* clientLink, const char * reason)
{
	NetLink *link = clientLink->netLink;
	Packet *pak;

	if (!clientLink->disconnected)
	{	
		log_printf(LOG_CLIENTSERVERCOMM, "Telling link %p to log out. Reason: %s", clientLink->netLink, reason);

		pak = pktCreate(link,TOCLIENT_GAME_LOGOUT);
		pktSendString(pak,reason);
		pktSend(&pak);

		linkFlushAndClose(&clientLink->netLink, "Force Logout");
		clientLink->disconnected = 1;
	}
}

void gslSendWorldUpdate(ClientLink *cLink, int full_update, bool bRecordSkyTime)
{
	NetLink *link = cLink->netLink;
	Packet *outpak;

	if (full_update || worldNeedsUpdate(cLink->lastWorldUpdateTime))
	{
		outpak = pktCreate(link, TOCLIENT_WORLD_UPDATE);
		if (!worldSendUpdate(outpak, full_update, NULL, &cLink->lastWorldUpdateTime))
			pktFree(&outpak);
		else
			pktSend(&outpak);
	}

	pktCreateWithCachedTracker(outpak, link, TOCLIENT_WORLD_PERIODIC_UPDATE);
	if (!worldSendPeriodicUpdate(outpak, &cLink->lastWorldSkyUpdateTime, bRecordSkyTime))
		pktFree(&outpak);
	else
		pktSend(&outpak);
}


void entSendVPrintf( Entity* client, const char *s, va_list va )
{
	int size;
	char *buffer, *cursor;

	if (!client)
		return;

	size = _vscprintf(s, va);

	buffer = _alloca(size+1);
	cursor = buffer;
	vsprintf_s(cursor, size+1, s, va);

	START_PACKET( pak1, client, GAMESERVERLIB_CON_PRINTF );
	pktSendString( pak1, buffer );
	END_PACKET

	// Forward messages to CSR listener
	gslSendCSRFeedback(client, buffer);
}


#undef gslSendPrintf
void gslSendPrintf( Entity* client, const char *s, ... )
{
	va_list va;

	va_start( va, s );
	entSendVPrintf( client, s, va );
	va_end(va);
}

void gslSendUnknownChatWindowCmd( Entity* client, const char *cmdString)
{
	START_PACKET(pak, client, GAMESERVERLIB_CHATCMD_UNKNOWN);
	pktSendString(pak, cmdString);
	END_PACKET
}


void gslSendPublicCommand( Entity *e, CmdContextFlag iFlag, const char *s, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	char *pComment = NULL;

	if (!s)
		return;


	if (pStructs)
	{
		estrStackCreate(&pComment);
		estrPrintf(&pComment, "Struct being sent server-to-client via gslSendPublicCommand for command %s", s);
	}

	START_PACKET(pak, e, GAMESERVERLIB_CMD_PUBLIC);
	pktSendString(pak,s);
	cmdParsePutStructListIntoPacket(pak, pStructs, pComment);
	pktSendBits(pak, 32, iFlag);
	pktSendBits(pak, 32, eHow);
	END_PACKET

	estrDestroy(&pComment);
}


void gslSendPrivateCommand( Entity *e, CmdContextFlag iFlag, const char *s, enumCmdContextHowCalled eHow, CmdParseStructList *pStructs)
{
	char *pComment = NULL;
	
	if (!s)
		return;

	if (pStructs)
	{
		estrStackCreate(&pComment);
		estrPrintf(&pComment, "Struct being sent server-to-client via gslSendPrivateCommand for command %s", s);
	}


	START_PACKET(pak, e, GAMESERVERLIB_CMD_PRIVATE);
	pktSendString(pak,s);
	cmdParsePutStructListIntoPacket(pak, pStructs, pComment);
	pktSendBits(pak, 32, iFlag);
	pktSendBits(pak, 32, eHow);
	END_PACKET

	estrDestroy(&pComment);
}

void gslSendFastCommand( Entity *e, CmdContextFlag iFlag, const char *s, enumCmdContextHowCalled eHow, bool bPrivate, CmdParseStructList *pStructs)
{
	if (!s)
		return;


	if(entGetPlayer(e) && entGetNetLink(e))
	{
		int cmd = bPrivate ? GAMESERVERLIB_CMD_PRIVATE : GAMESERVERLIB_CMD_PUBLIC;
		Packet* pak = pktCreate(entGetNetLink(e),TOCLIENT_GAME_MSG);

		char *pComment = NULL;

		if (pStructs)
		{
			estrStackCreate(&pComment);
			estrPrintf(&pComment, "Struct being sent server-to-client via gslSendFastCommand for command %s", s);
		}

		entSendRef(pak, entGetRef(e));
		if(bPrivate) {
			START_BIT_COUNT(pak, "GAMESERVERLIB_CMD_PRIVATE");
		} else {
			START_BIT_COUNT(pak, "GAMESERVERLIB_CMD_PUBLIC");
		}
		pktSendBits(pak, 1, !!(cmd & LIB_MSG_BIT));
		pktSendBitsPack(pak, GAME_MSG_SENDBITS, cmd & ~LIB_MSG_BIT);
		pktSendString(pak,s);
		cmdParsePutStructListIntoPacket(pak, pStructs, pComment);
		pktSendBits(pak, 32, iFlag);
		pktSendBits(pak, 32, eHow);
		STOP_BIT_COUNT(pak);
		pktSend(&pak);

		estrDestroy(&pComment);
	}
}

#undef gslSendPublicCommandf
void gslSendPublicCommandf(Entity *e, CmdContextFlag iFlags, const char *fmt, ...)
{
	va_list va;
	char *commandstr = NULL;
	estrStackCreate(&commandstr);

	va_start( va, fmt );
	estrConcatfv(&commandstr,fmt,va);
	va_end( va );

	gslSendPublicCommand(e, iFlags, commandstr, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
	estrDestroy(&commandstr);
}


#undef gslSendPrivateCommandf
void gslSendPrivateCommandf(Entity *e, CmdContextFlag iFlags, const char *fmt, ...)
{
	va_list va;
	char *commandstr = NULL;
	estrStackCreate(&commandstr);

	va_start( va, fmt );
	estrConcatfv(&commandstr,fmt,va);
	va_end( va );

	gslSendPrivateCommand(e, iFlags, commandstr, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
	estrDestroy(&commandstr);
}

void gslQueueFullUpdateForLink(ClientLink* clientLink)
{
	if(TRUE_THEN_RESET(clientLink->isExpectingEntityDiffs)){
		ZeroArray(clientLink->entSentLastFrameInfo);
		eaiDestroy(&clientLink->sentEntIndices);
		clientLink->isExpectingMapStateDiff = 0;
		mmClientResetSendState(clientLink->movementClient);
	}
}

int bAssertOnSoloClientHang = true;
AUTO_CMD_INT(bAssertOnSoloClientHang, AssertOnSoloClientHang) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE;

void
debugLogStuckClient(ClientLink * clientLink, NetLink *link, Entity *pEnt, S32 assertOnSolo)
{
	char ipbuf[100];
	int puppetCheckPassed = -1;
	int skippedSuccessOnLogin = -1;
	PuppetMaster *pPuppetMaster;
	char *missingPuppetStr = NULL;
	char *logString = NULL;
	CharacterTransfer *transfer = pEnt ? gslGetCharacterTransferForEntity(pEnt) : NULL;
	// is the character in the process of transferring out of this map?
	bool transferringOut = ((transfer != NULL) && (transfer->eFlags & TRANSFERFLAG_PASSED_POINT_OF_NO_RETURN));

	if ( pEnt && pEnt->pSaved && pEnt->pSaved->pPuppetMaster )
	{
		int i;
		pPuppetMaster = pEnt->pSaved->pPuppetMaster;
		
		puppetCheckPassed = pPuppetMaster->bPuppetCheckPassed;
		skippedSuccessOnLogin = pPuppetMaster->bSkippedSuccessOnLogin;

		for ( i = eaSize(&pPuppetMaster->ppPuppets)-1; i >= 0; i-- )
		{
			Entity *pPuppet = GET_REF(pPuppetMaster->ppPuppets[i]->hEntityRef);
			if ( pPuppet == NULL )
			{
				estrConcatf(&missingPuppetStr, "%d,", pPuppetMaster->ppPuppets[i]->curID );
			}
		}
	}
	linkGetIpPortStr(link, SAFESTR(ipbuf));
	estrConcatf( &logString, "Client link stopped sending 3 minutes ago. clientLoggedIn=%d, primaryEntityID=%d, entityFlags=0x%x, clientDisconnected=%d, clientAddr=%s, eLoginWaiting=0x%x, sendBufferFull=%d, puppetCheckPassed=%d, skippedSuccessOnLogin=%d, missingPuppets=%s",
		clientLink->clientLoggedIn, pEnt ? pEnt->myContainerID : 0, pEnt ? entGetFlagBits(pEnt) : 0, clientLink->disconnected, ipbuf, (pEnt && pEnt->pSaved) ? pEnt->pPlayer->eLoginWaiting : 0, linkSendBufFull(link), puppetCheckPassed, skippedSuccessOnLogin, NULL_TO_EMPTY(missingPuppetStr));

	log_printf(LOG_CLIENTSERVERCOMM, "%s", logString);
	if ( transferringOut )
	{
		ErrorDetailsf("%s", logString);
		Errorf("Player has been waiting 3 minutes to transfer out of map");
	}
	else if ( bAssertOnSoloClientHang && pEnt != NULL )
	{
		EntityIterator* iter;
		Entity *currEnt;
		bool solo = true;

		// check to see if there are any other players on this server
		iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			if ( currEnt->myContainerID != pEnt->myContainerID )
			{
				solo = false;
				break;
			}
		}
		EntityIteratorRelease(iter);

		if ( solo ) 
		{
			// only crash and generate a dump if the stuck player is the only one on the game server
			ErrorDetailsf("%s", logString);
			Errorf("Client hung on login to empty gameserver");
			if(assertOnSolo){
				assertmsg(false, "Client hung on login to empty gameserver");
			}
		}
	}

	estrDestroy(&missingPuppetStr);
	estrDestroy(&logString);
}

static S32 gslTestDebugLogStuckClient;
AUTO_CMD_INT(gslTestDebugLogStuckClient, gslTestDebugLogStuckClient);

static void gslQueueGeneralUpdateForLink(NetLink *link,ClientLink *clientLink)
{
	Entity *primaryEntity = gslPrimaryEntity(clientLink);
	U32 curTime = timeSecondsSince2000();

	// See StashTable.c for an explanation.
	//void hitme();
	//hitme();

	if(TRUE_THEN_RESET(gslTestDebugLogStuckClient)){
		PERFINFO_AUTO_START("debugLogStuckClient", 1);
		debugLogStuckClient(clientLink, link, primaryEntity, 0);
		PERFINFO_AUTO_STOP();
	}

	if(	!clientLink->clientLoggedIn ||
		!clientLink->readyForGeneralUpdates ||
		!primaryEntity || 
		entGetFlagBits(primaryEntity) & ENTITYFLAG_PLAYER_DISCONNECTED ||
		clientLink->disconnected)
	{
		if(!clientLink->debugStopUpdateTime){
			clientLink->debugStopUpdateTime = curTime;
		} 
		else if(clientLink->debugStopUpdateTime != U32_MAX &&
			max(clientLink->uClientPatchTm, clientLink->debugStopUpdateTime) + 180 < curTime)
		{
			clientLink->debugStopUpdateTime = U32_MAX;

			if(isProductionMode()){
				// only generate errors in production mode
				gslFrameTimerAddInstance("debugLogStuckClient");
				PERFINFO_AUTO_START("debugLogStuckClient", 1);
				debugLogStuckClient(clientLink, link, primaryEntity, 1);
				PERFINFO_AUTO_STOP();
				gslFrameTimerStopInstance("debugLogStuckClient");
			}
		}
		gslQueueFullUpdateForLink(clientLink);
		return;
	}

	if(linkSendBufFull(link)){
		PERFINFO_AUTO_START("buffer full", 1);

		if(FALSE_THEN_SET(clientLink->hasFullOutputBuffer)){
			gslClientPrintfColor(	clientLink,
									COLOR_RED|COLOR_GREEN,
									"buffer full, pausing netsends");
		}
		if ( clientLink->debugStopUpdateTime == 0 )
		{
			clientLink->debugStopUpdateTime = curTime;
		} 
		else if ( ( clientLink->debugStopUpdateTime != U32_MAX ) && ( max(clientLink->uClientPatchTm, clientLink->debugStopUpdateTime) + 180 < curTime ) )
		{
			clientLink->debugStopUpdateTime = U32_MAX;
			debugLogStuckClient(clientLink, link, primaryEntity, 1);
		}
		gslQueueFullUpdateForLink(clientLink);

		PERFINFO_AUTO_STOP();
		return;
	}
	else if(TRUE_THEN_RESET(clientLink->hasFullOutputBuffer)){
		PERFINFO_AUTO_START("buffer clear", 1);
		gslClientPrintfColor(	clientLink,
								COLOR_BRIGHT|COLOR_RED|COLOR_GREEN,
								"buffer clear, resuming netsends");
		PERFINFO_AUTO_STOP();
	}

	clientLink->debugStopUpdateTime = 0;

	if(!clientLink->isExpectingEntityDiffs){
		assert(!eaiSize(&clientLink->sentEntIndices));
	}
	
	eaPush(&generalUpdate.clientLinks, clientLink);
}

static void gslFinishGeneralUpdateForLink(NetLink* link, ClientLink* clientLink)
{
	resServerDestroySentUpdates(clientLink->pResourceCache);
}

static void gslSendMapState(int iPartitionIdx, 
							Packet* pak,
							NetLink* link,
							ClientLink* clientLink)
{
	S32								isFull = FALSE_THEN_SET(clientLink->isExpectingMapStateDiff);
	GeneralUpdateMapStatePartition*	pMapStatePartition;
	static U32						lock;

	PERFINFO_AUTO_START_FUNC();
	
	readLockU32(&lock);
	pMapStatePartition = eaGet(&generalUpdate.mapState.mapStatesByPartition, iPartitionIdx);
	readUnlockU32(&lock);

	if (!pMapStatePartition) {
		writeLockU32(&lock, 0);
		pMapStatePartition = eaGet(&generalUpdate.mapState.mapStatesByPartition, iPartitionIdx);
		if (!pMapStatePartition) {
			pMapStatePartition = callocStruct(GeneralUpdateMapStatePartition);
			eaSet(&generalUpdate.mapState.mapStatesByPartition, pMapStatePartition, iPartitionIdx);
		}
		writeUnlockU32(&lock);
	}

	if(	isFull &&
		!pMapStatePartition->mapState[1].ready
		||
		!pMapStatePartition->mapState[0].ready)
	{
		csEnter(&pMapStatePartition->cs);
		{
			// Always create a diff if at least one player on the map
			if (!pMapStatePartition->mapState[0].ready){
				if (!pMapStatePartition->mapState[0].pak){
					pMapStatePartition->mapState[0].pak = pktCreateTemp(link);
				}
				
				PERFINFO_AUTO_START("mapState_ServerAppendMapStateToPacket:diff", 1);
				mapState_ServerAppendMapStateToPacket(pMapStatePartition->mapState[0].pak, 0, iPartitionIdx);
				PERFINFO_AUTO_STOP();

				pMapStatePartition->mapState[0].ready = 1;
			}

			// But if this player expects a full send, prepare that too
			if (isFull && !pMapStatePartition->mapState[1].ready){
				if (!pMapStatePartition->mapState[1].pak){
					pMapStatePartition->mapState[1].pak = pktCreateTemp(link);
				}
				
				PERFINFO_AUTO_START("mapState_ServerAppendMapStateToPacket:full", 1);
				mapState_ServerAppendMapStateToPacket(pMapStatePartition->mapState[1].pak, 1, iPartitionIdx);
				PERFINFO_AUTO_STOP();

				pMapStatePartition->mapState[1].ready = 1;
			}
		}
		csLeave(&pMapStatePartition->cs);
	}

	// Append proper packet
	pktAppend(pak, pMapStatePartition->mapState[isFull].pak, 0);

	PERFINFO_AUTO_STOP();
}

static void gslApplyMapStateDiffs(void)
{
	GeneralUpdateMapStatePartition *pMapStatePartition;
	int iPartitionIdx;
	
	PERFINFO_AUTO_START_FUNC();
	
	for(iPartitionIdx=eaSize(&generalUpdate.mapState.mapStatesByPartition)-1; iPartitionIdx>=0; --iPartitionIdx) {
		pMapStatePartition = eaGet(&generalUpdate.mapState.mapStatesByPartition, iPartitionIdx);
		if (pMapStatePartition) {
			if (pMapStatePartition->mapState[0].ready) {
				mapState_ApplyDiffToOldState(iPartitionIdx, pMapStatePartition->mapState[0].pak);
			} else {
				mapState_ApplyDiffToOldState(iPartitionIdx, NULL);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void gslSendThreadedPacket(Packet** pak){
	S32 doSendPackets = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	csEnter(&generalUpdate.packet.cs);
	{
		eaPush(&generalUpdate.packet.queue[generalUpdate.packet.curQueue], *pak);
		
		if(!generalUpdate.packet.senderThreadID){
			generalUpdate.packet.curQueue = !generalUpdate.packet.curQueue;
			generalUpdate.packet.senderThreadID = GetCurrentThreadId();
			doSendPackets = 1;
		}
	}
	csLeave(&generalUpdate.packet.cs);
	
	*pak = NULL;

	while(doSendPackets){
		// Send everything in the queue.
		
		U32 queueIndex = !generalUpdate.packet.curQueue;
		
		EARRAY_CONST_FOREACH_BEGIN(generalUpdate.packet.queue[queueIndex], i, isize);
			pktSend(&generalUpdate.packet.queue[queueIndex][i]);
		EARRAY_FOREACH_END;
		
		eaSetSize(&generalUpdate.packet.queue[queueIndex], 0);
	
		// Check if anything was added to the other queue.
	
		csEnter(&generalUpdate.packet.cs);
		{
			if(eaSize(&generalUpdate.packet.queue[generalUpdate.packet.curQueue])){
				// More packets to send.
				
				generalUpdate.packet.curQueue = !generalUpdate.packet.curQueue;
			}else{
				// No packets, so release the send lock.
				
				doSendPackets = 0;

				assert(generalUpdate.packet.senderThreadID == GetCurrentThreadId());
				generalUpdate.packet.senderThreadID = 0;
			}
		}
		csLeave(&generalUpdate.packet.cs);	
	}
	
	PERFINFO_AUTO_STOP();
}

static S32 gslSendGeneralUpdateInThread(GeneralUpdateThread* t,
										S32 clientIndex,
										void* userPointerUnused)
{
	Packet*		pak;
	Entity*		primaryEntity;
	NetLink*	link;
	ClientLink*	clientLink;
	int			iPartitionIdx;
	
	if(clientIndex >= generalUpdate.clientLinkCount){
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	clientLink = generalUpdate.clientLinks[clientIndex];
	link = clientLink->netLink;
	primaryEntity = gslPrimaryEntity(clientLink);
	iPartitionIdx = entGetPartitionIdx(primaryEntity);

	pktCreateWithCachedTracker(pak, link, TOCLIENT_GENERAL_UPDATE);

	gslSendEntityUpdate(clientLink, pak, t->td);

	if(!resServerAreTherePendingUpdates(clientLink->pResourceCache)){
		pktSendBits(pak, 1, 0);
	}else{
		PERFINFO_AUTO_START("RefServer pending updates", 1);
		pktSendBits(pak, 1, 1);
		resServerSendUpdatesToClient(pak, clientLink->pResourceCache, primaryEntity, clientLink->clientLangID, false);
		PERFINFO_AUTO_STOP();
	}

	if(!clientLink->pPendingTestClientCommandsPacket){
		pktSendBits(pak, 1, 0);
	}else{
		PERFINFO_AUTO_START("Pending TestClient cmds packet", 1);
		pktSendBits(pak, 1, 1);
		pktAppend(pak, clientLink->pPendingTestClientCommandsPacket, 0);
		pktSendU32(pak, 0);
		pktFree(&clientLink->pPendingTestClientCommandsPacket);
		PERFINFO_AUTO_STOP();
	}

	gslSendMapState(iPartitionIdx, pak, link, clientLink);

	gslSendThreadedPacket(&pak);

	if(	SAFE_MEMBER2(primaryEntity, pPlayer, accessLevel) >= ACCESS_GM &&
		primaryEntity->pPlayer->accessLevel != clientLink->accessLevelOfSentCommands)
	{
		clientLink->accessLevelOfSentCommands = primaryEntity->pPlayer->accessLevel;
		SendCommandNamesToClientForAutoCompletion(primaryEntity);
	}

	// Send msgPaks.

	EARRAY_INT_CONST_FOREACH_BEGIN(clientLink->localEntities, i, isize);
	{
		PERFINFO_AUTO_START("msgPaks", 1);
		{
			Entity* e = entFromEntityRefAnyPartition(clientLink->localEntities[i]);

			if(SAFE_MEMBER2(e, pPlayer, msgPak)){
				gslSendThreadedPacket(&e->pPlayer->msgPak);
			}
		}
		PERFINFO_AUTO_STOP();
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();// FUNC
	
	return 1;
}

static DWORD WINAPI gslGeneralUpdateThreadMain(GeneralUpdateThread* t){
	EXCEPTION_HANDLER_BEGIN;
	
	mmSetIsForegroundThreadForLogging();

	while(1){
		SIMPLE_CPU_DECLARE_TICKS(ticksStart);
		SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

		PERFINFO_AUTO_START("wfso:start", 1);
		{
			WaitForSingleObject(t->frameStart.hEvent, INFINITE);
		}
		PERFINFO_AUTO_STOP();

		ADD_MISC_COUNT(	timeGetTime() - t->frameStart.msTimeEventSet,
						"milliseconds since start event set");

		autoTimerThreadFrameEnd();
		autoTimerThreadFrameBegin(__FUNCTION__);

		if(t->flags.killThread){
			t->frameDone.msTimeEventSet = timeGetTime();
			SetEvent(t->frameDone.hEvent);
			
			while(1){
				// Wait for ThreadManager to kill us.

				SleepEx(INFINITE, TRUE);
			}
		}
		
		SIMPLE_CPU_TICKS(ticksStart);

		PERFINFO_AUTO_START("frame", 1);
		
		while(1){
			S32 index = InterlockedIncrement(&generalUpdate.lastIndexShared);
			
			if(!t->callback(t, index, t->userPointer)){
				break;
			}
		}

		SIMPLE_CPU_TICKS(ticksEnd);
		SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT, ticksStart, ticksEnd);

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_START("SetEvent:done", 1);
		{
			t->frameDone.msTimeEventSet = timeGetTime();
			SetEvent(t->frameDone.hEvent);
		}
		PERFINFO_AUTO_STOP();
	}
	
	EXCEPTION_HANDLER_END;
}

AUTO_CMD_INT(generalUpdate.desiredThreadCount, generalUpdateThreadCount);

static void gslCreateGeneralUpdateThreads(void){
	S32 numThreads;

	PERFINFO_AUTO_START_FUNC();
	
    // If not overridden on the command line, use 2 movement threads.
	if(generalUpdate.desiredThreadCount <= 0){
		generalUpdate.desiredThreadCount = 2;
	}
	
    // Cap the number of movement threads by the number of real cores.
	MINMAX1(generalUpdate.desiredThreadCount, 1, getNumRealCpus());
	
	numThreads = MIN(eaSize(&generalUpdate.clientLinks), generalUpdate.desiredThreadCount);
	
	// Create threads if there aren't enough.
		
	if(eaSize(&generalUpdate.threads) < numThreads){
		PERFINFO_AUTO_START("create threads", 1);

		while(eaSize(&generalUpdate.threads) < numThreads){
			GeneralUpdateThread* t;
		
			t = callocStruct(GeneralUpdateThread);
			t->frameStart.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			t->frameDone.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			t->mt = tmCreateThread(gslGeneralUpdateThreadMain, t);
			gslEntityUpdateThreadDataCreate(&t->td);
			eaPush(&generalUpdate.entSend.tds, t->td);
		
			eaPush(&generalUpdate.threads, t);
		}

		PERFINFO_AUTO_STOP();
	}
	
	// Destroy threads if there are too many.

	if(eaSize(&generalUpdate.threads) > numThreads){
		PERFINFO_AUTO_START("destroy threads", 1);

		while(eaSize(&generalUpdate.threads) > numThreads){
			GeneralUpdateThread* t = eaPop(&generalUpdate.threads);

			// Tell thread to die.
		
			ASSERT_FALSE_AND_SET(t->flags.killThread);
			t->frameStart.msTimeEventSet = timeGetTime();
			SetEvent(t->frameStart.hEvent);
		
			// Wait for thread to die.
		
			WaitForSingleObject(t->frameDone.hEvent, INFINITE);
		
			ADD_MISC_COUNT(	timeGetTime() - t->frameDone.msTimeEventSet,
							"milliseconds since done event set");

			// Destroy managed thread.
		
			tmDestroyThread(t->mt, false);
		
			// Free thread stuff.
		
			if(eaFindAndRemove(&generalUpdate.entSend.tds, t->td) < 0){
				assert(0);
			}

			CloseHandle(t->frameStart.hEvent);
			CloseHandle(t->frameDone.hEvent);

			gslEntityUpdateThreadDataDestroy(&t->td);

			SAFE_FREE(t);
		}

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

static void gslDoThreaded(	GeneralUpdateThreadCallback callback,
							void* userPointer)
{
	U32 msTimeDiffMin = 0;
	U32 msTimeDiffMax = 0;

	if(!eaSize(&generalUpdate.threads)){
		return;
	}

	generalUpdate.lastIndexShared = -1;

	EARRAY_CONST_FOREACH_BEGIN(generalUpdate.threads, i, isize);
	{
		GeneralUpdateThread* t = generalUpdate.threads[i];
		
		t->callback = callback;
		t->userPointer = userPointer;
		t->frameStart.msTimeEventSet = timeGetTime();
		SetEvent(t->frameStart.hEvent);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(generalUpdate.threads, i, isize);
	{
		GeneralUpdateThread* t = generalUpdate.threads[i];
		
		WaitForSingleObject(t->frameDone.hEvent, INFINITE);

		if(PERFINFO_RUN_CONDITIONS){
			U32 msTimeDiff = timeGetTime() - t->frameDone.msTimeEventSet;

			if(!i){
				msTimeDiffMin = msTimeDiff;
				msTimeDiffMax = msTimeDiff;
			}else{
				MIN1(msTimeDiffMin, msTimeDiff);
				MAX1(msTimeDiffMax, msTimeDiff);
			}
		}
	}
	EARRAY_FOREACH_END;

	ADD_MISC_COUNT(msTimeDiffMin, "min ms since done event set");
	ADD_MISC_COUNT(msTimeDiffMax, "max ms since done event set");
}

static void gslSendGeneralUpdatesBegin(void){
	int i;

	// Setup the cached map state.

	for(i=eaSize(&generalUpdate.mapState.mapStatesByPartition)-1; i>0; --i)
	{
		GeneralUpdateMapStatePartition *pPart = generalUpdate.mapState.mapStatesByPartition[i];
		if (pPart) {
			pktSetWriteIndex(pPart->mapState[0].pak, 0);
			pktSetIndex(pPart->mapState[0].pak, 0);
			pktFree(&pPart->mapState[1].pak);
			pPart->mapState[0].ready = 0;
			pPart->mapState[1].ready = 0;
		}
	}
	
	generalUpdate.clientLinkCount = eaSize(&generalUpdate.clientLinks);
}

static void gslSendGeneralUpdatesThreaded(void){
	extern U32 gNoTextParserThreadCheck;

	gNoTextParserThreadCheck = 1;
	
	gslDoThreaded(gslSendGeneralUpdateInThread, NULL);

	gNoTextParserThreadCheck = 0;
}

static void gslCleanupMovement(void){
	PERFINFO_AUTO_START_FUNC();
	gslDoThreaded(mmAfterSendingToClientsInThread, NULL);
	PERFINFO_AUTO_STOP();
}

void clearEntityInPlayerProxFlag(Entity *pEnt)
{
	pEnt->nearbyPlayer = false;
}

static void gslHandlePlayerProximityLists(void)
{
	// swap the lists, the last frame's inPlayerProx list becomes the outOfPlayerProx list
	// as we process each of the update thread data's lists, 
	// the entities that are in the 'outOf' list will get moved to the 'in' list
	// and whatever is left over in the 'outOf' list need to be unmarked in the player's proximity
	{
		ImbeddedList *ptmp = generalUpdate.inPlayerProximityList;
		generalUpdate.inPlayerProximityList = generalUpdate.outOfPlayerProximityList;
		generalUpdate.outOfPlayerProximityList = ptmp;
	}

	EARRAY_CONST_FOREACH_BEGIN(generalUpdate.threads, i, isize);
		GeneralUpdateThread* t = generalUpdate.threads[i];
		gslPopulateNearbyPlayerNotifyEnts(t->td, generalUpdate.inPlayerProximityList);
	EARRAY_FOREACH_END;

	{
		ImbeddedListIterator it;
		Entity *pEnt;

		pEnt = ImbeddedList_IteratorInitialize(generalUpdate.inPlayerProximityList, &it);
		while(pEnt)
		{
			pEnt->nearbyPlayer = true;
			pEnt = ImbeddedList_IteratorGetNext(&it);
		}
	}

	// clear out what's left, as these entities are no longer in the player proximity list
	ImbeddedList_Clear(generalUpdate.outOfPlayerProximityList, clearEntityInPlayerProxFlag);
}

static void gslQueueGeneralUpdateForLinks(void){
	PERFINFO_AUTO_START_FUNC();
	eaSetSize(&generalUpdate.clientLinks, 0);
	EARRAY_CONST_FOREACH_BEGIN(gGSLState.clients.netListens, i, isize);
		linkIterate(gGSLState.clients.netListens[i], gslQueueGeneralUpdateForLink);
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

static void gslFinishGeneralUpdateForLinks(void){
	PERFINFO_AUTO_START_FUNC();
	EARRAY_CONST_FOREACH_BEGIN(gGSLState.clients.netListens, i, isize);
		linkIterate(gGSLState.clients.netListens[i], gslFinishGeneralUpdateForLink);
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

static void gslEntityUpdateEndInThreads(void){
	PERFINFO_AUTO_START_FUNC();
	gslDoThreaded(gslEntityUpdateEndInThread, generalUpdate.entSend.tds);
	PERFINFO_AUTO_STOP();
}

static S32 gslSendWaitForTrigger;
AUTO_CMD_INT(gslSendWaitForTrigger, gslSendWaitForTrigger);
static U32 gslSendTrigger;
AUTO_CMD_INT(gslSendTrigger, gslSendTrigger);

void gslSendGeneralUpdates(void)
{
	#define TIMED(x, params) STATEMENT(gslFrameTimerAddInstance(#x);x params;gslFrameTimerStopInstance(#x);)

	if(gslSendWaitForTrigger){
		if(!gslSendTrigger){
			return;
		}
		
		gslSendTrigger--;
	}

	if(!generalUpdate.inPlayerProximityList){
		generalUpdate.inPlayerProximityList = ImbeddedList_Create();
		generalUpdate.outOfPlayerProximityList = ImbeddedList_Create();
	}
	
	gslFrameTimerAddInstance("sendUpdate:begin");
	PERFINFO_AUTO_START("sendUpdate:begin", 1);
	{
		TIMED(combat_RegenerateDirtyInnates, ());
		TIMED(gslQueueGeneralUpdateForLinks, ());
		TIMED(gslCreateGeneralUpdateThreads, ());
		TIMED(gslSendGeneralUpdatesBegin, ());
		TIMED(gslEntityUpdateBegin, ());
	}
	PERFINFO_AUTO_STOP();
	gslFrameTimerStopInstance("sendUpdate:begin");

	gslFrameTimerAddInstance("sendUpdate:sendThreaded");
	PERFINFO_AUTO_START("sendUpdate:sendThreaded", 1);
	{
		gslSendGeneralUpdatesThreaded();
	}
	PERFINFO_AUTO_STOP();
	gslFrameTimerStopInstance("sendUpdate:sendThreaded");

	gslFrameTimerAddInstance("sendUpdate:end");
	PERFINFO_AUTO_START("sendUpdate:end", 1);
	{
		TIMED(gslHandlePlayerProximityLists, ());
		TIMED(gslCleanupMovement, ());
		TIMED(gslFinishGeneralUpdateForLinks, ());
		TIMED(RefSystem_CopyQueuedToPrevious, ());
		TIMED(gslEntityUpdateEnd, ());
		TIMED(gslEntityUpdateEndInThreads, ());
		TIMED(mmAllAfterSendingToClients, (gGSLState.flt));
		TIMED(mapState_ClearDirtyBits, ());
		TIMED(gslApplyMapStateDiffs, ());
	}
	PERFINFO_AUTO_STOP();
	gslFrameTimerStopInstance("sendUpdate:end");

	#undef TIMED
}

void gslSetCSRListener(Entity *e, GlobalType listenerType, ContainerID listenerID, const char *listenerName, const char *listenerAccount, U32 uSecondsToListen, AccessLevel eListenerLevel)
{
	if (e && e->pPlayer)
	{
		if (e->pPlayer->pCSRListener){
			StructDestroySafe(parse_CSRListenerInfo, &e->pPlayer->pCSRListener);
		}
		e->pPlayer->pCSRListener = StructCreate(parse_CSRListenerInfo);
		e->pPlayer->pCSRListener->listenerType = listenerType;
		e->pPlayer->pCSRListener->listenerID = listenerID;
		e->pPlayer->pCSRListener->listenerName = StructAllocString(listenerName);
		e->pPlayer->pCSRListener->listenerAccount = StructAllocString(listenerAccount);
		e->pPlayer->pCSRListener->uValidUntilTime = timeSecondsSince2000() + uSecondsToListen;
		e->pPlayer->pCSRListener->listenerAccessLevel = eListenerLevel;
	}
}

void gslObjPrint(GlobalType type, ContainerID id, const char *string)
{
	Entity *ent = entFromContainerIDAnyPartition(type, id);
	if (ent)
	{
		START_PACKET( pak1, ent, GAMESERVERLIB_CON_PRINTF );
		pktSendString( pak1, string );
		END_PACKET

		gslSendCSRFeedback(ent, string);
	}
	else if (type)
	{
		RemoteCommand_RemoteObjPrint(type, id, string);
	}
}

// Need a different version of this so that the CSR feedback can't infinitely recurse
static void gslCSRObjPrint(GlobalType type, ContainerID id, const char *string)
{
	Entity *ent = entFromContainerIDAnyPartition(type, id);
	if (ent)
	{
		START_PACKET( pak1, ent, GAMESERVERLIB_CON_PRINTF );
		pktSendString( pak1, string );
		END_PACKET
	}
	else if (type)
	{
		RemoteCommand_RemoteCSRObjPrint(type, id, string);
	}
}

// Sends feedback to the CSR rep who is executing a command on this player, if any
void gslSendCSRFeedback(Entity *ent, const char *string)
{
	if (ent && ent->pPlayer && ent->pPlayer->pCSRListener)
	{
		CSRListenerInfo *pListener = ent->pPlayer->pCSRListener;
		if (pListener->uValidUntilTime > timeSecondsSince2000()){
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			estrPrintf(&estrBuffer, "CSR: (%s,%s) %s", ent->debugName, ent->pPlayer->privateAccountName, string);

			gslCSRObjPrint(pListener->listenerType, pListener->listenerID, estrBuffer);
			objLog(LOG_CSR, pListener->listenerType, pListener->listenerID, 0, pListener->listenerName, NULL, pListener->listenerAccount,
				"CSRFeedback", NULL, "CSR: %s", string);

			estrDestroy(&estrBuffer);
		} else {
			StructDestroySafe(parse_CSRListenerInfo, &ent->pPlayer->pCSRListener);
		}
	}
}

typedef struct MessageHandlerStruct
{
	const char *title;
	const char *message;
	MessageStruct *pFmt;
} MessageHandlerStruct;

int gslBroadcastMessageToLink(NetLink *link, S32 index, ClientLink *clientLink, MessageHandlerStruct *pStruct)
{
	int i;

	if (clientLink->disconnected)
		return 1;

	for (i = 0; i < eaiSize(&clientLink->localEntities); i++)
	{
		Entity *ent = entFromEntityRefAnyPartition(clientLink->localEntities[i]);
		if (ent)
		{
			if (pStruct->pFmt)
			{
				Language entLang = entGetLanguage(ent);
				// Check Language-send restriction
				if (pStruct->pFmt->eLangSendRestriction)
				{
					if (entLang == pStruct->pFmt->eLangSendRestriction || 
						(entLang == LANGUAGE_DEFAULT && pStruct->pFmt->eLangSendRestriction == LANGUAGE_ENGLISH))
						notify_NotifySendMessageStruct(ent, kNotifyType_ServerBroadcast, pStruct->pFmt);
				}
				else
					notify_NotifySendMessageStruct(ent, kNotifyType_ServerBroadcast, pStruct->pFmt);
			}
			else
				notify_NotifySend(ent, kNotifyType_ServerBroadcast, pStruct->message, NULL, NULL);
		}
	}
	return 1;
}

void gslObjBroadcastMessage(GlobalType type, ContainerID id, const char *title, const char *string)
{
	Entity *ent = entFromContainerIDAnyPartition(type, id);
	if (type == objServerType())
	{
		MessageHandlerStruct mStruct = {0};
		
		mStruct.title = title;
		mStruct.message = string;
		
		EARRAY_CONST_FOREACH_BEGIN(gGSLState.clients.netListens, i, isize);
			linkIterate2(gGSLState.clients.netListens[i], gslBroadcastMessageToLink, &mStruct);
		EARRAY_FOREACH_END;
	}
	else if (ent)
	{
		notify_NotifySend(ent, kNotifyType_ServerBroadcast, string, NULL, NULL);
	}
	else if (type)
	{
		RemoteCommand_RemoteObjBroadcastMessage(type, id, title, string);
	}
}

void gslObjBroadcastMessageEx(GlobalType type, ContainerID id, const char *title, MessageStruct *pFmt)
{
	Entity *ent = entFromContainerIDAnyPartition(type, id);
	if (type == objServerType())
	{
		MessageHandlerStruct mStruct = {0};		
		mStruct.title = title;
		mStruct.pFmt = pFmt;		
		EARRAY_CONST_FOREACH_BEGIN(gGSLState.clients.netListens, i, isize);
			linkIterate2(gGSLState.clients.netListens[i], gslBroadcastMessageToLink, &mStruct);
		EARRAY_FOREACH_END;
	}
	else if (ent)
	{
		char *msg = NULL;
		estrStackCreate(&msg);
		entFormatMessageStruct(ent, &msg, pFmt);
		notify_NotifySend(ent, kNotifyType_ServerBroadcast, msg, NULL, NULL);
		estrDestroy(&msg);
	}
	else if (type)
	{
		RemoteCommand_RemoteObjBroadcastMessageEx(type, id, title, pFmt);
	}
}

AUTO_RUN_EARLY;
void setPrintCB(void)
{
	setObjPrintCB(gslObjPrint);
	setObjBroadcastMessageCB(gslObjBroadcastMessage);
	setObjBroadcastMessageExCB(gslObjBroadcastMessageEx);
}

void gslBootAllPlayersOnLink(NetLink *link,ClientLink *clientLink)
{
	if (clientLink->disconnected)
		return;

	EARRAY_INT_CONST_FOREACH_BEGIN(clientLink->localEntities, i, isize);
		Entity *ent = entFromEntityRefAnyPartition(clientLink->localEntities[i]);
		if (ent)
		{
			CommandLogOutPlayer(ent);
		}
	EARRAY_FOREACH_END;
}

void gslDisconnectAllOnLink(NetLink *link,ClientLink *clientLink)
{
	if(SAFE_MEMBER(clientLink, netLink)){
		assert(clientLink->netLink == link);
		linkFlushAndClose(&clientLink->netLink, "gslBootEveryone");
	}
}

AUTO_COMMAND_REMOTE;
void gslBootEveryone(bool bHardBoot)
{
	printf("Controller wants us to do %s boot\n", bHardBoot ? "hard" : "soft");
	EARRAY_CONST_FOREACH_BEGIN(gGSLState.clients.netListens, i, isize);
		linkIterate(gGSLState.clients.netListens[i], bHardBoot ? gslDisconnectAllOnLink : gslBootAllPlayersOnLink);
	EARRAY_FOREACH_END;
}

AUTO_COMMAND_REMOTE;
void RemoteCSRObjPrint(const char *pString, CmdContext *context)
{
	if (context && pString)
	{
		gslCSRObjPrint(context->clientType, context->clientID, pString);
	}
}
