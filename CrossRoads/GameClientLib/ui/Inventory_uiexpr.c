/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "expression.h"
#include "EntitySavedData.h"
#include "estring.h"
#include "earray.h"
#include "EntityLib.h"
#include "Player.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowerGrid.h"
#include "PowerGrid_h_ast.h"
#include "PowerModes.h"
#include "PowersAutoDesc.h"
#include "Character.h"
#include "CharacterClass.h"
#include "character_combat.h"
#include "CombatEval.h"
#include "FCInventoryUI.h"
#include "Guild.h"
#include "gclEntity.h"
#include "gclUIGen.h"
#include "tradeCommon.h"
#include "StringCache.h"
#include "UIGen.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "GameStringFormat.h"
#include "cmdClient.h"
#include "entCritter.h"
#include "OfficerCommon.h"
#include "PowerTree.h"
#include "species_common.h"
#include "AlgoPet.h"
#include "skills_DD.h"
#include "itemCommon.h"
#include "DiaryCommon.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "SavedPetCommon.h"
#include "Expression.h"
#include "ExpressionPrivate.h"
#include "BlockEarray.h"
#include "GameAccountDataCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "GamePermissionsCommon.h"
#include "MapDescription.h"
#include "contact_common.h"
#include "mission_common.h"
#include "storeCommon.h"

#include "../Common/item/itemDescCommon.h"

#include "Inventory_uiexpr_c_ast.h"
#include "Player_h_ast.h"
#include "PowersAutoDesc_h_ast.h"
#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"

extern InventorySlot **g_eaEmptySlots;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

const char* GenExprGetItemDescriptionCustom(ExprContext* pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID ItemDef *pItemDef, const char *pchItemValue, const char *pchSkillColors, ACMD_EXPR_DICT(Message) const char *pchDescriptionKey, ACMD_EXPR_DICT(Message) const char *pchPowerKey, ACMD_EXPR_DICT(Message) const char *pchModKey, ACMD_EXPR_DICT(Message) const char *pchCraftKey);

static SavedPetDisplayData **s_eaSavedPetDisplayData;
static ItemRewardPackRequestData **s_eaRewardPackData = NULL;

AUTO_STRUCT;
typedef struct InventoryBagData
{
	InventorySlot** eaSlots; AST(UNOWNED)
} InventoryBagData;

// Name value pair for numeric items.  Used in UI.
AUTO_STRUCT;
typedef struct NumericItemListNode
{
	char* pchName; AST(ESTRING)
	int iValue;
} NumericItemListNode;

AUTO_STRUCT;
typedef struct RewardPackDisplayData
{
	const char* pchItemDisplayName; AST(UNOWNED)
	InventorySlot* pSlot; AST(UNOWNED)
} RewardPackDisplayData;

AUTO_FIXUPFUNC;
TextParserResult InventoryBagDataFixup(InventoryBagData *pInventoryBagData, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pInventoryBagData->eaSlots);
		break;
	}
	return PARSERESULT_SUCCESS;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetIntVar);
int GenExprGetIntVar(SA_PRE_NN_VALID SA_POST_OP_VALID UIGen *pGen, const char* pchVarName)
{
	while (pGen)
	{
		UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVarName);
		if (pGlob)
			return pGlob->iInt;
		pGen = pGen->pParent;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetStringVar);
const char* GenExprGetStringVar(SA_PRE_NN_VALID SA_POST_OP_VALID UIGen *pGen, const char* pchVarName )
{
	while (pGen)
	{
		UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVarName);
		if (pGlob)
			return pGlob->pchString;
		pGen = pGen->pParent;
	}
	return "";
}


static void gclGetInventoryBagRange(SA_PARAM_OP_VALID Entity *pEnt, InvBagIDs eBagID, U32 iStartSlot, U32 iNumSlots, S32 iSkipSlot, InventorySlot ***peaSlots, GameAccountDataExtract *pExtract)
{
	MAX1F(iStartSlot,0);
	if (pEnt)
	{
		int i, iMaxSlots;
		int iEndSlot = iStartSlot + iNumSlots;
		InventoryBag *pBag;
		ANALYSIS_ASSUME(pEnt != NULL);
		pBag = eBagID ? CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract)) : NULL;
		iMaxSlots = eBagID ? inv_ent_GetMaxSlots(pEnt, eBagID, pExtract) : 0;

		if (iNumSlots == 0) {
			iEndSlot = iMaxSlots;
		}
		
		for (i = iStartSlot; iMaxSlots == -1 || (i < iMaxSlots && i < iEndSlot); i++) {
			if (i != iSkipSlot) {
				InventorySlot* pSlot = inv_ent_GetSlotPtr(pEnt, eBagID, i, pExtract);
				
				if (!pSlot) {
					if (iMaxSlots == -1) {
						break;
					}
					pSlot = gclInventoryUpdateNullSlot(pEnt, pBag, i);
				} else {
					gclInventoryUpdateSlot(pEnt, pBag, pSlot);
				}
				//else if(pSlot->pchName)
				//{
				//	devassertmsg(i == strtol(pSlot->pchName,NULL,10), "FIXME(jransom): Document my asserts and don't use them in production mode.");
				//}
				
				eaPush(peaSlots, pSlot);
			}
		}
	}
}

static void gclGenGetInventoryBagRange(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, InvBagIDs eBagID, U32 iStartSlot, U32 iNumSlots, S32 iSkipSlot, GameAccountDataExtract *pExtract)
{
	InventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);
	
	eaClearFast(peaSlots);

	if ( eBagID != InvBagIDs_None )
	{
		gclGetInventoryBagRange(pEnt, eBagID, iStartSlot, iNumSlots, iSkipSlot, peaSlots, pExtract);
	}

	ui_GenSetManagedListSafe(pGen, peaSlots, InventorySlot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInventoryBag);
void GenExprGetInventoryBag(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pcBagName)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InvBagIDs eBagID = InvBagIDs_None;
	if ( pcBagName && pcBagName[0] )
	{
		eBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
	}

	gclGenGetInventoryBagRange(pGen, pEnt, eBagID, 0, 0, -1, pExtract);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInventoryList);
void GenExprGetInventoryList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pcBagName)
{
	InventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);
	InvBagIDs eBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
	
	eaClearFast(peaSlots);

	if ( pEnt && eBagID != InvBagIDs_None )
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag *pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract));
		int i, iMaxSlots = inv_ent_GetMaxSlots(pEnt, eBagID, pExtract);

		for (i = 0; i < iMaxSlots; i++)
		{
			InventorySlot* pSlot = inv_ent_GetSlotPtr(pEnt, eBagID, i, pExtract);
			if (!pSlot || !pSlot->pItem) continue;
			gclInventoryUpdateSlot(pEnt, pBag, pSlot);
			eaPush(peaSlots, pSlot);
		}
	}

	ui_GenSetManagedListSafe(pGen, peaSlots, InventorySlot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInventoryBagSize);
int GenExprGetInventoryBagSize(SA_PARAM_OP_VALID Entity *pEnt, const char* pcBagName)
{
	InvBagIDs iBagID;
	int iResult = 0;

	if ( pEnt && pcBagName && pcBagName[0] )
	{
		iBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
		if ( iBagID != InvBagIDs_None )
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			iResult = inv_ent_GetMaxSlots(pEnt, iBagID, pExtract);
		}
	}

	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInventoryBagPartitioned);
void GenExprGetInventoryBagPartitioned(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pcBagName, S32 iPartitionSize)
{
	InventoryBagData ***peaBags = ui_GenGetManagedListSafe(pGen, InventoryBagData);
	InvBagIDs eBagID = InvBagIDs_None;
	int i = 0, n = 0;
	
	if ( pcBagName && pcBagName[0] )
	{
		eBagID = StaticDefineIntGetInt(InvBagIDsEnum, pcBagName);
		
		if ( eBagID != InvBagIDs_None )
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			int iBagSize = inv_ent_GetMaxSlots(pEnt, eBagID, pExtract);

			while ( n < iBagSize )
			{
				InventoryBagData* pData = eaGetStruct(peaBags, parse_InventoryBagData, i++);

				eaClearFast(&pData->eaSlots);
				gclGetInventoryBagRange(pEnt, eBagID, n, iPartitionSize, -1, &pData->eaSlots, pExtract);

				n += iPartitionSize;
			}
		}
	}

	while (eaSize(peaBags) > i)
		StructDestroy(parse_InventoryBagData, eaPop(peaBags));

	ui_GenSetManagedListSafe(pGen, peaBags, InventoryBagData, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetListFromInventoryBagData);
void GenExprSetListFromInventoryBagData(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBagData* pData)
{
	if ( pData )
	{
		ui_GenSetList(pGen, &pData->eaSlots, parse_InventorySlot);
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagGetChangeActiveSlotDelay);
F32 GenExprBagGetChangeActiveSlotDelay(SA_PARAM_OP_VALID Entity *pEnt, S32 eBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);
	if (pDef)
	{
		return pDef->fChangeActiveSlotDelay;
	}
	return 0.0f;
}

static void GetActiveSlotInBagInternal(UIGen *pGen, Entity *pEnt, S32 eBagID, S32 index)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);

	S32 slot = invbag_GetActiveSlot(pBag, index);
	if (slot == -1)
	{
		ui_GenSetList(pGen, NULL, NULL);
		return;
	}
		
	if (pDef && (pDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		gclGenGetInventoryBagRange(pGen, pEnt, eBagID, slot, 1, -1, pExtract);
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetActiveSlotInBag);
void GenExprGetActiveSlotInBag(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 eBagID)
{
	GetActiveSlotInBagInternal(pGen, pEnt, eBagID, 0);
}

// with multiple active slots enabled, choose which 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetActiveSlotWithIndexInBag);
void GenExprGetActiveSlotWithIndexInBag(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 eBagID, S32 index)
{
	GetActiveSlotInBagInternal(pGen, pEnt, eBagID, index);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetInactiveSlotsInBag);
void GenExprGetInactiveSlotsInBag(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 eBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);
	S32 iSkipSlot = -1;

	if (pDef && (pDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		iSkipSlot = invbag_GetActiveSlot(pBag, 0);
	}
	gclGenGetInventoryBagRange(pGen, pEnt, eBagID, 0, 0, iSkipSlot, pExtract);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagCanChangeActiveSlot);
bool GenExprBagCanChangeActiveSlot(SA_PARAM_OP_VALID Entity *pEnt, S32 eBagID, S32 iNewActiveSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);

	return invbag_CanChangeActiveSlot(pEnt, pBag, 0, iNewActiveSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagGetActiveSlot);
S32 GenExprBagGetActiveSlot(SA_PARAM_OP_VALID Entity *pEnt, S32 eBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);
	
	if (pDef && (pDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		return invbag_GetActiveSlot(pBag, 0);
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagGetFirstInactiveSlot);
S32 GenExprBagGetFirstInactiveSlot(SA_PARAM_OP_VALID Entity *pEnt, S32 eBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);
	
	if (pDef && (pDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		if (!eaiSize(&pBag->eaiActiveSlots) && !eaiSize(&pBag->eaiPredictedActiveSlots))
		{
			if (pDef->maxActiveSlots==1)
			{
				return 1;
			}
		}
		else
		{
			S32 i, j;
			for (i = 0; i < pDef->MaxSlots; ++i)
			{
				for (j = eaiSize(&pBag->eaiActiveSlots)-1; j >= 0; j--)
				{
					if (invbag_GetActiveSlot(pBag, j) == i)
					{
						break;
					}
				}
				if (j < 0)
				{
					return i;
				}
			}
		}
	}
	return -1;
}

static NOCONST(DiaryFilter) *s_currentVaultLogFilter = NULL;

// keeps track of various set of scratch tag bits that the UI may be using
//static U64 s_workingTagBits = 0;
static StashTable s_vaultLogScratchTagBits = NULL;

bool CheckVaultLogFilter(const char *text, DiaryFilter *filter)
{
	//if ( ( filter->startTime != 0 ) && ( displayHeader->displayTime < filter->startTime ) )
	//{
	//	return false;
	//}

	//if ( ( filter->endTime != 0 ) && ( displayHeader->displayTime > filter->endTime ) )
	//{
	//	return false;
	//}

	//if ( ( filter->type != DiaryEntryType_None ) && ( filter->type != displayHeader->type ) )
	//{
	//	return false;
	//}

	if ( ( filter->titleSubstring != NULL ) && ( ( text == NULL ) || ( strstri(text, filter->titleSubstring) == NULL ) ) )
	{
		return false;
	}

	//if ( ( filter->tagIncludeMask != 0 ) && ( ( filter->tagIncludeMask & displayHeader->tagBits ) == 0 ) )
	//{
	//	return false;
	//}

	//if ( ( filter->tagExcludeMask != 0 ) && ( ( filter->tagExcludeMask & displayHeader->tagBits ) != 0 ) )
	//{
	//	return false;
	//}

	return true;
}

//
// Implement named sets of scratch tag bits that can be used by different
//  parts of the UI at the same time, and not interfere with each other.
//
U64 *GetVaultLogScratchTagBits(const char *setName)
{
	U64 *ret;
	const char *pooledSetName = allocAddString(setName);

	if ( s_vaultLogScratchTagBits == NULL )
	{
		s_vaultLogScratchTagBits = stashTableCreateWithStringKeys(100, StashDefault);
	}

	if ( !stashFindPointer(s_vaultLogScratchTagBits, pooledSetName, &ret) )
	{
		// if we didn't find an entry in the table, then create one
		ret = (U64 *)malloc(sizeof(U64));
		*ret = 0;
		stashAddPointer(s_vaultLogScratchTagBits, pooledSetName, ret, false);
	}

	return ret;
}

NOCONST(DiaryFilter) *GetCurrentVaultLogFilter()
{
	if ( s_currentVaultLogFilter == NULL )
	{
		s_currentVaultLogFilter = StructCreateNoConst(parse_DiaryFilter);
	}

	return s_currentVaultLogFilter;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VaultLogSetCurrentFilter);
void exprVaultLogSetCurrentFilter(int entryType, const char *titleSubString, const char *tagSetInclude, const char *tagSetExclude)
{
	NOCONST(DiaryFilter) *filter = GetCurrentVaultLogFilter();
	U64 *pTagBits;

	// no time support right now
	filter->endTime = 0;
	filter->startTime = 0;

	filter->type = (DiaryEntryType)entryType;

	if ( filter->titleSubstring != NULL )
	{
		StructFreeString(filter->titleSubstring);
		filter->titleSubstring = NULL;
	}

	if ( ( titleSubString != NULL ) && ( titleSubString[0] != '\0' ) )
	{
		// only copy the string if it has something interesting in it
		filter->titleSubstring = StructAllocString(titleSubString);
	}

	pTagBits = GetVaultLogScratchTagBits(tagSetInclude);
	filter->tagIncludeMask = *pTagBits;

	pTagBits = GetVaultLogScratchTagBits(tagSetExclude);
	filter->tagExcludeMask = *pTagBits;

	return;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGuildBankLog);
void GenExprGetGuildBankLog(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, S32 iStart)
{
	static PlayerGuildLog **s_eaLog = NULL;
	static PlayerGuildLog **s_eaFilteredLog = NULL;
	static S32 iCurStart = -1;
	static U32 iLastUpdate = 0;
	static bool bWaiting = false;
	PlayerGuildLog *temp = NULL;
	S32 i;

	if (!guild_IsMember(pEnt)) {
		ui_GenSetList(pGen, NULL, NULL);
		return;
	}

	if (iCurStart == -1)
	{
		iLastUpdate = pEnt->pPlayer->pGuild->iUpdated + (bWaiting?1:0);
		eaClearStruct(&s_eaLog, parse_PlayerGuildLog);
		exprVaultLogSetCurrentFilter(0, NULL, "filterInclude", "filterExclude");
	}

	if (iStart != iCurStart) {
		iCurStart = iStart;
		if (iStart >= 0)
		{
			ServerCmd_Guild_RequestLog(iStart, 20);
			bWaiting = true;
		}
	}

	if (iStart >= 0 && iLastUpdate != pEnt->pPlayer->pGuild->iUpdated)
	{
		iLastUpdate = pEnt->pPlayer->pGuild->iUpdated;
		bWaiting = false;

		for (i = 0; i < eaSize(&pEnt->pPlayer->pGuild->eaBankLog); i++)
		{
			temp = StructClone(parse_PlayerGuildLog, pEnt->pPlayer->pGuild->eaBankLog[i]);
			if (eaSize(&s_eaLog) > iStart + i)
			{
				StructDestroy(parse_PlayerGuildLog, s_eaLog[(iStart + i)]);
				s_eaLog[(iStart + i)] = temp;
			}
			else
			{
				eaPush(&s_eaLog, temp);
			}
		}
	}

	eaClearFast(&s_eaFilteredLog);
	for (i = 0; i < eaSize(&s_eaLog); i++)
	{
		if (GetCurrentVaultLogFilter() && !CheckVaultLogFilter(s_eaLog[i]->pcLogEntry, (DiaryFilter*)GetCurrentVaultLogFilter()))
		{
			continue;
		}
		eaPush(&s_eaFilteredLog, s_eaLog[i]);
	}

	ui_GenSetManagedListSafe(pGen, &s_eaFilteredLog, PlayerGuildLog, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGuildBankLogFullSize);
int GenGetGuildBankLogFullSize(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild)
	{
		return pEnt->pPlayer->pGuild->iBankLogSize;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetLongestCooldownFromSlot);
F32 GenExprGetHighestItemCooldownFromSlot(const char* pBagName, int SlotIdx)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	if ( pEnt &&
		pBagName &&
		pBagName[0] )
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
		Item* pItem = inv_GetItemFromBag( pEnt, BagIdx, SlotIdx, pExtract);
		if (pItem)
			return item_GetLongestPowerCooldown(pItem);
	}
	return 0;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSlot);
void GenExprGetSlot(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pBagName, int SlotIdx)
{
	static InventorySlot **s_eaInventory;
	static InventorySlot s_EmptySlot;

	eaClear(&s_eaInventory);

	if ( pEnt &&
		pBagName &&
		pBagName[0] )
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
		InventorySlot* pSlot = inv_ent_GetSlotPtr( pEnt, BagIdx, SlotIdx, pExtract);

		if ( !pSlot )
			pSlot = &s_EmptySlot;

		ui_GenSetPointer(pGen, pSlot, parse_InventorySlot);
	}
	else
	{
		ui_GenSetPointer(pGen, &s_EmptySlot, parse_InventorySlot);
	}
}

// Returns the texture name used for the item icon given the item name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetItemIconTexNameFromItemName");
const char * GenExpr_GetItemIconTexNameFromItemName(SA_PARAM_OP_STR const char *pchItemName)
{
	const char* pchDefaultIcon = "default_item_icon";
	if (pchItemName && pchItemName[0])
	{
		ItemDef *pItemDef = item_DefFromName(pchItemName);
		if (pItemDef)
		{
			const char* pchIconName = item_GetIconName(NULL, pItemDef);
			if (pItemDef && pItemDef->eType == kItemType_Numeric)
			{
				return gclGetBestIconName(pchIconName, pchDefaultIcon);
			}
			return pchIconName;
		}
	}
	return pchDefaultIcon;
}

// Returns the texture name used for the item icon given the item name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetLoreCategoryNameFromItemName");
const char * GenExpr_GetLoreCategoryNameFromItemName(SA_PARAM_OP_STR const char *pchItemName)
{
	if (pchItemName == NULL || pchItemName[0] == '\0')
	{
		return "";
	}
	else
	{
		ItemDef *pItemDef = item_DefFromName(pchItemName);
		return pItemDef == NULL ? "" : StaticDefineIntRevLookup(LoreCategoryEnum, pItemDef->iLoreCategory);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetItemIconTexName");
const char* GenExpr_GetItemIconTexName(SA_PARAM_OP_VALID Item *pItem)
{
	if ( pItem )
	{
		const char* pchIconName = item_GetIconName(pItem, NULL);
		ItemDef* pItemDef = GET_REF(pItem->hItem);
		if (pItemDef && pItemDef->eType == kItemType_Numeric)
		{
			return gclGetBestIconName(pchIconName, "default_item_icon");
		}
		return pchIconName;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemGetDef, GenGetItemDef);
SA_RET_OP_VALID ItemDef *GenExprGetItemDef(ExprContext* pContext, SA_PARAM_OP_VALID Item *pItem)
{
	return pItem ? GET_REF(pItem->hItem) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemGetName, GenGetItemName);
const char *GenExprGetItemName(ExprContext* pContext, SA_PARAM_OP_VALID Item *pItem)
{
	Entity *entPlayer = entActiveOrSelectedPlayer();
	const char *pcName = "";
	if (pItem && entPlayer)
	{
		pcName = item_GetNameLang(pItem, locGetLanguage(getCurrentLocale()), entPlayer);
	}
	return pcName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetItemDefName);
const char *exprGetItemDefName(ExprContext* pContext, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	return pItemDef ? item_GetDefLocalName(pItemDef, locGetLanguage(getCurrentLocale())) : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetItemDefNameFromRef);
const char *exprGetItemDefNameFromRef(ExprContext* pContext, const char *pchItemDef)
{
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pchItemDef);
	return pItemDef ? item_GetDefLocalName(pItemDef, locGetLanguage(getCurrentLocale())) : "";
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemDefValue);
int GenExprGetItemDefValue(ExprContext* pContext, SA_PARAM_OP_VALID ItemDef *pItemDef, const char *pchResources)
{
	int iResult = 0;
	int iResourcesValue = 0;
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, "Player");

	if (pchResources && pchResources[0])
	{
		ItemDef *pResourceDef = item_DefFromName(pchResources);
		iResourcesValue = pResourceDef ? item_GetDefEPValue(PARTITION_CLIENT, pEnt, pResourceDef, pResourceDef->iLevel, pResourceDef->Quality) : iResourcesValue;
	}

	if (pItemDef)
	{
		iResult = item_GetDefEPValue(PARTITION_CLIENT, pEnt, pItemDef, pItemDef->iLevel, pItemDef->Quality);
		if (iResourcesValue != 0)
			iResult /= iResourcesValue;
	}

	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemSellValue);
int GenExprGetItemSellValue(ExprContext *pContext, int iBagID, int iSlot)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, "Player");

	if(pEnt)
	{
		ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

		if (pContactDialog && pContactDialog->eaSellableItemInfo)
		{
			int i;

			for(i = 0; i < eaSize(&pContactDialog->eaSellableItemInfo); ++i)
			{
				StoreSellableItemInfo *pInfo = pContactDialog->eaSellableItemInfo[i];

				if(pInfo && pInfo->iBagID == iBagID && pInfo->iSlot == iSlot)
				{
					return pInfo->iCost;
				}
			}
		}
	}
	
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemValue);
int GenExprGetItemValue(ExprContext* pContext, SA_PARAM_OP_VALID Item *pItem, const char *pchResources)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, "Player");

	return item_GetResourceValue(PARTITION_CLIENT, pEnt, pItem, pchResources);
}

static SavedPetDisplayData* gclFindSavedPetDisplayData(U32 uContainerID, bool bDestroyOldRequests)
{
	S32 i;
	U32 uCurrentTime = timeSecondsSince2000();
	SavedPetDisplayData* pFoundData = NULL;

	for (i = eaSize(&s_eaSavedPetDisplayData)-1; i >= 0; i--)
	{
		SavedPetDisplayData* pData = s_eaSavedPetDisplayData[i];
		
		// Remove any data that hasn't been requested in a while
		if (bDestroyOldRequests && pData->uLastRequestTime + 10 < uCurrentTime)
		{
			StructDestroy(parse_SavedPetDisplayData, eaRemoveFast(&s_eaSavedPetDisplayData, i));
		}
		else if (pData->iPetID == uContainerID)
		{
			pFoundData = pData;
			if (!bDestroyOldRequests)
			{
				break;
			}
		}
	}
	return pFoundData;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsPetDisplayDataRequested);
bool ExprIsPetDisplayDataRequested(SA_PARAM_OP_VALID Item* pItem)
{
	ItemContainerInfo *pContainerInfo = (pItem && pItem->pSpecialProps) ? pItem->pSpecialProps->pContainerInfo : NULL;
	if (pContainerInfo)
	{
		ContainerID iPetID = StringToContainerID(REF_STRING_FROM_HANDLE(pContainerInfo->hSavedPet));
		SavedPetDisplayData* pData = gclFindSavedPetDisplayData(iPetID, false);
		return pData && pData->bUpdateRequested;
	}
	return false;
}

