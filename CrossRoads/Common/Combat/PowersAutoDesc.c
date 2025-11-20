/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowersAutoDesc.h"
#include "PowersAutoDesc_h_ast.h"

#include "BlockEarray.h"
#include "entity.h"
#include "estring.h"
#include "file.h"
#include "fileutil.h"
#include "GameStringFormat.h"
#include "logging.h"
#include "MemoryPool.h"
#include "Player.h"
#include "winutil.h" // For time stuff


#include "AttribMod.h"
#include "AttribMod_h_ast.h"
#include "conversions.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "CharacterAttribs_h_ast.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "DamageTracker.h"
#include "entCritter.h"
#include "Powers_h_ast.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerApplication.h"
#include "PowerEnhancements.h"
#include "PowersEnums_h_ast.h"
#include "PowerModes.h"
#include "PowerTree.h"
#include "PowerVars.h"
#include "RegionRules.h"
#include "Character_combat.h"
#include "Combat_DD.h"
#include "AbilityScores_DD.h"
#include "LoginCommon.h"
#include "Login2Common.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "StringUtil.h"

// I am the devil
#include "ExpressionPrivate.h"

#ifdef GAMECLIENT
#include "gclEntity.h"
#include "gclUIGen.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern Login2CharacterCreationData *g_CharacterCreationData;

MP_DEFINE(AutoDescPower);
MP_DEFINE(AutoDescAttribMod);

AUTO_RUN;
void InitPowersAutoDesc(void)
{
	MP_CREATE(AutoDescPower,5);
	MP_CREATE(AutoDescAttribMod,10);
}

static AutoDescConfig s_AutoDescConfig;

typedef struct AutoDescModScale
{
	AttribModDef *pmodDef;
	S32 iLevel;
	F32 fScale;
	F32 fMag;
	S32 bProcessed;
	S32 iRequiredGemSlot;
} AutoDescModScale;

#define ASPECT_PERCENT(aspect) ((aspect)!=kAttribAspect_BasicAbs && (aspect)!=kAttribAspect_StrAdd && (aspect)!=kAttribAspect_Immunity)
#define ASPECT_NEGATIVE_PERCENT(aspect) ((aspect)==kAttribAspect_StrFactNeg || (aspect)==kAttribAspect_BasicFactNeg)
#define ATTRIB_ASPECT_PERCENT(attrib,aspect) ((aspect)==kAttribAspect_BasicAbs && ((attrib)==kAttribType_CritChance || (attrib)==kAttribType_Dodge || (attrib)==kAttribType_Avoidance || (attrib)==kAttribType_AttribModFragilityScale || (attrib)==kAttribType_AttribModShieldPercentIgnored || -1!=eaiFind(&s_AutoDescConfig.piPercentAttribs,(attrib))))
#define ATTRIB_ASPECT_SCALED(attrib,aspect) ((aspect)==kAttribAspect_BasicAbs && -1!=eaiFind(&s_AutoDescConfig.piScaledAttribs,(attrib)))
#define ATTRIB_ASPECT_PERIODIC_NONCOMPRESSED(attrib,aspect) ((aspect)==kAttribAspect_BasicAbs && (attrib)==kAttribType_ApplyPower)
#define ATTRIB_ASPECT_FLOOR(attrib,aspect) ((aspect)==kAttribAspect_BasicAbs && ((attrib)>=kAttribType_StatDamage && (attrib)<=kAttribType_StatRecovery))
#define ATTRIB_ASPECT_UNINTEGRATED(attrib,aspect) (IS_BASIC_ASPECT(aspect) && ((attrib)==kAttribType_CritChance || (attrib)==kAttribType_CritSeverity || ATTRIB_DATADEFINED(attrib)))
#define ATTRIB_MAXIMUM_DETAIL_ONLY(attrib) ((attrib)==kAttribType_Null || (attrib)==kAttribType_SetCostume || (attrib)==kAttribType_ModifyCostume)
#define ATTRIB_SPECIAL_AS_NORMAL(attrib) ((attrib)==kAttribType_AttribModShieldPercentIgnored)

// Forward declaration of internal functions

static void AutoDescPowerDefInternal(int iPartitionIdx,
									 SA_PARAM_NN_VALID PowerDef *pdef,
									 SA_PARAM_OP_VALID char **ppchDesc,
									 SA_PARAM_OP_VALID AutoDescPower *pAutoDescPower,
									 SA_PARAM_OP_VALID const char *cpchLine,
									 SA_PARAM_OP_VALID const char *cpchModIndent,
									 SA_PARAM_OP_VALID const char *cpchModPrefix,
									 AutoDescPowerHeader eHeader,
									 SA_PARAM_OP_VALID Character *pchar,
									 SA_PARAM_OP_VALID Power *ppow,
									 SA_PARAM_OP_VALID Power **ppEnhancements,
									 int iLevel,
									 int bIncludeStrength,
									 int bFullDescription,
									 int iDepth,
									 AutoDescDetail eDetail,
									 SA_PARAM_OP_VALID AutoDescAttribMod *pAutoDescAttribModEvent,
									 SA_PARAM_OP_VALID PowerDef ***pppPowerDefsDescribed,
									 SA_PARAM_OP_VALID const char ***pppchFootnoteMsgKeys,
									 GameAccountDataExtract *pExtract,
									 SA_PARAM_OP_STR const char *pchPowerAutoDescMessageKey);

static void AutoDescAttribModDef(int iPartitionIdx,
								 SA_PARAM_NN_VALID AttribModDef *pdef,
								 SA_PARAM_NN_VALID AutoDescAttribMod *pAutoDescAttribMod,
								 Language lang,
								 SA_PARAM_NN_VALID PowerDef *ppowdef,
								 SA_PARAM_OP_VALID Character *pchar,
								 SA_PARAM_OP_VALID Power *ppow,
								 SA_PARAM_OP_VALID Power **ppEnhancements,
								 int iLevel,
								 int bIncludeStrength,
								 int bFullDescription,
								 int bModsTargetMixed,
								 int iDepth,
								 int iCostPaidUnroll,
								 F32 fPowerAppsPerSecond,
								 AutoDescPowerHeader eHeader,
								 AutoDescDetail eDetail,
								 SA_PARAM_OP_VALID AutoDescAttribMod *pAutoDescAttribModEvent,
								 SA_PARAM_OP_VALID PowerDef ***pppPowerDefsDescribed,
								 SA_PARAM_OP_VALID const char ***pppchFootnoteMsgKeys,
								 GameAccountDataExtract *pExtract);

static void AutoDescAttribModDefInnate(int iPartitionIdx,
									   SA_PARAM_NN_VALID AttribModDef *pdef,
									   SA_PARAM_OP_VALID char **ppchDesc,
									   SA_PARAM_OP_VALID AutoDescAttribMod *pAutoDescAttribMod,
									   SA_PARAM_OP_VALID AutoDescInnateModDetails *pDetails,
									   F32 fMagnitude,
									   Language lang,
									   AutoDescDetail eDetail,
									   SA_PARAM_OP_VALID Message* pCustomMsg);


// Simple function to find the entity's desired Power AutoDesc detail.  Returns Normal if it can't find a preference.
int entGetPowerAutoDescDetail(Entity *pent, S32 bTooltip)
{
	AutoDescDetail eDetail = kAutoDescDetail_Normal;
	if(pent && pent->pPlayer && pent->pPlayer->pUI)
	{
		eDetail = bTooltip ? pent->pPlayer->pUI->ePowerTooltipDetail : pent->pPlayer->pUI->ePowerInspectDetail;
	}
	return eDetail;
}

__forceinline static F32 AutoDescRound(F32 f)
{
	if(f!=0.f && f!=POWERS_FOREVER)			// Don't try to round POWERS_FOREVER (FLT_MAX)
	{
		F32 fAbs = fabs(f);
		if(fAbs>=10.f&&!gConf.bAutoDescRoundToTenths)
		{
			return round(f);			// Round to integer
		}
		if(fAbs>=1.f)
		{
			return round(f*10.f)/10.f;	// Round to 1/10ths
		}
	}
	return f;							// Formatting automatically rounds to 1/100ths
}

__forceinline static F32 AutoDescFloor(F32 f)
{
	if(f!=0)
	{
		F32 fAbs = fabs(f);
		
		if(fAbs<1)
		{
			return (f < 0) ? -1 : 1;
		}

		return floorf(f);
	}
	return f;
}


static int SortAttribModDefsAutoDesc(const AttribModDef **ppModDefA, const AttribModDef **ppModDefB)
{
	int r = 0;
	
	const AttribModDef *pModDefA = (*ppModDefA);
	const AttribModDef *pModDefB = (*ppModDefB);

	int rAspect = pModDefA->offAspect - pModDefB->offAspect;

	// Sort by delay (lower delay first)
	if(!r)
	{
		F32 fDelay = pModDefA->fDelay - pModDefB->fDelay;
		r = fDelay > 0 ? 1 : (fDelay < 0 ? -1 : 0);
	}

	// Sort by existence of duration expression and period (first instants, then periodics, then permanents)
	if(!r)
	{
		r = pModDefA->pExprDuration ? (pModDefB->pExprDuration ? 0 : 1) : (pModDefB->pExprDuration ? -1 : 0);

		// If both have duration, Sort by existence of period
		if(!r && pModDefA->pExprDuration)
			r = pModDefA->fPeriod ? (pModDefB->fPeriod ? 0 : -1) : (pModDefB->fPeriod ? 1 : 0);
	}

	// Sort by attribute
	if(!r)
	{
		int offAttribA = pModDefA->offAttrib;
		int offAttribB = pModDefB->offAttrib;
		int rAttrib;

		if(offAttribA==kAttribType_AttribOverride)
		{
			AttribOverrideParams *pParams = (AttribOverrideParams*)pModDefA->pParams;
			if(pParams)
			{
				offAttribA = pParams->offAttrib;
			}
		}

		if(offAttribB==kAttribType_AttribOverride)
		{
			AttribOverrideParams *pParams = (AttribOverrideParams*)pModDefB->pParams;
			if(pParams)
			{
				offAttribB = pParams->offAttrib;
			}
		}

		rAttrib = offAttribA - offAttribB;

		if(rAttrib)
		{
			AttribType *pattribA = attrib_Unroll(offAttribA);
			AttribType *pattribB = attrib_Unroll(offAttribB);

			if(!pattribA && !pattribB)
			{
				// Neither is a set
				r = rAttrib;

				if(IS_SPECIAL_ATTRIB(offAttribA) && IS_SPECIAL_ATTRIB(offAttribB) && (offAttribA==kAttribType_Shield || offAttribB==kAttribType_Shield))
				{
					// They're both special, and one is a Shield, put the Shield first
					r = offAttribA==kAttribType_Shield ? -1 : 1;
				}
			}
			else if(pattribA && pattribB)
			{
				// Both are sets, compare the first element
				r = pattribA[0] - pattribB[0];

				if(!r)
				{
					// First element is identical, compare the set size
					r = eaiSize(&pattribB) - eaiSize(&pattribA);
				}
			}
			else if(pattribA)
			{
				// A is a set, but not B
				if(-1!=eaiFind(&pattribA,offAttribB))
				{
					// B is in A, so use rAspect or A goes first
					r = rAspect ? rAspect : -1;
				}
				else
				{
					// B isn't in A, compare the first element in A
					r = pattribA[0] - offAttribB;
				}
			}
			else
			{
				// B is a set, but not A
				if(-1!=eaiFind(&pattribB,offAttribA))
				{
					// A is in B, so use rAspect or B goes first
					r = rAspect ? rAspect : 1;
				}
				else
				{
					// A isn't in B, compare the first element in B
					r = offAttribA - pattribB[0];
				}
			}
		}
	}

	// Sort by aspect
	if(!r)
	{
		r = rAspect;
	}

	// Sort by existence of a chance expression
	if(!r)
	{
		r = pModDefA->pExprChance ? (pModDefB->pExprChance ? 0 : 1) : (pModDefB->pExprChance ? -1 : 0);
	}

	// Sort by existence of requires expression
	if(!r)
	{
		r = pModDefA->pExprRequires ? (pModDefB->pExprRequires ? 0 : 1) : (pModDefB->pExprRequires ? -1 : 0);
	}

	// Last resort - sort by uiDefIdx
	if(!r)
	{
		r = pModDefA->uiDefIdx > pModDefB->uiDefIdx ? 1 : (pModDefA->uiDefIdx < pModDefB->uiDefIdx ? -1 : 0);
	}

	return r;
}

static int SortAttribModScaleAutoDesc(const AutoDescModScale **ppModScaleA, const AutoDescModScale **ppModScaleB)
{
	const AttribModDef *pModDefA = (*ppModScaleA)->pmodDef;
	const AttribModDef *pModDefB = (*ppModScaleB)->pmodDef;
	return(SortAttribModDefsAutoDesc(&pModDefA, &pModDefB));
}

static void AutoDescTime(F32 fSeconds, char **ppchDesc, AutoDescDetail eDetail, Language lang)
{
	if(fSeconds < 60.f)
	{
		if(eDetail < kAutoDescDetail_Maximum)
		{
			fSeconds = AutoDescRound(fSeconds);
		}
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.Common.TimeSeconds",STRFMT_FLOAT("Sec",fSeconds),STRFMT_END);
	}
	else
	{
		F32 fMinutes = fSeconds / 60.f;
		S32 iMinutes = floor(fMinutes);
		if(iMinutes == fMinutes)
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.Common.TimeMinutes",STRFMT_INT("Min",iMinutes),STRFMT_END);
		}
		else
		{
			fSeconds -= (60.f * iMinutes);
			if(eDetail < kAutoDescDetail_Maximum)
			{
				fSeconds = AutoDescRound(fSeconds);
			}
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.Common.TimeMinutesSeconds",STRFMT_INT("Min",iMinutes),STRFMT_FLOAT("Sec",fSeconds),STRFMT_END);
		}
	}
}

#define AUTODESCATTRIBNAME "AutoDesc.AttribName."
#define AUTODESCATTRIBDESC "AutoDesc.AttribDesc."
#define AUTODESCATTRIBDESCLONG "AutoDesc.AttribDescLong."

// Automatically finds a key of the format prefix+attrib and translates that, otherwise
//  returns attrib
static const char *AutoDescAttribConst(const char *pchPrefix, const char *pchAttrib, Language lang)
{
	char *pchTemp = NULL;

	estrStackCreate(&pchTemp);
	estrCopy2(&pchTemp,pchPrefix);
	estrAppend2(&pchTemp,pchAttrib);

	if(RefSystem_ReferentFromString(gMessageDict,pchTemp))
	{
		const char *pchReturn = langTranslateMessageKey(lang,pchTemp);
		estrDestroy(&pchTemp);
		return pchReturn;
	}
	else
	{
		estrDestroy(&pchTemp);
		return pchAttrib;
	}
}

// For internal use, please do not expose.  If you need the name/desc/desclong outside this file
//   please use attrib_AutoDescFoo
static void AutoDescAttribName(AttribType offAttrib, char **ppchDesc, Language lang)
{
	const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum, offAttrib);
	const char *pchAttribResult = AutoDescAttribConst(AUTODESCATTRIBNAME, pchAttribBase, lang);
	estrCopy2(ppchDesc,pchAttribResult);
}

static void AutoDescAttribDesc(AttribType offAttrib, char **ppchDesc, Language lang)
{
	const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum, offAttrib);
	const char *pchAttribResult = AutoDescAttribConst(AUTODESCATTRIBDESC, pchAttribBase, lang);
	estrCopy2(ppchDesc,pchAttribResult);
}

static void AutoDescAttribDescLong(AttribType offAttrib, char **ppchDesc, Language lang)
{
	const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum, offAttrib);
	const char *pchAttribResult = AutoDescAttribConst(AUTODESCATTRIBDESCLONG, pchAttribBase, lang);
	estrCopy2(ppchDesc,pchAttribResult);
}

// Returns the translated name, desc or desclong for the Attrib, or if that doesn't exist, returns the internal name
const char *attrib_AutoDescName(AttribType eAttrib, Language lang)
{
	const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum, eAttrib);
	return AutoDescAttribConst(AUTODESCATTRIBNAME, pchAttribBase, lang);
}

const char *attrib_AutoDescDesc(AttribType eAttrib, Language lang)
{
	const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum, eAttrib);
	return AutoDescAttribConst(AUTODESCATTRIBDESC, pchAttribBase, lang);
}

const char *attrib_AutoDescDescLong(AttribType eAttrib, Language lang)
{
	const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum, eAttrib);
	return AutoDescAttribConst(AUTODESCATTRIBDESCLONG, pchAttribBase, lang);
}

// Returns if the Attrib is a percent as far as AutoDesc is concerned
S32 attrib_AutoDescIsPercent(AttribType eAttrib)
{
	return ATTRIB_ASPECT_PERCENT(eAttrib,kAttribAspect_BasicAbs);
}



static void AutoDescPowerComboRequires(PowerDef *pdef,
									   char **ppchDesc,
									   Language lang,
									   int iCombo)
{
	PowerCombo *pcombo = pdef->ppOrderedCombos[iCombo];
	if(pcombo->pExprRequires)
	{
		char *pchRequirements = NULL;
		static MultiVal **s_ppStack = NULL;
		Expression *pexpr = pcombo->pExprRequires;
		int i,s=beaSize(&pexpr->postfixEArray);

		eaClear(&s_ppStack);
		estrStackCreate(&pchRequirements);

		for(i=0; i<s; i++)
		{
 			MultiVal *pVal = &pexpr->postfixEArray[i];
 			if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
 				continue;
 			if(pVal->type==MULTIOP_FUNCTIONCALL)
 			{
				const char *pchFunction = pVal->str;
				if(!stricmp(pchFunction,"Combo"))
				{
					MultiVal *pValComboPower = eaPop(&s_ppStack);
					if(pValComboPower)
					{
						const char *pchComboPower = MultiValGetString(pValComboPower,NULL);
						PowerDef *pdefCombo = pchComboPower ? powerdef_Find(pchComboPower) : NULL;
						if(pdefCombo)
						{
							langFormatGameMessageKey(lang,&pchRequirements,"AutoDesc.PowerDef.ComboRequires.Combo",STRFMT_POWERDEF_KEY("Power", pdefCombo),STRFMT_END);
							continue;
						}
					}
				}
				else if(!stricmp(pchFunction,"Falling"))
				{
					FormatMessageKey(&pchRequirements,"AutoDesc.PowerDef.ComboRequires.Falling",STRFMT_END);
					continue;
				}
				else if(!stricmp(pchFunction,"OwnsPower"))
				{
					MultiVal *pValOwnsPower = eaPop(&s_ppStack);
					MultiVal *pValEntity = eaPop(&s_ppStack);
					if(pValEntity && pValOwnsPower)
					{
						const char *pchOwnsPower = MultiValGetString(pValOwnsPower,NULL);
						PowerDef *pdefOwns = pchOwnsPower ? powerdef_Find(pchOwnsPower) : NULL;
						if(pdefOwns)
						{
							langFormatGameMessageKey(lang,&pchRequirements,"AutoDesc.PowerDef.ComboRequires.OwnsPower",STRFMT_POWERDEF_KEY("Power", pdefOwns),STRFMT_END);
							continue;
						}
					}
				}
				else if(!stricmp(pchFunction,"TargetIsType"))
				{
					MultiVal *pValTarget = eaPop(&s_ppStack);
					MultiVal *pValEntityTarget = eaPop(&s_ppStack);
					MultiVal *pValEntity = eaPop(&s_ppStack);
					if(pValEntity
						&& pValEntityTarget
						&& pValTarget)
					{
						const char *pchTarget = MultiValGetString(pValTarget,NULL);
						PowerTarget *pTarget = pchTarget ? RefSystem_ReferentFromString(g_hPowerTargetDict,pchTarget) : NULL;
						if(pTarget)
						{
							langFormatMessageKey(lang,&pchRequirements,"AutoDesc.PowerDef.ComboRequires.TargetIsType",STRFMT_MESSAGEREF("Target",pTarget->hMsgDescription),STRFMT_END);
							continue;
						}
					}
				}
				else
				{
					// Didn't match with a function we know how to handle
				}
 			}
 			eaPush(&s_ppStack,pVal);
		}

		if(eaSize(&s_ppStack) > 0)
		{
			langFormatMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.ComboRequiresUnknown",STRFMT_END);
		}
		else
		{
			langFormatMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.ComboRequires",STRFMT_STRING("Requirements",pchRequirements),STRFMT_END);
		}
		estrDestroy(&pchRequirements);
	}
}

static void AutoDescAttributesAffectingPower_ParseExpr(Expression* pExpr, Language lang, 
													   U32** peaAttribs,
													   S32 bIsPet)
{
	static MultiVal **s_ppStack = NULL;
	int i,s=beaSize(&pExpr->postfixEArray);
	eaClear(&s_ppStack);
	for(i=0; i<s; i++)
	{
		MultiVal *pVal = &pExpr->postfixEArray[i];
		if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
			continue;
		if(pVal->type==MULTIOP_FUNCTIONCALL)
		{
			const char *pchFunction = pVal->str;
			if(!stricmp(pchFunction,"Attrib"))
			{
				MultiVal* pAttribVal = eaPop(&s_ppStack);
				MultiVal* pSourceVal = eaPop(&s_ppStack);
				bool bValid = !!pAttribVal;

				if (bValid && bIsPet)
				{
					bValid = false;
					if (pSourceVal && pSourceVal->type==MULTIOP_FUNCTIONCALL)
					{
						if(!stricmp(pSourceVal->str,"OwnerChar"))
						{
							pSourceVal = eaPop(&s_ppStack);
							bValid = true;
						}
					}
				}

				if(bValid)
				{
					const char *pchAttrib = MultiValGetString(pAttribVal,NULL);
					AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttrib);

					if (eAttrib >= 0)
					{
						ea32PushUnique(peaAttribs, eAttrib);
					}
				}
			}
		}
		eaPush(&s_ppStack,pVal);
	}
}

static int GetAttribsAffectingPower_Recurse(SA_PARAM_NN_VALID PowerDef *pdef, 
											Language lang, 
											U32** peaAttribs,
											PowerDef*** pppVisitedDefs,
											S32 bIsPet)
{
	int i, j;
	if (eaFind(pppVisitedDefs, pdef) >= 0)
	{
		return 0;
	}
	eaPush(pppVisitedDefs, pdef);
	for (i = 0; i < eaSize(&pdef->ppOrderedMods); i++)
	{
		AttribModDef* pAttrDef = pdef->ppOrderedMods[i];
		if (pAttrDef->pExpiration)
		{
			PowerDef* pExprPowDef = GET_REF(pAttrDef->pExpiration->hDef);
			if (pExprPowDef)
			{
				GetAttribsAffectingPower_Recurse(pExprPowDef, lang, peaAttribs, pppVisitedDefs, bIsPet);
			}
		}
		if (pAttrDef->pExprMagnitude)
		{
			AutoDescAttributesAffectingPower_ParseExpr(pAttrDef->pExprMagnitude, lang, peaAttribs, bIsPet);
		}
		if (pAttrDef->pExprDuration)
		{
			AutoDescAttributesAffectingPower_ParseExpr(pAttrDef->pExprDuration, lang, peaAttribs, bIsPet);
		}
		if (pAttrDef->offAttrib==kAttribType_EntCreate)
		{
			EntCreateParams* pParams = (EntCreateParams*)pAttrDef->pParams;
			CritterDef* pCritterDef = GET_REF(pParams->hCritter);
			if (pCritterDef)
			{
				for (j = 0; j < eaSize(&pCritterDef->ppPowerConfigs); j++)
				{
					CritterPowerConfig* pPowerConfig = pCritterDef->ppPowerConfigs[j];
					PowerDef* pCritterPowerDef = GET_REF(pPowerConfig->hPower);
					if (pCritterPowerDef)
					{
						GetAttribsAffectingPower_Recurse(pCritterPowerDef, lang, peaAttribs, pppVisitedDefs, true);
					}
				}
			}
		}
		else if (pAttrDef->offAttrib==kAttribType_GrantPower)
		{
			GrantPowerParams* pParams = (GrantPowerParams*)pAttrDef->pParams;
			PowerDef* pGrantPowerDef = GET_REF(pParams->hDef);
			if (pGrantPowerDef)
			{
				GetAttribsAffectingPower_Recurse(pGrantPowerDef, lang, peaAttribs, pppVisitedDefs, bIsPet);
			}
		}
		else if (pAttrDef->offAttrib==kAttribType_ApplyPower)
		{
			ApplyPowerParams* pParams = (ApplyPowerParams*)pAttrDef->pParams;
			PowerDef* pApplyPowerDef = GET_REF(pParams->hDef);
			if (pApplyPowerDef)
			{
				GetAttribsAffectingPower_Recurse(pApplyPowerDef, lang, peaAttribs, pppVisitedDefs, bIsPet);
			}
		}
		else if (pAttrDef->offAttrib==kAttribType_DamageTrigger)
		{
			DamageTriggerParams* pParams = (DamageTriggerParams*)pAttrDef->pParams;
			PowerDef* pDamageTriggerPowerDef = GET_REF(pParams->hDef);
			if (pDamageTriggerPowerDef)
			{
				GetAttribsAffectingPower_Recurse(pDamageTriggerPowerDef, lang, peaAttribs, pppVisitedDefs, bIsPet);
			}
		}
	}

	if (pdef && pdef->pExprRadius)
	{
		AutoDescAttributesAffectingPower_ParseExpr(pdef->pExprRadius, lang, peaAttribs, bIsPet);
	}
	return ea32Size(peaAttribs);
}

int GetAttributesAffectingPower(SA_PARAM_NN_VALID PowerDef *pdef, Language lang, U32** peaAttribs)
{
	static PowerDef** s_eaVisitedPowerDefs = NULL;
	eaClearFast(&s_eaVisitedPowerDefs);
	return GetAttribsAffectingPower_Recurse(pdef, lang, peaAttribs, &s_eaVisitedPowerDefs, false);
}

static void AutoDescAttributesAffectingPower(PowerDef *pdef, char **ppchDesc, Language lang)
{
	int i;
	U32* eaAttribs = NULL;
	char* pchAutoDesc = NULL;
	bool firstAttrib = true;

	if (IS_HANDLE_ACTIVE(pdef->msgAttribOverride.hMessage))
	{
		estrAppend2(ppchDesc, langTranslateDisplayMessage(lang,pdef->msgAttribOverride));
		estrAppend2(ppchDesc, "<br>");
		return;
	}

	estrStackCreate(&pchAutoDesc);
	GetAttributesAffectingPower(pdef, lang, &eaAttribs);

	if (ea32Size(&eaAttribs))
	{
		estrAppend2(&pchAutoDesc,langTranslateMessageKey(lang,"AutoDesc.PowerDef.AttributesAffectingPower"));
		estrAppend2(&pchAutoDesc, "<br>");
	}
	for (i = 0; i < ea32Size(&eaAttribs); i++)
	{
		const char* pchAttrib = StaticDefineIntRevLookup(AttribTypeEnum, eaAttribs[i]);
		if (strnicmp(pchAttrib,"Stat",4)==0)
		{
			estrAppend2(&pchAutoDesc, "&nbsp;");
			estrAppend2(&pchAutoDesc, AutoDescAttribConst(AUTODESCATTRIBNAME,pchAttrib,lang));
			estrAppend2(&pchAutoDesc, "<br>");
			firstAttrib = false;
		}
	}
	if (ea32Size(&eaAttribs) && firstAttrib)
	{
		estrAppend2(&pchAutoDesc,langTranslateMessageKey(lang,"AutoDesc.PowerDef.AttributesAffectingPower.None"));
		estrAppend2(&pchAutoDesc, "<br>");
	}

	estrCopy2(ppchDesc, pchAutoDesc);
	estrDestroy(&pchAutoDesc);
	ea32Destroy(&eaAttribs);
}

static void LineFeed(char **ppchDesc, const char *cpchLine, const char *cpchIndent, int iDepth)
{
	int i;
	estrAppend2(ppchDesc,cpchLine);
	for(i=0; i<iDepth; i++)
	{
		estrAppend2(ppchDesc,cpchIndent);
	}
}

void AutoDescPowerDefault(AutoDescPower *pAutoDescPower,
						  char **ppchDesc,
						  const char *cpchLine,
						  const char *cpchModIndent,
						  const char *cpchModPrefix,
						  int iDepth,
						  Language lang);

// Appends the default format for a complete AutoDescAttribMod to the estring
static void AutoDescAttribModDefault(AutoDescAttribMod *pAutoDescAttribMod,
									 char **ppchDesc,
									 const char *cpchLine,
									 const char *cpchModIndent,
									 const char *cpchModPrefix,
									 int iDepth,
									 Language lang)
{
	int i,s;

	LineFeed(ppchDesc,cpchLine,cpchModIndent,iDepth+1);

	estrAppend2(ppchDesc,cpchModPrefix);

	if(pAutoDescAttribMod->pchCustom)
	{
		estrAppend2(ppchDesc,pAutoDescAttribMod->pchCustom);
		return;
	}

	if(pAutoDescAttribMod->pchDefault)
	{
		estrAppend2(ppchDesc,pAutoDescAttribMod->pchDefault);
	}
	else
	{
		langFormatMessageKey(lang,ppchDesc,"AutoDesc.AttribModDef.Complete",
			STRFMT_STRING("Dev",pAutoDescAttribMod->pchDev),
			STRFMT_STRING("Target",pAutoDescAttribMod->pchTarget),
			STRFMT_STRING("Requires",pAutoDescAttribMod->pchRequires),
			STRFMT_STRING("Affects",pAutoDescAttribMod->pchAffects),
			STRFMT_STRING("Chance",pAutoDescAttribMod->pchChance),
			STRFMT_STRING("Delay",pAutoDescAttribMod->pchDelay),
			STRFMT_STRING("Description",pAutoDescAttribMod->pchEffect),
			STRFMT_STRING("Period",pAutoDescAttribMod->pchPeriod),
			STRFMT_STRING("Duration",pAutoDescAttribMod->pchDuration),
			STRFMT_END);
	}

	s = eaSize(&pAutoDescAttribMod->ppPowersInline);
	for(i=0; i<s; i++)
	{
		AutoDescPower *pAutoDescPower = pAutoDescAttribMod->ppPowersInline[i];
		LineFeed(ppchDesc,cpchLine,cpchModIndent,0);
		if(s>1)
			LineFeed(ppchDesc,cpchLine,cpchModIndent,0);

		AutoDescPowerDefault(pAutoDescPower,ppchDesc,cpchLine,cpchModIndent,cpchModPrefix,iDepth+2,lang);
	}

	if(pAutoDescAttribMod->pchExpire)
	{
		LineFeed(ppchDesc,cpchLine,cpchModIndent,iDepth+2);
		estrAppend2(ppchDesc,cpchModPrefix);
		estrAppend(ppchDesc,&pAutoDescAttribMod->pchExpire);
	}

	if(pAutoDescAttribMod->pPowerExpire)
	{
		LineFeed(ppchDesc,cpchLine,cpchModIndent,0);
		AutoDescPowerDefault(pAutoDescAttribMod->pPowerExpire,ppchDesc,cpchLine,cpchModIndent,cpchModPrefix,iDepth+3,lang);
	}
}

// Appends the default format for a complete AutoDescPower to the estring
void AutoDescPowerDefault(AutoDescPower *pAutoDescPower,
								 char **ppchDesc,
								 const char *cpchLine,
								 const char *cpchModIndent,
								 const char *cpchModPrefix,
								 int iDepth,
								 Language lang)
{
	S32 i,s;
	S32 bHeaderShown = false;

#define AUTODESCPOWER_ADDLINE(line) if((line) && *(line)) { LineFeed(ppchDesc,bHeaderShown?cpchLine:NULL,cpchModIndent,iDepth); estrAppend(ppchDesc,&line); bHeaderShown = true; }
#define AUTODESCPOWER_ADDLINE2(line) if((line) && *(line)) { LineFeed(ppchDesc,bHeaderShown?cpchLine:NULL,cpchModIndent,iDepth); estrAppend2(ppchDesc,line); bHeaderShown = true; }

	AUTODESCPOWER_ADDLINE2(pAutoDescPower->pchName);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchDev);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchComboRequires);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchComboCharge);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchType);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchCost);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchCostPeriodic);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchEnhanceAttach);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchEnhanceAttached);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchEnhanceApply);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchTarget);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchTargetArc);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchRange);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchTimeCharge);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchTimeActivate);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchTimeActivatePeriod);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchTimeMaintain);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchTimeRecharge);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchCharges);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchLifetimeUsage);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchLifetimeGame);
	AUTODESCPOWER_ADDLINE(pAutoDescPower->pchLifetimeReal);

	if(pAutoDescPower->pchCustom)
	{
		estrAppend2(ppchDesc,pAutoDescPower->pchCustom);
		return;
	}

	for(i=0; i<eaSize(&pAutoDescPower->ppAttribMods); i++)
	{
		AutoDescAttribMod *pAutoDescAttribMod = pAutoDescPower->ppAttribMods[i];
		AutoDescAttribModDefault(pAutoDescAttribMod,ppchDesc,cpchLine,cpchModIndent,cpchModPrefix,iDepth,lang);
	}

	for(i=0; i<eaSize(&pAutoDescPower->ppAttribModsEnhancements); i++)
	{
		AutoDescAttribMod *pAutoDescAttribMod = pAutoDescPower->ppAttribModsEnhancements[i];
		AutoDescAttribModDefault(pAutoDescAttribMod,ppchDesc,cpchLine,cpchModIndent,cpchModPrefix,iDepth,lang);
	}

	for(i=0; i<eaSize(&pAutoDescPower->ppPowersCombo); i++)
	{
		AutoDescPower *pAutoDescPowerChild = pAutoDescPower->ppPowersCombo[i];
		LineFeed(ppchDesc,cpchLine,cpchModIndent,0);
		LineFeed(ppchDesc,cpchLine,cpchModIndent,0);
		AutoDescPowerDefault(pAutoDescPowerChild,ppchDesc,cpchLine,cpchModIndent,cpchModPrefix,iDepth,lang);
	}

	s = eaSize(&pAutoDescPower->ppchFootnotes);
	if(s)
	{
		LineFeed(ppchDesc,cpchLine,cpchModIndent,0);
		for(i=0; i<s; i++)
		{
			AUTODESCPOWER_ADDLINE(pAutoDescPower->ppchFootnotes[i]);
		}
	}
}

static S32 AutoDescNeedsPeriodCorrection(SA_PARAM_NN_VALID Expression *pexpr)
{
	S32 bReturn = false;
	static MultiVal **ppStack = NULL;
	int i,s=beaSize(&pexpr->postfixEArray);
	eaClear(&ppStack);

	for(i=0; i<s; i++)
	{
		MultiVal *pVal = &pexpr->postfixEArray[i];
		if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
			continue;
		else
		{
			if(pVal->type==MULTIOP_OBJECT_PATH)
			{
				if(pVal->str && !stricmp(pVal->str,".Period"))
				{
					MultiVal *pValBase = eaPop(&ppStack);
					if(pValBase->type==MULTIOP_STATICVAR)
					{
						const char *pchBase = exprGetStaticVarName(pValBase->int32);
						if(pchBase && !stricmp(pchBase,"Activation"))
						{
							bReturn = true;
							continue;
						}
					}
				}
				bReturn = false;
				continue;
			}
			eaPush(&ppStack,pVal);
		}
	}

	return bReturn;
}


static S32 AutoDescNeedsChargeCorrection(SA_PARAM_NN_VALID Expression *pexpr)
{
	S32 bReturn = false;
	static MultiVal **ppStack = NULL;
	static char **ppchFunctions = NULL;
	int i,s=beaSize(&pexpr->postfixEArray);
	eaClear(&ppStack);

	if(!ppchFunctions)
	{
		eaPush(&ppchFunctions, StructAllocString("ActPercentCharged"));
		eaPush(&ppchFunctions, StructAllocString("ActCharged"));
		//TODO(BH): Add more functions here that expect charge times?
	}
	
	for(i=0; i<s; i++)
	{
		MultiVal *pVal = &pexpr->postfixEArray[i];
		if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
			continue;
		if(pVal->type==MULTIOP_FUNCTIONCALL &&
			eaFindString(&ppchFunctions, pVal->str) != -1)
		{
			bReturn = true;
			break;
		}
		else
		{
			if(pVal->type==MULTIOP_OBJECT_PATH)
			{
				if(pVal->str && !stricmp(pVal->str,".Charged"))
				{
					MultiVal *pValBase = eaPop(&ppStack);
					if(pValBase->type==MULTIOP_STATICVAR)
					{
						const char *pchBase = exprGetStaticVarName(pValBase->int32);
						if(pchBase && !stricmp(pchBase,"Activation"))
						{
							bReturn = true;
							continue;
						}
					}
				}
				bReturn = false;
				continue;
			}
			eaPush(&ppStack,pVal);
		}
	}

	return bReturn;
}


