#include "chatCommonStructs.h"
#include "chatdb.h"
#include "chatGlobal.h"
#include "ChatServer.h"
#include "ChatServer/chatShared.h"
#include "cmdparse.h"
#include "estring.h"
#include "msgsend.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TransactionOutcomes.h"
#include "users.h"

#include "AutoGen/chatdb_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/GlobalChatServer_autotransactions_autogen_wrappers.h"

extern bool gbGlobalChatResponse;
extern bool gbChatVerbose;

typedef struct SendMail_Struct
{
	int iCmdID;
	U32 uLinkID;
} SendMail_Struct;

typedef struct SendMailCB_Struct
{
	U32 uSenderID;
	U32 uAuctionLotID;
	U32 uRequestID;
	U32 uLinkID;
} SendMailCB_Struct;

#define USER_GET_LANGUAGE(eLanguage, user, linkID) { \
	PlayerInfoStruct *playerInfo = userFindPlayerInfoByLinkID(user, linkID); \
	eLanguage = sender->eLastLanguage; \
	if (playerInfo)\
		eLanguage = playerInfo->eLanguage; \
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
int ChatServerSetMailRead(CmdContext *context, ContainerID id, U32 uMailID)
{
	ChatUser *user = userFindByContainerId(id);
	Email *email;
	if (!user)
		return CHATRETURN_UNSPECIFIED;

	userSetMailAsRead(user, uMailID, true);
	email = eaIndexedGetUsingInt(&user->email, uMailID);
	if (email)
	{
		// TODO change this after shard is updated
		//estrPrintf(&command, "ChatServerForwardMailReadBitFromGlobal %d %d %d", user->id, uMailID, 
		//	email->expireTime ? email->expireTime - timeSecondsSince2000() : 0);
		sendCommandToUserLocalEx(user, NULL, "ChatServerForwardMailReadFromGlobal", "%d %d %d", user->id, uMailID, true);
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerDeleteMail(CmdContext *context, ContainerID id, U32 uMailID)
{
	ChatUser *user = userFindByContainerId(id);
	if (!user)
		return CHATRETURN_UNSPECIFIED;
	userDeleteEmail(user, uMailID);
	sendCommandToUserLocalEx(user, NULL, "ChatServerForwardMailDeleteFromGlobal", "%d %d", user->id, uMailID);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerDeleteMailNoReturn(CmdContext *context, ContainerID id, U32 uMailID)
{
	ChatUser *user = userFindByContainerId(id);
	if(!user)
		return CHATRETURN_UNSPECIFIED;
	userDeleteEmail(user, uMailID);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerDeleteMailWithLotID(CmdContext *context, ContainerID id, U32 uLotID)
{
	U32 uMailID;
	S32 i;
	ChatUser *user = userFindByContainerId(id);

	if(!uLotID)
		return CHATRETURN_UNSPECIFIED;

	if(!user)
		return CHATRETURN_UNSPECIFIED;

	for(i = 0; i < eaSize(&user->email); ++i)
	{
		if(user->email[i]->uLotID == uLotID)
		{
			uMailID = user->email[i]->uID;
			userDeleteEmail(user, uMailID);
			break;
		}
	}
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerGetMail(CmdContext *context, ContainerID id)
{
	ChatUser *user = userFindByContainerId(id);
	if (!user)
		return CHATRETURN_UNSPECIFIED;
	userOnlineUpdateMailbox(user, chatServerGetCommandLink());
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerGetMailPaged(CmdContext *context, ContainerID id, int iPage, int iPageSize)
{
	ChatUser *user = userFindByContainerId(id);
	if (!user)
		return CHATRETURN_FWD_NONE;
	userOnlineUpdateMailboxPaged(user, chatServerGetCommandLink(), iPage, iPageSize);
	return CHATRETURN_FWD_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
int ChatServerCheckMail(CmdContext *context, ContainerID id)
{
	ChatUser *user = userFindByContainerId(id);
	char *command = NULL;
	NetLink *chatLink;

	if (!user)
		return CHATRETURN_UNSPECIFIED;

	chatLink = chatServerGetCommandLink();
	if (!chatLink)
		return CHATRETURN_UNSPECIFIED; // does nothing
	estrStackCreate(&command);
	estrPrintf(&command, "ChatServerForwardUnreadMailBitFromGlobal %d %d", user->id, userHasUnreadMail(user));
	sendCommandToLink(chatLink, command);
	estrDestroy(&command);
	return CHATRETURN_FWD_NONE;

}

//////////////////////////////
// Sending Mail

static void ChatMailSendConfirmation_v2(NetLink *link, U32 uSenderID, U32 uAuctionLot, U32 uRequestID, ChatTranslation *error)
{
	ChatMailReceipt receipt = {0};
	char *receiptString = NULL;
	receipt.uSenderID = uSenderID;
	receipt.uAuctionLot = uAuctionLot;
	receipt.uRequestID = uRequestID;
	receipt.error = error;

	estrStackCreate(&receiptString);
	ParserWriteTextEscaped(&receiptString, parse_ChatMailReceipt, &receipt, 0, 0, 0);
	sendCommandToLinkEx(link, "ChatServerMail_ReceiveMailReceipt", "%s", receiptString);

	receipt.error = NULL; // freed by caller
	StructDeInit(parse_ChatMailReceipt, &receipt);
	estrDestroy(&receiptString);
}

static void ChatServerMailCB_v2(enumTransactionOutcome eResult, SendMailCB_Struct *id)
{
	if (eResult == TRANSACTION_OUTCOME_FAILURE)
	{
		ChatUser *sender = userFindByContainerId(id->uSenderID);
		if (sender)
		{
			ChatTranslation *error = constructTranslationMessage("ChatServer_SendMailFail", STRFMT_END);
			ChatMailSendConfirmation_v2(GetLocalChatLink(id->uLinkID), sender->id, id->uAuctionLotID, id->uRequestID, error);
			StructDestroy(parse_ChatTranslation, error);
			if (gbChatVerbose)
				printf ("Send mail handle return timed out\n");
		}
	}
	free(id);
	// Does nothing for success
}

AUTO_COMMAND_REMOTE;
void ChatServerSendMail_v2 (CmdContext *context, ChatMailRequest *request)
{
	ChatUser *sender = NULL, *recipient = NULL;
	ChatMailStruct *mail;
	ChatTranslation *error = NULL;
	int result;

	if (!request->mail)
		return;
	mail = request->mail;
	
	if (request->recipientAccountID)
		recipient = userFindByContainerId(request->recipientAccountID);
	else if (!gbGlobalChatResponse && request->recipientHandle)
	{
		SendMailCB_Struct *idData = calloc(1, sizeof(SendMailCB_Struct));
		idData->uSenderID= request->mail->fromID;
		idData->uAuctionLotID = request->mail->uLotID;
		idData->uRequestID = request->uRequestID;
		idData->uLinkID = GetLocalChatLinkID(chatServerGetCommandLink());

		if (globalChatAddCommandWaitQueueByLinkEx(chatServerGetCommandLink(), 
			request->recipientHandle, context->commandString, 
			ChatServerMailCB_v2, idData))
			return; // delayed response
		// Otherwise...
		free(idData);
		recipient = userFindByHandle(request->recipientHandle);
	}
	else
		recipient = userFindByHandle(request->recipientHandle);
	sender = userFindByContainerId(request->mail->fromID);

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

//////////////////////////////
// Sending Mail -- deprecated since this is trying to do all translations locally

static void ChatMailSendConfirmation(NetLink *link, U32 uSenderID, U32 uAuctionLot, char *error)
{
	char *escapedError = NULL;
	estrSuperEscapeString_shorter(&escapedError, error);
	sendCommandToLinkEx(link, "ChatServerForwardSendMailConfirmationFromGlobal", "%d %d \"%s\"", 
		uSenderID, uAuctionLot, escapedError ? escapedError : "");
	estrDestroy(&escapedError);
}

AUTO_COMMAND_REMOTE;
int ChatServerSendMailByID (CmdContext *context, ContainerID id, ContainerID playerID, char *shardName, char *subject, ACMD_SENTENCE body)
{
	ChatUser *sender = userFindByContainerId(id);
	ChatUser *recipient = userFindByContainerId(playerID);
	int result;
	char *error = NULL;
	Language eLanguage;

	result = userAddEmail(sender, recipient, shardName, subject, body, EMAIL_TYPE_PLAYER, NULL, 0, 0, true);

	USER_GET_LANGUAGE(eLanguage, sender, GetLocalChatLinkID(chatServerGetCommandLink()));

	switch(result)
	{
	case CHATRETURN_USER_DNE:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_SendMailFail", STRFMT_END);
		}
	xcase CHATRETURN_USER_IGNORING:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_BeingIgnored", STRFMT_STRING("User", recipient->handle), STRFMT_END);
		}
	xcase CHATRETURN_USER_PERMISSIONS:
		{
			U32 duration = sender->silenced-timeSecondsSince2000();
			langFormatMessageKey(eLanguage, &error, "ChatServer_Silenced", STRFMT_INT("H", duration/(3600)), 
				STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);
		}
	xcase CHATRETURN_MAILBOX_FULL:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_MailboxFull", STRFMT_STRING("User", recipient->handle), STRFMT_END);
		}
	}
	if (!error)
		estrCopy2(&error, "");
	ChatMailSendConfirmation(chatServerGetCommandLink(), sender->id, 0, error);
	estrDestroy(&error);
	return result;
}

static void ChatServerMail_CB(enumTransactionOutcome eResult, SendMail_Struct *id)
{
	if (eResult == TRANSACTION_OUTCOME_FAILURE)
	{
		ChatUser *sender = userFindByContainerId(id->iCmdID);

		if (sender)
		{
			Language eLanguage;
			char *error = NULL;

			USER_GET_LANGUAGE(eLanguage, sender, GetLocalChatLinkID(chatServerGetCommandLink()));
			langFormatMessageKey(eLanguage, &error, "ChatServer_SendMailFail", STRFMT_END);
			ChatMailSendConfirmation(GetLocalChatLink(id->uLinkID), sender->id, 0, error);
			estrDestroy(&error);
		}
	}
	free(id);
}

AUTO_COMMAND_REMOTE;
int ChatServerSendMailByHandle (CmdContext *context, ContainerID id, char *handle, char *shardName, char *subject, ACMD_SENTENCE body,
								EMailType emailType, const char *sendName, S64 npcEmailID, U32 futureSendTime)
{
	ChatUser *sender, *recipient;
	int result;
	char *error = NULL;
	Language eLanguage;

	if (!gbGlobalChatResponse)
	{
		SendMail_Struct *idData = calloc(1, sizeof(SendMail_Struct));
		idData->iCmdID = id;
		idData->uLinkID = GetLocalChatLinkID(chatServerGetCommandLink());
		if (globalChatAddCommandWaitQueueByLinkEx(chatServerGetCommandLink(), handle, context->commandString, 
			ChatServerMail_CB, idData))
			return CHATRETURN_FWD_NONE; // delayed response
		else
			free(idData);
	}
	sender = userFindByContainerId(id);
	recipient = userFindByHandle(handle);

	if (!sender)
		return CHATRETURN_FWD_NONE;
	result = userAddEmail(sender, recipient, shardName, subject, body, emailType, sendName, npcEmailID, futureSendTime, true);

	USER_GET_LANGUAGE(eLanguage, sender, GetLocalChatLinkID(chatServerGetCommandLink()));

	switch(result)
	{
	case CHATRETURN_USER_DNE:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_UserDNE", STRFMT_STRING("User", handle), STRFMT_END);
		}
	xcase CHATRETURN_USER_IGNORING:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_BeingIgnored", STRFMT_STRING("User", handle), STRFMT_END);
		}
	xcase CHATRETURN_USER_PERMISSIONS:
		{
			U32 duration = sender->silenced-timeSecondsSince2000();
			langFormatMessageKey(eLanguage, &error, "ChatServer_Silenced", STRFMT_INT("H", duration/(3600)), 
				STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);
		}
	xcase CHATRETURN_MAILBOX_FULL:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_MailboxFull", STRFMT_STRING("User", handle), STRFMT_END);
		}
	}
	if (!error)
		estrCopy2(&error, "");
	ChatMailSendConfirmation(chatServerGetCommandLink(), sender->id, 0, error);
	estrDestroy(&error);
	return result;
}

AUTO_COMMAND_REMOTE;
void ChatServerSendMassMailByID(CmdContext *pContext, ContainerID iID, const char *pcRecipients, char *pcShardName, char *pcSubject, ACMD_SENTENCE pcBody)
{
	ChatUser *sender = userFindByContainerId(iID);
	if (!sender)
		return; // total failure here
	while (1) {
		U32 iRecipientID = atoi(pcRecipients);
		if (iRecipientID) {
			ChatUser *recipient = userFindByContainerId(iRecipientID);
			int result = userAddEmail(sender, recipient, pcShardName, pcSubject, pcBody, 
				EMAIL_TYPE_PLAYER, NULL, 0, 0, false);
			bool bPermissionFail = false;

			switch(result)
			{
				case CHATRETURN_USER_DNE: // this silently fails 
				xcase CHATRETURN_USER_IGNORING:
				{
					sendTranslatedMessageToUser(sender, kChatLogEntryType_System, NULL, "ChatServer_BeingIgnored", 
						STRFMT_STRING("User", recipient->handle), STRFMT_END);
				}
				xcase CHATRETURN_USER_PERMISSIONS:
				{
					U32 duration = sender->silenced-timeSecondsSince2000();
					sendTranslatedMessageToUser(sender, kChatLogEntryType_System, NULL, "ChatServer_Silenced", 
						STRFMT_INT("H", duration/(3600)), STRFMT_INT("M", (duration/60)%60), 
						STRFMT_INT("S", duration%60), STRFMT_END);
					bPermissionFail = true;
				}
				xcase CHATRETURN_MAILBOX_FULL:
				{
					sendTranslatedMessageToUser(sender, kChatLogEntryType_System, NULL, "ChatServer_MailboxFull", 
						STRFMT_STRING("User", recipient->handle), STRFMT_END);
				}
			}
			if (bPermissionFail) // Sender is silenced, early out here
				break;
		}
		pcRecipients = strchr_fast(pcRecipients, ',');
		if (pcRecipients) {
			pcRecipients++;
		} else {
			break;
		}
	}
	ChatMailSendConfirmation(chatServerGetCommandLink(), sender->id, 0, "");
}

//////////////////////////////
// Sending Mail with Items -- deprecated, changed to share a code path with everything

AUTO_COMMAND_REMOTE;
int ChatServerSendMailItemsByID (CmdContext *context, ContainerID id, ContainerID playerID, char *shardName, ChatMailStruct *mail)
{
	ChatUser *sender = userFindByContainerId(id);
	ChatUser *recipient = userFindByContainerId(playerID);
	int result;
	char *error = NULL;
	PlayerInfoStruct *playerInfo = userFindPlayerInfoByLinkID(sender, GetLocalChatLinkID(chatServerGetCommandLink()));
	Language eLanguage = sender->eLastLanguage;

	result = userAddEmailEx(sender, recipient, shardName, mail);

	if (playerInfo)
		eLanguage = playerInfo->eLanguage;

	switch(result)
	{
	case CHATRETURN_USER_DNE:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_SendMailFail", STRFMT_END);
		}
	xcase CHATRETURN_USER_IGNORING:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_BeingIgnored", STRFMT_STRING("User", recipient->handle), STRFMT_END);
		}
	xcase CHATRETURN_USER_PERMISSIONS:
		{
			U32 duration = sender->silenced-timeSecondsSince2000();
			langFormatMessageKey(eLanguage, &error, "ChatServer_Silenced", STRFMT_INT("H", duration/(3600)), 
				STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);
		}
	xcase CHATRETURN_MAILBOX_FULL:
		{
			langFormatMessageKey(eLanguage, &error, "ChatServer_MailboxFull", STRFMT_STRING("User", recipient->handle), STRFMT_END);
		}
	}
	if (!error)
		estrCopy2(&error, "");
	ChatMailSendConfirmation(chatServerGetCommandLink(), sender->id, mail->uLotID, error);
	estrDestroy(&error);
	return result;
}

