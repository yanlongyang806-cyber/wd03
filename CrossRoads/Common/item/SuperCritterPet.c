
#include "referencesystem.h"
#include "itemCommon.h"
#include "supercritterpet.h"
#include "entity.h"
#include "EntitySavedData.h"
#include "PowerModes.h"
#include "NotifyCommon.h"
#include "GameAccountDataCommon.h"
#include "resourceinfo.h"
#include "ResourceManager.h"
#include "file.h"
#include "CharacterClass.h"
#include "entCritter.h"
#include "Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "PowerVars.h"
#include "Powers.h"
#include "CharacterAttribs.h"
#include "CombatEval.h"

#if defined(GAMESERVER) || defined(GAMECLIENT)
#include "PowersMovement.h"
#endif

#if defined(GAMESERVER) || defined(APPSERVER)
#include "inventoryTransactions.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#endif

#ifdef GAMESERVER
#include "gslSuperCritterPet.h"
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern DictionaryHandle g_hRewardValTableDict;
DictionaryHandle g_hSuperCritterPetDict;
ExprContext* g_pSCPContext = NULL;

SuperCritterPetConfig g_SCPConfig;

static const char *s_pcVarLevelDelta;
static int s_hVarLevelDelta = 0;
static const char *s_pcVarPetLevel;
static int s_hVarPetLevel = 0;
static const char *s_pcVarPetQuality;
static int s_hVarPetQuality = 0;
static const char *s_pcVarPetGemLevel;
static int s_hVarPetGemLevel = 0;


//get a superCritterPet from an item that is one.
SuperCritterPet* scp_GetPetFromItem(Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
		return pItem->pSpecialProps->pSuperCritterPet;
	return NULL;
}

//is an item a SCP?
bool scp_itemIsSCP(Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet){
		return true;
	}
	return false;
}

AUTO_RUN;
int SuperCritterPetRegisterDict(void)
{
	ExprFuncTable* stFuncs;
	// Set up reference dictionary
	g_hSuperCritterPetDict = RefSystem_RegisterSelfDefiningDictionary("SuperCritterPetDef", false, parse_SuperCritterPetDef, true, true, NULL);

	resDictSetDisplayName(g_hSuperCritterPetDict, "SuperCritterPet File", "SuperCritterPet Files", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hSuperCritterPetDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hSuperCritterPetDict, NULL, NULL, NULL, NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hSuperCritterPetDict, 16, false, resClientRequestSendReferentCommand);
	}

	//Also set up the expression context while we're here.
	g_pSCPContext = exprContextCreate();
	exprContextSetAllowRuntimePartition(g_pSCPContext);

	s_pcVarLevelDelta = allocAddStaticString("NumLevelsTrained");
	s_pcVarPetLevel = allocAddStaticString("PetLevel");
	s_pcVarPetQuality = allocAddStaticString("PetQuality");
	s_pcVarPetGemLevel = allocAddStaticString("PetGemLevel");

	stFuncs = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stFuncs, "CEFuncsGeneric");
	exprContextAddFuncsToTableByTag(stFuncs, "gameutil");
	exprContextAddFuncsToTableByTag(stFuncs, "util");
	exprContextSetFuncTable(g_pSCPContext, stFuncs);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarLevelDelta, 0, &s_hVarLevelDelta);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, 0, &s_hVarPetLevel);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, 0, &s_hVarPetQuality);
	exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetGemLevel, 0, &s_hVarPetGemLevel);

	return 1;
}

