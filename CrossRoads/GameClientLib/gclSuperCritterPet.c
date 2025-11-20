#include "SuperCritterPet.h"
#include "stdtypes.h"
#include "uigen.h"
#include "gclentity.h"
#include "gclSuperCritterPet.h"
#include "entCritter.h"
#include "FCInventoryUI.h"
#include "EntitySavedData.h"
#include "Character.h"
#include "GameAccountDataCommon.h"
#include "AutoGen/GameClientLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/powers_h_ast.h"
#include "CharacterClass.h"
#include "CharacterAttribs.h"
#include "character_mods.h"
#include "itemCommon.h"
#include "AttribsUI.h"
#include "AutoGen/gclsupercritterpet_c_ast.h"
#include "AutoGen/AttribsUI_h_ast.h"
#include "combateval.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "PowerApplication.h"
#include "PowerEnhancements.h"
#include "StringUtil.h"
#include "gclUIGenPaperdoll.h"
#include "PowersMovement.h"

static Entity** s_eaFakeSCPEntities = NULL;
static Entity* s_pFakeInspectSCPEntity = NULL;
static Item* s_pInspectSCPItem = NULL;
static U64* s_eaFakeEntSrcItemIDs = NULL;

Item* scp_GetInspectItem()
{
	return s_pInspectSCPItem;
}

extern REF_TO(RewardValTable) s_SCPXPTable;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


//gets the item from the active pet slot.  Used by the player_status UI to use many other functions.
//pet_inspect UI has a item reference instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetItem);
SA_RET_OP_VALID Item* scp_ExprGetPetItem(int idx)
{
	if (idx == -1)//return inspect item.
	{
		return s_pInspectSCPItem;
	}
	else
	{
		Entity* pEnt = entActivePlayerPtr();
		Item* pItem = scp_GetActivePetItem(pEnt, idx);
		return pItem;
	}

}

// gets a list of all the power enhancements that will apply to the power
static void scp_FakeEntProcessPassivesGetEnhancements(	Entity* pPetEnt, 
														Power *pPassivePower, 
														PowerDef *pPassivePowerDef, 
														Power ***peaPowEnhancements)
{
	FOR_EACH_IN_EARRAY(pPetEnt->pChar->ppPowers, Power, pPowEnhancement)
	{
		PowerDef *pEnhancementPowerDef = GET_REF(pPowEnhancement->hDef);
		if (pEnhancementPowerDef && pEnhancementPowerDef->eType == kPowerType_Enhancement && 
			power_EnhancementAttachIsAllowed(PARTITION_CLIENT, pPetEnt->pChar, pEnhancementPowerDef, pPassivePowerDef, false))
		{
			eaPush(peaPowEnhancements, pPowEnhancement);
		}
	}
	FOR_EACH_END
}

// returns true if the powerDef has all the categories on g_SCPConfig.piFakeEntStatsPassiveCategories
static bool scp_PassiveHasCorrectCategories(PowerDef *pPowerDef)
{
	FOR_EACH_IN_EARRAY_INT(g_SCPConfig.piFakeEntStatsPassiveCategories, S32, powerCat)
	{
		if (eaiFind(&pPowerDef->piCategories, powerCat) < 0)
		{
			return false;
		}
	}
	FOR_EACH_END

	return true;
}

// a very contrived application of all the passive powers on the fake entity to generate mods we want to see on stats
// will only apply passives that are character targeted, and the power must have categories that are blessed by the g_SCPConfig
// this feels somewhat wrong being in this file, but it's really only something we need for pet stat showing
static void scp_FakeEntProcessPassives(Entity* pPetEnt)
{
	if (!pPetEnt->pChar)
		return;

	PERFINFO_AUTO_START_FUNC();

	FOR_EACH_IN_EARRAY(pPetEnt->pChar->ppPowers, Power, pPower)
	{
		PowerDef *pPowerDef = GET_REF(pPower->hDef);
		// todo: check the restricted power categories
		if (pPowerDef && pPowerDef->eType == kPowerType_Passive && 
			pPowerDef->eEffectArea == kEffectArea_Character && 
			scp_PassiveHasCorrectCategories(pPowerDef))
		{
			PowerApplication app = {0};

			app.pcharSource = pPetEnt->pChar;
			app.pclass = character_GetClassCurrent(pPetEnt->pChar);

			app.ppow = pPower;
			app.iIdxMulti = pPower->iIdxMultiTable;
			app.fTableScale = pPower->fTableScale;

			app.pdef = pPowerDef;
			
			app.iLevel = entity_GetCombatLevel(pPetEnt);
			app.bLevelAdjusting = pPetEnt->pChar->bLevelAdjusting;
			app.erModOwner = entGetRef(pPetEnt);
			app.erModSource = entGetRef(pPetEnt);

			combateval_ContextSetupApply(pPetEnt->pChar, pPetEnt->pChar, NULL, &app);
						
			scp_FakeEntProcessPassivesGetEnhancements(pPetEnt, pPower, pPowerDef, &app.pppowEnhancements);
						
			character_ApplyModsFromPowerDef(PARTITION_CLIENT, pPetEnt->pChar, &app, 
											0.f, 0.f, kModTarget_Self, false, NULL, NULL, NULL);
		}

	}
	FOR_EACH_END
	PERFINFO_AUTO_STOP();
}


