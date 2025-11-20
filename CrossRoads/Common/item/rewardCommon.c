

#include "entCritter.h"
#include "team.h"
#include "Entity.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameAccountData\GameAccountData.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"
#include "ResourceManager.h"
#include "rewardCommon.h"
#include "Expression.h"
#include "GameBranch.h"
#include "logging.h"
#include "StringCache.h"
#include "Character.h"
#include "CharacterClass.h"
#include "wlGroupPropertyStructs.h"
#include "PowerVars.h"
#include "Player.h"
#include "AutoGen/rewardCommon_h_ast.h"
#include "AutoGen/team_h_ast.h"

// The names of all reward tags
RewardTags g_RewardTags;

DefineContext *s_pDefineRewardTags = NULL;
// Enum table for the reward tags
StaticDefineInt RewardTagsEnum[] =
{
	DEFINE_INT

	DEFINE_EMBEDDYNAMIC_INT(s_pDefineRewardTags)

	DEFINE_END
};

DefineContext *g_RewardExtraNumericTypes = NULL;
DefineContext *g_RewardGatedTypes = NULL;

extern ParseTable parse_Character[];
#define TYPE_parse_Character Character

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hRewardValTableDict;

extern ExprContext *g_pRewardNonPlayerContext;
extern ExprContext *g_pRewardPlayerContext;

static REF_TO(RewardValTable) s_XPRequiredTableHandle;
RewardConfig g_RewardConfig;

extern DictionaryHandle g_hPlayerCostumeDict;

AUTO_STARTUP(RewardsCommon);
void rewardCommon_InitContext(void)
{

}

static void rewardCommon_LifetimeFixup(void)
{
	S32 i, j;
	eaClear(&g_RewardConfig.eaLifetimeRewardsList);
	
	for(i = 0; i < eaSize(&g_RewardConfig.lifetimeRewards.eaLifetimeReward); ++i)
	{
		if(g_RewardConfig.lifetimeRewards.eaLifetimeReward[i]->pchLifetimeRewardType != NULL && g_RewardConfig.lifetimeRewards.eaLifetimeReward[i]->pchLifetimeRewardType[0] != 0)
		{
			S32 idx = -1;
			for(j = 0; j < eaSize(&g_RewardConfig.eaLifetimeRewardsList); ++j)
			{
				if(eaSize(&g_RewardConfig.eaLifetimeRewardsList[j]->eaLifetimeReward) > 0 &&
					stricmp(g_RewardConfig.eaLifetimeRewardsList[j]->eaLifetimeReward[0]->pchLifetimeRewardType, g_RewardConfig.lifetimeRewards.eaLifetimeReward[i]->pchLifetimeRewardType) == 0)
				{
					idx = j;
					break;				
				}
			}		
			
			if(idx >= 0)
			{
				LifetimeRewardStruct *pClone = StructClone(parse_LifetimeRewardStruct, g_RewardConfig.lifetimeRewards.eaLifetimeReward[i]);
				// already have this one
				eaPush(&g_RewardConfig.eaLifetimeRewardsList[idx]->eaLifetimeReward, pClone);
			}
			else
			{
				LifetimeRewardStruct *pClone = StructClone(parse_LifetimeRewardStruct, g_RewardConfig.lifetimeRewards.eaLifetimeReward[i]);
				LifetimeRewardsInfo *pNew = StructCreate(parse_LifetimeRewardsInfo);
				
				eaPush(&g_RewardConfig.eaLifetimeRewardsList, pNew);
				eaPush(&pNew->eaLifetimeReward, pClone);
				
			}
		}
	}
}

// Loads the reward tags
AUTO_STARTUP(RewardTagsLoad);
void rewardTags_Load(void)
{
	int i, s;
	char * pchTemp = NULL;

	loadstart_printf("Loading reward tags...");

	//do non shared memory load
	ParserLoadFiles(NULL, "defs/rewards/rewardtags.rewardtags", "rewardtags.bin", PARSER_OPTIONALFLAG, parse_RewardTags, &g_RewardTags);

	s_pDefineRewardTags = DefineCreate();

	estrStackCreateSize(&pchTemp, 20);
	s = eaSize(&g_RewardTags.tags);

	for(i = 0; i < s; i++)
	{
		estrPrintf(&pchTemp, "%d", i + 1);
		DefineAdd(s_pDefineRewardTags, g_RewardTags.tags[i], pchTemp);
	}

	estrDestroy(&pchTemp);

	loadend_printf(" done (%d reward tags).",i);
}

