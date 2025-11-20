/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMEACCOUNTDATA_H
#define GAMEACCOUNTDATA_H

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "accountnet.h"
#include "microtransactions_common.h"
#include "chatCommon.h"

typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct MicroTransaction MicroTransaction;
typedef struct GamePermissionContainer GamePermissionContainer;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef enum enumTransactionOutcome enumTransactionOutcome;

typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, void *userData);

AUTO_STRUCT AST_CONTAINER;
typedef struct AttribValuePair
{
	CONST_STRING_MODIFIABLE pchAttribute;			AST(PERSIST SUBSCRIBE KEY)
		//the name/key of the attribute

	CONST_STRING_MODIFIABLE pchValue;				AST(PERSIST SUBSCRIBE)
} AttribValuePair;

AUTO_STRUCT;
typedef struct ParsedAVP
{
	STRING_MODIFIABLE pchAttribute;					AST(KEY)
		//The unparsed attribute
	STRING_MODIFIABLE pchValue;
		// The value

	STRING_MODIFIABLE pchGameTitle;
		// The game title
	MicroItemType eType;
		// The type of attribute this is
	STRING_MODIFIABLE pchItemIdent;
		// The Payload string/ItemIdentifier
	int iCount;
		// How many to give
} ParsedAVP;

AUTO_STRUCT;
typedef struct ParsedAVPList
{
	EARRAY_OF(ParsedAVP) eaPairs;
} ParsedAVPList;

// A permission container.  Uses the permissions data in GamePermissionsCommon to expand to GameTokens
AUTO_STRUCT AST_CONTAINER;
typedef struct GamePermission
{
	CONST_STRING_POOLED pchName;			AST(PERSIST KEY POOL_STRING SUBSCRIBE)
		// The name of the permission
} GamePermission;

//These are the actual tokens that do the work
AUTO_STRUCT AST_CONTAINER;
typedef struct GameToken
{
	CONST_STRING_POOLED pchKey;				AST(PERSIST KEY POOL_STRING SUBSCRIBE)
		// The key of the token (e.g. "ZONE:MIL" or "NUMERIC:LEVEL:5").  created automatically
} GameToken;

// Record the last N tips given by this account
AUTO_STRUCT AST_CONTAINER;
typedef struct FoundryTipRecord
{
	const U32 uTimeOfTip;					AST(PERSIST SUBSCRIBE)
	const U32 uTipAuthorAccountID;		AST(PERSIST SUBSCRIBE)
} FoundryTipRecord;


