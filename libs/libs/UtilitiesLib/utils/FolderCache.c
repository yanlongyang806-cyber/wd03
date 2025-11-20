/***************************************************************************



* 
***************************************************************************/

#include "FolderCache.h"
#include <string.h>

#include "genericlist.h"
#include "DirMonitor.h"
#include "fileutil.h"
#include "strings_opt.h"
#include "winfiletime.h"
#include "EArray.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "StashTable.h"
#include "StringCache.h"
#include "fileWatch.h"
#include "ScratchStack.h"
#include "fileutil2.h"
#include "HashFunctions.h"
#include "sysutil.h"
#include "hoglib.h"
#include "ThreadManager.h"
#include "XboxThreads.h"
#include "FilespecMap.h"
#include "utilitiesLib.h"
#include "GlobalTypes.h"
#include "BlockEarray.h"
#include "TimedCAllback.h"
#include "utils.h"
#include "utf8.h"



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:FolderCacheMonitorThread", BUDGET_FileSystem););

static void FolderCacheAddHog(FolderCache *fc, HogFile *hog_file, int virtual_location, int pig_set_index);

bool g_bPausedCallback = false;

bool gbFolderCacheModeChosen = false;
bool g_do_not_try_to_load_folder_cache=false;

typedef struct FolderCacheCallbackInfo{
	struct FolderCacheCallbackInfo *next;
	struct FolderCacheCallbackInfo *prev;
	union{
		FolderCacheCallback callback;
		FolderCacheCallbackEx callbackEx;
	};
	FolderCache* fc;
	FolderNode* node;
	int virtual_location;
	int when;
	int isEx;
	char filespec[CRYPTIC_MAX_PATH];
	void *userData;
} FolderCacheCallbackInfo;

typedef struct PigChangeCallbackMapping {
	FolderCache *fc;
	int virtual_location;
} PigChangeCallbackMapping;

static void fcCallbackUpdated(PigChangeCallbackMapping *mapping, const char* path, U32 filesize, U32 timestamp, HogFileIndex file_index);

int folder_cache_debug=0;
int folder_cache_update_enable=1;
int folder_cache_manual_callback_check=0;

static int folder_cache_dirMonTimeout = 0;
static int folder_cache_noDisableOnBufferOverruns = 0;

static FolderCacheExclusion folder_cache_exclusion = FOLDER_CACHE_ALL_FOLDERS;
static char *folder_cache_exclusion_folder=NULL;

static const char **folder_cache_ignores=NULL;
static const char **folder_cache_ignore_exts=NULL;

void FolderCacheAddIgnore(const char *ignore_folder)
{
	assert(!PigSetInited());
	eaPush(&folder_cache_ignores, ignore_folder);
}

void FolderCacheAddIgnores(const char **ignore_list, int count)
{
	int i;

	assert(!PigSetInited());
	for(i=0; i<count; ++i) {
		eaPush(&folder_cache_ignores, ignore_list[i]);
	}
}

void FolderCacheAddIgnoreExt(const char *ignore_ext)
{
	assert(!PigSetInited());
	eaPush(&folder_cache_ignore_exts, ignore_ext);
}


__forceinline static bool FolderCacheIsIgnoredAbsolute(const char *path)
{
	const char *s;
	if (!eaSize(&folder_cache_ignores) && !eaSize(&folder_cache_ignore_exts))
		return false;
	s = strrchr(path, '/');
	if (s)
		path = s+1;
	s = strrchr(path, '\\');
	if (s)
		path = s+1;
	if (strStartsWithAny(path, folder_cache_ignores))
		return true;
	s = strrchr(path, '.');
	if (s)
	{
		FOR_EACH_IN_EARRAY(folder_cache_ignore_exts, const char, ext)
		{
			if (stricmp(s, ext)==0)
				return true;
		}
		FOR_EACH_END;
	}
	return false;
}

bool FolderCacheIsIgnored(const char *path)
{
	const char *s;
	if (!eaSize(&folder_cache_ignores) && !eaSize(&folder_cache_ignore_exts))
		return false;
	if (strStartsWithAny(path, folder_cache_ignores))
		return true;
	s = strrchr(path, '.');
	if (s)
	{
		FOR_EACH_IN_EARRAY(folder_cache_ignore_exts, const char, ext)
		{
			if (stricmp(s, ext)==0)
				return true;
		}
		FOR_EACH_END;
	}
	return false;
}



const char* storeHashKeyStringLocal(const char* pcStringToCopy)
{
	return allocAddString(pcStringToCopy);
}


void FolderCacheExclude(FolderCacheExclusion mode, const char *folder) {
	folder_cache_exclusion = mode;
	SAFE_FREE(folder_cache_exclusion_folder);
	switch(mode) {
	xcase FOLDER_CACHE_ALL_FOLDERS:
		assert(folder==NULL);
	xcase FOLDER_CACHE_ONLY_FOLDER:
	case FOLDER_CACHE_EXCLUDE_FOLDER:
		folder_cache_exclusion_folder = strdup(folder);
	xdefault:
		assert(false);
	}
}

void FolderCacheSetFullScanCreatedFolders(FolderCache* fc, int enabled){
	fc->scanCreatedFolders = enabled ? 1 : 0;
}


static DWORD threadid=0;

FolderCache *FolderCacheCreate() {
	FolderCache *fc = calloc(1,sizeof(FolderCache));

	if (!threadid) {
		threadid = GetCurrentThreadId();
	}

	InitializeCriticalSection(&fc->critSecAllFiles);

	return fc;
}

void FolderCacheDestroy(FolderCache *fc)
{
	if(!fc){
		return;
	}
	
	FolderNodeEnterCriticalSection();
	EnterCriticalSection(&fc->critSecAllFiles);

	while(FolderCache_GetNumGDDs(fc)){
		dirMonRemoveDirectory(NULL, fc->gamedatadirs[0]);
		free(fc->gamedatadirs[0]);
		eaRemove(&fc->gamedatadirs, 0);
	}

	FolderNodeDestroy(fc, fc->root);
	fc->root = fc->root_tail = NULL;

	stashTableDestroy(fc->htAllFiles);
	fc->htAllFiles=0;

	eaDestroyEx(&fc->piggs, NULL);
	eaDestroyEx(&fc->gamedatadirs, NULL);

	LeaveCriticalSection(&fc->critSecAllFiles);
	DeleteCriticalSection(&fc->critSecAllFiles);
	FolderNodeLeaveCriticalSection();
	
	free(fc);
}

void FolderCacheHashTableAdd(FolderCache* fc, const char* name, FolderNode* node){
	if(fc->htAllFiles){
		stashAddPointer(fc->htAllFiles, name, node, true);
		if(!node->in_hash_table) node->in_hash_table = 1;

		#if 0
		// Check parents.

		{
			FolderNode* cur;
			for(cur = node; cur; cur = cur->parent){
				assert(cur->in_hash_table);
			}
		}
		#endif
	}
}

FolderNode* FolderCacheHashTableRemove(FolderCache* fc, const char* name){
	FolderNode* node = NULL;
	
	if(fc->htAllFiles){
		stashRemovePointer(fc->htAllFiles, name, &node);
		
		if(node){
			node->in_hash_table = 0;
		}
	}
	
	return node;
}

void folderCacheVerifyIntegrity(FolderCache *fc) {
	FolderNode *walk;
	if (fc->root) {
		assert(fc->root_tail!=NULL);
	} else {
		assert(fc->root_tail==NULL);
	}
	walk = fc->root;
	while (walk) {
		FolderNodeVerifyIntegrity(walk, NULL);
		walk = walk->next;
	}
}

static int count_file=0;
static int count_folder=0;

static char *banned_overrides[] = {
	// These are not allowed to be overridden
//	"/object_library",
//	"/bin",
	"/TODO" // Update later for ignored paths
};
static int banned_overrides_len[ARRAY_SIZE(banned_overrides)];
static bool ban_all_overrides = false;

void FolderCacheDisallowOverrides(void)
{
	ban_all_overrides = true;
}

