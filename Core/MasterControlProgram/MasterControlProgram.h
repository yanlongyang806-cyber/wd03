#pragma once


#include "artisttools.h"
#include "GlobalTypeEnum.h"
#include "net/net.h"

#if !STANDALONE
#include "serverlib.h"
#endif

#include "process_util.h"
#if STANDALONE == 1
#include "resource_standaloneMCP.h"
#elif STANDALONE == 2 // !!!: Hack while I am bootstraping CrypticLauncher <NPK 2008-06-02>
#include "resource_CrypticLauncher.h"
#else
#include "resource_mastercontrolprogram.h"
#endif

#include "SimpleWindowManager.h"
#include "..\..\core\NewControllerTracker\pub\NewControllerTracker_pub.h"

//all the buttons/widgets associated with a single MCP-launchable server
typedef struct
{
	int iHideButtonID; 
	int iDebugButtonID; 
	int iDontAutoLaunchButtonID; 
	int iGoButtonID; 
	int iStatusStringID; 
	int iStateStringID;
	int iEditCommandLineButtonID; 
	int iEditSharedCommandLineButtonID; 
} MCPContainerButtonIDs;

#define MAX_CONTAINER_TYPES 36

//the resource editor can't make a big enough vertical dialog, so we just pile up a bunch of extra buttons on top of each other
#define FIRST_INDEX_OF_BUTTONS_THAT_ARE_PILED_UP 30

#define SERVER_NAME_LENGTH 128

#define NUM_EXTRA_GS_NAMES 4

AUTO_ENUM;
typedef enum
{
	SERVER_NOT_RUNNING,
	SERVER_RUNNING,
	SERVER_CRASHED,
	SERVER_WAITING,
} enumServerState;

#define COMMAND_LINE_SIZE 4096

AUTO_ENUM;
typedef enum
{
	ASKCONTROLLERTOCREATE_NEVER,
	ASKCONTROLLERTOCREATE_ALWAYS,
	ASKCONTROLLERTOCREATE_LOCALONLY,
} enumControllerCreationType;

AUTO_STRUCT;
typedef struct ContainerStaticInfo
{
	int iSlotNum; AST(DEF(-1))// -1 = auto-find. Otherwise, force into this slot
	GlobalType eContainerType; AST(SUBTABLE(GlobalTypeEnum))
	enumControllerCreationType eControllerCreationType; 	
	enumServerState eState; NO_AST
	char *pCommandName;
	char *pFixedCommandLine;
	char stateString[1024]; NO_AST

	bool bIsInMinimalShard; //if true, then this server type is essential for a minimal functional shard
} ContainerStaticInfo;

AUTO_STRUCT;
typedef struct ContainerDynamicInfo
{
	int iSlotNum; NO_AST
	GlobalType eContainerType; AST(SUBTABLE(GlobalTypeEnum))
	bool bHideButtonChecked;
	bool bDebugButtonChecked;

	//default is 2 so that we can specifically set the button to checked for servers that should be off by default
	bool bAutoLaunchButtonChecked; AST(DEF(2))
	char commandLine[COMMAND_LINE_SIZE];
	char sharedCommandLine[COMMAND_LINE_SIZE];

	char overrideExeName[CRYPTIC_MAX_PATH];
	char overrideLaunchDir[CRYPTIC_MAX_PATH];

	bool bForceHideServerType;
	bool bForceShowServerType;
} ContainerDynamicInfo;



AUTO_STRUCT;
typedef struct SavedWindowPos
{
	int iWindowID;
	int x;
	int y;
	int right;
	int bottom;
} SavedWindowPos;


#if STANDALONE
typedef struct ArtistToolStaticInfo ArtistToolStaticInfo;
typedef struct ArtistToolDynamicInfo ArtistToolDynamicInfo;
#endif

//a server configuration specifies which servers are visible on the complex BGB screen. There's always one called "all".


AUTO_STRUCT;
typedef struct ServerTypeConfiguration
{
	char *pName; AST(STRUCTPARAM)
	char *pComment; 
	char *pBasedOn;
	U32 *pAdd; AST(SUBTABLE(GlobalTypeEnum))//ea32
	U32 *pRemove; AST(SUBTABLE(GlobalTypeEnum))//ea32

	U32 *pFixedUpTypes; AST(SUBTABLE(GlobalTypeEnum) NO_WRITE)//ea32
} ServerTypeConfiguration;


AUTO_STRUCT;
typedef struct GlobalMCPStaticSettings
{
	ContainerStaticInfo **ppContainerStaticInfo;

	ArtistToolStaticInfo **ppArtistToolStaticInfo; AST(LATEBIND)

	ServerTypeConfiguration **ppServerTypeConfigurations; AST(NAME(ServerConfig))

} GlobalMCPStaticSettings;

