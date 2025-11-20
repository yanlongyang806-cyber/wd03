#ifndef SERVERLIB_H_
#define SERVERLIB_H_

// This file defines the base of server communication. comm_backend.h is the per-project equivalent

#include "net/net.h"
#include "GlobalTypeEnum.h"
#include "globalcomm.h"

typedef struct CoarseTimerManager CoarseTimerManager;
typedef struct ErrorMessage ErrorMessage;
typedef struct DynamicPatchInfo DynamicPatchInfo;





// Basic runtime configuration. This replaces db_state.
typedef struct ServerLibState
{
	int		bAllowErrorDialog; // Are we allowed to show dialogs here?

	int		bUseMultiplexerForLogging;
	int		bUseMultiplexerForTransactions;
	int		bProfile;

	// These two are only used if bUseMultiplexerForLogging is false
	//
	//if logServerHost is "NONE", then no logserver will be expected. If it's
	//empty, it defaults to localhost
	char	logServerHost[256];

	// These two are only used if bUseMultiplexerForTransactions is false
	char	transactionServerHost[256];
	int		transactionServerPort;

	//the container type used to be here. It is now find by calling GetAppGlobalType()
	ContainerID		containerID; //this server's unique container ID

	U32		antiZombificationCookie; //this server's anti-zombification cookie... compared to other servers to make sure they were all launched
									//by the same instantiation of whatever launched them


	char	controllerHost[256]; //IP address for where to find the Controller. "NONE" if no controller connection desired

	// Operations we should do

	int		removeBins;
	int		writeSchemasAndExit;
	int		fixupAllMapsAndExit;
	int		fixupAllMapsDryRun;
	int		calcSizesAllMapsAndExit;
	char	validateUGCProjects[256];

	int		dontLoadExternData;
	// This option below seems rather pointless - AM
	int		dontLoadData; // Don't load any data 

//if true, sends all errorf errors to the MCP, even if the console window is not hidden
	bool	bSendAllErrorsToController;

	//used by gameservers, mapmanager, and loginserver, so I'm sticking it here
	bool bUseAccountPermissionsForMapTransfer;
	
	// allow keys to be read in developer mode
	bool bAllowDeveloperKeyAccess;

	int iGenericHttpServingPort; //if non-zero, do generic http serving from this port
	
	U32 iSlowTransationCount; //count how many "slow transactions" (as calculated in runAutoTransCB) this server has seen

	char *pcEditingNamespace; // If we're in production edit mode, this is the namespace we're editing. (Dont' request from ResourceDB)
} ServerLibState;

extern ServerLibState gServerLibState;

//serverlib options that are read from ServerLibCfg.txt... note that for controllers, this should now
//be set by -SetControllerTracker on the command line from shardLauncher
AUTO_STRUCT;
typedef struct ServerLibLoadedConfig
{
	char controllerTrackerHost_obsolete[256]; AST(NAME(controllerTrackerHost))
	char newControllerTrackerHost_internal[256]; AST(NAME(newControllerTrackerHost))
	char qaControllerTrackerHost_internal[256]; AST(NAME(qaControllerTrackerHost))
} ServerLibLoadedConfig;

extern ServerLibLoadedConfig gServerLibLoadedConfig;

extern bool gbMakeVOTXTFilesAndExit;


// Returns a queued error message, for communication to a client. This was in svrError.h before
char *GetQueuedError(void);

// Forces logs to be flushed
void svrLogForceFlush(void);

// Forces logs to be flushed, after we've disconnected
void svrLogDisconnectFlush(void);

// Call when you first setup a server, before any network connections are made
void serverLibStartup(int argc, char **argv);

// Call each frame
void serverLibOncePerFrame(void);

// Shuts down serverlib stuff, and then exists
void svrExitEx(int returnVal, char *pFile, int iLine);
#define svrExit(returnVal) svrExitEx(returnVal, __FILE__, __LINE__)

// Enable or disable error dialogs for the server
void serverLibEnableErrorDialog(bool bEnable);

// Log a full mem report of this process, using name as a label (don't put spaces in it!)
void serverLibLogMemReport(const char *name);

// Log a short mem report (50 lines), using name as a label (don't put spaces in it!)
void serverLibLogMemShortReport(const char *name);

//call this function at startup if you are using the Global State Machine (GSM), and wish your server's state to
//be registered and updated with the controller (useful for various reasons)
void slSetGSMReportsStateToController(void);

LATELINK;
void dbSubscribeToContainer(GlobalType ownerType, ContainerID ownerID, GlobalType conType, ContainerID conID, const char* reason);