static void FolderCacheAddFolderToNode(	FolderCache* fc,
										FolderNode **head,
										FolderNode **tail,
										FolderNode *parent,
										const char *basepath,
										int virtual_location,
										int depth)
{
	char						buffer[1024];
	FolderNode*					node2;
	int							i;
	int							strLength;
	WIN32_FIND_DATAA             wfd;
	U32							handle;

	if (!fileIsUsingDevData()) {
		static bool banned_overrides_inited=false;
		
		if (ban_all_overrides)
			return;
		if (!banned_overrides_inited) {
			for (i=0; i<ARRAY_SIZE(banned_overrides); i++) 
				banned_overrides_len[i] = (int)strlen(banned_overrides[i]);
			banned_overrides_inited=true;
		}

		strLength = (int)strlen(basepath);
		for (i=0; i<ARRAY_SIZE(banned_overrides); i++) {
			if(banned_overrides_len[i] > strLength)
				continue;
			if(stricmp(basepath + strLength - banned_overrides_len[i], banned_overrides[i]) == 0)
				return;
		}
	}

	if (FolderCacheIsIgnoredAbsolute(basepath))
		return;

	//PERFINFO_AUTO_START_FUNC();

	strcpy(buffer, basepath);
	strcat(buffer, "/*");

	if (!fwFindFirstFile(&handle, buffer, &wfd)) {
		// no files found (this is not a valid filespec/path)
		// do nothing
		//PERFINFO_AUTO_STOP();
		return;
	}
	do {
		const char* fileName = wfd.cFileName;
		U32			fileSize;
		__time32_t		utc_time;
		int			created;
		
		if (fileName[0] == '.' && (!fileName[1] || (fileName[1]=='.' && !fileName[2])) /*|| strEndsWith(fileName,".bak")*/)
			continue;

		if (FolderCacheIsIgnored(fileName))
			continue;

		fileSize = wfd.nFileSizeLow;
		
		_FileTimeToUnixTime(&wfd.ftLastWriteTime, &utc_time, FALSE);
		
		if(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			// folder
			switch (folder_cache_exclusion) {
				xcase FOLDER_CACHE_EXCLUDE_FOLDER:
					if (folder_cache_exclusion_folder && stricmp(fileName, folder_cache_exclusion_folder)==0) {
						continue; // skip it!
					}
				xcase FOLDER_CACHE_ONLY_FOLDER:
					if (parent==NULL && folder_cache_exclusion_folder && stricmp(fileName, folder_cache_exclusion_folder)!=0) {
						continue; // skip it if it doesn't match and this is in the root
					}
				xcase FOLDER_CACHE_ALL_FOLDERS:
					// Everything is good
				xdefault:
					assert(false);
			}
			// add the folder
			STR_COMBINE_SS(buffer, fileName, "/");
			node2 = FolderNodeAdd(head, tail, parent, buffer, utc_time, fileSize, virtual_location, -1, &created, depth, 0);
			node2->is_dir = true; // in case it has no contents
			node2->seen_in_fs = true;
			node2->seen_in_fs_virtual_location = virtual_location;
			assert(node2->parent == parent);
			// Callback if created.
			if(created && fc->nodeCreateCallback){
				fc->nodeCreateCallback(fc, node2);
			}
			count_folder++;
		} else {
			// normal file
			node2 = FolderNodeAdd(head, tail, parent, fileName, utc_time, fileSize, virtual_location, -1, &created, depth, 0);
			
			if(node2 && node2->seen_in_fs_virtual_location == virtual_location)
			{
				node2->writeable = !(wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
				node2->hidden = wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
				node2->system = wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ? 1 : 0;
			
				// Callback if created.
				if(created && fc->nodeCreateCallback){
					fc->nodeCreateCallback(fc, node2);
				}
			}
			
			count_file++;
		}

	} while (fwFindNextFile(handle, &wfd)!=0);
	
	fwFindClose(handle);

	for(node2 = *head; node2; node2 = node2->next){
		if(node2->is_dir){
			// Add the contents of the folder.
			STR_COMBINE_SSS(buffer, basepath, "/", node2->name);
			FolderCacheAddFolderToNode(fc, &node2->contents, &node2->contents_tail, node2, buffer, virtual_location, depth+1);
		}
	}

	//PERFINFO_AUTO_STOP();
}

static void FolderCachePruneNonExistent(FolderCache* fc,
										FolderNode **head,
										FolderNode **tail,
										FolderNode *parent
										)
{
	FolderNode *node2;
	FolderNode *next;

	for(node2 = *head; node2; node2 = next){
		next = node2->next;
		if(node2->is_dir){
			// Add the contents of the folder.
			FolderCachePruneNonExistent(fc, &node2->contents, &node2->contents_tail, node2);
			if (!node2->seen_in_fs && !node2->contents && !node2->needs_patching) {
				FolderNodeDeleteFromTreeEx(fc, node2, 0);
			}
		} else {
			if (!node2->seen_in_fs && !node2->needs_patching) {
				FolderNodeDeleteFromTreeEx(fc, node2, 0);
			}
		}
	}
}


static FileScanAction FolderCacheBuildHashTableOp(const char *dir, FolderNode *node, void *userdata_proc, void *userdata_data) {
	FolderCache *fc = (FolderCache*)userdata_data;
	StashElement elem;

	// Prevent calling allocAddFilename unless necessary.
	if ( !stashFindElement(fc->htAllFiles, dir, &elem) )
	{
		const char *pcPath = allocAddFilename(dir);
		FolderCacheHashTableAdd(fc, pcPath, node);
	}
	else
	{
		stashElementSetPointer(elem, node);
	}
	return FSA_EXPLORE_DIRECTORY;
}

void FolderCacheBuildHashTable(FolderCache *fc, FolderNode *node, char *pathsofar) {
	FolderNodeEnterCriticalSection();
	EnterCriticalSection(&fc->critSecAllFiles);
	if (!fc->htAllFiles)
	{
		if (IsServer() && GetAppGlobalType() != GLOBALTYPE_TESTCLIENT)
			fc->htAllFiles = stashTableCreateWithStringKeys(524288, StashDefault);
		else
			fc->htAllFiles = stashTableCreateWithStringKeys(8192, StashDefault);
	}
	FolderNodeRecurseEx(node, FolderCacheBuildHashTableOp, NULL, (void*)fc, pathsofar, true, true);
	LeaveCriticalSection(&fc->critSecAllFiles);
	FolderNodeLeaveCriticalSection();
}


typedef struct DirectoryUpdateContext {
	FolderCache *fc;
	int virtual_location;
} DirectoryUpdateContext;

void FolderCacheAddFolder(FolderCache *fc, const char *basepath, int virtual_location, const char *pathname, bool is_core)
{
	DirectoryUpdateContext *context=NULL;
	int timer;
	int update, discard;

	if (FolderCache_GetFromVirtualLocation(fc, virtual_location))
	{
		assert(!"Two folders with the same virtual_location!");
		return;
	}

	// Add to virtual_location list
	FolderCache_SetFromVirtualLocation(fc, virtual_location, strdup(basepath), pathname?strdup(pathname):NULL, is_core);

	// Add notification hook
	dirMonSetFlags(NULL, DIR_MON_NO_TIMESTAMPS);
	context = malloc(sizeof(*context));
	context->fc = fc;
	context->virtual_location = virtual_location;
	if (dirExists(basepath))
	{
		if (!dirMonAddDirectory(NULL, basepath, true, (void*)context)) {
#if !PLATFORM_CONSOLE
			if (!gbSurpressStartupMessages)
				printf(	"Error adding dirMon(%s)!  Dynamic reloading may not fully function.\n",
						basepath);
#endif
		}
	}
	
	if (FolderCacheGetMode()==FOLDER_CACHE_MODE_PIGS_ONLY
		|| FolderCacheGetMode()==FOLDER_CACHE_MODE_FILESYSTEM_ONLY)
	{
		return;
	}

	if (FolderCacheGetMode()!=FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC) {

		timer = timerAlloc();
		timerStart(timer);
		count_file=0;
		count_folder=0;

		// Scan HD
		
		FolderCacheAddFolderToNode(fc, &fc->root, &fc->root_tail, NULL, basepath, virtual_location, 0);

		// Re-Fill hashtable by parsing the tree
		//if (fc->htAllFiles)
		// It's important that this table be built, so that when the piggs are loaded in prod mode 
		// they make shallow copies of they keynames into shared memory, and don't make extra local
		// deep copies of every string
		
		FolderCachePruneCoreExclusions(fc, NULL);
		FolderCacheBuildHashTable(fc, fc->root, "");

		FolderNodeGetCounts(&discard, &update);
		if (folder_cache_debug)
			OutputDebugStringf("%25s: Added %5d files and %5d folders with %5d updates and %5d discards in %1.3gs\n", basepath, count_file, count_folder, update, discard, timerElapsed(timer));

		timerFree(timer);
	}
}

#if !PLATFORM_CONSOLE
static void ErrorfPopupCallback(ErrorMessage* errMsg, void *userdata)
{
	char *errString = errorFormatErrorMessage(errMsg);
	if (!consoleIsCursorOnLeft())
		printf("\n");
	printfColor(COLOR_RED|COLOR_BRIGHT, "ERROR: %s\n", errString);
	if (errMsg->errorCount < 5 && errMsg->bRelevant)
		MessageBox_UTF8(compatibleGetConsoleWindow(), errString, "Error", MB_OK);
}
#endif


static void FolderCacheLoadCoreExclusions(FolderCache *fc)
{
	extern bool g_force_absolute_paths;
	char path[MAX_PATH];
	bool bSaved = g_force_absolute_paths;
	assert(!fc->core_exclusions);
	sprintf(path, "%s/CoreExclude.txt", fileDataDir());
	g_force_absolute_paths = true; // Prevent the file code from building the folder cache hashtable automtaically
#if !PLATFORM_CONSOLE
	ErrorfPushCallback(ErrorfPopupCallback, NULL);
#endif
	fc->core_exclusions = fileSpecTreeLoad(path);
#if !PLATFORM_CONSOLE
	ErrorfPopCallback();
#endif
	g_force_absolute_paths = bSaved;
}

static void FolderCachePruneCoreExclusionsInternal(
	FolderCache *fc,
	FolderNode **head,
	FolderNode **tail,
	FileSpecTree *fstree,
	bool bAlreadyHaveCritical)
{
	//char temp[MAX_PATH];
	FolderNode *node = *head, *next;
	if (!fstree || !node)
		return; // Everything under here included

	if (!bAlreadyHaveCritical)
		FolderNodeEnterCriticalSection();

	do
	{
		FileSpecTree *action;
		next = node->next;

		// Check if this is a core file or not
		ANALYSIS_ASSUME(INRANGE(VIRTUAL_LOCATION_TO_LOOKUP_INDEX(node->virtual_location), 0, ARRAY_SIZE(fc->is_core)));
		if (!node->is_dir && !fc->is_core[VIRTUAL_LOCATION_TO_LOOKUP_INDEX(node->virtual_location)])
		{
			//printfColor(COLOR_GREEN|COLOR_BLUE, "Skipping     %s\n", FolderNodeGetFullPath(node, temp));
			continue;
		}

		// Check this node for pruning
		action = fileSpecTreeGetAction(fstree, node->name);
		if (action == FST_NOT_SPECIFIED || // Default exclude
			action == FST_EXCLUDED)
		{
			// Remove it and all core children
			if (node->is_dir)
			{
				//printfColor(COLOR_RED, "Pruning DIR  %s\n", FolderNodeGetFullPath(node, temp));
				FolderCachePruneCoreExclusionsInternal(fc, &node->contents, &node->contents_tail, FST_EXCLUDED, true);
			} else {
				//printfColor(COLOR_RED, "Pruning FILE %s\n", FolderNodeGetFullPath(node, temp));
				FolderNodeDeleteFromTreeEx(fc, node, 0);
			}
		} else if (action == FST_INCLUDED)
		{
			// Include it and all children
			if (node->is_dir)
			{
				//printfColor(COLOR_GREEN, "Leaving DIR  %s\n", FolderNodeGetFullPath(node, temp));
			} else {
				//printfColor(COLOR_GREEN, "Leaving FILE %s\n", FolderNodeGetFullPath(node, temp));
			}
		} else { // action must be a folder
			if (!node->is_dir)
			{
				Errorf("CoreExclude.txt references a folder named \"%s\" but a file exists named \"%s\".",
					action->filespec,
					node->name);
			} else {
				//printfColor(COLOR_GREEN|COLOR_RED, "Recursing on %s\n", FolderNodeGetFullPath(node, temp));
				FolderCachePruneCoreExclusionsInternal(fc, &node->contents, &node->contents_tail, action, true);
			}
		}
	} while ((node = next));

	if (!bAlreadyHaveCritical)
		FolderNodeLeaveCriticalSection();
}

static bool g_process_core_exclusions;
// Used to disable core exclusions if something breaks
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void FolderCacheAllowCoreExclusions(bool bAllow)
{
	g_process_core_exclusions = bAllow;
}

void FolderCachePruneCoreExclusions(FolderCache *fc, const char *recently_scanned_folder)
{
	char folder_to_check[MAX_PATH];
	if (!g_process_core_exclusions || !fc->core_exclusions)
		return;
	if (recently_scanned_folder)
	{
		char *s;
		strcpy(folder_to_check, recently_scanned_folder);
		if ((s = strchr(folder_to_check, '/')))
			*s = '\0';
		// If this folder does not have a root entry in the tree, skip it!
		if (fileSpecTreeGetAction(fc->core_exclusions, folder_to_check)==FST_INCLUDED)
			return;
	}
//	{int t = timerAlloc();
	FolderCachePruneCoreExclusionsInternal(fc, &fc->root, &fc->root_tail, fc->core_exclusions, false);
//	printf("\nPRUNING TOOK %1.3fs\n", timerElapsed(t));
//	timerFree(t);}
}

static bool FolderCacheIsCoreExcludedInternal(FolderCache *fc, FileSpecTree *fstree, const char *filename)
{
	char buf[MAX_PATH];
	char *s;
	char *next=NULL;
	FileSpecTree *action;
	strcpy(buf, filename);
	s = strchr(buf, '/');
	if (s) {
		*s = '\0';
		next = s+1;
	}
	// Check this node for pruning
	action = fileSpecTreeGetAction(fstree, buf);
	if (action == FST_NOT_SPECIFIED || // Default exclude
		action == FST_EXCLUDED)
	{
		// Remove it and all core children
		return true;
	} else if (action == FST_INCLUDED)
	{
		// Include it and all children
		return false;
	} else { // action must be a folder
		if (!next)
		{
			Errorf("CoreExclude.txt references a folder named \"%s\" but a file exists named \"%s\".",
				action->filespec,
				filename);
			return false;
		} else {
			return FolderCacheIsCoreExcludedInternal(fc, action, next);
		}
	}
}

static bool FolderCacheIsCoreExcluded(FolderCache *fc, const char *filename)
{
	if (!g_process_core_exclusions || !fc->core_exclusions)
		return false;
	return FolderCacheIsCoreExcludedInternal(fc, fc->core_exclusions, filename);
}


int FolderCacheAddAllPigs(FolderCache *fc) {
	int i;

	// If this gets hit, some code was changed which is causing the creation of a rendering device
	//   or other client startup code to try to access the filesystem.  This is not allowed until after
	//   we have displayed the initial loading screen.
	assert(!g_do_not_try_to_load_folder_cache);

	if (FolderCacheGetMode() == FOLDER_CACHE_MODE_FILESYSTEM_ONLY) {
		return 0;
	}
	if (-1 == PigSetGetNumPigs()) {
		FolderCacheBuildHashTable(fc, fc->root, "");
		return 1; //TODO return an error
	}

	for (i=0; i<PigSetGetNumPigs(); i++)  {
		FolderCacheAddHog(fc, PigSetGetHogFile(i), PIG_INDEX_TO_VIRTUAL_LOCATION(i), i);
	}

	// fc->htAllFiles should not have been made yet, otherwise it will be bloated with files about to be pruned
	// (and it is also faster to do all of the additions at once).  If these asserts are going off at random,
	// they should be safe to remove, but we should later track down what is initializing the folder cache early.
	// JE: Removed them, as they seem to sometimes fire randomly/inconsistently
	//devassert(!fc->htAllFiles);
	FolderCacheLoadCoreExclusions(fc);
	//devassert(!fc->htAllFiles);
	FolderCachePruneCoreExclusions(fc, NULL);
	//devassert(!fc->htAllFiles);

	FolderCacheBuildHashTable(fc, fc->root, "");
	return 0;
}

typedef struct IndexPair {
	U32 index;
	const char *name;
} IndexPair;

__forceinline static int cmpIndexPair(const IndexPair *a, const IndexPair *b)
{
	return inline_stricmp(a->name, b->name);
}

static SimpleFileSpec *getFileSpecsForHog(HogFile *hog_file)
{
	char path[MAX_PATH];
	char *s;
	strcpy(path, hogFileGetArchiveFileName(hog_file));
	if (s=strrchr(path, '/'))
		*s = '\0';
	if (s=strrchr(path, '/'))
		*s = '\0';
	if (s=strrchr(path, '/'))
		*s = '\0';
	strcat(path, "/.patch/MirrorFilespec.txt");
	return simpleFileSpecLoad(path, "data/");
}


/*
   Multikey quicksort, a radix sort algorithm for arrays of character
   strings by Bentley and Sedgewick.

   J. Bentley and R. Sedgewick. Fast algorithms for sorting and
   searching strings. In Proceedings of 8th Annual ACM-SIAM Symposium
   on Discrete Algorithms, 1997.

   http://www.CS.Princeton.EDU/~rs/strings/index.html

   The code presented in this file has been tested with care but is
   not guaranteed for any purpose. The writer does not offer any
   warranties nor does he accept any liabilities with respect to
   the code.

   Ranjan Sinha, 1 jan 2003.

   School of Computer Science and Information Technology,
   RMIT University, Melbourne, Australia
   rsinha@cs.rmit.edu.au

*/


__forceinline static void inssort(IndexPair *a, int n, int d)
{
	IndexPair* pi;
	IndexPair* pj;
	const unsigned char* s;
	const unsigned char* t;

	for (pi = a + 1; --n > 0; pi++)
	{
		IndexPair tmp = *pi;

		for (pj = pi; pj > a; pj--)
		{
			for (s=(*(pj-1)).name+d, t=tmp.name+d; *s==*t && *s!=0; ++s, ++t)
			{
				if (*s!=*t && towlower(*s) != tolower(*t))
					break;
			}
			if (towlower(*s) <= tolower(*t))
				break;
			*pj = *(pj-1);
		}
		*pj = tmp;
	}
}

/* MULTIKEY QUICKSORT */

#ifndef min
#define min(a, b) ((a)<=(b) ? (a) : (b))
#endif

/* ssort2 -- Faster Version of Multikey Quicksort */

__forceinline static void vecswap2(IndexPair *a, IndexPair *b, int n)
{   while (n-- > 0)
	{
        IndexPair t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

#define swap2(a, b) { t = *(a); *(a) = *(b); *(b) = t; }
#define ptr2char(i) (tolower(*((i)->name + depth)))

__forceinline static IndexPair *med3func(IndexPair *a, IndexPair *b, IndexPair *c, int depth)
{   int va, vb, vc;
    if ((va=ptr2char(a)) == (vb=ptr2char(b)))
        return a;
    if ((vc=ptr2char(c)) == va || vc == vb)
        return c;       
    return va < vb ?
          (vb < vc ? b : (va < vc ? c : a ) )
        : (vb > vc ? b : (va < vc ? a : c ) );
}
#define med3(a, b, c) med3func(a, b, c, depth)

static void mkqsort(IndexPair *a, int n, int depth)
{   int d, r, partval;
	IndexPair	*pa, *pb, *pc, *pd, *pl, *pm, *pn, t;
    if (n < 20) {
        inssort(a, n, depth);
        return;
    }
    pl = a;
    pm = a + (n/2);
    pn = a + (n-1);
    if (n > 30) { /* On big arrays, pseudomedian of 9 */
        d = (n/8);
        pl = med3(pl, pl+d, pl+2*d);
        pm = med3(pm-d, pm, pm+d);
        pn = med3(pn-2*d, pn-d, pn);
    }
    pm = med3(pl, pm, pn);
    swap2(a, pm);
    partval = ptr2char(a);
    pa = pb = a + 1;
    pc = pd = a + n-1;
    for (;;) {
        while (pb <= pc && (r = ptr2char(pb)-partval) <= 0) {
            if (r == 0) { swap2(pa, pb); pa++; }
            pb++;
        }
        while (pb <= pc && (r = ptr2char(pc)-partval) >= 0) {
            if (r == 0) { swap2(pc, pd); pd--; }
            pc--;
       }
        if (pb > pc) break;
        swap2(pb, pc);
        pb++;
        pc--;
    }
    pn = a + n;
    r = min(pa-a, pb-pa);    vecswap2(a,  pb-r, r);
    r = min(pd-pc, pn-pd-1); vecswap2(pb, pn-r, r);
    if ((r = pb-pa) > 1)
        mkqsort(a, r, depth);
    if (ptr2char(a + r) != 0)
        mkqsort(a + r, pa-a + pn-pd-1, depth+1);
    if ((r = pd-pc) > 1)
        mkqsort(a + n-r, r, depth);
}

static void FolderCacheAddHog(FolderCache *fc, HogFile *hog_file, int virtual_location, int pig_set_index)
{
	U32 i, j;
	int skipped=0;
	int update, discard;
	char temp[CRYPTIC_MAX_PATH];
	int timer = timerAlloc();
	bool addToHashtable=!!fc->htAllFiles;
	U32 num_files;
	PigChangeCallbackMapping *callback_data;
	IndexPair *indices;
	SimpleFileSpec *mirrorfilespec;
	int this_hogs_diskmirror_virtual_location=0;
	timerStart(timer);

	if (FolderCache_GetFromVirtualLocation(fc, virtual_location))
	{
		assert(!"Two pigs with the same virtual_location!");
		timerFree(timer);
		return;
	}

	// Add to virtual_location list
	STR_COMBINE_SS(temp, hogFileGetArchiveFileName(hog_file), ":");
//	assert(temp[1]==':' || temp[0]=='.');
	assert(fileIsAbsolutePath(temp));
	
	loadstart_printf("Add to folder cache: %s...", temp);

	// Find what virtual location we're in
	this_hogs_diskmirror_virtual_location = -1;
	for (i=0; i<eaUSize(&fc->gamedatadirs); i++)
	{
		if (strStartsWith(temp, fc->gamedatadirs[i]))
		{
			this_hogs_diskmirror_virtual_location = i;
		}
	}
#ifdef _XBOX
	if (this_hogs_diskmirror_virtual_location == -1)
	{
		if (strstri(temp, "core"))
		{
			this_hogs_diskmirror_virtual_location = eaSize(&fc->gamedatadirs); // Imaginary virtual location for core
			fc->is_core[VIRTUAL_LOCATION_TO_LOOKUP_INDEX(this_hogs_diskmirror_virtual_location)] = 1;
		} else {
			devassert(stricmp(fc->gamedatadirs[0], "Game:")==0);
			this_hogs_diskmirror_virtual_location = 0;
		}
	}
#endif

	if (strStartsWith(temp, HOG_FILENAME_RESOURCE_PREFIX))
		this_hogs_diskmirror_virtual_location = 0;

	if (isDevelopmentMode())
	{
		assert(this_hogs_diskmirror_virtual_location != -1);
	} else {
		if (this_hogs_diskmirror_virtual_location == -1)
			this_hogs_diskmirror_virtual_location = 0;
	}

	FolderCache_SetFromVirtualLocation(fc, virtual_location, strdup(temp), NULL, fc->is_core[VIRTUAL_LOCATION_TO_LOOKUP_INDEX(this_hogs_diskmirror_virtual_location)]);

	mirrorfilespec = getFileSpecsForHog(hog_file);

	hogFileLock(hog_file); // Make sure no one changes it while we're adding the information but before we've set up our callbacks!
	if (addToHashtable)
	{
		FolderNodeEnterCriticalSection(); // Just grab this before addToHashTable and hog's data CS
		hogFileLockDataCS(hog_file); // Locking this to prevent deadlock detection from mis-firing when we grab critSectAllFiles belo
		EnterCriticalSection(&fc->critSecAllFiles);
	}

	// Sort the files first, as the FolderCacheAdd is optimized for adding to the end of the list
	num_files = hogFileGetNumFiles(hog_file);
	indices = ScratchAlloc(sizeof(indices[0])*num_files);
	j = 0;
	for (i=0; i<num_files; i++)
	{
		const char *relpath = hogFileGetFileName(hog_file, i);
		if (!relpath || hogFileIsSpecialFile(hog_file, i)) {
			skipped++;
			continue;
		}
		indices[j].index = i;
		indices[j].name = relpath;
		j++;
	}
	// used mkqsort because it's WAY faster when the leading characters are the same (like in pathnames)
	mkqsort(indices, j, 0);

	for (i=0; i<j; i++)
	{
		const char *relpath = indices[i].name;
		FolderNode *node;
		int created;

		if (FolderCacheIsIgnored(relpath))
			continue; // Pretend it does not exist

		if (FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT) {
			if (NULL==FolderCacheQueryEx(fc, relpath, false, false)) {
				//printf("File %s not found in File system, but is in Pig, NOT adding\n", relpath);
				skipped++;
				continue;
			}
		}
		//printf("%s\n", relpath);
		if (addToHashtable)
		{
			// Data CS is locked
			node = FolderNodeAdd(&fc->root, &fc->root_tail, NULL, relpath,
				hogFileGetFileTimestampInternal(hog_file, indices[i].index),
				hogFileGetFileSizeInternal(hog_file, indices[i].index), virtual_location, indices[i].index, &created, 0, relpath);
		} else {
			node = FolderNodeAdd(&fc->root, &fc->root_tail, NULL, relpath,
				hogFileGetFileTimestamp(hog_file, indices[i].index),
				hogFileGetFileSize(hog_file, indices[i].index), virtual_location, indices[i].index, &created, 0, relpath);
		}

		if(node){
			if (addToHashtable) {
				FolderCacheHashTableAdd(fc, relpath, node);
			}
			if (simpleFileSpecExcludesFile(relpath, mirrorfilespec))
			{
				FolderNode *parent;
				node->seen_in_fs = 1; // Pretend it's been seen on-disk already so it can load the hogg version
				node->seen_in_fs_virtual_location = this_hogs_diskmirror_virtual_location;
				node->non_mirrored_file = 1;
				for (parent = node->parent; parent && !parent->seen_in_fs; parent = parent->parent)
				{
					parent->seen_in_fs = 1;
					parent->seen_in_fs_virtual_location = this_hogs_diskmirror_virtual_location;
					parent->non_mirrored_file = 1;
				}
			}
			
			// Callback if created.
			if(created && fc->nodeCreateCallback){
				fc->nodeCreateCallback(fc, node);
			}
		}
	}
	ScratchFree(indices);

	simpleFileSpecDestroy(mirrorfilespec);

	// Fill hashtable by parsing the tree
	//FolderCacheBuildHashTable(fc, fc->root, "");

	callback_data = calloc(sizeof(*callback_data), 1);
	callback_data->fc = folder_cache;
	callback_data->virtual_location = virtual_location;
	hogFileSetCallbacks(hog_file, callback_data, fcCallbackUpdated);

	if (addToHashtable)
	{
		LeaveCriticalSection(&fc->critSecAllFiles);
		FolderNodeLeaveCriticalSection();
		hogFileUnlockDataCS(hog_file);
	}
	hogFileUnlock(hog_file);

	FolderNodeGetCounts(&discard, &update);
	if (folder_cache_debug)
		OutputDebugStringf("%25s: Added %5d files (%5d skipped) with %5d updates and %5d discards in %1.3gs\n", "PIG", hogFileGetNumFiles(hog_file) - skipped, skipped, update, discard, timerElapsed(timer));
	timerFree(timer);
	
	loadend_printf("done");
}


void printFilenameCallback(const char *relpath, int when) { // example/test callback
	printf("FolderCacheCallback on : %s\n", relpath);
}


static FolderCacheCallbackInfo *fccallbacks=NULL;
static FolderCacheCallbackInfo *queued_callbacks=NULL;

void FolderCacheSetCallback(int when, const char *filespec, FolderCacheCallback callback) {
	FolderCacheCallbackInfo * newitem;
	assert(when!=0);

	newitem = listAddNewMember(&fccallbacks, sizeof(FolderCacheCallbackInfo));
	strcpy(fccallbacks->filespec, filespec);
	fccallbacks->callback = callback;
	fccallbacks->when = when;
}

void FolderCacheSetCallbackEx(int when, const char *filespec, FolderCacheCallbackEx callback, void *userData) {
	FolderCacheCallbackInfo * newitem;
	assert(when!=0);

	newitem = listAddNewMember(&fccallbacks, sizeof(FolderCacheCallbackInfo));
	strcpy(fccallbacks->filespec, filespec);
	fccallbacks->callbackEx = callback;
	fccallbacks->when = when;
	fccallbacks->isEx = 1;
	fccallbacks->userData = userData;
}

static void checkForCallbacks(FolderCache* fc, FolderNode* node, int virtual_location, int whathappened, const char *filename) {
	FolderCacheCallbackInfo *walk;
	FolderCacheCallbackInfo *newitem;
	for (walk = fccallbacks; walk; walk = walk->next) {
		if ((whathappened & walk->when) && matchExact(walk->filespec, filename)) {
			newitem = listAddNewMember(&queued_callbacks, sizeof(FolderCacheCallbackInfo));
			if(walk->isEx){
				newitem->fc = fc;
				newitem->node = node;
				newitem->isEx = 1; // Just for debug, not used
			}
			newitem->callback = walk->callback;
			newitem->userData = walk->userData;
			strcpy(newitem->filespec, filename);
			newitem->when = whathappened;
			newitem->virtual_location = virtual_location;			
		}
	}
}

static void removeCallbacks(FolderNode* node)
{
	FolderCacheCallbackInfo *walk = queued_callbacks;
	while (walk) {
		FolderCacheCallbackInfo *next = walk->next;
		if (walk->node == node) {
			listFreeMember(walk, &queued_callbacks);
		}
		walk = next;
	}
}

unsigned int hashFolderCacheCallbackInfo(const FolderCacheCallbackInfo *info, int hashSeed)
{
	unsigned int ret = stashDefaultHashCaseInsensitive(info->filespec, (U32)strlen(info->filespec), hashSeed);
	ret = hashCalc(&info->callback, sizeof(info->callback), ret);
	return ret;
}

int compFolderCacheCallbackInfo(const FolderCacheCallbackInfo* key1, const FolderCacheCallbackInfo* key2)
{
	int ret = stricmp(key1->filespec, key2->filespec);
	if (ret)
		return ret;
	if (key1->callback > key2->callback)
		return 1;
	if (key1->callback < key2->callback)
		return -1;
	return 0;
}


static void doAllCallbacks(void)
{
	FolderCacheCallbackInfo *newlist;
	FolderCacheCallbackInfo *walk;
	FolderCacheCallbackInfo *tail;
	StashTable stCallbacks;
	int listSize;
	
	PERFINFO_AUTO_START_FUNC();
	
	FolderNodeEnterCriticalSection();
	newlist = queued_callbacks;
	queued_callbacks = NULL; // Just run over a local list, as queued_callbacks might get added to while running reload callbacks
	FolderNodeLeaveCriticalSection();
	if (!newlist)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	walk = newlist;

	// Remove elements which are identical to a later callback, to prevent multiple callbacks on the same file
	listSize = listLength(newlist);
	stCallbacks = stashTableCreateExternalFunctions(listSize * 1.7, StashDefault, hashFolderCacheCallbackInfo, compFolderCacheCallbackInfo);
	tail = walk;
	while (walk) {
		FolderCacheCallbackInfo *next = walk->next;
		if (!stashAddPointer(stCallbacks, walk, walk, false)) {
			// Already in there, keep the first one in the list (execute later)
			listFreeMember(walk, &newlist);
		} else {
			tail = walk;
		}
		walk = next;
	}
	stashTableDestroy(stCallbacks);
	assert(listInList(newlist, tail));

	while (tail) {
		FolderCacheCallbackInfo *next = tail->prev;
		ErrorfResetCounts();
		if(tail->fc){
			tail->callbackEx(tail->fc, tail->node, tail->virtual_location, tail->filespec, tail->when, tail->userData);
		}else{
			tail->callback(tail->filespec, tail->when);
		}
		listFreeMember(tail, &newlist);
		tail = next;
	}

	PERFINFO_AUTO_STOP();
}

void FolderCacheSetManualCallbackMode(int on) // Call this if you're calling FolderCacheDoCallbacks each tick
{
	folder_cache_manual_callback_check = on;
}

//returns 1 if it did something
AUTO_COMMAND;
int FolderCacheDoCallbacks(void)
{
	PERFINFO_AUTO_START_FUNC();

	folder_cache_manual_callback_check = 1;
	FolderCacheQueryEx(NULL, NULL, true, false);

	if(g_bPausedCallback)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if (queued_callbacks)
	{
		doAllCallbacks();
		PERFINFO_AUTO_STOP();
		return 1;
	}
	else
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
}

#if !_PS3
#pragma optimize( "", off )
#endif
static void handleDeleteInternal(FolderCache *fc, FolderNode *node, int virtual_location) {
	// TODO: make this do the following:
	//  If it's a folder deleted, call handleDelete on all entries off of the folder that match
	//     the virtual location
	//  If it's a file, and the node matches the virtual_location, scan the file system sources
	//     to see if it still exists in another one of the sources

	// Remove from any callbacks that might be referencing it (we'll get a DELETE callback instead, and possibly an ADD later)
	removeCallbacks(node);

	// Remove from tree
	FolderNodeDeleteFromTree(fc, node);
}

static void FolderCacheRemoveNonexistent(FolderCache* fc,__in const char* relativeDir){
	WIN32_FIND_DATAA wfd;
	U32				handle;
	FolderNode*		dirNode = FolderNodeFind(fc->root, relativeDir);
	FolderNode*		curNode;
	S32				good;
	char			buffer[CRYPTIC_MAX_PATH];
	
	// Find the first node in the dir.
	
	if(dirNode){
		dirNode = dirNode->contents;
	}
	else if(!relativeDir || !relativeDir[0]){
		dirNode = fc->root;
	}
	else{
		//printf("Can't find dir: %s/%s\n", fc->gamedatadirs[0], relativeDir);
		return;
	}
	
	// Mark all nodes for deletion.
	
	for(curNode = dirNode; curNode; curNode = curNode->next){
		curNode->delete_if_absent = 1;
	}
	
	assert(fc->gamedatadirs[0]);
	STR_COMBINE_SSSS(buffer, fc->gamedatadirs[0], "/", relativeDir, "/*");

	for(good = fwFindFirstFile(&handle, buffer, &wfd); good; good = fwFindNextFile(handle, &wfd)){
		const char* fileName = wfd.cFileName;
		FolderNode* node;
		
		node = FolderNodeFind(dirNode, fileName);
		
		if(node){
			node->delete_if_absent = 0;
		}
	}
	
	for(curNode = dirNode; curNode;){
		FolderNode* next = curNode->next;
		
		if(curNode->delete_if_absent){
			STR_COMBINE_SSS(buffer, relativeDir, relativeDir[0] ? "/" : "", curNode->name);

			//printf("Deleting absent node: %s/%s (deleted using short file name?)\n", fc->gamedatadirs[0], buffer);

			handleDeleteInternal(fc, curNode, 0);
		}

		curNode = next;
	}

	fwFindClose(handle);
}

static void handleDelete(FolderCache *fc, int virtual_location, const char *relpath_const, int log)
{
	char relpath[CRYPTIC_MAX_PATH];
	FolderNode *node = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	
	Strncpyt(relpath, relpath_const);
	forwardSlashes(relpath);
	FolderNodeEnterCriticalSection();
	if (fc->htAllFiles)
	{
		EnterCriticalSection(&fc->critSecAllFiles);
		if (!stashFindPointer(fc->htAllFiles, relpath, &node))
		{
			node = NULL;
		}
		LeaveCriticalSection(&fc->critSecAllFiles);
	} else {
		node = FolderNodeFind(fc->root, relpath);
	}
	// File has been deleted
	if (node==NULL) {
		if(strchr(relpath, '~')){
			// Deleted using short filename, so no way to know for sure which file it was... rescanning parent.
			
			if(	FolderCache_GetNumGDDs(fc) == 1 &&
				FolderCache_GetNumPiggs(fc) == 0)
			{
				char buffer[1000];
				char* lastSlash;

				strcpy(buffer, relpath_const);
				
				forwardSlashes(buffer);
				
				lastSlash = strrchr(buffer, '/');
				
				if(lastSlash){
					*lastSlash = 0;
				}else{
					buffer[0] = 0;
				}

				FolderCacheRemoveNonexistent(fc, buffer);
			}
		}
		else if (folder_cache_debug>=2 && log) {
			// Happens when things are too quick for us to catch, just let it go by
			printfColor(COLOR_RED, "(created and deleted)\n");
		}
	} else {
		if (!node->contents ||
			FolderCache_GetNumGDDs(fc) == 1 &&
			FolderCache_GetNumPiggs(fc) == 0)
		{
			// Only delete it IF:
			//   a. It's an empty tree in all gamedatadirs (and thus has no "contents"), OR...
			//   b. There's only one gamedatadir, OR...
			//   c. It's a single file.

			bool needToCheck=false;
			if (folder_cache_debug>=2 && log) {
				printfColor(COLOR_RED|COLOR_BRIGHT, "deleting\n");
			}
			if (node && !node->is_dir)
				needToCheck = true;
			handleDeleteInternal(fc, node, virtual_location);
			if (needToCheck)
				checkForCallbacks(fc, NULL, virtual_location, FOLDER_CACHE_CALLBACK_DELETE, relpath);
		}
	}
	FolderNodeLeaveCriticalSection();
	if (fc->htAllFiles) {
		EnterCriticalSection(&fc->critSecAllFiles);
		FolderCacheHashTableRemove(fc, relpath);
		LeaveCriticalSection(&fc->critSecAllFiles);
	}

	PERFINFO_AUTO_STOP();
}

static void handleUpdate(FolderCache *fc, int virtual_location, int file_index, const char *relpath_const, U32 filesize, __time32_t timestamp, U32 attrib, int isCreate, int log)
{
	char relpath[CRYPTIC_MAX_PATH];
	FolderNode *node;

	PERFINFO_AUTO_START_FUNC();

	strcpy(relpath, relpath_const);
	forwardSlashes(relpath);
	FolderNodeEnterCriticalSection();
	if (fc->htAllFiles)
	{
		EnterCriticalSection(&fc->critSecAllFiles);
		if (!stashFindPointer(fc->htAllFiles, relpath, &node))
		{
			node = NULL;
		}
		LeaveCriticalSection(&fc->critSecAllFiles);
	} else {
		node = FolderNodeFind(fc->root, relpath);
	}
	// File has changed or is new
	if (folder_cache_debug>=2 && log) {
		if (node==NULL) {
			printfColor(COLOR_GREEN | COLOR_BRIGHT, "adding\n");
		} else {
			printfColor(COLOR_BLUE | COLOR_BRIGHT, "updating\n");
		}
	}

	if (node && (virtual_location < node->virtual_location
		|| (FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC && virtual_location < node->seen_in_fs_virtual_location)))
	{
		// New update is in a hogg, or an overridden path, and our current node is not.
		// It's either overridden on disk already, or it's really the same thing
		//  - safest is to just use the on-disk version.
		// Do nothing.
	} else if (node && (node->timestamp == timestamp || ABS_UNS_DIFF(node->timestamp, timestamp)==3600) && node->size == filesize) {
		node->timestamp = timestamp;
		// If the original location is in a hogg, and we're identical, use the hogg
		if (node->virtual_location >= 0)
		{
			node->virtual_location = virtual_location;
			node->file_index = file_index;
		}
		if (virtual_location >= 0) // Only update attributes if this update is coming from the filesystem, not a hogg callback
		{
			if ((node->writeable != (U32)!(attrib & _A_RDONLY)) ||
				(node->hidden != (U32)!!(attrib & _A_HIDDEN)) ||
				(node->system != (U32)!!(attrib & _A_SYSTEM)))
			{
				node->writeable = !(attrib & _A_RDONLY);
				node->hidden = !!(attrib & _A_HIDDEN);
				node->system = !!(attrib & _A_SYSTEM);
				if (!node->is_dir)
					checkForCallbacks(fc, node, virtual_location, FOLDER_CACHE_CALLBACK_ATTRIB_CHANGE, relpath);
			}
		}
	} else {
		int created=0;

		assert(virtual_location >= -MAX_DATA_DIRS_OR_PIGS && virtual_location < MAX_DATA_DIRS_OR_PIGS);
		if (fc->is_core[VIRTUAL_LOCATION_TO_LOOKUP_INDEX(virtual_location)] &&
			FolderCacheIsCoreExcluded(fc, relpath))
		{
			node = NULL; // Excluded
		} else {
			node = FolderNodeAdd(&fc->root, &fc->root_tail, NULL, relpath, timestamp, filesize, virtual_location, file_index, &created, 0, 0);
		}

		if (node) { // Sometimes it doesn't do an update, like if it is just responding to an access time modification
			node->is_dir = attrib & _A_SUBDIR ? 1 : 0;
			// Update attributes after update/create
			node->writeable = !(attrib & _A_RDONLY);
			node->hidden = !!(attrib & _A_HIDDEN);
			node->system = !!(attrib & _A_SYSTEM);
			node->virtual_location = virtual_location;
			node->file_index = file_index;

			// Callback if created.
			if(created && fc->nodeCreateCallback){
				fc->nodeCreateCallback(fc, node);
			}

			EnterCriticalSection(&fc->critSecAllFiles);
			//assert(fc->htAllFiles);		// Adam: Should be handled fine, according to Jimb :)
			if (fc->htAllFiles)
			{
				const char* pcPath = allocAddFilename(relpath);
				FolderCacheHashTableAdd(fc, pcPath, node);
			}
			//assert(hashFindElement(fc->allfiles, relpath));
			LeaveCriticalSection(&fc->critSecAllFiles);

			if(	created &&
				fc->scanCreatedFolders &&
				node->is_dir &&
				FolderCache_GetNumGDDs(fc) == 1 &&
				FolderCache_GetNumPiggs(fc) == 0)
			{
				char buffer[CRYPTIC_MAX_PATH];

				STR_COMBINE_SSS(buffer, fc->gamedatadirs[0], "/", relpath);

				FolderCacheAddFolderToNode(	fc,
					&node->contents,
					&node->contents_tail,
					node,
					buffer,
					0,
					0);

				FolderCachePruneCoreExclusions(fc, NULL);
				FolderCacheBuildHashTable(fc, node->contents, relpath);
			}
		}
		if (node && !node->is_dir)
			checkForCallbacks(fc, node, virtual_location, FOLDER_CACHE_CALLBACK_UPDATE, relpath);
	}
	FolderNodeLeaveCriticalSection();
	
	PERFINFO_AUTO_STOP();
}


static int countSameEndingCharacters(const char *s1, const char *s2)
{
	int ret=0;
	int len1=(int)strlen(s1)-1;
	int len2=(int)strlen(s2)-1;
	while (len1 && len2 && s1[len1]==s2[len2]) {
		ret++;
		len1--;
		len2--;
	}
	return ret;
}

static int getLikelyVirtualLocationFromRemotePath(const char *contextDirName)
{
	int i;
	int virutal_location_best=0;
	int best_length=0;
	// Find the virtual location that best matches
	for (i=0; i<eaSize(&folder_cache->gamedatadirs); i++) {
		int len = countSameEndingCharacters(contextDirName, folder_cache->gamedatadirs[i]);
		if (len > best_length) {
			best_length = len;
			virutal_location_best = i;
		}
	}
	return virutal_location_best;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTONLY ACMD_CLIENTCMD;
void FolderCacheHandleRemoteUpdate(const char *contextDirName, const char *relpath, U32 filesize, U32 timestamp, U32 attrib)
{
	handleUpdate(folder_cache, getLikelyVirtualLocationFromRemotePath(contextDirName), -1,
		relpath, filesize, timestamp, attrib, 0, 1);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_CLIENTONLY ACMD_CLIENTCMD;
void FolderCacheHandleRemoteDelete(const char *contextDirName, const char *relpath)
{
	handleDelete(folder_cache, getLikelyVirtualLocationFromRemotePath(contextDirName), relpath, 1);
}



int FolderCacheSetDirMonTimeout(int timeout){
	int old = folder_cache_dirMonTimeout;
	
	folder_cache_dirMonTimeout = timeout;
	
	return old;
}

void FolderCacheSetNoDisableOnBufferOverruns(int value){
	folder_cache_noDisableOnBufferOverruns = value;
}

static bool fc_last_update_had_change=false;
static bool FolderCacheLastUpdateHadChange(void)
{
	bool ret = fc_last_update_had_change;
	fc_last_update_had_change = false;
	return ret;
}

static bool fc_updates_likely_working=true;
bool FolderCacheUpdatesLikelyWorking(void)
{
	return fc_updates_likely_working;
}

static bool fc_do_not_warn_on_overruns=false;
void FolderCacheDoNotWarnOnOverruns(bool bDoNotWarn)
{
	fc_do_not_warn_on_overruns = bDoNotWarn;
}

static int FolderCacheUpdate(bool do_hog_reload)
{
	DirChangeInfo *dci;
	static volatile long is_doing_update = 0;
	long test;
	static bool hog_file_changed=false;
	DirChangeInfo* bufferOverrun;
	static int overrun_count=0;
	static int overrun_counter=0;
	static __time32_t	lastUpdateTime;
	const __time32_t	curTime = _time32(NULL);
	bool bPrintCount=false;
	int op_count=0;

	if (!folder_cache_update_enable) {
		return 0;
	}

	if (stringCacheReadOnly()) {
		return 0;
	}

	// Enter critical first, so is_doing_update only checks recursive calls
	if (!FolderNodeTryEnterCriticalSection()) {
		return 0;
	}

	test = InterlockedIncrement(&is_doing_update);
	if (test>=2) { // Someone else is in this function
		InterlockedDecrement(&is_doing_update);
		FolderNodeLeaveCriticalSection();
		return 1; // No updates while in a callback!
	}

// Debugging if we're not calling it often enough
// 	if (lastUpdateTime && curTime - lastUpdateTime > 2)
// 	{
// 		printfColor(COLOR_BLUE|COLOR_BRIGHT, "%ds since last FolderCacheUpdate() call\n", curTime - lastUpdateTime);
// 		bPrintCount = true;
// 	}

	// Check for changes
	
	while(dci = dirMonCheckDirs(NULL, folder_cache_dirMonTimeout, &bufferOverrun)) {
		DirectoryUpdateContext *context = (DirectoryUpdateContext*)dci->userData;
		char fullpath[CRYPTIC_MAX_PATH], long_path_name[CRYPTIC_MAX_PATH];
		char *relpath;
		struct _finddata32i64_t fd;
		intptr_t fdHandle;
		int log=1;
		
		PERFINFO_AUTO_START("handleDirMonUpdate", 1);

		fc_last_update_had_change = true;
		op_count++;

		overrun_counter = 0;
		fc_updates_likely_working = true;

		if (folder_cache_debug>=2) {
			if (strnicmp(dci->filename + strlen(dci->filename) - 4, ".log", 4)==0 ||
				strstri(dci->filename, "WINDOWS\\system")!=0 ||
				strstri(dci->filename, "Temp\\vc7")!=0 ||
				strnicmp(dci->filename, "RECYCLER", 8)==0) {
					log=0;
				}
			if (log) {
				consoleSetFGColor(COLOR_RED | COLOR_BLUE | COLOR_GREEN);
				printf("%s/%s: ", dci->dirname, dci->filename);
			}
		}
		// process it

		// Make full path
		STR_COMBINE_SSS(fullpath, dci->dirname, "/", dci->filename);
		makeLongPathName(fullpath, long_path_name);
		strcpy(fullpath, long_path_name);
		// Make relative path from the LFN
		relpath = fullpath + strlen(dci->dirname);
		while (*relpath=='/')
			relpath++;

		// Check if it's a hog file
		if (strEndsWith(dci->filename, ".hogg"))
			hog_file_changed = true;

		// Force a delete, to catch those damn short filename thingies.
		if(dci->isDelete){
			handleDelete(context->fc, context->virtual_location, dci->filename, log);
		}
		
		// Find existing node
		PERFINFO_AUTO_START("_findfirst32i64", 1);
		fdHandle = findfirst32i64_SAFE(fullpath, &fd);
		PERFINFO_AUTO_STOP();
		
		if (fdHandle==-1) {
			// Only re-delete this if the name is different from the one above.
			if(!dci->isDelete || stricmp(dci->filename, relpath)){
				// Deleted!
				handleDelete(context->fc, context->virtual_location, relpath, log);
			}
		} else {
			U32 timestamp = fd.time_write;
			U32 filesize = fd.size; // truncation of size if > 4gb
			
			PERFINFO_AUTO_START("_findclose", 1);
			_findclose(fdHandle);
			PERFINFO_AUTO_STOP();
			
			handleUpdate(context->fc, context->virtual_location, -1, relpath,
				filesize, timestamp, fd.attrib, dci->isCreate, log);
		}
		
		PERFINFO_AUTO_STOP();
	}

	if (bPrintCount && op_count)
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "  and there were %d updates pending\n", op_count);
	}

	if(bufferOverrun){
		DirectoryUpdateContext* updateContext = bufferOverrun->userData;
		
		updateContext->fc->needsRescan = 1;

		if (!folder_cache_noDisableOnBufferOverruns) {
			// Error scanning the directory tree
			overrun_count++;
			overrun_counter++;

			if (overrun_counter > 2 && !fc_do_not_warn_on_overruns)
			{
				fc_updates_likely_working = false;
				// Happened 3 times, perhaps related to OS issue
				overrun_counter = 0;
				Errorf("%s: The application appears to no longer be getting updates from the filesystem.  This will likely cause reloading to fail, and may cause other problems as well.  Please restart the application and if this problem continues, please restart your computer.", getExecutableName());
			}

			printf(	"ERROR reading directory changes (%s)!  %d seconds since last update check."
					" Disabling directory cache!\n",
					bufferOverrun->dirname,
					curTime - lastUpdateTime);
					
			if (isDevelopmentMode())
				FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
			InterlockedDecrement(&is_doing_update);
			FolderNodeLeaveCriticalSection();
			return 1;
		}
	}
	
	lastUpdateTime = curTime;

	InterlockedDecrement(&is_doing_update);
	FolderNodeLeaveCriticalSection();

	// Check for hog modifications
	if (hog_file_changed && do_hog_reload && PigSetInited()) {
		int i;
		for (i=0; i<PigSetGetNumPigs(); i++) {
			hogFileCheckForModifications(PigSetGetHogFile(i));
		}
		hog_file_changed = false;
	}
	if (!folder_cache_manual_callback_check && GetCurrentThreadId()==threadid) {
		doAllCallbacks();
	}
	return 0;
}


typedef struct HogChangeCallback
{
	FolderCache *fc;
	int virtual_location;
	const char *path;
	U32 filesize;
	U32 timestamp;
	HogFileIndex file_index;
} HogChangeCallback;
static CRITICAL_SECTION folder_cache_callback_cs;
static volatile int hog_change_callbacks_queued;
static HogChangeCallback *beaHogChangeCallbacks;

// Must be called within FolderNode CriticalSection
void FolderCacheFlushHogChangeCallbacks(void)
{
	int i;
	static bool bInHere;
	if (!hog_change_callbacks_queued)
		return;
	EnterCriticalSection(&folder_cache_callback_cs);
	assert(!bInHere); // Check for infinite recursion, or even mild amounts
	if (!bInHere)
	{
		bInHere = true;
		hog_change_callbacks_queued = 0;
		for (i=0; i<beaSize(&beaHogChangeCallbacks); i++)
		{
			HogChangeCallback *cb = &beaHogChangeCallbacks[i];
			if (cb->file_index == HOG_INVALID_INDEX)
			{
				handleDelete(cb->fc, cb->virtual_location, cb->path, 1);
			}
			else
			{
				//printf("File new/updated: %s  size:%d  timestamp:%d\n", path, filesize, timestamp);
				handleUpdate(cb->fc, cb->virtual_location, cb->file_index, cb->path, cb->filesize, cb->timestamp, 0, 0, 1);
			}
		}
		beaSetSize(&beaHogChangeCallbacks, 0);
		bInHere = false;
	}
	LeaveCriticalSection(&folder_cache_callback_cs);
}

// Must *not* lock FolderNode CriticalSection
static void fcCallbackUpdated(PigChangeCallbackMapping *mapping, const char* path, U32 filesize, U32 timestamp, HogFileIndex file_index)
{
	HogChangeCallback *cb;
	assert(file_index != 0);
	path = allocAddString(path);
	EnterCriticalSection(&folder_cache_callback_cs);
	hog_change_callbacks_queued = 1;
	cb = beaPushEmpty(&beaHogChangeCallbacks);
	cb->fc = mapping->fc;
	cb->virtual_location = mapping->virtual_location;
	cb->path = path;
	cb->filesize = filesize;
	cb->timestamp = timestamp;
	cb->file_index = file_index;
	LeaveCriticalSection(&folder_cache_callback_cs);
}

#if !_PS3
#pragma optimize( "", on ) // Reset to default
#endif

static StashTable htOverrideCache = 0;
static CRITICAL_SECTION overrideCritSect;

AUTO_RUN_EARLY;
void initOverrideCritSec(void)
{
	InitializeCriticalSection(&overrideCritSect);
	InitializeCriticalSection(&folder_cache_callback_cs);
}

FolderNode *FolderCacheFileSystemOverride(FolderCache *fc, char *relpath)
{
	FolderNode *allocatedNode;
	FolderNode *ret=NULL;
	int i;
	FWStatType sbuf;
	char filename[CRYPTIC_MAX_PATH];
	int in_hash_table=0;

	fileDiskAccessCheck();

	allocatedNode = FolderNodeCreate(); // Allocate before entering critical section

	EnterCriticalSection(&overrideCritSect);

	if (!htOverrideCache) {
		htOverrideCache = stashTableCreateWithStringKeys(1000, StashDeepCopyKeys_NeverRelease);
	}

	ret = (FolderNode*)stashFindPointerReturnPointer(htOverrideCache, relpath);
	if (ret) {
		in_hash_table = 1;
	}
#if !_PS3
	if (relpath[1]==':') { // actually a absolute path
		if (fwStat(relpath, &sbuf)!=-1) { //file exists
			if (!in_hash_table) {
				ret = allocatedNode;
				allocatedNode = NULL;
			}
			strcpy(filename, relpath);
			ret->contents=NULL;
			ret->is_dir= (sbuf.st_mode & _S_IFDIR) ? 1: 0;
			ret->name = strdup(filename); // really passing back an absolute path
			ret->next = ret->parent = ret->prev = NULL;
			ret->size = sbuf.st_size;
			//ret->timestamp = sbuf.st_mtime;
			// reverse what fwStat just did to the filewatcher time and mimic
			// FolderCacheAddFolderToNode
			ret->timestamp = sbuf.st_mtime;
			ret->virtual_location = -99;
			ret->seen_in_fs = 1;
			ret->seen_in_fs_virtual_location = -99;
			ret->writeable = !!(sbuf.st_mode & _S_IWRITE);
			if (!in_hash_table)
				stashAddPointer(htOverrideCache, relpath, ret, true);
			goto cleanup;
		}
		ret = NULL;
		goto cleanup;
	}
#endif

	// Just do a _stat or two, ignore the folder cache
	for (i=FolderCache_GetNumGDDs(fc)-1; i>=0; i--)
	{
		char *folder = FolderCache_GetFromVirtualLocation(fc, i);
		if (!folder) continue;
#if _PS3
        if(folder[0]=='.' && folder[1]=='/')
            sprintf(filename, "/app_home/%s/%s", folder+2, relpath);
        else
#endif
		    STR_COMBINE_SSS(filename, folder, "/", relpath);

		if (folder_cache->is_core[VIRTUAL_LOCATION_TO_LOOKUP_INDEX(i)])
		{
			if (FolderCacheIsCoreExcluded(fc, relpath))
			{
				ret = NULL;
				goto cleanup;
			}
		}

		if (fwStat(filename, &sbuf)!=-1) { //file exists
			if (!in_hash_table) {
				ret = allocatedNode;
				allocatedNode = NULL;
			}
			strcpy(filename, relpath);
			ret->contents=NULL;
			ret->is_dir= (sbuf.st_mode & _S_IFDIR) ? 1: 0;
			if (!ret->name || stricmp(ret->name, filename)!=0) {
				size_t len = strlen(filename)+1;
				SAFE_FREE(ret->name);
				ret->name = malloc(len);
				strcpy_s(ret->name, len, filename);
			}
			ret->next = ret->parent = ret->prev = NULL;
			ret->size = sbuf.st_size;
			//ret->timestamp = sbuf.st_mtime;
			// reverse what fwStat just did to the filewatcher time and mimic
			// FolderCacheAddFolderToNode
			ret->timestamp = sbuf.st_mtime;
			ret->virtual_location = i;
			ret->seen_in_fs = 1;
			ret->seen_in_fs_virtual_location = i;
			ret->writeable = !!(sbuf.st_mode & _S_IWRITE);
			if (!in_hash_table)
				stashAddPointer(htOverrideCache, relpath, ret, true);
			goto cleanup;
		}
	}
	ret = NULL;
	goto cleanup;

cleanup:
	LeaveCriticalSection(&overrideCritSect);
	if (allocatedNode)
		FolderNodeDestroy(fc, allocatedNode); // Must be freed outside of critical section
	return ret;

}

void FolderCacheOverrideDelete(FolderCache *fc, char *relpath)
{
	FolderNode *ret;
	if (!htOverrideCache)
		return;
	EnterCriticalSection(&overrideCritSect);
	stashRemovePointer(htOverrideCache,relpath,&ret);
	LeaveCriticalSection(&overrideCritSect);
}

// Forces the FolderCache to update a file's entry, but this probably is
//  not useful on the PC, as the OS callbacks seem to function correctly
//  and update us automatically.
void FolderCacheForceUpdate(FolderCache *fc, const char *relpath)
{
	FolderNode *oldnode;
	FolderNode *newnode;
	char path[CRYPTIC_MAX_PATH];
	bool bChanged=false;
	FolderNode dummy_node;

	assert(fc && relpath);

	assert(*(U32*)relpath!=0xCCCCCCCC);

	// purdy up relpath
	strcpy(path, relpath);
	forwardSlashes(path);
	fixDoubleSlashes(path);
	assert(relpath[1]!=':');

	// Query from disk
	PERFINFO_AUTO_START("FolderCacheFileSystemOverride", 1);
	newnode = FolderCacheFileSystemOverride(fc, path);
	PERFINFO_AUTO_STOP();
	if (newnode)
	{
		assert(newnode->virtual_location >= 0);
		newnode->file_index = -1;
	}

	// Query from hoggs
	if (FolderCacheGetMode()!=FOLDER_CACHE_MODE_FILESYSTEM_ONLY)
	{
		int i;
		HogFileIndex found_index=HOG_INVALID_INDEX;
		int found_hog_index=-1;
		HogFile *found_hog=NULL;
		for (i=0; i<PigSetGetNumPigs(); i++)
		{
			HogFile *hog = PigSetGetHogFile(i);
			HogFileIndex index;

			if (!(hogGetCreateFlags(hog) & HOG_NO_ACCESS_BY_NAME))
			{
				index = hogFileFind(hog, path);
				if (index != HOG_INVALID_INDEX)
				{
					found_index = index;
					found_hog = hog;
					found_hog_index = i;
					break; // Skip lower priority (higher index) hoggs
				}
			}
		}
		if (found_hog)
		{
			if (newnode)
			{
				if (newnode->timestamp == (__time32_t)hogFileGetFileTimestamp(found_hog, found_index) &&
					newnode->size == hogFileGetFileSize(found_hog, found_index))
				{
					newnode->virtual_location = PIG_INDEX_TO_VIRTUAL_LOCATION(found_hog_index);
					newnode->file_index = found_index;
				}
			} else {
				// Doesn't exist on disk
				if (FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT || FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC)
				{
					// should not exist in cache
				} else if (FolderCacheGetMode()==FOLDER_CACHE_MODE_I_LIKE_PIGS) {
					// exists in hogg, use it
					newnode = &dummy_node;
					ZeroStruct(newnode);
					newnode->name = path;
					newnode->size = hogFileGetFileSize(found_hog, found_index);
					newnode->timestamp = hogFileGetFileTimestamp(found_hog, found_index);
					newnode->virtual_location = PIG_INDEX_TO_VIRTUAL_LOCATION(found_hog_index);
					newnode->file_index = found_index;
				}
			}
		}
	}
	
	// Remove old version
	FolderNodeEnterCriticalSection();
	EnterCriticalSection(&fc->critSecAllFiles);
	if ( !stashFindPointer(fc->htAllFiles, path, &oldnode))
	{
		oldnode = NULL;
	}
	LeaveCriticalSection(&fc->critSecAllFiles);

	if (oldnode && newnode) {
#define DIFF(field) oldnode->field != newnode->field
		if (DIFF(virtual_location) || DIFF(size) || DIFF(timestamp) || DIFF(writeable) || DIFF(hidden) || DIFF(system))
			bChanged = true;
#undef DIFF
	} else {
		bChanged = true;
	}
	if (bChanged && oldnode)
		FolderNodeDeleteFromTree(fc, oldnode); // Free the old node
	FolderNodeLeaveCriticalSection();

	if (newnode && bChanged) {
		handleUpdate(fc, newnode->virtual_location, newnode->file_index, newnode->name, (U32)newnode->size, newnode->timestamp,
			(newnode->writeable?0:_A_RDONLY) |
			(newnode->hidden?_A_HIDDEN:0) |
			(newnode->system?_A_SYSTEM:0),
			0, 1);
	}
}

int folder_cache_query_debug_location;
FolderNode *FolderCacheQueryEx(FolderCache *fc, const char *relpath, bool do_hog_reload, bool grab_node_critical_section)
{
	char path[CRYPTIC_MAX_PATH];
	FolderNode *node;
	int do_not_trust_piggs=0; // If we're in a callback that may have written to disk, we can't trust the piggs
	
	// Build a hashtable if it doesn't exist yet
	if (fc && !fc->htAllFiles) {
		FolderCacheBuildHashTable(fc, fc->root, "");
	}

	PERFINFO_AUTO_START("FolderCacheUpdate", 1);
		do_not_trust_piggs = FolderCacheUpdate(do_hog_reload);
	PERFINFO_AUTO_STOP();

	if (grab_node_critical_section)
		FolderNodeEnterCriticalSection(); // Has to be after FolderCacheUpdate() if do_hog_reload is true
	
	folder_cache_query_debug_location = 0;

	if (do_not_trust_piggs)
	{
		folder_cache_query_debug_location |= 1<<11;
		fileDiskAccessCheck();
	}

	if (fc==NULL || relpath==NULL)
		return NULL;

	assert(*(U32*)relpath!=0xCCCCCCCC);

	// purdy up relpath
	if(fileIsNameSpacePath(relpath) && strchr(relpath, ':')) {
		char abspath[MAX_PATH];
		folder_cache_query_debug_location |= 1<<0;
        if(!fileLocateRead(relpath, abspath))
			return NULL;
		fileRelativePath(abspath, path);
    }
    else
    {
        strcpy(path, relpath);
    }
    forwardSlashes(path);
    fixDoubleSlashes(path);


	while (path[0]=='/') {
		strcpy(path, path+1);
	}
	if (FolderCacheGetMode() == FOLDER_CACHE_MODE_FILESYSTEM_ONLY) {
		PERFINFO_AUTO_START("FolderCacheFileSystemOverride", 1);
			node = FolderCacheFileSystemOverride(fc, path);
		PERFINFO_AUTO_STOP();
		folder_cache_query_debug_location |= 1<<1;

		return node;
	}
	assert(relpath[1]!=':');
	EnterCriticalSection(&fc->critSecAllFiles);
	if ( !stashFindPointer(fc->htAllFiles, path, &node))
	{
		folder_cache_query_debug_location |= 1<<2;
		node = NULL;
	}
	LeaveCriticalSection(&fc->critSecAllFiles);

	if (FolderCacheGetMode() == FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC) {
		folder_cache_query_debug_location |= 1<<3;
		// If we're in dynamic mode, if it's not in the hashtable, it may exist somewhere on the disk...
		if (!node) {
			folder_cache_query_debug_location |= 1<<4;
			PERFINFO_AUTO_START("FolderCacheFileSystemOverride", 1);
				node = FolderCacheFileSystemOverride(fc, path);
			PERFINFO_AUTO_STOP();
			
			if (node) {
				const char* pcPath = allocAddFilename(path);
				folder_cache_query_debug_location |= 1<<5;
				EnterCriticalSection(&fc->critSecAllFiles);
				FolderCacheHashTableAdd(fc, pcPath, node);
				LeaveCriticalSection(&fc->critSecAllFiles);
			}
			return node;
		}
		if (node->virtual_location>=0 && !do_not_trust_piggs) {
			// It's in the HT, and it's marked as being from the filesystem, we must have queried this file once before
			folder_cache_query_debug_location |= 1<<6;
			return node;
		} else {
			FolderNode *node2;
			// It's at least in a .pig file, check the filesystem
			if (node->seen_in_fs && (!do_not_trust_piggs || node->non_mirrored_file || node->needs_patching)) {
				folder_cache_query_debug_location |= 1<<7;
				return node;
			}
			
			PERFINFO_AUTO_START("FolderCacheFileSystemOverride", 1);
				node2 = FolderCacheFileSystemOverride(fc, path);
			PERFINFO_AUTO_STOP();
			
			if (!node2) {
				folder_cache_query_debug_location |= 1<<8;
				return NULL;
			}
			// In FS and Pigg, return the FS if it's different
#if PLATFORM_CONSOLE
			// Xbox filesystem has 2second granularity?
			if (ABS_UNS_DIFF(node->timestamp, node2->timestamp)>2 && ABS_UNS_DIFF(node->timestamp, node2->timestamp)!=3600)
#else
			if (node->timestamp!=node2->timestamp && ABS_UNS_DIFF(node->timestamp, node2->timestamp)!=3600)
#endif
			{
				const char* pcPath = allocAddFilename(path);
				folder_cache_query_debug_location |= 1<<9;
				EnterCriticalSection(&fc->critSecAllFiles);
				FolderCacheHashTableAdd(fc, pcPath, node2);
				LeaveCriticalSection(&fc->critSecAllFiles);
				return node2;
			} else {
				// Timestamps are the same
				node->seen_in_fs = 1;
				node->seen_in_fs_virtual_location = node2->seen_in_fs_virtual_location;
			}
			return node;
		}
	}
	folder_cache_query_debug_location |= 1<<10;
	return node;
}

void FolderCacheEnableCallbacks(int enable) { // Disables/enables checking for changes in the filesystem (turn these off when loading)
	folder_cache_update_enable = enable;
}

void FolderCacheChooseMode(void) { // Auto-chooses the appropriate mode, based on existence of ./piggs
	// This may also go off if you call this function twice/out of order
	assertmsg(!folder_cache, "File access happened before FolderCacheChooseMode() (perhaps in an AUTO_RUN?)!  Not allowed, and will not work in production.");
	if (fileIsUsingDevData() && !g_xbox_local_hoggs_hack) {
		// This mode reads data from a pig file, only if it's identical to the one on the filesystem
		FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);
		if (pig_debug)
			OutputDebugString(L"Pig mode: development\n");
	} else {
#if _PS3
        // ignore filesystem
        FolderCacheSetMode(FOLDER_CACHE_MODE_PIGS_ONLY);
#else
		// This mode reads all data from pigs if available, but still allows filesystem overrides
		FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);
#endif
		if (pig_debug)
			OutputDebugString(L"Pig mode: semi-production\n");
	}
	gbFolderCacheModeChosen = true;
}

void FolderCacheChooseModeNoPigsInDevelopment(void) { // Auto-chooses the appropriate mode, based on existence of ./piggs
	// This may also go off if you call this function twice/out of order
	assertmsg(!folder_cache, "File access happened before FolderCacheChooseMode() (perhaps in an AUTO_RUN?)!  Not allowed, and will not work in production.");
	if (fileIsUsingDevData()) {
		if (isDevelopmentMode()) {
			// This mode reads data from a pig file, only if it's identical to the one on the filesystem
			FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY);
			if (pig_debug)
				OutputDebugString(L"Pig mode: FS only (development)\n");
		} else {
			// Running with -productionmode
			FolderCacheSetMode(FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC);
			if (pig_debug)
				OutputDebugString(L"Pig mode: development\n");
		}
	} else {
		// This mode reads all data from pigs if available, but still allows filesystem overrides
		FolderCacheSetMode(FOLDER_CACHE_MODE_I_LIKE_PIGS);
		if (pig_debug)
			OutputDebugString(L"Pig mode: semi-production\n");
	}

	gbFolderCacheModeChosen = true;
}