// takes all the strength aspect mods that are on the character and compiles them into the basic attrib struct
// this is totally not correct for powers system mechanics, but we're just using this to display stats
// that way when we call EntGetAttrib() to get the magnitude of an aspect we don't have to recalculate and apply the strength
static void scp_FakeEntCompileStrMods(Entity* pPetEnt)
{
	S32 *piAttribsToCompile = NULL;

	FOR_EACH_IN_EARRAY(pPetEnt->pChar->modArray.ppMods, AttribMod, pAttribMod)
	{
		if (pAttribMod->pDef && IS_STRENGTH_ASPECT(pAttribMod->pDef->offAspect) &&
			IS_NORMAL_ATTRIB(pAttribMod->pDef->offAttrib))
		{
			eaiPushUnique(&piAttribsToCompile, pAttribMod->pDef->offAttrib);
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY_INT(piAttribsToCompile, S32, iAttrib)
	{
		F32 fStrAdd = 0.f;
		F32 fStr = character_GetStrengthGeneric(PARTITION_CLIENT, pPetEnt->pChar, iAttrib, &fStrAdd);
		F32 *pF = F32PTR_OF_ATTRIB(pPetEnt->pChar->pattrBasic, iAttrib);
		*pF = (*pF + fStrAdd) * fStr;
	}
	FOR_EACH_END

	eaiDestroy(&piAttribsToCompile);
}


Entity* scp_CreateFakeEntity(Item* pPetItem, ActiveSuperCritterPet* pActivePet)
{
	Entity* pPetEnt = StructCreateWithComment(parse_Entity, "Fake Entity created by the client to simulate a SuperCritterPet.");
	Entity* pPlayer = entActivePlayerPtr();
	NOCONST(Entity)* pNCPetEnt = CONTAINER_NOCONST(Entity, pPetEnt);
	SuperCritterPet* pPet = pPetItem && pPetItem->pSpecialProps ? pPetItem->pSpecialProps->pSuperCritterPet : NULL;
	SuperCritterPetDef* pPetDef = pPet ? GET_REF(pPet->hPetDef) : NULL;
	CritterDef* pDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
	CharacterClass* pPetClass = pPet ? GET_REF(pPet->hClassDef) : NULL;
	int i = 0;

	if (!pPet || !pPetDef || !pPetClass || !pDef)
		return NULL;
	pNCPetEnt->bFakeEntity = true; // Mark this as a fake entity
	pNCPetEnt->myEntityType = GLOBALTYPE_ENTITYCRITTER;
	pNCPetEnt->pCritter = StructCreateNoConst(parse_Critter);

	pPetEnt->erOwner = pPlayer->myRef;
	pPetEnt->erCreator = pPlayer->myRef;

	SET_HANDLE_FROM_STRING(g_hCritterDefDict, pDef->pchName, pNCPetEnt->pCritter->critterDef);


	pPetEnt->pCritter->pcRank = pDef->pcRank;
	pPetEnt->pCritter->pcSubRank = pDef->pcSubRank;

	if (!pPetClass)
	{
		StructDestroy(parse_Entity, pPetEnt);
		return NULL;
	}
	critter_AddCombat( pPetEnt, pDef, scp_GetPetCombatLevel(pPetItem), 1, pDef->pcSubRank, 0, true, true, pPetClass, false);


	//Add critter inventory
	if(pPetEnt->pChar && IS_HANDLE_ACTIVE(pPetEnt->pChar->hClass))
	{
		CharacterClass *pClass = GET_REF(pPetEnt->pChar->hClass);
		DefaultInventory *pInventory = pClass ? GET_REF(pClass->hInventorySet) : NULL;
		DefaultItemDef **ppitemList = NULL;
		InventoryBag* pPlayerBag = pActivePet ? pActivePet->pEquipment : NULL;
		NOCONST(InventoryBag)* pPetBag = NULL;

		if(pInventory)
			inv_ent_trh_InitAndFixupInventory(ATR_EMPTY_ARGS,pNCPetEnt,pInventory,true,true,NULL);
		else if(eaSize(&pDef->ppCritterItems) > 0)
			ErrorFilenamef(pDef->pchFileName,"%s Critter has items, but no inventory set! No items will be equipped",pDef->pchName);
		for ( i = 0; i < eaSize(&pPetEnt->pInventoryV2->ppInventoryBags); i++)
		{
			if (invbag_flags(pPetEnt->pInventoryV2->ppInventoryBags[i]) & InvBagFlag_EquipBag)
			{
				pPetBag = CONTAINER_NOCONST(InventoryBag, pPetEnt->pInventoryV2->ppInventoryBags[i]);
				break;
			}
		}

		if (pPetBag && pPlayerBag)
		{
			for (i = 0; i < eaSize(&pPlayerBag->ppIndexedInventorySlots); i++)
			{
				if (pPlayerBag->ppIndexedInventorySlots[i]->pItem)
					inv_bag_trh_AddItem(ATR_EMPTY_ARGS, pNCPetEnt, NULL, pPetBag, -1, StructCloneDeConst(parse_Item, pPlayerBag->ppIndexedInventorySlots[i]->pItem), 0, NULL, NULL, NULL);
			}
		}
		//also give it its own item so gem powers are applied
		inv_bag_trh_AddItem(ATR_EMPTY_ARGS, pNCPetEnt, NULL, pPetBag, -1, StructCloneDeConst(parse_Item, pPetItem), 0, NULL, NULL, NULL);
		character_DirtyInnateEquip(pPetEnt->pChar);
	}

	// Start up the Character's innate state immediately, without waiting for a combat tick
	if(pPetEnt->pChar)
	{
		Character *pchar = pPetEnt->pChar;

		character_ResetPowersArray(PARTITION_CLIENT, pchar, NULL);

		pchar->bSkipAccrueMods = false;

		character_DirtyInnatePowers(pchar);
		character_DirtyPowerStats(pchar);
		
		scp_FakeEntProcessPassives(pPetEnt);
		character_FakeEntInitAccrual(pchar);
		character_AccrueMods(PARTITION_CLIENT,pchar,1.0f,NULL);
		scp_FakeEntCompileStrMods(pPetEnt);
				
		pchar->pattrBasic->fHitPoints = pchar->pattrBasic->fHitPointsMax;
		
	}
	return pPetEnt;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_ResetFakeEntity);
void scp_ExprResetFakeEntity(int idx)
{
	
	if (s_eaFakeSCPEntities[idx])
		StructDestroy(parse_Entity, s_eaFakeSCPEntities[idx]);
	s_eaFakeSCPEntities[idx] = NULL;
	s_eaFakeEntSrcItemIDs[idx] = 0;
	//if it's the summoned pet, resummon it.
	{
		Entity *pEnt = entActivePlayerPtr();
		EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	}
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void scp_InvalidateFakeEntities()
{
	//invalidate fake entities
	int idx = 0;
	for (idx = 0; idx < eaSize(&s_eaFakeSCPEntities); idx++)
	{
		scp_ExprResetFakeEntity(idx);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_isItemSCP);
bool scp_ExprIsItemSCP(SA_PARAM_OP_VALID Item* pItem){
	if (scp_itemIsSCP(pItem)){
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetXPBonusPool);
int scp_ExprGetPetXPBonusPool()
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData)
	{
		//
		ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, gConf.pcLevelingNumericItem);
		S32 iNumBonus = eaSize(&pItemDef->eaBonusNumerics);
		S32 i;
		S32 iBonus = 0;

		for(i = 0; i < iNumBonus; ++i)
		{
			ItemDef *pBonusDef = GET_REF(pItemDef->eaBonusNumerics[i]->hItem);
			if(pBonusDef && pBonusDef->flags & kItemDefFlag_SCPBonusNumeric)
			{
				iBonus += inv_GetNumericItemValue(pEnt, pBonusDef->pchName);
			}
		}

		//add pet xp bonus from gems in pet items:
		return iBonus;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetXPBonusPercent);
F32 scp_ExprGetPetXPBonusPercent(SA_PARAM_OP_VALID Item* pPetItem)
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	F32 iBonus = 0.0F;
	if (pData)
	{
		//
		ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, gConf.pcLevelingNumericItem);
		S32 iNumBonus = eaSize(&pItemDef->eaBonusNumerics);
		S32 i;

		for(i = 0; i < iNumBonus; ++i)
		{
			ItemDef *pBonusDef = GET_REF(pItemDef->eaBonusNumerics[i]->hItem);
			if(pBonusDef && pBonusDef->flags & kItemDefFlag_SCPBonusNumeric && pBonusDef->uBonusPercent)
			{
				//return the first nonzero result; this is the percent that will be applied to the next xp point
				iBonus = pBonusDef->uBonusPercent;
				break;
			}
		}
	}
	//add bonus from this pet's gems
	iBonus += scp_GetBonusXPPercentFromGems(pEnt, pPetItem) * 100;
	return iBonus;
}

void scp_PostGemChange()
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayerEnt);
	//recalculate the pet xp bonus from gems on the client and server:
	pData->fCachedPetBonusXPPct = scp_GetBonusXPPercentFromGems(pPlayerEnt, scp_ExprGetPetItem(pData->iSummonedSCP));
	ServerCmd_scp_resetSummonedPetInventory();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetXPPct);
