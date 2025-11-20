/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "Message.h"
#include "ReferenceSystem.h"

typedef struct AllegianceDef AllegianceDef;
typedef struct ContactDef ContactDef;
typedef struct DefineContext DefineContext;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct GameProgressionConfig GameProgressionConfig;
typedef struct GameProgressionNodeDef GameProgressionNodeDef;
typedef struct GameProgressionNodeRef GameProgressionNodeRef;
typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct PossibleCharacterChoice PossibleCharacterChoice;
typedef struct GameSession GameSession;
typedef struct MissionDef MissionDef;
typedef struct StoryDef StoryDef;
typedef struct Team Team;
typedef struct NOCONST(Team) NOCONST(Team);
typedef U32 ContainerID;
typedef enum GameProgressionNodeType GameProgressionNodeType;

extern bool g_bValidateProgressionData;
extern GameProgressionNodeRef** g_eaStoryArcNodes;
extern GameProgressionConfig g_GameProgressionConfig;
// Dictionary holding the game progression nodes
extern DictionaryHandle g_hGameProgressionNodeDictionary;

#define GAME_PROGRESSION_EXT "storynode"
#define GAME_PROGRESSION_BASE_DIR "defs/stories"

typedef struct DefineContext DefineContext;

extern DefineContext* g_pGameProgressionNodeTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pGameProgressionNodeTypes);
typedef enum GameProgressionNodeType
{
	GameProgressionNodeType_None,
	GameProgressionNodeType_Campaign,
	GameProgressionNodeType_Module,
	GameProgressionNodeType_MissionGroup,

	// Beginning of data-defined values...
	GameProgressionNodeType_FirstDataDefined, EIGNORE
} GameProgressionNodeType;
extern StaticDefineInt GameProgressionNodeTypeEnum[];

AUTO_ENUM;
typedef enum GameProgressionNodeFunctionalType
{
	GameProgressionNodeFunctionalType_StoryGroup,
	GameProgressionNodeFunctionalType_StoryRoot,
	GameProgressionNodeFunctionalType_MissionGroup,
} GameProgressionNodeFunctionalType;

// Details about a mission in a story arc
AUTO_STRUCT;
typedef struct GameProgressionMission
{
	// String reference to the MissionDef
	const char* pchMissionName;				AST(NAME(MissionName) STRUCTPARAM POOL_STRING)

	// Mission-specific description
	DisplayMessage msgDescription;			AST(NAME(Description) STRUCT(parse_DisplayMessage)) 
	
	// Image for this mission
	const char* pchImage;					AST(NAME(Image) POOL_STRING)

	// Determines whether or not this mission will show up in the list of missions
	Expression *pExprVisible;				AST(NAME(ExprBlockVisible,ExprVisibleBlock), REDUNDANT_STRUCT(ExprVisible, parse_Expression_StructParam), LATEBIND)

	// Whether or not this mission is optional for progression
	U32 bOptional : 1;						AST(NAME(Optional))

	// Whether or not a player can choose to make this mission optional
	U32 bSkippable : 1;						AST(NAME(Skippable))
} GameProgressionMission;

AUTO_STRUCT;
typedef struct GameProgressionNodeMissionGroupInfo
{
	// Indicates whether this mission group is major
	bool bMajor;							AST(NAME(Major))

	// Estimated time to complete this mission group
	U32 uiTimeToComplete;					AST(NAME(TimeToComplete))

	// Required player level for this node
	S32 iRequiredPlayerLevel;				AST(NAME(RequiredPlayerLevel))

	// Map name
	const char* pchMapName;					AST(NAME(MapName) POOL_STRING)

	// Spawn point name
	const char* pchSpawnPoint;				AST(NAME(SpawnPoint) POOL_STRING)

	// Position on the overworld map, in percentage units
	Vec2 overworldMapPos;					AST(NAME(OverworldMapPos))

	// Overworld map icon
	const char*  astrOverworldMapIcon;		AST(NAME(OverworldMapIcon) POOL_STRING)

	// The list of missions linked with this group
	GameProgressionMission** eaMissions;	AST(NAME(Mission, MissionRequired, RequiredMission))

	// The list of mission maps the player is allowed to be on
	const char** ppchAllowedMissionMaps;	AST(NAME(AllowedMissionMap) POOL_STRING)

	// If set, the first mission in the mission chain will not be auto granted even if the config says otherwise
	U32 bDontAutoGrantMissionOnSetProgression : 1;	AST(NAME(DontAutoGrantMissionOnSetProgression))

	// If set, the UI will display rewards for this mission group. Right now this flag is only supported in the lobby.
	U32 bShowRewardsInUI : 1;				AST(NAME(ShowRewardsInUI) DEFAULT(1))

} GameProgressionNodeMissionGroupInfo;

