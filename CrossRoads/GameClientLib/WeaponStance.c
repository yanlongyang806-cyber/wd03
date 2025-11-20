
#include "stdtypes.h"
#include "CostumeCommon.h"
#include "error.h"
#include "WeaponStance.h"
#include "ResourceManager.h"
#include "AutoGen/WeaponStance_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hWeaponStanceDict;

AUTO_STARTUP(TailorWeaponStance) ASTRT_DEPS(EntityCostumes, Powers, PowerAnimFX);
void TailorWeaponStance_load(void)
{
	loadstart_printf("Loading tailor weapon stances...");
	g_hWeaponStanceDict = RefSystem_RegisterSelfDefiningDictionary("TailorWeaponStance", false, parse_TailorWeaponStance, true, true, NULL);
	resLoadResourcesFromDisk(g_hWeaponStanceDict, WEAPONSTANCE_BASE_DIR, WEAPONSTANCE_EXTENSION, "WeaponStance.bin", PARSER_OPTIONALFLAG | PARSER_CLIENTSIDE);
	loadend_printf(" done (%d tailor weapon stances)", RefSystem_GetDictionaryNumberOfReferents(g_hWeaponStanceDict));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(WeaponStanceName);
const char *exprWeaponStanceName(const char *pchBone)
{
	TailorWeaponStance *pStance = RefSystem_ReferentFromString(g_hWeaponStanceDict, pchBone);
	const char *pchResult = NULL;
	if (pStance && pStance->pDefaultName)
		pchResult = TranslateDisplayMessage(*pStance->pDefaultName);
	return pchResult ? pchResult : "";
}

TailorWeaponStance* WeaponStace_GetStanceForBone(PCBoneDef* pBoneDef)
{
	// The Weapon Stances use PCBoneDefs as thier key. 
	// This was done becuase in Champs the stance we want the character to stand in 
	// is based on which weapon they're holding, and we can tell the weapon by which 
	// bone is being used. There is one bone per type of weapon. 
	//
	// This can be changed in the future if we decide to use a different method to 
	// determine the desired weapon to display. 
	return pBoneDef ? RefSystem_ReferentFromString(g_hWeaponStanceDict, pBoneDef->pcName) : NULL;
}

#include "AutoGen/WeaponStance_h_ast.c"