static void scp_ValidateRefs(SuperCritterPetDef* pPetDef)
{
	int i;
	if (IsServer())
	{
		if (REF_IS_SET_BUT_ABSENT(pPetDef->hCritterDef))
		{
			ErrorFilenamef(pPetDef->pchFileName, "SuperCritterPet %s specifies non-existent CritterDef \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->hCritterDef));
		}
		for(i = 0; i < eaSize(&pPetDef->ppAltCostumes); i++)
		{
			if (pPetDef->ppAltCostumes[i])
			{
				if (REF_IS_SET_BUT_ABSENT(pPetDef->ppAltCostumes[i]->hCostume))
				{
					ErrorFilenamef(pPetDef->pchFileName, "A costume on SuperCritterPet %s specifies non-existent Costume \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->ppAltCostumes[i]->hCostume));
				}
				if (REF_IS_SET_BUT_ABSENT(pPetDef->ppAltCostumes[i]->displayMsg.hMessage))
				{
					ErrorFilenamef(pPetDef->pchFileName, "A costume on SuperCritterPet %s specifies non-existent Display Message \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->ppAltCostumes[i]->displayMsg.hMessage));
				}
			}
		}
	}
	else if (IsClient())
	{
		if (REF_IS_SET_BUT_ABSENT(pPetDef->hContinuingPlayerFX))
		{
			ErrorFilenamef(pPetDef->pchFileName, "SuperCritterPet %s specifies non-existent Continuing Player FX \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->hContinuingPlayerFX));
		}
		for(i = 0; i < eaSize(&pPetDef->ppAltCostumes); i++)
		{
			if (REF_IS_SET_BUT_ABSENT(pPetDef->ppAltCostumes[i]->hContinuingPlayerFX))
			{
				ErrorFilenamef(pPetDef->pchFileName, "A costume on SuperCritterPet %s specifies non-existent Continuing Player FX \"%s\".", pPetDef->pchName, REF_STRING_FROM_HANDLE(pPetDef->ppAltCostumes[i]->hContinuingPlayerFX));
			}
		}
	}
}

static int SCPResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, SuperCritterPetDef *pPetDef, U32 userID)
{
	switch (eType)
	{	
		xcase RESVALIDATE_CHECK_REFERENCES: // Called when all data has been loaded
		{
			scp_ValidateRefs(pPetDef);
			return VALIDATE_HANDLED;
		}break;
		xcase RESVALIDATE_POST_BINNING: // Called when all data has been loaded
		{
			if (IsGameServerSpecificallly_NotRelatedTypes())
			{
				//Cache the actual class so the client will get it too, for tooltips.
				CritterDef* pCritterDef = GET_REF(pPetDef->hCritterDef);
				CharacterClass* pClass = pCritterDef ? characterclass_GetAdjustedClass(pCritterDef->pchClass, 1, pCritterDef->pcSubRank, pCritterDef) : NULL;
				if (pClass)
					SET_HANDLE_FROM_STRING(g_hCharacterClassDict, pClass->pchName, pPetDef->hCachedClassDef);
				else
					Errorf("Class %s not found during SCP initialization.", pCritterDef->pchClass);
			}
			return VALIDATE_HANDLED;
		}break;
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_STARTUP(SuperCritterPet) ASTRT_DEPS(InventoryBagIDs, PowerVars, ItemTags, Critters, CharacterClassInfos);
void SuperCritterPetLoad(void)
{
	if(IsGameServerSpecificallly_NotRelatedTypes() || IsGatewayServer())
	{
		resDictManageValidation(g_hSuperCritterPetDict, SCPResValidateCB);
		resLoadResourcesFromDisk(g_hSuperCritterPetDict,"defs/pets",".scp", "supercritterpet.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	}

	if(IsGameServerSpecificallly_NotRelatedTypes() || IsClient() || IsGatewayServer())
	{
		ParserLoadFiles(NULL, "defs/config/SuperCritterPetConfig.def", "SuperCritterPetConfig.bin", PARSER_OPTIONALFLAG, parse_SuperCritterPetConfig, &g_SCPConfig);
	
		exprGenerate(g_SCPConfig.pExprTrainingDuration, g_pSCPContext);
		exprGenerate(g_SCPConfig.pExprUnbindCost, g_pSCPContext);
		exprGenerate(g_SCPConfig.pExprGemUnslotCost, g_pSCPContext);
	}
}

AUTO_TRANS_HELPER;
enumTransactionOutcome scp_trh_SetNumActiveSlots(ATR_ARGS, ATH_ARG NOCONST(EntitySavedSCPData)* pData, int iNum)
{
	if (ISNULL(pData))
		return TRANSACTION_OUTCOME_FAILURE;

	eaSetSizeStructNoConst(&pData->ppSuperCritterPets, parse_ActiveSuperCritterPet, iNum);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
NOCONST(EntitySavedSCPData)* scp_trh_GetOrCreateEntSCPDataStruct(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bAllowModification)
{
#ifdef GAMECLIENT
	if (bAllowModification)
		assertmsg(0, "Attempting to run scp_trh_GetOrCreateEntSCPDataStruct() on the gameclient with bAllowModification = true! This is a big no-no!");
#endif

	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved))
	{
		NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, NULL);
		int iNum = invbag_trh_maxslots(pEnt, pBag);
		int iPet;
		
		if (bAllowModification)
		{
			if (ISNULL(pEnt->pSaved->pSCPData))
			{
				pEnt->pSaved->pSCPData = StructCreateNoConst(parse_EntitySavedSCPData);
			}

			scp_trh_SetNumActiveSlots(ATR_PASS_ARGS, pEnt->pSaved->pSCPData, iNum);

			for(iPet = 0; iPet < iNum; iPet++)
			{
				NOCONST(ActiveSuperCritterPet)* pActivePet = pEnt->pSaved->pSCPData->ppSuperCritterPets[iPet];
				NOCONST(Item)* pPetItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pBag, iPet);
				ItemDef* pItemDef = SAFE_GET_REF(pPetItem, hItem);
				SuperCritterPetDef* pPetDef = SAFE_GET_REF(pItemDef, hSCPdef);
				if (!pActivePet->pEquipment)
				{
					int iNumEquipSlots = pPetDef ? eaSize(&pPetDef->ppEquipSlots) : 0;
					pActivePet->pEquipment = StructCreateNoConst(parse_InventoryBag);
					pActivePet->pEquipment->n_additional_slots = iNumEquipSlots;
				}
			}
		}
		return pEnt->pSaved->pSCPData;
	}
	return NULL;
}

//Less locking than scp_trh_GetOrCreateEntSCPDataStruct()
AUTO_TRANS_HELPER;
int scp_trh_GetSummonedPetIdx(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pSCPData))
	{
		return pEnt->pSaved->pSCPData->iSummonedSCP;
	}
	return -1;
}

//Less locking than scp_trh_GetOrCreateEntSCPDataStruct()
AUTO_TRANS_HELPER;
F32 scp_trh_GetSummonedPetBonusXPPct(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pSCPData))
	{
		return pEnt->pSaved->pSCPData->fCachedPetBonusXPPct;
	}
	return 1.0f;
}