static S32 AutoDescExprFail(SA_PARAM_NN_VALID Expression *pexpr, SA_PARAM_NN_STR const char *cpchVar, SA_PARAM_NN_STR const char *cpchPath)
{
	S32 bReturn = false;
	S32 bAll = !stricmp(cpchPath, ".*");
	static MultiVal **ppStack = NULL;
	int i,s=beaSize(&pexpr->postfixEArray);
	eaClear(&ppStack);
	for(i=0; i<s; i++)
	{
		MultiVal *pVal = &pexpr->postfixEArray[i];
		if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
			continue;
		if(pVal->type==MULTIOP_OBJECT_PATH)
		{
			if(pVal->str 
				&& (bAll || isWildcardMatch(cpchPath,pVal->str,false,true)) )
			{
				MultiVal *pValBase = eaPop(&ppStack);
				if(pValBase->type==MULTIOP_STATICVAR)
				{
					const char *pchBase = exprGetStaticVarName(pValBase->int32);
					if(pchBase && !stricmp(pchBase,cpchVar))
					{
						bReturn = true;
						continue;
					}
				}
			}
			bReturn = false;
			continue;
		}
		eaPush(&ppStack,pVal);
	}
	return bReturn;
}

static S32 AutoDescExprFailFunc(SA_PARAM_NN_VALID Expression *pexpr, SA_PARAM_NN_STR const char *cpchFunc)
{
	int i;
	for(i=beaSize(&pexpr->postfixEArray)-1; i>=0; i--)
	{
		MultiVal *pVal = &pexpr->postfixEArray[i];
		if(pVal->type==MULTIOP_FUNCTIONCALL)
		{
			if(pVal->str && !stricmp(pVal->str,cpchFunc))
				return true;
		}
	}
	return false;
}

// Builds the list of Enhancements that are reasonable to include in autodescription for
//  a SPECIFIC Power on a Character.  If no real Character is provided, the Power may still
//  include local Enhancements from the Item it is on if the data is available.
static void AutoDescEnhancements(int iPartitionIdx, 
								 SA_PARAM_OP_VALID Character *pchar,
								 SA_PARAM_NN_VALID Power *ppow,
								 SA_PARAM_NN_VALID Power ***pppEnhancements)
{
	S32 i;

	if(pchar && verify(pchar->pEntParent) && entGetRef(pchar->pEntParent))
	{
		power_GetEnhancements(iPartitionIdx,pchar,ppow,pppEnhancements);
	}
	else if(ppow->eSource==kPowerSource_Item && ppow->pSourceItem)
	{
		static Power **s_ppEnhancementsLocal = NULL;
		eaClearFast(&s_ppEnhancementsLocal);
		item_GetEnhancementsLocal(ppow->pSourceItem,&s_ppEnhancementsLocal);
		power_AttachEnhancementsLocal(iPartitionIdx,ppow,pchar,s_ppEnhancementsLocal,pppEnhancements);
	}

	for(i=eaSize(pppEnhancements)-1; i>=0; i--)
	{
		PowerDef *pdef = GET_REF((*pppEnhancements)[i]->hDef);
		if(!pdef || pdef->pExprEnhanceApply)
		{
			eaRemoveFast(pppEnhancements,i);
		}
	}
}

// TODO(JW): AutoDesc: Design reasonable API for this
void power_AutoDesc(int iPartitionIdx,
					Power *ppow,
					Character *pchar,
					char **ppchDesc,
					AutoDescPower *pAutoDescPower,
					const char *cpchLine,
					const char *cpchModIndent,
					const char *cpchModPrefix,
					int bMinimalPowerDefHeader,
					int iDepth,
					AutoDescDetail eDetail,
					GameAccountDataExtract *pExtract,
					const char* pchPowerAutoDescMessageKey)
{
	static Power **s_ppEnhancements = NULL;
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(pdef)
	{
		int iLevel = pchar && pchar->iLevelCombat>0 ? pchar->iLevelCombat : 1;
		int eHeader = (!bMinimalPowerDefHeader || pdef->eType!=kPowerType_Innate) ? kAutoDescPowerHeader_Full : kAutoDescPowerHeader_None;
		AutoDescEnhancements(iPartitionIdx,pchar,ppow,&s_ppEnhancements);
		
		if(pchar && ppow->eSource==kPowerSource_PowerTree)
		{
			PTNode *pNode = NULL;
			character_FindPowerByIDTree(pchar,ppow->uiID,NULL,&pNode);
			if(pNode)
				g_CombatEvalOverrides.iAutoDescTableNodeRankHack = pNode->iRank+1;
		}
		
		AutoDescPowerDefInternal(iPartitionIdx,pdef,ppchDesc,pAutoDescPower,cpchLine,cpchModIndent,cpchModPrefix,eHeader,pchar,ppow,s_ppEnhancements,iLevel,true,false,iDepth,eDetail,NULL,NULL,NULL,pExtract,pchPowerAutoDescMessageKey);
		
		g_CombatEvalOverrides.iAutoDescTableNodeRankHack = 0;
		
		eaClearFast(&s_ppEnhancements);
	}
}

static void AutoDescPowerDev(PowerDef *pdef, char **ppchDesc, Language lang)
{
	if(isDevelopmentMode())
	{
		estrAppend2(ppchDesc,pdef->pchName);
		if(pdef->eError)
		{
			if(pdef->eError&kPowerError_Error)
			{
				estrAppend2(ppchDesc," : ");
				estrAppend2(ppchDesc,langTranslateMessageKey(lang,"AutoDesc.DataError"));
			}

			if(pdef->eError&kPowerError_Warning)
			{
				estrAppend2(ppchDesc," : ");
				estrAppend2(ppchDesc,langTranslateMessageKey(lang,"AutoDesc.DataWarning"));
			}
		}
	}
}

static void AutoDescPowerType(PowerDef *pdef, char **ppchDesc, Language lang)
{
	const char *pchTypeBase = StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType);
	char *pchTemp = NULL;
	estrStackCreate(&pchTemp);
	estrPrintf(&pchTemp,"AutoDesc.PowerDef.Type%s",pchTypeBase);
	langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Type",STRFMT_MESSAGEKEY("Type",pchTemp),STRFMT_END);
	estrDestroy(&pchTemp);
}

static void AutoDescPowerCost(int iPartitionIdx,
							  PowerDef *pdef,
							  S32 bSecondary,
							  char **ppchDescCost,
							  char **ppchDescCostPeriodic,
							  F32 *pCostMin,
							  F32 *pCostMax,
							  F32 *pCostPeriodicMin,
							  F32 *pCostPeriodicMax,
							  Character *pchar,
							  Power *ppow,
							  Power **ppEnhancements,
							  AutoDescDetail eDetail,
							  Language lang)
{
	static PowerActivation *s_pact = NULL;

	char *pchError = NULL;
	char *pchTemp = NULL;

	U32 bCostValid = false, bCostPeriodicValid = false;
	F32 fCost = 0, fCostMax = 0, fCostPeriodic = 0, fCostPeriodicMax = 0;
	const char *pchCostVariableReason = NULL;
	const char *pchCostPeriodicVariableReason = NULL;
	U32 bPeriodic = POWERTYPE_PERIODIC(pdef->eType);
	AttribType eAttribCost = bSecondary ? kAttribType_Power : POWERDEF_ATTRIBCOST(pdef);
	Expression *pExprCost = bSecondary ? pdef->pExprCostSecondary : pdef->pExprCost;
	Expression *pExprCostPeriodic = bSecondary ? pdef->pExprCostPeriodicSecondary : pdef->pExprCostPeriodic;

	if(!s_pact)
	{
		s_pact = poweract_Create();
	}

	SET_HANDLE_FROM_REFERENT(g_hPowerDefDict, pdef,s_pact->ref.hdef);

	estrStackCreate(&pchError);
	estrStackCreate(&pchTemp);

	if(pExprCost)
	{
		U32 bCopyCostToPeriodicCost = bPeriodic && !pExprCostPeriodic;

		combateval_ContextReset(kCombatEvalContext_Activate);
		combateval_ContextSetupActivate(pchar,NULL,NULL,kCombatEvalPrediction_None);
		fCost = combateval_EvalNew(iPartitionIdx,pExprCost,kCombatEvalContext_Activate,&pchError);

		if(pdef->fTimeCharge &&
			AutoDescNeedsChargeCorrection(pExprCost))
		{
			estrClear(&pchError);
			pchCostVariableReason = "AutoDesc.PowerDef.VariableReason.Charge";

			s_pact->fTimeCharged = 0;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
			fCost = combateval_EvalNew(iPartitionIdx,pExprCost,kCombatEvalContext_Activate,&pchError);

			s_pact->fTimeCharged = pdef->fTimeCharge;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
			fCostMax = combateval_EvalNew(iPartitionIdx,pExprCost,kCombatEvalContext_Activate,&pchError);
		}
		else if(*pchError && bPeriodic && AutoDescExprFail(pExprCost,"Activation",".Pulse"))
		{
			if(!pExprCostPeriodic && pdef->uiPeriodsMax)
			{
				estrClear(&pchError);
				pchCostPeriodicVariableReason = "AutoDesc.PowerDef.VariableReason.Pulse";

				s_pact->uiPeriod = 1;
				combateval_ContextReset(kCombatEvalContext_Activate);
				combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
				fCostPeriodic = combateval_EvalNew(iPartitionIdx,pExprCost,kCombatEvalContext_Activate,&pchError);

				s_pact->uiPeriod = pdef->uiPeriodsMax;
				combateval_ContextReset(kCombatEvalContext_Activate);
				combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
				fCostPeriodicMax = combateval_EvalNew(iPartitionIdx,pExprCost,kCombatEvalContext_Activate,&pchError);

				bCostPeriodicValid = !*pchError;
				bCopyCostToPeriodicCost = false;
			}

			estrClear(&pchError);
			s_pact->uiPeriod = 0;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
			fCost = combateval_EvalNew(iPartitionIdx,pExprCost,kCombatEvalContext_Activate,&pchError);
		}

		bCostValid = !*pchError;
		estrClear(&pchError);

		if(bCopyCostToPeriodicCost)
		{
			bCostPeriodicValid = bCostValid;
			fCostPeriodic = fCost;
			fCostPeriodicMax = fCostMax;
		}
	}

	if(pExprCostPeriodic)
	{
		combateval_ContextReset(kCombatEvalContext_Activate);
		combateval_ContextSetupActivate(pchar,NULL,NULL,kCombatEvalPrediction_None);
		fCostPeriodic = combateval_EvalNew(iPartitionIdx,pExprCostPeriodic,kCombatEvalContext_Activate,&pchError);

		if(pdef->fTimeCharge && 
			AutoDescNeedsChargeCorrection(pExprCostPeriodic))
		{
			estrClear(&pchError);
			pchCostPeriodicVariableReason = "AutoDesc.PowerDef.VariableReason.Charge";

			s_pact->fTimeCharged = 0;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
			fCostPeriodic = combateval_EvalNew(iPartitionIdx,pExprCostPeriodic,kCombatEvalContext_Activate,&pchError);

			s_pact->fTimeCharged = pdef->fTimeCharge;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
			fCostPeriodicMax = combateval_EvalNew(iPartitionIdx,pExprCostPeriodic,kCombatEvalContext_Activate,&pchError);
		}
		else if(*pchError && pdef->uiPeriodsMax && AutoDescExprFail(pExprCostPeriodic,"Activation",".Pulse"))
		{
			estrClear(&pchError);
			pchCostPeriodicVariableReason = "AutoDesc.PowerDef.VariableReason.Pulse";

			s_pact->uiPeriod = 1;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
			fCostPeriodic = combateval_EvalNew(iPartitionIdx,pExprCostPeriodic,kCombatEvalContext_Activate,&pchError);

			s_pact->uiPeriod = pdef->uiPeriodsMax;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pchar,NULL,s_pact,kCombatEvalPrediction_None);
			fCostPeriodicMax = combateval_EvalNew(iPartitionIdx,pExprCostPeriodic,kCombatEvalContext_Activate,&pchError);
		}

		bCostPeriodicValid = !*pchError;
		estrClear(&pchError);
	}

	if((bCostValid || bCostPeriodicValid) && pchar)
	{
		F32 fDiscount = 0;
		
		if(ppow)
			fDiscount = character_PowerBasicAttribEx(iPartitionIdx,pchar,ppow,NULL,kAttribType_DiscountCost,ppEnhancements,0);
		else
			fDiscount = pchar->pattrBasic ? pchar->pattrBasic->fDiscountCost : 0;

		if(fDiscount)
		{
			fCost /= fDiscount;
			fCostMax /= fDiscount;
			fCostPeriodic /= fDiscount;
			fCostPeriodicMax /= fDiscount;
		}
	}

	if(eDetail < kAutoDescDetail_Maximum)
	{
		fCost = AutoDescRound(fCost);
		fCostMax = AutoDescRound(fCostMax);
		fCostPeriodic = AutoDescRound(fCostPeriodic);
		fCostPeriodicMax = AutoDescRound(fCostPeriodicMax);
	}

	AutoDescAttribName(eAttribCost,&pchTemp,lang);

	if(pExprCost)
	{
		const char *pchSuffix = bPeriodic ? langTranslateMessageKey(lang,"AutoDesc.PowerDef.Cost.Initial") : NULL;

		if(pchCostVariableReason)
		{
			if(bCostValid)
			{
				*pCostMin = fCost;
				*pCostMax = fCostMax;
				langFormatGameMessageKey(lang,ppchDescCost,"AutoDesc.PowerDef.CostVariable",STRFMT_STRING("Attrib",pchTemp),STRFMT_FLOAT("CostMin",fCost),STRFMT_FLOAT("CostMax",fCostMax),STRFMT_STRING("Suffix",pchSuffix),STRFMT_MESSAGEKEY("Reason",pchCostVariableReason),STRFMT_END);
			}
			else
			{
				langFormatGameMessageKey(lang,ppchDescCost,"AutoDesc.PowerDef.CostVariable",STRFMT_STRING("Attrib",pchTemp),STRFMT_MESSAGEKEY("CostMin","AutoDesc.Unknown"),STRFMT_MESSAGEKEY("CostMax","AutoDesc.Unknown"),STRFMT_STRING("Suffix",pchSuffix),STRFMT_MESSAGEKEY("Reason",pchCostVariableReason),STRFMT_END);
			}
		}
		else
		{
			if(bCostValid)
			{
				*pCostMin = fCost;
				*pCostMax = fCost;
				langFormatGameMessageKey(lang,ppchDescCost,"AutoDesc.PowerDef.Cost",STRFMT_STRING("Attrib",pchTemp),STRFMT_FLOAT("Cost",fCost),STRFMT_STRING("Suffix",pchSuffix),STRFMT_END);
			}
			else
			{
				langFormatGameMessageKey(lang,ppchDescCost,"AutoDesc.PowerDef.Cost",STRFMT_STRING("Attrib",pchTemp),STRFMT_MESSAGEKEY("Cost","AutoDesc.Unknown"),STRFMT_STRING("Suffix",pchSuffix),STRFMT_END);
			}
		}
	}

	if(bPeriodic)
	{
		const char *pchSuffix = langTranslateMessageKey(lang,"AutoDesc.PowerDef.Cost.Periodic");

		if(pchCostPeriodicVariableReason)
		{
			if(bCostPeriodicValid)
			{
				*pCostPeriodicMin = fCostPeriodic;
				*pCostPeriodicMax = fCostPeriodicMax;
				langFormatGameMessageKey(lang,ppchDescCostPeriodic,"AutoDesc.PowerDef.CostVariable",STRFMT_STRING("Attrib",pchTemp),STRFMT_FLOAT("CostMin",fCostPeriodic),STRFMT_FLOAT("CostMax",fCostPeriodicMax),STRFMT_STRING("Suffix",pchSuffix),STRFMT_MESSAGEKEY("Reason",pchCostPeriodicVariableReason),STRFMT_END);
			}
			else
			{
				langFormatGameMessageKey(lang,ppchDescCostPeriodic,"AutoDesc.PowerDef.CostVariable",STRFMT_STRING("Attrib",pchTemp),STRFMT_MESSAGEKEY("CostMin","AutoDesc.Unknown"),STRFMT_MESSAGEKEY("CostMax","AutoDesc.Unknown"),STRFMT_STRING("Suffix",pchSuffix),STRFMT_MESSAGEKEY("Reason",pchCostPeriodicVariableReason),STRFMT_END);
			}
		}
		else
		{
			if(bCostPeriodicValid)
			{
				*pCostPeriodicMin = fCostPeriodic;
				*pCostPeriodicMax = fCostPeriodic;
				langFormatGameMessageKey(lang,ppchDescCostPeriodic,"AutoDesc.PowerDef.Cost",STRFMT_STRING("Attrib",pchTemp),STRFMT_FLOAT("Cost",fCostPeriodic),STRFMT_STRING("Suffix",pchSuffix),STRFMT_END);
			}
			else
			{
				langFormatGameMessageKey(lang,ppchDescCostPeriodic,"AutoDesc.PowerDef.Cost",STRFMT_STRING("Attrib",pchTemp),STRFMT_MESSAGEKEY("Cost","AutoDesc.Unknown"),STRFMT_STRING("Suffix",pchSuffix),STRFMT_END);
			}
		}
	}

	estrDestroy(&pchError);
	estrDestroy(&pchTemp);
}

static void AutoDescPowerEnhanceAttach(PowerDef *pdef, char **ppchDesc, Language lang)
{
	if(pdef->pExprEnhanceAttach)
	{
		char *pchRequirements = NULL;
		static MultiVal **s_ppStack = NULL;
		Expression *pexpr = pdef->pExprEnhanceAttach;
		int i,s=beaSize(&pexpr->postfixEArray);
		char* pchTagMsg = NULL;
		estrStackCreate(&pchTagMsg);
		estrStackCreate(&pchRequirements);
		eaClear(&s_ppStack);
		for(i=0; i<s; i++)
		{
			MultiVal *pVal = &pexpr->postfixEArray[i];
			if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
				continue;
			if(pVal->type==MULTIOP_FUNCTIONCALL)
			{
				const char *pchFunction = pVal->str;
				if(!stricmp(pchFunction,"HasPowerTag"))
				{
					MultiVal *pValTags = eaPop(&s_ppStack);
					MultiVal *pValXPath = eaPop(&s_ppStack);
					MultiVal *pValPower = eaPop(&s_ppStack);
					if(pValTags)
					{
						const char *pchTags = MultiValGetString(pValTags,NULL);
						estrPrintf(&pchTagMsg, "PowerTag.Desc.%s", pchTags);
						if(pchTags)
						{
							if(msgExists(pchTagMsg))
							{
								langFormatGameMessageKey(lang,&pchRequirements,"AutoDesc.PowerDef.EnhanceAttach.HasPowerTag",STRFMT_MESSAGEKEY("Tag",pchTagMsg),STRFMT_END);
							}
							else
							{
								langFormatGameMessageKey(lang,&pchRequirements,"AutoDesc.PowerDef.EnhanceAttach.HasPowerTag",STRFMT_STRING("Tag",pchTags),STRFMT_END);
							}
							continue;
						}
					}
				}
			}
			eaPush(&s_ppStack,pVal);
		}
		estrDestroy(&pchTagMsg);

		if(eaSize(&s_ppStack) > 0)
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.EnhanceAttachUnknown",STRFMT_END);
		}
		else
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.EnhanceAttach",STRFMT_STRING("Requirements",pchRequirements),STRFMT_END);
		}
		estrDestroy(&pchRequirements);
	}
}

static void AutoDescPowerEnhanceAttached(PowerDef *pdef, char **ppchDesc, Character *pchar, Power *ppow, Language lang)
{
	int i,s;
	langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Enhance.Enhancing",STRFMT_END);
	s = eaiSize(&ppow->puiEnhancementIDs);
	if(!s)
	{
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.Nothing",STRFMT_END);
	}
	else
	{
		for(i=0; i<s; i++)
		{
			Power *ppowEnhancing = character_FindPowerByID(pchar,ppow->puiEnhancementIDs[i]);
			PowerDef *pdefEnhancing = ppowEnhancing ? GET_REF(ppowEnhancing->hDef) : NULL;
			if(pdefEnhancing)
			{
				if(i)
				{
					estrAppend2(ppchDesc,"; ");
				}
				estrAppend2(ppchDesc,langTranslateDisplayMessage(lang,pdefEnhancing->msgDisplayName));
			}
		}
	}
}

static S32 AutoDescPowerTarget(PowerDef *pdef, char **ppchDesc, S32 *pMaxTargets, AutoDescPowerHeader eHeader, Language lang)
{
	S32 bSelf = false;

	PowerTarget *pTargetMain = GET_REF(pdef->hTargetMain);
	PowerTarget *pTargetAffected = GET_REF(pdef->hTargetAffected);

	if(!(pTargetMain && pTargetAffected))
		return true;	// Shouldn't ever happen, just here to keep analysis happy

	// Figure out of this affects just yourself
	if(pTargetAffected->bRequireSelf)
	{
		bSelf = true;
	}
	else if(eHeader==kAutoDescPowerHeader_ApplySelf && pdef->eEffectArea==kEffectArea_Character)
	{
		bSelf = true;
	}

	// Don't bother describing the target if it's a passive or apply that targets and affects yourself
	// Or if the it applies to yourself and the target area is a character
	if(!(bSelf && (pdef->eType==kPowerType_Passive || eHeader==kAutoDescPowerHeader_ApplySelf)))
	{
		const char *pchMain = langTranslateMessageRef(lang,pTargetMain->hMsgDescription);
		if(pTargetMain == pTargetAffected)
		{
			*pMaxTargets = 1;
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Target",STRFMT_STRING("Target",pchMain),STRFMT_END);
		}
		else
		{
			const char *pchAffected = langTranslateMessageRef(lang,pTargetAffected->hMsgDescription);
			// Don't bother mentioning the main target if it's a passive or it targets yourself
			if(!(pdef->eType==kPowerType_Passive || pTargetMain->bRequireSelf))
			{
				langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Target",STRFMT_STRING("Target",pchMain),STRFMT_END);
				estrAppend2(ppchDesc,"; ");
			}
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Affect",STRFMT_STRING("Affect",pchAffected),STRFMT_END);
			*pMaxTargets = 0;
		}

		if(pdef->iMaxTargetsHit > 0)
		{
			*pMaxTargets = pdef->iMaxTargetsHit;
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.MaxTargets",STRFMT_INT("MaxTargets",pdef->iMaxTargetsHit),STRFMT_END);
		}
	}

	return bSelf;
}

static void AutoDescPowerTargetArc(PowerDef *pdef, char **ppchDesc, Language lang)
{
	langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.TargetArc",STRFMT_INT("Arc",(S32)pdef->fTargetArc),STRFMT_END);
}

static const char *AutoDescGetUnitsMessage(MeasurementType eType, MeasurementSize eSize)
{
	switch(eType)
	{
	case kMeasurementType_Base:
		switch(eSize)
		{
		case kMeasurementSize_Base:
			return "AutoDesc.PowerDef.UnitsFoot";
		case kMeasurementSize_Small:
			return "AutoDesc.PowerDef.UnitsInch";
		case kMeasurementSize_Large:
			return "AutoDesc.PowerDef.UnitsMiles";
		}
	case kMeasurementType_Metric:
		switch(eSize)
		{
		case kMeasurementSize_Base:
			return "AutoDesc.PowerDef.UnitsMeter";
		case kMeasurementSize_Small:
			return "AutoDesc.PowerDef.UnitsCentimeter";
		case kMeasurementSize_Large:
			return "AutoDesc.PowerDef.UnitsKilometer";
		}
	case kMeasurementType_Yards:
		return "AutoDesc.PowerDef.UnitsYard";
	}

	//Default to foot units
	return "AutoDesc.PowerDef.UnitsFoot";
}

static void AutoDescPowerRange(int iPartitionIdx,
							   PowerDef *pdef,
							   char **ppchDesc,
							   F32 *pRangeMin,
							   F32 *pRangeMax,
							   F32 *pAreaMin,
							   F32 *pAreaMax,
							   F32 *pInnerAreaMin,
							   F32 *pInnerAreaMax,
							   Language lang,
							   MeasurementType eMeasurementType,
							   MeasurementSize eMeasurementSize,
							   F32 fScale)
{
	static PowerActivation *s_pact = NULL;

	S32 bRangeData = false;

	char *pchError = NULL;
	char *pchTemp = NULL;

	if(!s_pact)
	{
		s_pact = poweract_Create();
	}

	SET_HANDLE_FROM_REFERENT(g_hPowerDefDict, pdef, s_pact->ref.hdef);

	estrStackCreate(&pchError);
	estrStackCreate(&pchTemp);

	if(pdef->fRangeMin > 0)
	{
		F32 fFinalRangeMin = BaseToMeasurement(pdef->fRangeMin,eMeasurementType,eMeasurementSize) * fScale;
		F32 fFinalRange = BaseToMeasurement(pdef->fRange,eMeasurementType,eMeasurementSize) * fScale;
		*pRangeMin = fFinalRangeMin;
		*pRangeMax = fFinalRange;
		langFormatGameMessageKey(lang,&pchTemp,"AutoDesc.Common.Range",STRFMT_FLOAT("A",fFinalRangeMin),STRFMT_FLOAT("B",fFinalRange),STRFMT_END);
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Range",STRFMT_STRING("Range",pchTemp),STRFMT_MESSAGEKEY("Units",AutoDescGetUnitsMessage(eMeasurementType,eMeasurementSize)),STRFMT_END);
		estrClear(&pchTemp);
		bRangeData = true;
	}
	else if(pdef->fRange > 0)
	{
		F32 fFinalRange = BaseToMeasurement(pdef->fRange,eMeasurementType,eMeasurementSize) * fScale;
		*pRangeMin = fFinalRange;
		*pRangeMax = fFinalRange;
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Range",STRFMT_FLOAT("Range",fFinalRange),STRFMT_MESSAGEKEY("Units",AutoDescGetUnitsMessage(eMeasurementType,eMeasurementSize)),STRFMT_END);
		bRangeData = true;
	}

	if(GET_REF(pdef->hFX))
	{
		PowerAnimFX *pafx = GET_REF(pdef->hFX);
		if(pafx->pLunge && pafx->pLunge->fRange!=0)
		{
			F32 fFinalRangeLunge= BaseToMeasurement(pafx->pLunge->fRange,eMeasurementType,eMeasurementSize) * fScale;
			if(bRangeData)
				estrAppend2(ppchDesc,"; ");
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.RangeLunge",STRFMT_FLOAT("Lunge",fFinalRangeLunge),STRFMT_END);
			bRangeData = true;
		}
	}

	if(pdef->eEffectArea==kEffectArea_Sphere
		|| pdef->eEffectArea==kEffectArea_Cylinder
		|| pdef->eEffectArea==kEffectArea_Cone
		|| pdef->eEffectArea==kEffectArea_Team)
	{
		const char *pchAreaBase = StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea);
		const char *pchAreaVariableReason = NULL;
		const char *pchAreaUnits = NULL;
		F32 fArea = 0, fAreaMax = 0, fInnerArea = 0, fInnerAreaMax = 0;

		if(bRangeData)
		{
			estrAppend2(ppchDesc,"; ");
		}

		estrPrintf(&pchTemp,"AutoDesc.PowerDef.EffectArea%s",pchAreaBase);

		if(pdef->eEffectArea==kEffectArea_Cone)
		{
			pchAreaUnits = "AutoDesc.PowerDef.UnitsDegree";
		}
		else
		{
			pchAreaUnits = AutoDescGetUnitsMessage(eMeasurementType,eMeasurementSize);
		}

		if(pdef->eEffectArea==kEffectArea_Cone && pdef->pExprArc)
		{
			combateval_ContextReset(kCombatEvalContext_Target);
			combateval_ContextSetupTarget(NULL,NULL,NULL);
			fArea = combateval_EvalNew(iPartitionIdx,pdef->pExprArc,kCombatEvalContext_Target,&pchError);

			if(pdef->fTimeCharge && AutoDescNeedsChargeCorrection(pdef->pExprArc))
			{
				PowerApplication app = {0};
				app.pact = s_pact;
				estrClear(&pchError);
				pchAreaVariableReason = "AutoDesc.PowerDef.VariableReason.Charge";

				s_pact->fTimeCharged = 0;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fArea = combateval_EvalNew(iPartitionIdx,pdef->pExprArc,kCombatEvalContext_Target,&pchError);

				s_pact->fTimeCharged = pdef->fTimeCharge;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fAreaMax = combateval_EvalNew(iPartitionIdx,pdef->pExprArc,kCombatEvalContext_Target,&pchError);
			}
		}
		else if(pdef->pExprRadius)
		{
			S32 bUsingChargeCorrection = false;
			combateval_ContextReset(kCombatEvalContext_Target);
			combateval_ContextSetupTarget(NULL,NULL,NULL);
			fArea = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;
			
			if (pdef->pExprInnerRadius)
			{
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,NULL);
				fInnerArea = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprInnerRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;
			}
			if(pdef->fTimeCharge && AutoDescNeedsChargeCorrection(pdef->pExprRadius))
			{
				PowerApplication app = {0};
				app.pact = s_pact;
				estrClear(&pchError);
				pchAreaVariableReason = "AutoDesc.PowerDef.VariableReason.Charge";
				bUsingChargeCorrection = true;

				s_pact->fTimeCharged = 0;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fArea = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;

				s_pact->fTimeCharged = pdef->fTimeCharge;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fAreaMax = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;
			}

			if(pdef->fTimeCharge && pdef->pExprInnerRadius && AutoDescNeedsChargeCorrection(pdef->pExprInnerRadius))
			{
				PowerApplication app = {0};
				app.pact = s_pact;
				estrClear(&pchError);
				pchAreaVariableReason = "AutoDesc.PowerDef.VariableReason.Charge";
				bUsingChargeCorrection = true;

				s_pact->fTimeCharged = 0;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fInnerArea = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprInnerRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;

				s_pact->fTimeCharged = pdef->fTimeCharge;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fInnerAreaMax = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprInnerRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;
			}

			if(!bUsingChargeCorrection && pdef->uiPeriodsMax && AutoDescNeedsPeriodCorrection(pdef->pExprRadius))
			{
				PowerApplication app = {0};
				app.pact = s_pact;
				estrClear(&pchError);
				pchAreaVariableReason = "AutoDesc.PowerDef.VariableReason.Pulse";

				s_pact->uiPeriod = 0;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fArea = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;

				s_pact->uiPeriod = pdef->uiPeriodsMax;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fAreaMax = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;
			}

			if(!bUsingChargeCorrection && pdef->uiPeriodsMax && pdef->pExprInnerRadius && AutoDescNeedsPeriodCorrection(pdef->pExprInnerRadius))
			{
				PowerApplication app = {0};
				app.pact = s_pact;
				estrClear(&pchError);
				pchAreaVariableReason = "AutoDesc.PowerDef.VariableReason.Pulse";

				s_pact->uiPeriod = 0;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fInnerArea = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprInnerRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;

				s_pact->uiPeriod = pdef->uiPeriodsMax;
				combateval_ContextReset(kCombatEvalContext_Target);
				combateval_ContextSetupTarget(NULL,NULL,&app);
				fInnerAreaMax = BaseToMeasurement(combateval_EvalNew(iPartitionIdx,pdef->pExprInnerRadius,kCombatEvalContext_Target,&pchError),eMeasurementType,eMeasurementSize) * fScale;
			}
		}

		fArea = AutoDescRound(fArea);
		fAreaMax = AutoDescRound(fAreaMax);
		fInnerArea = AutoDescRound(fInnerArea);
		fInnerAreaMax = AutoDescRound(fInnerAreaMax);

		if(!*pchError)
		{
			*pAreaMin = fArea;
			*pAreaMax = fAreaMax;
			*pInnerAreaMin = fInnerArea;
			*pInnerAreaMax = fInnerAreaMax;
			if(pchAreaVariableReason)
			{
				langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.AreaVariable",
					STRFMT_FLOAT("SizeMin",fArea),
					STRFMT_FLOAT("SizeMax",fAreaMax),
					STRFMT_FLOAT("InnerSizeMin",fInnerArea),
					STRFMT_FLOAT("InnerSizeMax",fInnerAreaMax),
					STRFMT_MESSAGEKEY("Units",pchAreaUnits),
					STRFMT_MESSAGEKEY("Area",pchTemp),
					STRFMT_MESSAGEKEY("Reason",pchAreaVariableReason),
					STRFMT_END);
			}
			else
			{
				langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Area",
					STRFMT_FLOAT("Size",fArea),
					STRFMT_FLOAT("InnerSize",fInnerArea),
					STRFMT_MESSAGEKEY("Units",pchAreaUnits),
					STRFMT_MESSAGEKEY("Area",pchTemp),
					STRFMT_END);
			}
		}
		else
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Area",
				STRFMT_MESSAGEKEY("Size","AutoDesc.Unknown"),
				STRFMT_MESSAGEKEY("InnerSize","AutoDesc.Unknown"),
				STRFMT_MESSAGEKEY("Units",pchAreaUnits),
				STRFMT_MESSAGEKEY("Area",pchTemp),
				STRFMT_END);
		}
	}

	estrDestroy(&pchError);
	estrDestroy(&pchTemp);
}

static void AutoDescPowerTimeCharge(int iPartitionIdx, PowerDef *pdef, char **ppchDesc, F32 *pTimeChargeMin, F32 *pTimeChargeMax, Character *pchar, Power *ppow, Language lang)
{
	if(pdef->fTimeCharge)
	{
		F32 fTimeCharge = pdef->fTimeCharge;
		if(pchar)
		{
			F32 fSpeed = 1;
			if(ppow)
				fSpeed = character_GetSpeedCharge(iPartitionIdx, pchar, ppow);
			else
				fSpeed = pchar->pattrBasic ? pchar->pattrBasic->fSpeedCharge : 1;

			if(fSpeed <= 0)
			{
				fSpeed = 0.01;
			}
			fTimeCharge /= fSpeed;
		}

		if(pdef->fChargeRequire)
		{
			*pTimeChargeMin = fTimeCharge * pdef->fChargeRequire;
			*pTimeChargeMax = fTimeCharge;
			langFormatGameMessageKey(lang,ppchDesc,
									 "AutoDesc.PowerDef.TimeChargeRequire",
									 STRFMT_FLOAT("Time", fTimeCharge),
									 STRFMT_FLOAT("TimeRequire", fTimeCharge * pdef->fChargeRequire),
									 STRFMT_END);
		}
		else
		{
			*pTimeChargeMin = fTimeCharge;
			*pTimeChargeMax = fTimeCharge;
			langFormatGameMessageKey(lang,
									 ppchDesc,
									 "AutoDesc.PowerDef.TimeCharge",
									 STRFMT_FLOAT("Time", fTimeCharge),
									 STRFMT_END);
		}
	}
}

static void AutoDescPowerTimeActivate(PowerDef *pdef, char **ppchDesc, F32 *pTimeActivateMin, F32 *pTimeActivateMax, Language lang)
{
	if(pdef->fTimeActivate)
	{
		*pTimeActivateMin = pdef->fTimeActivate;
		*pTimeActivateMax = pdef->fTimeActivate;
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.TimeActivate",STRFMT_FLOAT("Time",pdef->fTimeActivate),STRFMT_END);
	}
}

static void AutoDescPowerTimeActivatePeriod(int iPartitionIdx, PowerDef *pdef, char **ppchDesc, F32 *pTimeActivatePeriodMin, F32 *pTimeActivatePeriodMax, Character *pchar, Power *ppow, Language lang)
{
	if(pdef->fTimeActivatePeriod)
	{
		F32 fTimeActivatePeriod = pdef->fTimeActivatePeriod;
		if(pchar)
		{
			F32 fSpeed = 1;
			if(ppow)
				fSpeed = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow);
			else
				fSpeed = pchar->pattrBasic ? pchar->pattrBasic->fSpeedCharge : 1;

			if(fSpeed <= 0)
			{
				fSpeed = 0.01;
			}

			fTimeActivatePeriod /= fSpeed;
		}

		*pTimeActivatePeriodMin = fTimeActivatePeriod;
		*pTimeActivatePeriodMax = fTimeActivatePeriod;
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.TimeActivatePeriod",STRFMT_FLOAT("Time",fTimeActivatePeriod),STRFMT_END);
	}
}

static void AutoDescPowerTimeMaintain(int iPartitionIdx, PowerDef *pdef, char **ppchDesc, F32 *pTimeMaintainMin, F32 *pTimeMaintainMax, Character *pchar, Power *ppow, Language lang)
{
	if(pdef->fTimeMaintain)
	{
		F32 fTimeMaintain = pdef->fTimeMaintain;
		if(pchar)
		{
			F32 fSpeed = 1;
			if(ppow)
				fSpeed = character_GetSpeedPeriod(iPartitionIdx, pchar, ppow);
			else
				fSpeed = pchar->pattrBasic ? pchar->pattrBasic->fSpeedCharge : 1;

			if(fSpeed <= 0)
			{
				fSpeed = 0.01;
			}

			fTimeMaintain /= fSpeed;
		}

		*pTimeMaintainMin = fTimeMaintain;
		*pTimeMaintainMax = fTimeMaintain;
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.TimeMaintain",STRFMT_FLOAT("Time",fTimeMaintain),STRFMT_END);
	}
}

