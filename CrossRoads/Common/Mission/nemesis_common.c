/***************************************************************************
*     Copyright (c) 2008-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "nemesis_common.h"

#include "AutoTransDefs.h"
#include "entity.h"
#include "EntitySavedData.h"
#include "fileutil.h"
#include "foldercache.h"
#include "Player.h"

#include "AutoGen/entEnums_h_ast.h"
#include "AutoGen/nemesis_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Global variables for each of the nemesis lists
NemesisPowerSetList g_NemesisPowerSetList;
NemesisMinionPowerSetList g_NemesisMinionPowerSetList;
NemesisMinionCostumeSetList g_NemesisMinionCostumeSetList;
NemesisPrices g_NemesisPrices;
NemesisConfig g_NemesisConfig;

const char **g_eaNemesisCostumeNames;

// --------------------------------------------------------------------------
// Nemesis utilities
// --------------------------------------------------------------------------

ContainerID player_GetPrimaryNemesisID(const Entity* pEnt)
{
	if (pEnt && pEnt->pPlayer && pEnt->pSaved)
	{
		ContainerID iNemesisID = 0;
		int i;
		for(i=eaSize(&pEnt->pPlayer->nemesisInfo.eaNemesisStates)-1; i>=0; --i)
		{
			if (pEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->eState == NemesisState_Primary)
			{
				return pEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID;
			}
		}
	}
	return 0;
}

// This returns a container subscription version of the ent, not the actual container
Entity* player_GetNemesisByID(const Entity* pEnt, ContainerID iNemesisID)
{
	if (pEnt && pEnt->pPlayer && pEnt->pSaved && iNemesisID)
	{
		int i;		
		for (i=eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i>=0; --i){
			Entity *pNemesisEnt = GET_REF(pEnt->pSaved->ppOwnedContainers[i]->hPetRef);
			if (pNemesisEnt && pNemesisEnt->pNemesis && pNemesisEnt->myContainerID == iNemesisID){
				return pNemesisEnt;
			}
		}
	}
	return NULL;
}

// Gets a player's Nemesis State by ID
PlayerNemesisState* player_GetNemesisStateByID(const Entity* pEnt, ContainerID iNemesisID)
{
	if (pEnt && pEnt->pPlayer && iNemesisID)
	{
		int i;		
		for (i=eaSize(&pEnt->pPlayer->nemesisInfo.eaNemesisStates)-1; i>=0; --i){
			if (pEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID == iNemesisID){
				return pEnt->pPlayer->nemesisInfo.eaNemesisStates[i];
			}
		}
	}
	return NULL;
}

// This returns a container subscription version of the ent, not the actual container
Entity* player_GetPrimaryNemesis(const Entity* pEnt)
{
	return player_GetNemesisByID(pEnt, player_GetPrimaryNemesisID(pEnt));
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pNemesis, ".Personality, .Fpowerhue, .Fminionpowerhue, .Pchpowerset, .Pchminionpowerset, .Pchminioncostumeset");
S32 nemesis_trh_GetCostToChange(ATH_ARG const NOCONST(Nemesis) *pNemesis, int iPlayerLevel, NemesisPersonality personality, const char *pchNemesisPowerSet, const char *pchMinionPowerSet, const char *pchMinionCostumeSet, F32 fPowerHue, F32 fMinionPowerHue)
{
	S32 cost = 0;
	if (NONNULL(pNemesis)){

		if (pNemesis->personality != personality){
			cost += g_NemesisPrices.personalityCost;
		}

		if (stricmp(pNemesis->pchPowerSet, pchNemesisPowerSet) != 0){
			cost += g_NemesisPrices.nemesisPowerSetCost;
		}

		if (stricmp(pNemesis->pchMinionPowerSet, pchMinionPowerSet) != 0){
			cost += g_NemesisPrices.minionPowerSetCost;
		}

		if (stricmp(pNemesis->pchMinionCostumeSet, pchMinionCostumeSet) != 0){
			cost += g_NemesisPrices.minionCostumeSetCost;
		}

		// Changing power hue is free if you are already changing the power set
		if (fPowerHue != pNemesis->fPowerHue){
			cost += g_NemesisPrices.nemesisPowerHueCost;
		}

		// Changing power hue is free if you are already changing the power set
		if (fMinionPowerHue != pNemesis->fMinionPowerHue && !stricmp(pNemesis->pchMinionPowerSet, pchMinionPowerSet)){
			cost += g_NemesisPrices.minionPowerHueCost;
		}

		// Apply level multiplier
		if (iPlayerLevel > 0 && iPlayerLevel <= eafSize(&g_NemesisPrices.eafLevelMultipliers)){
			cost *= g_NemesisPrices.eafLevelMultipliers[iPlayerLevel-1];
		} else if (eafSize(&g_NemesisPrices.eafLevelMultipliers)){
			cost *= g_NemesisPrices.eafLevelMultipliers[eafSize(&g_NemesisPrices.eafLevelMultipliers)-1];
		}
	}
	return cost;
}

// For debug commands only; shouldn't usually use the name for anything
ContainerID nemesis_FindIDFromName(Entity *pEnt, const char *pchNemesisName)
{
	if (pEnt && pEnt->pPlayer){
		int i, n = eaSize(&pEnt->pPlayer->nemesisInfo.eaNemesisStates);
		for (i = 0; i < n; i++){
			Entity *pNemesisEnt = player_GetNemesisByID(pEnt, pEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID);
			if (pNemesisEnt){
				const char *pchName = entGetPersistedName(pNemesisEnt);
				if (!stricmp(pchName, pchNemesisName)){
					return pEnt->pPlayer->nemesisInfo.eaNemesisStates[i]->iNemesisID;
				}
			}
		}
	}
	return 0;
}

// --------------------------------------------------------------------------
// Nemesis data load logic
// --------------------------------------------------------------------------

static void NemesisPowerSetsReload(const char *pchRelPath, int UNUSED_when)
{
	fileWaitForExclusiveAccess(pchRelPath);
	if (g_NemesisPowerSetList.sets) {
		int i;
		for (i = eaSize(&g_NemesisPowerSetList.sets)-1; i >= 0; i--) {
			StructDestroy(parse_NemesisPowerSet, g_NemesisPowerSetList.sets[i]);
		}
		eaClear(&g_NemesisPowerSetList.sets);
	}
	ParserLoadFiles(NULL, "defs/config/NemesisPowerSets.def", "NemesisPowerSets.bin", PARSER_OPTIONALFLAG, parse_NemesisPowerSetList, &g_NemesisPowerSetList);
}

static void NemesisPowerSets_Load()
{
	loadstart_printf("Loading Nemesis Power Sets...");
	ParserLoadFiles(NULL, "defs/config/NemesisPowerSets.def", "NemesisPowerSets.bin", PARSER_OPTIONALFLAG, parse_NemesisPowerSetList, &g_NemesisPowerSetList);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE | FOLDER_CACHE_CALLBACK_DELETE, "defs/config/NemesisPowerSets.def", NemesisPowerSetsReload);
	loadend_printf(" done.");
}

static void NemesisMinionPowerSetsReload(const char *pchRelPath, int UNUSED_when)
{
	fileWaitForExclusiveAccess(pchRelPath);
	if (g_NemesisMinionPowerSetList.sets) {
		int i;
		for (i = eaSize(&g_NemesisMinionPowerSetList.sets)-1; i >= 0; i--) {
			StructDestroy(parse_NemesisMinionPowerSet, g_NemesisMinionPowerSetList.sets[i]);
		}
		eaClear(&g_NemesisMinionPowerSetList.sets);
	}
	ParserLoadFiles(NULL, "defs/config/NemesisMinionPowerSets.def", "NemesisMinionPowerSets.bin", PARSER_OPTIONALFLAG, parse_NemesisMinionPowerSetList, &g_NemesisMinionPowerSetList);
}

static void NemesisMinionPowerSets_Load()
{
	loadstart_printf("Loading Nemesis Minion Power Sets...");
	ParserLoadFiles(NULL, "defs/config/NemesisMinionPowerSets.def", "NemesisMinionPowerSets.bin", PARSER_OPTIONALFLAG, parse_NemesisMinionPowerSetList, &g_NemesisMinionPowerSetList);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE | FOLDER_CACHE_CALLBACK_DELETE, "defs/config/NemesisMinionPowerSets.def", NemesisMinionPowerSetsReload);
	loadend_printf(" done.");
}

static void NemesisMinionCostumeSetsReload(const char *pchRelPath, int UNUSED_when)
{
	fileWaitForExclusiveAccess(pchRelPath);
	if (g_NemesisMinionCostumeSetList.sets) {
		int i;
		for (i = eaSize(&g_NemesisMinionCostumeSetList.sets)-1; i >= 0; i--) {
			StructDestroy(parse_NemesisMinionCostumeSet, g_NemesisMinionCostumeSetList.sets[i]);
		}
		eaClear(&g_NemesisMinionCostumeSetList.sets);
	}
	ParserLoadFiles(NULL, "defs/config/NemesisMinionCostumeSets.def", "NemesisMinionCostumeSets.bin", PARSER_OPTIONALFLAG, parse_NemesisMinionCostumeSetList, &g_NemesisMinionCostumeSetList);

	{
		S32 i;
		eaClear(&g_eaNemesisCostumeNames);
		for(i= 0; i < eaSize(&g_NemesisMinionCostumeSetList.sets); ++i)
		{
			if(g_NemesisMinionCostumeSetList.sets[i] && g_NemesisMinionCostumeSetList.sets[i]->pcName)
			{
				eaPush(&g_eaNemesisCostumeNames, g_NemesisMinionCostumeSetList.sets[i]->pcName);
			}
		}
	}

}

static void NemesisMinionCostumeSets_Load()
{
	S32 i;
	loadstart_printf("Loading Nemesis Minion Costume Sets...");
	ParserLoadFiles(NULL, "defs/config/NemesisMinionCostumeSets.def", "NemesisMinionCostumeSets.bin", PARSER_OPTIONALFLAG, parse_NemesisMinionCostumeSetList, &g_NemesisMinionCostumeSetList);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE | FOLDER_CACHE_CALLBACK_DELETE, "defs/config/NemesisMinionCostumeSets.def", NemesisMinionCostumeSetsReload);

	for(i= 0; i < eaSize(&g_NemesisMinionCostumeSetList.sets); ++i)
	{
		if(g_NemesisMinionCostumeSetList.sets[i] && g_NemesisMinionCostumeSetList.sets[i]->pcName)
		{
			eaPush(&g_eaNemesisCostumeNames, g_NemesisMinionCostumeSetList.sets[i]->pcName);
		}
	}

	loadend_printf(" done.");
}

static void NemesisConfigReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Nemesis Config...");
	fileWaitForExclusiveAccess(pchRelPath);
	ParserLoadFiles(NULL, "defs/config/NemesisConfig.def", "NemesisConfig.bin", PARSER_OPTIONALFLAG, parse_NemesisConfig, &g_NemesisConfig);
	loadend_printf(" done.");
}

static void NemesisConfigLoad()
{
	loadstart_printf("Loading Nemesis Config...");
	ParserLoadFiles(NULL, "defs/config/NemesisConfig.def", "NemesisConfig.bin", PARSER_OPTIONALFLAG, parse_NemesisConfig, &g_NemesisConfig);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE | FOLDER_CACHE_CALLBACK_DELETE, "defs/config/NemesisConfig.def", NemesisConfigReload);

	loadend_printf(" done.");
}

// Nemesis minion power sets and costumes get used in static checking functions the AI can call,
// so need to be defined before the rest of critters
AUTO_STARTUP(Nemesis) ASTRT_DEPS(Powers);
void nemesis_Load(void)
{
	NemesisPowerSets_Load();
	NemesisMinionPowerSets_Load();
	NemesisMinionCostumeSets_Load();
	NemesisConfigLoad();

	loadstart_printf("Loading Nemesis Prices...");
	ParserLoadFiles(NULL, "defs/config/NemesisPrices.def", "NemesisPrices.bin", PARSER_OPTIONALFLAG, parse_NemesisPrices, &g_NemesisPrices);
	loadend_printf("done.");
}

#include "AutoGen/nemesis_common_h_ast.c"