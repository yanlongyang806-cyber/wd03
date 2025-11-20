/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "textparser.h"
#include "EString.h"
#include "FolderCache.h"
#include "StringCache.h"

#include "EntityLib.h"
#include "gclCombatAdvantage.h"
#include "GraphicsLib.h"
#include "GameClientLib.h"
#include "GlobalTypes.h"
#include "Expression.h"

#include "UICore.h"
#include "UIStyle.h"
#include "CBox.h"

#include "gclEntity.h"
#include "gclHUDOptions.h"
#include "CharacterAttribs.h"
#include "CharacterStatus.h"
#include "DamageTracker.h"
#include "GfxSpriteText.h"
#include "Player.h"
#include "Team.h"
#include "GfxPrimitive.h"
#include "GfxTexAtlas.h"

#include "DamageFloaters.h"

#include "Entity_h_ast.h"
#include "UICore_h_ast.h"
#include "PowersEnums_h_ast.h"
#include "DamageTracker_h_ast.h"
#include "GfxSprite.h"

#include "Character_target.h"

#include "PowerAnimFX.h"

#include "rand.h"

#include "DamageFloaters_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

DamageFloatTemplate g_DamageFloatTemplate;
DamageFloatGroupTemplates g_DamageFloatGroupTemplates;
typedef struct AtlasTex AtlasTex;

static bool s_bHideDamage = false;
static bool s_bDisableDamage = false;

#define MAX_DAMAGE_FLOATERS 15
#define DAMAGE_FLOAT_TRACKER_FLAGS (kCombatTrackerFlag_ReactiveBlock|kCombatTrackerFlag_ShowPowerDisplayName)


// Whether to show/hide damage counters.
// This is used by demo recording and testing scripts.
AUTO_CMD_INT(s_bDisableDamage, DisableDamage) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Interface) ACMD_CMDLINEORPUBLIC;

// Whether to show/hide damage counters.
AUTO_CMD_INT(s_bHideDamage, HideDamage) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;

__forceinline static bool ShowDamageFloaters(Entity *pEnt)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	if(s_bDisableDamage || s_bHideDamage || !pHUDOptions)
		return false;

	return pHUDOptions->ShowOverhead.bShowDamageFloaters;
}

__forceinline static bool ShowPetDamageFloaters(Entity *pEnt)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	if(s_bDisableDamage || s_bHideDamage || !pHUDOptions)
		return false;

	return pHUDOptions->ShowOverhead.bShowPetDamageFloaters;
}

__forceinline static bool ShowUnrelatedDamageFloaters(Entity *pEnt)
{
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	if(s_bDisableDamage || s_bHideDamage || !pHUDOptions)
		return false;

	return pHUDOptions->ShowOverhead.bShowUnrelatedDamageFloaters;
}

__forceinline static bool ShowAnyDamageFloaters(void)
{
	Entity *pEnt = entActivePlayerPtr();

	return ShowDamageFloaters(pEnt) || ShowUnrelatedDamageFloaters(pEnt);
}

/***************************************************************************
 * gclDamageFloatTemplateReload
 *
 */
static void gclDamageFloatTemplateReload(const char *pchpath, S32 iWhen)
{
	StructDeInit(parse_DamageFloatTemplate, &g_DamageFloatTemplate);
	ParserLoadFiles(NULL, "ui/DamageFloats.def", "DamageFloats.bin", 0, parse_DamageFloatTemplate, &g_DamageFloatTemplate);
}

/***************************************************************************
 * gclDamageFloatGroupsReload
 *
 */
static void gclDamageFloatGroupsReload(const char *pchpath, S32 iWhen)
{
	StructDeInit(parse_DamageFloatGroupTemplates, &g_DamageFloatGroupTemplates);
	ParserLoadFiles(NULL, "ui/DamageFloatGroups.def", "DamageFloatsGroups.bin", 0, parse_DamageFloatGroupTemplates, &g_DamageFloatGroupTemplates);
}

/***************************************************************************
 * gclLoadDamageFloats
 *
 */
AUTO_STARTUP(GameUI) ASTRT_DEPS(UILib UIGen CharacterClasses);
void gclLoadDamageFloats(void)
{
	if(gbNoGraphics)
	{
		return;
	}

	gclDamageFloatTemplateReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/DamageFloats.def", gclDamageFloatTemplateReload);

	gclDamageFloatGroupsReload(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/DamageFloatGroups.def", gclDamageFloatGroupsReload);
}

/***************************************************************************
 * DamageFloatGetContext
 *
 */
static ExprContext *DamageFloatGetContext(void)
{
	static ExprContext *s_pDamageFloatContext;
	static ExprFuncTable* s_stFuncTable;
	if (!s_pDamageFloatContext)
	{
		s_stFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_stFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_stFuncTable, "entityutil");
		s_pDamageFloatContext = exprContextCreate();
		exprContextSetFuncTable(s_pDamageFloatContext, s_stFuncTable);
		exprContextAddStaticDefineIntAsVars(s_pDamageFloatContext, ColorEnum, "Color");
		exprContextAddStaticDefineIntAsVars(s_pDamageFloatContext, AttribTypeEnum, "AttribType");
		exprContextAddStaticDefineIntAsVars(s_pDamageFloatContext, CombatTrackerFlagEnum, "CombatTrackerFlag");
		exprContextAddStaticDefineIntAsVars(s_pDamageFloatContext, UIDirectionEnum, "");
		exprContextSetAllowRuntimeSelfPtr(s_pDamageFloatContext);
		exprContextSetAllowRuntimePartition(s_pDamageFloatContext);
	}
	return s_pDamageFloatContext;
}

/***************************************************************************
 * DamageFloatTemplateParserFixup
 *
 */
AUTO_FIXUPFUNC;
TextParserResult DamageFloatTemplateParserFixup(DamageFloatTemplate *pTemplate, enumTextParserFixupType eType, void *pExtraData)
{
	bool bValid = true;
	ExprContext *pContext = DamageFloatGetContext();
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
	case FIXUPTYPE_POST_RELOAD:

		exprContextSetPointerVar(pContext, "Player", NULL, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Source", NULL, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Target", NULL, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Owner", NULL, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Damage", NULL, parse_CombatTrackerNet, true, true);
		exprContextSetStringVar(pContext, "FlagText", "");
		exprContextSetStringVar(pContext, "DefaultText", "");
		exprContextSetIntVar(pContext, "Value", 0);
		exprContextSetIntVar(pContext, "MitigatedValue", 0);
		exprContextSetIntVar(pContext, "Group", 0);
		exprContextSetIntVar(pContext, "AutoAttack", 0);

		bValid = exprGenerate(pTemplate->pColor, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pFont, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pLifetime, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pScale, pContext) && bValid;
		bValid = pTemplate->pVelocityX ? exprGenerate(pTemplate->pVelocityX, pContext) && bValid : bValid;
		bValid = exprGenerate(pTemplate->pVelocityY, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pText, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pX, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pY, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pOffsetFrom, pContext) && bValid;
		bValid = exprGenerate(pTemplate->pGroup, pContext) && bValid;
		bValid = pTemplate->pBottomColor ? (exprGenerate(pTemplate->pBottomColor, pContext) && bValid) : bValid;
		bValid = pTemplate->pPriority ? (exprGenerate(pTemplate->pPriority, pContext) && bValid) : bValid;
		bValid = pTemplate->pPopout ? (exprGenerate(pTemplate->pPopout, pContext) && bValid) : bValid;
		bValid = pTemplate->pIconName ? (exprGenerate(pTemplate->pIconName, pContext) && bValid) : bValid;
		bValid = pTemplate->pIconColor ? (exprGenerate(pTemplate->pIconColor, pContext) && bValid) : bValid;
		bValid = pTemplate->pIconOffsetFrom ? (exprGenerate(pTemplate->pIconOffsetFrom, pContext) && bValid) : bValid;
		bValid = pTemplate->pIconScale ? (exprGenerate(pTemplate->pIconScale, pContext) && bValid) : bValid;
		bValid = pTemplate->pIconX ? (exprGenerate(pTemplate->pIconX, pContext) && bValid) : bValid;
		bValid = pTemplate->pIconY ? (exprGenerate(pTemplate->pIconY, pContext) && bValid) : bValid;
		bValid = pTemplate->pMinArc ? (exprGenerate(pTemplate->pMinArc, pContext) && bValid) : bValid;
		bValid = pTemplate->pMaxArc ? (exprGenerate(pTemplate->pMaxArc, pContext) && bValid) : bValid;
		bValid = pTemplate->pArcRadius ? (exprGenerate(pTemplate->pArcRadius, pContext) && bValid) : bValid;
		break;
	}
	return bValid ? PARSERESULT_SUCCESS : PARSERESULT_INVALID;
}

