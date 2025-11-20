/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct MissionDef MissionDef;
typedef struct MissionSet MissionSet;

MissionDef* missionset_RandomMissionFromSet(MissionSet *pSet);
MissionDef* missionset_RandomAvailableMissionFromSet(Entity *pEnt, MissionSet *pSet);

