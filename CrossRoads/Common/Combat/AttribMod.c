/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "AttribMod.h"

#include "Entity.h"
#include "entCritter.h"
#include "estring.h"
#include "Expression.h"
#include "MemoryPool.h"
#include "net.h"
#include "rand.h"
#include "TriCube/vec.h"

#include "AttribModFragility.h"
#include "AttribModFragility_h_ast.h"
#include "Character.h"
#include "Character_combat.h"
#include "Character_target.h"
#include "Character_tick.h"
#include "CharacterClass.h"
#include "CharacterAttribs.h"
#include "CombatCallbacks.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "CombatSensitivity.h"
#include "AutoGen/Powers_h_ast.h"
#include "CombatMods.h"
#include "CommandQueue.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerApplication.h"
#include "AutoGen/PowerApplication_h_ast.h"
#include "PowerEnhancements.h"
#include "PowerHelpers.h"
#include "PowersEnums_h_ast.h"
#include "PowerSubtarget.h"
#include "AutoGen/PowerSubtarget_h_ast.h"
#include "PowerVars.h"
#include "WorldGrid.h"
#include "dynAnimGraphPub.h"
#include "StringCache.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
	#include "EntityMovementManager.h"
	#include "EntityMovementTactical.h"
#endif

#if GAMESERVER 
	#include "aiLib.h"
	#include "gslCombatAdvantage.h"
	#include "gslCombatDeathPrediction.h"
	#include "gslEntity.h"
	#include "gslPartition.h"
	#include "gslPowerTransactions.h"
	#include "gslProjectileEntity.h"
	#include "gslPVP.h"
	#include "gslQueue.h"
	#include "mapstate_common.h"
	#include "PlayerDifficultyCommon.h"
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#endif

#include "AutoGen/AttribMod_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Whether variance is enabled
static S32 s_bPowersVariance = 1;
AUTO_CMD_INT(s_bPowersVariance, PowersVariance) ACMD_CATEGORY(Powers, DEBUG) ACMD_SERVERONLY;

// Whether to enable extra attribmod data collection when profiling.  Made conditional because it really slows things down.
static S32 s_bEnableExtraAttribmodProfileData = 0;
AUTO_CMD_INT(s_bEnableExtraAttribmodProfileData, EnableExtraAttribmodProfileData) ACMD_CATEGORY(Powers, DEBUG) ACMD_SERVERONLY;

extern ParseTable parse_CharacterAttribs[];
#define TYPE_parse_CharacterAttribs CharacterAttribs

DictionaryHandle g_hFragileScaleSetDict;

MP_DEFINE(AttribMod);
MP_DEFINE(AttribModNet);
MP_DEFINE(AttribModSourceDetails);
MP_DEFINE(PowerApplyStrength);
MP_DEFINE(AttribModLink);

// Parse tables configured for use in expressions
ParseTable parse_AttribModDef_ForExpr[] =
{
	{ "AttribModDef_ForExpr", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(AttribModDef), 0, NULL, 0 },
	{ "{",						TOK_START, 0 },
	{ "Tags",					TOK_EMBEDDEDSTRUCT(AttribModDef, tags, parse_PowerTagsStruct)},
	{ "Attrib",					TOK_AUTOINT(AttribModDef, offAttrib, -1), AttribTypeEnum },
	{ "Aspect",					TOK_AUTOINT(AttribModDef, offAspect, -1), AttribAspectEnum },
	{ "ArcAffects",				TOK_F32(AttribModDef, fArcAffects, 0), NULL },
	{ "Yaw",					TOK_F32(AttribModDef, fYaw, 0), NULL },
	{ "}",						TOK_END, 0 },
	{ "", 0, 0 }
};

ParseTable parse_AttribMod_ForExpr[] =
{
	{ "AttribMod_ForExpr", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(AttribMod), 0, NULL, 0 },
	{ "{",					TOK_START, 0 },
	{ "ApplyID",			TOK_AUTOINT(AttribMod, uiApplyID, 0), NULL },
	{ "Active",				TOK_BIT, 0, 8, NULL },
	{ "Fragile",			TOK_BIT, 0, 8, NULL },
	{ "Duration",			TOK_F32(AttribMod, fDuration, 0), NULL },
	{ "DurationOriginal",	TOK_F32(AttribMod, fDurationOriginal, 0), NULL },
	{ "Magnitude",			TOK_F32(AttribMod, fMagnitude, 0), NULL },
	{ "MagnitudeOriginal",	TOK_F32(AttribMod, fMagnitudeOriginal, 0), NULL },
	// TODO(JW): Support walking into the Fragility substructure for health data
//	{ "Health",				TOK_F32(AttribMod, fHealth, 0), NULL },
//	{ "HealthOriginal",		TOK_F32(AttribMod, fHealthOriginal, 0), NULL },
	{ "}",					TOK_END, 0 },
	{ "", 0, 0 }
};

// declarations of static functions
static void AttribModLink_PostApplyModsFromPowerDef(PowerDef *pPowerDef, AttribMod **ppLinkedMods, Character *pCharTarget, Character *pCharSource);
static void AttribModLink_ModCancel(int iPartitionIdx, AttribMod *pmod, Character *pchar, bool bInProcessingPending);
static void AttribModLink_LinkedAttribCancel(int iPartitionIdx, Character *pChar, AttribMod *pMod);
static bool AttribModLink_ProcessPendingGoingCurrent(int iPartition, AttribMod *pMod, Character *pchar, S32 *pbDestroyOut);

// Initialization

AUTO_RUN;
void InitAttribMod(void)
{
	ParserSetTableInfo(parse_AttribModDef_ForExpr, sizeof(AttribModDef), "AttribModDef_ForExpr", NULL, __FILE__, false, true);
	ParserSetTableInfo(parse_AttribMod_ForExpr, sizeof(AttribMod), "AttribMod_ForExpr", NULL, __FILE__, false, true);

#ifdef GAMESERVER
	MP_CREATE_COMPACT(AttribMod, 100, 200, 0.80);
	MP_CREATE(AttribModNet, 100);
	MP_CREATE(AttribModLink, 100);
	MP_CREATE(AttribModSourceDetails, 20);
	MP_CREATE(PowerApplyStrength, 20);
#else
	MP_CREATE_COMPACT(AttribMod, 20, 100, 0.80);
	MP_CREATE(AttribModNet, 20);
	MP_CREATE(AttribModLink, 20);
	MP_CREATE(AttribModSourceDetails, 4);
	MP_CREATE(PowerApplyStrength, 4);
#endif
}

// Globally available group set
AttribSets g_AttribSets;

// Attempts to return the AttribModDef of the AttribMod.  May return NULL.
AttribModDef *mod_GetDef(AttribMod *pmod)
{
	AttribModDef *pdef = NULL;
	if(pmod)
	{
#ifdef GAMESERVER
		pdef = pmod->pDef;
#else
		PowerDef *ppowdef = GET_REF(pmod->hPowerDef);
		if(ppowdef && pmod->uiDefIdx < eaUSize(&ppowdef->ppOrderedMods))
		{
			pdef = ppowdef->ppOrderedMods[pmod->uiDefIdx];
		}
#endif
	}
	return pdef;
}

// Attempts to return the AttribModDef of the AttribModNet.  May return NULL.
AttribModDef *modnet_GetDef(AttribModNet *pmodnet)
{
	AttribModDef *pdef = NULL;
	if(pmodnet)
	{
		PowerDef *ppowdef = GET_REF(pmodnet->hPowerDef);
		if(ppowdef && pmodnet->uiDefIdx < eaUSize(&ppowdef->ppOrderedMods))
		{
				pdef = ppowdef->ppOrderedMods[pmodnet->uiDefIdx];
		}
	}
	return pdef;
}

// the method the mod_AngleToSource uses to get th angle to the source
F32 mod_AngleToSourcePosUtil(Entity *pent, const Vec3 vSourcePos)
{
	F32 fAngle = 0;

	if(!ISZEROVEC3(vSourcePos))
	{
		Vec3 vecCurPos, vecToSource;
		F32 fAngleToSource;
		Vec2 pyFace;

		entGetCombatPosDir(pent,NULL,vecCurPos,NULL);

		entGetFacePY(pent, pyFace);

		subVec3(vSourcePos, vecCurPos, vecToSource);

		fAngleToSource = atan2(vecToSource[0],vecToSource[2]);
		fAngle = subAngle(fAngleToSource,pyFace[1]);
	}

	return fAngle;
}

// Returns the angle to the source of an AttribMod, give the current position and orientation of
//  the Entity.  Return value is in radians.
F32 mod_AngleToSource(AttribMod *pmod, Entity *pent)
{
	return mod_AngleToSourcePosUtil(pent, pmod->vecSource);
}

// Returns true if the AttribModDef is allowed to affect given the AngleToSource
__forceinline static S32 _affectsModFromAngleToSource(AttribModDef *pmoddefAffects,Character *pchar,F32 fAngleToSource)
{
	F32 fAngle = pmoddefAffects->fYaw ? subAngle(fAngleToSource,RAD(pmoddefAffects->fYaw)) : fAngleToSource;
	return  !(fabs(fAngle) > RAD(pmoddefAffects->fArcAffects)/2.f);
}

// Returns true if the AttribModDef is allowed to affect given the AngleToSource
S32 moddef_AffectsModFromAngleToSource(AttribModDef *pmoddefAffects,
										Character *pchar,
										F32 fAngleToSource)
{
	S32 bAffects = true;

	if(pmoddefAffects->fArcAffects > 0)
	{
		return _affectsModFromAngleToSource(pmoddefAffects, pchar, fAngleToSource);
	}

	return bAffects;
}

// Returns true if the AttribModDef is allowed to affect the target AttribMod, based on the yaw/arc of the
//  main AttribModDef and the incoming direction of the target AttribMod.  This does not also perform the
//  more general moddef_AffectsModOrPower() check.
S32 moddef_AffectsModFromDirection(AttribModDef *pmoddefAffects,
								   Character *pchar,
								   AttribMod *pmodTarget)
{
	S32 bAffects = true;

	if(pmoddefAffects->fArcAffects > 0)
	{
		if(!pmodTarget)
		{
			bAffects = false;
		}
		else
		{
			F32 fAngleToSource = mod_AngleToSource(pmodTarget,pchar->pEntParent);
			return _affectsModFromAngleToSource(pmoddefAffects, pchar, fAngleToSource);
		}
	}

	return bAffects;
}

// Returns true if the AttribModDef in question is allowed to affect the target mod and/or power.  Because of
//  the different situations in which this may be called, all parameters except the primary are optional.
int moddef_AffectsModOrPower(int iPartitionIdx,
							 AttribModDef *pmoddefAffects,
							 Character *pchar,
							 AttribMod *pmodAffects,
							 AttribModDef *pmoddefTarget,
							 AttribMod *pmodTarget,
							 PowerDef *ppowdefTarget)
{
	int bRet = true;
	if(pmoddefAffects->pExprAffects)
	{
		if(pmoddefTarget || pmodTarget || ppowdefTarget)
		{
			PERFINFO_AUTO_START_FUNC();
			combateval_ContextSetupAffects(pchar,pmodAffects,ppowdefTarget,pmoddefTarget,pmodTarget,NULL);
			bRet = (0.f!=combateval_EvalNew(iPartitionIdx, pmoddefAffects->pExprAffects,kCombatEvalContext_Affects,NULL));
			PERFINFO_AUTO_STOP();
		}
		else
		{
			// Affects expression with nothing to affect
			bRet = false;
		}
	}
	return bRet;
}

// Returns true if the given pExprAffects returns true that it affects the target mod and/or power.  Because of
//  the different situations in which this may be called, all parameters except the primary are optional.
int mod_AffectsModOrPower(	int iPartitionIdx,
							 Expression *pExprAffects,
							 Character *pchar,
							 AttribModDef *pmoddefTarget,
							 AttribMod *pmodTarget,
							 PowerDef *ppowdefTarget)
{
	int bRet = true;
	if(pmoddefTarget || pmodTarget || ppowdefTarget)
	{
		PERFINFO_AUTO_START_FUNC();
		combateval_ContextSetupAffects(pchar,NULL,ppowdefTarget,pmoddefTarget,pmodTarget,NULL);
		bRet = (0.f!=combateval_EvalNew(iPartitionIdx, pExprAffects,kCombatEvalContext_Affects,NULL));
		PERFINFO_AUTO_STOP();
	}
	else
	{
		// Affects expression with nothing to affect
		bRet = false;
	}
	return bRet;
}

// Gets the actual effective magnitude of an AttribModDef, based
//  on a specific class and level
F32 moddef_GetMagnitude(int iPartitionIdx,
								   AttribModDef *pdef,
								   CharacterClass *pClass,
								   int iLevel,
								   F32 fTableScale,
								   int bApply)
{
	F32 fMag = 0;
	if(pdef->pExprMagnitude)
	{
		fMag = combateval_EvalNew(iPartitionIdx, pdef->pExprMagnitude,bApply?kCombatEvalContext_Apply:kCombatEvalContext_Simple,NULL);

		// Scale by the default table if it's relevant and specified
		if(pdef->eType&kModType_Magnitude && pdef->pchTableDefault)
		{
			if(pClass)
			{
				fTableScale *= class_powertable_Lookup(pClass,pdef->pchTableDefault,iLevel-1);
			}
			else
			{
				fTableScale *= powertable_Lookup(pdef->pchTableDefault,iLevel-1);
			}
			fMag *= fTableScale;
		}
	}

	return fMag;
}

// Writes some debug data about each AttribMod to the estring
static void AttribModArrayToEString(AttribMod **ppMods, char **ppchString)
{
	int i, s=eaSize(&ppMods);
	for(i=0; i<s; i++)
	{
		AttribMod *pmod = ppMods[i];
		AttribModDef *pdef = pmod->pDef;
		estrConcatf(ppchString,"%s %d %f %d %d %d\n",pdef->pPowerDef->pchName,pdef->uiDefIdx,pmod->fDuration,pmod->uiPowerID,pmod->uiApplyID,pmod->erSource);
	}
}



// Returns true if the PowerTagsStruct includes the specified tag
S32 powertags_Check(PowerTagsStruct *pTags, S32 iTag)
{
	return (-1!=eaiFind(&pTags->piTags,iTag));
}


// Fills an AttribMod with data from the def.  Uses the source, target and context to evaluate
//  the magnitude and duration expressions.  Uses the enhancements, class, level and str attribs 
//  to  fill in various fields and apply strength.
static void ModFill(int iPartitionIdx,
				    SA_PARAM_NN_VALID AttribMod *pmod,
					SA_PARAM_NN_VALID AttribModDef *pdef,
					S32 iLevelTable,
					SA_PARAM_NN_VALID PowerApplication *papp,
					EntityRef erTargetMod, // Target of AttribMod
					SA_PARAM_NN_VALID Character *pcharTargetApp, // Current target of Application, not AttribMod
					SA_PARAM_OP_VALID Power **ppEnhancements, 
					SA_PARAM_OP_VALID ExprContext **ppOutContext)
{
	// For these to work correctly AttribMods have to be created in order, with the derived mods
	//  directly following the AttribSet mod.
	static F32 s_fSetBasicDuration = 0;
	static F32 s_fSetBasicMagnitude = 0;

	F32 fStr = 0.f, fStrAdd = 0.f;
	
	PERFINFO_AUTO_START_FUNC();

	// Fill in base data
	pmod->pDef = pdef;
	SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdef->pPowerDef,pmod->hPowerDef);
	pmod->uiVersion = pdef->pPowerDef->uiVersion;
	pmod->uiDefIdx = pdef->uiDefIdx;

	// Find basic values for duration, magnitude, health
	if(pdef->bDerivedInternally)
	{
		pmod->fDuration = s_fSetBasicDuration;
		pmod->fMagnitude = s_fSetBasicMagnitude;
	}
	else
	{
		if(pdef->pExprDuration)
		{
			PERFINFO_AUTO_START("EvalDuration",1);
			pmod->fDuration = combateval_EvalNew(iPartitionIdx, pdef->pExprDuration,kCombatEvalContext_Apply,NULL);
			PERFINFO_AUTO_STOP();
		}
		if(pdef->pExprMagnitude)
		{
			PERFINFO_AUTO_START("EvalMagnitude",1);
			pmod->fMagnitude = combateval_EvalNew(iPartitionIdx, pdef->pExprMagnitude,kCombatEvalContext_Apply,NULL);
			PERFINFO_AUTO_STOP();
		}

		if(IS_SET_ATTRIB(pdef->offAttrib))
		{
			s_fSetBasicDuration = pmod->fDuration;
			s_fSetBasicMagnitude = pmod->fMagnitude;
		}
	}


	// Stuff we do if this mod isn't of type None
	if(pdef->eType!=kModType_None)
	{
		CombatTrackerFlag eFlags = 0;
		int bDuration = pdef->eType&kModType_Duration && !pdef->bForever;
		int bMagnitude = pdef->eType&kModType_Magnitude;
		
		// Apply table
		PERFINFO_AUTO_START("Table",1);
		{
			F32 fTable = 1.f;

			// If we have an actual table, use that instead
			if(pdef->pchTableDefault)
			{
				fTable = class_powertable_LookupMulti(papp->pclass,pdef->pchTableDefault,iLevelTable-1,papp->iIdxMulti);
				fTable *= papp->fTableScale;
			}

			// Table value is a direct scale
			if(bDuration)
			{
				pmod->fDuration *= fTable;
			}

			if(bMagnitude)
			{
				pmod->fMagnitude *= fTable;
			}
		}
		PERFINFO_AUTO_STOP();

		// Get the strength and apply it
		PERFINFO_AUTO_START("Strength",1);

		// Check for Strengths in the Application
		if(papp->ppStrengths)
		{
			int i,s=eaSize(&papp->ppStrengths);
			for(i=0; i<s; i++)
			{
				if(pdef->pPowerDef == GET_REF(papp->ppStrengths[i]->hdef))
				{
					PowerApplyStrength *pStrength = papp->ppStrengths[i];
					int iIdx = (int)pdef->uiDefIdx;
					fStr = 1; // Default to 1 since we found something, the others default to 0 already
					if(iIdx < eafSize(&pStrength->pfModStrengths))
						fStr = pStrength->pfModStrengths[pdef->uiDefIdx];
					if(iIdx < eafSize(&pStrength->pfModStrAdd))
						fStrAdd = pStrength->pfModStrAdd[pdef->uiDefIdx];
					if(iIdx < eaiSize(&pStrength->piModFlags))
						eFlags = pStrength->piModFlags[pdef->uiDefIdx];
					break;
				}
			}
		}

		// todo(RP): investigate if we can skip computing strength if the mod is not (kModType_Duration or kModType_Magnitude)
		if(!fStr)
		{
			// We use the level of the Application's root Power here, because we're calculating Strengths.
			//  The level is only used for the tables on inline enhancements, which are
			//    based on the root Power, even if this is a proc on an Enhancement
			//    outside the scope of level-adjusting
			S32 iLevelInline = POWERLEVEL(papp->ppow,papp->iLevel);

			// The strength is based on:
			//  the source Character
			//  the non-Enhancement PowerDef being applied
			//    if this is a proc from an Enhancement, we want the proc to act as if it
			//    is part of original PowerDef for the purposes of inline Enhancement mods
			//    Strength mods that have Affects expressions based on the PowerDef
			//  the Enhancements available
			//  the target (for Personal AttribMods)
			fStr = moddef_GetStrength(	iPartitionIdx, pdef, papp->pcharSource, papp->pdef, 
										iLevelTable, iLevelInline, papp->fTableScale, ppEnhancements,
										erTargetMod, NULL, false, &eFlags, &fStrAdd);
		}
#if GAMESERVER 
		else if (g_CombatConfig.pCombatAdvantage)
		{
			// strengths were in the application, but we still need to check combat bonus from combatAdvantage
			F32 fCombatAdvantageStrBonus = gslCombatAdvantage_GetStrengthBonus(iPartitionIdx, papp->pcharSource, erTargetMod, pdef->offAttrib);
			if (fCombatAdvantageStrBonus > 0.f)
			{
				eFlags |= kCombatTrackerFlag_Flank;
				fStr *= 1.0f + fCombatAdvantageStrBonus;
			}
		}
#endif
		if (fStrAdd > 0.f)
		{
			// Apply the strength add before we apply the final strength
			if(bDuration)
			{
				pmod->fDuration += fStrAdd;
			}
			if(bMagnitude)
			{
				pmod->fMagnitude += fStrAdd;
			}
		}

		if(fStr != 1.0f)
		{
			// Apply the strength
			if(bDuration)
			{
				pmod->fDuration *= fStr;
			}

			if(bMagnitude)
			{
				pmod->fMagnitude *= fStr;
			}
		}
		PERFINFO_AUTO_STOP();

		// Apply variance.. duration and magnitude can vary independently
		if(s_bPowersVariance && pdef->fVariance > 0.0f)
		{
			if(bDuration)
			{
				pmod->fDuration *= mod_VarianceAdjustment(pdef->fVariance);
			}

			if(bMagnitude)
			{
				pmod->fMagnitude *= mod_VarianceAdjustment(pdef->fVariance);
			}
		}

		// Apply combat mods
		//  TODO(JW): I think this needs some review, is target the target of the apply or the mod?
		PERFINFO_AUTO_START("CombatMod",1);
		if(!papp->bLevelAdjusting && !pcharTargetApp->bLevelAdjusting)
		{
			Entity *eOwner = entFromEntityRef(iPartitionIdx,papp->erModOwner);
			if(eOwner && eOwner->erOwner && entFromEntityRef(iPartitionIdx,eOwner->erOwner))
			{
				eOwner = entFromEntityRef(iPartitionIdx,eOwner->erOwner);
			}

			if(eOwner && eOwner->pChar && !CombatMod_ignore(iPartitionIdx,eOwner->pChar,pcharTargetApp))
			{
				int iSourceLevel = papp->iLevel;
				int iTargetLevel = entity_GetCombatLevel(pcharTargetApp->pEntParent);

				if(bDuration && CombatMod_DoesAffectDuration(pdef->offAttrib))
				{
					F32 fDurationMod = CombatMod_getDuration( CombatMod_getMod(iSourceLevel,iTargetLevel,entGetType(eOwner) == GLOBALTYPE_ENTITYPLAYER) );
					pmod->fDuration *= mod_SensitivityAdjustment(fDurationMod,moddef_GetSensitivity(pmod->pDef,kSensitivityType_CombatMod));
				}

				if(bMagnitude && CombatMod_DoesAffectMagnitude(pdef->offAttrib))
				{
					F32 fMagnitudeMod = CombatMod_getMagnitude(CombatMod_getMod(iSourceLevel,iTargetLevel,entGetType(eOwner) == GLOBALTYPE_ENTITYPLAYER));
					pmod->fMagnitude *= mod_SensitivityAdjustment(fMagnitudeMod,moddef_GetSensitivity(pmod->pDef,kSensitivityType_CombatMod));
				}
			}
		}
		PERFINFO_AUTO_STOP();
				
		// If the app wasn't a critical, clear the critical flag
		if(!papp->critical.bSuccess && !papp->ppStrengths)
		{
			eFlags &= ~kCombatTrackerFlag_Critical;
		}
		
		pmod->eFlags = eFlags;
	}

	if (pdef->bUIShowSpecial)
	{
		pmod->eFlags |= kCombatTrackerFlag_ShowSpecial;
	}

	// Make sure magnitude and duration are legal values
	if(!verify(FINITE(pmod->fMagnitude)))
	{
		Errorf("Invalid AttribMod magnitude for %s %d",pdef->pPowerDef->pchName,pdef->uiDefIdx);
		pmod->fMagnitude = 0;
	}
	if(!verify(FINITE(pmod->fDuration)))
	{
		Errorf("Invalid AttribMod duration for %s %d",pdef->pPowerDef->pchName,pdef->uiDefIdx);
		pmod->fDuration = 0;
	}

	// Save the original values
	pmod->fDurationOriginal = pmod->fDuration;
	pmod->fMagnitudeOriginal = pmod->fMagnitude;

	// Fragility
	if(pdef->pFragility)
	{
		PERFINFO_AUTO_START("Health",1);
		pmod->pFragility = StructAlloc(parse_ModFragility);
		if(pdef->pFragility->bMagnitudeIsHealth)
		{
			pmod->pFragility->fHealthOriginal = pmod->fMagnitude;
		}
		else
		{
			pmod->pFragility->fHealthOriginal = combateval_EvalNew(iPartitionIdx, pdef->pFragility->pExprHealth,kCombatEvalContext_Apply,NULL);
			if(pdef->pFragility->cpchTableHealth)
			{
				F32 fTable = class_powertable_LookupMulti(papp->pclass,pdef->pFragility->cpchTableHealth,iLevelTable-1,papp->iIdxMulti);
				pmod->pFragility->fHealthOriginal *= fTable;
			}
		}

		if(pmod->pFragility->fHealthOriginal<=0)
		{
			// Making something fragile with 0 or less max health makes no sense, so we'll error and make it not fragile.
			ErrorDetailsf("Source %s, LevelTable %d, Health %f, Strength %f",CHARDEBUGNAME(papp->pcharSource), iLevelTable, pmod->pFragility->fHealthOriginal, fStr);
			ErrorFilenameDeferredf(pdef->pPowerDef->pchFile,"Tried to create a fragile attribmod with <= 0 maximum health");
			StructDestroySafe(parse_ModFragility,&pmod->pFragility);
		}
		else
		{
			pmod->pFragility->fHealthMax = pmod->pFragility->fHealthOriginal;
			pmod->pFragility->fHealth = combatpool_Init(&pdef->pFragility->pool,0,pmod->pFragility->fHealthMax,0);
		}
		
		PERFINFO_AUTO_STOP();
	}



	// Custom expressions for various attribs
	if(pdef->offAttrib==kAttribType_EntCreate
		|| pdef->offAttrib==kAttribType_EntCreateVanity
		|| pdef->offAttrib==kAttribType_ApplyObjectDeath
		|| pdef->offAttrib==kAttribType_Teleport
		|| pdef->offAttrib==kAttribType_Shield
		|| pdef->offAttrib==kAttribType_PVPSpecialAction
		|| pdef->offAttrib==kAttribType_ProjectileCreate
		|| pdef->offAttrib==kAttribType_DynamicAttrib)
	{
		mod_Fill_Params(iPartitionIdx, pmod, pdef, papp, pcharTargetApp ? entGetRef(pcharTargetApp->pEntParent) : 0);
	}

	PERFINFO_AUTO_STOP();
}

