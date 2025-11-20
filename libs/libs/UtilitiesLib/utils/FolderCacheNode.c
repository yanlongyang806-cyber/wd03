/***************************************************************************



* 
***************************************************************************/
#include "FolderCache.h"
#include <string.h>

#include "genericlist.h"
#include "MemoryPool.h"
#include "StringCache.h"
#include "timing.h"
#include "utils.h"
#include "timing_profiler.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

extern int folder_cache_debug;

static CRITICAL_SECTION folder_node_crit_sect;
static int g_debug_is_in_critical_section; // Will give false positives, only for debug asserts

MP_DEFINE(FolderNode);

static int discard_count=0;
static int update_count=0;
void FolderNodeGetCounts(int *discard, int *update) {
	if (discard) *discard = discard_count;
	if (update) *update = update_count;
	discard_count = 0;
	update_count = 0;
}

AUTO_RUN;
void initFolderNodeCriticalSection(void)
{
	InitializeCriticalSection(&folder_node_crit_sect);
}

static struct {
	const char *filename;
	int linenum;
	int is_in_value;
} fnecshist[16];
int fnecshist_index=0;

void FolderNodeEnterCriticalSection_dbg(const char *filename, int linenum)
{
	assert(g_debug_is_in_critical_section>=0);
	EnterCriticalSection(&folder_node_crit_sect);
	fnecshist[fnecshist_index].filename = filename;
	fnecshist[fnecshist_index].linenum = linenum;
	fnecshist[fnecshist_index].is_in_value = g_debug_is_in_critical_section;
	fnecshist_index++;
	fnecshist_index%=ARRAY_SIZE(fnecshist);
	g_debug_is_in_critical_section++;
	FolderCacheFlushHogChangeCallbacks();
}

bool FolderNodeTryEnterCriticalSection_dbg(const char *filename, int linenum)
{
	assert(g_debug_is_in_critical_section>=0);
	if (TryEnterCriticalSection(&folder_node_crit_sect)) {
		fnecshist[fnecshist_index].filename = filename;
		fnecshist[fnecshist_index].linenum = linenum;
		fnecshist[fnecshist_index].is_in_value = g_debug_is_in_critical_section;
		fnecshist_index++;
		fnecshist_index%=ARRAY_SIZE(fnecshist);
		g_debug_is_in_critical_section++;
		return true;
	} else {
		return false;
	}
}


void FolderNodeLeaveCriticalSection(void)
{
	assert(g_debug_is_in_critical_section);
	g_debug_is_in_critical_section--;
	LeaveCriticalSection(&folder_node_crit_sect);
}


static char *firstSlash(char *s) {
	char *c;
	for (c=s; *c; c++) {
		if (*c=='/' || *c=='\\') return c;
	}
	return NULL;
}

typedef struct FolderNodeAddCache {
	FolderNode *parent;
	FolderNode *lastnode;
} FolderNodeAddCache;

FolderNodeAddCache folder_node_add_cache[16];
struct {
	FolderNodeAddCache last_cache;
	int debug_location;
} folderNodeAddDebug;

//static int good_count,bad_count;

__forceinline static char *inline_strrchr(const char *src, int c)
{
	int i;
	for(i=(int)strlen(src)-1;i>=0;i--)
	{
		if (src[i] == c)
			return (char*)&src[i];
	}
	return 0;
}

