/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLENTITY_H_
#define GSLENTITY_H_

#include "Entity.h"

typedef struct ContainerSchema ContainerSchema;
typedef struct MapDescription MapDescription;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct ClientLink ClientLink;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct EntAndDist	EntAndDist;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef U32 EntityRef;

typedef enum enumTransactionOutcome enumTransactionOutcome;
typedef enum PCCostumeType PCCostumeType;

typedef void (*UserDataCallback)(void *userdata);

typedef struct RenamePetData
{
	int iPartitionIdx;
	EntityRef entRef;
	GlobalType eType;
	ContainerID iID;
	char *oldName;
	bool bUpdateOwnerCostumes;
} RenamePetData;

typedef enum FactionOverrideType
{
	kFactionOverrideType_DEFAULT = 0,
	kFactionOverrideType_POWERS,
	kFactionOverrideType_COUNT
} FactionOverrideType;

AUTO_STRUCT;
typedef struct BuildCreateParam {
	U32 uiValidateTag;
} BuildCreateParam;

AUTO_STRUCT;
typedef struct BuildDeleteParam {
	U32 uiValidateTag;

	U32 uiBuildIdx;
} BuildDeleteParam;

AUTO_STRUCT;
typedef struct BuildSetNameParam {
	U32 uiValidateTag;

	U32 uiBuildIdx;
	const char *pchName;
} BuildSetNameParam;

AUTO_STRUCT;
typedef struct BuildSetClassParam {
	U32 uiValidateTag;

	U32 uiBuildIdx;
	const char *pchClass;
} BuildSetClassParam;

AUTO_STRUCT;
typedef struct BuildSetCostumeParam {
	U32 uiValidateTag;

	U32 uiBuildIdx;
	int eCostumeType;
	int iCostumeIdx;
} BuildSetCostumeParam;

AUTO_STRUCT;
typedef struct BuildSetItemParam {
	U32 uiValidateTag;

	U32 uiBuildIdx;
	int iInvBag;
	int iSlot;
	U64 iItemID;
	int iSrcBag;
	int iSrcSlot;
} BuildSetItemParam;

AUTO_STRUCT;
typedef struct BuildCopyCurrentParam {
	U32 uiValidateTag;

	U32 uiDestIndex;
	int bCopyClass;
	int bCopyCostume;
	int bCopyItems;
	int bCopyPowers;
} BuildCopyCurrentParam;

AUTO_STRUCT;
typedef struct BuildSetCurrentParam {
	U32 uiValidateTag;

	U32 uiBuildIdx;
	int bSetCheck;
	int bSkipPlayerPower;
	int bMarkOld;
} BuildSetCurrentParam;


AUTO_STRUCT;
typedef struct PWMappedItem
{
	char *esName;								AST(NAME(pw_item_name) ESTRING)
	char *esDisplayName;						AST(NAME(pw_item_display_name) ESTRING)
	U32 uId;									AST(NAME(pw_item_id))
	U32 uCount;									AST(NAME(pw_item_count))

}PWMappedItem;

AUTO_STRUCT;
typedef struct PWInventory
{
	EARRAY_OF(PWMappedItem) eaMappedItems;		AST(NAME(pw_mapped_items))

}PWInventory;

int gslEntGetPartitionID(Entity *ent);

// Call these to create or destroy an eneity on the client or server

// Creates and initializes a NON-PERSISTED entity.
Entity *gslCreateEntity(GlobalType entityType, int iPartitionIdx);

// Destroys a NON-PERSISTED entity immediately. Do not call normally!
int gslDestroyEntity(Entity * e);

// Queues an entity to be destroyed
void gslQueueEntityDestroy(Entity * e);

// Deals with local entities on a Client Link

// Get the primary entity for a link
Entity *gslPrimaryEntity(ClientLink *pLink);

// Adds the specified entity to the link
void gslAddEntityToLink(ClientLink *pLink, Entity *e, bool isPrimary);

// Removes entity from link
void gslRemoveEntityFromLink(ClientLink *pLink, Entity *e);

// Adds entity to be debugged
void gslAddDebugEntityToLink(ClientLink *pLink, Entity *e);

// Adds entity to be debugged
void gslAddDebugEntityToLinkByRef(ClientLink *pLink, EntityRef ref);

// Removes entity from debugged list
void gslRemoveDebugEntityFromLink(ClientLink *pLink, Entity *e);

// Private helper functions

// Initializes the given entity, on the server
void gslInitializeEntity(SA_PARAM_NN_VALID Entity *e, bool bIsReloading);

// Cleans up given entity, on server
void gslCleanupEntityEx(int iPartitionIdx, Entity *e, bool bIsReloading, bool bDoCleanup);
#define gslCleanupEntity(e) gslCleanupEntityEx(entGetPartitionIdx(e), e, false, false)

