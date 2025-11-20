
#include "cmdClient.h"
#include "Character.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "Powers.h"
#include "qsortG.h"
#include "StringCache.h"
#include "UIGen.h"
#include "gclMicroTransactions.h"

#include "VanityPetUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct VanityPetInfo
{
	const char *pchName;			AST(KEY)
		//The pet's name (critter def of the vanity ent create)

	const char *pchIcon;	AST(POOL_STRING)
		// The icon to display

	char *pchDisplayName;
		// The nicer display name of the power

	char *pchShortDesc;
		//The short description

	char *pchLongDesc;
		// The long description

	REF_TO(PowerDef) hPowerDef;
		//The power that would actually get used

	U32 uiMicroTransactionID; AST(NAME(MicroTransactionID))
		// The microtransaction that may be used to purchase this vanity pet

	U32 bActive : 1;
	U32 bUpdated : 1;
	U32 bOwned : 1;
} VanityPetInfo;

int sortPetsByDisplayName(const VanityPetInfo **a, const VanityPetInfo **b)
{
	return(stricmp((*a)->pchDisplayName,(*b)->pchDisplayName));
}

static VanityPetInfo *FillVanityPet(Entity *pEntity, VanityPetInfo ***peaVanityPets, const char *pchPetName)
{
	VanityPetInfo *pPet = NULL;
	PowerDef *pDef;

	if (pchPetName)
	{
		pPet = eaIndexedGetUsingString(peaVanityPets, pchPetName);

		if (!pPet)
		{
			pPet = StructCreate(parse_VanityPetInfo);
			pPet->pchName = StructAllocString(pchPetName);
			SET_HANDLE_FROM_STRING(g_hPowerDefDict, pchPetName, pPet->hPowerDef);
			eaPush(peaVanityPets, pPet);
		}

		// Only compute once per frame
		if (!pPet->bUpdated)
		{
			pPet->bUpdated = true;

			pDef = GET_REF(pPet->hPowerDef);
			if(pDef)
			{
				Power *ppow = character_FindPowerByNamePersonal(pEntity->pChar, pPet->pchName);
				const char *pchMesg = NULL;
				pPet->bOwned = ppow != NULL;
				pPet->bActive = ppow ? !!ppow->bActive : false;

				if( stricmp(pPet->pchIcon,pDef->pchIconName) && pDef->pchIconName )
				{
					pPet->pchIcon = allocAddString(pDef->pchIconName);
				}

				pchMesg = entTranslateDisplayMessage(pEntity, pDef->msgDisplayName);
				if(stricmp(pPet->pchDisplayName, pchMesg))
				{
					StructFreeString(pPet->pchDisplayName);
					pPet->pchDisplayName = StructAllocString(pchMesg);
				}

				pchMesg = entTranslateDisplayMessage(pEntity, pDef->msgDescription);
				if(stricmp(pPet->pchShortDesc, pchMesg))
				{
					StructFreeString(pPet->pchShortDesc);
					pPet->pchShortDesc = StructAllocString(pchMesg);
				}

				pchMesg = entTranslateDisplayMessage(pEntity, pDef->msgDescriptionLong);
				if(stricmp(pPet->pchLongDesc, pchMesg))
				{
					StructFreeString(pPet->pchLongDesc);
					pPet->pchLongDesc = StructAllocString(pchMesg);
				}
			}
		}
	}

	return pPet;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VanityPet_GetList);
void exprFuncVanityPetGetList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	VanityPetInfo ***peaVanityPets = ui_GenGetManagedListSafe(pGen, VanityPetInfo);
	S32 iPetIdx;

	if(!eaSize(peaVanityPets))
		eaIndexedEnable(peaVanityPets, parse_VanityPetInfo);
	else
	{
		eaSortUsingKey(peaVanityPets,parse_VanityPetInfo);
	}

	for(iPetIdx = eaSize(peaVanityPets)-1;iPetIdx >= 0; iPetIdx--)
	{
		(*peaVanityPets)[iPetIdx]->bUpdated = false;
	}

	if (pEntity && g_pMTList)
	{
		S32 iMTIdx;
		for (iMTIdx = eaSize(&g_pMTList->ppProducts) - 1; iMTIdx >= 0; iMTIdx--)
		{
			MicroTransactionProduct *pProduct = g_pMTList->ppProducts[iMTIdx];
			MicroTransactionDef *pMTDef = pProduct ? GET_REF(pProduct->hDef) : NULL;
			if (pMTDef)
			{
				S32 iPartIdx;
				for (iPartIdx = eaSize(&pMTDef->eaParts) - 1; iPartIdx >= 0; iPartIdx--)
				{
					MicroTransactionPart *pPart = pMTDef->eaParts[iPartIdx];
					if (pPart->ePartType == kMicroPart_VanityPet)
					{
						VanityPetInfo *pPet = FillVanityPet(pEntity, peaVanityPets, REF_STRING_FROM_HANDLE(pPart->hPowerDef));
						pPet->uiMicroTransactionID = pProduct->uID;
					}
				}
			}
		}
	}

	if(pEntity && pEntity->pPlayer)
	{
		GameAccountData *pGameAccountData = GET_REF(pEntity->pPlayer->pPlayerAccountData->hData);
		if(pGameAccountData)
		{
			for(iPetIdx = eaSize(&pGameAccountData->eaVanityPets)-1; iPetIdx >= 0; iPetIdx--)
			{
				FillVanityPet(pEntity, peaVanityPets, pGameAccountData->eaVanityPets[iPetIdx]);
			}
		}
	}

	for(iPetIdx = eaSize(peaVanityPets)-1;iPetIdx >= 0; iPetIdx--)
	{
		if(!(*peaVanityPets)[iPetIdx]->bUpdated)
		{
			StructDestroy(parse_VanityPetInfo, eaRemove(peaVanityPets, iPetIdx));
		}
	}

	eaQSortG(*peaVanityPets, sortPetsByDisplayName);

	ui_GenSetManagedListSafe(pGen, peaVanityPets, VanityPetInfo, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(VanityPet_Activate);
void exprFuncVanityPetActivate(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID VanityPetInfo *pInfo)
{
	if(pEntity && pInfo)
	{
		entUsePower(1, REF_STRING_FROM_HANDLE(pInfo->hPowerDef));
	}
}

#include "VanityPetUI_c_ast.c"