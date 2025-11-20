/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ASLLOGINCHARACTERSELECT_H_
#define ASLLOGINCHARACTERSELECT_H_

#include "GlobalTypeEnum.h"
#include "TransactionOutcomes.h"
#include "objTransactions.h"

typedef struct Entity Entity;
typedef struct LoginLink LoginLink;
typedef struct Packet Packet;
typedef struct PlayerTypeConversion PlayerTypeConversion;
typedef struct TransactionRequest TransactionRequest;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PossibleCharacterChoice PossibleCharacterChoice;
typedef struct PossibleCharacterChoices PossibleCharacterChoices;
typedef struct GameAccountData GameAccountData;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct AvailableCharSlots AvailableCharSlots;
typedef struct AccountProxyKeyValueInfoList AccountProxyKeyValueInfoList;
typedef struct GameAccountData GameAccountData;
typedef struct AccountProxyKeyValueInfo AccountProxyKeyValueInfo;
typedef struct AccountProxyKeyValueInfoContainer AccountProxyKeyValueInfoContainer;
typedef struct VirtualShard VirtualShard;
typedef struct Login2CharacterCreationData Login2CharacterCreationData;
typedef enum PlayerType PlayerType;

// Internal login server functions dealing with character select

void aslLoginHandlePossibleCharactersJSON(Packet *pak, LoginLink *loginLink);
void aslLoginHandleCharacterChoice(Packet *pak, LoginLink *loginLink);
void aslLoginHandleCharacterChoiceByContainerId(Packet *pak, LoginLink *loginLink);
void aslLoginHandleCharacterDelete(Packet *pak, LoginLink *loginLink);
void aslLoginHandleCharacterUndelete(Packet *pak, LoginLink *loginLink);
void aslLoginHandleConvertCharacterPlayerType(Packet *pak, LoginLink *loginLink);
void aslLoginHandleChangeCharacterName(Packet *pak, LoginLink *loginLink);
void aslLoginHandleCreationDataRequest(Packet *pak, LoginLink *loginLink);
void aslLoginHandleQueuedCharacterChoice(LoginLink *loginLink);
void aslLoginHandleAccountSetPlayerType(Packet *pak, LoginLink *loginLink);
void aslLoginGoToCharacterSelect(LoginLink *loginLink);
void aslLoginRequestExistingCharacter(LoginLink *loginLink);

AUTO_ENUM;
typedef enum eLoginBootType
{
	ASLLOGINBOOT_NORMAL,
	ASLLOGINBOOT_WAITING_FOR_BOOT_BEFORE_CHARACTER_CHOICES,
} eLoginBootType;

void aslLoginBootPlayer(LoginLink *loginLink, ContainerID playerID, const char *playerName, eLoginBootType eBootType);

LATELINK;
bool gameSpecific_PreInitNewCharacter(NOCONST(Entity) *ent, Login2CharacterCreationData* pChoice, GameAccountDataExtract* pExtract);
LATELINK;
bool gameSpecific_PostInitNewCharacter(NOCONST(Entity) *ent, Login2CharacterCreationData* pChoice, GameAccountDataExtract* pExtract);

LATELINK;
int player_BuildCreate(NOCONST(Entity) *ent);

// Handles project-specific part of Character PlayerType conversion, should return
//  false if something went wrong.
LATELINK;
S32 login_ConvertCharacterPlayerType(Entity *pEnt, int accountPlayerType, TransactionReturnCallback cbFunc, void *cbData);

// handles project-specific charter map moves. Move characters off maps that either no longer exist, or places them back
// into common areas. Uses the player fixup number to determine this.
LATELINK;
S32 login_updateCharactersMapType(LoginLink *loginLink, Entity *pEnt);

// A game specific function that can do extra initialization of GameAccountData when it is created
LATELINK;
GameAccountData *gameSpecific_GameAccountDataCreateInit(void);

// Helpers for validating the common parts of PlayerType conversion and handling the result
enumTransactionOutcome asl_trh_ValidPlayerTypeConversion(ATR_ARGS, NOCONST(Entity)* pEnt, PlayerType newPlayerType);

void aslLoginGameAccountMakeNumericPurchase(Packet* pak, LoginLink *loginLink);

AvailableCharSlots *BuildAvailableCharacterSlots(GameAccountData *pGameAccount, int clientAccessLevel);
void RemoveUsedCharacterSlots(PossibleCharacterChoices *possibleCharacterChoices);

VirtualShard* aslGetVirtualShardByID(U32 iVShardContainerID);
bool aslCanPlayerAccessVirtualShard(CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) accountKeyValues, U32 iVShardContainerID);

bool aslIsVirtualShardEnabled(int clientAccessLevel, VirtualShard *pShard);
bool aslIsVirtualShardEnabledByID(int clientAccessLevel, U32 iVShardContainerID);
bool aslIsVirtualShardAvailable(int clientAccessLevel, bool hasUGCProjectSlots, VirtualShard *pShard);
bool aslIsVirtualShardAvailableByID(int clientAccessLevel, bool hasUGCProjectSlots, U32 iVShardContainerID);

#endif