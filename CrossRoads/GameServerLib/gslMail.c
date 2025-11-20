#include "MailCommon.h"
#include "MailCommon_h_ast.h"
#include "gslmail.h"
#include "gslmail_h_ast.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntityLib.h"
#include "SharedBankCommon.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "AutoTransDefs.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "NotifyEnum.h"
#include "LoggedTransactions.h"
#include "gslSharedBank.h"
#include "objTransactions.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "guild.h"
#include "gslLogSettings.h"
#include "GamePermissionsCommon.h"
#include "gslMail_old.h"
#include "loggingEnums.h"

#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "autogen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


AUTO_TRANS_HELPER;
enumTransactionOutcome EmailV3_trh_PlayerCanReceiveMessage(ATR_ARGS, ContainerID iSenderID, ATH_ARG NOCONST(Entity)* pRecipientAccountSharedBank, ATH_ARG NOCONST(EmailV3Message)* pMessage, bool bSendErrors)
{
	if (ISNULL(pRecipientAccountSharedBank) || ISNULL(pMessage))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		int i;
		if (NONNULL(pRecipientAccountSharedBank->pEmailV3))
		{
			U32 uInboxSize = 0;
			U32 uAttachments = 0;
			for (i = 0; i < eaSize(&pRecipientAccountSharedBank->pEmailV3->eaMessages); i++)
			{
				if (pRecipientAccountSharedBank->pEmailV3->eaMessages[i]->eTypeOfEmail == kEmailV3Type_Player)
				{
					uInboxSize++;
					if (eaSize(&pRecipientAccountSharedBank->pEmailV3->eaMessages[i]->ppItems) > 0)
						uAttachments++;
				}
			}
			if (uInboxSize >= g_SharedBankConfig.uMaxInboxSize)
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, iSenderID, "Mail_Error_RecipientInboxFull", kNotifyType_MailSendFailed);
				return TRANSACTION_OUTCOME_FAILURE;
			}
			if (uAttachments >= g_SharedBankConfig.uMaxInboxAttachments)
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, iSenderID, "Mail_Error_RecipientAttachmentsFull", kNotifyType_MailSendFailed);
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION 
	ATR_LOCKS(pSender, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(pRecipientAccountSharedBank, ".Pemailv3");
enumTransactionOutcome EmailV3_tr_SendMessageFromPlayer(ATR_ARGS, NOCONST(Entity)* pSender, NOCONST(Entity)* pRecipientAccountSharedBank, EmailV3NewMessageWrapper* pWrapper, const ItemChangeReason *pReason, GameAccountDataExtract* pSenderExtract)
{
	if (ISNULL(pWrapper) || ISNULL(pWrapper->pMessage) || ISNULL(pSender) || ISNULL(pRecipientAccountSharedBank))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		NOCONST(EmailV3Message)* pNCMessage = StructCloneDeConst(parse_EmailV3Message, pWrapper->pMessage);
		
		if (EmailV3_trh_PlayerCanReceiveMessage(ATR_PASS_ARGS, pSender->myContainerID, pRecipientAccountSharedBank, pNCMessage, true) == TRANSACTION_OUTCOME_FAILURE)
			return TRANSACTION_OUTCOME_FAILURE;
		
		if (EmailV3_trh_AddItemsToMessageFromEntInventory(ATR_PASS_ARGS, pNCMessage, pWrapper->eaItemsFromPlayer, pSender, pReason, pSenderExtract) == TRANSACTION_OUTCOME_FAILURE)
			return TRANSACTION_OUTCOME_FAILURE;

		EmailV3_trh_DeliverMessage(ATR_PASS_ARGS, pRecipientAccountSharedBank, pNCMessage);
		
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}

AUTO_TRANSACTION 
	ATR_LOCKS(pRecipientAccountSharedBank, ".Pemailv3.bUnreadMail");
enumTransactionOutcome EmailV3_tr_SetUnreadMailBit(ATR_ARGS, NOCONST(Entity)* pRecipientAccountSharedBank, S32 bSet)
{
	if (ISNULL(pRecipientAccountSharedBank) || ISNULL(pRecipientAccountSharedBank->pEmailV3))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		pRecipientAccountSharedBank->pEmailV3->bUnreadMail = bSet;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pRecipientAccountSharedBank, ".pEmailV3");
enumTransactionOutcome EmailV3_tr_SendMessageFromNPC(ATR_ARGS, NOCONST(Entity)* pRecipientAccountSharedBank, EmailV3NewMessageWrapper* pWrapper)
{
	if (ISNULL(pWrapper) || ISNULL(pWrapper->pMessage) || ISNULL(pRecipientAccountSharedBank))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		NOCONST(EmailV3Message)* pNCMessage = StructCloneDeConst(parse_EmailV3Message, pWrapper->pMessage);

		EmailV3_trh_AddItemsToMessage(ATR_PASS_ARGS, pNCMessage, (NOCONST(Item)**)pWrapper->eaItemsFromNPC);
	
		EmailV3_trh_DeliverMessage(ATR_PASS_ARGS, pRecipientAccountSharedBank, pNCMessage);
	
		return TRANSACTION_OUTCOME_SUCCESS;
	}
}

