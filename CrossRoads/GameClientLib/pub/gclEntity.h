#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLENTITY_H_
#define GCLENTITY_H_

#include "Entity.h"

typedef struct ContactInfo ContactInfo;
typedef struct ContainerSchema ContainerSchema;
typedef struct CritterInteractInfo CritterInteractInfo;
typedef struct CBox CBox;
typedef struct ExprContext ExprContext;
typedef struct MovementManagerMsg MovementManagerMsg;

typedef struct ClientOnlyEntity {
	Entity*			entity;
	EntityRef		oldEntityRef;
	bool			isCutsceneEnt;	//Was created by a cutscene
	bool			noAutoFree;		//Don't free when oldEntityRef is recived
} ClientOnlyEntity;

// Wrapper structure for a command string so that it can be displayed in UIGenLists
AUTO_STRUCT;
typedef struct MacroCommand
{
	char* estrCommand; AST(NAME(Command) ESTRING)
} MacroCommand;

// PlayerMacro data that the UI cares about
AUTO_STRUCT;
typedef struct MacroEditData
{
	U32 uMacroID;
	MacroCommand** eaCommands;
	char* pchDescription;
	const char* pchIcon; AST(POOL_STRING)
} MacroEditData;

//these cache info for a door to a region so the UI can point to the door instead of the entity 
//directly.
AUTO_STRUCT;
typedef struct EntRegionDoorForUIClamping
{
	EntityRef target;	//the entity whose icon should be moved to the door
	Vec3 pos;	//location of the door

	//Until either region changes this still points at the right door:
	const char* myRegion;	AST(POOL_STRING)
		const char* targetRegion;	AST(POOL_STRING)
		U32 timeUpdateRequested; //don't request updates every frame.
} EntRegionDoorForUIClamping;

// The active player is the local player that is currently being processed.
// This can set multiple players per console
#define entGetActiveLanguage() entGetLanguage(entActivePlayerPtr())

// Returns an entity pointer for the given player number
SA_RET_OP_VALID Entity *entPlayerPtr(int player);

// Returns ref for given player number
EntityRef entPlayerRef(int player);

// Sets given player number to given entref
void entSetPlayerRef(int player, SA_PARAM_NN_VALID EntityRef ref);

// Is the passed in entity a local player?
bool entIsLocalPlayer(Entity *ent);

// Returns number of local players
int entNumLocalPlayers(void);

//Gets and entity that is client side only
Entity *gclClientOnlyEntFromEntityRef(EntityRef iRef);

// Returns ent pointer for active player
SA_RET_OP_VALID Entity *entActivePlayerPtr(void);
// Also checks the Lobby-selected player
SA_RET_OP_VALID Entity *entActiveOrSelectedPlayer(void);

SA_RET_OP_VALID Character *characterActivePlayerPtr(void);
SA_RET_OP_VALID Player *playerActivePlayerPtr(void);

// Clears the active player, used for leaving gameplay state
void entClearLocalPlayers(void);

// Private helper functions for setting up client entities

// Initializes the given entity, on the client
void gclInitializeEntity(SA_PARAM_NN_VALID Entity *ent, bool isReloading);

// Cleans up given entity, on client
void gclCleanupEntity(SA_PARAM_NN_VALID Entity *ent, bool isReloading);

// Is the given entity actually visible to the camera?
bool entIsVisible(SA_PARAM_NN_VALID Entity *e);

// Find the pixel position of the top of the entity in the client window.
// yOffsetInFeet is added to the position, i.e. to get a position 6 inches
// above the entity's head, pass in 0.5. Returns the distance from the near
// plane of the frustum.
F32 entGetWindowScreenPosAndDist(Entity *e, Vec2 pixel_pos, F32 yOffsetInFeet);

F32 entGetScreenDist(Entity *e);

F32 entConvertUOM(Entity *pEntity, F32 fLength, const char** ppchUnitsOut, bool bGetAbbreviatedUnits);

