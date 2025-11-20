#ifndef TICKETTRACKER_H
#define TICKETTRACKER_H

#include "TicketEntry.h"

#define RESUBSCRIBE_MIN_TIME (60*60) // One hour minimum time between resubscriptions

/*#define TICKET_MAX_DAILY_COUNT_AGE (14 * 24 * 60 * 60) // In seconds (14 days) - cache age
AUTO_STRUCT AST_CONTAINER;
typedef struct TicketCategoryCount
{
	CONST_STRING_MODIFIABLE pCategory; AST(PERSIST KEY)
	const int iCount; AST(PERSIST)
} TicketCategoryCount;

AUTO_STRUCT AST_CONTAINER;
typedef struct TicketCategoryCountList
{
	CONST_EARRAY_OF(TicketCategoryCount) ppCategories; AST(PERSIST)
} TicketCategoryCountList;

AUTO_STRUCT AST_CONTAINER;
typedef struct TicketDailyCount
{
	const U32 uTime; AST(PERSIST KEY FORMATSTRING(HTML_SECS = 1)) // Clamped to the day (eg. hour:min:sec changed to 00:00:00)
	CONST_EARRAY_OF(TicketCategoryCountList) hourCount; AST(PERSIST)
} TicketDailyCount;*/

AUTO_ENUM;
typedef enum IncomingDataType
{
	INCOMINGDATATYPE_UNKNOWN = 0,
	INCOMINGDATATYPE_ERRORDATA,

	INCOMINGDATATYPE_LINK_DROPPED,

	INCOMINGDATATYPE_MAX, EIGNORE 
} IncomingDataType;

typedef struct TicketClientState
{
	U32   uTimer;
} TicketClientState;

AUTO_STRUCT;
typedef struct TicketEntryList
{
	TicketEntryConst **ppEntries;
} TicketEntryList;

typedef struct NetLink NetLink;
typedef struct TicketData TicketData;
typedef struct Packet Packet;

typedef struct NetComm NetComm;
NetComm * ticketTrackerCommDefault(void);

typedef enum GlobalType GlobalType;
void ticketUserEdit (NetLink *link, TicketEntry *pEntry, const char *username, const char *pSummary, const char *pDescription, bool bAdminEdit);
void ticketMergeMultiple(const char *actorName, TicketEntry *parent, TicketEntry **aquisitions, const char *pNewSummary, const char *pNewDescription);
void ticketMergeExisting (const char *actorName, const char ***eaUniqueAccounts, TicketEntry *parent, TicketEntry *aquisition, const char *pNewSummary, const char *pNewDescription, bool bEditDescription);

void mergeDataIntoExistingTicket (U32 uLinkID, TicketEntry *pEntry, TicketData *pTicketData);
ContainerID createTicket(TicketData *pTicketData, U32 linkID);
TicketEntry * findTicketEntryByID(U32 uID);

void removeTicketTrackerEntry (TicketEntry *pEntry);
bool hashMatchesU32(U32 *h1, U32 *h2);

// -------------------------------------

bool initIncomingSecureData(void);
bool initIncomingData(void);
void shutdownIncomingData(void);
const char * getMachineAddress(void);

U32 ticketTrackerGetOptions(void);
void ticketTrackerSetOptions(U32 uOptions);
void ticketTrackerAddOptions(U32 uOptions);

void ticketTrackerInit(void);
void ticketTrackerTimingTest(void);
void ticketTrackerShutdown(void);
void ticketTrackerOncePerFrame(void);

void ticketExtractCharacterName(char **estr, TicketEntry *pEntry);
void ticketConstructDebugPosString(char **estr, TicketEntry *pTicket);

#endif // ifndef ticket tracker
