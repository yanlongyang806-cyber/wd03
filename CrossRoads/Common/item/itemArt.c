/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "ItemArt.h"

#include "Character.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "dynFxInterface.h"
#include "dynFxInfo.h"
#include "../../WorldLib/AutoGen/dynFxInfo_h_ast.h"
#include "Entity.h"
#include "file.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "logging.h"
#include "powers.h"
#include "PowerAnimFX.h"
#include "PowerModes.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "TriCube/vec.h"
#include "dynAnimGraphPub.h"
#include "entCritter.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
#endif

#if GAMECLIENT
	#include "gclEntity.h"
	#include "EntityClient.h"
	#include "gclUIGenPaperdoll.h"
#endif

#include "AutoGen/ItemArt_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hItemArtDict;

// Static data for GeoName param passed to all ItemArt FX
static const char* s_pchGeoName = NULL;
static const char* s_pchBoneName = NULL;
static const char* s_pchPosName = NULL;
static const char* s_pchRotName = NULL;
static const char* s_pchSlotName = NULL;
static const char* s_pchColorName = NULL;
static const char* s_pchColor2Name = NULL;
static const char* s_pchColor3Name = NULL;
static const char* s_pchColor4Name = NULL;
static const char* s_pchOptionalName = NULL;
static const char* s_pchMatName = NULL;

AUTO_RUN;
void initItemArt(void)
{
	s_pchGeoName = allocAddString("GeoName");
	s_pchBoneName = allocAddString("BoneName");
	s_pchPosName = allocAddString("PositionParam");
	s_pchRotName = allocAddString("RotationParam");
	s_pchColorName = allocAddString("ColorParam");
	s_pchColor2Name = allocAddString("Color2Param");
	s_pchColor3Name = allocAddString("Color3Param");
	s_pchColor4Name = allocAddString("Color4Param");
	s_pchSlotName = allocAddString("EquippedSlotParam");
	s_pchOptionalName = allocAddString("OptionalParam");
	s_pchMatName = allocAddString("MaterialParam");
}

AUTO_FIXUPFUNC;
TextParserResult ItemArtFixup(ItemArt *pItemArt, enumTextParserFixupType eType, void *pExtraData)
{
	TextParserResult ret=PARSERESULT_SUCCESS;
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			// Derive proper pchName and pchScope from the filename
			char achTemp[512];
			const char *pchScopeStart, *pchScopeEnd;
			if(pItemArt->cpchName)
				pItemArt->cpchName = NULL;
			if(pItemArt->cpchScope)
				pItemArt->cpchScope = NULL;
			getFileNameNoExtNoDirs(achTemp, pItemArt->cpchFile);
			pItemArt->cpchName = allocAddString(achTemp);
			pchScopeStart = pItemArt->cpchFile + strlen("defs/itemart/");
			pchScopeEnd = strstri(pchScopeStart,pItemArt->cpchName);
			if(pchScopeEnd > pchScopeStart)
			{
				strncpy(achTemp,pchScopeStart,(pchScopeEnd-pchScopeStart)-1);
				pItemArt->cpchScope = allocAddString(achTemp);
			}

			FOR_EACH_IN_EARRAY(pItemArt->ppchPrimaryStanceWords, const char, pcStance)
			{
				if (!dynAnimStanceValid(pcStance))
				{
					ErrorFilenamef(pItemArt->cpchFile, "ItemArt references invalid stance word: %s", pcStance);
					ret = PARSERESULT_ERROR;
				}
			}
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY(pItemArt->ppchSecondaryStanceWords, const char, pcStance)
			{
				if (!dynAnimStanceValid(pcStance))
				{
					ErrorFilenamef(pItemArt->cpchFile, "ItemArt references invalid stance word: %s", pcStance);
					ret = PARSERESULT_ERROR;
				}
			}
			FOR_EACH_END;
		}
		break;
	}
	return ret;
}


static int ItemArtResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ItemArt *pItemArt, U32 userID)
{
//	switch (eType)
//	{
//	case RESVALIDATE_POST_BINNING:
//		ValidateAndGenerateAnimFX(pAfx);
//	return VALIDATE_HANDLED;
//	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int ItemArtRegisterDict(void)
{
	// Set up reference dictionary
	g_hItemArtDict = RefSystem_RegisterSelfDefiningDictionary("ItemArt", false, parse_ItemArt, true, true, NULL);

	resDictManageValidation(g_hItemArtDict, ItemArtResValidateCB);
	resDictSetDisplayName(g_hItemArtDict, "Item Art File", "Item Art Files", RESCATEGORY_ART);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hItemArtDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hItemArtDict, NULL, NULL, NULL, NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hItemArtDict, 16, false, resClientRequestSendReferentCommand);
	}
	return 1;
}


AUTO_STARTUP(ItemArt) ASTRT_DEPS(ItemTags ItemPowers);
void ItemArtLoad(void)
{
	if(IsGameServerSpecificallly_NotRelatedTypes() || IsClient())
	{
		int flags = PARSER_OPTIONALFLAG;
		if (IsServer())
		{
			flags |= RESOURCELOAD_SHAREDMEMORY;
		}
		resLoadResourcesFromDisk(g_hItemArtDict,"defs/itemart",".itemart", "ItemArt.bin", flags);
	}
}

