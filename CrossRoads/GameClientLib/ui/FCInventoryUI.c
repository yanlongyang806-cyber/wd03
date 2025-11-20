#include "expression.h"
#include "estring.h"
#include "earray.h"
#include "gclUIGen.h"
#include "itemCommon.h"
#include "powers.h"
#include "PowersAutoDesc.h"
#include "Tray.h"
#include "UITray.h"
#include "Character.h"
#include "character_combat.h"
#include "GameAccountDataCommon.h"
#include "Guild.h"
#include "gclEntity.h"
#include "EntitySavedData.h"
#include "tradeCommon.h"
#include "Player.h"
#include "StringFormat.h"
#include "StringCache.h"
#include "UIGen.h"
#include "GameStringFormat.h"
#include "FCInventoryUI_c_ast.h"
#include "mission_common.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "CombatEval.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "entCritter.h"
#include "GraphicsLib.h"
#include "StringUtil.h"
#include "FCInventoryUI.h"
#include "GameClientLib.h"
#include "ItemAssignments.h"
#include "ItemAssignmentsUI.h"
#include "species_common.h"
#include "EntityLib.h"
#include "EntityIterator.h"
#include "cmdClient.h"
#include "AuctionLot.h"
#include "storeCommon.h"
#include "LootUI.h"
#include "UIEnums.h"
#include "ExperimentUI.h"
#include "Inventory_uiexpr.h"
#include "SuperCritterPet.h"
#include "ItemAssignmentsUICommon.h"
#include "ItemAssignmentsUICommon_h_ast.h"

#include "AutoGen/UIEnums_h_ast.h"
#include "AutoGen/FCInventoryUI_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"
#include "Autogen/PowersAutoDesc_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_STRUCT;
typedef struct InvLoadItemHandle {
	REF_TO(ItemDef) handle;
} InvLoadItemHandle;

typedef struct UIInventoryReverseMapEntry
{
	U32 uLastUpdateTime;
	Entity *pEnt;
	InventoryBag *pBag;
	InventoryBagLite *pBagLite;
	InventorySlot *pSlot;
	InventorySlotLite *pSlotLite;
	InventorySlot **ppOwnedSlots;
} UIInventoryReverseMapEntry;

#define UIInvFilterFlag_NotOnItemAssignment		0x004000
#define UIInvFilterFlag_EmptySlots				0x008000
#define UIInvFilterFlag_DoNotSort				0x010000
#define UIInvFilterFlag_RequireAllCategories	0x020000
#define UIInvFilterFlag_LockedSlots				0x040000
#define UIInvFilterFlag_OnItemAssignment		0x080000
#define UIInvFilterFlag_DupesIncreaseCount		0x100000

static InvLoadItemHandle **s_eaInvLoadItemHandles;
static StashTable s_stInventoryReverseMap;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// Fake empty inventory slots; the way slot-indexed bags work, they don't
// extend past the last item. So we need to do it for them.
InventorySlot **g_eaEmptySlots;

// Get the define name of an item category. It operates based on a lookup table
// instead of a linear search through the StaticDefineInt.
static const char *gclInvGetCategoryName(ItemCategory eCategory)
{
	static const char **s_eaNames;

	if (!s_eaNames)
	{
		const char **eaKeys = NULL;
		S32 *eaiValues = NULL;
		S32 i, iNames = 0;
		DefineFillAllKeysAndValues(ItemCategoryEnum, &eaKeys, &eaiValues);
		for (i = 0; i < eaiSize(&eaiValues); i++)
			MAX1(iNames, eaiValues[i]);
		eaSetSize(&s_eaNames, iNames);
		for (i = 0; i < eaSize(&eaKeys); i++)
			eaSet(&s_eaNames, eaKeys[i], eaiValues[i]);
		eaDestroy(&eaKeys);
		eaiDestroy(&eaiValues);
	}

	return eaGet(&s_eaNames, eCategory);
}

ItemDef *gclInvGetItemDef(const char *pchItemName)
{
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pchItemName);
	if (!pItemDef)
	{
		int i;
		for (i = eaSize(&s_eaInvLoadItemHandles)-1; i >= 0; --i)
		{
			ItemDef *d = GET_REF(s_eaInvLoadItemHandles[i]->handle);
			if (d)
			{
				if (!stricmp(d->pchName,pchItemName))
				{
					break;
				}
			}
		}
		if (i < 0)
		{
			//Item not available on client; Make reference to it to get it
			InvLoadItemHandle *h = StructCreate(parse_InvLoadItemHandle);
			if (h)
			{
				SET_HANDLE_FROM_STRING(g_hItemDict, pchItemName, h->handle);
				eaPush(&s_eaInvLoadItemHandles, h);
			}
		}
	}
	return pItemDef;
}

AUTO_STARTUP(InventoryUI) ASTRT_DEPS(InventoryBags UsageRestrictionCategories ItemTags ItemQualities);
void FCInventoryUI_Startup(void)
{
	if(!gbNoGraphics)
	{
		ui_GenInitStaticDefineVars(ItemTypeEnum, "ItemType");
		ui_GenInitStaticDefineVars(InvBagIDsEnum, "BagID");
		ui_GenInitStaticDefineVars(InvBagFlagEnum, "BagFlag");
		ui_GenInitStaticDefineVars(ItemFlagEnum, "ItemFlag");
		ui_GenInitStaticDefineVars(ItemDefFlagEnum, "ItemDefFlag");
		ui_GenInitStaticDefineVars(SlotTypeEnum, "UpgradeTypeType");
		ui_GenInitStaticDefineVars(LootModeEnum, "LootMode_");
		ui_GenInitStaticDefineVars(ItemCategoryEnum, "ItemCategory");
		ui_GenInitStaticDefineVars(UsageRestrictionCategoryEnum, "UsageRestrictionCategory_");
		ui_GenInitStaticDefineVars(ItemQualityEnum, "ItemQuality");
		ui_GenInitIntVar("CategorizedInventoryHeaders", UIItemCategoryFlag_CategorizedInventoryHeaders);
		ui_GenInitIntVar("NotOnItemAssignment", UIInvFilterFlag_NotOnItemAssignment);
		ui_GenInitIntVar("OnItemAssignment", UIInvFilterFlag_OnItemAssignment);
		ui_GenInitIntVar("DupesIncreaseCount", UIInvFilterFlag_DupesIncreaseCount);
		ui_GenInitIntVar("InventoryEmptySlots", UIInvFilterFlag_EmptySlots);
		ui_GenInitIntVar("InventoryDoNotSort", UIInvFilterFlag_DoNotSort);
		ui_GenInitIntVar("InventoryRequireAllCategories", UIInvFilterFlag_RequireAllCategories);
		ui_GenInitIntVar("InventoryLockedSlots", UIInvFilterFlag_LockedSlots);
		
	}
}

void gclInventoryOncePerFrame(void)
{
	static void **s_eaRemoved;
	StashTableIterator it;
	StashElement pElem;

	if (!s_stInventoryReverseMap)
		return;

	stashGetIterator(s_stInventoryReverseMap, &it);

	while (stashGetNextElement(&it, &pElem))
	{
		UIInventoryReverseMapEntry *pEntry = stashElementGetPointer(pElem);
		if (!pEntry || pEntry->uLastUpdateTime != gGCLState.totalElapsedTimeMs)
			eaPush(&s_eaRemoved, stashElementGetKey(pElem));
	}

	if (s_eaRemoved)
	{
		while (eaSize(&s_eaRemoved) > 0)
		{
			UIInventoryReverseMapEntry *pEntry;
			if (stashRemovePointer(s_stInventoryReverseMap, eaPop(&s_eaRemoved), &pEntry) && pEntry)
			{
				eaDestroyStruct(&pEntry->ppOwnedSlots, parse_InventorySlot);
				free(pEntry);
			}
		}
		eaDestroy(&s_eaRemoved);
	}
}

Entity *gclInventoryGetBagEntity(InventoryBag *pBag)
{
	UIInventoryReverseMapEntry *pEntry = NULL;
	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pBag, &pEntry))
		pEntry = NULL;
	if (pEntry && pEntry->uLastUpdateTime == gGCLState.totalElapsedTimeMs && pEntry->pBag == pBag)
		return pEntry->pEnt;
	return NULL;
}

void gclInventoryUpdateBag(Entity *pEnt, InventoryBag *pBag)
{
	UIInventoryReverseMapEntry *pEntry = NULL;

	if (!pBag)
		return;

	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pBag, &pEntry))
		pEntry = NULL;

	if (!pEntry)
	{
		pEntry = calloc(1, sizeof(UIInventoryReverseMapEntry));
		if (!s_stInventoryReverseMap)
			s_stInventoryReverseMap = stashTableCreateAddress(256);
		stashAddressAddPointer(s_stInventoryReverseMap, pBag, pEntry, true);
	}

	pEntry->uLastUpdateTime = gGCLState.totalElapsedTimeMs;
	pEntry->pEnt = pEnt;
	pEntry->pBag = pBag;
	pEntry->pBagLite = NULL;
}

Entity *gclInventoryGetBagLiteEntity(InventoryBagLite *pBagLite)
{
	UIInventoryReverseMapEntry *pEntry = NULL;
	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pBagLite, &pEntry))
		pEntry = NULL;
	if (pEntry && pEntry->uLastUpdateTime == gGCLState.totalElapsedTimeMs && pEntry->pBagLite == pBagLite)
		return pEntry->pEnt;
	return NULL;
}

void gclInventoryUpdateBagLite(Entity *pEnt, InventoryBagLite *pBagLite)
{
	UIInventoryReverseMapEntry *pEntry = NULL;

	if (!pBagLite)
		return;

	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pBagLite, &pEntry))
		pEntry = NULL;

	if (!pEntry)
	{
		pEntry = calloc(1, sizeof(UIInventoryReverseMapEntry));
		if (!s_stInventoryReverseMap)
			s_stInventoryReverseMap = stashTableCreateAddress(256);
		stashAddressAddPointer(s_stInventoryReverseMap, pBagLite, pEntry, true);
	}

	pEntry->uLastUpdateTime = gGCLState.totalElapsedTimeMs;
	pEntry->pEnt = pEnt;
	pEntry->pBag = NULL;
	pEntry->pBagLite = pBagLite;
}

InventoryBag *gclInventoryGetSlotBag(InventorySlot *pSlot, Entity **ppEnt)
{
	UIInventoryReverseMapEntry *pEntry = NULL;
	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pSlot, &pEntry))
		pEntry = NULL;
	if (pEntry && pEntry->uLastUpdateTime == gGCLState.totalElapsedTimeMs && pEntry->pSlot == pSlot)
	{
		if (!pEntry->pEnt)
			pEntry->pEnt = gclInventoryGetBagEntity(pEntry->pBag);
		if (ppEnt)
			*ppEnt = pEntry->pEnt;
		return pEntry->pBag;
	}
	return NULL;
}

InventorySlot *gclInventoryUpdateSlot(Entity *pEnt, InventoryBag *pBag, InventorySlot *pSlot)
{
	UIInventoryReverseMapEntry *pEntry = NULL;

	if (!pSlot)
		return pSlot;

	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pSlot, &pEntry))
		pEntry = NULL;

	if (!pEntry)
	{
		pEntry = calloc(1, sizeof(UIInventoryReverseMapEntry));
		if (!s_stInventoryReverseMap)
			s_stInventoryReverseMap = stashTableCreateAddress(256);
		stashAddressAddPointer(s_stInventoryReverseMap, pSlot, pEntry, true);
	}

	if (pEnt)
		gclInventoryUpdateBag(pEnt, pBag);
	else
		pEnt = gclInventoryGetBagEntity(pBag);

	pEntry->uLastUpdateTime = gGCLState.totalElapsedTimeMs;
	pEntry->pEnt = pEnt;
	pEntry->pBag = pBag;
	pEntry->pBagLite = NULL;
	pEntry->pSlot = pSlot;
	pEntry->pSlotLite = NULL;
	return pSlot;
}

InventoryBagLite *gclInventoryGetSlotLiteBag(InventorySlotLite *pSlotLite, Entity **ppEnt)
{
	UIInventoryReverseMapEntry *pEntry = NULL;
	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pSlotLite, &pEntry))
		pEntry = NULL;
	if (pEntry && pEntry->uLastUpdateTime == gGCLState.totalElapsedTimeMs && pEntry->pSlotLite == pSlotLite)
	{
		if (!pEntry->pEnt)
			pEntry->pEnt = gclInventoryGetBagLiteEntity(pEntry->pBagLite);
		if (ppEnt)
			*ppEnt = pEntry->pEnt;
		return pEntry->pBagLite;
	}
	return NULL;
}

InventorySlotLite *gclInventoryUpdateSlotLite(Entity *pEnt, InventoryBagLite *pBagLite, InventorySlotLite *pSlotLite)
{
	UIInventoryReverseMapEntry *pEntry = NULL;

	if (!pSlotLite)
		return pSlotLite;

	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pSlotLite, &pEntry))
		pEntry = NULL;

	if (!pEntry)
	{
		pEntry = calloc(1, sizeof(UIInventoryReverseMapEntry));
		if (!s_stInventoryReverseMap)
			s_stInventoryReverseMap = stashTableCreateAddress(256);
		stashAddressAddPointer(s_stInventoryReverseMap, pSlotLite, pEntry, true);
	}

	if (pEnt)
		gclInventoryUpdateBagLite(pEnt, pBagLite);
	else
		pEnt = gclInventoryGetBagLiteEntity(pBagLite);

	pEntry->uLastUpdateTime = gGCLState.totalElapsedTimeMs;
	pEntry->pEnt = pEnt;
	pEntry->pBag = NULL;
	pEntry->pBagLite = pBagLite;
	pEntry->pSlot = NULL;
	pEntry->pSlotLite = pSlotLite;
	return pSlotLite;
}

InventorySlot *gclInventoryUpdateNullSlot(Entity *pEnt, InventoryBag *pBag, S32 iIndex)
{
	// MaxValidIndex is just to prevent bad data from completely
	// blowing up the cache (since NULL slots are stored on a
	// per-InventoryBag basis).
	const S32 c_iMaxValidIndex = 10240;
	static InventorySlot *s_pInvalid;
	static InventorySlot **s_eaBagLess;
	UIInventoryReverseMapEntry *pBagEntry = NULL;
	InventorySlot *pSlot = NULL;

	if (!pBag)
	{
		pSlot = eaGet(&s_eaBagLess, iIndex);
		if (!pSlot)
		{
			if (iIndex >= 0 && iIndex < c_iMaxValidIndex)
			{
				pSlot = CONTAINER_RECONST(InventorySlot, inv_InventorySlotCreate(iIndex));
				eaSet(&s_eaBagLess, pSlot, iIndex);
			}
			else
			{
				if (!s_pInvalid)
					s_pInvalid = CONTAINER_RECONST(InventorySlot, inv_InventorySlotCreate(-1));
				pSlot = s_pInvalid;
			}
		}
		return pSlot;
	}

	if (s_stInventoryReverseMap && !stashAddressFindPointer(s_stInventoryReverseMap, pBag, &pBagEntry))
		pBagEntry = NULL;

	if (!pBagEntry)
	{
		pBagEntry = calloc(1, sizeof(UIInventoryReverseMapEntry));
		if (!s_stInventoryReverseMap)
			s_stInventoryReverseMap = stashTableCreateAddress(256);
		stashAddressAddPointer(s_stInventoryReverseMap, pBag, pBagEntry, true);
	}

	pBagEntry->uLastUpdateTime = gGCLState.totalElapsedTimeMs;
	pBagEntry->pEnt = pEnt;
	pBagEntry->pBag = pBag;
	pBagEntry->pBagLite = NULL;

	pSlot = eaGet(&pBagEntry->ppOwnedSlots, iIndex);
	if (!pSlot && iIndex >= 0 && iIndex <= c_iMaxValidIndex)
	{
		pSlot = CONTAINER_RECONST(InventorySlot, inv_InventorySlotCreate(iIndex));
		eaSet(&pBagEntry->ppOwnedSlots, pSlot, iIndex);
	}

	if (pSlot)
		gclInventoryUpdateSlot(pEnt, pBag, pSlot);

	if (!pSlot)
	{
		if (!s_pInvalid)
			s_pInvalid = CONTAINER_RECONST(InventorySlot, inv_InventorySlotCreate(-1));
		pSlot = s_pInvalid;
	}

	return pSlot;
}

bool gclInventoryParseKey(const char *pchKey, UIInventoryKey *pKey)
{
	if (pchKey)
	{
		const char *pch = NULL_TO_EMPTY(pchKey);
		S32 iLen;

#define STRIP_COMMAS(pch) do { while (*(pch) == ',') (pch)++; } while (false)

		pKey->erOwner = atoi(pch);
		pch += strcspn(pch, ",");
		STRIP_COMMAS(pch);
		pKey->eBag = atoi(pch);
		pch += strcspn(pch, ",");
		STRIP_COMMAS(pch);

		pKey->iSlot = atoi(pch);
		iLen = (S32)strcspn(pch, ",");
		if (!iLen && pKey->pchName
			|| iLen && !pKey->pchName
			|| iLen && pKey->pchName && strnicmp(pKey->pchName, pch, strcspn(pch, ",") - 1))
		{
			char *achBuffer = alloca(iLen + 1);
			strncpy_s(achBuffer, iLen + 1, pch, iLen);
			pKey->pchName = iLen ? allocAddString(achBuffer) : NULL;
		}
		pch += strcspn(pch, ",");
		STRIP_COMMAS(pch);

		pKey->eType = atoi(pch);
		pch += strcspn(pch, ",");
		STRIP_COMMAS(pch);
		pKey->iContainerID = atoi(pch);

		pKey->pOwner = entFromEntityRefAnyPartition(pKey->erOwner);
		pKey->pExtract = entity_GetCachedGameAccountDataExtract(pKey->pOwner);
		if (pKey->eType == GLOBALTYPE_ENTITYCRITTER && !pKey->iContainerID)
		{
			EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pKey->pOwner);
			//special case for supercritterpet inventories
			ActiveSuperCritterPet* const* ppPets = SAFE_MEMBER(pData, ppSuperCritterPets);
			ActiveSuperCritterPet* pPet = ppPets ? eaGet(&ppPets, pKey->eBag) : NULL;
			pKey->pEntity = pKey->pOwner;
			pKey->pBag = pPet ? pPet->pEquipment : NULL;
			pKey->pSlot = pKey->pBag ? eaGet(&pKey->pBag->ppIndexedInventorySlots, pKey->iSlot) : NULL;
		}
		else
		{
			pKey->pEntity = pKey->eType ? entity_GetSubEntity(PARTITION_CLIENT, pKey->pOwner, pKey->eType, pKey->iContainerID) : pKey->pOwner;
			pKey->pBag = inv_IsGuildBag(pKey->eBag) ? inv_guildbank_GetBag(guild_GetGuildBank(pKey->pEntity), pKey->eBag) : CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pKey->pEntity), pKey->eBag, pKey->pExtract));
			pKey->pBagLite = inv_IsGuildBag(pKey->eBag) ? inv_guildbank_GetLiteBag(guild_GetGuildBank(pKey->pEntity), pKey->eBag) : CONTAINER_RECONST(InventoryBagLite, inv_GetLiteBag(pKey->pEntity, pKey->eBag, pKey->pExtract));
			pKey->pSlot = pKey->pBag ? eaGet(&pKey->pBag->ppIndexedInventorySlots, pKey->iSlot) : NULL;
			pKey->pSlotLite = pKey->pBagLite ? eaIndexedGetUsingString(&pKey->pBagLite->ppIndexedLiteSlots, pKey->pchName) : NULL;
		}
		return true;
	}

	ZeroStruct(pKey);
	return false;
}

const char *gclInventoryMakeKeyString(ExprContext *pContext, UIInventoryKey *pKey)
{
	static char s_ach[1024];
	if (!pKey->erOwner)
		return "";
	if (pKey->eType && pKey->pchName && *pKey->pchName)
		sprintf(s_ach, "%d,%d,%s,%d,%d", pKey->erOwner, pKey->eBag, pKey->pchName, pKey->eType, pKey->iContainerID);
	else if (pKey->pchName && *pKey->pchName)
		sprintf(s_ach, "%d,%d,%s", pKey->erOwner, pKey->eBag, pKey->pchName);
	else if (pKey->eType)
		sprintf(s_ach, "%d,%d,%d,%d,%d", pKey->erOwner, pKey->eBag, pKey->iSlot, pKey->eType, pKey->iContainerID);
	else
		sprintf(s_ach, "%d,%d,%d", pKey->erOwner, pKey->eBag, pKey->iSlot);
	return pContext ? exprContextAllocString(pContext, s_ach) : s_ach;
}

static Entity *gclInventoryGetOwner(Entity *pEntity)
{
	if (!pEntity)
		return NULL;

	if (entGetType(pEntity) == GLOBALTYPE_ENTITYSHAREDBANK)
	{
		Entity *pEnt = entActivePlayerPtr();
		EntityIterator *it;
		if (pEnt->pPlayer && pEnt->pPlayer->accountID == entGetContainerID(pEntity))
			return pEnt;

		// This case shouldn't be necessary, but if it does happen, it should
		// make a best case effort to make a valid key. The key itself should
		// be valid the slot the key points to does not have to be.
		it = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		while (pEnt = EntityIteratorGetNext(it))
		{
			if (pEnt->pPlayer && pEnt->pPlayer->accountID == entGetContainerID(pEntity))
				break;
		}
		EntityIteratorRelease(it);
		return pEnt ? pEnt : pEntity;
	}

	if (entGetType(pEntity) == GLOBALTYPE_ENTITYGUILDBANK)
	{
		Entity *pEnt = entActivePlayerPtr();
		EntityIterator *it;
		if (pEnt->pPlayer && pEnt->pPlayer->pGuild && pEnt->pPlayer->pGuild->iGuildID == entGetContainerID(pEntity))
			return pEnt;

		// This case shouldn't be necessary, but if it does happen, it should
		// make a best case effort to make a valid key. The key itself should
		// be valid the slot the key points to does not have to be.
		it = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
		while (pEnt = EntityIteratorGetNext(it))
		{
			if (pEnt->pPlayer && pEnt->pPlayer->pGuild && pEnt->pPlayer->pGuild->iGuildID == entGetContainerID(pEntity))
				break;
		}
		EntityIteratorRelease(it);
		return pEnt ? pEnt : pEntity;
	}

	if (pEntity->pSaved && pEntity->pSaved->conOwner.containerType)
		return entFromContainerIDAnyPartition(pEntity->pSaved->conOwner.containerType, pEntity->pSaved->conOwner.containerID);

	return pEntity;
}

bool gclInventoryMakeSlotKey(Entity *pEntity, InventoryBag *pBag, InventorySlot *pSlot, UIInventoryKey *pKey)
{
	if (pEntity && pBag)
		gclInventoryUpdateSlot(pEntity, pBag, pSlot);
	else if (!pBag)
		pBag = gclInventoryGetSlotBag(pSlot, &pEntity);
	else if (!pEntity)
		pEntity = gclInventoryGetBagEntity(pBag);

	if (!pEntity || !pBag)
	{
		ZeroStruct(pKey);
		return false;
	}

	pKey->pOwner = gclInventoryGetOwner(pEntity);
	pKey->pEntity = pEntity;
	pKey->pExtract = entity_GetCachedGameAccountDataExtract(pKey->pOwner);
	pKey->pBag = pBag;
	pKey->pBagLite = NULL;
	pKey->pSlot = pSlot;
	pKey->pSlotLite = NULL;

	pKey->erOwner = entGetRef(pKey->pOwner);
	pKey->eBag = pBag->BagID;
	pKey->iSlot = pSlot->pchName ? atoi(pSlot->pchName) : 0;
	pKey->pchName = pSlot->pchName;
	pKey->eType = pKey->pOwner != pKey->pEntity ? entGetType(pKey->pEntity) : GLOBALTYPE_NONE;
	pKey->iContainerID = pKey->pOwner != pKey->pEntity ? entGetContainerID(pKey->pEntity) : 0;
	return true;
}

bool gclInventoryMakeSlotLiteKey(Entity *pEntity, InventoryBagLite *pBagLite, InventorySlotLite *pSlotLite, UIInventoryKey *pKey)
{
	if (pEntity && pBagLite)
		gclInventoryUpdateSlotLite(pEntity, pBagLite, pSlotLite);
	else if (!pBagLite)
		pBagLite = gclInventoryGetSlotLiteBag(pSlotLite, &pEntity);
	else if (!pEntity)
		pEntity = gclInventoryGetBagLiteEntity(pBagLite);

	if (!pEntity || !pBagLite)
	{
		ZeroStruct(pKey);
		return false;
	}

	pKey->pOwner = gclInventoryGetOwner(pEntity);
	pKey->pEntity = pEntity;
	pKey->pExtract = entity_GetCachedGameAccountDataExtract(pKey->pOwner);
	pKey->pBag = NULL;
	pKey->pBagLite = pBagLite;
	pKey->pSlot = NULL;
	pKey->pSlotLite = pSlotLite;

	pKey->erOwner = entGetRef(pKey->pOwner);
	pKey->eBag = pBagLite->BagID;
	pKey->iSlot = pSlotLite->pchName ? atoi(pSlotLite->pchName) : 0;
	pKey->pchName = pSlotLite->pchName;
	pKey->eType = pKey->pOwner != pKey->pEntity ? entGetType(pKey->pEntity) : GLOBALTYPE_NONE;
	pKey->iContainerID = pKey->pOwner != pKey->pEntity ? entGetContainerID(pKey->pEntity) : 0;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Some notes on the expression functions in this file:
// - They are all in the Entity tag, unless they directly operate on a UIGen.
// - Prefixes:
//   - InventorySlot: Takes an InventorySlot*
//   - InventoryBag: Takes an InventoryBag*
//   - InventoryIndex: Takes a bag ID and slot number.
//   - InventoryKey: A string that can be generated/parsed from various sources, used for DnD.

// Get an InventoryBag.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetBag);
SA_RET_OP_VALID InventoryBag *gclInvExprGenInventoryGetBag(SA_PARAM_OP_VALID Entity *pEnt, S32 iBagID)
{
	InventoryBag *pBag = NULL;
	if (inv_IsGuildBag(iBagID))
	{
		pBag = inv_guildbank_GetBag(guild_GetGuildBank(pEnt), iBagID);
	}
	else
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iBagID, pExtract);
	}
	if (pBag)
		gclInventoryUpdateBag(pEnt, pBag);
	return pBag;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetBagMaxSlots);
SA_RET_OP_VALID int gclInventoryGetBagMaxSlots(SA_PARAM_OP_VALID Entity *pEnt, S32 iBagID)
{
	InventoryBag *bag = gclInvExprGenInventoryGetBag(pEnt,iBagID);
	return invbag_maxslots(pEnt, bag);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetBagFilledSlots);
SA_RET_OP_VALID int gclInventoryGetBagFilledSlots(SA_PARAM_OP_VALID Entity *pEnt, S32 iBagID)
{
	InventoryBag *bag = gclInvExprGenInventoryGetBag(pEnt,iBagID);
	S32 i, iFilled = 0;
	if (bag)
	{
		for (i = eaSize(&bag->ppIndexedInventorySlots) - 1; i >= 0; --i)
		{
			if (bag->ppIndexedInventorySlots[i]->pItem)
				++iFilled;
		}
	}
	return iFilled;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetBagEmptySlots);
