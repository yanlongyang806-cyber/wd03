/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"
#include "GlobalTypeEnum.h"

typedef struct GameProgressionNodeDef GameProgressionNodeDef;
typedef struct InventoryBag InventoryBag;
typedef struct Entity Entity;
typedef struct MissionDef MissionDef;
typedef struct QueueDef QueueDef;
typedef enum GameProgressionNodeType GameProgressionNodeType;
typedef enum MissionCreditType MissionCreditType;

#define GAME_CONTENT_NODE_BASE_DIR "defs/contentnodes"

AUTO_ENUM;
typedef enum GameContentNodeType
{
	GameContentNodeType_None = 0, // Invalid type
	GameContentNodeType_GameProgressionNode,
	GameContentNodeType_UGC,
	GameContentNodeType_Event,
	GameContentNodeType_Mission,
	GameContentNodeType_Queue,
} GameContentNodeType;

// This structure should usually be used instead of a REF_TO(GameProgressionNodeDef).  It stores the
// ref *or* a UGCProjectID, in case you are going there.
AUTO_STRUCT;
typedef struct GameContentNodeRef
{
	// The type of the content
	GameContentNodeType eType;

	// UGC Project ID
	ContainerID iUGCProjectID;						AST(NAME("UGCProjectID"))

	// The name of the UGC series this is associated with (if any)
	ContainerID iUGCProjectSeriesID;

	// The unique node id, only should be set for groups inside a series
	int iUGCProjectSeriesNodeID;

	// The unique name of the event
	const char *pchEventName;						AST(POOL_STRING NAME("Name"))

	// Reference to a game progression node
	REF_TO(GameProgressionNodeDef) hNode;			AST(NAME("Node") NAME("DestinationNode"))

	// Reference to a queue def
	REF_TO(QueueDef) hQueue;						AST(NAME("Queue") REFDICT(QueueDef))

	// Reference to a mission def
	REF_TO(MissionDef) hMission;					AST(NAME("Mission") REFDICT(Mission))
} GameContentNodeRef;
extern ParseTable parse_GameContentNodeRef[];
#define TYPE_parse_GameContentNodeRef GameContentNodeRef

AUTO_STRUCT;
typedef struct GameContentNodePartyCountResult
{
	// The reference to the progression node or the UGC project
	GameContentNodeRef nodeRef;

	// The number of game sessions playing the progression node or the UGC project
	U32 iNumSessions;

} GameContentNodePartyCountResult;
extern ParseTable parse_GameContentNodePartyCountResult[];
#define TYPE_parse_GameContentNodePartyCountResult GameContentNodePartyCountResult

AUTO_STRUCT;
typedef struct GameContentNodePartyCountResultCache
{
	U32 iLastUpdateTime;

	GameContentNodePartyCountResult *pLastResult;

	bool bQueryInProgress;

} GameContentNodePartyCountResultCache;
extern ParseTable parse_GameContentNodePartyCountResultCache[];
#define TYPE_parse_GameContentNodePartyCountResultCache GameContentNodePartyCountResultCache

AUTO_ENUM;
typedef enum GameContentListType
{
	GameContentListType_None = 0, // Invalid type
	GameContentListType_Browse,
	GameContentListType_Featured,
	GameContentListType_Search,
} GameContentListType;

AUTO_STRUCT;
typedef struct TimeSensitiveGameContentNodeInfo
{
	// The date content starts
	U32 uStartDate;

	// The date content ends
	U32 uEndDate;

	// Indicates the map name the player will be warped to if applicable
	const char *pchSpawnMap; AST(POOL_STRING)

	// Indicates whether the content is active at the moment
	U32 bActive : 1;

	// The queue that the content is linked with
	REF_TO(QueueDef) hQueue;
} TimeSensitiveGameContentNodeInfo;

