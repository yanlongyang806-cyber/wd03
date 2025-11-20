#ifndef ITEM_ASSIGNMENTS_UI_H
#define ITEM_ASSIGNMENTS_UI_H

#include "referencesystem.h"
#include "ItemAssignments.h"

#define AVAILABLE_ITEM_ASSIGNMENT_HEADER_MASK (16+8+1)

#define ITEM_ASSIGNMENTS_REFRESH_TIME_UI 3
#define ITEM_ASSIGNMENT_REWARD_REQUEST_TIMEOUT 5
#define ITEM_ASSIGNMENTS_REMOTE_REQUEST_TIMEOUT 5

#define ITEM_ASSIGNMENT_RECOMMEND_EXCLUDE_NEW_SLOT 1

#define ITEM_ASSIGNMENT_SLOTOPTION_EXCLUDE_REQUIRED 0x01
#define ITEM_ASSIGNMENT_SLOTOPTION_EXCLUDE_OPTIONAL 0x02

#define ITEM_ASSIGNMENT_REQUIRED_ITEMS_EXLUDE_NUMERIC 0x01

typedef struct ItemAssignmentDef ItemAssignmentDef;

AUTO_ENUM;
typedef enum ItemAssignmentRewardsFlags
{
	kItemAssignmentRewardsFlags_ExcludeNumericXP = 0x01,
	kItemAssignmentRewardsFlags_ExcludeNonNumericXP = 0x02,
	kItemAssignmentRewardsFlags_UnionOfOtherOutcome		= 0x04,
	kItemAssignmentRewardsFlags_DifferenceOfOtherOutcome = 0x08,
	kItemAssignmentRewardsFlags_ExcludeNumerics			= 0x10,
	kItemAssignmentRewardsFlags_ExcludeNonNumerics		= 0x20,

} ItemAssignmentRewardsFlags;
#define ItemAssignmentRewardsFlags_NUMERICS (kItemAssignmentRewardsFlags_ExcludeNumericXP|kItemAssignmentRewardsFlags_ExcludeNonNumericXP|kItemAssignmentRewardsFlags_ExcludeNumerics|kItemAssignmentRewardsFlags_ExcludeNonNumerics)

AUTO_ENUM;
typedef enum ItemAssignmentCountFlags
{
	kItemAssignmentCount_ExcludeWithOutcome = 1,
	kItemAssignmentCount_ExcludeWithoutOutcome = 2,
	kItemAssignmentCount_IncludeFilter = 4,
	kItemAssignmentCount_ExcludeFilter = 8,
	kItemAssignmentCount_FilterCategory = 16,
	kItemAssignmentCount_Items = 32,
	kItemAssignmentCount_Unread = (128+64),
	kItemAssignmentCount_RecentlyCompleted = 64,
	kItemAssignmentCount_ChainHeaders = 256,
	kItemAssignmentCount_ExcludeFree = 512,
} ItemAssignmentCountFlags;

AUTO_ENUM;
typedef enum GetActiveAssignmentFlags
{
	kGetActiveAssignmentFlags_AddHeaders = 1,
	kGetActiveAssignmentFlags_AddCategoryHeaders = (8+1),
	kGetActiveAssignmentFlags_AddAllCategoryHeaders = (16+8+1),
	kGetActiveAssignmentFlags_AddStatusHeaders = (16+1),
	kGetActiveAssignmentFlags_ExcludePersonal = 2,
	kGetActiveAssignmentFlags_ExcludeNonPersonal = 4,
	kGetActiveAssignmentFlags_ExcludeIncomplete = 32,
	kGetActiveAssignmentFlags_IncludeCompleted = 64,
	kGetActiveAssignmentFlags_ExcludeReady = 128,
	kGetActiveAssignmentFlags_DontFill = 256,
	kGetActiveAssignmentFlags_ExcludeLevelErrors = 1024,
	kGetActiveAssignmentFlags_ExcludeAnyErrors = 2048,
	kGetActiveAssignmentFlags_ExcludeUnread = 4096,
	kGetActiveAssignmentFlags_IncludeUnread = 8192,
	kGetActiveAssignmentFlags_ReverseTimeRemaining = 512,
	kGetActiveAssignmentFlags_ExcludeFactionErrors = 16384,
	kGetActiveAssignmentFlags_IncludeContact = 32768,
	kGetActiveAssignmentFlags_SortRequiredNumericAscending = (1 << 16),
	kGetActiveAssignmentFlags_SortRequiredNumericDecending = (1 << 17),
	kGetActiveAssignmentFlags_SortByWeight = (1 << 18),
	kGetActiveAssignmentFlags_AddWeightHeaders = (1 << 19),
	kGetActiveAssignmentFlags_HideUnmetRequirements = (1 << 20),
	kGetActiveAssignmentFlags_HideUnmetRequiredNumeric = (1 << 21),
} GetActiveAssignmentFlags;