SA_RET_OP_VALID int gclInventoryGetBagEmptySlots(SA_PARAM_OP_VALID Entity *pEnt, S32 iBagID)
{
	InventoryBag *bag = gclInvExprGenInventoryGetBag(pEnt,iBagID);
	S32 iMaxSlots = invbag_maxslots(pEnt, bag);
	if (!bag)
		return 0;
	if (iMaxSlots < 0 || (invbag_flags(bag) & InvBagFlag_NameIndexed))
		return -1;
	return iMaxSlots - gclInventoryGetBagFilledSlots(pEnt, iBagID);
}

// Get an InventorySlot from a bag using the slot number.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryBagGetSlotByIndex);
SA_RET_OP_VALID InventorySlot *gclInvExprInventoryBagGetSlotByIndex(SA_PARAM_OP_VALID InventoryBag *pBag, S32 iSlot)
{
	InventorySlot *pSlot = pBag ? inv_GetSlotPtr(pBag, iSlot) : NULL;
	if (pSlot)
		gclInventoryUpdateSlot(NULL, pBag, pSlot);
	return pSlot;
}

// Get an InventorySlot from a bag using the slot name.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryBagGetSlotByName);
SA_RET_OP_VALID InventorySlot *gclInvExprInventoryBagGetSlotByName(SA_PARAM_OP_VALID InventoryBag *pBag, const char *pchName)
{
	InventorySlot *pSlot = (pchName && pBag) ? eaIndexedGetUsingString(&pBag->ppIndexedInventorySlots, pchName) : NULL;
	if (pSlot)
		gclInventoryUpdateSlot(NULL, pBag, pSlot);
	return pSlot;
}

//internal function for InventoryBagGet(Filled)Slots(InRange)() functions.
static bool gclInv_GetInventoryBagSlotsInRange(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID InventoryBag *pBag, S32 iMin, S32 iMax, bool bExcludeEmpty, bool bAddPlayerBags)
{
	if (pGen && pEnt && pBag)
	{
		if (invbag_flags(pBag) & InvBagFlag_NameIndexed)
			ui_GenSetList(pGen, (void ***)&pBag->ppIndexedInventorySlots, parse_InventorySlot);
		else
		{
			InventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);
			S32 i,j;
			int iBagMaxSlots = invbag_maxslots(pEnt, pBag);
			eaClearFast(peaSlots);
			if (iMax == -1)
			{
				iMax = iBagMaxSlots - 1;
				MAX1(iMax, eaSize(&pBag->ppIndexedInventorySlots) - 1);
			}
			iMin = max(0, iMin);
			if (iBagMaxSlots > 0)
				iMax = min(iMax, iBagMaxSlots - 1);
			iMax = max(iMin, iMax);
			for (i = iMin; i <= iMax; i++)
			{
				if (!bExcludeEmpty && i >= eaSize(&pBag->ppIndexedInventorySlots))
					eaPush(peaSlots, gclInventoryUpdateNullSlot(pEnt, pBag, i));
				else if (!bExcludeEmpty || i < eaSize(&pBag->ppIndexedInventorySlots) && pBag->ppIndexedInventorySlots[i]->pItem)
					eaPush(peaSlots, gclInventoryUpdateSlot(pEnt, pBag, pBag->ppIndexedInventorySlots[i]));
			}
			if (bAddPlayerBags)
			{
				//player bags:
				for (j = InvBagIDs_PlayerBag1; j <= InvBagIDs_PlayerBag9; j++)
				{
					pBag = gclInvExprGenInventoryGetBag(pEnt, j);
					if (pBag)
					{
						for (i=0; i < eaSize(&pBag->ppIndexedInventorySlots); i++)
						{
							if (!bExcludeEmpty && i >= eaSize(&pBag->ppIndexedInventorySlots))
								eaPush(peaSlots, gclInventoryUpdateNullSlot(pEnt, pBag, i));
							else if (!bExcludeEmpty || i < eaSize(&pBag->ppIndexedInventorySlots) && pBag->ppIndexedInventorySlots[i]->pItem)
								eaPush(peaSlots, gclInventoryUpdateSlot(pEnt, pBag, pBag->ppIndexedInventorySlots[i]));
						}
					}
				}
			}
			ui_GenSetManagedListSafe(pGen, peaSlots, InventorySlot, false);
		}
		return true;
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_InventorySlot);
		return false;
	}
}

// Get the list of filled InventorySlots in the given bag, within the given range (inclusive).
// If the bag is not slot-indexed, the range is ignored.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInventoryBagGetFilledSlotsInRange);
bool gclInvExprGenInventoryBagGetFilledSlotsInRange(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBag *pBag, S32 iMin, S32 iMax)
{
	Entity* pEnt = gclInventoryGetBagEntity(pBag);
	if (!pEnt)
		pEnt = entActivePlayerPtr();
	return gclInv_GetInventoryBagSlotsInRange(pGen, pEnt, pBag, iMin, iMax, true, false);
}

// Get the list of filled InventorySlots in the given bag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInventoryBagGetFilledSlots);
bool gclInvExprGenInventoryBagGetFilledSlots(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBag *pBag)
{
	Entity* pEnt = gclInventoryGetBagEntity(pBag);
	if (!pEnt)
		pEnt = entActivePlayerPtr();
	return gclInv_GetInventoryBagSlotsInRange(pGen, pEnt, pBag, 0, -1, true, false);
}

// Get the list of filled InventorySlots in the bag with the given ID.  If iBagID is InvBagIDs_Inventory, also check overflow + player bags.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInventoryBagIDGetFilledSlots);
bool gclInvExprGenInventoryBagIDGetFilledSlots(SA_PARAM_NN_VALID UIGen *pGen, S32 iBagID)
{
	Entity* pEnt = entActivePlayerPtr();
	InventoryBag* pBag = gclInvExprGenInventoryGetBag(pEnt, iBagID);

	if (iBagID == InvBagIDs_Inventory)
		return gclInv_GetInventoryBagSlotsInRange(pGen, pEnt, pBag, 0, -1, true, true);
	else
		return gclInv_GetInventoryBagSlotsInRange(pGen, pEnt, pBag, 0, -1, true, false);
}

// Get the list of InventorySlots in the given bag, within the given range (inclusive).
// If the bag is not slot-indexed, the range is ignored.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInventoryBagGetSlotsInRange);
bool gclInvExprGenInventoryBagGetSlotsInRange(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBag *pBag, S32 iMin, S32 iMax)
{
	Entity* pEnt = gclInventoryGetBagEntity(pBag);
	if (!pEnt)
		pEnt = entActivePlayerPtr();
	return gclInv_GetInventoryBagSlotsInRange(pGen, pEnt, pBag, iMin, iMax, false, false);
}

// Get the list of InventorySlots in the given bag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInventoryBagGetSlots);
bool gclInvExprGenInventoryBagGetSlots(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBag *pBag)
{
	Entity* pEnt = gclInventoryGetBagEntity(pBag);
	if (!pEnt)
		pEnt = entActivePlayerPtr();
	return gclInv_GetInventoryBagSlotsInRange(pGen, pEnt, pBag, 0, -1, false, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenInventoryBagGetActiveSlots);
bool gclInvExprGenInventoryBagGetActiveSlots(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBag *pBag)
{
	if (pGen && pBag && (invbag_flags(pBag) & InvBagFlag_ActiveWeaponBag))
	{
		Entity* pEnt = gclInventoryGetBagEntity(pBag);
		InventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);
		S32 i, numSlots;
		if (!pEnt)
			pEnt = entActivePlayerPtr();
		eaClearFast(peaSlots);
		numSlots = invbag_maxActiveSlots(pBag); 
			
		for(i = 0; i < numSlots; ++i )
		{
			S32 slot = invbag_GetActiveSlot(pBag, i);
			if (slot < 0 || slot >= eaSize(&pBag->ppIndexedInventorySlots))
				eaPush(peaSlots, gclInventoryUpdateNullSlot(pEnt, pBag, slot));
			else
				eaPush(peaSlots, gclInventoryUpdateSlot(pEnt, pBag, pBag->ppIndexedInventorySlots[slot]));
		}
		ui_GenSetManagedListSafe(pGen, peaSlots, InventorySlot, false);
		return true;
	}
	else
	{
		ui_GenSetList(pGen, NULL, parse_InventorySlot);
		return false;
	}
}

// Get the display name of the item in the inventory slot, or "" if there isn't one.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotGetDisplayName);
const char *gclInvExprInventorySlotGetDisplayName(SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	if (pSlot && pSlot->pItem)
		return item_GetName(pSlot->pItem, entActivePlayerPtr());
	else
		return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotGetCount);
int gclInvExprInventorySlotGetCount(SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	if (pSlot && pSlot->pItem)
		return pSlot->pItem->count;
	else
		return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotGetChargesAndCount);
S32 gclInvExprInventorySlotGetChargesAndCount(SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	if (pSlot && pSlot->pItem)
	{
		return itemeval_GetItemChargesAndCount(pSlot->pItem);
	}
	
	return 0;
}


// Get the logical name of the item in the inventory slot, or "" if there isn't one.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotGetName);
const char *gclInvExprInventorySlotGetLogicalName(SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	const char *pchName = NULL;
	if (pSlot && pSlot->pItem)
	{
		ItemDef *pDef = GET_REF(pSlot->pItem->hItem);
		if (pDef)
			return pDef->pchName;
		pchName = REF_STRING_FROM_HANDLE(pSlot->pItem->hItem);
	}
	return pchName ? pchName : "";
}

// Get the icon of the item in the inventory slot, or "" if there isn't one.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotGetIcon);
const char *gclInvExprInventorySlotGetIcon(SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	if (pSlot && pSlot->pItem)
		return item_GetIconName(pSlot->pItem, NULL);
	else
		return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetSlotNumber);
S32 gclInventoryGetSlotNumber(SA_PARAM_OP_VALID Entity *pEntity, S32 eBag, SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	InventoryBag * pBag = gclInvExprGenInventoryGetBag(pEntity, eBag);
	if (pBag)
	{
		return eaFind(&pBag->ppIndexedInventorySlots, pSlot);
	}

	return -1;
}

// Get the drag key for an inventory slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotKey);
const char *gclInvExprInventorySlotKey(ExprContext *pContext, SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	UIInventoryKey Key = {0};
	gclInventoryMakeSlotKey(NULL, NULL, pSlot, &Key);
	return gclInventoryMakeKeyString(pContext, &Key);
}

// Get the drag key for this inventory bag and slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotGetKey);
const char *gclInvExprInventorySlotGetKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 eBag, SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	UIInventoryKey Key = {0};
	gclInventoryMakeSlotKey(NULL, NULL, pSlot, &Key);
	return gclInventoryMakeKeyString(pContext, &Key);
}

// DEPRECATED: Get the drag key for this inventory bag and slot; This is a pet or puppet.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotGetContKey);
const char *gclInvExprInventorySlotGetContKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pOwner, SA_PARAM_OP_VALID Entity *pPuppet, S32 eBag, const char *pchName)
{
	if (pOwner && eBag && pchName)
	{
		UIInventoryKey Key = {0};
		Key.erOwner = entGetRef(pOwner);
		Key.eBag = eBag;
		Key.pchName = pchName;
		if (pPuppet && entGetType(pPuppet) != GLOBALTYPE_ENTITYGUILDBANK && entity_GetSubEntity(PARTITION_CLIENT, pOwner, entGetType(pPuppet), entGetContainerID(pPuppet)) == pPuppet)
		{
			Key.eType = entGetType(pPuppet);
			Key.iContainerID = entGetContainerID(pPuppet);
		}
		return gclInventoryMakeKeyString(pContext, &Key);
	}
	return "";
}

// Get the drag key that corresponds to the item granting this power
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayElemGetKey);
const char *gclInvExprTrayElemGetKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID TrayElem *pTrayElem)
{
	if (pEntity && pEntity->pChar && pTrayElem)
	{
		Power *pPower = entity_TrayGetPower(pEntity, pTrayElem);
		Item *pSrcItem = SAFE_MEMBER(pPower, pSourceItem);
		// Find inventory slot in bags with this item
		if (pSrcItem && pEntity->pInventoryV2)
		{
			int i, j;
			for (i = 0; i < eaSize(&pEntity->pInventoryV2->ppInventoryBags); i++)
			{
				InventoryBag *pBag = pEntity->pInventoryV2->ppInventoryBags[i];
				for (j = 0; j < eaSize(&pBag->ppIndexedInventorySlots); j++)
				{
					InventorySlot *pSlot = pBag->ppIndexedInventorySlots[j];
					if (pSlot->pItem == pSrcItem)
					{
						return gclInvExprInventorySlotKey(pContext, pSlot);
					}
				}
			}
		}
	}
	return "";
}

// Get the inventory slot from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetSlot);
SA_RET_OP_VALID InventorySlot *gclInvExprInventoryKeyGetSlot(const char *pchKey)
{
	UIInventoryKey Key = {0};
	gclInventoryParseKey(pchKey, &Key);
	return Key.pSlot;
}

// Get the inventory item from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetItem);
SA_RET_OP_VALID Item *gclInvExprInventoryKeyGetItem(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	return pSlot ? pSlot->pItem : NULL;
}

// Get the inventory item from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetItemID);
U64 gclInvExprInventoryKeyGetItemID(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	return pSlot && pSlot->pItem ? pSlot->pItem->id : 0;
}

// Get the inventory item from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetItemCount);
U64 gclInvExprInventoryKeyGetItemCount(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	return pSlot && pSlot->pItem ? pSlot->pItem->count : 0;
}

// Get a power ID from an inventory item from the drag key.
// Takes an optional filter PowerCategory.
// returns 0 if there is no matching power on the item.
// added for NW mounts so an item can add the tray power that give the mount.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetPowerID);
U32 gclInvExprInventoryKeyGetPowerID(const char *pchItemKey, const char* pchPowerCategory)
{
	Item* item = gclInvExprInventoryKeyGetItem(pchItemKey);
	int i;
	for (i=0; i<eaSize(&item->ppPowers); i++){
		if (!pchPowerCategory || !pchPowerCategory[0] || HasPowerCat(GET_REF(item->ppPowers[i]->hDef), pchPowerCategory)){
			return item->ppPowers[i]->uiID;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenInventoryKeyVarItem");
SA_RET_OP_VALID Item *gclGenInventoryExprGetItem(SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (pGlob)
		return gclInvExprInventoryKeyGetItem(pGlob->pchString);
	return NULL;
}

// Get the drag key for this item ID.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIDGetKey);
const char *gclInvExprInventoryIDGetKey(ExprContext *pContext, U64 iItemID)
{
	UIInventoryKey Key = {0};
	InvBagIDs eRetBagID;
	int iRetSlot;
	InventorySlot* pSlot;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(entActivePlayerPtr());
	inv_trh_GetItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, entActivePlayerPtr()), iItemID, &eRetBagID, &iRetSlot, InvGetFlag_None);
	pSlot = inv_ent_GetSlotPtr(entActivePlayerPtr(), eRetBagID, iRetSlot, pExtract);
	
	gclInventoryMakeSlotKey(NULL, NULL, pSlot, &Key);
	return gclInventoryMakeKeyString(pContext, &Key);
}

// Gets the an Item by ID, returns NULL on non-existent ID
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIDGetItem);
SA_RET_OP_VALID Item *gclInvExprInventoryIDGetItem(U64 iItemID)
{
	return(inv_GetItemByID(entActivePlayerPtr(), iItemID));
}

// Get the inventory bag number from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetBagNumber);
S32 gclInvExprInventoryKeyGetBagNumber(const char *pchKey)
{
	UIInventoryKey Key = {0};
	gclInventoryParseKey(pchKey, &Key);
	return Key.eBag;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenInventoryKeyVarBagNumber");
S32 gclGenInventoryExprGetBagNumber(SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (pGlob)
		return gclInvExprInventoryKeyGetBagNumber(pGlob->pchString);
	return -1;
}

// Get the inventory slot number from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetSlotNumber);
S32 gclInvExprInventoryKeyGetSlotNumber(const char *pchKey)
{
	UIInventoryKey Key = {0};
	gclInventoryParseKey(pchKey, &Key);
	return Key.iSlot;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenInventoryKeyVarSlotNumber");
S32 gclGenInventoryExprGetSlotNumber(SA_PARAM_NN_VALID UIGen *pGen, const char *pchVar)
{
	UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVar);
	if (pGlob)
		return gclInvExprInventoryKeyGetSlotNumber(pGlob->pchString);
	return -1;
}

// Get the entity that is holding the item
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetEntity);
SA_RET_OP_VALID Entity *gclInvExprInventoryKeyGetEntity(const char *pchKey)
{
	UIInventoryKey Key = {0};
	gclInventoryParseKey(pchKey, &Key);
	return Key.pEntity;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyIsSharedBank);
bool gclInvExprInventoryKeyIsSharedBank(const char *pchKey)
{
	Entity *pEntity = gclInvExprInventoryKeyGetEntity(pchKey);
	if(pEntity && pEntity->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK)
	{
		return true;
	}

	return false;
}

// Get the pet type from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetPetType);
S32 gclInvExprInventoryKeyGetPetType(const char *pchKey)
{
	UIInventoryKey Key = {0};
	gclInventoryParseKey(pchKey, &Key);
	return Key.eType;
}

// Get the pet container ID from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetPetID);
S32 gclInvExprInventoryKeyGetPetID(const char *pchKey)
{
	UIInventoryKey Key = {0};
	gclInventoryParseKey(pchKey, &Key);
	return Key.iContainerID;
}

// Get the pet container ID from the drag key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetPetOrPlayerID);
S32 gclInvExprInventoryKeyGetPetOrPlayerID(const char *pchKey)
{
	UIInventoryKey Key = {0};
	gclInventoryParseKey(pchKey, &Key);
	return Key.pEntity ? entGetContainerID(Key.pEntity) : 0;
}

// Move an item to an from an inventory slot to an entity's inventory.
// The destination can't be an InventorySlot because it might not exist.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyMove);
bool gclInvExprInventoryKeyMove(const char *pchKeyA, S32 eBagB, S32 iSlotB, S32 iCount)
{
	UIInventoryKey KeyA = {0};

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;

	if (eBagB == InvBagIDs_None)
	{
		Item *pItem = KeyA.pSlot ? KeyA.pSlot->pItem : NULL;
		ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : NULL;
		if (pDef)
			eBagB = GetBestBagForItemDef(KeyA.pEntity, pDef, iCount, false, KeyA.pExtract);
	}

	if (!KeyA.eType)
	{
		if (KeyA.pEntity == entActivePlayerPtr())
		{
			ServerCmd_ItemMove(inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(eBagB), eBagB, iSlotB, iCount);
			return true;
		}
	}
	else
	{
		if (KeyA.pOwner == entActivePlayerPtr() && KeyA.pEntity)
		{
			ServerCmd_ItemMoveAcrossEnts(inv_IsGuildBag(KeyA.eBag), entGetType(KeyA.pEntity), entGetContainerID(KeyA.pEntity), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(eBagB), entGetType(KeyA.pOwner), entGetContainerID(KeyA.pOwner), eBagB, iSlotB, iCount);
			return true;
		}
	}
	return false;
}

// Move an item to an from an inventory slot to an entity's inventory.
// The destination can't be an InventorySlot because it might not exist.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyMoveEnt);
bool gclInvExprInventoryKeyMoveEnt(const char *pchKeyA, SA_PARAM_OP_VALID Entity *pEntB, S32 eBagB, S32 iSlotB, S32 iCount)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	UIInventoryKey KeyA = {0};
	Entity *pOwnerB = gclInventoryGetOwner(pEntB);

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (KeyA.pOwner != pPlayerEnt || pOwnerB != pPlayerEnt)
		return false;

	if (eBagB == InvBagIDs_None)
	{
		Item *pItem = KeyA.pSlot ? KeyA.pSlot->pItem : NULL;
		ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : NULL;
		if (pDef)
			eBagB = GetBestBagForItemDef(KeyA.pEntity, pDef, iCount, false, KeyA.pExtract);
	}

	if (KeyA.pEntity == pPlayerEnt && KeyA.pEntity == pEntB)
	{
		ServerCmd_ItemMove(inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(eBagB), eBagB, iSlotB, iCount);
		return true;
	}

	ServerCmd_ItemMoveAcrossEnts(inv_IsGuildBag(KeyA.eBag), entGetType(KeyA.pEntity), entGetContainerID(KeyA.pEntity), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(eBagB), entGetType(pEntB), entGetContainerID(pEntB), eBagB, iSlotB, iCount);
	return true;
}

// Move an item to an from an inventory slot to an entity's inventory.
// The destination can't be an InventorySlot because it might not exist.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyMoveKey);
bool gclInvExprInventoryKeyMoveKey(const char *pchKeyA, const char *pchKeyB, S32 iCount)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	UIInventoryKey KeyA = {0};
	UIInventoryKey KeyB = {0};

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (!gclInventoryParseKey(pchKeyB, &KeyB))
		return false;
	if (KeyA.pOwner != pPlayerEnt || KeyB.pOwner != pPlayerEnt)
		return false;

	if (KeyB.eBag == InvBagIDs_None)
	{
		Item *pItem = KeyA.pSlot ? KeyA.pSlot->pItem : NULL;
		ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : NULL;
		if (pDef)
			KeyB.eBag = GetBestBagForItemDef(KeyA.pEntity, pDef, iCount, false, KeyA.pExtract);
	}

	if (KeyA.pEntity == pPlayerEnt && KeyA.pEntity == KeyB.pEntity)
	{
		ServerCmd_ItemMove(inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(KeyB.eBag), KeyB.eBag, KeyB.iSlot, iCount);
		return true;
	}

	ServerCmd_ItemMoveAcrossEnts(inv_IsGuildBag(KeyA.eBag), entGetType(KeyA.pEntity), entGetContainerID(KeyA.pEntity), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(KeyB.eBag), entGetType(KeyB.pEntity), entGetContainerID(KeyB.pEntity), KeyB.eBag, KeyB.iSlot, iCount);
	return true;
}

// Return the count of items in an inventory slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventorySlotCount);
S32 gclInvExprInventorySlotCount(SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	return (pSlot && pSlot->pItem) ? pSlot->pItem->count : 0;
}

// Return the count of items in an inventory key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyCount);
S32 gclInvExprInventoryKeyCount(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	return (pSlot && pSlot->pItem) ? pSlot->pItem->count : 0;
}

// Return the ItemType of the item in an inventory key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyType);
S32 gclInvExprInventoryKeyType(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	Item *pItem = SAFE_MEMBER(pSlot, pItem);
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem): NULL;
	return pDef ? pDef->eType : 0;
}

// Return the name of the item in an inventory key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyName);
const char *gclInvExprInventoryKeyName(const char *pchKey, SA_PARAM_OP_VALID Entity *pEnt)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	Item *pItem = SAFE_MEMBER(pSlot, pItem);
	return pItem ? item_GetName(pItem, pEnt) : NULL;
}

// Return the icon of the item in an inventory key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyIcon);
const char *gclInvExprInventoryKeyIcon(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	Item *pItem = SAFE_MEMBER(pSlot, pItem);
	return pItem ? item_GetIconName(pItem, NULL) : NULL;
}

// Discard/drop an item.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyDrop);
bool gclInvExprInventorySlotDrop(const char *pchKey, S32 iCount)
{
	UIInventoryKey Key = {0};
	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (Key.pOwner != entActivePlayerPtr())
		return false;

	if (Key.pEntity != Key.pOwner)
	{
		ServerCmd_item_RemoveFromBagForEnt(entGetType(Key.pEntity), entGetContainerID(Key.pEntity), Key.eBag, Key.iSlot, iCount);
		return true;
	}

	ServerCmd_item_RemoveFromBag(Key.eBag, Key.iSlot, iCount, "Reward.YouDiscardedItem");
	return true;
}

// Equip an item based on source bag/index.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIndexEquip);
bool gclInvExprInventoryIndexEquip(S32 eBag, S32 iSlot)
{
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item *pItem = inv_GetItemFromBag(pEnt, eBag, iSlot, pExtract);
	if (!pItem)
		return false;
	ServerCmd_ItemEquip(eBag, iSlot);
	return true;
}

// Equip an item based on inventory key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyEquip);
bool gclInvExprInventoryKeyEquip(const char *pchKey)
{
	UIInventoryKey Key = {0};
	if (!gclInventoryParseKey(pchKey, &Key))
		return false;

	if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
		return false;

	if (Key.pEntity != Key.pOwner)
	{
		ServerCmd_ItemEquipAcrossEnts(entGetType(Key.pEntity), entGetContainerID(Key.pEntity), Key.eBag, Key.iSlot, entGetType(Key.pOwner), entGetContainerID(Key.pOwner));
		return true;
	}

	ServerCmd_ItemEquip(Key.eBag, Key.iSlot);
	return true;
}

bool gclInvExprInventoryKeyMoveValidEnt(const char *pchKeyA, SA_PARAM_OP_VALID Entity *pEntB, S32 iDestBag, S32 iDestSlot);

// Check to see if equipping is valid. TODO: This needs to call a common "is equip valid" function that ItemEquipInternal shares.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyEquipValidEnt);
bool gclInvExprInventoryKeyEquipValidEnt(const char *pchKeyA, SA_PARAM_OP_VALID Entity *pEntB)
{
	ItemDef *pDef;
	UIInventoryKey KeyA = {0};
	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;

	pDef = KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : KeyA.pSlot && KeyA.pSlot->pItem ? GET_REF(KeyA.pSlot->pItem->hItem) : NULL;
	if (!pDef)
		return false;
	if (KeyA.pOwner != entActivePlayerPtr() || gclInventoryGetOwner(pEntB) != KeyA.pOwner)
		return false;

	if (item_IsMissionGrant(pDef) ||
		item_IsSavedPet(pDef) ||
		pDef->eType == kItemType_STOBridgeOfficer ||
		item_isAlgoPet(pDef) ||
		item_IsVanityPet(pDef) ||
		item_IsAttributeModify(pDef) ||
		item_IsInjuryCureGround(pDef) ||
		item_IsInjuryCureSpace(pDef) ||
		item_IsRewardPack(pDef) ||
		item_IsMicroSpecial(pDef) ||
		item_IsExperienceGift(pDef))
	{
		return true;
	}

	return gclInvExprInventoryKeyMoveValidEnt(pchKeyA, pEntB, 0, -1);
}

