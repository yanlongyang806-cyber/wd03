#include "AttribsUI.h"
#include "Entity.h"
#include "gclEntity.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "PowerVars.h"
#include "UIGen.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "StringUtil.h"
#include "rewardCommon.h"

#include "AutoGen/AttribsUI_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// This file contains functions that Neverwinter is using for the character sheet.  They serve the same basic purpose as
// GetAttributeList in CharacterStatus.c, but are much more customization-friendly

Message *AttribGetMessageWithAspect(S32 iAttrib, S32 iAspect)
{
	const char *pchDefineName = FindStaticDefineName(AttribTypeEnum);
	const char *pchAttribKey = StaticDefineInt_FastIntToString(AttribTypeEnum, iAttrib);
	const char *pchAspectKey = StaticDefineInt_FastIntToString(AttribAspectEnum, iAspect);
	Message *pMessage = NULL;
	char achMessageKey[2048];
	if (pchDefineName && pchAttribKey && pchAspectKey)
	{
		sprintf(achMessageKey, "StaticDefine_%s_%s_%s", pchDefineName, pchAttribKey, pchAspectKey);
		pMessage = RefSystem_ReferentFromString(gMessageDict, achMessageKey);
	}
	if (!pMessage && pchDefineName && pchAttribKey)
	{
		sprintf(achMessageKey, "StaticDefine_%s_%s", pchDefineName, pchAttribKey);
		pMessage = RefSystem_ReferentFromString(gMessageDict, achMessageKey);
	}
	return pMessage;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetDisplayName);
const char * AttribGetDisplayName(S32 iAttrib, S32 iAspect)
{
	Message *pMessage = AttribGetMessageWithAspect(iAttrib, iAspect);
	if (pMessage)
	{
		return TranslateMessageKey(pMessage->pcMessageKey);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetBonuses);
void exprAttrib_GetBonuses(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEnt, const char *pchAttribName)
{
	AttribBonusUIElement ***peaNodeList = ui_GenGetManagedListSafe(pGen, AttribBonusUIElement);
	S32 iBonusCount = 0;

	S32 eAttrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttribName);

	if (pEnt && 
		pEnt->pChar &&
		pEnt->pChar->pPowerStatBonusData &&
		eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
	{
		S32 iIndexFound = eaIndexedFindUsingInt(&pEnt->pChar->pPowerStatBonusData->ppBonusList, eAttrib);
		if (iIndexFound >= 0)
		{
			PowerStatBonus *pBonus = pEnt->pChar->pPowerStatBonusData->ppBonusList[iIndexFound];
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pBonus->ppEntries, PowerStatBonusEntry, pEntry)
			{
				AttribBonusUIElement *pUIElement = eaGetStruct(peaNodeList, parse_AttribBonusUIElement, iBonusCount++);
				Message *pMessage = AttribGetMessageWithAspect(pEntry->offTargetAttrib, pEntry->offTargetAspect);
				const char *pchLogicalName = StaticDefineIntRevLookup(AttribTypeEnum, pEntry->offTargetAttrib);
				pUIElement->eAttrib = pEntry->offTargetAttrib;
				pUIElement->eAspect = pEntry->offTargetAspect;
				pUIElement->fBonus = pEntry->fBonus;
				if (pUIElement->eAspect == kAttribAspect_StrMult)
					pUIElement->fBonus = pEntry->fBonus-1.0f;

				if (pMessage)
				{
					const char *pchTranslatedMessage = entTranslateMessage(pEnt, pMessage);
					if (stricmp_safe(pUIElement->pchDisplayName, pchTranslatedMessage) != 0)
					{
						pUIElement->pchDisplayName = StructAllocString(pchTranslatedMessage);
					}					
				}
				else if (pchLogicalName && stricmp_safe(pUIElement->pchDisplayName, pchLogicalName) != 0)
				{
					pUIElement->pchDisplayName = StructAllocString(pchLogicalName);
				}
				else if (pchLogicalName == NULL && pUIElement->pchDisplayName)
				{
					StructFreeString(pUIElement->pchDisplayName);
					pUIElement->pchDisplayName = NULL;
				}				
			}
			FOR_EACH_END
		}
	}

	eaSetSizeStruct(peaNodeList, parse_AttribBonusUIElement, iBonusCount);

	ui_GenSetManagedListSafe(pGen, peaNodeList, AttribBonusUIElement, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetModsFromBuffs);
F32 exprAttrib_GetModsFromBuffs(SA_PARAM_OP_VALID Entity* pEnt, S32 eAttrib, S32 eAspect)
{
	F32 fModTotal = 0.f;

	if (eAspect == kAttribAspect_StrMult)
		fModTotal = 1.0f;

	if (pEnt && pEnt->pChar && eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->ppModsNet, AttribModNet, pModNet)
		{
			AttribModDef *pModDef;
			PowerDef *pDef;

			if(!ATTRIBMODNET_VALID(pModNet))
				continue;

			pDef = GET_REF(pModNet->hPowerDef);
			pModDef = pDef ? eaGet(&pDef->ppOrderedMods,pModNet->uiDefIdx) : NULL;

			if (pModDef && 
				pModDef->offAttrib == eAttrib &&
				pModDef->offAspect == eAspect)
			{
				if (eAspect == kAttribAspect_StrMult)
					fModTotal *= (pModNet->iMagnitude/ATTRIBMODNET_MAGSCALE);
				else
					fModTotal += (pModNet->iMagnitude/ATTRIBMODNET_MAGSCALE);
			}
		}
		FOR_EACH_END
	}

	if (eAspect == kAttribAspect_StrMult)
		fModTotal -= 1.0f;

	return fModTotal;
}

