#include "cmdparse.h"
#include "Expression.h"
#include "earray.h"

#include "UIGen.h"

#include "gclEntity.h"
#include "contact_common.h"

#include "chatCommonStructs.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gclChat.h"
#include "gclChatConfig.h"
#include "inventoryCommon.h"
#include "NotifyCommon.h"
#include "AuctionLot.h"
#include "StashTable.h"
#include "TextFilter.h"
#include "tradeCommon.h"
#include "Player.h"
#include "gclLogin.h"
#include "LoginCommon.h"
#include "GameClientLib.h"
#include "StringUtil.h"
#include "EntitySavedData.h"
#include "SavedPetCommon.h"
#include "utilitiesLib.h"
#include "GamePermissionsCommon.h"
#include "FCInventoryUI.h"
#include "Login2Common.h"

#include "AutoGen/tradeCommon_h_ast.h"
#include "AutoGen/AuctionLot_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("AuctionLot", BUDGET_UISystem););

// ------------------------------------------------------------
// Mail Functions

static int s_iPendingSends;
static int s_iPendingDeletes;
static unsigned char *s_pchError;

extern ParseTable parse_AuctionLot[];
#define TYPE_parse_AuctionLot AuctionLot

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMailPushNewMail(SA_PARAM_NN_VALID ChatMailStruct *mail)
{
	Entity *pEnt = entActivePlayerPtr();
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && mail->sent <= timeSecondsSince2000())
	{
		ChatConfig *pConfig = ClientChatConfig_GetChatConfig(pEnt);
		const char *pchFromName = mail->fromName && *mail->fromName ? mail->fromName : "";
		const char *pchFromHandle = mail->fromHandle && *mail->fromHandle ? mail->fromHandle : "";
		char *pchNotify = NULL;
		char *pchFullFromName = NULL;
		char *pchSubject = mail->subject && *mail->subject ? strdup(mail->subject) : strdup("");

		if (!pEnt->pPlayer->pUI->pMailList)
			pEnt->pPlayer->pUI->pMailList = StructCreate(parse_ChatMailList);
		mail->uTimeReceived = timeSecondsSince2000();
		eaPush(&pEnt->pPlayer->pUI->pMailList->mail, StructClone(parse_ChatMailStruct, mail));

		// Create notification
		if (*pchFromHandle) {
			estrPrintf(&pchFullFromName, "%s@%s", pchFromName, pchFromHandle);
		} else {
			estrCopy2(&pchFullFromName, pchFromName);
		}

		if (!pConfig || pConfig->bProfanityFilter) {
			ReplaceAnyWordProfane(pchSubject);
		}

		entFormatGameMessageKey(pEnt, &pchNotify, "Mail_NewMail",
			STRFMT_STRING("FROM", pchFullFromName),
			STRFMT_STRING("SUBJECT", pchSubject),
			STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_MailReceived, pchNotify, NULL, NULL);

		estrDestroy(&pchFullFromName);
		estrDestroy(&pchNotify);
		free(pchSubject);
	}
}