// Fetches an AttribModDef's sensitivity to a specific type
F32 moddef_GetSensitivity(SA_PARAM_NN_VALID AttribModDef *pmoddef, SensitivityType eType)
{
	int i;
	for(i=eaiSize(&pmoddef->piSensitivities)-1; i>=0; i--)
	{
		SensitivityMod *pSensitivity = g_SensitivityMods.ppSensitivities[pmoddef->piSensitivities[i]];
		if(pSensitivity->eType==eType)
			return pSensitivity->fValue;
		else if(pSensitivity->eAltType==eType)
			return pSensitivity->fAltValue;
	}
	return 1;
}



// Determines the effectiveness of a modifier on an attrib, based on the attrib's sensitivity
//  Used for both strength and resistance modifications
//  Assumes fSensitivity > 0.0f, fMag > 0.0f, and fMag < 1 is a penalty
F32 mod_SensitivityAdjustment(F32 fMag, F32 fSensitivity)
{
	// TODO(JW): Optimize
	if(fMag==1.0f)
	{
		return 1.0f;
	}
	else
	{
		bool bCrappy = false;

		if(fMag<1.0f)
		{
			fMag = 1.0f/fMag;
			bCrappy = true;
		}

		fMag -= 1.0f;
		fMag *= fSensitivity;
		fMag += 1.0f;

		if(bCrappy)
		{
			fMag = 1.0f/fMag;
		}

		return fMag;
	}
}

// Determines how much to scale an attrib, based on the attrib's variance.
//  Used for both strength and resistance modifications if the mod is of
//  that type.  Assumes fVariance is (0.0f..1.0f]
F32 mod_VarianceAdjustment(F32 fVariance)
{
	F32 fRand = randomF32();
	fRand *= fVariance;
	fRand += 1.0f;
	return fRand;
}

static void ModDefCheckSaveApplyStrength(SA_PARAM_NN_VALID AttribModDef *pdef,
										 SA_PARAM_NN_VALID PowerDef ***pppPowerDefsToSave)
{
	if(pdef->pExpiration)
	{
		PowerDef *ppowdef = GET_REF(pdef->pExpiration->hDef);
		if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
	}

	if(pdef->offAttrib==kAttribType_ApplyPower)
	{
		// Save the strength of the PowerDef that the ApplyPower applies
		ApplyPowerParams *pParams = (ApplyPowerParams*)pdef->pParams;
		if(pParams)
		{
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
		}
	}
	else if(pdef->offAttrib==kAttribType_DamageTrigger)
	{
		// Save the strength of the PowerDef that the DamageTrigger applies
		DamageTriggerParams *pParams = (DamageTriggerParams*)pdef->pParams;
		if(pParams)
		{
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
		}
	}
	else if(pdef->offAttrib==kAttribType_EntCreate)
	{
		// Save the strength of all the PowerDef that the Critter can own if it's
		//  set to Locked
		EntCreateParams *pParams = (EntCreateParams*)pdef->pParams;
		if(pParams && pParams->eStrength==kEntCreateStrength_Locked)
		{
			CritterDef *pdefCritter = GET_REF(pParams->hCritter);
			if(pdefCritter)
			{
				int i;
				for(i=eaSize(&pdefCritter->ppPowerConfigs)-1; i>=0; i--)
				{
					PowerDef *ppowdef = GET_REF(pdefCritter->ppPowerConfigs[i]->hPower);
					// Don't save Innates or Enhancements that don't have procs
					if(ppowdef
						&& ppowdef->eType!=kPowerType_Innate
						&& !(ppowdef->eType==kPowerType_Enhancement && !ppowdef->bEnhancementExtension))
					{
						eaPushUnique(pppPowerDefsToSave,ppowdef);
					}
				}
			}
		}
	}
	else if(pdef->offAttrib==kAttribType_KillTrigger)
	{
		// Save the strength of the PowerDef that the KillTrigger applies
		KillTriggerParams *pParams = (KillTriggerParams*)pdef->pParams;
		if(pParams)
		{
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
		}
	}
	else if(pdef->offAttrib==kAttribType_TeleThrow)
	{
		// Save the strength of the PowerDef that the TeleThrow applies
		TeleThrowParams *pParams = (TeleThrowParams*)pdef->pParams;
		if(pParams)
		{
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
			ppowdef = GET_REF(pParams->hDefFallback);
			if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
		}
	}
	else if(pdef->offAttrib==kAttribType_TriggerComplex)
	{
		// Save the strength of the PowerDef that the TriggerComplex applies
		TriggerComplexParams *pParams = (TriggerComplexParams*)pdef->pParams;
		if(pParams)
		{
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
		}
	}
	else if(pdef->offAttrib==kAttribType_TriggerSimple)
	{
		// Save the strength of the PowerDef that the TriggerSimple applies
		TriggerSimpleParams *pParams = (TriggerSimpleParams*)pdef->pParams;
		if(pParams)
		{
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			if(ppowdef) eaPushUnique(pppPowerDefsToSave,ppowdef);
		}
	}
}

// Fills the earray with a list of the PowerDefs the AttribModDef may cause to be applied
void moddef_GetAppliedPowerDefs(AttribModDef *pdef,
								PowerDef ***pppPowerDefs)
{
	// Current just calls ModDefCheckSaveApplyStrength(), since
	//  that's effectively the same thing, but there may be a
	//  point at which the concepts fork, so I'll just leave
	//  this as a wrapper.
	ModDefCheckSaveApplyStrength(pdef,pppPowerDefs);
}

static void PowerDefSaveApplyStrength(int iPartitionIdx,
									  SA_PARAM_NN_VALID PowerDef *pdef,
									  SA_PARAM_NN_VALID AttribMod *pmod,
									  SA_PARAM_NN_VALID PowerApplication *papp,
									  S32 iLevelMain,
									  S32 iLevelInline,
									  EntityRef erTargetMod,
									  SA_PARAM_OP_VALID Power **ppEnhancements)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	// Copy in strengths from the Application
	if(papp->ppStrengths && !pmod->ppApplyStrengths)
	{
		eaCopyStructs(&papp->ppStrengths,&pmod->ppApplyStrengths,parse_PowerApplyStrength);
	}

	// See if we've already built strengths for it
	for(i=eaSize(&pmod->ppApplyStrengths)-1; i>=0; i--)
	{
		if(pdef == GET_REF(pmod->ppApplyStrengths[i]->hdef))
			break;
	}

	// If we haven't...
	if(i<0)
	{
		PowerDef **ppPowerDefRecurse = NULL;
		int s = eaSize(&pdef->ppOrderedMods);
		S32 bStrengths = false, bStrAdds = false, bFlags = false;
		PowerApplyStrength *pStrength = MP_ALLOC(PowerApplyStrength);
		SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdef,pStrength->hdef);
		pStrength->uiVersion = pdef->uiVersion;

		if(pdef->bModsIgnoreStrength)
		{
			// Just do the recursive stuff, don't need to bother with the strength calculations
			// Would be nice if we didn't have to push this onto the list at all, but that'd be
			//  a bigger change.
			for(i=0; i<s; i++)
			{
				AttribModDef *pmoddef = pdef->ppOrderedMods[i];
				if(pmoddef->bSaveApplyStrengths)
					ModDefCheckSaveApplyStrength(pmoddef,&ppPowerDefRecurse);
			}
		}
		else
		{
			eafSetSize(&pStrength->pfModStrengths,s);
			eafSetSize(&pStrength->pfModStrAdd,s);
			eaiSetSize(&pStrength->piModFlags,s);
			for(i=0; i<s; i++)
			{
				// Find and save the strength, check if we need to recurse
				// The strength is based on:
				//  the source Character
				//  the PowerDef that will be applied
				//  the Enhancements available
				//  the target (for Personal AttribMods)
				AttribModDef *pmoddef = pdef->ppOrderedMods[i];
				F32 fStrAdd = 0.f;
				CombatTrackerFlag eFlags = 0;
				F32 fStrength = moddef_GetStrength(	iPartitionIdx, pmoddef, papp->pcharSource, pdef, 
													iLevelMain, iLevelInline, papp->fTableScale, ppEnhancements, 
													erTargetMod, NULL, false, &eFlags, &fStrAdd);
				pStrength->pfModStrengths[i] = fStrength;
				pStrength->pfModStrAdd[i] = fStrAdd;
				pStrength->piModFlags[i] = eFlags;

				bStrengths |= (fStrength != 1);
				bStrAdds |= (fStrAdd != 0);
				bFlags |= (eFlags != 0);

				if(pmoddef->bSaveApplyStrengths)
					ModDefCheckSaveApplyStrength(pmoddef,&ppPowerDefRecurse);
			}

			// Clear the arrays if they're full of defaults
			if(!bStrengths)
				eafDestroy(&pStrength->pfModStrengths);
			if(!bStrAdds)
				eafDestroy(&pStrength->pfModStrAdd);
			if(!bFlags)
				eaiDestroy(&pStrength->piModFlags);
		}

		// Save the strength to the mod
		eaPush(&pmod->ppApplyStrengths,pStrength);

		// Recurse
		s = eaSize(&ppPowerDefRecurse);
		for(i=0; i<s; i++)
		{
			PowerDefSaveApplyStrength(iPartitionIdx,ppPowerDefRecurse[i],pmod,papp,iLevelMain,iLevelInline,erTargetMod,ppEnhancements);
		}
		eaDestroy(&ppPowerDefRecurse);
	}

	PERFINFO_AUTO_STOP();
}

