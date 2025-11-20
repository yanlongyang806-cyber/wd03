/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLTRANSACTIONS_H_
#define GSLTRANSACTIONS_H_

// Defines transaction call backs and utility functions on the GameServer

#include "Entity.h"
#include "objTransactions.h"

typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(TempPuppetEntity) NOCONST(TempPuppetEntity);

typedef struct CharacterPreSaveInfo		CharacterPreSaveInfo;
typedef struct ChatPlayerList			ChatPlayerList;
typedef struct ChatTeamToJoinList		ChatTeamToJoinList;
typedef struct Container				Container;
typedef struct ContainerSchema			ContainerSchema;
typedef struct PlayerFindFilterStruct	PlayerFindFilterStruct;
typedef struct TempPuppetEntity			TempPuppetEntity;
typedef enum LogoffType LogoffType;

typedef void (*ResolveHandleCallback)(Entity *pEnt, ContainerID uiPlayerID, U32 uiAccountID, U32 uiLoginServerID, void *pData);

// Register all of the entity schemas with the object system
void gslTransactionInit(void);

// Connect to a transaction server
void gslConnectToTransactionServer(void);

// Request a simple transaction, using the based in entity as the subject
void entSimpleTransaction(Entity *ent, char *pTransName, char *msg);
void entSimpleTransactionf(Entity *ent, char *pTransName, FORMAT_STR const char *fmt, ...);
#define entSimpleTransactionf(ent, pTransName, fmt, ...) entSimpleTransactionf(ent, pTransName, FORMAT_STRING_CHECKED(fmt), __VA_ARGS__)

// Makes a backup to the database
void gslSendEntityToDatabase(Entity *ent, bool bRunTransact);
bool gslEntitySafeToSend(Entity *ent);

// Backup ents periodically
void gslEntityBackupTick(void);

// Logs out an entity
//
//one or zero of iSlowCommandID ir iPlayerBooterID can be set
void gslLogOutEntity(Entity *ent, SlowRemoteCommandID iSlowCommandID, U32 iPlayerBooterID);
void gslLogOutEntityEx(Entity *ent, SlowRemoteCommandID iCmdID, U32 iPlayerBooterHandle, LogoffType eLogoffType);
void gslForceLogOutEntity(Entity *ent, const char *pReason);

// Entity is leaving the game server to go back to the lobby
void gslLogOutMeetPartyInLobby(Entity *pEnt);
// Entity is not really logging out but leaving the game server to go back to the character select
void gslLogOutEntityGoToCharacterSelect(Entity *pEnt);
//The normal logoff function
void gslLogOutEntityNormal(Entity *pEnt);
// Disconnect logoff function
void gslLogOutEntityDisconnect(Entity *pEnt);

// Entity is leaving map, due to logout or map transfer
void gslLeaveMap(Entity *ent);

void gslGetPlayerIDFromName(const char* name, U32 iVirtualShardID, TransactionReturnCallback GetPlayerIDFromName_CB, void *userData);
enumTransactionOutcome gslGetPlayerIDFromNameReturn(TransactionReturnVal *returnVal, ContainerID *returnID);
void gslGetPlayerIDFromNameWithRestore(const char* name, U32 iVirtualShardID, TransactionReturnCallback GetPlayerIDFromName_CB, void *userData);
enumTransactionOutcome gslGetPlayerIDFromNameWithRestoreReturn(TransactionReturnVal *returnVal, ContainerID *returnID);
void gslGetPetIDFromName(const char* name, ContainerID iVirtualShardID, TransactionReturnCallback GetPetIDFromName_CB, void *userData);
enumTransactionOutcome gslGetPetIDFromNameReturn(TransactionReturnVal *returnVal, ContainerID *returnID);
void gslGetContainerOwnerFromName(const char* name, ContainerID iVirtualShardID, TransactionReturnCallback GetContainerOwnerFromName_CB, void *userData);
enumTransactionOutcome gslGetContainerOwnerFromNameReturn(TransactionReturnVal *returnVal, char **owner);
void gslGetPlayerNameFromID(ContainerID id, TransactionReturnCallback GetPlayerIDFromName_CB, void *userData);
enumTransactionOutcome gslGetPlayerNameFromIDReturn(TransactionReturnVal *returnVal, char **returnName);
void gslGetAccountIDFromName(const char* name, ContainerID iVirtualShardID, TransactionReturnCallback GetPlayerIDFromName_CB, void *userData);
enumTransactionOutcome gslGetAccountIDFromNameReturn(TransactionReturnVal *returnVal, U32 *returnID);
void gslFindPlayers(PlayerFindFilterStruct *pFilters, TransactionReturnCallback FindPlayers_CB, void *userData);
enumTransactionOutcome gslFindPlayersReturn(TransactionReturnVal *returnVal, ChatPlayerList **pList);
void gslFindTeams(PlayerFindFilterStruct *pFilters, TransactionReturnCallback FindPlayers_CB, void *userData);
enumTransactionOutcome gslFindTeamsReturn(TransactionReturnVal *returnVal, ChatTeamToJoinList **pList);


// Overridable function to do per-project login fixup
LATELINK;
void PlayerLoginFixup(NOCONST(Entity) *newPlayer);

LATELINK;
void PetLoginFixup(NOCONST(Entity) *newPlayer);

void trhTempPuppetPreSave(ATH_ARG NOCONST(TempPuppetEntity) *pTempPuppet, NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo);
enumTransactionOutcome trCharacterPreSave(ATR_ARGS, NOCONST(Entity) *ent, NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo);

void gslPlayerResolveHandle(	Entity *pEnt, const char *pcInputName, 
								ResolveHandleCallback pSuccessCallback, 
								ResolveHandleCallback pFailureCallback, 
								void* pCallbackData);

#endif
