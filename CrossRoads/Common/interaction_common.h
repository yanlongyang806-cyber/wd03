/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "contact_common.h"
#include "referencesystem.h"

typedef struct ContactDef ContactDef;
typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct ExprFuncTable ExprFuncTable;
typedef struct ParseTable ParseTable;
typedef struct StaticDefineInt StaticDefineInt;
typedef struct WorldAnimationInteractionProperties WorldAnimationInteractionProperties;
typedef struct WorldContactInteractionProperties WorldContactInteractionProperties;
typedef struct WorldCraftingInteractionProperties WorldCraftingInteractionProperties;
typedef struct WorldDoorInteractionProperties WorldDoorInteractionProperties;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldRewardInteractionProperties WorldRewardInteractionProperties;
typedef struct WorldScope WorldScope;
typedef struct WorldTimeInteractionProperties WorldTimeInteractionProperties;


AUTO_ENUM;
typedef enum InteractValidity
{
	kInteractValidity_Nonexistant = 0,
	kInteractValidity_OutOfRange,
	kInteractValidity_LineOfSight,
	kInteractValidity_FailedRequirement,
	kInteractValidity_InvalidUnknown,
	kInteractValidity_Valid,
	kInteractValidity_CurrentlyInteracting,
	kInteractValidity_NameOnly,
} InteractValidity;
extern StaticDefineInt InteractValidityEnum[];

AUTO_ENUM;
typedef enum InteractionDefType
{
	InteractionDefType_Any,
	InteractionDefType_Entity,
	InteractionDefType_Node,
	InteractionDefType_Volume
} InteractionDefType;
extern StaticDefineInt InteractionDefTypeEnum[];

AUTO_STRUCT;
typedef struct InteractionDef
{
	// Contact name that uniquely identifies the contact. Required.
	const char *pcName;							AST(STRUCTPARAM KEY POOL_STRING)

	// Filename that this contact came from.
	const char *pcFilename;						AST(CURRENTFILE)

	// Scope for the contact
	const char *pcScope;						AST(POOL_STRING SERVER_ONLY)

	// Designer comments.  no runtime behavior.
	const char *pcComments;						AST(SERVER_ONLY)

	// The type of interaction def
	InteractionDefType eType;
	
	// The actual interaction properties
	WorldInteractionPropertyEntry *pEntry;
} InteractionDef;
extern ParseTable parse_InteractionDef[];
#define TYPE_parse_InteractionDef InteractionDef

AUTO_STRUCT;
typedef struct InteractionInfo
{
	const char* pchInteractableName; AST(POOL_STRING)
	const char* pchVolumeName; AST(POOL_STRING)
	EntityRef erTarget;
} InteractionInfo;
extern ParseTable parse_InteractionInfo[];
#define TYPE_parse_InteractionInfo InteractionInfo

AUTO_STRUCT;
typedef struct CachedDoorDestination
{
	WorldInteractionPropertyEntry *pEntry;
	U32 uiExpireTime;
	const char* pchDoorKey;
} CachedDoorDestination;
extern ParseTable parse_CachedDoorDestination[];
#define TYPE_parse_CachedDoorDestination CachedDoorDestination
#define TEAM_CACHED_DOOR_LIFESPAN 20;

extern DictionaryHandle g_InteractionDefDictionary;

extern const char *pcPooled_Chair;
extern const char *pcPooled_Clickable;
extern const char *pcPooled_Contact;
extern const char *pcPooled_CraftingStation;
extern const char *pcPooled_Destructible;
extern const char *pcPooled_Door;
extern const char *pcPooled_FromDefinition;
extern const char *pcPooled_Gate;
extern const char *pcPooled_NamedObject;
extern const char *pcPooled_Throwable;
extern const char *pcPooled_AmbientJob;
extern const char *pcPooled_CombatJob;
extern const char *pcPooled_TeamCorral;

// Pre-defined cooldown lengths in seconds
#define INTERACTION_COOLDOWN_SHORT		30
#define INTERACTION_COOLDOWN_MEDIUM	300
#define INTERACTION_COOLDOWN_LONG		3600

extern ExprContext *g_pInteractionContext;
extern ExprContext *g_pInteractionNonPlayerContext;


