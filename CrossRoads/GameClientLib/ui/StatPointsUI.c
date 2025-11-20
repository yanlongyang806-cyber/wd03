#include "UIGen.h"
#include "Entity.h"
#include "Character.h"
#include "CharacterCreationUI.h"
#include "GameAccountDataCommon.h"
#include "LoginCommon.h"
#include "Login2Common.h"

#include "StatPointsUI_c_ast.h"
#include "StatPoints_h_ast.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

extern Login2CharacterCreationData *g_CharacterCreationData;

AUTO_STRUCT;
typedef struct StatPointUIElement
{
	AttribType eAttribType;
	const char *pchAttribName;	AST(UNOWNED)
	const char *pchDisplayName;	AST(UNOWNED)
	const char *pchDescription;	AST(UNOWNED)
	F32 fCurrentValue;
} StatPointUIElement;

// The cart containing the stat points. The items in the cart will be sent to server to finalize the transaction.
static StatPointCart *s_pStatPointCart = NULL;

static S32 StatPointPoolUI_CartFindAssignedStat(S32 eAttribType)
{
	if (eAttribType >= 0 && eaSize(&s_pStatPointCart->eaItems) > 0)
	{
		S32 i;
		for (i = 0; i < eaSize(&s_pStatPointCart->eaItems); i++)
		{
			if (s_pStatPointCart->eaItems[i]->eType == eAttribType)
			{
				return i;
			}
		}
	}
	return -1;
}

static void StatPointPoolUI_CartInit(void)
{
	s_pStatPointCart = StructCreate(parse_StatPointCart);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_CartGetPointsSpentOnPool);
S32 StatPointPoolUI_CartGetPointsSpentOnPool(SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	S32 iPointsTotal = 0;

	if (s_pStatPointCart == NULL)
	{
		StatPointPoolUI_CartInit();
	}

	if (s_pStatPointCart)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(s_pStatPointCart->eaItems, StatPointCartItem, pItem)
		{
			iPointsTotal += pItem->iPoints;
		}
		FOR_EACH_END
	}

	return iPointsTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_CartGetPointsSpentOnAttrib);
