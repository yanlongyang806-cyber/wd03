/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "entCritter.h"
#include "team.h"
#include "Entity.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "itemCommon.h"
#include "ResourceManager.h"
#include "rewardCommon.h"
#include "Expression.h"

#include "rewardCommon_h_ast.h"

extern StaticDefineInt kSkillTypeEnum[];

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hRewardTableDict;
ExprContext *g_pRewardContext = NULL;


#define REWARDS_BASE_DIR "defs/rewards"
#define REWARDS_EXTENSION "rewards"


bool rewardTable_Validate(RewardTable *pTable)
{
	const char *pchTempFileName;
	int i;
	bool retcode = true;

	if( !resIsValidName(pTable->pchName) )
	{
		ErrorFilenamef( pTable->pchFileName, "Reward table name is illegal: '%s'", pTable->pchName );
		retcode = false;
	}

	if( !resIsValidScope(pTable->pchScope) )
	{
		ErrorFilenamef( pTable->pchFileName, "Reward table scope is illegal: '%s'", pTable->pchScope );
		retcode = false;
	}

	pchTempFileName = pTable->pchFileName;
	if (resFixPooledFilename(&pchTempFileName, REWARDS_BASE_DIR, pTable->pchScope, pTable->pchName, REWARDS_EXTENSION))
	{
		if (IsServer()) {
			ErrorFilenamef( pTable->pchFileName, "Reward filename does not match name '%s' scope '%s'", pTable->pchName, pTable->pchScope);
			retcode = false;
		}
	}

	if (IsServer() && !GET_REF(pTable->hYoursCostumeRef) && REF_STRING_FROM_HANDLE(pTable->hYoursCostumeRef))
	{
		ErrorFilenamef( pTable->pchFileName, "Reward table references non-existent costume '%s'", REF_STRING_FROM_HANDLE(pTable->hYoursCostumeRef));
		retcode = false;
	}

	if (IsServer() && !GET_REF(pTable->hNotYoursCostumeRef) && REF_STRING_FROM_HANDLE(pTable->hNotYoursCostumeRef))
	{
		ErrorFilenamef( pTable->pchFileName, "Reward table references non-existent costume '%s'", REF_STRING_FROM_HANDLE(pTable->hNotYoursCostumeRef));
		retcode = false;
	}

	if ( IsServer() )
	{
		switch (pTable->PickupType)
		{
		case kRewardPickupType_None:
		case kRewardPickupType_Direct:
		case kRewardPickupType_Clickable:
		case kRewardPickupType_Choose:
			break;

		case kRewardPickupType_Interact:
		case kRewardPickupType_Rollover:
			if ( !IS_HANDLE_ACTIVE(pTable->hYoursCostumeRef) ||
				 !IS_HANDLE_ACTIVE(pTable->hNotYoursCostumeRef) )
			{
				ErrorFilenamef( pTable->pchFileName, "Reward table requires a valid Yours and NotYours costume");
				retcode = false;
			}
			break;
		}
	}

	for(i=eaSize(&pTable->ppRewardEntry)-1; i>=0; --i)
	{
		RewardEntry *entry = pTable->ppRewardEntry[i];
		ItemDef *itemdef = NULL;

		if(!IsServer())
			continue;

		if (!GET_REF(entry->hItemDef) && REF_STRING_FROM_HANDLE(entry->hItemDef))
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table references non-existent item '%s'", REF_STRING_FROM_HANDLE(entry->hItemDef));
			retcode = false;
		}

		if (!GET_REF(entry->hRewardTable) && REF_STRING_FROM_HANDLE(entry->hRewardTable))
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table references non-existent reward table '%s'", REF_STRING_FROM_HANDLE(entry->hRewardTable));
			retcode = false;
		}

		if (!GET_REF(entry->hItemPowerDef) && REF_STRING_FROM_HANDLE(entry->hItemPowerDef))
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table references non-existent item power '%s'", REF_STRING_FROM_HANDLE(entry->hItemPowerDef));
			retcode = false;
		}

		if (!GET_REF(entry->hCostumeDef) && REF_STRING_FROM_HANDLE(entry->hCostumeDef))
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table references non-existent costume '%s'", REF_STRING_FROM_HANDLE(entry->hCostumeDef));
			retcode = false;
		}

		switch (entry->Type)
		{
		case kRewardType_Numeric:
			itemdef = GET_REF(entry->hItemDef);
			if(itemdef && itemdef->eType != kItemType_Numeric)
			{
				ErrorFilenamef( pTable->pchFileName, "numeric reward-entry has invalid item type '%s', referencing item %s",StaticDefineIntRevLookup(ItemTypeEnum,itemdef->eType), itemdef->pchName);
				retcode = false;
			}
			break;
		case kRewardType_Item:
			itemdef = GET_REF(entry->hItemDef);
			if(itemdef && itemdef->eType == kItemType_Numeric)
			{
				ErrorFilenamef( pTable->pchFileName, "item reward-entry has invalid item type '%s', referencing item %s",StaticDefineIntRevLookup(ItemTypeEnum,itemdef->eType), itemdef->pchName);
				retcode = false;
			}
			break;
		default:
			break;
		}
	}

	{
		int NumRewardEntries = eaSize(&pTable->ppRewardEntry);
		int ii;
		int FoundLevelEntry = 0;
		int FoundSkillEntry = 0;
		int FoundEPEntry = 0;
		int FoundOtherEntry = 0;

		//update all reward entries
		for(ii=0; ii<NumRewardEntries; ii++)
		{
			RewardEntry *pRewardEntry = (RewardEntry*)pTable->ppRewardEntry[ii];

			switch (pRewardEntry->ChoiceType)
			{
			case kRewardChoiceType_LevelRange:
				FoundLevelEntry = 1;
				break;

			case kRewardChoiceType_SkillRange:
				FoundSkillEntry = 1;
				break;

			case kRewardChoiceType_EPRange:
				FoundEPEntry = 1;
				break;

			default:
				FoundOtherEntry = 1;
				break;
			}
		}

		if ( (( FoundLevelEntry || FoundSkillEntry || FoundEPEntry ) && FoundOtherEntry ) ||
			 ( FoundLevelEntry + FoundSkillEntry + FoundEPEntry > 1 ) )
		{
			ErrorFilenamef( pTable->pchFileName, "The reward table '%s' cannot Mix range entries with other types.", pTable->pchName);
			retcode = false;
		}
	}

	return retcode;
}

