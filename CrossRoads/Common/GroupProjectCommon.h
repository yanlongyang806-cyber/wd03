#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypeEnum.h"
#include "ReferenceSystem.h"
#include "stdtypes.h"
#include "Message.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"

typedef U32 ContainerID;
typedef struct ItemDef ItemDef;
typedef struct DisplayMessage DisplayMessage;
typedef struct Expression Expression;
typedef enum ItemCategory ItemCategory;
typedef struct Entity Entity;
typedef struct Item Item;
typedef struct RewardTable RewardTable;
typedef struct AllegianceRef AllegianceRef;

#define GROUP_PROJECT_EXT "groupproject"
#define GROUP_PROJECT_BASE_DIR "defs/GroupProjects"

#define GROUP_PROJECT_UNLOCK_EXT "groupprojectunlock"
#define GROUP_PROJECT_UNLOCK_BASE_DIR "defs/GroupProjects"

#define GROUP_PROJECT_NUMERIC_EXT "groupprojectnumeric"
#define GROUP_PROJECT_NUMERIC_BASE_DIR "defs/GroupProjects"

#define DONATION_TASK_EXT "donationtask"
#define DONATION_TASK_BASE_DIR "defs/GroupProjects"

#define GROUP_PROJECT_TASK_SLOT_TYPE_FILE "defs/config/GroupProjectTaskSlotTypes.def"
#define DONATION_TASK_CATEGORY_TYPE_FILE "defs/config/DonationTaskCategoryTypes.def"

extern DictionaryHandle g_GroupProjectDict;
extern DictionaryHandle g_GroupProjectUnlockDict;
extern DictionaryHandle g_GroupProjectNumericDict;
extern DictionaryHandle g_DonationTaskDict;

#define PROJECT_MESSAGE_MAX_LEN 200
#define PROJECT_PLAYER_NAME_MAX_LEN 64

//////////////////////////////////////////////////////////////////////////
//
// Design/Editor data
//
//////////////////////////////////////////////////////////////////////////

AUTO_ENUM;
typedef enum GroupProjectType
{
    GroupProjectType_None,

    // For guild specific projects
    GroupProjectType_Guild,

    // For faction specific projects - Not currently supported
    GroupProjectType_Faction,

    // For shard wide projects - Not currently supported
    GroupProjectType_Shard,

    // For personal projects
    GroupProjectType_Player,
} GroupProjectType;

AUTO_ENUM;
typedef enum GroupProjectDonationSpecType
{
    DonationSpecType_None,
    DonationSpecType_Item,
    DonationSpecType_Expression,
} GroupProjectDonationSpecType;

AUTO_ENUM;
typedef enum DonationTaskRewardType
{
    DonationTaskRewardType_None,
    DonationTaskRewardType_Unlock,
    DonationTaskRewardType_NumericAdd,
    DonationTaskRewardType_NumericSet,
} DonationTaskRewardType;

AUTO_ENUM;
typedef enum GroupProjectUnlockType
{
    UnlockType_None,
    UnlockType_Manual,
    UnlockType_NumericValueEqualOrGreater,
} GroupProjectUnlockType;

extern DefineContext *s_DefineGroupProjectTaskSlotTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(s_DefineGroupProjectTaskSlotTypes);
typedef enum GroupProjectTaskSlotType
{
    // Slot types are data defined.
    TaskSlotType_None = 0,
    TaskSlotType_MAX, EIGNORE
} GroupProjectTaskSlotType;

extern DefineContext *s_DefineDonationTaskCategoryTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(s_DefineDonationTaskCategoryTypes);
typedef enum DonationTaskCategoryType
{
    DonationTaskCategory_None = 0, ENAMES(None)
    DonationTaskCategory_MAX, EIGNORE
} DonationTaskCategoryType;

AUTO_ENUM;
typedef enum GroupProjectLevelTreeCount {
	kGroupProjectLevelTreeCount_ManualNodes = 1,
	kGroupProjectLevelTreeCount_NumericNodes = 2,
} GroupProjectLevelTreeCount;

