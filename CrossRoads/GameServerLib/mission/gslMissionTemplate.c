/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "Expression.h"
#include "gslMission.h"
#include "gslMissionTemplate.h"
#include "mission_common.h"
#include "rand.h"
#include "stringcache.h"

#include "mission_common_h_ast.h"


// ----------------------------------------------------------------------------------
// Mission Template Logic
// ----------------------------------------------------------------------------------

bool missiontemplate_MissionIsMadLibs(const MissionDef *pDef)
{
	if (pDef && pDef->missionTemplate && pDef->missionTemplate->rootVarGroup) {
		if (missiontemplate_VarGroupIsMadLibs(pDef->missionTemplate->rootVarGroup)) {
			return true;
		}
	}
	return false;
}


TemplateVariableSubList *missiontemplate_FindMatchingSubList(MissionVarTable *pValueList, TemplateVariable *pDependencyVal)
{
	int i, n = eaSize(&pValueList->subLists);
	for (i = 0; i < n; ++i) {
		TemplateVariableSubList *pSubList = pValueList->subLists[i];
		TemplateVariable *pSubGroupValue = pValueList->subLists[i]->parentValue;
		if (pSubGroupValue && pDependencyVal) {
			switch(pValueList->dependencyType)
			{
				xcase TemplateVariableType_Int:
				{
					int iValue = MultiValGetInt(&pDependencyVal->varValue, NULL);
					if (iValue >= pSubList->iParentValueMin && iValue <= pSubList->iParentValueMax) {
						return pValueList->subLists[i];
					}
				}

				xcase TemplateVariableType_Message:
				case TemplateVariableType_String:
				case TemplateVariableType_LongString:
				case TemplateVariableType_CritterDef:
				case TemplateVariableType_CritterGroup:
				case TemplateVariableType_StaticEncounter:
				case TemplateVariableType_Mission:
				case TemplateVariableType_Item:
				case TemplateVariableType_Volume:
				case TemplateVariableType_Map:
				case TemplateVariableType_Neighborhood:
				{
					const char *pcSubgroupStr = MultiValGetString(&pSubGroupValue->varValue, NULL);
					const char *pcDependencyStr = MultiValGetString(&pDependencyVal->varValue, NULL);
					if (pcSubgroupStr == pcDependencyStr || (pcSubgroupStr && pcDependencyStr && stricmp(pcSubgroupStr, pcDependencyStr) == 0)) {
						return pValueList->subLists[i];
					}
				}
				xdefault:
					Errorf("Unknown variable type in mission template UI.  Talk to a mission programmer.");
			}
		}
	}
	return NULL;
}


static TemplateVariable *missiontemplate_LookupTemplateVarInList(const char *pcVarName, TemplateVariable ***peaVarList)
{
	const char *pcVarNamePooled = allocFindString(pcVarName);
	int i, n = eaSize(peaVarList);

	if (!pcVarNamePooled) {
		// The variable name should have been put in the string pool when the template type was registered
		Errorf("Template callback function uses unregistered template variable %s", pcVarName);
		return NULL;
	}

	for(i=0; i<n; i++) {
		TemplateVariable *pVar = (*peaVarList)[i];
		if (pcVarNamePooled && pVar->varName && pcVarNamePooled == pVar->varName) {
			return pVar;
		}
	}
	return NULL;
}


TemplateVariable *missiontemplate_GetRandomValueFromSubList(TemplateVariableSubList *pSubList, int iLevel)
{
	F32 fTotalWeight = 0.0f;
	F32 fRandomRoll = 0.0f;
	F32 fCurrWeight = 0.0f;
	int i, n = eaSize(&pSubList->childValues);

	// Generate weight total
	for (i = 0; i < n; i++) {
		TemplateVariableOption *pOption = pSubList->childValues[i];
		if ((!pOption->minLevel || iLevel >= pOption->minLevel) && (!pOption->maxLevel || iLevel <= pOption->maxLevel)) {
			fTotalWeight += pOption->weight;
		}
	}

	fRandomRoll = randomMersennePositiveF32(NULL)*fTotalWeight;

	// Generate weight total
	for (i = 0; i < n; i++) {
		TemplateVariableOption *pOption = pSubList->childValues[i];
		if ((!pOption->minLevel || iLevel >= pOption->minLevel) && (!pOption->maxLevel || iLevel <= pOption->maxLevel)) {
			fCurrWeight += pOption->weight;
		}
		if (fCurrWeight >= fRandomRoll) {
			return &pOption->value;
		}
	}
	return NULL;
}