// The same, but returns true if the entity is visible.
bool entGetWindowScreenPos(SA_PARAM_NN_VALID Entity *e, Vec2 pixel_pos, F32 yOffsetInFeet);

// Find a box of a given width and height that is centered on top of the entity in the client window.
// yOffsetInFeet is added to the position of the box, i.e. to get a position 6 inches
// above the entity's head, pass in 0.5.
CBox *entGetWindowScreenBox(SA_PARAM_NN_VALID Entity *e, CBox *box, F32 yOffsetInFeet, F32 width, F32 height);

bool entGetScreenBoundingBox(Entity *pEnt, CBox *pBox, F32 *pfDistance, bool bClipToScreen);
bool entGetPrimaryCapsuleScreenBoundingBox(Entity *pEnt, CBox *pBox, F32 *pfDistance);

//if the target is in a different region, point to the door to that region instead:
void entGetPosClampedToRegion(Entity* pPlayer, Entity* pTarget, Vec3 targetPosOut);

// Callback functions
void *entClientCreateCB(ContainerSchema *sc);
void entClientInitCB(ContainerSchema *sc, void *obj);
void entClientDeInitCB(ContainerSchema *sc, void *obj);
void entClientDestroyCB(ContainerSchema *sc, void *obj, const char* file, int line);

// Update attachment state
void gclEntUpdateAttach(Entity *be);

ClientOnlyEntity*	gclClientOnlyEntityCreate(bool bMakeRef);
void				gclClientOnlyEntityDestroy(ClientOnlyEntity** coeInOut);

S32					gclClientOnlyEntityIterCreate(U32* iterOut);
S32					gclClientOnlyEntityIterDestroy(U32* iterInOut);
ClientOnlyEntity*	gclClientOnlyEntityIterGetNext(U32 iter);
void gclClientOnlyEntitiyEnforceCap(U32 uiNumEntities);
S32 gclNumClientOnlyEntities(void);
void gclClientOnlyEntitiyGetCutsceneEnts(Entity ***pppEnts);

bool entMouseRayCanHit(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);
bool entMouseRayCanHitPlayer(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bExcludePlayer);

Entity *savedpet_GetOfflineOrNotCopy(U32 uiPetID);
Entity *savedpet_GetOfflineCopy(U32 uiPetID);

bool gclEntGetIsFriend(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pEntity );
bool gclEntGetIsFoe(SA_PARAM_OP_VALID Entity *pEntitySource, SA_PARAM_OP_VALID Entity *pEntityTarget);
bool gclEntGetIsPvPOpponent(SA_PARAM_OP_VALID Entity *pEntitySource, SA_PARAM_OP_VALID Entity *pEntityTarget);
ContactInfo* gclEntGetContactInfoForPlayer(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity);
bool gclEntGetIsContact(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity);
CritterInteractInfo *gclEntGetInteractableCritterInfo(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity);

// Gets the interaction range for the specified interactable
F32 gclEntity_GetInteractRange(Entity *entPlayer, Entity *entCritter, U32 uNodeInteractDist);

void gclEntityMovementManagerMsgHandler(const MovementManagerMsg* msg);

WLCostume* gclEntity_CreateWLCostumeFromPlayerCostume(Entity* pEnt, PlayerCostume* pCostume, bool bForceUpdate);

void gclPlayerExecMacro(U32 uMacroID);

void gclEntity_NotifyIfAimUnavailable(Entity *pEnt);

bool gclEntIsPrimaryMission(SA_PARAM_NN_VALID Entity *pEntity, const char *pcMission);

void gclEntityScreenBounding_UpdateAndGetAdjustedTargetBoxBounds(Entity* e, Vec3 vBoundMinInOut, Vec3 vBoundMaxInOut, bool bSnap);

void gclEntityAddPendingLootGlow(EntityRef iDeadEntRef, const char* pchFXStart);
void gclCheckPendingLootGlowEnts();

#endif
