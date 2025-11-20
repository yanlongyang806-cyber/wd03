#include "gslChat.h"
#include "gslMail_Old.h"
#include "gslLogSettings.h"
#include "entityMailCommon.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "chatCommonStructs_h_ast.h"
#include "chatCommon_h_ast.h"
#include "AuctionLot.h"
#include "AuctionLot_h_ast.h"
#include "accountCommon.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"

#include "EntityLib.h"
#include "gslTransactions.h"
#include "inventoryCommon.h"
#include "contact_common.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "SavedPetCommon.h"
#include "Guild.h"
#include "gslAuction.h"
#include "GamePermissionsCommon.h"

#include "utilitiesLib.h"
#include "earray.h"
#include "GameStringFormat.h"
#include "logging.h"
#include "strings_opt.h"
#include "StringUtil.h"
#include "GamePermissionsCommon.h"
#include "gslMailNPC.h"

#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

#include "Entity.h"
#include "Entity_h_ast.h"
#include "SharedBankCommon.h"
#include "Player_h_ast.h"

#include "LoggedTransactions.h"
#include "gslMail_Old_c_ast.h"
#include "Guild_h_ast.h"

// ------------------------------------------------------------
// Mail Functions

#define MAIL_CONFIRM_TIMEOUT 30 // 30 seconds
AUTO_STRUCT;
typedef struct MailSendQueue
{
	S32 uLotID; AST(KEY)
	EntityRef entRef;
	U32 uSendTime;
} MailSendQueue;

static MailSendQueue **sppMailItemSendQueue = NULL;

static const char * gslGetVShardQualifiedName(void)
{
	static char name[256] = "";
	if (!*name)
	{
		GetVShardQualifiedName(name, ARRAY_SIZE_CHECKED(name), GAMESERVER_VSHARD_ID);
	}
	return name;
}

ChatMailStruct *gslMailCreateMail (Entity *sender, const char *pchSubject, const char *pchBody)
{
	ChatMailStruct *mail = StructCreate(parse_ChatMailStruct);
	mail->subject = StructAllocString(pchSubject);
	mail->body = StructAllocString(pchBody);
	mail->shardName = StructAllocString(gslGetVShardQualifiedName());

	if (sender)
	{
		mail->fromID = entGetAccountID(sender);
		mail->fromName = StructAllocString(entGetLocalName(sender));
	}
	return mail;
}

void gslMail_InitializeMailEx(SA_PARAM_NN_VALID ChatMailStruct *mail, Entity *sender, const char *pchSubject, const char *pchBody, 
									 const char *fromName, EMailType eType, S32 iNPCEMailID, U32 uFutureSendTime)
{
	mail->subject = StructAllocString(pchSubject);
	mail->body = StructAllocString(pchBody);
	mail->shardName = StructAllocString(gslGetVShardQualifiedName());
	mail->eTypeOfEmail = eType;
	mail->iNPCEMailID = iNPCEMailID;
	mail->uFutureSendTime = uFutureSendTime;
	mail->sent = uFutureSendTime;

	if (sender)
	{
		mail->fromID = entGetAccountID(sender);
		if (iNPCEMailID > 0)
			mail->toContainerID = entGetContainerID(sender);
	}
	if (sender && eType == EMAIL_TYPE_PLAYER && !fromName)
		mail->fromName = StructAllocString(entGetLocalName(sender));
	else
		mail->fromName = StructAllocString(fromName);
}

static void MailRemoveFromQueue(S32 uLotID)
{
	int idx = eaIndexedFindUsingInt(&sppMailItemSendQueue, uLotID);
	if (idx >= 0)
	{
		MailSendQueue *queue = sppMailItemSendQueue[idx];
		eaRemove(&sppMailItemSendQueue, idx);
		StructDestroy(parse_MailSendQueue, queue);
	}
}

void gslMailLogTo(char **pchOut, const char *toHandle, const char *subject)
{
	char *pescSub = NULL;
	
	estrAppendEscaped(&pescSub, subject);
	
	estrPrintf(pchOut, "P[@%s], Sub: ", toHandle);
	estrConcatChar(pchOut,'"');
	estrConcatf(pchOut, "%s", pescSub);
	estrConcatChar(pchOut,'"');
	
	estrDestroy(&pescSub);
}

static void gslMailLogLot(char **pchOut, const MailItemStruct *pMail)
{
	estrConcatf(pchOut, ", Lot: %d, Items: %s, Item Descriptions: %s",
		pMail->uLotID, pMail->pchItemString, pMail->pFullItemString);
}

static void MailCancelAuctionCB(TransactionReturnVal *returnStruct, MailSendQueue* mail)
{
	Entity *pEnt = mail ? entFromEntityRefAnyPartition(mail->entRef) : NULL;
	AuctionLot *pLot = NULL;
	S32 uLotID = mail ? mail->uLotID : 0;

	if(pEnt && RemoteCommandCheck_Auction_GetLot(returnStruct, &pLot) == TRANSACTION_OUTCOME_SUCCESS)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		U32* eaPets = NULL;
		U32* eaiContainerItemPets = NULL;
		ItemChangeReason reason = {0};

		ea32Create(&eaPets);
		if (gslAuction_LotHasUniqueItem(pLot))
		{
			Entity_GetPetIDList(pEnt, &eaPets);
		}
		gslAuction_GetContainerItemPetsFromLot(pLot, &eaiContainerItemPets);

		inv_FillItemChangeReason(&reason, pEnt, "Mail:Cancel", NULL);

		AutoTrans_auction_tr_CancelMail(
			NULL, GLOBALTYPE_GAMESERVER,
			entGetType(pEnt), entGetContainerID(pEnt),
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
			GLOBALTYPE_AUCTIONLOT, uLotID,
			GLOBALTYPE_ENTITYSAVEDPET, &eaiContainerItemPets,
			&reason, pExtract);

		if(eaiContainerItemPets)
			eaiDestroy(&eaiContainerItemPets);

		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_MAIL_GSL, pEnt, "Mail_With_Items_Cancelled", "Lot %d", uLotID);
		}
		ea32Destroy(&eaPets);
	}

	if (uLotID) {
		MailRemoveFromQueue(uLotID);
	}
}

