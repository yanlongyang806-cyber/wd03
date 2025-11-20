/***************************************************************************



***************************************************************************/


#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
 
#include "fileUtil.h"
#include "MemoryPool.h"
#include "timing.h"
#include "mathutil.h"
#include "linedist.h"
#include "StringCache.h"
#include "hashFunctions.h"

#include "sndLibPrivate.h"
#include "event_sys.h"

#include "gDebug.h"

#include "sndConn.h"
#include "sndDebug2.h"
#include "sndFade.h"
#include "sndMusic.h"
#include "sndSource.h"
#include "sndSpace.h"
#include "sndQueue.h"

#include "sndCluster.h"
#include "sndMixer.h"
#include "utilitiesLib.h"
#include "sndAnim.h"

#include "sndSource_h_ast.h"

#include "SoundLib_autogen_QueuedFuncs.h"

#define SRC_CLASS_NAME "SoundSource"

extern SoundSourceClusters gSndSourceClusters;
extern SoundMixer *gSndMixer;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

MP_DEFINE(SoundSource);

StashTable sndLineTable = 0;
StashTable sndStreamError = 0;

static void sndSourceCheckParams(SoundSource *source);
void sndSourceRemoveParams(SoundSource *source);


SoundSourceCreatedCB gSoundSourceCreatedCB;
SoundSourceDestroyedCB gSoundSourceDestroyedCB;
SoundSource *gConflictPrioritySoundSource = NULL;


AUTO_RUN;
void sndSourceRegisterMsgHandler(void)
{
#ifndef STUB_SOUNDLIB
	sndObjectRegisterClass(SRC_CLASS_NAME, sndSourceObjectMsgHandler);
#endif
}

#ifndef STUB_SOUNDLIB
//typedef U32 (*SoundObjectMsgHandler)(SoundObject *obj, SoundObjectMsg *msg);
U32 sndSourceObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg)
{
	SoundSource *source = (SoundSource*)obj;

	switch(msg->type)
	{
		xcase SNDOBJMSG_GET_NAME: {
			strcpy_s(msg->out.get_name.str, msg->out.get_name.len, source->obj.desc_name);
		}
		xcase SNDOBJMSG_INIT: {
			devassert(source->has_event);
			// devassert(source->fmod_event); called functions check for event
			obj->fmod_channel_group = fmodEventGetChannelGroup(source->fmod_event);
			obj->fmod_dsp_base = fmodChannelGroupGetDSPHead(obj->fmod_channel_group);
		}

		xcase SNDOBJMSG_CLEAN: {
			sndDebuggerCleanRefsToDSP(obj->fmod_dsp_base);
			sndDebuggerCleanRefsToDSP(obj->fmod_dsp_unit);
			sndDebuggerCleanRefsToDSP(fmodChannelGroupGetDSPHead(obj->fmod_channel_group));

			obj->fmod_channel_group = NULL;
			obj->fmod_dsp_base = NULL;
			obj->fmod_dsp_unit = NULL;
		}

		xcase SNDOBJMSG_MEMTRACKER: {
			//msg->out.memtracker.tracker = source->has_event ? fmodEventGetMemTracker(source->fmod_event) : NULL;
		}
	}

	return 0;
}

SoundSourceGroup *sndSourceFindSourceGroup(const char* full_name, void *fmod_event_info)
{
	int id;
	FMOD_RESULT result;
	SoundSourceGroup *group = NULL;
	

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(!g_audio_state.sndSourceGroupTable)
	{
		g_audio_state.sndSourceGroupTable = stashTableCreateInt(20);
	}

	result = FMOD_EventSystem_GetSystemID(fmod_event_info, &id);
	if(result)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if(stashIntFindPointer(g_audio_state.sndSourceGroupTable, id, &group))
	{
		PERFINFO_AUTO_STOP();
		return group;
	}

	group = callocStruct(SoundSourceGroup);

	group->fmod_info_event = fmod_event_info;
	group->name = allocAddString(full_name);
	group->fmod_id = id;
	group->emd = sndFindMetaData(fmod_event_info);

	eaPush(&space_state.source_groups, group);
	stashIntAddPointer(g_audio_state.sndSourceGroupTable, group->fmod_id, group, 1);

	if(g_audio_dbg.debugging)
	{
		ReferenceHandle *handle = NULL;

		fmodEventGetUserData(fmod_event_info, (void**)&handle);

		gDebuggerObjectAddVirtualObjectByHandle(handle, g_audio_dbg.group_type, "Active", &group->active_object.__handle_INTERNAL);
		gDebuggerObjectAddVirtualObjectByHandle(handle, g_audio_dbg.group_type, "Inactive", &group->inactive_object.__handle_INTERNAL);
		gDebuggerObjectAddVirtualObjectByHandle(handle, g_audio_dbg.group_type, "Dead", &group->dead_object.__handle_INTERNAL);
	}

	PERFINFO_AUTO_STOP();

	return group;
}

void sndSourceGroupDestroy(SoundSourceGroup *group)
{
	stashIntRemovePointer(g_audio_state.sndSourceGroupTable, group->fmod_id, &group);
	eaFindAndRemoveFast(&space_state.source_groups, group);

	if(g_audio_dbg.debugging)
	{
		gDebuggerObjectRemove(group->dead_object);
		gDebuggerObjectRemove(group->inactive_object);
		gDebuggerObjectRemove(group->active_object);
	}
	free(group);
}

void sndSourceGroupAddInstance(SoundSourceGroup *group, SoundSource *source)
{
	devassert(group->fmod_info_event==source->info_event);

	eaPush(&group->inactive_sources, source);
	//sndPrintf(4, SNDDBG_EVENT, "+Inactive: %s %p: Add Instance\n", source->obj.desc_name, source);

	source->group = group;
}