static FolderCacheMode folder_cache_mode = FOLDER_CACHE_MODE_FILESYSTEM_ONLY; // Disabled by default
// Per-thread override stack

#define FCM_MAX_OVERRIDES 8
static struct {
	DWORD tid;
	FolderCacheMode override;
} fcm_overrides[FCM_MAX_OVERRIDES];

static int fcm_override_count=0;
static CRITICAL_SECTION fcmo_critsec;
static bool fcmo_critsec_inited=false;

void FolderCacheSetMode(FolderCacheMode mode)
{
	FolderCacheMode oldmode = folder_cache_mode;
	if (!fcmo_critsec_inited) {
		InitializeCriticalSection(&fcmo_critsec);
		fcmo_critsec_inited = true;
	}
	folder_cache_mode = mode;
	if (oldmode == FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC &&
		(mode == FOLDER_CACHE_MODE_DEVELOPMENT ||
		mode == FOLDER_CACHE_MODE_I_LIKE_PIGS) &&
		eaSize(&folder_cache->gamedatadirs))
	{
		int i;
		// Moving away from DevDynamic to something that needs the disk scanned
		for (i=0; i<eaSize(&folder_cache->gamedatadirs); i++) {
			FolderCacheAddFolderToNode(folder_cache, &folder_cache->root, &folder_cache->root_tail, NULL, folder_cache->gamedatadirs[i], i, 0);
		}

		FolderCachePruneCoreExclusions(folder_cache, NULL);
		FolderCacheBuildHashTable(folder_cache, folder_cache->root, "");
	}

	gbFolderCacheModeChosen = true;
}