//Less locking than scp_trh_GetOrCreateEntSCPDataStruct()
AUTO_TRANS_HELPER;
EntityRef scp_trh_GetSummonedPetERSCP(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pSCPData))
	{
		return pEnt->pSaved->pSCPData->erSCP;
	}
	return 0;
}

int scp_MaxLevel(Item* pPetItem)
{
	if(pPetItem)
	{
		ItemDef* pPetItemDef = GET_REF(pPetItem->hItem);
		if(!pPetItemDef)
			return 0;	//this can be called before the itemDef reference is loaded, e.g. inspecting someone else's pet.

		if (pPetItemDef && pPetItemDef->Quality >= 0 && pPetItemDef->Quality < eafSize(&g_SCPConfig.eafMaxLevelsPerQuality))
		{
			return g_SCPConfig.eafMaxLevelsPerQuality[pPetItemDef->Quality];
		}
		//this should never happen.
		devassertmsg(0, "Trying to find the max level of a Super Critter Pet from an item with an unexpected Quality.");
	}
	return 0;
}

bool scp_LevelIsValid(int iLevel, Item* pPetItem)
{
	RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
	return (iLevel > 0 && iLevel <= scp_MaxLevel(pPetItem) && iLevel <= eafSize(&pXPTable->Val));
}

F32 scp_GetTotalXPRequiredForLevel(int iLevel, Item* pPetItem)
{
	RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
	if (scp_LevelIsValid(iLevel, pPetItem))
	{
		if (pXPTable)
		{
			return pXPTable->Val[CLAMP_TAB_LEVEL(USER_TO_TAB_LEVEL(iLevel))];
		}
	}
	return 0;
}