static void ModApply(int iPartitionIdx,
						   SA_PARAM_NN_VALID AttribModDef *pdef,
						   S32 iLevelTable,
						   SA_PARAM_NN_VALID PowerApplication *papp,
						   SA_PARAM_NN_VALID Character *pcharTarget,
						   ModTarget eTargetsAllowed,
						   S32 bMiss,
						   SA_PARAM_OP_VALID Power **ppEnhancements,
						   F32 fHitDelay,
						   F32 fCheckDelay,
						   SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsAppliedOut,
						   SA_PARAM_OP_VALID ExprContext **ppOutContext,
						   SA_PARAM_OP_VALID AttribMod*** peaLinkedModsOut)
{
	// Set by any AttribSet AttribModDef when it passes through, and
	//  any internally derived AttribModDefs just check this value to see
	//  if they are legal.
	// For this to work correctly AttribMods have to be created in order, with the derived mods
	//  directly following the AttribSet mod.
	static S32 s_bAttribSetTest = false;

	AttribMod *pmod = NULL;
	Character *pcharTargetMod;
	EntityRef erTargetMod;

	if(IS_SET_ATTRIB(pdef->offAttrib))
	{
		ADD_MISC_COUNT(1,"Set");
		s_bAttribSetTest = false;
	}
	else if(pdef->bDerivedInternally)
	{
		if(!s_bAttribSetTest)
		{
			ADD_MISC_COUNT(1,"Derived Set Failed");
			return;
		}
		ADD_MISC_COUNT(1,"Derived");
	}
	else
	{
		ADD_MISC_COUNT(1,"Normal");
	}

	pcharTargetMod = (pdef->eTarget==kModTarget_Target) ? pcharTarget : papp->pcharSource;

	if(!pdef->bDerivedInternally)
	{
		// If we don't actually have a valid target
		if(!pcharTargetMod)
			return;

		// If this attrib isn't of an allowed target type (usually either Target|Self or SelfOnce)
		if(!(pdef->eTarget & eTargetsAllowed))
			return;

		// If this is a Miss and the HitTest is Hit, or this isn't a Miss and the HitTest is Miss
		if((bMiss && pdef->eHitTest==kModHitTest_Hit) || (!bMiss && pdef->eHitTest==kModHitTest_Miss))
			return;

		// If this is Personal, and there's no source, or the source is also the target, it's not legal
		if(pdef->bPersonal
			&& (!papp->pcharSource
				|| pcharTarget==papp->pcharSource))
		{
			return;
		}
	}

	PERFINFO_AUTO_START_FUNC();

	if(!pdef->bDerivedInternally)
	{
		// If this is a non-periodic mod with a chance, and it fails that chance, skip it
		if(pdef->fPeriod==0.0f)
		{
			F32 fChance = 1.0f;
			if (pdef->pExprChance)
			{
				combateval_ContextSetupExpiration(papp->pcharSource,pmod,pdef,pdef->pPowerDef);
				fChance = combateval_EvalNew(iPartitionIdx,pdef->pExprChance,kCombatEvalContext_Apply,NULL);
			}

			// If it's normalized, normalize to 1s of activation
			if(pdef->bChanceNormalized)
			{
				F32 fTime = 0;
				if(papp->pact)
				{
					if(papp->pact->uiPeriod)
						fTime = papp->pdef->fTimeActivatePeriod;
					else
						fTime = powerapp_GetTotalTime(papp);
				}
				fChance *= fTime;
			}

			if(fChance < 1.f && fChance<randomPositiveF32())
			{
				PERFINFO_AUTO_STOP();
				return;
			}
		}

		// If this mod fails its requirements, skip it
		if(pdef->pExprRequires)
		{
			PERFINFO_AUTO_START("EvalRequires",1);
			if(!combateval_EvalNew(iPartitionIdx, pdef->pExprRequires,kCombatEvalContext_Apply,NULL))
			{
				PERFINFO_AUTO_STOP(); // EvalRequires
				PERFINFO_AUTO_STOP();
				return;
			}
			PERFINFO_AUTO_STOP(); // EvalRequires
		}
	}

	// If this is actually an include of an Enhancement PowerDef, run through that and just return
	if(pdef->offAttrib==kAttribType_IncludeEnhancement)
	{
		IncludeEnhancementParams *pParams = (IncludeEnhancementParams*)pdef->pParams;
		PowerDef *pdefInclude = pParams ? GET_REF(pParams->hDef) : NULL;
		if(pdefInclude)
		{
			int i, iNumMods = eaSize(&pdefInclude->ppOrderedMods);
			for(i=0; i<iNumMods; i++)
			{
				AttribModDef *pmoddefInclude = pdefInclude->ppOrderedMods[i];
				if(pmoddefInclude->bEnhancementExtension)
				{
					ModApply(iPartitionIdx,
						pmoddefInclude,
						iLevelTable,
						papp,
						pcharTarget,
						eTargetsAllowed,
						bMiss,
						ppEnhancements,
						fHitDelay,
						fCheckDelay,
						peFlagsAppliedOut,
						ppOutContext,
						peaLinkedModsOut);
				}
			}
		}
		PERFINFO_AUTO_STOP();
		return;
	}

	// Note that the AttribSet AttribMod was created OK, so the derived
	//  AttribMods that follow should be OK as well.
	if(IS_SET_ATTRIB(pdef->offAttrib))
		s_bAttribSetTest = true;

	// LevelAdjusting
	// Self-targeted AttribMods are exempt from level adjusting as they shouldn't
	//  care what level the target is (they're not affecting the target).
	// EntCreate is specifically exempted from level adjusting at the AttribMod level.
	//  Instead the created Entity is marked as bLevelAdjusting if its owner is.
	if(pdef->eTarget==kModTarget_Target && pdef->offAttrib!=kAttribType_EntCreate)
	{
		WorldRegionType eTargetRegion;
		if(pcharTarget && pcharTarget->pEntParent){
			eTargetRegion = entGetWorldRegionTypeOfEnt(pcharTarget->pEntParent);
		}
		else{
			eTargetRegion = WRT_None;
		}

		if(papp->bLevelAdjusting)
		{
			// If the Application is level adjusting (which means the source is level
			//  adjusting), the level is set to the level of the target
#ifdef GAMESERVER
			// TODO_PARTITION: Difficulty
			PlayerDifficultyMapData *pdiff = pd_GetDifficultyMapData(mapState_GetDifficulty(NULL), zmapInfoGetPublicName(NULL), eTargetRegion);
			iLevelTable = entity_GetCombatLevel(pcharTarget->pEntParent) + (pdiff ? pdiff->iLevelModifier : 0);
#else
			iLevelTable = entity_GetCombatLevel(pcharTarget->pEntParent);
#endif
		}
		else if(pcharTarget->bLevelAdjusting)
		{
			// If the source isn't level adjusting, but the target is, AND the target is
			//  lower level than the input level, we use the level of the target.  This
			//  means high levels can't bottom feed off low level adjusters, while low
			//  levels don't trivially combat a high level adjuster
#ifdef GAMESERVER
			PlayerDifficultyMapData *pdiff = pd_GetDifficultyMapData(mapState_GetDifficulty(mapState_FromPartitionIdx(iPartitionIdx)), zmapInfoGetPublicName(NULL), eTargetRegion);
			MIN1(iLevelTable,entity_GetCombatLevel(pcharTarget->pEntParent) - (pdiff ? pdiff->iLevelModifier : 0));
#else
			MIN1(iLevelTable,entity_GetCombatLevel(pcharTarget->pEntParent));
#endif
		}
		MAX1(iLevelTable, 1);
	}

	// Now that we've got our FINAL level for the purposes of this AttribMod, set it
	//  in the PowerApplication so it can be used elsewhere.
	papp->iLevelMod = iLevelTable;

	pmod = MP_ALLOC(AttribMod);

	erTargetMod = entGetRef(pcharTargetMod->pEntParent);

	ModFill(iPartitionIdx,pmod,pdef,iLevelTable,papp,erTargetMod,pcharTarget,ppEnhancements,ppOutContext);

	// Fill in values that don't need to be passed around
	pmod->fCheckTimer = fCheckDelay;
	pmod->fTimer = pdef->fDelay + fHitDelay;
	pmod->uiApplyID = papp->uiApplyID;
	pmod->uiActIDServer = papp->pact ? papp->pact->uiIDServer : 0;

	if (papp->ppow)
		power_GetIDAndSubIdx(papp->ppow, &pmod->uiPowerID, &pmod->iPowerIDSub, &pmod->iPowerIDLinkedSub);
	else
		pmod->uiPowerID = 0;

	pmod->uiRandomSeedActivation = papp->uiRandomSeedActivation;
	pmod->uiTimestamp = papp->uiTimestampAnim;
	pmod->fAnimFXDelay = pdef->fDelay + ((pdef->eTarget==kModTarget_Target) ? fHitDelay : 0);
	pmod->erOwner = papp->erModOwner;
	pmod->erSource = papp->erModSource;
	pmod->bProcessOfflineTimeOnLogin = pdef->bProcessOfflineTimeOnLogin;
	
	// check to see if the mod should be considered applied already for the immune/resist/shield only affecting first tick 
	if (papp->bCountModsAsPostApplied && papp->erTarget == entGetRef(pcharTarget->pEntParent))
	{
		pmod->bPostFirstTickApply = papp->bCountModsAsPostApplied;
	}


	if(pmod->erSource) 
	{
		pmod->bCheckSource = true;

		// don't check source for projectiles, they will sometimes die before the mods are applied, 
		// and we don't want them checked
		if(papp->pcharSource && 
			papp->pcharSource->pEntParent && 
			entCheckFlag(papp->pcharSource->pEntParent,ENTITYFLAG_PROJECTILE))
		{
			pmod->bCheckSource = false;
		}
	}
	if(pdef->bPersonal)
	{
		Entity *entPersonal = pcharTargetMod==pcharTarget ? papp->pcharSource->pEntParent : pcharTarget->pEntParent;
		pmod->erPersonal = entGetRef(entPersonal);
	}
	if(papp->pact && papp->pact->eLungeMode!=kLungeMode_None)
	{
		pmod->bCheckLunge = true;
	}

	if(!papp->bPrimaryTarget && papp->pdef->eEffectArea==kEffectArea_Sphere && papp->pdef->fRange>0)
	{
		// On ranged spheres, the secondary targets see the vecSource as the effective target, rather than the source position
		copyVec3(papp->vecTargetEff,pmod->vecSource);
	}
	else
	{
		copyVec3(papp->vecSourcePos,pmod->vecSource);
	}

	// Copy the subtarget if there is one and this does damage
	if(papp->pSubtarget && IS_DAMAGE_ATTRIBASPECT(pdef->offAttrib,pdef->offAspect))
	{
		pmod->pSubtarget = StructClone(parse_PowerSubtargetChoice,papp->pSubtarget);
	}

	// Save off the PowerDef apply strengths that may result from this AttribMod
	if(pdef->bSaveApplyStrengths)
	{
		int i,s;
		// We use the level of the Application's root Power here, because we're calculating Strengths.
		//  The level is only used for the tables on inline enhancements, which are
		//    based on the root Power, even if this is a proc on an Enhancement
		//    outside the scope of level-adjusting
		S32 iLevelInline = POWERLEVEL(papp->ppow,papp->iLevel);
		static PowerDef **s_ppPowerDefsToSave = NULL;

		PERFINFO_AUTO_START("SaveApplyStrengths", 1);
		
		eaClearFast(&s_ppPowerDefsToSave);
		ModDefCheckSaveApplyStrength(pdef,&s_ppPowerDefsToSave);
		s = eaSize(&s_ppPowerDefsToSave);
		for(i=0; i<s; i++)
		{
			PowerDefSaveApplyStrength(iPartitionIdx,s_ppPowerDefsToSave[i],pmod,papp,iLevelTable,iLevelInline,erTargetMod,ppEnhancements);
		}

		// Copy the subtarget if we haven't already, there is one, and this applies Powers
		if(s && !pmod->pSubtarget && papp->pSubtarget)
		{
			pmod->pSubtarget = StructClone(parse_PowerSubtargetChoice,papp->pSubtarget);
		}

		PERFINFO_AUTO_STOP();
	}

	if(pdef->bSaveSourceDetails)
	{
		PERFINFO_AUTO_START("SaveSourceDetails",1);
		{
			int i;
			pmod->pSourceDetails = MP_ALLOC(AttribModSourceDetails);
			if(papp->pclass)
			{
				SET_HANDLE_FROM_REFDATA(g_hCharacterClassDict,papp->pclass->pchName,pmod->pSourceDetails->hClass);
			}
			pmod->pSourceDetails->iLevel = iLevelTable;
			pmod->pSourceDetails->iIdxMulti = papp->iIdxMulti;
			pmod->pSourceDetails->fTableScale = papp->fTableScale;
			for(i=eaSize(&papp->pppowEnhancements)-1; i>=0; i--)
			{
				Power *ppow = papp->pppowEnhancements[i];
				PowerClone *pEnh = StructAlloc(parse_PowerClone);
				COPY_HANDLE(pEnh->hdef,ppow->hDef);
				pEnh->iLevel = POWERLEVEL(ppow,papp->iLevel);
				pEnh->fTableScale = ppow->fTableScale;
				eaPush(&pmod->pSourceDetails->ppEnhancements,pEnh);
			}
			pmod->pSourceDetails->pCritical = StructAlloc(parse_PACritical);
			StructCopyFields(parse_PACritical,&papp->critical,pmod->pSourceDetails->pCritical,0,0);
			pmod->pSourceDetails->erTargetApplication = papp->pentTargetEff ? entGetRef(papp->pentTargetEff) : 0;
			copyVec3(papp->vecTargetEff,pmod->pSourceDetails->vecTargetApplication);
			pmod->pSourceDetails->iItemID = papp->iSrcItemID;
			pmod->pSourceDetails->bLevelAdjusting = papp->bLevelAdjusting;
		}
		PERFINFO_AUTO_STOP();
	}

	if(pdef->bSaveHue)
	{
		pmod->fHue = papp->fHue;
		if(papp->ppafxEnhancements)
		{
			PowerAnimFX *pafx = GET_REF(pdef->pPowerDef->hFX);
			if(pafx!=papp->pafx)
			{
				int i;
				for(i=eaSize(&papp->ppafxEnhancements)-1; i>=0; i--)
				{
					if(pafx==papp->ppafxEnhancements[i]->pafx)
					{
						pmod->fHue = papp->ppafxEnhancements[i]->fHue;
						break;
					}
				}
			}
		}
	}

	if(papp->avoidance.bSuccess)
	{
		F32 fSensitivity;

		if(g_CombatConfig.pAvoidChance
			&& g_CombatConfig.pAvoidChance->bDamageOnly
			&& !IS_DAMAGE_ATTRIBASPECT(pdef->offAttrib,pdef->offAspect))
		{
			fSensitivity = 0;
		}
		else
		{
			fSensitivity = moddef_GetSensitivity(pdef,kSensitivityType_Avoidance);
		}

		pmod->fAvoidance = fSensitivity * papp->avoidance.fSeverity;
		if(pmod->fAvoidance)
		{
			// Default flag for avoidance is Dodge
			if(g_CombatConfig.pAvoidChance && g_CombatConfig.pAvoidChance->eFlag)
				pmod->eFlags |= g_CombatConfig.pAvoidChance->eFlag;
			else
				pmod->eFlags |= kCombatTrackerFlag_Dodge;
		}
	}


	if(papp->bPredict)
	{
		pmod->fPredictionOffset = ccbPredictAttribModDef(pdef);
	}
	else if (pdef->offAttrib == kAttribType_Root || pdef->offAttrib == kAttribType_Hold)
	{
		pmod->fPredictionOffset = 0.2f;
	}


	pmod->bNew = true;
	
	if(pmod->eFlags && peFlagsAppliedOut)
		(*peFlagsAppliedOut) |= pmod->eFlags;

	// Put it in the pending array
#define MAX_ATTRIBMODS_PENDING 1000
	if(eaSize(&pcharTargetMod->modArray.ppModsPending) >= MAX_ATTRIBMODS_PENDING)
	{
		// Detail what was trying to overfill
		ErrorDetailsf("%s\nTrying to add an AttribMod from PowerDef %s",ENTDEBUGNAME(pcharTargetMod->pEntParent),REF_STRING_FROM_HANDLE(pmod->hPowerDef));
		Errorf("Too many pending AttribMods on a Character");
		mod_Destroy(pmod);
		PERFINFO_AUTO_STOP();
		return;
	}
	else
	{
		eaPush(&pcharTargetMod->modArray.ppModsPending, pmod);
				
		if (peaLinkedModsOut && (pdef->bAttribLinkToSource || pdef->offAttrib == kAttribType_AttribLink))
		{
			eaPush(peaLinkedModsOut, pmod);
		}

		// Make sure the Character wakes in time to process this
#ifdef GAMESERVER
		if (!gslCombatDeathPrediction_IsEnabled())
		{
			character_SetSleep(pcharTargetMod,pmod->fTimer-pmod->fPredictionOffset);
		}
		else
		{	// death prediction requires that the character wake up immediately to process the pending mod
			// todo: check if I can just process the pending mod here instead of waking up the character
			character_Wake(pcharTargetMod);
		}

		if(characterPhase_GetCurrentPhase() <= ECharacterPhase_ONE)
		{
			// If the Character is not in a tick we have to adjust the timers to take into account the time the
			//  Character has already slept. That way when the Character wakes to runs its next tick, where
			//  it will add the time slept to the base tick time, the mods don't fire early.
			pmod->fTimer += pcharTargetMod->fTimeSlept;
			pmod->fCheckTimer += pcharTargetMod->fTimeSlept;
		}
#endif
		


		if(eaSize(&pcharTargetMod->modArray.ppModsPending) == MAX_ATTRIBMODS_PENDING)
		{
			// Detail what is in the list currently
			char *pchDetails = NULL;
			estrCreate(&pchDetails);
			estrConcatf(&pchDetails,"SecondsSince2000 %d\n",timeSecondsSince2000());
			AttribModArrayToEString(pcharTargetMod->modArray.ppModsPending,&pchDetails);
			ErrorDetailsf("%s\n%s",ENTDEBUGNAME(pcharTargetMod->pEntParent),pchDetails);
			Errorf("Too many pending AttribMods on a Character");
			estrDestroy(&pchDetails);
		}
	}

	PowersDebugPrintEnt(EPowerDebugFlags_APPLY, SAFE_MEMBER(papp->pcharSource, pEntParent), " - Apply %d: Attrib %d attached\n",papp->uiActID,pdef->iKey);

	PERFINFO_AUTO_STOP();
}

void character_ModExpireReason(Character *pchar, AttribMod *pmod, ModExpirationReason fReason)
{
	pmod->fDuration = fReason;

	if (pchar && 
		(pmod->ppPowersCreated || pmod->eaAttribModLinks ||
			(pmod->pDef && (IS_SPECIAL_ATTRIB(pmod->pDef->offAttrib) || pmod->pDef->bHasAnimFX ||
				(pmod->pDef->pExpiration && !pmod->pDef->pExpiration->bPeriodic)))))
	{	// the mod just expired, make sure we process it the next tick so it can do the mod_cancel stuff
		character_Wake(pchar);
	}
}

// Applies the appropriate AttribMods from a PowerDef to a target character.
void character_ApplyModsFromPowerDef(int iPartitionIdx,
									 Character *pcharTarget,
									 PowerApplication *papp,
									 F32 fHitDelay,
									 F32 fCheckDelay,
									 ModTarget eTargetsAllowed,
									 S32 bMiss,
									 CombatTrackerFlag *peFlagsAppliedOut,
									 ExprContext **ppOutContext,
									 S64 *plTimerExcludeAccum)
{
	static Power **s_ppEnhCopy = NULL;
	static AttribMod** s_ppLinkedMods = NULL;

	PERFINFO_AUTO_START_FUNC();
	{
		int i, j, iNumMods, iLevelTable, iLevelTableMain, iEnhancements = 0;
		PowerDef *pdef = papp->pdef;

		eaClear(&s_ppLinkedMods);

		// Check over the enhancements to make sure they all still apply
		// TODO(JW): Enhancements: Optimize this to make two lists, str-affecting mods and extending mods
		if(eaSize(&papp->pppowEnhancements))
		{
			PERFINFO_AUTO_START("PrepEnhancements",1);
			eaCopy(&s_ppEnhCopy,&papp->pppowEnhancements);
			power_CheckEnhancements(iPartitionIdx,&s_ppEnhCopy);
			iEnhancements = eaSize(&s_ppEnhCopy);
			PERFINFO_AUTO_STOP();
		}

		// Apply the power's defs
		PERFINFO_AUTO_START("ApplyPower",1);
		// This has been switched to the debug version of the POWERLEVEL macro because there's a bug
		//  somewhere which is making it return bad results - once that is fixed it should be switched
		//  back.
		iLevelTable = iLevelTableMain = power_PowerLevelDebug(papp->ppow,papp->iLevel); //POWERLEVEL(papp->ppow,papp->iLevel);
		iNumMods = eaSize(&pdef->ppOrderedMods);
		for(i=0; i<iNumMods; i++)
		{
			if(!pdef->ppOrderedMods[i]->bEnhancementExtension)
			{
				ModApply(iPartitionIdx,
					pdef->ppOrderedMods[i],
					iLevelTable,
					papp,
					pcharTarget,
					eTargetsAllowed,
					bMiss,
					s_ppEnhCopy,
					fHitDelay,
					fCheckDelay,
					peFlagsAppliedOut,
					ppOutContext, 
					&s_ppLinkedMods);
			}
		}
		PERFINFO_AUTO_STOP();

		// Apply the enhancements' defs
		if(iEnhancements)
		{
			S64 lTimerExclude = 0;

			// Save original app state
			S32 iIdxMultiApp = papp->iIdxMulti;
			F32 fTableScaleApp = papp->fTableScale;

			PERFINFO_AUTO_START("ApplyEnhancements",1);
			if(plTimerExcludeAccum) GET_CPU_TICKS_64(lTimerExclude);
			for(j=iEnhancements-1; j>=0; j--)
			{
				PowerDef *pdefEnh = GET_REF(s_ppEnhCopy[j]->hDef);

				if(!pdefEnh || !pdefEnh->bEnhancementExtension || (bMiss && !pdefEnh->bMissMods))
					continue;

				// Set app state for this particular enhancement
				papp->iIdxMulti = s_ppEnhCopy[j]->iIdxMultiTable;
				papp->fTableScale = s_ppEnhCopy[j]->fTableScale;

				iLevelTable = pdefEnh->bEnhanceCopyLevel ? iLevelTableMain : POWERLEVEL(s_ppEnhCopy[j],papp->iLevel);
				iNumMods = eaSize(&pdefEnh->ppOrderedMods);
				for(i=0; i<iNumMods; i++)
				{
					if(pdefEnh->ppOrderedMods[i]->bEnhancementExtension)
					{
						// TODO(JW): Enhancements: Review how enhancements should use eachother
						ModApply(iPartitionIdx,
							pdefEnh->ppOrderedMods[i],
							iLevelTable,
							papp,
							pcharTarget,
							eTargetsAllowed,
							bMiss,
							s_ppEnhCopy,
							fHitDelay,
							fCheckDelay,
							peFlagsAppliedOut,
							ppOutContext,
							&s_ppLinkedMods);
					}
				}
			}
			eaClearFast(&s_ppEnhCopy);
			if(lTimerExclude)
			{
				S64 lTimerExcludeClose;
				GET_CPU_TICKS_64(lTimerExcludeClose);
				*plTimerExcludeAccum += (lTimerExcludeClose - lTimerExclude);
			}
			PERFINFO_AUTO_STOP();

			// Restore app state
			papp->iIdxMulti = iIdxMultiApp;
			papp->fTableScale = fTableScaleApp;
		}

		// linked attribs
		if (pcharTarget && eaSize(&s_ppLinkedMods))
			AttribModLink_PostApplyModsFromPowerDef(pdef, s_ppLinkedMods, pcharTarget, papp->pcharSource);
	}
	PERFINFO_AUTO_STOP();
}

static void character_ModsProcessPendingDiscard(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract, 
												SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID AttribMod *pmodPrior, S32 iModIndex)
{
	if(pmodPrior->fDuration >= 0)
	{
		// We throw away new copies if the prior is still active, so get rid of this
		mod_Destroy(pmod);
	}
	else
	{
		// For expired mods, replace them (to keep bits/fx stable), but also run the
		//  expiration, since technically the old mod has died and this is a "new" mod.
		// NOTE: This puts new mods in the pending list, but since this loop is a countdown
		//  they won't be processed until the next tick.
		if(pmod->pDef && pmod->pDef->pExpiration && !pmod->pDef->pExpiration->bPeriodic)
			character_ApplyModExpiration(iPartitionIdx,pchar,pmod,pExtract);
		mod_Replace(iPartitionIdx, pchar, pmod, pmodPrior, iModIndex, pExtract);
	}
}

static void character_ModsProcessPendingReplace(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract, 
												SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID AttribMod *pmodPrior, S32 iModIndex)
{
	F32 fTimer = pmod->pDef->bReplaceKeepsTimer ? pmodPrior->fTimer : pmod->fTimer;

	// Copy the old health percent if necessary
	if(pmod->pDef->pFragility
		&& pmod->pDef->pFragility->bReplaceKeepsHealth
		&& pmod->pFragility
		&& pmodPrior->pFragility
		&& pmodPrior->pFragility->fHealthMax)
	{
		F32 fPercent = pmodPrior->pFragility->fHealth / pmodPrior->pFragility->fHealthMax;
		F32 fTargetHealth = fPercent * pmod->pFragility->fHealthMax;
		mod_FragileAffect(pmod, fTargetHealth - pmod->pFragility->fHealth);
	}

	mod_Replace(iPartitionIdx, pchar, pmod, pmodPrior, iModIndex, pExtract);
	pmod->fTimer = fTimer;
}

// Checks if two mods are assumed to be the same copy for stacking purposes.
// Remarks: Two mods are assumed to be the same copy in terms of stacking purposes only if they have
// the same stack group pending, attrib type, aspect and stack type.
static bool mod_AssumeSameCopy(SA_PARAM_NN_VALID AttribMod *pMod1, SA_PARAM_NN_VALID AttribMod *pMod2)
{
	AttribModDef *pDef1 = pMod1->pDef;
	AttribModDef *pDef2 = pMod2->pDef;

	return pDef1->eStackGroupPending && pDef1->eStackGroupPending == pDef2->eStackGroupPending &&
		pDef1->offAttrib == pDef2->offAttrib && pDef1->offAspect == pDef2->offAspect &&
		pDef1->eStack == pDef2->eStack;
}

