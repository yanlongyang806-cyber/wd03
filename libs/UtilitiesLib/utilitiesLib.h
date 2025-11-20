#pragma once
GCC_SYSTEM

#define DEFAULT_SERVER_SLEEP_TIME		1

bool utilitiesLibStartup(void);
bool utilitiesLibStartup_Lightweight(void);


//if you just want things to happen in the obvious fashion without bothering with a clock yourself, use REAL_TIME,
//which is special cased to work
#define REAL_TIME -1.0f, -1.0f
void utilitiesLibOncePerFrame(F32 elapsed, F32 timeStepScale);

int utilitiesLibLoadsPending(void);  // Counts loads form various generic systems

void utilitiesLibUpdateAbsTime(F32 timeStepScale);	// allows updating ABS_TIME independent of OncePerFrame

// Generic global flag on whether or not the app should exit (accessed/set in multiple libs)
bool utilitiesLibShouldQuit(void);
void utilitiesLibSetShouldQuit(bool bShouldQuit);

// Enable or disable FRAMEPERF FPS and long frame logging.
void utilitiesLibEnableFramePerfLogging(bool bEnable);

//gBuildVersion is set to the latest .svn version at AUTO_RUN time
extern int gBuildVersion;
extern char gBuildBranch[128];
const char *GetUsefulVersionString(void);
const char *ProdVersion(void);

//same as GetUsefulVersionString for non-production builds, for production builds chops off all the extra info, so
//instead of returning 
// ST.25.20121026a.21 (SVN 139662(http://code/svn/StarTrek/Baselines/ST.25.20121026a.0(branch svn = 140348)) Gimme 10261221:16:42)(+incrs)
// just returns
// ST.25.20121026a.21
const char *GetUsefulVersionString_Short(void);

//information about the shard this machine is running in. Put here because it's global
char *GetShardInfoString(void);
void SetShardInfoString(ACMD_SENTENCE pShardInfoString);

LATELINK;
char *GetShardNameFromShardInfoString(void);

LATELINK;
char *ShardCommon_GetClusterName(void);

char *DEFAULT_LATELINK_GetShardNameFromShardInfoString(void);
char *GetShardCategoryFromShardInfoString(void);
char *GetShardControllerTrackerFromShardInfoString(void);
bool ShardInfoStringWasSet(void);
//for parsing an externally stored shard info string
const char *GetShardValueFromInfoStringByKey(SA_PARAM_NN_STR const char *shardInfo, SA_PARAM_NN_STR const char *key);
const char *GetProductNameFromShardInfoString(SA_PARAM_NN_STR const char *shardInfo);


//used to check general system health/speed by various generic things
extern S64 gUtilitiesLibTicks;

//the longest single interval between calls to UtilitiesLibOncePerFrame, in msecs
extern U32 giLongestFrameMsecs;

//globally store whether we're in makebinsandexit mode so we don't have to have separate client and server checks
extern bool gbMakeBinsAndExit;
extern char *gpcMakeBinsAndExitNamespace;
extern char *gpcCalcDepsAndExit;

extern bool gbSurpressStartupMessages;

//during multiplxed binning, this gets overriden and sends the filenames over the netlink to the master
void binNotifyTouchedOutputFile(const char *filename);

//this does the actual adding to the internal table
void binNotifyTouchedOutputFile_Inner(const char *filename);

LATELINK;
bool binNotifyTouchedOutputFile_Multiplexed(const char *filename);

void binReadTouchedFileList(void);
void binWriteTouchedFileList(void);
void binDeleteUntouchedFiles(void);

typedef struct HWND__ *HWND;
typedef void (*ShellExecuteCallback)(int iReturnValue, HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd );
typedef void (*ShellExecuteFunc)(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd );
typedef void (*ShellExecuteWithCallbackFunc)(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd, ShellExecuteCallback callback);
void utilitiesLibSetShellExecuteFunc(ShellExecuteFunc shellExecuteFunc, ShellExecuteWithCallbackFunc shellExecuteWithCallback);

#define SW_SHOW             5
void ulShellExecute(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd );
void ulShellExecuteWithCallback(HWND hwnd, const char* lpOperation, const char* lpFile, const char* lpParameters, const char* lpDirectory, int nShowCmd, ShellExecuteCallback callback);

//this comically named variable is for things like SentryServer which do NOT have dev mode or prod mode, do NOT know anything
//about any tricky filesystem anything, etc., so we can turn off alerts, errorfs, and so forth, and not have random asserts
extern bool gbCavemanMode;

void setCavemanMode(void); // Sets this variable and all other things which can be set to make a standalone app behave as desired