static void EmailV3_RecipientAccountSharedBankExists_CB(TransactionReturnVal *pReturn, EmailV3NewMessageWrapper* pWrapper)
{
	if (pWrapper && pReturn && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pSender = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pWrapper->uSenderContainerID);

		if(!pSender && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
			pSender = entForClientCmd(pWrapper->uSenderContainerID,pSender);

		if (pSender)
		{
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pSender);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pSender, "Email:SendFromPlayer", NULL);

			AutoTrans_EmailV3_tr_SendMessageFromPlayer(LoggedTransactions_CreateManagedReturnValEnt("EmailV3SendMessage", pSender, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pWrapper->uSenderContainerID, GLOBALTYPE_ENTITYSHAREDBANK, pWrapper->uRecipientAccountID, pWrapper, &reason, pExtract);
		}
	}

	if (pWrapper)
	{
		StructDestroy(parse_EmailV3NewMessageWrapper, pWrapper);
	}
}

static void EmailV3_GetRecipientAccountID_CB(TransactionReturnVal *pReturn, EmailV3NewMessageWrapper* pWrapper)
{
	U32 uiAccountID;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(pReturn, &uiAccountID);

	if (pWrapper && pWrapper->pMessage)
	{
		if (uiAccountID == 0)
		{
			Entity* pSender = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pWrapper->uSenderContainerID);
			//account doesn't exist
			notify_NotifySend(pSender, kNotifyType_MailSendFailed, entTranslateMessageKey(pSender, "Mail_SendFailure_AccountDoesNotExist"), NULL, NULL);
		}
		else
		{
		
			//ignore/silence stuff
			/*
			if (!sender)
				return CHATRETURN_UNSPECIFIED;
			if (!recipient)
				return CHATRETURN_USER_DNE;
			if (recipient->uFlags & CHATUSER_FLAG_BOT)
				return CHATRETURN_USER_DNE;

			//If sender is sending messages too fast, silence them
			if(sender->id != recipient->id && !UserIsAdmin(sender)) {
				if(mailRateLimiter(sender)) {
					return CHATRETURN_USER_PERMISSIONS;
				}
			}
			if (recipient->uFlags & CHATUSER_FLAG_BOT)
				return CHATRETURN_USER_DNE;
			if (userIsIgnoring(recipient, sender) || !isEmailWhitelisted(sender, recipient, chatServerGetCommandLinkID()))
			{
				return CHATRETURN_USER_IGNORING;
			}
			if (userIsSilenced(sender))
			{
				return CHATRETURN_USER_PERMISSIONS;
			}
			*/

			//This is now handled inside the transaction.
			/*
			if (mail->eTypeOfEmail == EMAIL_TYPE_PLAYER && userMailBoxIsFull(recipient))
			{
				return CHATRETURN_MAILBOX_FULL;
			}
			if(pMessage->eTypeOfEmail == EMAIL_TYPE_NPC_FROM_PLAYER)
			{
				// prevent duplicate copies of NPC email (due to multiple syncs at near same time)
				EARRAY_FOREACH_BEGIN(recipient->email, i);
				{
					if (recipient->email[i]->eTypeOfEmail == EMAIL_TYPE_NPC_FROM_PLAYER && 
						recipient->email[i]->iNPCEMailID ==  mail->iNPCEMailID &&
						recipient->email[i]->senderContainerID == mail->toContainerID)
					{
						return CHATRETURN_FWD_NONE;
					}
				}
				EARRAY_FOREACH_END;
			}
			*/
			if (pWrapper->pMessage->eTypeOfEmail == kEmailV3Type_Player)
			{
				SharedBankCBData *cbData = NULL;
				cbData = calloc(sizeof(SharedBankCBData), 1);
				cbData->ownerType = 0;
				cbData->ownerID = 0;
				cbData->accountID = uiAccountID;
				cbData->pUserData = pWrapper;
				cbData->pFunc = EmailV3_RecipientAccountSharedBankExists_CB;

				pWrapper->uRecipientAccountID = uiAccountID;
				RemoteCommand_DBCheckAccountWideContainerExistsWithRestore(objCreateManagedReturnVal(RestoreSharedBank_CB, cbData), GLOBALTYPE_OBJECTDB, 0, uiAccountID, GLOBALTYPE_ENTITYSHAREDBANK);
				return;
			}
			else if (pWrapper->pMessage->eTypeOfEmail == kEmailV3Type_NPC)
			{
				AutoTrans_EmailV3_tr_SendMessageFromNPC(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, uiAccountID, pWrapper);
			}
		}
	}

	if (pWrapper)
	{
		StructDestroy(parse_EmailV3NewMessageWrapper, pWrapper);
	}
}

