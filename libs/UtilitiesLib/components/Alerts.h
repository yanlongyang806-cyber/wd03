#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "GlobalTypes.h"
#include "timing.h"
#include "EString.h"


typedef struct KeyedAlertList KeyedAlertList;
typedef struct Expression Expression;
typedef struct StashTableImp*			StashTable;

//if you add things here, you should add accompanying html styles called .divAlertLevelFOO in core\data\server\mcp\static_home\css\mcp.css
AUTO_ENUM;
typedef enum
{
	ALERTLEVEL_NONE, //dummy to make safe netsending work better

	ALERTLEVEL_WARNING,
	ALERTLEVEL_CRITICAL,

	ALERTLEVEL_COUNT, EIGNORE
} enumAlertLevel;

AUTO_ENUM;
typedef enum
{
	ALERTCATEGORY_NETOPS, //machines or servers have crashed, or are running out of memory
	ALERTCATEGORY_GAMEPLAY, //some guy just gained 10 levels in 10 seconds, something must be wrong
	ALERTCATEGORY_SECURITY, //someone is trying to attack a system or otherwise doing something they shouldn't be

	ALERTCATEGORY_NONE, //carelessly didn't add this to begin with, don't want to reorder the list

	ALERTCATEGORY_PROGRAMMER, //stuff that is bad, but we haven't really figured out the right paramaters
		//for how to deal with it yet, so send to programmers and don't bug netops
} enumAlertCategory;

AUTO_STRUCT  AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Command1, Command2, Command3, Command4, Key, VNC, String, Level, Category, MostRecentHappenedTime, ErrorLink");
typedef struct Alert
{
	char *pVNC; AST(ESTRING FORMATSTRING(HTML=1))
	const char *pKey; AST(POOL_STRING)
	char *pString; AST(FORMATSTRING(HTML_PREFORMATTED = 1))
	enumAlertLevel eLevel;
	enumAlertCategory eCategory;
	U32 iMostRecentHappenedTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	int iLifespan;
	GlobalType eContainerTypeOfObject;
	ContainerID iIDOfObject;
	GlobalType eContainerTypeOfServer;
	ContainerID iIDOfServer;
	int iPidOfServer;
	char *pMachineName;
	char *pMapName;
	int iErrorID; 
	
	U32 iAlertUID; 
	char AlertUIDStringed[16]; AST(KEY)

	char *pErrorLink; AST(ESTRING FORMATSTRING(HTML=1))


	char *pCommand1; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand2; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand3; AST(ESTRING, FORMATSTRING(command=1))
	char *pCommand4; AST(ESTRING, FORMATSTRING(command=1))

	KeyedAlertList *pList; NO_AST
	char *pPatchVersion;

	//was sent from the Critical Systems page down to the shard, presumably because
	//it was reported to the critical systems page via SendAlert. So don't send it back
	//up lest madness ensue.
	bool bWasSentByCriticalSystems; 

}
Alert;

#define CRASHEDORASSERTED "CRASHEDORASSERTED"

//alert keys are pooled strings. Here are some pointers to common ones, but feel free to locally create your own
extern const char *ALERTKEY_GAMESERVERNEVERSTARTED;
extern const char *ALERTKEY_GAMESERVERNOTRESPONDING;
extern const char *ALERTKEY_GAMESERVERRUNNINGSLOW;
extern const char *ALERTKEY_KILLINGNONRESPONDINGGAMESERVER;
extern const char *ALERTKEY_UGCEDIT_GAMESERVERNOTRESPONDING;
extern const char *ALERTKEY_UGCEDIT_GAMESERVERRUNNINGSLOW;
extern const char *ALERTKEY_UGCEDIT_KILLINGNONRESPONDINGGAMESERVER;
extern const char *ALERTKEY_EMAILSENDINGFAILED;
extern const char *ALERTKEY_VERSIONMISMATCH;
extern const char *ALERTKEY_VERSIONMISMATCH_REJECT;