static bool gclDamageFloat_EntIsPet(Entity* pEnt)
{
	return entGetType(pEnt) == GLOBALTYPE_ENTITYSAVEDPET || entCheckFlag(pEnt, ENTITYFLAG_CRITTERPET);
}

static int gclDamageFloat_ShouldShow(CombatTrackerNet *pNet, Entity *pTarget, Entity *pPlayer, Entity *pOwner, Entity *pSource)
{
	if (gConf.bDamageTrackerServerOnlySendFlaggedSpecial && !(pNet->eFlags & kCombatTrackerFlag_ShowSpecial))
	{
		return false;
	}

	if (pNet->eFlags & kCombatTrackerFlag_NoFloater)
		return false;

	if(pTarget == pPlayer || pSource == pPlayer || (!pSource && pOwner == pPlayer))
	{
		PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pPlayer);
		if(s_bDisableDamage || s_bHideDamage || !pHUDOptions)
			return false;

		if (pHUDOptions->ShowOverhead.bShowDamageFloaters)
		{
			if (pNet->eFlags & kCombatTrackerFlag_ShowPowerDisplayName)
				return pHUDOptions->ShowOverhead.bShowPlayerPowerDisplayNames;
			
			if (!pHUDOptions->ShowOverhead.bDontShowPlayerIncoming && !pHUDOptions->ShowOverhead.bDontShowPlayerOutgoing)
				return true;
			
			return	(!pHUDOptions->ShowOverhead.bDontShowPlayerIncoming && pTarget == pPlayer) ||
					(!pHUDOptions->ShowOverhead.bDontShowPlayerOutgoing && pSource == pPlayer) ||
					(!pHUDOptions->ShowOverhead.bDontShowPlayerOutgoing && (!pSource && pOwner == pPlayer));
		}

	}
	else
	{
		PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pPlayer);
		Entity* pTargetOwner = entFromEntityRefAnyPartition(pTarget->erOwner);

		if (s_bDisableDamage || s_bHideDamage || !pHUDOptions)
		{
			return false;
		}
		if (pHUDOptions->ShowOverhead.bShowAllPlayerDamageFloaters && 
			((pSource && entGetType(pSource) == GLOBALTYPE_ENTITYPLAYER) || (pTarget && entGetType(pTarget) == GLOBALTYPE_ENTITYPLAYER)))
		{
			return true;
		}
		//teammates
		if (pHUDOptions->ShowOverhead.bShowTeamDamageFloaters && 
			(team_OnSameTeam(pSource, pPlayer) || team_OnSameTeam(pTarget, pPlayer)))
		{
			return true;
		}
		//teammates' pets
		if ((pHUDOptions->ShowOverhead.bShowTeamDamageFloaters && pHUDOptions->ShowOverhead.bShowPetDamageFloaters) && 
			(team_OnSameTeam(pOwner, pPlayer) || team_OnSameTeam(pTargetOwner, pPlayer)))
		{
			return true;
		}
		//our own pets
		if (pHUDOptions->ShowOverhead.bShowPetDamageFloaters && 
			((pSource && gclDamageFloat_EntIsPet(pSource) && pOwner == pPlayer) || 
			(pTarget && gclDamageFloat_EntIsPet(pTarget) && pTargetOwner == pPlayer)))
		{
			return true;
		}
		//owned entities that aren't pets
		if (pHUDOptions->ShowOverhead.bShowOwnedEntityDamageFloaters && 
			(!pSource || !gclDamageFloat_EntIsPet(pSource)) && pOwner == pPlayer)
		{
			return true;
		}
		//enemy self-healing
		if (pHUDOptions->ShowOverhead.bShowHostileHealingFloaters && 
			(character_TargetIsFoe(PARTITION_CLIENT,pPlayer->pChar, pTarget->pChar) && pNet->fMagnitude < 0))
		{
			return true;
		}
		//everything else
		if (pHUDOptions->ShowOverhead.bShowUnrelatedDamageFloaters)
		{
			return true;
		}
		return false;
	}

	return true;
}

static void gclDamageFloat_GetAnchorPos(DamageFloat *pFloater, CBox *pBox, Vec2 v2OutPos)
{
	CBoxGetCenter(pBox, v2OutPos, v2OutPos+1);
	if(pFloater->eOffsetFrom & UITop)
		v2OutPos[1] = pBox->top;
	else if(pFloater->eOffsetFrom & UIBottom)
		v2OutPos[1] = pBox->bottom;

	if(pFloater->eOffsetFrom & UIRight)
		v2OutPos[0] = pBox->right;
	else if(pFloater->eOffsetFrom & UILeft)
		v2OutPos[0] = pBox->left;

}

static bool gclDamageFloat_GetScreenBoundingBox(Entity *pEnt, CBox *pBoxOut, F32 *pfDistance)
{
	if (gConf.bOverheadEntityGens)
	{	
		if (entGetPrimaryCapsuleScreenBoundingBox(pEnt, pBoxOut, pfDistance))
			return true;
	}
		
	return entGetScreenBoundingBox(pEnt, pBoxOut, pfDistance, true);
}

void DEFAULT_LATELINK_gclDamageFloat_Gamespecific_Preprocess(CombatTrackerNet *pNet, Entity *pTarget, F32 fDelay)
{
}

static F32 gclDamageFloat_GetPositionScale(CBox *pEntBox)
{
	F32 fHeight;
	S32 iScreenHeight;
	F32 fTargetPositionScale = 1.f;

	if (!g_DamageFloatTemplate.pTargetBoxScalar)
		return 1.f;

	fHeight = CBoxHeight(pEntBox);

	gfxGetActiveDeviceSize(NULL, &iScreenHeight);

	if (iScreenHeight)
	{
		F32 fHeightPercentOfScreen = fHeight / iScreenHeight;
		if (fHeightPercentOfScreen < g_DamageFloatTemplate.pTargetBoxScalar->fHeightPercentThreshold)
		{
			fTargetPositionScale = fHeightPercentOfScreen / g_DamageFloatTemplate.pTargetBoxScalar->fHeightPercentThreshold;
			fTargetPositionScale = CLAMP(fTargetPositionScale, g_DamageFloatTemplate.pTargetBoxScalar->fScaleMin, 1.f);
		}
	}

	return fTargetPositionScale;
}