NOCONST(SuperCritterPet)* scp_CreateFromDef(SuperCritterPetDef* pDef, Item* pPetItem, int iLevel)
{
	NOCONST(SuperCritterPet)* pPet = StructCreateNoConst(parse_SuperCritterPet);
	SuperCritterPetDef* pPetDef = RefSystem_ReferentFromString(g_hSuperCritterPetDict, pDef->pchName);
	CritterDef* pCritterDef = GET_REF(pPetDef->hCritterDef);
	CharacterClass* pClass = pCritterDef ? characterclass_GetAdjustedClass(pCritterDef->pchClass, 1, pCritterDef->pcSubRank, pCritterDef) : NULL;
	SET_HANDLE_FROM_STRING(g_hSuperCritterPetDict, pDef->pchName, pPet->hPetDef);
	if (pClass)
		SET_HANDLE_FROM_STRING(g_hCharacterClassDict, pClass->pchName, pPet->hClassDef);
	else
		SET_HANDLE_FROM_STRING(g_hCharacterClassDict, "Default", pPet->hClassDef);

	pPet->uLevel = scp_GetPetStartLevelForPlayerLevel(iLevel, pPetItem);
	pPet->uXP = scp_GetTotalXPRequiredForLevel(pPet->uLevel, pPetItem);
	return pPet;
}

AUTO_TRANS_HELPER;
bool scp_trh_IsAltCostumeUnlocked(ATH_ARG NOCONST(Entity)* pEnt, int idx, int iCostume, GameAccountDataExtract* pExtract)
{
	NOCONST(Item)* pPetItem = inv_trh_GetItemFromBag(ATR_EMPTY_ARGS, pEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, idx, pExtract);
	if (NONNULL(pPetItem) && NONNULL(pPetItem->pSpecialProps) && NONNULL(pPetItem->pSpecialProps->pSuperCritterPet))
	{
		NOCONST(SuperCritterPet)* pPet = pPetItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef *pPetDef = GET_REF(pPet->hPetDef);
		if (iCostume < eaSize(&pPetDef->ppAltCostumes)){
			return pPetDef->ppAltCostumes[iCostume]->iLevel <= pPet->uLevel;
		}
	}
	return false;
	
}

Item* scp_GetActivePetItem(Entity* pPlayer, int idx)
{
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayer);
	InventoryBag* pPetBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pPlayer), InvBagIDs_SuperCritterPets, pExtract);
	return pPetBag ? inv_bag_GetItem(pPetBag, idx) : NULL;
}

U32 scp_PetXPToLevelLookup(U32 uiCurXP, Item* pPetItem)
{
	RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
	int i;
	if (pXPTable)
	{
		for (i = 0; i < scp_MaxLevel(pPetItem) && pXPTable->Val[i] <= uiCurXP; i++)
		{
			//count up to next level that we qualify for
		}
		return MIN(i, scp_MaxLevel(pPetItem));	//reward table goes past max level.
	}
	return 1;
}

//sometimes we want pets to scale to a player's level based on this formula.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetStartLevelForPlayerLevel);
U32 scp_GetPetStartLevelForPlayerLevel(int playerLevel, SA_PARAM_OP_VALID Item* pPetItem){
	return CLAMP((playerLevel - g_SCPConfig.fLevelScalingStartsAtPlayerLevel) * g_SCPConfig.fLevelsPerPlayerLevel, 1, scp_MaxLevel(pPetItem));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(scp_EntIsSuperCritterPet);
bool scp_exprEntIsSuperCritterPet(SA_PARAM_OP_VALID Entity* pEnt){
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYCRITTER && pEnt->erOwner)
	{
		Entity* pOwner = entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->erOwner);
		return (pOwner && scp_GetSummonedPetEntRef(pOwner) == pEnt->myRef);
	}
	return false;
}

U32 scp_GetPetLevelAfterTraining(Item* pPetItem)
{
	if (scp_itemIsSCP(pPetItem))
	{
		return scp_PetXPToLevelLookup(scp_GetPetFromItem(pPetItem)->uXP, pPetItem);
	}
	return 1;
}