// Updates the read mail with the new end time
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMailReadUpdate(U32 uMailID, U32 uTimeLeft)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
	{
		ChatMailStruct *mailInternal = eaIndexedGetUsingInt(&pEnt->pPlayer->pUI->pMailList->mail, uMailID);
		if (mailInternal)
		{
			int i,size;
			mailInternal->bRead = true;
			mailInternal->uTimeLeft = uTimeLeft;
			mailInternal->uTimeReceived = timeSecondsSince2000();

			size = eaSize(&pEnt->pPlayer->pUI->pMailList->mail);
			for (i = 0; i < size; i++)
			{
				ChatMailStruct *pMail = pEnt->pPlayer->pUI->pMailList->mail[i];
				if(!pMail->bRead) {
					pEnt->pPlayer->pUI->bUnreadMail = true;
					return;
				}
			}
			pEnt->pPlayer->pUI->bUnreadMail = false;
		}
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMailRefresh(SA_PARAM_NN_VALID ChatMailList *pList)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		int i, size;
		U32 uTime = timeSecondsSince2000();
		StructDestroySafe(parse_ChatMailList, &pEnt->pPlayer->pUI->pMailList);
		pEnt->pPlayer->pUI->pMailList = StructClone(parse_ChatMailList, pList);
		size = eaSize(&pEnt->pPlayer->pUI->pMailList->mail);
		pEnt->pPlayer->pUI->bUnreadMail = false;
		for (i=0; i<size; i++)
		{
			pEnt->pPlayer->pUI->pMailList->mail[i]->uTimeReceived = uTime;
			if (!pEnt->pPlayer->pUI->pMailList->mail[i]->bRead) {
				pEnt->pPlayer->pUI->bUnreadMail = true;
			}
		}
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclSetUnreadMailBit(bool hasUnreadMail)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		pEnt->pPlayer->pUI->bUnreadMail = hasUnreadMail;
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMailSentConfirm(bool bMailSent, const char *errorString)
{
	s_iPendingSends = max(s_iPendingSends - 1, 0);
	if (errorString && *errorString)
		estrCopy2(&s_pchError, errorString);
	else
		estrDestroy(&s_pchError);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMailDeletedConfirm(U32 uMailID)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
	{
		int idx = eaIndexedFindUsingInt(&pEnt->pPlayer->pUI->pMailList->mail, uMailID);
		int i, size;
		if (idx >= 0)
		{
			ChatMailStruct *pMail = pEnt->pPlayer->pUI->pMailList->mail[idx];
			eaRemove(&pEnt->pPlayer->pUI->pMailList->mail, idx);
			StructDestroy(parse_ChatMailStruct, pMail);
		}

		pEnt->pPlayer->pUI->bUnreadMail = false;
		size = eaSize(&pEnt->pPlayer->pUI->pMailList->mail);
		for (i=0; i<size; i++)
		{
			if (!pEnt->pPlayer->pUI->pMailList->mail[i]->bRead) {
				pEnt->pPlayer->pUI->bUnreadMail = true;
				break;
			}
		}
	}
	s_iPendingDeletes = max(s_iPendingDeletes - 1, 0);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclMailReadConfirm(U32 uMailID)
{
	Entity *pEnt = entActivePlayerPtr();
	bool found = false;

	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
	{
		int i,size;
		ChatMailStruct *mailInternal = eaIndexedGetUsingInt(&pEnt->pPlayer->pUI->pMailList->mail, uMailID);
		if (mailInternal)
			mailInternal->bRead = true;
		size = eaSize(&pEnt->pPlayer->pUI->pMailList->mail);
		for (i = 0; i < size; i++)
		{
			ChatMailStruct *pMail = pEnt->pPlayer->pUI->pMailList->mail[i];
			if(!pMail->bRead) {
				pEnt->pPlayer->pUI->bUnreadMail = true;
				return;
			}
		}
		pEnt->pPlayer->pUI->bUnreadMail = false;
	}
}
/*
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailRead);
void gclMailExprGenMarkMailRead(SA_PARAM_NN_VALID ChatMailStruct *mail)
{
	if(!mail->bRead)
	{
		mail->bRead = true;	// marking mail as read to reduce number of updates to client
		ServerCmd_ServerChat_MarkMailRead(mail->uID, mail->iNPCEMailID);
	}
}
*/

void FormatTimeLeft(char **estr, U32 diffTime)
{
	if (diffTime >= SECONDS_PER_DAY)
	{
		U32 time = (diffTime + SECONDS_PER_DAY/2) / SECONDS_PER_DAY;
		FormatGameMessageKey(estr, "Mail_ExpireDays", STRFMT_INT("Count", time), STRFMT_END);
	}
	else if (diffTime >= SECONDS_PER_HOUR)
	{
		U32 time = (diffTime + SECONDS_PER_HOUR/2) / SECONDS_PER_HOUR;
		FormatGameMessageKey(estr, "Mail_ExpireHours", STRFMT_INT("Count", time), STRFMT_END);
	}
	else
	{
		U32 time = (diffTime + 30)/ 60;
		FormatGameMessageKey(estr, "Mail_ExpireMinutesShort", STRFMT_INT("Count", time), STRFMT_END);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailExpires);
char *gclMailFormatExpireTime(SA_PARAM_NN_VALID ChatMailStruct *mail)
{
	static char *datetime = NULL;
	U32 uTime = timeSecondsSince2000();

	estrClear(&datetime);
	if (mail->uTimeLeft)
	{
		U32 diffTime = uTime > mail->uTimeReceived ? uTime - mail->uTimeReceived : 0;
		diffTime = mail->uTimeLeft > diffTime ? mail->uTimeLeft - diffTime : 0;
		FormatTimeLeft(&datetime, diffTime);
	}
	else FormatGameMessageKey(&datetime, "Mail_ExpireNever", STRFMT_END);
	return datetime;
}

// TODO figure out how best to call this to clean up expired mail on the client
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailIsExpired);
void gclMailFormatIsExpired(SA_PARAM_NN_VALID ChatMailStruct *mail)
{
	U32 uTime = timeSecondsSince2000();
	U32 diffTime = uTime > mail->uTimeReceived ? uTime - mail->uTimeReceived : 0;
	diffTime = mail->uTimeLeft > diffTime ? mail->uTimeLeft - diffTime : 0;

	if (diffTime == 0)
	{
		Entity *pEnt = entActivePlayerPtr();
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
		{
			eaFindAndRemove(&pEnt->pPlayer->pUI->pMailList->mail, mail);
			StructDestroy(parse_ChatMailStruct, mail);
		}
	}
}

/*
// Set this gen's model to the list of mail the player has.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMailList);
void gclMailExprGenGetMailList(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
		ui_GenSetList(pGen, &pEnt->pPlayer->pUI->pMailList->mail, parse_ChatMailStruct);
	else
		ui_GenSetList(pGen, NULL, parse_ChatMailStruct);
}
// Send a mail.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailSend);
void gclExprMailSend(const char *pchTo, const char *pchSubject, const char *pchBody)
{
	ServerCmd_MailSend(pchTo, pchSubject, pchBody);
	s_iPendingSends++;
}
*/
// Send a mail.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailSendGuild);
void gclExprMailSendGuild(const char *pchSubject, const char *pchBody)
{
	ServerCmd_guild_SendMail(pchSubject, pchBody);
}

// Check if a mail send is pending.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailSendIsPending);
S32 gclExprMailSendIsPending(void)
{
	return 0;
}

// Check if a mail deletion is pending.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailDeleteIsPending);
S32 gclExprMailDeleteIsPending(void)
{
	return s_iPendingDeletes;
}

// Check if a mail error occurred.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailHasErrors);
bool gclExprMailHasErrors(void)
{
	return s_pchError && *s_pchError;
}

// Get the mail error that occurred.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailGetErrors);
const char *gclExprMailGetErrors(void)
{
	return s_pchError;
}

// Clear the current error status.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailClearErrors);
void gclExprMailClearErrors(void)
{
	estrDestroy(&s_pchError);
	s_iPendingDeletes = 0;
	s_iPendingSends = 0;
}

// Check if any mails are unread.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailUnread);
S32 gclExprMailUnread(SA_PARAM_OP_VALID Entity *pEnt)
{
	S32 iUnread = 0;
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
	{
		S32 i;
		for (i = 0; i < eaSize(&pEnt->pPlayer->pUI->pMailList->mail); i++)
		{
			if (!pEnt->pPlayer->pUI->pMailList->mail[i]->bRead)
				iUnread += 1;
		}
	}
	return iUnread;
}
// Return total number of mails.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailCount);
U32 gclExprMailCount(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
		return pEnt->pPlayer->pUI->pMailList->uTotalMail;
	else
		return 0;
}

///////////////////////////////
// Mailing Items

// Clear a TradeSlotLite's contents.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TradeSlotLiteClear);
void gclMailTradeSlotLiteClear(SA_PARAM_OP_VALID TradeSlotLite *pMailSlot)
{
	if (pMailSlot)
	{
		pMailSlot->count = 0;
		pMailSlot->SrcBagId = 0;
		pMailSlot->SrcSlot = 0;
		pMailSlot->tradeSlotPetID = 0;

		// Delete temp Container item
		if(pMailSlot->pItem)
		{
			ItemDef* pItemDef = GET_REF(pMailSlot->pItem->hItem);
			if(pItemDef && pItemDef->eType == kItemType_Container)
			{
				StructDestroy(parse_Item, pMailSlot->pItem);
			}
			pMailSlot->pItem = NULL;
		}
	}
}

/*
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailCanViewItems);
bool gclMail_MailCanViewItems(ChatMailStruct *mail)
{
	Entity *ent = entActivePlayerPtr();
	char shardName[256];

	if (!ent)
		return false;
	if (!mail || !mail->uLotID)
		return false;

	shardName[0] = 0;

	if (stricmp(mail->shardName, GetVShardQualifiedName(shardName, ARRAY_SIZE_CHECKED(shardName), entGetVirtualShardID(ent)))!=0)
	{
		//don't show items in mail from another shard
		return false;
	}
	return true;
}

// Get the AuctionItem(s) referenced by the mail into the cache.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailGetItems);
void gclMail_MailGetItems(ChatMailStruct *mail)
{
	if (gclMail_MailCanViewItems(mail))
	{
		ServerCmd_ServerChat_MailGetItems(mail->uLotID);
	}
}
*/

/*
// Generate a string for MailSendItems based on the list in this gen.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenMailGetItemString);
const char *gclMail_GenMailGetItemString(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	TradeSlotLite ***peaSlots;
	static char *s_pch;
	S32 i;

	estrClear(&s_pch);

	// Refresh the list so we know the Item pointers in it are valid.
	gclMail_GenMailGetSendingItemList(pGen, pEnt);

	peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlotLite);
	for (i = 0; i < eaSize(peaSlots); i++)
	{
		TradeSlotLite *pMailSlot = (*peaSlots)[i];
		if (pMailSlot->pItem && item_CanTrade(pMailSlot->pItem))
		{
			ItemDef* pItemDef = GET_REF(pMailSlot->pItem->hItem);

			// Add next Item ID
			estrConcatf(&s_pch, "%"FORM_LL"d", pMailSlot->pItem->id);

			// Append the pet ID if item is sent from a pet
			if (pMailSlot->tradeSlotPetID)
			{
				estrConcatf(&s_pch, ":%d", pMailSlot->tradeSlotPetID);
			}

			// Append the container ID if the item is a Container item
			if(pItemDef && pItemDef->eType == kItemType_Container && pMailSlot->pItem->pSpecialProps && pMailSlot->pItem->pSpecialProps->pContainerInfo)
			{
				estrConcatf(&s_pch, "C%d", StringToContainerID(REF_STRING_FROM_HANDLE(pMailSlot->pItem->pSpecialProps->pContainerInfo->hSavedPet)));
			}

			// Append the count
			estrConcatf(&s_pch, " %d ", pMailSlot->count);
		}
	}

	return s_pch ? s_pch : "";
}
*/
// Put this inventory slot into this TradeSlotLite.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TradeSlotLiteSetInventorySlot);
bool gclMailTradeSlotLiteSetInventorySlot(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID TradeSlotLite *pMailSlot, SA_PARAM_OP_VALID InventorySlot *pInvSlot, int iPetID)
{
	ParseTable *pListTable = NULL;
	TradeSlotLite ***peaSlots = NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);

	while (pGen && pListTable != parse_TradeSlotLite || (peaSlots && pListTable == parse_TradeSlotLite && eaFind(peaSlots, pMailSlot) < 0))
	{
		peaSlots = (TradeSlotLite ***)ui_GenGetList(pGen, NULL, &pListTable);
		pGen = pGen->pParent;
	}

	if (peaSlots && (pListTable != parse_TradeSlotLite || eaFind(peaSlots, pMailSlot) < 0))
		peaSlots = NULL;

	if (pMailSlot)
	{
		if (pInvSlot && pInvSlot->pItem && pEnt)
		{
			if (iPetID)
			{
				Entity *pPet = entity_GetSubEntity(PARTITION_CLIENT, pEnt, GLOBALTYPE_ENTITYSAVEDPET, iPetID);
				if (pPet)
				{
					pMailSlot->pItem = inv_GetItemAndSlotsByID(pPet, pInvSlot->pItem->id, &pMailSlot->SrcBagId, &pMailSlot->SrcSlot);
					if(pMailSlot->pItem)
					{
						// set to one to prevent item from clearing
						pMailSlot->count = pMailSlot->pItem->count;
					}
				}
				else
				{
					pMailSlot->pItem = NULL;
				}
			}
			else
			{
				pMailSlot->pItem = inv_GetItemAndSlotsByID(pEnt, pInvSlot->pItem->id, &pMailSlot->SrcBagId, &pMailSlot->SrcSlot);
				if(pMailSlot->pItem)
				{
					// set to one to prevent item from clearing
					pMailSlot->count = pMailSlot->pItem->count;
				}
			}
		}
		else
		{
			pMailSlot->pItem = NULL;
		}

		pMailSlot->tradeSlotPetID = iPetID;

		if (!pMailSlot->pItem)
		{
			gclMailTradeSlotLiteClear(pMailSlot);
		}
		else
		{
			if (peaSlots && pListTable == parse_TradeSlotLite)
			{
				S32 i;
				for (i = eaSize(peaSlots) - 1; i >= 0; i--)
				{
					if ((*peaSlots)[i] != pMailSlot && pMailSlot->pItem == (*peaSlots)[i]->pItem)
					{
						pMailSlot->count = (*peaSlots)[i]->count;
						gclMailTradeSlotLiteClear((*peaSlots)[i]);
					}
				}
			}

			pMailSlot->SrcItemId = pMailSlot->pItem->id;
		}
	}

	return (pMailSlot && pInvSlot && pInvSlot->pItem);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TradeSlotLiteSetInventorySlotByBagIndex);
bool gclMailTradeSlotLiteSetInventorySlotByBagIndex(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID TradeSlotLite *pMailSlot, S32 iBagID, S32 iSlotIndex, S32 iCount, S32 iPetID)
{
	InventorySlot *pInvSlot;
	bool bResult;

	if (pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		pInvSlot = inv_ent_GetSlotPtr(pEnt, iBagID, iSlotIndex, pExtract);
		bResult = gclMailTradeSlotLiteSetInventorySlot(pContext, pEnt, pMailSlot, pInvSlot, iPetID);
	}
	else
	{
		bResult = gclMailTradeSlotLiteSetInventorySlot(pContext, pEnt, pMailSlot, NULL, iPetID);
	}

	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TradeSlotLiteSetInventorySlotFromSavedPetID);
bool gclMailTradeSlotLiteSetInventorySlotFromSavedPetID(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID TradeSlotLite *pMailSlot, S32 iSavedPetID)
{
	Item* pItem = NULL;
	ParseTable *pListTable = NULL;
	TradeSlotLite ***peaSlots = NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	S32 i;

	if(!pMailSlot || !gConf.bAllowContainerItemsInAuction)
		return false;

	while (pGen && pListTable != parse_TradeSlotLite || (peaSlots && pListTable == parse_TradeSlotLite && eaFind(peaSlots, pMailSlot) < 0))
	{
		peaSlots = (TradeSlotLite ***)ui_GenGetList(pGen, NULL, &pListTable);
		pGen = pGen->pParent;
	}

	if (peaSlots && (pListTable != parse_TradeSlotLite || eaFind(peaSlots, pMailSlot) < 0))
		peaSlots = NULL;

	pItem = item_FromSavedPet(pEnt, iSavedPetID);

	if (peaSlots)
	{
		for (i = eaSize(peaSlots) - 1; i >= 0; i--)
		{
			if ((*peaSlots)[i]->pItem && (*peaSlots)[i]->pItem->pSpecialProps && (*peaSlots)[i]->pItem->pSpecialProps->pContainerInfo)
			{
				if ((*peaSlots)[i]->pItem->pSpecialProps->pContainerInfo->eContainerType == pItem->pSpecialProps->pContainerInfo->eContainerType
					&& REF_COMPARE_HANDLES((*peaSlots)[i]->pItem->pSpecialProps->pContainerInfo->hSavedPet, pItem->pSpecialProps->pContainerInfo->hSavedPet))
				{
					gclMailTradeSlotLiteClear((*peaSlots)[i]);
				}
			}
		}
	}

	if(pItem) {
		pMailSlot->tradeSlotPetID = 0;

		pMailSlot->count = 1;
		pMailSlot->pItem = pItem;
		pMailSlot->SrcBagId = 0;
		pMailSlot->SrcSlot = 0;
		pMailSlot->tradeSlotPetID = 0;
		return (pMailSlot->pItem != NULL);
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(TradeSlotLiteSetCount);
void gclMailTradeSlotLiteSetCount(SA_PARAM_OP_VALID TradeSlotLite *pMailSlot, S32 iCount)
{
	if (pMailSlot && pMailSlot->count >= 0 && pMailSlot->SrcItemId)
	{
		if (iCount <= 0)
		{
			gclMailTradeSlotLiteClear(pMailSlot);
		}
		else
		{
			Entity *pEnt = entActivePlayerPtr();
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			InventorySlot *pInvSlot = inv_ent_GetSlotPtr(pEnt, pMailSlot->SrcBagId, pMailSlot->SrcSlot, pExtract);
			if (pInvSlot && pInvSlot->pItem && iCount <= pInvSlot->pItem->count)
			{
				pMailSlot->count = iCount;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InventoryKeyIsMailing);
bool gclMailInventoryKeyIsMailing(SA_PARAM_NN_VALID UIGen *pGen, const char *pchInventoryKey)
{
	ParseTable *pTable;
	TradeSlotLite ***peaTradeSlots = (TradeSlotLite ***)ui_GenGetList(pGen, NULL, &pTable);
	UIInventoryKey Key = {0};
	if (peaTradeSlots && pTable == parse_TradeSlotLite && gclInventoryParseKey(pchInventoryKey, &Key) && Key.pSlot && Key.pSlot->pItem)
	{
		S32 i;
		for (i = eaSize(peaTradeSlots) - 1; i >= 0; i--)
		{
			if ((*peaTradeSlots)[i]->count > 0 && (*peaTradeSlots)[i]->SrcItemId == Key.pSlot->pItem->id)
				return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(InventorySlotIsMailing);
bool gclMailInventorySlotIsMailing(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	ParseTable *pTable;
	TradeSlotLite ***peaTradeSlots = (TradeSlotLite ***)ui_GenGetList(pGen, NULL, &pTable);
	if (peaTradeSlots && pTable == parse_TradeSlotLite && pSlot && pSlot->pItem)
	{
		S32 i;
		for (i = eaSize(peaTradeSlots) - 1; i >= 0; i--)
		{
			if ((*peaTradeSlots)[i]->count > 0 && (*peaTradeSlots)[i]->SrcItemId == pSlot->pItem->id)
				return true;
		}
	}
	return false;
}

// Send a mail with items. Item string is a space-delimited list of 2x(# of item) integers: <Item ID> <Count>
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MailSendItems);
void gclExprMailSendItems(const char *pchTo, const char *pchSubject, const char *pchBody, const char *pchItemString)
{
	Entity* pEnt = entActivePlayerPtr();

	if (!pEnt || entGetVirtualShardID(pEnt) != 0)
	{
		return;
	}
	ServerCmd_MailSendWithItems(pchTo, pchSubject, pchBody, pchItemString);
	s_iPendingSends++;
}

// Check if an entity is near a mailbox.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Mail_IsNearMailBox);
bool gclExprMailIsNearMailBox(SA_PARAM_OP_VALID Entity *pEnt)
{
	return contact_IsNearMailBox(pEnt);
}

// If the incoming string exactly matches a character name for this player change the string to "name@account"
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(Mail_MatchesCharacterName);
char * gclExprMailMatchesCharacterName(char *pStringToCheck)
{
	static char replaceName[MAX_NAME_LEN] = {""};

	replaceName[0] = 0;

	if(g_characterSelectionData && pStringToCheck && *pStringToCheck)
	{
		S32 i;

        for ( i = eaSize(&g_characterSelectionData->characterChoices->characterChoices) - 1; i >= 0; i-- )
		{
            Login2CharacterChoice *characterChoice = g_characterSelectionData->characterChoices->characterChoices[i];
            const char *name = characterChoice->savedName;
			if(stricmp_safe(name, pStringToCheck) == 0)
			{
				sprintf(replaceName, "%s@%s", name, gGCLState.displayName);
			}
		}
	}

	return replaceName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MailCanSendTo");
bool gclExprMailCanSendTo(const char *pchReceiver)
{
	Entity *pEnt = entActivePlayerPtr();

	pchReceiver = pchReceiver && *pchReceiver && strchr(pchReceiver, '@') ? strrchr(pchReceiver, '@') : NULL;
	while (pchReceiver && (*pchReceiver == '@' || IS_WHITESPACE(*pchReceiver)))
		pchReceiver++;

	if (!pEnt || !pEnt->pPlayer || !pchReceiver || !*pchReceiver)
	{
		return false;
	}

	if (gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_SEND_MAIL))
	{
		S32 i;

		if (!pEnt->pPlayer->pUI->pChatState)
		{
			return false;
		}

		for (i = eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends) - 1; i >= 0; --i)
		{
			ChatPlayerStruct *pFriend = pEnt->pPlayer->pUI->pChatState->eaFriends[i];

			if (pFriend->chatHandle == NULL || (pFriend->flags & (FRIEND_FLAG_PENDINGREQUEST|FRIEND_FLAG_RECEIVEDREQUEST)) != 0)
			{
				continue;
			}

			if (!stricmp(pFriend->chatHandle, pchReceiver))
			{
				return true;
			}
		}

		return false;
	}

	return true;
}