static void MailCancelAuction(Entity *pEnt, S32 uLotID)
{
	if (pEnt && uLotID)
	{
		MailSendQueue* pQueue = StructCreate(parse_MailSendQueue);
		pQueue->entRef = entGetRef(pEnt);
		pQueue->uLotID = uLotID;
		RemoteCommand_Auction_GetLot(
			objCreateManagedReturnVal(MailCancelAuctionCB, pQueue), 
			GLOBALTYPE_AUCTIONSERVER, 0, uLotID);
	}
}

void MailItemQueueTick(void)
{
	int i, size;
	size = eaSize(&sppMailItemSendQueue);
	if (size)
	{
		U32 uTime = timeSecondsSince2000();
		for (i=size-1; i>=0; i--)
		{
			MailSendQueue *queue = sppMailItemSendQueue[i];
			if (queue->uSendTime + MAIL_CONFIRM_TIMEOUT < uTime)
			{
				Entity *pEnt = entFromEntityRefAnyPartition(queue->entRef);
				if (pEnt)
				{
					// TODO translate
					ClientCmd_gclMailSentConfirm(pEnt, false, "Mail send timed out");
				}
				MailCancelAuction(pEnt, queue->uLotID);
			}
		}
	}
}

AUTO_COMMAND_REMOTE;
void ServerChat_SendMailConfirm(U32 uID, bool bMailSent, U32 uiAuctionLot, const char *errorString)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	if (pEnt)
		ClientCmd_gclMailSentConfirm(pEnt, bMailSent, errorString);
	if (!bMailSent)
		MailCancelAuction(pEnt, uiAuctionLot);
	else if(uiAuctionLot != 0)
	{
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_MAIL_GSL, pEnt, "Mail_With_Items_Confirmed", "Lot %d", uiAuctionLot);
		}
		MailRemoveFromQueue(uiAuctionLot);
	}
}

// Send a mail to someone.
AUTO_COMMAND ACMD_PRIVATE ACMD_NAME(MailSend) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail) ACMD_HIDE;
void ServerChat_SendMail(Entity *pEnt, const char * toHandleOrCharacter, const char * subject, const ACMD_SENTENCE body)
{
	if (pEnt && pEnt->pPlayer && toHandleOrCharacter && *toHandleOrCharacter)
	{
		// If the player is on a trial account, they can't mail non-friends
		if (gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_SEND_MAIL)) {
			S32 i = 0;
			if (SAFE_MEMBER4(pEnt, pPlayer, pUI, pChatState, eaFriends)) {
				for (; i < eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends); i++) {
					ChatPlayerStruct *pFriend = pEnt->pPlayer->pUI->pChatState->eaFriends[i];
					if (!ChatFlagIsFriend(pFriend->flags))
						continue;
					if (!stricmp(NULL_TO_EMPTY(pFriend->chatHandle), accountGetHandle(NULL_TO_EMPTY(toHandleOrCharacter)))) {
						break;
					}
				}
			}
			if (!SAFE_MEMBER4(pEnt, pPlayer, pUI, pChatState, eaFriends) || i == eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends)) {
				ClientCmd_gclMailSentConfirm(pEnt, false, entTranslateMessageKey(pEnt, "Chat_NoTrialMail"));
				return;
			}
		}
		
		//const char *start = strchr(toHandleOrCharacter, '@');
		//if (start)
		//	toHandleOrCharacter = start;
		//while (*toHandleOrCharacter == '@')
		//	toHandleOrCharacter++;
		toHandleOrCharacter = accountGetHandle(toHandleOrCharacter);
		if (!GameServer_IsGlobalChatOnline())
		{
			char *error = NULL;
			entFormatGameMessageKey(pEnt, &error, "ChatServer_GlobalOffline", STRFMT_END);
			if (!error)
				estrCopy2(&error, "Global Chat Server is offline.");
			ClientCmd_gclMailSentConfirm(pEnt, false, error);
			estrDestroy(&error);
		}
		else
		{
			ChatMailStruct mail = {0};
			gslMail_InitializeMailEx(&mail, pEnt, subject, body, NULL, EMAIL_TYPE_PLAYER, 0, 0);

			if (gbEnableGamePlayDataLogging) {
				char *pLog = NULL;
				gslMailLogTo(&pLog, toHandleOrCharacter, subject);
				entLog(LOG_MAIL_GSL, pEnt, "Mail_No_Items", "%s", pLog);
				estrDestroy(&pLog);
			}

			RemoteCommand_ChatServerSendMailByHandle_v2(GLOBALTYPE_CHATSERVER, 0, toHandleOrCharacter, &mail);
			StructDeInit(parse_ChatMailStruct, &mail);
		}
	}
	else
	{
		ClientCmd_gclMailSentConfirm(pEnt, false, entTranslateMessageKey(pEnt, "ChatServer_MalformedMail"));
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Ilastguildmailtime");
enumTransactionOutcome trUpdateGuildMailTimer(ATR_ARGS, NOCONST(Guild) *pGuildContainer)
{
	pGuildContainer->iLastGuildMailTime = timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}


// Send a mail to a player from an NPC (normally a send from self but with different rules like no reply)
AUTO_COMMAND ACMD_PRIVATE ACMD_NAME(NPCMailSend) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Mail);
void ServerChat_NPCSendMail(Entity *pEnt, const char * toHandleOrCharacter,const char *fromName, const char * subject, U32 futureTime, const ACMD_SENTENCE body)
{
	if (pEnt && pEnt->pPlayer && toHandleOrCharacter && *toHandleOrCharacter && fromName)
	{
		ChatMailStruct mail = {0};
		const char *start = strchr(toHandleOrCharacter, '@');
		if (start)
			toHandleOrCharacter = start;
		while (*toHandleOrCharacter == '@')
			toHandleOrCharacter++;

		gslMail_InitializeMailEx(&mail, pEnt, subject, body, fromName, EMAIL_TYPE_NPC_NO_REPLY, 0, futureTime);
		RemoteCommand_ChatServerSendMailByHandle_v2(GLOBALTYPE_CHATSERVER, 0, toHandleOrCharacter, &mail);
		StructDeInit(parse_ChatMailStruct, &mail);
	}
}

