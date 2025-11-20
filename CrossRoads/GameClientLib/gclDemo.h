#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLDEMO_H
#define GCLDEMO_H

// *** This is super-rough and not the final version at all ***

#include "GfxCamera.h"
#include "GfxRecord.h"
#include "EntityMovementManager.h"
#include "cmdparse.h"
#include "wlCostume.h"
#include "Entity.h"
#include "wlInteraction.h"
#include "dynFxDamage.h"

typedef struct CostumeRefV0 CostumeRefV0;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct MapState MapState;

AUTO_STRUCT AST_FIXUPFUNC(fixupRecordedEntity);
typedef struct RecordedEntity
{
	int entityRef;					AST(NAME(entityRef))
	int containerID;				AST(NAME(containerID))
	F32 fEntitySendDistance;		AST(NAME(EntitySendDistance) DEFAULT( 300 ))
	SavedEntityData *entityAttach;	AST(NAME(entityAttach))

    // Backward compatibility structures.  If formats change, we don't want to break demos.
	int entityTypeOld;				AST(NAME(entityType))
	GlobalType entityTypeEnum;		AST(NAME(entityTypeEnum) SUBTABLE(GlobalTypeEnum))
	
    char* costumeName;				AST(NAME(costumeName))
    CostumeRefV0* costumeV0;		AST(NAME(costume))
	CostumeRef costumeV5;			AST(NAME(CostumeV5))
} RecordedEntity;

AUTO_STRUCT;
typedef struct RecordedWIN
{
	const char* resourceName;		AST( POOL_STRING )
	WorldInteractionNode* node;		AST( LATEBIND )
} RecordedWIN;

AUTO_STRUCT;
typedef struct RecordedEntityDamage
{
	int entityRef;							AST(NAME(entityRef))
	F32 hp;									AST(NAME(HP))
	F32 maxHP;								AST(NAME(MaxHP))
} RecordedEntityDamage;

AUTO_STRUCT;
typedef struct RecordedMessage
{
	F32 time;
	char* message;
	U32 flags;			AST( DEFAULT( 2 ))
	int command;
	int entityRef;
} RecordedMessage;

// This struct feels like overkill!
AUTO_STRUCT;
typedef struct RecordedEntityDestruction
{
	U32 entityRef;
	bool noFade;					AST(NAME(NoFade))
} RecordedEntityDestruction;

// Record an entity changing its costume
AUTO_STRUCT AST_FIXUPFUNC(fixupRecordedEntityCostumeChange);
typedef struct RecordedEntityCostumeChange
{
	U32 entityRef;

	// backward compatibility structures
	CostumeRefV0* costumeV0;		AST(NAME(costume))
	CostumeRef costume;				AST(NAME(CostumeV5))
} RecordedEntityCostumeChange;

// Record a map state update
AUTO_STRUCT;
typedef struct RecordedMapStateUpdate
{
	U32 pakID;						AST(NAME(PakID))
	MapState* fullUpdate;			AST(NAME(Full))
	char* diffUpdate;				AST(NAME(Diff) ESTRING)
	bool deleteUpdate;				AST(NAME(Delete))
} RecordedMapStateUpdate;

AUTO_STRUCT;
typedef struct RecordedMMPacket
{
	F32 time;
	U32 serverProcessCount;

	RecordedEntityUpdate** updates; AST(NO_INDEX)
	RecordedEntityCostumeChange** costumeChanges;
	RecordedEntity** createdEnts;
	RecordedEntityDestruction** destroyedEnts;
	RecordedWIN** interactionUpdates;
	RecordedEntityDamage** entDamage;
	RecordedMapStateUpdate** mapState; AST(NAME(MapState))
} RecordedMMPacket;