// Equip an item based on inventory key.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyEquipEnt);
bool gclInvExprInventoryKeyEquipEnt(const char *pchKeyA, SA_PARAM_OP_VALID Entity *pEntB)
{
	Entity *pPlayerEnt = entActivePlayerPtr();
	ItemDef *pDef;
	InvBagIDs eBagB;
	UIInventoryKey KeyA = {0};
	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;

	pDef = KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : KeyA.pSlot && KeyA.pSlot->pItem ? GET_REF(KeyA.pSlot->pItem->hItem) : NULL;
	if (!pDef)
		return false;
	if (KeyA.pOwner != pPlayerEnt || gclInventoryGetOwner(pEntB) != KeyA.pOwner)
		return false;

	eBagB = GetBestBagForItemDef(pEntB, pDef, -1, false, KeyA.pExtract);
	if (KeyA.pEntity == pPlayerEnt && eBagB == KeyA.eBag)
		eBagB = InvBagIDs_Inventory;

	if (item_IsRecipe(pDef) ||
		item_IsMissionGrant(pDef) || 
		item_IsSavedPet(pDef) ||
		pDef->eType == kItemType_STOBridgeOfficer ||
		item_IsInjuryCureGround(pDef) || item_IsInjuryCureSpace(pDef) ||
		item_isAlgoPet(pDef) || 
		item_IsAttributeModify(pDef) ||
		item_IsRewardPack(pDef) ||
		item_IsMicroSpecial(pDef) ||
		item_IsExperienceGift(pDef))
	{
		ServerCmd_ItemEquipAcrossEnts(entGetType(KeyA.pEntity),entGetContainerID(KeyA.pEntity), KeyA.eBag, KeyA.iSlot, entGetType(pEntB), entGetContainerID(pEntB));
		return true;
	}
	else if (eBagB == KeyA.eBag || eBagB == InvBagIDs_Inventory && KeyA.eBag != InvBagIDs_Inventory)
	{
		//Looks like we are trying to unequip
		ServerCmd_ItemMoveAcrossEnts(inv_IsGuildBag(KeyA.eBag), entGetType(KeyA.pEntity), entGetContainerID(KeyA.pEntity), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(eBagB), entGetType(pEntB), entGetContainerID(pEntB), eBagB, -1, -1);
		return true;
	}
	else if (!item_IsUpgrade(pDef))
	{
		S32 iSlotB = -1;
		InventoryBag* pBagB;

		// Move the item only if it will move to another bag or entity
		if (KeyA.eBag == eBagB && KeyA.pEntity == pEntB)
			return false;

		// TODO(jm): Figure out why there is this weak logic on the client.
		pBagB = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEntB), eBagB, KeyA.pExtract));
		if (pBagB && inv_bag_BagFull(pEntB, pBagB))
			iSlotB = 0;

		ServerCmd_ItemMoveAcrossEnts(inv_IsGuildBag(KeyA.eBag), entGetType(KeyA.pEntity), entGetContainerID(KeyA.pEntity), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(eBagB), entGetType(pEntB), entGetContainerID(pEntB), eBagB, iSlotB, -1);
		return true;
	}

	ServerCmd_ItemEquipAcrossEnts(entGetType(KeyA.pEntity), entGetContainerID(KeyA.pEntity), KeyA.eBag, KeyA.iSlot, entGetType(pEntB), entGetContainerID(pEntB));
	return true;
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

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetItemCountByTypeAndCategory);
int gclInvExprGetItemCountByTypeAndCategory(ExprContext* pContext, 
											SA_PARAM_OP_VALID Entity *pEnt, 
											const char* pchTypes, 
											const char* pchCategories, 
											const char* pchVarFirstFoundItemKey)
{
	GameAccountDataExtract *pExtract;
	ItemType* peItemTypes = NULL;
	ItemCategory* peItemCategories = NULL;
	UIGenVarTypeGlob* pGlob;
	UIGen* pGen;
	int i, j, iCount = 0;

	if (!pEnt || !pEnt->pInventoryV2) {
		return 0;
	}

	itemeval_GetItemTypesFromString(pContext, pchTypes, &peItemTypes);
	itemeval_GetItemCategoriesFromString(pContext, pchCategories, &peItemCategories);

	if (!peItemTypes && !peItemCategories) {
		return 0;
	}
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pGen = (UIGen*)exprContextGetUserPtr(pContext, parse_UIGen);
	pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchVarFirstFoundItemKey);
	for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i >= 0; i--) {
		InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];
		if (!GamePermissions_trh_CanAccessBag(CONTAINER_NOCONST(Entity, pEnt), pBag->BagID, pExtract))
			continue;
		for (j = eaSize(&pBag->ppIndexedInventorySlots)-1; j >= 0; j--) {
			InventorySlot* pSlot = pBag->ppIndexedInventorySlots[j];
			ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
			if (pItemDef && (!peItemTypes || eaiFind(&peItemTypes, pItemDef->eType) >= 0)) {
				if (!peItemCategories || itemdef_HasItemCategory(pItemDef, peItemCategories)) {
					if (iCount == 0 &&
						pGlob &&
						pchVarFirstFoundItemKey && 
						pchVarFirstFoundItemKey[0]) {
						UIInventoryKey Key = {0};
						Key.pOwner = gclInventoryGetOwner(pEnt);
						Key.erOwner = entGetRef(Key.pOwner);
						Key.eBag = pBag->BagID;
						Key.iSlot = j;
						if (pEnt != Key.pOwner) {
							Key.eType = entGetType(pEnt);
							Key.iContainerID = entGetContainerID(pEnt);
						}
						estrCopy2(&pGlob->pchString, gclInventoryMakeKeyString(NULL, &Key));
					}
					iCount += SAFE_MEMBER(pSlot->pItem, count);
				}
			}
		}
	}
	if (pGlob && !iCount) {
		estrClear(&pGlob->pchString);
	}
	eaiDestroy(&peItemTypes);
	eaiDestroy(&peItemCategories);
	return iCount;
}

// Format a string with an Item.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatItem);
const char *gclInvExprMessageFormatItem(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, SA_PARAM_OP_VALID Item *pItem)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItem)
		FormatGameMessageKey(&s_pch, pchMessageKey, STRFMT_ITEM_KEY("Value", pItem), STRFMT_END);
	return s_pch;
}

// Format a string with an Item.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatItem);
const char *gclInvExprStringFormatItem(ExprContext *pContext, const char *pchFormat, SA_PARAM_OP_VALID Item *pItem)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItem)
		FormatGameString(&s_pch, pchFormat, STRFMT_ITEM_KEY("Value", pItem), STRFMT_END);
	return s_pch;
}

// Format a string with an Item.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatItemDef);
const char *gclInvExprMessageFormatItemDef(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItemDef)
		FormatGameMessageKey(&s_pch, pchMessageKey, STRFMT_ITEMDEF_KEY("Value", pItemDef), STRFMT_END);
	return s_pch;
}

// Format a string with an Item.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatItemDef);
const char *gclInvExprStringFormatItemDef(ExprContext *pContext, const char *pchFormat, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItemDef)
		FormatGameString(&s_pch, pchFormat, STRFMT_ITEMDEF_KEY("Value", pItemDef), STRFMT_END);
	return s_pch;
}

// Get the advanced information for an item. This returns nothing for almost all cases, and may safely be ignored.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAdvancedInfo);
const char *gclInvExprItemAdvancedInfo(ExprContext *pContext, SA_PARAM_OP_VALID Item *pItem)
{
	static char *s_pch;
	Entity *pPlayer = entActivePlayerPtr();
	int i;
	ItemDef *pItemDef;

	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);

	if (!pItem || !g_bDisplayItemDebugInfo)
		return s_pch;

	pItemDef = GET_REF(pItem->hItem);
	if (pItemDef && pItemDef->pCraft)
	{
		if (pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemValue)
			estrConcatf(&s_pch, "\n<br> result def: %s", REF_STRING_FROM_HANDLE(pItemDef->pCraft->hItemResult));
		else if (pItemDef->eType == kItemType_ItemPowerRecipe)
			estrConcatf(&s_pch, "\n<br> result power: %s", REF_STRING_FROM_HANDLE(pItemDef->pCraft->hItemPowerResult));
	}

	estrConcatf(&s_pch, "\n<br> item def: %s", REF_STRING_FROM_HANDLE(pItem->hItem));
	if ((pItem->flags & kItemFlag_Algo) && pItem->pAlgoProps)
	{
		for (i = 0; i < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); i++)
		{
			estrConcatf(&s_pch, "\n<br> power: %s", REF_STRING_FROM_HANDLE(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef));
		}
	}

	return s_pch;
}

// Format a string with an Item for a specific Entity. If the entity is null, then it behaves exactly like MessageFormatItem.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatItemForEntity);
const char *gclInvExprEntMessageFormatItem(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEntity)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItem)
	{
		if (pEntity)
			FormatGameMessageKey(&s_pch, pchMessageKey, STRFMT_ENTITEM_KEY("Value", pEntity, pItem, NULL), STRFMT_END);
		else
			FormatGameMessageKey(&s_pch, pchMessageKey, STRFMT_ITEM_KEY("Value", pItem), STRFMT_END);
	}
	return s_pch;
}

// Format a string with an Item for a specific Entity. If the entity is null, then it behaves exactly like MessageFormatItem.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatItemForEntity);
const char *gclInvExprEntStringFormatItem(ExprContext *pContext, const char *pchFormat, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEntity)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItem)
	{
		if (pEntity)
			FormatGameString(&s_pch, pchFormat, STRFMT_ENTITEM_KEY("Value", pEntity, pItem, NULL), STRFMT_END);
		else
			FormatGameString(&s_pch, pchFormat, STRFMT_ITEM_KEY("Value", pItem), STRFMT_END);
	}
	return s_pch;
}

// Format a string with an Item for a specific Entity. If the entity is null, then it behaves exactly like MessageFormatItemDef.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatItemDefForEntity);
const char *gclInvExprEntMessageFormatItemDef(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, SA_PARAM_OP_VALID ItemDef *pItemDef, SA_PARAM_OP_VALID Entity *pEntity)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItemDef)
	{
		if (pEntity)
			FormatGameMessageKey(&s_pch, pchMessageKey, STRFMT_ENTITEMDEF_KEY("Value", pEntity, pItemDef, NULL), STRFMT_END);
		else
			FormatGameMessageKey(&s_pch, pchMessageKey, STRFMT_ITEMDEF_KEY("Value", pItemDef), STRFMT_END);
	}
	return s_pch;
}

// Format a string with an Item for a specific Entity. If the entity is null, then it behaves exactly like MessageFormatItemDef.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatItemDefForEntity);
const char *gclInvExprEntStringFormatItemDef(ExprContext *pContext, const char *pchFormat, SA_PARAM_OP_VALID ItemDef *pItemDef, SA_PARAM_OP_VALID Entity *pEntity)
{
	static char *s_pch;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);
	if (pItemDef)
	{
		if (pEntity)
			FormatGameString(&s_pch, pchFormat, STRFMT_ENTITEMDEF_KEY("Value", pEntity, pItemDef, NULL), STRFMT_END);
		else
			FormatGameString(&s_pch, pchFormat, STRFMT_ITEMDEF_KEY("Value", pItemDef), STRFMT_END);
	}
	return s_pch;
}

// Generate the item's power description
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntItemGetPowerDesc);
const char *gclInvExprItemGetPowerDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, ACMD_EXPR_DICT(Message) const char *pchPowerMessageKey, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey)
{
	static char *s_pch;
	static AutoDescPower s_AutoDesc;

	Character *pChar = pPlayer ? pPlayer->pChar : NULL;
	int iLevel, iPower, iNumPowers;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	F32 *pfScales = NULL;
	Item *pCraftResult = NULL;
	Entity *pOwner;

	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);

	if (!pItem || !pChar || !pItemDef)
		return s_pch;

	if (pPlayer->erOwner)
		pOwner = entFromEntityRefAnyPartition(pPlayer->erOwner);
	else
		pOwner = pPlayer;

	iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pChar ? pChar->iLevelCombat : 1;
	if(pItemDef->bAutoDescDisabled)
	{
		estrCopy2(&s_pch, TranslateDisplayMessage(pItemDef->msgAutoDesc));
	}
	else if (pItemDef->eType == kItemType_ItemPowerRecipe)
	{
		ItemPowerDef *pItemPowerDef = pItemDef->pCraft ? GET_REF(pItemDef->pCraft->hItemPowerResult) : NULL;
		PowerDef *pPowerDef = pItemPowerDef ? GET_REF(pItemPowerDef->hPower) : NULL;

		if (pPowerDef)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
			StructInit(parse_AutoDescPower, &s_AutoDesc);
			powerdef_AutoDesc(entGetPartitionIdx(pPlayer),pPowerDef,NULL,&s_AutoDesc,NULL,NULL,NULL,pChar,NULL,NULL,iLevel,true,entGetPowerAutoDescDetail(pOwner,false),pExtract,NULL);
			powerdef_AutoDescCustom(pPlayer, &s_pch, pPowerDef, &s_AutoDesc, pchPowerMessageKey, pchAttribMessageKey);
			StructDeInit(parse_AutoDescPower, &s_AutoDesc);
		}
	}
	else
	{
		if (pItemDef->pCraft && (pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemValue))
		{
			ItemDef *pCraftItemDef = GET_REF(pItemDef->pCraft->hItemResult);
			if(!pCraftItemDef)
			{
				Errorf("ItemDef %s has doesn't have pItemDef->pCraft->hItemResult.", pItemDef->pchName);
				return s_pch;
			}
			pItemDef = pCraftItemDef;
			pCraftResult = item_FromEnt(CONTAINER_NOCONST(Entity, pOwner),pItemDef->pchName,0,NULL,0);
			pItem = pCraftResult;
		}

		iNumPowers = item_GetNumItemPowerDefs(pItem, true);

		//loop for all powers on the item
		for(iPower=0; iPower< iNumPowers; iPower++)
		{
			Power *pPower = item_GetPower(pItem,iPower);
			PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
			StructInit(parse_AutoDescPower, &s_AutoDesc);

			if(pPower)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
				power_AutoDesc(entGetPartitionIdx(pPlayer),pPower,pChar,NULL,&s_AutoDesc,NULL,NULL,NULL,true,0,entGetPowerAutoDescDetail(pOwner,false),pExtract,NULL);
				if (pPowerDef)
					powerdef_AutoDescCustom(pPlayer, &s_pch, pPowerDef, &s_AutoDesc, pchPowerMessageKey, pchAttribMessageKey);
			}
			else
			{
				if(pPowerDef && pPowerDef->eType!=kPowerType_Innate)
					powerdef_AutoDescCustom(pPlayer, &s_pch, pPowerDef, &s_AutoDesc, pchPowerMessageKey, pchAttribMessageKey);
			}

			StructDeInit(parse_AutoDescPower, &s_AutoDesc);
		}

		if (pCraftResult)
			StructDestroy(parse_Item, pCraftResult);
	}

	return s_pch;
}

static void gclInvCompareModDetails(char **ppchResult, Message *pMessage, const char *pchFormat, AutoDescInnateModDetails *pBase, AutoDescInnateModDetails *pNew)
{
	if (pMessage || pchFormat)
	{
		if (pMessage)
		{
			FormatGameMessage(ppchResult, pMessage, STRFMT_INNATEMOD(pBase, pNew), STRFMT_END);
		}
		else
		{
			FormatGameString(ppchResult, pchFormat, STRFMT_INNATEMOD(pBase, pNew), STRFMT_END);
		}
	}
	else
	{
		// Totally nerf'd AutoDesc, but then again, this should never happen.
		static char s_achBuf[64];
		char *pchSign = pBase->fMagnitude > 0 ? "+" : NULL;

		if (pBase->bAttribBoolean)
		{
			*s_achBuf = '\0';
			pchSign = NULL;
		}
		else
		{
			sprintf(s_achBuf, "%f", pBase->fMagnitude);
		}

		FormatGameMessageKey(ppchResult, pBase->pchDefaultMessage,
			STRFMT_STRING("Sign", pchSign),
			STRFMT_STRING("Attrib", pBase->pchAttribName),
			STRFMT_STRING("Magnitude", s_achBuf),
			STRFMT_STRING("Variance", NULL),
			STRFMT_STRING("Reason", NULL),
			STRFMT_STRING("GemTypeRequired",pBase->pchRequiredGemSlot),
			STRFMT_END);
	}
}

AUTO_EXPR_FUNC_STATIC_CHECK;
const char *gclInvExprItemGemGetInnatePowerDescFilteredCheck(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pHolder, SA_PARAM_OP_VALID Item *pItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey, int eGemType)
{
	Message *pMessage = strchr(pchAttribMessageKey, '{') ? NULL : RefSystem_ReferentFromString("Message", pchAttribMessageKey);
	const char *pchFormat = strchr(pchAttribMessageKey, '{') ? pchAttribMessageKey : NULL;
	if (pMessage || pchFormat)
		return "";
	if (pchAttribMessageKey && strstri(pchAttribMessageKey, "DummyMultiValString"))
		return "";
	ErrorFilenamef(exprContextGetBlameFile(pContext), "Not a valid message or a valid format string: %s", pchAttribMessageKey);
	return "";
}

AUTO_EXPR_FUNC_STATIC_CHECK;
const char *gclInvExprItemGemGetInnatePowerDescCheck(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pHolder, SA_PARAM_OP_VALID Item *pItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey)
{
	return gclInvExprItemGemGetInnatePowerDescFilteredCheck(pContext, pPlayer, pHolder, pItem, pchAttribMessageKey, kItemGemType_Any);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
const char *gclInvExprItemGetInnatePowerDescCheck(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey)
{
	return gclInvExprItemGemGetInnatePowerDescCheck(pContext,pPlayer,NULL,pItem,pchAttribMessageKey);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
const char *gclInvExprItemGemSlotInnatePowerDescCheck(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Item *pGem, const char *pchAttribMessageKey)
{
	return gclInvExprItemGemGetInnatePowerDescCheck(pContext,pPlayer,pItem,pGem,pchAttribMessageKey);
}

// Generate the description for the innate powers
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(StringFormatEntItemGemInnatePowerDescFiltered) ACMD_EXPR_STATIC_CHECK(gclInvExprItemGemGetInnatePowerDescFilteredCheck);
const char *gclInvExprItemGemGetInnatePowerDescFiltered(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pHolder, SA_PARAM_OP_VALID Item *pItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey, int eGemType)
{
	static char *s_pch;
	static AutoDescInnateModDetails **s_eaInnateMods;
	static PowerDef **s_ppDefsInnate;
	static F32 *s_pfScales;
	static S32 *s_eaSlotRequired;

	Character *pChar = pPlayer ? pPlayer->pChar : NULL;
	int iLevel, iPower, iNumPowers;
	int iGemPowers;
	int i;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	Message *pMessage = strchr(pchAttribMessageKey, '{') ? NULL : RefSystem_ReferentFromString("Message", pchAttribMessageKey);
	const char *pchFormat = strchr(pchAttribMessageKey, '{') ? pchAttribMessageKey : NULL;
	Item *pCraftResult = NULL;
	Entity *pOwner;
	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);

	if (!pItem || !pChar || !pItemDef)
		return s_pch;

	if (pPlayer->erOwner)
		pOwner = entFromEntityRefAnyPartition(pPlayer->erOwner);
	else
		pOwner = pPlayer;

	eaClear(&s_ppDefsInnate);
	eafClear(&s_pfScales);
	ea32Clear(&s_eaSlotRequired);

	if (pItemDef->pCraft && (pItemDef->eType == kItemType_ItemPowerRecipe))
		return s_pch;

	if (pItemDef->pCraft && (pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemValue))
	{
		pItemDef = GET_REF(pItemDef->pCraft->hItemResult);
		if(!pItemDef)
		{
			return s_pch;
		}
		pCraftResult = item_FromEnt(CONTAINER_NOCONST(Entity, pOwner),pItemDef->pchName,0,NULL,0);
		pItem = pCraftResult;
		if(!pItem)
		{
			return s_pch;
		}
	}

	iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	iGemPowers = item_GetNumGemsPowerDefs(pItem);
	if (pHolder)
		iLevel = item_trh_GetGemPowerLevel((NOCONST(Item)*)pHolder) ? item_trh_GetGemPowerLevel((NOCONST(Item)*)pHolder) : pChar ? pChar->iLevelCombat : 1;
	else
		iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pChar ? pChar->iLevelCombat : 1;

	for(iPower=0; iPower<iNumPowers; iPower++)
	{
		PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
		ItemPowerDef *pItemPower = item_GetItemPowerDef(pItem,iPower);
		if (pPowerDef && pPowerDef->eType==kPowerType_Innate)
		{
			if (iNumPowers - iGemPowers <= iPower || pHolder)
			{
				if (pItemPower->pRestriction && pItemPower->pRestriction->eRequiredGemSlotType)
				{
					// The eGemType == kItemType_Any is a hack - we should actually refactor this expression to make it clearer and cleaner. 
					// I think we actually want three expressions, 
					//     one for getting gems by themselves, 
					//     one for checking gems in a holder,
					//     one for checking gems w.r.t. a holder, even if it's not slotted in it
					ItemGemSlotDef *pGemSlotDef = (pHolder && eGemType == kItemGemType_Any) ? GetGemSlotDefFromPowerIdx(pHolder, iPower) :  GetGemSlotDefFromPowerIdx(pItem, iPower);
					ItemGemType eFilter = pGemSlotDef ? pGemSlotDef->eType : eGemType;
					if ((pItemPower->pRestriction->eRequiredGemSlotType & eFilter) == 0)
						continue;
				}
			}
			eaPush(&s_ppDefsInnate,pPowerDef);
			eafPush(&s_pfScales, item_GetItemPowerScale(pItem, iPower));

			if (pItemDef->eType == kItemType_Gem && !pHolder)
			{
				ea32Push(&s_eaSlotRequired,pItemPower->pRestriction ? pItemPower->pRestriction->eRequiredGemSlotType : 0);
			}
			else
			{
				ea32Push(&s_eaSlotRequired,0);
			}
		}
	}

	if(eaSize(&s_ppDefsInnate) > 0)
	{
		powerdefs_GetAutoDescInnateMods(entGetPartitionIdx(pPlayer), pItem,s_ppDefsInnate, s_pfScales, NULL, &s_eaInnateMods, NULL, pChar, s_eaSlotRequired, iGemPowers ? iNumPowers - iGemPowers : -1, iLevel, true, 0, entGetPowerAutoDescDetail(pOwner,false));
	}
	else
	{
		eaClearStruct(&s_eaInnateMods, parse_AutoDescInnateModDetails);
	}

	for(i = 0; i < eaSize(&s_eaInnateMods); i++)
	{
		gclInvCompareModDetails(&s_pch, pMessage, pchFormat, s_eaInnateMods[i], NULL);
	}

	if (pCraftResult)
		StructDestroy(parse_Item, pCraftResult);

	return s_pch;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(StringFormatEntItemGemInnatePowerDesc) ACMD_EXPR_STATIC_CHECK(gclInvExprItemGemGetInnatePowerDescCheck);
const char *gclInvExprItemGemGetInnatePowerDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pHolder, SA_PARAM_OP_VALID Item *pItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey)
{
	return gclInvExprItemGemGetInnatePowerDescFiltered(pContext, pPlayer, pHolder, pItem, pchAttribMessageKey, kItemGemType_Any);
}

// Generate the description for the innate powers
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(StringFormatEntItemInnatePowerDesc) ACMD_EXPR_STATIC_CHECK(gclInvExprItemGetInnatePowerDescCheck);
const char *gclInvExprItemGetInnatePowerDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, const char *pchAttribMessageKey);

// Generate the description for the innate powers
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntItemGetInnatePowerDesc) ACMD_EXPR_STATIC_CHECK(gclInvExprItemGetInnatePowerDescCheck);
const char *gclInvExprItemGetInnatePowerDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey)
{
	return gclInvExprItemGemGetInnatePowerDesc(pContext,pPlayer,NULL,pItem,pchAttribMessageKey);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(StringFormatEntItemGemSlotInnatePowerDesc) ACMD_EXPR_STATIC_CHECK(gclInvExprItemGemSlotInnatePowerDescCheck);
const char *gclInvExprItemGemSlotInnatePowerDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Item *pGem, const char *pchAttribMessageKey)
{
	return gclInvExprItemGemGetInnatePowerDesc(pContext,pPlayer,pItem,pGem,pchAttribMessageKey);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
const char *gclInvExprItemGetInnatePowerCompareDescCheck(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Item *pNewItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey)
{
	Message *pMessage = strchr(pchAttribMessageKey, '{') ? NULL : RefSystem_ReferentFromString("Message", pchAttribMessageKey);
	const char *pchFormat = strchr(pchAttribMessageKey, '{') ? pchAttribMessageKey : NULL;
	if (pMessage || pchFormat)
		return "";
	if (pchAttribMessageKey && strstri(pchAttribMessageKey, "DummyMultiValString"))
		return "";
	ErrorFilenamef(exprContextGetBlameFile(pContext), "Not a valid message or a valid format string: %s", pchAttribMessageKey);
	return "";
}

// Generate the comparison description for the innate powers
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(StringFormatEntItemInnatePowerCompareDesc) ACMD_EXPR_STATIC_CHECK(gclInvExprItemGetInnatePowerCompareDescCheck);
const char *gclInvExprItemGetInnatePowerCompareDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Item *pNewItem, const char *pchFormat);

// Generate the comparison description for the innate powers
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntItemGetInnatePowerCompareDesc) ACMD_EXPR_STATIC_CHECK(gclInvExprItemGetInnatePowerCompareDescCheck);
const char *gclInvExprItemGetInnatePowerCompareDesc(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Item *pNewItem, ACMD_EXPR_DICT(Message) const char *pchAttribMessageKey)
{
	static char *s_pch;
	static AutoDescInnateModDetails **s_eaInnateMods;
	static PowerDef **s_ppDefsInnate;
	static F32 *s_pfScales;
	static AutoDescInnateModDetails **s_eaNewInnateMods;
	static PowerDef **s_ppNewDefsInnate;
	static F32 *s_pfNewScales;

	Character *pChar = pPlayer ? pPlayer->pChar : NULL;
	int iLevel, iPower, iNumPowers;
	int iNewLevel;
	int i, j;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	ItemDef *pNewItemDef = pNewItem ? GET_REF(pNewItem->hItem) : NULL;
	Message *pMessage = strchr(pchAttribMessageKey, '{') ? NULL : RefSystem_ReferentFromString("Message", pchAttribMessageKey);
	const char *pchFormat = strchr(pchAttribMessageKey, '{') ? pchAttribMessageKey : NULL;
	Item *pCraftResult = NULL;
	Item *pNewCraftResult = NULL;
	Entity *pOwner = NULL;

	if (!s_pch)
		estrCreate(&s_pch);
	estrClear(&s_pch);

	if (!pItem || !pChar || !pItemDef || !pNewItemDef)
		return s_pch;

	if (pItemDef->pCraft && (pItemDef->eType == kItemType_ItemPowerRecipe))
		return s_pch;
	if (pNewItemDef->pCraft && (pNewItemDef->eType == kItemType_ItemPowerRecipe))
		return s_pch;

	if (pPlayer->erOwner)
		pOwner = entFromEntityRefAnyPartition(pPlayer->erOwner);
	else
		pOwner = pPlayer;

	if (pItemDef->pCraft && (pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemValue))
	{
		pItemDef = GET_REF(pItemDef->pCraft->hItemResult);
		pCraftResult = item_FromEnt(CONTAINER_NOCONST(Entity, pOwner),pItemDef->pchName,0,NULL,0);
		pItem = pCraftResult;
	}
	if (pNewItemDef->pCraft && (pNewItemDef->eType == kItemType_ItemRecipe || pNewItemDef->eType == kItemType_ItemValue))
	{
		pNewItemDef = GET_REF(pNewItemDef->pCraft->hItemResult);
		pNewCraftResult = item_FromEnt(CONTAINER_NOCONST(Entity, pOwner),pNewItemDef->pchName,0,NULL,0);
		pNewItem = pNewCraftResult;
	}

	iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pChar ? pChar->iLevelCombat : 1;
	iNewLevel = item_GetLevel(pNewItem) ? item_GetLevel(pNewItem) : pChar ? pChar->iLevelCombat : 1;

	eaClear(&s_ppDefsInnate);
	eafClear(&s_pfScales);
	eaClear(&s_ppNewDefsInnate);
	eafClear(&s_pfNewScales);

	for(iPower=0; iPower<iNumPowers; iPower++)
	{
		PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
		if(pPowerDef && pPowerDef->eType==kPowerType_Innate)
		{
			eaPush(&s_ppDefsInnate,pPowerDef);
			eafPush(&s_pfScales, item_GetItemPowerScale(pItem, iPower));
		}
	}

	iNumPowers = item_GetNumItemPowerDefs(pNewItem, true);
	for(iPower=0; iPower<iNumPowers; iPower++)
	{
		PowerDef *pPowerDef = item_GetPowerDef(pNewItem, iPower);
		if(pPowerDef && pPowerDef->eType==kPowerType_Innate)
		{
			eaPush(&s_ppNewDefsInnate,pPowerDef);
			eafPush(&s_pfNewScales, item_GetItemPowerScale(pNewItem, iPower));
		}
	}

	if(eaSize(&s_ppDefsInnate) > 0)
	{
		powerdefs_GetAutoDescInnateMods(entGetPartitionIdx(pPlayer), pItem, s_ppDefsInnate, s_pfScales, NULL, &s_eaInnateMods, NULL, pChar, NULL, -1, iLevel, true, 0, entGetPowerAutoDescDetail(pOwner,false));
	}
	else
	{
		eaClearStruct(&s_eaInnateMods, parse_AutoDescInnateModDetails);
	}
	if(eaSize(&s_ppNewDefsInnate) > 0)
	{
		powerdefs_GetAutoDescInnateMods(entGetPartitionIdx(pPlayer), pNewItem, s_ppNewDefsInnate, s_pfNewScales, NULL, &s_eaNewInnateMods, NULL, pChar, NULL, -1, iNewLevel, true, 0, entGetPowerAutoDescDetail(pOwner,false));
	}
	else
	{
		eaClearStruct(&s_eaNewInnateMods, parse_AutoDescInnateModDetails);
	}

	// Mods are sorted in descending order of magnitude
	for (i = 0; i < eaSize(&s_eaInnateMods); i++)
	{
		for (j = eaSize(&s_eaNewInnateMods) - 1; j >= 0; j--)
		{
			if (s_eaInnateMods[i]->offAttrib == s_eaNewInnateMods[j]->offAttrib &&
				s_eaInnateMods[i]->offAspect == s_eaNewInnateMods[j]->offAspect)
			{
				break;
			}
		}

		if (j >= 0)
		{
			// Compare the two
			gclInvCompareModDetails(&s_pch, pMessage, pchFormat, s_eaInnateMods[i], s_eaNewInnateMods[j]);
		}
		else
		{
			// Compare to a dummy
			AutoDescInnateModDetails dummy = {0};
			dummy = *s_eaInnateMods[i];
			dummy.fMagnitude = 0;
			gclInvCompareModDetails(&s_pch, pMessage, pchFormat, s_eaInnateMods[i], &dummy);
		}
	}

	// Finish with mods that aren't in the original list
	for (i = 0; i < eaSize(&s_eaNewInnateMods); i++)
	{
		for (j = eaSize(&s_eaInnateMods) - 1; j >= 0; j--)
		{
			if (s_eaInnateMods[j]->offAttrib == s_eaNewInnateMods[i]->offAttrib &&
				s_eaInnateMods[j]->offAspect == s_eaNewInnateMods[i]->offAspect)
			{
				break;
			}
		}

		// Only show if it's a new mod
		if (j < 0)
		{
			AutoDescInnateModDetails dummy = {0};
			dummy = *s_eaNewInnateMods[i];
			dummy.fMagnitude = 0;
			gclInvCompareModDetails(&s_pch, pMessage, pchFormat, &dummy, s_eaNewInnateMods[i]);
		}
	}

	if (pCraftResult)
		StructDestroy(parse_Item, pCraftResult);
	if (pNewCraftResult)
		StructDestroy(parse_Item, pNewCraftResult);

	return s_pch;
}