F32 scp_GetPetPercentToNextLevel(Item* pPetItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
	if( pPet )
	{
		RewardValTable* pXPTable = GET_REF(g_SCPConfig.hRequiredXPTable);
		int iLevel = scp_PetXPToLevelLookup(pPet->uXP, pPetItem);
		//the xp table for this is set up so that the xp required for level 2 is on the level 1 row.
		if (scp_LevelIsValid(iLevel+1, pPetItem) && (pXPTable->Val[iLevel]-pXPTable->Val[iLevel-1] > 0))
			return (pPet->uXP-pXPTable->Val[iLevel-1])/(pXPTable->Val[iLevel]-pXPTable->Val[iLevel-1]);
	}
	return 0.0;
}

int scp_GetPetCombatLevel(Item* pItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pItem);
	if (pPet)
	{
		return pPet->uLevel;
	}
	return -1;
}

U32 scp_GetActivePetTrainingTimeRemaining(Entity* pPlayer, int idx)
{
#if GAMECLIENT || GAMESERVER
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayer);
	if (pData && idx >= 0 && idx < eaSize(&pData->ppSuperCritterPets))
	{
		return pData->ppSuperCritterPets[idx]->uiTimeFinishTraining > 0 ? pData->ppSuperCritterPets[idx]->uiTimeFinishTraining - timeSecondsSince2000() : 0;
	}
#endif
	return 0;
}

const char* scp_GetPetItemName(Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pItem->pSpecialProps->pSuperCritterPet->hPetDef);
		CritterDef* pCritterDef = GET_REF(pDef->hCritterDef);
		if (pItem->pSpecialProps->pSuperCritterPet->pchName)
			return pItem->pSpecialProps->pSuperCritterPet->pchName;
		else if (pDef)
			return TranslateDisplayMessage(pCritterDef->displayNameMsg);
	}
	return NULL;
}

int scp_GetPetNumUnlockedSkins(Item* pItem)
{
	int count = 0;
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
	{
		SuperCritterPet* pPet = pItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef* pPetDef = GET_REF(pPet->hPetDef);
		int i;
		for(i = 0; i < eaSize(&pPetDef->ppAltCostumes); i++)
		{
			if (pPetDef->ppAltCostumes[i]->iLevel <= pPet->uLevel)
				count++;
		}
	}
	return count;
}

const char* scp_GetActivePetName(Entity* pPlayer, int idx)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pPlayer, idx));
	if (pPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		CritterDef* pCritterDef = NULL;
		if (pDef)
		{
			pCritterDef = GET_REF(pDef->hCritterDef);
		}
		if (pPet->pchName)
		{
			return pPet->pchName;
		}
		else if (pCritterDef)
		{
			return TranslateDisplayMessage(pCritterDef->displayNameMsg);
		}
	}
	return NULL;
}

SuperCritterPetDef* scp_GetPetDefFromItem(Item* pPetItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
	if (pPet)
	{
		return GET_REF(pPet->hPetDef);
	}
	return NULL;
}

const char* scp_GetActivePetDefName(Entity* pPlayer, int idx)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pPlayer, idx));
	if (pPet)
	{
		return REF_STRING_FROM_HANDLE(pPet->hPetDef);
	}
	return NULL;
}

U32 scp_EvalTrainingTime(Entity* pPlayerEnt, Item* pPetItem)
{
	MultiVal val = {0};
	ItemDef* pPetItemDef = GET_REF(pPetItem->hItem);
	SuperCritterPet* pPet = SAFE_MEMBER2(pPetItem,pSpecialProps,pSuperCritterPet);
	int uiLevelDelta = scp_GetPetLevelAfterTraining(pPetItem) - pPet->uLevel;

	if (pPetItemDef && pPet){

		exprContextSetPartition(g_pSCPContext, entGetPartitionIdx(pPlayerEnt));
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, pPet->uLevel, &s_hVarPetLevel);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, pPetItemDef->Quality, &s_hVarPetQuality);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarLevelDelta, uiLevelDelta, &s_hVarLevelDelta);

		exprEvaluate(g_SCPConfig.pExprTrainingDuration, g_pSCPContext, &val);

		return MultiValGetInt(&val, NULL);
	}
	return 0;
}

