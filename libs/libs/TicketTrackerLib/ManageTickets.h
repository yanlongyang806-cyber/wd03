#pragma once

#include "ticketenums.h"
typedef struct TicketCommentConst_AutoGen_NoConst TicketComment;
#define parse_TicketComment parse_TicketCommentConst
#define TYPE_parse_TicketComment TicketComment
typedef struct TicketEntryConst_AutoGen_NoConst TicketEntry;
#define parse_TicketEntry parse_TicketEntryConst
#define TYPE_parse_TicketEntry TicketEntry

typedef struct NOCONST(TicketEntryConst) NOCONST(TicketEntryConst);
typedef struct TicketLog TicketLog;
typedef enum TicketUserType TicketUserType;
typedef enum TicketResolution TicketResolution;

// Jira Modification
bool ticketChangeSolutionCount(TicketEntry *ticket, const char *key, int iChange);
bool ticketChangeJiraCount(TicketEntry *ticket, const char *key, int iChange);
void ticketInitializeJiraMappings(void);
TicketStatus ticketStatusFromJira (int iJiraStatus);
TicketResolution ticketResolutionFromJira (int iJiraResolution);

bool setJiraKey(TicketEntry *pEntry, const char *actorName, const char *issueKey);
bool setTicketSolution(TicketEntry *pEntry, const char *actorName, const char *solutionKey);
bool setTicketPhoneResolution(TicketEntry *pEntry, const char *actorName, const char *solutionKey);
int ticketEntryChangeResolution(const char *actorName, TicketEntry *pEntry, TicketResolution eResolution);

// General Ticket Modification
void changeTicketTrackerEntryVisible (const char *actorName, TicketEntry *pEntry, TicketVisibility eVisibility);
int ticketEntryChangeStatus(const char *actorName, TicketEntry *pEntry, TicketStatus eStatus);
bool ticketChangeCategory(TicketEntry *pEntry, const char *actorName, const char *pMainCategory, const char *pCategory);

void ticketChangeCSRResponse(TicketEntry *pEntry, const char *username, const char *response);
void ticketAssignToAccountName(TicketEntry *pEntry, const char *pAccountName);
void ticketAddComment(TicketEntry *pEntry, const char *pUser, const char *pText);

void ticketAddGenericLog (TicketEntry *pEntry, const char *actorName, const char *logfmt, ...);
void ticketLogCommunication(TicketEntry *pEntry, TicketUserType eType, const char *username, const char *logString);

void trh_AddGenericLog(NOCONST(TicketEntryConst) *pEntry, const char *actor, const char *logString);
void trh_AddCommunicationLog(NOCONST(TicketEntryConst) *pEntry, TicketUserType eType, const char *actor, const char *logString);

bool ticketUserCanEdit(TicketEntry *pEntry, const char *accountName);
void ticketUserEditInternal(TicketEntry *pEntry, const char *username, const char *pSummary, const char *pDescription, bool bAdminEdit);

// Jira Summary Struct
AUTO_STRUCT;
typedef struct TicketJiraSummary
{
	char *jiraKey; AST(ESTRING)
	U32 uCount;
	U32 *eaiTicketIDs;

	U32 uNumSubscribers; // not stored internally
} TicketJiraSummary;

AUTO_STRUCT;
typedef struct TicketJiraReport
{
	EARRAY_OF(TicketJiraSummary) ppJiras;
} TicketJiraReport;

void TicketTracker_GenerateJiraReport (TicketJiraSummary ***eaTopTen, U32 uStartTime, U32 uEndTime);
void TicketTracker_GenerateSolutionReport (TicketJiraSummary ***eaTopTen, U32 uStartTime, U32 uEndTime);

void TicketAutoResponse_ApplyRule(NOCONST(TicketEntryConst) *pEntry);