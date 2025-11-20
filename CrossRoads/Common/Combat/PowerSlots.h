#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// PowerSlots are a simple data-defined construct for limiting Power accessibility.
//  Whether or not a Power fits into a PowerSlot is based upon the PowerDef's categories,
//  using a simple include/exclude list.

#include "referencesystem.h"

// Forward declarations
typedef struct	Character	Character;
typedef struct  GameAccountDataExtract GameAccountDataExtract;
typedef struct	Power		Power;
typedef struct	PowerDef	PowerDef;
typedef struct	PTNodeDef	PTNodeDef;
typedef struct	PTGroupDef	PTGroupDef;
typedef enum	PowerMode	PowerMode;


// Defines the PowerSlot restrictions.  A PowerDef must have all the
//  required categories and none of the excluded categories.
AUTO_STRUCT;
typedef struct PowerSlot
{
	const char *pchType;	AST(STRUCTPARAM, POOL_STRING)
		// The type of slot.  The slots aren't in any sort of dictionary, you can have several
		//  of the same type.  You can do lookups to see what type of slot a Power is in.  If we
		//  end up with a lot of functionality based on the type, we should consider making
		//  type its own thing (ala PowerCategories).

	int *peRequires;		AST(NAME(Requires), SUBTABLE(PowerCategoriesEnum))
		// Categories required for this PowerSlot

	int *peExcludes;		AST(NAME(Excludes), SUBTABLE(PowerCategoriesEnum))
		// Categories excluded from this PowerSlot

	REF_TO(PowerDef) hDefAutoSlotReplace;	AST(NAME(DefAutoSlotReplace))
		// If the slot is already filled during auto slotting, but it's filled by this PowerDef,
		//  then it can still be replaced by a different PowerDef

	S32 iRequiredLevel;		AST(NAME(RequiredLevel))
		// If this is greater than 0, the power slot will be unlocked 
		// once the player reaches the given level

	bool bPreventSwappingDuringCooldown;
		// this is being added for the NW wizard's special slot, which gives powers in it bonuses.
		// no good if you can swap each power on your tray into the special slot before use!


} PowerSlot;

// Defines array of slotted PowerIDs
AUTO_STRUCT AST_CONTAINER;
typedef struct PowerSlotSet
{

	U32 *puiPowerIDs;		AST(PERSIST, SOMETIMES_TRANSACT, SELF_ONLY)
		// EArray of the PowerIDs in each slot

} PowerSlotSet;

// Defines array of PowerSlotSets, as well as the currently active Set
AUTO_STRUCT AST_CONTAINER;
typedef struct CharacterPowerSlots
{

	PowerSlotSet **ppSets;	AST(PERSIST, SOMETIMES_TRANSACT, SELF_ONLY)
		// EArray of EArrays, each with the PowerIDs in each slot

	U32 uiIndex;			AST(PERSIST, SOMETIMES_TRANSACT, SELF_ONLY)
		// The currently selected index into the above EArray
		
} CharacterPowerSlots;

// Defines a structure for loading a set of PowerSlots
AUTO_STRUCT;
typedef struct PowerSlotLoadData
{
	PowerSlot **ppSlots;	AST(NAME(PowerSlot))
} PowerSlotLoadData;

AUTO_STRUCT;
typedef struct PowerSlotsConfig
{
	// The power mode required for players to be able
	// to update their power slots (optional)
	PowerMode eRequiredModeForSlotting;

	// The power mode which prevents players to
	// update their power slots (optional)
	PowerMode eModeToDisableSlotting;

	// After you slot a power, it has this cooldown.
	F32 fSecondsCooldownAfterSlotting;

	// if you have this power mode, there is no cooldown for slotting.
	PowerMode eModeToDisableCooldown;

	//	if this is true, you can't slot over a power that is on cooldown.
	bool bDisableSlottingIfCooldown;
} PowerSlotsConfig;

// Returns true if the PowerSlot at the given index allows the PowerDef
//  Using iTray of -1 means current tray
S32 character_PowerTraySlotAllowsPowerDef(SA_PARAM_NN_VALID const Character *pchar, int iTray, int iSlot, SA_PARAM_NN_VALID const PowerDef *pDef, bool bNotifyErrors);

// Returns true if the Character's PowerSlots are valid, meaning all 
//  slotted Powers are allowed in their slots.  Checks only the
//  current set.  Any invalid slottings are removed.
S32 character_PowerSlotsValidate(SA_PARAM_NN_VALID const Character *pchar);

// Places the Power with the specified ID into the specified PowerSlot.
//  If the PowerID is 0, it empties the slot. Returns true if successful.
//  Generally should be called on the server.
//  bIgnoreCooldown: skip cooldown, even if game is configured to have one. e.g. if the server set this for you because you leveled up.
S32 character_PowerTraySlotSet( int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								int iTray,
								int iSlot,
								U32 uiIDPower,
								bool bUnslot,
								GameAccountDataExtract *pExtract,
								bool bIgnoreCooldown,
								bool bNotifyErrors);