// Utility function to check pending mods, make them active if needed, and merge them into
//  the active list.  Also adds fragile delayed mods to the fragile list if appropriate.
//  Should be called before processing the active list.
void character_ModsProcessPending(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	int i,s=eaSize(&pchar->modArray.ppModsPending);

	if(!s)
		return;
	
	PERFINFO_AUTO_START_FUNC();

	// NOTE: This process CAN actually add NEW pending mods to the Character being processed,
	//  but they are added at the end, and the loop always moves from back to front, so any
	//  mods added will NOT be processed this tick (though they can be brought forward due
	//  to eaRemoveFast, but they still won't be processed), which is good, and right, until
	//  someone says otherwise or screws things up.
	for(i=s-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppModsPending[i];
		int bFragile;

		if(pmod->bNew)
		{
			pmod->bNew = false;
			if(pmod->fCheckTimer > 0.0f && pmod->fTimer > 0.0f && pchar->fTimeSlept <= 0)
			{
				// we are waiting on this attrib- make sure we will wake up in time to process this
				character_SetSleep(pchar, pmod->fTimer - pmod->fPredictionOffset);
				continue;
			}
		}

		// Do we want to let this one be damaged even though it's still pending?
		bFragile = pmod->pFragility 
						&& pmod->pDef->offAttrib!=kAttribType_Shield
						&& pmod->pDef->pFragility->bFragileWhileDelayed;

		// If we still need to check validity, check it
		if(pmod->fCheckTimer - pmod->fPredictionOffset > 0.0f)
		{
			// Not allowed to be damaged yet
			bFragile = false;

			if(pmod->bCheckSource)
			{
				Entity *e = entFromEntityRef(iPartitionIdx,pmod->erSource);

				if (!e || !character_CheckSourceActivateRules(e->pChar, pmod->pDef->pPowerDef->eActivateRules))
				{
					// Invalid!  Never managed to become a "real mod", so we can just remove it 
					//  from the pending list and destroy it.
					eaRemoveFast(&pchar->modArray.ppModsPending,i);
					mod_Destroy(pmod);
					continue;
				}
			}

			// Still valid, decrement the check timer
			pmod->fCheckTimer -= fRate;
		}
		
#define MOD_CHECKTIMER_DONE -9999.0f

		// If the check timer hasn't been marked as done, but it's effectively done or the
		//  mod is going to go active this tick, mark it done and do any related work
		if(pmod->fCheckTimer!=MOD_CHECKTIMER_DONE
			&& ((pmod->fCheckTimer - pmod->fPredictionOffset <= 0.0f)
				|| (pmod->fTimer - pmod->fPredictionOffset <= fRate)))
		{
			// Don't need to check validity anymore
			pmod->fCheckTimer = MOD_CHECKTIMER_DONE;

			if(pmod->fAvoidance && g_CombatConfig.pAvoidChance && g_CombatConfig.pAvoidChance->fArc)
			{
				// Hit-time check for AvoidChance arc limitation
				F32 fAngleMod = mod_AngleToSource(pmod,pchar->pEntParent);
				if(fabs(fAngleMod) > RAD(g_CombatConfig.pAvoidChance->fArc)/2.f)
				{
					// Angle to Source is outside the arc, so remove the avoidance and flag
					pmod->fAvoidance = 0;
					if(g_CombatConfig.pAvoidChance->eFlag)
						pmod->eFlags &= ~g_CombatConfig.pAvoidChance->eFlag;
					else
						pmod->eFlags &= ~kCombatTrackerFlag_Dodge;
				}
			}

			// Track timed CombatEvents from flags
			// TODO(JW): The mapping of flag to event is a bit too hardcoded here
			if(pmod->eFlags)
			{
				// Check for Critical flag
				if(pmod->eFlags & kCombatTrackerFlag_Critical)
				{
					S32 iIndex;
					if(!pchar->pCombatEventState)
						pchar->pCombatEventState = combatEventState_Create();
					iIndex = ea32Find(&pchar->pCombatEventState->puiCombatTrackerFlagCriticalIDs,pmod->uiApplyID);
					if(iIndex < 0)
					{
						Entity *eSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
						ea32Push(&pchar->pCombatEventState->puiCombatTrackerFlagCriticalIDs,pmod->uiApplyID);
						character_CombatEventTrackInOut(pchar, kCombatEvent_CriticalInTimed, kCombatEvent_CriticalOutTimed,
														eSource, pmod->pDef->pPowerDef, pmod->pDef, 
														pmod->fMagnitude, 0, NULL, NULL);
					}
				}

				// Check for Dodge flag
				if(pmod->eFlags & kCombatTrackerFlag_Dodge)
				{
					S32 iIndex;
					if(!pchar->pCombatEventState)
						pchar->pCombatEventState = combatEventState_Create();
					iIndex = ea32Find(&pchar->pCombatEventState->puiCombatTrackerFlagDodgeIDs,pmod->uiApplyID);
					if(iIndex < 0)
					{
						Entity *eSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
						ea32Push(&pchar->pCombatEventState->puiCombatTrackerFlagDodgeIDs,pmod->uiApplyID);
						character_CombatEventTrackInOut(pchar, kCombatEvent_DodgeInTimed, kCombatEvent_DodgeOutTimed, 
														eSource, pmod->pDef->pPowerDef, pmod->pDef, 
														pmod->fMagnitude, 0, NULL, NULL);
					}
				}

				// Check for Block flag
				if(pmod->eFlags & kCombatTrackerFlag_Block)
				{
					S32 iIndex;
					if(!pchar->pCombatEventState)
						pchar->pCombatEventState = combatEventState_Create();
					iIndex = ea32Find(&pchar->pCombatEventState->puiCombatTrackerFlagBlockIDs,pmod->uiApplyID);
					if(iIndex < 0)
					{
						ea32Push(&pchar->pCombatEventState->puiCombatTrackerFlagBlockIDs,pmod->uiApplyID);
						if(character_CombatEventTrack(pchar,kCombatEvent_BlockInTimed))
						{
							character_CombatEventTrackComplex(pchar, kCombatEvent_BlockInTimed,
																entFromEntityRef(iPartitionIdx,pmod->erSource),
																pmod->pDef->pPowerDef, NULL, 
																pmod->fMagnitude, 0, NULL);
						}
					}
				}
			}
		}

/*
		if(pmod->bCheckLunge)
		{
			Entity *e = entFromEntityRef(pmod->erSource);
			if(e && e->pChar)
			{
				if(!e->pChar->pPowActCurrent || e->pChar->pPowActCurrent->eLungeMode==kLungeMode_Failure)
				{
					// Invalid!  Never managed to become a "real mod", so we can just remove it 
					//  from the pending list and destroy it.
					eaRemoveFast(&pchar->modArray.ppModsPending,i);
					mod_Destroy(pmod);
					continue;
				}
				else if(e->pChar->pPowActCurrent->eLungeMode==kLungeMode_Success)
				{
					pmod->bCheckLunge = false;
				}
				else
				{
					continue;
				}
			}
		}
*/

		// The mod is still valid, do normal processing
		if(pmod->fTimer - pmod->fPredictionOffset <= fRate)
		{
			AttribModDef *pdef = pmod->pDef;

			if (pmod->pDef->offAttrib == kAttribType_AttribLink)
			{
				S32 bDestroy = false;
				if (!AttribModLink_ProcessPendingGoingCurrent(iPartitionIdx, pmod, pchar, &bDestroy))
				{
					if (bDestroy)
					{
						eaRemoveFast(&pchar->modArray.ppModsPending, i);
						AttribModLink_ModCancel(iPartitionIdx, pmod, pchar, true);
						mod_Destroy(pmod);

						// go through the pending mods and remove the mods that were mark to be expired by AttribModLink_ModCancel
						// they couldn't be destroyed immediately since we are currently looping through them.
						FOR_EACH_IN_EARRAY(pchar->modArray.ppModsPending, AttribMod, pOtherMod)
						{
							if (pOtherMod->fDuration == kModExpirationReason_AttribLinkExpire)
							{
								eaRemoveFast(&pchar->modArray.ppModsPending, FOR_EACH_IDX(-,pOtherMod));
								mod_Destroy(pOtherMod);
								// check if we haven't yet iterated to this mod, if so we need to fixup our current index
								if (FOR_EACH_IDX(-,pOtherMod) < i)
									i--;
							}
						}
						FOR_EACH_END
					}
					continue;
				}
			}

			// Timer will complete this tick, so it's now considered an active mod, remove it from this list and
			//  put it in the real list.
			pmod->fTimer = pdef->bIgnoreFirstTick ? pdef->fPeriod : 0;
			pmod->uiPeriod = 1;

			// Remove it from the pending list
			eaRemoveFast(&pchar->modArray.ppModsPending,i);

#define MAX_MODS_ACTIVE 1000
			if(eaSize(&pchar->modArray.ppMods) >= MAX_MODS_ACTIVE)
			{
				// Detail what was trying to overfill
				ErrorDetailsf("%s\nTrying to add an AttribMod from PowerDef %s",ENTDEBUGNAME(pchar->pEntParent),REF_STRING_FROM_HANDLE(pmod->hPowerDef));
				Errorf("Too many active AttribMods on a Character");
				mod_Destroy(pmod);
				continue;
			}
			else if(eaSize(&pchar->modArray.ppMods) == (MAX_MODS_ACTIVE-1))
			{
				// Detail what is in the list currently
				char *pchDetails = NULL;
				estrCreate(&pchDetails);
				estrConcatf(&pchDetails,"SecondsSince2000 %d\n",timeSecondsSince2000());
				AttribModArrayToEString(pchar->modArray.ppMods,&pchDetails);
				ErrorDetailsf("%s\n%s",ENTDEBUGNAME(pchar->pEntParent),pchDetails);
				Errorf("Too many active AttribMods on a Character");
				estrDestroy(&pchDetails);
			}

			// Check suppression
			if(pdef->piCombatEvents
				&& (pdef->eCombatEventResponse==kCombatEventResponse_CancelIfNew
					|| pdef->eCombatEventResponse==kCombatEventResponse_IgnoreIfNew))
			{
				// Quick check, like the suppress stage, but just for new mods
				if(character_CheckCombatEvents(pchar,pdef->piCombatEvents,pdef->fCombatEventTime))
				{
					// A matching event was found
					if(pdef->eCombatEventResponse==kCombatEventResponse_CancelIfNew)
					{
						mod_Destroy(pmod);
						continue;
					}
					else
					{
						pmod->bIgnored = true;
					}
				}
			}

			pmod->fDuration += pchar->fTimeSlept;

			// Have to check stacking first
			if(pdef->eStack==kStackType_Stack)
			{
				// Stack gets a free pass
				eaPush(&pchar->modArray.ppMods,pmod);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}
			else
			{
				AttribMod *pmodPrior = NULL;
				AttribMod *pmodPriorWorst = NULL;
				int j, iWorstIdx = 0;
				U32 uiStackLimit = pdef->uiStackLimit ? pdef->uiStackLimit - 1 : 0;
				for(j=eaSize(&pchar->modArray.ppMods)-1; j>=0; j--)
				{
					// To be a stacking match, the AttribMod must have
					//  - the same def OR the same eStackGroupPending, attrib type, aspect and stack type
					//  - StackEntity of None OR the same appropriate StackEntity
					//    - for Source, to fix trivial stacking exploiting by zoning, matching works both:
					//     - implicitly (eTarget that is not Target means a Self-targeted AttribMod)
					//     - explicitly
					AttribMod *pmodCurrent = pchar->modArray.ppMods[j];
					AttribModDef *pdefCurrent = pmodCurrent->pDef;
					if(((pdefCurrent==pdef && (!pdef->bPowerInstanceStacking || pmodCurrent->uiPowerID==pmod->uiPowerID))
						|| mod_AssumeSameCopy(pmod, pmodCurrent)) 
						&& (!pdef->bPersonal || pmod->erPersonal == pmodCurrent->erPersonal) 
						&& ((pdef->eStackEntity==kStackEntity_Source && (pdef->eTarget!=kModTarget_Target || pmodCurrent->erSource==pmod->erSource))
							|| pdef->eStackEntity==kStackEntity_None
							|| (pdef->eStackEntity==kStackEntity_Owner && pmodCurrent->erOwner==pmod->erOwner)))
					{

// Returns if A is worse than B, where worse is defined as having less remaining duration,
//  or the same remaining duration but an older applyID (thus handling Forever durations gracefully)
#define MOD_IS_WORSE(pmodA,pmodB) ((pmodA)->fDuration<(pmodB)->fDuration || ((pmodA)->fDuration==(pmodB)->fDuration && (pmodA)->uiApplyID<(pmodB)->uiApplyID))

						if(uiStackLimit)
						{
							uiStackLimit--;
							// In stack limited scenario, track the 'worst' prior we've found
							if(!pmodPriorWorst || MOD_IS_WORSE(pmodCurrent,pmodPriorWorst))
							{
								pmodPriorWorst = pmodCurrent;
								iWorstIdx = j;
							}
						}
						else
						{
							if(pmodPriorWorst && MOD_IS_WORSE(pmodPriorWorst,pmodCurrent))
							{
								// We had some 'worst' prior due to a stack limiting, and it was worse than this one
								pmodPrior = pmodPriorWorst;
								j = iWorstIdx;
							}
							else
							{
								pmodPrior = pmodCurrent;
							}
							break;
						}
					}
				}

				if(!pmodPrior)
				{
					// Didn't find another copy, just push it
					eaPush(&pchar->modArray.ppMods,pmod);
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				}
				else
				{
					switch(pdef->eStack)
					{
						case kStackType_Discard:
						{
							character_ModsProcessPendingDiscard(iPartitionIdx, pchar, pExtract, pmod, pmodPrior, j);
							break;
						}
						case kStackType_Extend:
						{
							// Extend is allowed to "restart" prior AttribMods that expired due to duration
							//  last tick.
							if(pmodPrior->fDuration >= 0 || pmodPrior->fDuration==kModExpirationReason_Duration)
							{
								// Extend the old one by the new one's duration
								MAX1(pmodPrior->fDuration,0);
								pmodPrior->fDuration += pmod->fDuration;

								// Do more complicated stuff if it was fragile
								if(pmodPrior->pFragility)
								{
									// Reset the health to full
									pmodPrior->pFragility->fHealth = pmodPrior->pFragility->fHealthMax;
									if(pdef->eType&kModType_Duration)
									{
										// It's now full health, copy new duration
										pmodPrior->fDurationOriginal = pmodPrior->fDuration;
									}
									if(pdef->eType&kModType_Magnitude)
									{
										// It's now full health, reset the magnitude
										pmodPrior->fMagnitude = pmodPrior->fMagnitudeOriginal;
									}
								}
								// TODO(JW): What else needs to be adjusted on an extend?
								mod_Destroy(pmod);
							}
							else
							{
								//For expired mods, replace them
								mod_Replace(iPartitionIdx, pchar, pmod, pmodPrior, j, pExtract);
							}
							break;
						}
						case kStackType_Replace:
						{
							character_ModsProcessPendingReplace(iPartitionIdx, pchar, pExtract, pmod, pmodPrior, j);
							break;
						}
						case kStackType_KeepBest:
						{							
							if (mods_CompareMagnitude(pmod, pmodPrior) > 0)
							{
								character_ModsProcessPendingReplace(iPartitionIdx, pchar, pExtract, pmod, pmodPrior, j);
							}
							else
							{
								character_ModsProcessPendingDiscard(iPartitionIdx, pchar, pExtract, pmod, pmodPrior, j);
							}

							break;
						}
					}
				}
			}
		}
		else
		{
			// Hasn't become active this tick
			pmod->fTimer -= fRate;

			character_SetSleep(pchar,pmod->fTimer - pmod->fPredictionOffset);
			
			if(bFragile)
			{
				if(pmod->fDuration < 0)
				{
					// It's fragile and been marked as dead already, remove it from the pending list
					eaRemoveFast(&pchar->modArray.ppModsPending,i);
					mod_Destroy(pmod);
				}
				else
				{
					eaPush(&pchar->modArray.ppFragileMods,pmod);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}


typedef struct ModStackGroupSet
{
	AttribType eAttrib;
		// Attribute for this set of AttribMods

	AttribAspect eAspect;
		// Aspect for this set of AttribMods

	ModStackGroup eGroup;
		// Group for this set of AttribMods

	AttribMod **ppMods;
		// The set of AttribMods, all of which have the same Attribute, Aspect and Group.

	U32 uiStackLimit;
		// The number of mods that can stack for this set of AttribMods
} ModStackGroupSet;



// Utility function to mark mods as either canceled or ignored based on CombatEvents or being disabled.
//  Should be called before processing the active list.
void character_ModsSuppress(int iPartitionIdx, Character *pchar)
{
	int i;
	static ModStackGroupSet **s_ppStackGroups = NULL;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&pchar->modArray.ppMods)-1;i>=0;i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		AttribModDef *pdef = pmod->pDef;
		S32 bIgnoredOld = pmod->bIgnored && !pmod->bDisabled;
		
		pmod->bIgnored = false;

		// Check for expire if Power no longer exists
		if(pdef->pPowerDef->bModsExpireWithoutPower)
		{
			Entity *pEntSource = entFromEntityRef(iPartitionIdx,pmod->erSource);

			if (!pEntSource || !pEntSource->pChar ||
				(g_CombatConfig.bExpiresWithoutPowerChecksAlive && !entIsAlive(pEntSource)))
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
				continue;
			}
			else if (!pmod->uiPowerID && g_CombatConfig.bExpiresWithoutPowerChecksAlive)
			{
				// If no source power because we might be an apply power, just check alive
			}
			else if(!pmod->uiPowerID || !character_FindPowerByID(pEntSource->pChar,pmod->uiPowerID))
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
				continue;
			}
		}

		if(pmod->bDisabled)
		{
			pmod->bIgnored = true;
			continue;
		}

		if(pdef->piCombatEvents
			&& (pdef->eCombatEventResponse > kCombatEventResponse_CheckExisting
				|| (bIgnoredOld && pdef->eCombatEventResponse==kCombatEventResponse_IgnoreIfNew)))
		{
			if(character_CheckCombatEvents(pchar,pdef->piCombatEvents,pdef->fCombatEventTime))
			{
				// A matching event was found
				switch(pdef->eCombatEventResponse)
				{
				case kCombatEventResponse_Cancel:
					character_ModExpireReason(pchar, pmod, kModExpirationReason_CombatEventCancel);
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
					break;
				case kCombatEventResponse_Ignore:
				case kCombatEventResponse_IgnoreIfNew:
					pmod->bIgnored = true;
					break;
				}
			}
		}

		// Check if it should be ignored because the target is NearDeath and
		//  it modifies HitPoints but isn't from a NearDeath-specific Power
		if(!pmod->bIgnored
			&& pchar->pNearDeath
			&& pdef->offAttrib==kAttribType_HitPoints
			&& IS_BASIC_ASPECT(pdef->offAspect))
		{
			PowerTarget *pPowerTarget = GET_REF(pdef->pPowerDef->hTargetAffected);
			if(!(pPowerTarget && pPowerTarget->bAllowNearDeath))
				pmod->bIgnored = true;
		}

#ifdef GAMESERVER
		if(!pmod->bIgnored 
			&& pdef->bIgnoredDuringPVP
			&& gslQueue_IgnorePVPMods(entGetPartitionIdx(pchar->pEntParent)))
		{
			pmod->bIgnored = true;
		}
#endif

		// Check if we may need to ignore due to ModStackGroup
		//  (only non-personal basics are handled here)
		if(!pmod->bIgnored
			&& pmod->fDuration >= 0
			&& pdef->eStackGroup
			&& !pmod->erPersonal
			&& (IS_BASIC_ASPECT(pdef->offAspect) || pdef->uiStackLimit > 1))
		{
			int j;
			ModStackGroupSet *pStackGroup = NULL;
			for(j=eaSize(&s_ppStackGroups)-1; j>=0; j--)
			{
				pStackGroup = s_ppStackGroups[j];
				if(pStackGroup->eAttrib==pdef->offAttrib
					&& pStackGroup->eAspect==pdef->offAspect
					&& pStackGroup->eGroup==pdef->eStackGroup)
				{
					break;
				}
			}

			if(j<0)
			{
				pStackGroup = calloc(1,sizeof(ModStackGroupSet));
				pStackGroup->eAttrib = pdef->offAttrib;
				pStackGroup->eAspect = pdef->offAspect;
				pStackGroup->eGroup = pdef->eStackGroup;
				pStackGroup->uiStackLimit = pdef->uiStackLimit ? pdef->uiStackLimit : 1;
				eaPush(&s_ppStackGroups,pStackGroup);
			}

			eaPush(&pStackGroup->ppMods,pmod);
		}
	}

	// Find the stuff to ignore due to ModStackGroup
	for(i=eaSize(&s_ppStackGroups)-1; i>=0; i--)
	{
		ModStackGroupSet *pStackGroup = s_ppStackGroups[i];
		U32 uiStackLimit = pStackGroup->uiStackLimit;
		unsigned int s = eaSize(&pStackGroup->ppMods);
		if(s > uiStackLimit)
		{
			int j;
			int iBest = mods_FindBest(pStackGroup->ppMods, 0);
			int iModsToIgnore = s - uiStackLimit;
			for(j=s-1; j>=0 && iModsToIgnore > 0; j--)
			{
				if(j!=iBest)
				{
					pStackGroup->ppMods[j]->bIgnored = true;
					iModsToIgnore--;
				}
			}
		}
		eaDestroy(&pStackGroup->ppMods);
		free(pStackGroup);
	}

	eaClearFast(&s_ppStackGroups);

	PERFINFO_AUTO_STOP();
}

// Applies any expiration effects of the AttribMod
void character_ApplyModExpiration(int iPartitionIdx, Character *pchar, AttribMod *pmod, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	if(pmod->ppApplyStrengths && pmod->pSourceDetails)
	{
		AttribModDef *pdef = pmod->pDef;
		if(pdef->pExpiration && GET_REF(pdef->pExpiration->hDef))
		{
			PowerDef *ppowdef = GET_REF(pdef->pExpiration->hDef);
			EntityRef erApplyTarget = pmod->erOwner;
			S32 bFail = false;
			Character *pcharTargetType = mod_GetApplyTargetTypeCharacter(iPartitionIdx,pmod,&bFail);
			ApplyUnownedPowerDefParams applyParams = {0};
			static Power **s_eaPowEnhancements = NULL;

			if(bFail)
			{
				PERFINFO_AUTO_STOP();
				return;
			}

			if(pdef->pExpiration->pExprRequiresExpire)
			{
				combateval_ContextSetupExpiration(pchar,pmod,pdef,pdef->pPowerDef);
				if(!combateval_EvalNew(iPartitionIdx, pdef->pExpiration->pExprRequiresExpire,kCombatEvalContext_Expiration,NULL))
				{
					PERFINFO_AUTO_STOP();
					return;
				}
			}

			if(pdef->pExpiration->eTarget==kModExpirationEntity_ModSource)
			{
				erApplyTarget = pmod->erSource;
			}
			else if(pdef->pExpiration->eTarget==kModExpirationEntity_ModSourceTargetDual)
			{
				Entity *eSource = entFromEntityRef(iPartitionIdx, pmod->erSource);
				erApplyTarget = (eSource && eSource->pChar) ? character_GetTargetDualOrSelfRef(iPartitionIdx, eSource->pChar) : 0;
			}
			else if(pdef->pExpiration->eTarget==kModExpirationEntity_ModTarget)
			{
				erApplyTarget = entGetRef(pchar->pEntParent);
			}
			else if(pdef->pExpiration->eTarget==kModExpirationEntity_RandomNotSource)
			{
				erApplyTarget = character_FindRandomTargetForPowerDef(iPartitionIdx,pchar,ppowdef,pcharTargetType,pmod->erSource);
			}

			applyParams.pmod = pmod;
			applyParams.erTarget = erApplyTarget;
			applyParams.pSubtarget = pmod->pSubtarget;
			applyParams.pcharSourceTargetType = pcharTargetType;
			applyParams.pclass = GET_REF(pmod->pSourceDetails->hClass);
			applyParams.iLevel = pmod->pSourceDetails->iLevel;
			applyParams.iIdxMulti = pmod->pSourceDetails->iIdxMulti;
			applyParams.fTableScale = pmod->pSourceDetails->fTableScale;
			applyParams.iSrcItemID = pmod->pSourceDetails->iItemID;
			applyParams.bLevelAdjusting = pmod->pSourceDetails->bLevelAdjusting;
			applyParams.ppStrengths = pmod->ppApplyStrengths;
			applyParams.pCritical = pmod->pSourceDetails->pCritical;
			applyParams.erModOwner = pmod->erOwner;
			applyParams.uiApplyID = pmod->uiApplyID;
			applyParams.fHue = pmod->fHue;
			applyParams.pExtract = pExtract;
			applyParams.bCountModsAsPostApplied = true;
			
			
			if(pmod->erOwner)
			{
				Entity *pModOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
				if (pModOwner && pModOwner->pChar)
				{
					power_GetEnhancementsForAttribModApplyPower(iPartitionIdx, pchar, 
																pmod, EEnhancedAttribList_EXPIRATION, 
																ppowdef, &s_eaPowEnhancements);

					applyParams.pppowEnhancements = s_eaPowEnhancements;
				}
			}

			character_ApplyUnownedPowerDef(iPartitionIdx, pchar, ppowdef, &applyParams);
			eaClear(&s_eaPowEnhancements);
		}
	}
	PERFINFO_AUTO_STOP();
}