AUTO_ENUM;
typedef enum GroupProjectLevelTreeNodeStatus
{
	kGroupProjectLevelTreeNodeState_Locked,
	kGroupProjectLevelTreeNodeState_Progress,
	kGroupProjectLevelTreeNodeState_Ready,
	kGroupProjectLevelTreeNodeState_Complete,
} GroupProjectLevelTreeNodeStatus;

AUTO_STRUCT;
typedef struct GroupProjectNumericDef
{
    const char *name;                   AST(STRUCTPARAM KEY POOL_STRING)

    // The filename containing this def.
    char *filename;					    AST(CURRENTFILE)

    // Scope for this def.
    const char *scope;				    AST(POOL_STRING)

    // The name of the numeric that is displayed to the user.
    DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage))

    // The detailed description of the numeric that is displayed to the user.
    DisplayMessage tooltipMsg;  		AST(STRUCT(parse_DisplayMessage)) 

    // The name of the icon to display for this numeric.
    const char* iconName;               AST(NAME(Icon) POOL_STRING)

    // The maximum value of the numeric
    S32 maxValue;						AST(NAME(MaxValue))

} GroupProjectNumericDef;

AUTO_STRUCT;
typedef struct GroupProjectNumericDefRef
{
    REF_TO(GroupProjectNumericDef) numericDef;				AST(REFDICT(GroupProjectNumericDef) STRUCTPARAM KEY)
} GroupProjectNumericDefRef;

AUTO_STRUCT;
typedef struct GroupProjectUnlockDef
{
    const char *name;                   AST(STRUCTPARAM KEY POOL_STRING)

    // The filename containing this def.
    char *filename;					    AST(CURRENTFILE)

    // Scope for this def.
    const char *scope;				    AST(POOL_STRING)

    // The name of the unlock that is displayed to the user.
    DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage))

    // The detailed description of the unlock that is displayed to the user.
    DisplayMessage descriptionMsg;		AST(STRUCT(parse_DisplayMessage)) 

    // What triggers this unlock?  Current types are numeric value and manual.
    GroupProjectUnlockType type;		AST(NAME(Type))

    // For UnlockType_NumericValueEqualOrGreater only.  Which numeric to check.
    REF_TO(GroupProjectNumericDef) numeric;	AST(NAME(Numeric))

    // For UnlockType_NumericValueEqualOrGreater only.  Which value to compare against.
    S32 triggerValue;

    // The detailed description of the unlock that is displayed to the user.
    DisplayMessage tooltipMsg;  		AST(STRUCT(parse_DisplayMessage)) 

    // The name of the icon to display for this unlock.
    const char* iconName;               AST(NAME(Icon) POOL_STRING)
} GroupProjectUnlockDef;

AUTO_STRUCT;
typedef struct DonationTaskReward
{
    // What type of reward to give (unlock or numeric).
    DonationTaskRewardType rewardType;                  AST(NAME(Type))

    // An unlock to reward.
    // Only valid for DonationTaskRewardType_Unlock.
    REF_TO(GroupProjectUnlockDef) unlockDef;            AST(NAME(Unlock))

    // The group project numeric to reward.
    // Only valid for DonationTaskRewardType_Numeric.
    REF_TO(GroupProjectNumericDef) numericDef;			AST(NAME(Numeric))

    // The constant that defined how much to add to the reward numeric.
    // Only valid for DonationTaskRewardType_Numeric.
    STRING_POOLED rewardConstant;
} DonationTaskReward;