static void ServerChat_ItemMailFailHandler(EntityRef entRef, U32 uAuctionLotID, const char *msgkey)
{
	Entity *pEnt = entFromEntityRefAnyPartition(entRef);
	if (pEnt)
	{
		char *error = NULL;
		entFormatGameMessageKey(pEnt, &error, msgkey, STRFMT_END);
		if (!error)
			estrCopy2(&error, "Chat Server is offline.");
		ClientCmd_gclMailSentConfirm(pEnt, false, error);
		estrDestroy(&error);
	}
	else
	{
		//Transaction to add it back to the entity offline?
	}
	MailCancelAuction(pEnt, uAuctionLotID);
}

static void ServerChat_MailWithAuctionLotCallback(TransactionReturnVal *pReturn, MailItemStruct *mail)
{
	if( pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS )
	{
		ServerChat_ItemMailFailHandler(mail->entRef, mail->uLotID, "ChatServer_Offline");
	}
	else 
	{
		// does nothing, response is received through ServerChat_ItemMailResponse
	}
	StructDestroy(parse_MailItemStruct, mail);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ServerChat_ItemMailResponse(ContainerID containerID, U32 uAuctionLotID, char *error, int iOtherError)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, containerID);
	if (error && *error)
	{
		if (pEnt)
		{
			ClientCmd_gclMailSentConfirm(pEnt, false, error);
			MailCancelAuction(pEnt, uAuctionLotID);
		}
	}
	else if (iOtherError)
	{
		if (pEnt)
		{
			switch (iOtherError)
			{
			case CHATRETURN_DISCONNECTED:
				ServerChat_ItemMailFailHandler(entGetRef(pEnt), uAuctionLotID, "ChatServer_Offline");
				MailCancelAuction(pEnt, uAuctionLotID);
			xdefault:
				ServerChat_ItemMailFailHandler(entGetRef(pEnt), uAuctionLotID, "ChatServer_SendMailFail");
				MailCancelAuction(pEnt, uAuctionLotID);
			}
		}
	}
	else
	{
		ClientCmd_gclMailSentConfirm(pEnt, true, error);
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_MAIL_GSL, pEnt, "Mail_With_Items_Confirmed", "Lot %d", uAuctionLotID);
		}
		MailRemoveFromQueue(uAuctionLotID);
	}
}

static void ServerChat_SendMailWithAuctionLot(Entity *pEnt, MailItemStruct *mailItem)
{
	if(pEnt && pEnt->pPlayer && mailItem)
	{
		const char * toHandleOrCharacter = mailItem->mail->toName;
		ChatMailStruct *mail = mailItem->mail;
		
		if(toHandleOrCharacter && *toHandleOrCharacter)
		{
			while (*toHandleOrCharacter == '@')
				toHandleOrCharacter++;
			if (!GameServer_IsGlobalChatOnline())
			{
				char *error = NULL;
				entFormatGameMessageKey(pEnt, &error, "ChatServer_GlobalOffline", STRFMT_END);
				if (!error)
					estrCopy2(&error, "Global Chat Server is offline.");
				ClientCmd_gclMailSentConfirm(pEnt, false, error);
				estrDestroy(&error);
				MailCancelAuction(pEnt, mail->uLotID);
			}
			else
			{
				char *pLog = NULL;
				MailItemStruct *pNewMail = StructCreate(parse_MailItemStruct);
				pNewMail->mail = StructClone(parse_ChatMailStruct, mail);
				pNewMail->entRef = entGetRef(pEnt);
				pNewMail->uLotID = mail->uLotID;

				if (mail->uLotID)
				{
					MailSendQueue *pQueue = StructCreate(parse_MailSendQueue);
					pQueue->uLotID = mail->uLotID;
					pQueue->entRef = pNewMail->entRef;
					pQueue->uSendTime = timeSecondsSince2000();
					if (!sppMailItemSendQueue)
					{
						eaCreate(&sppMailItemSendQueue);
						eaIndexedEnable(&sppMailItemSendQueue, parse_MailSendQueue);
					}
					eaIndexedAdd(&sppMailItemSendQueue, pQueue);
				}
				
				gslMailLogTo(&pLog, toHandleOrCharacter, mail->subject);
				gslMailLogLot(&pLog, mailItem);
				if (gbEnableGamePlayDataLogging) {
					entLog(LOG_MAIL_GSL, pEnt, "Mail_With_Items", "%s", pLog);
				}
				estrDestroy(&pLog);
				
				RemoteCommand_ChatServerSendMailItemsByHandle_v2(objCreateManagedReturnVal(ServerChat_MailWithAuctionLotCallback, pNewMail), GLOBALTYPE_CHATSERVER, 0, 
					entGetContainerID(pEnt), toHandleOrCharacter, mail);
			}
		}
	}
}

// Check for new mail.
AUTO_COMMAND ACMD_NAME(MailGet) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Mail);
void ServerChat_GetMail(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerGetMail(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
	}
}