AUTO_ENUM;
typedef enum ItemAssignmentRiskFlags
{
	kItemAssignmentRiskFlags_Destroy = 1,
	kItemAssignmentRiskFlags_NewAssignment = 2,
	kItemAssignmentRiskFlags_Max = 4,
	kItemAssignmentRiskFlags_IgnoreDescribedDestroy = 8,
	kItemAssignmentRiskFlags_IgnoreDescribedNewAssignment = 16,
	kItemAssignmentRiskFlags_IgnoreDescribed = (16+8),
} ItemAssignmentRiskFlags;

AUTO_ENUM;
typedef enum ItemAssignmentChainFlags
{
	kItemAssignmentChainFlag_ExcludeRepeatable	= 1 << 0,
	kItemAssignmentChainFlag_OnlyStarted		= 1 << 1,
	kItemAssignmentChainFlag_ExcludeAssignments	= 1 << 2,
	kItemAssignmentChainFlag_ExcludeHeaders		= 1 << 3,
	kItemAssignmentChainFlag_ChainLengthExcludeRepeatable = 1 << 4,
} ItemAssignmentChainFlags;

AUTO_ENUM;
typedef enum ItemAssignmentCategoryUIFlags
{
	// automatically sorts in descending order
	lItemAssignmentCategoryUIFlags_RankHeaders = 1 << 0,

} ItemAssignmentCategoryUIFlags;


AUTO_STRUCT;
typedef struct ItemAssignmentSlotUI
{
	REF_TO(ItemAssignmentDef) hDef;
	// The AssignmentDef of this slot

	REF_TO(ItemAssignmentDef) hNewAssignmentDef;
	// The new assignment.
	// Only valid for completed assignments.

	U32 uNewAssignmentID;
	// The new assignment ID.
	// Only valid for completed assignments.

	REF_TO(ItemDef) hItemDef;
	// The currently slotted ItemDef

	U64 uItemID;
	// The currently slotted item ID

	InvBagIDs eBagID;
	// The bag ID of the currently slotted item

	S32 iBagSlot;
	// The bag slot of the currently slotted item

	S32 iAssignmentSlot;
	// The assignment slot index
	
	char* estrRequiredCategories; AST(NAME(RequiredCategories) ESTRING)
	// The required categories display string

	char* estrRequiredCategoriesRaw; AST(NAME(RawRequiredCategories) ESTRING)
	// The required categories internal name string

	char* estrRestrictCategories; AST(NAME(RestrictCategories) ESTRING)
	// The restricted categories display string

	char* estrRestrictCategoriesRaw; AST(NAME(RawRestrictCategories) ESTRING)
	// The restricted categories internal name string

	char* estrAffectedCategories; AST(NAME(AffectedCategories) ESTRING)
	// The affected categories display string

	char* estrAffectedCategoriesRaw; AST(NAME(RawAffectedCategories) ESTRING)
	// The affected categories internal name string

	const char* pchIcon; AST(NAME(Icon) POOL_STRING)
	// The icon to display for the slot

	const char* pchDestroyDescription; AST(UNOWNED)
	// The string to display for an item that was destroyed

	const char* pchNewAssignmentDescription; AST(UNOWNED)
	// The string to display for an item that went on a new assignment

	ItemAssignmentFailsRequiresReason eFailsReason;
	// The reasons why this slot fails

	F32 fOutcomeModifyValue;
	// when an item is slotted, what value is assigned to how it will effect the outcome

	F32 fDurationModifyValue;
	// when an item is slotted, what value is assigned to how it will effect the duration

	U32 bDestroyed : 1;
	// Whether or not the item was destroyed. 
	// Only valid for completed assignments.

	U32 bNewAssignment : 1;
	// Whether or not the item received a new assignment.
	// Only valid for completed assignments.

	U32 bUnslottable : 1;
	// Whether or not the item in this slot is unslottable.

	U32 bOptionalSlot : 1;
	// if this slot is optional or not

	U32 bUpdated : 1;
	// Flag to determine if the slot was updated
} ItemAssignmentSlotUI;

