#include "TicketAssignment.h"
#include "TicketAssignment_h_ast.h"
#include "TicketTracker.h"
#include "TicketEntry.h"
#include "TicketEntry_h_ast.h"
#include "Authentication.h"
#include "Authentication_h_ast.h"
#include "estring.h"
#include "earray.h"
#include "qsortG.h"
#include "textparser.h"
#include "utils.h"
#include "Category.h"
#include "GlobalTypes.h"
#include "Search.h"
#include "mathutil.h"
#include "file.h"

extern char gTicketTrackerAltDataDir[MAX_PATH];
AssignmentRulesList gGlobalRules = {0};
AssignmentRulesList gGroupRules = {0};

#define GET_RULES_LIST(eScope) (eScope == RULES_GLOBAL ? &gGlobalRules : &gGroupRules)

U32 getNextID(void)
{
	gGlobalRules.uNextID++;

	while (findAssignmentRuleByID(gGlobalRules.uNextID))
	{
		gGlobalRules.uNextID++;
	}
	return gGlobalRules.uNextID;
}

// always append, return index == count
int createNewAssignmentRule (RuleScope eScope, const char *pName, const char *pMainCategoryFilter, const char *pSubCategoryFilter, 
							 const char *pProductFilter, const char *pKeywordFilter, const char *pActionString)
{
	return editAssignmentRule (0, pName, pMainCategoryFilter, pSubCategoryFilter, pProductFilter, pKeywordFilter, pActionString);
}

int editAssignmentRule (U32 uID, const char *pName, const char *pMainCategoryFilter, const char *pSubCategoryFilter, const char *pProductFilter, const char *pKeywordFilter, const char *pActionString)
{
	AssignmentRule *pRule;
	AssignmentAction **ppActions = NULL;
	char *pKeyword = NULL;

	if  (!(pProductFilter && pActionString))
	{
		return -1;
	}
	pRule = findAssignmentRuleByID(uID);
	if (uID && !pRule)
		return -1;

	ppActions = readActionsFromString(pActionString);

	if ((pMainCategoryFilter || pSubCategoryFilter || pKeywordFilter || (pProductFilter && pProductFilter[0])) && ppActions)
	{
		//AssignmentRulesList *pRulesList = GET_RULES_LIST(eScope);
		if (!pRule)
		{
			pRule = StructCreate(parse_AssignmentRule);
			pRule->uID = getNextID();
			eaPush(&gGlobalRules.ppRules, pRule);
		}
		else
		{
			estrDestroy(&pRule->pMainCategoryFilter);
			estrDestroy(&pRule->pCategoryFilter);
			estrDestroy(&pRule->pProductName);
			eaDestroy(&pRule->ppActions);
		}

		estrCopy2(&pRule->pName, pName);
		if (pMainCategoryFilter)
			estrCopy2(&pRule->pMainCategoryFilter, pMainCategoryFilter);
		if (pSubCategoryFilter)
			estrCopy2(&pRule->pCategoryFilter, pSubCategoryFilter);
		
		if (pProductFilter)
			estrCopy2(&pRule->pProductName, pProductFilter);
		if (pKeywordFilter && pKeywordFilter[0])
		{
			estrCopy2(&pRule->pKeywordFilter, pKeywordFilter);
		}
		pRule->ppActions = ppActions;

		saveAssignmentRules();
		return pRule->uID;
	}

	eaDestroyStruct(&ppActions, parse_AssignmentAction);
	return -1;
}

int deleteAssignmentRule(U32 uID)
{
	int i;
	for (i=0; i<eaSize(&gGlobalRules.ppRules); i++)
	{
		if (gGlobalRules.ppRules[i]->uID == uID)
		{
			AssignmentRule *pRule = eaRemove(&gGlobalRules.ppRules, i);
			if (pRule)
				StructDestroy(parse_AssignmentRule, pRule);
			saveAssignmentRules();
			return 0;
		}
	}
	return -1;
}

bool checkRuleMatch(AssignmentRule *pRule, TicketEntry *pEntry)
{
	if (pRule->pProductName && pRule->pProductName[0])
	{
		if (!pEntry->pProductName || strstri(pEntry->pProductName, pRule->pProductName) == NULL)
			return false;
	}
	if (pRule->pMainCategoryFilter)
	{
		if (stricmp(pRule->pMainCategoryFilter, pEntry->pMainCategory) != 0)
			return false;
	}
	if (pRule->pCategoryFilter)
	{
		if (stricmp(pRule->pCategoryFilter, pEntry->pCategory) != 0)
			return false;
	}
	if (pRule->pKeywordFilter)
	{
		if (!strstri(pEntry->pSummary, pRule->pKeywordFilter) && !strstri(pEntry->pUserDescription, pRule->pKeywordFilter))
		{
			return false;
		}
	}
	// past all filters
	return true;
}