S32 character_PowerTraySlotSetNode(	int iPartitionIdx,
									Character *pchar,
									int iTray,
									int iSlot,
									PTNodeDef *pNodeDef,
									PTGroupDef *pGroupDef,
									bool bUnslot,
									GameAccountDataExtract *pExtract,
									bool bIgnoreCooldown);

// Attempts to swap the IDs in the two specified slots.
//  Generally should be called on the server.
S32 character_PowerTraySlotSwap(int iPartitionIdx,
							SA_PARAM_NN_VALID Character *pchar,
							int iTrayA,
							int iSlotA,
							int iTrayB,
							int iSlotB,
							GameAccountDataExtract *pExtract);

// Automatically fills the Character's PowerSlots with Powers, first-come, first-served,
//  assuming they're not already slotted and not in the optional earray of pre-existing IDs.
//  Cleans out taken slots if the IDs are no longer valid.
void character_PowerSlotsAutoSet(int iPartitionIdx,
								 SA_PARAM_NN_VALID Character *pchar,
								 SA_PARAM_OP_VALID const U32 *puiIDs,
								 GameAccountDataExtract *pExtract);

// Replaces all instances of the old ID with the new ID.  Used in pre-commit fixup from a respec.
void character_PowerSlotsReplaceID(SA_PARAM_NN_VALID Character *pchar,
								   U32 uiIDOld,
								   U32 uiIDNew);

// Returns the specified PowerSlot
SA_RET_OP_VALID PowerSlot *character_PowerSlotGetPowerSlot(SA_PARAM_NN_VALID const Character *pchar, int iSlot);

//Retusnr the specified powerslot from the specified tray
SA_RET_OP_VALID PowerSlot *CharacterGetPowerSlotInTrayAtIndex(SA_PARAM_NN_VALID const Character *pchar, int iTray, int iSlot);


// Returns the ID currently set in the specified PowerSlot in the build indexed by iTray
U32 character_PowerSlotGetFromTray(SA_PARAM_NN_VALID Character *pchar,
							int iTray,
							int iSlot);

// Sets the index into the EArray of PowerSlot sets on the Character.  Returns if the value changed.
S32 character_PowerSlotSetCurrent(SA_PARAM_NN_VALID Character *pchar,
								  U32 uiIndex);

// Returns the PowerSlot index if the Character has the given PowerID currently slotted, otherwise returns -1
S32 character_PowerIDSlot(SA_PARAM_NN_VALID Character *pchar,
						  U32 uiID);

// Returns the PowerSlot index if the Character has the given Power currently slotted, otherwise returns -1
S32 character_GetPowerSlot(SA_PARAM_NN_VALID Character *pChar, 
						   SA_PARAM_NN_VALID Power *pPow);


// Returns the PowerSlot index if the Character has the given PowerID currently slotted in iTray's build, otherwise returns -1
S32 character_PowerTrayIDSlot(	Character *pchar,
								int iTray,
								U32 uiID);

// Returns the "type" of PowerSlot if the Character has the given PowerID currently slotted.  Returns NULL if the
//  PowerID isn't slotted, or the PowerSlot it's in has no "type".
SA_RET_OP_STR const char *character_PowerIDSlotType(SA_PARAM_NN_VALID Character *pchar,
												   U32 uiID);


// Returns if the Power must be slotted to be used.  Also checks the parent Power if it's a child.
S32 power_SlottingRequired(SA_PARAM_NN_VALID Power *ppow);

// Returns true if the Character has the given Power currently slotted.  If slotting is being ignored
//  it always returns true.
S32 character_PowerSlotted(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow);

// Returns true if the Character has the given PowerID currently slotted.  If slotting is being ignored
//  it always returns true.
S32 character_PowerIDSlotted(SA_PARAM_NN_VALID Character *pchar, U32 uiID);

void character_PowerIDsSlottedByIndex(Character *pchar,
									  U32 **ppuiIDs,
									  U32 uiIndex);

// Returns the number of PowerSlots in the Character's current PowerSlotSet
S32 character_PowerSlotCount(SA_PARAM_NN_VALID const Character *pchar);
// Returns the number of PowerSlots in the Character's PowerSlotSet
S32 character_PowerSlotCountInTray(SA_PARAM_NN_VALID const Character *pchar, int iTray);

// Indicates whether the power slot is unlocked for the given character
bool character_PowerSlotIsUnlocked(const Character *pChar, SA_PARAM_NN_VALID PowerSlot *pPowerSlot);

// Indicates whether the power slot in the given index is unlocked for the given character
bool character_PowerSlotIsUnlockedBySlotInTray(const Character *pChar, S32 iTray, S32 iSlot);

// Indicates whether the character is eligible to make changes to the power tray
bool character_CanModifyPowerTray(SA_PARAM_NN_VALID Character *pChar, int iTray, int iSlot, bool bNotifyIfFalse);