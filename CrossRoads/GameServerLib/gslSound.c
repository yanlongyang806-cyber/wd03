/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios

// Note, this system should be partition safe, as of 3/23/2011
***************************************************************************/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "file.h"
#include <ctype.h>
#include "utils.h"
#include "memcheck.h"

#include "fileUtil.h"
#include "StashTable.h"
#include "timing.h"
#include "error.h"
#include "earray.h"
#include "FolderCache.h"
#include "logging.h"
#include "strings_opt.h"
#include "MemoryPool.h"
#include "stdtypes.h"
#include "timing.h"
#include "stringcache.h"
#include "textparser.h"
#include "Sound_common.h"
#include "Worldlib.h"
#include "WorldGrid.h"
#include "EntityGrid.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "Player.h"
#include "aiStruct.h"
#include "GameAccountDataCommon.h"
#include "AutoGen/Sound_common_h_ast.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"

// From UtilitiesLib, to set position
#include "cmdparse.h"
// to call all FX shortly before updating events
#include "CommandQueue.h"
#include "entCritter.h"
#include "GameEvent.h"

#include "gslSound.h"
#include "gslEncounter.h"
#include "gslMapState.h"

#include "gslEventTracker.h"
#include "referencesystem.h"
#include "ResourceInfo.h"

#include "gslOldEncounter.h"
#include "oldencounter_common.h"

#include "gslSound_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

extern ParseTable parse_GameEvent[];
#define TYPE_parse_GameEvent GameEvent

#define PERMISSION_TOKEN_NOADS	"Chat.DisableAds"

SoundServerState snd_server_state;

int g_VoiceForceAd = 0;
AUTO_CMD_INT(g_VoiceForceAd, AdForce);

typedef struct AudioStatusWatch {
	StatusWatchType type;
	GameAudioEventPair *pair;
	union {
		EntityRef ent;
		Vec3 pos;
	} source;
	EntityRef *last_entries;		// Nearby entities who are hearing the sound
	S64 s64Time;
	int partition;
} AudioStatusWatch;

SoundServerPartitionState* sndServerGetPartition(int partition, int create)
{
	SoundServerPartitionState *state = eaGet(&snd_server_state.partitions, partition);

	if(state)
		return state;

	if(!create)
		return NULL;

	state = callocStruct(SoundServerPartitionState);

	eaSet(&snd_server_state.partitions, state, partition);
	return state;
}

F32 distanceToWatch(AudioStatusWatch *watch, Vec3 pos)
{
	switch(watch->type)
	{
		xcase StatusWatch_Entity: {
			Vec3 entpos;
			Entity *ent = entFromEntityRef(watch->partition, watch->source.ent);

			if(!ent)
			{
				return FLT_MAX;
			}

			entGetPos(ent, entpos);
			return distance3(entpos, pos);
		}

		xcase StatusWatch_Pos: {
			return distance3(watch->source.pos, pos);
		}
	}

	return FLT_MAX;
}

static void entGetCloseStatusEnts(Entity *e, EntityRef **refsOut)
{
	int i;
	if(e && refsOut)
	{
		F32 maxDistance;
		
		if(e->aibase)
		{
			maxDistance = MAX(200.0, 2.0 * e->aibase->proximityRadius);
		}
		else
		{
			maxDistance = 200.0;
		}

		for(i=0; i<eaSize(&e->aibase->statusTable); i++)
		{
			AIStatusTableEntry *entry = e->aibase->statusTable[i];
			Entity *statEnt = entFromEntityRef(entGetPartitionIdx(e), entry->entRef);
			int visible = 0;

			if(entry->visible || ABS_TIME_SINCE(entry->time.lastVisible)<SEC_TO_ABS_TIME(15))
			{
				visible = 1;
			}

			if(statEnt && statEnt->pPlayer && entry->distanceFromMe < maxDistance && visible)
			{
				eaiPush(refsOut, statEnt->myRef);
			}
		}
	}
}