// Utility function to check all the currently active mods, and see if they have expired. if
// so removed them from the active mods list. Should be called before processing the active list.
void character_ModsRemoveExpired(int iPartitionIdx, Character *pchar, F32 fRate, GameAccountDataExtract *pExtract)
{
	int i,s = eaSize(&pchar->modArray.ppMods);

	if(!s)
		return;
	
	PERFINFO_AUTO_START_FUNC();

	for(i=s-1;i>=0;i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];

		if(pmod->fDuration < 0.0f)
		{
			if(!mod_ExpireIsValid(pmod))
			{
				Errorf("AttribMod (%s %d) expired without setting a proper expiration reason. Please check that the AttribMod duration expression will not returning a negative value. Otherwise notify a programmer.",pmod->pDef->pPowerDef->pchName,pmod->uiDefIdx);
				mod_Expire(pmod);
			}
			
			if(pmod->pDef && pmod->pDef->pExpiration && !pmod->pDef->pExpiration->bPeriodic)
				character_ApplyModExpiration(iPartitionIdx,pchar,pmod, pExtract);
			
			eaRemoveFast(&pchar->modArray.ppMods,i);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			mod_Cancel(iPartitionIdx,pmod,pchar,true,NULL,pExtract);
			eaFindAndRemoveFast(&pchar->modArray.ppModsSaved,pmod);
			mod_Destroy(pmod);
		}
	}
	PERFINFO_AUTO_STOP();
}

// Compares two attrib mods to determine which one has greater magnitude.
// Remarks: Shields use the health instead of magnitude for magnitude comparison
S32 mods_CompareMagnitude(SA_PARAM_NN_VALID AttribMod *pMod1, SA_PARAM_NN_VALID AttribMod *pMod2)
{
	if (pMod1 == pMod2)
	{
		return 0;
	}

	if (pMod1->pDef->offAttrib == kAttribType_Shield && 
		pMod2->pDef->offAttrib == kAttribType_Shield &&
		pMod1->pFragility &&
		pMod2->pFragility)
	{
		// Special handling for shields. In theory, we can do this for all fragile mods
		return fabs(pMod1->pFragility->fHealth) - fabs(pMod2->pFragility->fHealth);
	}
	else
	{
		return fabs(pMod1->fMagnitude) - fabs(pMod2->fMagnitude);
	}
}

// Searches the list of AttribMods for the "best" one and returns its index.  If erPersonal is 0,
//  will not include bPersonal AttribMods.  If erPersonal is non-zero, bPersonal AttribMods that
//  match will be included.  Assumes all AttribMods are the "same" (Attribute, Aspect and ModStackGroup).
S32 mods_FindBest(SA_PARAM_OP_VALID AttribMod **ppMods, EntityRef erPersonal)
{
	F32 fMagBest = -1;
	S32 i, iBest = -1;

	for(i=eaSize(&ppMods)-1; i>=0; i--)
	{
		F32 fMag;
		AttribMod *pmod = ppMods[i];
		if(pmod->erPersonal && pmod->erPersonal!=erPersonal)
			continue;

		// TODO(JW): Take resistance into account?  Wow...
		fMag = fabs(pmod->fMagnitude);
		if(fMag > fMagBest)
		{
			fMagBest = fMag;
			iBest = i;
		}
	}

	return iBest;
}

typedef struct ModStackGroupFilter
{
	AttribAspect eAspect;
		// Aspect for this filter

	ModStackGroup eGroup;
		// Group for this filter

	F32 fMagBest;
		// Best magnitude for this filter

	AttribMod *pModBest;
		// Pointer to the best AttribMod for this filter

} ModStackGroupFilter;


// Filters the list of AttribMods, removing anything that isn't the "best" in its ModStackGroup.
//  Assumes all AttribMods are the same Attribute, but not Aspect or ModStackGroup.
void mods_StackGroupFilter(AttribMod **ppMods)
{
	static ModStackGroupFilter **s_ppStackGroups = NULL;
	S32 i,j,k;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&ppMods)-1; i>=0; i--)
	{
		AttribMod *pmod = ppMods[i];
		AttribModDef *pdef = mod_GetDef(pmod);
		
		if(!pdef || !pdef->eStackGroup || pdef->uiStackLimit > 1)
			continue;
		
		for(j=eaSize(&s_ppStackGroups)-1; j>=0; j--)
		{
			ModStackGroupFilter *pStackGroup = s_ppStackGroups[j];
			if(pStackGroup->eAspect==pdef->offAspect
				&& pStackGroup->eGroup==pdef->eStackGroup)
			{
				// Found existing aspect/group, if this one is better, keep it
				//  and remove the prior, otherwise remove it
				F32 fMag = fabs(pmod->fMagnitude);
				if(fMag > pStackGroup->fMagBest)
				{
					for(k=i+1; k<eaSize(&ppMods); k++)
					{
						if(ppMods[k]==pStackGroup->pModBest)
						{
							eaRemoveFast(&ppMods,k);
							break;
						}
					}
					pStackGroup->fMagBest = fMag;
					pStackGroup->pModBest = pmod;
				}
				else
				{
					eaRemoveFast(&ppMods,j);
				}

				break;
			}
		}

		if(j<0)
		{
			ModStackGroupFilter *pStackGroup = calloc(1,sizeof(ModStackGroupFilter));
			pStackGroup->eAspect = pdef->offAspect;
			pStackGroup->eGroup = pdef->eStackGroup;
			pStackGroup->fMagBest = fabs(pmod->fMagnitude);
			pStackGroup->pModBest = pmod;
			eaPush(&s_ppStackGroups,pStackGroup);
		}
	}

	for(i=eaSize(&s_ppStackGroups)-1; i>=0; i--)
	{
		free(s_ppStackGroups[i]);
	}
	eaClearFast(&s_ppStackGroups);

	PERFINFO_AUTO_STOP();
}




// Updates an AttribModNet with an AttribMod's data
static void ModUpdateNet(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID AttribModNet *pnet, S32 bNew)
{
	U32 uTemp;
	S32 iTemp, iTempDurationOriginal, iTempResistPositive, iTempResistNegative;
	S32 bDirty = bNew;
	if(bNew)
	{
		COPY_HANDLE(pnet->hPowerDef,pmod->hPowerDef);
		pnet->uiDefIdx = pmod->uiDefIdx;
		pnet->pvAttribMod = pmod;
		if(!pmod->pFragility)
		{
			pnet->iHealth = 0;
			pnet->iHealthMax = 0;
		}
	}

	if(pmod->pDef->bForever)
	{
		pnet->uiDuration = 1;
		iTempDurationOriginal = 1;
		iTempResistPositive = 0;
		iTempResistNegative = 0;
	}
	else
	{
		pnet->uiDuration = (U32)(ceil(MAX(0,pmod->fDuration)));
		iTempDurationOriginal = (S32)(ceil(pmod->fDurationOriginal));
		iTempResistPositive = !!pmod->bResistPositive;
		iTempResistNegative = !!pmod->bResistNegative;
	}

#define MODNETSETU32(field,value) uTemp = (U32)(value); if((field) != uTemp) { (field) = uTemp; bDirty = true; }
#define MODNETSETS32(field,value) iTemp = (S32)(value); if((field) != iTemp) { (field) = iTemp; bDirty = true; }

	MODNETSETU32(pnet->uiDurationOriginal, iTempDurationOriginal);
	MODNETSETU32(pnet->bResistPositive, iTempResistPositive);
	MODNETSETU32(pnet->bResistNegative, iTempResistNegative);
	MODNETSETS32(pnet->iMagnitude, ATTRIBMODNET_MAGSCALE * pmod->fMagnitude);
	MODNETSETS32(pnet->iMagnitudeOriginal, pmod->fMagnitude==pmod->fMagnitudeOriginal ? 0 : (ATTRIBMODNET_MAGSCALE * pmod->fMagnitudeOriginal));
	if(pmod->pFragility)
	{
		MODNETSETS32(pnet->iHealth, pmod->pFragility->fHealth);
		MODNETSETS32(pnet->iHealthMax, pmod->pFragility->fHealth==pmod->pFragility->fHealthMax ? 0 : pmod->pFragility->fHealthMax);
	}

	if(bDirty)
	{
		entity_SetDirtyBit(pchar->pEntParent,parse_AttribModNet,pnet,false);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		pchar->uiTimestampModsNet = 0;
	}
}

// Utility function update the Character's AttribModNet data
//  TODO(JW): Optimize: This loop is probably slow
void character_ModsUpdateNet(Character *pchar)
{
	int i, j, s, t;
	static int *s_piOpen = NULL;
	static AttribMod **s_ppMods = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Find all AttribMods that should be included
	s = eaSize(&pchar->modArray.ppMods);
	for(i=0; i<s; i++)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		if(pmod->fDurationOriginal > 0
			&& !pmod->bDisabled
			&& !pmod->pDef->bDerivedInternally)
		{
			eaPush(&s_ppMods,pmod);
		}
	}

	s = eaSize(&s_ppMods);
	t = eaSize(&pchar->ppModsNet);

	// Freshen all the existing AttribModNets
	for(j=t-1; j>=0; j--)
	{
		AttribMod *pmod = NULL;
		AttribModNet *pnet = pchar->ppModsNet[j];

		// If we know this doesn't match an existing AttribMod
		if(!pnet->uiDurationOriginal || !pnet->pvAttribMod)
		{
			eaiPush(&s_piOpen,j);
			continue;
		}

		// Look for a match
		for(i=s-1; i>=0; i--)
		{
			if(pnet->pvAttribMod == s_ppMods[i])
				break;
		}

		if(i < 0)
		{
			// No match, mark it as open
			entity_SetDirtyBit(pchar->pEntParent,parse_AttribModNet,pnet,false);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			pnet->uiDurationOriginal = 0;
			pnet->pvAttribMod = 0;
			eaiPush(&s_piOpen,j);
			continue;
		}

		// Found our original, update the net and remove it from the list of mods
		ModUpdateNet(pchar,s_ppMods[i],pnet,false);
		eaRemoveFast(&s_ppMods,i);
		s--;
	}

	// Everything left in the list of mods needs a new net
	t = eaiSize(&s_piOpen);
	for(i=s-1; i>=0; i--)
	{
		if(t>0)
		{
			// There are open slots, use the last one in the list (which should actually
			//  be the first open slot)
			ModUpdateNet(pchar,s_ppMods[i],pchar->ppModsNet[s_piOpen[t-1]],true);
			eaiRemoveFast(&s_piOpen,t-1);
			t--;
		}
		else
		{
			// No room in the inn, make a new one
			AttribModNet *pnet = MP_ALLOC(AttribModNet);
			ModUpdateNet(pchar,s_ppMods[i],pnet,true);
			eaPush(&pchar->ppModsNet,pnet);
		}
	}

	eaiClearFast(&s_piOpen);
	eaClearFast(&s_ppMods);

	PERFINFO_AUTO_STOP();
}

bool character_ModsNetCheckUpdate(Character *pchar, Character *pcharOld)
{
	U32 uiTimeNow = timeSecondsSince2000();
	int i, s, oldSize;
	
	if(pchar->uiTimestampModsNet == uiTimeNow)
		return false;

	if(!pcharOld)
		return true;

	s = eaSize(&pchar->ppModsNet);
	oldSize = eaSize(&pcharOld->ppModsNet);

	if(s != oldSize)
		return true;

	for(i=0; i<s; i++)
	{
		if(pchar->ppModsNet[i]->uiDuration != pcharOld->ppModsNet[i]->uiDuration)
		{
			return true;
		}
	}

	return false;
}

// Sends the Character's AttribModNet's durations
void character_ModsNetSend(Character *pchar, Packet* pak)
{
	int i, s = eaSize(&pchar->ppModsNet);

	pktSendBitsAuto(pak, s); // Send the count

	// Send the current duration of each
	for(i=0; i<s; i++)
	{
		pktSendBitsAuto(pak,pchar->ppModsNet[i]->uiDuration);
	}
}

// Receives the Character's AttribModNet's durations.  Safely consumes all the data if there isn't a Character.
void character_ModsNetReceive(Character *pchar,
							  Packet *pak)
{
	int i;
	U16 s = pktGetBitsAuto(pak); // Get the count
	int iModsNet = pchar ? eaSize(&pchar->ppModsNet) : 0;

	// Get the current duration of each
	for(i=0; i<s; i++)
	{
		U32 uiDuration = pktGetBitsAuto(pak);
		if(i<iModsNet)
		{
			pchar->ppModsNet[i]->uiDuration = uiDuration;
		}
	}
	pchar->uiTimestampModsNet = timeSecondsSince2000();
}

// Updates the AttribModNets for diffing next frame
void character_ModsNetUpdate(Character *pchar, Character *pcharOld)
{
	U32 uiTimeNow = timeSecondsSince2000();
	int i, s = eaSize(&pchar->ppModsNet);
	int oldSize = pcharOld ? eaSize(&pcharOld->ppModsNet) : 0;

	if(uiTimeNow != pchar->uiTimestampModsNet)
	{
		pchar->uiTimestampModsNet = uiTimeNow;

		// Send the current duration of each
		for(i=0; i<s; i++)
		{
			if(i < oldSize)
				pcharOld->ppModsNet[i]->uiDuration = pchar->ppModsNet[i]->uiDuration;
		}
	}
}

