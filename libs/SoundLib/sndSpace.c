/***************************************************************************



***************************************************************************/

#ifndef STUB_SOUNDLIB

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "MemoryPool.h"
#include "UtilitiesLibEnums.h"
#include "wlVolumes.h"
#include "GraphicsLib.h"

#include "sndLibPrivate.h"
#include "event_sys.h"

#include "sndFade.h"
#include "sndSpace.h"
#include "sndConn.h"
#include "sndSource.h"
#include "sndDebug2.h"
#include "gDebug.h"
#include "sndCluster.h"
#include "sndSource_h_ast.h"
#include "sndQueue.h"

// Room system!
#include "entEnums.h"
#include "RoomConn.h"
#include "MapDescription.h"
#include "WorldGrid.h"

#include "sndLOD.h"

#include "SoundLib_autogen_QueuedFuncs.h"

#include "StringCache.h"

#include "sndMixer.h"


extern SoundMixer *gSndMixer;



StashTable snd_exclusive_playing = NULL;

#define SND_SRC_HYST_DIST 0
#define SND_AUDIBLE_RADIUS 20
#define SS_CLASS_NAME "SoundSpace"

#define SND_SRC_MAX_SPEED 300

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

void sndSourceGroupRemoveEvent(SoundSourceGroup *group, SoundSource *source);

MP_DEFINE(SoundSpace);

#endif

AUTO_RUN;
void sndSpaceRegisterMsgHandler(void)
{
#ifndef STUB_SOUNDLIB
	sndObjectRegisterClass(SS_CLASS_NAME, sndSpaceObjectMsgHandler);
#endif

}

#ifndef STUB_SOUNDLIB

//typedef U32 (*SoundObjectMsgHandler)(SoundObject *obj, SoundObjectMsg *msg);
U32 sndSpaceObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg)
{
	SoundSpace *space = (SoundSpace*)obj;
	switch(msg->type)
	{
		xcase SNDOBJMSG_GET_NAME: {
			if(space->type==SST_NULL)
			{
				strcpy_s(msg->out.get_name.str, msg->out.get_name.len, "NullSpace");
			}
			else
			{
				strcpy_s(msg->out.get_name.str, msg->out.get_name.len, space->obj.desc_name);
			}
		}

		xcase SNDOBJMSG_INIT: {
			
		}

		xcase SNDOBJMSG_MEMTRACKER: {
			
		}

		xcase SNDOBJMSG_DESTROY: {
			
		}

		xcase SNDOBJMSG_CLEAN: {
			
		}
	}

	return 0;
}

void sndSpaceInit(SoundSpace *space, const char *dsp_str, const char* filename, const char* group_name)
{
	int i;

	for(i=0; i<eaSize(&space_state.global_spaces); i++)
	{
		SoundSpace *other = space_state.global_spaces[i];

		if(space==other)
		{
			continue;
		}

		if(space->type==SST_NULL || other->type==SST_NULL)
		{
			continue;
		}
	}
	
	if(dsp_str && dsp_str[0])
	{
		SET_HANDLE_FROM_STRING(space_state.dsp_dict, dsp_str, space->dsp_ref);
	}

	if(!space->non_exclude && space->multiplier<=0)
	{
		ErrorFilenameGroupRetroactivef(	filename, "Group", 5, 3, 12, 2008, 
										"Found space: %s with multiplier <= 0", space->obj.desc_name);
	}
	
	sndObjectCreateByName(&space->obj, SS_CLASS_NAME, filename, group_name, "GROUPTREE");
}

SoundSpace* sndSpaceAlloc(void)
{
	SoundSpace *space;

	MP_CREATE(SoundSpace,10);

	space = MP_ALLOC(SoundSpace);

	return space;
}

void sndSpaceUnalloc(SoundSpace *space)
{
	if(space)
	{
		MP_FREE(SoundSpace, space);
	}
}

void sndSpaceCreateAndRegisterNullSpace()
{
	space_state.null_space = callocStruct(SoundSpace);
	space_state.null_space->type = SST_NULL;
	space_state.null_space->multiplier = 1;
	sndObjectCreateByName(&space_state.null_space->obj, "SoundSpace", "Sound.c", "NULLSpace", "AudioSystem");
	//eaPush(&space_state.global_spaces, space_state.null_space);

	sndSpaceRegister(space_state.null_space);
}


SoundSpace* sndSpaceCreateFromRoom(Room *room)
{
	SoundSpace *space = NULL;
	const char *event_name = NULL, *music_name = NULL;
	const char *dsp = NULL;


	space = sndSpaceAlloc();

	if(!space)
	{
		return NULL;
	}

	space->type = SST_VOLUME;

	copyVec3(room->bounds_mid, space->volume.world_mid);
	copyVec3(room->bounds_min, space->volume.world_min);
	copyVec3(room->bounds_max, space->volume.world_max);


	space->priority = room->client_volume.sound_volume_properties->priority;
	space->multiplier = room->client_volume.sound_volume_properties->multiplier;

	dsp = room->client_volume.sound_volume_properties->dsp_name;
	sndSpaceInit(space, dsp, layerGetFilename(room->layer), room->def_name);

	//sndPrintf(1, SNDDBG_SPACE, "Space created: %p %s\n", space, event_name ? event_name : "");

	// Room Tone Properties
	event_name = room->client_volume.sound_volume_properties->event_name;
	if(event_name && event_name[0])   // Create a room tone		
		sndSourceCreate(space->obj.file_name, space->obj.desc_name, event_name, space->volume.world_mid, ST_ROOM, SO_WORLD, space, -1, false);

	//sndSpaceCreateEventReverb(space);

	// Music Properties
	music_name = room->client_volume.sound_volume_properties->music_name;
	if(music_name && music_name[0]) 
	{
		space->music_name = allocAddString(music_name);
	}

	space_state.needs_rebuild = 1;

	if(g_audio_dbg.debugging)
	{
		gDebuggerObjectAddObject(g_audio_dbg.spaces, g_audio_dbg.ss_type, space, space->debug_object);
	}

	sndSpaceRegister(space);

	return space;
}

