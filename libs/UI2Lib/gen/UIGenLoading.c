#include "TokenStore.h"
#include "error.h"
#include "StringCache.h"
#include "Expression.h"
#include "ResourceManager.h"
#include "MessageExpressions.h"
#include "partition_enums.h"
#include "file.h"
#include "BlockEarray.h"

#include "InputLib.h"
#include "dynFxInfo.h"
#include "inputKeybind.h"

#include "UIGen.h"
#include "UIGenJail.h"
#include "UIGenWindowManager.h"
#include "UIGenTutorial.h"
#include "UIGenPrivate.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenDnD_h_ast.h"
#include "UIGenPrivate_h_ast.h"
#include "ExpressionPrivate.h"
#include "cmdparse.h"
#include "smf_render.h"
#include "AutoGen/ExpressionPrivate_h_ast.h"
#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/UIGenTutorial_h_ast.h"

#include "StringFormat.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

extern bool gbNoGraphics;

int g_ui_GenExprStrictValidation = 1;
AUTO_CMD_INT(g_ui_GenExprStrictValidation, GenStrictValidation) ACMD_CMDLINE ACMD_ACCESSLEVEL(9);

DefineContext *g_ui_pGenExtraStates;
DefineContext *g_ui_pGenExtraSizes;

static bool GenTypeIsRegistered(UIGen *pGen)
{
	return pGen->eType && pGen->eType < g_GenState.iFuncTableSize && g_GenState.aFuncTable[pGen->eType].bIsRegistered;
}

static bool GenAnchorResourceValidate(UIGenAnchor *pAnchor, UIGen *pGen, const char *pchPathString, void *pData)
{
	UIDirection ePrimaryEdge;
	UIDirection eSecondaryEdge;
	if (pAnchor->eOrientation == UIHorizontal)
	{
		// Primary edges are left/right
		ePrimaryEdge = pAnchor->eAlignment & UIHorizontal;
		eSecondaryEdge = pAnchor->eAlignment & UIVertical;
		UI_GEN_FAIL_IF(pGen, (ePrimaryEdge == UIHorizontal || ePrimaryEdge == UINoDirection), "%s: The primary edge of an anchored gen may not be centered.  Please include one of Left or Right in the anchor alignment.", pGen->pchName);
	}
	else if (pAnchor->eOrientation == UIVertical)
	{
		// Primary edges are top/bottom
		ePrimaryEdge = pAnchor->eAlignment & UIVertical;
		eSecondaryEdge = pAnchor->eAlignment & UIHorizontal;
		UI_GEN_FAIL_IF(pGen, (ePrimaryEdge == UIVertical || ePrimaryEdge == UINoDirection), "%s: The primary edge of an anchored gen may not be centered.  Please include one of Top or Bottom in the anchor alignment.", pGen->pchName);
	}
	else
	{
		UI_GEN_FAIL_IF(pGen, true, "%s: The orientation must be Horizontal or Vertical.", pGen->pchName);
	}
	return true;
}

static bool GenActionResourceValidate(UIGenAction *pAction, UIGen *pGen, const char *pchPathString, void *pData)
{
	S32 i;
	for (i = 0; pAction && i < eaSize(&pAction->eachCommands); i++)
	{
		UI_GEN_FAIL_IF(pGen, pAction->eachCommands[i] && strstri(pAction->eachCommands[i], "GenShow "), "%s: Calls 'GenShow', should call 'GenAddWindow', 'GenAddModal', or change the parent's state.", pchPathString);
	}
	for (i = 0; pAction && i < eaSize(&pAction->eachSounds); i++)
	{
		UI_GEN_WARN_IF(pGen, g_ui_State.cbValidateAudio && !g_ui_State.cbValidateAudio(pAction->eachSounds[i], pGen->pchFilename),
			"%s: Invalid audio event %s", pchPathString, pAction->eachSounds[i]);
	}
	return true;
}

static bool keyIsDigit(const char *pchKey)
{
	S32 iNum = strtol(pchKey, NULL, 10);

	if (iNum > 0 && iNum <= 9)
	{
		return true;
	}
	else if (iNum == 0 && pchKey[0] == '0' && pchKey[1] == 0)
	{
		return true;
	}

	return false;
}

static bool GenKeyActionResourceValidate(UIGenKeyAction *pAction, UIGen *pGen, const char *pchPathString, void *pData)
{
	if (pAction)
	{
		if (pAction->pchKey && !strStartsWith(pAction->pchKey, UI_GEN_KEY_ACTION_COMMAND_PREFIX))
		{
			S32 iKey1;
			S32 iKey2;

			//  THIS IS A HACK. French keyboards do not have number keys 1, 2, 3 etc. but we have
			//keyactions in our UI which override number key behavior. This is a temporary solution
			//to supporting those keyactions until we come up with something that is better long term.
			//Keyactions are inherently not localization friendly, so we need to come up with something
			//more robust.	~DHOGBERG 6/18/2013
			if (keyIsDigit(pAction->pchKey))
			{
				keybind_ParseKeyString(pAction->pchKey, &iKey1, &iKey2, KeyboardLocale_EnglishUS);
			}
			else
			{
				keybind_ParseKeyString(pAction->pchKey, &iKey1, &iKey2, KeyboardLocale_Current);
			}
			pAction->iKey1 = iKey1;
			pAction->iKey2 = iKey2;
			// Make sure the value didn't get truncated via the bitpacking.
			assert(pAction->iKey1 == iKey1);
			assert(pAction->iKey2 == iKey2);
			UI_GEN_FAIL_IF(pGen, (!pAction->iKey1 || (strchr(pAction->pchKey, '+') && !pAction->iKey2)),
				"Invalid key string %s in KeyAction %s. If you meant to use a command-based bind, use a '"UI_GEN_KEY_ACTION_COMMAND_PREFIX"' prefix.", pAction->pchKey, pchPathString);
		}
		return GenActionResourceValidate(&pAction->action, pGen, pchPathString, pData);
	}
	return true;
}

