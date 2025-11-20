#include "sndMission.h"
#include "mission_common.h"

// Cryptic Structs
#include "earray.h"
#include "estring.h"
#include "mathutil.h"
#include "timing.h"
#include "MemoryPool.h"

// Sound structs
#include "soundLib.h"
#include "sndLibPrivate.h"
#include "event_sys.h"
#include "sndSource.h"

#include "UGCProjectUtils.h"

SoundMission *gSoundMission = NULL;

#define MAX_PREFIX_SIZE (16)
#define MUSIC_MISSION_START_PATH ("Music/Mission_Begin")
#define MUSIC_MISSION_START_DEFAULT ("Default") //Music/Mission_Begin/Default_Music/Default_Random")
#define MUSIC_MISSION_COMPLETE_PATH ("Music/Mission_Complete")
#define MUSIC_MISSION_COMPLETE_DEFAULT ("Music/Mission_Complete/Default_Music/Fanfare_Random")

// Memory Pool
MP_DEFINE(SoundMissionNode);

// Memory Budget
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

void sndMissionUpdateWithMission(SoundMission *soundMission, Mission *mission);

#ifndef STUB_SOUNDLIB

#include "event_sys.h"

SoundMissionNode *sndMissionCreateMissionNode()
{
	SoundMissionNode *soundMissionNode;

	MP_CREATE(SoundMissionNode, 32);
	soundMissionNode = MP_ALLOC(SoundMissionNode);

	return soundMissionNode;
}

void sndMissionDestroyMissionNode(SoundMissionNode *soundMissionNode)
{
	if(!soundMissionNode) return; // protection

	SAFE_FREE(soundMissionNode->missionName);

	// destroy children (if any)
	FOR_EACH_IN_EARRAY(soundMissionNode->children, SoundMissionNode, childNode)
		sndMissionDestroyMissionNode(childNode);
	FOR_EACH_END;

	eaDestroy(&soundMissionNode->children);

	//if(soundFx->eventPath)
	//{
	//	free(soundFx->eventPath);
	//}
	//eaDestroy(&soundFx->eaSoundSources);

	MP_FREE(SoundMissionNode, soundMissionNode);
}

bool sndMissionPlayRandomEventAtPathUsingPrefix(char *groupPath, char *groupPrefix, char *eventPrefix)
{
	void **eventGroups = NULL;
	bool played = false;

	sndGetEventGroupsFromEventGroupPath(groupPath, &eventGroups);

	FOR_EACH_IN_EARRAY(eventGroups, void, eventGroup)
		char *eventGroupName = NULL;
		char *result;

		fmodEventGroupGetName(eventGroup, &eventGroupName);

		result = strstri(eventGroupName, groupPrefix);
		if(result && result == eventGroupName) // was it found, was it found at the beginning 
		{
			char dstPath[MAX_PATH];
			void **events = NULL;
			int numEvents;
			int chosenEventIndex;

			strcpy(dstPath, groupPath);
			strcat(dstPath, "/");
			strcat(dstPath, eventGroupName);

			// we found the group, so pick a sound from the group!
			if(eventPrefix)
			{
				sndGetEventsWithPrefixFromEventGroupPath(dstPath, eventPrefix, &events);
			}
			else
			{
				sndGetEventsFromEventGroupPath(dstPath, &events);
			}

			numEvents = eaSize(&events);
			if(numEvents > 0)
			{
				char *eventPath = NULL;
				void *chosenEvent = NULL;

				chosenEventIndex = randInt(numEvents);
				chosenEvent = events[chosenEventIndex];

				if(chosenEvent)
				{
					fmodEventGetFullName(&eventPath, chosenEvent, false);
					if(eventPath)
					{
						// I'm not sure passing eventPath for the file name is legit, so if it's wrong, please fix it
						sndMusicPlayWorld(eventPath, eventPath);
						played = true;
					}
					estrDestroy(&eventPath);
				}
			}

			break; // we're done here..
		}
	FOR_EACH_END;

	return played;
}


