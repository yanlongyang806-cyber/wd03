#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _SNDMISSION_H
#define _SNDMISSION_H

typedef struct Mission Mission;

typedef struct SoundMissionNode {
	struct SoundMissionNode **children; // child missions (if any)
	char *missionName;					// name of mission
	U32 depth;							// depth in tree

	U8 played : 1;
	U8 complete : 1;
	
} SoundMissionNode;

typedef struct SoundMission {
	SoundMissionNode *rootNode;

	Mission *currentMission;

	U8 lastCombatState : 1;
	U8 combatOverridden : 1;
} SoundMission;

extern SoundMission *gSoundMission;

void sndMissionInit();
void sndMissionOncePerFrame();


#endif