rewardTable_CreateExpressions(RewardTable* pReward)
{
	int ii;
	for (ii = 0; ii < eaSize(&pReward->ppRewardEntry); ii++)
	{
		RewardEntry* theEntry = (RewardEntry*)pReward->ppRewardEntry[ii];
		exprContextSetUserPtr(g_pRewardContext, NULL, parse_KillCreditTeam);
		if (theEntry->pRequiresExpr)
			exprGenerate(theEntry->pRequiresExpr, g_pRewardContext);
	}
}


rewardTable_FixupRewardEntries(RewardTable* pReward)
{
	int i;
	for (i = 0; i < eaSize(&pReward->ppRewardEntry); i++)
	{
		RewardEntry* e = (RewardEntry*)pReward->ppRewardEntry[i];
		e->parent = pReward;
	}
}


void rewardTableResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, RewardTable *pReward, U32 userID)
{
	switch (eType)
	{
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
		resFixPooledFilename(&pReward->pchFileName, REWARDS_BASE_DIR, pReward->pchScope, pReward->pchName, REWARDS_EXTENSION);

	xcase RESVALIDATE_CHECK_REFERENCES: // Called on all objects in dictionary after any load/reload of this dictionary
		rewardTable_Validate(pReward);

	xcase RESVALIDATE_POST_TEXT_READING:
		rewardTable_CreateExpressions(pReward);
		rewardTable_FixupRewardEntries(pReward);
		break;
	}
}


