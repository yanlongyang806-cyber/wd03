#define GENESIS_ALLOW_OLD_HEADERS
#include "EditorServerMain.h"
#include "memlog.h"
#include "Prefs.h"
#include "error.h"
#include "wlState.h"
#include "GlobalComm.h"
#include "WorldCellStreaming.h"
#include "WorldCellStreamingPrivate.h"
#include "ResourceSystem_Internal.h"
#include "gimmeDLLWrapper.h"
#include "fileutil.h"
#include "wlGenesisMissions.h"

#define SERVER_DEBUG 1

#ifndef NO_EDITORS

MemLog serverLog;

/*
 * This function performs initialization for the editor on the server side.
 */
void editorServerInit(void)
{
	memlog_init(&serverLog);
	//journalReset();
}

// Editor server callback system
typedef struct EditorServerCallbackData
{
	EditorServerCallback pCallback;
	void *pUserdata;
} EditorServerCallbackData; 
static EditorServerCallbackData **gpCallbacks = NULL;
void editorServerRegisterCallback(EditorServerCallback pCallback, void *pUserdata)
{
	EditorServerCallbackData *pData = calloc(1, sizeof(EditorServerCallbackData));
	pData->pCallback = pCallback;
	pData->pUserdata = pUserdata;
	eaPush(&gpCallbacks, pData);
}

static void editorServerRunCallbacks(EditorServerCallbackType eType, const char *pcMapName)
{
	int i;
	for(i=0; i<eaSize(&gpCallbacks); i++)
		gpCallbacks[i]->pCallback(eType, pcMapName, gpCallbacks[i]->pUserdata);
}

static bool wlesCheckLinkHasAllLocks(int uid)
{
	int i;
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer && layerGetLocked(layer) && !layerIsGenesis(layer) && layer->lock_owner != uid)
		{
			Errorf("Operation failed! The \"%s\" layer is locked by another client!", layerGetDef(layer)->name_str);
			return false;
		}
	}
	return true;
}

/*****************************************/
/*       SERVER COMMAND HANDLERS         */
/*****************************************/

/*
 * This function handles CommandOpenMap packets.
 * FORMAT:
 *   <full path of ZoneMap file to open (including extension)>
 * PARAMS:
 *   pak - the packet after the CommandOpenMap value has been read
 */
static ServerResponseStatus serverCommandOpenMap(Packet *pak, NetLink *link)
 {
	char mapFile[MAX_PATH];
	char *mapName;
	const char *filename;

	// before doing ANYTHING, make sure that the initiator is the only one with file/layer locks
	if (!wlesCheckLinkHasAllLocks(pktLinkID(pak)))
		return StatusError;

	mapName = pktGetStringTemp(pak);

	// validate that the path is within the data directory
	if (!fileIsInDataDirs(mapName))
		return StatusError;

	// clear out the journal
	//journalReset();
	groupDefModify(NULL, UPDATE_GROUP_PROPERTIES, true);

	fileRelativePath(mapName, mapFile);
	forwardSlashes(mapFile);
	if (!worldLoadZoneMapByName(mapFile))
		return StatusError;

	if (!(filename = zmapGetFilename(NULL)))
		return StatusError;

	// write to registry
	GamePrefStoreString("LastMapPublicName", zmapInfoGetPublicName(NULL));

	return StatusSuccess;
}

// TomY ENCOUNTER_HACK
static ServerResponseStatus serverCommandSaveDummyEncounters(Packet *pak, NetLink *link)
{
	if (wl_state.encounter_hack_callback)
		wl_state.encounter_hack_callback();
	return StatusSuccess;
}

static ServerResponseStatus serverCommandGenesisGenerateMissions(Packet *pak, NetLink *link)
{
	ZoneMapInfo* zmapInfo = pktGetStruct(pak, parse_ZoneMapInfo);
	
	wl_state.genesis_generate_missions_func(zmapInfo);

	StructDestroy( parse_ZoneMapInfo, zmapInfo );
	
	return StatusSuccess;
}

static ServerResponseStatus serverCommandGenesisGenerateEpisodeMission(Packet *pak, NetLink *link)
{
	char* episodeRoot = pktGetStringTemp(pak);
	GenesisEpisode* episode = pktGetStruct(pak, parse_GenesisEpisode);

	wl_state.genesis_generate_episode_mission_func(episodeRoot, episode);

	return StatusSuccess;
}