static bool gclDamageFloatCreate_ShouldMerge(DamageFloat *pNewFloat, DamageFloat *pOtherFloat)
{
	if (pNewFloat->combatTrackerFlags == kCombatTrackerFlag_ShowPowerDisplayName && 
		pOtherFloat->combatTrackerFlags == kCombatTrackerFlag_ShowPowerDisplayName && 
		!stricmp(pOtherFloat->pchMessage, pNewFloat->pchMessage))
	{
		return true;
	}

	return (pNewFloat->uiPowID != 0) && 
		(pNewFloat->uiPowID == pOtherFloat->uiPowID) && 
		(pNewFloat->bCrit == pOtherFloat->bCrit);
}


static DamageFloat* gclDamageFloat_FindOverlapping(DamageFloatGroup *pFloatGroup, 
														const Vec2 v2Size, 
														const Vec2 v2DstPos, 
														bool bFirstFound)
{
	DamageFloat* pHighest = NULL;
	F32 fHorizAdjustment = 40; // a small amount so we don't get numbers stringing together

	FOR_EACH_IN_EARRAY(pFloatGroup->eaDamageFloats, DamageFloat, pFloat)
	{
		Vec2 vDiff;
		subVec2(pFloat->v2DestPos, v2DstPos, vDiff);

		if (ABS(vDiff[0]) < ((pFloat->v2Size[0] + v2Size[0]) * 0.5f + fHorizAdjustment) && 
			ABS(vDiff[1]) < ((pFloat->v2Size[1] + v2Size[1]) * 0.5f))
		{
			if (bFirstFound)
				return pFloat;

			if (!pHighest)
				pHighest = pFloat;
			else if ((pFloat->v2DestPos[1] + pFloat->v2Size[1]) > 
					 (pHighest->v2DestPos[1] + pHighest->v2Size[1]))
			{
				pHighest = pFloat;
			}
			
		}
	}
	FOR_EACH_END

	return pHighest;
}

static void gclDamageFloatCreate_ResolveLayoutArcOverlapMoveUp(	DamageFloatGroup *pFloatGroup, 
																const Vec2 v2Size, 
																Vec2 v2DstPosInOut, 
																S32 iRecurseCheck)
{
	DamageFloat *pIntersectingFloater = NULL;
	
	iRecurseCheck++;
	if (iRecurseCheck > 2)
		return;

	pIntersectingFloater = gclDamageFloat_FindOverlapping(pFloatGroup, v2Size, v2DstPosInOut, false);
	if (pIntersectingFloater)
	{
		v2DstPosInOut[1] = pIntersectingFloater->v2DestPos[1] + (pIntersectingFloater->v2Size[1] * 0.6f);
		gclDamageFloatCreate_ResolveLayoutArcOverlapMoveUp(pFloatGroup, v2Size, v2DstPosInOut, iRecurseCheck);
	}
}

static void gclDamageFloatCreate_ArcGetValidPosition(	DamageFloatTemplate *pTemplate,
														DamageFloatGroup *pFloatGroup, 
														DamageFloat *pFloater, 
														Vec2 vDstPosOut)
{
	MultiVal mv;
	F32 fArcMin, fArcMax, fRadius;
	ExprContext *pContext = DamageFloatGetContext();
	Vec3 vPos = {0, 0, 0};
	Vec3 zAxis = {0,0,1};
	bool bOverlapping = false;

	// check if we are overlapping
	exprEvaluate(pTemplate->pMinArc, pContext, &mv);
	fArcMin = MultiValGetFloat(&mv, NULL);
	exprEvaluate(pTemplate->pMaxArc, pContext, &mv);
	fArcMax = MultiValGetFloat(&mv, NULL);
	exprEvaluate(pTemplate->pArcRadius, pContext, &mv);
	fRadius = MultiValGetFloat(&mv, NULL);

	vPos[1] = fRadius;
	pFloater->fAngle = randomPositiveF32() * (fArcMax - fArcMin) + fArcMin;
	
	rotateVecAboutAxis(pFloater->fAngle - HALFPI, zAxis, vPos, vDstPosOut);
	
	if ((fArcMax - fArcMin) < RAD(45.f))
	{	// 
		bOverlapping = true;
	}
	else if (gclDamageFloat_FindOverlapping(pFloatGroup, pFloater->v2Size, vDstPosOut, true))
	{
		F32 fStep = (fArcMax - fArcMin)/7.f;
		S32 iTries = 0;
		bOverlapping = true; 

		do 
		{
			pFloater->fAngle += fStep;
			if (pFloater->fAngle > fArcMax)
			{
				pFloater->fAngle = fArcMin + (pFloater->fAngle - fArcMax);
			}
			
			rotateVecAboutAxis(pFloater->fAngle - HALFPI, zAxis, vPos, vDstPosOut);
			if (!gclDamageFloat_FindOverlapping(pFloatGroup, pFloater->v2Size, vDstPosOut, true))
			{
				bOverlapping = false;
				pFloater->fAngle = getVec2Yaw(vDstPosOut);
				break;
			}
		} while (++iTries <= 3);
	}

	if (bOverlapping)
	{
		static S32 s_iLastAngleResolved = 0;
		s_iLastAngleResolved++;
		switch (s_iLastAngleResolved)
		{
			xcase 0:
			case 1:
				pFloater->fAngle = fArcMin;
			xcase 2:
			case 3:
				pFloater->fAngle = fArcMax;
			xdefault:
				pFloater->fAngle = interpF32(0.5f, fArcMin, fArcMax);
				s_iLastAngleResolved = 0;
		}

		vPos[1] = fRadius * 0.5f;
		rotateVecAboutAxis(pFloater->fAngle - HALFPI, zAxis, vPos, vDstPosOut);

		gclDamageFloatCreate_ResolveLayoutArcOverlapMoveUp(pFloatGroup, pFloater->v2Size, vDstPosOut, 0);
		pFloater->fAngle = getVec2Yaw(vDstPosOut);
	}
	
}


/***************************************************************************
 * gclDamageFloatCreate
 *
 */