void EmailV3_SendMessage_UserCanContactUserCB(TransactionReturnVal *pReturn, EmailV3NewMessageWrapper* pWrap)
{
	if (pWrap)
	{
		RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
			objCreateManagedReturnVal(EmailV3_GetRecipientAccountID_CB, pWrap),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountGetHandle(pWrap->pchRecipientHandle));
	}
}

// Function used by newer shards
void EmailV3_SendMessageInternal(EmailV3Message* pMessage,
								Entity* pSender, 
								const char* pchRecipientHandle, 
								EmailV3SenderItem** eaItemsFromPlayer, 
								Item** eaItemsFromNPC)
{
	NOCONST(EmailV3Message)* pNCMessage = CONTAINER_NOCONST(EmailV3Message, pMessage);
	EmailV3NewMessageWrapper* pWrap = StructCreate(parse_EmailV3NewMessageWrapper);

	eaCopyStructs(&eaItemsFromPlayer, &pWrap->eaItemsFromPlayer, parse_EmailV3SenderItem);
	//does NOT duplicate items
	eaCopy(&pWrap->eaItemsFromNPC, &eaItemsFromNPC);

	pWrap->pMessage = pMessage;
	pWrap->uSenderContainerID = pSender ? pSender->myContainerID : 0;
//	pWrap->pchRecipientHandle = StructAllocString(pchRecipientHandle);

	RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
		objCreateManagedReturnVal(EmailV3_GetRecipientAccountID_CB, pWrap),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountGetHandle(pchRecipientHandle));


//	RemoteCommand_chatServer_UserCanContactUser(
//		objCreateManagedReturnVal(EmailV3_SendMessage_UserCanContactUserCB, pWrap),
//		GLOBALTYPE_CHATSERVER, 0, pSender->pPlayer->publicAccountName, accountGetHandle(pchRecipientHandle));

}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_SendPlayerEmail(const char* pchSubject,
	const char* pchBody, 
	Entity* pSender,
	const char* pchRecipientHandle, 
	EmailV3SenderItemsWrapper* pWrapper)
{
	if (pSender)
	{
		EmailV3Message* pMessage = EmailV3_CreateNewMessage(pchSubject, pchBody, pSender, entGetLocalName(pSender), pSender->pPlayer ? pSender->pPlayer->publicAccountName : NULL);
		EmailV3_SendMessageInternal(pMessage, pSender, pchRecipientHandle, pWrapper ? pWrapper->eaItemsFromPlayer : NULL, NULL);
	}
}

void EmailV3_SendNPCEmail(const char* pchSubject,
	const char* pchBody, 
	const char* pchSenderName, 
	const char* pchRecipientHandle, 
	Item** eaNPCItems)
{
	EmailV3Message* pMessage = EmailV3_CreateNewMessage(pchSubject, pchBody, NULL, pchSenderName, NULL);
	EmailV3_SendMessageInternal(pMessage, NULL, pchRecipientHandle, NULL, eaNPCItems);
}