AUTO_STRUCT
AST_IGNORE(bLevelUnrestricted)
AST_IGNORE(bTradePermitted)
AST_IGNORE(bSocialPermitted)
AST_IGNORE(bAllBasicZones)
AST_CONTAINER;
typedef struct GameAccountData
{
	const ContainerID					iAccountID;			AST(PERSIST SUBSCRIBE KEY)
		// Entity's Account ID

	CONST_EARRAY_OF(AttribValuePair)	eaKeys;				AST(PERSIST SUBSCRIBE)
		// Attrib value pairs that mean something to somebody

	CONST_STRING_EARRAY					eaVanityPets;		AST(PERSIST SUBSCRIBE)
		// The powerdef name of vanity pets that have been unlocked

	CONST_EARRAY_OF(AttribValuePair)	eaCostumeKeys;		AST(PERSIST SUBSCRIBE)
		// Attrib value pairs of costumes unlocked.  Separated to keep the count in eaKeys down.

	CONST_EARRAY_OF(MicroTransaction)	eaAllPurchases;		AST(PERSIST FORCE_CONTAINER SUBSCRIBE)
		// The all purchases this player made

	CONST_EARRAY_OF(GamePermission)		eaPermissions;		AST(PERSIST SUBSCRIBE)
		// The game permissions this account has purchased

	CONST_EARRAY_OF(GameToken)			eaTokens;			AST(PERSIST SUBSCRIBE)
		// The game permission tokens this account has (Created from Game Permissions)

	CONST_STRING_MODIFIABLE				pchVoiceUsername;	AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE				pchVoicePassword;	AST(PERSIST SUBSCRIBE SELF_ONLY)
	const int							iVoiceAccountID;	AST(PERSIST SUBSCRIBE)

	const U32 iVersion;										AST(PERSIST SUBSCRIBE)
	
	const U32 iDaysSubscribed;								AST(PERSIST SUBSCRIBE)
		// Number of days as a subscriber rounded up.
		
    //jswdeprecated
	const float fLongPlayTime;								AST(PERSIST SUBSCRIBE)
		// highest total play time of all characters

	const U32 uFirstPlayedTime;								AST(PERSIST SUBSCRIBE)
		// First timestamp at which this user played

	const U32 uTotalPlayedTime_AccountServer;				AST(PERSIST SUBSCRIBE NAME(uTotalPlayedTime))
		// Total playtime according to the Account Server

    const U32 uLastRefreshTime;                             AST(PERSIST SUBSCRIBE)
        // The last time the game account data was refreshed.

	const U32 bLifetimeSubscription : 1;					AST(PERSIST SUBSCRIBE)
		// This account has a lifetime subscription

	const U32 bPress : 1;									AST(PERSIST SUBSCRIBE)
		// This account is a press account

	const U32 bLinkedAccount : 1;							AST(PERSIST SUBSCRIBE)
		// This account is linked to a PWE account

	const U32 bShadowAccount : 1;							AST(PERSIST SUBSCRIBE)
		// This account is a shadow created for a PWE account

    const U32 bBilled : 1;                                  AST(PERSIST SUBSCRIBE)
        // This account has been billed.

	const ChatConfig chatConfig; AST(PERSIST)
	const int iLastChatAccessLevel; AST(PERSIST SUBSCRIBE)
	    // This is actually access level + 1 so that 0 = uninitialized

	CONST_EARRAY_OF(RecruiterContainer) eaRecruiters;	AST(FORCE_CONTAINER PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(RecruitContainer) eaRecruits;		AST(FORCE_CONTAINER PERSIST SUBSCRIBE)

	const bool bUGCAccountCreated;						AST(PERSIST SUBSCRIBE)
	const int iFoundryTipBalance;						AST(PERSIST SUBSCRIBE)
	CONST_EARRAY_OF(FoundryTipRecord)	eaTipRecords;	AST(PERSIST SUBSCRIBE)

    CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) eaAccountKeyValues; AST(FORCE_CONTAINER PERSIST SUBSCRIBE)
        // A cached copy of the account server key/values.  This list is refreshed at login and updated as the account server notifies the shard that key/values are changed.

    const U32 uMaxCharacterLevelCached;                 AST(PERSIST SUBSCRIBE)
        // A cached copy of the max level that any character on the account has achieved.

    CONST_STRING_EARRAY cachedGamePermissionsFromAccountPermissions;    AST(PERSIST SUBSCRIBE POOL_STRING)
        // A cached copy of extra gamepermissions that come from account permissions.  This list is refreshed at login.  It is used when recomputing gamepermission tokens
        //  at times other than login, such as when a player levels up and needs to be granted new level based tokens.

} GameAccountData;

// This structure is used to hold portions of GameAccountData that are
// passed into transactions when we don't want to lock the container.
// Only put things in here that are unlikely to change during a transaction.
AUTO_STRUCT;
typedef struct GameAccountDataExtract
{
	AttribValuePair		**eaKeys;
	AttribValuePair		**eaCostumeKeys;
	GameToken			**eaTokens;
	AccountProxyKeyValueInfoContainer **eaAccountKeyValues;

	U32 iDaysSubscribed;
	U32 bLevelUnrestricted : 1;
	U32 bLinkedAccount : 1;
	U32 bShadowAccount : 1;
} GameAccountDataExtract;
extern ParseTable parse_GameAccountDataExtract[];
#define TYPE_parse_GameAccountDataExtract GameAccountDataExtract