AUTO_STRUCT;
typedef struct GroupProjectDonationRequirement
{
    // The logical name for this donation bucket.
    const char *name;                   AST(STRUCTPARAM POOL_STRING)

    // The name of the donation bucket that is displayed to the user.
    DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage))

    // The detailed description of the donation bucket that is displayed to the user.
    DisplayMessage descriptionMsg;		AST(STRUCT(parse_DisplayMessage)) 

    // How are the required items specified?
    GroupProjectDonationSpecType specType; AST(NAME(Type))

    // The required number of items that must be donated.
    U32 count;

    // For simple items spec, this is a reference to the item that must be donated.
    REF_TO(ItemDef) requiredItem;		AST(NAME(RequiredItem, RequireItem))

    // Expression that defines which items can be donated.  This should be evaluated in the Item expression context.
    Expression *allowedItemExpr; AST(REDUNDANT_STRUCT(allowedItemExpr, parse_Expression_StructParam), LATEBIND)

    // Require items to have one of the following item categories to be donated.
    ItemCategory* requiredItemCategories; AST(NAME(RequiredItemCategory) SUBTABLE(ItemCategoryEnum))
    
    // Require items to not have any of the following item categories to be donated.
    ItemCategory* restrictItemCategories; AST(NAME(RestrictItemCategory) SUBTABLE(ItemCategoryEnum))

    // The name of the constant that should be used for awarding contribution.  
    // The contribution earned should be the number of items donated to the bucket times the value of the constant.
    STRING_POOLED contributionConstant;

    // The detailed description of the requirement that is displayed to the user.
    DisplayMessage tooltipMsg;  		AST(STRUCT(parse_DisplayMessage)) 

    // The name of the icon to display for this requirement.
    const char* iconName;               AST(NAME(Icon) POOL_STRING)

    // Items must be donated in increments of this value.  The contribution for a donation is divided by this value.
    // A value of 0 is treated as a value of 1.
    // Note that this only works with DonationSpecType_Item.
    U32 donationIncrement;
} GroupProjectDonationRequirement;

AUTO_STRUCT;
typedef struct DonationTaskDef
{
    // The logical name for this donation task.
    const char *name;                   AST(STRUCTPARAM KEY POOL_STRING)

    // The filename containing this def.
    char *filename;					    AST(CURRENTFILE)

    // Scope for this def.
    const char *scope;				    AST(POOL_STRING)

    // The name of the donation task that is displayed to the user.
    DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage))

    // The detailed description of the donation task that is displayed to the user.
    DisplayMessage descriptionMsg;		AST(STRUCT(parse_DisplayMessage)) 

    // Is the project repeatable.
    bool repeatable;                    AST(NAME(Repeatable) BOOLFLAG)

    // Can the project be cancelled.
    bool cancelable;                    AST(NAME(Cancelable) BOOLFLAG)

    // Expression to determine if the task is available.
    Expression *taskAvailableExpr; AST(REDUNDANT_STRUCT(taskAvailableExpr, parse_Expression_StructParam), LATEBIND)

    // The set of buckets that must be filled for this task.
    EARRAY_OF(GroupProjectDonationRequirement) buckets; AST(NAME(Bucket))

    // The set of rewards that are awarded when this task starts.
    EARRAY_OF(DonationTaskReward) taskStartRewards; AST(NAME(TaskStartReward))

    // The set of rewards that are awarded when this task completes.
    EARRAY_OF(DonationTaskReward) taskRewards; AST(NAME(TaskReward))

    // The number of seconds it will take for this task to complete after all the donations have been provided.
    U32 secondsToComplete;

    // The slot that this task can be used in.
    GroupProjectTaskSlotType slotType;  AST(NAME(SlotType) SUBTABLE(GroupProjectTaskSlotTypeEnum))

    // The detailed description of the task that is displayed to the user.
    DisplayMessage tooltipMsg;  		AST(STRUCT(parse_DisplayMessage)) 

    // The name of the icon to display for this task.
    const char* iconName;               AST(NAME(Icon) POOL_STRING)

    // If true, this task is available for a project that doesn't have a container yet, and might not be able to evaluate some expressions.
    bool taskAvailableForNewProject;    AST(BOOLFLAG)

    // The category of this donation task, for UI sorting purposes
    DonationTaskCategoryType category;

    // A reward table that is granted when the task is completed.  Only valid for GroupProjectType_Player.
    REF_TO(RewardTable) completionRewardTable;
} DonationTaskDef;

// Note that there are separate versions of this struct, one for persisted containers and one for design defs.  See below.
AUTO_STRUCT;
typedef struct GroupProjectUnlockDefRef
{
    REF_TO(GroupProjectUnlockDef) unlockDef;    AST(STRUCTPARAM KEY)
} GroupProjectUnlockDefRef;

