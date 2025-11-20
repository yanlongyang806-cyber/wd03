/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMEACCOUNTDATA_COMMON_H
#define GAMEACCOUNTDATA_COMMON_H

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "GameAccountData\GameAccountData.h"
#include "Message.h"

typedef U32 ContainerID;
typedef struct Entity Entity;
typedef struct ItemDef ItemDef;
typedef struct GameAccountData GameAccountData;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct PossibleCharacterChoice PossibleCharacterChoice;
typedef struct PossibleCharacterNumeric PossibleCharacterNumeric;

#define NEW_RECRUIT_TIME_LENGTH 1296000 //(86400 * 15) = 15 Days
#define RECRUIT_WARP_TIME 7200 //(2 hours)

extern StashTable s_stashCachedExtracts;

AUTO_STRUCT;
typedef struct RequiredPower
{
	char * pcRequiredPowerPurpose;		AST(NAME(RequiredPowerPurpose))
	char * pcRequiredPowerCategory;		AST(NAME(RequiredPowerCategory))
	S32 iNumberOfRequiredPower;			AST(NAME(NumberOfRequiredPower))
} RequiredPower;

AUTO_STRUCT;
typedef struct RequiredPowersAtCreation
{
	RequiredPower **eaRequiredPowers;	AST(NAME(RequiredPowers))

} RequiredPowersAtCreation;

extern RequiredPowersAtCreation g_RequiredPowersAtCreation;

AUTO_STRUCT;
typedef struct DisallowedWarpMap
{
	STRING_MODIFIABLE pDisallowedWarpMap;				AST(STRUCTPARAM)
} DisallowedWarpMap;

AUTO_STRUCT;
typedef struct DisallowedWarpMaps
{
	DisallowedWarpMap **eaDisallowedWarpMaps;			AST(NAME(WarpRestriction))
} DisallowedWarpMaps;

AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pGameAccountDataNumericPurchaseCategories);
typedef enum GameAccountDataNumericPurchaseCategory
{
	kGameAccountDataNumericPurchaseCategory_None,
	kGameAccountDataNumericPurchaseCategory_UGC,
	// Additional categories are data-defined
	kGameAccountDataNumericPurchaseCategory_FirstDataDefined, EIGNORE
} GameAccountDataNumericPurchaseCategory;

AUTO_STRUCT;
typedef struct GameAccountDataRequiredKeyValue
{
	const char *pchKey;	AST(STRUCTPARAM KEY)
		// The key value

	const char *pchSpecificStringValue; AST(NAME(SpecificStringValue))
		// Specific string value
	
	S32 iMinIntValue; AST(NAME(MinValue))
		// Minimum int value

	S32 iMaxIntValue; AST(NAME(MaxValue) DEFAULT(-1))
		// Maximum int value
} GameAccountDataRequiredKeyValue;

AUTO_STRUCT;
typedef struct GameAccountDataRequiredTokenValue
{
	const char* pchKey; AST(STRUCTPARAM POOL_STRING)
		// The token key

	S32 iValue; AST(NAME(Value) DEFAULT(1))
		// Value of the token
} GameAccountDataRequiredTokenValue;

AUTO_STRUCT;
typedef struct GameAccountDataRequiredValues
{
	GameAccountDataRequiredKeyValue** eaKeyValues;	AST(NAME(KeyValue))
		// Array of key values
	GameAccountDataRequiredTokenValue** eaTokenValues; AST(NAME(Token))
		// Array of permission tokens
} GameAccountDataRequiredValues;

AUTO_STRUCT;
typedef struct GameAccountDataPurchaseKeyValue
{
	const char *pchKey;	AST(STRUCTPARAM KEY)
		// The key value

	const char *pchStringValue; AST(NAME(SetStringValue))
		// Set a specific string value

	S32 iIntValue; AST(NAME(AddIntValue))
		// Increment the existing int value by this amount
	
} GameAccountDataPurchaseKeyValue;

AUTO_STRUCT;
typedef struct GameAccountDataNumericPurchaseDef
{
	const char* pchName; AST(STRUCTPARAM KEY)
		// Internal name

	DisplayMessage msgDisplayName; AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))  
		// Display name

	DisplayMessage msgDescription; AST(NAME(Description) STRUCT(parse_DisplayMessage))  
		// Description message

	const char* pchNumericItemDef; AST(NAME(NumericItemDef) POOL_STRING)
		// The numeric to use to make this purchase

	S32 iNumericCost; AST(NAME(NumericCost))
		// The numeric cost

	EARRAY_OF(GameAccountDataPurchaseKeyValue) eaPurchaseKeyValues; AST(NAME(PurchaseKeyValue))
		// The key values to purchase

	GameAccountDataRequiredValues* pRequire; AST(NAME(Require))
		// Requirements that must be met

	EARRAY_OF(GameAccountDataRequiredValues) eaOrRequire; AST(NAME(OrRequire))
		// Array of OR requirements that must be met

	GameAccountDataNumericPurchaseCategory eCategory; AST(NAME(Category))
		// Category for UI sorting/display

} GameAccountDataNumericPurchaseDef;

AUTO_STRUCT;
typedef struct GameAccountDataNumericPurchaseDefs
{
	EARRAY_OF(GameAccountDataNumericPurchaseDef) eaDefs; AST(NAME(GameAccountNumericPurchaseDef))
} GameAccountDataNumericPurchaseDefs;

extern STRING_EARRAY g_eaDisallowedWarpMaps;
extern GameAccountDataNumericPurchaseDefs g_GameAccountDataNumericPurchaseDefs;