F32 scp_ExprGetPetXPPct(SA_PARAM_OP_VALID Item* pItem)
{
	return scp_GetPetPercentToNextLevel(pItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetNextLevelXP);
S32 scp_ExprGetPetNextLevelXP(SA_PARAM_OP_VALID Item* pItem)
{
	if (scp_itemIsSCP(pItem))
	{
		return scp_GetTotalXPRequiredForLevel(scp_GetPetFromItem(pItem)->uLevel + 1, pItem);
	}
	return 0.0f;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetXP);
S32 scp_ExprGetPetXP(SA_PARAM_OP_VALID Item* pItem)
{
	if (scp_itemIsSCP(pItem))
	{
		return pItem->pSpecialProps->pSuperCritterPet->uXP;
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetXPLevel);
int scp_ExprGetPetXPLevel(SA_PARAM_OP_VALID Item* pItem)
{
	return scp_GetPetCombatLevel(pItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetXPLevelAfterTraining);
int scp_ExprGetPetXPLevelAfterTraining(SA_PARAM_OP_VALID Item* pItem)
{
	return scp_GetPetLevelAfterTraining(pItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetMaxPetXPLevel);
int scp_ExprGetMaxPetXPLevel(SA_PARAM_OP_VALID Item* pPetItem)
{
	return scp_MaxLevel(pPetItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetActivePetList);
void scp_ExprGetActivePetList(SA_PARAM_NN_VALID UIGen *pGen, int iMinSlotsToShow)
{
	Entity *pEnt = entActivePlayerPtr();
	UIActiveSuperCritterPet ***peaPetList = ui_GenGetManagedListSafe(pGen, UIActiveSuperCritterPet);
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_SuperCritterPets, pExtract);
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pEnt && pBag)
	{
		int iCount = invbag_maxslots(pEnt, pBag);
		int i;
		if (!s_eaFakeSCPEntities)
			eaSetSize(&s_eaFakeSCPEntities, max(iMinSlotsToShow, iCount));
		if (!s_eaFakeEntSrcItemIDs)
			ea64SetSize(&s_eaFakeEntSrcItemIDs, max(iMinSlotsToShow, iCount));
		for (i = 0; i < max(iMinSlotsToShow, iCount); i++)
		{
			UIActiveSuperCritterPet *pUIPet = eaGetStruct(peaPetList, parse_UIActiveSuperCritterPet, i);
			ActiveSuperCritterPet* pPet = pData ? eaGet(&pData->ppSuperCritterPets, i) : NULL;
			Item* pItem = inv_bag_GetItem(pBag, i);
			const char* pchName = scp_GetActivePetName(pEnt, i);
			pUIPet->bSlotted = !!pItem;
			pUIPet->bLocked = (i >= iCount); 
			pUIPet->bSummoned = (pPet && pData) ? (i == pData->iSummonedSCP) : false;
			pUIPet->bDead = scp_CheckFlag(pItem, kSuperCritterPetFlag_Dead);
			pUIPet->bTraining = pPet ? (pPet->uiTimeFinishTraining > 0) : false;
			if (pUIPet->bTraining)
				pUIPet->uiTrainingTimeInSeconds = pPet->uiTimeFinishTraining >0 ? pPet->uiTimeFinishTraining - timeSecondsSince2000() : 0;
			pUIPet->entRef = (pData && i == pData->iSummonedSCP) ? pData->erSCP : 0;
			pUIPet->level = scp_GetPetCombatLevel(pItem);

			if (pUIPet->pchName)
				free(pUIPet->pchName);

			if (pchName)
				pUIPet->pchName = strdup(pchName);
			else
				pUIPet->pchName = NULL;

			//if we never generated a fake entity or if the source item's ID doesn't match
			if ((!pItem || pItem->id != s_eaFakeEntSrcItemIDs[i]) && s_eaFakeSCPEntities[i])
			{
				//if we're lacking a fake ent or it doesn't match
				StructDestroy(parse_Entity, s_eaFakeSCPEntities[i]);
				s_eaFakeSCPEntities[i] = NULL;
			}
			if (pItem && !s_eaFakeSCPEntities[i])
			{
				s_eaFakeSCPEntities[i] = scp_CreateFakeEntity(pItem, pPet);
				s_eaFakeEntSrcItemIDs[i] = pItem->id;
			}
			pUIPet->pFakeEnt = s_eaFakeSCPEntities[i];
		}
		eaSetSizeStruct(peaPetList, parse_UIActiveSuperCritterPet, max(iMinSlotsToShow, iCount));
	}

	ui_GenSetManagedListSafe(pGen, peaPetList, UIActiveSuperCritterPet, true);
}

//Creates a fake pet entity from an arbitrary pet item for the pet inspection window.
//Must only be called from a Gen's PointerUpdate block or know (!s_pFakeInspectSCPEntity).
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_CreateFakePetInspectEnt);
void scp_ExprCreateFakePetInspectEnt(SA_PARAM_OP_VALID Item* pItem)
{
	if(s_pFakeInspectSCPEntity)
	{
		StructDestroy(parse_Entity, s_pFakeInspectSCPEntity);
		s_pFakeInspectSCPEntity = NULL;
		StructDestroy(parse_Item, s_pInspectSCPItem);
		s_pInspectSCPItem = NULL;
	}
	s_pFakeInspectSCPEntity = scp_CreateFakeEntity(pItem, NULL);
	s_pInspectSCPItem = StructClone(parse_Item, pItem);
}

extern InventorySlot** g_eaEmptySlots;
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetEquipment);
void scp_ExprGetPetEquipment(SA_PARAM_NN_VALID UIGen *pGen, int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (!pData || !eaGet(&pData->ppSuperCritterPets, idx))
		return;
	else
	{
		InventorySlot ***peaSlotList = ui_GenGetManagedListSafe(pGen, InventorySlot);
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag* pBag = pData->ppSuperCritterPets[idx]->pEquipment;
		SuperCritterPetDef* pPetDef = scp_GetPetDefFromItem(scp_GetActivePetItem(pEnt, idx));
		int i;
		eaClearFast(peaSlotList);
		if (pBag && pPetDef)
		{
			int iCount = eaSize(&pPetDef->ppEquipSlots);
			while (iCount > eaSize(&g_eaEmptySlots))
				eaPush(&g_eaEmptySlots, (InventorySlot *)inv_InventorySlotCreate(eaSize(&g_eaEmptySlots)));
			for (i = 0; i < iCount; i++)
			{
				if (!inv_bag_GetItem(pBag, i))
					eaPush(peaSlotList, g_eaEmptySlots[i]);
				else
				{
					InventorySlot* pSlot = inv_GetSlotPtr(pBag, i);
					eaPush(peaSlotList, pSlot);
				}
			}
		}

		ui_GenSetManagedListSafe(pGen, peaSlotList, InventorySlot, false);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_PetTrainingTimeRemaining);
U32 scp_ExprTrainingTimeRemaining(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	return scp_GetActivePetTrainingTimeRemaining(pEnt, idx);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetTrainingTime);
U32 scp_ExprGetPetTrainingTime(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	S32 time = 0;
	if (pData && pData->ppSuperCritterPets)
	{
		if (pData->ppSuperCritterPets[idx]->uiTimeFinishTraining == 0)
		{
			//find the time that it would take if we trained.
			time = scp_EvalTrainingTime(pEnt, scp_GetActivePetItem(pEnt, idx));
		}
		else
		{
			time = pData->ppSuperCritterPets[idx]->uiTimeFinishTraining - timeSecondsSince2000();
		}
	}
	return time;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetRushTrainingCost);
int scp_ExprGetRushTrainingCost(int iSlot)
{
	Entity* pEnt = entActivePlayerPtr();
	return scp_GetRushTrainingCost(pEnt, iSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetRenameCost);
int scp_ExprGetRenameCost()
{
	return g_SCPConfig.iRenameCost;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_RushPetTraining);
void scp_ExprRushPetTraining(int idx)
{
	ServerCmd_scp_RushTraining(idx);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetCostume);
SA_RET_OP_VALID PlayerCostume* scp_ExprGetPetCostume(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pEnt, idx));
	if (pPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		if (pDef && pDef->ppAltCostumes && pPet->iCurrentSkin > 0)
			return GET_REF(pDef->ppAltCostumes[pPet->iCurrentSkin-1]->hCostume);
		else
		{
			CritterDef* pCritterDef = GET_REF(pDef->hCritterDef);
			return GET_REF(pCritterDef->ppCostume[0]->hCostumeRef);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetCostumeDisplayName);
SA_RET_NN_VALID const char* scp_ExprGetPetCostumeName(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pEnt, idx));
	if (pPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		if (pDef && pDef->ppAltCostumes && pPet->iCurrentSkin > 0)
			return TranslateDisplayMessage(pDef->ppAltCostumes[pPet->iCurrentSkin-1]->displayMsg);
	}
	return TranslateMessageKey("SuperCritterPet.DefaultCostumeString");
}


SA_RET_OP_VALID PaperdollHeadshotData* scp_PaperdollGetHeadshotFromSCP(ExprContext* pContext, SuperCritterPet* pPet)
{
	Entity *pEnt = entActivePlayerPtr();
	PaperdollHeadshotData* pData = NULL;
	if (pPet)
	{
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		PlayerCostume* pCostume = NULL;
		const char* pchFXName = NULL;
		const char** ppchAddedFx = NULL;
		if (pDef && pDef->ppAltCostumes && pPet->iCurrentSkin > 0)
		{
			pCostume = GET_REF(pDef->ppAltCostumes[pPet->iCurrentSkin-1]->hCostume);
			pchFXName = REF_STRING_FROM_HANDLE(pDef->ppAltCostumes[pPet->iCurrentSkin-1]->hContinuingPlayerFX);
		}
		else if (pDef)
		{
			CritterDef* pCritterDef = GET_REF(pDef->hCritterDef);
			int i;
			if (pCritterDef)
			{
				for (i = 0; i < eaSize(&pCritterDef->ppCostume); i++)
				{
					if ((pCritterDef->ppCostume[i]->iMinLevel < 0 || ((int)pPet->uLevel) >= pCritterDef->ppCostume[i]->iMinLevel) && 
						(pCritterDef->ppCostume[i]->iMaxLevel < 0 || ((int)pPet->uLevel) <= pCritterDef->ppCostume[i]->iMaxLevel))
					{
						pCostume = GET_REF(pCritterDef->ppCostume[i]->hCostumeRef);
						break;
					}
				}
			}
			pchFXName = REF_STRING_FROM_HANDLE(pDef->hContinuingPlayerFX);
		}
		if (pchFXName)
		{
			eaStackCreate(&ppchAddedFx, 1);
			eaPush(&ppchAddedFx,pchFXName);
		}

		if (ppchAddedFx)
		{
			pCostume = pEnt->costumeRef.pEffectiveCostume;
			if (!pCostume)
				pCostume = pEnt->costumeRef.pStoredCostume;
		}
		pData = gclPaperdoll_CreateHeadshotData(pContext, NULL, pCostume, NULL, NULL, pDef?pDef->pchStyleDef:NULL, &ppchAddedFx);
		eaDestroy(&ppchAddedFx);
	}
	return pData;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PaperdollGetHeadshotFromSCPActive);
SA_RET_OP_VALID PaperdollHeadshotData* scp_ExprPaperdollGetHeadshotFromSCPActive(ExprContext* pContext, int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pEnt, idx));
	if (pPet)
	{
		return scp_PaperdollGetHeadshotFromSCP(pContext, pPet);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PaperdollGetHeadshotFromSCPItem);