void getCloseEnts(AudioStatusWatch *watch, int **eaOut)
{
	switch(watch->type)
	{
		xcase StatusWatch_Entity: {
			Entity *ent = entFromEntityRef(watch->partition, watch->source.ent);

			entGetCloseStatusEnts(ent, eaOut ? eaOut : &watch->last_entries);
		}

		xcase StatusWatch_Pos: {
			int i;
			Entity **ents = NULL;
			
			entGridProximityLookupEArray(watch->partition, watch->source.pos, &ents, 1);

			for(i=0; i<eaSize(&ents); i++)
				eaiPush(eaOut ? eaOut : &watch->last_entries, ents[i]->myRef);

			eaDestroy(&ents);
		}
	}
}

static void sndServerSendEvent(AudioStatusWatch *watch, Entity *ent, GameAudioEventPair *pair)
{
	Vec3 pos;
	U32 entRef = -1;

	if(watch && watch->type==StatusWatch_Entity)
	{
		entRef = watch->source.ent;
	}

	if(!ent)
	{
		return;
	}

	if(strstri(pair->audio_event, "Music"))
	{
		ClientCmd_sndPlayMusic(ent, pair->audio_event, pair->map->filename, entRef);
	}
	else
	{
		
		if(watch && watch->type == StatusWatch_Pos)
		{
			// ideally, use the position of the game event (if we can get one)	
			copyVec3(watch->source.pos, pos);
		}
		else
		{
			// otherwise, just get the position of the entity by default
			entGetPos(ent, pos);
		}

		ClientCmd_sndPlayRemote3dV2(ent, pair->audio_event, pos[0], pos[1], pos[2], 
									pair->map->filename, entRef);
	}
}

static void sndServerStopEvent(Entity *ent, GameAudioEventPair *pair)
{
	if(!pair->one_shot)
	{
		if(strstr(pair->audio_event, "Music"))
		{
			ClientCmd_sndEndMusic(ent);
		}
		else
		{
			ClientCmd_sndStopOneShot(ent, pair->audio_event);
		}
	}
	else
	{
		ClientCmd_sndStopOneShot(ent, pair->audio_event);
	}
}

static void createEntWatch(GameAudioEventPair* pair, Entity *ent, AudioStatusWatch **watchOut)
{
	if(!ent)
		return;

	if(watchOut && ent)
	{
		int i;
		AudioStatusWatch *watch;
		SoundServerPartitionState *state = sndServerGetPartition(entGetPartitionIdx(ent), true);

		for(i=0; i<eaSize(&state->watches); i++)
		{
			watch = state->watches[i];

			if(watch->source.ent==ent->myRef && watch->pair==pair)
			{
				*watchOut = watch;
				return;	// Already have a watch for this
			}
		}
		watch = callocStruct(AudioStatusWatch);
		watch->type = StatusWatch_Entity;
		watch->source.ent = ent->myRef;
		watch->partition = entGetPartitionIdx(ent);
		watch->s64Time = ABS_TIME_PARTITION(watch->partition);
		*watchOut = watch;		
	}
}

static void createPosWatch(GameAudioEventPair* pair, GameEvent *ev, GameEvent *specific, AudioStatusWatch **watchOut)
{
	if(watchOut)
	{
		AudioStatusWatch *watch;
		watch = callocStruct(AudioStatusWatch);
		watch->type = StatusWatch_Pos;
		watch->partition = specific->iPartitionIdx;
		watch->s64Time = ABS_TIME_PARTITION(watch->partition);
		copyVec3(specific->pos, watch->source.pos);

		*watchOut = watch;		
	}
}

