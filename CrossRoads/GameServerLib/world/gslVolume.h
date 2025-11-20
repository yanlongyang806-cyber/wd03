/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct DoorConn DoorConn;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct GameInteractableOverride GameInteractableOverride;
typedef struct InteractOption InteractOption;
typedef struct WorldNamedVolume WorldNamedVolume;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldScope WorldScope;
typedef struct WorldVolume WorldVolume;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct ZoneMap ZoneMap;

typedef struct VolumePartitionState
{
	int iPartitionIdx;
	S64 iNextPowerTime;
	bool bExecutePower;
} VolumePartitionState;

typedef struct GameNamedVolume
{
	// The volume's map-level name (pooled)
	const char *pcName;

	// The world named volume
	WorldNamedVolume *pNamedVolume;

	// Property overrides
	GameInteractableOverride **eaOverrides;

	VolumePartitionState **eaPartitionStates;
} GameNamedVolume;

// Evaluates an expression in the volume context
int volume_EvaluateExpr(int iPartitionIdx, GameNamedVolume *pVolume, Entity *pEnt, Expression *pExpr);

// Gets a volume if one exists
SA_RET_OP_VALID GameNamedVolume *volume_GetByName(SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope *pScope);

// Translates over to GameNamedVolume from a WorldVolumeEntry.
SA_RET_OP_VALID GameNamedVolume *volume_GetByEntry(SA_PARAM_NN_VALID WorldVolumeEntry *pVolume);

// Traverse the list of volumes
typedef void (*VolumeForEachEntryCallback)(WorldVolumeEntry *pVolume, void* pUserData);
// Doesn't yet use pScope
void volume_ForEachEntry(const WorldScope* pScope, VolumeForEachEntryCallback cb, void *pUserData);

// Check if an entity is in a volume
bool volume_IsEntityInVolumeByName(SA_PARAM_NN_VALID const Entity *pEnt, SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope *pScope);
bool volume_IsEntityInAnyVolumeByName(SA_PARAM_NN_VALID const Entity *pEnt, const char **eaVolumeNames, SA_PARAM_OP_VALID const WorldScope *pScope);
bool volume_IsEntityInVolume(const Entity *pEnt, GameNamedVolume *pGameVolume);

// Get the list of entities in a volume
void volume_GetEntitiesInVolume(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope *pScope, Entity ***peaEntsOut, bool bFilterResults);

// Test that a volume exists
bool volume_VolumeExists(SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope *pScope);

// Gets a volume's center position.  Returns true if volume exists.
bool volume_GetCenterPosition(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope *pScope, Vec3 vPosition);

// Gets a list of volumes with warp properties
void volume_GetWarpConnections(DoorConn ***eaDoors);

// Gets volume position and size data.  Returns true if volume exists.
bool volume_GetVolumeData(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope* pScope, Vec3 vCenter, Vec3 vPosition, Vec3 vLocalMin, Vec3 vLocalMax, F32 *pfRot);

// Gets the world volume for a volume
WorldVolume *volume_GetWorldVolume(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope* pScope);

// Gets interaction properties
WorldInteractionPropertyEntry *volume_GetInteractionPropEntry(SA_PARAM_OP_VALID GameNamedVolume *pGameVolume, int iIndex);
WorldInteractionPropertyEntry *volume_GetInteractionPropEntryByName(SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope* pScope, int iIndex);

// Checks if the player can interact with the given volume
bool volume_AddInteractOptions(int iPartitionIdx, SA_PARAM_NN_STR const char *pcVolumeName, SA_PARAM_OP_VALID const WorldScope* pScope, SA_PARAM_NN_VALID Entity *pEnt, InteractOption ***peaOptionList);

// Checks if the player can interact with the given volume
bool volume_VerifyInteract(SA_PARAM_NN_VALID Entity *pEnt, const char *pcVolumeName, int iIndex);

// Adds an interaction override (from a mission) to the specified volume
bool volume_ApplyInteractableOverride(const char *pcMissionName, const char *pcFilename, const char *pcVolumeName, WorldInteractionPropertyEntry *pEntry);
void volume_RemoveInteractableOverridesFromMission(const char *pcMissionName);
void volume_InitOverridesMatchingName(const char* pchName);

//This returns true if the vec3 param is located inside of the passed in volume.
//This does not work for hull volumes at the moment.
bool volume_IsPointInVolume(Vec3 v3Point, WorldVolumeEntry *pEntry);

//Returns the map level override for a position if it exists, -1 otherwise
S32 volume_GetLevelOverrideForPosition(Vec3 v3WorldPos);

// Test that a volume exists
const char *volume_NameFromWorldEntry(WorldVolumeEntry *pEntry);

void volume_ClearPlayerVolumeTrackingData(Entity *pPlayerEnt);
void volume_ClearAllPlayerVolumeTrackingData();

// Map load and unload notification
void volume_PartitionLoad(int iPartitionIdx, bool bFullInit);
void volume_PartitionUnload(int iPartitionIdx);
void volume_MapLoad(ZoneMap *pZoneMap);
void volume_MapUnload(void);
void volume_MapValidate(void);

void volume_ResetVolumes(void);
void volume_ClearVolumeOverrides(void);
void volume_GenerateOverrideExpressions(void);
bool volume_AreVolumesLoaded();

extern const char *g_pchVolumeVarName;