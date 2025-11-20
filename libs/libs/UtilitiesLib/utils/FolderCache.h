/***************************************************************************



*
* Module Description:
*   System to keep track of a directory in memory that is scanned from
*   both a set of Pig files and from a set of game data dirs. Relies on
*   dirMonitor for real-time updating (WinNT only)
* 
***************************************************************************/

#ifndef _FOLDERMONITOR_H
#define _FOLDERMONITOR_H
#pragma once
GCC_SYSTEM

#include <time.h>
#include "piglib.h"
#include "windefinclude.h"

typedef struct StashTableImp *StashTable;
typedef struct SharedMemoryHandle SharedMemoryHandle;
typedef struct FileSpecTree FileSpecTree;

typedef struct FolderNode	FolderNode;
typedef struct FolderCache	FolderCache;

typedef struct FolderNode {
	// next and prev must be first because our standard link lists are used.
	FolderNode*	next;
	FolderNode*	prev;
	
	S8			virtual_location;		// <0 -> Piggs, >=0 -> a game data dir // larger ABS is higher virtual_location
	S8			seen_in_fs_virtual_location; // Remembers what virtual_location this was seen in

    U16			is_dir:1;				// Is a folder/directory.
	U16			writeable:1;			// Is writeable (i.e. not read-only).
	U16			hidden:1;				// Is hidden.
	U16			system:1;				// System flag is set.

    U16			needs_patching:1;		// Used by the patch client lib for dynamic patching
	U16			started_patching:1;		// Used by the patch client lib for dynamic patching
	U16			seen_in_fs:1;			// Used for development_dynamic to add all pigs and verify they're in the FS later
	U16			in_hash_table:1;		// Set when the node is added to htAllFiles.
	U16			delete_if_absent:1;		// Set when scanning for nonexistent nodes.
	U16			non_mirrored_file:1;	// This file exists only in hoggs, used for catching some edge cases

    S32 		file_index; 			// index to the file in the pigfile specified by the virtual_location
	FolderNode*	contents;				// NULL if !is_dir;  linked list of all of the children nodes
	FolderNode*	contents_tail;			// NULL if !is_dir;  tail end of the linked list of children
	FolderNode*	parent;					// NULL if root
	char*		name;					// name, relative to parent node
	size_t		size;
	__time32_t	timestamp;
} FolderNode;

#define MAX_DATA_DIRS_OR_PIGS 100
#define VIRTUAL_LOCATION_TO_LOOKUP_INDEX(x) (x + MAX_DATA_DIRS_OR_PIGS)
#define CORE_LOOKUP_SIZE (MAX_DATA_DIRS_OR_PIGS*2)


typedef void (*FolderCacheNodeCreateDeleteCallback)(FolderCache* fc, FolderNode* node);

typedef bool (*FolderCachePatchCallback)(FolderCache * fc, FolderNode* node, const char *relpath, PigFileDescriptor *pfd, bool header_only);

typedef struct FolderCache {
	FolderNode *root;
	FolderNode *root_tail;
	CRITICAL_SECTION critSecAllFiles; // Critical section around hashtable calls
	StashTable htAllFiles; // Hash table of pointers to FolderNodes
	char **gamedatadirs; // Map of "priorities" to the game data dir that is related
	char **gamedatadir_names; // Logical names for game data dirs
	char **piggs; // Map of "priorities" to the pig file that is related
	char **pigg_names; // Logical names for game data dirs

	bool is_core[CORE_LOOKUP_SIZE];
	FileSpecTree *core_exclusions;
	
	FolderCachePatchCallback patchCallback; // What to call when a node has needs_patching set.

	FolderCacheNodeCreateDeleteCallback nodeCreateCallback;
	FolderCacheNodeCreateDeleteCallback nodeDeleteCallback;
	
	S32	needsRescan;

	S32 scanCreatedFolders;
} FolderCache;

#define PIG_INDEX_TO_VIRTUAL_LOCATION(pi)  (-(pi+1))
#define VIRTUAL_LOCATION_TO_PIG_INDEX(vl)  ((-vl)-1)

#define FOLDER_CACHE_PAUSE_CALLBACKS(i)(g_bPausedCallback=i);

extern bool g_bPausedCallback;

// Maps "priorities" to the game data dir/pig file that is related
char *FolderCache_GetFromVirtualLocation(FolderCache *fc, int virtual_location);
void FolderCache_SetFromVirtualLocation(FolderCache *fc, int virtual_location, char *path, char *path_name, bool is_core);
int FolderCache_GetNumGDDs(FolderCache *fc);
int FolderCache_GetNumPiggs(FolderCache *fc);
void FolderCache_PrintGDDs(FolderCache *fc);