AUTO_TRANS_HELPER;
NOCONST(EmailV3Message)* EmailV3_trh_GetMessageByID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pSharedBank, U32 id)
{
	if (NONNULL(pSharedBank->pEmailV3))
	{
		return (NOCONST(EmailV3Message)*)eaIndexedGetUsingInt(&pSharedBank->pEmailV3->eaMessages, id);
	}
	return NULL;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Pugckillcreditlimit, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
	ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_TakeAttachedItem(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pSharedBank, int iMailID, int iItem, GameAccountDataExtract* pExtract)
{
	NOCONST(EmailV3Message)* pMessage =  EmailV3_trh_GetMessageByID(ATR_PASS_ARGS, pSharedBank, iMailID);
	if (pMessage && pMessage->ppItems)
	{
		Item* pItem = CONTAINER_RECONST(Item, pMessage->ppItems[iItem]);
		ItemDef* pDef = GET_REF(pItem->hItem);
		InvBagIDs eBag = inv_trh_GetBestBagForItemDef(pEnt, pDef, pItem->count, true, pExtract);

		if (inv_AddItem(ATR_PASS_ARGS, pEnt, NULL, eBag, -1, pItem, pDef->pchName, 0, NULL, pExtract) == TRANSACTION_OUTCOME_FAILURE)
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, "Mail.FailedToTakeItems", kNotifyType_InventoryFull);
			return TRANSACTION_OUTCOME_FAILURE;
		}

		StructDestroyNoConst(parse_Item, pMessage->ppItems[iItem]);
		eaRemove(&pMessage->ppItems, iItem);

		if (eaSize(&pMessage->ppItems) <= 0)
		{
			eaDestroy(&pMessage->ppItems);
			pMessage->ppItems = NULL;
		}

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_TakeAttachedItem(Entity* pEnt, int iMailID, int iItem)
{
	if (pEnt && pEnt->pPlayer)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		AutoTrans_EmailV3_tr_TakeAttachedItem(LoggedTransactions_CreateManagedReturnValEnt("EmailV3TakeItem", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, iMailID, iItem, pExtract);
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Pugckillcreditlimit, .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
	ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_TakeAllAttachedItems(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity)* pSharedBank, int iMailID, GameAccountDataExtract* pExtract)
{
	NOCONST(EmailV3Message)* pMessage =  EmailV3_trh_GetMessageByID(ATR_PASS_ARGS, pSharedBank, iMailID);
	if (pMessage && pMessage->ppItems)
	{
		int i;
		for (i = 0; i < eaSize(&pMessage->ppItems); i++)
		{
			Item* pItem = CONTAINER_RECONST(Item, pMessage->ppItems[i]);
			ItemDef* pDef = GET_REF(pItem->hItem);
			InvBagIDs eBag = inv_trh_GetBestBagForItemDef(pEnt, pDef, pItem->count, true, pExtract);

			if (inv_AddItem(ATR_PASS_ARGS, pEnt, NULL, eBag, -1, pItem, pDef->pchName, 0, NULL, pExtract) == TRANSACTION_OUTCOME_FAILURE)
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, "Mail_FailedToTakeItems", kNotifyType_InventoryFull);
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}

		eaDestroyStructNoConst(&pMessage->ppItems, parse_Item);
		pMessage->ppItems = NULL;

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_TakeAllAttachedItems(Entity* pEnt, int iMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		AutoTrans_EmailV3_tr_TakeAllAttachedItems(LoggedTransactions_CreateManagedReturnValEnt("EmailV3TakeAll", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, iMailID, pExtract);
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_DeleteMessage(ATR_ARGS, NOCONST(Entity)* pSharedBank, int iOwnerContainerID, int iMailID)
{
	if (NONNULL(pSharedBank->pEmailV3))
	{
		int i;
		NOCONST(EmailV3Message)* pMessage = eaIndexedRemoveUsingInt(&pSharedBank->pEmailV3->eaMessages, iMailID);
		for (i = 0; i < eaSize(&pMessage->ppItems); i++)
		{
			ItemDef* pDef = SAFE_GET_REF(pMessage->ppItems[i], hItem);
			if (pDef && (pDef->flags & kItemDefFlag_CantDiscard))
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, GLOBALTYPE_ENTITYPLAYER, iOwnerContainerID, "Mail_CannotDiscard", kNotifyType_MailSendFailed);
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}

		StructDestroyNoConst(parse_EmailV3Message, pMessage);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_DeleteMessage(Entity* pEnt, int iMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		AutoTrans_EmailV3_tr_DeleteMessage(LoggedTransactions_CreateManagedReturnValEnt("EmailV3DeleteMessage", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, pEnt->myContainerID, iMailID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pSharedBank, ".pEmailV3.eaMessages[]");
enumTransactionOutcome EmailV3_tr_MarkMessageAsRead(ATR_ARGS, NOCONST(Entity)* pSharedBank, int iMailID)
{
	NOCONST(EmailV3Message)* pMessage =  EmailV3_trh_GetMessageByID(ATR_PASS_ARGS, pSharedBank, iMailID);
	
	if (!pMessage)
		return TRANSACTION_OUTCOME_FAILURE;

	pMessage->bRead = true;

	return TRANSACTION_OUTCOME_SUCCESS;
}


void EmailV3_MarkMessageAsRead_CB(TransactionReturnVal *pReturn, ContainerID* cbData)
{
	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		bool bHasUnread = false;
		Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, *cbData);
		Entity* pBank = GET_REF(pEnt->pPlayer->hSharedBank);
		int i;
		if (pBank)
		{
			EmailV3* pEmail = EmailV3_GetSharedBankMail(pBank);
			for (i = 0; i < eaSize(&pEmail->eaMessages); i++)
			{
				if (!pEmail->eaMessages[i]->bRead)
					bHasUnread = true;
			}
			if (bHasUnread != pEmail->bUnreadMail)
				AutoTrans_EmailV3_tr_SetUnreadMailBit(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pBank->myContainerID, bHasUnread);
		}
	}
	if (cbData)
		free(cbData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void EmailV3_MarkMessageAsRead(Entity* pEnt, int iMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		ContainerID* pID = calloc(1, sizeof(ContainerID));
		*pID = pEnt->myContainerID;
		AutoTrans_EmailV3_tr_MarkMessageAsRead(LoggedTransactions_CreateManagedReturnValEnt("EmailV3MarkMessageAsRead", pEnt, EmailV3_MarkMessageAsRead_CB, pID), GetAppGlobalType(), GLOBALTYPE_ENTITYSHAREDBANK, pEnt->pPlayer->accountID, iMailID);
	}
}
// This could potentially cause a SHITLOAD of transactions all at once when used by members of a large guild. Leaving it alone on orders from Jeff W.
AUTO_COMMAND ACMD_PRIVATE ACMD_NAME(guild_SendMail) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail);
void EmailV3_SendGuildMail(Entity *pEnt, const char *pcSubject, const ACMD_SENTENCE pcBody)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	GuildMember *pMember = pGuild ? guild_FindMemberInGuild(pEnt, pGuild) : NULL;
	if (pMember) {
		// If the player is on a trial account, they can't send guild mail
		if (gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_SEND_MAIL)) {
			ClientCmd_gclMailSentConfirm(pEnt, false, entTranslateMessageKey(pEnt, "Chat_NoTrialGuildMail"));
			return;
		}

		if (!guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildMail)) {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			entFormatGameMessageKey(pEnt, &estrBuffer, "ChatServer_NoGuildMailPermission", STRFMT_END);
			ClientCmd_gclMailSentConfirm(pEnt, false, estrBuffer);
			estrDestroy(&estrBuffer);
			return;
		}

		if (timeSecondsSince2000() - pGuild->iLastGuildMailTime < GUILD_MAIL_TIME) {
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			entFormatGameMessageKey(pEnt, &estrBuffer, "ChatServer_GuildMailTooRecent", STRFMT_END);
			ClientCmd_gclMailSentConfirm(pEnt, false, estrBuffer);
			estrDestroy(&estrBuffer);
			return;
		}
		else 
		{
			EntityRef *pEntRef = NULL;
			int i;
			EmailV3NewMessageWrapper wrapper = {0};
			StashTable stAccountIDs = stashTableCreateInt(eaSize(&pGuild->eaMembers));
			
			wrapper.pMessage = EmailV3_CreateNewMessage(pcSubject, pcBody, pEnt, entGetLocalName(pEnt), pEnt->pPlayer ? pEnt->pPlayer->publicAccountName : NULL);
			wrapper.uSenderContainerID = pEnt->myContainerID;
			for (i = 0; i < eaSize(&pGuild->eaMembers); i++) {
				//Only send an email to each accountID once, regardless of how many of their characters are in the guild.
				if (stashIntAddInt(stAccountIDs, pGuild->eaMembers[i]->iAccountID, pGuild->eaMembers[i]->iAccountID, false))
				{
					if (gbEnableGamePlayDataLogging)
					{
						char *estrLog = NULL;
						gslMailLogTo(&estrLog, pGuild->eaMembers[i]->pcAccount, pcSubject);
						entLog(LOG_MAIL_GSL, pEnt, "Mail_Guild", "%s", estrLog);
						estrDestroy(&estrLog);
					}
					wrapper.uRecipientAccountID = pGuild->eaMembers[i]->iAccountID;
					AutoTrans_EmailV3_tr_SendMessageFromPlayer(LoggedTransactions_CreateManagedReturnValEnt("EmailV3SendMessage", pEnt, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, GLOBALTYPE_ENTITYSHAREDBANK, wrapper.uRecipientAccountID, &wrapper, NULL, NULL);
				}
			}

			stashTableDestroy(stAccountIDs);

			AutoTrans_trUpdateGuildMailTimer(NULL, GetAppGlobalType(), GLOBALTYPE_GUILD, pGuild->iContainerID);

			StructDestroy(parse_EmailV3Message, wrapper.pMessage);
		}
	} else {
		ClientCmd_gclMailSentConfirm(pEnt, false, entTranslateMessageKey(pEnt, "ChatServer_MalformedMail"));
	}
}

#include "AutoGen/gslmail_h_ast.c"