// Get the drag key for this inventory bag and slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIndexGetKey);
const char *gclInvExprInventoryIndexGetKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 eBag, S32 iSlot)
{
	if (pEntity && eBag)
	{
		UIInventoryKey Key = {0};
		Key.erOwner = entGetRef(pEntity);
		Key.eBag = eBag;
		Key.iSlot = iSlot;
		return gclInventoryMakeKeyString(pContext, &Key);
	}
	return "";
}

// Get the drag key for this inventory bag and slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InvSlotRefGetKey);
const char *gclInvExprInvSlotRefGetKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID InventorySlotReference *pInvSlotRef)
{
	if (pEntity && pInvSlotRef)
	{
		UIInventoryKey Key = {0};
		Key.erOwner = entGetRef(pEntity);
		Key.eBag = pInvSlotRef->eBagID;
		Key.iSlot = pInvSlotRef->iIndex;
		return gclInventoryMakeKeyString(pContext, &Key);
	}
	return "";
}

// Get the drag key for this inventory bag and slot; This is a pet or puppet.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIndexGetContKey);
const char *gclInvExprInventoryIndexGetContKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pOwner, SA_PARAM_OP_VALID Entity *pPuppet, S32 eBag, S32 iSlot)
{
	if (pOwner && eBag)
	{
		UIInventoryKey Key = {0};
		Key.erOwner = entGetRef(pOwner);
		Key.eBag = eBag;
		Key.iSlot = iSlot;
		if (pPuppet && entGetType(pPuppet) != GLOBALTYPE_ENTITYGUILDBANK && entity_GetSubEntity(PARTITION_CLIENT, pOwner, entGetType(pPuppet), entGetContainerID(pPuppet)) == pPuppet)
		{
			Key.eType = entGetType(pPuppet);
			Key.iContainerID = entGetContainerID(pPuppet);
		}
		return gclInventoryMakeKeyString(pContext, &Key);
	}
	return "";
}


// Get the item for this inventory bag and slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryIndexGetItem);
SA_RET_OP_VALID Item *gclInvExprInventoryIndexGetItem(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, S32 eBag, S32 iSlot)
{
	if (pEntity && eBag)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		InventorySlot *pSlot = inv_ent_GetSlotPtr(pEntity, eBag, iSlot, pExtract);
		return SAFE_MEMBER(pSlot, pItem);
	}
	return NULL;
}

// Format a numeric string into three values, using the given conversion rates.
// The message has replacements {Value1}, {Value2}, {Value3}.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetNumericString3);
const char *gclInvExprGetNumericString3(ExprContext *pContext, S32 iValue, ACMD_EXPR_DICT(Message) const char *pchMessageKey, S32 iConversion12, S32 iConversion23)
{
	char *estrOutput = NULL;
	S32 iValue1 = iValue % iConversion12;
	S32 iValue2 = (iValue / iConversion12) % iConversion23;
	S32 iValue3 = (iValue / iConversion12) / iConversion23;
	estrClear(&estrOutput);
	FormatGameMessageKey(&estrOutput, pchMessageKey,
		STRFMT_INT("Value1", iValue1),
		STRFMT_INT("Value2", iValue2),
		STRFMT_INT("Value3", iValue3),
		STRFMT_END);

	if( estrOutput )
	{
		size_t sz = estrLength(&estrOutput);
		char *output = exprContextAllocScratchMemory(pContext, sz+1);
		strncpy_s(output, sz+1, estrOutput, sz);
		return output;
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetNumeric2Types);
const char *gclInvExprGetNumeric2Types(ExprContext *pContext, S32 iValue1, S32 iValue2, ACMD_EXPR_DICT(Message) const char *pchMessageKey)
{
	char *estrOutput = NULL;
	estrClear(&estrOutput);
	FormatGameMessageKey(&estrOutput, pchMessageKey,
		STRFMT_INT("Value", iValue1),
		STRFMT_INT("Value1", iValue1),
		STRFMT_INT("Value2", iValue2),
		STRFMT_END);

	if( estrOutput )
	{
		size_t sz = estrLength(&estrOutput);
		char *output = exprContextAllocScratchMemory(pContext, sz+1);
		strncpy_s(output, sz+1, estrOutput, sz);
		return output;
	}
	return "";
}

// Return the value of the given numeric item for the given entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetNumericValue);
S32 gclInvExprInventoryNumericValue(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	if (pEnt)
		return inv_GetNumericItemValue(pEnt, pchItemName);
	else
		return 0;
}

// Return the maximum value for a given numeric.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetNumericMaxValue);
S32 gclInvExprInventoryNumericMaxValue(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	ItemDef *pItemDef = gclInvGetItemDef(pchItemName);
	S32 iMaxPermission;
	if (!pItemDef || pItemDef->eType != kItemType_Numeric)
		return 0;
	iMaxPermission = GamePermissions_trh_GetCachedMaxNumeric(CONTAINER_NOCONST(Entity, pEnt), pchItemName, true);
	return min(iMaxPermission, pItemDef->MaxNumericValue);
}

// Return the true cost of the given numeric item for the given entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetNumericTrueCost);
S32 gclInvExprInventoryNumericTrueCost(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	ItemDef *pItemDef = gclInvGetItemDef(pchItemName);
	if (!pItemDef)
		return 0;
	return item_GetDefEPValue(PARTITION_CLIENT, pEnt, pItemDef, pItemDef->iLevel, pItemDef->Quality);
}

// Return the value of the given numeric item for the given entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetNumericDisplayName);
const char *gclInvExprInventoryNumericDisplayName(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	ItemDef *pItemDef = gclInvGetItemDef(pchItemName);
	if (!pItemDef)
		return NULL;
	return inv_GetNumericItemDisplayName(pEnt, pchItemName);;
}

// Return the icon of the given numeric item
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetNumericIcon);
const char *gclInvExprInventoryGetNumericIcon(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	ItemDef *pItemDef = gclInvGetItemDef(pchItemName);
	if (!pItemDef)
		return NULL;
	return gclGetBestIconName(pItemDef->pchIconName, "default_item_icon");
}

// Return the value of the given numeric item for the given entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryGetGuildNumericValue);
S32 gclInvExprInventoryGuildNumericValue(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	if (pEnt) {
		Guild *pGuild = guild_GetGuild(pEnt);
		return inv_guildbank_GetNumericItemValue(guild_GetGuildBank(pEnt), InvBagIDs_Numeric, pchItemName);
	} else {
		return 0;
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryHasToken);
S32 gclInvExprInventoryHasToken(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	if (pEnt)
		return inv_HasToken(pEnt, pchItemName);
	else
		return 0;
}

// Return the display name for an ItemDef.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemDefGetDisplayName);
const char *gclItemExprDefGetDisplayName(SA_PARAM_OP_VALID ItemDef *pDef)
{
	const char *pch = "";
	if (pDef)
		pch = TranslateDisplayMessage(pDef->displayNameMsg);
	if (!pch && pDef)
		pch = pDef->pchName;
	if (!pch)
		pch = "";
	return pch;
}

// Return an ItemDef for a given name.
// No static check because the data is not loaded on the client.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemDef);
SA_RET_OP_VALID ItemDef *gclItemExprDef(const char *pchItem)
{
	return RefSystem_ReferentFromString("ItemDef", pchItem);
}

// Check if the item at the given inventory key can be traded.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyCanTrade);
bool gclInvExprInventoryKeyCanTrade(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	return pSlot && item_CanTrade(pSlot->pItem);
}

// Check if the item at the given inventory key can be traded.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemCanTrade);
bool gclInvExprItemCanTrade(SA_PARAM_OP_VALID Item *pItem)
{
	return pItem && item_CanTrade(pItem);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetCanTradeError);
const char* gclInvExprInventoryKeyGetCanTradeError(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	if (pSlot)
	{
		TradeErrorType eError = item_GetTradeError(pSlot->pItem);
		if (eError != kTradeErrorType_None)
		{
			return StaticDefineGetTranslatedMessage(TradeErrorTypeEnum, eError);
		}
	}
	return "";
}

// Check if this inventory move is valid.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyMoveValid);
bool gclInvExprInventoryKeyMoveValid(const char *pchKeyA, S32 iDestBag, S32 iDestSlot)
{
	ItemDef *pDef;
	UIInventoryKey KeyA = {0};
	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (KeyA.pOwner != entActivePlayerPtr())
		return false;

	pDef = KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : KeyA.pSlot && KeyA.pSlot->pItem ? GET_REF(KeyA.pSlot->pItem->hItem) : NULL;
	if (!pDef)
		return false;

	if (iDestBag <= InvBagIDs_None)
		iDestBag = GetBestBagForItemDef(KeyA.pEntity, pDef, -1, false, KeyA.pExtract);

	return item_ItemMoveValid(KeyA.pEntity, pDef, inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(iDestBag), iDestBag, iDestSlot, KeyA.pExtract);
}

// Check if this inventory move is valid.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyMoveValidEnt);
bool gclInvExprInventoryKeyMoveValidEnt(const char *pchKeyA, SA_PARAM_OP_VALID Entity *pEntB, S32 iDestBag, S32 iDestSlot)
{
	ItemDef *pDef;
	UIInventoryKey KeyA = {0};
	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (KeyA.pOwner != entActivePlayerPtr() || gclInventoryGetOwner(pEntB) != KeyA.pOwner)
		return false;

	pDef = KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : KeyA.pSlot && KeyA.pSlot->pItem ? GET_REF(KeyA.pSlot->pItem->hItem) : NULL;
	if (!pDef)
		return false;

	if (iDestBag <= InvBagIDs_None)
		iDestBag = GetBestBagForItemDef(pEntB, pDef, -1, false, KeyA.pExtract);

	if (KeyA.pEntity == KeyA.pOwner && KeyA.pEntity == pEntB)
		return item_ItemMoveValid(KeyA.pEntity, pDef, inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(iDestBag), iDestBag, iDestSlot, KeyA.pExtract);

	return item_ItemMoveValidAcrossEnts(KeyA.pOwner, KeyA.pEntity, pDef, inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, pEntB, inv_IsGuildBag(iDestBag), iDestBag, iDestSlot, KeyA.pExtract);
}

// Check if this inventory move is valid.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyMoveValidKey);
bool gclInvExprInventoryKeyMoveValidKey(const char *pchKeyA, const char *pchKeyB)
{
	ItemDef *pDef;
	UIInventoryKey KeyA = {0};
	UIInventoryKey KeyB = {0};
	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (!gclInventoryParseKey(pchKeyB, &KeyB))
		return false;
	if (KeyA.pOwner != entActivePlayerPtr() || KeyB.pOwner != KeyA.pOwner)
		return false;

	pDef = KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : KeyA.pSlot && KeyA.pSlot->pItem ? GET_REF(KeyA.pSlot->pItem->hItem) : NULL;
	if (!pDef)
	{
		pDef = KeyB.pSlotLite ? GET_REF(KeyB.pSlotLite->hItemDef) : KeyB.pSlot && KeyB.pSlot->pItem ? GET_REF(KeyB.pSlot->pItem->hItem) : NULL;
		if (!pDef)
			return false;
		swap(&KeyA, &KeyB, sizeof(KeyA));
	}

	if (KeyA.pEntity == KeyA.pOwner && KeyA.pEntity == KeyB.pEntity)
		return item_ItemMoveValid(KeyA.pEntity, pDef, inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, inv_IsGuildBag(KeyB.eBag), KeyB.eBag, KeyB.iSlot, KeyA.pExtract);

	return item_ItemMoveValidAcrossEnts(KeyA.pOwner, KeyA.pEntity, pDef, inv_IsGuildBag(KeyA.eBag), KeyA.eBag, KeyA.iSlot, KeyB.pEntity, inv_IsGuildBag(KeyB.eBag), KeyB.eBag, KeyB.iSlot, KeyA.pExtract);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(BagGetFlags);
S32 gclExprBagGetFlags(SA_PARAM_OP_VALID Entity* pEnt, S32 eBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);
	return pDef ? pDef->flags : 0;
}

// Return the flags for this item
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetFlags);
S32 gclItemExprGetFlags(SA_PARAM_OP_VALID Item *pItem)
{
	return pItem ? pItem->flags : 0;
}

// Return the flags for this item's def.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetDefFlags);
S32 gclItemExprGetDefFlags(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	return pDef ? pDef->flags : 0;
}

// Return the quality for this item
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetQuality);
S32 gclItemExprGetQuality(SA_PARAM_OP_VALID Item *pItem)
{
	return item_GetQuality(pItem);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetQuality);
S32 gclItemExprInventoryKeyGetQuality(const char *pchInventoryKey)
{
	Item *pItem = gclInvExprInventoryKeyGetItem(pchInventoryKey);
	return item_GetQuality(pItem);
}

// Return the quality for this item
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetQualityName);
const char *gclItemExprGetQualityName(SA_PARAM_OP_VALID Item *pItem)
{
	static const char **s_eaQualities;
	int iQuality = 0;
	if (!s_eaQualities)
	{
		const char **eaKeys = NULL;
		S32 *eaiValues = NULL;
		S32 i;
		DefineFillAllKeysAndValues(ItemQualityEnum, &eaKeys, &eaiValues);
		for (i = 0; i < eaSize(&eaKeys); i++)
		{
			if (eaiValues[i] >= 0)
				eaSet(&s_eaQualities, eaKeys[i], eaiValues[i]);
		}
		eaDestroy(&eaKeys);
		eaiDestroy(&eaiValues);
	}
	iQuality = item_GetQuality(pItem);
	return iQuality >= 0 ? eaGet(&s_eaQualities, iQuality) : "None";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(InventoryKeyGetQualityName);
const char *gclItemExprInventoryKeyGetQualityName(const char *pchInventoryKey)
{
	Item *pItem = gclInvExprInventoryKeyGetItem(pchInventoryKey);
	return pItem ? gclItemExprGetQualityName(pItem) : "None";
}

// Return the flags for this item's def.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetDefType);
const char* gclItemExprGetDefType(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	return pDef ? StaticDefineIntRevLookup(ItemTypeEnum, pDef->eType) : "";
}

// Return the flags for this item's def.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemType);
int gclItemType(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pDef = SAFE_GET_REF(pItem, hItem);
	return SAFE_MEMBER(pDef, eType);
}

// Return the flags for this item's def.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Gemtype);
int gclItemGemType(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pDef = SAFE_GET_REF(pItem, hItem);
	return pDef->eType == kItemType_Gem ? SAFE_MEMBER(pDef, eGemType) : kItemGemType_None;
}

// Return the id for this item.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetID);
const char* gclItemExprGetID(ExprContext *pContext, SA_PARAM_OP_VALID Item *pItem)
{
	if (pItem)
	{
		const int maxsize = 64;
		char *output = exprContextAllocScratchMemory(pContext, maxsize);
		snprintf_s(output, maxsize, "%lld", pItem->id);
		return output;
	}
	return "";
}

// Return true if the entity can access its bank bags.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerCanAccessBank);
bool gclInvExprPlayerCanAccessBank(SA_PARAM_OP_VALID Entity *pEnt)
{
	return true;
}

// Return true if this is a guild bank ID
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(BagIsGuildBankBagID);
bool gclExprBagIsGuildBankBagID(S32 eBagID)
{
	return inv_IsGuildBag(eBagID);
}

// Returns the skill type required to craft the item if you do not have that type
// returns "" if the item is not a recipe or if you meet the specialization requirement
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemPlayerHasCorrectSkillType);
bool gclInvExprItemPlayerHasCorrectSkillType(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	Player *pPlayer = SAFE_MEMBER(pEnt, pPlayer);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pPlayer && pItemDef && pItemDef->pCraft)
	{
		return pPlayer->SkillType == pItemDef->kSkillType;
	}
	return true;
}

// Returns the skill type 
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemSkillType);
int gclInvExprItemSkillType(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
	return SAFE_MEMBER(pItemDef, kSkillType);
}

// Returns 0 if you possess enough skill level to craft the given recipe (or if the item is not a recipe)
// otherwise returns the total skilllevel required to craft the item. 
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemPlayerHasCorrectSkillLevel);
bool gclInvExprItemPlayerHasCorrectSkillLevel(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	Player *pPlayer = SAFE_MEMBER(pEnt, pPlayer);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pPlayer && pItemDef && pItemDef->pCraft)
	{
		U32 iRequiredSkill = SAFE_MEMBER2(pItemDef, pRestriction, iSkillLevel);
		U32 iCurrentSkill = inv_GetNumericItemValue(pEnt, "SkillLevel");
		if (iRequiredSkill > iCurrentSkill)
		{
			return false;
		}
	}
	return true;
}

// Returns the skill specialization required to craft the item if you do not have that specialization
// returns "" if the item is not a recipe or if you meet the specialization requirement
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemPlayerHasCorrectSkillSpecialization);
bool gclInvExprItemPlayerHasCorrectSkillSpecialization(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	Player *pPlayer = SAFE_MEMBER(pEnt, pPlayer);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pPlayer && pItemDef && pItemDef->pCraft)
	{
		ItemDef *pResult = GET_REF(pItemDef->pCraft->hItemResult);
		if (pResult && !entity_CraftingCheckTag(pEnt, pResult->eTag))
		{
			return false;
		}
	}
	return true;
}

// Returns true if the entity has the necessary skill type and skill level to craft the item
// This is equivalent to the previous three expressions and'd together. 
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemMeetsSkillRequirements);
bool gclInvExprItemMeetsSkillRequirements(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	Player *pPlayer = SAFE_MEMBER(pEnt, pPlayer);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	bool bSuccess = true;
	if (pPlayer && pItemDef && pItemDef->pCraft)
	{
		UsageRestriction *pRestriction = pItemDef->pRestriction;
		ItemDef *pResult = GET_REF(pItemDef->pCraft->hItemResult);
		bSuccess &= pPlayer->SkillType == pItemDef->kSkillType;
		if (pResult)
		{
			bSuccess &= entity_CraftingCheckTag(pEnt, pResult->eTag);
		}
		if (pRestriction)
		{
			bSuccess &= (U32)inv_GetNumericItemValue(pEnt, "SkillLevel") >= pRestriction->iSkillLevel;
		}
	}
	return bSuccess;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemPlayerMeetsLevelRequirements);
bool gclInvExprItemPlayerMeetsLevelRequirements(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	int iEntLevel = entity_GetSavedExpLevel(pEnt);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	bool bSuccess = true;
	if (pEnt && pItem && pItemDef)
	{
		UsageRestriction *pRestriction = pItemDef ? pItemDef->pRestriction : NULL;
		if (pItem->flags & kItemFlag_Algo)
		{
			bSuccess &= (pItemDef->flags & kItemDefFlag_NoMinLevel) ||
				!!(pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought)) ||
				item_GetMinLevel(pItem) <= 0 ||
				item_GetMinLevel(pItem) <= iEntLevel;
		}
		else if (pRestriction)
		{
			S32 iMinLevel = pRestriction->iMinLevel;
			if (pItem && item_GetMinLevel(pItem) > iMinLevel && pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought))
				iMinLevel = item_GetMinLevel(pItem);
			if (pItemDef->flags & kItemDefFlag_NoMinLevel)
				iMinLevel = -1;
			if (iMinLevel > 0)
				bSuccess &= iEntLevel >= iMinLevel && (pRestriction->iMaxLevel <= iMinLevel || iEntLevel <= pRestriction->iMaxLevel);
			else if (pRestriction->iMaxLevel > 0)
				bSuccess &= iEntLevel <= pRestriction->iMaxLevel;
		}
	}
	return bSuccess;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemVerifyUsageRestrictionsSkillLevel);
bool gclInvExprItemVerifyUsageRestrictionsSkillLevel(SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
	if (pItemDef && pEnt)
	{
		Entity* pOwner = NULL;
		if (pEnt && pEnt->pSaved)
			pOwner = entFromContainerID(entGetPartitionIdx(pEnt), pEnt->pSaved->conOwner.containerType, pEnt->pSaved->conOwner.containerID);
		if (!pOwner)
			pOwner = pEnt;
		return !pItemDef->pRestriction || itemdef_trh_VerifyUsageRestrictionsSkillLevel(ATR_EMPTY_ARGS, pItemDef->pRestriction, CONTAINER_NOCONST(Entity, pOwner), pItemDef, NULL);
	}
	return false;	
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemVerifyUsageRestrictionsClass);
bool gclInvExprItemVerifyUsageRestrictionsClass(SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
	if (pItemDef && pEnt)
	{
		return !pItemDef->pRestriction || itemdef_trh_VerifyUsageRestrictionsClass(ATR_EMPTY_ARGS, pItemDef->pRestriction, CONTAINER_NOCONST(Entity, pEnt), NULL);
	}
	return false;	
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemVerifyUsageRestrictionsCharacterPath);
bool gclInvExprItemVerifyUsageRestrictionsCharacterPath(SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
	if (pItemDef && pEnt)
	{
		return !pItemDef->pRestriction || itemdef_trh_VerifyUsageRestrictionsCharacterPath(ATR_EMPTY_ARGS, pItemDef->pRestriction, CONTAINER_NOCONST(Entity, pEnt), NULL);
	}
	return false;	
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemVerifyUsageRestrictionsAllegiance);
bool gclInvExprItemVerifyUsageRestrictionsAllegiance(SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEnt)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
	if (pItemDef && pEnt)
	{
		Entity* pOwner = NULL;
		if (pEnt && pEnt->pSaved)
			pOwner = entFromContainerID(entGetPartitionIdx(pEnt), pEnt->pSaved->conOwner.containerType, pEnt->pSaved->conOwner.containerID);
		if (!pOwner)
			pOwner = pEnt;
		return !pItemDef->pRestriction || itemdef_trh_VerifyUsageRestrictionsAllegiance(ATR_EMPTY_ARGS, pItemDef->pRestriction, CONTAINER_NOCONST(Entity, pOwner), CONTAINER_NOCONST(Entity, pEnt), NULL);
	}
	return false;	
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetMissionGrant);
const char *gclInvExprItemGetMissionGrant(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	const char *pchMissionGrant = "None";
	if (pInfo && pItemDef && item_IsMissionGrant(pItemDef))
	{
		MissionDef *pMissionDef = GET_REF(pItemDef->hMission); 
		if (pMissionDef)
		{
			Mission *pMission = mission_GetMissionFromDef(pInfo, pMissionDef);
			pchMissionGrant = pMissionDef->repeatable ? "Repeatable" : "Available";

			if (pMission)
			{
				// This is slightly inaccurate, but since missiondef_CanBeOfferedAsPrimary
				// is only on the server, this is probably a good enough prediction.
				//  * False for finished missions
				//  * False for started/in progress missions
				if (!pMissionDef->repeatable && mission_IsComplete(pMission))
					pchMissionGrant = "Complete";
				else if (pMission->state == MissionState_Started || pMission->state == MissionState_InProgress)
					pchMissionGrant = "InProgress";
			}
		}
	}
	return pchMissionGrant;
}

// Check to see if the given item is a costume unlock and
// if the given player never has never unlocked the costume on the item before
// NB: This is may be very slow.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemIsNewCostumeUnlock);
bool gclInvExprItemIsNewCostumeUnlock(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	SavedEntityData *pSaved = SAFE_MEMBER(pEnt, pSaved);
	GameAccountData *pAccountData = entity_GetGameAccount(pEnt);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	bool bSuccess = false;
	int i;
	if (pSaved && pItemDef && pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock)
	{
		if ((pItem->flags & kItemFlag_Algo) && pItem->pSpecialProps && GET_REF(pItem->pSpecialProps->hCostumeRef))
		{
			bSuccess = !costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEnt, pEnt, REF_STRING_FROM_HANDLE(pItem->pSpecialProps->hCostumeRef));
		}
		else if (eaSize(&pItemDef->ppCostumes) > 0)
		{
			for (i = eaSize(&pItemDef->ppCostumes) - 1; i >= 0; i--)
			{
				if (!costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEnt, pEnt, REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[i]->hCostumeRef)))
				{
					bSuccess = true;
					break;
				}
			}
		}
	}
	return bSuccess;
}

// Check to see if the given item is a costume unlock
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemIsCostumeUnlock);
bool gclInvExprItemIsCostumeUnlock(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	SavedEntityData *pSaved = SAFE_MEMBER(pEnt, pSaved);
	GameAccountData *pAccountData = entity_GetGameAccount(pEnt);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	bool bSuccess = false;
	if (pSaved && pItemDef && pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock)
	{
		if ((pItem->flags & kItemFlag_Algo) && pItem->pSpecialProps && GET_REF(pItem->pSpecialProps->hCostumeRef))
		{
			bSuccess = true;
		}
		else if (eaSize(&pItemDef->ppCostumes) > 0)
		{
			bSuccess = true;
		}
	}
	return bSuccess;
}