AUTO_STRUCT;
typedef struct GameContentNodeMapEntry
{
	float xPercent;					AST(NAME("XPercent"))
	float yPercent;					AST(NAME("YPercent"))
	const char* textureName;		AST(NAME("TextureName") POOL_STRING)
	const char* strContentRefName;	AST(NAME("ContentRefName"))
} GameContentNodeMapEntry;
extern ParseTable parse_GameContentNodeMapEntry[];
#define TYPE_parse_GameContentNodeMapEntry GameContentNodeMapEntry

AUTO_STRUCT;
typedef struct GameContentNode
{
	// The reference to the actual content
	GameContentNodeRef contentRef;

	// The name for this GameContentNode
	char* strContentRefName;								AST( NAME( "ContentRefName" ))

	// The type of the progression node
	GameProgressionNodeType eProgressionNodeType;

	// The depth of the node in the tree
	S32 iDepth;

	// Display name for the game progression node
	const char* strDisplayName;								AST( NAME( "DisplayName" ) )

	// Summary
	const char* strSummary;									AST( NAME( "Summary" ) )

	// Teaser
	const char* pchTeaser;									AST( NAME( "Teaser" )  )

	// Rating, mainly valid for UGC
	F32 fRating;											AST( NAME( "Rating" ))

	// The art file name
	const char *pchArtFileName;								AST( POOL_STRING )

	// Parent of this node. Used for backtracking
	struct GameContentNode *pParent;					NO_AST

	// The number of child nodes
	S32 iNumChild;

	// The number of child nodes which are unlocked
	S32 iNumChildUnlocked;

	// The number of child nodes which are unlocked  (used for team progress setter)
	S32 iNumChildUnlockedForTeam;


	// The number of child nodes which are completed
	S32 iNumChildCompleted;

	// The number of child nodes which are completed  (used for team progress setter)
	S32 iNumChildCompletedForTeam;

	// The suggested player level for this content
	S32 iSuggestedPlayerLevel;

	// Fields below only applies to the nodes of type kGameProgressionNodeType_MissionGroup

	// Indicates whether this mission group is major
	bool bMajor;

	// Estimated time to complete this mission group
	U32 uiTimeToComplete;

	// Time sensitive game content information (optional)
	TimeSensitiveGameContentNodeInfo *pTimeSensitiveContentInfo;

	// Map layout for this node
	GameContentNodeMapEntry** eaMapEntries;
} GameContentNode;

AUTO_STRUCT;
typedef struct GameContentNodeSearchResults
{
	GameContentNode **eaResults;
} GameContentNodeSearchResults;
extern ParseTable parse_GameContentNodeSearchResults[];
#define TYPE_parse_GameContentNodeSearchResults GameContentNodeSearchResults

AUTO_STRUCT;
typedef struct GameContentNodeMissionReward
{
	// The mission
	REF_TO(MissionDef) hMissionDef;	AST(REFDICT(Mission))

	// The credit type
	MissionCreditType eCreditType;

	// The inventory bags containing the rewards
	InventoryBag** eaRewardBags;	AST(NO_INDEX)

} GameContentNodeMissionReward;

AUTO_STRUCT;
typedef struct GameContentNodeRewardResult
{
	// The game content node that the result is stored for.
	GameContentNodeRef nodeRef;

	// Mission rewards
	GameContentNodeMissionReward **eaMissionRewards;

} GameContentNodeRewardResult;
extern ParseTable parse_GameContentNodeRewardResult[];
#define TYPE_parse_GameContentNodeRewardResult GameContentNodeRewardResult

// Used for sending an entity to the webrequest server
AUTO_STRUCT;
typedef struct LobbyEntityContainer
{
	Entity *pEnt;
} LobbyEntityContainer;

AUTO_STRUCT;
typedef struct RecommendedPlayerContentForLevel
{
	// Player level
	S32 iLevel;								AST(KEY STRUCTPARAM)

	// The list of content
	GameContentNodeRef **ppContent;			AST(NAME("ContentInfo"))
} RecommendedPlayerContentForLevel;