// Set the enum values for RewardGatedTypes, must be before loading g_RewardConfig
// currently not referenced by g_RewardConfig but in case it ever is it is loaded here
static void rewardCommon_LoadGatedTypes(void)
{
	S32 i;
	RewardGatedTypeNames Names = {0};

	g_RewardGatedTypes = DefineCreate();

	loadstart_printf("Loading RewardGatedNames...");

	ParserLoadFiles(NULL, "defs/config/RewardGatedNames.def", "RewardGatedNames.bin", PARSER_OPTIONALFLAG, parse_RewardGatedTypeNames, &Names);

	for(i=0; i < eaSize(&Names.eaNames); ++i)
	{
		if(Names.eaNames[i]->pcName && Names.eaNames[i]->pcName[0])
		{
			DefineAddInt(g_RewardGatedTypes, Names.eaNames[i]->pcName, RewardGatedType_FirstGameSpecific + i);				
		}
	}

	StructDeInit(parse_RewardGatedTypeNames, &Names);

	loadend_printf("Done.");

}

// Loads the reward tags
AUTO_STARTUP(RewardGatedLoad);
void rewardGated_Load(void)
{
	rewardCommon_LoadGatedTypes();
}

AUTO_STARTUP(RewardsCommon) ASTRT_DEPS(RewardTagsLoad, RewardGatedLoad, ItemTags, EntityCostumes);
void rewardCommon_Load(void)
{
	char *estrSharedMemory = NULL;
	char *pcBinFile = NULL;
	char *pcBuffer = NULL;
	char *estrFile = NULL;
	S32 i;
	RewardExtraNumericTable s_RewardNumericExtraNames = {0};

	// reward extra numerics, must be before reward config is loaded
	g_RewardExtraNumericTypes = DefineCreate();

	ParserLoadFiles(NULL, "defs/rewards/rewardextranumerics.def", "rewardextranumerics.bin", PARSER_OPTIONALFLAG, parse_RewardExtraNumericTable, &s_RewardNumericExtraNames);

	for(i=0; i < eaSize(&s_RewardNumericExtraNames.eRewardNumericTableNames); ++i)
	{
		if(s_RewardNumericExtraNames.eRewardNumericTableNames[i]->pcRewardExtraNumericTableName && s_RewardNumericExtraNames.eRewardNumericTableNames[i]->pcRewardExtraNumericTableName[0])
		{
			DefineAddInt(g_RewardExtraNumericTypes, s_RewardNumericExtraNames.eRewardNumericTableNames[i]->pcRewardExtraNumericTableName, kRewardValueType_FirstGameSpecific + i);				
		}
	}

	StructDeInit(parse_RewardExtraNumericTable, &s_RewardNumericExtraNames);

	SET_HANDLE_FROM_STRING(g_hRewardValTableDict, "XP_REQUIRED", s_XPRequiredTableHandle);

	loadstart_printf("Loading RewardConfig...");
	MakeSharedMemoryName(GameBranch_GetFilename(&pcBinFile, "RewardConfig.bin"),&estrSharedMemory);
	estrPrintf(&estrFile, "defs/rewards/%s", GameBranch_GetFilename(&pcBuffer, "RewardConfig.def"));
	ParserLoadFilesShared(estrSharedMemory, NULL, estrFile, pcBinFile, PARSER_OPTIONALFLAG, parse_RewardConfig, &g_RewardConfig);
	estrDestroy(&estrFile);
	
	//set up type-to-costume stash
	if (g_RewardConfig.TypeCostumeMappings.eaTypeToCostume)
	{
		g_RewardConfig.TypeCostumeMappings.stTypeToCostume = stashTableCreateInt(eaSize(&g_RewardConfig.TypeCostumeMappings.eaTypeToCostume));
		for (i = 0; i < eaSize(&g_RewardConfig.TypeCostumeMappings.eaTypeToCostume); i++)
		{
			//0 can't be used as a key :(
			stashIntAddPointer(g_RewardConfig.TypeCostumeMappings.stTypeToCostume, g_RewardConfig.TypeCostumeMappings.eaTypeToCostume[i]->eType+1, g_RewardConfig.TypeCostumeMappings.eaTypeToCostume[i], false);
		}
	}

	rewardCommon_LifetimeFixup();

	loadend_printf("Done.");

	estrDestroy(&pcBuffer);
	estrDestroy(&pcBinFile);
	estrDestroy(&estrSharedMemory);
}