// This entity needs to be sent fully next time it is sent
void gslEntityForceFullSend(Entity *e);

// Updates the map history for an entity, called when exiting and entering a map
void gslEntityMapHistoryLeftMap(NOCONST(Entity)* e, MapDescription* targetMapDesc);
void gslEntityMapHistoryEnteredMap(NOCONST(Entity)* e);

// Attempts to leave the current map if the player came from a previous map
void LeaveMap(Entity* e);
bool LeaveMapEx(Entity* e, DoorTransitionSequenceDef* pTransOverride);
// gets the map where LeaveMap will take you
char const * GetExitMap(Entity* e);

// ClientLink entity queries
bool gslLinkOwnsRef(ClientLink *clientLink, EntityRef entRef);
const char *gslGetAccountNameForLink(ClientLink *clientLink);

// Enter/Exit callbacks
void gslPlayerEnteredMap(Entity* e, bool bInitPosition);
void gslPlayerLeftMap(Entity* e, bool bRemovePets);

// Track whether the entity is only online for a CSR command
void gslOfflineCSREntAdd(GlobalType eType, ContainerID uID);
void gslOfflineCSREntRemove(GlobalType eType, ContainerID uID);
bool gslIsOfflineCSREnt(GlobalType eType, ContainerID uID);

// Returns an Entity that is visible to all players, so it can be used to transport otherwise
//  unattached effects (like world FX).
SA_RET_NN_VALID Entity *gslGetTransportEnt(int iPartitionIdx);

// Sets ePet to be the Primary Pet of eOwner. If either ePet is NULL, cancel any existing relationships
void gslEntSetPrimaryPet(Entity *eOwner, Entity *ePet);

// Attach passed in entity to another entity, 0 unattaches it
bool gslEntAttachToEnt(Entity *e, Entity *eAttach, const char *pBoneName, const char *pExtraBit, const Vec3 posOffset, const Quat rotOffset);

// Update attachment state of entity
void gslEntUpdateAttach(Entity *e);

// Take control of another entity
void gslEntControlCritter(Entity* e, Entity *eTarget);

// Remove control of another entity
void gslEntEndControlCritter(Entity* e, Entity *eTarget);

// Ride another entity
void gslEntRideCritter(Entity* e, Entity *eTarget, GameAccountDataExtract *pExtract);

// Cancel ride
void gslEntCancelRide(Entity* e);

//called when the player enters the level, then periodically thereafter
void gslEntityPlayerLogStatePeriodic(SA_PARAM_NN_VALID Entity *e, bool bDoFullLog, float fSecondsPassed);

// Create and destroy movement requesters.
void gslEntMovementCreateSurfaceRequester(Entity* e);
void gslEntMovementDestroySurfaceRequester(Entity* e);
void gslEntMovementCreateFlightRequester(Entity* e);
void gslEntMovementCreateTacticalRequester(Entity* e);
void gslEntMovementCreateEmoteRequester(Entity* e);

void gslEntity_UnlockMovement(SA_PARAM_NN_VALID Entity *e);
void gslEntity_LockMovement(SA_PARAM_NN_VALID Entity* e, bool bIncludePets);


void gslEntityStartReceivingNearbyPlayerFlag(Entity *pEnt);
void gslEntityStopReceivingNearbyPlayerFlag(Entity *pEnt);

// Callback functions
void *entServerCreateCB(ContainerSchema *sc);
void entServerInitCB(ContainerSchema *sc, void *obj);
void entServerDeInitCB(ContainerSchema *sc, void *obj);
void entServerDestroyCB(ContainerSchema *sc, void *obj, const char* file, int line);

// EntityBuild functions

// Wrapper for trEntity_BuildSetCurrent
void entity_BuildSetCurrentEx(SA_PARAM_NN_VALID Entity *e, U32 uiIndex, S32 bSetCheck, S32 bSkipPlayerPower);
#define entity_BuildSetCurrent(e, uiIndex, bSetCheck) entity_BuildSetCurrentEx((e),(uiIndex),(bSetCheck), false)

// Wrapper for trEntity_BuildCreate
void entity_BuildCreate(SA_PARAM_NN_VALID Entity *e);

// transaction function so that PlayerLoginCB can create builds.
enumTransactionOutcome trEntity_BuildCreate(ATR_ARGS, NOCONST(Entity) *e, BuildCreateParam *pParam);

// Wrapper for trEntity_BuildDelete
void entity_BuildDelete(SA_PARAM_NN_VALID Entity *e, U32 uiBuildIdx, S32 bSetCheck);