AUTO_STRUCT AST_IGNORE(hashedPassword) AST_IGNORE(PasswordLength) AST_IGNORE(SuppressPopups) AST_IGNORE(CaptureClientErrors);
typedef struct GlobalMCPDynamicSettings
{
	ContainerDynamicInfo **ppContainerDynamicInfo;

	ArtistToolDynamicInfo **ppArtistToolDynamicInfo; AST(LATEBIND)

	SavedWindowPos **ppSavedWindowPositions;
	char globalCommandLine[COMMAND_LINE_SIZE];
	char xboxClientCommandLine[COMMAND_LINE_SIZE];
	char patchedPCClientCommandLine[COMMAND_LINE_SIZE];
	char patchedXboxClientCommandLine[COMMAND_LINE_SIZE];
	char launchClientDirectlyCommandLine[COMMAND_LINE_SIZE];

	char accountName[256];
	char password[256]; NO_AST
	int iPasswordLength; NO_AST

	char mapNameForGameServer[256];

	U32 iGameServerModificationTimeLastTimeMadeSchemas;

	char extraMapName_0[256];
	char extraMapName_1[256];
	char extraMapName_2[256];
	char extraMapName_3[256];
	bool extraMapNoAutoLaunch[4];

	char serverTypeConfig[256];
} GlobalMCPDynamicSettings;

//queued up error dialog
typedef struct 
{
	char *pString;
	char *pTitle;
	char *pFault;
	U32 iHighlight;
} QueuedErrorDialog;

extern QueuedErrorDialog **ppQueuedErrorDialogs;

/*
MCP states:
"init"
"startscreen"

"main"
"main/simple"
"main/complex"
"main/artists"
"main/artsetup"
"main/commandline"
"main/otheroptions"
"main/quitmessage"
*/



int GetNumArtistTools(void);
ArtistToolStaticInfo *GetArtistToolStaticInfo(int iSlotNum);
ArtistToolDynamicInfo *GetArtistToolDynamicInfo(int iSlotNum);

int GetNumContainerTypes(void);
ContainerStaticInfo *GetContainerStaticInfoFromType(int iContainerType);
ContainerStaticInfo *GetContainerStaticInfoFromSlot(int iSlotNum);
ContainerDynamicInfo *GetContainerDynamicInfoFromType(int iContainerType);
ContainerDynamicInfo *GetContainerDynamicInfoFromSlot(int iSlotNum);

extern int gbRunInLocalMode;
extern int gbRunInLocalPublicMode;

extern GlobalMCPStaticSettings gGlobalStaticSettings;
extern GlobalMCPDynamicSettings gGlobalDynamicSettings;
extern MCPContainerButtonIDs gContainerButtonIDs[MAX_CONTAINER_TYPES];
extern MCPArtistToolButtonIDs gArtistToolButtonIDs[MAX_ARTIST_TOOLS];
//extern NetLink *gpLinkToControllerTracker;
extern NetLink *gpLinkToNewControllerTracker;
extern HINSTANCE ghInstance;
extern bool gbArtist;

extern ShardInfo_Basic_List gAvailableShardList;
extern bool gbAvailableShardsChanged;

extern int gClientPID;
extern bool gbRunPublic;
extern bool gbQuit;
extern bool gbComplex;
extern bool gbProgrammer;
extern __time32_t giExecutableTimestamp;
extern char gExecutableFilename[MAX_PATH];
extern bool gbDontCreateController;
extern bool gbPressButtonAutomatically;
extern bool gbStartMinimized;
extern bool gbControllerHasALauncher;
extern bool gbOpenScriptWindow;
extern bool gbLaunchInExistingController;
extern bool gbHttpMonitor;
extern bool gbYouAreMonitoringMCP;

extern char gGenericMessage[1024];

extern char *gpTicket; //estring
extern U32 guAccountID;
extern U32 guTicketID;

typedef struct RdrDevice RdrDevice;
extern RdrDevice *gpMCPRenderDevice;

extern U32 gLastTimeRequestedServerList;

int MCPControllerTrackerCallback(Packet *pak,int cmd, NetLink *link, void *user_data);
void CreateController(bool bStartInDebugger, bool bHide, bool bStartLocalLauncherInDebugger, bool bHideLocalLauncher, char *pControllerScriptFileName);

bool IsStartInDebuggerChecked(GlobalType eServerType);
bool IsHideChecked(GlobalType eServerType);

void MasterControlProgramHandleMsg(Packet *pak,int cmd, NetLink *link,void *userdata);