// Note that there are separate versions of this struct, one for persisted containers and one for design defs.  See above.
AUTO_STRUCT AST_CONTAINER;
typedef struct GroupProjectUnlockDefRefContainer
{
    CONST_REF_TO(GroupProjectUnlockDef) unlockDef;    AST(PERSIST SUBSCRIBE STRUCTPARAM KEY)
} GroupProjectUnlockDefRefContainer;

AUTO_STRUCT;
typedef struct DonationTaskDefRef
{
    REF_TO(DonationTaskDef) taskDef;    AST(STRUCTPARAM KEY)
} DonationTaskDefRef;

AUTO_STRUCT AST_CONTAINER;
typedef struct DonationTaskDefRefContainer
{
    CONST_REF_TO(DonationTaskDef) taskDef;    AST(PERSIST SUBSCRIBE STRUCTPARAM KEY)
} DonationTaskDefRefContainer;

AUTO_STRUCT;
typedef struct GroupProjectConstant
{
    STRING_POOLED key;                  AST(STRUCTPARAM POOL_STRING)
    S32 value;                          AST(STRUCTPARAM)
} GroupProjectConstant;

AUTO_STRUCT;
typedef struct GroupProjectRemoteContact
{
    // A unique (to the group project def) identifier for this contact
    STRING_POOLED key;                  AST(STRUCTPARAM POOL_STRING)

    // The logical name of the contact
    STRING_POOLED contactDef;           AST(STRUCTPARAM POOL_STRING)

    // The list of unlocks that should grant access to the contact
    EARRAY_OF(GroupProjectUnlockDefRef) requiredUnlocks; AST(NAME(RequiredUnlock))
} GroupProjectRemoteContact;

AUTO_STRUCT;
typedef struct GroupProjectDef
{
    // The logical name of this project.
    const char *name;				    AST(STRUCTPARAM KEY POOL_STRING)

    // The filename containing this def.
    char *filename;					    AST(CURRENTFILE)

    // Scope for this def.
    const char *scope;				    AST(POOL_STRING)

    GroupProjectType type;

    // The name of the project that is displayed to the user.
    DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage))

    // The detailed description of the project that is displayed to the user.
    DisplayMessage descriptionMsg;		AST(STRUCT(parse_DisplayMessage)) 

    // The set of unlocks for this project.
    EARRAY_OF(GroupProjectUnlockDefRef) unlockDefs; AST(NAME(Unlock))

    // The set of donation tasks for this project.
    EARRAY_OF(DonationTaskDefRef) donationTaskDefs; AST(NAME(Task))

    // The set of group project numerics for this project.
    EARRAY_OF(GroupProjectNumericDefRef) validNumerics; AST(NAME(ValidNumeric))

    // EArray of GroupProjectTaskSlotTypes 
    GroupProjectTaskSlotType *slotTypes;			AST(NAME(SlotType) SUBTABLE(GroupProjectTaskSlotTypeEnum))

    // The set of constants for this project.  These constants are used to define rewards.
    EARRAY_OF(GroupProjectConstant) constants;      AST(NAME(constant))

    // The set of maps where donations can be made.
    STRING_EARRAY donationMaps;         AST(POOL_STRING NAME(DonationMap))

    // A reference to the player numeric that contribution is awarded to.
    REF_TO(ItemDef) contributionNumeric;

    // A reference to the player numeric that contribution is awarded to.  This numeric is never spent from, so it tracks total contribution ever earned.
    REF_TO(ItemDef) lifetimeContributionNumeric;

    // The list of contacts that this group project may grant access to
    EARRAY_OF(GroupProjectRemoteContact) remoteContacts; AST(NAME(RemoteContact))

    // The list of power trees this group project controls
    STRING_EARRAY powerTrees;           AST(NAME(PowerTree))

    // The name of the icon to display for this task.
    const char* iconName;               AST(NAME(Icon) POOL_STRING)
} GroupProjectDef;

AUTO_STRUCT;
typedef struct GroupProjectDefRef
{
    REF_TO(GroupProjectDef) projectDef;    AST(STRUCTPARAM KEY)
} GroupProjectDefRef;