static void AutoDescPowerTimeRecharge(	int iPartitionIdx, F32 fTimeRecharge, PowerDef *pdef, char **ppchDesc, 
										F32 *pfTimeRecharge, Character *pchar, Power *ppow, AutoDescDetail eDetail, Language lang)
{
	if(pchar)
	{
		F32 fSpeed = 1;
			
		if(ppow)
			fSpeed = power_GetSpeedRecharge(iPartitionIdx,pchar,ppow,pdef);
		else
			fSpeed = pchar->pattrBasic ? pchar->pattrBasic->fSpeedRecharge : 0;

		if(fSpeed<=0)
		{
			fSpeed = 1;
		}
		fTimeRecharge /= fSpeed;
	}
	*pfTimeRecharge = fTimeRecharge;

	if (ppchDesc)
	{
		char *pchTemp = NULL;
		estrStackCreate(&pchTemp);
		AutoDescTime(fTimeRecharge,&pchTemp,eDetail,lang);
		langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.TimeRecharge",STRFMT_STRING("Time",pchTemp),STRFMT_END);
		estrDestroy(&pchTemp);
	}
}

static void AutoDescPowerCharges(PowerDef *pdef, char **ppchDesc, Power *ppow, Language lang)
{
	if(pdef->iCharges)
	{
		if(ppow)
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.ChargesLeft",STRFMT_INT("Charges",pdef->iCharges),STRFMT_INT("ChargesLeft",power_GetChargesLeft(ppow)),STRFMT_END);
		}
		else
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.Charges",STRFMT_INT("Charges",pdef->iCharges),STRFMT_END);
		}
	}
}


static void AutoDescPowerLifetimeUsage(PowerDef *pdef, char **ppchDesc, Power *ppow, Language lang)
{
#ifdef GAMECLIENT
	if(pdef->fLifetimeUsage)
	{
		F32 fUsage = pdef->fLifetimeUsage;
		char* estrUsage = NULL;
		estrStackCreate(&estrUsage);
		gclFormatSecondsAsHMS(&estrUsage, fUsage, -1, "AutoDesc.PowerDef.FormatSeconds");
		if(ppow)
		{
			F32 fUsageLeft = power_GetLifetimeUsageLeft(ppow);
			char* estrUsageLeft = NULL;
			estrStackCreate(&estrUsageLeft);
			gclFormatSecondsAsHMS(&estrUsageLeft, fUsageLeft, -1, "AutoDesc.PowerDef.FormatSeconds");
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.LifetimeUsageLeft",STRFMT_STRING("LifetimeUsage",estrUsage),STRFMT_STRING("LifetimeUsageLeft",estrUsageLeft),STRFMT_END);
			estrDestroy(&estrUsageLeft);
		}
		else
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.LifetimeUsage",STRFMT_STRING("LifetimeUsage",estrUsage),STRFMT_END);
		}
		estrDestroy(&estrUsage);
	}
#endif
}

static void AutoDescPowerLifetimeGame(PowerDef *pdef, char **ppchDesc, Power *ppow, Language lang)
{
#ifdef GAMECLIENT
	if(pdef->fLifetimeGame)
	{
		F32 fGame = pdef->fLifetimeGame;
		char* estrGame = NULL;
		estrStackCreate(&estrGame);
		gclFormatSecondsAsHMS(&estrGame, fGame, -1, "AutoDesc.PowerDef.FormatSeconds");
		if(ppow)
		{
			F32 fGameLeft = power_GetLifetimeGameLeft(ppow);
			char* estrGameLeft = NULL;
			estrStackCreate(&estrGameLeft);
			gclFormatSecondsAsHMS(&estrGameLeft, fGameLeft, -1, "AutoDesc.PowerDef.FormatSeconds");
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.LifetimeGameLeft",STRFMT_STRING("LifetimeGame",estrGame),STRFMT_STRING("LifetimeGameLeft",estrGameLeft),STRFMT_END);
			estrDestroy(&estrGameLeft);
		}
		else
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.LifetimeGame",STRFMT_STRING("LifetimeGame",estrGame),STRFMT_END);
		}
		estrDestroy(&estrGame);
	}
#endif
}

static void AutoDescPowerLifetimeReal(PowerDef *pdef, char **ppchDesc, Power *ppow, Language lang)
{
#ifdef GAMECLIENT
	if(pdef->fLifetimeReal)
	{
		F32 fReal = pdef->fLifetimeReal;
		char* estrReal = NULL;
		estrStackCreate(&estrReal);
		gclFormatSecondsAsHMS(&estrReal, fReal, -1, "AutoDesc.PowerDef.FormatSeconds");
		if(ppow)
		{
			F32 fRealLeft = power_GetLifetimeRealLeft(ppow);
			char* estrRealLeft = NULL;
			estrStackCreate(&estrRealLeft);
			gclFormatSecondsAsHMS(&estrRealLeft, fRealLeft, -1, "AutoDesc.PowerDef.FormatSeconds");
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.LifetimeRealLeft",STRFMT_STRING("LifetimeReal",estrReal),STRFMT_STRING("LifetimeRealLeft",estrRealLeft),STRFMT_END);
			estrDestroy(&estrRealLeft);
		}
		else
		{
			langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerDef.LifetimeReal",STRFMT_STRING("LifetimeReal",estrReal),STRFMT_END);
		}
		estrDestroy(&estrReal);
	}
#endif
}


void powerdef_ConsolidateAutoDesc(AutoDescPower *pAutoDescPower)
{
	while(eaSize(&pAutoDescPower->ppPowersCombo))
	{
		AutoDescPower *pCurrentAudoDesc = eaPop(&pAutoDescPower->ppPowersCombo);
		
		#define DETERMINE_RANGE(varname) \
			if(pCurrentAudoDesc->f##varname##Min != -1.f) { if (pAutoDescPower->f##varname##Min == -1.f) { pAutoDescPower->f##varname##Min = pCurrentAudoDesc->f##varname##Min; } else { MIN1(pAutoDescPower->f##varname##Min, pCurrentAudoDesc->f##varname##Min); } } \
			if(pCurrentAudoDesc->f##varname##Max != -1.f) { if (pAutoDescPower->f##varname##Max == -1.f) { pAutoDescPower->f##varname##Max = pCurrentAudoDesc->f##varname##Max; } else { MAX1(pAutoDescPower->f##varname##Max, pCurrentAudoDesc->f##varname##Max); } }

		DETERMINE_RANGE(Cost);
		DETERMINE_RANGE(CostPeriodic);
		DETERMINE_RANGE(Range);
		DETERMINE_RANGE(Area);
		DETERMINE_RANGE(TimeCharge);
		DETERMINE_RANGE(TimeActivate);
		DETERMINE_RANGE(TimeActivatePeriod);
		DETERMINE_RANGE(TimeMaintain);
		DETERMINE_RANGE(TimeRecharge);

		if(pCurrentAudoDesc->iMaxTargets != -1.f) 
		{
			MAX1(pCurrentAudoDesc->iMaxTargets, pAutoDescPower->iMaxTargets); 
		}
		else 
		{
			pAutoDescPower->iMaxTargets = pCurrentAudoDesc->iMaxTargets; 
		}
		StructDestroy(parse_AutoDescPower, pCurrentAudoDesc);
		#undef DETERMINE_RANGE
	}
}

// returns a pooled string of the translated attribute type of the first AttribMod on the power.
const char* powerdef_GetAttributeType(PowerDef* pDef)
{
	int i;

	for (i = 0; i < eaSize(&pDef->ppOrderedMods); i++)
	{
		AttribModDef * pModDef = pDef->ppOrderedMods[i];
		if (pModDef && pModDef->offAttrib > -1)
		{
			return StaticDefineGetTranslatedMessage(AttribTypeEnum, pModDef->offAttrib);
		}
	}
	return NULL;
}

static F32 _getMinMaxExpectedDamage(int iPartitionIdx, PowerDef* pDef, Character* pChar, S32 iLevel, Item** ppWeapons, Power** ppEnhancements, bool bMax)
{
	F32 fTotalDamage = 0.0f;
	int i;

	for (i = 0; i < eaSize(&pDef->ppOrderedMods); i++)
	{
		AttribModDef * pModDef = pDef->ppOrderedMods[i];
		if (pModDef->eTarget == kModTarget_Target && pModDef->offAspect == kAttribAspect_BasicAbs && 
			(attrib_Matches(pModDef->offAttrib, StaticDefineIntGetInt(AttribTypeEnum, "DamageSetAll")) || attrib_Matches(pModDef->offAttrib, kAttribType_HitPoints))
			&& pModDef->bIncludeInEstimatedDamage)
		{
			F32 damage = 0.0f;
			F32 fStrength, fStrAdd = 0.f;
			PowerApplication papp = {0};
			
			// let's just duplicate all the code in AttribMod.c.  Yay!
			int bDuration = pModDef->eType&kModType_Duration && !pModDef->bForever;
			int bMagnitude = pModDef->eType&kModType_Magnitude;
			bool bDoIt = true;

			g_CombatEvalOverrides.bEnabled = true;
			g_CombatEvalOverrides.bMaxDamage = bMax;
			g_CombatEvalOverrides.bMinDamage = !bMax;
			g_CombatEvalOverrides.ppWeapons = ppWeapons;

			// Fake up a power application
			// this makes the g_CombatEvalOverrides.ppWeapons redundant.  Technically, bMaxDamage could do something other than crit, and bMinDamage
			// could do something other than "glancing", but we don't use DDDieRoll, etc, anymore.  So the overrides might be obsolete.  They certainly
			// can never handle things like Application.NumTargets, because we can't modify what that returns except by setting up the papp.
			papp.critical.bSuccess = bMax;
			papp.bGlancing = !bMax;
			papp.ppEquippedItems = ppWeapons;
			papp.iNumTargets = 1;
			papp.iNumTargetsHit = 1;
			papp.pdef = pDef;

			//Either of these contexts may be used for evaluation
			combateval_ContextSetupSimple(pChar, iLevel, ppWeapons ? ppWeapons[0] : NULL);
			combateval_ContextSetupApply(pChar, NULL, ppWeapons ? ppWeapons[0] : NULL, &papp);

			fStrength = moddef_GetStrength(iPartitionIdx, pModDef, pChar, pModDef->pPowerDef, 
											iLevel, iLevel, 1.0f, ppEnhancements, 0, NULL, true, NULL, &fStrAdd);  


			//The requires expression is no longer evaluated now that designers can 
			//	specify which mods get included and which don't.

			damage = moddef_GetMagnitude(iPartitionIdx, pModDef, character_GetClassCurrent(pChar), iLevel, 1.0f, true);

			// variance can apply to magnitude, duration, both, or neither
			if (pModDef->eType&kModType_Magnitude)
			{
				F32 fFactor = bMagnitude ? fStrength : 1.0f;
				
				damage += fStrAdd;

				if (bMax)
				{
					damage *= fFactor*(1.0f+pModDef->fVariance);
				}
				else
				{
					damage *= fFactor*(1.0f-pModDef->fVariance);
				}
			}
						
			if (pModDef->pExprDuration && pModDef->fPeriod)
			{
				F32 fFactor = bDuration ? fStrength : 1.0f;
				int iPulses = 1;
				F32 fDuration;
				fDuration = combateval_EvalNew(iPartitionIdx,pModDef->pExprDuration,kCombatEvalContext_Apply,NULL);
				
				if (pModDef->eType&kModType_Duration)
				{
					fDuration += fStrAdd;

					if (bMax)
					{
						fDuration *= fFactor*(1.0f+pModDef->fVariance);
					}
					else
					{
						fDuration *= fFactor*(1.0f-pModDef->fVariance);
					}
				}

				iPulses = (fDuration / pModDef->fPeriod)+1;
				if (pModDef->bIgnoreFirstTick)
					iPulses--;
				damage *= iPulses;
			}

			fTotalDamage += damage;
			
			g_CombatEvalOverrides.bEnabled = false;
			g_CombatEvalOverrides.bMaxDamage = false;
			g_CombatEvalOverrides.bMinDamage = false;
			g_CombatEvalOverrides.ppWeapons = NULL;
			
		}
		else if (pModDef->offAttrib == kAttribType_EntCreate)
		{
			EntCreateParams *pParams = (EntCreateParams*)pModDef->pParams;
			CritterDef * pCritter =  GET_REF(pParams->hCritter);
			if (pCritter)
			{
				int j;
				for (j=0;j<eaSize(&pCritter->ppPowerConfigs);j++)
				{
					PowerDef * pCritterPower = GET_REF(pCritter->ppPowerConfigs[j]->hPower);
					if (pCritterPower)
					{
						fTotalDamage += _getMinMaxExpectedDamage(iPartitionIdx, pCritterPower, pChar, iLevel, ppWeapons, ppEnhancements, bMax);
					}
				}
			}
		}
	}
	return fTotalDamage;
}

F32 powerdef_DDGetMinExpectedDamage(int iPartitionIdx, PowerDef* pDef, Character* pChar, S32 iLevel, Item** ppWeapons, Power** ppEnhancements)
{
	return _getMinMaxExpectedDamage(iPartitionIdx,pDef,pChar,iLevel,ppWeapons,ppEnhancements,false);
}


F32 powerdef_DDGetMaxExpectedDamage(int iPartitionIdx, PowerDef* pDef, Character* pChar, S32 iLevel, Item** ppWeapons, Power** ppEnhancements)
{
	return _getMinMaxExpectedDamage(iPartitionIdx,pDef,pChar,iLevel,ppWeapons,ppEnhancements,true);
}

void powerdef_UseNNOFormatting(	int iPartitionIdx, Power *ppow, PowerDef* pDef, char** ppchDesc, Character* pChar, 
								Item * pItem, Power** ppEnhancements, GameAccountDataExtract *pExtract, const char *pchMessageKey)
{
	int attr_STR, attr_CON, attr_DEX, attr_INT, attr_WIS, attr_CHA, toHit, radius, arc;
	float fCooldown, range;

	ItemDef* pItemDef = NULL;
	ItemDef* pWeaponDef = NULL;
	ItemPowerDef* pItemPowerDef = NULL;
	char* estrHitDesc = NULL;
	char* estrMissDesc = NULL;
	char* estrSpecialDesc = NULL;
	char* estrRankChangeDesc = NULL;
	char* estrWeapName = NULL;
	char* estrAoEString = NULL;
	char* estrTime = NULL;
	bool bHasCost = 0;
	bool bCharCreationWarning = false;
	int minDmg = 0, maxDmg = 0;
	int boonCost = 0;
	bool bSuppressErrorsOld = false;
	int i;
	F32 maxCategoryCooldown = 0;
	F32 fTimeRecharge = 0;

	//const char* typeString;
	char weaponDamageBuffer[6] = "[W]";
	Message *pPowerPurposeMessage;
	const char *pchPowerPurpose;
	const char *pchPowerPurposeUntranslated;
	const char* pchAttribType = "";

	// Look for the equipped weapon which will be the source of damage for this power (which will NOT necessarily be the item that is the source of the
	// power, and in fact those might be mutually exclusive use cases of this function)	
	// It should be 1 or 2 melee weapons, or a ranged weapon
	//Item* pEquippedWeapon = character_DDWeaponPickFirst(pChar, pDef);

	Item **ppEquippedWeapons = NULL;
	// if hTooltipDamagePowerDef exists, use it to calculate damage.  Otherwise just use pDef.
	PowerDef* pDamagePowerDef = GET_REF(pDef->hTooltipDamagePowerDef);
	if (!pDamagePowerDef)
	{
		pDamagePowerDef = pDef;
	}
	character_WeaponPick(pChar, pDamagePowerDef, &ppEquippedWeapons, pExtract);

	// I added pItem (the item that is the source of the power) as a parameter to this function, but it's currently not really necessary, since I ended up just
	// getting the item def after all, but it's not hurting anything either.  [RMARR - 9/29/10]
	if (pItem)
	{
		pItemDef = GET_REF(pItem->hItem);
		pItemPowerDef = itemdef_GetItemPowerDef(pItemDef, 0);
	}

	pchPowerPurposeUntranslated = StaticDefineIntRevLookup(PowerPurposeEnum, pDef->ePurpose);
	pPowerPurposeMessage = StaticDefineGetMessage(PowerPurposeEnum, pDef->ePurpose);
	if (pPowerPurposeMessage)
	{
		pchPowerPurpose = TranslateMessagePtr(pPowerPurposeMessage);
	}
	else
	{
		pchPowerPurpose = StaticDefineIntRevLookup(PowerPurposeEnum, pDef->ePurpose);
	}
	if (!pchPowerPurpose)
	{
		pchPowerPurpose = "";
	}

	range = pDef->fRange;

	for(i=0;i<ea32Size(&pDef->piCategories);i++)
	{
		PowerCategory *pCat = g_PowerCategories.ppCategories[pDef->piCategories[i]];

		maxCategoryCooldown = max(maxCategoryCooldown, pCat->fTimeCooldown);
	}

	fTimeRecharge = pDef->fTimeRecharge;
	// This really isn't right, and there may be no way to be right, without some fancier work.
	if (pDef->fTimeRecharge == 0.0f)
	{
		PowerDef * pComboPower=NULL;
		if (eaSize(&pDef->ppCombos))
		{
			pComboPower = pDef;
		}
		else if (ppow && ppow->pParentPower)
		{
			pComboPower = GET_REF(ppow->pParentPower->hDef);
		}

		if (pComboPower)
		{
			for(i=0; i<eaSize(&pComboPower->ppCombos); i++)
			{
				PowerCombo *pCombo = pComboPower->ppCombos[i];
				PowerDef * pChildDef = GET_REF(pCombo->hPower);
				if (pChildDef && pChildDef->fTimeRecharge)
				{
					fTimeRecharge = pChildDef->fTimeRecharge;
					break;
				}
			}
		}
	}
	AutoDescPowerTimeRecharge(iPartitionIdx,max(fTimeRecharge, maxCategoryCooldown),pDef,NULL,&fCooldown,pChar,ppow,0,0);

	/*
	if (pItem)
	{
		typeString = "ItemPower";
	}
	else if (HasPowerCat(pDef, "Daily"))
	{
		typeString = "Daily";
	}
	else if (HasPowerCat(pDef, "Encounter"))
	{
		typeString = "Encounter";
	}
	else if (HasPowerCat(pDef, "Boon"))
	{
		typeString = "Boon";
	}
	else if (HasPowerCat(pDef, "ClassFeature"))
	{
		typeString = "ClassFeature";
	}
	else
	{
		typeString = "At-will"; // You can't assume At Will, also, you can't use a hyphen in these strings, since they'll be appended to build a key
	}
	*/

	toHit = 0;
	if (pChar && pChar->pattrBasic)
	{
		attr_STR = *F32PTR_OF_ATTRIB(pChar->pattrBasic,StaticDefineIntGetInt(AttribTypeEnum,"STR"));
		attr_CON = *F32PTR_OF_ATTRIB(pChar->pattrBasic,StaticDefineIntGetInt(AttribTypeEnum,"CON"));
		attr_DEX = *F32PTR_OF_ATTRIB(pChar->pattrBasic,StaticDefineIntGetInt(AttribTypeEnum,"DEX"));
		attr_INT = *F32PTR_OF_ATTRIB(pChar->pattrBasic,StaticDefineIntGetInt(AttribTypeEnum,"INT"));
		attr_WIS = *F32PTR_OF_ATTRIB(pChar->pattrBasic,StaticDefineIntGetInt(AttribTypeEnum,"WIS"));
		attr_CHA = *F32PTR_OF_ATTRIB(pChar->pattrBasic,StaticDefineIntGetInt(AttribTypeEnum,"CHA"));
		/*if (pDef->bWeaponBased && pEquippedWeapon)
		{
			estrPrintf(&estrWeapName, "%s d", TranslateDisplayMessage(GET_REF(pEquippedWeapon->hItem)->displayNameMsg));
		}
		else
		{
			estrPrintf(&estrWeapName, "D");
		}*/
	}
	else // HACK: In character creation screen we don't have the attribs set on character.
	{
#ifdef GAMECLIENT
		attr_STR = DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, "STR");
		attr_CON = DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, "CON");
		attr_DEX = DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, "DEX");
		attr_INT = DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, "INT");
		attr_WIS = DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, "WIS");
		attr_CHA = DDGetAbilityScoreFromAssignedStats(g_CharacterCreationData->assignedStats, "CHA");
		if (pDef->bWeaponBased)
		{
			estrPrintf(&estrWeapName, "Example d");
		}
		else
		{
			estrPrintf(&estrWeapName, "D");
		}
#else
		attr_STR = 10.0f;
		attr_CON = 10.0f;
		attr_DEX = 10.0f;
		attr_INT = 10.0f;
		attr_WIS = 10.0f;
		attr_CHA = 10.0f;
#endif
	}

	if (pDef->pExprRadius)
	{
		combateval_ContextReset(kCombatEvalContext_Target);
		combateval_ContextSetupTarget(NULL,NULL,NULL);
		radius = combateval_EvalNew(iPartitionIdx, pDef->pExprRadius, kCombatEvalContext_Target, NULL);
	}
	else
	{
		radius = 0;
	}

	if (pDef->pExprArc)
	{
		combateval_ContextReset(kCombatEvalContext_Target);
		combateval_ContextSetupTarget(NULL,NULL,NULL);
		arc = combateval_EvalNew(iPartitionIdx, pDef->pExprArc, kCombatEvalContext_Target, NULL);
	}
	else
	{
		arc = 0;
	}

	if (pDef->eEffectArea == kEffectArea_Sphere)
	{
		if (range == 0)
			estrPrintf(&estrAoEString, "Burst");
		else
			estrPrintf(&estrAoEString, "Blast");
	}
	else if (pDef->eEffectArea == kEffectArea_Cone)
	{
		estrPrintf(&estrAoEString, "Cone");
	}
	else if (pDef->eEffectArea == kEffectArea_Cylinder)
	{
		estrPrintf(&estrAoEString, "Cylinder");
	}

	if (pDef->eAttribCost == -1 && pDef->pExprCost)
	{
		combateval_ContextReset(kCombatEvalContext_Target);
		combateval_ContextSetupTarget(NULL,NULL,NULL);
		boonCost = combateval_EvalNew(iPartitionIdx,pDef->pExprCost,kCombatEvalContext_Target,NULL);
	}
	if (ppEquippedWeapons && ppEquippedWeapons[0])
	{
		pWeaponDef = GET_REF(ppEquippedWeapons[0]->hItem);

		if (pWeaponDef)
		{
			if (pWeaponDef->pItemDamageDef)
			{
				S32 iMinDamage = 0;
				S32 iMaxDamage = 0;

				FOR_EACH_IN_CONST_EARRAY_FORWARDS(ppEquippedWeapons, Item, pEquippedItem)
				{
					pWeaponDef = GET_REF(pEquippedItem->hItem);

					if (pWeaponDef && pWeaponDef->pItemDamageDef)
					{
						iMinDamage += ItemWeaponDamageFromItemDef(iPartitionIdx, NULL, pWeaponDef, kCombatEvalMagnitudeCalculationMethod_Min);
						iMaxDamage += ItemWeaponDamageFromItemDef(iPartitionIdx, NULL, pWeaponDef, kCombatEvalMagnitudeCalculationMethod_Max);
					}
				}
				FOR_EACH_END

				if (iMinDamage != iMaxDamage)
				{
					sprintf(weaponDamageBuffer, "%d-%d", iMinDamage, iMaxDamage);
				}
				else
				{
					sprintf(weaponDamageBuffer, "%d", iMinDamage);
				}
			}
			else if (pWeaponDef->pItemWeaponDef)
			{
				sprintf(weaponDamageBuffer, "%id%i", pWeaponDef->pItemWeaponDef->iNumDice, pWeaponDef->pItemWeaponDef->iDieSize);
			}
		}
	}

	FormatDisplayMessage(&estrHitDesc, pDef->msgDescription,
		STRFMT_STRING("W", weaponDamageBuffer),
		STRFMT_INT("STR", combat_DDAbilityMod(attr_STR)),
		STRFMT_INT("CON", combat_DDAbilityMod(attr_CON)),
		STRFMT_INT("DEX", combat_DDAbilityMod(attr_DEX)),
		STRFMT_INT("INT", combat_DDAbilityMod(attr_INT)),
		STRFMT_INT("WIS", combat_DDAbilityMod(attr_WIS)),
		STRFMT_INT("CHA", combat_DDAbilityMod(attr_CHA)),
		STRFMT_INT("NUM", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iNumDice : 0),
		STRFMT_INT("SIZE", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iDieSize : 0),
		STRFMT_END);

	FormatDisplayMessage(&estrMissDesc, pDef->msgDescriptionLong,
		STRFMT_STRING("W", weaponDamageBuffer),
		STRFMT_INT("STR", combat_DDAbilityMod(attr_STR)),
		STRFMT_INT("CON", combat_DDAbilityMod(attr_CON)),
		STRFMT_INT("DEX", combat_DDAbilityMod(attr_DEX)),
		STRFMT_INT("INT", combat_DDAbilityMod(attr_INT)),
		STRFMT_INT("WIS", combat_DDAbilityMod(attr_WIS)),
		STRFMT_INT("CHA", combat_DDAbilityMod(attr_CHA)),
		STRFMT_INT("NUM", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iNumDice : 0),
		STRFMT_INT("SIZE", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iDieSize : 0),
		STRFMT_END);

	FormatDisplayMessage(&estrSpecialDesc, pDef->msgDescriptionFlavor,
		STRFMT_STRING("W", weaponDamageBuffer),
		STRFMT_INT("STR", combat_DDAbilityMod(attr_STR)),
		STRFMT_INT("CON", combat_DDAbilityMod(attr_CON)),
		STRFMT_INT("DEX", combat_DDAbilityMod(attr_DEX)),
		STRFMT_INT("INT", combat_DDAbilityMod(attr_INT)),
		STRFMT_INT("WIS", combat_DDAbilityMod(attr_WIS)),
		STRFMT_INT("CHA", combat_DDAbilityMod(attr_CHA)),
		STRFMT_INT("NUM", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iNumDice : 0),
		STRFMT_INT("SIZE", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iDieSize : 0),
		STRFMT_END);

	FormatDisplayMessage(&estrRankChangeDesc, pDef->msgRankChange,
		STRFMT_STRING("W", weaponDamageBuffer),
		STRFMT_INT("STR", combat_DDAbilityMod(attr_STR)),
		STRFMT_INT("CON", combat_DDAbilityMod(attr_CON)),
		STRFMT_INT("DEX", combat_DDAbilityMod(attr_DEX)),
		STRFMT_INT("INT", combat_DDAbilityMod(attr_INT)),
		STRFMT_INT("WIS", combat_DDAbilityMod(attr_WIS)),
		STRFMT_INT("CHA", combat_DDAbilityMod(attr_CHA)),
		STRFMT_INT("NUM", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iNumDice : 0),
		STRFMT_INT("SIZE", (pItemDef && pItemDef->pItemWeaponDef) ? pItemDef->pItemWeaponDef->iDieSize : 0),
		STRFMT_END);

/*	if (IS_HANDLE_ACTIVE(pDef->hCostRecipe) && pChar)
	{
		ItemDef* pRecipeDef = GET_REF(pDef->hCostRecipe);
		int i = 0;
		char* estrTemp = NULL;
		bHasCost = true;
		estrPrintf(&estrSpecialCost, "<table><tr><td valign=top border=3><font color=red>COST:</font></td><td valign=top border=3 width=\"90%%\">");
		for (i = 0; i < eaSize(&pRecipeDef->pCraft->ppPart); i++)
		{
			ItemDef* pComponentItem = GET_REF(pRecipeDef->pCraft->ppPart[i]->hItem);
			int numOwned = inv_ent_AllBagsCountItems(pChar->pEntParent, pComponentItem->pchName);
			estrClear(&estrTemp);
			estrPrintf(&estrTemp, "%s x%i (Owned: <font color=%s>%i</font>)<br>", TranslateDisplayMessage(pComponentItem->displayNameMsg), (int)pRecipeDef->pCraft->ppPart[i]->fCount, (numOwned >= pRecipeDef->pCraft->ppPart[i]->fCount) ? "limegreen" : "red", numOwned);
			estrAppend(&estrSpecialCost, &estrTemp);
		}
		estrDestroy(&estrTemp);
		estrAppend2(&estrSpecialCost, "</td></tr></table>");
	}*/

	// We only want this information for non-item-powers that the player might use.  Otherwise, we do not expect to see the damage
	// fields expanded in the string - [RMARR 9/29/10]
	if (pItem == NULL && pChar)
	{
		minDmg = powerdef_DDGetMinExpectedDamage(iPartitionIdx, pDamagePowerDef, pChar, pChar->iLevelCombat, ppEquippedWeapons, ppEnhancements);
		maxDmg = powerdef_DDGetMaxExpectedDamage(iPartitionIdx, pDamagePowerDef, pChar, pChar->iLevelCombat, ppEquippedWeapons, ppEnhancements);
		pchAttribType = powerdef_GetAttributeType(pDamagePowerDef);
	}

	{//Convert cooldown time into minutes, seconds, etc.
		int iCooldownSeconds = (int)fCooldown;
		int days, hours, mins;
		F32 fSeconds;

		days = iCooldownSeconds / SECONDS_PER_DAY;
		iCooldownSeconds -= days * SECONDS_PER_DAY;
		hours = iCooldownSeconds / SECONDS_PER_HOUR;
		iCooldownSeconds -= hours * SECONDS_PER_HOUR;
		mins = iCooldownSeconds / SECONDS_PER_MINUTE;
		iCooldownSeconds -= mins * SECONDS_PER_MINUTE;
		// Get just the seconds and round it to the tenths place.
		fSeconds = (F32)iCooldownSeconds+0.1f*floorf(10.f*fmodf(fCooldown,1.0f));

		FormatMessageKey(&estrTime, "CooldownTime.Formatted",
			STRFMT_INT("DAY", days),
			STRFMT_INT("HRS", hours),
			STRFMT_INT("MIN", mins),
			STRFMT_FLOAT("SEC", fSeconds),
			STRFMT_END);
	}

	langFormatGameMessageKey(langGetCurrent(), ppchDesc, pchMessageKey && *pchMessageKey ? pchMessageKey : "AutoDesc.NNOPowerFormat",
		STRFMT_STRING("NAME", pDef ? TranslateDisplayMessage(pDef->msgDisplayName) : NULL),
		STRFMT_INT("RANGE", range),
		STRFMT_STRING("AOE", estrAoEString),
		//STRFMT_STRING("COOLDOWN", typeString),
		STRFMT_STRING("HITDESC", estrHitDesc),
		STRFMT_STRING("MISSDESC", estrMissDesc),
		STRFMT_STRING("SPECIALDESC", estrSpecialDesc),
		STRFMT_INT("COST", boonCost),
		STRFMT_INT("ARC", arc),
		STRFMT_INT("RADIUS", radius),
		STRFMT_STRING("COOLDOWNTIME", estrTime),
		STRFMT_INT("COOLDOWNTIMETOTAL", fCooldown),
		STRFMT_INT("ItemPowerFactor", ppEquippedWeapons && ppEquippedWeapons[0] ? item_GetPowerFactor(ppEquippedWeapons[0]) : 0),
		STRFMT_INT("NUM", (pWeaponDef && pWeaponDef->pItemWeaponDef) ? pWeaponDef->pItemWeaponDef->iNumDice : 0),
		STRFMT_INT("SIZE", (pWeaponDef && pWeaponDef->pItemWeaponDef) ? pWeaponDef->pItemWeaponDef->iDieSize : 0),
		STRFMT_STRING("W", weaponDamageBuffer),
		STRFMT_INT("HasCost", bHasCost),
		STRFMT_INT("DoesDamage", (!!(minDmg + maxDmg) && (pDef->eType == kPowerType_Combo ||
														pDef->eType == kPowerType_Click ||
														pDef->eType == kPowerType_Maintained ||
														pDef->eType == kPowerType_Toggle))),
		STRFMT_INT("IsClickable", (pDef->eType == kPowerType_Combo ||
									pDef->eType == kPowerType_Click ||
									pDef->eType == kPowerType_Maintained ||
									pDef->eType == kPowerType_Toggle)),
		//STRFMT_STRING("WeapName", estrWeapName),
		STRFMT_FLOAT("MinDmg", minDmg),
		STRFMT_FLOAT("MaxDmg", maxDmg),
		STRFMT_STRING("AttribType", pchAttribType),
		STRFMT_STRING("Purpose", pchPowerPurpose),
		STRFMT_STRING("LogicalPurpose", pchPowerPurposeUntranslated),
		STRFMT_END);

	eaDestroy(&ppEquippedWeapons);
	estrDestroy(&estrHitDesc);
	estrDestroy(&estrMissDesc);
	estrDestroy(&estrSpecialDesc);
	estrDestroy(&estrAoEString);
	estrDestroy(&estrWeapName);
	estrDestroy(&estrTime);
}

void powerdef_UseNNOFormattingForBuff(PowerDef* pDef, char** ppchDesc)
{
	ItemDef* pItemDef = NULL;
	ItemDef* pWeaponDef = NULL;
	ItemPowerDef* pItemPowerDef = NULL;

	FormatDisplayMessage(ppchDesc, pDef->msgDescriptionLong,
		STRFMT_END);
}

bool g_bNNOItemPower = false;

