#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/


#include "ReferenceSystem.h"

typedef struct ResourceInfo ResourceInfo;
typedef struct ItemDef ItemDef;

// Standard modes for searches


// Standard types for search results
// This NEEDS to be the dictionary name
#define SEARCH_TYPE_CRITTER          "CritterDef"
#define SEARCH_TYPE_CRITTERGROUP     "CritterGroup"
#define SEARCH_TYPE_CRITTER_OVERRIDE "CritterOverrideDef"
#define SEARCH_TYPE_ENCOUNTER        "EncounterDef"
#define SEARCH_TYPE_ENCOUNTER_TEMPLATE "EncounterTemplate"
#define SEARCH_TYPE_ITEM             "ItemDef"
#define SEARCH_TYPE_ITEMPOWER        "ItemPowerDef"
#define SEARCH_TYPE_MESSAGE          "Message"
#define SEARCH_TYPE_MICROTRANSACTION "MicroTransactionDef"
#define SEARCH_TYPE_POWER            "PowerDef"
#define SEARCH_TYPE_POWER_STORE      "PowerStore"
#define SEARCH_TYPE_POWER_TREE       "PowerTreeDef"
#define SEARCH_TYPE_QUEUE            "QueueDef"
#define SEARCH_TYPE_REWARD_TABLE     "RewardTable"
#define SEARCH_TYPE_STORE            "Store"
#define SEARCH_TYPE_PLAYERSTAT       "PlayerStatDef"
#define SEARCH_TYPE_MISSIONSET       "MissionSet"
#define SEARCH_TYPE_ALGOPET			 "AlgoPetDef"
#define SEARCH_TYPE_ITEMGEN			 "ItemGenData"
#define SEARCH_TYPE_ITEMASSIGNMENT	 "ItemAssignmentDef"
#define SEARCH_TYPE_PROGRESSION		 "GameProgressionNodeDef"
#define SEARCH_TYPE_DONATIONTASK	 "DonationTaskDef"
#define SEARCH_TYPE_DONATIONTASKDISCOUNT "DonationTaskDiscountDef"
#define SEARCH_TYPE_GROUPPROJECT	 "GroupProjectDef"
#define SEARCH_TYPE_GROUPPROJECTBONUS "GroupProjectBonusDef"
#define SEARCH_TYPE_GP_UNLOCK		 "GroupProjectUnlockDef"
#define SEARCH_TYPE_GP_NUMERIC		 "GroupProjectNumericDef"

// Standard relationship types for search results
#define SEARCH_RELATION_ACTOR          "Actor"
#define SEARCH_RELATION_CHILD          "Child"
#define SEARCH_RELATION_COMBO          "Power Combo"
#define SEARCH_RELATION_MODEXPIRATION  "AttribMod Expiration"
#define SEARCH_RELATION_COMPONENT      "Craft Component"
#define SEARCH_RELATION_CRITTER_POWER  "Critter Power"
#define SEARCH_RELATION_ITEM_ITEMPOWER "Item ItemPower"
#define SEARCH_RELATION_ITEM_POWER     "Item Power"
#define SEARCH_RELATION_PARENT         "Parent"
#define SEARCH_RELATION_RECIPE         "Recipe"
#define SEARCH_RELATION_RECIPE_RESULT  "Result of Recipe"
#define SEARCH_RELATION_REWARD_ITEM    "Reward Item"
#define SEARCH_RELATION_REWARD_ITEMPOWER "Reward ItemPower"
#define SEARCH_RELATION_SELF           "Self"
#define SEARCH_RELATION_UNDERLING      "Critter Underling"
#define SEARCH_RELATION_USES           "Uses"
#define SEARCH_RELATION_CONTAINS       "Contains"


AUTO_STRUCT;
typedef struct SearchItemDefRef
{
	REF_TO(ItemDef) hDef;				AST(REFDICT(ItemDef) STRUCTPARAM)
} SearchItemDefRef;

AUTO_ENUM;
typedef enum ResourceSearchMode
{
	SEARCH_MODE_LIST,				ENAMES("List All of Type")
	SEARCH_MODE_REFERENCES,			ENAMES("List References")
	SEARCH_MODE_USAGE,				ENAMES("Find Usage of Resource")
	SEARCH_MODE_PARENT_USAGE,		ENAMES("Find Usage of Resource Parent") //Need to think of a better name for this
	SEARCH_MODE_TAG_SEARCH,			ENAMES("Search Tags")
	SEARCH_MODE_TAG_COMPLEX_SEARCH,	ENAMES("Complex Search Tags")
	SEARCH_MODE_EXPR_SEARCH,		ENAMES("Search Expressions")
	SEARCH_MODE_DISP_SEARCH,		ENAMES("Search Display Names")
	SEARCH_MODE_FIELD_SEARCH,		ENAMES("Search a Field")
} ResourceSearchMode;