FolderNode *FolderNodeAdd(FolderNode **head, FolderNode **tail, FolderNode *parent, const char *_fn, __time32_t timestamp, size_t size, int virtual_location, int file_index, int* createdOut, int depth, const char *pre_alloced_path)
{
	char *s;
	FolderNode *node = *head, *lastnode=NULL;
	FolderNode *node2, *node_ret=NULL;
	int is_folder=0;
	bool is_new=false;
	bool off_tail=false;
	bool needStricmp=true;
	char fn_temp[CRYPTIC_MAX_PATH], *fn;
	int stricmpres=-1;
	bool bNeedCriticalSection=false;

	PERFINFO_AUTO_START_FUNC();

	if (parent == NULL) {
		bNeedCriticalSection = true;
		FolderNodeEnterCriticalSection();
	}

	folderNodeAddDebug.debug_location = 0;

	if(createdOut){
		*createdOut = 0;
	}

	assert(tail);

	memcpy(fn_temp, _fn, strlen(_fn)+1);
	//strcpy(fn_temp, _fn);
	fn = fn_temp;

	//printf("Adding %s - %d\n", fn, timestamp);
	while (fn[0]=='/' || fn[0]=='\\') fn++;
	if (fn[0]=='.' && (fn[1]=='/' || fn[1]=='\\')) fn+=2;
	if (fn[0]==0) {
		// zero length name, we only get this when adding a folder without a file as well, the caller wants the folder's parent back
		if (bNeedCriticalSection)
			FolderNodeLeaveCriticalSection();
		PERFINFO_AUTO_STOP();
		return parent;
	}
	s = firstSlash(fn);
	if (!s) {
		is_folder=0;
	} else {
		*s=0;
		is_folder=1;
		s++;
	}
	if (tail && *tail) {
		stricmpres = inline_stricmp(fn, (*tail)->name);
		if (stricmpres>0) {
			// This new node is supposed to be right past the tail
			lastnode = *tail;
			node = NULL;
			is_new = true;
			off_tail = true;
			folderNodeAddDebug.debug_location = 1;
		} else if (stricmpres==0) {
			// This node *is* the tail
			node = *tail;
			is_new = false;
			lastnode = (void*)-1; // Not valid
			off_tail = true;
			needStricmp = false;
			folderNodeAddDebug.debug_location = 2;
		}
	}
	if (!off_tail && parent && depth < ARRAY_SIZE(folder_node_add_cache) && folder_node_add_cache[depth].parent == parent) {
		assert(folder_node_add_cache[depth].lastnode);
		stricmpres = inline_stricmp(fn, folder_node_add_cache[depth].lastnode->name);
		if (stricmpres>=0) {
			// This new node is supposed to be right past the previously used node (general case)
			node = folder_node_add_cache[depth].lastnode;
			lastnode = (void*)-1; // Not valid
			needStricmp = false;
			folderNodeAddDebug.debug_location = 3;
		}
	}
	while (node) {
		if (needStricmp)
			stricmpres = inline_stricmp(fn, node->name);
		needStricmp = true;
		if (stricmpres==0) {
			// Found the folder or file
			if (depth < ARRAY_SIZE(folder_node_add_cache)) {
				folderNodeAddDebug.last_cache = folder_node_add_cache[depth];
				folder_node_add_cache[depth].parent = parent;
				folder_node_add_cache[depth].lastnode = node;
			}
			if (!node->is_dir) {
				if (is_folder) { // this *is* a folder
					if (folder_cache_debug)
						printf("File system inconsistency:  A file in one location is named the same as a folder in another : %s\n", fn);
					// Assume the old one was actually a folder
					node->is_dir = 1;
					node = FolderNodeAdd(&node->contents, &node->contents_tail, node, s, timestamp, size, virtual_location, file_index, createdOut, depth+1, pre_alloced_path);
					if (bNeedCriticalSection)
						FolderNodeLeaveCriticalSection();
					PERFINFO_AUTO_STOP();
					return node;
				} else { // They're both files, treat this as an Update command
					folderNodeAddDebug.debug_location |= 0x10;
					break;
				}
			} else {
				if (is_folder) {
					node = FolderNodeAdd(&node->contents, &node->contents_tail, node, s, timestamp, size, virtual_location, file_index, createdOut, depth+1, pre_alloced_path);
					if (bNeedCriticalSection)
						FolderNodeLeaveCriticalSection();
					PERFINFO_AUTO_STOP();
					return node;
				} else {
					// We don't think this is a folder, but a folder exists under this name already, must be a folder
					// This call will just return node->contents back down the tree for us
					s = "";
					node = FolderNodeAdd(&node->contents, &node->contents_tail, node, s, timestamp, size, virtual_location, file_index, createdOut, depth+1, pre_alloced_path);
					if (bNeedCriticalSection)
						FolderNodeLeaveCriticalSection();
					PERFINFO_AUTO_STOP();
					return node;
				}
			}
			assert(0);
			if (bNeedCriticalSection)
				FolderNodeLeaveCriticalSection();
			PERFINFO_AUTO_STOP();
			return NULL;
		} else if (stricmpres<0) {
			// We want to insert the new one *before* this one
			is_new=true;
			folderNodeAddDebug.debug_location |= 0x100;
			break;
		}
		lastnode = node;
		node = node->next;
		folderNodeAddDebug.debug_location |= 0x1000;
	}
	if (node!=NULL && !is_new) { // We're doing an update
		bool update;
		node2 = node;
		if (is_folder) { // check timestamps and virtual location to see which one should be used
			// All folder nodes must link to the base GameDataDir (priority 0)
			if (virtual_location == 0) {
				folderNodeAddDebug.debug_location |= 0x10000;
				update = true;
			} else {
				if (node->virtual_location==0) {
					folderNodeAddDebug.debug_location |= 0x20000;
					update = false;
				} else {
					folderNodeAddDebug.debug_location |= 0x30000;
					update = virtual_location > node->virtual_location; // Take GDDs over pigs
				}
			}
		} else if (virtual_location < 0 && node->virtual_location < 0) {
			// Both pigs
			if (virtual_location != node->virtual_location) {
				// This is now allowed because of projects overriding Core
				//printf("Warning: duplicate file in two pigs: %s\n", fn);
				folderNodeAddDebug.debug_location |= 0x40000;
				update = virtual_location > node->virtual_location;
			} else {
				folderNodeAddDebug.debug_location |= 0x50000;
				update = true;
			}
		} else if (virtual_location < 0) {
			// Just the new one is a pig
			// Use the pig if it has the same timestamp // *and* this is not an "override" data dir
			folderNodeAddDebug.debug_location |= 0x60000;
			update= (timestamp == node->timestamp) || (timestamp - node->timestamp)==3600; // && (node->virtual_location==0);
			node2->seen_in_fs = 1;
			node2->seen_in_fs_virtual_location = node->virtual_location;
		} else if (node->virtual_location < 0) {
			// Just the old one is a pig
			// Use the pig if it has the same timestamp // *and* this is not an "override" data dir
			folderNodeAddDebug.debug_location |= 0x70000;
			if (node2->seen_in_fs && node2->seen_in_fs_virtual_location > virtual_location)
				update = false;
			else {
				update= !((timestamp == node->timestamp) || (timestamp - node->timestamp)==3600); // && (virtual_location==0));
				node2->seen_in_fs = 1;
				node2->seen_in_fs_virtual_location = virtual_location;
			}
		} else {
			// Both are file system
			// Update if this is a higher priority one, or if it's the same, then this has to be an update call
			folderNodeAddDebug.debug_location |= 0x80000;
			update = virtual_location >= node->virtual_location;
			node2->seen_in_fs = 1;
			node2->seen_in_fs_virtual_location = MAX(virtual_location, node->virtual_location);
		}
		//printf("file %s: %s\n", fn, update?"overridden":"did not override");
		assert(node2->parent == parent);
		assert(!is_folder);
		if (update) {
			update_count++;
			// Update the data
			node2->timestamp = timestamp;
			node2->size = size;
			node2->virtual_location = virtual_location;
			node2->file_index = file_index;
			node2->needs_patching = 0;
			node2->started_patching = 0;
		} else {
			discard_count++;
			if (bNeedCriticalSection)
				FolderNodeLeaveCriticalSection();
			PERFINFO_AUTO_STOP();
			return node2;
		}
	} else {
		folderNodeAddDebug.debug_location |= 0x100000;
		is_new = true;
		// Was not found in the list, it's a new node
		node2 = FolderNodeCreate();
		node2->parent = parent;
		if (is_folder)
        {
//printf("\nadding folder %s", fn);

			node2->name = (char*)allocAddFilename(fn);
        }
		else
		{
			if (pre_alloced_path)
			{
				//good_count++;
				node2->name = inline_strrchr(pre_alloced_path,'/');
				if (node2->name)
					node2->name++;
			}
			if (!node2->name)
			{
				//bad_count++;
				node2->name = (char*)allocAddFilename(fn);
			}
		}
		// These three not specifically needed if it is a folder
		node2->timestamp = timestamp;
		node2->size = size;
		node2->virtual_location = virtual_location;
		node2->file_index = file_index;
		if (virtual_location >=0) {
			node2->seen_in_fs = 1;
			node2->seen_in_fs_virtual_location = virtual_location;
		}
	}
	node2->is_dir = is_folder;
	node_ret = node2;
	if (is_folder) {
		node_ret = FolderNodeAdd(&node2->contents, &node2->contents_tail, node2, s, timestamp, size, virtual_location, file_index, createdOut, depth+1, pre_alloced_path);
	}
	if(is_new && createdOut){
		*createdOut = 1;
	}
	if (node!=NULL && !is_new) { // We are doing an update
		if (bNeedCriticalSection)
			FolderNodeLeaveCriticalSection();
		PERFINFO_AUTO_STOP();
		return node_ret;
	}
	if (lastnode) { // There was a list to begin with, add to it/insert in the middle
		FolderNode *next = lastnode->next;
		node2->prev = lastnode;
		lastnode->next = node2;
		node2->next = next;
	} else {  // node==NULL -> there was no list to begin with, or we want this to be the new first element
		node2->next = *head;
		*head = node2;
	}
	if (node2->next) {
		node2->next->prev = node2;
	}
	if (node2->next==NULL && tail) {
		// This is the new tail
		*tail = node2;
	}
	if (depth < ARRAY_SIZE(folder_node_add_cache)) {
		folder_node_add_cache[depth].parent = parent;
		folder_node_add_cache[depth].lastnode = node2;
	}
	if (bNeedCriticalSection)
		FolderNodeLeaveCriticalSection();
	PERFINFO_AUTO_STOP();
	return node_ret;
}

