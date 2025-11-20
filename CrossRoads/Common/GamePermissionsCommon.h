/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMEPERMISSIONS_COMMON_H
#define GAMEPERMISSIONS_COMMON_H

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "item/itemEnums.h"

#define GAME_PERMISSION_CHAT_ZONE "Zone"

typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct GameAccountData GameAccountData;
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct NOCONST(GameToken) NOCONST(GameToken);
typedef struct GameToken GameToken;

#define GAME_PERMISSION_NUMERIC "Numeric"

#define NO_NUMERIC_LIMIT 0x7fffffff
#define GAME_PERMISSION_CAN_SEND_TELL "Chat.Tell"
#define GAME_PERMISSION_CAN_SEND_ZONE "Chat.Zone"
#define GAME_PERMISSION_CAN_CREATE_GUILD "Guild.Create"
#define GAME_PERMISSION_CAN_JOIN_GUILD "Guild.Join"
#define GAME_PERMISSION_CAN_INVITE_INTO_GUILD "Guild.Invite"
#define GAME_PERMISSION_CAN_ADD_FRIEND "Social.Friend"
#define GAME_PERMISSION_CAN_INVITE_INTO_TEAM "Social.Team"
#define GAME_PERMISSION_CAN_TRADE "Social.Trade"					// the ability to trade
#define GAME_PERMISSION_CAN_USE_MARKET "Social.Market"				// the ability to use the market
#define GAME_PERMISSION_CAN_SEND_MAIL "Mail.Send"
#define GAME_PERMISSION_POWER_HUE "Power.Hue"						// the ability to change power hues
#define GAME_PERMISSION_POWER_EMIT "Power.Emit"						// the ability to change power emitters
#define GAME_PERMISSION_POWER_PET_COSTUME "Power.Pet"				// the ability to change pet power costume
#define GAME_PERMISSION_CHAT_MAIL_FULL_RATE "Chat.Full"				// full chat and mail rates
#define GAME_PERMISSION_COSTUME_EXTRA_BY_LEVEL "Costume.Extra"		// extra costumes by level
#define GAME_PERMISSION_BANK_BUILD_SWAP "Inv.BankBuild"				// Can use bank for build swaps
#define GAME_PERMISSION_PET_EXTRASLOTS "Pet.ExtraSlots"				// Can use the extra slots given thru ranking up of officer defs
#define GAME_PERMISSION_SHARED_BANK "Inv.SharedBank"				// Can access the shared bank
#define GAME_PERMISSION_INVENTORY_SLOTS_FROM_NUMERIC "Inv.SlotsFromNumeric"     // extra inventory slots are granted with AddInvSlots numeric
#define GAME_PERMISSION_BANK_SLOTS_FROM_NUMERIC "Inv.BankSlotsFromNumeric"      // extra bank slots are granted with the BankSize numeric
#define GAME_PERMISSION_UGC_CAN_REPORT_PROJECT "UGC.CanReportProject"

// numerics
#define GAME_PERMISSION_NUMERIC_LEVEL "Level"

// These are values but not numerics
#define GAME_PERMISSION_CHARACTER_SLOTS "CharSlots"
#define GAME_PERMISSION_UGC_PROJECT_SLOTS "UGCProjectSlots"
#define GAME_PERMISSION_UGC_FREE_CHAR_SLOTS "UGCFreeCharSlots"
#define GAME_PERMISSION_MAX_AUCTIONS "MaxAuctions"	
#define GAME_PERMISSION_AUCTION_EXPIRE "AuctionExpire"	
#define GAME_PERMISSION_AUCTION_POST_PERCENT "PostFee"	
#define GAME_PERMISSION_AUCTION_MIN_POST_FEE "MinPostFee"	
#define GAME_PERMISSION_AUCTION_SOLD_PERCENT "SoldFee"	
#define GAME_PERMISSION_DOWNGRADE_CHARACTER_SLOTS "DownCharSlots"
#define GAME_PERMISSION_FREEINTERIORPURCHASES "FreeInteriorPurchases"
#define GAME_PERMISSION_TAILORDISCOUNT "TailorDiscount"
#define GAME_PERMISSION_SHARED_BANK_SLOTS "SharedBankSlots"
#define GAME_PERMISSION_FREE "Free"
#define GAME_PERMISSION_OWNED "Owned"
#define GAME_PERMISSION_FREE_BAGS "FreeBags"					// The number of free player bags

