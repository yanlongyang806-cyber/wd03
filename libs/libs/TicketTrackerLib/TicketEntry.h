#pragma once

#include "language\AppLocale.h"
#include "ticketenums.h"

typedef struct TriviaData TriviaData;
typedef struct TicketCommentConst_AutoGen_NoConst TicketComment;
#define parse_TicketComment parse_TicketCommentConst
#define TYPE_parse_TicketComment TicketComment
typedef struct TicketEntryConst_AutoGen_NoConst TicketEntry;
#define parse_TicketEntry parse_TicketEntryConst
#define TYPE_parse_TicketEntry TicketEntry
typedef struct FileWrapper FileWrapper;
typedef struct SearchData SearchData;
typedef struct TicketData TicketData;

// Resolution type for closed / resolved statuses
AUTO_ENUM;
typedef enum TicketResolution
{
	TICKETRESOLUTION_UNDEFINED = 0,
	TICKETRESOLUTION_FIXED,				ENAMES(Ticket.Resolution.Fixed FIXED)
	TICKETRESOLUTION_AS_DESIGNED,		ENAMES(Ticket.Resolution.WorksAsDesigned AS_DESIGNED)
	TICKETRESOLUTION_DUPLICATE,			ENAMES(Ticket.Resolution.Duplicate DUPLICATE)
	TICKETRESOLUTION_CANNOT_REPRO,		ENAMES(Ticket.Resolution.CannotReproduce CANNOT_REPRO)
	TICKETRESOLUTION_EXTERNAL,			ENAMES(Ticket.Resolution.External EXTERNAL)
	TICKETRESOLUTION_WONT_FIX,			ENAMES(Ticket.Resolution.WontFix WONT_FIX)
	TICKETRESOLUTION_POSTPONED,			ENAMES(Ticket.Resolution.Postponed POSTPONED)

	TICKETRESOLUTION_COUNT,				EIGNORE
} TicketResolution;

AUTO_ENUM;
typedef enum TicketUserType
{
	TICKETUSER_CSR = 1, ENAMES(Ticket.User.CSR CSR)
	TICKETUSER_PLAYER,   ENAMES(Ticket.User.Player PLAYER)
} TicketUserType;

AUTO_ENUM;
typedef enum Platform
{
	PLATFORM_UNKNOWN = 0,				ENAMES(Unknown UNKNOWN)
	PLATFORM_WIN32,						ENAMES(Win32 WIN32)
	PLATFORM_XBOX360,					ENAMES(XBox360 XBOX360)
	PLATFORM_COUNT,						EIGNORE
} Platform;

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER;
typedef struct TicketCommentConst
{
	CONST_STRING_MODIFIABLE pUser;
	CONST_STRING_MODIFIABLE pComment;
} TicketCommentConst;
AST_PREFIX()

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER AST_IGNORE(uID);
typedef struct TicketLog
{
	CONST_STRING_MODIFIABLE actorName; // user who performed the action
	const U32 uTime;					AST(FORMATSTRING(HTML_SECS = 1))
	CONST_STRING_MODIFIABLE pLogString; AST(ESTRING)
} TicketLog;

AUTO_STRUCT AST_CONTAINER;
typedef struct TicketStatusLog
{
	CONST_STRING_MODIFIABLE actorName; // user who performed the action
	const U32 uTime;					AST(FORMATSTRING(HTML_SECS = 1))
	const TicketStatus eStatus;
	const TicketStatus ePrevStatus;
} TicketStatusLog;
AST_PREFIX()

// -------------------------------
// Ticket
typedef U32 ContainerID;
typedef struct JiraIssue JiraIssue;

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER;
typedef struct TicketClientUserInfo
{
	const U32 uFiledTime; AST(FORMATSTRING(HTML_SECS = 1)) // time when ticket was first filed by this user
	const U32 uUpdateTime; AST(FORMATSTRING(HTML_SECS = 1)) // time when ticket was last bumped by user
	
	CONST_STRING_MODIFIABLE pShardInfoString;
	CONST_STRING_MODIFIABLE pVersionString;
	const U32 uAccountID;
	CONST_STRING_MODIFIABLE pAccountName; AST(ADDNAMES(pAccoutName)) 
	CONST_STRING_MODIFIABLE pPWAccountName;
	CONST_STRING_MODIFIABLE pDisplayName;
	const U32 uCharacterID;
	CONST_STRING_MODIFIABLE pCharacterName;
} TicketClientUserInfo;