FolderNode *FolderNodeCreate()
{
	FolderNode *ret;

	FolderNodeEnterCriticalSection();
	MP_CREATE(FolderNode, 4096);
	ret = MP_ALLOC(FolderNode);
	FolderNodeLeaveCriticalSection();
	ret->contents=NULL;
	ret->contents_tail=NULL;
	ret->is_dir=0;
	ret->virtual_location=0;
	ret->name=NULL;
	ret->next=NULL;
	ret->prev=NULL;
	ret->parent=NULL;
	ret->size=-1;
	ret->timestamp=0;
	ret->seen_in_fs=0;

	return ret;
}

void FolderNodeDestroy(FolderCache* fc, FolderNode *node)
{
	FolderNode *next;
	
	FolderNodeEnterCriticalSection();

	for(next = node ? node->next : NULL;
		node;
		node = next, next = node ? node->next : NULL)
	{
		if (node->contents)
			FolderNodeDestroy(fc, node->contents);

		// Can't free strings from a stringtable
		//	free(node->name);

		if(fc->nodeDeleteCallback){
			FolderNodeLeaveCriticalSection();
			fc->nodeDeleteCallback(fc, node);
			FolderNodeEnterCriticalSection();
		}

		MP_FREE(FolderNode,node);
	}
	if (folder_node_add_cache[0].lastnode)
		ZeroStruct(&folder_node_add_cache);

	FolderNodeLeaveCriticalSection();
}

