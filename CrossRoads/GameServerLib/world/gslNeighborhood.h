/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct GameNamedVolume GameNamedVolume;
typedef struct Message Message;
typedef struct WorldVolumeEntry WorldVolumeEntry;

// Entity neighborhood tracking
void neighborhood_SetEntityCurrentHood(Entity *pEnt, const char *pcPooledNeighborhoodName, const char *pcDisplayNameKey);
void neighborhood_ClearEntityCurrentHood(Entity *pEnt, const char *pcNeighborhoodName);
void neighborhood_ClearEntityHoodData(Entity *pEnt);

// Neighborhood actions
void neighborhood_PlayHoodMusicForEntity(Entity *pEnt, const char *pcPooledNeighborhoodName, const char *pcSoundEffect);

// Callbacks for volume entry/exit
void neighborhood_VolumeEnteredCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume);
void neighborhood_VolumeExitedCB(WorldVolumeEntry *pEntry, Entity *pEnt);

// Validation
void neighborhood_ValidateNeighborhoodVolume(GameNamedVolume *pGameVolume);