static void createWatch(GameAudioEventPair* pair, GameEvent *ev, GameEvent *specific, AudioStatusWatch **watchOut)
{
	AudioStatusWatch *watch = NULL;
	StatusWatchType type;
	int i, n;

	if (!specific)
		return;

	type = sndCommonGetStatusWatchType(specific);

	switch(type)
	{
		xcase StatusWatch_Entity: {
			createEntWatch(pair, specific->eaSources[0]->pEnt, &watch);
		}

		xcase StatusWatch_Pos: {
			createPosWatch(pair, ev, specific, &watch);
		}

		xcase StatusWatch_Imm_Source: {
			n = eaSize(&specific->eaSources);
			for (i = 0; i < n; i++){
				sndServerSendEvent(NULL, specific->eaSources[i]->pEnt, pair);
			}
		}

		xcase StatusWatch_Imm_All: {
			n = eaSize(&specific->eaTargets);
			for (i = 0; i < n; i++){
				int j;
				static EntityRef *nearby = NULL;
				
				eaiSetSize(&nearby, 0);
				entGetCloseStatusEnts(specific->eaTargets[i]->pEnt, &nearby);
				for(j=eaiSize(&nearby)-1; j>=0; j--)
					sndServerSendEvent(NULL, entFromEntityRef(specific->iPartitionIdx, nearby[i]), pair);
			}

			n = eaSize(&specific->eaSources);
			for(i = 0; i < n; i++){
				sndServerSendEvent(NULL, specific->eaSources[i]->pEnt, pair);
			}
		}
	}

	if(watch)
	{
		SoundServerPartitionState *state = sndServerGetPartition(specific->iPartitionIdx, true);
		watch->pair = pair;

		eaPushUnique(&state->watches, watch);

		if(watchOut)
		{
			*watchOut = watch;
		}
	}
}

static void sndServerDestroyWatch(AudioStatusWatch *watch)
{
	ea32Destroy(&watch->last_entries);
	free(watch);
}

static void sndServerEventUpdate(GameAudioEventPair* pair, GameEvent *ev, GameEvent *specific, int value)
{
	int i;
	AudioStatusWatch *watch = NULL;

	createWatch(pair, ev, specific, &watch);
	if(!watch)
	{
		return;
	}

	watch->pair = pair;

	getCloseEnts(watch, NULL);

	// Do initial send
	for(i=0; i<eaiSize(&watch->last_entries); i++)
	{
		Entity *ent = entFromEntityRef(watch->partition, watch->last_entries[i]);

		sndServerSendEvent(watch, ent, watch->pair);
	}
}

static void sndServerEventStop(GameAudioEventPair* pair, GameEvent *ev, GameEvent *specific, int value)
{
	SoundServerPartitionState *state = sndServerGetPartition(specific->iPartitionIdx, false);
	devassert(pair->end_event == ev);

	if(!state) return;

	FOR_EACH_IN_EARRAY(state->watches, AudioStatusWatch, watch)
	{
		if(watch->pair==pair)
		{
			int i;
			for(i=0; i<ea32Size(&watch->last_entries); i++)
			{
				Entity *e = entFromEntityRef(watch->partition, watch->last_entries[i]);

				if(e)
					sndServerStopEvent(e, pair);
			}
			ea32Clear(&watch->last_entries);
		}
	}
	FOR_EACH_END
}

static void sndServerEventSetCount(void* unused, GameEvent *ev, GameEvent *specific, int value)
{
	
}

static GameEvent **g_TrackedEvents = NULL;

AUTO_STARTUP(Sound);
void sndServerInit(void)
{
	loadstart_printf("Loading server sound data...");

	sndCommonCreateGAEDict();
	StructInit(parse_SoundServerState, &snd_server_state);
	loadend_printf("done.");
}

void ea32Diff(cEArray32Handle *left, cEArray32Handle *right, EArray32Handle *result)
{
	int i;
	static StashTable table = 0;

	if(!table)
	{
		table = stashTableCreateInt(20);
	}

	stashTableClear(table);

	for(i=0; i<ea32Size(right); i++)
	{
		stashIntAddInt(table, right[0][i], 1, 1);
	}

	for(i=0; i<ea32Size(left); i++)
	{
		if(!stashIntFindInt(table, left[0][i], NULL))
		{
			ea32Push(result, left[0][i]);
		}
	}
}

U32 watchIsAlive(AudioStatusWatch *watch)
{
	if(!watch)
	{
		return 0;
	}
	switch(watch->type)
	{
		xcase StatusWatch_Entity: {
			Entity *watched = entFromEntityRef(watch->partition, watch->source.ent);

			return !!watched;
		}
	}

	return 0;
}