static ItemArtBagState* CreateAndInitItemArtBagState(CharacterClass *pClass, ItemArt *pItemArt, bool bPrimary) 
{
	ItemArtBagState *pState = StructAlloc(parse_ItemArtBagState);

	// need an ID that should be unique
#define CLASS_ITEMART_BAD_ID	255

	pState->eBagID = CLASS_ITEMART_BAD_ID;
	pState->iSlot = bPrimary ? 0 : 1;
	COPY_HANDLE(pState->hItemArt,pClass->hArt);

	if (pItemArt->pchFXAdd)
		eaPush(&pState->ppchFXNames, pItemArt->pchFXAdd);
	if (pClass->pchFX)
		eaPush(&pState->ppchFXNames, pClass->pchFX);
	
	if (bPrimary)
	{
		if (pItemArt->pchFXPrimary)
			eaPush(&pState->ppchFXNames, pItemArt->pchFXPrimary);
	}
	else
	{
		if (pItemArt->pchFXSecondary)
			eaPush(&pState->ppchFXNames, pItemArt->pchFXSecondary);
		if (pItemArt->pchGeoSecondary)
			pState->pchGeoOverride = pItemArt->pchGeoSecondary;
	}
		
	if (gConf.bNewAnimationSystem)
		pState->ppchBits = bPrimary ? pItemArt->ppchPrimaryStanceWords : pItemArt->ppchSecondaryStanceWords;
	else
		pState->ppchBits = bPrimary ? pItemArt->ppchPrimaryBits : pItemArt->ppchSecondaryBits;

	pState->pchBone = bPrimary ? pItemArt->pchActivePrimaryBone : pItemArt->pchActiveSecondaryBone;
	copyVec3(pItemArt->posActive, pState->pos);
	copyVec3(pItemArt->rotActive, pState->rot);

	return pState;
}

static void EntityGenerateItemArtStateFromCharacterClass(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID ItemArtBagState ***pppStates)
{
	if (pEnt->pChar)
	{
		CharacterClass *pClass = character_GetClassCurrent(pEnt->pChar);
		ItemArt *pItemArt = SAFE_GET_REF(pClass,hArt);

		if (pItemArt)
		{
			ItemArtBagState *pState = CreateAndInitItemArtBagState(pClass, pItemArt, true);
				
			if (pState)
				eaPush(pppStates,pState);

			if (pItemArt->pchActiveSecondaryBone)
			{
				pState = CreateAndInitItemArtBagState(pClass, pItemArt, false);
				if (pState)
					eaPush(pppStates,pState);
			}
		}
	}
}

static void gclEntityGenerateItemArtStateFromCharacterClass(SA_PARAM_NN_VALID CharacterClass *pClass, SA_PARAM_NN_VALID ItemArtBagState ***pppStates)
{
	if (pClass)
	{
		ItemArt *pItemArt = SAFE_GET_REF(pClass,hArt);

		if (pItemArt)
		{
			ItemArtBagState *pState = CreateAndInitItemArtBagState(pClass, pItemArt, true);

			if (pState)
				eaPush(pppStates,pState);

			if (pItemArt->pchActiveSecondaryBone)
			{
				pState = CreateAndInitItemArtBagState(pClass, pItemArt, false);
				if (pState)
					eaPush(pppStates,pState);
			}
		}
	}
}

static void ItemArt_GetGeoAndFXOverrides(ItemArt* pItemArt, Item* pItem, const char** pchGeo, const char** pchFx)
{
	int j, k;
	for (j = 0; j < item_GetNumItemPowerDefs(pItem, true); j++)
	{
		ItemPowerDef* pExistingPowerDef = item_GetItemPowerDef(pItem, j);
		if (pExistingPowerDef && pExistingPowerDef->iArtCategory > 0)
		{
			for (k = 0; k < eaSize(&pItemArt->eaGeoList); k++)
			{
				if (pItemArt->eaGeoList[k]->eCat == pExistingPowerDef->iArtCategory)
				{
					if (!(*pchGeo))
						(*pchGeo) = pItemArt->eaGeoList[k]->pchGeo;
					if (!(*pchFx))
						(*pchFx) = pItemArt->eaGeoList[k]->pchFXAdd;
				}
				//break early if we found both
				if (*pchGeo && *pchFx)
					return;
			}
		}
	}
}

static ItemArt * itemArt_GetFinalItemArtFromItem(SA_PARAM_OP_VALID Item *pItem)
{
	ItemDef *pItemDef = NULL;

	if (pItem == NULL)
	{
		return NULL;
	}

	if (pItem &&
		pItem->pSpecialProps &&
		pItem->pSpecialProps->pTransmutationProps)
	{
		pItemDef = GET_REF(pItem->pSpecialProps->pTransmutationProps->hTransmutatedItemDef);
	}

	if (!pItemDef)
	{
		pItemDef = GET_REF(pItem->hItem);
	}

	return pItemDef ? GET_REF(pItemDef->hArt) : NULL;
}


