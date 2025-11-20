/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "Expression.h"
#include "gslMission.h"
#include "gslMissionTemplate.h"
#include "inventoryCommon.h"
#include "itemcommon.h"
#include "mission_common.h"
#include "reward.h"
#include "stringcache.h"


// ----------------------------------------------------------------------------------
// Mission Template Expresions
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal missiontemplate_FuncMissionStringVarFromTableStaticCheck(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppcReturnVal, const char *pcVarTableName, ACMD_EXPR_ERRSTRING errEstr)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	TemplateVariableGroup *pRootVarGroup = SAFE_MEMBER2(pMissionDef, missionTemplate, rootVarGroup);
	MissionVarTable *pValueList = NULL;
	MultiVal *pmvDependencyVarNameMultiVal = exprContextGetSimpleVar(pContext, "varDependency");
	MultiVal *pmvVarNameMultiVal = exprContextGetSimpleVar(pContext, "varName");
	const char *pcDependencyVarName = pmvDependencyVarNameMultiVal ? MultiValGetString(pmvDependencyVarNameMultiVal, NULL):NULL;
	const char *pcVarName = pmvVarNameMultiVal ? MultiValGetString(pmvVarNameMultiVal, NULL):NULL;

	if (!pcVarTableName) {
		estrPrintf(errEstr, "Error: No var table name");
		return ExprFuncReturnError;
	} else {
		pValueList = RefSystem_ReferentFromString(g_MissionVarTableDict, pcVarTableName);
	}

	if (!pValueList) {
		estrPrintf(errEstr, "Error: Could not find var table %s", pcVarTableName);
		return ExprFuncReturnError;
	}

	if (pRootVarGroup) {
		TemplateVariable *pVar = pcVarName ? missiontemplate_LookupTemplateVarInVarGroup(pRootVarGroup, pcVarName, true) : NULL;
		TemplateVariable *pDependencyVar = pcDependencyVarName ? missiontemplate_LookupTemplateVarInVarGroup(pRootVarGroup, pcDependencyVarName, true) : NULL;
		if (pVar && pVar->varType != pValueList->varType) {
			estrPrintf(errEstr, "Error: Return type of VarTable %s does not match type of '%s'", pcVarTableName, pcVarName);
			return ExprFuncReturnError;
		}
		if (pDependencyVar && pDependencyVar->varType != pValueList->dependencyType) {
			estrPrintf(errEstr, "Error: Dependency type of VarTable %s does not match type of '%s'", pcVarTableName, pcDependencyVarName);
			return ExprFuncReturnError;
		}
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(mission_template) ACMD_NAME(MissionStringVarFromTable) ACMD_EXPR_STATIC_CHECK(missiontemplate_FuncMissionStringVarFromTableStaticCheck);
ExprFuncReturnVal missiontemplate_FuncMissionStringVarFromTable(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppcReturnVal, const char *pcVarTableName, ACMD_EXPR_ERRSTRING errEstr)
{
	RewardTable *pRewardTable = NULL;
	MissionVarTable *pValueList = NULL;
	TemplateVariable *pDependencyVal = exprContextGetVarPointerUnsafe(pContext, "dependencyVal");
	TemplateVariable *pTableVal = NULL;
	TemplateVariableSubList *pSubList = NULL;
	int iLevel = MultiValGetInt(exprContextGetSimpleVar(pContext, "iLevel"), NULL);

	if (!pcVarTableName) {
		return ExprFuncReturnError;
	}

	pValueList = RefSystem_ReferentFromString(g_MissionVarTableDict, pcVarTableName);
	if (!pValueList) {
		return ExprFuncReturnError;
	}

	// If this variable has a dependency, find and use the sub-list for that value.
	// Otherwise, use the first list.
	if (pDependencyVal) {
		pSubList = missiontemplate_FindMatchingSubList(pValueList, pDependencyVal);
		if (!pSubList) {
			if (pDependencyVal && pDependencyVal->varValue.type == MULTI_STRING) {
				Errorf("Error: Could not find matching parameter in %s for value %s", pcVarTableName, MultiValGetString(&pDependencyVal->varValue, NULL));
			} else {
				Errorf("Error: Could not find matching parameter in %s", pcVarTableName);
			}
			return ExprFuncReturnError;
		}
	} else {
		// Todo - Maybe this should append all the lists?
		if (eaSize(&pValueList->subLists) == 1) {
			pSubList = pValueList->subLists[0];
		} else if (eaSize(&pValueList->subLists) == 0) {
			Errorf("Error: Empty value list in MissionVarTable %s", pcVarTableName);
			return ExprFuncReturnError;
		} else {
			Errorf("Error: Variable with no dependency cannot use MissionVarTable with multiple sub-lists (%s)", pcVarTableName);
			return ExprFuncReturnError;
		}
	}

	if (GET_REF(pSubList->hRewardTable)) {
		pRewardTable = GET_REF(pSubList->hRewardTable);
	} else if (eaSize(&pSubList->childValues)) {
		pTableVal = missiontemplate_GetRandomValueFromSubList(pSubList, iLevel);
	}

	if (pRewardTable) {
		InventoryBag **ppBags = NULL;
		reward_GenerateMissionVarsBag(iPartitionIdx, NULL, pRewardTable, &ppBags, iLevel);
		if (eaSize(&ppBags)) {
			// TODO - Use all these bags somehow?
			InventoryBag *pBag = ppBags[0];
			int iItem, iNumItems = eaSize(&pBag->ppIndexedInventorySlots);

			if (eaSize(&ppBags) > 1) {
				ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: Reward Table %s returned %d bags!  Multiple bags not yet supported.", pRewardTable->pchName, eaSize(&ppBags));
			}

			for (iItem = iNumItems-1; iItem >= 0; --iItem) {
				if (pBag->ppIndexedInventorySlots[iItem]->pItem) {
					ItemDef *pItemDef = NULL;
					Item *pItem = pBag->ppIndexedInventorySlots[iItem]->pItem;
					if (pItem) {
						pItemDef = GET_REF(pItem->hItem);
					}
					if (pItemDef) {
						(*ppcReturnVal) =  pItemDef->pchName;
						break;
					}
				}
			}
		}
		eaDestroyStruct(&ppBags, parse_InventoryBag);

	} else if (pTableVal) {
		if (pTableVal->varType == TemplateVariableType_Int) {
			Errorf("Error: Trying to call MissionStringVarFromTable on non-string variable!");
		} else {
			(*ppcReturnVal) = MultiValGetString(&pTableVal->varValue, NULL);
		}
	}
	return ExprFuncReturnFinished;
}


