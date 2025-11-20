/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef DATABASELIB_H_
#define DATABASELIB_H_

#include "objContainer.h"
#include "objContainerIO.h"
#include "TimedCallback.h"
#include "CommandScheduling.h"
#include "TransactionSystem.h"

typedef struct TransDataBlock TransDataBlock;
typedef struct Container Container;
typedef struct IntAverager IntAverager;
typedef struct CountAverager CountAverager;
typedef struct NamedPathQueriesAndResults NamedPathQueriesAndResults;
typedef struct SerializedContainers SerializedContainers;

typedef enum enumLogCategory enumLogCategory;

bool ObjectDBDoingEmergencyRestart(void);

typedef enum 
{
	DBTYPE_MERGER = -1,
	DBTYPE_INVALID = 0, // This isn't actually an objectDB
	DBTYPE_STANDALONE = 1, // Runs by itself
	DBTYPE_MASTER, // Handles requests, and also forwards to clone
	DBTYPE_CLONE, // Saves to disk, using logs sent from the master
	DBTYPE_DUMPER,	//dump a single hog to sql
	DBTYPE_SCAN,	//scan a single hog for corruption
	DBTYPE_REPLAY,
} DatabaseType;

typedef struct DatabaseState
{
	DatabaseType databaseType;

	bool	bConnectedToClone;
	U32		cloneServerIP; // Host of clone server to connect to
	char	cloneHostName[128]; // override clone host name on command line
	U32 cloneListenPort;
	U32 cloneConnectPort;
	//U32		incrementalInterval; // How often to make a new incremental hog, or 0 to do full hog
	//U32		snapshotInterval; //How often to make a new snapshot.
	U32		lastSnapshotTime;
	U32		lastSnapshotBackupTime;
	U32		lastDefragDay;
	bool	lastMergerWasDefrag;

	char	corruptionScan[MAX_PATH];	//if set start in scan mode.
	char	corruptionExpr[1024];		//the scan expression

	char	dumpSQLData[MAX_PATH];	//if set start in sqldump mode.
	char	dumpType[1024];
	char	dumpOutPath[MAX_PATH];

	bool	bConnectedToMaster;
	bool	bIsolatedMode; // don't connect to transaction server

	bool	bCreateSnapshot; //Don't handle requests, just merge, create snapshot, and exit.
	bool	bCloneCreateSnapshot;	//Same as CreateSnapshot, except for clone
	bool	bDefragAfterMerger;					//After performing a merger, defrag the latest snapshot
	bool	bReplayLogsToClone;	// Read logs off the disk and just pipe them to a connected clone db
	char    dumpWebData[MAX_PATH]; //During load, dump web data to a file.

	int		iStartupWait; //ms wait before loading. Used for debugging.

	bool	bNoMerger;
	int		loadThreads; //threads for loading
	int		mergerThreads; //LoadThreads to specify when launching merger

	bool	bNoSaving; // Don't actually save data, just run the logic
	char	**ppCommandsToRun; // Run these commands while idle

	IntAverager *DBUpdateSizeAverager;
	CountAverager *DBUpdateCountAverager;
	CountAverager *DBUpdateTransferAverager;

	NamedPathQueriesAndResults *pLoginCharacterQueries;

	int lastMergerPID;
} DatabaseState;

AUTO_STRUCT;
typedef struct DatabaseInfo
{
	char ProductName[128];
	U32 DatabaseVersion;
} DatabaseInfo;

typedef struct DatabaseConfig DatabaseConfig;