AUTO_RUN;
void RegisterRewardTableDict(void)
{
	// Set up reference dictionaries
	g_hRewardTableDict = RefSystem_RegisterSelfDefiningDictionary("RewardTable",false, parse_RewardTable, true, true, NULL);

	resDictSetDisplayName(g_hRewardTableDict, "Reward Table", "Reward Tables", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictManageValidation(g_hRewardTableDict, rewardTableResValidateCB);
		resDictProvideMissingResources(g_hRewardTableDict);
		resDictGetMissingResourceFromResourceDBIfPossible((void*)g_hRewardTableDict);

		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hRewardTableDict, ".Name", ".Scope", ".Tags", ".Notes", NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hRewardTableDict, 8, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(g_hRewardTableDict);

	// Also register some Enums for static checking
	RegisterNamedStaticDefine(RewardTagsEnum, "RewardTag");
}


AUTO_RUN;
void RegisterRewardValTableDict(void)
{
	// Set up reference dictionaries
	g_hRewardValTableDict = RefSystem_RegisterSelfDefiningDictionary("RewardValTable",false, parse_RewardValTable, true, true, NULL);

	resDictSetDisplayName(g_hRewardValTableDict, "Reward Value Table", "Reward Value Tables", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hRewardValTableDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hRewardValTableDict, ".Name", ".Scope", NULL, ".Notes", NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hRewardValTableDict, 8, false, resClientRequestSendReferentCommand);
	}
}

bool reward_BagIsMyDrop(Entity* pEnt, InventoryBag* pBag)
{
	int i;
	for (i = 0; i < eaSize(&pBag->pRewardBagInfo->peaLootOwners); i++)
	{
		CritterLootOwner *pLootOwner = pBag->pRewardBagInfo->peaLootOwners[i];

		if (!pLootOwner)
			continue;

		switch (pLootOwner->eOwnerType)
		{
			xcase kRewardOwnerType_Player:
				if (ea32Contains(&pLootOwner->peaiOwnerIDs, pEnt->myContainerID))
					return true;

			xcase kRewardOwnerType_Team:
				if (pEnt->pTeam && ea32Contains(&pLootOwner->peaiOwnerIDs, pEnt->pTeam->iTeamID))
					return true;

			xcase kRewardOwnerType_TeamLeader:
				if (pEnt->pTeam && ea32Contains(&pLootOwner->peaiOwnerIDs, pEnt->pTeam->iTeamID))
				{
					Team *pTeam = GET_REF(pEnt->pTeam->hTeam);
					if (pTeam && pTeam->pLeader && pTeam->pLeader->iEntID == pEnt->myContainerID)
						return true;
				}

			xcase kRewardOwnerType_AllPlayers:
				if (pEnt->pPlayer)
					return true;

			xcase kRewardOwnerType_AllEnts:
				//allow all ents
				return true;

			xcase kRewardOwnerType_Enemies:
				//!!!!  check here to see if this ent is an enemy,  I have not idea what this check is
				// for now will just assume it is an ent with no player
				if (!pEnt->pPlayer)
					return true;

			xdefault:
				break;
		}
	}
	return false;
}

//determine if this ent is allowed to access this drop
bool reward_MyDrop(Entity *pEnt, Entity *pLootEnt)
{
	if (!pEnt ||
		!pLootEnt)
	{
		//don't have enough data to determine, just say no  <-- scrooge!
		return false;
	}

	if (!inv_HasLoot(pLootEnt))
		return true;

	FOR_EACH_IN_EARRAY(pLootEnt->pCritter->eaLootBags, InventoryBag, pBag)
	{
		if (reward_BagIsMyDrop(pEnt, pBag))
			return true;
	}
	FOR_EACH_END

	return false;
}

S32 LevelingNumericFromLevel(S32 iLevel)
{
	S32 iXP = 0;
	RewardValTable *pXPRequiredTable = GET_REF(s_XPRequiredTableHandle);
	
	if(pXPRequiredTable)
	{
		int iIndex = CLAMP_TAB_LEVEL(USER_TO_TAB_LEVEL(iLevel));

		// let's not index past the end of the array
		if (iIndex >= ea32Size(&pXPRequiredTable->Val))
			return 1000000000;

		iXP = pXPRequiredTable->Val[iIndex];
	}

	return iXP;
}

