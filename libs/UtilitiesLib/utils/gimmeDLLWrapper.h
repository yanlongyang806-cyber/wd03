#pragma once
GCC_SYSTEM

#ifdef GIMMEDLL_EXPORTS
Error!  Including gimmeDLLWrapper in GimmeDLL!
#endif

#include "gimmeDLLPublicInterface.h"

typedef struct StashTableImp *StashTable;

typedef enum GimmeFileStatus
{
	GIMME_STATUS_UNKNOWN, // We don't know, gimme may be disabled
	GIMME_STATUS_CHECKED_OUT_BY_ME, // I have this checked out
	GIMME_STATUS_NOT_CHECKED_OUT_BY_ME, // This file has not been checked out by me
	GIMME_STATUS_NEW_FILE, // File is yet to be added to the database
} GimmeFileStatus;

#if _PS3

__forceinline static GimmeErrorValue gimmeDLLDoOperation(const char *relpath, GIMMEOperation op, GimmeQuietBits quiet) { return 0; }
__forceinline static GimmeErrorValue gimmeDLLDoOperations(const char **relpaths, GIMMEOperation op, GimmeQuietBits quiet) { return 0; }

__forceinline static GimmeErrorValue gimmeDLLSetDefaultCheckinComment(const char *comment) { return 0; }

__forceinline static const char *gimmeDLLQueryIsFileLocked(const char *relpath) { return 0; } // Returns username of who has a file checked out, may be the current user
__forceinline static int			gimmeDLLQueryIsFileLockedByMeOrNew(const char *relpath) { return 0; }
__forceinline static int			gimmeDLLQueryIsFileMine(const char *relpath) { return 1; }

__forceinline static const char *gimmeDLLQueryLastAuthor(const char *relpath) { return "UNKNOWN"; }
__forceinline static int			gimmeDLLQueryIsFileLatest(const char *relpath) { return 0; }
__forceinline static const char *gimmeDLLQueryUserName(void) { return 0; }
__forceinline static int			gimmeDLLQueryAvailable(void) { return 0; } // Whether or not source control is available
__forceinline static int			gimmeDLLQueryExists(void) { return 0; } // Whether or not the DLL is there (regardless of whether it can get to revision control or not, etc)

__forceinline static const char *gimmeDLLQueryBranchName(const char *localpath) { return 0; }
__forceinline static int			gimmeDLLQueryBranchNumber(const char *localpath) { return 0; } // Use BranchName instead!
__forceinline static int			gimmeDLLQueryCoreBranchNumForDir(const char *localpath) { return 0; } // 

__forceinline static const char *gimmeDLLGetErrorString(GimmeErrorValue error) { return 0; }

__forceinline static GimmeErrorValue gimmeDLLDoCommand(const char *cmdline) { return 0; }

__forceinline static void		gimmeDLLDisable(int disable) {}
__forceinline static void		gimmeDLLForceManifest(bool force) {}
__forceinline static bool		gimmeDLLForceDirtyBit(const char *fullpath) {}

__forceinline static GimmeErrorValue gimmeDLLBlockFile(const char *relpath, const char *block_string) { return 0; }
__forceinline static GimmeErrorValue gimmeDLLUnblockFile(const char *relpath) { return 0; }
__forceinline static int			gimmeDLLQueryIsFileBlocked(const char *relpath) { return 0; }

__forceinline static const char *const *gimmeDLLQueryGroupList(void) { return 0; } // Returns the list of groups we're a member of
__forceinline static const char *const *gimmeDLLQueryFullGroupList(void) { return 0; } // Returns the full list of groups that any member is in
__forceinline static const char *const *gimmeDLLQueryFullUserList(void) { return 0; } // Returns the full list of users

__forceinline static GimmeFileStatus gimmeDLLQueryFileStatus(const char *relpath) { return 0; } // Wrapper around LastAuthor that returns an enum

#else

GimmeErrorValue gimmeDLLDoOperation(const char *relpath, GIMMEOperation op, GimmeQuietBits quiet);
GimmeErrorValue gimmeDLLDoOperations(const char **relpaths, GIMMEOperation op, GimmeQuietBits quiet);
GimmeErrorValue gimmeDLLDoOperationsDir(const char *dir, GIMMEOperation op, GimmeQuietBits quiet);
GimmeErrorValue gimmeDLLDoOperationsDirs(const char **dirs, GIMMEOperation op, GimmeQuietBits quiet);

// Set the default checkin comment, when doing a GIMME_CHECKIN with GIMME_QUIET.
GimmeErrorValue gimmeDLLSetDefaultCheckinComment(const char *comment);

const char *gimmeDLLQueryIsFileLocked(const char *relpath); // Returns username of who has a file checked out, may be the current user
int			gimmeDLLQueryIsFileLockedByMeOrNew(const char *relpath);
int			gimmeDLLQueryIsFileMine(const char *relpath);

const char *gimmeDLLQueryLastAuthor(const char *relpath);
int			gimmeDLLQueryIsFileLatest(const char *relpath);
const char *gimmeDLLQueryUserName(void);
int			gimmeDLLQueryAvailable(void); // Whether or not source control is available
int			gimmeDLLQueryExists(void); // Whether or not the DLL is there (regardless of whether it can get to revision control or not, etc)

const char *gimmeDLLQueryBranchName(const char *localpath);
int			gimmeDLLQueryBranchNumber(const char *localpath); // Use BranchName instead!
int			gimmeDLLQueryCoreBranchNumForDir(const char *localpath); // 

const char *gimmeDLLGetErrorString(GimmeErrorValue error);

GimmeErrorValue gimmeDLLDoCommand(const char *cmdline);

void		gimmeDLLDisable(int disable);
bool		gimmeDLLForceManifest(bool force);
bool		gimmeDLLForceDirtyBit(const char *fullpath);

bool		gimmeDLLCreateConsoleHidden(bool hidden);

GimmeErrorValue gimmeDLLBlockFile(const char *relpath, const char *block_string);
GimmeErrorValue gimmeDLLUnblockFile(const char *relpath);
int			gimmeDLLQueryIsFileBlocked(const char *relpath);

const char *const *gimmeDLLQueryGroupListForUser(const char *username); // Returns the list of groups the specified user is a member of
const char *const *gimmeDLLQueryGroupList(void); // Returns the list of groups that the current user is a member of
const char *const *gimmeDLLQueryFullGroupList(void); // Returns the full list of groups that any member is in
const char *const *gimmeDLLQueryFullUserList(void); // Returns the full list of users

GimmeFileStatus gimmeDLLQueryFileStatus(const char *relpath); // Wrapper around LastAuthor that returns an enum

#endif

// Manual parser for the client manifest file.
// NOTE: Giant flaming hack for WorldLib, eventually needs to be pulled into contentlib most likely. <NPK 2008-09-30>
StashTable gimmeDLLCacheManifestBinFiles(const char *example_file);
bool gimmeDLLCheckBinFileMatchesManifest(StashTable cache, const char *file, U32 *out_timestamp);