AUTO_COMMAND_REMOTE;
int ChatServerSendMailItemsByHandle (CmdContext *context, ContainerID id, char *handle, char *shardName, ChatMailStruct *mail)
{
	ChatUser *sender, *recipient;
	int result;
	char *error = NULL;
	PlayerInfoStruct *playerInfo;
	Language eLanguage;

	if (!gbGlobalChatResponse)
	{
		if (globalChatAddCommandWaitQueueByLink(chatServerGetCommandLink(), handle, context->commandString))
			return CHATRETURN_FWD_NONE; // delayed response
	}
	sender = userFindByContainerId(id);
	recipient = userFindByHandle(handle);

	result = userAddEmailEx(sender, recipient, shardName, mail);
	{
		playerInfo = userFindPlayerInfoByLinkID(sender, GetLocalChatLinkID(chatServerGetCommandLink()));
		eLanguage = sender->eLastLanguage;

		if (playerInfo)
			eLanguage = playerInfo->eLanguage;

		switch(result)
		{
		case CHATRETURN_USER_DNE:
			{
				langFormatMessageKey(eLanguage, &error, "ChatServer_UserDNE", STRFMT_STRING("User", handle), STRFMT_END);
			}
		xcase CHATRETURN_USER_IGNORING:
			{
				langFormatMessageKey(eLanguage, &error, "ChatServer_BeingIgnored", STRFMT_STRING("User", handle), STRFMT_END);
			}
		xcase CHATRETURN_USER_PERMISSIONS:
			{
				U32 duration = sender->silenced-timeSecondsSince2000();
				langFormatMessageKey(eLanguage, &error, "ChatServer_Silenced", STRFMT_INT("H", duration/(3600)), 
					STRFMT_INT("M", (duration/60)%60), STRFMT_INT("S", duration%60), STRFMT_END);
			}
		xcase CHATRETURN_MAILBOX_FULL:
			{
				langFormatMessageKey(eLanguage, &error, "ChatServer_BeingIgnored", STRFMT_STRING("User", handle), STRFMT_END);
			}
		}
		if (!error)
			estrCopy2(&error, "");
		ChatMailSendConfirmation(chatServerGetCommandLink(), sender->id, mail->uLotID, error);
		estrDestroy(&error);
	}
	return result;
}

