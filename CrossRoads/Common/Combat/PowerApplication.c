/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerApplication.h"

#include "Character.h"
#include "Entity.h"
#include "file.h"

#include "CharacterClass.h"
#include "CombatConfig.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerSubtarget.h"
#include "PowerTree.h"
#include "itemCommon.h"

#include "AutoGen/PowerApplication_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static U32 s_uiIDApply = 0;

// Returns the next Application ID
U32 powerapp_NextID(void)
{
	// Static U32 that rolls over when the high bit is set, skips 0
	s_uiIDApply++;
	if(s_uiIDApply & BIT(31))
		s_uiIDApply = 1;
	return s_uiIDApply;
}

U32 powerapp_GetCurrentID()
{
	return s_uiIDApply;
}

// Returns the hue that should be used, given the Character, Power
//  PowerActivation, and/or PowerDef/PowerDef's AnimFX (in order of preference)
F32 powerapp_GetHue(Character *pchar,
					Power *ppow,
					PowerActivation *pact,
					PowerDef *pdef)
{
	F32 fHue = 0;
	if(pchar && pchar->pEntParent && pchar->pEntParent->fHue)
	{
		fHue = pchar->pEntParent->fHue;
	}
	else
	{

		if(!ppow && pchar && pact)
		{
			ppow = character_ActGetPower(pchar,pact);
		}

		// Switch to parent (for combo powers)
		if(ppow && ppow->pParentPower)
		{
			ppow = ppow->pParentPower;
		}

		if(ppow
			&& ppow->fHue
			&& !(g_CombatConfig.bPowerCustomizationDisabled
				&& isProductionMode()
				&& pchar
				&& pchar->pEntParent
				&& entGetAccessLevel(pchar->pEntParent)<ACCESS_GM))
		{
			fHue = ppow->fHue;
		}
		else if(ppow && ppow->eSource == kPowerSource_Item && ppow->pSourceItem)
		{
			//Search though the gems to see if any modify the hue
			int i;
			ItemDef *pItemDef = GET_REF(ppow->pSourceItem->hItem);

			if(!pdef && ppow)
			{
				pdef = GET_REF(ppow->hDef);
			}

			if(pItemDef && pItemDef->fPowerHue)
				fHue = pItemDef->fPowerHue;
			else if(pdef)
				fHue = powerdef_GetHue(pdef);

			if(ppow->pSourceItem->pSpecialProps && eaSize(&ppow->pSourceItem->pSpecialProps->ppItemGemSlots) > 0)
			{
				for(i=0;i<eaSize(&ppow->pSourceItem->pSpecialProps->ppItemGemSlots);i++)
				{
					ItemDef *pGemDef = GET_REF(ppow->pSourceItem->pSpecialProps->ppItemGemSlots[i]->hSlottedItem);

					if(pGemDef && pGemDef->fPowerHue)
						fHue = pGemDef->fPowerHue;
				}
			}
		}
		else
		{
			if(!pdef && ppow)
				pdef = GET_REF(ppow->hDef);

			if(pdef)
			{
				bool bFound = false;
				int i;
				CharacterPath** pPaths = NULL;

				ANALYSIS_ASSUME(pdef != NULL);
				if(pchar && pchar->pEntParent)
				{
					ANALYSIS_ASSUME(pchar && pchar->pEntParent);
					eaStackCreate(&pPaths, eaSize(&pchar->ppSecondaryPaths) + 1);

					entity_GetChosenCharacterPaths(pchar->pEntParent, &pPaths);

					for (i = 0; i < eaSize(&pPaths); i++)
					{
						if (pPaths[i]->fHue
							&& ppow
							&& GET_REF(ppow->hDef))
						{
							PowerTree* pTree = NULL;
							if (character_FindPowerByDefTree(pchar,GET_REF(ppow->hDef),&pTree,NULL) == ppow)
							{
								if (GET_REF(pTree->hDef) == GET_REF(pPaths[i]->hPowerTree))
								{
									fHue = pPaths[i]->fHue;
									bFound = true;
									break;
								}
							}
						}
					}
				}

				if (!bFound)
				{
					fHue = powerdef_GetHue(pdef);
				}
			}
		}
	}

	return fHue;
}

F32 powerapp_GetTotalTime(PowerApplication * papp)
{
	devassert(papp && papp->pact && papp->pdef);
	return papp->pact->fTimeCharged + papp->pdef->fTimePreactivate + papp->pdef->fTimeActivate;
}


#include "AutoGen/PowerApplication_h_ast.c"