static bool GenTextureAssemblyResourceValidate(UIGenTextureAssembly *pAssembly, UIGen *pGen, const char *pchPathString, void *pData)
{
	if (pAssembly)
	{
		if (pAssembly->pchAssembly && !strchr(pAssembly->pchAssembly, STRFMT_TOKEN_START))
		{
			SET_HANDLE_FROM_STRING("UITextureAssembly", pAssembly->pchAssembly, pAssembly->hAssembly);
			UI_GEN_FAIL_IF(pGen, !GET_REF(pAssembly->hAssembly),
				"UITextureAssembly reference \"%s\" not found.", pAssembly->pchAssembly);
			//pAssembly->pchAssembly = NULL;
		}
		return true;
	}
	return true;
}

static bool GenResourceValidate(UIGen *pGen);

static bool GenInternalResourceValidate(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor, UIGenValidateFunc cbValidate)
{
	S32 i;

	UI_GEN_FAIL_IF(pGen, (pInt->eType != pGen->eType),
		"Base type %d (%s) has non-matching override %s, of type %d (%s)",
		pGen->eType, ui_GenGetTypeName(pGen->eType), pchDescriptor,
		pInt->eType, ui_GenGetTypeName(pInt->eType));

	for (i = 0; i < eaSize(&pInt->eaChildren); i++)
	{
		UI_GEN_FAIL_IF(pGen, !stricmp(REF_STRING_FROM_HANDLE(pInt->eaChildren[i]->hChild), pGen->pchName),
			"%s: Contains itself as a child", pchDescriptor);
	}

	UI_GEN_FAIL_IF(pGen, pInt->pos.Width.eUnit == UIUnitFixed && pInt->pos.Width.fMagnitude && pInt->pos.Width.fMagnitude < 1, "%s: Width is fixed but < 1", pchDescriptor);
	UI_GEN_FAIL_IF(pGen, pInt->pos.MinimumWidth.eUnit == UIUnitFixed && pInt->pos.MinimumWidth.fMagnitude && pInt->pos.MinimumWidth.fMagnitude < 1, "%s: MinimumWidth is fixed but < 1", pchDescriptor);
	UI_GEN_FAIL_IF(pGen, pInt->pos.MaximumWidth.eUnit == UIUnitFixed && pInt->pos.MaximumWidth.fMagnitude && pInt->pos.MaximumWidth.fMagnitude < 1, "%s: MaximumWidth is fixed but < 1", pchDescriptor);
	UI_GEN_FAIL_IF(pGen, pInt->pos.Height.eUnit == UIUnitFixed && pInt->pos.Height.fMagnitude && pInt->pos.Height.fMagnitude < 1, "%s: Height is fixed but < 1", pchDescriptor);
	UI_GEN_FAIL_IF(pGen, pInt->pos.MinimumHeight.eUnit == UIUnitFixed && pInt->pos.MinimumHeight.fMagnitude && pInt->pos.MinimumHeight.fMagnitude < 1, "%s: MinimumHeight is fixed but < 1", pchDescriptor);
	UI_GEN_FAIL_IF(pGen, pInt->pos.MaximumHeight.eUnit == UIUnitFixed && pInt->pos.MaximumHeight.fMagnitude && pInt->pos.MaximumHeight.fMagnitude < 1, "%s: MaximumHeight is fixed but < 1", pchDescriptor);

	if (pInt->pos.Width.eUnit == UIUnitFitContents
		|| pInt->pos.MaximumWidth.eUnit == UIUnitFitContents
		|| pInt->pos.MinimumWidth.eUnit == UIUnitFitContents
		|| pInt->pos.Height.eUnit == UIUnitFitContents
		|| pInt->pos.MaximumHeight.eUnit == UIUnitFitContents
		|| pInt->pos.MinimumHeight.eUnit == UIUnitFitContents
		)
	{
		UI_GEN_FAIL_IF(pGen, !g_GenState.aFuncTable[pInt->eType].cbFitContentsSize,
			"%s: Uses FitContents sizing, but gen type %s has no FitContents callback.",
			pchDescriptor, ui_GenGetTypeName(pInt->eType));
	}

	if (pInt->pRelative)
	{
		UI_GEN_FAIL_IF(pGen, (pInt->pRelative->LeftFrom.pchName && !(pInt->pRelative->LeftFrom.eOffset & (UILeft | UIRight))),
			"%s: LeftFrom must be specified relative to left or right", pchDescriptor);
		UI_GEN_FAIL_IF(pGen, (pInt->pRelative->RightFrom.pchName && !(pInt->pRelative->RightFrom.eOffset & (UILeft | UIRight))),
			"%s: RightFrom must be specified relative to left or right", pchDescriptor);

		UI_GEN_FAIL_IF(pGen, (pInt->pRelative->TopFrom.pchName && !(pInt->pRelative->TopFrom.eOffset & (UITop | UIBottom))),
			"%s: TopFrom must be specified relative to left or right", pchDescriptor);
		UI_GEN_FAIL_IF(pGen, (pInt->pRelative->BottomFrom.pchName && !(pInt->pRelative->BottomFrom.eOffset & (UITop | UIBottom))),
			"%s: BottomFrom must be specified relative to left or right", pchDescriptor);
	}

	UI_GEN_FAIL_IF(pGen,
		(pInt->pAssembly &&
			((pInt->pAssembly->pchAssembly && *pInt->pAssembly->pchAssembly) || GET_REF(pInt->pAssembly->hAssembly)) &&
		(pInt->pAssembly->iPaddingBottom || pInt->pAssembly->iPaddingTop || pInt->pAssembly->iPaddingLeft || pInt->pAssembly->iPaddingRight)),
		"Specifies both an assembly and a padding; the padding should be in the assembly.");

	for (i = 0; i < eaSize(&pInt->eaChildren); i++)
	{
		UI_GEN_WARN_IF(pGen, !GET_REF(pInt->eaChildren[i]->hChild),
			"%s: Invalid child %s", pchDescriptor, REF_STRING_FROM_HANDLE(pInt->eaChildren[i]->hChild));
	}

	for (i = 0; i < eaSize(&pInt->eaInlineChildren); i++)
		if (!GenResourceValidate(pInt->eaInlineChildren[i]))
			return false;

	if (pInt->pTooltip)
	{
		UI_GEN_FAIL_IF(pGen,
			pInt->pTooltip->pchAssembly && *pInt->pTooltip->pchAssembly &&
			!strchr(pInt->pTooltip->pchAssembly, STRFMT_TOKEN_START) &&
			!RefSystem_ReferentFromString("UITextureAssembly", pInt->pTooltip->pchAssembly),
			"%s: Invalid tooltip assembly %s", pchDescriptor, pInt->pTooltip->pchAssembly);

		for (i = 0; i < eaSize(&pInt->pTooltip->secondaryTooltipGroup.eaSecondaryToolTips); i++)
		{
			UIGenSecondaryTooltip *pSecondary = pInt->pTooltip->secondaryTooltipGroup.eaSecondaryToolTips[i];

			UI_GEN_FAIL_IF(pGen,
				pSecondary->pchAssembly && *pSecondary->pchAssembly &&
				!strchr(pSecondary->pchAssembly, STRFMT_TOKEN_START) &&
				!RefSystem_ReferentFromString("UITextureAssembly", pSecondary->pchAssembly),
				"%s: Invalid secondary tooltip assembly %s", pchDescriptor, pSecondary->pchAssembly);
		}
	}

	{
		static S32 iCenterXColumn = -1;
		static S32 iCenterYColumn = -1;

		if (iCenterXColumn < 0)
			ParserFindColumn(parse_UIGenInternal, "CenterX", &iCenterXColumn);
		if (!TSTB(pInt->bf, iCenterXColumn))
		{
			pInt->Transformation.CenterX.fMagnitude = .5f;
			pInt->Transformation.CenterX.eUnit = UIUnitPercentage;
		}

		if (iCenterYColumn < 0)
			ParserFindColumn(parse_UIGenInternal, "CenterY", &iCenterYColumn);
		if (!TSTB(pInt->bf, iCenterYColumn))
		{
			pInt->Transformation.CenterY.fMagnitude = .5f;
			pInt->Transformation.CenterY.eUnit = UIUnitPercentage;
		}
	}

	return cbValidate ? cbValidate(pGen, pInt, pchDescriptor) : true;
}