AUTO_STRUCT;
typedef struct GroupProjectDefs
{
    EARRAY_OF(GroupProjectDefRef) projectDefs;
} GroupProjectDefs;

AUTO_STRUCT;
typedef struct GroupProjectLevelTreeNodeDef
{
	// The name of the numeric unlock for this node
	const char *pchNumericUnlock; AST(POOL_STRING)

		// The name of the manual unlock for this node
		const char *pchManualUnlock; AST(POOL_STRING)

		// The style of this node
		const char *pchStyle; AST(POOL_STRING)

		// The image of this node
		const char *pchImage; AST(POOL_STRING)

		// The icon of this node
		const char *pchIcon; AST(POOL_STRING)

		// The level title of this node
		const char *pchLevelMessage; AST(POOL_STRING)

		// The XP title of this node
		const char *pchXPMessage; AST(POOL_STRING)

		// The XP unlock title of this node
		const char *pchXPUnlockMessage; AST(POOL_STRING)

		// A hint for the UI
		const char *pchHint; AST(POOL_STRING)

		// The PowerTree group of powers for this node
		STRING_EARRAY eaPowerTreeGroups; AST(NAME(PowerTreeGroup) POOL_STRING)
} GroupProjectLevelTreeNodeDef;

AUTO_STRUCT;
typedef struct GroupProjectLevelTreeDef
{
	// The level nodes
	GroupProjectLevelTreeNodeDef **eaLevelNodes; AST(NAME(LevelNode))
} GroupProjectLevelTreeDef;

extern GroupProjectLevelTreeDef g_GroupProjectLevelTreeDef;

//////////////////////////////////////////////////////////////////////////
//
// Persisted data
//
//////////////////////////////////////////////////////////////////////////
AUTO_ENUM;
typedef enum DonationTaskState
{
    DonationTaskState_None,                 // Slot is not being used.
    DonationTaskState_AcceptingDonations,   // Task is accepting donations.
    DonationTaskState_Finalized,            // Task donations have been completed.  Task is waiting for it's "time to complete" to expire.
    DonationTaskState_Completed,            // Task has completed.  Rewards have been granted.
    DonationTaskState_RewardPending,        // Task is ready to complete, but is waiting for the player to claim their reward first.
    DonationTaskState_RewardClaimed,        // Task is ready to complete and the player had claimed their reward.
    DonationTaskState_Canceled,             // Task has been canceled. Task is waiting to be completed without rewards.
} DonationTaskState;

AUTO_STRUCT AST_CONTAINER;
typedef struct GroupProjectNumericData
{
    REF_TO(GroupProjectNumericDef) numericDef;          AST(PERSIST SUBSCRIBE KEY)
    const S32 numericVal;								AST(PERSIST SUBSCRIBE)
} GroupProjectNumericData;

AUTO_STRUCT AST_CONTAINER;
typedef struct DonationTaskBucketData
{
    // The name of the requirement bucket.
    CONST_STRING_POOLED bucketName;                     AST(PERSIST SUBSCRIBE KEY)

    // The quantity that has been donated so far.
    const U32 donationCount;                            AST(PERSIST SUBSCRIBE)
} DonationTaskBucketData;

AUTO_STRUCT AST_CONTAINER;
typedef struct DonationTaskSlot
{
    // The slot number.
    const U32 taskSlotNum;                              AST(PERSIST SUBSCRIBE KEY)

    // What type of slot this is.
    const GroupProjectTaskSlotType taskSlotType;        AST(PERSIST SUBSCRIBE)

    // Reference to the task definition.
    CONST_REF_TO(DonationTaskDef) taskDef;              AST(PERSIST SUBSCRIBE)

    // Reference to the task that will replace this one when it completes.
    CONST_REF_TO(DonationTaskDef) nextTaskDef;          AST(PERSIST SUBSCRIBE)

    // The state of this task.
    const DonationTaskState state;                      AST(PERSIST SUBSCRIBE)

    // The donation buckets for this task.
    CONST_EARRAY_OF(DonationTaskBucketData) buckets;    AST(PERSIST SUBSCRIBE)

    // When the task was finalized.
    const U32 finalizedTime;                            AST(PERSIST SUBSCRIBE)

    // When the task will complete.  Can be in the future or the past.
    const U32 completionTime;                           AST(PERSIST SUBSCRIBE)

    // When this task was started.
    const U32 startTime;                                AST(PERSIST SUBSCRIBE)

    // Keep track of the number of donation buckets that have been completed.
    const U32 completedBuckets;                         AST(PERSIST SUBSCRIBE)

    // Non-persisted field used by the GroupProjectServer to keep track of which tasks have pending completion transactions.
    bool completionRequested;
} DonationTaskSlot;

