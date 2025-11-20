#pragma once

#include "itemCommon.h"

typedef struct EmailV3SenderItem EmailV3SenderItem;
typedef struct EmailV3Message EmailV3Message;
typedef struct ChatMailStruct ChatMailStruct;
typedef struct NPCEMailData NPCEMailData;
typedef struct Item Item;
typedef struct ItemChangeReason ItemChangeReason;

typedef enum enumTransactionOutcome enumTransactionOutcome;
typedef enum NPCEmailType NPCEmailType;

typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(EmailV3) NOCONST(EmailV3);
typedef struct NOCONST(EmailV3Message) NOCONST(EmailV3Message);
typedef struct NOCONST(AuctionLot) NOCONST(AuctionLot);

AUTO_ENUM;
typedef enum EMailV3Type
{
	kEmailV3Type_Player,
	kEmailV3Type_NPC,
	kEmailV3Type_Old_Player,
	kEmailV3Type_Old_NPC,

} EMailV3Type;

AUTO_STRUCT AST_CONTAINER;
typedef struct EmailV3Message
{
	const U32 uID;							AST(PERSIST SUBSCRIBE KEY)
	const U32 uSent;						AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))// time sent
	const U32 uExpireTime;					AST(PERSIST SUBSCRIBE FORMATSTRING(HTML_SECS = 1))

	CONST_STRING_MODIFIABLE pchSubject;		AST(PERSIST SUBSCRIBE)
	CONST_STRING_MODIFIABLE pchBody;		AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(Item) ppItems;			AST(PERSIST SUBSCRIBE NO_INDEX FORCE_CONTAINER)

	//	const U32	recipientContainerID;		// entity container ID always of player type
	CONST_STRING_MODIFIABLE	pchSenderName;	AST(PERSIST SUBSCRIBE)// sender name, character name for players
	CONST_STRING_MODIFIABLE	pchSenderHandle;AST(PERSIST SUBSCRIBE)// sender name, character name for players
	const EMailV3Type	eTypeOfEmail;		AST(PERSIST SUBSCRIBE)// type of email this is
	const bool bRead : 1;					AST(PERSIST SUBSCRIBE)
} EmailV3Message;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(uLastSyncTime) AST_IGNORE(bReadAll);
typedef struct EmailV3
{
	CONST_EARRAY_OF(EmailV3Message) eaMessages;		AST(PERSIST SUBSCRIBE)
	const bool bUnreadMail;							AST(PERSIST SUBSCRIBE)
	const U32 uLastUsedID;							AST(PERSIST)
} EmailV3;

AUTO_STRUCT;
typedef struct EmailV3SenderItem
{
	U64 uID;
	const char* pchLiteItemName;
	int iCount;			AST(NAME(count))
	int iPetID;

	//cached for UI
	Item* pItem;		AST(CLIENT_ONLY NAME(Item))
	bool bDestroyNextFrame;	AST(CLIENT_ONLY)
} EmailV3SenderItem;

AUTO_STRUCT;
typedef struct EmailV3UIMessage
{
	EmailV3Message* pMessage;		AST(UNOWNED)
	ChatMailStruct* pChatServerMessage;	AST(UNOWNED)
	NPCEMailData* pNPCMessage;	AST(UNOWNED)
	//cached
	const char* subject;		AST(UNOWNED)
	char* subjectOwned;
	const char* fromName;		AST(UNOWNED)
	const char* fromHandle;		AST(UNOWNED)
	const char* body;		AST(UNOWNED)
	char* bodyOwned;
	const char* shardName;		AST(UNOWNED) // Shard this mail originated from
	U32 sent;				AST(FORMATSTRING(JSON_SECS_TO_RFC822=1))
	U32 bRead;
	U32 uID;
	U32 uLotID;
	EMailV3Type eTypeOfEmail;			// the type of email
	ContainerID toContainerID;		// ID of the player that this was mailed to, used for NPC mail
	S32 iNPCEMailID;				// email id from player 
	U32 iNumAttachedItems;
} EmailV3UIMessage;

AUTO_STRUCT;
typedef struct EmailV3SenderItemsWrapper
{
	EmailV3SenderItem** eaItemsFromPlayer;
} EmailV3SenderItemsWrapper;

AUTO_STRUCT;
typedef struct WebRequestChatMailStruct {
	U32 uID; AST(KEY) // Mail ID (per Account unique)
		U32	sent; // time sent

	char *subject;
	char *body;
	bool bRead : 1;
	char *fromName;					// character name this is from
	Item** eaAttachedItems;
} WebRequestChatMailStruct;

AUTO_STRUCT;
typedef struct WebRequestChatMailList {
	U32 uID; // Character/Account ID this mail belongs to
	WebRequestChatMailStruct ** mail;

	// Used for paging
	U32 uTotalMail; // Total mail this account has
	U32 uPage; // Page # (starting with 0) that this mail list represents
	U32 uPageSize; // Max Number of mails per page this was using
} WebRequestChatMailList;

NOCONST(EmailV3Message)* EmailV3_trh_CreateNewMessage(const char* pchSubject, const char* pchBody, Entity* pSender, const char* pchSenderName, const char* pchSenderHandle);
#define EmailV3_CreateNewMessage(sub, bod, send, name, handle) CONTAINER_RECONST(EmailV3Message, EmailV3_trh_CreateNewMessage(sub, bod, send, name, handle))

NOCONST(EmailV3)* EmailV3_trh_GetOrCreateSharedBankMail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pSharedBank, bool bCreateIfNotFound);
#define EmailV3_GetSharedBankMail(pEnt) (CONTAINER_RECONST(EmailV3, EmailV3_trh_GetOrCreateSharedBankMail(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), false)))

void EmailV3_trh_AddItemsToMessageFromAuctionLot(ATR_ARGS, ATH_ARG NOCONST(EmailV3Message)* pMessage, NOCONST(AuctionLot)* pLot);
enumTransactionOutcome EmailV3_trh_AddItemsToMessageFromEntInventory(ATR_ARGS, ATH_ARG NOCONST(EmailV3Message)* pMessage, EmailV3SenderItem** eaItemsFromInventory, ATH_ARG NOCONST(Entity)* pSourceEnt, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract);
void EmailV3_trh_AddItemsToMessage(ATR_ARGS, ATH_ARG NOCONST(EmailV3Message)* pMessage, NOCONST(Item)** eaItems);

enumTransactionOutcome EmailV3_trh_DeliverMessage(ATR_ARGS, ATH_ARG NOCONST(Entity)* pRecipientAccountSharedBank, ATH_ARG NOCONST(EmailV3Message)* pMessage);

bool EmailV3_GetAllMessagesMatchingID(Entity* pEnt, U32 uID, EmailV3Message* pMessageOut, NPCEMailData* pNPCMessageOut, ChatMailStruct* pChatMailOut);
S32 EmailV3_GetNumNPCMessagesByType(Entity* pEnt, NPCEmailType eType, bool bWithItemsAttached);

void EmailV3_RebuildMailList(Entity *pEnt, Entity* pSharedBank, EmailV3UIMessage ***pppMessagesOut);