SA_RET_OP_VALID PaperdollHeadshotData* scp_PaperdollGetHeadshotFromSCPItem(ExprContext* pContext, SA_PARAM_OP_VALID Item* item)
{
	if(item && item->pSpecialProps && item->pSpecialProps->pSuperCritterPet)
	{
		return scp_PaperdollGetHeadshotFromSCP(pContext, item->pSpecialProps->pSuperCritterPet);
	}
	return NULL;
}

AUTO_STRUCT;
typedef struct SCPAltCostumeUI
{
	const char* pchName;	AST(NAME(DisplayName))
	bool bLocked;
} SCPAltCostumeUI;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetCostumeList);
void scp_ExprGetPetCostumeList(SA_PARAM_NN_VALID UIGen *pGen, int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (!pData || !eaGet(&pData->ppSuperCritterPets, idx))
		return;
	else
	{
		SCPAltCostumeUI ***peaCostumeList = ui_GenGetManagedListSafe(pGen, SCPAltCostumeUI);
		SuperCritterPetDef* pPetDef = scp_GetPetDefFromItem(scp_GetActivePetItem(pEnt, idx));
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int i;
		eaClearFast(peaCostumeList);
		if (pPetDef)
		{
			int iCount = eaSize(&pPetDef->ppAltCostumes) + 1;

			eaSetSizeStruct(peaCostumeList, parse_SCPAltCostumeUI, iCount);

			//the default costume comes first:
			(*peaCostumeList)[0]->pchName = TranslateMessageKey("SuperCritterPet.DefaultCostumeString");
			(*peaCostumeList)[0]->bLocked = false;
			//the other costumes:
			for (i = 0; i < iCount - 1; i++)
			{
				(*peaCostumeList)[i+1]->pchName =TranslateDisplayMessage(pPetDef->ppAltCostumes[i]->displayMsg);
				(*peaCostumeList)[i+1]->bLocked = !scp_IsAltCostumeUnlocked(pEnt, idx, i, pExtract);
			}
		}

		ui_GenSetManagedListSafe(pGen, peaCostumeList, SCPAltCostumeUI, false);
	}
}