void sndSpaceRegister(SoundSpace *space)
{
	SoundMixerChannel *soundMixerChannel;

	eaPush(&space_state.global_spaces, space);

	// Create and add a mixer channel group to the mixer
	soundMixerChannel = sndMixerChannelCreate(space);
	space->soundMixerChannel = soundMixerChannel;

	sndMixerAddChannel(gSndMixer, soundMixerChannel);
}

void sndSpaceCleanConn(SoundSpace *space, SoundSpaceConnector *conn)
{
	if(conn->space1==space)
	{
		eaDestroyEx(&conn->props1.audibleConns, NULL);
		conn->space1 = NULL;
	}
	else
	{
		eaDestroyEx(&conn->props2.audibleConns, NULL);
		conn->space2 = NULL;
	}
}

void sndSpaceDestroy(SoundSpace *space)
{
	int result;
	int i;

	ASSERT_FALSE_AND_SET(space->destroyed);

	// Update the world mixer
	sndMixerRemoveChannel(gSndMixer, space->soundMixerChannel);
	sndMixerChannelDestroy(space->soundMixerChannel);


	if(space->non_exclude)
	{
		result = eaFindAndRemoveFast(&space_state.non_exclusive_spaces, space);
		devassertmsg(result!=-1, "Somehow removing non exclusive space that wasn't in the list.");
	}
	else
	{
		result = eaFindAndRemoveFast(&space_state.global_spaces, space);
		devassertmsg(result!=-1, "Destroying space that wasn't in global spaces.  Get Adam.");
	}

	for(i=0; i<eaSize(&space->ownedSources); i++)
	{
		SoundSource *source = space->ownedSources[i];

		devassert(source->ownerSpace==space);
		
		if(eaSize(&source->line.spaces))
		{
			sndSourceDelPoint(source, space);
		}
		else
		{
			//printf("DEBUG(SPACE_DESTROY): %p %s->clean_up = 1\n", source, source->obj.desc_name);
			source->clean_up = 1;

			if(g_audio_state.editor_active_func && g_audio_state.editor_active_func())
			{
				source->immediate = 1;
			}
		}

		source->room.space = NULL;
		source->ownerSpace = NULL;
	}
	eaDestroy(&space->ownedSources);

	if(space_state.current_space==space)
	{
		space_state.current_space = NULL;
	}
	
	//sndPrintf(1, SNDDBG_SPACE, "Space destroyed: %p\n", space);

	devassert(eaFind(&space_state.global_spaces, space)==-1);
	devassert(GetCurrentThreadId()==g_audio_state.main_thread_id);

	for(i=0; i<eaSize(&space->localSources); i++)
	{
		SoundSource *source = space->localSources[i];

		assert(source->originSpace==space);
		source->originSpace = NULL;
		sndSourceSetConnToListener(source, NULL);
	}

	for(i=0; i<eaSize(&space_state.sources); i++)
	{
		SoundSource *source = space_state.sources[i];
		if(source->ownerSpace && source->ownerSpace==space)
		{
			source->ownerSpace = NULL;
		}
	}

	for(i=0; i<eaSize(&space_state.global_spaces); i++)
	{
		SoundSpace *other = space_state.global_spaces[i];

		devassertmsg(other!=space, "Found freed space in global list");
	}

	for(i=0; i<eaSize(&space->connectors); i++)
	{
		SoundSpaceConnector *conn = space->connectors[i];

		sndSpaceCleanConn(space, conn);
	}

	eaClearEx(&space->cache.connList, NULL);

	// Sanity check
	if(g_audio_state.debug_level > 2)
	{
		for(i=0; i<eaSize(&space_state.sources); i++)
		{
			assertmsg(space_state.sources[i]->originSpace!=space, "Found source in destroying space.  Get Adam.");
		}
	}

	REMOVE_HANDLE(space->dsp_ref);
	sndObjectDestroy(&space->obj);
	gDebuggerObjectRemove(space->debug_object);
	sndSpaceUnalloc(space);

	space_state.needs_rebuild = 1;
}

U32 sndSpaceSortConns(const SoundSource *source, const SoundSpaceConnector **c1, const SoundSpaceConnector **c2)
{
	Vec3 listener_pos, source_origin;
	F32 d1, d2;
	const SoundSpaceConnector *conn1 = *c1, *conn2 = *c2;

	sndGetListenerPosition(listener_pos);
	sndSourceGetOrigin(source, source_origin);

	devassert(conn1);
	devassert(conn2);

	if(!conn1)
	{
		return -1;
	}
	if(!conn2)
	{
		return 1;
	}

	if(conn1->audibility!=conn2->audibility)
	{
		return SIGN(conn2->audibility-conn1->audibility);
	}
	else if(conn1->audibility==0)
	{
		return 0;
	}
	
	d1 = sndConnSimplePointDist(conn1, listener_pos);
	d2 = sndConnSimplePointDist(conn2, listener_pos);

	return SIGN(d1-d2);
}