/////////////////////////////////////////
// Deprecated SlowMail stuff
static void ChatServerItemMailSlow_CB(enumTransactionOutcome eResult, SendMail_Struct *id)
{
	if (eResult == TRANSACTION_OUTCOME_FAILURE)
	{
		sendCommandToLinkEx(GetLocalChatLink(id->uLinkID), "ChatServerMail_SlowReturn", "%d %d", id->iCmdID, CHATRETURN_TIMEOUT);
	}
	free(id);
	// Does nothing for success
}

AUTO_COMMAND_REMOTE;
void ChatServerSendMailItemsByHandle_Slow (CmdContext *context, ContainerID id, char *handle, 
										   char *shardName, ChatMailStruct *mail, int iSlowCmdID)
{
	int result;
	if (!gbGlobalChatResponse)
	{
		SendMail_Struct *idData = calloc(1, sizeof(SendMail_Struct));
		idData->iCmdID = iSlowCmdID;
		idData->uLinkID = GetLocalChatLinkID(chatServerGetCommandLink());
		if (globalChatAddCommandWaitQueueByLinkEx(chatServerGetCommandLink(), handle, context->commandString, 
			ChatServerItemMailSlow_CB, idData))
			return; // delayed response
		else
			free(idData);
	}
	result = ChatServerSendMailItemsByHandle(context, id, handle, shardName, mail);
	sendCommandToLinkEx(chatServerGetCommandLink(), "ChatServerMail_SlowReturn", "%d %d", iSlowCmdID, result);
}
// End Deprecated
/////////////////////////////////////////

