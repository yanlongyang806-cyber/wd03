#pragma once

#include "accountnet.h"
#include "pcl_typedefs.h"
#include "windefinclude.h"
#include "Organization.h"

#define FC_FOLDER "C:/Program Files/Champions Online"
#define CONTROLLERTRACKER_HOST "controllertracker." ORGANIZATION_DOMAIN

#define QA_CONTROLLERTRACKER_HOST "qact." ORGANIZATION_DOMAIN
#define QA_ACCOUNTSERVER_HOST "qaa." ORGANIZATION_DOMAIN

#define DEV_CONTROLLERTRACKER_HOST "dct." ORGANIZATION_DOMAIN
#define DEV_ACCOUNTSERVER_HOST "da." ORGANIZATION_DOMAIN

#define PWRD_CONTROLLERTRACKER_HOST "pwrdct." ORGANIZATION_DOMAIN
#define PWRD_ACCOUNTSERVER_HOST "pwrda." ORGANIZATION_DOMAIN
#define PWRD_LAUNCHER_URL "http://pwrdlauncher." ORGANIZATION_DOMAIN "/"
#define PWRD_PATCHSERVER_HOST "pwrdpatchserver." ORGANIZATION_DOMAIN

#define ENV_USER "cryptic"

// Number of timer samples to keep
#define LAUNCHER_SPEED_SAMPLES 64

// Number of seconds to wait after a PCL error to restart
#define RESTART_DELAY 10

// Custom messages
#define WM_APP_TRAYICON (WM_APP+1)

// Forward defines
typedef struct IOleObject IOleObject;
typedef struct IHTMLDocument2 IHTMLDocument2;
typedef struct NetComm NetComm;
typedef struct NetLink NetLink;
typedef struct ControllerTrackerConnectionStatusStruct ControllerTrackerConnectionStatusStruct;
typedef struct ShardInfo_Basic_List ShardInfo_Basic_List;
typedef struct ShardInfo_Basic ShardInfo_Basic;
typedef	struct PCL_Client PCL_Client;
typedef struct SimpleWindow SimpleWindow;
typedef void *XLOCKFREE_HANDLE;

typedef enum
{
	CL_STATE_START,
	CL_STATE_LOGINPAGELOADED,
	CL_STATE_LOGGINGIN,
	CL_STATE_LOGGEDIN,
	CL_STATE_GOTSHARDS,
	CL_STATE_GETTINGPAGETICKET,
	CL_STATE_GOTPAGETICKET,
	CL_STATE_LAUNCHERPAGELOADED,
	CL_STATE_SETTINGVIEW,
	CL_STATE_WAITINGFORPATCH,
	CL_STATE_GETTINGFILES,
	CL_STATE_READY,
	CL_STATE_GETTINGGAMETICKET,
	CL_STATE_ERROR,
	CL_STATE_LOGGINGINAFTERLINK,		// Website performed linking, proceeding with login like CL_STATE_LOGGINGIN
	CL_STATE_LINKING,					// Website is performing linking, ignore page loads
} enumCrypticLauncherState;

typedef enum
{
	CL_BUTTONSTATE_DISABLED, // Maps to do_set_button_state('disabled')
	CL_BUTTONSTATE_PATCH, // 'patch'
	CL_BUTTONSTATE_PLAY, // 'play'
	CL_BUTTONSTATE_CANCEL, // 'cancel'
} enumCrypticLauncherButtonState;

// Enum values for window types.
enum 
{
	CL_WINDOW_LOGIN,
	CL_WINDOW_MAIN,
	CL_WINDOW_OPTIONS,
	CL_WINDOW_AUTOPATCH,
	CL_WINDOW_XFERS,
};

// Enum values for custom messages
// !!!: Some of these values are hard-coded as numbers in the website. Do not change the numeric value of anything to be safe. <NPK 2009-06-02>
enum
{
	CLMSG_PAGE_LOADED=1,
	CLMSG_ACTION_BUTTON_CLICKED=2,
	CLMSG_OPTIONS_CLICKED=3,
	CLMSG_LOGIN_SUBMIT=4,
	CLMSG_OPEN_XFER_DEBUG=5,
	CLMSG_RELOAD_PAGE=7,
	CLMSG_RESTART_PATCH=8,
};

typedef struct
{
	char *project;

	// Base folder for patching
	char *root;

	// 	What version to patch up to
	char *view_name;
	U32 view_time;
	U32 branch;
	char *sandbox;

	// Server to talk to
	char *server;
	U32 port;

	// Should we try patching even if it looks done?
	bool force_patch;
} PatchClientConfig;