void sndSourceGroupDelInstance(SoundSourceGroup *group, SoundSource *source)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(source->inst_deleted)
	{
		assert(!source->group);
		PERFINFO_AUTO_STOP();
		return;
	}

	if(source->dead)
	{
		int r = eaFindAndRemoveFast(&group->dead_sources, source);
		//sndPrintf(4, SNDDBG_EVENT, "-Dead: %s %p: Delete Instance\n", source->obj.desc_name, source);
		assert(r!=-1);		
	}
	else if(source->has_event)
	{
		int r = eaFindAndRemoveFast(&group->active_sources, source);
		//sndPrintf(4, SNDDBG_EVENT, "-Active: %s %p: Delete Instance\n", source->obj.desc_name, source);
		assert(r!=-1);
	}
	else
	{
		int r = eaFindAndRemoveFast(&group->inactive_sources, source);
		//sndPrintf(4, SNDDBG_EVENT, "-Inactive: %s %p: Delete Instance\n", source->obj.desc_name, source);
		assert(r!=-1);
	}

	source->inst_deleted = 1;
	source->group = NULL;
	
	PERFINFO_AUTO_STOP();
}

void sndSourceValidate(SoundSource *source)
{
	/*
	if(source->orig!=SO_WORLD && sndEventIsStreamed(source->info_event) && !sndEventIsVoice(source->info_event))
	{
		ErrorFilenameGroupRetroactivef(	source->obj.file_name, "Audio", 5, 3, 12, 2008, 
										"Streamed event: %s from non-world, non-voice object: %s", 
										source->obj.desc_name, source->orig_name);
	}
	*/
}

void sndSourceRemoveEvent(SoundSource *source)
{
	source->obj.fmod_channel_group = NULL;
	source->obj.original_conn = NULL;
	source->obj.fmod_dsp_base = NULL;
}

static int cmpSourceFile(const SoundSource **left, const SoundSource **right)
{
	int cmp = (*left)->obj.file_name - (*right)->obj.file_name;
	return cmp ? cmp : (*left - *right);
}