char *FolderNodeGetFullPath_s(FolderNode *node, char *buf, size_t buf_size)
{
	assert(buf);
	if (node->parent) {
		FolderNodeGetFullPath_s(node->parent, buf, buf_size);
	} else {
		buf[0]=0;
	}
	strcat_s(buf, buf_size, "/");
	strcat_s(buf, buf_size, node->name);
	return buf;
}


FolderNode *FolderNodeFind(FolderNode *node, const char *_fn) {
	char buf[CRYPTIC_MAX_PATH], *fn;
	char *s;
	int is_folder;

	PERFINFO_AUTO_START_FUNC();

	FolderNodeEnterCriticalSection();

	strcpy(buf, _fn);
	fn = buf;

	while (fn[0]=='/' || fn[0]=='\\') fn++;
	s = firstSlash(fn);
	if (!s) {
		is_folder=0;
	} else {
		*s=0;
		is_folder=1;
		s++;
	}
	while (node) {
		if (inline_stricmp(node->name, fn)==0) {
			// Found it!
			if (is_folder && *s) {
				node = FolderNodeFind(node->contents, s);
				FolderNodeLeaveCriticalSection();
				PERFINFO_AUTO_STOP();
				return node;
			} else {
				FolderNodeLeaveCriticalSection();
				PERFINFO_AUTO_STOP();
				return node;
			}
		}
		node = node->next;
	}
	FolderNodeLeaveCriticalSection();
	PERFINFO_AUTO_STOP();
	return NULL;
}