SoundSpaceConnector* sndSpaceGetBestConn(SoundSpace *space, SoundSource *source)
{
	if(space->connectors && eaSize(&space->connectors))
	{
		eaQSort_s(space->connectors, sndSpaceSortConns, source);

		return space->connectors[0];
	}

	return NULL;
}

SoundSpaceConnector *sndSpacesGetConn(SoundSpace *space1, SoundSpace *space2)
{
	int i;
	SoundSpaceConnector **conns = NULL;

	if(!space1 || !space2)
	{
		//badness
		return NULL;
	}

	conns = eaSize(&space1->connectors) < eaSize(&space2->connectors) ? space1->connectors : space2->connectors;

	for(i=0; i<eaSize(&conns); i++)
	{
		if((!space1 || sndConnGetSource(conns[i]) == space1) && (!space2 || sndConnGetTarget(conns[i]) == space2))
		{
			return conns[i];
		}
		if((!space2 || sndConnGetSource(conns[i]) == space2) && (!space2 || sndConnGetTarget(conns[i]) == space1))
		{
			return conns[i];
		}
	}

	return NULL;
}

void sndSpaceUpdateTransmissions(SoundSpace *space, SoundSpaceConnector *conn)
{
	int i;
	assert(conn->space1==space || conn->space2==space);

	for(i=0; i<eaSize(&space->connectors); i++)
	{
		SoundSpaceConnector *other = space->connectors[i];
		SoundSpaceConnectorProperties *props = NULL;
		F32 dist = FLT_MAX;

		assert(other->space1==space || other->space2==space);

		if(other==conn) continue;

		dist = sndConnConnDist(conn, other);
		if(dist < sndConnGetSpaceProperties(conn, space)->attn_max_range)
		{
			sndConnTransmissionCreate(conn, other, space, dist);
		}
		if(dist < sndConnGetSpaceProperties(other, space)->attn_max_range)
		{
			sndConnTransmissionCreate(other, conn, space, dist);
		}
	}
}

void sndSpaceConnectConnector(SoundSpace *space1, SoundSpace *space2, SoundSpaceConnector *conn)
{
	int i;

	// determine if the two spaces are already connected
	for(i=eaSize(&space1->connectors)-1; i>=0; i--)
	{
		SoundSpaceConnector *testConn = space1->connectors[i];

		if(space2==sndConnGetOther(testConn, space1))
			return;
	}

	centerVec3(conn->world_min, conn->world_max, conn->world_mid);

	eaFindAndRemoveFast(&space1->connectors, conn);
	eaFindAndRemoveFast(&space2->connectors, conn);
	conn->space1 = space1;
	conn->space2 = space2;

	sndSpaceUpdateTransmissions(space1, conn);
	sndSpaceUpdateTransmissions(space2, conn);

	eaPush(&space1->connectors, conn);
	eaPush(&space2->connectors, conn);
}

S32 sndSpaceSortPriorityDes(const WorldVolumeEntry **left, const WorldVolumeEntry **right)
{
	F32 ls = 0.0, rs = 0.0;
	const WorldVolumeEntry *l = *left, *r = *right;
	SoundSpace *lss = l->room->sound_space, *rss = r->room->sound_space;
	int i;

	if(lss->priority!=rss->priority)
		return -(lss->priority-rss->priority);

	for(i=eaSize(&l->eaVolumes)-1; i>=0; --i) {
		if (l->eaVolumes[i]) {
			ls = wlVolumeGetSize(l->eaVolumes[i]); 
			break;
		}
	}
	for(i=eaSize(&r->eaVolumes)-1; i>=0; --i) {
		if (r->eaVolumes[i]) {
			rs = wlVolumeGetSize(r->eaVolumes[i]);
			break;
		}
	}
	ls -= rs;
	return ls > 0 ? 1 : (ls < 0 ? -1 : l-r);
}

SoundSpace* sndSpaceFind(const Vec3 pos)
{
	int i;
	SoundSpace *primary_space;
	static WorldVolumeEntry **hitSpaces = NULL;
	static U32 roomtype = 0;
	static U32 portaltype = 0;
	static U32 playable_volume_type = 0;
	const WorldVolume **volumes;
	int numVolumes;

	// TMP DISABLED
	//if(!g_audio_state.player_exists_func)
	//{
	//	return space_state.null_space;
	//}

	PERFINFO_AUTO_START_FUNC();

	if(!roomtype)
	{
		roomtype = wlVolumeTypeNameToBitMask("RoomVolume");
	}

	if(!playable_volume_type)
	{
		playable_volume_type = wlVolumeTypeNameToBitMask("Playable");
	}

	if(!portaltype)
		portaltype = wlVolumeTypeNameToBitMask("RoomPortal");

	volumes = wlVolumeCacheQuerySphereByType(g_audio_state.snd_volume_query, pos, 0, roomtype);

	if(!eaSize(&volumes))
		volumes = wlVolumeCacheQuerySphereByType(g_audio_state.snd_volume_portal_query, pos, 0, portaltype);

	numVolumes = eaSize(&volumes);
	if(numVolumes > 0)
	{
		for(i = 0; i < numVolumes; i++)
		{
			WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(volumes[i]);

			if(volume_entry && volume_entry->room && volume_entry->room->sound_space)
				eaPush(&hitSpaces, volume_entry);
		}
	} 
	else
	{
		// see if we're close (within 100ft) to a playable volume
		volumes = wlVolumeCacheQuerySphereByType(g_audio_state.snd_volume_playable_query, pos, 100, playable_volume_type);

		numVolumes = eaSize(&volumes);
		for(i = 0; i < numVolumes; i++)
		{
			WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(volumes[i]);
			if(volume_entry)
			{
				if(volume_entry->room && volume_entry->room->sound_space)
					eaPush(&hitSpaces, volume_entry);
			}
		}
	}

	if(eaSize(&hitSpaces)==0)
	{
		PERFINFO_AUTO_STOP();
		return space_state.null_space;
	}

	if(eaSize(&hitSpaces)==1)
	{
		primary_space = hitSpaces[0]->room->sound_space;
		eaClear(&hitSpaces);
		PERFINFO_AUTO_STOP();
		return primary_space;
	}

	eaQSort(hitSpaces, sndSpaceSortPriorityDes);

	primary_space = hitSpaces[0]->room->sound_space;

	eaClear(&hitSpaces);

	PERFINFO_AUTO_STOP();
	return primary_space;
}

