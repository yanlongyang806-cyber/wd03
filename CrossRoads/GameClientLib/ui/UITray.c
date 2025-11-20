/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "UITray.h"
#include "AutoGen/UITray_h_ast.h"
#include "AutoGen/UITray_c_ast.h"

#include "Character.h"
#include "CharacterAttribs.h"
#include "Character_combat.h"
#include "Character_Target.h"
#include "ClientTargeting.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "cmdClient.h"
#include "cmdparse.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameClientLib.h"
#include "gclControlScheme.h"
#include "gclCursorMode.h"
#include "gclEntity.h"
#include "gclUIGen.h"
#include "gclWarp.h"
#include "GlobalTypes_h_ast.h"
#include "GraphicsLib.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "Player.h"
#include "Powers_h_ast.h"
#include "PowerActivation.h"
#include "CombatPowerStateSwitching.h"
#include "PowerAnimFX.h"
#include "PowersAutoDesc.h"
#include "PowersMovement.h"
#include "PowerReplace.h"
#include "PowerSlots.h"
#include "PowersUI.h"
#include "ResourceInfo.h"
#include "SavedPetCommon.h"
#include "soundlib.h"
#include "StringCache.h"
#include "Tray.h"
#include "Team.h"
#include "Tray_h_ast.h"
#include "UIGen.h"
#include "FCInventoryUI.h"
#include "gclCursorModePowerLocationTargeting.h"
#include "gclCursorModePowerTargeting.h"
#include "gclHUDOptions.h"
#include "GamePermissionsCommon.h"
#include "WorldGrid.h"
#include "UIEnums.h"
#include "NotifyCommon.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/AILib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_ENUM;
typedef enum UITrayPowerSortType
{
	UITrayPowerSortType_None = 0,
	UITrayPowerSortType_SmallestArc = 1,
} UITrayPowerSortType;

AUTO_STRUCT;
typedef struct UIPowerElem
{
	const char *pchIcon;	AST(POOL_STRING)

	char *pchToolTip;		AST(ESTRING)

	PowerPurpose ePurpose;
	U32 uiID;

	U32 bValid : 1;
} UIPowerElem;

// Client-only mapping of UITray index to Tray index
// Temporarily overrides the entity's UITray data
AUTO_STRUCT;
typedef struct UITrayOverlay
{
	int iUITray;
	int iTray;
} UITrayOverlay;

static UITrayOverlay** s_eaTrayOverlays = NULL;

AUTO_RUN;
void gclTray_Init(void)
{
	ui_GenInitStaticDefineVars(UITrayPowerSortTypeEnum, "TrayPowerSortType_");
}

static int gclTrayFindUIOverlay(int iUITray)
{
	int i;
	for (i = eaSize(&s_eaTrayOverlays)-1; i >= 0; i--)
	{
		if (s_eaTrayOverlays[i]->iUITray == iUITray)
		{
			return i;
		}
	}
	return -1;
}

// Create a temporary override for the entity's UITray data.
bool gclTraySetUIOverlay(int iUITray, int iTray)
{
	if(iUITray>=0 && iUITray<TRAY_UI_COUNT_MAX)
	{
		int iOverlay = gclTrayFindUIOverlay(iUITray);
		if (iOverlay >= 0)
		{
			s_eaTrayOverlays[iOverlay]->iTray = iTray;
		}
		else
		{
			UITrayOverlay* pOverlay = StructCreate(parse_UITrayOverlay);
			pOverlay->iUITray = iUITray;
			pOverlay->iTray = iTray;
			eaPush(&s_eaTrayOverlays, pOverlay);
		}
		return true;
	}
	return false;
}

// Remove a tray overlay at the specified UITray index.
bool gclTrayRemoveUIOverlay(int iUITray)
{
	int iOverlay = gclTrayFindUIOverlay(iUITray);

	if (iOverlay >= 0)
	{
		StructDestroy(parse_UITrayOverlay, eaRemove(&s_eaTrayOverlays, iOverlay));
		return true;
	}
	return false;
}

// Gets the tray index in the Entity's UITray array.  Returns iUITray if unset.
int entity_TrayGetUITrayIndex(Entity *e, int iUITray)
{
	int r = iUITray;
	SavedTray* pTray = entity_GetActiveTray(e);

	if(pTray && iUITray>=0 && iUITray<TRAY_UI_COUNT_MAX)
	{
		int s = eaiSize(&pTray->piUITrayIndex);
		if(iUITray < s)
			r = pTray->piUITrayIndex[iUITray];
	}
	return r;
}

// Gets the TrayElem given the UITray and Slot.  May return NULL.
static TrayElem *entity_TrayGetUITrayElem(Entity *e, int iUITray, int iSlot)
{
	if(iUITray>=0 && iUITray<TRAY_UI_COUNT_MAX)
	{
		int iTray = entity_TrayGetUITrayIndex(e,iUITray);
		return entity_TrayGetTrayElem(e,iTray,iSlot);
	}
	return NULL;
}

static S32 TrayFindKeyByPower(SA_PARAM_OP_VALID Power* pPower, bool bUseBaseReplacePower)
{
	Entity* pEntity = entActivePlayerPtr();
	SavedTray* pTray = entity_GetActiveTray(pEntity);

	if (pEntity && pEntity->pChar && pTray && pPower)
	{
		S32 i;
		for(i = eaSize(&pTray->ppTrayElems)-1; i >= 0; i--)
		{
			int iTray = pTray->ppTrayElems[i]->iTray;
			int iSlot = pTray->ppTrayElems[i]->iTraySlot;
			TrayElem* pElem = entity_TrayGetTrayElem(pEntity,iTray,iSlot);
			Power* pTrayPower = pElem ? entity_TrayGetPower(pEntity,pElem) : NULL;
			if (!bUseBaseReplacePower && pTrayPower && pTrayPower->uiReplacementID)
				pTrayPower = character_FindPowerByID(pEntity->pChar,pTrayPower->uiReplacementID);

			if (pTrayPower && pPower->uiID == pTrayPower->uiID)
				return (iTray * 256) | (iSlot);
		}
	}

	return -1;
}


static bool TrayPowerMatchesTrayKey(SA_PARAM_OP_VALID Power* pPower, S32 iKey, bool bUseBaseReplacePower)
{
	if ( iKey >= 0 )
	{
		Entity* pEntity = entActivePlayerPtr();
		S32 iTray = iKey >> 8;
		S32 iSlot = iKey & 255;

		if ( pEntity && pEntity->pChar && pPower )
		{
			TrayElem* pElem = entity_TrayGetTrayElem(pEntity,iTray,iSlot);
			Power* pTrayPower = pElem ? entity_TrayGetPower(pEntity,pElem) : NULL;
			if ( !bUseBaseReplacePower && pTrayPower && pTrayPower->uiReplacementID)
				pTrayPower = character_FindPowerByID(pEntity->pChar,pTrayPower->uiReplacementID);

			return pTrayPower && pPower->uiID == pTrayPower->uiID;
		}
	}

	return false;
}

static bool PowerTargetInArc(Entity *pEnt, Power *pPower, Entity *pEntTarget, WorldInteractionNode *pNodeTarget)
{
	PowerDef *pDef = pPower ? GET_REF(pPower->hDef) : NULL;

	if (pEnt && pEnt->pChar && pDef)
	{
		if ( pDef->fTargetArc < 0.001f )// the power doesn't have a firing arc, meaning that it's omni-directional
			return true;

		if(pNodeTarget)
		{
			return entity_TargetNodeInArc(pEnt,pNodeTarget,NULL,RAD(pDef->fTargetArc),pPower->fYaw);
		}
		else
		{
			return entity_TargetInArc(pEnt,pEntTarget,NULL,RAD(pDef->fTargetArc),pPower->fYaw);
		}
	}
	return false;
}

static PowerTarget* gclTray_GetPowerTargetType(SA_PARAM_NN_VALID Character *pchar, Power* pPower)
{
	PowerDef* pDef = pPower ? GET_REF(pPower->hDef) : NULL;
	PowerTarget* pPowTarget = pDef ? GET_REF(pDef->hTargetMain) : NULL;
	PowerTarget* pPowAffect = pDef ? GET_REF(pDef->hTargetAffected) : NULL;

	if (!character_PowerRequiresValidTarget(pchar, pDef) &&
		pPowTarget && pPowTarget->bRequireSelf &&
		pPowAffect && pPowAffect->bAllowFoe)
	{
		// Change the target to what is affected for tray checks
		return pPowAffect;
	}
	return pPowTarget;
}

static bool PlayerPowerTargetValidMainTarget(Entity *pEnt, Power* pPow, PowerTarget *pPowTargetOverride)
{
	PowerDef *pDef = pPow ? GET_REF(pPow->hDef) : NULL;
	PowerTarget *pPowtarget = pPowTargetOverride ? pPowTargetOverride : (pDef ? GET_REF(pDef->hTargetMain) : NULL);

	if(pEnt && pEnt->pChar && pPowtarget)
	{
		Entity *pTarget = entFromEntityRefAnyPartition(pEnt->pChar->currentTargetRef);

		if(pTarget && pTarget->pChar)
		{
			if(!character_TargetMatchesPowerType(PARTITION_CLIENT,pEnt->pChar,pTarget->pChar,pPowtarget))
			{
				return false;
			}
		}
		else if (!character_TargetMatchesPowerTypeNode(pEnt->pChar, pPowtarget))
		{
			return false;
		}

		return true;
	}

	return false;
}

SA_RET_OP_VALID static Entity *EntTrayGetExecutor(SA_PARAM_OP_VALID Entity *e,
											   SA_PARAM_OP_VALID TrayElem *pelem,
											   SA_PARAM_OP_VALID PetTrayElemData *pPetData)
{
	if(e)
	{
		if (pPetData && pPetData->erOwner)
			e = entFromEntityRefAnyPartition(pPetData->erOwner);
		else if (pelem && pelem->erOwner)
			e = entFromEntityRefAnyPartition(pelem->erOwner);
	}
	return e;
}

SA_RET_OP_VALID Entity *UITrayGetOwner(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	if (pelem)
	{
		if (pelem->pPetData)
		{
			return entFromEntityRefAnyPartition(pelem->pPetData->erOwner);
		}
		else if (pelem->pTrayElem)
		{
			return entFromEntityRefAnyPartition(pelem->pTrayElem->erOwner);
		}
	}
	return NULL;
}

// Utility function to gets the PetPowerState given the TrayElem.  May return NULL.
PetPowerState *UITrayElemGetPetPowerState(UITrayElem *pelem)
{
	Entity *ePlayer = entActivePlayerPtr();
	PetPowerState *pState = NULL;
	Entity *ePet = UITrayGetOwner(pelem);
	if (ePlayer && ePlayer->pPlayer && ePet)
	{
		PowerDef *pdef = NULL;
		if (pelem->pPetData)
		{
			pdef = powerdef_Find(pelem->pPetData->pchPower);
		}
		else if (pelem->pTrayElem)
		{
			pdef = entity_TrayGetPowerDef(ePet, pelem->pTrayElem);
		}
		if (pdef)
		{
			pState = player_GetPetPowerState(ePlayer->pPlayer,entGetRef(ePet),pdef);
		}
	}
	return pState;
}


// Utility function to set the UITrayElem's TrayElem, handles destroying the pre-existing TrayElem if
//  there is one, and cleaning up the UITrayElem's flags if the new TrayElem is NULL.
void tray_UITrayElemSetTrayElem(UITrayElem *pUIElem, TrayElem *pElem, bool bOwned)
{
	if (pUIElem->pTrayElem && pUIElem->bOwnedTrayElem)
		StructDestroySafe(parse_TrayElem, &pUIElem->pTrayElem);
	if (pUIElem->pPetData)
		StructDestroySafe(parse_PetTrayElemData, &pUIElem->pPetData);

	if(pElem)
	{
		pUIElem->pTrayElem = pElem;
		pUIElem->bOwnedTrayElem = bOwned;
	}
	else
	{
		ZeroStruct(pUIElem);
	}
}


AUTO_FIXUPFUNC;
TextParserResult ui_UITrayElemParserFixup(UITrayElem *pElem, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		tray_UITrayElemSetTrayElem(pElem, NULL, false);
	}
	return PARSERESULT_SUCCESS;
}

SA_ORET_OP_VALID static Power *EntGetActivatedPower(SA_PARAM_NN_VALID Entity *e, Power *pPow, bool bUseBaseReplacePower, Power **ppPowerTrayBase)
{
	Power *ppow = NULL;
	if(e->pChar)
	{
		Character *pchar = e->pChar;
		ppow = pPow;
		if(ppow)
		{
			Entity *eTarget = entFromEntityRefAnyPartition(pchar->currentTargetRef);
			Power *ppowActual = character_PickActivatedPower(entGetPartitionIdx(e), pchar, ppow, eTarget, NULL, NULL, NULL, NULL, true, !bUseBaseReplacePower, NULL);

			if(ppowActual)
			{
				ppow = ppowActual;
			}
		}

		//If we want the base tray power, return it here
		if(ppPowerTrayBase) *ppPowerTrayBase = ppow;

		// If we're physically holding an object, we try to find a reasonable alternative
		if(IS_HANDLE_ACTIVE(pchar->hHeldNode))
		{
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			if(!ppow || (pdef && !powerdef_IgnoresAttrib(pdef,kAttribType_BePickedUp)))
			{
				Power *ppowActual = character_FindPowerByCategory(pchar,"HeldObject");
				if(ppowActual)
				{
					ppow = ppowActual;
				}
			}
		}

		// If we're held, we try to find a reasonable alternative
		if(character_IsHeld(pchar))
		{
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			if(!ppow || (pdef && !powerdef_IgnoresAttrib(pdef,kAttribType_Hold)))
			{
				Power *ppowActual = character_FindPowerByCategory(pchar,"Struggle");
				if(ppowActual)
				{
					ppow = ppowActual;
				}
			}
		}
		else if(pchar->pattrBasic->fDisable > 0)
		{
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
			if(!ppow || (pdef && !powerdef_IgnoresAttrib(pdef,kAttribType_Disable)))
			{
				Power *ppowActual = character_FindPowerByCategory(pchar,"Struggle");
				if(ppowActual)
				{
					ppow = ppowActual;
				}
			}
		}
	}
	return ppow;
}

SA_RET_OP_VALID Power *EntTrayGetActivatedPower(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID TrayElem *pelem, bool bUseBaseReplacePower, Power **ppPowerTrayBase)
{
	return EntGetActivatedPower(e, entity_TrayGetPower(e,pelem), bUseBaseReplacePower, ppPowerTrayBase);
}

SA_RET_NN_VALID PowerDef *PetEntGuessExecutedPower(SA_PARAM_NN_VALID Entity *pent, SA_PARAM_NN_VALID PowerDef *pdef)
{
	static Power *s_apDummyPowers[24];
	static U32 s_aiUsed[24];
	int iCache, iCacheSave = -1;

	if (pdef->eType != kPowerType_Combo)
		return pdef;

	// Only do this crappy prediction for combo powers.

	for (iCache = 0; iCache < ARRAY_SIZE_CHECKED(s_apDummyPowers); iCache++)
	{
		if (s_apDummyPowers[iCache] && GET_REF(s_apDummyPowers[iCache]->hDef) == pdef) {
			if (eaSize(&s_apDummyPowers[iCache]->ppSubPowers) != eaSize(&pdef->ppCombos)) {
				StructDestroy(parse_Power,s_apDummyPowers[iCache]);
				s_apDummyPowers[iCache] = NULL;
			} else {
				break;
			}
		} else if (iCacheSave == -1 && !s_apDummyPowers[iCache]) {
			iCacheSave = iCache;
		}
	}

	if (iCache >= ARRAY_SIZE_CHECKED(s_apDummyPowers))
	{
		if (iCacheSave == -1) {
			// Destroy the LRU dummy power
			iCacheSave = 0;
			for (iCache = 1; iCache < ARRAY_SIZE_CHECKED(s_apDummyPowers); iCache++) {
				if (s_aiUsed[iCache] < s_aiUsed[iCacheSave]) {
					iCacheSave = iCache;
				}
			}
			StructDestroy(parse_Power,s_apDummyPowers[iCacheSave]);
			s_apDummyPowers[iCacheSave] = NULL;
		}

		iCache = iCacheSave;
		s_apDummyPowers[iCache] = power_Create(pdef->pchName);
		power_CreateSubPowers(s_apDummyPowers[iCache]);
	}

	if (iCache < ARRAY_SIZE_CHECKED(s_apDummyPowers))
	{
		Power *pact = character_PickActivatedPower(entGetPartitionIdx(pent), pent->pChar, s_apDummyPowers[iCache], NULL, NULL, NULL, NULL, NULL, true, false, NULL);
		PowerDef *pactdef = pact ? GET_REF(pact->hDef) : NULL;
		s_aiUsed[iCache] = gGCLState.totalElapsedTimeMs;
		return pactdef ? pactdef : pdef;
	}

	return pdef;
}

static void EntPowerGetTarget(SA_PARAM_NN_VALID Character *pchar, Power *ppow, Entity **pEntTarget, WorldInteractionNode **pNodeTarget)
{
	PowerTarget *ppowtarget = gclTray_GetPowerTargetType(pchar, ppow);
	ClientTargetDef *pClientTarget = clientTarget_SelectBestTargetForPowerEx(pchar->pEntParent,ppow,ppowtarget,NULL);
	(*pEntTarget) = pClientTarget ? entFromEntityRefAnyPartition(pClientTarget->entRef) : NULL;
	(*pNodeTarget) = pClientTarget ? GET_REF(pClientTarget->hInteractionNode) : NULL;

	if ((*pEntTarget) && GET_REF((*pEntTarget)->hCreatorNode))
	{
		(*pNodeTarget) = GET_REF((*pEntTarget)->hCreatorNode);
		(*pEntTarget) = NULL;
	}
}

static bool EntIsPowerTrayActivatableInternal(SA_PARAM_NN_VALID Character *pchar, Power *ppow,
											  Entity *pEntTarget, WorldInteractionNode *pNodeTarget,
											  GameAccountDataExtract *pExtract)
{
	Power *ppowBase = ppow->pParentPower ? ppow->pParentPower : ppow;
	PowerDef *pdefBase = GET_REF(ppowBase->hDef);
	PowerDef *pdef = GET_REF(ppow->hDef);
	PowerTarget *ppowTarget = gclTray_GetPowerTargetType(pchar, ppow);
	ActivationFailureReason	queuefailedReason = kActivationFailureReason_None;

	if (	pdef
		&&  (!pdef->bHasWarpAttrib || character_CanUseWarpPower(pchar, pdef))
		&&	character_PayPowerCost(PARTITION_CLIENT,pchar,ppow,0,NULL,false,pExtract)
		&&	character_CanActivatePowerDef(pchar,pdef,true,false,NULL,pExtract,NULL)
		&&  character_CheckPowerQueueRequires(PARTITION_CLIENT,pchar, ppow, pEntTarget, NULL, NULL, NULL, &queuefailedReason, true, pExtract)
		&&	(!pdefBase->bSlottingRequired || character_PowerIDSlotted(pchar,ppowBase->uiID))
		&&	(character_PowerBasicDisable(PARTITION_CLIENT,pchar,ppow) <= 0 || (pdef && powerdef_IgnoresAttrib(pdef,kAttribType_Disable))))
	{
		if (ppowTarget)
		{
			ANALYSIS_ASSUME(ppowTarget);
			if (character_TargetMatchesPowerType(PARTITION_CLIENT,pchar,pchar,ppowTarget))
			{
				return true;
			}
		}

		return !character_PowerRequiresValidTarget(pchar, pdef) || ((pNodeTarget || pEntTarget)
			&&	(!g_CombatConfig.bCheckMainTarget || PlayerPowerTargetValidMainTarget(pchar->pEntParent, ppow, ppowTarget))
			&&	character_TargetInPowerRange(pchar, ppow, pdef, pEntTarget, pNodeTarget)
			&&	(pdef->eTargetVisibilityMain != kTargetVisibility_LineOfSight || clientTarget_IsTargetInLoS(pchar->pEntParent,NULL,pEntTarget,pNodeTarget))
			&&	(pdef->fTargetArc < 0.001f || PowerTargetInArc(pchar->pEntParent,ppow,pEntTarget,pNodeTarget)));
	}

	return false;
}

bool EntIsPowerTrayActivatable(SA_PARAM_NN_VALID Character *pchar, Power *ppow, Entity* pEntTarget, GameAccountDataExtract *pExtract)
{
	WorldInteractionNode* pNodeTarget = NULL;
	if (!pEntTarget)
		EntPowerGetTarget( pchar, ppow, &pEntTarget, &pNodeTarget );
	return EntIsPowerTrayActivatableInternal(pchar, ppow, pEntTarget, pNodeTarget, pExtract);
}

void DEFAULT_LATELINK_GameSpecific_TrayUpdateHighlightTypeForElem(Entity* pEnt,
																  PowerDef* pPowerDef,
																  UITrayElem* pElem,
																  Entity* pEntTarget,
																  WorldInteractionNode* pNodeTarget)
{
	// Creatures method - Find Exploit-flagged mods with requires and see if they're legal
	int i;
	for(i=eaSize(&pPowerDef->ppOrderedMods)-1; i>=0; i--)
	{
		AttribModDef *pmoddef = pPowerDef->ppOrderedMods[i];
		if(pmoddef->eFlags & kCombatTrackerFlag_Exploit && pmoddef->pExprRequires && pEntTarget)
		{
			F32 fRequires;
			combateval_ContextSetupApply(pEnt->pChar,pEntTarget->pChar,NULL,NULL);
			fRequires = combateval_EvalNew(PARTITION_CLIENT,pmoddef->pExprRequires,kCombatEvalContext_Apply,NULL);
			if(fRequires)
			{
				pElem->bHighlight = true;
			}
		}
	}
}

static void TrayUpdateHighlightTypeForElem(Entity* pEnt,
										   Power* ppow,
										   UITrayElem* pElem,
										   Entity* pEntTarget,
										   WorldInteractionNode* pNodeTarget)
{
	PowerDef* ppowdef = GET_REF(ppow->hDef);

	if (!ppowdef)
		return;

	if (gConf.bExposeExploitTray)
	{
		pElem->bHighlight = false;

		GameSpecific_TrayUpdateHighlightTypeForElem(pEnt, ppowdef, pElem, pEntTarget, pNodeTarget);
	}
}

static Item* TrayInventoryElemGetItem(SA_PARAM_NN_VALID Entity *pEntity,
									  SA_PARAM_OP_VALID TrayElem *pTrayElem,
									  GameAccountDataExtract *pExtract)
{
	if (pTrayElem && pTrayElem->eType == kTrayElemType_InventorySlot)
	{
		S32 iBag = -1, iSlot = -1, iItemPowIdx = -1;
		Item *pItem = NULL;
		ItemDef *pItemDef = NULL;
		tray_InventorySlotStringToIDs(pTrayElem->pchIdentifier,&iBag,&iSlot,&iItemPowIdx);
		pItem = inv_GetItemFromBag(pEntity, iBag, iSlot, pExtract);
		return pItem;
	}
	return NULL;
}

static bool EntSetTrayElemWarpData(SA_PARAM_NN_VALID Entity *pEntity,
									SA_PARAM_NN_VALID UITrayElem *pelem,
									GameAccountDataExtract *pExtract)
{
	StructDestroySafe(parse_PetTrayElemData, &pelem->pPetData);

	pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,NULL);

	if(pEntity && pEntity->pChar && pelem->pTrayElem)
	{
		const char *pchInventorySlot = pelem->pTrayElem->pchIdentifier;
		if(pchInventorySlot)
		{
			Item *pItem = TrayInventoryElemGetItem(pEntity, pelem->pTrayElem, pExtract);
			ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

			if(pItemDef && pItemDef->pWarp)
			{
				pelem->pchIcon = allocAddString(item_GetIconName(pItem, pItemDef));
				pelem->pSourceItem = pItem;
				pelem->bActivatable = gclWarp_CanExec(pEntity, pItem, pItemDef);

				return true;
			}
		}
	}

	pelem->bEnoughPower = false;
	pelem->bLineOfSight = false;
	pelem->bInRange = false;
	pelem->bFacing = false;
	pelem->bNotDisabled = false;
	pelem->bActive = false;
	pelem->bCurrent = false;
	pelem->bQueued = false;
	pelem->bRecharging = false;
	pelem->bRefillingCharges = false;
	pelem->iNumTotalCharges = 0;
	pelem->bActivatable = false;
	pelem->bAutoActivating = false;
	pelem->bAutoActivateEnabled = false;
	pelem->pchIcon = NULL;
	pelem->pSourceItem = NULL;
	pelem->bValid = false;
	return false;
}

static void EntSetTrayElemMacroData(SA_PARAM_NN_VALID Entity *e,
									SA_PARAM_NN_VALID UITrayElem *pelem,
									SA_PARAM_OP_STR const char *cpchDefaultIcon)
{
	PlayerMacro* pMacro = NULL;
	S32 iMacroIdx = entity_FindMacroByID(e, SAFE_MEMBER(pelem->pTrayElem, lIdentifier));

	if (iMacroIdx >= 0)
	{
		pMacro = e->pPlayer->pUI->eaMacros[iMacroIdx];
	}

	if (pMacro && pMacro->pchIcon && pMacro->pchIcon[0])
	{
		pelem->pchIcon = gclGetBestPowerIcon(pMacro->pchIcon, pelem->pchIcon);
	}
	else
	{
		pelem->pchIcon = gclGetBestPowerIcon(cpchDefaultIcon, pelem->pchIcon);
	}
	if (pMacro) {
		estrCopy2(&pelem->estrShortDesc, pMacro->pchDescription);
	} else {
		estrClear(&pelem->estrShortDesc);
	}

	pelem->bValid = true;
	pelem->bActive = false;
	pelem->bCurrent = false;
	pelem->bCharging = false;
	pelem->bMaintaining = false;
	pelem->bQueued = false;
	pelem->bRecharging = false;
	pelem->bRefillingCharges = false;
	pelem->iNumTotalCharges = 0;
	pelem->bInCooldown = false;
	pelem->bAutoActivating = false;
	pelem->bModeEnabled = false;
	pelem->bActivatable = true;
	StructDestroySafe(parse_PetTrayElemData, &pelem->pPetData);
}