// Wrapper for trEntity_BuildSetName
void entity_BuildSetName(SA_PARAM_NN_VALID Entity *e, U32 uiIndex, SA_PARAM_NN_STR const char *pchName);

// Wrapper for trEntity_BuildSetClass
void entity_BuildSetClass(Entity *e, U32 uiIndex, const char *pchClass);

// Wrapper for trEntity_BuildSetItem
void entity_BuildSetItem(SA_PARAM_NN_VALID Entity *e, U32 iBuildIdx, int iInvBag, int iSlot, U64 iItemID, int iSrcBag, int iSrcSlot);

void entity_BuildSetCostume(Entity* e, U32 uiIndex, PCCostumeType eCostumeType, int iCostumeIdx);
bool entity_InitBuildSetCostumeParam(BuildSetCostumeParam *pParam, Entity *e, U32 uiIndex, PCCostumeType eCostumeType, int iCostumeIdx);
enumTransactionOutcome trEntity_BuildSetCostume(ATR_ARGS, NOCONST(Entity)* e, BuildSetCostumeParam *pParam);

// Copies the current build's data to uiDestIndex build
void entity_BuildCopyCurrent(Entity *e, U32 uiDestIndex, S32 bCopyClass, S32 bCopyCostume, S32 bCopyItems, S32 bCopyPowers);

//TODO(BH): Remove this code once characters are wiped
//Fixup code because of the jacked up hidden bag code
void entity_FixupBuilds(ATR_ARGS, ATH_ARG NOCONST(Entity) *e);

//used by SetPos debug command
void gslEntSetPos(Entity* e, const Vec3 vPos);

void gslCacheEntRegion(Entity* e, GameAccountDataExtract *pExtract);

void gslEntityUpdateSendDistance(Entity* e);

// Gets the language of the entity
int entity_trh_GetLanguage(ATH_ARG NOCONST(Entity) *e);

// Gets the interaction range for the interactable critter or node
F32 gslEntity_GetInteractRange(Entity *ePlayer, Entity *eCritter, WorldInteractionNode *pNode);

void gslEntity_SetGADDaysSubscribed(Entity *pEntity, U32 iDays);

LATELINK;
void gameSpecific_Load(void);

LATELINK;
void gameSpecific_EntityTick(Entity *e, F32 fTime);

LATELINK;
void gslGameSpecific_Minigame_Tick(void);

LATELINK;
void gslGameSpecific_Minigame_Login(Entity* e);

LATELINK;
void gslGameSpecific_Minigame_PartitionLoad(int iPartitionIdx);

LATELINK;
void gslGameSpecific_Minigame_PartitionUnload(int iPartitionIdx);

void gslEntitySetInvisibleTransient(Entity* e, S32 enabled);
void gslEntitySetInvisiblePersistent(Entity* e, S32 enabled);
void gslEntitySetIsStrafing(Entity* e, S32 enabled);
void gslEntitySetUseThrottle(Entity* e, S32 enabled);
void gslEntitySetUseOffsetRotation(Entity* e, S32 enabled);

void entCon(Entity* client, char* target, char* ecCmdData);

void gslEntityGodMode(Entity* ent, int iSet);
void gslEntityUntargetableMode(Entity* ent, int iSet);

void gslEntity_ClearFaction(SA_PARAM_NN_VALID Entity *pEnt, FactionOverrideType eOverride);
void gslEntity_SetFactionOverrideByName(SA_PARAM_NN_VALID Entity *pEnt, FactionOverrideType type, const char *pchFaction);
void gslEntity_SetFactionOverrideByHandle(SA_PARAM_NN_VALID Entity *pEnt, FactionOverrideType type, ConstReferenceHandle *phFactionRef);

U32 gslEntity_CreateMacro(Entity* pEnt, const char* pchMacro, const char* pchDesc, const char* pchIcon);
bool gslEntity_DestroyMacro(Entity* pEnt, U32 uMacroID);

void gslEntity_UpdateMovementMangerFaction(S32 iPartitionIdx, Entity *pEnt);

void entitytransaction_RenamePetCallback(TransactionReturnVal *pReturn, RenamePetData *pData);
void gslEntity_BadName(GlobalType eEntType, ContainerID uiEntID, GlobalType ePupType, ContainerID uPupID);

void entSetUIVar(SA_PARAM_NN_VALID Entity *e, SA_PARAM_OP_STR const char* VarName, SA_PARAM_NN_VALID MultiVal* pMultiVal);
bool entDeleteUIVar(SA_PARAM_NN_VALID Entity *e, SA_PARAM_OP_STR const char* VarName);

void KillTarget(Entity *pEntity);
void player_CmdGodMode(Entity *pPlayerEnt, int iSet);

#endif