// Check to see if the item is may be equipped.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemCanEquip);
bool gclInvExprItemCanEquip(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	bool bResult = false;

	if (pEnt && pItem)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemDef *pDef = GET_REF(pItem->hItem);
		InvBagIDs iDestBag = GetBestBagForItemDef(pEnt, pDef, -1, false, pExtract);
		bResult = bag_IsEquipBag(pEnt, iDestBag, pExtract) && item_ItemMoveDestValid(pEnt, pDef, pItem, false, iDestBag, -1, true, pExtract);
	}

	return bResult;
}

// Check to see if the item when equipped goes into a special bag (weapon, device, equip, recipe, etc).
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemCanEquipGeneral);
bool gclInvExprItemCanEquipGeneral(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	bool bResult = false;

	if (pEnt && pItem)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemDef *pDef = GET_REF(pItem->hItem);
		InvBagIDs iDestBag = GetBestBagForItemDef(pEnt, pDef, -1, false, pExtract);
		// Equipped is being able to move the item into a special bag.
		bResult = (invbag_trh_flags(inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDestBag, pExtract)) & InvBagFlag_SpecialBag) != 0 && item_ItemMoveDestValid(pEnt, pDef, pItem, false, iDestBag, -1, true, pExtract);
	}

	return bResult;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetPetCostume);
const char *gclInvExprItemGetPetCostume(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	PetDef *pPet = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
	CritterDef *pCritter = pPet ? GET_REF(pPet->hCritterDef) : NULL;
	return pCritter && eaSize(&pCritter->ppCostume) > 0 ? REF_STRING_FROM_HANDLE(pCritter->ppCostume[0]->hCostumeRef) : NULL;
}

// returns true if the item has any costumes
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemHasCostume");
bool gclInvExprItemHasCostume(SA_PARAM_OP_VALID Item* pItem)
{
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItem && pItem->pSpecialProps && pItem->pSpecialProps->pTransmutationProps)
		pItemDef = GET_REF(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);

	if (pItem && pItemDef)
	{
		return eaSize(&pItemDef->ppCostumes) > 0;
	}

	return false;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemIsNew);
bool gclInvExprCheckItemIsNew(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_OP_VALID Item *pItem)
{
	if (!pEnt || !pItem || !pEnt->pInventoryV2)
		return false;
	return (eaIndexedFindUsingInt(&pEnt->pInventoryV2->eaiNewItemIDs, pItem->id) != -1);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GetNumNewItems);
bool gclInvExprCheckForAnyNewItems(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (!pEnt || !pEnt->pInventoryV2)
		return 0;
	return eaSize(&pEnt->pInventoryV2->eaiNewItemIDs);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ClearNewItemList);
void gclInvExprClearNewItemList(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (!pEnt || !pEnt->pInventoryV2 || eaSize(&pEnt->pInventoryV2->eaiNewItemIDs) < 1)
		return;
	ServerCmd_ClearNewItemList();
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetPet);
SA_RET_OP_VALID PetDef *gclInvExprItemGetPet(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	return pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenItemGetPowerFactor);
int gclInvExprItemGetPowerFactor(SA_PARAM_OP_VALID Item *pItem)
{
	return item_GetPowerFactor(pItem);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenItemDefGetPowerFactor);
int gclInvExprItemDefGetPowerFactor(SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	return pItemDef ? pItemDef->iPowerFactor : 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetSpecies);
SA_RET_OP_VALID const char *gclInvExprItemGetSpecies(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItemDef)
	{
		SpeciesDef *pSpecies = GET_REF(pItemDef->hSpecies);
		if (pSpecies)
			return pSpecies->pcName;
		else if (IS_HANDLE_ACTIVE(pItemDef->hSpecies))
			return REF_STRING_FROM_HANDLE(pItemDef->hSpecies);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemDefGetSpecies);
SA_RET_OP_VALID const char *gclInvExprItemDefGetSpecies(SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	if (pItemDef)
	{
		SpeciesDef *pSpecies = GET_REF(pItemDef->hSpecies);
		if (pSpecies)
			return pSpecies->pcName;
		else if (IS_HANDLE_ACTIVE(pItemDef->hSpecies))
			return REF_STRING_FROM_HANDLE(pItemDef->hSpecies);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemDefGetUsageRestrictionCategory);
S32 gclItemDefExprGetUsageRestrictionCategory(SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	return pItemDef && pItemDef->pRestriction ? pItemDef->pRestriction->eUICategory : UsageRestrictionCategory_None;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemGetUsageRestrictionCategory);
S32 gclItemExprGetUsageRestrictionCategory(SA_PARAM_OP_VALID Item *pItem)
{
	return pItem ? gclItemDefExprGetUsageRestrictionCategory(GET_REF(pItem->hItem)) : UsageRestrictionCategory_None;
}

// This returns a zero value if the item may not be used by the given allegiance, and a negative number if there are no allegiance restrictions on the item.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemDefHasAllegianceRestriction);
S32 gclItemDefExprHasAllegianceRestriction(SA_PARAM_OP_VALID ItemDef *pItemDef, const char *pchAllegiance)
{
	if (pItemDef && pItemDef->pRestriction && pchAllegiance && pchAllegiance[0])
	{
		S32 i;
		for (i = eaSize(&pItemDef->pRestriction->eaRequiredAllegiances) - 1; i >= 0; --i)
		{
			AllegianceRef *pRef = pItemDef->pRestriction->eaRequiredAllegiances[i];
			if (GET_REF(pRef->hDef))
			{
				AllegianceDef *pDef = GET_REF(pRef->hDef);
				if (!stricmp(pDef->pcName, pchAllegiance))
					return 1;
			}
			else if (REF_STRING_FROM_HANDLE(pRef->hDef) == pchAllegiance)
			{
				return 1;
			}
		}
		return 0;
	}
	return -1;
}

// This returns a zero value if the item may not be used by the given allegiance, and a negative number if there are no allegiance restrictions on the item.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ItemHasAllegianceRestriction);
S32 gclItemExprHasAllegianceRestriction(SA_PARAM_OP_VALID Item *pItem, const char *pchAllegiance)
{
	return pItem ? gclItemDefExprHasAllegianceRestriction(GET_REF(pItem->hItem), pchAllegiance) : -1;
}

void gclInvSetCategoryInfo(UIItemCategory *pUICategory, ItemCategory eCategory, const char *pchName, const char *pchNamePrefix)
{
	pUICategory->eCategory = eCategory;
	pUICategory->pchCategoryName = allocAddString(pchName);
	pUICategory->pchCategoryNameWithoutPrefix = pchNamePrefix ? allocAddString(pchName + strlen(pchNamePrefix)) : allocAddString(pchName);
	pUICategory->pchCategoryNamePrefix = pchNamePrefix ? allocAddString(pchNamePrefix) : NULL;
	pUICategory->pchDisplayName = StaticDefineGetTranslatedMessage(ItemCategoryEnum, eCategory);
	pUICategory->pCategoryData = eaGet(&g_ItemCategoryNames.ppInfo, eCategory - kItemCategory_FIRST_DATA_DEFINED);
}

int gclInvCompareUICategoryOrdered(const UIItemCategory **ppLeft, const UIItemCategory **ppRight, const char * const* const*peaPrefixes)
{
	static const char * const* const* s_peaTablePrefixes;
	static StashTable s_stTablePrefixes;

	S32 iSortOrderLeft = (*ppLeft)->pCategoryData ? (*ppLeft)->pCategoryData->iSortOrder : 0;
	S32 iSortOrderRight = (*ppRight)->pCategoryData ? (*ppRight)->pCategoryData->iSortOrder : 0;
	S32 iGrandPosLeft = -1, iGrandPosRight = -1;
	int diff;

	if (s_peaTablePrefixes != peaPrefixes)
	{
		S32 i;

		if (!s_stTablePrefixes)
			s_stTablePrefixes = stashTableCreateAddress(256);
		else
			stashTableClear(s_stTablePrefixes);

		for (i = 0; i < eaSize(peaPrefixes); i++)
			stashAddInt(s_stTablePrefixes, (*peaPrefixes)[i], i, true);

		s_peaTablePrefixes = peaPrefixes;
	}

	// The whole "Grand" positioning is to make it so that the UI can provide the ordering of the
	// categories within the filter prefix.
	if (!stashFindInt(s_stTablePrefixes, (*ppLeft)->pchCategoryName, &iGrandPosLeft) && !stashFindInt(s_stTablePrefixes, (*ppLeft)->pchCategoryName, &iGrandPosLeft))
		iGrandPosLeft = -1;
	if (!stashFindInt(s_stTablePrefixes, (*ppRight)->pchCategoryName, &iGrandPosRight) && !stashFindInt(s_stTablePrefixes, (*ppRight)->pchCategoryName, &iGrandPosRight))
		iGrandPosRight = -1;

	if (iGrandPosLeft < 0 && iGrandPosRight >= 0)
		return 1;
	if (iGrandPosLeft >= 0 && iGrandPosRight < 0)
		return -1;
	if (iGrandPosLeft >= 0 && iGrandPosRight >= 0 && iGrandPosLeft != iGrandPosRight)
		return iGrandPosLeft - iGrandPosRight;

	if (iSortOrderLeft != iSortOrderRight)
		return iSortOrderLeft - iSortOrderRight;

	diff = stricmp_safe((*ppLeft)->pchDisplayName, (*ppRight)->pchDisplayName);
	if (diff != 0)
		return diff;

	diff = stricmp_safe((*ppLeft)->pchCategoryName, (*ppRight)->pchCategoryName);
	if (diff != 0)
		return diff;

	return (*ppLeft)->eCategory - (*ppRight)->eCategory;
}

int gclInvCompareUICategoryOrdered_s(const char * const* const*peaPrefixes, const UIItemCategory **ppLeft, const UIItemCategory **ppRight)
{
	return gclInvCompareUICategoryOrdered(ppLeft, ppRight, peaPrefixes);
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InventoryBagsGetItemCategories);
void gclInvExprInventoryBagsGetItemCategories(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchBags, const char *pchPrefix)
{
	UIItemCategory ***peaItemCategories = ui_GenGetManagedListSafe(pGen, UIItemCategory);
	static const char **s_eaPrefixes = NULL;
	static UIItemCategory **s_eaMasterList = NULL;
	static S32 s_iBitSize;
	char *pchBuffer = NULL;
	char *pchContext = NULL;
	char *pchToken;
	S32 i, j, iCount = 0;
	U32 *pbfIncludeCategories, *pbfExcludeCategories;

	if (!s_eaMasterList)
	{
		const char **eaKeys = NULL;
		S32 *eaiValues = NULL;
		S32 iMaxValue = 1;

		DefineFillAllKeysAndValues(ItemCategoryEnum, &eaKeys, &eaiValues);
		for (i = 0; i < eaSize(&eaKeys); i++)
		{
			const char *pchKey = eaGet(&eaKeys, i);
			S32 iValue = eaiGet(&eaiValues, i);
			UIItemCategory *pNewCategory = eaGetStruct(&s_eaMasterList, parse_UIItemCategory, i);
			pNewCategory->eCategory = iValue;
			pNewCategory->pchCategoryName = pchKey;
			MAX1(iMaxValue, iValue + 1);
		}
		eaDestroy(&eaKeys);
		eaiDestroy(&eaiValues);

		s_iBitSize = ((iMaxValue + 31) / 32) * 4;
	}

	pbfIncludeCategories = (U32 *)memset(alloca(s_iBitSize), 0, s_iBitSize);
	pbfExcludeCategories = (U32 *)memset(alloca(s_iBitSize), 0, s_iBitSize);

	strdup_alloca(pchBuffer, pchPrefix);
	eaClearFast(&s_eaPrefixes);
	if (pchToken = strtok_r(pchBuffer, " \r\n\t,|", &pchContext))
	{
		do
		{
			eaPush(&s_eaPrefixes, allocAddString(pchToken));
		} while (pchToken = strtok_r(NULL, " \r\n\t,|", &pchContext));
	}

	if (!pchBags || !pEnt)
	{
		// Go through the entire list of categories
		for (i = 0; i < eaSize(&s_eaMasterList); i++)
		{
			if (eaSize(&s_eaPrefixes))
			{
				const char *pchName = s_eaMasterList[i]->pchCategoryName;
				for (j = 0; j < eaSize(&s_eaPrefixes); j++)
				{
					if (strStartsWith(pchName, s_eaPrefixes[j]))
					{
						SETB(pbfIncludeCategories, s_eaMasterList[i]->eCategory);
						break;
					}
				}
			}
			else
			{
				SETB(pbfIncludeCategories, s_eaMasterList[i]->eCategory);
			}
		}
	}
	else
	{
		// Go through all the items the bags
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		strdup_alloca(pchBuffer, pchBags);
		if (pchToken = strtok_r(pchBuffer, " \r\n\t,|", &pchContext))
		{
			do
			{
				InvBagIDs eBag = StaticDefineIntGetInt(InvBagIDsEnum, pchToken);
				InventoryBag *pBag = eBag >= 0 ? CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBag, pExtract)) : NULL;
				if (pBag)
				{
					for (i = eaSize(&pBag->ppIndexedInventorySlots) - 1; i >= 0; i--)
					{
						InventorySlot *pSlot = pBag->ppIndexedInventorySlots[i];
						Item *pItem = pSlot ? pSlot->pItem : NULL;
						ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
						if (pItemDef)
						{
							for (j = eaiSize(&pItemDef->peCategories) - 1; j >= 0; j--)
							{
								ItemCategory eCategory = pItemDef->peCategories[j];
								const char *pchName;
								S32 k;
								if (TSTB(pbfIncludeCategories, eCategory) || TSTB(pbfExcludeCategories, eCategory))
									continue;
								if (eCategory >= s_iBitSize * 32)
									continue;
								pchName = StaticDefineIntRevLookup(ItemCategoryEnum, eCategory);
								if (!pchName)
								{
									SETB(pbfExcludeCategories, eCategory);
									continue;
								}
								for (k = 0; k < eaSize(&s_eaPrefixes); k++)
								{
									if (strStartsWith(pchName, s_eaPrefixes[k]))
									{
										SETB(pbfIncludeCategories, eCategory);
										break;
									}
								}
							}
						}
					}
				}
			} while (pchToken = strtok_r(NULL, " \r\n\t,|", &pchContext));
		}
	}

	// Set array from bitfield
	for (i = eaSize(&s_eaMasterList) - 1; i >= 0; i--)
	{
		if (TSTB(pbfIncludeCategories, i))
		{
			UIItemCategory *pUICategory = NULL;
			const char *pchNamePrefix = NULL;

			for (j = iCount; j < eaSize(peaItemCategories); j++)
			{
				if ((*peaItemCategories)[j]->eCategory == i)
				{
					if (j != iCount)
						eaMove(peaItemCategories, iCount, j);
					pUICategory = (*peaItemCategories)[iCount++];
					break;
				}
			}
			if (!pUICategory)
			{
				pUICategory = StructCreate(parse_UIItemCategory);
				eaInsert(peaItemCategories, pUICategory, iCount++);
			}

			if (eaSize(&s_eaPrefixes))
			{
				const char *pchName = s_eaMasterList[i]->pchCategoryName;
				for (j = 0; j < eaSize(&s_eaPrefixes); j++)
				{
					if (strStartsWith(pchName, s_eaPrefixes[j]))
					{
						pchNamePrefix = s_eaPrefixes[j];
						break;
					}
				}
			}

			gclInvSetCategoryInfo(pUICategory, i, s_eaMasterList[i]->pchCategoryName, pchNamePrefix);
		}
	}

	eaSetSizeStruct(peaItemCategories, parse_UIItemCategory, iCount);

	PERFINFO_AUTO_START("Sort Categories", 1);
	eaStableSort(*peaItemCategories, &s_eaPrefixes, gclInvCompareUICategoryOrdered);
	PERFINFO_AUTO_STOP();

	ui_GenSetManagedListSafe(pGen, peaItemCategories, UIItemCategory, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemCategoryGetCategories);
void gclInvExprItemCategoryGetCategories(SA_PARAM_NN_VALID UIGen *pGen, const char *pchPrefix)
{
	gclInvExprInventoryBagsGetItemCategories(pGen, NULL, NULL, pchPrefix);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemDefGetCategories);
bool gclInvExprItemDefGetCategories(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ItemDef *pItemDef, SA_PARAM_NN_STR const char *pchPrefix)
{
	UIItemCategory ***peaItemCategories = ui_GenGetManagedListSafe(pGen, UIItemCategory);
	static const char **s_eaCategoryNames = NULL;
	static S32 *s_eaiCategoryValues = NULL;
	static struct {
		char *pchInput;
		const char **eaPrefixes;
		const char **apchCategories;
		U32 uLastUseTime;
	} s_aCache[16];
	static ItemCategory s_eMaxCategory;
	S32 i, j, iCount = 0;
	S32 iCache = -1, iOldestCache = -1;

	if (!s_aCache[ARRAY_SIZE(s_aCache) - 1].apchCategories)
	{
		DefineFillAllKeysAndValues(ItemCategoryEnum, &s_eaCategoryNames, &s_eaiCategoryValues);
		for (i = 0; i < eaiSize(&s_eaiCategoryValues); i++)
			MAX1(s_eMaxCategory, s_eaiCategoryValues[i]);
		for (i = 0; i < ARRAY_SIZE(s_aCache); i++)
		{
			if (!s_aCache[i].apchCategories)
			{
				s_aCache[i].apchCategories = (const char **) malloc(sizeof(const char *) * s_eMaxCategory);
				if (!s_aCache[i].apchCategories)
					return false;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(s_aCache); i++)
	{
		if (!s_aCache[i].pchInput || iOldestCache < 0 || s_aCache[i].uLastUseTime < s_aCache[iOldestCache].uLastUseTime)
			iOldestCache = i;

		if (!s_aCache[i].pchInput || !stricmp(pchPrefix, s_aCache[i].pchInput))
		{
			iCache = i;
			break;
		}
	}
	if (iCache < 0)
		iCache = iOldestCache;

	if (!s_aCache[iCache].pchInput || stricmp(pchPrefix, s_aCache[iCache].pchInput))
	{
		char *pchPrefixBuffer = NULL;
		char *pchContext = NULL;
		char *pchToken;

		if (s_aCache[iCache].pchInput)
			StructFreeString(s_aCache[iCache].pchInput);
		s_aCache[iCache].pchInput = StructAllocString(pchPrefix);

		PERFINFO_AUTO_START("Parse Params", 1);

		strdup_alloca(pchPrefixBuffer, pchPrefix);
		eaClearFast(&s_aCache[iCache].eaPrefixes);
		if (pchToken = strtok_r(pchPrefixBuffer, " \r\n\t,|", &pchContext))
		{
			do
			{
				eaPush(&s_aCache[iCache].eaPrefixes, allocAddString(pchToken));
			} while (pchToken = strtok_r(NULL, " \r\n\t,|", &pchContext));
		}

		PERFINFO_AUTO_STOP();

		// Setup the category mask
		if (eaSize(&s_aCache[iCache].eaPrefixes))
		{
			PERFINFO_AUTO_START("Preprocess Categories", 1);

			memset((void *)s_aCache[iCache].apchCategories, 0, sizeof(const char *) * s_eMaxCategory);
			for (i = eaSize(&s_eaCategoryNames) - 1; i >= 0; i--)
			{
				for (j = 0; j < eaSize(&s_aCache[iCache].eaPrefixes); j++)
				{
					if (strStartsWith(s_eaCategoryNames[i], s_aCache[iCache].eaPrefixes[j]))
					{
						s_aCache[iCache].apchCategories[s_eaiCategoryValues[i]] = s_aCache[iCache].eaPrefixes[j];
						break;
					}
				}
			}

			PERFINFO_AUTO_STOP();
		}
	}

	if (pItemDef)
	{
		for (i = 0; i < eaiSize(&pItemDef->peCategories); i++)
		{
			ItemCategory eCategory = pItemDef->peCategories[i];
			const char *pchName = NULL;

			if (eaSize(&s_aCache[iCache].eaPrefixes) && (eCategory >= s_eMaxCategory || !s_aCache[iCache].apchCategories[eCategory]))
				continue;

			pchName = gclInvGetCategoryName(eCategory);
			if (pchName)
			{
				UIItemCategory *pUICategory = NULL;
				const char *pchNamePrefix = NULL;

				if (eaSize(&s_aCache[iCache].eaPrefixes))
				{
					pchNamePrefix = s_aCache[iCache].apchCategories[eCategory];
				}

				PERFINFO_AUTO_START("Add Category", 1);

				for (j = 0; j < eaSize(peaItemCategories); j++)
				{
					if ((*peaItemCategories)[j]->eCategory == eCategory)
					{
						if (j != iCount)
						{
							UIItemCategory *pTemp = (*peaItemCategories)[j];
							(*peaItemCategories)[j] = (*peaItemCategories)[iCount];
							(*peaItemCategories)[iCount] = pTemp;
						}
						pUICategory = (*peaItemCategories)[iCount++];
						break;
					}
				}

				if (!pUICategory)
					pUICategory = eaGetStruct(peaItemCategories, parse_UIItemCategory, iCount++);

				PERFINFO_AUTO_STOP();

				gclInvSetCategoryInfo(pUICategory, eCategory, pchName, pchNamePrefix);
			}
		}
	}

	eaSetSizeStruct(peaItemCategories, parse_UIItemCategory, iCount);

	PERFINFO_AUTO_START("Sort Categories", 1);
	eaQSort_s(*peaItemCategories, (int (*)(void *, const void *, const void *))gclInvCompareUICategoryOrdered_s, (void *)&s_aCache[iCache].eaPrefixes);
	PERFINFO_AUTO_STOP();

	ui_GenSetManagedListSafe(pGen, peaItemCategories, UIItemCategory, true);
	return pItemDef != NULL;
}

void gclInvCreateCategoryList(SA_PARAM_NN_VALID UIGen *pGen, ItemCategory *peCategories, const char *pchPrefix)
{
	static const char **s_eaPrefixes = NULL;
	UIItemCategory ***peaItemCategories = ui_GenGetManagedListSafe(pGen, UIItemCategory);
	S32 i, j, iCount = 0;
	char *pchPrefixBuffer = NULL;
	char *pchContext = NULL;
	char *pchToken;

	strdup_alloca(pchPrefixBuffer, pchPrefix);
	eaClearFast(&s_eaPrefixes);
	if (pchToken = strtok_r(pchPrefixBuffer, " \r\n\t,|", &pchContext))
	{
		do
		{
			eaPush(&s_eaPrefixes, allocAddString(pchToken));
		} while (pchToken = strtok_r(NULL, " \r\n\t,|", &pchContext));
	}

	for (i = 0; i < eaiSize(&peCategories); i++)
	{
		ItemCategory eCategory = peCategories[i];
		const char *pchName = gclInvGetCategoryName(eCategory);
		if (pchName)
		{
			UIItemCategory *pUICategory = NULL;

			for (j = iCount; j < eaSize(peaItemCategories); j++)
			{
				if ((*peaItemCategories)[j]->eCategory == eCategory)
				{
					pUICategory = (*peaItemCategories)[j];
					(*peaItemCategories)[j] = (*peaItemCategories)[iCount];
					(*peaItemCategories)[iCount] = pUICategory;
					iCount++;
					break;
				}
			}

			if (!pUICategory)
				pUICategory = eaGetStruct(peaItemCategories, parse_UIItemCategory, iCount++);
			gclInvSetCategoryInfo(pUICategory, eCategory, pchName, NULL);
		}
	}

	eaSetSizeStruct(peaItemCategories, parse_UIItemCategory, iCount);

	PERFINFO_AUTO_START("Sort Categories", 1);
	eaStableSort(*peaItemCategories, &s_eaPrefixes, gclInvCompareUICategoryOrdered);
	PERFINFO_AUTO_STOP();

	ui_GenSetManagedListSafe(pGen, peaItemCategories, UIItemCategory, true);
}

// returns a string list of space delimited category enums as
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InventorySlotGetItemDefGetCategories);
const char *gclInvExprInventory(ExprContext *pContext, SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	if (pSlot && pSlot->pItem)
	{
		ItemDef *pDef = GET_REF(pSlot->pItem->hItem);
		if (pDef)
		{
			S32 i, stringLen, numCategories = 0;
			char ach[1024], *pCurIdx;
			
			stringLen = 0;
			ach[0] = 0;
			pCurIdx = ach;
			
			numCategories = eaiSize(&pDef->peCategories);
			for (i = 0; i < numCategories; ++i)
			{
				char buffer[20] = {0};
				S32 len;

				itoa(pDef->peCategories[i], buffer, 10);
				len = strlen(buffer);
				stringLen += len;
				
				strcpy_s(pCurIdx, 1024 - stringLen, buffer);

				pCurIdx += len;
				if (i+1 < numCategories)
				{
					*pCurIdx = ',';
					pCurIdx++;
					*pCurIdx = 0;
					stringLen++;
				}
			}

			return exprContextAllocString(pContext, ach);
		}
	}
	return "";
}


static int gclInvCompareUICategorizedInventorySlotOrdered(const UICategorizedInventorySlot **ppLeft, const UICategorizedInventorySlot **ppRight, const void *pPrefixArray)
{
	const UIItemCategory *pLeftCategory = &(*ppLeft)->UICategory;
	const UIItemCategory *pRightCategory = &(*ppRight)->UICategory;
	const char *pchLeftName, *pchRightName;
	Entity *pPlayer;
	int iNameDiff, iSlotDiff;
	int iCategoryDiff;

	// check if for locked
	if ((*ppLeft)->bLocked != (*ppRight)->bLocked)
		return ((S32)(*ppLeft)->bLocked - (S32)(*ppRight)->bLocked);
	if ((*ppLeft)->bLocked)
		return *ppLeft < *ppRight ? 1 : *ppLeft > *ppRight ? -1 : 0;

	iCategoryDiff = gclInvCompareUICategoryOrdered(&pLeftCategory, &pRightCategory, pPrefixArray);
	if (iCategoryDiff != 0)
		return iCategoryDiff;
		
	if ((!(*ppLeft)->pSlot) != (!(*ppRight)->pSlot))
		return -((!(*ppLeft)->pSlot) - (!(*ppRight)->pSlot));
	
	// If neither have a slot, can't go any further
	if (!(*ppLeft)->pSlot)
		return *ppLeft < *ppRight ? -1 : *ppLeft > *ppRight ? 1 : 0;

	if ((!(*ppLeft)->pSlot->pItem) != (!(*ppRight)->pSlot->pItem))
		return (!(*ppLeft)->pSlot->pItem) - (!(*ppRight)->pSlot->pItem);

	// If neither have an item, can't go any further
	if (!(*ppLeft)->pSlot->pItem)
		return stricmp((*ppLeft)->pchSlotKey, (*ppRight)->pchSlotKey);

	// Compare the names
	// NB: for speed purposes, assume the display name on the Item is the name to be displayed.
	pPlayer = entActivePlayerPtr();
	pchLeftName = (*ppLeft)->pSlot->pItem->pchDisplayName;
	if (!pchLeftName || !pchLeftName[0])
		pchLeftName = item_GetName((*ppLeft)->pSlot->pItem, pPlayer);
	pchRightName = (*ppLeft)->pSlot->pItem->pchDisplayName;
	if (!pchRightName || !pchRightName[0])
		pchRightName = item_GetName((*ppRight)->pSlot->pItem, pPlayer);
	iNameDiff = stricmp_safe(pchLeftName, pchRightName);
	if (iNameDiff != 0)
		return iNameDiff;

	iSlotDiff = stricmp_safe((*ppLeft)->pSlot->pchName, (*ppRight)->pSlot->pchName);
	if (iSlotDiff != 0)
		return iSlotDiff;
	return *ppLeft < *ppRight ? -1 : *ppLeft > *ppRight ? 1 : 0;
}

static ItemCategory gclInvFindBestCategory(ItemDef *pItemDef, const char **eaPrefixes, const char **ppchBestPrefix)
{
	S32 i, j;
	ItemCategory eBestCategory = kItemCategory_None;
	S32 iBestIndex = -1;

	if (ppchBestPrefix)
		*ppchBestPrefix = NULL;

	if (!pItemDef || !eaSize(&eaPrefixes))
		return kItemCategory_None;

	for (i = 0; i < eaiSize(&pItemDef->peCategories); i++)
	{
		ItemCategory eCategory = pItemDef->peCategories[i];
		const char *pchName = gclInvGetCategoryName(eCategory);
		if (pchName)
		{
			for (j = 0; j < eaSize(&eaPrefixes); j++)
			{
				if (strStartsWith(pchName, eaPrefixes[j]) && (iBestIndex < 0 || j < iBestIndex || eCategory < eBestCategory))
				{
					eBestCategory = eCategory;
					iBestIndex = j;
					if (ppchBestPrefix)
						*ppchBestPrefix = eaPrefixes[j];
				}
			}
		}
	}

	return eBestCategory;
}

static UICategorizedInventorySlot *gclInvCreateCategorizedSlot(UICategorizedInventorySlot ***peaSlots, const char *pchName, S32 iCount)
{
	S32 i, n = eaSize(peaSlots);
	UICategorizedInventorySlot *pCategorizedSlot;
	for (i = iCount; i < n; i++)
	{
		pCategorizedSlot = (*peaSlots)[i];
		if (!stricmp(pCategorizedSlot->pchSlotKey, pchName))
		{
			if (i != iCount)
			{
				// Swap the slots
				(*peaSlots)[i] = (*peaSlots)[iCount];
				(*peaSlots)[iCount] = pCategorizedSlot;
			}
			pCategorizedSlot->bLocked = false;
			pCategorizedSlot->iAdditionalCount = 0;
			return pCategorizedSlot;
		}
	}
	pCategorizedSlot = StructCreate(parse_UICategorizedInventorySlot);
	pCategorizedSlot->iAdditionalCount = 0;
	pCategorizedSlot->pchSlotKey = StructAllocString(pchName);
	eaInsert(peaSlots, pCategorizedSlot, iCount);
	return pCategorizedSlot;
}

bool gclInvCreateCategorizedSlots(UIGen *pGen, Entity *pBagEnt, InventoryBag **eaBags, InventorySlot **eaSlots, const char *pchPrefix, U32 uOptions)
{
	UICategorizedInventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, UICategorizedInventorySlot);
	static const char **s_eaPrefixes = NULL;
	static char ach[1024];

	Entity *pEnt = gclInventoryGetOwner(pBagEnt);
	char *pchPrefixBuffer = NULL;
	char *pchContext = NULL;
	char *pchToken;
	S32 i, j, iCount = 0;
	S32 aiCategoryBuffer[64]; // Limited to 2,048 categories...
	UIInventoryKey Key = {0};

	if (!pBagEnt)
		pEnt = NULL;
	if (pEnt == pBagEnt)
		pBagEnt = NULL;
	if (!pEnt)
	{
		eaClearStruct(peaSlots, parse_UICategorizedInventorySlot);
		ui_GenSetManagedListSafe(pGen, peaSlots, UICategorizedInventorySlot, true);
		return pGen && eaSize(peaSlots);
	}

	Key.erOwner = pEnt ? entGetRef(pEnt) : 0;
	Key.eType = pBagEnt ? entGetType(pBagEnt) : GLOBALTYPE_NONE;
	Key.iContainerID = pBagEnt ? entGetContainerID(pBagEnt) : 0;

	memset(aiCategoryBuffer, 0, sizeof(aiCategoryBuffer));

	strdup_alloca(pchPrefixBuffer, pchPrefix);

	// s_eaPrefixes will contain pointers to data on the stack...
	eaClearFast(&s_eaPrefixes);

	if (pchToken = strtok_r(pchPrefixBuffer, " \r\n\t,|%", &pchContext))
	{
		do
		{
			eaPush(&s_eaPrefixes, allocAddString(pchToken));
		} while (pchToken = strtok_r(NULL, " \r\n\t,|%", &pchContext));
	}

	if (pEnt && peaSlots && eaSize(&eaSlots))
	{
		for (i = 0; i < eaSize(&eaSlots); i++)
		{
			UICategorizedInventorySlot *pSlot = NULL;
			InventorySlot *pInvSlot = eaSlots[i];
			Item *pItem = SAFE_MEMBER(pInvSlot, pItem);
			ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
			const char *pchNamePrefix = NULL;
			ItemCategory eCategory = gclInvFindBestCategory(pItemDef, s_eaPrefixes, &pchNamePrefix);
			const char *pchName;
			InventoryBag *pBag = NULL;

			for (j = eaSize(&eaBags) - 1; j >= 0; j--)
			{
				if (eaFind(&eaBags[j]->ppIndexedInventorySlots, pInvSlot) >= 0)
				{
					pBag = eaBags[j];
					break;
				}
			}

			if (!pBag)
				pBag = gclInventoryGetSlotBag(pInvSlot, NULL);

			if (!pBag || !pItemDef && (!pInvSlot || pInvSlot->pItem || (uOptions & UIInvFilterFlag_EmptySlots) == 0))
				continue;

			gclInventoryUpdateSlot(pBagEnt, pBag, pInvSlot);

			if (!devassertmsg(eCategory < 32 * ARRAY_SIZE(aiCategoryBuffer), "Unsupported number of categories."))
			{
				eCategory = kItemCategory_None;
				pchNamePrefix = NULL;
			}

			pchName = gclInvGetCategoryName(eCategory);

			if ((uOptions & UIItemCategoryFlag_CategorizedInventoryHeaders) && !TSTB(aiCategoryBuffer, eCategory))
			{
				pSlot = gclInvCreateCategorizedSlot(peaSlots, pchName, iCount++);
				gclInvSetCategoryInfo(&pSlot->UICategory, eCategory, pchName, pchNamePrefix);
				pSlot->pSlot = NULL;
				SETB(aiCategoryBuffer, eCategory);
			}
			
			if (uOptions & UIInvFilterFlag_DupesIncreaseCount)
			{
				bool bFoundDupe = false;
				S32 xx;
				for (xx = 0; xx < iCount; ++xx)
				{
					UICategorizedInventorySlot *pCatInvSlot = (*peaSlots)[xx];
					if (pCatInvSlot && REF_COMPARE_HANDLES(pCatInvSlot->pSlot->pItem->hItem, pInvSlot->pItem->hItem))
					{
						pCatInvSlot->iAdditionalCount ++;
						bFoundDupe = true;
						break;
					}
				}
									
				if (bFoundDupe)
					continue;
			}

			Key.eBag = pBag->BagID;
			Key.pchName = pInvSlot->pchName;

			pSlot = gclInvCreateCategorizedSlot(peaSlots, gclInventoryMakeKeyString(NULL, &Key), iCount++);
			gclInvSetCategoryInfo(&pSlot->UICategory, eCategory, pchName, pchNamePrefix);
			pSlot->pSlot = pInvSlot;
			// pSlot->pSlot->pItem->count
		}
	}
		
	if (uOptions & UIInvFilterFlag_LockedSlots)
	{
		FOR_EACH_IN_EARRAY(eaBags, InventoryBag, pBag)
		{
			S32 iMaxUnlockedSlots = inv_GetBagMaxSlotTableMaxSlots(pBag);
			S32 iMaxSlots = invbag_maxslots(pEnt, pBag);

			Key.eBag = pBag->BagID;

			for (i = iMaxSlots; i < iMaxUnlockedSlots; i++)
			{
				InventorySlot *pInvSlot = gclInventoryUpdateNullSlot(NULL, pBag, i);
				if (pInvSlot)
				{
					UICategorizedInventorySlot *pSlot = NULL;
					
					Key.pchName = pInvSlot->pchName;
					pSlot = gclInvCreateCategorizedSlot(peaSlots, gclInventoryMakeKeyString(NULL, &Key), iCount++);
					if (pSlot)
					{
						pSlot->bLocked = true;
						pSlot->pSlot = pInvSlot;
					}
				}
			}
		}
		FOR_EACH_END
	}

	eaSetSizeStruct(peaSlots, parse_UICategorizedInventorySlot, iCount);
	eaStableSort(*peaSlots, &s_eaPrefixes, gclInvCompareUICategorizedInventorySlotOrdered);
	ui_GenSetManagedListSafe(pGen, peaSlots, UICategorizedInventorySlot, true);
	return pGen && eaSize(peaSlots);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InventoryBagGetCategorizedInventory);