// Utility function to fill in UITrayElem data based on pet information.
void EntSetPetTrayElemPowerDataEx(UITrayElem *pelem,
								  Entity *pPlayerEnt,
								  PlayerPetInfo *pPetInfo,
								  PetPowerState *pPetPowerState,
								  PowerDef *pdef,
								  const char *cpchDefaultIcon)
{
	Player *pPlayer = pPlayerEnt->pPlayer;

	if(pdef && pdef->pchIconName)
	{
		pelem->pchIcon = gclGetBestPowerIcon(pdef->pchIconName, pelem->pchIcon);
	}
	else
	{
		pelem->pchIcon = gclGetBestPowerIcon(cpchDefaultIcon, pelem->pchIcon);
	}

	pelem->bValid = true;
	pelem->bActive = false;
	pelem->bCurrent = false;
	pelem->bCharging = false;
	pelem->bMaintaining = false;

	if (pPetPowerState) {
		pelem->bQueued = pPetPowerState->bQueuedForUse;
		pelem->bRecharging = (pPetPowerState->fTimerRecharge > 0);
		pelem->bAutoActivating = pPetPowerState->bAIUsageDisabled;
	} else {
		pelem->bQueued = false;
		pelem->bRecharging = false;
		pelem->bAutoActivating = false;
	}
	if (pPetInfo) {
		pelem->bInCooldown = (player_GetPetCooldownFromPowerDef(pPlayer, pPetInfo->iPetRef, pdef) > 0);
	} else {
		pelem->bInCooldown = false;
	}
	pelem->bModeEnabled = false;

	if (pelem->pPetData)
	{
		pelem->pPetData->fTimerRecharge = SAFE_MEMBER(pPetPowerState, fTimerRecharge);
		pelem->pPetData->fTimerRechargeBase = SAFE_MEMBER(pPetPowerState, fTimerRechargeBase);
	}
	pelem->bActivatable = !pelem->bRecharging && !pelem->bInCooldown;
	
	if (pdef)
	{
		pelem->ePurpose = pdef->ePurpose;
	}
}

static void EntSetPetTrayElemPowerData(SA_PARAM_NN_VALID UITrayElem *pelem,
									   SA_PARAM_NN_VALID Entity *pPlayerEnt,
									   SA_PARAM_OP_STR const char *cpchDefaultIcon)
{
	const char* pchPowerName = SAFE_MEMBER(pelem->pTrayElem, pchIdentifier);
	PowerDef *pDefExec = NULL;
	PlayerPetInfo* pPetInfo = NULL;
	PetPowerState* pPetPowerState = NULL;

	if (pchPowerName)
	{
		PowerDef *pDef;
		Entity* pPetEnt;
		ANALYSIS_ASSUME(pchPowerName);
		pDef = powerdef_Find(pchPowerName);
		pDefExec = pDef;
		pPetEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET, pelem->pTrayElem->lIdentifier);
		pPetInfo = pPetEnt ? player_GetPetInfo(pPlayerEnt->pPlayer, entGetRef(pPetEnt)) : NULL;
		pPetPowerState = playerPetInfo_FindPetPowerStateByName(pPetInfo, pchPowerName);

		if (pDef && !pDef->bHideInUI && pPetEnt && pPetPowerState)
		{
			ANALYSIS_ASSUME(pDef);
			ANALYSIS_ASSUME(pPetEnt);
			// Figure out what power will get executed
			pDefExec = PetEntGuessExecutedPower(pPetEnt, pDef);

			if (!pelem->pPetData)
				pelem->pPetData = StructCreate(parse_PetTrayElemData);

			pelem->pPetData->erOwner = entGetRef(pPetEnt);
			pelem->pPetData->pchPower = pDef->pchName;
			EntSetPetTrayElemPowerDataEx(pelem, pPlayerEnt, pPetInfo, pPetPowerState, pDefExec, cpchDefaultIcon);
			return;
		}
	}

	StructDestroySafe(parse_PetTrayElemData, &pelem->pPetData);
	EntSetPetTrayElemPowerDataEx(pelem, pPlayerEnt, pPetInfo, pPetPowerState, pDefExec, cpchDefaultIcon);
}

static bool EntSetTrayElemPowerData(SA_PARAM_NN_VALID Entity *pEntity,
									SA_PARAM_NN_VALID UITrayElem *pelem,
									SA_PARAM_OP_STR const char *cpchDefaultIcon,
									bool bDisable,
									GameAccountDataExtract *pExtract)
{
	StructDestroySafe(parse_PetTrayElemData, &pelem->pPetData);

	pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,NULL);

	if(pEntity && pEntity->pChar && pelem->pTrayElem)
	{
		Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
		Power *ppowBase = ppow && ppow->pParentPower ? ppow->pParentPower : ppow;
		PowerDef *pdefBase = ppowBase ? GET_REF(ppowBase->hDef) : NULL;

		F32 fCooldownGlobalQueueTime = g_CombatConfig.fCooldownGlobalQueueTime;

		// Handle building the element if it's a Power
		if(ppow && !SAFE_MEMBER(pdef, bHideInUI) && !SAFE_MEMBER(pdefBase, bHideInUI))
		{
			Entity *pEntTarget;
			WorldInteractionNode *pNodeTarget;
			Character *pchar = pEntity->pChar;

			EntPowerGetTarget(pchar, ppow, &pEntTarget, &pNodeTarget);

			if (ppowBase->eSource == kPowerSource_Item && ppowBase->pSourceItem)
			{
				pelem->pSourceItem = ppowBase->pSourceItem;
			}
			else // This else case probably isn't necessary, but keep it just to be safe
			{
				pelem->pSourceItem = TrayInventoryElemGetItem(pEntity, pelem->pTrayElem, pExtract);
			}

			if(pdef && pdef->pchIconName)
			{
				pelem->pchIcon = gclGetBestPowerIcon(pdef->pchIconName, pelem->pchIcon);
			}
			else if(ppowBase!=ppow && GET_REF(ppowBase->hDef) && GET_REF(ppowBase->hDef)->pchIconName)
			{
				PowerDef* pPowBaseDef = GET_REF(ppowBase->hDef);
				pelem->pchIcon = gclGetBestPowerIcon(pPowBaseDef->pchIconName, pelem->pchIcon);
			}
			else if(ppowBase->eSource==kPowerSource_Item)
			{
				Item *pitem = pelem->pSourceItem;
				if(pitem || item_FindPowerByID(pEntity,ppowBase->uiID,NULL,NULL,&pitem,NULL))
				{
					pelem->pchIcon = allocAddString(item_GetIconName(pitem, NULL));
				}
			}
			else
			{
				pelem->pchIcon = gclGetBestPowerIcon(cpchDefaultIcon, NULL);
			}

			pelem->bValid = true;
			pelem->bActive = !!ppowBase->bActive;
			pelem->bCurrent = pchar->pPowActCurrent && pchar->pPowActCurrent->ref.uiID == ppowBase->uiID;
			pelem->bQueued = (pchar->pPowActQueued && pchar->pPowActQueued->ref.uiID == ppowBase->uiID) || clientTarget_IsMultiPowerExec(ppowBase->uiID);
			pelem->bMultiExec = clientTarget_IsMultiPowerExec(ppow->uiID);
			pelem->bCharging = (pelem->bCurrent && pchar->eChargeMode==kChargeMode_Current) || (pelem->bCurrent && pchar->eChargeMode==kChargeMode_Queued);
			pelem->bMaintaining = (pelem->bCurrent && pchar->eChargeMode==kChargeMode_CurrentMaintain) || (pelem->bCurrent && pchar->eChargeMode==kChargeMode_QueuedMaintain);
			pelem->bRecharging = !(pelem->bCurrent || pelem->bQueued) && power_GetRecharge(ppowBase) > FLT_EPSILON;
			pelem->iNumTotalCharges = pdef ? pdef->iCharges : 1;
			pelem->bRefillingCharges = power_GetChargeRefillTime(ppowBase) > FLT_EPSILON;
			pelem->bInCooldown = !(pelem->bCurrent || pelem->bQueued) &&
				(character_GetCooldownFromPower(pchar,ppowBase) > FLT_EPSILON
				|| (pdefBase && !pdefBase->bCooldownGlobalNotChecked && fCooldownGlobalQueueTime && pchar->fCooldownGlobalTimer > fCooldownGlobalQueueTime));
			pelem->bModeEnabled = !(pelem->bCurrent || pelem->bQueued) && character_GetModeEnabledTime(pchar, ppow, gConf.bSelfOnlyPowerModeTimers) > FLT_EPSILON;
			pelem->bInterruptOnRequest = (pdef && (pdef->eInterrupts & kPowerInterruption_Requested));

			if (pelem->bUseBaseReplacePower || bDisable)
			{
				pelem->bActivatable = false;
				pelem->bAutoActivating = false;
				pelem->bAutoActivateEnabled = false;
			}
			else
			{
				pelem->bEnoughPower = pdef && character_PayPowerCost(PARTITION_CLIENT,pchar,ppow,0,NULL,false,pExtract);
				pelem->bLineOfSight = pdef && (!character_PowerRequiresValidTarget(pchar, pdef) || ((pNodeTarget || pEntTarget) && ((pdef->eTargetVisibilityMain != kTargetVisibility_LineOfSight || clientTarget_IsTargetInLoS(pchar->pEntParent,NULL,pEntTarget,pNodeTarget)))));
				pelem->bInRange = pdef && (!character_PowerRequiresValidTarget(pchar, pdef) || ((pNodeTarget || pEntTarget) && character_TargetInPowerRange(pchar, ppow, pdef, pEntTarget, pNodeTarget)));
				pelem->bFacing = pdef && (!character_PowerRequiresValidTarget(pchar, pdef) || ((pdef->fTargetArc < 0.001f || PowerTargetInArc(pchar->pEntParent,ppow,pEntTarget,pNodeTarget))));
				pelem->bNotDisabled = character_PowerBasicDisable(PARTITION_CLIENT,pchar,ppow) <= 0 || (pdef && powerdef_IgnoresAttrib(pdef,kAttribType_Disable));
				pelem->bActivatable =
					pelem->bEnoughPower && pelem->bLineOfSight
					&& pelem->bInRange && pelem->bFacing
					&& pelem->bNotDisabled
					&& EntIsPowerTrayActivatableInternal(pchar, ppow, pEntTarget, pNodeTarget, pExtract);

				pelem->bAutoActivating = gclAutoAttack_PowerIDAttacking(ppowBase->uiID);
				pelem->bAutoActivateEnabled = gclAutoAttack_PowerIDLegal(ppowBase->uiID);
			}

			pelem->bWillActivateOnUse = pelem->bActivatable && !pelem->bInCooldown && !pelem->bRecharging && !pelem->bCurrent;
			
			pelem->bStateSwitchedPower = (ppow->pCombatPowerStateParent != NULL) || 
										(CombatPowerStateSwitching_GetSwitchedStatePowerSlot(pchar, pelem->iSlot) >= 0);

			TrayUpdateHighlightTypeForElem(pEntity,ppow,pelem,pEntTarget,pNodeTarget);
			return true;
		}
	}

	pelem->bEnoughPower = false;
	pelem->bLineOfSight = false;
	pelem->bInRange = false;
	pelem->bFacing = false;
	pelem->bNotDisabled = false;
	pelem->bActive = false;
	pelem->bCurrent = false;
	pelem->bQueued = false;
	pelem->bRecharging = false;
	pelem->bRefillingCharges = false;
	pelem->iNumTotalCharges = 0;
	pelem->bActivatable = false;
	pelem->bWillActivateOnUse = false;
	pelem->bAutoActivating = false;
	pelem->bAutoActivateEnabled = false;
	pelem->pchIcon = gclGetBestPowerIcon(cpchDefaultIcon,NULL);
	pelem->pSourceItem = NULL;
	pelem->bValid = false;
	pelem->bStateSwitchedPower = false;
	return false;
}

static void EntGetTrayElems(SA_PARAM_NN_VALID UIGen *pGen,
							Entity *pEntity,
							int iTray,
							int iSlotMin,
							int iSlotMax,
							int bShowKeybind,
							int iTrayCommand,
							const char* pchFallbackIcon)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
	SavedTray* pTray = entity_GetActiveTray(pEntity);

	if (pTray && iSlotMin >= 0 && iSlotMax >= iSlotMin)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		S32 i;
		S32 iSize = 1 + iSlotMax - iSlotMin;

		// Make sure the array is the correct size.
		eaSetSizeStruct(peaElems, parse_UITrayElem, iSize);

		// Reset every slot to empty...
		for (i = 0; i < iSize; i++)
		{
			UITrayElem *pelem = (*peaElems)[i];
			tray_UITrayElemSetTrayElem(pelem, NULL, false);
			pelem->iTray = iTray;
			pelem->iSlot = iSlotMin + i;
			pelem->pchDragType = "TrayElem";
			pelem->iKey = (iTray * 256) | (iSlotMin + i);
			pelem->bDirty = false;
		}

		for (i = eaSize(&pTray->ppTrayElems) - 1; i >= 0; i--)
		{
			if (pTray->ppTrayElems[i]->iTray == iTray
				&& pTray->ppTrayElems[i]->iTraySlot >= iSlotMin
				&& pTray->ppTrayElems[i]->iTraySlot <= iSlotMax)
			{
				TrayElem* pTrayElem = pTray->ppTrayElems[i];
				S32 iElemIndex = pTrayElem->iTraySlot - iSlotMin;
				UITrayElem *pelem = eaGet(peaElems, iElemIndex);
				tray_UITrayElemSetTrayElem(pelem, pTrayElem, false);

				switch (pTrayElem->eType)
				{
					xcase kTrayElemType_SavedPetPower:
						EntSetPetTrayElemPowerData(pelem, pEntity, pchFallbackIcon);
					xcase kTrayElemType_Macro:
						EntSetTrayElemMacroData(pEntity, pelem, pchFallbackIcon);
					xdefault:
						EntSetTrayElemPowerData(pEntity, pelem, pchFallbackIcon, false, pExtract);
				}
				pelem->bDirty = true;
			}
		}

		for (i = 0; i < iSize; i++)
		{
			UITrayElem *pelem = (*peaElems)[i];
			if (!pelem->bDirty)
			{
				pelem->pchIcon = NULL;
				pelem->pSourceItem = NULL;
			}
			pelem->bDirty = false;
		}
	}
	else
	{
		eaClearStruct(peaElems, parse_UITrayElem);
	}

	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

// Sets the gen's managed list to temporary TrayElems based on Powers
static void EntGetPowersTrayElems(SA_PARAM_NN_VALID UIGen *pGen,
								  Entity *pEntity,
								  TrayElemOwner eOwner,
								  const char* pchCategory,
								  const char* pchFallbackIcon,
								  bool bShowKeybind,
								  bool bReferenceTrayElems)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
	int j=0;

	if(pEntity && pEntity->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		int i,s=eaSize(&pEntity->pChar->ppPowers);
		int iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);
		for(i=0; i<s; i++)
		{
			Power *ppow = pEntity->pChar->ppPowers[i];
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

			if(pdef && POWERTYPE_ACTIVATABLE(pdef->eType) && !pdef->bHideInUI)
			{
				UITrayElem *pelem;
				int iKey;

				if ( pchCategory && pchCategory[0] && (iCategory < 0 || eaiFind(&pdef->piCategories,iCategory) < 0) )
					continue;

				// Make sure we have a place for this in the array
				if(j>=eaSize(peaElems))
				{
					eaPush(peaElems, StructAlloc(parse_UITrayElem));
				}
				pelem = eaGet(peaElems,j);
				j++;
				iKey = pelem->iKey;

				if (	!bReferenceTrayElems
					||	((!pelem->bReferencesTrayElem || !TrayPowerMatchesTrayKey(ppow,pelem->iKey,0)) && (iKey = TrayFindKeyByPower(ppow,0)) < 0))
				{
					pelem->iKey = (S32)ppow->uiID;
					pelem->bReferencesTrayElem = false;
					pelem->pchDragType = "PowerID";
					pelem->iTray = (S32)ppow->uiID;
					pelem->iSlot = i;
				}
				else
				{
					pelem->iKey = iKey;
					pelem->bReferencesTrayElem = true;
					pelem->pchDragType = "TrayElem";
					pelem->iTray = iKey >> 8;
					pelem->iSlot = iKey & 255;
				}

				if (pelem->pTrayElem && pelem->bOwnedTrayElem)
				{
					tray_SetTrayElemForPowerID(pelem->pTrayElem, -1, -1, ppow->uiID);
				}
				else
				{
					tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForPowerID(-1,-1,ppow->uiID), true);
				}

				if(pelem->pTrayElem)
				{
					pelem->pTrayElem->eOwner = eOwner;
					EntSetTrayElemPowerData(pEntity,pelem,pchFallbackIcon,false,pExtract);
				}
				else
				{
					pelem->pchIcon = NULL;
					pelem->pSourceItem = NULL;
				}
			}
		}
	}
	eaSetSizeStruct(peaElems, parse_UITrayElem, j);
	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}


static int SavedPetPowersSort(PetRelationship *pEntData, const UIPowerElem **ppPower1, const UIPowerElem **ppPower2)
{
	int iPos1=0, iPos2 =0;

	if(ea32Size(&pEntData->eaPurposes))
	{
		iPos1 = ea32Find(&pEntData->eaPurposes,(*ppPower1)->ePurpose);
		iPos2 = ea32Find(&pEntData->eaPurposes,(*ppPower2)->ePurpose);
	}
	else
	{
		iPos1 = (*ppPower1)->ePurpose;
		iPos2 = (*ppPower2)->ePurpose;
	}

	if(iPos1 == iPos2)
	{
		iPos1 = (*ppPower1)->uiID;
		iPos2 = (*ppPower2)->uiID;
	}

	return iPos1 < iPos2 ? -1 : iPos1 > iPos2 ? 1 : 0;
}

static void EntGetPropSlotSavedPetPowers(SA_PARAM_NN_VALID UIGen *pGen,
										 SA_PARAM_OP_VALID Entity* pEntity,
										 U32 iSlotID,
										 S32 iStartSlot,
										 S32 iCount,
										 bool bShowKeybind,
										 bool bReferenceTrayElems)
{
	UIPowerElem ***peaPowerElems = ui_GenGetManagedListSafe(pGen, UIPowerElem);
	UIPowerElem *pPowerElem = NULL;
	Power **ppPowersTemp = NULL;
	Power** ppPowers = NULL;
	Entity *curEnt = NULL;
	AlwaysPropSlot* pPropSlot = NULL;
	PetRelationship *pEntData = NULL;
	int i, iSize = 0;

	if (!pEntity || !pEntity->pSaved || !pEntity->pSaved->ppAlwaysPropSlots)
	{
		eaDestroyStruct(peaPowerElems,parse_UIPowerElem);
		ui_GenSetManagedListSafe(pGen, peaPowerElems, UIPowerElem, true);
		return;
	}
	for(i=0;i<eaSize(&pEntity->pSaved->ppAlwaysPropSlots);i++)
	{
		if (iSlotID == pEntity->pSaved->ppAlwaysPropSlots[i]->iSlotID) break;
	}
	if (i >= eaSize(&pEntity->pSaved->ppAlwaysPropSlots)) return;
	pPropSlot = pEntity->pSaved->ppAlwaysPropSlots[i];
	if (pPropSlot->iPetID == 0)
	{
		for(i = 0; i < iCount; i++)
		{
			pPowerElem = eaGetStruct(peaPowerElems, parse_UIPowerElem, i);
			if ( pPowerElem->pchToolTip )
				estrClear(&pPowerElem->pchToolTip);
			else
				estrCreate(&pPowerElem->pchToolTip);
			pPowerElem->bValid = true;
			pPowerElem->ePurpose = 0;
			pPowerElem->uiID = 0;
			pPowerElem->pchIcon = NULL;
		}
		eaSetSizeStruct(peaPowerElems, parse_UIPowerElem, iCount);
		ui_GenSetManagedListSafe(pGen, peaPowerElems, UIPowerElem, true);
		return;
	}

	for(i=0;i<eaSize(&pEntity->pSaved->ppOwnedContainers);i++)
	{
		curEnt = GET_REF(pEntity->pSaved->ppOwnedContainers[i]->hPetRef);
		if (!curEnt) continue;
		if (curEnt->pSaved->iPetID == pPropSlot->iPetID)
		{
			pEntData = pEntity->pSaved->ppOwnedContainers[i];
			break;
		}
		curEnt = NULL;
	}

	if (curEnt)
	{
		S32 iNumPowers;
		ent_FindAllPropagatePowers(curEnt,
			pEntity->pSaved->ppOwnedContainers[i],
			NULL, NULL, &ppPowersTemp, true);
		iNumPowers = eaSize(&ppPowersTemp);

		for ( i = 0; i < iNumPowers; ++i )
		{
			Power *pPow = ppPowersTemp[i];
			PowerDef *pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;
			Character *pChar = pEntity->pChar;

			if ( !pPowDef || !pPowDef->ePurpose || pPowDef->bHideInUI )
				continue;

			pPowerElem = eaGetStruct(peaPowerElems, parse_UIPowerElem, iSize++);
			pPowerElem->bValid = true;
			pPowerElem->pchIcon = gclGetBestPowerIcon(pPowDef->pchIconName,pPowerElem->pchIcon);
			pPowerElem->ePurpose = pPowDef->ePurpose;
			pPowerElem->uiID = pPow->uiID;
			if ( pPowerElem->pchToolTip )
				estrClear(&pPowerElem->pchToolTip);
			else
				estrCreate(&pPowerElem->pchToolTip);

			if(pChar)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				power_AutoDesc(entGetPartitionIdx(pEntity),pPow,pChar,&pPowerElem->pchToolTip,NULL,"<br>","<bsp><bsp>","- ",false,0,entGetPowerAutoDescDetail(entActivePlayerPtr(),true),pExtract,NULL);
			}
		}
		eaDestroy(&ppPowersTemp);
		eaQSortCPP_s(*peaPowerElems, SavedPetPowersSort, iSize, pEntData);
		while( iSize < iCount )
		{
			pPowerElem = eaGetStruct(peaPowerElems, parse_UIPowerElem, iSize++);
			if ( pPowerElem->pchToolTip )
				estrClear(&pPowerElem->pchToolTip);
			else
				estrCreate(&pPowerElem->pchToolTip);
			pPowerElem->bValid = true;
			pPowerElem->ePurpose = 0;
			pPowerElem->uiID = 0;
			pPowerElem->pchIcon = NULL;
		}
	}

	eaSetSizeStruct(peaPowerElems, parse_UIPowerElem, iSize);
	ui_GenSetManagedListSafe(pGen, peaPowerElems, UIPowerElem, true);
}

static int SortPowerElemsByPowerDefPurpose(const UIPowerElem **ppLeft, const UIPowerElem **ppRight, const void *pContext)
{
	return (*ppLeft)->ePurpose - (*ppRight)->ePurpose;
}

// Currently, the only powers that are being filtered through are the power Tree powers.
static void EntGetSavedPetPowersByCategory(	SA_PARAM_NN_VALID UIGen *pGen,
											SA_PARAM_OP_VALID Entity* pEntity,
											ContainerID iContainerID,
											SA_PARAM_OP_STR const char *pchCategory,
											bool bSortByPurpose)
{
	UIPowerElem ***peaPowerElems = ui_GenGetManagedListSafe(pGen, UIPowerElem);
	UIPowerElem *pPowerElem = NULL;
	Power **ppPowersTemp = NULL;
	Power** ppPowers = NULL;
	Entity *curEnt = NULL;
	int i, j, iSize = 0;

	if (pEntity)
	{
		for(i=0;i<eaSize(&pEntity->pSaved->ppOwnedContainers);i++)
		{
			ContainerID id = pEntity->pSaved->ppOwnedContainers[i]->conID;
			if (id == iContainerID)
			{
				curEnt = GET_REF(pEntity->pSaved->ppOwnedContainers[i]->hPetRef);
				break;
			}
		}
	}

	if (curEnt && curEnt->pChar)
	{
		int iCategory = -1;

		character_FindAllPowersInPowerTrees(curEnt->pChar, &ppPowersTemp);

		if (pchCategory)
			iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);

		for ( i = 0; i < eaSize(&ppPowersTemp); ++i )
		{
			Power *pPow = ppPowersTemp[i];
			PowerDef *pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;
			Character *pChar = pEntity->pChar;

			if ( !pPowDef || !pPowDef->ePurpose || pPowDef->bHideInUI )
				continue;

			if(iCategory != -1)
			{
				if (eaiFind(&pPowDef->piCategories,iCategory) == -1)
					continue;
			}

			pPowerElem = NULL;
			for (j = iSize; j < eaSize(peaPowerElems); j++)
			{
				if ((*peaPowerElems)[j]->uiID == pPow->uiID)
				{
					if (j != iSize)
						eaMove(peaPowerElems, iSize, j);
					pPowerElem = (*peaPowerElems)[iSize];
					iSize++;
					break;
				}
			}
			if (!pPowerElem)
			{
				pPowerElem = StructCreate(parse_UIPowerElem);
				eaInsert(peaPowerElems, pPowerElem, iSize++);
			}

			pPowerElem->bValid = true;
			pPowerElem->pchIcon = gclGetBestPowerIcon(pPowDef->pchIconName,pPowerElem->pchIcon);
			pPowerElem->ePurpose = pPowDef->ePurpose;
			pPowerElem->uiID = pPow->uiID;
			if ( pPowerElem->pchToolTip )
				estrClear(&pPowerElem->pchToolTip);
			else
				estrCreate(&pPowerElem->pchToolTip);

			if(pChar)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				power_AutoDesc(entGetPartitionIdx(pEntity),pPow,pChar,&pPowerElem->pchToolTip,NULL,"<br>","<bsp><bsp>","- ",false,0,entGetPowerAutoDescDetail(entActivePlayerPtr(),true),pExtract,NULL);
			}
		}

		eaDestroy(&ppPowersTemp);
	}

	eaSetSizeStruct(peaPowerElems, parse_UIPowerElem, iSize);
	if (bSortByPurpose)
		eaStableSort(*peaPowerElems, NULL, SortPowerElemsByPowerDefPurpose);
	ui_GenSetListSafe(pGen, peaPowerElems, UIPowerElem);
}

