#pragma once
#include "StatusReporting.h"
#include "GlobalTypes.h"

void CriticalSystems_InitSystem(void);
void CriticalSystems_PeriodicUpdate(void);

typedef struct CriticalSystem_CategoryConfig CriticalSystem_CategoryConfig;
CriticalSystem_CategoryConfig *FindCategoryConfig(const char *pCatName);



AUTO_STRUCT;
typedef struct CriticalSystem_CategoryConfig
{
	const char *pCategoryName; AST(KEY, POOL_STRING)
	char *pEmailRecipients; AST(ESTRING) //comma-separated list of who will get all emails related to servers in this category
} CriticalSystem_CategoryConfig;

AUTO_STRUCT;
typedef struct CriticalSystem_AlertSuppression
{
	const char *pAlertKey; AST(POOL_STRING)
	GlobalType eServerType;

	char *pSystemName;
	AST_COMMAND("Remove", "RemoveAlertSuppression $FIELD(SystemName) $FIELD(AlertKey) $FIELD(ServerType)")
	U32 iWarnTime;
	U32 iStartedTime;
} CriticalSystem_AlertSuppression;

AUTO_STRUCT;
typedef struct CriticalSystem_VersionTiedAlertSuppression
{
	const char *pAlertKey; AST(POOL_STRING)
	char *pSystemName;
	char *pVersionName;
	AST_COMMAND("Remove", "RemoveVersionTiedAlertSuppression $FIELD(SystemName) $FIELD(AlertKey)")
} CriticalSystem_VersionTiedAlertSuppression;

AUTO_STRUCT;
typedef struct CriticalSystem_Config
{
	char *pName; 
	int iMaxStallTimeBeforeAssumedDeath; AST(DEF(120))
	int iMaxStallTimeWithDisconnectBeforeAssumedDeath; AST(DEF(15))
	int iStallTimeAfterDeathBeforeRestarting; AST(DEF(-1))
	char *pEmailRecipients; //recipients for this server only, not to be confused with global recipients
	const char **ppCategories; AST(POOL_STRING)//what categories this critical system is in.
	CriticalSystem_VersionTiedAlertSuppression **ppVersionTiedAlertSuppresions;
	
	//these should either both be set or both empty. If they're set, then the escalation system kicks in
	//on shard down
	char *pLevel1EscalationEmail;
	char *pLevel2EscalationEmail;

} CriticalSystem_Config;

AUTO_STRUCT;
typedef struct CriticalSystem_SystemDownTime
{
	U32 iTimeWentDown; AST(FORMATSTRING(HTML_SECS=1))
	U32 iTimeRecovered; AST(FORMATSTRING(HTML_SECS=1))
} CriticalSystem_SystemDownTime;

AUTO_STRUCT;
typedef struct CriticalSystem_GlobalConfig
{
	char *pGlobalEmailRecipients; AST(ESTRING) //comma-separated list of who will get emails when various things happen
	char *pSystemDownEmailRecipients; AST(ESTRING) //comma-separated list of who will get emails when a system goes to "failed"
	CriticalSystem_Config **ppCriticalSystems;
	CriticalSystem_SystemDownTime **ppRecentSystemDownTimes;
	CriticalSystem_CategoryConfig **ppCategoryConfigs;
} CriticalSystem_GlobalConfig;


AUTO_ENUM;
typedef enum enumCriticalSystemStatus
{
	CRITSYSTEMSTATUS_NONE,
	CRITSYSTEMSTATUS_STARTUP_NEVER_CONNECTED,
	CRITSYSTEMSTATUS_RUNNING,
	CRITSYSTEMSTATUS_DOWNTIME,
	CRITSYSTEMSTATUS_STALLED,
	CRITSYSTEMSTATUS_FAILED,
	CRITSYSTEMSTATUS_TRYINGTORESTART,

} enumCriticalSystemStatus;

AUTO_ENUM;
typedef enum CritSystemEscalationStatus
{
	ESCSTATUS_NONE,
	ESCSTATUS_READY,
	ESCSTATUS_ACKNOWLEDGED,
	ESCSTATUS_LEVEL1,
	ESCSTATUS_LEVEL2,
} CritSystemEscalationStatus;


