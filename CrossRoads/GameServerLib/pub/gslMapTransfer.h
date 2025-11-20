/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLMAPTRANSFER_H_
#define GSLMAPTRANSFER_H_

#include "GlobalComm.h"
#include "GlobalEnums.h"
#include "LocalTransactionManager.h"
#include "MapDescription.h"
#include "RemoteAutoCommandSupport.h"

typedef struct ClientLink ClientLink;
typedef struct WorldVariable WorldVariable;
typedef struct RegionRules RegionRules;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct SavedMapDescription SavedMapDescription;
typedef struct UGCProject UGCProject;
typedef struct PossibleMapChoice PossibleMapChoice;
typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct ReturnedGameServerAddress ReturnedGameServerAddress;
typedef struct MapSearchInfo MapSearchInfo;
typedef struct MapDescription MapDescription;

// This is the main header file for coordinate transfers between maps. This shares 
// some code and logic with the login server


// State machine for the map transfers

#define MAP_TRANSFER_STATE_MACHINE "aslLoginServer"

// Simple machine, all states are siblings
 
// Character has requested transfer
#define TRANSFERSTATE_INITIAL "TransferInitial"
// Character is selecting the destination
#define TRANSFERSTATE_SELECTING_MAP "TransferSelectingMap"
// Character is waiting for a team mate who started transferring first to get their destination map ID
#define TRANSFERSTATE_WAITING_FOR_TEAM_TRANSFER "TransferWaitingForTeamTransfer"
// Character being transferred to the new map
#define TRANSFERSTATE_TRANSFERRING_CHARACTER "TransferTransferringCharacter"
// Transfer failed for some reason
#define TRANSFERSTATE_FAILED "TransferFailed"
// Character successfully transferred
#define TRANSFERSTATE_COMPLETE "TransferComplete"
// Leading into TRANSFERSTATE_MOVING_BETWEEN_PARTITIONS. This allows us to make sure the entity is safe to save before moving on.
#define TRANSFERSTATE_MOVING_BETWEEN_PARTITIONS_PRESAVE "TransferMovingBetweenPartitionsPreSave"
// like transferring character, but for moves between partitions on the same GS.exe
#define TRANSFERSTATE_MOVING_BETWEEN_PARTITIONS "TransferMovingBetweenPartitions"

// give plenty of time for transition
#define AVERAGE_PARTY_LEVEL_EXPIRE_TIME_SECONDS 240

AUTO_ENUM;
typedef enum TransferFlags
{
	TRANSFERFLAG_CSR = 1<<0,
	TRANSFERFLAG_RECRUITWARP = 1<<1,


	TRANSFERFLAG_BEGAN_CLIENT_SIDE_TRANSFER = 1 << 2,
		//the client side transfer begins when we get a remote command from the mapmanager telling
		//us that getting the address will be slow, OR when we get the address	
	

	TRANSFERFLAG_MOVE_BETWEEN_PARTITIONS_TRANS_COMPLETED = 1 << 3,
	TRANSFERFLAG_PASSED_POINT_OF_NO_RETURN = 1 << 4,

	TRANSFERFLAG_FORCE_SEND_OPTIONS_TO_CLIENT = 1 << 5,
	TRANSFERFLAG_ONLY_EXISTING_MAPS = 1 << 6,
	TRANSFERFLAG_IGNORE_CURRENT_MAP = 1 << 7,

	//when requesting an owned map, it must be a pre-existing one (ie, you're being invited to a
	//stronghold or something)
	TRANSFERFLAG_NO_NEW_OWNED_MAP = 1 << 8,

	TRANSFERFLAG_FORCE_SEND_OPTIONS_TO_CLIENT_IF_GOING_TO_STATIC = 1 << 9,
	TRANSFERFLAG_SPECIFIC_MAP_ONLY = 1 << 10,
	TRANSFERFLAG_IGNORE_ENCOUNTER_SPAWN_LOGIC = 1 << 11,

	TRANSFERFLAG_SHOW_FULL_MAPS = 1 << 12,
} TransferFlags;