bool applyAssignmentRules (RuleScope eScope, TicketEntry *pEntry)
{
	AssignmentRulesList *pRulesList = GET_RULES_LIST(eScope);
	int i;

	if (!pRulesList->ppRules || eaSize(&pRulesList->ppRules) == 0)
		return false;

	for (i=0; i<eaSize(&pRulesList->ppRules); i++)
	{
		AssignmentRule *pRule = pRulesList->ppRules[i];

		if (checkRuleMatch (pRule, pEntry))
		{
			int j;
			for (j=0; j<eaSize(&pRule->ppActions); j++)
			{
				AssignmentAction *pAction = pRule->ppActions[j];
				switch (pAction->eAction)
				{
				case ASSIGN_GROUP:
					{
						TicketUserGroup *pGroup = findTicketGroupByID(pEntry->uGroupID);
						if (pGroup)
						{
							pEntry->uGroupID = pAction->uActionTarget;
						}
					}
				xcase ASSIGN_PRIORITY:
					pEntry->uPriority = pAction->uActionTarget;
					break;
				default:
					break;
				}
			}
		}
	}

	return true;
}

AssignmentRulesList * getGlobalRules(void)
{
	return &gGlobalRules;
}
AssignmentRulesList * getGroupRules(void)
{
	return &gGroupRules;
}
AssignmentRulesList * getAssignmentRules(RuleScope eScope)
{
	return GET_RULES_LIST(eScope);
}

AssignmentRule *findAssignmentRuleByID(U32 uID)
{
	int i;
	if (!uID)
		return NULL;
	for (i=0; i<eaSize(&gGlobalRules.ppRules); i++)
	{
		if (gGlobalRules.ppRules[i]->uID == uID)
			return gGlobalRules.ppRules[i];
	}
	return NULL;
}

U32 getActionTarget(AssignmentRule *pRule, ActionType eType)
{
	int i;
	for (i=0; i<eaSize(&pRule->ppActions); i++)
	{
		if (pRule->ppActions[i]->eAction == eType)
			return pRule->ppActions[i]->uActionTarget;
	}
	return 0;
}

void writeActionsToString(char **estr, AssignmentAction **ppActions)
{
	if (ppActions)
	{
		int i;
		for (i = 0; i < eaSize(&ppActions); i++)
		{
			AssignmentAction *pAction = ppActions[i];
			switch (pAction->eAction)
			{
			case ASSIGN_GROUP:
				{
					TicketUserGroup *pGroup = findTicketGroupByID(pAction->uActionTarget);
					if (pGroup)
						estrConcatf(estr, "%sGroup:%s", i > 0 ? ";" : "", pGroup->pName);
					else
						estrConcatf(estr, "%sGroup:%d", i > 0 ? ";" : "", pAction->uActionTarget);
				}
				xcase ASSIGN_PRIORITY:
				{
					estrConcatf(estr, "%sPriority:%d", i > 0 ? ";" : "", pAction->uActionTarget);
				}
				break;
			default:
				break;
			}
		}
	}
}

AssignmentAction ** readActionsFromString(const char *pActionString)
{
	size_t uFilterSize;
	char * pTokenString;
	char * pToken = NULL;
	char * pTokContext = NULL;
	AssignmentAction ** ppActions = NULL;

	uFilterSize = strlen(pActionString) + 1;
	pTokenString = malloc (uFilterSize);
	memcpy (pTokenString, pActionString, uFilterSize);
	pToken = strtok_s(pTokenString, ";", &pTokContext);
	while (pToken)
	{
		AssignmentAction *pAction = NULL;
		char *pActionTemp = NULL;
		char *pTarget = NULL;
		bool bParseSuccess = false;

		estrCopy2(&pActionTemp, pToken);
		estrTrimLeadingAndTrailingWhitespace(&pActionTemp);

		pTarget = strstr(pActionTemp, ":");
		if (pTarget) 
		{
			*pTarget++ = '\0';
			pAction = StructCreate(parse_AssignmentAction);
			if (stricmp(pActionTemp, "group") == 0)
			{
				TicketUserGroup *pGroup;
				pAction->eAction = ASSIGN_GROUP;
				pGroup = findTicketGroupByName(pTarget);
				if (pGroup)
				{
					pAction->uActionTarget = pGroup->uID;
					bParseSuccess = true;
				}
			}
			else if (stricmp(pActionTemp, "priority") == 0)
			{
				pAction->eAction = ASSIGN_PRIORITY;
				pAction->uActionTarget = atoi(pTarget);
				bParseSuccess = pAction->uActionTarget;
			}
		}

		if (bParseSuccess)
			eaPush(&ppActions, pAction);
		else
			// TODO - should this just fail the entire operation?
			StructDestroy(parse_AssignmentAction, pAction);

		estrDestroy(&pActionTemp);
		pToken = strtok_s(NULL, ";", &pTokContext);
	}
	free(pTokenString);
	return ppActions;
}


bool loadAssignmentRules(void)
{
	return (0 != ParserReadTextFile(STACK_SPRINTF("%srules.txt", gTicketTrackerAltDataDir), 
		parse_AssignmentRulesList, &gGlobalRules, 0));
}
bool saveAssignmentRules(void)
{
	return (0 != ParserWriteTextFile(STACK_SPRINTF("%s\\%srules.txt", fileLocalDataDir(), gTicketTrackerAltDataDir), 
		parse_AssignmentRulesList, &gGlobalRules, 0, 0));
}

#include "TicketAssignment_h_ast.c"