//idx < 0 means use fake inspect ent.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetSummonBonuses);
void scp_ExprGetPetSummonBonuses(SA_PARAM_NN_VALID UIGen *pGen, int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	Entity* pFakeEnt;
	AttribBonusUIElement ***peaBonusAttribs;
	int iNumBonuses = 0;

	if (idx < 0)
	{
		pFakeEnt = s_pFakeInspectSCPEntity;
	}
	else if (!pData || !eaGet(&pData->ppSuperCritterPets, idx))
	{
		return;
	}
	else
	{
		peaBonusAttribs = ui_GenGetManagedListSafe(pGen, AttribBonusUIElement);
		pFakeEnt = s_eaFakeSCPEntities[idx];
	}
	if (!pData || !eaGet(&pData->ppSuperCritterPets, idx))	{
		return;
	}
	else
	{
		pFakeEnt = s_eaFakeSCPEntities[idx];
	}
	
	peaBonusAttribs = ui_GenGetManagedListSafe(pGen, AttribBonusUIElement);
	if (pFakeEnt)
	{
		int numPowers = eaSize(&pFakeEnt->pChar->ppPowersPersonal);
		int i, j;
		combateval_ContextSetupSimple(pFakeEnt->pChar, pFakeEnt->pChar->iLevelExp, NULL);
		for (i = 0; i < numPowers; i++)
		{
			Power* pPow = pFakeEnt->pChar->ppPowersPersonal[i];
			PowerDef* pDef = SAFE_GET_REF(pPow, hDef);
			if (pDef && REF_STRING_FROM_HANDLE(pDef->hTargetAffected) == allocFindString("Creator"))
			{
				for (j = 0; j < eaSize(&pDef->ppOrderedMods); j++)
				{
					AttribModDef* pModDef = pDef->ppOrderedMods[j];
					if (pModDef->eTarget == kModTarget_Target)
					{
						Message *pMessage = StaticDefineGetMessage(AttribTypeEnum, pModDef->offAttrib);
						const char *pchLogicalName = StaticDefineIntRevLookup(AttribTypeEnum, pModDef->offAttrib);
						AttribBonusUIElement* pUIElement = eaGetStruct(peaBonusAttribs, parse_AttribBonusUIElement, iNumBonuses++);
						pUIElement->eAttrib = pModDef->offAttrib;
						pUIElement->eAspect = pModDef->offAspect;
						pUIElement->fBonus = combateval_EvalNew(PARTITION_CLIENT, pModDef->pExprMagnitude, kCombatEvalContext_Simple, NULL);

						if (pUIElement->fBonus == 0)
						{
							iNumBonuses--;
							continue;
						}
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
				}
			}
		}
	}
	eaSetSizeStruct(peaBonusAttribs, parse_AttribBonusUIElement, iNumBonuses);

	ui_GenSetManagedListSafe(pGen, peaBonusAttribs, AttribBonusUIElement, true);
}
// idx < 0 means use fake inspect ent.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetEntity);
SA_RET_OP_VALID Entity* scp_ExprGetPetEntity(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	if (idx < 0)
		return s_pFakeInspectSCPEntity;
	if (s_eaFakeSCPEntities)
		return s_eaFakeSCPEntities[idx];
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetSummonedPetEntity);
SA_RET_OP_VALID Entity* scp_ExprGetSummonedPetEntity()
{
	Entity *pEnt = entActivePlayerPtr();
	EntityRef erPet = scp_GetSummonedPetEntRef(pEnt);
	if (erPet > 0)
	{
		Entity* pPet = entFromEntityRefAnyPartition(erPet);
		if (pPet && !entCheckFlag(pPet, ENTITYFLAG_UNSELECTABLE | ENTITYFLAG_UNTARGETABLE))
			return pPet;//don't return an unselectable or untargetable pet

	}
	return NULL;
}

// returns true if the given entity is the local player's SCP
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_IsEntityLocalPlayerPet);
int scp_ExprIsEntityLocalPlayerPet(Entity *pEnt)
{
	Entity *pLocalPlayerEnt = entActivePlayerPtr();
	if (pEnt && pLocalPlayerEnt)
	{
		EntityRef erPet = scp_GetSummonedPetEntRef(pLocalPlayerEnt);
		return erPet == entGetRef(pEnt);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetSummonedPetItem);
SA_RET_OP_VALID Item* scp_ExprGetSummonedPetItem()
{
	Entity *pEnt = entActivePlayerPtr();
	Item* pPetItem = scp_GetSummonedPetItem(pEnt);
	return pPetItem;
}

//returns whether pEnt (NOT ActivePlayer) has a summoned pet (e.g. to inspect).
//used by NW entityContextMenu to decide whether to show "Inspect Pet..."
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_EntHasPet);
int scp_ExprEntHasPet(EntityRef hEnt)
{
	Entity* pEnt = entFromEntityRef(PARTITION_CLIENT, hEnt);
	if(!pEnt || !pEnt->pSaved || !pEnt->pSaved->pSCPData)
		return 0;
	if( pEnt->pSaved->pSCPData->iSummonedSCP >= 0 )	//-1 means unsummoned; 0 means slot 0
		return 1;
	return 0;
}