DamageFloat *gclDamageFloatCreate(DamageFloatTemplate *pTemplate, CombatTrackerNet *pNet, Entity *pTarget, F32 fDelay, F32 fDamagePct)
{
	static char *s_pchFlagText = NULL;
	static char *s_pchDefaultText = NULL;
	int iGroup;
	CBox targetScreenBox = {0};
	DamageFloat *pFloater = NULL;

	ExprContext *pContext = DamageFloatGetContext();
	Entity *pPlayer = entActivePlayerPtr();
	Entity *pOwner = entFromEntityRefAnyPartition(pNet->erOwner);
	Entity *pSource = entFromEntityRefAnyPartition(pNet->erSource);

	gclDamageFloat_Gamespecific_Preprocess(pNet, pTarget, fDelay);

	gclCombatAdvantage_ReportDamageFloat(pNet, pTarget, fDelay);

	if (!gclDamageFloat_ShouldShow(pNet, pTarget, pPlayer, pOwner, pSource))
		return NULL;

	if (g_DamageFloatTemplate.bDontAnchorToEntity)
	{
		F32 fZ = 0;
		if (!gclDamageFloat_GetScreenBoundingBox(pTarget, &targetScreenBox, &fZ))
			return NULL;
	}
	
	// filter damage

	if(!s_pchDefaultText)
	{
		estrCreate(&s_pchFlagText);
		estrCreate(&s_pchDefaultText);
	}
	else
	{
		estrClear(&s_pchFlagText);
		estrClear(&s_pchDefaultText);
	}
		


	// Generate the default strings for convenience in the expressions
	{
		int iAmount = (pNet->fMagnitude==0.f ? 0 : max(1,round(fabs(pNet->fMagnitude))));

		if(pNet->eFlags)
		{
			int i;

			for(i=1; i<=kCombatTrackerFlag_LASTFLOATER; i<<=1)
			{
				if(pNet->eFlags&i)
				{
					const char *pch = NULL;
					if(*s_pchFlagText)
					{
						estrAppend2(&s_pchFlagText," - ");
					}
					pch = StaticDefineGetTranslatedMessage(CombatTrackerFlagEnum, i);
					estrAppend2(&s_pchFlagText, pch ? pch : StaticDefineIntRevLookup(CombatTrackerFlagEnum,i));
				}
			}
		}
		exprContextSetStringVar(pContext, "FlagText", s_pchFlagText);

		estrCopy(&s_pchDefaultText, &s_pchFlagText);
		if(iAmount)
		{
			if(*s_pchFlagText)
			{
				estrConcatf(&s_pchDefaultText," - ");
			}
			estrConcatf(&s_pchDefaultText,"%d", iAmount);
		}
		exprContextSetStringVar(pContext, "DefaultText", s_pchDefaultText);

		exprContextSetIntVar(pContext, "Value", iAmount * fDamagePct);
		exprContextSetIntVar(pContext, "MitigatedValue", max(1,round(fabs(pNet->fMagnitudeBase-pNet->fMagnitude))) * fDamagePct);
	}

	// Evaluate all the expressions
	{
		UIStyleFont *pFont;
		const char *pch;
		MultiVal mv;

		exprContextSetPointerVar(pContext, "Player", pPlayer, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Source", pSource, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Target", pTarget, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Owner", pOwner, parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "Damage", pNet, parse_CombatTrackerNet, true, true);
		exprContextSetIntVar(pContext, "AutoAttack", !stricmp("Auto Attack",langTranslateMessageRef(LANGUAGE_ENGLISH,pNet->hDisplayName)));
		exprContextSetSelfPtr(pContext, pPlayer);
		exprContextSetPartition(pContext, PARTITION_CLIENT);

		exprEvaluate(pTemplate->pGroup, pContext, &mv);
		iGroup = MultiValGetInt(&mv, NULL);
		exprContextSetIntVar(pContext, "Group", iGroup);

		// Other expressions can use the result from Group.

		exprEvaluate(pTemplate->pText, pContext, &mv);
		pch = MultiValGetString(&mv, NULL);
		if(!pch || !*pch) return NULL;

		pFloater = StructCreate(parse_DamageFloat);

		pFloater->fDelay = fDelay / PAFX_FPS;

		pFloater->pchMessage = StructAllocString(pch);
		pFloater->uiPowID = pNet->powID;
		pFloater->combatTrackerFlags = pNet->eFlags&DAMAGE_FLOAT_TRACKER_FLAGS;

		exprEvaluate(pTemplate->pOffsetFrom, pContext, &mv);
		pFloater->eOffsetFrom = MultiValGetInt(&mv, NULL);
		
		exprEvaluate(pTemplate->pX, pContext, &mv);
		pFloater->v2PosOffset[0] = MultiValGetInt(&mv, NULL);

		exprEvaluate(pTemplate->pY, pContext, &mv);
		pFloater->v2PosOffset[1] = MultiValGetInt(&mv, NULL);
		
		copyVec2(pFloater->v2PosOffset, pFloater->v2Pos);

		exprEvaluate(pTemplate->pColor, pContext, &mv);
		pFloater->iColor = MultiValGetInt(&mv, NULL);

		if (pTemplate->pBottomColor)
		{
			exprEvaluate(pTemplate->pBottomColor, pContext, &mv);
			pFloater->iColor2 = MultiValGetInt(&mv, NULL);
		}
		else
			pFloater->iColor2 = pFloater->iColor;

		exprEvaluate(pTemplate->pLifetime, pContext, &mv);
		pFloater->fLifetime = MultiValGetFloat(&mv, NULL);
		pFloater->fMaxLifetime = pFloater->fLifetime;

		exprEvaluate(pTemplate->pScale, pContext, &mv);
		pFloater->fScale = MultiValGetFloat(&mv, NULL);

		if (pTemplate->pVelocityX)
		{
			exprEvaluate(pTemplate->pVelocityX, pContext, &mv);
			pFloater->v2VelocityBase[0] = MultiValGetFloat(&mv, NULL);
		}

		exprEvaluate(pTemplate->pVelocityY, pContext, &mv);
		pFloater->v2VelocityBase[1] = MultiValGetFloat(&mv, NULL);

		
		exprEvaluate(pTemplate->pIconName, pContext, &mv);
		if (MultiValGetString(&mv, NULL))
			pFloater->pIcon = atlasLoadTexture(MultiValGetString(&mv, NULL));
		if (pFloater->pIcon)
		{
			if (pTemplate->pIconOffsetFrom)
			{
				exprEvaluate(pTemplate->pIconOffsetFrom, pContext, &mv);
				pFloater->eIconOffsetFrom = MultiValGetInt(&mv, NULL);
			}

			if (pTemplate->pIconScale)
			{
				exprEvaluate(pTemplate->pIconScale, pContext, &mv);
				pFloater->fIconScale = MultiValGetFloat(&mv, NULL);
			}

			if (pTemplate->pIconX)
			{
				exprEvaluate(pTemplate->pIconX, pContext, &mv);
				pFloater->v2IconPosOffset[0] = MultiValGetInt(&mv, NULL);
			}

			if (pTemplate->pIconY)
			{
				exprEvaluate(pTemplate->pIconY, pContext, &mv);
				pFloater->v2IconPosOffset[1] = MultiValGetInt(&mv, NULL);
			}

			if (pTemplate->pIconColor)
			{
				exprEvaluate(pTemplate->pIconColor, pContext, &mv);
				pFloater->iIconColor = MultiValGetInt(&mv, NULL) & 0xFFFFFF00;
			}
			else
			{
				pFloater->iIconColor = 0xFFFFFF00;
			}
		}
		if (pTemplate->pPriority)
		{
			exprEvaluate(pTemplate->pPriority, pContext, &mv);
			pFloater->fPriority = MultiValGetFloat(&mv, NULL);
		}
		else
			pFloater->fPriority = 0;

		if (pTemplate->pPopout)
		{
			exprEvaluate(pTemplate->pPopout, pContext, &mv);
			pFloater->fMaxPopout = MultiValGetFloat(&mv, NULL);
		}
		else
			pFloater->fMaxPopout = 0;


		copyVec2(pFloater->v2VelocityBase, pFloater->v2Velocity);

		exprEvaluate(pTemplate->pFont, pContext, &mv);
		pch = MultiValGetString(&mv, NULL);
		SET_HANDLE_FROM_STRING("UIStyleFont", pch, pFloater->hFont);
		pFont = GET_REF(pFloater->hFont);
		if(pFont)
		{
			GfxFont *pGfxFont = GET_REF(pFont->hFace);
			if(pGfxFont)
			{
				pFloater->v2Size[0] = gfxfont_StringWidth(pGfxFont, pFloater->fScale, pFloater->fScale, pFloater->pchMessage);
				pFloater->v2Size[1] = gfxfont_FontHeight(pGfxFont, pFloater->fScale);
			}
		}

		if (g_DamageFloatTemplate.bDontAnchorToEntity)
		{
			Vec2 v2Pos;
			gclDamageFloat_GetAnchorPos(pFloater, &targetScreenBox, v2Pos);
			pFloater->v2Pos[0] += v2Pos[0];
			pFloater->v2Pos[1] += v2Pos[1];
		}
	}

	if (pFloater && (pNet->eFlags & kCombatTrackerFlag_Critical))
		pFloater->bCrit = true;

	// Add it to the list
	{
		DamageFloatGroup *pFloatGroup = eaGetStruct(&pTarget->pEntUI->eaDamageFloatGroups, parse_DamageFloatGroup, iGroup);
		
		if (g_DamageFloatGroupTemplates.ppGroups[iGroup]->eMotion == kDamageFloatMotion_Sticky)
		{
			//need a destination to determine where to stick
			switch (g_DamageFloatGroupTemplates.ppGroups[iGroup]->eLayout)
			{
			case kDamageFloatLayout_Arc:
				{
					//for arc motion need to rotate the given pos/velocity to face the target
					int i;
					Vec3 vDstPos;
					Vec3 vDstVel;
					bool bMerge = false;
					for (i = 0; i < eaSize(&pFloatGroup->eaDamageFloats); i++)
					{
						//match by display name, this is ugly but it's only here for the first draft of this feature
						//also throw all crits into their own bucket, per Zeke's request
						if (gclDamageFloatCreate_ShouldMerge(pFloater, pFloatGroup->eaDamageFloats[i]))
						{
							Vec3 zAxis = {0,0,1};
							Vec3 vVel = {0, pFloater->v2Velocity[1], 0};

							pFloater->fAngle = pFloatGroup->eaDamageFloats[i]->fAngle;

							rotateVecAboutAxis(pFloater->fAngle - PI/2, zAxis, vVel, vDstVel);
							vDstPos[0] = pFloatGroup->eaDamageFloats[i]->v2DestPos[0];
							vDstPos[1] = pFloatGroup->eaDamageFloats[i]->v2DestPos[1];
							bMerge = true;
							break;
						}
					}
						
					if (!bMerge)
					{
						gclDamageFloatCreate_ArcGetValidPosition(pTemplate, pFloatGroup, pFloater, vDstPos);
												
						copyVec2(vDstPos, vDstVel);
						normalVec2(vDstVel);
						scaleVec2(vDstVel, pFloater->v2Velocity[1], vDstVel);
					}
					pFloater->v2DestPos[0] = vDstPos[0];
					pFloater->v2DestPos[1] = vDstPos[1];

					pFloater->v2VelocityBase[0] = vDstVel[0];
					pFloater->v2VelocityBase[1] = vDstVel[1];

					copyVec2(pFloater->v2VelocityBase, pFloater->v2Velocity);

				}break;
			case kDamageFloatLayout_Linear:
				{
					//Linear + Sticky is used in NNO for "Immune" floaters
					//If there's already a floater with the same display name on-screen,
					//just reset its lifetime instead of spamming identical ones.
					int i;
					for (i = 0; i < eaSize(&pFloatGroup->eaDamageFloats); i++)
					{
						if (strcmp(pFloater->pchMessage, pFloatGroup->eaDamageFloats[i]->pchMessage) == 0)
						{
							pFloatGroup->eaDamageFloats[i]->fLifetime = pFloatGroup->eaDamageFloats[i]->fMaxLifetime;
							StructDestroy(parse_DamageFloat, pFloater);
							pFloater = NULL;
							break;
						}
					}
				}break;
			default:
				{
					Errorf("Damage floater created with Sticky motion and incompatible layout type %s.", StaticDefineInt_FastIntToString(DamageFloatLayoutEnum, g_DamageFloatGroupTemplates.ppGroups[iGroup]->eLayout));
				}
			}
		}
		if (pFloater)
			eaPush(&pFloatGroup->eaDamageFloats, pFloater);
		// Push off old ones if they don't fit
		while (eaSize(&pFloatGroup->eaDamageFloats) > MAX_DAMAGE_FLOATERS)
			StructDestroy(parse_DamageFloat, eaRemove(&pFloatGroup->eaDamageFloats, 0));
	}

	return pFloater;
}

// Sorting function for priority floaters
// Adds in an adjusted lifetime value to help prevent same-priority conflicts that make floaters shuffle around weirdly.
static int DamageFloater_PriorityComparator(const DamageFloat **pFloatA, const DamageFloat **pFloatB)
{
	if(((*pFloatA)->fPriority + (*pFloatA)->fLifetime/(*pFloatA)->fMaxLifetime) > ((*pFloatB)->fPriority + (*pFloatB)->fLifetime/(*pFloatB)->fMaxLifetime))
		return -1;
	else if (((*pFloatA)->fPriority + (*pFloatA)->fLifetime/(*pFloatA)->fMaxLifetime) < ((*pFloatB)->fPriority + (*pFloatB)->fLifetime/(*pFloatB)->fMaxLifetime))
		return 1;
	return 0;
}
/***************************************************************************
* DamageFloatLayout_PrioritySplatter
*
* Arranges floaters in a clumpy splatter with higher-priority one in the middle and lower-priority ones at the edges.
*/

#define POS_BUFFER_SIZE ((MAX_DAMAGE_FLOATERS)*3+1)

static void DamageFloatLayout_PrioritySplatter(DamageFloatGroup *pFloatGroup)
{
	int i;
	int j;
	int iCnt;
	F32 fLastY = 1;

	//store floater boxes so we can do collision testing on them
	static CBox s_pFloaterBoxes[MAX_DAMAGE_FLOATERS];

	iCnt = eaSize(&pFloatGroup->eaDamageFloats);
	eaQSort(pFloatGroup->eaDamageFloats, DamageFloater_PriorityComparator);
	for(i=0; i<iCnt; i++)
	{
		DamageFloat *p = pFloatGroup->eaDamageFloats[i];
		static F32 v2Positions[POS_BUFFER_SIZE][2];//buffer to hold possible positions for boxes, basically this is just a queue
		int iPos = 0;

		//set first position
		int numPositionsHeld = 1;
		v2Positions[0][0] = p->v2PosOffset[0];
		v2Positions[0][1] = p->v2Pos[1];

		//the floater is going to wind up centered, so we have to account for that
		CBoxSet(&(s_pFloaterBoxes[i]), v2Positions[iPos][0] -  p->v2Size[0]/2, v2Positions[iPos][1], v2Positions[iPos][0] + p->v2Size[0]/2, v2Positions[iPos][1] + p->v2Size[1]);

		//check for collisions with previous boxes
		//loop until we find a valid placement, or we fill up the buffer (which should never happen)
		//likely candidate for optimization, since we don't NEED to start at j=0 every time.
	 	while (iPos < numPositionsHeld)
		{
			int collisionIndex = -1;
			for (j = 0; j < i; j++)
			{
				if (CBoxIntersects(&s_pFloaterBoxes[i], &s_pFloaterBoxes[j]))
				{
					collisionIndex = j;
					break;
				}
			}
			if (collisionIndex > -1)
			{
				if (numPositionsHeld >= POS_BUFFER_SIZE)
				{
					//This should never happen in the course of normal gameplay, but it's possible
					//after a long alt-tab or editor binge for there to be an enormous backlog of damage floaters.
					//In that case, just allow some of them to stack on top of each other.
					break;
				}
				//try to place myself to the left, only if we're on the left side.
				if (v2Positions[iPos][0] < (p->v2PosOffset[0] + p->v2Size[0]/2))
				{
					v2Positions[numPositionsHeld][0] = s_pFloaterBoxes[collisionIndex].left - (p->v2Size[0]/2 + 8);
					v2Positions[numPositionsHeld++][1] = v2Positions[iPos][1];
				}

				//try to place myself to the right, only if we're on the right side.
				if (v2Positions[iPos][0] > (p->v2PosOffset[0] - p->v2Size[0]/2))
				{
					v2Positions[numPositionsHeld][0] = s_pFloaterBoxes[collisionIndex].right + 8 + p->v2Size[0]/2;
					v2Positions[numPositionsHeld++][1] = v2Positions[iPos][1];
				}

				//try to place myself above the conflicting box
				v2Positions[numPositionsHeld][0] = v2Positions[iPos][0];
				v2Positions[numPositionsHeld++][1] = s_pFloaterBoxes[collisionIndex].top + 2 + p->v2Size[1];
			}
			else
			{
				//we've got a valid placement
				break;
			}
			iPos++;
			CBoxSet(&(s_pFloaterBoxes[i]), v2Positions[iPos][0] -  p->v2Size[0]/2, v2Positions[iPos][1], v2Positions[iPos][0] + p->v2Size[0]/2, v2Positions[iPos][1] + p->v2Size[1]);
		}
		p->v2Pos[0] = v2Positions[iPos][0];
		p->v2Pos[1] = v2Positions[iPos][1];
	}
}
/***************************************************************************
 * DamageFloatLayout_Linear
 *
 */
static void DamageFloatLayout_Linear(DamageFloatGroup *pFloatGroup)
{
	int i;
	int iCnt;
	F32 fLastY = 1;

	iCnt = eaSize(&pFloatGroup->eaDamageFloats);
	for(i=0; i<iCnt; i++)
	{
		DamageFloat *p = pFloatGroup->eaDamageFloats[i];

		if(p->v2Velocity[1] > 0)
		{
			if(p->v2Pos[1] <= 0)
			{
				if(p->v2Pos[1] > fLastY)
				{
					// If this is ahead of the last one, push it below
					p->v2Pos[1] = fLastY;
				}
			}

			fLastY = p->v2Pos[1]-p->v2Size[1];
		}
		else
		{
			if(p->v2Pos[1] >= 0)
			{
				if(p->v2Pos[1] < fLastY+p->v2Size[1])
				{
					// If this is ahead of the last one, push it below
					p->v2Pos[1] = fLastY+p->v2Size[1];
				}
			}

			fLastY = p->v2Pos[1];
		}
	}
}

/***************************************************************************
 * DamageFloatLayout_ZigZag
 *
 */
static void DamageFloatLayout_ZigZag(DamageFloatGroup *pFloatGroup)
{
	int i;
	int iCnt;
	F32 fLastY = 1;
	F32 fLastX = 1;
	F32 fLastSize = 0;

	iCnt = eaSize(&pFloatGroup->eaDamageFloats);
	for(i=0; i<iCnt; i++)
	{
		DamageFloat *p = pFloatGroup->eaDamageFloats[i];

		if(p->v2Velocity[1] > 0)
		{
			if(p->v2Pos[1] <= 0)
			{
				if(p->v2Pos[1] > fLastY)
				{
					if(p->fScale>2.5 || strlen(p->pchMessage)>5)
					{
						p->v2Pos[0] = 0;
						p->v2Pos[1] = fLastY;
					}
					else if(fLastX==0)
					{
						p->v2Pos[0] = .005;
						p->v2Pos[1] = fLastY;
					}
					else
					{
						if(fLastX<1)
							p->v2Pos[0] = fLastSize/2 + p->v2Size[0]/2 + p->v2Size[1]*.2;
						else
							p->v2Pos[0] = .005;

						p->v2Pos[1] = fLastY+p->v2Size[1]*.4;
					}
				}
			}

			fLastX = p->v2Pos[0];
			fLastSize = p->v2Size[0];
			fLastY = p->v2Pos[1]-p->v2Size[1];
		}
		else
		{
			if(p->v2Pos[1] >= 0)
			{
				if(p->v2Pos[1] < fLastY+p->v2Size[1])
				{
					if(p->fScale>2.5 || strlen(p->pchMessage)>5)
					{
						p->v2Pos[0] = 0;
						p->v2Pos[1] = fLastY;
					}
					else if(fLastX==0)
					{
						p->v2Pos[0] = .005;
						p->v2Pos[1] = fLastY+p->v2Size[1];
					}
					else
					{
						if(fLastX<1)
							p->v2Pos[0] = fLastSize/2 + p->v2Size[0]/2 + p->v2Size[1]*.2;
						else
							p->v2Pos[0] = .005;

						p->v2Pos[1] = fLastY+p->v2Size[1]*.6;
					}
				}
			}

			fLastX = p->v2Pos[0];
			fLastSize = p->v2Size[0];
			fLastY = p->v2Pos[1];
		}
	}
}

/***************************************************************************
 * DamageFloatLayout_Splatter
 *
 */
void DamageFloatLayout_Splatter(DamageFloatGroup *pFloatGroup)
{
	static F32 s_afPerturbX[] = { 1.2,  -1.3, -0.8,  0.5,  2,  -2, 1.0 };
	static F32 s_afPerturbY[] = {  1,    0.5,  1.3,  0,   0.7,  .2 };
	static int s_idxX = 0;
	static int s_idxY = 0;

	int i;
	int iCnt;
	F32 fLastY = 1;

	iCnt = eaSize(&pFloatGroup->eaDamageFloats);
	for(i=0; i<iCnt; i++)
	{
		DamageFloat *p = pFloatGroup->eaDamageFloats[i];

		if(p->v2Velocity[1] > 0)
		{
			if(p->v2Pos[1] <= 0)
			{
				if(p->v2Pos[1] > fLastY)
				{
					if(p->fScale>2.5 || strlen(p->pchMessage)>5)
					{
						p->v2Pos[0] = 0;
						p->v2Pos[1] = fLastY;
					}
					else
					{
						p->v2Pos[0] = s_afPerturbX[s_idxX++]*p->v2Size[0]*1.3;
						p->v2Pos[1] = s_afPerturbY[s_idxY++]*p->v2Size[1]*2-p->v2Size[1]/2;

						s_idxX %= ARRAY_SIZE(s_afPerturbY);
						s_idxY %= ARRAY_SIZE(s_afPerturbY);
					}
				}
			}

			fLastY = min(p->v2Pos[1]-p->v2Size[1], fLastY);
		}
	}
}

/***************************************************************************
 * gclDamageFloatLayout
 *
 */
void gclDamageFloatLayout(DamageFloatTemplate *pTemplate, Entity *pTarget)
{
	int iGroup;

	if(!pTarget || !pTarget->pEntUI || !pTarget->pEntUI->eaDamageFloatGroups)
		return;

	for(iGroup=eaSize(&pTarget->pEntUI->eaDamageFloatGroups)-1; iGroup>=0; iGroup--)
	{
		DamageFloatGroup *pFloatGroup = eaGet(&pTarget->pEntUI->eaDamageFloatGroups, iGroup);
		DamageFloatGroupTemplate *pDef = eaGet(&g_DamageFloatGroupTemplates.ppGroups, iGroup);

		if(!pDef) continue;

		switch(pDef->eLayout)
		{
			case kDamageFloatLayout_Linear:
				DamageFloatLayout_Linear(pFloatGroup);
				break;
			case kDamageFloatLayout_ZigZag:
				DamageFloatLayout_ZigZag(pFloatGroup);
				break;
			case kDamageFloatLayout_Splatter:
				DamageFloatLayout_Splatter(pFloatGroup);
				break;
			case kDamageFloatLayout_PrioritySplatter:
				DamageFloatLayout_PrioritySplatter(pFloatGroup);
				break;
		}
	}
}

static bool DamageFloat_StickAtDestShouldMerge(DamageFloat *pFloat, DamageFloat *pCmpDamageFloat)
{
	if (pFloat == pCmpDamageFloat)
		return false;
	
	if (pFloat->combatTrackerFlags != 0 && 
		pFloat->combatTrackerFlags == pCmpDamageFloat->combatTrackerFlags && 
		!stricmp(pFloat->pchMessage, pCmpDamageFloat->pchMessage))
	{
		return sameVec2(pFloat->v2Pos, pCmpDamageFloat->v2Pos);
	}

	return pFloat->uiPowID != 0 &&
		(pFloat->uiPowID == pCmpDamageFloat->uiPowID) && 
		(pFloat->bCrit == pCmpDamageFloat->bCrit) &&
		sameVec2(pFloat->v2Pos, pCmpDamageFloat->v2Pos);

}

/***************************************************************************
 * DamageFloatTick_Sticky
 *
 */
static void DamageFloat_StickAtDest(DamageFloatGroup* pFloatGroup, DamageFloat* p)
{
	int j;
	p->v2Pos[0] = p->v2DestPos[0];
	p->v2Pos[1] = p->v2DestPos[1];
	p->v2Velocity[0] = 0;
	p->v2Velocity[1] = 0;
	p->fLifetime = p->fMaxLifetime;
	for (j = 0; j < eaSize(&pFloatGroup->eaDamageFloats); j++)
	{
		DamageFloat *pCmpDamageFloat = pFloatGroup->eaDamageFloats[j];
		if (DamageFloat_StickAtDestShouldMerge(p, pCmpDamageFloat))
		{
			//merge floaters, assume they're both ints for now.
			int iVal = atoi(pCmpDamageFloat->pchMessage);
			if (iVal)
			{
				char buf[16];
				iVal += atoi(p->pchMessage);
				free(pCmpDamageFloat->pchMessage);
				sprintf_s(SAFESTR(buf), "%i", iVal);
				pCmpDamageFloat->pchMessage = StructAllocString(buf);
			}
			
			pCmpDamageFloat->fLifetime = pCmpDamageFloat->fMaxLifetime;
			if (iVal && p->pIcon != pCmpDamageFloat->pIcon && 
				pCmpDamageFloat->fIconFade == 0.f)
			{
				if (!p->pIcon)
				{
					pCmpDamageFloat->fIconFade = 1.f;
				}
				else
				{
					pCmpDamageFloat->pIcon = p->pIcon;
					pCmpDamageFloat->eIconOffsetFrom = p->eIconOffsetFrom;
					copyVec2(pCmpDamageFloat->v2IconPos, p->v2IconPos);
					copyVec2(pCmpDamageFloat->v2IconPosOffset, p->v2IconPosOffset);
					pCmpDamageFloat->iIconColor = p->iIconColor;
					pCmpDamageFloat->fIconScale = p->fIconScale;
				}
			}
			
			p->fLifetime = 0;
		}
	}
}

/***************************************************************************
 * DamageFloatTick_Accelerate
 *
 */
static void DamageFloatTick_Accelerate(DamageFloatGroup *pFloatGroup)
{
	int i;
	int iNumToFit = 0;
	Vec2 v2Vel = {0};

	for(i=0; i<eaSize(&pFloatGroup->eaDamageFloats); i++)
	{
		DamageFloat *p = pFloatGroup->eaDamageFloats[i];
		if (p->fDelay > 0)
			continue;
		if(p->v2Velocity[1] > 0)
		{
			if(p->v2Pos[1] <= 0)
				iNumToFit++;

			if(v2Vel[0] < p->v2Velocity[0])  v2Vel[0] = p->v2Velocity[0];
			if(v2Vel[1] < p->v2Velocity[1])  v2Vel[1] = p->v2Velocity[1];
		}
		else
		{
			if(p->v2Pos[1] >= 0)
				iNumToFit++;

			if(v2Vel[0] > p->v2Velocity[0])  v2Vel[0] = p->v2Velocity[0];
			if(v2Vel[1] > p->v2Velocity[1])  v2Vel[1] = p->v2Velocity[1];
		}
	}

	if(iNumToFit > 1)
	{
		scaleVec2(v2Vel, 1.03, v2Vel);

		// OK, we have to fit more than one.
		for(i=0; i<eaSize(&pFloatGroup->eaDamageFloats); i++)
		{
			DamageFloat *p = pFloatGroup->eaDamageFloats[i];

			copyVec2(v2Vel, p->v2Velocity);
		}
	}
	else
	{
		scaleVec2(v2Vel, .9, v2Vel);

		for(i=0; i<eaSize(&pFloatGroup->eaDamageFloats); i++)
		{
			DamageFloat *p = pFloatGroup->eaDamageFloats[i];

			copyVec2(v2Vel, p->v2Velocity);
			if(abs(p->v2Velocity[1]) < abs(p->v2VelocityBase[1]))
			{
				copyVec2(p->v2VelocityBase, p->v2Velocity);
			}
		}
	}
}

/***************************************************************************
 * gclDamageFloatTick
 *
 */
void gclDamageFloatTick(DamageFloatTemplate *pTemplate, Entity *pTarget, F32 fElapsedTime)
{
	int iGroup;

	if(!pTarget || !pTarget->pEntUI || !pTarget->pEntUI->eaDamageFloatGroups)
		return;

	for(iGroup=eaSize(&pTarget->pEntUI->eaDamageFloatGroups)-1; iGroup>=0; iGroup--)
	{
		int i;
		Vec2 v2Vel = {0};
		int iNumToFit = 0;

		DamageFloatGroup *pFloatGroup = eaGet(&pTarget->pEntUI->eaDamageFloatGroups, iGroup);
		DamageFloatGroupTemplate *pDef = eaGet(&g_DamageFloatGroupTemplates.ppGroups, iGroup);

		if(!pDef) continue;

 		for(i=0; i<eaSize(&pFloatGroup->eaDamageFloats); i++)
		{
			DamageFloat *p = pFloatGroup->eaDamageFloats[i];

			p->fDelay -= fElapsedTime;
			if (p->fDelay <= 0)
			{
				p->fLifetime -= fElapsedTime;
				if (pDef->eMotion == kDamageFloatMotion_Sticky)
				{
					//if we're moving and are about to pass our destination
					if ((p->v2Velocity[0] != 0 && (p->v2Pos[0] >= p->v2DestPos[0]) != (p->v2Pos[0] + p->v2Velocity[0] * fElapsedTime >= p->v2DestPos[0])) ||
						(p->v2Velocity[1] != 0 && (p->v2Pos[1] >= p->v2DestPos[1]) != (p->v2Pos[1] + p->v2Velocity[1] * fElapsedTime >= p->v2DestPos[1])))
					{
						DamageFloat_StickAtDest(pFloatGroup, p);
					}
				}
				p->v2Pos[0] += p->v2Velocity[0] * fElapsedTime;
				p->v2Pos[1] += p->v2Velocity[1] * fElapsedTime;

				if(p->fLifetime < 0)
				{
					StructDestroy(parse_DamageFloat, eaRemove(&pFloatGroup->eaDamageFloats, i--));
				}
			}
		}

		switch(pDef->eMotion)
		{
			case kDamageFloatMotion_Accelerate:
				DamageFloatTick_Accelerate(pFloatGroup);
				break;
			case kDamageFloatMotion_Linear:
				break;
		}
	}
}

/***************************************************************************
 * FloatIsVisible
 *
 */
__forceinline static bool FloatIsVisible(DamageFloat *pFloat)
{
	return (pFloat->fDelay <= 0);
}

/***************************************************************************
 * gclDrawDamageFloaters
 *
 */
void gclDrawDamageFloaters(Entity *pEnt, F32 fScale)
{
	F32 fZ;
	S32 i;
	int iGroup;
	CBox box;

	if(!pEnt || !pEnt->pEntUI || !pEnt->pEntUI->eaDamageFloatGroups)
		return;

	if (!ShowAnyDamageFloaters())
		return;

	gclDamageFloat_GetScreenBoundingBox(pEnt, &box, &fZ /* placeholder */);
	fZ = UI_GET_Z();

	gclDamageFloatTick(&g_DamageFloatTemplate, pEnt, gGCLState.frameElapsedTime);

	if (!g_DamageFloatTemplate.bDontAnchorToEntity && !entIsVisible(pEnt))
		return;

	for(iGroup = eaSize(&pEnt->pEntUI->eaDamageFloatGroups)-1; iGroup>=0; iGroup--)
	{
		DamageFloatGroup *pFloatGroup = eaGet(&pEnt->pEntUI->eaDamageFloatGroups, iGroup);

		if(!pFloatGroup)
			continue;
		
		for (i = 0; i < eaSize(&pFloatGroup->eaDamageFloats); i++)
		{
			DamageFloat *pInfo = pFloatGroup->eaDamageFloats[i];
			Vec2 v2Pos = {0};
			
			if (pInfo->pchMessage && FloatIsVisible(pInfo))
			{
				S32 iAlpha = CLAMP(0xFF * pInfo->fLifetime, 0, 0xFF);
				//take the first half of a sine-wave and stretch it over 0.25 seconds, then multiply by the popout magnitude.
				//If we're older than 0.25 seconds, don't bother calculating it at all.
				F32 popoutSineValue = (pInfo->fMaxPopout && (pInfo->fMaxLifetime - pInfo->fLifetime < 0.25f)) ? sinf(min(PI, (pInfo->fMaxLifetime - pInfo->fLifetime)*4*PI)) : 0;
				F32 fNumScale = 0;
				F32 fPopAddScale = 0.f;
				UIStyleFont* pFont = GET_REF(pInfo->hFont);
				F32 fTargetPositionScale = 1.f;
				
				if (g_DamageFloatGroupTemplates.ppGroups[iGroup]->eMotion == kDamageFloatMotion_Sticky && pInfo->v2Velocity[0] != 0 || pInfo->v2Velocity[1] != 0)
					popoutSineValue = 0;
				
				fPopAddScale = pInfo->fMaxPopout * popoutSineValue;
				fNumScale = fScale * (pInfo->fScale + fPopAddScale);
				ui_StyleFontUse(pFont, false, kWidgetModifier_None);
				gfxfont_SetColorRGBA(pInfo->iColor, pInfo->iColor2);
				gfxfont_MultiplyAlpha(iAlpha);

				fTargetPositionScale = gclDamageFloat_GetPositionScale(&box);

				if (!g_DamageFloatTemplate.bDontAnchorToEntity)
				{
					gclDamageFloat_GetAnchorPos(pInfo, &box, v2Pos);
					v2Pos[0] = v2Pos[0] + pInfo->v2Pos[0] * fTargetPositionScale;
					v2Pos[1] = v2Pos[1] - (pInfo->v2Pos[1] + pInfo->v2Size[1]) * fTargetPositionScale;
				}
				else
				{
					v2Pos[0] = pInfo->v2Pos[0];
					v2Pos[1] = (pInfo->v2Pos[1] + pInfo->v2Size[1]);
				}

				if (pInfo->pIcon)
				{
 					F32 fIconHeight = pInfo->pIcon->height * (pInfo->fIconScale + fPopAddScale) * fScale;
					F32 fIconWidth = pInfo->pIcon->width * (pInfo->fIconScale + fPopAddScale) * fScale;
					GfxFont *pGfxFont = GET_REF(pFont->hFace);
					F32 fFontHeight = gfxfont_FontHeight(pGfxFont, pInfo->fScale);
					F32 fMsgWidth = gfxfont_StringWidth(pGfxFont, fNumScale, fNumScale, pInfo->pchMessage);
					Vec2 v2IconPos;
					CBox spriteBox;
					S32 iIconAlpha = iAlpha;
					
					bool bIconFaded = false;

					if (pInfo->fIconFade)
					{
						pInfo->fIconFade -= gGCLState.frameElapsedTime;
						if (pInfo->fIconFade <= 0.f)
						{
							pInfo->fIconFade = 0.f;
							bIconFaded = true;
						}
						iIconAlpha = 0xFF * pInfo->fIconFade;
						
					}
					

					v2IconPos[0] = v2Pos[0];
					v2IconPos[1] = v2Pos[1] - fFontHeight/2;

					if (pInfo->eIconOffsetFrom & UILeft)
					{
						v2IconPos[0] -= fMsgWidth * 0.5f + fIconWidth * 0.5f;
					}
					else if (pInfo->eIconOffsetFrom & UIRight)
					{
						v2IconPos[0] += fMsgWidth * 0.5f + fIconWidth * 0.5f;
					}

					BuildCBoxFromCenter(&spriteBox, v2IconPos[0], v2IconPos[1], fIconWidth, fIconHeight);
							
					display_sprite_box(pInfo->pIcon, &spriteBox, 1, pInfo->iIconColor | iIconAlpha);

					if (bIconFaded)
					{
						pInfo->pIcon = NULL;
					}
				}
				
 				gfxfont_Printf(v2Pos[0], v2Pos[1], fZ, fNumScale, fNumScale, CENTER_X, "%s",pInfo->pchMessage);
			
			}
		}
	}
}

#include "DamageFloaters_h_ast.c"

/* End of File */