void sndSpaceGetCenter(const SoundSpace *space, Vec3 posOut)
{
	switch(space->type)
	{
		xcase SST_VOLUME: {
			copyVec3(space->volume.world_mid, posOut);
		}
		xcase SST_SPHERE: {
			copyVec3(space->sphere.mid, posOut);
		}
	}
}

int sndSrcCmp(const SoundSource **s1, const SoundSource **s2)
{
	const SoundSource *src1 = *s1, *src2 = *s2;
	F32 dist1 = src1->distToListener, dist2 = src2->distToListener;
	int result;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(src1->has_event)
	{
		dist1 -= 1;
	}
	if(src2->has_event)
	{
		dist2 -= 1;
	}

	if(!src1->in_audible_space && src2->in_audible_space)
	{
		PERFINFO_AUTO_STOP();
		return 1;
	}
	
	if(src1->in_audible_space && !src2->in_audible_space)
	{
		PERFINFO_AUTO_STOP();
		return -1;
	}

	if(!src1->is_audible && src2->is_audible)
	{
		PERFINFO_AUTO_STOP();
		return 1;
	}

	if(src1->is_audible && !src2->is_audible)
	{
		PERFINFO_AUTO_STOP();
		return -1;
	}

	if(src1->stopped && !src2->stopped)
	{
		PERFINFO_AUTO_STOP();
		return 1;
	}
	if(src2->stopped && !src1->stopped)
	{
		PERFINFO_AUTO_STOP();
		return -1;
	}

	
	if(src1->cluster && src2->cluster)
	{
		// if both have the same cluster
		if(src1->cluster == src2->cluster)
		{
			if(src1->cluster->soundingSource == src1)
			{
				PERFINFO_AUTO_STOP();
				return -1;
			}
			if(src2->cluster->soundingSource == src2)
			{
				PERFINFO_AUTO_STOP();
				return 1;
			}
		}
		else
		{
			if(src1->cluster->soundingSource == src1 && src2->cluster->soundingSource != src2)
			{
				PERFINFO_AUTO_STOP();
				return -1;
			}
			if(src1->cluster->soundingSource != src1 && src2->cluster->soundingSource == src2)
			{
				PERFINFO_AUTO_STOP();
				return 1;
			}
		}
	}

	result = dist1 == dist2 ? src1-src2 : SIGN(dist1-dist2);
	
	PERFINFO_AUTO_STOP();

	return result;
}

F32 sndSourceGetCategoryVolume(SoundVolumes *options, SoundSource *source)
{
	return sndGetVolumeByType(options, source ? source->emd->type : SND_MAIN);
}

