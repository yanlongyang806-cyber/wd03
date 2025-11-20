#ifndef LOOT_UI_H
#define LOOT_UI_H

typedef U32 EntityRef;
typedef struct Item Item;

/********************************************************
* TEAM LOOT UI
********************************************************/
// This is the roll status structure that is used to hold all rolls on each item;
// This structure is not yet being used
//AUTO_STRUCT;
//typedef struct TeamLootItemRoll
//{
//	EntityRef				uiEntRef;				// ref to the entity that made this roll
//	NeedOrGreedChoice		eRollChoice;			// the entity's roll choice
//} TeamLootItemRoll;

// This is the structure used for the primary list models of the team looting UIs
AUTO_STRUCT;
typedef struct TeamLootItemSlot
{
	EntityRef				uiLootEnt;									// loot ent ref from which the item is being looted
	const char				*pchLootInteractable;	AST(POOL_STRING)	// interactable name from which the item is being looted (pooled)
	Item					*pItem;										// loot item
	int						iCount;										// number of items being rolled on
	int						iItemID;										// location of item in the loot bag
	int						iLocalStartTime;							// local start time for looting this item
	int						iTimeLimit;									// duration of need or greed roll time limit

	// The following fields have been commented out as they are not currently being used yet
	//	TeamLootItemRoll		**peaTeamRolls;			// all rolls made on the item (can be used later for more managed master looter or need or greed interfact; currently unused)
	//	EntityRef				uiEntWinner;			// ref to the winner of the item (can be used for need or greed interface; currently unused)
} TeamLootItemSlot;

#endif // LOOT_UI_H