//Because the same alert system needs to work on controllers and critical systems pages and stuff, each
//alert needs to have global location information that is irrelevant when alerts are occuring inside
//a single executable

//pass in 0 for types and IDs to use GetAppGlobalType() and GetAppGlobalId()

void TriggerAlertEx(const char *pKey, const char *pString, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer,
	  const char *pMachineName, int iErrorID, const char *pFileName, int iLineNum );

void TriggerAlertfEx(const char *pKey, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer,
	  const char *pMachineName, int iErrorID, const char *pFileName, int iLineNum, FORMAT_STR const char *pFmt, ... );

#define TriggerAlert(pKey, pString, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer, iIDOfServer, pMachineName, iErrorID) TriggerAlertEx(pKey, pString, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer, iIDOfServer, pMachineName, iErrorID, __FILE__, __LINE__)
#define TriggerAlertf(pKey, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer, iIDOfServer, pMachineName, iErrorID, pFmt, ...) TriggerAlertfEx(pKey, eLevel, eCategory, iLifespan, eContainerTypeOfObject, iIDOfObject, eContainerTypeOfServer, iIDOfServer, pMachineName, iErrorID, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pFmt), __VA_ARGS__)

void TriggerAlertByStruct(Alert *pAlert);

/*for lifespan alerts, this checks if the alert is already "going", and if so, keeps it going. This is useful rather than
caling full triggerAlert when the string generated is likely to change, which means that triggerAlert will fail
to recognize that this is the same alert, or when it requires actual effort to generate the string.

returns true if retriggered

typical usage:
	if (!RetriggerAlertIfActive(ALERTKEY_GAMESERVERNOTRESPONDING, blah blah))
			{
				estrPrintf(&pAlertString, blah blah

				TriggerAlert(ALERTKEY_GAMESERVERNOTRESPONDING, pAlertString,
					blah blah);
			}
			*/

bool RetriggerAlertIfActive(const char *pKey, GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer);


int Alerts_GetAllLevelCount(void);
int Alerts_GetCountByLevel(enumAlertLevel eLevel);

//total # that have happened since shard started up (not persisted over app restart)
int Alerts_GetTotalCountByLevel(enumAlertLevel eLevel);

//remember that keys must always be allocadded
int Alerts_GetCountByKey(const char *pKey);

typedef void FixupAlertCB(Alert *pAlert);

void AddFixupAlertCB(FixupAlertCB *pCB);

void AcknowledgeAlertByUID(U32 iUID);

void AcknowledgeAllAlertsByKey(char *pKey);

//if this callback is set, then it will be called for all alerts instead of the normal alert CB. (This is how
//alerts on systems other than the controller are redirected to the controller
typedef bool AlertRedirectionCB(const char *pKey, const char *pString, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer, const char *pMachineName,
	  int iErrorID );

//send the alert to this. If the return value is true, stop. If the return value is false,
//keep sending to more alertRedirectionCBs, and eventually to the normal alert code. These are
//called in the order they are added.
void AddAlertRedirectionCB(AlertRedirectionCB *pCB);

//when a state-based alert goes from "on" to "off", call this callback, if set
void SetStateBasedAlertOffCB(FixupAlertCB *pCB);