AUTO_STRUCT;
typedef struct SpeciesUnlock
{
	STRING_POOLED pchSpeciesName;			AST(KEY POOL_STRING)
		// The species def name
		char *pchSpeciesUnlockCode;
} SpeciesUnlock;

AUTO_STRUCT;
typedef struct SpeciesUnlockList
{
	SpeciesUnlock **eaSpeciesUnlocks;
} SpeciesUnlockList;

S32 gad_trh_GetAttribInt(ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttrib);
#define gad_GetAttribInt(data, attrib) gad_trh_GetAttribInt(CONTAINER_NOCONST(GameAccountData, data), attrib)
S32 gad_GetAttribIntFromExtract(GameAccountDataExtract *pExtract, const char *pchAttrib);

SA_RET_OP_STR const char* gad_trh_GetAttribString(ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttrib);
#define gad_GetAttribString(data, attrib) gad_trh_GetAttribString(CONTAINER_NOCONST(GameAccountData, data), attrib)
SA_RET_OP_STR const char *gad_GetAttribStringFromExtract(GameAccountDataExtract *pExtract, const char *pchAttrib);

const char* gad_trh_GetVanityPet(ATH_ARG  NOCONST(GameAccountData) *pData, const char *pchPetToFind);
#define gad_GetVanityPet(data, pet) (const char*)gad_trh_GetVanityPet(CONTAINER_NOCONST(GameAccountData, data), pet)

const char* gad_trh_GetCostumeRef(ATH_ARG  NOCONST(GameAccountData) *pData, const char *pchCostumeRef);
#define gad_GetCostumeRef(data, costume) (const char*)gad_trh_GetCostumeRef(CONTAINER_NOCONST(GameAccountData, data), costume)

#define gad_GetLastChatAccessLevel(data) (data->iLastChatAccessLevel ? data->iLastChatAccessLevel-1 : 0)

// Moves an account server key-value pair to the game account data main keys
enumTransactionOutcome slGAD_trh_UnlockCostumeItem(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeItem);
enumTransactionOutcome slGAD_trh_UnlockCostumeItem_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeItem);

// Moves an account server key-value pair to the game account data's costume keys
enumTransactionOutcome slGAD_trh_UnlockCostumeRef(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeRef);
enumTransactionOutcome slGAD_trh_UnlockCostumeRef_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchCostumeRef);

// Transaction to set an attrib in game account data's main keys
enumTransactionOutcome slGAD_trh_SetAttrib(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchKey, const char *pchValue);
enumTransactionOutcome slGAD_trh_SetAttrib_Force(ATR_ARGS, NOCONST(GameAccountData) *pData, const char *pchKey, const char *pchValue);

enumTransactionOutcome slGAD_trh_UnlockSpecies(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, SpeciesUnlock *pSpecies, const char *pchSpeciesUnlockCode);
enumTransactionOutcome slGAD_trh_UnlockSpecies_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, SpeciesUnlock *pSpecies, const char *pchSpeciesUnlockCode);
enumTransactionOutcome slGAD_trh_UnlockAttribValue(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttribValue);
enumTransactionOutcome slGAD_trh_UnlockAttribValue_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchAttribValue);

// Two transaction helpers that change an attrib by an amount
bool slGAD_trh_ChangeAttrib(ATR_ARGS, ATH_ARG  NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange);
bool slGAD_trh_ChangeAttrib_Force(ATR_ARGS, ATH_ARG  NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange);
// This one is clamped at a minimum and maximum
bool slGAD_trh_ChangeAttribClamped(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange, S32 iMin, S32 iMax);
bool slGAD_trh_ChangeAttribClamped_Force(ATR_ARGS, ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey, S32 iChange, S32 iMin, S32 iMax);
bool slGAD_CanChangeAttribClampedExtract(GameAccountDataExtract *pExtract, const char *pchKey, S32 iChange, S32 iMin, S32 iMax);

#endif GAMEACCOUNTDATA_H