AUTO_ENUM;
typedef enum enumCriticalSystemAlertSuppressedStatus
{
	CRITSYSTEMALERTS_NONE,
	CRITSYSTEMALERTS_SOME,
	CRITSYSTEMALERTS_ALL,
} enumCriticalSystemAlertSuppressedStatus;

AUTO_STRUCT;
typedef struct CriticalSystem_Event
{
	U32 iTime; AST(FORMATSTRING(HTML_SECS = 1))
	enumCriticalSystemStatus eNewStatus;
} CriticalSystem_Event;

AUTO_STRUCT;
typedef struct CriticalSystem_EventList
{
	CriticalSystem_Event **ppEvents;
} CriticalSystem_EventList;

typedef struct NetLink NetLink;


AUTO_STRUCT;
typedef struct CriticalSystem_Status
{
	char *pName_Internal; AST(KEY, FORMATSTRING(HTML_SKIP=1))
	char *pLink; AST(ESTRING, FORMATSTRING(html=1))
	char *pVNC; AST(ESTRING, FORMATSTRING(html=1))
	CriticalSystem_Config *pConfig; AST(UNOWNED, FORMATSTRING(HTML_SKIP_IN_TABLE=1, HTML_WRITE_UNOWNED=1))
	enumCriticalSystemStatus eStatus; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "\\q$\\q = \\qDOWNTIME\\q ; divRed ;\\q$\\q = \\qSTALLED\\q ; divWarning1 ; \\q$\\q = \\qTRYINGTORESTART\\q OR \\q$\\q = \\qFAILED\\q OR \\q$\\q = \\qSTARTUP_NEVER_CONNECTED\\q; divWarning2"))
	CritSystemEscalationStatus eEscStatus; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "\\q$\\q = \\qREADY\\q OR \\q$\\q = \\qACKNOWLEDGED\\q ; divAlertLevelWARNING ; \\q$\\q = \\qLEVEL1\\q OR \\q$\\q = \\qLEVEL2\\q; divRed"))

	char *pVersion; AST(ESTRING)
		
	char *pProductName; AST(POOL_STRING FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	char *pShortProductName; AST(POOL_STRING FORMATSTRING(HTML_SKIP_IN_TABLE=1))

	enumCriticalSystemAlertSuppressedStatus eAlertSuppresseds; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "\\q$\\q = \\qALL\\q ; divWarning2 ;\\q$\\q = \\qSOME\\q ; divWarning1"))

	U32 iLastContactTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U32 iTimeBeganDownTime; AST(FORMATSTRING(HTML_SECS = 1, HTML_SKIP_IN_TABLE=1))
	U32 iNextDownTimeAlertTime; AST(FORMATSTRING(HTML_SECS = 1, HTML_SKIP_IN_TABLE=1))
	
	U32 iEscalationBeganTime; AST(FORMATSTRING(HTML_SECS = 1, HTML_SKIP_IN_TABLE=1))
	U32 iFirstEmailEscalationTime; AST(FORMATSTRING(HTML_SECS = 1, HTML_SKIP_IN_TABLE=1))
	U32 iSecondEmailEscalationTime; AST(FORMATSTRING(HTML_SECS = 1, HTML_SKIP_IN_TABLE=1))


	U32 iLastDisconnectTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))

	char *pLink1; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pLink2; AST(ESTRING, FORMATSTRING(HTML=1))

	StatusReporting_GenericServerStatus lastStatus;  AST(EMBEDDED_FLAT)

	char *pCriticalAlerts; AST(ESTRING, FORMATSTRING(HTML=1, HTML_CLASS = "divAlertLevelCRITICAL"))
	char *pWarningAlerts; AST(ESTRING, FORMATSTRING(HTML=1, HTML_CLASS = "divAlertLevelWARNING"))

	char *pBeginDowntime; AST(ESTRING, FORMATSTRING(command=1))
	char *pEndDowntime; AST(ESTRING, FORMATSTRING(command=1))
	char *pModify; AST(ESTRING, FORMATSTRING(command=1))

	char *pStatusLog; AST(ESTRING, FORMATSTRING(HTML=1))

	CriticalSystem_EventList eventList; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))

	U32 IP; AST(FORMAT_IP)

	GlobalType eMyType; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	ContainerID iMyID; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	int iMyMainMonitoringPort;  AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))
	int iMyGenericMonitoringPort;  AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))

	char *pLastAddressesSentEscalationTo; AST(ESTRING FORMATSTRING(HTML_SKIP_IN_TABLE=1))

	int iMainIssue;  //issue number of down/up type issues for this server, but not alerts. Cleared to zero any time the server is up

	StashTable alertIssuesTable; NO_AST

	StashTable alertThrottlesTable; NO_AST

	CriticalSystem_AlertSuppression **ppAlertSuppressions; AST(FORMATSTRING(HTML_SKIP_IN_TABLE=1))

	AST_COMMAND("Add a category", "AddCategory $FIELD(Name_Internal) $STRING(CategoryName) $CONFIRM(Add a category?) $NORETURN")
	AST_COMMAND("Remove a category", "RemoveCategory $FIELD(Name_Internal) $STRING(CategoryName) $CONFIRM(Remove a category?) $NORETURN")
	AST_COMMAND("Suppress all alerts", "AddAlertSuppression $FIELD(Name_Internal) ALL CONTROLLER")
	AST_COMMAND("Suppress alerts", "AddAlertSuppression $FIELD(Name_Internal) $STRING(Key of alert) $STRING(ServerType)")

	AST_COMMAND("Version-tied alert suppression", "AddVersionTiedAlertSuppression $FIELD(Name_Internal) $STRING(What alert key to suppress as long as this system is running the same version)")
	AST_COMMAND("Acknowledge system is Down", "AcknowledgeSystemDown $FIELD(Name_Internal) 0 $STRING(Comment) $NORETURN", "$FIELD(SecondEmailEscalationTime) > 0")

	TimedCallback *pEndDowntimeCB; NO_AST

	NetLink *pNetLink; NO_AST

} CriticalSystem_Status;

