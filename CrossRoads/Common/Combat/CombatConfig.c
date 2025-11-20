/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CombatConfig.h"
#include "CombatConfig_h_ast.h"

#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"

#include "CharacterAttribs.h"
#include "CombatEval.h"
#include "PowerAnimFX.h"
#include "PowersEnums_h_ast.h"
#include "qsortG.h"
#include "WorldGrid.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void combatConfig_ValidateSpecialCritAttrib();

CombatConfig g_CombatConfig;
#define COMBAT_CONFIG_FILENAME	"defs/config/CombatConfig.def"
static void CombatConfig_FixupTacticalRequesterConfig(TacticalRequesterConfig *pTactical)
{
	if(pTactical->aim.aimDef.fSpeedScaleCrouch < 0)
	{
		pTactical->aim.aimDef.fSpeedScaleCrouch = 1;
	}

	if(pTactical->bRollDisableDuringPowers || 
		pTactical->bAimDisableDuringPowers || 
		pTactical->bSprintDisableDuringPowers)
	{
		pTactical->bTacticalDisableDuringPowers = true;
	}

	if (pTactical->bRollDisableDuringCombat ||
		pTactical->bAimDisableDuringCombat ||
		pTactical->bSprintDisableDuringCombat)
	{
		pTactical->bTacticalDisableDuringCombat = true;
	}

	if (pTactical->aim.eAimCostAttrib && !IS_NORMAL_ATTRIB(pTactical->aim.eAimCostAttrib))
	{
		ErrorFilenamef(COMBAT_CONFIG_FILENAME, "TacticalAimConfig: AimCostAttrib must be a normal attrib.");
		pTactical->aim.eAimCostAttrib = 0;
	}

	if (pTactical->roll.eRollCostAttrib && !IS_NORMAL_ATTRIB(pTactical->roll.eRollCostAttrib))
	{
		ErrorFilenamef(COMBAT_CONFIG_FILENAME, "TacticalAimConfig: RollCostAttrib must be a normal attrib.");
		pTactical->roll.eRollCostAttrib = 0;
	}

#if GAMESERVER || GAMECLIENT
	mrFixupTacticalRollDef(&pTactical->roll.rollDef);
	mrFixupTacticalSprintDef(&pTactical->sprint.sprintDef);
#endif
}

static void CombatConfig_FixupCombatAdvantageConfig(CombatAdvantageConfig *pConfig)
{
	if (!pConfig->fStrengthBonusMag)
	{
		ErrorFilenamef(COMBAT_CONFIG_FILENAME, "CombatAdvantage: StrengthBonusMag must be set to a non-zero value.");
	}
		
	if (pConfig->fFlankingDistance <= 0)
	{
		ErrorFilenamef(COMBAT_CONFIG_FILENAME, "CombatAdvantage: FlankingDistance not specified or not greater than zero.");
	}

	if (pConfig->fFlankAngleTolerance <= 0)
	{
		ErrorFilenamef(COMBAT_CONFIG_FILENAME, "CombatAdvantage: FlankAngleTolerance not specified or not greater than zero.");
	}
}

static void CombatConfigPostLoad(bool bGenerateExpressions)
{
	if(g_CombatConfig.pDamageDecay
		&& (g_CombatConfig.pDamageDecay->uiDelayDiscard
			|| g_CombatConfig.pDamageDecay->fPercentDiscard))
	{
		g_CombatConfig.pDamageDecay->bRunEveryTick = true;
	}

	if(g_CombatConfig.bUseNormalizedDodge && g_CombatConfig.fNormalizedDodgeVal <= 0.f)
	{
		/*TODO(BH): Commented out after normalized dodge was turned on
			loadupdate_printf("\n%s in %s is an invalid value [%.02f].  Overriding with [%.02f].\n   ..",
			"NormalizedDodgeVal",
			"CombatConfig",
			g_CombatConfig.fNormalizedDodgeVal,
			1.0f);
			*/
		g_CombatConfig.fNormalizedDodgeVal = 1.0f;
	}

	if(g_CombatConfig.iTrayMaxSize <= 0 || g_CombatConfig.iTrayMaxSize > TRAY_SIZE_MAX)
	{
		ErrorFilenamef(COMBAT_CONFIG_FILENAME, "Tray size is must be [1..%d] (currently %d)", TRAY_SIZE_MAX, g_CombatConfig.iTrayMaxSize);
		g_CombatConfig.iTrayMaxSize = TRAY_SIZE_MAX;
	}

	if (g_CombatConfig.pCombatAdvantage)
	{
		g_CombatConfig.pCombatAdvantage->fFlankingDotProductTolerance = SQR(cos(g_CombatConfig.pCombatAdvantage->fFlankAngleTolerance/2*PI/180));
	}

	if (g_CombatConfig.pCombatAdvantage)
		CombatConfig_FixupCombatAdvantageConfig(g_CombatConfig.pCombatAdvantage);

	CombatConfig_FixupTacticalRequesterConfig(&g_CombatConfig.tactical);

	combatConfig_ValidateSpecialCritAttrib();

	// Validate the falling parameters
	if (g_CombatConfig.pchFallingDamagePower &&
		g_CombatConfig.pchFallingDamagePower[0] &&
		!g_CombatConfig.bFallingDamageIsFatal)
	{
		ErrorFilenamef(COMBAT_CONFIG_FILENAME, 
			"FallingDamageIsFatal flag value will be ignored because you've used a falling damage power. "
			"Please set the FallingDamageIsFatal to 1 in the combat config to get rid of this error.");
	}

	if (g_CombatConfig.pPowerActivationImmunities)
	{
		if (!g_CombatConfig.pPowerActivationImmunities->pExprAffectsMod)
		{
			ErrorFilenamef(COMBAT_CONFIG_FILENAME, "PowerActivationImmunities specified but the AffectsMod expression was not specified.");
			StructDestroy(parse_PowerActivationImmunities, g_CombatConfig.pPowerActivationImmunities);
			g_CombatConfig.pPowerActivationImmunities = NULL;

		}
	}


	if (bGenerateExpressions)
		CombatConfig_PostCombatEvalGenerateExpressions();
}