static void AutoDescPowerDefInternal(int iPartitionIdx,
									 PowerDef *pdef,
									 char **ppchDesc,
									 AutoDescPower *pAutoDescPower,
									 const char *cpchLine,
									 const char *cpchModIndent,
									 const char *cpchModPrefix,
									 AutoDescPowerHeader eHeader,
									 Character *pchar,
									 Power *ppow,
									 Power **ppEnhancements,
									 int iLevel,
									 int bIncludeStrength,
									 int bFullDescription,
									 int iDepth,
									 AutoDescDetail eDetail,
									 AutoDescAttribMod *pAutoDescAttribModEvent,
									 PowerDef ***pppPowerDefsDescribed,
									 const char ***pppchFootnoteMsgKeys,
									 GameAccountDataExtract *pExtract,
									 const char *pchPowerAutoDescMessageKey)
{
	static PowerActivation *s_pact = NULL;

	Language lang = langGetCurrent();

	int i,s;
	int bSuppressErrorsOld;
	int bSelf = false;
	int bLocalAutoDescPower = false;
	int bHeaderShown = eHeader==kAutoDescPowerHeader_Full;
	int bModsTargetSelf = false, bModsTargetTarget = false, bModsTargetMixed = false;
	int iLevelPower = POWERLEVEL(ppow,iLevel);
	const char **ppchFootnoteMsgKeysLocal = NULL;
	PowerDef **ppPowerDefsDescribedLocal = NULL;
	AttribModDef **ppMods = NULL;
	AutoDescModScale **ppModsEnhancements = NULL;
	F32 fPowerAppsPerSecond = 0;
	PowerAnimFX *pafx = GET_REF(pdef->hFX);

	// NNO HACK - rather than use the system at all, this just sits here and routes you
	//  into entirely custom code
	if (gConf.bUseNNOPowerDescs && (!ppow || !ppow->pSourceItem) && !g_bNNOItemPower)
	{
		powerdef_UseNNOFormatting(iPartitionIdx, ppow, pdef, ppchDesc, pchar, ppow ? ppow->pSourceItem : NULL, ppEnhancements, pExtract, pchPowerAutoDescMessageKey);
		return;
	}

	// Note if everything below here should be fully described in order to support the custom AutoDesc message
	if(!bFullDescription && eDetail<kAutoDescDetail_Maximum && IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage))
		bFullDescription = true;

	// Horrible hack to fix TableApp function during autodescription
	g_CombatEvalOverrides.iAutoDescTableAppHack = iLevel;

	if(!pAutoDescPower)
	{
		devassert(ppchDesc); // Make sure if they're not passing a struct to fill, they're at least passing an estr
		pAutoDescPower = StructCreate(parse_AutoDescPower);
		bLocalAutoDescPower = true;
	}

	if(!s_pact)
	{
		s_pact = poweract_Create();
	}

	// Save old eval error suppression
	bSuppressErrorsOld = g_bCombatEvalSuppressErrors;
	g_bCombatEvalSuppressErrors = true;

	if(!pppPowerDefsDescribed)
	{
		pppPowerDefsDescribed = &ppPowerDefsDescribedLocal;
	}
	eaPushUnique(pppPowerDefsDescribed,pdef);

	if(!pppchFootnoteMsgKeys)
	{
		pppchFootnoteMsgKeys = &ppchFootnoteMsgKeysLocal;
	}

	// Key
	pAutoDescPower->pchKey = pdef->pchName;

	// Dev data
	AutoDescPowerDev(pdef,&pAutoDescPower->pchDev,lang);

	// Lifetimes
	AutoDescPowerCharges(pdef,&pAutoDescPower->pchCharges,ppow,lang);
	AutoDescPowerLifetimeUsage(pdef,&pAutoDescPower->pchLifetimeUsage,ppow,lang);
	AutoDescPowerLifetimeGame(pdef,&pAutoDescPower->pchLifetimeGame,ppow,lang);
	AutoDescPowerLifetimeReal(pdef,&pAutoDescPower->pchLifetimeReal,ppow,lang);

	// Attribs which modify duration/magnitude
	AutoDescAttributesAffectingPower(pdef, &pAutoDescPower->pchAttribs, lang);

	if(eHeader==kAutoDescPowerHeader_Full)
	{
		F32 fApps = 1;
		F32 fTimeAppCycle = 0;

		// Name and descriptions
		pAutoDescPower->pchName = langTranslateDisplayMessage(lang,pdef->msgDisplayName);
		pAutoDescPower->pchDesc = langTranslateDisplayMessage(lang,pdef->msgDescription);
		pAutoDescPower->pchDescLong = langTranslateDisplayMessage(lang,pdef->msgDescriptionLong);
		pAutoDescPower->pchDescFlavor = langTranslateDisplayMessage(lang,pdef->msgDescriptionFlavor);

		// Type
		AutoDescPowerType(pdef,&pAutoDescPower->pchType,lang);

		// Timing
		AutoDescPowerTimeCharge(iPartitionIdx,pdef,&pAutoDescPower->pchTimeCharge,&pAutoDescPower->fTimeChargeMin,&pAutoDescPower->fTimeChargeMax,pchar,ppow,lang);
		AutoDescPowerTimeActivate(pdef,&pAutoDescPower->pchTimeActivate,&pAutoDescPower->fTimeActivateMin,&pAutoDescPower->fTimeActivateMax,lang);
		AutoDescPowerTimeActivatePeriod(iPartitionIdx,pdef,&pAutoDescPower->pchTimeActivatePeriod,&pAutoDescPower->fTimeActivatePeriodMin,&pAutoDescPower->fTimeActivatePeriodMax,pchar,ppow,lang);
		AutoDescPowerTimeMaintain(iPartitionIdx,pdef,&pAutoDescPower->pchTimeMaintain,&pAutoDescPower->fTimeMaintainMin,&pAutoDescPower->fTimeMaintainMax,pchar,ppow,lang);
		AutoDescPowerTimeRecharge(iPartitionIdx,pdef->fTimeRecharge,pdef,&pAutoDescPower->pchTimeRecharge,&pAutoDescPower->fTimeRechargeMin,pchar,ppow,eDetail,lang);
		pAutoDescPower->fTimeRechargeMax = pAutoDescPower->fTimeRechargeMin;

		// Attempt to calculate some sort of Applications per Second for the Power
		if(POWERTYPE_PERIODIC(pdef->eType))
		{
			fApps = (F32)pdef->uiPeriodsMax;
			fTimeAppCycle += fApps * pdef->fTimeActivatePeriod;
			if(fApps>0 && pdef->eType==kPowerType_Maintained)
			{
				fApps += 1;
			}
		}
		fTimeAppCycle += pdef->fTimeActivate;
		fTimeAppCycle += pdef->fTimeRecharge;
		fTimeAppCycle += pdef->fTimeCharge; // This is totally incorrect for anything except STO, in which case it's required
		if(fApps > 0 && fTimeAppCycle > 0)
			fPowerAppsPerSecond = fApps / fTimeAppCycle;

		// Cost
		if(pdef->pExprCostSecondary || pdef->pExprCostPeriodicSecondary)
		{
			// Generate both main and secondary cost descriptions, then mash them together as appropriate
			char *pchCost = NULL, *pchCostPeriodic = NULL;
			char *pchCostSecondary = NULL, *pchCostPeriodicSecondary = NULL;
			AutoDescPowerCost(iPartitionIdx,pdef,false,&pchCost,&pchCostPeriodic,&pAutoDescPower->fCostMin,&pAutoDescPower->fCostMax,&pAutoDescPower->fCostPeriodicMin,&pAutoDescPower->fCostPeriodicMax,pchar,ppow,ppEnhancements,eDetail,lang);
			AutoDescPowerCost(iPartitionIdx,pdef,true,&pchCostSecondary,&pchCostPeriodicSecondary,&pAutoDescPower->fCostMin,&pAutoDescPower->fCostMax,&pAutoDescPower->fCostPeriodicMin,&pAutoDescPower->fCostPeriodicMax,pchar,ppow,ppEnhancements,eDetail,lang);

			if(pchCost && pchCostSecondary)
				langFormatGameMessageKey(lang,&pAutoDescPower->pchCost,"AutoDesc.PowerDef.Cost.Dual",STRFMT_STRING("Cost",pchCost),STRFMT_STRING("CostSecondary",pchCostSecondary),STRFMT_END);
			else if(pchCost)
				estrCopy(&pAutoDescPower->pchCost,&pchCost);
			else if(pchCostSecondary)
				estrCopy(&pAutoDescPower->pchCost,&pchCostSecondary);

			if(pchCostPeriodic && pchCostPeriodicSecondary)
				langFormatGameMessageKey(lang,&pAutoDescPower->pchCostPeriodic,"AutoDesc.PowerDef.Cost.Dual",STRFMT_STRING("Cost",pchCostPeriodic),STRFMT_STRING("CostSecondary",pchCostPeriodicSecondary),STRFMT_END);
			else if(pchCostPeriodic)
				estrCopy(&pAutoDescPower->pchCostPeriodic,&pchCostPeriodic);
			else if(pchCostPeriodicSecondary)
				estrCopy(&pAutoDescPower->pchCostPeriodic,&pchCostPeriodicSecondary);

			estrDestroy(&pchCost);
			estrDestroy(&pchCostPeriodic);
			estrDestroy(&pchCostSecondary);
			estrDestroy(&pchCostPeriodicSecondary);
		}
		else if(pdef->pExprCost || pdef->pExprCostPeriodic)
		{
			AutoDescPowerCost(iPartitionIdx,pdef,false,&pAutoDescPower->pchCost,&pAutoDescPower->pchCostPeriodic,&pAutoDescPower->fCostMin,&pAutoDescPower->fCostMax,&pAutoDescPower->fCostPeriodicMin,&pAutoDescPower->fCostPeriodicMax,pchar,ppow,ppEnhancements,eDetail,lang);
		}

		// Target Arc
		if(pdef->fTargetArc>0)
		{
			AutoDescPowerTargetArc(pdef,&pAutoDescPower->pchTargetArc,lang);
		}

		// Power Tags
		if(pdef->tags.piTags)
		{
			eaiCopy(&pAutoDescPower->eaPowerTags, &pdef->tags.piTags);
		}
	}

	if((eHeader==kAutoDescPowerHeader_Full || eHeader==kAutoDescPowerHeader_Apply || eHeader==kAutoDescPowerHeader_ApplySelf)
		&& !(eDetail<kAutoDescDetail_Maximum && eHeader!=kAutoDescPowerHeader_Full && pdef->eEffectArea==kEffectArea_Character))
	{
		// Targets
		if(pdef->eType==kPowerType_Enhancement)
		{
			if(pdef->pExprEnhanceAttach)
			{
				AutoDescPowerEnhanceAttach(pdef,&pAutoDescPower->pchEnhanceAttach,lang);

				if(ppow && pchar)
				{
					AutoDescPowerEnhanceAttached(pdef,&pAutoDescPower->pchEnhanceAttached,pchar,ppow,lang);
				}
			}

			if(pdef->pExprEnhanceApply)
			{
				langFormatGameMessageKey(lang,&pAutoDescPower->pchEnhanceApply,"AutoDesc.PowerDef.EnhanceApply",STRFMT_END);
			}
		}
		else if(pdef->eType!=kPowerType_Innate)
		{
			if(pdef->eType==kPowerType_Combo)
				bSelf = false;
			else
				bSelf = AutoDescPowerTarget(pdef,&pAutoDescPower->pchTarget,&pAutoDescPower->iMaxTargets,eHeader,lang);
		}
		else
		{
			bSelf = true;
		}

		// Range
		if(pdef->eType!=kPowerType_Innate 
			&& pdef->eType!=kPowerType_Enhancement
			&& pdef->eType!=kPowerType_Combo
			&& (pdef->fRange>0
				|| pdef->eEffectArea==kEffectArea_Sphere
				|| pdef->eEffectArea==kEffectArea_Cylinder
				|| pdef->eEffectArea==kEffectArea_Cone
				|| pdef->eEffectArea==kEffectArea_Team
				|| (pafx && pafx->pLunge))
			&& !bSelf)
		{
			WorldRegionType eRegionType = powerdef_GetBestRegionType(pdef);
			RegionRules *pRegionRules = getRegionRulesFromRegionType(eRegionType);
			AutoDescPowerRange(iPartitionIdx,pdef,&pAutoDescPower->pchRange,&pAutoDescPower->fRangeMin,&pAutoDescPower->fRangeMax,&pAutoDescPower->fAreaMin,&pAutoDescPower->fAreaMax,&pAutoDescPower->fInnerAreaMin,&pAutoDescPower->fInnerAreaMax,lang,pRegionRules ? pRegionRules->eDefaultMeasurement : kMeasurementType_Base,pRegionRules ? pRegionRules->eMeasurementSize : kMeasurementSize_Base, pRegionRules ? pRegionRules->fDefaultDistanceScale : 1.0f);
		}
	}

	// AttribMods
	s = eaSize(&pdef->ppOrderedMods);
	for(i=0; i<s; i++)
	{
		AttribModDef *pmoddef = pdef->ppOrderedMods[i];
		if((pmoddef->uiAutoDescKey && bFullDescription)
			|| (!pmoddef->bDerivedInternally
				&& (eDetail==kAutoDescDetail_Maximum
					|| !(ATTRIB_MAXIMUM_DETAIL_ONLY(pmoddef->offAttrib) || pmoddef->bAutoDescDisabled))
				&& (pmoddef->offAttrib!=kAttribType_Null 
					|| isDevelopmentMode())))
		{
			if(pdef->eType==kPowerType_Enhancement || !pmoddef->bEnhancementExtension)
			{
				eaPush(&ppMods,pmoddef);

				if(pmoddef->eTarget==kModTarget_Target)
					bModsTargetTarget = true;
				else
					bModsTargetSelf = true;
			}
			else if(pmoddef->pExprRequires || ATTRIB_ASPECT_UNINTEGRATED(pmoddef->offAttrib,pmoddef->offAspect))
			{
				AutoDescModScale *pModScale = calloc(1, sizeof(AutoDescModScale));
				pModScale->pmodDef = pmoddef;
				pModScale->fScale = 1;
				pModScale->iLevel = iLevelPower;
				eaPush(&ppModsEnhancements, pModScale);
			}
		}
	}

	bModsTargetMixed = bModsTargetSelf && bModsTargetTarget;

	s = eaSize(&ppMods);
	eaQSort(ppMods,SortAttribModDefsAutoDesc);
	for(i=0; i<s; i++)
	{
		AutoDescAttribMod *pAutoDescAttribMod = StructCreate(parse_AutoDescAttribMod);
		AutoDescAttribModDef(iPartitionIdx,ppMods[i],pAutoDescAttribMod,lang,pdef,pchar,ppow,ppEnhancements,iLevelPower,bIncludeStrength,bFullDescription,bModsTargetMixed,iDepth,0,fPowerAppsPerSecond,eHeader,eDetail,pAutoDescAttribModEvent,pppPowerDefsDescribed,pppchFootnoteMsgKeys,pExtract);
		eaPush(&pAutoDescPower->ppAttribMods,pAutoDescAttribMod);
	}
	eaDestroy(&ppMods);

	// AttribMods from Enhancements
	if(ppEnhancements && pdef->eType!=kPowerType_Enhancement && pdef->eType!=kPowerType_Combo)
	{
		s = eaSize(&ppEnhancements);
		for(i=0; i<s; i++)
		{
			Power *ppowEnhancement = ppEnhancements[i];
			PowerDef *pdefEnhancement = GET_REF(ppowEnhancement->hDef);
			S32 iLevelEnhancement = pdefEnhancement->bEnhanceCopyLevel ? iLevelPower : POWERLEVEL(ppowEnhancement,iLevel);
			if(pdefEnhancement)
			{
				int j, t = eaSize(&pdefEnhancement->ppOrderedMods);
				for(j=0; j<t; j++)
				{
					AttribModDef *pmoddef = pdefEnhancement->ppOrderedMods[j];
					if(!pmoddef->bDerivedInternally
						&& (eDetail==kAutoDescDetail_Maximum
							|| !(ATTRIB_MAXIMUM_DETAIL_ONLY(pmoddef->offAttrib) || pmoddef->bAutoDescDisabled))
						&& (pmoddef->offAttrib!=kAttribType_Null 
							|| isDevelopmentMode()))
					{
						if(pmoddef->bEnhancementExtension)
						{
							// TODO(JW): AutoDesc: Should this pass the enhancement powerdef or what?
							AutoDescAttribMod *pAutoDescAttribMod = StructCreate(parse_AutoDescAttribMod);
							pAutoDescAttribMod->bEnhancementExtension = true;
							AutoDescAttribModDef(iPartitionIdx,pdefEnhancement->ppOrderedMods[j],pAutoDescAttribMod,lang,pdef,pchar,ppow,ppEnhancements,iLevelEnhancement,bIncludeStrength,false,bModsTargetMixed,iDepth,0,fPowerAppsPerSecond,eHeader,eDetail,pAutoDescAttribModEvent,pppPowerDefsDescribed,pppchFootnoteMsgKeys,pExtract);
							eaPush(&pAutoDescPower->ppAttribMods,pAutoDescAttribMod);
						}
						else if(pmoddef->pExprRequires || ATTRIB_ASPECT_UNINTEGRATED(pmoddef->offAttrib,pmoddef->offAspect))
						{
							AutoDescModScale *pModScale = calloc(1, sizeof(AutoDescModScale));
							pModScale->pmodDef = pmoddef;
							pModScale->fScale = 1;
							pModScale->iLevel = iLevelEnhancement;
							eaPush(&ppModsEnhancements, pModScale);
						}
					}
				}
			}
		}
	}

	// Non-integrated Enhancement mods
	if(ppModsEnhancements)
	{
		CharacterClass *pClass = pchar ? character_GetClassCurrent(pchar) : NULL;
		s = eaSize(&ppModsEnhancements);
		eaQSort(ppModsEnhancements,SortAttribModScaleAutoDesc);
		for(i=0; i<s; i++)
		{
			AutoDescAttribMod *pAutoDescAttribMod = StructCreate(parse_AutoDescAttribMod);
			F32 fMag = 0;
			Message* pCustomMsg = NULL;
			S32 bRequires = !!ppModsEnhancements[i]->pmodDef->pExprRequires;
			AttribType eType = 0;
			
			// Copy the key
			pAutoDescAttribMod->iKey = (S32)ppModsEnhancements[i]->pmodDef->uiAutoDescKey;

			combateval_ContextSetupSimple(pchar,ppModsEnhancements[i]->iLevel,ppow ? ppow->pSourceItem : NULL);
			fMag = moddef_GetMagnitude(iPartitionIdx,ppModsEnhancements[i]->pmodDef,pClass,ppModsEnhancements[i]->iLevel,1,false);
			fMag *= ppModsEnhancements[i]->fScale;
			pCustomMsg = GET_REF(ppModsEnhancements[i]->pmodDef->msgAutoDesc.hMessage);

			if(ppModsEnhancements[i]->pmodDef->offAttrib == kAttribType_DynamicAttrib)
			{
				DynamicAttribParams *pParams = (DynamicAttribParams*)ppModsEnhancements[i]->pmodDef->pParams;
				if (pParams)
				{
					eType = (AttribType)combateval_EvalNew(iPartitionIdx, pParams->pExprAttrib, kCombatEvalContext_Simple, NULL);
				}
			}

			while(i+1<s && ppModsEnhancements[i+1]->pmodDef->offAttrib==ppModsEnhancements[i]->pmodDef->offAttrib && 
				ppModsEnhancements[i+1]->pmodDef->offAspect==ppModsEnhancements[i]->pmodDef->offAspect)
			{
				F32 fMagAdd;

				// If this is a DynamicAttrib, we need to do a little more evaluation before
				// determining that these two mod enhancements are the same
				if (ppModsEnhancements[i]->pmodDef->offAttrib == kAttribType_DynamicAttrib)
				{
					DynamicAttribParams *pParams = (DynamicAttribParams*)ppModsEnhancements[i+1]->pmodDef->pParams;
					if (pParams)
					{
						AttribType eSecondType = (AttribType)combateval_EvalNew(iPartitionIdx, pParams->pExprAttrib, kCombatEvalContext_Simple, NULL);

						if (eType != eSecondType)
						{
							break;
						}
					}
				}

				i++;
				fMagAdd = moddef_GetMagnitude(iPartitionIdx,ppModsEnhancements[i]->pmodDef,pClass,ppModsEnhancements[i]->iLevel,1,false);
				fMagAdd *= ppModsEnhancements[i]->fScale;
				fMag += fMagAdd;
				// Use the first custom message found on the attribmods used to build the magnitude
				if(!pCustomMsg && IS_HANDLE_ACTIVE(ppModsEnhancements[i]->pmodDef->msgAutoDesc.hMessage))
				{
					pCustomMsg = GET_REF(ppModsEnhancements[i]->pmodDef->msgAutoDesc.hMessage);
				}
				bRequires |= (!!ppModsEnhancements[i]->pmodDef->pExprRequires);
			}

			AutoDescAttribModDefInnate(iPartitionIdx, ppModsEnhancements[i]->pmodDef,NULL,pAutoDescAttribMod,NULL,fMag,lang,eDetail,pCustomMsg);

			eaPush(&pAutoDescPower->ppAttribModsEnhancements,pAutoDescAttribMod);

			// Requires footnote for non-custom
			if(bRequires && (!pCustomMsg || eDetail==kAutoDescDetail_Maximum))
			{
				pAutoDescAttribMod->pchRequires = langTranslateMessageKey(lang,"AutoDesc.AttribModDef.Requires");
				if(pppchFootnoteMsgKeys)
				{
					eaPushUnique(pppchFootnoteMsgKeys,"AutoDesc.AttribModDef.Requires.Footnote");
				}
			}
		}
		eaDestroyEx(&ppModsEnhancements,NULL);
	}

	// Add all keyed AttribMods to the indexed copy
	if(eaSize(&pAutoDescPower->ppAttribMods) || eaSize(&pAutoDescPower->ppAttribModsEnhancements))
	{
		eaIndexedEnable(&pAutoDescPower->ppAttribModsIndexed,parse_AutoDescAttribMod);
		s = eaSize(&pAutoDescPower->ppAttribMods);
		for(i=0; i<s; i++)
		{
			if(pAutoDescPower->ppAttribMods[i]->iKey)
				eaIndexedAdd(&pAutoDescPower->ppAttribModsIndexed,pAutoDescPower->ppAttribMods[i]);
		}
		s = eaSize(&pAutoDescPower->ppAttribModsEnhancements);
		for(i=0; i<s; i++)
		{
			if(pAutoDescPower->ppAttribModsEnhancements[i]->iKey)
				eaIndexedAdd(&pAutoDescPower->ppAttribModsIndexed,pAutoDescPower->ppAttribModsEnhancements[i]);
		}
	}

	// Combos
	s = eaSize(&pdef->ppOrderedCombos);
	for(i=s-1; i>=0; i--)
	{
		PowerCombo *pcombo = pdef->ppOrderedCombos[i];
		PowerDef *pdefCombo = GET_REF(pcombo->hPower);

		if(pdefCombo)
		{
			AutoDescPower *pAutoDescPowerCombo = StructCreate(parse_AutoDescPower);

			F32 fChargeRequired = pdefCombo ? pcombo->fPercentChargeRequired * pdefCombo->fTimeCharge : 0;

			AutoDescPowerComboRequires(pdef,&pAutoDescPowerCombo->pchComboRequires,lang,i);

			if(fChargeRequired > 0)
			{
				langFormatGameMessageKey(lang,&pAutoDescPowerCombo->pchComboCharge,"AutoDesc.PowerDef.ComboCharge",STRFMT_FLOAT("Time",fChargeRequired),STRFMT_END);
			}

			if(ppow && eaSize(&ppow->ppSubPowers) > i && ppow->ppSubPowers[i] && GET_REF(ppow->ppSubPowers[i]->hDef)==pdefCombo)
			{
				Power *ppowCombo = ppow->ppSubPowers[i];
				static Power **s_ppEnhancementsCombo = NULL;
				AutoDescEnhancements(iPartitionIdx,pchar,ppowCombo,&s_ppEnhancementsCombo);
				AutoDescPowerDefInternal(iPartitionIdx,pdefCombo,ppchDesc,pAutoDescPowerCombo,cpchLine,cpchModIndent,cpchModPrefix,kAutoDescPowerHeader_Full,pchar,ppowCombo,s_ppEnhancementsCombo,iLevel,bIncludeStrength,bFullDescription,iDepth,eDetail,pAutoDescAttribModEvent,NULL,pppchFootnoteMsgKeys,pExtract, pchPowerAutoDescMessageKey);
				eaClearFast(&s_ppEnhancementsCombo);
			}
			else
			{
				AutoDescPowerDefInternal(iPartitionIdx,pdefCombo,ppchDesc,pAutoDescPowerCombo,cpchLine,cpchModIndent,cpchModPrefix,kAutoDescPowerHeader_Full,pchar,NULL,NULL,iLevel,bIncludeStrength,bFullDescription,iDepth,eDetail,pAutoDescAttribModEvent,NULL,pppchFootnoteMsgKeys,pExtract, pchPowerAutoDescMessageKey);
			}
			eaPush(&pAutoDescPower->ppPowersCombo,pAutoDescPowerCombo);
		}
	}

	// Add all child Powers to the indexed copy
	if((s = eaSize(&pAutoDescPower->ppPowersCombo)))
	{
		eaIndexedEnable(&pAutoDescPower->ppPowersIndexed,parse_AutoDescPower);
		for(i=s-1; i>=0; i--)
			eaIndexedAdd(&pAutoDescPower->ppPowersIndexed,pAutoDescPower->ppPowersCombo[i]);
	}

	// Describe all Enhancements in full and add them to the indexed copy if bFullDescription is enabled
	if(bFullDescription)
	{
		for(i=eaSize(&ppEnhancements)-1; i>=0; i--)
		{
			Power *ppowEnhancement = ppEnhancements[i];
			PowerDef *pdefEnhancement = GET_REF(ppowEnhancement->hDef);
			if(pdefEnhancement)
			{
				AutoDescPower *pAutoDescPowerEnhancement = StructCreate(parse_AutoDescPower);
				int iLevelEnhancement = pdefEnhancement->bEnhanceCopyLevel ? iLevelPower : POWERLEVEL(ppowEnhancement,iLevel);
				AutoDescPowerDefInternal(iPartitionIdx,pdefEnhancement,NULL,pAutoDescPowerEnhancement,cpchLine,cpchModIndent,cpchModPrefix,kAutoDescPowerHeader_Full,pchar,ppowEnhancement,NULL,iLevelEnhancement,bIncludeStrength,bFullDescription,0,eDetail,NULL,NULL,pppchFootnoteMsgKeys,pExtract, pchPowerAutoDescMessageKey);
				eaPush(&pAutoDescPower->ppPowersEnhancements,pAutoDescPowerEnhancement);
			}
		}

		if((s = eaSize(&pAutoDescPower->ppPowersEnhancements)))
		{
			eaIndexedEnable(&pAutoDescPower->ppPowersIndexed,parse_AutoDescPower);
			for(i=s-1; i>=0; i--)
				eaIndexedAdd(&pAutoDescPower->ppPowersIndexed,pAutoDescPower->ppPowersEnhancements[i]);
		}
	}

	if(ppPowerDefsDescribedLocal)
	{
		eaDestroy(&ppPowerDefsDescribedLocal);
	}

	if(ppchFootnoteMsgKeysLocal)
	{
		for(i=0; i<eaSize(&ppchFootnoteMsgKeysLocal); i++)
		{
			char *pchFootnote = NULL;
			estrAppend2(&pchFootnote,langTranslateMessageKey(lang,ppchFootnoteMsgKeysLocal[i]));
			eaPush(&pAutoDescPower->ppchFootnotes,pchFootnote);
		}
		eaDestroy(&ppchFootnoteMsgKeysLocal);
	}

	if(bLocalAutoDescPower)
	{
		// Create default description
		if(ppchDesc)
			AutoDescPowerDefault(pAutoDescPower,ppchDesc,cpchLine,cpchModIndent,cpchModPrefix,iDepth,lang);

		StructDestroy(parse_AutoDescPower, pAutoDescPower);
	}
	else if(eDetail!=kAutoDescDetail_Maximum && IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage))
	{
		langFormatGameDisplayMessage(lang,&pAutoDescPower->pchCustom,&pdef->msgAutoDesc,
			STRFMT_STRUCT("p",pAutoDescPower,parse_AutoDescPower),
			STRFMT_END);
	}

	// Undo horrible hack to fix TableApp function during autodescription
	g_CombatEvalOverrides.iAutoDescTableAppHack = 0;

	// Restore old eval error suppression
	g_bCombatEvalSuppressErrors = bSuppressErrorsOld;
}

// TODO(JW): AutoDesc: Design reasonable API for this
void powerdef_AutoDesc(int iPartitionIdx,
					   PowerDef *pdef,
					   char **ppchDesc,
					   AutoDescPower *pAutoDescPower,
					   const char *cpchLine,
					   const char *cpchModIndent,
					   const char *cpchModPrefix,
					   Character *pchar,
					   Power *ppow,
					   Power **ppEnhancements,
					   int iLevel,
					   int bIncludeStrength,
					   AutoDescDetail eDetail,
					   GameAccountDataExtract *pExtract,
					   const char *pchPowerAutoDescMessageKey)
{
	AutoDescPowerDefInternal(iPartitionIdx,
		pdef,
		ppchDesc,
		pAutoDescPower,
		cpchLine,
		cpchModIndent,
		cpchModPrefix,
		kAutoDescPowerHeader_Full,
		pchar,
		ppow,
		ppEnhancements,
		iLevel,
		bIncludeStrength,
		false,
		0,
		eDetail,
		NULL,
		NULL,
		NULL,
		pExtract,
		pchPowerAutoDescMessageKey);
}

void powerdefs_AutoDescInnateMods(int iPartitionIdx,
								  Item * pItem,
								  SA_PARAM_NN_VALID PowerDef **ppdefs,
								  SA_PARAM_NN_VALID F32 *pfScales,
								  SA_PARAM_NN_VALID char **ppchDesc,
								  SA_PARAM_OP_STR const char *cpchKey,
								  SA_PARAM_OP_VALID Character *pchar,
								  S32 *eaSlotRequired,
								  int iLevel,
								  int bIncludeStrength,
								  ItemGemType eActiveGemSlotType,
								  AutoDescDetail eDetail)
{
	powerdefs_GetAutoDescInnateMods(iPartitionIdx, pItem,ppdefs, pfScales, ppchDesc, NULL, cpchKey, pchar, eaSlotRequired,-1, iLevel, bIncludeStrength, eActiveGemSlotType, eDetail);
}

void powerdefs_GetAutoDescInnateMods(int iPartitionIdx,
									 Item * pItem,
									 PowerDef **ppdefs,
								  F32 *pfScales,
								  char **ppchDesc,
								  AutoDescInnateModDetails ***pppDetails,
								  const char *cpchKey,
								  Character *pchar,
								  S32 *eaSlotRequired,
								  int iGemPowersBegin,
								  int iLevel,
								  int bIncludeStrength,
								  S32 eActiveGemSlotType,
								  AutoDescDetail eDetail)
{
	Language lang = langGetCurrent();

	int i,j,s,used;
	int bSuppressErrorsOld;
	int iGemLevel = iGemPowersBegin > -1 ? item_GetGemPowerLevel(pItem) : iLevel;
	int iGemType = 0;
	ItemDef *pDef = SAFE_GET_REF(pItem, hItem);
	ItemType eItemType = SAFE_MEMBER(pDef, eType);

	CharacterClass *pClass = pchar ? character_GetClassCurrent(pchar) : NULL;

	char *pchTemp = NULL;
	char *pchTemp2 = NULL;

	estrStackCreate(&pchTemp);
	estrStackCreate(&pchTemp2);

	// Save old eval error suppression
	bSuppressErrorsOld = g_bCombatEvalSuppressErrors;
	g_bCombatEvalSuppressErrors = true;

	combateval_ContextSetupSimple(pchar, iLevel, pItem);

	// AttribMods
	{
		AutoDescModScale **ppMods = NULL;

		// Go through each PowerDef and accumulate all the usable AttribModDefs
		for(j=eaSize(&ppdefs)-1; j>=0; j--)
		{
			PowerDef *pdef = ppdefs[j];
			F32 fScale = pfScales[j];

			s = eaSize(&pdef->ppOrderedMods);
			for(i=0; i<s; i++)
			{
				AttribModDef *pmoddef = pdef->ppOrderedMods[i];
				if(!pmoddef->bDerivedInternally
					&& (eDetail==kAutoDescDetail_Maximum
						|| !(ATTRIB_MAXIMUM_DETAIL_ONLY(pmoddef->offAttrib) || pmoddef->bAutoDescDisabled))
					&& (pmoddef->offAttrib!=kAttribType_Null 
						|| isDevelopmentMode()))
				{
					AutoDescModScale *pModScale = calloc(1, sizeof(AutoDescModScale));
					pModScale->pmodDef = pmoddef;
					pModScale->fScale = fScale;

					if(iGemPowersBegin > -1 && iGemPowersBegin <= j)
					{
						pModScale->iLevel = iGemLevel;
					}
					else
					{
						pModScale->iLevel = iLevel;
					}

					
					if(ea32Size(&eaSlotRequired))
						pModScale->iRequiredGemSlot = eaSlotRequired[j];
					eaPush(&ppMods, pModScale);
				}
			}
		}


		s = eaSize(&ppMods);
		eaQSort(ppMods,SortAttribModScaleAutoDesc);
		used = 0;
		for(i=0; i<s; i++)
		{
			F32 fMag = 0;
			F32 fGemMag = 0;
			bool bHasGem = false;
			bool bOnlyGem = false;
			Message* pCustomMsg = NULL;
			AutoDescInnateModDetails *pDetails = pppDetails ? eaGetStruct(pppDetails, parse_AutoDescInnateModDetails, used++) : NULL;
			AttribType eType = 0;
			estrClear(&pchTemp);

			fMag = mod_GetInnateMagnitude(iPartitionIdx,ppMods[i]->pmodDef,pchar,pClass,ppMods[i]->iLevel,1);
			fMag *= ppMods[i]->fScale;
			pCustomMsg = GET_REF(ppMods[i]->pmodDef->msgAutoDesc.hMessage);

			if (!!ppMods[i]->iRequiredGemSlot)
			{
				fGemMag = fMag;
				bHasGem = true;
				bOnlyGem = true;
			}

			if(ppMods[i]->pmodDef->offAttrib == kAttribType_DynamicAttrib)
			{
				DynamicAttribParams *pParams = (DynamicAttribParams*)ppMods[i]->pmodDef->pParams;
				if (pParams)
				{
					eType = (AttribType)combateval_EvalNew(iPartitionIdx, pParams->pExprAttrib, kCombatEvalContext_Simple, NULL);
				}
			}

			while(i+1<s && ppMods[i+1]->pmodDef->offAttrib==ppMods[i]->pmodDef->offAttrib && 
				ppMods[i+1]->pmodDef->offAspect==ppMods[i]->pmodDef->offAspect)
			{
				F32 fMagAdd;

				// If this is a DynamicAttrib, we need to do a little more evaluation before
				// determining that these two mod enhancements are the same
				if (ppMods[i]->pmodDef->offAttrib == kAttribType_DynamicAttrib)
				{
					DynamicAttribParams *pParams = (DynamicAttribParams*)ppMods[i+1]->pmodDef->pParams;
					if (pParams)
					{
						AttribType eSecondType = (AttribType)combateval_EvalNew(iPartitionIdx, pParams->pExprAttrib, kCombatEvalContext_Simple, NULL);

						if (eType != eSecondType)
						{
							break;
						}
					}
				}

				i++;
				fMagAdd = mod_GetInnateMagnitude(iPartitionIdx,ppMods[i]->pmodDef,pchar,pClass,ppMods[i]->iLevel,1);
				fMagAdd *= ppMods[i]->fScale;
				if (!ppMods[i]->iRequiredGemSlot)
				{
					fMag += fMagAdd;
					bOnlyGem = false;
				}
				else
				{
					fGemMag += fMagAdd;
					bHasGem = true;
				}
				// Use the first custom message found on the attribmods used to build the magnitude
				if(!pCustomMsg && IS_HANDLE_ACTIVE(ppMods[i]->pmodDef->msgAutoDesc.hMessage))
				{
					pCustomMsg = GET_REF(ppMods[i]->pmodDef->msgAutoDesc.hMessage);
				}
			}

			if (gConf.bRoundItemStatsOnApplyToChar)
			{
				fMag = (F32)round(fMag);
				fGemMag = (F32)round(fGemMag);
			}

			if (ppchDesc)
			{
				if(cpchKey)
				{
					// Custom just generates the description and passes it along
					estrClear(&pchTemp2);
					AutoDescAttribModDefInnate(iPartitionIdx, ppMods[i]->pmodDef,&pchTemp2,NULL,NULL,fMag,lang,eDetail,pCustomMsg);

					langFormatMessageKey(lang,&pchTemp,cpchKey,
						STRFMT_STRING("Description",pchTemp2),
						STRFMT_INT("IsEquippable", eItemType == kItemType_Weapon || eItemType == kItemType_Upgrade),
						STRFMT_INT("IsUseable", eItemType == kItemType_Device),
						STRFMT_INT("IsActive", true),
						STRFMT_INT("IsGemPower", false),
						STRFMT_STRING("GemSlotRequired", StaticDefineInt_FastIntToString(ItemGemTypeEnum, ppMods[i]->iRequiredGemSlot)),
						STRFMT_STRING("SlotTypeName", StaticDefineGetTranslatedMessage(ItemGemTypeEnum, ppMods[i]->iRequiredGemSlot)),
						STRFMT_STRING("DescriptionLong",attrib_AutoDescDescLong(ppMods[i]->pmodDef->offAttrib,lang)),
						STRFMT_END);
				}
				else
				{
					// Default is to add the line break if necessary, and a breaking space prefix
					LineFeed(&pchTemp,i?"<br>":NULL,NULL,0);
					estrAppend2(&pchTemp,"<bsp>");

					AutoDescAttribModDefInnate(iPartitionIdx, ppMods[i]->pmodDef,&pchTemp,NULL,NULL,fMag,lang,eDetail,pCustomMsg);
				}

				if (!bOnlyGem)
				{
					estrAppend(ppchDesc,&pchTemp);
				}

				//Now do gem components
				if (bHasGem)
				{
					estrClear(&pchTemp);

					if(cpchKey)
					{
						// Custom just generates the description and passes it along
						estrClear(&pchTemp2);
						AutoDescAttribModDefInnate(iPartitionIdx, ppMods[i]->pmodDef,&pchTemp2,NULL,NULL,fGemMag,lang,eDetail,pCustomMsg);

						langFormatMessageKey(lang,&pchTemp,cpchKey,
							STRFMT_STRING("Description",pchTemp2),
							STRFMT_INT("IsEquippable", !ppMods[i]->iRequiredGemSlot && (eItemType == kItemType_Weapon || eItemType == kItemType_Upgrade)),
							STRFMT_INT("IsUseable", !ppMods[i]->iRequiredGemSlot && (eItemType == kItemType_Device)),
							STRFMT_INT("IsActive", !eActiveGemSlotType || !ppMods[i]->iRequiredGemSlot || (eActiveGemSlotType == ppMods[i]->iRequiredGemSlot)),
							STRFMT_INT("IsGemPower", true),
							STRFMT_STRING("GemSlotRequired", StaticDefineInt_FastIntToString(ItemGemTypeEnum, ppMods[i]->iRequiredGemSlot)),
							STRFMT_STRING("SlotTypeName", StaticDefineGetTranslatedMessage(ItemGemTypeEnum, ppMods[i]->iRequiredGemSlot)),
							STRFMT_STRING("DescriptionLong",attrib_AutoDescDescLong(ppMods[i]->pmodDef->offAttrib,lang)),
							STRFMT_END);
					}
					else
					{
						// Default is to add the line break if necessary, and a breaking space prefix
						LineFeed(&pchTemp,i?"<br>":NULL,NULL,0);
						estrAppend2(&pchTemp,"<bsp>");

						AutoDescAttribModDefInnate(iPartitionIdx, ppMods[i]->pmodDef,&pchTemp,NULL,NULL,fGemMag,lang,eDetail,pCustomMsg);
					}

					estrAppend(ppchDesc,&pchTemp);
				}
			}
			else if (pDetails)
			{
				AutoDescAttribModDefInnate(iPartitionIdx, ppMods[i]->pmodDef,NULL,NULL,pDetails,fMag,lang,eDetail,NULL);

				if(ppMods[i]->iRequiredGemSlot)
				{
					estrClear(&pchTemp2);
					estrPrintf(&pchTemp2,"AutoDesc.GemType.%s",StaticDefineIntRevLookup(ItemGemTypeEnum,ppMods[i]->iRequiredGemSlot));

					estrClear(&pDetails->pchRequiredGemSlot);
					langFormatMessageKey(lang,&pDetails->pchRequiredGemSlot,pchTemp2,STRFMT_END);
				}
				else
				{
					pDetails->pchRequiredGemSlot = NULL;
				}
			}
		}
		if (pppDetails)
			eaSetSizeStruct(pppDetails, parse_AutoDescInnateModDetails, used);
		eaDestroyEx(&ppMods,NULL);
	}


	estrDestroy(&pchTemp);
	estrDestroy(&pchTemp2);

	// Restore old eval error suppression
	g_bCombatEvalSuppressErrors = bSuppressErrorsOld;
}