/*
* This function reloads the current map from source
* FORMAT:
*   NULL (i.e. nothing sent after the initial save command)
* PARAMS:
*    pak - the packet after the CommandReloadFromSource value has been read
*/
static ServerResponseStatus serverCommandReloadFromSource(Packet *pak, NetLink *link)
{
	const char *filename;
	// before doing ANYTHING, make sure that the initiator is the only one with file/layer locks
	if (!wlesCheckLinkHasAllLocks(pktLinkID(pak)))
		return StatusError;

	worldGridDeleteBinsForZoneMap(zmapGetFilename(NULL));

	//journalReset();
	groupDefModify(NULL, UPDATE_GROUP_PROPERTIES, true);

	filename = zmapInfoGetFilename(NULL);
	worldLoadZoneMap(worldGetZoneMapByPublicName(filename), true, false);
	editorServerRunCallbacks(ESCT_RELOADED_FROM_SOURCE, zmapInfoGetPublicName(NULL));
	return StatusSuccess;
}

static ServerResponseStatus serverCommandUGCPublish(Packet *pak, NetLink *link)
{
	ServerResponseStatus ret = StatusSuccess;

	editorServerRunCallbacks(ESCT_PRE_PUBLISH, zmapInfoGetPublicName(NULL)); // Old map name
	editorServerRunCallbacks(ESCT_PUBLISH, zmapInfoGetPublicName(NULL)); // New map name

	return StatusSuccess;
}

/******
* LOCKING OPERATIONS
******/

typedef struct ServerLayerBinStatus
{
	NetLink *link;
	U32 req_id;
} ServerLayerBinStatus;

static void serverLayerFinishBinning(ServerLayerBinStatus *status)
{
	Packet *out_pak = pktCreate(status->link, TOCLIENT_WORLD_REPLY);
	pktSendBitsAuto(out_pak, status->req_id);
	pktSendBitsAuto(out_pak, StatusSuccess);
	pktSendBitsAuto(out_pak, 0);
	pktSendBitsAuto(out_pak, 0);
	pktSend(&out_pak);
	SAFE_FREE(status);
}

static void serverLayerBinningStatus(ServerLayerBinStatus *status, int step, int total_steps)
{
	Packet *out_pak = pktCreate(status->link, TOCLIENT_WORLD_REPLY);
	pktSendBitsAuto(out_pak, status->req_id);
	pktSendBitsAuto(out_pak, StatusInProgress);
	pktSendBitsAuto(out_pak, step);
	pktSendBitsAuto(out_pak, total_steps);
	pktSend(&out_pak);
}

static void serverRequestSendMessageUpdate(DisplayMessage *pMsg, ResourceCache *pCache)
{
	ResourceDictionary *dict = resGetDictionary( gMessageDict );
	const char *pMsgKey;
	
	assert( !pMsg->pEditorCopy );

	pMsgKey = REF_STRING_FROM_HANDLE( pMsg->hMessage );
	if (pMsgKey)
		resServerRequestSendResourceUpdate( dict, pMsgKey, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE );
}