U32 scp_EvalGemRemoveCost(Entity* pPlayerEnt, Item* pPetItem, ItemDef* pGemItemDef, const char* pchCurrency)
{
	MultiVal val = {0};
	ItemDef* pPetItemDef = GET_REF(pPetItem->hItem);
	SuperCritterPet* pPet = SAFE_MEMBER2(pPetItem,pSpecialProps,pSuperCritterPet);
	int uiLevelDelta = scp_GetPetLevelAfterTraining(pPetItem) - pPet->uLevel;
	ItemDef* pCurrencyDef = GET_REF(g_SCPConfig.hGemUnslottingCurrency);

	if(!pCurrencyDef || stricmp(pchCurrency, pCurrencyDef->pchName))
	{
		//wrong currency.
		return 0;
	}

	if (pPetItemDef && pPet){

		exprContextSetPartition(g_pSCPContext, entGetPartitionIdx(pPlayerEnt));
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, pPet->uLevel, &s_hVarPetLevel);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, pPetItemDef->Quality, &s_hVarPetQuality);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetGemLevel, pGemItemDef->iLevel, &s_hVarPetGemLevel);

		exprEvaluate(g_SCPConfig.pExprGemUnslotCost, g_pSCPContext, &val);

		return MultiValGetInt(&val, NULL);
	}
	return 0;
}

U32 scp_EvalUnbindCost(Entity* pPlayerEnt, Item* pPetItem)
{
	MultiVal val = {0};
	ItemDef* pPetItemDef = GET_REF(pPetItem->hItem);
	SuperCritterPet* pPet = SAFE_MEMBER2(pPetItem,pSpecialProps,pSuperCritterPet);
	if (pPetItemDef && pPet){

		exprContextSetPartition(g_pSCPContext, entGetPartitionIdx(pPlayerEnt));
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetLevel, pPet->uLevel, &s_hVarPetLevel);
		exprContextSetIntVarPooledCached(g_pSCPContext, s_pcVarPetQuality, pPetItemDef->Quality, &s_hVarPetQuality);

		exprEvaluate(g_SCPConfig.pExprUnbindCost, g_pSCPContext, &val);

		return MultiValGetInt(&val, NULL);
	}
	return 0;
}

EntityRef scp_GetSummonedPetEntRef(Entity* pEnt)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData && eaSize(&pData->ppSuperCritterPets) > 0)
	{
		if (pData->iSummonedSCP > -1 && pData->iSummonedSCP < eaSize(&pData->ppSuperCritterPets))
		{
			return pData->erSCP;
		}
	}
	return 0;
}

Item* scp_GetSummonedPetItem(Entity* pEnt)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData && eaSize(&pData->ppSuperCritterPets) > 0)
	{
		if (pData->iSummonedSCP >= 0 && pData->iSummonedSCP < eaSize(&pData->ppSuperCritterPets))
		{
			return scp_GetActivePetItem(pEnt, pData->iSummonedSCP);
		}
	}
	return 0;
}

//IGNORES BAG PERMISSIONS ON GAMEACCOUNTDATAEXTRACT.
// The SuperCritterPets bag is available to all players.
// Ignoring pExtract allows us to get this bag directly from inside inv_lite_trh_SetNumericInternal()
// for the purpose of awarding pet XP.
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[]");
NOCONST(InventoryBag)* scp_trh_GetActivePetBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return NULL;

	return eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, 35 /* Literal InvBagIDs_SuperCritterPets */);
}

