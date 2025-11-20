/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CharacterAttribs.h"

#include "CostumeCommon.h"
#include "Entity.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntityLib.h"
#include "encounter_common.h"
#include "EString.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameAccountDataCommon.h"
#include "itemCommon.h"
#include "interactionManager_common.h"
#include "MemoryPool.h"
#include "nemesis_common.h"
#include "net.h"
#include "oldencounter_common.h"
#include "rand.h"
#include "StringCache.h"
#include "TriCube/vec.h"
#include "wlInteraction.h"
#include "entcritter.h"

#include "AttribCurveImp.h"
#include "AttribMod_h_ast.h"
#include "AttribModFragility.h"
#include "Character.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/CharacterAttribs_h_ast.h"
#include "CharacterClass.h"
#include "Character_combat.h"
#include "Character_mods.h"
#include "Character_target.h"
#include "CombatAdvantage.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "CombatPool_h_ast.h"
#include "CombatSensitivity.h"
#include "CostumeCommonGenerate.h"
#include "DamageTracker.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerApplication.h"
#include "PowerModes.h"
#include "AutoGen/Powers_h_ast.h"
#include "PowerEnhancements.h"
#include "PowersEnums_h_ast.h"
#include "PowerSlots.h"
#include "PowerSubtarget.h"
#include "AutoGen/PowerSubtarget_h_ast.h"
#include "PowerTree.h"
#include "PowerVars.h"
#include "SavedPetCommon.h"
#include "Player.h"
#include "qsortG.h"
#include "species_common.h"
#include "EntitySavedData.h"
#include "CostumeCommonTailor.h"
#include "notifyEnum.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "InventoryCommon.h"

#include "AutoGen/CharacterAttribsMinimal_h_ast.h"
#include "Player_h_ast.h"
#include "PvPGameCommon_h_ast.h"
#include "StringUtil.h"
#include "dynFxInfo.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "EntityMovementTactical.h"
	#include "PowersMovement.h"
#endif

#if GAMESERVER
	#include "CommandQueue.h"
	#include "aiLib.h"
	#include "aiPowers.h"
	#include "aiFCStruct.h"
	#include "Expression.h"
	#include "GameServerLib.h"
	#include "gslCostume.h"
	#include "gslCritter.h"
	#include "gslEntity.h"
	#include "gslEventSend.h"
	#include "gslInteractionManager.h"
	#include "LoggedTransactions.h"
	#include "gslCombatAdvantage.h"
	#include "gslMapTransfer.h"
	#include "gslMechanics.h"
	#include "gslPartition.h"
	#include "gslPowerTransactions.h"
	#include "gslPVP.h"
	#include "gslPvPGame.h"
	#include "gslSpawnPoint.h"
	#include "gslProjectileEntity.h"
	#include "mechanics_common.h"
	#include "Reward.h"
	#include "WorldColl.h"
	#include "WorldGrid.h"

	#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#endif

#if GAMECLIENT
	#include "gclEntity.h"
	#include "UIGen.h"
#endif

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("CharacterAttribsMinimal.h", BUDGET_GameSystems););

int g_iDamageTypeCount = 0;
DamageTypeNames g_DamageTypeNames = {0};

int g_iDataDefinedAttributesCount = 0;
DataDefinedAttributes g_DataDefinedAttributes = {0};

int g_iCharacterAttribCount;
int g_iCharacterAttribSizeUsed;

AttribPools g_AttribPools = {0};
int g_iAttribPoolCount = 0;
int g_bAttribPoolPower = false;

DictionaryHandle *g_hAttribStatsPresetsDict;

static int s_bDamageInventory = 0;

AUTO_CMD_INT(s_bDamageInventory,CombatDamageInventory) ACMD_SERVERONLY;



StaticDefineInt AttribAspectEnum[] =
{
	DEFINE_INT

	{ "BasicAbs", kAttribAspect_BasicAbs},
	{ "BasicFactPos", kAttribAspect_BasicFactPos},
	{ "BasicFactNeg", kAttribAspect_BasicFactNeg},

	{ "StrBase", kAttribAspect_StrBase},
	{ "StrFactPos", kAttribAspect_StrFactPos},
	{ "StrFactNeg", kAttribAspect_StrFactNeg},
	{ "StrFactBonus", kAttribAspect_StrFactBonus},
	{ "StrFactCrit", kAttribAspect_StrFactBonus},
	{ "StrMult", kAttribAspect_StrMult},
	{ "StrAdd", kAttribAspect_StrAdd},

	{ "ResTrue", kAttribAspect_ResTrue},
	{ "ResBase", kAttribAspect_ResBase},
	{ "ResFactPos", kAttribAspect_ResFactPos},
	{ "ResFactNeg", kAttribAspect_ResFactNeg},
	{ "ResFactBonus", kAttribAspect_ResFactBonus},
	{ "ResFactBlock", kAttribAspect_ResFactBonus},
	{ "Immunity", kAttribAspect_Immunity},

	DEFINE_END
};

DefineContext *s_pDefineDamageNames = NULL;
DefineContext *s_pDefineDataDefinedNames = NULL;

// To create this table from scratch
//  leave the DEFINE_* parts alone (make sure you don't lose DataDefinedNames, it's in the middle),
//  copy in the AttribType enum, 
//  and then Find and Replace using regular expressions
//   Find: ^\tkAttribType_{[^:b]*}.*,
//   Replace with: \t\{ "\1", kAttribType_\1\},
StaticDefineInt AttribTypeEnum[] =
{
	DEFINE_INT

	DEFINE_EMBEDDYNAMIC_INT(s_pDefineDamageNames)

	// Health .. assumptions made about the order of these
	{ "HitPointsMax", kAttribType_HitPointsMax},
	{ "HitPoints", kAttribType_HitPoints},

	// Power .. assumptions made about the order of these and their position relative to health
	{ "PowerMax", kAttribType_PowerMax},
	{ "Power", kAttribType_Power},

	// Air .. assumptions are made about the order of these and their position relative to power
	{ "AirMax", kAttribType_AirMax},
	{ "Air", kAttribType_Air},

	// Stats!
	{ "StatDamage", kAttribType_StatDamage},
	{ "StatHealth", kAttribType_StatHealth},
	{ "StatPower", kAttribType_StatPower},
	{ "StatStrength", kAttribType_StatStrength},
	{ "StatAgility", kAttribType_StatAgility},
	{ "StatIntelligence", kAttribType_StatIntelligence},
	{ "StatEgo", kAttribType_StatEgo},
	{ "StatPresence", kAttribType_StatPresence},
	{ "StatRecovery", kAttribType_StatRecovery},

	// Regen
	{ "Regeneration", kAttribType_Regeneration},
	{ "PowerDecay", kAttribType_PowerDecay},
	{ "PowerRecovery", kAttribType_PowerRecovery},
	{ "PowerEquilibrium", kAttribType_PowerEquilibrium},
	{ "AirRecovery", kAttribType_AirRecovery},

	// Combat Ability
	{ "Dodge", kAttribType_Dodge},
	{ "Avoidance", kAttribType_Avoidance},
	{ "CritChance", kAttribType_CritChance},
	{ "CritSeverity", kAttribType_CritSeverity},

	// Movement
	{ "Flight", kAttribType_Flight},
	{ "Swinging", kAttribType_Swinging},
	{ "NoCollision", kAttribType_NoCollision},
	{ "SpeedRunning", kAttribType_SpeedRunning},
	{ "SpeedFlying", kAttribType_SpeedFlying},
	{ "FlightGlideDecent", kAttribType_FlightGlideDecent},
	{ "SpeedJumping", kAttribType_SpeedJumping},
	{ "HeightJumping", kAttribType_HeightJumping},
	{ "FrictionRunning", kAttribType_FrictionRunning},
	{ "FrictionFlying", kAttribType_FrictionFlying},
	{ "TractionRunning", kAttribType_TractionRunning},
	{ "TractionFlying", kAttribType_TractionFlying},
	{ "TractionJumping", kAttribType_TractionJumping},
	{ "Gravity", kAttribType_Gravity},
	{ "GravityJumpingUp", kAttribType_GravityJumpingUp},
	{ "GravityJumpingDown", kAttribType_GravityJumpingDown},
	{ "TurnRateFlying", kAttribType_TurnRateFlying},

	// Perception
	{ "AggroStealth", kAttribType_AggroStealth},
	{ "PerceptionStealth", kAttribType_PerceptionStealth},
	{ "StealthSight", kAttribType_StealthSight},
	{ "Aggro", kAttribType_Aggro},
	{ "Perception", kAttribType_Perception},
	{ "Minimap", kAttribType_Minimap},

	// CC - Negative!
	{ "Root", kAttribType_Root},
	{ "Hold", kAttribType_Hold},
	{ "Confuse", kAttribType_Confuse},
	{ "Disable", kAttribType_Disable},

	// CC - Negative!
	{ "KnockUp", kAttribType_KnockUp},
	{ "KnockBack", kAttribType_KnockBack},
	{ "Repel", kAttribType_Repel},

	// Powers Effect Area
	{ "Radius", kAttribType_Radius},
	{ "Arc", kAttribType_Arc},

	// Powers Attributes
	{ "SpeedActivate", kAttribType_SpeedActivate},
	{ "SpeedRecharge", kAttribType_SpeedRecharge},
	{ "SpeedCharge", kAttribType_SpeedCharge},
	{ "SpeedPeriod", kAttribType_SpeedPeriod},
	{ "SpeedCooldown", kAttribType_SpeedCooldown},
	{ "DiscountCost", kAttribType_DiscountCost},
	{ "SubtargetAccuracy", kAttribType_SubtargetAccuracy},
	{ "OnlyAffectSelf", kAttribType_OnlyAffectSelf},

	// AI
	{ "AIThreatScale", kAttribType_AIThreatScale},

	DEFINE_EMBEDDYNAMIC_INT(s_pDefineDataDefinedNames)

	// Special Attribs (modify the commented part of CharacterAttribs.h, then update real structures)
	{ "Null", kAttribType_Null},
	{ "AIAvoid", kAttribType_AIAvoid},
	{ "AICommand", kAttribType_AICommand},
	{ "AISoftAvoid", kAttribType_AISoftAvoid},
	{ "AIThreat", kAttribType_AIThreat},
	{ "All", kAttribType_All},
	{ "ApplyObjectDeath", kAttribType_ApplyObjectDeath},
	{ "ApplyPower", kAttribType_ApplyPower},
	{ "AttribLink", kAttribType_AttribLink},
	{ "AttribModDamage", kAttribType_AttribModDamage},
	{ "AttribModExpire", kAttribType_AttribModExpire},
	{ "AttribModFragilityHealth", kAttribType_AttribModFragilityHealth},
	{ "AttribModFragilityScale", kAttribType_AttribModFragilityScale},
	{ "AttribModHeal", kAttribType_AttribModHeal},
	{ "AttribModShare", kAttribType_AttribModShare},
	{ "AttribModShieldPercentIgnored", kAttribType_AttribModShieldPercentIgnored},
	{ "AttribOverride", kAttribType_AttribOverride},
	{ "BecomeCritter", kAttribType_BecomeCritter},
	{ "BePickedUp", kAttribType_BePickedUp},
	{ "CombatAdvantage", kAttribType_CombatAdvantage},
	{ "ConstantForce", kAttribType_ConstantForce},
	{ "CurveDodgeAndAvoidance", kAttribType_CurveDodgeAndAvoidance},
	{ "CurveTriggeredPercentHeals", kAttribType_CurveTriggeredPercentHeals},
	{ "DamageTrigger", kAttribType_DamageTrigger},
	{ "DisableTacticalMovement", kAttribType_DisableTacticalMovement},
	{ "DropHeldObject", kAttribType_DropHeldObject},
	{ "EntAttach", kAttribType_EntAttach},
	{ "EntCreate", kAttribType_EntCreate},
	{ "EntCreateVanity", kAttribType_EntCreateVanity},
	{ "Faction", kAttribType_Faction},
	{ "Flag", kAttribType_Flag},
	{ "GrantPower", kAttribType_GrantPower},
	{ "GrantReward", kAttribType_GrantReward},
	{ "IncludeEnhancement", kAttribType_IncludeEnhancement},
	{ "Interrupt", kAttribType_Interrupt},
	{ "ItemDurability", kAttribType_ItemDurability},
	{ "Kill", kAttribType_Kill},
	{ "SilentKill", kAttribType_Kill},
	{ "KillTrigger", kAttribType_KillTrigger},
	{ "KnockTo", kAttribType_KnockTo},
	{ "MissionEvent", kAttribType_MissionEvent},
	{ "ModifyCostume", kAttribType_ModifyCostume},
	{ "Notify", kAttribType_Notify},
	{ "Placate", kAttribType_Placate},
	{ "PowerMode", kAttribType_PowerMode},
	{ "PowerRecharge", kAttribType_PowerRecharge},
	{ "PowerShield", kAttribType_PowerShield},
	{ "ProjectileCreate", kAttribType_ProjectileCreate}, 
	{ "PVPFlag", kAttribType_PVPFlag}, 
	{ "PVPSpecialAction", kAttribType_PVPSpecialAction},
	{ "RemovePower", kAttribType_RemovePower},
	{ "RewardModifier", kAttribType_RewardModifier},
	{ "Ride", kAttribType_Ride},
	{ "SetCostume", kAttribType_SetCostume},
	{ "Shield", kAttribType_Shield},
	{ "SpeedCooldownCategory", kAttribType_SpeedCooldownCategory},
	{ "SubtargetSet", kAttribType_SubtargetSet},
	{ "Taunt", kAttribType_Taunt},
	{ "Teleport", kAttribType_Teleport},
	{ "TeleThrow", kAttribType_TeleThrow},
	{ "TriggerComplex", kAttribType_TriggerComplex},
	{ "TriggerSimple", kAttribType_TriggerSimple},
	{ "WarpSet", kAttribType_WarpSet},
	{ "WarpTo", kAttribType_WarpTo},
	{ "AIAggroTotalScale", kAttribType_AIAggroTotalScale},
	{ "DynamicAttrib", kAttribType_DynamicAttrib},
	
	// kAttribType_LAST should be equal to the above value for the next define to work.

	DEFINE_EMBEDDYNAMIC_INT(g_AttribSets.pDefineAttribSets)

	DEFINE_END
};

typedef struct SpecialAttribsTable
{
	int iAttribType;
	ParseTable *pTable;
}SpecialAttribsTable;

SpecialAttribsTable g_SpecialAttribsTable[] = {
	{kAttribType_AIAvoid,parse_AIAvoidParams},
	{kAttribType_AICommand, parse_AICommandParams},
	{kAttribType_AISoftAvoid,parse_AISoftAvoidParams},
	{kAttribType_ApplyPower,parse_ApplyPowerParams},
	{kAttribType_AttribModDamage,parse_AttribModDamageParams},
	{kAttribType_AttribModExpire,parse_AttribModExpireParams},
	{kAttribType_AttribModFragilityScale,parse_AttribModFragilityScaleParams},
	{kAttribType_AttribModHeal,parse_AttribModHealParams},
	{kAttribType_AttribModShare,parse_AttribModShareParams},
	{kAttribType_AttribOverride, parse_AttribOverrideParams},
	{kAttribType_BecomeCritter,parse_BecomeCritterParams},
	{kAttribType_CombatAdvantage, parse_CombatAdvantageParams},
	{kAttribType_ConstantForce, parse_ConstantForceParams},
	{kAttribType_DamageTrigger,parse_DamageTriggerParams},
	{kAttribType_DisableTacticalMovement,parse_DisableTacticalMovementParams},
	{kAttribType_EntAttach, parse_EntAttachParams},
	{kAttribType_EntCreate,parse_EntCreateParams},
	{kAttribType_EntCreateVanity,parse_EntCreateVanityParams},
	{kAttribType_Faction,parse_FactionParams},
	{kAttribType_Flag,parse_FlagParams},
	{kAttribType_Flight,parse_FlightParams},
	{kAttribType_GrantPower,parse_GrantPowerParams},
	{kAttribType_GrantReward, parse_GrantRewardParams},
	{kAttribType_IncludeEnhancement,parse_IncludeEnhancementParams},
	{kAttribType_Interrupt,parse_InterruptParams},
	{kAttribType_ItemDurability,parse_ItemDurabilityParams},
	{kAttribType_Kill,parse_KillParams},
	{kAttribType_KillTrigger,parse_KillTriggerParams},
	{kAttribType_KnockBack,parse_AttribModKnockbackParams},
	{kAttribType_KnockTo,parse_KnockToParams},
	{kAttribType_KnockUp,parse_AttribModKnockupParams},
	{kAttribType_MissionEvent,parse_MissionEventParams},
	{kAttribType_ModifyCostume, parse_ModifyCostumeParams},
	{kAttribType_Notify, parse_NotifyParams},
	{kAttribType_Placate,parse_PlacateParams},
	{kAttribType_PowerMode,parse_PowerModeParams},
	{kAttribType_PowerRecharge, parse_PowerRechargeParams},
	{kAttribType_PowerShield, parse_PowerShieldParams},
	{kAttribType_ProjectileCreate, parse_ProjectileCreateParams},
	{kAttribType_PVPFlag, parse_PVPFlagParams},
	{kAttribType_PVPSpecialAction, parse_PVPSpecialActionParams},
	{kAttribType_RemovePower,parse_RemovePowerParams},
	{kAttribType_RewardModifier,parse_RewardModifierParams},
	{kAttribType_SetCostume, parse_SetCostumeParams},
	{kAttribType_Shield,parse_ShieldParams},
	{kAttribType_SpeedCooldownCategory,parse_SpeedCooldownParams},
	{kAttribType_SubtargetSet,parse_SubtargetSetParams},
	{kAttribType_TeleThrow, parse_TeleThrowParams},
	{kAttribType_Teleport, parse_TeleportParams},
	{kAttribType_TriggerComplex,parse_TriggerComplexParams},
	{kAttribType_TriggerSimple,parse_TriggerSimpleParams},
	{kAttribType_WarpTo,parse_WarpToParams},
	{kAttribType_AIAggroTotalScale,parse_AIAggroTotalScaleParams},
	{kAttribType_DynamicAttrib,parse_DynamicAttribParams},
	{kAttribType_Swinging,parse_SwingingParams},
};

// Returns the ParseTable for the Attrib
ParseTable *characterattribs_GetSpecialParseTable(S32 iAttribType)
{
	int i;
	int count = sizeof(g_SpecialAttribsTable) / sizeof(SpecialAttribsTable);

	for(i=0;i<count;i++)
	{
		if(g_SpecialAttribsTable[i].iAttribType == iAttribType)
			return g_SpecialAttribsTable[i].pTable;
	}
	return NULL;
}


MP_DEFINE(AttribAccrualSet);

static AttribAccrualSet s_AttribAccrualSetEmpty = {0};

static StashTable s_hInnateAccrualSetsCritter = NULL;

static void DestroyInnateAccrualSet(AttribAccrualSet* pSet)
{
	eaDestroyStruct(&pSet->ppSpeedCooldown, parse_CooldownRateModifier);
	MP_FREE(AttribAccrualSet,pSet);
}

// helper function to add two innate attribs together, 
// if it's kAttribAspect_StrMult multiply instead of add
__forceinline static void processInnateAttribAccrual(F32 *pf, F32 fMag, AttribAspect offAspect)
{
	if (offAspect != kAttribAspect_StrMult)
	{
		*pf += fMag;
	}
	else 
	{
		*pf *= fMag;
	}
}

// sets the AttribAccrualSet to default values
// if bStrMultOnly is true, the struct will not be zeroed out
// StrMult attribs are set to 1.0
void AttribAccrualSet_SetDefaultValues(AttribAccrualSet *pSet, bool bStrMultOnly)
{
	if (!bStrMultOnly)
		ZeroStruct(pSet);

	{
		F32 *pfAttrib = (F32*)(&pSet->CharacterAttribs.attrStrMult);
		S32 i = sizeof(CharacterAttribs)/sizeof(F32);
		do {
			*pfAttrib = 1.f;
			pfAttrib++;
		} while(--i > 0);
	}

}

AUTO_RUN;
int InitCharacterAttribs(void)
{
	RegisterNamedStaticDefine(AttribTypeEnum, "AttribType");
	RegisterNamedStaticDefine(AttribAspectEnum, "AttribAspect");
	s_hInnateAccrualSetsCritter = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
	MP_CREATE_COMPACT(AttribAccrualSet, 16, 256, 0.80);

	// 
	AttribAccrualSet_SetDefaultValues(&s_AttribAccrualSetEmpty, true);

#ifdef GAMECLIENT
	ui_GenInitIntVar("AttribAspect_BasicAbs",		kAttribAspect_BasicAbs);
	ui_GenInitIntVar("AttribAspect_BasicFactPos",	kAttribAspect_BasicFactPos);
	ui_GenInitIntVar("AttribAspect_BasicFactNeg",	kAttribAspect_BasicFactNeg);
	ui_GenInitIntVar("AttribAspect_StrBase",		kAttribAspect_StrBase);
	ui_GenInitIntVar("AttribAspect_StrFactPos",		kAttribAspect_StrFactPos);
	ui_GenInitIntVar("AttribAspect_StrFactNeg",		kAttribAspect_StrFactNeg);
	ui_GenInitIntVar("AttribAspect_StrFactBonus",	kAttribAspect_StrFactBonus);
	ui_GenInitIntVar("AttribAspect_StrMult",		kAttribAspect_StrMult);
	ui_GenInitIntVar("AttribAspect_StrAdd",			kAttribAspect_StrAdd);
	ui_GenInitIntVar("AttribAspect_ResTrue",		kAttribAspect_ResTrue);
	ui_GenInitIntVar("AttribAspect_ResBase",		kAttribAspect_ResBase);
	ui_GenInitIntVar("AttribAspect_ResFactPos",		kAttribAspect_ResFactPos);
	ui_GenInitIntVar("AttribAspect_ResFactNeg",		kAttribAspect_ResFactNeg);
	ui_GenInitIntVar("AttribAspect_ResFactBonus",	kAttribAspect_ResFactBonus);
	ui_GenInitIntVar("AttribAspect_Immunity",		kAttribAspect_Immunity);
#endif
	return 0;
}

AUTO_RUN;
int InitCharacterAttribPresets(void)
{
	g_hAttribStatsPresetsDict = RefSystem_RegisterSelfDefiningDictionary("AttribStatsPresetDef", false, parse_AttribStatsPresetDef, true, true, NULL);

	return 0;
}

void attribstatspresets_Reload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading AttribStatsPresets Dict...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_hAttribStatsPresetsDict);

	loadend_printf(" done (%d AttribStatsPresets Defs)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hAttribStatsPresetsDict));
}

AUTO_STARTUP(AttribStatsPresets) ASTRT_DEPS(AS_CharacterAttribs);
void attribstatspresets_Load(void)
{
	resLoadResourcesFromDisk(g_hAttribStatsPresetsDict, NULL, "defs/config/AttribStatsPresets.def", "AttribStatsPresets.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/AttribStatsPresets.def", attribstatspresets_Reload);
	}
}

AttribStatsPresetDef* attribstatspreset_GetDefByName( const char* pchName )
{
	return RefSystem_ReferentFromString(g_hAttribStatsPresetsDict,pchName);
}

int SortPowerByName(const void *a, const void *b)
{
	return stricmp((*(PowerDef**)a)->pchName,(*(PowerDef**)b)->pchName);
}

// Returns if the Attrib is the current value of an attrib pool
bool attrib_isAttribPoolCur(AttribType eAttrib)
{
	int i;

	for(i=g_iAttribPoolCount-1;i>=0;i--)
	{
		if(g_AttribPools.ppPools[i]->eAttribCur == eAttrib)
			return true;
	}

	return false;
}

// Returns the first attrib pool that is found that matches eAttribCur
AttribPool* attrib_getAttribPoolByCur(AttribType eAttrib)
{
	int i;

	for(i=g_iAttribPoolCount-1;i>=0;i--)
	{
		if(g_AttribPools.ppPools[i]->eAttribCur == eAttrib)
			return g_AttribPools.ppPools[i];
	}

	return NULL;
}

// pchName is assumed to be a POOL_STRING 
void attribPool_SetOverrideTickTimer(Character *pChar, const char *pchName, F32 fTimer)
{
	int i;

	if (pchName && eafSize(&pChar->pfTimersAttribPool) == g_iAttribPoolCount)
	{
		for(i = g_iAttribPoolCount-1; i>=0; i--)
		{
			if (g_AttribPools.ppPools[i]->pchName == pchName)
			{
				pChar->pfTimersAttribPool[i] = fTimer;
				break;
			}
		}
	}
}

// Performs load-time generation of custom params
S32 moddef_Load_Generate(AttribModDef *pdef)
{
	S32 bGood = true;

	switch (pdef->offAttrib)
	{
		xcase kAttribType_AttribModShare:
		{
			AttribModShareParams *pParams = (AttribModShareParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprShare,kCombatEvalContext_Affects);
				combateval_Generate(pParams->pExprContribution,kCombatEvalContext_Affects);
			}
		}

		xcase kAttribType_ConstantForce:
		{
			ConstantForceParams *pParams = (ConstantForceParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprYawOffset,kCombatEvalContext_Apply);
			}
		}

		xcase kAttribType_DamageTrigger:
		{
			DamageTriggerParams *pParams = (DamageTriggerParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprChance, kCombatEvalContext_Target);
			}
		}
		xcase kAttribType_TriggerComplex:
		{
			TriggerComplexParams *pParams = (TriggerComplexParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprChance, kCombatEvalContext_Target);
			}
		}
		xcase kAttribType_TriggerSimple:
		{
			TriggerSimpleParams *pParams = (TriggerSimpleParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprChance, kCombatEvalContext_Target);
			}
		}

		xcase kAttribType_EntCreate:
		{
			EntCreateParams *pParams = (EntCreateParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprDistanceFront,kCombatEvalContext_Apply);
				combateval_Generate(pParams->pExprDistanceRight,kCombatEvalContext_Apply);
				combateval_Generate(pParams->pExprDistanceAbove,kCombatEvalContext_Apply);
			}
		}

		xcase kAttribType_EntCreateVanity:
		{
			EntCreateVanityParams *pParams = (EntCreateVanityParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprDistanceFront,kCombatEvalContext_Apply);
				combateval_Generate(pParams->pExprDistanceRight,kCombatEvalContext_Apply);
				combateval_Generate(pParams->pExprDistanceAbove,kCombatEvalContext_Apply);
			}
		}
	
		xcase kAttribType_AICommand:
		{
	#ifdef GAMESERVER
			ExprContext* pContext;

			pContext = aiGetStaticCheckExprContext();
			if(((AICommandParams *)pdef->pParams)->pExpr)
			{
				exprContextSetPointerVar(pContext, "ModSourceEnt", NULL, parse_Entity, true, true);
				exprContextSetPointerVar(pContext, "ModOwnerEnt", NULL, parse_Entity, true, true);
				bGood = exprGenerate(((AICommandParams *)pdef->pParams)->pExpr,pContext);
				exprContextRemoveVar(pContext, "ModSourceEnt");
				exprContextRemoveVar(pContext, "ModOwnerEnt");
			}
	#endif
		}

		xcase kAttribType_Shield:
		{
			ShieldParams *pParams = (ShieldParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pExprMaxDamageAbsorbed,kCombatEvalContext_Apply);
			}
		}

		xcase kAttribType_Teleport:
		{
			TeleportParams *pParams = (TeleportParams*)(pdef->pParams);
			if(pParams)
			{
				combateval_Generate(pParams->pTeleportTargetExpr,kCombatEvalContext_Teleport);
				combateval_Generate(pParams->pExprDistanceFront,kCombatEvalContext_Apply);
				combateval_Generate(pParams->pExprDistanceRight,kCombatEvalContext_Apply);
				combateval_Generate(pParams->pExprDistanceAbove,kCombatEvalContext_Apply);
			}
		}
	}

	return bGood;
}

static AttribType DynamicAttribEval(int iPartitionIdx, AttribMod *pmod, DynamicAttribParams *pParams, bool bCache, bool bApply)
{
	AttribType eType = kAttribType_Null;
#ifdef GAMESERVER
	PERFINFO_AUTO_STOP_FUNC();
	{
		if(!pmod || (pParams && (pParams->bDoNoCache || bCache)))
		{
			eType = (AttribType)combateval_EvalNew(iPartitionIdx,pParams->pExprAttrib,bApply ? kCombatEvalContext_Apply : kCombatEvalContext_Simple,NULL);
			if(pmod)
				pmod->pParams->eDynamicCachedType = eType;
		}
		else
		{
			eType = pmod->pParams->eDynamicCachedType;
		}
	}
	PERFINFO_AUTO_STOP();
#endif
	return eType;
}

// Performs the custom parameter setup during mod fill if the mod needs it
void mod_Fill_Params(int iPartitionIdx,
				     AttribMod *pmod,
					 AttribModDef *pdef,
					 PowerApplication *pApplication,
					 EntityRef erEffectedTarget)
{
	PERFINFO_AUTO_START_FUNC();

	switch(pdef->offAttrib)
	{
		xcase kAttribType_EntCreate:
		{
			EntCreateParams *pParams = (EntCreateParams*)(pdef->pParams);
			int bValid = false;
			Vec3 vecDistance = {0};

			if(pParams->pExprDistanceFront)
			{
				vecDistance[0] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceFront,kCombatEvalContext_Apply,NULL);
				bValid |= (vecDistance[0] != 0);
			}
			if(pParams->pExprDistanceRight)
			{
				vecDistance[1] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceRight,kCombatEvalContext_Apply,NULL);
				bValid |= (vecDistance[1] != 0);
			}
			if(pParams->pExprDistanceAbove)
			{
				vecDistance[2] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceAbove,kCombatEvalContext_Apply,NULL);
				bValid |= (vecDistance[2] != 0);
			}

			{
				AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);
				if(bValid)
					copyVec3(vecDistance,pParamsMod->vecParam);
				else
					zeroVec3(pParamsMod->vecParam);

				if(pParams->bUseMainTarget)
				{
					if(pApplication->pentTargetEff)
						pParamsMod->erParam = entGetRef(pApplication->pentTargetEff);
					else
						pParamsMod->erParam = pApplication->erTarget;
				}
				else
				{
					pParamsMod->erParam = erEffectedTarget;
				}

				// Is there a secondary target
				if (pApplication->pdef && 
					pApplication->pdef->fRangeSecondary > 0.0f && 
					!ISZEROVEC3(pApplication->vecTargetSecondary))
				{
					copyVec3(pApplication->vecTargetSecondary, pParamsMod->vecTarget);
				} 
				else if (pParams->bUseTargetPositionWhenNoTarget && !pApplication->erTarget)
				{
					copyVec3(pApplication->vecTargetEff, pParamsMod->vecTarget);
				}
				else if (pParams->bUseTargetPositionAsAIVarsTargetPos)
				{
					copyVec3(pApplication->vecTarget, pParamsMod->vecTarget);
				}
				else
				{
					zeroVec3(pParamsMod->vecTarget);
				}

				if(pParams->bCanCustomizeCostume && pApplication->ppow)
				{
					Power *ppow = pApplication->ppow;
					if(ppow->pParentPower)
						ppow = ppow->pParentPower;
					pParamsMod->iParam = ppow->iEntCreateCostume;
				}


				pmod->pParams = pParamsMod;
			}
		}

		xcase kAttribType_EntCreateVanity:
		{
			EntCreateVanityParams *pParams = (EntCreateVanityParams*)(pdef->pParams);
			int bValid = false;
			Vec3 vecDistance = {0};

			if(pParams->pExprDistanceFront)
			{
				vecDistance[0] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceFront,kCombatEvalContext_Apply,NULL);
				bValid |= (vecDistance[0] != 0);
			}
			if(pParams->pExprDistanceRight)
			{
				vecDistance[1] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceRight,kCombatEvalContext_Apply,NULL);
				bValid |= (vecDistance[1] != 0);
			}
			if(pParams->pExprDistanceAbove)
			{
				vecDistance[2] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceAbove,kCombatEvalContext_Apply,NULL);
				bValid |= (vecDistance[2] != 0);
			}

			{
				AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);
				if(bValid)
					copyVec3(vecDistance,pParamsMod->vecParam);
				else
					zeroVec3(pParamsMod->vecParam);

				pmod->pParams = pParamsMod;
			}
		}

		xcase kAttribType_ApplyObjectDeath:
		{
			WorldInteractionNode *pNode = NULL;
			if(pApplication->pcharSource)
			{
				// Finding the node key is pretty simple.  If the source is a node, that's it.  Otherwise, if the
				//  source is holding something, that's it.
				if(IS_HANDLE_ACTIVE(pApplication->pcharSource->pEntParent->hCreatorNode))
				{
					pNode = GET_REF(pApplication->pcharSource->pEntParent->hCreatorNode);
				}
				else if(IS_HANDLE_ACTIVE(pApplication->pcharSource->hHeldNode))
				{
					pNode = GET_REF(pApplication->pcharSource->hHeldNode);
				}
				else if(pApplication->pcharSource->erHeld)
				{
					Entity *pentHeld = entFromEntityRef(iPartitionIdx, pApplication->pcharSource->erHeld);
					if(pentHeld && IS_HANDLE_ACTIVE(pentHeld->hCreatorNode))
					{
						pNode = GET_REF(pentHeld->hCreatorNode);
					}
				}
			}

			if(pNode)
			{
				AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);
				SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, pNode, pParamsMod->hNodeParam);

				pmod->pParams = pParamsMod;
			}
		}

		xcase kAttribType_PVPSpecialAction:
		{
			PVPSpecialActionParams *pParams = (PVPSpecialActionParams*)pdef->pParams;

			if(pParams->eAction == kPVPSpecialAction_ThrowFlag)
			{
				if(pApplication->pact && !ISZEROVEC3(pApplication->pact->vecTarget))
				{
					AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);
					copyVec3(pApplication->pact->vecTarget,pParamsMod->vecParam);

					pmod->pParams = pParamsMod;
				}
			}
		}

		xcase kAttribType_Teleport:
		{
			TeleportParams *pParams = (TeleportParams*)(pdef->pParams);
			AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);
			Entity *eTarget = entFromEntityRef(iPartitionIdx, erEffectedTarget);
			Entity *pMainTeleportTarget = ModTeleportGetTeleportMainEntity(pApplication->pcharSource, eTarget, pdef);

			setVec3(pParamsMod->vecOffset,	pParams->fOffsetForward, 
											pParams->fOffsetRight,
											pParams->fOffsetUp );
			
			// we need to treat bSpecialLargeMonster special if it's not casting the teleport
			if (!pMainTeleportTarget || 
				!pMainTeleportTarget->pChar || 
				pMainTeleportTarget->pChar == pApplication->pcharSource || 
				!pMainTeleportTarget->pChar->bSpecialLargeMonster)
			{
				if(pParams->pExprDistanceFront)
				{
					pParamsMod->vecOffset[0] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceFront,kCombatEvalContext_Apply,NULL);
				}
				if(pParams->pExprDistanceRight)
				{
					pParamsMod->vecOffset[1] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceRight,kCombatEvalContext_Apply,NULL);
				}
				if(pParams->pExprDistanceAbove)
				{
					pParamsMod->vecOffset[2] = combateval_EvalNew(iPartitionIdx,pParams->pExprDistanceAbove,kCombatEvalContext_Apply,NULL);
				}
			}
			else
			{	// this is a special character and we are overriding the teleport 

				setVec3(pParamsMod->vecOffset, entGetPrimaryCapsuleRadius(eTarget) + 3.f, 0.f, 0.f);
			}

			if (!pParams->bClientViewTeleport)
			{
				// get the entity we want to teleport relative to
				S32 bRet = ModTeleportGetTeleportTargetTranslations(	pApplication->pcharSource, 
																		eTarget, 
																		pdef, 
																		pParamsMod->vecTarget,
																		pParamsMod->vecParam);

				if (pdef->pPowerDef && pdef->pPowerDef->fRangeSecondary > 0.f)
				{
					copyVec3(pApplication->pact->vecTargetSecondary,pParamsMod->vecTarget);
				}

				if (!bRet)
				{	// not a valid teleport, do not process this mod
					StructDestroy(parse_AttribModParams, pParamsMod);
					pParamsMod = NULL;
				}
			}	
			else if(pApplication->pact && !ISZEROVEC3(pApplication->pact->vecTargetSecondary))
			{
				copyVec3(pApplication->pact->vecTargetSecondary,pParamsMod->vecTarget);
				copyVec3(pApplication->pact->vecTarget,pParamsMod->vecParam);
			}
			else
			{
				StructDestroy(parse_AttribModParams, pParamsMod);
				pParamsMod = NULL;
			}

			pmod->pParams = pParamsMod;
		}

		xcase kAttribType_Shield:
		{
			ShieldParams *pParams = (ShieldParams*)(pdef->pParams);
			AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);

			if(pParams->pExprMaxDamageAbsorbed)
			{
				F32 fMaxDamageAbsorbed = combateval_EvalNew(iPartitionIdx,pParams->pExprMaxDamageAbsorbed,kCombatEvalContext_Apply,NULL);

				if(fMaxDamageAbsorbed != 0.0f)
				{
					pParamsMod->vecParam[0] = fMaxDamageAbsorbed;
				}
			}

			pParamsMod->vecParam[1] = pParams->fPercentIgnored;

			pmod->pParams = pParamsMod;
		}

		xcase kAttribType_ProjectileCreate:
		{
			ProjectileCreateParams *pProjParams = (ProjectileCreateParams*)(pmod->pDef->pParams);
			AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);
							
			copyVec3(pApplication->vecTarget, pParamsMod->vecTarget);
			copyVec3(pApplication->vecTargetSecondary, pParamsMod->vecParam);

			pmod->pParams = pParamsMod;
		}

		xcase kAttribType_DynamicAttrib:
		{
			DynamicAttribParams *pParams = (DynamicAttribParams*)pmod->pDef->pParams;

			AttribModParams *pParamsMod = StructAlloc(parse_AttribModParams);

			pmod->pParams = pParamsMod;

			if(!pParams->bDoNoCache)
			{
				pParamsMod->eDynamicCachedType = DynamicAttribEval(iPartitionIdx,pmod,pParams,true,true);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

TempAttribute *character_NewTempAttribute(Character *pChar, AttribType eAttrib)
{
	TempAttribute *pRtn;
	
	if(!pChar)
		return NULL;
	
	pRtn = StructCreate(parse_TempAttribute);
	pRtn->pchAttrib = StructAllocString(StaticDefineIntRevLookup(AttribTypeEnum,eAttrib));
	pRtn->fValue = *F32PTR_OF_ATTRIB(pChar->pattrBasic,eAttrib);

	return pRtn;
}
#ifdef GAMESERVER

typedef struct TempAttributeCB
{
	TempAttributes tempAttribs;
	ContainerID eEntID;
	GlobalType eEntType;
}TempAttributeCB;

static void ModifySavedAttribute_CB(TransactionReturnVal *returnVal, TempAttributeCB *cbData)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(cbData->eEntType,cbData->eEntID);

		if(pEnt && pEnt->pChar)
		{
			int i;
			for(i=0;i<eaSize(&cbData->tempAttribs.ppAttributes);i++)
			{
				AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum,cbData->tempAttribs.ppAttributes[i]->pchAttrib);

				if(pEnt && pEnt->pChar)
				{
					F32 *fValue = F32PTR_OF_ATTRIB(pEnt->pChar->pattrBasic,eAttrib);

					*fValue = cbData->tempAttribs.ppAttributes[i]->fValue;
				}

			}
			entity_SetDirtyBit(pEnt,parse_CharacterAttribs,pEnt->pChar->pattrBasic,false);
		}

	}
	eaDestroyStruct(&cbData->tempAttribs.ppAttributes,parse_TempAttribute);
	free(cbData);
}

void entity_ModifySavedAttributes(Entity *pEnt, ItemDef *pItemDef, TempAttributes *pTempAttributes, Entity *pItemOwner, U64 uiItemID )
{
	TempAttributeCB *cbData = calloc(1, sizeof(TempAttributeCB));
	GameAccountDataExtract *pExtract;
	ItemChangeReason reason = {0};
	int i;

	for(i=0;i<eaSize(&pTempAttributes->ppAttributes);i++)
	{
		TempAttribute *pNewAttribute = StructCreate(parse_TempAttribute);
		AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum,pTempAttributes->ppAttributes[i]->pchAttrib);

		pNewAttribute->pchAttrib = StructAllocString(pTempAttributes->ppAttributes[i]->pchAttrib);

		if(pEnt->myRef)
		{
			pNewAttribute->fValue = *F32PTR_OF_ATTRIB(pEnt->pChar->pattrBasic,eAttrib) + pTempAttributes->ppAttributes[i]->fValue;
		}
		else
		{
			int j;

			for(j=0;j<eaSize(&pEnt->pChar->ppSavedAttributes);j++)
			{
				if(StaticDefineIntGetInt(AttribTypeEnum,pEnt->pChar->ppSavedAttributes[j]->pchAttrib) == eAttrib)
				{
					pNewAttribute->fValue = pEnt->pChar->ppSavedAttributes[j]->fValue + pTempAttributes->ppAttributes[i]->fValue;
				}
			}
		}

		eaPush(&cbData->tempAttribs.ppAttributes,pNewAttribute);
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	inv_FillItemChangeReason(&reason, pEnt, "Powers:ModifySavedAttribute", (pItemDef ? pItemDef->pchName : "Unknown") );

	cbData->eEntID = pEnt->myContainerID;
	cbData->eEntType = pEnt->myEntityType;

	AutoTrans_trUpdateSavedAttributes(LoggedTransactions_CreateManagedReturnValEnt("ModifySavedAttribute", pEnt, ModifySavedAttribute_CB, cbData),
		GetAppGlobalType(),
		entGetType(pEnt),
		entGetContainerID(pEnt),
		&cbData->tempAttribs,
		pItemOwner != pEnt ? entGetType(pItemOwner):0,
		pItemOwner != pEnt ? entGetContainerID(pItemOwner):0,
		uiItemID, &reason, pExtract);
}
#endif

// Saves the Attributes on a Character that need to be persisted, but aren't hardcoded as persist
void character_SaveTempAttributes(Character *pchar, TempAttributes *pTempAttributes)
{
	eaDestroyStruct(&pTempAttributes->ppAttributes,parse_TempAttribute);

	if(g_iAttribPoolCount)
	{
		int i;
		for(i=0; i<g_iAttribPoolCount; i++)
		{
			AttribPool *ppool = g_AttribPools.ppPools[i];
			if(ppool->bPersist)
			{
				TempAttribute *pTempAttribute = character_NewTempAttribute(pchar,ppool->eAttribCur);
				eaPush(&pTempAttributes->ppAttributes,pTempAttribute);
				// TODO(JW): Might need to save more of the Attributes if the Pool uses proportional bound?

				if(ppool->bTargetNotCalculated) //If the target is not calculated then persist it as well
				{
					TempAttribute *pTempTargetAttribute = character_NewTempAttribute(pchar,ppool->eAttribTarget);
					eaPush(&pTempAttributes->ppAttributes,pTempTargetAttribute);
				}
			}
		}
	}
}

AUTO_TRANS_HELPER;
void character_FillSavedAttributesFromClass(ATH_ARG NOCONST(Character) *pChar, int iLevel)
{
	if(g_iAttribPoolCount)
	{
		int i;
		for(i=0;i<g_iAttribPoolCount; i++)
		{
			AttribPool *pPool = g_AttribPools.ppPools[i];

			if(pPool->bPersist && pPool->bAutoFill)
			{
				// Find the max from the character class info
				CharacterClass *pClass = GET_REF(pChar->hClass);
				NOCONST(SavedAttribute) *pNewAttribute;
				F32 fMax = pClass ? *F32PTR_OF_ATTRIB(pClass->ppAttrBasic[iLevel],pPool->eAttribMax) : 25;

				pNewAttribute = StructCreateNoConst(parse_SavedAttribute);
				pNewAttribute->pchAttrib = StructAllocString(StaticDefineIntRevLookup(AttribTypeEnum,pPool->eAttribCur));
				pNewAttribute->fValue = fMax;
				eaPush(&pChar->ppSavedAttributes,pNewAttribute);

				if(pPool->bTargetNotCalculated)
				{
					pNewAttribute = StructCreateNoConst(parse_SavedAttribute);
					pNewAttribute->pchAttrib = StructAllocString(StaticDefineIntRevLookup(AttribTypeEnum,pPool->eAttribTarget));
					pNewAttribute->fValue = fMax;
					eaPush(&pChar->ppSavedAttributes,pNewAttribute);
				}
			}
		}
	}
}

void character_AttribPoolRespawn(Character *pChar)
{
	if(g_iAttribPoolCount)
	{
		int i;
		for(i=0;i<g_iAttribPoolCount;i++)
		{
			AttribPool *pPool = g_AttribPools.ppPools[i];

			if(pPool->bFillOnRespawn)
			{
				F32 fMin = pPool->eAttribMin ? *F32PTR_OF_ATTRIB(pChar->pattrBasic,pPool->eAttribMin) : 0;
				F32 fMax = pPool->eAttribMax ? *F32PTR_OF_ATTRIB(pChar->pattrBasic,pPool->eAttribMax) : 0;
				F32 fTarget = pPool->eAttribTarget ? *F32PTR_OF_ATTRIB(pChar->pattrBasic,pPool->eAttribTarget) : 0;
				F32 *fValue = F32PTR_OF_ATTRIB(pChar->pattrBasic,pPool->eAttribCur);

				*fValue = combatpool_Init(&pPool->combatPool,fMin,fMax,fTarget);
			}
		}
	}
}


// Loads the Attributes on from a Character's saved data back onto the Character
void character_LoadSavedAttributes(Character *pchar)
{
	int i;
	for(i=eaSize(&pchar->ppSavedAttributes)-1; i>=0; i--)
	{
		SavedAttribute *pSavedAttribute = pchar->ppSavedAttributes[i];
		int eAttrib = StaticDefineIntGetInt(AttribTypeEnum,pSavedAttribute->pchAttrib);
		if(eAttrib>=0)
		{
			F32 *pfCur = F32PTR_OF_ATTRIB(pchar->pattrBasic,eAttrib);
			*pfCur = pSavedAttribute->fValue;
		}
	}
}



// Notes that the character's innate effects from equipment have changed
void character_DirtyInnateEquip(Character *p)
{
	if(p->pInnateEquipAccrualSet)
	{
		if(p->pInnateEquipAccrualSet!=&s_AttribAccrualSetEmpty)
		{
			// Local copy.  Free it, will cause the innate accrual to be recalculated
			DestroyInnateAccrualSet(p->pInnateEquipAccrualSet);
		}
		p->pInnateEquipAccrualSet = NULL;
	}
}

// Notes that the character's innate powers have changed
void character_DirtyInnatePowers(Character *p)
{
	if(p->pInnatePowersAccrualSet)
	{
		GlobalType eEntType = entGetType(p->pEntParent);
		if(p->pInnatePowersAccrualSet!=&s_AttribAccrualSetEmpty
			&& (eEntType==GLOBALTYPE_ENTITYPLAYER || eEntType==GLOBALTYPE_ENTITYSAVEDPET))
		{
			// Local copy.  Free it, will cause the innate accrual to be recalculated
			DestroyInnateAccrualSet(p->pInnatePowersAccrualSet);
			p->pInnatePowersAccrualSet = NULL;
		}
		else
		{
			// Not a local copy.  Just set it to null, will cause the innate accrual to
			//  be recalculated
			p->pInnatePowersAccrualSet = NULL;
		}
	}
}

void character_DirtyPowerStats(Character *p)
{
	if(p->pInnateStatPointsSet)
	{
		if(p->pInnateStatPointsSet!=&s_AttribAccrualSetEmpty)
		{
			// Local copy.  Free it, will cause the innate accrual to be recalculated
			DestroyInnateAccrualSet(p->pInnateStatPointsSet);
		}
		p->pInnateStatPointsSet = NULL;
	}
}

// List of EntRefs that have had their InnateAccrual dirtied but not regenerated.
//  Used to regenerate the InnateAccruals just before net send to avoid sending the
//  client a NULL, causing it to be incorrect about its efficacy until the next net send.
static EntityRef *s_perDirtyInnate = NULL;

// Regenerates the InnateAccrual of all Characters that have been noted as dirty
void combat_RegenerateDirtyInnates(void)
{
	int i;
	PERFINFO_AUTO_START_FUNC();
	for(i=ea32Size(&s_perDirtyInnate)-1; i>=0; i--)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(s_perDirtyInnate[i]);
		if(pEnt && pEnt->pChar && !pEnt->pChar->pInnateAccrualSet)
		{
			if (pEnt->pChar->bResetPowersArray)
			{
				GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
				character_ResetPowersArray(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);
				pEnt->pChar->bResetPowersArray = false;
			}
			character_GetInnateAccrual(entGetPartitionIdx(pEnt),pEnt->pChar,NULL);
		}
	}
	ea32ClearFast(&s_perDirtyInnate);
	PERFINFO_AUTO_STOP();
}

// Notes that the Character's overall innate accrual has changed
void character_DirtyInnateAccrual(Character *pchar)
{
	if(pchar->pInnateAccrualSet)
	{
		PERFINFO_AUTO_START_FUNC();
		// Local copy.  Free it, will cause the innate accrual to be recalculated
		StructDestroySafe(parse_AttribAccrualSet,&pchar->pInnateAccrualSet);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		character_Wake(pchar);
		// Note that this is dirty
		ea32Push(&s_perDirtyInnate,entGetRef(pchar->pEntParent));
		PERFINFO_AUTO_STOP();
	}
}

static AttribAccrualSet *GetInnatePowersAccrual(int iPartitionIdx, Character *p)
{
	int i,c;
	Power **ppInnates = NULL;
	PowerDef **ppInnateDefs = NULL;
	AttribAccrualSet *pResult = NULL;

	assert(p);

	PERFINFO_AUTO_START_FUNC();

	combateval_ContextSetupSimple(p, entity_GetCombatLevel(p->pEntParent), NULL);

	// NOTE:
	//  This code is dumb.  It generates these earrays, but only uses them for the stashtable lookup
	//  for critters - it doesn't actually use them in the true innate accrual, which happens in
	//  character_AccrueModsInnate().  That code makes its own list.  So, this code is dumb.  Not
	//  fixing it right away though because I don't want to break things.

	// Find all Innate Powers in the basic Powers list
	for(i=eaSize(&p->ppPowers)-1; i>=0; i--)
	{
		PowerDef *pdef = GET_REF(p->ppPowers[i]->hDef);

		// Check for BecomeCritter restriction
		if((p->bBecomeCritter || p->bBecomeCritterTickPhaseTwo) && p->ppPowers[i]->eSource!=kPowerSource_AttribMod)
			continue;

		if(pdef && pdef->eType==kPowerType_Innate)
		{
			eaPush(&ppInnates,p->ppPowers[i]);
			eaPush(&ppInnateDefs,pdef);
		}
	}

	// Add all external innate powers
	if(p->pEntParent->externalInnate)
	{
		for(i=eaSize(&p->pEntParent->externalInnate->ppPowersExternalInnate)-1; i>=0; i--)
		{
			PowerDef *pdef = GET_REF(p->pEntParent->externalInnate->ppPowersExternalInnate[i]->hDef);
			if(pdef && pdef->eType==kPowerType_Innate)
			{
				eaPush(&ppInnates,p->pEntParent->externalInnate->ppPowersExternalInnate[i]);
				eaPush(&ppInnateDefs,pdef);
			}
		}
	}

	if((c=eaSize(&ppInnateDefs))>0)
	{
		GlobalType eEntType = entGetType(p->pEntParent);
		if(eEntType==GLOBALTYPE_ENTITYPLAYER || eEntType==GLOBALTYPE_ENTITYSAVEDPET)
		{
			pResult = MP_ALLOC(AttribAccrualSet);
			if(!character_AccrueModsInnate(iPartitionIdx,p,pResult))
			{
				// Failed to generate something useful for some reason
				DestroyInnateAccrualSet(pResult);
				pResult = NULL;
			}
		}
		else
		{
			char *pchSet = NULL;
			CharacterClass *pclass = character_GetClassCurrent(p);
			qsort(ppInnateDefs,c,sizeof(PowerDef*),SortPowerByName);
			estrStackCreate(&pchSet);
			estrPrintf(&pchSet,"%s__%d__%s,%g",pclass?pclass->pchName:"NULL",p->iLevelCombat,ppInnateDefs[0]->pchName,ppInnates[0]->fTableScale);
			for(i=1; i<c; i++)
			{
				estrConcatf(&pchSet,"_%s,%g",ppInnateDefs[i]->pchName,ppInnates[i]->fTableScale);
			}

			if(!stashFindPointer(s_hInnateAccrualSetsCritter,pchSet,&pResult))
			{
				pResult = MP_ALLOC(AttribAccrualSet);
				if(!character_AccrueModsInnate(iPartitionIdx,p,pResult))
				{
					// Failed to generate something useful for some reason
					DestroyInnateAccrualSet(pResult);
					pResult = NULL;
				}
				else
				{
					stashAddPointer(s_hInnateAccrualSetsCritter,pchSet,pResult,0);
				}
			}
			estrDestroy(&pchSet);
		}
	}

	eaDestroy(&ppInnates);
	eaDestroy(&ppInnateDefs);

	if(!pResult)
	{
		pResult = &s_AttribAccrualSetEmpty;
	}

	PERFINFO_AUTO_STOP();

	return pResult;
}

static int mod_ProcessDynamicAttrib(int iPartitionIdx,
									AttribModDef *pdef,
									Character *pchar,
									CharacterClass *pClass,
									int iLevel,
									F32 fTableScale,
									AttribAccrualSet *pattrSet,
									bool bApply,
									F32 fItemScale)
{
	AttribType eOffAttrib = DynamicAttribEval(iPartitionIdx,NULL,(DynamicAttribParams*)pdef->pParams,false,bApply); 
	F32 *pf;
	F32 fMag = mod_GetInnateMagnitude(iPartitionIdx,pdef,pchar,pClass,iLevel,fTableScale);
	fMag *= fItemScale;

	pf = (F32*)((char*)pattrSet + pdef->offAspect + eOffAttrib);
	*pf += fMag;

	return *pf != 0.0f;
}

static int mod_ProcessInnateSpeedCooldown(int iPartitionIdx, 
										  AttribModDef *pdef,
										  AttribAccrualSet *pSet,
										  Character *pchar, 
										  CharacterClass *pClass,
										  int iLevel,
										  F32 fTableScale)
{
	SpeedCooldownParams *pParams = (SpeedCooldownParams*)pdef->pParams;
	if (pParams)
	{
		CooldownRateModifier* pCooldownModifier = eaIndexedGetUsingInt(&pSet->ppSpeedCooldown, pParams->ePowerCategory);
		F32 fMag;
			
		if (!pCooldownModifier)
		{
			pCooldownModifier = StructCreate(parse_CooldownRateModifier);
			pCooldownModifier->iPowerCategory = pParams->ePowerCategory;
			eaIndexedEnable(&pSet->ppSpeedCooldown, parse_CooldownRateModifier);
			eaPush(&pSet->ppSpeedCooldown, pCooldownModifier);
		}
		fMag = mod_GetInnateMagnitude(iPartitionIdx,pdef,pchar,pClass,iLevel,fTableScale);

		switch(pdef->offAspect)
		{
		xcase kAttribAspect_BasicAbs:
			pCooldownModifier->fBasicAbs += fMag;
		xcase kAttribAspect_BasicFactPos:
			pCooldownModifier->fBasicPos += fMag;
		xcase kAttribAspect_BasicFactNeg:
			pCooldownModifier->fBasicNeg += fMag;
		}
		return !nearSameF32(fMag, 1.0f);
	}
	return false;
}

static bool AccrueBag(int iPartitionIdx, AttribAccrualSet *pSet, InventoryBag *pBag, Character *p, CharacterClass *pClass, int iLevel, GameAccountDataExtract *pExtract)
{
	int c,iPower;
	bool bHasEffect = false;
	char *pchTrickAccrualSet = (char*)pSet;
	int iCharLevelControlled = 0;
	Entity *pEnt = (p ? p->pEntParent : NULL );
	BagIterator *iter;
	InvBagIDs BagID = pBag ? pBag->BagID : InvBagIDs_None;

	if(BagID == InvBagIDs_None) return false;

	if (!pEnt)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if(p->pLevelCombatControl)
	{
		iCharLevelControlled = p->iLevelCombat;
	}

	iter = invbag_IteratorFromEnt(pEnt,BagID,pExtract);
	for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pItem = (Item*)bagiterator_GetItem(iter);
		ItemDef *pItemDef = bagiterator_GetDef(iter);
		S32 NumPowers = item_GetNumItemPowerDefs(pItem, true);
		S32 iNumGemPowers = item_GetNumGemsPowerDefs(pItem);
		S32 iGemPowersStartIndex = NumPowers - iNumGemPowers;
		S32 iLevelAdjustment = 0;
		S32 iMinItemLevel = pItem ? item_GetMinLevel(pItem) : 0;

		if (!pItem || !pItemDef)
			continue;

		// Check to make sure that the player can actually use this item, if it's not in a special bag
		if (!(invbag_flags(pBag) & InvBagFlag_SpecialBag) && (!(pItemDef->flags & kItemDefFlag_CanUseUnequipped) ||
			!itemdef_VerifyUsageRestrictions(iPartitionIdx, pEnt, pItemDef, iMinItemLevel, NULL, -1)))
		{
			continue;
		}

		// If your combat level is being controlled, and the item requires a
		//  higher level, the item applies an appropriate negative adjustment
		//  to the level of the Powers that have levels.
		if(iCharLevelControlled && item_GetMinLevel(pItem) > iCharLevelControlled)
		{
			iLevelAdjustment = iCharLevelControlled - item_GetMinLevel(pItem);
		}

		//loop for all powers on the item
		for(iPower=NumPowers-1;iPower>=0;iPower--)
		{			
			PowerDef *pdef;
			bool bRollFailed = false;

			ItemPowerDefRef *pItemPowerDefRef = item_GetItemPowerDefRef(pItem, iPower);
			ItemPowerDef *pItemPowerDef = pItemPowerDefRef ? GET_REF(pItemPowerDefRef->hItemPowerDef) : NULL;

			if (g_ItemConfig.bUseUniqueIDsForItemPowerDefRefs && // Gem rolls only work when item power def refs have unique IDs
				pItemPowerDefRef &&
				iPower >= iGemPowersStartIndex && pItem->pSpecialProps)
			{
				// This is a power from a gem
				// Find which gem slot we are in
				S32 iSlot = 0;
				S32 iPowerOffset = iGemPowersStartIndex;

				for (iSlot=0; iSlot < eaSize(&pItem->pSpecialProps->ppItemGemSlots); iSlot++)
				{
					ItemDef *pGemDef = GET_REF(pItem->pSpecialProps->ppItemGemSlots[iSlot]->hSlottedItem);

					if(pGemDef)
					{
						int iGemPowers = eaSize(&pGemDef->ppItemPowerDefRefs);

						if (iGemPowers + iPowerOffset > iPower)
						{
							// Found the slot. Look at the roll result
							ItemGemSlot *pItemGemSlot = pItem->pSpecialProps->ppItemGemSlots[iSlot];
							S32 iRollIndex;

							if (pItemGemSlot &&
								pItemGemSlot->pRollData &&
								(iRollIndex = eaIndexedFindUsingInt(&pItemGemSlot->pRollData->ppRollResults, pItemPowerDefRef->uID)) >= 0)
							{
								bRollFailed = !pItemGemSlot->pRollData->ppRollResults[iRollIndex]->bSuccess;
							}

							break;
						}
						else
						{
							// Jump to the 
							iPowerOffset += iGemPowers;
						}
					}
				}
			}
			
			pdef = pItemPowerDef ? GET_REF(pItemPowerDef->hPower) : NULL;
			if ( pdef && !bRollFailed )
			{
				//make sure power is active (equipped or flagged)
				if(	item_ItemPowerActive(pEnt, pBag, pItem, iPower) )
				{
					if(pdef->eType==kPowerType_Innate)
					{
						int iPowerLevel = iLevel;
						F32 fItemScale = item_GetItemPowerScale(pItem, iPower);

						// If the power is from a gem, and the level was set from the quality on the base item
						// ignore the level boost from the quality flag.
						if(pItemDef->flags & kItemDefFlag_LevelFromQuality && iPower >= NumPowers - iNumGemPowers)
						{
							int iGemLevel = item_GetGemPowerLevel(pItem);

							if(iGemLevel + iLevelAdjustment > 0)
							{
								iPowerLevel = iGemLevel;
							}
							else
							{
								continue;
							}
						}
						else if(item_GetLevel(pItem))
						{
							// Apply the level adjustment.  If the item would be dropped to level 0 or below, it's removed entirely
							if(item_GetLevel(pItem) + iLevelAdjustment > 0)
							{
								iPowerLevel = item_GetLevel(pItem) + iLevelAdjustment;
							}
							else
							{
								continue;
							}
						}

						combateval_ContextSetupSimple(p, iPowerLevel,pItem);

						for(c=0;c<eaSize(&pdef->ppOrderedMods);c++)
						{
							AttribModDef *pmoddef = pdef->ppOrderedMods[c];
							if(IS_NORMAL_ATTRIB(pmoddef->offAttrib))
							{
								F32 fMag = combateval_EvalNew(iPartitionIdx,pmoddef->pExprMagnitude,kCombatEvalContext_Simple,NULL);
								F32 *pf = (F32*)((char*)pSet + pmoddef->offAspect + pmoddef->offAttrib);
								if(pClass && pmoddef->eType&kModType_Magnitude && pmoddef->pchTableDefault)
								{
									F32 fTableScale = class_powertable_Lookup(pClass,pmoddef->pchTableDefault,iPowerLevel-1);
									fMag *= fTableScale;
								}
								fMag *= fItemScale;
								*pf += fMag;

								if (gConf.bSendInnateAttribModData && 
									fMag != 0.f &&
									pmoddef->offAspect == kAttribAspect_BasicAbs) // Only process basic absolute aspect for now
								{
									InnateAttribMod *pMod = StructCreate(parse_InnateAttribMod);
									pMod->eSource = InnateAttribModSource_Item;
									pMod->eAttrib = pmoddef->offAttrib;
									pMod->eAspect = pmoddef->offAspect;
									pMod->fMag = fMag;
									SET_HANDLE_FROM_REFERENT(g_hPowerDefDict, pmoddef->pPowerDef, pMod->hPowerDef);
									COPY_HANDLE(pMod->hItemDef, pItem->hItem);
									eaPush(&p->pInnateAttribModData->ppInnateAttribMods, pMod);
								}

								bHasEffect = true;
							}
							else if (pmoddef->offAttrib == kAttribType_SpeedCooldownCategory)
							{
								mod_ProcessInnateSpeedCooldown(iPartitionIdx, pmoddef, pSet, p, pClass, iLevel, 1.0f);
								bHasEffect = true;
							}
							else if (pmoddef->offAttrib == kAttribType_DynamicAttrib)
							{
								//F32 fTableScale = class_powertable_Lookup(pClass,pmoddef->pchTableDefault,iPowerLevel-1);
								mod_ProcessDynamicAttrib(iPartitionIdx,pmoddef,p,pClass,iPowerLevel,1.0,pSet,false, fItemScale);
								bHasEffect = true;
							}
						}
					}
				}
			}
		}

		if (gConf.bRoundItemStatsOnApplyToChar)
		{
			// I'm shadowing the loop above.  Kind of weak, but I need to visit the attribs again, or we need to rewrite this function to collect
			// all the attrib mods, then apply them, like the autodesc does [RMARR - 1/15/13]
			//loop for all powers on the item
			for(iPower=NumPowers-1;iPower>=0;iPower--)
			{	
				ItemPowerDefRef *pItemPowerDefRef = item_GetItemPowerDefRef(pItem, iPower);
				ItemPowerDef *pItemPowerDef = pItemPowerDefRef ? GET_REF(pItemPowerDefRef->hItemPowerDef) : NULL;
				PowerDef *pdef = pItemPowerDef ? GET_REF(pItemPowerDef->hPower) : NULL;
				if ( pdef )
				{
					for(c=0;c<eaSize(&pdef->ppOrderedMods);c++)
					{
						AttribModDef *pmoddef = pdef->ppOrderedMods[c];
						if(IS_NORMAL_ATTRIB(pmoddef->offAttrib))
						{
							F32 *pf = (F32*)((char*)pSet + pmoddef->offAspect + pmoddef->offAttrib);
							*pf = (F32)round(*pf);
						}
					}
				}
			}
		}
	}

	bagiterator_Destroy(iter);

	PERFINFO_AUTO_STOP();

	return bHasEffect;
}

static bool AccrueStatPoints(Character *p, AttribAccrualSet *pSet, AssignedStats *pStats)
{
	F32 *pf;
	
	pf = (F32*)((char*)pSet + kAttribAspect_BasicAbs + pStats->eType);
	*pf += pStats->iPoints;

	if (gConf.bSendInnateAttribModData && 
		pStats->iPoints != 0)
	{
		InnateAttribMod *pMod = StructCreate(parse_InnateAttribMod);
		pMod->eSource = InnateAttribModSource_StatPoint;
		pMod->eAttrib = pStats->eType;
		pMod->eAspect = kAttribAspect_BasicAbs;
		pMod->fMag = pStats->iPoints;
		eaPush(&p->pInnateAttribModData->ppInnateAttribMods, pMod);
	}

	return true;

}

static AttribAccrualSet *GetInnateStatAccrual(Character *p)
{
	AttribAccrualSet *pResult = &s_AttribAccrualSetEmpty;
	bool bHasPoints = false;
	int i,s=eaSize(&p->ppAssignedStats);

	if(s)
	{
		PERFINFO_AUTO_START_FUNC();

		pResult = MP_ALLOC(AttribAccrualSet);
		AttribAccrualSet_SetDefaultValues(pResult, true);

		for(i=0;i<s;i++)
			bHasPoints = AccrueStatPoints(p, pResult,p->ppAssignedStats[i]) | bHasPoints;

		if(!bHasPoints)
		{
			DestroyInnateAccrualSet(pResult);
			pResult = &s_AttribAccrualSetEmpty;
		}

		PERFINFO_AUTO_STOP();
	}

	return pResult;
}

static AttribAccrualSet *GetInnateEquipAccrual(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar)
{
	AttribAccrualSet *pResult = NULL;

	Entity* pEnt = pchar->pEntParent;
	GlobalType eTypeEnt = pEnt ? entGetType(pEnt) : GLOBALTYPE_NONE;

	if(pEnt && eTypeEnt==GLOBALTYPE_ENTITYPLAYER || eTypeEnt==GLOBALTYPE_ENTITYSAVEDPET || g_CombatConfig.bCritterEquipment)
	{
		bool bHasEffect = false;
		S32 iBag;
		S32 iLevelCombat = entity_GetCombatLevel(pEnt);
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		PERFINFO_AUTO_START_FUNC();

		pResult = MP_ALLOC(AttribAccrualSet);
		AttribAccrualSet_SetDefaultValues(pResult, true);

		//loop for all bags
		if(pEnt->pInventoryV2)
		{
			for (iBag = 0; iBag < eaSize(&pEnt->pInventoryV2->ppInventoryBags); iBag++)
			{
				InventoryBag *bag = pEnt->pInventoryV2->ppInventoryBags[iBag];
				if(bag)
					bHasEffect = AccrueBag(iPartitionIdx,pResult,bag,pchar,character_GetClassCurrent(pchar),iLevelCombat,pExtract) | bHasEffect;
			}
		}

		if(!bHasEffect)
		{
			DestroyInnateAccrualSet(pResult);
			pResult = &s_AttribAccrualSetEmpty;
		}

		PERFINFO_AUTO_STOP();
	}

	if(!pResult)
		pResult = &s_AttribAccrualSetEmpty;

	return pResult;
}

static __forceinline void AddAllAttribs(CharacterAttribs *pTarget, CharacterAttribs *pSource)
{
	S32 i = sizeof(CharacterAttribs) / sizeof(F32);
	F32 *pfTarget = (F32*)(pTarget);
	F32 *pfSource = (F32*)(pSource);
	do {
		*pfTarget += *pfSource;
		++pfTarget; 
		++pfSource;
	} while(--i > 0);
}

static __forceinline void MultAllAttribs(CharacterAttribs *pTarget, CharacterAttribs *pSource)
{
	S32 i = sizeof(CharacterAttribs) / sizeof(F32);
	F32 *pfTarget = (F32*)(pTarget);
	F32 *pfSource = (F32*)(pSource);
	do {
		*pfTarget *= *pfSource;
		++pfTarget; 
		++pfSource;
	} while(--i > 0);
}

static void ApplyInnateSetToAccuralSet(Character *p, AttribAccrualSet *pSet)
{
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrBasicAbs, &pSet->CharacterAttribs.attrBasicAbs);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrBasicFactNeg, &pSet->CharacterAttribs.attrBasicFactNeg);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrBasicFactPos, &pSet->CharacterAttribs.attrBasicFactPos);

	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrStrBase, &pSet->CharacterAttribs.attrStrBase);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrStrFactPos, &pSet->CharacterAttribs.attrStrFactPos);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrStrFactNeg, &pSet->CharacterAttribs.attrStrFactNeg);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrStrFactBonus, &pSet->CharacterAttribs.attrStrFactBonus);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrStrAdd, &pSet->CharacterAttribs.attrStrAdd);

	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrResTrue, &pSet->CharacterAttribs.attrResTrue);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrResBase, &pSet->CharacterAttribs.attrResBase);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrResFactPos, &pSet->CharacterAttribs.attrResFactPos);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrResFactNeg, &pSet->CharacterAttribs.attrResFactNeg);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrResFactBonus, &pSet->CharacterAttribs.attrResFactBonus);
	AddAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrImmunity, &pSet->CharacterAttribs.attrImmunity);

	MultAllAttribs(&p->pInnateAccrualSet->CharacterAttribs.attrStrMult, &pSet->CharacterAttribs.attrStrMult);
}

static void ApplyEquipToAccrual(Character *p)
{
	if(p->pInnateEquipAccrualSet != &s_AttribAccrualSetEmpty)
	{
		ApplyInnateSetToAccuralSet(p, p->pInnateEquipAccrualSet);
	}
}

static void ApplyPointsToAccrual(Character *p)
{
	if(p->pInnateStatPointsSet != &s_AttribAccrualSetEmpty)
	{
		ApplyInnateSetToAccuralSet(p, p->pInnateStatPointsSet);
	}

}

static void AccruePowerTreeAttributes(SA_PARAM_NN_VALID Character *pchar)
{
	int i,j;
	CharacterAttribs *pattrBasic = &pchar->pInnateAccrualSet->CharacterAttribs.attrBasicAbs;
	PowerTree **ppPowerTrees = NULL;

	Entity_getAllPowerTreesFromPuppets(pchar->pEntParent,&ppPowerTrees);
	for(i=eaSize(&ppPowerTrees)-1; i>=0; i--)
	{
		for(j=eaSize(&ppPowerTrees[i]->ppNodes)-1; j>=0; j--)
		{
			PTNode *pnode = ppPowerTrees[i]->ppNodes[j];
			PTNodeDef *pnodedef = GET_REF(pnode->hDef);
			if(pnodedef && pnodedef->eAttrib>=0)
			{
				F32 *pfTargetAccrue;

				// Default result is just the rank of the node + 1 (since rank is 0-based)
				F32 fResult = pnode->iRank + 1;

				if(pnodedef->pchAttribPowerTable)
				{
					// Use a table lookup instead
					fResult = powertable_Lookup(pnodedef->pchAttribPowerTable,pnode->iRank);
				}

				if(IS_SPECIAL_ATTRIB(pnodedef->eAttrib))
				{
					// Special attrib, see if it unrolls
					int k;
					AttribType *pAttribs = attrib_Unroll(pnodedef->eAttrib);
					for(k=eaiSize(&pAttribs)-1 ; k>=0; k--)
					{
						pfTargetAccrue = F32PTR_OF_ATTRIB(pattrBasic,pAttribs[k]);
						*pfTargetAccrue += fResult;
					}
				}
				else
				{
					// Normal attrib, just accrue it
					pfTargetAccrue = F32PTR_OF_ATTRIB(pattrBasic,pnodedef->eAttrib);
					*pfTargetAccrue += fResult;
				}
			}
		}
	}

	eaDestroy(&ppPowerTrees);
}

static void AccruePowerStats(SA_PARAM_NN_VALID Character *pchar)
{
	int i;
	CharacterClass *pClass = character_GetClassCurrent(pchar);
	PowerStat **ppStats = pClass && pClass->ppStatsFull ? pClass->ppStatsFull : g_PowerStats.ppPowerStats;
	int s = eaSize(&ppStats);
	char *pchTrickAccrualSet = (char*)pchar->pInnateAccrualSet;
	
	// Stupid check to make sure this even makes sense, since apparently people now try to call this on
	//  fake entities
	if(!pchar->pattrBasic)
		return;

	if (gConf.bUsePowerStatBonuses)
	{
		if (pchar->pPowerStatBonusData == NULL)
		{
			pchar->pPowerStatBonusData = StructCreate(parse_PowerStatBonusData);
			eaIndexedEnable(&pchar->pPowerStatBonusData->ppBonusList, parse_PowerStatBonus);
		}
		eaClearStruct(&pchar->pPowerStatBonusData->ppBonusList, parse_PowerStatBonus);
		entity_SetDirtyBit(pchar->pEntParent, parse_PowerStatBonusData, pchar->pPowerStatBonusData, false);
	}

	for(i=0; i<s; i++)
	{
		static F32 *pfStatValues = NULL;
		F32 *pfTargetAccrue;
		F32 fResult;
		PowerStat *pStat = ppStats[i];
		PTNode *pNode = NULL;

		if(!powerstat_Active(pStat,pchar,&pNode))
			continue;

		if(eaSize(&pStat->ppSourceStats))
		{
			int j, t = eaSize(&pStat->ppSourceStats);
			for(j = 0; j<t; j++)
			{
				eafPush(&pfStatValues, *F32PTR_OF_ATTRIB(pchar->pattrBasic,pStat->ppSourceStats[j]->offSourceAttrib));
			}
		}
		else if(pStat->pchSourceNumericItem)
		{
			eafPush(&pfStatValues, (float)inv_GetNumericItemValue(pchar->pEntParent,pStat->pchSourceNumericItem));
		}

		if(eafSize(&pfStatValues))
			fResult = powerstat_Eval(pStat,pClass,pfStatValues,entity_GetCombatLevel(pchar->pEntParent),pNode?pNode->iRank+1:0);
		else
			fResult = 0.f;

		if (gConf.bUsePowerStatBonuses)
		{
			// Store any positive/negative bonus from stat points
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pStat->ppSourceStats, PowerStatSource, pCurrentSource)
			{
				PowerStatBonus *pBonus;
				PowerStatBonusEntry *pBonusEntry;
				S32 iIndexFound = eaIndexedFindUsingInt(&pchar->pPowerStatBonusData->ppBonusList, pCurrentSource->offSourceAttrib);
				if (iIndexFound >= 0)
				{
					pBonus = pchar->pPowerStatBonusData->ppBonusList[iIndexFound];
				}
				else
				{
					pBonus = StructCreate(parse_PowerStatBonus);
					pBonus->offSourceAttrib = pCurrentSource->offSourceAttrib;
					eaIndexedAdd(&pchar->pPowerStatBonusData->ppBonusList, pBonus);
				}

				pBonusEntry = StructCreate(parse_PowerStatBonusEntry);
				pBonusEntry->offTargetAttrib = pStat->offTargetAttrib;
				pBonusEntry->offTargetAspect = pStat->offTargetAspect;
				pBonusEntry->fBonus = fResult;
				eaPush(&pBonus->ppEntries, pBonusEntry);
			}
			FOR_EACH_END
		}

		if(IS_SPECIAL_ATTRIB(pStat->offTargetAttrib))
		{
			// Special attrib, see if it unrolls
			int j;
			AttribType *pAttribs = attrib_Unroll(pStat->offTargetAttrib);
			for(j=eaiSize(&pAttribs)-1; j>=0; j--)
			{
				pfTargetAccrue = (F32*)(pchTrickAccrualSet + pStat->offTargetAspect + pAttribs[j]);
				processInnateAttribAccrual(pfTargetAccrue, fResult, pStat->offTargetAspect);
			}
		}
		else
		{
			// Normal attrib, just accrue it
			pfTargetAccrue = (F32*)(pchTrickAccrualSet + pStat->offTargetAspect + pStat->offTargetAttrib);
			processInnateAttribAccrual(pfTargetAccrue, fResult, pStat->offTargetAspect);
		}
		eafClearFast(&pfStatValues);
	}
}

static void character_AccrueSpeedCooldownInnateFromSet(Character* pChar, AttribAccrualSet* pSet)
{
	int i;
	for (i = eaSize(&pSet->ppSpeedCooldown)-1; i >= 0; i--)
	{
		CooldownRateModifier* pSpeedCooldown = pSet->ppSpeedCooldown[i];
		CooldownRateModifier* pSpeedCooldownFinal = eaIndexedGetUsingInt(&pChar->pInnateAccrualSet->ppSpeedCooldown, pSpeedCooldown->iPowerCategory);
		if (!pSpeedCooldownFinal)
		{
			pSpeedCooldownFinal = StructCreate(parse_CooldownRateModifier);
			pSpeedCooldownFinal->iPowerCategory = pSpeedCooldown->iPowerCategory;
			if (!pChar->pInnateAccrualSet->ppSpeedCooldown)
				eaIndexedEnable(&pChar->pInnateAccrualSet->ppSpeedCooldown, parse_CooldownRateModifier);
			eaPush(&pChar->pInnateAccrualSet->ppSpeedCooldown, pSpeedCooldownFinal);
		}
		pSpeedCooldownFinal->fBasicAbs += pSpeedCooldown->fBasicAbs;
		pSpeedCooldownFinal->fBasicNeg += pSpeedCooldown->fBasicNeg;
		pSpeedCooldownFinal->fBasicPos += pSpeedCooldown->fBasicPos;
	}
}

static void character_AccrueSpeedCooldownInnate(Character* pChar)
{
	int i;
	assert(pChar->pInnateAccrualSet);
	
	if (pChar->pInnateEquipAccrualSet)
	{
		character_AccrueSpeedCooldownInnateFromSet(pChar, pChar->pInnateEquipAccrualSet);
	}
	for (i = eaSize(&pChar->pInnateAccrualSet->ppSpeedCooldown)-1; i >= 0; i--)
	{
		CooldownRateModifier* pSpeedCooldown = pChar->pInnateAccrualSet->ppSpeedCooldown[i];
		F32 fBasicBase = pChar->pattrBasic ? pChar->pattrBasic->fSpeedCooldown : 1.0f;
		pSpeedCooldown->fValue = pSpeedCooldown->fBasicAbs + fBasicBase * (1.0f + pSpeedCooldown->fBasicPos) / (1.0f + pSpeedCooldown->fBasicNeg);
	}
}

// Clears the innate attrib mod data for the given source type
static void character_ClearInnateAttribModData(Character *pChar, InnateAttribModSource eSource)
{
	if (gConf.bSendInnateAttribModData)
	{
		if (pChar->pInnateAttribModData)
		{
			S32 i;		
			for (i = eaSize(&pChar->pInnateAttribModData->ppInnateAttribMods) - 1; i >= 0; i--)
			{
				InnateAttribMod *pMod = pChar->pInnateAttribModData->ppInnateAttribMods[i];

				if (pMod->eSource == eSource)
				{
					eaRemove(&pChar->pInnateAttribModData->ppInnateAttribMods, i);
					StructDestroy(parse_InnateAttribMod, pMod);				
				}
			}
		}
		else
		{
			pChar->pInnateAttribModData = StructCreate(parse_InnateAttribModData);
		}

		entity_SetDirtyBit(pChar->pEntParent, parse_PowerStatBonusData, pChar->pInnateAttribModData, false);
	}
}

void character_FakeEntInitAccrual(Character* pchar)
{
	character_ClearInnateAttribModData(pchar, InnateAttribModSource_Power);
	pchar->pInnatePowersAccrualSet = GetInnatePowersAccrual(PARTITION_CLIENT, pchar);
	character_ClearInnateAttribModData(pchar, InnateAttribModSource_Item);
	pchar->pInnateEquipAccrualSet = GetInnateEquipAccrual(PARTITION_CLIENT, pchar);
	character_ClearInnateAttribModData(pchar, InnateAttribModSource_StatPoint);
	pchar->pInnateStatPointsSet = GetInnateStatAccrual(pchar);
	pchar->pInnateAccrualSet = StructCreate(parse_AttribAccrualSet);
	StructCopyAll(parse_AttribAccrualSet, pchar->pInnatePowersAccrualSet, pchar->pInnateAccrualSet);

	ApplyEquipToAccrual(pchar);

	ApplyPointsToAccrual(pchar);
	AccruePowerTreeAttributes(pchar);
	AccruePowerStats(pchar);
}

// Fetches the base accrual set for this character, which is based on: 
//  class, level and innate powers
//  stats
// Optionally sets a dirty bit to true if it had to do any recalculation
AttribAccrualSet *character_GetInnateAccrual(int iPartitionIdx, Character *pchar, S32 *pbDirtyOut)
{
	if(entIsServer())
	{
		int i;
		EntityRef er;

		if(!pchar->pInnatePowersAccrualSet)
		{
			// No innate powers accrued.  Mark stats dirty, and get the powers accrual.
			character_DirtyInnateAccrual(pchar);
			character_ClearInnateAttribModData(pchar, InnateAttribModSource_Power);
			pchar->pInnatePowersAccrualSet = GetInnatePowersAccrual(iPartitionIdx, pchar);
		}

		if(!pchar->pInnateEquipAccrualSet)
		{
			// No innate equip accrued.  Mark stats dirty, and get the equip accrual
			character_DirtyInnateAccrual(pchar);
			character_ClearInnateAttribModData(pchar, InnateAttribModSource_Item);
			pchar->pInnateEquipAccrualSet = GetInnateEquipAccrual(iPartitionIdx, pchar);
		}

		if(!pchar->pInnateStatPointsSet)
		{
			// No innate stat points accrued, Make stats dirty, and get the stat accrual.
			character_DirtyInnateAccrual(pchar);
			character_ClearInnateAttribModData(pchar, InnateAttribModSource_StatPoint);
			pchar->pInnateStatPointsSet = GetInnateStatAccrual(pchar);
		}

		if(!pchar->pInnateAccrualSet)
		{
			GlobalType eEntType = entGetType(pchar->pEntParent);

			PERFINFO_AUTO_START("InnateAccrual",1);

			// Make sure the character accrues mods next tick
			pchar->bSkipAccrueMods = false;

			// No stats accrued.  Copy the powers accrual and apply equip, then points, then apply stats
			assert(pchar->pInnatePowersAccrualSet);
			pchar->pInnateAccrualSet = StructCreate(parse_AttribAccrualSet);
			StructCopyAll(parse_AttribAccrualSet, pchar->pInnatePowersAccrualSet, pchar->pInnateAccrualSet);

			// Post process speed cooldown data
			character_AccrueSpeedCooldownInnate(pchar);

			// For players and saved pets, apply gear and stats
			if(eEntType==GLOBALTYPE_ENTITYPLAYER || eEntType==GLOBALTYPE_ENTITYSAVEDPET)
			{
				// Check for BecomeCritter restriction, also check the TickPhaseTwo flag
				//  because this can be called inside character_TickPhaseTwo, where the
				//  main flag may be temporarily false.
				if(!pchar->bBecomeCritter && !pchar->bBecomeCritterTickPhaseTwo)
					ApplyEquipToAccrual(pchar);

				ApplyPointsToAccrual(pchar);
				AccruePowerTreeAttributes(pchar);
				AccruePowerStats(pchar);
			}
			else 
			{
				if (g_CombatConfig.bCritterEquipment && !pchar->bBecomeCritter && !pchar->bBecomeCritterTickPhaseTwo)
				{
					ApplyEquipToAccrual(pchar);
				}
				if(g_CombatConfig.bCritterStats) //Allow critters to use stats if this feature is turned on
				{
					AccruePowerStats(pchar);
				}
			}

			// Mark everything in the set dirty
			entity_SetDirtyBit(pchar->pEntParent,parse_AttribAccrualSet, pchar->pInnateAccrualSet, false);

			// There's no magical 'mark all the substructs dirty' function
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrBasicAbs, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrBasicFactPos, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrBasicFactNeg, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrStrBase, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrStrFactPos, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrStrFactNeg, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrStrFactBonus, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrStrMult, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrStrAdd, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrResTrue, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrResBase, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrResFactPos, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrResFactNeg, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrResFactBonus, false);
			entity_SetDirtyBit(pchar->pEntParent,parse_CharacterAttribs, &pchar->pInnateAccrualSet->CharacterAttribs.attrImmunity, false);

			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

			if(pbDirtyOut)
			{
				*pbDirtyOut = true;
			}

			// Note that this has been regenerated.  Loops directly instead of calling ea32FindAndRemoveFast in
			//  order to loop in reverse order, since the last ent on the list is the one we're most likely to be
			//  regenerating.
			er = entGetRef(pchar->pEntParent);
			for(i=ea32Size(&s_perDirtyInnate)-1; i>=0; i--)
			{
				if(s_perDirtyInnate[i]==er)
				{
					ea32RemoveFast(&s_perDirtyInnate,i);
					break;
				}
			}

			character_Wake(pchar);
			
			PERFINFO_AUTO_STOP();
		}
	}

	return pchar->pInnateAccrualSet;
}

/** Early concept optimization structures for the new mod processing system
typedef struct AttribAccumCache
{
	F32 fBase;
	F32 fAbs;
	F32 fPos;
	F32 fNeg;
	AttribMod **ppMods;
} AttribAccumCache;

typedef struct CharacterAccumCache
{
	Character *p;
	U32 uiNow;
	AttribAccumCache **ppRes;
	AttribAccumCache **ppStr;
} CharacterAccumCache;

static CharacterAccumCache s_AccumCache = {0};

static AttribAccumCache *GetStrAccumCache(Character *p, AttribType offAttrib)
{
	U32 uiNow = pmTimestamp(0.0f);
	if(p!=s_AccumCache.p || uiNow!=s_AccumCache.uiNow)
	{
		int i;
		for(i=eaSize(&s_AccumCache.ppRes)-1; i>=0 i--)
	}
}
*/

// 
bool character_TriggerAttribModCheckChance(int iPartitionIdx, Character *pChar, EntityRef erModOwner, Expression *pExprChance)
{
	if (pExprChance)
	{
		F32 f;
		Entity *pModOwnerE = erModOwner ? entFromEntityRef(iPartitionIdx, erModOwner) : NULL;
		combateval_ContextSetupTarget(SAFE_MEMBER(pModOwnerE,pChar), pChar, NULL);
		
		f = combateval_EvalNew(iPartitionIdx, pExprChance, kCombatEvalContext_Target, NULL);
		return f > randomPositiveF32();
	}
	
	return true;
}

// Checks to see if the Character triggers a Power apply on themselves based on the damage dealt.
static void CharacterCheckDamageTriggers(int iPartitionIdx,
										 Character *pchar,
										 Character *pcharTarget,
										 AttribType offAttrib,
										 F32 fMag,
										 F32 fMagNoResist,
										 AttribMod *pmodDamage,
										 bool bOutgoing,
										 GameAccountDataExtract *pExtract)
{
	int i, s = eaSize(&pchar->ppModsDamageTrigger);
	if(s)
	{
		PERFINFO_AUTO_START_FUNC();
		for(i=s-1; i>=0; i--)
		{
			AttribMod *pmod = pchar->ppModsDamageTrigger[i];
			DamageTriggerParams *pparams = (DamageTriggerParams*)pmod->pDef->pParams;
			S32 bFail = false;
			Character *pcharTargetType = mod_GetApplyTargetTypeCharacter(iPartitionIdx,pmod,&bFail);
			
			if(!bFail
				&& pparams
				&& pparams->bHeal==(U32)(fMagNoResist<0)
				&& pparams->bOutgoing==(U32)bOutgoing
				&& attrib_Matches(offAttrib,pparams->offAttrib)
				&& (!pparams->bMagnitudeIsCharges || pmod->fMagnitude >= 1)
				&& (!pparams->pExprChance || character_TriggerAttribModCheckChance(iPartitionIdx, pchar, pmod->erOwner, pparams->pExprChance))
				&& moddef_AffectsModFromDirectionChk(pmod->pDef, pchar, pmodDamage)
				&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmod->pDef,pchar,pmod,pmodDamage->pDef,pmodDamage,pmodDamage->pDef->pPowerDef))
			{
				PowerDef *pdef = GET_REF(pparams->hDef);
				if(pdef && pmod->pSourceDetails)
				{
					PATrigger trigger = {0};
					EntityRef erTarget = entGetRef(pchar->pEntParent); // kDamageTriggerEntity_Self
					Entity *pTargetEntity = NULL;
					ApplyUnownedPowerDefParams applyParams = {0};
					static Power **s_eaPowEnhancements = NULL;
										
					if(pparams->eTarget==kDamageTriggerEntity_SelfOwner)
					{
						erTarget = pchar->pEntParent->erOwner;
					}
					else if(pparams->eTarget==kDamageTriggerEntity_DamageOwner)
					{
						erTarget = pmodDamage->erOwner;
					}
					else if(pparams->eTarget==kDamageTriggerEntity_DamageSource)
					{
						erTarget = pmodDamage->erSource;
					}
					else if(pparams->eTarget==kDamageTriggerEntity_DamageTarget)
					{
						erTarget = entGetRef(pcharTarget->pEntParent);
					}
					else if(pparams->eTarget==kDamageTriggerEntity_TriggerOwner)
					{
						erTarget = pmod->erOwner;
					}
					else if(pparams->eTarget==kDamageTriggerEntity_TriggerSource)
					{
						erTarget = pmod->erSource;
					}

					// we have to check if this entity is a projectile, and if it is get it's owner as the creator
					pTargetEntity = entFromEntityRef(iPartitionIdx, erTarget);
					if (pTargetEntity && entGetType(pTargetEntity) == GLOBALTYPE_ENTITYPROJECTILE)
					{
						erTarget = pTargetEntity->erCreator;
					}

					// Set up the trigger data
					trigger.fMag = fabs(fMag);
					trigger.fMagPreResist = fabs(fMagNoResist);
					// TODO(JW): This should probably be smarter about where it gets the max from, since
					//  this could be Power damage.
					if(pchar->pattrBasic->fHitPointsMax)
						trigger.fMagScale = trigger.fMag / pchar->pattrBasic->fHitPointsMax;

					applyParams.pmod = pmod;
					applyParams.erTarget = erTarget;
					applyParams.pcharSourceTargetType = pcharTargetType;
					applyParams.pclass = GET_REF(pmod->pSourceDetails->hClass);
					applyParams.iLevel = pmod->pSourceDetails->iLevel;
					applyParams.iIdxMulti = pmod->pSourceDetails->iIdxMulti;
					applyParams.fTableScale = pmod->pSourceDetails->fTableScale;
					applyParams.iSrcItemID = pmod->pSourceDetails->iItemID;
					applyParams.bLevelAdjusting = pmod->pSourceDetails->bLevelAdjusting;
					applyParams.ppStrengths = pmod->ppApplyStrengths;
					applyParams.pCritical = pmod->pSourceDetails->pCritical;
					applyParams.erModOwner = (pparams->bOwnedByDamager ? pmodDamage->erOwner : pmod->erOwner);
					applyParams.uiApplyID = pmod->uiApplyID;
					applyParams.fHue = pmod->fHue;
					applyParams.pTrigger = &trigger;
					applyParams.pExtract = pExtract;
					applyParams.bCountModsAsPostApplied = true;

					// if the power is weapon based, if the mod has an owner use that as the weapon picker
					if (pdef->bWeaponBased && pmod->erOwner)
					{
						Entity *pOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
						applyParams.pCharWeaponPicker = SAFE_MEMBER(pOwner, pChar);
					}

					if(pmod->erOwner)
					{
						Entity *pModOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
						if (pModOwner && pModOwner->pChar)
						{
							power_GetEnhancementsForAttribModApplyPower(iPartitionIdx, pModOwner->pChar, 
																		pmod, EEnhancedAttribList_DEFAULT, 
																		pdef, &s_eaPowEnhancements);
							applyParams.pppowEnhancements = s_eaPowEnhancements;
						}
					}

					character_ApplyUnownedPowerDef(iPartitionIdx, pchar, pdef, &applyParams);

					eaClear(&s_eaPowEnhancements);

					// Decrement charges, and expire if necessary
					if(pparams->bMagnitudeIsCharges)
					{
						pmod->fMagnitude -= 1;
						if(pmod->fMagnitude < 1)
						{
							character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
						}
					}
				}
			}
		}
		PERFINFO_AUTO_STOP();
	}
}

static void ModCheckDamageTriggers(int iPartitionIdx,
								   AttribMod *pmodDamage,
								   Character *pcharTarget,
								   AttribType offAttrib,
								   F32 fMag,
								   F32 fMagNoResist,
								   GameAccountDataExtract *pExtract)
{
	Entity *eOwner;

	CharacterCheckDamageTriggers(iPartitionIdx,pcharTarget,pcharTarget,offAttrib,fMag,fMagNoResist,pmodDamage,false,pExtract);

	if(pmodDamage->erSource!=pmodDamage->erOwner)
	{
		Entity *eSource = entFromEntityRef(iPartitionIdx,pmodDamage->erSource);
		if(eSource && eSource->pChar && !eSource->pChar->ppApplyStrengths)
		{
			// The source is different than the owner, but it's NOT a strength-locked pet, so it
			//  is the damager for the purposes of outgoing triggers
			CharacterCheckDamageTriggers(iPartitionIdx,eSource->pChar,pcharTarget,offAttrib,fMag,fMagNoResist,pmodDamage,true,pExtract);
			return;
		}
	}

	// Didn't give the outgoing trigger to the source, so give it to the owner
	eOwner = entFromEntityRef(iPartitionIdx,pmodDamage->erOwner);
	if(eOwner && eOwner->pChar)
	{
		CharacterCheckDamageTriggers(iPartitionIdx,eOwner->pChar,pcharTarget,offAttrib,fMag,fMagNoResist,pmodDamage,true,pExtract);
	}
}


// Calculates all the mitigation mechanics
void character_ModGetMitigators(int iPartitionIdx, 
								SA_PARAM_NN_VALID AttribMod *pmod, 
								SA_PARAM_NN_VALID AttribModDef *pmoddef, 
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_OP_VALID F32 *pfResTrueOut,
								SA_PARAM_NN_VALID F32 *pfResistOut, 
								SA_PARAM_OP_VALID F32 *pfImmuneOut, 
								SA_PARAM_NN_VALID F32 *pfAvoidOut, 
								SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsOut)
{
	bool bGetImmunities = pmoddef->fSensitivityImmune > 0.0f;

	PERFINFO_AUTO_START_FUNC();
	// Return the basic magnitude if it's not resistible
	if(pmoddef->fSensitivityResistance > 0.0f)
	{
		// Get the basic resistance and immunity
		*pfResistOut = mod_GetResist(iPartitionIdx,pmod,pmoddef,pchar,pfResTrueOut,(bGetImmunities?pfImmuneOut:NULL),peFlagsOut);
	}
	else if(bGetImmunities)
	{
		// Get just the immunity
		mod_GetResist(iPartitionIdx,pmod,pmoddef,pchar,pfResTrueOut,pfImmuneOut,peFlagsOut);
	}

	// Get the avoidance (which has already had sensitivity applied)
	*pfAvoidOut = pmod->fAvoidance;
	PERFINFO_AUTO_STOP();
}

F32 ModCalculateEffectiveMagnitude(F32 fMag, F32 fResistTrue, F32 fResist, F32 fAvoid, F32 fImmune, F32 fAdd)
{
	if(fImmune > 0.0f)
	{
		return 0.0f;
	}
	else
	{
		if (fAdd > 0.f)
			fMag += fAdd;

		fMag = (fMag * fResistTrue) / fResist;
		fMag /= (1.f + fAvoid);
		
		return fMag;
	}
}

static F32 ModGetEffectiveMagnitudeEx(	int iPartitionIdx,
										SA_PARAM_NN_VALID AttribMod *pmod,
										SA_PARAM_NN_VALID AttribModDef *pmoddef,
										SA_PARAM_NN_VALID Character *pchar,
										F32 fResistTrue,
										F32 fResist,
										F32 fAvoid,
										F32 fImmune,
										F32 fAdd)
{
	F32 fMag = pmod->fMagnitude;

	// Return the basic magnitude if the magnitude doesn't scale, or there's no character
	if(!(pmoddef->eType&kModType_Magnitude) || !pchar)
	{
		return fMag;
	}
	
	return ModCalculateEffectiveMagnitude(fMag, fResistTrue, fResist, fAvoid, fImmune, fAdd);
}


// Gets the actual effective magnitude of an AttribMod based on the character
static F32 ModGetEffectiveMagnitude(int iPartitionIdx,
									SA_PARAM_NN_VALID AttribMod *pmod,
									SA_PARAM_NN_VALID AttribModDef *pmoddef,
									SA_PARAM_NN_VALID Character *pchar)
{
	F32 fResistTrue = 1.f, fResist = 1.f, fAvoid = 0, fImmune = 0;
	F32 fMag = pmod->fMagnitude;

	// Return the basic magnitude if the magnitude doesn't scale, or there's no character
	if(!(pmoddef->eType&kModType_Magnitude) || !pchar)
	{
		return fMag;
	}

	// Triggers are not allowed
	character_ModGetMitigators(iPartitionIdx,pmod,pmoddef,pchar,&fResistTrue,&fResist,&fImmune,&fAvoid,NULL);

	return ModCalculateEffectiveMagnitude(fMag, fResistTrue, fResist, fAvoid, fImmune, 0.f);
}

// Returns the effective magnitude of an AttribMod, given the Character it's on
F32 mod_GetEffectiveMagnitude(int iPartitionIdx,
							  AttribMod *pmod,
							  AttribModDef *pmoddef,
							  Character *pchar)
{
	return ModGetEffectiveMagnitude(iPartitionIdx,pmod,pmoddef,pchar);
}

// Mod accumulator for CharacterGetResistEx
static void CharacterGetResistExAccum(AttribAspect eAspect,
									  F32 fMag,
									  F32 fMagNoCurve,
									  ResistAspectSet *pRes,
									  ResistAspectSet *pResNoCurve,
									  F32 *pfImmune)
{
	switch(eAspect)
	{
	case kAttribAspect_ResTrue:
		pRes->fResTrue += fMag;
		pResNoCurve->fResTrue  += fMagNoCurve;
		break;
	case kAttribAspect_ResBase:
		pRes->fResBase += fMag;
		pResNoCurve->fResBase += fMagNoCurve;
		break;
	case kAttribAspect_ResFactPos:
		pRes->fResFactPos += fMag;
		pResNoCurve->fResFactPos += fMagNoCurve;
		break;
	case kAttribAspect_ResFactNeg:
		pRes->fResFactNeg += fMag;
		pResNoCurve->fResFactNeg += fMagNoCurve;
		break;
	case kAttribAspect_ResFactBonus:
		pRes->fResFactBonus += fMag;
		pResNoCurve->fResFactBonus += fMagNoCurve;
		break;
	case kAttribAspect_Immunity:
		*pfImmune += fMag + fMagNoCurve;
	}
}

// Calculate a Character's resistance
// TODO(JW): Optimize the hell out of this
static F32 CharacterGetResistEx(int iPartitionIdx,
								SA_PARAM_NN_VALID Character *pchar,
								SA_PARAM_OP_VALID AttribMod *pmod,
								SA_PARAM_OP_VALID AttribModDef *pmoddef,
								AttribType offAttrib,
								SA_PARAM_OP_VALID F32 *pfResTrueOut,
								SA_PARAM_OP_VALID F32 *pfImmuneOut,
								SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsOut)
{
	int i;
	AttribMod **ppMods = NULL;
	EntityRef erSelf = pchar->pEntParent ? entGetRef(pchar->pEntParent) : 0;
	CharacterClass *pClass = character_GetClassCurrent(pchar);

	F32 fResFinal;
	F32 fResSensitivity = pmoddef ? pmoddef->fSensitivityResistance : 1.f;
	F32 fImmuneSensitivity = pmoddef ? pmoddef->fSensitivityImmune : 1.f;
	PowerDef *ppowdef = pmoddef ? pmoddef->pPowerDef : NULL;
	AttribAspect offAspect = pmoddef ? pmoddef->offAspect : kAttribAspect_BasicAbs;

	PowerDef *ppowdefState = NULL; // PowerDef for immediate State-specific effects

	// Basic sets of resistance data
	ResistAspectSet resAspects = {0};
	ResistAspectSet resAspectsNoCurve = {0};
	F32 fImmune = 0;

	// If there's no Character, the mod isn't resistible, or it doesn't scale,
	//  return 1.0 (which means not resisted)
	if(!pchar || (fResSensitivity <= 0.0f && fImmuneSensitivity <= 0.0f) || (pmoddef && pmoddef->eType==kModType_None))
	{
		if(pfImmuneOut) *pfImmuneOut = fImmune;
		return 1.0f;
	}

	// Start this a little late, but who cares
	PERFINFO_AUTO_START_FUNC();
		
	resAspects.fResBase = 1.f;

	// Special case - BasicAbs of kAttribType_KnockTo resists like kAttribType_KnockBack
	if(offAspect==kAttribAspect_BasicAbs && offAttrib==kAttribType_KnockTo)
	{
		offAttrib = kAttribType_KnockBack;
	} 
	else if(offAspect==kAttribAspect_BasicAbs && offAttrib==kAttribType_ConstantForce)
	{	// Special case - BasicAbs of kAttribType_ConstantForce resists like kAttribType_Repel
		offAttrib = kAttribType_Repel;

	}

	// Find all mods that can cause resistance to this:
	//  That means all mods on the character that are of the same
	//  attribute, affect resistance, aren't ignored, aren't yourself,
	//  pass the Personal test, and have the proper tags
	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		AttribMod *pmodChar = pchar->modArray.ppMods[i];
		AttribModDef *pmoddefChar = mod_GetDef(pmodChar);
		if(pmoddefChar
			&& (pmoddefChar->offAttrib==offAttrib || pmoddefChar->offAttrib==kAttribType_All)
			&& !pmodChar->bIgnored
			&& pmodChar!=pmod
			&& (IS_RESIST_OR_IMMUNITY_ASPECT(pmoddefChar->offAspect) 
				&& (!pmoddefChar->bAffectsOnlyOnFirstModTick || (pmod && !pmod->bPostFirstTickApply)))
			&& (!pmoddefChar->bPersonal || (pmod && pmodChar->erPersonal==pmod->erSource))
			&& moddef_AffectsModFromDirectionChk(pmoddefChar,pchar,pmod)
			&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmoddefChar,pchar,pmodChar,pmoddef,pmod,ppowdef))
		{
			eaPush(&ppMods,pchar->modArray.ppMods[i]);
		}
	}

	// If this is a regular attribute, get class attributes and innate accruals
	if(IS_NORMAL_ATTRIB(offAttrib))
	{
		AttribAccrualSet *pSet = character_GetInnateAccrual(iPartitionIdx,pchar,NULL);
		resAspects.fResBase = character_GetClassAttrib(pchar,kClassAttribAspect_Res,offAttrib);
		if(pSet)
		{
			resAspects.fResTrue = *(F32*)((char*)(&pSet->CharacterAttribs.attrResTrue) + offAttrib);
			resAspects.fResBase += *(F32*)((char*)(&pSet->CharacterAttribs.attrResBase) + offAttrib);
			resAspects.fResFactPos = *(F32*)((char*)(&pSet->CharacterAttribs.attrResFactPos) + offAttrib);
			resAspects.fResFactNeg = *(F32*)((char*)(&pSet->CharacterAttribs.attrResFactNeg) + offAttrib);
			resAspects.fResFactBonus = *(F32*)((char*)(&pSet->CharacterAttribs.attrResFactBonus) + offAttrib);
			fImmune = *(F32*)((char*)(&pSet->CharacterAttribs.attrImmunity) + offAttrib);
		}
	}

	// Filter the list for ModStackGroups
	mods_StackGroupFilter(ppMods);

	// First pass, accrue irresistible resist mods.  These mods aren't resistible
	//  themselves, which is why they're easy to apply and work against everything.
	for(i=eaSize(&ppMods)-1; i>=0; i--)
	{
		AttribModDef *pmoddefChar = mod_GetDef(ppMods[i]);
		F32 fCurve, fMagNoCurve, fMag = ppMods[i]->fMagnitude;

		// Skip resistible resist mods
		if(pmoddefChar->fSensitivityResistance > 0.0f)
			continue;

		fCurve = moddef_GetSensitivity(pmoddefChar,kSensitivityType_AttribCurve);
		fMag *= fCurve;
		fMagNoCurve = ppMods[i]->fMagnitude - fMag;

		CharacterGetResistExAccum(pmoddefChar->offAspect, fMag, fMagNoCurve, &resAspects, &resAspectsNoCurve, &fImmune);

		// In theory this could pick up flags that shouldn't be set, since they're
		//  not really for resist, so we could mask the flags we're checking for,
		//  but now we won't bother.
		if(peFlagsOut)
			*(peFlagsOut) |= pmoddefChar->eFlags;

		eaRemoveFast(&ppMods,i);
	}

	// If the aspect we're trying to resist is a resist itself,
	//  we don't apply the resistance of other resistible resist mods.
	//  If we did that, things end up interacting oddly and spiraling
	//  out of control.
	if(!IS_RESIST_OR_IMMUNITY_ASPECT(offAspect))
	{
		// Second pass, accrue resistible resist mods
		for(i=eaSize(&ppMods)-1; i>=0; i--)
		{
			AttribModDef *pmoddefChar = mod_GetDef(ppMods[i]);
			F32 fCurve, fMag, fMagNoCurve;

			// These mods are resistible, so we have to get their effective magnitude
			F32 fMagnitude = ModGetEffectiveMagnitude(iPartitionIdx,ppMods[i],pmoddefChar,pchar);
			fMag = fMagnitude;
			fCurve = moddef_GetSensitivity(pmoddefChar,kSensitivityType_AttribCurve);
			fMag *= fCurve;
			fMagNoCurve = fMagnitude - fMag;

			CharacterGetResistExAccum(pmoddefChar->offAspect, fMag, fMagNoCurve, &resAspects, &resAspectsNoCurve, &fImmune);

			// In theory this could pick up flags that shouldn't be set, since they're
			//  not really for resist, so we could mask the flags we're checking for,
			//  but now we won't bother.
			if(peFlagsOut)
				*(peFlagsOut) |= pmoddefChar->eFlags;
		}
	}

	PERFINFO_AUTO_START("Apply AttribCurves",1);
	// Apply AttribCurves
	{
		if(pClass)
		{
			AttribCurve **ppCurves = class_GetAttribCurveArray(pClass,offAttrib);
			int s = eaSize(&ppCurves);
			if(s && verify(ATTRIBASPECT_INDEX(kAttribAspect_ResFactBonus)<s))
			{
				i = ATTRIBASPECT_INDEX(kAttribAspect_ResTrue);
				if(ppCurves[i])
				{
					resAspects.fResTrue = character_ApplyAttribCurve(pchar, ppCurves[i], resAspects.fResTrue);
				}
				i = ATTRIBASPECT_INDEX(kAttribAspect_ResBase);
				if(ppCurves[i])
				{
					resAspects.fResBase = character_ApplyAttribCurve(pchar, ppCurves[i], resAspects.fResBase);
				}
				i = ATTRIBASPECT_INDEX(kAttribAspect_ResFactPos);
				if(ppCurves[i])
				{
					resAspects.fResFactPos = character_ApplyAttribCurve(pchar, ppCurves[i], resAspects.fResFactPos);
				}
				i = ATTRIBASPECT_INDEX(kAttribAspect_ResFactNeg);
				if(ppCurves[i])
				{
					resAspects.fResFactNeg = character_ApplyAttribCurve(pchar, ppCurves[i], resAspects.fResFactNeg);
				}
				i = ATTRIBASPECT_INDEX(kAttribAspect_ResFactBonus);
				if(ppCurves[i])
				{
					resAspects.fResFactBonus = character_ApplyAttribCurve(pchar, ppCurves[i], resAspects.fResFactBonus);
				}

			}
		}
	}
	resAspects.fResTrue += resAspectsNoCurve.fResTrue;
	resAspects.fResBase += resAspectsNoCurve.fResBase; 
	resAspects.fResFactPos += resAspectsNoCurve.fResFactPos; 
	resAspects.fResFactNeg += resAspectsNoCurve.fResFactNeg; 
	resAspects.fResFactBonus += resAspectsNoCurve.fResFactBonus; 
	PERFINFO_AUTO_STOP();

	//Now we check if there were any override attrib mods applied to the player
	//causing any of these values to be overridden to a giving value
	//TODO (MM): Optimize
	for(i=0;i<eaSize(&pchar->modArray.ppOverrideMods);i++)
	{
		if(pchar->modArray.ppOverrideMods[i]->pDef
			&& (((AttribOverrideParams*)pchar->modArray.ppOverrideMods[i]->pDef->pParams)->offAttrib==offAttrib
				|| ((AttribOverrideParams*)pchar->modArray.ppOverrideMods[i]->pDef->pParams)->offAttrib==kAttribType_All)
			&& IS_RESIST_OR_IMMUNITY_ASPECT(pchar->modArray.ppOverrideMods[i]->pDef->offAspect)
			&& moddef_AffectsModOrPowerChk(iPartitionIdx,pchar->modArray.ppOverrideMods[i]->pDef,pchar,pchar->modArray.ppOverrideMods[i],pmoddef,pmod,ppowdef))
		{
			F32 fMag = pchar->modArray.ppOverrideMods[i]->fMagnitude;

			switch (pchar->modArray.ppOverrideMods[i]->pDef->offAspect)
			{
				case kAttribAspect_ResTrue:
					resAspects.fResTrue = fMag;
					break;
				case kAttribAspect_ResBase:
					resAspects.fResBase = fMag;
					break;
				case kAttribAspect_ResFactPos:
					resAspects.fResFactPos = fMag;
					break;
				case kAttribAspect_ResFactNeg:
					resAspects.fResFactNeg = fMag;
					break;
				case kAttribAspect_ResFactBonus:
					resAspects.fResFactBonus = fMag;
					break;
				case kAttribAspect_Immunity:
					fImmune = fMag;
					break;
					
			}
		}
	}

	// if we are not yet immune, see if we have a powerActivation immunity that applies to this mod
	if (fImmune <= 0 && g_CombatConfig.pPowerActivationImmunities && pchar->bPowerActivationImmunity)
	{	
		if (mod_AffectsModOrPower(	iPartitionIdx, 
									g_CombatConfig.pPowerActivationImmunities->pExprAffectsMod, 
									pchar, 
									pmoddef, 
									pmod, 
									ppowdef))
		{
			fImmune = 1.f;
		}
	}

	// Clamping ResFactPos to 0 here to allow for negative modifiers to
	// ResFactPos for armor penetration style mechanics.
	if(g_CombatConfig.bClampResFactPosToZero && resAspects.fResFactPos < 0.0f)
		resAspects.fResFactPos = 0.0f;
	
	// We have accrued our absolute, positive and negative resistance factors.  At 
	//  this point we can calculate the effective resistance
	fResFinal = resAspects.fResBase * (1.0f + resAspects.fResFactPos) / (1.0f + resAspects.fResFactNeg);

	fResFinal *= 1.0f + resAspects.fResFactBonus;
	
	// Now we do the final adjustment based on the sensitivity
	fResFinal = mod_SensitivityAdjustment(fResFinal, fResSensitivity);

	resAspects.fResTrue = CLAMP(resAspects.fResTrue, g_CombatConfig.fAspectResTrueMin, g_CombatConfig.fAspectResTrueMax);
	resAspects.fResTrue = 1.f - resAspects.fResTrue;
	if (pfResTrueOut)
		*pfResTrueOut = resAspects.fResTrue;


	// Final check to make sure this resistance value is acceptable
	if(fResFinal <= 0.0f)
	{
		ErrorDetailsf("Character %s, Attrib %s, Final %f, True %f, Base %f, FactPos %f, FactNeg, %f, FactBonus %f",
						CHARDEBUGNAME(pchar), StaticDefineIntRevLookup(AttribTypeEnum,offAttrib), fResFinal,
						resAspects.fResTrue, resAspects.fResBase, resAspects.fResFactPos, 
						resAspects.fResFactNeg, resAspects.fResFactBonus);

		devassertmsg(0,"Resistance is at or below 0!");
		fResFinal = 1.0f;
	}
	
	// Set immunity
	if(pfImmuneOut) *pfImmuneOut = fImmune;

	// Clean up
	eaDestroy(&ppMods);

	PERFINFO_AUTO_STOP();

	return fResFinal;
}

// Returns the resistance to an AttribMod, given the Character resisting it
F32 mod_GetResist(int iPartitionIdx,
				  AttribMod *pmod,
				  AttribModDef *pmoddef,
				  Character *pchar,
				  F32 *pfResTrueOut,
				  F32 *pfImmuneOut,
				  CombatTrackerFlag *peFlagsOut)
{
	return CharacterGetResistEx(iPartitionIdx, pchar, pmod, pmoddef, 
								(pmoddef->offAttrib == kAttribType_DynamicAttrib ? pmod->pParams->eDynamicCachedType : pmoddef->offAttrib),
								pfResTrueOut, pfImmuneOut, peFlagsOut);
}

// Returns the generic resistance to an Attrib, given the Character resisting it.
F32 character_GetResistGeneric(int iPartitionIdx,
							   Character *pchar,
							   AttribType offAttrib,
							   F32 *pfResTrueOut,
							   F32 *pfImmuneOut)
{
	return CharacterGetResistEx(iPartitionIdx,pchar,NULL,NULL,offAttrib,pfResTrueOut,pfImmuneOut,NULL);
}



__forceinline static void character_compileStrength(AttribModDef *pmoddef, 
													F32 fMagnitude,
													StrengthAspectSet *pStrAspects, 
													StrengthAspectSet *pStrAspectsNoCurve,
													CombatTrackerFlag *peFlagsOut)
{
	F32 fMag, fMagNoCurve;

	if(peFlagsOut)
		*(peFlagsOut) |= pmoddef->eFlags;

	// ignoring curves for StrMult and StrAdd
	if (pmoddef->offAspect != kAttribAspect_StrMult && pmoddef->offAspect != kAttribAspect_StrAdd)
	{
		F32 fCurve = moddef_GetSensitivity(pmoddef,kSensitivityType_AttribCurve);

		fMag = fMagnitude * fCurve;
		fMagNoCurve = fMagnitude - fMag;
	}
	else
	{
		fMag = fMagnitude;
		fMagNoCurve = 0.f;
	}
		
		
	switch(pmoddef->offAspect)
	{
		xcase kAttribAspect_StrBase:
			pStrAspects->fStrBase += fMag;
			pStrAspectsNoCurve->fStrBase += fMagNoCurve;

		xcase kAttribAspect_StrFactPos:
			pStrAspects->fStrFactPos += fMag;
			pStrAspectsNoCurve->fStrFactPos += fMagNoCurve;

		xcase kAttribAspect_StrFactNeg:
			pStrAspects->fStrFactNeg += fMag;
			pStrAspectsNoCurve->fStrFactNeg += fMagNoCurve;

		xcase kAttribAspect_StrFactBonus:
			pStrAspects->fStrFactBonus += fMag;
			pStrAspectsNoCurve->fStrFactBonus += fMagNoCurve;

		xcase kAttribAspect_StrMult:
			pStrAspects->fStrMult *= fMag;

		xcase kAttribAspect_StrAdd:
			pStrAspects->fStrAdd += fMag;

	}
}


// Returns the Character's strength.  If passed a CharacterAttribs array, the function will do
//  a lookup into that, instead of basing strength off the character and enhancements.
// TODO(JW): Optimize the hell out of this
static F32 CharacterGetStrengthEx(int iPartitionIdx,
								  SA_PARAM_OP_VALID Character *pchar,
								  SA_PARAM_OP_VALID AttribModDef *pdef,
								  AttribType offAttrib,
								  SA_PARAM_OP_VALID PowerDef *ppowdefActing,
								  S32 iLevelMain,
								  S32 iLevelInline,
								  F32 fTableScaleInline,
								  SA_PARAM_OP_VALID Power **ppEnhancements,
								  EntityRef erTarget,
								  SA_PARAM_OP_VALID CharacterAttribs *pattrStrOverride,
								  S32 bExcludeRequires,
								  SA_PARAM_OP_VALID CombatTrackerFlag *peFlagsOut,
								  SA_PARAM_OP_VALID F32 *pfStrAddOut)
{
	int i;
	AttribMod **ppMods = NULL;
	StrengthAspectSet	strAspects = {0};
	StrengthAspectSet	strAspectsNoCurve = {0};

	F32 fStrFinal = 1.0f;
	F32 fStrSensitivity = pdef ? pdef->fSensitivityStrength : 1.f;
	PowerDef *ppowdef = ppowdefActing ? ppowdefActing : (pdef ? pdef->pPowerDef : NULL);
	CharacterClass *pClass = pchar ? character_GetClassCurrent(pchar) : NULL;

	PowerDef *ppowdefState = NULL; // PowerDef for immediate State-specific effects

	// set the defaults
	strAspects.fStrBase = 1.f;
	strAspects.fStrMult = 1.f;

	// If the mod isn't strength-able, or it doesn't scale, return 1.0 (which means normal strength)
	if(fStrSensitivity <= 0.0f || (pdef && pdef->eType==kModType_None))
		return 1.0f;

	// If it's a normal attrib and we received an overriding strength set, lookup into that
	if(IS_NORMAL_ATTRIB(offAttrib) && pattrStrOverride)
	{
		fStrFinal = *F32PTR_OF_ATTRIB(pattrStrOverride, offAttrib);
	}
	else // Otherwise do a (mostly) character-based determination
	{
		if(pchar)
		{
			// Find all mods that can mod the strength of this:
			//  That means all mods on the character that are of the same
			//  attribute, affect strength, aren't ignored, pass the Personal check and have the proper tags
			for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
			{
				AttribModDef *pmoddefChar = mod_GetDef(pchar->modArray.ppMods[i]);
				if(pmoddefChar
					&& (pmoddefChar->offAttrib==offAttrib
						|| pmoddefChar->offAttrib==kAttribType_All)
					&& IS_STRENGTH_ASPECT(pmoddefChar->offAspect)
					&& !pchar->modArray.ppMods[i]->bIgnored
					&& (!pmoddefChar->bPersonal || (erTarget && pchar->modArray.ppMods[i]->erPersonal==erTarget))
					&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmoddefChar,pchar,pchar->modArray.ppMods[i],pdef,NULL,ppowdef))
				{
					eaPush(&ppMods,pchar->modArray.ppMods[i]);
				}
			}

			// If this is a regular attribute, get class attributes and the character's innate accrual.
			//  These give us our initial operating values.
			if(IS_NORMAL_ATTRIB(offAttrib))
			{
				AttribAccrualSet *pInnateAccrualSet = character_GetInnateAccrual(iPartitionIdx,pchar,NULL);
				strAspects.fStrBase = character_GetClassAttrib(pchar,kClassAttribAspect_Str,offAttrib);

				if(pInnateAccrualSet)
				{
					F32 fStrMultTemp;

					strAspects.fStrBase += *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrStrBase) + offAttrib);
					strAspects.fStrFactPos = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrStrFactPos) + offAttrib);
					strAspects.fStrFactNeg = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrStrFactNeg) + offAttrib);
					strAspects.fStrFactBonus = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrStrFactBonus) + offAttrib);
					fStrMultTemp = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrStrMult) + offAttrib);
					strAspects.fStrAdd = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrStrAdd) + offAttrib);
					
					// only use the innate StrMult if it is non-zero
					if (fStrMultTemp > 0)
						strAspects.fStrMult = fStrMultTemp;
				}
			}

			// Filter the list for ModStackGroups
			mods_StackGroupFilter(ppMods);

			// Single pass, accrue all strength mods
			for(i=eaSize(&ppMods)-1; i>=0; i--)
			{
				AttribModDef *pmoddefChar = mod_GetDef(ppMods[i]);

				// These mods could be resistible, so we have to get their effective magnitude
				F32 fMagnitude = ModGetEffectiveMagnitude(iPartitionIdx,ppMods[i],pmoddefChar,pchar);

				character_compileStrength(pmoddefChar, fMagnitude, &strAspects, &strAspectsNoCurve, peFlagsOut);
			}
			
#ifdef GAMESERVER
			if (g_CombatConfig.pCombatAdvantage && offAttrib != kAttribType_CombatAdvantage)
			{
				F32 fCombatAdvantageStr = gslCombatAdvantage_GetStrengthBonus(iPartitionIdx, pchar, erTarget, offAttrib);
				if (fCombatAdvantageStr > 0)
				{
					strAspects.fStrFactBonus += fCombatAdvantageStr;
					if (peFlagsOut)
						*(peFlagsOut) |= kCombatTrackerFlag_Flank;
				}
			}
#endif
		}

		// Now we have to accrue all the strength mods from enhancements
		//  TODO(JW): Enhancements: Precalculate if the enhancement improves strength, flag the power
		//  TODO(JW): Could/Should this be precalculated and stored on the power somehow?
		//  TODO(JW): Should the class be the character's class, or a generic class?
		if(ppEnhancements)
		{
			for(i=eaSize(&ppEnhancements)-1; i>=0; i--)
			{
				int j, iLevelEnh;
				F32 fTableScale;
				PowerDef *pDefEnh = ppEnhancements[i] ? GET_REF(ppEnhancements[i]->hDef) : NULL;
				
				if(!pDefEnh)
					continue;

				// RMARR - This feature has been disabled, because:
				// * We are not currently using it as it was originally intended
				// * The way I implemented it does not distinguish between the item for the power apply, and the item for the enhancement, and
				//    this code was stomping it, so this needs to be done better.
				// I'm going to evaluate some expressions in the apply context for this enhancement, so I'm going to load the source item
				// into the power application now
				//combateval_ContextSetEnhancementSourceItem(ppEnhancements[i]->pSourceItem);

				
				iLevelEnh = pDefEnh->bEnhanceCopyLevel ? iLevelMain : POWERLEVEL(ppEnhancements[i], MAX(SAFE_MEMBER(pchar,iLevelCombat),1));
				fTableScale = ppEnhancements[i]->fTableScale;

				for(j=eaSize(&pDefEnh->ppOrderedMods)-1; j>=0; j--)
				{
					AttribModDef *pmoddefEnh = pDefEnh->ppOrderedMods[j];
					if((pmoddefEnh->offAttrib==offAttrib
						|| pmoddefEnh->offAttrib==kAttribType_All)
						&& !pmoddefEnh->bEnhancementExtension
						&& IS_STRENGTH_ASPECT(pmoddefEnh->offAspect)
						&& (!pmoddefEnh->pExprRequires
							|| (!bExcludeRequires
								&& (g_CombatConfig.bIgnoreStrEnhanceRequiresCheck
									|| 0!=combateval_EvalNew(iPartitionIdx,pmoddefEnh->pExprRequires,kCombatEvalContext_Apply,NULL))))
						&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmoddefEnh,pchar,NULL,pdef,NULL,pdef->pPowerDef))
					{
						F32 fMagnitude = moddef_GetMagnitude(iPartitionIdx,pmoddefEnh,pClass,iLevelEnh,fTableScale,true);

						character_compileStrength(pmoddefEnh, fMagnitude, &strAspects, &strAspectsNoCurve, peFlagsOut);
					}
				}

				// clear this
				//combateval_ContextSetEnhancementSourceItem(NULL);
			}
		}

		// Inline Enhancement check.  Same code and TODOs as above.
		//  TODO(JW): Enhancements: Precalculate if the enhancement improves strength, flag the power
		//  TODO(JW): Could/Should this be precalculated and stored on the power somehow?
		//  TODO(JW): Should the class be the character's class, or a generic class?
		if(ppowdef && ppowdef->bEnhancementExtension && ppowdef->eType!=kPowerType_Enhancement)
		{
			int j;
			for(j=eaSize(&ppowdef->ppOrderedMods)-1; j>=0; j--)
			{
				AttribModDef *pmoddefEnh = ppowdef->ppOrderedMods[j];
				if(pmoddefEnh->bEnhancementExtension
					&& (pmoddefEnh->offAttrib==offAttrib
						|| pmoddefEnh->offAttrib==kAttribType_All)
					&& IS_STRENGTH_ASPECT(pmoddefEnh->offAspect)
					&& (!pmoddefEnh->pExprRequires
						|| (!bExcludeRequires
							&& (g_CombatConfig.bIgnoreStrEnhanceRequiresCheck
								|| 0!=combateval_EvalNew(iPartitionIdx,pmoddefEnh->pExprRequires,kCombatEvalContext_Apply,NULL))))
					&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmoddefEnh,pchar,NULL,pdef,NULL,pdef->pPowerDef))
				{
					F32 fMagnitude = moddef_GetMagnitude(iPartitionIdx,pmoddefEnh,pClass,iLevelInline,fTableScaleInline,true);

					character_compileStrength(pmoddefEnh, fMagnitude, &strAspects, &strAspectsNoCurve, peFlagsOut);
				}
			}
		}

		if(pchar)
		{
			PERFINFO_AUTO_START("Apply AttribCurves",1);
			// Apply AttribCurves
			{
				if(pClass)
				{
					AttribCurve **ppCurves = class_GetAttribCurveArray(pClass,offAttrib);
					int s = eaSize(&ppCurves);
					if(s && verify(ATTRIBASPECT_INDEX(kAttribAspect_StrFactBonus)<s))
					{
						i = ATTRIBASPECT_INDEX(kAttribAspect_StrBase);
						if(ppCurves[i])
						{
							strAspects.fStrBase = character_ApplyAttribCurve(pchar,ppCurves[i],strAspects.fStrBase);
						}
						i = ATTRIBASPECT_INDEX(kAttribAspect_StrFactPos);
						if(ppCurves[i])
						{
							strAspects.fStrFactPos = character_ApplyAttribCurve(pchar,ppCurves[i],strAspects.fStrFactPos);
						}
						i = ATTRIBASPECT_INDEX(kAttribAspect_StrFactNeg);
						if(ppCurves[i])
						{
							strAspects.fStrFactNeg = character_ApplyAttribCurve(pchar,ppCurves[i],strAspects.fStrFactNeg);
						}
						i = ATTRIBASPECT_INDEX(kAttribAspect_StrFactBonus);
						if(ppCurves[i])
						{
							strAspects.fStrFactBonus = character_ApplyAttribCurve(pchar,ppCurves[i],strAspects.fStrFactBonus);
						}
                        // don't apply curves to strMult and StrAdd
					}
				}
			}
			
			strAspects.fStrBase += strAspectsNoCurve.fStrBase;
			strAspects.fStrFactPos += strAspectsNoCurve.fStrFactPos;
			strAspects.fStrFactNeg += strAspectsNoCurve.fStrFactNeg;
			strAspects.fStrFactBonus += strAspectsNoCurve.fStrFactBonus;
			PERFINFO_AUTO_STOP();

			// Now we check if there were any override attrib mods applied to the player
			// causing any of these values to be overridden to a giving value
			//TODO (MM): This could use Optimization 
			for(i=0;i<eaSize(&pchar->modArray.ppOverrideMods);i++)
			{
				if(pchar->modArray.ppOverrideMods[i]->pDef
					&& (((AttribOverrideParams *)pchar->modArray.ppOverrideMods[i]->pDef->pParams)->offAttrib==offAttrib
					|| ((AttribOverrideParams *)pchar->modArray.ppOverrideMods[i]->pDef->pParams)->offAttrib==kAttribType_All)
					&& IS_STRENGTH_ASPECT(pchar->modArray.ppMods[i]->pDef->offAspect)
					&& moddef_AffectsModOrPowerChk(iPartitionIdx,pchar->modArray.ppOverrideMods[i]->pDef,pchar,pchar->modArray.ppOverrideMods[i],pdef,NULL,ppowdef))
				{
					F32 fMag = pchar->modArray.ppOverrideMods[i]->fMagnitude;

					if(peFlagsOut)
						*(peFlagsOut) |= pchar->modArray.ppOverrideMods[i]->eFlags;

					switch (pchar->modArray.ppOverrideMods[i]->pDef->offAspect)
					{
						case kAttribAspect_StrBase:
							strAspects.fStrBase = fMag;
							break;
						case kAttribAspect_StrFactPos:
							strAspects.fStrFactPos = fMag;
							break;
						case kAttribAspect_StrFactNeg:
							strAspects.fStrFactNeg = fMag;
							break;
						case kAttribAspect_StrFactBonus:
							strAspects.fStrFactBonus = fMag;
							break;
						case kAttribAspect_StrMult:
							strAspects.fStrMult = fMag;
							break;
						case kAttribAspect_StrAdd:
							strAspects.fStrAdd = fMag;
							break;
					}
				}
			}
		}

		// We have accrued our absolute, positive and negative strength factors.  At 
		//  this point we can calculate the effective strength
		fStrFinal = strAspects.fStrBase * (1.0f + strAspects.fStrFactPos) / (1.0f + strAspects.fStrFactNeg);
		fStrFinal *= 1.0f + strAspects.fStrFactBonus;
		fStrFinal *= strAspects.fStrMult;

		if (pfStrAddOut)
			*pfStrAddOut = strAspects.fStrAdd;
	}

	// Now we do the final adjustment based on the sensitivity
	fStrFinal = mod_SensitivityAdjustment(fStrFinal, fStrSensitivity);

	// Clean up
	eaDestroy(&ppMods);

	return fStrFinal;
}

// Returns the strength for an AttribModDef, given the Character and optional power/enhancements/target involved in
//  creating it.  If passed a CharacterAttribs array, the function will do a lookup into that, instead
//  of basing strength off the Character and power/enhancements/target.
F32 moddef_GetStrength(int iPartitionIdx,
					   AttribModDef *pdef,
					   Character *pchar,
					   PowerDef *ppowdefActing,
					   S32 iLevelMain,
					   S32 iLevelInline,
					   F32 fTableScaleInline,
					   Power **ppEnhancements,
					   EntityRef erTarget,
					   CharacterAttribs *pattrStrOverride,
					   S32 bExcludeRequires,
					   CombatTrackerFlag *peFlagsOut,
					   F32 *pfStrAddOut)
{
	return CharacterGetStrengthEx(	iPartitionIdx, pchar, pdef, pdef->offAttrib, ppowdefActing,
									iLevelMain, iLevelInline, fTableScaleInline, ppEnhancements,
									erTarget, pattrStrOverride, bExcludeRequires, peFlagsOut, pfStrAddOut);
}

// Returns the generic strength of an Attrib, given the Character creating it.
F32 character_GetStrengthGeneric(int iPartitionIdx,
								 Character *pchar,
								 AttribType offAttrib,
								 F32 *pfStrAddOut)
{
	return CharacterGetStrengthEx(iPartitionIdx,pchar,NULL,offAttrib,NULL,0,0,0,NULL,0,NULL,false,NULL,pfStrAddOut);
}


// Returns the basic value of an attribute with respect to the Power and Character.
//  Automatically includes relevant Enhancements.
F32 character_PowerBasicAttrib(int iPartitionIdx,
							   Character *pchar,
							   Power *ppow,
							   AttribType offAttrib,
							   EntityRef erTarget)
{
	static Power **s_ppEnhancements = NULL;
	F32 f = 0;
	if (ppow)
		power_GetEnhancements(iPartitionIdx,pchar,ppow,&s_ppEnhancements);

	f = character_PowerBasicAttribEx(iPartitionIdx,pchar,ppow,NULL,offAttrib,s_ppEnhancements,erTarget);
	eaClearFast(&s_ppEnhancements);
	return f;
}

// Custom call for character_PowerBasicAttrib() on kAttribType_Disable, which has an early exit
//  if there are no Basic Disable mods.
F32 character_PowerBasicDisable(int iPartitionIdx,
								Character *pchar,
								Power *ppow)
{
	if(!pchar->modArray.bHasBasicDisableAffects)
		return pchar->pattrBasic->fDisable;
	else
		return character_PowerBasicAttrib(iPartitionIdx,pchar,ppow,kAttribType_Disable,0);
}

// Returns the basic value of an attribute with respect to the Power and Character.
//  Uses specific list of Enhancements.
F32 character_PowerBasicAttribEx(int iPartitionIdx,
								 Character *pchar,
								 Power *ppow,
								 PowerDef *pdef, 
								 AttribType offAttrib,
								 Power **ppEnhancements,
								 EntityRef erTarget)
{
	int i;
	AttribMod **ppMods = NULL;
	AttribMod **ppModsOverride = NULL;	
	F32 fBasicBase = 1.0f, fBasicAbs = 0, fBasicPos = 0, fBasicNeg = 0;
	F32 fBasicFinal = 1.0f;
	CharacterClass *pClass = character_GetClassCurrent(pchar);
	S32 bFakeMods = false;

	if (!pdef && ppow)
	{
		pdef = GET_REF(ppow->hDef);
	}

	PERFINFO_AUTO_START_FUNC();

	// If this is a regular attribute, get class attributes and the character's innate accrual.
	//  These give us our initial operating values.
	if(IS_NORMAL_ATTRIB(offAttrib))
	{
		AttribAccrualSet *pInnateAccrualSet = character_GetInnateAccrual(iPartitionIdx,pchar,NULL);
		fBasicBase = character_GetClassAttrib(pchar,kClassAttribAspect_Basic,offAttrib);

		if(pInnateAccrualSet)
		{
			fBasicAbs = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrBasicAbs) + offAttrib);
			fBasicPos = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrBasicFactPos) + offAttrib);
			fBasicNeg = *(F32*)((char*)(&pInnateAccrualSet->CharacterAttribs.attrBasicFactNeg) + offAttrib);
		}
	}

#ifdef GAMECLIENT
	// Make fake mods if this doesn't appear to be the active Player
	if(!eaSize(&pchar->modArray.ppMods) && eaSize(&pchar->ppModsNet) && pchar!=characterActivePlayerPtr())
		bFakeMods = character_ModsNetCreateFakeMods(pchar);
#endif

	// Find all mods that can mod the basic value of this:
	//  That means all mods on the character that are active, of the same
	//  attribute, affect a basic value, arent't ignored and have the proper tags
	// Also find all the matching AttribOverride mods, which we do at the same
	//  time since the client doesn't keep that earray
	for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
	{
		AttribModDef *pmoddefChar = mod_GetDef(pchar->modArray.ppMods[i]);
		if(pmoddefChar
			&& pmoddefChar->offAttrib==offAttrib 
			&& IS_BASIC_ASPECT(pmoddefChar->offAspect)
			&& !pchar->modArray.ppMods[i]->bIgnored
			&& (!pmoddefChar->bPersonal || pchar->modArray.ppMods[i]->erPersonal==erTarget)
			&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmoddefChar,pchar,pchar->modArray.ppMods[i],NULL,NULL,pdef))
		{
			eaPush(&ppMods,pchar->modArray.ppMods[i]);
		}
		else if(pmoddefChar
			&& pmoddefChar->offAttrib==kAttribType_AttribOverride
			&& ((AttribOverrideParams *)pmoddefChar->pParams)->offAttrib==offAttrib
			&& IS_BASIC_ASPECT(pmoddefChar->offAspect)
			&& !pchar->modArray.ppMods[i]->bIgnored
			&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmoddefChar,pchar,pchar->modArray.ppMods[i],NULL,NULL,pdef))
		{
			eaPush(&ppModsOverride,pchar->modArray.ppMods[i]);
		}
	}

	// Filter the list for ModStackGroups
	mods_StackGroupFilter(ppMods);

	// Single pass, accrue all basic value mods
	for(i=eaSize(&ppMods)-1; i>=0; i--)
	{
		AttribModDef *pmoddefChar = mod_GetDef(ppMods[i]);
		// These mods could be resistible, so we have to get their effective magnitude
		F32 fMagnitude = ModGetEffectiveMagnitude(iPartitionIdx,ppMods[i],pmoddefChar,pchar);

		switch(pmoddefChar->offAspect)
		{
		case kAttribAspect_BasicAbs:
			fBasicAbs += fMagnitude;
			break;
		case kAttribAspect_BasicFactPos:
			fBasicPos += fMagnitude;
			break;
		case kAttribAspect_BasicFactNeg:
			fBasicNeg += fMagnitude;
			break;
		}
	}

	// Override all basic values
	for(i=eaSize(&ppModsOverride)-1; i>=0; i--)
	{
		AttribModDef *pmoddefChar = mod_GetDef(ppModsOverride[i]);
		// These mods could be resistible, so we have to get their effective magnitude
		F32 fMagnitude = ModGetEffectiveMagnitude(iPartitionIdx,ppModsOverride[i],pmoddefChar,pchar);

		switch(pmoddefChar->offAspect)
		{
		case kAttribAspect_BasicAbs:
			fBasicAbs = fMagnitude;
			break;
		case kAttribAspect_BasicFactPos:
			fBasicPos = fMagnitude;
			break;
		case kAttribAspect_BasicFactNeg:
			fBasicNeg = fMagnitude;
			break;
		}
	}

	// Now we have to accrue all the basic mods from enhancements
	//  TODO(JW): Enhancements: Precalculate if the enhancement improves basic values, flag the power
	//  TODO(JW): Could/Should this be precalculated and stored on the power somehow?
	//  TODO(JW): Should the class be the character's class, or a generic class?
	i = eaSize(&ppEnhancements);
	if(i)
	{
		for(i-=1; i>=0; i--)
		{
			int j;
			PowerDef *pDefEnh = GET_REF(ppEnhancements[i]->hDef);
			if(pDefEnh) 
			{
				F32 fTableScale = ppEnhancements[i]->fTableScale;
				S32 iCombatLevel = MAX(pchar->iLevelCombat, 1);
				S32 iLevelEnh = pDefEnh->bEnhanceCopyLevel ? POWERLEVEL(ppow, iCombatLevel) : POWERLEVEL(ppEnhancements[i], iCombatLevel);

				combateval_ContextSetupSimple(pchar, iLevelEnh, ppEnhancements[i]->pSourceItem);

				for(j=eaSize(&pDefEnh->ppOrderedMods)-1; j>=0; j--)
				{
					if(pDefEnh->ppOrderedMods[j]->offAttrib==offAttrib
						&& !pDefEnh->ppOrderedMods[j]->bEnhancementExtension
						&& IS_BASIC_ASPECT(pDefEnh->ppOrderedMods[j]->offAspect)
						&& moddef_AffectsModOrPowerChk(iPartitionIdx,pDefEnh->ppOrderedMods[j],pchar,NULL,NULL,NULL,pdef))
					{
						F32 fMagnitude = moddef_GetMagnitude(iPartitionIdx,pDefEnh->ppOrderedMods[j],pClass,iLevelEnh,fTableScale,false);

						switch(pDefEnh->ppOrderedMods[j]->offAspect)
						{
						case kAttribAspect_BasicAbs:
							fBasicAbs += fMagnitude;
							break;
						case kAttribAspect_BasicFactPos:
							fBasicPos += fMagnitude;
							break;
						case kAttribAspect_BasicFactNeg:
							fBasicNeg += fMagnitude;
							break;
						}
					}
				}
			}
		}
	}

	// Inline Enhancement check
	if(ppow && pdef && pdef->bEnhancementExtension && pdef->eType!=kPowerType_Enhancement)
	{
		int j;
		F32 fTableScale = ppow->fTableScale;
		S32 iLevel = POWERLEVEL(ppow, MAX(pchar->iLevelCombat,1));

		combateval_ContextSetupSimple(pchar, iLevel, ppow->pSourceItem);

		for(j=eaSize(&pdef->ppOrderedMods)-1; j>=0; j--)
		{
			if(pdef->ppOrderedMods[j]->offAttrib==offAttrib
				&& pdef->ppOrderedMods[j]->bEnhancementExtension
				&& IS_BASIC_ASPECT(pdef->ppOrderedMods[j]->offAspect)
				&& moddef_AffectsModOrPowerChk(iPartitionIdx,pdef->ppOrderedMods[j],pchar,NULL,NULL,NULL,pdef))
			{
				F32 fMagnitude = moddef_GetMagnitude(iPartitionIdx,pdef->ppOrderedMods[j],pClass,iLevel,fTableScale,false);

				switch(pdef->ppOrderedMods[j]->offAspect)
				{
				case kAttribAspect_BasicAbs:
					fBasicAbs += fMagnitude;
					break;
				case kAttribAspect_BasicFactPos:
					fBasicPos += fMagnitude;
					break;
				case kAttribAspect_BasicFactNeg:
					fBasicNeg += fMagnitude;
					break;
				}
			}
		}
	}

	PERFINFO_AUTO_START("Apply AttribCurves",1);
	// Apply AttribCurves
	if(pClass)
	{
		AttribCurve **ppCurves = class_GetAttribCurveArray(pClass,offAttrib);
		int s = eaSize(&ppCurves);
		if(s && verify(ATTRIBASPECT_INDEX(kAttribAspect_BasicFactNeg)<s))
		{
			i = ATTRIBASPECT_INDEX(kAttribAspect_BasicAbs);
			if(ppCurves[i])
			{
				fBasicAbs = character_ApplyAttribCurve(pchar,ppCurves[i],fBasicAbs);
			}
			i = ATTRIBASPECT_INDEX(kAttribAspect_BasicFactPos);
			if(ppCurves[i])
			{
				fBasicPos = character_ApplyAttribCurve(pchar,ppCurves[i],fBasicPos);
			}
			i = ATTRIBASPECT_INDEX(kAttribAspect_BasicFactNeg);
			if(ppCurves[i])
			{
				fBasicNeg = character_ApplyAttribCurve(pchar,ppCurves[i],fBasicNeg);
			}
		}
	}
	PERFINFO_AUTO_STOP();

	// We have accrued our absolute, positive and negative factors.  At 
	//  this point we can calculate the effective basic value
	fBasicFinal = fBasicAbs + fBasicBase * (1.0f + fBasicPos) / (1.0f + fBasicNeg);

	// Clean up
	eaDestroy(&ppMods);
	eaDestroy(&ppModsOverride);
#ifdef GAMECLIENT
	if(bFakeMods)
		eaDestroyStruct(&pchar->modArray.ppMods,parse_AttribMod);
#endif

	PERFINFO_AUTO_STOP();

	return fBasicFinal;
}


// Resets a Character's AttribPools to their initial values
void character_AttribPoolsReset(Character *pchar, bool bResetNonPersistedOnly)
{
	if(g_iAttribPoolCount)
	{
		int i;

		for(i=0; i<g_iAttribPoolCount; i++)
		{
			AttribPool *ppool = g_AttribPools.ppPools[i];

			if (!bResetNonPersistedOnly || !ppool->bPersist)
			{
				F32 *pfCur = F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribCur);
				F32 fMin = ppool->eAttribMin ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribMin) : 0;
				F32 fMax = ppool->eAttribMax ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribMax) : 0;
				F32 fTarget = ppool->eAttribTarget ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribTarget) : 0;

				*pfCur = combatpool_Init(&ppool->combatPool,fMin,fMax,fTarget);
			}
		}
	}
}

// Clamps a Character's AttribPools to the current minimum and maximum
void character_AttribPoolsClamp(Character *pchar)
{
	if(g_iAttribPoolCount)
	{
		int i;

		PERFINFO_AUTO_START_FUNC();

		for(i=0; i<g_iAttribPoolCount; i++)
		{
			AttribPool *ppool = g_AttribPools.ppPools[i];
			F32 *pfCur = F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribCur);
			F32 *pfTarget = ppool->eAttribTarget ? F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribTarget): NULL;
			F32 fMin = ppool->eAttribMin ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribMin) : 0;
			F32 fMax = ppool->eAttribMax ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribMax) : 0;
			*pfCur = CLAMP(*pfCur,fMin,fMax);
			if(pfTarget)
				*pfTarget = CLAMP(*pfTarget,fMin,fMax);
		}

		PERFINFO_AUTO_STOP();
	}
}

// Sets a Character's AttribPools to their minimum values
void character_AttribPoolsEmpty(Character *pchar)
{
	if(g_iAttribPoolCount)
	{
		int i;

		for(i=0; i<g_iAttribPoolCount; i++)
		{
			AttribPool *ppool = g_AttribPools.ppPools[i];
			F32 *pfCur = F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribCur);
			F32 fMin = ppool->eAttribMin ? *F32PTR_OF_ATTRIB(pchar->pattrBasic,ppool->eAttribMin) : 0;
			
			if(ppool->bDoNotEmpty)
				continue;

			*pfCur = fMin;
		}
	}
}


// Returns a bunch of temporarily functional Powers based on the list of PowerClones
static Power **GetPowersFromPowerClones(PowerClone ***pppEnhancements)
{
	Power **ppPowers = NULL;
	int i;
	for(i=eaSize(pppEnhancements)-1; i>=0; i--)
	{
		PowerClone *pEnh = (*pppEnhancements)[i];
		Power *ppow = power_Create(REF_STRING_FROM_HANDLE(pEnh->hdef));
		CONTAINER_NOCONST(Power, ppow)->iLevel = pEnh->iLevel;
		ppow->fTableScale = pEnh->fTableScale;
		eaPush(&ppPowers,ppow);
	}
	return ppPowers;
}


// Checks to see if the character shields work against the damage dealt, returns the portion not shielded
F32 character_ProcessShields(int iPartitionIdx, Character *pchar, AttribType offAttrib, 
								F32 fMag, F32 fMagNoResist, AttribMod *pmodDamage, 
								S32 *pbCountAsDamaged, bool bPeekOnly, CombatTrackerFlag *pFlagsAddedOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	F32 fRemaining = 1.f;
	F32 fSensitivity;

	if(pmodDamage->pDef->pPowerDef && powerdef_IgnoresAttrib(pmodDamage->pDef->pPowerDef,kAttribType_Shield))
	{
		fSensitivity = 0.f;
	}
	else
	{
		fSensitivity = moddef_GetSensitivity(pmodDamage->pDef,kSensitivityType_Shield);
	}
	
	if(fSensitivity > 0)
	{
		F32 fMagResult = fMag * fSensitivity;
		F32 fScaleNoResist = fMagNoResist/fMag;
		int s = eaSize(&pchar->ppModsShield);
		if(s)
		{
			int i;
			for(i=0; i<s && fMagResult > 0; i++)
			{
				AttribMod *pmod = pchar->ppModsShield[i];
				AttribModDef *pdef = pmod->pDef;
				ShieldParams *pparams = (ShieldParams*)pdef->pParams;
				if(pmod->pFragility
					&& pmod->pFragility->fHealth > 0
					&& pparams
					&& attrib_Matches(offAttrib,pparams->offAttrib)
					&& (!pdef->bAffectsOnlyOnFirstModTick || !pmodDamage->bPostFirstTickApply)
					&& (!pparams->uiCharges
						|| (pmod->pParams && pmod->pParams->vecParam[2] < pparams->uiCharges))
					&& moddef_AffectsModFromDirectionChk(pdef,pchar,pmodDamage)
					&& moddef_AffectsModOrPowerChk(iPartitionIdx,pdef,pchar,pmod,pmodDamage->pDef,pmodDamage,pmodDamage->pDef->pPowerDef))
				{
					// This works a lot like the general fragility code, but
					//  because shields are a little special, it's all custom.
					F32 fScaleSet;
					F32 fMaxAbsorb = pmod->pParams ? pmod->pParams->vecParam[0] : 0.0f;
					F32 fPercentIgnored = pmod->pParams ? pmod->pParams->vecParam[1] : pparams->fPercentIgnored;
					F32 fDamage = 0, fAbsorbed = 0;
					F32 fScale = pdef->pFragility->bUseResistIn ? 1.f : fScaleNoResist;
					F32 fMagPossible = fMagResult * (1.f - fPercentIgnored);
					if(fMaxAbsorb)
					{
						//If we want the max absorption to be affected by proportion 
						if( pparams->bScaleMaxAbsorbedByProportion && pdef->pFragility->fProportion )
						{
							fMaxAbsorb -= (pmod->pFragility->fHealthMax-pmod->pFragility->fHealth) * pdef->pFragility->fProportion * fMaxAbsorb / pmod->pFragility->fHealthMax;
							MAX1(fMaxAbsorb, 0);
						}
						MIN1(fMagPossible, fMaxAbsorb);
					}

					// Scale the damage by the custom scale, if there is one
					fScaleSet = character_FragileModScale(iPartitionIdx,pchar,pmod,offAttrib,true);
					fScale *= fScaleSet;
					// If this is set negative, clamp it to immune
					if(fScale < 0)
						fScale = 0;
					
					// Find the most damage this could take, and take it
					fDamage = MIN(pmod->pFragility->fHealth,fMagPossible*fScale);
					pmod->pFragility->fHealth -= fDamage;
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

					// If this mod was immune to the damage, we absorbed all damage,
					//  otherwise we absorbed the appropriately scaled amount of damage
					fAbsorbed = fScale==0 ? fMagPossible : fDamage/fScale;
					
					// Remove what we absorbed from the result magnitude
					fMagResult -= fAbsorbed;

					if (fAbsorbed && pbCountAsDamaged)
					{
						(*pbCountAsDamaged) = pparams->bDamageTriggersTrackers;
					}

					if (pFlagsAddedOut)
						*pFlagsAddedOut |= pdef->eFlags;

					if (!bPeekOnly)
					{
						Entity *pOwnerEnt = entFromEntityRef(iPartitionIdx, pmodDamage->erOwner);
						EntityRef erOwner = pmodDamage->erOwner;

						if (pOwnerEnt && pOwnerEnt->erOwner)
						{
							erOwner = pOwnerEnt->erOwner;
						}

						// Add the shield absorb and damage to the combat tracker (as a negative event, so it matches the way heals show up)
						//  The magnitude is the damage the shield took, and the base magnitude is the damage the shield absorbed
						character_CombatTrackerAdd(pchar, pmodDamage->pDef->pPowerDef, erOwner, pmodDamage->erOwner, pmod->pDef->pPowerDef, kAttribType_Shield, -fDamage, -fAbsorbed, 0, 0, false);

						if(pparams->bDamageCredit)
						{
							// Add the Shield damage to the accumulated damage tracker
							character_DamageTrackerAccumEvent(iPartitionIdx,pchar,pmodDamage->erOwner,fDamage);
						}

#ifdef GAMESERVER
						// Notify the AI about damage to the Shield
						if(!g_CombatConfig.bShieldAggroDisable)
						{
							F32 fThreatScale = 1;
							Entity *eSource = entFromEntityRef(iPartitionIdx,pmodDamage->erSource);
							PERFINFO_AUTO_START("AINotify",1);
							if(eSource && eSource->pChar)
							{
								// TODO(JW): This is lazier than it should be (probably should be calculated at
								//  mod apply time), but we'll ignore that for now.
								fThreatScale = eSource->pChar->pattrBasic->fAIThreatScale;
							}
							aiFCNotify(pchar->pEntParent, pmodDamage, pmodDamage->pDef, fDamage, fThreatScale);
							PERFINFO_AUTO_STOP();
						}
#endif

						// Play custom HitFX
						if(pparams->pchHitFX)
						{
							char **ppchFXList = NULL;
							eaPush(&ppchFXList,(char*)pparams->pchHitFX);
							character_FlashFX(iPartitionIdx,pchar,pmod->uiApplyID,pmodDamage->uiApplyID,kPowerAnimFXType_ModUse,
											pchar,NULL,NULL,NULL,NULL,ppchFXList,NULL,pmod->fHue,pmTimestamp(0),0,0);										
							eaDestroy(&ppchFXList);
						}

						// Track hits
						if(pmod->pParams)
						{
							pmod->pParams->vecParam[2] += 1;
							if(pparams->uiCharges && pmod->pParams->vecParam[2] >= pparams->uiCharges)
							{
								character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
							}
						}
					}
				}
			}
		}

		fRemaining = (1.f - fSensitivity) + (fMagResult / fMag);
	}

	return fRemaining;
#endif
}

static void ModProcessAICommand(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
#ifdef GAMESERVER
		AttribModDef *pdef = pmod->pDef;
		if(pdef->pParams && ((AICommandParams*)pdef->pParams)->pExpr)
		{
			static AttribModParams *params = NULL;

			if(pmod->pParams && pmod->pParams->pCommandQueue)
			{
				// We already have a setup command queue, which means it's been run before
				if (pdef->fPeriod)
				{
					// If periodic, run the code from mod_cancel and reapply
					CommandQueue_ExecuteAllCommands(pmod->pParams->pCommandQueue);
					CommandQueue_Destroy(pmod->pParams->pCommandQueue);
					pmod->pParams->pCommandQueue = NULL;
					eaDestroy(&pmod->pParams->localData);
				}
				else
				{
					// If not periodic, just return. It'll get cleared up on mod_cancel
					PERFINFO_AUTO_STOP();
					return;
				}
			}
			
			if(!params) params = StructAlloc(parse_AttribModParams);

			aiPowersRunAIExpr(pchar->pEntParent,entFromEntityRef(iPartitionIdx,pmod->erOwner),
								entFromEntityRef(iPartitionIdx,pmod->erSource),((AICommandParams*)pdef->pParams)->pExpr, 
								&params->pCommandQueue, &params->localData);

			if(params->pCommandQueue)
			{
				pmod->pParams = params;

				params = StructAlloc(parse_AttribModParams);
			}
		}
#endif 
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessApplyObjectDeath(AttribMod *pmod, Character *pchar, int iPartitionIdx, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	{
		WorldInteractionNode *pnode;
		if(pmod->pParams && (pnode = GET_REF(pmod->pParams->hNodeParam)))
		{
			Entity *eSource = entFromEntityRef(iPartitionIdx, pmod->erSource);
			Character *pcharSource = eSource && eSource->pChar ? eSource->pChar : pchar; 
			CharacterClass *pclass = im_GetCharacterClass(pnode);
			S32 iLevel = im_GetLevel(pnode);
			if(pclass)
			{
				PowerDef **ppdefs = NULL;
				int i,s;
				if(0!=(s=im_GetDeathPowerDefs(pnode,&ppdefs)))
				{
					for(i=0; i<s; i++)
					{
						if(pchar)
						{
							ApplyUnownedPowerDefParams applyParams = {0};

							applyParams.erTarget = entGetRef(pchar->pEntParent);
							applyParams.pcharSourceTargetType = pcharSource;
							applyParams.pclass = pclass;
							applyParams.iLevel = iLevel;
							applyParams.fTableScale = 1.f;
							applyParams.erModOwner = pmod->erOwner;
							applyParams.uiApplyID = pmod->uiApplyID;
							applyParams.fHue = powerapp_GetHue(NULL,NULL,NULL,ppdefs[i]);
							applyParams.pExtract = pExtract;

							character_ApplyUnownedPowerDef(iPartitionIdx, pchar, ppdefs[i], &applyParams);

						}
						else
						{
							location_ApplyPowerDef(pmod->vecSource,
								iPartitionIdx,
								ppdefs[i],
								0,
								pmod->vecSource,
								NULL,
								pcharSource,
								pclass,
								iLevel,
								pmod->erOwner);
						}
					}
				}
				eaDestroy(&ppdefs);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}



static void ModProcessApplyPower(int iPartitionIdx, AttribMod *pmod, Character *pchar, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	{
		AttribModDef *pdef = pmod->pDef;
		ApplyPowerParams *pParams = (ApplyPowerParams*)(pdef->pParams);
		S32 bFail = false;
		Character *pcharTargetType = mod_GetApplyTargetTypeCharacter(iPartitionIdx,pmod,&bFail);
		if(!bFail && pParams && pmod->pSourceDetails)
		{
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			if(ppowdef)
			{
				Entity *eModSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
				Character *pModSource = eModSource ? eModSource->pChar : NULL;
				Entity *eModOwner = entFromEntityRef(iPartitionIdx,pmod->erOwner);
				Character *pModOwner = eModOwner ? eModOwner->pChar : NULL;
				Character *pApplySource = pchar;
				EntityRef erApplyTarget = pmod->erOwner;

				if(pParams->eSource==kApplyPowerEntity_ModOwner)
				{
					pApplySource = pModOwner;
				}
				else if(pParams->eSource==kApplyPowerEntity_ModSource)
				{
					pApplySource = pModSource;
				}
				else if(pParams->eSource==kApplyPowerEntity_ModSourceCreator)
				{
					Entity *eCreator = eModSource ? entFromEntityRef(iPartitionIdx,eModSource->erCreator) : NULL;
					pApplySource = eCreator ? eCreator->pChar : NULL;
				}
				else if(pParams->eSource==kApplyPowerEntity_ModSourceTargetDual)
				{
					EntityRef erSpecial = pModSource ? character_GetTargetDualOrSelfRef(iPartitionIdx, pModSource) : 0;
					Entity *eSpecial = erSpecial ? entFromEntityRef(iPartitionIdx,erSpecial) : NULL;
					pApplySource = eSpecial ? eSpecial->pChar : NULL;
				}
				else if(pParams->eSource==kApplyPowerEntity_HeldObject)
				{
					pApplySource = NULL;
					if(pModSource && pModSource->erHeld)
					{
						Entity *eHeld = entFromEntityRef(iPartitionIdx,pModSource->erHeld);
						if(eHeld && eHeld->pChar)
						{
							pApplySource = eHeld->pChar;
						}
					}
				}

				if(pApplySource)
				{
					S32 bValid = true, bUseVecTarget = false;
					Vec3 vecTarget;

					if(pParams->eTarget==kApplyPowerEntity_ModSource)
					{
						erApplyTarget = pmod->erSource;
					}
					else if(pParams->eTarget==kApplyPowerEntity_ModSourceCreator)
					{
						erApplyTarget = eModSource ? eModSource->erCreator : 0;
						if(!erApplyTarget)
						{
							bValid = false;
						}
					}
					else if(pParams->eTarget==kApplyPowerEntity_ModSourceTargetDual)
					{
						erApplyTarget = pModSource ? character_GetTargetDualOrSelfRef(iPartitionIdx, pModSource) : 0;
						if(!erApplyTarget)
						{
							bValid = false;
						}
					}
					else if(pParams->eTarget==kApplyPowerEntity_ModTarget)
					{
						erApplyTarget = entGetRef(pchar->pEntParent);
					}
					else if(pApplySource && pParams->eTarget==kApplyPowerEntity_RandomNotSource)
					{
						erApplyTarget = character_FindRandomTargetForPowerDef(iPartitionIdx,pApplySource,ppowdef,pcharTargetType,pmod->erSource);
						if(!erApplyTarget)
						{
							bValid = false;
						}
					}
					else if(pApplySource && pParams->eTarget==kApplyPowerEntity_RandomNotApplicationTarget)
					{
						erApplyTarget = character_FindRandomTargetForPowerDef(iPartitionIdx,pApplySource,ppowdef,pcharTargetType,pmod->pSourceDetails->erTargetApplication);
						if(!erApplyTarget)
						{
							bValid = false;
						}
					}
					else if(pApplySource && pParams->eTarget==kApplyPowerEntity_Random)
					{
						erApplyTarget = character_FindRandomTargetForPowerDef(iPartitionIdx,pApplySource,ppowdef,pcharTargetType,0);
						if(!erApplyTarget)
						{
							bValid = false;
						}
					}
					else if(pApplySource
							&& (pParams->eTarget==kApplyPowerEntity_ClosestNotSource
								|| pParams->eTarget==kApplyPowerEntity_ClosestNotSourceOrTarget
								|| pParams->eTarget==kApplyPowerEntity_ClosestNotTarget))
					{
						EntityRef erExcludeSource = (pParams->eTarget==kApplyPowerEntity_ClosestNotSource || pParams->eTarget==kApplyPowerEntity_ClosestNotSourceOrTarget) ? pmod->erSource : 0;
						EntityRef erExcludeTarget = (pParams->eTarget==kApplyPowerEntity_ClosestNotTarget || pParams->eTarget==kApplyPowerEntity_ClosestNotSourceOrTarget) ? entGetRef(pchar->pEntParent) : 0;
						erApplyTarget = character_FindClosestTargetForPowerDef(iPartitionIdx,pApplySource,ppowdef,pcharTargetType,erExcludeSource,erExcludeTarget);
						if(!erApplyTarget)
						{
							bValid = false;
						}
					}
					else if(pParams->eTarget==kApplyPowerEntity_ApplicationTarget)
					{
						erApplyTarget = pmod->pSourceDetails->erTargetApplication;
						if(!ISZEROVEC3(pmod->pSourceDetails->vecTargetApplication))
						{
							copyVec3(pmod->pSourceDetails->vecTargetApplication,vecTarget);
							bUseVecTarget = true;
						}
					}

					if(bValid)
					{
						ApplyUnownedPowerDefParams applyParams = {0};
						static Power **s_eaPowEnhancements = NULL;

						applyParams.pmod = pmod;
						applyParams.erTarget = erApplyTarget;
						applyParams.pVecTarget = (bUseVecTarget ? vecTarget : NULL);
						applyParams.pSubtarget = pmod->pSubtarget;
						applyParams.pcharSourceTargetType = pcharTargetType;
						applyParams.pclass = GET_REF(pmod->pSourceDetails->hClass);
						applyParams.iLevel = pmod->pSourceDetails->iLevel;
						applyParams.iIdxMulti = pmod->pSourceDetails->iIdxMulti;
						applyParams.fTableScale = pmod->pSourceDetails->fTableScale;
						applyParams.iSrcItemID = pmod->pSourceDetails->iItemID;
						applyParams.bLevelAdjusting = pmod->pSourceDetails->bLevelAdjusting;
						applyParams.bEvalHitChanceWithoutPower = pParams->bCanMiss;
						applyParams.ppStrengths = pmod->ppApplyStrengths;
						applyParams.pCritical = pmod->pSourceDetails->pCritical;
						applyParams.erModOwner = pmod->erOwner;
						applyParams.uiApplyID = pmod->uiApplyID;
						applyParams.fHue = pmod->fHue;
						applyParams.pExtract = pExtract;
						applyParams.bCountModsAsPostApplied = true;

						if(pModOwner)
						{
							power_GetEnhancementsForAttribModApplyPower(iPartitionIdx, pModOwner, 
																		pmod, EEnhancedAttribList_DEFAULT, 
																		ppowdef, &s_eaPowEnhancements);
							applyParams.pppowEnhancements = s_eaPowEnhancements;
						}

						// if the power is weapon based, if the mod has an owner use that as the weapon picker
						if (ppowdef->bWeaponBased && pmod->erOwner)
						{
							Entity *pOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
							applyParams.pCharWeaponPicker = SAFE_MEMBER(pOwner, pChar);
						}

						character_ApplyUnownedPowerDef(iPartitionIdx, pApplySource, ppowdef, &applyParams);
						
						eaClear(&s_eaPowEnhancements);
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessAttribModDamage(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		AttribModDef *pdef = pmod->pDef;
		AttribModDamageParams *pParams = (AttribModDamageParams *)pdef->pParams;
		AttribType uiDamageType = pParams ? pParams->offattribDamageType : kAttribType_Hold;
		S32 idx;
		AttribType *pUnroll;
		F32 fDamageMag = mod_GetEffectiveMagnitude(iPartitionIdx,pmod,pdef,pchar);
		Entity *eSource = entFromEntityRef(iPartitionIdx,pmod->erSource);

		damageTracker_AddTick(iPartitionIdx,
			pchar,
			pmod->erOwner,
			pmod->erSource,
			pchar->pEntParent->myRef,
			fDamageMag,
			fDamageMag, 
			uiDamageType, 
			pmod->uiApplyID, 
			GET_REF(pmod->hPowerDef),
			pdef->uiDefIdx, 
			pParams ? pParams->puiAffectsAttribTypes : NULL,
			pmod->eFlags | kCombatTrackerFlag_Pseudo);

		character_CombatEventTrackInOut(pchar, kCombatEvent_AttribDamageIn, kCombatEvent_AttribDamageOut,
										eSource, pdef->pPowerDef, pdef, fDamageMag, fDamageMag, NULL, NULL);

		//Unroll if the damage is a set
		pUnroll = attrib_Unroll(uiDamageType);
		for(idx=eaiSize(&pUnroll)-1; idx>=0; idx--)
		{
			uiDamageType = pUnroll[idx];
			damageTracker_AddTick(iPartitionIdx,
				pchar,
				pmod->erOwner,
				pmod->erSource,
				pchar->pEntParent->myRef,
				fDamageMag,
				fDamageMag, 
				uiDamageType, 
				pmod->uiApplyID, 
				GET_REF(pmod->hPowerDef),
				pdef->uiDefIdx, 
				pParams ? pParams->puiAffectsAttribTypes : NULL,
				pmod->eFlags | kCombatTrackerFlag_Pseudo);
		}
	}
	PERFINFO_AUTO_STOP();
}

static int AttribModSortDurationLeast(const AttribMod** a, const AttribMod** b)
{
	F32 fDelta = (*a)->fDuration - (*b)->fDuration;

	if(fDelta==0)
		return 0;

	if(fDelta>0) // A has more
		return 1; // B comes first

	return -1;
}

static int AttribModSortDurationMost(const AttribMod** a, const AttribMod** b)
{
	F32 fDelta = (*a)->fDuration - (*b)->fDuration;

	if(fDelta==0)
		return 0;

	if(fDelta>0) // A has more
		return -1; // A comes first

	return 1;
}

static int AttribModSortDurationUsedLeast(const AttribMod** a, const AttribMod** b)
{
	F32 fDurationUsedA = (*a)->fDurationOriginal - (*a)->fDuration;
	F32 fDurationUsedB = (*b)->fDurationOriginal - (*b)->fDuration;
	F32 fDelta = fDurationUsedA - fDurationUsedB;

	if(fDelta==0)
		return 0;

	if(fDelta>0) // A has used more
		return 1; // B comes first

	return -1;
}

static int AttribModSortDurationUsedMost(const AttribMod** a, const AttribMod** b)
{
	F32 fDurationUsedA = (*a)->fDurationOriginal - (*a)->fDuration;
	F32 fDurationUsedB = (*b)->fDurationOriginal - (*b)->fDuration;
	F32 fDelta = fDurationUsedA - fDurationUsedB;
	
	if(fDelta==0)
		return 0;

	if(fDelta>0) // A has used more
		return -1; // A comes first

	return 1;
}

static void ModProcessAttribModExpire(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		AttribModExpireParams *pParams = (AttribModExpireParams*)pmod->pDef->pParams;
		if(pParams)
		{
			int i,s=eaSize(&pchar->modArray.ppMods);
			int iExpired = 0;
			int iLimit = pmod->fMagnitude > 0 ? floor(pmod->fMagnitude) : s;
			S32 bGroup = pParams->bGroupByApplication;

			if(!bGroup && (pParams->eOrder==kAttribModExpireOrder_Unset || iLimit>=s))
			{
				// Unset order, or no practical limit on the number of targets, so just walk the list
				for(i=0; i<s && iExpired<iLimit; i++)
				{
					AttribMod *pmodTarget = pchar->modArray.ppMods[i];
					if(!pmodTarget->pDef->bIgnoreAttribModExpire
						&& pmodTarget->fDuration > 0
						&& pmodTarget!=pmod
						&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmod->pDef,pchar,pmod,pmodTarget->pDef,pmodTarget,pmodTarget->pDef->pPowerDef))
					{
						character_ModExpireReason(pchar, pmodTarget, kModExpirationReason_AttribModExpire);
						entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
						iExpired++;
					}
				}
			}
			else
			{
				// Grouped by app, or specific order with limited targets, so find all the possible targets first
				AttribMod **ppModsLegal = NULL;
				U32 *puiIDsLegal = NULL;
				for(i=0; i<s; i++)
				{
					AttribMod *pmodTarget = pchar->modArray.ppMods[i];
					if(!pmodTarget->pDef->bIgnoreAttribModExpire
						&& pmodTarget->fDuration > 0
						&& pmodTarget!=pmod
						&& moddef_AffectsModOrPowerChk(iPartitionIdx,pmod->pDef,pchar,pmod,pmodTarget->pDef,pmodTarget,pmodTarget->pDef->pPowerDef))
					{
						eaPush(&ppModsLegal,pmodTarget);
						if(bGroup)
							ea32PushUnique(&puiIDsLegal,pmodTarget->uiApplyID);
					}
				}

				// Count number of potential targets
				if(bGroup)
				{
					s = ea32Size(&puiIDsLegal);
				}
				else
				{
					s = eaSize(&ppModsLegal);
				}

				// If we're not going to expire them all...
				if(iLimit<s)
				{
					// ... sort the AttribMods
					switch(pParams->eOrder)
					{
					case kAttribModExpireOrder_DurationLeast:
						eaQSort(ppModsLegal,AttribModSortDurationLeast);
						break;
					case kAttribModExpireOrder_DurationMost:
						eaQSort(ppModsLegal,AttribModSortDurationMost);
						break;
					case kAttribModExpireOrder_DurationUsedLeast:
						eaQSort(ppModsLegal,AttribModSortDurationUsedLeast);
						break;
					case kAttribModExpireOrder_DurationUsedMost:
						eaQSort(ppModsLegal,AttribModSortDurationUsedMost);
						break;
					}

					// ... and rebuild the IDs in order
					if(bGroup)
					{
						ea32ClearFast(&puiIDsLegal);
						s = eaSize(&ppModsLegal);
						for(i=0; i<s; i++)
							ea32PushUnique(&puiIDsLegal,ppModsLegal[i]->uiApplyID);
						s = ea32Size(&puiIDsLegal);
					}
				}

				// Then expire as much as allowed
				for(i=0; i<s && iExpired<iLimit; i++)
				{
					if(bGroup)
					{
						int j;
						U32 uiID = puiIDsLegal[i];
						for(j=eaSize(&ppModsLegal)-1; j>=0; j--)
						{
							if(ppModsLegal[j]->uiApplyID==uiID)
							{
								character_ModExpireReason(pchar, ppModsLegal[j], kModExpirationReason_AttribModExpire);
							}
						}
						iExpired++;
					}
					else
					{
						AttribMod *pmodTarget = ppModsLegal[i];
						character_ModExpireReason(pchar, pmodTarget, kModExpirationReason_AttribModExpire);
						iExpired++;
					}
				}

				// And clean up
				eaDestroy(&ppModsLegal);
				ea32Destroy(&puiIDsLegal);
			}

			// Remove the count of expired AttribMods, and potentially expire this
			//  as well.
			if(pmod->fMagnitude > 0)
			{
				pmod->fMagnitude -= iExpired;
				if(floor(pmod->fMagnitude) <= 0)
				{
					character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessAttribOverride(AttribMod *pmod, Character *pchar)
{
	AttribModDef *pdef = pmod->pDef;
	PERFINFO_AUTO_START_FUNC();
	eaPush(&pchar->modArray.ppOverrideMods,pmod);
#ifdef GAMESERVER
	if(((AttribOverrideParams *)pdef->pParams)->offAttrib == kAttribType_HitPoints && (pchar)->bCanRegen[0] == true)
	{
		(pchar)->bCanRegen[0] = false;
	}
	else if(((AttribOverrideParams *)pdef->pParams)->offAttrib == kAttribType_Power && (pchar)->bCanRegen[1] == true)
	{
		(pchar)->bCanRegen[1] = false;
	}
	else if(((AttribOverrideParams *)pdef->pParams)->offAttrib == kAttribType_Air && (pchar)->bCanRegen[2] == true)
	{
		(pchar)->bCanRegen[2] = false;
	}
#endif
	PERFINFO_AUTO_STOP();
}

typedef struct BecomeCritterStruct
{
	int iPartitionIdx;
	EntityRef erTarget;
	AttribMod *pmod;
	U32 uiApplyID;
	AttribModDef *pmoddef;
	F32 fHue;
	int idx;
} BecomeCritterStruct;

static int BecomeCritterCallback(Power *ppow, BecomeCritterStruct *pStruct)
{
	bool bSuccess = false;

	if(ppow && pStruct)
	{
		Entity *e = entFromEntityRef(pStruct->iPartitionIdx,pStruct->erTarget);
		if(e && e->pChar)
		{
			int i;
			int iPartitionIdx = entGetPartitionIdx(e);
			Character *pchar = e->pChar;
			for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
			{
				AttribMod *pmod = pchar->modArray.ppMods[i];
				if(pmod==pStruct->pmod)
				{
					// We found the supposed AttribMod that started this whole process,
					//  but if the transaction was slow, it may have actually been destroyed
					//  and then reallocated as some entirely other AttribMod.  So we
					//  do a careful check to make sure this is the same guy.
					if(verify(pmod->pDef==pStruct->pmoddef
								&& pmod->uiApplyID==pStruct->uiApplyID
								&& eaSize(&pmod->ppPowersCreated)>pStruct->idx
								&& pmod->ppPowersCreated[pStruct->idx]==NULL))
					{
						// Commented out because it could be breaking things, though I doubt it.  It's not really necessary.
						//if(character_AddPower(pchar,ppow,kPowerSource_AttribMod))
						{
							GameAccountDataExtract *pExtract = NULL; // don't need this data for this use

							pmod->ppPowersCreated[pStruct->idx] = ppow;
							eaIndexedEnable(&pchar->modArray.ppPowers,parse_Power);
							eaPush(&pchar->modArray.ppPowers,ppow);
#ifdef GAMESERVER
							entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
							if(pStruct->fHue)
								character_SetPowerHue(pchar,ppow->uiID,pStruct->fHue);
#endif
							bSuccess = true;
							character_ResetPowersArray(iPartitionIdx, pchar, pExtract);
							character_DirtyPowerStats(pchar);
						}
					}
					break;
				}
			}
		}
	}

	if(pStruct)
	{
		free(pStruct);
	}

	return bSuccess;
}

static int CmpCritterPowerConfig(const CritterPowerConfig **power1, const CritterPowerConfig **power2)
{
	if ((*power1)->fOrder > (*power2)->fOrder) {
		return 1;
	} else if ((*power1)->fOrder < (*power2)->fOrder) {
		return -1;
	}
	return 0;
}

static void ModProcessBecomeCritter(int iPartitionIdx, AttribMod *pmod, Character *pchar, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	{
		BecomeCritterParams *pParams = (BecomeCritterParams*)(pmod->pDef->pParams);
		if(!pchar->bBecomeCritter)
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		pchar->bBecomeCritter = true;
		if(pParams)
		{
#ifdef GAMESERVER
			S32 bClassChange = (GET_REF(pParams->hClass)!=GET_REF(pchar->hClassTemporary));
			if(bClassChange)
			{
				COPY_HANDLE(pchar->hClassTemporary,pParams->hClass);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}

			if(!eaSize(&pmod->ppPowersCreated))
			{
				S32 iLevel = entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER) ? entity_GetSavedExpLevel(pchar->pEntParent) : pchar->iLevelCombat;
				CritterDef *pdefCritter = GET_REF(pParams->hCritter);
				if(pdefCritter)
				{
					int i,idx,s=eaSize(&pdefCritter->ppPowerConfigs);
					CritterPowerConfig **ppConfigs = NULL;
					eaCopy(&ppConfigs,&pdefCritter->ppPowerConfigs);
					eaQSort(ppConfigs,CmpCritterPowerConfig);
					eaSetSize(&pmod->ppPowersCreated,s); // Make sure there's enough room to start, will resize after
					for(i=0,idx=0; i<s; i++)
					{
						CritterPowerConfig *pConfig = ppConfigs[i];
						PowerDef *pdefPower = GET_REF(pConfig->hPower);
						if(pdefPower
							&& pConfig->iMinLevel <= iLevel
							&& (pConfig->iMaxLevel <= 0 || pConfig->iMaxLevel >= iLevel))
						{
							BecomeCritterStruct *pStruct = malloc(sizeof(BecomeCritterStruct));
							pStruct->iPartitionIdx = iPartitionIdx;
							pStruct->erTarget = entGetRef(pchar->pEntParent);
							pStruct->pmod = pmod;
							pStruct->uiApplyID = pmod->uiApplyID;
							pStruct->pmoddef = pmod->pDef;
							pStruct->fHue = pmod->fHue ? pmod->fHue : pdefCritter->fHue;
							pStruct->idx = idx++;
							character_AddPowerTemporary(pchar,pdefPower,BecomeCritterCallback,pStruct);
						}
					}
					eaSetSize(&pmod->ppPowersCreated,idx);
					eaDestroy(&ppConfigs);
				}
			}

			if(bClassChange)
				character_SetClassCallback(pchar->pEntParent, pExtract);
#endif
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessDamageTrigger(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		DamageTriggerParams *pParams = (DamageTriggerParams*)(pmod->pDef->pParams);

		// Cleanup if out of charges
		if(pParams && pParams->bMagnitudeIsCharges && pmod->fMagnitude < 1)
		{
			character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
		}
		else
		{
			eaPush(&pchar->ppModsDamageTrigger,pmod);
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessDisableTacticalMovement(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
#ifdef GAMESERVER
		if (!pmod->bActive)
		{
			DisableTacticalMovementParams *pParams = (DisableTacticalMovementParams*)(pmod->pDef->pParams);
			U32 uiID = pmod->uiPowerID | (pmod->pDef->uiDefIdx << POWERID_BASE_BITS);
			mrTacticalNotifyPowersStart(pchar->pEntParent->mm.mrTactical, uiID, pParams->eFlags|TDF_QUEUE, pmTimestamp(0));
			pmod->bActive = true;
		}
#endif
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessDropHeldObject(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		int i;
#ifdef GAMESERVER
		if(IS_HANDLE_ACTIVE(pchar->hHeldNode))
		{
			eventsend_RecordObjectDeath(pchar->pEntParent,REF_STRING_FROM_HANDLE(pchar->hHeldNode));
		}
#endif

		REMOVE_HANDLE(pchar->hHeldNode);
		pchar->erHeld = 0;
		pchar->fHeldMass = 0;
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		character_AnimFXCarryOff(pchar);
		// TODO(JW): Hack: Seriously
		for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
		{
			AttribModDef *pmoddefTarget = pchar->modArray.ppMods[i]->pDef;
			if(eaSize(&pmoddefTarget->ppchContinuingFX) && eaSize(&pmoddefTarget->ppContinuingFXParams))
			{
				character_ModExpireReason(pchar, pchar->modArray.ppMods[i], kModExpirationReason_Unset);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}


// Strikes a target EntityRef with the source Character's held object, and makes the source Character drop said object
void character_DropHeldObjectOnTarget(int iPartitionIdx, Character *pchar, EntityRef erTarget, GameAccountDataExtract *pExtract)
{
	if(pchar->erHeld || IS_HANDLE_ACTIVE(pchar->hHeldNode))
	{
		Entity *eTarget = entFromEntityRef(iPartitionIdx,erTarget);
		EntityRef erSource = entGetRef(pchar->pEntParent);
		if(eTarget && eTarget->pChar && eTarget->pChar!=pchar)
		{
			WorldInteractionNode *pnode = GET_REF(pchar->hHeldNode);
			if(pnode)
			{
				CharacterClass *pclass = im_GetCharacterClass(pnode);
				S32 iLevel = im_GetLevel(pnode);
				if(pclass)
				{
					PowerDef **ppdefs = NULL;
					int j,t;
					if(0!=(t=im_GetDeathPowerDefs(pnode,&ppdefs)))
					{
						for(j=0; j<t; j++)
						{
							ApplyUnownedPowerDefParams applyParams = {0};

							applyParams.erTarget = erTarget;
							applyParams.pcharSourceTargetType = pchar;
							applyParams.pclass = pclass;
							applyParams.iLevel = iLevel;
							applyParams.fTableScale = 1.f;
							applyParams.erModOwner = erSource;
							applyParams.uiApplyID = powerapp_NextID();
							applyParams.fHue = powerapp_GetHue(NULL,NULL,NULL,ppdefs[j]);
							applyParams.pExtract = pExtract;

							character_ApplyUnownedPowerDef(iPartitionIdx, eTarget->pChar, ppdefs[j], &applyParams);
						}
					}
				}
			}
		}

		ModProcessDropHeldObject(NULL,pchar);
	}
}

static void ModProcessEntAttach(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		EntAttachParams *pParams = (EntAttachParams*)(pmod->pDef->pParams);
		if(pParams && !pmod->erCreated)
		{
			Entity *e = NULL;
#ifdef GAMESERVER
			e = critter_Create("FakeCritter", NULL, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(pchar->pEntParent), NULL, 1, 1, 0, 0, 0, 0, 0, 0, 0, NULL, NULL);

			if(e)
			{
				Entity *eCreator;
				Vec3 tempVec = {0, 0, 0};

				// Set creator/owner

				eCreator = pchar->pEntParent;
				e->erOwner = entGetRef(eCreator);
				entity_SetDirtyBit(e, parse_Entity, e, false);
				if(eCreator && eCreator->erOwner)
				{
					e->erOwner = eCreator->erOwner;
				}
				else
				{
					e->erOwner = e->erCreator;
				}

				// Set up rider
				REMOVE_HANDLE(e->costumeRef.hReferencedCostume);
				SET_HANDLE_FROM_STRING("PlayerCostume", pParams->pchCostumeName, e->costumeRef.hReferencedCostume);
				costumeGenerate_FixEntityCostume(e);

				gslEntAttachToEnt(e, eCreator, pParams->pchAttachBone, pParams->pchExtraBit, tempVec, unitquat);

				// Set mod info
				pmod->erCreated = entGetRef(e);
			}
#endif
		}
	}
	PERFINFO_AUTO_STOP();
}

#ifdef GAMESERVER
// attaches any power enhancements from the creator entity to the new entity
static void ModEntityCreateAttachEnhancements(int iPartitionIdx, Entity *pCreatorEnt, Entity *pNewEnt)
{
	// apply any enhancements that the creator might have for me.
	if (pCreatorEnt && eaSize(&pCreatorEnt->pChar->ppPowersEntCreateEnhancements) && pNewEnt->pChar)
	{
		FOR_EACH_IN_EARRAY(pCreatorEnt->pChar->ppPowersEntCreateEnhancements, Power, pPower)
		{
			// check to see if this power can be applied to this entcreate
			PowerDef *pDef = GET_REF(pPower->hDef);
			if (pDef && 
				power_EnhancementAttachToEntCreateAllowed(iPartitionIdx, pCreatorEnt->pChar, pDef, pNewEnt))
			{
				character_AddPowerPersonal(iPartitionIdx, pNewEnt->pChar, pDef, 0, false, NULL);
			}
		}
		FOR_EACH_END
	}

}
#endif

// Takes the input vector and adds the forward, right and above offsets based on the facing of the Entity
static void VecOffsetInFacingDirection(	Vec3 vPosInOut,	
										Entity *e,	
										const Vec3 vOverridePYR,	
										F32 fFront, F32 fRight, F32 fAbove,
										bool bOffsetUsesPitch,
										bool bOffsetUsesRoll)
{
	
	Mat3 xMat;

	if (vOverridePYR)
	{
		Vec3 vPYR;
		copyVec3(vOverridePYR, vPYR);

		if (!bOffsetUsesPitch)
		{
			vPYR[0] = 0.0f;
		}
		if (!!bOffsetUsesRoll)
		{
			vPYR[2] = 0.0f;
		}
		createMat3YPR(xMat, vPYR);

		scaleAddVec3(xMat[0],fRight,vPosInOut,vPosInOut);
		scaleAddVec3(xMat[1],fAbove,vPosInOut,vPosInOut);
		scaleAddVec3(xMat[2],fFront,vPosInOut,vPosInOut);
	}
	else 
	{
		Vec3 pyrFace;
		
		devassert(e);

		entGetFacePY(e, pyrFace);
		if (!bOffsetUsesPitch)
		{
			pyrFace[0] = 0.0f;
		}
		pyrFace[2] = 0.0f;

		if (bOffsetUsesPitch || bOffsetUsesRoll)
		{
			if (bOffsetUsesRoll)
			{
				Vec3 pyrRot;
				Quat qRot;
				entGetRot(e, qRot);
				quatToPYR(qRot, pyrRot);
				pyrFace[2] = pyrRot[2];
			}

			createMat3YPR(xMat, pyrFace);

			scaleAddVec3(xMat[0],fRight,vPosInOut,vPosInOut);
			scaleAddVec3(xMat[1],fAbove,vPosInOut,vPosInOut);
			scaleAddVec3(xMat[2],fFront,vPosInOut,vPosInOut);
		}
		else
		{
			F32 fAngle;
			Vec3 vForward;
			createMat3_2_YP(vForward, pyrFace);

			normalVec3(vForward);
			scaleAddVec3(vForward,fFront,vPosInOut,vPosInOut);

			fAngle = atan2(-vForward[2],vForward[0]);
			fAngle += HALFPI;
			vForward[0] = cos(fAngle);
			vForward[2] = -sin(fAngle);
			scaleAddVec3XZ(vForward,fRight,vPosInOut,vPosInOut);

			vPosInOut[1] += fAbove;
		}
	}
}

static const Capsule* EntGetWorldCollisionCapsule()
{
	static const Capsule s_capsule = {	{0.f, ENT_WORLDCAP_DEFAULT_PLAYER_STEP_HEIGHT, 0.f}, 
										{0.f, 1.0f, 0.f}, 
										(ENT_WORLDCAP_DEFAULT_HEIGHT_OFFUP - ENT_WORLDCAP_DEFAULT_PLAYER_STEP_HEIGHT), 
										ENT_WORLDCAP_DEFAULT_RADIUS };
	return &s_capsule;
}

static bool EntGetValidOffsetPosition(	S32 iPartitionIdx, 
										Entity* pFaceEnt,
										Entity* pBaseEnt,
										Quat qRot, 
										bool bTryTeleportFirst, 
										int iRetryCount, 
										const Vec3 vPosSource, 
										Vec3 vPosTargetOut)
{
	S32 bValidLoc = false;
#ifdef GAMESERVER
	WorldCollCollideResults wcResults;
	Vec3 vCastSource, vCastEnd;
	const Capsule *pCap = EntGetWorldCollisionCapsule();
	WorldColl *pWorldColl;

	// Collide with world
	// Lift up to about waist height
	copyVec3(vPosSource, vCastSource);
	copyVec3(vPosTargetOut, vCastEnd);
	
	vCastSource[1] += 3.0f;
	vCastEnd[1] += 3.0f;

	if(bTryTeleportFirst)
	{
		if(entity_LocationValid(pBaseEnt, vCastEnd))
		{
			bValidLoc = true;
		}
	}

	pWorldColl = worldGetActiveColl(iPartitionIdx);

	if(!bValidLoc && 
			(wcCapsuleCollideEx(pWorldColl, *pCap, vCastSource, vCastEnd, WC_QUERY_BITS_WORLD_ALL, &wcResults) ||
			 wcRayCollide(pWorldColl, vCastSource, vCastEnd, WC_QUERY_BITS_WORLD_ALL, &wcResults)))
	{
		bValidLoc = true;
		copyVec3(wcResults.posWorldEnd,vPosTargetOut);

		if (pFaceEnt && iRetryCount > 0)
		{
			Vec3 vFacePos;
			entGetPos(pFaceEnt, vFacePos);
			quatLookAt(vPosTargetOut, vFacePos, qRot);
			return EntGetValidOffsetPosition(iPartitionIdx, 
											 pFaceEnt, 
											 pBaseEnt, 
											 qRot, 
											 bTryTeleportFirst, 
											 iRetryCount-1, 
											 vPosSource, 
											 vPosTargetOut);
		}
	}
#endif
	return bValidLoc;
}

// now capsule cast downwards to just above the vStartPos to see if we find the ground.
//returns the height of the ground, or the original height if no ground found.
F32 ModCapsuleCastDown(const Vec3 vStartPos, Entity* pEnt)
{
	Vec3 vCapsuleCastSt, vCapsuleCastEnd;
	WorldColl *pWorldColl = worldGetActiveColl(entGetPartitionIdx(pEnt));
	WorldCollCollideResults resultsOut = {0};
	static const Capsule s_groundFindCapsule = {	{0.f, 0.0f, 0.f}, 
	{0.f, 1.0f, 0.f}, 
	ENT_WORLDCAP_DEFAULT_RADIUS, 
	1.f };

	copyVec3(vStartPos, vCapsuleCastSt);
	copyVec3(vStartPos, vCapsuleCastEnd);

	vCapsuleCastEnd[1] -= 7.1f;
	vCapsuleCastSt[1] += 3.f + ENT_WORLDCAP_DEFAULT_HEIGHT_OFFUP * 0.4f;
	if (wcCapsuleCollideEx(	pWorldColl, 
		s_groundFindCapsule, 
		vCapsuleCastSt, 
		vCapsuleCastEnd,
		WC_QUERY_BITS_WORLD_ALL, 
		&resultsOut))
	{

		return resultsOut.posWorldImpact[1];

	}
	return vStartPos[1];
}


// Adjust the position of the entity by the oriented offsets
static bool EntGetOffsetPositionByOrientedOffsets(Entity *pCapsuleEnt,
												  Entity *pFaceEnt,		// the entity to face
												  Entity *pBaseEnt,		// the entity to cast from
												  const Vec3 vOverridePYR, // uses this as oriented offset direction if valid
												  Quat qRot,
												  const Vec3 vOffset, 
												  bool bTryTeleportFirst,
												  bool bOffsetUsesPitch,
												  bool bOffsetUsesRoll,
												  bool bClampToGround,
												  const Vec3 vPosSource, 
												  Vec3 vPosTargetOut)
{
	const int iRetryCount = 2;

	copyVec3(vPosSource, vPosTargetOut);

	if (!ISZEROVEC3(vOffset))
	{
		VecOffsetInFacingDirection(vPosTargetOut, pBaseEnt, vOverridePYR, vOffset[0], vOffset[1], vOffset[2], bOffsetUsesPitch, bOffsetUsesRoll);
	}

	// If there is a facing ent, set rotation to face
	if (pFaceEnt)
	{
		Vec3 vFacePos;
		entGetPos(pFaceEnt, vFacePos);
		quatLookAt(vPosTargetOut, vFacePos, qRot);
	}

	if (bClampToGround)
	{
		vPosTargetOut[1] = ModCapsuleCastDown(vPosTargetOut, pCapsuleEnt);
	}

	if (ISZEROVEC3(vOffset))
		return true;


	return EntGetValidOffsetPosition(	entGetPartitionIdx(pCapsuleEnt), 
										pFaceEnt, 
										pBaseEnt, 
										qRot, 
										bTryTeleportFirst, 
										iRetryCount, 
										vPosSource, 
										vPosTargetOut);
}

static void ModProcessEntCreate(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		EntCreateParams *pParams = (EntCreateParams*)(pmod->pDef->pParams);
		AttribModParams *pParamsMod = pmod->pParams;
		if(pParams && pParamsMod && !pmod->erCreated)
		{
			Entity *e = NULL;
			Entity *eCreator;
			CritterDef *pdefCritter = GET_REF(pParams->hCritter);
			CritterGroup *pCritterGroup = GET_REF(pParams->hCritterGroup);
			const char *pcSubRank = g_pcCritterDefaultSubRank;
			const char *pcRank = g_pcCritterDefaultRank;
			
			EntityRef erOwner = 0;

			eCreator = entFromEntityRef(iPartitionIdx,pmod->erSource);

			if(pParams->eStrength == kEntCreateStrength_Locked)
			{
				if(eCreator && eCreator->pCritter)
				{
					pcSubRank = eCreator->pCritter->pcSubRank;
					pcRank = eCreator->pCritter->pcRank;
				}
				else
				{
					if (pdefCritter && pdefCritter->pcSubRank)
					{
						pcSubRank = pdefCritter->pcSubRank;
						pcRank = pdefCritter->pcRank;
					}
					else
					{
						pcSubRank = g_pcCritterDefaultSubRank;
						pcRank = g_pcCritterDefaultRank;
					}
				}
			}
			else //if( pParams->eStrength == kEntCreateStrength_Independent)
			{
				pcSubRank = pParams->pcSubRank;
				pcRank = pParams->pcRank;
			}

			if(pParams->eTeam!=kEntCreateTeam_None)
			{
				if(eCreator && eCreator->erOwner)
				{
					erOwner = eCreator->erOwner;
				}
				else
				{
					erOwner = pmod->erSource;
				}
			}

			switch(pParams->eCreateType)
			{
			case kEntCreateType_Critter:
				{
					if(pdefCritter && (pParams->eTeam==kEntCreateTeam_None || entFromEntityRef(iPartitionIdx,erOwner)))
					{
						CritterCreateParams createParams = {0};
						createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
						createParams.iPartitionIdx = iPartitionIdx;
						createParams.fsmOverride = pParams->pchFSM;
						createParams.iLevel = MAX(pmod->fMagnitude,1);
						createParams.iTeamSize = 1;
						createParams.pcSubRank = pcSubRank;
						createParams.erOwner = erOwner;
						createParams.erCreator = pmod->erSource;
						createParams.pCostume = NULL;
						createParams.iCostumeKey = (pParams->bCanCustomizeCostume && pParamsMod->iParam) ? pParamsMod->iParam : critterdef_GetCostumeKeyFromIndex(pdefCritter,pParams->iCostumeDefault);
						createParams.fHue = pmod->fHue;
						createParams.bPowersEntCreated = true;

						if (pParams->pcBoneGroup && *pParams->pcBoneGroup && eCreator && eCreator->pSaved)
						{
							NOCONST(PlayerCostume) *pBaseCostume = CONTAINER_NOCONST(PlayerCostume, eCreator->pSaved->costumeData.eaCostumeSlots[eCreator->pSaved->costumeData.iActiveCostume]->pCostume);
							NOCONST(PlayerCostume) *pCostume = CONTAINER_NOCONST(PlayerCostume, costumeTailor_MakeCostumeOverlayEx(CONTAINER_RECONST(PlayerCostume, pBaseCostume), GET_REF(pParams->hSkeleton), pParams->pcBoneGroup, true, true));
							if (pCostume)
							{
								createParams.pCostume = (PlayerCostume *)pCostume;
							}
						}

						e = critter_CreateByDef(pdefCritter, &createParams, NULL, true);

						if (e && e->pCritter && eCreator && eCreator->pSaved)
						{
							if (pParams->bUseCreatorsDisplayName)
							{
								e->pCritter->displayNameOverride = StructAllocString(eCreator->pSaved->savedName);
							}
							else if (pParams->bUseCreatorsPuppetDisplayName && eCreator->pSaved->pPuppetMaster)
							{
								e->pCritter->displayNameOverride = StructAllocString(eCreator->pSaved->pPuppetMaster->curPuppetName);
							}
						}

						if (createParams.pCostume) StructDestroy(parse_PlayerCostume, createParams.pCostume);
					}
					break;
				}
			case kEntCreateType_Nemesis:
				{
					if(pCritterGroup && (pParams->eTeam==kEntCreateTeam_None || entFromEntityRef(iPartitionIdx, erOwner)))
					{
						Entity *pNemesisEnt = eCreator?player_GetPrimaryNemesis(eCreator):NULL;
						
						if (pNemesisEnt){
							e = critter_CreateNemesisMinion(pNemesisEnt, pcRank, pParams->pchFSM, NULL, NULL, NULL, 0, MAX(pmod->fMagnitude,1), 1, pcSubRank, entGetPartitionIdx(pchar->pEntParent), NULL, NULL, NULL, NULL, 0, eCreator, NULL);
							if(e && e->pCritter)
								pdefCritter = GET_REF(e->pCritter->critterDef);
						}
					}
					break;
				}
			case kEntCreateType_CritterOfGroup:
				{
					if(pCritterGroup && (pParams->eTeam==kEntCreateTeam_None || entFromEntityRef(iPartitionIdx, erOwner)))
					{
						e = critter_FindAndCreate(pCritterGroup, pcRank, NULL, NULL, NULL, 0, MAX(pmod->fMagnitude,1), 1, pcSubRank, GLOBALTYPE_ENTITYCRITTER, entGetPartitionIdx(pchar->pEntParent), pParams->pchFSM, 0, 0, 0, 0, NULL, 0, 0, eCreator, NULL, NULL, NULL);
						if(e && e->pCritter)
							pdefCritter = GET_REF(e->pCritter->critterDef);
					}
					break;
				}
			default:
				break;
			}

			if(e && pdefCritter)
			{
				Vec3 vPosSource,vPosTarget;
				Quat qRotFinal;
				Entity* eFaceEnt = NULL;

				// apply any enhancements that the creator might have for me.
				if (eCreator && pParams->eCreateType == kEntCreateType_Critter)
				{
					ModEntityCreateAttachEnhancements(iPartitionIdx, eCreator, e);
				}

				//Fill in targeting information
				e->erCreatorTarget = pParamsMod->erParam;

				if (e->pChar)
				{
					if(pmod->pSubtarget)
					{
						e->pChar->pSubtarget = StructClone(parse_PowerSubtargetChoice,pmod->pSubtarget);
						entity_SetDirtyBit(e, parse_Character, e->pChar, false);
					}

					if(pParams->eStrength==kEntCreateStrength_Locked && pmod->ppApplyStrengths)
					{
						eaCopyStructs(&pmod->ppApplyStrengths,&e->pChar->ppApplyStrengths,parse_PowerApplyStrength);
					}

					// Inherit the bLevelAdjusting flag from the AttribMod's source details if it's true
					if(pmod->pSourceDetails && pmod->pSourceDetails->bLevelAdjusting)
						e->pChar->bLevelAdjusting = true;

					if(pParams->bModsOwnedByOwner)
						e->pChar->bModsOwnedByOwner = true;
				}
				
				// Set Rotation
				if(pParams->eFaceType == kEntCreateFaceType_Absolute)
				{
					axisAngleToQuat(upvec,-RAD(pParams->fFacing), qRotFinal);
				}
				else if (pParams->eFaceType == kEntCreateFaceType_FaceCreator ||
						 pParams->eFaceType == kEntCreateFaceType_FaceTarget)
				{
					Entity* eApplyTarget = entFromEntityRef(iPartitionIdx, pmod->pSourceDetails->erTargetApplication);
					eFaceEnt = pchar->pEntParent;

					if (eCreator && pParams->eFaceType == kEntCreateFaceType_FaceCreator)
					{
						eFaceEnt = eCreator;
					}
					else if (eApplyTarget && pParams->eFaceType == kEntCreateFaceType_FaceTarget)
					{
						eFaceEnt = eApplyTarget;
					}
				}
				else 
				{
					Entity* eRot = pchar->pEntParent;
					Entity* eApplyTarget = entFromEntityRef(iPartitionIdx, pmod->pSourceDetails->erTargetApplication);
					Vec3 pyrFace;

					if (eCreator && pParams->eFaceType == kEntCreateFaceType_RelativeToCreator)
					{
						eRot = eCreator;
					}
					else if (eApplyTarget && pParams->eFaceType == kEntCreateFaceType_RelativeToTarget)
					{
						eRot = eApplyTarget;
					}
					entGetFacePY(eRot,pyrFace);

					if (!pParams->bUseFacingPitch)
					{
						pyrFace[0] = 0.f;
					}
					if (pParams->bUseFacingRoll)
					{
						Vec3 pyrTemp;
						Quat qRot;
						entGetRot(eRot, qRot);
						quatToPYR(qRot, pyrTemp);
						pyrFace[2] = pyrTemp[2];
					}
					else
					{
						pyrFace[2] = 0.f;
					}
					pyrFace[1] -= RAD(pParams->fFacing);

					PYRToQuat(pyrFace,qRotFinal);
				}

				// Get target position
				if (pParams->bCreateAtTargetedEntityPos)
				{
					Entity * pParamEnt = pParamsMod->erParam ? entFromEntityRef(iPartitionIdx, pParamsMod->erParam) : NULL;
					if(pParamEnt)
					{
						entGetPos(pParamEnt, vPosSource);
					}
					else if (!ISZEROVEC3(pmod->pParams->vecTarget))
					{
						copyVec3(pmod->pParams->vecTarget,vPosSource);
					}
					else
					{
						entGetPos(e, vPosSource);
					}
				}
				else
				{
					if (!pParams->bUseTargetPositionAsAIVarsTargetPos &&
						!ISZEROVEC3(pmod->pParams->vecTarget))
					{
						copyVec3(pmod->pParams->vecTarget,vPosSource);
					}
					else
					{
						entGetPos(pchar->pEntParent, vPosSource);					
					}
				}

				// Adjust position by oriented offsets
				EntGetOffsetPositionByOrientedOffsets(e,
													  eFaceEnt,
													  pchar->pEntParent,
													  NULL,
													  qRotFinal, 
													  pParamsMod->vecParam, 
													  pParams->bTryTeleportFirst,
													  pParams->bOffsetUsesPitchAndRoll,
													  pParams->bOffsetUsesPitchAndRoll,
													  pParams->bClampToGround,
													  vPosSource, 
													  vPosTarget);

				if (eFaceEnt && (pParams->fFacing || !pParams->bUseFacingPitch))
				{
					Vec3 vRotPYR;
					quatToPYR(qRotFinal, vRotPYR);
					if (pParams->fFacing)
					{
						vRotPYR[1] -= RAD(pParams->fFacing);
					}
					if (!pParams->bUseFacingPitch)
					{
						vRotPYR[0] = 0.0f;
					}
					PYRToQuat(vRotPYR, qRotFinal);
				}

				if (pParams->bUseTargetPositionAsAIVarsTargetPos)
					aiSetPowersEntCreateTargetLocation(e, pParamsMod->vecTarget);
				
				// Jitter and set
				vPosTarget[0] += 0.01f * randomF32();
				vPosTarget[2] += 0.01f * randomF32();
				entSetPos(e, vPosTarget, true, "EntCreateAttribMod");
				entSetRot(e, qRotFinal, true, "EntCreateAttribMod");

				// Set mod info
				pmod->erCreated = entGetRef(e);
			}
		}
		else if(pmod->erCreated)
		{
			if(!entFromEntityRef(iPartitionIdx, pmod->erCreated))
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}


static void ModProcessEntCreateVanity(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		EntCreateVanityParams *pParams = (EntCreateVanityParams*)(pmod->pDef->pParams);
		AttribModParams *pParamsMod = pmod->pParams;
		if(pParams && pParamsMod && !pmod->erCreated)
		{
			Entity *e = NULL;
			CritterDef *pdefCritter = GET_REF(pParams->hCritter);
			Entity *eSource = pchar->pEntParent;
			EntityRef erSource = entGetRef(eSource);
			int i;

			if(pdefCritter)
			{
				CritterCreateParams createParams = {0};
				createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
				createParams.iPartitionIdx = iPartitionIdx;
				createParams.iLevel = 1;
				createParams.iTeamSize = 1;
				createParams.pcSubRank = g_pcCritterDefaultSubRank;
				createParams.erOwner = erSource;
				createParams.erCreator = erSource;

				e = critter_CreateByDef(pdefCritter, &createParams, NULL, true);
			}

			if(e)
			{
				Vec3 vPosSource, vPosTarget;
				Quat qRotSource;

				if(!pdefCritter->bIgnoreEntCreateHue)
				{
					e->fHue = pmod->fHue;
					entity_SetDirtyBit(e, parse_Entity, e, false);
				}

				// Get and set rotation
				entGetRot(eSource, qRotSource);
				entSetRot(e, qRotSource, true, "EntCreateVanityAttribMod");

				// Get source position
				entGetPos(pchar->pEntParent, vPosSource);

				// Adjust position by oriented offsets
				EntGetOffsetPositionByOrientedOffsets(e,
													  NULL,
													  pchar->pEntParent, 
													  NULL,
													  qRotSource, 
													  pParamsMod->vecParam, 
													  false,
													  false,
													  false,
													  false,
													  vPosSource, 
													  vPosTarget);

				// Jitter and set
				vPosTarget[0] += 0.01f * randomF32();
				vPosTarget[2] += 0.01f * randomF32();
				entSetPos(e, vPosTarget, true, "EntCreateVanityAttribMod");

				// Set mod info
				pmod->erCreated = entGetRef(e);

				// Forcibly set no-coll
				mmNoCollHandleCreateFG(e->mm.movement, &e->mm.mnchVanity, __FILE__, __LINE__);

				// Flag it as a vanity pet
				entSetCodeFlagBits(e, ENTITYFLAG_VANITYPET);

				// Forcefully expire any other EntCreateVanity mods
				for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
				{
					if(pchar->modArray.ppMods[i]->pDef->offAttrib==kAttribType_EntCreateVanity
						&& pchar->modArray.ppMods[i]!=pmod)
					{
						character_ModExpireReason(pchar, pchar->modArray.ppMods[i], kModExpirationReason_Unset);
						entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
					}
				}
			}
		}
		else if(pmod->erCreated)
		{
			if(!entFromEntityRef(iPartitionIdx, pmod->erCreated))
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessFaction(AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		FactionParams *pParams = (FactionParams*)(pmod->pDef->pParams);
		if(pParams)
		{
			Entity *e = pchar->pEntParent;
			if(e)
			{
				if(!IS_HANDLE_ACTIVE(e->hPowerFactionOverride))
				{
					gslEntity_SetFactionOverrideByHandle(e, kFactionOverrideType_POWERS, REF_HANDLEPTR(pParams->hFaction));
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessFlag(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		FlagParams *pParams = (FlagParams*)(pmod->pDef->pParams);
		if(pParams)
		{
			Entity *e = pchar->pEntParent;
			if(e)
			{
				if(pParams->eFlags & kFlagAttributeFlags_Untargetable)
				{
					entSetDataFlagBits(e,ENTITYFLAG_UNTARGETABLE);
				}
				if(pParams->eFlags & kFlagAttributeFlags_Unkillable)
				{
					pchar->bUnkillable = true;
					entity_SetDirtyBit(e, parse_Character, pchar, false);
				}
				if(pParams->eFlags & kFlagAttributeFlags_Unselectable)
				{
					entSetDataFlagBits(e,ENTITYFLAG_UNSELECTABLE);
					
				}
				
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

typedef struct GrantPowerStruct
{
	int iPartitionIdx;
	EntityRef erTarget;
	AttribMod *pmod;
	U32 uiApplyID;
	AttribModDef *pmoddef;
} GrantPowerStruct;

static int GrantPowerCallback(Power *ppow, GrantPowerStruct *pStruct)
{
	bool bSuccess = false;

	if(ppow && pStruct)
	{
		Entity *e = entFromEntityRef(pStruct->iPartitionIdx,pStruct->erTarget);
		if(e && e->pChar)
		{
			int i;
			Character *pchar = e->pChar;
			for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
			{
				AttribMod *pmod = pchar->modArray.ppMods[i];
				if(pmod==pStruct->pmod)
				{
					// We found the supposed AttribMod that started this whole process,
					//  but if the transaction was slow, it may have actually been destroyed
					//  and then reallocated as some entirely other AttribMod.  So we
					//  do a careful check to make sure this is the same guy.
					if(verify(pmod->pDef==pStruct->pmoddef
								&& pmod->uiApplyID==pStruct->uiApplyID
								&& !eaSize(&pmod->ppPowersCreated)))
					{
						eaPush(&pmod->ppPowersCreated,ppow);
						eaIndexedEnable(&pchar->modArray.ppPowers,parse_Power);
						eaPush(&pchar->modArray.ppPowers,ppow);
#ifdef GAMESERVER
						character_SetPowerHue(pchar,ppow->uiID,pmod->fHue);
#endif
						bSuccess = true;
						pchar->bResetPowersArray = true;
					}
					break;
				}
			}
		}
	}

	if(pStruct)
	{
		free(pStruct);
	}

	return bSuccess;
}

static void ModProcessGrantPower(int iPartitionIdx, AttribMod *pmod, Character *pchar, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	{
		GrantPowerParams *pParams = (GrantPowerParams*)(pmod->pDef->pParams);
		if(pParams && !eaSize(&pmod->ppPowersCreated))
		{
			PowerDef *pdefPower = GET_REF(pParams->hDef);
			if(pdefPower)
			{
#ifdef GAMESERVER
				if(pmod->pDef->pExprDuration || pmod->pDef->eType&kModType_Duration)
				{
					GrantPowerStruct *pStruct = malloc(sizeof(GrantPowerStruct));
					pStruct->iPartitionIdx = iPartitionIdx;
					pStruct->erTarget = entGetRef(pchar->pEntParent);
					pStruct->pmod = pmod;
					pStruct->uiApplyID = pmod->uiApplyID;
					pStruct->pmoddef = pmod->pDef;
					character_AddPowerTemporary(pchar,pdefPower,GrantPowerCallback,pStruct);
				}
				else
				{
					if(entGetType(pchar->pEntParent)==GLOBALTYPE_ENTITYCRITTER)
					{
						character_AddPowerPersonal(iPartitionIdx,pchar,pdefPower,0,true, pExtract);
					}
					else
					{
						Errorf("Power %s is attempting to permanently grant Power %s to a non-Critter Entity",pmod->pDef->pPowerDef->pchName,pdefPower->pchName);
					}
				}
#endif
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

// Set the swinging fx
static void ModProcessSwinging(AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		AttribModDef *pDef = pmod->pDef;

		// reset to no swing fx (default)
		if(pDef)
		{
			SwingingParams *pParams = (SwingingParams*)(pDef->pParams);
			if(pParams)
			{
				const char *pcFxInfoName = REF_STRING_FROM_HANDLE(pParams->hSwingingFx);
				if(pcFxInfoName != pchar->pcSwingingFX)
				{
					pchar->pcSwingingFX = pcFxInfoName;
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif

}

static void ModProcessGrantReward(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
#ifdef GAMESERVER
		AttribModDef *pdef = pmod->pDef;
		GrantRewardParams *pParams = (GrantRewardParams*)(pdef->pParams);

		if(pParams)
		{
			RewardTable *pRewardTable = GET_REF(pParams->hRewardTable);
			ItemChangeReason reason = {0};
			inv_FillItemChangeReason(&reason, pchar->pEntParent, "Powers:GrantRewardMod", NULL);
			reward_PowerExec(pchar->pEntParent,pRewardTable,pmod->fMagnitude, 1.f, 0, &reason);
		}
#endif
	}
	PERFINFO_AUTO_STOP();

}

static void ModProcessInterrupt(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		if(pmod->fMagnitude>0 && pchar->pPowActCurrent)
		{
			Power *ppow = character_ActGetPower(pchar,pchar->pPowActCurrent);
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			InterruptParams *pParams = (InterruptParams*)pmod->pDef->pParams;
			if(pdef && pParams)
			{
				// Forced cancel, then set recharge directly if it worked but resulted in less recharge than we specified
				F32 fRecharge = pParams->bRechargePercent ? pmod->fMagnitude * pdef->fTimeRecharge : pmod->fMagnitude;
				if(fRecharge > 0)
				{
					U8 uchAct = character_ActCurrentCancelReason(iPartitionIdx,pchar,true,false,true,kAttribType_Interrupt);
					if(uchAct)
					{
						F32 fRechargeOld = power_GetRecharge(ppow);
						if(fRechargeOld < fRecharge)
						character_PowerSetRecharge(iPartitionIdx,pchar,ppow->uiID,fRecharge);
				}
			}
		}
		}

		character_ActInterrupt(iPartitionIdx, pchar, kPowerInterruption_InterruptAttrib);
		// Cancel everything
		character_ActAllCancelReason(iPartitionIdx,pchar,false,kAttribType_Interrupt);
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessKill(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		KillParams *pParams = (KillParams*)(pmod->pDef->pParams);
		KillType eKillType = pParams ? pParams->eKillType : kKillType_Silent;
		switch(eKillType)
		{
			case kKillType_Irresponsible:
			{
				if(pchar->erRingoutCredit)
				{
					damageTracker_AddTick(iPartitionIdx,
						pchar,
						pchar->erRingoutCredit,
						pchar->erRingoutCredit,
						pchar->pEntParent->myRef,
						pchar->pattrBasic->fHitPoints,
						pchar->pattrBasic->fHitPoints,
						kAttribType_HitPoints,
						pmod->uiApplyID,
						pmod->pDef->pPowerDef,
						pmod->pDef->uiDefIdx,
						NULL,
						0);
				}
				pchar->bKill = true;
			}
			break;

			case kKillType_Responsible:
			{
				//Add the damage to the tracker
				damageTracker_AddTick(iPartitionIdx,
					pchar,
					pmod->erOwner,
					pmod->erSource,
					pchar->pEntParent->myRef,
					pchar->pattrBasic->fHitPoints,
					pchar->pattrBasic->fHitPoints,
					kAttribType_HitPoints,
					pmod->uiApplyID,
					pmod->pDef->pPowerDef,
					pmod->pDef->uiDefIdx,
					NULL,
					0);
				
				// Then set the kill flag
				pchar->bKill = true;
			}
			break;

			case kKillType_Silent:
			{
				if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
				{
					Errorf("%s attempted to Silent Kill a Player",pmod->pDef->pPowerDef->pchName);
				}
				else
				{
					gslQueueEntityDestroy(pchar->pEntParent);
				}
			}
			break;

			default:
			{
				Errorf("%s attempted to kill with an unknown type",pmod->pDef->pPowerDef->pchName);
			}
			break;
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessKillTrigger(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		KillTriggerParams *pParams = (KillTriggerParams*)(pmod->pDef->pParams);
		// Cleanup if out of charges
		if(pParams && pParams->bMagnitudeIsCharges && pmod->fMagnitude < 1)
			character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
	}
	PERFINFO_AUTO_STOP();
}

static F32 ModKnockGetProneTime(int iPartitionIdx, AttribMod *pmod, F32 fProneTimer, Entity *pSource, Character *pTargetChar)
{
	if (g_CombatConfig.specialAttribModifiers.eKnockProneModifier)
	{
		F32 fProneResistTrue = 1.f, fProneResist = 1.f, fProneImmune = 0.f; 
		AttribType eProneAttrib = g_CombatConfig.specialAttribModifiers.eKnockProneModifier;
		F32 fProneBasic = 0.f;

		if (pSource && pSource->pChar)
		{
			F32 fProneStr = character_GetStrengthGeneric(iPartitionIdx, pSource->pChar, eProneAttrib, NULL);
			Power *pOriginatingPower = character_FindPowerByID(pSource->pChar, pmod->uiPowerID);
									
			fProneBasic = character_PowerBasicAttrib(iPartitionIdx, pSource->pChar, pOriginatingPower, eProneAttrib, entGetRef(pTargetChar->pEntParent));

			fProneTimer *= fProneStr;
		}

		fProneResist = character_GetResistGeneric(iPartitionIdx, pTargetChar, eProneAttrib, &fProneResistTrue, &fProneImmune);

		fProneTimer = ModCalculateEffectiveMagnitude(fProneTimer, fProneResistTrue, fProneResist, 0.f, fProneImmune, 0.f) + fProneBasic;
		if (fProneTimer < 0.f)
			fProneTimer = 0.f;
	}

	return fProneTimer;
}

static void ModProcessKnockTo(int iPartitionIdx, AttribMod *pmod, Character *pchar, F32 fResistTrue, F32 fResist, F32 fImmune, F32 fAvoid)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PERFINFO_AUTO_START_FUNC();
	if(fImmune<=0 && !pchar->bUnstoppable && !pchar->bUsingDoor && pmod->pDef->offAspect==kAttribAspect_BasicAbs)
	{
		Vec3 vecCurrent,vecTarget;
		F32 fDist;
		Entity *pentSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
		KnockToParams *pParams = (KnockToParams*)(pmod->pDef->pParams);
		
		if(pentSource)
		{
			entGetPos(pentSource,vecTarget);
			VecOffsetInFacingDirection(vecTarget,pentSource,NULL,pParams->fDistanceFront,pParams->fDistanceRight,pParams->fDistanceAbove,false,false);
			pchar->erRingoutCredit = entGetRef(pentSource);
		}
		else
		{
			copyVec3(pmod->vecSource,vecTarget);
			if(pParams)
			{
				vecTarget[0] += pParams->fDistanceFront;
				vecTarget[1] += pParams->fDistanceAbove;
				vecTarget[2] += pParams->fDistanceRight;
			}
		}

		entGetPos(pchar->pEntParent,vecCurrent);
		fDist = distance3Squared(vecCurrent,vecTarget);
		
		// Hard limit on range for safety
		if(fDist<40000.f)
		{
			F32 fHitChance = (fResistTrue/fResist)/(1+fAvoid);
			U32 uiTimeKnock = pmTimestamp(pmod->fPredictionOffset);
			if(fHitChance >= 1.f || fHitChance > randomPositiveF32())
			{
				F32 fProneTimer = ModKnockGetProneTime(iPartitionIdx, pmod, pParams->fTimer, pentSource, pchar);

				pmKnockToStart(pchar->pEntParent,vecTarget,pmTimestamp(0), pParams->bInstantFacePlant, 
								!pParams->bOmitProne, fProneTimer, pParams->bIgnoreTravelTime);

				character_ActInterrupt(iPartitionIdx,pchar, kPowerInterruption_Knock);
#ifdef GAMESERVER
				gslLogoff_Cancel(pchar->pEntParent, kLogoffCancel_CombatDamage);
#endif;
				// Cancel everything
				character_ActAllCancelReason(iPartitionIdx,pchar,false, kAttribType_KnockTo);

				// Add the combat event for being Knocked
				character_CombatEventTrack(pchar, kCombatEvent_KnockIn);
			}
			else if(!ISZEROVEC3(pmod->vecSource))
			{
				Vec3 vecDir;
				subVec3(vecTarget,vecCurrent,vecDir);
				pmPushStart(pchar->pEntParent,vecDir,sqrtf(fDist),uiTimeKnock);

				// Add the combat event for being Knocked
				character_CombatEventTrack(pchar, kCombatEvent_KnockIn);
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessMissionEvent(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		AttribModDef *pdef = pmod->pDef;
		MissionEventParams *pParams = (MissionEventParams*)(pdef->pParams);
		Entity *pentSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
		PowerDef *ppowdef = pdef->pPowerDef;
		const char *pchEventName = pParams ? pParams->pchEventName : NULL;
#ifdef GAMESERVER
		eventsend_RecordPowerAttribModApplied(pentSource,pchar->pEntParent,ppowdef,pchEventName);
#endif
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessNotify(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		AttribModDef *pdef = pmod->pDef;
		MissionEventParams *pParams = (MissionEventParams*)(pdef->pParams);
		Entity *pentSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
		PowerDef *ppowdef = pdef->pPowerDef;
#ifdef GAMESERVER
		ClientCmd_NotifySend(pchar->pEntParent, kNotifyType_FromPower, langTranslateMessageKey(entGetLanguage(pchar->pEntParent), ((NotifyParams*)pdef->pParams)->pchMessageKey) ,ppowdef->pchName, ppowdef->pchIconName);
#endif
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessPlacate(int iPartitionIdx, AttribMod *pmod, AttribModDef *pmoddef, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	if(pmod->erSource && pmod->pDef->offAspect==kAttribAspect_BasicAbs)
	{
		F32 fResist = 1.0f;
		F32 fImmune = 0.0f;
		F32 fAvoid = 0.f;
		PlacateParams *pParams  = (PlacateParams*)(pmod->pDef->pParams);

		character_ModGetMitigators(iPartitionIdx, pmod, pmoddef, pchar, NULL, &fResist, &fImmune, &fAvoid, NULL);

		if(fImmune<=0)
		{
			if(pParams && pParams->bStealthPlacater)
				ea32PushUnique(&pchar->perHidden,pmod->erSource);

			ea32PushUnique(&pchar->perUntargetable,pmod->erSource);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessPickedUp(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		Entity *pentSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
		if(IS_HANDLE_ACTIVE(pchar->pEntParent->hCreatorNode)
			&& !pchar->erHeldBy
			&& !entCheckFlag(pchar->pEntParent,ENTITYFLAG_DESTROY)
			&& entIsAlive(pchar->pEntParent)
			&& pentSource
			&& pentSource->pChar)
		{
			if(pmod->fDurationOriginal<=0)
			{
				// PickedUp instantly, means the entity goes away, and we start carrying bits/fx
				if(!pentSource->pChar->erHeld && !IS_HANDLE_ACTIVE(pentSource->pChar->hHeldNode))
				{
					// Wasn't already holding anything, so hold this, and destroy the entity
					WorldInteractionNode *pnode;
					
					COPY_HANDLE(pentSource->pChar->hHeldNode, pchar->pEntParent->hCreatorNode);
					entity_SetDirtyBit(pentSource, parse_Character, pentSource->pChar, false);
					pentSource->pChar->fHeldHealth = 1.f; // Default health to 1

					// Get mass, real health if we can
					pnode = GET_REF(pchar->pEntParent->hCreatorNode);
					if(pnode)
					{
						CharacterClass *pclass = im_GetCharacterClass(pnode);
						int iLevel = im_GetLevel(pnode);
						pentSource->pChar->fHeldMass = im_GetMass(pnode);
						if(pclass)
						{
							F32 fHealth = class_GetAttribBasic(pclass,kAttribType_HitPointsMax,iLevel-1);
							if(fHealth > 0)
							{
								pentSource->pChar->fHeldHealth = fHealth;
							}
						}
					}

					gslQueueEntityDestroy(pchar->pEntParent);
					if(pmod->pDef)
					{
						U32 uiID,uiSubID;
						mod_AnimFXID(pmod,&uiID,&uiSubID);
						character_AnimFXCarryOn(iPartitionIdx,pentSource->pChar,pmod->pDef,uiID);
					}
				}
			}
			else
			{
				// PickedUp for a time, means the entity stays around
				if(!pentSource->pChar->erHeld && !IS_HANDLE_ACTIVE(pentSource->pChar->hHeldNode))
				{
					// Wasn't already holding anything, so hold this
					pentSource->pChar->erHeld = entGetRef(pchar->pEntParent);
					entity_SetDirtyBit(pentSource, parse_Character, pentSource->pChar, false);
					pchar->erHeldBy = pmod->erSource;
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				}
			}

			im_DestroyChainedDestructibles( pchar->pEntParent, NULL, entGetRef(pentSource) ); 

			// In either case, send an event that the object was picked up
			eventsend_RecordPickupObject(pentSource, pchar->pEntParent);
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessCombatAdvantage(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	CombatAdvantageParams *pCombatAdvantageParams = (CombatAdvantageParams*)(pmod->pDef->pParams);
	if (IS_BASIC_ASPECT(pmod->pDef->offAspect))
	{
		if (pCombatAdvantageParams->eAdvantageType == kCombatAdvantageApplyType_Advantage)
		{
			if (!pmod->erPersonal)
			{	// not a personal mod, so you get advantage to everyone
				gslCombatAdvantage_AddAdvantageToEveryone(pchar, pmod->uiApplyID);
			}
			else
			{	// the given pchar has advantage over the given entity
				Entity *pEnt = entFromEntityRef(iPartitionIdx, pmod->erPersonal);
				if (pEnt && pEnt->pChar)
				{
					gslCombatAdvantage_AddAdvantagedCharacter(pEnt->pChar, entGetRef(pchar->pEntParent), pmod->uiApplyID);
				}
			}
		}
		else
		{
			if (!pmod->erPersonal)
			{	// not a personal mod, so you get advantage to everyone
				gslCombatAdvantage_AddDisadvantageToEveryone(pchar, pmod->uiApplyID);
			}
			else
			{	// the personal entity has advantage over me
				gslCombatAdvantage_AddAdvantagedCharacter(pchar, pmod->erPersonal, pmod->uiApplyID);
			}
		}
	}
	
#endif
}

static void ModProcessConstantForce(int iPartitionIdx, 
									AttribMod *pmod, 
									AttribModDef *pmoddef,
									Character *pchar)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	F32 fResistTrue = 1.f, fResist = 1.f, fAvoid = 0, fImmune = 0;
	F32 fHitChance = 1.f;
	F32 fMag = 0.f;
	
	if (!IS_BASIC_ASPECT(pmod->pDef->offAspect) || pmod->bActive)
		return;

	PERFINFO_AUTO_START("Repel",1);

	pmod->bActive = true;
		
	character_ModGetMitigators(iPartitionIdx, pmod, pmoddef, pchar, &fResistTrue, &fResist, &fImmune, &fAvoid, NULL);
	
	fMag = ModGetEffectiveMagnitudeEx(iPartitionIdx, pmod, pmoddef, pchar, fResistTrue, fResist, fAvoid, fImmune, 0.f);
	if (fResist != 0.f)
	{
		fHitChance = fResistTrue/fResist;
	}

	if(	fImmune<=0 && 
		!pchar->bUnstoppable && 
		!pchar->bUsingDoor && 
		fMag != 0 && 
		(fHitChance >= 1.f || fHitChance > randomPositiveF32()))
	{
		Vec3 vForce = {0};
		F32 fYawOffset = 0.f;
		U32 uiTimeRepel = pmTimestamp(pmod->fPredictionOffset);
		U32 uiTimeRepelStop = pmTimestampFrom(uiTimeRepel, pmod->fDurationOriginal);
		Entity *eSource = entFromEntityRef(iPartitionIdx, pmod->erSource);
		ConstantForceParams *pParams = (ConstantForceParams*)(pmod->pDef->pParams);
			
		if (pParams->pExprYawOffset)
		{
			fYawOffset = combateval_EvalNew(iPartitionIdx,pParams->pExprYawOffset,kCombatEvalContext_Apply,NULL);
			fYawOffset = RAD(fYawOffset);
		}

		if (!pParams->bModOwnerRelative || !eSource)
		{
			Vec3 vTargetPos;
			
			entGetCombatPosDir(pchar->pEntParent, NULL, vTargetPos, NULL);
			subVec3(vTargetPos, pmod->vecSource, vForce);
			normalVec3(vForce);
			scaleVec3(vForce, fMag, vForce);
			rotateXZ(fYawOffset, vForce, vForce + 2);

			pmConstantForceStart(	pchar->pEntParent, 
									pmod->uiActIDServer, 
									uiTimeRepel, 
									uiTimeRepelStop, 
									vForce);
		}
		else
		{
			pmConstantForceStartWithRepeller(	pchar->pEntParent, 
												pmod->uiActIDServer, 
												uiTimeRepel, 
												uiTimeRepelStop, 
												entGetRef(eSource), 
												fYawOffset, 
												fMag);
		}

#ifdef GAMESERVER
		
		if(eSource && pmod->fPredictionOffset)
		{
			if (!pParams->bModOwnerRelative)
			{
				ClientCmd_PowersPredictConstantForce(	eSource,
														entGetRef(pchar->pEntParent),
														pmod->uiActIDServer,
														uiTimeRepel,
														uiTimeRepelStop,
														vForce);
			}
			else
			{
				ClientCmd_PowersPredictConstantForceWithRepeller(	eSource,
																	entGetRef(pchar->pEntParent),
																	pmod->uiActIDServer,
																	uiTimeRepel,
																	uiTimeRepelStop,
																	entGetRef(eSource),
																	fYawOffset,
																	fMag);
			}
			
		}
		
#endif
		// Add the combat event for being Knocked
		character_CombatEventTrack(pchar, kCombatEvent_KnockIn);
		if(eSource)
			pchar->erRingoutCredit = entGetRef(eSource);

		character_CombatEventTrack(pchar, kCombatEvent_AttemptRepelIn);
		if(eSource && eSource->pChar)
			character_CombatEventTrack(eSource->pChar, kCombatEvent_AttemptRepelOut);
	}
	else if(fHitChance >= 1.f || fHitChance > randomPositiveF32())
	{
		Entity *eSource = entFromEntityRef(iPartitionIdx, pmod->erSource);

		character_CombatEventTrack(pchar, kCombatEvent_AttemptRepelIn);
		if(eSource && eSource->pChar)
			character_CombatEventTrack(eSource->pChar, kCombatEvent_AttemptRepelOut);
	}


	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessPowerMode(int iPartitionIdx, AttribMod *pmod, Character *pchar, int *piModes)
{
	PERFINFO_AUTO_START_FUNC();
	{		
		PowerModeParams *pParams = (PowerModeParams*)(pmod->pDef->pParams);
		if(pmod->pDef->offAspect==kAttribAspect_BasicAbs && !pmod->pDef->bPersonal && pParams && pParams->iPowerMode>kPowerMode_LAST_CODE_SET) // Personal modes are queried directly
		{
			eaiPushUnique(&pchar->piPowerModes,pParams->iPowerMode);
			
			if(pParams->bMissionEvent && -1==eaiFind(&piModes,pParams->iPowerMode))
			{
				Entity *pentSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
				PowerDef *ppowdef = pmod->pDef->pPowerDef;
				const char *pchEventName = StaticDefineIntRevLookup(PowerModeEnum,pParams->iPowerMode);
#ifdef GAMESERVER
				eventsend_RecordPowerAttribModApplied(pentSource,pchar->pEntParent,ppowdef,pchEventName);
#endif
			}

			if(pParams->bCombatEvent)
			{
				character_CombatEventTrack(pchar,kCombatEvent_PowerMode);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}


static void ModProcessPowerRecharge(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		PowerRechargeParams *pParams = (PowerRechargeParams*)(pmod->pDef->pParams);
		if(pParams)
		{
			if (pParams->bAffectsGlobalCooldown)
			{
				if (g_CombatConfig.fCooldownGlobal > 0.f)
				{
					F32 fCooldownMod = pParams->bPercent ? pmod->fMagnitude * g_CombatConfig.fCooldownGlobal : pmod->fMagnitude;
					F32 fCooldownCurrent = pchar->fCooldownGlobalTimer;

					// Exit if there would be no changes in cooldown
					if ((pParams->eApply == kPowerRechargeApply_SetIfLarger && fCooldownMod <= fCooldownCurrent) ||
						(pParams->eApply == kPowerRechargeApply_SetIfSmaller && fCooldownMod >= fCooldownCurrent))
					{
						PERFINFO_AUTO_STOP();
						return;
					}

					// Add the current cooldown time for the Add apply method
					if(pParams->eApply == kPowerRechargeApply_Add)
					{
						fCooldownMod += fCooldownCurrent;
					}

					// Set the final cooldown
					pchar->fCooldownGlobalTimer = MAX(0.f, fCooldownMod);
#ifdef GAMESERVER
					// Let the client know about the change
					ClientCmd_SetCooldownGlobalClient(pchar->pEntParent, pchar->fCooldownGlobalTimer);
#endif
				}
			}
			else
			{
				int i;
				for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
				{
					Power *ppow = pchar->ppPowers[i];
					PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
					if(ppow && pdef)
					{
						F32 fRechargeMod = pParams->bPercent ? pmod->fMagnitude * pdef->fTimeRecharge : pmod->fMagnitude;
						F32 fRechargeCurrent = ppow->fTimeRecharge;

						// Skip if the type won't actually change the recharge time
						if((pParams->eApply==kPowerRechargeApply_SetIfLarger && fRechargeMod <= fRechargeCurrent)
							|| (pParams->eApply==kPowerRechargeApply_SetIfSmaller && fRechargeMod >= fRechargeCurrent))
							continue;

						// Add the current recharge time for the Add apply method
						if(pParams->eApply==kPowerRechargeApply_Add)
						{
							fRechargeMod += fRechargeCurrent;
						}

						// If the resulting recharge time is zero, and it's already at zero, skip it
						if(fRechargeMod <= 0 && fRechargeCurrent <= 0 && 
							(!pdef->bChargesSetCooldownWhenEmpty || power_GetChargesUsed(ppow) == 0))
							continue;

						// Do the check to see if this AttribMod actually affects the Power in question
						if(moddef_AffectsModOrPowerChk(iPartitionIdx,pmod->pDef,pchar,pmod,NULL,NULL,pdef))
						{
							character_PowerSetRecharge(iPartitionIdx,pchar,ppow->uiID,fRechargeMod);
						}
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessPowerShield(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		PowerShieldParams *pParams = (PowerShieldParams*)pmod->pDef->pParams;
		if(pParams->fRatio > pchar->fPowerShieldRatio)
		{
			pchar->fPowerShieldRatio = pParams->fRatio;
		}
	}
	PERFINFO_AUTO_STOP();
}

#ifdef GAMESERVER
static void ModProcessProjectileCreate(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		ProjectileCreateParams *pProjParams = (ProjectileCreateParams*)(pmod->pDef->pParams);
		AttribModParams *pParamsMod = pmod->pParams;
		if(pProjParams && pParamsMod && !pmod->erCreated)
		{
			ProjCreateParams projCreateParams = {0};
			Entity *pProjectileEnt = NULL;
			Vec3 vStartPos;
			Vec3 vDirection= {0};
			Vec3 vTrajectoryDir = {0};
			Quat rot = {0};
			Vec3 vCreateOffset = {0};
			bool bSecondaryCreationPos = true;
						
			if (pmod->pDef->pPowerDef->fRangeSecondary > 0.f && !ISZEROVEC3(pParamsMod->vecParam))
			{
				bSecondaryCreationPos = true;
				copyVec3(pParamsMod->vecParam, vStartPos);
			}
			else
			{
				entGetCombatPosDir(pchar->pEntParent, NULL, vStartPos, NULL);
			}

			if (pProjParams->fCreateDistanceForward || pProjParams->fCreateDistanceRight || pProjParams->fCreateDistanceUp)
			{
				Vec3 vEntPos;
				setVec3(vCreateOffset, 
						pProjParams->fCreateDistanceRight, 
						pProjParams->fCreateDistanceUp, 
						pProjParams->fCreateDistanceForward);
				
				entGetPos(pchar->pEntParent, vEntPos);
				vCreateOffset[1] += vStartPos[1] - vEntPos[1];

				projCreateParams.pvCreateOffset = vCreateOffset;
				
				VecOffsetInFacingDirection(vStartPos, pchar->pEntParent, NULL,
											pProjParams->fCreateDistanceForward,
											pProjParams->fCreateDistanceRight,
											pProjParams->fCreateDistanceUp,
											false,
											false);

			}
			
			if (pProjParams->fDirectionPitchOffset || pProjParams->fDirectionYawOffset)
			{
				F32 fYaw, fPitch;
				subVec3(pParamsMod->vecTarget, vStartPos, vDirection);
				getVec3YP(vDirection, &fYaw, &fPitch);

				fPitch = -fPitch;
				fPitch += HALFPI;

				if (pProjParams->fDirectionYawOffset)
				{
					fYaw += RAD(pProjParams->fDirectionYawOffset);
				}
				if (pProjParams->fDirectionPitchOffset)
				{
					fPitch -= RAD(pProjParams->fDirectionPitchOffset);
				}

				sphericalCoordsToVec3(vDirection, fYaw, fPitch, 1.f);
				projCreateParams.pvDirection = vDirection;
			}
			else
			{
				projCreateParams.pvTargetPos = pParamsMod->vecTarget;
			}

			
			if (!bSecondaryCreationPos && pProjParams->bUseAimingTrajectory && !ISZEROVEC3(pParamsMod->vecParam))
			{
				projCreateParams.pvTrajectorySourcePos = pParamsMod->vecParam;
				subVec3(pParamsMod->vecTarget, pParamsMod->vecParam, vTrajectoryDir);
				projCreateParams.pvTrajectorySourceDir = vTrajectoryDir;
			}

			projCreateParams.pDef = GET_REF(pProjParams->hProjectileDef);
			projCreateParams.pOwner = pchar->pEntParent;
			projCreateParams.pvStartPos = vStartPos;
			
			entGetRot(pchar->pEntParent, rot);
			projCreateParams.pqRot = rot;
			projCreateParams.bSnapToGround = pProjParams->bSnapToGroundOnCreate;
			projCreateParams.fHue = pmod->fHue;
									
			pProjectileEnt = gslProjectile_CreateProjectile(iPartitionIdx, &projCreateParams);
			if (pProjectileEnt)
			{
				pmod->erCreated = entGetRef(pProjectileEnt);
				if(pProjectileEnt->pChar && pmod->ppApplyStrengths) // always assume we're copying the apply strengths
				{
					eaCopyStructs(&pmod->ppApplyStrengths,&pProjectileEnt->pChar->ppApplyStrengths,parse_PowerApplyStrength);
				}

				// apply any enhancements that the creator might have for me.
				ModEntityCreateAttachEnhancements(iPartitionIdx, pchar->pEntParent, pProjectileEnt);
			}
		}

	}
	PERFINFO_AUTO_STOP();
}


static void ModProcessPVPFlag(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	Entity *e = entFromEntityRef(iPartitionIdx, pmod->erOwner);
	PVPFlagParams *pParams;

	if(!e)
	{
		character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
		return;
	}

	if(pchar->pvpFlag || pchar->pvpTeamDuelFlag)
		return;

	pParams = (PVPFlagParams*)pmod->pDef->pParams;
	if(pParams->pchGroupName)
	{
		gslPVPJoinGroupEnt(pchar->pEntParent, entFromEntityRef(iPartitionIdx, pmod->erOwner), pParams);
	}
	else if(pmod->erOwner==entGetRef(pchar->pEntParent))
	{
		// New infection
		gslPVPInfectEnt(pchar->pEntParent, pParams->fRadius ? pParams->fRadius : 200, pParams->bAllowHeal, pParams->bAllowExternCombat);
	}
	else
	{
		// Gain pvp flag of pmod's owner
		gslPVPInfect(pchar->pEntParent, entFromEntityRef(iPartitionIdx, pmod->erOwner), 1);
	}
}

static void ModProcessPVPSpecialAction(AttribMod *pmod, Character *pchar)
{
	PVPSpecialActionParams *pParams = (PVPSpecialActionParams *)pmod->pDef->pParams;

	if(pParams->eAction == kPVPSpecialAction_ThrowFlag)
		gslPVPGame_ThrowFlag(NULL,pchar->pEntParent,pmod->pParams->vecParam);
	else if(pParams->eAction == kPVPSpecialAction_DropFlag)
		gslPVPGame_DropFlag(NULL,pchar->pEntParent);
}
#endif

static void ModProcessRemovePower(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		AttribModDef *pdef = pmod->pDef;
		RemovePowerParams *pParams = (RemovePowerParams*)(pdef->pParams);
		if(pParams)
		{
			PowerDef *pdefPower = GET_REF(pParams->hDef);
			if(pdefPower)
			{
				Power *ppow = character_FindPowerByDefPersonal(pchar,pdefPower);
				if(ppow)
				{
#ifdef GAMESERVER
					if(entGetType(pchar->pEntParent)==GLOBALTYPE_ENTITYCRITTER)
					{
						character_RemovePowerPersonal(pchar,ppow->uiID);
					}
					else
					{
						Errorf("Power %s is attempting to permanently remove Power %s from a non-Critter Entity",pdef->pPowerDef->pchName,pdefPower->pchName);
					}
#endif
				}
				else
				{
					// If we didn't find the power in your personal powers, search for Grant Power mods with the same def
					S32 i = 0;
					for(i=eaSize(&pchar->modArray.ppMods)-1; i>=0; i--)
					{
						AttribMod *pGrantPowerMod = pchar->modArray.ppMods[i];
						AttribModDef *pGrantPowerModDef = mod_GetDef(pGrantPowerMod);
						if(pGrantPowerModDef && pGrantPowerModDef->offAttrib == kAttribType_GrantPower)
						{
							GrantPowerParams *pGrantPowerParams = (GrantPowerParams*)pGrantPowerMod->pDef->pParams;
							PowerDef *pdefGrantedPower = GET_REF(pGrantPowerParams->hDef);

							//If the power granted by this mod is the same as the def attempting to be removed
							if(pdefGrantedPower == pdefPower)
							{
								// The expire reason isn't quite right but it's close.
								// The attrib mod being processed removed the correct grant power mod we just found
								character_ModExpireReason(pchar, pGrantPowerMod, kModExpirationReason_AttribModExpire);
								break;
							}
						}
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessRide(int iPartitionIdx, AttribMod *pmod, Character *pchar, GameAccountDataExtract *pExtract)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		Entity *pentSource = entFromEntityRef(iPartitionIdx, pmod->erSource);
		if(pentSource && pentSource->pChar && !pmod->erCreated)
		{
			gslEntRideCritter(pentSource, pchar->pEntParent, pExtract);
			pmod->erCreated = pmod->erSource;
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessShield(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	if(pmod->pFragility && !pmod->bIgnored)
	{
		if(pmod->pFragility->fHealth > 0
			|| pmod->pDef->pFragility->bUnkillable)
		{
			eaPush(&pchar->ppModsShield,pmod);
		}
		else
		{
			character_ModExpireReason(pchar, pmod, kModExpirationReason_Unset);
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessSubtargetSet(AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		SubtargetSetParams *pParams = (SubtargetSetParams*)(pmod->pDef->pParams);
		if(pParams && !pchar->pSubtarget)
		{
			PowerSubtargetCategory *pcat = GET_REF(pParams->hCategory);
			if(pcat)
			{
				character_SetSubtargetCategory(pchar,pcat);
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

// gets the entity that we will basing our teleport position off of
Entity* ModTeleportGetTeleportMainEntity(	Character *pChar, 
											Entity *pTarget,
											AttribModDef *pTeleportAttribModDef)
{
	TeleportParams *pTeleportAttribParams = NULL;

	devassert(pTeleportAttribModDef->offAttrib == kAttribType_Teleport);

	if(!pTeleportAttribModDef || !pTeleportAttribModDef->pParams)
	{
		return NULL;
	}
		
	pTeleportAttribParams = (TeleportParams*)pTeleportAttribModDef->pParams;

	switch (pTeleportAttribParams->eTeleportTarget)
	{
		xcase kAttibModTeleportTarget_Self:
			return pChar ? pChar->pEntParent : NULL;
		
		xcase kAttibModTeleportTarget_Target:
			return pTarget;

		xcase kAttibModTeleportTarget_OwnedProjectile:
		{	// search for an attribMod that is a projectile 
			int iPartition;
			if (!pChar)
				return NULL;

			iPartition = entGetPartitionIdx(pChar->pEntParent);

			FOR_EACH_IN_EARRAY(pChar->modArray.ppMods, AttribMod, pAttribMod)
			{
				if (pAttribMod->erCreated)
				{
					AttribModDef *pDef = mod_GetDef(pAttribMod);

					if (pDef->offAttrib == kAttribType_ProjectileCreate)
					{
						Entity *pEnt = NULL;
						S32 i, iNumTags = eaiSize(&pTeleportAttribParams->piProjectileTags);
						bool bFound = false;
						if (iNumTags == 0)
							bFound = true;

						for (i = iNumTags - 1; i >= 0; --i)
						{
							if (eaiFind(&pDef->tags.piTags, pTeleportAttribParams->piProjectileTags[i]) >= 0)
							{
								bFound = true;
								break;
							}
						}

						pEnt = entFromEntityRef(iPartition, pAttribMod->erCreated);
						if (pEnt)
							return pEnt;
					}
				}
			}
			FOR_EACH_END
		}

		xcase kAttibModTeleportTarget_Expression:
		{
			if (pChar && pTeleportAttribParams->pTeleportTargetExpr)
			{
				Entity *pRetEnt = NULL;
				S32 iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);
				combateval_ContextReset(kCombatEvalContext_Teleport);
				combateval_ContextSetupTeleport(pChar, pTarget ? pTarget->pChar : NULL);
				pRetEnt = combateval_EvalReturnEntity(	iPartitionIdx,
														pTeleportAttribParams->pTeleportTargetExpr,
														kCombatEvalContext_Teleport,
														NULL);
				return pRetEnt;
			}
		}
	}
	
	
	

	return NULL;
}

S32 ModTeleportGetTeleportTargetTranslations(	Character *pChar, 
												Entity *eTarget, 
												AttribModDef *pTeleportAttribModDef,
												Vec3 vTeleportBasePosOut,
												Vec3 vTeleportPYROut)
{
#if GAMESERVER || GAMECLIENT
	// further target overriding for teleport
	Entity *pMainTeleportTarget = ModTeleportGetTeleportMainEntity(pChar, eTarget, pTeleportAttribModDef);
	
	if (pMainTeleportTarget)
	{
		Vec3 vecPY;
		Quat qRot;
		TeleportParams *pTeleportAttribParams = (TeleportParams*)pTeleportAttribModDef->pParams;
		
		if (IsServer() || !mmGetLatestServerPosFaceFG(pMainTeleportTarget->mm.movement, vTeleportBasePosOut, vecPY))
		{
			entGetPos(pMainTeleportTarget, vTeleportBasePosOut);
			entGetFacePY(pMainTeleportTarget, vecPY);
		}

		if (pMainTeleportTarget && 
			pMainTeleportTarget->pChar && 
			pChar != pMainTeleportTarget->pChar &&
			pMainTeleportTarget->pChar->bSpecialLargeMonster)
		{
			entOffsetPositionToCombatPos(pMainTeleportTarget, vTeleportBasePosOut);
		}

		switch (pTeleportAttribParams->eTeleportOffsetOrientation)
		{
			xcase kTeleportOffsetOrientation_TeleportTargetFacing:
				// we've already gotten the facing above
				entGetRot(pMainTeleportTarget, qRot);
				quatToPYR(qRot, vTeleportPYROut);
				copyVec2(vecPY, vTeleportPYROut);

			xcase kTeleportOffsetOrientation_CurrentFacing:
				// use the given character's current facing 
				entGetFacePY(pChar->pEntParent, vecPY);
				entGetRot(pChar->pEntParent, qRot);
				quatToPYR(qRot, vTeleportPYROut);
				copyVec2(vecPY, vTeleportPYROut);

			xcase kTeleportOffsetOrientation_TeleportTargetMovementRotation:
				entGetRot(pMainTeleportTarget, qRot);
				quatToPYR(qRot, vTeleportPYROut);

			xcase kTeleportOffsetOrientation_RelativeFromTeleportTarget:
			{
				// use the direction from the target to the character,
				// if the given character and the target are the same entity, just get our current facing
				if (pChar->pEntParent != pMainTeleportTarget)
				{
					Vec3 vCurPos;
					Vec3 vToTarget;

					entGetPos(pChar->pEntParent, vCurPos);
					subVec3(vCurPos, vTeleportBasePosOut, vToTarget);
					normalVec3(vToTarget);
					getVec3YP(vToTarget, &vTeleportPYROut[1], &vTeleportPYROut[0]);
					vTeleportPYROut[2] = 0.0f;
				}
				else
				{
					entGetRot(pMainTeleportTarget, qRot);
					quatToPYR(qRot, vTeleportPYROut);
					copyVec2(vecPY, vTeleportPYROut);
				}
			}
		}
		

		return true;
	}
#endif
	return false;
}



// Returns true if the target location is a valid place to teleport the entity
static S32 ModTeleportGetValidOffsetLocation(	Entity *pEnt, 
												const Vec3 vOffset,
												const Vec3 vBaseEntPYR, 
												const Vec3 vSourcePos,
												bool bOffsetUsesPitch,
												bool bOffsetUsesRoll,
												bool bDontAttemptGroundSnap,
												Vec3 vPosOut)
{
	const Capsule *pCapsule = EntGetWorldCollisionCapsule();

	S32 bValid = false;
	WorldColl *pWorldColl = worldGetActiveColl(entGetPartitionIdx(pEnt));

	copyVec3(vPosOut, vPosOut);

	if (vOffset && vBaseEntPYR && !ISZEROVEC3(vOffset) )
	{
		Quat qRot;
		
		VecOffsetInFacingDirection(vPosOut, pEnt, vBaseEntPYR, vOffset[0], vOffset[1], vOffset[2], bOffsetUsesPitch, bOffsetUsesRoll);
		unitQuat(qRot);

		// get a potentially valid position offset and then further check the position for teleport
		EntGetValidOffsetPosition(entGetPartitionIdx(pEnt), NULL, NULL, qRot, false, 0, vSourcePos, vPosOut);
		
	}

	if (!bDontAttemptGroundSnap) {
		vPosOut[1] = ModCapsuleCastDown(vPosOut, pEnt);
	}

	// Check and see if a standard capsule hits the world here
	if(!wcCapsuleCollideEx(	pWorldColl, 
							*pCapsule, 
							vPosOut, 
							vPosOut, 
							WC_QUERY_BITS_WORLD_ALL, NULL))
	{
		return true;
	}

	return false;
}

static void ModProcessTeleport(AttribMod *pmod, Character *pchar, F32 fResistTrue, F32 fResist, F32 fImmune)
{
#if GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	if (fImmune<=0 && !pchar->bUnstoppable && pmod->pDef->offAspect==kAttribAspect_BasicAbs)
	{
		F32 fHitChance = fResistTrue/fResist;
		if(pmod->pParams && (fHitChance >= 1.f || fHitChance > randomPositiveF32()))
		{
			Vec3 vTargetPos;
			bool bValidPos = false;
			TeleportParams *pTeleportAttribParams = (TeleportParams*)(pmod->pDef->pParams);
			
			copyVec3 (pmod->pParams->vecTarget, vTargetPos);
						

			// validate the position
			if (!vec3IsZero(pmod->pParams->vecOffset))
			{
				Vec3 vSourcePos;
				copyVec3(vTargetPos, vSourcePos);
				bValidPos = ModTeleportGetValidOffsetLocation(	pchar->pEntParent, 
																pmod->pParams->vecOffset, 
																pmod->pParams->vecParam,
																vSourcePos,
																pTeleportAttribParams->bOffsetUsesPitch,
																pTeleportAttribParams->bOffsetUsesRoll,
																pTeleportAttribParams->bDontAttemptGroundSnap,
																vTargetPos);
			}
			else
			{
				bValidPos = ModTeleportGetValidOffsetLocation(	pchar->pEntParent, 
																NULL, 
																NULL, 
																vTargetPos, 
																pTeleportAttribParams->bOffsetUsesPitch,
																pTeleportAttribParams->bOffsetUsesRoll,
																pTeleportAttribParams->bDontAttemptGroundSnap,
																vTargetPos);
			}
			

			if(bValidPos)
			{
				bool bHasRotation = true;
				F32 fYaw = 0.f;
				F32 fPitch = 0.f;
				Vec2 pyFace; 

				entGetFacePY(pchar->pEntParent, pyFace);

				// see if we handle rotation, and get the base yaw
				switch (pTeleportAttribParams->eFacingType)
				{
					xcase kTeleportFaceType_FaceTarget:
					{
						Vec3 vToTarget;
						subVec3(pmod->pParams->vecTarget, vTargetPos, vToTarget);
						getVec3YP(vToTarget, &fYaw, &fPitch);
					}
					xcase kTeleportFaceType_MatchTargetOrientation:
					{
						getVec3YP(pmod->pParams->vecParam, &fYaw, &fPitch);
					}
					xcase kTeleportFaceType_Current:
					{
						if (pTeleportAttribParams->fFacingYawOffset == 0.f)
						{
							bHasRotation = false;
						}
						else
						{
							fPitch = pyFace[0];
							fYaw = pyFace[1];
						}
					} 
					xcase kTeleportFaceType_Absolute:
						break;

					xdefault:
						bHasRotation = false;
						break;
				}

				if (!bHasRotation)
				{
					entSetPos(pchar->pEntParent, vTargetPos, true, "TeleportAttribMod");
				}
				else
				{
					Quat qRot;

					if (pTeleportAttribParams->fFacingYawOffset)
						fYaw = addAngle(fYaw, RAD(pTeleportAttribParams->fFacingYawOffset));

					pyFace[1] = fYaw;
					if (pTeleportAttribParams->bFacingUsePitch)
					{	// include the pitch to the facing and rotation
						Vec3 vPYR = {fPitch, fYaw, 0.f};
						pyFace[1] = fPitch;

						PYRToQuat(vPYR, qRot);
					}
					else
					{
						yawQuat(-fYaw, qRot);
					}


					entSetPosRotFace(pchar->pEntParent, vTargetPos, qRot, pyFace, true, false, "TeleportAttribMod");
				}

				if (pmod->pParams->erParam && g_CombatConfig.pCombatAdvantage)
				{
					S32 iPartitionIdx = entGetPartitionIdx(pchar->pEntParent);
					Entity *pTarget = entFromEntityRef(iPartitionIdx, pmod->pParams->erParam);
					gslCombatAdvantage_CalculateFlankingForEntity(iPartitionIdx, pTarget);
				}
			}


		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessTeleThrow(AttribMod *pmod, Character *pchar)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	PERFINFO_AUTO_START_FUNC();
	{
		TeleThrowParams *pParams = (TeleThrowParams*)(pmod->pDef->pParams);
		if(pParams && pmod->pSourceDetails)
		{
			S32 i, iPartitionIdx = entGetPartitionIdx(pchar->pEntParent);
			PowerDef *ppowdef = GET_REF(pParams->hDef);
			PowerDef *ppowdefFallback = GET_REF(pParams->hDefFallback);
			Entity *pentSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
			Character *pcharSource = pentSource ? pentSource->pChar : NULL;
			
			if(pcharSource && ppowdef)
			{
				F32 fRadius = pParams->fRadius, fSourceCollRadius = 0;
				Entity **ppEntObjects = NULL;
				Vec3 vSourcePos, vTargetPos;
				Entity *pBestEntObject = NULL;
				WorldInteractionNode *pBestNode = NULL;
				F32 fBestMass = -1, fBestDistanceSqr = FLT_MAX;

				#if GAMESERVER || GAMECLIENT
					mmGetCollisionRadius(pentSource->mm.movement,&fSourceCollRadius);
				#else
					fSourceCollRadius = 0;
				#endif

				entGetCombatPosDir(pentSource,NULL,vSourcePos,NULL);
				entGetCombatPosDir(pchar->pEntParent,NULL,vTargetPos,NULL);

				entGridProximityLookupExEArray(iPartitionIdx,vSourcePos,&ppEntObjects,fRadius+fSourceCollRadius,0,ENTITYFLAG_IGNORE|ENTITYFLAG_UNTARGETABLE,NULL);

				for(i=eaSize(&ppEntObjects)-1; i>=0; i--)
				{
					Entity *pentObject = ppEntObjects[i];
					WorldInteractionNode *pNode = GET_REF(pentObject->hCreatorNode);
					if(pNode && pentObject->pChar
						&& !pentObject->pChar->erHeldBy
						&& entity_CanAffect(iPartitionIdx, pentSource, pentObject))
					{
						F32 fDistanceSqr;
						Vec3 vObjectPos;
						F32 fMass = im_GetMass(pNode);
						if(fMass < fBestMass || fMass > pmod->fMagnitude)
							continue; // Less mass or too heavy

						entGetCombatPosDir(pentObject,NULL,vObjectPos,NULL);
						fDistanceSqr = distance3Squared(vSourcePos,vObjectPos);
						
						if(fMass == fBestMass && fDistanceSqr >= fBestDistanceSqr)
							continue; // Same mass, not any closer

						if(!combat_CheckLoS(iPartitionIdx,vObjectPos,vTargetPos,pentObject,pchar->pEntParent,NULL,false,false,NULL))
							continue; // Doesn't have LoS to target

						// Better mass, or same mass but closer, with LoS to target
						pBestEntObject = pentObject;
						pBestNode = pNode;
						fBestMass = fMass;
						fBestDistanceSqr = fDistanceSqr;
					}
				}

				if(entity_CanAffect(iPartitionIdx, pentSource, NULL))
				{
					WorldInteractionNode **ppNodes = NULL;
					wlInteractionQuerySphere(iPartitionIdx,g_iDestructibleThrowableMask,NULL,vSourcePos,fRadius+fSourceCollRadius,false,false,true,&ppNodes);

					for(i=eaSize(&ppNodes)-1; i>=0; i--)
					{
						WorldInteractionNode *pNode = ppNodes[i];
						F32 fDistanceSqr;
						Vec3 vObjectPos,vObjectLocalMin,vObjectLocalMax;
						Mat4 mObjectPos;
						F32 fMass = im_GetMass(pNode);
						if(fMass < fBestMass || fMass > pmod->fMagnitude)
							continue; // Less mass or too heavy

						if(!im_EntityCanThrowObject(pentSource, pNode, pmod->fMagnitude))
						{
							continue;
						}

						//wlInteractionNodeGetWorldMid(pNode,vObjectPos);
						wlInteractionNodeGetLocalBounds(pNode,vObjectLocalMin,vObjectLocalMax,mObjectPos);
						copyVec3(mObjectPos[3],vObjectPos);
						vObjectPos[1] += 5;

						fDistanceSqr = distance3Squared(vSourcePos,vObjectPos);

						if(fMass == fBestMass && fDistanceSqr >= fBestDistanceSqr)
							continue; // Same mass, not any closer

						if(!combat_CheckLoS(iPartitionIdx,vObjectPos,vTargetPos,NULL,pchar->pEntParent,NULL,false,false,NULL))
							continue; // Doesn't have LoS to target

						// Better mass, or same mass but closer, with LoS to target
						pBestEntObject = NULL;
						pBestNode = pNode;
						fBestMass = fMass;
						fBestDistanceSqr = fDistanceSqr;
					}
				}

#ifdef GAMESERVER
				if(pBestNode && !pBestEntObject)
					pBestEntObject = im_InteractionNodeToEntity(iPartitionIdx, pBestNode);
#endif

				if(pBestEntObject && pBestEntObject->pChar)
				{
					ApplyUnownedPowerDefParams applyParams = {0};

					pBestEntObject->pChar->erHeldBy = pmod->erSource;

					applyParams.pmod = pmod;
					applyParams.erTarget = entGetRef(pchar->pEntParent);
					applyParams.pSubtarget = pmod->pSubtarget;
					applyParams.pcharSourceTargetType = pcharSource;
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

					character_ApplyUnownedPowerDef(iPartitionIdx, pBestEntObject->pChar, ppowdef, &applyParams);
					
#ifdef GAMESERVER
					if(IS_HANDLE_ACTIVE(pBestEntObject->hCreatorNode))
						eventsend_RecordObjectDeath(pcharSource->pEntParent,REF_STRING_FROM_HANDLE(pBestEntObject->hCreatorNode));
#endif
				}
				else if(ppowdefFallback)
				{
					ApplyUnownedPowerDefParams applyParams = {0};

					applyParams.pmod = pmod;
					applyParams.erTarget = entGetRef(pchar->pEntParent);
					applyParams.pSubtarget = pmod->pSubtarget;
					applyParams.pcharSourceTargetType = pcharSource;
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

					character_ApplyUnownedPowerDef(iPartitionIdx, pcharSource, ppowdefFallback, &applyParams);

				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessTriggerComplex(AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		S32 i;
		TriggerComplexParams *pParams = (TriggerComplexParams*)(pmod->pDef->pParams);
		if(pParams)
		{
			// Cleanup if out of charges, and exit early
			if(pParams->bMagnitudeIsCharges && pmod->fMagnitude < 1)
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
				PERFINFO_AUTO_STOP();
				return;
			}

			// Note that we need to track complex data for this trigger's events
			if(!pchar->pCombatEventState)
				pchar->pCombatEventState = combatEventState_Create();
			for(i=eaiSize(&pParams->piCombatEvents)-1; i>=0; i--)
				pchar->pCombatEventState->abCombatEventTriggerComplex[pParams->piCombatEvents[i]] = true;
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessTriggerSimple(int iPartitionIdx, AttribMod *pmod, Character *pchar, GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	{
		static Power **s_eaPowEnhancements = NULL;
		PowerDef *pdef;
		TriggerSimpleParams *pParams = (TriggerSimpleParams*)(pmod->pDef->pParams);
		S32 i, bFail = false;
		Character *pcharTargetType = mod_GetApplyTargetTypeCharacter(iPartitionIdx,pmod,&bFail);

		// Cleanup if out of charges, and exit early
		if(pParams && pParams->bMagnitudeIsCharges && pmod->fMagnitude < 1)
		{
			character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
			PERFINFO_AUTO_STOP();
			return;
		}

		if(!pchar->pCombatEventState)
			pchar->pCombatEventState = combatEventState_Create();
		for(i=eaiSize(&pParams->piCombatEvents)-1; i>=0; i--)
			pchar->pCombatEventState->abCombatEventTriggerSimple[pParams->piCombatEvents[i]] = true;

		if(!bFail
			&& pParams
			&& (pdef=GET_REF(pParams->hDef))
			&& pmod->pSourceDetails)
		{
			S32 iEvents = character_CountCombatEvents(pchar,pParams->piCombatEvents,pParams->bRespondOncePerTick?1:0);
			if(iEvents)
			{
				Character *pcharSource = pchar;
				Entity *eModOwner = entFromEntityRef(iPartitionIdx, pmod->erOwner);
				
				if(pParams->eSource==kTriggerSimpleEntity_ModOwner)
				{
					pcharSource = eModOwner ? eModOwner->pChar : NULL;
				}
				else if(pParams->eSource==kTriggerSimpleEntity_ModSource)
				{
					Entity *eModSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
					pcharSource = eModSource ? eModSource->pChar : NULL;
				}

				if(pcharSource)
				{
					EntityRef erTarget = pmod->erOwner;
					ApplyUnownedPowerDefParams applyParams = {0};

					if(pParams->eTarget==kTriggerSimpleEntity_ModSource)
					{
						erTarget = pmod->erSource;
					}
					else if(pParams->eTarget==kTriggerSimpleEntity_ModTarget)
					{
						erTarget = entGetRef(pchar->pEntParent);
					}

					applyParams.pmod = pmod;
					applyParams.erTarget = erTarget;
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

					if(eModOwner && eModOwner->pChar)
					{
						power_GetEnhancementsForAttribModApplyPower(iPartitionIdx, eModOwner->pChar, 
																	pmod, EEnhancedAttribList_DEFAULT, 
																	pdef, &s_eaPowEnhancements);
						applyParams.pppowEnhancements = s_eaPowEnhancements;
					}

					for(i=0; i < iEvents; i++)
					{
						if(pParams->pExprChance && !character_TriggerAttribModCheckChance(iPartitionIdx, pchar, pmod->erOwner, pParams->pExprChance))
							continue;

						character_ApplyUnownedPowerDef(iPartitionIdx, pcharSource, pdef, &applyParams);
				
						// Decrement charges, and expire and break if necessary
						if(pParams->bMagnitudeIsCharges)
						{
							pmod->fMagnitude -= 1;
							if(pmod->fMagnitude < 1)
							{
								character_ModExpireReason(pchar, pmod, kModExpirationReason_Charges);
								break;
							}
						}
					}

					eaClear(&s_eaPowEnhancements);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void ModProcessWarpSet(AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER)
			&& gGSLState.gameServerDescription.baseMapDescription.eMapType==ZMTYPE_STATIC)
		{
			if(!pchar->pEntParent->pPlayer->pWarpTo)
			{
				pchar->pEntParent->pPlayer->pWarpTo = StructCreate(parse_PlayerPowersWarpToData);
			}

			StructFreeString(pchar->pEntParent->pPlayer->pWarpTo->pchMap);

			pchar->pEntParent->pPlayer->pWarpTo->pchMap = StructAllocString(gGSLState.gameServerDescription.baseMapDescription.mapDescription);
			entGetPos(pchar->pEntParent,pchar->pEntParent->pPlayer->pWarpTo->vecPos);
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessWarpTo(AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		Entity* pEnt = pchar->pEntParent;
		WarpToParams *pParams = (WarpToParams*)pmod->pDef->pParams;
		if(pParams && entCheckFlag(pEnt,ENTITYFLAG_IS_PLAYER))
		{
			if(pParams->cpchMap || pParams->cpchSpawn)
			{
				const char* pchCurrMap = zmapInfoGetPublicName(NULL);
				const char* pchNextMap = pParams->cpchMap;
				const char* pchSpawn = pParams->cpchSpawn;
				DoorTransitionSequenceDef* pTransOverride = GET_REF(pParams->hTransOverride);

				if (pchNextMap && stricmp(pchCurrMap, pchNextMap)==0)
				{
					pchNextMap = NULL; // Don't specify a map if this is a same-map transfer
					if (!pchSpawn || !(*pchSpawn) || !stricmp(pchSpawn, START_SPAWN)) 
					{
						pchSpawn = allocFindString(START_SPAWN);
					}
				}
				// Warp to the spawn point on the specified map
				spawnpoint_MovePlayerToMapAndSpawn(pEnt,pchNextMap,pchSpawn,NULL,0,0,0,0,NULL,NULL,pTransOverride,0, 0);
			}
			else if(pEnt->pPlayer && pEnt->pPlayer->pWarpTo)
			{
				// Warp to set location if there is one
				MapDescription mapDesc = {0};
				Quat qRot = {0};
				mapDesc.eMapType = ZMTYPE_STATIC;
				mapDesc.mapDescription = allocAddString(pEnt->pPlayer->pWarpTo->pchMap);
				MapMoveWithDescriptionAndPosRot(pEnt,&mapDesc,pEnt->pPlayer->pWarpTo->vecPos,qRot,__FUNCTION__, false);
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessAIAggroTotalScale(AttribMod *pmod, Character *pchar)
{
#ifdef GAMESERVER
	PERFINFO_AUTO_START_FUNC();
	{
		eaPush(&pchar->ppModsAIAggro, pmod);
	}
	PERFINFO_AUTO_STOP();
#endif
}

static void ModProcessSpeedCooldown(int iPartitionIdx, AttribMod *pmod, Character *pchar)
{
	PERFINFO_AUTO_START_FUNC();
	{
		SpeedCooldownParams *pParams = (SpeedCooldownParams*)pmod->pDef->pParams;
		if (pParams)
		{
			AttribModDef *pmoddefChar = mod_GetDef(pmod);
			CooldownRateModifier* pCooldownModifier = eaIndexedGetUsingInt(&pchar->ppSpeedCooldown, pParams->ePowerCategory);
			F32 fMag;
			
			if (!pCooldownModifier)
			{
				pCooldownModifier = StructCreate(parse_CooldownRateModifier);
				pCooldownModifier->iPowerCategory = pParams->ePowerCategory;
				eaIndexedEnable(&pchar->ppSpeedCooldown, parse_CooldownRateModifier);
				eaPush(&pchar->ppSpeedCooldown, pCooldownModifier);
			}
			fMag = ModGetEffectiveMagnitude(iPartitionIdx,pmod,pmoddefChar,pchar);

			switch(pmoddefChar->offAspect)
			{
			xcase kAttribAspect_BasicAbs:
				pCooldownModifier->fBasicAbs += fMag;
			xcase kAttribAspect_BasicFactPos:
				pCooldownModifier->fBasicPos += fMag;
			xcase kAttribAspect_BasicFactNeg:
				pCooldownModifier->fBasicNeg += fMag;
			}

			pCooldownModifier->bDirty = true;
		}
	}
	PERFINFO_AUTO_STOP();
}


// Case-by-case code for handling non-numerical attribs once they fire
static void ModProcessSpecial(int iPartitionIdx,
								AttribMod *pmod,
								AttribModDef *pdef,
								Character *pchar,
								F32 fRate,
								int *piModes,
								F32 fResistTrue,
								F32 fResist,
								F32 fImmune,
								F32 fAvoid,
								GameAccountDataExtract *pExtract)
{
	PERFINFO_AUTO_START_FUNC();
	
	switch(pdef->offAttrib)
	{
	case kAttribType_AICommand:
		ModProcessAICommand(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_ApplyObjectDeath:
		ModProcessApplyObjectDeath(pmod,pchar,entGetPartitionIdx(pchar->pEntParent),pExtract);
		break;
	case kAttribType_ApplyPower:
		ModProcessApplyPower(iPartitionIdx,pmod,pchar,pExtract);
		break;
	case kAttribType_AttribLink:
		AttribModLink_ModProcess(iPartitionIdx, pmod, pchar);
		break;
	case kAttribType_AttribModDamage:
		ModProcessAttribModDamage(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_AttribModExpire:
		ModProcessAttribModExpire(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_AttribModFragilityHealth:
		PERFINFO_AUTO_START("AttribModFragilityHealth",1);
		eaPushUnique(&pchar->ppModsAttribModFragilityHealth,pmod);
		PERFINFO_AUTO_STOP();
		break;
	case kAttribType_AttribModFragilityScale:
		PERFINFO_AUTO_START("AttribModFragilityScale",1);
		eaPushUnique(&pchar->ppModsAttribModFragilityScale,pmod);
		PERFINFO_AUTO_STOP();
		break;
	case kAttribType_AttribModHeal:
		if(pdef->offAspect==kAttribAspect_BasicAbs)
		{
			PERFINFO_AUTO_START("AttribModHeal",1);
			eaPush(&pchar->ppModsHeal,pmod);
			PERFINFO_AUTO_STOP();
		}
		break;
	case kAttribType_AttribModShare:
		PERFINFO_AUTO_START("AttribModShare",1);
		eaPush(&pchar->ppModsShare,pmod);
		PERFINFO_AUTO_STOP();
		break;
	case kAttribType_AttribModShieldPercentIgnored:
		PERFINFO_AUTO_START("AttribModShieldPercentIgnored",1);
		eaPushUnique(&pchar->ppModsAttribModShieldPercentIgnored,pmod);
		PERFINFO_AUTO_STOP();
		break;
	case kAttribType_AttribOverride:
		ModProcessAttribOverride(pmod,pchar);
		break;
	case kAttribType_BecomeCritter:
		ModProcessBecomeCritter(iPartitionIdx,pmod,pchar,pExtract);
		break;	
	case kAttribType_BePickedUp:
		ModProcessPickedUp(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_CombatAdvantage:
		ModProcessCombatAdvantage(iPartitionIdx, pmod, pchar);
		break;
	case kAttribType_ConstantForce:
		ModProcessConstantForce(iPartitionIdx, pmod, pdef, pchar);
		break;
	case kAttribType_DamageTrigger:
		ModProcessDamageTrigger(pmod,pchar);
		break;	
	case kAttribType_DisableTacticalMovement:
		ModProcessDisableTacticalMovement(pmod,pchar);
		break;
	case kAttribType_DropHeldObject:
		ModProcessDropHeldObject(pmod,pchar);
		break;
	case kAttribType_EntAttach:
		ModProcessEntAttach(pmod,pchar);
		break;
	case kAttribType_EntCreate:
		ModProcessEntCreate(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_EntCreateVanity:
		ModProcessEntCreateVanity(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_Faction:
		ModProcessFaction(pmod,pchar);
		break;
	case kAttribType_Flag:
		ModProcessFlag(pmod,pchar);
		break;
	case kAttribType_GrantPower:
		ModProcessGrantPower(iPartitionIdx,pmod,pchar,pExtract);
		break;
	case kAttribType_GrantReward:
		ModProcessGrantReward(pmod,pchar);
		break;
	case kAttribType_Interrupt:
		ModProcessInterrupt(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_ItemDurability:
		//This attrib has been removed.
		break;
	case kAttribType_Kill:
		ModProcessKill(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_KillTrigger:
		ModProcessKillTrigger(pmod,pchar);
		break;
	case kAttribType_KnockTo:
		ModProcessKnockTo(iPartitionIdx,pmod,pchar,fResistTrue,fResist,fImmune,fAvoid);
		break;
	case kAttribType_MissionEvent:
		ModProcessMissionEvent(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_ModifyCostume:
		PERFINFO_AUTO_START("ModifyCostume",1);
		eaPush(&pchar->ppCostumeModifies,pmod);
		PERFINFO_AUTO_STOP();
		break;
	case kAttribType_Notify:
		ModProcessNotify(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_Placate:
		ModProcessPlacate(iPartitionIdx,pmod,pdef,pchar);
		break;
	case kAttribType_PowerMode:
		ModProcessPowerMode(iPartitionIdx,pmod,pchar,piModes);
		break;
	case kAttribType_PowerRecharge:
		ModProcessPowerRecharge(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_PowerShield:
		ModProcessPowerShield(pmod,pchar);
		break;
#ifdef GAMESERVER
	case kAttribType_ProjectileCreate:
		ModProcessProjectileCreate(iPartitionIdx, pmod, pchar);
		break;

	case kAttribType_PVPFlag:
		ModProcessPVPFlag(iPartitionIdx, pmod, pchar);
		break;

	case kAttribType_PVPSpecialAction:
		ModProcessPVPSpecialAction(pmod, pchar);
		break;
#endif
	case kAttribType_RemovePower:
		ModProcessRemovePower(pmod,pchar);
		break;
	case kAttribType_RewardModifier:
		PERFINFO_AUTO_START("RewardModifier",1);
		eaPush(&pchar->ppRewardModifies, pmod);
		PERFINFO_AUTO_STOP();
		break;
#ifdef GAMESERVER
	case kAttribType_Ride:
		ModProcessRide(iPartitionIdx,pmod,pchar,pExtract);
		break;
#endif
	case kAttribType_SetCostume:
		PERFINFO_AUTO_START("SetCostume",1);
		eaPush(&pchar->ppCostumeChanges,pmod);
		PERFINFO_AUTO_STOP();
		break;
	case kAttribType_Shield:
		ModProcessShield(pmod,pchar);
		break;	
	case kAttribType_SpeedCooldownCategory:
		ModProcessSpeedCooldown(iPartitionIdx,pmod,pchar);
		break;
	case kAttribType_SubtargetSet:
		ModProcessSubtargetSet(pmod,pchar);
		break;
	case kAttribType_Taunt:
		PERFINFO_AUTO_START("Taunt",1);
		eaPushUnique(&pchar->ppModsTaunt,pmod);
		PERFINFO_AUTO_STOP();
		break;
	case kAttribType_Teleport:
		ModProcessTeleport(pmod,pchar,fResistTrue,fResist,fImmune);
		break;
	case kAttribType_TeleThrow:
		ModProcessTeleThrow(pmod,pchar);
		break;
	case kAttribType_TriggerComplex:
		ModProcessTriggerComplex(pmod,pchar);
		break;
	case kAttribType_TriggerSimple:
		ModProcessTriggerSimple(iPartitionIdx,pmod,pchar,pExtract);
		break;
	case kAttribType_WarpSet:
		ModProcessWarpSet(pmod,pchar);
		break;
	case kAttribType_WarpTo:
		ModProcessWarpTo(pmod,pchar);
		break;
	case kAttribType_AIAggroTotalScale:
		ModProcessAIAggroTotalScale(pmod, pchar);
		break;
	case kAttribType_DynamicAttrib:
		break;
	default:
		// If we're recoding perf stuff, also record what kinds of AttribMods we're seeing
		if(PERFINFO_RUN_CONDITIONS)
		{
			static StashTable stPerfs = NULL;
			moddef_RecordStaticPerf(pdef,&stPerfs);
		}
	}
	PERFINFO_AUTO_STOP();
}

// Case-by-case code for handling special attrib mods, if they're not on a Character
void mod_ProcessSpecialUnowned(AttribMod *pmod,
							   F32 fRate,
							   int iPartitionIdx,
							   GameAccountDataExtract *pExtract)
{
	AttribModDef *pdef;
	PERFINFO_AUTO_START_FUNC();

	// If we still need to check validity, check it
	if(pmod->fCheckTimer > 0.0f)
	{
		if(pmod->bCheckSource)
		{
			Entity *e = entFromEntityRef(iPartitionIdx,pmod->erSource);
			
			if(!e || !character_CheckSourceActivateRules(e->pChar, pmod->pDef->pPowerDef->eActivateRules))
			{
				// Invalid!  Never managed to become a "real mod", so we can just expire it
				mod_Expire(pmod);
				PERFINFO_AUTO_STOP();
				return;
			}
		}

		// Still valid, decrement the check timer
		pmod->fCheckTimer -= fRate;
	}

	if(pmod->fTimer <= fRate)
	{
		pmod->fDuration -= fRate;

		pdef = pmod->pDef;
		switch(pdef->offAttrib)
		{
		case kAttribType_ApplyObjectDeath:
			ModProcessApplyObjectDeath(pmod,NULL,iPartitionIdx,pExtract);
			break;
		}
	}

	pmod->fTimer -= fRate;

	PERFINFO_AUTO_STOP();
}

AUTO_STARTUP(AS_CharacterAttribs);
void CharacterAttribsLoad(void)
{
	int i;
	char *pchSharedMemory = NULL;

	// Don't load on app servers except specific servers
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsQueueServer() && !IsTeamServer() && !IsGroupProjectServer()) {
		return;
	}

	// Load DamageNames
	loadstart_printf("Loading DamageNames...");
	MakeSharedMemoryName("DamageNames.bin",&pchSharedMemory);
	ParserLoadFilesShared(pchSharedMemory, NULL, "defs/config/DamageNames.def", "DamageNames.bin", 0, parse_DamageTypeNames, &g_DamageTypeNames);
	estrDestroy(&pchSharedMemory);

	g_iDamageTypeCount = eaSize(&g_DamageTypeNames.ppchNames);
	if(g_iDamageTypeCount>DAMAGETYPECOUNT)
	{
		ErrorFilenamef("defs/config/damagenames.def","Too many damage names (max of %d)\n",DAMAGETYPECOUNT);
		g_iDamageTypeCount = DAMAGETYPECOUNT;
	}

	s_pDefineDamageNames = DefineCreate();
	for(i=0; i<g_iDamageTypeCount; i++)
	{
		char achVal[20];
		sprintf(achVal,"%d",i*sizeof(F32));
		DefineAdd(s_pDefineDamageNames,g_DamageTypeNames.ppchNames[i],achVal);
	}
	loadend_printf(" done (%d DamageNames).", g_iDamageTypeCount);



	// Load Data Defined Attributes
	loadstart_printf("Loading Attributes...");
	MakeSharedMemoryName("Attributes.bin",&pchSharedMemory);
	ParserLoadFilesShared(pchSharedMemory, NULL, "defs/config/Attributes.def", "Attributes.bin", PARSER_OPTIONALFLAG, parse_DataDefinedAttributes, &g_DataDefinedAttributes);

	g_iDataDefinedAttributesCount = eaSize(&g_DataDefinedAttributes.ppchAttributes);
	if(g_iDataDefinedAttributesCount>DATADEFINED_ATTRIB_COUNT)
	{
		ErrorFilenamef("defs/config/Attributes.def","Too many attributes (max of %d)\n",DATADEFINED_ATTRIB_COUNT);
		g_iDataDefinedAttributesCount = DATADEFINED_ATTRIB_COUNT;
	}

	s_pDefineDataDefinedNames = DefineCreate();
	for(i=0; i<g_iDataDefinedAttributesCount; i++)
	{
		char achVal[20];
		sprintf(achVal,"%d",offsetof(CharacterAttribs,fDataDefined01) + i*sizeof(F32));
		DefineAdd(s_pDefineDataDefinedNames,g_DataDefinedAttributes.ppchAttributes[i],achVal);
	}
	loadend_printf(" done (%d Attributes).", g_iDataDefinedAttributesCount);


	// Set the useful attribute count by subtracting the maximum possible data defined, and adding back in the number
	//  actually defined by data
	g_iCharacterAttribCount = NUM_NORMAL_ATTRIBS - DATADEFINED_ATTRIB_COUNT + g_iDataDefinedAttributesCount;
	g_iCharacterAttribSizeUsed = g_iCharacterAttribCount * SIZE_OF_NORMAL_ATTRIB;

	// Load AttribPools
	loadstart_printf("Loading AttribPools...");
	MakeSharedMemoryName("AttribPools.bin",&pchSharedMemory);
	ParserLoadFilesShared(pchSharedMemory, NULL, "defs/config/AttribPools.def", "AttribPools.bin", PARSER_OPTIONALFLAG, parse_AttribPools, &g_AttribPools);
	g_iAttribPoolCount = eaSize(&g_AttribPools.ppPools);
	for(i=g_iAttribPoolCount-1; i>=0; i--)
	{
		if(g_AttribPools.ppPools[i]->eAttribCur==kAttribType_Power)
		{
			g_bAttribPoolPower = true;
			break;
		}
	}
	loadend_printf(" done (%d AttribPools).", g_iAttribPoolCount);

	estrDestroy(&pchSharedMemory);
}



bool stat_FillInAssignedStats(AssignedStats *pstat, Character *pchar, AttribAccrualSet *pattrSet)
{
	return true;
}

// Calculates the magnitude of an AttribModDef, assuming it's from an Innate PowerDef
F32 mod_GetInnateMagnitude(int iPartitionIdx,
						   AttribModDef *pdef,
						   Character *pchar,
						   CharacterClass *pClass,
						   int iLevel,
						   F32 fTableScale)
{
	F32 fMag = 0;

	if(pdef->pExprMagnitude)
	{
		fMag = combateval_EvalNew(iPartitionIdx,pdef->pExprMagnitude,kCombatEvalContext_Simple,NULL);

		// Apply default table if we have one
		if(pdef->pchTableDefault && (pdef->eType&kModType_Magnitude))
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

		// Apply strength and resistance
		//  TODO(JW): Do strength and resistance really make sense for innates?  Should they
		//  always be un-strengthable and un-resistable?
		if(pchar && eaiSize(&pdef->piSensitivities) && pdef->eType!=kModType_None && IS_NORMAL_ATTRIB(pdef->offAttrib))
		{
			{
				F32 fStr = character_GetClassAttrib(pchar,kClassAttribAspect_Str,pdef->offAttrib);
				fStr = mod_SensitivityAdjustment(fStr,pdef->fSensitivityStrength);
				fMag *= fStr;
			}

			{
				F32 fRes = character_GetClassAttrib(pchar,kClassAttribAspect_Res,pdef->offAttrib);
				fRes = mod_SensitivityAdjustment(fRes,pdef->fSensitivityResistance);
				fMag /= fRes;
			}
		}
	}

	return fMag;
}

// Slimmed down and tweaked version of mod_Process, used to accumulate mods from innate powers
//  Returns true if the accrual changed.
int mod_ProcessInnate(int iPartitionIdx,
					  AttribModDef *pdef,
					  Character *pchar,
					  CharacterClass *pClass,
					  int iLevel,
					  F32 fTableScale,
					  AttribAccrualSet *pattrSet)
{
	if(IS_NORMAL_ATTRIB(pdef->offAttrib))
	{
		F32 *pf;
		F32 fMag = mod_GetInnateMagnitude(iPartitionIdx,pdef,pchar,pClass,iLevel,fTableScale);
		bool bResult;

		pf = (F32*)((char*)pattrSet + pdef->offAspect + pdef->offAttrib);
		processInnateAttribAccrual(pf, fMag, pdef->offAspect);

		bResult = *pf != 0.0f;

		if (gConf.bSendInnateAttribModData && 
			bResult &&
			pdef->offAspect == kAttribAspect_BasicAbs) // Only process basic absolute aspect for now
		{
			InnateAttribMod *pMod = StructCreate(parse_InnateAttribMod);
			pMod->eSource = InnateAttribModSource_Power;
			pMod->eAttrib = pdef->offAttrib;
			pMod->eAspect = pdef->offAspect;
			pMod->fMag = fMag;
			SET_HANDLE_FROM_REFERENT(g_hPowerDefDict, pdef->pPowerDef, pMod->hPowerDef);
			eaPush(&pchar->pInnateAttribModData->ppInnateAttribMods, pMod);
		}

		return bResult;
	}
	else if (pdef->offAttrib == kAttribType_SpeedCooldownCategory)
	{
		return mod_ProcessInnateSpeedCooldown(iPartitionIdx, pdef, pattrSet, pchar, pClass, iLevel, fTableScale);
	}
	else if (pdef->offAttrib == kAttribType_DynamicAttrib)
	{
		return mod_ProcessDynamicAttrib(iPartitionIdx,pdef,pchar,pClass,iLevel,fTableScale,pattrSet,false, 1.0f);
	}
	return false;
}

void mod_Process(int iPartitionIdx,
				 AttribMod *pmod,
				 Character *pchar,
				 F32 fRate,
				 AttribAccrualSet *pattrSet,
				 int *piModes,
				 U32 uiTimeLoggedOut,
				 GameAccountDataExtract *pExtract)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	Entity *eSource;
	char *pchTrickPtr;
	AttribModDef *pmoddef;
	AttribType eAttrib;
	AttribAspect eAspect;
	S32 bAnimFX;
	S32 bPeriod;
	S32 bDynamicAttrib = false;

	PERFINFO_AUTO_START_FUNC();

	assert(pmod);
	eSource = entFromEntityRef(iPartitionIdx,pmod->erSource);
	pchTrickPtr = (char*)pattrSet;
	pmoddef = pmod->pDef;
	eAttrib = pmoddef->offAttrib;
	eAspect = pmoddef->offAspect;
	pmod->fTimer -= fRate;
	bAnimFX = false;
	bPeriod = false;

	if(pmoddef->offAttrib == kAttribType_DynamicAttrib)
	{
		bDynamicAttrib = true;
		eAttrib = DynamicAttribEval(iPartitionIdx,pmod,(DynamicAttribParams*)pmoddef->pParams,false,true);
		pmod->pParams->eDynamicCachedType = eAttrib;
	}
	

	if(pmod->pFragility && eAttrib!=kAttribType_Shield)
		eaPush(&pchar->modArray.ppFragileMods,pmod);

	// If we're recoding perf stuff, also record what kinds of AttribMods we're seeing
	if(PERFINFO_RUN_CONDITIONS)
	{
		static StashTable stPerfs = NULL;
		moddef_RecordStaticPerf(pmoddef,&stPerfs);
	}

	PERFINFO_AUTO_START("Step",1);
	{
		// Default mod mitigation values
		F32 fResist = 1.0f;
		F32 fResistTrue = 1.f;
		F32 fImmune = 0.0f;
		F32 fAvoid = 0.f;
		CombatTrackerFlag eFlagsMit = 0;
		
		F32 fRateScale = 1;

		bool bFoundMitigators = false;

		bool bApplyThisTick;
		
		PERFINFO_AUTO_START("Duration & Timer",1);
		// Ready to roll if the timer is up
		//  If it's gotten to here, it didn't expire last tick, so it's
		//  still valid this tick
		bApplyThisTick = pmod->fTimer <= pmod->fPredictionOffset;
		bAnimFX = true;

		if(pmoddef->eType&kModType_Duration
			&& (!pmoddef->bForever
				|| !pmoddef->bKeepWhenImmune))
		{
			character_ModGetMitigators(iPartitionIdx,pmod,pmoddef,pchar,&fResistTrue,&fResist,&fImmune,&fAvoid,&eFlagsMit);
			bFoundMitigators = true;
			if (fResistTrue > 0.f)
				fRateScale = fResist/fResistTrue;

			fRateScale *= 1.f + fAvoid;

			fRate *= fRateScale; // fRateScale > 1 makes makes time go faster, fResist < 1 makes time go slower

			// Check for cancel if immune
			if(fImmune>0 && !pmoddef->bKeepWhenImmune)
			{
				// TODO(JW): Invalid Mods: Should mark these so they're not
				//  used for the rest of the tick
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Immunity);
				bAnimFX = false;
				bApplyThisTick = false;
								
				if (g_CombatConfig.bSendImmunityCombatTrackers && !IS_DAMAGE_ATTRIBASPECT(eAttrib,eAspect))
				{
					character_CreateImmunityCombatTracker(pchar, eSource, pmod, pmoddef);
				}
			}

			// Apparently these are set here, so the only way they could
			//  be set is if the mod is of type duration?
			pmod->bResistPositive = fRateScale > 1.01f;
			pmod->bResistNegative = fRateScale < 0.99f;
		}

		if(pmod->fDuration >= 0 && !pmoddef->bForever)
		{
			pmod->fDuration -= fRate;
			if (uiTimeLoggedOut > 0 && pmod->bProcessOfflineTimeOnLogin)
				pmod->fDuration -= uiTimeLoggedOut;
			if(pmod->fDuration < 0)
			{
				character_ModExpireReason(pchar, pmod, kModExpirationReason_Duration);
			}
			else
			{
				character_SetSleep(pchar,pmod->fDuration/fRateScale);
			}
		}
		PERFINFO_AUTO_STOP(); // Duration & Timer

		if(bApplyThisTick)
		{
			F32 fRand;
			F32 fChance;

			PERFINFO_AUTO_START("ApplyThisTick",1);

			fRand = 0.0f;
			fChance = 1.0f;

			if(pmoddef->fPeriod!=0.0f)
			{
				bPeriod = true;

				// Add the period to get the next tick started
				pmod->fTimer += pmoddef->fPeriod; // TODO(JW): Period speed modifications go here?

				// Evaluate the chance
				if(pmoddef->pExprChance)
				{
					combateval_ContextSetupExpiration(pchar,pmod,pmoddef,pmoddef->pPowerDef);
					fChance = combateval_EvalNew(iPartitionIdx,pmoddef->pExprChance,kCombatEvalContext_Apply,NULL);
				}

				// Increment the period counter
				pmod->uiPeriod++;

				// If this periodic attrib needs a chance check, roll it
				if(fChance<1.0f)
				{
					fRand = randomPositiveF32();
				}
			}
			else
			{
				// Keep the timer pegged at 0 on applied, non-periodic mods, to help
				// minimize diffs in various systems
				pmod->fTimer = 0;
			}

			if(fRand>=fChance)
			{
				if(pmoddef->bCancelOnChance)
				{
					// TODO(JW): Invalid Mods: Should mark these so they're not
					//  used for the rest of the tick
					character_ModExpireReason(pchar, pmod, kModExpirationReason_Chance);
					entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
				}

				bAnimFX = false;
				bPeriod = false;
			}
			else if(!pmod->bIgnored)
			{
				F32 fMag = pmod->fMagnitude;
				F32 fMagAI = fMag;	// Magnitude given to the AI, may be different than the actual magnitude

				bAnimFX = true;

				if((pmod->erPersonal || pmoddef->pExprAffects) && IS_NORMAL_ATTRIB(eAttrib) && IS_BASIC_ASPECT(eAspect))
				{
					// Would modify an attrib, but it's Personal or has an Affects expression, so we
					//  just do some of the basic magnitude adjustments (for the AI) and
					//  then do not actually accumulate the result.
					PERFINFO_AUTO_START("Personal",1);

					// Mark that we've got some Basic Disable mod  with an Affects expression
					if(eAttrib==kAttribType_Disable && pmoddef->pExprAffects)
						pchar->modArray.bHasBasicDisableAffects = true;

					if(!bFoundMitigators)
					{
						character_ModGetMitigators(iPartitionIdx, pmod, pmoddef, pchar, &fResistTrue, &fResist, &fImmune, &fAvoid, &eFlagsMit);
						bFoundMitigators = true;
					}

					if(fImmune > 0.0f)
					{
						// If the character is Immune, then kill the magnitude, check for cancel
						fMag = 0.0f;
						if(!pmoddef->bKeepWhenImmune)
						{
							// TODO(JW): Invalid Mods: Should mark these so they're not
							//  used for the rest of the tick
							character_ModExpireReason(pchar, pmod, kModExpirationReason_Immunity);
							entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
							bAnimFX = false;

							if (g_CombatConfig.bSendImmunityCombatTrackers && !IS_DAMAGE_ATTRIBASPECT(eAttrib,eAspect))
							{
								character_CreateImmunityCombatTracker(pchar, eSource, pmod, pmoddef);
							}
						}
					}
					else if(pmoddef->eType&kModType_Magnitude)
					{
						// Scale the mag down by the resist
						fMag = fMag * fResistTrue / fResist; // fResist > 1 makes mag go down, fResist < 1 makes mag go up
					}

					// Apply avoidance
					if(fAvoid)
					{
						if(pmoddef->eType&kModType_Magnitude)
						{
							fMag /= 1.f + fAvoid;
						}
					}
					
					PERFINFO_AUTO_STOP();
				}
				else if(IS_NORMAL_ATTRIB(eAttrib) && IS_BASIC_ASPECT(eAspect))
				{
					// Modify an attrib
					F32 fMagNoResist, *pf;
					CombatTrackerFlag eFlagsAdded = 0;

					PERFINFO_AUTO_START("Normal",1);
					
					PERFINFO_AUTO_START("Magnitude",1);
					fMagNoResist = fMag;
					pf = (F32*)(pchTrickPtr + eAspect + eAttrib);

					if(!bFoundMitigators)
					{
						character_ModGetMitigators(iPartitionIdx,pmod,pmoddef,pchar,&fResistTrue,&fResist,&fImmune,&fAvoid,&eFlagsMit);
						bFoundMitigators = true;
					}

					if(fImmune > 0.0f)
					{
						// If the character is Immune, then kill the magnitude, check for cancel
						fMag = 0.0f;
						eFlagsAdded |= kCombatTrackerFlag_Immune;
						if(!pmoddef->bKeepWhenImmune)
						{
							// TODO(JW): Invalid Mods: Should mark these so they're not
							//  used for the rest of the tick
							character_ModExpireReason(pchar, pmod, kModExpirationReason_Immunity);
							entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
							bAnimFX = false;
							
							if (g_CombatConfig.bSendImmunityCombatTrackers && !IS_DAMAGE_ATTRIBASPECT(eAttrib,eAspect))
							{
								character_CreateImmunityCombatTracker(pchar, eSource, pmod, pmoddef);
							}
						}
					}
					else if(pmoddef->eType&kModType_Magnitude)
					{
						// Scale the mag down by the resist
						fMag = fMag * fResistTrue / fResist; // fResist > 1 makes mag go down, fResist < 1 makes mag go up
					}

					// Add the flags
					eFlagsAdded |= eFlagsMit;

					// Apply avoidance
					if(fAvoid)
					{
						if(pmoddef->eType&kModType_Magnitude)
						{
							fMag /= 1.f + fAvoid;
						}
					}
					
					// Save the magnitude for the AI after potential fiddling with resists, immunity and avoidance
					fMagAI = fMag;

					PERFINFO_AUTO_STOP(); // Magnitude

					if(IS_DAMAGE_ATTRIBASPECT(eAttrib,eAspect))
					{
						DamageTracker *pDamageTracker;
						F32 fEffMag = fMag, fEffMagNoResist = fMagNoResist;
						S32 bShieldedCountsAsDamage = false;
						F32 fPreShieldMag = fMag;

						PERFINFO_AUTO_START("Damage",1);

						// Apply invulnerability
						if(pchar->bInvulnerable || pchar->bUsingDoor || (eSource && encounter_IsDamageDisabled(entGetPartitionIdx(eSource))))
						{
							fEffMag = fMag = 0;
							fEffMagNoResist = fMagNoResist = 0;
						}

						// Use an approximation of the damage about to be dealt
						if(eAspect==kAttribAspect_BasicFactNeg)
						{
							F32 fMax = pchar->pattrBasic->fHitPointsMax;
							fEffMag *= fMax;
							fEffMagNoResist *= fMax;
							fMagAI *= fMax;
						}

						fPreShieldMag = fMag;

						// Check to see if the Shield attrib absorbs any damage
						if(fEffMag > 0 && eaSize(&pchar->ppModsShield))
						{
							F32 fResult = character_ProcessShields(iPartitionIdx, pchar, eAttrib, fEffMag, 
																	fEffMagNoResist, pmod, &bShieldedCountsAsDamage, 
																	false, &eFlagsAdded);
							fMag *= fResult;
							fEffMag *= fResult;
						}

						// See if we randomly or intentionally deal damage to items
						if(fEffMag > 0 && !powerdef_IgnoresAttrib(pmoddef->pPowerDef,kAttribType_SubtargetSet))
						{
							CharacterClass *pClass = character_GetClassCurrent(pchar);
							F32 fItemDamagePct = 0;
							
							if(pClass && pClass->fAutoItemDamageChance && randomPositiveF32() < pClass->fAutoItemDamageChance)
							{
								fItemDamagePct += pClass->fAutoItemDamageProportion;
							}
							if(pmod->pSubtarget)
							{
								fItemDamagePct += pmod->pSubtarget->fAccuracy;
							}
						}

						// ... apply the mag
						*pf += fMag;

						if (bShieldedCountsAsDamage)
						{	// if we want to otherwise count the shielded damage as damage for the purposes of triggers 
							// and damage floats
							fMag = fPreShieldMag;
							fEffMag = fPreShieldMag;
						}

						
						pDamageTracker = damageTracker_AddTick(iPartitionIdx,pchar, pmod->erOwner, pmod->erSource,
																pchar->pEntParent->myRef, fEffMag, fEffMagNoResist, eAttrib, 
																pmod->uiApplyID, GET_REF(pmod->hPowerDef), pmoddef->uiDefIdx, 
																NULL, ((pmod->eFlags|eFlagsAdded) & ~kCombatTrackerFlag_ShowPowerDisplayName));

						if (g_CombatConfig.pchFallingDamagePower &&
							g_CombatConfig.pchFallingDamagePower == pmoddef->pPowerDef->pchName)
						{
							pDamageTracker->pchDisplayNameKey = "AutoDesc.CombatEvent.Falling";
						}

						if(fEffMagNoResist > 0)
						{
							ModCheckDamageTriggers(iPartitionIdx,pmod,pchar,eAttrib,fEffMag,fEffMagNoResist,pExtract);
#ifdef GAMESERVER
							gslLogoff_Cancel(pchar->pEntParent, kLogoffCancel_CombatDamage);
#endif
						}

						if(fMag > 0)
						{
							Vec3 vCharPos;
							entGetPos(pchar->pEntParent, vCharPos);
							character_CombatEventTrackInOut(pchar, kCombatEvent_DamageIn, kCombatEvent_DamageOut, eSource,
															pmoddef->pPowerDef, pmoddef, fEffMag, fEffMagNoResist, 
															pmod->vecSource, vCharPos);
							character_ActInterrupt(iPartitionIdx, pchar, kPowerInterruption_Damage);
						}

						PERFINFO_AUTO_STOP();
					}
					else if(!pchar->bUnstoppable && !pchar->bUsingDoor && eAttrib==kAttribType_KnockUp && eAspect==kAttribAspect_BasicAbs)
					{
						F32 fHitChance = fResistTrue/fResist;
						PERFINFO_AUTO_START("KnockUp",1);
						if(fMag > 0 && (fHitChance >= 1.f || fHitChance > randomPositiveF32()))
						{
							AttribModKnockupParams *pKnockupParams = (AttribModKnockupParams*)(pmoddef->pParams);
							U32 uiTimeKnock = pmTimestamp(pmod->fPredictionOffset);
							F32 fProneTimer = ModKnockGetProneTime(iPartitionIdx, pmod, pKnockupParams->fTimer, eSource, pchar);

							pmKnockUpStart(	pchar->pEntParent,
											fMagNoResist,
											uiTimeKnock,
											pKnockupParams->bInstantFacePlant,
											!pKnockupParams->bOmitProne,
											fProneTimer,
											pKnockupParams->bIgnoreTravelTime);

#ifdef GAMESERVER
							if(pmod->fPredictionOffset)
							{
								EntityRef erTarget = entGetRef(pchar->pEntParent);
								ClientCmd_PowersPredictKnock(	eSource,
																erTarget,
																kAttribType_KnockUp,
																NULL,
																fMagNoResist,
																uiTimeKnock,
																pKnockupParams->bInstantFacePlant,
																!pKnockupParams->bOmitProne,
																fProneTimer,
																pKnockupParams->bIgnoreTravelTime);
							}
#endif
							character_ActInterrupt(iPartitionIdx, pchar, kPowerInterruption_Knock);
#ifdef GAMESERVER
							gslLogoff_Cancel(pchar->pEntParent, kLogoffCancel_CombatDamage);
#endif
							// Cancel everything
							character_ActAllCancelReason(iPartitionIdx,pchar,false,kAttribType_KnockUp);

							// Add the combat event for being Knocked
							character_CombatEventTrack(pchar, kCombatEvent_KnockIn);
							if(eSource)
								pchar->erRingoutCredit = entGetRef(eSource);
						}
						PERFINFO_AUTO_STOP();
					}
					else if(!pchar->bUnstoppable && !pchar->bUsingDoor && eAttrib==kAttribType_KnockBack && eAspect==kAttribAspect_BasicAbs)
					{
						PERFINFO_AUTO_START("KnockBack",1);

						if(fMag != 0)
						{
							F32 fHitChance = fResistTrue/fResist;
							U32 uiTimeKnock = pmTimestamp(pmod->fPredictionOffset);

							if(fHitChance >= 1.f || fHitChance > randomPositiveF32())
							{
								AttribModKnockbackParams *pKnockbackParams = (AttribModKnockbackParams*)pmoddef->pParams;
								F32 fProneTimer = ModKnockGetProneTime(iPartitionIdx, pmod, pKnockbackParams->fTimer, eSource, pchar);
												
								if(!ISZEROVEC3(pmod->vecSource))
								{
									Vec3 vecDir,vecTarget;
									entGetCombatPosDir(pchar->pEntParent,NULL,vecTarget,NULL);
									subVec3(vecTarget,pmod->vecSource,vecDir);

									if (vec3IsZeroXZ(vecDir))
									{	// if the target happened to be right on top of the pmod->vecSource
										// use the source's position to get a knock direction
										if (eSource)
										{
											Vec3 vSourcePos;
											entGetPos(eSource, vSourcePos);
											subVec3(vecTarget,vSourcePos,vecDir);
										}
									}

									pmKnockBackStart(	pchar->pEntParent,
														vecDir,
														fMagNoResist,
														uiTimeKnock,
														pKnockbackParams->bInstantFacePlant,
														!pKnockbackParams->bOmitProne,
														fProneTimer,
														pKnockbackParams->bIgnoreTravelTime);
#ifdef GAMESERVER
									if(pmod->fPredictionOffset)
									{
										EntityRef erTarget = entGetRef(pchar->pEntParent);
										ClientCmd_PowersPredictKnock(	eSource, 
																		erTarget,
																		kAttribType_KnockBack,
																		vecDir,
																		fMagNoResist,
																		uiTimeKnock,
																		pKnockbackParams->bInstantFacePlant,
																		!pKnockbackParams->bOmitProne,
																		fProneTimer,
																		pKnockbackParams->bIgnoreTravelTime);
									}
#endif
								}
								else
								{
									pmKnockUpStart(	pchar->pEntParent,
													1.f,
													uiTimeKnock,
													pKnockbackParams->bInstantFacePlant,
													!pKnockbackParams->bOmitProne,
													fProneTimer,
													pKnockbackParams->bIgnoreTravelTime);
#ifdef GAMESERVER
									if(pmod->fPredictionOffset)
									{
										EntityRef erTarget = entGetRef(pchar->pEntParent);
										ClientCmd_PowersPredictKnock(	eSource,
																		erTarget,
																		kAttribType_KnockUp,
																		NULL,
																		1.f,
																		uiTimeKnock,
																		pKnockbackParams->bInstantFacePlant,
																		!pKnockbackParams->bOmitProne,
																		fProneTimer,
																		pKnockbackParams->bIgnoreTravelTime);
									}
#endif
								}
								character_ActInterrupt(iPartitionIdx, pchar, kPowerInterruption_Knock);
#ifdef GAMESERVER
								gslLogoff_Cancel(pchar->pEntParent, kLogoffCancel_CombatDamage);
#endif
								// Cancel everything
								character_ActAllCancelReason(iPartitionIdx,pchar,false,kAttribType_KnockBack);

								// Add the combat event for being Knocked
								character_CombatEventTrack(pchar, kCombatEvent_KnockIn);

								if(eSource)
									pchar->erRingoutCredit = entGetRef(eSource);
							}
							else if(!ISZEROVEC3(pmod->vecSource))
							{
								Vec3 vecDir,vecTarget;
								entGetCombatPosDir(pchar->pEntParent,NULL,vecTarget,NULL);
								subVec3(vecTarget,pmod->vecSource,vecDir);
								pmPushStart(pchar->pEntParent,vecDir,fMag,uiTimeKnock);

#ifdef GAMESERVER
								if(pmod->fPredictionOffset)
								{
									EntityRef erTarget = entGetRef(pchar->pEntParent);
									ClientCmd_PowersPredictKnock(	eSource,
																	erTarget,
																	kAttribType_Repel,
																	vecDir,
																	fMag,
																	uiTimeKnock,
																	false,
																	false,
																	0.f,
																	true);
								}
#endif
								// Add the combat event for being Knocked
								character_CombatEventTrack(pchar, kCombatEvent_KnockIn);
								if(eSource)
									pchar->erRingoutCredit = entGetRef(eSource);
							}
						}
						PERFINFO_AUTO_STOP();
					}
					else if(!pchar->bUnstoppable && !pchar->bUsingDoor && eAttrib==kAttribType_Repel && eAspect==kAttribAspect_BasicAbs)
					{
						F32 fHitChance = fResistTrue/fResist;
						PERFINFO_AUTO_START("Repel",1);
						if(!ISZEROVEC3(pmod->vecSource)
							&& fMag != 0 
							&& (fHitChance >= 1.f || fHitChance > randomPositiveF32()))
						{
							U32 uiTimeKnock = pmTimestamp(pmod->fPredictionOffset);
							Vec3 vecDir,vecTarget;
							entGetCombatPosDir(pchar->pEntParent,NULL,vecTarget,NULL);
							subVec3(vecTarget,pmod->vecSource,vecDir);
							pmPushStart(pchar->pEntParent,vecDir,fMag,uiTimeKnock);

#ifdef GAMESERVER
							if(pmod->fPredictionOffset)
							{
								EntityRef erTarget = entGetRef(pchar->pEntParent);
								ClientCmd_PowersPredictKnock(	eSource,
																erTarget,
																kAttribType_Repel,
																vecDir,
																fMag,
																uiTimeKnock,
																false,
																false,
																0.f,
																true);
							}
#endif
							// Add the combat event for being Knocked
							character_CombatEventTrack(pchar, kCombatEvent_KnockIn);
							if(eSource)
								pchar->erRingoutCredit = entGetRef(eSource);

							character_CombatEventTrack(pchar, kCombatEvent_AttemptRepelIn);
							if(eSource && eSource->pChar)
								character_CombatEventTrack(eSource->pChar, kCombatEvent_AttemptRepelOut);
						}
						else if(fHitChance >= 1.f || fHitChance > randomPositiveF32())
						{
							character_CombatEventTrack(pchar, kCombatEvent_AttemptRepelIn);
							if(eSource && eSource->pChar)
								character_CombatEventTrack(eSource->pChar, kCombatEvent_AttemptRepelOut);
						}

						
						PERFINFO_AUTO_STOP();
					}
					else
					{
						// ... apply the mag
						*pf += fMag;


						switch (eAttrib)
						{
							// Track direct changes to hitpoints and power
							case kAttribType_HitPoints:
							case kAttribType_Power:
							{
								F32 fEffMag = fMag * -1;
								F32 fEffMagNoResist = fMagNoResist * -1;

								// Use an approximation of the percentage about to be dealt
								if(eAspect!=kAttribAspect_BasicAbs)
								{
									F32 fMax = eAttrib==kAttribType_HitPoints ? pchar->pattrBasic->fHitPointsMax : pchar->pattrBasic->fPowerMax;
									fEffMag *= fMax;
									fEffMagNoResist *= fMax;
									fMagAI *= fMax;
									if(eAspect==kAttribAspect_BasicFactNeg)
									{
										fEffMag *= -1;
										fEffMagNoResist *= -1;
										fMagAI *= -1;
									}
								}

								// Check "Damage" triggers (may also be heal triggers)
								ModCheckDamageTriggers(iPartitionIdx,pmod,pchar,eAttrib,fEffMag,fEffMagNoResist,pExtract);

								if(eAttrib==kAttribType_HitPoints)
								{
									// Update damage tracker (which will handle adding a combat tracker automatically)
									damageTracker_AddTick(iPartitionIdx,pchar,pmod->erOwner,pmod->erSource,pchar->pEntParent->myRef, fEffMag, fEffMagNoResist, eAttrib, pmod->uiApplyID, GET_REF(pmod->hPowerDef), pmoddef->uiDefIdx, NULL, pmod->eFlags|eFlagsAdded);

									if(fEffMagNoResist < 0)
									{
										character_CombatEventTrackInOut(pchar, kCombatEvent_HealIn, kCombatEvent_HealOut,
																		eSource, pmoddef->pPowerDef, pmoddef, 
																		-fEffMag, -fEffMagNoResist, NULL, NULL);
									}
								}
								else
								{
									// Directly add a combat tracker
									character_CombatTrackerAdd(pchar, pmoddef->pPowerDef, pmod->erOwner, pmod->erSource, NULL, eAttrib, fEffMag, 0, 0, 0, false);
								}
							} break;

							case kAttribType_Swinging:
							{
								ModProcessSwinging(pmod,pchar);
							} break;

							case kAttribType_Flight:
							{
								FlightParams *pParams = (FlightParams*)(pmod->pDef->pParams);
								if(pParams && pchar->pEntParent)
								{
#ifdef GAMESERVER
									bool bUseFakeRoll = !pParams->bDisableFakeRoll;
									if (!pParams->bDisableFakeRoll && pchar->pEntParent->aibase)
									{
										AIConfig* pConfig = aiGetConfig(pchar->pEntParent, pchar->pEntParent->aibase);
										if (pConfig)
										{
											bUseFakeRoll = !!pConfig->movementParams.bankWhenMoving;
										}
									}
									pmUpdateFlightParams(pchar->pEntParent, bUseFakeRoll, pParams->bIgnorePitch, pParams->bUseJumpBit, pParams->bConstantForward);
#endif
								}
							} break;

							case kAttribType_Root:
							{
								if (fMag > 0.f)
								{
									U32 uiRootTime = pmTimestamp(pmod->fPredictionOffset);
									if (!pchar->bIsRooted && 
										(!pchar->uiScheduledRootTime || uiRootTime < pchar->uiScheduledRootTime))
									{
										pchar->uiScheduledRootTime = uiRootTime;
									}
								}
								else
								{
									pchar->uiScheduledRootTime = 0;
								}
								
							} break;

							case kAttribType_Hold:
							{
								if (fMag > 0.f)
								{
									U32 uiHoldTime = pmTimestamp(pmod->fPredictionOffset);
									if (!pchar->bIsHeld && 
										(!pchar->uiScheduledHoldTime || uiHoldTime < pchar->uiScheduledHoldTime))
									{
										pchar->uiScheduledHoldTime = uiHoldTime;
									}
								}
								else
								{
									pchar->uiScheduledHoldTime = 0;
								}

							} break;


						}

					}
					PERFINFO_AUTO_STOP();
				}
				else if(IS_SPECIAL_ATTRIB(eAttrib) && eAttrib!=kAttribType_Null && !IS_SET_ATTRIB(eAttrib))
				{
					if(!bFoundMitigators && (eAttrib==kAttribType_KnockTo || eAttrib==kAttribType_Teleport))
					{
						character_ModGetMitigators(iPartitionIdx, pmod, pmoddef, pchar, &fResistTrue, &fResist, &fImmune, &fAvoid, &eFlagsMit);
						bFoundMitigators = true;
					}

					ModProcessSpecial(iPartitionIdx, pmod, pmoddef,pchar, fRate, piModes, fResistTrue, fResist, fImmune, fAvoid, pExtract);
				}

#ifdef GAMESERVER
				if (pmoddef->bNotifyGameEventOnApplication && !pmod->bNotifiedGameEvent)
				{
					Entity *entModOwner = pmod->erOwner ? entFromEntityRef(iPartitionIdx, pmod->erOwner) : eSource;
					if (!entModOwner)
						entModOwner = eSource;
					PERFINFO_AUTO_START("GameEventNotify",1);
					eventsend_RecordPowerAttribModApplied(entModOwner, pchar->pEntParent, pmoddef->pPowerDef, NULL);
					pmod->bNotifiedGameEvent = true;
					PERFINFO_AUTO_STOP();
				}
				if((!pmod->bNotifiedAI || pmoddef->fPeriod > 0.f) && 
					(!IS_DAMAGE_ATTRIBASPECT(eAttrib, eAspect) && 
					 !IS_HEALING_ATTRIBASPECT(eAttrib, eAspect)))
				{
					// Note: Healing and damage are dealt with in CharacterTickDamageTrackers
					//   to account for overhealing and overkilling
					F32 fThreatScale = 1;
					PERFINFO_AUTO_START("AINotify",1);
					if(eSource && eSource->pChar)
					{
						// TODO(JW): This is lazier than it should be (probably should be calculated at
						//  mod apply time), but we'll ignore that for now.
						fThreatScale = eSource->pChar->pattrBasic->fAIThreatScale;
					}
					aiFCNotify(pchar->pEntParent, pmod, pmoddef, fMagAI, fThreatScale);
					pmod->bNotifiedAI = true;
					PERFINFO_AUTO_STOP();
				}
				if(!pmod->bNotifiedPVP || pmoddef->fPeriod > 0.f)
				{
					pmod->bNotifiedPVP = true;
					gslPVPModNotify(eSource, pchar->pEntParent, pmod, pmoddef);
				}

				if (!pmod->bProcessedDisplayNameTraker && !pmoddef->bDerivedInternally && 
					(pmoddef->eFlags & kCombatTrackerFlag_ShowPowerDisplayName))
				{
					pmod->bProcessedDisplayNameTraker = true;
					character_CombatTrackerAdd(pchar, pmoddef->pPowerDef,
													pmod->erOwner, pmod->erSource, NULL,
													eAttrib, pmod->fMagnitude, 0.f, 
													kCombatTrackerFlag_ShowPowerDisplayName, 0, false);
				}
#endif
			}

			pmod->bPostFirstTickApply = true;

			PERFINFO_AUTO_STOP(); // ApplyThisTick
		}


		if(pmod->fDuration > 0.f && pmoddef->fPeriod != 0.0f)
		{
			character_SetSleep(pchar, pmod->fTimer);
		}
	}
	PERFINFO_AUTO_STOP(); // Step

	if(bPeriod && pmoddef->pExpiration && pmoddef->pExpiration->bPeriodic)
		character_ApplyModExpiration(iPartitionIdx,pchar,pmod,pExtract);

	if(pmoddef->bHasAnimFX)
	{
		PERFINFO_AUTO_START("AnimFX",1);
		if(bAnimFX)
			mod_AnimFXOn(iPartitionIdx,pmod,pchar);
		else
			mod_AnimFXOff(pmod,pchar);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
#endif
}




// Functions related to netsending of Attributes (defined in CombatConfig)

// Returns true if the Character needs to send Attributes
S32 character_AttribsNetCheckUpdate(Character *pchar)
{
	return !!g_CombatConfig.peAttribsNet && pchar->pattrBasic && pchar->pattrBasic->dirtyBit;
}

// Sends the Character's Attributes
void character_AttribsNetSend(Character *pchar, Packet *pak)
{
	S32 i;
	for(i=eaiSize(&g_CombatConfig.peAttribsNet)-1; i>=0; i--)
	{
		F32 *pf = F32PTR_OF_ATTRIB(pchar->pattrBasic,g_CombatConfig.peAttribsNet[i]);
		pktSendF32(pak,*pf);
	}
}

// Receives the Character's Attributes.  Safely consumes all the Attributes if there isn't a Character.
void character_AttribsNetReceive(Character *pchar, Packet *pak)
{
	S32 i;
	for(i=eaiSize(&g_CombatConfig.peAttribsNet)-1; i>=0; i--)
	{
		F32 f = pktGetF32(pak);
		if(pchar && pchar->pattrBasic)
		{
			F32 *pf = F32PTR_OF_ATTRIB(pchar->pattrBasic,g_CombatConfig.peAttribsNet[i]);
			*pf = f;
		}
	}
}

// Returns true if the Character needs to send innate Attributes
S32 character_AttribsInnateNetCheckUpdate(Character *pchar)
{
	return !!g_CombatConfig.peAttribsInnateNet && pchar->pInnateAccrualSet && pchar->pInnateAccrualSet->dirtyBit;
}

// Sends the Character's innate Attributes
void character_AttribsInnateNetSend(Character *pchar, Packet *pak)
{
	S32 i;
	for(i=eaiSize(&g_CombatConfig.peAttribsInnateNet)-1; i>=0; i--)
	{
		F32 *pf;
		AttribType offAttrib = g_CombatConfig.peAttribsInnateNet[i];
		AttribAccrualSet *pattrSet = pchar->pInnateAccrualSet;

#define ATR_INNATE_SEND(aspect) pf = F32PTR_OF_ATTRIB(&pattrSet->CharacterAttribs.##aspect##,offAttrib); pktSendF32(pak,*pf);

		ATR_INNATE_SEND(attrBasicAbs);
		ATR_INNATE_SEND(attrBasicFactPos);
		ATR_INNATE_SEND(attrBasicFactNeg);
		ATR_INNATE_SEND(attrStrBase);
		ATR_INNATE_SEND(attrStrFactPos);
		ATR_INNATE_SEND(attrStrFactNeg);
		ATR_INNATE_SEND(attrStrFactBonus);
		ATR_INNATE_SEND(attrStrMult);
		ATR_INNATE_SEND(attrStrAdd);
		ATR_INNATE_SEND(attrResTrue);
		ATR_INNATE_SEND(attrResBase);
		ATR_INNATE_SEND(attrResFactPos);
		ATR_INNATE_SEND(attrResFactNeg);
		ATR_INNATE_SEND(attrResFactBonus);
		ATR_INNATE_SEND(attrImmunity);
	}
}

// Receives the Character's innate Attributes.  Safely consumes all the Attributes if there isn't a Character.
void character_AttribsInnateNetReceive(Character *pchar, Packet *pak)
{
	S32 i;
	S32 bActivePlayer = false;

#ifdef GAMECLIENT
	bActivePlayer = pchar==characterActivePlayerPtr(); // We don't really want to blow away what the client has for itself
#endif

	if(pchar)
	{
		// Since this isn't normally on the client, make it up
		if(!pchar->pInnateAccrualSet)
			pchar->pInnateAccrualSet = StructCreate(parse_AttribAccrualSet);
		else if(!bActivePlayer)
			StructReset(parse_AttribAccrualSet, pchar->pInnateAccrualSet);
	}

	for(i=eaiSize(&g_CombatConfig.peAttribsInnateNet)-1; i>=0; i--)
	{
		F32 f, *pf;
		AttribType offAttrib = g_CombatConfig.peAttribsInnateNet[i];
		AttribAccrualSet *pattrSet = pchar && !bActivePlayer ? pchar->pInnateAccrualSet : NULL;

#define ATR_INNATE_RECV(aspect) f = pktGetF32(pak); if(pattrSet) { pf = F32PTR_OF_ATTRIB(&pattrSet->CharacterAttribs.##aspect##,offAttrib); *pf = f; }

		ATR_INNATE_RECV(attrBasicAbs);
		ATR_INNATE_RECV(attrBasicFactPos);
		ATR_INNATE_RECV(attrBasicFactNeg);
		ATR_INNATE_RECV(attrStrBase);
		ATR_INNATE_RECV(attrStrFactPos);
		ATR_INNATE_RECV(attrStrFactNeg);
		ATR_INNATE_RECV(attrStrFactBonus);
		ATR_INNATE_RECV(attrStrMult);
		ATR_INNATE_RECV(attrStrAdd);
		ATR_INNATE_RECV(attrResTrue);
		ATR_INNATE_RECV(attrResBase);
		ATR_INNATE_RECV(attrResFactPos);
		ATR_INNATE_RECV(attrResFactNeg);
		ATR_INNATE_RECV(attrResFactBonus);
		ATR_INNATE_RECV(attrImmunity);
	}
}





// Misc utility functions related to specific attributes
S32 character_ModsShieldsFull(Character *pchar)
{
	int i;
	for(i=eaSize(&pchar->ppModsShield)-1; i>=0; i--)
	{
		if(pchar->ppModsShield[i]->pFragility
			&& !nearf(pchar->ppModsShield[i]->pFragility->fHealth,pchar->ppModsShield[i]->pFragility->fHealthMax))
		{
			return false;
		}
	}
	return true;
}

bool character_AffectedBy(Character *pChar, AttribType eAttrib)
{
	if(eAttrib >= 0 && IS_NORMAL_ATTRIB(eAttrib))
	{
		if(pChar && *F32PTR_OF_ATTRIB(pChar->pattrBasic, eAttrib) > character_GetClassAttrib(pChar, kClassAttribAspect_Basic, eAttrib))
			return true;
		else
			return false;
	}
	else
		return false;
}

bool character_IsHeld(Character *pChar)
{
	if (!pChar)
		return false;
	else if (pChar->bIsHeld > 0)
		return true;
	else
		return false;
}

bool character_IsRooted(Character *pChar)
{
	if (!pChar)
		return false;
	else if (pChar->bIsRooted)
		return true;
	else
		return false;
}

bool character_IsDisabled(Character *pChar)
{
	if (!pChar)
		return false;
	else if (pChar->pattrBasic->fDisable > 0)
		return true;
	else
		return false;
}

static int FreeInnateAccrual(StashElement element)
{
	AttribAccrualSet *pSet = (AttribAccrualSet*)stashElementGetPointer(element);
	DestroyInnateAccrualSet(pSet);
	stashElementSetPointer(element,NULL);
	return 1;
}

// Dirties the various caches of Innate data when PowerDefs reload
void powerdefs_Reload_DirtyInnateCaches(void)
{
	stashForEachElement(s_hInnateAccrualSetsCritter,FreeInnateAccrual);
	stashTableClear(s_hInnateAccrualSetsCritter);
}

F32 character_GetSpeedCharge(int iPartitionIdx, Character *pchar, Power *ppow)
{
	// Get the charge speed of this power
	F32 fSpeed = character_PowerBasicAttrib(iPartitionIdx, pchar, ppow, kAttribType_SpeedCharge, 0);
	if(fSpeed <= 0)
	{
		// Minimum speed modifier is set to 1% to avoid the charge bar from halting completely or going backwards.
		// If we encounter this case, and the designers want maintaining to stop for some reason, change this,
		// but otherwise this should not happen
		Errorf("Charge speeds that are 0 or negative are not allowed. Charge speed was %f", fSpeed);
		fSpeed = 0.01;
	}

	return fSpeed;
}

F32 character_GetSpeedPeriod(int iPartitionIdx, Character *pchar, Power *ppow)
{
	// Get the period speed of this power
	F32 fSpeed = character_PowerBasicAttrib(iPartitionIdx, pchar, ppow, kAttribType_SpeedPeriod, 0);
	if(fSpeed <= 0)
	{
		// Minimum speed modifier is set to 1% to avoid activation from halting completely or going backwards.
		// If we encounter this case, and the designers want activating to stop for some reason, change this,
		// but otherwise this should not happen
		Errorf("Period speeds that are 0 or negative are not allowed. Period speed was %f", fSpeed);
		fSpeed = 0.01;
	}

	return fSpeed;
}

#include "AutoGen/AttribModFragility_h_ast.c"
#include "AutoGen/CharacterAttribs_h_ast.c"
#include "AutoGen/CharacterAttribsMinimal_h_ast.c"
#include "AutoGen/CharacterClass_h_ast.c"
#include "AutoGen/CombatEnums_h_ast.c"