static int SortPropSlotPowers(const Power **ppPowerA, const Power **ppPowerB)
{
	const Power* pA = (*ppPowerA);
	const Power* pB = (*ppPowerB);
	PowerDef* pDefA = GET_REF(pA->hDef);
	PowerDef* pDefB = GET_REF(pB->hDef);

	if ( pDefA && pDefB && pDefA->ePurpose != pDefB->ePurpose )
	{
		return pDefA->ePurpose - pDefB->ePurpose;
	}
	return pA->uiID - pB->uiID;
}

// Sets the gen's managed list to temporary TrayElems based on AlwaysPropSlots
static void EntGetPropSlotTrayElems(SA_PARAM_NN_VALID UIGen *pGen,
									SA_PARAM_OP_VALID Entity* pEntity,
									AlwaysPropSlotData* pPropSlot,
									S32 iStartSlot,
									S32 iCount,
									bool bShowKeybind,
									bool bReferenceTrayElems)
{
	S32 i, j=0, c=0;
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
	static Power** ppPowers = NULL;

	if ( pEntity && pPropSlot && pEntity->pChar && pPropSlot->uiPetID > 0 )
	{
		//get all the propagated powers that are from the pet corresponding to the prop slot
		for ( i = 0; i < eaSize(&pEntity->pChar->ppPowersPropagation); i++ )
		{
			U32 uiPowID = pEntity->pChar->ppPowersPropagation[i]->uiID;
			U32 uiSavedPetID = POWERID_GET_ENT(uiPowID);

			if ( uiSavedPetID == pPropSlot->uiPetID )
			{
				eaPush(&ppPowers,pEntity->pChar->ppPowersPropagation[i]);
			}
		}
	}
	if ( pPropSlot && pPropSlot->pPropItem )
	{
		// add powers from item
		eaPushEArray(&ppPowers, &pPropSlot->pPropItem->ppPowers);
	}

	eaQSort(ppPowers, SortPropSlotPowers);

	if(iStartSlot>=0 && iCount>0)
	{
		if ( eaSize(&ppPowers) > 0 )
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
			// Set up the tray information
			for(i = 0; i < eaSize(&ppPowers); i++)
			{
				UITrayElem *pelem = eaGetStruct(peaElems, parse_UITrayElem, j);
				int iKey = pelem->iKey;
				Power *pPow = eaGet(&ppPowers,i);
				PowerDef *pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;

				if ( !pPowDef || (!pPowDef->ePurpose && !pPropSlot->pPropItem) || !POWERTYPE_ACTIVATABLE(pPowDef->eType) || pPowDef->bHideInUI )
					continue;

				//if bReferenceTrayElems is set and the power is found in in the tray:
				//set the key to (tray,slot) of the first instance in the tray,
				//otherwise set the key to the power prop id
				if (	!pPow
					||	!bReferenceTrayElems
					||	(!TrayPowerMatchesTrayKey(pPow,pelem->iKey,0) && (iKey = TrayFindKeyByPower(pPow,0)) < 0) )
				{
					pelem->iKey = pPow ? pPow->uiID : 0;
					pelem->bReferencesTrayElem = false;
					pelem->pchDragType = "TrayPropSlot";
					pelem->iTray = (int)pPropSlot->uiPetID;
					pelem->iSlot = iStartSlot + j;
				}
				else
				{
					pelem->iKey = iKey;
					pelem->bReferencesTrayElem = true;
					pelem->pchDragType = "TrayElem";
					pelem->iTray = iKey >> 8;
					pelem->iSlot = iKey & 255;
				}
				j++;

				if(pPowDef)
				{
					UIInventoryKey Key = {0};
					S32 iItemPowIndex = pPropSlot && pPropSlot->pPropItem ? eaFind(&pPropSlot->pPropItem->ppPowers, pPow) : -1;

					if (iItemPowIndex >= 0)
						gclInventoryParseKey(pPropSlot->pchPropItemKey, &Key);

					if (pelem->pTrayElem && pelem->bOwnedTrayElem)
					{
						if (iItemPowIndex >= 0)
							tray_SetTrayElemForInventorySlot(pelem->pTrayElem,-1,-1,Key.eBag,Key.iSlot,iItemPowIndex,pPow->uiID);
						else
							tray_SetTrayElemForPowerPropSlot(pelem->pTrayElem,-1,-1,pPow->uiID,pPowDef->ePurpose,pPropSlot->iSlotID);
					}
					else
					{
						if (iItemPowIndex >= 0)
							tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForInventorySlot(-1,-1,Key.eBag,Key.iSlot,iItemPowIndex,pPow->uiID), true);
						else
							tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForPowerPropSlot(-1,-1,pPow->uiID,pPowDef->ePurpose,pPropSlot->iSlotID), true);
					}

					if (pelem->pTrayElem)
					{
						EntSetTrayElemPowerData(pEntity,pelem,NULL,false,pExtract);
					}
					else
					{
						pelem->pchIcon = gclGetBestPowerIcon(pPowDef->pchIconName,NULL);
						pelem->pSourceItem = NULL;
					}
				}
				else
				{
					tray_UITrayElemSetTrayElem(pelem, NULL, false);
					pelem->pchIcon = NULL;
					pelem->pSourceItem = NULL;
				}
			}
		}
		else
		{
			for(i = 0; i < iCount; i++)
			{
				UITrayElem *pelem = eaGetStruct(peaElems, parse_UITrayElem, j++);

				pelem->iTray = (int)0;
				pelem->iSlot = iStartSlot + i;
				pelem->iKey = 0;
				pelem->bReferencesTrayElem = false;
				pelem->pchDragType = "TrayPropSlot";
				pelem->pchIcon = NULL;
				pelem->pSourceItem = NULL;
				tray_UITrayElemSetTrayElem(pelem, NULL, false);
			}
		}
	}

	eaClearFast(&ppPowers);

	eaSetSizeStruct(peaElems, parse_UITrayElem, j);
	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

static S32 TrayInventorySlotToKey( S32 iBagID, S32 iBagSlot, S32 iItemPowIdx )
{
	devassert( iBagSlot < 1<<16 );
	devassert( iItemPowIdx < 1<<8 );
	devassert( iBagID < 1<<8 );
	return ((iBagSlot)<<16) | (iItemPowIdx<<8) | (iBagID);
}

// Sets the gen's managed list to temporary TrayElems based on a single item (N powers)
static int EntGetItemTrayElems(		SA_PARAM_OP_VALID UIGen *pGen,
									Entity *pEntity,
									int iInvBag,
									int iInvBagSlot,
									int iFirstPower,
									int iLastPower,
									bool bShowKeybind,
									bool bReferenceTrayElems,
									bool bGetBaseReplacePowers,
									GameAccountDataExtract *pExtract)
{
	UITrayElem ***peaElems = pGen ? ui_GenGetManagedListSafe(pGen, UITrayElem) : NULL;
	InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), iInvBag, pExtract);
	int j = 0;

	if(pEntity)
	{
		Item *pitem = inv_GetItemFromBag(pEntity,iInvBag,iInvBagSlot,pExtract);
		S32 i, s = ((pitem!=NULL) ? item_GetNumItemPowerDefs(pitem, true) : 0);

		for(i=0; i<s && (iLastPower < iFirstPower || iFirstPower + j <= iLastPower); i++)
		{
			UITrayElem *pelem = peaElems ? eaGetStruct(peaElems, parse_UITrayElem, j) : NULL;
			int iKey = pelem ? pelem->iKey : 0;
			bool bInPowersList = true;
			Power *ppowItem = item_GetPower(pitem,i);
			PowerDef *pdefItem = ppowItem ? GET_REF(ppowItem->hDef) : NULL;
			ItemPowerDef *pitemPowDef = item_GetItemPowerDef(pitem, i);
			if (pitemPowDef && !pdefItem)
				pdefItem = GET_REF(pitemPowDef->hPower);

			if(!pdefItem || !POWERTYPE_ACTIVATABLE(pdefItem->eType) || pdefItem->bHideInUI || pdefItem->bSlottingRequired || !pitemPowDef)
				continue;

			if (!ppowItem)
				continue;

			if ( bGetBaseReplacePowers && GET_REF(pitemPowDef->hPowerReplace) )
			{
				PowerReplace *pReplace = Entity_FindPowerReplace(pEntity, GET_REF(pitemPowDef->hPowerReplace));

				if ( pReplace )
				{
					ppowItem = character_FindPowerByIDComplete(pEntity->pChar, pReplace->uiBasePowerID);
				}

				if ( ppowItem==NULL )
					continue;

				if ( pelem && GET_REF(ppowItem->hDef) != pdefItem )
				{
					pelem->bUseBaseReplacePower = true;
				}
			}
			else
			{
				bInPowersList = character_FindPowerByID(pEntity->pChar, ppowItem->uiID) != NULL;
			}

			if (iFirstPower > 0)
			{
				iFirstPower--;
				continue;
			}

			j++;

			if (!pelem)
				continue;

			//if bReferenceTrayElems is set and the power is found in in the tray:
			//set the key to (tray,slot) of the first instance in the tray,
			//otherwise fill the key with bag id and slot
			if (	!ppowItem
				||	!bReferenceTrayElems
				||	((!pelem->bReferencesTrayElem || !TrayPowerMatchesTrayKey(ppowItem,pelem->iKey,pelem->bUseBaseReplacePower)) && (iKey = TrayFindKeyByPower(ppowItem,pelem->bUseBaseReplacePower)) < 0) )
			{
				if ( bGetBaseReplacePowers )
				{
					pelem->iKey = ppowItem ? ppowItem->uiID : 0;
					pelem->bReferencesTrayElem = false;
					pelem->pchDragType = "PowerID";
					pelem->iTray = ppowItem->uiID;
					pelem->iSlot = j - 1;
				}
				else
				{
					pelem->iKey = TrayInventorySlotToKey(iInvBag,iInvBagSlot,i);
					pelem->bReferencesTrayElem = false;
					pelem->pchDragType = "TrayItem";
					pelem->iTray = iInvBag;
					pelem->iSlot = iInvBagSlot + j - 1;
				}
			}
			else
			{
				pelem->iKey = iKey;
				pelem->bReferencesTrayElem = true;
				pelem->pchDragType = "TrayElem";
				pelem->iTray = iKey >> 8;
				pelem->iSlot = iKey & 255;
			}

			if(pitem)
			{
				if (pelem->pTrayElem && pelem->bOwnedTrayElem)
				{
					if ( bGetBaseReplacePowers )
						tray_SetTrayElemForPowerID(pelem->pTrayElem, -1, -1, ppowItem->uiID);
					else
						tray_SetTrayElemForInventorySlot(pelem->pTrayElem, -1,-1,iInvBag,iInvBagSlot,i,ppowItem?ppowItem->uiID:0);
				}
				else
				{
					if ( bGetBaseReplacePowers )
						tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForPowerID(-1,-1,ppowItem->uiID), true);
					else
						tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForInventorySlot(-1,-1,iInvBag,iInvBagSlot,i,ppowItem?ppowItem->uiID:0), true);
				}
				if(pelem->pTrayElem)
				{
					EntSetTrayElemPowerData(pEntity,pelem,NULL,!bInPowersList,pExtract);
					pelem->bValid = true;
				}
				else
				{
					pelem->pchIcon = allocAddString(item_GetIconName(pitem, NULL));
					pelem->pSourceItem = NULL;
				}
			}
			else if (pelem)
			{
				tray_UITrayElemSetTrayElem(pelem, NULL, false);
				pelem->pchIcon = NULL;
				pelem->pSourceItem = NULL;
			}
		}

		// Clear additional elements
		if (peaElems)
		{
			for (; iFirstPower <= iLastPower && iFirstPower + j <= iLastPower; j++)
			{
				tray_UITrayElemSetTrayElem((*peaElems)[j], NULL, false);
				(*peaElems)[j]->pchIcon = NULL;
				(*peaElems)[j]->pSourceItem = NULL;
			}

			eaSetSizeStruct(peaElems, parse_UITrayElem, j);
		}
	}
	else if (peaElems)
	{
		eaClearStruct(peaElems, parse_UITrayElem);
	}

	if (pGen)
		ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);

	return j;
}

// Sets the gen's managed list to temporary TrayElems based on inventory slots (1 Power per Item)
static void EntGetInvTrayElems(	SA_PARAM_NN_VALID UIGen *pGen,
								Entity *pEntity,
								int iInvBag,
								int iInvBagSlotMin,
								int iInvBagSlotMax,
								bool bReferenceTrayElems,
								GameAccountDataExtract *pExtract)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
	InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), iInvBag, pExtract);
	if (iInvBagSlotMax == -1 && pBag)
		iInvBagSlotMax = invbag_maxslots(pEntity, pBag) - 1;

	if(pEntity && iInvBagSlotMin>=0 && iInvBagSlotMax>=iInvBagSlotMin)
	{
		int i,s=1+iInvBagSlotMax-iInvBagSlotMin;

		// Make sure the array is the correct size.
		eaSetSizeStruct(peaElems, parse_UITrayElem, s);

		for(i=0; i<s; i++)
		{
			UITrayElem *pelem = eaGet(peaElems, i);
			int iKey = pelem->iKey;
			Item *pitem = inv_GetItemFromBag(pEntity,iInvBag,iInvBagSlotMin + i, pExtract);
			S32 iPowIdx, iPowers = ((pitem!=NULL) ? item_GetNumItemPowerDefs(pitem, true) : 0);
			Power *ppowItem = NULL;
			PowerDef *pdefItem = NULL;
			ItemPowerDef *pitemPowDef = NULL;
			ItemDef *pitemDef = pitem ? GET_REF(pitem->hItem) : NULL;
			ItemDefWarp *pitemDefWarp = pitemDef ? pitemDef->pWarp : NULL;

			for(iPowIdx=0; iPowIdx<iPowers; iPowIdx++)
			{
				ppowItem = item_GetPower(pitem,iPowIdx);
				pdefItem = ppowItem ? GET_REF(ppowItem->hDef) : NULL;
				pitemPowDef = item_GetItemPowerDef(pitem, iPowIdx);
				if (pitemPowDef && !pdefItem)
					pdefItem = GET_REF(pitemPowDef->hPower);
				if(ppowItem && pdefItem && !pdefItem->bSlottingRequired && pitemPowDef)
					break;
			}

			//if bReferenceTrayElems is set and the power is found in in the tray:
			//set the key to (tray,slot) of the first instance in the tray,
			//otherwise fill the key with bag id and slot
			if (	!ppowItem
				||	!bReferenceTrayElems
				||	((!pelem->bReferencesTrayElem || !TrayPowerMatchesTrayKey(ppowItem,pelem->iKey,0)) && (iKey = TrayFindKeyByPower(ppowItem,0)) < 0) )
			{
				pelem->iKey = TrayInventorySlotToKey(iInvBag,iInvBagSlotMin+i,iPowIdx);
				pelem->bReferencesTrayElem = false;
				pelem->pchDragType = "TrayItem";
				pelem->iTray = iInvBag;
				pelem->iSlot = iInvBagSlotMin + i;
			}
			else
			{
				pelem->iKey = iKey;
				pelem->bReferencesTrayElem = true;
				pelem->pchDragType = "TrayElem";
				pelem->iTray = iKey >> 8;
				pelem->iSlot = iKey & 255;
			}

			if(pitem)
			{
				if (pelem->pTrayElem && pelem->bOwnedTrayElem)
				{
					tray_SetTrayElemForInventorySlot(pelem->pTrayElem, -1,-1,iInvBag,iInvBagSlotMin + i,iPowIdx,ppowItem?ppowItem->uiID:0);
				}
				else
				{
					tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForInventorySlot(-1,-1,iInvBag,iInvBagSlotMin + i,iPowIdx,ppowItem?ppowItem->uiID:0), true);
				}
				if (pelem->pTrayElem)
				{
					if(ppowItem)
					{
						EntSetTrayElemPowerData(pEntity,pelem,NULL,false,pExtract);
					}
					else if(pitemDefWarp)
					{
						EntSetTrayElemWarpData(pEntity, pelem, pExtract);
					}
					pelem->bValid = true;
				}
				else
				{
					pelem->pchIcon = allocAddString(item_GetIconName(pitem, NULL));
					pelem->pSourceItem = NULL;
				}
			}
			else
			{
				tray_UITrayElemSetTrayElem(pelem, NULL, false);
				pelem->pchIcon = NULL;
				pelem->pSourceItem = NULL;
			}
		}
	}
	else
	{
		eaClearStruct(peaElems, parse_UITrayElem);
	}

	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

// Sets the gen's managed list to temporary TrayElems based on PowerSlots
static void EntGetPowerSlotsTrayElems(SA_PARAM_NN_VALID UIGen *pGen,
									  Entity *pEntity,
									  TrayElemOwner eOwner,
									  S32 iMinSlot,
									  S32 iMaxSlot,
									  bool bShowKeybinds,
									  const char* pchFallbackIcon)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);

	if(pEntity && pEntity->pChar && pEntity->pSaved)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		int i,s = character_PowerSlotCount(pEntity->pChar);

		s = min(s, (iMaxSlot - iMinSlot) + 1);

		// Make sure the array is the correct size.
		eaSetSizeStruct(peaElems, parse_UITrayElem, s);

		for (i = 0; i < s; i++)
		{
			// Reset every slot to empty...
			int slotIndex = i + iMinSlot;
			UITrayElem *pelem = (*peaElems)[i];
			pelem->iTray = pEntity->pSaved->uiIndexBuild;
			pelem->iSlot = slotIndex;
			pelem->pchDragType = "TrayElem";
			pelem->iKey = (pelem->iTray * 256) | slotIndex;
			pelem->bLocked = !character_PowerSlotIsUnlockedBySlotInTray(pEntity->pChar, pelem->iTray, pelem->iSlot);

			if (pelem->pTrayElem && pelem->bOwnedTrayElem)
			{
				tray_SetTrayElemForPowerSlot(pelem->pTrayElem, -1, -1, slotIndex);
			}
			else
			{
				tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForPowerSlot(-1, -1, slotIndex), true);
			}
			if(pelem->pTrayElem)
			{
				pelem->pTrayElem->eOwner = eOwner;
				EntSetTrayElemPowerData(pEntity,pelem,pchFallbackIcon,false,pExtract);
			}
			else
			{
				pelem->pchIcon = NULL;
				pelem->pSourceItem = NULL;
			}
		}
	}
	else
	{
		eaClearStruct(peaElems, parse_UITrayElem);
	}

	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

// Sets the gen's managed list to temporary TrayElems based on PowerSlots
static void EntGetPowerSlotsTrayElemsByIndex(SA_PARAM_NN_VALID UIGen *pGen,
											 Entity *pEntity,
											 TrayElemOwner eOwner,
											 U32 uiIndex,
											 SA_PARAM_OP_STR const char *pchDragType,
											 const char* pchFallbackIcon)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);

	if(pEntity && pEntity->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		static U32 *puiIDs = NULL;
		int i,s;
		character_PowerIDsSlottedByIndex(pEntity->pChar, &puiIDs, uiIndex);
		if(puiIDs)
		{
			s=ea32Size(&puiIDs);

			// Make sure the array is the correct size.
			eaSetSizeStruct(peaElems, parse_UITrayElem, s);

			for (i = 0; i < s; i++)
			{
				// Reset every slot to empty...
				UITrayElem *pelem = (*peaElems)[i];
				pelem->iTray = uiIndex;
				pelem->iSlot = i;
				pelem->pchDragType = ( (pchDragType) ? (pchDragType) : ("TrayElem") );
				pelem->iKey = (uiIndex * 256) | i;
				pelem->bLocked = !character_PowerSlotIsUnlockedBySlotInTray(pEntity->pChar, pelem->iTray, pelem->iSlot);

				if (pelem->pTrayElem && pelem->bOwnedTrayElem)
				{
					tray_SetTrayElemForPowerSlot(pelem->pTrayElem, uiIndex,-1,(U32)i);
				}
				else
				{
					tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForPowerSlot(uiIndex,-1,(U32)i), true);
				}
				if(pelem->pTrayElem)
				{
					pelem->pTrayElem->eOwner = eOwner;
					EntSetTrayElemPowerData(pEntity,pelem,pchFallbackIcon,false,pExtract);
				}
				else
				{
					pelem->pchIcon = NULL;
					pelem->pSourceItem = NULL;
				}
			}
		}
		else
		{
			eaClearStruct(peaElems, parse_UITrayElem);
		}
	}
	else
	{
		eaClearStruct(peaElems, parse_UITrayElem);
	}

	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

// Sets the gen's managed list to temporary TrayElems based on PowerSlots
static void EntGetPowerSlotsTrayElemsInRange(SA_PARAM_NN_VALID UIGen *pGen,
											 Entity *pEntity,
											 TrayElemOwner eOwner,
											 U32 uiIndex,
											 S32 iMinSlot,
											 S32 iMaxSlot,
											 SA_PARAM_OP_STR const char *pchDragType,
											 const char* pchFallbackIcon)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);

	if(pEntity && pEntity->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		static U32 *puiIDs = NULL;
		int i,s;
		character_PowerIDsSlottedByIndex(pEntity->pChar, &puiIDs, uiIndex);
		if(puiIDs)
		{
			s=ea32Size(&puiIDs);

			s = min(s, (iMaxSlot - iMinSlot) + 1);

			// Make sure the array is the correct size.
			eaSetSizeStruct(peaElems, parse_UITrayElem, s);

			for (i = 0; i < s; i++)
			{
				// Reset every slot to empty...
				UITrayElem *pelem = (*peaElems)[i];

				int slotIndex = i + iMinSlot;
				pelem->iTray = uiIndex;
				pelem->iSlot = slotIndex;
				pelem->pchDragType = ( (pchDragType) ? (pchDragType) : ("TrayElem") );
				pelem->iKey = (uiIndex * 256) | slotIndex;
				pelem->bLocked = !character_PowerSlotIsUnlockedBySlotInTray(pEntity->pChar, pelem->iTray, pelem->iSlot);

				if (pelem->pTrayElem && pelem->bOwnedTrayElem)
				{
					tray_SetTrayElemForPowerSlot(pelem->pTrayElem, uiIndex,-1,(U32)slotIndex);
				}
				else
				{
					tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForPowerSlot(uiIndex,-1,(U32)slotIndex), true);
				}
				if(pelem->pTrayElem)
				{
					pelem->pTrayElem->eOwner = eOwner;
					EntSetTrayElemPowerData(pEntity,pelem,pchFallbackIcon,false,pExtract);
				}
				else
				{
					pelem->pchIcon = NULL;
					pelem->pSourceItem = NULL;
				}
			}
		}
		else
		{
			eaClearStruct(peaElems, parse_UITrayElem);
		}
	}
	else
	{
		eaClearStruct(peaElems, parse_UITrayElem);
	}

	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

// Sets the gen's managed list to a temporary TrayElem based on a PowerSlot
static void EntGetPowerSlotTrayElems(SA_PARAM_NN_VALID UIGen *pGen,
									 Entity *pEntity,
									 TrayElemOwner eOwner,
									 U32 uiSlot,
									 const char* pchFallbackIcon)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);

	if(pEntity && pEntity->pChar && pEntity->pSaved)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		// Make sure the array is the correct size.
		eaSetSizeStruct(peaElems, parse_UITrayElem, 1);

		{

			// Reset every slot to empty...
			UITrayElem *pelem = (*peaElems)[0];
			pelem->iTray = pEntity->pSaved->uiIndexBuild;
			pelem->iSlot = uiSlot;
			pelem->pchDragType = "TrayElem";
			pelem->iKey = uiSlot;
			pelem->bLocked = !character_PowerSlotIsUnlockedBySlotInTray(pEntity->pChar, pelem->iTray, pelem->iSlot);

			if (pelem->pTrayElem && pelem->bOwnedTrayElem)
			{
				tray_SetTrayElemForPowerSlot(pelem->pTrayElem, -1, -1, (U32)uiSlot);
			}
			else
			{
				tray_UITrayElemSetTrayElem(pelem, tray_CreateTrayElemForPowerSlot(-1,-1,(U32)uiSlot), true);
			}
			if(pelem->pTrayElem)
			{
				pelem->pTrayElem->eOwner = eOwner;
				EntSetTrayElemPowerData(pEntity,pelem,pchFallbackIcon,false,pExtract);
			}
			else
			{
				pelem->pchIcon = NULL;
				pelem->pSourceItem = NULL;
			}
		}
	}
	else
	{
		eaClearStruct(peaElems, parse_UITrayElem);
	}

	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayIndex");