AUTO_STRUCT;
typedef struct ItemAssignmentUI
{
	REF_TO(ItemAssignmentDef) hDef; AST(REFDICT(ItemAssignmentDef))
	// The ItemAssignmentDef

	const char* pchDisplayName; AST(UNOWNED)
	// Display name of this assignment

	const char* pchDescription; AST(UNOWNED)
	// Description of this assignment

	const char* pchChainDisplayName; AST(UNOWNED)
	// The display name of the chain

	const char* pchChainDescription; AST(UNOWNED)
	// The description of the chain

	S32 iChainDepth;
	// How far into the chain this assignment is

	S32 iChainLength;
	// The length of the longest chain starting from this assignment

	S32 iChainLengthNoRepeats;
	// The length of the longest chain starting from this assignment excluding repeatable assignments

	S32 iChainLengthCompleted;
	// The length of the longest chain of completed assignments from this assignment excluding repeatable assignments

	const char* pchMapDisplayName; AST(NAME(MapDisplayName) ADDNAMES(MapName) UNOWNED)
	// The map display name that this assignment was started on

	const char* pchOutcomeName; AST(POOL_STRING)
	// The chosen outcome name

	const char* pchOutcomeDisplayName; AST(UNOWNED)
	// The display name of the chosen outcome

	const char* pchOutcomeDescription; AST(UNOWNED)
	// The description of the chose outcome

	U32 uOutcomeDestroyed;
	// The number of items that were destroyed in this assignment

	const char* pchOutcomeDestroyedDescription; AST(UNOWNED)
	// The description for destroyed items

	U32 uOutcomeNewAssignment;
	// The number of items that were put on a new assignment

	const char* pchOutcomeNewAssignmentDescription; AST(UNOWNED)
	// The description for items put on a new assignment

	REF_TO(ItemAssignmentDef) hOutcomeNewAssignmentDef; AST(NAME(OutcomeNewAssignmentDef))
	// The outcome new assignment def

	const char* pchIcon; AST(POOL_STRING)
	// The icon name for this assignment

	const char* pchImage; AST(POOL_STRING)
	// The image name to use for this assignment

	const char* pchWeight; AST(POOL_STRING)
	// The weight of this assignment appearing

	F32 fWeightValue; 
	// the value of the given pchWeight

	const char* pchFeaturedActivity; AST(POOL_STRING)
	// The featured activity associated with the assignment

	char* estrFailsRequires; AST(NAME(FailsRequirementsReason) ESTRING)
	// The reason why the player cannot use this assignment

	ItemAssignmentCategory eCategory;
	// The category of this assignment

	const char* pchCategoryNumericXP1; AST(POOL_STRING)
	// The XP numeric for the category

	const char* pchCategoryNumericXP2; AST(POOL_STRING)
	// The other XP numeric for the category

	const char* pchCategoryNumericRank1; AST(POOL_STRING)
	// The rank numeric for the category

	const char* pchCategoryNumericRank2; AST(POOL_STRING)
	// The other rank numeric for the category

	const char* pchCategoryIcon; AST(POOL_STRING)

	U32 uAssignmentID;
	// Unique assignment ID

	U32 uTimeStarted;			AST(FORMATSTRING(JSON_SECS_TO_RFC822=1))
	// The time the assignment started

	U32 uDuration;
	// The duration of the assignment

	U32 uTimeRemaining;
	// The amount of time remaining before this assignment completes

	U32 uNextRequirementsUpdateTime;
	// The next time to update requirement information about this assignment

	U32 uCompletedTime;					
	// The last time this assignment was completed

	S32 iRequiredMinimumLevel;
	// Required minimum level

	S32 iRequiredNumericValue;
	// Required minimum level

	REF_TO(MissionDef) hRequiredMission; AST(NAME(RequiredMission))
	// Required mission

	REF_TO(ItemAssignmentDef) hRequiredAssignment; AST(NAME(RequiredItemAssignment))
	// Requires that the player has completed an item assignment

	U32 bfInvalidSlots[1]; AST(NAME(SlotRequirement))
	// The bit field that indicates whether or not the player has an item that can be slotted in that slot.
	// This assumes that there won't be more than 64 slots on an assignment.

	ItemAssignmentFailsRequiresReason eFailsRequiresReasons; AST(NAME(FailsRequiresReasons))
	// Flags that describe why the player fails requirements for this assignment

	S32 iCompletedAssignmentIndex;
	// The index of the completed assignment

	U8 uSortCategory;
	// The sort order for this assignment

	S32 iSortOrder;
		// the sort order given from the itemAssignmentDef 

	S32 iCompletionExperience;
	// the amount of 

	F32 fQualityRewardBonus;
	// The reward bonus for this assignment

	S32 iLevelUnlocked;	
	// if locked, what level this slot will be unlocked at

	S32 iSlotIndex;
	// for strict assignment slots

	U32 bIsHeader : 1;
	// This is a category header

	U32 bIsPersonalAssignment : 1;
	// This is a personal assignment

	U32 bIsAbortable : 1;
	// Whether or not this assignment is abortable

	U32 bHasRewards : 1;
	// This assignment has completed and has rewards that need to be collected

	U32 bFailsRequirements : 1;
	// Whether or not the player has access to this assignment

	U32 bHasCompleteDetails : 1;
	// Whether or not the complete details is available for this assignment

	U32 bNew : 1;
	// Whether or not this recently completed assignment is new (hasn't been seen by the user)

	U32 bRepeatable : 1;
	// Whether or not this assignment is repeatable

	U32 bIsFeatured : 1;
	// Whether or not this assignment is currently featured

	U32 bIsLockedSlot : 1;
	
	U32 bHasRequiredSlots : 1;
	// if true the assignment has slots that are required 
	U32 bHasOptionalSlots : 1;
	// if true the assignment has slots that are required 
	U32 bHasItemCosts : 1;
	// if true the assignment has slots that are required 

} ItemAssignmentUI;

