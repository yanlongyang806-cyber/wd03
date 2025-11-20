#include "TicketTrackerDB.h"

#include "AutoStartupSupport.h"
#include "file.h"
#include "foldercache.h"
#include "logging.h"
#include "objBackupCache.h"
#include "objContainerIO.h"
#include "objMerger.h"
#include "objServerDB.h"
#include "objTransactionCommands.h"
#include "objTransactions.h"
#include "hoglib.h"
#include "ServerLib.h"
#include "sysutil.h"

#include "TicketTracker.h"
#include "EntityDescriptor.h"
#include "EntityDescriptor_h_ast.h"

extern ParseTable parse_TicketEntry[];
#define TYPE_parse_TicketEntry TicketEntry
char gTicketTrackerDBLogFilename[MAX_PATH];
extern char gTicketTrackerAltDataDir[MAX_PATH];

bool gbIgnoreContainerSource = false;
AUTO_CMD_INT(gbIgnoreContainerSource, StaticHogg) ACMD_CMDLINE;
bool gbCreateSnapshotMode = false;
AUTO_CMD_INT(gbCreateSnapshotMode, CreateSnapshot) ACMD_CMDLINE;
bool gbForceSnapshot = false;
AUTO_CMD_INT(gbForceSnapshot, ForceSnapshot) ACMD_CMDLINE;

#define TT_HOG_BUFFER_SIZE 1024*1024*1024

const char *TicketTrackerGetDatabaseDir(void)
{
	static char path[MAX_PATH] = "";
	if (!path[0])
	{
		sprintf(path, "%s/%s", fileLocalDataDir(), gTicketTrackerAltDataDir);
		forwardSlashes(path);
		printf("Database Path: %s\n", path);
	}
	return path;
}

static const char *TicketTrackerGetLogFilename(void)
{
	static char path[MAX_PATH] = "";
	if (!path[0])
		sprintf(path, "%s/DB.log", TicketTrackerGetDatabaseDir());
	return path;
}

static const char *TicketTrackerGetConfigFilename(void)
{
	return "server/TicketTracker/DBConfig.txt";
}

int getSnapshotInterval(void)
{
	const ServerDatabaseConfig *pConfig = GetServerDatabaseConfig(TicketTrackerGetConfigFilename());
	return pConfig->iSnapshotInterval;
}

void TicketTrackerDBInit(void)
{
	const ServerDatabaseConfig *pConfig;
	PERFINFO_AUTO_START_FUNC();

	if (gbCreateSnapshotMode)
	{
		if (!LockMerger(TICKETTRACKER_MERGER_NAME))
		{
			log_printf(LOG_CHATSERVER, "Merger already running");
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}
	serverdbSetLogFileName(TicketTrackerGetLogFilename());

	objRegisterNativeSchema(GLOBALTYPE_TICKETENTRY, parse_TicketEntry, 
		NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_ENTITYDESCRIPTOR, parse_EntityDescriptor, 
		NULL, NULL, NULL, NULL, NULL);
	BackupCache_RegisterType(GLOBALTYPE_TICKETENTRY, BACKUPCACHE_STASH, 5000);
	RegisterFastLocalCopyCB(GLOBALTYPE_TICKETENTRY, serverdbGetObject, serverdbGetBackup);

	InitObjectLocalTransactionManager(GLOBALTYPE_TICKETTRACKER, NULL);
	pConfig = GetServerDatabaseConfig(TicketTrackerGetConfigFilename());

	// Loading database
	assertmsg(GetAppGlobalType() == GLOBALTYPE_TICKETTRACKER, "Ticket Tracker app type not set");
	objSetContainerSourceToHogFile(STACK_SPRINTF("%stickettracker.hogg", TicketTrackerGetDatabaseDir()),
		pConfig->iIncrementalInterval, serverdbLogRotateCB, serverdbLogCloseCB);

	objSetContainerIgnoreSource(gbIgnoreContainerSource);
	objSetSnapshotMode(gbCreateSnapshotMode);

	if (gbIgnoreContainerSource)
	{
		objSetContainerForceSnapshot(true);
		objSetSnapshotMode(true);
	}
	else if (gbForceSnapshot)
		objSetContainerForceSnapshot(true);

	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
	hogSetMaxBufferSize(TT_HOG_BUFFER_SIZE);

	objSetIncrementalHoursToKeep(pConfig->iIncrementalHoursToKeep);
	objSetCommandReplayCallback(serverdbHandleDatabaseReplayString);
	RegisterDBUpdateDataCallback(objLocalManager(), &serverdbUpdateCB);
	if (gbCreateSnapshotMode)
	{
		objMergeIncrementalHogs(pConfig->bBackupSnapshot, false, false);
		UnlockMerger();
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	else
	{
		loadstart_printf("Loading containers... ");
		objSetPurgeHogOnFailedContainer(true);
		objLoadAllContainers();
		LocalTransactionsTakeOwnership();
		objContainerLoadingFinished();
		loadend_printf("Done.");

		if (gbIgnoreContainerSource)
		{
			gbCreateSnapshotMode = true; // to force it to quit
			objContainerSaveTick();
			objForceRotateIncrementalHog();
			objSaveSnapshotHog(true);
			objMergeIncrementalHogs(pConfig->bBackupSnapshot, false, false);
			objCloseContainerSource();
		}
	}
	PERFINFO_AUTO_STOP_FUNC();
}

// -------------------------------
// Creating Snapshots

bool gbDisableMergers = false;
AUTO_CMD_INT(gbDisableMergers, DisableMerger);
// If set, don't run the merger until after the end of the first period.
bool gbDelayMerger = false;
AUTO_CMD_INT(gbDelayMerger, DelayMerger) ACMD_CMDLINE;

static U32 suLastSnapshotTime = 0;
static int siLastMergerPID = 0;

int TicketTrackerGetMergerPID(void)
{
	return siLastMergerPID;
}

static void TicketTrackerCreateSnapshotInternal(void)
{
	static int poke_count = 0;
	char *estr = NULL;
	U32 currentTime = timeSecondsSince2000();

	// If delayed merging has been requested, don't run the merger on startup.
	if (!suLastSnapshotTime && gbDelayMerger)
	{
		suLastSnapshotTime = currentTime;
		return;
	}

	suLastSnapshotTime = timeSecondsSince2000();
	if (siLastMergerPID)
	{
		if (IsMergerRunning(TICKETTRACKER_MERGER_NAME))
		{
			poke_count++;
			if (poke_count > 1)
				ErrorOrAlert("TICKETTRACKER.MERGER_STILL_RUNNING",
					"TicketTracker merger [pid:%d] is still running! Please wait a few minutes before snapshotting again. If this message repeats, find Theo.\n", siLastMergerPID);
			return;
		}
	}
	poke_count = 0;

	estrStackCreate(&estr);
	estrPrintf(&estr, "%s -CreateSnapshot -NeverConnectToController", getExecutableName());
	estrConcatf(&estr, " -SetErrorTracker %s", getErrorTracker());

	siLastMergerPID = system_detach(estr,1,false);
	estrDestroy(&estr);
}

void TicketTrackerCreateSnapshot(void)
{
	U32 currentTime = timeSecondsSince2000();
	if (gbDisableMergers || currentTime < suLastSnapshotTime + getSnapshotInterval()*60)
		return;
	TicketTrackerCreateSnapshotInternal();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(TicketTracker);
void TT_CreateSnapshot(void)
{
	TicketTrackerCreateSnapshotInternal();
}