typedef void (*TextureNameFixup)(const char *src, char *dst, int dst_size);
extern TextureNameFixup g_texture_name_fixup;

//what step of autoruns we are on. Do not use this unless you really really know what you are doing
extern int giCurAutoRunStep;


//returns the directory where very root-ish config files are located, things like controllerServerSetup.txt
//generally server/config, but being a bit tricky to avoid code/data incompatibility during the switchover
char *GetDirForBaseConfigFiles(void);


//counts "events" as they happen. Tells you if n have happened in the last m seconds. Then doesn't tell you again
//for a while
typedef struct SimpleEventCounter SimpleEventCounter;
SimpleEventCounter *SimpleEventCounter_Create(int iNumEventsICareAbout, int iWithinHowManySeconds, U32 iThrottleSeconds);

//returns true if the n-within-m condition was met (automatically does throttling)
#define SimpleEventCounter_ItHappened(pCounter, iCurTime) SimpleEventCounter_ItHappenedWithInfo(pCounter, iCurTime, NULL, NULL)
bool SimpleEventCounter_ItHappenedWithInfo(SimpleEventCounter *pCounter, U32 iCurTime, const char* info, char** outStr);

void SimpleEventCounter_Destroy(SimpleEventCounter **ppCounter);


//SimpleEventThrottler is very sipmle to SimpleEventCounter, but not quite the same. SimpleEventCounter just tells you when
//there was a clump of things happening so you can generate an alert, and then doesn't generate that alert again for 
//a certainly throttling amount. SimpleEventThrottler is for cases like "when we get too many emails to send to netops people's
//phones in a rush, we want to stop sending them on for a while, but also let them know that you are doing so. 
//
//So if you are getting in the events slowly, you'll get back "OK OK OK OK OK". If they get into too much of a clump you'll
//get "OK OK OKOKOK FIRSTFAIL FAIL FAIL FAIL FAIL FAILFAILFAILFAIL       OK OK OK".
//
//Note that it fully resets after coming out of fail mode, so if you are constantly spamming through the fail time 
//you'll get OKOKOKOKOKFIRSTFAIL after it, rather than immediately getting FIRSTFAIL again
typedef enum SimpleEventThrottlerResult
{
	SETR_OK,
	SETR_FIRSTFAIL,
	SETR_FAIL,
} SimpleEventThrottlerResult;

typedef struct SimpleEventThrottler SimpleEventThrottler;
SimpleEventThrottler *SimpleEventThrottler_Create(int iNumEventsICareAbout, int iWithinHowManySeconds, int iTimeToKeepFailingAfterFirstFail);
SimpleEventThrottlerResult SimpleEventThrottler_ItHappened(SimpleEventThrottler *pCounter, U32 iCurTime);
void SimpleEventThrottler_Destroy(SimpleEventThrottler **ppCounter);


//when multiple shards share GS machines, each one has a sharedMachineIndex which allows them to not fight over 
//global resources on the shared machines
int UtilitiesLib_GetSharedMachineIndex(void);

typedef void ExtraTickFunction(void);
void UtilitiesLib_AddExtraTickFunction(ExtraTickFunction *pTickFunc);


//this really shouldn't be here, but LATELINKs in utilitieslib often have weird limitations because some .h files are 
//included from the autogen files
LATELINK;
char *GetRecvFailCommentString(void);

//another one that really shouldn't be here
//given a struct, generates the response string for an XMLRPC command with that struct
//as the return value
LATELINK;
void XMLRPC_WriteSimpleStructResponse(char **ppOutString, void *pStruct, ParseTable *pTPI);

//I'm a child process of some sort, I want to be constantly monitoring the existence of my parent (in a background thread)
//and exit if it goes away
void UtilitiesLib_KillMeWhenAnotherProcessDies(char *pExeNAme, int iOtherProcessPID);

//a shard, or other server, can optionally have a single "overlord" machine that various parts of it will report to in 
//various ways
char *UtilitiesLib_GetOverlordName(void);

//in case some system wants to hook something up to overlord being set
LATELINK;
void OverlordWasJustSet(void);

//this file is the dumping ground for latelink declarations that don't have anywhere better to go
typedef struct PCLStatusMonitoringUpdate PCLStatusMonitoringUpdate;
LATELINK;
void PCLStatusMonitoring_GetAllStatuses(PCLStatusMonitoringUpdate ***pppUpdates);

//another latelink that should really be somewhere else, except that there are a few places you can't really put latelink
//declarations
//modified version of ParserGetTableName, given an object, distinguishes between the various types of Enitities using their container
//type (for debugging/reporting only)
LATELINK;
const char *ParserGetTableName_WithEntityFixup(ParseTable *table, void *pObject);