S32 exprEntGetTrayIndex(SA_PARAM_OP_VALID Entity *pEntity,
						int iUITray)
{
	return entity_TrayGetUITrayIndex(pEntity, iUITray);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElems");
void exprEntGetTrayElems(SA_PARAM_NN_VALID UIGen *pGen,
						 SA_PARAM_OP_VALID Entity *pEntity,
						 int iUITray,
						 int iSlotMin,
						 int iSlotMax,
						 const char* pchFallbackIcon)
{
	int iTray = entity_TrayGetUITrayIndex(pEntity, iUITray);
	EntGetTrayElems(pGen,pEntity,iTray,iSlotMin,iSlotMax,true,iUITray,pchFallbackIcon);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntHasPowerQueuedForTime");
bool exprEntHasPowerQueuedForTime(SA_PARAM_OP_VALID Entity *pEntity, F32 fTimeTolerance)
{
	return (pEntity && pEntity->pChar && pEntity->pChar->pPowActQueued &&
		(pmTimestampFrom(pEntity->pChar->pPowActQueued->uiTimestampQueued, fTimeTolerance) <= pEntity->pChar->pPowActQueued->uiTimestampActivate));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetMultiExecPowerTrayElem");
void exprEntGetQueuedPowerTrayElem(SA_PARAM_NN_VALID UIGen *pGen)
{
	UITrayElem ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayElem);
	Entity* pEnt = entActivePlayerPtr();
	Power* ppow = clientTarget_GetMultiExecPower(0);
	// Make sure we have a place for this in the array
	if(eaSize(peaElems) < 1)
	{
		eaPush(peaElems, StructAlloc(parse_UITrayElem));
	}
	if(pEnt && ppow)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		UITrayElem* pelem = (*peaElems)[0];
		pelem->iKey = (S32)ppow->uiID;
		pelem->bReferencesTrayElem = false;
		pelem->pchDragType = "PowerID";
		pelem->iTray = (S32)ppow->uiID;
		pelem->iSlot = 0;

		tray_UITrayElemSetTrayElem((*peaElems)[0], tray_CreateTrayElemForPowerID(-1,-1,ppow->uiID), true);
		EntSetTrayElemPowerData(pEnt, pelem, NULL, false, pExtract);
	}
	else
	{
		tray_UITrayElemSetTrayElem((*peaElems)[0], NULL, false);
		(*peaElems)[0]->pchIcon = NULL;
		(*peaElems)[0]->pSourceItem = NULL;
	}
	// Make sure the array isn't too big
	eaSetSizeStruct(peaElems, parse_UITrayElem, 1);
	ui_GenSetManagedListSafe(pGen, peaElems, UITrayElem, true);
}

AUTO_STRUCT;
typedef struct UITrayData
{
	S32 iTray;
} UITrayData;

extern ParseTable parse_UITrayData[];
#define TYPE_parse_UITrayData UITrayData

//gets a list of trays.
//NOTE: iUITrayStart can be greater than iUITrayEnd, in which case the list will be backwards
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayList");
void exprEntGetTrayList( SA_PARAM_NN_VALID UIGen *pGen,
						 SA_PARAM_OP_VALID Entity *pEntity,
						 int iUITrayStart,
						 int iUITrayEnd)
{
	UITrayData ***peaElems = ui_GenGetManagedListSafe(pGen, UITrayData);
	int i, iSize = ABS(iUITrayEnd-iUITrayStart)+1;
	int iCount = 0;
	SavedTray* pTray = entity_GetActiveTray(pEntity);

	if (pTray)
	{
		for (i = 0; i < iSize; i++)
		{
			int iUITray = iUITrayStart <= iUITrayEnd ? iUITrayStart+i : iUITrayStart-i;

			if (iUITray >= 0)
			{
				UITrayData* pData = eaGetStruct(peaElems, parse_UITrayData, iCount++);
				pData->iTray = entity_TrayGetUITrayIndex(pEntity, iUITray);
			}
		}
	}

	eaSetSizeStruct(peaElems, parse_UITrayData, iCount);
	ui_GenSetManagedListSafe(pGen, peaElems, UITrayData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsByTray");
void exprEntGetTrayElemsByTray(SA_PARAM_NN_VALID UIGen *pGen,
							   SA_PARAM_OP_VALID Entity *pEntity,
							   int iTray,
							   int iSlotMin,
							   int iSlotMax,
							   const char* pchFallbackIcon)
{
	EntGetTrayElems(pGen,pEntity,iTray,iSlotMin,iSlotMax,false,iTray,pchFallbackIcon);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromInventory");
void exprEntGetTrayElemsFromInventory(SA_PARAM_NN_VALID UIGen *pGen,
									  SA_PARAM_OP_VALID Entity *pEntity,
									  const char *pchInvBag,
									  int iInvBagSlotMin,
									  int iInvBagSlotMax)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	int iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchInvBag);
	EntGetInvTrayElems(pGen,pEntity,iInvBag,iInvBagSlotMin,iInvBagSlotMax,false,pExtract);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromItem");
void exprEntGetTrayElemsFromItem(	SA_PARAM_NN_VALID UIGen *pGen,
	SA_PARAM_OP_VALID Entity *pEntity,
	const char *pchInvBag,
	int iInvBagSlot,
	bool bShowKeyBinds)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	int iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchInvBag);
	EntGetItemTrayElems(pGen,pEntity,iInvBag,iInvBagSlot,0,-1,bShowKeyBinds,true,false,pExtract);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromItemRange");
void exprEntGetTrayElemsFromItemRange(	SA_PARAM_NN_VALID UIGen *pGen,
										SA_PARAM_OP_VALID Entity *pEntity,
										const char *pchInvBag,
										int iInvBagSlot,
										int iFirstPower,
										int iLastPower,
										bool bShowKeyBinds)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	int iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchInvBag);
	EntGetItemTrayElems(pGen,pEntity,iInvBag,iInvBagSlot,iFirstPower,iLastPower,bShowKeyBinds,true,false,pExtract);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntCountTrayElemsFromItem");
S32 exprEntCountTrayElemsFromItem(	SA_PARAM_OP_VALID Entity *pEntity,
									const char *pchInvBag,
									int iInvBagSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	int iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchInvBag);
	return EntGetItemTrayElems(NULL,pEntity,iInvBag,iInvBagSlot,0,-1,false,true,false,pExtract);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromItemBaseReplacePowers");
void exprEntGetTrayElemsFromItemBaseReplacePowers(	SA_PARAM_NN_VALID UIGen *pGen,
													SA_PARAM_OP_VALID Entity *pEntity,
													const char *pchInvBag,
													int iInvBagSlot,
													bool bShowKeyBinds)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	int iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchInvBag);
	EntGetItemTrayElems(pGen,pEntity,iInvBag,iInvBagSlot,0,-1,bShowKeyBinds,true,true,pExtract);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromInvRefTrayElems");
void exprEntGetTrayElemsFromInvRefTrayElems(SA_PARAM_NN_VALID UIGen *pGen,
											SA_PARAM_OP_VALID Entity *pEntity,
											const char *pchInvBag,
											int iInvBagSlotMin,
											int iInvBagSlotMax)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	int iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchInvBag);
	EntGetInvTrayElems(pGen,pEntity,iInvBag,iInvBagSlotMin,iInvBagSlotMax,true,pExtract);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPropSlots");