static F32 attribsUI_GetInnateModsBySource(SA_PARAM_OP_VALID Entity *pEnt, AttribType eAttrib, AttribAspect eAspect, InnateAttribModSource eSource)
{
	F32 fModTotal = 0.f;
	int i;

	if (eAspect == kAttribAspect_StrMult)
		fModTotal = 1.0f;

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->pInnateAttribModData->ppInnateAttribMods, InnateAttribMod, pMod)
	{
		if (pMod->eSource == eSource &&
			pMod->eAttrib == eAttrib &&
			pMod->eAspect == eAspect)
		{
			PowerDef* pPowerDef = GET_REF(pMod->hPowerDef);
			bool bExclude = false;
			if(pPowerDef)
			{
				for(i=0; i<eaiSize(&pPowerDef->piCategories); i++)
				{
					if (g_PowerCategories.ppCategories[pPowerDef->piCategories[i]]->bDisplayAttribModsAsBaseStat)
					{
						bExclude = true;
						break;
					}
				}
			}
			if(!bExclude)
			{
				if (eAspect == kAttribAspect_StrMult)
					fModTotal *= pMod->fMag;
				else
					fModTotal += pMod->fMag;
			}
		}
	}
	FOR_EACH_END

	if (eAspect == kAttribAspect_StrMult)
		fModTotal -= 1.0f;

	return fModTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetModsFromPowers);
F32 exprAttrib_GetModsFromPowers(SA_PARAM_OP_VALID Entity* pEnt, S32 eAttrib, S32 eAspect)
{
	if (pEnt && 
		pEnt->pChar && 
		pEnt->pChar->pInnateAttribModData &&
		eAttrib >= 0)
	{
		return attribsUI_GetInnateModsBySource(pEnt, eAttrib, eAspect, InnateAttribModSource_Power);
	}

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetModsFromStatPoints);
F32 exprAttrib_GetModsFromStatPoints(SA_PARAM_OP_VALID Entity* pEnt, S32 eAttrib, S32 eAspect)
{
	if (pEnt && 
		pEnt->pChar && 
		pEnt->pChar->pInnateAttribModData &&
		eAttrib >= 0)
	{
		// Only handle basic absolute aspect for now
		return attribsUI_GetInnateModsBySource(pEnt, eAttrib, eAspect, InnateAttribModSource_StatPoint);
	}

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetModsFromItems);
F32 exprAttrib_GetModsFromItems(SA_PARAM_OP_VALID Entity* pEnt, S32 eAttrib, S32 eAspect)
{
	if (pEnt && 
		pEnt->pChar && 
		pEnt->pChar->pInnateAttribModData &&
		eAttrib >= 0)
	{
		return attribsUI_GetInnateModsBySource(pEnt, eAttrib, eAspect, InnateAttribModSource_Item);
	}

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetModsFromAttribs);
F32 exprAttrib_GetModsFromAttribs(SA_PARAM_OP_VALID Entity* pEnt, S32 eAttrib, S32 eAspect)
{
	F32 fModTotal = 0.f;
	if (eAspect == kAttribAspect_StrMult)
		fModTotal = 1.0f;

	if (pEnt && 
		pEnt->pChar &&
		pEnt->pChar->pPowerStatBonusData &&
		eAttrib >= 0)
	{
		// If this look up turns out to be a performance bottle neck, it can be avoided if we create a reverse lookup data for power stat bonuses.		
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->pPowerStatBonusData->ppBonusList, PowerStatBonus, pPowerStatBonus)
		{
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pPowerStatBonus->ppEntries, PowerStatBonusEntry, pBonusEntry)
			{
				if (pBonusEntry->offTargetAspect == eAspect &&
					pBonusEntry->offTargetAttrib == eAttrib)
				{
					if (eAspect == kAttribAspect_StrMult)
						fModTotal *= pBonusEntry->fBonus;
					else
						fModTotal += pBonusEntry->fBonus;
				}
			}
			FOR_EACH_END
		}
		FOR_EACH_END
	}
		
	if (eAspect == kAttribAspect_StrMult)
		fModTotal -= 1.0f;

	return fModTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Attrib_GetRewardModify);
F32 exprAttrib_GetRewardModify(SA_PARAM_OP_VALID Entity* pEnt, char * pchNumeric)
{
	if (pEnt && pEnt->pPlayer)
	{
		RewardModifier *pModifier = eaIndexedGetUsingString(&pEnt->pPlayer->eaRewardMods, pchNumeric);
		if(pModifier)
			return pModifier->fFactor-1.0f;
	}
	return 0.0;
}

#include "AutoGen/AttribsUI_h_ast.c"