// use to record a best time for players that have cash or purchased something for the current game
#define GAME_PERMISSION_MAX_TIME 9999999.0f		// ~2777 hours worth of seconds

AUTO_ENUM;
typedef enum GameTokenType
{
	kGameToken_None,
		//Not a specific token, things like "_Gold" "_Silver" "_Premium" "Lifetime" etc are these kind of tokens.  They are just information
	kGameToken_CharType,
		//The character creation type, Premium, Standard
	kGameToken_Numeric,
		// A numeric limit.  Used for Resources, Levels etc
	kGameToken_Zone,
		// Permission to go to a zone
	kGameToken_Mail,
		// Use of mail
	kGameToken_Chat,
		// Use of chat
	kGameToken_PowerSet,
		// Power Sets
	kGameToken_Guild,
		// Use of guild
	kGameToken_Social,
		// other social features such as friend, trade
	kGameToken_Power,
		// Power permissions such as tinting, emit points
	kGameToken_Value,
		// Used for systems that require a number but not an actual numeric
	kGameToken_Inv,
		// Used for systems that deal with inventory permissions
	kGameToken_CostumeSet,
		// Unlocks a specific costume set
	kGameToken_Costume,
		// information about costumes (extra permissions etc)
	kGameToken_Pet,
		// Information about pets (bridge officers)
	kGameToken_Interior,
		// Information about Interiors/Hideouts
    kGameToken_AllegianceCharSlots,
        // Allegiance restricted character slots.  Token strings will be of the form AllegianceCharSlots.AllegianceName.NumSlots
    kGameToken_Reward,
        // Reward system related tokens
    kGameToken_Unlock,
        // A token that unlocks a feature or content
	kGameToken_UGC,
		// Something related to UGC, like ability to review
} GameTokenType;

//This is the structure read in from the game permissions file.  Used to create GameTokens when passed through the PermissionTokenizer
AUTO_STRUCT;
typedef struct GameTokenText
{
	GameTokenType eType;
		//The type of this token

	char *pchKey;						AST(NAME(Key))
		//Optional key.  Used for numeric limits

	char *pchValue;						AST(NAME(Value))
		//The value of this token

} GameTokenText;

AUTO_ENUM;
typedef enum GamePermissionType
{
	kGamePermission_Normal,
		// This is just a normal permission
	kGamePermission_Base,
		// This is the 'F2P' permission.
	kGamePermission_Premium,
		// This is the 'Premium' or 'Gold' permission.  Subscription accounts get these values

	//kGamePermission_SuperPremium
		
} GamePermissionType;

AUTO_STRUCT;
typedef struct LevelRestrictedTokens
{
    // Only grant this token if any character on the account has ever been above this level.  The max level gained by any character on the account
    //  is tracked by the account server.
    U32 uMinAccountLevel;               AST(NAME(MinAccountLevel))

    // If true will grant these tokens if the player has ever been billed, regardless of level.
    bool allowIfBilled;                 AST(NAME(AllowIfBilled))

    // Tokens to grant.
    GameTokenText **eaTextTokens;		AST(NAME(Token))
} LevelRestrictedTokens;

//These are the game permission definitions
AUTO_STRUCT;
typedef struct GamePermissionDef
{
	char *pchName;						AST(NAME(Name) KEY POOL_STRING)
		//The name of the permission

	const char *pchFile;				AST(CURRENTFILE)
		//The current file (for reloading)

	GameTokenText **eaTextTokens;		AST(NAME(Token))
		//This game permission def has these tokens it provides

	GameTokenText **eaNotPresentTokens;	AST(NAME(NotPresentToken))
		//These tokens are given to players who to not have this permission.

    LevelRestrictedTokens **eaLevelRestrictedTokens;
        //Tokens that are granted at various levels.

	GamePermissionType eType;
		//Normal, Base, Premium

} GamePermissionDef;