// Attempts to create fake AttribMods using the Character's AttribModNets
S32 character_ModsNetCreateFakeMods(Character *pchar)
{
	S32 bCreated = false;
#ifdef GAMECLIENT
	PERFINFO_AUTO_START_FUNC();

	if(verify(!eaSize(&pchar->modArray.ppMods)))
	{
		int i;
		for(i=eaSize(&pchar->ppModsNet)-1; i>=0; i--)
		{
			AttribModNet *pmodnet = pchar->ppModsNet[i];
			if(ATTRIBMODNET_VALID(pmodnet))
			{
				AttribModDef *pmoddef = modnet_GetDef(pmodnet);
				if(pmoddef)
				{
					AttribMod *pmod = StructCreate(parse_AttribMod);
					REF_HANDLE_COPY(pmod->hPowerDef,pmodnet->hPowerDef);
					pmod->uiDefIdx = pmodnet->uiDefIdx;
					pmod->pDef = pmoddef;
					pmod->fMagnitude = (F32)pmodnet->iMagnitude / ATTRIBMODNET_MAGSCALE;
					// Original Magnitude, Durations and Healths shouldn't really matter
					eaPush(&pchar->modArray.ppMods,pmod);
					bCreated = true;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
#endif
	return bCreated;
}

// Gets the predicted duration of a mod net
U32 character_ModNetGetPredictedDuration(Character *pchar, AttribModNet *pmodNet)
{
	if (pchar && pmodNet)
	{
		U32 uCurrentTime = timeSecondsSince2000();
		U32 uiDuration = pmodNet->uiDuration;
		// If the current time is greater than the last sent/receive time, then offset the duration by the elapsed time
		// Use a one second offset because the receive time will likely be somewhere in the middle of the second interval
		if (uCurrentTime > pchar->uiTimestampModsNet + 1) 
		{
			uiDuration -= MIN(uiDuration, uCurrentTime - pchar->uiTimestampModsNet);
		}
		return uiDuration;
	}
	return 0;
}

// Gets the magnitude of all the AttribModNet's on the character that match the (optional) given tags
F32 character_ModsNetGetTotalMagnitudeByTag(Character *pchar, 
											AttribType eType, 
											S32 *piTags,
											AttribModNet ***peaModNetsOut)
{
	if(pchar)
	{
		int i;
		F32 fMag = 0.f;
		S32 iNumTags = eaiSize(&piTags);
				
		for(i=eaSize(&pchar->ppModsNet)-1; i>=0; i--)
		{
			AttribModDef *pmoddef;
			AttribModNet *pNet = pchar->ppModsNet[i];

			if(!ATTRIBMODNET_VALID(pNet))
				continue;

			pmoddef = modnet_GetDef(pNet);

			if(pmoddef && pmoddef->offAttrib == eType)
			{
				if (iNumTags)
				{	// if we are looking for specific tags
					S32 x;

					if (!eaiSize(&pmoddef->tags.piTags))
						continue;

					for (x = iNumTags - 1; x >= 0; --x)
					{
						if (eaiFind(&pmoddef->tags.piTags, piTags[x]) != -1)
							break;
					}

					if (x < 0) // did not find it
						continue;
				}

				if (peaModNetsOut)
					eaPush(peaModNetsOut,pNet);
				
				if(eType == kAttribType_Shield && pNet->iHealthMax)
				{
					fMag += pNet->iHealth;
				}
				else
				{
					fMag += (F32)pNet->iMagnitude / ATTRIBMODNET_MAGSCALE;
				}
			
			}
		}

		return fMag;
	}

	return 0.f;
}

AttribModNet* character_ModsNetGetByIndexAndTag(Character *pchar, 
												AttribType eType, 
												S32 *piTags,
												S32 index)
{
	if (pchar && index >= 0)
	{
		static AttribModNet **s_ppModNets = NULL;
		eaClearFast(&s_ppModNets);

		character_ModsNetGetTotalMagnitudeByTag(pchar, eType, piTags, &s_ppModNets);

		if(eaSize(&s_ppModNets) > index)
		{
			eaQSort(s_ppModNets,modnet_CmpDurationDefIdx);
			return s_ppModNets[index];
		}
	}

	return NULL;
}

// When most AttribMods need to apply a Power, they call this function to find the Character that
//  should be used for the TargetType tests.  Basically just a wrapper for a CombatConfig check
//  and erOwner lookup for now.  If the value returned is invalid, true is assigned to pbFailOut,
//  and the apply should probably be terminated.
Character* mod_GetApplyTargetTypeCharacter(int iPartitionIdx, AttribMod *pmod, S32 *pbFailOut)
{
	if(g_CombatConfig.bModAppliedPowersUseOwnerTargetType
		|| (pmod->pDef->offAttrib==kAttribType_ApplyPower
			&& pmod->pDef->pParams
			&& ((ApplyPowerParams*)pmod->pDef->pParams)->bUseOwnerTargetType))
	{
		// TODO(JW): This gives a pass to stuff loaded from the DB, which is bad
		//  but that can potentially be fixed later, while unowned applies need to
		//  work now.
		EntityRef erOwner = pmod->erOwner;
		if(erOwner)
		{
			Entity *eOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
			if(eOwner && eOwner->pChar)
				return eOwner->pChar;
			else
				*pbFailOut = true;
		}
	}
	return NULL;
}


// Returns a list of attribs that the given attrib unrolls to, or NULL if the attrib does
//  not unroll.
AttribType *attrib_Unroll(AttribType eAttrib)
{
	if(eAttrib>=g_AttribSets.iSetOffsetStart && eAttrib<g_AttribSets.iSetOffsetEnd)
	{
		return g_AttribSets.ppSets[eAttrib-g_AttribSets.iSetOffsetStart]->poffAttribs;
	}

	return NULL;
}

// Determines if the Attrib A can be found in the Attrib B.  This is trivially true
//  if they're the same Attrib.  If the Attrib B unrolls, this function will return true
//  if the Attrib A is in the unrolled list.
bool attrib_Matches(AttribType eAttribA, AttribType eAttribB)
{
	if(eAttribA==eAttribB)
	{
		return true;
	}
	else
	{
		int i;
		AttribType *pUnroll = attrib_Unroll(eAttribB);
		for(i=eaiSize(&pUnroll)-1; i>=0; i--)
		{
			if(eAttribA==pUnroll[i])
				return true;
		}
	}

	return false;
}





// Loads and sets up the attrib groups
AUTO_STARTUP(AS_AttribSets) ASTRT_DEPS(AS_CharacterAttribs);
void attribsets_Load(void)
{
	int i,s;

	char *pSharedMemoryName = NULL;

	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsQueueServer() && !IsGroupProjectServer()) {
		return;
	}
	
	loadstart_printf("Loading attrib sets...");

	// load into shared memory, no reloading
	MakeSharedMemoryName("AttribSets", &pSharedMemoryName);
	ParserLoadFilesShared(pSharedMemoryName, NULL, "defs/config/attribsets.def", "AttribSets.bin", PARSER_OPTIONALFLAG, parse_AttribSets, &g_AttribSets);
	estrDestroy(&pSharedMemoryName);

	s = eaSize(&g_AttribSets.ppSets);

	g_AttribSets.pDefineAttribSets = DefineCreate();
	g_AttribSets.iSetOffsetStart = kAttribType_LAST+1;
	g_AttribSets.iSetOffsetEnd = g_AttribSets.iSetOffsetStart + s;

	for(i=0; i<s; i++)
	{
		char achVal[20];
		sprintf(achVal,"%d",g_AttribSets.iSetOffsetStart + i);
		DefineAdd(g_AttribSets.pDefineAttribSets,g_AttribSets.ppSets[i]->pchName,achVal);
	}

	loadend_printf(" done (%d  attrib sets).", s);
}

// Adds an AttribMod to the array
void modarray_Add(ModArray *plist, AttribMod *pmod)
{
	assert(plist);

	if(!plist->ppMods)
		eaSetCapacity(&plist->ppMods,5);

	eaPush(&plist->ppMods,pmod);
}

// Removes the AttribMod at the given index, destroys it
void modarray_Remove(ModArray *plist, int idx)
{
	AttribMod *pmod;

	assert(plist);

	pmod = eaRemoveFast(&plist->ppMods,idx);
	// dirtybit set in calling function

	if(pmod)
	{
		if(pmod->pFragility) eaFindAndRemoveFast(&plist->ppFragileMods,pmod);
		eaFindAndRemoveFast(&plist->ppModsSaved,pmod);
		mod_Destroy(pmod);
	}
}

// Check to see if this AttribMod is ready to be destroyed by the RemoveAll call,
//  which should have called cancel on everything already, so this should never
//  get hit, but it will...
static void ModRemoveAllCheck(AttribMod *pmod)
{
	if(eaSize(&pmod->ppPowersCreated))
	{
		int i;
		char *pchDetails = NULL;
		estrStackCreate(&pchDetails);
		estrConcatf(&pchDetails,"Pow %s, Mod %d, AttribType %s:\n",
			REF_STRING_FROM_HANDLE(pmod->hPowerDef),
			pmod->uiDefIdx,
			StaticDefineIntRevLookup(AttribTypeEnum, pmod->pDef->offAttrib));
		for(i=eaSize(&pmod->ppPowersCreated)-1; i>=0; i--)
		{
			Power *ppow = pmod->ppPowersCreated[i];
			estrConcatf(&pchDetails,"  %s %u\n", REF_STRING_FROM_HANDLE(ppow->hDef), ppow->uiID);
		}
		ErrorDetailsf("%s",pchDetails);
		estrDestroy(&pchDetails);
		ErrorfForceCallstack("AttribMod being destroyed still has Powers");
	}
}

// Removes and frees all AttribMods in the array, and destroys the array
void modarray_RemoveAll(Character *pchar, ModArray *plist, S32 bAllowSurvival, S32 bUnownedOnly)
{
	assert(plist);
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	if(!bAllowSurvival && !bUnownedOnly)
	{
		int i;
		eaDestroy(&plist->ppFragileMods);
		eaDestroy(&plist->ppOverrideMods);
		eaClear(&plist->ppModsSaved);

		for(i=eaSize(&plist->ppMods)-1; i>=0; i--)
		{
			ModRemoveAllCheck(plist->ppMods[i]);
		}

		eaDestroyEx(&plist->ppMods,mod_Destroy);
		eaDestroyEx(&plist->ppModsPending,mod_Destroy);
	}
	else
	{
		int i;
		for(i=eaSize(&plist->ppMods)-1; i>=0; i--)
		{
			AttribMod *pmod = plist->ppMods[i];
			if(bAllowSurvival && pmod && pmod->pDef && pmod->pDef->bSurviveTargetDeath)
			{
				continue;
			}
			if(bUnownedOnly && pmod && pmod->erOwner==entGetRef(pchar->pEntParent))
			{
				continue;
			}

			eaRemoveFast(&plist->ppMods,i);
			eaFindAndRemoveFast(&plist->ppFragileMods,pmod);
			eaFindAndRemoveFast(&plist->ppOverrideMods,pmod);
			eaFindAndRemoveFast(&plist->ppModsSaved,pmod);
			if(pmod)
			{
				ModRemoveAllCheck(pmod);
				mod_Destroy(pmod);
			}
		}

		for(i=eaSize(&plist->ppModsPending)-1; i>=0; i--)
		{
			AttribMod *pmod = plist->ppModsPending[i];
			if(bAllowSurvival && pmod && pmod->pDef && pmod->pDef->bSurviveTargetDeath)
			{
				continue;
			}
			if(bUnownedOnly && pmod && pmod->erOwner==entGetRef(pchar->pEntParent))
			{
				continue;
			}

			eaRemoveFast(&plist->ppModsPending,i);
			eaFindAndRemoveFast(&plist->ppFragileMods,pmod);
			if(pmod)
			{
				mod_Destroy(pmod);
			}
		}
	}
}

// Searches a mod array to find a mod that matches the given def from the same source
AttribMod *modarray_Find(ModArray *plist, AttribModDef *pdef, EntityRef erOwner, EntityRef erSource)
{
	int i;
	for(i=eaSize(&plist->ppMods)-1; i>=0; i--)
	{
		// A match is a matching def from the same source
		if(plist->ppMods[i]->pDef == pdef && plist->ppMods[i]->erSource == erSource)
		{
			return plist->ppMods[i];
		}
	}
	return NULL;
}

// Test to throw away mods that shouldn't be saved or loaded
// TODO(JW): Database: Eventually this code will have to be much smarter
static S32 Character_ModShouldNotPersist(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID AttribMod *pmod, SA_PARAM_NN_VALID AttribModDef *pmoddef, SA_PARAM_NN_VALID PowerDef *ppowdef)
{
	S32 bNoPersist = false;

	//Special case for ent creates marked to be persistent
	if(pmoddef->offAttrib==kAttribType_EntCreate)
	{
		EntCreateParams *pParams = (EntCreateParams*)pmoddef->pParams;
		if(pmod->erSource == pmod->erOwner && pmod->erSource == entGetRef(pchar->pEntParent))
			bNoPersist = !pParams->bPersistent;
		//If the mod source AND owner is not yourself, the mod cannot persist
		else
			bNoPersist = true;
	}
	else
	{
		// Don't bother saving AttribMods that last forever, since they're presumably going
		//  to get recreated by whatever created them in the first place
		bNoPersist = (pmoddef->bForever
			|| ppowdef->bModsExpireWithoutPower);
	}

	bNoPersist |= ( ppowdef->eType==kPowerType_Passive 
		|| ppowdef->eType==kPowerType_Toggle
		|| pmoddef->offAttrib==kAttribType_Confuse //Do not persist confuse powers for exploit reasons
		|| pmoddef->bPersonal
		);
	
	return bNoPersist;
}

// Fixes up a mod after a db load.  Returns true if the mod is still valid
static int Character_ModPostLoad(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_OP_VALID AttribMod *pmod, S32 bLevelDecreased)
{
	if(pmod && pmod->uiVersion && !pmod->bDisabled)
	{
		PowerDef *pdef = GET_REF(pmod->hPowerDef);
		if(pdef && pmod->uiVersion==pdef->uiVersion)
		{
			int i;
			AttribModDef *pmoddef = NULL;

			// First make sure the uiDefIdx is valid, if it's not, something is seriously wrong
			if(pmod->uiDefIdx >= eaUSize(&pdef->ppOrderedMods))
			{
				devassertmsg(0,"Loading AttribMod with invalid def index");
				return false;
			}

			// Set the pDef pointer
			pmod->pDef = pmoddef = pdef->ppOrderedMods[pmod->uiDefIdx];

			if(bLevelDecreased && pmoddef->offAttrib==kAttribType_EntCreate)
			{
				return false;
			}

			// Sanity check the duration v duration original
			if(pmod->fDuration > pmod->fDurationOriginal && pmoddef->eStack!=kStackType_Extend)
			{
				devassertmsg(0,"Loading AttribMod with more duration than original duration that doesn't extend");
				return false;
			}

			// Get the current period of the attrib mod
			// TODO: This will not work if the duration has been extended
			if(pmoddef->fPeriod && pmod->fDurationOriginal > pmod->fDuration)
			{
				pmod->uiPeriod = (U32)((pmod->fDurationOriginal - pmod->fDuration) / pmoddef->fPeriod) + 1;
			}

			// Validate and fix up any fragility data
			if(pmod->pFragility)
			{
				// Validate existence
				if(pmod->pFragility && !pmoddef->pFragility)
				{
					devassertmsg(0,"Loading AttribMod with fragility data it should not have");
					return false;
				}

				// Validate original health
				if(pmod->pFragility->fHealthOriginal <= 0)
				{
					devassertmsg(0,"Loading AttribMod with fragility data having <= 0 original health");
					return false;
				}

				// Fix fHealthMax if it's 0
				if(pmod->pFragility->fHealthMax==0)
				{
					pmod->pFragility->fHealthMax = pmod->pFragility->fHealthOriginal;
				}
			}

			// Check the version of all the PowerApplyStrengths
			for(i=eaSize(&pmod->ppApplyStrengths)-1; i>=0; i--)
			{
				PowerDef *pdefApply = GET_REF(pmod->ppApplyStrengths[i]->hdef);
				if(!pdefApply || !pdefApply->uiVersion || pdefApply->uiVersion!=pmod->ppApplyStrengths[i]->uiVersion)
				{
					StructDestroy(parse_PowerApplyStrength,pmod->ppApplyStrengths[i]);
					eaRemoveFast(&pmod->ppApplyStrengths,i);
				}
			}

			//Special case to make sure the owner of EntCreate mods is yourself
			if(pmoddef->offAttrib==kAttribType_EntCreate && !pmod->erSource)
			{
				pmod->erSource = pmod->erOwner = entGetRef(pchar->pEntParent);
			}

			// Basic non-persist check (shouldn't have been saved, but checking again for safety)
			if(Character_ModShouldNotPersist(pchar,pmod,pmoddef,pdef))
			{
				return false;
			}
			else
			{
				//If the parameters got deleted before save, recreate them
				if( !pmod->pParams
					&& ( pmoddef->offAttrib==kAttribType_EntCreate 
						 || pmoddef->offAttrib==kAttribType_Shield
						 || pmoddef->offAttrib==kAttribType_DynamicAttrib) )
				{
					pmod->pParams = StructCreate(parse_AttribModParams);
					//TODO(BH): If some non-persisted fields need better default values, fill them in here
				}
				return true;
			}
		}
	}
	return false;
}

// Sorts mods first by PowerDef, then by uiApplyID, then uiDefIdx
static int SortModForDiff(const void *a, const void *b)
{
	int d;
	AttribMod *pModA = *((AttribMod**)a);
	AttribMod *pModB = *((AttribMod**)b);
	PowerDef *pDefA = pModA ? GET_REF(pModA->hPowerDef) : NULL;
	PowerDef *pDefB = pModB ? GET_REF(pModB->hPowerDef) : NULL;

	if(pDefA!=pDefB)
	{
		if(pDefA==NULL || pDefB==NULL)
		{
			d = pDefA==NULL ? 1 : -1;
		}
		else
		{
			d = strcmp(pDefA->pchName,pDefB->pchName);
		}
	}
	else
	{
		if(pDefA==NULL)
		{
			d = 0;
		}
		else
		{
			d = pModA->uiApplyID - pModB->uiApplyID; // Sort by apply id
			if(!d) d = pModA->uiDefIdx - pModB->uiDefIdx; // Sort by def idx
			// TODO(JW): If we use multi-attrib mod defs, sort by the attrib if the def idx matches
		}
	}
	return d;
}

// Pushes the AttribMods that needs to be saved into the ppModsSaved list
void character_SaveAttribMods(Character *pchar)
{
	static AttribModParams *pDefaultParams = NULL;
	int i;

	if(!pDefaultParams)
		pDefaultParams = StructCreate(parse_AttribModParams);

	eaClearFast(&pchar->modArray.ppModsSaved);

	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		AttribModDef *pmoddef = pmod->pDef;
		PowerDef *pdef = pmoddef->pPowerDef;

		// Basic non-persist check
		if(Character_ModShouldNotPersist(pchar,pmod, pmoddef, pdef))
		{
			continue;
		}
		
		//Check to see if the AttribModParams are the default values, else destroy the params
		if( pmoddef->offAttrib==kAttribType_EntCreate
			|| pmoddef->offAttrib==kAttribType_Shield
			|| pmoddef->offAttrib==kAttribType_DynamicAttrib)
		{
			//If the struct is the default value, just destroy it before we save this character
			if(StructCompare(parse_AttribModParams, pmod->pParams, pDefaultParams, 0, TOK_PERSIST|TOK_NO_TRANSACT,0)==0)
			{
				StructDestroySafe(parse_AttribModParams, &pmod->pParams);
			}
		}
		else
		{
			StructDestroySafe(parse_AttribModParams, &pmod->pParams);
		}

		eaPush(&pchar->modArray.ppModsSaved,pmod);
	}

	i = eaSize(&pchar->modArray.ppModsSaved);
	if(i)
	{
		qsort((void*)pchar->modArray.ppModsSaved,i,sizeof(AttribMod*),SortModForDiff);
	}
}

// Cleans up the array of AttribMods after a load from db
void character_LoadAttribMods(Character *pchar, S32 bOffline, S32 bLevelDecreased)
{
	int i;
	static U32 *s_puiApplyIDs = NULL;

	ea32ClearFast(&s_puiApplyIDs);
	eaIndexedEnable(&pchar->modArray.ppPowers,parse_Power);

	// If there was anything in the ppModsSaved, move it to ppMods.  This is an if() simply
	//  to make transition smoother for existing Characters who had AttribMods in ppMods.
	if(eaSize(&pchar->modArray.ppModsSaved) && !eaSize(&pchar->modArray.ppMods))
	{
		eaCopy(&pchar->modArray.ppMods,&pchar->modArray.ppModsSaved);
		eaClearFast(&pchar->modArray.ppModsSaved);
	}

	// Standard loading process.  If this is an offline load we throw all the mods away.
	//  Otherwise we test it with ModPostLoad() to see if it's legal to keep, which
	//  checks a bunch of things.
	// Forward order for both loops to try and keep things in similar order to how they
	//  were upon save.
	for(i=0; i<eaSize(&pchar->modArray.ppMods); i++)
	{
		AttribMod *pmod = pchar->modArray.ppMods[i];
		if(bOffline || !Character_ModPostLoad(pchar,pmod,bLevelDecreased))
		{
			modarray_Remove(&pchar->modArray,i);
			i--;
		}
		else
		{
			// Any AttribMods that had Powers need to update PowerIDs and add them to list
			int j, iPowersAdded = 0;
			for(j=0; j<eaSize(&pmod->ppPowersCreated); j++)
			{
				Power* ppow = pmod->ppPowersCreated[j];

				if(pmod->pDef->offAttrib==kAttribType_BecomeCritter || 
				   (pmod->pDef->offAttrib==kAttribType_GrantPower && !iPowersAdded))
				{
					// If this function gets called twice (which is bad in general), just blindly changing the
					//  PowerIDs and pushing can result in multiple copies of the same Power* in this array,
					//  so do a plain eaFind first.
					S32 iExistingIndex = eaFind(&pchar->modArray.ppPowers,ppow);
					if(iExistingIndex==-1)
					{
						U32 uiID = character_GetNewTempPowerID(pchar);
						power_SetIDHelper(CONTAINER_NOCONST(Power, ppow),uiID);
						eaPush(&pchar->modArray.ppPowers,ppow);
						iPowersAdded++;
					}
				}
				else
				{
					ErrorDetailsf("Character %s, Mod PowerDef %s, AttribType %s, Created PowerDef %s, Powers Added %d", 
						CHARDEBUGNAME(pchar), REF_STRING_FROM_HANDLE(pmod->hPowerDef), 
						StaticDefineIntRevLookup(AttribTypeEnum, pmod->pDef->offAttrib),
						REF_STRING_FROM_HANDLE(ppow->hDef), iPowersAdded);
					Errorf("character_LoadAttribMods: Mod had an invalid created power");
					
					// This created power is invalid, destroy it
					eaFindAndRemove(&pchar->modArray.ppPowers, ppow);
					power_Destroy(ppow, pchar);
					eaRemove(&pmod->ppPowersCreated, j);
					j--;
				}
			}

			// Update the uiApplyID - keep a unique list of uiApplyIDs we've seen on
			//  this Character, give a new id based on the unique index with the high
			//  bit set.
			if(pmod->uiApplyID)
			{
				j = ea32PushUnique(&s_puiApplyIDs,pmod->uiApplyID);
				pmod->uiApplyID = j | BIT(31);
			}
		}
	}

	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
}

// Walks the Character's AttribMod Powers and adds them to the general Powers list
void character_AddPowersFromAttribMods(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	int i;
	for(i=eaSize(&pchar->modArray.ppPowers)-1; i>=0; i--)
	{
		character_AddPower(iPartitionIdx,pchar,pchar->modArray.ppPowers[i],kPowerSource_AttribMod,pExtract);
	}
}



/***** End Mod Array Implementation *****/


int mod_CmpDuration(const void *a, const void *b)
{
	return ((*(AttribMod**)a)->fDuration > (*(AttribMod**)b)->fDuration) ? 1 : ((*(AttribMod**)a)->fDuration < (*(AttribMod**)b)->fDuration) ? -1 : 0;
}

int mod_CmpDurationDefIdx(const void *a, const void *b)
{
	int i = mod_CmpDuration(a,b);
	if(!i)
	{
		i = ((*(AttribMod**)a)->uiDefIdx > (*(AttribMod**)b)->uiDefIdx) ? 1 : ((*(AttribMod**)a)->uiDefIdx < (*(AttribMod**)b)->uiDefIdx) ? -1 : 0;
	}
	return i;
}

int modnet_CmpDurationDefIdx(const void *a, const void *b)
{
	int i;
	AttribModNet *pnetA = *(AttribModNet**)a;
	AttribModNet *pnetB = *(AttribModNet**)b;
	i = (pnetA->uiDuration > pnetB->uiDuration) ? 1 : (pnetA->uiDuration < pnetB->uiDuration) ? -1 : 0;
	if(!i)
	{
		i = (pnetA->uiDefIdx > pnetB->uiDefIdx) ? 1 : (pnetA->uiDefIdx < pnetB->uiDefIdx) ? -1 : 0;
	}
	return i;
}




// Destroys and frees the memory of an AttribMod.  If outside the mod processing loop, you
//  should be calling one of the modarray_Remove functions.
void mod_Destroy(AttribMod *pmod)
{
	PERFINFO_AUTO_START_FUNC();
	if (pmod->eaAttribModLinks)
	{
		// handle the deletion of AttribModLinks as it is a union and will either be a list of 
		// AttribModLinks or just a pointer to one depending if it is kAttribType_AttribLink or not
		if (pmod->pDef->offAttrib == kAttribType_AttribLink)
		{
			FOR_EACH_IN_EARRAY(pmod->eaAttribModLinks, AttribModLink, pLink)
				MP_FREE(AttribModLink, pLink);
			FOR_EACH_END
			eaDestroy(&pmod->eaAttribModLinks);
		}
		else
		{
			MP_FREE(AttribModLink, pmod->pAttribModLink);
			pmod->pAttribModLink = NULL;
		}
	}
	StructDestroy(parse_AttribMod, pmod);

	PERFINFO_AUTO_STOP();
}

void mod_Replace(int iPartitionIdx, Character *pchar, AttribMod *pmod, AttribMod *pmodPrior, int imodIdx, GameAccountDataExtract *pExtract)
{
	S32 bCancelFX = false;

	PERFINFO_AUTO_START_FUNC();

	if(pmod->pDef == pmodPrior->pDef) // Normal case - exact match
	{
		// Copy the old one's anim/fx state to the new one
		pmod->bContinuingAnimFXOn = pmodPrior->bContinuingAnimFXOn;
		pmod->bConditionalAnimFXOn = pmodPrior->bConditionalAnimFXOn;
		pmod->uiAnimFXID = pmodPrior->uiAnimFXID;

		// If the new mod will need an AttribModNet, see if we can re-use the old one's
		if(pmod->fDurationOriginal > 0
			&& !pmod->bDisabled
			&& !pmod->pDef->bDerivedInternally)
		{
			int i;
			for(i=eaSize(&pchar->ppModsNet)-1; i>=0; i--)
			{
				AttribModNet *pNet = pchar->ppModsNet[i];
				if(pNet->pvAttribMod==pmodPrior && pNet->uiDurationOriginal)
				{
					// This is the AttribModNet for the old mod, make it point
					//  to the new one now.  The actual data will be updated
					//  later in character_ModsUpdateNet()
					pNet->pvAttribMod = pmod;
					break;
				}
			}
		}
	}
	else
	{
		// Special case, we're replacing the old one with something that isn't an exact
		//  match (because of eStackGroupPending).  So we actually do cancel the fx, and
		//  we don't do any clever AttribModNet matching.
		bCancelFX = true;
	}

	// Kill the old mod
	mod_Cancel(iPartitionIdx,pmodPrior,pchar,bCancelFX,pmod,pExtract);
	eaFindAndRemoveFast(&pchar->modArray.ppModsSaved,pmodPrior);
	mod_Destroy(pmodPrior);

	pchar->modArray.ppMods[imodIdx] = pmod;
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

	PERFINFO_AUTO_STOP();
}

// Fills in an appropriate id and subid for bits and FX for this AttribMod
void mod_AnimFXID(AttribMod *pmod, U32 *uiID, U32 *uiSubID)
{
	if(!pmod->uiAnimFXID)
	{
		// We use the mod's address in memory as the unique id.
		pmod->uiAnimFXID = (U32)((U64)pmod);
	}
	// TODO(JW): AnimFX: Do I need to make the subid different?
	*uiID = *uiSubID = pmod->uiAnimFXID;
}

// Gets the actual CFX Params list from the default list (normally it just returns the default, but it
//  may end up appending special Params)
static PowerFXParam** ModDefCFXParams(AttribModDef *pmoddef, PowerFXParam **ppFXParams)
{
	static PowerFXParam **ppFXParamsCopy = NULL;
	PowerFXParam *pFXParamPowerIcon = NULL;

	if(!pmoddef->bPowerIconCFX)
		return ppFXParams;
	
	eaClearFast(&ppFXParamsCopy);
	eaCopy(&ppFXParamsCopy,&ppFXParams);
	pFXParamPowerIcon = moddef_SetPowerIconParam(pmoddef);
	eaPush(&ppFXParamsCopy,pFXParamPowerIcon);

	return ppFXParamsCopy;
}

// Starts the continuing animation bits and FX the may turned on.  Conditional
//  bits and fx are handled elsewhere
void mod_AnimFXOn(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	AttribModDef *pmoddef = pmod->pDef;
	PERFINFO_AUTO_START_FUNC();
	if(!pmod->bContinuingAnimFXOn)
	{
		U32 uiID=0,uiSubID=0;
		Entity *eSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
		Character *pcharTarget = eSource ? eSource->pChar : NULL;
		PowerFXParam **ppFXParams = ModDefCFXParams(pmoddef,pmoddef->ppContinuingFXParams);
		AttribModParams *pParamsMod = pmod->pParams;
		F32 *pvVecTarget = NULL;

		mod_AnimFXID(pmod,&uiID,&uiSubID);
		if(!character_IgnoresExternalAnimBits(pchar, pmod->erSource))
		{
			character_StickyBitsOn(pchar,uiID,uiSubID,kPowerAnimFXType_Continuing,pmod->erSource,pmoddef->ppchContinuingBits,MODTIME(pmod));
			character_StanceWordOn(pchar,uiID,uiSubID,kPowerAnimFXType_Continuing,pmod->erSource,pmoddef->ppchAttribModDefStanceWordText,MODTIME(pmod));
			character_SendAnimKeywordOrFlag(pchar,uiID,uiSubID,kPowerAnimFXType_ActivateFlash,pmod->erSource,pmoddef->pchAttribModDefAnimKeywordText,NULL,MODTIME(pmod),false,false,false,true,true,false);
		}

		if (pmoddef->bContinuingFXAsLocation && pParamsMod && !ISZEROVEC3(pParamsMod->vecTarget))
		{
			pcharTarget = NULL;
			pvVecTarget = pParamsMod->vecTarget;
		}

		character_StickyFXOn(	iPartitionIdx, pchar, uiID, uiSubID, kPowerAnimFXType_Continuing,
								pcharTarget, pvVecTarget,
								NULL, NULL, NULL, pmoddef->ppchContinuingFX, ppFXParams,
								pmod->fHue, MODTIME(pmod), 0, 0, 0);

	}
	// Always set this to true
	pmod->bContinuingAnimFXOn = true;
	PERFINFO_AUTO_STOP();
#endif
}

// Terminates animation bits and FX the AttribMod may have turned on
void mod_AnimFXOff(AttribMod *pmod, Character *pchar)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	AttribModDef *pmoddef = pmod->pDef;
	U32 uiID=0,uiSubID=0;
	PERFINFO_AUTO_START_FUNC();
	mod_AnimFXID(pmod,&uiID,&uiSubID);
	// Only turn them off if they're on
	if(pmod->bContinuingAnimFXOn)
	{
		if(!character_IgnoresExternalAnimBits(pchar, pmod->erSource))
		{
			character_StickyBitsOff(pchar,uiID,uiSubID,kPowerAnimFXType_Continuing,pmod->erSource,pmoddef->ppchContinuingBits,MODTIME(pmod));
			character_StanceWordOff(pchar,uiID,uiSubID,kPowerAnimFXType_Continuing,pmod->erSource,pmoddef->ppchAttribModDefStanceWordText,MODTIME(pmod));
		}
		character_StickyFXOff(pchar,uiID,uiSubID,kPowerAnimFXType_Continuing,NULL,pmoddef->ppchContinuingFX,MODTIME(pmod),false);
		pmod->bContinuingAnimFXOn = false;
	}
	// Only turn them off if they're on
	if(pmod->bConditionalAnimFXOn)
	{
		if(!character_IgnoresExternalAnimBits(pchar, pmod->erSource))
		{
			character_StickyBitsOff(pchar,uiID,uiSubID,kPowerAnimFXType_Conditional,pmod->erSource,pmoddef->ppchConditionalBits,MODTIME(pmod));
		}
		character_StickyFXOff(pchar,uiID,uiSubID,kPowerAnimFXType_Conditional,NULL,pmoddef->ppchConditionalFX,MODTIME(pmod),false);
		pmod->bConditionalAnimFXOn = false;
	}
	PERFINFO_AUTO_STOP();
#endif
}

// Checks and updates the status of conditional animation bits and FX
static void UpdateConditionalAnimFX(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	AttribModDef *pmoddef = pmod->pDef;
	// Only bother if this attrib makes sense and we actually have stuff to turn on/off
	if(eaSize(&pmoddef->ppchConditionalFX) || eaSize(&pmoddef->ppchConditionalBits))
	{
		int bOn = false;
		PERFINFO_AUTO_START_FUNC();
		if(IS_NORMAL_ATTRIB(pmoddef->offAttrib))
		{
			// Get the current value of the attrib (ignores aspect)
			F32 *pf = (F32*)((char*)(pchar->pattrBasic) + pmoddef->offAttrib);
			if(*pf>0)
			{
				bOn = true;
			}
		}
		else
		{
			// It's special, so if it's fragile it needs health, otherwise it needs to have lost magnitude
			if(pmod->pFragility)
			{
				bOn = (pmod->pFragility->fHealth > 0);
			}
			else
			{
				bOn = (pmod->fMagnitudeOriginal && pmod->fMagnitude);
			}
		}

		if(bOn)
		{
			// Only turn these on if the normal fx for the AttribMod are on
			if(pmod->bContinuingAnimFXOn && !pmod->bConditionalAnimFXOn)
			{
				U32 uiID=0,uiSubID=0;
				Entity *eSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
				Character *pcharTarget = eSource ? eSource->pChar : NULL;
				PowerFXParam **ppFXParams = ModDefCFXParams(pmoddef,pmoddef->ppConditionalFXParams);
				mod_AnimFXID(pmod,&uiID,&uiSubID);
				if(!character_IgnoresExternalAnimBits(pchar,pmod->erSource))
				{
					character_StickyBitsOn(pchar,uiID,uiSubID,kPowerAnimFXType_Conditional,pmod->erSource,pmoddef->ppchConditionalBits,MODTIME(pmod));
				}
				character_StickyFXOn(iPartitionIdx,pchar,uiID,uiSubID,kPowerAnimFXType_Conditional,
									pcharTarget,NULL,NULL,NULL,NULL,pmoddef->ppchConditionalFX,ppFXParams,pmod->fHue,MODTIME(pmod),
									0, 0, 0);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				pmod->bConditionalAnimFXOn = true;
			}
		}
		else
		{
			// Only turn them off if they're on
			if(pmod->bConditionalAnimFXOn)
			{
				U32 uiID=0,uiSubID=0;
				mod_AnimFXID(pmod,&uiID,&uiSubID);
				if(!character_IgnoresExternalAnimBits(pchar, pmod->erSource))
				{
					character_StickyBitsOff(pchar,uiID,uiSubID,kPowerAnimFXType_Conditional,pmod->erSource,pmoddef->ppchConditionalBits,MODTIME(pmod));
				}
				character_StickyFXOff(pchar,uiID,uiSubID,kPowerAnimFXType_Conditional,NULL,pmoddef->ppchConditionalFX,MODTIME(pmod),false);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				pmod->bConditionalAnimFXOn = false;
			}
		}
		PERFINFO_AUTO_STOP();
	}
#endif
}

// Looks through the mods and updates the conditional animation bits and fx
void character_UpdateConditionalModAnimFX(int iPartitionIdx, Character *pchar)
{
	int s=eaSize(&pchar->modArray.ppMods);
	if(s)
	{
		int i;
		PERFINFO_AUTO_START_FUNC();

		for(i=0; i<s; i++)
		{
			AttribMod *pmod = pchar->modArray.ppMods[i];
			if(pmod)
			{
				UpdateConditionalAnimFX(iPartitionIdx,pmod,pchar);
			}
		}
		PERFINFO_AUTO_STOP();
	}
}


// Creates a CSV description of the attrib mod and concatenates it to the estring
void moddef_CSV(AttribModDef *pdef, char **estr, const char *pchPrefix)
{
	estrConcatf(estr, "%s,%s,%s,%s,",pchPrefix,
		StaticDefineIntRevLookup(ModTargetEnum,pdef->eTarget),
		StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),
		StaticDefineIntRevLookup(AttribAspectEnum,pdef->offAspect));

	estrConcatf(estr," %s,",pdef->pExprMagnitude ? exprGetCompleteString(pdef->pExprMagnitude) : "0 (No Expression)");
	estrConcatf(estr," %s,",pdef->pExprDuration ? exprGetCompleteString(pdef->pExprDuration) : "0 (No Expression)");

	estrConcatf(estr,"%s\n",
		StaticDefineIntRevLookup(ModTypeEnum,pdef->eType));
}