AUTO_STRUCT;
typedef struct ItemAssignmentList
{
	ItemAssignmentUI **ppAssignments;
}ItemAssignmentList;

AUTO_STRUCT;
typedef struct ItemAssignmentOutcomeUI
{
	const char* pchName; AST(POOL_STRING)
	// The internal outcome name

	const char* pchDisplayName; AST(UNOWNED)
	// The display name of this outcome

	const char* pchDescription; AST(UNOWNED)
	// The description of this outcome

	F32 fOutcomePercentChance;
	// The percent chance that this outcome will be chosen

	F32 fBasePercentChance;
	// The base percent chance that this outcome will be chosen

	F32 fQualityRewardBonus;
	// The quality reward bonus for this outcome
} ItemAssignmentOutcomeUI;


AUTO_STRUCT;
typedef struct ItemRecommendationValue
{
	InventoryBag *pBag; AST(NAME(bag) UNOWNED)
		InventorySlot *pSlot; AST(UNOWNED)
		F32 fValue;
} ItemRecommendationValue;

AUTO_STRUCT;
typedef struct ItemAssignmentCategoryUI
{
	ItemAssignmentCategory eCategory;

	const char* pchName;				AST(UNOWNED)

	const char* pchDisplayName;			AST(UNOWNED)
	// Display name of this assignment

	char* estrHeaderDisplayName; AST(NAME(HeaderDisplayName) ESTRING)

	const char* pchIcon;				AST(UNOWNED)

	S32 iCurrentRank;
	S32 iNextRank;

	S32 iCurrentXP;
	S32 iNextXP;

	F32 fPercentageThroughCurrentLevel;

	S32 iIndex;
	S32 iSortOrder;
	U32 bIsHeader : 1;

} ItemAssignmentCategoryUI;