int FolderNodeRecurse(FolderNode *node, FolderNodeOp op)
{
	FolderNode* cur;
	int action;

	assert(g_debug_is_in_critical_section);

	for(cur = node; cur; cur = cur->next){
		action = op(cur);
		// Do children
		if (action==1) {
			action = FolderNodeRecurse(cur->contents, op);
		}
		if (action==2)
			return 2;
	}
	return 1;
}

void FolderCacheOverrideDelete(FolderCache *fc, char *relpath);

void FolderNodeDeleteFromTreeEx(FolderCache* fc, FolderNode *node, int deleteParent)
{
	char temp[CRYPTIC_MAX_PATH];
	FolderNode *parent = node->parent;

	assert(g_debug_is_in_critical_section);

	while(node->contents){
		FolderNodeDeleteFromTreeEx(fc, node->contents, 0);
	}

	FolderCacheOverrideDelete(fc, FolderNodeGetFullPath(node, temp)+1);

	EnterCriticalSection(&fc->critSecAllFiles);
	FolderCacheHashTableRemove(fc, FolderNodeGetFullPath(node, temp)+1);
	LeaveCriticalSection(&fc->critSecAllFiles);

	if (parent == NULL) {
		// We're at the base!
		if (fc->root_tail==node) {
			fc->root_tail = node->prev;
		}
		listRemoveMember(node, &fc->root);
		if (fc->root == NULL) {
			assert(fc->root_tail==NULL);
		}
		if (fc->root_tail== NULL) {
			assert(fc->root==NULL);
		}
	}else{
		// remove from parent's list and free memory
		if (parent->contents_tail==node) {
			parent->contents_tail = node->prev;
		}
		listRemoveMember(node, &parent->contents);
		if (parent->contents_tail == NULL) {
			assert(parent->contents==NULL);
		}
	}
	
	if(fc->nodeDeleteCallback){
		fc->nodeDeleteCallback(fc, node);
	}

	FolderNodeEnterCriticalSection();
	MP_FREE(FolderNode,node);
	FolderNodeLeaveCriticalSection();

	if (parent && deleteParent && parent->contents == NULL) {
		// The parent has no children, remove it as well
		assert(parent->contents_tail==NULL);
		FolderNodeDeleteFromTreeEx(fc, parent, 0);
	}

	if (folder_node_add_cache[0].lastnode)
		ZeroStruct(&folder_node_add_cache);
}

void FolderNodeDeleteFromTree(FolderCache* fc, FolderNode *node)
{
	FolderNodeDeleteFromTreeEx(fc, node, 1);
}

#define FNREP_PATH_SIZE CRYPTIC_MAX_PATH

