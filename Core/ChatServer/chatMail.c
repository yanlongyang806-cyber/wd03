#include "chatMail.h"
#include "chatdb.h"
#include "users.h"
#include "chatGlobal.h"
#include "msgsend.h"
#include "chatStringFormat.h"
#include "StringCache.h"

#include "chatCommonStructs.h"
#include "chatCommonStructs_h_ast.h"

#include "estring.h"
#include "cmdparse.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "Autogen/ChatServer_autogen_SlowFuncs.h"
#include "utilitiesLib.h"
#include "chatMail_c_ast.h"
#include "chatdb_h_ast.h"

AUTO_COMMAND_REMOTE;
int ChatServerForwardMailReadFromGlobal(CmdContext *context, ContainerID accountID, U32 uMailID, bool bRead)
{
	ChatUser *user = userFindByContainerId(accountID);
	if (userCharacterOnline(user))
		RemoteCommand_ServerChat_MailReadConfirmation(GLOBALTYPE_ENTITYPLAYER, 
			user->pPlayerInfo->onlineCharacterID, user->pPlayerInfo->onlineCharacterID, uMailID, true);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardMailReadBitFromGlobal(CmdContext *context, ContainerID accountID, U32 uMailID, U32 uTimeLeft)
{
	ChatUser *user = userFindByContainerId(accountID);
	if (userCharacterOnline(user))
		RemoteCommand_ServerChat_MailReadExpireUpdate(GLOBALTYPE_ENTITYPLAYER, 
			user->pPlayerInfo->onlineCharacterID, user->pPlayerInfo->onlineCharacterID, uMailID, uTimeLeft);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerSetMailRead(ContainerID id, U32 uMailID)
{
	sendCmdAndParamsToGlobalChat("ChatServerSetMailRead", "%d %d", id, uMailID);
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardMailDeleteFromGlobal(CmdContext *context, ContainerID accountID, U32 uMailID)
{
	ChatUser *user = userFindByContainerId(accountID);
	if (userCharacterOnline(user))
		RemoteCommand_ServerChat_MailDeleteConfirmation(GLOBALTYPE_ENTITYPLAYER, 
			user->pPlayerInfo->onlineCharacterID, user->pPlayerInfo->onlineCharacterID, uMailID);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerDeleteMail(ContainerID id, U32 uMailID)
{
	sendCmdAndParamsToGlobalChat("ChatServerDeleteMail", "%d %d", id, uMailID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void  ChatServerDeleteMailNoReturn(ContainerID id, U32 uMailID)
{
	sendCmdAndParamsToGlobalChat("ChatServerDeleteMailNoReturn", "%d %d", id, uMailID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void  ChatServerDeleteMailWithLotID(ContainerID id, U32 uLotID)
{
	sendCmdAndParamsToGlobalChat("ChatServerDeleteMailWithLotID", "%d %d", id, uLotID);
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardMailFromGlobal(CmdContext *context, ChatMailList *mailList)
{
	ChatUser *user = userFindByContainerId(mailList->uID);
	if (user)
		ChatServerForwardMail(user, mailList);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE;
int ChatServerForwardUnreadMailBitFromGlobal(CmdContext *context, U32 userID, bool hasUnreadMail)
{
	ChatUser *user = userFindByContainerId(userID);
	if (user)
		ChatServerForwardUnreadMailBit(user, hasUnreadMail);
	return CHATRETURN_NONE;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerGetMail(ContainerID id)
{
	sendCmdAndParamsToGlobalChat("ChatServerGetMail", "%d", id);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerGetMailPaged(ContainerID id, int iPage, int iPageSize)
{
	sendCmdAndParamsToGlobalChat("ChatServerGetMailPaged", "%d %d %d", id, iPage, iPageSize);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void ChatServerCheckMail(ContainerID id)
{
	sendCmdAndParamsToGlobalChat("ChatServerCheckMail", "%d", id);	
}

//////////////////////////////
// Sending Mail - deprecated and removed

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
int ChatServerForwardSendMailConfirmationFromGlobal(CmdContext *context, ContainerID id, U32 auctionLot, const char *error)
{
	// This should never get hit by the new code path for mails
	devassert(0);
	return CHATRETURN_FWD_NONE;
}

void ChatServerSendMailByID_v2(U32 toAccountID, ChatMailStruct *mail);
// Used by the UGC system
AUTO_COMMAND_REMOTE;
void ChatServerSendNPCEmail_Simple(ContainerID iAccountID, ContainerID vshardID, char *pAccountName, char *pFrom, char *pHeader, char *pBody)
{
	char shardName[256] = "";
	ChatMailStruct mail = {0};
	mail.fromID = iAccountID;
	mail.shardName = allocAddString(GetVShardQualifiedName(shardName, ARRAY_SIZE_CHECKED(shardName), vshardID));
	mail.subject = StructAllocString(pHeader);
	mail.body = StructAllocString(pBody);
	mail.eTypeOfEmail = EMAIL_TYPE_NPC_NO_REPLY;
	mail.fromName = StructAllocString(pFrom);
	mail.uFutureSendTime = timeSecondsSince2000();

	ChatServerSendMailByID_v2(iAccountID, &mail);
	StructDeInit(parse_ChatMailStruct, &mail);
}

AUTO_COMMAND_REMOTE;
int ChatServerSendMassMailByID(CmdContext *pContext, ContainerID iID, const char *pcRecipients, char *pcShardName, char *pcSubject, ACMD_SENTENCE pcBody)
{
	sendCommandToGlobalChatServer(pContext->commandString);
	return CHATRETURN_FWD_NONE;
}

// Deprecated and unused
/*AUTO_COMMAND_REMOTE;
void ChatServerSendAuctionExpireMail(ContainerID id, ContainerID playerID, char *shardName, int iLangID, ContainerID iLotID)
{
	char *estrParsedMail = NULL;
	ChatMailStruct *pMail = StructCreate(parse_ChatMailStruct);
	
	pMail->fromName = StructAllocString(langTranslateMessageKey(iLangID, "Auction_Sold_From_Name"));
	pMail->shardName = StructAllocString(shardName);
	pMail->subject = StructAllocString(langTranslateMessageKey(iLangID, "Auction_Expired_Subject"));
	pMail->body = StructAllocString(langTranslateMessageKey(iLangID, "Auction_Expired_Body"));
	pMail->uLotID = iLotID;
	pMail->eTypeOfEmail = EMAIL_TYPE_NPC_NO_REPLY;
	
	ParserWriteTextEscaped(&estrParsedMail, parse_ChatMailStruct, pMail, 0, 0, 0);
	sendCmdAndParamsToGlobalChat("ChatServerSendMailItemsByID", "%d %d %s %s", id, playerID, shardName, estrParsedMail);
	estrDestroy(&estrParsedMail);
}*/

////////////////////////////////////
// Version 2 of Mail calls

// Sends the mail request struct (does not clean it up)
__forceinline static void ChatServer_SendMailToGlobal(ChatMailRequest *request, ChatMailStruct *mail)
{
	char *escapedMailRequest = NULL;
	request->mail = mail;
	estrStackCreate(&escapedMailRequest);
	ParserWriteTextEscaped(&escapedMailRequest, parse_ChatMailRequest, request, 0, 0, 0);
	sendCmdAndParamsToGlobalChat("ChatServerSendMail_v2", "%s", escapedMailRequest);
	estrDestroy(&escapedMailRequest);
	request->mail = NULL;
}

AUTO_COMMAND_REMOTE;
void ChatServerSendMailByID_v2(U32 toAccountID, ChatMailStruct *mail)
{
	ChatMailRequest request = {0};
	request.recipientAccountID = toAccountID;

	ChatServer_SendMailToGlobal(&request, mail);
	StructDeInit(parse_ChatMailRequest, &request);
}

AUTO_COMMAND_REMOTE;
void ChatServerSendMailByHandle_v2(char *toHandle, ChatMailStruct *mail)
{
	ChatMailRequest request = {0};
	request.recipientHandle = StructAllocString(toHandle);

	ChatServer_SendMailToGlobal(&request, mail);
	StructDeInit(parse_ChatMailRequest, &request);
}

AUTO_STRUCT;
typedef struct ItemMailCB_Struct
{
	U32 uRequestID; AST(KEY)
	U32 uAccountID;
	U32 uCharacterID;
	U32 uAuctionLotID;
} ItemMailCB_Struct;

static U32 uLastItemMailRequestID = 0;
static ItemMailCB_Struct **seaItemMailQueue = NULL;

// This is to be used for remote calls that require a return value for callbacks (item mails)
AUTO_COMMAND_REMOTE;
int ChatServerSendMailItemsByHandle_v2(ContainerID characterID, char *toHandle, ChatMailStruct *mail)
{
	ItemMailCB_Struct *mailQueue = NULL;
	ChatMailRequest request = {0};
	
	mailQueue = StructCreate(parse_ItemMailCB_Struct);
	mailQueue->uAccountID = mail->fromID;
	mailQueue->uCharacterID = characterID;
	mailQueue->uAuctionLotID = mail->uLotID;
	mailQueue->uRequestID = ++uLastItemMailRequestID;
	if (!seaItemMailQueue)
		eaIndexedEnable(&seaItemMailQueue, parse_ItemMailCB_Struct);
	eaIndexedAdd(&seaItemMailQueue, mailQueue);
	
	request.recipientHandle = StructAllocString(toHandle);
	request.uRequestID = mailQueue->uRequestID;

	ChatServer_SendMailToGlobal(&request, mail);
	StructDeInit(parse_ChatMailRequest, &request);
	return 0;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ChatServerMail_ReceiveMailReceipt(ChatMailReceipt *receipt)
{
	ChatUser *user = userFindByContainerId(receipt->uSenderID);
	char *error = NULL;
	if (receipt->uRequestID)
	{
		if (seaItemMailQueue)
		{
			ItemMailCB_Struct *mail = eaIndexedGetUsingInt(&seaItemMailQueue, receipt->uRequestID);
			if (mail)
			{
				int iError = 0;

				if (receipt->error)
				{
					if (user)
						ChatServer_Translate(user, receipt->error, &error);
					else
						iError = CHATRETURN_UNSPECIFIED; // unknown error
				}
				RemoteCommand_ServerChat_ItemMailResponse(GLOBALTYPE_ENTITYPLAYER, mail->uCharacterID, 
					mail->uCharacterID, mail->uAuctionLotID, error, iError);
				eaFindAndRemove(&seaItemMailQueue, mail);
				StructDestroy(parse_ItemMailCB_Struct, mail);
				estrDestroy(&error);
			}
		}
	}
	else if (userOnline(user))
	{
		if (receipt->error)
			ChatServer_Translate(user, receipt->error, &error);
		else
			estrCopy2(&error, "");
		ChatServerForwardMailConfirmation(user, receipt->uAuctionLot, error);
	}
	estrDestroy(&error);
}

void ChatServerMail_GCSDisconnect(void)
{
	EARRAY_FOREACH_BEGIN(seaItemMailQueue, i);
	{
		RemoteCommand_ServerChat_ItemMailResponse(GLOBALTYPE_ENTITYPLAYER, seaItemMailQueue[i]->uCharacterID, 
			seaItemMailQueue[i]->uCharacterID, seaItemMailQueue[i]->uAuctionLotID, NULL, CHATRETURN_DISCONNECTED);
	}
	EARRAY_FOREACH_END;
	eaDestroyStruct(&seaItemMailQueue, parse_ItemMailCB_Struct);
}


// A list of auction lots that should be included in mail to this account
// if not create a mail message
AUTO_COMMAND_REMOTE;
void chatCommandRemote_CheckMailAuctionLots(MailedAuctionLots *pAuctionLots)
{
	if(pAuctionLots)
	{
		char *escapedAuction = NULL;
		ChatUser *user = userFindByContainerId(pAuctionLots->iOwnerAccountID);
		Language eLanguage = user && user->pPlayerInfo ? user->pPlayerInfo->eLanguage : LANGUAGE_DEFAULT;
		char *pTempString = NULL;

		estrStackCreate(&pTempString);
		langFormatMessageKey(eLanguage, &pTempString, "ChatServer_Subject_Returned_Auction", STRFMT_END);
		if(estrLength(&pTempString))
			pAuctionLots->pReturnedSubject = StructAllocString(pTempString);

		estrClear(&pTempString);
		langFormatMessageKey(eLanguage, &pTempString, "ChatServer_Body_Returned_Auction", STRFMT_END);
		if(estrLength(&pTempString))
			pAuctionLots->pReturnedBody = StructAllocString(pTempString);

		estrClear(&pTempString);
		langFormatMessageKey(eLanguage, &pTempString, "ChatServer_From_Returned_Auction", STRFMT_END);
		if(estrLength(&pTempString))
			pAuctionLots->pReturnedFrom = StructAllocString(pTempString);
		estrDestroy(&pTempString);

		estrStackCreate(&escapedAuction);
		ParserWriteTextEscaped(&escapedAuction, parse_MailedAuctionLots, pAuctionLots, 0, 0, 0);
		sendCmdAndParamsToGlobalChat("chatCommandRemote_CheckMailAuctionLots", "%s", escapedAuction);
		estrDestroy(&escapedAuction);
	}
}

// Get the mail list remotely
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void chatCommandRemote_GetMailbox(int accountID)
{
	sendCmdAndParamsToGlobalChat("chatCommandRemote_GetMailbox", "%d", accountID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void chatCommandRemote_GetNPCMail(int accountID)
{
	sendCmdAndParamsToGlobalChat("chatCommandRemote_GetNPCMail", "%d", accountID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void chatCommandRemote_DeleteAllShardMail(int accountID)
{
	sendCmdAndParamsToGlobalChat("chatCommandRemote_DeleteShardMail", "%d \"%s\"", accountID, GetShardNameFromShardInfoString());
}

#include "chatMail_c_ast.c"