static int SortSavedPetPowerDisplayData(const SavedPetPowerDisplayData** ppA, const SavedPetPowerDisplayData** ppB)
{
	const SavedPetPowerDisplayData* pA = (*ppA);
	const SavedPetPowerDisplayData* pB = (*ppB);
	PTNodeDef* pNodeDefA = GET_REF(pA->hNodeDef);
	PTNodeDef* pNodeDefB = GET_REF(pB->hNodeDef);
	PTNodeRankDef* pRankDefA = pNodeDefA ? eaGet(&pNodeDefA->ppRanks, pA->iRank) : NULL;
	PTNodeRankDef* pRankDefB = pNodeDefB ? eaGet(&pNodeDefB->ppRanks, pB->iRank) : NULL;
	PowerDef* pPowerDefA = pRankDefA ? GET_REF(pRankDefA->hPowerDef) : NULL;
	PowerDef* pPowerDefB = pRankDefB ? GET_REF(pRankDefB->hPowerDef) : NULL;
	PowerPurpose ePurposeA = kPowerPurpose_Uncategorized;
	PowerPurpose ePurposeB = kPowerPurpose_Uncategorized;

	if (pPowerDefA) {
		ePurposeA = pPowerDefA->ePurpose;
	} else if (pNodeDefA) {
		ePurposeA = pNodeDefA->ePurpose;
	}
	if (pPowerDefB) {
		ePurposeB = pPowerDefB->ePurpose;
	} else if (pNodeDefB) {
		ePurposeB = pNodeDefB->ePurpose;
	}
	if (ePurposeA != ePurposeB) {
		return ePurposeA - ePurposeB;
	}
	return stricmp(REF_STRING_FROM_HANDLE(pA->hNodeDef), REF_STRING_FROM_HANDLE(pB->hNodeDef));
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void ReceivePetDisplayData(SavedPetDisplayData *pDisplayData)
{
	SavedPetDisplayData* pData;

	if (!pDisplayData || !(pData = gclFindSavedPetDisplayData(pDisplayData->iPetID, false)))
	{
		return;
	}

	pData->bUpdateRequested = false;
	eaCopyStructs(&pDisplayData->eaPowerData, &pData->eaPowerData, parse_SavedPetPowerDisplayData);
}

static void gclCheckSavedPetDisplayData(void)
{
	U32 uOldestRequestTime = 0;
	S32 iOldestRequestIdx = -1;
	S32 i;
	// Don't let the request array grow larger than 10 elements
	if (eaSize(&s_eaSavedPetDisplayData) > 10) {
		for (i = eaSize(&s_eaSavedPetDisplayData)-2; i >= 0; i--)
		{
			SavedPetDisplayData* pCheckData = s_eaSavedPetDisplayData[i];
			if (!uOldestRequestTime || pCheckData->uLastRequestTime < uOldestRequestTime)
			{
				uOldestRequestTime = pCheckData->uLastRequestTime;
				iOldestRequestIdx = i;
			}
		}
		if (iOldestRequestIdx >= 0)
		{
			StructDestroy(parse_SavedPetDisplayData, eaRemoveFast(&s_eaSavedPetDisplayData, iOldestRequestIdx));
		}
	}
}



static void gclRequestPowerTreeFromNodeDefName(const char* pchNodeName)
{
	static PowerTreeDefRef** s_eaPowerTreeRefs = NULL;
	char* estrPowerTreeName = NULL;

	estrStackCreate(&estrPowerTreeName);
	if (powertree_TreeNameFromNodeDefName(pchNodeName, &estrPowerTreeName))
	{
		S32 i;
		for (i = eaSize(&s_eaPowerTreeRefs)-1; i >= 0; i--)
		{
			PowerTreeDefRef* pRef = s_eaPowerTreeRefs[i];
			if (stricmp(REF_STRING_FROM_HANDLE(pRef->hRef), estrPowerTreeName)==0)
			{
				break;
			}
		}
		if (i < 0)
		{
			PowerTreeDefRef* pRef = StructCreate(parse_PowerTreeDefRef);
			SET_HANDLE_FROM_STRING("PowerTreeDef", estrPowerTreeName, pRef->hRef);
			eaPush(&s_eaPowerTreeRefs, pRef);
		}
	}
	estrDestroy(&estrPowerTreeName);
}

static void gclGetSavedPetPowerDesc(Entity* pEnt, PTNodeDef* pNodeDef, S32 iRank, bool bEscrow,
									const char* pchTitleMessageKey, 
									const char* pchPowerMessageKey,
									const char* pchAttribMessageKey,
									const char* pchRankMessageKey,
									bool* pbAddedTitle, char** pestrResult)
{
	PTNodeRankDef* pNodeRankDef = pNodeDef ? eaGet(&pNodeDef->ppRanks, iRank) : NULL;
	PowerDef *pPowerDef = pNodeRankDef ? GET_REF(pNodeRankDef->hPowerDef) : NULL;

	if(pPowerDef) {
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		static AutoDescPower* pAutoDesc = NULL;
		static char* s_estrPower = NULL;

		if(!pAutoDesc)
			pAutoDesc = StructCreate(parse_AutoDescPower);
		if(!(*pbAddedTitle) && pchTitleMessageKey && pchTitleMessageKey[0]) {
			FormatGameMessageKey(pestrResult, pchTitleMessageKey, STRFMT_END);
			(*pbAddedTitle) = true;
		}
		powerdef_AutoDesc(entGetPartitionIdx(pEnt), pPowerDef, NULL, pAutoDesc, NULL, NULL, NULL, NULL, NULL, NULL, 1, true, kAutoDescDetail_Normal, pExtract, NULL);
		powerdef_AutoDescCustom(pEnt, &s_estrPower, pPowerDef, pAutoDesc, pchPowerMessageKey, pchAttribMessageKey);

		FormatGameMessageKey(pestrResult, pchRankMessageKey, 
			STRFMT_INT("Rank", bEscrow?0:iRank+1), 
			STRFMT_STRING("Power", s_estrPower), 
			STRFMT_END);
		estrClear(&s_estrPower);

		if(pAutoDesc) {
			StructReset(parse_AutoDescPower, pAutoDesc);
		}
	}
}

void gclGetSavedPetPowerDescCustom(	SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Item *pItem, int iDepth, 
									ACMD_EXPR_DICT(Message) const char* pchTitleMessageKey, 
									ACMD_EXPR_DICT(Message) const char* pchPowerMessageKey, 
									ACMD_EXPR_DICT(Message) const char* pchAttribMessageKey, 
									ACMD_EXPR_DICT(Message) const char* pchRankMessageKey, 
									bool bPropagated, char** pestrResult)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	bool bAddedTitle = false;

	if ( !pestrResult )
		return;

	if(pItemDef && pItemDef->eType == kItemType_STOBridgeOfficer) {
		PetDef* pPet = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
		int i = 0;
		if(pPet) {
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			S32 numPowers = eaSize(&pPet->ppEscrowPowers);
			if(iDepth != -1)
				numPowers = MIN(numPowers, iDepth);

			for(i = 0; i < numPowers; i++) {
				PTNodeDef *pNode = GET_REF(pPet->ppEscrowPowers[i]->hNodeDef);
				if(!pNode)
				{
					const char* pchNodeName = REF_STRING_FROM_HANDLE(pPet->ppEscrowPowers[i]->hNodeDef);
					gclRequestPowerTreeFromNodeDefName(pchNodeName);
				}
				if(pNode && pNode->ppRanks && powertree_NodeHasPropagationPowers(pNode) == bPropagated) {
					PowerDef *pPowerDef = GET_REF(pNode->ppRanks[0]->hPowerDef);
					if(pPowerDef) {
						AutoDescPower* pAutoDesc = StructCreate(parse_AutoDescPower);
						if(pEnt) {
							if ( !bAddedTitle && pchTitleMessageKey && pchTitleMessageKey[0] ) {
								FormatGameMessageKey(pestrResult, pchTitleMessageKey, STRFMT_END);
								bAddedTitle = true;
							}
							powerdef_AutoDesc(entGetPartitionIdx(pEnt), pPowerDef, NULL, pAutoDesc, NULL, NULL, NULL, NULL, NULL, NULL, 1, true, kAutoDescDetail_Normal, pExtract, NULL);
							powerdef_AutoDescCustom(pEnt, pestrResult, pPowerDef, pAutoDesc, pchPowerMessageKey, pchAttribMessageKey);
						}
						if(pAutoDesc) {
							StructDestroy(parse_AutoDescPower, pAutoDesc);
						}
					}
				}
			}
		}
	} else if(pItemDef && pItemDef->eType == kItemType_AlgoPet) {
		AlgoPet *pAlgoPet = pItem->pSpecialProps ? pItem->pSpecialProps->pAlgoPet : NULL;
		int i;
		if(pAlgoPet)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			S32 numPowers = eaSize(&pAlgoPet->ppEscrowNodes);
			if(iDepth != -1)
				numPowers = MIN(numPowers, iDepth);

			for(i=0;i<numPowers;i++)
			{
				PTNodeDef *pNode = GET_REF(pAlgoPet->ppEscrowNodes[i]->hNodeDef);
				if(!pNode)
				{
					const char* pchNodeName = REF_STRING_FROM_HANDLE(pAlgoPet->ppEscrowNodes[i]->hNodeDef);
					gclRequestPowerTreeFromNodeDefName(pchNodeName);
				}
				if(pNode && pNode->ppRanks && powertree_NodeHasPropagationPowers(pNode) == bPropagated)
				{
					PowerDef *pPowerDef = GET_REF(pNode->ppRanks[0]->hPowerDef);
					if(pPowerDef) {
						AutoDescPower* pAutoDesc = StructCreate(parse_AutoDescPower);
						if ( !bAddedTitle && pchTitleMessageKey && pchTitleMessageKey[0] ) {
							FormatGameMessageKey(pestrResult, pchTitleMessageKey, STRFMT_END);
							bAddedTitle = true;
						}
						powerdef_AutoDesc(entGetPartitionIdx(pEnt), pPowerDef, NULL, pAutoDesc, NULL, NULL, NULL, NULL, NULL, NULL, 1, true, kAutoDescDetail_Normal, pExtract, NULL);
						powerdef_AutoDescCustom(pEnt, pestrResult, pPowerDef, pAutoDesc, pchPowerMessageKey, pchAttribMessageKey);
						if(pAutoDesc) {
							StructDestroy(parse_AutoDescPower, pAutoDesc);
						}
					}
				}
			}
		}
	} else if(pItemDef && pItemDef->eType == kItemType_Container) {
		ItemContainerInfo *pContainerInfo = pItem->pSpecialProps ? pItem->pSpecialProps->pContainerInfo : NULL;
		Entity* pPetEnt = pContainerInfo ? GET_REF(pContainerInfo->hSavedPet) : NULL;

		if(pPetEnt && pPetEnt->pChar && pPetEnt->pChar->ppPowerTrees && eaSize(&pPetEnt->pChar->ppPowerTrees)) {
			static PTNode** eaNodes = NULL;
			int i, j;
			S32 numTrees = eaSize(&pPetEnt->pChar->ppPowerTrees);
			eaClear(&eaNodes);
			for(i = 0; i < numTrees; i++) {
				PowerTree* pPowerTree = pPetEnt->pChar->ppPowerTrees[i];
				S32 numPowers = pPowerTree ? eaSize(&pPowerTree->ppNodes) : 0;
				if(iDepth != -1)
					numPowers = MIN(numPowers, iDepth);
				if(pPowerTree) {
					for(j=0;j<numPowers;j++)
					{
						PTNode *pNode = pPowerTree->ppNodes[j];
						PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
						if(pNodeDef && pNodeDef->ppRanks && powertree_NodeHasPropagationPowers(pNodeDef) == bPropagated && !(pNodeDef->eFlag & kNodeFlag_HideNode))
						{
							PowerDef *pPowerDef = GET_REF(pNodeDef->ppRanks[pNode->iRank]->hPowerDef);
							if(pPowerDef) {
								eaPush(&eaNodes, pNode);
								if(iDepth > 0) {
									iDepth--;
								}
							}
						}
					}
				}
			}
			if(eaSize(&eaNodes) > 0) {
				if(eaSize(&eaNodes) > 1) {
					eaQSort(eaNodes, ComparePTNodesByPurpose);	
				}
				for(i = 0; i < eaSize(&eaNodes); i++) {
					PTNode *pNode = eaNodes[i];
					PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
					
					gclGetSavedPetPowerDesc(pEnt, pNodeDef, pNode->iRank, pNode->bEscrow, 
						pchTitleMessageKey, pchPowerMessageKey, pchAttribMessageKey, pchRankMessageKey, 
						&bAddedTitle, pestrResult);
				}
			}
		} else if (pContainerInfo) {
			ContainerID iPetID = StringToContainerID(REF_STRING_FROM_HANDLE(pContainerInfo->hSavedPet));
			Entity* pOwner = pPetEnt && pPetEnt->pSaved ? entFromContainerIDAnyPartition(pPetEnt->pSaved->conOwner.containerType, pPetEnt->pSaved->conOwner.containerID) : NULL;
			S32 i;

			// Request display data
			SavedPetDisplayData* pDisplayData = gclFindSavedPetDisplayData(iPetID, true);
			if(!pDisplayData)
			{
				pDisplayData = StructCreate(parse_SavedPetDisplayData);
				pDisplayData->iPetID = iPetID;
				pDisplayData->bUpdateRequested = true;
				eaPush(&s_eaSavedPetDisplayData, pDisplayData);
				gclCheckSavedPetDisplayData();

				if(pOwner) {
					ServerCmd_RequestPetDisplayData(iPetID, entGetType(pOwner), entGetContainerID(pOwner), iDepth);
				} else {
					ServerCmd_RequestPetDisplayData(iPetID, GLOBALTYPE_NONE, 0, 0);
				}
			}
			pDisplayData->uLastRequestTime = timeSecondsSince2000();

			// Use cached display data
			if(pDisplayData && !pDisplayData->bUpdateRequested)
			{
				eaQSort(pDisplayData->eaPowerData, SortSavedPetPowerDisplayData);
				for(i = 0; i < eaSize(&pDisplayData->eaPowerData); i++)
				{
					SavedPetPowerDisplayData* pPowerDisplayData = pDisplayData->eaPowerData[i];
					PTNodeDef* pNodeDef = GET_REF(pPowerDisplayData->hNodeDef);

					if(powertree_NodeHasPropagationPowers(pNodeDef) != bPropagated) {
						continue;
					}
					gclGetSavedPetPowerDesc(pEnt, pNodeDef, pPowerDisplayData->iRank, pPowerDisplayData->bEscrow, 
						pchTitleMessageKey, pchPowerMessageKey, pchAttribMessageKey, pchRankMessageKey, 
						&bAddedTitle, pestrResult);
				}
			}
		}
	}
}

//Looks up the powers which bridge officer can teach and adds their description to ppchDesc
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSavedPetPowerDescCustom);
char* GenExprGetSavedPetPowerDescCustom(ExprContext* pContext, 
										SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Item *pItem, int iDepth, 
										ACMD_EXPR_DICT(Message) const char* pchTitleMessageKey, 
										ACMD_EXPR_DICT(Message) const char* pchPowerMessageKey, 
										ACMD_EXPR_DICT(Message) const char* pchAttribMessageKey,
										bool bPropagated)
{
	char* estrResult = NULL;
	char* result = NULL;
	
	gclGetSavedPetPowerDescCustom(	pEnt, pItem, iDepth, 
									pchTitleMessageKey, pchPowerMessageKey, pchAttribMessageKey, NULL,
									bPropagated, &estrResult);

	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);
}

//Looks up the powers which bridge officer can teach and adds their description to ppchDesc
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSavedPetPowerDescWithRankCustom);
char* GenExprGetSavedPetPowerDescWithRankCustom(ExprContext* pContext, 
										SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Item *pItem, int iDepth, 
										ACMD_EXPR_DICT(Message) const char* pchTitleMessageKey, 
										ACMD_EXPR_DICT(Message) const char* pchPowerMessageKey, 
										ACMD_EXPR_DICT(Message) const char* pchAttribMessageKey,
										ACMD_EXPR_DICT(Message) const char* pchRankMessageKey,
										bool bPropagated)
{
	char* estrResult = NULL;
	char* result = NULL;

	gclGetSavedPetPowerDescCustom(	pEnt, pItem, iDepth, 
		pchTitleMessageKey, pchPowerMessageKey, pchAttribMessageKey, pchRankMessageKey,
		bPropagated, &estrResult);

	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);
}

// A helper function to get the character class display name from PetDef
static const char* gclGetSavedPetClassDisplayName(PetDef* pPetDef)
{
	if (pPetDef)
	{
		CharacterClass* pClass = GET_REF(pPetDef->hClass);
		if (pClass)
		{
			return TranslateDisplayMessage(pClass->msgDisplayName);
		}
	}
	return NULL;
}

static void gclGetSavedPetDescCustom(	ExprContext* pContext, SA_PARAM_NN_VALID Item* pItem,
										ACMD_EXPR_DICT(Message) const char* pchDescriptionKey, 
										ACMD_EXPR_DICT(Message) const char* pchWrongAllegianceKey,
										char** pestrResult)
{
	ItemDef *pItemDef = GET_REF(pItem->hItem);
	Entity *pPlayerEnt = entActiveOrSelectedPlayer();

	if ( pItemDef && pchDescriptionKey && pchDescriptionKey[0] )
	{
		PetDef* pPetDef = GET_REF(pItemDef->hPetDef);
		AllegianceDef* pAllegianceDef = NULL;
		const char* pchSpeciesName = NULL;
		const char* pchClassName = gclGetSavedPetClassDisplayName(pPetDef);
		const char* pchRankName = NULL;
		const char* pchGender = NULL;
		const char* pchQualityName = NULL;
		char* pchPetName = NULL;
		char* pchAllegiance = NULL;

		estrCreate(&pchPetName);
		estrCreate(&pchAllegiance);

		if ( pItemDef->eType == kItemType_AlgoPet || pItemDef->eType == kItemType_STOBridgeOfficer )
		{
			CritterDef* pCritterDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
			AlgoPet* pAlgoPet = pItem->pSpecialProps ? pItem->pSpecialProps->pAlgoPet : NULL;
			SpeciesDef* pSpecies = pAlgoPet ? GET_REF(pAlgoPet->hSpecies) : NULL;
			pAllegianceDef = pPetDef ? GET_REF(pPetDef->hAllegiance) : NULL;
			item_GetDisplayNameFromPetCostume(pPlayerEnt, pItem, &pchPetName, NULL);
			pchSpeciesName = pSpecies ? TranslateDisplayMessage(pSpecies->displayNameMsg) : NULL;
			pchGender = pSpecies ? TranslateDisplayMessage( pSpecies->genderNameMsg ) : NULL;

			if ( pCritterDef && pAllegianceDef && Officer_GetRankCount(pAllegianceDef)>0 )
			{
				S32 i, iRank = 0;
				OfficerRankDef* pRankDef = NULL;
				for ( i = eaSize(&pCritterDef->ppCritterItems)-1; i >= 0; i-- )
				{
					DefaultItemDef* pDefaultDef = pCritterDef->ppCritterItems[i];
					ItemDef* pDef = GET_REF(pDefaultDef->hItem);
					if ( pDef && pDef->eType == kItemType_Numeric && stricmp(pDef->pchName,"StarfleetRank")==0 )
					{
						iRank = pDefaultDef->iCount;
						break;
					}
				}
				pRankDef = Officer_GetRankDef(iRank, pAllegianceDef, NULL);
				pchRankName = pRankDef ? TranslateDisplayMessage(*pRankDef->pDisplayMessage) : NULL;
			}
		} else if(pItemDef->eType == kItemType_Container) {
			ItemContainerInfo *pContainerInfo = pItem->pSpecialProps ? pItem->pSpecialProps->pContainerInfo : NULL;
			Entity* pPetEnt = pContainerInfo ? GET_REF(pContainerInfo->hSavedPet) : NULL;
			if(pPetEnt) {
				pAllegianceDef = GET_REF(pPetEnt->hAllegiance);
				estrCopy2(&pchPetName, entGetLocalName(pPetEnt));
				if(pPetEnt->pChar) {
					SpeciesDef *pSpecies = GET_REF(pPetEnt->pChar->hSpecies);
					CharacterClass *pClass = GET_REF(pPetEnt->pChar->hClass);
					pchSpeciesName = pSpecies ? TranslateDisplayMessage(pSpecies->displayNameMsg) : NULL;
					pchGender = pSpecies ? TranslateDisplayMessage( pSpecies->genderNameMsg ) : NULL;
					pchClassName = pClass ? TranslateDisplayMessage(pClass->msgDisplayName) : NULL;
				}

				if(pAllegianceDef) {
					S32 iRank = inv_GetNumericItemValue(pPetEnt,"StarfleetRank");
					OfficerRankDef* pOfficerRankDef = Officer_GetRankDef(iRank, pAllegianceDef, NULL);

					if ( pOfficerRankDef && pOfficerRankDef->pDisplayMessage )
						pchRankName =  TranslateMessageRef(pOfficerRankDef->pDisplayMessage->hMessage);
				}
			}
		}

		if(pAllegianceDef) {
			if(pPlayerEnt) {
				AllegianceDef* pPlayerAllegiance = GET_REF(pPlayerEnt->hAllegiance);
				AllegianceDef* pPlayerSubAllegiance = GET_REF(pPlayerEnt->hSubAllegiance);
				if(pPlayerAllegiance && pPlayerAllegiance != pAllegianceDef && pPlayerSubAllegiance != pAllegianceDef)
				{
					FormatMessageKey(&pchAllegiance, pchWrongAllegianceKey, STRFMT_STRING("Allegiance", TranslateDisplayMessage(pAllegianceDef->displayNameMsg)), STRFMT_END);
				}
			}
		}

		{
			char achMessageKey[2048];
			const char *pchKey = StaticDefineIntRevLookup(ItemQualityEnum, item_GetQuality(pItem));
			sprintf(achMessageKey, "StaticDefine_ItemQuality_%s", pchKey);
			pchQualityName = TranslateMessageKey(achMessageKey);
		}

		FormatGameMessageKey(pestrResult, pchDescriptionKey,
			STRFMT_ITEM(pItem),
			STRFMT_ITEMDEF(pItemDef),
			STRFMT_STRING("Name", NULL_TO_EMPTY(pchPetName)),
			STRFMT_INT("Quality", item_GetQuality(pItem)),
			STRFMT_STRING("QualityName", NULL_TO_EMPTY(pchQualityName)),
			STRFMT_STRING("Species", NULL_TO_EMPTY(pchSpeciesName)),
			STRFMT_STRING("Class", NULL_TO_EMPTY(pchClassName)),
			STRFMT_STRING("Rank", NULL_TO_EMPTY(pchRankName)),
			STRFMT_STRING("Allegiance", NULL_TO_EMPTY(pchAllegiance)),
			STRFMT_STRING("Gender", NULL_TO_EMPTY(pchGender)),
			STRFMT_END);

		estrDestroy(&pchPetName);
		estrDestroy(&pchAllegiance);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSavedPetDescCustom);
char* GenGetSavedPetDescCustom(	ExprContext* pContext, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Item *pItem,
								ACMD_EXPR_DICT(Message) const char* pchDescriptionKey,
								ACMD_EXPR_DICT(Message) const char* pchWrongAllegianceKey)
{
	char* estrResult = NULL;
	char* result = NULL;

	if ( pItem )
	{
		gclGetSavedPetDescCustom( pContext, pItem, pchDescriptionKey, pchWrongAllegianceKey, &estrResult );
	}

	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);
}