// Get mail by page number + page count
AUTO_COMMAND ACMD_NAME(MailGetPage) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Mail) ACMD_PRIVATE;
void ServerChat_GetMailPage(Entity *pEnt, int iPage, int iPageSize)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerGetMailPaged(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, iPage, iPageSize);
	}
}

// Check for unread mail.
AUTO_COMMAND ACMD_NAME(MailCheck) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Mail);
void ServerChat_CheckMail(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerCheckMail(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ServerChat_PushNewMail(U32 uID, ChatMailStruct *mail)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	if(pEnt)
	{
		ANALYSIS_ASSUME(pEnt != NULL);
		if (( mail->eTypeOfEmail == EMAIL_TYPE_PLAYER
			|| mail->toContainerID == entGetContainerID(pEnt) 
			|| !mail->toContainerID) )
		{
			ClientCmd_gclMailPushNewMail(pEnt, mail);
		}
	}
}

// Remove NPC mail from the chat server
static void RemoveOldNPCEmail(Entity *pEnt, ChatMailList *mailList, S32 iIndex)
{
	RemoteCommand_ChatServerDeleteMail(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, mailList->mail[iIndex]->uID);
	StructDestroy(parse_ChatMailStruct, eaRemove(&mailList->mail, iIndex));
}

static void AddNpcMailForClient(Entity *pEntity, ChatMailList *mailList)
{
	S32 i;
	char *esFormatedSubString = NULL;
	char *esFormatedBodyString = NULL;
	U32 tm = timeSecondsSince2000();

	for(i = 0; i < eaSize(&pEntity->pPlayer->pEmailV2->mail); ++i)
	{
		if(tm >= pEntity->pPlayer->pEmailV2->mail[i]->sentTime)
		{
			ChatMailStruct *pMail = StructCreate(parse_ChatMailStruct);

			if(gslMailNPC_BuildChatMailStruct(pEntity, i, pMail))
			{
				eaPush(&mailList->mail, pMail);
			}
			else
			{
				// something went wrong, destroy struct
				StructDestroy(parse_ChatMailStruct, pMail);
			}

			estrDestroy(&esFormatedSubString);
			estrDestroy(&esFormatedBodyString);

		}
	}
}

AUTO_COMMAND_REMOTE;
void ServerChat_RefreshClientMailListReturn(U32 uID, ChatMailList *mailList)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	U32 uLastID = 0;
	if(mailList && mailList->mail && pEnt)
	{
		S32 i;
		for(i=eaSize(&mailList->mail)-1; i>=0; i--)
		{
			if(mailList->mail[i]->eTypeOfEmail == EMAIL_TYPE_NPC_FROM_PLAYER)
			{
				// does a struct destroy on the index i
				// Tells the mails server to delete this npcemail
				RemoveOldNPCEmail(pEnt, mailList, i);
			}
		}

		AddNpcMailForClient(pEnt, mailList);
	}

	if(pEnt)
	{
		ClientCmd_gclMailRefresh(pEnt, mailList);
	}
}

AUTO_COMMAND_REMOTE;
void ServerChat_RefreshClientUnreadMailBitReturn(U32 uID, bool hasUnreadMail)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);

	bool bUnread = gslMailNpc_HasUnreadMail(pEnt, false);

	ClientCmd_gclSetUnreadMailBit(pEnt, hasUnreadMail | bUnread);
}

AUTO_COMMAND_REMOTE;
void ServerChat_MailDeleteConfirmation(U32 uID, U32 uMailID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	ClientCmd_gclMailDeletedConfirm(pEnt, uMailID);
}

// Delete a mail message.
AUTO_COMMAND ACMD_PRIVATE ACMD_NAME(MailDelete) ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail);
void ServerChat_DeleteMail(Entity *pEnt, U32 uMailID)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_ChatServerDeleteMail(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, uMailID);
	}
}

AUTO_COMMAND_REMOTE;
void ServerChat_MailReadConfirmation(U32 uID, U32 uMailID, bool bRead)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	ClientCmd_gclMailReadConfirm(pEnt, uMailID);
}

AUTO_COMMAND_REMOTE;
void ServerChat_MailReadExpireUpdate(U32 uID, U32 uMailID, U32 uTimeLeft)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uID);
	ClientCmd_gclMailReadUpdate(pEnt, uMailID, uTimeLeft);
}

// Mark a mail message as read. DO NOT CALL THIS DIRECTLY FROM UI
AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail);
void ServerChat_MarkMailRead(Entity *pEnt, U32 uMailID, S32 iNpcMailId)
{
	if (pEnt && pEnt->pPlayer)
	{
		if(iNpcMailId > 0)
		{
			gslMailNPC_MarkRead(pEnt, iNpcMailId, true);
		}
		else
		{
			RemoteCommand_ChatServerSetMailRead(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, uMailID);
		}
	}
}

////////////////////////////
// Item Mail

static void ServerChat_AcceptItemCallback(TransactionReturnVal *pReturn, MailItemStruct *mail)
{
	Entity *pEnt = entFromEntityRefAnyPartition(mail->entRef);

	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		if (gbEnableGamePlayDataLogging)
		{
			char *pRetString = NULL;
			S32 i;
		
			estrPrintf(&pRetString, "Failed to Get items from lot %d", mail->uLotID);
		
			for(i = 0; i < pReturn->iNumBaseTransactions; ++i)
			{
				if(pReturn->pBaseReturnVals[i].eOutcome != TRANSACTION_OUTCOME_SUCCESS)
				{
					estrConcatf(&pRetString, ": %s", pReturn->pBaseReturnVals[i].returnString);
				}
			}
		
			entLog(LOG_MAIL_GSL, pEnt, "mail", "%s", pRetString);
			estrDestroy(&pRetString);
		}
	}
	else
	{
		AuctionLot Mail = {0};
		ClientCmd_gclMail_MailGetItemReturn(pEnt, mail->uLotID, &Mail);
		objRequestContainerDestroy(NULL, GLOBALTYPE_AUCTIONLOT, mail->uLotID, GLOBALTYPE_AUCTIONSERVER, 0);
	}

	StructDestroy(parse_MailItemStruct, mail);
}