static void powerdefs_GetBasicAttribsFromItemPowers(int iPartitionIdx,
													SA_PARAM_OP_VALID Character *pChar,
													SA_PARAM_OP_VALID CharacterClass *pClass,
													Item *pItem,
													AutoDescModScale ***peaMods,
													AutoDescDetail eDetail)
{
	int iLevel, iPower, iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	S32 iNumMods = 0;

	iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pChar ? pChar->iLevelCombat : 1;

	combateval_ContextSetupSimple(pChar, iLevel, pItem);

	// get the list of attribs and their mags
	for(iPower = 0; iPower < iNumPowers; ++iPower)
	{
		PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
		
		if(pPowerDef && pPowerDef->eType==kPowerType_Innate &&
			(!pChar || item_ItemPowerActive(pChar->pEntParent, NULL, pItem, iPower)))
		{	// calculate the attribs for this power
			F32 fItemPowerScale = item_GetItemPowerScale(pItem, iPower);

			FOR_EACH_IN_EARRAY_FORWARDS(pPowerDef->ppOrderedMods, AttribModDef, pModDef)
			{
				if(!pModDef->bDerivedInternally
					&& (eDetail==kAutoDescDetail_Maximum || !(ATTRIB_MAXIMUM_DETAIL_ONLY(pModDef->offAttrib) || pModDef->bAutoDescDisabled))
					&& (pModDef->offAttrib!=kAttribType_Null || isDevelopmentMode()))
				{
					AutoDescModScale *pModScale = calloc(1, sizeof(AutoDescModScale));

					pModScale->pmodDef = pModDef;
					pModScale->fScale = fItemPowerScale;
					pModScale->iLevel = iLevel;
					eaPush(peaMods, pModScale);
				}
			}
			FOR_EACH_END
		}

	}


	// sort, evaluate and combine the mods 
	iNumMods = eaSize(peaMods);
	if (iNumMods)
	{
		S32 iMod;

		eaQSort(*peaMods, SortAttribModScaleAutoDesc);

		for (iMod = 0; iMod < iNumMods; ++iMod)
		{
			AutoDescModScale *pModScale = (*peaMods)[iMod];
			F32 fMag = mod_GetInnateMagnitude(iPartitionIdx, pModScale->pmodDef, pChar, pClass, iLevel, 1);
			fMag *= pModScale->fScale;


			while(	(iMod+1) < iNumMods && 
				(*peaMods)[iMod+1]->pmodDef->offAttrib == pModScale->pmodDef->offAttrib && 
				(*peaMods)[iMod+1]->pmodDef->offAspect == pModScale->pmodDef->offAspect)
			{	// this is the same mod, let's combine them
				F32 fMagAdd;
				AutoDescModScale *pNextMod = NULL; 

				iMod++;
				pNextMod = (*peaMods)[iMod];

				fMagAdd = mod_GetInnateMagnitude(iPartitionIdx,pNextMod->pmodDef, pChar ,pClass, pNextMod->iLevel, 1);
				fMagAdd *= pNextMod->fScale;
				
				fMag += fMagAdd;
			}

			if (gConf.bRoundItemStatsOnApplyToChar)
			{
				fMag = (F32)round(fMag);
			}

			pModScale->fMag = fMag;
		}

		// any mods that are zero, remove
		for (iMod = iNumMods - 1; iMod >= 0; --iMod)
		{
			AutoDescModScale *pModScale = (*peaMods)[iMod];
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '*pModScale'"
			if (pModScale->fMag == 0.f)
			{
				free(pModScale);
				eaRemove(peaMods, iMod);
			}
		}
	}
}

static int SortAttribModPositiveToNegative(const AutoDescModScale **ppModScaleA, const AutoDescModScale **ppModScaleB)
{
	if (((*ppModScaleA)->fMag > 0 && (*ppModScaleB)->fMag > 0) ||
		((*ppModScaleA)->fMag < 0 && (*ppModScaleB)->fMag < 0))
	{
		return SortAttribModScaleAutoDesc(ppModScaleA, ppModScaleB);
	}

	if ((*ppModScaleA)->fMag > 0)
	{
		return -1;
	}
	else
	{
		return 1;
	}
}

// Fills the string ppchDesc with the 
void powerdefs_GetAutoDescInnateModsDiff(	int iPartitionIdx,
											Item * pItem,
											Item * pOtherItem,
											char **ppchDesc,
											const char *cpchKey,
											Character *pChar,
											AutoDescDetail eDetail)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	ItemDef *pOtherItemDef = pOtherItem ? GET_REF(pOtherItem->hItem) : NULL;
	int iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	AutoDescModScale **eaItemDescMods = NULL;
	AutoDescModScale **eaOtherItemDescMods = NULL;
	CharacterClass *pClass = pChar ? character_GetClassCurrent(pChar) : NULL;
	bool bAddedModScale = false; 
	char *pchTemp = NULL;
	char *pchTemp2 = NULL;
	int bSuppressErrorsOld;
	Language lang = langGetCurrent();
	
	if (!pItemDef || !pOtherItemDef || !ppchDesc)
		return;

	// Save old eval error suppression
	bSuppressErrorsOld = g_bCombatEvalSuppressErrors;
	g_bCombatEvalSuppressErrors = true;

	powerdefs_GetBasicAttribsFromItemPowers(iPartitionIdx, pChar, pClass, pItem, &eaItemDescMods, eDetail);
	powerdefs_GetBasicAttribsFromItemPowers(iPartitionIdx, pChar, pClass, pOtherItem, &eaOtherItemDescMods, eDetail);
	
	// diff the arrays
	FOR_EACH_IN_EARRAY(eaOtherItemDescMods, AutoDescModScale, pOtherModScale)
	{
		bool bFound = false;

		// try and find the mod in the other list
		FOR_EACH_IN_EARRAY(eaItemDescMods, AutoDescModScale, pModScale)
		{
			if (pOtherModScale->pmodDef->offAttrib == pModScale->pmodDef->offAttrib &&
				pOtherModScale->pmodDef->offAspect == pModScale->pmodDef->offAspect)
			{// found, subtract this one from the other
				pOtherModScale->fMag = pOtherModScale->fMag - pModScale->fMag;
				pModScale->bProcessed = true;
				break;
			}
		}
		FOR_EACH_END
	}
	FOR_EACH_END

	// go through the eaItemDescMods and any that were not processed, move them to the other list
	FOR_EACH_IN_EARRAY(eaItemDescMods, AutoDescModScale, pModScale)
	{
		if (!pModScale->bProcessed && pModScale->fMag > 0.f)
		{
			bAddedModScale = true;
			pModScale->fMag = -pModScale->fMag;
			eaPush(&eaOtherItemDescMods, pModScale);
			eaRemoveFast(&eaItemDescMods, FOR_EACH_IDX(-, pModScale));
		}
	}
	FOR_EACH_END

	// go through the final list and remove any that are zero
	FOR_EACH_IN_EARRAY(eaOtherItemDescMods, AutoDescModScale, pOtherModScale)
	{
		if (pOtherModScale->fMag == 0.f)
		{
			free(pOtherModScale);
			eaRemove(&eaOtherItemDescMods, FOR_EACH_IDX(-, pOtherModScale));
		}
	}
	FOR_EACH_END

	eaQSort(eaOtherItemDescMods, SortAttribModPositiveToNegative);
	
	// now get the description for the remaining mods in the eaOtherItemDescMods list!
	eaDestroyEx(&eaItemDescMods, NULL);

	estrStackCreate(&pchTemp);
	estrStackCreate(&pchTemp2);

	FOR_EACH_IN_EARRAY_FORWARDS(eaOtherItemDescMods, AutoDescModScale, pModScale)
	{
		Message* pCustomMsg = NULL;
		pCustomMsg = GET_REF(pModScale->pmodDef->msgAutoDesc.hMessage);

		estrClear(&pchTemp);

		if(cpchKey)
		{
			S32 bAttribIsNegative = (pModScale->fMag < 0);

			// Custom just generates the description and passes it along
			estrClear(&pchTemp2);
			
			AutoDescAttribModDefInnate(iPartitionIdx, pModScale->pmodDef, &pchTemp2, NULL, NULL, pModScale->fMag, lang, eDetail, pCustomMsg);

			langFormatMessageKey(lang,&pchTemp,cpchKey,
									STRFMT_STRING("Description",pchTemp2),
									STRFMT_STRING("DescriptionLong",attrib_AutoDescDescLong(pModScale->pmodDef->offAttrib,lang)),
									STRFMT_INT("IsAttribNeg", bAttribIsNegative),
									STRFMT_END);
		}
		else
		{
			// Default is to add the line break if necessary, and a breaking space prefix
			LineFeed(	&pchTemp, 
						(FOR_EACH_IDX(-, pModScale) ? "<br>" : NULL),
						NULL,
						0);

			estrAppend2(&pchTemp,"<bsp>");

			AutoDescAttribModDefInnate(	iPartitionIdx,
										pModScale->pmodDef,
										&pchTemp,
										NULL,NULL,
										pModScale->fMag,
										lang,
										eDetail,
										pCustomMsg);
		}

		estrAppend(ppchDesc, &pchTemp);
	}
	FOR_EACH_END

	estrDestroy(&pchTemp);
	estrDestroy(&pchTemp2);
	eaDestroyEx(&eaOtherItemDescMods, NULL);

	// Restore old eval error suppression
	g_bCombatEvalSuppressErrors = bSuppressErrorsOld;
}


void AutoDesc_InnateAttribMods(int iPartitionIdx, Character *pchar, PowerDef *pdef, AutoDescAttribMod ***peaInfos)
{
	CharacterClass *pClass = pchar ? character_GetClassCurrent(pchar) : NULL;
	int iLevel = pchar ? pchar->iLevelCombat : 1;
	AutoDescDetail eDetail = kAutoDescDetail_Normal;
	Language lang = langGetCurrent();

	AttribModDef **ppMods = NULL;
	char *pchTemp = NULL;
	int s;
	int i;

	combateval_ContextSetupSimple(pchar, iLevel, NULL);

	s = eaSize(&pdef->ppOrderedMods);
	for(i=0; i<s; i++)
	{
		AttribModDef *pmoddef = pdef->ppOrderedMods[i];
		if(!pmoddef->bDerivedInternally
			&& (eDetail==kAutoDescDetail_Maximum
				|| !(ATTRIB_MAXIMUM_DETAIL_ONLY(pmoddef->offAttrib) || pmoddef->bAutoDescDisabled))
			&& (pmoddef->offAttrib!=kAttribType_Null 
				|| isDevelopmentMode()))
		{
			eaPush(&ppMods,pmoddef);
		}
	}

	s = eaSize(&ppMods);
	eaQSort(ppMods,SortAttribModDefsAutoDesc);

	for(i=0; i<s; i++)
	{
		F32 fMag = mod_GetInnateMagnitude(iPartitionIdx, ppMods[i], pchar, pClass, iLevel, 1);

		if(eaSize(peaInfos)<=i)
		{
			eaPush(peaInfos, StructCreate(parse_AutoDescAttribMod));
		}

		estrPrintf(&((*peaInfos)[i]->pchMagnitude), "%d", fMag < 1.0f ? 1 : (int)fMag);
		AutoDescAttribName(ppMods[i]->offAttrib, &((*peaInfos)[i]->pchAttribName), lang);
		AutoDescAttribDesc(ppMods[i]->offAttrib, &((*peaInfos)[i]->pchAttribDesc), lang);
		AutoDescAttribDescLong(ppMods[i]->offAttrib, &((*peaInfos)[i]->pchAttribDescLong), lang);
		(*peaInfos)[i]->offAttrib = ppMods[i]->offAttrib;
		(*peaInfos)[i]->offAspect = ppMods[i]->offAspect;
	}

	eaDestroy(&ppMods);

	while (eaSize(peaInfos) > s)
		StructDestroy(parse_AutoDescAttribMod, eaPop(peaInfos));
}

static void AutoDescSpecialAIAvoid(AttribModDef *pdef,
								   char **ppchDescription,
								   const char *pchMessageKey,
								   const char *pchSign,
								   const char *pchAttrib,
								   const char *pchTarget,
								   const char *pchMagnitude)
{
	F32 fRadius = 0;
	AIAvoidParams *pParams = (AIAvoidParams*)pdef->pParams;
	if(pParams)
	{
		fRadius = pParams->fRadius;
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_FLOAT("Radius",fRadius),
		STRFMT_END);
}

static void AutoDescSpecialAISoftAvoid(AttribModDef *pdef,
	char **ppchDescription,
	const char *pchMessageKey,
	const char *pchSign,
	const char *pchAttrib,
	const char *pchTarget,
	const char *pchMagnitude)
{
	F32 fRadius = 0;
	AISoftAvoidParams *pParams = (AISoftAvoidParams*)pdef->pParams;
	if(pParams)
	{
		fRadius = pParams->fRadius;
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_FLOAT("Radius",fRadius),
		STRFMT_END);
}

static void AutoDescSpecialAIThreat(AttribModDef *pdef,
								    char **ppchDescription,
								    const char *pchMessageKey,
								    const char *pchSign,
								    const char *pchAttrib,
								    const char *pchTarget,
								    const char *pchMagnitude)
{
	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);
}

static void AutoDescSpecialApplyPower(AttribModDef *pdef,
									  char **ppchDescription,
									  const char *pchMessageKey,
									  const char *pchSign,
									  const char *pchAttrib,
									  const char *pchTarget,
									  const char *pchMagnitude)
{
	const char *pchPower = TranslateMessageKey("AutoDesc.Unknown");
	const char *pchApplySource = pchPower;
	char *pchApplyTarget = NULL;
	ApplyPowerParams *pParams = (ApplyPowerParams*)pdef->pParams;
	estrStackCreate(&pchApplyTarget);
	if(pParams)
	{
		PowerDef *ppowdef = GET_REF(pParams->hDef);
		if(ppowdef)
		{
			pchPower = powerdef_GetLocalName(ppowdef);
		}

		switch(pParams->eSource)
		{
		case kApplyPowerEntity_ModOwner:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.AttribModOwner");
			break;
		case kApplyPowerEntity_ModSource:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.AttribModSource");
			break;
		case kApplyPowerEntity_ModSourceCreator:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.AttribModSourceCreator");
			break;
		case kApplyPowerEntity_ModTarget:
			pchApplySource = NULL;
			break;
		case kApplyPowerEntity_RandomNotSource:
		case kApplyPowerEntity_Random:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.RandomTarget");
			break;
		case kApplyPowerEntity_ClosestNotSource:
		case kApplyPowerEntity_ClosestNotSourceOrTarget:
		case kApplyPowerEntity_ClosestNotTarget:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.ClosestTarget");
			break;
		case kApplyPowerEntity_HeldObject:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.HeldObject");
			break;
		}

		if(pParams->eTarget!=pParams->eSource || pParams->eTarget==kApplyPowerEntity_RandomNotSource || pParams->eTarget==kApplyPowerEntity_Random)
		{
			switch(pParams->eTarget)
			{
			case kApplyPowerEntity_ModOwner:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModOwner"),STRFMT_END);
				break;
			case kApplyPowerEntity_ModSource:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSource"),STRFMT_END);
				break;
			case kApplyPowerEntity_ModSourceTargetDual:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSourceTargetDual"),STRFMT_END);
				break;
			case kApplyPowerEntity_ModSourceCreator:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSourceCreator"),STRFMT_END);
				break;
			case kApplyPowerEntity_ModTarget:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModTarget"),STRFMT_END);
				break;
			case kApplyPowerEntity_RandomNotSource:
			case kApplyPowerEntity_Random:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.RandomTarget"),STRFMT_END);
				break;
			case kApplyPowerEntity_ClosestNotSource:
			case kApplyPowerEntity_ClosestNotSourceOrTarget:
			case kApplyPowerEntity_ClosestNotTarget:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.ClosestTarget"),STRFMT_END);
				break;
			case kApplyPowerEntity_ApplicationTarget:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.ApplicationTarget"),STRFMT_END);
				break;
			}
		}
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Power",pchPower),
		STRFMT_STRING("ApplySource",pchApplySource),
		STRFMT_STRING("ApplyTarget",pchApplyTarget),
		STRFMT_STRING("Target",pchTarget),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);

	estrDestroy(&pchApplyTarget);
}

static void AutoDescSpecialAttribModDamage(AttribModDef *pdef,
										   char **ppchDescription,
										   const char *pchMessageKey,
										   const char *pchSign,
										   const char *pchAttrib,
										   const char *pchTarget,
										   const char *pchMagnitude,
										   Language lang)
{
	char *pchDamage = NULL;
	AttribModDamageParams *pParams = (AttribModDamageParams*)pdef->pParams;
	if(pParams)
	{
		estrStackCreate(&pchDamage);
		AutoDescAttribName(pParams->offattribDamageType, &pchDamage, lang);
	}

	langFormatMessageKey(lang,ppchDescription,pchMessageKey,
		STRFMT_STRING("Sign",pchSign),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_STRING("Damage",pchDamage),
		STRFMT_END);

	estrDestroy(&pchDamage);
}

static void AutoDescSpecialAttribModHeal(AttribModDef *pdef,
										 char **ppchDescription,
										 const char *pchMessageKey,
										 const char *pchSign,
										 const char *pchAttrib,
										 const char *pchTarget,
										 const char *pchMagnitude)
{
	const char *pchAspect = NULL;
	AttribModHealParams *pParams = (AttribModHealParams*)pdef->pParams;
	if(pParams)
	{
		pchAspect = StaticDefineIntRevLookup(AttribModHealAspectEnum,pParams->eAspect);
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Sign",pchSign),
		STRFMT_STRING("Aspect",pchAspect),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);
}

static void AutoDescSpecialAttribModExpire(AttribModDef *pdef,
										   char **ppchDescription,
										   const char *pchMessageKey,
										   const char *pchSign,
										   const char *pchAttrib,
										   const char *pchTarget,
										   const char *pchMagnitude)
{
	char *pchUpTo = NULL;
	if(pdef->pExprMagnitude)
	{
		FormatMessageKey(&pchUpTo,"AutoDesc.Common.UpToValue",
			STRFMT_STRING("UpTo",pchMagnitude),
			STRFMT_END);
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("UpTo",pchUpTo),
		STRFMT_END);
}

static void AutoDescSpecialAttribOverride(AttribModDef *pdef,
										  char **ppchDescription,
										  const char *pchMessageKey,
										  const char *pchSign,
										  const char *pchAttrib,
										  const char *pchTarget,
										  const char *pchMagnitude,
										  Language lang)
{
	const char *pchAspect = NULL;
	char *pchAttribOverride = NULL;
	AttribOverrideParams *pParams = (AttribOverrideParams*)pdef->pParams;
	estrStackCreate(&pchAttribOverride);
	if(pParams)
	{
		AutoDescAttribName(pParams->offAttrib,&pchAttribOverride,lang);
	}

	if(pdef->offAspect!=kAttribAspect_BasicAbs)
	{
		pchAspect = StaticDefineIntRevLookup(AttribAspectEnum,pdef->offAspect);
	}

	langFormatMessageKey(lang,ppchDescription,pchMessageKey,
		STRFMT_STRING("Aspect",pchAspect),
		STRFMT_STRING("Attrib",pchAttribOverride),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);

	estrDestroy(&pchAttribOverride);
}

static void AutoDescSpecialBecomeCritter(AttribModDef *pdef,
										 char **ppchDescription,
										 const char *pchMessageKey,
										 Language lang)
{
	const char *pchCritter = NULL;
	BecomeCritterParams *pParams = (BecomeCritterParams*)pdef->pParams;
	if(pParams)
	{
		CritterDef *pdefCritter = GET_REF(pParams->hCritter);
		if(pdefCritter)
		{
			pchCritter = langTranslateDisplayMessage(lang,pdefCritter->displayNameMsg);
		}
	}

	if(!pchCritter)
	{
		pchCritter = langTranslateMessageKey(lang,"AutoDesc.Unknown");
	}

	langFormatMessageKey(lang,ppchDescription,pchMessageKey,
		STRFMT_STRING("Entity",pchCritter),
		STRFMT_END);
}

static void AutoDescSpecialDamageTrigger(int iPartitionIdx, 
										 Character *pchar,
										 AttribModDef *pdef,
										 char **ppchDescription,
										 const char *pchMessageKey,
										 const char *pchSign,
										 const char *pchAttrib,
										 const char *pchTarget,
										 const char *pchMagnitude,
										 Language lang)
{
	const char *pchPower = langTranslateMessageKey(lang,"AutoDesc.Unknown");
	char *pchType = NULL;
	char *pchApplyTarget = NULL;
	char *pchCharges = NULL;
	char *pchChance = NULL;
	char *pchMessageKeyInternal = estrStackCreateFromStr(pchMessageKey);
	DamageTriggerParams *pParams = (DamageTriggerParams*)pdef->pParams;

	estrStackCreate(&pchType);
	estrStackCreate(&pchApplyTarget);

	if(pParams)
	{
		PowerDef *ppowdef = GET_REF(pParams->hDef);
		F32 fChance = 1.f;
		if(ppowdef)
		{
			pchPower = langTranslateDisplayMessage(lang,ppowdef->msgDisplayName);
		}

		AutoDescAttribName(pParams->offAttrib,&pchType,lang);

		switch(pParams->eTarget)
		{
		case kDamageTriggerEntity_DamageOwner:
			langFormatMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.DamageOwner"),STRFMT_END);
			break;
		case kDamageTriggerEntity_DamageSource:
			langFormatMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.DamageSource"),STRFMT_END);
			break;
		case kDamageTriggerEntity_DamageTarget:
			langFormatMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.DamageTarget"),STRFMT_END);
			break;
		case kDamageTriggerEntity_TriggerOwner:
			langFormatMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModOwner"),STRFMT_END);
			break;
		case kDamageTriggerEntity_TriggerSource:
			langFormatMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSource"),STRFMT_END);
			break;
		}

		if(pParams->bMagnitudeIsCharges)
		{
			estrStackCreate(&pchCharges);
			langFormatMessageKey(lang,&pchCharges,"AutoDesc.AttribModDef.Charges",STRFMT_STRING("Charges",pchMagnitude),STRFMT_END);
		}

		if(pParams->bOutgoing)
			estrAppend2(&pchMessageKeyInternal,".Outgoing");

		
		if(pParams->pExprChance)
		{
			fChance = character_TriggerAttribModCheckChance(iPartitionIdx, pchar, 0, pParams->pExprChance);
		}

		if(RefSystem_ReferentFromString(gMessageDict, "AutoDesc.AttribModDef.Trigger.Chance"))
		{
			estrStackCreate(&pchChance);

			if(pParams->pExprChance == NULL)
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.ChanceAlways", STRFMT_STRING("ApplySource", ""), STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
			}
			else
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.Chance", STRFMT_STRING("ApplySource", ""), STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
			}
		}
		else if(fChance != 1.f)
		{
			estrStackCreate(&pchChance);
			FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Chance", STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
		}
	}

	langFormatMessageKey(lang,ppchDescription,pchMessageKeyInternal,
		STRFMT_STRING("Power",pchPower),
		STRFMT_STRING("Type",pchType),
		STRFMT_STRING("ApplyTarget",pchApplyTarget),
		STRFMT_STRING("Target",pchTarget),
		STRFMT_STRING("Charges",pchCharges),
		STRFMT_STRING("Chance",pchChance),
		STRFMT_END);

	estrDestroy(&pchMessageKeyInternal);
	estrDestroy(&pchType);
	estrDestroy(&pchApplyTarget);
	estrDestroy(&pchCharges);
	estrDestroy(&pchChance);
}

static void AutoDescSpecialEntCreate(AttribModDef *pdef,
									 char **ppchDescription,
									 const char *pchMessageKey,
									 const char *pchSign,
									 const char *pchAttrib,
									 const char *pchTarget,
									 const char *pchMagnitude)
{
	const char *pchCritter = NULL;
	EntCreateParams *pParams = (EntCreateParams*)pdef->pParams;
	if(pParams)
	{
		CritterDef *pdefCritter = GET_REF(pParams->hCritter);
		if(pdefCritter)
		{
			pchCritter = TranslateDisplayMessage(pdefCritter->displayNameMsg);
		}
		if(!pchCritter)
		{
			pchCritter = TranslateMessageKey("AutoDesc.AttribModDef.GenericPetEntity");
		}
	}

	if(!pchCritter)
	{
		pchCritter = TranslateMessageKey("AutoDesc.Unknown");
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Entity",pchCritter),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);
}

static void AutoDescSpecialGrantPower(AttribModDef *pdef,
									  char **ppchDescription,
									  const char *pchMessageKey,
									  Language lang)
{
	const char *pchPower = NULL;
	GrantPowerParams *pParams = (GrantPowerParams*)pdef->pParams;
	if(pParams)
	{
		PowerDef *ppowdef = GET_REF(pParams->hDef);
		if(ppowdef)
		{
			pchPower = langTranslateDisplayMessage(lang,ppowdef->msgDisplayName);
		}
	}

	if(!pchPower)
	{
		pchPower = langTranslateMessageKey(lang,"AutoDesc.Unknown");
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Power",pchPower),
		STRFMT_END);
}

static void AutoDescSpecialIncludeEnhancement(AttribModDef *pdef,
	char **ppchDescription,
	const char *pchMessageKey,
	Language lang)
{
	const char *pchPower = NULL;
	IncludeEnhancementParams *pParams = (IncludeEnhancementParams*)pdef->pParams;
	if(pParams)
	{
		PowerDef *ppowdef = GET_REF(pParams->hDef);
		if(ppowdef)
		{
			pchPower = langTranslateDisplayMessage(lang,ppowdef->msgDisplayName);
		}
	}

	if(!pchPower)
	{
		pchPower = langTranslateMessageKey(lang,"AutoDesc.Unknown");
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Power",pchPower),
		STRFMT_END);
}

static void AutoDescSpecialKillTrigger(int iPartitionIdx, 
									   Character *pchar, 
									   AttribModDef *pdef,
									   char **ppchDescription,
									   const char *pchMessageKey,
									   const char *pchSign,
									   const char *pchAttrib,
									   const char *pchTarget,
									   const char *pchMagnitude)
{
	const char *pchPower = TranslateMessageKey("AutoDesc.Unknown");
	char *pchApplyTarget = NULL;
	char *pchCharges = NULL;
	char *pchChance = NULL;
	KillTriggerParams *pParams = (KillTriggerParams*)pdef->pParams;
	if(pParams)
	{
		PowerDef *ppowdef = GET_REF(pParams->hDef);
		if(ppowdef)
		{
			pchPower = powerdef_GetLocalName(ppowdef);
		}

		switch(pParams->eTarget)
		{
		case kKillTriggerEntity_Victim:
			FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.Victim"),STRFMT_END);
			break;
		}

		if(pParams->bMagnitudeIsCharges)
		{
			estrStackCreate(&pchCharges);
			FormatMessageKey(&pchCharges,"AutoDesc.AttribModDef.Charges",STRFMT_STRING("Charges",pchMagnitude),STRFMT_END);
		}

		if(RefSystem_ReferentFromString(gMessageDict, "AutoDesc.AttribModDef.Trigger.Chance"))
		{
			estrStackCreate(&pchChance);
			if(pParams->fChance == 1.f)
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.ChanceAlways", STRFMT_STRING("ApplySource", ""), STRFMT_FLOAT("Chance", pParams->fChance*100.f), STRFMT_END);
			}
			else
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.Chance", STRFMT_STRING("ApplySource", ""), STRFMT_FLOAT("Chance", pParams->fChance*100.f), STRFMT_END);
			}
		}
		else if(pParams->fChance != 1.f)
		{
			estrStackCreate(&pchChance);
			FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Chance", STRFMT_FLOAT("Chance", pParams->fChance*100.f), STRFMT_END);
		}
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Power",pchPower),
		STRFMT_STRING("ApplyTarget",pchApplyTarget),
		STRFMT_STRING("Target",pchTarget),
		STRFMT_STRING("Charges",pchCharges),
		STRFMT_STRING("Chance",pchChance),
		STRFMT_END);

	estrDestroy(&pchApplyTarget);
	estrDestroy(&pchCharges);
	estrDestroy(&pchChance);
}

static void AutoDescSpecialPowerMode(AttribModDef *pdef,
									 char **ppchDescription,
									 const char *pchMessageKey,
									 const char *pchSign,
									 const char *pchAttrib,
									 const char *pchTarget,
									 const char *pchMagnitude)
{
	const char *pchMode = NULL;
	PowerModeParams *pParams = (PowerModeParams*)pdef->pParams;
	if(pParams)
	{
		pchMode = StaticDefineIntRevLookup(PowerModeEnum,pParams->iPowerMode);
	}

	if(!pchMode)
	{
		pchMode = TranslateMessageKey("AutoDesc.Unknown");
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Mode",pchMode),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);
}

static void AutoDescSpecialPowerRecharge(AttribModDef *pdef,
										 char **ppchDescription,
										 const char *pchMessageKey,
										 const char *pchSign,
										 const char *pchAttrib,
										 const char *pchTarget,
										 const char *pchMagnitude)
{
	char *pchTemp = NULL;
	const char *pchApply = NULL;
	PowerRechargeParams *pParams = (PowerRechargeParams*)pdef->pParams;
	if(pParams)
	{
		pchApply = StaticDefineIntRevLookup(PowerRechargeApplyEnum,pParams->eApply);
		if(pchApply)
		{
			estrStackCreate(&pchTemp);
			estrCopy2(&pchTemp,pchMessageKey);
			estrConcatChar(&pchTemp,'.');
			estrAppend2(&pchTemp,pchApply);

			if(pParams->bPercent)
				estrAppend2(&pchTemp,".Percent");

			if(!RefSystem_ReferentFromString(gMessageDict,pchTemp))
			{
				estrDestroy(&pchTemp);
			}
		}
	}

	if(!pchApply)
	{
		pchApply = TranslateMessageKey("AutoDesc.Unknown");
	}

	FormatMessageKey(ppchDescription,pchTemp ? pchTemp : pchMessageKey,
		STRFMT_STRING("Apply",pchApply),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);

	estrDestroy(&pchTemp);
}

static void AutoDescSpecialPowerShield(AttribModDef *pdef,
									   char **ppchDescription,
									   const char *pchMessageKey,
									   const char *pchSign,
									   const char *pchAttrib,
									   const char *pchTarget,
									   const char *pchMagnitude)
{
	F32 fRatio = 0;
	const char *pchMode = NULL;
	PowerShieldParams *pParams = (PowerShieldParams*)pdef->pParams;
	if(pParams)
	{
		fRatio = pParams->fRatio;
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_FLOAT("Ratio",fRatio),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_END);
}

static void AutoDescSpecialTriggerComplex(int iPartitionIdx, 
										  Character *pchar,
										  AttribModDef *pdef,
										  char **ppchDescription,
										  const char *pchMessageKey,
										  const char *pchSign,
										  const char *pchAttrib,
										  const char *pchTarget,
										  const char *pchMagnitude)
{
	const char *pchPower = TranslateMessageKey("AutoDesc.Unknown");
	const char *pchApplySource = pchPower;
	char *pchApplyTarget = NULL;
	char *pchCharges = NULL;
	char *pchChance = NULL;
	TriggerComplexParams *pParams = (TriggerComplexParams*)pdef->pParams;

	estrStackCreate(&pchApplyTarget);

	if(pParams)
	{
		PowerDef *ppowdef = GET_REF(pParams->hDef);
		F32 fChance = 1.f;
		if(ppowdef)
		{
			pchPower = powerdef_GetLocalName(ppowdef);
		}

		switch(pParams->eSource)
		{
		case kTriggerComplexEntity_ModOwner:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.AttribModOwner");
			break;
		case kTriggerComplexEntity_ModSource:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.AttribModSource");
			break;
		case kTriggerComplexEntity_ModTarget:
			pchApplySource = NULL;
			break;
		}

		if(pParams->eTarget!=pParams->eSource)
		{
			switch(pParams->eTarget)
			{
			case kTriggerComplexEntity_ModOwner:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModOwner"),STRFMT_END);
				break;
			case kTriggerComplexEntity_ModSource:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSource"),STRFMT_END);
				break;
			case kTriggerComplexEntity_ModTarget:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModTarget"),STRFMT_END);
				break;
			}
		}

		if(pParams->bMagnitudeIsCharges)
		{
			estrStackCreate(&pchCharges);
			FormatMessageKey(&pchCharges,"AutoDesc.AttribModDef.Charges",STRFMT_STRING("Charges",pchMagnitude),STRFMT_END);
		}

		if(pParams->pExprChance)
		{
			fChance = character_TriggerAttribModCheckChance(iPartitionIdx, pchar, 0, pParams->pExprChance);
		}

		if(RefSystem_ReferentFromString(gMessageDict, "AutoDesc.AttribModDef.Trigger.Chance"))
		{
			estrStackCreate(&pchChance);
			if(pParams->pExprChance == NULL)
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.ChanceAlways", STRFMT_STRING("ApplySource", pchApplySource), STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
			}
			else
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.Chance", STRFMT_STRING("ApplySource", pchApplySource), STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
			}
		}
		else if(fChance != 1.f)
		{
			estrStackCreate(&pchChance);
			FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Chance", STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
		}
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Power",pchPower),
		STRFMT_STRING("ApplySource",pchApplySource),
		STRFMT_STRING("ApplyTarget",pchApplyTarget),
		STRFMT_STRING("Target",pchTarget),
		STRFMT_STRING("Charges",pchCharges),
		STRFMT_STRING("Chance",pchChance),
		STRFMT_END);

	estrDestroy(&pchApplyTarget);
	estrDestroy(&pchCharges);
	estrDestroy(&pchChance);
}

static void AutoDescSpecialTriggerSimple(int iPartitionIdx, 
										 Character *pchar,
										 AttribModDef *pdef,
										 char **ppchDescription,
										 const char *pchMessageKey,
										 const char *pchSign,
										 const char *pchAttrib,
										 const char *pchTarget,
										 const char *pchMagnitude)
{
	const char *pchPower = TranslateMessageKey("AutoDesc.Unknown");
	const char *pchApplySource = pchPower;
	char *pchApplyTarget = NULL;
	char *pchCharges = NULL;
	char *pchChance = NULL;
	TriggerSimpleParams *pParams = (TriggerSimpleParams*)pdef->pParams;

	estrStackCreate(&pchApplyTarget);

	if(pParams)
	{
		PowerDef *ppowdef = GET_REF(pParams->hDef);
		F32 fChance = 1.f;
		if(ppowdef)
		{
			pchPower = powerdef_GetLocalName(ppowdef);
		}

		switch(pParams->eSource)
		{
		case kTriggerSimpleEntity_ModOwner:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.AttribModOwner");
			break;
		case kTriggerSimpleEntity_ModSource:
			pchApplySource = TranslateMessageKey("AutoDesc.AttribModDef.AttribModSource");
			break;
		case kTriggerSimpleEntity_ModTarget:
			pchApplySource = NULL;
			break;
		}

		if(pParams->eTarget!=pParams->eSource)
		{
			switch(pParams->eTarget)
			{
			case kTriggerSimpleEntity_ModOwner:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModOwner"),STRFMT_END);
				break;
			case kTriggerSimpleEntity_ModSource:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSource"),STRFMT_END);
				break;
			case kTriggerSimpleEntity_ModTarget:
				FormatMessageKey(&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModTarget"),STRFMT_END);
				break;
			}
		}

		if(pParams->bMagnitudeIsCharges)
		{
			estrStackCreate(&pchCharges);
			FormatMessageKey(&pchCharges,"AutoDesc.AttribModDef.Charges",STRFMT_STRING("Charges",pchMagnitude),STRFMT_END);
		}
		
		if(pParams->pExprChance)
		{
			fChance = character_TriggerAttribModCheckChance(iPartitionIdx, pchar, 0, pParams->pExprChance);
		}

		if(RefSystem_ReferentFromString(gMessageDict, "AutoDesc.AttribModDef.Trigger.Chance"))
		{
			estrStackCreate(&pchChance);
			if(pParams->pExprChance == NULL)
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.ChanceAlways", STRFMT_STRING("ApplySource", pchApplySource), STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
			}
			else
			{
				FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Trigger.Chance", STRFMT_STRING("ApplySource", pchApplySource), STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
			}
		}
		else if(fChance != 1.f)
		{
			estrStackCreate(&pchChance);
			FormatMessageKey(&pchChance, "AutoDesc.AttribModDef.Chance", STRFMT_FLOAT("Chance", fChance*100.f), STRFMT_END);
		}
	}

	FormatMessageKey(ppchDescription,pchMessageKey,
		STRFMT_STRING("Power",pchPower),
		STRFMT_STRING("ApplySource",pchApplySource),
		STRFMT_STRING("ApplyTarget",pchApplyTarget),
		STRFMT_STRING("Target",pchTarget),
		STRFMT_STRING("Charges",pchCharges),
		STRFMT_STRING("Chance",pchChance),
		STRFMT_END);

	estrDestroy(&pchApplyTarget);
	estrDestroy(&pchCharges);
	estrDestroy(&pchChance);
}

