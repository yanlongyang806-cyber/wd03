#include "MailCommon.h"
#include "MailCommon_h_ast.h"
#include "itemCommon.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "Player.h"
#include "autotransdefs.h"
#include "AuctionLot.h"
#include "AuctionLot_h_ast.h"
#include "chatCommonStructs.h"
#include "GameStringFormat.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


AUTO_TRANS_HELPER;
NOCONST(EmailV3)* EmailV3_trh_GetOrCreateSharedBankMail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pSharedBank, bool bCreateIfNotFound)
{
	if (NONNULL(pSharedBank))
	{
		if (ISNULL(pSharedBank->pEmailV3) && bCreateIfNotFound)
		{
			pSharedBank->pEmailV3 = StructCreateNoConst(parse_EmailV3);
			pSharedBank->pEmailV3->eaMessages = NULL;
		}

		if (NONNULL(pSharedBank->pEmailV3))
			eaIndexedEnableNoConst(&pSharedBank->pEmailV3->eaMessages, parse_EmailV3Message);

		return pSharedBank->pEmailV3;
	}
	return NULL;
}

int EmailV3_GetPlayerInbox(Entity* pEnt, EmailV3Message*** peaMessagesOut)
{
	Entity* pAccountSharedBank = GET_REF(pEnt->pPlayer->hSharedBank);
	EmailV3* pMailbox = EmailV3_GetSharedBankMail(pAccountSharedBank);
	if (pMailbox)
		eaPushEArray(peaMessagesOut, &pMailbox->eaMessages);
	return eaSize(&pMailbox->eaMessages);
}

NOCONST(EmailV3Message)* EmailV3_trh_CreateNewMessage(const char* pchSubject,
	const char* pchBody, 
	Entity* pSender, 
	const char* pchSenderName,
	const char* pchSenderHandle)
{
	NOCONST(EmailV3Message)* pNCMessage = StructCreateNoConst(parse_EmailV3Message);

	pNCMessage->pchSenderName = StructAllocString(pchSenderName);
	pNCMessage->pchSenderHandle = pchSenderHandle ? StructAllocString(pchSenderHandle) : NULL;
	pNCMessage->pchSubject = StructAllocString(pchSubject);
	pNCMessage->pchBody = StructAllocString(pchBody);
	pNCMessage->eTypeOfEmail = pSender ? kEmailV3Type_Player : kEmailV3Type_NPC;
	pNCMessage->uSent = timeSecondsSince2000();

	return pNCMessage;
}

AUTO_TRANS_HELPER;
void EmailV3_trh_AddItemsToMessage(ATR_ARGS, ATH_ARG NOCONST(EmailV3Message)* pMessage, NOCONST(Item)** eaItems)
{
	eaPushEArray(&pMessage->ppItems, &eaItems);
}

AUTO_TRANS_HELPER;
void EmailV3_trh_AddItemsToMessageFromAuctionLot(ATR_ARGS, ATH_ARG NOCONST(EmailV3Message)* pMessage, ATH_ARG NOCONST(AuctionLot)* pLot)
{
	int i;
	for(i = 0; i < eaSize(&pLot->ppItemsV2); ++i)
	{
		if(NONNULL(pLot->ppItemsV2[i]->slot.pItem))
		{
			item_trh_FixupAlgoProps(pLot->ppItemsV2[i]->slot.pItem);
			eaPush(&pMessage->ppItems, StructCloneNoConst(parse_Item, pLot->ppItemsV2[i]->slot.pItem));
		}
	}
}