//requests hEnt's pet's item from the server.  Called when Pet Inspect is started on a player.
//Must only be called from a Gen's PointerUpdate block.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetOtherPlayerPet) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void scp_ExprGetOtherPlayerPet(EntityRef hEnt)
{
	ServerCmd_scp_CmdGetPlayerPet(hEnt);
	//destroy current data here, because this is definitely called from from UIGen PointerUpdate;
	//the callback from CmdGetPlayerPet might not be.
	if(s_pFakeInspectSCPEntity)
	{
		StructDestroy(parse_Entity, s_pFakeInspectSCPEntity);
		s_pFakeInspectSCPEntity = NULL;
		StructDestroy(parse_Item, s_pInspectSCPItem);
		s_pInspectSCPItem = NULL;
	}
}

//receives a SCP pet item from the server
AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void scp_CmdRecievePetItem(Item* pItem)
{
	if(!s_pFakeInspectSCPEntity && pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
	{
		//can't call this out of Gen PointerUpdate if it might trash s_pFakeInspectSCPEntity.
		scp_ExprCreateFakePetInspectEnt(pItem);
	}
	//else probably means the player inspected another pet before the server responded, so the player 
	//shouldn't notice that we don't update the gen here.
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_IsSummonedPetDead);
bool scp_ExprIsSummonedPetDead()
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData && pData->iSummonedSCP > -1)
	{
		Item* pItem = scp_GetActivePetItem(pEnt, pData->iSummonedSCP);
		return scp_CheckFlag(pItem, kSuperCritterPetFlag_Dead);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetSummonedPetName);
SA_RET_OP_STR const char* scp_ExprGetSummonedPetName()
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData && pData->iSummonedSCP > -1)
	{
		return scp_GetActivePetName(pEnt, pData->iSummonedSCP);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_ReviveSummonedPet);
void scp_ExprReviveSummonedPet()
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	if (pData && pData->iSummonedSCP > -1)
	{
		Item* pItem = scp_GetActivePetItem(pEnt, pData->iSummonedSCP);
		if (scp_CheckFlag(pItem, kSuperCritterPetFlag_Dead))
		{
			ServerCmd_scp_RevivePet(pData->iSummonedSCP);
			return;
		}
	}
	return;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetName);
SA_RET_OP_STR const char* scp_ExprGetPetName(SA_PARAM_OP_VALID Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
	{
		SuperCritterPet* pPet = pItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		CritterDef* pCritterDef = pDef ? GET_REF(pDef->hCritterDef) : NULL;
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

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetClass);
SA_RET_OP_STR const char* scp_ExprGetPetGenericName(SA_PARAM_OP_VALID Item* pItem)
{
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pSuperCritterPet)
	{
		SuperCritterPet* pPet = pItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef* pDef = GET_REF(pPet->hPetDef);
		CritterDef* pCritterDef = pDef ? GET_REF(pDef->hCritterDef) : NULL;
		if (pCritterDef)
		{
			return TranslateDisplayMessage(pCritterDef->displayNameMsg);
		}
	}
	return NULL;
}