static void itemeval_GetItemTypesFromString(ExprContext* pContext, 
	const char* pchItemTypes, 
	ItemType** peaItemTypes)
{
	if (pchItemTypes && pchItemTypes[0]) {
		char* pchContext;
		char* pchStart;
		char* pchCopy;
		strdup_alloca(pchCopy, pchItemTypes);
		pchStart = strtok_r(pchCopy, " ,\t\r\n", &pchContext);
		do {
			if (pchStart) {
				ItemType eType = StaticDefineIntGetInt(ItemTypeEnum,pchStart);
				if (eType != -1) {
					eaiPush(peaItemTypes, eType);
				} else {
					const char* pchBlameFile = exprContextGetBlameFile(pContext);
					ErrorFilenamef(pchBlameFile, "Item Type %s not recognized", pchStart);
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
}

static void itemeval_GetItemCategoriesFromString(ExprContext* pContext, 
	const char* pchItemCategories, 
	ItemCategory** peaItemCategories)
{
	if (pchItemCategories && pchItemCategories[0]) {
		char* pchContext;
		char* pchStart;
		char* pchCopy;
		strdup_alloca(pchCopy, pchItemCategories);
		pchStart = strtok_r(pchCopy, " ,\t\r\n", &pchContext);
		do {
			if (pchStart) {
				ItemType eCategory = StaticDefineIntGetInt(ItemCategoryEnum,pchStart);
				if (eCategory != -1) {
					eaiPush(peaItemCategories, eCategory);
				} else {
					const char* pchBlameFile = exprContextGetBlameFile(pContext);
					ErrorFilenamef(pchBlameFile, "Item Category %s not recognized", pchStart);
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ListItemsByTypeAndCategory);
void gclInvExprListItemsByTypeAndCategory(ExprContext* pContext, 
	SA_PARAM_OP_VALID Entity *pEnt, 
	const char* pchTypes, 
	const char* pchCategories)
{
	GameAccountDataExtract *pExtract;
	ItemType* peItemTypes = NULL;
	ItemCategory* peItemCategories = NULL;
	int i, j;
	UIGen *pGen = (UIGen*)exprContextGetUserPtr(pContext, parse_UIGen);
	ItemListEntry ***peaInvItems = ui_GenGetManagedListSafe(pGen, ItemListEntry);

	if (!pEnt || !pEnt->pInventoryV2)
	{
		return;
	}

	itemeval_GetItemTypesFromString(pContext, pchTypes, &peItemTypes);
	itemeval_GetItemCategoriesFromString(pContext, pchCategories, &peItemCategories);

	if (!peItemTypes && !peItemCategories)
	{
		return;
	}

	eaClearStruct(peaInvItems, parse_ItemListEntry);

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--)
	{
		InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];
		if (!GamePermissions_trh_CanAccessBag(CONTAINER_NOCONST(Entity, pEnt), pBag->BagID, pExtract))
			continue;
		for (j = eaSize(&pBag->ppIndexedInventorySlots)-1; j >= 0; j--)
		{
			InventorySlot* pSlot = pBag->ppIndexedInventorySlots[j];
			ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
			if (pItemDef && (!peItemTypes || eaiFind(&peItemTypes, pItemDef->eType) >= 0))
			{
				if (!peItemCategories || itemdef_HasItemCategory(pItemDef, peItemCategories))
				{

					ItemListEntry *pNewEntry = StructCreate(parse_ItemListEntry);
					pNewEntry->pItem = pSlot->pItem;
					pNewEntry->iSlot = j;
					pNewEntry->id = pBag->BagID;
					eaPush(peaInvItems, pNewEntry);
				}
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, peaInvItems, ItemListEntry, true);

	eaiDestroy(&peItemTypes);
	eaiDestroy(&peItemCategories);
}



// returns true if actual appending happened
static bool inv_AppendItemDescMessage(char ** pestrOut,DisplayMessage * pMessage,Item * pItem)
{
	bool bAppended = false;
	char * estrTemp = NULL;
	estrStackCreate(&estrTemp);
	FormatDisplayMessage(&estrTemp,*pMessage,STRFMT_INT("ItemPowerFactor",item_GetPowerFactor(pItem)),STRFMT_END);
	if (estrTemp)
	{
		estrAppend2(pestrOut, estrTemp);
		bAppended = true;
	}
	estrDestroy(&estrTemp);

	return bAppended;
}

void Item_ItemPowersAutoDesc(Item *pItem, char **pestrDesc, const char *pchPowerMessageKey, const char *pchAttribMessageKey, S32 eActiveGemSlotType)
{
	Entity *pEnt = entActiveOrSelectedPlayer();
	Character *pChar = pEnt ? pEnt->pChar : NULL;
	int iLevel, iPower, iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	int iGemPowers = item_GetNumGemsPowerDefs(pItem);
	F32 *pfScales = NULL;
	int i = 0;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if (!pItem || !pChar || !pItemDef)
		return;

	iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pChar ? pChar->iLevelCombat : 1;

	if(pItemDef->eType == kItemType_Gem)
	{
		iLevel = 1;
	}/*
	if (pItemDef->eType == kItemType_TradeGood)
	{
		ItemPowerDefRef* pItemPowerDefRef = (ItemPowerDefRef*)eaGet(&pItemDef->ppItemPowerDefRefs,0);
		if (pItemPowerDefRef)
		{
			char tempStr[32] = "";
			sprintf(tempStr, "%i", (int)(1/pItemPowerDefRef->fChanceToApply));
			estrAppend2(pestrDesc, tempStr);
			estrAppend2(pestrDesc, " required to apply the following:<br>");
		}
	}
	*/
	//loop for all powers on the item
	for(iPower=0; iPower< iNumPowers; iPower++)
	{
		bool bActive = true;
		ItemPowerDef *pItemPower = item_trh_GetItemPowerDef((Item_AutoGen_NoConst*)pItem,iPower);
		ItemPowerDefRef* pItemPowerDefRef = item_trh_GetItemPowerDefRef((Item_AutoGen_NoConst*)pItem,iPower);
		bool bRollFailed = false;
		
		if (pItemPower && pItemPower->pRestriction && (pItemPower->pRestriction->eRequiredGemSlotType & eActiveGemSlotType) != eActiveGemSlotType)
			bActive = false;

		if(iNumPowers - iGemPowers <= iPower)
		{
			if (g_ItemConfig.bUseUniqueIDsForItemPowerDefRefs && pItem->pSpecialProps)
			{
				// This is a power from a gem
				// Find which gem slot we are in
				S32 iSlot = 0;
				S32 iPowerOffset = iNumPowers - iGemPowers;

				for (iSlot = 0; iSlot < eaSize(&pItem->pSpecialProps->ppItemGemSlots); iSlot++)
				{
					ItemDef *pGemDef = GET_REF(pItem->pSpecialProps->ppItemGemSlots[iSlot]->hSlottedItem);

					if(pGemDef)
					{
						S32 iNumGemPowers = eaSize(&pGemDef->ppItemPowerDefRefs);

						if (iNumGemPowers + iPowerOffset > iPower)
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
							iPowerOffset += iNumGemPowers;
						}
					}
				}
			}

			if(pItemPower->pRestriction && pItemPower->pRestriction->eRequiredGemSlotType)
			{
				ItemGemSlotDef *pGemSlotDef = GetGemSlotDefFromPowerIdx(pItem, iPower);

				if(pGemSlotDef && !(pItemPower->pRestriction->eRequiredGemSlotType & pGemSlotDef->eType))
					continue;

				if(item_GetGemPowerLevel(pItem))
					iLevel = item_GetGemPowerLevel(pItem);
			}
		}
		if(pItemPower && !bRollFailed)
		{
			PowerDef *pPowerDef = GET_REF(pItemPower->hPower);
			if (pPowerDef && (pPowerDef->eType == kPowerType_Click || pPowerDef->eType == kPowerType_Toggle || pPowerDef->eType == kPowerType_Instant))
			{
				if (RefSystem_ReferentFromString(gMessageDict, "Item.UI.UsePower"))
				{
					char * estrTemp;
					estrStackCreate(&estrTemp);
					FormatDisplayMessage(&estrTemp,pItemPower->descriptionMsg,STRFMT_INT("ItemPowerFactor",item_GetPowerFactor(pItem)), STRFMT_INT("Scale",pItemPowerDefRef ? pItemPowerDefRef->ScaleFactor : 1.0),STRFMT_END);

					FormatMessageKey(pestrDesc, "Item.UI.UsePower", STRFMT_STRING("POWER",estrTemp), STRFMT_END);
					estrDestroy(&estrTemp);
				}
				else
				{
					estrAppend2(pestrDesc, "Use: ");

					if (!inv_AppendItemDescMessage(pestrDesc,&pItemPower->descriptionMsg,pItem))
					{
						estrAppend2(pestrDesc, TranslateDisplayMessage(pPowerDef->msgDescription));
					}
					estrAppend2(pestrDesc, "<br>");
				}
			}
			else if (IS_HANDLE_ACTIVE(pItemPower->descriptionMsg.hMessage) &&
				(pItemDef->eType == kItemType_Upgrade || pItemDef->eType == kItemType_Weapon || pItemDef->eType == kItemType_Gem))
			{
				const char *pchMessageKey;
				const char *pchNoMessageString;
				F32 fGemSlottingChance = 100.f;

				if (pItemPowerDefRef && pItemPowerDefRef->fGemSlottingApplyChance < 1.f)
				{
					fGemSlottingChance = pItemPowerDefRef->fGemSlottingApplyChance * 100.f;
				}

				if (pItemDef->eType == kItemType_Gem)
				{
					pchMessageKey = "Item.UI.SlotPower";
					pchNoMessageString = "Slot: ";
				}
				else
				{
					pchMessageKey = "Item.UI.EquipPower";
					pchNoMessageString = "Equip: ";
				}

				if (RefSystem_ReferentFromString(gMessageDict, pchMessageKey))
				{
					char * estrTemp;
					char * estrTempGemTypeDisplayName;
					S32 eReqSlotType = pItemPower->pRestriction ? pItemPower->pRestriction->eRequiredGemSlotType : -1;
					const char* pchGemType = StaticDefineInt_FastIntToString(ItemGemTypeEnum, eReqSlotType);
					F32 fScaleFactor;
					estrStackCreate(&estrTemp);
					estrStackCreate(&estrTempGemTypeDisplayName);

					estrPrintf(&estrTempGemTypeDisplayName, "StaticDefine_ItemGemType_%s", pchGemType);
					
					// this should maybe get removed.  Not really how this feature is intended to work [RMARR - 9/7/12]
					fScaleFactor = item_GetItemPowerScale(pItem, iPower);
					if (gConf.bRoundItemStatsOnApplyToChar)
						fScaleFactor += 0.5f;

					FormatDisplayMessage(&estrTemp,
						pItemPower->descriptionMsg, 
						STRFMT_INT("ItemPowerFactor",item_GetPowerFactor(pItem)), 
						STRFMT_INT("Scale",pItemPowerDefRef ? fScaleFactor : 1.0),
						STRFMT_FLOAT("SlotChance", fGemSlottingChance),
						STRFMT_END);

					FormatMessageKey(pestrDesc, pchMessageKey, 
						STRFMT_STRING("POWER",estrTemp), 
						STRFMT_INT("IsActive", bActive), 
						STRFMT_STRING("GemSlotType", pchGemType),
						STRFMT_STRING("GemSlotTypeDisplayName", TranslateMessageKey(estrTempGemTypeDisplayName)),
						STRFMT_FLOAT("SlotChance", fGemSlottingChance),
						STRFMT_END);
					estrDestroy(&estrTemp);
					estrDestroy(&estrTempGemTypeDisplayName);
				}
				else
				{
					estrAppend2(pestrDesc, pchNoMessageString);
					inv_AppendItemDescMessage(pestrDesc,&pItemPower->descriptionMsg,pItem);
					estrAppend2(pestrDesc, "<br>");
				}
			}
			else if (IS_HANDLE_ACTIVE(pItemPower->descriptionMsg.hMessage) && pItemDef->eType == kItemType_TradeGood)
			{
				if (RefSystem_ReferentFromString(gMessageDict, "Item.UI.TradeGoodPower"))
				{
					char * estrTemp;
					estrStackCreate(&estrTemp);
					FormatDisplayMessage(&estrTemp,pItemPower->descriptionMsg,STRFMT_INT("ItemPowerFactor",item_GetPowerFactor(pItem)),STRFMT_END);

					FormatMessageKey(pestrDesc, "Item.UI.TradeGoodPower", STRFMT_STRING("POWER",estrTemp), STRFMT_STRING("SLOT",StaticDefineIntRevLookup(InvBagIDsEnum, pItemPowerDefRef->eAppliesToSlot)), STRFMT_END);
					estrDestroy(&estrTemp);
				}
			}

		}
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetItemInspect");
const char* GenExprGetItemInspect(ExprContext* context, SA_PARAM_OP_VALID Item *pItem, int iMaxPowers)
{
	char * InspectText = NULL;
	char * tmpS = NULL;
	char *result;
	int outlinetab[] = {1, 1, 1, 1, 1};
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	Entity* pEnt = entActiveOrSelectedPlayer();

	estrStackCreate(&InspectText);
	estrStackCreate(&tmpS);

	if ( pItem && pItemDef )
	{
		estrClear(&tmpS);
		estrPrintf(&tmpS, 
			"<font style=Game_HUD color=%s outline=%d>%s</font>",
			StaticDefineIntRevLookup(ItemQualityEnum,item_GetQuality(pItem)),
			outlinetab[item_GetQuality(pItem)], 
			item_GetName(pItem, pEnt));

		estrAppend2(&InspectText,tmpS);
		estrAppend2(&InspectText,"<br><br>");

		{
			Message *pMsg = NULL;

			{
				estrClear(&tmpS);
				estrPrintf(&tmpS, "<font style=Game_HUD color=white outline=0>- " );

				pMsg = GET_REF(pItemDef->descriptionMsg.hMessage);
				if (pMsg)
				{
					estrAppend2(&tmpS, TranslateMessagePtr(pMsg) );
					estrAppend2(&tmpS, " " );
				}

				if ((pItem->flags & kItemFlag_Algo) && pItem->pAlgoProps)
				{
					int ii;

					//description on algo item comes from 1st item power on the item
					for (ii=0; ii < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); ii++ )
					{
						ItemPowerDef *pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[ii]->hItemPowerDef);

						if (pItemPower)
						{
							pMsg = GET_REF(pItemPower->descriptionMsg.hMessage);
							if (pMsg)
							{
								estrAppend2(&tmpS, TranslateMessagePtr(pMsg) );
								estrAppend2(&tmpS, " " );
							}
						}
					}
				}

				estrAppend2(&tmpS, "\n" );

				estrAppend2(&InspectText,tmpS);
				estrAppend2(&InspectText,"</font>");
			}
		}

		estrClear(&tmpS);
		estrPrintf(&tmpS, "<font style=Game_HUD color=white outline=0>" );
		estrAppend2(&InspectText,tmpS);

		switch (pItemDef->eType)
		{
		case kItemType_Bag:
			if ( pItemDef->iNumBagSlots > 0 )
			{
				estrClear(&tmpS);
				FormatMessageKey(&tmpS, "Item.UI.BagSize", STRFMT_INT("size", pItemDef->iNumBagSlots),STRFMT_END);
				estrAppend2(&InspectText,tmpS);
				estrAppend2(&InspectText," ");
			}
			break;

		case kItemType_Upgrade:
		case kItemType_Device:
		case kItemType_Weapon:
		case kItemType_Component:
		case kItemType_ItemRecipe:
		case kItemType_ItemValue:
		case kItemType_ItemPowerRecipe:
		case kItemType_Mission:
		case kItemType_MissionGrant:
		case kItemType_Boost:
		case kItemType_Numeric:
		default:
			break;
		}

		switch (pItemDef->eType)
		{
		case kItemType_Upgrade:
		case kItemType_Device:
		case kItemType_Weapon:
		case kItemType_Bag:
			if ( item_GetMinLevel(pItem) > 0 )
			{
				estrClear(&tmpS);
				FormatMessageKey(&tmpS, "Item.UI.MinLevel", STRFMT_INT("minlevel", item_GetMinLevel(pItem)),STRFMT_END);
				estrAppend2(&InspectText,tmpS);
				estrAppend2(&InspectText," ");
			}
			break;

		case kItemType_Component:
		case kItemType_ItemRecipe:
		case kItemType_ItemValue:
		case kItemType_ItemPowerRecipe:
		case kItemType_Mission:
		case kItemType_MissionGrant:
		case kItemType_Boost:
		case kItemType_Numeric:
		case kItemType_STOBridgeOfficer:
		case kItemType_AlgoPet:
		case kItemType_ExperienceGift:
			{
				if(pItem && (pItem->flags & kItemFlag_Full) != 0)
				{
					FormatMessageKey(&tmpS, "Item.UI.ItemFull", STRFMT_END);
					estrAppend2(&InspectText,tmpS);
					estrAppend2(&InspectText," ");
				}
				break;
			}
		default:
			break;
		}

		if (pItemDef->kSkillType != kSkillType_None )
		{
			estrClear(&tmpS);
			switch (pItemDef->kSkillType)
			{
			case kSkillType_Arms:
				FormatMessageKey(&tmpS, "Item.UI.Arms", STRFMT_END);
				break;
			case kSkillType_Mysticism:
				FormatMessageKey(&tmpS, "Item.UI.Mysticism", STRFMT_END);
				break;
			case kSkillType_Science:
				FormatMessageKey(&tmpS, "Item.UI.Science", STRFMT_END);
				break;
			}
			estrAppend2(&InspectText,tmpS);
			estrAppend2(&InspectText," ");
		}

        if (pItemDef && (pItemDef->flags & kItemDefFlag_BindOnEquip) && !(pItem->flags & kItemFlag_Bound))
        {
            estrClear(&tmpS);
            FormatMessageKey(&tmpS, "Item.UI.BindOnEquip", STRFMT_END);
            estrAppend2(&InspectText,tmpS);
            estrAppend2(&InspectText," ");
        }

		if (pItem->flags & kItemFlag_Bound)
		{
			estrClear(&tmpS);
			FormatMessageKey(&tmpS, "Item.UI.Bound", STRFMT_END);
			estrAppend2(&InspectText,tmpS);
			estrAppend2(&InspectText," ");
		}
		else if(pItem->flags & kItemFlag_BoundToAccount)
		{
			estrClear(&tmpS);
			FormatMessageKey(&tmpS, "Item.UI.BoundToAccount", STRFMT_END);
			estrAppend2(&InspectText,tmpS);
			estrAppend2(&InspectText," ");
		}




		estrAppend2(&InspectText,"<br><br></font>");

		estrClear(&tmpS);
		estrPrintf(&tmpS, "<font style=Game_HUD color=white outline=0>" );
		Item_PowerAutoDescCustom(entActiveOrSelectedPlayer(), pItem, &tmpS, "", "", 0);
		estrAppend2(&InspectText,tmpS);
		estrAppend2(&InspectText,"<br></font>");
	}

	result = exprContextAllocScratchMemory(context, strlen(InspectText)+1 );

	memcpy(result,InspectText,strlen(InspectText)+1);

	estrDestroy(&InspectText);
	estrDestroy(&tmpS);

	return result;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMoveNumericToGuild);
void GenExprMoveNumericToGuild(SA_PARAM_OP_VALID Entity *pEnt, const char *pcNumeric, int iCount)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	if (!pGuild || !pcNumeric || !pcNumeric[0]) {
		return;
	}
	ServerCmd_ItemMoveNumericToGuild(InvBagIDs_Numeric, InvBagIDs_Numeric, pcNumeric, iCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMoveItem);
void GenExprMoveItem(SA_PARAM_NN_VALID UIGen *pGen, 
					 SA_PARAM_OP_VALID Entity *pEnt, 
					 bool bGrabDropAll,
					 bool bSrcGuild, const char* pcSrcBagName, int iSrcSlot,
					 bool bDstGuild, const char* pcDstBagName, int iDstSlot,
                     int iCount)
{
	if ( pEnt &&
		 pcSrcBagName && pcSrcBagName[0] &&
		 pcDstBagName && pcDstBagName[0] )
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		Entity *pGuildBank = guild_GetGuildBank(pEnt);
		int iSrcBagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pcSrcBagName);
		int iDstBagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pcDstBagName);
		GameAccountDataExtract *pExtract;
		
		if ((bSrcGuild || bDstGuild) && (!pGuild || !pGuildBank || !pGuildBank->pInventoryV2)) {
			return;
		}
		
		if ( iSrcBagIdx < 0 || iDstBagIdx < 0 )
			return;
		
		//if the item is coming from the loot bag, then use the loot server cmd to transfer it
		if ( iSrcBagIdx == InvBagIDs_Loot )
		{
			ServerCmd_loot_InteractTake( iSrcSlot, iDstBagIdx, iDstSlot );
			return;
		}
		
		pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		
		if (iDstBagIdx == InvBagIDs_None)
		{
			//an unspecified destination bag on the player means the item is being equipped
			//on the guild, it means go to the first available bank bag
			if (bDstGuild) {
				int i;
				// TODO: Add permission checking, so it skips bags you're not allowed to put
				// stuff into
				for (i = 0; i < eaSize(&pGuildBank->pInventoryV2->ppInventoryBags); i++) {
					InventoryBag *pBag = pGuildBank->pInventoryV2->ppInventoryBags[i];
					if ((invbag_flags(pBag) & InvBagFlag_GuildBankBag) != 0 && !inv_bag_BagFull(pEnt, pBag)) {
						iDstBagIdx = pBag->BagID;
						break;
					}
				}
				
				if (iDstBagIdx == InvBagIDs_None) {
					// We didn't find a guild bank bag with any empty slots
					return;
				}
			} 
			else 
			{
				if (bag_IsNoModifyInCombatBag(pEnt,iSrcBagIdx,pExtract) && character_HasMode(pEnt->pChar,kPowerMode_Combat))
				{
					return;
				}

				if (iSrcBagIdx != InvBagIDs_Inventory)
				{
					//equip used on any items not in inventory just moves it back into inventory
					iDstBagIdx = InvBagIDs_Inventory;
				}
				else
				{
					//equip on items in inventory has behavior based on item type
					Item *pItem = inv_GetItemFromBag( pEnt, iSrcBagIdx, iSrcSlot, pExtract);
					ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
					iDstBagIdx = pDef ? GetBestBagForItemDef(pEnt, pDef, iCount, false, pExtract) : InvBagIDs_None;
					
					//verify that item def is valid just to be safe
					if (!pDef) 
					{
						return;  
					}
					
					//special handling for recipe and mission items
					if ( item_IsRecipe(pDef) ||
						 item_IsMissionGrant(pDef) ||
						 item_IsSavedPet(pDef) || 
						 pDef->eType == kItemType_STOBridgeOfficer ||
						 item_isAlgoPet(pDef) ||
						 item_IsAttributeModify(pDef))
					{
						ServerCmd_ItemEquip(iSrcBagIdx, iSrcSlot);
						return;
					}
					
					//can't figure out what to do
					if (iDstBagIdx == InvBagIDs_None)
					{
						return;
					}
					
					//shortcut move from inventory to inventory does nothing
					if (iDstBagIdx == InvBagIDs_Inventory)
					{
						return;
					}
				}
			}
		}
		
		//if the user was in "drop all" mode, drop only as many as possible
		if ( bGrabDropAll )
		{
			int iSrcSlotCount;
			Item *pItem = NULL;
			InventoryBag *pSrcBag = bSrcGuild ? inv_guildbank_GetBag(pGuildBank, iSrcBagIdx) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iSrcBagIdx, pExtract);
			InventoryBag *pDstBag = bDstGuild ? inv_guildbank_GetBag(pGuildBank, iDstBagIdx) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDstBagIdx, pExtract);
			
			if ( iSrcBagIdx != InvBagIDs_Loot )
			{
				pItem = bSrcGuild ? inv_guildbank_GetItemFromBag( pEnt, iSrcBagIdx, iSrcSlot) : inv_GetItemFromBag( pEnt, iSrcBagIdx, iSrcSlot, pExtract);
			}
			
			if ( !pItem || !pDstBag || !pSrcBag ) 
			{
				return;
			}
			
			if ( invbag_flags(pDstBag) & InvBagFlag_PlayerBagIndex )
			{
				InventorySlot* pSlot = inv_ent_GetSlotPtr( pEnt, iDstBagIdx, iDstSlot, pExtract );
				
				if  ( !pSlot ||
					  !(pSlot->pItem) )
				{
					//if the slot is empty then only bag items can drop into it
					//bag items can't stack in index bags so the count is always 1
					iCount = 1;
				}
				else
				{
					// the slot has a bag in it, attempting to drop item into that bag
					//get the max count of the player bag
					InventoryBag* pPlayerBag = inv_PlayerBagFromSlotIdx(pEnt, iDstSlot);
					iCount = inv_GetMaxDropCount(pEnt, pPlayerBag, pItem);
				}									
			}
			else
			{
				ItemDef* pItemDef = GET_REF( pItem->hItem );

				iCount = ( pItemDef ) ? pItemDef->iStackLimit : 0;
			}
			
			iSrcSlotCount = inv_bag_GetSlotItemCount( pSrcBag, iSrcSlot );
			
			if ( iCount > iSrcSlotCount ) 
				iCount = iSrcSlotCount;
		}

		//everything else just uses standard item move
		ServerCmd_ItemMove(bSrcGuild, iSrcBagIdx, iSrcSlot, bDstGuild, iDstBagIdx, iDstSlot, iCount);
	}
}

//MULTIPLE ENTITY VERSION
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoveItem);
void GenExprRemoveItem(SA_PARAM_NN_VALID UIGen *pGen, 
					   SA_PARAM_OP_VALID Entity *pEnt, 
					   const char* pSrcBagName, int SrcSlot, int iCount )
{
	if ( pEnt &&
		pSrcBagName &&	pSrcBagName[0] )
	{
		int SrcBagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pSrcBagName);

		if ( SrcBagIdx <= 0 )
			return;

		ServerCmd_item_RemoveFromBag(SrcBagIdx, SrcSlot, iCount, "Reward.YouLostItem");
	}
}

//remove an item from any entity that belongs to the player entity (i.e. a pet)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoveItemFromEnt);
void GenExprRemoveItemFromEnt(	SA_PARAM_NN_VALID UIGen *pGen, 
								SA_PARAM_OP_VALID Entity *pEnt, 
								const char* pSrcBagName, int SrcSlot, int iCount )
{
	if ( pEnt==NULL )
		return;
	
	if ( pEnt == entActiveOrSelectedPlayer() )
	{
		GenExprRemoveItem( pGen, pEnt, pSrcBagName, SrcSlot, iCount );
		return;
	}
	
	if ( pSrcBagName &&	pSrcBagName[0] )
	{
		int SrcBagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pSrcBagName);

		if ( SrcBagIdx <= 0 )
			return;

		ServerCmd_item_RemoveFromBagForEnt(entGetType(pEnt), entGetContainerID(pEnt), SrcBagIdx, SrcSlot, iCount);
	}
}