bool interaction_IsPlayerInteracting(SA_PARAM_NN_VALID Entity *pPlayerEnt);
bool interaction_IsPlayerInteractTimerFinished(SA_PARAM_NN_VALID Entity *pPlayerEnt);
bool interaction_IsPlayerInDialog(SA_PARAM_NN_VALID Entity *pPlayerEnt);
bool interaction_IsPlayerInDialogAndTeamSpokesman(SA_PARAM_NN_VALID Entity* playerEnt);
bool interaction_IsPlayerNearContact(SA_PARAM_NN_VALID Entity *pPlayerEnt, ContactFlags eFlags);
bool interaction_CanInteractWithContact(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID ContactInfo* pInfo);
void interaction_GetNearbyInteractableContacts(Entity* pPlayerEnt, ContactFlags eFlags, ContactInfo*** peaContacts);
void interaction_ClearPlayerInteractState(Entity *pPlayerEnt);

const char *interaction_GetEffectiveClass(WorldInteractionPropertyEntry *pEntry);
Expression *interaction_GetInteractCond(WorldInteractionPropertyEntry *pEntry);
Expression *interaction_GetSuccessCond(WorldInteractionPropertyEntry *pEntry);
Expression *interaction_GetAttemptableCond(WorldInteractionPropertyEntry *pEntry);
Expression *interaction_GetVisibleExpr(WorldInteractionPropertyEntry *pEntry);
const char *interaction_GetCategoryName(WorldInteractionPropertyEntry *pEntry);
int interaction_GetPriority(WorldInteractionPropertyEntry *pEntry);
bool interaction_GetAutoExecute(WorldInteractionPropertyEntry *pEntry);
WorldActionInteractionProperties *interaction_GetActionProperties(WorldInteractionPropertyEntry *pEntry);
WorldAnimationInteractionProperties *interaction_GetAnimationProperties(WorldInteractionPropertyEntry *pEntry);
WorldChairInteractionProperties *interaction_GetChairProperties(WorldInteractionPropertyEntry *pEntry);
WorldContactInteractionProperties *interaction_GetContactProperties(WorldInteractionPropertyEntry *pEntry);
ContactDef *interaction_GetContactDef(WorldInteractionPropertyEntry *pEntry);
WorldCraftingInteractionProperties *interaction_GetCraftingProperties(WorldInteractionPropertyEntry *pEntry);
WorldDestructibleInteractionProperties *interaction_GetDestructibleProperties(WorldInteractionPropertyEntry *pEntry);
WorldDoorInteractionProperties *interaction_GetDoorProperties(WorldInteractionPropertyEntry *pEntry);
WorldGateInteractionProperties *interaction_GetGateProperties(WorldInteractionPropertyEntry *pEntry);
WorldRewardInteractionProperties *interaction_GetRewardProperties(WorldInteractionPropertyEntry *pEntry);
WorldSoundInteractionProperties *interaction_GetSoundProperties(WorldInteractionPropertyEntry *pEntry);
WorldTextInteractionProperties *interaction_GetTextProperties(WorldInteractionPropertyEntry *pEntry);
WorldTimeInteractionProperties *interaction_GetTimeProperties(WorldInteractionPropertyEntry *pEntry);
WorldMotionInteractionProperties *interaction_GetMotionProperties(WorldInteractionPropertyEntry *pEntry);
bool interaction_EntryGetExclusive(WorldInteractionPropertyEntry *pEntry, bool bIsNode);
F32 interaction_GetCooldownTime(WorldInteractionPropertyEntry *pEntry);
bool interaction_HasTag(WorldInteractionProperties *pProps, const char *pchPooledTag);

void interaction_FixupMessages(WorldInteractionPropertyEntry *pEntry, const char *pcScope, const char *pcBaseMessageKey, const char *pcSubKey, int iIndex);
void interaction_CleanProperties(WorldInteractionPropertyEntry *pEntry);

ExprFuncTable* interactable_CreateExprFuncTable(void);
ExprFuncTable* interactable_CreateNonPlayerExprFuncTable(void);

// Determine if a property entry is set up correctly
void interaction_ValidatePropertyEntry(WorldInteractionPropertyEntry *pEntry, WorldScope *pScope, const char *pcFilename, const char *pcObjectType, const char *pcObjectName);

// Generate expressions and other initialization
void interaction_InitPropertyEntry(WorldInteractionPropertyEntry *pEntry, ExprContext *pContext, const char *pcFilename, const char *pcObjectType, const char *pcObjectName, bool bEntSpecificVisibility);

// Used to load the dictionary
bool interactiondef_Validate(InteractionDef *pDef);

// Used to load the dictionary
void interactiondef_LoadDefs(void);

void interaction_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);