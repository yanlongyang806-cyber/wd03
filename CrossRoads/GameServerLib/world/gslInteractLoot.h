/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "Entity.h"
#include "EntityInteraction.h"
#include "Team.h"

typedef struct GameInteractable GameInteractable;
typedef struct TeamLootEvent TeamLootEvent;
typedef struct LootInteractCBData LootInteractCBData;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct InventoryBag InventoryBag;

// This is the roll status structure that is associated with each item being team-looted
AUTO_STRUCT;
typedef struct TeamLootEventItemRoll
{
	EntityRef				uiEntRef;					// ref to the entity that made this roll
	NeedOrGreedChoice		eRollChoice;				// the entity's roll choice
} TeamLootEventItemRoll;

// One of these is created for each item in a team-looted bag to hold the status of various
// member-specific data to the item as well as the winner/assignee of the item
AUTO_STRUCT;
typedef struct TeamLootEventItem
{
	U64						itemID;						// ID of the item in the bag
	TeamLootEventItemRoll	**peaTeamRolls;				// all rolls made on the item
	bool					bResolved;					// indicates that this event item should no longer be usable

	TeamLootEvent			*pParentEvent;	AST(UNOWNED)// parent event
} TeamLootEventItem;

// A TeamLootEvent is created for each need-or-greed or master looter bag that a
// team loots; it is held until the entire loot assignment is resolved
AUTO_STRUCT;
typedef struct TeamLootEvent
{
	int						iPartitionIdx;
    LootMode				eLootMode;				NO_AST				// looting mode for this event
	EntityRef				uiInitiator;								// a ref to the player that initiated the event
	EntityRef				uiLootEnt;									// a ref to the loot entity possessing the loot bag
	const char				*pchLootInteractName;	AST(POOL_STRING)	// zmap scope name of the loot interactable (pooled)
 	int idxEntry;														// index of the interaction entry on the node
	REF_TO(Team)			hTeam;										// ref to the team that initiated the event
    TeamLootEventItem		**peaItems;									// data on the loot status of every item stack in the loot bag
} TeamLootEvent;

AUTO_STRUCT;
typedef struct TeamLootGiveItemCBData
{
	int						iPartitionIdx;
	EntityRef				uiRecipientRef;								// recipient of the loot
	EntityRef				uiLootEntRef;								// loot entity
	const char				*pchLootInteractName;	AST(POOL_STRING)	// loot interactable
	int						itemID;									// slot index in the loot bag that was looted
} TeamLootGiveItemCBData;

extern ParseTable parse_TeamLootEvent[];
#define TYPE_parse_TeamLootEvent TeamLootEvent
extern ParseTable parse_TeamLootGiveItemCBData[];
#define TYPE_parse_TeamLootGiveItemCBData TeamLootGiveItemCBData

extern StashTable s_LootEventFromLootEntRef;
extern StashTable s_LootEventFromLootInteractName;

const char* interactloot_GetLootFXName(S32 iHighestItemQuality, RewardPickupType ePickupType, bool bBagHasMissionItem);

bool interactloot_CleanupLootEntBag(Entity *pLootEnt, InventoryBag *pLootBag, Entity* pLootingPlayer);
bool interactloot_CleanupLootInteraction(Entity *pPlayerEnt, InteractionLootTracker **ppLootTracker, InventoryBag *pLootBag);

void interactloot_TeamLootGiveItemCallback(TransactionReturnVal *returnVal, TeamLootGiveItemCBData *pData);

void interactloot_PerformInteract(Entity *pPlayerEnt, Entity *pTargetEnt, GameInteractable *pTargetInteractable, InventoryBag** eaLootBags);
void interactloot_PerformLootEntInteract(Entity *pPlayerEnt, Entity *pTargetEnt);
void interactloot_EndInteract(Entity *pPlayerEnt, EntityRef uiLootEntRef, const char *pchLootInteractableName);

void interactloot_NeedOrGreedResolveRolls(TeamLootEventItem *pEventItem, bool bForce);
void interactloot_AssignNeedOrGreedRewards(TeamLootEvent *pEvent);

bool LootTracker_CanEntityLoot(InteractionLootTracker *pLootTracker, Entity *pPlayerEnt);
bool LootTracker_FindOwnedLootBags(InteractionLootTracker *pLootTracker, Entity *pPlayerEnt, InventoryBag*** peaBagsOut);
bool LootTracker_FindBagsForTeamID(InteractionLootTracker *pLootTracker, U32 uiTeamID, InventoryBag*** peaBagsOut);
void LootTracker_RemoveItemFromAllBags(InteractionLootTracker *pLootTracker, U64 iID);
void LootTracker_Cleanup(InteractionLootTracker **ppTracker);