AUTO_STRUCT;
typedef struct CharacterTransfer
{	

	EntityRef entRef;
	ClientLink *clientLink; NO_AST
	GlobalType containerType;
	ContainerID containerID;
	ContainerID teamID;

	ContainerID destinationServer;
	GlobalType destinationType;
	U32 destinationPartitionID;

	U32 loginCookie;

	U32 teamTransferQueuedCount;
	bool doingTeamTransfer;
	
	PossibleMapChoices *pPossibleMapChoices;
	int iNumSpecificChoices;

	MapSearchInfo *pMapSearchInfo;
	PossibleMapChoice *pChosenMap;
	ReturnedGameServerAddress *pReturnedAddress;

	char *pReasonString;

	TransferFlags eFlags; AST(FLAGS)

	float fTimeInState; //used only during MOVING_BETWEEN_PARTITIONS state
} CharacterTransfer;

// Call once per frame, and deals with any in-progress map transfers
void gslMapTransferTick(F32 elapsed);

// Gets a login cookie for a map transfer
int gslMapTransferGetLoginCookie(void);

// Deal with a failed map transfer
void gslMapTransferFail(CharacterTransfer *transfer, bool bCancelled, const char *reason);
void gslMapTransferFailf(CharacterTransfer *transfer, bool bCancelled, FORMAT_STR const char *reason, ...);

// When a client disconnects, call this. If it returns true, they disconnected due to a map transfer
bool gslMapTransferHandleDisconnect(ClientLink *link);

// For an in-progress map transfer, choose a specific map, generally called from client
void gslMapTransferChooseAddress(Entity *entity, PossibleMapChoice *pChoice);

void MapMoveStaticEx(SA_PARAM_NN_VALID Entity *ent, SA_PARAM_OP_STR const char *zoneName, SA_PARAM_OP_STR const char* spawnName, int iMapIndex, ContainerID iMapContainerID, U32 uPartitionID, GlobalType eOwnerType, ContainerID ownerID, WorldVariable **eaVariables, TransferFlags eFlags, MapSearchType eSearchType, char *pReason, CmdContext *pContext);
void MapMoveWithDescriptionAndPosRot(SA_PARAM_NN_VALID Entity *ent, SA_PARAM_NN_VALID const MapDescription *pMapDesc, Vec3 vPos, Quat qRot3, char *pReason, bool bCSR);
void MapMoveWithDescription(SA_PARAM_NN_VALID Entity *ent, SA_PARAM_NN_VALID const MapDescription *pMapDesc, char *pReason, TransferFlags eFlags);
void MapMoveOrSpawnWithDescription(SA_PARAM_NN_VALID Entity *ent, SA_PARAM_NN_VALID MapDescription *pMapDesc, char *pReason, TransferFlags eFlags);
void MapMoveFillMapDescriptionEx(MapDescription *pMapDesc, SA_PARAM_NN_STR const char *zoneName, ZoneMapType eMapType, SA_PARAM_OP_STR const char *spawnName, int iMapIndex, ContainerID iMapContainerID, U32 uPartitionID, GlobalType eOwnerType, ContainerID ownerID, const char* pchMapVars);
void MapMoveFillMapDescription(MapDescription *pMapDesc, SA_PARAM_NN_STR const char *zoneName, ZoneMapType eMapType, SA_PARAM_OP_STR const char *spawnName, int iMapIndex, ContainerID iMapContainerID, GlobalType eOwnerType, ContainerID ownerID, WorldVariable **eaVariables);
void MapMoveFromSavedMapDescription(Entity *pEnt, SavedMapDescription *pMap);
void MapMoveFromUGCProject(Entity *pEnt, UGCProject *pProject, CmdContext *pContext);



// Get region rules from map
RegionRules* MapTransferGetRegionRulesFromMapName( const char* pchNextMap );


// Returns true if the Character has a pending transfer OR if there is no NetLink to the client yet
bool characterIsTransferring(Entity *entity);


char *GetVerboseMapMoveComment(Entity *ent, FORMAT_STR const char *pFmt, ...);

CharacterTransfer *gslGetCharacterTransferForEntity(Entity *entity);
#endif
