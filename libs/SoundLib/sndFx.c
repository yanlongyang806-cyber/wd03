#ifndef STUB_SOUNDLIB

#include "sndFx.h"

// Cryptic Structs
#include "earray.h"
#include "estring.h"
#include "mathutil.h"
#include "timing.h"
#include "MemoryPool.h"

// DynFx
#include "dynFxParticle.h" // For getting pos and vel of event/effect
#include "dynFxInfo.h" // For verification of soundevents
#include "../dynFx.h" 
#include "dynFxInterface.h"

// Sound structs
#include "sndLibPrivate.h"
#include "event_sys.h"
#include "sndSource.h"
#include "sndSpace.h"
#include "sndConn.h"



// Memory Budget
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

// Memory Pool
MP_DEFINE(SoundFx);

// Globals
StashTable disabledFXFiles = 0;
StashTable fxSounds = 0;				// FX sounds that still have a valid particle/guid

SoundFx *sndFxCreate()
{
	SoundFx *soundFx;
	
	MP_CREATE(SoundFx, 32);
	soundFx = MP_ALLOC(SoundFx);
	
	return soundFx;
}

void sndFxDestroy(SoundFx *soundFx)
{
	if(soundFx->eventPath)
	{
		free(soundFx->eventPath);
	}
	eaDestroy(&soundFx->eaSoundSources);
	
	MP_FREE(SoundFx, soundFx);
}


// Update any snd fx that need frame by frame updates
void sndFxOncePerFrame()
{
	
}

// Convert a string to lower case.
const char *strlower(char *str)
{
	char *start = str;
	for (; *str; ++str)
		*str = tolower(*str);
	return start;
}

void sndFxSplitStringWithDelim(const char *str, const char *delim, char ***eaOutput)
{
	char *token;
	char *strCopy = strdup(str);
	char *ptr = strCopy;

	do {
		token = strsep2(&ptr, delim, NULL);
		if(token)
		{
			eaPush(eaOutput, strdup(token));
		}
	} while(token);

	free(strCopy);
}

void sndInvalidateFxFile(const char *file_name)
{
	if(!disabledFXFiles)
	{
		disabledFXFiles = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	}

	stashAddInt(disabledFXFiles, file_name, 1, 1);
}

int sndFxCmdGetGuid(DynFx *dynFx)
{
	static int guid = 0;
	SoundFx *soundFx;

	guid++;
	if(guid==0)
	{
		guid++;
	}
	
	soundFx = sndFxCreate();
	soundFx->ident = guid;
	ADD_SIMPLE_POINTER_REFERENCE_DYN(soundFx->hDynFx, dynFx);
	soundFx->bLocalPlayer = dynFx->pManager->bLocalPlayer;

	stashIntAddPointer(fxSounds, guid, soundFx, 1);

	return guid;
}


SoundSource *sndFxAddSoundSource(SoundFx *soundFx, char *eventPath, char *fileName, Vec3 pos)
{
	SoundSource *source = NULL;
	
	// basic protection from trying to create an invalid event
	//if(!eventPath) return;
	//if(*eventPath == NULL) return;

	// 4 - create source
	source = sndSourceCreate(fileName, fileName, eventPath, pos, ST_POINT, SO_FX, NULL, -1, false);
	if(source)
	{
		source->point.effect.guid = soundFx->ident;
		source->bLocalPlayer = soundFx->bLocalPlayer; // copy the flag

		// 5 - add it to the list
		eaPush(&soundFx->eaSoundSources, source);
	}

	return source;
}