typedef struct LauncherSpeedData
{
	U32 deltas[LAUNCHER_SPEED_SAMPLES];
	F32 times[LAUNCHER_SPEED_SAMPLES];
	S64 cur, last;
	U8 head;
} LauncherSpeedData;

typedef enum
{
	// To PCL
	CLCMD_START_PATCH,
	CLCMD_DO_BUTTON_ACTION,
	CLCMD_STOP_THREAD,
	CLCMD_FIX_STATE,
	CLCMD_RESTART_PATCH,
	// From PCL
	CLCMD_DISPLAY_MESSAGE,
	CLCMD_SET_PROGRESS,
	CLCMD_SET_PROGRESS_BAR,
	CLCMD_SET_BUTTON_STATE,
	CLCMD_PUSH_BUTTON,
	CLCMD_START_LOGIN_FOR_GAME,
} enumCrypticLauncherCommandType;

typedef struct CrypticLauncherCommand
{
	enumCrypticLauncherCommandType type;
	union
	{
		U32 int_value;
		char *str_value;
		void *ptr_value;
	};
} CrypticLauncherCommand;

typedef struct CrypticLauncherWindow
{
	SimpleWindow *window;
	
	// The current state of the launcher process
	enumCrypticLauncherState state;

	// The active game ID
	U32 gameID;

	// Original command line
	char *commandline;

	// Base URL for the launcher site
	char *baseUrl;

	// Currently loaded URL
	char *currentUrl;
	U32 currentLocale;

	// Authentication information
	char username[MAX_LOGIN_FIELD];
	char password[MAX_PASSWORD];
	AccountLoginType loginType;
	bool accountServerSupportsLoginFailedPacket;			// If set, ignore legacy login packets.
	bool forceMigrate;										// If set, use forced migration retry semantics if PW login fails.
	bool passwordHashed;									// If set, the password field is already hashed

	// PW conflict resolution URL, provided by website.
	char *conflictUrl;
	
	// Comm block for all launcher networking
	NetComm *comm;
	NetComm *pclComm;

	// True if we are running in generic-launcher-for-all-games mode
	bool allMode;

	// True if we are running in standalone creator mode
	bool avatarMode;

	// Current value of the status line
	char *statusText;

	// Should we start the next game with -safemode?
	bool useSafeMode;

	// Should we do a full verification of all files?
	bool askVerify;
	bool forceVerify;
	bool isVerifying;

	// Tray icon options
	bool showTrayIcon;
	bool minimizeTrayIcon;

	// Should we launch the game as soon as patching is done?
	bool autoLaunch;

	// Browser COM object memory block, see embedbrowser.c
	IOleObject **browserPtr;
	IHTMLDocument2 *htmlDoc;

	// Controller tracker and shard list data
	NetLink *linkToCT;
	ControllerTrackerConnectionStatusStruct *ctConnStatus;
	bool shardsChanged;
	ShardInfo_Basic_List *shardList;
	char *lastControllerTracker;
	
	// If set, patch to this shard as soon as possible.
	ShardInfo_Basic *fastLaunch;

	// PCL-related structures
	PatchClientConfig *config;
	PCL_Client *client;
	F32 elapsedLast;
	int patchTicks;
	time_t restartTime;
	int restartLast;
	PCL_ErrorCode restartError;
	int http_percent;

	// Patch speed tracking
	U32 delta_timer;
	LauncherSpeedData speed_received, speed_actual, speed_link;

	// Extra command-line arguments to pass to GameClient
	char *commandLine;

	// For accountnet
	bool accountHasTicket;
	NetLink *accountLink;
	U32 accountID, accountTicketID;
	char *accountTicket;
	char *accountError;
	time_t accountLastFailureTime;

	// Recently launched games
	char **history;

	// Command queues to and from the PCL thread
	XLOCKFREE_HANDLE queueToPCL;
	XLOCKFREE_HANDLE queueFromPCL;

	// Which, if any, proxy server to use
	char *proxy;
	bool proxy_patching;

	// Disable micropatching
	bool disable_micropatching;

	// Debugging
	HRESULT com_initialization_status;

} CrypticLauncherWindow;

bool DisplayMsg(SimpleWindow *window, const char *msg);
bool setButtonState(SimpleWindow *window, enumCrypticLauncherButtonState state);