AUTO_STRUCT;
typedef struct GameProgressionNodeDef
{
	// The name of the game progression node
	const char* pchName;										AST(STRUCTPARAM KEY POOL_STRING)

	// The scope
	const char* pchScope;										AST(POOL_STRING)

	// Used for reloading
	const char* pchFilename;									AST(CURRENTFILE)

	// UI type for this progression node
	GameProgressionNodeType eType;								AST(NAME(NodeType) SUBTABLE(GameProgressionNodeTypeEnum))

	// The type code respects.
	GameProgressionNodeFunctionalType eFunctionalType;			AST(NAME(NodeFunctionalType) SUBTABLE(GameProgressionNodeFunctionalTypeEnum))

	// Display name for the game progression node
	DisplayMessage msgDisplayName;								AST(STRUCT(parse_DisplayMessage) NAME(DisplayName))

	// Summary
	DisplayMessage msgSummary;									AST(STRUCT(parse_DisplayMessage) NAME(Summary))

	// Teaser text for locked nodes
	DisplayMessage msgTeaser;									AST(STRUCT(parse_DisplayMessage) NAME(Teaser))

	// The art file name
	const char* pchArtFileName;									AST(POOL_STRING NAME(ArtFileName))

	// Icon image to use
	const char* pchIcon;										AST(NAME(Icon) POOL_STRING)

	// Mission group specific information
	GameProgressionNodeMissionGroupInfo* pMissionGroupInfo;		AST(NAME(MissionGroupInfo))

	// The required allegiance for this story
	REF_TO(AllegianceDef) hRequiredAllegiance;					AST(NAME(RequiredAllegiance))

	// The required sub-allegiance for this story
	REF_TO(AllegianceDef) hRequiredSubAllegiance;				AST(NAME(RequiredSubAllegiance))

	// The required node
	REF_TO(GameProgressionNodeDef) hRequiredNode;				AST(NAME(RequiredNode) REFDICT(GameProgressionNodeDef))

	// A story might lock itself after a certain step is completed. This field is used so
	// the story will uncomplete itself until this node is reached. The specified node must be in the
	// same story line and should be before this node. This is mostly used to prevent people from playing
	// a certain story over and over. (optional)
	REF_TO(GameProgressionNodeDef) hNodeToWindBack;				AST(REFDICT(GameProgressionNodeDef) NAME(NodeToWindBack))

	// List of children
	EARRAY_OF(GameProgressionNodeRef) eaChildren;				AST(NAME(ChildNode) NO_INDEX)

	// The nodes that require this one
	EARRAY_OF(GameProgressionNodeRef) eaDependentNodes;			AST(NO_TEXT_SAVE NO_INDEX)

	// The next sibling node - derived at load time
	REF_TO(GameProgressionNodeDef) hNextSibling;				AST(REFDICT(GameProgressionNodeDef) NO_TEXT_SAVE)

	// The prev sibling node - derived at load time
	REF_TO(GameProgressionNodeDef) hPrevSibling;				AST(REFDICT(GameProgressionNodeDef) NO_TEXT_SAVE)

	// The parent - derived at load time
	REF_TO(GameProgressionNodeDef) hParent;						AST(REFDICT(GameProgressionNodeDef) NO_TEXT_SAVE)

	// An arbitrary value assigned to StoryRoot nodes to define a loose progression order
	S32 iSortOrder;												AST(NAME(SortOrder))

	// Whether or not this arc is debug only, and can only be accessed by players at AL9
	U32 bDebug : 1;												AST(NAME(Debug))

	// If set, every StoryGroup immediately under this node is independent of the previous StoryGroup
	U32 bBranchStory : 1;										AST(NAME(BranchStory))

	// If set, any of the mission group nodes under this story node does not need the previous node to be completed in order to be unlocked.
	// Only valid for story root nodes.
	U32 bMissionGroupsAreUnlockedByDefault : 1;					AST(NAME(MissionGroupsAreUnlockedByDefault))

	// If set, the current progression in the story will not automatically advance to the next node once the current node is completed.
	// Only valid for story root nodes.
	U32 bDontAdvanceStoryAutomatically : 1;						AST(NAME(DontAdvanceStoryAutomatically))

	// If set, the player's progress will be set whenever they accept a mission linked with a mission group node.
	// Only valid for story root nodes.
	U32 bSetProgressionOnMissionAccept : 1;						AST(NAME(SetProgressionOnMissionAccept))
} GameProgressionNodeDef;