// Generates data for InvBags that want to show ItemArt
static void EntityGenerateItemArtState(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID ItemArtBagState ***pppStates)
{
	S32 i, bActive;
	CritterDef *pCritterDef = NULL;
	
	if(!pEnt->pInventoryV2 || !pEnt->pChar || !pEnt->pEquippedArt)
		return;

	if(g_CombatConfig.pBattleForm && IS_HANDLE_ACTIVE(pEnt->hAllegiance) && pEnt->pChar && !pEnt->pChar->bBattleForm)
		return;

	PERFINFO_AUTO_START_FUNC();

	EntityGenerateItemArtStateFromCharacterClass(pEnt, pppStates);
	
	if(pEnt->pCritter)
	{
		pCritterDef = GET_REF(pEnt->pCritter->critterDef);
	}

	// Determine if we want any ItemArt in the "active" state or not.
	//  If you're dead or near dead, you just stay in the same state,
	//  otherwise it's just based on combat
	if(entCheckFlag(pEnt,ENTITYFLAG_DEAD) || pEnt->pChar->pNearDeath)
		bActive = !!pEnt->pEquippedArt->bActive;
	else
		bActive = (pEnt->pChar->uiTimeCombatExit || pEnt->pChar->uiTimeCombatVisualsExit);

	if(pCritterDef && pCritterDef->bAlwaysHaveWeaponsReady)
		bActive = true;

	pEnt->pEquippedArt->bActive = !!bActive;

	for(i=eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i>=0; i--)
	{
		const InvBagDef *pBagDef;
		InvBagIDs eBagID;
		S32 bActiveBag;
		Item *pItem = NULL;
		ItemArt *pItemArt = NULL;
		ItemArtBagState *pState;

		pBagDef = invbag_def(pEnt->pInventoryV2->ppInventoryBags[i]);

		// Gotta be a bag that at least shows active ItemArt
		if(!pBagDef || (!pBagDef->pItemArtActive && !pBagDef->pItemArtActiveSecondary))
			continue;

		// Get the BagID
		eBagID = invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]);

		// See if this bag wants to be active
		bActiveBag = bActive && -1!=eaiFind(&pEnt->pEquippedArt->piInvBagsReady,eBagID);
		
		// Skip if the bag's not active and doesn't show inactive ItemArt
		if((bActiveBag && pBagDef->pItemArtActive) || (!bActiveBag && pBagDef->pItemArtInactive))
		{
			
			if(pBagDef->flags & InvBagFlag_ActiveWeaponBag)
			{
				// If the bag has an active weapon, get that item
				pItem = invbag_GetActiveSlotItem(pEnt->pInventoryV2->ppInventoryBags[i], 0);
			}
			else
			{
				// Get the Item in slot 0 (currently only that one is relevant)
				pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], 0);
			}
			
			pItemArt = itemArt_GetFinalItemArtFromItem(pItem);

			// No item with ItemArt to actually show?
			if(pItemArt)		
			{
				//get the geo/fx from the itemart based on itempower categories
				const char* pchGeo = NULL;
				const char* pchFx = NULL;
				InvBagDefItemArt *pBagItemArt = NULL;
				if (pItemArt->eaGeoList)
				{
					ItemArt_GetGeoAndFXOverrides(pItemArt, pItem, &pchGeo, &pchFx);
				}

				pState = StructAlloc(parse_ItemArtBagState);
				pState->eBagID = eBagID;
				pState->pchGeoOverride = pchGeo;
				SET_HANDLE_FROM_REFERENT(g_hItemArtDict, pItemArt, pState->hItemArt);
				if(bActiveBag)
				{
					pBagItemArt = pBagDef->pItemArtActive;

					if (pItemArt->pchFXItemArtActive)
						eaPush(&pState->ppchFXNames, pItemArt->pchFXItemArtActive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					if (gConf.bNewAnimationSystem)
						pState->ppchBits = pItemArt->ppchPrimaryStanceWords;
					else
						pState->ppchBits = pItemArt->ppchPrimaryBits;
					pState->pchBone = pItemArt->pchActivePrimaryBone ? pItemArt->pchActivePrimaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posActive) ? pBagItemArt->vPosition : pItemArt->posActive, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotActive) ? pBagItemArt->vRotation : pItemArt->rotActive, pState->rot);
				}
				else
				{
					pBagItemArt = pBagDef->pItemArtInactive;

					if (pItemArt->pchFXItemArtInactive)
						eaPush(&pState->ppchFXNames, pItemArt->pchFXItemArtInactive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					pState->pchBone = pItemArt->pchHolsterPrimaryBone ? pItemArt->pchHolsterPrimaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posHolster) ? pBagItemArt->vPosition : pItemArt->posHolster, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotHolster) ? pBagItemArt->vRotation : pItemArt->rotHolster, pState->rot);
				}
				if (pchFx)
					eaPush(&pState->ppchFXNames, pchFx);
				else if (pItemArt->pchFXAdd)
					eaPush(&pState->ppchFXNames, pItemArt->pchFXAdd);
				pState->iSlot = 0;
				eaPush(pppStates,pState);
			}
		}

		if((bActiveBag && pBagDef->pItemArtActiveSecondary) || (!bActiveBag && pBagDef->pItemArtInactiveSecondary) ||
			(pItemArt && pItemArt->bAlwaysShowOnBothBones))
		{	
			// We may care about slot 1 as well
			if(pBagDef->flags & InvBagFlag_ActiveWeaponBag)
			{
				// Get the item in the first non active slot
				//pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], !pEnt->pInventoryV2->ppInventoryBags[i]->iActiveSlot);
			}
			else
			{
				pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], 1);
			}
			
			//if the primary item was supposed to show on both bones, show it here.
			if (!pItem && pItemArt && pItemArt->bAlwaysShowOnBothBones)
			{
				pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], 0);
			}

			pItemArt = itemArt_GetFinalItemArtFromItem(pItem);

			// No item with ItemArt to actually show?
			if(pItemArt)		
			{
				//get the geo/fx from the itemart based on itempower categories
				const char* pchGeo = NULL;
				const char* pchFx = NULL;
				InvBagDefItemArt *pBagItemArt = NULL;
				if (pItemArt->eaGeoList)
				{
					ItemArt_GetGeoAndFXOverrides(pItemArt, pItem, &pchGeo, &pchFx);
				}

				pState = StructAlloc(parse_ItemArtBagState);
				pState->eBagID = eBagID;
				SET_HANDLE_FROM_REFERENT(g_hItemArtDict, pItemArt, pState->hItemArt);

				if (pItemArt->pchGeoSecondary)
				{
					pState->pchGeoOverride = pItemArt->pchGeoSecondary;
				}
				else
				{
					pState->pchGeoOverride = pchGeo;
				}

				if(bActiveBag)
				{
					pBagItemArt = pBagDef->pItemArtActiveSecondary;

					if (pItemArt->pchSecondaryFXItemArtActive)
						eaPush(&pState->ppchFXNames, pItemArt->pchSecondaryFXItemArtActive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					if (gConf.bNewAnimationSystem)
						pState->ppchBits = pItemArt->ppchSecondaryStanceWords;
					else
						pState->ppchBits = pItemArt->ppchSecondaryBits;
					pState->pchBone = pItemArt->pchActiveSecondaryBone ? pItemArt->pchActiveSecondaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posActive) ? pBagItemArt->vPosition : pItemArt->posActive, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotActive) ? pBagItemArt->vRotation : pItemArt->rotActive, pState->rot);
				}
				else
				{
					pBagItemArt = pBagDef->pItemArtInactiveSecondary;

					if (pItemArt->pchSecondaryFXItemArtInactive)
						eaPush(&pState->ppchFXNames, pItemArt->pchSecondaryFXItemArtInactive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					pState->pchBone = pItemArt->pchHolsterSecondaryBone ? pItemArt->pchHolsterSecondaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posHolster) ? pBagItemArt->vPosition : pItemArt->posHolster, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotHolster) ? pBagItemArt->vRotation : pItemArt->rotHolster, pState->rot);
				}

				if (pchFx)
					eaPush(&pState->ppchFXNames, pchFx);
				else if (pItemArt->pchFXAdd)
					eaPush(&pState->ppchFXNames, pItemArt->pchFXAdd);

				pState->iSlot = 1;
				eaPush(pppStates,pState);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