// Get an SMF version of the item name, colored according to the item's quality
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemColoredName);
const char* GenExprGetItemColoredName(ExprContext* pContext, SA_PARAM_OP_VALID Item* pItem)
{
	char* TooltipText = NULL;
	char* result;
	int outlinetab[] = {1, 1, 1, 1, 1};
	Entity* pEnt = entActiveOrSelectedPlayer();

	estrStackCreate(&TooltipText);

	if (pItem) {
		estrPrintf(&TooltipText, 
			"<font style=Game_HUD color=%s outline=%d>%s</font>", 
			StaticDefineIntRevLookup(ItemQualityEnum,item_GetQuality(pItem)),
			outlinetab[item_GetQuality(pItem)], 
			item_GetName(pItem, pEnt));
	}

	result = exprContextAllocScratchMemory(pContext, strlen(TooltipText)+1 );

	memcpy(result,TooltipText,strlen(TooltipText)+1);
	estrDestroy(&TooltipText);

	return result;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetFormattedItemInfo");
const char* GenExprGetFormattedItemInfo(ExprContext* pContext, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEnt, ACMD_EXPR_DICT(Message) const char *pchDescriptionKey, S32 eActiveGemSlotType)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	Character *pChar = pEnt ? pEnt->pChar : NULL;
	char *estrResult = NULL;
	char *result = NULL;
	
	//Setup a static handle to the resources item
	static REF_TO(ItemDef) s_hResources;
	if(!IS_HANDLE_ACTIVE(s_hResources))
	{
		SET_HANDLE_FROM_STRING(g_hItemDict, "resources", s_hResources);
	}
	

	if ( pItem && pItemDef && pChar )
	{
		char pchTempFmt[256] = {0};
		const char *pchItemQuality = NULL;
		const char *pchItemName = NULL;
		const char *pchItemUsable = TranslateMessageKey("Item.UI.Usable");
		const char *pchItemRecipeLearned = NULL;
		const char *pchItemType = NULL;
		const char *pchItemComponentType = NULL;
		const char *pchItemQuest = NULL;
		const char *pchItemBind = NULL;
		const char *pchItemDesc = NULL;
		const char *pchItemDescShort = NULL;
		const char *pchItemPrompt = NULL;
		const char *pchItemRequiresExprPower = NULL;
		bool bEntUsableLevels = false;
		bool bEntUsableSkill = false;
		bool bEntUsableExpr = true;
		bool bEntLearnedRecipe = false;
		char *pchItemSkillColor = "White";
		char *pchItemTypeColor = "White";
		char *estrItemEquipLevel = NULL;
		char *estrItemSkill = NULL;
		char *estrComponentSkill = NULL;
		char *estrItemDurability = NULL;
		char *estrItemBagSize = NULL;
		char *estrItemAlgoDesc = NULL;
		char *estrItemInnatePowerAutoDesc = NULL;
		char *estrItemPowerAutoDesc = NULL;
		char *estrItemValue = NULL;
		char *estrSavedPetPowers = NULL;
		char *estrItemUsagePrompt = NULL;
		char *estrItemPowers = NULL;
		char *estrItemFlavorDesc = NULL;
		char *estrItemBridgeOfficer = NULL ;
		char *estrItemCategories = NULL;
		char *estrItemWeaponDamage = NULL;
		F32 fItemDurabilityCurrent = 0.0f;
		F32 fItemDurabilityMax = 0.0f;
		int iItemBagSlots = 0;
		int iEntLevel = pChar->iLevelCombat;
		int iEntSkillLevel = entity_GetSkillLevel(pEnt);
		ItemSortType *pSortType;
		UsageRestriction *pRestrict = pItemDef->pRestriction;
		SkillType eEntSkill = entity_GetSkill(pEnt);
		ItemDef *pResourceDef = GET_REF(s_hResources);
		
		if (!pchItemUsable)
		{
			pchItemUsable = "[UNTRANSLATED: Item.UI.Usable]";
		}

		// Extract variables
		pchItemQuality = StaticDefineIntRevLookup(ItemQualityEnum, pItem ? item_GetQuality(pItem) : pItemDef->Quality);
		if (item_IsUnidentified(pItem))
		{
			pchItemName = TranslateDisplayMessage(pItemDef->displayNameMsgUnidentified);
			pchItemDesc = item_GetTranslatedDescription(pItem, langGetCurrent());
			if (pchItemDesc)
				estrPrintf(&estrItemFlavorDesc, "\"%s\"<br>", pchItemDesc);
		}
		else
		{
			pchItemName = pItem ? item_GetName(pItem, pEnt) : TranslateDisplayMessage(pItemDef->displayNameMsg);
			pchItemDesc = item_GetTranslatedDescription(pItem, langGetCurrent());
			pchItemDescShort = TranslateDisplayMessage(pItemDef->descShortMsg);
			if (pchItemDesc)
				estrPrintf(&estrItemFlavorDesc, "\"%s\"<br>", pchItemDesc);
			pchItemPrompt = item_GetItemPowerUsagePrompt(langGetCurrent(), pItem);
			if (pchItemPrompt)
			{
				estrAppend2(&estrItemUsagePrompt, "Use: ");
				estrAppend2(&estrItemUsagePrompt, pchItemPrompt);
			}
		}

		if (pRestrict || (pItem && (pItem->flags & kItemFlag_Algo)))
		{
			MultiVal mvResult = {0};

			if (!pItem || !(pItem->flags & kItemFlag_Algo))
			{
				S32 iMinLevel = pRestrict->iMinLevel;
				// take the higher of the created from source or prestrict min level
				if(pItem && pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought) && item_GetMinLevel(pItem) > iMinLevel)
				{
					iMinLevel = item_GetMinLevel(pItem);
				}
				if (pRestrict->iMaxLevel > iMinLevel)
				{
					bEntUsableLevels = iMinLevel <= iEntLevel && (pRestrict->iMaxLevel <= iMinLevel || iEntLevel <= pRestrict->iMaxLevel);
					estrStackCreate(&estrItemEquipLevel);
					FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.MinMax", STRFMT_INT("MinLevel", MAX(1, iMinLevel)), STRFMT_INT("MaxLevel", pRestrict->iMaxLevel), STRFMT_END);
				}
				else if (iMinLevel > 0)
				{
					bEntUsableLevels = iMinLevel <= iEntLevel && (pRestrict->iMaxLevel <= iMinLevel || iEntLevel <= pRestrict->iMaxLevel);
					estrStackCreate(&estrItemEquipLevel);
					FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.Min", STRFMT_INT("MinLevel", iMinLevel), STRFMT_END);
				}
			}
			else if (pItem && (pItem->flags & kItemFlag_Algo))
			{
				bEntUsableLevels = item_GetMinLevel(pItem) <= iEntLevel;
				if (item_GetMinLevel(pItem) > 0)
				{
					estrStackCreate(&estrItemEquipLevel);
					FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.Min", STRFMT_INT("MinLevel", item_GetMinLevel(pItem)), STRFMT_END);
				}
			}

			if (pRestrict && pRestrict->eSkillType != kSkillType_None)
			{
				bEntUsableSkill = pRestrict->eSkillType == eEntSkill && inv_GetNumericItemValue(pEnt, "SkillLevel") >= (S32)pRestrict->iSkillLevel;
				estrStackCreate(&estrItemSkill);
				sprintf(pchTempFmt, "Item.UI.%s", StaticDefineIntRevLookup(SkillTypeEnum, pRestrict->eSkillType));
				FormatMessageKey(&estrItemSkill, (pRestrict->iSkillLevel > 1 ? "Item.UI.SkillLevel" : "Item.UI.Skill"), STRFMT_STRING("SkillName", TranslateMessageKey(pchTempFmt)), STRFMT_INT("SkillLevel", pRestrict->iSkillLevel),STRFMT_END);
			}

			if (pRestrict)
			{
				bEntUsableExpr = (pRestrict->pRequires == NULL);
				if (pItemDef->pRestriction->pRequires)
				{
					int ii,s=beaSize(&pItemDef->pRestriction->pRequires->postfixEArray);
					static MultiVal **s_ppStack = NULL;

					if (!bEntUsableExpr)
					{
						itemeval_Eval(PARTITION_CLIENT, pItemDef->pRestriction->pRequires, pItemDef, NULL, pItem, pEnt, item_GetLevel(pItem), item_GetQuality(pItem), 0, pItemDef->pchFileName, -1, &mvResult);
						bEntUsableExpr = itemeval_GetIntResult(&mvResult,pItemDef->pchFileName,pItemDef->pRestriction->pRequires);
					}

					//parse required power name from requires expression
					for(ii=0; ii<s; ii++)
					{
						MultiVal *pVal = pItemDef->pRestriction->pRequires->postfixEArray + ii;
						if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
							continue;
						if(pVal->type==MULTIOP_FUNCTIONCALL)
						{
							const char *pchFunction = pVal->str;
							if(!strncmp(pchFunction,"Entownspower", 12))
							{
								MultiVal* pPowNameVal = eaPop(&s_ppStack);
								PowerDef* pDef = RefSystem_ReferentFromString(g_hPowerDefDict, MultiValGetString(pPowNameVal, NULL));
								pchItemRequiresExprPower = pDef ? TranslateDisplayMessage(pDef->msgDisplayName) : "INVALID_POWER";
							}
						}
						eaPush(&s_ppStack,pVal);
					}
				}
			}
		}
		else if (pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought) && item_GetMinLevel(pItem))
		{
			bEntUsableLevels = item_GetMinLevel(pItem) <= iEntLevel;
			estrStackCreate(&estrItemEquipLevel);
			FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.Min", STRFMT_INT("MinLevel", item_GetMinLevel(pItem)), STRFMT_END);
		}
		if (pItemDef->eType == kItemType_Component || (pItemDef->kSkillType != kSkillType_None && !estrItemSkill))
		{
			sprintf(pchTempFmt, "Item.UI.%s", StaticDefineIntRevLookup(SkillTypeEnum, pItemDef->kSkillType));
			FormatMessageKey(&estrComponentSkill, "Item.UI.Skill", STRFMT_STRING("SkillName", TranslateMessageKey(pchTempFmt)), STRFMT_END);
			switch (pItemDef->kSkillType) {
				case kSkillType_Arms:
					pchItemTypeColor = "red";
					break;
				case kSkillType_Science:
					pchItemTypeColor = "green";
					break;
				case kSkillType_Mysticism:
					pchItemTypeColor = "purple";
					break;
			}
		}
		pSortType = item_GetSortTypeForID(pItemDef->iSortID);
		if (pSortType)
		{
			pchItemType = TranslateMessageRef(pSortType->hNameMsg);
		}
		if (pItemDef->eType == kItemType_Component)
		{
			pchItemComponentType = pchItemType;
			pchItemType = NULL;
		}
		if (pItem && (pItem->flags & kItemFlag_Bound))
		{
			pchItemBind = TranslateMessageKey("Item.UI.Bound");
		}
		else if (pItem && (pItem->flags & kItemFlag_BoundToAccount))
		{
			pchItemBind = TranslateMessageKey("Item.UI.BoundToAccount");
		}
		else if ((pItemDef->flags & kItemDefFlag_BindOnPickup) || (pItem && pItem->bForceBind))
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindOnPickup");
		}
		else if (pItemDef->flags & kItemDefFlag_BindOnEquip)
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindOnEquip");
		}
		else if ((pItemDef->flags & kItemDefFlag_BindToAccountOnPickup) || (pItem && pItem->bForceBind))
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindToAccountOnPickup");
		}
		else if (pItemDef->flags & kItemDefFlag_BindToAccountOnEquip)
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindToAccountOnEquip");
		}
		if (pItemDef->eType == kItemType_Bag)
		{
			FormatMessageKey(&estrItemBagSize, "Item.UI.BagSize", STRFMT_INT("size", pItemDef->iNumBagSlots),STRFMT_END);
			iItemBagSlots = pItemDef->iNumBagSlots;
		}

		if (pItem && (pItem->flags & kItemFlag_Algo) && pItem->pAlgoProps && eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) > 0)
		{
			int ii;
			estrStackCreate(&estrItemAlgoDesc);
			for (ii=0; ii < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); ii++ )
			{
				ItemPowerDef *pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[ii]->hItemPowerDef);
				if (pItemPower)
				{
					if (inv_AppendItemDescMessage(&estrItemAlgoDesc,&pItemPower->descriptionMsg,pItem))
					{
						estrAppend2(&estrItemAlgoDesc, " ");
					}
				}
			}
		}
		if (pItem)
		{
			estrStackCreate(&estrItemInnatePowerAutoDesc);
			estrStackCreate(&estrItemPowerAutoDesc);
			estrStackCreate(&estrItemPowers);
			Item_InnatePowerAutoDesc(entActiveOrSelectedPlayer(), pItem, &estrItemInnatePowerAutoDesc, eActiveGemSlotType);
			Item_PowerAutoDescCustom(entActiveOrSelectedPlayer(), pItem, &estrItemPowerAutoDesc, "", "", 0);
			Item_ItemPowersAutoDesc(pItem, &estrItemPowers, "", "", eActiveGemSlotType);
		}

		if (item_IsRecipe(pItemDef))
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			bEntLearnedRecipe = inv_ent_CountItems(pEnt, InvBagIDs_Recipe, pItemDef->pchName, pExtract) > 0;
			if (bEntLearnedRecipe)
			{
				pchItemRecipeLearned = TranslateMessageKey("Item.UI.Recipe.Learned");
				if (!pchItemRecipeLearned)
				{
					pchItemRecipeLearned = "[UNTRANSLATED: Item.UI.Recipe.Learned]";
				}
			}
		}
		if (pItemDef->pItemWeaponDef)
		{
			estrStackCreate(&estrItemWeaponDamage);
			FormatGameMessageKey(&estrItemWeaponDamage, "Item.UI.WeaponDamage",
				STRFMT_INT("ItemWeaponDamageDieSize", pItemDef->pItemWeaponDef->iDieSize),
				STRFMT_INT("ItemWeaponDamageNumDice", pItemDef->pItemWeaponDef->iNumDice),
				STRFMT_END);

			if (pItemDef->pItemWeaponDef->pAdditionalDamageExpr)
			{
				char * estrAdditionalDamage=NULL;
				int iAdditionalDamage = combateval_EvalNew(PARTITION_CLIENT,pItemDef->pItemWeaponDef->pAdditionalDamageExpr,kCombatEvalContext_Simple,NULL);
				estrStackCreate(&estrAdditionalDamage);
				FormatGameMessageKey(&estrAdditionalDamage, "Item.UI.WeaponDamageAdditional",
					STRFMT_INT("ItemWeaponDamageAdditional", iAdditionalDamage),
					STRFMT_END);

				if (estrAdditionalDamage)
				{
					estrAppend(&estrItemWeaponDamage,&estrAdditionalDamage);
				}
				estrDestroy(&estrAdditionalDamage);
			}
			estrConcatf(&estrItemWeaponDamage,"%s","<br>");
		}

		if (item_IsMissionGrant(pItemDef))
		{
			pchItemQuest = TranslateMessageKey("Item.UI.QuestStart");
			if (!pchItemQuest)
			{
				pchItemQuest = "[UNTRANSLATED: Item.UI.QuestStart]";
			}
		}
		else if (item_IsMission(pItemDef))
		{
			pchItemQuest = TranslateMessageKey("Item.UI.QuestItem");
			if (!pchItemQuest)
			{
				pchItemQuest = "[UNTRANSLATED: Item.UI.QuestItem]";
			}
		}
		else if(item_IsExperienceGift(pItemDef) && pItem && (pItem->flags & kItemFlag_Full) != 0)
		{
			pchItemQuest = TranslateMessageKey("Item.UI.ItemFull");
			if (!pchItemQuest)
			{
				pchItemQuest = "[UNTRANSLATED: Item.UI.ItemFull]";
			}
		}

		if (pchItemDesc && !stricmp(pchItemDesc, "."))
		{
			pchItemDesc = NULL;
		}

		if (pResourceDef) {
			S32 iResourceValue = item_GetDefEPValue(PARTITION_CLIENT, pEnt, pResourceDef, pResourceDef->iLevel, pResourceDef->Quality);
			S32 iValue = item_GetStoreEPValue(PARTITION_CLIENT, pEnt, pItem, NULL);
			if (iResourceValue) {
				iValue /= iResourceValue;
			}
			estrStackCreate(&estrItemValue);
			estrPrintf(&estrItemValue, "%d %s", iValue, item_GetDefLocalName(pResourceDef, locGetLanguage(getCurrentLocale())));
		}

		// Get the item categories text
		if (eaiSize(&pItemDef->peCategories) > 0)
		{
			Item_GetItemCategoriesString(pItemDef, NULL, &estrItemCategories);
		}

		FormatGameMessageKey(&estrResult, pchDescriptionKey,
			STRFMT_ITEM(pItem),
			STRFMT_ITEMDEF(pItemDef),
			STRFMT_STRING("ItemQuality", NULL_TO_EMPTY(pchItemQuality)),
			STRFMT_STRING("ItemName", NULL_TO_EMPTY(pchItemName)),
			STRFMT_INT("IsUsableLevels", bEntUsableLevels),
			STRFMT_STRING("ItemEquipLevels", NULL_TO_EMPTY(estrItemEquipLevel)),
			STRFMT_INT("IsUsableSkill", bEntUsableSkill),
			STRFMT_STRING("ItemSkill", NULL_TO_EMPTY(estrItemSkill)),
			STRFMT_STRING("ComponentSkill", NULL_TO_EMPTY(estrComponentSkill)),
			STRFMT_STRING("ItemTypeColor", pchItemTypeColor),
			STRFMT_INT("IsUsableExpr", bEntUsableExpr),
			STRFMT_STRING("ItemUsable", !bEntUsableExpr ? NULL_TO_EMPTY(pchItemUsable) : ""),
			STRFMT_STRING("ItemRequiresPowerName", NULL_TO_EMPTY(pchItemRequiresExprPower)),
			STRFMT_INT("IsLearnedRecipe", bEntLearnedRecipe),
			STRFMT_STRING("ItemRecipeLearned", NULL_TO_EMPTY(pchItemRecipeLearned)),
			STRFMT_STRING("ItemType", NULL_TO_EMPTY(pchItemType)),
			STRFMT_STRING("ItemComponentType", NULL_TO_EMPTY(pchItemComponentType)),
			STRFMT_STRING("ItemDurability", NULL_TO_EMPTY(estrItemDurability)),
			STRFMT_FLOAT("ItemDurabilityCurrent", fItemDurabilityCurrent),
			STRFMT_FLOAT("ItemDurabilityMax", fItemDurabilityMax),
			STRFMT_STRING("ItemQuest", NULL_TO_EMPTY(pchItemQuest)),
			STRFMT_STRING("ItemBind", NULL_TO_EMPTY(pchItemBind)),
			STRFMT_STRING("ItemBagSize", NULL_TO_EMPTY(estrItemBagSize)),
			STRFMT_INT("ItemBagSlots", iItemBagSlots),
			STRFMT_STRING("ItemValue", NULL_TO_EMPTY(estrItemValue)),
			STRFMT_STRING("ItemSkillColor", NULL_TO_EMPTY(pchItemSkillColor)),
			STRFMT_STRING("ItemDesc", NULL_TO_EMPTY(pchItemDesc)),
			STRFMT_STRING("ItemDescShort", NULL_TO_EMPTY(pchItemDescShort)),
			STRFMT_STRING("ItemAlgoDesc", NULL_TO_EMPTY(estrItemAlgoDesc)),
			STRFMT_STRING("ItemPowerAutoDesc", NULL_TO_EMPTY(estrItemPowerAutoDesc)),
			STRFMT_STRING("ItemInnatePowerAutoDesc", NULL_TO_EMPTY(estrItemInnatePowerAutoDesc)),
			STRFMT_STRING("ItemPowers", NULL_TO_EMPTY(estrItemPowers)),
			STRFMT_STRING("ItemSavedPetPowers", NULL_TO_EMPTY(estrSavedPetPowers)),
			STRFMT_STRING("ItemUsagePrompt", NULL_TO_EMPTY(estrItemUsagePrompt)),
			STRFMT_STRING("ItemFlavorDesc", NULL_TO_EMPTY(estrItemFlavorDesc)),
			STRFMT_STRING("ItemCategories", NULL_TO_EMPTY(estrItemCategories)),
			STRFMT_STRING("ItemWeaponDamage", NULL_TO_EMPTY(estrItemWeaponDamage)),
			STRFMT_STRING("W", NULL_TO_EMPTY(estrItemWeaponDamage)),
			STRFMT_INT("ItemPowerFactor", item_GetPowerFactor(pItem)),
			STRFMT_END
			);

		estrDestroy(&estrItemEquipLevel);
		estrDestroy(&estrItemSkill);
		estrDestroy(&estrComponentSkill);
		estrDestroy(&estrItemDurability);
		estrDestroy(&estrItemBagSize);
		estrDestroy(&estrItemAlgoDesc);
		estrDestroy(&estrItemInnatePowerAutoDesc);
		estrDestroy(&estrItemPowerAutoDesc);
		estrDestroy(&estrItemValue);
		estrDestroy(&estrSavedPetPowers);
		estrDestroy(&estrItemUsagePrompt);
		estrDestroy(&estrItemFlavorDesc);
		estrDestroy(&estrItemBridgeOfficer);
		estrDestroy(&estrItemPowers);
		estrDestroy(&estrItemCategories);
		estrDestroy(&estrItemWeaponDamage);
	}

#if GAMECLIENT
	if(g_bDisplayItemDebugInfo && pItem && pItemDef)
	{
		ItemPowerDef *item_power;

		item_PrintDebugText(&estrResult,pEnt,pItem,pItemDef);
		if (pItem->flags & kItemFlag_Algo)
		{	
			int i;
			if (pItem->pAlgoProps)
			{
				for (i = 0; i < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); i++)
				{
					item_power = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef);
					if (item_power)
					{
						estrConcatf(&estrResult,"\npower: %s", item_power->pchName);
					}
				}
			}
		}
	}
#endif

	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerHasItem");
ExprFuncReturnVal GenExprPlayerHasItem(ExprContext* pContext, ACMD_EXPR_INT_OUT intOut, const char* pchItemName, ACMD_EXPR_ERRSTRING errString)
{
	*intOut = 0;

	if(inv_ent_AllBagsCountItems(entActiveOrSelectedPlayer(), pchItemName)>0)
		*intOut = 1;

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBuyGuildBankTab);
void GenExprBuyGuildBankTab(void)
{
	ServerCmd_item_BuyBankTab();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenPlayerBuyGuildBankTab);
void GenExprPlayerBuyGuildBankTab(void)
{
    ServerCmd_item_PlayerBuyGuildBankTab();
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenBagEmpty);
bool GenExprBagEmpty(SA_PARAM_OP_VALID Entity *pEnt, const char* pBagName)
{
	bool bResult = true;

	if ( pEnt &&
		pBagName &&
		pBagName[0] )
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int BagId = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);

		bResult = inv_ent_BagEmpty(pEnt, BagId, pExtract);
	}

	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemType);
const char *GenExprGetItemType(SA_PARAM_OP_VALID Entity *pEnt, const char *pBagName, int SrcSlot)
{
 	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item *pItem = NULL;
    ItemDef *pItemDef = NULL;

    int SrcBagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
    pItem = inv_GetItemFromBag( pEnt, SrcBagIdx, SrcSlot, pExtract);

    if (!pItem)
        return "(null)";

    pItemDef = GET_REF(pItem->hItem);

    if (!pItemDef)
        return "(null)";

    return StaticDefineIntRevLookup(ItemTypeEnum, pItemDef->eType);
}

__forceinline static void InventoryExecBagSlot(S32 bStart, Entity *e, S32 iBag, S32 iSlot, GameAccountDataExtract *pExtract)
{
	Item *pitem = inv_GetItemFromBag(e,iBag,iSlot, pExtract);
	if(pitem)
	{
		int i,s=item_GetNumItemPowerDefs(pitem, true);
		for(i=0; i<s; i++)
		{
			Power *ppow = item_GetPower(pitem,i);
			if(ppow && character_ActivatePowerByIDClient(entGetPartitionIdx(e), e->pChar, ppow->uiID, NULL, NULL, bStart, pExtract))
			{
				break;
			}
		}
	}
}

// Executes the first power on the item in the bag at the slot
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void InventoryExec(S32 bStart, const char *pchBag, S32 iSlot)
{
	Entity *e = entActiveOrSelectedPlayer();
	if(e && e->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		S32 iBag = StaticDefineIntGetInt(InvBagIDsEnum,pchBag);
		InventoryExecBagSlot(bStart, e, iBag, iSlot, pExtract);
	}
}

// Executes the first power on the item in the bag at the slot
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyExecItem);
void gclInvExprInventoryKeyExecItem(S32 bStart, const char *pchKey)
{
	Entity *e = entActiveOrSelectedPlayer();
	if(e && e->pChar && pchKey)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		UIInventoryKey Key = {0};
		if (gclInventoryParseKey(pchKey, &Key))
			InventoryExecBagSlot(bStart, e, Key.eBag, Key.iSlot, pExtract);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsBagCostumeHideable);
int GenIsBagCostumeHideable(SA_PARAM_OP_VALID Entity *pEnt, int BagIdx)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagIdx, pExtract);
	if (!pBag) 
	{
		return 0;
	}
	return (invbag_flags(pBag) & InvBagFlag_CostumeHideable);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsBagCostumeHidden);
int GenIsBagCostumeHidden(SA_PARAM_OP_VALID Entity *pEnt, int BagIdx)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagIdx, pExtract);
	if (!pBag) 
	{
		return 0;
	}
	return pBag->bHideCostumes;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagIsCostumeHideablePerSlot);
int exprBagIsCostumeHideablePerSlot(SA_PARAM_OP_VALID Entity *pEnt, int iBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iBagID, pExtract);
	return (invbag_flags(pBag) & InvBagFlag_CostumeHideablePerSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InvSlotIsCostumeHidden);
int exprInvSlotIsCostumeHidden(SA_PARAM_OP_VALID Entity *pEnt, int iBagID, int iSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iBagID, pExtract);
	if (pBag)
	{
		if (pBag->bHideCostumes)
		{
			return true;
		}
		else
		{
			InventorySlot* pSlot = inv_GetSlotFromBag(pBag, iSlot);
			return pSlot && pSlot->bHideCostumes;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagItemHasCostumeOverlay);
int exprBagItemHasCostumeOverlay(SA_PARAM_OP_VALID Entity *pEnt, int iBagID, int iSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iBagID, pExtract);
	if (pBag)
	{
		InventorySlot* pSlot = inv_GetSlotFromBag(pBag, iSlot);
		if (pSlot)
		{
			ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
			if (pItemDef && eaSize(&pItemDef->ppCostumes))
			{
				return pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetIDFromBageName);
int GenExprGetIDFromBagName(const char *pBagName);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetIDFromBagName);
int GenExprGetIDFromBagName(const char *pBagName)
{
	return StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCheckItemRestrictions);
bool GenExprCheckItemRestrictions(SA_PARAM_OP_VALID Item *pItem, InvBagIDs uiBagID, SlotType uiSlotType)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItemDef)
	{
		ItemSortType *pSortType = item_GetSortTypeForID(pItemDef->iSortID);
		if (pSortType)
		{
			return pSortType->eRestrictBagID == uiBagID && pSortType->eRestrictSlotType == uiSlotType;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCheckBagRestrictions);
bool GenExprCheckBagRestrictions(SA_PARAM_OP_VALID Item *pItem, U32 uiBagID)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItemDef)
	{
		return eaiFind(&pItemDef->peRestrictBagIDs, uiBagID) >= 0;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetStringsForItemCompare);
void GenExprSetStringsForItemCompare(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEntity, const char *pchVar1, const char *pchVar2)
{
	S32 bVar1 = false, bVar2 = false;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if (pItemDef && pItemDef->pCraft && (pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemValue))
	{
		pItemDef = GET_REF(pItemDef->pCraft->hItemResult);
	}

	if(pItemDef && pEntity)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		InvBagIDs eBag = eaiGet(&pItemDef->peRestrictBagIDs, 0);
		InvBagIDs eAltBag = eaiGet(&pItemDef->peRestrictBagIDs, 1);

		InvBagIDs eCmpBag1 = InvBagIDs_None;
		InvBagIDs eCmpBag2 = InvBagIDs_None;
		S32 iSlot1 = -1;
		S32 iSlot2 = -1;

		InventoryBag *pBag = NULL;
		InventoryBag *pAltBag = NULL;

		Entity *pPlayerEnt = entActiveOrSelectedPlayer();
		bool bOwnedEnt = false;
		S32 i;

		if (pPlayerEnt && pPlayerEnt->pSaved && eaSize(&pPlayerEnt->pSaved->ppOwnedContainers) >= 0)
		{
			for (i = eaSize(&pPlayerEnt->pSaved->ppOwnedContainers) - 1; i >= 0; i--)
			{
				if (GET_REF(pPlayerEnt->pSaved->ppOwnedContainers[i]->hPetRef) == pEntity)
				{
					bOwnedEnt = true;
					break;
				}
			}
		}

		if (eBag)
			pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntity),eBag,pExtract);
		if (eAltBag)
			pAltBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntity),eAltBag,pExtract);

		if (pBag && (invbag_flags(pBag) & (InvBagFlag_ActiveWeaponBag | InvBagFlag_WeaponBag | InvBagFlag_EquipBag)) == 0)
			pBag = NULL;
		if (pAltBag && (invbag_flags(pAltBag) & (InvBagFlag_ActiveWeaponBag | InvBagFlag_WeaponBag | InvBagFlag_EquipBag)) == 0)
			pAltBag = NULL;

		if (!pBag && pAltBag)
		{
			pBag = pAltBag;
			pAltBag = NULL;
		}

