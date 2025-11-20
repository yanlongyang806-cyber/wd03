#pragma once

#include "referencesystem.h"
#include "itemCommon.h"

// Forward declarations for structs and enums
typedef struct CharacterClass CharacterClass;

AUTO_STRUCT;
typedef struct AuctionBrokerItemDropInfo
{
	// The list of items to search for in the auction house
	ItemDefRef  itemDefRef;						AST(NAME(Item))
	const char* pchDropInfo;					AST(NAME(Where))
} AuctionBrokerItemDropInfo;

AUTO_STRUCT;
typedef struct AuctionBrokerLevelInfo
{
	// The start level for the level range
	S32 iLevelRangeStart;						AST(KEY NAME(LevelStart))

	// The end level for the level range
	S32 iLevelRangeEnd;							AST(NAME(LevelEnd))

	AuctionBrokerItemDropInfo **ppItemDropInfo;	AST(NAME(Item))
} AuctionBrokerLevelInfo;
extern ParseTable parse_AuctionBrokerLevelInfo[];
#define TYPE_parse_AuctionBrokerLevelInfo AuctionBrokerLevelInfo

AUTO_STRUCT;
typedef struct AuctionBrokerClassInfo
{
	// Reference to the character class
	REF_TO(CharacterClass) hCharacterClass;		AST(KEY STRUCTPARAM REFDICT(CharacterClass))

	// The list of level information
	AuctionBrokerLevelInfo **ppLevelInfoList;	AST(NAME(LevelInfo))
} AuctionBrokerClassInfo;
extern ParseTable parse_AuctionBrokerClassInfo[];
#define TYPE_parse_AuctionBrokerClassInfo AuctionBrokerClassInfo

AUTO_STRUCT;
typedef struct AuctionBrokerDef
{
	// The logical name of the def
	char *pchName;								AST(KEY STRUCTPARAM POOL_STRING)

	// Used for reloading and error reporting purposes
	const char* pchFilename;					AST(CURRENTFILE)

	// The indexed list of class information
	AuctionBrokerClassInfo **ppClassInfoList;	AST(NAME(Class))
} AuctionBrokerDef;
extern ParseTable parse_AuctionBrokerDef[];
#define TYPE_parse_AuctionBrokerDef AuctionBrokerDef

// Dictionary holding the auction broker defs
extern DictionaryHandle g_hAuctionBrokerDictionary;

// Returns the info for the given character class in the def
const AuctionBrokerClassInfo * AuctionBroker_GetClassInfo(SA_PARAM_NN_VALID AuctionBrokerDef *pDef, SA_PARAM_NN_STR const char *pchCharacterClassName);

// Returns the level info
const AuctionBrokerLevelInfo * AuctionBroker_GetLevelInfo(SA_PARAM_NN_VALID const AuctionBrokerClassInfo *pClassInfo, S32 iLevel);