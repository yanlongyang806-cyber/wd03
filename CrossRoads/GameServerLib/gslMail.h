#pragma once

#include "MailCommon.h"

AUTO_STRUCT;
typedef struct EmailV3NewMessageWrapper
{
	EmailV3SenderItem** eaItemsFromPlayer;
	Item** eaItemsFromNPC;
	EmailV3Message* pMessage;
	U32 uSenderContainerID;
	const char* pchRecipientHandle;
	U32 uRecipientAccountID;
} EmailV3NewMessageWrapper;

void EmailV3_SendPlayerEmail(const char* pchSubject,
	const char* pchBody, 
	Entity* pSender,
	const char* pchRecipientHandle, 
	EmailV3SenderItemsWrapper* pWrapper);

void EmailV3_DeleteMessage(Entity* pEnt, int iMailID);