#define SET_ITEM_FROM_SLOT(index, bag, slot)				\
		{													\
			if (inv_bag_GetItem((bag), (slot)) != pItem)	\
			{												\
				eCmpBag##index = (bag)->BagID;				\
				iSlot##index = (slot);						\
			}												\
		}

		if (pBag)
		{
			if (pItemDef->eRestrictSlotType == kSlotType_Secondary)
			{
				// Show the second slot
				SET_ITEM_FROM_SLOT(1, pBag, 1);
			}
			else
			{
				// Show the first slot
				SET_ITEM_FROM_SLOT(1, pBag, 0);
			}
		}

		if (pAltBag)
		{
			if (pItemDef->eRestrictSlotType == kSlotType_Secondary)
			{
				// Show the second slot of the second bag
				SET_ITEM_FROM_SLOT(2, pAltBag, 1);
			}
			else
			{
				// Show the first slot of the second bag
				SET_ITEM_FROM_SLOT(2, pAltBag, 0);
			}
		}
		else if (pBag)
		{
			if (pItemDef->eRestrictSlotType == kSlotType_Secondary)
			{
				// Show the third slot
				SET_ITEM_FROM_SLOT(2, pBag, 2);
			}
			else if (pItemDef->eRestrictSlotType == kSlotType_Any)
			{
				// Show the second slot
				SET_ITEM_FROM_SLOT(2, pBag, 1);
			}
			// else if (pItemDef->eRestrictSlotType == kUpgradeSlotType_Primary)
			// should only be displayed once
		}

#undef SET_ITEM_FROM_SLOT

		if (pchVar1 && *pchVar1 && eCmpBag1 != -1)
		{
			UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar1);
			if(pGlob)
			{
				UIInventoryKey Key = {0};
				Key.erOwner = bOwnedEnt ? entGetRef(pPlayerEnt) : entGetRef(pEntity);
				Key.eBag = eCmpBag1;
				Key.iSlot = iSlot1;
				if (bOwnedEnt)
				{
					Key.eType = entGetType(pEntity);
					Key.iContainerID = entGetContainerID(pEntity);
				}
				estrPrintf(&pGlob->pchString, "%s", gclInventoryMakeKeyString(NULL, &Key));
				bVar1 = true;
			}
			else
				ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar1, pGen->pchName);
		}

		if (pchVar2 && *pchVar2 && eCmpBag2 != -1)
		{
			UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar2);
			if(pGlob)
			{
				UIInventoryKey Key = {0};
				Key.erOwner = bOwnedEnt ? entGetRef(pPlayerEnt) : entGetRef(pEntity);
				Key.eBag = eCmpBag2;
				Key.iSlot = iSlot2;
				if (bOwnedEnt)
				{
					Key.eType = entGetType(pEntity);
					Key.iContainerID = entGetContainerID(pEntity);
				}
				estrPrintf(&pGlob->pchString, "%s", gclInventoryMakeKeyString(NULL, &Key));
				bVar2 = true;
			}
			else
				ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchVar2, pGen->pchName);
		}
	}

	if(!bVar1 && pchVar1 && *pchVar1)
	{
		UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar1);
		if(pGlob)
			estrClear(&pGlob->pchString);
	}

	if(!bVar2 && pchVar2 && *pchVar2)
	{
		UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar2);
		if(pGlob)
			estrClear(&pGlob->pchString);
	}
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSlotCountByBagID);
int GenExprGetSlotCountByBagID(SA_PARAM_OP_VALID Entity *pEnt, int BagIdx, int SrcSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
    InventoryBag* pBag = NULL;
    InventorySlot* pSlot = NULL;

    pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagIdx, pExtract);

    if (!pBag)
        return 0;
    pSlot = inv_GetSlotPtr(pBag, SrcSlot);
    if (!pSlot || !pSlot->pItem)
        return 0;

	return pSlot->pItem->count;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSlotCount);
int GenExprGetSlotCount(SA_PARAM_OP_VALID Entity *pEnt, const char *pBagName, int SrcSlot)
{
    int BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
    return GenExprGetSlotCountByBagID(pEnt, BagIdx, SrcSlot);
}

// This will return true if the given item is a BoE item _and_ it is not bound yet. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemIsBindOnEquip);
bool GenExprItemIsBindOnEquip(SA_PARAM_OP_VALID Entity *pEnt, const char *pBagName, int iSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
    int BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
    Item* pItem = inv_GetItemFromBag(pEnt, BagIdx, iSlot, pExtract);
    ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

    if (pItemDef)
        return (pItemDef->flags & kItemDefFlag_BindOnEquip) && !(pItem->flags & kItemFlag_Bound);
    else 
        return false;
}

// This will return true if the item can be used while unequipped
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenCanUseDevice);
bool GenExprCanUseDevice(SA_PARAM_OP_VALID Entity *pEnt, const char *pBagName, int iSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	int BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
	Item* pItem = inv_GetItemFromBag(pEnt, BagIdx, iSlot, pExtract);
	bool bResult = item_IsDeviceUsableByPlayer(pEnt, pItem, (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagIdx, pExtract));
	return bResult;
}

// This uses the specified device if allowed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenUseDevice);
bool GenExprUseDevice(SA_PARAM_OP_VALID Entity *pEnt, const char *pBagName, int iSlot)
{
	Power* ppow;
	S32 iPower;
	int NumPowers;
	bool powerUsed = false;
	int BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item* pItem = inv_GetItemFromBag(pEnt, BagIdx, iSlot, pExtract);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagIdx, pExtract);

	if(!pEnt || !pItem || !pBag || !pEnt->pChar) {
		return false;
	}

	//Make sure the device is usable by the player
	if(!item_IsDeviceUsableByPlayer(pEnt, pItem, pBag)) {
		return false;
	}

	//Execute all useable powers
	NumPowers = item_GetNumItemPowerDefs(pItem, true);
	if(NumPowers < 1) {
		return false;
	}

	for(iPower=NumPowers-1; iPower>=0; iPower--)
	{
		if(item_isPowerUsableByPlayer(pEnt, pItem, pBag, iPower)) {
			U32 powID;

			ppow = item_GetPower(pItem, iPower);
			powID = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;

			entUsePowerID(true,powID);

			powerUsed = true;
		}
	}

	//Return true if any powers were used
	return powerUsed;
}

//Use an item in a given invetory slot
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(UseDevice);
void cmdUseDevice(char *pBagName, int iSlot)
{
	Entity *pEntity = entActiveOrSelectedPlayer();

	if(pEntity)
	{
		GenExprUseDevice(pEntity,pBagName,iSlot);
	}
}

S32 Item_IsBeingTraded(Entity* pEnt, Item* pItem) 
{
	int ii = 0;

	if (pItem)
	{
		U64 itemId = pItem->id;

		// check to see if an item with this id is already in the entity's trade bag
		if (pEnt->pPlayer && pEnt->pPlayer->pTradeBag)
		{
			for (ii = 0; ii < eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots); ii++)
			{
				TradeSlot* pTradeSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, ii);
				Item* pTradeItem = pTradeSlot->pItem;
				if (pTradeItem && (pTradeItem->id == itemId))
				{
					return pTradeSlot->count;
				}
			}
		}
	}

	return 0;
}

// This returns the number of items in this bag/slot currently being offered for trade.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemIsTradedByIndexFromPet);
S32 GenExprItemIsTradedByIndexFromPet(SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_OP_VALID Entity* pPetOrPlayer, int BagIdx, int iSlot)
{
	if (pPetOrPlayer && BagIdx >= 0)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		Item* pItem = inv_GetItemFromBag(pPetOrPlayer, BagIdx, iSlot, pExtract);
		return Item_IsBeingTraded(pEnt, pItem);
	}
	return 0;
}

// This returns the number of items in this bag/slot currently being offered for trade.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemIsTradedByIndex);
S32 GenExprItemIsTradedByIndex(SA_PARAM_NN_VALID Entity* pEnt, int BagIdx, int iSlot)
{
	return GenExprItemIsTradedByIndexFromPet(pEnt, pEnt, BagIdx, iSlot);
}

// This will return true if the item in the given entity's specified bag slot is being offered for trade, 
// false otherwise.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemIsTraded);
bool GenExprItemIsTraded(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pBagName, int iSlot)
{
	int BagIdx;

	if ( pEnt==NULL )
		return false;

	BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
	return GenExprItemIsTradedByIndex(pEnt, BagIdx, iSlot);
}

Item* GetLootItem( S32 iSlot );

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemMoveValid);
bool GenExprItemMoveValid(SA_PARAM_OP_VALID Entity* pEnt, bool bGrabDropAll,
								   bool bSrcIsGuild, bool bDstIsGuild,
								   SA_PARAM_NN_STR const char* pSrcBag, int iSrcSlot, 
								   SA_PARAM_NN_STR const char* pDstBag, int iDstSlot)
{
	Item *pItem;
	ItemDef *pItemDef;
	
	int iSrcBagID = StaticDefineIntGetInt(InvBagIDsEnum, pSrcBag);
	int iDstBagID = StaticDefineIntGetInt(InvBagIDsEnum, pDstBag);
	
	if(!pEnt)
		return false;

	if (iSrcBagID != InvBagIDs_Loot) {
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		pItem = bSrcIsGuild ? inv_guildbank_GetItemFromBag(pEnt, iSrcBagID, iSrcSlot) : inv_GetItemFromBag(pEnt, iSrcBagID, iSrcSlot, pExtract);
		pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		
		if (pItemDef) {
			bool bResult = item_ItemMoveValid(pEnt, pItemDef, bSrcIsGuild, iSrcBagID, iSrcSlot, bDstIsGuild, iDstBagID, iDstSlot, pExtract);
			return bResult;
		}
	} else {
		pItem = GetLootItem( iSrcSlot );
		pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		
		if (pItemDef) {
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			InventorySlot* pSlot = inv_ent_GetSlotPtr(pEnt, iDstBagID, iDstSlot, pExtract);
			bool bResult;

			if (pSlot && pSlot->pItem && iDstBagID) {
				InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDstBagID, pExtract);
				if (invbag_flags(pBag) & InvBagFlag_PlayerBagIndex) {
					if (pItemDef->eType != kItemType_Bag) {
						InventorySlot* pSrcSlot = inv_ent_GetSlotPtr(pEnt, iSrcBagID, iSrcSlot, pExtract);
						InventoryBag *pPlayerBag = (InventoryBag *)inv_PlayerBagFromSlotIdx(pEnt, iDstSlot);
						if (pBag && inv_CanItemFitInBag(pPlayerBag, pItem, 1) && GamePermissions_trh_CanAccessBag(CONTAINER_NOCONST(Entity, pEnt), pPlayerBag->BagID, pExtract)) {
							return true;
						}
					}
				}
				
				return false;
			}
			
			bResult = item_ItemMoveDestValid(pEnt, pItemDef, pItem, false, iDstBagID, iDstSlot, false, pExtract);

			return bResult;
		}
	}
	
	return false;
}

//MULTIPLE ENTITY VERSION
//NOTE: shouldn't need loot functionality for this function
static bool GenExprItemMoveValidAcrossEnts(	bool bGrabDropAll,
											SA_PARAM_OP_VALID Entity* pEntSrc,
											int iSrcBagID, int iSrcSlot,
											SA_PARAM_OP_VALID Entity* pEntDst,
											int iDstBagID, int iDstSlot,
											int count, bool bNoSwap)
{
	Item *pItem;
	ItemDef *pItemDef;
	bool bResult = false;

	if ( pEntSrc==NULL || pEntDst==NULL )
		return false;

	if ( iSrcBagID != InvBagIDs_Loot )
	{
		GameAccountDataExtract *pExtractSrc = entity_GetCachedGameAccountDataExtract(pEntSrc);
		pItem = inv_GetItemFromBag( pEntSrc, iSrcBagID, iSrcSlot, pExtractSrc);
		pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

		if ( pItemDef )
		{
			GameAccountDataExtract *pExtractDst = entity_GetCachedGameAccountDataExtract(pEntDst);
			int tempCount;
			Entity* pPlayerEnt = entActiveOrSelectedPlayer();

			tempCount = count;
			if (tempCount <= 0 && (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntSrc), iSrcBagID, pExtractSrc))
			{
				tempCount = inv_bag_GetSlotItemCount( (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntSrc), iSrcBagID, pExtractSrc), iSrcSlot );
			}

			if ( iDstBagID <= InvBagIDs_None )
				iDstBagID = GetBestBagForItemDef(pEntDst, pItemDef, tempCount, false, pExtractDst);
			if(bNoSwap) {
				InventoryBag *pDstBag = pEntDst ? (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntDst), iDstBagID, pExtractDst) : NULL;
				if(pDstBag)
				{
					bResult = !inv_bag_BagFull(pEntDst, pDstBag) && item_ItemMoveValidAcrossEntsWithCount(pPlayerEnt, pEntSrc, pItemDef, tempCount, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pEntDst, inv_IsGuildBag(iDstBagID), iDstBagID, iDstSlot, pExtractSrc);
				}
			} else {
				bResult = item_ItemMoveValidAcrossEntsWithCount(pPlayerEnt, pEntSrc, pItemDef, tempCount, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pEntDst, inv_IsGuildBag(iDstBagID), iDstBagID, iDstSlot, pExtractSrc);
			}
		}
	}

	return bResult;
}

// DWHITE TODO: remove this function and update the inventory UIGen
// Check for a valid move of items across ents.  Do not allow swapping of items
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemMoveValidAcrossEnts);
bool GenExprItemMoveValidAcrossEntsNoSwap(	bool bGrabDropAll,
										  SA_PARAM_OP_VALID Entity* pEntSrc,
										  int iSrcBagID, int iSrcSlot,
										  SA_PARAM_OP_VALID Entity* pEntDst,
										  int iDstBagID, int iDstSlot,
										  int count)
{
	return GenExprItemMoveValidAcrossEnts(bGrabDropAll, pEntSrc, iSrcBagID, iSrcSlot, pEntDst, iDstBagID, iDstSlot, count, true);
}



//is the item temporarily disabled due to trade/craft/invalid move?
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemIsDisabled);
bool GenExprItemIsDisabled(	SA_PARAM_NN_VALID Entity* pEnt, 
							bool bGrabMode, bool bGrabDropAll,
							bool bDstIsGuild, SA_PARAM_NN_STR const char* pcDstBag, S32 iDstSlot,
							bool bSrcIsGuild, SA_PARAM_NN_STR const char* pcSrcBag, S32 iSrcSlot)
{
	int iDstBagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pcDstBag);

	if (	GenExprItemIsTradedByIndex( pEnt, iDstBagIdx, iDstSlot )
		||	ExperimentIsItemInListByBagIndex( pEnt, iDstBagIdx, iDstSlot ))
	{
		return false;
	}

	if ( bGrabMode )
	{
		return GenExprItemMoveValid( pEnt, bGrabDropAll, bSrcIsGuild, bDstIsGuild, pcSrcBag, iSrcSlot, pcDstBag, iDstSlot );
	}

	return true;
}

//MULTIPLE ENTITY VERSION
//is the item temporarily disabled due to trade/craft/invalid move?
//if not in grab mode, then only pass destination information
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemIsDisabledAcrossEnts);
bool GenExprItemIsDisabledAcrossEnts(	bool bGrabMode, bool bGrabDropAll,
										SA_PARAM_OP_VALID Entity* pEntSrc, 
										int iSrcBagID, S32 iSrcSlot,
										SA_PARAM_OP_VALID Entity* pEntDst,
										int iDstBagID, S32 iDstSlot,
										int count)
{	
	if ( pEntSrc==NULL )
		return false;

	if ( pEntDst==NULL )
		return false;

	if (	GenExprItemIsTradedByIndex( pEntDst, iDstBagID, iDstSlot )
		||	ExperimentIsItemInListByBagIndex( pEntDst, iDstBagID, iDstSlot ))
	{
		return false;
	}

	if ( bGrabMode )
	{
		return GenExprItemMoveValidAcrossEnts( bGrabDropAll, pEntSrc, iSrcBagID, iSrcSlot, pEntDst, iDstBagID, iDstSlot, count, false );
	}

	return true;
}

#define EXCH_RATE 100
AUTO_EXPR_FUNC(UIGen, Player) ACMD_NAME(GenGetResourceString);
char* GenExprGetResourceString(ExprContext* context, int iResources, const char* pchValueFormatKey, bool bShowZeros)
{
	char *estrResult = NULL;
	char *result = NULL;
	estrStackCreate(&estrResult);
	
	item_GetFormattedResourceString(langGetCurrent(), &estrResult, iResources, pchValueFormatKey, EXCH_RATE, bShowZeros);

	if (strlen(estrResult))
	{
		result = exprContextAllocScratchMemory(context, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
	}
	estrDestroy(&estrResult);
	return NULL_TO_EMPTY(result);
}

// Searches through all the Entity's Puppets to find one with an InventoryBag
//  that matches the first bag restriction on the ItemDef.  The first one it
//  finds it returns.  If it can't find one it returns the master Entity.
// Technically this should be per-Power on the ItemDef due to propagation, but
//  this is getting into really horrible hacky code as it is, so stupid wins
//  for now.
SA_RET_NN_VALID static Entity *STOFindDescriptionEntity(SA_PARAM_NN_VALID Entity *pMaster, SA_PARAM_OP_VALID ItemDef *pItemDef, U64 uiItemID)
{
	if(pMaster->pSaved && pMaster->pSaved->pPuppetMaster && pItemDef && eaiGet(&pItemDef->peRestrictBagIDs, 0))
	{
		int i;
		InvBagIDs eBag = eaiGet(&pItemDef->peRestrictBagIDs, 0);
		PuppetMaster *pPuppetMaster = pMaster->pSaved->pPuppetMaster;
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pMaster);
		Entity *pActivePuppet = NULL;

		if (inv_GetBag(CONTAINER_NOCONST(Entity, pMaster),eBag,pExtract))
		{
			pActivePuppet = pMaster;
		}

		if (!pActivePuppet)
		{
			for(i=eaSize(&pPuppetMaster->ppPuppets)-1; i>=0; i--)
			{
				Entity *pPuppet = GET_REF(pPuppetMaster->ppPuppets[i]->hEntityRef);
				if (!pPuppet || (entGetType(pPuppet) == pPuppetMaster->curType && entGetContainerID(pPuppet) == pPuppetMaster->curID))
					continue;
				if(uiItemID && inv_GetItemByID(pPuppet, uiItemID))
				{
					pActivePuppet = pPuppet;
					break;
				}
				else if(pPuppetMaster->ppPuppets[i]->eState == PUPPETSTATE_ACTIVE && inv_GetBag(CONTAINER_NOCONST(Entity, pPuppet),eBag,pExtract))
				{
					pActivePuppet = pPuppet;
				}
			}
		}

		if(pActivePuppet && pActivePuppet != pMaster) {
			if(pActivePuppet->pChar)
			{
				// Since this is very likely a subscribed entity we need to set up
				//  the backpointer
				//  the combat level
				//  .. anything else?
				pActivePuppet->pChar->pEntParent = pActivePuppet;
				pActivePuppet->pChar->iLevelCombat = entity_GetSavedExpLevel(pMaster);
			}
			return pActivePuppet;
		}
	}

	return pMaster;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemFindDescriptionEntity");
SA_RET_OP_VALID Entity *GenExprItemFindDescriptionEntity(SA_PARAM_OP_VALID Item *pItem)
{
	Entity *pPlayer = entActivePlayerPtr();
	return pItem && pPlayer ? STOFindDescriptionEntity(pPlayer, GET_REF(pItem->hItem), pItem->id) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemDefFindDescriptionEntity");
SA_RET_OP_VALID Entity *GenExprItemDefFindDescriptionEntity(SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	Entity *pPlayer = entActivePlayerPtr();
	return pItemDef && pPlayer ? STOFindDescriptionEntity(pPlayer, pItemDef, 0) : NULL;
}

// Builds custom auto description for array of powers from GenExprGetItemPowerDescByType.
// NOTE(JW): This is hardcoded to be NORMALIZED FOR STO. If you don't know what that means,
//  or you need non-normalized descriptions, talk to Jered.
// NOTE(JW): This is hardcoded to display Combo Powers as their unrestricted base Power.
static void STOPowersAutoDescCustom(Power** eaPowers,
									Entity* pEnt,
									Item* pItem,
									char **ppchDesc,
									const char* pchPowerMessageKey,
									const char* pchAttribModsMessageKey)
{
	char* estrPowerDesc = NULL;
	Character* pChar = pEnt ? pEnt->pChar : NULL;
	AutoDescPower* pAutoDesc = StructCreate(parse_AutoDescPower);
	GameAccountDataExtract *pExtract = NULL;
	int i = 0;
	int len = eaPowers ? eaSize(&eaPowers) : 0;

	if(!len || !pChar) {
		StructDestroy(parse_AutoDescPower, pAutoDesc);
		return;
	}

	// Enable attribute overrides
	g_CombatEvalOverrides.bEnabled = true;
	g_CombatEvalOverrides.bAttrib = true;
	STONormalizeAttribs(NULL);

	estrStackCreate(&estrPowerDesc);
	for(i = 0; i < len; i++) {
		Power* pPow = eaPowers[i];
		PowerDef* pPowerDef = GET_REF(pPow->hDef);
		S32 bCreatedSubPowers = false;
		PowerSource eSourceOld = kPowerSource_Unset;
		Item *pSourceItemOld = NULL;
		if(!pPowerDef)
			continue;
		
		// Find the bottom-most combo that doesn't have a requires (should always be
		//  the first one we look at)
		if(pPowerDef->eType==kPowerType_Combo)
		{
			int j;
			for(j=eaSize(&pPowerDef->ppOrderedCombos)-1; j>=0; j--)
			{
				if(!pPowerDef->ppOrderedCombos[j]->pExprRequires)
				{
					// Hack(JW): If the server hasn't made subpowers for this yet (likely because
					//  it's not equipped) we'll let the client make them on its own so we can
					//  use the standard description code.  After the description runs, we'll
					//  destroy the array.  This makes me uncomfortable, but it should work ok.
					if(!eaSize(&pPow->ppSubPowers))
					{
						power_CreateSubPowers(pPow);
						bCreatedSubPowers = true;
					}
					// Hack(AMA): Not sure what to do here. Jered says character_ResetPowersArray was supposed
					//  to run but didn't, and I don't know the system well enough to find where to put it, 
					//  so I'm going with the suggested plan B and just bailing if the array sizes don't match
					if (eaSize(&pPowerDef->ppOrderedCombos) == eaSize(&pPow->ppSubPowers))
					{
						pPow = pPow->ppSubPowers[j];
						pPowerDef = GET_REF(pPowerDef->ppOrderedCombos[j]->hPower);
						break;
					}
				}
			}
		}

		// Hack(JW) - in order to get local enhancements to show up on Items that are on offline Entities
		//  we temporarily set the Power's source and source Item
		if(pItem)
		{
			eSourceOld = pPow->eSource;
			pSourceItemOld = pPow->pSourceItem;
			pPow->eSource = kPowerSource_Item;
			pPow->pSourceItem = pItem;
		}

		if (!pExtract) {
			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		}

		StructReset(parse_AutoDescPower, pAutoDesc);
		power_AutoDesc(entGetPartitionIdx(pEnt), pPow, pEnt->pChar, NULL, pAutoDesc, NULL, NULL, NULL, false, -1, entGetPowerAutoDescDetail(entActiveOrSelectedPlayer(), true), pExtract,NULL);
		powerdef_AutoDescCustom(pEnt, &estrPowerDesc, pPowerDef, pAutoDesc, pchPowerMessageKey, pchAttribModsMessageKey);
		estrAppend(ppchDesc, &estrPowerDesc);
		estrClear(&estrPowerDesc); 

		if(pItem)
		{
			pPow->eSource = eSourceOld;
			pPow->pSourceItem = pSourceItemOld;
		}

		if(bCreatedSubPowers)
		{
			pPow = pPow->pParentPower ? pPow->pParentPower : pPow;
			eaDestroyStruct(&pPow->ppSubPowers, parse_Power);
		}
	}
	estrDestroy(&estrPowerDesc);
	StructDestroy(parse_AutoDescPower, pAutoDesc);

	// Disable attribute overrides
	g_CombatEvalOverrides.bEnabled = false;
	g_CombatEvalOverrides.bAttrib = false;
}

//If bAllExceptType is true, returns a description of iPowerDepth number of powers except the type passed in
//Otherwise returns a description of iPowerDepth number of powers only of the type passed in
// NOTE(JW): This is hardcoded to be NORMALIZED FOR STO. If you don't know what that means,
//  or you need non-normalized descriptions, talk to Jered.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemPowerDescByType);
char* GenExprGetItemPowerDescByType(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Item *pItem, 
									const char* pchTypes, int iPowerDepth, 
									const char* pchPowerKey, const char* pchAttribKey,
									bool bAllExceptType)
{
	char* result = NULL;
	if (pItem && pEnt)
	{
		int i;
		int count = 0;
		int numPowers = item_GetNumItemPowerDefs(pItem, true);
		static int* eaiTypes = NULL;
		static Power** eaPowers = NULL;
		char* estrResult = NULL;

		char* typeContext = NULL;
		char* pchTypesDup = strdup(pchTypes);
		char* pchToken;
		pchToken = strtok_s(pchTypesDup, ",", &typeContext);

		if(eaiTypes) {
			eaiClear(&eaiTypes);
		}
		if(eaPowers) {
			eaClear(&eaPowers);
		}

		while (pchToken)
		{
			int eType = StaticDefineIntGetInt(PowerTypeEnum, pchToken);
			if(eType > -1) {
				eaiPush(&eaiTypes, eType);
			}
			pchToken = strtok_s(NULL, ",", &typeContext);
		}
		free(pchTypesDup);

		if(iPowerDepth == -1) {
			iPowerDepth = numPowers;
		}

		for(i = 0; i < numPowers; i++) {
			Power* pPower = item_GetPower(pItem, i);
			PowerDef* pPowerDef = pPower ? GET_REF(pPower->hDef) : NULL;
			ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem, i);
			if(!pPowerDef || !pItemPowerDef || pItemPowerDef->flags&kItemPowerFlag_LocalEnhancement)
				continue;

			if(!bAllExceptType) {
				if(eaiFind(&eaiTypes, pPowerDef->eType) > -1 && count < iPowerDepth) {
					eaPush(&eaPowers, pPower);
					count++;
				} 
			} else {
				if(eaiFind(&eaiTypes, pPowerDef->eType) == -1 && count < iPowerDepth) {
					eaPush(&eaPowers, pPower);
					count++;
				} 
			}

		}

		if(eaPowers) {
			ItemDef *pItemDef = GET_REF(pItem->hItem);
			Entity *pentDesc = STOFindDescriptionEntity(pEnt, pItemDef, pItem->id); //pEnt;
			STOPowersAutoDescCustom(eaPowers, pentDesc, pItem, &estrResult, pchPowerKey, pchAttribKey);
			eaDestroy(&eaPowers);
		}

		if (estrResult)
		{
			result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
			memcpy(result, estrResult, strlen(estrResult) + 1);
			estrDestroy(&estrResult);
		}
	}
	return NULL_TO_EMPTY(result);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetNumItemPowersOfType);
int GenExprGetNumItemPowersOfType(ExprContext *pContext, SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID Item *pItem, const char* pchTypes)
{
	int i;
	int totalNumPowers = item_GetNumItemPowerDefs(pItem, true);
	int numPowers = 0;
	int* eaiTypes = NULL;

	char* typeContext = NULL;
	char* pchTypesDup = strdup(pchTypes);
	char* pchToken;
	pchToken = strtok_s(pchTypesDup, ",", &typeContext);

	while (pchToken)
	{
		int eType = StaticDefineIntGetInt(PowerTypeEnum, pchToken);
		if(eType > -1) {
			eaiPush(&eaiTypes, eType);
		}
		pchToken = strtok_s(NULL, ",", &typeContext);
	}
	free(pchTypesDup);

	for(i = 0; i < totalNumPowers; i++) {
		Power* pPower = item_GetPower(pItem, i);
		PowerDef* pPowerDef = pPower ? GET_REF(pPower->hDef) : NULL;
		ItemPowerDef *pItemPowerDef = item_GetItemPowerDef(pItem, i);
		if(!pPowerDef || !pItemPowerDef || pItemPowerDef->flags&kItemPowerFlag_LocalEnhancement)
			continue;
 
		if(eaiFind(&eaiTypes, pPowerDef->eType) > -1) {
			numPowers++;
		}

	}

	eaiDestroy(&eaiTypes);

	return numPowers;

}

//This function creates a quick custom description.  For use with GenGetPowerDescByType to get the power descriptions.
// NOTE(JW): This is hardcoded to be NORMALIZED. If you don't know what that means,
//  or you need non-normalized descriptions, talk to Jered.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemDescriptionCustomShort, GenGetNormalizedItemDescCustom);
const char* GenExprGetNormalizedItemDescCustom(ExprContext *pContext,
												 SA_PARAM_OP_VALID Entity* pEnt,
												 SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID ItemDef* pItemDef, 
												 const char* pchDescriptionKey, 
												 const char* pchPowers, 
												 const char* pchItemSetDesc,
												 const char* pchInnateDescKey,
												 const char* pchResourceType, 
												 const char* pchValueKey
												 )
{
	/* Information currently available:
	 * Item									{Item}
	 * ItemDef								{ItemDef}
	 * Item Name							{Name}
	 * Item Description						{ItemDesc}
	 * Item Quality Color					{ItemQuality}
	 * Item Quality Enum Value				{ItemQualityInt}
	 * Item Quality Translated Display Msg	{ITemQualityMsg}
	 * Item Sort Type						{ItemSortType}
	 * Current Durability					{ItemDurabilityCurrent}
	 * Max Durability						{ItemDurabilityMax}
	 * Current Durability Percentage		{ItemDurabilityPct}
	 * Quest Item Message					{ItemQuest}
	 * Item Bind Status						{ItemBind}
	 * Number of Bag Slots					{ItemBagSlots}
	 * Innate Mods							{ItemInnateMods}
	 * Formatted Item Value					{FormattedItemValue}
	 * Item Value							{ItemValue}
	 * Consumed on Use						{ItemConsumedOnUse}		1=true 0=false
	 * Primary Equip Bag					{EquipBag}
	 * Item Tag								{ItemTag}
	 */

	char* result = NULL;
	char* estrResult = NULL;
	estrCreate(&estrResult);

	item_GetNormalizedDescCustom(&estrResult, 
								 pEnt, 
								 pItem, 
								 pItemDef,
								 pchDescriptionKey, 
								 pchPowers, 
								 pchItemSetDesc, 
								 pchInnateDescKey, 
								 pchResourceType, 
								 pchValueKey, 
								 EXCH_RATE);
	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}

	return NULL_TO_EMPTY(result);
}