AUTO_STRUCT;
typedef struct ItemAssignmentCategoryUIList
{
	ItemAssignmentCategoryUI **ppCategories;
}ItemAssignmentCategoryUIList;

AUTO_STRUCT;
typedef struct ItemAssignmentEquippedUI
{
	S32					bagID;			AST(NAME(BagID))
	U64					uid;
	ItemAssignmentUI	assignmentUI;	AST(NAME(AssignmentUI))
	S32					cacheFlag;
} ItemAssignmentEquippedUI;

AUTO_STRUCT;
typedef struct ItemAssignmentCachedStruct
{
	ItemAssignmentSlotUI** eaSlots;
	const char **eaWaitingAssignments;
	ItemAssignmentEquippedUI **ppItemAssignmentUIs; 
	ItemAssignmentCategoryUI **ppItemCategories;
	ItemAssignmentRewardRequestData* pRewardRequestData;
	REF_TO(ItemAssignmentDef) hCurrentDef;
}ItemAssignmentCachedStruct;

ItemAssignmentCachedStruct *pIACache;

AUTO_STRUCT;
typedef struct ItemAssignmentItemCategoryCount
{
	ItemCategory eCategory;
	S32 iCount;
} ItemAssignmentItemCategoryCount;

void GetAvailableItemAssignmentsByCategoryInternal(	Entity *pEnt, 
													ItemAssignmentUI *** peaData,
													const char *pchBagIDs, 
													const char *pchItemCategoryFilters, 
													const char *pchWeightFilters, 
													U32 uFlags,
													S32 iMinRequiredNumericValue,
													S32 iMaxRequiredNumericValue,
													S32 iUnmetRequiredNumericRange,
													const char *pchStringSearch);

bool SetGenSlotsForItemAssignment(	Entity *pEnt, ItemAssignmentSlotUI ***peaItemAssignmentSlotUIs, S32 *piCachedCount, 
									bool bIgnoreUpdateThrottle, ItemAssignmentSlotUI*** peaDataOut, const char* pchAssignmentDef, U32 uOptionFlags);

void ItemAssignment_GetUIOutcomes(	SA_PARAM_OP_VALID Entity* pEnt, 
									SA_PARAM_OP_VALID ItemAssignmentDef* pDef, 
									ItemAssignmentSlottedItem** eaSlottedItems,
									ItemAssignmentOutcomeUI*** peaOutcomeUI);

ItemAssignmentSlottedItem** ItemAssignment_GetOrCreateTempItemAssignmentSlottedItemList();
ItemAssignmentCompletedDetails* ItemAssignment_FindPossibleCompletedDetails(SA_PARAM_NN_VALID ItemAssignmentPersistedData* pPlayerData, 
																			const ItemAssignment *pAssignment);

void ItemAssignment_FillInFromCompleteDetails(	SA_PARAM_NN_VALID Entity *pEnt, 
												SA_PARAM_NN_VALID ItemAssignmentPersistedData* pPlayerData, 
												SA_PARAM_NN_VALID ItemAssignmentSlotUI*** peaData, 
												SA_PARAM_NN_VALID ItemAssignmentCompletedDetails *pCompleteDetails);

ItemAssignmentUI* ItemAssignment_GetAssignment(ItemAssignmentUI*** peaData, S32 iIndex, U32 uAssignmentID, S32 iCompletedIndex, const char *pchHeader, bool bInsertIfNotFound);

void FillActiveItemAssignment(	ItemAssignmentUI* pDataOut, 
	const ItemAssignment* pAssignment, 
	Entity* pEnt, 
	ItemAssignmentPersistedData* pPlayerData, 
	U32 uTimeCurrent);

ItemAssignmentUI* ItemAssignment_CreateActiveHeader(ItemAssignmentUI*** peaData, S32* piCount, const char* pchDisplayNameKey);

int SortActiveItemAssignments(U32 *puFlags, const ItemAssignmentUI** ppA, const ItemAssignmentUI** ppB);