static bool GenParserValidate(UIGen *pGen)
{
	S32 i;

	UI_GEN_FAIL_IF(pGen, (!pGen->pchName || strchr(pGen->pchName, '.') || !resIsValidName(pGen->pchName) || pGen->pchName[0] == '_'),
		"Invalid name, gen names must be only A-Z, 0-9, -, and _.");

	UI_GEN_FAIL_IF(pGen, (eaSize(&pGen->eaComplexStates) > 32),
		"Has %d complex states, the limit is 32", eaSize(&pGen->eaComplexStates));

	UI_GEN_FAIL_IF(
		pGen,
		pGen->pSpriteCache && pGen->pSpriteCache->iFrameSkip <= 0,
		"Sprite caches must have a non-zero frameskip.");

	// Figure out this gen's type, if we can
	if (!pGen->eType && pGen->pBase)
		pGen->eType = pGen->pBase->eType;
	if (!pGen->eType && pGen->pLast)
		pGen->eType = pGen->pLast->eType;
	for (i = 0; i < eaSize(&pGen->eaStates) && !pGen->eType; i++)
		if (pGen->eaStates[i]->pOverride)
			pGen->eType = pGen->eaStates[i]->pOverride->eType;
	for (i = 0; i < eaSize(&pGen->eaComplexStates) && !pGen->eType; i++)
		if (pGen->eaComplexStates[i]->pOverride)
			pGen->eType = pGen->eaComplexStates[i]->pOverride->eType;

	// If we're borrowing we can figure out the type implicitly later, during RESVALIDATE_POST_READ.
	UI_GEN_FAIL_IF(pGen, !(GenTypeIsRegistered(pGen) || eaSize(&pGen->eaBorrowTree)), "Has an unregistered/no type %d", pGen->eType);

	return true;
}