AUTO_STRUCT;
typedef struct DatabaseConfig
{
	char	CLDatabaseDir[MAX_PATH];
	char	CLCloneDir[MAX_PATH];
	char	*pDatabaseDir;						//"" Defaults to C:/<Product Name>/objectdb
	char	*pCloneDir;							//Defaults to pDatabaseDir/../cloneobjectdb
	bool	bNoHogs;							//force discrete container file storage.

	bool	bShowSnapshots;						AST(DEFAULT(2))//Whether to show/hide the snapshot console window.
	bool	bFastSnapshot;						AST(DEFAULT(1))//If we're making a snapshot do it the fast hog->hog way
	bool	bAppendSnapshot;					//When creating a snapshot, open the new snapshot in appen only mode.
	bool	bBackupSnapshot;					AST(DEFAULT(1))//After making a snapshot back it up ourselves, only works with fast snapshot
	bool	bUseHeaders;						AST(DEFAULT(1))//Enable headers for indexing and saving
	bool	bLazyLoad;							AST(DEFAULT(1))//Load everything into memory, but don't unpack until needed
	bool	bSaveAllHeaders;					//When loading old databases, force a save of every container you add a header for
	bool	bMergerWaitForDebugger;				//When starting a merger, wait for debugger
	bool	bCloneConnectToMaster;				//When loading a clone, this flag controls whether you should connect to the master server (i.e. restarting after a crash, you shouldn't)
	bool	bEnableOfflining;					AST(DEFAULT(1))//Allows to creation of offline.hogg to hold unused Containers.
	bool	bDisableNonCriticalIndexing;		//Disable indexing not critical to basic operation to save memory in lowMemory mode
	bool	bDisablePlaytimeReportingBuckets;	

	U32		iIncrementalInterval;				//The number of minutes between incremental hog rotation (defaults to 5)
	U32		iSnapshotInterval;					//The number of minutes between snapshot creation		 (defaults to 60)
	U32		iIncrementalHoursToKeep;			//The number of hours to keep incremental files around.	 (default to 4)
	U32		iSnapshotBackupInterval;			//The number of minutes between snapshot backups
	U32		iDebugLagOnUpdate;					//The number of milliseconds stalled on each DB update
	U32		iDaysBetweenDefrags;				AST(DEFAULT(1))//The number of days between automated defrags. 0 means no automated defrags
	U32		iTargetDefragWindowStart;			AST(DEFAULT(3))//Automated defrags will happen after start and before start + duration.
	U32		iTargetDefragWindowDuration;		AST(DEFAULT(2))//
	U32		iOfflineThreshold;					AST(DEFAULT(15))//Time since last login to offline a character
	U32		iLowLevelOfflineThreshold;			AST(DEFAULT(1))//Time since last login to offline a low level character
	U32		iLowLevelThreshold;					AST(DEFAULT(15))//The highest level that we still count as low level
	U32		iOfflineThrottle;					AST(DEFAULT(120000))
	U32		iOfflineFrameThrottle;				AST(DEFAULT(10))
	U32		iLowLevelDCCThreshold;				AST(DEFAULT(2*24*60*60))
	U32		iLowLevelStaleThresholdHours;		AST(DEFAULT(5))

	char	CLExportDir[MAX_PATH];

	ContainerIOConfig *IOConfig;
	DatabaseConfig *MasterConfig;
	DatabaseConfig *CloneConfig;
} DatabaseConfig;

typedef void VersionTransitionFunction(GlobalType type, ContainerID id);

extern DatabaseState gDatabaseState;
extern DatabaseConfig gDatabaseConfig;


// Initial program init
void ObjectDBInit(int argc, char **argv);
void ObjectDBDumpStats(void);

// Update the title
void dbUpdateTitle(void);

// Directory where the database files are located
char *dbDataDir(void);

// Hog file to store the database in
char *dbDataHogFile(void);

//Setup global config
int dbConfigureDatabase(void);

// Load all containers
int dbLoadEntireDatabase(void);

// Save out a DatabaseInfo
void dbSaveDatabaseInfo(void);

// Parse and execute a Database Update String, replay = false.
void dbHandleDatabaseUpdateString(const char *pString, U64 sequence, U32 timestamp);

// Parse and execute a Database Update String, replay = true.
void dbHandleDatabaseReplayString(const char *cmd_orig, U64 sequence, U32 timestamp);

// Parse and execute a Database Update String; replay=true will disable logging and require seqence and timestamp to be valid.
void dbHandleDatabaseUpdateStringEx(const char *cmd_orig, U64 sequence, U32 timestamp, bool replay);


// Parse and execute an earray of Database Update TransDataBlocks
void dbHandleDatabaseUpdateDataBlocks(TransDataBlock ***pppBlocks, U64 sequence, U32 timestamp);

// Parse and execute a Database Update String, replay = true.
void dbHandleDatabaseReplayDataBlocks(TransDataBlock ***pppBlocks, U64 sequence, U32 timestamp);

// Parse and execute a Database Update String; replay=true will disable logging and require seqence and timestamp to be valid.
void dbHandleDatabaseUpdateDataBlocksEx(TransDataBlock ***pppBlocks, U64 sequence, U32 timestamp, bool replay);