static void AutoDescAttribModDev(AttribModDef *pdef,
								 char **ppchDesc,
								 Language lang)
{
	if(pdef->eError)
	{
		if(pdef->eError&kPowerError_Error)
		{
			estrAppend2(ppchDesc,langTranslateMessageKey(lang,"AutoDesc.DataError"));
			estrAppend2(ppchDesc," ");
		}

		if(pdef->eError&kPowerError_Warning)
		{
			estrAppend2(ppchDesc,langTranslateMessageKey(lang,"AutoDesc.DataWarning"));
			estrAppend2(ppchDesc," ");
		}
	}
}

static const char *AutoDescAttribModTarget(AttribModDef *pdef,
										   PowerDef *ppowdef,
										   S32 bModsTargetMixed,
										   AutoDescDetail eDetail,
										   Language lang)
{
	const char *pchTarget = NULL;
	S32 bIgnoreTarget = false;

	if(ppowdef->eType==kPowerType_Innate)
	{
		bIgnoreTarget = true;
	}
	else if(ppowdef->eType==kPowerType_Enhancement)
	{
		if(!pdef->bEnhancementExtension)
		{
			bIgnoreTarget = true;
		}
	}
	else
	{
		PowerTarget *pAffected = GET_REF(ppowdef->hTargetAffected);
		if(pAffected && pAffected->bRequireSelf)
		{
			bIgnoreTarget = true;
		}
		else if(!bModsTargetMixed && pdef->eTarget==kModTarget_Target)
		{
			bIgnoreTarget = true;
		}
	}

	if(!bIgnoreTarget)
	{
		char *pchTemp = NULL;
		const char *pchTargetBase = NULL;
		ModTarget eTarget = pdef->eTarget;
		if(eTarget==kModTarget_SelfOnce && eDetail<kAutoDescDetail_Maximum)
		{
			eTarget = kModTarget_Self;
		}
		pchTargetBase = StaticDefineIntRevLookup(ModTargetEnum,eTarget);

		estrStackCreate(&pchTemp);
		estrPrintf(&pchTemp,"AutoDesc.AttribModDef.Target%s",pchTargetBase);
		pchTarget = langTranslateMessageKey(lang,pchTemp);
		estrDestroy(&pchTemp);
	}

	return pchTarget;
}

static S32 AutoDescAttribModDuration(int iPartitionIdx,
									 AttribModDef *pdef,
									 char **ppchDuration,
									 S32 *piPeriodsOut,
									 S32 *piPeriodsMaxOut,
									 F32 fStrength,
									 F32 fStrengthAdd,
									 PowerDef *ppowdef,
									 Character *pchar,
									 CharacterClass *pclass,
									 int iLevel,
									 int iCostPaidUnroll,
									 AutoDescPowerHeader eHeader,
									 AutoDescDetail eDetail,
									 AutoDescAttribMod *pAutoDescAttribModEvent,
									 S32 bLocalFullDescription,
									 Language lang)
{
	static PowerActivation *s_pact = NULL;

	S32 bVariance = (eDetail>kAutoDescDetail_Normal);
	S32 bCompressPeriods = false;

	char *pchError = NULL;

	const char *pchVariableReason = NULL;
	F32 fDuration = 0, fDurationMax = 0;
	S32 iPeriods = 0, iPeriodsMax = 0;

	if(!s_pact)
	{
		s_pact = poweract_Create();
	}

	SET_HANDLE_FROM_REFERENT(g_hPowerDefDict, ppowdef, s_pact->ref.hdef);

	estrStackCreate(&pchError);

	combateval_ContextReset(kCombatEvalContext_Apply);

	if(iCostPaidUnroll)
	{
		PowerApplication app = {0};
		app.pact = s_pact;
		s_pact->fCostPaid = iCostPaidUnroll;
		combateval_ContextSetupApply(pchar,NULL,NULL,&app);
	}
	else
	{
		combateval_ContextSetupApply(pchar,NULL,NULL,NULL);
	}

	fDuration = fDurationMax = combateval_EvalNew(iPartitionIdx,pdef->pExprDuration,kCombatEvalContext_Apply,&pchError);

	if(ppowdef->fTimeCharge && AutoDescNeedsChargeCorrection(pdef->pExprDuration))
	{
		PowerApplication app = {0};
		app.pact = s_pact;
		app.pdef = ppowdef;
		estrClear(&pchError);
		pchVariableReason = "AutoDesc.PowerDef.VariableReason.Charge";

		s_pact->fTimeCharged = 0;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,NULL,&app);
		fDuration = combateval_EvalNew(iPartitionIdx,pdef->pExprDuration,kCombatEvalContext_Apply,&pchError);

		s_pact->fTimeCharged = ppowdef->fTimeCharge;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,NULL,&app);
		fDurationMax = combateval_EvalNew(iPartitionIdx,pdef->pExprDuration,kCombatEvalContext_Apply,&pchError);
	}
	else if(*pchError && AutoDescExprFail(pdef->pExprDuration, "PowerDef", ".*"))
	{
		PowerApplication app = {0};
		app.pact = s_pact;
		app.pdef = ppowdef;
		estrClear(&pchError);

		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,NULL,&app);
		fDuration = combateval_EvalNew(iPartitionIdx,pdef->pExprDuration,kCombatEvalContext_Apply,&pchError);
	}
	else if(*pchError && pAutoDescAttribModEvent && AutoDescExprFailFunc(pdef->pExprDuration,"ModMag"))
	{
		PowerApplication app = {0};
		app.pmodEvent = StructCreate(parse_AttribMod);
		estrClear(&pchError);

		app.pmodEvent->fMagnitude = pAutoDescAttribModEvent->fMagnitudeActual;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,NULL,&app);
		fDuration = combateval_EvalNew(iPartitionIdx,pdef->pExprDuration,kCombatEvalContext_Apply,&pchError);

		app.pmodEvent->fMagnitude = pAutoDescAttribModEvent->fMagnitudeMaxActual;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,NULL,&app);
		fDurationMax = combateval_EvalNew(iPartitionIdx,pdef->pExprDuration,kCombatEvalContext_Apply,&pchError);

		StructDestroy(parse_AttribMod,app.pmodEvent);
	}


	// Make sure the larger value is fDurationMax
	if(fDuration>fDurationMax)
	{
		F32 fTemp = fDurationMax;
		fDurationMax = fDuration;
		fDuration = fTemp;
	}

	if(!*pchError)
	{
		if(pdef->eType&kModType_Duration)
		{
			if(pdef->pchTableDefault)
			{
				F32 fTableScale;
				if(pclass)
				{
					fTableScale = class_powertable_LookupMulti(pclass,pdef->pchTableDefault,iLevel-1,0);
				}
				else
				{
					fTableScale = powertable_LookupMulti(pdef->pchTableDefault,iLevel-1,0);
				}
				fDuration *= fTableScale;
				fDurationMax *= fTableScale;
			}

			fDuration += fStrengthAdd;
			fDurationMax += fStrengthAdd;
			
			fDuration *= fStrength;
			fDurationMax *= fStrength;
		}

		// See if we can compress this into an "over x sec" sort of description
		if(eDetail<kAutoDescDetail_Maximum
			&& pdef->fPeriod
			&& !pdef->bForever
			&& !ATTRIB_ASPECT_PERIODIC_NONCOMPRESSED(pdef->offAttrib,pdef->offAspect)
			&& !bLocalFullDescription)
		{
			S32 iBase = pdef->bIgnoreFirstTick ? 0 : 1;
			iPeriods = iBase + floor(fDuration/pdef->fPeriod);
			iPeriodsMax = iBase + floor(fDurationMax/pdef->fPeriod);
			MAX1(iPeriodsMax,iPeriods);
		}

		// Handle rounding
		if(eDetail<kAutoDescDetail_Maximum && !pdef->bForever)
		{
			fDuration = AutoDescRound(fDuration);
			fDurationMax = AutoDescRound(fDurationMax);
		}

		if(pchVariableReason)
		{
			if(bVariance && pdef->eType&kModType_Duration && pdef->fVariance)
			{
				langFormatGameMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationVariableVariance",STRFMT_FLOAT("TimeMin",fDuration),STRFMT_FLOAT("TimeMax",fDurationMax),STRFMT_FLOAT("Variance",pdef->fVariance*100.f),STRFMT_MESSAGEKEY("Reason",pchVariableReason),STRFMT_END);
			}
			else if(pdef->bForever)
			{
				langFormatMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationForever",STRFMT_END);
			}
			else if(iPeriods)
			{
				langFormatGameMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationCompressedVariable",STRFMT_FLOAT("TimeMin",fDuration),STRFMT_FLOAT("TimeMax",fDurationMax),STRFMT_MESSAGEKEY("Reason",pchVariableReason),STRFMT_END);
				bCompressPeriods = true;
			}
			else
			{
				langFormatGameMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationVariable",STRFMT_FLOAT("TimeMin",fDuration),STRFMT_FLOAT("TimeMax",fDurationMax),STRFMT_MESSAGEKEY("Reason",pchVariableReason),STRFMT_END);
			}
		}
		else
		{
			if(bVariance && pdef->eType&kModType_Duration && pdef->fVariance)
			{
				langFormatGameMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationVariance",STRFMT_FLOAT("Time",fDuration),STRFMT_FLOAT("Variance",pdef->fVariance*100.f),STRFMT_END);
			}
			else if(pdef->bForever)
			{
				langFormatMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationForever",STRFMT_END);
			}
			else if(iPeriods)
			{
				langFormatGameMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationCompressed",STRFMT_FLOAT("Time",fDuration),STRFMT_END);
				bCompressPeriods = true;
			}
			else if(ppowdef->fTimeActivatePeriod > 0
					&& !ppowdef->bDeactivationLeavesMods
					&& fDuration >= ppowdef->fTimeActivatePeriod
					&& !(eHeader == kAutoDescPowerHeader_Apply || eHeader == kAutoDescPowerHeader_ApplySelf))
			{
				// We don't normally show the duration in this case, since the Power is Periodic and
				//  it doesn't leave any AttribMods behind when it shuts off, the duration is effectively
				//  "while the Power is active", but that's a bit wordy for normal detail level.
				// In theory there might be an exception to the rule when the AttribMod has no weird
				//  tricks that would make it not operate all the time, and the Power has a limited
				//  number of periods.  Then the implication is that the effect lasts up to a certain
				//  amount of time.
				// But what about AoEs?

				if(eDetail>kAutoDescDetail_Normal)
				{
					langFormatMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationWhileActive",STRFMT_END);
				}
			}
			else
			{
				langFormatGameMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.Duration",STRFMT_FLOAT("Time",fDuration),STRFMT_END);
			}
		}
	}
	else
	{
		if(bVariance && pdef->eType&kModType_Duration && pdef->fVariance)
		{
			langFormatGameMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.DurationVariance",STRFMT_MESSAGEKEY("Time","AutoDesc.Unknown"),STRFMT_FLOAT("Variance",pdef->fVariance*100.f),STRFMT_END);
		}
		else
		{
			langFormatMessageKey(lang,ppchDuration,"AutoDesc.AttribModDef.Duration",STRFMT_MESSAGEKEY("Time","AutoDesc.Unknown"),STRFMT_END);
		}
	}

	estrDestroy(&pchError);

	*piPeriodsOut = iPeriods;
	*piPeriodsMaxOut = iPeriodsMax;

	return bCompressPeriods;
}

static void AutoDescAttribModMagnitude(int iPartitionIdx,
									   AttribModDef *pdef,
									   AutoDescAttribMod *pAutoDescAttribMod,
									   const char **ppchMagnitudeVariableReason,
									   const char **ppchSign,
									   F32 fStrength,
									   F32 fStrengthAdd,
									   Power *ppow,
									   PowerDef *ppowdef,
									   Character *pchar,
									   CharacterClass *pclass,
									   int iLevel,
									   int iCostPaidUnroll,
									   S32 bCompressPeriods,
									   S32 iPeriods,
									   S32 iPeriodsMax,
									   F32 fPowerAppsPerSecond,
									   AutoDescDetail eDetail,
									   AutoDescAttribMod *pAutoDescAttribModEvent,
									   Language lang)
{
	static PowerActivation *s_pact = NULL;

	S32 bVariance = (eDetail>kAutoDescDetail_Normal) && !s_AutoDescConfig.bVariance;
	S32 bVariable = false;

	char *pchError = NULL;

	const char *pchVariableReason = NULL;
	F32 fMagnitude = 0, fMagnitudeMax = 0, fMagnitudePerSecond = 0;
	Item * pSourceItem = ppow ? ppow->pSourceItem:NULL;

	if(!s_pact)
	{
		s_pact = poweract_Create();
	}

	SET_HANDLE_FROM_REFERENT(g_hPowerDefDict, ppowdef, s_pact->ref.hdef);

	estrStackCreate(&pchError);

	if(pdef->pExprMagnitude)
	{
		S32 iold = g_CombatEvalOverrides.iAutoDescTableAppHack;
		g_CombatEvalOverrides.iAutoDescTableAppHack = iLevel;
		combateval_ContextReset(kCombatEvalContext_Apply);
		if(iCostPaidUnroll)
		{
			PowerApplication app = {0};
			app.pact = s_pact;
			s_pact->fCostPaid = iCostPaidUnroll;
			combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		}
		else
		{
			combateval_ContextSetupApply(pchar,NULL,pSourceItem,NULL);
		}
		fMagnitude = fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
		g_CombatEvalOverrides.iAutoDescTableAppHack = iold;
	}

	// Correct for Activation.Charged (or ActPercentCharged), Application.NumTargets, Activation.LungeDistance, Activation.Period
	if(ppowdef->fTimeCharge
		&& AutoDescNeedsChargeCorrection(pdef->pExprMagnitude) )
	{
		PowerApplication app = {0};
		app.pdef = ppowdef;
		app.pact = s_pact;
		estrClear(&pchError);
		pchVariableReason = "AutoDesc.PowerDef.VariableReason.Charge";

		s_pact->fTimeCharged = 0;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);

		s_pact->fTimeCharged = ppowdef->fTimeCharge;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
	}
	else if(ppowdef->fTimeMaintain && ppowdef->fTimeActivatePeriod && AutoDescExprFail(pdef->pExprMagnitude,"Activation",".Pulse"))
	{
		PowerApplication app = {0};
		app.pdef = ppowdef;
		app.pact = s_pact;
		estrClear(&pchError);
		pchVariableReason = "AutoDesc.PowerDef.VariableReason.Pulse";

		s_pact->uiPeriod = 0;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);

		s_pact->uiPeriod = floor(ppowdef->fTimeMaintain/ppowdef->fTimeActivatePeriod);
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
	}
	else if(ppowdef->fTimeActivatePeriod && AutoDescExprFail(pdef->pExprMagnitude,"Activation",".Period"))
	{
		PowerApplication app = {0};
		app.pdef = ppowdef;
		app.pact = s_pact;
		estrClear(&pchError);
		pchVariableReason = "AutoDesc.PowerDef.VariableReason.Period";

		s_pact->uiPeriod = 0; 
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);

		s_pact->uiPeriod = ppowdef->uiPeriodsMax;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
	}
	else if(*pchError && (ppowdef->iMaxTargetsHit || ppowdef->eType==kPowerType_Enhancement) && AutoDescExprFail(pdef->pExprMagnitude,"Application",".NumTargets"))
	{
		PowerApplication app = {0};
		app.pdef = ppowdef;
		app.pact = s_pact;
		estrClear(&pchError);
		pchVariableReason = "AutoDesc.PowerDef.VariableReason.NumTargets";

		app.iNumTargets = 1;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);

		if(ppowdef->iMaxTargetsHit)
		{
			app.iNumTargets = ppowdef->iMaxTargetsHit;
			combateval_ContextReset(kCombatEvalContext_Apply);
			combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
			fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
		}
	}
	else if(*pchError && GET_REF(ppowdef->hFX) && GET_REF(ppowdef->hFX)->pLunge && AutoDescExprFail(pdef->pExprMagnitude,"Activation",".LungeDistance"))
	{
		PowerAnimFX *pafx = GET_REF(ppowdef->hFX);
		PowerApplication app = {0};
		app.pdef = ppowdef;
		app.pact = s_pact;
		estrClear(&pchError);
		pchVariableReason = "AutoDesc.PowerDef.VariableReason.LungeDistance";

		s_pact->fLungeDistance = 0;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);

		s_pact->fLungeDistance = pafx->pLunge->fRange;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
	}
	else if(*pchError && AutoDescExprFail(pdef->pExprMagnitude,"PowerDef",".*"))
	{
		PowerApplication app = {0};
		app.pdef = ppowdef;
		app.pact = s_pact;
		estrClear(&pchError);

		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
	}
	else if(*pchError && pAutoDescAttribModEvent && AutoDescExprFailFunc(pdef->pExprMagnitude,"ModMag"))
	{
		PowerApplication app = {0};
		app.pmodEvent = StructCreate(parse_AttribMod);
		estrClear(&pchError);

		app.pmodEvent->fMagnitude = pAutoDescAttribModEvent->fMagnitudeActual;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);

		app.pmodEvent->fMagnitude = pAutoDescAttribModEvent->fMagnitudeMaxActual;
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);

		bVariable = (fMagnitude!=fMagnitudeMax);
		
		StructDestroy(parse_AttribMod,app.pmodEvent);
	}
	else if (*pchError && AutoDescExprFail(pdef->pExprMagnitude,"Application",".DamageTrigger.*"))
	{
		PowerApplication app = {0};
		app.pdef = ppowdef;
		app.pact = s_pact;
		app.trigger.fMag = 1.f;
		app.trigger.fMagPreResist = 1.f;
		app.trigger.fMagScale = 1.f;
		estrClear(&pchError);

		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,&app);
		fMagnitude = fMagnitudeMax = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Apply,&pchError);
	}

	// If we want natural variance included
	if(s_AutoDescConfig.bVariance
		&& !pchVariableReason
		&& (eDetail>kAutoDescDetail_Minimum)
		&& pdef->eType&kModType_Magnitude
		&& pdef->fVariance)
	{
		fMagnitude *= (1-pdef->fVariance);
		fMagnitudeMax *= (1+pdef->fVariance);
		bVariable = true;
	}

	// Make sure the larger value is fMagnitudeMax
	if(fMagnitude>fMagnitudeMax)
	{
		F32 fTemp = fMagnitudeMax;
		fMagnitudeMax = fMagnitude;
		fMagnitude = fTemp;
	}

	if(!*pchError)
	{
		if(pdef->eType&kModType_Magnitude)
		{
			if(pdef->pchTableDefault)
			{
				F32 fTableScale;
				if(pclass)
				{
					fTableScale = class_powertable_LookupMulti(pclass,pdef->pchTableDefault,iLevel-1,0);
				}
				else
				{
					fTableScale = powertable_LookupMulti(pdef->pchTableDefault,iLevel-1,0);
				}
				fMagnitude *= fTableScale;
				fMagnitudeMax *= fTableScale;
			}

			fMagnitude += fStrengthAdd;
			fMagnitudeMax += fStrengthAdd;

			fMagnitude *= fStrength;
			fMagnitudeMax *= fStrength;
		}

		// Special inversion for AttribModFragilityScale, which is a direct scalar but displays better as
		//  a psaudo-resistance-style gain/loss
		if(pdef->offAspect==kAttribAspect_BasicAbs && pdef->offAttrib==kAttribType_AttribModFragilityScale)
		{
			fMagnitude = 1 - fMagnitude;
			fMagnitudeMax = 1 - fMagnitudeMax;
		}

		// Scale for percents
		if (pdef->offAspect == kAttribAspect_StrMult)
		{
			// This is the only one where "1" means "leave it alone"
			fMagnitude = fabsf(fMagnitude-1.f)*100.f;
			fMagnitudeMax = fabsf(fMagnitudeMax-1.f)*100.f;
		}
		else if(ASPECT_PERCENT(pdef->offAspect))
		{
			if (ASPECT_NEGATIVE_PERCENT(pdef->offAspect))
			{
				fMagnitude = 1 - (1 / (1 + MAX(0, fMagnitude)));
				fMagnitudeMax = 1 - (1 / (1 + MAX(0, fMagnitudeMax)));
			}

			fMagnitude *= 100.f;
			fMagnitudeMax *= 100.f;
		}
		else if(ATTRIB_ASPECT_PERCENT(pdef->offAttrib,pdef->offAspect))
		{
			// Percent-type attribs
			fMagnitude *= 100.f;
			fMagnitudeMax *= 100.f;
		}
		else if(ATTRIB_ASPECT_SCALED(pdef->offAttrib,pdef->offAspect))
		{
			// Config-scaled attribs
			fMagnitude *= 100.f;
			fMagnitudeMax *= 100.f;
		}

		if(fPowerAppsPerSecond>0
			&& fMagnitude>0
			&& IS_DAMAGE_ATTRIBASPECT(pdef->offAttrib,pdef->offAspect)
			&& fMagnitude==fMagnitudeMax
			&& !pdef->pExprRequires
			&& !pdef->pExprDuration)
		{
			fMagnitudePerSecond = fMagnitude * fPowerAppsPerSecond;
			if(eDetail<kAutoDescDetail_Maximum)
				fMagnitudePerSecond = AutoDescRound(fMagnitudePerSecond);
			if(fMagnitudePerSecond!=0)
				langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchMagnitudePerSecond,"AutoDesc.AttribModDef.MagnitudePerSecond",STRFMT_FLOAT("Size",fMagnitudePerSecond),STRFMT_END);
		}

		// Scale if the periods are compressed
		if(bCompressPeriods)
		{
			fMagnitude *= iPeriods;
			fMagnitudeMax *= iPeriodsMax;
		}

		if(eDetail<kAutoDescDetail_Maximum)
		{
			if(ATTRIB_ASPECT_FLOOR(pdef->offAttrib,pdef->offAspect))
			{
				fMagnitude = AutoDescFloor(fMagnitude);
				fMagnitudeMax = AutoDescFloor(fMagnitudeMax);
			}
			else
			{
				fMagnitude = AutoDescRound(fMagnitude);
				fMagnitudeMax = AutoDescRound(fMagnitudeMax);
			}
		}

		if(fMagnitude > 0)
		{
			*ppchSign = TranslateMessageKey("AutoDesc.AttribModDef.MagnitudeSignPositive");
		}

		if(ATTRIB_BOOLEAN(pdef->offAttrib)
			&& fMagnitude == 1
			&& fMagnitudeMax == 1)
		{
			// "Proper" boolean attributes don't mention magnitude
			*ppchSign = NULL;
		}
		else
		{
			if(pchVariableReason || (bVariable && fMagnitude!=fMagnitudeMax))
			{
				langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchMagnitude,"AutoDesc.AttribModDef.MagnitudeVariable",STRFMT_FLOAT("SizeMin",fMagnitude),STRFMT_FLOAT("SizeMax",fMagnitudeMax),STRFMT_END);
			}
			else
			{
				langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchMagnitude,"AutoDesc.AttribModDef.Magnitude",STRFMT_FLOAT("Size",fMagnitude),STRFMT_END);
			}
		}
	}
	else
	{
		langFormatMessageKey(lang,&pAutoDescAttribMod->pchMagnitude,"AutoDesc.AttribModDef.Magnitude",STRFMT_MESSAGEKEY("Size","AutoDesc.Unknown"),STRFMT_END);
	}

	if(bVariance && pdef->eType&kModType_Magnitude && pdef->fVariance)
	{
		langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchMagnitudeVariance,"AutoDesc.AttribModDef.MagnitudeVariance",STRFMT_FLOAT("Variance",pdef->fVariance*100.f),STRFMT_END);
	}

	*ppchMagnitudeVariableReason = pchVariableReason;

	pAutoDescAttribMod->fMagnitudeActual = fMagnitude;
	pAutoDescAttribMod->fMagnitudeMaxActual = fMagnitudeMax;

	estrDestroy(&pchError);
}

