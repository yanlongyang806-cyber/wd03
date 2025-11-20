/***************************************************************************



***************************************************************************/

#ifdef STUB_SOUNDLIB

void sndMusicPlayUI(const char* eventIn, const char *filename) {}
void sndMusicClearUI(void) {}

#else

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "earray.h"
#include "StringCache.h"
#include "timing.h"
#include "utils.h"

#include "sndLibPrivate.h"
#include "event_sys.h"

#include "sndFade.h"
#include "sndSource.h"
#include "sndMusic.h"

#include "sndQueue.h"

MusicState		music_state = {0};

#define EVENT_CF_TIME	1
#define MUSIC_FADE_RATE 1.0/15

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

void sndMusicCleanup(SoundSource *source)
{
	int i;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	eaFindAndRemoveFast(&music_state.playing, source);

	sndFadeManagerRemove(music_state.fadeManager, source); //->fmod_event);
	for(i=0; i<eaSize(&music_state.active); i++)
	{
		MusicFrame *mf = music_state.active[i];

		if(eaFindAndRemoveFast(&mf->active, source)!=-1)
		{
			if(eaSize(&mf->active)==0)
			{
				eaRemove(&music_state.active, i);
				i--;
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

MusicFrame* sndMusicCreateFrameForSource(SoundSource *source)
{
	MusicFrame *musicFrame = NULL;
	musicFrame = callocStruct(MusicFrame);

	if(musicFrame)
	{
		eaPush(&musicFrame->active, source);
		eaPush(&music_state.active, musicFrame);

		eaPush(&music_state.playing, source);
	}
	
	return musicFrame;
}

U32 sndMusicIsPlaying(const char* event_name, SoundSource **sourceOut)
{
	FOR_EACH_IN_EARRAY(music_state.playing, SoundSource, me)
	{
		if(me->obj.desc_name==event_name)
		{
			if(sourceOut)
				*sourceOut = me;
			
			return 1;
		}
	}
	FOR_EACH_END

	return 0;
}

U32 sndMusicIsCurrent(SoundSource *s)
{
	MusicFrame *mf = eaTail(&music_state.active);

	if(!mf)
		return false;

	return eaSize(&mf->active)==1 && mf->active[0]==s;
}

void sndMusicPlayInternal(const char *eventIn, const char *filename, U32 entRef, SourceOrigin orig)
{
	int i;
	int playing = 0;
	void *p_event = NULL;
	SoundSource *p_source = NULL;
	void *event;
	void *parent;
	char event_temp[MAX_PATH], *ev_name;
	char group_name[MAX_PATH], *group_end;
	const char *event_name = allocAddString(eventIn);
	EventMetaData *emd;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	if(!event_name || !event_name[0])
	{
		ErrorFilenamef(filename, "Null music found");
		return;
	}

	if(!sndEventExists(eventIn))
	{
		ErrorFilenamef(filename, "Invalid music found: %s", eventIn);
		return;
	}

	strcpy(event_temp, event_name);
	ev_name = strrchr(event_temp, '/');  // If this is null it means they're trying to play the root?
	if(!ev_name || !ev_name[0])  
	{
		ErrorFilenamef(filename, "Bad music found, root group: %s", event_name);
		return;
	}
	ev_name += 1;

	strcpy(group_name, event_name);
	group_end = strrchr(group_name, '/');
	*group_end = '\0';

	playing = sndMusicIsPlaying(event_name, &p_source);
	if(p_source)
		p_event = p_source->fmod_event;	

	if(playing && eaSize(&music_state.active))
	{
		// Already in the playing group, create a new frame
		MusicFrame *mf = NULL;
		SoundSource *me = p_source;
		SoundSource *activeSource = NULL;
		MusicFrame *tail = eaTail(&music_state.active);

		if(sndMusicIsCurrent(me))
		{
			// Duplicate frame, do nothing
		}
		else
		{
			// Create new frame
			MusicFrame *nf;

			nf = callocStruct(MusicFrame);
			for(i=0; i<eaSize(&tail->active); i++)
			{
				activeSource = tail->active[i];
				activeSource->is_muted = 0; 

				if(activeSource->fmod_event)
					sndFadeManagerAddMinMax(music_state.fadeManager, activeSource, SFT_EVENT, -SND_STANDARD_FADE, 0.00, 1.0);
			}
			eaPush(&nf->active, me);

			me->is_muted = 0; // un-mute

			if(me->fmod_event)
				//sndFadeManagerAddMinMax(music_state.fadeManager, me->fmod_event, SFT_EVENT, SND_STANDARD_FADE, 0.00, 1.0);
				sndFadeManagerAddMinMax(music_state.fadeManager, me, SFT_EVENT, SND_STANDARD_FADE, 0.00, 1.0);
			eaPush(&music_state.active, nf);
		}
	}
	else
	{
		int groupsize; 
		char *groupname = NULL;
		void *event_info = NULL;

		sndMusicClear(false);

		FMOD_EventSystem_GetEventInfoOnly(eventIn, &event_info);

		emd = sndFindMetaData(event_info);

		fmodEventSystemGetGroup(group_name, &parent);

		if(!parent)
		{
			return;
		}

		FMOD_EventSystem_GetGroupName(parent, &groupname);

		if(!fmodEventIsLooping(event_info))
		{
			MusicFrame *mf = NULL;
			
			SoundSource *me = sndSourceCreate(filename, "Music", eventIn, NULL, ST_MUSIC, orig, NULL, -1, false);

			if(!me)
				return;

			me->fade_level = 1.0;
			me->is_muted = 0;
			if(me->emd->queueGroup > 0)
			{
				sndQueueManagerEnqueueSoundSource(g_SoundQueueManager, me);
			}
			else
			{
				sndSourcePlay(me);
			}

			mf = callocStruct(MusicFrame);

			eaPush(&mf->active, me);
			eaPush(&music_state.active, mf);
			eaPush(&music_state.playing, me);
		}
		else if(groupname && (emd && emd->playAsGroup))   // strstri(groupname, "_Music") ||
		{
			FMOD_EventSystem_GroupGetNumEvents(parent, &groupsize);

			for(i=0; i<groupsize; i++)
			{
				SoundSource *me = NULL;
				int myid;
				char full_name[MAX_PATH];
				char *self_name = NULL;
				int result;
				result = FMOD_EventSystem_GroupGetEventByIndex(parent, i, &event);

				
				if(result!=FMOD_OK)
					continue;
				
				if(!fmodEventIsLooping(event))
					continue;

				FMOD_EventSystem_GetName(event, &self_name);

				if(!self_name)
					continue;
				
				strcpy(full_name, group_name);
				strcat(full_name, "/");
				strcat(full_name, self_name);
				me = sndSourceCreate(filename, "Music", full_name, NULL, ST_MUSIC, orig, NULL, -1, false);
				
				if(!me)
					continue;
				
				me->music.entRef = entRef;

				FMOD_EventSystem_GetSystemID(me->info_event, &myid);

				eaPush(&music_state.playing, me);

				if(me->obj.desc_name!=event_name)
				{
					me->is_muted = 1;
					me->fade_level = 0.0;
				}
				else
				{
					MusicFrame *mf = NULL;
					mf = callocStruct(MusicFrame);

					eaPush(&mf->active, me);
					eaPush(&music_state.active, mf);
				}
			}
		}
		else
		{
			SoundSource *me = NULL;
			int myid;
			MusicFrame *mf = NULL;

			me = sndSourceCreate(filename, "Music", event_name, NULL, ST_MUSIC, orig, NULL, -1, false);
			if(me)
			{
				// I'm catching the fmod_event handle as not existing at this point
				// need to figure out why, but it doesn't appear to be hurting anything
				// devassert(me->fmod_event);

				me->fade_level = 1.0;
				FMOD_EventSystem_GetSystemID(me->fmod_event, &myid);

				eaPush(&music_state.playing, me);

				mf = callocStruct(MusicFrame);
				
				me->music.entRef = entRef;
				eaPush(&mf->active, me);
				eaPush(&music_state.active, mf);
			}
		}
	}
}

void sndMusicPlayRemote(const char* eventIn, const char *filename, U32 entRef)
{
	sndMusicPlayInternal(eventIn, filename, entRef, SO_REMOTE);
}

void sndMusicPlayUI(const char* eventIn, const char *filename)
{
	sndMusicPlayInternal(eventIn, filename, -1, SO_UI);
}

void sndMusicPlayWorld(const char* eventIn, const char *filename)
{
	sndMusicPlayInternal(eventIn, filename, -1, SO_WORLD);
}

void sndMusicReplace(char *eventIn, char *filename, U32 entRef)
{
	int i;
	int playing;
	char *ev_name;
	const char *event_name = allocAddString(eventIn);
	SoundSource *source = NULL;
	MusicFrame *top = eaTail(&music_state.active);

	if(!top)
	{
		// No frame, just play as normal
		sndMusicPlayRemote(event_name, filename, entRef);
		return;
	}

	if(!event_name || !event_name[0])
	{
		return;
	}

	ev_name = strrchr(event_name, '/');

	for(i=0; i<eaSize(&music_state.playing); i++)
	{
		if(music_state.playing[i]->obj.desc_name==event_name)
		{
			// Found it
			playing = 1;
			source = music_state.playing[i];
		}
	}

	if(!playing)
	{
		// New sound group
		sndMusicPlayRemote(event_name, filename, entRef);
		return;
	}

	playing = 0;
	for(i=0; i<eaSize(&top->active); i++)
	{
		if(top->active[i]->obj.desc_name==event_name)
		{
			// Already playing
			playing = 1;
		}
		else
		{
			sndFadeManagerAdd(music_state.fadeManager, top->active[i], SFT_EVENT, -SND_STANDARD_FADE);
		}
	}

	eaClear(&top->active);
	eaPush(&top->active, source);

	if(!playing)
	{
		sndFadeManagerAdd(music_state.fadeManager, top->active[i], SFT_EVENT, SND_STANDARD_FADE);
	}
}

void sndMusicEnd(bool immediate)
{
	int i;
	MusicFrame *mf = NULL;

	if(!music_state.active)
	{
		return;
	}

	mf = eaPop(&music_state.active);

	if(!mf)
	{
		return;
	}

	if(eaSize(&music_state.active)>0)
	{
		// Fall back to a previous state
		MusicFrame *mf2;

		// First, fade out all leaving events
		for(i=0; i<eaSize(&mf->active); i++)
		{
			SoundSource *me = mf->active[i];
			sndFadeManagerAdd(music_state.fadeManager, me, SFT_EVENT, -SND_STANDARD_FADE);
		}

		mf2 = eaTail(&music_state.active);

		if(mf2)
		{
			// Fade in all new events that aren't already playing
			// (Already playing is determined by being in the fadeout list)
			for(i=0; i<eaSize(&mf2->active); i++)
			{
				SoundSource *me = mf2->active[i];
				sndFadeManagerAdd(music_state.fadeManager, me, SFT_EVENT, SND_STANDARD_FADE);
			}
		}
	}	
	else
	{
		for(i=0; i<eaSize(&music_state.playing); i++)
		{
			SoundSource *me = music_state.playing[i];
			
			me->clean_up = 1;
			if( immediate ) {
				me->immediate = 1;
			}
		}

		//sndPrintf(2, SNDDBG_MUSIC, "sndMusicEnd - eaClear(&music_state.playing)\n");

		eaClear(&music_state.active);
		eaClear(&music_state.playing);  // free() takes place in stop callback
	}

	eaDestroy(&mf->active);
	free(mf);
}

void sndMusicClearOrigin(SourceOrigin orig)
{
	int i;
	for(i=0; i<eaSize(&music_state.playing); i++)
	{
		SoundSource *source = music_state.playing[i];

		if(source->orig==orig)
		{
			//printf("DEBUG(MUSIC_CLEAN): %p %s->clean_up = 1\n", source, source->obj.desc_name);
			source->clean_up = 1;
			sndMusicCleanup(source);
		}
	}
}

void sndMusicClearUI(void)
{
	sndMusicClearOrigin(SO_UI);
}

void sndMusicClearWorld(void)
{
	sndMusicClearOrigin(SO_WORLD);
}

void sndMusicClearRemote(void)
{
	sndMusicClearOrigin(SO_REMOTE);
}

void sndMusicClear(bool immediate)
{
	// Clears all musics to nothingness
	// Probably could be written better :)  But it works simply and fairly cheaply.
	while(eaSize(&music_state.active))
	{
		sndMusicEnd(immediate);
	}
	sndMusicEnd(immediate);
}

#endif
