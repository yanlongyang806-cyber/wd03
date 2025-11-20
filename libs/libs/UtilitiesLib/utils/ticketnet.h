#ifndef TICKETNET_H
#define TICKETNET_H
#pragma once
GCC_SYSTEM
#include "language\AppLocale.h"
#include "ticketenums.h"

typedef struct TriviaList TriviaList;

#define TICKET_MAX_ENTITY_LEN 100000
#define TICKETDATA_ALL_CATEGORY_STRING "all"

typedef struct TicketData TicketData;
typedef void (*TicketReturnCallback) (TicketData *ticket, const char *error, U32 uTicketID, int result);

AUTO_STRUCT;
typedef struct ImageData
{
	U32 uImageSize;
	char *pData;
} ImageData;

AUTO_STRUCT;
typedef struct TicketData
{
	// About the executable itself
	char *pPlatformName;
	char *pProductName;
	char *pVersionString;

	// Basic error info
	U32 uAccountID;
	char *pPWAccountName; AST(NAME(pwAccountName))
	char *pAccountName;
	char *pDisplayName;
	U32 uCharacterID;
	char *pCharacterName;
	char *pMainCategory; AST(ESTRING)
	char *pCategory; AST(ESTRING)
	char *pSummary;
	char *pUserDescription;

	TriviaList *pTriviaList;

	int iProductionMode;

	// Character Info
	char *pEntityPTIStr; AST(ESTRING)
	char *pEntityStr; AST(ESTRING)

	U32 uImageSize;

	// Additional Data
	char *pUserDataStr; AST(ESTRING) // deprecated
	char *pUserDataPTIStr; AST(ESTRING) // deprecated
	char **ppUserDataStr; AST(ESTRING NAME(UserDataList))
	char **ppUserDataPTIStr; AST(ESTRING NAME(UserDataPTIList))
	char *pTicketLabel; AST(ESTRING)
	Language eLanguage;

	char *pShardInfoString;
	int iServerContainerID;

	int iMergeID; // Existing Ticket ID to merge this info into
	TicketVisibility eVisibility; AST(DEFAULT(TICKETVISIBLE_UNKNOWN)) // defaults to UNKNOWN

    U8 uIsInternal : 1;
	// Used in TicketTrackerLib for IP tracking
	U32 uIP; NO_AST

	// used by the game client and game server for temporary associations
	U32 entRef; NO_AST
	char *imageBuffer; NO_AST
	char *imagePath; NO_AST 
	char *pServerDataString; NO_AST
} TicketData;

#define TICKETREQUEST_FILTER_ACCOUNT 0x1
#define TICKETREQUEST_FILTER_CHARACTER 0x2

AUTO_STRUCT;
typedef struct TicketRequestData
{
	U32 uID;
	char *pProduct;
	const char *pAccountName;
	const char *pPWAccountName; AST(NAME(pwAccountName))
	const char *pCharacterName; // For filtering on player account/character name

	const char *pMainCategory;
	const char *pCategory;
	const char *pLabel; // For label-filtered requests
	const char *pShardName;

	const char *pKeyword;
	const char *pDebugPosString; AST(ESTRING)

	int accessLevel;
	
	U32 uIP; NO_AST
} TicketRequestData;

typedef enum TicketStatus TicketStatus;
AUTO_STRUCT;
typedef struct TicketRequestResponse
{
	U32 uID;
	char *pCharacter;
	char *pProduct;
	char *pMainCategory;
	char *pCategory;
	char *pLabel;
	char *pSummary;
	char *pDescription;
	char *pStatus;
	char *pResponse;

	char *pDescriptionBreak; AST(CLIENT_ONLY ESTRING) // convert '\n' to "<br>"
	char *pResponseBreak; AST(CLIENT_ONLY ESTRING) // convert '\n' to "<br>"

	U32 uFiledTime;
	U32 uLastTime;
	U32 uDaysAgo;
	F32 fPriority;
	U32 uCount;

	U32 uSubscribedAccounts;
	bool bSticky;
	bool bVisible; AST(NAME(visible))
	bool bIsSubscribed; // Was the requesting account subscribed to this ticket?
} TicketRequestResponse;
extern ParseTable parse_TicketRequestResponse[];
#define TYPE_parse_TicketRequestResponse TicketRequestResponse

void TicketResponse_CalculatePriority(TicketRequestResponse *response, U32 uCurrentTime);
void TicketResponse_CalculateAndSortByPriority(TicketRequestResponse **ppResponses);

AUTO_STRUCT;
typedef struct TicketRequestResponseList
{
	TicketRequestResponse **ppTickets;
} TicketRequestResponseList;
extern ParseTable parse_TicketRequestResponseList[];
#define TYPE_parse_TicketRequestResponseList TicketRequestResponseList

AUTO_STRUCT;
typedef struct TicketRequestResponseWrapper
{
	char *pListString; AST(ESTRING) // of TicketRequestResponseList type
	char *pTPIString; AST(ESTRING)
	U32 uCRC;
} TicketRequestResponseWrapper;
extern ParseTable parse_TicketRequestResponseWrapper[];
#define TYPE_parse_TicketRequestResponseWrapper TicketRequestResponseWrapper

typedef void (*TicketStatusRequestCallback)(UserData userData, const char *ticketResponse);
void ticketTrackerSendStatusRequestAsync(TicketRequestData *pTicketData, TicketStatusRequestCallback pCallback, UserData userData);
void ticketTrackerSendLabelRequestAsync(TicketRequestData *pTicketData, TicketStatusRequestCallback pCallback, UserData userData);

void ticketTrackerSendLabelRequest(TicketRequestData *pTicketData, TicketStatusRequestCallback pCallback, UserData userData);

typedef struct Packet Packet;


const char * getTicketTracker(void);
bool ticketTrackerWasSet();

// These only work for synchronous tickets
int GetTicketTrackerErrorFlags(void);
const char * GetTicketTrackerErrorMsg(void);
int ticketTrackerGetLastID(void);
// -- end --

TicketData * getTicketDataFromPacket(Packet *pkt);
bool putUserDataIntoTicket (SA_PARAM_NN_VALID TicketData *ticket, SA_PARAM_NN_VALID void *pStruct, SA_PARAM_NN_VALID ParseTable *pti);
void putTicketDataIntoPacket(Packet *pkt, TicketData *pTicketData);

bool ticketTrackerSendTicket(TicketData *pTicketData);
bool ticketTrackerSendTicketPlusScreenshot(TicketData *pTicketData, void *data);

bool ticketTrackerSendTicketAsync(TicketData *pTicketData, TicketReturnCallback cbResponse);
bool ticketTrackerSendTicketPlusScreenshotAsync(TicketData *pTicketData, void *data, TicketReturnCallback cbResponse);
bool ticketTrackerSendTicketEditAsync(TicketData *pTicketData, TicketReturnCallback cbResponse);
bool ticketTrackerSendTicketCloseAsync(TicketData *pTicketData, TicketReturnCallback cbResponse);

void ProcessTicketSendQueue(void);
void ProcessTicketRequestQueue(void);

#define TICKETFLAGS_SUCCESS 0
#define TICKETFLAGS_ERROR BIT(0)
#define TICKETFLAGS_CONNECTION_ERROR BIT(1)

#endif