bool gclInvExprGenInventoryBagGetCategorizedInventory(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventoryBag *pBag, const char *pchPrefix, U32 uOptions)
{
	static InventorySlot **s_eaSlots;
	Entity *pEnt = gclInventoryGetBagEntity(pBag);
	InventoryBag **eaBags = NULL;
	bool bRet = false;

	eaClearFast(&s_eaSlots);

	if (pBag)
	{
		S32 i;
		UICategorizedInventorySlot *pSlot = NULL;
		S32 iMaxSlots = invbag_maxslots(pEnt, pBag);

		for (i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); i++)
		{
			InventorySlot *pInvSlot = pBag->ppIndexedInventorySlots[i];
			Item *pItem = SAFE_MEMBER(pInvSlot, pItem);
			ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

			if (!pItemDef && !(uOptions & UIInvFilterFlag_EmptySlots))
				continue;

			eaPush(&s_eaSlots, gclInventoryUpdateSlot(NULL, pBag, pInvSlot));
		}

		// Add empty slots up to max
		for (; (uOptions & UIInvFilterFlag_EmptySlots) && i < iMaxSlots; i++)
			eaPush(&s_eaSlots, gclInventoryUpdateNullSlot(NULL, pBag, i));

		eaStackCreate(&eaBags, 1);
		eaPush(&eaBags, pBag);
	}
	
	bRet = gclInvCreateCategorizedSlots(pGen, pEnt, eaBags, s_eaSlots, pchPrefix, uOptions);

	eaDestroy(&eaBags);

	return bRet;
}

// Bloody over complicated expression function that adds items from multiple bags,
// and filters, and sorts by given prefixes.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InventoryMultiBagGetCategorizedInventory);
bool gclInvExprGenInventoryMultiBagGetCategorizedInventory(	SA_PARAM_NN_VALID UIGen *pGen, 
															SA_PARAM_OP_VALID Entity *pEnt, 
															const char *pchBagIds, 
															const char *pchPrefix, 
															const char *pchIncludeCategories, 
															const char *pchExcludeCategories, 
															U32 uOptions)
{
	static InventoryBag **s_eaInventoryBags = NULL;
	static InventorySlot **s_eaSlots = NULL;
	static PerfInfoStaticData **s_eaPerfStatic = NULL;
	static const char **s_eaPrefixes = NULL;
	static ItemCategory *s_eaiIncludeCategories = NULL;
	static ItemCategory *s_eaiExcludeCategories = NULL;
	ItemAssignmentPersistedData* pItemAssignmentData = NULL;
	Entity *pOwnerEnt = gclInventoryGetOwner(pEnt);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwnerEnt);
	char *pchPrefixBuffer = NULL;
	char *pchContext = NULL;
	char *pchToken;
	S32 i, iBag, iCat, iAssign, iCount = 0;

	if (uOptions & (UIInvFilterFlag_NotOnItemAssignment|UIInvFilterFlag_OnItemAssignment)) 
		pItemAssignmentData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);

	eaClearFast(&s_eaInventoryBags);
	eaClearFast(&s_eaSlots);

	PERFINFO_AUTO_START("Parse Params", 1);
	{
		strdup_alloca(pchPrefixBuffer, pchPrefix);
		eaClearFast(&s_eaPrefixes);
		if (pchToken = strtok_r(pchPrefixBuffer, " \r\n\t,|%", &pchContext))
		{
			do
			{
				eaPush(&s_eaPrefixes, allocAddString(pchToken));
			} while (pchToken = strtok_r(NULL, " \r\n\t,|%", &pchContext));
		}

		strdup_alloca(pchPrefixBuffer, pchBagIds);
		eaClearFast(&s_eaInventoryBags);
		if (pchToken = strtok_r(pchPrefixBuffer, " \r\n\t,|%", &pchContext))
		{
			do
			{
				InvBagIDs BagID = StaticDefineIntGetInt(InvBagIDsEnum, pchToken);
				NOCONST(InventoryBag) *pEntBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);
				if (pEntBag)
					eaPush(&s_eaInventoryBags, CONTAINER_RECONST(InventoryBag, pEntBag));
			} while (pchToken = strtok_r(NULL, " \r\n\t,|%", &pchContext));
		}

		strdup_alloca(pchPrefixBuffer, pchIncludeCategories);
		eaiClearFast(&s_eaiIncludeCategories);
		if (pchToken = strtok_r(pchPrefixBuffer, " \r\n\t,|%", &pchContext))
		{
			do
			{
				ItemCategory eCategory = StaticDefineIntGetInt(ItemCategoryEnum, pchToken);
				if (eCategory != -1)
					eaiPush(&s_eaiIncludeCategories, eCategory);
			} while (pchToken = strtok_r(NULL, " \r\n\t,%", &pchContext));
		}

		strdup_alloca(pchPrefixBuffer, pchExcludeCategories);
		eaiClearFast(&s_eaiExcludeCategories);
		if (pchToken = strtok_r(pchPrefixBuffer, " \r\n\t,|%", &pchContext))
		{
			do
			{
				ItemCategory eCategory = StaticDefineIntGetInt(ItemCategoryEnum, pchToken);
				if (eCategory != -1)
					eaiPush(&s_eaiExcludeCategories, eCategory);
			} while (pchToken = strtok_r(NULL, " \r\n\t,|%", &pchContext));
		}
	}
	PERFINFO_AUTO_STOP();

	if (pEnt && pGen && eaSize(&s_eaInventoryBags))
	{
		for (iBag = eaSize(&s_eaInventoryBags) - 1; iBag >= 0; --iBag)
		{
			UICategorizedInventorySlot *pSlot = NULL;
			InventoryBag *pBag = s_eaInventoryBags[iBag];
			S32 iMaxSlots = invbag_maxslots(pEnt, pBag);
			const char *pchBagName = StaticDefineIntRevLookup(InvBagIDsEnum, pBag->BagID);

			// Do performance on a per-bag basis
			if (eaSize(&s_eaPerfStatic) <= pBag->BagID)
				eaSetSize(&s_eaPerfStatic, pBag->BagID + 1);
			PERFINFO_AUTO_START_STATIC(pchBagName, &s_eaPerfStatic[pBag->BagID], 1);

			for (i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); i++)
			{
				InventorySlot *pInvSlot = pBag->ppIndexedInventorySlots[i];
				Item *pItem = SAFE_MEMBER(pInvSlot, pItem);
				ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
				bool bInclude = false, bExclude = false;

				if (!pItemDef && !(uOptions & UIInvFilterFlag_EmptySlots))
					continue;

				if (pItemDef && eaiSize(&pItemDef->peCategories))
				{
					// if we require all the categories set, check to see if we have all the includeCategories
					if (uOptions & UIInvFilterFlag_RequireAllCategories)
					{
						bInclude = true;

						for (iCat = eaiSize(&s_eaiIncludeCategories)-1; iCat >= 0; --iCat)
						{
							if (eaiFind(&pItemDef->peCategories, s_eaiIncludeCategories[iCat]) == -1)
							{
								bInclude = false;
								break;
							}
						}
					}
					
					for (iCat = eaiSize(&pItemDef->peCategories) - 1; iCat >= 0 && !(bInclude && bExclude); --iCat)
					{
						if (!(uOptions & UIInvFilterFlag_RequireAllCategories))
						{
							if (!bInclude && eaiFind(&s_eaiIncludeCategories, pItemDef->peCategories[iCat]) >= 0)
								bInclude = true;
						}
												
						if (!bExclude && eaiFind(&s_eaiExcludeCategories, pItemDef->peCategories[iCat]) >= 0)
							bExclude = true;
					}
				}

				if (eaiSize(&s_eaiIncludeCategories) && !bInclude || eaiSize(&s_eaiExcludeCategories) && bExclude)
					continue;

				if (pItem && pItemAssignmentData)
				{
					bool bOnAssignment = false;
					PERFINFO_AUTO_START("Filter out assigned", 1);
					for (iAssign = eaSize(&pItemAssignmentData->eaActiveAssignments) - 1; iAssign >= 0; --iAssign)
					{
						ItemAssignment *pAssignment = pItemAssignmentData->eaActiveAssignments[iAssign];
						S32 iItem;
						for (iItem = eaSize(&pAssignment->eaSlottedItems) - 1; iItem >= 0; --iItem)
						{
							if (pAssignment->eaSlottedItems[iItem]->uItemID == pItem->id)
							{
								bOnAssignment = true;
								break;
							}
						}
						if (bOnAssignment)
							break;
					}
					PERFINFO_AUTO_STOP();
					if ((bOnAssignment && (uOptions & UIInvFilterFlag_NotOnItemAssignment)) ||
						(!bOnAssignment && (uOptions & UIInvFilterFlag_OnItemAssignment)))
						continue;
				}

				eaPush(&s_eaSlots, gclInventoryUpdateSlot(NULL, pBag, pInvSlot));
			}

			// Add empty slots up to max
			PERFINFO_AUTO_START("Add Empty", iMaxSlots - i);
			for (; (uOptions & UIInvFilterFlag_EmptySlots) && i < iMaxSlots; i++)
				eaPush(&s_eaSlots, gclInventoryUpdateNullSlot(NULL, pBag, i));
			PERFINFO_AUTO_STOP();

			PERFINFO_AUTO_STOP();
		}
	}

	return gclInvCreateCategorizedSlots(pGen, pEnt, NULL, s_eaSlots, pchPrefix, uOptions);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemDefNameHasCategory");