LATELINK;
void dbUnsubscribeFromContainer(GlobalType ownerType, ContainerID ownerID, GlobalType conType, ContainerID conID, const char* reason);

LATELINK;
void dbSubscribeToOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType);

LATELINK;
void dbUnsubscribeFromOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType);

void ServerLibSetControllerHost (SA_PARAM_NN_STR const char *host);


//in QA shards, clients are notified about map transfer stuff to make it easy to debug stuff. This
//function takes a message from the controller or mapmanager and sends it to the client
LATELINK;
void SendDebugTransferMessageToClient(U32 iCookie, char *pMessage);

//part of DebugCheckContainerLoc system
LATELINK;
void DoYouOwnContainerReturn_Internal(int iRequestID, bool bIsTrans, bool bIOwn);

//graceful server shutdown... currently only used by gameservers
LATELINK;
void GoAheadAndDie_ServerSpecific(void);

// Server error handler that generates critical alerts
void serverErrorAlertCallbackCritical(ErrorMessage *errMsg, void *userdata);

// Server error handler that generates warning alerts
void serverErrorAlertCallbackWarning(ErrorMessage *errMsg, void *userdata);

// Server error handler that generates programmer alerts
void serverErrorAlertCallbackProgrammer(ErrorMessage *errMsg, void *userdata);

// Patch any needed dynamic data. Must be called before the filesystem loads
void ServerLibPatch(void);

// Returns the patch info for those interested
DynamicPatchInfo *ServerLib_GetPatchInfo(void);

// Allows setting the patch info. Introduced a setter so that UGCImport tool could use existing GameServerLib functions for saving and patching up UGC projects
void ServerLib_SetPatchInfo(DynamicPatchInfo *pDynamicPatchInfo);

// Return true if dynamic patching should be used.
bool ServerLib_DynamicPatchingEnabled(void);

// Upload files to the configured patch server
bool ServerLibPatchUpload(char **paths, const char *author, char **estrErrorMessage, const char *comment, ...);


//note that you can only call this if your server type has InformAboutShardLockStatus set in ControllerServerSetup.txt
bool ControllerReportsShardLocked(void);

//similarly for bInformAboutAllowShardVersionMismatch
bool ControllerReportsAllowShardVersionMismatch(void);

//only works on servers where InformAboutNumGameServerMachines is set in ControllerServerSetup.txt
LATELINK;
int ServerLib_GetNumGSMachines(void);

//used by several commands which set values based on num gameserver machines... so
//legal values would be either "12" or "14_PER_GS_MACHINE"
#define PER_GS_MACHINE_SUFFIX "_PER_GS_MACHINE"

//returns true if properly formatted string
bool ServerLib_GetIntFromStringPossiblyWithPerGsSuffix(char *pStr, U32 *piOutVal, bool *pbOutHadGSSuffix);

// Request an online character ID, given an account ID
typedef void (*OnlineCharacterIDFromAccountIDCB)(U32 resultID, void *userData);
void RequestOnlineCharacterIDFromAccountID(U32 accountID, OnlineCharacterIDFromAccountIDCB pCB, void *userData);

LATELINK;
void dbOnlineCharacterIDFromAccountID(GlobalType eSourceType, ContainerID uSourceID, U32 uRequestID, U32 uAccountID);

LATELINK;
bool SpecialMapTypeCorruptionAlertHandling(const char *pKey, const char *pString);

LATELINK;
char *GetControllerTrackerHost(void);

LATELINK;
char *GetQAControllerTrackerHost(void);

typedef struct TextParserBinaryBlock TextParserBinaryBlock;
LATELINK;
void TextureServerReturn(int iRequestID, TextParserBinaryBlock *pTexture, char *pErrorString);

//a debug-only dev mode function to check if a server type exists in the shard. For instance, to make sure that people
//who are calling team functions remembered to turn on the teamserver in their MCP
#define VerifyServerTypeExistsInShard(eType) { if (isDevelopmentMode()) VerifyServerTypeExistsInShardEx(eType); }
void VerifyServerTypeExistsInShardEx(GlobalType eType);


LATELINK;
void GetHeadShot_ReturnInternal(TextParserBinaryBlock *pData, char *pMessage, U32 iUserData);

void ReportOwnedChildProcess(char *pChildProcName, U32 iChildProcPid);

//if alerts happen during server startup they will end up being saved locally in the alerts dictionary and not sent
//to the controller... if so, then when we want to "flush" them all to the controller when that connection is established,
//either via controllerLink or because we connected to the trans server
void SendAllLocallySavedAlertsToController(void);

//serverlib-specific stuff to do upon connection to the trans server
void ServerLib_ConnectedToTransServer(void);

#endif
