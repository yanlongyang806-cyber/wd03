#ifndef FC_INVENTORY_UI_H
#define FC_INVENTORY_UI_H

#include "stdtypes.h"
#include "GlobalTypeEnum.h"
#include "itemEnums.h"

typedef Item Item;
typedef struct Entity Entity;
typedef struct InventoryBag InventoryBag;
typedef struct ItemAssignmentUI ItemAssignmentUI;
typedef struct ItemCategoryInfo ItemCategoryInfo;
typedef U32 EntityRef;
typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct InventoryBag InventoryBag;
typedef struct InventorySlot InventorySlot;
typedef struct InventoryBagLite InventoryBagLite;
typedef struct InventorySlotLite InventorySlotLite;

#define UIItemCategoryFlag_CategorizedInventoryHeaders 0x2000

// NOTE: This structure exists to simplify handling UIInventoryKeys, it
// is not intended to be given to the UI code.
typedef struct UIInventoryKey
{
	EntityRef erOwner;
	InvBagIDs eBag;
	S32 iSlot;
	const char *pchName;
	GlobalType eType;
	ContainerID iContainerID;

	Entity *pOwner;
	GameAccountDataExtract *pExtract;
	Entity *pEntity;
	InventoryBag *pBag;
	InventoryBagLite *pBagLite;
	InventorySlot *pSlot;
	InventorySlotLite *pSlotLite;
} UIInventoryKey;

AUTO_STRUCT;
typedef struct UIItemCategory
{
	const char *pchCategoryName; AST(UNOWNED)
	const char *pchCategoryNameWithoutPrefix; AST(UNOWNED)
	const char *pchCategoryNamePrefix; AST(UNOWNED)
	const char *pchDisplayName; AST(UNOWNED)
	ItemCategory eCategory;
	ItemCategoryInfo *pCategoryData; AST(UNOWNED)
} UIItemCategory;

AUTO_STRUCT;
typedef struct UICategorizedInventorySlot
{
	UIItemCategory UICategory;			AST(EMBEDDED_FLAT)
	char *pchSlotKey;					AST(NAME(SlotKey))
	InventorySlot *pSlot;				AST(UNOWNED)
	ItemAssignmentUI *pAssignmentUI;	AST(UNOWNED)
	
	// if we have combined duplicates into a count. This will start at 0 if there is no other item dupe
	S32 iAdditionalCount;
	
	// if this bag slot is locked, currently due to the bag having a MaxSlotTable and having more slots to unlock
	U32 bLocked : 1;

} UICategorizedInventorySlot;

// Reverse mappings for inventory slot -> bag -> entity.
// Only tracked for the duration of the current frame.
extern void gclInventoryOncePerFrame(void);
extern Entity *gclInventoryGetBagEntity(InventoryBag *pBag);
extern void gclInventoryUpdateBag(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID InventoryBag *pBag);
extern Entity *gclInventoryGetBagLiteEntity(InventoryBagLite *pBagLite);
extern void gclInventoryUpdateBagLite(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID InventoryBagLite *pBagLite);
extern InventoryBag *gclInventoryGetSlotBag(InventorySlot *pSlot, Entity **ppEnt);
extern InventorySlot *gclInventoryUpdateSlot(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID InventoryBag *pBag, SA_PARAM_NN_VALID InventorySlot *pSlot);
extern InventorySlot *gclInventoryUpdateNullSlot(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID InventoryBag *pBag, S32 iIndex);
extern InventoryBagLite *gclInventoryGetSlotLiteBag(InventorySlotLite *pSlotLite, Entity **ppEnt);
extern InventorySlotLite *gclInventoryUpdateSlotLite(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID InventoryBagLite *pBagLite, SA_PARAM_NN_VALID InventorySlotLite *pSlotLite);

// Generate/parse slot keys for UIGens. For the functions that fill pKey, they will initialize all members of pKey.
extern const char *gclInventoryMakeKeyString(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_NN_VALID UIInventoryKey *pKey);
extern bool gclInventoryParseKey(const char *pchKey, SA_PARAM_NN_VALID UIInventoryKey *pKey);
extern bool gclInventoryMakeSlotKey(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID InventoryBag *pBag, SA_PARAM_NN_VALID InventorySlot *pSlot, SA_PARAM_NN_VALID UIInventoryKey *pKey);
extern bool gclInventoryMakeSlotLiteKey(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID InventoryBagLite *pBagLite, SA_PARAM_NN_VALID InventorySlotLite *pSlotLite, SA_PARAM_NN_VALID UIInventoryKey *pKey);

ItemDef *gclInvGetItemDef(const char *pchItemName);
void gclInvCreateCategoryList(SA_PARAM_NN_VALID UIGen *pGen, ItemCategory *peCategories, const char *pchPrefix);
void gclInvSetCategoryInfo(UIItemCategory *pUICategory, ItemCategory eCategory, const char *pchName, const char *pchNamePrefix);
int gclInvCompareUICategoryOrdered(const UIItemCategory **ppLeft, const UIItemCategory **ppRight, const char * const* const*peaPrefixes);

SA_RET_OP_VALID InventoryBag *gclInvExprGenInventoryGetBag(SA_PARAM_OP_VALID Entity *pEnt, S32 iBagID);
bool gclInvCreateCategorizedSlots(UIGen *pGen, Entity *pBagEnt, InventoryBag **eaBags, InventorySlot **eaSlots, const char *pchPrefix, U32 uOptions);

bool gclInvExprGenInventoryBagGetCategorizedInventory(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBag *pBag, const char *pchPrefix, U32 uOptions);

bool gclInvExprGenInventoryMultiBagGetCategorizedInventory(	SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, 
															const char *pchBagIds, const char *pchPrefix, 
															const char *pchIncludeCategories, 
															const char *pchExcludeCategories, U32 uOptions);

const char *gclInvExprInventoryNumericDisplayName(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName);

#endif FC_INVENTORYUI_H