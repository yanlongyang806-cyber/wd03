#pragma once
GCC_SYSTEM

// Public interface to the Gimme DLL

// You CAN NOT re-order or remove any enums in this file, that would break talking
//  with older/newer versions of the DLL

AUTO_ENUM;
typedef enum GIMMEOperation {
	GIMME_CHECKOUT=0,     //    checkout
	GIMME_CHECKIN=1,      //    checkin
	GIMME_FORCECHECKIN=2, //    forcefully checkin even if someone else has it checked out
	GIMME_DELETE=3,       //    mark a file as deleted
	GIMME_GLV=4,          //    just get the latest version (don't checkout)
	GIMME_UNDO_CHECKOUT=5,//    undo checkout
	GIMME_CHECKIN_LEAVE_CHECKEDOUT=6, // Check in, but leave checked out
	GIMME_LABEL=7,		  //	 A labeling operation (used internally only)
	GIMME_ACTUALLY_DELETE=8,//	when committing a deleted file, this one actually does the deleting
} GIMMEOperation;

typedef enum GimmeQuietBits {
	GIMME_QUIET = 1<<0,
	GIMME_QUIET_NOUNDOCHECKOUT = 1<<1, // Hides "no changes detected, just undoing checkout" message
	GIMME_QUIET_LARGE_CHECKOUT = 1<<2, // Hides "checking out too many files"
} GimmeQuietBits;

AUTO_ENUM;
typedef enum GimmeErrorValue {
	GIMME_NO_ERROR=0,
	GIMME_ERROR_COPY=1,
	GIMME_ERROR_RULESFILE=2,
	GIMME_ERROR_LOCKFILE_CREATE=3,
	GIMME_ERROR_LOCKFILE_REMOVE=4,
	GIMME_ERROR_NODIR=5,
	GIMME_ERROR_LOCKFILE=6,
	GIMME_ERROR_NOTLOCKEDBYYOU=7, // When checking in, it's not yours
	GIMME_ERROR_CANNOT_DELETE_LOCAL=8,
	GIMME_ERROR_ALREADY_CHECKEDOUT=9, // Checked out by someone else, checkout failed
	GIMME_ERROR_NOT_IN_DB=10,
	GIMME_ERROR_FILENOTFOUND=11,
	GIMME_ERROR_NO_SC=12,
	GIMME_ERROR_ALREADY_DELETED=13,
	GIMME_ERROR_DB=14,
	GIMME_ERROR_FUTURE_FILE=15,
	GIMME_ERROR_CANCELED=16,
	GIMME_ERROR_NO_DLL=17,
	GIMME_ERROR_COMMANDLINE=18,
	GIMME_ERROR_UNKNOWN=19,
	GIMME_ERROR_BLOCKED=20, // If file is blocked in registry, checkin fails
} GimmeErrorValue;

#define isGimmeErrorFatal(error) \
	((error==GIMME_NO_ERROR || error == GIMME_ERROR_NOT_IN_DB || error == GIMME_ERROR_FILENOTFOUND)?0:1)	

// Function prototypes
typedef GimmeErrorValue (*tpfnGimmeDoOperation)(const char *fullpath, GIMMEOperation op, GimmeQuietBits quiet);
typedef GimmeErrorValue (*tpfnGimmeDoOperations)(const char **fullpaths, int file_count, GIMMEOperation op, GimmeQuietBits quiet);

typedef GimmeErrorValue (*tpfnGimmeSetDefaultCheckinComment)(const char *comment);

typedef const char *(*tpfnGimmeQueryIsFileLocked)(const char *fullpath); // Returns username of who has a file checked out, may be the current user
typedef int			(*tpfnGimmeQueryIsFileLockedByMeOrNew)(const char *fullpath);
typedef int			(*tpfnGimmeQueryIsFileMine)(const char *fullpath);

typedef const char *(*tpfnGimmeQueryLastAuthor)(const char *fullpath);
typedef int			(*tpfnGimmeQueryIsFileLatest)(const char *fullpath);
typedef const char *(*tpfnGimmeQueryUserName)(void);
typedef int			(*tpfnGimmeQueryAvailable)(void); // Whether or not source control is available

typedef const char *(*tpfnGimmeQueryBranchName)(const char *localpath);
typedef int			(*tpfnGimmeQueryBranchNumber)(const char *localpath); // Use BranchName instead!

#define GIMME_BRANCH_UNKNOWN -2
typedef int			(*tpfnGimmeQueryCoreBranchNumForDir)(const char *localpath);

typedef const char *(*tpfnGimmeGetErrorString)(GimmeErrorValue error);

typedef GimmeErrorValue (*tpfnGimmeDoCommand)(const char *cmdline);

typedef GimmeErrorValue (*tpfnGimmeBlockFile)(const char *fullpath, const char *block_string);
typedef GimmeErrorValue (*tpfnGimmeUnblockFile)(const char *fullpath);
typedef int			(*tpfnGimmeQueryIsFileBlocked)(const char *fullpath);

typedef const char * const *(*tpfnGimmeQueryGroupListForUser)(const char *);
typedef const char * const *(*tpfnGimmeQueryGroupList)(void);
typedef const char * const *(*tpfnGimmeQueryFullGroupList)(void);
typedef const char * const *(*tpfnGimmeQueryFullUserList)(void);

typedef int (*VprintfFunc)(const char *format, va_list argptr);
typedef void (*tpfnGimmeSetVprintfFunc)(VprintfFunc func);
typedef void (*tpfnGimmeSetCrashStateFunc)(crashStateFunc func);

typedef void* (*CRTMallocFunc)(size_t size, int blockType, const char *filename, int linenumber);
typedef void* (*CRTCallocFunc)(size_t num, size_t size, int blockType, const char *filename, int linenumber);
typedef void* (*CRTReallocFunc)(void *data, size_t newSize, int blockType, const char *filename, int linenumber);
typedef void (*CRTFreeFunc)(void *data, int blockType);
typedef void (*tpfnGimmeSetMemoryAllocators)(CRTMallocFunc m, CRTCallocFunc c, CRTReallocFunc r, CRTFreeFunc f);

typedef struct AutoTimerData AutoTimerData;
typedef void (*tpfnGimmeSetAutoTimer)(const AutoTimerData *data);

typedef void (*tpfnGimmeForceManifest)(bool force);
typedef void (*tpfnGimmeCreateConsoleHidden)(bool force);

typedef bool (*tpfnGimmeForceDirtyBit)(const char *fullpath);