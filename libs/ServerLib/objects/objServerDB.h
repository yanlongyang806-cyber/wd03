#pragma once

typedef struct ContainerIOConfig ContainerIOConfig;
typedef struct TransDataBlock TransDataBlock;
typedef enum GlobalType GlobalType;
typedef U32 ContainerID;

AUTO_STRUCT;
typedef struct ServerDatabaseConfig
{
	char *pDatabaseDir;						// "" Defaults to C:/<Product Name>/objectdb
	char *pCloneDir;						// Defaults to pDatabaseDir/../cloneobjectdb
	bool bNoHogs;							// force discrete container file storage.

	bool bShowSnapshots;					// Whether to show/hide the snapshot console window.
	bool bBackupSnapshot;	AST(DEF(true))	// Whether to create backup snapshots

	U32		iIncrementalInterval;			// The number of minutes between incremental hog rotation (defaults to 5)
	U32		iSnapshotInterval;				// The number of minutes between snapshot creation		 (defaults to 60)
	U32		iIncrementalHoursToKeep;		// The number of hours to keep incremental files around.	 (default to 4)

	U32		iDaysBetweenDefrags;			AST(DEFAULT(1))//The number of days between automated defrags. 0 means no automated defrags
	U32		iTargetDefragWindowStart;		AST(DEFAULT(3))//Automated defrags will happen after start and before start + duration.
	U32		iTargetDefragWindowDuration;	AST(DEFAULT(2))//

	ContainerIOConfig *IOConfig;
} ServerDatabaseConfig;

void serverdbSetLogFileName(const char *filename);
void serverdbUpdateCB(TransDataBlock ***pppBlocks, void *pUserData);
void serverdbLogRotateCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog);
void serverdbLogCloseCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog);
void serverdbHandleDatabaseReplayString(const char *cmd_orig, U64 sequence, U32 timestamp);
const ServerDatabaseConfig *GetServerDatabaseConfig(const char *configFile);

void *serverdbGetObject(GlobalType globalType, U32 uID);
void *serverdbGetBackup(GlobalType type, ContainerID id);