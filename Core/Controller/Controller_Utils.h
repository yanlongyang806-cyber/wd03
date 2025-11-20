#pragma once

//sends a text message to all users
char *BroadcastMessage(char* pMessage);

//reloade from localdata the list of commands currently banned on gameservers
char *ReloadGSLBannedCommands(void);

//check for any random frankenbuild exes floating around and prompt the user whether to use them or not
void CheckForPotentialFrankenbuildExes(void);

//should return that "" and NULL and " - - " are all the same, as are "-dothis" and "   -dothis   "
bool DoCommandLineFragmentsDifferMeaningfully(char *pCmdline1, char *pCmdline2);

//if command line had -ServerTypeOverrideLaunchDir, need to apply it after assembling gServerTypeInfo
void ApplyOverrideLaunchDirsAndExeNames(void);

void SimpleServerLog_AddTick(TrackedServerState *pServer);
void SimpleServerLog_AddUpdate(TrackedServerState *pServer, FORMAT_STR const char* fmt, ...);
void SimpleServerLog_MakeLogString(TrackedServerState *pServer, char **ppOutStr);
char *SimpleServerLog_GetLogString(TrackedServerState *pServer);


//stuff relating to the checking to alert when there's a sudden drop in concurrent players
void ReportNumPlayersForForSuddenDrops(int iNumPlayers);
void ResetNumPlayersForSuddenDrops(void);

void CheckForEmptyGameServers(void);

void ControllerStartLocalMemLeakTracking(void);

//NULL machineName means local machine. Always use this for killing things with the controller, as
//it has the logic to only kill things launched from the current directory so that shards wont' interfere
//with each other when possible.
void ControllerKillAll(char *pMachineName, const char *pExeName);

//if doing lots of killing on a single remote machine, better to do a bunch of killallDeferred, then a single DoDeferredKills, this
//means there's only one call made through SentryServer
void ControllerKillAllDeferred(char *pMachineName, const char *pExeName);
void ControllerKillAll_DoDeferredKills(char *pMachineName);


void Controller_HereIsTotalNumPlayersForQueue(int iNumPlayers);

void Controller_DoXperfDumpOnMachine(TrackedMachineState *pMachine, FORMAT_STR const char *pFileNameFmt, ...);


//whenever an MCP connects to the controller, the controller sends it all local notes
void SendAllSingleNotes(TrackedServerState *pServer);

//called when the logserver is "going", so that the controller can now inform all present and future launchers
void Controller_LogServerNowSetAndActive(char *pMachineName);

//returns NULL if it is not yet set/active
char *Controller_GetMachineNameOfActiveLogServer(void);

int Controller_GetNumPlayersInMainLoginQueue(void);
int Controller_GetNumPlayersInVIPLoginQueue(void);


bool VersionMismatchAlreadyReported(char *pVersionString, GlobalType eContainerType);
void VersionMismatchReported(char *pVersionString, GlobalType eContainerType);

typedef struct CompressedFileCache
{
	char *pFileName;
	char *pCompressedBuffer;
	int iCompressedSize;
	int iNormalSize;
	int iCRC;
	//.exe caches always link to the accompanying .pdb cache for convenience
	struct CompressedFileCache *pPDBCache;
} CompressedFileCache;

//might block and be VERY slow
CompressedFileCache *Controller_GetCompressedFileCache(char *pFileName);

//given a list of filenames, does threaded zipping and file loading to ensure that Controller_GetCompressedFileCache will return
//instantly in the future
typedef  void (*GetCompressedFileCacheCB)(CompressedFileCache *pCache, void *pUserData);

//call this and the files will be loaded and zipped in a background thread, and you'll get a CB when the CompressedFileCache is
//ready. Note that this may return instantly if the file is already loaded
void Controller_GetCompressedFileCache_Threaded(char *pFileName, GetCompressedFileCacheCB pCB, void *pUserData);

//smart enough to ignore command line options included in the filename
U32 Controller_GetCRCFromExeName(char *pDirName, char *pLocalName);


void HandleLauncherRequestingExeFromNameAndCRC(Packet *pPak, NetLink *pLink);
void UdpateThrottledFileSends();
void HandleThrottledFileSendingHandshake(Packet *pak);

bool Controller_AreThereFrankenBuilds(void);

//pops up a little message box on the controller machine (same thing alert system does when it can't send the alert)
//
//in a clustered environment, pops it up on the clusterController machine
void Controller_MessageBoxError(const char *pTitle, FORMAT_STR const char* format, ...); 

//if non-NULL, then controller startup is happening
char *Controller_GetStartupStatusString(void);

//there can be any number of string-defined categories, make sure to call with status NULL for each category when that
//category is done
void Controller_SetStartupStatusString(char *pCategory, FORMAT_STR const char* format, ...);

ControllerInterestingStuff *GetInterestingStuff(void);


//Log server stress test is started by the command BeginLogServerStressTest. Executing that command launches a number of game servers.
//Then all game servers, when they get to gslRunning, get sent BeginLogServerStressTestMode
bool Controller_DoingLogServerStressTest(void);
int Controller_GetLogServerStressTestLogsPerServerPerSecond(void);