AUTO_STRUCT AST_CONTAINER;
typedef struct GroupProjectDonationStats
{
    // The container ID of the donating player entity.
    const U32 donatorID;                                AST(PERSIST SUBSCRIBE KEY)

    // The display name of the donating player.  Should be name@handle.
    CONST_STRING_MODIFIABLE displayName;                AST(PERSIST SUBSCRIBE)

    // The amount of contribution that this player has earned by donating to group project tasks.
    const U32 contribution;                             AST(PERSIST SUBSCRIBE)
} GroupProjectDonationStats;

AUTO_STRUCT AST_CONTAINER;
typedef struct GroupProjectState
{
    // Which project this is the state for.
    REF_TO(GroupProjectDef) projectDef;						AST(PERSIST SUBSCRIBE KEY)

    // Which unlocks have been unlocked for this project.
    CONST_EARRAY_OF(GroupProjectUnlockDefRefContainer) unlocks;  AST(PERSIST SUBSCRIBE)

    // The project numerics.
    CONST_EARRAY_OF(GroupProjectNumericData) numericData;   AST(PERSIST SUBSCRIBE)

    // The current tasks.
    CONST_EARRAY_OF(DonationTaskSlot) taskSlots;            AST(PERSIST SUBSCRIBE)

    // Donation stats.
    CONST_EARRAY_OF(GroupProjectDonationStats) donationStats; AST(PERSIST SUBSCRIBE)

    // A message that can be set by the project leader.
    CONST_STRING_MODIFIABLE projectMessage;					AST(PERSIST SUBSCRIBE)

    // A name for the project that can be set by the project leader.
    CONST_STRING_MODIFIABLE projectPlayerName;				AST(PERSIST SUBSCRIBE)

    // The set of non-repeatable asks that have been completed.
    CONST_EARRAY_OF(DonationTaskDefRefContainer) completedTasks;     AST(PERSIST SUBSCRIBE)
} GroupProjectState;

AUTO_STRUCT AST_CONTAINER;
typedef struct GroupProjectContainer
{
    // The container type and ID of this GroupProjectContainer
    const GlobalType containerType;                     AST(PERSIST SUBSCRIBE SUBTABLE(GlobalTypeEnum))
    const U32 containerID;                              AST(PERSIST SUBSCRIBE KEY)

    // The type and ID of the owner of this GroupProjectContainer
    const GlobalType ownerType;                         AST(PERSIST SUBSCRIBE SUBTABLE(GlobalTypeEnum))
    const U32 ownerID;                                  AST(PERSIST SUBSCRIBE)

    // Data for the projects.
    CONST_EARRAY_OF(GroupProjectState) projectList;     AST(PERSIST SUBSCRIBE)

    // Non-persisted field used by the GroupProjectServer to track when to trigger completions.
    U32 nextCompletionTime;
} GroupProjectContainer;

AUTO_STRUCT;
typedef struct ContributionItemData
{
    // The name of the item to donate
    STRING_POOLED itemName;                     AST(POOL_STRING NAME(ItemName))

    // The bag id the item is in
    S32 bagID;                                  AST(NAME(BagID))

    // The slot the item is in
    S32 slotIdx;

    // The number of the item to donate
    S32 count;
} ContributionItemData;

AUTO_STRUCT;
typedef struct ContributionItemList
{
    // The list of items to donate in the order they should be donated
    EARRAY_OF(ContributionItemData) items;
} ContributionItemList;