void sndLibServerOncePerFramePerPartition(SoundServerPartitionState *state)
{
	int i;
	
	for(i=0; i<eaSize(&state->watches); i++)
	{
		AudioStatusWatch *watch = state->watches[i];

		if(!watchIsAlive(watch)) 
		{
			bool bRemove = false;

			// if pos, give it 5 secs before removing (to allow for cancel)
			if(watch->type == StatusWatch_Pos)
			{
				if( ABS_TIME_SINCE_PARTITION(watch->partition, watch->s64Time) > SEC_TO_ABS_TIME(5.0) )
				{
					bRemove = true;
				}
			}
			else
			{
				bRemove = true;
			}

			if(bRemove)
			{
				eaRemoveFast(&state->watches, i);
				i--;

				sndServerDestroyWatch(watch);
			}
		}
		else
		{
			int j;
			U32 *refArray = NULL;
			U32 *otherArray = NULL;

			getCloseEnts(watch, &otherArray);
			ea32Diff(&otherArray, &watch->last_entries, &refArray);

			for(j=0; j<ea32Size(&refArray); j++)
			{
				// New entities
				Entity *client = entFromEntityRef(watch->partition, refArray[j]);
				if(client)
				{
					sndServerSendEvent(watch, client, watch->pair);
				}
			}

			ea32Clear(&refArray);
			ea32Diff(&watch->last_entries, &otherArray, &refArray);

			for(j=0; j<ea32Size(&refArray); j++)
			{
				// Removed entities
				Entity *client = entFromEntityRef(watch->partition, refArray[j]);
				if(client)
				{
					sndServerStopEvent(client, watch->pair);
				}
			}

			ea32Clear(&watch->last_entries);
			ea32Copy(&watch->last_entries, &otherArray);
			ea32Destroy(&refArray);
			ea32Destroy(&otherArray);
		}
	}
}

bool sndServerEntNoAds(Entity *e)
{
	GameAccountDataExtract *gade = NULL;
	
	if(!e)
		return true;

	if(e)
		gade = entity_GetCachedGameAccountDataExtract(e);

	if(gade)
		return eaIndexedFindUsingString(&gade->eaTokens, PERMISSION_TOKEN_NOADS)!=-1;

	return true;
}

bool sndServerEntAdEligible(Entity *e, U32 time, U32 timeout)
{
	Player *player = entGetPlayer(e);

	if(entIsInCombat(e))
		return false;

	if(sndServerEntNoAds(e))
		return false;

	if(player->pCutscene)
		return false;

	if(time - player->lastAdvertTime < timeout)
		return false;

	return true;
}

void sndServerPlayAd(Entity *e)
{
	U32 curTime = timeSecondsSince2000();
	Player *player = entGetPlayer(e);

	if(!player)
		return;

	ClientCmd_VoicePlayAd(e, "sip:confctl-1270@trekd.vivox.com");
	player->lastAdvertTime = curTime;
}

U32 sndServerAdsEnabled(void)
{
	return gConf.bVoiceChat && gConf.bVoiceAds;
}

void sndServerMapTransfer(Entity *e)
{
	U32 curTime = timeSecondsSince2000();
	if(!sndServerAdsEnabled())
		return;

	if(!sndServerEntAdEligible(e, curTime, snd_server_state.advertMapTransitionTimeout))
		return;

	sndServerPlayAd(e);	
}