// This randomizes a particular variable in a template.
// Returns TRUE if the variable was successfully given a value.
static bool missiontemplate_RandomizeVariable(TemplateVariable *pVariable, TemplateVariable *pBaseVariable, TemplateVariable ***peaFinishedVariables, int iLevel)
{
	static StashTable s_missionTemplateFuncTable = NULL;

	ExprContext *pContext;
	TemplateVariable *pDependencyVal = NULL;
	const char *pcDependentVarName = pVariable->varDependency;
	bool bSuccess = false;
	MultiVal mvAnswer;

	if (!pVariable->varValueExpression) {
		return true;
	}

	pContext = exprContextCreate();
	exprContextSetFuncTable(pContext, missiontemplate_CreateTemplateVarExprFuncTable());

	// If this variable has a dependency, look up the value of the dependency in the list of
	// completed variables.  If it exists, find and use the sub-list for that value.
	// Otherwise, return false and this variable will be evaluated again later.
	if (pcDependentVarName) {
		pDependencyVal = missiontemplate_LookupTemplateVarInList(pcDependentVarName, peaFinishedVariables);
		if (!pDependencyVal) {
			return false; // Couldn't find dependency; fail
		}
		// Put val into expression context
		exprContextSetPointerVar(pContext, "dependencyVal", pDependencyVal, parse_TemplateVariable, 0, 0);
	}

	exprContextSetIntVar(pContext, "iLevel", iLevel);
	exprEvaluate(pVariable->varValueExpression, pContext, &mvAnswer);
	MultiValCopy(&pVariable->varValue, &mvAnswer);
	if (pVariable->varValue.type == MULTI_STRING && !(pVariable->varValue.str)) {
		bSuccess = false;
	} else {
		bSuccess = true;
	}

	exprContextDestroy(pContext);
	
	return bSuccess;
}


bool missiontemplate_RandomizeTemplateValues(MissionTemplateType *pTemplateType, TemplateVariableGroup *pGroup, TemplateVariable ***peaFinishedVariables, int iLevel)
{
	TemplateVariable **eaVariables = NULL;
	int iLastNumIncompleteVariables;
	int i, n;

	if (pTemplateType) {
		eaPushEArray(&eaVariables, &pGroup->variables);
		iLastNumIncompleteVariables = eaSize(&eaVariables);

		while(eaSize(&eaVariables)) {
			n = eaSize(&eaVariables);
			for (i = n-1; i >= 0; --i) {
				TemplateVariable *pBaseVar = missiontemplate_LookupTemplateVarInVarGroup(&pTemplateType->rootVarGroup, eaVariables[i]->varName, true);
				if (missiontemplate_RandomizeVariable(eaVariables[i], pBaseVar, peaFinishedVariables, iLevel)) {
					// Debugging printf
					if (eaVariables[i]->varValue.type == MULTI_STRING) {
						printf("Variable %s = %s\n", eaVariables[i]->varName, MultiValGetString(&eaVariables[i]->varValue, NULL));
					} else if (eaVariables[i]->varValue.type == MULTI_INT) {
						printf("Variable %s = %"FORM_LL"d\n", eaVariables[i]->varName, MultiValGetInt(&eaVariables[i]->varValue, NULL));
					}

					eaPush(peaFinishedVariables, eaVariables[i]);
					eaRemove(&eaVariables, i);
				}
			}
			if (eaSize(&eaVariables) == iLastNumIncompleteVariables) {
				Errorf("Error: Could not resolve dependencies between Mission Template variables!");
				return false;
			} else {
				iLastNumIncompleteVariables = eaSize(&eaVariables);
			}
		}

		n = eaSize(&pGroup->subGroups);
		for (i = 0; i < n; i++)	{
			if (!missiontemplate_RandomizeTemplateValues(pTemplateType, pGroup->subGroups[i], peaFinishedVariables, iLevel)) {
				return false;
			}
		}
	}

	return true;
}