AUTO_STRUCT;
typedef struct ContributionNotifyData
{
    // The ID of the player who made the contribution.
    ContainerID playerID;

    // The type and ID of the project being donated to.
    GlobalType projectcontainerType;
    ContainerID projectContainerID;

    // The item that was donated.
    STRING_POOLED donatedItemName;              AST(POOL_STRING)

    // Requested donation quantity.
    S32 requestedDonationCount;

    // The quantity donated.
    S32 donationCount;

    // The list of requested item donations
    EARRAY_OF(ContributionItemData) requestedDonations;

    // The list of successful item donations
    EARRAY_OF(ContributionItemData) actualDonations;

    // The contribution numeric that was awarded.
    STRING_POOLED contributionNumericName;      AST(POOL_STRING)

    // How much contribution was earned.
    S32 contributionEarned;

    // The actual contribution given
    S32 contributionGiven;

    // The initial value of the contribution numeric
    S32 initialContribution;

    // The project that was donated to.
    STRING_POOLED projectName;                  AST(POOL_STRING)

    // The task that was donated to.
    STRING_POOLED taskName;                     AST(POOL_STRING)

    // Which bucket was donated to.
    STRING_POOLED bucketName;           AST(POOL_STRING)

    // Will be true if this was the final donation to fill a bucket.
    bool bucketFilled;

    // Will be true if this was the final donation to finalize a task
    bool taskFinalized;

    // Will be true if the donation failed because the player does not have permission to donate.
    bool noPermission;

    // Will be true if only a partial donation was given
    bool partialDonation;
} ContributionNotifyData;

bool GroupProject_Validate(GroupProjectDef* projectDef);
bool DonationTask_Validate(DonationTaskDef* taskDef);
bool GroupProjectUnlock_Validate(GroupProjectUnlockDef* unlockDef);
bool GroupProjectNumeric_Validate(GroupProjectNumericDef* numericDef);

void GroupProject_GiveNumeric(GlobalType containerType, ContainerID containerID, const char *projectName, const char *numericName, S32 value);
int GroupProject_NumTaskSlots(GroupProjectDef *projectDef);
int GroupProject_FindConstant(const GroupProjectDef *projectDef, const char *key);
GroupProjectContainer *GroupProject_ResolveContainer(Entity *playerEnt, GroupProjectType projectType);

int DonationTask_FindRequirement(DonationTaskDef *taskDef, const char *requirementName);
bool DonationTask_ItemMatchesExpressionRequirement(Entity *playerEnt, GroupProjectDonationRequirement *taskBucket, Item *item);

bool GuildProject_DonationTaskAllowed(Entity *playerEnt, const char *projectName, DonationTaskDef *taskDef, int taskSlotNum);
bool PlayerProject_DonationTaskAllowed(Entity *playerEnt, const char *projectName, DonationTaskDef *taskDef, int taskSlotNum);

void GroupProject_SubscribeToGuildProject(Entity *playerEnt);
void GroupProject_SubscribeToPlayerProjectContainer(Entity *playerEnt);

void GroupProject_ValidateContainer(Entity *playerEnt, GroupProjectType projectType);
GlobalType GroupProject_ContainerTypeForProjectType(GroupProjectType projectType);
ContainerID GroupProject_ContainerIDForProjectType(Entity *playerEnt, GroupProjectType projectType);

// This function is called by several expression functions that have to get the 
bool GroupProject_GetNumericFromPlayerExprHelper(Entity *playerEnt, GroupProjectType projectType, const char *projectName, const char *numericName, int *pValueOut, char **errString);
bool GroupProject_GetUnlockFromPlayerExprHelper(Entity *playerEnt, GroupProjectType projectType, const char *projectName, const char *unlockName, int *pValueOut, char **errString);

S32 GroupProject_GetLevelTreeCount(const GroupProjectState *pState, const char *pchHint, GroupProjectLevelTreeCount uFlags);
void GroupProject_UpdateBucketQuantities(const GroupProjectDef *pGroupProject, const DonationTaskDef *pDef, const DonationTaskSlot *pSlot, S32 *piCurrentBucketQuantityOut, S32 *piTotalBucketQuantityOut);