// Returns true if the first Item in the Bag has all the ItemCategories required by the PowerCategory
static S32 InvBagHasItemForPowerCat(SA_PARAM_NN_VALID const InvBagDef *pBagDef, SA_PARAM_NN_VALID InventoryBag *pBag, SA_PARAM_NN_VALID PowerCategory *pCat)
{
	int i;
	S32 bValid = false;
	
	if(pBagDef->pItemArtActive)
	{
		// Get the Item in slot 0 (currently only that one is relevant)
		Item *pItem = inv_bag_GetItem(pBag, 0);
		ItemDef *pItemDef = SAFE_GET_REF(pItem,hItem);
		ItemArt *pItemArt = SAFE_GET_REF(pItemDef, hArt);

		if(pItemDef)
		{
			for(i=eaiSize(&pCat->piRequiredItemCategories)-1; i>=0; i--)
			{
				if (-1 == eaiFind(&pItemDef->peCategories, pCat->piRequiredItemCategories[i]) && 
					(pItemArt && (-1 == eaiFind(&pItemArt->peAdditionalCategories, pCat->piRequiredItemCategories[i]))))
					break;
			}
			bValid = (i==-1);
		}
	}
	
	if(bValid)
		return true;

	if(pBagDef->pItemArtActiveSecondary)
	{
		// Get the Item in slot 1 beacuse we have secondary fx
		Item *pItem = inv_bag_GetItem(pBag, 1);
		ItemDef *pItemDef = SAFE_GET_REF(pItem,hItem);
		ItemArt *pItemArt = SAFE_GET_REF(pItemDef, hArt);

		if(pItemDef)
		{
			for(i=eaiSize(&pCat->piRequiredItemCategories)-1; i>=0; i--)
			{
				if (-1 == eaiFind(&pItemDef->peCategories, pCat->piRequiredItemCategories[i]) && 
					(pItemArt && (-1 == eaiFind(&pItemArt->peAdditionalCategories, pCat->piRequiredItemCategories[i]))))
					break;
			}
			bValid = (i==-1);
		}
	}

	return bValid;
}


// Clears the existing ready bags, finds Items for the PowerCategory and readies those
//  bags.  Updates the ItemArt state if the list ends up changing.
void entity_ReadyItemsForPowerCat(Entity *pEnt, PowerCategory *pCat)
{
	int i;
	static S32 *s_piBagsOld = NULL;

	if (!pEnt->pInventoryV2)
		return;

	if(!pEnt->pEquippedArt)
		pEnt->pEquippedArt = StructCreate(parse_EquippedArt);

	eaiCopy(&s_piBagsOld,&pEnt->pEquippedArt->piInvBagsReady);

	eaiClearFast(&pEnt->pEquippedArt->piInvBagsReady);

	for(i=eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i>=0; i--)
	{
		const InvBagDef *pBagDef = invbag_def(pEnt->pInventoryV2->ppInventoryBags[i]);

		// Gotta be a bag that at least shows active ItemArt as evidenced by an FX name
		if(!pBagDef)
			continue;

		if(InvBagHasItemForPowerCat(pBagDef, pEnt->pInventoryV2->ppInventoryBags[i],pCat))
		{
			InvBagIDs eBagID = invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]);
			eaiPush(&pEnt->pEquippedArt->piInvBagsReady,eBagID);
		}
	}

	if(eaiCompare(&s_piBagsOld,&pEnt->pEquippedArt->piInvBagsReady))
	{
		// List of ready bags changed, update the ItemArt state
		entity_UpdateItemArtAnimFX(pEnt);
	}
}