bool sndFxProcessCommand(SoundFx *soundFx, char ***eaArguments, Vec3 pos, char *file_name)
{
	bool result = false;
	char eventName[MAX_PATH];

	switch(soundFx->command)
	{
		case SNDFX_STATIONARY:
			{
				if(eaSize(eaArguments) >= 1) 
				{
					SoundSource *source;

					strncpy(eventName, (*eaArguments)[1], MAX_PATH-1);
					source = sndFxAddSoundSource(soundFx, eventName, file_name, pos);
					if(source)
					{
						source->stationary = 1;
					}
				}
			}
			break;
		case SNDFX_TRAVEL_POWER:
			{
				SoundSource *source = NULL;
				void *fmodEventGroup;
				char *path = NULL;

				if(eaSize(eaArguments) >= 1) 
				{
					path = (*eaArguments)[1];
				}

				// 1 - does event group exist?
				if(!FMOD_EventSystem_FindEventGroupByName(path, &fmodEventGroup))
				{
					void **events = NULL;
					
					soundFx->eventPath = strdup(path);
					soundFx->timestamp = timerCpuTicks();

					// 2 - find events in group and create sources for each
					fmodGroupGetEvents(fmodEventGroup, &events);

					FOR_EACH_IN_EARRAY(events, void, fmodEvent)
						char *name = NULL;

						fmodEventGetName(fmodEvent, &name);
						if(name)
						{
							// 3 - construct path to event
							strcpy(eventName, path);
							strcat(eventName, "/");
							strcat(eventName, name);

							sndFxAddSoundSource(soundFx, eventName, file_name, pos);
							result = true;
						}
					FOR_EACH_END;
				
					//eaPush(&eaSoundFxUpdateList, soundFx);

					eaDestroy(&events);
				}
			}
			break;
		case SNDFX_REPLACE_HIT_FIX:
			{
				SoundSource *source = NULL;
				void *fmodEventGroup;
				char *path = NULL;
				char *defaultEventPath = NULL;
				bool foundEvent = false;
				const char *geometryName = NULL;

				if(eaSize(eaArguments) >= 1) 
				{
					path = (*eaArguments)[1];
				}

				if(eaSize(eaArguments) >= 2) 
				{
					defaultEventPath = (*eaArguments)[2];
				}

				// 1 - does event group exist?
				if(!FMOD_EventSystem_FindEventGroupByName(path, &fmodEventGroup))
				{
					void **events = NULL;
					const DynParticle *dynParticle;
					DynFx *dynFx = GET_REF(soundFx->hDynFx);

					soundFx->eventPath = strdup(path);
					soundFx->timestamp = timerCpuTicks();

					// 2 - find events in group
					fmodGroupGetEvents(fmodEventGroup, &events);

					// 3 - extract geometry name from DynFx
					if(dynFx)
					{
						dynParticle = dynFxGetParticleConst(dynFx);
						if(dynParticle)
						{
							geometryName = dynParticle->pDraw->pcModelName;
						}
					}

					// 4 - look for geometry name in event group
					if(geometryName)
					{
						FOR_EACH_IN_EARRAY(events, void, fmodEvent)
							char *name = NULL;

							fmodEventGetName(fmodEvent, &name);
							if(name)
							{
								if(!stricmp(name, geometryName))
								{
									foundEvent = true;
									break;
								}
							}
						FOR_EACH_END;
					}

					eaDestroy(&events);
				}

				// 5 - create appropriate source 
				if(foundEvent)
				{
					strcpy(eventName, path);
					strcat(eventName, "/");
					strcat(eventName, geometryName);

					sndFxAddSoundSource(soundFx, eventName, file_name, pos);
				}
				else
				{
					if(defaultEventPath)
					{
						sndFxAddSoundSource(soundFx, defaultEventPath, file_name, pos);
					}
				}

			}
			break;
	}

	return result;
}

// on success, return true
bool sndFxParseCommand(SoundFx *soundFx, Vec3 pos, char *event_name, char *file_name)
{
	char **eaParts = NULL;
	bool result = false;

	sndFxSplitStringWithDelim(event_name, ":", &eaParts);
	
	if(eaSize(&eaParts) >= 2)
	{
		const char *commandName = strlower(eaParts[0]);
		char *path = eaParts[1];
		bool validCommand = false;
		int command;

		if(!strcmp(commandName, "travelpower"))
		{
			command = SNDFX_TRAVEL_POWER;	
			validCommand = true;
		} 
		else if(!strcmp(commandName, "replacehitfx"))
		{
			command = SNDFX_REPLACE_HIT_FIX;
			validCommand = true;
		} 
		else if(!strcmp(commandName, "stationary"))
		{
			command = SNDFX_STATIONARY;
			validCommand = true;
		}

		if(validCommand)
		{
			soundFx->command = command;
			result = sndFxProcessCommand(soundFx, &eaParts, pos, file_name);
		}
		else
		{
			// Invalid
		}
	}
	else
	{
		// invalid
	}


	FOR_EACH_IN_EARRAY(eaParts, char, token)
		free(token);
	FOR_EACH_END

	eaDestroy(&eaParts);
	
	return result;
}

void sndFxCmdStart(int guid, Vec3 pos, char *event_name, char *file_name)
{
	SoundFx *soundFx = NULL;
	
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	if(stashFindInt(disabledFXFiles, file_name, NULL))
	{
		// FX file was invalidated due to missing create keyframe
		return;
	}

	// Find the soundFx
	stashIntFindPointer(fxSounds, guid, &soundFx);

	if(!soundFx)
	{
		return;
	}

	if(strstr(event_name, ":"))
	{
		sndFxParseCommand(soundFx, pos, event_name, file_name);
	}
	else
	{
		sndFxAddSoundSource(soundFx, event_name, file_name, pos);

		//source = sndSourceCreate(file_name, file_name, event_name, pos, ST_POINT, SO_FX, NULL, false);
		//if(!source)
		//{
		//	return;
		//}
		//source->point.effect.guid = guid;
		//eaPush(&soundFx->eaSoundSources, source);

		soundFx->command = SNDFX_STANDARD_EVENT;
		soundFx->eventPath = strdup(event_name);
		soundFx->timestamp = timerCpuTicks();
	}
}