FolderCacheMode FolderCacheGetMode(void)
{
	if (fcm_override_count)
	{
		int i;
		EnterCriticalSection(&fcmo_critsec);
		for (i=fcm_override_count-1; i>=0; i--) {
			assert(i < FCM_MAX_OVERRIDES);
			if (fcm_overrides[i].tid == GetCurrentThreadId()) {
				LeaveCriticalSection(&fcmo_critsec);
				return fcm_overrides[i].override;
			}
		}
		LeaveCriticalSection(&fcmo_critsec);
	}
	return folder_cache_mode;
}

void FolderCacheModePush(FolderCacheMode newmode) // Another hack for dynamic mode
{
	if (folder_cache_mode==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC)
	{
		int index;
		EnterCriticalSection(&fcmo_critsec);
		index = fcm_override_count++;
		assertmsg(index<ARRAY_SIZE(fcm_overrides), "FolderCache stack overflow");
		fcm_overrides[index].tid = GetCurrentThreadId();
		fcm_overrides[index].override = newmode; // FOLDER_CACHE_MODE_DEVELOPMENT;
		LeaveCriticalSection(&fcmo_critsec);
	}
}

void FolderCacheModePop(void) // Another hack for dynamic mode
{
	if (folder_cache_mode==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC)
	{
		int i, j;
		EnterCriticalSection(&fcmo_critsec);
		for (i=fcm_override_count-1; i>=0; i--) {
			if (fcm_overrides[i].tid == GetCurrentThreadId()) {
				for (j=i; j<fcm_override_count-1; j++) {
					fcm_overrides[j].tid = fcm_overrides[j+1].tid;
					fcm_overrides[j].override = fcm_overrides[j+1].override;
				}
				fcm_override_count--;
				LeaveCriticalSection(&fcmo_critsec);
				return;
			}
		}
		assertmsg(0, "FolderCache Stack underflow");
		LeaveCriticalSection(&fcmo_critsec);
	}
}