//returns true if the user has named this pet.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_PetHasCustomName);
int scp_ExprPetHasCustomName(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	SuperCritterPet* pPet = scp_GetPetFromItem(scp_GetActivePetItem(pEnt, idx));
	if (pPet && pPet->pchName)
	{
		return 1;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_PetIsSummoned);
bool scp_ExprPetIsSummoned(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	ActiveSuperCritterPet* pPet = NULL;

	if (!pEnt || !pData)
		return false;

	pPet = eaGet(&pData->ppSuperCritterPets, idx);
	return pPet && (idx == pData->iSummonedSCP);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_PetIsDead);
bool scp_ExprPetIsDead(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	Item* pItem = NULL;

	if (!pEnt || !pData || pData->iSummonedSCP == -1)	//iSummonedSCP == -1 means no pet summoned.
		return false;

	pItem = scp_GetActivePetItem(pEnt, pData->iSummonedSCP);
		return scp_CheckFlag(pItem, kSuperCritterPetFlag_Dead);
	}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_PetIsSlotted);
bool scp_ExprPetIsSlotted(int idx)
{
	Entity *pEnt = entActivePlayerPtr();
	return !!scp_GetActivePetItem(pEnt, idx);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_CanPetEquip);
int scp_ExprCanPetEquip(SA_PARAM_OP_VALID Item* pPetItem, int iEquipSlot, SA_PARAM_OP_VALID Item* pItem)
{
	SuperCritterPet* pPet = scp_GetPetFromItem(pPetItem);
	return pPet ? scp_CanEquip(pPet, iEquipSlot, pItem) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetEquipmentSlotType);
const char* scp_ExprGetPetEquipmentSlotType(SA_PARAM_OP_VALID Item* pPetItem, int iSlot)
{
	Entity *pEnt = entActivePlayerPtr();
	SuperCritterPetDef* pDef = scp_GetPetDefFromItem(pPetItem);
	SCPEquipSlotDef* pSlot = pDef ? eaGet(&pDef->ppEquipSlots, iSlot) : NULL;
	if( pSlot && pSlot->peCategories)
	{
		// if there are additional restrictions on the item type, use the first restriction instead.
		// This is so that we can see off-hand sub-types.
		return StaticDefineInt_FastIntToString(ItemCategoryEnum, pSlot->peCategories[0]);
	}
	return StaticDefineInt_FastIntToString(InvBagIDsEnum, pSlot ? pSlot->eID : 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetPetEquipmentSlotTypeTranslated);
const char* scp_ExprGetPetEquipmentSlotTypeTranslate(SA_PARAM_OP_VALID Item* pPetItem, int iSlot)
{
	Entity *pEnt = entActivePlayerPtr();
	SuperCritterPetDef* pDef = scp_GetPetDefFromItem(pPetItem);
	SCPEquipSlotDef* pSlot = pDef ? eaGet(&pDef->ppEquipSlots, iSlot) : NULL;
	if( pSlot && pSlot->peCategories)
	{
		// if there are additional restrictions on the item type, use the first restriction instead.
		// This is so that we can see off-hand sub-types.
		return StaticDefineGetTranslatedMessage(ItemCategoryEnum, pSlot->peCategories[0]);
	}
	return StaticDefineGetTranslatedMessage(InvBagIDsEnum, pSlot ? pSlot->eID : 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_PetEquipmentSlotIsLocked);
bool scp_ExprPetEquipmentSlotIsLocked(int idx, int iSlot)
{
	Entity *pEnt = entActivePlayerPtr();
	return scp_IsEquipSlotLocked(pEnt, idx, iSlot);
}

// Move an item to an from an inventory slot to an entity's inventory.
// The destination can't be an InventorySlot because it might not exist.
// expr name is for binding but this is also the move SCP function.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyBindSCPAndName);
bool gclInv_InventoryKeyMoveSCP(const char *pchKeyA, S32 eBagB, S32 iSlotB, S32 iCount, const char* pchBackupBagName, const char* pchNewName)
{
	UIInventoryKey KeyA = {0};
	Item *pItem;
	ItemDef *pDef;
	SuperCritterPet *pPet;
	int iBackupBagID = StaticDefineInt_FastStringToInt(InvBagIDsEnum, pchBackupBagName, 0);

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (!KeyA.pSlot || !KeyA.pSlot->pItem || KeyA.pOwner != entActivePlayerPtr())
		return false;
	
	pItem = KeyA.pSlot->pItem;
	pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	pPet = pItem && pItem->pSpecialProps ? pItem->pSpecialProps->pSuperCritterPet : NULL;
	if (!pPet || !pDef)
		return false;

	if (pItem->flags & kItemFlag_Bound && (KeyA.eBag == iBackupBagID || KeyA.eBag == InvBagIDs_SuperCritterPets))
	{
		//already bound and source bag is SCP bag (dst) or inactive pet bag (backup).
		ServerCmd_scp_MoveBoundPet(eBagB, iSlotB, KeyA.eBag, KeyA.iSlot);
	}
	else
	{
		ServerCmd_scp_BindPet(eBagB, iSlotB, KeyA.eBag, KeyA.iSlot, iBackupBagID, pchNewName);
	}
	return true;
}

//wrapper for gclInv_InventoryKeyMoveSCP without rename.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyMoveSCP);
bool gclInvExprInventoryKeyMoveSCP(const char *pchKeyA, S32 eBagB, S32 iSlotB, S32 iCount, const char* pchBackupBagName)
{
	return gclInv_InventoryKeyMoveSCP(pchKeyA, eBagB, iSlotB, iCount, pchBackupBagName, NULL);
}

// Move an item to an from an inventory slot to an entity's inventory.
// The destination can't be an InventorySlot because it might not exist.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyEquipPet);
bool gclInvExprInventoryKeyEquipPet(const char *pchKeyA, int iPetIdx, S32 iSlot)
{
	UIInventoryKey KeyA = {0};
	Item *pItem;
	ItemDef *pDef;
	SuperCritterPet *pPet;
	SuperCritterPetDef* pPetDef;
	Item* pPetItem = NULL;
	bool bValid = false;
	S32 i;

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (!KeyA.pSlot || !KeyA.pSlot->pItem || KeyA.pOwner != entActivePlayerPtr())
		return false;

	pItem = KeyA.pSlot->pItem;
	pDef = GET_REF(pItem->hItem);
	if (!pDef)
		return false;

	pPetItem = inv_GetItemFromBag(KeyA.pEntity, InvBagIDs_SuperCritterPets, iPetIdx, KeyA.pExtract);

	if ((pDef->eType != kItemType_Upgrade &&
		pDef->eType != kItemType_Weapon) ||
		!pPetItem || !pPetItem->pSpecialProps ||
		!pPetItem->pSpecialProps->pSuperCritterPet ||
		item_IsUnidentified(pItem))
		return false;

	pPet = pPetItem->pSpecialProps->pSuperCritterPet;
	pPetDef = GET_REF(pPet->hPetDef);
	for (i = 0; i < eaiSize(&pDef->peRestrictBagIDs); i++)
	{
		if (pDef->peRestrictBagIDs[i] == pPetDef->ppEquipSlots[iSlot]->eID)
		{
			bValid = true;
			break;
		}
	}

	if (!bValid)
		return false;
	
	if (!KeyA.iContainerID && KeyA.eType == GLOBALTYPE_ENTITYCRITTER)
	{
		ServerCmd_scp_MovePetEquipmentToPet(iPetIdx, KeyA.iSlot, iSlot);
	}
	
	ServerCmd_scp_MovePetEquipment(iPetIdx, iSlot, KeyA.eBag, KeyA.iSlot, true);
	
	return true;
}

// Move an item to an from an inventory slot to an entity's inventory.
// The destination can't be an InventorySlot because it might not exist.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyUnequipPet);
bool gclInvExprInventoryKeyUnequipPet(const char *pchKeyA, int iBagID, S32 iSlot)
{
	UIInventoryKey KeyA = {0};

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;

	ServerCmd_scp_MovePetEquipment(KeyA.eBag, KeyA.iSlot, iBagID, iSlot, false);
	return true;
}

// Get the drag key for this SCP.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(SuperCritterPetGetKey);
const char *gclInvExprSuperCritterPetGetKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 eBag, int idx)
{
	UIInventoryKey Key = {0};
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), eBag, pExtract);
	InventorySlot* pSlot = inv_GetSlotFromBag(pBag, idx);
	if (!pBag || !pSlot)
		return "";
	gclInventoryMakeSlotKey(pEntity, pBag, pSlot, &Key);
	return gclInventoryMakeKeyString(pContext, &Key);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetSCPIcon) ACMD_HIDE;