// Return 0 to prune a tree, 1 to keep parsing, and 2 for a complete stop
//typedef int (*FolderNodeOpEx)(const char *dir, FolderNode *node, void *userdata);
static int FolderNodeRecurseExPath(	FolderNode *node,
									FolderNodeOpEx op,
									void *userdata_proc,
									void *userdata_data,
									bool bAlreadyHaveCritical,
									bool bDoNotReleaseCritical,
									char* path,
									char* pathstart)
{
	int action;
	FolderNode *next = node;
	// This function is recursive, so it's better to profile at a higher level.  It's just messy in the profiler to do it here.
	//PERFINFO_AUTO_START_FUNC();
	if (!bAlreadyHaveCritical)
		FolderNodeEnterCriticalSection();

	for (; node; node = next)
	{
		bool isDirWithChildren = node->is_dir && node->contents;
		next = node->next;
		// Append to working path
		strcpy_s(pathstart, FNREP_PATH_SIZE - (pathstart - path), node->name);

		// Leave the critical section so other threads can run, since the
		//  callback might be multithreaded, and as long as they do not *delete*
		//  nodes, any other modification to the tree is safe.
		if (!bDoNotReleaseCritical)
			FolderNodeLeaveCriticalSection();
		PERFINFO_AUTO_START("callback", 1);
		action = op(path, node, userdata_proc, userdata_data);
		PERFINFO_AUTO_STOP();
		if (!bDoNotReleaseCritical)
			FolderNodeEnterCriticalSection();
		// node might have been freed by here.
		if (isDirWithChildren && action==1) {
			char* slash = pathstart + strlen(pathstart);
			
			if(slash < path + FNREP_PATH_SIZE){
				*slash = '/';
		
				assert(node->contents != (FolderNode*)(size_t)0xFAFAFAFA);
				// scan subs
				action = FolderNodeRecurseExPath(	node->contents,
													op,
													userdata_proc,
													userdata_data,
													true,
													bDoNotReleaseCritical,
													path,
													slash + 1);
			}
		}
		if (action==2) {
			if (!bAlreadyHaveCritical)
				FolderNodeLeaveCriticalSection();
			PERFINFO_AUTO_STOP();
			return 2;
		}
	}
	if (!bAlreadyHaveCritical)
		FolderNodeLeaveCriticalSection();
	//PERFINFO_AUTO_STOP();
	return 1;
}

// Return 0 to prune a tree, 1 to keep parsing, and 2 for a complete stop
//typedef int (*FolderNodeOpEx)(const char *dir, FolderNode *node, void *userdata);
int FolderNodeRecurseEx(FolderNode *node,
						FolderNodeOpEx op,
						void *userdata_proc,
						void *userdata_data,
						const char *pathsofar,
						bool bAlreadyHaveCritical,
						bool bDoNotReleaseCritical)
{
	char path[FNREP_PATH_SIZE];
	char *pathstart=path;

	if (pathsofar==NULL || pathsofar[0]==0) {
		path[0]=0;
	} else {
		strcpy(path, pathsofar);
		strcat(path, "/");
		pathstart = path + strlen(path);
	}
	
	return FolderNodeRecurseExPath(	node,
									op,
									userdata_proc,
									userdata_data,
									bAlreadyHaveCritical,
									bDoNotReleaseCritical,
									path,
									pathstart);
}

int FolderNodeVerifyIntegrity(FolderNode *node, FolderNode *parent) {
	FolderNode *walk;
	assert(node->parent == parent);
	if (node->next) {
		assert(node->next->prev == node);
	}
	if (node->prev) {
		assert(node->prev->next == node);
	}
	if (node->contents) {
		assert(node->contents_tail!=NULL);
	} else {
		assert(node->contents_tail==NULL);
	}
	walk = node->contents;
	while (walk) {
		FolderNodeVerifyIntegrity(walk, node);
		walk = walk->next;
	}
	return 1;
}

size_t FolderNodeGetMemoryUsage(FolderNode *node)
{
	size_t size = sizeof(FolderNode);

	return size;
}

FolderNode *FolderNodeMoveToShared(FolderNode *node, SharedMemoryHandle *sm_handle)
{
	FolderNode *sm_node;
	FolderNode *subnode;

	sm_node = sharedMemoryAlloc(sm_handle, sizeof(FolderNode));

	if(node->parent && node->parent->contents == node) node->parent->contents = sm_node;
	if(node->parent && node->parent->contents_tail == node) node->parent->contents_tail = sm_node;

	if(node->prev) node->prev->next = sm_node;
	if(node->next) node->next->prev = sm_node;

	if(node->is_dir)
	{
		subnode = node->contents;

		while(subnode)
		{
			subnode->parent = sm_node;
			subnode = subnode->next;
		}
	}

	memcpy(sm_node, node, sizeof(FolderNode));
	MP_FREE(FolderNode, node);

	return sm_node;
}

void FolderNodeDestroyPool(void)
{
	MP_DESTROY(FolderNode);
}