// Updates the Entity's ItemArt state based on the ready bags and active state.
//  If there are no ready bags, generates the default ready bags list.
void entity_UpdateItemArtAnimFX(Entity *pEnt)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	S32 i, bStateDiff;
	EntityRef er = entGetRef(pEnt);
	static const char **s_ppchNames = NULL;
	ItemArtBagState **ppStates = NULL;

	if(!pEnt->pInventoryV2 || !pEnt->pChar || !gConf.bItemArt)
		return;

	PERFINFO_AUTO_START_FUNC();

	if(!pEnt->pEquippedArt)
	{
		pEnt->pEquippedArt = StructCreate(parse_EquippedArt);
		entity_SetDirtyBit(pEnt,parse_EquippedArt,pEnt->pEquippedArt,false);
	}
	else if(!pEnt->pEquippedArt->bCanUpdate)
		return;

	// If no bags are ready at all, ready all bags flagged as DefaultReady
	if(!eaiSize(&pEnt->pEquippedArt->piInvBagsReady))
	{
		for(i=eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i>=0; i--)
		{
			if(invbag_flags(pEnt->pInventoryV2->ppInventoryBags[i]) & InvBagFlag_DefaultReady)
			{
				InvBagIDs eBagID = invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]);
				eaiPush(&pEnt->pEquippedArt->piInvBagsReady,eBagID);
				entity_SetDirtyBit(pEnt,parse_EquippedArt,pEnt->pEquippedArt,false);
			}
		}
	}

	// Generate the target state
	EntityGenerateItemArtState(pEnt, &ppStates);
	
	// Figure out if the target state is different than the current state
	bStateDiff = (eaSize(&ppStates) != eaSize(&pEnt->pEquippedArt->ppState));
	if(!bStateDiff)
	{
		for(i=eaSize(&ppStates)-1; i>=0; i--)
		{
			if(StructCompare(parse_ItemArtBagState,ppStates[i],pEnt->pEquippedArt->ppState[i],0,0,0))
				break;
		}
		bStateDiff = (i>=0);
	}

	if(bStateDiff)
	{
		U32 uiTime = pmTimestamp(0);
		pEnt->pEquippedArt->bCanUpdate = false;
		PM_CREATE_SAFE(pEnt->pChar);

#if GAMESERVER
		ClientCmd_gclPaperDoll_resetAnimated(pEnt);
#endif

		// Stop any existing ItemArt fx/bits
		// TODO(JW): Could use ReplaceOrStart here, but we also need to replace if the params are different,
		//  which is a harder check.
		pmFxStop(pEnt->pChar->pPowersMovement, 0, 0, kPowerAnimFXType_ItemArt, er, er, uiTime, NULL);
		pmBitsStop(pEnt->pChar->pPowersMovement, 0, 0, kPowerAnimFXType_ItemArt, er, uiTime, false);

		for(i=eaSize(&ppStates)-1; i>=0; i--)
		{
			ItemArt *pItemArt = GET_REF(ppStates[i]->hItemArt);

			// TODO(JW): Geo should be based on the eBagID as specified in the ItemArt?  Or just a
			//  default with optional overrides based on eBagID?
			if(pItemArt && pItemArt->pchGeo && ppStates[i]->ppchFXNames)
			{
				DynParamBlock *pParamBlock;
				DynDefineParam *pParam;

				eaClearFast(&s_ppchNames);
				eaPushEArray(&s_ppchNames, &ppStates[i]->ppchFXNames);

				pParamBlock = dynParamBlockCreate();

				pParam = StructAlloc(parse_DynDefineParam);
				pParam->pcParamName = s_pchGeoName;
				if (!ppStates[i]->pchGeoOverride)
				{
					MultiValReferenceString(&pParam->mvVal, pItemArt->pchGeo);
				}
				else
				{
					MultiValReferenceString(&pParam->mvVal, ppStates[i]->pchGeoOverride);
				}

				eaPush(&pParamBlock->eaDefineParams, pParam);

				if(ppStates[i]->pchBone)
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchBoneName;
					MultiValReferenceString(&pParam->mvVal, ppStates[i]->pchBone);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				pParam = StructAlloc(parse_DynDefineParam);
				pParam->pcParamName = s_pchOptionalName;
				MultiValSetFloat(&pParam->mvVal, pItemArt->fOptionalParam);
				eaPush(&pParamBlock->eaDefineParams, pParam);

				if(!ISZEROVEC3(ppStates[i]->pos))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchPosName;
					MultiValSetVec3(&pParam->mvVal, &ppStates[i]->pos);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(pItemArt->pchMaterialReplace)
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchMatName;
					MultiValReferenceString(&pParam->mvVal, pItemArt->pchMaterialReplace);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC3(ppStates[i]->rot))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchRotName;
					MultiValSetVec3(&pParam->mvVal, &ppStates[i]->rot);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC3(pItemArt->vColor))
				{
					pParam = StructAlloc(parse_DynDefineParam); 
					pParam->pcParamName = s_pchColorName;
					MultiValSetVec3(&pParam->mvVal, &pItemArt->vColor);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC4(pItemArt->vColor2))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchColor2Name;
					MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor2);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC4(pItemArt->vColor3))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchColor3Name;
					MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor3);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC4(pItemArt->vColor4))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchColor4Name;
					MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor4);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				//pass through which slot this is equipped in
				pParam = StructAlloc(parse_DynDefineParam);
				pParam->pcParamName = s_pchSlotName;
				MultiValSetFloat(&pParam->mvVal, ppStates[i]->iSlot);
				eaPush(&pParamBlock->eaDefineParams, pParam);
				
				pmFxStart(pEnt->pChar->pPowersMovement,
					ppStates[i]->eBagID * 2 + ppStates[i]->iSlot,0,kPowerAnimFXType_ItemArt,
					er,
					er,
					uiTime,
					s_ppchNames,
					pParamBlock,
					pItemArt->fHue,
					0.0f,
					0.0f,
					0.0f,
					NULL,
					NULL,
					NULL,
					0,
					0);
			}

			if(eaSize(&ppStates[i]->ppchBits))
			{
				pmBitsStartSticky(	pEnt->pChar->pPowersMovement,
									ppStates[i]->eBagID * 2 + ppStates[i]->iSlot,0,kPowerAnimFXType_ItemArt,
									er,
									uiTime,
									ppStates[i]->ppchBits,
									false,
									false,
									false);
			}
		}

		// Replace the old state with the new
		eaDestroyStruct(&pEnt->pEquippedArt->ppState,parse_ItemArtBagState);
		eaCopy(&pEnt->pEquippedArt->ppState,&ppStates);
		eaDestroy(&ppStates);

		entity_SetDirtyBit(pEnt,parse_EquippedArt,pEnt->pEquippedArt,false);
	}
	else
	{
		eaDestroyStruct(&ppStates,parse_ItemArtBagState);
	}

	PERFINFO_AUTO_STOP();