/*alerts are grouped two different ways: alertIssues and alertThrottlers. AlertIssues
ensure that if the same error happens TO THE SAME SERVER repeatedly, it has the same
issue number. Alert throttlers ensure that if the same error happens a bunch at once
(for instance, every game server has the same error) only a finite number of actual emails
are sent out*/

AUTO_STRUCT;
typedef struct AlertIssueKey
{
	GlobalType eType;
	ContainerID iID;
	const char *pKey; AST(POOL_STRING)
} AlertIssueKey;

//tracks the issue numbers that are associated with a container inside a crit system
AUTO_STRUCT;
typedef struct CriticalSystem_AlertIssue
{
	int iIssueNum;
	U32 iExpTime;
	AlertIssueKey key;
} CriticalSystem_AlertIssue;

AUTO_STRUCT;
typedef struct CriticalSystem_AlertThrottler
{
	char *pKey; AST(POOL_STRING) // this is NOT the same as the alert key, it has container and issue stuff glommed on
	const char *pAlertKey; AST(POOL_STRING)
	int iCurStage;
	int iCurCount;
	char *pCurGlomString; AST(ESTRING)
		enumAlertLevel eLevel;
	enumAlertCategory eCategory;
	char *pSystemName;
	char shortBody[250];
	GlobalType eAlertServerType;
	GlobalType eAlertObjType;
} CriticalSystem_AlertThrottler;


extern char gOnlySendTo[256];
char GetIssueNumPrefixChar(void);

bool DoesCategoryExist(char *pCatName);

bool IsCategoryOrSystemName(char *pName);

//given a list of strings each of which is either the name of a system or the name of a category,
//returns a larger list containing every list of a system or category that is either in the first list,
//or contains/is contained by something in the first list. (Returns a list of POOL_STRINGS)
void ExpandListOfCategoryOrSystemNames(char **ppInNames, char ***pppOutNames);