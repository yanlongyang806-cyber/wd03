#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypeEnum.h"
#include "MultiVal.h"
#include "itemEnums.h"
#include "referencesystem.h"

#define TRAY_SIZE_MAX (12)			// Max number of elements in a tray
#define TRAY_COUNT_MAX (10)			// Max number of trays
#define TRAY_UI_COUNT_MAX (10)		// Max number of ui trays (a ui tray can show any one of the actual trays)

#define TRAY_SIZE_MAX_CONFIG (g_CombatConfig.iTrayMaxSize) // Gets the max size of a tray (normally TRAY_SIZE_MAX, but may be less)

#define TRAYSLOT_VALID(tray,slot) ((tray)>=0 && (tray)<TRAY_COUNT_MAX && (slot)>=0 && slot<TRAY_SIZE_MAX_CONFIG)

typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct PlayerMacro PlayerMacro;
typedef struct Power Power;
typedef struct PowerDef PowerDef;
typedef struct PTNodeDef PTNodeDef;
typedef struct SavedTray SavedTray;
typedef struct GameAccountDataExtract GameAccountDataExtract;

// Defines the various types objects that tray elements can represent
AUTO_ENUM;
typedef enum TrayElemType
{
	kTrayElemType_Unset = 0,		EIGNORE
		// Invalid

	kTrayElemType_Power,			ENAMES(Power)
		// A Power, identified by the uiID of the Power

	kTrayElemType_PowerTreeNode,	ENAMES(PowerTreeNode)
		// A PowerTree Node, identified by the internal name of the Node

	kTrayElemType_InventorySlot,	ENAMES(InventorySlot)
		// A specific Slot in a Bag in your Inventory

	kTrayElemType_PowerSlot,		ENAMES(PowerSlot)
		// An index into the Character's currently active PowerSlot

	kTrayElemType_PowerPropSlot,	ENAMES(PowerPropSlot PowerSlotLocked)
		// A power stored for an AlwaysPropSlot

	kTrayElemType_TempPower,		ENAMES(TempPower)
		// A power granted by an Temporary power, stored by the power def name

	kTrayElemType_SavedPetPower,	ENAMES(SavedPetPower)
		// A power from a SavedPet. Information is faked about this power.
	
	kTrayElemType_Macro,			ENAMES(Macro)
		// This tray element references a macro

	kTrayElemType_Max,				EIGNORE
		// Invalid

} TrayElemType;

// Defines the entity that owns and executes the tray element
AUTO_ENUM;
typedef enum TrayElemOwner
{
	kTrayElemOwner_Self = 0,
		// You own/execute this element
	
	kTrayElemOwner_PrimaryPet,
		// Your primary pet owns/executes this element

} TrayElemOwner;

AUTO_STRUCT;
typedef struct TraySlot
{
	S32 iTray;
	S32 iSlot;
	U32 uiPowID;
	bool bAutoAttack;
} TraySlot;

AUTO_STRUCT;
typedef struct TraySlots
{
	TraySlot** eaTraySlots;
} TraySlots;

// Defines the object being represented in a tray
AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct TrayElem
{
	TrayElemType eType;					AST(PERSIST, NO_TRANSACT)
		// The type of this element, used with the identifier to determine what the
		//  tray element is representing

	S64 lIdentifier;					AST(PERSIST, NO_TRANSACT)
		// S64 used to specify what this tray element is representing

	STRING_MODIFIABLE pchIdentifier;	AST(PERSIST, NO_TRANSACT, POOL_STRING_DB)
		// String used to specify what this tray element is representing

	int iTray;							AST(PERSIST, NO_TRANSACT)
		// What tray this tray element is in

	int iTraySlot;						AST(PERSIST, NO_TRANSACT)
		// What slot in the tray this tray element is in

	EntityRef erOwner;					NO_AST
		// Non-persisted EntityRef that owns and executes this TrayElem

	TrayElemOwner eOwner;				NO_AST
		// Who owns this tray element
		// Currently non-functional
} TrayElem;

// Defines the object being represented in a tray
AUTO_STRUCT AST_CONTAINER AST_IGNORE(mvIdentifier);
typedef struct TrayElemOld
{
	const TrayElemType eType;				AST(PERSIST, SUBSCRIBE)
		// The type of this element, used with the identifier to determine what the
		//  tray element is representing

	const S64 lIdentifier;					AST(PERSIST, SUBSCRIBE)
		// S64 used to specify what this tray element is representing

	CONST_STRING_MODIFIABLE pchIdentifier;	AST(PERSIST, SUBSCRIBE, POOL_STRING_DB)
		// String used to specify what this tray element is representing

	const int iTray;						AST(PERSIST, SUBSCRIBE)
		// What tray this tray element is in

	const int iTraySlot;					AST(PERSIST, SUBSCRIBE)
		// What slot in the tray this tray element is in
} TrayElemOld;