//defines a state which an alert will periodically check
AUTO_STRUCT;
typedef struct StateBasedAlert
{
	char *pGlobObjType; //will check every object in the resource dictionary with this name. If empty, will just check "globally" once
	char *pExpressionString; //if this expression string is true, the assert will happen
	int iCheckFrequency; AST(DEF(5)) //every n seconds, check every object
	int iLifeSpan; //if the alert system goes this long without seeing this alert happen, turn it back off (defaults to twice iCheckFrequency)
	int iTimeBeforeTriggering; //the condition must be continuously true for this many seconds before triggering
	enumAlertLevel eLevel;
	enumAlertCategory eCategory;
	char *pAlertKey; AST(POOL_STRING) //an all-caps spaceless string like "SLOWGAMESERVER" that can be used for easy querying (ie, it's easier
		//to say "count the SLOWGAMESERVER alerts" than "count the alerts whose string matches "% gameserver is running *" or whatever
	char *pAlertString; //if this string contains a %s, it will be replaced by the keyed name of the object in question. Any other
		//%'s in the string must be escaped sprintf-style
	Expression *pExpression; NO_AST

	PERFINFO_TYPE* piProcess; NO_AST
	U32 iFirstTimeHappened; NO_AST

	StashTable sTriggerTimesByName_Last; NO_AST 
	StashTable sTriggerTimesByName_Cur; NO_AST 
		//for alerts that are associated with an object type, 
		//we need multiple copies of the data in iFirstTimeHappened, one per object, but also
		//double buffered so we don't confuse this frame's data with last frame's data

	char *pFileName; AST(CURRENTFILE)
} StateBasedAlert;

AUTO_STRUCT;
typedef struct StateBasedAlertList
{
	StateBasedAlert **ppAlerts; AST(NAME(alert))
	int iCheckFrequency; //unused when being loaded from disk
	bool bTimedCallbackAdded;
} StateBasedAlertList;

typedef struct ServerSpecificAlertListStuff ServerSpecificAlertListStuff;
typedef struct EventCounter EventCounter;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "iTotalCount, iLast15Minutes, iLastHour");
typedef struct KeyedAlertList
{
	const char *pKey; AST(POOL_STRING)
	int iCurActiveBySeverity[ALERTLEVEL_COUNT];
	int iTotalBySeverity[ALERTLEVEL_COUNT];


	int iTotalCount;

	EventCounter *pEventCounter; NO_AST
	int iLastMinute;
	int iLast15Minutes;
	int iLastHour;
	int iLastDay;

	ServerSpecificAlertListStuff *pExtraStuff; AST(LATEBIND)

	Alert **ppAlerts; AST(NO_INDEX)


} KeyedAlertList;

extern StashTable gKeyedAlertListsByKey;

//ParserLoadFiles from pDirectory, for each filename in ppFileNames sequentially... alerts
//with the same key in later loads replace ones in earlier loads
//
//if bReloading is true, then it does nothing if there are textparser errors and returns a string. If it's false, 
//then it does its best, and alerts if there are textparser errors
char *BeginStateBasedAlerts(char *pDirectory, char **ppFileNames, bool bReloading);

void AddStaticDefineIntForStateBasedAlerts(StaticDefineInt *pDefines);

LATELINK;
void AlertWasJustAddedToList(Alert *pAlert);

LATELINK;
char *GetMachineNameForAlert(Alert *pAlert);

#define CRITICAL_NETOPS_ALERT(pKey, pFmt, ...) TriggerAlertfEx(pKey, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pFmt), __VA_ARGS__)
#define WARNING_NETOPS_ALERT(pKey, pFmt, ...) TriggerAlertfEx(pKey, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pFmt), __VA_ARGS__)

#define CRITICAL_PROGRAMMER_ALERT(pKey, pFmt, ...) TriggerAlertfEx(pKey, ALERTLEVEL_CRITICAL, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pFmt), __VA_ARGS__)
#define WARNING_PROGRAMMER_ALERT(pKey, pFmt, ...) TriggerAlertfEx(pKey, ALERTLEVEL_WARNING, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0, __FILE__, __LINE__, FORMAT_STRING_CHECKED(pFmt), __VA_ARGS__)


//an alert is going to happen potentially a bunch of times, and group all with the same key that happen for the next N seconds together into one larger alert
void TriggerAutoGroupingAlert(const char *pKey, enumAlertLevel eLevel, enumAlertCategory eCategory, 
	int iGroupingTime, FORMAT_STR const char *pFmtString, ...);

void SetAlwaysErrorfOnAlert(bool bSet);