AUTO_STRUCT AST_IGNORE(nearz) AST_IGNORE(farz);
typedef struct DemoRecording
{
	U32 version;

	char* old_worldgridName; AST(NAME(worldgridName))
	char* zoneName;
	ZoneMapInfo* zmInfo;	 AST(NAME(ZMInfo) LATEBIND)
	char* zmInfoFilename;

	// Wall clock time
	U32 startTime;
	U32 endTime;

	// World lib time (e.g. night/day)
	F32 startWorldTime;

	// Entity stuff
    EntityRef activePlayerRef;

	// Movement manager commands, entity creation, etc.
	RecordedMMPacket** packets;
	U32 curPacket; NO_AST
	// All other commands
	RecordedMessage** messages;
	U32 curMessage; NO_AST

	// Camera stuff
	F32 fovy;

	// Camera positions
    CameraMatRelative** relativeCameraViews;    AST(NO_INDEX)

    // Legacy format -- should no longer be recorded
	CameraMat** cameraViews;	AST(NO_INDEX)

	// Overrides camera positions
	CutsceneDef *cutsceneDef;

} DemoRecording;
extern ParseTable parse_DemoRecording[];
#define TYPE_parse_DemoRecording DemoRecording

// Get the current state
bool demo_recording(void);
bool demo_playingBack(void);

//Only the editor should need this funciton
DemoRecording* demo_GetInfo(char **demo_name/*out*/);
F32 demo_GetPlaybackTimeElapsed();

void demo_record(const char *file);
void demo_record_cutscene(CmdContext *cmd_context, const char *file, const char *cutscene);
void demo_record_stop(CmdContext *cmd_context);

void demo_play(CmdContext *cmd_context, char *file);
void demo_play_repeat(CmdContext *cmd_context, char *file, U32 repeat_count);
void demo_play_60fps(CmdContext *cmd_context, char *file);
void demo_play_repeat_60fps(CmdContext *cmd_context, char *file, U32 repeat_count);
void demo_play_30fps(CmdContext *cmd_context, char *file);
void demo_play_repeat_30fps(CmdContext *cmd_context, char *file, U32 repeat_count);
void demo_save_images(
        CmdContext* cmd_context, SA_PARAM_NN_STR const char* demo_file,
        SA_PARAM_OP_STR char* image_file_prefix, SA_PARAM_OP_STR char* image_file_ext );

void demo_restart( void );

void demo_RecordMapName(SA_PARAM_NN_STR const char* mapname);
void demo_loadMap(void);
void demo_startMapPatching(void);

// Gameplay has started; begin recording
void demo_startRecording(void);

// Load a replay
void demo_LoadReplay(void);
void demo_LoadReplayLate(void);

// Get Player
Entity* demo_GetActivePlayer();

// State machines for demos
void gclDemoPlayback_Enter(void);
void gclDemoPlayback_BeginFrame(void);
void gclDemoPlayback_Leave(void);

//void gclDemoLoading_Enter(void);
//void gclDemoLoading_EndFrame(void);
//void gclDemoLoading_Leave(void);

// Record various events
void demo_RecordCamera(void);

// Record a message from the server to the client (chat bubbles, etc.)
void demo_RecordMessage(SA_PARAM_NN_STR const char* msg, U32 flags, int cmd, SA_PARAM_NN_VALID Entity *ent);

void demo_RecordResourceUpdated(void* ignored, const char* dictName, const char* resourceName, void* object, ParseTable* parseTable);
void demo_RecordEntityCreation(Entity* pEntity);
void demo_RecordEntityUpdate(Entity* pEntity, RecordedEntityUpdate* recPos);
void demo_RecordEntityCostumeChange(Entity* pEntity);
void demo_RecordEntityDestruction(int iEntRef, bool noFade);
void demo_RecordEntityDamage(Entity* pEntity, F32 hp, F32 maxHP);

void demo_RecordMMHeader(int curProcessCount);

void demo_RecordMapStateFull(MapState* mapState, U32 pakID);
void demo_RecordMapStateDiffBefore(MapState* beforeDiffMapState);
void demo_RecordMapStateDiff(MapState* mapState, U32 pakID);
void demo_RecordMapStateDestroy(U32 pakID);

void demo_saveMemoryUsage(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

#endif