static void GetItemDescriptionCrafting(
		ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID ItemDef *pItemDef, 
		const char *pchPowerKey, const char *pchModKey, const char *pchCraftKey, 
		const char *pchItemValue, 
		const char *pchSkillColors, 
		const char **pchItemSpecialization, 
		bool *pbHasSpecialization, bool *pbIsUsableSpecialization,
		bool *bIsItemRecipe, bool *bIsAlgoItemRecipe, 
		char **pestrItemRecipe, 
		char **pestrItemPowerRecipe,
		GameAccountDataExtract *pExtract)
{
	if (SAFE_MEMBER(pItemDef, pCraft) && pchCraftKey)
	{
		*bIsItemRecipe = GET_REF(pItemDef->pCraft->hItemResult) || GET_REF(pItemDef->pCraft->hItemPowerResult);
		*bIsAlgoItemRecipe = *bIsItemRecipe && SAFE_MEMBER(pItemDef, Group) != 0;
		if (*bIsItemRecipe)
		{
			if (pItemDef->eType == kItemType_ItemRecipe
				|| pItemDef->eType == kItemType_ItemValue)
			{
				ItemDef *pItemRecipe = GET_REF(pItemDef->pCraft->hItemResult);
				Item *pResultItem = pItemRecipe ? item_FromEnt(CONTAINER_NOCONST(Entity, pEntity),pItemRecipe->pchName, 0,NULL,0) : NULL;
				Message *pmsgItemSpecialization;
				
				estrAppend2(pestrItemRecipe, GenExprGetItemDescriptionCustom(pContext, pEntity, pResultItem, NULL, pchItemValue, pchSkillColors, pchCraftKey, pchPowerKey, pchModKey, NULL));
				StructDestroy(parse_Item, pResultItem);
				if (1 <= pItemRecipe->eTag && pItemRecipe->eTag <= 9)
				{
					*pbIsUsableSpecialization = eaiFind(&pEntity->pPlayer->eSkillSpecialization, pItemRecipe->eTag) >= 0;
					*pbHasSpecialization = true;
					pmsgItemSpecialization =  StaticDefineGetMessage(ItemTagEnum, pItemRecipe->eTag);
				}
				else 
				{
					pmsgItemSpecialization = NULL;
				}
				*pchItemSpecialization = TranslateMessagePtr(pmsgItemSpecialization);
			}
			else if (pItemDef->eType == kItemType_ItemPowerRecipe)
			{
				ItemPowerDef *pItemRecipe = GET_REF(pItemDef->pCraft->hItemPowerResult);
				PowerDef *pResultPower = pItemRecipe ? GET_REF(pItemRecipe->hPower) : NULL;
				
				if (pResultPower)
				{
					AutoDescPower* pAutoDesc = StructCreate(parse_AutoDescPower);
					powerdef_AutoDesc(entGetPartitionIdx(pEntity), pResultPower, NULL, pAutoDesc, NULL, NULL, NULL, NULL, NULL, NULL, 1, true, kAutoDescDetail_Normal, pExtract, NULL);
					powerdef_AutoDescCustom(pEntity, pestrItemPowerRecipe, pResultPower, pAutoDesc, pchPowerKey, pchModKey);
					StructDestroy(parse_AutoDescPower, pAutoDesc);
				}
			}
		}
	}
}
/*
static void GetItemDescriptionInfuse(SA_PARAM_OP_VALID Item *pItem, SA_PARAM_NN_VALID ItemDef *pItemDef, char **pestrInfuseDesc)
{
	int i,s=eaSize(&pItemDef->ppInfuseSlotDefRefs);
	for(i=0; i<s; i++)
	{
		InfuseSlotDef *pInfuseSlotDef = item_GetInfuseSlotDef(pItemDef,i);
		if(pInfuseSlotDef)
		{
			const char *pchIcon=NULL;
			char *estrCurrent = NULL;
			InfuseSlot *pInfuseSlot = pItem ? eaIndexedGetUsingInt(&pItem->ppInfuseSlots,i) : NULL;
			
			pchIcon = infuse_GetIcon(pInfuseSlotDef,pInfuseSlot);

			if(pInfuseSlot)
			{
				ItemDef *pItemDefOption = GET_REF(pInfuseSlot->hItem);
				const char *pchItem = pItemDefOption ? TranslateDisplayMessage(pItemDefOption->displayNameMsg) : NULL;
				estrStackCreate(&estrCurrent);
				FormatGameMessageKey(&estrCurrent,"Item.UI.Infuse.Current",
					STRFMT_STRING("Item",NULL_TO_EMPTY(pchItem)),
					STRFMT_INT("Rank",pInfuseSlot->iRank+1),
					STRFMT_END);
			}
			
			FormatGameMessageKey(pestrInfuseDesc, "Inventory_ItemInfo_InfuseFormat",
				STRFMT_DISPLAYMESSAGE("Name",pInfuseSlotDef->msgDisplayName),
				STRFMT_DISPLAYMESSAGE("Description",pInfuseSlotDef->msgDescription),
				STRFMT_STRING("Icon",NULL_TO_EMPTY(pchIcon)),
				STRFMT_STRING("Current",NULL_TO_EMPTY(estrCurrent)),
				STRFMT_END);
			
			estrDestroy(&estrCurrent);
		}
		else
		{
			// Display something for currently unknown slots?
		}
	}
}
*/

static void GetItemDescriptionWarp(SA_PARAM_OP_VALID Item *pItem, SA_PARAM_NN_VALID ItemDef *pItemDef, char **pestrDesc)
{
	if(pItemDef->pWarp)
	{
		char *estrWarpActionDesc = NULL;
		char *estrSpawn = NULL;
		estrStackCreate(&estrWarpActionDesc);
		estrStackCreate(&estrSpawn);

		if(pItemDef->pWarp->pchSpawn)
		{
			if(!stricmp(pItemDef->pWarp->pchSpawn, START_SPAWN))
			{
				FormatGameMessageKey(&estrSpawn, "Item.UI.Warp.StartSpawn", STRFMT_END);
			}
			else if(!stricmp(pItemDef->pWarp->pchSpawn, SPAWN_AT_NEAR_RESPAWN))
			{
				FormatGameMessageKey(&estrSpawn, "Item.UI.Warp.ClosestSpawn", STRFMT_END);
			}
			else
			{
				//An unknown but specified spawn location
				FormatGameMessageKey(&estrSpawn, "Item.UI.Warp.SpecifiedSpawn", STRFMT_END);
			}
		}
		else
		{
			FormatGameMessageKey(&estrSpawn, "Item.UI.Warp.NoSpawn", STRFMT_END);
		}

		switch(pItemDef->pWarp->eWarpType)
		{
		case kItemWarp_SelfToMapSpawn:
			{
				if( !pItemDef->pWarp->bCanMapMove )
				{
					FormatGameMessageKey(&estrWarpActionDesc, "Item.UI.Warp.Self.Evac", 
						STRFMT_STRING("Spawn", NULL_TO_EMPTY(estrSpawn)),
						STRFMT_END);
				}
				else
				{
					const char *pchMap = "Unknown Map";
					if(pItemDef->pWarp->pchMap && zmapInfoGetByPublicName(pItemDef->pWarp->pchMap))
					{
						pchMap = TranslateMessageKey(zmapInfoGetDisplayNameMsgKey(zmapInfoGetByPublicName(pItemDef->pWarp->pchMap)));
					}
					FormatGameMessageKey(&estrWarpActionDesc, "Item.UI.Warp.Self.Evac.Global", 
						STRFMT_STRING("Map", pchMap),
						STRFMT_STRING("Spawn", NULL_TO_EMPTY(estrSpawn)),
						STRFMT_END);
				}
				break;
			}
		case kItemWarp_TeamToMapSpawn:
			{
				const char *pchMap = "Unknown Map";
				if(pItemDef->pWarp->pchMap && zmapInfoGetByPublicName(pItemDef->pWarp->pchMap))
				{
					pchMap = TranslateMessageKey(zmapInfoGetDisplayNameMsgKey(zmapInfoGetByPublicName(pItemDef->pWarp->pchMap)));
				}
				if( !pItemDef->pWarp->bCanMapMove )
				{
					FormatGameMessageKey(&estrWarpActionDesc, "Item.UI.Warp.Team.Evac",
					STRFMT_STRING("Spawn", NULL_TO_EMPTY(estrSpawn)),
					STRFMT_END);
				}
				else
				{
					FormatGameMessageKey(&estrWarpActionDesc, "Item.UI.Warp.Team.Evac.Global",
					STRFMT_STRING("Map", pchMap),
					STRFMT_STRING("Spawn", NULL_TO_EMPTY(estrSpawn)),
					STRFMT_END);
				}
				break;
			}
		case kItemWarp_SelfToTarget:
			{
				FormatGameMessageKey(&estrWarpActionDesc, "Item.UI.Warp.Self.Target", STRFMT_END);
				break;
			}
		case kItemWarp_TeamToSelf:
			{
				FormatGameMessageKey(&estrWarpActionDesc, "Item.UI.Warp.Team.ToSelf", STRFMT_END);
				break;
			}
		default:
			break;
		}
		
		if(pItemDef->pWarp->bLimitedUse)
		{
			FormatGameMessageKey(pestrDesc, "Inventory_ItemInfo_LimitedWarp",
				STRFMT_STRING("WarpAction", estrWarpActionDesc),
				STRFMT_END);
		}
		else
		{
			FormatGameMessageKey(pestrDesc, "Inventory_ItemInfo_UnlimitedWarp",
				STRFMT_STRING("WarpAction", estrWarpActionDesc),
				STRFMT_END);
		}
		estrDestroy(&estrWarpActionDesc);
		estrDestroy(&estrSpawn);
	}
}




AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemDescriptionCustom);
const char* GenExprGetItemDescriptionCustom(ExprContext* pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID ItemDef *pItemDef, const char *pchItemValue, const char *pchSkillColors, ACMD_EXPR_DICT(Message) const char *pchDescriptionKey, ACMD_EXPR_DICT(Message) const char *pchPowerKey, ACMD_EXPR_DICT(Message) const char *pchModKey, ACMD_EXPR_DICT(Message) const char *pchCraftKey)
{
	char *estrResult = NULL;
	char *result = NULL;
	UsageRestriction *pRestrict = pItemDef ? pItemDef->pRestriction : NULL;
	Character *pCharacter = pEntity ? pEntity->pChar : NULL;
	int iEntLevel = pCharacter ? pCharacter->iLevelCombat : 0;
	SkillType eEntSkill = entity_GetSkill(pEntity);
	int iEntSkillLevel = entity_GetSkillLevel(pEntity);

	if (!pItemDef)
	{
		pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	}

	if (pEntity && pItemDef)
	{
		char pchTempFmt[256] = {0};
		SavedEntityData *pSaved = SAFE_MEMBER(pEntity, pSaved);
		GameAccountData *pAccountData = entity_GetGameAccount(pEntity);
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);

		int iItemBagSlots = 0;

		bool bEntUsableLevels = false;
		bool bEntUsableExpr = true;
		
		bool bCostumeUnlock = false;
		bool bNewCostume = false;

		bool bIsItemRecipe = false;
		char *estrItemPowerRecipe = NULL;
		char *estrItemRecipe = NULL;
		bool bIsAlgoItemRecipe = false;
		const char *pchItemRecipeLearned = NULL;
		bool bEntLearnedRecipe = false;
		
		int iEquipLimit = 0;
		int iEquipLimitCategory = 0;
		const char* pchEquipLimitCategory = NULL;

		const char *pchItemQuest = NULL;
		const char *pchItemBind = NULL;
		const char *pchItemQuality = NULL;
		const char *pchItemName = NULL;
		const char *pchItemUsable = TranslateMessageKey("Item.UI.Usable");

		bool bItemHasSpecialization = false;
		bool bIsUsableSpecialization = false;
		const char *pchItemSpecialization = NULL;

		const char *pchItemSkillName = NULL;
		int iItemSkillValue = 0;
		bool bEntUsableSkill = false;
		char *pchItemSkillColor = "White";

		char *pchItemTypeColor = "White";
		char *estrItemType = NULL;
		char *estrItemComponentType = NULL;
		char *estrItemEquipLevel = NULL;
		char *estrComponentSkill = NULL;
		char *estrItemBagSize = NULL;

		const char *pchItemDesc = NULL;
		const char *pchItemDescShort = NULL;
		char *estrItemAlgoDesc = NULL;
		char *estrClasssesAllowed = NULL;
		char *estrItemPowerAutoDesc = NULL;
		char *estrItemInnatePowerAutoDesc = NULL;
//		char *estrInfuseDesc = NULL;
		char *estrItemSetDesc = NULL;
		char *estrWarpDesc = NULL;
		
		char *estrSavedPetPowers = NULL;

		bool bSellable = true;

		if (!pchItemUsable)
		{
			pchItemUsable = "[UNTRANSLATED: Item.UI.Usable]";
		}

		// Extract variables
		pchItemQuality = StaticDefineIntRevLookup(ItemQualityEnum, pItem ? item_GetQuality(pItem) : pItemDef->Quality);
		pchItemName = pItem ? item_GetName(pItem, pEntity) : TranslateDisplayMessage(pItemDef->displayNameMsg);
		if (pRestrict || (pItem && (pItem->flags & kItemFlag_Algo)))
		{
			MultiVal mvResult;

			if (!pItem || !(pItem->flags & kItemFlag_Algo))
			{
				S32 iMinLevel = pRestrict->iMinLevel;
				// take the higher of the created from source or prestrict min level
				if(pItem && pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought) && item_GetMinLevel(pItem) > iMinLevel)
				{
					iMinLevel = item_GetMinLevel(pItem);
				}
				if (pRestrict->iMaxLevel > iMinLevel)
				{
					bEntUsableLevels = iMinLevel <= iEntLevel && (pRestrict->iMaxLevel <= iMinLevel || iEntLevel <= pRestrict->iMaxLevel);
					estrStackCreate(&estrItemEquipLevel);
					FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.MinMax", STRFMT_INT("MinLevel", MAX(1, iMinLevel)), STRFMT_INT("MaxLevel", pRestrict->iMaxLevel), STRFMT_END);
				}
				else if((pItemDef->flags & kItemDefFlag_NoMinLevel))
				{
					bEntUsableLevels = true;
				}
				else if (iMinLevel > 0)
				{
					bEntUsableLevels = iMinLevel <= iEntLevel && (pRestrict->iMaxLevel <= iMinLevel || iEntLevel <= pRestrict->iMaxLevel);
					estrStackCreate(&estrItemEquipLevel);
					FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.Min", STRFMT_INT("MinLevel", iMinLevel), STRFMT_END);
				}
			}
			else if (pItem && (pItem->flags & kItemFlag_Algo))
			{
				bEntUsableLevels = item_GetMinLevel(pItem) <= iEntLevel;
				if (item_GetMinLevel(pItem) > 1)
				{
					estrStackCreate(&estrItemEquipLevel);
					FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.Min", STRFMT_INT("MinLevel", item_GetMinLevel(pItem)), STRFMT_END);
				}
			}

			if (pRestrict && pRestrict->eSkillType != kSkillType_None)
			{
				bEntUsableSkill = pRestrict->eSkillType == eEntSkill && inv_GetNumericItemValue(pEntity, "SkillLevel") >= (S32)pRestrict->iSkillLevel;
				sprintf(pchTempFmt, "Item.UI.%s", StaticDefineIntRevLookup(SkillTypeEnum, pRestrict->eSkillType));
				pchItemSkillName = TranslateMessageKey(pchTempFmt);
				iItemSkillValue = pRestrict->iSkillLevel;
			}

			if (pRestrict)
			{
				bEntUsableExpr = pRestrict->pRequires == NULL;
				if (!bEntUsableExpr)
				{
					itemeval_Eval(PARTITION_CLIENT, pItemDef->pRestriction->pRequires, pItemDef, NULL, pItem, pEntity, item_GetLevel(pItem), item_GetQuality(pItem), 0, pItemDef->pchFileName, 0, &mvResult);
					bEntUsableExpr = itemeval_GetIntResult(&mvResult,pItemDef->pchFileName,pItemDef->pRestriction->pRequires);
				}
			}
		}
		else if (pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought) && item_GetMinLevel(pItem) && !(pItemDef->flags & kItemDefFlag_NoMinLevel))
		{
			bEntUsableLevels = item_GetMinLevel(pItem) <= iEntLevel;
			estrStackCreate(&estrItemEquipLevel);
			FormatMessageKey(&estrItemEquipLevel, "Item.UI.EquipLevel.Min", STRFMT_INT("MinLevel", item_GetMinLevel(pItem)), STRFMT_END);
		}
		if (pItemDef->eType == kItemType_Component || (pItemDef->kSkillType != kSkillType_None && !pchItemSkillName))
		{
			sprintf(pchTempFmt, "Item.UI.%s", StaticDefineIntRevLookup(SkillTypeEnum, pItemDef->kSkillType));
			FormatMessageKey(&estrComponentSkill, "Item.UI.Skill", STRFMT_STRING("SkillName", TranslateMessageKey(pchTempFmt)), STRFMT_END);
			switch (pItemDef->kSkillType) {
				case kSkillType_Arms:
					pchItemTypeColor = "red";
					break;
				case kSkillType_Science:
					pchItemTypeColor = "green";
					break;
				case kSkillType_Mysticism:
					pchItemTypeColor = "purple";
					break;
			}
		}
		
		// Get the item type
		switch (pItemDef->eType)
		{
		xcase kItemType_ItemPowerRecipe:
		{
			int i;
			bool bFirst = true;
			
			for (i = 0; i < ITEM_POWER_GROUP_COUNT; i++) {
				if (pItemDef->Group & 1<<i) {
					if (bFirst) {
						bFirst = false;
					} else {
						estrAppend2(&estrItemType, "<br>");
					}
					estrAppend2(&estrItemType, StaticDefineGetTranslatedMessage(ItemPowerGroupEnum, 1<<i));
				}
			}
		} 
		xcase kItemType_Upgrade:
		{
			const char *pchText;
			sprintf(pchTempFmt, "Item.UI.Type.Upgrade.%s.%s", StaticDefineIntRevLookup(InvBagIDsEnum, eaiGet(&pItemDef->peRestrictBagIDs,0)), StaticDefineIntRevLookup(SlotTypeEnum, pItemDef->eRestrictSlotType));
			pchText = TranslateMessageKey(pchTempFmt);
			if (pchText)
				estrCopy2(&estrItemType, pchText);
		}
		xcase kItemType_Gem:
		{
			const char *pchText;
			sprintf(pchTempFmt, "Item.UI.Type.Gem.%s", StaticDefineIntRevLookup(ItemGemTypeEnum,pItemDef->eGemType));
			pchText = TranslateMessageKey(pchTempFmt);
			if (pchText)
				estrCopy2(&estrItemType, pchText);
		}
		xcase kItemType_UpgradeModifier:
		{
			const char *pchText;
			pchText = TranslateMessageKey("Item.UI.Type.Catalyst");
			if (pchText)
				estrCopy2(&estrItemType, pchText);
		}
		xcase kItemType_Component:
		{
			const char *pchText;
			pchText = StaticDefineGetTranslatedMessage(ItemTypeEnum, pItemDef->eType);
			if (pchText)
				estrCopy2(&estrItemComponentType, pchText);
		}
		xcase kItemType_Mission:
		case kItemType_MissionGrant:
		{
			// Taken care of by the MissionGrant field
		}
		xdefault:
		{
			const char *pchText;
			pchText = StaticDefineGetTranslatedMessage(ItemTypeEnum, pItemDef->eType);
			if (pchText)
				estrCopy2(&estrItemType, pchText);
		}
		}
		
		// Check the binding flags
		if (pItem && (pItem->flags & kItemFlag_Bound))
		{
			pchItemBind = TranslateMessageKey("Item.UI.Bound");
		}
		else if ((pItemDef->flags & kItemDefFlag_BindOnPickup) || (pItem && pItem->bForceBind))
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindOnPickup");
		}
		else if (pItemDef->flags & kItemDefFlag_BindOnEquip)
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindOnEquip");
		}
		else if (pItem && (pItem->flags & kItemFlag_BoundToAccount))
		{
			pchItemBind = TranslateMessageKey("Item.UI.BoundToAccount");
		}
		else if ((pItemDef->flags & kItemDefFlag_BindToAccountOnPickup) || (pItem && pItem->bForceBind))
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindToAccountOnPickup");
		}
		else if (pItemDef->flags & kItemDefFlag_BindToAccountOnEquip)
		{
			pchItemBind = TranslateMessageKey("Item.UI.BindToAccountOnEquip");
		}

		// Equip Limit Count
		if (pItemDef->pEquipLimit)
		{
			ItemEquipLimitCategoryData* pEquipLimitCategory;
			ItemEquipLimitCategory eCategory = pItemDef->pEquipLimit->eCategory;	
			iEquipLimit = pItemDef->pEquipLimit->iMaxEquipCount;
			pEquipLimitCategory = item_GetEquipLimitCategory(pItemDef->pEquipLimit->eCategory);
			if (pEquipLimitCategory)
			{
				iEquipLimitCategory = pEquipLimitCategory->iMaxItemCount;
				pchEquipLimitCategory = TranslateDisplayMessage(pEquipLimitCategory->msgDisplayName);
			}
		}
		
		if (pItemDef->eType == kItemType_Bag)
		{
			FormatMessageKey(&estrItemBagSize, "Item.UI.BagSize", STRFMT_INT("size", pItemDef->iNumBagSlots),STRFMT_END);
			iItemBagSlots = pItemDef->iNumBagSlots;
		}
		if (pchSkillColors && pchSkillColors[0])
		{
			char *pchColorList = NULL;
			char *pchKeyword = NULL;
			char *pchContext = NULL;
			const char *pchGoalKeyword = StaticDefineIntRevLookup(SkillTypeEnum, pItemDef->kSkillType);
			bool bNext = false;
			strdup_alloca(pchColorList, pchSkillColors);
			while (pchKeyword = strtok_r(pchContext ? NULL : pchColorList, " ,=\t\r\n", &pchContext))
			{
				char *pchColor = strtok_r(NULL, " ,=\t\r\n", &pchContext);
				if (!stricmp(pchKeyword, pchGoalKeyword))
				{
					pchItemSkillColor = NULL;
					strdup_alloca(pchItemSkillColor, pchColor);
					break;
				}
			}
		}
		pchItemDesc = TranslateDisplayMessage(pItemDef->descriptionMsg);
		if (pItem && (pItem->flags & kItemFlag_Algo) && pItem->pAlgoProps && eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs) > 0)
		{
			int ii;
			estrStackCreate(&estrItemAlgoDesc);
			for (ii=0; ii < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); ii++ )
			{
				ItemPowerDef *pItemPower = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[ii]->hItemPowerDef);
				if (pItemPower)
				{
					if (inv_AppendItemDescMessage(&estrItemAlgoDesc,&pItemPower->descriptionMsg,pItem))
					{
						estrAppend2(&estrItemAlgoDesc, " ");
					}
				}
			}
		}
		pchItemDescShort = TranslateDisplayMessage(pItemDef->descShortMsg);

		itemdef_DescClassesAllowed(&estrClasssesAllowed,pItemDef,pCharacter?GET_REF(pCharacter->hClass):NULL);

		Item_InnatePowerAutoDesc(entActiveOrSelectedPlayer(), pItem, &estrItemInnatePowerAutoDesc, 0);
		Item_PowerAutoDescCustom(entActiveOrSelectedPlayer(), pItem, &estrItemPowerAutoDesc, pchPowerKey, pchModKey, 0);