AUTO_STRUCT;
typedef struct RecommendedPlayerContent
{
	// The unique name
	const char *pchName;					AST(KEY STRUCTPARAM)

	// The scope
	const char* pchScope;					AST(POOL_STRING)

	// Used for reloading
	const char* pchFilename;				AST(CURRENTFILE)

	// An array of recommended content per level
	RecommendedPlayerContentForLevel **ppRecommendedPlayerContentForLevels;	AST(NAME("Level"))
} RecommendedPlayerContent;

AUTO_STRUCT;
typedef struct PlayerSpecificRecommendedContent
{
	// The name of the list where the content came from
	const char *pchRecommendedContentListName;		AST(POOL_STRING KEY)

	// Actual content
	GameContentNode contentNode;
} PlayerSpecificRecommendedContent;

AUTO_STRUCT;
typedef struct HomePageContentInfo
{
	// The time this list is generated (Client fills this in, since client is the one periodically requesting this data)
	U32 uDataGenerationTimestamp;

	// The list of recommended content
	PlayerSpecificRecommendedContent **ppPlayerSpecificRecommendedContent;

} HomePageContentInfo;

AUTO_STRUCT;
typedef struct LobbyConfig
{
	// The array of recommended content list names used when the home page content is pulled
	const char **ppchHomePageRecommendedContentLists;		AST(NAME(HomePageRecommendedContentListName) POOL_STRING)

	// If this string is defined, home page will pull the recommended progression node from this story
	const char *pchStoryNameForRecommendedProgressionNodes;	AST(POOL_STRING)
} LobbyConfig;

// Global settings for the lobby
extern LobbyConfig g_LobbyConfig;

// Dictionary holding the game progression nodes
extern DictionaryHandle g_hRecommendedPlayerContentDictionary;

// Compares 2 game content node refs and checks whether they point to the same node
bool gameContentNode_RefsAreEqual(SA_PARAM_OP_VALID GameContentNodeRef *pRef1, SA_PARAM_OP_VALID GameContentNodeRef *pRef2);

// Indicates whether the given node reference points to the given node
bool gameContentNode_RefPointsToNode(SA_PARAM_NN_VALID GameContentNodeRef *pRef, SA_PARAM_NN_VALID GameContentNode *pNode);

// Returns a static string which will be shared by all calls to this function. This function never returns NULL.
const char* gameContentNode_GetUniqueNameByParams(GameContentNodeType eType, SA_PARAM_OP_STR const char *pchName, ContainerID iUGCProjectID, ContainerID iUGCProjectSeriesID, ContainerID iUGCProjectSeriesNodeID);

// Returns a static string which will be shared by all calls to this function. This function never returns NULL.
const char * gameContentNode_GetUniqueName(SA_PARAM_NN_VALID GameContentNodeRef *pNodeRef);

// Returns the reference to either a progression node or a UGC project given a destination string. The reference returns must be freed if not NULL.
SA_RET_OP_VALID GameContentNodeRef * gameContentNode_GetRefFromName(SA_PARAM_OP_STR const char *pchName);

// Indicates whether both nodes point to the same content
bool gameContentNode_NodesAreEqual(GameContentNode *pNodeLeft, GameContentNode *pNodeRight);

// Loads the recommended player content
void gameContentNode_LoadRecommendedContent(void);

// Gets the RecommendedPlayerContent from the dictionary
RecommendedPlayerContent * gameContentNode_RecommendedPlayerContentFromName(const char *pchName);

// Fills a game content node with information from a progression node
void gameContentNode_FillFromGameProgressionNode(SA_PARAM_NN_VALID GameContentNode *pContentNode, SA_PARAM_NN_VALID const GameProgressionNodeDef *pNode);

// Indicates whether the given game content node points to a current progression node for the entity
bool gameContentNode_IsCurrentCharProgress(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID GameContentNode *pNode);