static void modCancelPVPFlag(AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	gslPVPCleanup(pchar->pEntParent);
#endif
}

static void mod_DestroyCreatedPower(AttribMod *pmod, Character *pchar, int i)
{
	Power *ppow = pmod->ppPowersCreated[i];
	int idx = eaIndexedFindUsingInt(&pchar->modArray.ppPowers,(S32)ppow->uiID);

	if (idx >= 0 && pchar->modArray.ppPowers[idx] == ppow)
	{
		eaRemove(&pchar->modArray.ppPowers,idx);
	}
	else
	{
		if (isDevelopmentMode())
		{
			assertmsg(0, "mod_DestroyCreatedPower: Couldn't find power in mod array");
		}
		else
		{
			int s = eaSize(&pchar->modArray.ppPowers);
			ErrorDetailsf("Character %s, Array Size %d, Index %d, PowerDef %s, ID %u, AttribType %s", 
				CHARDEBUGNAME(pchar), s, idx, REF_STRING_FROM_HANDLE(ppow->hDef), ppow->uiID, 
				StaticDefineIntRevLookup(AttribTypeEnum, pmod->pDef->offAttrib));
			ErrorfForceCallstack("mod_DestroyCreatedPower: Couldn't find power in mod array");
		}
	}
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	power_Destroy(ppow, pchar);
}

#ifdef GAMESERVER
static void modCancelGrantPower(int iPartitionIdx, AttribMod *pmod, AttribMod *pmodReplace, Character *pchar, GameAccountDataExtract *pExtract)
{
	if(eaSize(&pmod->ppPowersCreated))
	{
		// check if we have a mod replacing this one, if we don't destroy the created powers
		// otherwise, we'll transfer the new mod the ppPowersCreated list so we don't have to destroy and 
		// then run character_ResetPowersArray which can be an expensive operation
		if (!pmodReplace || eaSize(&pmodReplace->ppPowersCreated) ||
			(pmod->pDef != pmodReplace->pDef))
		{	
			S32 i;
			for(i=eaSize(&pmod->ppPowersCreated)-1; i>=0; i--)
			{
				if(pmod->ppPowersCreated[i])
				{
					mod_DestroyCreatedPower(pmod, pchar, i);
				}
				else
				{
					Errorf("Found a NULL Power when attempting to cancel a GrantPower AttribMod.");
				}
			}
			eaDestroy(&pmod->ppPowersCreated);
			character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
		}
		else
		{	// transfer the powers to the mod that's replacing this one
			pmodReplace->ppPowersCreated = pmod->ppPowersCreated;
			pmod->ppPowersCreated = NULL;
		}
	}
}

static void modCancelConstantForce(int iPartitionIdx, AttribMod *pmod, AttribMod *pmodReplace, Character *pchar)
{
	if (pmodReplace && pchar)
	{	// we are being replaced by another mod, so cancel the old one
		U32 uiTimeStop = pmTimestamp(pmod->fPredictionOffset);

		pmConstantForceStop(pchar->pEntParent, pmod->uiActIDServer, uiTimeStop);

		if (pmod->fPredictionOffset)
		{
			Entity *eSource = entFromEntityRef(iPartitionIdx, pmod->erSource);

			if (eSource)
			{
				ClientCmd_PowersPredictConstantForceStop(	eSource, 
															entGetRef(pchar->pEntParent),
															pmod->uiActIDServer, 
															uiTimeStop);
			}
			

		}
	}
	
}
#endif

#ifdef GAMESERVER
static void modCancelEntCreate(int iPartitionIdx, AttribMod *pmod, AttribModDef *pdef)
{
	if(pmod->erCreated)
	{
		EntCreateParams* pParams = ((EntCreateParams*)pdef->pParams);
		// Only kill pets of AttribMods that use duration
		if((pdef->pExprDuration || pdef->eType&kModType_Duration)
			&& !(pParams->bSurviveCharacterDeathExpiration && pmod->fDuration==kModExpirationReason_CharacterDeath))
		{
			Entity *pPet = entFromEntityRef(iPartitionIdx, pmod->erCreated);
			if(pPet)
			{
				if(!pPet->pChar || entIsAlive(pPet))
				{
					if(pParams->bDieOnExpire)
					{
						if(pPet->pChar)
						{
							pPet->pChar->bKill = true;
						}
						else
						{
							entDie(pPet, -1, false, false, NULL);
						}
					}
					else
					{
						gslQueueEntityDestroy(pPet);
					}
				}
				else if(pmod->fDuration==kModExpirationReason_CharacterDeath)
				{
					// If the pet is a dead Character and the mod is expiring because the owner died, then destroy the pet
					// Remove the creator entity ref so it doesn't re-expire the mod in gslCleanupEntity
					// which calls character_CreatedEntityDestroyed if erCreator is non-zero.
					pPet->erCreator = 0;
					gslQueueEntityDestroy(pPet);
				}
				else
				{
					// If it's a Character and already marked as dead,
					//  we could re-flag it as dead, but that seems unnecessary
					// pPet->pChar->bKill = true;
					// However, if it's supposed to be destroyed when the mod is cancelled, we'll
					//  still queue up a destroy.
					if(!pParams->bDieOnExpire)
					{
						pPet->erCreator = 0;
						gslQueueEntityDestroy(pPet);
					}
				}
			}
		}
	}
}
#endif

#ifdef GAMESERVER
static void modCancelProjectileCreate(int iPartitionIdx, AttribMod *pmod, AttribModDef *pdef)
{
	if(pmod->erCreated)
	{
		ProjectileCreateParams* pParams = ((ProjectileCreateParams*)pdef->pParams);
		// Only kill projectiles of AttribMods that use duration
		if(pdef->pExprDuration || pdef->eType&kModType_Duration)
		{
			Entity *pProjectile = entFromEntityRef(iPartitionIdx, pmod->erCreated);
			if (pProjectile)
			{
				devassert(entIsProjectile(pProjectile));
				gslProjectile_Expire(pProjectile, true);
			}
		}
	}
}
#endif

#ifdef GAMESERVER
static void modCancelCombatAdvantage(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	CombatAdvantageParams *pCombatAdvantageParams = (CombatAdvantageParams*)(pmod->pDef->pParams);
	
	if (pCombatAdvantageParams->eAdvantageType == kCombatAdvantageApplyType_Advantage)
	{
		if (!pmod->erPersonal)
		{	// not a personal mod, so you get advantage to everyone
			gslCombatAdvantage_RemoveAdvantageToEveryone(pchar, pmod->uiApplyID);
		}
		else
		{	// the given pchar has advantage over the given entity
			Entity *pEnt = entFromEntityRef(iPartitionIdx, pmod->erPersonal);
			if (pEnt && pEnt->pChar)
			{
				gslCombatAdvantage_RemoveAdvantagedCharacter(pEnt->pChar, entGetRef(pchar->pEntParent), pmod->uiApplyID);
			}
		}
	}
	else
	{
		if (!pmod->erPersonal)
		{	// not a personal mod, so you get advantage to everyone
			gslCombatAdvantage_RemoveDisadvantageToEveryone(pchar, pmod->uiApplyID);
		}
		else
		{	// the personal entity has advantage over me
			gslCombatAdvantage_RemoveAdvantagedCharacter(pchar, pmod->erPersonal, pmod->uiApplyID);
		}
	}
}
#endif


// Cleans up side effects of a mod when it is done
void mod_Cancel(int iPartitionIdx, AttribMod *pmod, Character *pchar, S32 bCancelAnimFX, AttribMod *pmodReplace, GameAccountDataExtract *pExtract)
{
	int i;
	AttribModDef *pdef = pmod->pDef;

	if (pdef)
	{
		switch(pdef->offAttrib)
		{
		case kAttribType_AIAvoid:
		case kAttribType_AISoftAvoid:
			{
#ifdef GAMESERVER
				Entity *sourceEnt = entFromEntityRef(iPartitionIdx, pmod->erSource);
				if(sourceEnt)
				{
					aiNotifyPowerEnded(pchar->pEntParent, entFromEntityRef(iPartitionIdx, pmod->erSource), 
						pdef->offAttrib == kAttribType_AIAvoid ? AI_NOTIFY_TYPE_AVOID : AI_NOTIFY_TYPE_SOFT_AVOID, pmod->uiApplyID, 
						pmodReplace ? pmodReplace->uiApplyID : 0, pdef->pParams );
					if(pmodReplace)
						pmodReplace->bNotifiedAI = true;
				}
#endif
			}
			break;
		case kAttribType_AICommand: 
			{
				if(pmod->pParams && pmod->pParams->pCommandQueue)
				{
					CommandQueue_ExecuteAllCommands(pmod->pParams->pCommandQueue);
					CommandQueue_Destroy(pmod->pParams->pCommandQueue);
					pmod->pParams->pCommandQueue = NULL;
					eaDestroy(&pmod->pParams->localData);
				}
			}
			break;

		case kAttribType_AttribLink:
			{
#ifdef GAMESERVER
				AttribModLink_ModCancel(iPartitionIdx, pmod, pchar, false);
#endif
			}
			break;

		case kAttribType_AttribModFragilityHealth:
			eaFindAndRemoveFast(&pchar->ppModsAttribModFragilityHealth,pmod);
			break;
		case kAttribType_AttribModFragilityScale:
			eaFindAndRemoveFast(&pchar->ppModsAttribModFragilityScale,pmod);
			break;
		case kAttribType_AttribModShieldPercentIgnored:
			eaFindAndRemoveFast(&pchar->ppModsAttribModShieldPercentIgnored,pmod);
			break;
		case kAttribType_BecomeCritter:
#ifdef GAMESERVER
			for(i=eaSize(&pmod->ppPowersCreated)-1; i>=0; i--)
			{
				if(pmod->ppPowersCreated[i])
				{
					mod_DestroyCreatedPower(pmod, pchar, i);
				}
				else
				{
					Errorf("Found a NULL Power when attempting to cancel a BecomeCritter AttribMod.");
				}
			}
			eaDestroy(&pmod->ppPowersCreated);
			if(IS_HANDLE_ACTIVE(pchar->hClassTemporary))
			{
				REMOVE_HANDLE(pchar->hClassTemporary);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				character_SetClassCallback(pchar->pEntParent, pExtract);
			}
			character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
#endif
			break;

		case kAttribType_ConstantForce:
#ifdef GAMESERVER
			modCancelConstantForce(iPartitionIdx, pmod, pmodReplace, pchar);
#endif
			break;

		case kAttribType_DisableTacticalMovement:
#ifdef GAMESERVER
			{
				if (!pmodReplace)
				{
					DisableTacticalMovementParams* pParams = ((DisableTacticalMovementParams*)pdef->pParams);
					U32 uiID = pmod->uiPowerID | (pmod->pDef->uiDefIdx << POWERID_BASE_BITS);
					mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical, uiID, pmTimestamp(0));
				}
			}
#endif
			break;
		case kAttribType_EntCreate:
#ifdef GAMESERVER
			modCancelEntCreate(iPartitionIdx, pmod, pdef);
#endif
			break;
		case kAttribType_EntCreateVanity:
			if(pmod->erCreated)
			{
				Entity *pPet = entFromEntityRef(iPartitionIdx,pmod->erCreated);
				if(pPet)
				{
#ifdef GAMESERVER
					gslQueueEntityDestroy(pPet);
#endif
				}
			}
			break;
		case kAttribType_Faction:
#ifdef GAMESERVER
			{
				FactionParams *pParams = (FactionParams*)pdef->pParams;
				if(pParams)
				{
					CritterFaction *pfaction = GET_REF(pParams->hFaction);
					Entity *e = pchar->pEntParent;
					if(pfaction && e)
					{
						if(pfaction==GET_REF(e->hPowerFactionOverride))
						{
							gslEntity_ClearFaction(e, kFactionOverrideType_POWERS);
						}
					}
				}
			}
#endif
			break;
		case kAttribType_Flag:
			{
				FlagParams *pParams = (FlagParams*)pdef->pParams;
				if(pParams)
				{
					Entity *e = pchar->pEntParent;
					if(e)
					{
						int iReplaceFlags = 0;
						if(pmodReplace && pmodReplace->pDef)
						{
							FlagParams *pReplaceParams = (FlagParams*)pdef->pParams;
							if(pReplaceParams)
							{
								iReplaceFlags = pParams->eFlags;
							}
						}

						if(pParams->eFlags & kFlagAttributeFlags_Untargetable &&
							(!iReplaceFlags || !(iReplaceFlags & kFlagAttributeFlags_Untargetable)))
						{
							entClearDataFlagBits(e,ENTITYFLAG_UNTARGETABLE);
						}
						if(pParams->eFlags & kFlagAttributeFlags_Unkillable && 
							(!iReplaceFlags || !(iReplaceFlags & kFlagAttributeFlags_Unkillable)))
						{
							pchar->bUnkillable = false;
							entity_SetDirtyBit(e, parse_Character, pchar, false);
						}
						if(pParams->eFlags & kFlagAttributeFlags_Unselectable && 
							(!iReplaceFlags || !(iReplaceFlags & kFlagAttributeFlags_Unselectable)))
						{
							entClearDataFlagBits(e,ENTITYFLAG_UNSELECTABLE);
						}
					}
				}
			}
			break;

		case kAttribType_ProjectileCreate:
			{
#ifdef GAMESERVER
				modCancelProjectileCreate(iPartitionIdx, pmod, pdef);
#endif 
			} 
			break;

		case kAttribType_PVPFlag:
			{
				modCancelPVPFlag(pmod, pchar);
			}
			break;
		case kAttribType_SubtargetSet:
			{
				SubtargetSetParams *pParams = (SubtargetSetParams*)pdef->pParams;
				if(pParams && pchar->pSubtarget && GET_REF(pchar->pSubtarget->hCategory)==GET_REF(pParams->hCategory))
				{
					character_ClearSubtarget(pchar);
				}
			}
			break;
		case kAttribType_Taunt:
			i = eaFindAndRemoveFast(&pchar->ppModsTaunt,pmod);
			if(i==0 && pchar->bTauntActive)
			{
				pchar->bTauntActive = false;
			}
			break;
		case kAttribType_EntAttach:
			if(pmod->erCreated)
			{
				Entity *pPet = entFromEntityRef(iPartitionIdx,pmod->erCreated);
#ifdef GAMESERVER
				gslQueueEntityDestroy(pPet);
#endif
			}
			break;
		case kAttribType_GrantPower:
#ifdef GAMESERVER
				modCancelGrantPower(iPartitionIdx, pmod, pmodReplace, pchar, pExtract);
#endif
			break;
		case kAttribType_Shield:
			{
				eaFindAndRemove(&pchar->ppModsShield,pmod);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}
			break;
		case kAttribType_AttribOverride:
#ifdef GAMESERVER
			if(((AttribOverrideParams *)pdef->pParams)->offAttrib == kAttribType_HitPoints)
			{
				(pchar)->bCanRegen[0] = true;
			}
			else if(((AttribOverrideParams *)pdef->pParams)->offAttrib == kAttribType_Power)
			{
				(pchar)->bCanRegen[1] = true;
			}
			else if(((AttribOverrideParams *)pdef->pParams)->offAttrib == kAttribType_Air)
			{
				(pchar)->bCanRegen[2] = true;
			}
#endif
			break;
		case kAttribType_BePickedUp:
			{
				if(pmod->fDurationOriginal>0)
				{
					if(pmod->erSource && pmod->erSource==pchar->erHeldBy)
					{
						Entity *pentSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
						EntityRef erTarget = entGetRef(pchar->pEntParent);
						if(pentSource && pentSource->pChar && pentSource->pChar->erHeld==erTarget)
						{
							// TODO(JW): Probably need to do real soft drop stuff here?
							pentSource->pChar->erHeld = 0;
							entity_SetDirtyBit(pentSource, parse_Character, pentSource->pChar, false);
							pchar->erHeldBy = 0;
							entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
						}
					}
				}
			}
			break;
		case kAttribType_Ride:
			{
#ifdef GAMESERVER
				Entity *pentSource = entFromEntityRef(iPartitionIdx, pmod->erSource);
				EntityRef erMe = entGetRef(pchar->pEntParent);
				if (entGetMount(pentSource) == pchar->pEntParent)
				{
					gslEntCancelRide(pentSource);
				}
#endif
			}
			break;
		case kAttribType_SetCostume:
			{
				eaFindAndRemove(&pchar->ppCostumeChanges,pmod);
			}
			break;
		case kAttribType_ModifyCostume:
			{
				eaFindAndRemove(&pchar->ppCostumeModifies,pmod);
			}
			break;
		case kAttribType_AIAggroTotalScale:
			{
				eaFindAndRemoveFast(&pchar->ppModsAIAggro, pmod);
			}
			break;
		case kAttribType_CombatAdvantage:
			{
#ifdef GAMESERVER
				modCancelCombatAdvantage(iPartitionIdx, pmod, pchar);
#endif
			} 
			break;

		}
	}

	// If the unthinkable happens and an AttribMod still has created Powers sitting around clean them up
	//  Don't bother if there's nothing in the modArray's ppPowers though, which generally means the
	//  Character never got fully loaded from the DB (which would also generally mean that pmod->pDef
	//  is NULL)
	if(eaSize(&pmod->ppPowersCreated) && eaSize(&pchar->modArray.ppPowers))
	{
		Power* ppow = pmod->ppPowersCreated[0];
		ErrorDetailsf("Character %s, Mod PowerDef %s, AttribType %s, Created PowerDef %s", 
			CHARDEBUGNAME(pchar), REF_STRING_FROM_HANDLE(pmod->hPowerDef), 
			StaticDefineIntRevLookup(AttribTypeEnum, pmod->pDef->offAttrib),
			REF_STRING_FROM_HANDLE(ppow->hDef));
		devassertmsg(0,"AttribMod created Powers and didn't clean up after itself");
		
		for(i=eaSize(&pmod->ppPowersCreated)-1; i>=0; i--)
		{
			if(pmod->ppPowersCreated[i])
			{
				mod_DestroyCreatedPower(pmod, pchar, i);
			}
		}
		eaDestroy(&pmod->ppPowersCreated);
		character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
	}

	if(bCancelAnimFX && pdef && pdef->bHasAnimFX)
	{
		mod_AnimFXOff(pmod,pchar);
	}

	if (pdef && pdef->offAttrib != kAttribType_AttribLink && pmod->pAttribModLink)
		AttribModLink_LinkedAttribCancel(iPartitionIdx, pchar, pmod);

	if (pmodReplace && pmod->bProcessedDisplayNameTraker && 
		pmodReplace->pDef == pmod->pDef)
	{
		pmodReplace->bProcessedDisplayNameTraker = true;
	}

	return;
}