//		estrStackCreate(&estrInfuseDesc);
//		GetItemDescriptionInfuse(pItem,pItemDef,&estrInfuseDesc);

		estrStackCreate(&estrItemSetDesc);
		GetItemDescriptionItemSet(&estrItemSetDesc,pItemDef,pEntity,pchPowerKey,pchModKey,"Inventory_ItemInfo_ItemSetFormat",pExtract);

		estrStackCreate(&estrWarpDesc);
		GetItemDescriptionWarp(pItem, pItemDef,&estrWarpDesc);

		if (item_IsRecipe(pItemDef))
		{
			bEntLearnedRecipe = inv_ent_CountItems(pEntity, InvBagIDs_Recipe, pItemDef->pchName, pExtract) > 0;
			if (bEntLearnedRecipe)
			{
				pchItemRecipeLearned = TranslateMessageKey("Item.UI.Recipe.Learned");
				if (!pchItemRecipeLearned)
				{
					pchItemRecipeLearned = "[UNTRANSLATED: Item.UI.Recipe.Learned]";
				}
			}
		}

		if (item_IsMissionGrant(pItemDef))
		{
			pchItemQuest = TranslateMessageKey("Item.UI.QuestStart");
			if (!pchItemQuest)
			{
				pchItemQuest = "[UNTRANSLATED: Item.UI.QuestStart]";
			}
		}
		else if (item_IsMission(pItemDef))
		{
			pchItemQuest = TranslateMessageKey("Item.UI.QuestItem");
			if (!pchItemQuest)
			{
				pchItemQuest = "[UNTRANSLATED: Item.UI.QuestItem]";
			}
		}
		else if(item_IsExperienceGift(pItemDef) && pItem && (pItem->flags & kItemFlag_Full) != 0)
		{
			pchItemQuest = TranslateMessageKey("Item.UI.ItemFull");
			if (!pchItemQuest)
			{
				pchItemQuest = "[UNTRANSLATED: Item.UI.ItemFull]";
			}
		}

		if (pItem && (pItem->flags & kItemFlag_Algo) && pItem->pSpecialProps && GET_REF(pItem->pSpecialProps->hCostumeRef))
		{
			bCostumeUnlock = true;
			if (pSaved && pAccountData)
			{
				bNewCostume = !costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEntity, pEntity, REF_STRING_FROM_HANDLE(pItem->pSpecialProps->hCostumeRef));
			}
		}
		else if (eaSize(&pItemDef->ppCostumes) > 0)
		{
			int iCostume;
			bCostumeUnlock = true;
			if (pEntity->pSaved)
			{
				bNewCostume = false;
				for (iCostume = eaSize(&pItemDef->ppCostumes) - 1; iCostume >= 0; iCostume--)
				{
					if (!costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEntity, pEntity, REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[iCostume]->hCostumeRef)))
					{
						bNewCostume = true;
						break;
					}
				}
			}
		}
		if (bCostumeUnlock && (pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock))
		{
			bCostumeUnlock = false;
		}

		if (pchItemDesc && !stricmp(pchItemDesc, "."))
		{
			pchItemDesc = NULL;
		}

		if ((pItemDef->flags & kItemDefFlag_CantSell) != 0)
		{
			bSellable = false;
		}
		
		GetItemDescriptionCrafting(
			pContext, pEntity, pItemDef, pchPowerKey, pchModKey, pchCraftKey, pchItemValue, pchSkillColors, 
			&pchItemSpecialization, &bItemHasSpecialization, &bIsUsableSpecialization, &bIsItemRecipe, &bIsAlgoItemRecipe, &estrItemRecipe, &estrItemPowerRecipe, pExtract);

		FormatGameMessageKey(&estrResult, pchDescriptionKey,
			STRFMT_ITEM(pItem),
			STRFMT_ITEMDEF(pItemDef),
			STRFMT_STRING("ItemQuality", NULL_TO_EMPTY(pchItemQuality)),
			STRFMT_STRING("ItemName", NULL_TO_EMPTY(pchItemName)),
			STRFMT_INT("IsUsableLevels", bEntUsableLevels),
			STRFMT_STRING("ItemEquipLevels", NULL_TO_EMPTY(estrItemEquipLevel)),
			STRFMT_INT("ItemMinEquipLevel", MAX(SAFE_MEMBER2(pItemDef, pRestriction, iMinLevel), item_GetMinLevel(pItem))),
			STRFMT_INT("ItemMaxEquipLevel", SAFE_MEMBER2(pItemDef, pRestriction, iMaxLevel)),
			STRFMT_INT("IsUsableSkill", bEntUsableSkill),
			STRFMT_STRING("ItemSkillName", NULL_TO_EMPTY(pchItemSkillName)),
			STRFMT_INT("ItemSkillValue", iItemSkillValue),
			STRFMT_STRING("ComponentSkill", NULL_TO_EMPTY(estrComponentSkill)),
			STRFMT_STRING("ItemTypeColor", pchItemTypeColor),
			STRFMT_INT("IsUsableExpr", bEntUsableExpr),
			STRFMT_STRING("ItemUsable", !bEntUsableExpr ? NULL_TO_EMPTY(pchItemUsable) : ""),
			STRFMT_INT("IsLearnedRecipe", bEntLearnedRecipe),
			STRFMT_STRING("ItemRecipeLearned", NULL_TO_EMPTY(pchItemRecipeLearned)),
			STRFMT_STRING("ItemType", NULL_TO_EMPTY(estrItemType)),
			STRFMT_STRING("ItemComponentType", NULL_TO_EMPTY(estrItemComponentType)),
			STRFMT_STRING("ItemQuest", NULL_TO_EMPTY(pchItemQuest)),
			STRFMT_STRING("ItemBind", NULL_TO_EMPTY(pchItemBind)),
			STRFMT_STRING("ItemBagSize", NULL_TO_EMPTY(estrItemBagSize)),
			STRFMT_INT("ItemBagSlots", iItemBagSlots),
			STRFMT_STRING("ItemValue", NULL_TO_EMPTY(pchItemValue)),
			STRFMT_STRING("ItemSkillColor", NULL_TO_EMPTY(pchItemSkillColor)),
			STRFMT_STRING("ItemDesc", NULL_TO_EMPTY(pchItemDesc)),
			STRFMT_STRING("ItemDescShort", NULL_TO_EMPTY(pchItemDescShort)),
			STRFMT_STRING("ItemAlgoDesc", NULL_TO_EMPTY(estrItemAlgoDesc)),
			STRFMT_STRING("Classes", NULL_TO_EMPTY(estrClasssesAllowed)),
			STRFMT_STRING("ItemPowerAutoDesc", NULL_TO_EMPTY(estrItemPowerAutoDesc)),
			STRFMT_STRING("ItemInnatePowerAutoDesc", NULL_TO_EMPTY(estrItemInnatePowerAutoDesc)),
//			STRFMT_STRING("Infuse",NULL_TO_EMPTY(estrInfuseDesc)),
			STRFMT_STRING("ItemSet",NULL_TO_EMPTY(estrItemSetDesc)),
			STRFMT_STRING("ItemRecipe", NULL_TO_EMPTY(estrItemRecipe)),
			STRFMT_STRING("ItemPowerRecipe", NULL_TO_EMPTY(estrItemPowerRecipe)),
			STRFMT_STRING("ItemWarp", NULL_TO_EMPTY(estrWarpDesc)),
			STRFMT_INT("IsItemRecipe", bIsItemRecipe),
			STRFMT_INT("IsAlgoItemRecipe", bIsAlgoItemRecipe),
			STRFMT_INT("ItemCostumeUnlock", bCostumeUnlock),
			STRFMT_INT("ItemNewCostumeUnlock", bNewCostume && bCostumeUnlock),
			STRFMT_STRING("ItemSavedPetPowers", NULL_TO_EMPTY(estrSavedPetPowers)),
			STRFMT_STRING("ItemSpecialization", NULL_TO_EMPTY(pchItemSpecialization)),
			STRFMT_INT("ItemHasSpecialization", bItemHasSpecialization),
			STRFMT_INT("IsUsableSpecialization", bIsUsableSpecialization),
			STRFMT_INT("IsSellable", bSellable),
			STRFMT_STRING("EquipLimitCategoryName", NULL_TO_EMPTY(pchEquipLimitCategory)),
			STRFMT_INT("EquipLimitCategory", iEquipLimitCategory),
			STRFMT_INT("EquipLimit", iEquipLimit),
			STRFMT_END);

		estrDestroy(&estrComponentSkill);
		estrDestroy(&estrItemEquipLevel);
		estrDestroy(&estrItemEquipLevel);
		estrDestroy(&estrItemBagSize);
		estrDestroy(&estrItemAlgoDesc);
		estrDestroy(&estrClasssesAllowed);
		estrDestroy(&estrItemPowerAutoDesc);
		estrDestroy(&estrItemInnatePowerAutoDesc);
//		estrDestroy(&estrInfuseDesc);
		estrDestroy(&estrItemSetDesc);
		estrDestroy(&estrItemRecipe);	
		estrDestroy(&estrSavedPetPowers);
		estrDestroy(&estrItemPowerRecipe);
		estrDestroy(&estrWarpDesc);
	}

    if(SAFE_MEMBER2(pEntity,pPlayer,accessLevel) >= ACCESS_GM && pItem && pItemDef)
    {
        ItemPowerDef *item_power;

        if (!(pItem->flags & kItemFlag_Algo))
        {
            estrConcatf(&estrResult,"\n%s",pItemDef->pchName);            
        }
        else
        {	
			int i;
            estrConcatf(&estrResult,"\nitem def: %s",pItemDef->pchName);
            if (pItem->pAlgoProps)
			{
				for (i = 0; i < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); i++)
				{
					item_power = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef);
					if (item_power)
					{
						estrConcatf(&estrResult,"\npower: %s", item_power->pchName);
					}
				}    
			}
        }
    }
    
	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetLocationItemDescriptionCustom);
const char* GenExprGetLocationItemDescriptionCustom(ExprContext *pContext,
											   SA_PARAM_NN_VALID Entity* pEnt,
											   SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID ItemDef* pItemDef, 
											   const char* pchDescriptionKey)
{
	/* Information currently available:
	* Item									{Item}
	* ItemDef								{ItemDef}
	* Item Name							    {Name}
	* Item Description						{ItemDesc}
	* Map Name								{MapName}
	* X Coordinate							{LocX}
	* Y Coordinate							{LocY}
	* Z Coordinate							{LocZ}
	*/

	char* estrResult = NULL; 
	char* result = NULL;
	const char* pchDescription = "";
	const char* pchDescShort = "";
	const char* pchMapName = "";
	char* estrDev = NULL;
	ZoneMapInfo *pZMapInfo = NULL;
	SpecialItemProps* pProps = pItem ? pItem->pSpecialProps : NULL;
	

	if(pProps && pProps->pDoorKey && EMPTY_TO_NULL(pProps->pDoorKey->pchMap)) {
		pZMapInfo = zmapInfoGetByPublicName(pProps->pDoorKey->pchMap);
	}

	//ItemDef checks
	if(!pItemDef)
		pItemDef = pItem && IS_HANDLE_ACTIVE(pItem->hItem) ? GET_REF(pItem->hItem) : NULL;

	if(!pItemDef || !pProps->pDoorKey)
		return "";

	//Description
	if(IS_HANDLE_ACTIVE(pItemDef->descriptionMsg.hMessage)) {
		pchDescription = TranslateDisplayMessage(pItemDef->descriptionMsg);
	}

	//Short Description
	if(IS_HANDLE_ACTIVE(pItemDef->descShortMsg.hMessage)) {
		pchDescShort = TranslateDisplayMessage(pItemDef->descShortMsg);
	}

	//MapName
	if(pZMapInfo) {
		DisplayMessage *pMsg = zmapInfoGetDisplayNameMessage(pZMapInfo);
		pchMapName = pMsg ? TranslateDisplayMessage(*(pMsg)) : "";
	}

	//Dev
	if (g_bDisplayItemDebugInfo)
	{ 
		estrStackCreate(&estrDev);
		item_PrintDebugText(&estrDev,pEnt,pItem,pItemDef);
	}

	FormatGameMessageKey(&estrResult, pchDescriptionKey,
		STRFMT_ITEM(pItem),
		STRFMT_ITEMDEF(pItemDef),
		STRFMT_STRING("ItemName", NULL_TO_EMPTY(pItem ? item_GetName(pItem,pEnt) : TranslateDisplayMessage(pItemDef->displayNameMsg)) ),
		STRFMT_STRING("ItemDesc", NULL_TO_EMPTY(pchDescription)),
		STRFMT_STRING("ItemDescShort", NULL_TO_EMPTY(pchDescShort)),
		STRFMT_STRING("MapName", pchMapName),
		STRFMT_FLOAT("LocX", pProps->pDoorKey->vPos[0]),
		STRFMT_FLOAT("LocY", pProps->pDoorKey->vPos[1]),
		STRFMT_FLOAT("LocZ", pProps->pDoorKey->vPos[2]),
		STRFMT_STRING("Dev", estrDev),
		STRFMT_END);

	if(estrDev)
		estrDestroy(&estrDev);

	if (estrResult)
	{
		result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
		memcpy(result, estrResult, strlen(estrResult) + 1);
		estrDestroy(&estrResult);
	}
	return NULL_TO_EMPTY(result);

}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemDescription);
const char* GenExprGetItemDescription(ExprContext* pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID ItemDef *pItemDef, const char *pchItemValue, const char *pchSkillColors, ACMD_EXPR_DICT(Message) const char *pchDescriptionKey)
{
	return GenExprGetItemDescriptionCustom(pContext, pEntity, pItem, pItemDef, pchItemValue, pchSkillColors, pchDescriptionKey , "", "", "");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemSetDescriptionFromDef);
const char* GenGetItemSetDescriptionFromDef(ExprContext* pContext, 
											SA_PARAM_OP_VALID Entity *pEntity, 
											SA_PARAM_OP_VALID ItemDef *pItemSetDef,
											ACMD_EXPR_DICT(Message) const char *pchPowerKey, 
											ACMD_EXPR_DICT(Message) const char *pchModKey,
											ACMD_EXPR_DICT(Message) const char* pchItemSetFormatKey)
{
	char* pchResult = NULL;
	char* estrItemSetDesc = NULL;
	
	if (pItemSetDef && eaSize(&pItemSetDef->ppItemSetMembers) > 0)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		estrStackCreate(&estrItemSetDesc);
		GetItemDescriptionFromItemSetDef(&estrItemSetDesc,pItemSetDef,pEntity,pchPowerKey,pchModKey,pchItemSetFormatKey,pExtract);
	}
	if (estrLength(&estrItemSetDesc) > 0)
	{
		pchResult = exprContextAllocScratchMemory(pContext, strlen(estrItemSetDesc) + 1);
		memcpy(pchResult, estrItemSetDesc, strlen(estrItemSetDesc) + 1);
	}
	estrDestroy(&estrItemSetDesc);
	return NULL_TO_EMPTY(pchResult);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetItemSetDescription);
const char* GenExprGetItemSetDescription(ExprContext* pContext, 
										 SA_PARAM_OP_VALID Entity *pEntity, 
										 SA_PARAM_OP_VALID Item *pItem,
										 ACMD_EXPR_DICT(Message) const char *pchPowerKey, 
										 ACMD_EXPR_DICT(Message) const char *pchModKey,
										 ACMD_EXPR_DICT(Message) const char* pchItemSetFormatKey)
{
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItemDef)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		char* pchResult = NULL;
		char* estrItemSetDesc = NULL;

		estrStackCreate(&estrItemSetDesc);
		GetItemDescriptionItemSet(&estrItemSetDesc, pItemDef, pEntity, pchPowerKey, pchModKey, pchItemSetFormatKey, pExtract);

		if (estrLength(&estrItemSetDesc) > 0)
		{
			pchResult = exprContextAllocScratchMemory(pContext, strlen(estrItemSetDesc) + 1);
			memcpy(pchResult, estrItemSetDesc, strlen(estrItemSetDesc) + 1);
		}
		estrDestroy(&estrItemSetDesc);
		return NULL_TO_EMPTY(pchResult);
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIndexIdentify);
void gclInvExprIdentifyBagSlot(S32 eBag, S32 iSlot, const char* pchScrollDefName)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item *pItem = inv_GetItemFromBag(pEnt, eBag, iSlot, pExtract);

	if (pItem && item_IsUnidentified(pItem))
	{
		ServerCmd_itemIdentify(eBag, iSlot, pchScrollDefName);
	}
}


// Execute the best action for an inventory item.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIndexDoBestAction);
bool gclInvExprInventoryIndexBestAction(S32 eBag, S32 iSlot)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item *pItem = inv_GetItemFromBag(pEnt, eBag, iSlot, pExtract);
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBag, pExtract);
	bool done = false;

	if (pDef)
	{

		if (eaSize(&pDef->ppTrainableNodes))
		{
			ServerCmd_CreateTrainerContactFromItem(eBag, iSlot);
			return true;
		}
		
		if (item_IsDeviceUsableByPlayer(pEnt, pItem, pBag))
		{
			//Execute all useable powers
			int NumPowers = item_GetNumItemPowerDefs(pItem, true);
			int iPower;
			if(NumPowers >0)
			{
				for(iPower=NumPowers-1; iPower>=0; iPower--)
				{
					if(item_isPowerUsableByPlayer(pEnt, pItem, pBag, iPower)) {
						U32 powID;

						Power* ppow = item_GetPower(pItem, iPower);
						powID = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;

						entUsePowerID(true,powID);
						done = true;
					}
				}
			}
		}
		
		if (done) return true;
		
		ServerCmd_ItemEquip(eBag, iSlot);

		return true;
	}
	return false;
}

// Given a bag and an item, finds the first instance of the item by its itemDef in the bag, then moves it to the desination bag
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryFindItemMoveToDest);
bool gclInvExprInventoryFindItemMoveToDest(S32 eSrcBag, S32 eDstBag, SA_PARAM_NN_VALID Item *pSearchItem, S32 count)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	if (pEnt && pSearchItem)
	{
		const char *pchItemDefName = REF_STRING_FROM_HANDLE(pSearchItem->hItem);
		if (pchItemDefName)
		{
			BagIterator* pIter = inv_bag_trh_FindItemByDefName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pEnt)), eSrcBag, pchItemDefName, NULL);

			if (pIter)
			{
				if (eDstBag == InvBagIDs_None)
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

					//equip on items in inventory has behavior based on item type
					// Item *pItem = inv_GetItemFromBag( pEnt, eSrcBag, pIter->i_cur, pExtract);
					ItemDef *pDef = bagiterator_GetDef(pIter); // pItem ? GET_REF(pSearchItem->hItem) : NULL;
					eDstBag = pDef ? GetBestBagForItemDef(pEnt, pDef, count, false, pExtract) : InvBagIDs_None;

					//verify that item def is valid just to be safe
					if (eDstBag == InvBagIDs_None) 
						return false;  
				}
				

				ServerCmd_ItemMove(false, eSrcBag, pIter->i_cur, false, eDstBag, -1, count);
				bagiterator_Destroy(pIter);
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetTrainablePowerNodeCount);
S32 gclInvExprItemGetTrainablePowerNodeCount(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItemDef)
	{
		return eaSize(&pItemDef->ppTrainableNodes);
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(UseItemPowerInSlot);
void gclInvExprUseItemPowerInSlot( const char* pchPowerName, const char* pBagName, S32 iSlot)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	if ( pEnt && pEnt->pChar && 
		pBagName &&
		pBagName[0] )
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int BagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
		InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagIdx, pExtract);
		Item* pItem = inv_GetItemFromBag( pEnt, BagIdx, iSlot, pExtract);
		int ii;

		if (pItem)
		{

			int NumPowers = eaSize(&pItem->ppPowers);
			for(ii=0; ii<NumPowers; ii++)
			{
				Power* pPower = pItem->ppPowers[ii];
				PowerDef* pDef = GET_REF(pPower->hDef);

				if (!pPower)
					continue;

				if (strcmp(pchPowerName, pDef->pchName) == 0)
				{
					U32 powID;

					powID = pPower->pParentPower ? pPower->pParentPower->uiID : pPower->uiID;
					entUsePowerID(true,powID);
				}
			}	
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryMoveAllItemsFromBag);
void gclInvExprInventoryMoveAllItemsFromBag(S32 iSrcBag, S32 iDstBag)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	if (pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag* pSrcBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iSrcBag, pExtract);
		InventoryBag* pDstBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDstBag, pExtract);

		if (pSrcBag && pDstBag)
		{
			ServerCmd_InventoryMoveItemsFromBag(iSrcBag, iDstBag);
		}
	}
}