void GenCloneTimers(UIGen *pAddTo, UIGen *pBorrowedGen)
{
	int i, j;
	for (i = 0; i < eaSize(&pBorrowedGen->eaTimers); i++)
	{
		UIGenTimer *pTimer = pBorrowedGen->eaTimers[i];
		bool bFound = false;
		if (pTimer->pchName && pTimer->pchName[0])
		{
			for (j = 0; j < eaSize(&pAddTo->eaTimers); j++)
			{
				if (stricmp(pAddTo->eaTimers[j]->pchName, pTimer->pchName) == 0)
				{
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
			eaPush(&pAddTo->eaTimers, StructClone(parse_UIGenTimer, pTimer));
	}
}

static bool GenMeetsBorrowRequirements(UIGen *pGen, UIGen *pBorrow)
{
	int i, j;
	if (eaSize(&pBorrow->eaRequiredBorrows) == 0)
		return true;
	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
		for (j = 0; j < eaSize(&pBorrow->eaRequiredBorrows); j++)
			if (GET_REF(pGen->eaBorrowed[i]->hGen) == GET_REF(pBorrow->eaRequiredBorrows[j]->hGen))
				return true;
	return false;
}

static bool GenAddBorrow(UIGen *pAddTo, UIGenBorrowed *pBorrowed)
{
	S32 i;
	UIGen *pBorrowedGen = pBorrowed ? GET_REF(pBorrowed->hGen) : NULL;
	assert(pAddTo);
	UI_GEN_FAIL_IF(pAddTo, pAddTo == pBorrowedGen, "Borrows from itself.");
	UI_GEN_FAIL_IF(pAddTo, !(pBorrowed && pBorrowedGen), "Borrows from an unknown/invalid gen '%s'.", pBorrowed ? REF_STRING_FROM_HANDLE(pBorrowed->hGen) : "");
	UI_GEN_FAIL_IF(pAddTo, !GenMeetsBorrowRequirements(pAddTo, pBorrowedGen), "BorrowsFrom %s, which has unfulfilled RequiresBorrow statements.", REF_STRING_FROM_HANDLE(pBorrowed->hGen));

	// No duplicates, but diamond inheritance is not an error.
	for (i = 0; i < eaSize(&pAddTo->eaBorrowed); i++)
		if (GET_REF(pAddTo->eaBorrowed[i]->hGen) == pBorrowedGen)
			return true;
	for (i = 0; i < eaSize(&pBorrowedGen->eaBorrowTree); i++)
		if (!GenAddBorrow(pAddTo, pBorrowedGen->eaBorrowTree[i]))
			return false;
	// If we're in the list now, though, we borrow from ourself somehow, and that's bad.
	for (i = 0; i < eaSize(&pAddTo->eaBorrowed); i++)
	{
		UI_GEN_FAIL_IF(pAddTo, (GET_REF(pAddTo->eaBorrowed[i]->hGen) == pBorrowedGen),
			"BorrowFrom tree contains itself.");
	}
	eaPush(&pAddTo->eaBorrowed, StructClone(parse_UIGenBorrowed, pBorrowed));
	GenCloneTimers(pAddTo, pBorrowedGen);
	return true;
}

bool ui_GenInitializeBorrows(UIGen *pGen)
{
	S32 i;

	if (eaSize(&pGen->eaBorrowed) > 0)
		return true;

	for (i = 0; i < eaSize(&pGen->eaBorrowTree); i++)
		if (!GenAddBorrow(pGen, pGen->eaBorrowTree[i]))
			return false;
	return true;
}

static bool GenResourceValidate(UIGen *pGen)
{
	S32 i;
	static const char *s_pchSelf = NULL;
	static const char *s_pchParent = NULL;

	if (s_pchSelf == NULL)
	{
		s_pchSelf = allocAddString("_Self");
		s_pchParent = allocAddString("_Parent");
	}

	eaClear(&pGen->eaBorrowed);
	if (!ui_GenInitializeBorrows(pGen))
		return false;

	for (i = 0; i < eaSize(&pGen->eaBorrowed) && (!pGen->eType || i > 0 && pGen->eType == kUIGenTypeBox); i++)
		if (GET_REF(pGen->eaBorrowed[i]->hGen))
			pGen->eType = GET_REF(pGen->eaBorrowed[i]->hGen)->eType;
	UI_GEN_FAIL_IF(pGen, !GenTypeIsRegistered(pGen), "Has an unregistered/no type %d", pGen->eType);

	if (pGen->pBase && !GenInternalResourceValidate(pGen, pGen->pBase, "base", g_GenState.aFuncTable[pGen->eType].cbValidate))
		return false;
	if (pGen->pLast && !GenInternalResourceValidate(pGen, pGen->pLast, "last", g_GenState.aFuncTable[pGen->eType].cbValidate))
		return false;

	for (i = eaSize(&pGen->eaStates) - 1; i >= 0; i--)
	{
		// If no enter or exit event and no override is set, this state is pointless.
		if (!(pGen->eaStates[i]->pOverride || pGen->eaStates[i]->pOnExit || pGen->eaStates[i]->pOnEnter))
			StructDestroy(parse_UIGenStateDef, eaRemove(&pGen->eaStates, i));

		if (pGen->eaStates[i]->pOverride && !GenInternalResourceValidate(pGen, pGen->eaStates[i]->pOverride, ui_GenGetStateName(pGen->eaStates[i]->eState), g_GenState.aFuncTable[pGen->eType].cbValidate))
			return false;
	}

	for (i = eaSize(&pGen->eaComplexStates) - 1; i >= 0; i--)
	{
		UIGenComplexStateDef *pComplexStateDef = pGen->eaComplexStates[i];
		int j;
		bool bFoundCondition = false;
		for (j = 0; j < eaSize(&pComplexStateDef->eaIntCondition); j++)
		{
			UIGenIntCondition *pCond = pComplexStateDef->eaIntCondition[j];
			bFoundCondition = true;
			if (s_pchSelf != pCond->pchGen && s_pchParent != pCond->pchGen)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen, pCond->hGen);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen), "IntCondition targets invalid gen %s", pCond->pchGen);
			}
		}
		for (j = 0; j < eaSize(&pComplexStateDef->eaFloatCondition); j++)
		{
			UIGenFloatCondition *pCond = pComplexStateDef->eaFloatCondition[j];
			bFoundCondition = true;
			if (s_pchSelf != pCond->pchGen && s_pchParent != pCond->pchGen)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen, pCond->hGen);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen), "FloatCondition targets invalid gen %s", pCond->pchGen);
			}
		}
		for (j = 0; j < eaSize(&pComplexStateDef->eaStringCondition); j++)
		{
			UIGenStringCondition *pCond = pComplexStateDef->eaStringCondition[j];
			bFoundCondition = true;
			if (s_pchSelf != pCond->pchGen && s_pchParent != pCond->pchGen)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen, pCond->hGen);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen), "StringCondition targets invalid gen %s", pCond->pchGen);
			}
		}

		for (j = 0; j < eaSize(&pComplexStateDef->eaIntCondition2); j++)
		{
			UIGenCondition2 *pCond = pComplexStateDef->eaIntCondition2[j];
			bFoundCondition = true;
			if (s_pchSelf != pCond->pchGen1 && s_pchParent != pCond->pchGen1)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen1, pCond->hGen1);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen1), "IntCondition2 targets invalid gen %s", pCond->pchGen1);
			}
			if (s_pchSelf != pCond->pchGen2 && s_pchParent != pCond->pchGen2)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen2, pCond->hGen2);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen2), "IntCondition2 targets invalid gen %s", pCond->pchGen2);
			}
		}
		for (j = 0; j < eaSize(&pComplexStateDef->eaFloatCondition2); j++)
		{
			UIGenCondition2 *pCond = pComplexStateDef->eaFloatCondition2[j];
			bFoundCondition = true;
			if (s_pchSelf != pCond->pchGen1 && s_pchParent != pCond->pchGen1)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen1, pCond->hGen1);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen1), "FloatCondition2 targets invalid gen %s", pCond->pchGen1);
			}
			if (s_pchSelf != pCond->pchGen2 && s_pchParent != pCond->pchGen2)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen2, pCond->hGen2);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen2), "FloatCondition2 targets invalid gen %s", pCond->pchGen2);
			}
		}
		for (j = 0; j < eaSize(&pComplexStateDef->eaStringCondition2); j++)
		{
			UIGenCondition2 *pCond = pComplexStateDef->eaStringCondition2[j];
			bFoundCondition = true;
			if (s_pchSelf != pCond->pchGen1 && s_pchParent != pCond->pchGen1)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen1, pCond->hGen1);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen1), "StringCondition2 targets invalid gen %s", pCond->pchGen1);
			}
			if (s_pchSelf != pCond->pchGen2 && s_pchParent != pCond->pchGen2)
			{
				REF_HANDLE_SET_FROM_STRING(g_GenState.hGenDict, pCond->pchGen2, pCond->hGen2);
				UI_GEN_FAIL_IF(pGen, !GET_REF(pCond->hGen2), "StringCondition2 targets invalid gen %s", pCond->pchGen2);
			}
		}

		bFoundCondition |= !!eaiSize(&pComplexStateDef->eaiInState);
		bFoundCondition |= !!eaiSize(&pComplexStateDef->eaiNotInState);
		bFoundCondition |= !!pComplexStateDef->pCondition;
		
		UI_GEN_FAIL_IF(pGen, !bFoundCondition, "ComplexStateDefs must have at least one condition");

		if (pComplexStateDef->pOverride && !GenInternalResourceValidate(pGen, pComplexStateDef->pOverride, "complex state", g_GenState.aFuncTable[pGen->eType].cbValidate))
			return false;

		// If no enter or exit event and no override is set, this complex state is pointless.
		if (!(pComplexStateDef->pOverride || pComplexStateDef->pOnExit || pComplexStateDef->pOnEnter))
			StructDestroy(parse_UIGenComplexStateDef, eaRemove(&pGen->eaComplexStates, i));
	}

	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
		UI_GEN_FAIL_IF(pGen, !pBorrowed,
			"Borrows from an invalid gen, %s", REF_STRING_FROM_HANDLE(pGen->eaBorrowed[i]->hGen));
		UI_GEN_FAIL_IF(pGen, (pGen && pBorrowed->eType != pGen->eType && pBorrowed->eType != kUIGenTypeBox),
			"Type %s borrows from gen %s, of type %s", ui_GenGetTypeName(pGen->eType), pBorrowed->pchName,
			ui_GenGetTypeName(pBorrowed->eType));

		if (pBorrowed->bPopup)
			pGen->bPopup = true;

		if (pBorrowed->pchJailCell)
			pGen->pchJailCell = pBorrowed->pchJailCell;

		resAddFileDep(pBorrowed->pchFilename);
	}

	ParserScanForSubstruct(parse_UIGen, pGen, parse_UIGenAction, 0, 0, GenActionResourceValidate, NULL);
	ParserScanForSubstruct(parse_UIGen, pGen, parse_UIGenKeyAction, 0, 0, GenKeyActionResourceValidate, NULL);
	ParserScanForSubstruct(parse_UIGenAnchor, pGen, parse_UIGenKeyAction, 0, 0, GenAnchorResourceValidate, NULL);

	UI_GEN_FAIL_IF(pGen, pGen->pchJailCell && pGen->bPopup,
		"Gens should not be in jail cells and be flagged as popups, as this will destroy performance.");

	return true;
}