AUTO_TRANS_HELPER;
enumTransactionOutcome EmailV3_trh_AddItemsToMessageFromEntInventory(ATR_ARGS, ATH_ARG NOCONST(EmailV3Message)* pMessage, EmailV3SenderItem** eaItemsFromInventory, ATH_ARG NOCONST(Entity)* pSourceEnt, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	int i;
	NOCONST(Item)* pItem = NULL;

	for (i = 0; i < eaSize(&eaItemsFromInventory); i++)
	{
		if (eaItemsFromInventory[i]->iCount <= 0)
			continue;

		if (eaItemsFromInventory[i]->pchLiteItemName)
		{
			ItemDef* pDef = RefSystem_ReferentFromString(g_hItemDict, eaItemsFromInventory[i]->pchLiteItemName);
			InvBagIDs eID = inv_trh_GetBestBagForItemDef(pSourceEnt, pDef, -eaItemsFromInventory[i]->iCount, false, pExtract);
			BagIterator* pBagIter = inv_bag_trh_FindItemByDefName(ATR_PASS_ARGS, pSourceEnt, eID, eaItemsFromInventory[i]->pchLiteItemName, NULL);
			pItem = CONTAINER_NOCONST(Item, bagiterator_RemoveItem(ATR_PASS_ARGS, pBagIter, pSourceEnt, eaItemsFromInventory[i]->iCount, pReason, pExtract));

			if (!pItem || pItem->count != eaItemsFromInventory[i]->iCount)
			{
				StructDestroyNoConstSafe(parse_Item, &pItem);
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
		else
		{
			BagIterator* pIter = inv_trh_FindItemByID(ATR_PASS_ARGS, pSourceEnt, eaItemsFromInventory[i]->uID);

			if (bagiterator_GetItemCount(pIter) < eaItemsFromInventory[i]->iCount)
				return TRANSACTION_OUTCOME_FAILURE;

			pItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pSourceEnt, true, bagiterator_GetCurrentBag(pIter), bagiterator_GetSlotID(pIter), eaItemsFromInventory[i]->iCount, pReason);

			if (!pItem || pItem->count != eaItemsFromInventory[i]->iCount)
				return TRANSACTION_OUTCOME_FAILURE;
		}
		if (pItem)
		{
			eaPush(&pMessage->ppItems, pItem);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome EmailV3_trh_DeliverMessage(ATR_ARGS, ATH_ARG NOCONST(Entity)* pRecipientAccountSharedBank, ATH_ARG NOCONST(EmailV3Message)* pMessage)
{
	NOCONST(EmailV3)* pEmail = EmailV3_trh_GetOrCreateSharedBankMail(ATR_PASS_ARGS, pRecipientAccountSharedBank, true);

	if (ISNULL(pEmail))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		pMessage->uID = ++pEmail->uLastUsedID;
		eaIndexedPushUsingIntIfPossible(&pEmail->eaMessages, pMessage->uID, pMessage);
		pRecipientAccountSharedBank->pEmailV3->bUnreadMail = true;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}

/*
AUTO_COMMAND_REMOTE;
void ChatServerSendMail_v2 (CmdContext *context, ChatMailRequest *request)
{
	int senderID = 0;
	int recipientAccountID = 0;
	ChatMailStruct *mail;
	ChatTranslation *error = NULL;
	int result;

	if (!request->mail)
		return;
	mail = request->mail;

	recipientAccountID = userFindByHandle(request->recipientHandle);
	senderID = request->mail->fromID;

	result = userAddEmailEx(sender, recipient, mail->shardName, mail);
	switch(result)
	{
	case CHATRETURN_USER_DNE:
		if (request->recipientHandle)
			error = constructTranslationMessage("ChatServer_UserDNE", STRFMT_STRING("User", request->recipientHandle), STRFMT_END);
		else
			error = constructTranslationMessage("ChatServer_SendMailFail", STRFMT_END);
		xcase CHATRETURN_USER_IGNORING:
		error = constructTranslationMessage("ChatServer_BeingIgnored", STRFMT_STRING("User", recipient->handle), STRFMT_END);
		xcase CHATRETURN_USER_PERMISSIONS:
		{
			U32 duration = sender->silenced-timeSecondsSince2000();
			error = constructTranslationMessage("ChatServer_Silenced", STRFMT_INT("H", duration/(3600)), 
				STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);
		}
		xcase CHATRETURN_MAILBOX_FULL:
		error = constructTranslationMessage("ChatServer_MailboxFull", STRFMT_STRING("User", recipient->handle), STRFMT_END);
	}

	ChatMailSendConfirmation_v2(chatServerGetCommandLink(), sender->id, mail->uLotID, request->uRequestID, error);
	if (error)
	{
		if (gbChatVerbose)
			printf("Send Mail error: %s\n", error->key);
		StructDestroy(parse_ChatTranslation, error);
	}
}
*/

bool EmailV3_GetAllMessagesMatchingID(Entity* pEnt, U32 uID, EmailV3Message* pMessageOut, NPCEMailData* pNPCMessageOut, ChatMailStruct* pChatMailOut)
{
	Entity* pSharedBank = SAFE_GET_REF2(pEnt, pPlayer, hSharedBank);
	EmailV3* pMailData = EmailV3_GetSharedBankMail(pSharedBank);
	int i;
	//New mail storage
	if (pMessageOut && pSharedBank && pSharedBank->pEmailV3 && pSharedBank->pEmailV3->eaMessages)
	{
		int iMessage = eaIndexedFindUsingInt(&pSharedBank->pEmailV3->eaMessages, uID);
		if (iMessage >= 0)
			pMessageOut = pSharedBank->pEmailV3->eaMessages[iMessage];
	}

	if (pChatMailOut && pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
	{
		for (i = 0; i < eaSize(&pEnt->pPlayer->pUI->pMailList->mail); i++)
		{
			if (pEnt->pPlayer->pUI->pMailList->mail[i]->uID == uID)
			{
				pChatMailOut = pEnt->pPlayer->pUI->pMailList->mail[i];
			}
		}
	}
	//Mail from NPCs
	if (pNPCMessageOut && pEnt && pEnt->pPlayer && pEnt->pPlayer->pEmailV2)
	{
		for (i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); i++)
		{
			if (pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID > -1 && (U32)pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID == uID)
			{
				pNPCMessageOut = pEnt->pPlayer->pEmailV2->mail[i];
			}
		}
	}

	return pMessageOut || pChatMailOut || pNPCMessageOut;
}

S32 EmailV3_GetNumNPCMessagesByType(Entity* pEnt, NPCEmailType eType, bool bWithItemsAttached)
{
	int iCount = 0;
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pEmailV2)
	{
		int i;
		for (i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); i++)
		{
			if (pEnt->pPlayer->pEmailV2->mail[i]->eType == eType && (!bWithItemsAttached || eaSize(&pEnt->pPlayer->pEmailV2->mail[i]->ppItemSlot) > 0))
				iCount++;
		}
	}
	return iCount;
}

void EmailV3_RebuildMailList(Entity *pEnt, Entity* pSharedBank, EmailV3UIMessage ***pppMessagesOut)
{
	ParseTable *pListTable = NULL;
	int i, iNum = 0;
	//Entity* pSharedBank = SAFE_GET_REF2(pEnt, pPlayer, hSharedBank);
	EmailV3* pMailData = EmailV3_GetSharedBankMail(pSharedBank);
	//Old mail from chatserver
	if (!pEnt)
		return;
	pEnt->pPlayer->pUI->bUnreadMail = false;
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pMailList)
	{
		for (i = 0; i < eaSize(&pEnt->pPlayer->pUI->pMailList->mail); i++)
		{
			EmailV3UIMessage* pUIMessage = eaGetStruct(pppMessagesOut, parse_EmailV3UIMessage, iNum);
			pUIMessage->pMessage = NULL;
			pUIMessage->pChatServerMessage = pEnt->pPlayer->pUI->pMailList->mail[i];
			pUIMessage->pNPCMessage = NULL;
			pUIMessage->subject = pUIMessage->pChatServerMessage->subject;
			pUIMessage->fromName = pUIMessage->pChatServerMessage->fromName;
			pUIMessage->fromHandle = pUIMessage->pChatServerMessage->fromHandle;
			pUIMessage->body = pUIMessage->pChatServerMessage->body;
			pUIMessage->sent = pUIMessage->pChatServerMessage->sent;
			pUIMessage->shardName = pUIMessage->pChatServerMessage->shardName;
			pUIMessage->bRead = pUIMessage->pChatServerMessage->bRead;
			pUIMessage->uID = pUIMessage->pChatServerMessage->uID;
			pUIMessage->uLotID = pUIMessage->pChatServerMessage->uLotID;
			pUIMessage->iNPCEMailID = pUIMessage->pChatServerMessage->iNPCEMailID;
			pUIMessage->toContainerID = pUIMessage->pChatServerMessage->toContainerID;
			pUIMessage->eTypeOfEmail = pUIMessage->iNPCEMailID ? kEmailV3Type_Old_NPC : kEmailV3Type_Old_Player;
			pUIMessage->iNumAttachedItems = 0;
			iNum++;
			if (!pUIMessage->pChatServerMessage->bRead)
				pEnt->pPlayer->pUI->bUnreadMail = true;
		}
	}
	//Mail from NPCs
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pEmailV2)
	{
		char *esFormatedSubString = NULL;
		char *esFormatedBodyString = NULL;
		estrCreate(&esFormatedSubString);
		estrCreate(&esFormatedBodyString);
		for (i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); i++)
		{
			EmailV3UIMessage* pUIMessage = eaGetStruct(pppMessagesOut, parse_EmailV3UIMessage, iNum);
			
			estrClear(&esFormatedSubString);
			estrClear(&esFormatedBodyString);

			langFormatGameString(entGetLanguage(pEnt), &esFormatedSubString, pEnt->pPlayer->pEmailV2->mail[i]->subject, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);
			langFormatGameString(entGetLanguage(pEnt), &esFormatedBodyString, pEnt->pPlayer->pEmailV2->mail[i]->body, STRFMT_ENTITY_KEY("Entity", pEnt), STRFMT_END);

			if (pUIMessage->subjectOwned)
				StructFreeString(pUIMessage->subjectOwned);
			pUIMessage->subjectOwned = StructAllocString(esFormatedSubString);
			pUIMessage->subject = pUIMessage->subjectOwned;

			if (pUIMessage->bodyOwned)
				StructFreeString(pUIMessage->bodyOwned);
			pUIMessage->bodyOwned = StructAllocString(esFormatedBodyString);
			pUIMessage->body = pUIMessage->bodyOwned;

			pUIMessage->pMessage = NULL;
			pUIMessage->pChatServerMessage = NULL;
			pUIMessage->pNPCMessage = pEnt->pPlayer->pEmailV2->mail[i];
			pUIMessage->fromName = pUIMessage->pNPCMessage->fromName;
			pUIMessage->sent = pUIMessage->pNPCMessage->sentTime;
			pUIMessage->bRead = pUIMessage->pNPCMessage->bRead;
			pUIMessage->uID = pUIMessage->pNPCMessage->iNPCEMailID;
			pUIMessage->iNPCEMailID = pUIMessage->pNPCMessage->iNPCEMailID;
			pUIMessage->eTypeOfEmail = kEmailV3Type_NPC;
			pUIMessage->iNumAttachedItems = 0;
			iNum++;
			if (!pUIMessage->pNPCMessage->bRead)
				pEnt->pPlayer->pUI->bUnreadMail = true;
		}
		estrDestroy(&esFormatedBodyString);
		estrDestroy(&esFormatedSubString);
	}
	//New mail
	if (pMailData)
	{
		for (i = 0; i < eaSize(&pMailData->eaMessages); i++)
		{
			EmailV3UIMessage* pUIMessage = eaGetStruct(pppMessagesOut, parse_EmailV3UIMessage, iNum);
			pUIMessage->pMessage = pMailData->eaMessages[i];
			pUIMessage->pChatServerMessage = NULL;
			pUIMessage->pNPCMessage = NULL;
			pUIMessage->subject = pUIMessage->pMessage->pchSubject;
			pUIMessage->fromName = pUIMessage->pMessage->pchSenderName;
			pUIMessage->fromHandle = pUIMessage->pMessage->pchSenderHandle;
			pUIMessage->sent = pUIMessage->pMessage->uSent;
			pUIMessage->body = pUIMessage->pMessage->pchBody;
			pUIMessage->bRead = pUIMessage->pMessage->bRead;
			pUIMessage->iNumAttachedItems = eaSize(&pUIMessage->pMessage->ppItems);
			pUIMessage->uID = pUIMessage->pMessage->uID;
			pUIMessage->uLotID = 0;
			pUIMessage->iNPCEMailID = 0;
			pUIMessage->toContainerID = 0;
			pUIMessage->eTypeOfEmail = kEmailV3Type_Player;
			pUIMessage->uLotID = 0;
			iNum++;
			if (!pUIMessage->pMessage->bRead)
				pEnt->pPlayer->pUI->bUnreadMail = true;
		}
	}
	eaSetSizeStruct(pppMessagesOut, parse_EmailV3UIMessage, iNum);
	//	eaQSort(s_eaMessages, EmailV3_SortByDateSent);
}

#include "AutoGen/MailCommon_h_ast.h"
#include "AutoGen/MailCommon_h_ast.c"
//#include "AutoGen/MailCommon_c_ast.c"