typedef struct NOCONST(TrayElem) NOCONST(TrayElem);

AUTO_STRUCT;
typedef struct DefaultTrayElemDef
{
	const char* pchName;				AST(STRUCTPARAM POOL_STRING)
	const char* pchRelativeTo;			AST(NAME(RelativeTo) POOL_STRING)
	S32 iTray;							AST(NAME(Tray))
	S32 iSlot;							AST(NAME(Slot))
	REF_TO(PTNodeDef) hNodeDef;			AST(NAME(NodeDef))
	InvBagIDs eBagID;					AST(NAME(BagID) SUBTABLE(InvBagIDsEnum) DEFAULT(InvBagIDs_None))
	S32 iBagSlot;						AST(NAME(BagSlot))
	S32 iItemPowerIndex;				AST(NAME(ItemPowerIndex))
	bool bAutoAttack;					AST(NAME(EnableAutoAttack))
	bool bSlotEntireBag;				AST(NAME(SlotEntireBag))
} DefaultTrayElemDef;

typedef struct DefaultTray DefaultTray;

AUTO_STRUCT;
typedef struct DefaultTray
{
	const char* pchDefaultTrayName;		AST(NAME(DefaultTray) KEY STRUCTPARAM)
	const char* pchFilename;			AST(CURRENTFILE)
	DefaultTrayElemDef** ppTrayElems;	AST(NAME(TrayElem))
	REF_TO(DefaultTray) hBorrowFrom;	AST(NAME(BorrowFromTray) REFDICT(DefaultTray))
	DefaultTray* pParent;				NO_AST
} DefaultTray;

extern DictionaryHandle g_hDefaultTrayDict;

SavedTray* entity_GetActiveTray(Entity *e);

// Gets the next unused TrayElem's Tray and Slot.  Returns true when successful.
int entity_TrayGetUnusedTrayElem(Entity *e, SA_PARAM_NN_VALID int *piTrayOut, SA_PARAM_NN_VALID int *piSlotOut, int iPreferredTray);

// Gets the TrayElem given the Tray and Slot.  May return NULL.
SA_RET_OP_VALID TrayElem *entity_TrayGetTrayElem(Entity *e, int iTray, int iSlot);

// Gets the TrayElem given the PowerID.  May return NULL.
SA_RET_OP_VALID TrayElem *entity_TrayGetTrayElemByPowerID(SA_PARAM_NN_VALID Entity *e, U32 uiID, TrayElemOwner eOwner);

// Gets the Power given the TrayElem.  May return NULL.
SA_RET_OP_VALID Power *entity_TrayGetPower(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID TrayElem *pelem);
// Gets the PowerDef given the TrayElem.  May return NULL.
SA_RET_OP_VALID PowerDef *entity_TrayGetPowerDef(Entity *e, TrayElem *pelem);

// Returns whether or not auto-attack can be activated for a particular tray element
bool tray_CanEnableAutoAttackForElem(Entity *e, TrayElem* pElem, S32* piTrayIndex);
// Misc utility

// Returns the Power from the Entity's Item, based on the Bag, Slot and optional ItemPower
//  If the ItemPower is -1, it just returns the first executable Power on the Item
SA_RET_OP_VALID Power* entity_GetTrayPowerFromItem(SA_PARAM_NN_VALID Entity *pEnt, S32 iInvBag, S32 iInvBagSlot, S32 iItemPower, GameAccountDataExtract *pExtract);

// Converts an inventory bag ID, slot, and item power idx into an InventorySlot string identifier (estring)
void tray_InventorySlotIDsToString(SA_PARAM_NN_VALID char **ppchInventorySlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowerIndex);

// Converts an InventorySlot string identifier into the bag and slot id and item power idx
void tray_InventorySlotStringToIDs(SA_PARAM_NN_STR const char *pchInventorySlot, SA_PARAM_NN_VALID S32 *piInvBagOut, SA_PARAM_NN_VALID S32 *piInvBagSlotOut, SA_PARAM_NN_VALID S32 *piItemPowerIndex);

// Converts a PowerPropSlot string identifier to a purpose and prop slot ID
void tray_PowerPropSlotStringToSlotData(const char *pchPowerPropSlot, S32* ePurpose, U32* piPropSlotID);

