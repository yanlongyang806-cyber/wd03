#pragma once
GCC_SYSTEM

#include "pcl_typedefs.h"

typedef struct DirEntry DirEntry;
typedef struct PCL_Client PCL_Client;
typedef struct StashTableImp*			StashTable;

// Define to turn on extra PCL packet timing
// #define DEBUG_PCL_TIMING 1

// Internal PCL debugging logs.
// TODO: Unify pcllog, patchmelog, pclMSpf, pclSendReport, pclSendLog(), and explicit printfs in some sane way.
#define pclMSpf(format, ...) pclMSpf_dbg(1, format, __VA_ARGS__)
#define pclMSpf2(format, ...) pclMSpf_dbg(2, format, __VA_ARGS__)
void pclMSpf_dbg(int level, FORMAT_STR const char* format, ...);

// Profiler name mutex.
#define EnterStaticCmdPerfMutex()					\
do {												\
	extern CRITICAL_SECTION StaticCmdPerfMutex;		\
	EnterCriticalSection(&StaticCmdPerfMutex);		\
} while(0)
#define LeaveStaticCmdPerfMutex()					\
do {												\
	extern CRITICAL_SECTION StaticCmdPerfMutex;		\
	LeaveCriticalSection(&StaticCmdPerfMutex);		\
} while(0)

// Error reporting

#define REPORT_ERROR_STRINGS report_error_strings_function

#define REPORT_ERROR_STRING(client, error_code, err_str)\
	REPORT_ERROR_STRINGS(client, error_code, "The patchclientlib is internally recording error code ", err_str)
#define REPORT_ERROR(client, error_code) REPORT_ERROR_STRING(client, error_code, NULL)

#define RETURN_ERROR_INTERNAL(client, error_code, EXTRA)																\
	do {																												\
	PCL_ErrorCode RETURN_ERROR_ERROR = (error_code);																\
	REPORT_ERROR_STRINGS((client), RETURN_ERROR_ERROR, "The patchclientlib is returning error code ", NULL);		\
	EXTRA																											\
	return RETURN_ERROR_ERROR;																						\
	} while (0)

#define RETURN_ERROR(client, error_code) RETURN_ERROR_INTERNAL((client), (error_code), ;)

#define RETURN_ERROR_PERFINFO_STOP(client, error_code) RETURN_ERROR_INTERNAL((client), (error_code), PERFINFO_AUTO_STOP();)

void report_error_strings_function(PCL_Client *patch_client, PCL_ErrorCode error_code, const char *intro_str, const char *additional_str);

// Used by forceInCheckFile()
typedef struct PatchFilescanData
{
	// for the entire scan
	PCL_Client *client;
	int file_count;
	char **hide;
	StashTable stash;
	bool ignore_checksum; // FIXME: this used to be a global, set by an AUTO_COMMAND
	bool force_delete;

	// Add to the diff list, even if it wouldn't be otherwise:
	bool forceIfNotLockedByClient;	// ... if the file is not locked (eg, force in files)
	bool forceIfLockedByClient;		// ... if the file is locked (eg, undo checkout)

	// for the current directory
	char *dir_name;
	char *counts_as;
	char *match_str;
	bool recursive;

	// for the current file
	int file;

	// for db scanning
	int str_count;
	char **on_disk_strings;
	char **counts_as_strings;
	char **wildcard_strings;

	// output
	char ***disk_names;
	char ***db_names;
	PCL_DiffType **diff_types;
	char ***undo_names;
} PatchFilescanData;

bool isLockedByClient(PCL_Client *client, DirEntry *dir);

void searchDbForDeletionsCB(DirEntry *dir, PatchFilescanData *userdata);