static void ServerChat_MailTakeItemsCallback(TransactionReturnVal *returnStruct, MailItemStruct *mail)
{
	Entity *pEnt = entFromEntityRefAnyPartition(mail->entRef);
	AuctionLot *pLot = NULL;

	if(pEnt && RemoteCommandCheck_Auction_GetLot(returnStruct, &pLot) == TRANSACTION_OUTCOME_SUCCESS)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		U32* eaiContainerItemPets = NULL;
		U32* eaPets = NULL;
		ItemChangeReason reason = {0};
		TransactionReturnVal* returnVal;

		ea32Create(&eaPets);
		if (gslAuction_LotHasUniqueItem(pLot))
		{
			Entity_GetPetIDList(pEnt, &eaPets);
		}
		gslAuction_GetContainerItemPetsFromLot(pLot, &eaiContainerItemPets);
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("AcceptMailedAuction", pEnt, ServerChat_AcceptItemCallback, mail);

		inv_FillItemChangeReason(&reason, pEnt, "Mail:TakeItems", NULL);

		AutoTrans_auction_tr_AcceptMailedAuction(returnVal, GLOBALTYPE_GAMESERVER,
			entGetType(pEnt), entGetContainerID(pEnt), 
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets, 
			GLOBALTYPE_AUCTIONLOT, mail->uLotID, 
			GLOBALTYPE_ENTITYSAVEDPET, &eaiContainerItemPets,
			&reason, pExtract);

		if(eaiContainerItemPets)
			eaiDestroy(&eaiContainerItemPets);
		ea32Destroy(&eaPets);
	}

	if(pLot)
	{
		StructDestroySafe(parse_AuctionLot, &pLot);
	}

}

// Transfer items from the given mail item lot to your inventory.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Mail) ACMD_NAME(MailAcceptItems) ACMD_PRIVATE;
void ServerChat_MailAcceptItems(Entity *pEnt, U32 uLotID)
{
	MailItemStruct *mail;
	if (!uLotID)
		return;

	if (!contact_IsNearMailBox(pEnt) && entGetAccessLevel(pEnt) < ACCESS_DEBUG)
	{
		notify_NotifySend(pEnt, kNotifyType_MailSendFailed, entTranslateMessageKey(pEnt, "Chat_MailNotNearContact"), NULL, NULL);
		return;
	}

	mail = StructCreate(parse_MailItemStruct);
	mail->entRef = entGetRef(pEnt);
	mail->uLotID = uLotID;
	RemoteCommand_Auction_GetLot(
		objCreateManagedReturnVal(ServerChat_MailTakeItemsCallback, mail), 
		GLOBALTYPE_AUCTIONSERVER, 0, uLotID);
}

// Transfer items from the given mail item lot to your inventory including NPC mail items
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Mail) ACMD_NAME(MailTakeItems) ACMD_HIDE;
void ServerChat_MailTakeItems(Entity *pEnt, U32 uLotID, S32 iNPCEmailID)
{
	MailItemStruct *mail;

	if (!contact_IsNearMailBox(pEnt) && entGetAccessLevel(pEnt) < ACCESS_DEBUG)
	{
		notify_NotifySend(pEnt, kNotifyType_MailSendFailed, entTranslateMessageKey(pEnt, "Chat_MailNotNearContact"), NULL, NULL);
		return;
	}

	if(uLotID)
	{
		mail = StructCreate(parse_MailItemStruct);
		mail->entRef = entGetRef(pEnt);
		mail->uLotID = uLotID;
		RemoteCommand_Auction_GetLot(
			objCreateManagedReturnVal(ServerChat_MailTakeItemsCallback, mail), 
			GLOBALTYPE_AUCTIONSERVER, 0, uLotID);
	}
	else if(iNPCEmailID > 0)
	{
		S32 i, j;
		
		if(pEnt->pPlayer)
		{
			for(i = 0; i < eaSize(&pEnt->pPlayer->pEmailV2->mail); ++i)
			{
				if(pEnt->pPlayer->pEmailV2->mail[i]->iNPCEMailID == iNPCEmailID && eaSize(&pEnt->pPlayer->pEmailV2->mail[i]->ppItemSlot) > 0)
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					NPCEMailData* pData = pEnt->pPlayer->pEmailV2->mail[i];
					TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("TakeItems", pEnt, NULL, NULL);
					U32* eaPets = NULL;
					ItemChangeReason reason = {0};

					ea32Create(&eaPets);
					for (j = eaSize(&pData->ppItemSlot)-1; j >= 0; j--)
					{
						InventorySlot* pSlot = pData->ppItemSlot[j];
						if (pSlot->pItem)
						{
							ItemDef* pItemDef = GET_REF(pSlot->pItem->hItem);
							if (pItemDef && (pItemDef->flags & kItemDefFlag_Unique)!=0)
							{
								break;
							}
						}
					}
					if (j >= 0)
					{
						Entity_GetPetIDList(pEnt, &eaPets);
					}

					inv_FillItemChangeReason(&reason, pEnt, "Mail:TakeItemsNPC", NULL);

					AutoTrans_gslMailNPC_tr_TakeItems(returnVal, GetAppGlobalType(), 
						entGetType(pEnt), entGetContainerID(pEnt), 
						GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
						i, &reason, pExtract);

					ea32Destroy(&eaPets);
				}
			}
		}
	}
}