void sndMissionPerformMissionMusic(SoundMission *soundMission, Mission *mission, char *eventPrefix)
{
	if(soundMission && soundMission->rootNode)
	{
		char *missionName = soundMission->rootNode->missionName;
		MissionDef *rootDef = GET_REF(mission->rootDefOrig);

		if(missionName) // attempt to determine appropriate sound event based on root mission's name
		{
			// grab the prefix of the mission
			char *endPrefixMarker = strstr(missionName, "_");
			if(endPrefixMarker)
			{
				if(endPrefixMarker - missionName < MAX_PREFIX_SIZE)
				{
					char *estrPrefix = NULL;

					estrConcat(&estrPrefix, missionName, endPrefixMarker - missionName);

					// we have a prefix, now check for an appropriate event group
					if(!sndMissionPlayRandomEventAtPathUsingPrefix(MUSIC_MISSION_START_PATH, estrPrefix, eventPrefix))
					{
						// play the default music
						sndMissionPlayRandomEventAtPathUsingPrefix(MUSIC_MISSION_START_PATH, MUSIC_MISSION_START_DEFAULT, eventPrefix);
						//sndMusicPlayUI(MUSIC_MISSION_START_DEFAULT, __FILE__);
					}

					estrDestroy(&estrPrefix);
				}
				else
				{
					// play the default music
					sndMissionPlayRandomEventAtPathUsingPrefix(MUSIC_MISSION_START_PATH, MUSIC_MISSION_START_DEFAULT, eventPrefix);
				}
			}
		}
	}
}

void sndMissionPerformMissionBegin(SoundMission *soundMission, Mission *mission)
{
	if(soundMission && soundMission->rootNode)
	{
		char *missionName = soundMission->rootNode->missionName;
		MissionDef *rootDef = GET_REF(mission->rootDefOrig);

		soundMission->combatOverridden = false;

		// check for an override	
		if(mission->missionNameOrig && rootDef && rootDef->pchSoundOnStart && rootDef->pchSoundOnStart[0])
		{
			// play override
			soundMission->combatOverridden = true;
			sndMusicPlayUI(rootDef->pchSoundOnStart, rootDef->filename);
		}
		else if(rootDef && rootDef->pchSoundAmbient && rootDef->pchSoundAmbient[0])
		{
			sndMusicPlayUI(rootDef->pchSoundAmbient, rootDef->filename);
		}
		else if(missionName) // attempt to determine appropriate sound event based on root mission's name
		{
			// grab the prefix of the mission
			char *endPrefixMarker = strstr(missionName, "_");
			if(endPrefixMarker)
			{
				if(endPrefixMarker - missionName < MAX_PREFIX_SIZE)
				{
					char *estrPrefix = NULL;

					estrConcat(&estrPrefix, missionName, endPrefixMarker - missionName);
					
					// we have a prefix, now check for an appropriate event group
					if(!sndMissionPlayRandomEventAtPathUsingPrefix(MUSIC_MISSION_START_PATH, estrPrefix, "Ambient"))
					{
						// play the default music
						sndMissionPlayRandomEventAtPathUsingPrefix(MUSIC_MISSION_START_PATH, MUSIC_MISSION_START_DEFAULT, "Ambient");
						//sndMusicPlayUI(MUSIC_MISSION_START_DEFAULT, __FILE__);
					}

					estrDestroy(&estrPrefix);
				}
				else
				{
					// play the default music
					sndMissionPlayRandomEventAtPathUsingPrefix(MUSIC_MISSION_START_PATH, MUSIC_MISSION_START_DEFAULT, "Ambient");
				}
			}
		}
	}
}

void sndMissionPerformMissionComplete(SoundMission *soundMission, Mission *mission)
{
	if(soundMission && soundMission->rootNode)
	{
		char *missionName = soundMission->rootNode->missionName;
		MissionDef *rootDef = GET_REF(mission->rootDefOrig);

		// check for an override	
		if(mission->missionNameOrig && rootDef && rootDef->pchSoundOnComplete && rootDef->pchSoundOnComplete[0])
		{
			// play override
			sndMusicPlayUI(rootDef->pchSoundOnComplete, rootDef->filename);
		}
		else if(missionName) // attempt to determine appropriate sound event based on root mission's name
		{
			// grab the prefix of the mission
			char *endPrefixMarker = strstr(missionName, "_");
			if(endPrefixMarker)
			{
				if(endPrefixMarker - missionName < MAX_PREFIX_SIZE)
				{
					char *estrPrefix = NULL;

					estrConcat(&estrPrefix, missionName, endPrefixMarker - missionName);

					// we have a prefix, now check for an appropriate event group

					if(!sndMissionPlayRandomEventAtPathUsingPrefix(MUSIC_MISSION_COMPLETE_PATH, estrPrefix, NULL))
					{
						// play the default music
						sndMusicPlayUI(MUSIC_MISSION_COMPLETE_DEFAULT, NULL);
					}

					estrDestroy(&estrPrefix);
				}
				else
				{
					// play the default music
					sndMusicPlayUI(MUSIC_MISSION_COMPLETE_DEFAULT, NULL);
				}
			}
		}
	}
}