#endif
}

PCFXTemp* gclItemArt_GetItemPreviewFX( SA_PARAM_NN_VALID ItemArt* pItemArt)
{
	PCFXTemp* pFX = NULL;
#if !GAMECLIENT
	assert(0);
#else
	if(pItemArt && pItemArt->pchGeo)
	{
		DynParamBlock *pParamBlock;
		DynDefineParam *pParam;

		pFX = StructAlloc(parse_PCFXTemp);

		pParamBlock = dynParamBlockCreate();

		pParam = StructAlloc(parse_DynDefineParam);
		pParam->pcParamName = s_pchGeoName;
		MultiValReferenceString(&pParam->mvVal, pItemArt->pchGeo);

		eaPush(&pParamBlock->eaDefineParams, pParam);

		if(pItemArt->pchMaterialReplace)
		{
			pParam = StructAlloc(parse_DynDefineParam);
			pParam->pcParamName = s_pchMatName;
			MultiValReferenceString(&pParam->mvVal, pItemArt->pchMaterialReplace);
			eaPush(&pParamBlock->eaDefineParams, pParam);
		}

		if(!ISZEROVEC3(pItemArt->vColor))
		{
			pParam = StructAlloc(parse_DynDefineParam); 
			pParam->pcParamName = s_pchColorName;
			MultiValSetVec3(&pParam->mvVal, &pItemArt->vColor);
			eaPush(&pParamBlock->eaDefineParams, pParam);
		}

		if(!ISZEROVEC4(pItemArt->vColor2))
		{
			pParam = StructAlloc(parse_DynDefineParam);
			pParam->pcParamName = s_pchColor2Name;
			MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor2);
			eaPush(&pParamBlock->eaDefineParams, pParam);
		}

		if(!ISZEROVEC4(pItemArt->vColor3))
		{
			pParam = StructAlloc(parse_DynDefineParam);
			pParam->pcParamName = s_pchColor3Name;
			MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor3);
			eaPush(&pParamBlock->eaDefineParams, pParam);
		}

		if(!ISZEROVEC4(pItemArt->vColor4))
		{
			pParam = StructAlloc(parse_DynDefineParam);
			pParam->pcParamName = s_pchColor4Name;
			MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor4);
			eaPush(&pParamBlock->eaDefineParams, pParam);
		}

		pFX->pcName = allocAddString("FX_ItemArt_Preview");
		pFX->fHue = pItemArt->fHue;
		pFX->pParams = pParamBlock;
	}
#endif
	return pFX;
}

//If the client needs to do some itemart processing (for example, headshots),
// it needs to store the states outside of the entity because the client
// can't modify server-maintained fields.
static EquippedArt s_ClientArt;