// Log details about a container
int dbContainerLog(enumLogCategory eCategory, GlobalType type, ContainerID id, bool getLock, const char *action, FORMAT_STR char const *fmt, ...);

// Callback to the transaction system for handling database update strings
void dbUpdateCB(TransDataBlock ***pppBlocks, void *pUserData);

// Autocommand to fire off a snapshot merger process.
char* dbCreateSnapshotLocal(S32 delaySeconds, bool verbose, char *dumpwebdata, bool hideMerger, bool defrag);

// Functions for dealing with database versioning

// Sets the expected database version. Do NOT increment the call to this without setting
// up transition functions
void dbSetDatabaseCodeVersion(U32 version);

// Register a database transition function. For every version transition data goes through, 
// the appropriate transition function will be called
void dbRegisterVersionTransition(U32 version, VersionTransitionFunction loadedfunction, VersionTransitionFunction invalidfunction);

// A db transition function that deletes the passed in container that has been loaded
void dbTransitionDeleteLoadedContainer(GlobalType type, ContainerID id);

// db transition to delete invalid containers
void dbTransitionDeleteInvalidContainer(GlobalType type, ContainerID id);

// Callback function to send updates to the controller
void dbSendGlobalInfo(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

// Functions that are also remote commands

// Returns name@publicaccount, if it's a player
char *dbNameAndPublicAccountFromID(U32 containerType, U32 containerID);

// Returns id from name and account, if it's a player
U32 dbIDFromNameAndPublicAccount(U32 containerType, const char *name, const char *accountName, ContainerID iVirtualShardID);

// Returns id from name and account, if it's a player
U32 dbIDFromNameAndAccountID(U32 containerType, const char *name, int accountID, ContainerID iVirtualShardID);

// Returns the name of container, even if it's a saved pet
char *dbNameFromID(U32 containerType, U32 containerID);

//Do a simple objpath resolve to get .pplayer.accountid
U32 accountIDFromEntityPlayerContainer(Container *con);

// Fills an estring with a container export path based on accountID
void dbAccountIDMakeExportPath(char **estr, U32 accountID);

// Convert a file system path to a http path
void dbChangeFilePathEstringToHttpPath(char **estr);

//Stringify an EntityPlayer container.
bool dbSerializeEntityForExport(char **estr, Container **container);

//Parse a serialized character for import.
//Returns an EntityExport intermediary struct
SerializedContainers *dbParseCharacterEntity(char *fileData);

//Load and fixup an entity for import.
U32 dbLoadEntity(U32 type, SerializedContainers *ee, TransactionRequest *request, U32 setAccountid, const char *setPrivateAccountName, const char *setPublicAccountName, bool overwriteAccountData);

//Destroy an EntityExport struct
void dbDestroyEntityExport(SerializedContainers *ee);

//if true, then only return minimal PossibleCharacterChoices until full ones are requested
bool dbMinimalCharacterChoices(void);

AUTO_STRUCT;
typedef struct XMLRPCNamedFetchInput
{
	char *namedfetch;
	ContainerID *Ids;
	GlobalType type;
} XMLRPCNamedFetchInput;

AUTO_STRUCT;
typedef struct XMLRPCFetchColumn
{
	char name[64];						AST(KEY)
	char *objectpath;
} XMLRPCFetchColumn;

AUTO_STRUCT;
typedef struct XMLRPCNamedFetch
{
	char name[64];						AST(STRUCTPARAM)
	U32 type;							AST(STRUCTPARAM)
	XMLRPCFetchColumn **ppFetchColumns;
} XMLRPCNamedFetch;

AUTO_STRUCT;
typedef struct XMLRPCNamedFetchKey
{
	char *name;							AST(STRUCTPARAM UNOWNED)
	U32 type;							AST(STRUCTPARAM)
} XMLRPCNamedFetchKey;

AUTO_STRUCT;
typedef struct XMLRPCNamedFetchWrapper
{
	XMLRPCNamedFetchKey key;			AST(KEY)
	XMLRPCNamedFetch *fetch;
} XMLRPCNamedFetchWrapper;

XMLRPCNamedFetch *dbGetNamedFetchInternal(char *name, GlobalType type);

AUTO_STRUCT;
typedef struct StalePlayerID
{
	U32 uID; AST(KEY)
} StalePlayerID;

// States defined for the global state machine

#define DBSTATE_INIT "dbInit"

#define DBSTATE_MASTER "dbMaster"

#	define DBSTATE_MASTER_LOAD "dbMasterLoad"

#	define DBSTATE_HOG_SNAPSHOT_MERGER "dbHogSnapshotMerger"

#	define DBSTATE_MASTER_LAUNCH_CLONE "dbMasterLaunchClone"

#	define DBSTATE_MASTER_CONNECT_TO_CLONE "dbMasterConnectToClone"

#	define DBSTATE_MASTER_WAIT_FOR_RETRY "dbMasterWaitForRetryCloneConnect"

#	define DBSTATE_MASTER_MOVE_TO_OFFLINE_HOGG "dbMasterMoveToOfflineHogg"

#	define DBSTATE_MASTER_CLEAN_UP_OFFLINE_HOGG "dbMasterCleanUpOfflineHogg"

#	define DBSTATE_MASTER_HANDLE_REQUESTS "dbMasterHandleRequests"

#	define DBSTATE_MASTER_RUNNING_TEST "dbMasterRunningTest"

#define DBSTATE_CLONE "dbClone"

#	define DBSTATE_CLONE_LOAD "dbCloneLoad"

#	define DBSTATE_CLONE_WAITING_FOR_MASTER "dbCloneWaitingForMaster"

#	define DBSTATE_CLONE_WAIT_FOR_RETRY "dbCloneWaitForRetry"

#	define DBSTATE_CLONE_HANDLE_LOG "dbCloneHandleLog"

#define DBSTATE_CLEANUP "dbCleanup"

#define DBSTATE_DUMPER "dbDump"

#define DBSTATE_SCANNER "dbScan"

#define DBSTATE_REPLAYER "dbReplay"

// How much memory should the Hog system be allowed to use (larger -> better
//  performance if load is spikey)
#define DBLIB_HOG_BUFFER_SIZE 1024*1024*1024

// Increment this when you want to delete/modify old characters.
// Update ObjectDBMain.c to add a new transition function
// This MUST be reset before production, and proper transition functions must be defined

// Reset to 0 so schema files work
// Version 1: Changed child missions on player to be non-indexed.
// Version 2: Submission lists are indexed again, at least until after milestone.  Need to delete characters with unsorted mission lists.
// Version 3: Mission events changed from string indexed to int indexed. Only need to delete invalid characters
// Version 4: Entities changed from graphics costumes to PlayerCostumes.  Need to delete all characters
// Version 6: Powers ownership change, a variety of previous fields are now changed, must delete all characters
// Version 7: Ent/BaseEnt merge
// Version 8: New inventory data layout
// Version 9: Localization changes on entity/critter/item
// Version 10: removed old inventory data from player
// Version 11: Changed Mission structure for server saving (submissions removed from dictionary)
// Version 12: Reorganized lots of data for saved pets
// Version 13: Cleaned up fixups for FC beta character wipe
#define DATABASE_VERSION 13

AUTO_STRUCT;
typedef struct SerializedEntity
{
	char *entityType;
	char *entityData;
} SerializedEntity;

AUTO_STRUCT;
typedef struct SerializedContainers
{
	SerializedEntity **containers;
} SerializedContainers;

bool alreadyInRefs(ContainerRef **refs, GlobalType type, ContainerID ID);

bool GetContainerTypeAndIDFromContainerRef(ParseTable pti[], void *pStruct, int column, int index, char **idObjPath, ContainerID *id, GlobalType *eGlobalType);
bool GetContainerTypeAndIDFromTypeString(ParseTable pti[], void *pStruct, int column, int index, const char *ptypestring, char **idObjPath, ContainerID *id, GlobalType *eGlobalType);

void initOutgoingIntershardTransfer();
void UpdateOutgoingIntershardTransfers();

AUTO_STRUCT;
typedef struct ObjectDBStatus_SubscriptionInfo
{
	U32 eType; AST(FORMATSTRING(HTML_SKIP=1))
	char *pTypeName;
	ContainerID containerID;
	U32 refCount;

	AST_COMMAND("Report", "ContainerSubscriptionReport $FIELD(Type) $FIELD(containerID) $CONFIRM(DO NOT USE THIS ON A LIVE SHARD unless the shard is already wrecked.)")
} ObjectDBStatus_SubscriptionInfo;

AUTO_STRUCT;
typedef struct ObjectDBStatus_ContainerInfo
{
	GlobalType eType; AST(KEY, FORMATSTRING(HTML_SKIP=1))
	char *pName; AST(ESTRING, FORMATSTRING(HTML=1))
	U32 ContainerCount;
	U32 ActiveCount;
	U32 Subscriptions;
	U32 OnlineSubscriptions;
	U32 MaxContainerID;
	U32 DestroyedThisRun;
	F32 SubscriptionsPerPlayer;
	U32 CurrentLockedContainers;
	U64 UpdateCount;
	U64 UpdateLineCount;
	F32 AverageLinesPerUpdate;
} ObjectDBStatus_ContainerInfo;

AUTO_STRUCT;
typedef struct ObjectDBStatus_PlayerStorageInfo
{
	U32 PreloadedPlayers;
	U32 OfflinePlayerCount;
	U32 OnlinedPlayers;
	U32 LastRestoreTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U32 DeletedPlayerCount;
	U32 UnpackedDeletedPlayers;
	U32 NextDelete; AST(FORMATSTRING(HTML_SECS_DURATION_SHORT=1))
} ObjectDBStatus_PlayerStorageInfo;

AUTO_STRUCT;
typedef struct ObjectDBStatus_StorageInfo
{
	GlobalType eType; AST(KEY, FORMATSTRING(HTML_SKIP=1))
	char *pName; AST(ESTRING, FORMATSTRING(HTML=1))
	U32 ContainerCount;
	U32 UnpackedCount;
	U32 UnpackedThisRun;
	U32 RepackedThisRun;
	U32 FreeRepacksThisRun;
	U32 TotalUnpacks;
	U64 UnpackAvgTicks;
} ObjectDBStatus_StorageInfo;

#define MAX_SUBSCRIBED_CONTAINERS_TO_SHOW 5

AUTO_STRUCT;
typedef struct ObjectDBStatus
{
	AST_COMMAND("Check Container Location", "DebugCheckContainerLoc $SELECT(Find location of container type|ENUM_GlobalType) $INT(ID)")
	AST_COMMAND("Dump Container", "ServerMonDumpEntity $SELECT(Container type|ENUM_GlobalType) $INT(ID)")

	char *pGenericInfo; AST(NO_LOG, ESTRING, FORMATSTRING(HTML=1))

	char serverType[256];
	char *pCommandLine; AST(ESTRING)
	ContainerID iID;

	char *ObjectDBCommands; AST(ESTRING, FORMATSTRING(HTML=1))

	U32 iLastReplicatedTimeStamp; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U64 iLastReplicatedSequenceNumber;

	U32 iLastTimeStampProcessedOnClone; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U64 iLastSequenceNumberProcessedOnClone;
	U32 iLastCloneUpdateTimeStamp; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))

	U32 iOutstandingDatabaseRequests;

	U32 iBaseContainerID;
	U32 iMaxContainerID;

	U32 iTotalSubscriptions;
	F32 fSubscriptionsPerPlayer;

	U32 iCurrentlyLockedContainerStores;

	ObjectDBStatus_ContainerInfo **ppContainerInfo;
	ObjectDBStatus_StorageInfo **ppContainerStorageInfo;
	ObjectDBStatus_PlayerStorageInfo PlayerStorageInfo;

	int iInUseHandleCaches;
} ObjectDBStatus;

U32 GetSubscriptionRefCountByType(GlobalType eType);
U32 GetOnlineSubscriptionRefCountByType(GlobalType eType);
void UpdateObjectDBStatusReplicationData(U32 timestamp, U64 sequence);

void ApplyCloneStatusPacket(Packet *pkt);
void SendCloneStatusPacket(NetLink *link);

void dbLogTimeInState(void);

void AlertIfStringCacheIsNearlyFull(void);

ContainerID *GetContainerIDsFromAccountID(U32 accountID);
ContainerID *GetContainerIDsFromAccountName(const char *accountName);
ContainerID *GetContainerIDsFromDisplayName(const char *displayName);
ContainerID *GetDeletedContainerIDsFromAccountID(U32 accountID);
ContainerID *GetDeletedContainerIDsFromAccountName(const char *accountName);
Container *GetOnlineCharacterFromList(ContainerID *eaIDs, int virtualShard);
ContainerID GetOnlineCharacterIDFromList(ContainerID *eaIDs, int virtualShard);
ContainerID *GetPetIDsFromCharacterID(ContainerID id);

void GetUpdatesByContainerType(GlobalType containerType, U64 *countOut, U64 *lineCountOut);

#endif