static bool serverLayerLock(ZoneMapLayer *layer, bool lock, int lockID, NetLink *link, U32 req_id, bool forceSave, ResourceCache *pCache, bool *need_bins)
{
	assert(layer);

	// locking
	if (lock)
	{
		if (layer->lock_owner != 0)
		{
			memlog_printf(&serverLog, "Attempted to lock already locked layer \"%s\".", layerGetFilename(layer));
			printf("Attempted to lock already locked layer \"%s\".\n", layerGetFilename(layer));
			return layer->lock_owner == lockID;
		}

		layerSetMode(layer, LAYER_MODE_GROUPTREE, true, false, false);
		assert(layer->grouptree.def_lib);

		// Make sure the client has all the messages in this layer
		{
			GroupDefPropertyGroup gdg = { 0 };
			GroupDef **lib_defs = groupLibGetDefEArray(layer->grouptree.def_lib);
			int i;
			for (i = 0; i < eaSize(&lib_defs); ++i)
				eaPush(&gdg.props, &lib_defs[i]->property_structs);
			langForEachDisplayMessage(parse_GroupDefPropertyGroup, &gdg, serverRequestSendMessageUpdate, pCache);
			eaDestroy(&gdg.props);

			// Messages will get pushed later, when the reply is being sent
		}
		
		

		layer->lock_owner = lockID;
		worldIncModTime();
		memlog_printf(&serverLog, "Layer \"%s\" locked.\n", layerGetFilename(layer)); 
		printf("Layer \"%s\" locked.\n", layerGetFilename(layer)); 
		return true;
	}
	// unlocking
	else
	{
		if (layer->lock_owner == 0)
		{
			memlog_printf(&serverLog, "Attempted to unlock already unlocked layer \"%s\".\n", layerGetFilename(layer));
			printf("Attempted to unlock already unlocked layer \"%s\".\n", layerGetFilename(layer));
			return true;
		}
		if (layer->lock_owner != lockID)
		{
			memlog_printf(&serverLog, "Attempted to unlock layer \"%s\" locked by someone else.\n", layerGetFilename(layer));
			printf("Attempted to unlock layer \"%s\" locked by someone else.\n", layerGetFilename(layer));
			return false;
		}

		if (!layer->reload_pending)
		{
			ServerLayerBinStatus *status = calloc(1, sizeof(ServerLayerBinStatus));
			WorldGridBinningCallback *callback = calloc(1, sizeof(WorldGridBinningCallback));
			printf("Queuing layer reload \"%s\".\n", layerGetFilename(layer));
			layer->reload_pending = true;
			status->link = link;
			status->req_id = req_id;
			callback->callback = serverLayerFinishBinning;
			callback->userdata = status;
			layer->bin_status_callback = serverLayerBinningStatus;
			layer->bin_status_userdata = status;
			eaPush(&world_grid.binning_callbacks, callback);
			*need_bins = true;
		}

		layer->lock_owner = 0;
		worldIncModTime();
		memlog_printf(&serverLog, "Layer \"%s\" unlocked.\n", layerGetFilename(layer)); 
		return true;
	}
}

static void serverGetFiles(Packet *pak, ZoneMapLayer ***layers)
{
	int numFiles = pktGetBitsAuto(pak);
	int i;
	char fileName[MAX_PATH];
	
	for (i = 0; i < numFiles; i++)
	{	
		ZoneMapLayer *layer;

		pktGetString(pak, SAFESTR(fileName));

		if (layer = zmapGetLayerByName(NULL, fileName))
			eaPush(layers, layer);
	}
}

static ServerResponseStatus serverCommandLock(Packet *pak, ResourceCache *pCache)
{
	int i;
	ZoneMapLayer **layers = NULL;
	ServerResponseStatus ret = StatusSuccess;
	bool need_bins = false;

	serverGetFiles(pak, &layers);
	for (i = 0; i < eaSize(&layers); i++)
		if (!serverLayerLock(layers[i], true, pktLinkID(pak), NULL, 0, false, pCache, &need_bins))
			ret = StatusError;

	need_bins = true;
	
	return ret;
}

static ServerResponseStatus serverCommandUnlock(Packet *pak, U32 req_id, ResourceCache* pCache)
{
	int i;
	ZoneMapLayer **layers = NULL;
	ServerResponseStatus ret = StatusSuccess;
	bool need_bins = false;

	serverGetFiles(pak, &layers);
	for (i = 0; i < eaSize(&layers); i++)
		if (!serverLayerLock(layers[i], false, pktLinkID(pak), pktLink(pak), req_id, false, pCache, &need_bins))
			ret = StatusError;

	if (need_bins)
		ret = StatusNoReply;

	return ret;
}

void serverCommandDebugListLocks(Packet *pak, NetLink *link)
{
	int i;
	printf("----Listing locks (initiated from link %i)----\n", pktLinkID(pak));
	// layer locks
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer)
		{
			if (layerGetLocked(layer))
				printf("  Layer \"%s\" locked by %s\n", layerGetFilename(layer), layer->lock_owner == pktLinkID(pak) ? "YOU" : "someone");
			else
				printf("  Layer \"%s\" unlocked\n", layerGetFilename(layer));
		}
	}
	printf("----End lock list---\n");
}

typedef struct EditorServerQueuedReply
{
	U32 linkID;
	U32 req_id;
	ServerResponseStatus status;
} EditorServerQueuedReply;


static EditorServerQueuedReply** editorServerQueuedReplies = NULL;

/*
 * This function sends all queued replies that are safe to send.  It
 * should be called once per frame.  Replies are queued so they can be
 * sent AFTER all resource updates get sent.
 */