char *FolderCacheGetRealPath(FolderCache *fc, FolderNode *node, char *buffer, size_t buffer_size)
{
	char temp[CRYPTIC_MAX_PATH];

	if (!node)
		return NULL;

	assert(buffer);

	strcpy_s(buffer, buffer_size, FolderCache_GetFromVirtualLocation(fc, node->virtual_location));
	if (buffer[strlen(buffer)-1]=='/') {
		buffer[strlen(buffer)-1]=0;
	}
	strcat_s(buffer, buffer_size, FolderNodeGetFullPath(node, temp));
	return buffer;
}

char *FolderCacheGetOnDiskPath(FolderCache *fc, FolderNode *node, char *buffer, size_t buffer_size) // Returns the on-disk path if this file has been seen on disk
{
	char temp[CRYPTIC_MAX_PATH];

	if (!node)
		return NULL;
	if (!node->seen_in_fs)
		return NULL;

	assert(buffer);

	strcpy_s(buffer, buffer_size, FolderCache_GetFromVirtualLocation(fc, node->seen_in_fs_virtual_location));
	if (buffer[strlen(buffer)-1]=='/') {
		buffer[strlen(buffer)-1]=0;
	}
	strcat_s(buffer, buffer_size, FolderNodeGetFullPath(node, temp));
	return buffer;
}