AUTO_TRANS_HELPER_SIMPLE;
S32 LevelFromLevelingNumeric(S32 iValue)
{
	S32 iLevel = 1;
	RewardValTable *pXPRequiredTable = GET_REF(s_XPRequiredTableHandle);

	if (pXPRequiredTable)
	{
		for (iLevel = 1; iLevel < NUM_PLAYER_LEVELS; iLevel++)
		{
			int iIndex = CLAMP_TAB_LEVEL(USER_TO_TAB_LEVEL(iLevel+1));
			if (iIndex >= ea32Size(&pXPRequiredTable->Val))
				break;
			if (iValue < pXPRequiredTable->Val[iIndex])
				break;
		}
	}
	
	return iLevel;
}

//dummy startup routine so that I can set the client reward data dependencies in a single place
AUTO_STARTUP(ClientReward) ASTRT_DEPS(AlgoTablesCommon);
void ClientRewardStartup(void)
{
}

AUTO_EXPR_FUNC(reward) ACMD_NAME(EntGetPointbuyValue);
int exprFuncEntGetPointBuyValue(ExprContext* context)
{
	// Point buy currently hard-coded to always be level, I guess.
	MultiVal * pLevel = exprContextGetSimpleVar(g_pRewardNonPlayerContext, "iLevel");
	if (pLevel)
	{
		return pLevel->int32;
	}
	else
	{
		return -1;
	}
}

//Gets the list of players who are eligible for loot.
AUTO_EXPR_FUNC(reward) ACMD_NAME("GetLootParty"); 
ExprFuncReturnVal exprFuncGetLootParty(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut)
{
#ifndef GAMECLIENT
	int i;
	KillCreditTeam *team_credit;
	Entity* lootEnt;
	eaClear(peaEntsOut);
	team_credit = exprContextGetVarPointer(pContext, "TeamCredit", parse_KillCreditTeam);
	if (team_credit)
	{
        for (i = 0; i < eaSize(&(team_credit->eaMembers)); i++)
            eaPushIfNotNull(peaEntsOut, entFromEntityRef(iPartitionIdx, team_credit->eaMembers[i]->entRef));
	}
	else if (exprContextGetParseTable(pContext) == parse_Entity)
	{
		lootEnt = exprContextGetUserPtr(pContext, parse_Entity);
		eaPushIfNotNull(peaEntsOut, lootEnt);
	}
	else
	{
		return ExprFuncReturnError;
	}
#endif
	return ExprFuncReturnFinished;
}

//Gets the list of players who are eligible for loot.
AUTO_EXPR_FUNC(reward) ACMD_NAME("GetLooterClass"); 
const char* exprFuncGetLooterClass(ExprContext *pContext)
{
	Entity* lootEnt;
	if (exprContextGetParseTable(pContext) == parse_Entity)
	{
		lootEnt = exprContextGetUserPtr(pContext, parse_Entity);
		
		if (lootEnt && lootEnt->pChar)
		{
			CharacterClass *pClass = GET_REF(lootEnt->pChar->hClass);
			if (pClass && pClass->pchName)
				return pClass->pchName;
			else
				return REF_STRING_FROM_HANDLE(lootEnt->pChar->hClass);
		}
	}
	return "";
}