GameAccountDataNumericPurchaseDef* GAD_NumericPurchaseDefFromName(const char* pchDefName);
int GAD_NumericPurchaseCountByCategory(GameAccountDataNumericPurchaseCategory eCategory);
bool GAD_NumericPurchaseGetRequiredNumerics(GameAccountDataNumericPurchaseCategory eCategory, const char*** pppchNumerics);
int GAD_NumericPurchaseGetMinimumCostForAccount(GameAccountData* pData, const char* pchNumeric, GameAccountDataNumericPurchaseCategory eCategory, const char** ppchNumericOut);

GameAccountData *entity_trh_GetGameAccount(ATH_ARG NOCONST(Entity) *pEnt);
GameAccountData *entity_GetGameAccount(Entity *pEnt);

GameAccountDataExtract *entity_GetCachedGameAccountDataExtract(Entity *pEnt);

GameAccountDataExtract *entity_CreateLocalGameAccountDataExtract(const GameAccountData *pData);
void entity_DestroyLocalGameAccountDataExtract(GameAccountDataExtract **ppExtract);
GameAccountDataExtract *entity_trh_CreateGameAccountDataExtract(NOCONST(GameAccountData) *pData);
void entity_trh_DestroyGameAccountDataExtract(GameAccountDataExtract **ppExtract);
GameAccountDataExtract *entity_trh_CreateShallowGameAccountDataExtract(ATH_ARG NOCONST(GameAccountData) *pData);
void entity_trh_DestroyShallowGameAccountDataExtract(GameAccountDataExtract **ppExtract);

S64 gad_trh_GetAccountValueInt(ATH_ARG NOCONST(GameAccountData) *pData, const char *pchKey);
#define gad_GetAccountValueInt(data, key) gad_trh_GetAccountValueInt(CONTAINER_NOCONST(GameAccountData, data), key)
S64 gad_GetAccountValueIntFromExtract(GameAccountDataExtract *pExtract, const char *pchKey);

bool entity_LifetimeSubscription(Entity *pEnt);

bool entity_PressSubscription(Entity *pEnt);

bool entity_LinkedAccount(Entity *pEnt);

bool entity_ShadowAccount(Entity *pEnt);

U32 entity_GetDaysSubscribedFromExtract(GameAccountDataExtract *pExtract);
U32 entity_GetDaysSubscribed(Entity *pEnt);

//  Is the recruit's account ID in this game account data
U32 GAD_AccountIsRecruit(SA_PARAM_NN_VALID const GameAccountData *pData, U32 iAccountID);

// Is the recruiter's account ID in this game account data
U32 GAD_AccountIsRecruiter(SA_PARAM_NN_VALID const GameAccountData *pData, U32 iAccountID);

// Is the target entity a recruit of the source
U32 entity_IsRecruit(Entity *pEntSource, Entity *pEntTarget);

// Is the target entity a recruiter of the source
U32 entity_IsRecruiter(Entity *pEntSource, Entity *pEntTarget);

// Is the target entity a recruit of the source AND newly accepted
S32 entity_IsNewRecruit(Entity *pEntSource, Entity *pEntTarget);

// Is the target entity a recruiter of the source AND newly accepted
S32 entity_IsNewRecruiter(Entity *pEntSource, Entity *pEntTarget);

S32 entity_IsWarpRestricted(const char *pchMapName);

bool GAD_trh_CanMakeNumericPurchaseCheckKeyValues(ATR_ARGS, ATH_ARG NOCONST(GameAccountData)* pData, GameAccountDataNumericPurchaseDef* pPurchaseDef);
#define GAD_CanMakeNumericPurchaseCheckKeyValues(pData, pPurchaseDef) GAD_trh_CanMakeNumericPurchaseCheckKeyValues(ATR_EMPTY_ARGS, CONTAINER_NOCONST(GameAccountData, pData), pPurchaseDef)

bool GAD_trh_EntCanMakeNumericPurchase(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData)* pData, GameAccountDataNumericPurchaseDef* pPurchaseDef, bool bCheckKeyValues);
#define GAD_EntCanMakeNumericPurchase(pEnt, pData, pPurchaseDef, bCheckKeyValues) GAD_trh_EntCanMakeNumericPurchase(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(GameAccountData, pData), pPurchaseDef, bCheckKeyValues)
bool GAD_PossibleCharacterCanMakeNumericPurchase(PossibleCharacterNumeric **eaNumerics, ContainerID iVirtualShardID, GameAccountData* pData, GameAccountDataNumericPurchaseDef* pPurchaseDef, bool bCheckKeyValues);

typedef enum enumTransactionOutcome enumTransactionOutcome;
// The FoundryTips  transaction so I can call in another transaction.
enumTransactionOutcome GameAccount_tr_AddToUGCTips(ATR_ARGS, NOCONST(GameAccountData) *pData, int iAddAmount);

GameAccountDataExtract *GAD_CreateExtract(const GameAccountData *pData);

#define GAD_MODIFICATION_ALERT_STRING "A GAD modification was attempted, but it is disallowed in this game!"
#define RETURN_IF_GAD_MODIFICATION_DISALLOWED(ret) if (gConf.bDontAllowGADModification) { ErrorDetailsf("Transaction: " __FUNCTION__); Errorf(GAD_MODIFICATION_ALERT_STRING); WARNING_NETOPS_ALERT("GAD_MODIFY_ATTEMPTED", GAD_MODIFICATION_ALERT_STRING); return (ret); } (0)

#endif // GAMEACCOUNTDATA_COMMON_H