void sndSourceGroupUpdateVolumeAndPan(SoundSourceGroup *group, SoundSource *source)
{
	F32 pan_level = 1;
	F32 volume = 0;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (!source->is_muted)
	{
		volume = sndGetAdjustedVolumeByType(source->emd->type);

		volume *= source->fade_level;

		if(source->emd->fade2d)
		{
			volume *= source->pseudoAttenuate;
		}

		if(source->obj.debug_volume_set)
		{
			volume *= source->obj.debug_volume;
		}

		if((!source->emd || !source->emd->clickie) && source->soundMixerChannel)
		{
			volume *= source->soundMixerChannel->audibility;
		}

		if(source->orig == SO_FX && source->emd->duckable && !source->bLocalPlayer) {
			// Source Originated from a DynFx
			// The source is duckable
			// The source did not originate from the local player (i.e., it was a critter, team member, pet or other)

			// duck it
			volume *= g_audio_state.fxDuckScaleFactor;
		}
	}

	source->volume = volume;

	if (source->fmod_event)
	{
		fmodEventSetVolume(source->fmod_event, volume);

		if(!source->emd->ignorePosition)
		{
			if(source->type == ST_ROOM && source->room.space)
			{
				if(!source->emd->fade2d)
				{
					Vec3 dir;
					F32 dist;
					sndGetListenerPanPosition(dir);
					subVec3(source->virtual_pos, dir, dir);
					dist = normalVec3(dir);
					pan_level = dist/g_audio_state.hrtf_range;
					pan_level = CLAMPF32(pan_level, 0, 1);
					pan_level *= source->directionality;

					fmodEventSetPanLevel(source->fmod_event, pan_level);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

// Performs all the FMOD operations
void sndSourceGroupUpdateActiveSource(SoundSourceGroup *group, SoundSource *source, int space_update)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	sndSourceGroupUpdateVolumeAndPan(group, source);

	assert(source->has_event || source->dead);
	if(source->needs_start && !source->stopped)
	{
		if(source->emd->queueGroup > 0)
		{
			sndQueueManagerEnqueueSoundSource(g_SoundQueueManager, source);
		}
		else
		{
			sndSourcePlay(source);
		}

		// Set by sndSourceStartCB if the event is actually started by FMOD
		if(!source->started && !source->enqueued)
		{
			sndSourceGroupRemoveEvent(group, source);
			eaFindAndRemoveFast(&group->active_sources, source);
			eaPush(&group->inactive_sources, source);

			PERFINFO_AUTO_STOP();
			return;
		}
		
		source->needs_channelgroup = 1;
		source->needs_start = 0;
	}

	if(source->needs_move && (source->emd && !source->emd->clickie))
	{
		Vec3 vel = {0};
		if(source->type==ST_POINT)
		{
			copyVec3(source->point.vel, vel);
		}
		// devassert(source->fmod_event); called function checks for event
		FMOD_EventSystem_Set3DEventAttributes(source->fmod_event, source->virtual_pos, vel, source->virtual_dir);

		source->needs_move = 0;
	}

	if(source->needs_stop)
	{
		// devassert(source->fmod_event); called function checks for event
		FMOD_EventSystem_StopEvent(source->fmod_event, source->immediate);

		source->needs_stop = 0;
	}
	PERFINFO_AUTO_STOP();
}

void sndSourceGroupRemoveEvent(SoundSourceGroup *group, SoundSource *source)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(source->emd && source->emd->exclusivity_group && snd_exclusive_playing)
	{
		stashIntRemovePointer(snd_exclusive_playing, source->emd->exclusivity_group, NULL);
	}
	sndSourceRemoveEvent(source);
	source->has_event = 0;

	PERFINFO_AUTO_STOP();
}

void sndSourceGroupGrantEvent(SoundSourceGroup *group, SoundSource *source)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(source->emd && source->emd->exclusivity_group)
	{
		SoundSource *other = NULL;
		if(!snd_exclusive_playing)
		{
			snd_exclusive_playing = stashTableCreateInt(10);
		}
		if(stashIntFindPointer(snd_exclusive_playing, source->emd->exclusivity_group, &other))
		{
			if(other->emd->priority >= source->emd->priority)
			{
				sndSourceDestroy(source);

				PERFINFO_AUTO_STOP();
				return;
			}

			// Other sound loses, so tell it to stop
			other->needs_stop = true;
		}
		stashIntAddPointer(snd_exclusive_playing, source->emd->exclusivity_group, source, 1);
	}
	eaPush(&group->active_sources, source);

	source->has_event = 1;
	source->needs_start = 1;
	
	PERFINFO_AUTO_STOP();
}

#define EVENT_STEAL_MIN_FADEOUT (50) // ms

// this is where the voice stealing magic happens
//
void sndUpdateSourceGroup(SoundSourceGroup *group, int space_update)
{
	int j;
	int mp = fmodEventGetMaxPlaybacks(group->fmod_info_event);
	int open_slots;
	static SoundSource **toActive = NULL;
	bool isLooping = fmodEventIsLooping(group->fmod_info_event);

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	eaClear(&toActive);

	if(eaSize(&group->active_sources)==0 && eaSize(&group->inactive_sources)==0 && eaSize(&group->dead_sources)==0)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	// First, move inaudible out of active, won't free any up, but will eventually
	open_slots = mp - eaSize(&group->active_sources) - eaSize(&group->dead_sources);

	if(open_slots <= eaSize(&group->inactive_sources) || group->emd->streamed || isLooping)
	{
		for(j=eaSize(&group->active_sources)-1; j>=0; j--)
		{
			bool lodAudible;
			SoundSource *source = group->active_sources[j];
			assert(source->has_event);

			lodAudible = sndLODIsSourceAudible(&gSndLOD, source);

			// check audibility
			if(!source->in_audible_space || !source->is_audible || !lodAudible)
			{
				// if this is ignore3d then the only way it may be hidden is if the LOD system says so (ex: music)
				if((source->emd->ignore3d && source->type == ST_POINT && !source->is_audible) || (!source->emd->ignore3d || !lodAudible))
				{
					// make sure we don't attempt to stop & remove more than once
					if (!source->hidden &&
						!source->runtimeError)
					{
						//sndPrintf(4, SNDDBG_EVENT, "-Active: %s %p: Inaudible\n", source->obj.desc_name, source);
						//sndPrintf(4, SNDDBG_EVENT, "+Dead: %s %p: Inaudible\n", source->obj.desc_name, source);

						// devassert(source->fmod_event); called functions check for event

						if( fmodEventGetFadeOutTime(source->fmod_event) <= 0 ) {
							fmodEventSetFadeOutTime(source->fmod_event, EVENT_STEAL_MIN_FADEOUT); // smooth fade out
						}

						//printf("DEBUG(2): FMOD_EventSystem_StopEvent: %s\n", source->obj.desc_name);
						FMOD_EventSystem_StopEvent(source->fmod_event, 0);
						eaRemoveFast(&group->active_sources, j);
						eaPush(&group->dead_sources, source);
						source->dead = 1;
						source->hidden = 1;
						if(!lodAudible) source->lod_muted = 1;
					}
				}
			}
		}
	}

	if(isLooping)
	{
		open_slots = mp - eaSize(&group->active_sources) - eaSize(&group->dead_sources);

		// Next determine which sources should have events.
		eaQSort(group->inactive_sources, sndSrcCmp);
		eaQSort(group->active_sources, sndSrcCmp);

		// If sources need events and we have extra, get them.
		while(open_slots>0 && eaSize(&group->inactive_sources))
		{
			SoundSource *source = group->inactive_sources[0];

			if(source->emd->ignore3d && source->type == ST_POINT)
			{
				if(!source->is_audible)
				{
					break;
				}
			}
			else if(!(source->in_audible_space && source->is_audible) && !source->emd->ignore3d)
			{
				break;  // Shouldn't be any useful ones after here
			}
			if(!sndLODIsSourceAudible(&gSndLOD, source))
			{
				break;
			}
			if(source->stopped)
				break;

			if(source->cluster)
			{
				// are we the sounding source of the cluster?
				if(source->cluster->soundingSource != source)
				{
					break; // nope.
				}
			}

			//sndPrintf(4, SNDDBG_EVENT, "-Inactive: %s %p: Open slot\n", source->obj.desc_name, source);
			eaRemove(&group->inactive_sources, 0);
			open_slots--;

			eaPush(&toActive, source);
			//sndPrintf(4, SNDDBG_EVENT, "+Active: %s %p: Open slot\n", source->obj.desc_name, source);
		}

		// If no extra, determine if lower priority sound can die
		if(open_slots <= 0)
		{
			for(j=0; j<eaSize(&group->inactive_sources); j++)
			{
				SoundSource *source = group->inactive_sources[j];
				SoundSource *tosteal = NULL;

				if(!(source->in_audible_space && source->is_audible) && !source->emd->ignore3d)
				{
					break;  // Shouldn't be any useful ones after here
				}

				if(eaSize(&group->active_sources)==0)
				{
					// None left to steal... highest priority already in toActive
					break;
				}

				if(source->stopped)  // World source events will be stopped, waiting to be reloaded or disowned
					break;

				if(source->distToListener < group->active_sources[eaSize(&group->active_sources)-1]->distToListener)
				{
					// It is at least closer than one active source
					int k;
					for(k=eaSize(&group->active_sources)-1; k>=0; k--)
					{
						SoundSource *other = group->active_sources[k];
						if (other->hidden ||
							other->runtimeError)
						{
							// Already waiting for this one to stop
							continue;
						}

						// don't steal from the same cluster
						if(source->cluster && other->cluster && source->cluster == other->cluster)
						{
							continue;
						}

						// if this inactive source is clustered, and it is not the 'sounding source' of the cluster, then 
						// we're not going to steal it
						if(source->cluster && source->cluster->soundingSource != source)
						{
							continue;
						}

						if(source->distToListener + SND_SRC_HYST_DIST < other->distToListener)
						{
							tosteal = other;
							break;
						}
					}
				}

				if(tosteal)
				{
					// devassert(tosteal->fmod_event); called functions check for event

					if( fmodEventGetFadeOutTime(tosteal->fmod_event) <= 0 ) {
						fmodEventSetFadeOutTime(tosteal->fmod_event, EVENT_STEAL_MIN_FADEOUT); // smooth fade out
					}

					//printf("DEBUG(3): FMOD_EventSystem_StopEvent: %s\n", tosteal->obj.desc_name);
					FMOD_EventSystem_StopEvent(tosteal->fmod_event, 0);
					tosteal->hidden = 1;
					tosteal->dead = 1;

					eaPush(&group->dead_sources, tosteal);
					eaFindAndRemove(&group->active_sources, tosteal);
				}
			}
		}
	} 
	else 
	{ 
		// This is a one-shot, so move all audible inactive toActive

		for(j=eaSize(&group->inactive_sources)-1; j>=0; j--)
		{
			SoundSource *source = group->inactive_sources[j];
			bool okToPlay = true;


			if(source->emd->ignore3d && source->type == ST_POINT)
			{
				if(!source->is_audible)
				{
					okToPlay = false;
				}
			}
			else
			{
				if(!(source->in_audible_space && source->is_audible) && !source->emd->ignore3d)
				{
					okToPlay = false;
				}
			}

			if(source->stopped)
			{
				okToPlay = false;
			}

			if(okToPlay)
			{
				eaRemoveFast(&group->inactive_sources, j);
				eaPush(&toActive, source);
			}
		}
	}

	for(j=0; j<eaSize(&toActive); j++)
	{
		int result;
		SoundSource *source = toActive[j];
		result = fmodEventSystemGetEvent(source->obj.desc_name, &source->fmod_event, source);

		if(result)
		{
			if(result==FMOD_ERR_NET_CONNECT)
			{
				source->patching = 1;
			}
			if(result==FMOD_ERR_EVENT_MAXSTREAMS)
			{
				ErrorFilenameGroupRetroactivef(source->emd->project_filename, "Audio", 15, 5, 15, 2008, 
					"Reached max streams on %s.  Make sure wavebanks aren't limiting this.", source->obj.desc_name);
			}
			eaPush(&group->inactive_sources, source);
		}
		else
		{
	
			sndSourceGroupGrantEvent(group, source);
		}
	}
	//eaDestroy(&toActive);

	if(g_audio_dbg.debugging)
	{
		for(j=0; j<eaSize(&group->dead_sources); j++)
		{
			SoundSource *source = group->dead_sources[j];

			gDebuggerObjectAddObject(group->dead_object, g_audio_dbg.instance_type, source, source->debug_object);
			if(!gDebuggerObjectHasFlag(source->debug_object, g_audio_dbg.event_playing))
				gDebuggerObjectAddFlag(source->debug_object, g_audio_dbg.event_playing);
			if(!gDebuggerObjectHasFlag(source->debug_object, g_audio_dbg.event_played))
				gDebuggerObjectAddFlag(source->debug_object, g_audio_dbg.event_played);
		}

		for(j=0; j<eaSize(&group->active_sources); j++)
		{
			SoundSource *source = group->active_sources[j];

			gDebuggerObjectAddObject(group->active_object, g_audio_dbg.instance_type, source, source->debug_object);
			if(!gDebuggerObjectHasFlag(source->debug_object, g_audio_dbg.event_playing))
				gDebuggerObjectAddFlag(source->debug_object, g_audio_dbg.event_playing);
			if(!gDebuggerObjectHasFlag(source->debug_object, g_audio_dbg.event_played))
				gDebuggerObjectAddFlag(source->debug_object, g_audio_dbg.event_played);
		}

		for(j=0; j<eaSize(&group->inactive_sources); j++)
		{
			SoundSource *source = group->inactive_sources[j];

			gDebuggerObjectAddObject(group->inactive_object, g_audio_dbg.instance_type, source, source->debug_object);
			if(gDebuggerObjectHasFlag(source->debug_object, g_audio_dbg.event_playing))
				gDebuggerObjectRemoveFlag(source->debug_object, g_audio_dbg.event_playing);
		}
	}

	// For those with instances, perform moves, mutes, etc.
	for(j=eaSize(&group->dead_sources)-1; j>=0; j--)
	{
		SoundSource *source = group->dead_sources[j];

		sndSourceGroupUpdateVolumeAndPan(group, source);
	}

	for(j=eaSize(&group->active_sources)-1; j>=0; j--)
	{
		SoundSource *source = group->active_sources[j];

		sndSourceGroupUpdateActiveSource(group, source, space_update);
	}

	for(j=eaSize(&group->active_sources)-1; j>=0; j--)
	{
		SoundSource *source = group->active_sources[j];

		assert(eaFind(&source->group->active_sources, source)!=-1);
		assert(eaFind(&source->group->inactive_sources, source)==-1);
		
		if(source->clean_up && source->stopped)
		{
			// Leave source->clean_up on so it is removed when checking inactive
			sndSourceGroupRemoveEvent(group, source);
			eaRemoveFast(&group->active_sources, j);
			eaPush(&group->inactive_sources, source);
		}
		else if(source->clean_up)
		{			
			if(fmodEventIsLooping(source->info_event) || source->immediate || source->emd->type==SND_MUSIC)
			{
				// devassert(source->fmod_event); called functions check for event

				if( fmodEventGetFadeOutTime(source->fmod_event) <= 0 ) {
					fmodEventSetFadeOutTime(source->fmod_event, EVENT_STEAL_MIN_FADEOUT); // smooth fade out
				}

				//printf("DEBUG(4): FMOD_EventSystem_StopEvent: %s %d\n", source->obj.desc_name, source->immediate);
				FMOD_EventSystem_StopEvent(source->fmod_event, source->immediate);
			}
			
			if(source->immediate)
			{
				sndSourceGroupRemoveEvent(group, source);
				eaRemoveFast(&group->active_sources, j);
				eaPush(&group->inactive_sources, source);
			}
			else
			{
				//printf("DEBUG(DEAD): move source to dead list : %p %s\n", source, source->obj.desc_name);
				eaRemoveFast(&group->active_sources, j);
				eaPush(&group->dead_sources, source);
	
				source->dead = 1;
			}

		}
		else if(source->stopped)
		{
			//printf("DEBUG(source->stopped): %p %s->clean_up = 1\n", source, source->obj.desc_name);
			source->clean_up = 1;
			sndSourceGroupRemoveEvent(group, source);
			eaRemoveFast(&group->active_sources, j);
			eaPush(&group->inactive_sources, source);			
		}
	}

	for(j=eaSize(&group->dead_sources)-1; j>=0; j--)
	{
		SoundSource *source = group->dead_sources[j];

		devassert(source->dead);
		devassert(eaFind(&source->group->active_sources, source)==-1);
		devassert(eaFind(&source->group->inactive_sources, source)==-1);
		if(source->stopped)
		{
			if(source->hidden)
			{
				eaPush(&source->group->inactive_sources, source);
				eaRemoveFast(&group->dead_sources, j);
				sndSourceGroupRemoveEvent(group, source);
				source->dead = 0;
				source->hidden = 0;
				if (source->runtimeError) {
					source->clean_up = 1;
					// source->stopped = 1; // leave stopped on since we want to get rid of this event due to an error that's causing it to hog resources
				} else {
					source->stopped = 0; // clear stopped since it was removed for priority reasons
				}
			}
			else
			{
				//printf("DEBUG(!source->hidden): %p %s->clean_up = 1\n", source, source->obj.desc_name);
				source->dead = 0;
				sndSourceGroupRemoveEvent(group, source);
				source->clean_up = 1;
				eaRemoveFast(&group->dead_sources, j);
				eaPush(&group->inactive_sources, source);
			}
		}
		else if (source->fmod_event)
		{
			U32 isEventPlaying;
			U32 isEventLooping;
			U32 isInfoEventLooping;
			FmodEventInfo instanceInfo;
			int pos;
			int length;
			float percent;

			isEventPlaying = fmodEventIsPlaying(source->fmod_event);
			isEventLooping = fmodEventIsLooping(source->fmod_event);
			isInfoEventLooping = fmodEventIsLooping(source->info_event);

			// Make sure we don't have any misc events hanging around
			if(!isEventLooping && isEventPlaying)
			{
				// we do not want wavebank or instances info
				instanceInfo.maxwavebanks = 0;
				instanceInfo.wavebankinfo = NULL;
				instanceInfo.numinstances = 0;
				instanceInfo.instances = NULL;

				fmodGetFmodEventInfo(source->fmod_event, &instanceInfo);

				pos = instanceInfo.positionms;
				//length = instanceInfo.lengthms == -1 ? instanceInfo.lengthmsnoloop : instanceInfo.lengthms;
				length = instanceInfo.lengthms == -1 ? 0 : instanceInfo.lengthms;
				if(length > 0)
				{
					percent = ((float)pos / (float)length) * 100.0;			
					if(percent > 200.0)
					{
						sndEventDecLRU(source->fmod_event);
					}
				}
			}
		}
	}

	// Now do clean up
	for(j=eaSize(&group->inactive_sources)-1; j>=0; j--)
	{
		SoundSource *source = group->inactive_sources[j];

		assert(eaFind(&source->group->active_sources, source)==-1);

		if(!source->enqueued && !source->patching && (source->clean_up || !fmodEventIsLooping(source->info_event)))
		{
			source->clean_up = 0;
			if(source->orig==SO_WORLD && source->ownerSpace)  // Only clean up a WORLD source if the owner is killed
				continue;
			
			sndSourceGroupDelInstance(source->group, source);
			sndSourceDestroy(source);
		} else {
			source->patching = 0; // clear this, it will get re-set on the next frame if we try to play it and it's still patching
		}
	}

	PERFINFO_AUTO_STOP();
}

FMOD_RESULT __stdcall sndModified(void *event, int type, void *p1, void *p2, void *ud)
{
	if(FMOD_EventSystem_CBMod(type))
	{
		SoundSource *source = (SoundSource*)ud;
		FMOD_RESULT result = FMOD_EventSystem_EventGetRadius(source->info_event, &source->point.sphere.radius);
		FMOD_ErrCheckRetF(result);
	}

	return FMOD_OK;
}

static U32 sndDefIsExcluder(const char *excluder_str)
{
	int excluder = 0;

	if(excluder_str)
	{
		excluder = atoi(excluder_str);
	}

	return excluder;
}

SoundSpace *sndSphereCreate(const char *event_name, const char *excluder_str, const char *dsp_str, 
							const char *editor_group_str, const char *sound_group_str, const char *sound_group_ord, 
							const Mat4 world_mat)
{
	SoundSource *source = NULL;
	SoundSpace *space = NULL;
	U32 excluder = sndDefIsExcluder(excluder_str);

	space = sndSpaceAlloc();

	if(!space)
	{
		return NULL;
	}

	space->type = SST_SPHERE;
	copyVec3(world_mat[3],space->sphere.mid);

	if(!sound_group_str || !sndSourceFindGroup(sound_group_str, &source))
	{
		if(event_name && event_name[0])
		{
			source = sndSourceCreate("Null", editor_group_str, event_name, world_mat[3], excluder ? ST_ROOM : ST_POINT, SO_WORLD, space, -1, false);		// Point... room?
			if(source)
			{
				FMOD_EventSystem_EventGetRadius(source->info_event, &space->sphere.radius);
			}
		}
		else
		{
			space->sphere.radius = 200;  // ADAM TODO: Fix this to use a radius set in the game
		}
	}

	if(source && sound_group_str)
	{
		int ord = 0;

		if(!source->line.group_str)
		{
			sndSourceAddGroup(source, sound_group_str);
		}

		sndSourceAddPoint(source, space);

		if(sound_group_ord)
		{
			ord = atoi(sound_group_ord);
		}

		space->sphere.order = ord;
	}

	if(excluder)
	{
		eaPush(&space_state.global_spaces, space);

		space->multiplier = 1;

		sndSpaceInit(space, dsp_str, "", editor_group_str);

		space_state.needs_rebuild = 1;
	}
	else
	{
		eaPush(&space_state.non_exclusive_spaces, space);
	}

	space->non_exclude = !excluder;

	return space;
}

void sndSphereDestroy(SoundSpace *space)
{
	if(space)
	{
		if(space->pLineSoundSource)
		{
			int iSize = eaSize(&space->pLineSoundSource->line.spaces);
			if(iSize > 0)
			{
				eaFindAndRemoveFast(&space->pLineSoundSource->line.spaces, space);
				
				// see if we just removed the last one
				iSize = eaSize(&space->pLineSoundSource->line.spaces);
				if(iSize == 0)
				{
					// remove references to source
					sndSourceGroupDelInstance(space->pLineSoundSource->group, space->pLineSoundSource);
					
					sndSourceClearGroup(space->pLineSoundSource);
				}
			}
		}
		sndSpaceDestroy(space);
	}
}

const char* sndSpaceName(SoundSpace *space)
{
	return space->obj.desc_name;
}

void sndSpaceAddConnector(SoundSpace *soundSpace, SoundSpaceConnector *soundSpaceConnector)
{
	eaPush(&soundSpace->connectors, soundSpaceConnector);
}

#endif