// Converts PlayerMacro data into a Macro drag string
void tray_MacroDataToDragString(SA_PARAM_NN_VALID char **pestrMacroDragString, PlayerMacro *pMacroData);

// Converts a Macro drag string into PlayerMacro data
void tray_MacroDragStringToMacroData(const char *pchMacroDragString, PlayerMacro *pMacroData);

// Searches an earray of TrayElems to find a match to the given TrayElem based on type and identifier. returns
//  the index, or -1 if not found.
int tray_FindTrayElem(TrayElem*** pppElems, TrayElem *pElem);

// Finds an TempPower type tray elem that matches the power def, and no longer has a matching power
TrayElem *entity_FindEmptyTempPowerTrayElem(Entity *e, const char *pchPowerDef);


// Functions for creating new TrayElems or setting existing TrayElems to a specific type

// Creates a TrayElem mapped to the PowerID
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForPowerID(S32 iTray, S32 iSlot, U32 uiID);

// Creates a TrayElem mapped to the PowerTree Node
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForPowerTreeNode(S32 iTray, S32 iSlot, SA_PARAM_NN_STR const char *pchName);

// Creates a TrayElem mapped to the inventory data
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForInventorySlot(S32 iTray, S32 iSlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowIdx, U32 uiPowID);

// Creates a TrayElem mapped to the power granted temporarily
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForTempPower(S32 iTray,S32 iSlot,S32 uiID,SA_PARAM_NN_STR const char *pchPowerDef);

// Creates a TrayElem mapped to the PowerSlot index
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForPowerSlot(S32 iTray, S32 iSlot, U32 uiIndex);

// Creates a TrayElem mapped to the AlwaysPropSlot PetID and PowID
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForPowerPropSlot(S32 iTray, S32 iSlot, U32 uiPowID, S32 ePurpose, U32 uPropSlotID);

// Creates a TrayElem mapped to a PetID and PowerDef name
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForSavedPetPower(S32 iTray, S32 iSlot, U32 uiPetID, const char *pchPowerDef);

// Creates a TrayElem mapped to the macro ID
SA_RET_OP_VALID TrayElem *tray_CreateTrayElemForMacro(S32 iTray, S32 iSlot, U32 uMacroID);

// Creates a TrayElem from a TrayElemOld
SA_RET_OP_VALID TrayElem* tray_CreateTrayElemFromOldTrayElem(const TrayElemOld* pElemOld);

// Sets a TrayElem from a TrayElemOld
void tray_SetTrayElemFromOldTrayElem(TrayElem* pElem, const TrayElemOld* pElemOld);

// Sets a TrayElem mapped to the PowerID
void tray_SetTrayElemForPowerID(SA_PARAM_NN_VALID TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiID);

// Sets a TrayElem mapped to the PowerTree Node
void tray_SetTrayElemForPowerTreeNode(SA_PARAM_NN_VALID TrayElem *pelem, S32 iTray, S32 iSlot, SA_PARAM_NN_STR const char *pchName);

// Sets a TrayElem mapped to the inventory data
void tray_SetTrayElemForInventorySlot(SA_PARAM_NN_VALID TrayElem *pelem, S32 iTray, S32 iSlot, S32 iInvBag, S32 iInvBagSlot, S32 iItemPowerIdx, U32 uiPowID);

// Sets a TrayElem mapped to a Temporary power
void tray_SetTrayElemForTempPower(SA_PARAM_NN_VALID TrayElem *pelem, S32 iTray, S32 iSlot, S32 uiID, SA_PARAM_NN_STR const char *pchPowerDef);

// Sets a TrayElem mapped to the PowerSlot index
void tray_SetTrayElemForPowerSlot(SA_PARAM_NN_VALID TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiIndex);

// Sets a TrayElem mapped to the AlwaysPropSlot PetID and PowID
void tray_SetTrayElemForPowerPropSlot(SA_PARAM_NN_VALID TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiPowID, S32 ePurpose, U32 uPropSlotID);

// Sets a TrayElem mapped to a PetID and PowerDef name
void tray_SetTrayElemForSavedPetPower(SA_PARAM_NN_VALID TrayElem *pelem, S32 iTray, S32 iSlot, U32 uiPetID, const char *pchPowerDef);

// Sets a TrayElem mapped to the macro ID
void tray_SetTrayElemForMacro(TrayElem *pelem, S32 iTray, S32 iSlot, U32 uMacroID);