void CombatConfig_PostCombatEvalGenerateExpressions()
{
	if (g_CombatConfig.pPowerActivationImmunities && g_CombatConfig.pPowerActivationImmunities->pExprAffectsMod)
	{
		combateval_Generate(g_CombatConfig.pPowerActivationImmunities->pExprAffectsMod,kCombatEvalContext_Affects);
	}
}

// Reload CombatConfig top level callback, not particularly safe/correct
static void CombatConfigReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading %s...","CombatConfig");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	StructInit(parse_CombatConfig, &g_CombatConfig);
	ParserLoadFiles(NULL,"defs/config/CombatConfig.def","CombatConfig.bin",PARSER_OPTIONALFLAG,parse_CombatConfig,&g_CombatConfig);
	CombatConfigPostLoad(true);

	loadend_printf(" done.");
}

AUTO_STARTUP(CombatConfig) ASTRT_DEPS(AS_CharacterClassTypes, PowerCategories);
void CombatConfigLoad(void)
{
	loadstart_printf("Loading %s...","CombatConfig");

	//Fill-in the default values
	StructInit(parse_CombatConfig, &g_CombatConfig);

	ParserLoadFiles(NULL,"defs/config/CombatConfig.def","CombatConfig.bin",PARSER_OPTIONALFLAG,parse_CombatConfig,&g_CombatConfig);

	CombatConfigPostLoad(false);

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/CombatConfig.def", CombatConfigReload);
	}

	loadend_printf(" done.");
}

//Command to change the range check on the fly
AUTO_COMMAND;
void Combat_HitOutOfRange(bool bActivate)
{
	g_CombatConfig.bDisableOutOfRange = !bActivate;
}

// Returns true if BattleForm is optional (based on current map name)
S32 combatconfig_BattleFormOptional(void)
{
	S32 i;
	if(g_CombatConfig.pBattleForm && (i=eaSize(&g_CombatConfig.pBattleForm->ppchMapsOptional)))
	{
		const char *pchMapName = zmapInfoGetCurrentName(NULL);
		for(i=i-1; i>=0; i--)
		{
			if(!stricmp(g_CombatConfig.pBattleForm->ppchMapsOptional[i],pchMapName))
			{
				return true;
			}
		}
	}
	return false;
}

// Returns the tactical disable flags set during combat
TacticalDisableFlags combatconfig_GetTacticalDisableFlagsForCombat(void)
{
	TacticalDisableFlags flags = 0;

	if (g_CombatConfig.tactical.bRollDisableDuringCombat)
		flags |= TDF_ROLL;
	if (g_CombatConfig.tactical.bAimDisableDuringCombat)
		flags |= TDF_AIM;
	if (g_CombatConfig.tactical.bSprintDisableDuringCombat)
		flags |= TDF_SPRINT;

	return flags;
}

static void combatConfig_ValidateSpecialCritAttrib() 
{
	// make each has both the 
	FOR_EACH_IN_EARRAY(g_CombatConfig.specialAttribModifiers.eaSpecialCriticalAttribs, SpecialCriticalAttribs, pCritAttribs)
	{
		if (pCritAttribs->eCriticalChance == -1 || pCritAttribs->eCriticalSeverity == -1)
		{
			ErrorFilenamef(COMBAT_CONFIG_FILENAME, "CriticalAttrib: Both CriticalChance and CriticalSeverity must be assigned valid attributes.");
		}
	}
	FOR_EACH_END
}

bool combatConfig_FindSpecialCritAttrib(AttribType eType, AttribType *peCritChanceOut, AttribType *peCritSeverityOut)
{
	FOR_EACH_IN_EARRAY(g_CombatConfig.specialAttribModifiers.eaSpecialCriticalAttribs, SpecialCriticalAttribs, pCritAttribs)
	{
		S32 numAttribs = eaiSize(&pCritAttribs->eaAttribs);
		S32 i;

		for (i = numAttribs - 1; i >= 0; --i)
		{
			AttribType critAttrib = pCritAttribs->eaAttribs[i];
			if (eType == critAttrib)
			{
				(*peCritChanceOut) = pCritAttribs->eCriticalChance;
				(*peCritSeverityOut) = pCritAttribs->eCriticalSeverity;
				return true;
			}
			else if(IS_SPECIAL_ATTRIB(critAttrib)) 
			{	// see if it's an attribSet
				AttribType *pAttribs = attrib_Unroll(critAttrib);
				if (pAttribs)
				{	
					if (eaiFind(&pAttribs, eType) != -1)
					{
						(*peCritChanceOut) = pCritAttribs->eCriticalChance;
						(*peCritSeverityOut) = pCritAttribs->eCriticalSeverity;
						return true;
					}
				}
			}
		}
	}
	FOR_EACH_END

	return false;
}


#include "AutoGen/CombatConfig_h_ast.c"