static void gclEntityGenerateItemArtState(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID ItemArtBagState ***pppStates, SA_PARAM_NN_VALID CharacterClass *pClass)
{
	S32 i, bActive;

	if(!pEnt->pInventoryV2)
		return;

	PERFINFO_AUTO_START_FUNC();

	gclEntityGenerateItemArtStateFromCharacterClass(pClass, pppStates);

	bActive = false; //This will need to change if we decide we want to be able to have characters holding their weapons

	s_ClientArt.bActive = !!bActive;

	for(i=eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i>=0; i--)
	{
		const InvBagDef *pBagDef;
		InvBagIDs eBagID;
		S32 bActiveBag;
		Item *pItem = NULL;
		ItemArt *pItemArt = NULL;
		ItemArtBagState *pState;

		pBagDef = invbag_def(pEnt->pInventoryV2->ppInventoryBags[i]);

		// Gotta be a bag that at least shows active ItemArt
		if(!pBagDef || (!pBagDef->pItemArtActive && !pBagDef->pItemArtActiveSecondary))
			continue;

		// Get the BagID
		eBagID = invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]);

		// See if this bag wants to be active
		bActiveBag = bActive && -1!=eaiFind(&s_ClientArt.piInvBagsReady,eBagID);

		// Skip if the bag's not active and doesn't show inactive ItemArt
		if((bActiveBag && pBagDef->pItemArtActive) || (!bActiveBag && pBagDef->pItemArtInactive))
		{

			if(pBagDef->flags & InvBagFlag_ActiveWeaponBag)
			{
				// If the bag has an active weapon, get that item
				pItem = invbag_GetActiveSlotItem(pEnt->pInventoryV2->ppInventoryBags[i], 0);
			}
			else
			{
				// Get the Item in slot 0 (currently only that one is relevant)
				pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], 0);
			}

			pItemArt = itemArt_GetFinalItemArtFromItem(pItem);

			// No item with ItemArt to actually show?
			if(pItemArt)		
			{
				//get the geo/fx from the itemart based on itempower categories
				const char* pchGeo = NULL;
				const char* pchFx = NULL;
				InvBagDefItemArt *pBagItemArt = NULL;
				if (pItemArt->eaGeoList)
				{
					ItemArt_GetGeoAndFXOverrides(pItemArt, pItem, &pchGeo, &pchFx);
				}

				pState = StructAlloc(parse_ItemArtBagState);
				pState->eBagID = eBagID;
				pState->pchGeoOverride = pchGeo;
				SET_HANDLE_FROM_REFERENT(g_hItemArtDict, pItemArt, pState->hItemArt);
				if(bActiveBag)
				{
					pBagItemArt = pBagDef->pItemArtActive;

					if (pItemArt->pchFXItemArtActive)
						eaPush(&pState->ppchFXNames, pItemArt->pchFXItemArtActive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					if (gConf.bNewAnimationSystem)
						pState->ppchBits = pItemArt->ppchPrimaryStanceWords;
					else
						pState->ppchBits = pItemArt->ppchPrimaryBits;
					pState->pchBone = pItemArt->pchActivePrimaryBone ? pItemArt->pchActivePrimaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posActive) ? pBagItemArt->vPosition : pItemArt->posActive, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotActive) ? pBagItemArt->vRotation : pItemArt->rotActive, pState->rot);
				}
				else
				{
					pBagItemArt = pBagDef->pItemArtInactive;

					if (pItemArt->pchFXItemArtInactive)
						eaPush(&pState->ppchFXNames, pItemArt->pchFXItemArtInactive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					pState->pchBone = pItemArt->pchHolsterPrimaryBone ? pItemArt->pchHolsterPrimaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posHolster) ? pBagItemArt->vPosition : pItemArt->posHolster, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotHolster) ? pBagItemArt->vRotation : pItemArt->rotHolster, pState->rot);
				}
				if (pchFx)
					eaPush(&pState->ppchFXNames, pchFx);
				else if (pItemArt->pchFXAdd)
					eaPush(&pState->ppchFXNames, pItemArt->pchFXAdd);
				pState->iSlot = 0;
				eaPush(pppStates,pState);
			}
		}

		if((bActiveBag && pBagDef->pItemArtActiveSecondary) || (!bActiveBag && pBagDef->pItemArtInactiveSecondary) ||
			(pItemArt && pItemArt->bAlwaysShowOnBothBones))
		{	
			// We may care about slot 1 as well
			if(pBagDef->flags & InvBagFlag_ActiveWeaponBag)
			{
				// Get the item in the first non active slot
				//pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], !pEnt->pInventoryV2->ppInventoryBags[i]->iActiveSlot);
			}
			else
			{
				pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], 1);
			}

			//if the primary item was supposed to show on both bones, show it here.
			if (!pItem && pItemArt && pItemArt->bAlwaysShowOnBothBones)
			{
				pItem = inv_bag_GetItem(pEnt->pInventoryV2->ppInventoryBags[i], 0);
			}

			pItemArt = itemArt_GetFinalItemArtFromItem(pItem);

			// No item with ItemArt to actually show?
			if(pItemArt)		
			{
				//get the geo/fx from the itemart based on itempower categories
				const char* pchGeo = NULL;
				const char* pchFx = NULL;
				InvBagDefItemArt *pBagItemArt = NULL;
				if (pItemArt->eaGeoList)
				{
					ItemArt_GetGeoAndFXOverrides(pItemArt, pItem, &pchGeo, &pchFx);
				}

				pState = StructAlloc(parse_ItemArtBagState);
				pState->eBagID = eBagID;
				SET_HANDLE_FROM_REFERENT(g_hItemArtDict, pItemArt, pState->hItemArt);

				if (pItemArt->pchGeoSecondary)
				{
					pState->pchGeoOverride = pItemArt->pchGeoSecondary;
				}
				else
				{
					pState->pchGeoOverride = pchGeo;
				}

				if(bActiveBag)
				{
					pBagItemArt = pBagDef->pItemArtActiveSecondary;

					if (pItemArt->pchSecondaryFXItemArtActive)
						eaPush(&pState->ppchFXNames, pItemArt->pchSecondaryFXItemArtActive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					if (gConf.bNewAnimationSystem)
						pState->ppchBits = pItemArt->ppchSecondaryStanceWords;
					else
						pState->ppchBits = pItemArt->ppchSecondaryBits;
					pState->pchBone = pItemArt->pchActiveSecondaryBone ? pItemArt->pchActiveSecondaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posActive) ? pBagItemArt->vPosition : pItemArt->posActive, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotActive) ? pBagItemArt->vRotation : pItemArt->rotActive, pState->rot);
				}
				else
				{
					pBagItemArt = pBagDef->pItemArtInactiveSecondary;

					if (pItemArt->pchSecondaryFXItemArtInactive)
						eaPush(&pState->ppchFXNames, pItemArt->pchSecondaryFXItemArtInactive);
					else
						eaPush(&pState->ppchFXNames, pBagItemArt->pchFX);

					pState->pchBone = pItemArt->pchHolsterSecondaryBone ? pItemArt->pchHolsterSecondaryBone : pBagItemArt->pchBone;
					copyVec3(ISZEROVEC3(pItemArt->posHolster) ? pBagItemArt->vPosition : pItemArt->posHolster, pState->pos);
					copyVec3(ISZEROVEC3(pItemArt->rotHolster) ? pBagItemArt->vRotation : pItemArt->rotHolster, pState->rot);
				}

				if (pchFx)
					eaPush(&pState->ppchFXNames, pchFx);
				else if (pItemArt->pchFXAdd)
					eaPush(&pState->ppchFXNames, pItemArt->pchFXAdd);

				pState->iSlot = 1;
				eaPush(pppStates,pState);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