AUTO_STRUCT;
typedef struct FromToDate
{
	// Date/time that this starts at
	const char *pcFromDateString;			AST(POOL_STRING)
	// Date/time that this ends at
	const char *pcToDateString;				AST(POOL_STRING)

	// The calculated value that this starts at
	U32 uFromTimeSeconds;
	// The calculated value this ends at
	U32 uToTimeSeconds;

}FromToDate;

AUTO_STRUCT;
typedef struct GamePermissionTimed
{
	// hours required before the character gets this token
	U32 iHours;										AST(NAME(GamePermissionHours))	
	// the permissions at this time level
	EARRAY_OF(GamePermissionDef) eaPermissions;		AST(NAME(Permission))
	// The number of days subscribed that will unlock this
	U32 iDaysSubscribed;							AST(NAME(GamePermissionDaysSubscribed))
	// Number of seconds since200 required before this is available
	U32 iStartSeconds;								AST(NAME(GamePermissionStartSeconds))

	// The earray of valid date ranges for this timed permission
	EARRAY_OF(FromToDate) eaFromToDates;			AST(NAME(FromToDates))

}GamePermissionTimed;

AUTO_STRUCT;
typedef struct GamePermissionBagRestriction
{
	InvBagIDs	eBagID;					AST(KEY NAME(BagID) SUBTABLE(InvBagIDsEnum))
	const char *pchValue;				AST(NAME(Value))
		// this matches a token value which removes the restriction

	bool		bCanBuyBag;				AST(NAME(CanBuyBag))
		// Can this bag be purchased if it is not yet unlocked?
	
}GamePermissionBagRestriction;

AUTO_STRUCT;
typedef struct GamePermissionDefs
{
	GamePermissionDef **eaPermissions;								AST(NAME(Permission))
	EARRAY_OF(GamePermissionBagRestriction) eaInvBagRestrictions;	AST(NAME(InvBagRestrictions))
	// the list of tokens that are activated upon a character reaching N amount of play time
	EARRAY_OF(GamePermissionTimed) eaTimedTokenList;				AST(NAME(TimedTokenList))

	// Uses free bag permission to find the first 'purchase bag'
	bool bUseFreeBag;

    char *pLoadingComment; AST(ESTRING, FORMATSTRING(HTML_PREFORMATTED=1))
} GamePermissionDefs;

S32 GamePermission_GADHasToken(GameAccountData *pAccountData, const char *pchToken);
S32 GamePermission_EntHasToken(SA_PARAM_OP_VALID Entity *pEnt, const char *pchToken);
	//Does this entity have that token, returns true if game permissions are off

S32 GamePermission_EntHasPermission(SA_PARAM_OP_VALID Entity *pEnt, const char *pchPermission);
	//Does this entity have that permission, returns true if game permissions are off

S32 GamePermission_HasTokenType( SA_PARAM_OP_VALID GameAccountData *pData, GameTokenType eType, const char *pchToken);
	// Does this game account data have a token of this type.  Forms a string <Type>.<Token> to determine if it exists

S32 GamePermission_HasTokenKeyType(GameAccountData *pData, GameTokenType eType, const char *pchKey, const char *pchToken);
	// Does this game account data have a token of this type.  Forms a string <Type>.<Key>.<Token> to determine if it exists

void gamePermission_trh_GetTokens(GamePermissionDef *pDef, NOCONST(GameToken) ***peaTokens);
	// Fills peaTokens with the tokens pDef creates.
void gamePermission_trh_GetTokensEx(EARRAY_OF(GameTokenText) pDefTokens, ATH_ARG NOCONST(GameToken) ***peaTokens);
	// Fills peaTokens with the passed in tokens.
void gamePermission_GetTokenKeys(GamePermissionDef *pDef, const char ***pppchTokens);
	// Fiils pppchTokens with the token keys pDef creates.

bool gamePermission_Enabled();
	//Returns whether game permissions are enabled or not

void gamePermissions_FillNameEArray(char ***peaNames);
	//Used in the microtransaction editor to get the list of permissions

GameTokenText *gamePermission_TokenStructFromString(SA_PARAM_OP_STR const char *pchString);
	//Returns a game token text structure given a string.  Returns null of pchString is null

extern GamePermissionDefs g_GamePermissions;
	// All the permissions in the system