int FolderNodePigIndex(FolderNode *node) {
	if (node->virtual_location >=0) {
		return -1;
	} else {
		return -node->virtual_location - 1;
	}
}

void FolderCacheRequestTree(FolderCache *fc, const char *relpath) { // If we're in dynamic mode, this will load the tree!
	static StashTable ht_added_trees=0;
	char path[CRYPTIC_MAX_PATH], basepath[CRYPTIC_MAX_PATH];
	int i, num_dirs;
	FolderNode *node;

	if (FolderCacheGetMode() != FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC &&
		FolderCacheGetMode() != FOLDER_CACHE_MODE_FILESYSTEM_ONLY)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	if (ht_added_trees==0) {
		ht_added_trees = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	}

	while (relpath[0]=='/') relpath++;
	strcpy(path, relpath);
	relpath = path;

	// Check if already did filesystem scan on the specified tree
	if (stashFindInt(ht_added_trees, relpath, NULL)) {
		PERFINFO_AUTO_STOP();
		return;
	}
	stashAddInt(ht_added_trees, relpath, 1, true);

	// (FIND_DATA_DIRS_TO_CACHE) 
	// Uncomment the printfColor line below to see output any time the folder cache
	// encounters a miss and needs to load from filewatcher for a scan.  This can be
	// used to detect folders that should be included in pre-cached folder lists
	//printfColor(COLOR_BRIGHT|COLOR_RED, "FOLDER CACHE LOADING '%s'\n", relpath);
	
	FolderNodeEnterCriticalSection();
	node = FolderNodeFind(fc->root, path);
	if (!node) {
		int created;
		
		if (strcmp(path, "")==0)
		{
			// The root, we don't support requesting it
			FolderNodeLeaveCriticalSection();
			PERFINFO_AUTO_STOP();
			return;
		}

		// The node isn't there, add it!
		node = FolderNodeAdd(&fc->root, &fc->root_tail, NULL, path, 0, 0, 0, -1, &created, 0, 0);
		
		if(node){
			node->is_dir = 1;
			
			if(created && fc->nodeCreateCallback){
				fc->nodeCreateCallback(fc, node);
			}
		}
	}

	num_dirs = FolderCache_GetNumGDDs(fc);
	// Scan backwards so that if a high-priority matches a pig, it is flagged as such
	for (i=num_dirs-1; i>=0; i--)
	{
		char *getpath;
		if (getpath = FolderCache_GetFromVirtualLocation(fc, i))
		{
			assert(node);
			STR_COMBINE_SSS(basepath, getpath, "/", path);
			FolderCacheAddFolderToNode(fc, &node->contents, &node->contents_tail, node, basepath, i, 0);
		}
	}

	if (node)
		FolderCachePruneNonExistent(fc, &node->contents, &node->contents_tail, node);

	FolderCachePruneCoreExclusions(fc, relpath);

	if (fc->htAllFiles) {
		assert(node);
		FolderCacheBuildHashTable(fc, node->contents, path);
	}
	FolderNodeLeaveCriticalSection();
	PERFINFO_AUTO_STOP();
}