static bool GenResValidateCheckReferences(UIGen *pGen);

static bool GenInternalResourcePostValidate(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	return true;
}

static bool GenResValidateCheckReferences(UIGen *pGen)
{
	S32 i;
	if (pGen->pBase && !GenInternalResourcePostValidate(pGen, pGen->pBase, "base"))
		return false;

	for (i = 0; i < eaSize(&pGen->eaStates); i++)
		if (pGen->eaStates[i]->pOverride && !GenInternalResourcePostValidate(pGen, pGen->eaStates[i]->pOverride, ui_GenGetStateName(pGen->eaStates[i]->eState)))
			return false;
	for (i = 0; i < eaSize(&pGen->eaComplexStates); i++)
		if (pGen->eaComplexStates[i]->pOverride &&  !GenInternalResourcePostValidate(pGen, pGen->eaComplexStates[i]->pOverride, "complex state"))
			return false;

	return true;
}


AUTO_FIXUPFUNC;
TextParserResult ui_GenCodeInterfaceParserFixup(UIGenCodeInterface *pCode, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pCode->bManagedStructures)
			eaDestroyStructVoid(&pCode->eaList, pCode->pListTable);
		else
			eaDestroy(&pCode->eaList);
		if (pCode->bManagedPointer)
			StructDestroyVoid(pCode->pTable, pCode->pStruct);
		break;
	}
	return PARSERESULT_SUCCESS;
}