static void ServerChat_MailItemCallback(TransactionReturnVal *returnStruct, MailItemStruct *mail)
{
	Entity *pEnt = entFromEntityRefAnyPartition(mail->entRef);
	AuctionLot *pItemList = NULL;

	switch (RemoteCommandCheck_Auction_GetLot(returnStruct, &pItemList))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		break;

	case TRANSACTION_OUTCOME_SUCCESS:
		ClientCmd_gclMail_MailGetItemReturn(pEnt, mail->uLotID, pItemList);
		break;
	}
	StructDestroy(parse_AuctionLot, pItemList);
	StructDestroy(parse_MailItemStruct, mail);
}

AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Mail);
void ServerChat_MailGetItems(Entity *pEnt, U32 uLotID)
{
	MailItemStruct *mail;
	if (!uLotID)
		return;

	if (!contact_IsNearMailBox(pEnt) && entGetAccessLevel(pEnt) < ACCESS_DEBUG)
	{
		notify_NotifySend(pEnt, kNotifyType_MailSendFailed, entTranslateMessageKey(pEnt, "Chat_MailNotNearContact"), NULL, NULL);
		return;
	}

	mail = StructCreate(parse_MailItemStruct);
	mail->entRef = entGetRef(pEnt);
	mail->uLotID = uLotID;
	RemoteCommand_Auction_GetLot(
		objCreateManagedReturnVal(ServerChat_MailItemCallback, mail), 
		GLOBALTYPE_AUCTIONSERVER, 0, uLotID);
}

static void LotAddMultipleItems_CB(TransactionReturnVal *pReturn, MailItemStruct *mail)
{
	Entity *pEnt = entFromEntityRefAnyPartition(mail->entRef);
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS && pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		U32* eaPets = NULL;
		char *error = NULL;
		ItemChangeReason reason = {0};

		entFormatGameMessageKey(pEnt, &error, "AuctionServer_MailCannotRemoveItems", STRFMT_END);
		if (!error)
			estrCopy2(&error, "Could not mail your items.");

		ClientCmd_gclMailSentConfirm(pEnt, false, error);

		ea32Create(&eaPets);
		Entity_GetPetIDList(pEnt, &eaPets);
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_MAIL_GSL, pEnt, "mail", "Failed to add items to mail lot: %s\n",pReturn->pBaseReturnVals[0].returnString);
		}

		inv_FillItemChangeReason(&reason, pEnt, "Mail:FailedToMailItems", NULL);

		AutoTrans_auction_tr_CancelMail(
			NULL, GLOBALTYPE_GAMESERVER,
			entGetType(pEnt), entGetContainerID(pEnt),
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
			GLOBALTYPE_AUCTIONLOT, mail->mail->uLotID,
			GLOBALTYPE_ENTITYSAVEDPET, &mail->containerItemPetIDs,
			&reason, pExtract);

		estrDestroy(&error);
		ea32Destroy(&eaPets);
	}
	else if (mail->mail && pEnt)
	{
		ServerChat_SendMailWithAuctionLot(pEnt, mail);
	}
	StructDestroy(parse_MailItemStruct, mail);
}

static void LotAddMultipleContainerItems_CB(TransactionReturnVal *pReturn, MailItemStruct *mail)
{
	Entity *pEnt = entFromEntityRefAnyPartition(mail->entRef);
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS && pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		char *error = NULL;
		U32* eaPets = NULL;
		ItemChangeReason reason = {0};

		entFormatGameMessageKey(pEnt, &error, "AuctionServer_MailCannotRemoveItems", STRFMT_END);
		if (!error)
			estrCopy2(&error, "Could not mail your items.");

		ClientCmd_gclMailSentConfirm(pEnt, false, error);

		ea32Create(&eaPets);
		Entity_GetPetIDList(pEnt, &eaPets);

		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_MAIL_GSL, pEnt, "mail", "Failed to add items to mail lot: %s\n",pReturn->pBaseReturnVals[0].returnString);
		}

		inv_FillItemChangeReason(&reason, pEnt, "Mail:FailedToMailItems", NULL);

		AutoTrans_auction_tr_CancelMail(
			NULL, GLOBALTYPE_GAMESERVER,
			entGetType(pEnt), entGetContainerID(pEnt),
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
			GLOBALTYPE_AUCTIONLOT, mail->mail->uLotID,
			GLOBALTYPE_ENTITYSAVEDPET, &mail->containerItemPetIDs,
			&reason, pExtract);

		estrDestroy(&error);
		StructDestroy(parse_MailItemStruct, mail);
		ea32Destroy(&eaPets);
	}
	else if (mail->mail && pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Mail:AddItems", NULL);

		AutoTrans_auction_tr_AddItemsToAuction(
			objCreateManagedReturnVal(LotAddMultipleItems_CB, mail),
			GLOBALTYPE_GAMESERVER,
			entGetType(pEnt), entGetContainerID(pEnt),
			GLOBALTYPE_AUCTIONLOT, mail->mail->uLotID,
			GLOBALTYPE_ENTITYSAVEDPET, &(U32*)mail->auctionPetContainerIDs,
			ALS_Mailed, false, &reason, pExtract);
	}
}