AUTO_STRUCT;
typedef struct ResourceSearchResultRow
{
	char *pcName;					// name of resource that was found (for use with emOpenFileEx)
	char *pcType;					// type of resource that was found (for use with emOpenFileEx)
	char *pcExtraData;				// special data for this search type
	SearchItemDefRef *pResRef;		//If we add more special cases, this may need to be unionized
} ResourceSearchResultRow;
extern ParseTable parse_ResourceSearchResultRow[];
#define TYPE_parse_ResourceSearchResultRow ResourceSearchResultRow

AUTO_STRUCT;
typedef struct ResourceSearchResult
{
	U32 iRequest;

	ResourceSearchResultRow **eaRows;			// result rows
} ResourceSearchResult;
extern ParseTable parse_ResourceSearchResult[];
#define TYPE_parse_ResourceSearchResult ResourceSearchResult

typedef bool (*ResFilterFn)(ResourceInfo* resInfo, UserData data);

AUTO_ENUM;
typedef enum ResourceSearchRequestTagsType
{
	SEARCH_TAGS_NODE_TAG,
	SEARCH_TAGS_NODE_OR,
	SEARCH_TAGS_NODE_AND,
} ResourceSearchRequestTagsType;

typedef struct ResourceSearchRequestTags ResourceSearchRequestTags;

AUTO_STRUCT;
typedef struct ResourceSearchRequestTags
{
	ResourceSearchRequestTagsType type;
	const char* strTag;
	ResourceSearchRequestTags** eaChildren;
} ResourceSearchRequestTags;
extern ParseTable parse_ResourceSearchRequestTags[];
#define TYPE_parse_ResourceSearchRequestTags ResourceSearchRequestTags

AUTO_STRUCT;
typedef struct ResourceSearchRequest
{
	ResourceSearchMode eSearchMode;				// The mode of the search
	ResourceSearchRequestTags* pTagsDetails;	// For tag search, the data to search for
	char *pcSearchDetails;						// Extra parameter for search
	char *pcName;								// name of the resource to search from
	char *pcType;								// type of the resource to search from
	ResFilterFn filterFn;			NO_AST		// A filter function.  will get called on each ResourceInfo, and if returns true then the real search will be done.  Only works for SEARCH_MODE_TAG_SEARCH currently
	UserData filterData;			NO_AST
	U8 one_result : 1;							// As an optimization, stops after the first result. Only works for SEARCH_MODE_TAG_SEARCH currently.

	U32 iRequest;
} ResourceSearchRequest;



// Add a result
ResourceSearchResultRow *createResourceSearchResultRow(const char *pcName, const char *pcType, const char *pcExtraData);

// Continues an existing search. Intended for searching both server and client assets of all types.
ResourceSearchResult *continueResourceSearchRequest(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult);

// Handles a request
__forceinline static ResourceSearchResult *handleResourceSearchRequest(ResourceSearchRequest *pRequest)
{
	return continueResourceSearchRequest(pRequest, NULL);
}



// Register a custom search handler

typedef void CustomResourceSearchHandler(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult);

void registerCustomResourceSearchHandler(ResourceSearchMode eSearchMode, const char *pcType, CustomResourceSearchHandler *pHandler);


// You can call this to fill in generic dictionaries
void SearchUsageGeneric(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult, const char **ppIgnoreDictionaries);

// Tag processing

// Parses the tag string into an earray of tags.  The eArray contains pooled strings.
void tagParse(const char *pcTagString, const char ***peaTags);

// Returns true if all tags in the Test value are present in the Complete value.
bool tagMatchSubset(const char **eaTestTags, const char **eaCompleteTags);

// Adds valid combinations from the TagsToAdd into the Collected value
void tagAddCombinations(const char ***peaTags, const char **eaTagsToAdd);

// Adds valid combinations from the TagString into the Collected value
void tagAddCombinationsFromString(const char ***peaTags, const char *pcTagString);

// Adds all valid combinations represented in the dictionary
void tagGetCombinationsForDictionary(DictionaryHandleOrName dictNameOrHandle, const char ***peaTags);

// Clears the tag cache -- call this once per frame
void tagCacheClear(void);