static void AutoDescAttribModEffect(int iPartitionIdx,
									AttribModDef *pdef,
									Character *pchar,
									char **ppchDesc,
									const char *pchSign,
									char *pchAttrib,
									const char *pchTarget,
									char *pchMagnitude,
									char *pchMagnitudeVariance,
									const char *pchMagnitudeVariableReason,
									Language lang)
{
	char *pchTemp = NULL;
	int offAttrib = pdef->offAttrib;
	int offAspect = pdef->offAspect;

	estrStackCreate(&pchTemp);

	if(IS_DAMAGE_ATTRIBASPECT(offAttrib,offAspect))
	{
		estrCopy2(&pchTemp,"AutoDesc.AttribModDef.AttribAspectDamage");
	}
	else if(offAspect==kAttribAspect_BasicAbs && IS_SPECIAL_ATTRIB(offAttrib))
	{
		S32 bDefault = false;
		estrPrintf(&pchTemp,"AutoDesc.AttribModDef.SpecialAttrib%s",StaticDefineIntRevLookup(AttribTypeEnum,offAttrib));

		if(!RefSystem_ReferentFromString(gMessageDict,pchTemp))
		{
			bDefault = true;
			// Didn't find a custom default message at that key
			if(attrib_Unroll(offAttrib) || ATTRIB_SPECIAL_AS_NORMAL(offAttrib))
			{
				estrCopy2(&pchTemp,"AutoDesc.AttribModDef.AspectBasicAbs");
				if(ATTRIB_ASPECT_PERCENT(offAttrib,offAspect))
				{
					estrAppend2(&pchTemp,".Percent");
				}
			}
			else
			{
				estrCopy2(&pchTemp,"AutoDesc.AttribModDef.SpecialAttrib.Default");
			}
		}

		// Custom code for some of the specials
		if(bDefault)
		{
			; // Don't use the special calls, we only have a generic message
		}
		else if(pdef->offAttrib==kAttribType_AIAvoid)
		{
			AutoDescSpecialAIAvoid(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib == kAttribType_AISoftAvoid)
		{
			AutoDescSpecialAISoftAvoid(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_AIThreat)
		{
			AutoDescSpecialAIThreat(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_ApplyPower)
		{
			AutoDescSpecialApplyPower(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_AttribModDamage)
		{
			AutoDescSpecialAttribModDamage(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude,lang);
		}
		else if(pdef->offAttrib==kAttribType_AttribModHeal)
		{
			AutoDescSpecialAttribModHeal(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_AttribModExpire)
		{
			AutoDescSpecialAttribModExpire(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_AttribOverride)
		{
			AutoDescSpecialAttribOverride(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude,lang);
		}
		else if(pdef->offAttrib==kAttribType_BecomeCritter)
		{
			AutoDescSpecialBecomeCritter(pdef,ppchDesc,pchTemp,lang);
		}
		else if(pdef->offAttrib==kAttribType_DamageTrigger)
		{
			AutoDescSpecialDamageTrigger(iPartitionIdx,pchar,pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude,lang);
		}
		else if(pdef->offAttrib==kAttribType_EntCreate)
		{
			AutoDescSpecialEntCreate(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_GrantPower)
		{
			AutoDescSpecialGrantPower(pdef,ppchDesc,pchTemp,lang);
		}
		else if(pdef->offAttrib==kAttribType_IncludeEnhancement)
		{
			AutoDescSpecialIncludeEnhancement(pdef,ppchDesc,pchTemp,lang);
		}
		else if(pdef->offAttrib==kAttribType_KillTrigger)
		{
			AutoDescSpecialKillTrigger(iPartitionIdx,pchar,pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_PowerMode)
		{
			AutoDescSpecialPowerMode(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_PowerRecharge)
		{
			AutoDescSpecialPowerRecharge(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_PowerShield)
		{
			AutoDescSpecialPowerShield(pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_TriggerComplex)
		{
			AutoDescSpecialTriggerComplex(iPartitionIdx,pchar,pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
		else if(pdef->offAttrib==kAttribType_TriggerSimple)
		{
			AutoDescSpecialTriggerSimple(iPartitionIdx,pchar,pdef,ppchDesc,pchTemp,pchSign,pchAttrib,pchTarget,pchMagnitude);
		}
	}
	else
	{
		S32 bCustom = false;

		if(pdef->offAspect==kAttribAspect_BasicAbs)
		{
			const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum,offAttrib);
			estrPrintf(&pchTemp,"AutoDesc.AttribModDef.Attrib%s",pchAttribBase);
			if(RefSystem_ReferentFromString(gMessageDict,pchTemp))
			{
				// Found a custom default message at that key
				bCustom = true;
			}
		}

		if(!bCustom)
		{
			const char *pchAspectBase = StaticDefineIntRevLookup(AttribAspectEnum,offAspect);
			estrPrintf(&pchTemp,"AutoDesc.AttribModDef.Aspect%s",pchAspectBase);
			if(ATTRIB_ASPECT_PERCENT(offAttrib,offAspect))
			{
				estrAppend2(&pchTemp,".Percent");
			}
		}
	}

	// If we didn't manage to come up with a description from the above code
	if(!(*ppchDesc))
	{
		const char *pchTransMagnitudeVariableReason = pchMagnitudeVariableReason ? langTranslateMessageKey(lang,pchMagnitudeVariableReason) : NULL;
		langFormatGameMessageKey(lang,ppchDesc,pchTemp,
			STRFMT_STRING("Sign",pchSign),
			STRFMT_STRING("Attrib",pchAttrib),
			STRFMT_STRING("Magnitude",pchMagnitude),
			STRFMT_STRING("Variance",pchMagnitudeVariance),
			STRFMT_STRING("Reason",pchTransMagnitudeVariableReason),
			STRFMT_END);
	}

	estrDestroy(&pchTemp);
}

static void AutoDescAttribModExpiration(AttribModDef *pdef,
										char **ppchDesc,
										AutoDescDetail eDetail,
										const char ***pppchFootnoteMsgKeys,
										Language lang)
{
	if(pdef->pExpiration && eDetail > kAutoDescDetail_Minimum)
	{
		PowerDef *pdefExpire = GET_REF(pdef->pExpiration->hDef);
		if(pdefExpire)
		{
			S32 bRequires = !!pdef->pExpiration->pExprRequiresExpire;
			const char *pchPower = langTranslateMessageKey(lang,"AutoDesc.Unknown");
			const char *pchKey = "AutoDesc.AttribModDef.Expiration";
			char *pchApplyTarget = NULL;

			estrStackCreate(&pchApplyTarget);
			
			if(pdef->pExpiration->bPeriodic)
				pchKey = "AutoDesc.AttribModDef.PeriodicApply";
			else if(!pdef->pExprDuration)
				pchKey = "AutoDesc.AttribModDef.ExpirationInstant";

			pchPower = langTranslateDisplayMessage(lang,pdefExpire->msgDisplayName);

			switch(pdef->pExpiration->eTarget)
			{
				case kModExpirationEntity_Unset:
				case kModExpirationEntity_ModTarget:
					break; // Nothing
				case kModExpirationEntity_ModSource:
					langFormatGameMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSource"),STRFMT_END);
					break;
				case kModExpirationEntity_ModSourceTargetDual:
					langFormatGameMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModSourceTargetDual"),STRFMT_END);
					break;
				case kModExpirationEntity_ModOwner:
					langFormatGameMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.AttribModOwner"),STRFMT_END);
					break;
				case kModExpirationEntity_RandomNotSource:
					langFormatGameMessageKey(lang,&pchApplyTarget,"AutoDesc.AttribModDef.ApplyTarget",STRFMT_MESSAGEKEY("Target","AutoDesc.AttribModDef.RandomTarget"),STRFMT_END);
					break;
			}
			
			langFormatGameMessageKey(lang,ppchDesc,pchKey,
				STRFMT_STRING("Power",pchPower),
				STRFMT_STRING("ApplyTarget",pchApplyTarget),
				STRFMT_MESSAGEKEY("Requires",bRequires ? "AutoDesc.AttribModDef.Requires" : NULL),
				STRFMT_END);

			if(bRequires && pppchFootnoteMsgKeys)
			{
				eaPushUnique(pppchFootnoteMsgKeys,"AutoDesc.AttribModDef.Requires.Footnote");
			}

			estrDestroy(&pchApplyTarget);
		}
	}
}


static void AutoDescAttribModDef(int iPartitionIdx,
								 SA_PARAM_NN_VALID AttribModDef *pdef,
								 SA_PARAM_NN_VALID AutoDescAttribMod *pAutoDescAttribMod,
								 Language lang,
								 SA_PARAM_NN_VALID PowerDef *ppowdef,
								 SA_PARAM_OP_VALID Character *pchar,
								 SA_PARAM_OP_VALID Power *ppow,
								 SA_PARAM_OP_VALID Power **ppEnhancements,
								 int iLevel,
								 int bIncludeStrength,
								 int bFullDescription,
								 int bModsTargetMixed,
								 int iDepth,
								 int iCostPaidUnroll,
								 F32 fPowerAppsPerSecond,
								 AutoDescPowerHeader eHeader,
								 AutoDescDetail eDetail,
								 SA_PARAM_OP_VALID AutoDescAttribMod *pAutoDescAttribModEvent,
								 SA_PARAM_OP_VALID PowerDef ***pppPowerDefsDescribed,
								 SA_PARAM_OP_VALID const char ***pppchFootnoteMsgKeys,
								 GameAccountDataExtract *pExtract)
{
	S32 i;
	F32 fStrength = 1.f, fStrAdd = 0.f;
	S32 iPeriods = 0;
	S32 iPeriodsMax = 0;
	S32 bCompressPeriods = false, bLocalFullDescription = false;

	// Used to compose description
	const char *pchMagnitudeVariableReason = NULL;
	const char *pchSign = NULL;
	Item * pSourceItem = ppow ? ppow->pSourceItem : NULL;

	CharacterClass *pClass = pchar ? character_GetClassCurrent(pchar) : NULL;

	// If the depth is absurdly large, stop recursing and error
	if (iDepth > 100)
	{
		Errorf("AttribMod %s %d had a depth value greater than 100 while attempting to generate auto descriptions!", ppowdef->pchName, pdef->uiDefIdx);
		return;
	}

	// We show more if there was a request for full description and this has a key, or this has a custom message.
	bLocalFullDescription = (pdef->uiAutoDescKey && bFullDescription) || (eDetail<kAutoDescDetail_Maximum && IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage));

	// Very first thing is check if we need to recurse to "unroll" this AttribModDef to describe its
	//  variable behavior due to custom costs
	if(!iCostPaidUnroll && ppowdef->eAttribCost>=0 && IS_NORMAL_ATTRIB(ppowdef->eAttribCost))
	{
		// Only two things we check are the Magnitude and Duration expressions
		if((pdef->pExprMagnitude && AutoDescExprFail(pdef->pExprMagnitude,"Activation",".CostPaid"))
			|| (pdef->pExprDuration && AutoDescExprFail(pdef->pExprDuration,"Activation",".CostPaid")))
		{
			eaIndexedEnable(&pAutoDescAttribMod->ppModUnrollCost,parse_AutoDescAttribMod);
			// Hardcode the unroll costs [1..5] for now, could be moved to config later
			for(i=1; i<=5; i++)
			{
				AutoDescAttribMod *pAutoDescAttribModUnroll = StructCreate(parse_AutoDescAttribMod);
				AutoDescAttribModDef(iPartitionIdx,pdef,pAutoDescAttribModUnroll,lang,ppowdef,pchar,ppow,ppEnhancements,iLevel,bIncludeStrength,bFullDescription,bModsTargetMixed,iDepth,i,fPowerAppsPerSecond,eHeader,eDetail,pAutoDescAttribModEvent,pppPowerDefsDescribed,pppchFootnoteMsgKeys,pExtract);
				eaIndexedAdd(&pAutoDescAttribMod->ppModUnrollCost,pAutoDescAttribModUnroll);
			}
		}
	}


	if(isDevelopmentMode())
	{
		AutoDescAttribModDev(pdef,&pAutoDescAttribMod->pchDev,lang);
	}

	// Copy the key.  This is just the designer-specified AutoDescKey, unless we're doing an unroll,
	//  in which case it's the unroll number
	if(iCostPaidUnroll)
		pAutoDescAttribMod->iKey = iCostPaidUnroll;
	else
		pAutoDescAttribMod->iKey = (S32)pdef->uiAutoDescKey;

	// Requires
	if(pdef->pExprRequires && !(eDetail!=kAutoDescDetail_Maximum && IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage)))
	{
		pAutoDescAttribMod->pchRequires = langTranslateMessageKey(lang,"AutoDesc.AttribModDef.Requires");
		if(pppchFootnoteMsgKeys)
		{
			eaPushUnique(pppchFootnoteMsgKeys,"AutoDesc.AttribModDef.Requires.Footnote");
		}
	}

	// Affects
	if(pdef->pExprAffects && !(ATTRIB_AFFECTOR(pdef->offAttrib) && IS_BASIC_ASPECT(pdef->offAspect)) && !(eDetail!=kAutoDescDetail_Maximum && IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage)))
	{
		pAutoDescAttribMod->pchAffects = langTranslateMessageKey(lang,"AutoDesc.AttribModDef.Affects");
		if(pppchFootnoteMsgKeys)
		{
			eaPushUnique(pppchFootnoteMsgKeys,"AutoDesc.AttribModDef.Affects.Footnote");
		}
	}

	// Expiration
	AutoDescAttribModExpiration(pdef,&pAutoDescAttribMod->pchExpire,eDetail,pppchFootnoteMsgKeys,lang);

	// Attrib
	if(pdef->offAttrib==kAttribType_DynamicAttrib)
	{
		DynamicAttribParams *pParams = (DynamicAttribParams*)pdef->pParams;
		if(pParams && pParams->cpchAttribMessageKey)
		{
			char *pchAttribName = NULL;
			if(pdef->eTarget==kModTarget_Self || pdef->eTarget==kModTarget_SelfOnce)
			{
				AttribType eType = (AttribType)combateval_EvalNew(iPartitionIdx, pParams->pExprAttrib, kCombatEvalContext_Simple, NULL);
				AutoDescAttribName(eType, &pchAttribName, lang);
			}

			langFormatGameMessageKey(lang, &pAutoDescAttribMod->pchAttribName, pParams->cpchAttribMessageKey, STRFMT_STRING("AttribName", pchAttribName), STRFMT_END);
		}
		else
		{
			langFormatGameMessageKey(lang, &pAutoDescAttribMod->pchAttribName, "AutoDesc.AttribName.DynamicAttrib", STRFMT_END);
		}
	}
	else
	{
		AutoDescAttribName(pdef->offAttrib,&pAutoDescAttribMod->pchAttribName,lang);
	}

	// Chance
	if(pdef->bChanceNormalized || pdef->pExprChance)
	{
		F32 fChance = 1.0f;
		// TODO: Add support for a chance range based on the expected period range of the attrib mod
		if (pdef->pExprChance)
		{
			AttribMod Mod = {0};
			Mod.uiPeriod = 1;

			combateval_ContextReset(kCombatEvalContext_Apply);
			combateval_ContextSetupExpiration(pchar,&Mod,pdef,pdef->pPowerDef);
			fChance = combateval_EvalNew(iPartitionIdx,pdef->pExprChance,kCombatEvalContext_Apply,NULL);
		}

		if(pdef->bChanceNormalized)
		{
			F32 fTimeActivateMin, fTimeActivateMax;
			if(ppowdef->eType==kPowerType_Toggle)
			{
				fTimeActivateMin = ppowdef->fTimeActivatePeriod;
				fTimeActivateMax = 0;
			}
			else
			{
				F32 fSpeed = 1;
				if(pchar && ppow)
				{
					// Get the charge speed of this power
					fSpeed = character_GetSpeedCharge(iPartitionIdx, pchar, ppow);
				}

				fTimeActivateMin = ppowdef->fTimeActivate;
				fTimeActivateMax = ppowdef->fTimeCharge ? fTimeActivateMin + (ppowdef->fTimeCharge / fSpeed) : 0;
			}

			langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchChance,eDetail>kAutoDescDetail_Normal?"AutoDesc.AttribModDef.ChanceNormalized":"AutoDesc.AttribModDef.Chance",STRFMT_FLOAT("Chance",AutoDescRound(fChance*100.f)),STRFMT_END);

			if(fTimeActivateMin)
			{
				if(!fTimeActivateMax)
				{
					langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchChanceDenormalized,"AutoDesc.AttribModDef.Chance",STRFMT_FLOAT("Chance",AutoDescRound(fChance*100.f*fTimeActivateMin)),STRFMT_END);
				}
				else
				{
					// Kinda hacky - use the MagnitudeVariable message for building an X-Y string, and then pass that to
					//  the standard Chance message, instead of making a custom message for a chance range.
					char *pchChanceRange = NULL;
					F32 fChanceMin = AutoDescRound(fChance*100.f*fTimeActivateMin);
					F32 fChanceMax = AutoDescRound(fChance*100.f*fTimeActivateMax);
					MIN1(fChanceMin,100);
					MIN1(fChanceMax,100);
					estrStackCreate(&pchChanceRange);
					langFormatGameMessageKey(lang,&pchChanceRange,"AutoDesc.AttribModDef.MagnitudeVariable",STRFMT_FLOAT("SizeMin",fChanceMin),STRFMT_FLOAT("SizeMax",fChanceMax),STRFMT_END);
					langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchChanceDenormalized,"AutoDesc.AttribModDef.Chance",STRFMT_STRING("Chance",pchChanceRange),STRFMT_END);
					estrDestroy(&pchChanceRange);
				}
			}
		}
		else
		{
			langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchChance,"AutoDesc.AttribModDef.Chance",STRFMT_FLOAT("Chance",AutoDescRound(fChance*100.f)),STRFMT_END);
		}
	}

	// Delay (only delays over 2s in normal and down)
	if(pdef->fDelay > 2 || (eDetail > kAutoDescDetail_Normal && pdef->fDelay != 0.f))
	{
		langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchDelay,"AutoDesc.AttribModDef.Delay",STRFMT_FLOAT("Time",pdef->fDelay),STRFMT_END);
	}

	// Target
	pAutoDescAttribMod->pchTarget = AutoDescAttribModTarget(pdef,ppowdef,bModsTargetMixed,eDetail,lang);

	// StackLimit
	if(pdef->uiStackLimit)
	{
		langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchStackLimit,"AutoDesc.AttribModDef.StackLimit",STRFMT_INT("Limit",pdef->uiStackLimit),STRFMT_END);
	}

	// Period
	if(pdef->fPeriod)
	{
		langFormatGameMessageKey(lang,&pAutoDescAttribMod->pchPeriod,"AutoDesc.AttribModDef.Period",STRFMT_FLOAT("Time",pdef->fPeriod),STRFMT_END);
	}



	// Calculate the strength if necessary
	if(bIncludeStrength && ppowdef->eType!=kPowerType_Innate && pdef->eType!=kModType_None)
	{
		PowerDef *ppowdefActing = ppow ? GET_REF(ppow->hDef) : NULL;
		S32 iLevelInline = iLevel ? iLevel : (pchar && pchar->iLevelCombat ? pchar->iLevelCombat : 1); 
		iLevelInline = POWERLEVEL(ppow, iLevelInline); // Best guess for the level
		combateval_ContextReset(kCombatEvalContext_Apply);
		combateval_ContextSetupApply(pchar,NULL,pSourceItem,NULL);
		fStrength = moddef_GetStrength(	iPartitionIdx, pdef, pchar, ppowdefActing, iLevel, iLevelInline, 
										1, ppEnhancements, 0, NULL, true, NULL, &fStrAdd);
	}

	// Duration, and optional period compression
	if(pdef->pExprDuration)
	{
		bCompressPeriods = AutoDescAttribModDuration(	iPartitionIdx,
														pdef,
														&pAutoDescAttribMod->pchDuration,
														&iPeriods,
														&iPeriodsMax,
														fStrength,
														fStrAdd,
														ppowdef,
														pchar,
														pClass,
														iLevel,
														iCostPaidUnroll,
														eHeader,
														eDetail,
														pAutoDescAttribModEvent,
														bLocalFullDescription,
														lang);

		if(bCompressPeriods)
		{
			estrDestroy(&pAutoDescAttribMod->pchPeriod);
		}
	}

	// Magnitude
	if(pdef->pExprMagnitude || pdef->offAttrib==kAttribType_PowerRecharge)
	{
		AutoDescAttribModMagnitude(	iPartitionIdx,
									pdef,
									pAutoDescAttribMod,
									&pchMagnitudeVariableReason,
									&pchSign,
									fStrength,
									fStrAdd,
									ppow,
									ppowdef,
									pchar,
									pClass,
									iLevel,
									iCostPaidUnroll,
									bCompressPeriods,
									iPeriods,
									iPeriodsMax,
									fPowerAppsPerSecond,
									eDetail,
									pAutoDescAttribModEvent,
									lang);
	}

	// Entire effect (sans duration, chance, etc)
	AutoDescAttribModEffect(iPartitionIdx, pdef,
							pchar,
							&pAutoDescAttribMod->pchEffect,
							pchSign,
							pAutoDescAttribMod->pchAttribName,
							pAutoDescAttribMod->pchTarget,
							pAutoDescAttribMod->pchMagnitude,
							pAutoDescAttribMod->pchMagnitudeVariance,
							pchMagnitudeVariableReason,
							lang);

	// Inlined descriptions
	// Custom code for some of the specials
	// Only shown if we're not too deep or need a full description
	if(iDepth < 10 || bLocalFullDescription)
	{
		PowerDef *pdefInline = NULL;
		AutoDescPowerHeader eHeaderInline = kAutoDescPowerHeader_Apply;
		
		// Show anything (as opposed to only very simple inlines) if we're doing higher detail
		//  or we're not doing minimal detail and this PowerDef is really simple
		S32 bShowAny = eDetail>kAutoDescDetail_Normal
						|| (eDetail>kAutoDescDetail_Minimum
							&& pdef->pPowerDef==ppowdef
							&& !ppowdef->bMultiAttribPower);

		if(pdef->offAttrib==kAttribType_ApplyPower)
		{
			ApplyPowerParams *pParams = (ApplyPowerParams*)pdef->pParams;
			if(pParams)
			{
				pdefInline = GET_REF(pParams->hDef);
				if(pParams->eSource==pParams->eTarget)
				{
					eHeaderInline = kAutoDescPowerHeader_ApplySelf;
				}
			}
		}
		else if(pdef->offAttrib==kAttribType_DamageTrigger)
		{
			DamageTriggerParams *pParams = (DamageTriggerParams*)pdef->pParams;
			if(pParams)
			{
				pdefInline = GET_REF(pParams->hDef);
				if(pParams->eTarget==kDamageTriggerEntity_Self)
				{
					eHeaderInline = kAutoDescPowerHeader_ApplySelf;
				}
			}
		}
		else if(pdef->offAttrib==kAttribType_GrantPower)
		{
			GrantPowerParams *pParams = (GrantPowerParams*)pdef->pParams;
			if(pParams)
			{
				pdefInline = GET_REF(pParams->hDef);
			}
		}
		else if(pdef->offAttrib==kAttribType_IncludeEnhancement)
		{
			IncludeEnhancementParams *pParams = (IncludeEnhancementParams*)pdef->pParams;
			if(pParams)
			{
				pdefInline = GET_REF(pParams->hDef);
			}
		}
		else if(pdef->offAttrib==kAttribType_KillTrigger)
		{
			KillTriggerParams *pParams = (KillTriggerParams*)pdef->pParams;
			if(pParams)
			{
				pdefInline = GET_REF(pParams->hDef);
				if(pParams->eTarget==kKillTriggerEntity_Self)
				{
					eHeaderInline = kAutoDescPowerHeader_ApplySelf;
				}
			}
		}
		else if(pdef->offAttrib==kAttribType_TeleThrow)
		{
			TeleThrowParams *pParams = (TeleThrowParams*)pdef->pParams;
			if(pParams)
			{
				// Copy of the standard inline description code below for the fallback PowerDef
				pdefInline = GET_REF(pParams->hDefFallback);
				if(pdefInline
					&& (bLocalFullDescription
						|| ((!pppPowerDefsDescribed || -1==eaFind(pppPowerDefsDescribed,pdefInline))
							&& (bShowAny || !pdefInline->bMultiAttribPower))))
				{
					AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
					if(bLocalFullDescription)
						eHeaderInline = kAutoDescPowerHeader_Full;
					AutoDescPowerDefInternal(iPartitionIdx,pdefInline,NULL,pAutoDescPower,NULL,NULL,NULL,eHeaderInline,pchar,NULL,NULL,iLevel,bIncludeStrength,bLocalFullDescription,iDepth+3,eDetail,pAutoDescAttribMod,pppPowerDefsDescribed,pppchFootnoteMsgKeys,pExtract, NULL);
					eaPush(&pAutoDescAttribMod->ppPowersInline,pAutoDescPower);
				}

				pdefInline = GET_REF(pParams->hDef); // Normal method for the main PowerDef
			}
		}
		else if(pdef->offAttrib==kAttribType_TriggerComplex)
		{
			TriggerComplexParams *pParams = (TriggerComplexParams*)pdef->pParams;
			if(pParams)
			{
				pdefInline = GET_REF(pParams->hDef);
				eHeaderInline = kAutoDescPowerHeader_ApplySelf;
			}
		}
		else if(pdef->offAttrib==kAttribType_TriggerSimple)
		{
			TriggerSimpleParams *pParams = (TriggerSimpleParams*)pdef->pParams;
			if(pParams)
			{
				pdefInline = GET_REF(pParams->hDef);
				eHeaderInline = kAutoDescPowerHeader_ApplySelf;
			}
		}
		else if((pdef->offAttrib==kAttribType_EntCreate && (bShowAny || bLocalFullDescription))
				|| (pdef->offAttrib==kAttribType_BecomeCritter && bLocalFullDescription))
		{
			S32 bLocked = false;
			CritterDef *pdefCritter = NULL;

			if(pdef->offAttrib==kAttribType_EntCreate)
			{
				EntCreateParams *pParams = (EntCreateParams*)pdef->pParams;
				if(pParams && (pParams->eStrength==kEntCreateStrength_Locked || bLocalFullDescription))
				{
					bLocked = (pParams->eStrength==kEntCreateStrength_Locked);
					pdefCritter = GET_REF(pParams->hCritter);
				}
			}
			else
			{
				BecomeCritterParams *pParams = (BecomeCritterParams*)pdef->pParams;
				if(pParams)
					pdefCritter = GET_REF(pParams->hCritter);
			}

			if(pdefCritter)
			{
				int s=eaSize(&pdefCritter->ppPowerConfigs);
				for(i=0; i<s; i++)
				{
					PowerDef *pdefCritterPower = GET_REF(pdefCritter->ppPowerConfigs[i]->hPower);

					if(pdefCritter->ppPowerConfigs[i]->bDisabled)
						continue;

					if(!bLocalFullDescription && eDetail <= kAutoDescDetail_Normal && pdefCritter->ppPowerConfigs[i]->bAutoDescDisabled)
						continue;

					if(pdefCritterPower)
					{
						if(bLocalFullDescription
							|| (pdefCritterPower->eType!=kPowerType_Innate
								&& !pdefCritterPower->pExprEnhanceApply // Hides "Critical Hit" style Enhancements
								&& (!pppPowerDefsDescribed || -1==eaFind(pppPowerDefsDescribed,pdefCritterPower))))
						{
							AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
							// TODO: This is still wrong if bLocked is false - need to pass in the Critter's CharacterClass, which is complicated to get, plus changes to the rest of the functions to handle it
							AutoDescPowerDefInternal(iPartitionIdx,pdefCritterPower,NULL,pAutoDescPower,NULL,NULL,NULL,kAutoDescPowerHeader_Full,bLocked?pchar:NULL,NULL,NULL,iLevel,bLocked?bIncludeStrength:false,bLocalFullDescription,iDepth+4,eDetail,pAutoDescAttribMod,pppPowerDefsDescribed,pppchFootnoteMsgKeys,pExtract, NULL);
							eaPush(&pAutoDescAttribMod->ppPowersInline,pAutoDescPower);
						}
					}
				}
			}
		}

		if(pdefInline
			&& (bLocalFullDescription
				|| ((!pppPowerDefsDescribed || -1==eaFind(pppPowerDefsDescribed,pdefInline))
					&& (bShowAny || !pdefInline->bMultiAttribPower))))
		{
			AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
			if(bLocalFullDescription)
				eHeaderInline = kAutoDescPowerHeader_Full;
			AutoDescPowerDefInternal(iPartitionIdx,pdefInline,NULL,pAutoDescPower,NULL,NULL,NULL,eHeaderInline,pchar,NULL,NULL,iLevel,bIncludeStrength,bLocalFullDescription,iDepth+3,eDetail,pAutoDescAttribMod,pppPowerDefsDescribed,pppchFootnoteMsgKeys,pExtract, NULL);
			eaPush(&pAutoDescAttribMod->ppPowersInline,pAutoDescPower);
		}
	}

	if(pdef->pExpiration)
	{
		PowerDef *pdefExpiration = GET_REF(pdef->pExpiration->hDef);
		if(eDetail>kAutoDescDetail_Minimum)
		{
			if(pdefExpiration && (bLocalFullDescription || !pppPowerDefsDescribed || -1==eaFind(pppPowerDefsDescribed,pdefExpiration)))
			{
				AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
				AutoDescPowerHeader eHeaderInline = (pdef->pExpiration->eTarget==kModExpirationEntity_ModTarget) ? kAutoDescPowerHeader_ApplySelf : kAutoDescPowerHeader_Apply;
				if(bLocalFullDescription)
					eHeaderInline = kAutoDescPowerHeader_Full;
				AutoDescPowerDefInternal(iPartitionIdx,pdefExpiration,NULL,pAutoDescPower,NULL,NULL,NULL,eHeaderInline,pchar,NULL,NULL,iLevel,bIncludeStrength,bLocalFullDescription,iDepth+3,eDetail,pAutoDescAttribMod,pppPowerDefsDescribed,pppchFootnoteMsgKeys,pExtract, NULL);
				pAutoDescAttribMod->pPowerExpire = pAutoDescPower;
			}
		}
	}

	// Copy the sub-Powers into either the single sub-Power or the indexed array.  EntCreates & BecomeCritters always end up in the indexed array.
	i = eaSize(&pAutoDescAttribMod->ppPowersInline) + !!pAutoDescAttribMod->pPowerExpire;
	if(i==1 && pdef->offAttrib!=kAttribType_EntCreate && pdef->offAttrib!=kAttribType_BecomeCritter)
	{
		pAutoDescAttribMod->pPowerSub = pAutoDescAttribMod->pPowerExpire ? pAutoDescAttribMod->pPowerExpire : pAutoDescAttribMod->ppPowersInline[0];
	}
	else if(i>1 || (i==1 && (pdef->offAttrib==kAttribType_EntCreate || pdef->offAttrib==kAttribType_BecomeCritter)))
	{
		eaIndexedEnable(&pAutoDescAttribMod->ppPowersSubIndexed, parse_AutoDescPower);
		for(i=eaSize(&pAutoDescAttribMod->ppPowersInline)-1; i>=0; i--)
			eaIndexedAdd(&pAutoDescAttribMod->ppPowersSubIndexed,pAutoDescAttribMod->ppPowersInline[i]);
		if(pAutoDescAttribMod->pPowerExpire)
			eaIndexedAdd(&pAutoDescAttribMod->ppPowersSubIndexed,pAutoDescAttribMod->pPowerExpire);
	}

	// Custom message (only in non-maximum mode)
	if(eDetail!=kAutoDescDetail_Maximum)
	{
		// Make the default message (in here we don't actually know what the real key
		//  is, but we could find some way to pass this in later)
		langFormatMessageKey(lang,&pAutoDescAttribMod->pchDefault,"AutoDesc.AttribModDef.Complete",
			STRFMT_STRING("Dev",pAutoDescAttribMod->pchDev),
			STRFMT_STRING("Target",pAutoDescAttribMod->pchTarget),
			STRFMT_STRING("Requires",pAutoDescAttribMod->pchRequires),
			STRFMT_STRING("Affects",pAutoDescAttribMod->pchAffects),
			STRFMT_STRING("Chance",pAutoDescAttribMod->pchChance),
			STRFMT_STRING("Delay",pAutoDescAttribMod->pchDelay),
			STRFMT_STRING("Description",pAutoDescAttribMod->pchEffect),
			STRFMT_STRING("Period",pAutoDescAttribMod->pchPeriod),
			STRFMT_STRING("Duration",pAutoDescAttribMod->pchDuration),
			STRFMT_END);

		if(!iCostPaidUnroll && IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage))
		{
			// The custom message gets all that, plus the entire struct, magnitude the default description.
			//  In this case, the Effect field is actually called Effect.
			langFormatGameDisplayMessage(lang,&pAutoDescAttribMod->pchCustom,&pdef->msgAutoDesc,
				STRFMT_STRUCT("m",pAutoDescAttribMod,parse_AutoDescAttribMod),
				STRFMT_STRING("Magnitude",pAutoDescAttribMod->pchMagnitude),
				STRFMT_STRING("Default",pAutoDescAttribMod->pchDefault),
				STRFMT_STRING("Dev",pAutoDescAttribMod->pchDev),
				STRFMT_STRING("Target",pAutoDescAttribMod->pchTarget),
				STRFMT_STRING("Requires",pAutoDescAttribMod->pchRequires),
				STRFMT_STRING("Affects",pAutoDescAttribMod->pchAffects),
				STRFMT_STRING("Chance",pAutoDescAttribMod->pchChance),
				STRFMT_STRING("Delay",pAutoDescAttribMod->pchDelay),
				STRFMT_STRING("Effect",pAutoDescAttribMod->pchEffect),
				STRFMT_STRING("Period",pAutoDescAttribMod->pchPeriod),
				STRFMT_STRING("Duration",pAutoDescAttribMod->pchDuration),
				STRFMT_END);
		}
	}
}


static void AutoDescAttribModDefInnate(int iPartitionIdx,
									   SA_PARAM_NN_VALID AttribModDef *pdef,
									   SA_PARAM_OP_VALID char **ppchDesc,
									   SA_PARAM_OP_VALID AutoDescAttribMod *pAutoDescAttribMod,
									   SA_PARAM_OP_VALID AutoDescInnateModDetails *pDetails,
									   F32 fMagnitude,
									   Language lang,
									   AutoDescDetail eDetail,
									   SA_PARAM_OP_VALID Message* pCustomMsg)
{
	S32 bPercent = false;
	
	const char *pchSign = NULL;
	
	char *pchTemp = NULL;
	char *pchMagnitude = NULL;
	char *pchAttrib = NULL;
	char *pchEffect = NULL;

	estrStackCreate(&pchTemp);
	estrStackCreate(&pchMagnitude);
	estrStackCreate(&pchAttrib);
	estrStackCreate(&pchEffect);

	// Scale for percents
	if(ASPECT_PERCENT(pdef->offAspect))
	{
		fMagnitude *= 100.f;
	}
	else if(ATTRIB_ASPECT_PERCENT(pdef->offAttrib,pdef->offAspect))
	{
		// Percent-type attribs
		fMagnitude *= 100.f;
		bPercent = true;
	}
	else if(ATTRIB_ASPECT_SCALED(pdef->offAttrib,pdef->offAspect))
	{
		// Config-scaled attribs
		fMagnitude *= 100.f;
	}

	if(eDetail<kAutoDescDetail_Maximum)
	{
		if(ATTRIB_ASPECT_FLOOR(pdef->offAttrib,pdef->offAspect))
		{
			fMagnitude = AutoDescFloor(fMagnitude);
		}
		else
		{
			fMagnitude = AutoDescRound(fMagnitude);
		}
	}

	langFormatGameMessageKey(lang,&pchMagnitude,"AutoDesc.AttribModDef.Magnitude",STRFMT_FLOAT("Size",fMagnitude),STRFMT_END);

	if(fMagnitude > 0)
	{
		pchSign = langTranslateMessageKey(lang,"AutoDesc.AttribModDef.MagnitudeSignPositive");
	}

	if(ATTRIB_BOOLEAN(pdef->offAttrib)
		&& fMagnitude == 1)
	{
		estrClear(&pchMagnitude);
		pchSign = NULL;
	}

	// Attrib
	if(pdef->offAttrib==kAttribType_DynamicAttrib)
	{
		DynamicAttribParams *pParams = (DynamicAttribParams*)pdef->pParams;
		if(pParams && pParams->cpchAttribMessageKey)
		{
			char *pchAttribName = NULL;
			if(pdef->eTarget==kModTarget_Self || pdef->eTarget==kModTarget_SelfOnce)
			{
				AttribType eType = (AttribType)combateval_EvalNew(iPartitionIdx, pParams->pExprAttrib, kCombatEvalContext_Simple, NULL);
				AutoDescAttribName(eType, &pchAttribName, lang);
			}

			langFormatGameMessageKey(lang, &pchAttrib, pParams->cpchAttribMessageKey, STRFMT_STRING("AttribName", pchAttribName), STRFMT_END);
		}
		else
		{
			langFormatGameMessageKey(lang, &pchAttrib, "AutoDesc.AttribName.DynamicAttrib", STRFMT_END);
		}
	}
	else
	{
		AutoDescAttribName(pdef->offAttrib,&pchAttrib,lang);
	}

	// Description
	if(IS_DAMAGE_ATTRIBASPECT(pdef->offAttrib,pdef->offAspect))
	{
		estrCopy2(&pchTemp,"AutoDesc.AttribModDef.AttribAspectDamage");
	}
	else
	{
		S32 bCustom = false;

		if(pdef->offAspect==kAttribAspect_BasicAbs)
		{
			const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib);
			estrPrintf(&pchTemp,"AutoDesc.AttribModDef.Attrib%s",pchAttribBase);
			if(RefSystem_ReferentFromString(gMessageDict,pchTemp))
			{
				// Found a custom default message at that key
				bCustom = true;
			}
		}

		if(!bCustom)
		{
			const char *pchAspectBase = StaticDefineIntRevLookup(AttribAspectEnum,pdef->offAspect);
			estrPrintf(&pchTemp,"AutoDesc.AttribModDef.Aspect%s",pchAspectBase);
			if(bPercent)
			{
				estrAppend2(&pchTemp,".Percent");
			}
		}
	}

	if (ppchDesc || pAutoDescAttribMod)
	{
		langFormatMessageKey(lang,&pchEffect,pchTemp,
			STRFMT_STRING("Sign",pchSign),
			STRFMT_STRING("Attrib",pchAttrib),
			STRFMT_STRING("Magnitude",pchMagnitude),
			STRFMT_STRING("Variance",NULL),
			STRFMT_STRING("Reason",NULL),
			STRFMT_END);
	}

	if(ppchDesc)
	{
		// Custom message (only in non-maximum mode)
		if(eDetail!=kAutoDescDetail_Maximum && pCustomMsg)
		{
			langFormatGameMessage(lang,ppchDesc,pCustomMsg,
				STRFMT_STRING("Magnitude",pchMagnitude),
				STRFMT_STRING("Duration",""),
				STRFMT_STRING("Effect",pchEffect),
				STRFMT_END);
		}
		else
		{
			estrAppend(ppchDesc,&pchEffect);
		}
	}
	else if(pAutoDescAttribMod)
	{
		estrCopy(&pAutoDescAttribMod->pchAttribName,&pchAttrib);
		estrCopy(&pAutoDescAttribMod->pchMagnitude,&pchMagnitude);
		estrCopy(&pAutoDescAttribMod->pchEffect,&pchEffect);
		estrCopy(&pAutoDescAttribMod->pchDefault,&pchEffect);

		// Custom message (only in non-maximum mode)
		if(eDetail!=kAutoDescDetail_Maximum && pCustomMsg)
		{
			langFormatGameMessage(lang,&pAutoDescAttribMod->pchCustom,pCustomMsg,
				STRFMT_STRING("Magnitude",pchMagnitude),
				STRFMT_STRING("Duration",""),
				STRFMT_STRING("Effect",pchEffect),
				STRFMT_END);
		}
	}
	else if (pDetails)
	{
		pDetails->fMagnitude = fMagnitude;
		pDetails->bAttribBoolean = ATTRIB_BOOLEAN(pdef->offAttrib);
		pDetails->bPercent = bPercent || ASPECT_PERCENT(pdef->offAspect);
		pDetails->bDamageAttribAspect = IS_DAMAGE_ATTRIBASPECT(pdef->offAttrib,pdef->offAspect);
		pDetails->offAttrib = pdef->offAttrib;
		pDetails->offAspect = pdef->offAspect;
		estrCopy2(&pDetails->pchDefaultMessage,pchTemp);
		estrCopy2(&pDetails->pchAttribName,pchAttrib);
		AutoDescAttribDesc(pdef->offAttrib, &pDetails->pchDesc, lang);
		AutoDescAttribDescLong(pdef->offAttrib, &pDetails->pchDescLong, lang);
		pDetails->pchPowerDef = SAFE_MEMBER2(pdef, pPowerDef, pchName);
	}

	estrDestroy(&pchTemp);
	estrDestroy(&pchMagnitude);
	estrDestroy(&pchAttrib);
	estrDestroy(&pchEffect);
}



void modnet_AutoDesc(Character *pchar,
					 AttribModNet *pmodnet,
					 char **ppchDesc,
					 Language lang,
					 AutoDescDetail eDetail)
{
	S32 bPercent = false;

	char *pchTemp = NULL;

	char *pchPeriod = NULL;
	char *pchDuration = NULL;
	char *pchMagnitude = NULL;
	const char *pchSign = NULL;
	char *pchAttrib = NULL;
	char *pchDescription = NULL;

	AttribModDef *pdef = modnet_GetDef(pmodnet);
	PowerDef *pdefPower = GET_REF(pmodnet->hPowerDef);
	
	if(!pdef)
		return;

	if(eDetail<kAutoDescDetail_Maximum
		&& (pdef->offAttrib==kAttribType_Null
			|| pdef->bAutoDescDisabled))
		return;

	// Period
	if(pdef->fPeriod)
	{
		langFormatGameMessageKey(lang,&pchPeriod,"AutoDesc.AttribModDef.Period",STRFMT_FLOAT("Time",pdef->fPeriod),STRFMT_END);
	}

	// Duration
	if(pmodnet->uiDuration > 0
		&& (!pdefPower->fTimeActivatePeriod
			|| pmodnet->uiDurationOriginal < pdefPower->fTimeActivatePeriod))
	{
		U32 uiDuration = character_ModNetGetPredictedDuration(pchar, pmodnet);

		if(pdef->bForever)
		{
			langFormatMessageKey(lang,&pchDuration,"AutoDesc.AttribModDef.DurationForever",STRFMT_END);
		}
		else
		{
			langFormatGameMessageKey(lang,&pchDuration,"AutoDesc.AttribModDef.Duration",STRFMT_INT("Time",uiDuration),STRFMT_END);
		}
	}

	// Magnitude
	if(pmodnet->iMagnitude != 0)
	{
		F32 fMagnitude = (F32)pmodnet->iMagnitude / ATTRIBMODNET_MAGSCALE;

		// Scale for percents
		if(ASPECT_PERCENT(pdef->offAspect))
		{
			fMagnitude *= 100.f;
		}
		else if(ATTRIB_ASPECT_PERCENT(pdef->offAttrib,pdef->offAspect))
		{
			// Percent-type attribs
			fMagnitude *= 100.f;
			bPercent = true;
		}
		else if(ATTRIB_ASPECT_SCALED(pdef->offAttrib,pdef->offAspect))
		{
			// Otherwise-scaled attribs
			fMagnitude *= 100.f;
		}

		if(eDetail<kAutoDescDetail_Maximum)
		{
			fMagnitude = AutoDescRound(fMagnitude);
		}

		langFormatGameMessageKey(lang,&pchMagnitude,"AutoDesc.AttribModDef.Magnitude",STRFMT_FLOAT("Size",fMagnitude),STRFMT_END);

		if(fMagnitude > 0)
		{
			pchSign = TranslateMessageKey("AutoDesc.AttribModDef.MagnitudeSignPositive");
		}

		if(ATTRIB_BOOLEAN(pdef->offAttrib)
			&& fMagnitude == 1)
		{
			estrClear(&pchMagnitude);
			pchSign = NULL;
		}
	}

	// Attrib
	AutoDescAttribName(pdef->offAttrib,&pchAttrib,lang);

	// Description
	if(IS_DAMAGE_ATTRIBASPECT(pdef->offAttrib,pdef->offAspect))
	{
		estrCopy2(&pchTemp,"AutoDesc.AttribModDef.AttribAspectDamage");
	}
	else
	{
		S32 bCustom = false;

		if(pdef->offAspect==kAttribAspect_BasicAbs)
		{
			const char *pchAttribBase = StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib);
			estrPrintf(&pchTemp,"AutoDesc.AttribModDef.Attrib%s",pchAttribBase);
			if(RefSystem_ReferentFromString(gMessageDict,pchTemp))
			{
				// Found a custom default message at that key
				bCustom = true;
			}
		}

		if(!bCustom)
		{
			const char *pchAspectBase = StaticDefineIntRevLookup(AttribAspectEnum,pdef->offAspect);
			estrPrintf(&pchTemp,"AutoDesc.AttribModDef.Aspect%s",pchAspectBase);
			if(bPercent)
			{
				estrAppend2(&pchTemp,".Percent");
			}
		}
	}

	langFormatMessageKey(lang,&pchDescription,pchTemp,
		STRFMT_STRING("Sign",pchSign),
		STRFMT_STRING("Attrib",pchAttrib),
		STRFMT_STRING("Magnitude",pchMagnitude),
		STRFMT_STRING("Variance",NULL),
		STRFMT_STRING("Reason",NULL),
		STRFMT_END);

	langFormatMessageKey(lang,ppchDesc,"AutoDesc.AttribModDef.Complete",
		STRFMT_STRING("Dev",NULL),
		STRFMT_STRING("Prefix",NULL),
		STRFMT_STRING("Target",NULL),
		STRFMT_STRING("Requires",NULL),
		STRFMT_STRING("Affects",NULL),
		STRFMT_STRING("Chance",NULL),
		STRFMT_STRING("Delay",NULL),
		STRFMT_STRING("Description",pchDescription),
		STRFMT_STRING("Period",pchPeriod),
		STRFMT_STRING("Duration",pchDuration),
		STRFMT_STRING("Suffix",NULL),
		STRFMT_END);

	estrDestroy(&pchTemp);
	
	estrDestroy(&pchPeriod);
	estrDestroy(&pchDuration);
	estrDestroy(&pchMagnitude);
	estrDestroy(&pchAttrib);
	estrDestroy(&pchDescription);
}



// TODO(JW): AutoDesc: Design reasonable API for this
// Very similar to AutoDescAttribModDefInnate
void AutoDescPowerStat(char **ppchDesc,
					   PowerStat *pstat,
					   CharacterClass *pclass,
					   F32 *pfStats,
					   int iStatSourceIndex,
					   int iLevel,
					   int iNodeRank,
					   Language lang,
					   AutoDescDetail eDetail)
{
	S32 bPercent = false;

	const char *pchSign = NULL;
	const char *pchAspect = NULL;

	char *pchAttrib = NULL;
	char *pchKey = NULL;

	F32 fMagnitude = powerstat_Eval(pstat,pclass,pfStats,iLevel,iNodeRank);

	if(eafSize(&pfStats))
	{
		F32 fPercentContrib = 1.f;
		F32 fStatSum = 0.0f;
		int i, s = eaSize(&pstat->ppSourceStats);
		for(i=0; i<s; i++)
		{
			fStatSum += (pfStats[i] * pstat->ppSourceStats[i]->fMultiplier);
		}
		if(fStatSum != 0.f)
		{
			fPercentContrib = (pfStats[iStatSourceIndex] * pstat->ppSourceStats[iStatSourceIndex]->fMultiplier) /(fStatSum);
		}
		else
		{
			fPercentContrib = 0.f;
		}

		fMagnitude *= fPercentContrib;
	}
	
	// Scale for percents
	if(ASPECT_PERCENT(pstat->offTargetAspect))
	{
		fMagnitude *= 100.f;
	}
	else if(ATTRIB_ASPECT_PERCENT(pstat->offTargetAttrib,pstat->offTargetAspect))
	{
		// Percent-type attribs
		fMagnitude *= 100.f;
		bPercent = true;
	}
	else if(ATTRIB_ASPECT_SCALED(pstat->offTargetAttrib,pstat->offTargetAspect))
	{
		// Config-scaled attribs
		fMagnitude *= 100.f;
	}

	if(eDetail<kAutoDescDetail_Maximum)
	{
		if(ATTRIB_ASPECT_FLOOR(pstat->offTargetAttrib,pstat->offTargetAspect))
		{
			fMagnitude = AutoDescFloor(fMagnitude);
		}
		else
		{
			fMagnitude = AutoDescRound(fMagnitude);
		}
	}

	// Sign
	if(fMagnitude >= 0)
	{
		pchSign = langTranslateMessageKey(lang,"AutoDesc.PowerStat.SignPositive");
	}
	else
	{
		//pchSign = TranslateMessageKey("AutoDesc.PowerStat.SignNegative");
	}

	// Attrib
	AutoDescAttribName(pstat->offTargetAttrib,&pchAttrib,lang);

	// Aspect
	pchAspect = StaticDefineIntRevLookup(AttribAspectEnum,pstat->offTargetAspect);

	// Key
	estrPrintf(&pchKey,"AutoDesc.PowerStat.Aspect%s",pchAspect);
	if(bPercent)
		estrAppend2(&pchKey,".Percent");
	if(!RefSystem_ReferentFromString(gMessageDict,pchKey))
	{
		estrCopy2(&pchKey,"AutoDesc.PowerStat.Default");
	}

	langFormatGameMessageKey(lang,ppchDesc,pchKey,
		STRFMT_STRING("Sign",pchSign),
		STRFMT_STRING("Aspect",pchAspect),
		STRFMT_STRING("Attrib",pchAttrib),
		STRFMT_FLOAT("Magnitude", fMagnitude),
		STRFMT_END);

	estrDestroy(&pchKey);
	estrDestroy(&pchAttrib);
}