static void CreateMailLot_CB(TransactionReturnVal *pReturn, MailItemStruct *mail)
{
	Entity *pEnt = entFromEntityRefAnyPartition(mail->entRef);

	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS && pEnt)
	{
		char *error = NULL;
		entFormatGameMessageKey(pEnt, &error, "AuctionServer_MailOffline", STRFMT_END);
		if (!error)
			estrCopy2(&error, "Mail is offline.");

		ClientCmd_gclMailSentConfirm(pEnt, false, error);	
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_MAIL_GSL, pEnt, "mail", "Failed to create mail lot: %s\n",pReturn->pBaseReturnVals[0].returnString);
		}

		estrDestroy(&error);
		StructDestroy(parse_MailItemStruct, mail);
	}
	else if (pEnt && mail->mail)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemChangeReason reason = {0};

		mail->mail->uLotID = atoi(pReturn->pBaseReturnVals->returnString);
		mail->uLotID = mail->mail->uLotID;
		
		inv_FillItemChangeReason(&reason, pEnt, "Mail:CreateMail", NULL);

		if(eaiSize(&mail->containerItemPetIDs) > 0) 
		{
			AutoTrans_auction_tr_AddContainerItemsToAuction(
				objCreateManagedReturnVal(LotAddMultipleContainerItems_CB, mail),
				GLOBALTYPE_GAMESERVER, 
				entGetType(pEnt), entGetContainerID(pEnt), 
				GLOBALTYPE_ENTITYSAVEDPET, &mail->containerItemPetIDs, 
				GLOBALTYPE_AUCTIONLOT, mail->uLotID, 
				ALS_New, &reason);				
		} 
		else 
		{
			AutoTrans_auction_tr_AddItemsToAuction(
				objCreateManagedReturnVal(LotAddMultipleItems_CB, mail),
				GLOBALTYPE_GAMESERVER,
				entGetType(pEnt), entGetContainerID(pEnt),
				GLOBALTYPE_AUCTIONLOT, mail->mail->uLotID,
				GLOBALTYPE_ENTITYSAVEDPET, &(U32*)mail->auctionPetContainerIDs,
				ALS_Mailed, false, &reason, pExtract);
		}
	}
	else
	{
		StructDestroy(parse_MailItemStruct, mail);
	}
}

static void GetAccountID_CB(TransactionReturnVal *pReturn, MailItemStruct *pMail)
{
	U32 uiAccountID;
	enumTransactionOutcome eOutcome = RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(pReturn, &uiAccountID);
	Entity *pEnt = entFromEntityRefAnyPartition(pMail->entRef);
	char *apchItemCountPairs[MAIL_ITEM_MAX * 2];
	NOCONST(AuctionLot) *pNewLot;
	char *pchItemCopy;
	S32 i, iSize;

	if (!pEnt)
	{
		StructDestroy(parse_MailItemStruct, pMail);
		return;
	}

	if (uiAccountID == 0 || eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		char *pchError = NULL;
		entFormatGameMessageKey(pEnt, &pchError, "ChatServer_UserDNE", STRFMT_STRING("User", pMail->mail->toName), STRFMT_END);
		ClientCmd_gclMailSentConfirm(pEnt, false, pchError);
		estrDestroy(&pchError);
		StructDestroy(parse_MailItemStruct, pMail);
		return;
	}

	pNewLot = StructCreateNoConst(parse_AuctionLot);
	AuctionLotInit(pNewLot);
	pNewLot->ownerID = entGetContainerID(pEnt);
	pNewLot->OwnerAccountID = entGetAccountID(pEnt);
	pNewLot->state = ALS_New;
	pNewLot->creationTime = timeSecondsSince2000();
	pNewLot->modifiedTime = 0;
	pNewLot->uExpireTime = 0xffffffff;	// never expire (mail system should expire these)
	pNewLot->uPostingFee = 0;			// no fees at this time for mailed items
	pNewLot->uSoldFee = 0;
	// FIXME: Does this need to be localized? We currently don't show it anywhere.
	pNewLot->title = strdup(STACK_SPRINTF("Mail from %s", pEnt->pPlayer->publicAccountName));
	pNewLot->recipientID = uiAccountID;
	ea32Copy(&pNewLot->auctionPetContainerIDs, &pMail->mailPetContainerIDs);

	strdup_alloca(pchItemCopy, pMail->pchItemString);
	iSize = tokenize_line(pchItemCopy, apchItemCountPairs, NULL);

	for (i=0; i<iSize; i += 2)
	{		
		InvBagIDs eBagID = InvBagIDs_None;
		int iSlotIdx = 0;
		U64 uiItemID = atoi64(apchItemCountPairs[i]);
		S32 iCount = atoi(apchItemCountPairs[i + 1]);
		Item *pItem = NULL;
		NOCONST(Item) *pContainerItem = NULL;
		InventoryBag *pBag;
		InventorySlot *pSlot = NULL;
		ItemDef *pItemDef = NULL;
		Entity *pPet = NULL;
		char *c;
		S32 cID = 0;
		S32 iContainerItem = 0;

		if ((c = strchr(apchItemCountPairs[i], ':')) != NULL)
		{
			cID = atoi(c + 1);
		}

		if (cID)
		{
			pPet = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, GLOBALTYPE_ENTITYSAVEDPET, cID);
		}

		if((c = strchr(apchItemCountPairs[i], 'C')) != NULL)
		{
			iContainerItem = atoi(c + 1);
		}

		// Special handling for container items
		if(iContainerItem)
		{
			Entity* pOwner = pPet ? pPet : pEnt;

			if(pOwner)
			{
				pItem = item_FromSavedPet(pOwner, iContainerItem);
				pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
				pBag = NULL;
				pSlot = NULL;
			}
		}
		else
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			if (pPet)
			{
				pItem = inv_GetItemAndSlotsByID(pPet, uiItemID, &eBagID, &iSlotIdx);
				pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pPet), eBagID, pExtract));
			}
			else
			{
				pItem = inv_GetItemAndSlotsByID(pEnt, uiItemID, &eBagID, &iSlotIdx);
				pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract));
				
				if (!item_ItemMoveValidTemporaryPuppetCheck(pEnt, pBag))
				{
					// Don't allow the player to send mail items from a temporary puppet
					pItem = NULL;
				}
			}
			pSlot = pBag ? inv_GetSlotPtr(pBag, iSlotIdx) : NULL;
			pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		}
		
		if(pItem)
		{
			if(estrLength(&pMail->pFullItemString) > 0)
			{
				estrConcatf(&pMail->pFullItemString,", ");
			}
			estrConcatf(&pMail->pFullItemString, "%d %s", iCount, item_GetLogString(pItem));
		}

		// FIXME: Numeric items don't work, the auction server removes them by count rather
		// than value. For now, just make regular items work.
		if (pItemDef && pItem && item_CanTrade(pItem) && pItemDef->eType != kItemType_Numeric && (pSlot || iContainerItem)) 
		{
			MIN1(iCount, pItem->count);

			if (iCount > 0)
			{
				NOCONST(AuctionSlot)* pAuctionItem = StructCreateNoConst(parse_AuctionSlot);
				pAuctionItem->slot.pItem = StructCloneDeConst(parse_Item, pItem);
				pAuctionItem->slot.pItem->count = iCount;
				eaPush(&pNewLot->ppItemsV2, pAuctionItem);
			}
		}
	}

	if (eaSize(&pNewLot->ppItemsV2) > 0)
	{
		gslAuction_GetContainerItemPetsFromLot(CONTAINER_RECONST(AuctionLot, pNewLot), &pMail->containerItemPetIDs);
		objRequestContainerCreate(objCreateManagedReturnVal(CreateMailLot_CB, pMail), 
			GLOBALTYPE_AUCTIONLOT, pNewLot, GLOBALTYPE_AUCTIONSERVER, 0); // Give ownership to Auction Server
	}
	else
	{
		ServerChat_SendMail(pEnt, pMail->mail->toName, pMail->mail->subject, pMail->mail->body);
		StructDestroy(parse_MailItemStruct, pMail);
	}
	StructDestroyNoConst(parse_AuctionLot, pNewLot);
}