AUTO_STARTUP(RewardsTables);
void rewardTables_Load(void)
{
	if (!gConf.bLoadRewards)
		return;

	resLoadResourcesFromDisk(g_hRewardTableDict, "defs/rewards", ".rewards", NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG | RESOURCELOAD_USERDATA);
}

// I don't think this works. algoitems don't have a reference, for example
// BF -- All I really care about is avoiding "inventory full" transaction failures.
// Maybe a better way to do this is to figure out which bag the item would go in,
// and see if that bag has a size limit.
static bool rewardTable_HasItemsMatchingPickupTypeRecursive(RewardTable *pTable, RewardPickupType typeToMatch, RewardPickupType parentType, bool bIgnoreInvisibles)
{
	RewardPickupType type = pTable->PickupType;
	int i;

	// Reward tables with type None default to the parent's type
	if (type == kRewardPickupType_None)
		type = parentType;

	for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
	{
		RewardEntry *pEntry = pTable->ppRewardEntry[i];
		if ((pEntry->Type == kRewardType_Item || pEntry->Type == kRewardType_AlgoItem || pEntry->Type == kRewardType_AlgoItemForce)
			&& type == typeToMatch)
		{
			ItemDef *pDef = GET_REF(pEntry->hItemDef);
			// Ignore invisible items
			if (!(bIgnoreInvisibles && pDef && (pDef->eType == kItemType_Callout || pDef->eType == kItemType_Lore || pDef->eType == kItemType_Title)))
				return true;
		}
		else if (pEntry->Type == kRewardType_RewardTable && GET_REF(pEntry->hRewardTable))
		{
			if (rewardTable_HasItemsMatchingPickupTypeRecursive(GET_REF(pEntry->hRewardTable), typeToMatch, type, bIgnoreInvisibles))
				return true;
		}
	}
	return false;
}

// Returns TRUE if the reward table can grant an Item using the given Grant Type
bool rewardTable_HasItemsWithType(RewardTable *pTable, RewardPickupType type)
{
	if (pTable)
		return rewardTable_HasItemsMatchingPickupTypeRecursive(pTable, type, kRewardPickupType_None, false);
	return false;
}

// Returns TRUE if the reward table can grant an Item using a Direct grant
// that is unsafe when not called from within a transaction
bool rewardTable_HasUnsafeDirectGrants(RewardTable *pTable)
{
	if (!pTable)
		return false;
	if(rewardTable_HasItemsMatchingPickupTypeRecursive(pTable, kRewardPickupType_Direct, kRewardPickupType_None, true))
		return true;
	return false;
}

void rewardTable_GetAllGrantedItemsWithType(RewardTable *pTable, ItemType kItemType, ItemDef*** peaItemDefsOut)
{
	if (pTable){
		int i;
		for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
		{
			RewardEntry *pEntry = pTable->ppRewardEntry[i];
			if ((pEntry->Type == kRewardType_Item || pEntry->Type == kRewardType_AlgoItem || pEntry->Type == kRewardType_AlgoItemForce))
			{
				ItemDef *pDef = GET_REF(pEntry->hItemDef);
				if (pDef && pDef->eType == kItemType){
					eaPush(peaItemDefsOut, pDef);
				}
			}
			else if (pEntry->Type == kRewardType_RewardTable && GET_REF(pEntry->hRewardTable))
			{
				rewardTable_GetAllGrantedItemsWithType(GET_REF(pEntry->hRewardTable), kItemType, peaItemDefsOut);
			}
		}
	}
}

AUTO_RUN;
int reward_contextStartup(void)
{
	StashTable stFuncs;

	g_pRewardContext = exprContextCreate();
	stFuncs = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextAddFuncsToTableByTag(stFuncs, "ItemEval");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsCharacter");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsSelf");
	exprContextAddFuncsToTableByTag(stFuncs, "player");
	exprContextAddFuncsToTableByTag(stFuncs, "reward");
	exprContextSetFuncTable(g_pRewardContext, stFuncs);
	exprContextSetSelfPtr(g_pRewardContext, NULL);

	return 1;
}

/* End of File */