void sndLibServerOncePerFrame(F32 elapsed)
{
	static F32 countdown = 0.5;
	
	PERFINFO_AUTO_START_FUNC();

	countdown -= elapsed;
	if(countdown<0)
	{
		countdown = 0.5;

		FOR_EACH_IN_EARRAY_FORWARDS(snd_server_state.partitions, SoundServerPartitionState, state)
		{
			if(state)
				sndLibServerOncePerFramePerPartition(state);
		}
		FOR_EACH_END
	}

	if(snd_server_state.needsInitEnc)
	{
		snd_server_state.needsInitEnc = 0;

		// Force InitEncounters on next tick
		g_EncounterResetOnNextTick = true;
	}

	if(sndServerAdsEnabled())
	{
		if(!snd_server_state.nextAdvertPass)
			snd_server_state.nextAdvertPass = timeSecondsSince2000()+snd_server_state.advertTimeout;

		if(timeSecondsSince2000() > snd_server_state.nextAdvertPass || g_VoiceForceAd)
		{
			EntityIterator *iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
			Entity *e;
			U32 curTime = timeSecondsSince2000();

			while(e = EntityIteratorGetNext(iter))
			{
				if(!g_VoiceForceAd && !sndServerEntAdEligible(e, curTime, snd_server_state.advertTimeout))
					continue;

				sndServerPlayAd(e);
			}
			EntityIteratorRelease(iter);

			g_VoiceForceAd = 0;

			snd_server_state.nextAdvertPass = timeSecondsSince2000()+snd_server_state.advertTimeout;
		}
	}
	
	PERFINFO_AUTO_STOP();
}

S32 sndGAEMapShouldTrackPair(GameAudioEventPair *pair)
{
	return pair && pair->game_event && pair->audio_event && pair->audio_event[0] && !pair->invalid;
}

void sndGAEMapStartTracking(GameAudioEventMap *map)
{
	int i;

	// should prevent tracking duplicates
	if(!map->is_tracking)
	{
		sndGAEMapValidate(map, 0);

		for(i=0; i<eaSize(&map->pairs); i++)
		{
			GameAudioEventPair *pair = (GameAudioEventPair*)map->pairs[i];

			if(!sndGAEMapShouldTrackPair(pair))
				continue;
			
			if(pair->game_event->type==EventType_ClickableActive)
			{
				ErrorFilenameGroupRetroactivef(pair->map->filename, "Audio", 10, 04, 18, 2008, 
					"Game-Audio-Event Map Error: Invalid event type, clickableactive.  Change to \"Complete\".\n");
				pair->game_event->type = EventType_InteractEndActive;
			}

			if(pair->game_event->type==EventType_InteractBegin)
			{
				// Also watch for interrupt
				pair->end_event = gameevent_CopyListener(pair->game_event);
				pair->end_event->type = EventType_InteractInterrupted;
				pair->end_event->iPartitionIdx = PARTITION_ANY;
				eventtracker_StartTracking(pair->end_event, NULL, pair, sndServerEventStop, sndServerEventStop);
			}

			pair->game_event->iPartitionIdx = PARTITION_ANY;
			eventtracker_StartTracking(pair->game_event, NULL, pair, sndServerEventUpdate, sndServerEventSetCount);
		}
		map->is_tracking = 1;
	}
}

void sndGAEMapStopTracking(GameAudioEventMap *map)
{
	int i;

	if(!map->is_tracking)
		return;

	for(i=0; i<eaSize(&map->pairs); i++)
	{
		GameAudioEventPair *pair = (GameAudioEventPair*)map->pairs[i];

		if(!sndGAEMapShouldTrackPair(pair))
			continue;

		eventtracker_StopTracking(PARTITION_ANY, pair->game_event, pair);

		if(pair && pair->end_event &&
			pair->game_event->type==EventType_InteractBegin)
		{
			// Also stop interrupt
			eventtracker_StopTracking(PARTITION_ANY, pair->end_event, pair);
		}
	}

	FOR_EACH_IN_EARRAY_FORWARDS(snd_server_state.partitions, SoundServerPartitionState, state)
	{
		if(!state)
			continue;

		for(i=eaSize(&state->watches)-1; i>=0; i--)
		{
			AudioStatusWatch *watch = state->watches[i];

			if(watch->pair->map==map && watch->partition==FOR_EACH_IDX(0, state))
			{
				eaRemoveFast(&state->watches, i);
				sndServerDestroyWatch(watch);
			}
		}
	}
	FOR_EACH_END

	map->is_tracking = 0;
}

void sndServer_PartitionLoad(int iPartitionIdx)
{

}

