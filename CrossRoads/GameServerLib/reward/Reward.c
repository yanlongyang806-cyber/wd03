/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "error.h"
#include "logging.h"
#include "utils.h"
#include "wininclude.h"

#include "textparser.h"
#include "earray.h"
#include "earray_inline.h"
#include "referencesystem.h"
#include "Expression.h"
#include "ExpressionFunc.h"
#include "rand.h"
#include "StringCache.h"

#include "Entity.h"
#include "EntityIterator.h"
#include "EntityBuild.h"
#include "EntitySavedData.h"
#include "entCritter.h"
#include "entCritter.h"
#include "ExpressionPrivate.h"
#include "gslCritter.h"
#include "gslEntity.h"
#include "gslInteraction.h"
#include "gslInteractLoot.h"
#include "gslLogSettings.h"
#include "gslMissionDrop.h"
#include "interaction_common.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Team.h"
#include "AutoTransDefs.h"
#include "objContainer.h"
#include "EntityLib.h"
#include "CostumeCommon.h"
#include "RegionRules.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "Character_combat.h"
#include "Character_target.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"
#include "SavedPetCommon.h"
#include "UGCCommon.h"
#include "ugcprojectcommon.h"
#include "NotifyCommon.h"
#include "WorldGrid.h"
#include "TeamUpCommon.h"
#include "gslTeamUp.h"

#include "mission_common.h"
#include "gslMission.h"
#include "oldencounter_common.h"
#include "mapstate_common.h"
#include "GameAccountDataCommon.h"
#include "GameBranch.h"
#include "GamePermissionsCommon.h"

#include "DamageTracker.h"
#include "gslActivity.h"
#include "gslSendToClient.h"
#include "gslChat.h"
#include "gslInteractable.h"
#include "gslQueue.h"
#include "EntityMovementManager.h"
#include "EntityMovementProjectile.h"
#include "EntityMovementDefault.h"

#include "itemCommon.h"
#include "itemTransaction.h"
#include "itemServer.h"
#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "reward.h"
#include "algoitem.h"

#include "LoggedTransactions.h"
#include "GameServerLib.h"
#include "gslSocial.h"
#include "gslActivityLog.h"

#include "MapDescription.h"
#include "WorldGrid.h"
#include "wlEncounter.h"
#include "gslSendToClient.h"
#include "aiLib.h"
#include "gslNamedPoint.h"

#include "CombatMods.h"
#include "encounter_common.h"
#include "CombatEval.h"
#include "PowerVars.h"

#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Reward_c_ast.h"
#include "AutoGen/RewardCommon_h_ast.h"
#include "Autogen/NotifyEnum_h_ast.h"

#include "ShardVariableCommon.h"
#include "gslShardVariable.h"
#include "gslWorldVariable.h"

#include "fileutil.h"
#include "FolderCache.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "inventoryTransactions.h"


extern ExprContext *g_pRewardNonPlayerContext;
extern ExprContext *g_pRewardPlayerContext;

extern ParseTable parse_Character[];
extern ItemQualities g_ItemQualities;
#define TYPE_parse_Character Character

static char s_pcVarPlayerList[] = "PlayerList";
static int s_hVarPlayerList = 0;

DictionaryHandle g_hRewardTierDict;
DictionaryHandle g_hDropRateTableDict;
DictionaryHandle g_hDropSetDict;

static const char* s_pchKilled;
static int s_hKilled = 0;

static const char* s_pchItemName_XP = NULL;
static const char* s_pchItemName_RP = NULL;
static const char* s_pchItemName_Resources = NULL;
static const char* s_pchItemName_Pvp_Resources = NULL;
static const char* s_pchItemName_Stars = NULL;
static const char* s_pchItemName_SkillPoint = NULL;
static const char* s_pchItemName_OfficerSkillPoint = NULL;

AUTO_STRUCT;
typedef struct LevelupNotification
{
	REF_TO(Message) hMessage;				AST(STRUCTPARAM REQUIRED NON_NULL_REF)
	const char *pchTexture;					AST(POOL_STRING)
	NotifyType eType;						AST(NAME(Type))
} LevelupNotification;

AUTO_STRUCT;
typedef struct LevelupNotificationGroup
{
	S32 iLevel;								AST(STRUCTPARAM REQUIRED)
	LevelupNotification **eaNotifies;		AST(NAME(Notify))

	// Conditions
	const char *pchCharacterPath;			AST(NAME(CharacterPath))
} LevelupNotificationGroup;

AUTO_STRUCT;
typedef struct LevelupNotificationGroups
{
	LevelupNotificationGroup **eaLevels;	AST(NAME(Level))
} LevelupNotificationGroups;

static LevelupNotificationGroups s_LevelupNotificationGroups;

static int s_rewardtrace = 0;
// print a trace of what the reward system did during a given reward
AUTO_CMD_INT(s_rewardtrace,reward_trace);

static void LootBag_ShareNumerics(Entity* player_ent, S32 iDstBagID, S32 iSlot, InventoryBag *pLootBag, EntityRef opt_loot_ent, Item *pItem, const ItemChangeReason *pReason);


//Initializes or creates a RewardContext with default values.
RewardContext* Reward_CreateOrResetRewardContext(RewardContext* pContext)
{
	if (!pContext)
		pContext = StructCreate(parse_RewardContext);
	else
		StructReset(parse_RewardContext, pContext);

	pContext->KillerLevel = 1;
	pContext->RewardLevel = 1;
	pContext->RewardScale = 1.0;
	pContext->TeamMemberPercentage = 1.0;
	pContext->bGiveItems = true;
	pContext->bBestTeam = true;
	pContext->TeamSize = 1;
	pContext->iKillerCombatLevel = 1;
	pContext->iTeamRealSize = 1;
	pContext->iTeamCombatLevel = 0;
	pContext->iForceSoloOwnershipContainerID = -1;

	return pContext;
}

AUTO_RUN;
void Reward_InitStrings(void)
{
	s_pchKilled = allocAddStaticString("Killed");
	s_pchItemName_XP = allocAddStaticString("XP");
	s_pchItemName_RP = allocAddStaticString("RP");
	s_pchItemName_Resources = allocAddStaticString("Resources");
	s_pchItemName_Pvp_Resources = allocAddStaticString("Pvp_Resources");
	s_pchItemName_Stars = allocAddStaticString("Stars");
	s_pchItemName_SkillPoint = allocAddStaticString("Skillpoint");
	s_pchItemName_OfficerSkillPoint = allocAddStaticString("Officerskillpoint");

}

static int rewardtrace_indent = 0;

static void rewardtracev(char *fmt, va_list argptr)
{
	char indent[128];
	int i;
	
	if(!s_rewardtrace)
		return;
	indent[0] = 0;
	for(i = 0; i < rewardtrace_indent && i < ARRAY_SIZE(indent); i++)
		indent[i] = ' ';
	printf("%s",indent);
	vprintf(fmt,argptr);
	printf("\n");
}


static void rewardtrace(char *fmt,...)
{
	VA_START(va,fmt);
	rewardtracev(fmt,va);
	VA_END();
}

static void reward_ContextCleanup(RewardContext *pContext)
{
	if(pContext)
	{
		StructDestroy(parse_RewardContext, pContext);
	}
}

static void reward_GetAllPossibleCharacterBasedTables(Entity* pEnt, CharacterBasedIncludeContext* pContext, const char* pchPrefix, CharacterBasedIncludeType eType, RewardTable*** peaTablesOut)
{
	char* estrName = NULL;
	RewardTable* pTable = NULL;
	int iPath = 0;
	estrStackCreate(&estrName);
	if (pContext && pchPrefix && pchPrefix[0] && eType > 0)
	{
		switch (eType)
		{
		case kCharacterBasedIncludeType_Class:
			{
				estrPrintf(&estrName, "%s_%s", pchPrefix, pContext->pchClass);
			}break;
		case kCharacterBasedIncludeType_AllClassPaths:
			{
				for (iPath = 0; iPath < eaSize(&pContext->eaClassPathNames); iPath++)
				{
					estrPrintf(&estrName, "%s_%s", pchPrefix, pContext->eaClassPathNames[iPath]);
					pTable = RefSystem_ReferentFromString(g_hRewardTableDict, estrName);
					if (!pTable)
						Errorf("Character-Based Include reward entry resulted in non-existant table named \"%s\". Prefix: %s, type: %s.", estrName, pchPrefix, StaticDefineIntRevLookup(CharacterBasedIncludeTypeEnum, eType));
					else
						eaPush(peaTablesOut, pTable);
				}

				estrDestroy(&estrName);
				if (eaSize(peaTablesOut) > 0)
					return;
			}break;
		case kCharacterBasedIncludeType_Species:
			{
				estrPrintf(&estrName, "%s_%s", pchPrefix, pContext->pchSpecies);
			}break;
		case kCharacterBasedIncludeType_Gender:
			{
				estrPrintf(&estrName, "%s_%s", pchPrefix, StaticDefineIntRevLookup(GenderEnum, pContext->eGender));
			}break;
		}
	}
	else if (pEnt && pEnt->pChar && pchPrefix && pchPrefix[0] && eType > 0)
	{
		switch (eType)
		{
		case kCharacterBasedIncludeType_Class:
			{
				estrPrintf(&estrName, "%s_%s", pchPrefix, REF_STRING_FROM_HANDLE(pEnt->pChar->hClass));
			}break;
		case kCharacterBasedIncludeType_AllClassPaths:
			{
				CharacterPath** eaPaths = NULL;
				eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
				entity_GetChosenCharacterPaths(pEnt, &eaPaths);

				for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
				{
					estrPrintf(&estrName, "%s_%s", pchPrefix, eaPaths[iPath]->pchName);
					pTable = RefSystem_ReferentFromString(g_hRewardTableDict, estrName);
					if (!pTable)
						Errorf("Character-Based Include reward entry resulted in non-existant table named \"%s\". Prefix: %s, type: %s.", estrName, pchPrefix, StaticDefineIntRevLookup(CharacterBasedIncludeTypeEnum, eType));
					else
						eaPush(peaTablesOut, pTable);
				}

				estrDestroy(&estrName);
				if (eaSize(peaTablesOut) > 0)
					return;
			}break;
		case kCharacterBasedIncludeType_Species:
			{
				estrPrintf(&estrName, "%s_%s", pchPrefix, REF_STRING_FROM_HANDLE(pEnt->pChar->hSpecies));
			}break;
		case kCharacterBasedIncludeType_Gender:
			{
				estrPrintf(&estrName, "%s_%s", pchPrefix, StaticDefineIntRevLookup(GenderEnum, pEnt->eGender));
			}break;
		}
	}
	else
		return;

	pTable = RefSystem_ReferentFromString(g_hRewardTableDict, estrName);

	if (!pTable)
	{
		char* estrDefault = NULL;
		estrStackCreate(&estrDefault);
		estrPrintf(&estrDefault, "%s_Default", pchPrefix);

		pTable = RefSystem_ReferentFromString(g_hRewardTableDict, estrDefault);
		if (!pTable && estrName && estrName[0])
			Errorf("Character-Based Include reward entry resulted in non-existant table named \"%s\", and no default table was defined. Prefix: %s, type: %s.", estrName, pchPrefix, StaticDefineIntRevLookup(CharacterBasedIncludeTypeEnum, eType));
		else if (!pTable)
			Errorf("Character-Based Include reward entry failed to result in a valid table name, and no default table was defined. Prefix: %s, type: %s.", pchPrefix, StaticDefineIntRevLookup(CharacterBasedIncludeTypeEnum, eType));

		estrDestroy(&estrDefault);
	}

	estrDestroy(&estrName);
	if (pTable)
		eaPush(peaTablesOut, pTable);
}

static RewardTable* reward_GetCharacterBasedTable(Entity* pEnt, CharacterBasedIncludeContext* pContext, const char* pchPrefix, CharacterBasedIncludeType eType, U32* pSeed)
{
	RewardTable** eaTables = NULL;
	RewardTable* pTable = NULL;
	S32 iNumPaths = 1;

	if (eType == kCharacterBasedIncludeType_AllClassPaths)
	{
		if (pContext)
			iNumPaths = eaSize(&pContext->eaClassPathNames);
		else if (pEnt && pEnt->pChar)
			iNumPaths = eaSize(&pEnt->pChar->ppSecondaryPaths) + 1;

		if (iNumPaths > 0)
		{
			eaStackCreate(&eaTables, iNumPaths);
		}
		else
		{
			Errorf("reward_GetCharacterBasedTable() was supposed to get characterpath-based tables, but wound up with 0 valid ones. Go find CMiller and make fun of him.");
			return NULL;
		}
	}

	reward_GetAllPossibleCharacterBasedTables(pEnt, pContext, pchPrefix, eType, &eaTables);

	if (eaSize(&eaTables) > 1)
	{
		int iPath = randomIntRangeSeeded(pSeed, RandType_LCG, 0, eaSize(&eaTables)-1);
		pTable = eaTables[iPath];
	}
	else if (eaSize(&eaTables) > 0)
		pTable = eaTables[0];

	return pTable;
}

int GetEntLevel(Entity* pEnt)
{
	if (!pEnt)
		return 1;

	//first check if this is a player and if the player has XP
	if (pEnt->pPlayer)
	{
		int Level;

		Level = entity_GetSavedExpLevelLimited(pEnt);

		if (Level > 0)
			return Level;
	}

	//next check for a valid combat level
	if (pEnt->pChar && pEnt->pChar->iLevelCombat > 0 )
		return pEnt->pChar->iLevelCombat;


	//else just return 1
	return 1;
}

static void rewardbag_AddNumeric(NOCONST(InventoryBag)* pBag, Item *pNumeric)
{
	NOCONST(Item) *pItem = NULL;
	ItemDef *pItemDef = SAFE_GET_REF(pNumeric,hItem);
	int iItemCount, i;
	Item **ppItems = NULL;
	bool bFound = false;

	if(!pItemDef || pItemDef->eType != kItemType_Numeric)
		return;

	iItemCount = inv_bag_trh_GetSimpleItemList(ATR_EMPTY_ARGS, pBag, &ppItems, false, -1);

	for(i=0; i<iItemCount; ++i)
	{
		ItemDef *pItemDefCheck = NULL;
		
		pItem = CONTAINER_NOCONST(Item, ppItems[i]);
		pItemDefCheck = SAFE_GET_REF(pItem, hItem);

		if(!pItem || !pItemDefCheck || pItemDefCheck->eType != kItemType_Numeric)
			continue;

		if( pItemDefCheck->pchName == pItemDef->pchName 
			&& pItem->numeric_op == pNumeric->numeric_op)
		{
			bFound = true;
			break;
		}
	}

	eaDestroy(&ppItems);

	//If we don't have this specific numeric het, put it in the bag in the last slot
	if(!bFound)
	{
		pItem = StructCloneDeConst(parse_Item, pNumeric);
		if (pItem)
		{
			int iSlotIdx = inv_trh_GetSlot(ATR_EMPTY_ARGS, NULL, pBag, -1, pItemDef ? pItemDef->pchName : NULL, pItemDef && IS_HANDLE_ACTIVE(pItemDef->hSlotID) ? GET_REF(pItemDef->hSlotID) : NULL);
			NOCONST(InventorySlot) *pSlot = eaGet(&pBag->ppIndexedInventorySlots,iSlotIdx);
			if(pSlot)
			{
				pSlot->pItem = pItem;
			}
			else
			{
				StructDestroyNoConstSafe(parse_Item, &pItem);
			}
		}
		return;
	}

	if(!pItem)
		return;

	switch(pNumeric->numeric_op)
	{
	case NumericOp_Add:
		{
			//Add the two numerics together
			S64 finalVal = pItem->count + pNumeric->count;

			if(finalVal > INT_MAX)
				finalVal = INT_MAX;
			else if (finalVal < INT_MIN)
				finalVal = INT_MIN;
			pItem->count = (S32) finalVal;
			break;
		}
	case NumericOp_RaiseTo:
		{
			if (pItem->count < pNumeric->count)
				pItem->count = pNumeric->count;
			break;
		}
	case NumericOp_LowerTo:
		{
			if (pItem->count > pNumeric->count)
				pItem->count = pNumeric->count;
			break;
		}
	case NumericOp_SetTo:
		{
			pItem->count = pNumeric->count;
			break;
		}
	}

	//(BH): I previously had clamped the numeric value here
	//  However, the clamping shouldn't be done until it is transferred to an entity.
}

//These functions take ownership of the passed-in item.
void rewardbag_AddItem(InventoryBag *bag, Item *item, bool bSetID)
{
	if (!bag || !item)
	{
		Errorf("rewardbag_AddItem: NULL(%p) bag or NULL item(%p) passed.",bag,item);
		return;
	}
	else
	{
		NOCONST(Item)* pItem = CONTAINER_NOCONST(Item, item);
		ItemDef *def = SAFE_GET_REF(item, hItem);
		if (pItem && bag->pRewardBagInfo && bSetID)
			pItem->id = bag->pRewardBagInfo->nextID++;
		if(def && def->eType == kItemType_Numeric)
		{
			rewardbag_AddNumeric(CONTAINER_NOCONST(InventoryBag, bag), CONTAINER_RECONST(Item, pItem));
			StructDestroy(parse_Item, item);
		}
		else
		{
			inv_bag_trh_AddItem(ATR_EMPTY_ARGS, NULL, NULL, CONTAINER_NOCONST(InventoryBag, bag), -1, pItem, 0, NULL, NULL, NULL);
		}
	}
	
}

void reward_FixupRewardItemIDs(InventoryBag*** peaBags)
{
	int i,j;
	int id = 0;
	for (i = 0; i < eaSize(peaBags); i++)
	{
		for (j = 0; j < eaSize(&((*peaBags)[i]->ppIndexedInventorySlots)); j++)
		{
			if ((*peaBags)[i]->ppIndexedInventorySlots[j]->pItem)
			{
				NOCONST(Item)* pItem = CONTAINER_NOCONST(Item, (*peaBags)[i]->ppIndexedInventorySlots[j]->pItem);
				pItem->id = id++;
			}
		}
	}
	for (i = 0; i < eaSize(peaBags); i++)
	{
		(*peaBags)[i]->pRewardBagInfo->nextID = id;
	}
}

void rewardbag_RemoveItem(InventoryBag *rw_bag, int index, Item **res_item)
{
	Item *item = CONTAINER_RECONST(Item, inv_bag_trh_RemoveItem(ATR_EMPTY_ARGS, NULL, false,  CONTAINER_NOCONST(InventoryBag, (rw_bag)), index, -1, NULL));

	if(res_item)
	{
		if(item)
			*res_item = item;
		else
			*res_item = NULL;
	}
	else if(item)
	{
		StructDestroy(parse_Item,item);
	}
}

bool rewardbag_RemoveItemByID(InventoryBag *rw_bag, int id, Item **res_item, int* pCountOut)
{
	BagIterator* pIter = inv_bag_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, (rw_bag)), id, NULL);
	int count = pIter ? bagiterator_GetItemCount(pIter) : 0;
	Item *item = pIter ? (Item*)inv_bag_trh_RemoveItem(ATR_EMPTY_ARGS, NULL, false,  CONTAINER_NOCONST(InventoryBag, (rw_bag)), pIter->i_cur, -1, NULL) : NULL;

	bagiterator_Destroy(pIter);

	if (pCountOut)
		*pCountOut = count;
	if(res_item)
	{
		if(item)
			*res_item = item;
		else
			*res_item = NULL;
	}
	else if(item)
	{
		StructDestroy(parse_Item,item);
	}
	return !!pIter;
}

int rewardbag_Size(InventoryBag *rw_bag)
{
	int i;
	int res = 0;
	if(!rw_bag)
		return 0;
	for(i = eaSize(&rw_bag->ppIndexedInventorySlots)-1; i>=0; --i)
	{
		InventorySlot const *slot = rw_bag->ppIndexedInventorySlots[i];
		if(slot && slot->pItem)
			res++;
	}
	return res;
}


// find item by name, -1 on failure
int rewardbag_FindItemIndexByDefName(InventoryBag const *bag, char const *def_name)
{
	int i;
	if(!bag || !def_name)
		return -1;
	for (i = eaSize(&bag->ppIndexedInventorySlots)-1; i >= 0; --i)
	{
		ItemDef *def = NULL;

		InventorySlot const* pSlot = bag->ppIndexedInventorySlots[i];

		if (pSlot->pItem)
		{
			def = SAFE_GET_REF2(pSlot,pItem,hItem);
		}
		if (def && 0 == strcmp(def_name,def->pchName))
			break; // done!
	}
	return i;
}

#define LAUNCH_VEL  31.0
	// If the Y velocity is <= 30, then the projectile movement manager will ignore it.
	// So, I'm going to set the default value to be 31.

void LaunchLoot(Entity* e)
{
	MovementRequester *mr;
	Vec3 vecVelocity;
	RegionRules *pRules = getRegionRulesFromEnt(e);
	F32 gravMul = pRules ? pRules->fGravityMulti : 1;
	F32 launchMulHeight = pRules ? pRules->fLaunchMultiHeight : 1;
	F32 launchMulDistance = pRules ? pRules->fLaunchMultiDistance : 1;

	vecVelocity[0] = (randomF32() * LAUNCH_VEL * launchMulDistance) * gravMul;

	// NOTE: If this is <= 30, then the projectile movement manager will ignore it. :(
	vecVelocity[1] = LAUNCH_VEL * launchMulHeight * gravMul;

	vecVelocity[2] = (randomF32() * LAUNCH_VEL * launchMulDistance) * gravMul;

	mmRequesterCreateBasicByName(e->mm.movement,&mr,"ProjectileMovement");
	mrProjectileStartWithVelocity(mr,e,vecVelocity,mmGetProcessCountAfterSecondsFG(0), false, true, 1.f, true);

	mrSurfaceSetGravity(e->mm.mrSurface, (-28+randomIntRange(0,5)) * gravMul);
	mrSurfaceSetFriction(e->mm.mrSurface, 0.5);
}

void TinyLaunchLoot(Entity* e)
{
	MovementRequester *mr;
	Vec3 vecVelocity;
	RegionRules *pRules = getRegionRulesFromEnt(e);
	F32 gravMul = pRules ? pRules->fGravityMulti : 1;


	vecVelocity[0] = 0;
	vecVelocity[1] = 3 * gravMul;
	vecVelocity[2] = 0;

	mmRequesterCreateBasicByName(e->mm.movement,&mr,"ProjectileMovement");
	mrProjectileStartWithVelocity(mr,e,vecVelocity,mmGetProcessCountAfterSecondsFG(0), false, true, 1.f, true);

	{
		F32 fGravity = -28;

		if(pRules)
			fGravity *= gravMul;

		mrSurfaceSetGravity(e->mm.mrSurface, fGravity );
		mrSurfaceSetFriction(e->mm.mrSurface, 0.5);
	}
}

static bool reward_ShouldUseCostumes(RewardPickupType pickupType,RewardContext * pContext)
{
	return (pickupType == kRewardPickupType_Interact || pickupType == kRewardPickupType_Rollover) &&
		(pContext->pKilled == NULL || !gConf.bKeepLootsOnCorpses);
}

// Get the pickup type from the reward table and override
RewardPickupType reward_GetPickupType(const RewardTable *pRewardTable, const RewardTableOverride *pOverride, const RewardContext* pContext)
{
	RewardPickupType pickupType = kRewardPickupType_Interact;

	// check for over flag pickuptype
	if(pOverride && (pOverride->flags & kRewardFlag_PickupTypeFromThisTable) != 0)
	{
		pickupType = pOverride->PickupType;
	}
	else if(pRewardTable)
	{
		pickupType = pRewardTable->PickupType;
	}

	if (pickupType == kRewardPickupType_FromOrigin)
	{
		return reward_GetPickupTypeFromRewardOrigin(pRewardTable, pContext->type);
	}

	return pickupType;
}

// Set the overrides from passed in overrides and the rewardtable
void reward_InitRewardTableOverride(RewardTableOverride *pOutOverride, RewardTableOverride *pInOverride, RewardTable *pRewardTable)
{
	if(!pOutOverride)
	{
		// no out table
		return;
	}

	// init the struct
	StructInit(parse_RewardTableOverride, pOutOverride);
	
	// check for over flag pickuptype
	if(pInOverride && (pInOverride->flags & kRewardFlag_PickupTypeFromThisTable) != 0)
	{
		pOutOverride->flags |= kRewardFlag_PickupTypeFromThisTable;
		pOutOverride->PickupType = pInOverride->PickupType;
	}
	else if(pRewardTable && (pRewardTable->flags & kRewardFlag_PickupTypeFromThisTable) != 0)
	{
		pOutOverride->flags |= kRewardFlag_PickupTypeFromThisTable;
		pOutOverride->PickupType = pRewardTable->PickupType;
	}
	
}

// Fills the reward context with tags from the reward table that exists in the reward entry
static void reward_PushRewardTagsToContextFromRewardTable(RewardContext *pContext, RewardTable *pRewardTable, S32 **peaiTagsPushed)
{
	if (pContext && pRewardTable && peaiTagsPushed)
	{
		S32 i;
		for (i = 0; i < eaiSize(&pRewardTable->piRewardTags); i++)
		{
			// See if this already exists
			if (eaiFind(&pContext->piRewardTags, pRewardTable->piRewardTags[i]) == -1)
			{
				eaiPush(&pContext->piRewardTags, pRewardTable->piRewardTags[i]);
				eaiPush(peaiTagsPushed, pRewardTable->piRewardTags[i]);
			}
		}
	}
}

// Pops all tags in the given array from the context
static void reward_PopTagsFromContext(RewardContext *pContext, S32 **peaiTagsPushed)
{
	if (pContext && peaiTagsPushed)
	{
		S32 i;
		for (i = 0; i < eaiSize(peaiTagsPushed); i++)
		{
			eaiFindAndRemove(&pContext->piRewardTags, (*peaiTagsPushed)[i]);
		}
	}
}

bool reward_InRange(RewardEntry *pRewardEntry, int value)
{
	if (value >= pRewardEntry->MinLevel &&
		(!pRewardEntry->MaxLevel || value <= pRewardEntry->MaxLevel))
	{
		return true;
	}
	return false;
}

// this is garbage
bool reward_RangeTable(RewardTable *pRewardTable)
{
	int NumRewardEntries = eaSize(&pRewardTable->ppRewardEntry);
	int ii;
	bool FoundRangeEntry = false;
	bool FoundOtherEntry = false;

	//update all reward entries
	for(ii=0; ii<NumRewardEntries; ii++)
	{
		RewardEntry *pRewardEntry = (RewardEntry*)pRewardTable->ppRewardEntry[ii];


		switch (pRewardEntry->ChoiceType)
		{
		case kRewardChoiceType_LevelRange:
		case kRewardChoiceType_SkillRange:
		case kRewardChoiceType_EPRange:
		case kRewardChoiceType_TimeRange:
			FoundRangeEntry = true;
			break;

		default:
			FoundOtherEntry = true;
			break;
		}
	}

	if ( FoundRangeEntry && !FoundOtherEntry )
	{
		return true;
	}

	return false;
}

void reward_GetRangeTables(RewardTable *pRewardTable, int value, RewardTable ***peaTablesOut)
{
	int ii, NumRewardEntries = eaSize(&pRewardTable->ppRewardEntry);

	//update all reward entries
	for(ii=0; ii<NumRewardEntries; ii++)
	{
		RewardEntry *pRewardEntry = (RewardEntry*)pRewardTable->ppRewardEntry[ii];

		if ( ( (pRewardEntry->ChoiceType == kRewardChoiceType_LevelRange) ||
			   (pRewardEntry->ChoiceType == kRewardChoiceType_SkillRange) ||
			   (pRewardEntry->ChoiceType == kRewardChoiceType_EPRange) ||
			   (pRewardEntry->ChoiceType == kRewardChoiceType_TimeRange) )
			 &&
			 (reward_InRange(pRewardEntry, value)) )
		{
			RewardTable *table = GET_REF(pRewardEntry->hRewardTable);
			eaPushIfNotNull(peaTablesOut,table);
		}
	}
}

RewardTable *reward_GetRangeTableEx(RewardTable *pRewardTable, int value, U32 *pSeed)
{
	int i, iSize;
	RewardTable *pTable = NULL;
	RewardTable **eaTables = NULL;
	reward_GetRangeTables(pRewardTable, value, &eaTables);
	iSize = eaSize(&eaTables);
	if (iSize <= 1)
		i = 0;
	else
		i = randomIntRangeSeeded(pSeed, RandType_LCG, 0, iSize-1);
	pTable = eaGet(&eaTables, i);	
	eaDestroy(&eaTables);
	return pTable;
}

int reward_GetRangeTableType(RewardTable *pRewardTable)
{
	int NumRewardEntries = eaSize(&pRewardTable->ppRewardEntry);

	if (NumRewardEntries > 0)
	{
		switch (pRewardTable->ppRewardEntry[0]->ChoiceType)
		{
		case kRewardChoiceType_LevelRange:
		case kRewardChoiceType_SkillRange:
		case kRewardChoiceType_EPRange:
		case kRewardChoiceType_TimeRange:
			return pRewardTable->ppRewardEntry[0]->ChoiceType;
		}
	}

	return kRewardChoiceType_None;
}

void rewardTableExpr_EvalNonPlayer(int iPartitionIdx, Expression *pExpr, RewardContext *pContext, MultiVal * pReturn)
{
	if (pExpr) {
		if (g_pRewardNonPlayerContext) {
			bool bValid = false;

			exprContextSetPointerVar(g_pRewardNonPlayerContext, "Reward", pContext, parse_RewardContext, false, true);
			exprContextSetIntVar(g_pRewardNonPlayerContext, "iLevel", pContext->RewardLevel);
			if (pContext->pTeamCredit)
			{
				exprContextSetPointerVar(g_pRewardNonPlayerContext,"TeamCredit", pContext->pTeamCredit, parse_KillCreditTeam, true, true);
			}

			if (pContext->pKilled && pContext->pKilled->pChar)
			{
				exprContextSetPointerVarPooledCached(g_pRewardNonPlayerContext,s_pchKilled,pContext->pKilled->pChar,parse_Character,true,false,&s_hKilled);
				exprContextSetUserPtr(g_pRewardNonPlayerContext, pContext->pKilled, parse_Entity);
			}
			exprContextSetPartition(g_pRewardNonPlayerContext, iPartitionIdx);
			exprEvaluate(pExpr, g_pRewardNonPlayerContext, pReturn);

			exprContextSetPointerVar(g_pRewardNonPlayerContext, "Reward", NULL, parse_RewardContext, false, true);
			exprContextSetPointerVarPooledCached(g_pRewardNonPlayerContext,s_pchKilled,NULL,parse_Character,true,false,&s_hKilled);
			exprContextSetPointerVar(g_pRewardNonPlayerContext,"TeamCredit", NULL, parse_KillCreditTeam, true, true);
			exprContextSetUserPtr(g_pRewardNonPlayerContext, NULL, parse_Entity);
		}
	}
}


void rewardTableExpr_EvalPlayer(int iPartitionIdx, Entity *pPlayerEnt, Expression *pExpr, RewardContext *pContext, MultiVal * pReturn)
{
	if (pExpr) {
		if (g_pRewardPlayerContext) {
			bool bValid = false;

			if (!pPlayerEnt) {
				Errorf("Attempting to evaluate reward expression '%s' that requires a player without a valid player.  May generate incorrect results.", exprGetCompleteString(pExpr));
			}
			exprContextSetPointerVar(g_pRewardPlayerContext, "Reward", pContext, parse_RewardContext, false, true);
			if (pContext->pTeamCredit)
			{
				exprContextSetPointerVar(g_pRewardNonPlayerContext,"TeamCredit", pContext->pTeamCredit, parse_KillCreditTeam, true, true);
			}
			if (pContext->pKilled && pContext->pKilled->pChar)
			{
				exprContextSetPointerVarPooledCached(g_pRewardPlayerContext,s_pchKilled,pContext->pKilled->pChar,parse_Character,true,false,&s_hKilled);
				exprContextSetUserPtr(g_pRewardPlayerContext, pContext->pKilled, parse_Entity);
			}
			exprContextSetPartition(g_pRewardPlayerContext, iPartitionIdx);
			exprContextSetPointerVarPooled(g_pRewardPlayerContext, "Player", pPlayerEnt, parse_Entity, true, true);
			exprContextSetIntVar(g_pRewardPlayerContext, "iLevel", pContext->RewardLevel);
			exprContextSetSelfPtr(g_pRewardPlayerContext,pPlayerEnt);
			exprEvaluate(pExpr, g_pRewardPlayerContext, pReturn);

			exprContextSetPointerVar(g_pRewardPlayerContext, "Reward", NULL, parse_RewardContext, false, true);
			exprContextSetPointerVarPooledCached(g_pRewardPlayerContext,s_pchKilled,NULL,parse_Character,true,false,&s_hKilled);
			exprContextSetPointerVar(g_pRewardPlayerContext,"TeamCredit", NULL, parse_KillCreditTeam, true, true);
			exprContextSetUserPtr(g_pRewardPlayerContext, NULL, parse_Entity);

		}
	}
}

int rewardTableExpr_GetIntResult(MultiVal * pResult, const char *pcFilename, Expression *pExpr)
{
	bool bValid;
	int iResult = MultiValGetInt(pResult, &bValid);

	if (!bValid) {
		if (pResult->type == MULTI_INVALID) {
			ErrorFilenamef(pcFilename, "Error executing reward expression '%s':\n%s", pResult->str, exprGetCompleteString(pExpr));
		} else {
			ErrorFilenamef(pcFilename, "Reward expression returned incorrect data type:\n%s", exprGetCompleteString(pExpr));
		}
	}

	return iResult;
}

static bool reward_PlayerHasGamePermissionTokens(Entity* pPlayerEnt, RewardContext* pContext, const char** ppchTokens)
{
	int i;
	for (i = eaSize(&ppchTokens)-1; i >= 0; i--)
	{
		if (pPlayerEnt)
		{
			if (!GamePermission_EntHasToken(pPlayerEnt, ppchTokens[i]))
			{
				return false;
			}
		}
		else if (pContext && pContext->pExtract)
		{
			if (!GamePermission_ExtractHasToken(pContext->pExtract, ppchTokens[i], true))
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}

static bool	reward_EntryIsValid(int iPartitionIdx, Entity *pPlayerEnt, RewardContext *ctxt, RewardEntry *pRewardEntry, bool bCheckExp, bool bUsePlayerLevel)
{
	// If the player doesn't have all of the required permissions, skip this entry
	if(!reward_PlayerHasGamePermissionTokens(pPlayerEnt, ctxt, pRewardEntry->ppchRequiredGamePermissionTokens))
	{
		return false;
	}

	//check activities
	if(pRewardEntry->pchActivityName && pRewardEntry->pchActivityName[0] && !gslActivity_IsActive(pRewardEntry->pchActivityName))
	{
		return false;
	}

	if(pRewardEntry->pchShardVariable && pRewardEntry->pchShardVariable[0])
	{
		ShardVariable *pShardVar;
		bool bFound = false;

		pShardVar = shardvariable_GetByName(pRewardEntry->pchShardVariable);
		if (pShardVar) {
			MultiVal val = {0};
			worldVariableToMultival(NULL, pShardVar->pVariable, &val);

			bFound = !!MultiValGetInt(&val,NULL);
		}

		if(bFound == false)
			return false;
	}

	if(bCheckExp && pRewardEntry->ChoiceType == kRewardChoiceType_Expression || pRewardEntry->ChoiceType == kRewardChoiceType_ExpressionInclude)
	{
		MultiVal result = {0};
		char const * pcFilename = NULL;
		RewardTable * pTable = GET_REF(pRewardEntry->hRewardTable);
		if (pTable)
		{
			pcFilename = pTable->pchFileName;
		}
		rewardTableExpr_EvalPlayer(iPartitionIdx, pPlayerEnt, pRewardEntry->pRequiresExpr, ctxt, &result);

		if (rewardTableExpr_GetIntResult(&result,pcFilename,pRewardEntry->pCountExpr) == 0)
		{
			return false;
		}
	}

	if (g_RewardConfig.bCheckMinMaxLevelForAllEntries)
	{
		int iLevel = (bUsePlayerLevel && ctxt->killerIsPlayer) ? ctxt->KillerLevel : ctxt->RewardLevel;
		if (pRewardEntry->MinLevel > ctxt->RewardLevel ||
			(pRewardEntry->MaxLevel > 0 && pRewardEntry->MaxLevel < ctxt->RewardLevel))
			return false;
	}

	if (pRewardEntry->ChoiceType == kRewardChoiceType_Disabled)
		return false;

	return true;
}

void reward_CalcWeightTotal(int iPartitionIdx, Entity *pPlayerEnt, RewardTable *pRewardTable, RewardContext *pContext, F32 *pTotal, int depth, bool bUseAllNoCharacter)
{
	int NumRewardEntries;
	int ii;


	//validate parameters
	if  ( !pRewardTable ||
		  !pTotal )
	{
		return;
	}

	depth++;
	if (depth > MAX_RECURSE_DEPTH)
	{
		Errorf("This reward table appears to be in a recursive loop: %s", pRewardTable->pchName );
		return;
	}

	//just pass through range tables (if bUseAll is in effect then this has no meaning)
	if ( reward_RangeTable(pRewardTable))
	{
		RewardTable *pRangeRewardTable = NULL;

		if(bUseAllNoCharacter)
		{
			// no character so just return
			return;
		}

		switch ( reward_GetRangeTableType(pRewardTable) )
		{
		case kRewardChoiceType_LevelRange:
			pRangeRewardTable = reward_GetRangeTable(pRewardTable, pContext->RewardLevel);
			break;
		case kRewardChoiceType_SkillRange:
			pRangeRewardTable = reward_GetRangeTable(pRewardTable, pContext->SkillLevel);
			break;
		case kRewardChoiceType_EPRange:
			pRangeRewardTable = reward_GetRangeTable(pRewardTable, pContext->EP);
			break;
		case kRewardChoiceType_TimeRange:
			pRangeRewardTable = reward_GetRangeTable(pRewardTable, pContext->TimeLevel);
			break;
		}

		//nothing in this range, just return
		if ( !pRangeRewardTable )
			return;

		//recurse into range table
		reward_CalcWeightTotal(iPartitionIdx, pPlayerEnt, pRangeRewardTable, pContext, pTotal, depth, false);
		return;
	}

	//only weighted tabled count towards total
	if ( pRewardTable->Algorithm != kRewardAlgorithm_Weighted )
		return;

	//loop for all entries in the table
	NumRewardEntries = eaSize(&pRewardTable->ppRewardEntry);
	for(ii=0; ii<NumRewardEntries; ii++)
	{
		RewardEntry *pRewardEntry = (RewardEntry*)pRewardTable->ppRewardEntry[ii];

		if (!bUseAllNoCharacter && !reward_EntryIsValid(iPartitionIdx, pPlayerEnt, pContext, pRewardEntry, false, (pRewardTable->flags & kRewardFlag_UsePlayerLevel)))
			continue;

		switch ( pRewardEntry->ChoiceType )
		{
		case kRewardChoiceType_Choice:
		case kRewardChoiceType_ChoiceVariableCount:
		case kRewardChoiceType_Empty:
		case kRewardChoiceType_AlgoBase:
		case kRewardChoiceType_AlgoChar:
		case kRewardChoiceType_AlgoCost:
		case kRewardChoiceType_CharacterBasedInclude:
			*pTotal += pRewardEntry->fWeight;
			break;

		case kRewardChoiceType_Expression:
			{
				MultiVal result = {0};

				if(bUseAllNoCharacter)
				{
					return;
				}
				else
				{
					rewardTableExpr_EvalPlayer(iPartitionIdx, pPlayerEnt, pRewardEntry->pRequiresExpr, pContext, &result);
					if (rewardTableExpr_GetIntResult(&result,pRewardTable->pchFileName,pRewardEntry->pRequiresExpr))
						*pTotal += pRewardEntry->fWeight;
				}
				break;
			}break;
		case kRewardChoiceType_ExpressionInclude:
			{
				MultiVal result = {0};
				if(bUseAllNoCharacter)
				{
					return;
				}
				else
				{
					rewardTableExpr_EvalPlayer(iPartitionIdx, pPlayerEnt, pRewardEntry->pRequiresExpr, pContext,&result);
					if (!rewardTableExpr_GetIntResult(&result,pRewardTable->pchFileName,pRewardEntry->pRequiresExpr))
						break;
				}
			}
			// fall thru
		case kRewardChoiceType_Include:
			{
				//recurse into included tables
				RewardTable *nested_table = GET_REF(pRewardEntry->hRewardTable);

				if (nested_table)
					reward_CalcWeightTotal(iPartitionIdx, pPlayerEnt, nested_table, pContext, pTotal, depth, bUseAllNoCharacter);
			}
			break;

		case kRewardChoiceType_Disabled:
			//does nothing
			break;

		case kRewardChoiceType_Count:
		default:
			break;
		}
		STATIC_INFUNC_ASSERT(kRewardChoiceType_Count == 16);
	}
}



// get the adjustment offset for the team, this is the bonus/penalty for team combat level vs target killed
// now correctly takes into account missions
S32 GetLevelAdjustment(RewardContext *pContext, bool bTeam)
{
	S32 iAdjustment = REWARDVALTABLE_ADJUST_ZERO_IDX;

	if(pContext)
	{
		S32 iLevel = pContext->iTeamCombatLevel;
		if(!bTeam || iLevel < 1)
		{
			// no team level set, use individual level
			iLevel = pContext->iKillerCombatLevel;
			if(iLevel < 1)
			{
				iLevel = pContext->KillerLevel;
			}
		}
		
		if(pContext->pKilled && pContext->pKilled->pChar)
		{
			iAdjustment = CLAMP( ((entity_GetCombatLevel(pContext->pKilled) - iLevel) + REWARDVALTABLE_ADJUST_ZERO_IDX), REWARDVALTABLE_ADJUST_MIN_IDX, REWARDVALTABLE_ADJUST_MAX_IDX);
		}
		else
		{
			// missions don't have pKilled, only a killed level
			iAdjustment = CLAMP( ((pContext->RewardLevel - iLevel) + REWARDVALTABLE_ADJUST_ZERO_IDX), REWARDVALTABLE_ADJUST_MIN_IDX, REWARDVALTABLE_ADJUST_MAX_IDX);
		}
		
	}

	return iAdjustment;
}

static F32 RecruitBonus(RewardContext *pContext)
{
	F32 fRet = 1.0f;

	if(pContext->eRecruitTypes & kRecruitType_RecruitOrRecruiter)
	{
		if(pContext->eRecruitTypes & kRecruitType_New)
			fRet = g_RewardConfig.RecruitMods.fNewRecruitOrRecruiter;
		else
			fRet = g_RewardConfig.RecruitMods.fRecruitOrRecruiter;
	}

	return fRet;
}

static MissionModsScalingData* MissionScalingDataFromType(MissionCreditType eType)
{
	int i;
	for (i = 0; i < eaSize(&g_RewardConfig.MissionMods.eaScalingData); i++){
		if (eType == g_RewardConfig.MissionMods.eaScalingData[i]->eType){
			return g_RewardConfig.MissionMods.eaScalingData[i];
		}
	}
	return NULL;
}

F32 GetLifetimeNumericBonus(const RewardContext *pContext, const char *pcNumericType)
{
	S32 i, j;
	F32 bonus = 1.0f;
	
	if(!pContext->killerIsPlayer || !pcNumericType)
	{
		return bonus;
	}
	
	for(i = 0; i < eaSize(&g_RewardConfig.eaLifetimeRewardsList) ;++i)
	{
		if(eaSize(&g_RewardConfig.eaLifetimeRewardsList[i]->eaLifetimeReward) > 0 && stricmp(g_RewardConfig.eaLifetimeRewardsList[i]->eaLifetimeReward[0]->pchLifetimeRewardType, pcNumericType) == 0)	
		{
			for(j = 0; j < eaSize(&g_RewardConfig.eaLifetimeRewardsList[i]->eaLifetimeReward); ++j)
			{
				LifetimeRewardStruct *plifetime = g_RewardConfig.eaLifetimeRewardsList[i]->eaLifetimeReward[j];
				if(pContext->iKillerSubscriptionDays >= (U32)plifetime->iLifetimeRequiredDays &&
					pContext->KillerLevel >= plifetime->iLifetimeRewardLowLevel && pContext->KillerLevel <= plifetime->iLifetimeRewardHighLevel &&
					bonus < plifetime->fLifetimeRewardModifier)
				{
					bonus = plifetime->fLifetimeRewardModifier;
				}
			}
			
			break;
		}
	}
	
	return bonus;
}

static F32 GetModifierNumericBonus(RewardContext *pContext, const char *pcNumericType)
{
	F32 bonus = 1.0f;
	RewardModifier *pModifier = eaIndexedGetUsingString(&pContext->eaRewardMods, pcNumericType);
	if(pModifier)
		bonus = pModifier->fFactor;

	return bonus;
}

F32 GetTotalNumericBonus(RewardContext *pContext, U32 rewardBonusType, const char *pcNumericType)
{
	F32 value = 1.0f;

	if((rewardBonusType & RewardBonusType_Recruit) != 0)
	{
		F32 recruit = RecruitBonus(pContext);
		value += recruit - 1.0f;
	}
	
	if((rewardBonusType & RewardBonusType_Lifetime) != 0)
	{
		F32 lifeTime = GetLifetimeNumericBonus(pContext, pcNumericType);
		value += lifeTime - 1.0f;
	}

	if((rewardBonusType & RewardBonusType_Modifier) != 0)
	{
		F32 modifier = GetModifierNumericBonus(pContext, pcNumericType);
		value += modifier - 1.0f;
	}
	
	if(value < 0.0f)
	{
		value = 0.0f;
	}
	
	return value;
}

S32 GetGenericNumericRewardIndex(const char *pcNumericName)
{
	return eaIndexedFindUsingString(&g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics, pcNumericName);
}

static F32 reward_GetScaledNumericFromXPTable(RewardValTable* theTable, S32 iLevel, S32 iAdjustmentIndex)
{
	iAdjustmentIndex = CLAMP(iAdjustmentIndex, REWARDVALTABLE_ADJUST_MIN_IDX, REWARDVALTABLE_ADJUST_MAX_IDX);
	
	if (iAdjustmentIndex >= eafSize(&theTable->Adj))
		Errorf("XPTable \"%s\" doesn't have enough adjustment values, multiplying by 1 instead. Check XPTables.data.", theTable->Name);
	else
	{
		return theTable->Val[iLevel] * theTable->Adj[iAdjustmentIndex];
	}
	return theTable->Val[iLevel];
}

int GetGenericNumericReward(RewardContext *pContext, RewardValTable* pValTableOverride, const S32 iNumericIndex, bool bScaleRewards)
{
	int value = 0;

	if ( pContext )
	{
		switch (pContext->type)
		{
		case RewardContextType_EntKill:
			if ( pContext->killerIsPlayer)
			{
				F32 tmpval;
				//todo: actual strings
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = rewardvaltable_Lookup(pContext->pcRank, pContext->pcSubRank,
						g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->eRewardGenericKillType);
				}
				if (theTable)
				{
					// Get the current map description
					MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;

					tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, true));

					//modify by the team size bonus
					if (pContext->iTeamRealSize > 1 && !zmapInfoGetMapIgnoreTeamSizeBonusXP(pMapDescription->pZoneMapInfo))
					{
						tmpval *= g_RewardConfig.TeamMods.TeamMods[pContext->TeamSize-1];
					}

					tmpval *= GetTotalNumericBonus(pContext, g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->eRewardGenericBonus,
						g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->pcRewardGenericName);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_OpenMission:  // TODO - Maybe Open Missions should have a separate table
		case RewardContextType_SubMissionTurnIn:
		case RewardContextType_MissionReward:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, 
						g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->pcRewardGenericMission);
				}
				if (theTable)
				{
					F32 tmpval = theTable->Val[USER_TO_TAB_LEVEL(pContext->RewardLevel)];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					// Apply scaling for Secondary missions
					if (pContext->eMissionCreditType != MissionCreditType_Primary){
						MissionModsScalingData* pScalingData = MissionScalingDataFromType(pContext->eMissionCreditType);
						tmpval *= pScalingData?pScalingData->fXPScale:g_RewardConfig.MissionMods.fSecondaryXPScale;
					}

					tmpval *= GetTotalNumericBonus(pContext, g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->eRewardGenericBonus,
						g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->pcRewardGenericName);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_PowerExec:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict,
						g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->pcRewardGenericPower);
				}
				if (theTable)
				{
					F32 tmpval = theTable->Val[USER_TO_TAB_LEVEL(pContext->RewardLevel)];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					tmpval *= GetTotalNumericBonus(pContext, g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->eRewardGenericBonus,
						g_RewardConfig.Modifications.eaRewardExtraAlgoNumerics[iNumericIndex]->pcRewardGenericName);

					value = (int)(tmpval+.5);
				}
			}
			break;

		default:
			break;
		}
	}

	return value;
}


int GetXPreward(RewardContext *pContext, RewardValTable* pValTableOverride, const char *pcType, bool bScaleRewards)
{
	int value = 0;

	if ( pContext )
	{
		switch (pContext->type)
		{
		case RewardContextType_EntKill:
			if ( pContext->killerIsPlayer)
			{
				F32 tmpval;
				//todo: actual strings
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = rewardvaltable_Lookup(pContext->pcRank, pContext->pcSubRank, kRewardValueType_XP);
				}
				if (theTable)
				{
					// Get the current map description
					MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;

					tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, true));

					//modify by the team size bonus
					if (pContext->iTeamRealSize > 1 && !zmapInfoGetMapIgnoreTeamSizeBonusXP(pMapDescription->pZoneMapInfo))
					{
						tmpval *= g_RewardConfig.TeamMods.TeamMods[pContext->TeamSize-1];
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_OpenMission:  // TODO - Maybe Open Missions should have a separate table
		case RewardContextType_SubMissionTurnIn:
		case RewardContextType_MissionReward:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "XP_MISSION");
				}
				if (theTable)
				{
					F32 tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, false));
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;
					
					// Apply scaling for Secondary missions
					if (pContext->eMissionCreditType != MissionCreditType_Primary){
						MissionModsScalingData* pScalingData = MissionScalingDataFromType(pContext->eMissionCreditType);
						tmpval *= pScalingData?pScalingData->fXPScale:g_RewardConfig.MissionMods.fSecondaryXPScale;
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_PowerExec:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "XP_MISC");
				}
				if (theTable)
				{
					F32 tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, false));
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		default:
			break;
		}
	}
	
	return value;
}


int GetRPreward(RewardContext *pContext, RewardValTable* pValTableOverride, const char *pcType, bool bScaleRewards)
{
	int value = 0;

	if ( pContext )
	{
		switch (pContext->type)
		{
		case RewardContextType_EntKill:
			if ( pContext->killerIsPlayer)
			{
				F32 tmpval;

				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = rewardvaltable_Lookup(pContext->pcRank, pContext->pcSubRank, kRewardValueType_RP);
				}
				if (theTable)
				{
					// Get the current map description
					MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;

					tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, true));

					//modify by the team size bonus
					if (pContext->iTeamRealSize > 1 && !zmapInfoGetMapIgnoreTeamSizeBonusXP(pMapDescription->pZoneMapInfo))
					{
						tmpval *= g_RewardConfig.TeamMods.TeamMods[pContext->TeamSize-1];
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_SubMissionTurnIn:
		case RewardContextType_MissionReward:
			{
				F32 tmpval;
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = ((RewardValTable*)resGetObject(g_hRewardValTableDict, "RP_MISSION"));
				}
				if (theTable)
				{
					tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, false));

					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					// Apply scaling for Secondary missions
					if (pContext->eMissionCreditType != MissionCreditType_Primary){
						MissionModsScalingData* pScalingData = MissionScalingDataFromType(pContext->eMissionCreditType);
						tmpval *= pScalingData?pScalingData->fRPScale:g_RewardConfig.MissionMods.fSecondaryRPScale;
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_PowerExec:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "RP_MISC");
				}
				if (theTable)
				{
					F32 tmpval = theTable->Val[USER_TO_TAB_LEVEL(pContext->RewardLevel)];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		default:
			break;
		}
	}

	return value;
}

int GetStarsReward(RewardContext *pContext, RewardValTable* pValTableOverride, const char *pcType, bool bScaleRewards)
{
	int value = 0;
	if ( pContext )
	{
		switch (pContext->type)
		{
		case RewardContextType_EntKill:
			if ( pContext->killerIsPlayer)
			{
				F32 tmpval;

				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = rewardvaltable_Lookup(pContext->pcRank, pContext->pcSubRank, kRewardValueType_Star);
				}
				if (theTable)
				{
					// Get the current map description
					MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;

					tmpval = reward_GetScaledNumericFromXPTable(theTable, 0, GetLevelAdjustment(pContext, true));

					//modify by the team size bonus
					if (pContext->iTeamRealSize > 1 && !zmapInfoGetMapIgnoreTeamSizeBonusXP(pMapDescription->pZoneMapInfo))
					{
						tmpval *= g_RewardConfig.TeamMods.TeamMods[pContext->iTeamRealSize-1];
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_SubMissionTurnIn:
		case RewardContextType_MissionReward:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "STAR_MISSION");
				}
				if (theTable) {
					F32 tmpval = theTable->Val[0];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					// Apply scaling for Secondary missions
					if (pContext->eMissionCreditType != MissionCreditType_Primary){
						MissionModsScalingData* pScalingData = MissionScalingDataFromType(pContext->eMissionCreditType);
						tmpval *= pScalingData?pScalingData->fStarsScale:g_RewardConfig.MissionMods.fSecondaryStarsScale;
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_PowerExec:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "STAR_MISC");
				}
				if (theTable) {
					F32 tmpval = theTable->Val[0];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;
					
					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);
					
					value = (int)(tmpval+.5);
				}
			}
			break;

		default:
			break;
		}
	}

	return value;
}

int GetRESreward(RewardContext *pContext, RewardValTable* pValTableOverride, const char *pcType, bool bScaleRewards)
{
	int value = 0;

	if ( pContext)
	{
		switch (pContext->type)
		{
		case RewardContextType_EntKill:
			if ( pContext->killerIsPlayer)
			{
				F32 tmpval;

				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = rewardvaltable_Lookup(pContext->pcRank, pContext->pcSubRank, kRewardValueType_Res);
				}
				if (theTable)
				{
					// Get the current map description
					MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;

					tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, true));

					//modify by the team size bonus
					if (pContext->TeamSize > 1 && !zmapInfoGetMapIgnoreTeamSizeBonusXP(pMapDescription->pZoneMapInfo))
					{
						tmpval *= g_RewardConfig.TeamMods.TeamMods[pContext->iTeamRealSize-1];
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_SubMissionTurnIn:
		case RewardContextType_MissionReward:
			{
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = ((RewardValTable*)resGetObject(g_hRewardValTableDict, "RES_MISSION"));
				}
				if (theTable) {
					F32 tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, false));
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					// Apply scaling for Secondary missions
					if (pContext->eMissionCreditType != MissionCreditType_Primary){
						MissionModsScalingData* pScalingData = MissionScalingDataFromType(pContext->eMissionCreditType);
						tmpval *= pScalingData?pScalingData->fResourceScale:g_RewardConfig.MissionMods.fSecondaryResourceScale;
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_PowerExec:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "RES_MISC");
				}
				if (theTable) {
					F32 tmpval = theTable->Val[0];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;
		}
	}

	return value;
}

int GetPVPRESreward(RewardContext *pContext, RewardValTable* pValTableOverride, const char *pcType, bool bScaleRewards)
{
	int value = 0;

	if ( pContext)
	{
		switch (pContext->type)
		{
		default:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "RES_MISC_PVP");
				}
				if (theTable) {
					F32 tmpval = theTable->Val[USER_TO_TAB_LEVEL(pContext->RewardLevel)];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;
		}
	}

	return value;
}

int GetSPreward(RewardContext *pContext, RewardValTable* pValTableOverride, const char *pcType, bool bScaleRewards)
{
	int value = 0;

	if ( pContext )
	{
		switch (pContext->type)
		{
		case RewardContextType_EntKill:
			if ( pContext->killerIsPlayer)
			{
				F32 tmpval;
				//todo: actual strings
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = rewardvaltable_Lookup(pContext->pcRank, pContext->pcSubRank, kRewardValueType_SP);
				}
				if (theTable)
				{
					// Get the current map description
					MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;

					tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, true));

					//modify by the team size bonus
					if (pContext->TeamSize > 1 && !zmapInfoGetMapIgnoreTeamSizeBonusXP(pMapDescription->pZoneMapInfo))
					{
						tmpval *= g_RewardConfig.TeamMods.TeamMods[pContext->iTeamRealSize-1];
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_OpenMission:  // TODO - Maybe Open Missions should have a separate table
		case RewardContextType_SubMissionTurnIn:
		case RewardContextType_MissionReward:
			{
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = ((RewardValTable*)resGetObject(g_hRewardValTableDict, "SP_MISSION"));
				}
				if (theTable)
				{
					F32 tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, false));
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					// Apply scaling for Secondary missions
					if (pContext->eMissionCreditType != MissionCreditType_Primary){
						MissionModsScalingData* pScalingData = MissionScalingDataFromType(pContext->eMissionCreditType);
						tmpval *= pScalingData?pScalingData->fSPScale:g_RewardConfig.MissionMods.fSecondarySPScale;
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_PowerExec:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "SP_MISC");
				}
				if (theTable)
				{
					F32 tmpval = theTable->Val[USER_TO_TAB_LEVEL(pContext->RewardLevel)];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		default:
			break;
		}
	}

	return value;
}

int GetOSPreward(RewardContext *pContext, RewardValTable* pValTableOverride, const char *pcType, bool bScaleRewards)
{
	int value = 0;

	if ( pContext )
	{
		switch (pContext->type)
		{
		case RewardContextType_EntKill:
			if ( pContext->killerIsPlayer)
			{
				F32 tmpval;
				//todo: actual strings
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = rewardvaltable_Lookup(pContext->pcRank, pContext->pcSubRank, kRewardValueType_OSP);
				}
				if (theTable)
				{
					// Get the current map description
					MapDescription *pMapDescription = &gGSLState.gameServerDescription.baseMapDescription;

					tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, true));

					//modify by the team size bonus
					if (pContext->TeamSize > 1 && !zmapInfoGetMapIgnoreTeamSizeBonusXP(pMapDescription->pZoneMapInfo))
					{
						tmpval *= g_RewardConfig.TeamMods.TeamMods[pContext->iTeamRealSize-1];
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_OpenMission:  // TODO - Maybe Open Missions should have a separate table
		case RewardContextType_SubMissionTurnIn:
		case RewardContextType_MissionReward:
			{
				RewardValTable* theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = ((RewardValTable*)resGetObject(g_hRewardValTableDict, "OSP_MISSION"));
				}
				if (theTable) {
					F32 tmpval = reward_GetScaledNumericFromXPTable(theTable, USER_TO_TAB_LEVEL(pContext->RewardLevel), GetLevelAdjustment(pContext, false));
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					// Apply scaling for Secondary missions
					if (pContext->eMissionCreditType != MissionCreditType_Primary){
						MissionModsScalingData* pScalingData = MissionScalingDataFromType(pContext->eMissionCreditType);
						tmpval *= pScalingData?pScalingData->fOSPScale:g_RewardConfig.MissionMods.fSecondaryOSPScale;
					}

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		case RewardContextType_PowerExec:
			{
				RewardValTable *theTable = pValTableOverride;
				if (!theTable)
				{
					theTable = (RewardValTable*)resGetObject(g_hRewardValTableDict, "OSP_MISC");
				}
				if (theTable) {
					F32 tmpval = theTable->Val[USER_TO_TAB_LEVEL(pContext->RewardLevel)];
					tmpval *= bScaleRewards ? pContext->RewardScale : 1;

					tmpval *= GetTotalNumericBonus(pContext, (RewardBonusType_Recruit|RewardBonusType_Lifetime|RewardBonusType_Modifier), pcType);

					value = (int)(tmpval+.5);
				}
			}
			break;

		default:
			break;
		}
	}

	return value;
}

S32 reward_GetItemLevel(RewardContext *pContext)
{
	S32 iItemLevel = 1;

	if(pContext)
	{
		if(pContext->iPlayerLevelForItem > 0)
		{
			iItemLevel = pContext->iPlayerLevelForItem;
		}
		else if(pContext->iAlgoItemLevel > 0)
		{
			iItemLevel = pContext->iAlgoItemLevel;
		}
		else
		{
			iItemLevel = pContext->RewardLevel;
		}
	}

	return iItemLevel;
}



static S32 s_RewardLog = 0;

AUTO_CMD_INT(s_RewardLog, RewardLog) ACMD_SERVERONLY ACMD_ACCESSLEVEL(9);

void reward_SetPlayerOwned(InventoryBag *pRewardBag)
{
	if(pRewardBag && pRewardBag->pRewardBagInfo)
	{
		pRewardBag->pRewardBagInfo->flags |= kRewardFlag_PlayerOwned;
	}
}

bool reward_UseGenericDifficultyScaling(RewardType type)
{
	switch(type)
	{
		xcase kRewardType_ItemDifficultyScaled:
		acase kRewardType_AlgoItemDifficultyScaled:
		acase kRewardType_AlgoItemForceDifficultyScaled:
		{
			return false;
			break;
		}
	}

	return true;
}

RewardTable *reward_GetDifficultyPowerRewardTable(PlayerDifficultyIdx iPlayerDiffIdx, bool bGeneric)
{
	RewardTable *pRewardTable = NULL;
	PlayerDifficulty * pDiff = pd_GetDifficulty(iPlayerDiffIdx);
	
	if(pDiff)
	{
		char *esTableName = NULL;

		if(bGeneric)
		{
			estrPrintf(&esTableName, "Reward_Difficulty_Power_Generic_%s", pDiff->pchInternalName);		
		}
		else
		{
			estrPrintf(&esTableName, "Reward_Difficulty_Power_%s", pDiff->pchInternalName);
		}
	
		pRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, esTableName);
		if(!pRewardTable)
		{
			// removed as some difficulties will not have a reward table for extra powers
//			Errorf("Reward system: Unable to find DifficultyPowerRewardTable: %s",esTableName);
		}

		estrDestroy(&esTableName);
		
		return pRewardTable;
	}
	return pRewardTable;
}

//add Gems to this item
static void reward_AddGemsToItem(int iPartitionIdx, Entity *pPlayerEnt, NOCONST(Item)* pItem, RewardContext *pContext, RewardEntry *pRewardEntry, int *pSeed, RewardTable *pParentTable)
{
	if(pItem && pContext && pRewardEntry && GET_REF(pRewardEntry->hRewardTable))
	{
		RewardContext *pLocalContext = NULL;
		RewardTable *pGemRewardTable = GET_REF(pRewardEntry->hRewardTable);
		ItemDef *pOurItemDef;
		InventoryBag **ppRewardBags = NULL;
		S32 i,j, iGemSlots;

		if(!pGemRewardTable)
		{
			// no reward table
			return;
		}

		pOurItemDef = GET_REF(pItem->hItem);
		iGemSlots = eaSize(&pOurItemDef->ppItemGemSlots);
		if(!pOurItemDef || iGemSlots < 1)
		{
			// no gem slots
			return;
		}

		pLocalContext = StructClone(parse_RewardContext, pContext);

		if(pLocalContext == NULL)
		{
			return;
		}

		// Generate rewards, all non-gems will be tossed
		reward_generate(iPartitionIdx, pPlayerEnt, pLocalContext, pGemRewardTable, &ppRewardBags, NULL, pSeed);

		// go through reward bags and add gems to item (if they can be added)
		for(i = 0; i < eaSize(&ppRewardBags); ++i)
		{
			for(j = 0; j < eaSize(&ppRewardBags[i]->ppIndexedInventorySlots);++j)
			{
				NOCONST(Item) *pNCGItem = CONTAINER_NOCONST(Item, ppRewardBags[i]->ppIndexedInventorySlots[j]->pItem);
				if(pNCGItem)
				{
					ItemDef *pGemItemDef = GET_REF(pNCGItem->hItem);
					if(pGemItemDef && pGemItemDef->eGemType != kItemGemType_None)
					{
						// attempt to add this gem item to the item passed in
						if(inv_trh_GemItem(pItem, pNCGItem, -1))
						{
							CONTAINER_NOCONST(InventorySlot, ppRewardBags[i]->ppIndexedInventorySlots[j])->pItem = NULL;	// item was destroyed wjhen it was slotted so just NULL the pointer
						}
						else
						{
							// cant be slotted send message
							if(pParentTable)
							{
								ErrorDetailsf
								(
									"Reward table %s (%s). %s (%s) can't slot item %s (%s). Child reward table was %s (%s).",
									pParentTable->pchName, pParentTable->pchFileName, 
									pOurItemDef->pchName, pOurItemDef->pchFileName,
									pGemItemDef->pchName,pGemItemDef->pchFileName,
									pGemRewardTable->pchName, pGemRewardTable->pchFileName
								);
								Errorf("RewardTable unable to slot gem. See details.");
							}
						}
					}
				}
			}
		}

		// Destroy the local context
		StructDestroy(parse_RewardContext, pLocalContext);

		// Destroy everything generated
		eaDestroyStruct(&ppRewardBags, parse_InventoryBag);

		item_trh_FixupPowers(pItem);
	}
}

//add powers based on difficulty (from map) to this item
void reward_AddDifficultyPowers(int iPartitionIdx, Entity *pPlayerEnt, NOCONST(Item)* pItem, RewardContext *pContext, RewardEntry *pRewardEntry, int *pSeed, bool bGeneric)
{
	if(pItem && pContext && pRewardEntry && pContext->iPlayerDifficultyIdx)
	{
		// valid non-zero difficulty
		RewardContext *pLocalContext = NULL;
		RewardTable *reward_table;
		RewardEntry *algo_base = NULL;
		InventoryBag **ppRewardBags = NULL;
		NOCONST(ItemPowerDefRef) *power_def;
		int i;
		ItemDef *pOurItemDef;
		NOCONST(AlgoItemProps)* pAlgoProps = NULL;

		// get powers
		// get the table associated with this difficulty
		reward_table = reward_GetDifficultyPowerRewardTable(pContext->iPlayerDifficultyIdx, bGeneric);
		if( !reward_table )
		{
			return;
		}
		
		// only upgrades can have additional powers
		pOurItemDef = GET_REF(pItem->hItem);
		if(!pOurItemDef || pOurItemDef->eType != kItemType_Upgrade)
		{
			return;
		}
		
		pLocalContext = StructClone(parse_RewardContext, pContext);

		if (pLocalContext == NULL)
			return;

		pLocalContext->pAlgoRewards = calloc(sizeof(AlgoRewardContext),1);
		pLocalContext->type = RewardContextType_AlgoExtra;
		

		reward_generate(iPartitionIdx, pPlayerEnt, pLocalContext, reward_table, &ppRewardBags, NULL, pSeed);

		if (eaSize(&pLocalContext->pAlgoRewards->ppCostumes) == 1)
		{
			NOCONST(SpecialItemProps)* pSpecProps = NULL;
			pSpecProps = item_trh_GetOrCreateSpecialProperties(pItem);
			SET_HANDLE_FROM_REFERENT("PlayerCostume",pLocalContext->pAlgoRewards->ppCostumes[0], pSpecProps->hCostumeRef);
		}
		pAlgoProps = item_trh_GetOrCreateAlgoProperties(pItem);
		// add powers
		for (i = 0; i < eaSize(&pLocalContext->pAlgoRewards->ppExtras); i++)
		{
			ItemPowerDef *pItemPower = pLocalContext->pAlgoRewards->ppExtras[i];
			ItemDef *pItemPowerRecipeDef = SAFE_GET_REF(pItemPower, hCraftRecipe);
			power_def = StructCreateNoConst(parse_ItemPowerDefRef);        

			SET_HANDLE_FROM_REFERENT("ItemPowerDef", pItemPower, power_def->hItemPowerDef);
			power_def->iPowerGroup = pItemPowerRecipeDef ? log2_floor(pItemPowerRecipeDef->Group) : 0; // TODO: Check on this
			power_def->ScaleFactor = pRewardEntry->fRewardDifficultyPowerScaleMultiplier * reward_table->fRewardDifficultyPowerScale;
			eaPush(&pAlgoProps->ppItemPowerDefRefs, power_def);
		}
		
		// Destroy the algo-rewards on the local context (it's marked as NO_AST)
		eaDestroy(&pLocalContext->pAlgoRewards->ppBaseItems);
		eaDestroy(&pLocalContext->pAlgoRewards->ppExtras);
		eaDestroy(&pLocalContext->pAlgoRewards->ppCostumes);
		free(pLocalContext->pAlgoRewards);

		// Destroy the local context
		StructDestroy(parse_RewardContext, pLocalContext);

		eaDestroyStruct(&ppRewardBags, parse_InventoryBag);

		item_trh_FixupPowers(pItem);
		
	}
}

// Helper for creating an item from a reward context
NOCONST(Item)* rewarditem_FromCtxt(RewardContext *pContext, char const *pchDefName, U32 *pSeed)
{
	NOCONST(Item)* pItem = NULL;
	S32 iItemLevel = reward_GetItemLevel(pContext);
	bool bUseOverrideLevel = pContext->iPlayerLevelForItem > 0;
	const char* pchRank = pContext->pcRank;
	
	pItem = inv_ItemInstanceFromDefName(pchDefName,iItemLevel,iItemLevel,pchRank,NULL,NULL,bUseOverrideLevel,pSeed);
	return pItem;
}

static void rewarditem_FillRewardDataFromEntry(Item* pItem, RewardEntry* pEntry)
{
	if (pEntry && IS_HANDLE_ACTIVE(pEntry->msgBroadcastChatMessage.hMessage) || pEntry->bHideInUI)
	{
		if (!pItem->pRewardData)
			pItem->pRewardData = StructCreate(parse_ItemRewardData);
		// BroadcastChatMessage: Copy the shard chat message onto the item instance
		COPY_HANDLE(pItem->pRewardData->hBroadcastChatMessage, pEntry->msgBroadcastChatMessage.hMessage);
		// Copy the HideInUI flag from the reward entry
		pItem->pRewardData->bHideInUI = pEntry->bHideInUI;
	}
}

static bool reward_ShouldGiveItems(RewardContext *pContext, RewardTable *pRewardTable)
{
	if(pRewardTable && pContext)
	{
		if(pContext->bGiveItems || (pContext->bWouldGiveItems && (pRewardTable->flags & kRewardFlag_PlayerKillCreditAlways) != 0))
		{
			return true;
		}
	}

	return false;
	
}

static F32 reward_ContextGetPerNumericScale(RewardContext *pContext, ItemDef* pNumericItem)
{
	RewardNumericScale* pData = eaIndexedGetUsingString(&pContext->eaNumericScales, pNumericItem->pchName);
	if (pData)
	{
		return pData->fScale; 
	}
	return 1.0f;
}

static F32 GetGatedNumericMult(Entity *pPlayerEnt, RewardContext *pContext, RewardEntry *pRewardEntry, RewardGatedDataInOut *pGatedData)
{
	S32 iGatedIndex = -1;
	if(pRewardEntry->eGatedForNumeric == RewardGatedType_None)
	{
		// This entry is not gated
		return 1.0f;
	}

	// This is only true for transacted (mission) rewards and only if they are allowed
	if(g_RewardConfig.bUseRewardGatingMissions && pGatedData)
	{
		// This version doesn't use the playerent. It uses the passed in RewardGatedDataInOut struct which has the players gated values
		iGatedIndex = Reward_GetGatedIndexTransacted(pGatedData, pRewardEntry->eGatedForNumeric);
	}
	else
	{
		if(pPlayerEnt)
		{
			iGatedIndex = Reward_GetGatedIndex(pPlayerEnt, pRewardEntry->eGatedForNumeric);
		}
	}


	if(iGatedIndex > 0)
	{
		S32 iPercentChange = pRewardEntry->iGatedPercentChange * iGatedIndex;
		F32 fGatedPercent;

		if(pRewardEntry->iMaxGatedPercentChange > 0)
		{
			if(iPercentChange > 0)
			{
				iPercentChange = min(iPercentChange, pRewardEntry->iMaxGatedPercentChange);
			}
			else
			{
				iPercentChange = -min(abs(iPercentChange), pRewardEntry->iMaxGatedPercentChange);
			}
		}

		fGatedPercent = ((F32)iPercentChange) / 100.0f + 1.0f;
		if(fGatedPercent > 0.00001f)
		{
			return fGatedPercent;
		}
		else
		{
			// below zero or close to zero is zero
			return 0.0f;
		}
	}

	return 1.0f;
}



//add this reward entry
void reward_addentry(int iPartitionIdx, Entity *pPlayerEnt, RewardContext *pContext, RewardEntry *pRewardEntry, RewardTable *pRewardTable, InventoryBag ***bags, InventoryBag *pRewardBag, U32 *pSeed, bool bMissionKillReward, RewardTableOverride *pInRewardTableOverride, RewardGatedDataInOut *pGatedData)
{
	int iCount;

	//validate parameters
	if  ( !pContext ||
		  !pRewardEntry ||
		  !pRewardTable ||
		  !bags ||
		  !pRewardBag )
	{
		return;
	}

	if((pRewardTable->flags & kRewardFlag_UsePlayerLevel) != 0 && pContext->killerIsPlayer)
	{
		// Set the item level to that of the player
		pContext->iPlayerLevelForItem = pContext->KillerLevel;
	}
	else
	{
		pContext->iPlayerLevelForItem = 0;
	}

	if((pRewardTable->flags & kRewardFlag_DupForTeam) != 0 && pRewardBag->pRewardBagInfo)
	{
		// the reward info needs to be updated so to make sure it is not given to team
		pRewardBag->pRewardBagInfo->flags |= kRewardFlag_DupForTeam;
	}

	iCount = pRewardEntry->Count;
	if (rewardEntry_UsesCountExpression(pRewardEntry))
	{
		MultiVal result = {0};
		rewardTableExpr_EvalNonPlayer(iPartitionIdx, pRewardEntry->pCountExpr, pContext, &result);
		iCount = rewardTableExpr_GetIntResult(&result,pRewardTable->pchFileName,pRewardEntry->pCountExpr);
	}

	switch ( pRewardEntry->ChoiceType )
	{
	xcase kRewardChoiceType_Empty:
	acase kRewardChoiceType_Disabled:
		//no reward

	xcase kRewardChoiceType_Expression:
	acase kRewardChoiceType_Choice:
	acase kRewardChoiceType_ChoiceVariableCount:
		//reward the choice
		switch (pRewardEntry->Type)
		{
		xcase kRewardType_ItemDifficultyScaled:
			if (reward_ShouldGiveItems(pContext, pRewardTable))
			{
				ItemDef *pItemDef = GET_REF(pRewardEntry->hItemDef);
				NOCONST(Item)* pItem;
				
				if ( !pItemDef )
					break;

				pItem = rewarditem_FromCtxt(pContext, pItemDef->pchName, pSeed);
				if(pItem)
				{
					if(pContext->iPlayerDifficultyIdx && 
						(pRewardEntry->Type == kRewardType_ItemDifficultyScaled || g_RewardConfig.Modifications.bUseGenericDifficultyPowerScaling))				
					{
						reward_AddDifficultyPowers(iPartitionIdx, pPlayerEnt, pItem, pContext, pRewardEntry, pSeed, reward_UseGenericDifficultyScaling(pRewardEntry->Type));
					}
					rewarditem_FillRewardDataFromEntry(CONTAINER_RECONST(Item, pItem), pRewardEntry);
					rewardbag_AddItem(pRewardBag,CONTAINER_RECONST(Item, pItem), true);
				}
			}
		
		xcase kRewardType_Item:
		acase kRewardType_ItemWithGems:
			if (reward_ShouldGiveItems(pContext, pRewardTable))
			{
				ItemDef *pItemDef = GET_REF(pRewardEntry->hItemDef);
				NOCONST(Item)* pItem;
				if ( !pItemDef )
					break;

				pItem = rewarditem_FromCtxt(pContext, pItemDef->pchName, pSeed);
				if(pRewardEntry->Type == kRewardType_ItemWithGems)
				{
					// find and slot gems
					reward_AddGemsToItem(iPartitionIdx, pPlayerEnt, pItem, pContext, pRewardEntry, pSeed, pRewardTable);
				}
				pItem->count = max(1, iCount);

				rewarditem_FillRewardDataFromEntry(CONTAINER_RECONST(Item, pItem), pRewardEntry);
				rewardbag_AddItem(pRewardBag,CONTAINER_RECONST(Item, pItem), true);
			}

		xcase kRewardType_UnidentifiedItemWrapper:
			if (reward_ShouldGiveItems(pContext, pRewardTable))
			{
				NOCONST(Item)* pItem;
				ItemDef *pItemDef = GET_REF(pRewardEntry->hItemDef);
				ItemDef *pResultDef = GET_REF(pRewardEntry->hUnidentifiedResultDef);

				if ( !pItemDef || !pResultDef || pItemDef->eType != kItemType_UnidentifiedWrapper)
					break;

				pItem = inv_UnidentifiedWrapperFromDefName(pItemDef->pchName, pResultDef->pchName);

				if (pRewardEntry->iUnidentifiedResultLevel > 0)
				{
					item_trh_SetAlgoPropsLevel(pItem, pRewardEntry->iUnidentifiedResultLevel);
					item_trh_SetAlgoPropsMinLevel(pItem, pRewardEntry->iUnidentifiedResultLevel);
				}

				pItem->count = max(1, iCount);

				rewarditem_FillRewardDataFromEntry(CONTAINER_RECONST(Item, pItem), pRewardEntry);
				rewardbag_AddItem(pRewardBag,CONTAINER_RECONST(Item, pItem), true);
			}

		xcase kRewardType_AlgoItemDifficultyScaled:	
		acase kRewardType_AlgoItem:
		{
			if (reward_ShouldGiveItems(pContext, pRewardTable))
			{
				Item *pItem = NULL;

				pItem =	algoitem_generate(iPartitionIdx, pContext, pSeed);

				if (pItem)
				{
					if(pContext->iPlayerDifficultyIdx &&
						(pRewardEntry->Type == kRewardType_AlgoItemDifficultyScaled || g_RewardConfig.Modifications.bUseGenericDifficultyPowerScaling))				
					{
						reward_AddDifficultyPowers(iPartitionIdx, pPlayerEnt, CONTAINER_NOCONST(Item, pItem), pContext, pRewardEntry, pSeed, reward_UseGenericDifficultyScaling(pRewardEntry->Type));
					}
					rewarditem_FillRewardDataFromEntry(pItem, pRewardEntry);
					rewardbag_AddItem(pRewardBag, pItem, true);
				}
			}
			break;
		}

		xcase kRewardType_AlgoItemForceDifficultyScaled:	
		acase kRewardType_AlgoItemForce:
			if (reward_ShouldGiveItems(pContext, pRewardTable))
			{
				Item *pItem = NULL;


				pItem =	algoitem_generate_quality(iPartitionIdx, pContext, pRewardEntry->Quality, pSeed);

				if (pItem)
				{
					if(pContext->iPlayerDifficultyIdx &&
						(pRewardEntry->Type == kRewardType_AlgoItemForceDifficultyScaled || g_RewardConfig.Modifications.bUseGenericDifficultyPowerScaling))				
					{
						reward_AddDifficultyPowers(iPartitionIdx, pPlayerEnt, CONTAINER_NOCONST(Item, pItem), pContext, pRewardEntry, pSeed, reward_UseGenericDifficultyScaling(pRewardEntry->Type));
					}
					rewarditem_FillRewardDataFromEntry(pItem, pRewardEntry);
					rewardbag_AddItem(pRewardBag, pItem, true);
				}
			}
		xcase kRewardType_RewardTable:
			{
				//recurse into the included table
				RewardTableOverride newRewardOverride = {0};	// reward_InitRewardTableOverride will init correctly
				RewardTable *nested_table = GET_REF(pRewardEntry->hRewardTable);
				int count = MAX(iCount,1);

				reward_InitRewardTableOverride(&newRewardOverride, pInRewardTableOverride, pRewardTable);

				if (nested_table)
				{
					RewardContext *pNestedContext = StructClone(parse_RewardContext, pContext);
					int i;
					pNestedContext->bBaseItemsOnly |= pRewardEntry->bBaseItemsOnly;
					for(i = 0; i<count; ++i)
						reward_generateEx(iPartitionIdx, pPlayerEnt, pNestedContext, nested_table, bags, pRewardBag, pSeed, bMissionKillReward, &newRewardOverride, pGatedData);
					
					// Destroy the nested context
					StructDestroy(parse_RewardContext, pNestedContext);
				}
				
				// in case some adds stuff to this table that actually creates structs they will be destroyed correctly
				StructDeInit(parse_RewardTableOverride, &newRewardOverride);
			}

		xcase kRewardType_Numeric:
			{
				ItemDef *itemDef = GET_REF(pRewardEntry->hItemDef);
				if (itemDef)
				{
					bool bGiveItem = true;

					// See if there is a list of numerics which are not given to whole team
					if (eaFindString(&g_RewardConfig.ppchNumericsNotGivenToWholeTeam, itemDef->pchName) >= 0)
					{
						bGiveItem = pContext->bGiveItems;
					}

					if (bGiveItem)
					{
						F32 fValue = 0.f;
						NOCONST(Item)* item = CONTAINER_NOCONST(Item, item_FromDefName(itemDef->pchName));

						// Crazy one-off code to handle a divergence in convention.  Numeric uses a float called "Value", and Item uses an int called "Count".
						// As far as we can tell, all values of Value are integers, in practice, and so we are extending the "Count Expression" functionality
						// to work for Numeric as well.  [RMARR - 8/25/10]
						F32 fBaseValue = pRewardEntry->Value;
						if (rewardEntry_UsesCountExpression(pRewardEntry))
						{
							fBaseValue = (F32)iCount;
						}
					
						fValue = ( fBaseValue *
							(pRewardTable->flags & kRewardFlag_DupForTeam ? 1 : (pContext->TeamMemberPercentage) * //don't reduce numerics by teamsize if DupForTeam is set
							((pContext->pKilled && pContext->pKilled->pCritter && (itemDef->flags & kItemDefFlag_ScaleWithCritterScaling)) ? pContext->pKilled->pCritter->fNumericRewardScale : 1)) );
						item->numeric_op = pRewardEntry->numeric_op;

						if(pRewardEntry->bScaleNumeric)
						{
							fValue *= pContext->RewardScale ? pContext->RewardScale : 1.0f;
						}

						//If the numeric operation is to add
						//TODO(BH): Should this also check to see if the value is > 0?
						if(pRewardEntry->numeric_op == NumericOp_Add)
						{
							//Scale the numeric based on the reward modifiers from the combat system
							fValue *= GetTotalNumericBonus(pContext, RewardBonusType_Modifier, itemDef->pchName);
						}
						fValue *= reward_ContextGetPerNumericScale(pContext, itemDef);

						// Get gated value if any and change reward based on index
						fValue *= GetGatedNumericMult(pPlayerEnt, pContext, pRewardEntry, pGatedData);

						//Set the final value
						item->count = (S32)	fValue + (fBaseValue<0.f ?-.5f:.5f);

						if ( item->count != 0  || item->numeric_op != NumericOp_Add)
						{
							rewarditem_FillRewardDataFromEntry(CONTAINER_RECONST(Item, item), pRewardEntry);
							rewardbag_AddItem(pRewardBag, (Item*)item, true);
						}
						else
							StructDestroyNoConst(parse_Item, item);
					}
				}
			}

		xcase kRewardType_AlgoNumeric:
		acase kRewardType_AlgoNumericNoScale:
			{
				ItemDef *pItemDef = GET_REF(pRewardEntry->hItemDef);
				if (pItemDef)
				{
					if ( pItemDef->eType == kItemType_Numeric)
					{
						F32 fValue = 1.0;
						NOCONST(Item)* pItem = CONTAINER_NOCONST(Item, item_FromDefName(pItemDef->pchName));
						RewardValTable* pValTableOverride = GET_REF(pRewardEntry->hRewardValTableOverride);
						S32 iIndex = GetGenericNumericRewardIndex(pItemDef->pchName);
						bool bScaleRewards = (pRewardEntry->Type == kRewardType_AlgoNumeric);

						if(iIndex >= 0)
						{
							fValue = GetGenericNumericReward(pContext, pValTableOverride, iIndex, bScaleRewards);
						}
						else if ( pItemDef->pchName == s_pchItemName_XP )
						{
							fValue = GetXPreward(pContext, pValTableOverride, pItemDef->pchName, bScaleRewards);
						}
						else if ( pItemDef->pchName == s_pchItemName_RP )
						{
							fValue = GetRPreward(pContext, pValTableOverride, pItemDef->pchName, bScaleRewards);
						}
						else if ( pItemDef->pchName == s_pchItemName_Resources )
						{
							fValue = GetRESreward(pContext, pValTableOverride, pItemDef->pchName, bScaleRewards);
						}
						else if ( pItemDef->pchName == s_pchItemName_Pvp_Resources )
						{
							fValue = GetPVPRESreward(pContext, pValTableOverride, pItemDef->pchName, bScaleRewards);
						}
						else if ( pItemDef->pchName == s_pchItemName_Stars )
						{
							fValue = GetStarsReward(pContext, pValTableOverride, pItemDef->pchName, bScaleRewards);
						}
						else if ( pItemDef->pchName == s_pchItemName_SkillPoint )
						{
							fValue = GetSPreward(pContext, pValTableOverride, pItemDef->pchName, bScaleRewards);
						}
						else if ( pItemDef->pchName == s_pchItemName_OfficerSkillPoint)
						{
							fValue = GetOSPreward(pContext, pValTableOverride, pItemDef->pchName, bScaleRewards);
						}

						fValue *= pContext->TeamMemberPercentage;
						fValue *= reward_ContextGetPerNumericScale(pContext, pItemDef);
						fValue *= pRewardEntry->fScale;

						if (pContext->pKilled && pContext->pKilled->pCritter &&
							(pItemDef->flags & kItemDefFlag_ScaleWithCritterScaling))
							fValue *= pContext->pKilled->pCritter->fNumericRewardScale;

						// Get gated value if any and change reward based on index
						fValue *= GetGatedNumericMult(pPlayerEnt, pContext, pRewardEntry,NULL);

						// if you don't check the sign -20.0 will get cast to
						// 19, for example. it is questionable if
						// rounding towards the negative is the correct
						// behavior...
						pItem->count = (S32)(fValue+(fValue < 0.f ?-.5f:.5f));

						if ( pItem->count != 0 )
						{
							rewarditem_FillRewardDataFromEntry(CONTAINER_RECONST(Item, pItem), pRewardEntry);
							rewardbag_AddItem(pRewardBag, CONTAINER_RECONST(Item, pItem), true);
						}
						else
						{
							StructDestroyNoConstSafe(parse_Item, &pItem);
						}
					}
				}
			}
		}

	xcase kRewardChoiceType_AlgoBase:
		{
			if (pContext->type == RewardContextType_AlgoBase && pContext->pAlgoRewards)
			{
				ItemDef *pItemDef = GET_REF(pRewardEntry->hItemDef);

				if (pItemDef)
				{
					eaPush(&pContext->pAlgoRewards->ppBaseItems, pItemDef);
				}
			}
		}

	xcase kRewardChoiceType_AlgoChar:
		{
			if ((pContext->type == RewardContextType_AlgoExtra) && pContext->pAlgoRewards)
			{
				ItemPowerDef *pItemDef = GET_REF(pRewardEntry->hItemPowerDef);

				if (pItemDef)
				{
					if (pContext->iPointBuyRemaining > -1) pContext->iPointBuyRemaining -= pItemDef->iPointBuyCost;
					eaPush(&pContext->pAlgoRewards->ppExtras, pItemDef);
				}
			}
		}

	xcase kRewardChoiceType_AlgoCost:
		{
			if (pContext->type == RewardContextType_AlgoExtra && pContext->pAlgoRewards)
			{
				PlayerCostume *pItemDef = GET_REF(pRewardEntry->hCostumeDef);

				if (pItemDef)
				{
					eaPush(&pContext->pAlgoRewards->ppCostumes, pItemDef);
				}
			}
		}
	xcase kRewardChoiceType_CharacterBasedInclude:
		{
			//recurse into the included table
			RewardTable *found_table = reward_GetCharacterBasedTable(pPlayerEnt, pContext->pCBIData, pRewardEntry->pchCharacterBasedIncludePrefix, pRewardEntry->eCharacterBasedIncludeType, pSeed);

			RewardTableOverride newRewardOverride = {0};	// reward_InitRewardTableOverride will init correctly
			
			reward_InitRewardTableOverride(&newRewardOverride, pInRewardTableOverride, pRewardTable);

			if (found_table)
			{
				RewardContext *pNestedContext = StructClone(parse_RewardContext, pContext);
				pNestedContext->bBaseItemsOnly |= pRewardEntry->bBaseItemsOnly;

				reward_generateEx(iPartitionIdx, pPlayerEnt, pNestedContext, found_table, bags, pRewardBag, pSeed, bMissionKillReward, &newRewardOverride, pGatedData);
				pContext->iPointBuyRemaining = pNestedContext->iPointBuyRemaining;

				// Destroy the nested context
				StructDestroy(parse_RewardContext, pNestedContext);
			}
			
			// in case some adds stuff to this table that actually creates structs they will be destroyed correctly
			StructDeInit(parse_RewardTableOverride, &newRewardOverride);
		}

	xcase kRewardChoiceType_ExpressionInclude:
	acase kRewardChoiceType_Include:
		{
			//recurse into the included table
			RewardTable *nested_table = GET_REF(pRewardEntry->hRewardTable);
			RewardTableOverride newRewardOverride = {0};	// reward_InitRewardTableOverride will init correctly
			
			reward_InitRewardTableOverride(&newRewardOverride, pInRewardTableOverride, pRewardTable);

			if (nested_table)
			{
				RewardContext *pNestedContext = StructClone(parse_RewardContext, pContext);
				pNestedContext->bBaseItemsOnly |= pRewardEntry->bBaseItemsOnly;

				reward_generateEx(iPartitionIdx, pPlayerEnt, pNestedContext, nested_table, bags, pRewardBag, pSeed, bMissionKillReward, &newRewardOverride, pGatedData);
				pContext->iPointBuyRemaining = pNestedContext->iPointBuyRemaining;

				// Destroy the nested context
				StructDestroy(parse_RewardContext, pNestedContext);
			}
			
			// in case some adds stuff to this table that actually creates structs they will be destroyed correctly
			StructDeInit(parse_RewardTableOverride, &newRewardOverride);
		}
	}
}

//make sure that bag 0 exists  (this is the generic bag)
//generic bag is an interact drop, for the player only, with the default NotYour and Yours costume
InventoryBag * rewardbag_CreateEx(const char* pchRewardTable)
{
	InventoryBag *tmp_bag = StructCreate(parse_InventoryBag);
	RewardBagInfo*pRewardBagInfo = StructCreate(parse_RewardBagInfo);
	NOCONST(InventoryBag)*pncTempRewardBag = CONTAINER_NOCONST(InventoryBag, tmp_bag);

	SET_HANDLE_FROM_STRING(g_hDefaultInventoryDict,"Loot",tmp_bag->inv_def);
	DECONST(InvBagIDs,tmp_bag->BagID) = InvBagIDs_Loot;

	pncTempRewardBag->pRewardBagInfo = pRewardBagInfo;

	pRewardBagInfo->ExecuteType = kRewardExecuteType_None;
	pRewardBagInfo->PickupType = kRewardPickupType_Interact;
	pRewardBagInfo->OwnerType = kRewardOwnerType_Player;
	pRewardBagInfo->LaunchType = kRRewardLaunchType_Drop;
	pRewardBagInfo->loot_mode = LootMode_FreeForAll;
	pRewardBagInfo->eLootModeThreshold = eaSize(&g_ItemQualities.ppQualities);

	SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, "NotYourVisibleDrop", pRewardBagInfo->hNotYoursCostumeRef);
	SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, "YourVisibleDrop", pRewardBagInfo->hYoursCostumeRef);

	pRewardBagInfo->LingerTime = LOOT_CRITTER_LINGER;
	pRewardBagInfo->pcRewardTable = allocAddString(pchRewardTable);

	return tmp_bag;
}

//make sure that bag 0 exists  (this is the generic bag)
//generic bag is an interact drop, for the player only, with the default NotYour and Yours costume
void reward_create_default_bag(const char* pchRewardTable, InventoryBag ***bags)
{
	if ( eaSize(bags) < 1 )
	{
		InventoryBag *bag = rewardbag_CreateEx(pchRewardTable);
		eaPush(bags,bag);
	}
}

// check for costume match
static bool RewardCheckBagCostumeMatch(InventoryBag *pBag, RewardContext *pContext, RewardTable *pTable)
{
	if(GET_REF(pContext->hNotYoursCostumeRef) && GET_REF(pContext->hYoursCostumeRef))
	{
		return (REF_COMPARE_HANDLES(pBag->pRewardBagInfo->hNotYoursCostumeRef,pContext->hNotYoursCostumeRef) &&
			REF_COMPARE_HANDLES(pBag->pRewardBagInfo->hYoursCostumeRef, pContext->hYoursCostumeRef));
	}

	return (REF_COMPARE_HANDLES(pBag->pRewardBagInfo->hNotYoursCostumeRef,pTable->hNotYoursCostumeRef) &&
			REF_COMPARE_HANDLES(pBag->pRewardBagInfo->hYoursCostumeRef, pTable->hYoursCostumeRef));

}

// set costume for reward bag
static void RewardSetBagCostume(NOCONST(InventoryBag) *pBag, RewardContext *pContext, RewardTable *pTable)
{
	if(GET_REF(pContext->hNotYoursCostumeRef) && GET_REF(pContext->hYoursCostumeRef))
	{
		COPY_HANDLE(pBag->pRewardBagInfo->hNotYoursCostumeRef, pContext->hNotYoursCostumeRef);
		COPY_HANDLE(pBag->pRewardBagInfo->hYoursCostumeRef, pContext->hYoursCostumeRef);
	}
	else
	{
		COPY_HANDLE(pBag->pRewardBagInfo->hNotYoursCostumeRef, pTable->hNotYoursCostumeRef);
		COPY_HANDLE(pBag->pRewardBagInfo->hYoursCostumeRef, pTable->hYoursCostumeRef);
	}
}

static InventoryBag *rewardbag_GetFromContext(RewardContext * pContext,RewardTable *table, InventoryBag ***bags,InventoryBag *pParentBag, S32 iNewBagIfNumItems, RewardTableOverride *pInRewardTableOverride)
{
	InventoryBag *pRewardBag = NULL;
	RewardPickupType pickupType;
	bool bUsingCostumes;
	//default to generic reward bag
	pRewardBag = (InventoryBag *)eaGet(bags, 0);

	// if a parent bag was passed in, default to the parent bag  (usually indicates a recursive call)
	if ( pParentBag )
		pRewardBag = pParentBag;

	pickupType = reward_GetPickupType(table, pInRewardTableOverride, pContext);
	bUsingCostumes = reward_ShouldUseCostumes(pickupType, pContext);

	//if the reward table specifies a custom bag then create it
	// also search for a new bag if the default one is kRewardFlag_SeparateBag
	if(iNewBagIfNumItems > 0 || 
		pickupType != kRewardPickupType_None || 
		(table->flags & (kRewardFlag_SeparateBag | kRewardFlag_PlayerOwned)) != 0 ||
		(pContext->iForceSoloOwnershipContainerID != -1) ||
		(pRewardBag->pRewardBagInfo && (pRewardBag->pRewardBagInfo->flags & (kRewardFlag_SeparateBag | kRewardFlag_PlayerOwned)) != 0))
	{
		int BagIdx;

		//see if there is already a bag that matches the bag you are creating
		//if there is just use that one.
		for ( BagIdx=0; BagIdx<eaSize(bags); BagIdx++)
		{
			InventoryBag *pBag = eaGet(bags,BagIdx);
			if(pBag)
			{
				S32 iNumItems = inv_bag_CountItems(pBag, NULL);

				if
				(
					(iNewBagIfNumItems == 0 || iNumItems < iNewBagIfNumItems) &&
					((table->flags & (kRewardFlag_SeparateBag | kRewardFlag_PlayerOwned)) == 0 || iNumItems == 0) &&
					 pBag->pRewardBagInfo &&
					 (!(pBag->pRewardBagInfo->flags & kRewardFlag_SeparateBag)) &&
					 (pBag->pRewardBagInfo->flags == table->flags) &&
					 (pBag->pRewardBagInfo->ExecuteType == table->ExecuteType) &&
					 (pBag->pRewardBagInfo->PickupType == pickupType) &&
					 (pBag->pRewardBagInfo->LaunchType == table->LaunchType) &&
					 (pBag->pRewardBagInfo->NumPicks == table->NumPicks) &&
					 (pBag->pRewardBagInfo->LingerTime == table->LingerTime) &&
					 (pBag->pRewardBagInfo->bShowRewardPackUI == (U32)pContext->bShowRewardPackUI) &&
					 (
						// only compare costume refs if we're going to drop into a costume
						//(pickupType != kRewardPickupType_Interact && pickupType != kRewardPickupType_Rollover) ||
						(!bUsingCostumes) || RewardCheckBagCostumeMatch(pBag, pContext, table)
					)
				)
				{
					//Either the owner type needs to match (if this bag isn't being forced into single-ownership,)
					// or this bag needs to be player-owner by a single entity that matches the passed-in ID.
					if ((pContext->iForceSoloOwnershipContainerID == -1 && pBag->pRewardBagInfo->OwnerType == table->OwnerType) || 
						(pBag->pRewardBagInfo->OwnerType == kRewardOwnerType_Player && 
						eaSize(&pBag->pRewardBagInfo->peaLootOwners) == 1 && 
						pBag->pRewardBagInfo->peaLootOwners[0]->eOwnerType == kRewardOwnerType_Player &&
						eaiSize(&pBag->pRewardBagInfo->peaLootOwners[0]->peaiOwnerIDs) == 1 &&
						pBag->pRewardBagInfo->peaLootOwners[0]->peaiOwnerIDs[0] == pContext->iForceSoloOwnershipContainerID))
					{
						pRewardBag = pBag;
					}
					break;
				}
			}
		}

		if (BagIdx >= eaSize(bags))
		{
			int iResult;
			//create a special bag for this table
			NOCONST(InventoryBag)*tmp_bag = CONTAINER_NOCONST(InventoryBag, rewardbag_CreateEx(table->pchName));

			tmp_bag->pRewardBagInfo->flags = table->flags;

			if(iNewBagIfNumItems > 0)
			{
				reward_SetPlayerOwned((InventoryBag*)tmp_bag);
			}

			tmp_bag->pRewardBagInfo->ExecuteType = table->ExecuteType;
			tmp_bag->pRewardBagInfo->PickupType = pickupType;
			if(tmp_bag->pRewardBagInfo->PickupType == kRewardPickupType_None)
			{
				// give it a default pick-up which is interact
				tmp_bag->pRewardBagInfo->PickupType = kRewardPickupType_Interact;
			}
			if (pContext->iForceSoloOwnershipContainerID == -1)
				tmp_bag->pRewardBagInfo->OwnerType = table->OwnerType;
			else
			{
				CritterLootOwner* pOwner = StructCreate(parse_CritterLootOwner);

				tmp_bag->pRewardBagInfo->OwnerType = kRewardOwnerType_Player;

				pOwner->eOwnerType = kRewardOwnerType_Player;
				eaiPush(&pOwner->peaiOwnerIDs, pContext->iForceSoloOwnershipContainerID);
				eaPush(&tmp_bag->pRewardBagInfo->peaLootOwners, pOwner);
				tmp_bag->pRewardBagInfo->flags |= kRewardFlag_DupForTeam;
			}
			tmp_bag->pRewardBagInfo->LaunchType = table->LaunchType;

			RewardSetBagCostume(tmp_bag, pContext, table);

			tmp_bag->pRewardBagInfo->NumPicks = table->NumPicks;
			tmp_bag->pRewardBagInfo->LingerTime = table->LingerTime;
			tmp_bag->pRewardBagInfo->bShowRewardPackUI = pContext->bShowRewardPackUI;

			iResult = eaPush(bags,(InventoryBag *)tmp_bag);

			if (eaIndexedGetTable_inline(bags) && !iResult)
			{
				Errorf("Couldn't create bag for reward table '%s'. Output bag array should not be indexed!", table->pchName);
				StructDestroy(parse_InventoryBag, CONTAINER_RECONST(InventoryBag, tmp_bag));
			}
			else
			{
				pRewardBag = (InventoryBag *)tmp_bag;
			}
		}
	}
	
	return pRewardBag;
}

static int reward_weightWalker(F32 *weights)
{
	F32 total = 0.f;
	F32 choice;
	int i;
	int n = eafSize(&weights);
	for(i = 0; i<n; ++i)
		total += weights[i];
	if(total<=0)
		return -1;
	choice = randomPositiveF32() * total;
	total = 0;
	for(i = 0; i<n; ++i)
	{
		total += weights[i];
		if(total >= choice)
			return i;
	}
	return -1;
}

// Set the time or increment the number of times this reward gate type has been granted
static void reward_GatedRewardGenerated(Entity *pEntity, S32 gatedType)
{
	if(pEntity && pEntity->pPlayer && gatedType > 0)
	{
		Player *pPlayer = pEntity->pPlayer;
		RewardGatedInfo *pRewardGatedInfo = eaIndexedGetUsingInt(&g_RewardConfig.eRewardGateInfo, gatedType);
		RewardGatedTypeData *pPlayerGated = eaIndexedGetUsingInt(&pEntity->pPlayer->eaRewardGatedData, gatedType);

		if(!pRewardGatedInfo)
		{
			// no such type
			Errorf("RewardGateType error, %d is not a valid rewardgatetype enum value", gatedType);
			return;
		}

		// Note that on the server that the data is reset in Reward_GetGatedIndex when the block is checked and it is not the same block
		if(pPlayerGated)
		{
			U32 tm = timeServerSecondsSince2000();
			++pPlayerGated->uNumTimes;

			// check for full reset
			if(pRewardGatedInfo->uResetAt > 0 && pPlayerGated->uNumTimes >= pRewardGatedInfo->uResetAt)
			{
				pPlayerGated->uNumTimes = 0;
				pPlayerGated->uTimeSet = tm;
			}
		}
		else
		{
			U32 tm = timeServerSecondsSince2000();
			RewardGatedTypeData *pNewGatedData = StructCreate(parse_RewardGatedTypeData);

			pNewGatedData->eType = gatedType;
			pNewGatedData->uNumTimes = 1;
			pNewGatedData->uTimeSet = tm;

			if(!pPlayer->eaRewardGatedData)
			{
				eaIndexedEnable(&pEntity->pPlayer->eaRewardGatedData, parse_RewardGatedTypeData);
			}

			eaIndexedAdd(&pPlayer->eaRewardGatedData, pNewGatedData);
		}

		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, true);

	}
}

bool reward_QualityShouldUseLootMode(ItemQuality eQuality, ItemQuality eThreshold)
{
	return (eQuality >= eThreshold && !(g_ItemQualities.ppQualities[eQuality]->flags & kItemQualityFlag_IgnoreLootThreshold));
}

//generate rewards from a reward table/Level
//rewards w/o specific bag get put into bags[0]
//other bags may be created for rewards that require their own (like rollovers)
void reward_generateEx( int iPartitionIdx, Entity *pPlayerEnt, RewardContext *ctxt, RewardTable *table, InventoryBag ***bags, InventoryBag *pParentBag, U32 *pSeed, bool bMissionKillReward, RewardTableOverride *pInRewardTableOverride, RewardGatedDataInOut *pGatedData)
{
	int n_rw_entries = 0;
	InventoryBag *pRewardBag = NULL;
	int ii;
	S32 iNumItemsForSeparateBag = 0;
	S32 *eaiRewardTagsPushed = NULL;
	bool bClearForcedOwnership = false;
	CharacterBasedIncludeContext* pOldCBIData = ctxt->pCBIData;

	//validate parameters
	if(!ctxt || !table || !bags  )
		return;

	ctxt->depth++;
	rewardtrace("generate(%i): table %s", ctxt->depth, table->pchName);
	rewardtrace_indent++;

	if (table->bShowRewardPackUI)
		ctxt->bShowRewardPackUI = true;

	if (table->eRewardPackOverallQuality >= -1)
		ctxt->eRewardPackOverallQuality = table->eRewardPackOverallQuality;

	if (ctxt->depth > MAX_RECURSE_DEPTH)
	{
		Errorf("This reward table appears to be in a recursive loop: %s", table->pchName );
		ctxt->depth--;
		rewardtrace_indent--;
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	//just pass through range tables
	if ( reward_RangeTable(table) )
	{
		RewardTable *range_table = NULL;
		RewardTable **eaRangeTables = NULL;
		RewardContext *pNestedContext = StructClone(parse_RewardContext, ctxt);
		RewardChoiceType type = reward_GetRangeTableType(table);
		RewardTableOverride newRewardOverride = {0};	// reward_InitRewardTableOverride will init correctly
		MersenneTable *pmTable = NULL;
		bool bInvalidNoRepeats = false;

		reward_InitRewardTableOverride(&newRewardOverride, pInRewardTableOverride, table);
		switch (type)
		{
			xcase kRewardChoiceType_LevelRange:
			{
				if((table->flags & kRewardFlag_UsePlayerLevel) != 0 && ctxt->killerIsPlayer)
				{
					reward_GetRangeTables(table, ctxt->KillerLevel, &eaRangeTables);
				}
				else
				{
					reward_GetRangeTables(table, ctxt->RewardLevel, &eaRangeTables);
				}
			
			}
			xcase kRewardChoiceType_SkillRange:
			{
				reward_GetRangeTables(table, ctxt->SkillLevel, &eaRangeTables);
			}
			xcase kRewardChoiceType_EPRange:
			{
				reward_GetRangeTables(table, ctxt->EP, &eaRangeTables);
			}
			xcase kRewardChoiceType_TimeRange:
			{
				reward_GetRangeTables(table, ctxt->TimeLevel, &eaRangeTables);
			}
		}
		if(pSeed)
		{
			pmTable = mersenneTableCreate(*pSeed);
			++(*pSeed);
		}

		for (ii = 0; ii < table->NumChoices; ii++)
		{
			int iRandIndex;

			if (eaSize(&eaRangeTables) <= 1)
			{
				iRandIndex = 0;
				if (table->flags & kRewardFlag_NoRepeats)
				{
					bInvalidNoRepeats = true;
				}
			}
			else
			{
				if(pmTable)
				{
					iRandIndex = randomMersenneIntRange(pmTable, 0, eaSize(&eaRangeTables)-1);
				}
				else
				{
					iRandIndex = randomIntRange(0, eaSize(&eaRangeTables)-1);
				}
			}
			if ((table->flags & kRewardFlag_NoRepeats) && eaSize(&eaRangeTables) > 1)
			{
				range_table = eaRemove(&eaRangeTables, iRandIndex);
			}
			else
			{
				range_table = eaGet(&eaRangeTables, iRandIndex);
			}

			if ( range_table )
				reward_generateEx(iPartitionIdx, pPlayerEnt, pNestedContext, range_table, bags, pParentBag, pSeed, bMissionKillReward, &newRewardOverride, pGatedData);
			else
				rewardtrace("no table found.");

		}

		ctxt->iPointBuyRemaining = pNestedContext->iPointBuyRemaining;
		ctxt->depth--;

		if (bInvalidNoRepeats)
		{
			Errorf("RewardTable %s was flagged as 'NoRepeats', but it chose all entries. This should not have happened!", table->pchName);
		}
		// Destroy the nested context
		StructDestroy(parse_RewardContext, pNestedContext);
		// DeInit anything added to the new reward table override
		StructDeInit(parse_RewardTableOverride, &newRewardOverride);
		// Destroy range table array
		eaDestroy(&eaRangeTables);
		// Destroy the mersenne table
		if(pmTable)
		{
			mersenneTableFree(pmTable);
		}
		PERFINFO_AUTO_STOP();
		rewardtrace_indent--;
		return;
	}

	n_rw_entries = eaSize(&table->ppRewardEntry);

	//make sure that bag 0 exists  (this is the generic bag)
	reward_create_default_bag(table->pchName, bags);
	
	if(ctxt->TeamSize > 1 && (bMissionKillReward || (table->flags & kRewardFlag_DupForTeam)))
	{
		if(ctxt->lootMode == LootMode_RoundRobin)
		{
			// if there are two or more items in the bag, we need to a new bag in round robin
			iNumItemsForSeparateBag = 2;		
		}
		else
		{
			// always make a new bag if there are items in the bag
			iNumItemsForSeparateBag = 1;		
		}
	}

	//if this reward is for a member of the killing team
	//and the reward is duped for all team members
	if (ctxt->bBestTeam && ctxt->bGiveItems && ctxt->iForceSoloOwnershipContainerID == -1 && (table->flags & kRewardFlag_DupForTeam) && ctxt->pTeamCredit)
	{
		//Okay. At this point, we need to duplicate the current table once for each team member.
		int iMember = 0;
		RewardContext nestedCtxt = {0};
		S32 iPlayerContainerID = -1;

		if (ctxt->pCBIData)
			iPlayerContainerID = ctxt->pCBIData->iContainerID;
		else if (pPlayerEnt)
			iPlayerContainerID = pPlayerEnt->myContainerID;

		// (n-1) members handled here, the last one is handled by the current call to reward_generate
		for (iMember = 0; iMember < eaSize(&ctxt->pTeamCredit->eaMembers); iMember++)
		{
			//skip the actual player running this reward_generate and let theirs occur naturally
			if (ctxt->pTeamCredit->eaMembers[iMember]->CBIData.iContainerID != iPlayerContainerID)
			{
				Reward_CreateOrResetRewardContext(&nestedCtxt);
				StructCopyAll(parse_RewardContext, ctxt, &nestedCtxt);
				nestedCtxt.pCBIData = &ctxt->pTeamCredit->eaMembers[iMember]->CBIData;
				nestedCtxt.iForceSoloOwnershipContainerID = ctxt->pTeamCredit->eaMembers[iMember]->iContainerID;
				reward_generateEx(iPartitionIdx, pPlayerEnt, &nestedCtxt, table, bags, pParentBag, pSeed, bMissionKillReward, pInRewardTableOverride, pGatedData);
			}
		}
		StructDeInit(parse_RewardContext, &nestedCtxt);
		ctxt->iForceSoloOwnershipContainerID = iPlayerContainerID;
		bClearForcedOwnership = true;
	}
	
	pRewardBag = rewardbag_GetFromContext(ctxt,table,bags,pParentBag,iNumItemsForSeparateBag, pInRewardTableOverride);

	if (pRewardBag->pRewardBagInfo)
		pRewardBag->pRewardBagInfo->eRewardPackOverallQuality = max(pRewardBag->pRewardBagInfo->eRewardPackOverallQuality, ctxt->eRewardPackOverallQuality);
	
	//make sure we have a valid bag
	if ( !pRewardBag )
	{
		Errorf("error creating bag" );
		goto rewardGenerate_exit;
	}

	// Push the reward tags from this reward table into the context
	reward_PushRewardTagsToContextFromRewardTable(ctxt, table, &eaiRewardTagsPushed);

	switch ( table->Algorithm )
	{
	case kRewardAlgorithm_Weighted:
		{
			F32	TotalWeight = 0;
			F32 *entry_weights = NULL;
			int iTotalValidEntries = 0;
			int iChoice;
			bool bInvalidNoRepeats = false;
			MersenneTable *pmTable = NULL;

			//calc total weight for all entries
			reward_CalcWeightTotal(iPartitionIdx, pPlayerEnt, table, ctxt, &TotalWeight, 0, false);

			// removed weight table failure. Added errorf to rewardTable_Validate for cases with multiple entries and no weight

			//calc weights for each entry
			for(ii=0; ii<n_rw_entries; ii++)
			{
				RewardEntry *entry = (RewardEntry*)table->ppRewardEntry[ii];

				if(!reward_EntryIsValid(iPartitionIdx, pPlayerEnt, ctxt, entry, false, (table->flags & kRewardFlag_UsePlayerLevel)))
				{
					eafPush(&entry_weights, 0);
					continue;
				}

				switch ( entry->ChoiceType )
				{
				case kRewardChoiceType_AlgoChar:
						eafPush(&entry_weights, entry->fWeight);
						break;
				case kRewardChoiceType_Choice:
				case kRewardChoiceType_ChoiceVariableCount:
				case kRewardChoiceType_Empty:
				case kRewardChoiceType_AlgoBase:
				case kRewardChoiceType_AlgoCost:
				case kRewardChoiceType_CharacterBasedInclude:
					eafPush(&entry_weights, entry->fWeight);
					break;

				case kRewardChoiceType_Expression:
					{
						MultiVal result = {0};
						rewardTableExpr_EvalPlayer(iPartitionIdx, pPlayerEnt, entry->pRequiresExpr, ctxt, &result);

						if (rewardTableExpr_GetIntResult(&result,table->pchFileName,entry->pRequiresExpr))
							eafPush(&entry_weights, entry->fWeight);
						else
							eafPush(&entry_weights, 0);
						break;
					}
				case kRewardChoiceType_ExpressionInclude:
					{
						if(entry->pRequiresExpr)
						{
							MultiVal result = {0};
							rewardTableExpr_EvalPlayer(iPartitionIdx, pPlayerEnt, entry->pRequiresExpr, ctxt, &result);
							if (!rewardTableExpr_GetIntResult(&result,table->pchFileName,entry->pRequiresExpr))
							{
								break;
							}
						}
					}
					// fall through
				case kRewardChoiceType_Include:
					{
						F32 EntryTotal = 0;
						RewardTable *nested_table = GET_REF(entry->hRewardTable);

						if (nested_table)
							reward_CalcWeightTotal(iPartitionIdx, pPlayerEnt, nested_table, ctxt, &EntryTotal, 0, false);

						eafPush(&entry_weights, EntryTotal);
					}
					break;

				//Handled by reward_EntryIsValid(), but what the heck.
				case kRewardChoiceType_Disabled:
					{
						eafPush(&entry_weights, 0);
					}break;

				default:
					Errorf("Bad reward choice type in reward table %s %d", table->pchName, ii );
					eafPush(&entry_weights, 0);
					break;
				}
				STATIC_INFUNC_ASSERT(kRewardChoiceType_Count == 16);// update this switch statement
			}

			if(pSeed)
			{
				pmTable = mersenneTableCreate(*pSeed);
				++(*pSeed);
			}

			// If this table is flagged as NoRepeats, calculate the number of valid entries
			if (table->flags & kRewardFlag_NoRepeats)
			{
				for(ii=0; ii<n_rw_entries; ii++)
				{
					F32 fEntryWeight = eafGet(&entry_weights,ii);
					if (fEntryWeight > 0.0f)
					{
						iTotalValidEntries++;
					}
				}
			}
			//loop for the Num Choices to be awarded
			for (iChoice=0; iChoice<table->NumChoices; iChoice++)
			{
				F32 ChoiceRoll; 
				F32 WeightWalker = 0;
				
				// If a seed is passed in use RandType_Mersenne for this and increment seed
				if(pmTable)
				{
					ChoiceRoll = randomMersennePositiveF32(pmTable) * TotalWeight;
				}
				else
				{
					ChoiceRoll = randomPositiveF32Seeded(NULL, RandType_LCG) * TotalWeight;
				}

				//update all reward entries
				for(ii=0; ii<n_rw_entries; ii++)
				{
					RewardEntry *pRewardEntry = (RewardEntry*)table->ppRewardEntry[ii];
					F32 fEntryWeight = eafGet(&entry_weights,ii);

					// CO allows zero weight tables, note that any reward table with more than one entry and not weight will errorf but still always give the first item
					if(fEntryWeight <= 0.0f)
					{
						continue;
					}

					WeightWalker += fEntryWeight;
					
					// check to see if we need a new bag, separate rewards need new bags once a bag has an item
					if((table->flags & (kRewardFlag_SeparateBag | kRewardFlag_PlayerOwned)) != 0)			
					{
						InventoryBag *pNewRewardBag = rewardbag_GetFromContext(ctxt,table,bags,pParentBag,iNumItemsForSeparateBag,pInRewardTableOverride);
						if(pNewRewardBag && pNewRewardBag != pRewardBag)
						{
							pRewardBag = pNewRewardBag;
						}
					}

					if ( ChoiceRoll <= WeightWalker )
					{
						//give this reward entry
						reward_addentry(iPartitionIdx, pPlayerEnt, ctxt, pRewardEntry, table, bags, pRewardBag, pSeed, bMissionKillReward, pInRewardTableOverride, pGatedData);
						if (table->flags & kRewardFlag_NoRepeats)
						{
							if (iTotalValidEntries > 1)
							{
								TotalWeight -= entry_weights[ii];
								entry_weights[ii] = 0.0f;
								iTotalValidEntries--;
							}
							else
							{
								bInvalidNoRepeats = true;
							}
						}
						//done with this choice
						break;
					}
				}
				if (ii>=n_rw_entries)
					Errorf("weight problem in %s. didn't find a matching entry in %i tries", table->pchName, ii);
			}
			eafDestroy(&entry_weights);
			
			if (bInvalidNoRepeats)
			{
				Errorf("RewardTable %s was flagged as 'NoRepeats', but it chose all entries. This should not have happened!", table->pchName);
			}
			if(pmTable)
			{
				mersenneTableFree(pmTable);
			}
			
		}
		break;
	case kRewardAlgorithm_GiveAll:
		//update all reward entries
		for(ii=0; ii<n_rw_entries; ii++)
		{
			RewardEntry *pRewardEntry = (RewardEntry*)table->ppRewardEntry[ii];

			if(!reward_EntryIsValid(iPartitionIdx, pPlayerEnt, ctxt, pRewardEntry, true, (table->flags & kRewardFlag_UsePlayerLevel)))
			{
				continue;
			}

			// check to see if we need a new bag, separate rewards need new bags once a bag has an item
			if((table->flags & (kRewardFlag_SeparateBag | kRewardFlag_PlayerOwned)) != 0)			
			{
				InventoryBag *pNewRewardBag = rewardbag_GetFromContext(ctxt,table,bags,pParentBag,iNumItemsForSeparateBag,pInRewardTableOverride);
				if(pNewRewardBag && pNewRewardBag != pRewardBag)
				{
					pRewardBag = pNewRewardBag;
				}
			}

			reward_addentry(iPartitionIdx, pPlayerEnt, ctxt, pRewardEntry, table, bags, pRewardBag, pSeed, bMissionKillReward, pInRewardTableOverride, pGatedData);
			
		}
		break;
	case kRewardAlgorithm_Gated:
		{
			S32 iEntry = Reward_GetGatedIndex(pPlayerEnt, table->eRewardGatedType);
			if(iEntry >= n_rw_entries)
			{
				iEntry = n_rw_entries - 1;
			}

			if(iEntry >= 0)
			{
				RewardEntry *pRewardEntry = (RewardEntry*)table->ppRewardEntry[iEntry];
				if(reward_EntryIsValid(iPartitionIdx, pPlayerEnt, ctxt, pRewardEntry, true, (table->flags & kRewardFlag_UsePlayerLevel)))
				{
					// check to see if we need a new bag, separate rewards need new bags once a bag has an item
					if((table->flags & (kRewardFlag_SeparateBag | kRewardFlag_PlayerOwned)) != 0)			
					{
						InventoryBag *pNewRewardBag = rewardbag_GetFromContext(ctxt,table,bags,pParentBag,iNumItemsForSeparateBag,pInRewardTableOverride);
						if(pNewRewardBag && pNewRewardBag != pRewardBag)
						{
							pRewardBag = pNewRewardBag;
						}
					}

					reward_addentry(iPartitionIdx, pPlayerEnt, ctxt, pRewardEntry, table, bags, pRewardBag, pSeed, bMissionKillReward, pInRewardTableOverride, pGatedData);
					// increment this gate type
					reward_GatedRewardGenerated(pPlayerEnt, table->eRewardGatedType);
				}
			}

		}
		break;
	default:
		break;
	}

	reward_PopTagsFromContext(ctxt, &eaiRewardTagsPushed);
	eaiDestroy(&eaiRewardTagsPushed);

rewardGenerate_exit:
	ctxt->depth--;
	PERFINFO_AUTO_STOP();
	rewardtrace_indent--;

	if (bClearForcedOwnership)
	{
		ctxt->iForceSoloOwnershipContainerID = -1;
	}
}


void reward_generate_specialcase(RewardContext *pContext, const char *RewardTableName, InventoryBag ***bags, InventoryBag *pParentBag )
{
	InventoryBag *pRewardBag = NULL;
	ItemDef *pItemDef = NULL;
	F32 fPercentage = 0;


	//validate parameters
	if  ( !pContext   ||
		  !RewardTableName   ||
		  !RewardTableName[0]   ||
		  !bags  )
	{
		return;
	}

	if ( strnicmp(RewardTableName, "__itemdrop_", 11) == 0 )
	{
		char * PercentageStr;
		char * ItemNameStr;

		estrStackCreate(&PercentageStr);
		estrStackCreate(&ItemNameStr);

		//get a string that is just the percentage (bracketed by 3rd and 4th underscore)
		estrPrintf(&PercentageStr, "%s", RewardTableName);
		estrRemoveUpToFirstOccurrence(&PercentageStr, '_');
		estrRemoveUpToFirstOccurrence(&PercentageStr, '_');
		estrRemoveUpToFirstOccurrence(&PercentageStr, '_');
		estrTruncateAtFirstOccurrence(&PercentageStr, '_');

		//get a string that is just the itemname, remaining after 4th underscore
		estrPrintf(&ItemNameStr, "%s", RewardTableName);
		estrRemoveUpToFirstOccurrence(&ItemNameStr, '_');
		estrRemoveUpToFirstOccurrence(&ItemNameStr, '_');
		estrRemoveUpToFirstOccurrence(&ItemNameStr, '_');
		estrRemoveUpToFirstOccurrence(&ItemNameStr, '_');
		fPercentage = atof(PercentageStr);
		fPercentage /= 100.0;

		pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,ItemNameStr);

		estrDestroy(&PercentageStr);
		estrDestroy(&ItemNameStr);
	}

	//make sure that bag 0 exists  (this is the generic bag)
	reward_create_default_bag(RewardTableName, bags);

	//default to generic reward bag
	pRewardBag = (InventoryBag *)eaGet(bags, 0);

	// if a parent bag was passed in, default to the parent bag  (usually indicates a recursive call)
	if ( pParentBag )
		pRewardBag = pParentBag;

	//make sure we have a valid bag
	if ( !pRewardBag )
	{
		Errorf("error creating bag" );
		return;
	}

	if ( (fPercentage < 0) ||
		 (fPercentage > 100) ||
		 !pItemDef )
	{
		return;
	}

	if (randomPositiveF32Seeded(NULL, RandType_LCG) <= fPercentage)
	{
		Item *item = item_FromDefName(pItemDef->pchName);
		rewardbag_AddItem(pRewardBag,item, true);
	}
}

bool reward_PlaceLootOnCorpse(InventoryBag *pRewardBag, Entity *pEntKilled)
{
	if (!pEntKilled || !pRewardBag)
	{
		return false;
	}

	entity_SetDirtyBit(pEntKilled, parse_Critter, pEntKilled->pCritter, false);

	if (eaSize(&pEntKilled->pCritter->eaLootBags) > 0)
	{
		//if we're adding more than one loot bag to the same ent, we need to
		//fix up the item IDs.
		int i;
		pRewardBag->pRewardBagInfo->nextID = pEntKilled->pCritter->eaLootBags[eaSize(&pEntKilled->pCritter->eaLootBags)-1]->pRewardBagInfo->nextID;
		for (i = 0; i < eaSize(&pRewardBag->ppIndexedInventorySlots); i++)
		{
			CONTAINER_NOCONST(Item, pRewardBag->ppIndexedInventorySlots[i]->pItem)->id = pRewardBag->pRewardBagInfo->nextID++;
		}
	}
	inv_ent_AddLootBag(pEntKilled, pRewardBag);


	// Make the entity interactable
	pEntKilled->pCritter->bIsInteractable = true;

	// Do not allow costume changes
	pEntKilled->pCritter->bDoNotAutoSetLootCostume = true;

	return true;
}

static void reward_PlaceLootFXOnEntity(CritterLootOwner **peaOwners, InventoryBag *pRewardBag, int iPartitionIdx, Entity *pEnt, Entity *pLootEnt, S32 iHighestItemQuality, bool bBagHasMissionItem)
{
	// Notify loot owners about this, so they can see the glow FX on the corpse
	if (peaOwners[0]->eOwnerType == kRewardOwnerType_Player)
	{
		S32 itEntity;
		Entity *pRewardOwnerEnt;
		for (itEntity = 0; itEntity < eaiSize(&peaOwners[0]->peaiOwnerIDs); itEntity++)
		{
			pRewardOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, peaOwners[0]->peaiOwnerIDs[itEntity]);
			if (pRewardOwnerEnt)
			{
				const char* pchNewFX = interactloot_GetLootFXName(iHighestItemQuality, pRewardBag->pRewardBagInfo->PickupType, bBagHasMissionItem);
				ClientCmd_SetGlowOnCorpseForLoot(pRewardOwnerEnt, pLootEnt->myRef, pchNewFX);
			}
		}			
	}
	else if (peaOwners[0]->eOwnerType == kRewardOwnerType_Team || peaOwners[0]->eOwnerType == kRewardOwnerType_TeamLeader)
	{
		// There is a possibility that, some team members might not have access to the critter at this exact moment
		// If this becomes an issue we need to handle it
		Team *pTeam = team_GetTeam(pEnt);

		if (pTeam && pTeam->eaMembers)
		{
			S32 itTeam;
			TeamMember *pTeamMember = NULL;
			Entity *pTeamMemberEnt = NULL;

			for (itTeam = 0; itTeam < eaSize(&pTeam->eaMembers); itTeam++)
			{
				pTeamMember = pTeam->eaMembers[itTeam];

				if (pTeamMember)
				{
					pTeamMemberEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
					if (pTeamMemberEnt)
					{
						const char* pchNewFX = interactloot_GetLootFXName(iHighestItemQuality, pRewardBag->pRewardBagInfo->PickupType, bBagHasMissionItem);
						ClientCmd_SetGlowOnCorpseForLoot(pTeamMemberEnt, pLootEnt->myRef, pchNewFX);
					}
				}
			}
		}
	}
}

static Entity *reward_GiveInteractibleWithKilledEntity(CritterLootOwner **peaOwners, InventoryBag *pRewardBag, Vec3 vDropPos, int iPartitionIdx, Entity *pEnt, Entity *pEntKilled, bool bAutoLootInteract)
{
	S32 iHighestItemQuality;
	bool bBagHasMissionItem = false;

	// If there the bag is empty, then do nothing
	if (!eaSize(&pRewardBag->ppIndexedInventorySlots))
	{
		StructDestroy(parse_InventoryBag, pRewardBag);
		return NULL;
	}

	// Get the highest item quality in the reward bag
	iHighestItemQuality = MAX(0, invbag_GetHighestItemQuality(pRewardBag, &bBagHasMissionItem));

	if (peaOwners)
		eaCopyStructs(&peaOwners, &pRewardBag->pRewardBagInfo->peaLootOwners, parse_CritterLootOwner);
	
	if (gConf.bKeepLootsOnCorpses && pEntKilled && pEntKilled->pCritter && pRewardBag->pRewardBagInfo->PickupType != kRewardPickupType_Rollover)
	{
		S32 iLastHighestItemQuality = kItemQuality_None;
		bool bSuccess;
		int i;
		for (i = eaSize(&pEntKilled->pCritter->eaLootBags)-1; i >= 0; i--)
		{
			iLastHighestItemQuality = MAX(iLastHighestItemQuality, invbag_GetHighestItemQuality(pEntKilled->pCritter->eaLootBags[i], NULL));
		}

		bSuccess = reward_PlaceLootOnCorpse(pRewardBag, pEntKilled);
		if (bSuccess == false)
		{
			Errorf("Unanticipated failure to put loot on a corpse: %s.\n",pEntKilled->debugName);
		}
	
		if (iHighestItemQuality > iLastHighestItemQuality)
		{
			// Place the loot FX on the killed entity
			reward_PlaceLootFXOnEntity(pRewardBag->pRewardBagInfo->peaLootOwners, pRewardBag, iPartitionIdx, pEnt, pEntKilled, iHighestItemQuality, bBagHasMissionItem);
		}
		return pEntKilled;
	}
	else
	{
		Message *pMsg = RefSystem_ReferentFromString(gMessageDict, (char*)"Reward.LootCritter.DisplayName");
		Entity *loot_ent = critter_Create( "NotYourVisibleDrop", NULL, GLOBALTYPE_ENTITYCRITTER, iPartitionIdx, NULL, 0, 1, 0, 0, 0, pMsg, 0, 0, 0, NULL, NULL);
		PlayerCostume *loot_costume = 0;
		const char *pcName;

		if ( !pRewardBag->pRewardBagInfo->peaLootOwners || !loot_ent
			|| !inv_ent_AddInventory(loot_ent)
			|| !inv_ent_AddLootBag(loot_ent, pRewardBag) )
		{
			//could not create main bag on loot ent, just clear the ent
			StructDestroy(parse_InventoryBag, pRewardBag);
			entDie(loot_ent,0.01,0,0,NULL);
			return NULL;
		}

		entSetPos(loot_ent, vDropPos, 1, __FUNCTION__);
		entSetRot(loot_ent, unitquat, 1, __FUNCTION__);

		entity_SetDirtyBit(loot_ent, parse_Critter, loot_ent->pCritter, false);
		loot_ent->pCritter->bAutoInteract = bAutoLootInteract;
		loot_ent->pCritter->bAutoLootMe = g_RewardConfig.bLootEntsAlwaysAutoLoot;
		entDie(loot_ent, pRewardBag->pRewardBagInfo->LingerTime,0,0,NULL);

		if (reward_SetLootEntCostumeRefsForItem(inv_bag_GetItem(pRewardBag, 0), loot_ent))
		{
			COPY_HANDLE(loot_ent->costumeRef.hReferencedCostume, loot_ent->pCritter->hNotYoursCostumeRef);
		}
		else
		{
			loot_costume = GET_REF(pRewardBag->pRewardBagInfo->hNotYoursCostumeRef);
			pcName = (loot_costume && loot_costume->pcName) ? loot_costume->pcName : "NotYourVisibleDrop";

			SET_HANDLE_FROM_STRING("PlayerCostume", pcName, loot_ent->pCritter->hNotYoursCostumeRef);
			SET_HANDLE_FROM_STRING("PlayerCostume", pcName, loot_ent->costumeRef.hReferencedCostume);

			loot_costume = GET_REF(pRewardBag->pRewardBagInfo->hYoursCostumeRef);
			pcName = (loot_costume && loot_costume->pcName) ? loot_costume->pcName : "YourVisibleDrop";

			SET_HANDLE_FROM_STRING("PlayerCostume", pcName, loot_ent->pCritter->hYoursCostumeRef);
		}
		costumeGenerate_FixEntityCostume(loot_ent);

		if(pRewardBag->pRewardBagInfo->PickupType == kRewardPickupType_Rollover)
		{
			loot_ent->pCritter->bDoNotAutoSetLootCostume = true;
		}
		else
			loot_ent->pCritter->bIsInteractable = true;
		//Rollover loot should not be flagged as interactable, since it isn't.

		if (pRewardBag->pRewardBagInfo->LaunchType == kRRewardLaunchType_Scatter)
		{
			LaunchLoot(loot_ent);
		}
		else
		{
			TinyLaunchLoot(loot_ent); // don't move it far
		}

		// Place the loot FX on the new loot entity
		reward_PlaceLootFXOnEntity(pRewardBag->pRewardBagInfo->peaLootOwners, pRewardBag, iPartitionIdx, pEnt, loot_ent, iHighestItemQuality, bBagHasMissionItem);
		return loot_ent;
	}
}

Entity *reward_GiveInteractible(int iPartitionIdx, CritterLootOwner **peaOwners, InventoryBag *pRewardBag, Vec3 vDropPos)
{
	// find owner ent
	if(peaOwners[0]->eOwnerType == kRewardOwnerType_Player)
	{
		S32 itEntity;
		Entity *pRewardOwnerEnt;
		for(itEntity = 0; itEntity < eaiSize(&peaOwners[0]->peaiOwnerIDs); itEntity++)
		{
			pRewardOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, peaOwners[0]->peaiOwnerIDs[itEntity]);
			if (pRewardOwnerEnt)
			{
				return reward_GiveInteractibleWithKilledEntity(peaOwners, pRewardBag, vDropPos, iPartitionIdx, NULL, NULL, false);
			}
		}
	}

	// no owner ...
	return NULL;
}

void reward_GiveBagCB(Entity *pEnt, InventoryBag *pRewardBag, Vec3 DropPos, bool bAutoLootInteract,
					  Entity *pEntKilled, const ItemChangeReason *pReason, TransactionReturnCallback pFunc, void* pData)
{
	if (!pRewardBag)
		return;
	if (!pEnt)
	{
		StructDestroy(parse_InventoryBag, pRewardBag);
		return;
	}

	switch (pRewardBag->pRewardBagInfo->PickupType)
	{
		//case kRewardPickupType_Choose: choose should never drop anything
		case kRewardPickupType_Interact:
		case kRewardPickupType_Rollover:
		{
			CritterLootOwner **pOwners = NULL;

			devassertmsg(eaSize(&pOwners) == 0, "Previous loot owners exist where code assumes they don't.");

			//DupForTeam bags already have their owners set.
			if ((pRewardBag->pRewardBagInfo->flags & kRewardFlag_DupForTeam) && eaSize(&pRewardBag->pRewardBagInfo->peaLootOwners) == 1)
			{
				Entity* pOwnerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pRewardBag->pRewardBagInfo->peaLootOwners[0]->peaiOwnerIDs[0]);
				reward_GiveInteractibleWithKilledEntity(NULL, pRewardBag, DropPos, entGetPartitionIdx(pOwnerEnt), pOwnerEnt, pEntKilled, bAutoLootInteract);
				break;
			}
			else
			{
				eaPush(&pOwners, StructCreate(parse_CritterLootOwner));
				pOwners[0]->eOwnerType = pRewardBag->pRewardBagInfo->OwnerType;
				if (pOwners[0]->eOwnerType == kRewardOwnerType_None)
					pOwners[0]->eOwnerType = kRewardOwnerType_Player;
				else if (pOwners[0]->eOwnerType == kRewardOwnerType_Team || pOwners[0]->eOwnerType == kRewardOwnerType_TeamLeader)
				{
					if (team_GetTeam(pEnt))
						eaiPush(&pOwners[0]->peaiOwnerIDs, pEnt->pTeam->iTeamID);
					else
						pOwners[0]->eOwnerType = kRewardOwnerType_Player;
				}
				if (pOwners[0]->eOwnerType == kRewardOwnerType_Player)
					eaiPush(&pOwners[0]->peaiOwnerIDs, pEnt->myContainerID);

				reward_GiveInteractibleWithKilledEntity(pOwners, pRewardBag, DropPos, entGetPartitionIdx(pEnt), pEnt, pEntKilled, bAutoLootInteract);

				eaDestroyStruct(&pOwners, parse_CritterLootOwner);
			}
			break;
		}

		case kRewardPickupType_Direct:
			{
				// only pSaved can get direct rewards (such as numerics)
				if(pEnt->pSaved)
				{
					reward_EntLootCB(pEnt, pRewardBag, pReason, pFunc, pData);
				}
			}
			break;

		default:
			if (gbEnableRewardDataLogging) {
				entLog(LOG_REWARDS, pEnt, "GiveBag", "Attempted to give invalid bag type %s", StaticDefineIntRevLookup(RewardPickupTypeEnum, pRewardBag->pRewardBagInfo->PickupType) );
			}
			StructDestroy(parse_InventoryBag, pRewardBag);
			break;
	}
}

void reward_SetTeamCredit(KillCreditTeam*** peaCreditTeams, RewardContext * pContext)
{
	int numPlayersWithCredit = 0;
	int i;
	KillCreditTeam *team_credit = NULL;

	for (i = 0; i < eaSize(peaCreditTeams); i++)
	{
		team_credit = (*peaCreditTeams)[i];
		if (team_credit->bHighestDamager)
			break;
	}

	pContext->pTeamCredit = team_credit;
}

static void reward_GenerateBagFromTable(int iPartitionIdx, Entity *pPlayerEnt, RewardTable *pTable, InventoryBag ***rewardBagList, int iLevel, F32 fScale, RewardNumericScale **eaNumericScales, RewardContextType type, int skillLevel, bool bUseRewardModifiers, U32* pSeed)
{
	RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);
	int RewardLevel = iLevel;

	if (pLocalContext == NULL)
		return;

	//make sure Level is in range
	if ( !USER_LEVEL_VALID(RewardLevel) )
	{
		Errorf("Level out of range" );
		RewardLevel = CLAMP_USER_LEVEL(RewardLevel);
	}

	pLocalContext->type = type;
	pLocalContext->RewardLevel = RewardLevel;
	pLocalContext->KillerLevel = RewardLevel;
	pLocalContext->SkillLevel = skillLevel;
	pLocalContext->RewardScale = fScale;
	
	if (eaSize(&eaNumericScales)) 
	{
		eaCopyStructs(&eaNumericScales, &pLocalContext->eaNumericScales, parse_RewardNumericScale);
		eaIndexedEnable(&pLocalContext->eaNumericScales, parse_RewardNumericScale);
	}
	if (bUseRewardModifiers && pPlayerEnt->pPlayer)
	{
		eaCopyStructs(&pPlayerEnt->pPlayer->eaRewardMods, &pLocalContext->eaRewardMods, parse_RewardModifier);
		eaIndexedEnable(&pLocalContext->eaRewardMods, parse_RewardModifier);
	}

	reward_generateEx(iPartitionIdx, pPlayerEnt, pLocalContext, pTable, rewardBagList, NULL, pSeed, false, NULL, NULL);

	//Cleanup the context
	reward_ContextCleanup(pLocalContext);
}

// Generates a bag for use in populating random variables in a MadLibs Mission
void reward_GenerateMissionVarsBag(int iPartitionIdx, Entity *pPlayerEnt, RewardTable *pTable, InventoryBag ***rewardBagList, int iLevel)
{
	reward_GenerateBagFromTable(iPartitionIdx, pPlayerEnt, pTable, rewardBagList, iLevel, 1.0f, NULL, RewardContextType_RandomMissionGen, 0, false, NULL);
}

// Generates a bag for an interactable "treasure chest"
void reward_GenerateInteractableBag(int iPartitionIdx, Entity *pPlayerEnt, RewardTable *pTable, InventoryBag ***rewardBagList, int iLevel, int skillLevel, U32* pSeed, Entity* interactingEnt)
{
	exprContextSetSilentErrors(g_pRewardNonPlayerContext, false);
	exprContextSetIntVar(g_pRewardNonPlayerContext, "iLevel", iLevel);
	exprContextSetUserPtr(g_pRewardNonPlayerContext, interactingEnt, parse_Entity);
	reward_GenerateBagFromTable(iPartitionIdx, pPlayerEnt, pTable, rewardBagList, iLevel, 1.0f, NULL, RewardContextType_Clickable, skillLevel, false, pSeed);
}

// Generates reward bags for persisted stores
void reward_GenerateBagsForStore(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, U32* pSeed, InventoryBag*** peaBags)
{
	reward_GenerateBagFromTable(iPartitionIdx, pPlayerEnt, pTable, peaBags, iLevel, 1.0f, NULL, RewardContextType_Store, 0, false, pSeed);
}

// Generates reward bags for item assignments
void reward_GenerateBagsForItemAssignment(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, F32 fScale, RewardNumericScale **eaNumericScales, bool bUseRewardMods, U32* pSeed, InventoryBag*** peaBags)
{
	reward_GenerateBagFromTable(iPartitionIdx, pPlayerEnt, pTable, peaBags, iLevel, fScale, eaNumericScales, RewardContextType_MissionReward, 0, bUseRewardMods, pSeed);
}

// Generates reward bags for micro transactions
void reward_GenerateBagsForMicroTransaction(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, InventoryBag*** peaBags)
{
	reward_GenerateBagFromTable(iPartitionIdx, pPlayerEnt, pTable, peaBags, 1, 1.0f, NULL, RewardContextType_MissionReward, 0, false, NULL);
}

// Generates reward bags for a reward pack item
void reward_GenerateBagsForRewardPack(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, InventoryBag*** peaBags)
{
	reward_GenerateBagFromTable(iPartitionIdx, pPlayerEnt, pTable, peaBags, iLevel, 1.0f, NULL, RewardContextType_MissionReward, 0, false, NULL);
}

// Generates reward bags for a personal project
void reward_GenerateBagsForPersonalProject(int iPartitionIdx, Entity *pPlayerEnt, SA_PARAM_NN_VALID RewardTable* pTable, S32 iLevel, InventoryBag*** peaBags)
{
    reward_GenerateBagFromTable(iPartitionIdx, pPlayerEnt, pTable, peaBags, iLevel, 1.0f, NULL, RewardContextType_MissionReward, 0, false, NULL);
}

F32 reward_GetRewardScaleOverTime(S32 iElapsedTime, S32 iScaleRewardOverTimeMinutes)
{
	if (iScaleRewardOverTimeMinutes > 0)
	{
		S32 iExpectedTime = iScaleRewardOverTimeMinutes * 60; //convert to seconds

		if (iElapsedTime > iExpectedTime)
		{
			F32 fBonus = ((iElapsedTime - iExpectedTime) / (F32)iExpectedTime) * 0.5f;
			return MINF(1.0f + fBonus, 1.5f); //cap the scale at 1.5
		}
		else
		{
			//just linearly increase the scale value for now
			return MAXF(iElapsedTime / (F32)iExpectedTime, 0.0f); 
		}
	}
	return 1.0f;
}

static void reward_ContextApplyPerNumericScaleFromMissionDef(RewardContext* pContext, MissionDef* pMissionDef)
{
	if (pMissionDef && pMissionDef->params)
	{
		int i;
		for (i = eaSize(&pMissionDef->params->eaNumericScales)-1; i >= 0; i--)
		{
			MissionNumericScale* pScale = pMissionDef->params->eaNumericScales[i];
			RewardNumericScale* pRewardScale = StructCreate(parse_RewardNumericScale);
			pRewardScale->pchNumericItem = allocAddString(pScale->pchNumeric);
			pRewardScale->fScale = pScale->fScale;
			eaIndexedEnable(&pContext->eaNumericScales, parse_RewardNumericScale);
			eaPush(&pContext->eaNumericScales, pRewardScale);
		}
	}
}

static void reward_GenerateMissionActionRewardByTable(int iPartitionIdx,
	Entity *pPlayerEnt,
	InventoryBag ***bags,
	MissionDef* missionDef,
	RewardTable * table,
	U32* seed,
	S32 iPlayerLevel,
	S32 mission_level,
	S32 time_level,
	bool bSubMissionTurnin,
	MissionCreditType eCreditType,
	RecruitType eRecruitType,
	RewardContextData *pRewardContextData,
	RewardGatedDataInOut *pGatedData)
{
	RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

	pLocalContext->type = bSubMissionTurnin ? RewardContextType_SubMissionTurnIn : RewardContextType_MissionReward;
	pLocalContext->eMissionCreditType = eCreditType;
	pLocalContext->pKilled = NULL;
	pLocalContext->TimeLevel = time_level;
	if(pRewardContextData->pEnt)
	{
		SetKillerRewardContextEx(pRewardContextData->pEnt, pLocalContext, 0, 0, 0);
	}
	else
	{
		SetKillerRewardContextEx(NULL, pLocalContext, pRewardContextData->iPlayerLevel, eRecruitType, true);
		pLocalContext->iKillerSubscriptionDays = entity_GetDaysSubscribedFromExtract(pRewardContextData->pExtract);
		eaIndexedEnable(&pLocalContext->eaRewardMods, parse_RewardModifier);
		eaCopyStructs(&pRewardContextData->eaRewardMods, &pLocalContext->eaRewardMods, parse_RewardModifier);
	}

	if (mission_level) {
		// Mission level already set in mission instance
		pLocalContext->RewardLevel = mission_level;
	} else {
		// Mission instance not yet created, calculate the mission level
		pLocalContext->RewardLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, missionDef);
	}

	pLocalContext->pExtract = pRewardContextData->pExtract;
	pLocalContext->KillerLevel = iPlayerLevel;
	pLocalContext->pcRank = critterRankGetMissionDefault();
	//		Context.SkillLevel = inv_GetNumericItemValue(pPlayerEnt, InvBagIDs_Numeric, "SkillLevel");
	pLocalContext->SkillLevel = 0;	// No playerEnt available
	if(missionDef)
		pLocalContext->pcMission = RefSystem_StringFromReferent(missionDef);

	pLocalContext->pCBIData = &pRewardContextData->CBIData;
	reward_generateEx(iPartitionIdx, pPlayerEnt, pLocalContext, table, bags, NULL, seed, false, NULL, pGatedData);

	//Cleanup the context
	reward_ContextCleanup(pLocalContext);
}

void reward_GenerateMissionActionRewards(int iPartitionIdx, Entity *pPlayerEnt, MissionDef* missionDef, MissionState trigger, InventoryBag ***bags, U32* seed, MissionCreditType eCreditType,
	int mission_level, int time_level, U32 iMissionStartTime, U32 iMissionEndTime, RecruitType eRecruitType, bool bUGCProject, bool bMissionQualifiesForUGCReward,
	bool bMissionQualifiesForUGCFeaturedReward, bool bMissionQualifiesForUGCNonCombatReward, bool bSubMissionTurnin, bool bGenerateChestRewards, RewardContextData *pRewardContextData,
	F32 fAverageDurationInMinutes, RewardGatedDataInOut *pGatedData)
{
	RewardTable *table = NULL;
	RewardTable *pChestRewardTable = NULL;
	S32 iPlayerLevel;

	//error check input parms
	if ( !missionDef || !missionDef->params )
		return;

	if(pRewardContextData->pEnt)
	{
		iPlayerLevel = entity_GetSavedExpLevel(pRewardContextData->pEnt);
		ent_GetCharacterRewardContextInfo(&pRewardContextData->CBIData, pRewardContextData->pEnt);
	}
	else
	{
		iPlayerLevel = pRewardContextData->iPlayerLevel;
	}

	//Give Mission numerics rewards on mission completed
	//only for top level mission
	if ((!IS_HANDLE_ACTIVE(missionDef->parentDef)) &&
		((bUGCProject && g_RewardConfig.bUseNonQualifyingUGCRewardConfig) ||
		(bMissionQualifiesForUGCFeaturedReward || bMissionQualifiesForUGCReward) ||
		 missionDef->params->NumericRewardScale != 0 || 
		 missionDef->params->iScaleRewardOverTime != 0 ||
		 (eCreditType == MissionCreditType_AlreadyCompleted && 
		  missionDef->params->NumericRewardScaleAlreadyCompleted != 0) ||
		 (eCreditType == MissionCreditType_Ineligible &&
		  missionDef->params->NumericRewardScaleIneligible != 0)))
	{
		F32 fRewardScale = missionDef->params->NumericRewardScale;

		if (g_MissionConfig.fGlobalSecondaryNumericRewardScale != 0 &&
			(eCreditType == MissionCreditType_AlreadyCompleted ||
			eCreditType == MissionCreditType_Ineligible))
		{
			fRewardScale = g_MissionConfig.fGlobalSecondaryNumericRewardScale;
		}

		if (eCreditType == MissionCreditType_AlreadyCompleted && 
			missionDef->params->NumericRewardScaleAlreadyCompleted != 0)
		{
			fRewardScale = missionDef->params->NumericRewardScaleAlreadyCompleted;
		}
		else if (eCreditType == MissionCreditType_Ineligible &&
				 missionDef->params->NumericRewardScaleIneligible != 0)
		{
			fRewardScale = missionDef->params->NumericRewardScaleIneligible;
		}

		switch (trigger)
		{
		case MissionState_TurnedIn:
			// TODO - Do Secondary rewards need a different Reward Table?
			if((bUGCProject && g_RewardConfig.bUseNonQualifyingUGCRewardConfig) || bMissionQualifiesForUGCFeaturedReward || bMissionQualifiesForUGCReward)
			{
				if(bMissionQualifiesForUGCFeaturedReward)
					table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, gConf.pchUGCMissionFeaturedRewardTable);

				// If there's no featured UGC reward setup, then try to give out the default reward instead
				if(!table)
					table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, gConf.pchUGCMissionDefaultRewardTable);
				if(!table)
					table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, "DefaultMissionNumerics");

				fRewardScale = gConf.fUGCMissionRewardMaxScale / 1.5;
			}
			else
				table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, "DefaultMissionNumerics");
		}

		if (table)
		{
			RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);
			if(g_RewardConfig.bUseUGCRewardConfig && (bMissionQualifiesForUGCReward || bMissionQualifiesForUGCFeaturedReward))
			{
				// Calculate the reward scale for UGC missions. This is a linear scaling of the minimum-to-maximum reward scale
				// based on where the average UGC mission duration lies between the minimum reward time and the maximum reward time
				F32 fUGCRewardTimeDifference = MAX(g_RewardConfig.UGCRewardConfig.fMaximumRewardTime - g_RewardConfig.UGCRewardConfig.fMinimumRewardTime, 1.0f);
				F32 fMissionTime = MIN(MAX(fAverageDurationInMinutes - g_RewardConfig.UGCRewardConfig.fMinimumRewardTime, 0.0f), fUGCRewardTimeDifference);
				F32 fUGCRewardTimeScale = fMissionTime / fUGCRewardTimeDifference;
				F32 fUGCRewardScale = g_RewardConfig.UGCRewardConfig.fMaximumRewardScale - g_RewardConfig.UGCRewardConfig.fMinimumRewardScale;

				fRewardScale = g_RewardConfig.UGCRewardConfig.fMinimumRewardScale + (fUGCRewardScale * fUGCRewardTimeScale);
			}
			else if(g_RewardConfig.bUseNonQualifyingUGCRewardConfig && bUGCProject && !(bMissionQualifiesForUGCReward || bMissionQualifiesForUGCFeaturedReward))
			{
				// Calculate the reward scale for non-qualifying UGC missions. This was put in here for NW to reduce the hate of people playing new, non-qualifying missions that are
				// long and good and not getting any XP at the end. We give them some XP at least.
				if(fAverageDurationInMinutes >= g_RewardConfig.UGCNonQualifyingRewardConfig.fThresholdBetweenLowAndHighTime)
					fRewardScale = g_RewardConfig.UGCNonQualifyingRewardConfig.fHighRewardScale;
				else if(fAverageDurationInMinutes >= g_RewardConfig.UGCNonQualifyingRewardConfig.fThresholdForZeroTime)
					fRewardScale = g_RewardConfig.UGCNonQualifyingRewardConfig.fLowRewardScale;
				else
					fRewardScale = 0.0f;
			}
			else if(iMissionStartTime && (missionDef->params->iScaleRewardOverTime || bMissionQualifiesForUGCReward || bMissionQualifiesForUGCFeaturedReward))
			{
				S32 iScaleRewardOverTime = (bMissionQualifiesForUGCReward || bMissionQualifiesForUGCFeaturedReward) ? gConf.iUGCMissionRewardScaleOverTime : missionDef->params->iScaleRewardOverTime;
				S32 iEndTime = iMissionEndTime ? iMissionEndTime : iMissionStartTime+iScaleRewardOverTime;
				S32 iElapsedMissionTime = (S32)(iEndTime - iMissionStartTime);
				fRewardScale *= reward_GetRewardScaleOverTime(iElapsedMissionTime, iScaleRewardOverTime);
			}

			pLocalContext->type = bSubMissionTurnin ? RewardContextType_SubMissionTurnIn : RewardContextType_MissionReward;
			pLocalContext->eMissionCreditType = eCreditType;
			pLocalContext->pKilled = NULL;
			pLocalContext->RewardScale = fRewardScale;
			if(pRewardContextData->pEnt)
			{
				SetKillerRewardContextEx(pRewardContextData->pEnt, pLocalContext, 0, 0, 0);
			}
			else
			{
				SetKillerRewardContextEx(NULL, pLocalContext, pRewardContextData->iPlayerLevel, eRecruitType, true);
				pLocalContext->iKillerSubscriptionDays = entity_GetDaysSubscribedFromExtract(pRewardContextData->pExtract);
				eaIndexedEnable(&pLocalContext->eaRewardMods, parse_RewardModifier);
				eaCopyStructs(&pRewardContextData->eaRewardMods, &pLocalContext->eaRewardMods, parse_RewardModifier);
			}

			if (mission_level) {
				// Mission level already set in mission instance
				pLocalContext->RewardLevel = mission_level;
			} else {
				// Mission instance not yet created, calculate the mission level
				pLocalContext->RewardLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, missionDef);
			}

			if (eCreditType == MissionCreditType_Flashback){
				pLocalContext->RewardLevel = iPlayerLevel;
			} else if(eCreditType != MissionCreditType_Primary) {
				pLocalContext->RewardLevel = MIN(iPlayerLevel, pLocalContext->RewardLevel);
			}

			pLocalContext->pExtract = pRewardContextData->pExtract;
			pLocalContext->KillerLevel = iPlayerLevel;
//			Context.SkillLevel = inv_GetNumericItemValue(pPlayerEnt, InvBagIDs_Numeric, "SkillLevel");
			pLocalContext->SkillLevel = 0;	// Not supported, because there's no playerEnt
			if(missionDef)
				pLocalContext->pcMission = RefSystem_StringFromReferent(missionDef);

			// Apply per-numeric scales
			reward_ContextApplyPerNumericScaleFromMissionDef(pLocalContext, missionDef);
			reward_generateEx(iPartitionIdx, pPlayerEnt, pLocalContext, table, bags, NULL, seed, false, NULL, pGatedData);

			//Cleanup the context
			reward_ContextCleanup(pLocalContext);
		}
	}

	// Give Non-Combat UGC Mission Rewards
	table = NULL;
	if ( (!IS_HANDLE_ACTIVE(missionDef->parentDef)) && bMissionQualifiesForUGCNonCombatReward && bMissionQualifiesForUGCReward && trigger == MissionState_TurnedIn)
	{
		table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, gConf.pchUGCMissionDefaultNonCombatRewardTable);

		if(table)
		{
			RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

			pLocalContext->type = bSubMissionTurnin ? RewardContextType_SubMissionTurnIn : RewardContextType_MissionReward;
			pLocalContext->eMissionCreditType = eCreditType;
			pLocalContext->pKilled = NULL;
			if(pRewardContextData->pEnt)
			{
				SetKillerRewardContextEx(pRewardContextData->pEnt, pLocalContext, 0, 0, 0);
			}
			else
			{
				SetKillerRewardContextEx(NULL, pLocalContext, pRewardContextData->iPlayerLevel, eRecruitType, true);
				pLocalContext->iKillerSubscriptionDays = entity_GetDaysSubscribedFromExtract(pRewardContextData->pExtract);
				eaIndexedEnable(&pLocalContext->eaRewardMods, parse_RewardModifier);
				eaCopyStructs(&pRewardContextData->eaRewardMods, &pLocalContext->eaRewardMods, parse_RewardModifier);
			}

			if (mission_level) {
				// Mission level already set in mission instance
				pLocalContext->RewardLevel = mission_level;
			} else {
				// Mission instance not yet created, calculate the mission level
				pLocalContext->RewardLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, missionDef);
			}

			if (eCreditType == MissionCreditType_Flashback){
				pLocalContext->RewardLevel = iPlayerLevel;
			} else if(eCreditType != MissionCreditType_Primary) {
				pLocalContext->RewardLevel = MIN(iPlayerLevel, pLocalContext->RewardLevel);
			}

			pLocalContext->pExtract = pRewardContextData->pExtract;
			pLocalContext->KillerLevel = iPlayerLevel;
			pLocalContext->SkillLevel = 0;	// Not supported, because there's no playerEnt
			if(missionDef)
				pLocalContext->pcMission = RefSystem_StringFromReferent(missionDef);

			reward_generateEx(iPartitionIdx, pPlayerEnt, pLocalContext, table, bags, NULL, seed, false, NULL, pGatedData);

			//Cleanup the context
			reward_ContextCleanup(pLocalContext);
		}
	}


	//Give action specific rewards
	table = NULL;
	switch (trigger)
	{
	case MissionState_InProgress:
		if (missionDef->params->OnstartRewardTableName && missionDef->params->OnstartRewardTableName[0])
		{
			table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,missionDef->params->OnstartRewardTableName);
		}
		break;

	case MissionState_Succeeded:
		if (missionDef->params->ActivitySuccessRewardTableName && missionDef->params->ActivitySuccessRewardTableName[0] &&
			missionDef->params->pchActivityName && missionDef->params->pchActivityName[0] && 
			gslActivity_IsActive(missionDef->params->pchActivityName)) {
			table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,missionDef->params->ActivitySuccessRewardTableName);
		} else if (missionDef->params->OnsuccessRewardTableName && missionDef->params->OnsuccessRewardTableName[0]) {
			table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,missionDef->params->OnsuccessRewardTableName);
		}
		break;

	case MissionState_Failed:
		if (missionDef->params->OnfailureRewardTableName && missionDef->params->OnfailureRewardTableName[0])
		{
			table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,missionDef->params->OnfailureRewardTableName);
		}
		break;

	case MissionState_TurnedIn:
		if (missionDef->params->ActivityReturnRewardTableName && missionDef->params->ActivityReturnRewardTableName[0] &&
			missionDef->params->pchActivityName && missionDef->params->pchActivityName[0] &&
			gslActivity_IsActive(missionDef->params->pchActivityName)) {
			table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,missionDef->params->ActivityReturnRewardTableName);
		} else if (missionDef->params->OnreturnRewardTableName && missionDef->params->OnreturnRewardTableName[0] &&
				   eCreditType == MissionCreditType_Primary) {
			table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,missionDef->params->OnreturnRewardTableName);
		} else if (missionDef->params->OnReplayReturnRewardTableName && missionDef->params->OnReplayReturnRewardTableName[0] &&
				   eCreditType == MissionCreditType_AlreadyCompleted){
			table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,missionDef->params->OnReplayReturnRewardTableName);
		}

		// The code below is NW specific and it's unfortunate it's implemented like this.
		// I have not added a config flag for this just in case they want to use this implementation in other games.
		// Each dungeon's reward is defined in a submission's on success reward table.
		// We find which submission to use by looking for a mission offer override in the mission
		// with UI type of ContactMissionUIType_FauxTreasureChest. We assume that there will only be 1 treasure chest reward per mission.
		if (bGenerateChestRewards)
		{
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(missionDef->ppMissionOfferOverrides, MissionOfferOverride, pMissionOfferOverride)
			{
				if (pMissionOfferOverride->pMissionOffer &&
					pMissionOfferOverride->pMissionOffer->eUIType == ContactMissionUIType_FauxTreasureChest &&
					pMissionOfferOverride->pMissionOffer->pchSubMissionName)
				{
					MissionDef *pSubMissionDef = missiondef_FindMissionByName(missionDef, pMissionOfferOverride->pMissionOffer->pchSubMissionName);

					if (pSubMissionDef &&
						pSubMissionDef->params &&
						pSubMissionDef->params->OnsuccessRewardTableName)
					{
						pChestRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict, pSubMissionDef->params->OnsuccessRewardTableName);
						break;
					}
				}
			}
			FOR_EACH_END
		}

		break;
	}

	if ( table )
	{
		reward_GenerateMissionActionRewardByTable(iPartitionIdx, 
			pPlayerEnt, bags, missionDef, table, seed, iPlayerLevel, 
			mission_level, time_level, bSubMissionTurnin, eCreditType, eRecruitType, pRewardContextData, pGatedData);
	}

	if (pChestRewardTable)
	{
		reward_GenerateMissionActionRewardByTable(iPartitionIdx, 
			pPlayerEnt, bags, missionDef, pChestRewardTable, seed, iPlayerLevel, 
			mission_level, time_level, true, eCreditType, eRecruitType, pRewardContextData, pGatedData);
	}
}

//////////////////////////////////////////////////////////////////////////
// Reward adapters
//////////////////////////////////////////////////////////////////////////
/*
 * Each of these functions knows how to award a reward table for a particular
 * system.  If you want to give rewards from a new place, create a new function
 * here.
 * Rewards given from different functions have different values in their context
 * and are treated differently in the reward code.
 */

// Common code to give a reward bag.  Any adapter is welcome to use custom code instead.
// Destroys any leftover reward bags
static void reward_GiveBagsAtPoint(SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_NN_VALID InventoryBag ***peaRewardBags, 
								   Vec3 DropPos, bool bAutoLootInteract, const ItemChangeReason *pReason)
{
	int NumRewardBags = eaSize(peaRewardBags);
	InventoryBag** eaRewardPackBags = NULL;
	int ii;
	
	eaStackCreate(&eaRewardPackBags, NumRewardBags);

	for(ii=NumRewardBags-1; ii>=0; ii--)
	{
		InventoryBag *pRewardBag = (InventoryBag *)eaRemove(peaRewardBags, ii);
		if (pRewardBag && pRewardBag->pRewardBagInfo && pRewardBag->pRewardBagInfo->bShowRewardPackUI)
			eaPush(&eaRewardPackBags, pRewardBag);
	}

	if (eaSize(&eaRewardPackBags) > 0)
	{
		ItemRewardPackRequestData* pData = StructCreate(parse_ItemRewardPackRequestData);
		pData->pRewards = StructCreate(parse_InvRewardRequest);
		pData->ePackResultQuality = inv_FillRewardRequest(eaRewardPackBags, pData->pRewards);
		if (eaSize(&pData->pRewards->eaItemRewards) || eaSize(&pData->pRewards->eaNumericRewards))
		{
			ClientCmd_gclReceiveRewardPackData(pPlayerEnt, pData);
		}
		StructDestroy(parse_ItemRewardPackRequestData, pData);
	}


	for(ii=NumRewardBags-1; ii>=0; ii--)
	{
		InventoryBag *pRewardBag = (*peaRewardBags)[ii];

		if ( pRewardBag )
		{
			int ItemCount = inv_bag_CountItems(pRewardBag, NULL);

			//skip empty bags
			if (ItemCount == 0)
			{
				StructDestroy(parse_InventoryBag, pRewardBag);
				continue;
			}
			reward_GiveBag(pPlayerEnt, pRewardBag, DropPos, bAutoLootInteract, pReason);
		}
		eaRemove(peaRewardBags, ii);
	}
	//cleanup any remaining reward bags
	eaDestroyStruct(peaRewardBags, parse_InventoryBag);
	eaDestroy(&eaRewardPackBags);
}

void reward_GiveBags(SA_PARAM_NN_VALID Entity *pPlayerEnt, bool bAutoLootInteract, SA_PARAM_NN_VALID InventoryBag ***peaRewardBags, const ItemChangeReason *pReason)
{
	Vec3 DropPos;
	entGetPos(pPlayerEnt, DropPos);
	reward_GiveBagsAtPoint(pPlayerEnt, peaRewardBags, DropPos, bAutoLootInteract, pReason);
}

// reward table can be NULL, for example open missions might not have a reward table.
static void SetBagCostumeContext(RewardTable *pTable, RewardContext *pLocalContext)
{
	if(pTable && (pTable->flags & kRewardFlag_AllBagsUseThisCostume) !=0 )
	{
		if(GET_REF(pTable->hNotYoursCostumeRef) && GET_REF(pTable->hYoursCostumeRef))
		{
			COPY_HANDLE(pLocalContext->hNotYoursCostumeRef, pTable->hNotYoursCostumeRef);
			COPY_HANDLE(pLocalContext->hYoursCostumeRef, pTable->hYoursCostumeRef);
		}
		else
		{
			ErrorFilenamef(pTable->pchFileName, "Reward Table %s has kRewardFlag_AllBagsUseThisCostume but one of hNotYoursCostumeRef or hYoursCostumeRef is not a valid costume.", pTable->pchName);
		}
	}
}

// Power/Expression adapter.  Used for rewards given by powers or by expressions; these are
// "game logic" and probably don't fit in any common reward category.
void reward_PowerExec(Entity* e, RewardTable *table, int level, F32 scale, bool bInformClient, const ItemChangeReason *pReason)
{
	InventoryBag **bags = NULL;

	if(table && e)
	{
		RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);
		
		pLocalContext->type = RewardContextType_PowerExec;
		pLocalContext->pKilled = NULL;
		pLocalContext->RewardLevel = level;
		pLocalContext->RewardScale = scale;
		SetKillerRewardContext(e, pLocalContext);
		SetBagCostumeContext(table, pLocalContext);
		reward_generateEx(entGetPartitionIdx(e), e, pLocalContext, table, &bags, NULL, NULL, false, NULL, NULL);

		if(bInformClient && eaSize(&bags))
		{
			int i;

			for(i=0;i<eaSize(&bags);i++)
			{
				if(eaSize(&bags[i]->ppIndexedInventorySlots) > 0)
					ClientCmd_PVP_SetReward(e,bags[i]);
			}
		}

		reward_GiveBags(e, false, &bags, pReason);

		


		//Cleanup the context
		reward_ContextCleanup(pLocalContext);
	}
}

// Gives Rewards from an Open Mission
// Automatically gives algorithmic XP in addition to the reward table passed in
void reward_OpenMissionExec(int iPartitionIdx, Entity* pPlayerEnt, MissionDef* pRootMissionDef, RewardTable *pRewardTable, int level, F32 fRewardScale, Vec3 DropPos, const ItemChangeReason *pReason)
{
	InventoryBag **ppRewardBags = NULL;
	RewardTable *pDefaultMissionNumerics = RefSystem_ReferentFromString(g_hRewardTableDict,"DefaultMissionNumerics");
	int i;

	RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

	if (pLocalContext == NULL)
		return;

	pLocalContext->type = RewardContextType_OpenMission;
	pLocalContext->pKilled = NULL;
	pLocalContext->RewardLevel = level;
	pLocalContext->RewardScale = fRewardScale;
	SetKillerRewardContext(pPlayerEnt, pLocalContext);
	SetBagCostumeContext(pRewardTable, pLocalContext);

	if (pRewardTable)
	{
		reward_generate(iPartitionIdx, pPlayerEnt, pLocalContext, pRewardTable, &ppRewardBags, NULL, NULL);

		// remove unique items from bags that the player already has
		for(i = 0; i < eaSize(&ppRewardBags); ++i)
		{
			reward_cullDuplicateUniquesFromBag(pPlayerEnt, ppRewardBags[i]);
		}
	}

	// Apply per-numeric scales
	reward_ContextApplyPerNumericScaleFromMissionDef(pLocalContext, pRootMissionDef);

	// Always give default XP
	reward_generate(iPartitionIdx, pPlayerEnt, pLocalContext, pDefaultMissionNumerics, &ppRewardBags, NULL, NULL);

	for(i = 0; i < eaSize(&ppRewardBags); i++) {
		if(ppRewardBags[i]->pRewardBagInfo && ppRewardBags[i]->pRewardBagInfo->PickupType == kRewardPickupType_Direct) {
			Item** eaItems = NULL;
			int j;
			inv_bag_GetSimpleItemList(ppRewardBags[i], &eaItems, false);
			for(j = 0; j < eaSize(&eaItems); j++) {
				Item* pItem = eaItems[j];
				ItemDef* pItemDef = GET_REF(pItem->hItem);
				if (pItemDef && !SAFE_MEMBER2(pItem, pRewardData, bHideInUI))
				{
					if (pItemDef->eType == kItemType_Numeric)
					{
						//If it's a numeric, then we can get away with just sending the def name and numeric value
						ClientCmd_AddOpenMissionRewardNumeric(pPlayerEnt, pItemDef->pchName, pItem->count);
					}
					else
					{
						//Deep copy the item so that modifications can be made
						Item* pItemCopy = StructClone(parse_Item, pItem);
						//Send down the entire item, but strip any data that can be generated on the client
						item_trh_RemovePowers(CONTAINER_NOCONST(Item, pItemCopy));
						StructDestroySafe(parse_ItemRewardData, &pItemCopy->pRewardData);
						ClientCmd_AddOpenMissionRewardItem(pPlayerEnt, pItemCopy);
						StructDestroy(parse_Item, pItemCopy);
					}
				}
			}
			eaDestroy(&eaItems);
		}
	}

	reward_GiveBagsAtPoint(pPlayerEnt, &ppRewardBags, DropPos, false, pReason);

	//Cleanup the context
	reward_ContextCleanup(pLocalContext);
}

void reward_DeathPenaltyExec(Entity* pEnt)
{
	InventoryBag **ppRewardBags = NULL;
	RewardTable *pRewardTable = NULL;
	WorldRegionType eRegion;
	PlayerDifficultyMapData *pDifficultyData;
	Entity* pPlayerEnt = NULL;

	if(!pEnt)
		return;

	eRegion = entGetWorldRegionTypeOfEnt(pEnt);
	pDifficultyData = pd_GetDifficultyMapData(mapState_GetDifficulty(mapState_FromEnt(pEnt)), zmapInfoGetPublicName(NULL), eRegion);

	switch (entGetType(pEnt))
	{
		xcase GLOBALTYPE_ENTITYPLAYER:
		{
			// default reward table
			pRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,"Death_Penalty");

			if(pDifficultyData && GET_REF(pDifficultyData->hDeathPenaltyTable))
			{
				pRewardTable = GET_REF(pDifficultyData->hDeathPenaltyTable);
			}

			pPlayerEnt = pEnt;
		} 
		xcase GLOBALTYPE_ENTITYSAVEDPET:
		{
			// default reward table
			pRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,"Death_Penalty_SavedPet");

			if(pDifficultyData && GET_REF(pDifficultyData->hSavedPetDeathPenaltyTable))
			{
				pRewardTable = GET_REF(pDifficultyData->hSavedPetDeathPenaltyTable);
			}

			if (pEnt->pSaved)
			{
				pPlayerEnt = entFromContainerIDAnyPartition(pEnt->pSaved->conOwner.containerType, pEnt->pSaved->conOwner.containerID);
			}
		}
		xdefault:
		{
			Errorf("reward_DeathPenaltyExec: Unsupported entity type %s", GlobalTypeToName(entGetType(pEnt)));
		}
	}

	if (pRewardTable && pPlayerEnt)
	{
		RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

		if (pLocalContext)
		{
			ItemChangeReason reason = {0};

			pLocalContext->type = RewardContextType_DeathPenalty;
			pLocalContext->pKilled = pEnt;
			pLocalContext->RewardLevel = GetEntLevel(pEnt);
			SetKillerRewardContext(pEnt, pLocalContext);

			reward_generate(entGetPartitionIdx(pEnt), pPlayerEnt, pLocalContext, pRewardTable, &ppRewardBags, NULL, NULL);

			inv_FillItemChangeReason(&reason, pEnt, "Powers:DeathPenalty", NULL);

			reward_GiveBags(pEnt, false, &ppRewardBags, &reason);

			//Cleanup the context
			reward_ContextCleanup(pLocalContext);
		}

	}
}

void reward_RespawnPenaltyExec(Entity* pEnt)
{
	InventoryBag **ppRewardBags = NULL;
	RewardTable *pRewardTable = NULL;

	pRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,"Respawn_Penalty");

	if ( pRewardTable )
	{
		RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

		if (pLocalContext)
		{
			ItemChangeReason reason = {0};

			pLocalContext->type = RewardContextType_DeathPenalty;
			pLocalContext->pKilled = pEnt;
			SetKillerRewardContext(pEnt, pLocalContext);
			pLocalContext->RewardLevel = GetEntLevel(pEnt);
			pLocalContext->KillerLevel = pLocalContext->RewardLevel;

			reward_generate(entGetPartitionIdx(pEnt), pEnt, pLocalContext, pRewardTable, &ppRewardBags, NULL, NULL);

			inv_FillItemChangeReason(&reason, pEnt, "Powers:RespawnPenalty", NULL);

			reward_GiveBags(pEnt, false, &ppRewardBags, &reason);

			//Cleanup the context
			reward_ContextCleanup(pLocalContext);
		}

	}
}

void reward_LevelUp(Entity* pPlayerEnt, char* RewardTableName, int level)
{
	InventoryBag **ppRewardBags = NULL;
	RewardTable *pRewardTable = NULL;
	S32 i, j;

	pRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,RewardTableName);

	if ( pRewardTable )
	{
		RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

		if (pLocalContext)
		{
			ItemChangeReason reason = {0};

			pLocalContext->type = RewardContextType_LevelUp;
			pLocalContext->pKilled = NULL;
			pLocalContext->RewardLevel = level;
			SetKillerRewardContext(pPlayerEnt, pLocalContext);

			reward_generate(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pLocalContext, pRewardTable, &ppRewardBags, NULL, NULL);

			inv_FillItemChangeReason(&reason, pPlayerEnt, "Misc:LevelUpBonus", NULL);

			reward_GiveBags(pPlayerEnt, false, &ppRewardBags, &reason);

			//Cleanup the context
			reward_ContextCleanup(pLocalContext);
		}
	}

	// Send additional level up information from the message files
	if(pPlayerEnt->pChar)
	{
		char *esLevelUpKey = NULL;
		estrStackCreate(&esLevelUpKey);
		for(i = 0; i < gProjectGameServerConfig.iMaximumMessagesPerLevelUp; ++i)
		{
			const char *pcMessage;
			estrPrintf(&esLevelUpKey, "Levelup_Message_%3.3d_%2.2d", level, i);
			pcMessage = TranslateMessageKey(esLevelUpKey);
			if(pcMessage)
			{
				notify_NotifySend(pPlayerEnt, kNotifyType_LevelUp_OtherInfo, pcMessage, NULL, NULL);
			}
		}
		for (i = 0; i < eaSize(&s_LevelupNotificationGroups.eaLevels); i++)
		{
			LevelupNotificationGroup *pGroup = s_LevelupNotificationGroups.eaLevels[i];
			if (pGroup->iLevel == level)
			{
				// Check conditions based on primary character path
				if (pGroup->pchCharacterPath && *pGroup->pchCharacterPath)
				{
					CharacterPath *pPath = entity_GetPrimaryCharacterPath(pPlayerEnt);
					if ((stricmp(pGroup->pchCharacterPath, "None")==0 && pPath)
						|| (stricmp(pGroup->pchCharacterPath, "Any")==0 && !pPath)
						|| (stricmp(pGroup->pchCharacterPath, "None")!=0
							&& stricmp(pGroup->pchCharacterPath, "Any")!=0
							&& (!pPath || stricmp(pGroup->pchCharacterPath, pPath->pchName))
							)
						)
					{
						continue;
					}
				}

				// Send the messages
				for (j = 0; j < eaSize(&pGroup->eaNotifies); j++)
				{
					LevelupNotification *pNotify = pGroup->eaNotifies[j];
					const char *pchMessage = TranslateMessageRef(pNotify->hMessage);
					const char *pchTexture = pNotify->pchTexture && *pNotify->pchTexture ? pNotify->pchTexture : NULL;
					if (pchMessage)
					{
						notify_NotifySend(pPlayerEnt, pNotify->eType != kNotifyType_Default ? pNotify->eType : kNotifyType_LevelUp_OtherInfo, pchMessage, NULL, pchTexture);
					}
				}
			}
		}
		estrDestroy(&esLevelUpKey);
	}

	//Incase the player unlocked another class with the level
	characterclasses_SendClassList(pPlayerEnt);
	ServerChat_PlayerUpdate(pPlayerEnt, CHATUSER_UPDATE_SHARD);

	if(pPlayerEnt && !!entity_BuildCanCreate((ATH_ARG NOCONST(Entity)*)pPlayerEnt))
	{
		entity_BuildCreate(pPlayerEnt);
	}

	// destroy the old project-specific log string, since it probably includes level
	estrDestroy(&pPlayerEnt->estrProjSpecificLogString);
	//log levelup
	if (gbEnableGamePlayDataLogging) {
		entLog(LOG_PLAYER, pPlayerEnt, "LevelUp", "Level %d, TimePlayed %g", level, pPlayerEnt->pPlayer->fTotalPlayTime);
	}

	// Alert the social media system
	gslSocialActivity(pPlayerEnt, kActivityType_LevelUp, (void*)(intptr_t)level);

	// add entry to player's persisted activity log
	gslActivity_AddLevelUpEntry(pPlayerEnt, level);
}

// Adapter for rewards given from expressions.  These could be from anywhere
void reward_ExprExec(Entity* pPlayerEnt, const char* RewardTableName, int level)
{
	InventoryBag **ppRewardBags = NULL;
	RewardTable *pRewardTable = NULL;

	pRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,RewardTableName);

	if ( pRewardTable )
	{
		RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

		if (pLocalContext)
		{	
			ItemChangeReason reason = {0};

			pLocalContext->type = RewardContextType_PowerExec;
			pLocalContext->pKilled = NULL;
			pLocalContext->RewardLevel = level;
			SetKillerRewardContext(pPlayerEnt, pLocalContext);

			reward_generate(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pLocalContext, pRewardTable, &ppRewardBags, NULL, NULL);

			inv_FillItemChangeReason(&reason, pPlayerEnt, "Misc:FromAnExpression", NULL);

			reward_GiveBags(pPlayerEnt, false, &ppRewardBags, &reason);

			//Cleanup the context
			reward_ContextCleanup(pLocalContext);
		}
	}
}

void reward_CraftingContextInitialize(Entity* pEnt, RewardContext *pRewardContext, int iEPValue, int iSkillLevelOffset)
{
	pRewardContext->pKilled = NULL;
	pRewardContext->RewardLevel = entity_GetSavedExpLevel(pEnt);
	MAX1(pRewardContext->RewardLevel, 1);
	SetKillerRewardContext(pEnt, pRewardContext);
	pRewardContext->SkillLevel -= iSkillLevelOffset;
	MAX1(pRewardContext->SkillLevel, 1);
	pRewardContext->EP = iEPValue;
}

// Gets the range for sharing kill credit with teammates according to region rules
static U32 reward_GetTeamKillCreditRange(Entity *pEnt)
{
	Vec3 vPos;
	WorldRegion *pRegion;

	if (pEnt){
		entGetPos(pEnt,vPos);

		pRegion = worldGetWorldRegionByPos(vPos);

		if (pRegion) {
			RegionRules *pRules = getRegionRulesFromRegionType(worldRegionGetType(pRegion));

			if ( pRules && pRules->uiTeamKillCreditDist != 0 ) {
				return pRules->uiTeamKillCreditDist;
			}
		}
	}

	return 350; // hardcoded default
}

static F32 reward_CombatValue(Entity *pAttacker, Entity *pTarget, S32 iAttackerExpLevel)
{
	F32 fCombatValue = 0.0f;
	if(pAttacker->pChar)
	{
		S32 iAttackerCombatLevel;

		// There have been valid cases where the attacker isn't loaded yet
		// If this is the case, just use the exp level as a fallback (without erroring)
		if(pAttacker->pChar->bLoaded)
		{
			iAttackerCombatLevel = entity_GetCombatLevel(pAttacker);
		}
		else
		{
			iAttackerCombatLevel = iAttackerExpLevel;
		}

		// Get the combat value by looking up in a table, if no table just use combat level
		if(g_RewardConfig.Modifications.pcCombatValueLevelTable)
		{
			CharacterClass *pClass = character_GetClassCurrent(pAttacker->pChar);
			const char *pchTable = g_RewardConfig.Modifications.pcCombatValueLevelTable;

			if(pClass)
			{
				fCombatValue = class_powertable_Lookup(pClass, pchTable, MAX(iAttackerCombatLevel-1,0));
			}
		}

		// bad or non-existent value, use combat level
		if(fCombatValue < 0.0001f)
		{
			fCombatValue = (F32)iAttackerCombatLevel;
		}

		if(pTarget->pChar && g_RewardConfig.Modifications.bModifyByDifficulty)
		{
			// get the difficulty modifier for the enemy
			CombatMod *pComMod = CombatMod_getMod(iAttackerCombatLevel, entity_GetCombatLevel(pTarget), true);
			if(pComMod)
			{
				fCombatValue *= pComMod->fMagnitude;
			}
		}
	}

	return fCombatValue;
}



// return the modified damage based on level difference
// This is use to penalize griefers
F32 reward_ModifiedDamageByLevelDifference(F32 fDamage, S32 iKillerLevel, S32 iKilledLevel)
{
	// if attacker is higher apply penalty from table
	if(iKillerLevel > iKilledLevel)
	{
		S32 diff = iKillerLevel - iKilledLevel;
		F32 fVal = powertable_Lookup(g_RewardConfig.Modifications.pcItemRewardModificationTable, diff - 1);
		if(fVal > 0.0f)
		{
			fDamage *= fVal;
		}
	}

	return fDamage;
}

void reward_KillCreditLimitTick(Entity* pEnt, F32 fElapsedTime)
{
	PERFINFO_AUTO_START_FUNC();

	if (pEnt->pPlayer)
	{
		RegionRules* pRules = getRegionRulesFromEnt(pEnt);

		if (pRules && pRules->pKillCreditLimit)
		{
			if (pEnt->pPlayer->iKillCreditCounter < pRules->pKillCreditLimit->iMaxKills)
			{
				pEnt->pPlayer->fKillCreditLimitAccum += fElapsedTime;
		
				while (pEnt->pPlayer->fKillCreditLimitAccum >= pRules->pKillCreditLimit->fUpdateRate)
				{
					pEnt->pPlayer->iKillCreditCounter++;
					pEnt->pPlayer->fKillCreditLimitAccum -= pRules->pKillCreditLimit->fUpdateRate;

					if (pEnt->pPlayer->iKillCreditCounter == pRules->pKillCreditLimit->iMaxKills)
					{
						pEnt->pPlayer->fKillCreditLimitAccum = 0.0f;
						break;
					}
				}
			}
			else if (pEnt->pPlayer->iKillCreditCounter > pRules->pKillCreditLimit->iMaxKills)
			{
				pEnt->pPlayer->iKillCreditCounter = pRules->pKillCreditLimit->iMaxKills;
			}
		}
		else
		{
			pEnt->pPlayer->iKillCreditCounter = 0;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Check to see if the player has exceeded the allowed kill credit limit
static bool reward_EntExceededKillCreditLimit(Entity* pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		RegionRules* pRules = getRegionRulesFromEnt(pEnt);

		if (pRules && pRules->pKillCreditLimit)
		{
			if (pEnt->pPlayer->iKillCreditCounter <= 0)
			{
				return true;
			}
		}
	}
	return false;
}

static bool reward_EntExceededUGCKillCreditLimit(Entity* pEnt)
{
	const UGCKillCreditLimit* pKillCreditLimit = ugcDefaultsGetKillCreditLimit();

	if (pKillCreditLimit && pEnt && pEnt->pPlayer && pEnt->pPlayer->pUGCKillCreditLimit && zmapIsUGCGeneratedMap(NULL))
	{
		S32 iSize = eaiSize(&pKillCreditLimit->piExpLimitPerLevel);
		U32 uCurrentTime = timeSecondsSince2000();
		U32 uTimeInterval = MAX(pKillCreditLimit->uTimeInterval, 1);
		U32 uTimestamp = pEnt->pPlayer->pUGCKillCreditLimit->uTimestamp;
		U32 uLastTimeIntervalIndex = uTimestamp / uTimeInterval;
		U32 uThisTimeIntervalIndex = uCurrentTime / uTimeInterval;
		S32 iExpLevel = entity_GetSavedExpLevel(pEnt);
		S32 iExpLimit;

		if (iExpLevel > iSize) {
			iExpLimit = pKillCreditLimit->piExpLimitPerLevel[iSize-1]; 
		} else {
			iExpLimit = pKillCreditLimit->piExpLimitPerLevel[iExpLevel-1];
		}
		if (uLastTimeIntervalIndex == uThisTimeIntervalIndex &&
			pEnt->pPlayer->pUGCKillCreditLimit->iExpEarned >= iExpLimit)
		{
			return true;
		}
	}
	return false;
}

static KillCreditEntity* reward_AddKillCreditEntity(KillCreditTeam* pCreditTeam, Entity* pMember, Entity* pKilled)
{
	KillCreditEntity *pCreditEntity = StructCreate(parse_KillCreditEntity);
	eaPush(&pCreditTeam->eaMembers, pCreditEntity);
	if (pMember)
	{
		S32 iMemberExpLevel = entity_GetSavedExpLevel(pMember);
		pCreditEntity->entRef = entGetRef(pMember);
		pCreditEntity->iContainerID = entGetContainerID(pMember);

		ent_GetCharacterRewardContextInfo(&pCreditEntity->CBIData, pMember);

		pCreditTeam->iTotalLevels += iMemberExpLevel;
	
		if (pKilled)
			pCreditTeam->fTotalCombatValue += reward_CombatValue(pMember, pKilled, iMemberExpLevel);
	}
	return pCreditEntity;
}

// Calculates credit percentages for all damage sources involved in a Kill
// Returns the total number of damage dealt
F32 reward_CalculateKillCredit(Entity *pKilled, KillCreditTeam ***peaTeams)
{
	KillCreditTeam *pHighestDamagingTeam = NULL;
	F32 fTotalDamage = 0.f;
	F32 fMaxDamage = 0.f;
	Vec3 KilledPos;
	U32 iTeamCreditDist = reward_GetTeamKillCreditRange(pKilled);  // Should this use each player's region instead of the critter's region?
	int iNumKillers = 0;
	int i, n, j = 0;
	Entity *pentKiller = NULL;
	int iPartitionIdx;

	if (!pKilled)
		return 0.f;

	iPartitionIdx = entGetPartitionIdx(pKilled);
	pentKiller = character_FindKiller(iPartitionIdx, pKilled->pChar, NULL);

	entGetPos(pKilled, KilledPos);

	// If this is a mission map, all teammates should get credit regardless of distance
	if (zmapInfoGetMapType(NULL) == ZMTYPE_MISSION || zmapInfoGetMapType(NULL) == ZMTYPE_OWNED)
		//TODO(BH): This should be done? ||zmapTypeGetMapType(NULL) == ZMTYPE_QUEUED_PVE)
	{
		iTeamCreditDist = 0;
	}

	// --- First Pass ---
	// Sum all damage for each player/team

	n = eaSize(&pKilled->pChar->ppDamageTrackers);
	for(i=n-1; i>=0; i--)
	{
		DamageTracker *pTracker = pKilled->pChar->ppDamageTrackers[i];
		Entity *pDamager = pTracker ? entFromEntityRef(iPartitionIdx, pTracker->erOwner) : NULL;
		Team *pTeam = team_GetTeam(pDamager);
		KillCreditTeam *pCreditTeam = NULL;
		U32 iTeamID = 0;
		bool bTeamUp = false;

		if ( !pTracker )
			 continue;

		// Don't ever give an entity credit for killing itself, or for killing a friendly target.
		// Also, don't count this damage in the final number
		if(pDamager && (pDamager == pKilled || !critter_IsFactionKOS(iPartitionIdx, pDamager, pKilled)))
			continue;

		// If this is a player and the player has exceeded the kill credit limit, don't count this damage in the final number
		if (pDamager && reward_EntExceededKillCreditLimit(pDamager))
			continue;

		if (pDamager && reward_EntExceededUGCKillCreditLimit(pDamager))
			continue;

		// Add to total damage
		fTotalDamage += pTracker->fDamage;

		// Figure out TeamID to use
		if ( !pDamager || !pDamager->pPlayer ){
			//if no damager then it is world damager
			//if not a player then this is a critter which is also grouped as world damage
			//world team ID is U32_MAX
			iTeamID = U32_MAX;
		} else if (pDamager && pDamager->pTeamUpRequest && pDamager->pTeamUpRequest->eState == kTeamUpState_Member) {
			iTeamID = pDamager->pTeamUpRequest->uTeamID;
			bTeamUp = true;
		} else if (pTeam && pTeam->iContainerID){
			iTeamID = pTeam->iContainerID;
		} else {
			//if not on team, create a dummy team for each player that did damage
			iTeamID = 0;
		}

		//look for existing team
		for(j=0;j<eaSize(peaTeams);j++)
		{
			if((*peaTeams)[j]->bTeamUp != bTeamUp)
				continue;

			if ( iTeamID && ((*peaTeams)[j]->iTeamID == iTeamID)){
				pCreditTeam = (*peaTeams)[j];
				break;
			} else if (!iTeamID && !(*peaTeams)[j]->iTeamID && (*peaTeams)[j]->firstMemberEntRef == pTracker->erOwner){
				pCreditTeam = (*peaTeams)[j];
				break;
			}
		}
		if (!pCreditTeam)
		{
			// Need to add a new team
			pCreditTeam = StructCreate(parse_KillCreditTeam);
			pCreditTeam->iTeamID = iTeamID;
			pCreditTeam->firstMemberEntRef = pTracker->erOwner;
			pCreditTeam->iValidTeamSize = 1;	// default size
			pCreditTeam->bTeamUp = bTeamUp;
			eaPush(peaTeams, pCreditTeam);

			// Add empty entries for all nearby teammates, so that they
			// will get credit even if they haven't done damage
			if(bTeamUp)
			{
				int iMember;
				int iGroup;
				TeamUpInstance *pInstance = gslTeamUpInstance_FromKey(iPartitionIdx,pDamager->pTeamUpRequest->uTeamID);

				for(iGroup=0;iGroup<eaSize(&pInstance->ppGroups);iGroup++)
				{
					pCreditTeam->iTeamCombatLevel = encounter_getTeamLevelInRange(pDamager, &pCreditTeam->iValidTeamSize, true);

					for(iMember=0;iMember<eaSize(&pInstance->ppGroups[iGroup]->ppMembers);iMember++)
					{
						Entity *pMember = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pInstance->ppGroups[iGroup]->ppMembers[iMember]->iEntID);
						Vec3 MemberPos;

						if(pMember)
							entGetPos(pMember,MemberPos);
						if (pMember && (!iTeamCreditDist || distance3XZ(MemberPos, KilledPos) < iTeamCreditDist)){
							reward_AddKillCreditEntity(pCreditTeam, pMember, pKilled);
						}
					}
				}
				
			}
			else if (pTeam && pDamager && pDamager->pTeam && pDamager->pTeam->eState == TeamState_Member){
				int iMember;

				pCreditTeam->iTeamCombatLevel = encounter_getTeamLevelInRange(pDamager, &pCreditTeam->iValidTeamSize, true);

				for (iMember = 0; iMember < eaSize(&pTeam->eaMembers); iMember++){
					Entity * pMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[iMember]->iEntID);
					Vec3 MemberPos;

					if(pMember && pMember->pTeamUpRequest && pMember->pTeamUpRequest->eState == kTeamUpState_Member)
						continue;

					if (pMember)
						entGetPos(pMember, MemberPos);
					if (pMember && (!iTeamCreditDist || distance3XZ(MemberPos, KilledPos) < iTeamCreditDist)){
						reward_AddKillCreditEntity(pCreditTeam, pMember, pKilled);
					}
				}
			}
			else
			{
				// no team so combat level is that of this character
				if(pDamager && pDamager->pChar)
				{
					if (gConf.bEncountersScaleWithActivePets && pDamager->pPlayer)
					{
						pCreditTeam->iTeamCombatLevel = encounter_getTeamLevelInRange(pDamager, &pCreditTeam->iValidTeamSize, true);
					}
					else
					{
						pCreditTeam->iTeamCombatLevel = entity_GetCombatLevel(pDamager);
					}					
				}
				else
				{
					pCreditTeam->iTeamCombatLevel = 0;	// just a dummy value, should not be used as this is a death not caused by a player
				}
			}
		}

		// Add Team damage
		if (pCreditTeam)
			pCreditTeam->fTotalTeamDamage += pTracker->fDamage;


		// Add damage for this Entity
		if (pCreditTeam)
		{
			for (j = 0; j < eaSize(&pCreditTeam->eaMembers); j++)
			{
				if (pCreditTeam->eaMembers[j]->entRef == pTracker->erOwner)
				{
					pCreditTeam->eaMembers[j]->fTotalDamageSelf += pTracker->fDamage;
					
					if(pentKiller && pCreditTeam->eaMembers[j]->entRef == entGetRef(pentKiller))
					{
						pCreditTeam->eaMembers[j]->bFinalBlow = true;
					}

					break;
				}
			}

			if (j == eaSize(&pCreditTeam->eaMembers))
			{
				KillCreditEntity *pCreditEntity = reward_AddKillCreditEntity(pCreditTeam, pDamager, pKilled);
				
				pCreditEntity->fTotalDamageSelf = pTracker->fDamage;
				
				if(pentKiller && pCreditEntity->entRef == entGetRef(pentKiller))
				{
					pCreditEntity->bFinalBlow = true;
				}
			}
		}
	}

	// ****  TODO - Add Healing numbers from the Critter's AIBase?

	// --- Second Pass ---
	// Find highest damaging team or player
	// Compute % damage for each player
	// Compute % damage for each team member

	for(i=0; i<eaSize(peaTeams); i++)
	{
		KillCreditTeam *pCreditTeam = (*peaTeams)[i];
		F32 fAdjustedTotalTeamDamage = pCreditTeam->fTotalTeamDamage;

		// Check to see if we should adjust team damage by level
		if(g_RewardConfig.Modifications.pcItemRewardModificationTable)
		{
			fAdjustedTotalTeamDamage = reward_ModifiedDamageByLevelDifference(fAdjustedTotalTeamDamage, pCreditTeam->iTeamCombatLevel, entity_GetCombatLevel(pKilled));
		}

		//compute highest damaging team
		if (fAdjustedTotalTeamDamage > fMaxDamage )
		{
			fMaxDamage = fAdjustedTotalTeamDamage;
			pHighestDamagingTeam = pCreditTeam;
		}

		// Compute total Team percentage
		pCreditTeam->fTotalTeamPercentage = (fTotalDamage>0)?(pCreditTeam->fTotalTeamDamage/fTotalDamage):0.f;

		// Compute percentages for each entity
		for (j = 0; j < eaSize(&pCreditTeam->eaMembers); j++)
		{
			KillCreditEntity *pCreditEnt = pCreditTeam->eaMembers[j];
			Entity *pDamager = entFromEntityRef(iPartitionIdx, pCreditEnt->entRef);
			F32 fKillPower;

			// -- Solo credit --
			// Compute average Self damage
			pCreditEnt->fPercentCreditSelf = (fTotalDamage>0)?(pCreditEnt->fTotalDamageSelf/fTotalDamage):0.f;

			// -- Team credit --
			// KDB iLevel = entity_GetSavedExpLevel(pDamager);
			fKillPower = pDamager?reward_CombatValue(pDamager, pKilled, entity_GetSavedExpLevel(pDamager)):0.f;

			if(pCreditTeam->fTotalCombatValue > 0.f){
				pCreditEnt->fPercentOfTeamCredit = ( fKillPower / pCreditTeam->fTotalCombatValue );
			}else{
				pCreditEnt->fPercentOfTeamCredit = 0.f;
			}
			pCreditEnt->fMyTeamDamageShare = pCreditEnt->fPercentOfTeamCredit * pCreditTeam->fTotalTeamDamage;
			pCreditEnt->fPercentCreditTeam = pCreditEnt->fPercentOfTeamCredit * pCreditTeam->fTotalTeamPercentage;
		}

	}
	if (pHighestDamagingTeam)
		pHighestDamagingTeam->bHighestDamager = true;

	// Find the total number of damage sources
	for (i = 0; i < eaSize(peaTeams); i++){
		iNumKillers += eaSize(&(*peaTeams)[i]->eaMembers);
	}

	// Determine whether each player has "credit" for Missions, etc. based on a threshold
	for (i = 0; i < eaSize(peaTeams); i++)
	{
		for (j = 0; j < eaSize(&(*peaTeams)[i]->eaMembers); j++)
		{
			KillCreditEntity *pCreditEnt = (*peaTeams)[i]->eaMembers[j];

			if (!gameevent_AreAssistsEnabled())
			{
				pCreditEnt->bHasCredit = (pCreditEnt->fPercentCreditSelf >= 1.f/(2*iNumKillers+1));
			}
			else
			{	// hard-rule if assists are enabled, always give credit
				pCreditEnt->bHasCredit = true;
			}
			
			pCreditEnt->bHasTeamCredit = ((*peaTeams)[i]->fTotalTeamPercentage >= 1.f/(2*iNumKillers+1));

			// If this is a player and they received any credit for the kill, decrement the kill credit counter
			if (pCreditEnt->bHasCredit || pCreditEnt->bHasTeamCredit) 
			{
				Entity* pEnt = entFromEntityRefAnyPartition(pCreditEnt->entRef);
				if (pEnt && pEnt->pPlayer) 
				{
					RegionRules* pRules = getRegionRulesFromEnt(pEnt);
					if (pRules && pRules->pKillCreditLimit) 
					{
						pEnt->pPlayer->iKillCreditCounter = MAX(pEnt->pPlayer->iKillCreditCounter-1, 0);
					}
				}
			}
		}
	}

	return fTotalDamage;
}

// just broke this out to make the code a little simpler; we can assume the bag is not empty
//
// this returns the new value that should be set on the team reward index
static U32 GiveRewardForTeamWithKilledEntity(int iPartitionIdx, InventoryBag *bag, Team *team, Entity *pKiller, Vec3 DropPos, int iCurrentRewardIdx, RewardContext* pContext, Entity *pKilledEntity, bool bCreateCritter, KillCreditTeam *pKillTeam, bool bDefaultToRoundRobin)
{
	CritterLootOwner **peaOwners = NULL;
	ItemQuality eMaxQuality = 0;
	Item **peaRoundRobinItems = NULL;
	Entity **peaIneligibleMembers = NULL;
	int *ent_ids = NULL;
	int *peaiRoundRobinIds = NULL;
	int iNumFailedAssignments = 0;
	int i, j;
	LootMode eMode;
	if (!SAFE_MEMBER(bag,pRewardBagInfo) || (bCreateCritter && !pKiller) || !pKillTeam)
	{
		return iCurrentRewardIdx;
	}

	if(!team && pKillTeam->bTeamUp == false)
	{
		return iCurrentRewardIdx;
	}

	eMode = team ? team->loot_mode : LootMode_FreeForAll;

	// check if this reward just isn't meant to be an interactable (i.e. XP) or clickable
	// designers probably shouldn't have rare loot be anything but interactables or clickables for
	// inventory capacity reasons, so if you get here on a designer request to change
	// this behavior, think hard before changing this.
	// TODO (JDJ): we might have to split these rewards according to round-robin rules as well;
	// for now, next person in line gets the entire bag
	// TODO (JDJ): also fix updating of reward index correctly because the usage of the bGiveItems
	// flag in the reward context makes it hard to determine whether the entire team is getting
	// the reward or just one player, so we don't know how to update the reward index
	if (bag->pRewardBagInfo->PickupType != kRewardPickupType_Interact && bag->pRewardBagInfo->PickupType != kRewardPickupType_Clickable)
	{
		// increment index for player-type rollovers
		if (bag->pRewardBagInfo->PickupType == kRewardPickupType_Rollover &&
			bag->pRewardBagInfo->OwnerType == kRewardOwnerType_Player)
			iCurrentRewardIdx = (iCurrentRewardIdx + 1) % eaSize(&pKillTeam->eaMembers);

		if (bCreateCritter)
		{ 
			ItemChangeReason reason = {0};
			if (pKilledEntity)
				inv_FillItemChangeReasonKill(&reason, pKiller, pKilledEntity);
			else
				inv_FillItemChangeReason(&reason, pKiller, "Loot:GiveRewardBagForTeam", NULL);
			reward_GiveBagWithKilledEntity(pKiller, bag, DropPos, pKilledEntity, &reason);
		}

		return iCurrentRewardIdx;
	}

	// only apply team loot modes for reward tables/bags with "team" or "player" owner types;
	// all other types are given
	// TODO (JDJ): talk with designers about desired distinction between "player" and "team" owner
	// types and adjust behavior accordingly
	if (bag->pRewardBagInfo->OwnerType != kRewardOwnerType_Player &&
		bag->pRewardBagInfo->OwnerType != kRewardOwnerType_Team)
	{
		if (bCreateCritter)
		{
			ItemChangeReason reason = {0};
			if (pKilledEntity)
				inv_FillItemChangeReasonKill(&reason, pKiller, pKilledEntity);
			else
				inv_FillItemChangeReason(&reason, pKiller, "Loot:GiveRewardBagForTeam", NULL);
			reward_GiveBagWithKilledEntity(pKiller, bag, DropPos, pKilledEntity, &reason);
		}
		return iCurrentRewardIdx;
	}
	
	// determine the highest quality in the bag and figure out which items need to be given round-robin
	// remove unique items that no one can use during this loop check
	for(i = eaSize(&bag->ppIndexedInventorySlots) - 1; i>=0; --i)
	{
		ItemDef *def;
		if(!bag->ppIndexedInventorySlots[i])
			continue;
		else if(!bag->ppIndexedInventorySlots[i]->pItem)//pItem was sometimes null and causing a crash.
			continue;
		def = GET_REF(bag->ppIndexedInventorySlots[i]->pItem->hItem);
		if(!def)
			continue;
			
		if((def->flags & kItemDefFlag_Unique) != 0)
		{
			S32 iTeamIdx;
			bool bDelete = true;
			if(team)
			{
				for(iTeamIdx = 0; iTeamIdx < eaSize(&team->eaMembers); ++iTeamIdx)
				{
					TeamMember *pTeamMemeber = team->eaMembers[iTeamIdx];
					if(pTeamMemeber)
					{
						Entity *pTeamEnt = GET_REF(pTeamMemeber->hEnt);
						if(pTeamEnt && inv_ent_AllBagsCountItems(pTeamEnt, def->pchName) == 0)
						{
							bDelete = false;
							break;
						}
					}
				}
			}
			else
			{
				//Team up
				for(iTeamIdx = 0; iTeamIdx < eaSize(&pKillTeam->eaMembers); ++iTeamIdx)
				{
					KillCreditEntity *pTeamMember = pKillTeam->eaMembers[iTeamIdx];
					if(pTeamMember)
					{
						Entity *pTeamEnt = entFromEntityRef(iPartitionIdx,pTeamMember->entRef);
						if(pTeamEnt && inv_ent_AllBagsCountItems(pTeamEnt, def->pchName) == 0)
						{
							bDelete = false;
							break;
						}
					}
				}
			}
			
			if(bDelete)
			{
				rewardbag_RemoveItem(bag, i, NULL);
				continue;
			}
		}
		
		if(team && bDefaultToRoundRobin)
		{
			if (eMode == LootMode_RoundRobin ||
				(eMode != LootMode_FreeForAll && item_GetQuality(bag->ppIndexedInventorySlots[i]->pItem) < StaticDefineIntGetInt(ItemQualityEnum, team->loot_mode_quality)))
			{
				eaPush(&peaRoundRobinItems, bag->ppIndexedInventorySlots[i]->pItem);
			}
		}
		else if(pKillTeam->bTeamUp)
		{
			eaPush(&peaRoundRobinItems, bag->ppIndexedInventorySlots[i]->pItem);
		}
		
		eMaxQuality = MAX(eMaxQuality, item_GetQuality(bag->ppIndexedInventorySlots[i]->pItem));
	}

	// we assign items below the team loot threshold to individual team members via
	// round-robin (unless the loot mode is free-for-all); for each member in the team
	// whose round-robin turn is up, we find an eligible item and repeat until all items are
	// allocated
	while (eaSize(&peaRoundRobinItems) > 0 && iNumFailedAssignments < eaSize(&pKillTeam->eaMembers))
	{
		Entity *pMember = NULL;
		bool bAssignedItem = false;

		iCurrentRewardIdx = iCurrentRewardIdx % eaSize(&pKillTeam->eaMembers);
		if (iCurrentRewardIdx >= 0 && iCurrentRewardIdx < eaSize(&pKillTeam->eaMembers) && pKillTeam->eaMembers[iCurrentRewardIdx])
		{
			pMember = entFromEntityRef(iPartitionIdx, pKillTeam->eaMembers[iCurrentRewardIdx]->entRef);
		}

		// find an item the member can take
		if (pMember && eaFind(&peaIneligibleMembers, pMember) < 0)
		{
			for (i = eaSize(&peaRoundRobinItems) - 1; i >= 0; i--)
			{
				// TODO (JDJ): create a function for the check below - check: mission applicability
				// if (pMember can take item)
				// added check so not to give unique item to player who already has it
				ItemDef *pDef = GET_REF(peaRoundRobinItems[i]->hItem);
				if(pDef && ((pDef->flags & kItemDefFlag_Unique) == 0 || inv_ent_AllBagsCountItems(pMember, pDef->pchName) == 0))
				{
					peaRoundRobinItems[i]->owner = pMember->myContainerID;
					eaRemove(&peaRoundRobinItems, i);
					eaiPushUnique(&peaiRoundRobinIds, pMember->myContainerID);
					iNumFailedAssignments = 0;
					bAssignedItem = true;
					break;
				}
			}
		}

		if (!bAssignedItem)
			iNumFailedAssignments++;

		if (!g_RewardConfig.bRoundRobinEachKill)
		{
			// get the next member
			iCurrentRewardIdx = (iCurrentRewardIdx + 1) % eaSize(&pKillTeam->eaMembers);
		}
	}

	// if some items went unassigned, find and remove them
	if (iNumFailedAssignments == eaSize(&pKillTeam->eaMembers))
	{
		for (i = eaSize(&peaRoundRobinItems) - 1; i >= 0; i--)
		{
			for (j = eaSize(&bag->ppIndexedInventorySlots) - 1; j >= 0; j--)
			{
				if (!bag->ppIndexedInventorySlots[j])
					continue;

				if (bag->ppIndexedInventorySlots[j]->pItem == peaRoundRobinItems[i])
					rewardbag_RemoveItem(bag, j, NULL);
			}
		}
	}

	// update gained loot on players to continue round-robin cycle
	reward_UpdateLootGained(iPartitionIdx, bag);

	// add round-robin item owners to the loot
	if (eaiSize(&peaiRoundRobinIds) > 0)
	{
		devassertmsg(eaSize(&peaOwners) == 0, "Previous loot owners exist where code assumes they don't.");
		eaPush(&peaOwners, StructCreate(parse_CritterLootOwner));
		peaOwners[0]->eOwnerType = kRewardOwnerType_Player;
		eaiCopy(&peaOwners[0]->peaiOwnerIDs, &peaiRoundRobinIds);
	}

	if (s_RewardLog)
		reward_logBag(iPartitionIdx, bag, pContext, NULL);

	// if threshold not met, give like normal round robin
	if (team && bDefaultToRoundRobin && eMaxQuality < StaticDefineIntGetInt(ItemQualityEnum, team->loot_mode_quality) && eMode != LootMode_FreeForAll)
	{
		bag->pRewardBagInfo->loot_mode = LootMode_RoundRobin;
		//Insurance against trying to grant an item with NO owners, which will crash. 
		//This shouldn't happen at all now that free-for-all looting isn't allowed in here, 
		//but better safe than sorry.
		if (eaSize(&peaOwners) == 0)
		{
			eaPush(&peaOwners, StructCreate(parse_CritterLootOwner));
			peaOwners[0]->eOwnerType = kRewardOwnerType_Player;
			team_GetOnMapEntIds(iPartitionIdx, &peaOwners[0]->peaiOwnerIDs, team);
		}
		if (bCreateCritter)
			reward_GiveInteractibleWithKilledEntity(peaOwners, bag, DropPos, entGetPartitionIdx(pKiller), pKiller, pKilledEntity, false);
	}
	else
	{
		// special team behavior:
		// this bag contains an item of rare enough quality
		// that a special interactible will be created just to
		// allow the team to interact with it in a manner
		// they see fit.
		if(team)
		{
			switch(eMode)
			{
				xcase LootMode_NeedOrGreed:
			{
				// add all team ents to round robin owner IDs
				if (eaSize(&peaOwners) == 0)
				{
					eaPush(&peaOwners, StructCreate(parse_CritterLootOwner));
					peaOwners[0]->eOwnerType = kRewardOwnerType_Player;
				}
				team_GetOnMapEntIds(iPartitionIdx, &peaOwners[0]->peaiOwnerIDs, team);

				bag->pRewardBagInfo->loot_mode = LootMode_NeedOrGreed;
				bag->pRewardBagInfo->eLootModeThreshold = StaticDefineIntGetInt(ItemQualityEnum, team->loot_mode_quality);
				if (bCreateCritter)
					reward_GiveInteractibleWithKilledEntity(peaOwners, bag, DropPos, entGetPartitionIdx(pKiller), pKiller, pKilledEntity, false);
			}
			xcase LootMode_MasterLooter:
			{
				// add team leader ownership
				CritterLootOwner *pLeaderOwner = NULL;
				Entity *leader = team_GetTeamLeader(iPartitionIdx, team);
				if (!leader)
				{
					if (bCreateCritter)
					{
						ItemChangeReason reason = {0};
						if (pKilledEntity)
							inv_FillItemChangeReasonKill(&reason, pKiller, pKilledEntity);
						else
							inv_FillItemChangeReason(&reason, pKiller, "Loot:GiveRewardBagForTeam", NULL);
						reward_GiveBagWithKilledEntity(pKiller, bag, DropPos, pKilledEntity, &reason);
					}
					break;
				}
				pLeaderOwner = StructCreate(parse_CritterLootOwner);
				pLeaderOwner->eOwnerType = kRewardOwnerType_TeamLeader;
				eaiPush(&pLeaderOwner->peaiOwnerIDs, team->iContainerID);
				eaPush(&peaOwners, pLeaderOwner);

				bag->pRewardBagInfo->loot_mode = LootMode_MasterLooter;
				bag->pRewardBagInfo->eLootModeThreshold = StaticDefineIntGetInt(ItemQualityEnum, team->loot_mode_quality);
				if (bCreateCritter)
					reward_GiveInteractibleWithKilledEntity(peaOwners, bag, DropPos, entGetPartitionIdx(pKiller), pKiller, pKilledEntity, false);
			}
			xcase LootMode_FreeForAll:
			{
				// there should have been no round-robin owners; we assign current
				// team members to own this bag (lootable even after members have left
				// team)
				devassertmsg(eaSize(&peaOwners) == 0, "Previous loot owners exist where code assumes they don't.");
				eaPush(&peaOwners, StructCreate(parse_CritterLootOwner));
				peaOwners[0]->eOwnerType = kRewardOwnerType_Player;
				team_GetOnMapEntIds(iPartitionIdx, &peaOwners[0]->peaiOwnerIDs, team);

				bag->pRewardBagInfo->loot_mode = LootMode_FreeForAll;
				if (bCreateCritter)
					reward_GiveInteractibleWithKilledEntity(peaOwners, bag, DropPos, entGetPartitionIdx(pKiller), pKiller, pKilledEntity, false);
			}
			xcase LootMode_RoundRobin:
			{
				// just use the round robin owners we acquired above
				bag->pRewardBagInfo->loot_mode = LootMode_RoundRobin;
				if (bCreateCritter)
					reward_GiveInteractibleWithKilledEntity(peaOwners, bag, DropPos, entGetPartitionIdx(pKiller), pKiller, pKilledEntity, false);
			}
xdefault:
			if (bCreateCritter)
			{
				ItemChangeReason reason = {0};
				if (pKilledEntity)
					inv_FillItemChangeReasonKill(&reason, pKiller, pKilledEntity);
				else
					inv_FillItemChangeReason(&reason, pKiller, "Loot:GiveRewardBagForTeam", NULL);
				reward_GiveBagWithKilledEntity(pKiller, bag, DropPos, pKilledEntity, &reason);
			}
			break;
			};
		} 
		else
		{
			// teamups use roundrobin
			// just use the round robin owners we acquired above
			bag->pRewardBagInfo->loot_mode = LootMode_RoundRobin;
			if (bCreateCritter)
				reward_GiveInteractibleWithKilledEntity(peaOwners, bag, DropPos, entGetPartitionIdx(pKiller), pKiller, pKilledEntity, false);
		}
		
	}

	// cleanup
	eaDestroyStruct(&peaOwners, parse_CritterLootOwner);
	eaiDestroy(&peaiRoundRobinIds);
	ea32Destroy(&ent_ids);
	eaDestroy(&peaRoundRobinItems);
	eaDestroy(&peaIneligibleMembers);

	return iCurrentRewardIdx;
}

static U32 GiveRewardForTeam(int iPartitionIdx, InventoryBag *bag, Team *team, Entity *pKiller, Vec3 DropPos, int iCurrentRewardIdx, RewardContext* pContext, bool bCreateCritter, KillCreditTeam *pKillTeam, bool bDefaultToRoundRobin)
{
	return GiveRewardForTeamWithKilledEntity(iPartitionIdx, bag, team, pKiller, DropPos, iCurrentRewardIdx, pContext, NULL, bCreateCritter, pKillTeam, bDefaultToRoundRobin);
}

static RewardTable* get_dropelt_reward(Entity *killed, ItemGenRarity quality)
{
	char const *stem = NULL;
	RewardTable *res = NULL;
	char const *quality_name = NULL;
	char *reward_name = NULL;
	int i,n;
	DropSet *set = NULL; // rank ? RefSystem_ReferentFromString(g_hDropSetDict,rank) : NULL;
	DropSetElt *elt;
	F32 *weights = 0;
	CritterDef *critter_def = NULL;

	critter_def = SAFE_GET_REF2(killed, pCritter,critterDef);
	quality_name = StaticDefineIntRevLookup(ItemGenRarityEnum,quality);

	if(!quality_name)
	{
		Errorf("couldn't get str for quality %i", quality);
		return NULL;
	}

	if(!critter_def)
		return NULL;

	stem = critter_def->pchName;
	set = RefSystem_ReferentFromString(g_hDropSetDict,stem);
	rewardtrace("dropset: %sfound for critter %s",(set?"":"none "),stem);

	if(!set)
	{
		CritterGroup *group = GET_REF(critter_def->hGroup);
		if(group)
		{
			stem = group->pchName;
			set = RefSystem_ReferentFromString(g_hDropSetDict,stem);
			rewardtrace("dropset: %sfound for group %s",(set?"":"none "),stem);
		}
	}

	if(!set)
	{
		stem = killed->pCritter->pcRank;
		set = RefSystem_ReferentFromString(g_hDropSetDict,stem);
		rewardtrace("dropset: %sfound for rank %s",(set?"":"none "),stem);
	}


	if(!set)
	{
		rewardtrace("dropset: none found, aborting");
		return NULL;
	}

	n = eaSize(&set->elts);
	eafSetCapacity(&weights,n);
	for(i = 0; i<n; ++i)
	{
		elt = set->elts[i];
		if(!elt)
			continue;

		// if the reward doesn't exist at this rarity, skip.
		estrPrintf(&reward_name,"%s_%s",elt->name,quality_name);
		if(!RefSystem_ReferentFromString(g_hRewardTableDict, reward_name))
		{
			rewardtrace("weights: skipping reward %s: not found",reward_name);
			eafPush(&weights,0.f);
			continue;
		}

		eafPush(&weights,elt->weight);
	}

	i = reward_weightWalker(weights);
	elt = eaGet(&set->elts,i);
	eafDestroy(&weights);

	if(!elt)
	{
		Errorf("no dropset element selected.") ;
		rewardtrace("dropelt: no dropset element selected");
		return NULL;
	}


	estrPrintf(&reward_name,"%s_%s",elt->name,quality_name);
	res = RefSystem_ReferentFromString(g_hRewardTableDict, reward_name);
	estrDestroy(&reward_name);

	rewardtrace("dropset reward: picked %s",SAFE_MEMBER(res,pchName));

	return res;
}
static ItemGenRarity gen_get_rarity(Entity *killed)
{
	ItemGenRarity	      quality	= 0;
	F32					  roll;
	F32					  w;
	F32					  sum;
	int					  i;
	RewardTable*		  res       = NULL;
	const char *		  pcRank    = NULL;
	const char *		  pcSubRank = NULL;
	DropRateTable		 *droprate  = NULL;
	DropRateQuality		**qualities = NULL;
	int killed_level;
	CritterDef *critter_def = NULL;
	PlayerDifficultyMapData* pDifficultyData;

	if (!SAFE_MEMBER(killed,pCritter))
		return 0;

	pcRank       = killed->pCritter->pcRank;
	pcSubRank    = killed->pCritter->pcSubRank;
	killed_level = GetEntLevel(killed);
	critter_def = GET_REF(killed->pCritter->critterDef);
	pDifficultyData = pd_GetDifficultyMapData(mapState_GetDifficulty(mapState_FromEnt(killed)), zmapInfoGetPublicName(NULL), entGetWorldRegionTypeOfEnt(killed));

	if(!critter_def)
		return 0;

	droprate = RefSystem_ReferentFromString(g_hDropRateTableDict,critter_def->pchName);

	if(!droprate)
	{
		CritterGroup *group = GET_REF(critter_def->hGroup);
		rewardtrace("droprate: none found for critter %s. trying group",critter_def->pchName);
		if(group)
			droprate = RefSystem_ReferentFromString(g_hDropRateTableDict,group->pchName);
		rewardtrace("droprate: %sfound for group %s.",(droprate?"":"none "),SAFE_MEMBER(group,pchName));
	}

	if(!droprate)
		droprate = RefSystem_ReferentFromString(g_hDropRateTableDict,pcRank);

	if(!droprate)
	{
		rewardtrace("droprate: none found for rank %s. giving up.",pcRank);
		return 0;
	}


	rewardtrace("droprate %s found", droprate->name);

	// ----------
	// figure out the drop quality

	for(i = eaSize(&droprate->tiers)-1; i>=0; --i)
	{
		DropRateTier *drt = droprate->tiers[i];
		RewardTier *tier = SAFE_GET_REF(drt,tier);
		if(!tier)
			continue;
		if(!INRANGE(killed_level,tier->min_level,tier->max_level+1))
			continue;
		qualities = drt->qualities;  // @todo -AB: overlapping tiers :08/21/09
		rewardtrace("droptier: %s found for rolling quality",tier->name);
		break;
	}

	if(!qualities)
		return 0;

	// Determine sum of drop rates
	sum = 0;
	for(i = eaSize(&qualities)-1; i>=0; --i)
	{
		DropRateQuality *qual = qualities[i];
		float fMultiplier = 1;
		if(pDifficultyData && pDifficultyData->eaDropRateMultipliers)
		{
			int j;
			for(j = eaSize(&pDifficultyData->eaDropRateMultipliers)-1; j >= 0; j--)
			{
				if(qual->quality == (ItemGenRarity)pDifficultyData->eaDropRateMultipliers[j]->eQuality)
					break;
			}
			if(j >= 0)
			{
				fMultiplier = pDifficultyData->eaDropRateMultipliers[j]->fMultiplier;
			}
		}
		sum += (qual->rate*fMultiplier);
	}

	// Roll to determine the drop quality
	roll = randomPositiveF32()*sum;	// [0,sum)
	w    = 0.f;
	for(i = eaSize(&qualities)-1; i>=0; --i)
	{
		DropRateQuality *qual = qualities[i];
		float fMultiplier = 1;
		if(pDifficultyData && pDifficultyData->eaDropRateMultipliers)
		{
			int j;
			for(j = eaSize(&pDifficultyData->eaDropRateMultipliers)-1; j >= 0; j--)
			{
				if(qual->quality == (ItemGenRarity)pDifficultyData->eaDropRateMultipliers[j]->eQuality)
					break;
			}
			if(j >= 0)
			{
				fMultiplier = pDifficultyData->eaDropRateMultipliers[j]->fMultiplier;
			}
		}
		if(roll > w+(qual->rate*fMultiplier))
		{
			w += (qual->rate*fMultiplier);
			continue;
		}

		quality = qual->quality;
		break;
	}
	if(!quality)
	{
		rewardtrace("dropquality: roll %f. no quality passed",roll);
	}
	return quality;
}

static RewardTable* gen_dropsystem_reward(Entity *killed)
{
	ItemGenRarity quality = gen_get_rarity(killed);
	if(!quality)
	{
		
		return NULL; // no quality passed.
	}

	rewardtrace("dropquality: roll %f. quality %s picked",StaticDefineIntRevLookup(ItemGenRarityEnum,quality));

	// ----------
	// quality determined, now find the element from the drop set

	return get_dropelt_reward(killed,quality);
}

static bool reward_CanAddRegionTable(RewardTable *pRewardTable)
{
	S32 i;
	if(!pRewardTable)
	{
		// no killed reward table so allow it
		return true;
	}

	// check to see if killed table is ok to add region reward
	for(i = 0; i < eaSize(&g_RewardConfig.eaBlockRegionTables); ++i)
	{
		if(stricmp(pRewardTable->pchName, g_RewardConfig.eaBlockRegionTables[i]->pcRewardTable) == 0)
		{
			// don't generate region rewards
			return false;
		}
	}

	return true;
}

//determine which reward table to use for the killed critter
//the order is;
//	table on critter
//  table on critter group
//  default table based on rank
//  default critter kill table
static RewardTable** gen_killed_rewardtables(Entity *pKilled)
{
	RewardTable **reward_tables = NULL;
	Critter *killed_critter = pKilled ? pKilled->pCritter : NULL;
	CritterDef *killed_critterDef = killed_critter ? GET_REF(killed_critter->critterDef) : NULL;
	CritterGroup *killed_critterGroup = killed_critterDef ? GET_REF(killed_critterDef->hGroup) : NULL;
	RewardTable *critter_addl_table = killed_critterDef ? GET_REF(killed_critterDef->hAddRewardTable) : NULL;
	RewardTable *crittergroup_addl_table = killed_critterGroup ? GET_REF(killed_critterGroup->hAddRewardTable) : NULL;
	char const *mapname;
	RewardTable *map_addl_table; // ... look up map name. zmapGetNam

	RewardTable *killed_table = NULL;
	char *tmp = NULL;
	killed_table = killed_critterDef ? GET_REF(killed_critterDef->hRewardTable) : NULL;

	rewardtrace("generating tables for kill");

	if (critter_addl_table && killed_critter->eRewardType != kWorldEncounterRewardType_OverrideStandardRewards)
	{
		eaPush(&reward_tables,critter_addl_table);
		rewardtrace("adding critter additional table: %s",critter_addl_table->pchName);
	}

	if (crittergroup_addl_table && killed_critter->eRewardType != kWorldEncounterRewardType_OverrideStandardRewards)
	{
		eaPushIfNotNull(&reward_tables,crittergroup_addl_table);
		rewardtrace("addding group additional table: %s", crittergroup_addl_table->pchName);
	}


	//check for a reward table on the killed critters group
	if (!killed_table && killed_critterGroup)
		killed_table =  GET_REF(killed_critterGroup->hRewardTable);

	//check for a reward table based on critter rank
	if (!killed_table && killed_critter && killed_critter->eRewardType != kWorldEncounterRewardType_OverrideStandardRewards)
	{
		const char *pcRank = NULL;
		const char *pcSubRank = NULL;
		char * TmpName;
		RewardTable *dropsys_table = NULL;

		pcRank = killed_critter->pcRank;
		pcSubRank = killed_critter->pcSubRank;

		if (!pcRank)
		{
			pcRank = g_pcCritterDefaultRank;
		}

		estrStackCreate(&TmpName);

		estrPrintf(&TmpName, "%s", pcRank);
		// @todo -AB: for STO only look up by rank :08/11/09
		if (pcSubRank && killed_critter->pcSubRank != g_pcCritterDefaultSubRank )
		{
			estrAppend2(&TmpName, "_");
			estrAppend2(&TmpName, pcSubRank);
		}
		killed_table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,TmpName);
		if(killed_table)
			rewardtrace("rank based reward %s: found",TmpName);
		else
			rewardtrace("rank based reward %s: not found",TmpName);

		dropsys_table = gen_dropsystem_reward(pKilled);
		eaPushIfNotNull(&reward_tables,dropsys_table);

		estrDestroy(&TmpName);
	}

	// If none of the above, use the default table, and possibly the global region rules table
	if (!killed_table)
	{
		killed_table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,"EntKillDefault");
		rewardtrace("default fallback: grabbing reward table %s",SAFE_MEMBER(killed_table,pchName));
	}

	if (killed_table && (!killed_critter || killed_critter->eRewardType != kWorldEncounterRewardType_OverrideStandardRewards))
	{
		eaPushIfNotNull(&reward_tables,killed_table);
	}

	// Add in region rule reward tables. Added code to block this tables via rewardconfig
	if (killed_critter && killed_critter->eRewardType != kWorldEncounterRewardType_OverrideStandardRewards)
	{
		if(reward_CanAddRegionTable(killed_table))
		{
			RegionRules* pRegionRules;
			if (!zmapIsUGCGeneratedMap(NULL) && (pRegionRules = getRegionRulesFromEnt(pKilled)))
			{
				const char* pchRegionTable = pRegionRules->pchGlobalCritterDropRewardTable;
				RewardTable* pRegionRulesTable = RefSystem_ReferentFromString(g_hRewardTableDict,pchRegionTable);
				eaPushIfNotNull(&reward_tables,pRegionRulesTable);
			}
		}
	}

	// awful hack. Allow additional default tables by mapname and for
	// specially named missions, e.g. patrol missions.
	map_addl_table = zmapInfoGetRewardTable(NULL);
	mapname = zmapInfoGetPublicName(NULL);
	if(map_addl_table || mapname)
	{
		if(!map_addl_table)
			map_addl_table = RefSystem_ReferentFromString(g_hRewardTableDict,mapname);
		rewardtrace("maptable: %sadded for map %s",(map_addl_table?"": "not "),mapname);
		eaPushIfNotNull(&reward_tables,map_addl_table);
	}

	// extra reward table specified on critter
	if (killed_critter)
	{
		int i;
		for (i = 0; i < eaSize(&killed_critter->eaAdditionalRewards); i++)
		{
			RewardTable *pExtraRewardTable = GET_REF(killed_critter->eaAdditionalRewards[i]->hRewardTable);
			eaPushIfNotNull(&reward_tables, pExtraRewardTable);
		}	
	}

	estrDestroy(&tmp);
	return reward_tables;
}

static void gen_rewards_into_bags(int iPartitionIdx, Entity *pPlayerEnt, RewardTable **rewards,RewardContext *ctxt,InventoryBag ***bags, U32 inSeed)
{
	int i;
	U32 seed = inSeed;
	for(i = eaSize(&rewards)-1; i>=0; --i)
	{
		bool give_rw = false;
		RewardContext *pLocalContext = StructClone(parse_RewardContext, ctxt);
		RewardTable *table = rewards[i];

		// Set the bag costume in the context
		SetBagCostumeContext(rewards[i], pLocalContext);

		if (!table)
			continue;

		switch (table->KillerType)
		{
		case RewardKillerType_Players:
			if (pLocalContext->killerIsPlayer )
				give_rw = true;
			break;

		case RewardKillerType_Critters:
			if (!pLocalContext->killerIsPlayer )
				give_rw = true;
			break;

		case RewardKillerType_AllEnts:
			give_rw = true;
			break;
		}

		if (give_rw)
		{
			// use seed to make dup for team rewards consistent
			if((table->flags & kRewardFlag_DupForTeam) != 0)
			{
				++seed;
				reward_generateEx(iPartitionIdx, pPlayerEnt, pLocalContext, table, bags, NULL, &seed ,false, NULL, NULL);
			}
			else
			{
				reward_generateEx(iPartitionIdx, pPlayerEnt, pLocalContext, table, bags, NULL, NULL ,false, NULL, NULL);
			}
		}

		//Cleanup the context
		reward_ContextCleanup(pLocalContext);

	}
}

//Matches items in pBag to items on pKiller and deletes the pBag ones if they're unique.
void reward_cullDuplicateUniquesFromBag(Entity *pKiller, InventoryBag* pBag)
{
	Item** itemList = NULL;
	int numItems = inv_bag_GetSimpleItemList(pBag, &itemList, false);
	int ii;
	for (ii = numItems - 1; ii >= 0 ; --ii)
	{
		ItemDef *pDef = GET_REF(itemList[ii]->hItem);
		if (!pDef)
			continue;
		if (pDef->flags & kItemDefFlag_Unique && inv_ent_AllBagsCountItems(pKiller, pDef->pchName) > 0)
		{
			int index = rewardbag_FindItemIndexByDefName(pBag,pDef->pchName);
			rewardbag_RemoveItem(pBag, index, NULL);
		}
	}
	eaDestroy(&itemList);
}

static void LogInventoryBagSummary(Entity *pKiller, InventoryBag *pBag)
{
	InventoryBagSummary summary = {0};
	int i;

	if(!pBag && !gbEnableRewardDataLogging)
		return;

	if(pBag->pRewardBagInfo)
		summary.pcTargetRank = pBag->pRewardBagInfo->pcRank;

	for(i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); ++i)
	{
		InventorySlot *pSlot = pBag->ppIndexedInventorySlots[i];
		if(pSlot && pSlot->pItem)
		{
			ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);
			ItemSummary *pItemSummary = StructCreate(parse_ItemSummary);
			pItemSummary->Quality = item_GetQuality(pSlot->pItem);
			pItemSummary->iNumericValue = pSlot->pItem->count;
			pItemSummary->bAlgo = pSlot->pItem->flags & kItemFlag_Algo;
			pItemSummary->pchName = pItemDef->pchName;
			eaPush(&summary.ppItemSummaries, pItemSummary);
		}
	}

	entLogWithStruct(LOG_DROPSUMMARY, pKiller, "DropSummary", &summary, parse_InventoryBagSummary);
	eaDestroyEx(&summary.ppItemSummaries, NULL);
}

void gen_critter_inventory_into_bags(Entity* pKilled, RewardContext *ctxt,InventoryBag ***bags)
{
	InventoryBag* pRewardBag = NULL;
	Item** ppItemList = NULL;
	int i;

	//make sure that bag 0 exists  (this is the generic bag)
	reward_create_default_bag(NULL, bags);
	pRewardBag = eaGet(bags, 0);

	//make sure we have a valid bag
	if ( !pRewardBag )
	{
		Errorf("error creating bag" );
		ctxt->depth--;
		PERFINFO_AUTO_STOP();
		return;
	}

//  Drop all critter bags that might have something in them.
	inv_ent_GetSimpleItemList(pKilled, StaticDefineIntGetInt(InvBagIDsEnum,"Inventory"), &ppItemList, false, NULL);
	inv_ent_GetSimpleItemList(pKilled, StaticDefineIntGetInt(InvBagIDsEnum,"WeaponR"), &ppItemList, false, NULL);
	inv_ent_GetSimpleItemList(pKilled, StaticDefineIntGetInt(InvBagIDsEnum,"WeaponL"), &ppItemList, false, NULL);
	for (i = 0; i < eaSize(&ppItemList); i++)
	{
		rewardbag_AddItem(pRewardBag, ppItemList[i], true);
	}
}

RecruitType GetRecruitTypes(Entity *pSelf)
{
	RecruitType eRecruitType = kRecruitType_None;
	
	if(pSelf && pSelf->myEntityType == GLOBALTYPE_ENTITYPLAYER && pSelf->pTeam)
	{
		int i;
		Team *pTeam = GET_REF(pSelf->pTeam->hTeam);		
		
		if(pTeam)
		{
			Entity **eaEnts = NULL;
			team_GetOnMapEntsUnique(entGetPartitionIdx(pSelf), &eaEnts, pTeam, false);
			for(i = 0; i < eaSize(&eaEnts);++i)
			{
				Entity *pEnt = eaEnts[i];
				if(pEnt && pEnt != pSelf)
				{
					U32 uiTimeAccepted;
					if((uiTimeAccepted = entity_IsRecruit(pSelf, pEnt))>0)
						eRecruitType = kRecruitType_Recruit;
					else if((uiTimeAccepted = entity_IsRecruiter(pSelf, pEnt))>0)
						eRecruitType = kRecruitType_Recruiter;

					if(uiTimeAccepted
						&& timeServerSecondsSince2000() - uiTimeAccepted < NEW_RECRUIT_TIME_LENGTH)
					{
						eRecruitType |= kRecruitType_New;
					}
				}
			}
			eaDestroy(&eaEnts);
		}
		
		// record last recruit type for use in mission transactions
		pSelf->pTeam->lastRecruitType = eRecruitType;
	}

	return eRecruitType;
}

// Set the reward context for the killer
void SetKillerRewardContextEx(Entity *pEntity, RewardContext *pContext, U32 iKillerLevel, RecruitType recruitType, bool bIsPlayer)
{
	if(!pContext)
	{
		return;
	}
	
	if(pEntity)
	{
		pContext->killerIsPlayer = (entGetType(pEntity) == GLOBALTYPE_ENTITYPLAYER);
		pContext->KillerLevel = entity_GetSavedExpLevel(pEntity);
		pContext->SkillLevel = inv_GetNumericItemValue(pEntity, "SkillLevel");

		if (pEntity->pChar && pEntity->pChar->bLoaded)
		{
			pContext->iKillerCombatLevel = entity_GetCombatLevel(pEntity); // combat level of killer
		}
		else // If the character isn't loaded, get the exp level as a fallback without erroring
		{
			pContext->iKillerCombatLevel = entity_GetSavedExpLevel(pEntity);
		}
		pContext->eRecruitTypes = GetRecruitTypes(pEntity);
		pContext->iKillerSubscriptionDays = entity_GetDaysSubscribed(pEntity);
		if(pEntity->pPlayer)
		{
			eaCopyStructs(&pEntity->pPlayer->eaRewardMods, &pContext->eaRewardMods, parse_RewardModifier);
			eaIndexedEnable(&pContext->eaRewardMods, parse_RewardModifier);
		}
		if(pContext->killerIsPlayer)
		{
			pContext->iPlayerDifficultyIdx = mapState_GetDifficulty(mapState_FromEnt(pEntity));
		}
	}
	else
	{
		pContext->killerIsPlayer = bIsPlayer;
		pContext->KillerLevel = iKillerLevel;
		pContext->SkillLevel = 0;
		pContext->iKillerCombatLevel = recruitType;
	}
}

// Get the index of the player that has the lowest time stamp for loot
// this makes sure everyone gets an item one after another (by map).
S32 reward_Get_Reward_Index(int iPartitionIdx, KillCreditTeam *pTeam)
{
	S32 i, idx = 0;
	U32 uRewards = UINT_MAX;
	U32 uiLowestTimeStamp = UINT_MAX;

	if(pTeam->bTeamUp)
	{
		idx = teamup_GetRewardIndex(iPartitionIdx, eaSize(&pTeam->eaMembers));
		if(idx >= eaSize(&pTeam->eaMembers))
		{
			idx = 0;
			teamup_SetRewardIndex(iPartitionIdx, 0);
		}

		return idx;
	}
	else if(pTeam && eaSize(&pTeam->eaMembers) > 1)
	{
		for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
		{
			KillCreditEntity *pCreditEnt = pTeam->eaMembers[i];
			Entity * killer = entFromEntityRef(iPartitionIdx, pCreditEnt->entRef);
			if(killer && killer->pPlayer)
			{
				if(killer->pPlayer->uLastRewardTeam != pTeam->iTeamID)
				{
					killer->pPlayer->uLastRewardTeam = pTeam->iTeamID;
					killer->pPlayer->uLastRewardCount = 0;
					killer->pPlayer->uiLastRewardTime = 0;
				}
				
				if(killer->pPlayer->uLastRewardCount < uRewards)
				{
					uRewards = killer->pPlayer->uLastRewardCount;
					if (!g_RewardConfig.bRoundRobinEachKill)
						idx = i;
				}

				if (g_RewardConfig.bRoundRobinEachKill && killer->pPlayer->uiLastRewardTime < uiLowestTimeStamp)
				{
					uiLowestTimeStamp = killer->pPlayer->uiLastRewardTime;
					idx = i;
				}
			}
		}

		if (g_RewardConfig.bRoundRobinEachKill)
		{
			KillCreditEntity *pCreditEnt = pTeam->eaMembers[idx];
			Entity * killer = entFromEntityRef(iPartitionIdx, pCreditEnt->entRef);
			if(killer && killer->pPlayer)
			{
				// Update the last reward time
				killer->pPlayer->uiLastRewardTime = timeSecondsSince2000();
			}
		}

		// make sure no one has more than one reward more
		// than anyone else. This fixes cases where new team member joins  		
		for(i = 0; i < eaSize(&pTeam->eaMembers); ++i)
		{
			KillCreditEntity *pCreditEnt = pTeam->eaMembers[i];
			Entity * killer = entFromEntityRef(iPartitionIdx, pCreditEnt->entRef);
			if(killer && killer->pPlayer && killer->pPlayer->uLastRewardCount > uRewards + 1)
			{
				killer->pPlayer->uLastRewardCount = uRewards + 1;
			}
		}
		
	}
	
	return idx;
}

// If this bag is an interact or clickable bag then record the reward count for the players that own items in the bag
void reward_UpdateLootGained(int iPartitionIdx, InventoryBag *pBag)
{
	if (pBag && (pBag->pRewardBagInfo->PickupType == kRewardPickupType_Interact || pBag->pRewardBagInfo->PickupType == kRewardPickupType_Clickable))
	{
		// increment for non-numeric items
		S32 i;
		for (i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); ++i)
		{
			Item *pItem = SAFE_MEMBER(pBag->ppIndexedInventorySlots[i], pItem);
			if (pItem) 	
			{
				ItemDef *pItemDef = GET_REF(pItem->hItem);
				if (pItemDef && pItemDef->eType != kItemType_Numeric)
				{
					if (pItem->owner)
					{
						Entity *pOwnerEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pItem->owner);
						if (pOwnerEnt && pOwnerEnt->pPlayer)
							++pOwnerEnt->pPlayer->uLastRewardCount;
					}
				}
			}
		}
	}
}

// is the extra static map reward table active?
bool Reward_StaticMapRewardTableActive(void)
{
	if(g_RewardConfig.Modifications.pcStaticMapRewardTableShardVariable && g_RewardConfig.Modifications.pcStaticMapRewardTableShardVariable[0])
	{
		ShardVariable *pShardVar = shardvariable_GetByName(g_RewardConfig.Modifications.pcStaticMapRewardTableShardVariable);
		if(pShardVar && pShardVar->pVariable && pShardVar->pVariable->iIntVal == g_RewardConfig.Modifications.iStaticMapRewardTableShardValue)
		{
			return true;
		}
	}

	return false;
}

// Add the reward context map for static zones
void Reward_AddStaticMapTable(RewardTable ***reward_tables, KillCreditTeam*** peaCreditTeams, Entity *pKilled)
{
	// creatures without a reward table won't get the global static map table
	if(g_RewardConfig.Modifications.pcStaticMapRewardTableName && g_RewardConfig.Modifications.pcStaticMapRewardTableName[0] &&
		zmapInfoGetMapType(NULL) == ZMTYPE_STATIC && eaSize(reward_tables) > 0)
	{
		U32 uCurTm = timeServerSecondsSince2000();
		
		if(Reward_StaticMapRewardTableActive())
		{
			S32 teamidx;
			// figure out level diff of party vs kill team
			for(teamidx = 0; teamidx < eaSize(peaCreditTeams); ++teamidx)
			{
				KillCreditTeam *team_credit = (*peaCreditTeams)[teamidx];
				if(team_credit && team_credit->bHighestDamager)
				{
					S32 iKilledLevel = GetEntLevel(pKilled);
					if((U32)(iKilledLevel + g_RewardConfig.Modifications.iStaticMapRewardTableLevelDiffMax) >= team_credit->iTeamCombatLevel)
					{
						RewardTable *pRewardTable = RefSystem_ReferentFromString(g_hRewardTableDict, g_RewardConfig.Modifications.pcStaticMapRewardTableName);
						eaPushIfNotNull(reward_tables, pRewardTable);
						
					}
					break;
				}
			}
		}
	}
}

void reward_processCreditTeams(Entity *pKilled, RewardTable*** reward_tables, KillCreditTeam*** peaCreditTeams, F32 fTotalDamage, const char *pcRank, const char *pcSubRank, char **logS)
{
	int teamidx, ii, memberidx;
	ItemGenRarity rewardQuality = 0;
	bool bItemDropped = false;
	int iPartitionIdx = entGetPartitionIdx(pKilled);

	PERFINFO_AUTO_START_FUNC();

	if (g_RewardConfig.bComputeRewardQuality)
	{
		// Compute the reward quality if we should at this point. Allows dynamic drop rates.
		rewardQuality = gen_get_rarity(pKilled);
	}

	//loop for all teams
	for(teamidx=0; teamidx<eaSize(peaCreditTeams); teamidx++)
	{
		KillCreditTeam *team_credit = (*peaCreditTeams)[teamidx];
		Entity *first_mbr=entFromEntityRef(iPartitionIdx, team_credit->firstMemberEntRef);
		Team *team = team_credit->bTeamUp ? NULL : team_GetTeam(first_mbr);
		int iNewRewardIdx;
		int iRewardIdx;
		U32 seed = timeSecondsSince2000();	// in case there are dup for team reward tables

		iRewardIdx = reward_Get_Reward_Index(iPartitionIdx, team_credit);
		iNewRewardIdx = iRewardIdx;

		estrConcatf(logS, "[Team%d %d/%d %.2f %.2f, ", teamidx, eaSize(&team_credit->eaMembers), team?eaSize(&team->eaMembers):0, team_credit->fTotalTeamDamage, team_credit->fTotalTeamPercentage);

		//loop for all team members
		for(memberidx=0; memberidx<eaSize(&team_credit->eaMembers); memberidx++)
		{
			KillCreditEntity *pCreditEnt = team_credit->eaMembers[memberidx];
			Entity * killer = entFromEntityRef(iPartitionIdx, pCreditEnt->entRef);
			int reward_level = 1;
			InventoryBag **reward_bags = NULL;
			RewardContext *pLocalContext = NULL;
			LevelCombatControl *combat_level = SAFE_MEMBER2(killer,pChar,pLevelCombatControl);
			Entity *sidekick_ent = combat_level ? entFromEntityRef(iPartitionIdx, combat_level->erLink) : NULL;
			ItemChangeReason reason = {0};

			if (!killer)
				continue;

			//ignore self damage
			if (pKilled == killer)
				continue;

			PERFINFO_AUTO_START("per teammember", 1);

			estrConcatf(logS, "%s %.2f,", killer->debugName, pCreditEnt->fPercentOfTeamCredit);

			reward_create_default_bag(NULL, &reward_bags);

			if ( pKilled )
			{
				if (pKilled->pCritter && pKilled->pCritter->eRewardLevelType != kWorldEncounterRewardLevelType_DefaultLevel)
				{
					switch(pKilled->pCritter->eRewardLevelType)
					{
						xcase kWorldEncounterRewardLevelType_PlayerLevel:
						{
							reward_level = GetEntLevel(killer);
						}
						xcase kWorldEncounterRewardLevelType_SpecificLevel:
						{
							reward_level = pKilled->pCritter->iRewardLevel;
						}
						xdefault:
						{
							reward_level = GetEntLevel(pKilled);
							Errorf("Bad reward level type specified on critter %s", pKilled->debugName);
						}
					}
				}
				else if (gConf.bRewardTablesUseEncounterLevel)
					reward_level = (pKilled && pKilled->pCritter && pKilled->pCritter->encounterData.activeTeamSize) ? pKilled->pCritter->encounterData.activeTeamLevel : GetEntLevel(pKilled);
				else
					reward_level = GetEntLevel(pKilled);
			}

			// ----------------------------------------
			// init reward context

			pLocalContext = Reward_CreateOrResetRewardContext(NULL);
			pLocalContext->pcRank = pcRank ? pcRank : g_pcCritterDefaultRank;
			pLocalContext->pcSubRank = pcSubRank ? pcSubRank : g_pcCritterDefaultSubRank;
			pLocalContext->TeamMemberPercentage = pCreditEnt->fPercentCreditTeam;
			pLocalContext->TeamSize = min(eaSize(&team_credit->eaMembers),TEAM_MAX_SIZE);
			pLocalContext->SkillLevel = inv_GetNumericItemValue(killer, "SkillLevel");
			pLocalContext->iTeamCombatLevel = team_credit->iTeamCombatLevel;
			pLocalContext->iTeamRealSize = min(team_credit->iValidTeamSize,TEAM_MAX_SIZE);		// size of the team (in level range and in range)

			pLocalContext->type = RewardContextType_EntKill;
			SetKillerRewardContext(killer, pLocalContext);
			pLocalContext->pKilled = pKilled;
			pLocalContext->RewardLevel = reward_level;
			pLocalContext->iAlgoItemLevel = reward_level;
			pLocalContext->RewardQuality = rewardQuality;
			if(team)
			{
				pLocalContext->lootMode = team->loot_mode;
			}
			else if(team_credit->bTeamUp)
			{
				pLocalContext->lootMode = LootMode_FreeForAll;
			}
			else
			{
				pLocalContext->lootMode = LootMode_RoundRobin;
			}

			pLocalContext->iPointBuyRemaining = -1;

			//sidekick rewards
			//when leveling down, player always gets rewards based on the killed critter level
			//when leveling up, player gets rewards based on their own level plus offset of critter level

			if(pLocalContext->iKillerCombatLevel && pLocalContext->iKillerCombatLevel != pLocalContext->KillerLevel)
			{
				pLocalContext->bSidekicked = true;

				if(pLocalContext->KillerLevel < pLocalContext->iKillerCombatLevel)
				{
					int critterleveloffset = pLocalContext->RewardLevel - pLocalContext->iKillerCombatLevel;
					pLocalContext->RewardLevel = CLAMP(pLocalContext->KillerLevel + critterleveloffset, 1, MAX_LEVELS);
				}
			}

			//only give items to the best team
			//and only the team member that currently matches the teams reward index
			// TODO (JDJ): adjust context so that it doesn't just use one person to generate multiple
			// items (otherwise, we can get a high level player killing something and the good items round-robin'ing
			// to lower level players)
			pLocalContext->bBestTeam = false;
			pLocalContext->bGiveItems = false;
			if (team_credit->bHighestDamager)
			{
				pLocalContext->bBestTeam = true;
				if ( (eaSize(&team_credit->eaMembers) <= 1) || (memberidx == iRewardIdx))
					pLocalContext->bGiveItems = true;
			}

			if ((eaSize(&team_credit->eaMembers) <= 1) || (memberidx == iRewardIdx))
			{
				// this member would get the item ... but only will if kRewardFlag_PlayerKillCreditAlways is set
				pLocalContext->bWouldGiveItems = true;
			}

			// ----------------------------------------
			// gen rewards



			// get the rewards from the appropriate tables.
			reward_SetTeamCredit(peaCreditTeams, pLocalContext);
			gen_rewards_into_bags(iPartitionIdx, killer, (*reward_tables),pLocalContext,&reward_bags, seed);

			if (SAFE_MEMBER(pKilled, pCritter))
			{
				CritterDef* theDef = GET_REF(pKilled->pCritter->critterDef);
				if (theDef && theDef->bDropMyInventory)
					gen_critter_inventory_into_bags(pKilled, pLocalContext, &reward_bags);
			}

			// TODO (JDJ): fix this for teams
			//handle mission drops (for players only) and civilian kill event callouts and duplicates
			if ( killer && (entGetType(killer) == GLOBALTYPE_ENTITYPLAYER ))
			{
				int iBags;
				aiCivilianReportPlayerKillEvent(killer);

				// Only give Mission Drops to players who have "team credit" for the kill
				if ( pCreditEnt->bHasTeamCredit )
				{
					MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(killer);
					pLocalContext->type = RewardContextType_MissionDrop;
					pLocalContext->bGiveItems = true;  //always give mission drops to all players with the mission
					if (!(SAFE_MEMBER(killer->pCritter,eInteractionFlag) & kCritterOverrideFlag_Destructible))//Don't award mission drops for killing destructible objects
					{
						// use a slightly different seed for mission rewards
						reward_AwardMissionDrops( pLocalContext, killer, &reward_bags, seed + 1);
					}
				}

				//Delete unique items which the killing entity already has.
				// ab: this is not transactionally safe. who wrote this?
				// only do this if not on a team
				if(eaSize(&team_credit->eaMembers) == 1)
				{
					for (iBags = 0; iBags < eaSize(&reward_bags); iBags++)
						reward_cullDuplicateUniquesFromBag(killer, reward_bags[iBags]);
				}
			}

			// ----------------------------------------
			// give reward bag(s)

			for(ii=eaSize(&reward_bags)-1; ii>=0; ii--)
			{
				int ItemCount;
				InventoryBag *bag = (InventoryBag *)eaPop(&reward_bags);
				Vec3 DropPos;

				if ( !bag )
					continue;
				ItemCount = inv_bag_CountItems(bag, NULL);

				//skip empty bags
				if (ItemCount < 1)
				{
					StructDestroy(parse_InventoryBag, bag);
					continue;
				}
				else
					bItemDropped = true;

				if (bag->pRewardBagInfo)
				{
					bag->pRewardBagInfo->pcRank = pcRank;
					bag->pRewardBagInfo->LevelDelta = pLocalContext->RewardLevel - pLocalContext->KillerLevel;
				}

				if (gbEnableRewardDataLogging) {
					entLogWithStruct(LOG_REWARDS, killer, "EntKill", bag, parse_InventoryBag );
				}
				LogInventoryBagSummary(killer, bag);
				entGetPos(pKilled, DropPos);

				inv_FillItemChangeReasonKill(&reason, killer, pKilled);

				if ((team || team_credit->bTeamUp) && bag->pRewardBagInfo && !(bag->pRewardBagInfo->flags & kRewardFlag_DupForTeam) && !(bag->pRewardBagInfo->flags & kRewardFlag_PlayerOwned))
				{
					S32 iModifiedRewardIdx = (GiveRewardForTeamWithKilledEntity(iPartitionIdx, bag,team,killer,DropPos, iNewRewardIdx, pLocalContext, pKilled, true, team_credit, -1) % eaSize(&team_credit->eaMembers));
					if (!g_RewardConfig.bRoundRobinEachKill)
						iNewRewardIdx = iModifiedRewardIdx;

					if(team_credit->bTeamUp)
					{
						teamup_SetRewardIndex(iPartitionIdx, iNewRewardIdx);
					}
				}
				else
				{
					if (s_RewardLog)
						reward_logBag(iPartitionIdx, bag, pLocalContext, killer);
					reward_GiveBagWithKilledEntity(killer, bag, DropPos, pKilled, &reason);
				}
			}

			//cleanup any remaining reward bags
			eaDestroyStruct(&reward_bags,parse_InventoryBag);

			//Cleanup the context
			reward_ContextCleanup(pLocalContext);

			PERFINFO_AUTO_STOP();
		}

		estrConcatf(logS, "] ");
	}

	if (s_RewardLog)
		reward_logDroprate(pKilled, bItemDropped);

	PERFINFO_AUTO_STOP();
}

//For player kill, only find the player reward table on the map
void reward_PlayerKill(Entity *pKilled, KillCreditTeam*** peaCreditTeams, F32 fTotalDamage)
{
	char *logS;
	RewardTable **reward_tables = NULL;
	RewardTable *map_addl_table;
	const char *mapname;
	
	if ( !pKilled ||
		!pKilled->pChar )
		return;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&logS);
	estrConcatf(&logS, "[NumTeams %d] [Damage %.2f] [Rank None] ", eaSize(peaCreditTeams),fTotalDamage);

	rewardtrace("entkill: killed '%s'",pKilled->debugName);

	map_addl_table = zmapInfoGetPlayerRewardTable(NULL);
	mapname = zmapInfoGetPublicName(NULL);
	if(map_addl_table || mapname)
	{
		rewardtrace("maptable: %sadded for map %s",(map_addl_table?"": "not "),mapname);
		eaPushIfNotNull(&reward_tables,map_addl_table);
	}

	reward_processCreditTeams(pKilled,&reward_tables,peaCreditTeams,fTotalDamage,NULL,NULL,&logS);

	estrDestroy(&logS);
	eaDestroy(&reward_tables);
	PERFINFO_AUTO_STOP();
}

void reward_EntKill(Entity *pKilled, KillCreditTeam*** peaCreditTeams, F32 fTotalDamage)
{
	char *logS;
	RewardTable **reward_tables = NULL;
	Vec3 KilledPos;
	const char *pcRank = NULL;
	const char *pcSubRank = NULL;

	if ( !pKilled ||
		 !pKilled->pChar )
		return;

	PERFINFO_AUTO_START_FUNC();
 
	if (pKilled->pCritter)
	{
		pcRank = pKilled->pCritter->pcRank;
		pcSubRank = pKilled->pCritter->pcSubRank;
		pKilled->pCritter->bDeathRewardsGiven = true;	// don't allow rewards for this creature in the future
	}

	estrStackCreate(&logS);
	estrConcatf(&logS, "[NumTeams %d] [Damage %.2f] [Rank %s] ", eaSize(peaCreditTeams),fTotalDamage, pcRank?pcRank:"None");

	rewardtrace("entkill: killed '%s' (%s:%s)",pKilled->debugName, NULL_TO_EMPTY(pcRank), NULL_TO_EMPTY(pcSubRank));

	PERFINFO_AUTO_START("find table", 1);
	entGetPos(pKilled, KilledPos);
	reward_tables = gen_killed_rewardtables(pKilled);
	Reward_AddStaticMapTable(&reward_tables, peaCreditTeams, pKilled);
	PERFINFO_AUTO_STOP();

	//log the summary info
	if (gbEnableRewardDataLogging) {
		entLog(LOG_REWARDS, pKilled, "EntKillSummary", "%s", logS );
	}

	reward_processCreditTeams(pKilled,&reward_tables,peaCreditTeams,fTotalDamage,pcRank,pcSubRank,&logS);

	estrDestroy(&logS);
	eaDestroy(&reward_tables);
	PERFINFO_AUTO_STOP();
}


void reward_EntLootCB(Entity *pEnt, InventoryBag *pRewardBag, const ItemChangeReason *pReason, TransactionReturnCallback pFunc, void* pData)
{
	int allNumeric = true;
	bool bShareNumerics = false;
	BagIterator *iter;
	Team *pTeam = team_GetTeam(pEnt);
	int i;

	if ( !pEnt ||
		!pRewardBag )
		return;

	if (g_RewardConfig.bShareLootedNumericsWithTeamMembers && pTeam)
	{
		if (pRewardBag->uiTeamOwner != 0 && team_GetTeamID(pEnt) == pRewardBag->uiTeamOwner)
			bShareNumerics = true;
		else if (pRewardBag->pRewardBagInfo && pRewardBag->pRewardBagInfo->OwnerType == kRewardOwnerType_Team)
		{
			for (i = 0; i < eaSize(&pRewardBag->pRewardBagInfo->peaLootOwners); i++)
			{
				if (pRewardBag->pRewardBagInfo->peaLootOwners[i]->eOwnerType == kRewardOwnerType_Team && eaiFind(&pRewardBag->pRewardBagInfo->peaLootOwners[i]->peaiOwnerIDs, team_GetTeamID(pEnt)) >= 0)
				{
					bShareNumerics = true;
					break;
				}
			}
		}
	}

	if (!bShareNumerics)
	{
		iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pRewardBag));
		for (;!bagiterator_Stopped(iter);bagiterator_Next(iter))
		{
			ItemDef *def = bagiterator_GetDef(iter);

			if(!def || def->eType != kItemType_Numeric)
			{
				allNumeric = false;
				break;
			}
		}
		bagiterator_Destroy(iter);
	}

	if(allNumeric && !bShareNumerics)
	{
		TransactionReturnVal* returnVal;
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("AddItem", pEnt, pFunc, pData);

		AutoTrans_inv_tr_GiveNumericBag(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				pRewardBag, pReason);
	}
	else
	{
		iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pRewardBag));
		for (;!bagiterator_Stopped(iter);bagiterator_Next(iter))
		{
			Item *pItem = (Item*)bagiterator_GetItem(iter);

			if (!pItem) continue;

			if (pRewardBag->pRewardBagInfo && pRewardBag->pRewardBagInfo->ExecuteType == kRewardExecuteType_AutoExec)
			{
				Item* pAutoExecItem = NULL;
				rewardbag_RemoveItem(pRewardBag, iter->i_cur, &pAutoExecItem);
				reward_execute_item(pEnt, pAutoExecItem);
			}
			else
			{
				ItemDef *pItemDef = GET_REF(pItem->hItem);
				//call transaction to add the item
				bool bUseOverflowBag = (pRewardBag->pRewardBagInfo->PickupType == kRewardPickupType_Direct);
				
				if (bShareNumerics && pItemDef->eType == kItemType_Numeric)
				{
					LootBag_ShareNumerics(pEnt, InvBagIDs_Numeric, -1, pRewardBag, 0, pItem, pReason);
				}
				else 
				{
					// Send a notify to client
					if(bUseOverflowBag && g_RewardConfig.Modifications.pDirectRewardNotifyMessageKey && g_RewardConfig.Modifications.pDirectRewardNotifyMessageKey[0])
					{
						if(pItemDef && pItemDef->eType != kItemType_Numeric && pItemDef->eType != kItemType_Title &&
							(pItemDef->flags & kItemDefFlag_Silent) == 0 && pItemDef->pchName && pItemDef->pchIconName)
						{
							char *tmpS = NULL;
							char *pItemName = NULL;

							estrConcatf(&pItemName,"%s",item_GetName(pItem, pEnt));
							entFormatGameMessageKey(pEnt, &tmpS, g_RewardConfig.Modifications.pDirectRewardNotifyMessageKey, STRFMT_STRING("Name", pItemName), STRFMT_END);
							notify_NotifySend(pEnt, kNotifyType_ItemRewardDirectGive, tmpS, pItemDef->pchName, pItemDef->pchIconName);

							estrDestroy(&pItemName);
							estrDestroy(&tmpS);
						}
					}
					invtransaction_AddItem(pEnt, InvBagIDs_None, -1, pItem, bUseOverflowBag?ItemAdd_UseOverflow:0, pReason, pFunc, pData );
				}
				
			}
		}
		bagiterator_Destroy(iter);
	}

	//destroy the rewarded bag
	StructDestroy(parse_InventoryBag, pRewardBag);
}

// If a Mission Grant item has dropped, update Mission Requests so that the item doesn't drop again in the near future
static void reward_UpdateMissionRequestTimestampsForDrop(ItemDef *pItemDef, MissionInfo *pInfo)
{
	MissionDef *pMissionDef = pItemDef?GET_REF(pItemDef->hMission):NULL;

	if(pItemDef && pMissionDef && pItemDef->eType == kItemType_MissionGrant && pInfo){
		int i;
		for (i = eaSize(&pInfo->eaMissionRequests)-1; i>=0; --i){
			if (GET_REF(pInfo->eaMissionRequests[i]->hRequestedMission) == pMissionDef){
				MAX1(pInfo->eaMissionRequests[i]->uInactiveTime, timeSecondsSince2000() + LOOT_CRITTER_LINGER);
			}
		}
	}
}


void reward_AwardMissionDrops( RewardContext *pContext, Entity *pPlayerEnt, InventoryBag ***bags, U32 inSeed)
{
	MissionDrop** eaMissionDrops = NULL;
	MissionInfo *pMissionInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, missionInfo);
	int i;
	U32 seed = inSeed;

	//make sure that the killed ent has a valid critter pointer
	if ( !pContext->pKilled->pCritter )
		return;

	PERFINFO_AUTO_START_FUNC();

	if ( pPlayerEnt && pPlayerEnt->pPlayer)
	{
		Team *pTeam = team_GetTeam(pPlayerEnt);
		bool bMissionRewardTeam = false;
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		
		if(pTeam)
		{
			// generate separate bags for mission rewards if the player is on a team
			bMissionRewardTeam = true;
		}
		
		missiondrop_GetMissionDropsForPlayerKill(pPlayerEnt, pContext->pKilled->pCritter, &eaMissionDrops);

		// iterate through all Mission Drop reward tables
		for (i = 0; i < eaSize(&eaMissionDrops); i++)
		{
			MissionDrop *pDrop = eaMissionDrops[i];
			if ( pDrop && pDrop->RewardTableName && pDrop->RewardTableName[0])
			{
				RewardTable *pRewardTable = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,pDrop->RewardTableName);

				if ( pRewardTable )
				{
					if((pRewardTable->flags & kRewardFlag_DupForTeam) != 0)
					{
						reward_generateEx(iPartitionIdx, pPlayerEnt, pContext, pRewardTable, bags, NULL, &seed, bMissionRewardTeam, NULL, NULL);
					}
					else
					{
						reward_generateEx(iPartitionIdx, pPlayerEnt, pContext, pRewardTable, bags, NULL, NULL, bMissionRewardTeam, NULL, NULL);
					}
				}
				else
				{
					//!!!! hack for special case rewards
					reward_generate_specialcase(pContext, pDrop->RewardTableName, bags, NULL);
				}
			}
		}

		// If any Mission Grant items were awarded for a Mission Request, we should update Mission Requests accordingly
		for (i = eaSize(bags)-1; i>=0; --i){
			InventoryBag *bag = (*bags)[i];
			BagIterator *iter;
			bool found = false;
			
			if(!bag)
				continue;
			
			iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, bag));
			for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
			{
				ItemDef *itemdef = bagiterator_GetDef(iter);
				reward_UpdateMissionRequestTimestampsForDrop(itemdef, pMissionInfo);
			}
			bagiterator_Destroy(iter);
		}

		eaDestroy(&eaMissionDrops);
	}

	PERFINFO_AUTO_STOP();
}

//Loot drop interaction
bool loot_InteractBegin(Entity *pEnt, InventoryBag *pLootBag, bool bClientUpdate, bool bForceSendToClient)
{
	bool bInteractSuccess = false;

	if (pEnt && pLootBag)
	{
		RewardBagInfo *pRewardBagInfo = pLootBag->pRewardBagInfo;
		InventoryBag *pLootBagCopy = StructClone(parse_InventoryBag, pLootBag);

		// copy the loot bag and filter out all items not belonging to the entity
		if (pLootBagCopy)
		{
			BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBagCopy));

			for (;!bagiterator_Stopped(iter);bagiterator_Next(iter))
			{
				Item *pItem = (Item*)bagiterator_GetItem(iter);
				ItemDef *item_def = bagiterator_GetDef(iter);
				if (!pItem || !item_def)
					continue;
				if (pItem->bUnlootable ||
					(pRewardBagInfo && pRewardBagInfo->loot_mode != LootMode_FreeForAll &&
					 ((!!pItem->owner && pItem->owner != pEnt->myContainerID) ||
					  (!pItem->owner && reward_QualityShouldUseLootMode(item_GetQuality(pItem), pRewardBagInfo->eLootModeThreshold))) && !pItem->bExemptFromLootMode))
				{
					int iSlotIdx = iter->i_cur;
					if (iSlotIdx >= 0)
						rewardbag_RemoveItem(pLootBagCopy, iSlotIdx, NULL);
				}
			}
			bagiterator_Destroy(iter);

			if (bClientUpdate)
			{
				if(bForceSendToClient || !entity_IsAutoLootEnabled(pEnt))
					ClientCmd_LootUpdateList(pEnt, pLootBagCopy);
				bInteractSuccess = true;
			}
			else if (rewardbag_Size(pLootBagCopy))
			{
				if(bForceSendToClient || !entity_IsAutoLootEnabled(pEnt))
					ClientCmd_LootInteraction(pEnt, pLootBagCopy);
				bInteractSuccess = true;
			}
			StructDestroy(parse_InventoryBag, pLootBagCopy);
		}
	}

	return bInteractSuccess;
}

//Loot drop interaction
bool loot_InteractBeginMultiBags(Entity *pEnt, InventoryBag*** peaLootBags, bool bClientUpdate, bool bForceSendToClient, bool bForceAutoLoot)
{
	bool bInteractSuccess = false;

	if (pEnt && eaSize(peaLootBags) > 0)
	{
		InventoryBag *pLootBagCopy = rewardbag_CreateEx(NULL);
		// merge all loot bags into a single one for transmission to client
		FOR_EACH_IN_EARRAY((*peaLootBags), InventoryBag, pLootBag)
		{
			RewardBagInfo *pRewardBagInfo = pLootBag->pRewardBagInfo;
			BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBag));

			for (;!bagiterator_Stopped(iter);bagiterator_Next(iter))
			{
				Item *pItem = (Item*)bagiterator_GetItem(iter);
				ItemDef *item_def = bagiterator_GetDef(iter);
				if (!pItem || !item_def)
					continue;
				if (!(pItem->bUnlootable ||
					(pRewardBagInfo && pRewardBagInfo->loot_mode != LootMode_FreeForAll &&
					((!!pItem->owner && pItem->owner != pEnt->myContainerID) ||
					(!pItem->owner && reward_QualityShouldUseLootMode(item_GetQuality(pItem), pRewardBagInfo->eLootModeThreshold))) && !pItem->bExemptFromLootMode)))
				{
					rewardbag_AddItem(pLootBagCopy, StructClone(parse_Item, pItem), false);
				}
			}
			bagiterator_Destroy(iter);

		}
		FOR_EACH_END

			if (bClientUpdate)
			{
				if(bForceSendToClient || (!entity_IsAutoLootEnabled(pEnt) && !bForceAutoLoot))
					ClientCmd_LootUpdateList(pEnt, pLootBagCopy);
				bInteractSuccess = true;
			}
			else if (rewardbag_Size(pLootBagCopy))
			{
				if(bForceSendToClient || (!entity_IsAutoLootEnabled(pEnt) && !bForceAutoLoot))
					ClientCmd_LootInteraction(pEnt, pLootBagCopy);
				bInteractSuccess = true;
			}
			StructDestroy(parse_InventoryBag, pLootBagCopy);
	}

	return bInteractSuccess;
}

static void loot_InteractLog(Entity *pEnt, Entity *pLootEnt, GameInteractable *pInteractable, LootInteractCBData *pData)
{
	static const char *pcPooled_HarvestNode = NULL;
	WorldInteractionEntry *pEntry = NULL;

	if (!pEnt || !gbEnableGamePlayDataLogging)
		return;

	// for now, only logging harvest node interactions
	if (!pData || !pData->pcLootedItemsLogList || !pData->pcLootedItemsLogList[0])
		return;

	if (!pcPooled_HarvestNode) {
		pcPooled_HarvestNode = 	allocAddString("HarvestNode");
	}
	if (pInteractable && eaFind(&pInteractable->eaInteractableCategories, pcPooled_HarvestNode) >= 0) {
		entLog(LOG_GSL, pEnt, "Harvest", "%s", pData->pcLootedItemsLogList);
	}
}

void loot_InteractOnceCallback(TransactionReturnVal *returnVal, LootInteractCBData *pData)
{
	if (pData && returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pLootEnt = NULL;
		GameInteractable *pInteractable = NULL;
		Entity *pPlayerEnt = entFromEntityRef(pData->iPartitionIdx, pData->PlayerEntRef);

		if (pData->type == LootInteractType_Ent)
			pLootEnt = entFromEntityRef(pData->iPartitionIdx, pData->LootEntRef);
		else if (pPlayerEnt)
			pInteractable = interactable_GetByNode(GET_REF(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));

		loot_InteractLog(pPlayerEnt, pLootEnt, pInteractable, pData);
	}
}

static void entity_FillLootBags(Entity* pLootEnt, Entity *pPlayerEnt, InventoryBag*** peaBags)
{
	if (interaction_IsLootEntity(pLootEnt))
		eaPushEArray(peaBags, &pLootEnt->pCritter->eaLootBags);
	else if (pLootEnt && pLootEnt->pCritter)
		LootTracker_FindOwnedLootBags(pLootEnt->pCritter->encounterData.pLootTracker, pPlayerEnt, peaBags);
}

void loot_InteractCallback(TransactionReturnVal *returnVal, LootInteractCBData *pData)
{
	if ( pData )
	{
		Entity *pLootEnt = NULL;
		GameInteractable *pInteractable = NULL;
		InteractionLootTracker **ppLootTracker = NULL;
		InventoryBag** eaLootBags = NULL;
		Entity *pPlayerEnt = entFromEntityRef(pData->iPartitionIdx, pData->PlayerEntRef);
		int iPartitionIdx = FIRST_IF_SET(pData->iPartitionIdx,entGetPartitionIdx(pPlayerEnt));
		BagIterator* pIter = NULL;
		// look for the appropriate loot bag (can't use LootTracker_FindOwnedLootBag because
		// we have to use the cached team ID as the criterion and NOT the player's current team ID
		if (pData->type == LootInteractType_Ent)
		{
			pLootEnt = entFromEntityRef(pData->iPartitionIdx, pData->LootEntRef);
			if (interaction_IsLootEntity(pLootEnt))
				entity_FillLootBags(pLootEnt, pPlayerEnt, &eaLootBags);
			else if (SAFE_MEMBER2(pLootEnt, pCritter, encounterData.pLootTracker))
			{
				LootTracker_FindBagsForTeamID(pLootEnt->pCritter->encounterData.pLootTracker, pData->uiTeamID, &eaLootBags);
				ppLootTracker = &pLootEnt->pCritter->encounterData.pLootTracker;
			}
		}
		else
		{
			InteractionLootTracker **ppTracker;
			if (pPlayerEnt)
			{
				WorldInteractionNode* pNode = RefSystem_ReferentFromString(INTERACTION_DICTIONARY, pData->pcInteractableNodeName);
				pInteractable = interactable_GetByNode(pNode);
			}
			ppTracker = interactable_GetLootTrackerAddress(iPartitionIdx, pInteractable);
			if (ppTracker && *ppTracker)
			{
				LootTracker_FindBagsForTeamID(*ppTracker, pData->uiTeamID, &eaLootBags);
				ppLootTracker = ppTracker;
			}
		}

		FOR_EACH_IN_EARRAY(eaLootBags, InventoryBag, pLootBag)
		{
			Item* pItem;
			pIter = inv_bag_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(InventoryBag, pLootBag), pData->iItemID, pIter);
			
			if (!pIter)
				continue;


			pItem = CONTAINER_RECONST(Item, bagiterator_GetItem(pIter));
			
			if (pItem)
			{
				pItem->iPendingTransactionCount--;
				if (pItem->iPendingTransactionCount <= 0)
					pItem->bTransactionPending = false;
			}

			if ( returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
			{
				bool bRemoveItem = true;

				if (pItem)
				{
					// See if we need to remove the numeric completely or update the count of it
					if (pItem->count - pData->iItemCount > 0)
					{
						CONTAINER_NOCONST(Item, pItem)->count -= pData->iItemCount;
						bRemoveItem = false;
					}
				}

				// remove item from all bags on the relevant tracker (or just remove the item from a loot entity's loot bag)
				if (bRemoveItem)
				{
					if (ppLootTracker && *ppLootTracker)
						LootTracker_RemoveItemFromAllBags(*ppLootTracker, pData->iItemID);
					else
						rewardbag_RemoveItemByID(pLootBag, pData->iItemID, NULL, NULL);
				}

				// update the loot bag on the client
				loot_InteractBeginMultiBags(pPlayerEnt, &eaLootBags, true, true, false);

				// log
				loot_InteractLog(pPlayerEnt, pLootEnt, pInteractable, pData);

				// perform cleanup
				if (ppLootTracker)
				{
					//if the tracker has been cleaned out, we're done.
					if (interactloot_CleanupLootInteraction(pPlayerEnt, ppLootTracker, pLootBag))
						break;
				}else if (pLootEnt && interaction_IsLootEntity(pLootEnt))
				{
					if (interactloot_CleanupLootEntBag(pLootEnt, pLootBag, pPlayerEnt))
						eaRemove(&eaLootBags, FOR_EACH_IDX(eaLootBags, pLootBag));
				}
			}
		}
		FOR_EACH_END

		bagiterator_Destroy(pIter);

		if ( returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
		{
			if (pInteractable)
				estrConcatf(&pInteractable->estrDebugLog,"interact_callback fail;\n");

			if (pPlayerEnt)
				ClientCmd_NotifySend(pPlayerEnt, kNotifyType_InventoryFull, entTranslateMessageKey(pPlayerEnt, INVENTORY_FULL_MSG), NULL, NULL);

			if (entity_ShouldAutoLootTarget(pPlayerEnt, pLootEnt))
				loot_InteractBeginMultiBags(pPlayerEnt, &eaLootBags, false, true, false);
		}
		eaDestroy(&eaLootBags);
	}

	if (pData)
		StructDestroy(parse_LootInteractCBData, pData);
}

void loot_InteractTake(Entity* pPlayerEnt, S32 iID, S32 iBagID, S32 iDstSlot, const ItemChangeReason *pReason)
{
	Entity *pLootEnt;
	InventoryBag** eaLootBags = NULL;
	GameInteractable *pInteractable = NULL;
	int iPartitionIdx;

	if(!pPlayerEnt)
		return;

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	
	if ( interaction_IsPlayerInteracting(pPlayerEnt) )
	{
		pLootEnt = entFromEntityRef(iPartitionIdx, pPlayerEnt->pPlayer->InteractStatus.interactTarget.entRef);
		if (!pLootEnt)
			pInteractable = interactable_GetByNode(GET_REF(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));

		if (pLootEnt || pInteractable)
		{
			bool bFound = false;
			if (pLootEnt)
				entity_FillLootBags(pLootEnt, pPlayerEnt, &eaLootBags);
			else if (pInteractable)
			{
				InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
				LootTracker_FindOwnedLootBags(pTracker, pPlayerEnt, &eaLootBags);
			}

			FOR_EACH_IN_EARRAY(eaLootBags, InventoryBag, pBag)
			{
				int iSlot = 0;
				Item *pItem = inv_bag_GetItemByID(pBag, iID, &iSlot, NULL);
				InventorySlot* pSlot = pBag->ppIndexedInventorySlots[iSlot];
				ItemQuality iQuality =  item_GetQuality(pItem);

				// ensure item exists and the player owns it (or that the bag doesn't have more than 1 owner)
				if (pItem &&
					!(!!pItem->owner && pItem->owner != pPlayerEnt->myContainerID) &&
					!(!pItem->owner && pBag->pRewardBagInfo && !pItem->bExemptFromLootMode && 
					// pItem->Quality is u8 and can be 255 (-1) from kItemQuality_None. Therefore item quality needs to be checked vs max, if higher its ok (no quality)
					(iQuality < eaSize(&g_ItemQualities.ppQualities) && reward_QualityShouldUseLootMode(iQuality, pBag->pRewardBagInfo->eLootModeThreshold))
					))
				{
					if (pBag->pRewardBagInfo && pBag->pRewardBagInfo->ExecuteType == kRewardExecuteType_AutoExec)
					{
						Item* pAutoExecItem = NULL;
						rewardbag_RemoveItem(pBag, iSlot, &pAutoExecItem);
						reward_execute_item(pPlayerEnt, pAutoExecItem);
						loot_InteractBeginMultiBags(pPlayerEnt, &eaLootBags, true, false, false);
					}
					else
					{
						if ( !pItem->bTransactionPending )
						{
							Team *pTeam = team_GetTeam(pPlayerEnt);
							ItemDef *pItemDef = GET_REF(pItem->hItem);

							if (g_RewardConfig.bShareLootedNumericsWithTeamMembers &&
								pLootEnt &&
								pTeam &&
								pItemDef &&
								pItemDef->eType == kItemType_Numeric &&
								pItem->numeric_op == NumericOp_Add &&			
								(pBag->uiTeamOwner == 0 || team_GetTeamID(pPlayerEnt) == pBag->uiTeamOwner))
							{
								LootBag_ShareNumerics(pPlayerEnt, iBagID, iDstSlot, pBag, pLootEnt->myRef, pItem, pReason);
							}
							else
							{
								LootInteractCBData* pData = StructCreate(parse_LootInteractCBData);
								pData->iPartitionIdx = iPartitionIdx;
								pData->PlayerEntRef = pPlayerEnt->myRef;
								pData->uiTeamID = pBag->uiTeamOwner;
								pData->pcInteractableNodeName = REF_STRING_FROM_HANDLE(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode);

								if ( pLootEnt )
								{
									pData->type = LootInteractType_Ent;
									pData->LootEntRef = pLootEnt->myRef;
								}
								else
								{
									pData->type = LootInteractType_Clickable;
								}

								pData->iItemID = pItem->id;
								pData->iItemCount = pItem->count;
								pData->pcLootedItemsLogList = StructAllocString(item_GetLogString(pItem));
								if(pInteractable)
									estrConcatf(&pInteractable->estrDebugLog,"player %s taking one;\n", pPlayerEnt->debugName);

								// reset item power lifetimes
								item_trh_ResetPowerLifetimes(CONTAINER_NOCONST(Item, pItem));

								//call transaction to add the item
								pItem->bTransactionPending = true;
								invtransaction_AddItem(pPlayerEnt, iBagID, iDstSlot, pItem, 0, pReason, loot_InteractCallback, (void*)pData);
							}
						}
					}
					bFound = true;
				}
				// destroy the rewarded bag
				if (pLootEnt && interaction_IsLootEntity(pLootEnt))
					interactloot_CleanupLootEntBag(pLootEnt, pBag, pPlayerEnt);
				else if (pBag && (!gConf.bLootBagsRemainOnInteractablesUntilEmpty || inv_bag_CountItems(pBag, NULL) <= 0))
				{
					// remove the bag from the loot tracker
					InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
					S32 iRemoveIdx = -1;

					if (pTracker)
						iRemoveIdx = eaFindAndRemove(&pTracker->eaLootBags, pBag);
					else if (pLootEnt && SAFE_MEMBER(pLootEnt->pCritter, encounterData.pLootTracker))
						iRemoveIdx = eaFindAndRemove(&pLootEnt->pCritter->encounterData.pLootTracker->eaLootBags, pBag);
					
					if (iRemoveIdx >= 0)
						StructDestroy(parse_InventoryBag, pBag);

					if (pPlayerEnt && eaSize(&eaLootBags) <= 0)
						interaction_EndInteractionAndDialog(iPartitionIdx, pPlayerEnt, true, false, true);
				}
				if (bFound)
					break;
			}
			FOR_EACH_END

		}
	}
	eaDestroy(&eaLootBags);
}

static void LootBag_ShareNumerics(Entity* player_ent, S32 iDstBagID, S32 iSlot, InventoryBag *pLootBag, EntityRef opt_loot_ent, Item *pItem, const ItemChangeReason *pReason)
{
	if (player_ent && pLootBag && pItem && pItem->numeric_op == NumericOp_Add)
	{
		int iPartitionIdx = entGetPartitionIdx(player_ent);
		LootInteractCBData* pData;
		Team *pTeam = team_GetTeam(player_ent);

		// The amount of numeric left to share
		S32 iNumericRemaining = pItem->count;

		TeamMember **eaTeamMembers = NULL;

		S32 itMember;

		eaCopy(&eaTeamMembers, &pTeam->eaMembers);

		// Make sure looter gets the most money
		for (itMember = 0; itMember < eaSize(&eaTeamMembers); itMember++)
		{
			Entity *pTeamMember = eaTeamMembers[itMember] ? GET_REF(eaTeamMembers[itMember]->hEnt) : NULL;

			if (pTeamMember && pTeamMember == player_ent)
			{
				// Move the looter to the end of the list
				if (itMember != eaSize(&eaTeamMembers) - 1)
				{
					eaSwap(&eaTeamMembers, eaSize(&eaTeamMembers) - 1, itMember);
				}
				break;
			}
		}

		//Remove members who aren't on the same map and partition.
		for (itMember = eaSize(&eaTeamMembers)-1; itMember >= 0; itMember--)
		{
			Entity *pTeamMember = eaTeamMembers[itMember] ? GET_REF(eaTeamMembers[itMember]->hEnt) : NULL;
			if (pTeamMember)
			{
				Entity *e = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->myContainerID);
				if (e)
					continue;
			}
			eaRemove(&eaTeamMembers, itMember);
		}


		// Distribute evenly amongst all team members
		for (itMember = 0; itMember < eaSize(&eaTeamMembers); itMember++)
		{
			Entity *pTeamMember = eaTeamMembers[itMember] ? GET_REF(eaTeamMembers[itMember]->hEnt) : NULL;

			if (pTeamMember)
			{
				// Duplicate the item (loot_InteractCallback frees this item)
				Item *pItemDupe = StructClone(parse_Item, pItem);

				pData = StructCreate(parse_LootInteractCBData);
				pData->iPartitionIdx = iPartitionIdx;
				pData->PlayerEntRef = player_ent->myRef;
				pData->uiTeamID = pLootBag->uiTeamOwner;
				pData->type = opt_loot_ent?LootInteractType_Ent:LootInteractType_Clickable;
				pData->LootEntRef = opt_loot_ent;
				pData->iItemID = pItem->id;
				pData->pcLootedItemsLogList = StructAllocString(item_GetLogString(pItem));
				pData->pcInteractableNodeName = REF_STRING_FROM_HANDLE(player_ent->pPlayer->InteractStatus.interactTarget.hInteractionNode);

				// Set the proper share
				CONTAINER_NOCONST(Item, pItemDupe)->count = itMember == eaSize(&eaTeamMembers) - 1 ? iNumericRemaining : pItem->count / eaSize(&eaTeamMembers);

				// Increment the transaction count
				pItem->iPendingTransactionCount++;

				// Set callback values
				pData->iItemCount = pItemDupe->count;

				iNumericRemaining -= pItemDupe->count;

				invtransaction_AddItem(pTeamMember, iDstBagID, -1, pItemDupe, 0, pReason, loot_InteractCallback, (void*)pData);
				StructDestroy(parse_Item, pItemDupe);
			}
		}

		// Clean up
		eaDestroy(&eaTeamMembers);
	}
}

static bool LootBag_TakeAll(Entity* player_ent, S32 iDstBagID, InventoryBag*** peaLootBags, int idx, EntityRef opt_loot_ent, const ItemChangeReason *pReason)
{
	Team *pTeam = team_GetTeam(player_ent);
	BagIterator *iter;
	int iPartitionIdx;
	InventoryBag* pLootBag = (*peaLootBags)[idx];
	bool retVal = true;

	if(!player_ent || !pLootBag)
		return false;	

	iPartitionIdx = entGetPartitionIdx(player_ent);
	iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBag));
	for (;!bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pItem = (Item*)bagiterator_GetItem(iter);
		ItemDef *pItemDef = bagiterator_GetDef(iter);
		LootInteractCBData* pData;		

		if (!pItem)
			continue;
		if ( pItem->bTransactionPending )
			continue;
		// ensure player has ownership
		if (((!!pItem->owner && pItem->owner != player_ent->myContainerID) ||
			(!pItem->owner && pLootBag->pRewardBagInfo && reward_QualityShouldUseLootMode(item_GetQuality(pItem), pLootBag->pRewardBagInfo->eLootModeThreshold))) &&
			!pItem->bExemptFromLootMode)
			continue;

		// invariant over loop
		if (SAFE_MEMBER(pLootBag->pRewardBagInfo,ExecuteType) == kRewardExecuteType_AutoExec)
		{
			Item* pAutoExecItem = NULL;
			rewardbag_RemoveItem(pLootBag, iter->i_cur, &pAutoExecItem);
			reward_execute_item(player_ent, pAutoExecItem);
			loot_InteractBeginMultiBags(player_ent, peaLootBags, true, false, false);
			continue;
		}

		pItemDef = GET_REF(pItem->hItem);

		// reset lifetimes on items
		item_trh_ResetPowerLifetimes(CONTAINER_NOCONST(Item, pItem));

		//call transaction to add the item
		pItem->bTransactionPending = true;
		if (g_RewardConfig.bShareLootedNumericsWithTeamMembers &&
			pTeam &&
			pItemDef &&
			pItemDef->eType == kItemType_Numeric &&
			pItem->numeric_op == NumericOp_Add &&			
			(pLootBag->uiTeamOwner == 0 || team_GetTeamID(player_ent) == pLootBag->uiTeamOwner))
		{
			LootBag_ShareNumerics(player_ent, iDstBagID, -1, pLootBag, opt_loot_ent, pItem, pReason);
		}
		else
		{
			pData = StructCreate(parse_LootInteractCBData);
			pData->iPartitionIdx = iPartitionIdx;
			pData->PlayerEntRef = player_ent->myRef;
			pData->uiTeamID = pLootBag->uiTeamOwner;
			pData->type = opt_loot_ent?LootInteractType_Ent:LootInteractType_Clickable;
			pData->LootEntRef = opt_loot_ent;
			pData->iItemCount = pItem->count;
			pData->iItemID = pItem->id;
			pData->pcLootedItemsLogList = StructAllocString(item_GetLogString(pItem));
			pData->pcInteractableNodeName = REF_STRING_FROM_HANDLE(player_ent->pPlayer->InteractStatus.interactTarget.hInteractionNode);

			pItem->iPendingTransactionCount++;
			if (!invtransaction_AddItem(player_ent, iDstBagID, -1, pItem, 0, pReason, loot_InteractCallback, (void*)pData))
				retVal = false;
		}

	}
	bagiterator_Destroy(iter);
	return retVal;
}

void loot_InteractTakeAll(Entity* pPlayerEnt, S32 iBagID, bool bEndInteraction)
{
	Entity *pLootEnt;
	InventoryBag** eaLootBags = NULL;
	GameInteractable *pInteractable = NULL;
	int iPartitionIdx;
	bool bSuccess = true;
	ItemChangeReason reason = {0};
	
	if(!pPlayerEnt)
		return;

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	
	if(!interaction_IsPlayerInteractTimerFinished(pPlayerEnt)) //We only want to continue if the player is actually looking at a loot window
		return;
	pLootEnt = entFromEntityRef(iPartitionIdx, pPlayerEnt->pPlayer->InteractStatus.interactTarget.entRef);

	if (!pLootEnt)
		pInteractable = interactable_GetByNode(GET_REF(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));

	if (pLootEnt || pInteractable)
	{
		if (pLootEnt)
			entity_FillLootBags(pLootEnt, pPlayerEnt, &eaLootBags);
		else if (pInteractable)
		{
			InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
			LootTracker_FindOwnedLootBags(pTracker, pPlayerEnt, &eaLootBags);
			// Using estrprintf instead of estrconcatf here as the same intractable can be used over and over again and the log just keeps getting bigger and bigger
			estrPrintf(&pInteractable->estrDebugLog,"player %s taking all;\n", pPlayerEnt->debugName);
		}

		inv_FillItemChangeReason(&reason, pPlayerEnt, "Loot:InteractTakeAll", NULL);
		FOR_EACH_IN_EARRAY(eaLootBags, InventoryBag, pBag)
		{
			if (!LootBag_TakeAll(pPlayerEnt,iBagID,&eaLootBags,FOR_EACH_IDX(eaLootBags, pBag),SAFE_MEMBER(pLootEnt,myRef),&reason) && gConf.bLootBagsRemainOnInteractablesUntilEmpty)
			{
				bSuccess = false;
				continue;
			}
			if (pLootEnt && interaction_IsLootEntity(pLootEnt))
				interactloot_CleanupLootEntBag(pLootEnt, pBag, pPlayerEnt);
			else if (pBag && (!gConf.bLootBagsRemainOnInteractablesUntilEmpty || inv_bag_CountItems(pBag, NULL) <= 0))
			{
				// remove the bag from the loot tracker
				InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
				S32 iRemoveIdx = -1;

				eaRemove(&eaLootBags, FOR_EACH_IDX(eaLootBags, pBag));

				if (pTracker)
					iRemoveIdx = eaFindAndRemove(&pTracker->eaLootBags, pBag);
				else if (pLootEnt && SAFE_MEMBER(pLootEnt->pCritter, encounterData.pLootTracker))
					iRemoveIdx = eaFindAndRemove(&pLootEnt->pCritter->encounterData.pLootTracker->eaLootBags, pBag);

				if (iRemoveIdx >= 0)
					StructDestroy(parse_InventoryBag, pBag);

			}
		}
		FOR_EACH_END
	}

	eaDestroy(&eaLootBags);

	if (!bSuccess)
	{
		// If auto-looting is enabled, show the LootUI
		if (entity_ShouldAutoLootTarget(pPlayerEnt, pLootEnt))
		{
			loot_InteractBeginMultiBags(pPlayerEnt, &eaLootBags, false, true, false);
		}
		// If we didn't manage to take all the items, don't just destroy them.
		return;
	}
	else if (pPlayerEnt && bEndInteraction)
	{
		interaction_EndInteractionAndDialog(iPartitionIdx, pPlayerEnt, true, false, true);
	}

}

void loot_InteractTakeAllOfType(Entity* pPlayerEnt, S32 eItemType, S32 iBagID)
{
	Entity *pLootEnt;
	InventoryBag** eaLootBags = NULL;
	GameInteractable *pInteractable = NULL;
	BagIterator *iter;
	int iPartitionIdx;
	ItemChangeReason reason = {0};
	
	if(!pPlayerEnt)
		return;

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	if( !interaction_IsPlayerInteracting(pPlayerEnt) )
		return;
	pLootEnt = entFromEntityRef(iPartitionIdx, pPlayerEnt->pPlayer->InteractStatus.interactTarget.entRef);

	if (!pLootEnt)
		pInteractable = interactable_GetByNode(GET_REF(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));
	if (!pLootEnt && !pInteractable)
		return;


	if (pLootEnt)
		entity_FillLootBags(pLootEnt, pPlayerEnt, &eaLootBags);
	else if (pInteractable)
	{
		InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
		LootTracker_FindOwnedLootBags(pTracker, pPlayerEnt, &eaLootBags);
		estrConcatf(&pInteractable->estrDebugLog,"player %s taking all;\n", pPlayerEnt->debugName);
	}

	if (!eaLootBags)
	{
		return;
	}

	inv_FillItemChangeReason(&reason, pPlayerEnt, "Loot:InteractTake", NULL);

	iter = invbag_IteratorFromBagEarray((NOCONST(InventoryBag)***)&eaLootBags);
	for(;!bagiterator_Stopped(iter);bagiterator_Next(iter))
	{
		Item* pItem = (Item*)bagiterator_GetItem(iter);
		ItemDef* pItemDef = bagiterator_GetDef(iter);
		if(pItemDef && pItemDef->eType == eItemType) {
			loot_InteractTake(pPlayerEnt, pItem->id, iBagID, -1, &reason);
		}
	}
	bagiterator_Destroy(iter);
	eaDestroy(&eaLootBags);
}

void loot_InteractTakeAllExceptType(Entity* pPlayerEnt, S32 eItemType, S32 iBagID)
{
	Entity *pLootEnt;
	InventoryBag** eaLootBags = NULL;
	GameInteractable *pInteractable = NULL;
	BagIterator *iter;
	int iPartitionIdx;
	ItemChangeReason reason = {0};

	if(!pPlayerEnt)
		return;

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	
	if( !interaction_IsPlayerInteracting(pPlayerEnt) )
		return;
	pLootEnt = entFromEntityRef(iPartitionIdx, pPlayerEnt->pPlayer->InteractStatus.interactTarget.entRef);

	if (!pLootEnt)
		pInteractable = interactable_GetByNode(GET_REF(pPlayerEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));
	if (!pLootEnt && !pInteractable)
		return;

	if (pLootEnt)
		entity_FillLootBags(pLootEnt, pPlayerEnt, &eaLootBags);
	else if (pInteractable)
	{
		InteractionLootTracker *pTracker = interactable_GetLootTracker(iPartitionIdx, pInteractable, false);
		LootTracker_FindOwnedLootBags(pTracker, pPlayerEnt, &eaLootBags);
		estrConcatf(&pInteractable->estrDebugLog,"player %s taking all;\n", pPlayerEnt->debugName);
	}

	if (!eaLootBags)
		return;

	inv_FillItemChangeReason(&reason, pPlayerEnt, "Loot:InteractTake", NULL);
	iter = invbag_IteratorFromBagEarray((NOCONST(InventoryBag)***)&eaLootBags);
	for(; !bagiterator_Stopped(iter);bagiterator_Next(iter))
	{
		Item* pItem = (Item*)bagiterator_GetItem(iter);
		ItemDef *pItemDef = bagiterator_GetDef(iter);
		if(!pItemDef || pItemDef->eType != eItemType) {
			loot_InteractTake(pPlayerEnt, pItem->id, iBagID, -1, &reason);
		}
	}	
	bagiterator_Destroy(iter);
	eaDestroy(&eaLootBags);
}


// Execute a reward table.  Uses the power GrantReward context.
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(5) ACMD_CATEGORY(Standard, Reward);
void GrantRewardTable(Entity *pEnt, ACMD_NAMELIST("RewardTable", REFDICTIONARY) const char *reward_table_name)
{
	RewardTable *reward = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	ItemChangeReason reason = {0};
	inv_FillItemChangeReason(&reason, pEnt, "Internal:GrantRewardTable", NULL);
	reward_PowerExec(pEnt, reward, entity_GetCombatLevel(pEnt), 1.f, 0, &reason);
}

// give the reward table directly into your inventory
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(5) ACMD_CATEGORY(Standard, Reward);
void GrantRewardTableDirect(Entity *e, ACMD_NAMELIST("RewardTable", REFDICTIONARY) char *reward_table_name)
{
	int i;
	InventoryBag **bags = NULL;
	RewardTable *table = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	int level = entity_GetCombatLevel(e);
	RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);
	ItemChangeReason reason = {0};

	if(!table || !e || pLocalContext == NULL)
		return;

	pLocalContext->type = RewardContextType_PowerExec;
	pLocalContext->pKilled = NULL;
	pLocalContext->RewardLevel = level;
	pLocalContext->RewardScale = 1.0 ;
	SetKillerRewardContext(e, pLocalContext);
	reward_generateEx(entGetPartitionIdx(e), e, pLocalContext, table, &bags, NULL, NULL, false, NULL, NULL);
	
	for(i = eaSize(&bags)-1; i>=0; --i)
	{
		InventoryBag *bag = bags[i];
		if(!bag)
			continue;
		inv_FillItemChangeReason(&reason, e, "Internal:GrantRewardTableDirect", reward_table_name);
		LootBag_TakeAll(e, InvBagIDs_None, &bags, i, 0, &reason);
	}
	//reward_GiveBags(e, false, &bags);
	
	//Cleanup the context
	reward_ContextCleanup(pLocalContext);
}

bool reward_PowerExec_GenerateBags(Entity* e, RewardTable *table, int level, F32 scale, U32 iDifficulty, 
								   U32* pSeed, InventoryBag ***peaBags)
{
	if(table && e)
	{
		RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

		if (pLocalContext)
		{
			pLocalContext->type = RewardContextType_PowerExec;
			pLocalContext->pKilled = NULL;
			pLocalContext->RewardLevel = level;
			pLocalContext->RewardScale = scale;
			SetKillerRewardContext(e, pLocalContext);
			pLocalContext->iPlayerDifficultyIdx = iDifficulty;
			reward_generateEx(entGetPartitionIdx(e), e, pLocalContext, table, peaBags, NULL, pSeed, false, NULL, NULL);
			
			//Cleanup the context
			reward_ContextCleanup(pLocalContext);
		}

		return eaSize(peaBags) > 0;
	}
	return false;
}

// Execute a reward table.  Uses the power GrantReward context.
// used for testing more complex tables
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Standard, Reward) ACMD_NAME(RewardGrant);
void GrantRewardTableEx(Entity *pEnt, ACMD_NAMELIST("RewardTable", REFDICTIONARY) char *reward_table_name, U32 iDifficulty)
{
	InventoryBag **bags = NULL;
	RewardTable *reward = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);

	if (reward_PowerExec_GenerateBags(pEnt, reward, entity_GetCombatLevel(pEnt), 0, iDifficulty, NULL, &bags))
	{
		ItemChangeReason reason = {0};
		inv_FillItemChangeReason(&reason, pEnt, "Internal:GrantRewardTableEx", NULL);
		reward_GiveBags(pEnt, false, &bags, &reason);
	}
}

// This is a debug command to test the granting of mission rewards
// the inputs are a reward table name, and an optional choice item name
// this debug routine only supports sending a single choice
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Reward);
void GrantMissionRewards( Entity *pEnt,
						  ACMD_NAMELIST("RewardTable", REFDICTIONARY) char *RewardTableName,
						  ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *ChoiceName )
{
	RewardTable *reward = NULL;
	GiveRewardBagsData *pRewardBagsData = StructCreate(parse_GiveRewardBagsData);
	TransactionReturnVal* returnVal;

	pRewardBagsData->ppRewardBags = NULL;
	pRewardBagsData->ppChoices = NULL;

	reward = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,RewardTableName);

	if ( reward )
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 i, j;
		U32* eaPets = NULL;
		RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);
		char *pChoice;
		ItemChangeReason reason = {0};

		if (pLocalContext)
		{
			pLocalContext->type = RewardContextType_PowerExec;
			pLocalContext->pKilled = NULL;
			pLocalContext->RewardLevel = GetEntLevel(pEnt);
			SetKillerRewardContext(pEnt, pLocalContext);

			reward_generate(entGetPartitionIdx(pEnt), pEnt, pLocalContext, reward, &pRewardBagsData->ppRewardBags, NULL, NULL);

			// Destroy the local context
			StructDestroy(parse_RewardContext, pLocalContext);

			pChoice = StructAllocString(ChoiceName);
			eaPush(&pRewardBagsData->ppChoices, pChoice);

			ea32Create(&eaPets);
			for (i = eaSize(&pRewardBagsData->ppRewardBags)-1; i >= 0; i--)
			{
				InventoryBag* pBag = pRewardBagsData->ppRewardBags[i];
				for (j = eaSize(&pBag->ppIndexedInventorySlots)-1; j >= 0; j--)
				{
					InventorySlot* pSlot = pBag->ppIndexedInventorySlots[j];
					if (pSlot->pItem)
					{
						ItemDef* pItemDef = GET_REF(pSlot->pItem->hItem);
						if (pItemDef && (pItemDef->flags & kItemDefFlag_Unique)!=0)
						{
							break;
						}
					}
				}
				if (j >= 0) break;
			}
			if (i >= 0)
			{
				Entity_GetPetIDList(pEnt, &eaPets);
			}

			returnVal = LoggedTransactions_CreateManagedReturnValEnt("GiveBags", pEnt, NULL, NULL);
			inv_FillItemChangeReason(&reason, pEnt, "Internal:GrantMissionRewardTable", RewardTableName);

			AutoTrans_inv_tr_GiveRewardBags(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
				pRewardBagsData, &reason, pExtract);

			ea32Destroy(&eaPets);
		}
	}

	//cleanup any remaining reward bags
	StructDestroy(parse_GiveRewardBagsData, pRewardBagsData);
}

static void rewardtable_InvalidDirectDropCheck(ExprContext* context, RewardTable *pTable, RewardContextType ctxtType)
{
	if (rewardTable_HasItemsWithType(pTable, kRewardPickupType_Choose, ctxtType)
		|| rewardTable_HasItemsWithType(pTable, kRewardPickupType_Clickable, ctxtType))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Reward Table %s contains Direct, Choose, or Clickable item grants!", pTable->pchName);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void exprFuncGrantRewardToEntArray_StaticCheck(ExprContext* context, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name)
{
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);
}

// Example: GetAllMapPlayers().GrantRewardToEntArray("RewardTableName")
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GrantRewardToEntArray) ACMD_EXPR_STATIC_CHECK(exprFuncGrantRewardToEntArray_StaticCheck);
void exprFuncGrantRewardToEntArray(ExprContext* context, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name)
{
	int i;
	MultiVal answer = {0};
	RewardTable *pTable = NULL;
	ItemChangeReason reason = {0};

	if(!(pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name)))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find reward table %s in GrantRewardToEntArray", reward_table_name);
		return;
	}

	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);
	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		Entity* pTargetEnt = (*entsIn)[i];
		int level = GetEntLevel(pTargetEnt);
		inv_FillItemChangeReason(&reason, pTargetEnt, "Expression:GrantRewardToEntArray", NULL);

		reward_PowerExec(pTargetEnt, pTable, level, 1.f, 0, &reason);
	}
}

AUTO_EXPR_FUNC_STATIC_CHECK;
void exprFuncGrantRewardToEntArrayWithScale_StaticCheck(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, ACMD_EXPR_SUBEXPR_IN subExpr)
{
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);
}

extern int targetEntVarHandle;
extern const char* targetEntString;
static ExprContext* reward_GetGrantRewardSubContext(ExprContext* pParentContext)
{
	static ExprContext* lokContext = NULL;
	if(!lokContext)
	{
		lokContext = exprContextCreate();
		exprContextSetSilentErrors(lokContext, true);
		//lokContext->silentErrors = 1; // TODO: remove this when it can be done more intelligently
		// (constant errors when trying to evaluate .pCritter.parentEncounter.staticEnc.basedef because
		// not all evaluated critters had it
		exprContextSetPointerVarPooledCached(lokContext, targetEntString, NULL, parse_Entity,
			false, true, &targetEntVarHandle);
		// TODO: this will fail if anything from the passed in context is used to evaluate the ents...
		// i don't think anyone is doing that yet though, and i don't have time to look at this right now
		exprContextSetAllowRuntimePartition(lokContext);
	}
	lokContext->parent = pParentContext;
	return lokContext;
}

// DANGER: Applies the given reward table to every ent in the array  Uses the supplied expression to create
// a numeric input for each ent separately.  Can be quite expensive, so use this expression sparingly.
// If you use this function to give reward tables with non-numeric rewards, those rewards will materialize at the
// players' feet (which may be a design faux pas).
// Example: GetAllMapPlayers().GrantRewardToEntArray("RewardTableName", { PlayerMapValueGet(targetEnt, "Score")})
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GrantRewardToEntArrayWithScale) ACMD_EXPR_STATIC_CHECK(exprFuncGrantRewardToEntArrayWithScale_StaticCheck);
void exprFuncGrantRewardToEntArrayWithScale(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, ACMD_EXPR_SUBEXPR_IN subExpr)
{
	ExprContext* pRewardContext;
	int i;
	MultiVal answer = {0};
	RewardTable *pTable = NULL;
	ItemChangeReason reason = {0};

	if(!(pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name)))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find reward table %s in GrantRewardToEntArray", reward_table_name);
		return;
	}

	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);

	pRewardContext = reward_GetGrantRewardSubContext(context);

	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		Entity* pTargetEnt = (*entsIn)[i];
		int level = GetEntLevel(pTargetEnt);
		exprContextSetPointerVarPooledCached(pRewardContext, targetEntString, (*entsIn)[i],
			parse_Entity, false, true, &targetEntVarHandle);
		exprContextSetPartition(pRewardContext, iPartitionIdx);
		exprEvaluateSubExpr(subExpr, context, pRewardContext, &answer, false);
		inv_FillItemChangeReason(&reason, pTargetEnt, "Expression:GrantRewardToEntArrayWithScale", NULL);
		reward_PowerExec(pTargetEnt, pTable, level, MultiValGetFloat(&answer, NULL), 0, &reason);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void exprFuncGrantPvPRewardToEntArray_StaticCheck(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, const char* teamScoreValueName, const char* opponentScoreValueName, int teamWon, const char* personalScoreValueName, const char* personalDeathsValueName, int gameLength, const char* personalDamageValueName, const char* personalHealingValueName)
{
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);
}

// Grant a reward according to a hardcoded PvP formula.  Less flexible than the above, but keeps designer expressions simpler and avoids typos
// The PvP equation is: ((teamScore / (teamScore + opponentScore + 1)) + teamWon + (0.75 * ((personalScore + teamScore) / 2) / (teamScore + 1))) / 3
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GrantPvPRewardToEntArray) ACMD_EXPR_STATIC_CHECK(exprFuncGrantPvPRewardToEntArray_StaticCheck);
void exprFuncGrantPvPRewardToEntArray(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx,ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, const char* teamScoreValueName, const char* opponentScoreValueName, int teamWon, const char* personalScoreValueName, const char* personalDeathsValueName, int gameLength, const char* personalDamageValueName, const char* personalHealingValueName)
{
	int i;
	MultiVal answer = {0};
	RewardTable *pTable = NULL;
	MapState *state = mapState_FromPartitionIdx(iPartitionIdx);
	MultiVal* teamScoreMV = mapState_GetValue(state,teamScoreValueName);
	MultiVal* opponentScoreMV = mapState_GetValue(state,opponentScoreValueName);
	float teamScore, opponentScore;
	ItemChangeReason reason = {0};

	if(!teamScoreMV)
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find map value %s in GrantPvPRewardToEntArray", teamScoreValueName);
		return;
	}
	else
	{
		teamScore = MultiValGetInt(teamScoreMV, NULL);
	}
	if(!opponentScoreMV)
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find map value %s in GrantPvPRewardToEntArray", opponentScoreValueName);
		return;
	}
	else
	{
		opponentScore = MultiValGetInt(opponentScoreMV, NULL);
	}

	if(!(pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name)))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find reward table %s in GrantRewardToEntArray", reward_table_name);
		return;
	}

	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);

	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		Entity* pTargetEnt = (*entsIn)[i];
		int level = GetEntLevel(pTargetEnt);
		float scaleFactor;
		MultiVal* playerScoreMV = mapState_GetPlayerValue(state, pTargetEnt, personalScoreValueName);
		MultiVal* playerDeathsMV = mapState_GetPlayerValue(state, pTargetEnt, personalDeathsValueName);
		MultiVal* playerDamageMV = mapState_GetPlayerValue(state, pTargetEnt, personalDamageValueName);
		MultiVal* playerHealingMV = mapState_GetPlayerValue(state, pTargetEnt, personalHealingValueName);
		int playerScore;
		int playerDeaths;
		int playerDamage;
		int playerHealing;

		if(!playerScoreMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantPvPRewardToEntArray", personalScoreValueName);
			playerScore = 0;
		}
		else
		{
			playerScore = MultiValGetInt(playerScoreMV, NULL);
		}
		if(!playerDeathsMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantPvPRewardToEntArray", personalDeathsValueName);
			playerDeaths = 0;
		}
		else
		{
			playerDeaths = MultiValGetInt(playerDeathsMV, NULL);
		}
		if(!playerDamageMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantPvPRewardToEntArray", personalDamageValueName);
			playerDamage = 0;
		}
		else
		{
			playerDamage = MultiValGetInt(playerDamageMV, NULL);
		}
		if(!playerHealingMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantPvPRewardToEntArray", personalHealingValueName);
			playerHealing = 0;
		}
		else
		{
			playerHealing = MultiValGetInt(playerHealingMV, NULL);
		}

		assertmsg(teamScore + opponentScore != -1, "Team score + opponent score is invalid");
		assertmsg(teamScore != -1, "Team score is invalid");

		scaleFactor = teamWon ? 1.0 : 0;
		scaleFactor += teamScore / (teamScore + opponentScore + 1.0);
		scaleFactor += 0.75 * (((playerScore + teamScore) / 2) / (teamScore + 1));
		scaleFactor /= 3;

		// At this point, scaleFactor can approach 1, but never exceed it.  0.5 is actually doing pretty well.
		scaleFactor += scaleFactor * (gameLength / 600);	// Give bonus XP as the game goes longer.  Doubles XP for every 10 minutes the game lasted
		// At this point, for a 10-minute game, scaleFactor can be between 0 and 2, and we expect it to be around 1, with +0.5 mission reward every 10 minutes after.
		scaleFactor += (gameLength / 600 / 2.0);	// Give bonus XP as the game goes longer.  Gives 1 scalefactor for every 20 minutes the game lasted
		// At this point, for a 10-minute game, scalefactor can be between 0.5 and 2.5, and we expect it to be around 1.5, with +1 mission reward every 10 minutes after.

		// Punish players who are AFKing
		if(playerDeaths==0 && playerScore==0 && playerDamage < 1000.0 && playerHealing < 1000.0)
		{
			scaleFactor = 0;
		}

		// Just in case, cap the scaleFactor
		if(scaleFactor>7)
			scaleFactor=7;

		inv_FillItemChangeReason(&reason, pTargetEnt, "Expression:GrantPvPRewardToEntArray", NULL);

		reward_PowerExec(pTargetEnt, pTable, level, scaleFactor, true, &reason);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void exprFuncGrantFFAPvPRewardToEntArray_StaticCheck(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, const char* totalScoreValueName, int totalPlayers, int winner, const char* personalScoreValueName, int gameLength)
{
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);
}

// Grant a reward according to a hardcoded PvP formula for players on a free-for-all map.  Keeps designer expressions simpler and avoids typos
// The PvP equation is: (2 * score / (score + average_score + 1)) + winner) / 3
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GrantFFAPvPRewardToEntArray) ACMD_EXPR_STATIC_CHECK(exprFuncGrantFFAPvPRewardToEntArray_StaticCheck);
void exprFuncGrantFFAPvPRewardToEntArray(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, const char* totalScoreValueName, int totalPlayers, int winner, const char* personalScoreValueName, int gameLength)
{
	int i;
	MultiVal answer = {0};
	RewardTable *pTable = NULL;
	MapState *state = mapState_FromPartitionIdx(iPartitionIdx);
	MultiVal* totalScoreMV = mapState_GetValue(state,totalScoreValueName);
	float totalScore, avgScore;
	ItemChangeReason reason = {0};

	if(!totalScoreMV)
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find map value %s in GrantFFAPvPRewardToEntArray", totalScoreValueName);
		return;
	}
	else
	{
		totalScore = MultiValGetInt(totalScoreMV, NULL);
	}

	if(totalPlayers<=0 && eaSize(entsIn))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Trying to grant PvP reward, but was told that there are no players on the map");
		return;
	}
	else
	{
		avgScore = totalScore / totalPlayers;
	}

	if(!(pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name)))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find reward table %s in GrantRewardToEntArray", reward_table_name);
		return;
	}

	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);

	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		Entity* pTargetEnt = (*entsIn)[i];
		int level = GetEntLevel(pTargetEnt);
		float scaleFactor;
		MultiVal* playerScoreMV = mapState_GetPlayerValue(state, pTargetEnt, personalScoreValueName);
		int playerScore;
		if(!playerScoreMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantPvPRewardToEntArray", personalScoreValueName);
			playerScore = 0;
		}
		else
		{
			playerScore = MultiValGetInt(playerScoreMV, NULL);
		}

		assertmsg(playerScore + avgScore != -1, "playerScore + avgScore is invalid.");

		scaleFactor = winner ? 1.0 : 0;
		scaleFactor += 2 * playerScore / (playerScore + avgScore + 1.0);
		scaleFactor /= 3;

		// At this point, scaleFactor can approach 1, but never exceed it.  0.5 is actually doing pretty well.
		scaleFactor += scaleFactor * (gameLength / 600);	// Give bonus XP as the game goes longer.  Doubles XP for every 10 minutes the game lasted
		// At this point, for a 10-minute game, scaleFactor can be between 0 and 2, and we expect it to be around 1, with +0.5 mission reward every 10 minutes after.
		scaleFactor += (gameLength / 600 / 2.0);	// Give bonus XP as the game goes longer.  Gives 1 scalefactor for every 20 minutes the game lasted
		// At this point, for a 10-minute game, scalefactor can be between 0.5 and 2.5, and we expect it to be around 1.5, with +1 mission reward every 10 minutes after.

		// Punish AFK players.  This is a little likely to hit genuinely bad players, too.
		if(playerScore == 0)
			scaleFactor = 0;

		// Just in case, cap the scaleFactor
		if(scaleFactor>7)
			scaleFactor=7;

		inv_FillItemChangeReason(&reason, pTargetEnt, "Expression:GrantFFAPvPRewardToEntArray", NULL);

		reward_PowerExec(pTargetEnt, pTable, level, scaleFactor, true, &reason);
	}
}

AUTO_EXPR_FUNC_STATIC_CHECK;
void exprFuncGrantGrantStrongholdPvPRewardToEntArray_StaticCheck(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, const char* teamScoreValueName, const char* opponentScoreValueName, const char* teamKillsValueName, int teamWon, const char* personalKillsValueName, const char* personalDeathsValueName, int gameLength)
{
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);
}

// Grant a reward according to a hardcoded PvP formula for players on the stronghold apocalypse map.  Keeps designer expressions simpler and avoids typos
// Very similar to the normal table, except that team score is different than the total kills on the map.
// The PvP equation is: ((teamScore / (teamScore + opponentScore + 1)) + teamWon + (personalKills / (teamKills + 1))) / 3
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GrantStrongholdPvPRewardToEntArray) ACMD_EXPR_STATIC_CHECK(exprFuncGrantGrantStrongholdPvPRewardToEntArray_StaticCheck);
void exprFuncGrantStrongholdPvPRewardToEntArray(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx,ACMD_EXPR_ENTARRAY_IN entsIn, const char* reward_table_name, const char* teamScoreValueName, const char* opponentScoreValueName, const char* teamKillsValueName, int teamWon, const char* personalKillsValueName, const char* personalDeathsValueName, int gameLength)
{
	int i;
	MultiVal answer = {0};
	RewardTable *pTable = NULL;
	MapState *state = mapState_FromPartitionIdx(iPartitionIdx);
	MultiVal* teamScoreMV = mapState_GetValue(state,teamScoreValueName);
	MultiVal* opponentScoreMV = mapState_GetValue(state,opponentScoreValueName);
	MultiVal* teamKillsMV = mapState_GetValue(state,teamKillsValueName);
	float teamScore, opponentScore, teamKills;
	ItemChangeReason reason = {0};

	if(!teamScoreMV)
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find map value %s in GrantStrongholdPvPRewardToEntArray", teamScoreValueName);
		return;
	}
	else
	{
		teamScore = MultiValGetInt(teamScoreMV, NULL);
	}
	if(!opponentScoreMV)
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find map value %s in GrantStrongholdPvPRewardToEntArray", opponentScoreValueName);
		return;
	}
	else
	{
		opponentScore = MultiValGetInt(opponentScoreMV, NULL);
	}
	if(!teamKillsMV)
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find map value %s in GrantStrongholdPvPRewardToEntArray", teamKillsValueName);
		return;
	}
	else
	{
		teamKills = MultiValGetInt(teamKillsMV, NULL);
	}

	if(!(pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name)))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find reward table %s in GrantStrongholdPvPRewardToEntArray", reward_table_name);
		return;
	}

	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);

	for(i = eaSize(entsIn) - 1; i >= 0; i--)
	{
		Entity* pTargetEnt = (*entsIn)[i];
		int level = GetEntLevel(pTargetEnt);
		float scaleFactor;
		MultiVal* playerKillsMV = mapState_GetPlayerValue(state, pTargetEnt, personalKillsValueName);
		MultiVal* playerDeathsMV = mapState_GetPlayerValue(state, pTargetEnt, personalDeathsValueName);
		int playerKills;
		int playerDeaths;
		if(!playerKillsMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantStrongholdPvPRewardToEntArray", personalKillsValueName);
			playerKills = 0;
		}
		else
		{
			playerKills = MultiValGetInt(playerKillsMV, NULL);
		}
		if(!playerDeathsMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantStrongholdPvPRewardToEntArray", personalDeathsValueName);
			playerDeaths = 0;
		}
		else
		{
			playerDeaths = MultiValGetInt(playerDeathsMV, NULL);
		}

		assertmsg(teamScore + opponentScore != -1, "teamScore + opponentScore is invalid.");
		assertmsg(teamKills != -1, "teamKills is invalid.");

		scaleFactor = teamWon ? 1.0 : 0;
		scaleFactor += teamScore / (teamScore + opponentScore + 1.0);
		scaleFactor += playerKills / (teamKills + 1);
		scaleFactor /= 3;

		// At this point, scaleFactor can approach 1, but never exceed it.  0.5 is actually doing pretty well.
		scaleFactor += scaleFactor * (gameLength / 600);	// Give bonus XP as the game goes longer.  Doubles XP for every 10 minutes the game lasted
		// At this point, for a 10-minute game, scaleFactor can be between 0 and 2, and we expect it to be around 1, with +0.5 mission reward every 10 minutes after.
		scaleFactor += (gameLength / 600 / 2.0);	// Give bonus XP as the game goes longer.  Gives 1 scalefactor for every 20 minutes the game lasted
		// At this point, for a 10-minute game, scalefactor can be between 0.5 and 2.5, and we expect it to be around 1.5, with +1 mission reward every 10 minutes after.

		// Punish players who are AFKing
		if(playerDeaths==0 && playerKills==0)
		{
			scaleFactor = 0;
		}

		// Just in case, cap the scaleFactor
		if(scaleFactor>7)
			scaleFactor=7;

		inv_FillItemChangeReason(&reason, pTargetEnt, "Expression:GrantStrongholdPvPRewardToEntArray", NULL);

		reward_PowerExec(pTargetEnt, pTable, level, scaleFactor, true, &reason);
	}
}

AUTO_EXPR_FUNC_STATIC_CHECK;
void exprFuncGrantZombiePvPRewardToEntArray_StaticCheck(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN allPlayers, const char* reward_table_name, const char* personalTimeValueName, const char *personalKillsValueName, const char* winnerValueName, int gameLength)
{
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);
}

// Grant a reward according to a hardcoded PvP formula for players on the zombie apocalypse map.  Keeps designer expressions simpler and avoids typos
// Very similar to the FFA table, except that score = time + 20*kills, and the total score is calculated rather than passed in.
// The PvP equation is: (2 * score / (score + average_score + 1)) + winner) / 3
// Must be passed all players on the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GrantZombiePvPRewardToEntArray) ACMD_EXPR_STATIC_CHECK(exprFuncGrantZombiePvPRewardToEntArray_StaticCheck);
void exprFuncGrantZombiePvPRewardToEntArray(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN allPlayers, const char* reward_table_name, const char* personalTimeValueName, const char *personalKillsValueName, const char* winnerValueName, int gameLength)
{
	int i;
	MultiVal answer = {0};
	RewardTable *pTable = NULL;
	int totalScore=0;
	int numPlayers=0;
	float avgScore;
	MapState *pState;
	ItemChangeReason reason = {0};

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	// Calculate the total score
	for(i = eaSize(allPlayers) - 1; i >= 0; i--)
	{
		Entity* pTargetEnt = (*allPlayers)[i];

		MultiVal* playerTimeMV = mapState_GetPlayerValue(pState, pTargetEnt, personalTimeValueName);
		MultiVal* playerKillsMV = mapState_GetPlayerValue(pState, pTargetEnt, personalKillsValueName);
		int playerTime;
		int playerKills;
		int playerScore;

		if(!playerTimeMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantZombiePvPRewardToEntArray", personalTimeValueName);
			playerTime = 0;
		}
		else
		{
			playerTime = MultiValGetInt(playerTimeMV, NULL);
		}
		if(!playerKillsMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantZombiePvPRewardToEntArray", personalKillsValueName);
			playerKills = 0;
		}
		else
		{
			playerKills = MultiValGetInt(playerKillsMV, NULL);
		}

		playerScore = playerTime + 30 * playerKills;
		totalScore += playerScore;
		numPlayers++;
	}

	avgScore = totalScore / (float) numPlayers;

	if(!(pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name)))
	{
		ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find reward table %s in GrantZombiePvPRewardToEntArray", reward_table_name);
		return;
	}

	rewardtable_InvalidDirectDropCheck(context, pTable, RewardContextType_PowerExec);

	// Re-get the values from the multiVal player values.
	for(i = eaSize(allPlayers) - 1; i >= 0; i--)
	{
		Entity* pTargetEnt = (*allPlayers)[i];
		int level = GetEntLevel(pTargetEnt);
		MultiVal* playerTimeMV = mapState_GetPlayerValue(pState, pTargetEnt, personalTimeValueName);
		MultiVal* playerKillsMV = mapState_GetPlayerValue(pState, pTargetEnt, personalKillsValueName);
		MultiVal* winnerMV = mapState_GetPlayerValue(pState, pTargetEnt, winnerValueName);
		int playerTime;
		int playerKills;
		int playerScore;
		int winner;
		float scaleFactor;

		if(!playerTimeMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantZombiePvPRewardToEntArray", personalTimeValueName);
			playerTime = 0;
		}
		else
		{
			playerTime = MultiValGetInt(playerTimeMV, NULL);
		}
		if(!playerKillsMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantZombiePvPRewardToEntArray", personalKillsValueName);
			playerKills = 0;
		}
		else
		{
			playerKills = MultiValGetInt(playerKillsMV, NULL);
		}
		if(!winnerMV)
		{
			ErrorFilenamef(exprContextGetBlameFile(context), "Couldn't find player map value %s in GrantPvPRewardToEntArray", winnerValueName);
			winner = 0;
		}
		else
		{
			winner = MultiValGetInt(winnerMV, NULL);
		}

		playerScore = playerTime + 30 * playerKills;

		assertmsg(playerScore + avgScore != -1, "playerScore + avgScore is invalid.");

		scaleFactor = winner ? 0.5 : 0;
		scaleFactor += 2 * playerScore / (playerScore + avgScore + 1.0);
		scaleFactor /= 2.5;

		// At this point, scaleFactor can approach 1, but never exceed it.  0.5 is actually doing pretty well.
		scaleFactor += scaleFactor * (gameLength / 600);	// Give bonus XP as the game goes longer.  Doubles XP for every 10 minutes the game lasted
		// At this point, for a 10-minute game, scaleFactor can be between 0 and 2, and we expect it to be around 1, with +0.5 mission reward every 10 minutes after.
		scaleFactor += (gameLength / 600 / 2.0);	// Give bonus XP as the game goes longer.  Gives 1 scalefactor for every 20 minutes the game lasted
		// At this point, for a 10-minute game, scalefactor can be between 0.5 and 2.5, and we expect it to be around 1.5, with +1 mission reward every 10 minutes after.

		if(playerScore == 0)
			scaleFactor = 0;

		// Just in case, cap the scaleFactor
		if(scaleFactor>7)
			scaleFactor=7;

		inv_FillItemChangeReason(&reason, pTargetEnt, "Expression:GrantZombiePvPRewardToEntArray", NULL);

		reward_PowerExec(pTargetEnt, pTable, level, scaleFactor, true, &reason);
	}
}

static void LevelupNotification_Reload(const char *pchPath, S32 iWhen)
{
	S32 i;

	loadstart_printf("Loading Levelup Notifications... ");
	if (pchPath)
		fileWaitForExclusiveAccess(pchPath);

	StructDeInit(parse_LevelupNotificationGroups, &s_LevelupNotificationGroups);
	ParserLoadFiles(NULL, "defs/config/LevelupNotifications.def", "LevelupNotifications.bin", PARSER_OPTIONALFLAG, parse_LevelupNotificationGroups, &s_LevelupNotificationGroups);

	for (i = 0; i < eaSize(&s_LevelupNotificationGroups.eaLevels); i++)
	{
		LevelupNotificationGroup *pGroup = s_LevelupNotificationGroups.eaLevels[i];
		if (pGroup->pchCharacterPath && *pGroup->pchCharacterPath)
		{
			if (!RefSystem_ReferentFromString(g_hCharacterPathDict, pGroup->pchCharacterPath))
			{
				if (stricmp(pGroup->pchCharacterPath, "None")!=0 && stricmp(pGroup->pchCharacterPath, "Any")!=0)
				{
					ErrorFilenamef(pchPath, "CharacterPath reference \"%s\" not found. Expected None, Any, or a valid CharacterPath name.", pGroup->pchCharacterPath);
				}
			}
		}
	}

	loadend_printf("Done. (%d)", eaSize(&s_LevelupNotificationGroups.eaLevels));
}

AUTO_STARTUP(RewardsServer) ASTRT_DEPS(RewardsCommon, RewardTables, RewardValTables, AlgoTables, AlgoTablesCommon, ItemGen, AS_Messages, CharacterPaths);
void ServerRewardStartup(void)
{
	char *pcBinFile = NULL;
	char *pcRewardsDir = NULL;

	GameBranch_GetDirectory(&pcRewardsDir, REWARDS_BASE_DIR);

	resLoadResourcesFromDisk(g_hRewardTierDict, pcRewardsDir, ".rewardtier", 
		GameBranch_GetFilename(&pcBinFile, "RewardTier.bin"), 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	
	resLoadResourcesFromDisk(g_hDropRateTableDict, pcRewardsDir, ".droprate", 
		GameBranch_GetFilename(&pcBinFile, "DropRate.bin"),
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	
	resLoadResourcesFromDisk(g_hDropSetDict, pcRewardsDir, ".dropset", 
		GameBranch_GetFilename(&pcBinFile, "DropSet.bin"), 
		PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	
	estrDestroy(&pcBinFile);
	estrDestroy(&pcRewardsDir);

	LevelupNotification_Reload("defs/config/LevelupNotifications.def", 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/LevelupNotifications.def", LevelupNotification_Reload);
}

AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Inventory);
void InventoryPrintNumerics(Entity *pEnt)
{
	int i;

	if(!SAFE_MEMBER(pEnt,pInventoryV2))
		return;

	gslSendPrintf(pEnt,"Inventory Numerics ====================");
	for(i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i>=0; --i)
	{
		int ii;
		InventoryBagLite *bag = (InventoryBagLite*)inv_GetLiteBag(pEnt, InvBagIDs_Numeric, NULL);
		for(ii = eaSize(&bag->ppIndexedLiteSlots)-1; ii>=0; --ii)
		{
			gslSendPrintf(pEnt,"%s: %i", bag->ppIndexedLiteSlots[ii]->pchName, bag->ppIndexedLiteSlots[ii]->count);
		}
	}
}


static int rewardtierValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, RewardTier *tier, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_BINNING:
		if(tier->min_level > tier->max_level)
		{
			ErrorFilenamef( tier->file, "RewardTier range invalid %i %i", tier->min_level, tier->max_level);
			tier->max_level = tier->min_level;
		}

		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterDropRateDict(void)
{
	g_hDropRateTableDict = RefSystem_RegisterSelfDefiningDictionary("DropRate",false, parse_DropRateTable, true, true, NULL);
	g_hDropSetDict = RefSystem_RegisterSelfDefiningDictionary("DropSet",false, parse_DropSet, true, true, NULL);
	g_hRewardTierDict = RefSystem_RegisterSelfDefiningDictionary("RewardTier",false, parse_RewardTier, true, true, NULL);
	resDictManageValidation(g_hRewardTierDict, rewardtierValidateCB);
}

AUTO_RUN;
void AutoEnableRewardLogging(void)
{
	if (gConf.bAutoRewardLogging)
	s_RewardLog = 1;
}

static void TimestampItemLog(char **ppchLine)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	// Yeah, this is the slow crappy way to do it
	estrPrintf(ppchLine,"%02d:%02d:%02d:%02d:%02d:%02d.%1d::",t.wYear%100,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds/100);
}

void reward_logDroprate(Entity* pEnt, bool droppedItem)
{
	EntityIterator *iter;
	Entity* pent;
	char* pchLine = NULL;
	iter = entGetIteratorSingleType(pEnt ? entGetPartitionIdx(pEnt) : PARTITION_ANY, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

	estrStackCreate(&pchLine);
	
	TimestampItemLog(&pchLine);

	FormatMessageKey(&pchLine,"RewardLog.Item",
		STRFMT_STRING("Critter",pEnt?entGetLocalName(pEnt):"Non_Combat_Drop"),
		STRFMT_STRING("Assignee","Droprate"),
		STRFMT_STRING("Item",droppedItem ? "Loot" : "No Loot"),
		STRFMT_STRING("ItemDef",droppedItem ? "Loot" : "No Loot"),
		STRFMT_INT("Num",1),
		STRFMT_END);

	estrAppend2(&pchLine,"\n");


	while(pent = EntityIteratorGetNext(iter))
	{
		if (entGetAccessLevel(pent) == 9)
		{
			ClientCmd_PrintToLootLog(pent, pchLine);
		}
	}
	EntityIteratorRelease(iter);


	estrDestroy(&pchLine);
}

void reward_logBag(int iPartitionIdx, InventoryBag* pBag, RewardContext* pContext, Entity* pAssignee)
{
	Item** itemList = NULL;
	int numItems = inv_bag_GetSimpleItemList(pBag, &itemList, false);
	int ii;
	Entity* pOwner;
	for (ii = 0; ii < numItems; ii++)
	{
		ItemDef *pDef = GET_REF(itemList[ii]->hItem);
		if (!pDef)
			continue;
		pOwner = pAssignee ? pAssignee : entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, itemList[ii]->owner);
		reward_logItem(pContext, pOwner, itemList[ii], pDef, itemList[ii]->count ? itemList[ii]->count : 1);
	}
	eaDestroy(&itemList);
}

void reward_logItem(RewardContext* pContext, Entity* pAssignee, Item* pItem, ItemDef* pItemDef, int num)
{
	Entity* eKilled = pContext ? pContext->pKilled : NULL;
	char* pchLine = NULL;
	EntityIterator *iter;
	Entity *pent;

	if (!pItemDef) return;
	iter = entGetIteratorSingleType(pAssignee ? entGetPartitionIdx(pAssignee) : PARTITION_ANY, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

	estrStackCreate(&pchLine);

	TimestampItemLog(&pchLine);

	FormatMessageKey(&pchLine,"RewardLog.Item",
		STRFMT_STRING("Critter",eKilled?entGetLocalName(eKilled):"Non_Combat_Drop"),
		STRFMT_STRING("Assignee",pAssignee ? entGetLocalName(pAssignee):"Not_Assigned"),
		STRFMT_STRING("Item",pItem ? item_GetNameLang(pItem, locGetLanguage(getCurrentLocale()),pAssignee):pItemDef->pchName),
		STRFMT_STRING("ItemDef",pItemDef?pItemDef->pchName:NULL),
		STRFMT_INT("Num",num),
		STRFMT_END);

	estrAppend2(&pchLine,"\n");


	while(pent = EntityIteratorGetNext(iter))
	{
		if (entGetAccessLevel(pent) == 9)
		{
			ClientCmd_PrintToLootLog(pent, pchLine);
		}
	}
	EntityIteratorRelease(iter);


	estrDestroy(&pchLine);
}

void reward_SetupTeamLootBag(int iPartitionIdx, InventoryBag *pBag, Team *pTeam, bool bDefaultToRoundRobin)
{
	KillCreditTeam *pCreditTeam = NULL;
	int i;

	pCreditTeam = StructCreate(parse_KillCreditTeam);
	if (pTeam)
	{
		for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
		{
			Entity *pEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
			KillCreditEntity *pCreditEnt = NULL;

			if (pEnt)
			{
				pCreditEnt = StructCreate(parse_KillCreditEntity);
				pCreditEnt->entRef = pEnt->myRef;
				eaPush(&pCreditTeam->eaMembers, pCreditEnt);
			}
		}
	}

	GiveRewardForTeam(iPartitionIdx, pBag, pTeam, NULL, NULL, reward_Get_Reward_Index(iPartitionIdx, pCreditTeam), NULL, false, pCreditTeam, bDefaultToRoundRobin);
	StructDestroy(parse_KillCreditTeam, pCreditTeam);
}

static void GiveLootBagToPlayer(int iPartitionIdx, Entity* pEnt, int iLevel, F32 fScale, Vec3 vPos, RewardTable* pTable, PlayerCostume* pYoursCostume, PlayerCostume* pNotYoursCostume, const ItemChangeReason *pReason)
{
	InventoryBag** eaBags = NULL;
	int i;
	reward_GenerateBagFromTable(iPartitionIdx, pEnt, pTable, &eaBags, iLevel, fScale, NULL, RewardContextType_EntKill, 0, false, NULL);
	
	for (i = 0; i < eaSize(&eaBags); i++) {
		InventoryBag* pBag = eaBags[i];
		if (pBag->pRewardBagInfo) {
			if (pYoursCostume) {
				SET_HANDLE_FROM_REFERENT(g_hPlayerCostumeDict, pYoursCostume, pBag->pRewardBagInfo->hYoursCostumeRef);
			} else {
				SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, "YourVisibleDrop", pBag->pRewardBagInfo->hYoursCostumeRef);
			}
			if (pNotYoursCostume) {
				SET_HANDLE_FROM_REFERENT(g_hPlayerCostumeDict, pNotYoursCostume, pBag->pRewardBagInfo->hNotYoursCostumeRef);
			} else {
				SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, "NotYourVisibleDrop", pBag->pRewardBagInfo->hNotYoursCostumeRef);
			}
		}
		reward_GiveBag(pEnt, pBag, vPos, false, pReason);
	}
	eaDestroy(&eaBags);
}

static void GetNamedPointsFromString(const char* pchNamedPoints, GameNamedPoint*** pppNamedPoints)
{
	char* pchContext;
	char* pchStart;
	char* pchNamedPointsCopy;
	if (!pchNamedPoints || !pchNamedPoints[0]) {
		return;
	}
	strdup_alloca(pchNamedPointsCopy, pchNamedPoints);
	pchStart = strtok_r(pchNamedPointsCopy, " ,\t\r\n", &pchContext);
	do {
		if (pchStart) {
			GameNamedPoint *pPoint = namedpoint_GetByName(pchStart, NULL);
			if (pPoint)
			{
				eaPush(pppNamedPoints, pPoint);
			}
			else
			{
				Errorf("Invalid GameNamedPoint %s", pchStart);
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
}

// Generates a loot bag for each player in entsIn
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GiveLootBagToEachPlayerWithScale);
void exprFunc_GiveLootBagToEachPlayerWithScale(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_NAMELIST("RewardTable", REFDICTIONARY) char *reward_table_name, char* pchNamedPoints, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchYoursCostumeOverride, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchNotYoursCostumeOverride, ACMD_EXPR_SUBEXPR_IN subExpr)
{
	Vec3 vPos;
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	PlayerCostume *pYoursCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchYoursCostumeOverride);
	PlayerCostume *pNotYoursCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchNotYoursCostumeOverride);
	GameNamedPoint** ppNamedPoints = NULL;
	ExprContext* pRewardContext;
	int i, iNamedPointsSize;
	ItemChangeReason reason = {0};

	if (!pTable || !entsIn) {
		return;
	}

	GetNamedPointsFromString(pchNamedPoints, &ppNamedPoints);
	iNamedPointsSize = eaSize(&ppNamedPoints);
	pRewardContext = reward_GetGrantRewardSubContext(pContext);

	for (i = eaSize(entsIn)-1; i >= 0; i--)
	{
		Entity* pEnt = (*entsIn)[i];
		int iCombatLevel;
		F32 fScale = 1.0f;
		GameNamedPoint* pNamedPoint = NULL;

		if (!pEnt || !pEnt->pChar || !pEnt->pChar->bLoaded)
			continue;

		iCombatLevel = entity_GetCombatLevel(pEnt);

		if (iNamedPointsSize) {
			pNamedPoint = eaGet(&ppNamedPoints, (i % iNamedPointsSize));
		}
		if(!namedpoint_GetPosition(pNamedPoint, vPos, NULL)){
			entGetPos(pEnt, vPos);
		}
		if (subExpr)
		{
			MultiVal answer = {0};
			exprContextSetPointerVarPooledCached(pRewardContext, targetEntString, pEnt, parse_Entity, false, true, &targetEntVarHandle);
			exprContextSetPartition(pRewardContext, iPartitionIdx);
			exprEvaluateSubExpr(subExpr, pContext, pRewardContext, &answer, false);
			fScale = MultiValGetFloat(&answer, NULL);
		}
		inv_FillItemChangeReason(&reason, pEnt, "Misc:ExpressionGrantedReward", NULL);
		GiveLootBagToPlayer(iPartitionIdx, pEnt, iCombatLevel, fScale, vPos, pTable, pYoursCostume, pNotYoursCostume, &reason);
	}

	eaDestroy(&ppNamedPoints);
}

// Generates a loot bag for each player in entsIn
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GiveLootBagToEachPlayer);
void exprFunc_GiveLootBagToEachPlayer(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_NAMELIST("RewardTable", REFDICTIONARY) char *reward_table_name, char* pchNamedPoints, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchYoursCostumeOverride, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchNotYoursCostumeOverride)
{
	exprFunc_GiveLootBagToEachPlayerWithScale(pContext, iPartitionIdx, entsIn, reward_table_name, pchNamedPoints, pchYoursCostumeOverride, pchNotYoursCostumeOverride, NULL);
}

// Generates a loot bag for each team member
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GiveLootBagToEachTeamMember);
void exprFunc_GiveLootBagToEachTeamMember(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_NAMELIST("RewardTable", REFDICTIONARY) char *reward_table_name, char* pchNamedPoints, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchYoursCostumeOverride, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchNotYoursCostumeOverride)
{
	Entity* pEnt = entsIn && (*entsIn) && eaSize(entsIn) ? (*entsIn)[0] : NULL;
	Team* pTeam = pEnt ? team_GetTeam(pEnt) : NULL;
	Vec3 vPos;
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	PlayerCostume *pYoursCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchYoursCostumeOverride);
	PlayerCostume *pNotYoursCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchNotYoursCostumeOverride);
	GameNamedPoint** ppNamedPoints = NULL;
	int i;
	ItemChangeReason reason = {0};

	if (!pTable || !pEnt) {
		return;
	}

	GetNamedPointsFromString(pchNamedPoints, &ppNamedPoints);

	if (pTeam) {
		int iTeamSize = eaSize(&pTeam->eaMembers);
		int iNamedPointsSize = eaSize(&ppNamedPoints);
		for (i = 0; i < iTeamSize; i++) {
			TeamMember* pTeamMember = pTeam->eaMembers[i];
			Entity* pMemberEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
			int iCombatLevel;
			GameNamedPoint* pNamedPoint = NULL;

			if (!pMemberEnt || !pMemberEnt->pChar || !pMemberEnt->pChar->bLoaded)
				continue;

			iCombatLevel = entity_GetCombatLevel(pMemberEnt);

			if (iNamedPointsSize) {
				pNamedPoint = eaGet(&ppNamedPoints, (i % iNamedPointsSize));
			}
			if(!namedpoint_GetPosition(pNamedPoint, vPos, NULL)){
				entGetPos(pMemberEnt, vPos);
			}
			inv_FillItemChangeReason(&reason, pMemberEnt, "Misc:ExpressionGrantedReward", NULL);
			GiveLootBagToPlayer(iPartitionIdx, pMemberEnt, iCombatLevel, 1.0f, vPos, pTable, pYoursCostume, pNotYoursCostume, &reason);
		}
	} else if (pEnt->pChar && pEnt->pChar->bLoaded) {
		int iCombatLevel = entity_GetCombatLevel(pEnt);
		GameNamedPoint* pNamedPoint = eaGet(&ppNamedPoints, 0);

		if(!namedpoint_GetPosition(pNamedPoint, vPos, NULL)){
			entGetPos(pEnt, vPos);
		}
		inv_FillItemChangeReason(&reason, pEnt, "Misc:ExpressionGrantedReward", NULL);
		GiveLootBagToPlayer(iPartitionIdx, pEnt, iCombatLevel, 1.0f, vPos, pTable, pYoursCostume, pNotYoursCostume, &reason);
	}

	eaDestroy(&ppNamedPoints);
}

// Generates a loot bag at the specified position with the specified costume for the first entity in the passed in entity array.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GiveLootBagToTeam);
void exprFunc_GiveLootBagToTeam(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_NAMELIST("RewardTable", REFDICTIONARY) char *reward_table_name, char* pchNamedPoint, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchYoursCostumeOverride, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY) char* pchNotYoursCostumeOverride)
{
	Entity* pEnt = entsIn && (*entsIn) && eaSize(entsIn) ? (*entsIn)[0] : NULL;
	Team* pTeam = pEnt ? team_GetTeam(pEnt) : NULL;
	Vec3 vPos;
	RewardTable *pTable = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	PlayerCostume *pYoursCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchYoursCostumeOverride);
	PlayerCostume *pNotYoursCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pchNotYoursCostumeOverride);
	int i;

	if(!pTable || !pEnt){
		return;
	}

	if(!namedpoint_GetPositionByName(pchNamedPoint, vPos, NULL)){
		entGetPos(pEnt, vPos);
	}

	if(pTeam){
		InventoryBag** eaBags = NULL;
		KillCreditTeam *pCreditTeam = NULL;

		pCreditTeam = StructCreate(parse_KillCreditTeam);
		pCreditTeam->iTeamID = team_GetTeamID(pEnt);
		pCreditTeam->firstMemberEntRef = entGetRef(pEnt);
		pCreditTeam->iValidTeamSize = 1;	// default size

		pCreditTeam->iTeamCombatLevel = encounter_getTeamLevelInRange(pEnt, &pCreditTeam->iValidTeamSize, true);
		
		reward_GenerateBagFromTable(iPartitionIdx, pEnt, pTable, &eaBags, pCreditTeam->iTeamCombatLevel, 1.0f, NULL, RewardContextType_EntKill, 0, false, NULL);
		
		for(i = 0; i < eaSize(&eaBags); i++){
			InventoryBag* pBag = eaBags[i];
			if(pBag->pRewardBagInfo) {
				if(pYoursCostume){
					SET_HANDLE_FROM_REFERENT(g_hPlayerCostumeDict, pYoursCostume, pBag->pRewardBagInfo->hYoursCostumeRef);
				} else {
					SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, "YourVisibleDrop", pBag->pRewardBagInfo->hYoursCostumeRef);
				}
				if(pNotYoursCostume){
					SET_HANDLE_FROM_REFERENT(g_hPlayerCostumeDict, pNotYoursCostume, pBag->pRewardBagInfo->hNotYoursCostumeRef);
				} else {
					SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, "NotYourVisibleDrop", pBag->pRewardBagInfo->hNotYoursCostumeRef);
				}
			}
			GiveRewardForTeam(iPartitionIdx, pBag, pTeam, pEnt, vPos, reward_Get_Reward_Index(iPartitionIdx, pCreditTeam), NULL, true, pCreditTeam, -1);
		}
		StructDestroy(parse_KillCreditTeam, pCreditTeam);
		eaDestroy(&eaBags);
	} else if (pEnt->pChar && pEnt->pChar->bLoaded) {
		int iCombatLevel = entity_GetCombatLevel(pEnt);
		ItemChangeReason reason = {0};
		inv_FillItemChangeReason(&reason, pEnt, "Expression:GiveLootBagToTeam", NULL);
		GiveLootBagToPlayer(iPartitionIdx, pEnt, iCombatLevel, 1.0f, vPos, pTable, pYoursCostume, pNotYoursCostume, &reason);
	}
}

// Checks the reward context to see if the given reward tag exists
AUTO_EXPR_FUNC(reward) ACMD_NAME("HasRewardTag"); 
bool exprFuncHasRewardTag(ExprContext *pContext, SA_PARAM_NN_STR const char *pchRewardTag)
{
	devassert(pchRewardTag && pchRewardTag[0]);

	if (pchRewardTag == NULL || pchRewardTag[0] == '\0')
		return false;
	else
	{
		RewardContext *pRewardContext = exprContextGetVarPointer(pContext, "Reward", parse_RewardContext);
		if (pRewardContext == NULL)
		{
			return false;
		}
		else
		{
			S32 i;
			for (i = 0; i < eaiSize(&pRewardContext->piRewardTags); i++)
			{
				const char *pchCurrentTagName = StaticDefineIntRevLookup(RewardTagsEnum, pRewardContext->piRewardTags[i]);
				if (pchCurrentTagName && stricmp(pchCurrentTagName, pchRewardTag) == 0)
					return true;
			}
			return false;
		}
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Earewardmods");
enumTransactionOutcome trEntity_RegenRewardModList(ATR_ARGS, NOCONST(Entity) *pEnt, NON_CONTAINER RewardModifierList *pRewardModList)
{
	int i;
	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
		TRANSACTION_RETURN_FAILURE("Couldn't update the entity's reward mod list because it is an invalid entity");

	eaDestroyStructNoConst(&pEnt->pPlayer->eaRewardMods, parse_RewardModifier);

	eaIndexedEnableNoConst(&pEnt->pPlayer->eaRewardMods, parse_RewardModifier);

	for(i=eaSize(&pRewardModList->ppRewardMods)-1; i>=0; i--)
	{
		//Push the structs into the cleared list
		eaPush(&pEnt->pPlayer->eaRewardMods, CONTAINER_NOCONST(RewardModifier, pRewardModList->ppRewardMods[i]));
	}
	//Clear the earray so the transaction system doesn't delete these
	eaClear(&pRewardModList->ppRewardMods);

	return TRANSACTION_OUTCOME_SUCCESS;
}

//Wrapper function to regenerate the reward mod list that hangs off pEnt->pPlayer
void rewards_RegenRewardModList(Entity *pEnt, RewardModifierList *pRewardModList, S32 bTest)
{
	int s;

	if(!pEnt || !pRewardModList)
		return;

	// Test to see if the new list is the same as the list on the entity
	s = eaSize(&pRewardModList->ppRewardMods);
	if(pEnt->pPlayer && eaSize(&pEnt->pPlayer->eaRewardMods)==s)
	{
		for(s=s-1; s>=0; s--)
		{
			RewardModifier *pNewMod = pRewardModList->ppRewardMods[s];
			RewardModifier *pOldMod = pEnt->pPlayer->eaRewardMods[s];
			if ((pOldMod->pchNumeric != pNewMod->pchNumeric) || !nearSameF32(pOldMod->fFactor,pNewMod->fFactor)) 
			{
				break;
			}
		}

		if(s<0)
			return;
	}

	AutoTrans_trEntity_RegenRewardModList(NULL, GLOBALTYPE_GAMESERVER, entGetType(pEnt),entGetContainerID(pEnt), pRewardModList);
}

void reward_MapValidate(ZoneMap* pZoneMap)
{
	ZoneMapInfo* pZoneInfo = zmapGetInfo(pZoneMap);
	if (!pZoneInfo)
		return;
	if (!killreward_Validate(zmapInfoGetRewardTable(pZoneInfo), zmapInfoGetFilename(pZoneInfo)))
	{
		ErrorFilenamef(zmapInfoGetFilename(pZoneInfo), "Zonemap reward table cannot be granted from killed entities.");
	}

	if (!killreward_Validate(zmapInfoGetPlayerRewardTable(pZoneInfo), zmapInfoGetFilename(pZoneInfo)))
	{
		ErrorFilenamef(zmapInfoGetFilename(pZoneInfo), "Zonemap reward table cannot be granted from killed entities.");
	}

}

AUTO_TRANS_HELPER;
void ent_trh_GetCharacterRewardContextInfo(CharacterBasedIncludeContext* pContext, ATH_ARG NOCONST(Entity)* pEnt)
{
	if(NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		CharacterPath** eaPaths = NULL;
		int i;

		eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths)+1);
		entity_trh_GetChosenCharacterPaths(pEnt, &eaPaths);

		eaClear(&pContext->eaClassPathNames);

		for (i = 0; i < eaSize(&eaPaths); i++)
			eaPush(&pContext->eaClassPathNames, eaPaths[i]->pchName);

		pContext->pchClass = REF_STRING_FROM_HANDLE(pEnt->pChar->hClass);
		pContext->pchSpecies = REF_STRING_FROM_HANDLE(pEnt->pChar->hSpecies);
		pContext->iContainerID = pEnt->myContainerID;
	}
	pContext->eGender = pEnt->eGender;
}

static void reward_OddsAddTable(RewardTable *pTable, RewardOdds *pOddsOut, F32 fCurrentOdds, U32 uDepth)
{
	if(fCurrentOdds > 0.00000001f)
	{
		RewardOddsTable *pNewTable = StructCreate(parse_RewardOddsTable);

		pNewTable->pRewardTable = pTable;
		pNewTable->fCurrentChance = fCurrentOdds;
		pNewTable->uDepth = uDepth;

		eaPush(&pOddsOut->eaRewardTables, pNewTable);
	}
	else
	{
		Errorf("Reward odds, New table %s has odds less than 0.000001%%.",pTable->pchName);
	}
}

void reward_OddsAddEntry(RewardTable *pTable, RewardOdds *pOddsOut, RewardEntry *pRewardEntry, F32 fCurrentOdds, U32 uDepth)
{
	RewardOddsEntry *pEntry = StructCreate(parse_RewardOddsEntry);

	pEntry->pRewardTable = pTable;
	pEntry->fChance = fCurrentOdds;
	pEntry->uDepth = uDepth;
	pEntry->pRewardEntry = StructClone(parse_RewardEntry, pRewardEntry);

	if((pRewardEntry->ChoiceType == kRewardChoiceType_Choice || pRewardEntry->ChoiceType == kRewardChoiceType_Include ||
		pRewardEntry->ChoiceType == kRewardChoiceType_ExpressionInclude || pRewardEntry->ChoiceType == kRewardChoiceType_CharacterBasedInclude ||
		pRewardEntry->ChoiceType == kRewardChoiceType_Expression) && 
		GET_REF(pRewardEntry->hRewardTable))
	{
		pEntry->bHasRewardTable = true;
		reward_OddsAddTable(GET_REF(pRewardEntry->hRewardTable), pOddsOut, fCurrentOdds, uDepth);
	}

	// Check for reward pack item
	if(pRewardEntry->Type == kRewardType_Item)
	{
		ItemDef *pItemDef = GET_REF(pRewardEntry->hItemDef);
		if(pItemDef && pItemDef->eType == kItemType_RewardPack && pItemDef->pRewardPackInfo && GET_REF(pItemDef->pRewardPackInfo->hRewardTable))
		{
			pEntry->bHasRewardTable = true;
			reward_OddsAddTable(GET_REF(pItemDef->pRewardPackInfo->hRewardTable), pOddsOut, fCurrentOdds, uDepth);
		}
	}

	eaPush(&pOddsOut->eaEntries, pEntry);
}

void reward_OddsWeighted(RewardTable *pTable, RewardOdds *pOddsOut, F32 fCurrentOdds, U32 uDepth)
{
	F32 fWeightTotal = 0.0f;
	bool bNoChar = true;
	F32 fNumChoices;

	fNumChoices = pTable->NumChoices;
	if(fNumChoices < 1.0f)
	{
		fNumChoices = 1.0f;
	}

	if(pOddsOut->pPlayerEnt)
	{
		bNoChar = false;
	}

	reward_CalcWeightTotal(pOddsOut->iPartition, pOddsOut->pPlayerEnt, pTable, pOddsOut->pRewardContext, &fWeightTotal, 0, bNoChar);

	// Is this table type supported
	if(fWeightTotal < 0.00000001f)
	{
		// not supported
		pOddsOut->bUnableToCalculate = true;
		return;
	}
	else
	{
		// go through all entries and record them
		S32 i;
		F32 fNewChance;

		for(i = 0; i < eaSize(&pTable->ppRewardEntry); ++i)
		{
			fNewChance = fNumChoices * fCurrentOdds * pTable->ppRewardEntry[i]->fWeight / fWeightTotal;
			switch(pTable->ppRewardEntry[i]->ChoiceType)
			{
				case kRewardChoiceType_Expression:
				{
					MultiVal result = {0};
					if(!pOddsOut->pPlayerEnt)
					{
						pOddsOut->bUnableToCalculate = true;
						return;
					}
					rewardTableExpr_EvalPlayer(pOddsOut->iPartition, pOddsOut->pPlayerEnt, pTable->ppRewardEntry[i]->pRequiresExpr, pOddsOut->pRewardContext, &result);
					if(rewardTableExpr_GetIntResult(&result,pTable->pchFileName,pTable->ppRewardEntry[i]->pRequiresExpr))
					{
						reward_OddsAddEntry(pTable, pOddsOut, pTable->ppRewardEntry[i], fNewChance, uDepth);
					}

					break;
				}
				case kRewardChoiceType_Choice:
				case kRewardChoiceType_ChoiceVariableCount:
				{
					reward_OddsAddEntry(pTable, pOddsOut, pTable->ppRewardEntry[i], fNewChance, uDepth);
					break;
				}

				case kRewardChoiceType_AlgoChar:
				case kRewardChoiceType_Empty:
				case kRewardChoiceType_AlgoBase:
				case kRewardChoiceType_AlgoCost:
				case kRewardChoiceType_Disabled:
					{
						// ignore
						break;
					}

				case kRewardChoiceType_CharacterBasedInclude:
				{
					if(!pOddsOut->pPlayerEnt)
					{
						pOddsOut->bUnableToCalculate = true;
						return;
					}
					{
						RewardTable** eaPossibleTables = NULL;
						F32 EntryTotal = 0;
						int j;

						reward_GetAllPossibleCharacterBasedTables(pOddsOut->pPlayerEnt, pOddsOut->pRewardContext->pCBIData, pTable->ppRewardEntry[i]->pchCharacterBasedIncludePrefix, pTable->ppRewardEntry[i]->eCharacterBasedIncludeType, &eaPossibleTables);
						
						for (j = 0; j < eaSize(&eaPossibleTables); j++)
						{
							fNewChance = (fNumChoices * fCurrentOdds * pTable->ppRewardEntry[i]->fWeight / fWeightTotal) / eaSize(&eaPossibleTables);
							reward_OddsAddTable(eaPossibleTables[j], pOddsOut, fNewChance, uDepth);
						}

						if (eaSize(&eaPossibleTables) <= 0)
						{
							// no table !
							Errorf("Reward odds, no table for CharacterBasedInclude entry in table %s %d", pTable->pchName, i);
							pOddsOut->bUnableToCalculate = true;
							return;
						}
					}
					break;
				}
				case kRewardChoiceType_ExpressionInclude:
				{
					MultiVal result = {0};
					if(!pOddsOut->pPlayerEnt)
					{
						pOddsOut->bUnableToCalculate = true;
						return;
					}
					rewardTableExpr_EvalPlayer(pOddsOut->iPartition, pOddsOut->pPlayerEnt, pTable->ppRewardEntry[i]->pRequiresExpr, pOddsOut->pRewardContext, &result);
					if(rewardTableExpr_GetIntResult(&result,pTable->pchFileName,pTable->ppRewardEntry[i]->pRequiresExpr))
					{
						// fall through
					}
					else
					{
						break;
					}
				}
				case kRewardChoiceType_Include:
				{
					F32 EntryTotal = 0;
					RewardTable *nested_table = GET_REF(pTable->ppRewardEntry[i]->hRewardTable);

					if(nested_table)
					{
						reward_CalcWeightTotal(pOddsOut->iPartition, pOddsOut->pPlayerEnt, nested_table, pOddsOut->pRewardContext, &EntryTotal, 0, bNoChar);
					}
					else
					{
						// no table !
						Errorf("Reward odds, no table for include entry in table %s %d", pTable->pchName, i);
						pOddsOut->bUnableToCalculate = true;
						return;
					}

					fNewChance = fNumChoices * fCurrentOdds * EntryTotal / fWeightTotal;
					reward_OddsAddEntry(pTable, pOddsOut, pTable->ppRewardEntry[i], fNewChance, uDepth);
				}
				break;

				default:
					Errorf("Reward odds Bad reward choice type in reward table %s %d", pTable->pchName, i);
					pOddsOut->bUnableToCalculate = true;
					return;
				STATIC_INFUNC_ASSERT(kRewardChoiceType_Count == 16);// update this switch statement
			}			
		}
	}
}

void reward_OddsGiveAll(RewardTable *pTable, RewardOdds *pOddsOut, F32 fCurrentOdds, U32 uDepth)
{
	// go through all entries and record them
	S32 i;

	for(i = 0; i < eaSize(&pTable->ppRewardEntry); ++i)
	{
		switch(pTable->ppRewardEntry[i]->ChoiceType)
		{
		case kRewardChoiceType_Expression:
			{
				MultiVal result = {0};
				if(!pOddsOut->pPlayerEnt)
				{
					pOddsOut->bUnableToCalculate = true;
					return;
				}
				rewardTableExpr_EvalPlayer(pOddsOut->iPartition, pOddsOut->pPlayerEnt, pTable->ppRewardEntry[i]->pRequiresExpr, pOddsOut->pRewardContext, &result);
				if(rewardTableExpr_GetIntResult(&result,pTable->pchFileName,pTable->ppRewardEntry[i]->pRequiresExpr))
				{
					reward_OddsAddEntry(pTable, pOddsOut, pTable->ppRewardEntry[i], fCurrentOdds, uDepth);
				}

				break;
			}
		case kRewardChoiceType_Choice:
		case kRewardChoiceType_ChoiceVariableCount:
			{
				reward_OddsAddEntry(pTable, pOddsOut, pTable->ppRewardEntry[i], fCurrentOdds, uDepth);
				break;
			}

		case kRewardChoiceType_AlgoChar:
		case kRewardChoiceType_Empty:
		case kRewardChoiceType_Disabled:
		case kRewardChoiceType_AlgoBase:
		case kRewardChoiceType_AlgoCost:
			{
				// ignore
				break;
			}


		case kRewardChoiceType_CharacterBasedInclude:
			{
				if(!pOddsOut->pPlayerEnt)
				{
					pOddsOut->bUnableToCalculate = true;
					return;
				}
				{
					RewardTable** eaPossibleTables = NULL;
					F32 EntryTotal = 0;
					int j;

					reward_GetAllPossibleCharacterBasedTables(pOddsOut->pPlayerEnt, pOddsOut->pRewardContext->pCBIData, pTable->ppRewardEntry[i]->pchCharacterBasedIncludePrefix, pTable->ppRewardEntry[i]->eCharacterBasedIncludeType, &eaPossibleTables);

					for (j = 0; j < eaSize(&eaPossibleTables); j++)
					{
						F32 fNewChance = fCurrentOdds / eaSize(&eaPossibleTables);
						reward_OddsAddTable(eaPossibleTables[j], pOddsOut, fNewChance, uDepth);
					}

					if (eaSize(&eaPossibleTables) <= 0)
					{
						// no table !
						Errorf("Reward odds, no table for CharacterBasedInclude entry in table %s %d", pTable->pchName, i);
						pOddsOut->bUnableToCalculate = true;
						return;
					}
				}
				break;
			}
		case kRewardChoiceType_ExpressionInclude:
			{
				MultiVal result = {0};
				if(!pOddsOut->pPlayerEnt)
				{
					pOddsOut->bUnableToCalculate = true;
					return;
				}
				rewardTableExpr_EvalPlayer(pOddsOut->iPartition, pOddsOut->pPlayerEnt, pTable->ppRewardEntry[i]->pRequiresExpr, pOddsOut->pRewardContext, &result);
				if(rewardTableExpr_GetIntResult(&result,pTable->pchFileName,pTable->ppRewardEntry[i]->pRequiresExpr))
				{
					// fall through
				}
				else
				{
					break;
				}
			}
		case kRewardChoiceType_Include:
			{
				F32 EntryTotal = 0;
				RewardTable *nested_table = GET_REF(pTable->ppRewardEntry[i]->hRewardTable);

				if(nested_table)
				{
				}
				else
				{
					// no table !
					Errorf("Reward odds, no table for include entry in table %s %d", pTable->pchName, i);
					pOddsOut->bUnableToCalculate = true;
					return;
				}

				reward_OddsAddEntry(pTable, pOddsOut, pTable->ppRewardEntry[i], fCurrentOdds, uDepth);
			}
			break;

		default:
			Errorf("Reward odds Bad reward choice type in reward table %s %d", pTable->pchName, i);
			pOddsOut->bUnableToCalculate = true;
			return;
			STATIC_INFUNC_ASSERT(kRewardChoiceType_Count == 16);// update this switch statement
		}			
	}

}

void reward_OddsGated(RewardTable *pTable, RewardOdds *pOddsOut, F32 fCurrentOdds)
{

}

void Reward_OddsAddItemName(RewardOdds *pOddsOut, const RewardOddsEntry *pEntry, const char *pItemName)
{
	RewardOddsItem *pRewardItem;
	F32 fQty;

	fQty = (F32)pEntry->pRewardEntry->Count;
	if(fQty < 1.0f)
	{
		fQty = 1.0f;
	}

	fQty *= pEntry->fChance;

	if(fQty < 0.00000001f)
	{
		// odds too low
		return;
	}

	pRewardItem = eaIndexedGetUsingString(&pOddsOut->eaItems, pItemName);

	if(pRewardItem)
	{
		pRewardItem->fTotalQuantity += fQty;
	}
	else
	{
		pRewardItem = StructCreate(parse_RewardOddsItem);

		pRewardItem->fTotalQuantity = fQty;
		pRewardItem->pcItemName = allocAddString(pItemName);

		eaIndexedAdd(&pOddsOut->eaItems, pRewardItem);
	}
}


void reward_GenerateOdds(RewardTable *pTable, RewardOdds *pOddsOut, F32 fCurrentOdds, U32 uDepth, Entity *pUseEnt)
{
	if(pTable && pOddsOut)
	{
		if(uDepth > MAX_RECURSE_DEPTH)
		{
			pOddsOut->bUnableToCalculate = true;
			return;
		}

		if(!pOddsOut->bInitialized)
		{
			pOddsOut->bInitialized = true;
			pOddsOut->pCurrentTable = pTable;
			pOddsOut->pStartingTable = pTable;

			// This first time trough is 100%
			fCurrentOdds = 1.0f;

			if(pUseEnt)
			{
				int level = entity_GetCombatLevel(pUseEnt);
				RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);
				ItemChangeReason reason = {0};

				pLocalContext->type = RewardContextType_PowerExec;
				pLocalContext->pKilled = NULL;
				pLocalContext->RewardLevel = level;
				pLocalContext->RewardScale = 0 ;
				SetKillerRewardContext(pUseEnt, pLocalContext);

				pOddsOut->pRewardContext = pLocalContext;
				pOddsOut->pPlayerEnt = pUseEnt;
				pOddsOut->iPartition = entGetPartitionIdx(pUseEnt);
			}
		}

		if(reward_RangeTable(pTable))
		{
			if(pOddsOut->pRewardContext)
			{
				S32 i, iNumEntries, iVal = 0;
				F32 fEntMod;
				RewardTable **eaTables = NULL;

				switch ( reward_GetRangeTableType(pTable) )
				{
				case kRewardChoiceType_LevelRange:
					iVal = pOddsOut->pRewardContext->RewardLevel;
					break;
				case kRewardChoiceType_SkillRange:
					iVal = pOddsOut->pRewardContext->SkillLevel;
					break;
				case kRewardChoiceType_EPRange:
					iVal = pOddsOut->pRewardContext->EP;
					break;
				case kRewardChoiceType_TimeRange:
					iVal = pOddsOut->pRewardContext->TimeLevel;
					break;
				}

				// Get the tables
				reward_GetRangeTables(pTable, iVal, &eaTables);

				iNumEntries = eaSize(&eaTables);

				if(iNumEntries > 0)
				{
					fEntMod = 1.0f / ((F32)iNumEntries) * pTable->NumChoices;

					// Add the range tables and generate everything
					for(i = 0; (i < iNumEntries && !pOddsOut->bUnableToCalculate); ++i)
					{
						reward_OddsAddTable(eaTables[i], pOddsOut, fCurrentOdds * fEntMod, uDepth);
					}
				}

				// clear the tables
				eaDestroy(&eaTables);
			}
			else
			{
				// no ent, can't use range table
				pOddsOut->bUnableToCalculate = true;
			}
		}
		else
		{
			// Go through table by algorithm
			switch(pTable->Algorithm)
			{
			case kRewardAlgorithm_Weighted:
				{
					reward_OddsWeighted(pTable, pOddsOut, fCurrentOdds, uDepth);
					break;
				}
			case kRewardAlgorithm_GiveAll:
				{
					reward_OddsGiveAll(pTable, pOddsOut, fCurrentOdds, uDepth);
					break;
				}
			case kRewardAlgorithm_Gated:
				{
					reward_OddsGated(pTable, pOddsOut, fCurrentOdds);
					break;
				}
			}
		}

		if(pOddsOut->bUnableToCalculate)
		{
			// Too messy, can't calculate as results won't have meaning
			reward_ContextCleanup(pOddsOut->pRewardContext);
			return;	
		}

		// Deal with other tables that we need to go through
		if(uDepth == 0)
		{
			while(eaSize(&pOddsOut->eaRewardTables) > 0 && !pOddsOut->bUnableToCalculate)
			{
				RewardOddsTable *pNextTable;
				pNextTable = eaRemove(&pOddsOut->eaRewardTables, 0);
				if(pNextTable)
				{
					reward_GenerateOdds(pNextTable->pRewardTable, pOddsOut, pNextTable->fCurrentChance, pNextTable->uDepth+1, pOddsOut->pPlayerEnt);
					StructDestroySafe(parse_RewardOddsTable, &pNextTable);
				}
			}
		}

		// create the items
		if(uDepth == 0 && !pOddsOut->bUnableToCalculate)
		{
			S32 i;
			ItemDef *pItemDef;

			// remove the context
			reward_ContextCleanup(pOddsOut->pRewardContext);

			eaIndexedEnable(&pOddsOut->eaItems, parse_RewardOddsItem);
					
			for(i = 0; i < eaSize(&pOddsOut->eaEntries); ++i)
			{
				const char *pItemName = NULL;
				F32 fCount = 1.0f;
				if(pOddsOut->eaEntries[i]->pRewardEntry->Count > 1)
				{
					fCount = (F32)pOddsOut->eaEntries[i]->pRewardEntry->Count;
				}
				switch(pOddsOut->eaEntries[i]->pRewardEntry->ChoiceType)
				{
					xcase kRewardChoiceType_Expression:
					acase kRewardChoiceType_Choice:
					acase kRewardChoiceType_ChoiceVariableCount:
						switch(pOddsOut->eaEntries[i]->pRewardEntry->Type)
						{
							case kRewardType_Numeric:
							case kRewardType_AlgoNumeric:
							case kRewardType_AlgoNumericNoScale:
							{
								pItemDef = GET_REF(pOddsOut->eaEntries[i]->pRewardEntry->hItemDef);
								if(pItemDef)
								{
									pOddsOut->fTotalNumericsGiven += pOddsOut->eaEntries[i]->fChance * fCount;
									pItemName = pItemDef->pchName;
								}
								break;
							}

							case kRewardType_Item:
							case kRewardType_ItemDifficultyScaled:
							case kRewardType_AlgoItem:
							case kRewardType_ItemWithGems:
							{
								pItemDef = GET_REF(pOddsOut->eaEntries[i]->pRewardEntry->hItemDef);
								if(pItemDef)
								{
									pOddsOut->fTotalItemsGiven += pOddsOut->eaEntries[i]->fChance * fCount;
									pItemName = pItemDef->pchName;
								}
								break;
							}

							case kRewardType_AlgoItemForce:
							case kRewardType_AlgoItemDifficultyScaled:
							case kRewardType_AlgoItemForceDifficultyScaled:
							{
								pOddsOut->fTotalItemsGiven += pOddsOut->eaEntries[i]->fChance * fCount;
								pItemName = "Algo Item";
								break;
							}
						}
						if(pItemName)
						{
							pOddsOut->eaEntries[i]->bIsItem = true;
						}
						break;
				}

				if(pItemName)
				{
					Reward_OddsAddItemName(pOddsOut, pOddsOut->eaEntries[i], pItemName);					
				}
			}
		}
	}
}

void reward_SendLootFXMessageToBagOwners(Entity* pTriggeringEnt, Entity* pLootEnt, InventoryBag* pBag)
{
	int i, j, k;

	if(!pTriggeringEnt)
	{
		// no triggering ent for need or greed
		return;
	}

	//send FX message to all loot owners 
	for (i = 0; i < eaSize(&pBag->pRewardBagInfo->peaLootOwners); i++)
	{
		for (j = 0; j < eaiSize(&pBag->pRewardBagInfo->peaLootOwners[i]->peaiOwnerIDs); j++)
		{
			if (pBag->pRewardBagInfo->peaLootOwners[i]->eOwnerType == kRewardOwnerType_Player)
				ClientCmd_SendFXMessage(entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pBag->pRewardBagInfo->peaLootOwners[i]->peaiOwnerIDs[j]), pLootEnt->myRef, "LootFX");
			else if (pBag->pRewardBagInfo->peaLootOwners[i]->eOwnerType == kRewardOwnerType_Team)
			{
				Team* pTeam = team_GetTeam(pTriggeringEnt);
				if (pTeam && (ContainerID)pBag->pRewardBagInfo->peaLootOwners[i]->peaiOwnerIDs[j] == pTeam->iContainerID)
				{
					for (k = 0; k < eaSize(&pTeam->eaMembers); k++)
					{
						Entity* pTeammate = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[k]->iEntID);
						ClientCmd_SendFXMessage(pTeammate, pLootEnt->myRef, "LootFX");
					}
				}
				else if (!pTeam)
					ClientCmd_SendFXMessage(pTriggeringEnt, pLootEnt->myRef, "LootFX");
			}
		}
	}
}

#include "AutoGen/reward_h_ast.c"
#include "AutoGen/Reward_c_ast.c"
