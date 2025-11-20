#include "gclWarp.h"

#include "gclEntity.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "MapDescription.h"
#include "Team.h"
#include "UIGen.h"
#include "WorldGrid.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

static U64 s_WarpItemId = 0;
static ItemWarpType s_WarpType = 0;

bool gclWarp_StartItemWarp(Item *pItem, ItemDef *pItemDef)
{
	if(!pItemDef)
	{
		pItemDef = GET_REF(pItem->hItem);
		if(!pItemDef || !pItemDef->pWarp)
			return false;
	}

	s_WarpItemId = 0;
	s_WarpType = 0;

	switch(pItemDef->pWarp->eWarpType)
	{
	case kItemWarp_SelfToMapSpawn:
	case kItemWarp_TeamToMapSpawn:
	case kItemWarp_TeamToSelf:
		{
			UIGen *pGen = ui_GenFind("Warp_Confirm", kUIGenTypeNone);
			if(pGen)
			{
				ui_GenSendMessage(pGen, "Show");
				s_WarpItemId = pItem->id;
				s_WarpType = pItemDef->pWarp->eWarpType;
				return true;
			}
			return true;
		}
		break;
	case kItemWarp_SelfToTarget:
		{
			UIGen *pGen = ui_GenFind("WarpTarget_Confirm", kUIGenTypeNone);
			if(pGen)
			{
				ui_GenSendMessage(pGen, "Show");
				s_WarpItemId = pItem->id;
				s_WarpType = pItemDef->pWarp->eWarpType;
				return true;
			}
		}

		break;
	default: 
		break;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ConfirmItemWarp);
void gclWarp_expr_ConfirmWarp(U64 uiItemId, ItemWarpType eType)
{
	if(uiItemId == s_WarpItemId && eType == s_WarpType)
	{
		Entity *pEnt = entActivePlayerPtr();
		Item *pItem = inv_GetItemByID(pEnt, s_WarpItemId);
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		if(!pItem
			|| !pItemDef
			|| !pItemDef->pWarp
			|| (ItemWarpType)pItemDef->pWarp->eWarpType != eType
			|| (ItemWarpType)pItemDef->pWarp->eWarpType == kItemWarp_SelfToTarget)
			return;
		
		ServerCmd_ItemWarp(uiItemId, 0);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ConfirmItemWarpTarget);
void gclWarp_expr_ConfirmWarpTarget(U64 uiItemId, ItemWarpType eType, U32 iEntTarget)
{
	if(uiItemId == s_WarpItemId && eType == s_WarpType)
	{
		Entity *pEnt = entActivePlayerPtr();
		Item *pItem = inv_GetItemByID(pEnt, s_WarpItemId);
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		if(!pItem
			|| !pItemDef
			|| !pItemDef->pWarp
			|| (ItemWarpType)pItemDef->pWarp->eWarpType != eType
			|| (ItemWarpType)pItemDef->pWarp->eWarpType != kItemWarp_SelfToTarget)
			return;

		ServerCmd_ItemWarp(uiItemId, iEntTarget);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemWarp_GetItem);
SA_RET_OP_VALID Item *gclWarp_expr_GetItem(void)
{
	static Item *s_pItem = NULL;
	Entity *pEnt = entActivePlayerPtr();
	if(s_WarpItemId && pEnt)
	{
		s_pItem = inv_GetItemByID(pEnt, s_WarpItemId);
	}
	else
		s_pItem = NULL;

	return s_pItem;
};

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemWarp_GetType);
int gclWarp_expr_GetType(void)
{
	if(s_WarpItemId)
		return s_WarpType;
	else
		return kItemWarp_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemWarp_GetLocation);
const char *gclWarp_expr_GetLocation()
{
	Entity *pEnt = entActivePlayerPtr();
	Item *pItem = inv_GetItemByID(pEnt, s_WarpItemId);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	ItemDefWarp *pWarp = pItemDef ? pItemDef->pWarp : NULL;
	if(pItem && pItemDef && pWarp && (pWarp->eWarpType == kItemWarp_SelfToMapSpawn || pWarp->eWarpType == kItemWarp_TeamToMapSpawn) )
	{
		if(pWarp->pchMap)
		{
			ZoneMapInfo *pInfo = zmapInfoGetByPublicName(pWarp->pchMap);
			if(pInfo)
			{
				return TranslateMessagePtrSafe(zmapInfoGetDisplayNameMessagePtr(pInfo), "[Unknown Location]");
			}
		}
		else if(pWarp->pchSpawn)
		{
			if(!stricmp(pWarp->pchSpawn, SPAWN_AT_NEAR_RESPAWN))
				return TranslateMessageKeyDefault("Warp_Confirm_ClosestRespawnPointOnMap", "[Unknown Location on map]");
			else if(!stricmp(pWarp->pchSpawn, START_SPAWN))
				return TranslateMessageKeyDefault("Warp_Confirm_StartSpawnOnMap", "[Unknown Location on map]");
			else
				return TranslateMessageKeyDefault("Warp_Confirm_UnknownSpawnPointOnMap", "[Unknown Location on map]");
		}
	}

	 return "[Unknown Location]";
}

bool gclWarp_CanExec(Entity *pEntity, Item *pItem, ItemDef *pItemDef)
{
	bool bActivatable = true;
	
	if(!pEntity || !pItemDef || !pItem || !pItemDef->pWarp)
		return false;

	switch(pItemDef->pWarp->eWarpType)
	{
	case kItemWarp_SelfToMapSpawn:
		{
			if(!pItemDef->pWarp->bCanMapMove 
				&& pItemDef->pWarp->pchMap
				&& stricmp(pItemDef->pWarp->pchMap, zmapInfoGetPublicName(NULL)))
				bActivatable = false;
		}
		break;
	case kItemWarp_SelfToTarget:
		{
			Team *pTeam = team_GetTeam(pEntity);
			if(!pTeam)
			{
				bActivatable = false;
			}

			//If this can't map move, look for teammates on the map
			if(!pItemDef->pWarp->bCanMapMove)
			{
				int iIdx;
				int *piTargets = NULL;
				team_GetOnMapEntIds(entGetPartitionIdx(pEntity), &piTargets, pTeam);
				for(iIdx = eaiSize(&piTargets);iIdx >=0; iIdx--)
				{
					if((ContainerID)piTargets[iIdx] != entGetContainerID(pEntity))
						break;
				}

				if(iIdx < 0)
					bActivatable = false;
				eaiDestroy(&piTargets);
			}
		}
		break;
	case kItemWarp_TeamToSelf:
		{
			Team *pTeam = team_GetTeam(pEntity);
			if(!pTeam)
			{
				bActivatable = false;
			}
			//If this can't map move, look for teammates on the map
			if(!pItemDef->pWarp->bCanMapMove)
			{
				int iIdx;
				int *piTargets = NULL;
				team_GetOnMapEntIds(entGetPartitionIdx(pEntity), &piTargets, pTeam);
				for(iIdx = eaiSize(&piTargets);iIdx >=0; iIdx--)
				{
					if((ContainerID)piTargets[iIdx] != entGetContainerID(pEntity))
						break;
				}

				if(iIdx < 0)
					bActivatable = false;
				eaiDestroy(&piTargets);
			}
		}
		break;
	case kItemWarp_TeamToMapSpawn:
		{
			Team *pTeam = team_GetTeam(pEntity);
			if(!pTeam)
			{
				bActivatable = false;
			}
			//If this can't map move, look for teammates on the map
			if(!pItemDef->pWarp->bCanMapMove)
			{
				int iIdx;
				int *piTargets = NULL;
				team_GetOnMapEntIds(entGetPartitionIdx(pEntity), &piTargets, pTeam);
				for(iIdx = eaiSize(&piTargets);iIdx >=0; iIdx--)
				{
					if((ContainerID)piTargets[iIdx] != entGetContainerID(pEntity))
						break;
				}

				if(iIdx < 0)
					bActivatable = false;
				eaiDestroy(&piTargets);

				//If this map cannot map move and the current map isn't the specified map...
				if(pItemDef->pWarp->pchMap
					&& stricmp(pItemDef->pWarp->pchMap, zmapInfoGetPublicName(NULL)))
					bActivatable = false;
			}

		}
		break;
	}

	return bActivatable;
}

AUTO_RUN;
void gclWarp_RegisterEnums(void)
{
	ui_GenInitStaticDefineVars(ItemWarpTypeEnum, "ItemWarp_");
}