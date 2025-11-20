/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "PowerReplace.h"

#include "Entity.h"
#include "EString.h"
#include "file.h"
#include "ResourceInfo.h"
#include "ResourceManager.h"

#include "Character.h"
#include "CombatEval.h"
#include "inventoryCommon.h"
#include "Powers.h"
#include "PowerTree.h"

#include "AutoGen/itemEnums_h_ast.h"
#include "AutoGen/PowerReplace_h_ast.h"

DictionaryHandle g_hPowerReplaceDefDict;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

PowerReplace *PowerReplace_Create(Entity *pEnt, PowerReplaceDef *pDef, U32 uiPowerID)
{
	PowerReplace *pRtn = StructCreate(parse_PowerReplace);

	pEnt->uiPowerReplaceIDMax++;
	pRtn->uiID = pEnt->uiPowerReplaceIDMax;
	pRtn->uiBasePowerID = uiPowerID;
	SET_HANDLE_FROM_REFERENT(g_hPowerReplaceDefDict,pDef,pRtn->hDef);

	eaPush(&pEnt->ppPowerReplaces,pRtn);
	entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);

	return pRtn;
}

void Entity_AddPowerReplacesFromTrees(Entity *pEnt)
{
	int i;

	for(i=0;i<eaSize(&pEnt->pChar->ppPowerTrees);i++)
	{
		PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
		int j;

		for(j=0;j<eaSize(&pTree->ppNodes);j++)
		{
			PTNodeDef *pNodeDef = GET_REF(pTree->ppNodes[j]->hDef);

			if (pTree->ppNodes[j]->bEscrow)
				continue;

			if(pNodeDef && IS_HANDLE_ACTIVE(pNodeDef->hGrantSlot))
			{
				Power *pPow = powertreenode_GetActivatablePower(pTree->ppNodes[j]);
				PowerReplaceDef *pReplaceDef = GET_REF(pNodeDef->hGrantSlot);
				PowerReplace *pReplace = NULL;

				if(pReplaceDef && pPow)
					pReplace = PowerReplace_Create(pEnt,pReplaceDef,pPow->uiID);

				pTree->ppNodes[j]->uiPowerReplaceID = pReplace ? pReplace->uiID : 0;
			}
		}
	}
}

PowerReplace *Entity_FindPowerReplace(Entity *pEnt, const PowerReplaceDef *pDef)
{
	int i;

	for(i=0;i<eaSize(&pEnt->ppPowerReplaces);i++)
	{
		PowerReplaceDef *pReplaceDef = GET_REF(pEnt->ppPowerReplaces[i]->hDef);

		if(pReplaceDef == pDef)
			return pEnt->ppPowerReplaces[i];
	}

	return NULL;
}

void Entity_ReBuildPowerReplace(Entity *pEnt)
{
	int i;
	//Free all existing power replaces

	for(i=eaSize(&pEnt->ppPowerReplaces)-1;i>=0;i--)
	{
		StructDestroy(parse_PowerReplace,pEnt->ppPowerReplaces[i]);
		eaRemove(&pEnt->ppPowerReplaces,i);
	}

	pEnt->uiPowerReplaceIDMax = 0;

	Entity_AddPowerReplacesFromTrees(pEnt);
	entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
}

void PowerReplace_reset(Entity *pEnt)
{
	int i;

	for(i=0;i<eaSize(&pEnt->ppPowerReplaces);i++)
	{
		eaClear(&pEnt->ppPowerReplaces[i]->ppEnhancements);
		pEnt->ppPowerReplaces[i]->uiReplacePowerID = 0;
	}
}

const PowerReplace *PowerReplace_GetFromID(Entity *pEnt, U32 uiSlotID)
{
	int i;

	for(i=eaSize(&pEnt->ppPowerReplaces)-1;i>=0;i--)
	{
		if(pEnt->ppPowerReplaces[i]->uiID == uiSlotID)
			return pEnt->ppPowerReplaces[i];
	}

	return NULL;
}

void power_GetEnhancementsPowerReplace(int iPartitionIdx,Entity *pEnt,Power *ppow,Power ***ppPowersAttached)
{
	int i;

	for(i=0;i<eaSize(&pEnt->ppPowerReplaces);i++)
	{
		if(pEnt->ppPowerReplaces[i]->uiBasePowerID == ppow->uiID || pEnt->ppPowerReplaces[i]->uiReplacePowerID == ppow->uiID)
		{
			int n;

			for(n=0;n<eaSize(&pEnt->ppPowerReplaces[i]->ppEnhancements);n++)
			{
				Power *pPower = pEnt->ppPowerReplaces[i]->ppEnhancements[n];
				PowerDef *pPowerDef = GET_REF(pPower->hDef);
				int iCharges = pPowerDef->iCharges;

				combateval_ContextReset(kCombatEvalContext_Enhance);
				combateval_ContextSetupEnhance(pEnt->pChar,GET_REF(ppow->hDef),false);

				//Check to see if it can apply
				if((!pPowerDef->pExprEnhanceAttach || combateval_EvalNew(iPartitionIdx,pPowerDef->pExprEnhanceAttach,kCombatEvalContext_Enhance,NULL))
					&& (iCharges == 0 || iCharges > power_GetChargesUsed(pPower)))
					eaPushUnique(ppPowersAttached,pPower);
			}
		}
	}
}

//----------------Loading Funcs----------------//

AUTO_RUN;
void PowerReplaceRegisterDict(void)
{
	g_hPowerReplaceDefDict = RefSystem_RegisterSelfDefiningDictionary("PowerReplaceDef", false, parse_PowerReplaceDef, true, true, "PowerReplace");
	
	// This is used on a dictionary that loads on the client to enable editor usage
	if (isDevelopmentMode() || IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hPowerReplaceDefDict, ".Name", NULL, NULL, NULL, NULL);		
		}
	}
}

AUTO_STARTUP(PowerReplaces) ASTRT_DEPS(InventoryBags);
void PowerReplaceLoad(void)
{
	resLoadResourcesFromDisk(g_hPowerReplaceDefDict, NULL, "defs/config/PowerReplaces.def", "PowerReplaces.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
}

#include "AutoGen/PowerReplace_h_ast.c"