// Names of bags, delimited list by < \r\n\t,|>
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryMoveAllItemsFromBags);
void gclInvExprInventoryMoveAllItemsFromBags(SA_PARAM_NN_VALID const char *pchSourceBags, S32 iDstBag)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	if (pEnt && pchSourceBags)
	{
		InventoryBagArray bags = {0};
		char *pchBuffer = NULL;
		char *pchContext = NULL;
		const char *pchTok;
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag* pDstBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDstBag, pExtract);

		if (!pDstBag)
			return;

		// get all the bags 
		strdup_alloca(pchBuffer, pchSourceBags);

		if (pchTok = strtok_r(pchBuffer, " \r\n\t,|", &pchContext))
		{
			do {
				S32 invBagID = StaticDefineIntGetInt(InvBagIDsEnum, pchTok);
				if (invBagID >= 0)
				{
					InventoryBag* pSrcBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), invBagID, pExtract);
					if (pSrcBag)
					{
						// check to see if anything is in this bag
						FOR_EACH_IN_EARRAY(pSrcBag->ppIndexedInventorySlots, InventorySlot, pSlot)
						{
							if (pSlot && pSlot->pItem)
							{
								eaiPush(&bags.eaiBagArray, invBagID);
								break;
							}
						}
						FOR_EACH_END
					}
				}
			} while (pchTok = strtok_r(NULL, " \r\n\t,|", &pchContext));
		}

		if (eaiSize(&bags.eaiBagArray))
		{
			ServerCmd_InventoryMoveItemsFromBags(&bags, iDstBag);
		}

		eaiDestroy(&bags.eaiBagArray);
	}
}




// Get a list of numeric item values from a list of items, e.g.
// GetNumericValuesFromList(Self, "Resources XP Level")
//
// The gen then gets a model of NumericItemListNode structures, which
// has Name (human-readable name) and Value attributes.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetNumericValuesFromList);
void GenExprGetNumericValuesFromList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity* pEnt, const char* pchKeys, bool bIncludeZeroValues)
{
	// References to make sure the data we want gets sent from the server, so we
	// can get the display name of the item even if the player doesn't have it.
	static ItemDefRef **s_eaItemDefs = NULL;
	char *pchKeysCopy;
	char *apchKeys[128];
	int iAdded = 0;

	NumericItemListNode ***peaValues = ui_GenGetManagedListSafe(pGen, NumericItemListNode);

	strdup_alloca(pchKeysCopy, pchKeys);

	if (pchKeys && *pchKeys)
	{
		S32 iCount = tokenize_line(pchKeysCopy, apchKeys, NULL);
		S32 i;
		for (i = 0; i < iCount; i++)
		{
			S32 iValue = inv_GetNumericItemValue(pEnt, apchKeys[i]);
			if (bIncludeZeroValues || iValue != 0)
			{
				ItemDef *pDef = item_DefFromName(apchKeys[i]);
				NumericItemListNode *pNode = eaGetStruct(peaValues, parse_NumericItemListNode, iAdded);
				if (!pDef)
				{
					int j = -1;
					for (j = eaSize(&s_eaItemDefs) - 1; j >= 0; j--)
						if (stricmp(REF_STRING_FROM_HANDLE(s_eaItemDefs[j]->hDef), apchKeys[i]) == 0)
							break;

					if (j == -1)
					{
						ItemDefRef* pDefRef = StructCreate(parse_ItemDefRef);
						SET_HANDLE_FROM_STRING(g_hItemDict, apchKeys[i], pDefRef->hDef);
						eaPush(&s_eaItemDefs, pDefRef);
					}
				}
				estrCopy2(&pNode->pchName, inv_GetNumericItemDisplayName(pEnt, apchKeys[i]));
				pNode->iValue = iValue;
				iAdded++;
			}
		}
	}

	eaSetSizeStruct(peaValues, parse_NumericItemListNode, iAdded);
	ui_GenSetManagedListSafe(pGen, peaValues, NumericItemListNode, true);
}

#define MAX_ITEMDEF_REQUESTS 10
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RequestItemDef);
void GenExprRequestItemDef(const char* pchItemDefName)
{
	static ItemDefRef** s_eaItemRefs = NULL;
	if (pchItemDefName)
	{
		ItemDef* pItemDef = item_DefFromName(pchItemDefName);
		if (!pItemDef)
		{
			S32 i;
			for (i = eaSize(&s_eaItemRefs)-1; i >= 0; i--)
			{
				if (stricmp(REF_STRING_FROM_HANDLE(s_eaItemRefs[i]->hDef), pchItemDefName) == 0)
				{
					break;
				}
			}
			if (i < 0)
			{
				ItemDefRef* pRef;
				if (eaSize(&s_eaItemRefs) > MAX_ITEMDEF_REQUESTS)
				{
					pRef = eaRemove(&s_eaItemRefs, 0);
				}
				else
				{
					pRef = StructCreate(parse_ItemDefRef);
				}
				SET_HANDLE_FROM_STRING(g_hItemDict, pchItemDefName, pRef->hDef);
				eaPush(&s_eaItemRefs, pRef);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CountItemsInBag);
int exprCountItemsInBag(SA_PARAM_OP_VALID Entity* pEnt, S32 eBagID, const char* pchItemDef)
{
	int iResult = 0;
	if(pEnt && pchItemDef && eBagID > -1)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		iResult = inv_ent_CountItems(pEnt, eBagID, pchItemDef, pExtract);
	}
	return iResult;
}

// finds any items in the bag matching the given item's itemDef 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CountItemsInBagByItem);
int exprCountItemsInBagByItem(SA_PARAM_OP_VALID Entity* pEnt, S32 eBagID, SA_PARAM_NN_VALID Item *pItem)
{
	int iResult = 0;
	if(pEnt && pItem && eBagID > -1)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		const char *pchItemDefName = REF_STRING_FROM_HANDLE(pItem->hItem);
		if (pchItemDefName)
			iResult = inv_ent_CountItems(pEnt, eBagID, pchItemDefName, pExtract);
	}
	return iResult;
}

// Get the maximum value of the named numeric
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetNumericMaxValue);
S32 GenExprGetNumericMaxValue(const char *numericName)
{
	ItemDef *pItemDef;

	pItemDef = item_DefFromName(numericName);
	if ( pItemDef != NULL )
	{
		return pItemDef->MaxNumericValue;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemDefGetCategoryDisplayNamesByPrefix);
const char* GenExprItemDefGetCategoryDisplayNamesByPrefix(SA_PARAM_OP_VALID ItemDef* pItemDef, const char* pchPrefix)
{
	static char* s_estrItemCategories = NULL;
	estrClear(&s_estrItemCategories);
	Item_GetItemCategoriesString(pItemDef, pchPrefix, &s_estrItemCategories);
	return s_estrItemCategories;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclReceiveRewardPackData(ItemRewardPackRequestData* pRequest)
{
	Entity* pEnt = entActiveOrSelectedPlayer();
	if (pEnt && pRequest && pRequest->pRewards)
	{
		ItemRewardPackRequestData* pData = StructCreate(parse_ItemRewardPackRequestData);
		COPY_HANDLE(pData->hRewardPackItem, pRequest->hRewardPackItem);
		pData->pRewards = StructCreate(parse_InvRewardRequest);
		inv_FillRewardRequestClient(pEnt, pRequest->pRewards, pData->pRewards, false);
		pData->ePackResultQuality = pRequest->ePackResultQuality;
		eaPush(&s_eaRewardPackData, pData);
	}
}

static int compareInventorySlotByQuality(const InventorySlot **ppFirst, const InventorySlot **ppSecond)
{
	const InventorySlot *pFirst = (ppFirst ? *ppFirst : NULL);
	const InventorySlot *pSecond = (ppSecond ? *ppSecond : NULL);

	if(pFirst && pSecond)
	{
		if( pFirst->pItem && pSecond->pItem)
		{
			ItemDef *pFirstDef = GET_REF(pFirst->pItem->hItem);
			ItemDef *pSecondDef = GET_REF(pSecond->pItem->hItem);

			if(pFirstDef && pSecondDef)
			{
				int result = pSecondDef->Quality - pFirstDef->Quality;

				if (result == 0)
				{
					return stricmp(pFirstDef->pchName, pSecondDef->pchName);
				}
				else
				{
					return result;
				}
			}
		}

		return stricmp(pFirst->pchName, pSecond->pchName);
	}

	return 0;
}


// If an item is a reward box and requires a lockbox key, return the name of the key.
//   Returns "" if the item is not a reward box or requires no key
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetLockboxRequiredKeyName");
const char* gclItemRewardPackGetRequiredLockboxKeyName(ExprContext* pContext, SA_PARAM_OP_VALID Item* pBoxItem)
{
	ItemDef *pBoxDef = pBoxItem ? GET_REF(pBoxItem->hItem) : NULL;
	
	if (pBoxDef!=NULL && pBoxDef->eType==kItemType_RewardPack)
	{
		if (pBoxDef->pRewardPackInfo)
		{
			ItemDef* pRequiredItemDef = GET_REF(pBoxDef->pRewardPackInfo->hRequiredItem);
			if (pRequiredItemDef && pRequiredItemDef->eType==kItemType_LockboxKey)
			{
				return(REF_STRING_FROM_HANDLE(pBoxDef->pRewardPackInfo->hRequiredItem));
			}
		}
	}
	return("");
}

// An Item (Lockbox Key) can open another item (rewardpack)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemCanOpenItem);
bool gclItemCanOpenItem(SA_PARAM_OP_VALID Item *pBoxItem, ACMD_EXPR_RES_DICT(ItemDef) const char *pcKeyItemName)
{
	ItemDef *pBoxDef = pBoxItem ? GET_REF(pBoxItem->hItem) : NULL;
	ItemDef *pKeyDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pcKeyItemName);

	if (pKeyDef==NULL || pBoxDef==NULL || pKeyDef->eType!=kItemType_LockboxKey || pBoxDef->eType!=kItemType_RewardPack)
	{
		return(false);
	}
	
	if (pBoxDef->pRewardPackInfo)
	{
		ItemDef* pRequiredItemDef = GET_REF(pBoxDef->pRewardPackInfo->hRequiredItem);

		if (pRequiredItemDef==pKeyDef)
		{
			return(true);
		}
	}
	return(false);
}


static void gclFillRewardPackData(ItemRewardPackRequestData* pRequestData, 
								  RewardPackDisplayData*** peaData, 
								  S32* piCount, 
								  bool bShowRewardPackHeaders,
								  bool bSortByQuality)
{
	ItemDef* pRewardPackDef = GET_REF(pRequestData->hRewardPackItem);
	InvRewardRequest* pRewards = pRequestData->pRewards;

	if (pRewards && eaSize(&pRewards->eaRewards))
	{
		RewardPackDisplayData* pData;
		S32 i;

		if(bSortByQuality)
			eaQSort(pRewards->eaRewards, compareInventorySlotByQuality);

		if (bShowRewardPackHeaders)
		{
			pData = eaGetStruct(peaData, parse_RewardPackDisplayData, (*piCount)++);
			pData->pchItemDisplayName = pRewardPackDef ? TranslateDisplayMessage(pRewardPackDef->displayNameMsg) : NULL;
			pData->pSlot = NULL;
		}
		for (i = 0; i < eaSize(&pRewards->eaRewards); i++)
		{
			pData = eaGetStruct(peaData, parse_RewardPackDisplayData, (*piCount)++);
			pData->pchItemDisplayName = NULL;
			pData->pSlot = pRewards->eaRewards[i];
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRewardPackDisplayData);
void GenExprGetRewardPackDisplayData(SA_PARAM_NN_VALID UIGen* pGen, S32 iRewardPackIndex)
{
	RewardPackDisplayData*** peaData = ui_GenGetManagedListSafe(pGen, RewardPackDisplayData);
	S32 iCount = 0;

	if (iRewardPackIndex >= 0)
	{
		ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
		if (pRequestData)
		{
			gclFillRewardPackData(pRequestData, peaData, &iCount, false, false);
		}
	}
	else
	{
		for (iRewardPackIndex = 0; iRewardPackIndex < eaSize(&s_eaRewardPackData); iRewardPackIndex++)
		{
			ItemRewardPackRequestData* pRequestData = s_eaRewardPackData[iRewardPackIndex];
			gclFillRewardPackData(pRequestData, peaData, &iCount, true, false);
		}
	}

	eaSetSizeStruct(peaData, parse_RewardPackDisplayData, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, RewardPackDisplayData, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRewardPackDisplayDataSorted);
void GenExprGetRewardPackDisplayDataSorted(SA_PARAM_NN_VALID UIGen* pGen, S32 iRewardPackIndex)
{
	RewardPackDisplayData*** peaData = ui_GenGetManagedListSafe(pGen, RewardPackDisplayData);
	S32 iCount = 0;

	if (iRewardPackIndex >= 0)
	{
		ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
		if (pRequestData)
		{
			gclFillRewardPackData(pRequestData, peaData, &iCount, false, true);
		}
	}
	else
	{
		for (iRewardPackIndex = 0; iRewardPackIndex < eaSize(&s_eaRewardPackData); iRewardPackIndex++)
		{
			ItemRewardPackRequestData* pRequestData = s_eaRewardPackData[iRewardPackIndex];
			gclFillRewardPackData(pRequestData, peaData, &iCount, true, true);
		}
	}

	eaSetSizeStruct(peaData, parse_RewardPackDisplayData, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, RewardPackDisplayData, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRewardPackListCountAndRemoveItem);
S32 GenExprRewardPackListCountAndRemoveItem(SA_PARAM_NN_VALID UIGen* pGen, const char* pchName)
{
	RewardPackDisplayData*** peaData = ui_GenGetManagedListSafe(pGen, RewardPackDisplayData);
	S32 iCount = eaSize(peaData);
	S32 i;
	S32 iRet = 0;
	for (i = iCount-1; i >= 0; i--)
	{
		if ((*peaData)[i]->pSlot && (*peaData)[i]->pSlot->pItem && REF_STRING_FROM_HANDLE((*peaData)[i]->pSlot->pItem->hItem) == allocFindString(pchName))
		{
			iRet += (*peaData)[i]->pSlot->pItem->count;
			StructDestroy(parse_RewardPackDisplayData, (*peaData)[i]);
			eaRemove(peaData, i);
			iCount--;
		}
	}

	eaSetSizeStruct(peaData, parse_RewardPackDisplayData, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, RewardPackDisplayData, true);
	return iRet;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetRewardPackDisplayName);
const char* GenExprGetRewardPackDisplayName(S32 iRewardPackIndex)
{
	ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
	if (pRequestData)
	{
		ItemDef* pRewardPackDef = GET_REF(pRequestData->hRewardPackItem);
		if (pRewardPackDef)
		{
			return TranslateDisplayMessage(pRewardPackDef->displayNameMsg);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetRewardPackOverallQuality);
S32 GenExprGetRewardPackOverallQuality(S32 iRewardPackIndex)
{
	ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
	if (pRequestData)
	{
		return pRequestData->ePackResultQuality;
	}
	return kItemQuality_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetRewardPackDefName);
const char* GenExprGetRewardPackDefName(S32 iRewardPackIndex)
{
	ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
	if (pRequestData)
	{
		ItemDef* pRewardPackDef = GET_REF(pRequestData->hRewardPackItem);
		if (pRewardPackDef)
		{
			return pRewardPackDef->pchName;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetRewardPackRequiredItemName);
const char* GenExprGetRewardPackRequiredItemName(S32 iRewardPackIndex)
{
	ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
	if (pRequestData)
	{
		ItemDef* pRewardPackDef = GET_REF(pRequestData->hRewardPackItem);
		if (pRewardPackDef && pRewardPackDef->pRewardPackInfo)
		{
			ItemDef *pDef = GET_REF(pRewardPackDef->pRewardPackInfo->hRequiredItem);

			if(pDef)
				return pDef->pchName;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetRewardPackRequiredProductName);
const char* GenExprGetRewardPackRequiredProductName(S32 iRewardPackIndex)
{
	ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
	if (pRequestData)
	{
		ItemDef* pRewardPackDef = GET_REF(pRequestData->hRewardPackItem);
		if (pRewardPackDef && pRewardPackDef->pRewardPackInfo)
		{
			return pRewardPackDef->pRewardPackInfo->pchRequiredItemProduct;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetRewardPackDisplayCount);
S32 GenExprGetRewardPackDisplayCount(void)
{
	return eaSize(&s_eaRewardPackData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ClearRewardPackDisplayData);
void GenExprClearRewardPackDisplayData(void)
{
	eaDestroyStruct(&s_eaRewardPackData, parse_ItemRewardPackRequestData);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RewardPackHasCategory);
bool GenExprRewardPackHasCategory(S32 iRewardPackIndex, const char *pchCategories)
{
	ItemRewardPackRequestData* pRequestData = eaGet(&s_eaRewardPackData, iRewardPackIndex);
	if (pRequestData)
	{
		ItemDef* pRewardPackDef = GET_REF(pRequestData->hRewardPackItem);
		if (pRewardPackDef)
		{
			char* pchBuffer;
			char* pchToken;
			char* pchContext = NULL;
			strdup_alloca(pchBuffer, pchCategories);
			for (pchToken = strtok_r(pchBuffer, " \r\n\t,%|", &pchContext);
				pchToken != NULL;
				pchToken = strtok_r(NULL, " \r\n\t,%|", &pchContext))
			{
				bool bRequire = false, bExclude = false, bNot = false, bExact = false;
				bool bMatch = false;
				S32 i;

				while (strchr("!+-=", pchToken[0]) != NULL)
				{
					switch (pchToken[0])
					{
					case '+': bRequire = true; break;
					case '-': bExclude = true; break;
					case '!': bNot = true; break;
					case '=': bExact = true; break;
					}
					++pchToken;
				}

				for (i = eaiSize(&pRewardPackDef->peCategories) - 1; i >= 0; --i)
				{
					const char *pchName = StaticDefineIntRevLookup(ItemCategoryEnum, pRewardPackDef->peCategories[i]);
					if (pchName)
					{
						ANALYSIS_ASSUME(pchName != NULL);
						if (bExact ? stricmp(pchName, pchToken) == 0 : strstri(pchName, pchToken) != NULL)
						{
							bMatch = true;
							break;
						}
					}
				}

				if (bNot)
					bMatch = !bMatch;

				if (bRequire && !bMatch || bExclude && bMatch)
					return false;
				if (bMatch)
					return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagAllowsDiscard);
bool GenExprBagAllowsDiscard(SA_PARAM_NN_STR const char* pBag)
{
	int iBagID = StaticDefineIntGetInt(InvBagIDsEnum, pBag);
	if (inv_IsGuildBag(iBagID))
	{
		return(false);
	}
	return(true);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanRemoveItem);
bool GenExprCanRemoveItem(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	return pDef && (pDef->flags & kItemDefFlag_CantDiscard) == 0 && item_CanRemoveItem(pItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenApplyDyeToItem);
void exprApplyDyeToItem(SA_PARAM_OP_VALID Item* pItem, SA_PARAM_OP_VALID Item* pDye, int iChannel)
{
	if (pItem && pDye)
		ServerCmd_ApplyDyeToItem(pItem->id, pDye->id, iChannel); 
}

static bool InvUI_ItemCanLevelUpPowerFactorOnItem(ItemDef* pLevelUpDef, Item* pTargetItem)
{
	// We must have a valid levelUpDef of the proper type.
	// We must be a positive S8.
	// We can only level up one level at a time so must target something one level lower than the source. (This covers for rental mounts which are Rank0. There are no Rank 1 books at the moment)
	
	if (pTargetItem!=NULL && pLevelUpDef!=NULL)
	{
		ItemDef* pTargetItemDef = GET_REF(pTargetItem->hItem);
		int iTargetPowerFactor = item_GetPowerFactor(pTargetItem);

		if (pTargetItemDef!=NULL && 
			pLevelUpDef->eType == kItemType_PowerFactorLevelUp &&
			iTargetPowerFactor == pLevelUpDef->iPowerFactor-1 &&
			pLevelUpDef->iPowerFactor <= 127 &&
			itemdef_HasItemCategory(pTargetItemDef, pLevelUpDef->peCategories))
		{
			return(true);
		}
	}
	return(false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemLevelUpPowerFactor);
void exprItemLevelUpPowerFactor(SA_PARAM_OP_VALID Item* pItem, const char* pchLevelUpItemDefName)
{
	ItemDef* pLevelUpDef;
	Entity* pEnt = entActiveOrSelectedPlayer();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	pLevelUpDef = RefSystem_ReferentFromString(g_hItemDict, pchLevelUpItemDefName);

	if (InvUI_ItemCanLevelUpPowerFactorOnItem(pLevelUpDef, pItem))
	{
		ServerCmd_itemLevelUpPowerFactor(pItem->id, pchLevelUpItemDefName);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemCanLevelUpPowerFactor);
bool exprItemCanLevelUpPowerFactor(SA_PARAM_OP_VALID Item* pItem, const char* pchLevelUpItemDefName)
{
	ItemDef* pLevelUpDef;
	Entity* pEnt = entActiveOrSelectedPlayer();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	pLevelUpDef = RefSystem_ReferentFromString(g_hItemDict, pchLevelUpItemDefName);
	if (InvUI_ItemCanLevelUpPowerFactorOnItem(pLevelUpDef, pItem))
	{
		return(true);
	}
	return(false);
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenTransmutateItem);
void exprTransmutateItem(SA_PARAM_OP_VALID Item* pMainItem, SA_PARAM_OP_VALID Item* pTransmutateToItem)
{
	if (pMainItem && pTransmutateToItem)
	{
		ServerCmd_TransmutateItem(pMainItem->id, pTransmutateToItem->id); 
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDiscardItemTransmutation);
void exprDiscardItemTransmutation(SA_PARAM_OP_VALID Item* pItem)
{
	if (pItem)
	{
		ServerCmd_DiscardItemTransmutation(pItem->id); 
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemHasTransmutation);
bool exprItemHasTransmutation(SA_PARAM_OP_VALID Item* pItem)
{
	return pItem && 
		pItem->pSpecialProps && 
		pItem->pSpecialProps->pTransmutationProps &&
		IS_HANDLE_ACTIVE(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemDefCanTransmutateTo);
bool exprItemDefCanTransmutateTo(SA_PARAM_OP_VALID ItemDef* pMainItemDef, SA_PARAM_OP_VALID ItemDef* pTransmutateToItemDef)
{
	return pMainItemDef && pTransmutateToItemDef && item_CanTransMutateTo(pMainItemDef, pTransmutateToItemDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemCanTransmutateTo);
bool exprItemCanTransmutateTo(SA_PARAM_OP_VALID Item* pMainItem, SA_PARAM_OP_VALID Item* pTransmutateToItem)
{
	ItemDef *pMainItemDef = pMainItem ? GET_REF(pMainItem->hItem) : NULL;
	ItemDef *pTransmutateToItemDef = pTransmutateToItem ? GET_REF(pTransmutateToItem->hItem) : NULL;

	// The item being transmutated into cannot have a transmutation.
	// No longer true. The transmuted appearance will be copied over, rather than the original appearance.
	//if (pTransmutateToItem && 
	//	exprItemHasTransmutation(pTransmutateToItem))
	//{
	//	return false;
	//}
	if(!pTransmutateToItem || item_IsUnidentified(pTransmutateToItem) || !pMainItem || item_IsUnidentified(pMainItem))
	{
		//no transmuting unidentified stuff!
		return false;
	}

	return pMainItemDef && pTransmutateToItemDef && exprItemDefCanTransmutateTo(pMainItemDef, pTransmutateToItemDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemCanTransmutate);
bool exprItemCanTransmutate(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	return item_CanTransMutate(pItemDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemCanBeDyed);
bool exprItemCanBeDyed(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	return item_CanDye(pItemDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemHasWarpPowers);
bool ItemExprHasWarpPowers(SA_PARAM_OP_VALID Item* pItem)
{
	if (pItem)
	{
		int iPower, iNumPowers = item_GetNumItemPowerDefs(pItem, true);

		for (iPower=0; iPower<iNumPowers; iPower++)
		{
			PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
			if (pPowerDef && pPowerDef->bHasWarpAttrib)
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemGetNumSlottedGems);
bool ItemExprGetNumSlottedGems(SA_PARAM_OP_VALID Item* pItem)
{
	int count = 0;
	if (pItem && pItem->pSpecialProps)
	{
		int i;

		for (i = 0; i < eaSize(&pItem->pSpecialProps->ppItemGemSlots); i++)
		{
			count += !!IS_HANDLE_ACTIVE(pItem->pSpecialProps->ppItemGemSlots[i]->hSlottedItem);
		}
	}
	return count;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenUseNextItemOfName);
bool exprUseNextItemOfName(SA_PARAM_OP_VALID Entity *pEntity, S32 eBagID, const char *pchName)
{
	if(pEntity && pchName)
	{
		BagIterator *pIter = NULL;
		if (eBagID == InvBagIDs_Inventory)//Search overflow + player bags too if we're looking in "inventory"
			pIter = inv_bag_FindItem(pEntity, eBagID, NULL, kFindItem_ByName, pchName, true, false);
		else
			pIter = inv_bag_FindItem(pEntity, eBagID, NULL, kFindItem_ByName, pchName, false, false);
		if (pIter)
		{
			return gclInvExprInventoryIndexBestAction(bagiterator_GetCurrentBagID(pIter), bagiterator_GetSlotID(pIter));
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenHasItemOfName);
bool exprHasItemOfName(const char *pchName)
{
	return inv_ent_AllBagsCountItemsAtLeast(entActiveOrSelectedPlayer(), pchName, 1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FindInventoryKeyByItemName);
const char* exprFindInventoryKeyByItemName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 eBagID, SA_PARAM_OP_STR const char *pchItemName)
{
	if(pEntity && pchItemName)
	{
		BagIterator *pIter = NULL;
		if (eBagID == InvBagIDs_Inventory)//Search overflow + player bags too if we're looking in "inventory"
			pIter = inv_bag_FindItem(pEntity, eBagID, NULL, kFindItem_ByName, pchItemName, true, false);
		else
			pIter = inv_bag_FindItem(pEntity, eBagID, NULL, kFindItem_ByName, pchItemName, false, false);
		if (pIter)
		{
			UIInventoryKey Key = {0};
			if( gclInventoryMakeSlotKey(pEntity, CONTAINER_RECONST(InventoryBag, bagiterator_GetCurrentBag(pIter)), bagiterator_GetSlot(pIter), &Key) )
				return gclInventoryMakeKeyString(pContext, &Key);
		}
	}
	return "";
}

#include "Inventory_uiexpr_c_ast.c"