// This is used by CO to see if a characterpath has a power (superstat) at index N in a suggested node
// Since this is a CO function, it will only check the Primary character path, since that's all CO uses.
AUTO_EXPR_FUNC(reward, player) ACMD_NAME("RewardEntCharPathHasPowerAtIndex");
bool RewardEntCharPathHasPowerAtIndex(ExprContext *pContext, SA_PARAM_OP_VALID const char *pcSuperStat, S32 iIndex)
{
	Entity* pEntity = exprContextGetSelfPtr(pContext);
	if(pEntity && pcSuperStat && pEntity->pChar)
	{
		CharacterPath *pPath = entity_GetPrimaryCharacterPath(pEntity);
		if(pPath)
		{
			S32 i,j;
			for(i = 0; i < eaSize(&pPath->eaSuggestedPurchases); ++i)
			{
				for(j = 0; j <eaSize(&pPath->eaSuggestedPurchases[i]->eaChoices); ++j)
				{
					if(iIndex >= 0 && iIndex < eaSize(&pPath->eaSuggestedPurchases[i]->eaChoices[j]->eaSuggestedNodes))
					{
						PTNodeDef *pNodeDef = GET_REF(pPath->eaSuggestedPurchases[i]->eaChoices[j]->eaSuggestedNodes[iIndex]->hNodeDef);
						if(pNodeDef)
						{
							if(stricmp(pNodeDef->pchName, pcSuperStat) == 0)
							{
								return true;
							}
						}
					}
				}
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(reward, player, entity) ACMD_NAME("RewardEntHasPower");
bool RewardEntHasPower(ExprContext *pContext, SA_PARAM_OP_VALID const char *pcPower)
{
	Entity* pEntity = exprContextGetSelfPtr(pContext);
	if(pEntity && pcPower && pEntity->pChar)
	{
		Character *pChar = pEntity->pChar;
		CharacterClass *pClass = character_GetClassCurrent(pChar);
		PowerDef *pPowerDef = powerdef_Find(pcPower);
		if(pPowerDef)
		{
			Power *pPow = character_FindPowerByDef(pChar, pPowerDef);
			if(pPow)
			{
				return true;
			}
		}
		else
		{
			ErrorFilenamef(exprContextGetBlameFile(pContext), "RewardEntHasPower: No such power %s", pcPower);
		}
	}

	return false;
}


AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void PrintToLootLog(char* stringToPrint)
{
#ifdef GAMECLIENT
	logDirectWrite("itemlog.log",stringToPrint);
#endif
}

int FindOriginType(const OriginPickupTypeStruct *a, const RewardContextType* b)
{
	return (a && b) ? (a->eOrigin == *b) : 0;
}

RewardPickupType reward_GetPickupTypeFromRewardOrigin(const RewardTable* pTable, RewardContextType eType)
{
	OriginPickupTypeStruct* pFound = NULL;
	int idx;
	idx = eaFindCmp(&g_RewardConfig.OriginMappings.eaOriginToPickupType, &eType, FindOriginType);
	pFound = (idx >= 0) ? g_RewardConfig.OriginMappings.eaOriginToPickupType[idx] : NULL;
	if (!pFound)
		Errorf("Reward table %s wanted to use \"FromOrigin\" pickuptype, but origin %s not found in rewardconfig.def. If you don't understand this error, ask cmiller.", pTable->pchName, StaticDefineIntRevLookup(RewardContextTypeEnum, eType));
	return pFound ? pFound->ePickup : kRewardPickupType_None;
}

S32 Reward_GetGatedIndexTransacted(RewardGatedDataInOut *pRewardGatedData, S32 gatedType)
{
	// only check if a pointer has been passed in
	if(pRewardGatedData)
	{
		S32 i;
		RewardGatedInfo *pRewardGatedInfo = eaIndexedGetUsingInt(&g_RewardConfig.eRewardGateInfo, gatedType);

		if(!pRewardGatedInfo)
		{
			// errorf ?
			return 0;
		}

		// this type has been accessed, increment count after the reward tables (and all of their children) for mission transaction have been checked
		eaiPushUnique(&pRewardGatedData->eaGateTypesChanged, gatedType);

		for(i = 0; i < eaiSize(&pRewardGatedData->eaCurrentGatedType);++i)
		{
			if(pRewardGatedData->eaCurrentGatedType[i] == gatedType)			
			{
				// if this every crashes that means that this structure wasn't initialized correctly as the arrays are supposed line up one to one
				return pRewardGatedData->eaCurrentGatedValues[i];
			}
		}
	}

	return 0;
}

// Gets index, on server it will do a reset if the block has changed
S32 Reward_GetGatedIndex(Entity *pPlayerEnt, S32 gatedType)
{
	RewardGatedInfo *pRewardGatedInfo;
	RewardGatedTypeData *pPlayerGated;
	if(!pPlayerEnt || !pPlayerEnt->pPlayer)
	{
		return -1;
	}

	pPlayerGated = eaIndexedGetUsingInt(&pPlayerEnt->pPlayer->eaRewardGatedData, gatedType);
	pRewardGatedInfo = eaIndexedGetUsingInt(&g_RewardConfig.eRewardGateInfo, gatedType);

	if(pPlayerGated && pRewardGatedInfo)
	{
		U32 tm = timeServerSecondsSince2000();
		U32 uBlock = 0;
		U32 uBlockChar = 0;

		if(pRewardGatedInfo->uHoursPerBlock)
		{
			if (pRewardGatedInfo->bGateFromPlayerGrantTime)
			{
				uBlockChar = 0; // assume that whenever the character started was 0, since we don't really care
				uBlock = (tm-pPlayerGated->uTimeSet) / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
			}
			else
			{
				uBlock = tm / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
				uBlockChar = pPlayerGated->uTimeSet / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
			}
		}

		if(uBlockChar == uBlock)
		{
			// return number of times already done this block
			if(pRewardGatedInfo->uNumberOfTimesToIncrement <= 1)
			{
				return pPlayerGated->uNumTimes;
			}

			// rounded down
			return pPlayerGated->uNumTimes / pRewardGatedInfo->uNumberOfTimesToIncrement;
		}
#ifdef GAMESERVER
		else
		{
			// Reset time as this is a new block, allow correct incrementing after it is done
			pPlayerGated->uNumTimes = 0;
			pPlayerGated->uTimeSet = tm;
			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, true);
		}
#endif
	}

	// If not in the block data for the character or the config is zero
	return 0;
}

bool reward_SetLootEntCostumeRefsForItem(Item* pItem, Entity* pEnt)
{
#ifdef GAMESERVER
	if (pEnt && pItem && pEnt->pCritter && g_RewardConfig.TypeCostumeMappings.stTypeToCostume)
	{
		ItemDef* pDef = GET_REF(pItem->hItem);
		TypeToCostumeStruct* pMapping = NULL;
		//0 can't be used as a key so we need to add 1 to the type.
		stashIntFindPointer(g_RewardConfig.TypeCostumeMappings.stTypeToCostume, pDef->eType+1, &pMapping);

		COPY_HANDLE(pEnt->pCritter->hNotYoursCostumeRef, g_RewardConfig.TypeCostumeMappings.hNotYoursDefault);
		if (pMapping)
		{
			int i;
			for (i = 0; i < eaSize(&pMapping->eaCats); i++)
			{
				if (eaiFind(&pDef->peCategories, pMapping->eaCats[i]->eCat) >= 0)
				{
					COPY_HANDLE(pEnt->pCritter->hYoursCostumeRef, pMapping->eaCats[i]->hCostume);
					return true;
				}
			}
			COPY_HANDLE(pEnt->pCritter->hYoursCostumeRef, pMapping->hDefault);
			return true;
		}
		if (IS_HANDLE_ACTIVE(g_RewardConfig.TypeCostumeMappings.hYoursDefault))
		{
			PlayerCostume* pCostume = GET_REF(g_RewardConfig.TypeCostumeMappings.hYoursDefault);//why do I have to do this?
			SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pCostume->pcName,pEnt->pCritter->hYoursCostumeRef);
			return true;
		}
	}
#endif
	return false;
}

// get cooldown for gated type. Zero is no cooldown
U32 Reward_GetGatedCooldown(Entity *pEntPlayer, S32 gatedType)
{
	if(pEntPlayer && pEntPlayer->pPlayer)
	{
		RewardGatedInfo *pRewardGatedInfo = eaIndexedGetUsingInt(&g_RewardConfig.eRewardGateInfo, gatedType);
		RewardGatedTypeData *pPlayerGated = eaIndexedGetUsingInt(&pEntPlayer->pPlayer->eaRewardGatedData, gatedType);
		static bool bSentError = false;

		if(pRewardGatedInfo)
		{
			// fall through and return zero if there isn't data or we arn't in the same block
			if(pPlayerGated && pPlayerGated->uNumTimes > 0)
			{
				U32 tm = timeServerSecondsSince2000();
				U32 uBlock = 0;
				U32 uBlockChar = 0;

				if(pRewardGatedInfo->uHoursPerBlock)
				{
					if (pRewardGatedInfo->bGateFromPlayerGrantTime)
					{
						uBlockChar = 0; // assume that whenever the character started was 0, since we don't really care
						uBlock = (tm-pPlayerGated->uTimeSet) / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
					}
					else
					{
						uBlock = tm / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
						uBlockChar = pPlayerGated->uTimeSet / (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
					}
				}

				if(uBlockChar == uBlock)
				{
					// in block
					// return seconds until next block
					if (pRewardGatedInfo->bGateFromPlayerGrantTime)
					{
						devassert(pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR > tm-pPlayerGated->uTimeSet);
						return (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR)-(tm-pPlayerGated->uTimeSet);
					}
					else
					{
						U32 uNextBlockTm = (uBlock + 1) * (pRewardGatedInfo->uHoursPerBlock * SECONDS_PER_HOUR);
						U32 uTimeNext = uNextBlockTm - tm;

						return uTimeNext;
					}
				}
			}
		}
		else if(!bSentError)
		{
			bSentError = true;
			ErrorDetailsf("No info for reward gated type id %d", gatedType);
			Errorf("EntGetRewardGatedCooldownSeconds No reward config info.");
		}
	}

	return 0;
}


#include "AutoGen/rewardCommon_h_ast.c"


