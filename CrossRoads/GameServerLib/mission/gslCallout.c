/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiLib.h"
#include "Character_target.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "gslCallout.h"
#include "gslMission.h"
#include "ItemCommon.h"
#include "Player.h"
#include "rand.h"
#include "textparser.h"
#include "inventoryCommon.h"

// ----------------------------------------------------------------------------
//  Static Data
// ----------------------------------------------------------------------------

// NPC Callout defines
#define NPC_CALLOUT_DIST 80.f
#define CALLOUT_TICK 60
#define PLAYER_CALLOUT_TIMEOUT 30

static U32 s_uCalloutTick = 0;

// Base chance for an NPC callout to happen for a particular NPC
F32 g_CalloutBaseChance = 0.1f;

AUTO_CMD_FLOAT(g_CalloutBaseChance, CivilianCalloutChance);


// ----------------------------------------------------------------------------
//  NPC Callout
// ----------------------------------------------------------------------------

static void callout_GetCalloutItems(Entity *pEnt, Item ***peaCalloutItems)
{
	if (pEnt && pEnt->pInventoryV2) {
		int iBag, iItem;

		PERFINFO_AUTO_START_FUNC();

		for (iBag = 0; iBag < eaSize(&pEnt->pInventoryV2->ppInventoryBags); iBag++) {
			InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];

			for (iItem = 0; iItem < eaSize(&pBag->ppIndexedInventorySlots); iItem++) {
				InventorySlot *pSlot = pBag->ppIndexedInventorySlots[iItem];
				if (pSlot && pSlot->pItem) {
					ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);
					if (pItemDef && GET_REF(pItemDef->calloutMsg.hMessage)){
						eaPush(peaCalloutItems, pSlot->pItem);
					}
				}
			}
		}

		PERFINFO_AUTO_STOP();
	}
}


// Get the callout percent for low importance callouts. If the config is not present then use old default value
static F32 callout_GetCalloutPercent(void)
{
	if (g_MissionConfig.fCalloutPercent > 0.0f) {
		return g_MissionConfig.fCalloutPercent;
	}

	return g_CalloutBaseChance;
}


// Get the callout time for a player. If the config is not present then use old default value
static U32 callout_GetPlayerCalloutTimeSeconds(void)
{
	if (g_MissionConfig.uPlayerCalloutTimeSeconds > 0) {
		return g_MissionConfig.uPlayerCalloutTimeSeconds;
	}
	
	return PLAYER_CALLOUT_TIMEOUT;
}


void callout_ProcessCallout(void)
{
	static Entity **eaNearbyCivilians = NULL;
	static Item **eaCalloutItems = NULL;

	Entity *pEnt;
	EntityIterator *pIter;
	U32 currTickModded;
	U32 uWhichPlayer = 0;
	F32 fCalloutPercent;
	U32 uCurTime;
	
	currTickModded = s_uCalloutTick % CALLOUT_TICK;
	fCalloutPercent = callout_GetCalloutPercent();
	uCurTime =  timeSecondsSince2000();

	// Process all players
	// Don't process ENTITYFLAG_IGNORE ents here, just to save time; they're probably still loading the map
	pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((pEnt = EntityIteratorGetNext(pIter))) {
		++uWhichPlayer;

		if (pEnt && (uWhichPlayer % CALLOUT_TICK == currTickModded)) {
			Vec3 vPlayerPos;
			
			if (pEnt->pPlayer->lastCalloutTime == 0) {
				pEnt->pPlayer->lastCalloutTime = uCurTime;
				continue;
			}
			if (uCurTime < pEnt->pPlayer->lastCalloutTime + callout_GetPlayerCalloutTimeSeconds()) {
				continue;
			}

			PERFINFO_AUTO_START("FindPossibleCallouts", 1);

			entGetPos(pEnt, vPlayerPos);
			eaClearFast(&eaNearbyCivilians);
			eaClearFast(&eaCalloutItems);

			// Find all possible Callouts
			callout_GetCalloutItems(pEnt, &eaCalloutItems);

			PERFINFO_AUTO_STOP(); // FindPossibleCallouts

			if (!eaSize(&eaCalloutItems)) {
				continue;
			}

			PERFINFO_AUTO_START("FindNearbyEnts", 1);

			// Find all nearby Civilian Entities
			entGridProximityLookupExEArray(entGetPartitionIdx(pEnt), vPlayerPos, &eaNearbyCivilians, NPC_CALLOUT_DIST, ENTITYFLAG_CIV_PROCESSING_ONLY, 0, NULL);

			PERFINFO_AUTO_STOP(); // FindNearbyEnts

			PERFINFO_AUTO_START("AssignCalloutToCivilian", 1);

			// Assign a random Callout item to each Civilian
			while (eaSize(&eaNearbyCivilians)) {
				Entity *pCiv = eaPop(&eaNearbyCivilians);
				Item *pCalloutItem = NULL;

				if (pCiv && pCiv->pCritter && pCiv->pCritter->civInfo 
					&& aiCivilianCanDoCallout(pCiv, pCiv->pCritter->civInfo, pEnt)
					&& entity_TargetInArc(pCiv, pEnt, NULL, PI, 0.f)) {	// Only those within 180º facing arc
					int i;
					int importantItem = -1;

					for (i = 0; i < eaSize(&eaCalloutItems); i++) {
						ItemDef *pItemDef = GET_REF(eaCalloutItems[i]->hItem);
						if (pItemDef->bImportantCallout) {
							importantItem = i;
							break;
						}
					}
					if (importantItem != -1) {
						// Always do an important one if it's available
						pCalloutItem = eaRemove(&eaCalloutItems, importantItem);
					} else if (eaSize(&eaCalloutItems) && randomMersennePositiveF32(NULL) < fCalloutPercent) {
						// %chance to do callout
						// Select a Callout at random
						pCalloutItem = eaRemove(&eaCalloutItems, randomIntRange(0, eaSize(&eaCalloutItems) - 1));					
					}

					if (pCalloutItem) {
						ItemDef *pItemDef = GET_REF(pCalloutItem->hItem);
						Message *pMessage = GET_REF(pItemDef->calloutMsg.hMessage);
						aiCivilianDoCallout(pCiv, pCiv->pCritter->civInfo, pEnt, pItemDef->calloutFSM, pMessage->pcMessageKey, pCalloutItem->id);
						if (pEnt->pPlayer) {
							pEnt->pPlayer->lastCalloutTime = timeSecondsSince2000();
						}
					} else {
						// Add cooldown for this player
						aiCivilianAddCooldownForPlayer(pCiv->pCritter->civInfo, pEnt);
					}
				}
			}

			PERFINFO_AUTO_STOP();
		}
	}

	EntityIteratorRelease(pIter);
	++s_uCalloutTick;
}