void editorServerSendQueuedReplies(void)
{
	int it;
	for (it = eaSize(&editorServerQueuedReplies)-1; it >= 0; --it) {
		EditorServerQueuedReply* esqr = editorServerQueuedReplies[it];
		NetLink* link;
		ResourceCache* pCache;
		Packet* out_pak;
		U32 last_update_time = 0;

		if(!wl_state.link_info_from_id_func) {
			Errorf("LinkInfo func has not been set.");
			return;
		}
		
		wl_state.link_info_from_id_func(esqr->linkID, &link, &pCache);
		if (!link || !pCache)
		{
			// Client is no longer connected -- no need to send a
			// reply.
			free( esqr );
			eaRemove(&editorServerQueuedReplies, it);
			continue;
		}
		if (resServerAreTherePendingUpdates(pCache))
		{
			// Not yet safe to send a reply -- keep the reply around
			// to send in the future.
			continue;
		}
		
		out_pak = pktCreate(link, TOCLIENT_WORLD_UPDATE);

		if (!worldSendUpdate(out_pak, false, pCache, &last_update_time))
			pktFree(&out_pak);
		else
			pktSend(&out_pak);

		out_pak = pktCreate(link, TOCLIENT_WORLD_REPLY);
		pktSendBitsAuto(out_pak, esqr->req_id);
		pktSendBitsAuto(out_pak, esqr->status);
		pktSendBitsAuto(out_pak, 0);
		pktSendBitsAuto(out_pak, 0);
		pktSend(&out_pak);

		// Reply has been sent.
		free(esqr);
		eaRemove(&editorServerQueuedReplies, it);
	}
}

/*
 * This function processes all editor packets.  We send these packets from the
 * client with an editor flag; the packets processed here will have had those
 * flags read (and hence removed from the packet) here.
 * PARAMS:
 *   pak - the (remaining) packet to process.
 */
void editorHandleGameMsg(Packet * pak, NetLink *link, ResourceCache *pCache) 
{
	ServerCommand com;
	U32 req_id;
	int timestamp;
	ServerResponseStatus status = StatusNoReply;

	if (SERVER_DEBUG)
		memlog_printf(&serverLog, "---BEGIN NEW SERVER TRANSACTION---");

	req_id = pktGetBitsAuto(pak);
	// ensure that the packet is not dependent on old data
	timestamp = pktGetBitsAuto(pak);
//	if (timestamp < worldGetModTime())
//		return;

	// determine correct code path depending on the command
	com = pktGetBitsPack(pak, 1);
	if (SERVER_DEBUG)
		memlog_printf(&serverLog, "com=[%i]", com);

	switch(com)
	{
		xcase CommandOpenMap:
			status = serverCommandOpenMap(pak, link);
		xcase CommandLock:
			status = serverCommandLock(pak, pCache);
		xcase CommandUnlock:
			status = serverCommandUnlock(pak, req_id, pCache);
		xcase CommandDebugListLocks:
			serverCommandDebugListLocks(pak, link);
		xcase CommandReloadFromSource:
			status = serverCommandReloadFromSource(pak,link);
		xcase CommandSaveDummyEncounters: // TomY ENCOUNTER_HACK
			status = serverCommandSaveDummyEncounters(pak,link);
		xcase CommandGenesisGenerateMissions:
			status = serverCommandGenesisGenerateMissions(pak, link);
		xcase CommandGenesisGenerateEpisodeMission:
			status = serverCommandGenesisGenerateEpisodeMission(pak, link);
		xcase CommandUGCPublish:
			status = serverCommandUGCPublish(pak, link);
	}

	if (status != StatusNoReply)
	{
		EditorServerQueuedReply* esqr = calloc( 1, sizeof( *esqr ));

		esqr->linkID = pktLinkID(pak);
		esqr->req_id = req_id;
		esqr->status = status;

		eaPush( &editorServerQueuedReplies, esqr );
	}

	if (SERVER_DEBUG)
		memlog_printf(&serverLog, "---END SERVER TRANSACTION---");

	// Callback after change
	switch (com)
	{
		// Don't do validation if just generation mission data.
		case CommandGenesisGenerateMissions: case CommandGenesisGenerateEpisodeMission:
			// do nothing... no layers have changed yet so I do not need
			// to update game data.
			
		xdefault:
			if(wl_state.save_map_game_callback)
				wl_state.save_map_game_callback(worldGetPrimaryMap());
	}
}

#else

void editorServerInit(void)
{
}

void editorHandleGameMsg(Packet * pak, NetLink *link, ResourceCache *pCache)
{
}

void editorServerRegisterCallback(EditorServerCallback pCallback, void *pUserdata)
{
}

void editorServerSendQueuedReplies(void)
{
}

#endif
