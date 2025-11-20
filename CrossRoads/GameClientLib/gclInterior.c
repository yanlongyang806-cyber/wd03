/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "StringCache.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "entCritter.h"
#include "gclEntity.h"
#include "UIGen.h"
#include "EntityIterator.h"
#include "WorldGrid.h"
#include "InteriorCommon.h"
#include "InteriorCommon_h_ast.h"
#include "gclInterior.h"
#include "GameAccountDataCommon.h"
#include "MicroTransactionUI.h"
#include "gclMicroTransactions.h"
#include "stringUtil.h"
#include "GamePermissionsCommon.h"
#include "CharacterClass.h"

#include "AutoGen/gclInterior_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

//
// Is the current map the interior of a player's pet?  This is used
//  to enable UI and commands such as invite, kick, etc.
//
static U32 s_currentMapPlayersInterior = false;

static MicroTransactionUIProduct **s_eaSelectedProductList;

void
gclInterior_GameplayEnter(void)
{
	s_currentMapPlayersInterior = false;
	ServerCmd_gslInterior_IsCurrentMapPlayerCurrentInterior();
}

void
gclInterior_GameplayLeave(void)
{
	s_currentMapPlayersInterior = false;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Interior);
void
gclInterior_SetCurrentMapPlayersInterior(bool isInterior)
{
	s_currentMapPlayersInterior = isInterior;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsCurrentMapPlayersPetInterior);
bool
exprIsCurrentMapPlayersPetInterior()
{
	return s_currentMapPlayersInterior;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsCurrentMapPlayersInterior);
bool
exprIsCurrentMapPlayersInterior()
{
	return exprIsCurrentMapPlayersPetInterior();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CurrentMapIsInterior);
bool
exprCurrentMapIsInterior()
{
	return InteriorCommon_IsCurrentMapInterior();
}

int SortInteriorChoices(const InteriorChoiceRow **pA, const InteriorChoiceRow **pB)
{
	return(strcmp_safe((*pA)->displayMessageText, (*pB)->displayMessageText));
}

static void
GetOptionsForInterior(SA_PARAM_NN_VALID UIGen *pGen, Entity *pEnt, ContainerID petID, InteriorDef *pInteriorDef, const char *optionName)
{
	static InteriorOptionChoice **s_availableChoices = NULL;
	static InteriorChoiceRow **s_choiceRows = NULL;
	int i;
	Message *displayNameMessage;
	const char *pooledCurrentChoice = InteriorCommon_GetOptionChoiceNameForInterior(pEnt, petID, pInteriorDef->name, optionName);

	eaClearFast(&s_availableChoices);

	InteriorCommon_GetInteriorOptionChoices(pEnt, pInteriorDef, optionName, &s_availableChoices);

	while ( eaSize(&s_choiceRows) < eaSize(&s_availableChoices) )
	{
		eaPush(&s_choiceRows, StructCreate(parse_InteriorChoiceRow));
	}

	for ( i = eaSize(&s_availableChoices) - 1; i >= 0; i-- )
	{
		devassert(s_choiceRows != NULL);
		displayNameMessage = GET_REF(s_availableChoices[i]->hDisplayName);
		s_choiceRows[i]->name = s_availableChoices[i]->name;

		s_choiceRows[i]->isChosen = (s_choiceRows[i]->name == pooledCurrentChoice);

		if ( displayNameMessage != NULL )
		{
			s_choiceRows[i]->displayMessageName = displayNameMessage->pcMessageKey;
		}
		else
		{
			s_choiceRows[i]->displayMessageName = "";
		}

		s_choiceRows[i]->displayMessageText = StructAllocString(TranslateMessageRef(s_availableChoices[i]->hDisplayName));

		s_choiceRows[i]->value = s_availableChoices[i]->value;
	}

	while ( eaSize(&s_choiceRows) > eaSize(&s_availableChoices) )
	{
		StructDestroy(parse_InteriorChoiceRow, eaPop(&s_choiceRows));
	}

	eaQSort(s_choiceRows, SortInteriorChoices);

	ui_GenSetList(pGen, &s_choiceRows, parse_InteriorChoiceRow);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAvailableInteriorOptionChoices);
void 
exprGetAvailableInteriorOptionChoices(SA_PARAM_NN_VALID UIGen *pGen, ContainerID petID, const char *optionName)
{
	Entity *pEnt = entActivePlayerPtr();

	// If the player or pet are not found, then the gen list is not updated.
	if ( pEnt )
	{
		Entity *pTargetEnt = petID ? InteriorCommon_GetPetByID(pEnt, petID) : pEnt;
		if ( pTargetEnt )
		{
			InteriorDef *pInteriorDef = InteriorCommon_GetCurrentInteriorDef(pTargetEnt);
			GetOptionsForInterior(pGen, pEnt, petID, pInteriorDef, optionName);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetOptionChoicesForInterior);
void 
exprGetOptionChoicesForInterior(SA_PARAM_NN_VALID UIGen *pGen, ContainerID petID, const char *interiorName, const char *optionName)
{
	Entity *pEnt = entActivePlayerPtr();

	// If the player or pet are not found, then the gen list is not updated.
	if ( pEnt )
	{
		Entity *pTargetEnt = petID ? InteriorCommon_GetPetByID(pEnt, petID) : pEnt;
		if ( pTargetEnt )
		{
			InteriorDef *pInteriorDef = InteriorCommon_GetPetInteriorDefByName(pTargetEnt, interiorName);
			GetOptionsForInterior(pGen, pEnt, petID, pInteriorDef, optionName);
		}
	}
}

// Gets the name of the first option on this interior that has the isTailorOption flag on it.
// In STO this is used to find the correct option to use when selection ship interior layout.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteriorTailorOptionName);
const char *
exprGetInteriorTailorOptionName(ContainerID petID, const char *interiorName)
{
	Entity *playerEnt = entActivePlayerPtr();
	if ( ( playerEnt != NULL ) && ( interiorName != NULL ) && ( interiorName[0] != '\0' ) )
	{
		Entity *petEnt = InteriorCommon_GetPetByID(playerEnt, petID);
		if ( petEnt != NULL )
		{
			InteriorDef *interiorDef;
			int i;

			interiorDef = InteriorCommon_GetPetInteriorDefByName(petEnt, interiorName);

			for ( i = 0; i < eaSize(&interiorDef->optionRefs); i++ )
			{
				InteriorOptionDef *optionDef;
				optionDef = GET_REF(interiorDef->optionRefs[i]->hOptionDef);

				if ( optionDef && optionDef->isTailorOption )
				{
					return optionDef->name;
				}
			}
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsInteriorOptionChoiceValid);
bool
exprIsInteriorOptionChoiceValid(ContainerID petID, const char *interiorName, const char *optionName, const char *choiceName)
{
	Entity *pEnt = entActivePlayerPtr();

	// If the player or pet are not found, then the gen list is not updated.
	if ( pEnt )
	{
		Entity *petEnt = InteriorCommon_GetPetByID(pEnt, petID);
		if ( petEnt )
		{
			InteriorDef *pInteriorDef = InteriorCommon_GetPetInteriorDefByName(petEnt, interiorName);
			if ( pInteriorDef )
			{
				InteriorOptionDef *pOptionDef = InteriorCommon_FindOptionDefByName(optionName);

				if ( pOptionDef )
				{
					return InteriorCommon_IsOptionAvailableForInterior(pInteriorDef, pOptionDef) && InteriorCommon_IsChoiceAvailableForOption(pOptionDef, choiceName);
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteriorOptionChoiceName);
const char * 
exprGetInteriorOptionChoiceName(ContainerID petID, const char *optionName)
{
	Entity *playerEnt = entActivePlayerPtr();
	const char *name;

	name = InteriorCommon_GetOptionChoiceName(playerEnt, petID, optionName);

	return NULL_TO_EMPTY(name);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteriorOptionChoiceDisplayMessage);
const char * 
exprGetInteriorOptionChoiceDisplayMessage(ContainerID petID, const char *optionName)
{
	Entity *playerEnt = entActivePlayerPtr();
	const char *name;

	name = InteriorCommon_GetOptionChoiceDisplayMessage(playerEnt, petID, optionName);

	return NULL_TO_EMPTY(name);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetOptionChoiceNameForInterior);
const char * 
exprGetOptionChoiceNameForInterior(ContainerID petID, const char *interiorName, const char *optionName)
{
	Entity *playerEnt = entActivePlayerPtr();
	const char *name;

	name = InteriorCommon_GetOptionChoiceNameForInterior(playerEnt, petID, interiorName, optionName);

	return NULL_TO_EMPTY(name);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetOptionChoiceDisplayMessageForInterior);
const char * 
exprGetOptionChoiceDisplayMessageForInterior(ContainerID petID, const char *interiorName, const char *optionName)
{
	Entity *playerEnt = entActivePlayerPtr();
	const char *name;

	name = InteriorCommon_GetOptionChoiceDisplayMessageForInterior(playerEnt, petID, interiorName, optionName);

	return NULL_TO_EMPTY(name);
}

static InteriorOptionChoice *
GetValidOptionChoiceForInterior(ContainerID petID, const char *interiorName, const char *optionName)
{
	Entity *pEnt = entActivePlayerPtr();

	if ( pEnt )
	{
		Entity *petEnt = InteriorCommon_GetPetByID(pEnt, petID);
		if ( petEnt )
		{
			InteriorDef *pInteriorDef = InteriorCommon_GetPetInteriorDefByName(petEnt, interiorName);
			if ( pInteriorDef )
			{
				InteriorOptionDef *pOptionDef = InteriorCommon_FindOptionDefByName(optionName);

				if ( pOptionDef && InteriorCommon_IsOptionAvailableForInterior(pInteriorDef, pOptionDef) && GET_REF(pOptionDef->hDefaultChoice) )
				{
					return GET_REF(pOptionDef->hDefaultChoice);
				}
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetValidOptionChoiceNameForInterior);
const char * 
exprGetValidOptionChoiceNameForInterior(ContainerID petID, const char *interiorName, const char *optionName)
{
	InteriorOptionChoice *pChoice = GetValidOptionChoiceForInterior(petID, interiorName, optionName);

	if ( pChoice )
	{
		return pChoice->name;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetValidOptionChoiceDisplayMessageForInterior);
const char * 
exprGetValidOptionChoiceDisplayMessageForInterior(ContainerID petID, const char *interiorName, const char *optionName)
{
	InteriorOptionChoice *pChoice = GetValidOptionChoiceForInterior(petID, interiorName, optionName);

	if ( pChoice )
	{
		Message *pMessage = GET_REF(pChoice->hDisplayName);
		if ( pMessage )
		{
			return pMessage->pcMessageKey;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteriorOptionChoiceValue);
S32 
exprGetInteriorOptionChoiceValue(ContainerID petID, const char *optionName)
{
	Entity *playerEnt = entActivePlayerPtr();
	S32 value;

	value = InteriorCommon_GetOptionChoiceValue(playerEnt, petID, optionName);

	return value;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCurrentPetInteriorName);
const char *
exprGetCurrentPetInteriorName(ContainerID petID)
{
	Entity *petEnt = InteriorCommon_GetPetByID(entActivePlayerPtr(), petID);
	InteriorDef *interiorDef;

	interiorDef = InteriorCommon_GetCurrentInteriorDef(petEnt);

	if ( interiorDef == NULL )
	{
		return NULL;
	}

	return interiorDef->name;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCurrentInteriorName);
const char *
exprGetCurrentInteriorName()
{
	InteriorDef *interiorDef = InteriorCommon_GetCurrentInteriorDef(entActivePlayerPtr());

	return interiorDef ? interiorDef->name : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCurrentPetInteriorDisplayNameMsg);
const char *
	exprGetCurrentPetInteriorDisplayNameMsg(ContainerID petID)
{
	Entity *petEnt = InteriorCommon_GetPetByID(entActivePlayerPtr(), petID);
	InteriorDef *interiorDef;
	Message *message;

	interiorDef = InteriorCommon_GetCurrentInteriorDef(petEnt);

	if ( interiorDef == NULL )
	{
		return NULL;
	}

	message = GET_REF(interiorDef->displayNameMsg.hMessage);

	if ( message == NULL )
	{
		return NULL;
	}

	return message->pcMessageKey;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCurrentInteriorDisplayName);
const char *
exprGetCurrentInteriorDisplayNameMsg()
{
	InteriorDef *interiorDef = InteriorCommon_GetCurrentInteriorDef(entActivePlayerPtr());

	if( (interiorDef == NULL) )
	{
		return NULL;
	}

	return TranslateDisplayMessage(interiorDef->displayNameMsg);
}

// Get the list of products that unlock the interior
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteriorSelect_GetProductList);
void
exprInteriorSelectRowGetProductList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InteriorSelectRow *pInteriorSelect)
{
	MicroTransactionUIProduct ***peaProducts = ui_GenGetManagedListSafe(pGen, MicroTransactionUIProduct);
	eaClearFast(peaProducts);

	if (pInteriorSelect)
	{
		if (eaSize(&pInteriorSelect->eaFullProductList))
		{
			eaCopy(peaProducts, &pInteriorSelect->eaFullProductList);
		}
		else if (pInteriorSelect->pProduct)
		{
			eaPush(peaProducts, pInteriorSelect->pProduct);
		}
	}

	ui_GenSetManagedListSafe(pGen, peaProducts, MicroTransactionUIProduct, false);
}

// Get the number of products that unlock the interior
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteriorSelect_GetProductListSize);
S32
exprInteriorSelectRowGetProductListSize(SA_PARAM_OP_VALID InteriorSelectRow *pInteriorSelect)
{
	if (pInteriorSelect)
	{
		if (eaSize(&pInteriorSelect->eaFullProductList))
		{
			return eaSize(&pInteriorSelect->eaFullProductList);
		}
		else if (pInteriorSelect->pProduct)
		{
			return 1;
		}
	}
	return 0;
}

// Cache the product list of the selected interior, the product list is accessed through InteriorSelect_GetSelectedProductList and InteriorSelect_GetSelectedProductListSize
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteriorSelect_SetSelectedProductList);
void
exprInteriorSelectRowSetSelectedProductList(SA_PARAM_OP_VALID InteriorSelectRow *pInteriorSelect)
{
	eaClearStruct(&s_eaSelectedProductList, parse_MicroTransactionUIProduct);
	if (pInteriorSelect)
	{
		if (eaSize(&pInteriorSelect->eaFullProductList))
		{
			eaCopyStructs(&pInteriorSelect->eaFullProductList, &s_eaSelectedProductList, parse_MicroTransactionUIProduct);
		}
		else if (pInteriorSelect->pProduct)
		{
			eaPush(&s_eaSelectedProductList, StructClone(parse_MicroTransactionUIProduct, pInteriorSelect->pProduct));
		}
	}
}

// Get the product list of the selected interior, the product list returned is set by using InteriorSelect_SetSelectedProductList
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteriorSelect_GetSelectedProductList);
void
exprInteriorSelectRowGetSelectedProductList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_eaSelectedProductList, parse_MicroTransactionUIProduct);
}

// Get the product list size of the selected interior, the product list inspected is set by using InteriorSelect_SetSelectedProductList
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteriorSelect_GetSelectedProductListSize);
S32
exprInteriorSelectRowGetSelectedProductListSize(void)
{
	return eaSize(&s_eaSelectedProductList);
}

static bool
UpdateInteriorRow(Entity *pEnt, InteriorDef *interiorDef, InteriorSelectRow ***peaRows, int idx, bool includeMicroTransactions)
{
	static MicroTransactionProduct **s_productList;
	int microTrans = 0, unlocked = 0, updated = 0, i, usedProducts;

	// if unlocked by microtransaction, get the product id
	if ( interiorDef->bUnlockable && interiorDef->pchUnlockKey && *interiorDef->pchUnlockKey )
	{
		gclMicroTrans_FindProductsForKey(&s_productList, interiorDef->pchUnlockKey);

		for ( i = eaSize(&s_productList) - 1; i >= 0; i-- )
		{
			if ( gclMicroTrans_IsProductHidden(s_productList[i]) || microtrans_GetPrice(s_productList[i]->pProduct) < 0 )
			{
				eaRemove(&s_productList, i);
			}
		}

		if ( eaSize(&s_productList) > 0 )
		{
			microTrans = s_productList[0]->uID;
		}
	}

	if ( InteriorCommon_IsInteriorUnlocked(pEnt, interiorDef) )
	{
		unlocked = true;
	}

	if ( unlocked || (includeMicroTransactions && microTrans != 0) )
	{
		Message *message = GET_REF(interiorDef->displayNameMsg.hMessage);

		if ( message != NULL )
		{
			InteriorSelectRow *row = (*peaRows)[idx];
			row->displayNameMsg = message->pcMessageKey;
			row->interiorName = interiorDef->name;
			row->unlocked = unlocked;
			SET_HANDLE_FROM_REFERENT(g_hInteriorDefDict, interiorDef, row->hInterior);

			row->iMinimumProductPrice = LLONG_MAX;
			row->iMaximumProductPrice = 0;

			row->MicroTransactionID = microTrans;
			if (microTrans)
			{
				// Since this is called every frame, only regenerate the contents if the ID's change
				if ( !row->pProduct || row->pProduct->uID != (U32)microTrans )
				{
					StructDestroySafe(parse_MicroTransactionUIProduct, &row->pProduct);
					row->pProduct = gclMicroTrans_MakeUIProduct(microTrans);
				}

				if ( row->pProduct )
				{
					row->iMinimumProductPrice = row->iMaximumProductPrice = row->pProduct->iPrice;
				}

				// Only generate an array if there's more than one
				if ( eaSize(&s_productList) > 1 )
				{
					usedProducts = 0;
					for ( i = 0; i < eaSize(&s_productList); i++ )
					{
						if ( gclMicroTrans_IsProductHidden(s_productList[i]) || microtrans_GetPrice(s_productList[i]->pProduct) < 0 )
						{
							continue;
						}

						if ( usedProducts >= eaSize(&row->eaFullProductList) || row->eaFullProductList[usedProducts]->uID != s_productList[i]->uID )
						{
							MicroTransactionUIProduct *newProductInfo = gclMicroTrans_MakeUIProduct(s_productList[i]->uID);
							if ( newProductInfo )
							{
								MIN1(row->iMinimumProductPrice, newProductInfo->iPrice);
								MAX1(row->iMaximumProductPrice, newProductInfo->iPrice);
								// Always make sure pProduct contains valid information
								if ( !row->pProduct )
									row->pProduct = StructClone(parse_MicroTransactionUIProduct, newProductInfo);
							}
							if ( newProductInfo && usedProducts < eaSize(&row->eaFullProductList) )
							{
								// replace old entry
								StructDestroy(parse_MicroTransactionUIProduct, row->eaFullProductList[usedProducts]);
								row->eaFullProductList[usedProducts] = newProductInfo;
								usedProducts++;
							}
							else if ( newProductInfo )
							{
								// add new entry
								eaPush(&row->eaFullProductList, newProductInfo);
								usedProducts++;
							}
							else if ( usedProducts < eaSize(&row->eaFullProductList) )
							{
								// remove old entry
								StructDestroy(parse_MicroTransactionUIProduct, eaRemove(&row->eaFullProductList, usedProducts));
							}
						}
						else
						{
							// cached entry still good
							MIN1(row->iMinimumProductPrice, row->eaFullProductList[usedProducts]->iPrice);
							MAX1(row->iMaximumProductPrice, row->eaFullProductList[usedProducts]->iPrice);
							usedProducts++;
						}
					}
					//eaSetSizeStruct(&row->eaFullProductList, parse_MicroTransactionUIProduct, usedProducts);
					for(i=eaSize(&row->eaFullProductList)-1;i>=usedProducts;i--)
					{
						MicroTransactionUIProduct *productInfo = row->eaFullProductList[i];

						eaRemove(&row->eaFullProductList,i);
						StructDestroySafe(parse_MicroTransactionUIProduct,&productInfo);
					}
				}
				else
				{
					eaClearStruct(&row->eaFullProductList, parse_MicroTransactionUIProduct);
				}
			}
			else
			{
				StructDestroySafe(parse_MicroTransactionUIProduct, &row->pProduct);
				eaClearStruct(&row->eaFullProductList, parse_MicroTransactionUIProduct);
			}

			updated = true;
		}
	}
	return updated;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAllAvailablePetInteriors);
void 
exprGetAllAvailablePetInteriors(SA_PARAM_NN_VALID UIGen *pGen, ContainerID petID, U32 flags)
{
	static InteriorSelectRow **s_InteriorSelectRows = NULL;
	Entity *pEnt = entActivePlayerPtr();
	Entity *petEnt = InteriorCommon_GetPetByID(pEnt, petID);
	PetDef *petDef;
	bool includeMicroTransactions = (flags & 1);
	int i,n, numInteriors = 0;

	if ( petEnt != NULL )
	{
		petDef = GET_REF(petEnt->pCritter->petDef);
		if ( petDef != NULL )
		{
			n = eaSize(&petDef->ppInteriorDefs);
			for ( i = 0; i < n; i++ )
			{
				InteriorDef *interiorDef = GET_REF(petDef->ppInteriorDefs[i]->hInterior);

				if ( interiorDef )
				{
					while (eaSize(&s_InteriorSelectRows) <= numInteriors)
					{
						eaPush(&s_InteriorSelectRows, StructCreate(parse_InteriorSelectRow));
					}
					if(UpdateInteriorRow(pEnt, interiorDef, &s_InteriorSelectRows, numInteriors, includeMicroTransactions))
						numInteriors++;
				}
			}
		}
	}

	// clean up any excess row structures
	while (eaSize(&s_InteriorSelectRows) > numInteriors)
	{
		StructDestroy(parse_InteriorSelectRow, eaPop(&s_InteriorSelectRows));
	}

	ui_GenSetList(pGen, &s_InteriorSelectRows, parse_InteriorSelectRow);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAvailablePetInteriors);
void 
exprGetAvailablePetInteriors(SA_PARAM_NN_VALID UIGen *pGen, ContainerID petID)
{
	exprGetAllAvailablePetInteriors(pGen, petID, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAllAvailableInteriors);
void 
exprGetAllAvailableInteriors(SA_PARAM_NN_VALID UIGen *pGen)
{
	InteriorSelectRow ***peaRows = ui_GenGetManagedListSafe(pGen, InteriorSelectRow);
	Entity *pEnt = entActivePlayerPtr();
	int numInteriors = 0;

	if ( pEnt != NULL )
	{
		FOR_EACH_IN_REFDICT(g_hInteriorDefDict, InteriorDef, pInterior)
		{
			if(pInterior)
			{
				while (eaSize(peaRows) <= numInteriors)
				{
					eaPush(peaRows, StructCreate(parse_InteriorSelectRow));
				}
				if(UpdateInteriorRow(pEnt, pInterior, peaRows, numInteriors, 0))
					numInteriors++;
			}
		} FOR_EACH_END;
	}

	// clean up any excess row structures
	while (eaSize(peaRows) > numInteriors)
	{
		StructDestroy(parse_InteriorSelectRow, eaPop(peaRows));
	}

	ui_GenSetManagedListSafe(pGen, peaRows, InteriorSelectRow, false);
}

static int SortInteriorOptions(const InteriorOptionDef **pA, const InteriorOptionDef **pB)
{
	if((*pA) && (*pB))
	{
		return stricmp(TranslateMessageRef((*pA)->hDisplayName),TranslateMessageRef((*pB)->hDisplayName));
	}
	else
	{
		return 0;
	}
}

static void GetInteriorOptions(Entity *pEnt, InteriorOptionDef ***peaOptions, InteriorDef *pInterior, bool bIncludeOneChoiceOptions)
{
	eaClear(peaOptions);

	if(pInterior && pEnt)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pInterior->optionRefs, InteriorOptionDefRef, pDefRef)
		{
			InteriorOptionDef *pOptionDef = GET_REF(pDefRef->hOptionDef);
			if(pOptionDef)
			{
				if(!bIncludeOneChoiceOptions)
				{
					static InteriorOptionChoice **s_availableChoices = NULL;

					eaClearFast(&s_availableChoices);

					InteriorCommon_GetInteriorOptionChoices(pEnt, pInterior, pOptionDef->name, &s_availableChoices);

					if(eaSize(&s_availableChoices) <= 1)
						continue;
				}

				eaPush(peaOptions, pOptionDef);
			}
		} FOR_EACH_END;
	}

	eaQSort(*peaOptions, SortInteriorOptions);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAvailableOptions);
void 
exprGetAvailableOptionsByInterior(SA_PARAM_NN_VALID UIGen *pGen, const char *pchInterior)
{
	InteriorOptionDef ***peaOptions = (InteriorOptionDef***) ui_GenGetManagedList(pGen, parse_InteriorOptionDef);
	InteriorDef *pInterior = (InteriorDef*)RefSystem_ReferentFromString(g_hInteriorDefDict, pchInterior);
	
	Entity *pEnt = entActivePlayerPtr();

	GetInteriorOptions(pEnt, peaOptions, pInterior, false);

	ui_GenSetManagedListSafe(pGen, peaOptions, InteriorOptionDef, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAllOptions);
void 
exprGetAllOptionsByInterior(SA_PARAM_NN_VALID UIGen *pGen, const char *pchInterior)
{
	InteriorOptionDef ***peaOptions = (InteriorOptionDef***) ui_GenGetManagedList(pGen, parse_InteriorOptionDef);
	InteriorDef *pInterior = (InteriorDef*)RefSystem_ReferentFromString(g_hInteriorDefDict, pchInterior);

	Entity *pEnt = entActivePlayerPtr();

	GetInteriorOptions(pEnt, peaOptions, pInterior, true);

	ui_GenSetManagedListSafe(pGen, peaOptions, InteriorOptionDef, false);
}

static int SortInteriorSettings(const InteriorSettingRow **pA, const InteriorSettingRow **pB)
{
	return stricmp((*pA)->displayName,(*pB)->displayName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteriorSettingsDescString);
void 
exprGetInteriorSettingsDescString(ACMD_EXPR_STRING_OUT pcDesc, const char *pchInterior)
{
	InteriorDef *pInterior = (InteriorDef*)RefSystem_ReferentFromString(g_hInteriorDefDict, pchInterior);
	Entity *pEnt = entActivePlayerPtr();
	static char *estrDesc = NULL;
	estrClear(&estrDesc);

	if(pInterior)
	{
		InteriorSetting **eaSettings = NULL;
		InteriorCommon_GetSettingsByInterior(pInterior, &eaSettings);
		FOR_EACH_IN_EARRAY(eaSettings, InteriorSetting, pSetting)
		{

			if(pSetting->pchPermission && g_pMTList && !GamePermission_EntHasToken(pEnt, pSetting->pchPermission))
			{
				// check to see that we want to add this item to the tooltip
				// make sure this is in a list
				S32 i, partIdx;
				bool bFound = false;
				for(i = 0; i < eaSize(&g_pMTList->ppProducts); ++i)
				{
					MicroTransactionDef *pDef = GET_REF(g_pMTList->ppProducts[i]->hDef);
					if(pDef)
					{
						for(partIdx = 0; partIdx < eaSize(&pDef->eaParts); ++partIdx)
						{
							if(pDef->eaParts[partIdx]->ePartType == kMicroPart_Permission && pDef->eaParts[partIdx]->pchPermission && stricmp(pDef->eaParts[partIdx]->pchPermission, pSetting->pchName) == 0)
							{
								bFound = true;
							}
						}
					}
				}

				if(!bFound)
				{
					continue;
				}
			}

			if(estrDesc && *estrDesc)
				estrAppend2(&estrDesc, "<br>");

			estrAppend2(&estrDesc, TranslateDisplayMessage(pSetting->displayNameMsg));
			if(pSetting->pchPermission)
			{
				estrConcatf(&estrDesc, " - %sOwned",
					GamePermission_EntHasToken(pEnt, pSetting->pchPermission) ? "" : "Not ");
			}
		} FOR_EACH_END;
		eaDestroy(&eaSettings);
	}

	(*pcDesc) = strdup(estrDesc);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAvailableInteriorSettings);
void 
exprGetAvailableSettingsByInterior(SA_PARAM_NN_VALID UIGen *pGen, const char *pchInterior)
{
	InteriorSettingRow ***peaSettings = (InteriorSettingRow***) ui_GenGetManagedList(pGen, parse_InteriorSettingRow);
	InteriorDef *pInterior = (InteriorDef*)RefSystem_ReferentFromString(g_hInteriorDefDict, pchInterior);
	Entity *pEnt = entActivePlayerPtr();
	EntityInteriorData *pIntData = SAFE_MEMBER2(pEnt, pSaved, interiorData);

	eaClearStruct(peaSettings, parse_InteriorSettingRow);

	if(pInterior)
	{
		InteriorSetting **eaSettings = NULL;
		InteriorCommon_GetSettingsByInterior(pInterior, &eaSettings);
		FOR_EACH_IN_EARRAY(eaSettings, InteriorSetting, pSetting)
		{
			InteriorSettingRow *pSettingRow = NULL;
			if(pSetting->pchPermission && !GamePermission_EntHasToken(pEnt, pSetting->pchPermission))
				continue;

			pSettingRow = StructCreate(parse_InteriorSettingRow);
			pSettingRow->name = pSetting->pchName;
			pSettingRow->displayName = StructAllocString(TranslateDisplayMessage(pSetting->displayNameMsg));
				
			if( pIntData && 
				GET_REF(pIntData->hSetting) &&
				GET_REF(pIntData->hSetting) == pSetting)
			{
				pSettingRow->isChosen = true;
			}
			eaPush(peaSettings, pSettingRow);
		} FOR_EACH_END;
		eaDestroy(&eaSettings);
	}

	eaQSort(*peaSettings, SortInteriorSettings);

	//Sort before adding in the "None" line so it's always first
	{
		InteriorSettingRow *pSettingRow = StructCreate(parse_InteriorSettingRow);
		pSettingRow->name = NULL;
		pSettingRow->displayName = StructAllocString("None");

		if( pIntData && !GET_REF(pIntData->hSetting) )
		{
			pSettingRow->isChosen = true;
		}
		eaInsert(peaSettings, pSettingRow, 0);
	}

	ui_GenSetManagedListSafe(pGen, peaSettings, InteriorSettingRow, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPetInteriorChangeCost);
S32
exprGetPetInteriorChangeCost(ContainerID petID)
{
	Entity *playerEnt = entActivePlayerPtr();
	if ( playerEnt != NULL )
	{
		Entity *petEnt = InteriorCommon_GetPetByID(playerEnt, petID);
		if ( petEnt != NULL )
		{
			return InteriorConfig_InteriorChangeCost(playerEnt,petEnt,entity_GetGameAccount(playerEnt));
		}
	}
	return -1;
}

// Compute the cost of changing the interior and an interior option.  Don't charge for changing to the same value.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteriorAndOptionChangeCost);
S32
exprGetInteriorAndOptionChangeCost(ContainerID petID, const char *interiorName, const char *optionName, const char *choiceName)
{
	S32 cost = 0;
	Entity *playerEnt = entActivePlayerPtr();
	if ( playerEnt != NULL )
	{
		Entity *petEnt = InteriorCommon_GetPetByID(playerEnt, petID);
		if ( petEnt != NULL )
		{
			const char *currentInteriorName = exprGetCurrentPetInteriorName(petID);
			const char *currentChoiceName = exprGetInteriorOptionChoiceName(petID, optionName); 

			// add cost for changing interior
			if ( ( currentInteriorName != NULL ) && ( interiorName != NULL ) && ( stricmp(currentInteriorName, interiorName) != 0 ) )
			{
				cost = cost + InteriorConfig_InteriorChangeCost(playerEnt,petEnt,entity_GetGameAccount(playerEnt));
			}

			// add cost for changing option
			if ( ( currentChoiceName != NULL ) && ( choiceName != NULL ) && ( optionName != NULL ) && ( stricmp(currentChoiceName, choiceName) != 0 ) )
			{
				cost = cost + InteriorCommon_InteriorOptionChangeCost(optionName);
			}
		}
	}
	return cost;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPetInteriorChangeCostNumeric);
const char *
exprGetPetInteriorChangeCostNumeric(ContainerID petID)
{
	return InteriorConfig_InteriorChangeCostNumeric();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanPlayerAffordInteriorChange);
bool
exprCanPlayerAffordInteriorChange(ContainerID petID)
{
	Entity *playerEnt = entActivePlayerPtr();
	if ( playerEnt != NULL )
	{
		Entity *petEnt = InteriorCommon_GetPetByID(playerEnt, petID);
		if ( petEnt != NULL )
		{
			return InteriorCommon_CanAffordInteriorChange(playerEnt, petEnt);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetPetInterior);
void
exprSetPetInterior(ContainerID petID, const char *interiorName)
{
	ServerCmd_gslInterior_SetInterior(petID, interiorName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetPetInteriorAndOption);
void
exprSetPetInteriorAndOption(ContainerID petID, const char *interiorName, const char *optionName, const char *choiceName)
{
	ServerCmd_gslInterior_SetInteriorAndOption(petID, interiorName, optionName, choiceName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SetPetInteriorAndSetting);
void
exprSetPetInteriorAndsetting(ContainerID petID, const char *interiorName, const char *settingName)
{
	ServerCmd_gslInterior_SetInteriorAndSetting(petID, interiorName, settingName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetPetInteriorDisplayNameMsg);
const char *
exprGetPetInteriorDisplayNameMsg(ContainerID petID, const char *interiorName)
{
	Entity *playerEnt = entActivePlayerPtr();
	if ( ( playerEnt != NULL ) && ( interiorName != NULL ) && ( interiorName[0] != '\0' ) )
	{
		Entity *petEnt = InteriorCommon_GetPetByID(playerEnt, petID);
		if ( petEnt != NULL )
		{
			InteriorDef *interiorDef;
			Message *message;

			interiorDef = InteriorCommon_GetPetInteriorDefByName(petEnt, interiorName);

			if ( interiorDef != NULL )
			{
				message = GET_REF(interiorDef->displayNameMsg.hMessage);

				if ( message != NULL )
				{
					return message->pcMessageKey;
				}
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsInteriorInvitee);
bool
exprIsInteriorInvitee(void)
{
	Entity *playerEnt = entActivePlayerPtr();
	if ( playerEnt == NULL)
	{
		return false;
	}

	return InteriorCommon_IsInteriorInvitee(playerEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteriorInviteOwnerName);
const char *
exprInteriorInviteOwnerName(void)
{
	InteriorInvite *invite;
	Entity *playerEnt = entActivePlayerPtr();
	if ( playerEnt == NULL)
	{
		return false;
	}

	invite = InteriorCommon_GetCurrentInteriorInvite(playerEnt);

	if ( invite == NULL )
	{
		return false;
	}

	return invite->ownerDisplayName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InteriorInviteShipName);
const char *
exprInteriorInviteShipName(void)
{
	InteriorInvite *invite;
	Entity *playerEnt = entActivePlayerPtr();
	if ( playerEnt == NULL)
	{
		return false;
	}

	invite = InteriorCommon_GetCurrentInteriorInvite(playerEnt);

	if ( invite == NULL )
	{
		return false;
	}

	return invite->shipDisplayName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CanMoveToInterior);
bool
exprCanMoveToInterior(void)
{
	Entity *playerEnt = entActivePlayerPtr();
	return InteriorCommon_CanMoveToInterior(playerEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetInteriorGuests);
void 
exprGetInteriorGuests(SA_PARAM_NN_VALID UIGen *pGen)
{
	static InteriorGuestRow **s_InteriorGuestRows = NULL;
	int numGuests = 0;

	if ( s_currentMapPlayersInterior )
	{
		Entity *player = NULL;
		Entity *owner = entActivePlayerPtr();
		int ownerParitionIdx = entGetPartitionIdx(owner);

		if ( owner != NULL )
		{
			ContainerID ownerID = owner->myContainerID;
			EntityIterator *iter = entGetIteratorSingleType(ownerParitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);

			while ((player = EntityIteratorGetNext(iter)))
			{
				// don't include interior owner in the list
				if ( ( player != NULL ) && ( player->pSaved != NULL ) && ( player->myContainerID != ownerID ) )
				{
					// expand s_InteriorGuestRows as needed
					while (eaSize(&s_InteriorGuestRows) <= numGuests)
					{
						eaPush(&s_InteriorGuestRows, StructCreate(parse_InteriorGuestRow));
					}

					s_InteriorGuestRows[numGuests]->guestName = StructAllocString(entGetLocalName(player));
					s_InteriorGuestRows[numGuests]->guestRef = entGetRef(player);

					numGuests++;
				}
			}
			EntityIteratorRelease(iter);
		}
	}

	// clean up any excess row structures
	while (eaSize(&s_InteriorGuestRows) > numGuests)
	{
		StructDestroy(parse_InteriorGuestRow, eaPop(&s_InteriorGuestRows));
	}

	ui_GenSetList(pGen, &s_InteriorGuestRows, parse_InteriorGuestRow);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ExpelInteriorGuest);
void 
exprExpelInteriorGuest(EntityRef guestRef)
{
	ServerCmd_gslInterior_ExpelGuest(guestRef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MoveToInterior);
void 
exprMoveToInterior(const char *pchSet)
{
	ServerCmd_gslInterior_MoveToActiveInterior(pchSet);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(HasPetWithInterior);
bool
exprHasPetWithInterior(SA_PARAM_OP_VALID CharClassCategorySet *pSet)
{
	Entity *playerEnt = entActivePlayerPtr();
	Entity *petEnt = InteriorCommon_GetActiveInteriorOwner(playerEnt, pSet);
	if ( petEnt != NULL && entGetType(petEnt) == GLOBALTYPE_ENTITYSAVEDPET )
	{
		PetDef *petDef = GET_REF(petEnt->pCritter->petDef);
		if ( petDef != NULL )
		{
			if (eaSize(&petDef->ppInteriorDefs) > 0)
			{
				return true;
			}
		}
	}

    return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetAllFreeSettings);
void
exprGetAllFreeSettings(SA_PARAM_NN_VALID UIGen *pGen)
{
	InteriorSettingRefRow ***peaRows = (InteriorSettingRefRow***) ui_GenGetManagedList(pGen, parse_InteriorSettingRefRow);
	Entity *pEnt = entActivePlayerPtr();
	InteriorSettingMTRef ***peaSettings = InteriorCommon_GetFreeSettings();
	int i, s = eaSize(peaSettings);

	eaClearStruct(peaRows, parse_InteriorSettingRefRow);

	for(i=0; i<s;i++)
	{
		InteriorSettingMTRef *pRef = eaGet(peaSettings, i);
		InteriorSettingRefRow *pRow = NULL;
		InteriorSetting *pSetting = GET_REF(pRef->hSetting);
		MicroTransactionDef *pDef = GET_REF(pRef->hMTDef);
		MicroTransactionProduct *pProduct = NULL;
		if(g_pMTList)
		{
			FOR_EACH_IN_EARRAY(g_pMTList->ppProducts, MicroTransactionProduct, pMTProduct)
			{
				if(	GET_REF(pMTProduct->hDef) && 
					GET_REF(pMTProduct->hDef) == pDef)
				{
					pProduct = pMTProduct;
					break;
				}
			} FOR_EACH_END;
		}

		if(pSetting && pDef && pProduct)
		{
			pRow = StructCreate(parse_InteriorSettingRefRow);
			pRow->name = allocAddString(pSetting->pchName);
			pRow->displayName = StructAllocString(TranslateDisplayMessage(pSetting->displayNameMsg));
			pRow->MicroTransactionID = pProduct->uID;
			pRow->pProduct = gclMicroTrans_MakeUIProduct(pProduct->uID);
			pRow->isOwned = microtrans_HasPurchased(entity_GetGameAccount(pEnt), pDef);

			eaPush(peaRows, pRow);
		}
	}

	ui_GenSetManagedListSafe(pGen, peaRows, InteriorSettingRefRow, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FreeInteriorPurchasesRemaining);
S32
exprFreeInteriorPurchasesRemaining(ExprContext *context)
{
	Entity *pEnt = entActivePlayerPtr();
	if(pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		S32 iValue = InteriorCommon_trh_EntFreePurchasesRemaining(CONTAINER_NOCONST(Entity, pEnt), pExtract);
		
		return iValue;
	}

	return 0;
}

#include "gclInterior_h_ast.c"