//you can use this at startup time before connection to trans server or controller has been established, it will then save the alert until
//it's ready to go. Also potentially useful to avoid recursion. This is the only threadsafe way to trigger an alert
void TriggerAlertDeferred(const char *pKey, enumAlertLevel eLevel, enumAlertCategory eCategory, FORMAT_STR const char *pFmt, ...);

LATELINK;
bool SystemIsReadyForAlerts(void);

//put this in a main loop, it will alert any time the main loop takes longer than fTime seconds, won't alert again for iResetTime
//seconds, and skips the first nSkipFrames frames (in case startup is slow)
#define ALERT_ON_SLOW_FRAME(fTime, iResetTime, nSkipFrames)			\
{																	\
	int iSlowTimeMsecs = (fTime * 1000.0f);							\
	static U32 siSkipCount = 0;										\
	static S64 iLastTime = 0;										\
	static U32 iAlertCutoffTime = 0;								\
	S64 iCurTime = timeGetTime();									\
	if (iLastTime)													\
	{																\
		if (siSkipCount < nSkipFrames)								\
		{															\
			siSkipCount++;											\
		}															\
		else														\
		{															\
			if (iCurTime - iLastTime > iSlowTimeMsecs && timeSecondsSince2000() > iAlertCutoffTime)										\
			{																															\
				CRITICAL_NETOPS_ALERT("SLOW_FRAME", "%s frame was %d msecs, slower than %d. Will alert again in %d seconds",			\
					GlobalTypeToName(GetAppGlobalType()), (int)(iCurTime - iLastTime), (int)iSlowTimeMsecs, (int)iResetTime);			\
				if (isProductionMode())																									\
				{																														\
					xperfDump("slowframe");																								\
				}																														\
				iAlertCutoffTime = timeSecondsSince2000() + iResetTime;																	\
			}														\
		}															\
	}																\
	iLastTime = iCurTime;											\
}


//wrap these around a block of code to generate an alert if it's slow (not useful for super-short times, as it
//uses milliseconds)
#define ALERT_ON_SLOW_BEGIN()						\
{																	\
	S64 _iBeginTime = timeGetTime();							




#define ALERT_ON_SLOW_END(fTime, iResetTime, message, ...)	{if (_iBeginTime) {		\
	static int _iSlowTimeMsecs = 0;									\
	static U32 _iAlertCutoffTime = 0;								\
	S64 _iEndTime = timeGetTime();								\
	if (!_iSlowTimeMsecs) _iSlowTimeMsecs = fTime * 1000.0f;					\
	if (_iEndTime > _iBeginTime + _iSlowTimeMsecs && timeSecondsSince2000() > _iAlertCutoffTime)					\
	{																\
		char *_pFullString;											\
		estrStackCreate(&_pFullString);								\
		estrPrintf(&_pFullString, message, __VA_ARGS__);				\
		estrConcatf(&_pFullString, " took %d msecs on %s, slower than %d. Will alert again in %d seconds", \
			(int)(_iEndTime - _iBeginTime), GlobalTypeToName(GetAppGlobalType()), _iSlowTimeMsecs, (int)iResetTime);	\
		CRITICAL_NETOPS_ALERT("SOMETHING_SLOW", "%s", _pFullString);				\
		estrDestroy(&_pFullString);									\
		_iAlertCutoffTime = timeSecondsSince2000() + iResetTime;		\
	}																\
}}}

#define ALERT_ON_SLOW_CANCEL() { _iBeginTime = 0; }


typedef struct AlertOnSlowArgs
{
	float fTime;
	int iResetTime;
	char *pMessage;
} AlertOnSlowArgs;

//doesn't actually need to be called, but calling this ensures that you can servermonitor alerts and see
//that there is nothing, as opposed to getting a bad empty page
void InitAlertSystem(void);

LATELINK;
void StateBasedAlertJustTriggered(StateBasedAlert *pStateBasedAlert, const char *pDescription, const char *pTypeName, const char *pName);