const char* scp_ExprGetSCPIcon(SA_PARAM_OP_VALID Item* pPetItem)
{
	if (pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet)
	{
		SuperCritterPetDef* pPetDef = GET_REF(pPetItem->pSpecialProps->pSuperCritterPet->hPetDef);
		if(pPetDef->pchIconName)
			return pPetDef->pchIconName;
		return item_GetIconName(pPetItem, NULL);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_UnbindPet) ACMD_HIDE;
void scp_ExprUnbindPet(int iBag, int iSlot)
{
	ServerCmd_scp_UnbindPet(iBag, iSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_UnbindPetDragDrop) ACMD_HIDE;
void scp_ExprUnbindPetDragDrop(const char *pchKeyA)
{
	UIInventoryKey KeyA = {0};

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return;

	scp_ExprUnbindPet(KeyA.eBag, KeyA.iSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_canUnbind) ACMD_HIDE;
int scp_ExprCanUnbind(SA_PARAM_OP_VALID Item* pPetItem)
{
	ItemDef* pPetItemDef = pPetItem ? GET_REF(pPetItem->hItem) : NULL;
	if (pPetItem)
	{
		return !(pPetItemDef->flags & kItemDefFlag_BindOnPickup);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetUnbindCost) ACMD_HIDE;
S32 scp_ExprGetPetUnbindCost(SA_PARAM_OP_VALID Item* pPetItem)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	if (pPetItem)
		return scp_EvalUnbindCost(pPlayerEnt, pPetItem);
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_SetPetSummonState);
void scp_ExprSetPetSummonState(int idx, bool bSummon)
{
	ServerCmd_scp_CmdSetPetState(idx, bSummon);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_RevivePet) ACMD_HIDE;
void scp_ExprRevivePet(int idx)
{
	ServerCmd_scp_RevivePet(idx);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_SetPetCostume);
void scp_ExprSetPetCostume(int idx, int iCostume)
{
	ServerCmd_scp_ChangeCostume(idx, iCostume);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_SendPetForTraining);
void scp_ExprSendPetForTraining(int idx)
{
	ServerCmd_scp_SendActivePetForTraining(idx);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_RenamePet);
void scp_ExprRenamePet(int idx, char* pchNewName)
{
	ServerCmd_scp_Rename(idx, pchNewName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_MakePetEquipSlotKey);
const char* scp_ExprMakePetEquipSlotKey(ExprContext* pContext, int idx, int iSlot)
{
	UIInventoryKey Key = {0};
	Entity* pEntity = entActivePlayerPtr();
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEntity);
	if (pData && idx < eaSize(&pData->ppSuperCritterPets))
	{
		InventoryBag* pPetBag = pData->ppSuperCritterPets[idx]->pEquipment;

		Key.pOwner = pEntity;
		Key.pEntity = pEntity;
		Key.pExtract = entity_GetCachedGameAccountDataExtract(pEntity);

		Key.erOwner = entGetRef(pEntity);
		Key.eBag = idx;
		Key.iSlot = iSlot;
		Key.pBag = pPetBag;
		Key.eType = GLOBALTYPE_ENTITYCRITTER;

		return gclInventoryMakeKeyString(pContext, &Key);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetEquipSlotUnlockLevel);
int scp_ExprGetEquipSlotUnlockLevel(int iSlot)
{
	if (iSlot > 0 && iSlot < eaiSize(&g_SCPConfig.eaEquipSlotUnlockLevels))
		return g_SCPConfig.eaEquipSlotUnlockLevels[iSlot];

	return 1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(scp_GetGemSlotUnlockLevel);
int scp_ExprGetGemSlotUnlockLevel(int iSlot)
{
	if (iSlot > 0 && iSlot < eaiSize(&g_SCPConfig.eaGemSlotUnlockLevels))
		return g_SCPConfig.eaGemSlotUnlockLevels[iSlot];

	return 1;
}



#include "AutoGen/gclSuperCritterPet_h_ast.c"
#include "AutoGen/gclSuperCritterPet_c_ast.c"