// Sorting function for setting higher priority attrib mods first
int attrib_sortfunc(const AttribMod **pmodA, const AttribMod **pmodB)
{
	// Magnitude wins
	if((*pmodA)->fMagnitude > (*pmodB)->fMagnitude)
		return 1;
	else if((*pmodA)->fMagnitude < (*pmodB)->fMagnitude)
		return -1;

	// Duration comes second
	if((*pmodA)->fDuration > (*pmodB)->fDuration)
		return 1;
	else if((*pmodA)->fDuration < (*pmodB)->fDuration)
		return -1;

	// Next, sort by apply ID
	if((*pmodA)->uiApplyID > (*pmodB)->uiApplyID)
		return 1;
	else if((*pmodA)->uiApplyID < (*pmodB)->uiApplyID)
		return -1;

	// If they're from the same application, sort by the def index.  Lower index wins.
	if((*pmodA)->uiDefIdx > (*pmodB)->uiDefIdx)
		return 1;
	else if((*pmodA)->uiDefIdx < (*pmodB)->uiDefIdx)
		return -1;

	return 0;
}

// Counts similar AttribMods, stores them in the given StashTable* (which it creates
//  if it doesn't exist)
void moddef_RecordStaticPerf(AttribModDef *pdef, StashTable *pstStaticPerfs)
{
	static StashTable s_stAttribs = NULL;
	static StashTable s_stAspects = NULL;

	StaticCmdPerf* p;
	const char *pchAttrib;
	const char *pchAspect;
	char perfName[MAX_PATH];

    // Collection of this data is expensive, so make it conditional rather than just do it every time we are profiling.
    if ( !s_bEnableExtraAttribmodProfileData )
        return;

	PERFINFO_AUTO_START_FUNC();

	if(!s_stAttribs)
	{
		s_stAttribs = stashTableCreateInt(100);
		s_stAspects = stashTableCreateInt(10);
	}

	if(!stashIntFindPointerConst(s_stAttribs,pdef->offAttrib+1,&pchAttrib))
	{
		char *pchTemp;
		pchAttrib = StaticDefineIntRevLookupNonNull(AttribTypeEnum,pdef->offAttrib);
		pchTemp = strdup(pchAttrib);
		stashIntAddPointer(s_stAttribs,pdef->offAttrib+1,pchTemp,false);
	}

	if(!stashIntFindPointerConst(s_stAspects,pdef->offAspect+1,&pchAspect))
	{
		char *pchTemp;
		pchAspect = StaticDefineIntRevLookupNonNull(AttribAspectEnum,pdef->offAspect);
		pchTemp = strdup(pchAspect);
		stashIntAddPointer(s_stAspects,pdef->offAspect+1,pchTemp,false);
	}

	strcpy(perfName, pchAttrib);
	strcat_trunc(perfName, " ");
	strcat_trunc(perfName, pchAspect);

	if(!*pstStaticPerfs)
		*pstStaticPerfs = stashTableCreateWithStringKeys(100,StashDefault);
	
	if(!stashFindPointer(*pstStaticPerfs, perfName, &p))
	{
		p = callocStruct(StaticCmdPerf);
		p->name = strdup(perfName);
		stashAddPointer(*pstStaticPerfs, p->name, p, false);
	}

	START_MISC_COUNT_STATIC(0,p->name,&p->pi);
	STOP_MISC_COUNT(1);

	PERFINFO_AUTO_STOP();
}

void moddef_PostTextReadFixup(AttribModDef *pdef, PowerDef *ppowDef)
{
	int i;
	for (i=eaSize(&pdef->ppchAttribModDefStanceWordText)-1; i>=0; --i)
	{
		if (!dynAnimStanceValid(pdef->ppchAttribModDefStanceWordText[i]))
		{
			Errorf("AttribModeDef references invalid stance word: %s", pdef->ppchAttribModDefStanceWordText[i]);
		}
	}
	if ((ppowDef->eActivateRules & (kPowerActivateRules_SourceAlive|kPowerActivateRules_SourceDead)) == (kPowerActivateRules_SourceAlive|kPowerActivateRules_SourceDead))
	{
		pdef->bSurviveTargetDeath = true;
	}
}

// --------------------------------------------------------------------------------------------------------------------
S32 moddef_IsPredictedAttrib(AttribModDef *pdef)
{
	return pdef->offAspect==kAttribAspect_BasicAbs
		&& (pdef->offAttrib==kAttribType_KnockBack
			|| pdef->offAttrib==kAttribType_KnockUp
			|| pdef->offAttrib==kAttribType_Repel 
			|| pdef->offAttrib==kAttribType_ConstantForce
			|| pdef->offAttrib==kAttribType_Root
			|| pdef->offAttrib==kAttribType_Hold);
}

// --------------------------------------------------------------------------------------------------------------------
// returns true if the moddef has a powerDef is can apply somehow (expiration def, apply powers, triggers, etc)
S32 moddef_HasUnownedPowerApplication(SA_PARAM_NN_VALID AttribModDef *pdef)
{
	if (pdef->pExpiration)
	{	// has an expiration def
		return true;
	}

	if (IS_SPECIAL_ATTRIB(pdef->offAttrib) && IS_BASIC_ASPECT(pdef->offAspect) && pdef->pParams)
	{	// if it's a special attrib, check the types we wish to attach to and try their powerDefs
		switch(pdef->offAttrib)
		{
			case kAttribType_TriggerSimple:
			case kAttribType_ApplyPower:
			case kAttribType_TriggerComplex:
			case kAttribType_DamageTrigger:
			case kAttribType_KillTrigger:
			{
				return true;
			}
		}
	}

	return false;
}

// --------------------------------------------------------------------------------------------------------------------
void AttribModLink_Free(AttribModLink *pLink)
{
	if (pLink)
	{
		MP_FREE(AttribModLink, pLink);
	}
}

// --------------------------------------------------------------------------------------------------------------------
static void AttribModLink_LinkMods(AttribMod *pMod, AttribMod *pAttribLinkMod, 
									Character *pCharTarget, Character *pCharSource)
{
	AttribModLink *pLink = MP_ALLOC(AttribModLink);
	pLink->uiLinkedApplyID = pMod->uiApplyID;
	pLink->uiLinkedDefIdx = pMod->uiDefIdx;
	pLink->uiLinkedActIDServer = pMod->uiActIDServer;
	pLink->erTarget = entGetRef(pCharTarget->pEntParent);
	eaPush(&pAttribLinkMod->eaAttribModLinks, pLink);

	// add the link to the mod being tracked back to the pLinkAttrib
	pLink = MP_ALLOC(AttribModLink);
	pLink->uiLinkedApplyID = pAttribLinkMod->uiApplyID;
	pLink->uiLinkedDefIdx = pAttribLinkMod->uiDefIdx;
	pLink->uiLinkedActIDServer = pAttribLinkMod->uiActIDServer;
	pLink->erTarget = entGetRef(pCharSource->pEntParent);
	pMod->pAttribModLink = pLink;
}

// --------------------------------------------------------------------------------------------------------------------
static void AttribModLink_PostApplyModsFromPowerDef(PowerDef *pPowerDef, AttribMod **ppLinkedMods, Character *pCharTarget, Character *pCharSource)
{
	// linked attribs
	S32 iSize = eaSize(&ppLinkedMods);
	AttribMod *pLinkAttrib = NULL;
	F32 fMaxDelay = 0.f;

	// find the link attrib
	FOR_EACH_IN_EARRAY(ppLinkedMods, AttribMod, pMod)
	{
		if (pMod->pDef->offAttrib == kAttribType_AttribLink)
		{
			pLinkAttrib = pMod;
			eaRemoveFast(&ppLinkedMods, FOR_EACH_IDX(-, pMod));
			--iSize;
			break;
		}
	}
	FOR_EACH_END
	
	if (!pLinkAttrib)
	{
		if (pPowerDef)
			Errorf("%s: Power has Linked Mods but no AttribLink type created!", pPowerDef->pchFile);
		return;
	}

	if (!iSize)
	{
		if (pPowerDef)
			Errorf("%s: Power created AttribLink but no linked mods were created!", pPowerDef->pchFile);
		return;
	}

	fMaxDelay = pLinkAttrib->fTimer;

	eaSetCapacity(&pLinkAttrib->eaAttribModLinks, iSize);

	// apply the self mods first, then target mods
	if (pCharSource)
	{
		FOR_EACH_IN_EARRAY(ppLinkedMods, AttribMod, pMod)
		{
			if (pMod->pDef->eTarget != kModTarget_Target)
			{
				AttribModLink_LinkMods(pMod, pLinkAttrib, pCharSource, pCharSource);
				eaRemoveFast(&ppLinkedMods, FOR_EACH_IDX(-, pMod));
			}
		}
		FOR_EACH_END
	}

	FOR_EACH_IN_EARRAY(ppLinkedMods, AttribMod, pMod)
	{
		if (pMod->pDef->eTarget == kModTarget_Target)
		{
			F32 fDelay = pMod->fTimer + pMod->fPredictionOffset - pCharTarget->fTimeSlept;
			AttribModLink_LinkMods(pMod, pLinkAttrib, pCharTarget, pCharSource);
			MAX1(fMaxDelay, fDelay);
		}
	}
	FOR_EACH_END

	// we need to delay the link attrib until after all the other attribs have a chance to go active
	// so we can properly do any stacking rules with the linked attrib
	pLinkAttrib->fTimer = fMaxDelay + 0.1f;

	eaClear(&ppLinkedMods);
}

// --------------------------------------------------------------------------------------------------------------------
AttribMod* AttribModLink_FindMod(AttribMod **eaMods, AttribModLink *pLink, S32 *piIndexOut)
{
	FOR_EACH_IN_EARRAY(eaMods, AttribMod, pOtherMod)
	{
		if (pOtherMod->uiApplyID == pLink->uiLinkedApplyID && 
			pOtherMod->uiActIDServer == pLink->uiLinkedActIDServer && 
			pOtherMod->uiDefIdx == pLink->uiLinkedDefIdx && 
			pOtherMod->fDuration > 0.f)
		{	// found a valid mod
			if (piIndexOut) *piIndexOut = FOR_EACH_IDX(-, pOtherMod);
			return pOtherMod;
		}
	}
	FOR_EACH_END
		
	if (piIndexOut) *piIndexOut = -1;

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
static bool AttribModLink_HasAnyModsInList(	SA_PARAM_NN_VALID Entity *pTargetEnt, 
											SA_PARAM_OP_VALID Character *pSelf, 
											SA_PARAM_NN_VALID AttribMod *pMod, 
											bool bPendingList)
{
	S32 iSize = eaSize(&pMod->eaAttribModLinks);
	S32 i;
	AttribMod **eaMods = bPendingList ? pTargetEnt->pChar->modArray.ppModsPending : pTargetEnt->pChar->modArray.ppMods;

	// check mods on the target
	for (i = iSize - 1; i >= 0; --i) 
	{
		AttribModLink *pLink = pMod->eaAttribModLinks[i];
		if (pLink->erTarget == pTargetEnt->myRef)
		{
			if (AttribModLink_FindMod(eaMods, pLink, NULL))
				return true;
		}
		else break;
	}

	if (pSelf)
	{
		eaMods = bPendingList ? pSelf->modArray.ppModsPending : pSelf->modArray.ppMods;
		// check mods on self
		for (; i >= 0; --i) 
		{
			AttribModLink *pLink = pMod->eaAttribModLinks[i];
			if (AttribModLink_FindMod(pSelf->modArray.ppMods, pLink, NULL))
				return true;
		}
	}

	return false;
}

// --------------------------------------------------------------------------------------------------------------------
// called from within character_ModsProcessPending when the attribMod is about to go current.
// we need to make sure that there's at least one mod on the target that went active
// if not we need to make sure there's at least one mod pending- otherwise we destroy the mod 
static bool AttribModLink_ProcessPendingGoingCurrent(int iPartition, AttribMod *pMod, Character *pchar, S32 *pbDestroyOut)
{
	S32 iSize = eaSize(&pMod->eaAttribModLinks);
	if (iSize)
	{
		Entity *pTargetEnt = entFromEntityRef(iPartition, pMod->eaAttribModLinks[iSize-1]->erTarget);
		if (pTargetEnt && pTargetEnt->pChar && pTargetEnt->pChar != pchar)
		{
			if (AttribModLink_HasAnyModsInList(pTargetEnt, NULL, pMod, false))
				return true;

			if (!AttribModLink_HasAnyModsInList(pTargetEnt, NULL, pMod, true))
			{	// all of the mods were removed for this link 
				if (pbDestroyOut) *pbDestroyOut = true;
			}
			else
			{
				character_Wake(pchar);
			}

		}
	}
	
	return false;
}

// --------------------------------------------------------------------------------------------------------------------
void AttribModLink_ModProcess(int iPartition, AttribMod *pMod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		Entity *pTargetEnt = NULL;
		EntityRef erSelf = entGetRef(pchar->pEntParent);
		S32 iSize = eaSize(&pMod->eaAttribModLinks);

		if (iSize)
		{
			pTargetEnt = entFromEntityRef(iPartition, pMod->eaAttribModLinks[iSize-1]->erTarget);

			if (pTargetEnt && pTargetEnt->pChar)
			{
				S32 iTargetModsFound = 0;

				devassert(pTargetEnt->pChar != pchar);

				// check to see if the attribs linked are still alive
				// if not, expire this mod
				FOR_EACH_IN_EARRAY(pMod->eaAttribModLinks, AttribModLink, pLink)
				{
					devassert(erSelf == pLink->erTarget || entGetRef(pTargetEnt) == pLink->erTarget);
					if (entGetRef(pTargetEnt) == pLink->erTarget)
					{
						if (AttribModLink_FindMod(pTargetEnt->pChar->modArray.ppMods, pLink, NULL))
						{
							iTargetModsFound++;
							continue;
						}
						else if (AttribModLink_FindMod(pTargetEnt->pChar->modArray.ppModsPending, pLink, NULL))
						{
							iTargetModsFound++;
							continue;
						}
						else
						{	// attrib was not found, kill the link
							AttribModLink_Free(pLink);
							eaRemove(&pMod->eaAttribModLinks, FOR_EACH_IDX(-, pLink));
						}
					} else break; // the self-linked mods are at the start of the list
				}
				FOR_EACH_END

				// have linked mods on the target still, don't expire this yet
				if (iTargetModsFound > 0)
					return;
			}
		}

		// if we get here, then this link attrib will be expiring as we did not find any linked mods
		character_ModExpireReason(pchar, pMod, kModExpirationReason_Unset);
	}


	PERFINFO_AUTO_STOP();
}


// --------------------------------------------------------------------------------------------------------------------
static void AttribModLink_ExpireLinkedMod(Character *pchar, AttribModLink *pLink, bool bInProcessingPending)
{
	AttribMod *pMod = AttribModLink_FindMod(pchar->modArray.ppMods, pLink, NULL);
	if (pMod)
	{
		character_ModExpireReason(pchar, pMod, kModExpirationReason_AttribLinkExpire);
	}
	else 
	{
		S32 iIdx = -1;
		pMod = AttribModLink_FindMod(pchar->modArray.ppModsPending, pLink, &iIdx);
		if (pMod)
		{	// on the pending list, we have to destroy it now
			// if we are currently in ModsProcessPending, do not destroy the mod immediately,
			// flag it expired and we'll clean it up when we're done canceling all the mods
			if (!bInProcessingPending)
			{
				devassert(iIdx >= 0);
				eaRemove(&pchar->modArray.ppModsPending, iIdx);
				mod_Destroy(pMod);
			}
			else 
			{
				pMod->fDuration = kModExpirationReason_AttribLinkExpire;
			}
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
static void AttribModLink_ModCancel(int iPartitionIdx, AttribMod *pmod, Character *pchar, bool bInProcessingPending)
{
	EntityRef erSourceRef = entGetRef(pchar->pEntParent);
	S32 iSize;

	PERFINFO_AUTO_START_FUNC();

	iSize = eaSize(&pmod->eaAttribModLinks);
	if (iSize)
	{
		AttribModLink *pLink = NULL;
		S32 i;
		// expire the ones on the source
		for (i = 0; i < iSize; ++i)
		{
			pLink = pmod->eaAttribModLinks[i];

			if (pLink->erTarget != erSourceRef)
			{
				break;				
			}

			AttribModLink_ExpireLinkedMod(pchar, pLink, bInProcessingPending);
		}

		// expire the ones on the target
		if (i < iSize)
		{
			Entity *pTarget = NULL;

			pLink = pmod->eaAttribModLinks[i];

			pTarget = entFromEntityRef(iPartitionIdx, pLink->erTarget);

			if (pTarget && pTarget->pChar)
			{
				for (; i < iSize; ++i)
				{
					pLink = pmod->eaAttribModLinks[i];
					AttribModLink_ExpireLinkedMod(pTarget->pChar, pLink, false);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void AttribModLink_LinkedAttribCancel(int iPartitionIdx, Character *pChar, AttribMod *pMod)
{
	// this is linked to another attrib - notify that attrib that this one has went away
	Entity *pSource = entFromEntityRef(iPartitionIdx, pMod->pAttribModLink->erTarget);
	if (pSource && pSource->pChar)
	{
		AttribMod *pLinkedMod = AttribModLink_FindMod(pSource->pChar->modArray.ppMods, pMod->pAttribModLink, NULL);

		if (pLinkedMod)
		{
			S32 iSize = eaSize(&pLinkedMod->eaAttribModLinks);
			EntityRef erChar = entGetRef(pChar->pEntParent);
			EntityRef erSource = entGetRef(pSource);
			bool bOtherModsExist = false;
			bool bFound = false;
			S32 i;

			for (i = iSize - 1; i >= 0; --i)
			{
				AttribModLink *pLink = pLinkedMod->eaAttribModLinks[i];
				if (pLink->erTarget == erChar)
				{
					if (!bFound && 
						pLink->uiLinkedApplyID == pMod->uiApplyID && 
						pLink->uiLinkedActIDServer == pMod->uiActIDServer && 
						pLink->uiLinkedDefIdx == pMod->uiDefIdx)
					{
						// the start of the list has the self-targeted mods, and the end of the list
						// has the mods placed on the target. 
						// if we're removing from the target we can do a removeFast
						if (erSource != pLink->erTarget) 
							eaRemoveFast(&pLinkedMod->eaAttribModLinks, i);
						else 
							eaRemove(&pLinkedMod->eaAttribModLinks, i);

						MP_FREE(AttribModLink, pLink);

						bFound = true;
						// if we already found there is another mod on the target, break out
						// we know we won't need to expire the kAttribType_AttribLink
						if (bOtherModsExist) 
							break;
					}
					else
					{
						bOtherModsExist = true;

						// if we already found the mod on the target, break out
						// we know we won't need to expire the kAttribType_AttribLink
						if (bFound) 
							break;
					}
				}
			}

			// if there are more mods, wake up the linked source 
			if (bOtherModsExist == false)
			{	
				character_ModExpireReason(pSource->pChar, pLinkedMod, kModExpirationReason_Unset);
			}
		}
	}

}

#include "AutoGen/AttribMod_h_ast.c"