void WriteSettingsToFile();
void ReadSettingsFromFile();

void CreateAndConnectToController(char *pControllerScriptFileName);

void MasterControlProgramTick();
FILE *GetTempBMPFile(char **ppFileName /*estring*/);

char *GetGlobalCommandLine();
char *GetDynamicCommandLine(int iContainerType);

bool IsCommandLineNonEmpty(char *pString);

void ForceResetAll(char *pControllerScriptFileName);


void SendCommandLineSettingsToController(void);

void StartMainWindow(void);


//prototypes for dlg and tick funcs for all window types

bool MainScreenTickFunc(SimpleWindow *pWindow);
BOOL MainScreenDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

bool MCPStartTickFunc(SimpleWindow *pWindow);
BOOL MCPStartDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

bool ArtistSetupTickFunc(SimpleWindow *pWindow);
BOOL ArtistSetupDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

BOOL EditCommandLineDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

BOOL QuitMessageDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

BOOL OtherOptionsDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

bool ControllerScriptTickFunc(SimpleWindow *pWindow);
BOOL ControllerScriptDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);


BOOL SuperEscaperDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
BOOL sentryServerTestDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
BOOL ServerMonitorControlScreenDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
bool ServerMonitorControlScreenTickFunc(SimpleWindow *pWindow);
BOOL TimingConversionDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);


char *GetStartingScriptFileName(void);

void HandleHereAreLastMinuteFiles(Packet *pPak);
bool TryToGetLastMinuteFiles(int iShardID, bool bGetForXbox);
bool ShouldTryToGetLastMinuteFiles(ShardInfo_Basic *pShard);


extern bool gbCommandLinesMightHaveChanged;

//stuff relating to the MCP's interactions with the SimpleWindowManager
enum
{
	MCPWINDOW_STARTING,
	MCPWINDOW_MAIN,
	MCPWINDOW_ARTSETUP,
	MCPWINDOW_COMMANDLINE,
	MCPWINDOW_CONTROLLERSCRIPT,
	MCPWINDOW_OTHEROPTIONS,
	MCPWINDOW_QUITMESSAGE,
	MCPWINDOW_GENERICMESSAGE,
	MCPWINDOW_SUPERESCAPER,
	MCPWINDOW_UTILITIES,
	MCPWINDOW_GIMMECHECKINS,
	MCPWINDOW_SVNCHECKINS,
	MCPWINDOW_ACCOUNTINFO,
	MCPWINDOW_PURGELOGS,
	MCPWINDOW_XBOXCP,
	MCPWINDOW_SENTRYSERVERTEST,
	MCPWINDOW_SERVERMONITORCONTROL,
	MCPWINDOW_MEMLEAKFINDER,
	MCPWINDOW_CONFIGURE,
	MCPWINDOW_ERRORS,
	MCPWINDOW_TIMINGCONVERSION,
};

enum
{
	MAININDEX_SIMPLE,
	MAININDEX_COMPLEX,
	MAININDEX_ARTIST
};



//a little struct which is for a simple FSM of the status of attempting to connect
//to the controller tracker
typedef struct ControllerTrackerConnectionStatusStruct
{
	U32 iOverallBeginTime; //if 0, then this is our first attempt, or our first attempt since connection failed
	U32 iCurBeginTime; //every 30 seconds, we kill our current link and attempt to connect again
} ControllerTrackerConnectionStatusStruct;

//returns true if connected
bool UpdateControllerTrackerConnection(ControllerTrackerConnectionStatusStruct *pStatus, char **ppResultEString);

//each command-line editing window has an index, which is usually the server type.
//add this amount to edit the shared window instead
#define COMMAND_LINE_WINDOW_SHARED_OFFSET 1000000

//In order to get command line editing working for things other than the "normal" types, we need
//to use some fake global types
#define FAKE_GLOBALTYPE_XBOXCLIENT GLOBALTYPE_CLONEOBJECTDB
#define FAKE_GLOBALTYPE_PATCHED_CLIENT GLOBALTYPE_ENTITYPLAYER
#define FAKE_GLOBALTYPE_PATCHED_XBOX_CLIENT GLOBALTYPE_TESTGAMESERVER
#define FAKE_GLOBALTYPE_LAUNCH_CLIENT GLOBALTYPE_ERRORTRACKER


char *GetNthExtraGameServerNameFromDynamicSettings(int i);
void SetNthExtraGameServerNameFromDynamicSettings(int i, char *pName);

ServerTypeConfiguration *FindServerTypeConfig(char *pName);

extern bool gbNewErrorsInErrorScreen;
extern bool gbDisableNewErrorScreen;

void SendAllOverrideExeNamesAndDirs(void);