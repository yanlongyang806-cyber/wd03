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
#include "itemCommon_h_ast.h"
#include "ResourceManager.h"
#include "rewardCommon.h"
#include "Expression.h"
#include "GameBranch.h"
#include "itemGenCommon.h"
#include "StringCache.h"
#include "Character.h"
#include "message.h"
#include "characterclass.h"
#include "species_common.h"

#ifdef GAMESERVER
#include "Reward.h"
#endif

#include "rewardCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hRewardTableDict;
ExprContext *g_pRewardNonPlayerContext = NULL;
ExprContext *g_pRewardPlayerContext = NULL;

extern DictionaryHandle g_hCharacterClassDict;
extern DictionaryHandle g_hCharacterPathDict;
extern DictionaryHandle g_hSpeciesDict;

void rewardentry_GetAllPossibleCharBasedTableNames(RewardEntry* entry, char*** peaTableNames)
{
	RefDictIterator iter;
	char* estrTableName = NULL;
	estrStackCreate(&estrTableName);
	switch (entry->eCharacterBasedIncludeType)
	{
	case kCharacterBasedIncludeType_Class:
		{
			CharacterClass* pClass = NULL;
			RefSystem_InitRefDictIterator(g_hCharacterClassDict, &iter);
			while(pClass = (CharacterClass*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				if (pClass->bPlayerClass)
				{
					estrPrintf(&estrTableName, "%s_%s", entry->pchCharacterBasedIncludePrefix, pClass->pchName);
					eaPush(peaTableNames, strdup(estrTableName));
				}
			}
			estrDestroy(&estrTableName);
		}break;
	case kCharacterBasedIncludeType_AllClassPaths:
		{
			CharacterPath* pPath = NULL;
			RefSystem_InitRefDictIterator(g_hCharacterPathDict, &iter);
			while(pPath = (CharacterPath*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				estrPrintf(&estrTableName, "%s_%s", entry->pchCharacterBasedIncludePrefix, pPath->pchName);
				eaPush(peaTableNames, strdup(estrTableName));
			}
			estrDestroy(&estrTableName);
		}break;
	case kCharacterBasedIncludeType_Species:
		{
			SpeciesDef* pSpecies = NULL;
			RefSystem_InitRefDictIterator(g_hSpeciesDict, &iter);
			while(pSpecies = (SpeciesDef*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				if (pSpecies->eRestriction & kPCRestriction_Player)
				{
					estrPrintf(&estrTableName, "%s_%s", entry->pchCharacterBasedIncludePrefix, pSpecies->pcName);
					eaPush(peaTableNames, strdup(estrTableName));
				}
			}
			estrDestroy(&estrTableName);
		}break;
	case kCharacterBasedIncludeType_Gender:
		{
			int iGender;
			for (iGender = 1; iGender < Gender_MAX; iGender++)
			{
				estrPrintf(&estrTableName, "%s_%s", entry->pchCharacterBasedIncludePrefix, StaticDefineIntRevLookup(GenderEnum, iGender));
				eaPush(peaTableNames, strdup(estrTableName));
			}
			estrDestroy(&estrTableName);
		}break;
	}
}

bool rewardTable_Validate(RewardTable *pTable)
{
	const char *pchTempFileName;
	char *pchPath = NULL;
	int i;
	bool retcode = true;
	F32 weight = 0.0f;

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
	if (resFixPooledFilename(&pchTempFileName, pTable->pchScope && resIsInDirectory(pTable->pchScope, "maps/") ? NULL : GameBranch_GetDirectory(&pchPath,REWARDS_BASE_DIR), pTable->pchScope, pTable->pchName, REWARDS_EXTENSION))
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
		RewardPickupType eType = pTable->PickupType;
		if (eType == kRewardPickupType_FromOrigin)
			eType = reward_GetPickupTypeFromRewardOrigin(pTable, RewardContextType_EntKill);

		switch (eType)
		{
		case kRewardPickupType_None:
		case kRewardPickupType_Direct:
		case kRewardPickupType_Clickable:
		case kRewardPickupType_Choose:
			break;

		case kRewardPickupType_Interact:
		case kRewardPickupType_Rollover:
			// If we're keeping loot on corpses, we don't need costumes for that, and there's no way to know with this set of loot types whether
			// or not a costume will be needed, so we'll have to do that validation at drop time.  [RMARR 9-23-10]
			// Also if our rewardconfig specifies a type-to-costume mapping for items with both defaults filled in, then tables are allowed to specify no costume.
			if (!gConf.bKeepLootsOnCorpses && !(IS_HANDLE_ACTIVE(g_RewardConfig.TypeCostumeMappings.hNotYoursDefault) && IS_HANDLE_ACTIVE(g_RewardConfig.TypeCostumeMappings.hYoursDefault)))
			{
				if ( !IS_HANDLE_ACTIVE(pTable->hYoursCostumeRef) ||
					 !IS_HANDLE_ACTIVE(pTable->hNotYoursCostumeRef) )
				{
					ErrorFilenamef( pTable->pchFileName, "Reward table requires a valid Yours and NotYours costume");
					retcode = false;
				}
			}
			break;
		}
	}

	if((pTable->flags & kRewardFlag_AllBagsUseThisCostume)!=0 && !IS_HANDLE_ACTIVE(pTable->hNotYoursCostumeRef) && !IS_HANDLE_ACTIVE(pTable->hYoursCostumeRef))
	{
		ErrorFilenamef( pTable->pchFileName, "Reward table: When BagCostumeUseTopTable is true both hNotYoursCostumeRef and hYoursCostumeRef must have valid costumes.");
		retcode = false;
	}

	// if flagged as NoRepeats, then make sure that the algorithm and choices are set up correctly
	if((pTable->flags & kRewardFlag_NoRepeats) != 0)
	{
		if (pTable->Algorithm != kRewardAlgorithm_Weighted)
		{
			ErrorFilenamef(pTable->pchFileName, "Reward table is flagged as 'NoRepeats', but doesn't use 'Weighted' as its algorithm");
		}
		else
		{
			if (pTable->NumChoices >= eaSize(&pTable->ppRewardEntry))
			{
				ErrorFilenamef(pTable->pchFileName, "Reward table is flagged as 'NoRepeats', but is set up to choose all entries");
			}
			else if (pTable->NumChoices <= 1)
			{
				ErrorFilenamef(pTable->pchFileName, "Reward table is flagged as 'NoRepeats', but is set up to choose only one entry");
			}
		}
	}
	
	// if kRewardFlag_PickupTypeFromThisTable then make sure pickuptype is valid
	if((pTable->flags & kRewardFlag_PickupTypeFromThisTable) != 0)
	{
		switch (pTable->PickupType)
		{
			case kRewardPickupType_Direct:
			case kRewardPickupType_Interact:
			case kRewardPickupType_Rollover:
			{
				break;
			}
			default:
			{
			
				ErrorFilenamef( pTable->pchFileName, "Reward table with flag kRewardFlag_PickupTypeFromThisTable needs pickuptype Direct, Interact or Rollover.");
				retcode = false;
				break;
			}
		}
		
	}

	for(i=eaSize(&pTable->ppRewardEntry)-1; i>=0; --i)
	{
		RewardEntry *entry = pTable->ppRewardEntry[i];
		ItemDef *itemdef = NULL;

		if
		(
			pTable->Algorithm == kRewardAlgorithm_Weighted && entry->fWeight < 0.00001f &&
			(
				entry->ChoiceType == kRewardChoiceType_Choice || entry->ChoiceType == kRewardChoiceType_ChoiceVariableCount ||
				entry->ChoiceType == kRewardChoiceType_Empty || entry->ChoiceType == kRewardChoiceType_AlgoBase ||
				entry->ChoiceType == kRewardChoiceType_AlgoChar || entry->ChoiceType == kRewardChoiceType_AlgoCost
			)
		)
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table is weighted table with a zero weight entry.");
			retcode = false;
		}

		if (!GET_REF(entry->msgBroadcastChatMessage.hMessage) && REF_STRING_FROM_HANDLE(entry->msgBroadcastChatMessage.hMessage))
		{
			ErrorFilenamef(pTable->pchFileName, "Reward table references non-existent chat broadcast message '%s'", REF_STRING_FROM_HANDLE(entry->msgBroadcastChatMessage.hMessage));
			retcode = false;
		}

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

		if(entry->Type == kRewardType_ItemWithGems && !GET_REF(entry->hRewardTable))
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table kRewardType_ItemWithGems requires reward table");
			retcode = false;
		}

		if(entry->Type == kRewardType_ItemWithGems)
		{
			// check for valid item def
			ItemDef *pItemDef = GET_REF(entry->hItemDef);
			RewardTable *pGemRewardTable;
			if(!pItemDef)
			{
				ErrorFilenamef( pTable->pchFileName, "Reward table entry type  kRewardType_ItemWithGems requires valid item.");
				retcode = false;
			}
			else
			{
				if(eaSize(&pItemDef->ppItemGemSlots) < 1)
				{
					ErrorFilenamef( pTable->pchFileName, "Reward table entry type kRewardType_ItemWithGems item %s does not have gem slots.", pItemDef->pchName);
					retcode = false;
				}
			}

			pGemRewardTable = GET_REF(entry->hRewardTable);
			if(pGemRewardTable)
			{
				if(rewardTable_HasNonGems(pGemRewardTable))
				{
					ErrorFilenamef( pTable->pchFileName, "Reward table entry type kRewardType_ItemWithGems not all children are gems. First Child is %s (%s).", pGemRewardTable->pchName, pGemRewardTable->pchFileName);
					retcode = false;
				}
			}
			else
			{
				ErrorFilenamef( pTable->pchFileName, "Reward table entry type kRewardType_ItemWithGems requires a reward table.");
				retcode = false;
			}

		}

		if (GET_REF(entry->hRewardTable) == pTable)
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table \"%s\" includes itself recursively.", REF_STRING_FROM_HANDLE(entry->hRewardTable));
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

		if ((entry->ChoiceType == kRewardChoiceType_Expression || entry->ChoiceType == kRewardChoiceType_ExpressionInclude) && entry->pRequiresExpr == NULL)
		{
			ErrorFilenamef( pTable->pchFileName, "Reward table '%s' has expression choice type, but no expression",pTable->pchName);
			retcode = false;
		}

		if (IsServer() && entry->ChoiceType == kRewardChoiceType_CharacterBasedInclude)
		{
			char** eaNames = NULL;
			int j = 0;
			char* estrDefault = NULL;

			estrStackCreate(&estrDefault);
			estrPrintf(&estrDefault, "%s_Default", entry->pchCharacterBasedIncludePrefix);

			if (!RefSystem_IsReferentStringValid(g_hRewardTableDict, estrDefault))
			{
				rewardentry_GetAllPossibleCharBasedTableNames(entry, &eaNames);
				if (eaNames)
				{
					for (j = 0; j < eaSize(&eaNames); j++)
					{
						if (!RefSystem_IsReferentStringValid(g_hRewardTableDict, eaNames[j]))
							ErrorFilenamef(pTable->pchFileName, "Reward table %s has a Character-Based Include entry with a possible result table \"%s\" that doesn't exist.", pTable->pchName, eaNames[j]);
					}
					eaDestroyEx(&eaNames, NULL);
				}
			}
			estrDestroy(&estrDefault);
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

	if(pTable->Algorithm == kRewardAlgorithm_Gated && pTable->eRewardGatedType == RewardGatedType_None)
	{
		ErrorFilenamef( pTable->pchFileName, "Reward table is kRewardAlgorithm_Gated but has eRewardGatedType of type none.");
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

			case kRewardChoiceType_TimeRange:
				FoundLevelEntry = 1;
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

	estrDestroy(&pchPath);

	return retcode;
}

void rewardTable_CreateExpressions(RewardTable* pReward)
{
	int ii;
	for (ii = 0; ii < eaSize(&pReward->ppRewardEntry); ii++)
	{
		RewardEntry* theEntry = (RewardEntry*)pReward->ppRewardEntry[ii];
		exprContextSetUserPtr(g_pRewardNonPlayerContext, NULL, parse_KillCreditTeam);
		exprContextSetUserPtr(g_pRewardPlayerContext, NULL, parse_KillCreditTeam);

		if (theEntry->pRequiresExpr)
			exprGenerate(theEntry->pRequiresExpr, g_pRewardPlayerContext);
		if (theEntry->pCountExpr)
			exprGenerate(theEntry->pCountExpr, g_pRewardNonPlayerContext);
	}
}


void rewardTable_FixupRewardEntries(RewardTable* pReward)
{
	int i;
	for (i = 0; i < eaSize(&pReward->ppRewardEntry); i++)
	{
		RewardEntry* e = (RewardEntry*)pReward->ppRewardEntry[i];
		e->parent = pReward;
	}
}


int rewardTableResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, RewardTable *pReward, U32 userID)
{
	switch (eType)
	{
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
	{
		char *pchPath = NULL;
		resFixPooledFilename(&pReward->pchFileName, pReward->pchScope && resIsInDirectory(pReward->pchScope, "maps/") ? NULL : GameBranch_GetDirectory(&pchPath,REWARDS_BASE_DIR), pReward->pchScope, pReward->pchName, REWARDS_EXTENSION);
		estrDestroy(&pchPath);
		return VALIDATE_HANDLED;
	}

	xcase RESVALIDATE_CHECK_REFERENCES: // Called on all objects in dictionary after any load/reload of this dictionary
		rewardTable_Validate(pReward);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_TEXT_READING:
		rewardTable_CreateExpressions(pReward);
		rewardTable_FixupRewardEntries(pReward);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * 

				Recursive reward table validation functions

	 Previously, each of these functions was a simple recursive check.
	This was horrible, because it's perfectly legal for reward tables to
	recursively include themselves, which meant any of these functions
	would have just caused a stack overflow.
	
	 Each validation function has now been converted to a callback 
	that gets called by rewardTable_RunCallbackOnTableRecursive(),
	which handles storing previously-processed tables in a stack to
	prevent an infinite loop.

	 Most of them consist of a public wrapper and a static recursive CB,
	plus optionally a struct def so that additional parameters can be passed.

* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

typedef bool (*rewardTable_RecursiveCB)(RewardTable* pTable, RewardPickupType eInheritedPickupType, void* userData);

static bool rewardTable_RunCallbackOnTableRecursive(RewardTable* pTable, RewardPickupType eInheritedPickupType, rewardTable_RecursiveCB cbFunc, void* userData, bool bTerminateOnTrue, bool bNewRecursion)
{
	static RewardTable** eaTableStack = NULL;
	RewardPickupType ePickupType = (pTable->PickupType > kRewardPickupType_None) ? pTable->PickupType : eInheritedPickupType;
	int i;

	if(!pTable)
		return false;

	if (bNewRecursion)
		eaClear(&eaTableStack);

	//don't touch tables we've already checked
	if (eaFind(&eaTableStack, pTable) > -1)
		return false;

	eaPush(&eaTableStack, pTable);

	if (cbFunc(pTable, ePickupType, userData) && bTerminateOnTrue)
		return true;

	for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
	{
		RewardEntry *pEntry = pTable->ppRewardEntry[i];
		if ((pEntry->Type == kRewardType_RewardTable || 
			pEntry->ChoiceType == kRewardChoiceType_Include ||
			pEntry->ChoiceType == kRewardChoiceType_ExpressionInclude ||
			pEntry->ChoiceType == kRewardChoiceType_LevelRange ||
			pEntry->ChoiceType == kRewardChoiceType_TimeRange) && GET_REF(pEntry->hRewardTable))
		{
			if (rewardTable_RunCallbackOnTableRecursive(GET_REF(pEntry->hRewardTable), ePickupType, cbFunc, userData, bTerminateOnTrue, false) && bTerminateOnTrue)
				return true;
		}
	}
	eaPop(&eaTableStack);
	return false;
}

//optional params
typedef struct PickupTypeCBData
{
	RewardPickupType typeToMatch;
	bool bIgnoreSafeItems;
	RewardContextType ctxtType;
}PickupTypeCBData;
	 
//returns TRUE if the reward table has the given pickup type
static bool rewardTable_HasPickupTypeCB(RewardTable* pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	PickupTypeCBData* pData = (PickupTypeCBData*)userData;
	if (!pData)
		return false;
	else
	{
		RewardPickupType type = pTable->PickupType;
		int i;

		if (type == kRewardPickupType_FromOrigin)
			type = reward_GetPickupTypeFromRewardOrigin(pTable, pData->ctxtType);

		if (type == kRewardPickupType_None)
			type = eInheritedPickupType;

		for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
		{
			RewardEntry *pEntry = pTable->ppRewardEntry[i];
			if ((pEntry->Type == kRewardType_Item || pEntry->Type == kRewardType_AlgoItem || pEntry->Type == kRewardType_AlgoItemForce)
				&& type == pData->typeToMatch)
			{
				ItemDef *pDef = GET_REF(pEntry->hItemDef);

				// Ignore invisible items, and items that have been deemed "safe" via s_eaSafeItemTypes, unless they said not to
				if (!pData->bIgnoreSafeItems || !pDef || item_IsUnsafeGrant(pDef))
					return true;
			}
		}
	}
	return false;
}

//wrapper
static bool rewardTable_HasItemsMatchingPickupType(RewardTable *pTable, RewardPickupType typeToMatch, RewardPickupType parentType, bool bIgnoreSafeItems, RewardContextType ctxtType)
{
	PickupTypeCBData data;
	data.typeToMatch = typeToMatch;
	data.bIgnoreSafeItems = bIgnoreSafeItems;
	data.ctxtType = ctxtType;
	return rewardTable_RunCallbackOnTableRecursive(pTable, parentType, rewardTable_HasPickupTypeCB, &data, true, true);
}

// Returns TRUE if the reward table can grant an Item using the given Grant Type
bool rewardTable_HasItemsWithType(RewardTable *pTable, RewardPickupType type, RewardContextType ctxtType)
{
	if (pTable)
		return rewardTable_HasItemsMatchingPickupType(pTable, type, kRewardPickupType_None, false, ctxtType);
	return false;
}

// Returns TRUE if the reward table has an expression in it anywhere
static bool rewardTable_HasExpressionCB(RewardTable *pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	if (pTable)
	{
		int i;
		for(i=eaSize(&pTable->ppRewardEntry)-1; i>=0; --i) 
		{
			RewardEntry *pEntry = pTable->ppRewardEntry[i];
			if (pEntry->pRequiresExpr || pEntry->pCountExpr) 
			{
				return true;
			}
		}
	}
	return false;
}
bool rewardTable_HasExpression(RewardTable *pTable)
{
	return rewardTable_RunCallbackOnTableRecursive(pTable, 0, rewardTable_HasExpressionCB, NULL, true, true);
}

static bool rewardTable_HasAlgorithmCB(RewardTable *pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	if(pTable && pTable->Algorithm == *((S32 *)userData))
	{
		return true;
	}

	return false;
}

bool rewardTable_HasAlgorithm(RewardTable *pTable, RewardAlgorithm algo)
{
	S32 type = algo;
	return rewardTable_RunCallbackOnTableRecursive(pTable, 0, rewardTable_HasAlgorithmCB, &type, true, true);
}

// Returns TRUE if the reward table can grant an Item using a Direct grant
// that is unsafe when not called from within a transaction
bool rewardTable_HasUnsafeDirectGrants(RewardTable *pTable, RewardContextType ctxtType)
{
	if (!pTable)
		return false;
	if(rewardTable_HasItemsMatchingPickupType(pTable, kRewardPickupType_Direct, kRewardPickupType_None, true, ctxtType))
		return true;
	if(rewardTable_HasItemsMatchingPickupType(pTable, kRewardPickupType_Choose, kRewardPickupType_None, true, ctxtType))
		return true;
	return false;
}

//optional params
typedef struct GetAllItemsWithTypeCBData
{
	ItemType kItemType;
	ItemDef*** peaItemDefsOut;
}GetAllItemsWithTypeCBData;

static bool rewardTable_GetAllGrantedItemsWithTypeCB(RewardTable *pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	GetAllItemsWithTypeCBData* pData = (GetAllItemsWithTypeCBData*)userData;
	if (!pData)
		return false;
	else
	{
		int i;
		for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
		{
			RewardEntry *pEntry = pTable->ppRewardEntry[i];
			if ((pEntry->Type == kRewardType_Item || pEntry->Type == kRewardType_AlgoItem || pEntry->Type == kRewardType_AlgoItemForce))
			{
				ItemDef *pDef = GET_REF(pEntry->hItemDef);
				if (pDef && pDef->eType == pData->kItemType){
					eaPush(pData->peaItemDefsOut, pDef);
				}
			}
		}
	}
	return false;
}

void rewardTable_GetAllGrantedItemsWithType(RewardTable *pTable, ItemType kItemType, ItemDef*** peaItemDefsOut)
{
	GetAllItemsWithTypeCBData data;
	data.kItemType = kItemType;
	data.peaItemDefsOut = peaItemDefsOut;
	rewardTable_RunCallbackOnTableRecursive(pTable, 0, rewardTable_GetAllGrantedItemsWithTypeCB, &data, false, true);
}

typedef struct HasMissionItemsCBData
{
	bool bIgnoreMissionGrant;
	MissionDef* pMissionDef;
}HasMissionItemsCBData;

// Checks whether this reward table has any Mission Items (items that have a Mission reference on them)
// If MissionDef is specified, only mission items for the specified MissionDef will be considered.
static bool rewardTable_HasMissionItemsCB(RewardTable *pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	HasMissionItemsCBData* pData = (HasMissionItemsCBData*)userData;
	if (!pData)
		return false;
	else
	{
		RewardPickupType type = pTable->PickupType;
		int i;

		for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
		{
			RewardEntry *pEntry = pTable->ppRewardEntry[i];
			if (pEntry->Type == kRewardType_Item || pEntry->Type == kRewardType_AlgoItem || pEntry->Type == kRewardType_AlgoItemForce)
			{
				ItemDef *pDef = GET_REF(pEntry->hItemDef);
				if (pDef && IS_HANDLE_ACTIVE(pDef->hMission) && !(pData->bIgnoreMissionGrant && pDef->eType == kItemType_MissionGrant)){
					if(pData->pMissionDef)
					{
						MissionDef* pItemMission = GET_REF(pDef->hMission);
						if(pData->pMissionDef == pItemMission)
						{
							return true;
						}
					} else {
						return true;
					}
				}
			} 
		}
	}
	return false;
}

// Checks whether this reward table has any Mission Items (items that have a Mission reference on them)
// If MissionDef is specified, only mission items for the specified MissionDef will be considered.
bool rewardTable_HasMissionItems(RewardTable *pTable, bool bIgnoreMissionGrant, MissionDef* pMissionDef)
{
	HasMissionItemsCBData data;
	data.bIgnoreMissionGrant = bIgnoreMissionGrant;
	data.pMissionDef = pMissionDef;
	return rewardTable_RunCallbackOnTableRecursive(pTable, 0, rewardTable_HasMissionItemsCB, &data, true, true);
}


// Checks whether this reward table has any items flagged as unique
static bool rewardTable_HasUniqueItemsCB(RewardTable *pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	int i;

	if(!pTable)
		return false;

	for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
	{
		RewardEntry *pEntry = pTable->ppRewardEntry[i];
		if (pEntry->Type == kRewardType_Item || pEntry->Type == kRewardType_AlgoItem || pEntry->Type == kRewardType_AlgoItemForce)
		{
			ItemDef *pDef = GET_REF(pEntry->hItemDef);
			if(pDef && (pDef->flags & kItemDefFlag_Unique))
			{
				return true;
			}
		}
	}
	return false;
}

// Checks whether this reward table has any items flagged as unique
bool rewardTable_HasUniqueItems(RewardTable *pTable)
{
	return rewardTable_RunCallbackOnTableRecursive(pTable, 0, rewardTable_HasUniqueItemsCB, NULL, true, true);
}

// Checks whether this reward table has any character-based entries
static bool rewardTable_HasCharacterBasedEntryCB(RewardTable *pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	int i;

	if(!pTable)
		return false;

	for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
	{
		RewardEntry *pEntry = pTable->ppRewardEntry[i];
		if (pEntry->ChoiceType == kRewardChoiceType_CharacterBasedInclude)
		{
			return true;
		}
	}
	return false;
}

// Checks whether this reward table has any character-based entries
bool rewardTable_HasCharacterBasedEntry(RewardTable *pTable)
{
	return rewardTable_RunCallbackOnTableRecursive(pTable, 0, rewardTable_HasCharacterBasedEntryCB, NULL, true, true);
}

// Checks whether this reward table has any character-based entries
static bool rewardTable_HasNonGemsCB(RewardTable *pTable, RewardPickupType eInheritedPickupType, void* userData)
{
	int i;

	if(!pTable)
	{
		return false;
	}

	for (i = 0; i < eaSize(&pTable->ppRewardEntry); i++)
	{
		RewardEntry *pEntry = pTable->ppRewardEntry[i];
		switch(pEntry->Type)
		{
			case kRewardType_Item:
			{
				// test for gem
				ItemDef *pItemDef = GET_REF(pEntry->hItemDef);
				if(pItemDef && pItemDef->eGemType == kItemGemType_None)
				{
					// not a gem
					return true;
				}
				break;
			}
			case kRewardType_AlgoItem:
			case kRewardType_AlgoItemForce:
			case kRewardType_ItemDifficultyScaled:
			case kRewardType_ItemWithGems:
			case kRewardType_Numeric:
			case kRewardType_AlgoItemDifficultyScaled:
			case kRewardType_AlgoItemForceDifficultyScaled:
			{
				// can't have these type
				return true;
			}
		}
	}
	return false;
}

// Checks whether this reward table has any non-gem items reward entries
bool rewardTable_HasNonGems(RewardTable *pTable)
{
	return rewardTable_RunCallbackOnTableRecursive(pTable, 0, rewardTable_HasNonGemsCB, NULL, true, true);
}


/* * * * * * * * * End of recursive validation functions * * * * * * * */


bool rewardEntry_UsesCountExpression(RewardEntry * pEntry)
{
	switch (pEntry->Type)
	{
	case kRewardType_ItemDifficultyScaled:
	case kRewardType_Item:
	case kRewardType_RewardTable:
	case kRewardType_Numeric:
		if (pEntry->ChoiceType == kRewardChoiceType_ChoiceVariableCount)
		{
			return true;
		}
	}

	return false;
}

AUTO_STARTUP(RewardTables) ASTRT_DEPS(Items, PlayerDifficulty);
void rewardTables_Load(void)
{
	char *pcPath = NULL;
	char *pcBinFile = NULL;
	char pcDirList[512];

	exprContextAddStaticDefineIntAsVars(g_pRewardNonPlayerContext, ItemGenRarityEnum, NULL);
	exprContextAddStaticDefineIntAsVars(g_pRewardPlayerContext, ItemGenRarityEnum, NULL);

	sprintf(pcDirList, "%s;maps", GameBranch_GetDirectory(&pcPath, REWARDS_BASE_DIR));
	resLoadResourcesFromDisk(g_hRewardTableDict, 
		pcDirList,
		".rewards", 
		GameBranch_GetFilename(&pcBinFile, "RewardTable.bin"), 
		RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);

	estrDestroy(&pcPath);
	estrDestroy(&pcBinFile);
}

static const char* s_pchKilled;
static int s_hKilled = 0;

AUTO_RUN;
int reward_contextStartup(void)
{
	ExprFuncTable* stFuncs;

	s_pchKilled = allocAddStaticString("Killed");

	g_pRewardNonPlayerContext = exprContextCreate();
	stFuncs = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextAddFuncsToTableByTag(stFuncs, "ItemEval");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsCharacter");
	exprContextAddFuncsToTableByTag(stFuncs, "reward");
	exprContextSetFuncTable(g_pRewardNonPlayerContext, stFuncs);
	exprContextSetAllowRuntimePartition(g_pRewardNonPlayerContext);
	exprContextSetPointerVarPooledCached(g_pRewardNonPlayerContext,s_pchKilled,NULL,parse_Character,true,false,&s_hKilled);
	exprContextSetIntVar(g_pRewardNonPlayerContext, "iLevel", 0);
#ifdef GAMESERVER
	exprContextSetPointerVar(g_pRewardNonPlayerContext, "Reward", NULL, parse_RewardContext, false, true);
#endif

	g_pRewardPlayerContext = exprContextCreate();
	stFuncs = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextAddFuncsToTableByTag(stFuncs, "ItemEval");
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsCharacter");
	exprContextAddFuncsToTableByTag(stFuncs, "reward");
	exprContextAddFuncsToTableByTag(stFuncs, "entityutil");
	exprContextAddFuncsToTableByTag(stFuncs, "player");
	exprContextSetFuncTable(g_pRewardPlayerContext, stFuncs);
	exprContextSetAllowRuntimePartition(g_pRewardPlayerContext);
	exprContextSetAllowRuntimeSelfPtr(g_pRewardPlayerContext);
	exprContextSetPointerVarPooled(g_pRewardPlayerContext, "Player", NULL, parse_Entity, true, true);
	exprContextSetPointerVarPooledCached(g_pRewardPlayerContext,s_pchKilled,NULL,parse_Character,true,false,&s_hKilled);
	exprContextSetIntVar(g_pRewardPlayerContext, "iLevel", 0);
#ifdef GAMESERVER
	exprContextSetPointerVar(g_pRewardPlayerContext, "Reward", NULL, parse_RewardContext, false, true);
#endif
	return 1;
}

/* End of File */