AUTO_STRUCT;
typedef struct GameProgressionNodeRef
{
	REF_TO(GameProgressionNodeDef) hDef; AST(NAME(NodeDef) KEY)
} GameProgressionNodeRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct TeamProgressionData
{
	// The story arc
	CONST_REF_TO(GameProgressionNodeDef) hStoryArcNode;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(GameProgressionNodeDef) KEY)

	// The current progress
	CONST_REF_TO(GameProgressionNodeDef) hNode;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(GameProgressionNodeDef))

	// The time of last progression update in this story
	const U32 iLastUpdated;								AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	// When the override is synced between the team members, they
	// know if they need to delete any missions from the completed missions list for the team.
	CONST_STRING_EARRAY ppchMissionsToUncomplete;		AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE POOL_STRING)

	// The list of missions completed while a member of the current team. Always sorted.
	CONST_STRING_EARRAY ppchCompletedMissions;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE SELF_ONLY POOL_STRING)
} TeamProgressionData;

AUTO_STRUCT AST_CONTAINER;
typedef struct ReplayProgressionData
{
	// The story arc
	CONST_REF_TO(GameProgressionNodeDef) hStoryArcNode;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(GameProgressionNodeDef) KEY)

	// The current progress
	CONST_REF_TO(GameProgressionNodeDef) hNode;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(GameProgressionNodeDef))

	// Completed missions for this node
	CONST_STRING_EARRAY	ppchCompletedMissions;			AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE POOL_STRING)
} ReplayProgressionData;


AUTO_STRUCT;
typedef struct ProgressionTrackingData
{
	// The story arc
	REF_TO(GameProgressionNodeDef) hStoryArcNode;	AST(REFDICT(GameProgressionNodeDef) KEY)

	// The current progression node
	REF_TO(GameProgressionNodeDef) hNode;			AST(REFDICT(GameProgressionNodeDef))
} ProgressionTrackingData;

// This struct is used to store game progression information on the player
AUTO_STRUCT AST_CONTAINER 
AST_IGNORE(ppchUnlockedNodes)
AST_IGNORE(hCurrentProgressionNode)
AST_IGNORE(ppStoryTrackingData)
AST_IGNORE(pStoryTrackingDataOverride)
AST_IGNORE(ppchCompletedMissions);
typedef struct ProgressionInfo
{
	DirtyBit dirtyBit;										AST(NO_NETSEND)

	// The list of game progressions nodes completed by the player. This list is passed back to the client in the possible character choice.
	// Since this field needs to be accessed in the object DB, I chose it to be not an indexed array. Otherwise we would need to
	// introduce a game specific struct to the object DB. However the system makes sure this array is always sorted and you can do a
	// binary search on it.
	CONST_STRING_EARRAY	ppchCompletedNodes;					AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE POOL_STRING)

	// When a player completes a node that has a WindBackToNode specified, all missions for all nodes 
	// between the completed node and the node to wind back to are considered un-completed. This list tracks those
	// uncompleted missions. If a mission in this list is completed again, it is removed.
	CONST_STRING_EARRAY ppchWindBackMissions;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE POOL_STRING)

	// Missions that the player has chosen to skip. Missions in this list must have the bSkippable flag set
	CONST_STRING_EARRAY ppchSkippedMissions;				AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE POOL_STRING)

	// Replay tracking
	CONST_EARRAY_OF(ReplayProgressionData) eaReplayData;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)	

	// Team sets this override to the same progression node for everyone in the team.
	CONST_OPTIONAL_STRUCT(TeamProgressionData) pTeamData;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE)

	// Non-persisted progress tracking information for each story arc
	EARRAY_OF(ProgressionTrackingData) eaTrackingData;		AST(SELF_ONLY)

	// The last node the player has played
	CONST_REF_TO(GameProgressionNodeDef) hMostRecentlyPlayedNode;	AST(PERSIST SUBSCRIBE LOGIN_SUBSCRIBE REFDICT(GameProgressionNodeDef))

	U32 bTeamTransactionPending : 1;						NO_AST

	// Once this flag is set, the server sends a story intro notification if necessary
	U32 bEvaluateStoryNotificationSending : 1;				NO_AST

} ProgressionInfo;