typedef enum {
	FOLDER_CACHE_MODE_DEVELOPMENT, // Filesystem is "master", only load from a Pig if the file exists in the file system and it's the same timestamp
	FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC, // Same as above, but don't initially scan the tree, only do so as needed...  this is sorta a hack.  For folders that are not scanned with fileScanAllDataDirs, this won't do anything better than FILESYSTEM_ONLY
	FOLDER_CACHE_MODE_I_LIKE_PIGS, // Equal rights for pigs.  Use files from the pigs even if they don't exist in the File System (hybride mode)
	FOLDER_CACHE_MODE_PIGS_ONLY, // Pigs are master, do not load from file system in any circumstances (in fact, don't even scan the file system)
	FOLDER_CACHE_MODE_FILESYSTEM_ONLY, // Do not cache the tree, do not load pigs
} FolderCacheMode;

typedef enum {
	FOLDER_CACHE_ALL_FOLDERS,
	FOLDER_CACHE_EXCLUDE_FOLDER,
	FOLDER_CACHE_ONLY_FOLDER,
} FolderCacheExclusion;

FolderCache *FolderCacheCreate(void);
void FolderCacheDestroy(FolderCache *fc);

void FolderCacheHashTableAdd(FolderCache* fc, const char* name, FolderNode* node);
FolderNode* FolderCacheHashTableRemove(FolderCache* fc, const char* name);

void FolderCacheSetMode(FolderCacheMode mode);
FolderCacheMode FolderCacheGetMode(void);
void FolderCacheChooseMode(void); // Auto-chooses the appropriate mode, based on existence of ./piggs
void FolderCacheChooseModeNoPigsInDevelopment(void); // For apps not loading data, don't load the piggs

void FolderCacheEnableCallbacks(int enable); // Disables/enables checking for changes in the filesystem (turn these off when loading)
bool FolderCacheUpdatesLikelyWorking(void);
void FolderCacheDoNotWarnOnOverruns(bool bDoNotWarn);

void FolderCacheExclude(FolderCacheExclusion mode, const char *folder);

void FolderCacheSetFullScanCreatedFolders(FolderCache* fc, int enabled);

enum {
	FOLDER_CACHE_CALLBACK_UPDATE	= (1<<0), // Called on add or update
	FOLDER_CACHE_CALLBACK_DELETE	= (1<<1), // Called on delete
	FOLDER_CACHE_CALLBACK_ATTRIB_CHANGE = (1<<2), // Read-only or other attribute change

	FOLDER_CACHE_CALLBACK_ALL		= (1 << 3)-1, // You probably don't want this one, just _UPDATE or _UPDATE|_DELETE
};
typedef void (*FolderCacheCallback)(const char *relpath, int when);
typedef void (*FolderCacheCallbackEx)(FolderCache* fc, FolderNode* node, int virtual_location, const char *relpath, int when, void *userData);
// Sets it up so that a callback will be called when a file with a given filespec
// matches  (uses simpleMatch, i.e. only 1 '*' allowed)
void FolderCacheSetCallback(int when, const char *filespec, FolderCacheCallback callback);
void FolderCacheSetCallbackEx(int when, const char *filespec, FolderCacheCallbackEx callback, void *userData);
void printFilenameCallback(const char *relpath, int when); // example/test callback

//returns 1 if it did something, 0 otherwise (can thus be used to check if reloading is currently going on)
int FolderCacheDoCallbacks(void);
void FolderCacheSetManualCallbackMode(int on); // Call this if you're calling FolderCacheDoCallbacks each tick


// Add a folder from the filesystem into the virtual tree
void FolderCacheAddFolder(FolderCache *fc, const char *basepath, int virtual_location, const char *pathname, bool is_core);
// Add a Pig's files into the virtual tree.  This must be called *AFTER* all calls to 
//   FolderCacheAddFolder, because Pigged files can only be added if they already exist
//   in the file system (unless we're in production mode, then Pig files are used
//   exclusively).  Returns non-zero upon error
int FolderCacheAddAllPigs(FolderCache *fc);

// Dynamic hack functions:
void FolderCacheRequestTree(FolderCache *fc, const char *relpath); // If we're in dynamic mode, this will load the tree!
void FolderCacheModePush(FolderCacheMode newmode); // Another hack for dynamic mode
void FolderCacheModePop(void); // Another hack for dynamic mode

int FolderCacheSetDirMonTimeout(int timeout);
void FolderCacheSetNoDisableOnBufferOverruns(int value);

FolderNode *FolderCacheQueryEx(FolderCache *fc, const char *relpath, bool do_hog_reload, bool grab_node_critical_section);
#define FolderCacheQuery(fc, relpath) FolderCacheQueryEx(fc, relpath, true, true)
char *FolderCacheGetRealPath(FolderCache *fc, FolderNode *node, char *buffer, size_t buffer_size); // pass a buffer to fill
char *FolderCacheGetOnDiskPath(FolderCache *fc, FolderNode *node, char *buffer, size_t buffer_size); // Returns the on-disk path if this file has been seen on disk

