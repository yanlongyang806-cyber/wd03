#pragma once

#include "referencesystem.h"
#include "GroupProjectCommon.h"

AUTO_STRUCT;
typedef struct GroupProjectNumericUI
{
	// The display name of the numeric
	const char *pchDisplayName; AST(UNOWNED)

	// The description of the numeric
	const char *pchDescription; AST(UNOWNED)

	// The tooltip of the numeric
	const char *pchTooltip; AST(UNOWNED)

	// The image associated with the numeric
	const char *pchImage; AST(POOL_STRING)

	// The internal name of this numeric
	REF_TO(GroupProjectNumericDef) hGroupProjectNumeric; AST(NAME(GroupProjectNumeric))

	// The current value of this numeric
	S32 iCurrentValue;

	// The maximum value of this numeric
	S32 iMaximumValue;

	// The adjustment value of this numeric
	S32 iAdjustValue;

	// The owner type of this numeric
	GroupProjectType eProjectType;

	// The owner group project of this numeric
	REF_TO(GroupProjectDef) hGroupName; AST(NAME(GroupName))
} GroupProjectNumericUI;

AUTO_STRUCT;
typedef struct GroupProjectUnlockUI
{
	// The display name of the unlock
	const char *pchDisplayName; AST(UNOWNED)

	// The description of the unlock
	const char *pchDescription; AST(UNOWNED)

	// The tooltip of the unlock
	const char *pchTooltip; AST(UNOWNED)

	// The image associated with the unlock
	const char *pchImage; AST(POOL_STRING)

	// The internal name of this unlock
	REF_TO(GroupProjectUnlockDef) hGroupProjectUnlock; AST(NAME(GroupProjectUnlock))

	// The group project numeric required before unlocking
	GroupProjectNumericUI *pUnlockNumeric;

	// The group project numeric amount required
	S32 iUnlockReferenceValue;

	// The flag that indicates that the group project is a manual unlock
	U32 bUnlockManual : 1;

	// The flag that indicates that the group project unlocks when a group project numeric has been reached
	U32 bUnlockNumericGreaterEqual : 1;

	// The flag that indicates that the group project unlock has been unlocked
	U32 bUnlocked : 1; AST(NAME(unlocked))
} GroupProjectUnlockUI;

AUTO_STRUCT;
typedef struct DonationTaskRewardUI
{
	// This reward is an group project unlock
	GroupProjectUnlockUI *pUnlock;

	// This reward is a numeric grant
	GroupProjectNumericUI *pNumeric;

	// This item is rewarded from a reward table
	Item *pItem; AST(UNOWNED)

	// The numeric reward will set the value
	U32 bNumericSet : 1;
} DonationTaskRewardUI;

AUTO_STRUCT;
typedef struct DonationTaskUI
{
	// The display name of the group project
	const char *pchDisplayName; AST(UNOWNED)

	// The description of the group project
	const char *pchDescription; AST(UNOWNED)

	// The tooltip of the group project
	const char *pchTooltip; AST(UNOWNED)

	// The image associated with the assignment
	const char *pchImage; AST(POOL_STRING)

	// The ref to the task
	REF_TO(DonationTaskDef) hDonationTask; AST(NAME(DonationTask))

	// The number of buckets in this task
	S32 iBucketCount;

	// The current number of buckets in this task (summed from all buckets)
	S32 iCurrentBucketQuantity;

	// The total number of buckets for this task (summed from all buckets)
	S32 iTotalBucketQuantity;

	// The time remaining for the task to complete
	S32 iRemainingCompletionTime;

	// The total time for the task to complete
	S32 iTotalCompletionTime;

	// The category of this donation task
	DonationTaskCategoryType eCategory;

	// The flag that indicates the player can contribute to this task
	U32 bCanFillBucket : 1;

	// The flag that indicates this task has all contributions filled
	U32 bBucketsFilled : 1;

	// The flag that indicates this task is available to add to a project slot
	U32 bCanSlot : 1;

	// The flag that indicates this task is repeatable
	U32 bRepeatable : 1;

	// The flag that indicates this task has already been completed at least once
	U32 bAlreadyCompleted : 1;

	// The flag that indicates this task is waiting for manual claiming
	U32 bRewardPending : 1;
} DonationTaskUI;

AUTO_STRUCT;
typedef struct DonationTaskBucketUI
{
	// The display name of the group project
	const char *pchDisplayName; AST(NAME(DisplayName) UNOWNED)

	// The description of the group project
	const char *pchDescription; AST(NAME(Description) UNOWNED)

	// The tooltip of the group project
	const char *pchTooltip; AST(NAME(Tooltip) UNOWNED)

	// The image for this bucket
	const char *pchImage; AST(NAME(Image) UNOWNED)

	// The item def to fill
	REF_TO(ItemDef) hItemDef; AST(NAME(ItemDef))

	// The ref to the task that owns this bucket
	REF_TO(DonationTaskDef) hDonationTask; AST(NAME(DonationTask))

	// The name of this bucket
	const char *pchBucketName; AST(NAME(bucketName) POOL_STRING)

	// The current filled quantity
	S32 iCurrentQuantity;

	// The required filled quantity
	S32 iRequiredQuantity;

	// The quantity increment
	S32 iIncrementQuantity;

	// The list of items the player can add to the bucket
	STRING_EARRAY eachAvailableItemKeys; AST(NAME(AvailableItemKeys))

	// The list of items queued for the bucket
	STRING_EARRAY eachQueuedItemKeys; AST(NAME(QueuedItemKeys))

	// The number of queued items
	S32 iQueuedItemCount;

	// The magic key to determine exactly which bucket this is
	char *pchBucketKey;

	// The maximum possible amount the player can donate
	S32 iMaximumDonation;

	// The remaining donation amount
	S32 iOpenDonation;

	// The timestamp of the last time AvailableItems was updated
	U32 uAvailableItemsUpdate; AST(NAME(AvailableItemsUpdate))

	// The flag that indicates the player can contribute to this project
	U32 bCanFillBucket : 1;

	// This bucket is filled
	U32 bFilled : 1; AST(NAME(filled))

	// This bucket may queue up items
	U32 bCanQueueItems : 1; AST(NAME(CanQueueItems))
} DonationTaskBucketUI;