AUTO_STRUCT;
typedef struct ProgressionUpdateParams
{
	REF_TO(GameProgressionNodeDef) hOverrideNode;	AST(REFDICT(GameProgressionNodeDef))
	U32 iOverrideTime;
	U32 bDestroyTeamData : 1;

	// The missions to uncomplete in the completed mission list for the team
	const char** ppchMissionsToUncomplete;			AST(POOL_STRING)
} ProgressionUpdateParams;

AUTO_STRUCT AST_IGNORE(ValidateMaps);
typedef struct GameProgressionConfig
{
	// The text used for the story intro notification
	DisplayMessage msgStoryIntroNotification; AST(NAME(StoryIntroNotification) STRUCT(parse_DisplayMessage))

	// If set, persist progression nodes and missions
	U32 bAllowReplay : 1; AST(DEFAULT(1))

	// If set, create tracking data for teams to track progression
	U32 bEnableTeamProgressionTracking : 1; AST(DEFAULT(1))

	// If set, missions must be completed in the order defined in a player's progression
	U32 bValidateProgression : 1;

	// If set, the players will be granted the first required mission when the progression is set manually
	U32 bAutoGrantMissionOnSetProgression : 1;

	// If set, the players receive a story notification when they enter a map that matches their progression
	U32 bSendStoryIntroNotificationOnMapEnter : 1;	

	// If set, the player must meet the requirements for a progression mission in order to skip it
	U32 bMustMeetRequirementsToSkipMissions : 1; AST(DEFAULT(1))

	// If set, the last played node is persisted in the progression info
	U32 bStoreMostRecentlyPlayedNode : 1;

} GameProgressionConfig;

// Accessor to get the progression expression context
ExprContext* progression_GetContext(Entity *pEnt);

// Validate a GameProgressionNodeDef
bool progression_Validate(GameProgressionNodeDef* pNodeDef);
// Load GameProgression systems
void progression_Load(void);

// Indicates whether the game progression node is unlocked by the character choice
bool progression_NodeUnlockedByCharacterChoice(PossibleCharacterChoice* pChoice, GameProgressionNodeDef* pNodeDef, bool bAllowNonMissionGroups);

// Indicates whether the game progression node is completed by the character choice
bool progression_NodeCompletedByCharacterChoice(PossibleCharacterChoice* pChoice, GameProgressionNodeDef* pNodeDef, bool bAllowNonMissionGroups);

// Indicates whether the node is unlocked by the game session given
bool progression_NodeUnlockedByGameSession(GameSession* pGameSession, SA_PARAM_NN_VALID GameProgressionNodeDef* pNodeDef);

// Indicates whether the node is completed by the game session given
bool progression_NodeCompletedByGameSession(GameSession* pGameSession, SA_PARAM_NN_VALID GameProgressionNodeDef* pNodeDef);

GameProgressionNodeDef* progression_NodeDefFromName(SA_PARAM_OP_STR const char* pchName);

// Check to see if the player meets the requirements for a progression mission
bool progression_trh_CheckMissionRequirementsEx(ATH_ARG NOCONST(Entity)* pPlayerEnt, GameProgressionNodeDef* pNodeDef, int iProgressionMissionIndex);
#define progression_CheckMissionRequirementsEx(pPlayerEnt, pNodeDef, iProgressionMissionIndex) progression_trh_CheckMissionRequirementsEx(CONTAINER_NOCONST(Entity, pPlayerEnt), pNodeDef, iProgressionMissionIndex)
bool progression_trh_CheckMissionRequirements(ATH_ARG NOCONST(Entity)* pPlayerEnt, MissionDef* pMissionDef);
#define progression_CheckMissionRequirements(pPlayerEnt, pMissionDef) progression_trh_CheckMissionRequirements(CONTAINER_NOCONST(Entity, pPlayerEnt), pMissionDef)