void exprEntGetTrayElemsFromPropSlots(	SA_PARAM_NN_VALID UIGen *pGen,
										SA_PARAM_OP_VALID Entity* pEntity,
										SA_PARAM_OP_VALID AlwaysPropSlotData *pPropSlot,
										S32 iStartSlot,
										S32 iCount )
{
	EntGetPropSlotTrayElems(pGen, pEntity, pPropSlot, iStartSlot, iCount, true, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetAllTrayElemsFromPropSlots");
void exprEntGetAllTrayElemsFromPropSlots(SA_PARAM_NN_VALID UIGen *pGen,
										SA_PARAM_OP_VALID Entity* pEntity,
										SA_PARAM_OP_VALID AlwaysPropSlotData *pPropSlot,
										bool bEmpty )
{
	S32 iCount = 0;

	if (pPropSlot && GET_REF(pPropSlot->hPropItemDef))
	{
		ItemDef *pDef = GET_REF(pPropSlot->hPropItemDef);
		iCount = eaSize(&pDef->ppItemPowerDefRefs);
	}
	else if (pPropSlot && GET_REF(pPropSlot->hDef))
	{
		AlwaysPropSlotDef *pDef = GET_REF(pPropSlot->hDef);
		iCount = pDef->iMaxPropPowers;

		// Clip count to actual number of pet powers
		if (!bEmpty && pPropSlot->uiPetID > 0 && pEntity && pEntity->pChar)
		{
			S32 i, iMax = 0;
			//get all the propagated powers that are from the pet corresponding to the prop slot
			for ( i = 0; i < eaSize(&pEntity->pChar->ppPowersPropagation); i++ )
			{
				U32 uiPowID = pEntity->pChar->ppPowersPropagation[i]->uiID;
				if (POWERID_GET_ENT(uiPowID) == pPropSlot->uiPetID)
					iMax++;
			}
			MIN1(iCount, iMax);
		}
	}

	EntGetPropSlotTrayElems(pGen, pEntity, pPropSlot, 0, iCount, true, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSavedPetPowersFromPropSlots");
void exprEntGetSavedPetPowersFromPropSlots(	SA_PARAM_NN_VALID UIGen *pGen,
									  SA_PARAM_OP_VALID Entity* pEntity,
									  int iSlotID,
									  S32 iStartSlot,
									  S32 iCount )
{
	EntGetPropSlotSavedPetPowers(pGen, pEntity, iSlotID, iStartSlot, iCount, true, true);
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSavedPetPowersByCategory");
void exprEntGetSavedPetPowersByCategory( SA_PARAM_NN_VALID UIGen *pGen,
										 SA_PARAM_OP_VALID Entity* pEntity,
										 int iContainerID,
										 SA_PARAM_NN_STR const char *pchCategory)
{
	EntGetSavedPetPowersByCategory(pGen, pEntity, iContainerID, pchCategory, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSavedPetPowersByCategorySortByPurpose");
void exprEntGetSavedPetPowersByCategorySortByPurpose(SA_PARAM_NN_VALID UIGen *pGen,
													 SA_PARAM_OP_VALID Entity* pEntity,
													 int iContainerID,
													 SA_PARAM_NN_STR const char *pchCategory)
{
	EntGetSavedPetPowersByCategory(pGen, pEntity, iContainerID, pchCategory, true);
}

//const TeamInfoFromServer *team_RequestTeamInfoFromServer(const char *pPowersCategory);

void EntGetSavedPetPowersFromOtherPlayerByCategory(SA_PARAM_NN_VALID UIGen *pGen,
												   SA_PARAM_OP_VALID Entity* pEntity,
												   ContainerID iContainerID,
												   SA_PARAM_NN_STR const char *pchCategory,
												   bool bSortByPurpose)
{
	UIPowerElem ***peaPowerElems = ui_GenGetManagedListSafe(pGen, UIPowerElem);
	int i, j, iSize = 0;

	if ( pEntity )
	{
		const TeamInfoFromServer *teamInfo = team_RequestTeamInfoFromServer(pchCategory);

		if (teamInfo)
		{
			for (i = eaSize(&teamInfo->eaPlayerList)-1; i >= 0; --i)
			{
				if ((ContainerID)teamInfo->eaPlayerList[i]->containerID == entGetContainerID(pEntity)) break;
			}
			if (i >= 0)
			{
				TeamInfoPlayer *tip = teamInfo->eaPlayerList[i];
				for (i = eaSize(&tip->eaPetList)-1; i >= 0; --i)
				{
					if ((ContainerID)tip->eaPetList[i]->containerID == iContainerID) break;
				}
				if (i >= 0)
				{
					TeamInfoPet *tip2 = tip->eaPetList[i];
					for (i = 0; i < eaSize(&tip2->eaPowerElem); ++i)
					{
						const char* pchTooltip;
						int iCategory = -1;
						UIPowerElem *pPowerElem;
						PowerDef *pPowDef = GET_REF(tip2->eaPowerElem[i]->hRef);

						if (pchCategory)
							iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);

						if(iCategory != -1)
						{
							if (!pPowDef || eaiFind(&pPowDef->piCategories,iCategory) == -1)
								continue;
						}

						pPowerElem = NULL;
						for (j = iSize; j < eaSize(peaPowerElems); j++)
						{
							if ((*peaPowerElems)[j]->uiID == tip2->eaPowerElem[i]->uiID)
							{
								if (j != iSize)
									eaMove(peaPowerElems, iSize, j);
								pPowerElem = (*peaPowerElems)[iSize];
								iSize++;
								break;
							}
						}
						if (!pPowerElem)
						{
							pPowerElem = StructCreate(parse_UIPowerElem);
							eaInsert(peaPowerElems, pPowerElem, iSize++);
						}

						pPowerElem->bValid = true;
						pPowerElem->pchIcon = gclGetBestPowerIcon(tip2->eaPowerElem[i]->pchIcon,pPowerElem->pchIcon);
						pPowerElem->ePurpose = (PowerPurpose)tip2->eaPowerElem[i]->iePurpose;
						pPowerElem->uiID = tip2->eaPowerElem[i]->uiID;
						pchTooltip = tip2->eaPowerElem[i]->pchToolTip;
						if ( pPowerElem->pchToolTip )
							estrClear(&pPowerElem->pchToolTip);
						else
							estrCreate(&pPowerElem->pchToolTip);
						estrConcatString(&pPowerElem->pchToolTip,pchTooltip,(U32)strlen(pchTooltip));
					}
				}
			}
		}
	}

	eaSetSizeStruct(peaPowerElems, parse_UIPowerElem, iSize);
	if (bSortByPurpose)
		eaStableSort(*peaPowerElems, NULL, SortPowerElemsByPowerDefPurpose);
	ui_GenSetManagedListSafe(pGen, peaPowerElems, UIPowerElem, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSavedPetPowersFromOtherPlayerByCategory");
void exprEntGetSavedPetPowersFromOtherPlayerByCategory(SA_PARAM_NN_VALID UIGen *pGen,
													   SA_PARAM_OP_VALID Entity* pEntity,
													   int iContainerID,
													   SA_PARAM_NN_STR const char *pchCategory)
{
	EntGetSavedPetPowersFromOtherPlayerByCategory(pGen, pEntity, iContainerID, pchCategory, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSavedPetPowersFromOtherPlayerByCategorySortByPurpose");
void exprEntGetSavedPetPowersFromOtherPlayerByCategorySortByPurpose(SA_PARAM_NN_VALID UIGen *pGen,
																	SA_PARAM_OP_VALID Entity* pEntity,
																	int iContainerID,
																	SA_PARAM_NN_STR const char *pchCategory)
{
	EntGetSavedPetPowersFromOtherPlayerByCategory(pGen, pEntity, iContainerID, pchCategory, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPowersByCategory");
void exprEntGetTrayElemsFromPowersByCategory(SA_PARAM_NN_VALID UIGen *pGen,
											 SA_PARAM_OP_VALID Entity *pEntity,
											 SA_PARAM_OP_STR const char* pchCategory,
											 const char* pchFallbackIcon,
											 bool bShowKeyBinds)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowersTrayElems(pGen,pEntity,kTrayElemOwner_Self,pchCategory,pchFallbackIcon,bShowKeyBinds,bShowKeyBinds);
	}
	else
	{
		// For now, assume if it's not you, it's your pet
		EntGetPowersTrayElems(pGen,pEntity,kTrayElemOwner_PrimaryPet,pchCategory,pchFallbackIcon,bShowKeyBinds,bShowKeyBinds);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetNumPowersReadyWithCat");
S32 exprEntGetNumPowersReadyWithCat(SA_PARAM_OP_VALID Entity *pEntity,
	const char* pchCategory)
{
	Entity *e = entActivePlayerPtr();
	Character* pchar = pEntity ? pEntity->pChar : NULL;
	int iNumReady = 0;
	if(pchar)
	{
		int i,s=eaSize(&pchar->ppPowers);
		int iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);
		for(i=0; i<s; i++)
		{
			Power *ppow = pchar->ppPowers[i];
			PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

			if(pdef && POWERTYPE_ACTIVATABLE(pdef->eType))
			{

				if ( pchCategory && pchCategory[0] && (iCategory < 0 || eaiFind(&pdef->piCategories,iCategory) < 0) )
					continue;

				iNumReady += ((pchar->pPowActCurrent && pchar->pPowActCurrent->ref.uiID == ppow->uiID) ||
							((pchar->pPowActQueued && pchar->pPowActQueued->ref.uiID == ppow->uiID) || clientTarget_IsMultiPowerExec(ppow->uiID))) ||
							power_GetRecharge(ppow) <= FLT_EPSILON;
			}
		}
	}
	return iNumReady;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPowers");
void exprEntGetTrayElemsFromPowers(SA_PARAM_NN_VALID UIGen *pGen,
								   SA_PARAM_OP_VALID Entity *pEntity,
								   const char* pchFallbackIcon)
{
	//default keybinds to off since Champs uses this
	exprEntGetTrayElemsFromPowersByCategory( pGen, pEntity, NULL, pchFallbackIcon, false );
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPowerSlots");
void exprEntGetTrayElemsFromPowerSlots(SA_PARAM_NN_VALID UIGen *pGen,
									   SA_PARAM_OP_VALID Entity *pEntity,
									   const char* pchFallbackIcon)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowerSlotsTrayElems(pGen,pEntity,kTrayElemOwner_Self, 0, 1000, 1, pchFallbackIcon);
	}
	else
	{
		// For now, assume if it's not you, it's your pet
		EntGetPowerSlotsTrayElems(pGen,pEntity,kTrayElemOwner_PrimaryPet, 0, 1000, 1, pchFallbackIcon);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPowerSlotsRange");
void exprEntGetTrayElemsFromPowerSlotsRange(SA_PARAM_NN_VALID UIGen *pGen,
									   SA_PARAM_OP_VALID Entity *pEntity,
									   S32 iMinSlot,
									   S32 iMaxSlot,
									   const char* pchFallbackIcon)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowerSlotsTrayElems(pGen,pEntity,kTrayElemOwner_Self, iMinSlot, iMaxSlot, 1, pchFallbackIcon);
	}
	else
	{
		// For now, assume if it's not you, it's your pet
		EntGetPowerSlotsTrayElems(pGen,pEntity,kTrayElemOwner_PrimaryPet, iMinSlot, iMaxSlot, 1, pchFallbackIcon);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPowerSlotsRangeNoKeyBinds");
void exprEntGetTrayElemsFromPowerSlotsRangeNoKeyBinds(SA_PARAM_NN_VALID UIGen *pGen,
											SA_PARAM_OP_VALID Entity *pEntity,
											S32 iMinSlot,
											S32 iMaxSlot,
											const char* pchFallbackIcon)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowerSlotsTrayElems(pGen,pEntity,kTrayElemOwner_Self, iMinSlot, iMaxSlot, 0, pchFallbackIcon);
	}
	else
	{
		// For now, assume if it's not you, it's your pet
		EntGetPowerSlotsTrayElems(pGen,pEntity,kTrayElemOwner_PrimaryPet, iMinSlot, iMaxSlot, 0, pchFallbackIcon);
	}
}



AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPowerSlotsByIndex");
void exprEntGetTrayElemsFromPowerSlotsByIndex(SA_PARAM_NN_VALID UIGen *pGen,
									   SA_PARAM_OP_VALID Entity *pEntity,
									   U32 uiIndex,
									   const char* pchFallbackIcon)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowerSlotsTrayElemsByIndex(pGen,pEntity,kTrayElemOwner_Self, uiIndex, NULL, pchFallbackIcon);
	}
	else
	{
		EntGetPowerSlotsTrayElemsByIndex(pGen,NULL,kTrayElemOwner_Self, uiIndex, NULL, pchFallbackIcon);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuildElemsFromPowerSlotsByIndex");
void exprEntGetBuildElemsFromPowerSlotsByIndex(SA_PARAM_NN_VALID UIGen *pGen,
											   SA_PARAM_OP_VALID Entity *pEntity,
											   U32 uiIndex,
											   const char* pchFallbackIcon)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowerSlotsTrayElemsByIndex(pGen,pEntity,kTrayElemOwner_Self, uiIndex, "BuildElem", pchFallbackIcon);
	}
	else
	{
		EntGetPowerSlotsTrayElemsByIndex(pGen,NULL,kTrayElemOwner_Self, uiIndex, "BuildElem", pchFallbackIcon);
	}
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetBuildElemsFromPowerSlotsInRange");
void exprEntGetBuildElemsFromPowerSlotsInRange(SA_PARAM_NN_VALID UIGen *pGen,
											   SA_PARAM_OP_VALID Entity *pEntity,
											   U32 uiIndex,
											   S32 iMinSlot,
											   S32 iMaxSlot,
											   const char* pchFallbackIcon)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowerSlotsTrayElemsInRange(pGen,pEntity,kTrayElemOwner_Self, uiIndex, iMinSlot, iMaxSlot, "BuildElem", pchFallbackIcon);
	}
	else
	{
		EntGetPowerSlotsTrayElemsInRange(pGen,NULL,kTrayElemOwner_Self, uiIndex, iMinSlot, iMaxSlot, "BuildElem", pchFallbackIcon);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTrayElemsFromPowerSlot");
void exprEntGetTrayElemsFromPowerSlot(SA_PARAM_NN_VALID UIGen *pGen,
									  SA_PARAM_OP_VALID Entity *pEntity,
									  int iSlot,
									  const char* pchFallbackIcon)
{
	Entity *e = entActivePlayerPtr();
	if(e==pEntity)
	{
		EntGetPowerSlotTrayElems(pGen,pEntity,kTrayElemOwner_Self,(U32)iSlot, pchFallbackIcon);
	}
	else
	{
		// For now, assume if it's not you, it's your pet
		EntGetPowerSlotTrayElems(pGen,pEntity,kTrayElemOwner_PrimaryPet,(U32)iSlot, pchFallbackIcon);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemDragKeyString");
SA_RET_OP_STR const char *exprTrayElemDragKeyString(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	if(pElem)
	{
		if (pElem->pTrayElem)
		{
			if (pElem->pTrayElem->eOwner==kTrayElemOwner_PrimaryPet)
			{
				// For now, simply identify elements dragged from your pet with the "Pet" string key
				return "Pet";
			}
			else if (pElem->pTrayElem->eType == kTrayElemType_SavedPetPower)
			{
				return pElem->pTrayElem->pchIdentifier;
			}
		}
		else if (pElem->pPetData)
		{
			return pElem->pPetData->pchPower;
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayGetMacroDragKeyString");
SA_RET_OP_STR const char *exprTrayGetMacroDragKeyString(const char* pchMacro,
													   const char* pchDescription,
													   const char* pchIcon)
{
	static char* s_estrMacroKeyString = NULL;
	estrClear(&s_estrMacroKeyString);
	if (pchMacro && pchMacro[0])
	{
		PlayerMacro Data = {0};
		Data.pchMacro = (char*)pchMacro;
		Data.pchDescription = (char*)pchDescription;
		Data.pchIcon = pchIcon;
		tray_MacroDataToDragString(&s_estrMacroKeyString, &Data);
	}
	return s_estrMacroKeyString;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayCopyElem");
void exprEntTrayCopyElem(SA_PARAM_OP_VALID Entity *pEntity,
						 SA_PARAM_OP_VALID UITrayElem *pElem,
						 int iTray,
						 int iSlot,
						 int bOverwrite)
{
	int iID = -1;
	if(pElem)
	{
		if (pElem->pTrayElem)
		{
			iID = pElem->pTrayElem->lIdentifier;
		}
		if (iID >= 0)
		{
			if (bOverwrite)
			{
				ServerCmd_TrayElemDestroy(iTray, iSlot);
			}
			ServerCmd_TrayElemCreatePower(iTray,iSlot,iID,false);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemRequires");
bool exprEntTrayElemRequires(SA_PARAM_OP_VALID Entity *pEntity,
							 SA_PARAM_OP_VALID UITrayElem *pElem,
							 const char* pchRequires)
{
	if (pchRequires && pEntity && pEntity->pChar)
	{
		S32 iReq = StaticDefineIntGetInt(PowerCategoriesEnum, pchRequires);
		PowerSlot *pSlot = character_PowerSlotGetPowerSlot(pEntity->pChar, pElem->iSlot);
		if (pSlot && iReq > 0)
		{
			return eaiFind(&pSlot->peRequires, iReq) >= 0;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemRequiresByIndex");
bool exprEntTrayElemRequiresByIndex(SA_PARAM_OP_VALID Entity *pEntity,
							 SA_PARAM_OP_VALID UITrayElem *pElem,
							 S32 iTray,
							 const char* pchRequires)
{
	if (pchRequires && pEntity && pEntity->pChar)
	{
		S32 iReq = StaticDefineIntGetInt(PowerCategoriesEnum, pchRequires);
		PowerSlot *pSlot = CharacterGetPowerSlotInTrayAtIndex(pEntity->pChar, iTray, pElem->iSlot);
		if (pSlot && iReq > 0)
		{
			return eaiFind(&pSlot->peRequires, iReq) >= 0;
		}
	}
	return false;
}

//Get the power ID for a particular tray elem
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerIDForTrayElemDropKey");
U32 exprEntGetPowerIDForTrayElemDropKey(SA_PARAM_OP_VALID Entity *pEntity, int iKeyDrop)
{
	int iTray, iSlot;
	iTray = iKeyDrop >> 8;
	iSlot = iKeyDrop & 255;

	if (pEntity) {
		return character_PowerSlotGetFromTray(pEntity->pChar, iTray, iSlot);
	}

	return 0;
}



//Whether this tray elem can catch the dragged element...
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCanCatch");
S32 exprEntTrayElemCanCatch(SA_PARAM_OP_VALID Entity *pEntity,
							SA_PARAM_OP_VALID UITrayElem *pElem,
							SA_PARAM_OP_STR const char *pchDragType,
							int iID)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		int iTrayOld, iSlotOld;
		bool bValidDrag = false;
		U32 iPowerID = 0;

		if(stricmp(pchDragType, "TrayElem") == 0 ||
			stricmp(pchDragType, "BuildElem") == 0)
		{
			iTrayOld = iID >> 8;
			iSlotOld = iID & 255;
			iPowerID = character_PowerSlotGetFromTray(pEntity->pChar, iTrayOld, iSlotOld);
			bValidDrag = true;
		}
		else if(stricmp(pchDragType, "PowerID") == 0 ||
				stricmp(pchDragType, "PowerNode") == 0)
		{
			iTrayOld = iTray;
			iSlotOld = -1;
			iPowerID = iID;
			bValidDrag = true;
		}

		if(bValidDrag)
		{
			if(pEntity->pChar && pElem->pTrayElem && pElem->pTrayElem->eType == kTrayElemType_PowerSlot)
			{
				PowerDef *ppowDef = character_FindPowerDefByID(pEntity->pChar, iPowerID);
				if(ppowDef)
					return( character_PowerTraySlotAllowsPowerDef(pEntity->pChar, iTray, iSlot, ppowDef, false) );
							//&& (iTray == iTrayOld || character_PowerTrayIDSlot(	pEntity->pChar, iTray, iPowerID) == -1));
				else if(iPowerID == 0)
					return(true);
				else
					return(false);
			}
			else
				return(false);
		}
	}
	return(false);
}
//Whether this Build elem can catch the dragged element...
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBuildElemCanCatch");
S32 exprEntBuildElemCanCatch(SA_PARAM_OP_VALID Entity *pEntity,
							SA_PARAM_OP_VALID UITrayElem *pElem,
							SA_PARAM_OP_STR const char *pchDragType,
							int iID,
							SA_PARAM_OP_STR const char *pchKey)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		int iTrayOld, iSlotOld;
		bool bValidDrag = false;
		U32 iPowerID = 0;

		if(stricmp(pchDragType, "TrayElem") == 0 ||
			stricmp(pchDragType, "BuildElem") == 0)
		{
			iTrayOld = iID >> 8;
			iSlotOld = iID & 255;
			iPowerID = character_PowerSlotGetFromTray(pEntity->pChar, iTrayOld, iSlotOld);
			bValidDrag = true;
		}
		else if(stricmp(pchDragType, "PowerID") == 0 ||
				stricmp(pchDragType, "PowerNode") == 0)
		{
			iTrayOld = iTray;
			iSlotOld = -1;
			iPowerID = iID;
			bValidDrag = true;
		}

		if(bValidDrag)
		{
			if(pEntity->pChar && pElem->pTrayElem && pElem->pTrayElem->eType == kTrayElemType_PowerSlot)
			{
				PowerDef *ppowDef = character_FindPowerDefByID(pEntity->pChar, iPowerID);
				if(ppowDef)
					return( character_PowerTraySlotAllowsPowerDef(pEntity->pChar, iTray, iSlot, ppowDef, true) );
							// && (iTray == iTrayOld || character_PowerTrayIDSlot(	pEntity->pChar, iTray, iPowerID) == -1));
				else if(iPowerID == 0)
					return(true);
				else
					return(false);
			}
			else
				return(false);
		}
	}
	return(false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchTrayElem");
void exprEntTrayElemCatchTrayElem(SA_PARAM_OP_VALID Entity *pEntity,
								  SA_PARAM_OP_VALID UITrayElem *pElem,
								  int iKeyDrop)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		int iTrayOld, iSlotOld;
		iTrayOld = iKeyDrop >> 8;
		iSlotOld = iKeyDrop & 255;
		if(pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
		{
			ServerCmd_PowerTray_SlotSwap(iTray, iSlot, iTrayOld, iSlotOld);
		}
		else
		{
			globCmdParsef("TrayElemMove %d %d %d %d",iTrayOld,iSlotOld,iTray,iSlot);
		}
	}
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchSavedPetPower");
void exprEntTrayElemCatchSavedPetPower(SA_PARAM_OP_VALID Entity *pEntity,
									   SA_PARAM_OP_VALID UITrayElem *pElem,
									   int iKeyDrop,
									   const char* pchStringDrop)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		U32 uPetID = (U32)iKeyDrop;
		const char* pchPowerDef = pchStringDrop;
		ServerCmd_TrayElemCreateSavedPetPower(iTray,iSlot,uPetID,pchPowerDef);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchMacro");
void exprEntTrayElemCatchMacro(SA_PARAM_OP_VALID Entity *pEntity,
							   SA_PARAM_OP_VALID UITrayElem *pElem,
							   const char* pchStringDrop)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		PlayerMacro Data = {0};
		tray_MacroDragStringToMacroData(pchStringDrop, &Data);
		ServerCmd_TrayElemCreateMacro(iTray,iSlot,&Data);
		StructDeInit(parse_PlayerMacro, &Data);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchMacroID");
void exprEntTrayElemCatchMacroID(SA_PARAM_OP_VALID Entity *pEntity,
								 SA_PARAM_OP_VALID UITrayElem *pElem,
								 int iMacroID)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		ServerCmd_TrayElemCreateMacroByID(iTray,iSlot,(U32)iMacroID);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBuildElemCatchBuildElem");
void exprEntBuildElemCatchBuildElem(SA_PARAM_OP_VALID Entity *pEntity,
								  SA_PARAM_OP_VALID UITrayElem *pElem,
								  int iKeyDrop)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		int iTrayOld, iSlotOld;
		iTrayOld = iKeyDrop >> 8;
		iSlotOld = iKeyDrop & 255;
		if(pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
		{
			if(iTray == iTrayOld)
			{
				ServerCmd_PowerTray_SlotSwap(iTray, iSlot, iTrayOld, iSlotOld);
			}
			else
			{
				U32 iPowerID = character_PowerSlotGetFromTray(pEntity->pChar, iTrayOld, iSlotOld);
				ServerCmd_PowerTray_Slot(iTray, iSlot, iPowerID, true);
			}
		}
		else
		{
			globCmdParsef("TrayElemMove %d %d %d %d",iTrayOld,iSlotOld,iTray,iSlot);
		}
	}
}

//This gets called when a build elem gets dropped onto a tray elem during dragon drop
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchBuildElem");
void exprEntTrayElemCatchBuildElem(SA_PARAM_OP_VALID Entity *pEntity,
								  SA_PARAM_OP_VALID UITrayElem *pElem,
								  int iKeyDrop)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		int iTrayOld, iSlotOld;
		iTrayOld = iKeyDrop >> 8;
		iSlotOld = iKeyDrop & 255;

		if(pEntity->pChar && pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
		{
			if(iTray == iTrayOld)
			{
				ServerCmd_PowerTray_SlotSwap(iTray, iSlot, iTrayOld, iSlotOld);
			}
			else
			{
				U32 iPowerID = character_PowerSlotGetFromTray(pEntity->pChar, iTrayOld, iSlotOld);
				ServerCmd_PowerTray_Slot(iTray, iSlot, iPowerID, true);
			}
		}
		else
		{
			globCmdParsef("TrayElemMove %d %d %d %d",iTrayOld,iSlotOld,iTray,iSlot);
		}
	}
}

//This gets called when a tray elem gets dropped onto a build elem during dragon drop
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBuildElemCatchTrayElem");
void exprEntBuildElemCatchTrayElem(SA_PARAM_OP_VALID Entity *pEntity,
								   SA_PARAM_OP_VALID UITrayElem *pElem,
								   int iKeyDrop)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		int iTrayOld, iSlotOld;
		iTrayOld = iKeyDrop >> 8;
		iSlotOld = iKeyDrop & 255;

		if(pEntity->pChar && pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
		{
			if(iTray == iTrayOld)
			{
				ServerCmd_PowerTray_SlotSwap(iTray, iSlot, iTrayOld, iSlotOld);
			}
			else
			{
				U32 iPowerID = character_PowerSlotGetFromTray(pEntity->pChar, iTrayOld, iSlotOld);
				ServerCmd_PowerTray_Slot(iTray, iSlot, iPowerID, true);
			}
		}
		else
		{
			globCmdParsef("TrayElemMove %d %d %d %d",iTrayOld,iSlotOld,iTray,iSlot);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchPowerID");
void exprEntTrayElemCatchPowerID(SA_PARAM_OP_VALID Entity *pEntity,
								 SA_PARAM_OP_VALID UITrayElem *pElem,
								 int iID,
								 F32 fPet)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;

		if(fPet == 0.0f)
		{
			// It's yours
			if(pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
			{
				ServerCmd_PowerTray_Slot(iTray,iSlot,iID, true);
			}
			else
			{
				ServerCmd_TrayElemCreatePower(iTray,iSlot,iID,false);
			}
		}
		else
		{
			// It's your Pet's power
			//ServerCmd_TrayElemCreatePowerPet(iTray,iSlot,iID);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchPowerNode");
void exprEntTrayElemCatchPowerNode(	SA_PARAM_OP_VALID Entity *pEntity,
								   SA_PARAM_OP_VALID UITrayElem *pElem,
								   int iID,
								   F32 fPet,
								   SA_PARAM_OP_STR const char *pchNodeFull)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;

		if(fPet == 0.0f)
		{
			// It's yours
			if(pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
			{
				ServerCmd_PowerTray_SlotNode(iTray,iSlot,pchNodeFull, true);
			}
			else
			{
				ServerCmd_TrayElemCreatePower(iTray,iSlot,iID,false);
			}
		}
		else
		{
			// It's your Pet's power
			//ServerCmd_TrayElemCreatePowerPet(iTray,iSlot,iID);
		}
	}
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBuildElemCatchPowerID");
void exprEntBuildElemCatchPowerID(SA_PARAM_OP_VALID Entity *pEntity,
								 SA_PARAM_OP_VALID UITrayElem *pElem,
								 int iID,
								 SA_PARAM_OP_STR const char *pchKey)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;

		if(!pchKey || !pchKey[0])
		{
			// It's yours
			if(pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
			{
				ServerCmd_PowerTray_Slot(iTray,iSlot,(U32)iID, true);
			}
			else
			{
				ServerCmd_TrayElemCreatePower(iTray,iSlot,iID,false);
			}
		}
		else
		{
			// It's your Pet's power (pchKey=="Pet")
			//ServerCmd_TrayElemCreatePowerPet(iTray,iSlot,iID);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntBuildElemCatchPowerNode");
void exprEntBuildElemCatchPowerNode(SA_PARAM_OP_VALID Entity *pEntity,
								    SA_PARAM_OP_VALID UITrayElem *pElem,
								    int iID,
								    F32 fPet,
								    SA_PARAM_OP_STR const char *pchNodeFull)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;

		if(fPet == 0.0f)
		{
			// It's yours
			if(pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
			{
				ServerCmd_PowerTray_SlotNode(iTray,iSlot,pchNodeFull, true);
			}
			else
			{
				ServerCmd_TrayElemCreatePower(iTray,iSlot,iID,false);
			}
		}
		else
		{
			// It's your Pet's power
			//ServerCmd_TrayElemCreatePowerPet(iTray,iSlot,iID);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchItem");
void exprEntTrayElemCatchItem(SA_PARAM_OP_VALID Entity *pEntity,
							  SA_PARAM_OP_VALID UITrayElem *pElem,
							  const char *pchInvBag,
							  int iInvBagSlot)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		S32 iInvBag = StaticDefineIntGetInt(InvBagIDsEnum,pchInvBag);
		ServerCmd_TrayElemCreateInventorySlot(iTray,iSlot,iInvBag,iInvBagSlot,-1);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchItemFromPayload");
void exprEntTrayElemCatchItemFromPayload(SA_PARAM_OP_VALID Entity *pEntity,
							  SA_PARAM_OP_VALID UITrayElem *pElem,
							  const char *pchPayload)
{
	if(pElem)
	{
		UIInventoryKey Key = {0};
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		if (gclInventoryParseKey(pchPayload, &Key))
			ServerCmd_TrayElemCreateInventorySlot(iTray,iSlot,Key.eBag,Key.iSlot,-1);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchTempPowerFromItemPayload");
void exprEntTrayElemCatchTempPowerFromItemPayload(SA_PARAM_OP_VALID Entity *pEntity,
										 SA_PARAM_OP_VALID UITrayElem *pElem,
										 const char *pchPayload)
{
	if(pElem)
	{
		UIInventoryKey Key = {0};
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		if (gclInventoryParseKey(pchPayload, &Key))
			ServerCmd_TrayElemCreatePowerFromInventorySlot(iTray,iSlot,Key.eBag,Key.iSlot,-1);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchItemWithKey");
void exprEntTrayElemCatchItemWithKey(	SA_PARAM_OP_VALID Entity *pEntity,
										SA_PARAM_OP_VALID UITrayElem *pElem,
										int iKey )
{
	if ( pElem )
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		int iInvBagSlot = iKey >> 16;
		int iItemPowIdx = (iKey >> 8) & (255);
		int iInvBag = iKey & 255;;

		ServerCmd_TrayElemCreateInventorySlot(iTray,iSlot,iInvBag,iInvBagSlot,iItemPowIdx);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemCatchPowerPropSlot");
void exprEntTrayElemCatchPowerPropSlot(	SA_PARAM_OP_VALID Entity *pEntity,
										SA_PARAM_OP_VALID UITrayElem *pElem,
										int iKey )
{
	if (pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		U32 uPropSlotID = 0;
		
		if (pElem->pTrayElem)
		{
			S32 ePurpose;
			tray_PowerPropSlotStringToSlotData(pElem->pTrayElem->pchIdentifier, &ePurpose, &uPropSlotID);
		}
		else if (pEntity && pEntity->pSaved)
		{
			U32 uPowID = (U32)iKey;
			U32 uPetID = POWERID_GET_ENT(uPowID);
			S32 iPropSlotIdx = AlwaysPropSlot_FindByPetID(pEntity, uPetID, pEntity->pSaved->pPuppetMaster->curID, kAlwaysPropSlotCategory_Default);
			AlwaysPropSlot* pPropSlot = eaGet(&pEntity->pSaved->ppAlwaysPropSlots, iPropSlotIdx);
			if (pPropSlot)
			{
				uPropSlotID = pPropSlot->iSlotID;
			}
		}
		ServerCmd_TrayElemCreatePowerPropSlot(iTray,iSlot,iKey,uPropSlotID);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemDestroy");
void exprEntTrayElemDestroy(SA_PARAM_OP_VALID Entity *pEntity,
							SA_PARAM_OP_VALID UITrayElem *pElem)
{
	if(pElem)
	{
		int iTray = pElem->iTray;
		int iSlot = pElem->iSlot;
		if(pElem->pTrayElem && pElem->pTrayElem->eType==kTrayElemType_PowerSlot)
		{
			ServerCmd_PowerTray_Slot(iTray,iSlot,0, true);
		}
		else
		{
			ServerCmd_TrayElemDestroy(iTray,iSlot);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemName");
const char *exprEntTrayElemName(SA_PARAM_OP_VALID Entity *pEntity,
								   SA_PARAM_NN_VALID UITrayElem *pelem)
{
	pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,pelem->pPetData);

	if(pEntity && pEntity->pChar && pelem->pTrayElem)
	{
		Character *pchar = pEntity->pChar;
		Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
		if(pchar && ppow)
		{
			PowerDef* pDef = GET_REF(ppow->hDef);
			if (pDef)
				return pDef->pchName;
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemDisplayName");
const char *exprEntTrayElemDisplayName(SA_PARAM_OP_VALID Entity *pEntity,
									   SA_PARAM_NN_VALID UITrayElem *pelem)
{
	if (pelem->pTrayElem && pelem->pTrayElem->eType == kTrayElemType_Macro)
	{
		return pelem->estrShortDesc;
	}
	else
	{
		pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,pelem->pPetData);

		if(pEntity && pelem->pTrayElem)
		{
			Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
			if(ppow)
			{
				PowerDef *pDef = GET_REF(ppow->hDef);
				if(pDef)
				{
					return TranslateDisplayMessage(pDef->msgDisplayName);
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemLongDescription");
const char *exprEntTrayElemLongDescription(SA_PARAM_OP_VALID Entity *pEntity,
										   SA_PARAM_NN_VALID UITrayElem *pelem)
{
	pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,pelem->pPetData);

	if(pEntity && pelem->pTrayElem)
	{
		Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
		if(ppow)
		{
			PowerDef *pDef = GET_REF(ppow->hDef);
			if(pDef)
			{
				return TranslateDisplayMessage(pDef->msgDescriptionLong);
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemFlavorDescription");
const char *exprEntTrayElemFlavorDescription(SA_PARAM_OP_VALID Entity *pEntity,
	SA_PARAM_NN_VALID UITrayElem *pelem)
{
	pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,pelem->pPetData);

	if(pEntity && pelem->pTrayElem)
	{
		Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
		if(ppow)
		{
			PowerDef *pDef = GET_REF(ppow->hDef);
			if(pDef)
			{
				return TranslateDisplayMessage(pDef->msgDescriptionFlavor);
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemDescription");
const char *exprEntTrayElemDescription(SA_PARAM_OP_VALID Entity *pEntity,
	SA_PARAM_NN_VALID UITrayElem *pelem)
{
	pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,pelem->pPetData);

	if(pEntity && pelem->pTrayElem)
	{
		Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
		if(ppow)
		{
			PowerDef *pDef = GET_REF(ppow->hDef);
			if(pDef)
			{
				return TranslateDisplayMessage(pDef->msgDescription);
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemRankChangeDescription");
const char *exprEntTrayElemRankChangeDescription(SA_PARAM_OP_VALID Entity *pEntity,
	SA_PARAM_NN_VALID UITrayElem *pelem)
{
	pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,pelem->pPetData);

	if(pEntity && pelem->pTrayElem)
	{
		Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
		if(ppow)
		{
			PowerDef *pDef = GET_REF(ppow->hDef);
			if(pDef)
			{
				return TranslateDisplayMessage(pDef->msgRankChange);
			}
		}
	}

	return NULL;
}
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTrayElemTooltip");
const char *exprEntTrayElemTooltip(SA_PARAM_OP_VALID Entity *pEntity,
								   SA_PARAM_NN_VALID UITrayElem *pelem)
{
	static char *s_pchTooltip = NULL;

 	estrDestroy(&s_pchTooltip);

	if (pelem->pTrayElem && pelem->pTrayElem->eType == kTrayElemType_Macro)
	{
		// TODO: Special tooltips for macros
	}
	else
	{
		pEntity = EntTrayGetExecutor(pEntity,pelem->pTrayElem,pelem->pPetData);

		if(pEntity && pEntity->pChar && pelem->pTrayElem)
		{
			Character *pchar = pEntity->pChar;
			Power *ppow = EntTrayGetActivatedPower(pEntity,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
			if(pchar && ppow)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				power_AutoDesc(entGetPartitionIdx(pEntity),ppow,pchar,&s_pchTooltip,NULL,"<br>","<bsp><bsp>","- ",false,0,entGetPowerAutoDescDetail(entActivePlayerPtr(),true),pExtract, NULL);
			}
		}
		else if (pEntity && pEntity->pChar && pelem->pPetData && pelem->pPetData->pchPower)
		{
			Character *pchar = pEntity->pChar;
			PowerDef *pdef = RefSystem_ReferentFromString(g_hPowerDefDict, pelem->pPetData->pchPower);
			if(pchar && pdef)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				powerdef_AutoDesc(entGetPartitionIdx(pEntity),pdef,&s_pchTooltip,NULL,"<br>","<bsp><bsp>","- ",pchar, NULL, NULL, pchar->iLevelCombat, 0, entGetPowerAutoDescDetail(pEntity,true), pExtract, NULL);
			}
		}
	}
	return s_pchTooltip;
}

// TrayChangeIndex <UITray> <Change>: Change the UITray's displayed Tray by a positive or negative amount.  Will rollover in case of underflow or overflow.
AUTO_COMMAND ACMD_NAME("TrayChangeIndex") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void cmdTrayChangeIndex(int iUITray, int iChange)
{
	Entity *e = entActivePlayerPtr();
	if(e)
	{
		int iCurrent = entity_TrayGetUITrayIndex(e,iUITray);
		int iNew = iCurrent + iChange;
		if(iNew < 0)
		{
			iNew += TRAY_COUNT_MAX;
		}
		else if(iNew >= TRAY_COUNT_MAX)
		{
			iNew -= TRAY_COUNT_MAX;
		}
		ServerCmd_TraySetIndex(iUITray,iNew);
	}
}

static bool TrayNotifyAudioError_Internal(Entity* e,
										  bool bRecharging,
										  bool bInCooldown,
										  bool bActivatable,
										  const char* pchCooldownSnd,
										  const char* pchNotActivatableSnd)
{
	if (bRecharging || bInCooldown)
	{
		sndPlayAtCharacter(pchCooldownSnd,"UITray.c",0,NULL,NULL);
		return true;
	}
	else if (!bActivatable)
	{
		sndPlayAtCharacter(pchNotActivatableSnd,"UITray.c",0,NULL,NULL);
		return true;
	}
	return false;
}

static bool TrayNotifyAudioError(Entity* e,
								 TrayElem* pElem,
								 const char* pchCooldownSnd,
								 const char* pchNotActivatableSnd)
{

	if (e->pChar && pElem)
	{
		bool bCurrent, bQueued, bRecharging, bInCooldown, bActivatable;

		if (pElem->eType == kTrayElemType_SavedPetPower)
		{
			const char* pchPowerName = pElem->pchIdentifier;
			PowerDef* pDef = powerdef_Find(pchPowerName);
			PlayerPetInfo* pPetInfo = player_GetPetInfo(e->pPlayer, pElem->erOwner);
			PetPowerState* pPetPowerState = playerPetInfo_FindPetPowerStateByName(pPetInfo, pchPowerName);
			if (pDef && pPetInfo && pPetPowerState)
			{
				bCurrent = false;
				bQueued = pPetPowerState->bQueuedForUse;
				bRecharging = (pPetPowerState->fTimerRecharge > 0);
				bInCooldown = (player_GetPetCooldownFromPowerDef(e->pPlayer, pPetInfo->iPetRef, pDef) > 0);;
				bActivatable = !bRecharging && !bInCooldown;
				return TrayNotifyAudioError_Internal(e,bRecharging,bInCooldown,bActivatable,pchCooldownSnd,pchNotActivatableSnd);
			}
		}
		else
		{
			Power* pPow = EntTrayGetActivatedPower(e,pElem,false,NULL);
			if (pPow)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
				Entity *pEntTarget;
				WorldInteractionNode *pNodeTarget;
				Power *ppowBase = pPow->pParentPower ? pPow->pParentPower : pPow;
				PowerDef *pdefBase = GET_REF(ppowBase->hDef);
				PowerDef *pDef = GET_REF(pPow->hDef);

				EntPowerGetTarget(e->pChar, pPow, &pEntTarget, &pNodeTarget);

				bCurrent = e->pChar->pPowActCurrent && e->pChar->pPowActCurrent->ref.uiID == ppowBase->uiID;
				bQueued = e->pChar->pPowActQueued && e->pChar->pPowActQueued->ref.uiID == ppowBase->uiID;
				bRecharging = !bCurrent && !bQueued && power_GetRecharge(ppowBase) > FLT_EPSILON;
				bInCooldown = !bCurrent && !bQueued && character_GetCooldownFromPower(e->pChar,ppowBase) > FLT_EPSILON;
				bActivatable = EntIsPowerTrayActivatableInternal(e->pChar, pPow, pEntTarget, pNodeTarget, pExtract);
				return TrayNotifyAudioError_Internal(e,bRecharging,bInCooldown,bActivatable,pchCooldownSnd,pchNotActivatableSnd);
			}
		}
	}
	return false;
}

static U32 PowerExec_GetRootActivatablePowerID(Power *pPower)
{
	if (pPower->pParentPower)
	{
		return !pPower->pParentPower->pCombatPowerStateParent ? 
					pPower->pParentPower->uiID : pPower->pParentPower->pCombatPowerStateParent->uiID;
	}

	return !pPower->pCombatPowerStateParent ? pPower->uiID : pPower->pCombatPowerStateParent->uiID;
}


static bool PowerExec(Power* ppow, Power* ppowBase, int bActive)
{
	Entity *e = entActivePlayerPtr();
	PowerDef *pDef = GET_REF(ppow->hDef);
	bool bIsBaseAutoPower = false;
	PowerDef *pDefBase = NULL;
	U32 powID = PowerExec_GetRootActivatablePowerID(ppow);
	bool bIsAutoPower = gclAutoAttack_PowerIDLegal(powID) && gclAutoAttack_GetOverrideID() != powID;
		
	if(ppowBase)
	{
		U32 powIDBase = ppowBase->pParentPower ? ppowBase->pParentPower->uiID : ppowBase->uiID;
		pDefBase = GET_REF(ppowBase->hDef);
		bIsBaseAutoPower = gclAutoAttack_PowerIDLegal(powIDBase) && gclAutoAttack_GetOverrideID() != powIDBase;
	}

	if(!pDef)
		bIsAutoPower = false;
	if(!pDefBase)
		bIsBaseAutoPower = false;

	if (gclCursorPowerLocationTargeting_PowerValid(pDef))
	{
		if(bActive)
		{
			if (gclCursorPowerLocationTargeting_GetCurrentPowID() == powID)
			{
				if (g_CurrentScheme.ePowerCursorActivationType == kPowerCursorActivationType_ActivateOnSecondPress)
					gclCursorPowerLocationTargeting_PowerExec();
			}
			else
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
				ANALYSIS_ASSUME(e->pChar); // I think this is correct, but i'm not sure.
				if(character_ActTestPowerTargeting(entGetPartitionIdx(e), e->pChar, ppow, pDef, false, pExtract))
				{
					// Location targeting needs to be activated before we use the power
					gclCursorPowerLocationTargeting_Begin(powID);
				}
			}
		}
		else if (g_CurrentScheme.ePowerCursorActivationType == kPowerCursorActivationType_ActivateOnRelease && 
					gclCursorPowerLocationTargeting_GetCurrentPowID() == powID)
		{
			gclCursorPowerLocationTargeting_PowerExec();
		}
	}
	else if (e && gclCursorPowerTargeting_PowerValid(e->pChar, pDef))
	{
		if(bActive)
		{
			if (gclCursorPowerTargeting_GetCurrentPowID() == powID)
			{
				if (g_CurrentScheme.ePowerCursorActivationType == kPowerCursorActivationType_ActivateOnSecondPress)
					gclCursorPowerTargeting_PowerExec();
			}
			else
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
				ANALYSIS_ASSUME(e->pChar); // I think this is correct, but i'm not sure.
				if(character_ActTestPowerTargeting(entGetPartitionIdx(e), e->pChar, ppow, pDef, false, pExtract))
				{
					// Activate mouse targeting
					gclCursorPowerTargeting_Begin(powID, pDef->eEffectArea);
				}
			}
		}
		else if (g_CurrentScheme.ePowerCursorActivationType == kPowerCursorActivationType_ActivateOnRelease && 
					gclCursorPowerTargeting_GetCurrentPowID() == powID)
		{
			gclCursorPowerTargeting_PowerExec();
		}
	}
	else if(bIsAutoPower
		&& (g_CurrentScheme.eAutoAttackType == kAutoAttack_Toggle
			|| g_CurrentScheme.eAutoAttackType == kAutoAttack_ToggleNoCancel
			|| g_CurrentScheme.eAutoAttackType == kAutoAttack_ToggleCombat))
	{
		if(bActive)
			gclAutoAttack_ToggleDefaultAutoAttack();
	}
	//For maintain autoattack, turn it on only if the picked power is auto attack
	else if( (bIsAutoPower || (bIsBaseAutoPower && !bActive)) && g_CurrentScheme.eAutoAttackType == kAutoAttack_Maintain)
	{
		// check if we are disabling auto-attack on a power that isn't the current autoattack power
		if (!bActive && gclAutoAttack_IsExplicitPowerSet() && !gclAutoAttack_IsExplicitPowerEnabled(powID))
			return false;
		// check if we have a different power autoattacking, stop the activations of it
		if (bActive && gclAutoAttack_IsExplicitPowerSet() && !gclAutoAttack_IsExplicitPowerEnabled(powID))
		{
			gclAutoAttack_StopActivations(false, true);
		}

		gclAutoAttack_SetExplicitPowerEnabledID(powID);
		gclAutoAttack_DefaultAutoAttack(bActive);
		
	}
	else
	{
		entUsePowerID(bActive,powID);
	}
	return true;
}
static bool WarpExec(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID TrayElem *pelem)
{
	const char *pchInventorySlot = pelem->pchIdentifier;
	if(pelem->eType == kTrayElemType_InventorySlot && pchInventorySlot)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		Item *pItem = TrayInventoryElemGetItem(e, pelem, pExtract);
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

		if(pItemDef && gclWarp_CanExec(e, pItem, pItemDef))
		{
			gclWarp_StartItemWarp(pItem, pItemDef);
			return true;
		}
	}

	return false;
}

static bool PetTrayExecInternal(Entity *pPetEnt, const char* pchPowerDef, const char* pchAiState)
{
	if (pPetEnt)
	{
		if (pchPowerDef)
		{
			ServerCmd_PetCommands_UsePowerForPet(entGetRef(pPetEnt), pchPowerDef);
			return true;
		}
		else if (pchAiState)
		{
			ServerCmd_PetCommands_SetSpecificPetState(entGetRef(pPetEnt), pchAiState);
			return true;
		}
	}
	else
	{
		if (pchPowerDef)
		{
			ServerCmd_PetCommands_UsePowerForAllPets(pchPowerDef);
			return true;
		}
		else if (pchAiState)
		{
			ServerCmd_PetCommands_SetAllPetsState(pchAiState);
			return true;
		}
	}
	return false;
}

static bool PetTrayExec(PetTrayElemData *pData)
{
	Entity *pPetEnt = entFromEntityRefAnyPartition(pData->erOwner);
	return PetTrayExecInternal(pPetEnt,pData->pchPower,pData->pchAiState);
}

// entity and pelem must be good
static void TrayItemExecFailure(Entity *pEntity, TrayElem *pelem)
{

	if(pelem->pchIdentifier)
	{
		S32 iBag = -1, iSlot = -1, iItemPowIdx = -1;
		InventoryBag *pBag;
		Item *pItem;
		ItemDef *pItemDef;

		tray_InventorySlotStringToIDs(pelem->pchIdentifier, &iBag, &iSlot, &iItemPowIdx);

		pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEntity), iBag, NULL));
		pItem = inv_bag_GetItem(pBag, iSlot);
		pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

		if(pItemDef && pItemDef->bMessageOnTrayActivateFailure)
		{
			notify_NotifySend(pEntity, kNotifyType_PowerExecutionFailed,  TranslateMessageKey("Item_TrayCantActivatePower"), NULL, NULL);
		}
	}
}

// General Tray execution function
// Added the bool return as feedback for functions that call this
static bool TrayExec(Entity *e, TrayElem *pelem, int bActive)
{
	if (pelem && pelem->eType == kTrayElemType_SavedPetPower)
	{
		Entity *pPetEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET, pelem->lIdentifier);
		return PetTrayExecInternal(pPetEnt, pelem->pchIdentifier, NULL);
	}
	else if (e && pelem && pelem->eType == kTrayElemType_Macro)
	{
		U32 uMacroID = (U32)pelem->lIdentifier;
		gclPlayerExecMacro(uMacroID);
	}
	else if(e && pelem)
	{
		if(!pelem->erOwner)
		{
			Power *ppowTrayBase = NULL;
			Power *ppow = EntTrayGetActivatedPower(e,pelem,false,&ppowTrayBase);
			if(ppow)
			{
				PowerExec(ppow, ppowTrayBase, bActive);
			}
			else if(bActive)
			{
				WarpExec(e, pelem);
			}

			// Error message about power failure for items
			if(!ppow && pelem->eType == kTrayElemType_InventorySlot && bActive)
			{
				TrayItemExecFailure(e, pelem);
			}
		}
		else
		{
			Entity *eExec = EntTrayGetExecutor(e,pelem,NULL);
			if(eExec)
			{
				Power *ppow = EntTrayGetActivatedPower(eExec,pelem,false,NULL);
				if(ppow)
				{
					ServerCmd_PetCommands_UsePowerForPet(entGetRef(eExec), REF_STRING_FROM_HANDLE(ppow->hDef));
					return true;
				}
			}
		}
	}
	return false;
}

// PowerSlotExec <Active> <Slot>: Attempts to execute whatever Power is in the given PowerSlot
AUTO_COMMAND ACMD_NAME("PowerSlotExec","PowerTrayExec") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void gclPowerSlotExec(int bActive, int iSlot)
{
	static TrayElem trayElem = {0};
	Entity *e = entActivePlayerPtr();
	if(e)
	{
		tray_SetTrayElemForPowerSlot(&trayElem,-1,-1,iSlot);
		TrayExec(e,&trayElem,bActive);
	}
}

// TrayExec <Active> <UITray> <Slot>: Attempts to execute the element in the UITray at the Slot
AUTO_COMMAND ACMD_NAME("TrayExec") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void cmdTrayExec(int bActive, int iUITray, int iSlot)
{
	Entity *e = entActivePlayerPtr();
	if(e)
	{
		TrayExec(e,entity_TrayGetUITrayElem(e,iUITray,iSlot),bActive);
	}
}

// TrayExec <Active> <Tray> <Slot>: Attempts to execute the element in the Tray at the Slot
AUTO_COMMAND ACMD_NAME("TrayExecByTray") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void cmdTrayExecByTray(int bActive, int iTray, int iSlot)
{
	Entity *e = entActivePlayerPtr();
	if(e)
	{
		TrayExec(e,entity_TrayGetTrayElem(e,iTray,iSlot),bActive);
	}
}

AUTO_COMMAND ACMD_NAME("TrayExecByTrayNotifyAudio") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void cmdTrayExecByTrayNotifyAudio(int bActive, int iTray, int iSlot, const char* pchCooldownSnd, const char* pchFailSnd)
{
	Entity *e = entActivePlayerPtr();
	if(e)
	{
		TrayElem* pElem = entity_TrayGetTrayElem(e,iTray,iSlot);
		TrayNotifyAudioError(e,pElem,pchCooldownSnd,pchFailSnd);
		TrayExec(e,pElem,bActive);
	}
}

// TrayExec <Active> <Tray> <Slot>: Attempts to execute the element in the Tray at the Slot
AUTO_COMMAND ACMD_NAME("TrayExecByTrayWithBackup") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void cmdTrayExecByTrayWithBackup(int bActive, int iTray, int iSlot, int iTray2, int iSlot2)
{
	Entity *e = entActivePlayerPtr();
	if(e)
	{
		if (!TrayExec(e,entity_TrayGetTrayElem(e,iTray,iSlot),bActive))
			TrayExec(e,entity_TrayGetTrayElem(e,iTray2,iSlot2),bActive);
	}
}

bool UITrayExec(bool bActive, SA_PARAM_NN_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem && pelem->bValid && !pelem->bUseBaseReplacePower)
	{
		if (pelem->pPetData)
		{
			return PetTrayExec(pelem->pPetData);
		}
		else if (pelem->pTrayElem)
		{
			return TrayExec(e,pelem->pTrayElem,bActive);
		}
	}
	return false;
}

//non-static version
bool gclTrayExec(bool bActive, TrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem)
	{
		return TrayExec(e,pelem,bActive);
	}
	return false;
}

// Gen function for executing tray elements
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayExecGen");
void exprTrayExec(int bActive, SA_PARAM_OP_VALID UITrayElem *pelem)
{
	UITrayExec(bActive, pelem);
}

// Gen function for executing tray elements
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayExecGenNotifyAudio");
bool exprTrayExecGenNotifyAudio(int bActive, SA_PARAM_OP_VALID UITrayElem *pelem,
								const char* pchCooldownSnd, const char* pchFailSnd)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem && pelem->bValid && !pelem->bUseBaseReplacePower)
	{
		bool bRecharge = pelem->bRecharging;
		bool bCooldown = pelem->bInCooldown;
		bool bActivate = pelem->bActivatable;
		TrayNotifyAudioError_Internal(e,bRecharge,bCooldown,bActivate,pchCooldownSnd,pchFailSnd);
		TrayExec(e,pelem->pTrayElem,bActive);
		return true;
	}
	return false;
}

__forceinline static bool gclTrayPowerMatchesCategory(SA_PARAM_NN_VALID Power* pPow, S32 iCategory)
{
	PowerDef* pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;
	return (pPowDef && (iCategory < 0 || eaiFind(&pPowDef->piCategories, iCategory) >= 0));
}

// Gen function for executing all tray elements by category .
// if pchCategory == "" or NULL, then that indicates all categories
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayExecGenAllByCategory");
void exprTrayExecGenAllByCategory(int bActive, SA_PARAM_OP_VALID UIGen* pGen, const char* pchCategory)
{
	Entity *pEntity = entActivePlayerPtr();
	S32 iCategory = pchCategory && pchCategory[0] ? StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory) : -1;
	S32 i;
	UITrayElem*** peaElems = ui_GenGetManagedListSafe( pGen, UITrayElem );

	if ( pEntity && pEntity->pChar && pGen && eaSize(peaElems)>0 )
	{
		for ( i = eaSize(peaElems)-1; i >= 0; i-- )
		{
			UITrayElem* pElemUI = (*peaElems)[i];
			TrayElem* pElem = pElemUI->pTrayElem;

			if ( pElemUI->bUseBaseReplacePower || pElem==NULL )
				continue;

			if ( pElem->eOwner==kTrayElemOwner_Self )
			{
				Power* pPow = EntTrayGetActivatedPower(pEntity,pElem,false,NULL);

				if (gclTrayPowerMatchesCategory(pPow, iCategory))
				{
					U32 uiPowID = pPow->pParentPower ? pPow->pParentPower->uiID : pPow->uiID;
					clientTarget_AddMultiPowerExec(uiPowID);
				}
			}
			else
			{
				Entity *ePet = entGetPrimaryPet(pEntity);
				Power* pPow = ePet ? EntTrayGetActivatedPower(ePet,pElem,false,NULL) : NULL;

				if (gclTrayPowerMatchesCategory(pPow, iCategory))
				{
					U32 uiPowID = pPow->pParentPower ? pPow->pParentPower->uiID : pPow->uiID;
					clientTarget_AddMultiPowerExec(uiPowID);
				}
			}
		}
	}
	ui_GenSetListSafe( pGen, peaElems, UITrayElem );
}

static void EntGetInvTrayPowersByCategory(Power*** peaPowers,
										  Entity* pEnt,
										  InventoryBag* pBag,
										  S32 iCategory,
										  S32 iInvBagSlotMin,
										  S32 iInvBagSlotMax,
										  S32 iItemPowerIdx)
{
	if (iInvBagSlotMax == -1 && pBag)
		iInvBagSlotMax = invbag_maxslots(pEnt, pBag) - 1;

	if (pBag && iInvBagSlotMin>=0 && iInvBagSlotMax>=iInvBagSlotMin)
	{
		int i,s=1+iInvBagSlotMax-iInvBagSlotMin;

		for (i=0; i<s; i++)
		{
			Item* pItem = inv_bag_GetItem(pBag, iInvBagSlotMin + i);
			S32 iPowIdx, iPowers = pItem ? item_GetNumItemPowerDefs(pItem, true) : 0;

			if (iItemPowerIdx < 0)
			{
				for (iPowIdx = 0; iPowIdx < iPowers; iPowIdx++)
				{
					Power* pPowItem = item_GetPower(pItem,iPowIdx);
					if (pPowItem && gclTrayPowerMatchesCategory(pPowItem, iCategory))
					{
						eaPush(peaPowers, pPowItem);
					}
				}
			}
			else
			{
				Power* pPowItem = item_GetPower(pItem,iItemPowerIdx);
				if (pPowItem && gclTrayPowerMatchesCategory(pPowItem, iCategory))
				{
					eaPush(peaPowers, pPowItem);
				}
			}
		}
	}
}

static void gclTrayPowersSortBySmallestArc(Entity* pEnt,
										   Power** eaPowersA,
										   Power** eaPowersB,
										   Power*** peaPowersSorted)
{
	S32 iIndexA = 0;
	S32 iIndexB = 0;
	S32 bLastInArcA = false;
	S32 bLastInArcB = false;
	while (1)
	{
		Entity* pEntTarget=NULL;
		WorldInteractionNode* pNodeTarget=NULL;
		Power* pPowerA = eaGet(&eaPowersA, iIndexA);
		Power* pPowerB = eaGet(&eaPowersB, iIndexB);
		F32 fArcA = -1;
		F32 fArcB = -1;
		if (!pPowerA && !pPowerB)
			break;
		if (pPowerA)
		{
			PowerDef* pDefA = GET_REF(pPowerA->hDef);
			fArcA = pDefA ? pDefA->fTargetArc : 0.0f;
			if (!bLastInArcA)
			{
				EntPowerGetTarget(pEnt->pChar, pPowerA, &pEntTarget, &pNodeTarget);
				if (!PowerTargetInArc(pEnt,pPowerA,pEntTarget,pNodeTarget))
				{
					iIndexA++;
					continue;
				}
			}
			bLastInArcA = true;
		}
		if (pPowerB)
		{
			PowerDef* pDefB = GET_REF(pPowerB->hDef);
			fArcB = pDefB ? pDefB->fTargetArc : 0.0f;
			if (!bLastInArcB)
			{
				EntPowerGetTarget(pEnt->pChar, pPowerB, &pEntTarget, &pNodeTarget);
				if (!PowerTargetInArc(pEnt,pPowerB,pEntTarget,pNodeTarget))
				{
					iIndexB++;
					continue;
				}
			}
			bLastInArcB = true;
		}
		if (!pPowerB || (pPowerA && fArcA <= fArcB))
		{
			eaPush(peaPowersSorted, pPowerA);
			iIndexA++;
			bLastInArcA = false;
		}
		else
		{
			eaPush(peaPowersSorted, pPowerB);
			iIndexB++;
			bLastInArcB = false;
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ExecAllInInvBagsByCategorySort");
void exprExecAllInInvBagsByCategorySorted(const char* pchBagA,
										  const char* pchBagB,
										  const char* pchCategory,
										  U32 eSortType)
{
	Power** eaPowersA = NULL;
	Power** eaPowersB = NULL;
	Power** eaPowersSorted = NULL;
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 iInvBagA = StaticDefineIntGetInt(InvBagIDsEnum,pchBagA);
	S32 iInvBagB = StaticDefineIntGetInt(InvBagIDsEnum,pchBagB);
	S32 i, iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);
	EntGetInvTrayPowersByCategory(&eaPowersA, pEnt, (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iInvBagA, pExtract), iCategory, 0, -1, -1);
	EntGetInvTrayPowersByCategory(&eaPowersB, pEnt, (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iInvBagB, pExtract), iCategory, 0, -1, -1);

	if (!eaSize(&eaPowersA) && !eaSize(&eaPowersB))
	{
		return;
	}
	switch (eSortType)
	{
		xcase UITrayPowerSortType_SmallestArc:
		{
			gclTrayPowersSortBySmallestArc(pEnt, eaPowersA, eaPowersB, &eaPowersSorted);
		}
	}
	for (i = eaSize(&eaPowersSorted)-1; i >= 0; i--)
	{
		Power* pPow = eaPowersSorted[i];
		U32 uiPowID = pPow->pParentPower ? pPow->pParentPower->uiID : pPow->uiID;
		clientTarget_AddMultiPowerExec(uiPowID);
	}
	eaDestroy(&eaPowersA);
	eaDestroy(&eaPowersB);
	eaDestroy(&eaPowersSorted);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ExecAllInInvBagsWithCategoriesSort");
void exprExecAllInInvBagsWithCategoriesSort(const char* pchBags, 
											const char* pchCategories, 
											U32 eSortType)
{
	Power** eaPowers = NULL;
	Power** eaPowersSorted1 = NULL;
	Power** eaPowersSorted2 = NULL;
	Power*** peaPowersList = &eaPowersSorted1;
	Power*** peaPowersSorted = &eaPowersSorted2;
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	char *pchBagsBuffer, *pchCategoriesBuffer;
	char *pchBag, *pchCategory, *pchBagCtx = NULL, *pchCategoryCtx = NULL;
	S32 i;

	if (!pchCategories)
		pchCategories = "";

	strdup_alloca(pchBagsBuffer, pchBags);
	strdup_alloca(pchCategoriesBuffer, pchCategories);

	while (pchBag = strtok_r(pchBagCtx ? NULL : pchBagsBuffer, " ,%\r\n\t", &pchBagCtx))
	{
		InvBagIDs eBag = StaticDefineIntGetInt(InvBagIDsEnum, pchBag);
		InventoryBag *pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBag, pExtract));

		while (pchCategory = strtok_r(pchCategoryCtx ? NULL : pchCategoriesBuffer, " ,%\r\n\t", &pchCategoryCtx))
		{
			eaClear(&eaPowers);
			EntGetInvTrayPowersByCategory(&eaPowers, pEnt, pBag, StaticDefineIntGetInt(PowerCategoriesEnum, pchCategory), 0, -1, -1);

			for (i = eaSize(&eaPowers) - 1; i >= 0; i--)
			{
				if (eaFind(peaPowersSorted, eaPowers[i]) >= 0)
					eaRemove(&eaPowers, i);
			}

			switch (eSortType)
			{
				xcase UITrayPowerSortType_SmallestArc:
				{
					gclTrayPowersSortBySmallestArc(pEnt, *peaPowersSorted, eaPowers, peaPowersList);
					peaPowersList = peaPowersList == &eaPowersSorted1 ? &eaPowersSorted2 : &eaPowersSorted1;
					peaPowersSorted = peaPowersSorted == &eaPowersSorted1 ? &eaPowersSorted2 : &eaPowersSorted1;
					eaClear(peaPowersList);
				}
			}
		}

		if (!pchCategories[0])
		{
			eaClear(&eaPowers);
			EntGetInvTrayPowersByCategory(&eaPowers, pEnt, pBag, -1, 0, -1, -1);

			for (i = eaSize(&eaPowers) - 1; i >= 0; i--)
			{
				if (eaFind(peaPowersSorted, eaPowers[i]) >= 0)
					eaRemove(&eaPowers, i);
			}

			switch (eSortType)
			{
				xcase UITrayPowerSortType_SmallestArc:
				{
					gclTrayPowersSortBySmallestArc(pEnt, *peaPowersSorted, eaPowers, peaPowersList);
					peaPowersList = peaPowersList == &eaPowersSorted1 ? &eaPowersSorted2 : &eaPowersSorted1;
					peaPowersSorted = peaPowersSorted == &eaPowersSorted1 ? &eaPowersSorted2 : &eaPowersSorted1;
					eaClear(peaPowersList);
				}
			}
		}

		strcpy_s(pchCategoriesBuffer, strlen(pchCategories) + 1, pchCategories);
		pchCategoryCtx = NULL;
	}

	for (i = eaSize(peaPowersSorted)-1; i >= 0; i--)
	{
		Power* pPow = (*peaPowersSorted)[i];
		U32 uiPowID = pPow->pParentPower ? pPow->pParentPower->uiID : pPow->uiID;
		clientTarget_AddMultiPowerExec(uiPowID);
	}

	eaDestroy(&eaPowers);
	eaDestroy(&eaPowersSorted1);
	eaDestroy(&eaPowersSorted2);
}

static Power* gclGetActiveWeaponPower(Entity* pEnt, const char* pchBag, S32 iItemPowerIdx, GameAccountDataExtract *pExtract)
{
	InvBagIDs eBagID = StaticDefineIntGetInt(InvBagIDsEnum, pchBag);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	const InvBagDef* pDef = invbag_def(pBag);
	Power* pPower = NULL;

	if (pDef && (pDef->flags & InvBagFlag_ActiveWeaponBag))
	{
		Power** eaPowers = NULL;
		S32 iBagSlot = invbag_GetActiveSlot(pBag, 0);
		EntGetInvTrayPowersByCategory(&eaPowers, pEnt, pBag, -1, iBagSlot, iBagSlot, iItemPowerIdx);
		pPower = eaGet(&eaPowers, 0);
		eaDestroy(&eaPowers);
	}
	return pPower;
}

AUTO_COMMAND ACMD_NAME("ExecActiveItemPowerInBag") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface);
void cmdExecActiveItemPowerInBag(int bActive,
								 const char* pchBag,
								 S32 iItemPowerIdx,
								 const char* pchFallbackPower)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		Power* pPower = gclGetActiveWeaponPower(pEnt, pchBag, iItemPowerIdx, pExtract);
		if (pPower)
		{
			Power* pPowBase = NULL;
			EntGetActivatedPower(pEnt, pPower, false, &pPowBase);
			PowerExec(pPower, pPowBase, true);
		}
		else if (pchFallbackPower && pchFallbackPower[0])
		{
			entUsePower(bActive, pchFallbackPower);
		}
	}
}

// Gen function for executing all powers by category .
// if pchCategory == "" or NULL, then that indicates all categories
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ExecGenAllByCategory");
void exprExecGenAllByCategory(int bActive, const char* pchCategory)
{
	Entity *pEntity = entActivePlayerPtr();
	S32 i, iCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory);

	if (pEntity && pEntity->pChar)
	{
		for (i = 0; i < eaSize(&pEntity->pChar->ppPowers); ++i)
		{
			Power *pPow = pEntity->pChar->ppPowers[i];
			if (gclTrayPowerMatchesCategory(pPow, iCategory))
			{
				U32 uiPowID = pPow->pParentPower ? pPow->pParentPower->uiID : pPow->uiID;
				clientTarget_AddMultiPowerExec(uiPowID);
			}
		}
	}
}

// Gen function for executing all powers by category .
// if pchCategory == "" or NULL, then that indicates all categories
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ExecGenFirstByCategory");
void exprExecGenFirstByCategory(int bActive, const char* pchCategory)
{
	Entity *pEntity = entActivePlayerPtr();
	S32 iCategory = pchCategory && pchCategory[0] ? StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory) : -1;
	S32 i;

	if ( pEntity && pEntity->pChar )
	{
		for ( i = 0; i < eaSize(&pEntity->pChar->ppPowers); ++i )
		{
			Power *pPow = pEntity->pChar->ppPowers[i];
			PowerDef* pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;

			if ( pPowDef && (iCategory < 0 || eaiFind(&pPowDef->piCategories, iCategory) >= 0) )
			{
				Power* pPow2 = EntGetActivatedPower(pEntity,pPow,false,NULL);
				U32 uiPowID = pPow2 && pPow2->pParentPower ? pPow2->pParentPower->uiID : pPow2->uiID;
				if (pPow2)
				{
					clientTarget_AddMultiPowerExec(uiPowID);
					return;
				}
			}
		}
	}
}

static bool trayCanUseElem(SA_PARAM_OP_VALID Entity* pEntity, UITrayElem* pElem, S32 iCategory)
{
	if ( pEntity && iCategory >= 0 )
	{
		PowerDef* pPowDef = NULL;
		if ( pElem->bUseBaseReplacePower || pElem->pTrayElem==NULL )
			return false;

		if ( pElem->pTrayElem->eOwner==kTrayElemOwner_Self )
		{
			Power* pPow = EntTrayGetActivatedPower(pEntity,pElem->pTrayElem,false,NULL);
			pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;
		}
		else
		{
			Entity *ePet = entGetPrimaryPet(pEntity);
			Power* pPow = ePet ? EntTrayGetActivatedPower(ePet,pElem->pTrayElem,false,NULL) : NULL;
			pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;
		}

		if ( !pPowDef || eaiFind(&pPowDef->piCategories, iCategory) < 0 )
		{
			return false;
		}
	}

	return (	pElem && pElem->bActivatable
			&& !pElem->bActive && !pElem->bQueued && !pElem->bMaintaining
			&& !pElem->bInCooldown && !pElem->bCharging && !pElem->bRecharging );
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemCanUse");
bool exprTrayElemCanUse(SA_PARAM_OP_VALID UITrayElem* pElem)
{
	return trayCanUseElem( NULL, pElem, -1 );
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayGenCanUseAnyByCategory");
bool exprTrayGenCanUseAnyByCategory(SA_PARAM_OP_VALID UIGen* pGen, const char* pchCategory)
{
	Entity *pEntity = entActivePlayerPtr();
	UITrayElem*** peaElems = ui_GenGetManagedListSafe( pGen, UITrayElem );
	S32 iCategory = pchCategory && pchCategory[0] ? StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory) : -1;
	S32 i, iSize = eaSize(peaElems);

	if ( pEntity && pEntity->pChar && pGen && iSize > 0 )
	{
		for ( i = 0; i < iSize; i++ )
		{
			if ( trayCanUseElem(pEntity,(*peaElems)[i], iCategory) )
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenCanUseAnyByCategory");
bool exprGenCanUseAnyByCategory(const char* pchCategory)
{
	Entity *pEntity = entActivePlayerPtr();
	S32 iCategory = pchCategory && pchCategory[0] ? StaticDefineIntGetInt(PowerCategoriesEnum,pchCategory) : -1;
	S32 i;

	if ( pEntity && pEntity->pChar )
	{
		for ( i = 0; i < eaSize(&pEntity->pChar->ppPowers); ++i )
		{
			Power *pPow = pEntity->pChar->ppPowers[i];
			PowerDef* pPowDef = pPow ? GET_REF(pPow->hDef) : NULL;

			if ( pPowDef && (iCategory < 0 || eaiFind(&pPowDef->piCategories, iCategory) >= 0) )
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				Power* pPow2 = EntGetActivatedPower(pEntity,pPow,false,NULL);

				if (pPow2 && character_PayPowerCost(entGetPartitionIdx(pEntity), pEntity->pChar,pPow2,0,NULL,false,pExtract))
				{
					return true;
				}
			}
		}
	}
	return false;
}


// Toggles the AutoAttack-legal state of whatever is under the UITrayElem.  Does NOT
//  actually toggle AutoAttack itself on or off.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemToggleAutoAttack");
void exprTrayElemToggleAutoAttack(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if (e && e->pSaved && pelem && pelem->bValid && pelem->pTrayElem)
	{
		S32 iTrayIndex = -1;
		if (tray_CanEnableAutoAttackForElem(e, pelem->pTrayElem, &iTrayIndex))
		{
			switch (pelem->pTrayElem->eType)
			{
				xcase kTrayElemType_InventorySlot:
				{
					int iBag, iSlot, iPower;
					tray_InventorySlotStringToIDs(pelem->pTrayElem->pchIdentifier, &iBag, &iSlot, &iPower);

					// Toggle it on
					ServerCmd_AutoAttackElemCreateInventorySlot(iBag, iSlot, iPower);
				}
				xcase kTrayElemType_PowerTreeNode:
				{
					// Toggle it on
					ServerCmd_AutoAttackElemCreatePowerTreeNode(pelem->pTrayElem->pchIdentifier);
				}
			}
		}
		else if (iTrayIndex >= 0)
		{
			// Toggle it off
			ServerCmd_AutoAttackElemDestroy(iTrayIndex);
		}
	}
}

// Sets the override power to the tray's power if possible
// Will enable/disable auto attack as appropriate, and cancel any other auto attacks
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemToggleAutoAttackClient");
void exprTrayElemToggleAutoAttackClient(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if (e && e->pSaved && pelem && pelem->bValid && pelem->pTrayElem)
	{
		S32 iTrayIndex = -1;
		Power *pPower = entity_TrayGetPower(e, pelem->pTrayElem);
		if (pPower && tray_CanEnableAutoAttackForElem(e, pelem->pTrayElem, &iTrayIndex) && !gclAutoAttack_PowerIDAttacking(pPower->uiID))
		{
			gclAutoAttack_SetOverrideID(pPower->uiID);
			gclAutoAttack_DefaultAutoAttack(1);
		}
		else if (pPower)
		{
			gclAutoAttack_SetOverrideID(0);
			gclAutoAttack_DefaultAutoAttack(0);
		}
	}
}


// Gen functions for getting useful data about tray elements

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemGetTrayIndex");
S32 exprTrayElemGetTrayIndex(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	if ( pElem )
	{
		if ( pElem->pTrayElem && pElem->pTrayElem->iTray >= 0 )
		{
			return pElem->pTrayElem->iTray;
		}
		else if ( pElem->bReferencesTrayElem )
		{
			return pElem->iKey >> 8;
		}
	}

	return -1;
}

// The time in seconds the the UITrayElem has been activating.  Only valid if it is current.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeActivating");
F32 exprTrayElemTimeActivating(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem && pelem->bCurrent)
	{
		if(e->pChar && e->pChar->pPowActCurrent)
		{
			return e->pChar->pPowActCurrent->fTimeActivating;
		}
	}
	return -1.f;
}

// The time in seconds the the UITrayElem has charged to.  Only valid if it is current.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeCharged");
F32 exprTrayElemTimeCharged(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem && pelem->bCurrent)
	{
		if(e->pChar && e->pChar->pPowActCurrent)
		{
			return e->pChar->pPowActCurrent->fTimeCharged;
		}
	}
	return -1.f;
}

static void trayElemGetMaintainPowerInfo(	SA_PARAM_OP_VALID Entity *e, 
											SA_PARAM_OP_VALID UITrayElem *pelem, 
											SA_PARAM_NN_VALID PowerActivation **ppPowerActOut, 
											SA_PARAM_NN_VALID PowerDef **ppPowerDefOut)
{
	*ppPowerActOut = NULL;
	*ppPowerDefOut = NULL;

	if(e && e->pChar && pelem && pelem->pTrayElem)
	{
		if(pelem->bCurrent && e->pChar->pPowActCurrent)
		{
			*ppPowerDefOut = GET_REF(e->pChar->pPowActCurrent->hdef);
			*ppPowerActOut = e->pChar->pPowActCurrent;
		}
		else if(pelem->bActive)
		{
			Power *ppow = EntTrayGetActivatedPower(e, pelem->pTrayElem, false, NULL);
			int i = poweract_FindPowerInArray(&e->pChar->ppPowerActToggle, ppow);

			if(i>=0)
			{
				*ppPowerDefOut = GET_REF(e->pChar->ppPowerActToggle[i]->hdef);
				*ppPowerActOut = e->pChar->ppPowerActToggle[i];
			}
		}
	}
}

// The time in seconds the UITrayElem has been maintained.  Only valid if it is current or active.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemMaintainTimeRemaining");
F32 exprTrayElemMaintainTimeRemaining(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	PowerActivation *pPowerAct = NULL;
	PowerDef *pPowerDef = NULL;
	F32 fRet = -1.0f;

	trayElemGetMaintainPowerInfo(e, pelem, &pPowerAct, &pPowerDef);

	if (pPowerAct && pPowerDef)
	{
		fRet = pPowerDef->fTimeMaintain - pPowerAct->fTimeMaintained;
	}

	return fRet;
}

// The percent of time UITrayElem has been maintained.  Only valid if it is current or active.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemMaintainPercentTimeRemaining");
F32 exprTrayElemMaintainPercentTimeRemaining(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	PowerActivation *pPowerAct = NULL;
	PowerDef *pPowerDef = NULL;
	F32 fRet = -1.0f;

	trayElemGetMaintainPowerInfo(e, pelem, &pPowerAct, &pPowerDef);

	if (pPowerAct && pPowerDef)
	{
		fRet = 1.0f - pPowerAct->fTimeMaintained / pPowerDef->fTimeMaintain;
	}

	return fRet;
}

// The UITrayElem percentage left to be maintained.  Only valid if it is current or active.  Returns 0 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemMaintainPercentageRemaining");
F32 exprTrayElemMaintainPercentageRemaining(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	PowerActivation *pPowerAct = NULL;
	PowerDef *pPowerDef = NULL;
	
	trayElemGetMaintainPowerInfo(e, pelem, &pPowerAct, &pPowerDef);

	if (pPowerAct && pPowerDef && pPowerDef->fTimeMaintain > 0)
	{
		return pPowerAct->fTimeMaintained / pPowerDef->fTimeMaintain;
	}

	return 0.f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeModeEnabled");
F32 exprTrayElemTimeModeEnabled(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();

	if(e && pelem && pelem->bValid)
	{
		e = EntTrayGetExecutor(e,pelem->pTrayElem,pelem->pPetData);
		if(e && e->pChar)
		{
			Power *ppow = EntTrayGetActivatedPower(e,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
			if(ppow)
			{
				return character_GetModeEnabledTime(e->pChar,ppow,gConf.bSelfOnlyPowerModeTimers);
			}
		}
	}

	return 0.0f;
}

// The time in seconds the the UITrayElem has left to recharge.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeRecharge");
F32 exprTrayElemTimeRecharge(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *ePlayer = entActivePlayerPtr();
	Entity *e = ePlayer;
	if(e && pelem && pelem->bValid)
	{
		e = EntTrayGetExecutor(e,pelem->pTrayElem,pelem->pPetData);
		if (pelem->pPetData)
		{
			return pelem->pPetData->fTimerRecharge;
		}
		else if (e && e->pChar)
		{
			Power *ppow = EntTrayGetActivatedPower(e,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
			if(ppow)
			{
				if(!pelem->pTrayElem->erOwner)
				{
					F32 fResult = character_GetPowerRechargeEffective(entGetPartitionIdx(e), e->pChar,ppow);
					return fResult;
				}
				else
				{
					// Owned by a SavedPet, get the PetPowerState to find the recharge
					PowerDef *pdef = GET_REF(ppow->hDef);
					if(pdef)
					{
						PetPowerState *pState = player_GetPetPowerState(ePlayer->pPlayer,pelem->pTrayElem->erOwner,pdef);
						if(pState)
						{
							return pState->fTimerRecharge;
						}
					}

				}
			}
		}
	}

	return -1.f;
}

// The time in seconds the the UITrayElem has left to gain another charge.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeChargeRefill");
F32 exprTrayElemTimeChargeRefill(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *ePlayer = entActivePlayerPtr();
	Entity *e = ePlayer;
	if(e && pelem && pelem->bValid)
	{
		e = EntTrayGetExecutor(e,pelem->pTrayElem,pelem->pPetData);
		if (pelem->pPetData)
		{
			return pelem->pPetData->fTimerRecharge;
		}
		else if (e && e->pChar)
		{
			Power *ppow = EntTrayGetActivatedPower(e,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
			if(ppow)
			{
				return power_GetChargeRefillTime(ppow);
			}
		}
	}

	return -1.f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeCooldown");
F32 exprTrayElemTimeCooldown(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *ePlayer = entActivePlayerPtr();
	Entity *e = ePlayer;
	if(e && pelem && pelem->pTrayElem && pelem->bValid)
	{
		e = EntTrayGetExecutor(e,pelem->pTrayElem,pelem->pPetData);
		if(e && e->pChar)
		{
			Power *ppow = EntTrayGetActivatedPower(e,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
			if(ppow)
			{
				if (!pelem->pTrayElem->erOwner)
				{
					return character_GetCooldownFromPower(e->pChar,ppow);
				}
				else
				{
					return player_GetPetCooldownFromPower(ePlayer->pPlayer,pelem->pTrayElem->erOwner,ppow);
				}
			}
		}
	}

	return -1.f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeCooldownMax");
F32 exprTrayElemTimeCooldownMax(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem && pelem->pTrayElem && pelem->bValid)
	{
		e = EntTrayGetExecutor(e,pelem->pTrayElem,pelem->pPetData);
		if(e && e->pChar)
		{
			Power *ppow = EntTrayGetActivatedPower(e,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);
			if(ppow)
			{
				PowerDef* pDef = GET_REF(ppow->hDef);
				if(pDef)
				{
					return powerdef_GetCooldown(pDef);
				}
			}
		}
	}

	return -1.f;
}

// The time in seconds the the UITrayElem takes to recharge.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemTimeRechargeBase");
F32 exprTrayElemTimeRechargeBase(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem && pelem->bValid)
	{
		if (pelem->pPetData)
		{
			return pelem->pPetData->fTimerRechargeBase;
		}
		else if(e = EntTrayGetExecutor(e,pelem->pTrayElem,pelem->pPetData))
		{
			Power *ppow = EntTrayGetActivatedPower(e,pelem->pTrayElem,pelem->bUseBaseReplacePower,NULL);

			if (ppow)
			{
				return character_GetPowerRechargeBaseEffective(entGetPartitionIdx(e), e->pChar, ppow);
			}
		}
	}
	return -1.f;
}

// The time in seconds the the UITrayElem takes to recharge.  Returns -1 on failure.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayElemIsPowerStateSwitched");
bool exprTrayElemIsPowerStateSwitched(SA_PARAM_OP_VALID UITrayElem *pelem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pelem && pelem->bValid)
	{
		return pelem->bStateSwitchedPower;
	}
	return false;
}

// Returns the time to recharge on a given item, similar to how it would on a tray slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ItemTimeRecharge");
F32 exprItemTimeRecharge(SA_PARAM_OP_VALID Item *pItem)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		F32 fCooldown= 0.0f;
		S32 i;
		S32 iPowers = pItem ? item_GetNumItemPowerDefs(pItem, true) : 0;
		for(i=0; i<iPowers; i++)
		{
			Power *pPowItem = item_GetPower(pItem,i);
			if (pPowItem)
			{
				MAX1(fCooldown, character_GetPowerRechargeEffective(entGetPartitionIdx(e), e->pChar, pPowItem));
			}
		}
		return fCooldown;
	}
	return -1.f;
}

// Returns the time to recharge on a given item, similar to how it would on a tray slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ItemTimeRechargePct");
F32 exprItemTimeRechargePct(SA_PARAM_OP_VALID Item *pItem)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		F32 fCooldown= 0.0f;
		S32 i;
		S32 iPowers = pItem ? item_GetNumItemPowerDefs(pItem, true) : 0;
		for(i=0; i<iPowers; i++)
		{
			Power *pPowItem = item_GetPower(pItem,i);
			if (pPowItem)
			{
				F32 fCooldownBase = character_GetPowerRechargeBaseEffective(entGetPartitionIdx(e), e->pChar, pPowItem);
				if (fCooldownBase == 0)
					return 0.0f;
				MAX1(fCooldown, character_GetPowerRechargeEffective(entGetPartitionIdx(e), e->pChar, pPowItem)/fCooldownBase);
			}
		}
		return fCooldown;
	}
	return -1.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetMaxCategoryCooldownPct");
F32 exprItemGetMaxCategoryCooldownPct(SA_PARAM_OP_VALID Item *pItem)
{
	int i;
	F32 maxTime = 0.0;
	Entity *e = entActivePlayerPtr();
	PowerDef* pPowDef = NULL;
	CooldownTimer** ppTimers = e ? e->pChar->ppCooldownTimers : NULL;
	if(e && e->pChar)
	{
		S32 iPowers = pItem ? item_GetNumItemPowerDefs(pItem, true) : 0;
		for(i=0; i<iPowers; i++)
		{
			Power *pPowItem = item_GetPower(pItem,i);
			if (pPowItem)
			{
				pPowDef = GET_REF(pPowItem->hDef);
				break;
			}
		}
	}

	if (pPowDef)
	{
		for(i=0;i<eaSize(&ppTimers);i++)
		{
			if(eaiFind(&pPowDef->piCategories, ppTimers[i]->iPowerCategory) > -1)
			{
				maxTime = max(maxTime, ppTimers[i]->fCooldown/g_PowerCategories.ppCategories[ppTimers[i]->iPowerCategory]->fTimeCooldown);
			}
		}
	}
	return maxTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetMaxCategoryCooldown");
F32 exprItemGetMaxCategoryCooldown(SA_PARAM_OP_VALID Item *pItem)
{
	int i;
	F32 maxTime = 0.0;
	Entity *e = entActivePlayerPtr();
	PowerDef* pPowDef = NULL;
	CooldownTimer** ppTimers = e ? e->pChar->ppCooldownTimers : NULL;
	if(e && e->pChar)
	{
		S32 iPowers = pItem ? item_GetNumItemPowerDefs(pItem, true) : 0;
		for(i=0; i<iPowers; i++)
		{
			Power *pPowItem = item_GetPower(pItem,i);
			if (pPowItem)
			{
				pPowDef = GET_REF(pPowItem->hDef);
				break;
			}
		}
	}

	if (pPowDef)
	{
		for(i=0;i<eaSize(&ppTimers);i++)
		{
			if(eaiFind(&pPowDef->piCategories, ppTimers[i]->iPowerCategory) > -1)
			{
				maxTime = max(maxTime, ppTimers[i]->fCooldown);
			}
		}
	}
	return maxTime;
}

// Gets a power from the current active weapon
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetActiveWeaponPower");
SA_RET_OP_VALID Power* exprGetActiveWeaponPower(const char* pchBag, S32 iItemPowerIdx, const char* pchFallbackPower)
{
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Power* pPower = gclGetActiveWeaponPower(pEnt, pchBag, iItemPowerIdx, pExtract);
	if (!pPower && pEnt && pEnt->pChar)
	{
		pPower = character_FindPowerByName(pEnt->pChar, pchFallbackPower);
	}
	return pPower;
}

// Returns the current time to recharge on a given power, similar to how it would on a tray slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PowerGetRecharge");
F32 exprPowerGetRecharge(SA_PARAM_OP_VALID Power* pPower)
{
	Entity* pEnt = entActivePlayerPtr();
	F32 fResult = -1.0f;
	if (pPower && pEnt && pEnt->pChar)
	{
		fResult = character_GetPowerRechargeEffective(entGetPartitionIdx(pEnt), pEnt->pChar, pPower);
	}
	return fResult;
}

// Returns the default time to recharge on a given power, similar to how it would on a tray slot.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PowerGetRechargeBase");
F32 exprPowerGetRechargeBase(SA_PARAM_OP_VALID Power* pPower)
{
	PowerDef* pDef = pPower ? GET_REF(pPower->hDef) : NULL;
	if (pDef)
	{
		return powerdef_GetRechargeDefault(pDef);
	}
	return -1.0f;
}

// Returns the active cooldown time on a given power
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PowerGetCooldown");
F32 exprPowerGetCooldown(SA_PARAM_OP_VALID Power* pPower)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pPower && pEnt && pEnt->pChar)
	{
		return character_GetCooldownFromPower(pEnt->pChar, pPower);
	}
	return -1.0f;
}

// Return the hue of the given power ID, or use ID 0 to return the hue of the entity's first power.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerHue");
F32 exprEntGetPowerHue(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID)
{
	Power *pPower;
	if (!pEnt)
		return 0.f;
	if (uiPowerID == 0)
		pPower = eaGet(&pEnt->pChar->ppPowers, 0);
	else
		pPower = character_FindPowerByID(pEnt->pChar, uiPowerID);

	if(pPower)
	{
		if(pPower->fHue)
		{
			return pPower->fHue;
		}
		else
		{
			PowerDef *pdef = GET_REF(pPower->hDef);
			if(pdef)
			{
				return powerdef_GetHue(pdef);
			}
		}
	}
	return 0.f;
}

// Sets the hue of the given power ID
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntSetPowerHue");
void exprEntSetPowerHue(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID, F32 fHue)
{
	if (pEnt && uiPowerID)
	{
		ServerCmd_PowerHue(uiPowerID,fHue);
	}
}

// Returns if the given power ID can have its emit set by the player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntCanSetPowerHue");
S32 exprEntCanSetPowerHue(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID)
{
	S32 bRet = false;

	if(pEnt && pEnt->pChar && uiPowerID)
	{
		Power *ppow = character_FindPowerByID(pEnt->pChar,uiPowerID);
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		if(!GamePermission_EntHasToken(pEnt, GAME_PERMISSION_POWER_HUE) || !pdef)
		{
			return false;
		}

		bRet = true;
	}

	return bRet;
}

// Returns the translated display name of the emit of the given power ID
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerEmit");
const char *exprEntGetPowerEmit(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID)
{
	int i;
	DictionaryEArrayStruct *pDictEArrayStruct;

	if (pEnt && pEnt->pChar && uiPowerID)
	{
		Power *ppow = character_FindPowerByID(pEnt->pChar,uiPowerID);
		PowerEmit *pEmit = ppow ? power_GetEmit(ppow, pEnt->pChar) : NULL;
		if(pEmit)
		{
			return TranslateDisplayMessage(pEmit->msgDisplayName);
		}
	}

	pDictEArrayStruct = resDictGetEArrayStruct(g_hPowerEmitDict);
	for(i=0; i<eaSize(&pDictEArrayStruct->ppReferents); i++)
	{
		PowerEmit *pEmit = (PowerEmit*)pDictEArrayStruct->ppReferents[i];
		if(!eaSize(&pEmit->ppchBits))
		{
			return TranslateDisplayMessage(pEmit->msgDisplayName);
		}
	}

	return NULL;
}

// Returns if the given power ID can have its emit set by the player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntCanSetPowerEmit");
S32 exprEntCanSetPowerEmit(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID)
{
	S32 bRet = false;

	if(pEnt && pEnt->pChar && uiPowerID)
	{
		Power *ppow = character_FindPowerByID(pEnt->pChar,uiPowerID);
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		if(!GamePermission_EntHasToken(pEnt, GAME_PERMISSION_POWER_EMIT))
		{
			return false;
		}

		bRet = (pdef && powerdef_EmitCustomizable(pdef));
	}

	return bRet;
}

// Sets the emit of the given power ID
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntSetPowerEmit");
void exprEntSetPowerEmit(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID, const char *pchEmit)
{
	if (pEnt && uiPowerID)
	{
		ServerCmd_PowerEmit(uiPowerID,pchEmit);
	}
}

// Gets a list of all PowerEmits
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerEmitList");
void exprPowerEmitList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static PowerEmit **s_ppEmits = NULL;

	int i;
	DictionaryEArrayStruct *pDictEArrayStruct = resDictGetEArrayStruct(g_hPowerEmitDict);

	eaClearFast(&s_ppEmits);
	for(i=0; i<eaSize(&pDictEArrayStruct->ppReferents); i++)
	{
		eaPush(&s_ppEmits,pDictEArrayStruct->ppReferents[i]);
	}

	ui_GenSetManagedListSafe(pGen, &s_ppEmits, PowerEmit, false);
}



// Returns the translated display name of the EntCreateCostume of the given power ID
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPowerEntCreateCostume");
const char *exprEntGetPowerEntCreateCostume(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID)
{
	int iEntCreateCostume = 0;
	AttribModDef *pmoddef = NULL;

	if (pEnt && pEnt->pChar && uiPowerID)
	{
		Power *ppow = character_FindPowerByID(pEnt->pChar,uiPowerID);
		if(ppow)
		{
			PowerDef *pdef = GET_REF(ppow->hDef);

			if(pdef)
				pmoddef = powerdef_EntCreateCostumeCustomizable(pdef);

			if(ppow->iEntCreateCostume)
				iEntCreateCostume = ppow->iEntCreateCostume;
		}
	}

	if(pmoddef && iEntCreateCostume)
	{
		CritterDef *pdefCritter = GET_REF(((EntCreateParams*)pmoddef->pParams)->hCritter);
		if(pdefCritter)
		{
			CritterCostume *pCostume = eaIndexedGetUsingInt(&pdefCritter->ppCostume,iEntCreateCostume);
			if(pCostume)
				return TranslateDisplayMessage(pCostume->displayNameMsg);
		}
	}

	return TranslateMessageKey("PowerEntCreateCostume_Default");
}

// Returns if the given power ID can have its EntCreateCostume set by the player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntCanSetPowerEntCreateCostume");
S32 exprEntCanSetPowerEntCreateCostume(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID)
{
	S32 bRet = false;

	if(pEnt && pEnt->pChar && uiPowerID)
	{

		Power *ppow = character_FindPowerByID(pEnt->pChar,uiPowerID);
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;

		if(!GamePermission_EntHasToken(pEnt, GAME_PERMISSION_POWER_PET_COSTUME))
		{
			return false;
		}

		bRet = (pdef && powerdef_EntCreateCostumeCustomizable(pdef));
	}

	return bRet;
}

// Sets the EntCreateCostume of the given power ID
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntSetPowerEntCreateCostume");
void exprEntSetPowerEntCreateCostume(SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID, S32 iEntCreateCostume)
{
	if (pEnt && uiPowerID)
	{
		ServerCmd_PowerEntCreateCostume(uiPowerID,iEntCreateCostume);
	}
}

// Gets a list of all PowerEntCreateCostumes for the given power ID
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerEntCreateCostumeList");
void exprPowerEntCreateCostumeList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, U32 uiPowerID)
{
	PooledStringWrapper ***pppStrings = ui_GenGetManagedListSafe(pGen, PooledStringWrapper);
	int iSize = 0;

	if (pEnt && pEnt->pChar && uiPowerID)
	{
		Power *ppow = character_FindPowerByID(pEnt->pChar,uiPowerID);
		if(ppow)
		{
			PowerDef *pdef = GET_REF(ppow->hDef);
			if(pdef)
			{
				AttribModDef *pmoddef = powerdef_EntCreateCostumeCustomizable(pdef);
				if(pmoddef)
				{
					int i;
					CritterDef *pdefCritter = GET_REF(((EntCreateParams*)pmoddef->pParams)->hCritter);
					CritterCostume **ppCostume = NULL;

					PooledStringWrapper *pString = eaGetStruct(pppStrings,parse_PooledStringWrapper,0);
					pString->cpchString = TranslateMessageKey("PowerEntCreateCostume_Default");
					iSize++;

					critterdef_CostumeSort(pdefCritter,&ppCostume);

					for(i=0; i<eaSize(&ppCostume); i++)
					{
						CritterCostume *pCostume = ppCostume[i];
						if(pCostume)
						{
							pString = eaGetStruct(pppStrings,parse_PooledStringWrapper,iSize);
							pString->cpchString = TranslateDisplayMessage(pCostume->displayNameMsg);
							iSize++;
						}
					}

					eaDestroy(&ppCostume);
				}
			}
		}
	}

	eaSetSizeStruct(pppStrings,parse_PooledStringWrapper,iSize);
	ui_GenSetManagedListSafe(pGen, pppStrings, PooledStringWrapper, true);
}



// Returns the number of slots with a given type in your tray
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayNumSlotsWithReqs");
S32 exprTrayNumSlotsWithReq(SA_PARAM_OP_VALID Entity *pEntity, const char* pchSlotRequirements)
{
	S32 count = 0;
	if (pchSlotRequirements && pEntity && pEntity->pChar)
	{
		int i, j, size = character_PowerSlotCount(pEntity->pChar);

		char *pchCopy;
		char *pchContext;
		char *pchStart;

		strdup_alloca(pchCopy, pchSlotRequirements);
		pchStart = strtok_r(pchCopy, " ,\t\r\n", &pchContext);
		do
		{
			S32 iReq = StaticDefineIntGetInt(PowerCategoriesEnum, pchStart);
			if (iReq < 0) continue;

			for (i = 0; i < size; i++)
			{
				PowerSlot *pSlot = character_PowerSlotGetPowerSlot(pEntity->pChar, i);

				if (pSlot)
				{
					for (j = 0; j < eaiSize(&pSlot->peRequires); j++)
					{
						if (pSlot->peRequires[j] == iReq)
						{
							count++;
							break;
						}
					}
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
	return count;
}

// Returns the number of slots with a given type in your tray
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TrayNumSlotsWithReqsByIndex");
S32 exprTrayNumSlotsWithReqByIndex(SA_PARAM_OP_VALID Entity *pEntity, U32 iTray, const char* pchSlotRequirements)
{
	S32 count = 0;
	if (pchSlotRequirements && pEntity && pEntity->pChar)
	{
		int i, j, size = character_PowerSlotCountInTray(pEntity->pChar, iTray);

		char *pchCopy;
		char *pchContext;
		char *pchStart;

		strdup_alloca(pchCopy, pchSlotRequirements);
		pchStart = strtok_r(pchCopy, " ,\t\r\n", &pchContext);
		do
		{
			S32 iReq = StaticDefineIntGetInt(PowerCategoriesEnum, pchStart);
			if (iReq < 0) continue;

			for (i = 0; i < size; i++)
			{
				PowerSlot *pSlot = CharacterGetPowerSlotInTrayAtIndex(pEntity->pChar, iTray, i);

				if (pSlot)
				{
					for (j = 0; j < eaiSize(&pSlot->peRequires); j++)
					{
						if (pSlot->peRequires[j] == iReq)
						{
							count++;
							break;
						}
					}
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
	return count;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntIsTargetInRange);
bool exprEntIsTargetInRange(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID PowerDef *pPowerDef)
{
	Character *pChar = pPlayer ? pPlayer->pChar : NULL;
	Power *pPower = NULL;
	Entity *pEntTarget;
	WorldInteractionNode *pNodeTarget;
	PowerDef *pOwnedDef;
	int i, j;

	if (!pPlayer || !pChar || !pPowerDef) {
		return false;
	}

	for(i = eaSize(&pChar->ppPowers) - 1; i >= 0; i--)
	{
		pOwnedDef = GET_REF(pChar->ppPowers[i]->hDef);
		if(pPowerDef == pOwnedDef)
		{
			pPower = pChar->ppPowers[i];
			break;
		}
		else if (pOwnedDef && pOwnedDef->eType == kPowerType_Combo)
		{
			for (j = eaSize(&pOwnedDef->ppCombos) - 1; j >= 0; j--)
			{
				if (pPowerDef == GET_REF(pOwnedDef->ppCombos[j]->hPower))
				{
					pPower = pChar->ppPowers[i];
					break;
				}
			}

			// Found the power?
			if (pPower) {
				break;
			}
		}
	}

	if (!pPower) {
		return false;
	}

	EntPowerGetTarget(pPlayer->pChar, pPower, &pEntTarget, &pNodeTarget);
	return character_TargetInPowerRange(pPlayer->pChar, pPower, GET_REF(pPower->hDef), pEntTarget, pNodeTarget);
}

AUTO_EXPR_FUNC(UIGen);
U32 UITrayGetID(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	U32 ret = -1;
	if (pElem && pElem->pTrayElem)
	{
		ret = pElem->pTrayElem->lIdentifier;
	}
	return ret;
}

AUTO_EXPR_FUNC(UIGen);
SA_RET_OP_VALID Power* UITrayGetPower(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pElem && pElem->pTrayElem)
	{
		return entity_TrayGetPower(pEnt,pElem->pTrayElem);
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen);
bool isUITrayAPower(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	if (pElem && pElem->pTrayElem)
	{
		if ( pElem->pTrayElem->eType == kTrayElemType_Power
			|| pElem->pTrayElem->eType == kTrayElemType_TempPower)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen);
bool TrayElemMatchesType(SA_PARAM_OP_VALID UITrayElem *pElem, ACMD_EXPR_ENUM(TrayElemType) const char* pchType)
{
	if ( pElem && pElem->pTrayElem )
	{
		TrayElemType eType = StaticDefineIntGetInt(TrayElemTypeEnum, pchType);

		return ( pElem->pTrayElem->eType == eType );
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen);
bool TrayInventoryElemMatchesBag(SA_PARAM_OP_VALID UITrayElem *pElem, ACMD_EXPR_ENUM(InvBagIDs) const char* pchBag)
{
	if ( pElem && pElem->pTrayElem && pElem->pTrayElem->eType == kTrayElemType_InventorySlot )
	{
		InvBagIDs eBagID = StaticDefineIntGetInt(InvBagIDsEnum, pchBag);
		S32 iTrayBagID = 0, iTrayBagSlot = 0, iTrayItemPowIdx = 0;
		const char* pchString = pElem->pTrayElem->pchIdentifier;
		tray_InventorySlotStringToIDs(pchString, &iTrayBagID, &iTrayBagSlot, &iTrayItemPowIdx);

		return ( iTrayBagID == eBagID );
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen);
bool TrayPowerNodeElemMatchesNode(SA_PARAM_OP_VALID UITrayElem *pElem, const char* pchNode)
{
	if ( pElem && pElem->pTrayElem && pElem->pTrayElem->eType == kTrayElemType_PowerTreeNode )
	{
		const char* pchPooledNode = allocAddString(pchNode);
		const char* pchTrayNode = allocAddString(pElem->pTrayElem->pchIdentifier);

		return ( stricmp(pchTrayNode,pchPooledNode)==0 );
	}
	return false;
}

//The following Power expressions should probably be moved to a different file
AUTO_EXPR_FUNC(UIGen);
SA_RET_OP_VALID Power* PlayerPowerGetByName( const char* pchName )
{
	Entity *pEnt = entActivePlayerPtr();

	return pEnt ? character_FindPowerByName( pEnt->pChar, pchName ) : NULL;
}

AUTO_EXPR_FUNC(UIGen);
bool PlayerPowerActive(SA_PARAM_OP_VALID Power* pPow)
{
	Power *pPowBase = pPow && pPow->pParentPower ? pPow->pParentPower : pPow;

	return pPowBase ? pPowBase->bActive : false;
}

AUTO_EXPR_FUNC(UIGen);
F32 PlayerPowerTargetArc(SA_PARAM_OP_VALID Power* pPow)
{
	PowerDef *pDef = pPow ? GET_REF(pPow->hDef) : NULL;

	return pDef ? pDef->fTargetArc : 0;
}

AUTO_EXPR_FUNC(UIGen);
bool PlayerPowerIsYawFacing(SA_PARAM_OP_VALID Power* pPow, F32 fDegrees)
{
	if ( pPow )
	{
		F32 fSlop = 1.0f;
		F32 fYaw = DEG(pPow->fYaw);
		return fYaw > fDegrees-fSlop && fYaw < fDegrees+fSlop;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen);
F32 PlayerPowerYaw(SA_PARAM_OP_VALID Power *pPow)
{
	return pPow ? DEG(pPow->fYaw) : 0;
}

//This is a hack to get clamped degrees for arc FX
AUTO_EXPR_FUNC(UIGen);
S32 PlayerGetValidArcFXAngle(F32 fArc)
{
	S32 i, iCount = 5;
	S32 piArcs[] = { 45, 90, 180, 250, 360 };
	F32 fEpsilon = 0.001f;

	for ( i = 0; i < iCount; i++ )
	{
		if ( fArc < piArcs[i]+fEpsilon )
		{
			if ( i > 0 && i + 1 < iCount )
			{
				F32 fMidpoint = (piArcs[i] + piArcs[i+1])*0.5f;

				return ( fArc < fMidpoint+fEpsilon ) ? piArcs[i] : piArcs[i+1];
			}
			else
			{
				return piArcs[i];
			}
		}
	}

	return piArcs[iCount-1];
}

// Attempt to struggle out of a power
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface, Powers) ACMD_PRODUCTS(StarTrek);
void PowerExecStruggleIfHeld(bool bActive)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar && (character_IsHeld(pEnt->pChar) || character_IsDisabled(pEnt->pChar)) )
	{
		Power *pStruggle = character_FindPowerByCategory(pEnt->pChar,"Struggle");
		if (pStruggle)
			entUsePowerID(bActive, pStruggle->uiID);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetPowerAtSlotIndex);
const char * gclExprEntGetPowerAtSlotIndex(ExprContext *pContext, int iTrayIndex, int iSlotIndex)
{
	PowerDef * pDef = NULL;
	Entity* pEnt = entActivePlayerPtr();
	if( pEnt )
	{
		Character* pChar = pEnt->pChar;
		U32 uiID = character_PowerSlotGetFromTray(pChar, iTrayIndex, iSlotIndex);
		Power* pPower = character_FindPowerByID(pChar, uiID);
		pDef = SAFE_GET_REF(pPower, hDef);
	}
	return pDef ? pDef->pchName : "";
}

AUTO_EXPR_FUNC(UIGen);
S32 TrayElemGetChargesRemaining(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	Entity *e = entActivePlayerPtr();
	if(e && pElem && pElem->bValid)
	{
		if (!pElem->pTrayElem)
		{
			Errorf("TrayElemGetChargesRemaining : invalid element");
			return -1;
		}
		e = EntTrayGetExecutor(e,pElem->pTrayElem,pElem->pPetData);
		if(e && e->pChar)
		{
			Power *pPow = entity_TrayGetPower(e,pElem->pTrayElem);
			if(pPow)
			{
				PowerDef* pDef = GET_REF(pPow->hDef);

				if (pDef)
				{
					S32 iChargesLeft = pDef->iCharges - power_GetChargesUsed(pPow);

					if (pPow->eSource == kPowerSource_Item)
					{
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
						S32 iStackCount = PowerGetStackCount(e, pPow, &pElem->iLastStackedItemCount, &pElem->uiNextStackedItemUpdate, pExtract);

						if (iStackCount > 1)
						{
							iChargesLeft += (iStackCount-1) * pDef->iCharges;
						}
					}

					return iChargesLeft;
				}
			}
		}
	}

	return -1;
}

AUTO_EXPR_FUNC(UIGen);
bool TrayElemHasLifetimeUsage(SA_PARAM_OP_VALID UITrayElem* pElem)
{
	Entity* e = entActivePlayerPtr();
	if (e && pElem && pElem->bValid)
	{
		e = EntTrayGetExecutor(e, pElem->pTrayElem, pElem->pPetData);
		if (e && e->pChar)
		{
			PowerDef* pDef = entity_TrayGetPowerDef(e, pElem->pTrayElem);
			if (pDef)
			{
				return pDef->fLifetimeUsage > FLT_EPSILON;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen);
S32 TrayElemGetLifetimeUsageLeft(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	Entity *e = entActivePlayerPtr();
	if (e && pElem && pElem->bValid)
	{
		e = EntTrayGetExecutor(e,pElem->pTrayElem,pElem->pPetData);
		if (e && e->pChar)
		{
			Power *pPow = entity_TrayGetPower(e,pElem->pTrayElem);
			if (pPow)
			{
				return (S32)power_GetLifetimeUsageLeft(pPow);
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen);
bool CanExecPower(SA_PARAM_OP_VALID Power* pPower)
{
	Entity *e = entActivePlayerPtr();
	bool bResult = false;
	if(e && e->pChar && pPower)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		bResult = EntIsPowerTrayActivatable(e->pChar, pPower, NULL, pExtract);	
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen);
bool CanExecPowerCategory(const char *cpchCategory)
{
	Entity *e = entActivePlayerPtr();
	bool bResult = false;
	if(e && e->pChar)
	{
		Power *ppow = character_FindPowerByCategory(e->pChar,cpchCategory);
		if(ppow)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			bResult = EntIsPowerTrayActivatable(e->pChar, ppow, NULL, pExtract);
		}
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen);
bool CanExecPowerCategoryOnTarget(const char *cpchCategory, S32 entRef)
{
	Entity *e = entActivePlayerPtr();
	Entity* pTarget = entFromEntityRefAnyPartition(entRef);
	bool bResult = false;
	if(e && e->pChar)
	{
		Power *ppow = character_FindPowerByCategory(e->pChar,cpchCategory);
		if(ppow)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			bResult = EntIsPowerTrayActivatable(e->pChar, ppow, pTarget, pExtract);
		}
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen);
void CancelQueuedPower(void)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pChar)
	{
		U8 uchActCanceled = character_ActQueuedCancel(entGetPartitionIdx(e), e->pChar, NULL, 0);
		if(uchActCanceled)
			ServerCmd_PowersActCancelServer(uchActCanceled, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen);
F32 GetActivationPercentRemaining(void)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pChar && e->pChar->pPowActCurrent && !e->pChar->pPowActCurrent->uiPeriod)
	{
		return e->pChar->pPowActCurrent->fTimerActivate / (e->pChar->pPowActCurrent->fTimerActivate + e->pChar->pPowActCurrent->fTimeActivating);
	}
	return 0;
}


AUTO_EXPR_FUNC(UIGen);
F32 UITrayElemGetMaxCategoryCooldownPercent(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	int i;
	F32 maxTime = 0.0;
	Entity *pSource = entActivePlayerPtr();
	PowerDef *ppowdef = (pElem && pElem->pTrayElem && pSource) ? entity_TrayGetPowerDef(pSource, pElem->pTrayElem) : NULL;
	CooldownTimer** ppTimers = pSource ? pSource->pChar->ppCooldownTimers : NULL;

	if (ppowdef)
	{
		for(i=0;i<eaSize(&ppTimers);i++)
		{
			if(eaiFind(&ppowdef->piCategories, ppTimers[i]->iPowerCategory) > -1)
			{
				maxTime = max(maxTime, ppTimers[i]->fCooldown/g_PowerCategories.ppCategories[ppTimers[i]->iPowerCategory]->fTimeCooldown);
			}
		}
	}
	return maxTime;
}

AUTO_EXPR_FUNC(UIGen);
bool UITrayElemIsSubjectToGlobalCooldown(SA_PARAM_OP_VALID UITrayElem *pElem)
{
	Entity *pSource = entActivePlayerPtr();
	if (pSource && pElem->pTrayElem)
	{
		PowerDef *ppowdef = entity_TrayGetPowerDef(pSource, pElem->pTrayElem);
		if (ppowdef && ppowdef->bCooldownGlobalNotChecked)
			return false;
	}
	return true;
}

AUTO_EXPR_FUNC(UIGen);
F32 GetGlobalCooldownPercentRemaining(void)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pChar && g_CombatConfig.fCooldownGlobal > 0)
	{
		return e->pChar->fCooldownGlobalTimer / g_CombatConfig.fCooldownGlobal;
	}
	return 0;
}

// Return whether the power executed by this tray element could target this entity,
// assuming it was fully recharged, within range, and so on.
AUTO_EXPR_FUNC(UIGen);
bool UITrayElemIsValidTargetType(SA_PARAM_OP_VALID UITrayElem *pElem, SA_PARAM_OP_VALID Entity *pTarget)
{
	Entity *pSource = (pElem && pElem->pTrayElem) ? entFromEntityRefAnyPartition(pElem->pTrayElem->erOwner) : NULL;
	if (pSource && pTarget)
	{
		PowerDef *ppowdef = entity_TrayGetPowerDef(pSource, pElem->pTrayElem);
		PowerTarget *pTargetType = ppowdef ? GET_REF(ppowdef->hTargetMain) : NULL;
		if (pTargetType)
			return character_TargetMatchesPowerType(PARTITION_CLIENT,pSource->pChar, pTarget->pChar, pTargetType);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsTrayLocked);
bool UITrayExprIsTrayLocked(void)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer)
	{
		return pEnt->pPlayer->pUI->pLooseUI->bLockTray;
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(TrayHideTrayTooltips);
bool exprTrayShowTrayTooltips(ExprContext *pContext)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	return SAFE_MEMBER(pHUDOptions, bHideTrayTooltips);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GetExploitIcon);
const char *exprGetExploitIcon(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pentTarget)
{
	// Creatures method - Find Exploit-flagged mods with requires and see if they're legal
	int i, j;
	Entity *pent = entActivePlayerPtr();

	if(pent && pentTarget && character_TargetIsFoe(PARTITION_CLIENT,pent->pChar,pentTarget->pChar))
	{
		for(i=eaSize(&pent->pChar->ppPowers)-1; i>=0; i--)
		{
			Power *ppow = pent->pChar->ppPowers[i];
			PowerDef *ppowdef = GET_REF(ppow->hDef);

			if(ppowdef==NULL)
				continue;

			for(j=eaSize(&ppowdef->ppOrderedMods)-1; j>=0; j--)
			{
				AttribModDef *pmoddef = ppowdef->ppOrderedMods[j];
				if(pmoddef->eFlags & kCombatTrackerFlag_Exploit && pmoddef->pExprRequires)
				{
					F32 fRequires;
					combateval_ContextSetupApply(pent->pChar, pentTarget->pChar,NULL, NULL);
					fRequires = combateval_EvalNew(PARTITION_CLIENT,pmoddef->pExprRequires, kCombatEvalContext_Apply, NULL);
					if(fRequires)
					{
						return ppowdef->pchIconName;
					}
				}
			}
		}
	}

	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntTrayGetItemComparisonPowerReplace);
SA_RET_OP_VALID Power* exprEntTrayGetItemComparisonPowerReplace(SA_PARAM_OP_VALID Entity* pEntity,
																SA_PARAM_OP_VALID UITrayElem* pElem,
																S32 iBagID,
																S32 iBagSlot)
{
	if (pEntity && pEntity->pChar && pElem && pElem->pTrayElem)
	{
		Item* pItem = NULL;
		Power* pPower = NULL;
		PowerReplaceDef* pPowerReplaceDef = NULL;

		// Get the PowerReplaceDef to compare to
		switch (pElem->pTrayElem->eType)
		{
			xcase kTrayElemType_PowerTreeNode:
			{
				PTNodeDef* pNodeDef = powertreenodedef_Find(pElem->pTrayElem->pchIdentifier);
				if (pNodeDef)
				{
					pPowerReplaceDef = GET_REF(pNodeDef->hGrantSlot);
				}
			}
			xcase kTrayElemType_InventorySlot:
			{
				ItemPowerDef* pItemPowerDef;
				S32 iBag = -1, iSlot = -1, iItemPowIdx = -1;
				tray_InventorySlotStringToIDs(pElem->pTrayElem->pchIdentifier,&iBag,&iSlot,&iItemPowIdx);
				pItemPowerDef = item_GetItemPowerDef(pElem->pSourceItem, iItemPowIdx);
				if (pItemPowerDef)
				{
					pPowerReplaceDef = GET_REF(pItemPowerDef->hPowerReplace);
				}
			}
		}

		// Find the matching PowerReplaceDef on the item
		if (pPowerReplaceDef)
		{
			S32 i;
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
			pItem = inv_GetItemFromBag(pEntity,iBagID,iBagSlot,pExtract);

			for (i = item_GetNumItemPowerDefs(pItem, true)-1; i >= 0; i--)
			{
				ItemPowerDef* pItemPowerDef = item_GetItemPowerDef(pItem, i);

				if (!pItemPowerDef)
					continue;

				if (pPowerReplaceDef == GET_REF(pItemPowerDef->hPowerReplace))
				{
					pPower = item_GetPower(pItem, i);
					break;
				}
			}
		}

		// If there is no item in the specified slot, get the base power
		if (!pItem && pElem->pSourceItem)
		{
			switch (pElem->pTrayElem->eType)
			{
				xcase kTrayElemType_PowerTreeNode:
				{
					if (pPowerReplaceDef)
					{
						pPower = entity_TrayGetPower(pEntity, pElem->pTrayElem);
					}
				}
				xcase kTrayElemType_InventorySlot:
				{
					PowerReplace* pReplace = Entity_FindPowerReplace(pEntity, pPowerReplaceDef);
					if (pReplace)
					{
						pPower = character_FindPowerByIDComplete(pEntity->pChar, pReplace->uiBasePowerID);
					}
				}
			}
		}
		return pPower;
	}
	return NULL;
}

//Whether this Build elem can catch the dragged element...
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntCanModifyPowerTray");
bool exprEntCanModifyPowerTray(SA_PARAM_OP_VALID Entity *pEntity)
{
	return pEntity && pEntity->pChar && character_CanModifyPowerTray(pEntity->pChar, -1, -1, false);
}

//Whether this Build elem can catch the dragged element...
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("UITrayElemInjectStates");
void exprUITrayElemInjectStates(SA_PARAM_OP_VALID UIGen *pGen)
{
	if (pGen)
	{
		UIGen ***peaInstances = ui_GenGetInstances(pGen);
		UITrayElem ***peaData = ui_GenGetManagedListSafe(pGen, UITrayElem);
		int i;
		if (eaSize(peaInstances) != eaSize(peaData))
			return;
		for (i = 0; i < eaSize(peaInstances); ++i)
		{
			UIGen *pChild = (*peaInstances)[i];
			UITrayElem *pElem = (*peaData)[i];
			bool bHasIcon = pElem->pchIcon && pElem->pchIcon[0];
			ui_GenStates(pChild,
				kUIGenStateTrayElemRefillingCharges,       pElem->bRefillingCharges,
				kUIGenStateTrayElemMaintainTimeRemaining,  exprTrayElemMaintainTimeRemaining(pElem) > 0,
				kUIGenStateTrayElemNotActivatable,         !pElem->bActivatable && bHasIcon,
				kUIGenStateTrayElemHasCharges,             pElem->iNumTotalCharges > 0,
				kUIGenStateTrayElemNoChargesRemaining,     (pElem->iNumTotalCharges > 0) && (TrayElemGetChargesRemaining(pElem) <= 0),
				kUIGenStateTrayElemCooldown,               (pElem->bInCooldown || pElem->bRecharging) && bHasIcon,
				kUIGenStateTrayElemActive,                 pElem->bCurrent || pElem->bActive,
				kUIGenStateTrayElemAutoActivate,           pElem->bAutoActivating,
				kUIGenStateTrayElemEmpty,				   !bHasIcon && !pElem->bLocked,
				kUIGenStateTrayElemLocked,                 pElem->bLocked,
				kUIGenStateTrayElemGlobalCooldown,
					!(pElem->bCurrent || pElem->bActive)
					&& pElem->bActivatable
					&& (pElem->bRecharging
						|| (UITrayElemIsSubjectToGlobalCooldown(pElem) && GetGlobalCooldownPercentRemaining() > 0)
						|| (UITrayElemGetMaxCategoryCooldownPercent(pElem) > 0)),
				kUIGenStateNone);
		}
	}
}

#include "AutoGen/UITray_h_ast.c"
#include "AutoGen/UITray_c_ast.c"