int FolderCache_GetNumGDDs(FolderCache *fc)
{
	return eaSize(&fc->gamedatadirs);
}

void FolderCache_PrintGDDs(FolderCache *fc)
{
	int i;
	printf("  GameDataDirs:\n");
	for (i = 0; i < eaSize(&fc->gamedatadirs); ++i) {
		printf("   %c%s\n", (stricmp(fc->gamedatadirs[i], fileDataDir())==0)?'*':' ', fc->gamedatadirs[i]);
	}
}

int FolderCache_GetNumPiggs(FolderCache *fc)
{
	return eaSize(&fc->piggs);
}

char *FolderCache_GetFromVirtualLocation(FolderCache *fc, int virtual_location)
{
	char *retval;
	PERFINFO_AUTO_START_FUNC();
	if (virtual_location < 0)
	{
		// pigg
		virtual_location = VIRTUAL_LOCATION_TO_PIG_INDEX(virtual_location);
		retval = (char *)eaGet(&fc->piggs, virtual_location);
		PERFINFO_AUTO_STOP();
		return retval;
	}

	// game data dir
	retval = (char *)eaGet(&fc->gamedatadirs, virtual_location);
	PERFINFO_AUTO_STOP();
	return retval;
}

void FolderCache_SetFromVirtualLocation(FolderCache *fc, int virtual_location, char *path, char *path_name, bool is_core)
{
	assert(INRANGE(VIRTUAL_LOCATION_TO_LOOKUP_INDEX(virtual_location), 0, ARRAY_SIZE(fc->is_core)));
	if (virtual_location < 0)
	{
		// pigg
		int pig_index = VIRTUAL_LOCATION_TO_PIG_INDEX(virtual_location);
		if (pig_index >= eaSize(&fc->piggs))
		{
			eaSetSize(&fc->piggs, pig_index + 1);
			eaSetSize(&fc->pigg_names, pig_index + 1);
		}
		eaSet(&fc->piggs, path, pig_index);
		eaSet(&fc->pigg_names, path_name, pig_index);
	}
	else
	{
		// game data dir
		if (virtual_location >= eaSize(&fc->gamedatadirs))
		{
			eaSetSize(&fc->gamedatadirs, virtual_location + 1);
			eaSetSize(&fc->gamedatadir_names, virtual_location + 1);
		}
		eaSet(&fc->gamedatadirs, path, virtual_location);
		eaSet(&fc->gamedatadir_names, path_name, virtual_location);
	}
	fc->is_core[VIRTUAL_LOCATION_TO_LOOKUP_INDEX(virtual_location)] = is_core;
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void ForceReloadsByTouchingFiles_All(void)
{
	FolderCacheCallbackInfo *pInfo = fccallbacks;

	while (pInfo)
	{
		//skip trivial strings like "*"
		if (strlen(pInfo->filespec) > 2 )
		{
			char reloadPath[CRYPTIC_MAX_PATH];
			char *pFirstAsterisk;


			printf("About to force reloading of %s\n", pInfo->filespec);

			strcpy(reloadPath, pInfo->filespec);
			pFirstAsterisk = strchr(reloadPath, '*');

			if (pFirstAsterisk)
			{
				char **ppFilesToReload = NULL;
				int i;

				*pFirstAsterisk = 0;
				pFirstAsterisk++;

				FindNLargestFilesInDirectory(reloadPath, pFirstAsterisk, 3, &ppFilesToReload);

				for (i=0; i < eaSize(&ppFilesToReload); i++)
				{
					TouchFile(ppFilesToReload[i]);
				}

				eaDestroy(&ppFilesToReload);
			}
			else
			{
				TouchFile(reloadPath);
			}			
		}

		pInfo = pInfo->next;
	}
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void ForceReloadsByTouchingFiles_OneByOne(void)
{
	static int iNextCount = 0;
	int iCounter = -1;
	FolderCacheCallbackInfo *pInfo = fccallbacks;

	while (pInfo)
	{
		//skip trivial strings like "*"
		if (strlen(pInfo->filespec) > 2	)
		{
			char reloadPath[CRYPTIC_MAX_PATH];
			char *pFirstAsterisk;

			iCounter++;

			if (iNextCount == iCounter)
			{
				iNextCount++;

				printf("About to force reloading of %s\n", pInfo->filespec);

				strcpy(reloadPath, pInfo->filespec);
				pFirstAsterisk = strchr(reloadPath, '*');

				if (pFirstAsterisk)
				{
					char **ppFilesToReload = NULL;
					int i;

					*pFirstAsterisk = 0;
					pFirstAsterisk++;

					FindNLargestFilesInDirectory(reloadPath, pFirstAsterisk, 3, &ppFilesToReload);

					for (i=0; i < eaSize(&ppFilesToReload); i++)
					{
						TouchFile(ppFilesToReload[i]);
					}

					eaDestroy(&ppFilesToReload);
				}
				else
				{
					TouchFile(reloadPath);
				}

				return;
			}
		}

		pInfo = pInfo->next;
	}
}

static DWORD WINAPI FolderCacheMonitorThread( LPVOID lpParam )
{
	U32 sleep_time = 128;
	EXCEPTION_HANDLER_BEGIN
		for(;;)
		{
			autoTimerThreadFrameBegin(__FUNCTION__);
			FolderCacheUpdate(true);
			if (FolderCacheLastUpdateHadChange()) {
				sleep_time = 16;
			} else {
				sleep_time = CLAMP(2*sleep_time, 16, 128);
			}
			Sleep(sleep_time);
			autoTimerThreadFrameEnd();
		}
		return 0; 
	EXCEPTION_HANDLER_END
} 

static bool disable_folder_cache_monitor_thread=false;
// Disables running a thread for folder cache updates
AUTO_CMD_INT(disable_folder_cache_monitor_thread, disable_folder_cache_monitor_thread) ACMD_CATEGORY(Debug);

void FolderCacheStartMonitorThread(void)
{
#if !PLATFORM_CONSOLE // No folder cache monitoring on consoles
	static bool doneonce=false;
	ManagedThread *thread;
	assert(!doneonce);
	doneonce = true;
	if (disable_folder_cache_monitor_thread)
		return;
	thread = tmCreateThreadEx(FolderCacheMonitorThread, NULL, 0, THREADINDEX_DATASTREAMING);
	assert(thread);
	tmSetThreadRelativePriority(thread, 1);
#endif
}

size_t FolderCacheGetMemoryUsage(FolderCache *cache)
{
	size_t size = sizeof(FolderCache);
	int i;

	if(cache->gamedatadirs)
	{
		size += eaMemUsage(&cache->gamedatadirs, true);

		for(i = 0; i < eaSize(&cache->gamedatadirs); ++i)
		{
			if(cache->gamedatadirs[i])
			{
				size += strlen(cache->gamedatadirs[i])+1;
			}
		}
	}

	if(cache->gamedatadir_names)
	{
		size += eaMemUsage(&cache->gamedatadir_names, true);

		for(i = 0; i < eaSize(&cache->gamedatadir_names); ++i)
		{
			if(cache->gamedatadir_names[i])
			{
				size += strlen(cache->gamedatadir_names[i])+1;
			}
		}
	}

	if(cache->piggs)
	{
		size += eaMemUsage(&cache->piggs, true);

		for(i = 0; i < eaSize(&cache->piggs); ++i)
		{
			if(cache->piggs[i])
			{
				size += strlen(cache->piggs[i])+1;
			}
		}
	}

	if(cache->pigg_names)
	{
		size += eaMemUsage(&cache->pigg_names, true);

		for(i = 0; i < eaSize(&cache->pigg_names); ++i)
		{
			if(cache->pigg_names[i])
			{
				size += strlen(cache->pigg_names[i])+1;
			}
		}
	}

	if(cache->htAllFiles)
	{
		StashTableIterator iter = {0};
		StashElement elem = NULL;
		FolderNode *node;

		size += stashGetMemoryUsage(cache->htAllFiles);
		stashGetIterator(cache->htAllFiles, &iter);

		while(stashGetNextElement(&iter, &elem))
		{
			node = stashElementGetPointer(elem);
			size += FolderNodeGetMemoryUsage(node);
		}
	}

	return size;
}

void FolderCacheMoveToShared(FolderCache *cache, SharedMemoryHandle *sm_handle)
{
	FolderCache *sm_cache;
	int i;

	sm_cache = sharedMemoryAlloc(sm_handle, sizeof(FolderCache));

	{
		StashTableIterator iter = {0};
		StashElement elem = NULL;
		FolderNode *node;
		FolderNode *node_new;
		bool is_root, is_root_tail;

		stashGetIterator(cache->htAllFiles, &iter);

		while(stashGetNextElement(&iter, &elem))
		{
			is_root = false;
			is_root_tail = false;

			node = stashElementGetPointer(elem);
			if(node == cache->root) is_root = true;
			if(node == cache->root_tail) is_root_tail = true;

			node_new = FolderNodeMoveToShared(node, sm_handle);
			if(is_root) cache->root = node_new;
			if(is_root_tail) cache->root_tail = node_new;

			stashElementSetPointer(elem, node_new);
		}

		FolderNodeDestroyPool();
	}

	memcpy(sm_cache, cache, sizeof(FolderCache));
	ZeroStruct(&sm_cache->critSecAllFiles);
	ZeroStruct(&sm_cache->patchCallback);
	ZeroStruct(&sm_cache->nodeCreateCallback);
	ZeroStruct(&sm_cache->nodeDeleteCallback);
	ZeroStruct(&sm_cache->core_exclusions);

	if(cache->htAllFiles)
	{
		sm_cache->htAllFiles = stashTableClone(cache->htAllFiles, sharedMemoryAlloc, sm_handle);
		stashTableDestroy(cache->htAllFiles);
		cache->htAllFiles = calloc(1, stashGetTableImpSize());
		memcpy(cache->htAllFiles, sm_cache->htAllFiles, stashGetTableImpSize());
	}

	if(cache->gamedatadirs)
	{
		for(i=0; i<eaSize(&cache->gamedatadirs); ++i)
		{
			char *str = sharedMemoryAlloc(sm_handle, strlen(cache->gamedatadirs[i])+1);
			memcpy(str, cache->gamedatadirs[i], strlen(cache->gamedatadirs[i])+1);
			free(cache->gamedatadirs[i]);
			cache->gamedatadirs[i] = str;
		}

		eaCompress(&sm_cache->gamedatadirs, &cache->gamedatadirs, sharedMemoryAlloc, sm_handle);
		eaDestroy(&cache->gamedatadirs);
		cache->gamedatadirs = sm_cache->gamedatadirs;
	}

	if(cache->gamedatadir_names)
	{
		for(i=0; i<eaSize(&cache->gamedatadir_names); ++i)
		{
			if(cache->gamedatadir_names[i])
			{
				char *str = sharedMemoryAlloc(sm_handle, strlen(cache->gamedatadir_names[i])+1);
				memcpy(str, cache->gamedatadir_names[i], strlen(cache->gamedatadir_names[i])+1);
				free(cache->gamedatadir_names[i]);
				cache->gamedatadir_names[i] = str;
			}
		}

		eaCompress(&sm_cache->gamedatadir_names, &cache->gamedatadir_names, sharedMemoryAlloc, sm_handle);
		eaDestroy(&cache->gamedatadir_names);
		cache->gamedatadir_names = sm_cache->gamedatadir_names;
	}

	if(cache->piggs)
	{
		for(i=0; i<eaSize(&cache->piggs); ++i)
		{
			char *str = sharedMemoryAlloc(sm_handle, strlen(cache->piggs[i])+1);
			memcpy(str, cache->piggs[i], strlen(cache->piggs[i])+1);
			free(cache->piggs[i]);
			cache->piggs[i] = str;
		}

		eaCompress(&sm_cache->piggs, &cache->piggs, sharedMemoryAlloc, sm_handle);
		eaDestroy(&cache->piggs);
		cache->piggs = sm_cache->piggs;
	}

	if(cache->pigg_names)
	{
		for(i=0; i<eaSize(&cache->pigg_names); ++i)
		{
			if(cache->pigg_names[i])
			{
#pragma warning(suppress:6001) // /analzye "Using uninitialized memory '*sharedMemoryAlloc(sm_handle, (strlen(cache->piggs[i]))+1)'"
				char *str = sharedMemoryAlloc(sm_handle, strlen(cache->pigg_names[i])+1);
				memcpy(str, cache->pigg_names[i], strlen(cache->pigg_names[i])+1);
				free(cache->pigg_names[i]);
				cache->pigg_names[i] = str;
			}
		}
		
		eaCompress(&sm_cache->pigg_names, &cache->pigg_names, sharedMemoryAlloc, sm_handle);
		eaDestroy(&cache->pigg_names);
		cache->pigg_names = sm_cache->pigg_names;
	}
}


static StashTable fca_root_data;
static StashTable fca_onelevel_data;
static StashTable fca_ext_data;
static FileScanAction FCASub(const char *dir, FolderNode *node, void *pUserData_Proc, void *pUserData_Data)
{
	char fullpath[MAX_PATH];
	char root[MAX_PATH];
	char onelevel[MAX_PATH];
	char *s;
	StashElement elem;
	sprintf(fullpath, "%s/%s", dir, node->name);
	strcpy(root, fullpath);
	s = strchr(root, '/');
	if (s) {
		*s=0;
		if (stashFindElement(fca_root_data, root, &elem))
			stashElementSetInt(elem, stashElementGetInt(elem)+1);
		else
			stashAddInt(fca_root_data, root, 1, false);
	}
	strcpy(onelevel, fullpath);
	s = strchr(onelevel, '/');
	if(s)
	{
		char *s2 = strchr(s+1, '/');
		if (s2)
			*s2=0;
		else
			*s=0;
		if (stashFindElement(fca_onelevel_data, onelevel, &elem))
			stashElementSetInt(elem, stashElementGetInt(elem)+1);
		else
			stashAddInt(fca_onelevel_data, onelevel, 1, false);
	}
	s = strrchr(fullpath, '.');
	if (s)
	{
		if (stashFindElement(fca_ext_data, s, &elem))
			stashElementSetInt(elem, stashElementGetInt(elem)+1);
		else
			stashAddInt(fca_ext_data, s, 1, false);
	}
	return FSA_EXPLORE_DIRECTORY;
}

static int cmpStashElemStringKey(const void **a, const void **b)
{
	StashElement *sa = (StashElement*)a;
	StashElement *sb = (StashElement*)b;
	return stricmp(stashElementGetStringKey(*sa), stashElementGetStringKey(*sb));
}

static int cmpStashElemIntValue(const void **_a, const void **_b)
{
	StashElement *a = (StashElement*)_a;
	StashElement *b = (StashElement*)_b;
	return stashElementGetInt(*b) - stashElementGetInt(*a);
}

// Analyzes the folder cache
AUTO_COMMAND;
void FolderCacheAnalyze(void)
{
	StashElement *root_elems=NULL;
	StashElement *onelevel_elems=NULL;
	StashElement *ext_elems=NULL;
	int i, j;
	fca_root_data = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	fca_onelevel_data = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	fca_ext_data = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
	FolderNodeRecurseEx(folder_cache->root, FCASub, NULL, NULL, "", false, false);
	FOR_EACH_IN_STASHTABLE2(fca_root_data, elem)
	{
		eaPush(&root_elems, elem);
	}
	FOR_EACH_END;
	FOR_EACH_IN_STASHTABLE2(fca_onelevel_data, elem)
	{
		eaPush(&onelevel_elems, elem);
	}
	FOR_EACH_END;
	FOR_EACH_IN_STASHTABLE2(fca_ext_data, elem)
	{
		eaPush(&ext_elems, elem);
	}
	FOR_EACH_END;
	eaQSort(root_elems, cmpStashElemIntValue);
	eaQSort(onelevel_elems, cmpStashElemIntValue);
	eaQSort(ext_elems, cmpStashElemIntValue);
	printf("FOLDER CACHE ANALYZE\n");
	for (i=0; i<eaSize(&root_elems); i++)
	{
		StashElement elem = root_elems[i];
		if (stashElementGetInt(elem)<=2)
			continue;
		printf("%6d %s\n", stashElementGetInt(elem), stashElementGetStringKey(elem));
		for (j=0; j<eaSize(&onelevel_elems); j++)
		{
			StashElement subelem = onelevel_elems[j];
			if (strStartsWith(stashElementGetStringKey(subelem), stashElementGetStringKey(elem)))
			{
				if (stashElementGetInt(subelem)<=2)
					continue;
				printf("\t%6d %s\n", stashElementGetInt(subelem), stashElementGetStringKey(subelem));
			}
		}
	}
	printf("\nBY EXT\n");
	for (i=0; i<eaSize(&ext_elems); i++)
	{
		StashElement elem = ext_elems[i];
		if (stashElementGetInt(elem)<=2)
			continue;
		printf("%6d %s\n", stashElementGetInt(elem), stashElementGetStringKey(elem));
	}
}

static char *fcgcm_buf;
static char *fcgcm_bufend;

static void fcgcm_pt(int d)
{
	int i;
	for (i=0; i<d; i++) {
		*fcgcm_bufend++ = ' ';
		*fcgcm_bufend++ = ' ';
	}
	*fcgcm_bufend = 0;
}
static void fcgcm_puts(char *s)
{
	strcpy_unsafe(fcgcm_bufend, s);
	fcgcm_bufend += strlen(fcgcm_bufend);
}

static int FolderCacheGenCoreMappingCallback(FolderNode *node, int depth)
{
	FolderNode *n;
	int ret=0;
	char *saved = fcgcm_bufend;
	fcgcm_pt(depth);
	fcgcm_puts("Folder ");
	fcgcm_puts(node->name);
	fcgcm_puts("\n");
	n = node->contents;
	while (n)
	{
		if (n->is_dir)
			ret |= FolderCacheGenCoreMappingCallback(n, depth+1);
		else if (folder_cache->is_core[VIRTUAL_LOCATION_TO_LOOKUP_INDEX(node->virtual_location)])
		{
// 			if (strstri(n->name, "STO_"))
// 				fcgcm_puts("#");
			fcgcm_pt(depth+1);
			fcgcm_puts("Incl ");
			fcgcm_puts(n->name);
			fcgcm_puts("\n");
			ret = 1;
		}
		n = n->next;
	}
	fcgcm_pt(depth);
	fcgcm_puts("EndFolder\n");
	if (!ret) {
		fcgcm_bufend = saved;
		*fcgcm_bufend = 0;
	}
	return ret;
}

// Prints to the console a mapping to be placed into CoreExclude.txt for a given folder
AUTO_COMMAND;
void FolderCacheGenCoreMapping(const char *rootfolder)
{
	FolderNode *node = folder_cache->root;
	fcgcm_bufend = fcgcm_buf = malloc(5*1024*1024);

	while (node)
	{
		if (strStartsWith(node->name, "rootfolder"))
			FolderCacheGenCoreMappingCallback(node, 1);
		node = node->next;
	}
	printf("%s", fcgcm_buf);
	free(fcgcm_buf);
}

void FolderCacheReleaseHogHeaderData(void)
{
	int i;
	for (i=0; i<PigSetGetNumPigs(); i++)  {
		hogReleaseHeaderData(PigSetGetHogFile(i));
	}
}

static void FolderCacheForceUpdateCallbacksForFileCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	char *pFileName = (char*)userData;
	checkForCallbacks(folder_cache, NULL, 0, FOLDER_CACHE_CALLBACK_UPDATE, pFileName);
	free(pFileName);
}


void FolderCacheForceUpdateCallbacksForFile(const char *pFileName)
{
	char path[CRYPTIC_MAX_PATH];

	strcpy(path, pFileName);

	forwardSlashes(path);
	fixDoubleSlashes(path);

	//always do this one second in the future just so we don't have to worry about whether people set up
	//the reload callbacks before they do the loading, etc.
	TimedCallback_Run(FolderCacheForceUpdateCallbacksForFileCB, strdup(path), 1.0f);
}