extern GamePermissionDef *g_pPremiumPermission;
	// The premium permission.  AKA "Gold"

extern GamePermissionDef *g_pBasePermission;
	// The base permissions.  AKA "Silver"

extern int g_bDebugF2P;
	// Is F2P debugging on?  Turns off trusted IP on the login server. Uses standard permissions.

extern int g_bDebugF2PPremium;
    // Is F2P debugging on?  Turns off trusted IP on the login server. Uses premium permissions.

extern const char *g_PlayerVarName;
	// Used in the expressions for the player context.  Defined in mission_common.c

void GenerateGameTokenKey(char **estrBuffer, GameTokenType eType, const char *pchKey, const char *pchValue);
	// Generates a string token from the type, key and value of a game token

bool GamePermissions_trh_GetNameKeyValue(ATH_ARG NOCONST(GameToken) *pToken, char **esName, char **esKey, S32 *piValue);
#define gamePermissions_GetNameKeyValue(pToken, esName, esKey, piValue) GamePermissions_trh_GetNameKeyValue(CONTAINER_NOCONST(GameToken, pToken), esName, esKey, piValue)
	// Get the name, key and value for a token
	
bool GamePermissions_trh_GetPermissionValueUncached(ATH_ARG NOCONST(GameAccountData) *pData, const char *pcKey, S32 *piValue);
#define GetGamePermissionValueUncached(pData, pcKey, piValue) GamePermissions_trh_GetPermissionValueUncached(CONTAINER_NOCONST(GameAccountData, pData), pcKey, piValue)
	// return true if a token with this value is found. Return the highest value of the token
	
S32 GamePermissions_trh_GetCachedMaxNumeric(ATH_ARG NOCONST(Entity) *pEntity, const char *pcNumeric, bool bMustBeNumeric);
	// get maximum numeric or value, if not present return very high value (0x7fffffff)
	
S32 GamePermissions_trh_GetCachedMaxNumericEx(ATH_ARG NOCONST(Entity) *pEntity, const char *pcNumeric, bool bMustBeNumeric, bool *bFound);
	// get maximum numeric or value, if not present return very high value (0x7fffffff), this function can return a found true false
	
bool GamePermissions_trh_CanAccessBag(ATH_ARG NOCONST(Entity) *pEntity, InvBagIDs bagId, GameAccountDataExtract *pExtract);
	// Can this entity access this bag?

bool GamePermissions_trh_CanBuyBag(ATH_ARG NOCONST(Entity) *pEntity, InvBagIDs bagId, GameAccountDataExtract *pExtract);
	// can this bag be purchased if it is locked?
	
const char *GamePermissions_GetBagPermission(InvBagIDs bagId);
	// The permission that grants this entity access to the bag.
	
S32 GamePermissions_trh_GetNumberOfRestrictedBags(ATH_ARG NOCONST(Entity) *pEntity, GameAccountDataExtract *pExtract);
	// return number of bag slots that the player still has restricted
	
S32 GamePermission_ExtractHasToken(GameAccountDataExtract *pExtract, const char *pchToken, bool bTrueIfNoGamePermission);
	// does the entity have this token (helper version). If game permissions are off the the last parameter is returned
	
bool GamePermissions_PermissionGivesToken(GamePermissionDef *pPermission, const GameTokenText *pSearchToken);
	// returns true if the given permission provides the given token.

bool GamePermissions_PremiumGivesToken(const GameTokenText *pToken);
	// returns true if the global premium permission provides the given token.

bool GamePermissions_BaseGivesToken(const GameTokenText *pToken);
	// returns true if the global base permission provides the given token.

void GamePermissions_SetPlayerConversionState(bool bConversionActive);
	// Sets the static "conversion active" flag so that bag access can return true during this period of time

bool GamePermissions_InFromToDate(S32 iIndex, U32 uSeconds);
	// return true if the time is in a from to range block

void GamePermissions_SetBaseAndPremium(void);
    // After loading GamePermissions, call this function to set the globals that point to Base and Premium permissions.

int *GamePermissions_GetLevelList(void);
    // Get an array of levels that can unlock permissions.

#endif //GAMEPERMISSIONS_COMMON_H