//if this is already bonus xp, use bonus=true so the bonus doesn't get more bonuses (want to add not multiply).
AUTO_TRANS_HELPER;
enumTransactionOutcome scp_trh_AwardActivePetXP(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, float delta, bool bonus)
{
	int iSummonedPetIdx = scp_trh_GetSummonedPetIdx(ATR_PASS_ARGS, pEnt);
	F32 fBonusXPPct = scp_trh_GetSummonedPetBonusXPPct(ATR_PASS_ARGS, pEnt);
#ifdef GAMECLIENT
	assertmsg(0, "scp_trh_AwardActivePetXP() cannot be run on the client!");
#endif
	if (iSummonedPetIdx > -1)
	{
		NOCONST(InventoryBag)* pBag = scp_trh_GetActivePetBag(ATR_PASS_ARGS, pEnt);
		NOCONST(Item)* pPetItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pBag, iSummonedPetIdx);
		
		//no XP for dead pets or pets blocked by region rules
		if (NONNULL(pPetItem) && NONNULL(pPetItem->pSpecialProps) && NONNULL(pPetItem->pSpecialProps->pSuperCritterPet) && !scp_trh_CheckFlag(ATR_PASS_ARGS, pPetItem, kSuperCritterPetFlag_Dead) && scp_trh_GetSummonedPetERSCP(ATR_PASS_ARGS, pEnt))
		{
			F32 fXPDelta = delta * g_SCPConfig.fPetXPMultiplier;
			if (!bonus)
				fXPDelta *= fBonusXPPct;
			pPetItem->pSpecialProps->pSuperCritterPet->uXP += fXPDelta;

#if defined(GAMESERVER) || defined(APPSERVER)
			QueueRemoteCommand_scp_PetGainedXP_CB(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, iSummonedPetIdx, fXPDelta);
#endif
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
bool scp_trh_IsEquipSlotLocked(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iPet, int iEquipSlot)
{
	if (ea32Size(&g_SCPConfig.eaEquipSlotUnlockLevels) > 0)
	{
		NOCONST(InventoryBag)* pSCPBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, NULL);
		NOCONST(Item)* pPetItem =  pSCPBag ? inv_bag_trh_GetItem(ATR_PASS_ARGS, pSCPBag, iPet) : NULL;
		int iPetLevel = pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet ? pPetItem->pSpecialProps->pSuperCritterPet->uLevel : 0;
		if (iPetLevel < g_SCPConfig.eaEquipSlotUnlockLevels[iEquipSlot])
			return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
bool scp_trh_IsGemSlotLocked(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iPet, int iGemSlot)
{
	if (ea32Size(&g_SCPConfig.eaGemSlotUnlockLevels) > 0)
	{
		NOCONST(InventoryBag)* pSCPBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, InvBagIDs_SuperCritterPets, NULL);
		NOCONST(Item)* pPetItem =  pSCPBag ? inv_bag_trh_GetItem(ATR_PASS_ARGS, pSCPBag, iPet) : NULL;
		int iPetLevel = pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet ? pPetItem->pSpecialProps->pSuperCritterPet->uLevel : 0;
		if (iPetLevel < g_SCPConfig.eaGemSlotUnlockLevels[iGemSlot])
			return true;
	}
	return false;
}

bool scp_IsGemSlotLockedOnPet(SuperCritterPet* pPet, int iGemSlot)
{
	if (pPet && ea32Size(&g_SCPConfig.eaGemSlotUnlockLevels) > 0)
	{
		int iPetLevel = pPet->uLevel;
		if (iPetLevel < g_SCPConfig.eaGemSlotUnlockLevels[iGemSlot])
			return true;
	}
	return false;

}


int scp_GetRushTrainingCost(Entity* pPlayerEnt, int iSlot)
{
	Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, iSlot);
	//just scale from the training time lookup.
	int ret = g_SCPConfig.fRushCostPerTrainingSecond * scp_EvalTrainingTime(pPlayerEnt, pPetItem);
	return max(ret, 0);
}

AUTO_TRANS_HELPER;
bool scp_trh_CheckFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag)
{
	return (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet && (pItem->pSpecialProps->pSuperCritterPet->bfFlags & eFlag));
}

AUTO_TRANS_HELPER;
void scp_trh_SetFlag(ATR_ARGS, ATH_ARG NOCONST(Item)* pItem, S32 eFlag, bool bSet)
{
	if (NONNULL(pItem) && NONNULL(pItem->pSpecialProps) && NONNULL(pItem->pSpecialProps->pSuperCritterPet))
	{
		if (bSet)
			pItem->pSpecialProps->pSuperCritterPet->bfFlags |= eFlag;
		else
			pItem->pSpecialProps->pSuperCritterPet->bfFlags &= ~eFlag;
	}
}