bool ui_GenGenerateExpr(Expression *pExpr, UIGen *pGen, const char *pchPathString, ExprContext *pContext)
{
	if (pExpr)
	{
		const char *pchExpr = exprGetCompleteString(pExpr);
		// This is WAY too invasive.  I can't even write a comment with matched squigglies.
//		UI_GEN_FAIL_IF_RETURN(pGen, strchr(pchExpr, '{') < strchr(pchExpr, '}') && !(strstri(pchExpr, "GenRunCommand") || strstri(pchExpr, "StringFormat") || strstri(pchExpr, "GenListSetSelectedRowFilter")), 1,
	//		"%s: Using {...} in an expression string that is not a command or a string formatter, this does not work like you think it does.\n\n%s",
		//	pchPathString, pchExpr);
		if (g_ui_GenExprStrictValidation)
		{
			exprContextSetPointerVar(pContext, "GenData", NULL, parse_UIGenBadPointer, true, true);
			exprContextSetPointerVar(pContext, "RowData", NULL, parse_UIGenBadPointer, true, true);
			exprContextSetPointerVar(pContext, "GenInstanceData", NULL, parse_UIGenBadPointer, true, true);
			exprContextSetPointerVar(pContext, "TabData", NULL, parse_UIGenBadPointer, true, true);
		}
		if (exprGenerate(pExpr, pContext))
		{
			if (!UI_GEN_ALLOW_OBJECT_PATHS)
			{
				int i;
				for(i = 0; i < beaSize(&pExpr->postfixEArray); i++)
				{
					MultiVal *val = &pExpr->postfixEArray[i];
					UI_GEN_FAIL_IF_RETURN(
						pGen, val->type==MULTIOP_OBJECT_PATH, 1,
						"Using an object path in a project that does not allow them.\n\n%s",
						pchExpr);
				}
			}
			return false;
		}
		else
			return true;
	}
	return false;
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenInternalParserFixup(UIGenInternal *pInt, enumTextParserFixupType eType, void *pExtraData)
{	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		{

			ParseTable *pTable = ui_GenInternalGetType(pInt);
			S32 i;
			if (!pTable)
				return PARSERESULT_INVALID;


			FORALL_PARSETABLE(pTable, i)
			{
				// RGBA tokens do not support default values, so instead we need to set
				// white as the default color here.
				if (pTable[i].format == TOK_FORMAT_COLOR)
					TokenStoreSetInt(pTable, i, pInt, 0, 0xFFFFFFFF, NULL, NULL);
			}
			break;
		}
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenBundleTextureParserFixup(UIGenBundleTexture *pBundle, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		pBundle->uiTopLeftColor = 0xFFFFFFFF;
		pBundle->uiTopRightColor = 0xFFFFFFFF;
		pBundle->uiBottomLeftColor = 0xFFFFFFFF;
		pBundle->uiBottomRightColor = 0xFFFFFFFF;
		break;
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenParserFixup(UIGen *pGen, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		if (!GenParserValidate(pGen))
			return PARSERESULT_INVALID;
		break;
	case FIXUPTYPE_DESTRUCTOR:
		ui_GenReset(pGen);
		eaFindAndRemoveFast(&g_GenState.eaFreeQueue, pGen);
		break;
	}

	return PARSERESULT_SUCCESS;
}

static bool GenResValidatePostTextReading(UIGen *pGen);

static void ui_GenInternalResourceFixupCB(UIGenInternal *pInt)
{
	if (pInt)
	{
		ParseTable *pTable = ui_GenInternalGetType(pInt);
		S32 iColumn = ParserFindColumnFromOffset(pTable, offsetof(UIGenInternal, eaInlineChildren));
		S32 i;
		TokenSetSpecified(pTable, iColumn, pInt, -1, false);

		for (i = 0; i < eaSize(&pInt->eaInlineChildren); i++)
			GenResValidatePostTextReading(pInt->eaInlineChildren[i]);
	}
}

static bool GenResValidatePostTextReading(UIGen *pGen)
{
	const char *apchCritical[] = {"Modal_Root", "Window_Root", "Root", "HUD_Root", "HUD", "ModalDialog_Root", "Cutscene_Root"};
	bool bValid = true;
	S32 i;

	bValid &= GenResourceValidate(pGen);
	if (bValid)
	{
		ExprContext *pContext = ui_GenGetContext(pGen);
		// This pointer must be set to NULL to work with static checking.
		exprContextSetPointerVar(pContext, "Self", NULL, parse_UIGen, true, true);
		// Likewise we need to make sure GenData will exist so we can set its type.
		exprContextSetPointerVar(pContext, "GenData", NULL, parse_UIGen, true, true);
		exprContextSetPartition(pContext, PARTITION_CLIENT);
		bValid &= !ParserScanForSubstruct(parse_UIGen, pGen, parse_Expression, 0, 0, ui_GenGenerateExpr, pContext);
	}

	if (!bValid)
		for (i = 0; i < ARRAY_SIZE_CHECKED(apchCritical); i++)
			if (!stricmp(pGen->pchName, apchCritical[i]))
				ErrorFilenamef(pGen->pchFilename, "Validation errors in gen %s (%s), which is critical for game operation. Fix this immediately.", pGen->pchName, pGen->pchFilename);

	UI_GEN_FAIL_IF(pGen, !bValid, "Validation errors.");

	ui_GenInternalResourceFixupCB(pGen->pBase);
	for (i = 0; i < eaSize(&pGen->eaStates); i++)
	{
		ui_GenInternalResourceFixupCB(pGen->eaStates[i]->pOverride);
		// StateDef Visible OnExit should actually be in BeforeHide.
		if (pGen->eaStates[i]->eState == kUIGenStateVisible && pGen->eaStates[i]->pOnExit)
		{
			UI_GEN_FAIL_IF(pGen, pGen->pBeforeHide,
				"Has an OnExit event for the Visible state, which will never be run. Use BeforeHide instead.");
			pGen->pBeforeHide = pGen->eaStates[i]->pOnExit;
			pGen->eaStates[i]->pOnExit = NULL;
		}
	}
	for (i = 0; i < eaSize(&pGen->eaComplexStates); i++)
		ui_GenInternalResourceFixupCB(pGen->eaComplexStates[i]->pOverride);

	if (pGen->pLast)
		ui_GenInternalResourceFixupCB(pGen->pLast);

	return bValid;
}

static bool GenReset(UIGen *pGen, UIGen *pParent, const char *pchPathString, void *pData)
{
	S32 i;
	S32 j;

	if (!pGen)
		return true;

	ui_GenReset(pGen);

	for (i = eaSize(&pGen->eaBorrowed) - 1; i >= 0; i--)
	{
		UIGen *pBorrow = GET_REF(pGen->eaBorrowed[i]->hGen);
		if (pBorrow)
		{
			for (j = 0; j < eaSize(&pBorrow->eaCopyVars); j++)
			{
				UIGenVarTypeGlob *pGlob = pBorrow->eaCopyVars[j];
				if (eaIndexedFindUsingString(&pGen->eaVars, pGlob->pchName) < 0)
					eaPush(&pGen->eaVars, StructClone(parse_UIGenVarTypeGlob, pGlob));
			}
		}
	}

	return true;
}

static void GenResValidatePostBinning(UIGen *pGen)
{
	ParserScanForSubstruct(parse_UIGen, pGen, parse_UIGenKeyAction, 0, 0, GenKeyActionResourceValidate, NULL);
	ParserScanForSubstruct(parse_UIGen, pGen, parse_UIGenTextureAssembly, 0, 0, GenTextureAssemblyResourceValidate, NULL);
	ParserScanForSubstruct(parse_UIGen, pGen, parse_UIGen, 0, 0, GenReset, NULL);
}

static int GenResValidate(enumResourceValidateType eType, const char *pDictName, const char *pGenName, UIGen *pGen, U32 iUserID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		GenResValidatePostTextReading(pGen);
		return VALIDATE_HANDLED;
	case RESVALIDATE_POST_BINNING:
		GenResValidatePostBinning(pGen);
		return VALIDATE_HANDLED;
	case RESVALIDATE_CHECK_REFERENCES:
		GenResValidateCheckReferences(pGen);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

static void GenDeleteExpressionLine(void *pExprLine)
{
	if (pExprLine)
	{
		StructDestroy(parse_ExprLine, pExprLine);
	}
}

static bool GenDeleteExpressionLines(Expression *pExpr, UIGen *pGen, const char *pchPathString, ExprContext *pContext)
{
	if (pExpr->lines)
	{
		eaDestroyEx(&pExpr->lines, GenDeleteExpressionLine);
	}
	return true;
}

static void GenResEvent(enumResourceEventType eType, const char *pDictName, const char *pResourceName, UIGen *pGen, void *pUserData)
{
	// this doesn't really accomplish anything, since the value of pPerfInfo doesn't matter.
/*	static const char *s_pchReloadName;
	static PERFINFO_TYPE *s_pchReloadTimer;
	switch (eType)
	{
	case RESEVENT_RESOURCE_PRE_MODIFIED:
		s_pchReloadName = pGen->pchName;
		s_pchReloadTimer = pGen->pPerfInfo;
		break;
	case RESEVENT_RESOURCE_MODIFIED:
		if (pGen->pchName == s_pchReloadName)
			pGen->pPerfInfo = s_pchReloadTimer;
	}*/

#if PLATFORM_CONSOLE
	// Remove all lines from expressions in console versions
	if (isProductionMode())
	{
		ParserScanForSubstruct(parse_UIGen, pGen, parse_Expression, 0, 0, GenDeleteExpressionLines, NULL);
	}
#endif
}

static void ui_GenPreLoad(void)
{
	// This will autoinstantiate the context if necessary.
	ui_GenInitPointerVar("Self", parse_UIGen);
	ui_GenInitPointerVar("FocusedGen", parse_UIGen);
	ui_GenInitPointerVar("DragDropData", parse_UIGenDragDrop);
	ui_GenInitPointerVar("Tutorial", parse_UIGenTutorialInfo);
	ui_GenInitFloatVar("ElapsedTime");
	ui_GenInitIntVar("ScreenWidth", g_ui_State.screenWidth);
	ui_GenInitIntVar("ScreenHeight", g_ui_State.screenHeight);
	ui_GenInitIntVar("Frame", g_ui_State.uiFrameCount);
	ui_GenSetIntVar("Key", 0);
	exprContextSetStaticCheck(g_GenState.pContext, ExprStaticCheck_AllowTypeChanges);
	inpUpdateExpressionContext(g_GenState.pContext, true);

#if _XBOX
	eaiPush(&g_GenState.eaiGlobalOnStates, kUIGenStateXbox);
#else
	eaiPush(&g_GenState.eaiGlobalOnStates, kUIGenStateWindows);
#endif
}

void ui_GenInternalResourceFixup(UIGenInternal *pInt)
{
	ui_GenInternalResourceFixupCB(pInt);
}

void ui_GenInternalResourcePostBinning(UIGenInternal *pInt)
{
	ParseTable *pTable = pInt ? ui_GenInternalGetType(pInt) : NULL;
	if (pTable)
	{
		ParserScanForSubstruct(pTable, pInt, parse_UIGenKeyAction, 0, 0, GenKeyActionResourceValidate, NULL);
		ParserScanForSubstruct(pTable, pInt, parse_UIGenTextureAssembly, 0, 0, GenTextureAssemblyResourceValidate, NULL);
		ParserScanForSubstruct(pTable, pInt, parse_UIGen, 0, 0, GenReset, NULL);
	}
}

void ui_GenInternalResourceValidate(const char *pchName, const char *pchDescriptor, UIGenInternal *pInt)
{
	UIGen FakeGen = {0};
	FakeGen.pchName = pchName;
	FakeGen.eType = pInt->eType;
	GenInternalResourceValidate(&FakeGen, pInt, pchDescriptor, g_GenState.aFuncTable[pInt->eType].cbValidate);
}

void ui_GenAddExprFuncs(const char *pchExprTag)
{
	if (!g_GenState.stGenFuncTable)
	{
		g_GenState.stGenFuncTable = exprContextCreateFunctionTable("UIGenLoading");
	}
	exprContextAddFuncsToTableByTag(g_GenState.stGenFuncTable, pchExprTag);
}

AUTO_STARTUP(UISize);
void ui_LoadSizes(void)
{
	UIGenSizes Sizes = {0};
	S32 i;

	if(!gbNoGraphics)
	{
		loadstart_printf("Loading UI sizes...");

		ParserLoadFiles("ui", "UISizes.def", "UISizes.bin", 0, parse_UIGenSizes, &Sizes);
		for (i=0; i < eaSize(&Sizes.eaSizes); i++)
		{
			if (!DefineLookup(g_ui_pGenExtraSizes, Sizes.eaSizes[i]->pchName))
				DefineAddInt_EnumOverlapOK(g_ui_pGenExtraSizes, Sizes.eaSizes[i]->pchName, Sizes.eaSizes[i]->iValue);
			else
				ErrorFilenamef(Sizes.pchFilename, "Duplicate UI size %s.", Sizes.eaSizes[i]->pchName);
		}

		StructDeInit(parse_UIGenSizes, &Sizes);
		loadend_printf(" done (%d sizes).", i);
	}
}

extern bool g_ui_GenDumpExprFunctions;

void ui_LoadGens(void)
{
	UIGen *pGen = NULL;
	int iUIGenStateMin;
	int iUIGenStateMax;
	int i;
	ui_GenPreLoad();

	// Read in per-project states and add them to the lookup table.
	
	DefineGetMinAndMaxInt(UIGenStateEnum, &iUIGenStateMin, &iUIGenStateMax);
	g_GenState.iMaxStates = DefineLoadFromFile(g_ui_pGenExtraStates, "State", "UIGenStates", "ui", "UIGenStates.def", "UIGenStates.bin", iUIGenStateMax+1);
	assertmsgf(g_GenState.iMaxStates < sizeof(pGen->bfStates) * 8, "Gens have space for %d states, but %d are defined.", (U32)(sizeof(pGen->bfStates) * 8), g_GenState.iMaxStates);

	for (i = iUIGenStateMax+1; i < g_GenState.iMaxStates; i++)
	{
		const char *pchName = StaticDefineIntRevLookup(UIGenStateEnum, i);
		if (pchName && strStartsWith(pchName, "UISkin"))
		{
			eaiPush(&g_GenState.eaiSkinStates, i);
			eaPush(&g_GenState.eapchSkinOverrides, pchName + 6);
		}
	}

	ui_GenInitStaticDefineVars(UIGenStateEnum, "");
	ui_GenInitStaticDefineVars(UISizeEnum, "Size");
	ui_GenInitStaticDefineVars(UIUnitTypeEnum, "Unit");
	ui_GenInitStaticDefineVars(UIAngleUnitTypeEnum, "Angle");
	ui_GenInitStaticDefineVars(UISortTypeEnum, "Sort");
	ui_GenInitStaticDefineVars(UIDirectionEnum, "Direction");
	ui_GenInitStaticDefineVars(LanguageEnum, "Language");
	ui_GenAddExprFuncs(UI_GEN_EXPR_TAG);
	//ui_GenAddExprFuncs(INPUT_EXPR_TAG); // now just uses the UIGen tag for these
	//ui_GenAddExprFuncs(MESSAGE_EXPR_TAG); // now just uses the UIGen tag for these
	ui_GenAddExprFuncs("util");
	exprContextSetFuncTable(g_GenState.pContext, g_GenState.stGenFuncTable);

	for (i = eaSize(&g_GenState.eaContexts) - 1; i >= 0; i--)
		exprContextSetFuncTable(g_GenState.eaContexts[i]->pContext, g_GenState.stGenFuncTable);

	resDictManageValidation(g_GenState.hGenDict, GenResValidate);
	if (isDevelopmentMode())
		resDictMaintainInfoIndex(g_GenState.hGenDict, NULL, NULL, NULL, NULL, NULL);
	resDictRegisterEventCallback(g_GenState.hGenDict, GenResEvent, NULL);

	ui_GenJailLoad();
	ui_GenWindowManagerLoad();
	resLoadResourcesFromDisk(UI_GEN_DICTIONARY, "ui", ".uigen", NULL, PARSER_CLIENTSIDE);
	ui_GenTutorialLoad();

	if (g_ui_GenDumpExprFunctions)
	{
		globCmdParse("GenDumpExprFunctions");
		globCmdParse("Quit");
	}
}

UIGen *ui_GenCreateFromString(const char *pchDef)
{
	UIGen *pGen = NULL;

	if(pchDef)
	{
		pGen = StructCreate(parse_UIGen);

		if(pGen)
		{
			bool ret = ParserReadText(pchDef, parse_UIGen, pGen, PARSER_CLIENTSIDE);
			if (ret)
				ret = GenResValidatePostTextReading(pGen);
			if (ret)
				GenResValidatePostBinning(pGen);
			if (ret)
				ret = GenResValidateCheckReferences(pGen);

			if(!ret)
			{
				StructDestroy(parse_UIGen, pGen);
				pGen = NULL;
			}
		}
	}

	return pGen;
}

// Must be done in an AUTO_RUN or this cannot be used as a static check dictionary.
// If expression initialization ever stops being crappy, this should be moved back
// to ui_GenPreLoad.
AUTO_RUN;
void ui_GenRegisterDictionary(void)
{
	S32 i;
	g_GenState.hGenDict = RefSystem_RegisterSelfDefiningDictionary(UI_GEN_DICTIONARY, false, parse_UIGen, true, true, NULL);
	g_ui_pGenExtraStates = DefineCreate();
	g_ui_pGenExtraSizes = DefineCreate();
	g_ui_bGenLayoutOrder = gConf.bUIGenSaneLayoutOrder;

	FORALL_PARSETABLE(polyTable_UIGenInternal, i)
	{
		if (!ParserGetTableFixupFunc(polyTable_UIGenInternal[i].subtable))
			ParserSetTableFixupFunc(polyTable_UIGenInternal[i].subtable, ui_GenInternalParserFixup);
	}

	ui_GenInitStaticDefineVars(UITweenTypeEnum, "TweenType_");

	smf_setImageSkinOverrideCallbackFunc(ui_GenGetTextureSkinOverride);

	g_GenState.fScale = 1.0;
}

static bool ui_GenGetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

static bool ui_GenGetAudioAssets_UIGenAction_GatherSoundStrings(UIGenAction *pAction, UIGen *pGen, const char *pchPathString, char ***peaStrings)
{
	FOR_EACH_IN_EARRAY(pAction->eachSounds, const char, pcSound) {
		eaPush(peaStrings, strdup(pcSound));
	} FOR_EACH_END;
	return true;
}

void ui_GenGetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	UIGen *pUIGen;
	ResourceIterator rI;

	*ppcType = strdup("UIGen");

	resInitIterator(UI_GEN_DICTIONARY, &rI);
	while (resIteratorGetNext(&rI, NULL, &pUIGen))
	{
		char **eaNonConstStrings = NULL;
		bool bResourceHasAudio = false;
		
		ParserScanForSubstruct(parse_UIGen, pUIGen, parse_UIGenAction, 0, 0, ui_GenGetAudioAssets_UIGenAction_GatherSoundStrings, &eaNonConstStrings);
		FOR_EACH_IN_EARRAY(eaNonConstStrings, char, pcString) {
			bResourceHasAudio |= ui_GenGetAudioAssets_HandleString(pcString, peaStrings);
		} FOR_EACH_END;
		eaDestroyEx(&eaNonConstStrings, NULL);

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	resFreeIterator(&rI);
}