// Send mail with items attached.
AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Auction, Chat) ACMD_NAME(MailSendWithItems);
void ServerChat_MailSendWithItems(Entity* pEnt, const char *pchTo, const char *pchSubject, const char *pchBody, const ACMD_SENTENCE pchItemString)
{
	MailItemStruct *pMail;
	char *pchItemCopy;
	char *apchItemCountPairs[MAIL_ITEM_MAX * 2];
	S32 i = 0;
	S32 iSize;

	// Prevent players from sending items from any VShard except the first
	if (entGetVirtualShardID(pEnt) != 0)
	{
		return;
	}
	
	if (pchItemString && *pchItemString && !contact_IsNearMailBox(pEnt) && entGetAccessLevel(pEnt) < ACCESS_DEBUG)
	{
		notify_NotifySend(pEnt, kNotifyType_MailSendFailed, entTranslateMessageKey(pEnt, "Chat_MailNotNearContact"), NULL, NULL);
		return;
	}
	
	// If the player is on a trial account, they can't mail items
	if( gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_TRADE))
	{
		notify_NotifySend(pEnt, kNotifyType_Failed, entTranslateMessageKey(pEnt, "Chat_NoTrialItemMail"), NULL, NULL);
		return;
	}
	
	// If the player is on a trial account, they can't mail non-friends
	if (gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_SEND_MAIL)) {
		if (SAFE_MEMBER4(pEnt, pPlayer, pUI, pChatState, eaFriends)) {
			for (i = 0; i < eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends); i++) {
				ChatPlayerStruct *pFriend = pEnt->pPlayer->pUI->pChatState->eaFriends[i];
				if (pFriend && !stricmp(NULL_TO_EMPTY(pFriend->chatHandle), accountGetHandle(NULL_TO_EMPTY(pchTo)))) {
					break;
				}
			}
		}
		if (!SAFE_MEMBER4(pEnt, pPlayer, pUI, pChatState, eaFriends) || i == eaSize(&pEnt->pPlayer->pUI->pChatState->eaFriends)) {
			notify_NotifySend(pEnt, kNotifyType_Failed, entTranslateMessageKey(pEnt, "Chat_NoTrialMail"), NULL, NULL);
			return;
		}
	}
	
	pMail = StructCreate(parse_MailItemStruct);
	pMail->entRef = entGetRef(pEnt);
	pMail->mail = gslMailCreateMail(pEnt, pchSubject, pchBody);
	pMail->mail->toName = StructAllocString(accountGetHandle(pchTo));
	pMail->pchItemString = StructAllocString(pchItemString);
	pMail->auctionPetContainerIDs = NULL;

	strdup_alloca(pchItemCopy, pMail->pchItemString);
	iSize = tokenize_line(pchItemCopy, apchItemCountPairs, NULL);
	for (i=0; i<iSize; i += 2)
	{
		S32 cID;
		char *c = strchr(apchItemCountPairs[i], ':');
		if (!c)
		{
			ea32Push(&pMail->mailPetContainerIDs, 0);
			continue;
		}
		cID = atoi(c + 1);
		if (cID)
		{
			Entity *e = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, GLOBALTYPE_ENTITYSAVEDPET, cID);
			if (e)
			{
				ea32PushUnique(&pMail->auctionPetContainerIDs, cID);
				ea32Push(&pMail->mailPetContainerIDs, cID);
			}
			else
			{
				StructDestroy(parse_MailItemStruct, pMail);
				notify_NotifySend(pEnt, kNotifyType_MailSendFailed, entTranslateMessageKey(pEnt, "ChatServer_MalformedMail"), NULL, NULL);
				return;
			}
		}
		else
		{
			ea32Push(&pMail->mailPetContainerIDs, 0);
		}
	}

	RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
		objCreateManagedReturnVal(GetAccountID_CB, pMail),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountGetHandle(pchTo));
}

#include "AutoGen/gslMail_Old_c_ast.c"