//Unsummons the pet and empties its inventory into the player's.
AUTO_TRANS_HELPER;
enumTransactionOutcome scp_trh_ResetActivePet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, int iNumEquipSlots, int iSlot)
{
	if (NONNULL(pPlayerEnt) && NONNULL(pPlayerEnt->pSaved))
	{
		NOCONST(EntitySavedSCPData)* pData = scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pPlayerEnt, true);
		NOCONST(ActiveSuperCritterPet)* pPet = eaGet(&pData->ppSuperCritterPets, iSlot);
		NOCONST(InventoryBag)* pInvBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt,  2 /* Literal InvBagIDs_Inventory */, NULL);
		if (ISNULL(pPet))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		if (pData->iSummonedSCP == iSlot)
			pData->iSummonedSCP = -1;

		pPet->uiTimeFinishTraining = 0;

		if (!pPet->pEquipment)
			pPet->pEquipment = StructCreateNoConst(parse_InventoryBag);

		if (!inv_bag_trh_BagEmpty(ATR_PASS_ARGS, pPet->pEquipment))
		{
			if (!inv_ent_trh_MoveAllItemsFromBag(ATR_PASS_ARGS, pPlayerEnt, pPet->pEquipment, pInvBag, true, NULL, NULL))
				return TRANSACTION_OUTCOME_FAILURE;
		}
		pPet->pEquipment->n_additional_slots = iNumEquipSlots;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

F32 scp_GetBonusXPPercentFromGems(Entity* pEnt, Item* pPetItem)
{
	int iGem, iPow, iAttr;
	F32 retVal = 0.0f;
	
	if (!pPetItem || !pPetItem->pSpecialProps || !pPetItem->pSpecialProps->pSuperCritterPet)
		return retVal;

	for (iGem = 0; iGem < eaSize(&pPetItem->pSpecialProps->ppItemGemSlots); iGem++)
	{
		ItemGemSlot* pGem = pPetItem->pSpecialProps->ppItemGemSlots[iGem];

		if (!pGem)
			continue;

		for (iPow = 0; iPow < eaSize(&pGem->ppPowers); iPow++)
		{
			PowerDef* pPowDef = SAFE_GET_REF(pGem->ppPowers[iPow], hDef);

			if (!pPowDef || pPowDef->eType != kPowerType_Passive)
				continue;

			for (iAttr = 0; iAttr < eaSize(&pPowDef->ppOrderedMods); iAttr++)
			{
				AttribModDef* pAttrDef = pPowDef->ppOrderedMods[iAttr];

				if (!pAttrDef || pAttrDef->eTarget != kModTarget_Self)
					continue;

				if (pAttrDef->offAttrib == kAttribType_RewardModifier)
				{
					RewardModifierParams* pParams = (RewardModifierParams*)pAttrDef->pParams;
					if (REF_STRING_FROM_HANDLE(pParams->hNumeric) == allocAddString("XP"))
					{
						combateval_ContextSetupSimple(NULL, 1, NULL);
						retVal += combateval_EvalNew(entGetPartitionIdx(pEnt), pAttrDef->pExprMagnitude, kCombatEvalContext_Simple, NULL);
					}
				}
			}
		}
	}
	return retVal + 1.0;
}

//Check whether pPet is allowed to put pItem in it's iEquipSlot slot.
bool scp_CanEquip(SuperCritterPet *pPet, int iEquipSlot, Item* pItem)
{
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	SuperCritterPetDef* pPetDef;
	int i;

	if (   !pItemDef
		|| (pItemDef->eType != kItemType_Upgrade && pItemDef->eType != kItemType_Weapon) 
		|| item_IsUnidentified(pItem))
		return false;

	pPetDef = GET_REF(pPet->hPetDef);
	if(!pPetDef)
		return false;
	for (i = 0; i < eaiSize(&pPetDef->ppEquipSlots[iEquipSlot]->peCategories); i++)
	{
		if (!(eaiFind(&pItemDef->peCategories, pPetDef->ppEquipSlots[iEquipSlot]->peCategories[i]) >= 0))
			return false;
	}
	for (i = 0; i < eaiSize(&pItemDef->peRestrictBagIDs); i++)
	{
		if (pItemDef->peRestrictBagIDs[i] == pPetDef->ppEquipSlots[iEquipSlot]->eID)
		{
			return true;
		}
	}
	return false;
}

#include "AutoGen/supercritterpet_h_ast.c"