void attrib_AutoDescPowerStatsInternal(
			char **ppchDesc,
			AttribType eAttrib,
			CharacterClass *pclass, 
			Character *pchar, 
			Language lang,
			AutoDescDetail eDetail,
			F32 *pfStats,
			bool *pbNoEffect,
			const char *pchSortingTag,
			bool bInclude)
{
	PowerStat **ppStats = pclass->ppStatsFull ? pclass->ppStatsFull : g_PowerStats.ppPowerStats;
	S32 iStatIdx;
	bool bSorting = !!pchSortingTag;
	int i,s=eaSize(&ppStats);
	for(i=0; i<s; i++)
	{
		PowerStat *pStat = ppStats[i];
		int j, t = eaSize(&pStat->ppSourceStats);
		PTNode *pNode = NULL;
		iStatIdx = -1;

		if (bSorting)
		{
			bool bMatch = pStat->pchTag && pchSortingTag && strcmp(pStat->pchTag, pchSortingTag) == 0;
			if (bInclude != bMatch)
				continue;
		}

		if(!powerstat_Active(ppStats[i],pchar,&pNode))
			continue;

		for(j=0; j<t; j++)
		{
			if(pStat->ppSourceStats[j]->offSourceAttrib==eAttrib)
			{
				iStatIdx = j;
				break;
			}
		}

		if(iStatIdx >= 0)
		{
			if(estrLength(ppchDesc))
				estrAppend2(ppchDesc,"<br>");

			for(j=0; j<t ;j++)
				eafPush(&pfStats, *F32PTR_OF_ATTRIB(pchar->pattrBasic, pStat->ppSourceStats[j]->offSourceAttrib));

			if(eafSize(&pfStats))
			{
				AutoDescPowerStat(ppchDesc,pStat,pclass,pfStats,iStatIdx,pchar->iLevelCombat,pNode?pNode->iRank+1:0,lang,eDetail);
				*pbNoEffect = false;
			}

			eafClearFast(&pfStats);
		}
	}
}

void attrib_AutoDescPowerStats(char **ppchDesc,
							   AttribType eAttrib,
							   Character *pchar,
							   Language lang,
							   AutoDescDetail eDetail,
							   const char* pchPrimaryHeader, 
							   const char* pchSecondaryHeader,
							   const char *pchSortingTag)
{
	static F32 *pfStats = NULL;
	CharacterClass *pclass = character_GetClassCurrent(pchar);
	bool bNoEffect = true;
	pchSortingTag = allocFindString(pchSortingTag);

	if(pclass && IS_NORMAL_ATTRIB(eAttrib))
	{
		if (pchSortingTag)
		{
			if (pchPrimaryHeader)
				estrAppend2(ppchDesc,pchPrimaryHeader);
			attrib_AutoDescPowerStatsInternal(ppchDesc, eAttrib, pclass, pchar, lang, eDetail, pfStats, &bNoEffect, pchSortingTag, true);
			if (pchPrimaryHeader)
				estrAppend2(ppchDesc,pchSecondaryHeader);
			attrib_AutoDescPowerStatsInternal(ppchDesc, eAttrib, pclass, pchar, lang, eDetail, pfStats, &bNoEffect, pchSortingTag, false);
		}
		else
		{
			attrib_AutoDescPowerStatsInternal(ppchDesc, eAttrib, pclass, pchar, lang, eDetail, pfStats, &bNoEffect, NULL, false);
		}
	}

	if(bNoEffect && RefSystem_ReferentFromString(gMessageDict, "AutoDesc.PowerStat.NoEffect"))
	{
		langFormatMessageKey(lang,ppchDesc,"AutoDesc.PowerStat.NoEffect",STRFMT_STRING("Attrib",attrib_AutoDescName(eAttrib,lang)),STRFMT_END);
	}
}

// Automatic description for the attribute granted by a PTNodeDef at a particular rank (0-based)
void powertreenode_AutoDescAttrib(char **ppchDesc,
								  PTNodeDef *pdef,
								  int iRank,
								  Language lang)
{
	char *pchAttrib = NULL;
	F32 fResult;

	if(pdef->eAttrib < 0)
	{
		return;
	}

	// Default result is just the rank of the node + 1 (since rank is 0-based)
	fResult = iRank + 1;

	if(pdef->pchAttribPowerTable)
	{
		// Use a table lookup instead
		fResult = powertable_Lookup(pdef->pchAttribPowerTable,iRank);
	}

	if(ATTRIB_ASPECT_SCALED(pdef->eAttrib,kAttribAspect_BasicAbs))
	{
		// Config-scaled attribs
		fResult *= 100.f;
	}

	// Attrib
	AutoDescAttribName(pdef->eAttrib,&pchAttrib,lang);

	langFormatGameMessageKey(lang,ppchDesc,"AutoDesc.PowerTreeNode.Attrib",
		STRFMT_STRING("Attrib", pchAttrib),
		STRFMT_FLOAT("Magnitude", fResult),
		STRFMT_END);

	estrDestroy(&pchAttrib);
}


void combatevent_AutoDesc(char **ppchDesc,
						  EntityRef erTarget,
						  EntityRef erOwner,
						  EntityRef erSource,
						  S32 iAttrib,
						  F32 fMagnitude,
						  F32 fMagnitudeBase,
						  Message *pMsg,
						  Message *pSecondaryMsg,
						  CombatTrackerFlag eFlags,
						  U32 bPositive)
{

#ifdef GAMECLIENT
	Entity *ePlayer = entActivePlayerPtr();
	EntityRef erPlayer = ePlayer ? entGetRef(ePlayer) : 0;
	const char *cpchKey = "AutoDesc.CombatEvent.OtherToOther";
	const char *cpchSource = TranslateMessageKey("AutoDesc.CombatEvent.UnknownEntity");
	const char *cpchTarget = TranslateMessageKey("AutoDesc.CombatEvent.UnknownEntity");
	char *pchSourceFull = NULL;
	char *pchTargetFull = NULL;
	char *pchAttrib = NULL;
	char *pchKeyCompiled = NULL;
	Language lang = langGetCurrent();
	int iPartitionIdx = entGetPartitionIdx(ePlayer);

	const char *cpchPower = TranslateMessagePtr(pMsg);
	const char *cpchSecondaryPower = NULL;

	if(pSecondaryMsg){//there probably isn't one.
		cpchSecondaryPower = TranslateMessagePtr(pSecondaryMsg);
	}
	if(!cpchPower)
	{
		cpchPower = TranslateMessageKey("AutoDesc.CombatEvent.UnknownPower");
	}

	// If the owner is around, or there's no source, we probably want to describe this as
	//  coming from the owner.
	if(entFromEntityRef(iPartitionIdx, erOwner) || !erSource)
	{
		// Note that this is the credit owner, not AttribMod owner.  Pets are the source but not
		//  the owner for their attacks in pretty much all cases.  Since we still want to report
		//  pets verbosely we leave the erSource intact if it's got an erOwner that matches this
		//  event's erOwner.
		Entity *eSource = entFromEntityRef(iPartitionIdx, erSource);
		if(!eSource || eSource->erOwner!=erOwner || !(entGetType(eSource) == GLOBALTYPE_ENTITYSAVEDPET || entCheckFlag(eSource, ENTITYFLAG_CRITTERPET)))
			erSource = erOwner;
	}
	
	// EntityToEntity key
	if(erPlayer && erPlayer==erSource)
	{
		if(erSource==erTarget)
		{
			cpchKey = "AutoDesc.CombatEvent.SelfToSelf";
		}
		else
		{
			cpchKey = "AutoDesc.CombatEvent.SelfToOther";
		}
	}
	else if(!erSource)
	{
		if(erPlayer && erPlayer==erTarget)
		{
			cpchKey = "AutoDesc.CombatEvent.EnvironmentToSelf";
		}
		else
		{
			cpchKey = "AutoDesc.CombatEvent.EnvironmentToOther";
		}
	}
	else
	{
		if(erPlayer && erPlayer==erTarget)
		{
			cpchKey = "AutoDesc.CombatEvent.OtherToSelf";
		}
	}

	// Custom Keys for certain Flags, Shields and Positive values
	if(!fMagnitude && !fMagnitudeBase && eFlags&kCombatTrackerFlag_Miss)
	{
		estrStackCreate(&pchKeyCompiled);
		estrCopy2(&pchKeyCompiled,cpchKey);
		estrAppend2(&pchKeyCompiled,".Miss");
		if(!RefSystem_ReferentFromString(gMessageDict,pchKeyCompiled))
			estrDestroy(&pchKeyCompiled);
	}
	else if(!fMagnitude && !fMagnitudeBase && eFlags&kCombatTrackerFlag_Dodge)
	{
		estrStackCreate(&pchKeyCompiled);
		estrCopy2(&pchKeyCompiled,cpchKey);
		estrAppend2(&pchKeyCompiled,".Dodge");
		if(!RefSystem_ReferentFromString(gMessageDict,pchKeyCompiled))
			estrDestroy(&pchKeyCompiled);
	}
	else if(iAttrib==kAttribType_Shield)
	{
		estrStackCreate(&pchKeyCompiled);
		estrCopy2(&pchKeyCompiled,cpchKey);
		estrAppend2(&pchKeyCompiled,".Shield");
	}
	else if(bPositive)
	{
		estrStackCreate(&pchKeyCompiled);
		estrCopy2(&pchKeyCompiled,cpchKey);
		estrAppend2(&pchKeyCompiled,".Positive");
	}

	// Source name
	if(erSource)
	{
		Entity *eSource = entFromEntityRef(iPartitionIdx, erSource);
		if(eSource)
		{
			cpchSource = entGetLocalName(eSource);
			if(eSource->erOwner)
			{
				if(erPlayer && erPlayer==eSource->erOwner)
				{
					FormatMessageKey(&pchSourceFull,"AutoDesc.CombatEvent.SelfPet",STRFMT_STRING("Pet",cpchSource),STRFMT_END);
				}
				else
				{
					Entity *eOwner = entFromEntityRef(iPartitionIdx, eSource->erOwner);
					if(eOwner)
					{
						FormatMessageKey(&pchSourceFull,"AutoDesc.CombatEvent.OtherPet",STRFMT_STRING("Owner",entGetLocalName(eOwner)),STRFMT_STRING("Pet",cpchSource),STRFMT_END);
					}
				}
			}
		}
	}

	// Target name
	if(erTarget)
	{
		Entity *eTarget = entFromEntityRef(iPartitionIdx, erTarget);
		if(eTarget)
		{
			cpchTarget = entGetLocalName(eTarget);
			if(eTarget->erOwner)
			{
				if(erPlayer && erPlayer==eTarget->erOwner)
				{
					FormatMessageKey(&pchTargetFull,"AutoDesc.CombatEvent.SelfPet",STRFMT_STRING("Pet",cpchTarget),STRFMT_END);
				}
				else
				{
					Entity *eOwner = entFromEntityRef(iPartitionIdx, eTarget->erOwner);
					if(eOwner)
					{
						FormatMessageKey(&pchTargetFull,"AutoDesc.CombatEvent.OtherPet",STRFMT_STRING("Owner",entGetLocalName(eOwner)),STRFMT_STRING("Pet",cpchTarget),STRFMT_END);
					}
				}
			}
		}
	}

	// Attrib
	AutoDescAttribName(iAttrib,&pchAttrib,lang);

	// Make sure we always say at least 1 if it's bigger than 0
	fMagnitude = fMagnitude < 1 ? ceil(fMagnitude) : round(fMagnitude);

	// If base magnitude isn't 0, use the dual magnitude format
	if(fMagnitudeBase)
	{
		char *pchDualMagnitude = NULL;

		// Make sure we always say at least 1 if it's bigger than 0
		fMagnitudeBase = fMagnitudeBase < 1 ? ceil(fMagnitudeBase) : round(fMagnitudeBase);
		
		FormatMessageKey(&pchDualMagnitude,"AutoDesc.CombatEvent.DualMagnitude",STRFMT_INT("Magnitude",fMagnitude),STRFMT_INT("MagnitudeBase",fMagnitudeBase),STRFMT_END);

		FormatMessageKey(ppchDesc,pchKeyCompiled?pchKeyCompiled:cpchKey,
			STRFMT_STRING("Source",pchSourceFull?pchSourceFull:cpchSource),
			STRFMT_STRING("Target",pchTargetFull?pchTargetFull:cpchTarget),
			STRFMT_STRING("Attrib",pchAttrib),
			STRFMT_STRING("Magnitude",pchDualMagnitude),
			STRFMT_STRING("Power",cpchPower),
			STRFMT_STRING("SecondaryPower",cpchSecondaryPower),
			STRFMT_INT("Critical",(eFlags & kCombatTrackerFlag_Critical)!=0),
			STRFMT_END);

		estrDestroy(&pchDualMagnitude);
	}
	else
	{
		FormatMessageKey(ppchDesc,pchKeyCompiled?pchKeyCompiled:cpchKey,
			STRFMT_STRING("Source",pchSourceFull?pchSourceFull:cpchSource),
			STRFMT_STRING("Target",pchTargetFull?pchTargetFull:cpchTarget),
			STRFMT_STRING("Attrib",pchAttrib),
			STRFMT_INT("Magnitude",fMagnitude),
			STRFMT_STRING("Power",cpchPower),
			STRFMT_STRING("SecondaryPower",cpchSecondaryPower),
			STRFMT_INT("Critical",(eFlags & kCombatTrackerFlag_Critical)!=0),
			STRFMT_END);
	}

	{//Strip out any SMF formatting since chat doesn't support it
		char *estrTemp = NULL;
		estrStackCreate(&estrTemp);
		StringStripTagsSafe(*ppchDesc, &estrTemp);
		if (estrTemp && estrTemp[0])
		{
			estrCopy(ppchDesc, &estrTemp);
		}
		estrDestroy(&estrTemp);
	}

	estrDestroy(&pchSourceFull);
	estrDestroy(&pchTargetFull);
	estrDestroy(&pchAttrib);
	estrDestroy(&pchKeyCompiled);
#endif
}


static void TimestampCombatLog(char **ppchLine)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	// Yeah, this is the slow crappy way to do it
	estrPrintf(ppchLine,"%02d:%02d:%02d:%02d:%02d:%02d.%1d::",t.wYear%100,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds/100);
}


void combattracker_CombatLog(CombatTrackerNet *pnet, EntityRef erTarget)
{
	char *pchLine = NULL;
	char *pchFlags = NULL;
	Message *pmsg = GET_REF(pnet->hDisplayName);
	const char *cpchEvent = TranslateMessagePtr(pmsg);
	Entity *eOwner = entFromEntityRefAnyPartition(pnet->erOwner);
	Entity *eSource = entFromEntityRefAnyPartition(pnet->erSource);
	Entity *eTarget = entFromEntityRefAnyPartition(erTarget);

	S32 bTargetIsSource = eSource && eSource==eTarget;
	S32 bSourceIsOwner = eOwner && eOwner==eSource;
	S32 bUntyped = (pnet->eFlags&kCombatTrackerFlag_Miss) || (g_CombatConfig.pHitChance && (pnet->eFlags & g_CombatConfig.pHitChance->eFlag));
	char ownerName[256];
	char sourceName[256];
	char targetName[256];

	estrStackCreate(&pchLine);

	TimestampCombatLog(&pchLine);

	if(pnet->eFlags)
	{
		int i;
		for(i=1; i<=kCombatTrackerFlag_LAST; i<<=1)
		{
			if(pnet->eFlags&i)
			{
				if(!pchFlags)
				{
					estrStackCreate(&pchFlags);
				}
				else
				{
					estrConcatChar(&pchFlags,'|');
				}
				estrAppend2(&pchFlags,StaticDefineIntRevLookup(CombatTrackerFlagEnum,i));
			}
		}
	}

	if (gConf.bVerboseCombatLogging)
	{
		// Add level to the display name
		if (eOwner)
			sprintf(ownerName, "%s-%d", entGetLocalName(eOwner), entity_GetCombatLevel(eOwner));
		else
			strcpy(ownerName, "");
		
		if (eSource && !bSourceIsOwner)
			sprintf(sourceName, "%s-%d", entGetLocalName(eSource), entity_GetCombatLevel(eSource));
		else
			strcpy(sourceName, "");

		if (eTarget && !bTargetIsSource)
			sprintf(targetName, "%s-%d", entGetLocalName(eTarget), entity_GetCombatLevel(eTarget));
		else
			strcpy(targetName, "");
	}
	else
	{
		sprintf(ownerName, "%s", eOwner ? entGetLocalName(eOwner) : "");
		sprintf(sourceName, "%s", eSource && !bSourceIsOwner ? entGetLocalName(eSource) : "");
		sprintf(targetName, "%s", eTarget && !bTargetIsSource ? entGetLocalName(eTarget) : "");
	}

	FormatMessageKey(&pchLine,"CombatLog.Damage",
		STRFMT_STRING("Owner", ownerName),
		STRFMT_STRING("OwnerDebug", eOwner ? ENTDEBUGNAME(eOwner) : NULL),
		STRFMT_STRING("Source", sourceName),
		STRFMT_STRING("SourceDebug", eSource ? (bSourceIsOwner ? "*" : ENTDEBUGNAME(eSource)) : NULL),
		STRFMT_STRING("Target", targetName),
		STRFMT_STRING("TargetDebug", eTarget ? (bTargetIsSource ? "*" : ENTDEBUGNAME(eTarget)) : NULL),
		STRFMT_STRING("Event",cpchEvent),
		STRFMT_STRING("EventDebug",REF_STRING_FROM_HANDLE(pnet->hDisplayName)),
		STRFMT_STRING("Type", !(pnet->eFlags&kCombatTrackerFlag_Miss) ? StaticDefineIntRevLookup(AttribTypeEnum,pnet->eType) : NULL),
		STRFMT_STRING("Flags",pchFlags),
		STRFMT_FLOAT("Magnitude",pnet->fMagnitude),
		STRFMT_FLOAT("MagnitudeBase",pnet->fMagnitudeBase),
		STRFMT_END);

	estrAppend2(&pchLine,"\n");

	logDirectWrite("combatlog.log",pchLine);

	estrDestroy(&pchFlags);
	estrDestroy(&pchLine);
}




static void ModAutoDescCustom(Entity *pEnt, char **ppchDescription, PowerDef *pPowerDef, AutoDescAttribMod *pAutoDescAttribMod, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey)
{
	if (pEnt && ppchDescription && pAutoDescAttribMod && pchPowerMessageKey)
	{
		int i;

		char *pchInlinePowers = NULL;
		char *pchPowerExpire = NULL;

		// In the custom case, we just use that as the description, with NULL for everything else, and bail
		if(pAutoDescAttribMod->pchCustom)
		{
#ifdef GAMECLIENT
			FormatGameMessageKey(ppchDescription,pchAttribModsMessageKey,
				STRFMT_STRING("Dev", (SAFE_MEMBER2(pEnt, pPlayer, accessLevel) >= ACCESS_GM) ? pAutoDescAttribMod->pchDev : ""),
				STRFMT_STRING("Target", NULL),
				STRFMT_STRING("Requires", NULL),
				STRFMT_STRING("Affects", NULL),
				STRFMT_STRING("Chance", NULL),
				STRFMT_STRING("Delay", NULL),
				STRFMT_STRING("Description", pAutoDescAttribMod->pchCustom),
				STRFMT_STRING("MagnitudePerSecond", NULL),
				STRFMT_STRING("Period", NULL),
				STRFMT_STRING("Duration", NULL),
				STRFMT_STRING("Expire", NULL),
				STRFMT_STRING("InlinePowers", NULL),
				STRFMT_STRING("PowerExpire", NULL),
				STRFMT_END);
#else
			entFormatGameMessageKey(pEnt,ppchDescription,pchAttribModsMessageKey,
				STRFMT_STRING("Dev", (SAFE_MEMBER2(pEnt, pPlayer, accessLevel) >= ACCESS_GM) ? pAutoDescAttribMod->pchDev : ""),
				STRFMT_STRING("Target", NULL),
				STRFMT_STRING("Requires", NULL),
				STRFMT_STRING("Affects", NULL),
				STRFMT_STRING("Chance", NULL),
				STRFMT_STRING("Delay", NULL),
				STRFMT_STRING("Description", pAutoDescAttribMod->pchCustom),
				STRFMT_STRING("MagnitudePerSecond", NULL),
				STRFMT_STRING("Period", NULL),
				STRFMT_STRING("Duration", NULL),
				STRFMT_STRING("Expire", NULL),
				STRFMT_STRING("InlinePowers", NULL),
				STRFMT_STRING("PowerExpire", NULL),
				STRFMT_END);
#endif

			return;
		}

		for(i = 0; i < eaSize(&pAutoDescAttribMod->ppPowersInline); i++)
		{
			AutoDescPower *pAutoDescPower = pAutoDescAttribMod->ppPowersInline[i];
			powerdef_AutoDescCustom(pEnt, &pchInlinePowers, pPowerDef, pAutoDescPower, pchPowerMessageKey, pchAttribModsMessageKey);
		}

		if(pAutoDescAttribMod->pPowerExpire)
		{
			powerdef_AutoDescCustom(pEnt, &pchPowerExpire, pPowerDef, pAutoDescAttribMod->pPowerExpire, pchPowerMessageKey, pchAttribModsMessageKey);
		}

#ifdef GAMECLIENT
		FormatGameMessageKey(ppchDescription,pchAttribModsMessageKey,
			STRFMT_STRING("Dev", (SAFE_MEMBER2(pEnt, pPlayer, accessLevel) >= ACCESS_GM) ? pAutoDescAttribMod->pchDev : ""),
			STRFMT_STRING("Target", pAutoDescAttribMod->pchTarget),
			STRFMT_STRING("Requires", pAutoDescAttribMod->pchRequires),
			STRFMT_STRING("Affects", pAutoDescAttribMod->pchAffects),
			STRFMT_STRING("Chance", pAutoDescAttribMod->pchChance),
			STRFMT_STRING("Delay", pAutoDescAttribMod->pchDelay),
			STRFMT_STRING("Description", pAutoDescAttribMod->pchEffect),
			STRFMT_STRING("MagnitudePerSecond", pAutoDescAttribMod->pchMagnitudePerSecond),
			STRFMT_STRING("Period", pAutoDescAttribMod->pchPeriod),
			STRFMT_STRING("Duration", pAutoDescAttribMod->pchDuration),
			STRFMT_STRING("Expire", pAutoDescAttribMod->pchExpire),
			STRFMT_STRING("InlinePowers", pchInlinePowers),
			STRFMT_STRING("PowerExpire", pchPowerExpire),
			STRFMT_END);
#else
		entFormatGameMessageKey(pEnt,ppchDescription,pchAttribModsMessageKey,
			STRFMT_STRING("Dev", (SAFE_MEMBER2(pEnt, pPlayer, accessLevel) >= ACCESS_GM) ? pAutoDescAttribMod->pchDev : ""),
			STRFMT_STRING("Target", pAutoDescAttribMod->pchTarget),
			STRFMT_STRING("Requires", pAutoDescAttribMod->pchRequires),
			STRFMT_STRING("Affects", pAutoDescAttribMod->pchAffects),
			STRFMT_STRING("Chance", pAutoDescAttribMod->pchChance),
			STRFMT_STRING("Delay", pAutoDescAttribMod->pchDelay),
			STRFMT_STRING("Description", pAutoDescAttribMod->pchEffect),
			STRFMT_STRING("MagnitudePerSecond", pAutoDescAttribMod->pchMagnitudePerSecond),
			STRFMT_STRING("Period", pAutoDescAttribMod->pchPeriod),
			STRFMT_STRING("Duration", pAutoDescAttribMod->pchDuration),
			STRFMT_STRING("Expire", pAutoDescAttribMod->pchExpire),
			STRFMT_STRING("InlinePowers", pchInlinePowers),
			STRFMT_STRING("PowerExpire", pchPowerExpire),
			STRFMT_END);
#endif

		estrDestroy(&pchInlinePowers);
		estrDestroy(&pchPowerExpire);
	}
}

void powerdef_AutoDescCustom(Entity *pEnt, char **ppchDescription, PowerDef *pPowerDef, AutoDescPower *pAutoDescPower, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey)
{
	if (pEnt && pPowerDef && ppchDescription && pAutoDescPower && pchPowerMessageKey)
	{
		int i;
		char *pchCombo = NULL;
		char *pchAttribMods = NULL;
		char *pchFootnotes = NULL;
		char* pchAttribs = NULL;
		bool bHasDescription = false;
		bool bGemPower = pAutoDescPower->eRequiredGemType > 0;

		if(!pAutoDescPower->pchCustom)
		{
			for (i = 0; i < eaSize(&pAutoDescPower->ppPowersCombo); i++)
			{
				AutoDescPower *pComboPower = pAutoDescPower->ppPowersCombo[i];
				powerdef_AutoDescCustom(pEnt, &pchCombo, pPowerDef, pComboPower, pchPowerMessageKey, pchAttribModsMessageKey);
			}

			for(i = 0; i<eaSize(&pAutoDescPower->ppAttribMods); i++)
			{
				AutoDescAttribMod *pAutoDescAttribMod = pAutoDescPower->ppAttribMods[i];
				ModAutoDescCustom(pEnt, &pchAttribMods, pPowerDef, pAutoDescAttribMod, pchPowerMessageKey, pchAttribModsMessageKey);
			}

			for(i = 0; i<eaSize(&pAutoDescPower->ppAttribModsEnhancements); i++)
			{
				AutoDescAttribMod *pAutoDescAttribMod = pAutoDescPower->ppAttribModsEnhancements[i];
				ModAutoDescCustom(pEnt, &pchAttribMods, pPowerDef, pAutoDescAttribMod, pchPowerMessageKey, pchAttribModsMessageKey);
			}

			for(i = 0; i < eaSize(&pAutoDescPower->ppchFootnotes); i++)
			{
				estrAppend2(&pchFootnotes, pAutoDescPower->ppchFootnotes[i]);
				if (i != eaSize(&pAutoDescPower->ppchFootnotes) - 1)
				{
					estrAppend2(&pchFootnotes, "<br>");
				}
			}
		}
		else
		{
			pchAttribMods = estrStackCreateFromStr(pAutoDescPower->pchCustom);
		}

		bHasDescription = (	stricmp(NULL_TO_EMPTY(pAutoDescPower->pchDesc), "") ||
							stricmp(NULL_TO_EMPTY(pAutoDescPower->pchDescLong), "") ||
							stricmp(NULL_TO_EMPTY(pAutoDescPower->pchDescFlavor), ""));

#ifdef GAMECLIENT
		FormatGameMessageKey(ppchDescription, pchPowerMessageKey,
			STRFMT_INT("HasName", stricmp(NULL_TO_EMPTY(pAutoDescPower->pchName), "")),
			STRFMT_STRING("Name", pAutoDescPower->pchName),
			STRFMT_INT("HasDesc", bHasDescription),
			STRFMT_STRING("DescShort", pAutoDescPower->pchDesc),
			STRFMT_STRING("DescLong", pAutoDescPower->pchDescLong),
			STRFMT_STRING("Flavor", pAutoDescPower->pchDescFlavor),
			STRFMT_STRING("Dev", (SAFE_MEMBER2(pEnt, pPlayer, accessLevel) >= ACCESS_GM) ? pAutoDescPower->pchDev : ""),
			STRFMT_STRING("ComboRequires", pAutoDescPower->pchComboRequires),
			STRFMT_STRING("ComboCharge", pAutoDescPower->pchComboCharge),
			STRFMT_STRING("Type", pAutoDescPower->pchType),
			STRFMT_STRING("Cost", pAutoDescPower->pchCost),
			STRFMT_STRING("CostPeriodic", pAutoDescPower->pchCostPeriodic),
			STRFMT_STRING("EnhanceAttach", pAutoDescPower->pchEnhanceAttach),
			STRFMT_STRING("EnhanceAttached", pAutoDescPower->pchEnhanceAttached),
			STRFMT_STRING("EnhanceApply", pAutoDescPower->pchEnhanceApply),
			STRFMT_STRING("Target", pAutoDescPower->pchTarget),
			STRFMT_STRING("TargetArc", pAutoDescPower->pchTargetArc),
			STRFMT_STRING("Range", pAutoDescPower->pchRange),
			STRFMT_STRING("TimeCharge", pAutoDescPower->pchTimeCharge),
			STRFMT_STRING("TimeActivate", pAutoDescPower->pchTimeActivate),
			STRFMT_STRING("TimeActivatePeriod", pAutoDescPower->pchTimeActivatePeriod),
			STRFMT_STRING("TimeMaintain", pAutoDescPower->pchTimeMaintain),
			STRFMT_STRING("TimeRecharge", pAutoDescPower->pchTimeRecharge),
			STRFMT_STRING("Charges", pAutoDescPower->pchCharges),
			STRFMT_STRING("LifetimeUsage", pAutoDescPower->pchLifetimeUsage),
			STRFMT_STRING("LifetimeGame", pAutoDescPower->pchLifetimeGame),
			STRFMT_STRING("LifetimeReal", pAutoDescPower->pchLifetimeReal),
			STRFMT_STRING("Combo", pchCombo),
			STRFMT_STRING("AttribMods", pchAttribMods),
			STRFMT_STRING("Footnotes", pchFootnotes),
			STRFMT_STRING("Attribs", pAutoDescPower->pchAttribs),
			STRFMT_INT("IsEquippable", !bGemPower && (pPowerDef->eType == kPowerType_Innate || pPowerDef->eType == kPowerType_Passive || pPowerDef->eType == kPowerType_Enhancement)),
			STRFMT_INT("IsUseable", !bGemPower && (pPowerDef->eType == kPowerType_Click || pPowerDef->eType == kPowerType_Toggle || pPowerDef->eType == kPowerType_Combo || pPowerDef->eType == kPowerType_Instant || pPowerDef->eType == kPowerType_Maintained)),
			STRFMT_INT("IsActive", pAutoDescPower->bActive),
			STRFMT_INT("IsGemPower", bGemPower),
			STRFMT_STRING("GemSlotRequired", NULL_TO_EMPTY(StaticDefineInt_FastIntToString(ItemGemTypeEnum, pAutoDescPower->eRequiredGemType))),
			STRFMT_STRING("SlotTypeName", NULL_TO_EMPTY(StaticDefineGetTranslatedMessage(ItemGemTypeEnum, pAutoDescPower->eRequiredGemType))),

			STRFMT_POWERTAGS(pAutoDescPower->eaPowerTags),
			STRFMT_END);
#else
		entFormatGameMessageKey(pEnt, ppchDescription, pchPowerMessageKey,
			STRFMT_INT("HasName", stricmp(NULL_TO_EMPTY(pAutoDescPower->pchName), "")),
			STRFMT_STRING("Name", pAutoDescPower->pchName),
			STRFMT_INT("HasDesc", bHasDescription),
			STRFMT_STRING("DescShort", pAutoDescPower->pchDesc),
			STRFMT_STRING("DescLong", pAutoDescPower->pchDescLong),
			STRFMT_STRING("Flavor", pAutoDescPower->pchDescFlavor),
			STRFMT_STRING("Dev", (SAFE_MEMBER2(pEnt, pPlayer, accessLevel) >= ACCESS_GM) ? pAutoDescPower->pchDev : ""),
			STRFMT_STRING("ComboRequires", pAutoDescPower->pchComboRequires),
			STRFMT_STRING("ComboCharge", pAutoDescPower->pchComboCharge),
			STRFMT_STRING("Type", pAutoDescPower->pchType),
			STRFMT_STRING("Cost", pAutoDescPower->pchCost),
			STRFMT_STRING("CostPeriodic", pAutoDescPower->pchCostPeriodic),
			STRFMT_STRING("EnhanceAttach", pAutoDescPower->pchEnhanceAttach),
			STRFMT_STRING("EnhanceAttached", pAutoDescPower->pchEnhanceAttached),
			STRFMT_STRING("EnhanceApply", pAutoDescPower->pchEnhanceApply),
			STRFMT_STRING("Target", pAutoDescPower->pchTarget),
			STRFMT_STRING("TargetArc", pAutoDescPower->pchTargetArc),
			STRFMT_STRING("Range", pAutoDescPower->pchRange),
			STRFMT_STRING("TimeCharge", pAutoDescPower->pchTimeCharge),
			STRFMT_STRING("TimeActivate", pAutoDescPower->pchTimeActivate),
			STRFMT_STRING("TimeActivatePeriod", pAutoDescPower->pchTimeActivatePeriod),
			STRFMT_STRING("TimeMaintain", pAutoDescPower->pchTimeMaintain),
			STRFMT_STRING("TimeRecharge", pAutoDescPower->pchTimeRecharge),
			STRFMT_STRING("Charges", pAutoDescPower->pchCharges),
			STRFMT_STRING("LifetimeUsage", pAutoDescPower->pchLifetimeUsage),
			STRFMT_STRING("LifetimeGame", pAutoDescPower->pchLifetimeGame),
			STRFMT_STRING("LifetimeReal", pAutoDescPower->pchLifetimeReal),
			STRFMT_STRING("Combo", pchCombo),
			STRFMT_STRING("AttribMods", pchAttribMods),
			STRFMT_STRING("Footnotes", pchFootnotes),
			STRFMT_STRING("Attribs", pAutoDescPower->pchAttribs),
			STRFMT_INT("IsEquippable", !bGemPower && (pPowerDef->eType == kPowerType_Innate || pPowerDef->eType == kPowerType_Passive || pPowerDef->eType == kPowerType_Enhancement)),
			STRFMT_INT("IsUseable", !bGemPower && (pPowerDef->eType == kPowerType_Click || pPowerDef->eType == kPowerType_Toggle || pPowerDef->eType == kPowerType_Combo || pPowerDef->eType == kPowerType_Instant || pPowerDef->eType == kPowerType_Maintained)),
			STRFMT_INT("IsActive", pAutoDescPower->bActive),
			STRFMT_INT("IsGemPower", bGemPower),
			STRFMT_STRING("GemSlotRequired", NULL_TO_EMPTY(StaticDefineInt_FastIntToString(ItemGemTypeEnum, pAutoDescPower->eRequiredGemType))),
			STRFMT_STRING("SlotTypeName", NULL_TO_EMPTY(StaticDefineGetTranslatedMessage(ItemGemTypeEnum, pAutoDescPower->eRequiredGemType))),
			STRFMT_POWERTAGS(pAutoDescPower->eaPowerTags),
			STRFMT_END);
#endif

		estrDestroy(&pchCombo);
		estrDestroy(&pchAttribMods);
		estrDestroy(&pchFootnotes);
		estrDestroy(&pchAttribs);
	}
}


AUTO_FIXUPFUNC;
TextParserResult AutoDescPowerFixupFunc(AutoDescPower *pAutoDescPower, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		case FIXUPTYPE_DESTRUCTOR:
			eaDestroy(&pAutoDescPower->ppAttribModsIndexed);
			break;
	}

	return true;
}

AUTO_FIXUPFUNC;
TextParserResult AutoDescAttribModFixupFunc(AutoDescAttribMod *pAutoDescAttribMod, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pAutoDescAttribMod->ppPowersSubIndexed);
		break;
	}

	return true;
}

AUTO_STARTUP(PowersAutoDesc) ASTRT_DEPS(Powers);
void PowersAutoDescLoad(void)
{
	loadstart_printf("Loading %s...","AutoDescConfig");

	//Fill-in the default values
	StructInit(parse_AutoDescConfig, &s_AutoDescConfig);

	ParserLoadFiles(NULL, "defs/config/AutoDescConfig.def","AutoDescConfig.bin",PARSER_OPTIONALFLAG,parse_AutoDescConfig,&s_AutoDescConfig);

	loadend_printf(" done.");
}

#include "PowersAutoDesc_h_ast.c"