AUTO_STRUCT;
typedef struct DonationTaskSlotUI
{
	// The slot number
	S32 iSlot;

	// The task that's currently active
	DonationTaskUI *pActiveTask;

	// The task that's queued for when the active task completes
	DonationTaskUI *pNextTask;

	// The category of this slot
	GroupProjectTaskSlotType eSlotType;

	// The number of available tasks that could be added to this slot (or replace the next task)
	S32 iAvailableTasks;
} DonationTaskSlotUI;

AUTO_STRUCT;
typedef struct GroupProjectDonorUI
{
	// The donor name
	char *pchDisplayName;

	// The donor score
	U32 uContribution; AST(NAME(contribution))
} GroupProjectDonorUI;

AUTO_STRUCT;
typedef struct GroupProjectUI
{
	// The name of the group project
	const char *pchDisplayName; AST(UNOWNED)

	// The description of the group project
	const char *pchDescription; AST(UNOWNED)

	// The "primary" image associated with the group project (e.g. starbase screenshot)
	const char *pchImage; AST(POOL_STRING)

	// The group name
	const char *pchGroupName; AST(POOL_STRING)
} GroupProjectUI;

AUTO_STRUCT;
typedef struct GroupProjectLevelTreeNodeUI
{
	// A key that may be used to identify this tree node
	char *pchKey;

	// The numeric used for level up
	GroupProjectNumericUI *pUnlockNumeric;

	// The unlock that is auto granted on level up
	GroupProjectUnlockUI *pNumericUnlock;

	// The unlock that that must be manually granted to fully unlock
	GroupProjectUnlockUI *pManualUnlock;

	// The current progress towards the numeric unlock
	S32 iNumericProgress;

	// The required progress to unlock the numeric unlock
	S32 iRequiredProgress;

	// The status of the tree node
	GroupProjectLevelTreeNodeStatus eStatusNumber;

	// The status of the tree node as a string
	const char *pchStatusName; AST(UNOWNED)

	// The style of the tree node
	const char *pchStyle; AST(POOL_STRING)

	// The image of this node
	const char *pchImage; AST(POOL_STRING)

	// The icon of this node
	const char *pchIcon; AST(POOL_STRING)

	// The level title of this node
	const char *pchLevelMessage; AST(POOL_STRING)

	// The XP title of this node
	const char *pchXPMessage; AST(POOL_STRING)

	// The XP unlock of this node
	const char *pchXPUnlockMessage; AST(POOL_STRING)

	// If the auto unlock has been started
	U32 bNumericStarted : 1;

	// If the auto unlock has been granted
	U32 bNumericUnlocked : 1;

	// If the manual unlock has been granted
	U32 bManualUnlocked : 1;
} GroupProjectLevelTreeNodeUI;

AUTO_STRUCT;
typedef struct GroupProjectLevelTreeUI
{
	// The current number of completed tree nodes that have been auto granted
	S32 iGrantedNumericNodes;

	// The current number of completed tree nodes that have been manually granted
	S32 iGrantedManualNodes;

	// The total number of tree nodes
	S32 iTotalNodes;

	// The image of this level tree
	const char *pchImage; AST(POOL_STRING)

	// The level title of this level tree
	const char *pchLevelMessage; AST(POOL_STRING)

	// The XP title of this level tree
	const char *pchXPMessage; AST(POOL_STRING)

	// The XP unlock of this level tree
	const char *pchXPUnlockMessage; AST(POOL_STRING)

	// The group project information for this level tree
	GroupProjectUI *pGroupProject;

	// The numeric used for level up
	GroupProjectNumericUI *pUnlockNumeric;

	// The tree nodes available for level up
	GroupProjectLevelTreeNodeUI **eaTreeNodes;
} GroupProjectLevelTreeUI;

AUTO_STRUCT;
typedef struct DonationTaskBucketDonationResultUI
{
	// A dummy item instance of the item donated
	Item *pDonatedItem;

	// The name of the item
	REF_TO(ItemDef) hItemDef; AST(POOL_STRING NAME(ItemDef))

	// The bag of the item
	S32 eBagID;

	// The slot of the item
	S32 iSlot;

	// The requested amount of the item to donate
	int iRequestedDonation;

	// The actual mount of the item donated
	int iActualDonation;

	// If the donation was successful
	U32 bSuccessfulDonation : 1;

	// If the donation was only partially successful
	U32 bPartialDonation : 1; AST(NAME(partialDonation))

	// If the donation was not successful
	U32 bFailedDonation : 1;
} DonationTaskBucketDonationResultUI;