void sndServer_PartitionUnload(int iPartitionIdx)
{
	SoundServerPartitionState *state = sndServerGetPartition(iPartitionIdx, false);

	if(!state)
		return;

	eaDestroyEx(&state->watches, sndServerDestroyWatch);
	free(state);

	eaSet(&snd_server_state.partitions, NULL, iPartitionIdx);
}

void sndServerMapLoad(ZoneMap *zmap)
{
	int i;
	int j;
	int globalGAELayersCount;
	GameAudioEventMap *map;
	ZoneMapInfo *zminfo = zmapGetInfo(NULL);
	DictionaryEArrayStruct *dictea;
	GlobalGAELayerDef *layerDef;
	GameAudioEventMap *globalMap = NULL;

	char relFileName[MAX_PATH];
	const char *mapFileName = zmapGetFilename(zmap);

	if(!mapFileName || !mapFileName[0])
	{
		return;
	}

	fileRelativePath(mapFileName, relFileName);
	getDirectoryName(relFileName);

	// load the relevant local .gaelayers into the dictionary (based on the zone map's folder)
	sndCommonLoadToGAEDict(relFileName);

	// First start by tracking all of the local GAE maps
	//  also use this loop to scan for global GAE layer map
	dictea = resDictGetEArrayStruct(g_GAEMapDict);
	for(i=0; i<eaSize(&dictea->ppReferents); i++)
	{
		map = (GameAudioEventMap*)dictea->ppReferents[i];
		if(!map->is_global)
		{
			sndGAEMapStartTracking(map);
		}

		if(!strcmpi(map->filename, GLOBAL_GAE_LAYER_FILENAME))
		{
			globalMap = map;
		}
	}

	// Found _the_ global, so track it
	if(globalMap)
	{
		sndGAEMapStartTracking(globalMap);
	}

	// Next, track any global that may be assigned to the zone map
	globalGAELayersCount = zmapInfoGetGAELayersCount(zminfo);
	for(j = 0; j < globalGAELayersCount; j++)
	{
		layerDef = zmapInfoGetGAELayerDef(zminfo, j);
		if(layerDef)
		{
			for(i=0; i<eaSize(&dictea->ppReferents); i++)
			{
				map = (GameAudioEventMap*)dictea->ppReferents[i];
				if(map->is_global)
				{
					if(!strcmpi(map->filename, layerDef->name))
					{
						sndGAEMapStartTracking(map);
						break;
					}
				}
			}
		}
	}
}

void __cdecl sndServerMapUnload(void)
{
	int i;
	DictionaryEArrayStruct *dictea;

	RefDictIterator iterator;
	Referent pReferent;

	// stop tracking any maps that are being tracked
	dictea = resDictGetEArrayStruct(g_GAEMapDict);
	for(i=0; i<eaSize(&dictea->ppReferents); i++)
	{
		GameAudioEventMap *map = (GameAudioEventMap*)dictea->ppReferents[i];
		if(map->is_tracking)
		{
			sndGAEMapStopTracking(map);
		}
	}

	// attempt to only remove non-global GAE maps
	RefSystem_InitRefDictIterator(g_GAEMapDict, &iterator);
	while ((pReferent = RefSystem_GetNextReferentFromIterator(&iterator)))
	{
		GameAudioEventMap *map = (GameAudioEventMap*)pReferent;
		if(!map->is_global)
		{
			RefSystem_RemoveReferent(pReferent, 0);
		}
	}

	// Destroy all partition states
	for(i=0; i<eaSize(&snd_server_state.partitions); i++)
		sndServer_PartitionUnload(i);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void sndServerVoiceFirstSpoken(Entity *e, const char* channelName)
{
	entLog(LOG_VOICE_METRICS, e, "Speak", "%s", channelName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void sndServerVoiceFirstListened(Entity *e, const char* channelName)
{
	entLog(LOG_VOICE_METRICS, e, "Listen", "%s", channelName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void sndServerVoiceAdPlayed(Entity *e)
{
	entLog(LOG_VOICE_METRICS, e, "AdPlay", "");
}

#include "gslSound_h_ast.c"