bool progression_trh_IsMissionOptional(ATH_ARG NOCONST(Entity)* pEnt, GameProgressionMission* pProgMission);
#define progression_IsMissionOptional(pEnt, pProgMission) progression_trh_IsMissionOptional(CONTAINER_NOCONST(Entity, pEnt), pProgMission)

bool progression_trh_IsMissionSkippable(ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef, int iProgMissionIdx);
#define progression_IsMissionSkippable(pEnt, pNodeDef, iProgMissionIdx) progression_trh_IsMissionSkippable(CONTAINER_NOCONST(Entity, pEnt), pNodeDef, iProgMissionIdx)

// Finds the game progression node which requires the given mission
GameProgressionNodeDef* progression_trh_GetNodeFromMissionDef(ATH_ARG NOCONST(Entity)* pEnt, MissionDef* pMissionDef, GameProgressionMission** ppProgMission);
#define progression_GetNodeFromMissionDef(pEnt, pMissionDef, ppProgMission) progression_trh_GetNodeFromMissionDef(CONTAINER_NOCONST(Entity, pEnt), pMissionDef, ppProgMission)
// Finds the game progression node which requires the given mission
GameProgressionNodeDef* progression_trh_GetNodeFromMissionName(ATH_ARG NOCONST(Entity)* pEnt, const char* pchMissionName, GameProgressionMission** ppProgMission);
#define progression_GetNodeFromMissionName(pEnt, pchMissionName, ppProgMission) progression_trh_GetNodeFromMissionName(CONTAINER_NOCONST(Entity, pEnt), pchMissionName, ppProgMission)

GameProgressionNodeDef* progression_GetStoryRootNode(SA_PARAM_OP_VALID GameProgressionNodeDef* pNodeDef);
GameProgressionNodeDef* progression_GetStoryBranchNode(SA_PARAM_OP_VALID GameProgressionNodeDef* pNodeDef);

GameProgressionNodeDef* progression_FindRightMostLeaf(GameProgressionNodeDef* pNodeDef);
GameProgressionNodeDef* progression_FindLeftMostLeaf(GameProgressionNodeDef* pNodeDef);

// Find a mission in a progression node by def
int progression_FindMissionForNode(GameProgressionNodeDef* pNodeDef, const char* pchMissionName);

// Returns the progression info for the player
ProgressionInfo* progression_GetInfoFromPlayer(const Entity* pEnt);

bool progression_NodeUnlockedByAnyTeamMember(Team* pTeam, GameProgressionNodeDef* pNodeDef);

bool progression_NodeUnlockedByAnyTeamMember(Team* pTeam, GameProgressionNodeDef* pNode);

bool progression_NodeCompletedByAnyTeamMember(Team* pTeam, GameProgressionNodeDef* pNodeDef);

bool progression_NodeCompletedByTeam(Team* pTeam, GameProgressionNodeDef* pNodeDef);

void progression_ProcessTeamProgression(SA_PARAM_NN_VALID Entity* pEnt, bool bEvaluateTeamStoryAdvancement);

TeamProgressionData* progression_GetCurrentTeamProgress(Team* pTeam);
GameProgressionNodeDef* progression_GetCurrentProgress(Entity* pEnt, GameProgressionNodeDef* pNodeDef);

// Update the current non-persisted progression information for the player
void progression_UpdateCurrentProgression(Entity* pEnt);

#ifdef GAMESERVER
// This is called when a player logs in to a map
void progression_PlayerLoggedIn(SA_PARAM_NN_VALID Entity *pEnt, bool bJustLoggedIn);

// Player tick function for the progression system
void progression_ProcessPlayer(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ProgressionInfo *pInfo);
#endif

// Returns the first required mission for a game progression node
MissionDef * progression_GetFirstRequiredMissionByNode(SA_PARAM_NN_VALID GameProgressionNodeDef *pNode);

// Returns the first required mission name for a game progression node
const char * progression_GetFirstRequiredMissionNameByNode(SA_PARAM_NN_VALID GameProgressionNodeDef *pNode);

// Indicates whether the node requires the player to complete the previous node in order to be unlocked
bool progression_NodeUnlockRequiresPreviousNodeToBeCompleted(SA_PARAM_OP_VALID GameProgressionNodeDef *pNodeDef);
