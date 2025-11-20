/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ComboTrackerUI.h"

#include "Character.h"
#include "ComboTracker.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "PowerActivation.h"
#include "PowerSlots.h"
#include "UIGen.h"

#include "AutoGen/ComboTrackerUI_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

ComboTrackerUI UITracker = {0};

AUTO_RUN;
void comboTracker_init(void)
{

}

void comboTracker_PowerActivate(PowerActivation *pAct)
{
	PowerDef *pDef = GET_REF(pAct->ref.hdef);

	if(pDef && comboTracker_RequiresTracking(pDef))
	{
		ComboTrackerEntry *pLastEntry = NULL;
		PowerDef *pLastPowerDef = NULL;

		if(eaSize(&UITracker.ppEntries)>0)
		{
			pLastEntry = UITracker.ppEntries[eaSize(&UITracker.ppEntries)-1];
			pLastPowerDef = GET_REF(pLastEntry->hDef);
			

			if(comboTracker_isComboEnd(pLastPowerDef->ePurpose))
			{
				eaDestroyStruct(&UITracker.ppEntries,parse_ComboTrackerEntry);
				pLastEntry = NULL;
				pLastPowerDef = NULL;
				printf("End combo!\n");
			}
		}

		if(comboTracker_isComboStart(pDef->ePurpose))
		{
			eaDestroyStruct(&UITracker.ppEntries,parse_ComboTrackerEntry);
			pLastEntry = NULL;
			pLastPowerDef = NULL;

			printf("\nStart new combo!\n");
		}

		if((!pLastEntry && comboTracker_isComboStart(pDef->ePurpose)) 
			|| !comboTracker_isComboStart(pDef->ePurpose))
		{
			ComboTrackerEntry *pEntry = StructCreate(parse_ComboTrackerEntry);

			COPY_HANDLE(pEntry->hDef,pAct->ref.hdef);
			pEntry->uchPowerActID = pAct->uchID;
			pEntry->pchIconName = StructAllocString(pDef->pchIconName);

			eaPush(&UITracker.ppEntries,pEntry);

			printf("Continue combo: %s\n",pDef->pchName);
		}
	}
	else if(eaSize(&UITracker.ppEntries))
	{
		eaDestroyStruct(&UITracker.ppEntries,parse_ComboTrackerEntry);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetComboTrackerList");
void comboTracker_GetCurrentCombo(UIGen *pGen)
{
	ui_GenSetManagedListSafe(pGen, &UITracker.ppEntries, ComboTrackerEntry, false);
}

int g_iSortDirection = 0;
int comboTracker_compare(const ComboTrackerEntry **aPtr,const ComboTrackerEntry **bPtr)
{
	const ComboTrackerEntry *a = (*aPtr);
	const ComboTrackerEntry *b = (*bPtr);

	if(a->iTraySlot == -1 || b->iTraySlot == -1)
	{
		return  (b->iTraySlot - a->iTraySlot) * g_iSortDirection;
	}

	return (a->iTraySlot - b->iTraySlot) * g_iSortDirection;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetComboExtenders");
void comboTracker_GetComboExtenders(UIGen *pGen, Entity *pPlayer, int iSortDirection)
{
	int i;
	ComboTrackerEntry **ppEntries = NULL;
	PowerDef *pQueueDef = NULL;

	if(pPlayer->pChar->pPowActQueued)
	{
		 pQueueDef = GET_REF(pPlayer->pChar->pPowActQueued->ref.hdef);
	}

	if(pPlayer && pPlayer->pChar && eaSize(&UITracker.ppEntries))
	{
		GameAccountDataExtract *pExtract = NULL;
		int iPartitionIdx = entGetPartitionIdx(pPlayer);

		for(i=0;i<eaSize(&pPlayer->pChar->ppPowers);i++)
		{
			Power *pPower = character_PickActivatedPower(iPartitionIdx, pPlayer->pChar,pPlayer->pChar->ppPowers[i],NULL,NULL,NULL,NULL,NULL,1,1,NULL);
			PowerDef *pDef = pPower ? GET_REF(pPower->hDef) : NULL;
			
			if(pDef && comboTracker_RequiresTracking(pDef) && !comboTracker_isComboStart(pDef->ePurpose))
			{
				ComboTrackerEntry *pNewEntry = StructCreate(parse_ComboTrackerEntry);

				pNewEntry->pchIconName = StructAllocString(pDef->pchIconName);
				pNewEntry->uchPowerActID = 0;
				COPY_HANDLE(pNewEntry->hDef,pPlayer->pChar->ppPowers[i]->hDef);	

				if(pQueueDef)
					pNewEntry->bQueued = pQueueDef == pDef;

				if (!pExtract)
				{
					pExtract = entity_GetCachedGameAccountDataExtract(pPlayer);
				}
				pNewEntry->bCanActivate = character_CanQueuePower(iPartitionIdx, pPlayer->pChar,pPlayer->pChar->ppPowers[i],NULL,NULL,NULL,NULL,NULL,NULL,NULL,0,0,NULL,0,1,true,pExtract) != NULL;

				if(eaSize(&pPlayer->pChar->pSlots->ppSets))
				{
					pNewEntry->iTraySlot = ea32Find(&pPlayer->pChar->pSlots->ppSets[pPlayer->pChar->pSlots->uiIndex]->puiPowerIDs,pPlayer->pChar->ppPowers[i]->uiID);
				}
				else
				{
					pNewEntry->iTraySlot = -1;
				}

				if(iSortDirection)
					g_iSortDirection = iSortDirection;
				else
					g_iSortDirection = 1;

				eaPush(&ppEntries,pNewEntry);
			}
		}
	}

	if(eaSize(&ppEntries) > 1)
		eaQSort(ppEntries,comboTracker_compare);

	ui_GenSetManagedListSafe(pGen,&ppEntries, ComboTrackerEntry,true);
}

#include "AutoGen/ComboTrackerUI_h_ast.c"