AUTO_STRUCT AST_CONTAINER;
typedef struct TicketClientGameLocation
{
	CONST_STRING_MODIFIABLE zoneName; AST(ESTRING)
	const Vec3 position;
	const Vec3 rotation;
} TicketClientGameLocation;

// Log of response to user or user description changes
AUTO_STRUCT AST_CONTAINER;
typedef struct TicketCommLog
{
	const TicketUserType eType;
	CONST_STRING_MODIFIABLE userName;
	const U32 uTime;					AST(FORMATSTRING(HTML_SECS = 1))
	CONST_STRING_MODIFIABLE pLogString; AST(ESTRING)
} TicketCommLog;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(uIP);
typedef struct TicketEntryConst 
{
	const U32 uID; AST(KEY)
	const U32 uFlags;
	
	// -------------------------------------------------------------------------------
	// Product Information
	const Platform ePlatform;						AST(FORMATSTRING(XML_DECODE_KEY = 1))
	CONST_STRING_MODIFIABLE pPlatformName;
	CONST_STRING_POOLED pProductName; AST(POOL_STRING)
	CONST_STRING_MODIFIABLE pVersionString; // TODO(Theo) deprecate this; use UserInfo
		
	// -------------------------------------------------------------------------------
	// Ticket Grouping
	CONST_STRING_MODIFIABLE pMainCategory;
	CONST_STRING_MODIFIABLE pCategory;
	const bool bIsInternal; // If ticket was submitted through an internal command

	// -------------------------------------------------------------------------------
	CONST_EARRAY_OF(TicketClientUserInfo) ppUserInfo;
	const TicketClientGameLocation gameLocation; // deprecated; old tickets may still have this value, but new tickets will have the DebugPosString
	CONST_STRING_MODIFIABLE pDebugPosString; AST(ESTRING)
	const Language eLanguage; // This is for the one from the first submitted ticket

	CONST_STRING_MODIFIABLE pUserDescription; AST(FORMATSTRING(XML_ENCODE_BASE64=1))
	CONST_STRING_MODIFIABLE pSummary; AST(FORMATSTRING(XML_ENCODE_BASE64=1))
	CONST_STRING_MODIFIABLE pScreenshotFilename; AST(ESTRING)
	CONST_EARRAY_OF(TriviaData) ppTriviaData; AST(FORCE_CONTAINER FORMATSTRING(XML_DECODE_KEY = 2))

	// -------------------------------------------------------------------------------
	// In-Game Character Information
	const U32 uEntityDescriptorID;
	CONST_STRING_MODIFIABLE pEntityFileName; AST(ESTRING)
	const bool bReadableEntity;

	CONST_CONTAINERID_EARRAY eaiUserDataDescriptorIDs;
	CONST_STRING_EARRAY eaUserDataStrings; AST(FORMATSTRING(XML_ENCODE_BASE64=1))
	CONST_STRING_MODIFIABLE pUserDataFilename; AST(ESTRING)

	// Category-specific information, such as mission or power name
	CONST_STRING_MODIFIABLE pLabel; AST(FORMATSTRING(XML_ENCODE_BASE64=1))
	const U32 uOccurrences;
	const U32 uUniqueAccounts;

	// -------------------------------------------------------------------------------
	// Resolution information
	const U32 uFiledTime;				AST(FORMATSTRING(HTML_SECS=1))	// time when ticket was first filed
	const U32 uLastTime;				AST(FORMATSTRING(HTML_SECS=1))	// most recent time the ticket was seen
	const U32 uEndTime;					AST(FORMATSTRING(HTML_SECS=1))	// time when ticket was resolved
	const U32 uMergedID; // Ticket ID this ticket was merged to
	const TicketStatus eStatus;

	// ---------------------------
	// Server Infomation

	CONST_EARRAY_OF(TicketCommentConst) ppComments;
	CONST_STRING_MODIFIABLE pResponseToUser;
	const bool bAutoResponse;
	const U32 uResponseTime;
	CONST_EARRAY_OF(TicketCommLog) ppResponseDescriptionLog;
	CONST_EARRAY_OF(TicketLog) ppLog;
	CONST_EARRAY_OF(TicketStatusLog) ppStatusLog;

	float fPriority; NO_AST
	float fDistance; NO_AST // distance to last ticket compared to
	bool bJiraIsDirty; NO_AST

	CONST_STRING_MODIFIABLE pDxDiagFilename; AST(ESTRING) // File Path; should be in screenshots folder
	
	// ---------------------------
	// Deprecated Fields
	const U32 uGroupID; // TODO(Theo) Fully remove group feature
	const TicketVisibility eVisibility; AST( ADDNAMES(Visible, bVisible)) // If Ticket is visible to end-users for searches
	const TicketInternalStatus eInternalStatus;
	const U32 uLastModifiedTime;		AST(FORMATSTRING(HTML_SECS=1))	// most recent time the ticket was modified
	const U32 uStartTime;				AST(FORMATSTRING(HTML_SECS=1))	// time when resolution process was started --- TEMPORARY --- 

	CONST_STRING_MODIFIABLE pAccountName; AST(ADDNAMES(pAccoutName)) // deprecated; moved to user info
	CONST_STRING_MODIFIABLE pCharacterName; // deprecated; moved to user info
	CONST_STRING_MODIFIABLE pShardInfoString; // deprecated; moved to user info
	const int iGameServerID; // deprecated; moved to user info
	
	CONST_STRING_MODIFIABLE pEntityStr; // deprecated
	const U32 uUserDataDescriptorID; // deprecated
	CONST_STRING_MODIFIABLE pUserDataStr; // deprecated

	const TicketResolution eResolution;
	const U32 uRepID; // deprecated
	CONST_STRING_MODIFIABLE pRepAccountName;
	const U32 uPriority;
	const U32 uSolutionID; // KB-%d Jira issue - deprecated
	CONST_STRING_MODIFIABLE pSolutionKey; // Usually Knowledge Base jira (KB)
	CONST_STRING_MODIFIABLE pPhoneResKey; // Usually Phone Resolution jira (RC)
	CONST_STRING_MODIFIABLE pJiraKey;
	CONST_OPTIONAL_STRUCT(JiraIssue) pJiraIssue; AST(FORCE_CONTAINER)
} TicketEntryConst;
AST_PREFIX();

