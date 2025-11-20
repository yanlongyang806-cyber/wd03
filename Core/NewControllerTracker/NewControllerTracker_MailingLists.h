#pragma once
#include "alerts.h"

typedef struct PointerCounter PointerCounter;


AUTO_STRUCT AST_IGNORE(Comment);
typedef struct CritSystemEmailList_Pub
{
	char *pFileName; AST(CURRENTFILE)

	
	//if you want ALL systems, specify "all" for Systems, leave Categories blank

	//what categories of critical system this applies to. Comma-separated
	char *pCategories; 
	//what specifically named critical systems this applies to. Comma-separated
	char *pSystems; 

	//what kinds of emails to send
	bool bSendAll; //send all emails
	bool bSendDown; //send emails when the system goes down
	bool bSendOther; //send other state change emails

	//can be "ALL". Alternatively each must consist of either a category (NETOPS/GAMEPLAY/SECURITY) or a 
	//severity (WARNING/CRITICAL) or both. So "SECURITY" or "GAMEPLAY WARNING" or "CRITICAL SECURITY"
	//Comma separated, so "SECURITY, GAMEPLAY WARNING" would send all security alerts and all gameplay warning alerts
	//
	//You can also add GAMESERVER or NOT_GAMESERVER (but, as of now, not other container types)
	char *pAlerts; 
	
	//comma-separated list of alert keys which will not be sent to the short list, optionally with
	//<n on the end, ie, "GAMESERVERCRASHED<15" will suppress if fewer than 15 gameserver crashes are
	//grouped together. Can also have >n on the end
	char *pAlertsToSuppressForShortRecipients;

	//comma-separated list of shard categories, emails from which will not be sent to the short list
	char *pCategoriesToSuppressForShortRecipients;

	//comma-separated list of systems, emails from which will not be sent to the short list
	char *pSystemsToSuppressForShortRecipients;

	//these alerts will never be reported no matter what (uses same optional <15 syntax)
	char *pAlertsToAlwaysSuppress;

	//these alerts WILL be reported even if they don't match the normal categories (uses same optional <15 syntax)
	char *pAlertsToAlwaysInclude;

	//comma-separated list. Each item must be days followed optionally by hours. Days is "ALL" or 
	//some subset of MoTuWeThFrSaSu (glommed together... ie, "MoTuWeTh"). Hours is "0800-1600". 
	//If hours goes backwards, then it means go into the next morning, so "2000 - 0600" means "8 p.m. through
	//6 a.m. the next day
	//
	//if none specified at all, then ALL is assumed
	char *pTimes;

	char *pFullRecipients; 
	char *pShortRecipients;

	//if non-empty, then a count of how many alerts of each type (by key) there have been is generated
	//and sent out at 9 a.m. each morning. Note that this is somewhat separate from all of the above, and ignores
	//all the time restrictions, types-of-alerts restrictions, etc... all it cares about is categories and systems
	char *pDailyAlertSummaryRecipients;

//	//will be copied over in entirety to the internal mailing list
//	CritSystemEmailList_AlertComment **ppComments; AST(NAME(Comment))
} CritSystemEmailList_Pub;
			

AUTO_STRUCT;
typedef struct CritSystemEmailListOfLists_Pub
{
	CritSystemEmailList_Pub **ppMailingList;
} CritSystemEmailListOfLists_Pub;


AUTO_STRUCT;
typedef struct CritSystemEmailList_TimeRestriction
{
	bool bDays[7]; //sunday = 0
	int iStartTime; //military time... if no restriction, will be set to 0-2400
	int iEndTime;
} CritSystemEmailList_TimeRestriction;


AUTO_STRUCT;
typedef struct CritSystemEmailList_AlertGroup
{
	bool bAll;
	enumAlertLevel eLevel;
	enumAlertCategory eCategory;
	bool bGameServer;
	bool bNotGameServer;
} CritSystemEmailList_AlertGroup;