//Generates a list of item art anim fx and pushes them into the passed earray
bool gclEntity_UpdateItemArtAnimFX( SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID CharacterClass *pClass, PCFXTemp ***peaFX, bool bForceUpdate )
{
#if !GAMECLIENT
	assert(0);
#else
	S32 i, bStateDiff;
	EntityRef er = entGetRef(pEnt);
	static const char **s_ppchNames = NULL;
	ItemArtBagState **ppStates = NULL;

	if(!pEnt->pInventoryV2 || !gConf.bItemArt || !pClass)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (!pEnt->pEquippedArt)
		StructInit(parse_EquippedArt, &s_ClientArt);
	else
		StructCopyAll(parse_EquippedArt, pEnt->pEquippedArt, &s_ClientArt);

	// If no bags are ready at all, ready all bags flagged as DefaultReady
	if(!eaiSize(&s_ClientArt.piInvBagsReady))
	{
		for(i=eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i>=0; i--)
		{
			if(invbag_flags(pEnt->pInventoryV2->ppInventoryBags[i]) & InvBagFlag_DefaultReady)
			{
				InvBagIDs eBagID = invbag_bagid(pEnt->pInventoryV2->ppInventoryBags[i]);
				eaiPush(&s_ClientArt.piInvBagsReady,eBagID);
			}
		}
	}

	// Generate the target state
	gclEntityGenerateItemArtState(pEnt, &ppStates, pClass);

	// Figure out if the target state is different than the current state
	bStateDiff = (eaSize(&ppStates) != eaSize(&s_ClientArt.ppState));
	if(!bStateDiff && !bForceUpdate)
	{
		for(i=eaSize(&ppStates)-1; i>=0; i--)
		{
			if(StructCompare(parse_ItemArtBagState,ppStates[i],s_ClientArt.ppState[i],0,0,0))
				break;
		}
		bStateDiff = (i>=0);
	}

	if(bStateDiff || bForceUpdate)
	{
		U32 uiTime = pmTimestamp(0);
		s_ClientArt.bCanUpdate = false;

		eaClearStruct(peaFX, parse_PCFXTemp);
		
		for(i=eaSize(&ppStates)-1; i>=0; i--)
		{
			ItemArt *pItemArt = GET_REF(ppStates[i]->hItemArt);

			// TODO(JW): Geo should be based on the eBagID as specified in the ItemArt?  Or just a
			//  default with optional overrides based on eBagID?
			if(pItemArt && pItemArt->pchGeo && ppStates[i]->ppchFXNames)
			{
				DynParamBlock *pParamBlock;
				DynDefineParam *pParam;

				eaClearFast(&s_ppchNames);
				eaPushEArray(&s_ppchNames, &ppStates[i]->ppchFXNames);

				pParamBlock = dynParamBlockCreate();

				pParam = StructAlloc(parse_DynDefineParam);
				pParam->pcParamName = s_pchGeoName;
				if (!ppStates[i]->pchGeoOverride)
				{
					MultiValReferenceString(&pParam->mvVal, pItemArt->pchGeo);
				}
				else
				{
					MultiValReferenceString(&pParam->mvVal, ppStates[i]->pchGeoOverride);
				}

				eaPush(&pParamBlock->eaDefineParams, pParam);

				if(ppStates[i]->pchBone)
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchBoneName;
					MultiValReferenceString(&pParam->mvVal, ppStates[i]->pchBone);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				pParam = StructAlloc(parse_DynDefineParam);
				pParam->pcParamName = s_pchOptionalName;
				MultiValSetFloat(&pParam->mvVal, pItemArt->fOptionalParam);
				eaPush(&pParamBlock->eaDefineParams, pParam);

				if(!ISZEROVEC3(ppStates[i]->pos))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchPosName;
					MultiValSetVec3(&pParam->mvVal, &ppStates[i]->pos);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(pItemArt->pchMaterialReplace)
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchMatName;
					MultiValReferenceString(&pParam->mvVal, pItemArt->pchMaterialReplace);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC3(ppStates[i]->rot))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchRotName;
					MultiValSetVec3(&pParam->mvVal, &ppStates[i]->rot);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC3(pItemArt->vColor))
				{
					pParam = StructAlloc(parse_DynDefineParam); 
					pParam->pcParamName = s_pchColorName;
					MultiValSetVec3(&pParam->mvVal, &pItemArt->vColor);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC4(pItemArt->vColor2))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchColor2Name;
					MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor2);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC4(pItemArt->vColor3))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchColor3Name;
					MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor3);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				if(!ISZEROVEC4(pItemArt->vColor4))
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_pchColor4Name;
					MultiValSetVec4(&pParam->mvVal, &pItemArt->vColor4);
					eaPush(&pParamBlock->eaDefineParams, pParam);
				}

				//pass through which slot this is equipped in
				pParam = StructAlloc(parse_DynDefineParam);
				pParam->pcParamName = s_pchSlotName;
				MultiValSetFloat(&pParam->mvVal, ppStates[i]->iSlot);
				eaPush(&pParamBlock->eaDefineParams, pParam);

				{
					int j;
					for(j = 0; j < eaSize(&ppStates[i]->ppchFXNames); ++j) {
						PCFXTemp *pFX = StructAlloc(parse_PCFXTemp);
						pFX->pcName = allocAddString(ppStates[i]->ppchFXNames[j]);
						pFX->fHue = pItemArt->fHue;
						pFX->pParams = dynParamBlockCopy(pParamBlock);
						eaPush(peaFX, pFX);
					}
				}

				dynParamBlockFree(pParamBlock);
			}
		}

		// Replace the old state with the new
		eaDestroyStruct(&s_ClientArt.ppState,parse_ItemArtBagState);
		eaCopy(&s_ClientArt.ppState,&ppStates);
		eaDestroy(&ppStates);
	}
	else
	{
		eaDestroyStruct(&ppStates,parse_ItemArtBagState);
	}

	PERFINFO_AUTO_STOP();
	return bStateDiff;
#endif
	return false;
}

#include "AutoGen/ItemArt_h_ast.c"