SoundSource *sndSourceCreate(const char *file_name, const char *orig_name, const char *event_name, const Vec3 pos, SourceType type, SourceOrigin orig, SoundSpace *owner, U32 entRef, bool failQuietly)
{
	SoundSource *source = NULL;
	void *event_info = NULL;
	SoundSourceGroup *group = NULL;
	void *info_obj = NULL;
	EventMetaData *emd = NULL;
	bool needToSetConflictPrioritySource = false;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	// do validation checks ----------------
	
	// check for valid name
	if(!event_name || !event_name[0])
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}
	if(gbMakeBinsAndExit) 
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// check for too many sources
	if(eaSize(&space_state.sources)>20000)
	{
		static int done_source_blame = 0;
		if(!done_source_blame)
		{
			int i;
			int maxcount = 0;
			int count = 0;
			const char *str = NULL;
			const char *maxstr = NULL;

			done_source_blame = 1;

			eaQSort(space_state.sources, cmpSourceFile);
			for(i=eaSize(&space_state.sources)-1; i>=0; i--)
			{
				source = space_state.sources[i];
				if(!str)
					str = source->obj.file_name;

				if(source->obj.file_name==str)
					count++;
				else
				{
					if(count>200)
					{
						ErrorFilenameGroupRetroactivef(source->obj.file_name, "Audio", 10, 3, 26, 2009, 
							"Too many sounds from file (event was possibly: %s)", source->obj.desc_name);
					}
					str = source->obj.file_name;
				}
			}
		}
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// do we have any event info for that name?
	FMOD_EventSystem_GetEventInfoOnly(event_name, &event_info);

	if(!event_info && !failQuietly)
	{
		if(entRef > 0)
		{
			const char *pchEntName = NULL;
			if(g_audio_state.get_entity_name_func)
			{
				pchEntName = g_audio_state.get_entity_name_func(entRef);
			}

			// inform the user which entity is associated with the event
			ErrorFilenameGroupRetroactivef(file_name, "Audio", 20, 3, 26, 2009, 
				"Tried to play event for entity [%s] but couldn't find event info: %s probably doesn't exist", pchEntName, event_name); 
		}
		else
		{
			ErrorFilenameGroupRetroactivef(file_name, "Audio", 20, 3, 26, 2009, 
				"Tried to play event but couldn't find event info: %s probably doesn't exist", event_name); 
		}

		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// can we play this event?
	if(!fmodEventCanPlay(event_info))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// PriorityConflicts check
	// allows us to resolve conflicts of two or more events playing at the same time
	// Example: 
	// When Level-Up sound event plays at the same time as Mission Complete sound event
	// Design prefers Level-Up and not the Mission Complete
	
	// Check for conflicts 
	emd = sndFindMetaData(event_info);
	if(emd && emd->conflictPriority > 0)
	{
		// do we have an event playing that has a priority?
		if(gConflictPrioritySoundSource)
		{
			// yes, so see if the new event is higher priority
			if(emd->conflictPriority > gConflictPrioritySoundSource->emd->conflictPriority)
			{
				// it does, so stop the existing event
				gConflictPrioritySoundSource->needs_stop = 1;

				// keep track of the new event
				needToSetConflictPrioritySource = true;
			}
			else
			{
				PERFINFO_AUTO_STOP();
				// priority is too low, so do not create/play
				return NULL;
			}
		}
		else
		{
			// no event to compete with, but we need to keep track of this source
			needToSetConflictPrioritySource = true;
		}
	}

	// filter out events in cutscene
	if(g_audio_state.cutscene_active_func && g_audio_state.cutscene_active_func())
	{
		if(g_audio_state.fCutsceneCropDistanceScalar < 1.0)
		{
			if(emd && emd->type == SND_FX) // only SFX
			{
				F32 fMaxRadius = fmodEventGetMaxRadius(event_info);
				F32 fCropDistance = fMaxRadius * g_audio_state.fCutsceneCropDistanceScalar;
				F32 fDist = distance3(listener.camera_pos, pos);
				if(fDist > fCropDistance)
				{
					PERFINFO_AUTO_STOP();
					return NULL;
				}
			}
		}
	}

	// Allocate and setup -------------

	MP_CREATE(SoundSource, 30);

	source = MP_ALLOC(SoundSource);
	
	if(!source)
	{
		PERFINFO_AUTO_STOP();
		return NULL;		// Ran out of RAM?
	}

	group = sndSourceFindSourceGroup(event_name, event_info);
	if(!group)
	{
		eaFindAndRemoveFast(&space_state.sources, source);
		MP_FREE(SoundSource, source);
		
		PERFINFO_AUTO_STOP();

		return NULL;
	}

	source->info_event = event_info;
	source->soundMixerChannel = NULL;

	sndSourceGroupAddInstance(group, source);

	source->fade_level = 1.0; // init to full level
	source->type = type;
	source->orig = orig;
	source->directionality = 1;


	eaPush(&space_state.sources, source);

	if(owner)
	{
		eaPush(&owner->ownedSources, source);
		source->ownerSpace = owner;

		if(source->type==ST_ROOM)
		{
			source->room.space = owner;
		}
	}

	
	source->emd = emd;
	sndSourceValidate(source);

	if(source->type==ST_POINT)
	{
		copyVec3(pos, source->point.pos);

		// add 5-ft on clickie to move up from the floor
		if(source->emd && source->emd->clickie)
		{
			source->point.pos[1] += 5.0; 
		}
	}

	sndObjectCreateByName(&source->obj, SRC_CLASS_NAME, file_name, event_name, orig_name);
	fmodEventGetUserData(source->info_event, &info_obj);

	sndSourceCheckParams(source);

	if(gSoundSourceCreatedCB.soundSourceCreatedFunc) {
		gSoundSourceCreatedCB.soundSourceCreatedFunc(source, gSoundSourceCreatedCB.userData);
	}

	sndClustersAddSource(&gSndSourceClusters, source);

	// remember to store the conflict
	if(needToSetConflictPrioritySource)
	{
		gConflictPrioritySoundSource = source;
	}

	//sndPrintf(5, SNDDBG_EVENT, "SourceCreate: %s - %p - %p - %p - %s\n", source->obj.desc_name, source, source->info_event, source->originSpace, source->obj.file_name);

	PERFINFO_AUTO_STOP();

	return source;
}


void sndSourceSetCreatedCB(SoundSourceCreatedFunc func, void* userData)
{
	gSoundSourceCreatedCB.soundSourceCreatedFunc = func;
	gSoundSourceCreatedCB.userData = userData;
}
 
void sndSourceSetDestroyedCB(SoundSourceDestroyedFunc func, void* userData)
{
	gSoundSourceDestroyedCB.soundSourceDestroyedFunc = func;
	gSoundSourceDestroyedCB.userData = userData;
}

void sndSourcePointFree(SoundSource *source)
{
	int i;
	assert(source->type==ST_POINT);

	switch(source->orig)
	{
		xcase SO_FX: {
			stashIntRemovePointer(fxSounds, source->point.effect.guid, NULL);
		}
		xcase SO_REMOTE: {
			stashRemovePointer(externalSounds, source->obj.desc_name, NULL);
		}
		xcase SO_WORLD: {
			for(i=0; i<eaSize(&space_state.non_exclusive_spaces); i++)
			{
				SoundSpace *space = space_state.non_exclusive_spaces[i];

				eaFindAndRemoveFast(&space->localSources, source);
			}
		}
	}
}

void sndSourceAddGroup(SoundSource *source, const char* group)
{
	source->line.group_str = strdup(group);
	stashAddPointer(sndLineTable, group, source, 1);
}

void sndSourceClearGroup(SoundSource *source)
{
	eaClear(&source->line.spaces);
	if(source->line.group_str)
	{
		stashRemovePointer(sndLineTable, source->line.group_str, NULL);
		SAFE_FREE(source->line.group_str);
	}
}

int sndSourceFindGroup(const char *group, SoundSource **source)
{	
	if(!sndLineTable)
	{
		sndLineTable = stashTableCreateWithStringKeys(20, StashDeepCopyKeys_NeverRelease);
	}
	
	return stashFindPointer(sndLineTable, group, source);
}

void sndSourceClearGroups()
{
	FOR_EACH_IN_STASHTABLE2(sndLineTable, pElement)
		SoundSource *pSoundSource = stashElementGetPointer(pElement);
		sndSourceClearGroup(pSoundSource);
	FOR_EACH_END
}

void sndSourceFree(SoundSource *source)
{
	S32 rmIdx;

	devassert(source->destroyed);
	devassert(GetCurrentThreadId()==g_audio_state.main_thread_id);

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(source->originSpace)
	{
		eaFindAndRemoveFast(&source->originSpace->localSources, source);
	}

	if(source->connToListener)
	{
		eaFindAndRemoveFast(&source->connToListener->transmitted, source);
	}

	if(source->type==ST_POINT)
	{
		sndSourcePointFree(source);
	}

	if(g_audio_state.debug_level > 2)
	{
		int i;
		for(i=0; i<eaSize(&space_state.global_spaces); i++)
		{
			assertmsg(eaFind(&space_state.global_spaces[i]->localSources, source)==-1, "Found destroying source in space.  Get Adam.");
		}
	}

	devassert(!source->has_event);
	source->originSpace = NULL;

	devassert(eaFind(&space_state.sources, source)==-1);

	if(source->has_event)
	{
		devassert(!fmodEventIsPlaying(source->fmod_event));
	}

	sndObjectDestroy(&source->obj);
	if(gDebuggerObjectIs(source->debug_object))
	{
		SoundSourceDebugPersistInfo *saved_info = StructAlloc(parse_SoundSourceDebugPersistInfo);
		copyVec3(source->virtual_dir, saved_info->dir);
		copyVec3(source->virtual_pos, saved_info->pos);
		saved_info->type = source->type;
		if(source->type==ST_POINT)
		{
			copyVec3(source->point.vel, saved_info->vel);
		}
		FMOD_EventSystem_EventGetRadius(source->info_event, &saved_info->radius);
		saved_info->directionality = source->directionality;
		saved_info->memory_usage_max = source->memory_usage_max;
		saved_info->info_event = source->info_event;
		gDebuggerObjectUnlink(source->debug_object, saved_info, parse_SoundSourceDebugPersistInfo);

		if(g_audio_dbg.debugged_source == source)
		{
			g_audio_dbg.debugged_source = NULL;
		}
	}

	if (source->onStreamedList &&
		0 <= (rmIdx = eaFind(&g_audio_state.streamed_sources, source)))
	{
		char *sourceName = NULL;
		if (source->fmod_event)
			fmodEventGetName(source->fmod_event, &sourceName);
		ErrorDetailsf("Name: %s", sourceName ? sourceName : "NULL");
		Errorf("Freeing a SoundSource that is a member of g_audio_state.streamed_sources!");
	}

	sndSourceRemoveParams(source);
	MP_FREE(SoundSource, source);

	PERFINFO_AUTO_STOP();
}

void sndSourceDestroy(SoundSource *source)
{
	SoundSourceGroup *group = NULL;
	
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	//int i;
	// Clean up connections, spaces, etc...
	//sndPrintf(2, SNDDBG_EVENT, "SourceDestroy: %s - %p - %p - %p - %s\n", source->obj.desc_name, source, source->info_event, source->originSpace, source->obj.file_name);
	
	if(gSoundSourceDestroyedCB.soundSourceDestroyedFunc) {
		gSoundSourceDestroyedCB.soundSourceDestroyedFunc(source, gSoundSourceDestroyedCB.userData);
	}

	// should we clear the conflict priority slot?
	if(gConflictPrioritySoundSource == source)
	{
		gConflictPrioritySoundSource = NULL;
	}

	sndClustersRemoveSource(&gSndSourceClusters, source);
	
	// detach from mixer
	if(source->soundMixerChannel)
	{
		sndMixerChannelRemoveSource(source->soundMixerChannel, source);
	}
	
	sndSourceClearGroup(source);
	

	if(eaSize(&source->line.spaces))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	ASSERT_FALSE_AND_SET(source->destroyed);

	//eaPush(&space_state.destroyed.sources, source);

	eaFindAndRemoveFast(&space_state.sources, source);

	sndMusicCleanup(source);

	

	if(source->ownerSpace)
	{
		int result = eaFindAndRemoveFast(&source->ownerSpace->ownedSources, source);
		assert(result!=-1);
		source->ownerSpace = NULL;
	}

	if(source->has_event)
	{
		devassert(!fmodEventIsPlaying(source->fmod_event));
	}

	// NOTE: the fmod_event is set to NULL if the event was stopped (see sndSourceStopCB)
	if (source->fmod_event) {
		void* pUserData = NULL;
		if (fmodEventGetUserData(source->fmod_event, &pUserData) == FMOD_OK &&
			source == pUserData)
		{
			// this should just handle edge cases where the event wasn't stopped 1st
			fmodEventSetUserData(source->fmod_event, NULL);
		}
	}

	sndSourceFree(source);

	PERFINFO_AUTO_STOP();
}

FMOD_RESULT F_CALLBACK sndSourceStolenCB(void *event, int type, void* p1, void *p2, void* data)
{
	// While this appears to look dangerous whenever FMOD calls this on an
	// event with a source (UserData) FMOD will also call the sndSourceStoppedCB
	// on that same event. This is likely why we don't see an additional work
	// being done here on the SoundSource.

	return FMOD_OK;

	/*
	// making the early out more obvious by commenting this out (not sure why it used to be included)

	if(g_audio_state.d_reloading)
	{
		return FMOD_OK;
	}

	fmodEventGetUserData(event, &source);

	if (source)
	{
		ASSERT_FALSE_AND_SET(((SoundSource*)source)->stolen);
	}

	return FMOD_OK;
	*/
}

static int cmpSource(const void** arg1, const void** arg2)
{
    const SoundSource** left = (const SoundSource**)arg1;
    const SoundSource** right = (const SoundSource**)arg2;
	return stricmp((*left)->obj.desc_name,(*right)->obj.desc_name);
}

FMOD_RESULT F_CALLBACK sndSourceStartCB(void *event, int type, void* p1, void *p2, void* data)
{
    void* userData = NULL;
	SoundSource *source = NULL;
	void *info_event = NULL;
	static int last_editor_active = 0;

	if(g_audio_state.d_reloading)
	{
		return FMOD_OK;
	}

	fmodEventGetUserData(event, &userData);
    source = (SoundSource*)userData;

	if(!source)
	{
		if (!isCrashed()) {
			if (g_audio_state.debug_level > 2) {
				devassert(!"Event without user data found");
			} else {
				char *eventName = NULL;
				FMOD_RESULT result = fmodEventGetName(event, &eventName);
				if (result != FMOD_OK) {
					ErrorDetailsf("FMOD GetName Error: %s", fmodGetErrorText(result));
				} else {
					ErrorDetailsf("Name: %s", eventName);
				}
				Errorf("Event without user data found in %s", __FUNCTION__);
			}
		}
	}
	else
	{
		source->stopped = 0;
		source->started = 1;
	}

	fmodEventGetInfoEvent(event, &info_event);

	if(!isProductionMode())
	{
		if(info_event && sndEventIsStreamed(info_event))
		{
			int nonMusicStreams = 0;
			int musicStreams = 0;
			int i;

			eaPush(&g_audio_state.streamed_sources, source);
			source->onStreamedList = 1;

			for(i = 0; i < eaSize(&g_audio_state.streamed_sources); i++)
			{
				if(g_audio_state.streamed_sources[i]->emd->type != SND_MUSIC)
				{
					nonMusicStreams++;
				}
				else
				{
					musicStreams++;
				}
			}

			if(nonMusicStreams + (int)(!!musicStreams) > 4)
			{
				int hash;
				char *streams = NULL;
				
				eaQSort(g_audio_state.streamed_sources, cmpSource);  // Make sure they are in some sort of order

				estrPrintf(&streams, "%s (%s) %s (%s) %s (%s) %s (%s) %s (%s)", 
					g_audio_state.streamed_sources[0]->obj.desc_name,
					g_audio_state.streamed_sources[0]->obj.orig_name,
					g_audio_state.streamed_sources[1]->obj.desc_name,
					g_audio_state.streamed_sources[1]->obj.orig_name,
					g_audio_state.streamed_sources[2]->obj.desc_name,
					g_audio_state.streamed_sources[2]->obj.orig_name,
					g_audio_state.streamed_sources[3]->obj.desc_name,
					g_audio_state.streamed_sources[3]->obj.orig_name,
					g_audio_state.streamed_sources[4]->obj.desc_name,
					g_audio_state.streamed_sources[4]->obj.orig_name);

				hash = hashStringInsensitive(streams);

				if(!sndStreamError)
				{
					sndStreamError = stashTableCreateInt(5);
				}

				if(!stashIntFindInt(sndStreamError, hash, NULL))
				{
					stashIntAddInt(sndStreamError, hash, 1, 1);
					ErrorFilenameGroupRetroactiveDeferredf(source ? source->obj.file_name : "null", "Audio", 10, 3, 12, 2008, 
						"Reached max number of streams: %s\n", streams);
				}

				estrDestroy(&streams);
			}
		}
	}

	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK sndSourceStopCB(void *event, int type, void* p1, void *p2, void* data)
{
    void* userData = NULL;
	SoundSource *source = NULL;

	if(g_audio_state.d_reloading)
	{
		return FMOD_OK;
	}

	fmodEventGetUserData(event, &userData);
    source = (SoundSource*)userData;

	if(!source)
	{
		if (!isCrashed()) {
			if (g_audio_state.debug_level > 2) {
				devassert(!"Event without user data found");
			} else {
				char *eventName = NULL;
				FMOD_RESULT result = fmodEventGetName(event, &eventName);
				if (result != FMOD_OK) {
					ErrorDetailsf("FMOD GetName Error: %s", fmodGetErrorText(result));
				} else {
					ErrorDetailsf("Name: %s", eventName);
				}
				Errorf("Event without user data found in %s", __FUNCTION__);
			}
		}
	}
	else
	{
		ASSERT_FALSE_AND_SET(source->stopped);
		ASSERT_TRUE_AND_RESET(source->started);

		if(source->soundMixerChannel)
		{
			sndMixerChannelRemoveSource(source->soundMixerChannel, source);
		}

		if(source->enqueued)
		{
			sndQueueManagerSoundSourceFinished(g_SoundQueueManager, source);
		}

		if(source->updatePosFromEnt)
		{
			// add to watch list
			sndAnimRemoveSource(g_SoundAnim, source);
		}

		if (source->drivesContactDialogAnim)
		{
			sndAnimRemoveContactDialogSource(source);
		}

		if (source->onStreamedList)
		{
			eaFindAndRemoveFast(&g_audio_state.streamed_sources, source);
			source->onStreamedList = 0;
		}

		// Don't keep a link between the source & event after FMOD has stopped an event
		// We do this since FMOD may reuse that same handle giving us faulty links
		fmodEventSetUserData(event, NULL);
		source->fmod_event = NULL;
	}

	return FMOD_OK;
}

int sortSphereSpaces(const void **s1, const void **s2)
{
    const SoundSpace **space1 = (const SoundSpace**)s1;
    const SoundSpace **space2 = (const SoundSpace**)s2;

	Vec3 pos, sp1, sp2;
	F32 y1, y2;

	if((*space1)->sphere.order!=(*space2)->sphere.order)
	{
		return (*space1)->sphere.order-(*space2)->sphere.order;
	}

	sndGetPlayerPosition(pos);

	sndSpaceGetCenter(*space1, sp1);
	sndSpaceGetCenter(*space2, sp2);

	subVec3(sp1, pos, sp1);
	subVec3(sp2, pos, sp2);
	
	y1 = getVec3Yaw(sp1);
	y2 = getVec3Yaw(sp2);

	return (y1 - y2 > 0) ? 1 : ((y1 - y2 < 0) ? -1 : 0);
}

void sndSourcePreprocess(SoundSource *source)
{
	int iNumSpaces = eaSize(&source->line.spaces);

	if(source->type == ST_POINT && iNumSpaces > 0)
	{
		Vec3 vBestPos;
		Vec3 vListenerPos;
		
		if(iNumSpaces == 1)
		{
			sndSpaceGetCenter(source->line.spaces[0], vBestPos);
			sndSourceMove(source, vBestPos, NULL, NULL);
		}
		else
		{
			int i;
			F32 fMinDistance = FLT_MAX;
			Vec3 vSpace1, vSpace2, vSpace3;
			Vec3 vPos, vPos2;
			F32 fDistance;
			int iMinIndex;

			sndGetListenerSpacePos(vListenerPos);

			eaQSort(source->line.spaces, sortSphereSpaces);

			// find the nearest point
			for(i = 0; i < iNumSpaces; i++)
			{
				sndSpaceGetCenter(source->line.spaces[i], vPos);
				fDistance = distance3(vListenerPos, vPos);
				if(fDistance < fMinDistance)
				{
					fMinDistance = fDistance;
					iMinIndex = i;
				}
			}

			if(iMinIndex == 0) // beginning
			{
				// get distance to line segment
				sndSpaceGetCenter(source->line.spaces[0], vSpace1);
				sndSpaceGetCenter(source->line.spaces[1], vSpace2);
				pointLineDistSquared(vListenerPos, vSpace1, vSpace2, vBestPos);
			}
			else if(iMinIndex == iNumSpaces-1) // end
			{
				// get distance to line segment
				sndSpaceGetCenter(source->line.spaces[iMinIndex-1], vSpace1);
				sndSpaceGetCenter(source->line.spaces[iMinIndex], vSpace2);
				pointLineDistSquared(vListenerPos, vSpace1, vSpace2, vBestPos);
			}
			else // interpolate
			{
				sndSpaceGetCenter(source->line.spaces[iMinIndex-1], vSpace1);
				sndSpaceGetCenter(source->line.spaces[iMinIndex], vSpace2);
				sndSpaceGetCenter(source->line.spaces[iMinIndex+1], vSpace3);

				// get virtual line points
				pointLineDistSquared(vListenerPos, vSpace1, vSpace2, vPos);
				pointLineDistSquared(vListenerPos, vSpace2, vSpace3, vPos2);
				
				pointLineDistSquared(vListenerPos, vPos, vPos2, vBestPos);
			}

			sndSourceMove(source, vBestPos, NULL, NULL);
		}
	}
}

// Called for a sound changing spaces
void sndSourceChangeSpaces(SoundSource* source, SoundSpace *new_space)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	//sndPrintf(2, SNDDBG_EVENT | SNDDBG_SPACE, "Source %p changing spaces: %p -> %p\n", source, source->originSpace, new_space);

	if(source->originSpace)
	{
		eaFindAndRemoveFast(&source->originSpace->localSources, source);
		
		gDebuggerObjectRemoveChild(source->originSpace->debug_object, source->debug_object);
	}
	source->originSpace = new_space;
	sndSourceSetConnToListener(source, NULL);
	eaPush(&source->originSpace->localSources, source);
	source->needs_channelgroup = 1;

	if(new_space)
	{
		gDebuggerObjectAddObject(new_space->debug_object, g_audio_dbg.instance_type, source, source->debug_object);
	}

	PERFINFO_AUTO_STOP();
}

void sndSourceCheckAudibility(SoundSource* source)
{
	U32 originAudible = source->originSpace && source->originSpace->is_audible;
	U32 shouldBeAudible = (source->type==ST_POINT || source->type==ST_ROOM) ? originAudible : 1;

	PERFINFO_AUTO_START_FUNC();

	if(source->emd->ignore3d)
	{
		if(!source->started && !source->stopped)
		{
			source->needs_start	= 1;
		}
	}
	else if(shouldBeAudible && source->in_audible_space)
	{
		if(!source->unmuted)
		{
			source->unmuted = 1;
			source->is_muted = 0;
		}
	} 
	else if(shouldBeAudible && !source->in_audible_space)
	{
		source->in_audible_space = 1;

		if(source->emd && source->emd->clickie)
		{
			
		}
		else
		{
			if(source->is_muted)
			{
				source->is_muted = 0;
			}
			else
			{
				source->needs_start = 1;
			}
		}
		
	}
	else if(!shouldBeAudible && source->in_audible_space)
	{
		if(source->emd && source->emd->clickie)
		{
			
		}
		else
		{
			source->in_audible_space = 0;
			source->is_muted = 1;
		}
	}
	else 
	{
		// No need to do anything
	}

	PERFINFO_AUTO_STOP();
}

void sndSourceGetOrigin(const SoundSource *source, Vec3 posOut)
{
	switch(source->type)
	{
		xcase ST_ROOM:	{
			if(source->room.space)
			{
				sndSpaceGetCenter(source->room.space, posOut);
			}
		}
		xcase ST_POINT: {
			copyVec3(source->point.pos, posOut);
		}
	}
}

F32 sndSourceConnDist(int index, SoundSpaceConnector ***conns, F32 ***points, F32 radius, F32 *directionality)
{
	F32 realdist = distance3((*points)[index], (*points)[index+1]);
	F32 dampdist = 0;
	int multiplier = 1;
	SoundSpaceConnector *conn = (*conns)[index];

	if(conn)
	{
		SoundSpace *space = sndConnGetTarget(conn);
		if(space)
		{
			multiplier = space->multiplier;
		}
	}
	else
	{
		SoundSpace *space;
		if(index==0 && eaSize(conns)>2)
		{
			space = sndConnGetSource((*conns)[index+1]);
			if(space)
			{
				multiplier = space->multiplier;
			}
		}
		if(index==eaSize(conns)-1 && eaSize(conns)>2)
		{
			space = sndConnGetTarget((*conns)[index+1]);
			if(space)
			{
				multiplier = space->multiplier;
			}
		}
	}

	realdist *= multiplier;

	if(conn)
	{
		SoundSpaceConnectorProperties *props = sndConnGetTargetProps((*conns)[index]);
		if(props && props->attn_max_range>0 && props->attn_0db_range < props->attn_max_range)
		{
			dampdist = radius * (realdist - props->attn_0db_range)/(props->attn_max_range-props->attn_0db_range);
			MAX1(dampdist, 0);
		}
		else if(props && props->attn_0db_range>props->attn_max_range)
		{
			dampdist = realdist < props->attn_max_range ? 0 : radius;
		}
		else
		{
			dampdist = radius;
		}

		if(directionality)
		{
			if(eaSize(conns)==3)
			{
				// Single link only
				*directionality = realdist / g_audio_state.directionality_range;
				MIN1(*directionality, 1);
			}
			else
			{
				*directionality = 1;
			}
		}
	}

	return realdist + dampdist;
}

void sndSourceSetConnToListener(SoundSource *source, SoundSpaceConnector *conn)
{
	if(source->connToListener!=conn)
	{
		if(source->connToListener)
		{
			eaFindAndRemoveFast(&source->connToListener->transmitted, source);
		}
		source->connToListener = conn;
		if(source->connToListener)
		{
			eaPush(&source->connToListener->transmitted, source);
		}
		source->needs_channelgroup = 1;
	}
}

U32 sndSourceIsVisible(SoundSource *source)
{
	//if(source->connToListener && sndConnGetSource(source->connToListener)->is_nearby)
	// do some test
	return 0;
}

void sndSourceAddPoint(SoundSource *source, SoundSpace *space)
{
	eaPush(&source->line.spaces, space);

	space->pLineSoundSource = source;
}

void sndSourceDelPoint(SoundSource *source, SoundSpace *space)
{
	eaFindAndRemoveFast(&source->line.spaces, space);
}

void sndSourceRemoveParams(SoundSource *source)
{
	int i;

	for(i=0; i<eaSize(&source->gps); i++)
	{
		GlobalParam *gp = source->gps[i];

		eaFindAndRemoveFast(&gp->sources, source);
	}

	eaDestroy(&source->gps);
}

//typedef FMOD_RESULT (F_CALLBACK *sndEventCallback)(void *e, int type, void *p1, void *p2, void *ud);
static FMOD_RESULT F_CALLBACK sndRemoveParam(void *e, int type, void *p1, void *p2, void *ud)
{
	void *source = NULL;
	fmodEventGetUserData(e, &source);

	if(!source)
	{
		return FMOD_OK;
	}

	sndSourceRemoveParams((SoundSource*)source);

	return FMOD_OK;
}

static void sndSourceCheckParams(SoundSource *source)
{
	int i;
	
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	for(i=0; i<eaSize(&globalParams); i++)
	{
		GlobalParam *gp = globalParams[i];

		if(fmodEventHasParam(source->info_event, gp->param_name))
		{
			eaPush(&gp->sources, source);
			eaPush(&source->gps, gp);
			// devassert(source->fmod_event); called function checks for this via FMOD_Result
			fmodEventSetParam(source->fmod_event, gp->param_name, gp->value_func());
		}
	}

	PERFINFO_AUTO_STOP();
}

int sndSourcePlay(ACMD_POINTER SoundSource *source)
{
	FMOD_RESULT result = 0;
	void *event_info = NULL;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

//#if _XBOX
	{
		int currentMem;
		int maxUsedMem;

		// make sure we have a significant amount of memory available - otherwise FMOD may become unstable
		FMOD_EventSystem_GetMemStats(&currentMem, &maxUsedMem);
		if(currentMem > soundBufferSize * 0.85)
		{
			sndMixerIncEventsSkippedDueToMemory(gSndMixer); // track for debugging

			PERFINFO_AUTO_STOP();
			return 0;
		}
	}
//#endif

	// Music Test
	if(source->emd->music)
	{
		if(!sndMusicIsPlaying(source->obj.desc_name, NULL))
			sndMusicEnd(false);
		if(!sndMusicIsCurrent(source))
			sndMusicCreateFrameForSource(source);
	}

	if(source->emd->clickie)
	{
		// head relative
		Vec3 listenerPos;
		
		sndGetListenerPosition(listenerPos);
		subVec3(listenerPos, source->virtual_pos, source->relative_pos);
	
		// devassert(source->fmod_event); called function checks for event
		FMOD_EventSystem_Set3DEventAttributes(source->fmod_event, source->relative_pos, NULL, NULL);
	}
	else
	{
		// absolute - world relative
		// devassert(source->fmod_event); called function checks for event
		FMOD_EventSystem_Set3DEventAttributes(source->fmod_event, source->virtual_pos, NULL, NULL);
	}
	
	result = sndMixerPlayEvent(gSndMixer, source);

	if(result==FMOD_ERR_EVENT_MAXSTREAMS)
	{
		ErrorFilenameGroupRetroactivef(source->emd->project_filename, "Audio", 15, 5, 15, 2008, 
			"Reached max streams on %s.  Make sure wavebanks aren't limiting this.", source->obj.desc_name);
	}
	else if(result!=FMOD_OK)
	{
		if(result==FMOD_ERR_EVENT_MISMATCH)
			ErrorFilenamef(source->emd->project_filename, "FSB and FEV mismatch for event: %s.  Please rebuild.", source->obj.desc_name);
		else if(result!=FMOD_ERR_MEMORY && result!=FMOD_ERR_INVALID_PARAM && result!= FMOD_ERR_UNSUPPORTED && result!= FMOD_ERR_INVALID_HANDLE)
			ErrorFilenamef(source->obj.file_name, "Error playing event: %s!\n", FMOD_ErrorString(result));
		else if(result == FMOD_ERR_MEMORY)
		{
			//sndPrintf(2, SNDDBG_EVENT, "Memory Error %s %p\n", source->obj.desc_name, source);
		}

		PERFINFO_AUTO_STOP();
		return 0;
	}
 
	if(result == FMOD_OK && source->fmod_event)
	{
		// valid event
		fmodEventSetUserData(source->fmod_event, source);

		// do we attach a analysis DSP?
		if(source->emd && source->emd->type == SND_VOICE && source->emd->animate)
		{
			void *soundSourceCG = fmodEventGetChannelGroup(source->fmod_event);
			
			if(sndAnalysisCreateDSP(source, &source->ampAnalysisDSP) == FMOD_OK)
			{
				// add the DSP
				fmodChannelGroupAddDSP(soundSourceCG, source->ampAnalysisDSP);
			}
		}
	}
	
	// devassert(source->fmod_event); called function checks for event
	fmodEventGetInfoEvent(source->fmod_event, &event_info);
	if(event_info)
	{
		fmodEventSetCanPlay(event_info, 0);
	}
	
	if(source->lod_muted)
	{
		source->fade_level = 0.0; // start faded down
		sndFadeManagerAddMinMax(music_state.fadeManager, source, SFT_LOD, 0.1, 0.00, 1.0);
		source->lod_muted = 0;
	}

	PERFINFO_AUTO_STOP();
	return 1;
}

void sndSourceValidatePlaying(void)
{
	void **events = NULL;
	int i;
	PerfInfoGuard* guard;

	PERFINFO_AUTO_START_FUNC_GUARD(&guard);
	fmodEventSystemGetPlaying(&events);
	for(i=0; i<eaSize(&events); i++)
	{
        void* userData = NULL;
		SoundSource *source = NULL;
		char *name = NULL;

		fmodEventGetUserData(events[i], &userData);
        source = (SoundSource*)userData;

		FMOD_EventSystem_GetName(events[i], &name);

		assert(source);
		assert(source->fmod_event==events[i]);
		assert(source->has_event);
		assert(!source->stopped && source->started);
		assert(strstri(source->obj.desc_name, name));
	}

	PERFINFO_AUTO_STOP_GUARD(&guard);
}


static void printSource2(SoundSource *source, Vec3 fmod_pos)
{
	printf("%s at %3.2f %3.2f %3.2f | %3.2f %3.2f %3.2f: type %d\n", 
		source->obj.desc_name, 
		vecParamsXYZ(source->virtual_pos),
		vecParamsXYZ(fmod_pos), 
		source->type);
}

void sndPrintPlaying(void)
{
	int i;
	void **events = NULL;
	fmodEventSystemGetPlaying(&events);

	for(i=0; i<eaSize(&events); i++)
	{
		SoundSource *source = NULL;
		Vec3 fmod_pos;

		fmodEventGetUserData(events[i], &source);
		FMOD_EventSystem_Get3DEventAttributes(events[i], fmod_pos, NULL, NULL);
		if(!source)
		{
			continue;
		}

		printSource2(source, fmod_pos);
	}
}

void sndSourceMove(SoundSource *source, Vec3 new_pos, Vec3 new_vel, Vec3 new_dir)
{
	assert(source->type==ST_POINT);

	if(source->cluster)
	{
		sndClustersRemoveSource(&gSndSourceClusters, source);
	}

	// set the new position
	copyVec3(new_pos, source->point.pos);

	source->moved = 1;
}

void sndSourceGetFile(void *event_data, char *buffer, int len)
{
	SoundSource *source = (SoundSource*)event_data;

	strcpy_s(buffer, len, source->obj.orig_name);
}


#define SND_SRC_MAX_SPEED (50)

// returns distance moved
F32 sndSourceUpdatePosition(SoundSource *source)
{
	F32 dist;
	bool movementChecked = false;

	// Are we tracking the position of an entity for this source?
	if(source->updatePosFromEnt)
	{
		// Get the position of the entity
		Vec3 entityPos;
		if(g_audio_state.get_entity_pos_func && g_audio_state.get_entity_pos_func(source->point.entRef, entityPos))
		{
			// ok, the position appears to be valid, so update the virtual_pos of the source
			copyVec3(entityPos, source->virtual_pos);
			source->virtual_pos[1] += 5.0; // Add 5ft to keep it from the floor
		}
	}

	// Handle moving the source ----------

	// Did the position of the source change?
	// Set speed limit
	dist = 0;
	if(!sameVec3(source->virtual_pos, source->last_virtual_pos) || !source->last_virtual_time)
	{ 
		// calculate velocity for point sources
		if(source->type == ST_POINT)
		{
			F32 timeDelta; 

			timeDelta = timerSeconds(timerCpuTicks() - source->last_virtual_time);

			if(timeDelta > 0) 
			{
				Vec3 tmpVec, tmpVec2;
				Vec3 deltaPos;

				// get delta pos (scaled by time)
				subVec3(source->virtual_pos, source->last_virtual_pos, deltaPos);
				scaleVec3(deltaPos, 1.0 / timeDelta, deltaPos);

				dist = distance3(source->virtual_pos, source->last_virtual_pos);

				// averaging filter
				scaleVec3(deltaPos, 0.25, tmpVec);
				scaleVec3(source->point.last_vel, 0.75, tmpVec2);
				addVec3(tmpVec, tmpVec2, source->point.vel);

				scaleVec3(source->point.vel, 0.5, source->point.vel);

				copyVec3(source->point.vel, source->point.last_vel);
			}
		}

		source->needs_move = 1;
		source->moved = 1;
	} 
	else
	{
		source->moved = 0;
	}


	source->last_virtual_time = timerCpuTicks(); //ABS_TIME;
	copyVec3(source->virtual_pos, source->last_virtual_pos);

	// ---------------------------------------
	return dist;
}

#endif

#include "sndSource_h_ast.c"