AUTO_STRUCT;
typedef struct CritSystemEmailList_AlertWithCount
{
	const char *pKey; AST(POOL_STRING)
	int iMatchIfLessThan; //if both are 0, then always suppress. Only one can be set
	int iMatchIfGreaterThan; 
} CritSystemEmailList_AlertWithCount;

AUTO_STRUCT;
typedef struct PointerCounterResult_ForServerMon
{
	const char *pName; AST(POOL_STRING)
	int iCount;
} PointerCounterResult_ForServerMon;

AUTO_STRUCT;
typedef struct CritSystemEmailList_Internal
{
	char *pName; AST(KEY ESTRING) //copied from filename

	char **ppCategories; AST(POOL_STRING)
	char **ppSystems; AST(POOL_STRING)

	bool bSendAll; //send all emails
	bool bSendDown; //send emails when the system goes down (highest priority, obviously)
	bool bSendOther; //send other emails

	CritSystemEmailList_AlertGroup **ppAlertGroups;

	CritSystemEmailList_TimeRestriction **ppTimeRestrictions;

	CritSystemEmailList_AlertWithCount **ppAlertsToSuppressForShortKeys;

	CritSystemEmailList_AlertWithCount **ppAlertsToAlwaysSuppress;
	CritSystemEmailList_AlertWithCount **ppAlertsToAlwaysInclude;

	char **ppCategoriesToSuppressForShort; AST(POOL_STRING)
	char **ppSystemsToSuppressForShort; AST(POOL_STRING)

	char **ppFullRecipients; AST(POOL_STRING)
	char **ppShortRecipients; AST(POOL_STRING)
	char **ppDailyAlertSummaryRecipients; AST(POOL_STRING)

	PointerCounter *pCriticalAlertCounter; NO_AST
	PointerCounter *pWarningAlertCounter; NO_AST
	PointerCounter *pSuppressedAlertCounter; NO_AST

	//filled in from the above two fields whenever server monitoring happens
	PointerCounterResult_ForServerMon **ppCriticalAlertCountsForServerMon;
	PointerCounterResult_ForServerMon **ppWarningAlertCountsForServerMon;
	PointerCounterResult_ForServerMon **ppSuppressedAlertCountsForServerMon;


	U32 iLastAlertCountingTimeBegan;

} CritSystemEmailList_Internal;

AUTO_STRUCT;
typedef struct CritSystemEmailListOfLists_Internal
{
	CritSystemEmailList_Internal **ppLists;
} CritSystemEmailListOfLists_Internal;


bool LoadMailingLists(char *pDirectory, char **ppErrorString);

typedef enum
{
	EMAILTYPE_NONE,
	EMAILTYPE_SYSTEMDOWN,
	EMAILTYPE_ALERT,
	EMAILTYPE_SYSTEMUP,
	EMAILTYPE_STILLINDOWNTIME,
	EMAILTYPE_STILLSUPPRESSINGALERTS,
	EMAILTYPE_BEINGREMOVED,
} enumCritSystemEmailType;

typedef struct CriticalSystem_Status CriticalSystem_Status;

void SendEmailWithMailingLists(CriticalSystem_Status *pSystem, enumCritSystemEmailType eType,
	enumAlertCategory eAlertCategory, enumAlertLevel eAlertLevel, const char *pAlertKey, 
	GlobalType eAlertServerType, GlobalType eAlertObjType, int iAlertCount,
	char *pFullSubjectString, char *pShortSubjectString, char *pFullBodyString,
	char *pShortBodyString, char **ppOutWhoSentToEstring);

//this never causes an email to be sent, but instead registers the alert with the "24 hour summary" 
//producing code
void ReportAlertToMailingLists(CriticalSystem_Status *pSystem, const char *pPooledAlertKey, enumAlertLevel eLevel, bool bSuppressed);

void MailingLists_Tick(void);

//pTo is a comma-separated list
void SendEmail_Simple(char *pTo, char *pSubject, char *pBody);