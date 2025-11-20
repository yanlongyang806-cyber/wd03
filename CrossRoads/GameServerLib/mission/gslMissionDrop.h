/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Critter Critter;
typedef struct MissionDef MissionDef;
typedef struct MissionDrop MissionDrop;


typedef struct GlobalMissionDropContainer {
	MissionDrop *pDrop;
	REF_TO(MissionDef) hMissionDef;
} GlobalMissionDropContainer;


// Gets all Mission Drops that should be awarded to a player for killing the specified critter
void missiondrop_GetMissionDropsForPlayerKill(Entity *pPlayerEnt, Critter *pCritter, MissionDrop ***peaMissionDropsOut);

// Registers all pre-Mission MissionDrops for the given MissionDef
void missiondrop_RegisterGlobalMissionDrops(MissionDef *pDef);

// Unregisters all pre-Mission MissionDrops for the given MissionDef
void missiondrop_UnregisterGlobalMissionDrops(MissionDef *pDef);

void missiondrop_RefreshGlobalMissionDrops(void);
