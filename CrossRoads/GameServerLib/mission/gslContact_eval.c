/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "contact_common.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "estring.h"
#include "expression.h"
#include "ExpressionPrivate.h"
#include "gslContact.h"
#include "mission_common.h"
#include "Player.h"


// ----------------------------------------------------------------------------------
// Contact Expression Functions
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
int contact_ShowContact_StaticCheck(ExprContext *pContext, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, ACMD_EXPR_ERRSTRING errEstr)
{
	ContactDef *pContactDef;

	if (!pcContactName) {
		estrPrintf(errEstr, "Error: contact name not provided");
		return ExprFuncReturnError;
	}

	pContactDef = contact_DefFromName(pcContactName);
	if (!pContactDef) {
		estrPrintf(errEstr, "Error: contact '%s' does not exist", pcContactName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(ShowContact) ACMD_EXPR_STATIC_CHECK(contact_ShowContact_StaticCheck);
int contact_ShowContact(ExprContext *pContext, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	if (pPlayerEnt && pcContactName) {
		ContactDef *pContactDef = contact_DefFromName(pcContactName);

		if (pContactDef) {
			contact_InteractBegin(pPlayerEnt, NULL, pContactDef, NULL, NULL);
			return 1;
		}
	}
	return 0;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int contact_ShowContactDialog_StaticCheck(ExprContext *pContext, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, const char *pcDialogName, ACMD_EXPR_ERRSTRING errEstr)
{
	ContactDef *pContactDef;

	if (!pcContactName)  {
		estrPrintf(errEstr, "Error: contact name not provided");
		return ExprFuncReturnError;
	}

	if (!pcDialogName) {
		estrPrintf(errEstr, "Error: dialog name not provided");
		return ExprFuncReturnError;
	}

	pContactDef = contact_DefFromName(pcContactName);
	if (!pContactDef) {
		estrPrintf(errEstr, "Error: contact '%s' does not exist", pcContactName);
		return ExprFuncReturnError;
	}

	if(!contact_HasSpecialDialog(pContactDef, pcDialogName)) {
		estrPrintf(errEstr, "Error: contact '%s' does not have a special dialog name '%s'", pcContactName, pcDialogName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(ShowContactDialog) ACMD_EXPR_STATIC_CHECK(contact_ShowContactDialog_StaticCheck);
int contact_ShowContactDialog(ExprContext *pContext, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, const char *pcDialogName, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	if (pPlayerEnt && pcContactName)
	{
		ContactDef *pContactDef = contact_DefFromName(pcContactName);

		if (pContactDef) {
			contact_InteractBegin(pPlayerEnt, NULL, pContactDef, pcDialogName, NULL);
			return 1;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(player) ACMD_NAME(HasRecentlyCompletedContactDialog) ACMD_EXPR_STATIC_CHECK(contact_ShowContactDialog_StaticCheck);
int contact_HasRecentlyCompletedContactDialog(ExprContext *pContext, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, const char *pcDialogName, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	if (pPlayerEnt && pcContactName)
	{
		ContactDef *pContactDef = contact_DefFromName(pcContactName);

		if (pContactDef) {
			ContactDialogInfo *pInfo;
			int i;
			for(i=eaSize(&pPlayerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs)-1; i>=0; --i) {
				pInfo = pPlayerEnt->pPlayer->pInteractInfo->recentlyCompletedDialogs[i];
				if ((pContactDef == GET_REF(pInfo->hContact)) && (pcDialogName == pInfo->pcDialogName)) {
					return true;
				}
			}
		}
	}
	return false;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int contact_ShowContactToEntArray_StaticCheck(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, ACMD_EXPR_ERRSTRING errEstr)
{
	ContactDef *pContactDef;

	if (!pcContactName) {
		estrPrintf(errEstr, "Error: contact name not provided");
		return ExprFuncReturnError;
	}

	pContactDef = contact_DefFromName(pcContactName);
	if (!pContactDef) {
		estrPrintf(errEstr, "Error: contact '%s' does not exist", pcContactName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(ShowContactToEntArray) ACMD_EXPR_STATIC_CHECK(contact_ShowContactToEntArray_StaticCheck);
int contact_ShowContactToEntArray(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, ACMD_EXPR_ERRSTRING errEstr)
{
	if (pcContactName) {
		int i;
		ContactDef *pContactDef = contact_DefFromName(pcContactName);

		if (pContactDef) {
			for(i=eaSize(peaEnts)-1; i>=0; --i) {
				Entity *pEnt = (*peaEnts)[i];	
				if (entIsPlayer(pEnt)) {
					contact_InteractBegin(pEnt, NULL, pContactDef, NULL, NULL);
				}
			}
		}
	}
	return 0;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int contact_ShowContactDialogToEntArray_StaticCheck(ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, const char *pcDialogName, ACMD_EXPR_ERRSTRING errEstr)
{
	ContactDef *pContactDef;

	if (!pcContactName)  {
		estrPrintf(errEstr, "Error: contact name not provided");
		return ExprFuncReturnError;
	}

	if (!pcDialogName) {
		estrPrintf(errEstr, "Error: dialog name not provided");
		return ExprFuncReturnError;
	}

	pContactDef = contact_DefFromName(pcContactName);
	if (!pContactDef) {
		estrPrintf(errEstr, "Error: contact '%s' does not exist", pcContactName);
		return ExprFuncReturnError;
	}

	if(!contact_HasSpecialDialog(pContactDef, pcDialogName)) {
		estrPrintf(errEstr, "Error: contact '%s' does not have a special dialog name '%s'", pcContactName, pcDialogName);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(ShowContactDialogToEntArray) ACMD_EXPR_STATIC_CHECK(contact_ShowContactDialogToEntArray_StaticCheck);
int contact_ShowContactDialogToEntArray(ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_RES_DICT(Contact) const char *pcContactName, const char *pcDialogName, ACMD_EXPR_ERRSTRING errEstr)
{
	if (pcContactName) {
		int i;
		ContactDef *pContactDef = contact_DefFromName(pcContactName);

		if (pContactDef) {
			for(i=eaSize(peaEnts)-1; i>=0; --i) {
				Entity *pEnt = (*peaEnts)[i];
				if (entIsPlayer(pEnt)) {
					contact_InteractBegin(pEnt, NULL, pContactDef, pcDialogName, NULL);
				}
			}
		}
	}
	return 0;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int exprContactDisplayLoreItemSC(ExprContext *pContext, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemDefName, ACMD_EXPR_ERRSTRING errEstr)
{
	ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pcItemDefName);
	if (!pItemDef){
		estrPrintf(errEstr, "No item named %s", pcItemDefName);
		return ExprFuncReturnError;
	}
	if (pItemDef && pItemDef->eType != kItemType_Lore){
		estrPrintf(errEstr, "DisplayLoreItem cannot be called on item %s.  Item must be of type 'Lore'.", pItemDef->pchName);
		return ExprFuncReturnError;
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(DisplayLoreItem) ACMD_EXPR_STATIC_CHECK(exprContactDisplayLoreItemSC);
int exprContactDisplayLoreItem(ExprContext *pContext, ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemDefName, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	if (pPlayerEnt && pcItemDefName) {
		ItemDef *pItemDef = RefSystem_ReferentFromString("ItemDef", pcItemDefName);

		if(pItemDef && pItemDef->eType == kItemType_Lore) {
			contact_DisplayLoreItem(pPlayerEnt, pItemDef);
		}
	}
	return ExprFuncReturnFinished;
}