AUTO_TRANSACTION ATR_LOCKS(user, ".email");
enumTransactionOutcome trDeleteShardMail(ATR_ARGS, NOCONST(ChatUser) *user, const char *shardName)
{
	INT_EARRAY eaiToRemove = NULL;
	const char *shardPooled = allocAddString(shardName);
	EARRAY_CONST_FOREACH_BEGIN(user->email, i, size);
	{
		NOCONST(Email) *email = user->email[i];
		if (email->shardName && email->shardName == shardPooled)
			eaiPush(&eaiToRemove, email->uID);
	}
	EARRAY_FOREACH_END;

	EARRAY_INT_CONST_FOREACH_BEGIN(eaiToRemove, i, size);
	{
		NOCONST(Email) *email = eaIndexedRemoveUsingInt(&user->email, eaiToRemove[i]);
		if (email)
			StructDestroyNoConst(parse_Email, email);
	}
	EARRAY_FOREACH_END;
	eaiDestroy(&eaiToRemove);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE;
void chatCommandRemote_DeleteAllShardMail(int accountID, const char *shardName)
{
	ChatUser *user = userFindByContainerId(accountID);
	if (!user || nullStr(shardName))
		return;
	AutoTrans_trDeleteShardMail(NULL, GLOBALTYPE_GLOBALCHATSERVER, GLOBALTYPE_CHATUSER, accountID, shardName);
	chatServerLogUserCommandf(LOG_CHATMAIL, "DeleteShardMail", NULL, user, NULL, "ShardName %s", shardName);
}