void sndFxCmdEnd(int guid)
{	 
	SoundFx *soundFx = NULL;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	stashIntFindPointer(fxSounds, guid, &soundFx);

	if(!soundFx)
	{
		return;
	}

	// stop all sources
	FOR_EACH_IN_EARRAY(soundFx->eaSoundSources, SoundSource, source)
		source->needs_stop = 1;
	FOR_EACH_END;
}

void sndFxCmdMove(int guid, Vec3 new_pos, Vec3 new_vel, Vec3 new_dir)
{
	SoundFx *soundFx = NULL;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	stashIntFindPointer(fxSounds, guid, &soundFx);
	if(!soundFx)
	{
		return;
	}

	// move all sources
	FOR_EACH_IN_EARRAY(soundFx->eaSoundSources, SoundSource, source)
		if(!source->stationary)
		{
			sndSourceMove(source, new_pos, new_vel, NULL);
		}
	FOR_EACH_END;
}

void sndFxCmdClean(int guid)
{
	SoundFx *soundFx = NULL;
	int loop = 0;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	stashIntFindPointer(fxSounds, guid, &soundFx);

	if(!soundFx)
	{
		return;
	}

	// move all sources
	FOR_EACH_IN_EARRAY(soundFx->eaSoundSources, SoundSource, source)
		//printf("DEBUG(FX_CLEAN): %p %s->clean_up = 1\n", source, source->obj.desc_name);
		source->clean_up = 1;
	FOR_EACH_END;

	// remove & destroy the instance
	//eaFindAndRemoveFast(&eaSoundFxUpdateList, soundFx); 
	REMOVE_HANDLE(soundFx->hDynFx);

	stashIntRemovePointer(fxSounds, guid, NULL);
	sndFxDestroy(soundFx);
}

extern SoundMixer *gSndMixer;
void sndFxCmdDSPStart(int guid, const char* dspname, char* filename)
{
	SoundFx *soundFx = NULL;
	SoundDSP *dsp = NULL;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return;
	}

	if(stashFindInt(disabledFXFiles, filename, NULL))
	{
		// FX file was invalidated due to missing create keyframe
		return;
	}

	stashIntFindPointer(fxSounds, guid, &soundFx);
	
	if(!soundFx)
	{
		return;
	}

	dsp = RefSystem_ReferentFromString(space_state.dsp_dict, dspname);

	if(!dsp)
		return;

	soundFx->command = SNDFX_DSP;
	soundFx->effect = sndMixerAddDSPEffect(gSndMixer, dsp);
	soundFx->timestamp = timerCpuTicks();
}

bool sndFxCmdDSPStop(int guid)
{
	SoundFx *soundFx = NULL;

	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return false;
	}

	stashIntFindPointer(fxSounds, guid, &soundFx);

	if(!soundFx)
	{
		return false;
	}

	sndMixerDelDSPEffect(gSndMixer, &soundFx->effect);
	REMOVE_HANDLE(soundFx->hDynFx);
	stashIntRemovePointer(fxSounds, guid, NULL);
	sndFxDestroy(soundFx);

	return true;
}

void sndFxCmdDSPClean(int guid)
{
	sndFxCmdDSPStop(guid);
}

U32 sndEventExistsEx(const char* event_name)
{
	return strstr(event_name, ":") || fmodEventExists(event_name);
}

void sndFxInit()
{

}

U32 sndDSPExists(const char* dspname)
{
	SoundDSP *dsp = RefSystem_ReferentFromString(space_state.dsp_dict, dspname);

	return !!dsp;
}

void sndFxSetupCallbacks()
{
	// For FX playing, stopping, and cleaning up (clean kills all looping sounds)
	dynFxSetSoundStartFunc(sndFxCmdStart);
	dynFxSetSoundStopFunc(sndFxCmdEnd);
	dynFxSetSoundCleanFunc(sndFxCmdClean);
	dynFxSetSoundMoveFunc(sndFxCmdMove);
	dynFxSetDSPStartFunc(sndFxCmdDSPStart);
	dynFxSetDSPStopFunc(sndFxCmdDSPStop);
	dynFxSetDSPCleanFunc(sndFxCmdDSPClean);
	dynFxSetDSPVerifyFunc(sndDSPExists);

	dynFxSetSoundGuidFunc(sndFxCmdGetGuid);
	dynFxSetSoundVerifyFunc(sndEventExistsEx);
	dynFxSetSoundInvalidateFunc(sndInvalidateFxFile);
}

void sndFxExecuteCommandQueue()
{
	
}



#endif