void sndMissionClearState(SoundMission *soundMission)
{
	sndMissionDestroyMissionNode(soundMission->rootNode);
	soundMission->rootNode = NULL;
}

void sndMissionSetNodeStateFromMission(SoundMissionNode *soundMissionNode, Mission *mission)
{
	// extract state info
	soundMissionNode->complete = (mission->state == MissionState_InProgress) ? false : true;
	soundMissionNode->missionName = strdup(mission->missionNameOrig);
	soundMissionNode->depth = mission->depth;
	
	// temp disabled
	/*
	// process children
	FOR_EACH_IN_EARRAY(mission->children, Mission, childMission)
		// create a mission node and attach
		SoundMissionNode *newMissionNode = sndMissionCreateMissionNode();

		eaPush(&soundMissionNode->children, newMissionNode);

		sndMissionSetNodeStateFromMission(newMissionNode, childMission);
	FOR_EACH_END;
	*/
}

void sndMissionSetState(SoundMission *soundMission, Mission *mission)
{
	sndMissionClearState(soundMission); // destroy the tree
	
	// CONSTRUCT THE TREE
	// create the root
	soundMission->rootNode = sndMissionCreateMissionNode();

	sndMissionSetNodeStateFromMission(soundMission->rootNode, mission);
}

void sndMissionCheckStateChanges(SoundMission *soundMission, Mission *mission)
{
	if(soundMission && soundMission->rootNode && mission)
	{
		MissionDef *def = mission_GetDef(mission);

		// only perform the following if the main mission has not been completed
		if(!soundMission->rootNode->complete)
		{
			// CHECK THE ROOT (for main mission complete)
			if(mission->state == MissionState_Succeeded)
			{
				// fire the end 
				sndMissionPerformMissionComplete(soundMission, mission);

				soundMission->rootNode->complete = true;
			}
			else
			{
				// (the following is not currently necessary)
				// TODO: check for submission complete
				// scan existing local copy of the tree
				// if node data is incomplete find status of node in new tree (search by name)
				// determine if the new tree has changed structurally from old, if so, reset the state

			}
		}
	

		// Check for changes in combat

		if(g_audio_state.player_in_combat_func && !soundMission->combatOverridden)
		{
			if(g_audio_state.player_in_combat_func())
			{
				if(soundMission->lastCombatState != 1)
				{
					// in combat
					if(def && def->pchSoundCombat && def->pchSoundCombat[0])
						sndMusicPlayUI(def->pchSoundCombat, def->filename);
					else
						sndMissionPerformMissionMusic(soundMission, mission, "Combat");
				}
				soundMission->lastCombatState = 1;
			}
			else
			{
				if(soundMission->lastCombatState != 0)
				{
					// not in combat
					if(def && def->pchSoundAmbient && def->pchSoundAmbient[0])
						sndMusicPlayUI(def->pchSoundAmbient, def->filename);
					else
						sndMissionPerformMissionMusic(soundMission, mission, "Ambient");
				}
				soundMission->lastCombatState = 0;
			}
		}
	}
}

void sndMissionUpdateWithMission(SoundMission *soundMission, Mission *mission)
{
	bool namesDiffer = false;
	if(!soundMission) return; // protection

	if(mission && soundMission->currentMission && soundMission->rootNode)
	{
		if(strcmp(mission->missionNameOrig, soundMission->rootNode->missionName))
		{
			namesDiffer = true;
		}
	}

	if(soundMission->currentMission != mission || namesDiffer)
	{
		if(mission)
		{
			// the mission itself has changed
			// make an initial copy of the relevant parts of the tree
			sndMissionSetState(soundMission, mission);
			
			sndMissionPerformMissionBegin(soundMission, mission);
		}
		else
		{
			// no mission now, just clear the state entirely
			sndMissionClearState(soundMission);
		}
	} 
	else if(soundMission->currentMission)
	{
		// we have a current mission, let's see if anything has changed

		sndMissionCheckStateChanges(soundMission, mission);
	}

	soundMission->currentMission = mission; // save this
}

#endif




void sndMissionInit()
{
#ifndef STUB_SOUNDLIB
	gSoundMission = calloc(1, sizeof(SoundMission));
#endif
}

void sndMissionOncePerFrame()
{
#ifndef STUB_SOUNDLIB
	if(g_audio_state.active_open_mission_func)
	{
		OpenMission *om = g_audio_state.active_open_mission_func();
		
		sndMissionUpdateWithMission(gSoundMission, SAFE_MEMBER(om, pMission));
	}
#endif
}