bool gclInvExprItemDefNameHasCategory(ACMD_EXPR_RES_DICT(ItemDef) const char *pchItemDef, S32 eCategory)
{
	ItemDef *pItemDef = item_DefFromName(pchItemDef);
	return pItemDef && eaiFind(&pItemDef->peCategories, eCategory) >= 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemDefHasCategory");
bool gclInvExprItemDefHasCategory(SA_PARAM_OP_VALID ItemDef *pItemDef, S32 eCategory)
{
	return pItemDef && eaiFind(&pItemDef->peCategories, eCategory) >= 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemHasCategory");
bool gclInvExprItemHasCategory(SA_PARAM_OP_VALID Item *pItem, S32 eCategory)
{
	return pItem && gclInvExprItemDefHasCategory(GET_REF(pItem->hItem), eCategory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyHasCategory");
bool gclInvExprInventoryKeyHasCategory(const char *pchInventoryKey, S32 eCategory)
{
	Item *pItem = gclInvExprInventoryKeyGetItem(pchInventoryKey);
	return pItem && gclInvExprItemDefHasCategory(GET_REF(pItem->hItem), eCategory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetDummy");
SA_RET_NN_VALID Item *gclInvExprItemGetDummy(const char *pchItemDef)
{
	static NOCONST(Item) s_aItemCache[16];
	static U32 s_auTimes[16];

	Entity *pEnt = entActivePlayerPtr();
	S32 iOldest = -1;
	U32 uOldestTime = 0;
	NOCONST(Item) *pItem;
	S32 i;

	pchItemDef = allocAddString(pchItemDef);

	for (i = 0; i < ARRAY_SIZE(s_aItemCache); i++)
	{
		if (REF_STRING_FROM_HANDLE(s_aItemCache[i].hItem) == pchItemDef)
		{
			if (s_aItemCache[i].count > 0)
			{
				return CONTAINER_RECONST(Item, &s_aItemCache[i]);
			}

			// Refresh the contents of the item pointer
			iOldest = i;
			break;
		}

		if (iOldest == -1 || gGCLState.totalElapsedTimeMs - s_auTimes[i] > gGCLState.totalElapsedTimeMs - uOldestTime)
		{
			iOldest = i;
			uOldestTime = s_auTimes[i];
		}
	}

	pItem = inv_ItemInstanceFromDefName(pchItemDef, entity_GetSavedExpLevel(pEnt), 0, NULL, NULL, NULL, false, NULL);
	if (pItem)
	{
		pItem->count = 1;//using this to denote that it's an actual item as opposed to a holder for a ref-to
		StructCopyAllNoConst(parse_Item, pItem, &s_aItemCache[iOldest]);
		StructDestroyNoConst(parse_Item, pItem);
	}
	else
	{
		StructResetNoConst(parse_Item, &s_aItemCache[iOldest]);
		s_aItemCache[iOldest].count = 0;
		SET_HANDLE_FROM_STRING(g_hItemDict, pchItemDef, s_aItemCache[iOldest].hItem);
	}

	s_auTimes[iOldest] = gGCLState.totalElapsedTimeMs;
	return CONTAINER_RECONST(Item, &s_aItemCache[iOldest]);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryGetInnatePowerAttribFromItemsInBag");
F32 gclInvExprInventoryGetInnatePowerAttribFromItemsInBag(SA_PARAM_NN_VALID Entity *pEnt, S32 eSrcBag, SA_PARAM_NN_VALID const char *pchAttribTypeName) 
{
	if (pEnt && pEnt->pChar && pchAttribTypeName)
	{
		S32 iPartitionIdx = entGetPartitionIdx(pEnt);
		static AutoDescInnateModDetails **s_eaInnateMods = NULL;
		static PowerDef **s_ppDefsInnate = NULL;
		static F32 *s_pfScales = NULL;
		GameAccountDataExtract *pExtract = NULL;
		BagIterator *pBagIt = NULL;
		AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttribTypeName);
		F32 fMagTotal = 0.f;
		S32 setupContext = true;

		if (eAttrib == -1)
			return 0.f;

		// it's in the bag
		pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		pBagIt = invbag_IteratorFromEnt(pEnt, eSrcBag, pExtract);
		if (!pBagIt)
			return 0.f;
			
		do {
			Item *pItem = (Item*)bagiterator_GetItem(pBagIt);
			S32 iNumPowers, i;
			if (!pItem)
				continue;

			eaClear(&s_ppDefsInnate);
			eafClear(&s_pfScales);

			iNumPowers = item_GetNumItemPowerDefs(pItem, true);
				
			for(i = 0; i < iNumPowers; ++i)
			{
				PowerDef *pPowerDef = item_GetPowerDef(pItem, i);
				if(pPowerDef && pPowerDef->eType == kPowerType_Innate)
				{
					// go through the power's attribMod and find the ones we care about
					FOR_EACH_IN_EARRAY(pPowerDef->ppOrderedMods, AttribModDef, pmoddef) 
					{
						if(!pmoddef->bDerivedInternally && pmoddef->offAttrib == eAttrib)
						{
							eaPush(&s_ppDefsInnate, pPowerDef);
							eafPush(&s_pfScales, item_GetItemPowerScale(pItem, i));
							break;
						}
					}
					FOR_EACH_END
				}
			}


			if (eaSize(&s_ppDefsInnate))
			{
				S32 iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : pEnt->pChar->iLevelCombat;

				powerdefs_GetAutoDescInnateMods(iPartitionIdx, 
												pItem,
												s_ppDefsInnate, 
												s_pfScales,
												NULL, 
												&s_eaInnateMods, 
												NULL, 
												pEnt->pChar, 
												NULL, 
												-1,
												iLevel, 
												true, 
												0,
												kAutoDescDetail_Normal);

				FOR_EACH_IN_EARRAY(s_eaInnateMods, AutoDescInnateModDetails, pDetails)
				{
					if (pDetails->offAttrib == eAttrib)
					{
						fMagTotal += pDetails->fMagnitude;
					}
				}
				FOR_EACH_END
			}

		} while (bagiterator_Next(pBagIt));

		bagiterator_Destroy(pBagIt);
			
		return fMagTotal;
	}

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemDefGetPowerDef");
const char *gclItemExprGetPowerDef(SA_PARAM_OP_VALID ItemDef *pItemDef, S32 iPower)
{
	ItemPowerDefRef *pItemPowerDefRef = pItemDef ? eaGet(&pItemDef->ppItemPowerDefRefs, iPower) : NULL;
	ItemPowerDef *pItemPowerDef = pItemPowerDefRef ? GET_REF(pItemPowerDefRef->hItemPowerDef) : NULL;
	PowerDef *pPowerDef = pItemPowerDef ? GET_REF(pItemPowerDef->hPower) : NULL;
	return pPowerDef ? pPowerDef->pchName : NULL;
}

static const char *gclInvBagFindItemKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, GameAccountDataExtract *pExtract, InvBagIDs eBagID, const char *pchName)
{
	BagIterator *pIter = inv_bag_trh_FindItemByDefName(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEntity), eBagID, pchName, NULL);
	if (pIter)
	{
		InventoryBag *pBag = CONTAINER_RECONST(InventoryBag, bagiterator_GetCurrentBag(pIter));
		InventorySlot *pSlot = bagiterator_GetSlot(pIter);
		UIInventoryKey Key = {0};
		const char *pchResult;
		gclInventoryMakeSlotKey(pEntity, pBag, pSlot, &Key);
		pchResult = gclInventoryMakeKeyString(pContext, &Key);
		bagiterator_Destroy(pIter);
		return pchResult;
	}
	return NULL;
}

// Returns the item key of the first item it finds of the particular itemdef
// NOTE: This function won't work once we have more than 32 bags, and uses hard-coded bag IDs without considering the data-extended ones.
// Consider using FindInventoryKeyByItemName() instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryFindItemKey");
const char *gclInventoryExprFindItemKey(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	if (pEntity && pEntity->pInventoryV2 && pItemDef)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		InvBagIDs eBagIDs[] = {
			InvBagIDs_Inventory, 
			InvBagIDs_PlayerBag1, InvBagIDs_PlayerBag2, InvBagIDs_PlayerBag3,
			InvBagIDs_PlayerBag4, InvBagIDs_PlayerBag5, InvBagIDs_PlayerBag6,
			InvBagIDs_PlayerBag7, InvBagIDs_PlayerBag8, InvBagIDs_PlayerBag9
		};
		U32 uTestedBags = 0;
		const char *pchResult;
		S32 i;

		// First check the "Inventory" bags
		for (i = 0; i < ARRAY_SIZE(eBagIDs); i++)
		{
			pchResult = gclInvBagFindItemKey(pContext, pEntity, pExtract, eBagIDs[i], pItemDef->pchName);
			if (pchResult)
				return pchResult;
			uTestedBags |= 1 << eBagIDs[i];
		}

		// Then just check all the bags
		for (i = 0; i < eaSize(&pEntity->pInventoryV2->ppInventoryBags); i++)
		{
			if (!(uTestedBags & (1 << pEntity->pInventoryV2->ppInventoryBags[i]->BagID)))
			{
				pchResult = gclInvBagFindItemKey(pContext, pEntity, pExtract, eBagIDs[i], pItemDef->pchName);
				if (pchResult)
					return pchResult;
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetSlot");
SA_RET_OP_VALID InventorySlot *gclInventoryExprGetSlot(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? pInvSlot->pSlot : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetSlotKey");
const char *gclInventoryExprGetSlotKey(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? pInvSlot->pchSlotKey: NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetSlotNumber");
S32 gclInventoryExprGetSlotNumber(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? gclInvExprInventoryKeyGetSlotNumber(pInvSlot->pchSlotKey): -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetSlotBag");
S32 gclInventoryExprGetSlotBag(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? gclInvExprInventoryKeyGetBagNumber(pInvSlot->pchSlotKey): -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetItem");
SA_RET_OP_VALID Item *gclInventoryExprGetItem(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot && pInvSlot->pSlot ? pInvSlot->pSlot->pItem : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetItemIcon");
const char *gclInventoryExprGetItemIcon(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot && pInvSlot->pSlot ? item_GetIconName(pInvSlot->pSlot->pItem, NULL) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetItemName");
const char *gclInventoryExprGetItemName(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot, SA_PARAM_OP_VALID Entity *pEnt)
{
	return pInvSlot && pInvSlot->pSlot ? item_GetName(pInvSlot->pSlot->pItem, pEnt) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetItemQuality");
S32 gclInventoryExprGetItemQuality(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot, SA_PARAM_OP_VALID Entity *pEnt)
{
	return pInvSlot && pInvSlot->pSlot ? gclItemExprGetQuality(pInvSlot->pSlot->pItem) : kItemQuality_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetItemQualityName");
const char *gclInventoryExprGetItemQualityName(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot, SA_PARAM_OP_VALID Entity *pEnt)
{
	return pInvSlot && pInvSlot->pSlot ? gclItemExprGetQualityName(pInvSlot->pSlot->pItem) : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCount");
S32 gclInventoryExprGetCount(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot && pInvSlot->pSlot && pInvSlot->pSlot->pItem ? pInvSlot->pSlot->pItem->count : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategory");
S32 gclInventoryExprGetCategory(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? pInvSlot->UICategory.eCategory : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryName");
const char *gclInventoryExprGetCategoryName(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? pInvSlot->UICategory.pchCategoryName : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryDisplayName");
const char *gclInventoryExprGetCategoryDisplayName(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? pInvSlot->UICategory.pchDisplayName : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryNameWithoutPrefix");
const char *gclInventoryExprGetCategoryNameWithoutPrefix(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? pInvSlot->UICategory.pchCategoryNameWithoutPrefix : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryNamePrefix");
const char *gclInventoryExprGetCategoryNamePrefix(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot ? pInvSlot->UICategory.pchCategoryNamePrefix : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryHint");
const char *gclInventoryExprGetCategoryHint(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot && pInvSlot->UICategory.pCategoryData ? pInvSlot->UICategory.pCategoryData->pchHint : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryHint2");
const char *gclInventoryExprGetCategoryHint2(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot && pInvSlot->UICategory.pCategoryData ? pInvSlot->UICategory.pCategoryData->pchHint2 : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryIcon");
const char *gclInventoryExprGetCategoryIcon(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot && pInvSlot->UICategory.pCategoryData ? pInvSlot->UICategory.pCategoryData->pchIcon : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CategorizedInventoryGetCategoryLargeIcon");
const char *gclInventoryExprGetCategoryLargeIcon(SA_PARAM_OP_VALID UICategorizedInventorySlot *pInvSlot)
{
	return pInvSlot && pInvSlot->UICategory.pCategoryData ? pInvSlot->UICategory.pCategoryData->pchLargeIcon : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetName");
const char *gclUIItemCategoryExprGetName(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory ? pCategory->pchCategoryName : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetDisplayName");
const char *gclUIItemCategoryExprGetDisplayName(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory && pCategory->pCategoryData ? pCategory->pchDisplayName : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetNameWithoutPrefix");
const char *gclUIItemCategoryExprGetNameWithoutPrefix(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory ? pCategory->pchCategoryNameWithoutPrefix : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetNamePrefix");
const char *gclUIItemCategoryExprGetNamePrefix(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory ? pCategory->pchCategoryNamePrefix : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetHint");
const char *gclUIItemCategoryExprGetHint(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory && pCategory->pCategoryData ? pCategory->pCategoryData->pchHint : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetHint2");
const char *gclUIItemCategoryExprGetHint2(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory && pCategory->pCategoryData ? pCategory->pCategoryData->pchHint2 : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetIcon");
const char *gclUIItemCategoryExprGetIcon(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory && pCategory->pCategoryData ? pCategory->pCategoryData->pchIcon : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("UIItemCategoryGetLargeIcon");
const char *gclUIItemCategoryExprGetLargeIcon(SA_PARAM_OP_VALID UIItemCategory *pCategory)
{
	return pCategory && pCategory->pCategoryData ? pCategory->pCategoryData->pchLargeIcon : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemCategoryGetHint");
const char *gclItemCategoryExprGetHint(S32 eCategory)
{
	ItemCategoryInfo *pInfo = eaGet(&g_ItemCategoryNames.ppInfo, eCategory - kItemCategory_FIRST_DATA_DEFINED);
	return pInfo ? pInfo->pchHint : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemCategoryGetHint2");
const char *gclItemCategoryExprGetHint2(S32 eCategory)
{
	ItemCategoryInfo *pInfo = eaGet(&g_ItemCategoryNames.ppInfo, eCategory - kItemCategory_FIRST_DATA_DEFINED);
	return pInfo ? pInfo->pchHint2 : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemCategoryGetIcon");
const char *gclItemCategoryExprGetIcon(S32 eCategory)
{
	ItemCategoryInfo *pInfo = eaGet(&g_ItemCategoryNames.ppInfo, eCategory - kItemCategory_FIRST_DATA_DEFINED);
	return pInfo ? pInfo->pchIcon : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemCategoryGetLargeIcon");
const char *gclItemCategoryExprGetLargeIcon(S32 eCategory)
{
	ItemCategoryInfo *pInfo = eaGet(&g_ItemCategoryNames.ppInfo, eCategory - kItemCategory_FIRST_DATA_DEFINED);
	return pInfo ? pInfo->pchLargeIcon : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemCanOpenRewardPack");
bool gclItemCanOpenRewardPack(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID ItemDef* pItemDef, const char* pchRequiredProductVar)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemDef* pRequiredItemDef = NULL;
	if (!item_CanOpenRewardPack(pEnt, pItemDef, &pRequiredItemDef, NULL))
	{
		if (pRequiredItemDef && pchRequiredProductVar && pchRequiredProductVar[0])
		{
			if (pItemDef->pRewardPackInfo && EMPTY_TO_NULL(pItemDef->pRewardPackInfo->pchRequiredItemProduct))
			{
				UIGenVarTypeGlob *pGlob = eaIndexedGetUsingString(&pGen->eaVars, pchRequiredProductVar);
				if (pGlob)
				{
					estrCopy2(&pGlob->pchString, pItemDef->pRewardPackInfo->pchRequiredItemProduct);
				}
			}
		}
		return false;
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetPowerTimestamp");
U32 gclItemExprGetPowerTimestamp(SA_PARAM_OP_VALID Item *pItem)
{
	if (pItem && eaSize(&pItem->ppPowers))
	{
		// Get the oldest timestamp
		U32 uOldest = 0xFFFFFFFF;
		S32 i;
		for (i = eaSize(&pItem->ppPowers) - 1; i >= 0; i--)
		{
			if (pItem->ppPowers[i]->uiTimeCreated < uOldest)
				uOldest = pItem->ppPowers[i]->uiTimeCreated;
		}
		return uOldest;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyGetPowerTimestamp");
U32 gclInventoryKeyExprGetPowerTimestamp(const char *pchKey)
{
	InventorySlot *pSlot = gclInvExprInventoryKeyGetSlot(pchKey);
	if (pSlot && pSlot->pItem)
		return gclItemExprGetPowerTimestamp(pSlot->pItem);
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyIsTraded");
bool gclInventoryKeyExprIsTraded(const char *pchKey)
{
	UIInventoryKey Key = {0};
	S32 i;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem || !Key.pOwner || !Key.pOwner->pPlayer || !Key.pOwner->pPlayer->pTradeBag)
		return false;

	for (i = eaSize(&Key.pOwner->pPlayer->pTradeBag->ppTradeSlots) - 1; i >= 0; i--)
	{
		Item *pItem = Key.pOwner->pPlayer->pTradeBag->ppTradeSlots[i]->pItem;
		if (pItem && pItem->id == Key.pSlot->pItem->id)
			return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyGetBagFlags");
S32 gclInventoryKeyExprGetBagFlags(const char *pchKey)
{
	UIInventoryKey Key = {0};
	const InvBagDef* pDef;
	if (!gclInventoryParseKey(pchKey, &Key))
		return 0;
	if (Key.pBag && (pDef = invbag_def(Key.pBag)))
		return pDef->flags;
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyIsBindOnEquip");
bool gclInventoryKeyExprIsBindOnEquip(const char *pchKey)
{
	UIInventoryKey Key = {0};
	Item *pItem;
	ItemDef *pItemDef;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem)
		return false;

	pItem = Key.pSlot->pItem;
	pItemDef = GET_REF(pItem->hItem);
	if (!pItemDef)
		return false;

	if ((pItemDef->flags & kItemDefFlag_BindOnEquip) && !(pItem->flags & kItemFlag_Bound))
		return true;
	if ((pItemDef->flags & kItemDefFlag_BindToAccountOnEquip) && !(pItem->flags & (kItemFlag_BoundToAccount | kItemFlag_Bound)))
		return true;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyIsAccountBindOnEquip");
bool gclInventoryKeyExprIsAccountBindOnEquip(const char *pchKey)
{
	UIInventoryKey Key = {0};
	Item *pItem;
	ItemDef *pItemDef;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem)
		return false;

	pItem = Key.pSlot->pItem;
	pItemDef = GET_REF(pItem->hItem);
	if (!pItemDef)
		return false;

	if ((pItemDef->flags & kItemDefFlag_BindToAccountOnEquip) && !(pItem->flags & (kItemFlag_BoundToAccount | kItemFlag_Bound)))
		return true;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyGetBindOnEquip");
const char *gclInventoryKeyExprGetBindOnEquip(const char *pchKeyA, const char *pchKeyB)
{
	UIInventoryKey KeyA = {0};
	UIInventoryKey KeyB = {0};
	bool bEquipBagA = false, bEquipBagB = false;
	Item *pItemA = NULL, *pItemB = NULL;
	ItemDef *pItemDefA = NULL, *pItemDefB = NULL;

	if (!gclInventoryParseKey(pchKeyA, &KeyA) || !gclInventoryParseKey(pchKeyB, &KeyB))
		return "";

	bEquipBagA = (invbag_flags(KeyA.pBag) & InvBagFlag_BindBag) != 0;
	bEquipBagB = (invbag_flags(KeyB.pBag) & InvBagFlag_BindBag) != 0;

	if (!bEquipBagA && !bEquipBagB)
		return "";

	if (KeyA.pSlot && KeyA.pSlot->pItem)
	{
		pItemA = KeyA.pSlot->pItem;
		pItemDefA = GET_REF(pItemA->hItem);
	}

	if (KeyB.pSlot && KeyB.pSlot->pItem)
	{
		pItemB = KeyB.pSlot->pItem;
		pItemDefB = GET_REF(pItemB->hItem);
	}

	if (pItemDefA && bEquipBagB)
	{
		if ((pItemDefA->flags & kItemDefFlag_BindOnEquip) && !(pItemA->flags & kItemFlag_Bound))
			return pchKeyA;
		if ((pItemDefA->flags & kItemDefFlag_BindToAccountOnEquip) && !(pItemA->flags & (kItemFlag_Bound | kItemFlag_BoundToAccount)))
			return pchKeyA;
	}

	if (pItemDefB && bEquipBagA)
	{
		if ((pItemDefB->flags & kItemDefFlag_BindOnEquip) && !(pItemB->flags & kItemFlag_Bound))
			return pchKeyB;
		if ((pItemDefB->flags & kItemDefFlag_BindToAccountOnEquip) && !(pItemB->flags & (kItemFlag_Bound | kItemFlag_BoundToAccount)))
			return pchKeyB;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyHasCostumeOverlay");
bool gclInventoryKeyExprHasCostumeOverlay(const char *pchKey)
{
	UIInventoryKey Key = {0};
	Item *pItem = NULL;
	ItemDef *pItemDef = NULL;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;

	if (!Key.pSlot || !Key.pSlot->pItem)
		return false;

	pItem = Key.pSlot->pItem;
	pItemDef = GET_REF(pItem->hItem);

	return pItemDef && eaSize(&pItemDef->ppCostumes) && (pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay || pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay_Always);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyAppliesCostumeOverlay");
bool gclInventoryKeyExprAppliesCostumeOverlay(const char *pchKey)
{
	UIInventoryKey Key = {0};
	Item *pItem = NULL;
	ItemDef *pItemDef = NULL;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;

	if (!Key.pSlot || !Key.pSlot->pItem)
		return false;

	pItem = Key.pSlot->pItem;
	pItemDef = GET_REF(pItem->hItem);

	if (pItemDef && eaSize(&pItemDef->ppCostumes) && (pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay || pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay_Always))
	{
		return (invbag_flags(Key.pBag) & (InvBagFlag_CostumeHideable | InvBagFlag_CostumeHideablePerSlot)) != 0;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyApplyCostumeOverlay");
bool gclInventoryKeyExprApplyCostumeOverlay(const char *pchKey, bool bEnable)
{
	UIInventoryKey Key = {0};
	Item *pItem = NULL;
	ItemDef *pItemDef = NULL;
	int iChange = 0;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;

	if (!Key.pSlot || !Key.pSlot->pItem || !Key.pBag)
		return false;

	pItem = Key.pSlot->pItem;
	pItemDef = GET_REF(pItem->hItem);

	if (pItemDef && eaSize(&pItemDef->ppCostumes) && (pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay || pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay_Always))
	{
		if (invbag_flags(Key.pBag) & InvBagFlag_CostumeHideablePerSlot)
		{
			if (Key.pSlot->bHideCostumes && bEnable || !Key.pSlot->bHideCostumes && !bEnable)
				iChange = 2;
		}
		else if (invbag_flags(Key.pBag) & InvBagFlag_CostumeHideable)
		{
			if (Key.pBag->bHideCostumes && bEnable || !Key.pBag->bHideCostumes && !bEnable)
				iChange = 1;
		}
	}

	switch (iChange)
	{
	case 1:
		if(Key.pEntity == Key.pOwner)
		{
			ServerCmd_SetInvBagHideMode(Key.eBag, !bEnable);
		}
		else
		{
			ServerCmd_SetPetInvBagHideMode(entGetType(Key.pEntity), entGetContainerID(Key.pEntity), Key.eBag, !bEnable);
		}
		break;
	case 2:
		if (Key.pEntity == Key.pOwner)
		{
			ServerCmd_SetInvSlotHideMode(Key.eBag, Key.iSlot, !bEnable);
		}
		else
		{
			ServerCmd_SetInvSlotHideModeForEnt(entGetType(Key.pEntity), entGetContainerID(Key.pEntity), Key.eBag, Key.iSlot, !bEnable);
		}
		break;
	}

	return !!iChange;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyApplyingCostumeOverlay");
bool gclInventoryKeyExprApplyingCostumeOverlay(const char *pchKey)
{
	UIInventoryKey Key = {0};
	Item *pItem = NULL;
	ItemDef *pItemDef = NULL;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;

	if (!Key.pSlot || !Key.pSlot->pItem || !Key.pBag)
		return false;

	pItem = Key.pSlot->pItem;
	pItemDef = GET_REF(pItem->hItem);

	if (pItemDef && eaSize(&pItemDef->ppCostumes) && (pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay || pItemDef->eCostumeMode == kCostumeDisplayMode_Overlay_Always))
	{
		if (invbag_flags(Key.pBag) & InvBagFlag_CostumeHideablePerSlot)
			return !Key.pSlot->bHideCostumes;
		else if (invbag_flags(Key.pBag) & InvBagFlag_CostumeHideable)
			return !Key.pBag->bHideCostumes;
		return true;
	}

	return false;
}

// Check to see if the item when equipped goes into a special bag (weapon, device, equip, recipe, etc).
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("InventoryKeyIsEquippable");
bool gclInventoryKeyExprIsEquippable(const char *pchKey, SA_PARAM_OP_VALID Entity *pEnt)
{
	UIInventoryKey Key = {0};
	Item *pItem = NULL;
	ItemDef *pItemDef = NULL;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem)
		return false;
	if (gclInventoryGetOwner(pEnt) != Key.pOwner)
		return false;
	if (Key.pEntity->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK || Key.pEntity->myEntityType == GLOBALTYPE_ENTITYGUILDBANK)
		return false;

	pItem = Key.pSlot->pItem;
	pItemDef = GET_REF(pItem->hItem);

	if (item_IsMissionGrant(pItemDef) ||
		item_IsSavedPet(pItemDef) ||
		pItemDef && pItemDef->eType == kItemType_STOBridgeOfficer ||
		item_isAlgoPet(pItemDef) ||
		item_IsVanityPet(pItemDef) ||
		item_IsAttributeModify(pItemDef) ||
		item_IsInjuryCureGround(pItemDef) ||
		item_IsInjuryCureSpace(pItemDef) ||
		item_IsRewardPack(pItemDef) ||
		item_IsMicroSpecial(pItemDef) ||
		item_IsExperienceGift(pItemDef))
	{
		return true;
	}

	if (pEnt && pItem)
	{
		InvBagIDs iDestBag = GetBestBagForItemDef(pEnt, pItemDef, -1, false, Key.pExtract);
		if ((invbag_trh_flags(inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDestBag, Key.pExtract)) & InvBagFlag_SpecialBag) != 0)
		{
			// Equipped is being able to move the item into a special bag.
			return item_ItemMoveDestValid(pEnt, pItemDef, pItem, inv_IsGuildBag(iDestBag), iDestBag, -1, true, Key.pExtract);
		}
	}

	return false;
}

// This will return true if the item can be used while unequipped
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyIsDeviceUsable");
bool gclInventoryKeyExprIsDeviceUsable(const char *pchKey)
{
	UIInventoryKey Key = {0};

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem)
		return false;
	if (Key.pEntity->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK || Key.pEntity->myEntityType == GLOBALTYPE_ENTITYGUILDBANK)
		return false;

	return item_IsDeviceUsableByPlayer(Key.pEntity, Key.pSlot->pItem, Key.pBag);
}

// This uses the specified device if allowed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyUseDevice");
bool gclInventoryKeyExprUseDevice(const char *pchKey)
{
	UIInventoryKey Key = {0};
	S32 i, iPowers, iUsed = 0;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem)
		return false;
	if (!item_IsDeviceUsableByPlayer(Key.pEntity, Key.pSlot->pItem, Key.pBag))
		return false;

	//Execute all useable powers
	iPowers = item_GetNumItemPowerDefs(Key.pSlot->pItem, true);
	if(iPowers < 1)
		return false;

	for(i = iPowers - 1; i>=0; i--)
	{
		if(item_isPowerUsableByPlayer(Key.pEntity, Key.pSlot->pItem, Key.pBag, i))
		{
			Power *pPower = item_GetPower(Key.pSlot->pItem, i);
			U32 uPowerID = SAFE_MEMBER2(pPower, pParentPower, uiID);
			if (!uPowerID)
				uPowerID = SAFE_MEMBER(pPower, uiID);
			entUsePowerID(true, uPowerID);
			iUsed++;
		}
	}

	return iUsed > 0;
}

static InvBagIDs gclInventoryGetUnequipBag(Entity *pOwner, Entity *pEntity, ItemDef *pDef)
{
	InvBagIDs eBestBag = InvBagIDs_Inventory;

	if (pOwner && pEntity && pDef && (pDef->flags & kItemDefFlag_LockToRestrictBags) != 0 && eaiSize(&pDef->peRestrictBagIDs) > 0 && eaiFind(&pDef->peRestrictBagIDs, eBestBag) <= 0)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
		bool bCheckOwner = true;
		S32 i;

		for (i = eaiSize(&pDef->peRestrictBagIDs) - 1; i >= 0; i--)
		{
			InventoryBag *pEntBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), pDef->peRestrictBagIDs[i], pExtract));
			if (pEntBag && (invbag_flags(pEntBag) & InvBagFlag_SpecialBag) == 0)
			{
				eBestBag = pEntBag->BagID;
				bCheckOwner = false;
				break;
			}
		}

		// If the owner and entity are different, and an appropriate bag
		// was not found on the entity. Check the owner for a bag.
		if (bCheckOwner && pEntity != pOwner)
		{
			for (i = eaiSize(&pDef->peRestrictBagIDs) - 1; i >= 0; i--)
			{
				InventoryBag *pEntBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pOwner), pDef->peRestrictBagIDs[i], pExtract));
				if (pEntBag && (invbag_flags(pEntBag) & InvBagFlag_SpecialBag) == 0)
				{
					eBestBag = pEntBag->BagID;
					break;
				}
			}
		}
	}

	return eBestBag;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyCanUnequip");
bool gclInventoryKeyExprCanUnequip(const char *pchKeyA)
{
	ItemDef *pDef;
	UIInventoryKey KeyA = {0};
	InvBagIDs eBestBag = InvBagIDs_Inventory;

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if ((invbag_flags(KeyA.pBag) & InvBagFlag_SpecialBag) == 0)
		return false;

	pDef = KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : KeyA.pSlot && KeyA.pSlot->pItem ? GET_REF(KeyA.pSlot->pItem->hItem) : NULL;
	if (!pDef)
		return false;
	if (KeyA.pOwner != entActivePlayerPtr())
		return false;

	eBestBag = gclInventoryGetUnequipBag(KeyA.pOwner, KeyA.pEntity, pDef);
	if (KeyA.eBag == eBestBag)
		return false;

	if (gclInvExprInventoryKeyMoveValidEnt(pchKeyA, KeyA.pEntity, eBestBag, -1))
		return true;
	return gclInvExprInventoryKeyMoveValidEnt(pchKeyA, KeyA.pOwner, eBestBag, -1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyUnequip");
bool gclInventoryKeyExprUnequip(const char *pchKeyA)
{
	ItemDef *pDef;
	UIInventoryKey KeyA = {0};
	UIInventoryKey KeyB;
	InvBagIDs eBestBag = InvBagIDs_Inventory;

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if ((invbag_flags(KeyA.pBag) & InvBagFlag_SpecialBag) == 0)
		return false;

	pDef = KeyA.pSlotLite ? GET_REF(KeyA.pSlotLite->hItemDef) : KeyA.pSlot && KeyA.pSlot->pItem ? GET_REF(KeyA.pSlot->pItem->hItem) : NULL;
	if (!pDef)
		return false;
	if (KeyA.pOwner != entActivePlayerPtr())
		return false;

	eBestBag = gclInventoryGetUnequipBag(KeyA.pOwner, KeyA.pEntity, pDef);
	if (KeyA.eBag == eBestBag)
		return false;

	KeyB = KeyA;
	KeyB.eBag = eBestBag;
	KeyB.iSlot = -1;
	KeyB.pchName = NULL;

	if (gclInvExprInventoryKeyMoveValidEnt(pchKeyA, KeyA.pEntity, eBestBag, -1))
	{
		gclInvExprInventoryKeyMoveKey(pchKeyA, gclInventoryMakeKeyString(NULL, &KeyB), -1);
		return true;
	}

	if (gclInvExprInventoryKeyMoveValidEnt(pchKeyA, KeyA.pOwner, eBestBag, -1))
	{
		// Remove sub entity
		KeyB.pEntity = KeyB.pOwner;
		KeyB.eType = GLOBALTYPE_NONE;
		KeyB.iContainerID = 0;
		gclInvExprInventoryKeyMoveKey(pchKeyA, gclInventoryMakeKeyString(NULL, &KeyB), -1);
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyIsSellable");
bool gclInventoryKeyExprIsSellable(const char *pchKey)
{
	ItemDef *pItemDef;
	UIInventoryKey Key = {0};

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (Key.pOwner != entActivePlayerPtr())
		return false;

	pItemDef = Key.pSlotLite ? GET_REF(Key.pSlotLite->hItemDef) : Key.pSlot && Key.pSlot->pItem ? GET_REF(Key.pSlot->pItem->hItem) : NULL;
	if (!pItemDef)
		return false;

	if (pItemDef->flags & kItemDefFlag_CantSell)
		return false;

	switch (pItemDef->eType)
	{
	case kItemType_Mission:
	case kItemType_MissionGrant:
		return false;
	}

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyGetItemValue");
int gclInventoryKeyExprGetItemValue(ExprContext* pContext, const char *pchKey, const char *pchResources)
{
	UIInventoryKey Key = {0};

	if (!gclInventoryParseKey(pchKey, &Key))
		return 0;
	if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
		return 0;

	return item_GetResourceValue(PARTITION_CLIENT, Key.pOwner, Key.pSlot->pItem, pchResources);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeySellToContact");
bool gclInventoryKeyExprSellToContact(const char *pchKey, S32 iCount, const char *pchContactDef)
{
	UIInventoryKey Key = {0};

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
		return false;

	if (iCount < 0)
		iCount = Key.pSlot->pItem->count;

	ServerCmd_store_SellItemNoDialog(Key.eBag, Key.iSlot, iCount, entGetType(Key.pEntity), entGetContainerID(Key.pEntity), pchContactDef);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyGetItemTypeName");
const char *gclInventoryKeyExprGetItemTypeName(const char *pchKey)
{
	UIInventoryKey Key = {0};
	ItemDef *pItemDef;

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;
	if (!Key.pSlot || !Key.pSlot->pItem || Key.pOwner != entActivePlayerPtr())
		return false;
	pItemDef = Key.pSlotLite ? GET_REF(Key.pSlotLite->hItemDef) : Key.pSlot && Key.pSlot->pItem ? GET_REF(Key.pSlot->pItem->hItem) : NULL;
	if (!pItemDef)
		return false;

	return StaticDefineIntRevLookup(ItemTypeEnum, pItemDef->eType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetDyeColor");
U32 gclExprItemGetDyeColor(SA_PARAM_NN_VALID Item* pItem, int channel)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem, hItem);
	if (pItemDef->eType == kItemType_DyeBottle && channel == 0)
	{
		return Vec3ToRGBA(pItemDef->vDyeColor0);
	}
	else if (pItemDef->eType == kItemType_DyePack)
	{
		switch (channel)
		{
		case 0:
			{
				return Vec3ToRGBA(pItemDef->vDyeColor0);
			}break;
		case 1:
			{
				return Vec3ToRGBA(pItemDef->vDyeColor1);
			}break;
		case 2:
			{
				return Vec3ToRGBA(pItemDef->vDyeColor2);
			}break;
		case 3:
			{
				return Vec3ToRGBA(pItemDef->vDyeColor3);
			}break;
		}
	}
	return 0;
}

// Get the list of InventorySlots in the given bag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetDyeableEquippedItems);
bool gclInvExprGenGetDyeableEquippedItems(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity* pEnt = entActivePlayerPtr();
	int i, j;
	TradeSlotLite ***peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlotLite);
	eaClearStruct(peaSlots, parse_TradeSlotLite);
	if (pGen && pEnt && pEnt->pInventoryV2)
	{
		for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
		{
			InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];
			
			if (invbag_flags(pBag) & InvBagFlag_EquipBag)
			{
				for (j = 0; j < eaSize(&pBag->ppIndexedInventorySlots); j++)
				{
					if (pBag->ppIndexedInventorySlots[j])
					{
						ItemDef *pDef = pBag->ppIndexedInventorySlots[j]->pItem ? GET_REF(pBag->ppIndexedInventorySlots[j]->pItem->hItem) : NULL;
						if (pDef && eaSize(&pDef->ppCostumes) > 0)
						{
							TradeSlotLite* pSlot = StructCreate(parse_TradeSlotLite);
							pSlot->count = 1;
							pSlot->pItem = pBag->ppIndexedInventorySlots[j]->pItem;
							pSlot->SrcBagId = pBag->BagID;
							pSlot->SrcItemId = pBag->ppIndexedInventorySlots[j]->pItem->id;
							pSlot->SrcSlot = j;
							eaPush(peaSlots, pSlot);
						}
					}
				}
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, peaSlots, TradeSlotLite, false);
	return true;
}

// Get the list of InventorySlots in the given bag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetTransmutableItems);
bool gclInvExprGenGetTransmutableItems(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity* pEnt = entActivePlayerPtr();
	int i, j;
	TradeSlotLite ***peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlotLite);
	eaClearStruct(peaSlots, parse_TradeSlotLite);
	if (pGen && pEnt && pEnt->pInventoryV2)
	{
		for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
		{
			InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];

			for (j = 0; j < eaSize(&pBag->ppIndexedInventorySlots); j++)
			{
				if (pBag->ppIndexedInventorySlots[j])
				{
					ItemDef *pDef = pBag->ppIndexedInventorySlots[j]->pItem ? GET_REF(pBag->ppIndexedInventorySlots[j]->pItem->hItem) : NULL;
					if (item_CanTransMutate(pDef))
					{
						TradeSlotLite* pSlot = StructCreate(parse_TradeSlotLite);
						pSlot->count = 1;
						pSlot->pItem = pBag->ppIndexedInventorySlots[j]->pItem;
						pSlot->SrcBagId = pBag->BagID;
						pSlot->SrcItemId = pBag->ppIndexedInventorySlots[j]->pItem->id;
						pSlot->SrcSlot = j;
						eaPush(peaSlots, pSlot);
					}
				}
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, peaSlots, TradeSlotLite, false);
	return true;
}

// Get the list of InventorySlots in the given bag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGemmableItems);
bool gclInvExprGenGetGemmableItems(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity* pEnt = entActivePlayerPtr();
	int i, j;
	TradeSlotLite ***peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlotLite);
	eaClearStruct(peaSlots, parse_TradeSlotLite);
	if (pGen && pEnt && pEnt->pInventoryV2)
	{
		for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
		{
			InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];

			for (j = 0; j < eaSize(&pBag->ppIndexedInventorySlots); j++)
			{
				if (pBag->ppIndexedInventorySlots[j])
				{
					ItemDef *pDef = pBag->ppIndexedInventorySlots[j]->pItem ? GET_REF(pBag->ppIndexedInventorySlots[j]->pItem->hItem) : NULL;
					if (eaSize(&pDef->ppItemGemSlots) > 0)
					{
						TradeSlotLite* pSlot = StructCreate(parse_TradeSlotLite);
						pSlot->count = 1;
						pSlot->pItem = pBag->ppIndexedInventorySlots[j]->pItem;
						pSlot->SrcBagId = pBag->BagID;
						pSlot->SrcItemId = pBag->ppIndexedInventorySlots[j]->pItem->id;
						pSlot->SrcSlot = j;
						eaPush(peaSlots, pSlot);
					}
				}
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, peaSlots, TradeSlotLite, false);
	return true;
}

// Get the list of InventorySlots in the given bag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGemmableItemsWithSlotType);
bool gclInvExprGenGetGemmableItemsWithSlotType(SA_PARAM_NN_VALID UIGen *pGen, S32 eRequiredTypes)
{
	Entity* pEnt = entActivePlayerPtr();
	int i, j;
	TradeSlotLite ***peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlotLite);
	eaClearStruct(peaSlots, parse_TradeSlotLite);
	if (pGen && pEnt && pEnt->pInventoryV2)
	{
		for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
		{
			InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];

			for (j = 0; j < eaSize(&pBag->ppIndexedInventorySlots); j++)
			{
				if (pBag->ppIndexedInventorySlots[j])
				{
					ItemDef *pDef = pBag->ppIndexedInventorySlots[j]->pItem ? GET_REF(pBag->ppIndexedInventorySlots[j]->pItem->hItem) : NULL;
					int k;
					if (pDef && !item_IsUnidentified(pBag->ppIndexedInventorySlots[j]->pItem))
					{
						for (k = 0; k < eaSize(&pDef->ppItemGemSlots); k++)
						{
							if (!eRequiredTypes || (pDef->ppItemGemSlots[k]->eType & eRequiredTypes))
							{
								TradeSlotLite* pSlot = StructCreate(parse_TradeSlotLite);
								pSlot->count = 1;
								pSlot->pItem = pBag->ppIndexedInventorySlots[j]->pItem;
								pSlot->SrcBagId = pBag->BagID;
								pSlot->SrcItemId = pBag->ppIndexedInventorySlots[j]->pItem->id;
								pSlot->SrcSlot = j;
								eaPush(peaSlots, pSlot);
								break;
							}
						}
					}
				}
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, peaSlots, TradeSlotLite, false);
	return true;
}

// Get the list of InventorySlots in the given bag.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetGemsOfType);
bool gclInvExprGenGetGemsWithGemType(SA_PARAM_NN_VALID UIGen *pGen, S32 eRequiredTypes)
{
	Entity* pEnt = entActivePlayerPtr();
	int i, j;
	TradeSlotLite ***peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlotLite);
	eaClearStruct(peaSlots, parse_TradeSlotLite);
	if (pGen && pEnt && pEnt->pInventoryV2)
	{
		for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
		{
			InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];

			for (j = 0; j < eaSize(&pBag->ppIndexedInventorySlots); j++)
			{
				if (pBag->ppIndexedInventorySlots[j])
				{
					ItemDef *pDef = pBag->ppIndexedInventorySlots[j]->pItem ? GET_REF(pBag->ppIndexedInventorySlots[j]->pItem->hItem) : NULL;
					if (pDef && pDef->eType == kItemType_Gem && (!eRequiredTypes || (pDef->eGemType & eRequiredTypes)))
					{
						TradeSlotLite* pSlot = StructCreate(parse_TradeSlotLite);
						pSlot->count = 1;
						pSlot->pItem = pBag->ppIndexedInventorySlots[j]->pItem;
						pSlot->SrcBagId = pBag->BagID;
						pSlot->SrcItemId = pBag->ppIndexedInventorySlots[j]->pItem->id;
						pSlot->SrcSlot = j;
						eaPush(peaSlots, pSlot);
					}
				}
			}
		}
	}
	ui_GenSetManagedListSafe(pGen, peaSlots, TradeSlotLite, false);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyCanChangeActiveSlot");
bool gclInventoryKeyExprCanChangeActiveSlot(const char *pchKey, S32 iNewActiveSlot)
{
	UIInventoryKey Key = {0};

	if (!gclInventoryParseKey(pchKey, &Key))
		return false;

	if (iNewActiveSlot < 0)
	{
		const InvBagDef* pBagDef = invbag_def(Key.pBag);

		if (!pBagDef || !(pBagDef->flags & InvBagFlag_ActiveWeaponBag))
			return false;

		if (!eaiSize(&Key.pBag->eaiActiveSlots) && !eaiSize(&Key.pBag->eaiPredictedActiveSlots))
		{
			if (pBagDef->maxActiveSlots==1)
			{
				iNewActiveSlot = 1;
			}
		}
		else
		{
			S32 i, j;
			for (i = 0; i < pBagDef->MaxSlots; ++i)
			{
				for (j = eaiSize(&Key.pBag->eaiActiveSlots)-1; j >= 0; j--)
				{
					if (invbag_GetActiveSlot(Key.pBag, j) == i)
						break;
				}

				if (j < 0)
					iNewActiveSlot = i;
			}
		}
	}

	return invbag_CanChangeActiveSlot(Key.pEntity, Key.pBag, 0, iNewActiveSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetDataItem);
bool gclInvExprGenSetDataItem(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Item *pItem)
{
	ui_GenSetPointer(pGen, pItem, parse_Item);
	return !!pItem;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemGetSpecialCount);
int gclGenItemGetSpecialCount(SA_PARAM_OP_VALID Item *pItem)
{
	int iNumericValue = 0;
	if (pItem)
	{
		ItemDef* pItemDef = GET_REF(pItem->hItem);
		iNumericValue = pItem->count;

		if (pItemDef)
		{
			iNumericValue *= pItemDef->fScaleUI;
		}
	}
	return iNumericValue;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemRequiresSpecialCount);
bool gclInvExprGenItemRequiresSpecialCount(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

	if(pItemDef && pItemDef->eType == kItemType_Numeric
		&& !nearSameF32(pItemDef->fScaleUI, 1.0f))
	{
		return true;
	}

	return false;
}


typedef Item* (*ui_GenGetItemFromStructCB)(void* pvUserdata);
#define UI_GEN_GET_ITEM_CALLBACK(type, retval) \
	static Item* ui_GenGetItemFrom##type(void* pvUserdata) \
	{ \
		if (pvUserdata) \
		{ \
			type *pStruct = (type*)pvUserdata; \
			retval \
		} \
		return NULL; \
	}

extern ParseTable parse_InventorySlot[];
extern ParseTable parse_TradeSlotLite[];
extern ParseTable parse_TradeSlot[];
extern ParseTable parse_AuctionLot[];
extern ParseTable parse_StoreItemInfo[];
extern ParseTable parse_TeamLootItemSlot[];

UI_GEN_GET_ITEM_CALLBACK(Item,				return pStruct;)
UI_GEN_GET_ITEM_CALLBACK(InventorySlot,		return pStruct->pItem;)
UI_GEN_GET_ITEM_CALLBACK(TradeSlotLite,		return pStruct->pItem;)
UI_GEN_GET_ITEM_CALLBACK(TradeSlot,			return pStruct->pItem;)
UI_GEN_GET_ITEM_CALLBACK(AuctionLot,		return pStruct->ppItemsV2[0]->slot.pItem;)
UI_GEN_GET_ITEM_CALLBACK(StoreItemInfo,		return pStruct->pItem;)
UI_GEN_GET_ITEM_CALLBACK(TeamLootItemSlot,	return pStruct->pItem;)

static ui_GenGetItemFromStructCB GetItemStructCallback(ParseTable *pTable)
{
	     if (pTable == parse_Item)				return ui_GenGetItemFromItem;
	else if (pTable == parse_InventorySlot)		return ui_GenGetItemFromInventorySlot;
	else if (pTable == parse_TradeSlotLite)		return ui_GenGetItemFromTradeSlotLite;
	else if (pTable == parse_TradeSlot)			return ui_GenGetItemFromTradeSlot;
	else if (pTable == parse_AuctionLot)		return ui_GenGetItemFromAuctionLot;
	else if (pTable == parse_StoreItemInfo)		return ui_GenGetItemFromStoreItemInfo;
	else if (pTable == parse_TeamLootItemSlot)	return ui_GenGetItemFromTeamLootItemSlot;
	else										return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenListSetDataItemFromAnything);
void ui_GenExprLayoutBoxSetDataItemFromAnything(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxSetDataItemFromAnything);
void ui_GenExprLayoutBoxSetDataItemFromAnything(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pTable;
	UIGen ***peaInstances = ui_GenGetInstances(pGen);
	void ***peaData = ui_GenGetList(pGen, NULL, &pTable);
	int i;
	ui_GenGetItemFromStructCB cbGetItemFromStruct = GetItemStructCallback(pTable);
	if (cbGetItemFromStruct)
	{
		for (i = 0; i < eaSize(peaInstances); i++)
		{
			ui_GenSetPointer((*peaInstances)[i], cbGetItemFromStruct((*peaData)[i]), parse_Item);
		}
	}
	else
	{
		for (i = 0; i < eaSize(peaInstances); i++)
		{
			ui_GenSetPointer((*peaInstances)[i], NULL, parse_Item);
		}
	}

}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetDataItemFromAnything);
void ui_GenExprSetDataItemFromAnything(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, int iIndex)
{
	ParseTable *pTable;
	UIGen *pListGen = pGen->pParent;
	void ***peaData = ui_GenGetList(pListGen, NULL, &pTable);
	ui_GenGetItemFromStructCB cbGetItemFromStruct = GetItemStructCallback(pTable);
	if (cbGetItemFromStruct)
		ui_GenSetPointer(pGen, cbGetItemFromStruct(eaGet(peaData, iIndex)), parse_Item);
	else
	{
		ui_GenSetPointer(pGen, NULL, parse_Item);
	}

}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("InventorySlotInjectStates");
void exprInventorySlotInjectStates(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity* pEnt, S32 iBagIndex)
{
	if (pGen)
	{
		UIGen ***peaInstances = ui_GenGetInstances(pGen);
		InventorySlot ***peaData = ui_GenGetManagedListSafe(pGen, InventorySlot);
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		int i;
		if (eaSize(peaInstances) != eaSize(peaData))
			return;
		for (i = 0; i < eaSize(peaInstances); ++i)
		{
			UIGen *pChild = (*peaInstances)[i];
			InventorySlot *pSlot = (*peaData)[i];
			ui_GenStates(pChild,
				kUIGenStateInventorySlotEmpty,        !pSlot->pItem,
				kUIGenStateInventorySlotFilled,       pSlot->pItem,
				kUIGenStateInventorySlotInExperiment, pSlot->pItem ? ExperimentIsItemInListByBag(pSlot->pItem, iBagIndex, i, pExtract) : false,
				kUIGenStateInventorySlotInTrade,      pSlot->pItem ? pEnt && Item_IsBeingTraded(pEnt, pSlot->pItem) : false,
				kUIGenStateNone);
		}
	}
}

AUTO_STRUCT;
typedef struct InventorySpeciesUI {
	const char *pchDisplayName; AST(UNOWNED)
	char *pchSpeciesListPct; AST(ESTRING) // SpeciesDefs that are delimited by %.
} InventorySpeciesUI;

static int SortInventorySpeciesUIByName(const InventorySpeciesUI **ppLeft, const InventorySpeciesUI **ppRight, const void *pContext)
{
	return stricmp_safe((*ppLeft)->pchDisplayName, (*ppRight)->pchDisplayName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryBagsGetItemSpecies");
void gclInvExprInventoryBagsGetItemSpecies(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchBags, U32 uOptions)
{
	static SpeciesDef **s_eaSpecies = NULL;
	char *pchBuffer, *pchContext, *pchToken;
	S32 i, j;
	InventorySpeciesUI ***peaInventorySpeciesUI = ui_GenGetManagedListSafe(pGen, InventorySpeciesUI);
	S32 iCount = 0;
	bool bMergeByName = !!(uOptions & 1);

	// Go through all the items the bags
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	strdup_alloca(pchBuffer, pchBags);
	eaClearFast(&s_eaSpecies);
	if (pchToken = strtok_r(pchBuffer, " \r\n\t,|", &pchContext))
	{
		do
		{
			InvBagIDs eBag = StaticDefineIntGetInt(InvBagIDsEnum, pchToken);
			InventoryBag *pBag = eBag >= 0 ? CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBag, pExtract)) : NULL;
			if (pBag)
			{
				for (i = eaSize(&pBag->ppIndexedInventorySlots) - 1; i >= 0; i--)
				{
					InventorySlot *pSlot = pBag->ppIndexedInventorySlots[i];
					Item *pItem = pSlot ? pSlot->pItem : NULL;
					ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
					SpeciesDef *pSpecies = pItemDef ? GET_REF(pItemDef->hSpecies) : NULL;
					if (pSpecies)
						eaPushUnique(&s_eaSpecies, pSpecies);
				}
			}
		} while (pchToken = strtok_r(NULL, " \r\n\t,|", &pchContext));
	}

	// Generate species list
	for (i = 0; i < eaSize(&s_eaSpecies); i++)
	{
		SpeciesDef *pDef = s_eaSpecies[i];
		InventorySpeciesUI *pUI = NULL;
		const char *pchDisplayName = TranslateDisplayMessage(pDef->displayNameMsg);

		if (!pchDisplayName)
			continue;

		for (j = iCount; j < eaSize(peaInventorySpeciesUI); j++)
		{
			char *pchList = (*peaInventorySpeciesUI)[j]->pchSpeciesListPct;
			char *pch = strstri(pchList, pDef->pcName);
			int chLead = !pch || pch == pchList ? -1 : pch[-1];
			int chTail = pch ? pch[strlen(pDef->pcName)] : -1;
			if (pch && (chLead == -1 || chLead == '%') && (chTail == 0 || chTail == '%'))
			{
				if (j != iCount)
					eaMove(peaInventorySpeciesUI, iCount, j);
				pUI = (*peaInventorySpeciesUI)[iCount++];
				break;
			}
		}
		if (!pUI)
		{
			pUI = StructCreate(parse_InventorySpeciesUI);
			eaInsert(peaInventorySpeciesUI, pUI, iCount++);
		}

		pUI->pchDisplayName = pchDisplayName;
		estrClear(&pUI->pchSpeciesListPct);

		estrAppend2(&pUI->pchSpeciesListPct, pDef->pcName);
		estrConcatChar(&pUI->pchSpeciesListPct, '%');

		if (bMergeByName || pDef->pcSpeciesName && *pDef->pcSpeciesName)
		{
			for (j = i + 1; j < eaSize(&s_eaSpecies); j++)
			{
				bool bMerge = false;

				if (bMergeByName)
				{
					const char *pchName = TranslateDisplayMessage(s_eaSpecies[j]->displayNameMsg);
					if (pchName && !stricmp(pUI->pchDisplayName, pchName))
						bMerge = true;
				}
				else
				{
					const char *pchName = s_eaSpecies[j]->pcSpeciesName;
					if (pchName && !stricmp(pDef->pcSpeciesName, pchName))
						bMerge = true;
				}

				if (bMerge)
				{
					estrAppend2(&pUI->pchSpeciesListPct, s_eaSpecies[j]->pcName);
					estrConcatChar(&pUI->pchSpeciesListPct, '%');
					eaRemove(&s_eaSpecies, j);
				}
			}
		}
	}

	eaSetSizeStruct(peaInventorySpeciesUI, parse_InventorySpeciesUI, iCount);
	eaStableSort(*peaInventorySpeciesUI, NULL, SortInventorySpeciesUIByName);
	ui_GenSetManagedListSafe(pGen, peaInventorySpeciesUI, InventorySpeciesUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetExtraSafeRemove");
bool gclItemExprExtraSafeRemove(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = SAFE_GET_REF(pItem,hItem);
	return pItemDef && pItemDef->bExtraSafeRemove;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemDefGetExtraSafeRemove");
bool gclItemDefExprExtraSafeRemove(SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	return pItemDef && pItemDef->bExtraSafeRemove;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyGetExtraSafeRemove");
bool gclInventoryKeyExprExtraSafeRemove(const char *pchKey)
{
	UIInventoryKey Key = {0};
	return gclInventoryParseKey(pchKey, &Key) && Key.pSlot && gclItemExprExtraSafeRemove(Key.pSlot->pItem);
}

void gclSetCompareKeys(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVars, InventorySlot ***peaSlots)
{
	UIInventoryKey Key = {0};
	char *pchToken, *pchContext;
	S32 iSlot = 0;

	strdup_alloca(pchContext, pchVars);
	if (pchToken = strtok_r(pchContext, " ,\r\n\t", &pchContext))
	{
		do
		{
			InventorySlot *pSlot = eaGet(peaSlots, iSlot++);
			UIGenVarTypeGlob *pVar = eaIndexedGetUsingString(&pGen->eaVars, pchToken);

			if (!pVar)
			{
				ErrorFilenamef(exprContextGetBlameFile(pContext), "Variable %s not found in gen %s", pchToken, pGen->pchName);
			}
			else if (pSlot)
			{
				gclInventoryMakeSlotKey(NULL, NULL, pSlot, &Key);
				estrCopy2(&pVar->pchString, gclInventoryMakeKeyString(pContext, &Key));
			}
			else
			{
				estrClear(&pVar->pchString);
			}
		} while (pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext));
	}
}

void gclGetComparableSlots(SA_PARAM_NN_VALID InventorySlot ***peaSlots, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEntity)
{
	static InventoryBag **s_eaCheckBags;
	Entity *pOwner = gclInventoryGetOwner(pEntity);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	S32 iBag, iSlot, iFirstSlot = 0, iMaxSlots = 0;

	if (!pItemDef || !pEntity)
		return;

	// Get the valid equipment bags
	for (iBag = 0; iBag < eaiSize(&pItemDef->peRestrictBagIDs); iBag++)
	{
		InventoryBag *pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), pItemDef->peRestrictBagIDs[iBag], pExtract));

		if (!pBag || (invbag_flags(pBag) & (InvBagFlag_ActiveWeaponBag | InvBagFlag_WeaponBag | InvBagFlag_EquipBag)) == 0)
			continue;

		eaPush(&s_eaCheckBags, pBag);
		MAX1(iMaxSlots, eaSize(&pBag->ppIndexedInventorySlots));
	}

	// Apply slot restriction rules
	if (pItemDef->eRestrictSlotType == kSlotType_Secondary)
		iFirstSlot = 1;
	else if (pItemDef->eRestrictSlotType == kSlotType_Primary && iMaxSlots > 1)
		iMaxSlots = 1;

	// Get all the slots with an item in it.
	// This generates the list first by bag then by slot.
	for (iSlot = iFirstSlot; iSlot < iMaxSlots; iSlot++)
	{
		for (iBag = 0; iBag < eaSize(&s_eaCheckBags); iBag++)
		{
			InventoryBag *pBag = s_eaCheckBags[iBag];
			InventorySlot *pSlot = eaGet(&pBag->ppIndexedInventorySlots, iSlot);

			if (!pSlot || !pSlot->pItem || pSlot->pItem == pItem)
				continue;

			gclInventoryUpdateSlot(pEntity, pBag, pSlot);
			eaPush(peaSlots, pSlot);
		}
	}

	eaClearFast(&s_eaCheckBags);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemGetCompareKeys);
void gclItemExprGetCompareKeys(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVars, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEntity)
{
	static InventorySlot **s_eaSlots;
	gclGetComparableSlots(&s_eaSlots, pItem, pEntity);
	gclSetCompareKeys(pContext, pGen, pchVars, &s_eaSlots);
	eaClearFast(&s_eaSlots);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemGetCompareKeysSimilarPowerCategories);
void gclItemExprGetCompareKeysSimilarPowerCategories(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, const char *pchVars, SA_PARAM_OP_VALID Item *pItem, SA_PARAM_OP_VALID Entity *pEntity, const char *pchPowerCategories)
{
	static InventorySlot **s_eaSlots;
	static S32 *s_eaiValidPowerCategories;
	static S32 *s_eaiMatchedPowerCategories;
	S32 iNumPowers, iPower, iCategory, iSlot, iBetterSlot = 0;
	char *pchToken, *pchContext;

	// Extract the provided set of power categories
	strdup_alloca(pchContext, pchPowerCategories);
	if (pchToken = strtok_r(pchContext, " ,\r\n\t", &pchContext))
	{
		do
		{
			S32 eCat = StaticDefineIntGetInt(PowerCategoriesEnum, pchToken);
			if (eCat != -1)
				eaiPush(&s_eaiValidPowerCategories, eCat);
		}
		while (pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext));
	}

	// Get the list of power categories that the item has that are in the provided set
	iNumPowers = item_GetNumItemPowerDefs(pItem, true);
	for (iPower = 0; iPower < iNumPowers; iPower++)
	{
		PowerDef *pPowerDef = item_GetPowerDef(pItem, iPower);
		if (!pPowerDef)
			continue;

		for (iCategory = eaiSize(&pPowerDef->piCategories) - 1; iCategory >= 0; iCategory--)
		{
			if (eaiFind(&s_eaiValidPowerCategories, pPowerDef->piCategories[iCategory]) >= 0)
				eaiPush(&s_eaiMatchedPowerCategories, pPowerDef->piCategories[iCategory]);
		}
	}

	if (eaiSize(&s_eaiMatchedPowerCategories) > 0)
		gclGetComparableSlots(&s_eaSlots, pItem, pEntity);

	// Sort slotted items that share power categories that the provided item
	// to the beginning of the list.
	for (iSlot = 0; iSlot < eaSize(&s_eaSlots); iSlot++)
	{
		Item *pSlotItem = s_eaSlots[iSlot]->pItem;
		iNumPowers = item_GetNumItemPowerDefs(pSlotItem, true);
		for (iPower = 0; iPower < iNumPowers; iPower++)
		{
			bool bMatching = true;
			PowerDef *pPowerDef = item_GetPowerDef(pSlotItem, iPower);
			if (!pPowerDef)
				continue;

			for (iCategory = eaiSize(&pPowerDef->piCategories) - 1; iCategory >= 0; iCategory--)
			{
				if (eaiFind(&s_eaiMatchedPowerCategories, pPowerDef->piCategories[iCategory]) >= 0)
				{
					// Move current item to the better slot index
					if (iSlot != iBetterSlot)
						eaMove(&s_eaSlots, iBetterSlot, iSlot);
					iBetterSlot++;
					bMatching = false;
					break;
				}
			}

			if (!bMatching)
				break;
		}
	}

	gclSetCompareKeys(pContext, pGen, pchVars, &s_eaSlots);
	eaClearFast(&s_eaSlots);
	eaiClearFast(&s_eaiMatchedPowerCategories);
	eaiClearFast(&s_eaiValidPowerCategories);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(item_GetTransmutationCost);
S32 exprGetItemTransmutationCost(ExprContext* pContext, SA_PARAM_OP_VALID Item* pStats, SA_PARAM_OP_VALID Item* pAppearance)
{
	Entity *pPlayer = entActivePlayerPtr();
	if (pPlayer && pStats && pAppearance)
		return itemeval_GetTransmutationCost(pPlayer->iPartitionIdx_UseAccessor, pPlayer, pStats, pAppearance);
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryBagGetActiveSlotKey");
const char *gclInventoryBagExprGetActiveSlotKey(ExprContext *pContext, SA_PARAM_OP_VALID InventoryBag *pBag, S32 iIndex)
{
	Entity *pEntity = gclInventoryGetBagEntity(pBag);
	if (pBag && pEntity != NULL)
	{
		Entity *pOwner = gclInventoryGetOwner(pEntity);
		S32 iSlot = invbag_GetActiveSlot(pBag, iIndex);
		if (iSlot >= 0)
		{
			UIInventoryKey Key = {0};
			Key.erOwner = entGetRef(pOwner);
			Key.pEntity = pEntity;
			if (pOwner != pEntity)
			{
				Key.eType = entGetType(pEntity);
				Key.iContainerID = entGetContainerID(pEntity);
			}
			Key.eBag = pBag->BagID;
			Key.iSlot = iSlot;
			return gclInventoryMakeKeyString(pContext, &Key);
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("InventoryKeyGetItemPower");
SA_RET_OP_VALID Power *gclInventoryKeyExprGetItemPower(const char *pchKey, const char *pchPower)
{
	UIInventoryKey Key = {0};
	if (!gclInventoryParseKey(pchKey, &Key))
		return NULL;

	if (Key.pSlot && Key.pSlot->pItem)
	{
		S32 i, iPowers = item_GetNumItemPowerDefs(Key.pSlot->pItem, true);
		for (i = 0; i < iPowers; i++)
		{
			PowerDef *pPowerDef = item_GetPowerDef(Key.pSlot->pItem, i);
			if (pPowerDef && !stricmp(pPowerDef->pchName, pchPower))
				return item_GetPower(Key.pSlot->pItem, i);
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemIsUnidentified");
bool gclInventoryExprItemIsUnidentified(SA_PARAM_OP_VALID Item* pItem)
{
	return item_IsUnidentified(pItem);
}

#include "AutoGen/UIEnums_h_ast.c"
#include "FCInventoryUI_h_ast.c"
#include "FCInventoryUI_c_ast.c"