ItemAssignment* ItemAssignment_FindPossibleActiveItemAssignment(SA_PARAM_NN_VALID ItemAssignmentPersistedData* pPlayerData, ItemAssignmentCompletedDetails *pCompleteDetails);

void FillCompletedItemAssignment(Entity *pEnt, ItemAssignmentUI* pData, ItemAssignmentCompletedDetails* pAssignment, ItemAssignment* pActiveAssignment, U32 uTimeCurrent);

const char* ItemAssignment_GetIconName(ItemAssignmentDef* pDef, ItemAssignmentCategorySettings* pCategory);
const char* ItemAssignment_GetImageName(ItemAssignmentDef* pDef, ItemAssignmentCategorySettings* pCategory);

int SortAvailableItemAssignments(const ItemAssignmentUI** ppA, const ItemAssignmentUI** ppB);
void ItemAssignmentsClearSlottedItem(SA_PARAM_NN_VALID ItemAssignmentSlotUI* pSlotUI);

void ItemAssignmentsSlotItemCheckSwap(Entity* pEnt, ItemAssignmentSlotUI* pNewSlot, S32 eBagID, S32 iBagSlot, Item* pItem);
bool ItemAssignmentsCanSlotItem(Entity* pEnt, S32 iAssignmentSlot, S32 eBagID, S32 iBagSlot, GameAccountDataExtract* pExtract);

S32 ItemAssignmentsGetRequiredItemsInternal(Entity *pEnt, InventorySlot ***peaInvSlots, const char *pchItemAssignment, U32 uFlags);

ItemAssignmentFailsRequiresReason ItemAssignment_UpdateRequirementsInfo(ItemAssignmentUI* pData, 
																		Entity* pEnt, 
																		ItemAssignmentDef* pDef, 
																		bool bTestSlottedAssets);

void SetItemAssignmentData(	Entity* pEnt, 
							ItemAssignmentUI* pData, 
							ItemAssignmentDef* pDef, 
							bool bIsPersonalAssignment, 
							bool bIsFeatured,
							bool bRequirementsTestSlottedAssets);

int ItemAssignment_OrderChains(const void *ppvA, const void *ppvB, const void *pContext);
bool ItemAssignments_HandleNewAssignment(const char *pchName);
void ItemAssignment_AddChainToList(Entity *pEnt, ItemAssignmentUI ***peaData, S32 *piCount, ItemAssignmentDef *pDef, U32 uFlags, bool bIsPersonalAssignment);
ItemAssignmentEquippedUI* FindItemAssignmentEquippedUIForItemID(U64 id);
void ItemAssignment_FillItemAssignmentSlots(Entity *pEnt, ItemAssignmentUI ***peaItemAssignmentUIData);

void ItemAssignment_FillAssignmentCategories(Entity *pEnt, ItemAssignmentCategoryUI ***pppCategories, U32 uFlags);

// returns true if there was anything found in the list
bool ItemAssignment_GetFilteredRewards(	Entity *pEnt,
										InventorySlot*** peaData,
										ItemAssignmentDef *pAssignmentDef, 
										const char* pchOutcome, 
										U32 uFlags,
										bool bCheckOnly);

ItemAssignmentOutcomeRewardRequest* ItemAssignmentsUI_FindRewardRequestOutcome(const char* pchOutcomePooled);

bool ItemAssignment_GetFailsRequirementsReason(	Entity* pEnt, 
												ItemAssignmentDef* pDef, 
												char** pestrFailsRequires,
												ItemAssignmentFailsRequiresReason* peFailsRequires,
												U32 *pbfInvalidSlots, 
												S32 iMaxSlots,
												bool bTestSlottedAssets);

bool buildSlotsForItemAssignment(Entity *pEnt, ItemAssignmentDef *pDef, bool bIgnoreUpdateThrottle, ItemAssignmentSlotUI ***peaItemAssignmentSlotUICache, S32 *piCacheCount);

S32 ItemAssignmentPersonalUpdateTime(Entity *pEnt, S32 iPersonalBucket);
#endif // ITEM_ASSIGNMENTS_UI_H