S32 StatPointPoolUI_CartGetPointsSpentOnAttrib(S32 eAttribType)
{
	S32 iExistingStatIndex;

	if (s_pStatPointCart == NULL)
	{
		StatPointPoolUI_CartInit();
	}
	if (s_pStatPointCart)
	{
		iExistingStatIndex = StatPointPoolUI_CartFindAssignedStat(eAttribType);

		if (iExistingStatIndex < 0)
		{
			return 0;
		}
		else
		{
			return s_pStatPointCart->eaItems[iExistingStatIndex]->iPoints;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_CartClear);
void StatPointPoolUI_CartClear(void)
{
	if (s_pStatPointCart == NULL)
	{
		StatPointPoolUI_CartInit();
	}

	if (s_pStatPointCart)
	{
		// Clear all items in the cart
		eaClearStruct(&s_pStatPointCart->eaItems, parse_StatPointCartItem);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_CartModify);
void StatPointPoolUI_CartModify(S32 eAttribType, S32 iPoints)
{
	devassert(iPoints != 0);

	if (s_pStatPointCart == NULL)
	{
		StatPointPoolUI_CartInit();
	}

	if (iPoints != 0 && s_pStatPointCart)
	{
		S32 iExistingStatIndex = StatPointPoolUI_CartFindAssignedStat(eAttribType);
		if (iExistingStatIndex == -1)
		{
			StatPointCartItem *pNewStat = StructCreate(parse_StatPointCartItem);
			pNewStat->eType = eAttribType;
			pNewStat->iPoints = iPoints;
			eaPush(&s_pStatPointCart->eaItems, pNewStat);
		}
		else
		{
			StatPointCartItem *pExistingStat = s_pStatPointCart->eaItems[iExistingStatIndex];
			if (pExistingStat->iPoints + iPoints == 0)
			{
				eaRemove(&s_pStatPointCart->eaItems, iExistingStatIndex);
				StructDestroy(parse_StatPointCartItem, pExistingStat);
			}
			else
			{
				pExistingStat->iPoints += iPoints;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_CartCommit);
void StatPointPoolUI_CartCommit(void)
{
	if (s_pStatPointCart && eaSize(&s_pStatPointCart->eaItems) > 0)
	{
		ServerCmd_character_ModifyStatPointsInCart(s_pStatPointCart);
	}
}

static F32 StatPointPoolUI_DDGetBaseAttribValue(SA_PARAM_NN_STR const char *pchStatPointPoolName, AttribType eAttribType)
{
	if (stricmp(pchStatPointPoolName, "Skills") == 0)
	{
		const char *pchAttribName = StaticDefineIntRevLookup(AttribTypeEnum, eAttribType);
		const char *pchAbilityScore = NULL;

		if (pchAttribName)
		{
			if (stricmp(pchAttribName, "SkillArcana") == 0 || stricmp(pchAttribName, "SkillReligion") == 0)
			{
				pchAbilityScore = "INT";
			}
			else if (stricmp(pchAttribName, "SkillBluff") == 0 || stricmp(pchAttribName, "SkillDiplomacy") == 0 || stricmp(pchAttribName, "SkillIntimidate") == 0)
			{
				pchAbilityScore = "CHA";
			}
			else if (stricmp(pchAttribName, "SkillHeal") == 0 || stricmp(pchAttribName, "SkillNature") == 0 || stricmp(pchAttribName, "SkillPerception") == 0)
			{
				pchAbilityScore = "WIS";
			}
			else if (stricmp(pchAttribName, "SkillStealth") == 0 || stricmp(pchAttribName, "SkillThievery") == 0)
			{
				pchAbilityScore = "DEX";
			}
		}

		if (pchAbilityScore == NULL)
		{
			return 0.f;
		}

		return CharacterCreation_DD_GetAbilityMod(pchAbilityScore);
	}
	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_CharCreation_GetBaseAttribValue);
F32 StatPointPoolUI_CharCreation_GetBaseAttribValue(SA_PARAM_NN_STR const char *pchStatPointPoolName, S32 eAttribType)
{
	if (gConf.bUseDDBaseStatPointsFunction)
	{
		// Return D&D base values
		return StatPointPoolUI_DDGetBaseAttribValue(pchStatPointPoolName, eAttribType);
	}
	return 0.f;
}

static NOCONST(AssignedStats) * StatPointPoolUI_CharCreation_GetAssignedStat(AttribType eAttribType)
{
	if (g_CharacterCreationData->assignedStats && eaSize(&g_CharacterCreationData->assignedStats) > 0)
	{
		S32 i;
		for (i = 0; i < eaSize(&g_CharacterCreationData->assignedStats); i++)
		{
			if (g_CharacterCreationData->assignedStats[i] && g_CharacterCreationData->assignedStats[i]->eType == eAttribType)
			{
				return g_CharacterCreationData->assignedStats[i];
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_CharCreation_GetPointsSpentOnAttrib);
F32 StatPointPoolUI_CharCreation_GetPointsSpentOnAttrib(S32 eAttribType)
{
	NOCONST(AssignedStats) *pAssignedStat = StatPointPoolUI_CharCreation_GetAssignedStat(eAttribType);
	return pAssignedStat ? pAssignedStat->iPoints : 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_GetStatPointsByPoolName);
void StatPointPoolUI_GetStatPointsByPoolName(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pPlayer, SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	StatPointUIElement ***peaStatPoints = ui_GenGetManagedListSafe(pGen, StatPointUIElement);

	if (peaStatPoints)
	{
		S32 iStatPointCount = 0;

		// Get the stat point pool def
		StatPointPoolDef *pStatPointPoolDef = StatPointPool_DefFromName(pchStatPointPoolName);

		if (pStatPointPoolDef)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pStatPointPoolDef->ppValidAttribs, StatPointDef, pStatPointDef)
			{
				AttribType eAttribType = pStatPointDef ? StaticDefineIntGetInt(AttribTypeEnum, pStatPointDef->pchAttribName) : -1;
				if (eAttribType >= 0)
				{
					StatPointUIElement *pStatPointUIElement = eaGetStruct(peaStatPoints, parse_StatPointUIElement, iStatPointCount++);
					pStatPointUIElement->pchAttribName = pStatPointDef->pchAttribName;
					pStatPointUIElement->pchDisplayName = TranslateDisplayMessage(pStatPointDef->displayName);
					pStatPointUIElement->pchDescription = TranslateDisplayMessage(pStatPointDef->description);
					pStatPointUIElement->eAttribType = eAttribType;
					if (pPlayer->pChar->pattrBasic)
					{
						pStatPointUIElement->fCurrentValue = *F32PTR_OF_ATTRIB(pPlayer->pChar->pattrBasic, eAttribType);
					}
					else
					{
						NOCONST(AssignedStats) *pAssignedStat = StatPointPoolUI_CharCreation_GetAssignedStat(eAttribType);
						F32 fAssignedPoints = pAssignedStat ? pAssignedStat->iPoints : 0.f;
						// This case is assumed to be in character creation mode where the attribs are not accrued yet
						pStatPointUIElement->fCurrentValue = StatPointPoolUI_CharCreation_GetBaseAttribValue(pchStatPointPoolName, eAttribType) + fAssignedPoints;
					}
				}
			}
			FOR_EACH_END
		}

		// Trim the unused space
		eaSetSizeStruct(peaStatPoints, parse_StatPointUIElement, iStatPointCount);
	}
	ui_GenSetListSafe(pGen, peaStatPoints, StatPointUIElement);
}

static S32 StatPointPoolUI_CharCreation_GetStatPointsAssignedByPoolName(SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	StatPointPoolDef *pDef = StatPointPool_DefFromName(pchStatPointPoolName);
	int i, iAssigned = 0;
	if(pDef && g_CharacterCreationData->assignedStats)
	{
		ANALYSIS_ASSUME(pDef != NULL);
		for(i = eaSize(&g_CharacterCreationData->assignedStats) - 1; i >= 0; i--)
		{
			if (StatPointPool_ContainsAttrib(pDef, g_CharacterCreationData->assignedStats[i]->eType))
			{
				iAssigned += g_CharacterCreationData->assignedStats[i]->iPoints + g_CharacterCreationData->assignedStats[i]->iPointPenalty;
			}
		}
	}
	return iAssigned;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("StatPointPoolUI_CharCreation_ModifyStatPoint");
bool StatPointPoolUI_CharCreation_ModifyStatPoint(SA_PARAM_NN_STR const char *pchStatPointPoolName, S32 eAttribType, F32 fDelta)
{
	StatPointPoolDef *pDef = StatPointPool_DefFromName(pchStatPointPoolName);

	devassert(fDelta >= 1.f || fDelta <= -1.f);

	if (pDef && eAttribType >= 0)
	{
		ANALYSIS_ASSUME(pDef);
		if (StatPointPool_ContainsAttrib(pDef, eAttribType) && (fDelta >= 1.f || fDelta <= -1.f))
		{
			// See if the player has already invested in this attribute
			NOCONST(AssignedStats) *pAssignedStat = StatPointPoolUI_CharCreation_GetAssignedStat(eAttribType);

			if (pAssignedStat == NULL)
			{
				if (fDelta < 0.f)
				{
					return false;
				}
				else
				{
					// Create a new AssignedStats and add it to the array
					pAssignedStat = StructCreateNoConst(parse_AssignedStats);
					pAssignedStat->eType = eAttribType;
					pAssignedStat->iPoints = fDelta;
					pAssignedStat->iPointPenalty = 0;
					eaPush(&g_CharacterCreationData->assignedStats, pAssignedStat);

					return true;
				}
			}
			else
			{
				if (pAssignedStat->iPoints + fDelta <= 0.f)
				{
					// Destroy the assigned stat
					eaFindAndRemove(&g_CharacterCreationData->assignedStats, pAssignedStat);
					StructDestroyNoConst(parse_AssignedStats, pAssignedStat);
				}
				else
				{
					// Increment the points invested
					pAssignedStat->iPoints += fDelta;
				}
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_GetStatPointsAllowedByPoolName);
S32 StatPointPoolUI_GetStatPointsAllowedByPoolName(SA_PARAM_NN_VALID Entity *pPlayer, SA_PARAM_NN_STR const char *pchStatPointPoolName)
{
	S32 iResult;

	iResult = entity_GetAssignedStatAllowed(CONTAINER_NOCONST(Entity, pPlayer), pchStatPointPoolName);

	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StatPointPoolUI_GetStatPointsRemainingByPoolName);
S32 StatPointPoolUI_GetStatPointsRemainingByPoolName(SA_PARAM_NN_VALID Entity *pPlayer, SA_PARAM_NN_STR const char *pchStatPointPoolName, bool bInCharCreation)
{
	S32 iResult;

	if (bInCharCreation)
	{
		iResult = entity_GetAssignedStatAllowed(CONTAINER_NOCONST(Entity, pPlayer), pchStatPointPoolName) - StatPointPoolUI_CharCreation_GetStatPointsAssignedByPoolName(pchStatPointPoolName);
	}
	else
	{
		iResult = entity_GetAssignedStatUnspent(CONTAINER_NOCONST(Entity, pPlayer), pchStatPointPoolName);
	}

	return iResult;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void StatPointPoolUI_ModifyStatPointsInCartCB(bool bSuccess)
{
	if (bSuccess)
	{
		// Clear the cart
		StatPointPoolUI_CartClear();
	}
}

#include "StatPointsUI_c_ast.c"