AUTO_STRUCT;
typedef struct TicketUserData
{
	STRING_EARRAY eaUserDataStrings;
} TicketUserData;

#define CONTAINER_ENTRY(pContainer) ((TicketEntry*) pContainer->containerData)
#define TICKET_STATUS_IS_CLOSED(eStatus) (eStatus == TICKETSTATUS_CLOSED || eStatus == TICKETSTATUS_RESOLVED || eStatus == TICKETSTATUS_MERGED || eStatus == TICKETSTATUS_PROCESSED)
#define TICKET_FLAG_INTERNAL BIT(0)
#define TICKET_FLAG_RIGHTNOW_QUEUED BIT(1)
#define TICKET_FLAG_RIGHTNOW_FAILED BIT(2) // Mutually exclusive with above bit
#define TICKETOPTION_NO_ANON_COMMENTS 0x1

// Functions for converting various enums into display strings
const char *getPlatformName(Platform ePlatform);
const char *getStatusString(TicketStatus eStatus);
const char *getInternalStatusString(TicketInternalStatus eStatus);
const char *getVisibilityString(TicketVisibility eVisibility);
const char *getResolutionString(TicketResolution eResolution);

void TicketEntry_CalculateAndSortByPriority(SA_PARAM_NN_OP_VALID CONTAINERID_EARRAY *eaiTicketIDs);

void TicketEntry_CsvHeaderCB(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char **estr, ParseTable *pti, void *filter);
void TicketEntry_CsvHeaderFileCB(FileWrapper *file, ParseTable *pti, void *filter);
void TicketEntry_CsvCB(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char **estr, TicketEntry *ticket, void *filter);
void TicketEntry_CsvFileCB(FileWrapper *file, TicketEntry *ticket, void *filter);
void TicketTracker_DumpSearchToCSVString(SA_PRE_OP_OP_STR SA_POST_NN_NN_STR char **estr, SearchData *sd);

void GetTicketFileDir(U32 uID, char *dirname, size_t dirname_size);
const char *GetTicketFileRelativePath(U32 uID, const char *file);
void TicketEntry_WriteScreenshot(TicketEntry *pEntry, TicketData *data);
void TicketEntry_WriteEntity(TicketEntry *pEntry, const char *pEntityStr);
void TicketEntry_WriteDxDiag(TicketEntry *pEntry, const char *pDxDiag);
void TicketEntry_WriteUserData(TicketEntry *pEntry, STRING_EARRAY *eaUserDataStrings);
void appendEntityParseTable(char **estr, U32 uTicketID, U32 uDescriptorID, const char *pParseFileName, bool bXML);