void FolderCacheFlushHogChangeCallbacks(void);
void FolderCacheForceUpdate(FolderCache *fc, const char *relpath); // Call after closing a file being written to

void FolderCacheDisallowOverrides(void); // Disallows any override files

FolderNode *FolderNodeCreate(void);
void FolderNodeDestroy(FolderCache* fc, FolderNode *node);
FolderNode *FolderNodeAdd(FolderNode **head, FolderNode **tail, FolderNode *parent, const char *relpath, __time32_t timestamp, size_t size, int virtual_location, int file_index, int* createdOut, int depth,const char *pre_alloced_path); // returns the newly added node // orignode is a sibling that the new node should be attached to, parent is what the parent field should be set to (the parent is not modified, unless, of course, &parent->contents is passed in as orignode)
char *FolderNodeGetFullPath_s(FolderNode *node, char *buf, size_t buf_size); // Includes prefixed slash, pass a buffer to fill
#define FolderNodeGetFullPath(node, buf) FolderNodeGetFullPath_s(node, buf, ARRAY_SIZE_CHECKED(buf))
FolderNode *FolderNodeFind(FolderNode *base, __in const char *relpath);
void FolderNodeDeleteFromTree(FolderCache* fc, FolderNode *node);
void FolderNodeDeleteFromTreeEx(FolderCache* fc, FolderNode *node, int deleteParent);
int FolderNodePigIndex(FolderNode *node);
void FolderCacheBuildHashTable(FolderCache *fc, FolderNode *node, char *pathsofar);
void FolderCachePruneCoreExclusions(FolderCache *fc, SA_PARAM_OP_STR const char *recently_scanned_folder);
void FolderCacheAllowCoreExclusions(bool bAllow);

#define FolderNodeEnterCriticalSection() FolderNodeEnterCriticalSection_dbg(__FILE__, __LINE__)
void FolderNodeEnterCriticalSection_dbg(const char *filename, int linenum);
#define FolderNodeTryEnterCriticalSection() FolderNodeTryEnterCriticalSection_dbg(__FILE__, __LINE__)
bool FolderNodeTryEnterCriticalSection_dbg(const char *filename, int linenum);
void FolderNodeLeaveCriticalSection(void);

// Return 0 to prune a tree, 1 to keep parsing, and 2 for a complete stop
typedef int (*FolderNodeOp)(FolderNode *node);
int FolderNodeRecurse(FolderNode *node, FolderNodeOp op);

// Return 0 to prune a tree, 1 to keep parsing, and 2 for a complete stop
typedef FileScanAction (*FolderNodeOpEx)(const char *dir, FolderNode *node, void *pUserData_Proc, void *pUserData_Data);
int FolderNodeRecurseEx(FolderNode *node, FolderNodeOpEx op, void *pUserData_Proc, void *pUserData_Data, const char *base, bool bAlreadyHaveCritical, bool bDoNotReleaseCritical); // Pass NULL to base if we're operating on the root, or you don't care about getting the path up to the current node

void FolderCacheReleaseHogHeaderData(void);

// Debug/performance info
void FolderNodeGetCounts(int *discard, int *update);
int FolderNodeVerifyIntegrity(FolderNode *node, FolderNode *parent);

// Ignores both folders and hoggs starting with this name
// Expects a static string or allocAddString'd string.
void FolderCacheAddIgnore(const char *ignore_folder);
void FolderCacheAddIgnores(const char **ignore_list, int count);
void FolderCacheAddIgnoreExt(const char *ignore_ext);
bool FolderCacheIsIgnored(const char *path);

const char* storeHashKeyStringLocal(const char* pcStringToCopy);

void FolderCacheStartMonitorThread(void);

size_t FolderCacheGetMemoryUsage(FolderCache *cache);
size_t FolderNodeGetMemoryUsage(FolderNode *node);
void FolderCacheMoveToShared(FolderCache *cache, SharedMemoryHandle *sm_handle);
FolderNode *FolderNodeMoveToShared(FolderNode *node, SharedMemoryHandle *sm_handle);
void FolderNodeDestroyPool(void);

//pretends that a file has been touched so that any relevant reloading will happen. Note: written by
//someone who has no particular knowledge of how this system works, but seems to do the job properly
void FolderCacheForceUpdateCallbacksForFile(const char *pFileName);

extern FolderCache *folder_cache;

//set to true when FolderCacheChooseMode() has run. Generally means "it's OK to do file stuff"
extern bool gbFolderCacheModeChosen;
// Set this to true to cause an assert if any code tries to access the foldercache/game filesystem
extern bool g_do_not_try_to_load_folder_cache;

#endif
