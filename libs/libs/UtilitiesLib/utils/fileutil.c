#include "CrypticPorts.h"
#include "earray.h"
#include "fileWatch.h"
#include <fcntl.h>
#include "fileutil.h"
#include "FolderCache.h"
#include "net.h"
#include <sys/stat.h>
#include "sysutil.h"
#include <errno.h>
#include <share.h>
#include "StashTable.h"
#include "timing.h"
#include "utils.h"
#include "winfiletime.h"
#include "utf8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

void fileLoadDataDirs(int forceReload);


//////////////////////////////////////////////////////////////////////////
// Old version of fileScanAllDataDirs, only used when FolderCache is disabled
//  (shouldn't actually ever be used anymore)

static char* curRootPath;
static int curRootPathLength;
static FileScanProcessor scanAllDataDirsProcessor_slow;
static FolderNodeOpEx scanAllDataDirsProcessor_fast;
static char* scanTargetDir;

static void old_fileScanAllDataDirsRecurseHelper(char* relRootPath, void *pUserData){
	WIN32_FIND_DATAA wfd;
	struct _finddata32_t fd;
	FolderNode node = {0};
	char buffer[1024];
	FileScanAction action = FSA_EXPLORE_DIRECTORY;
	S32 good;
	U32 handle;

	sprintf(buffer, "%s/*", relRootPath);
	
	for(good = fwFindFirstFile(&handle, buffer, &wfd); good; good = fwFindNextFile(handle, &wfd))
	{
		if(stricmp(wfd.cFileName, ".") == 0 || stricmp(wfd.cFileName, "..") == 0)
			continue;
	
		strcpy(fd.name, wfd.cFileName);
		fd.size = wfd.nFileSizeLow;
		_FileTimeToUnixTime(&wfd.ftLastWriteTime, &fd.time_write, FALSE);
		fd.time_write = statTimeFromUTC(fd.time_write);
		_FileTimeToUnixTime(&wfd.ftCreationTime, &fd.time_create, FALSE);
		fd.time_create = statTimeFromUTC(fd.time_create);
		_FileTimeToUnixTime(&wfd.ftLastAccessTime, &fd.time_access, FALSE);
		fd.time_access = statTimeFromUTC(fd.time_access);
		fd.attrib = wfd.dwFileAttributes;

		if (scanAllDataDirsProcessor_fast)
		{
			// node
			node.name = wfd.cFileName;
			node.size = wfd.nFileSizeLow;
			node.timestamp = fd.time_write;
			node.hidden = !!(wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
			node.is_dir = !!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
			node.system = !!(wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM);
			node.writeable = !(wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
		}

		if(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			// Send directories to the processor directory without caching the name.
			// We do not want to prevent duplicate directories from being explored,
			// only duplicate files.
			if (scanAllDataDirsProcessor_fast)
				action = scanAllDataDirsProcessor_fast(relRootPath, &node, NULL, pUserData);
			else
				action = scanAllDataDirsProcessor_slow(relRootPath, &fd, pUserData);
		} else {
			//	Check if the path exists in the table of processed files.
			// Check if the file has already been processed before.
			//	Construct the relative path name to the file.
			sprintf(buffer, "%s/%s", relRootPath + curRootPathLength, wfd.cFileName);
			if (scanAllDataDirsProcessor_fast)
			{
				char buffer2[MAX_PATH];
				sprintf(buffer2, "%s/%s", relRootPath, wfd.cFileName);
				action = scanAllDataDirsProcessor_fast(buffer2, &node, NULL, pUserData);
			}
			else
				action = scanAllDataDirsProcessor_slow(relRootPath, &fd, pUserData);
		}

		if(	(action & FSA_EXPLORE_DIRECTORY) && 
			(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			sprintf(buffer, "%s/%s", relRootPath, wfd.cFileName);
			old_fileScanAllDataDirsRecurseHelper(buffer, pUserData);
		}

		if(action & FSA_STOP)
			break;
	}
	fwFindClose(handle);
}

// Scans for files in a given directory with a given extension.
// Will traverse max_depth into sub directories.
void fileScanFoldersToDepth(const char* absPath, int max_depth, FolderNodeOpEx processor, void *pUserData)
{
	FileScanAction action = FSA_EXPLORE_DIRECTORY;
	WIN32_FIND_DATAA wfd;
	FolderNode node = {0};
	char buffer[1024];
	U32 handle;
	S32 good;

	sprintf(buffer, "%s/*", absPath);

	for(good = fwFindFirstFile(&handle, buffer, &wfd); good; good = fwFindNextFile(handle, &wfd))
	{
		if(stricmp(wfd.cFileName, ".") == 0 || stricmp(wfd.cFileName, "..") == 0)
			continue;

		// node
		node.name = wfd.cFileName;
// 		node.size = wfd.nFileSizeLow;
// 		node.timestamp = fd.time_write;
// 		node.hidden = !!(wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
// 		node.is_dir = !!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
// 		node.system = !!(wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM);
// 		node.writeable = !(wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY);

		if(	(action & FSA_EXPLORE_DIRECTORY) && 
			(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && 
			(max_depth != 0))
		{
			char buffer2[MAX_PATH];
			sprintf(buffer2, "%s/%s", absPath, wfd.cFileName);
			fileScanFoldersToDepth(buffer2, max_depth-1, processor, pUserData);
		} 
		else if(!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			action = processor(absPath, &node, NULL, pUserData);
		}

		if(action & FSA_STOP)
			break;
	}
	fwFindClose(handle);
}

static int old_fileScanAllDataDirsHelper(const char* rootPath, void *pUserData){
	char buffer[1024];

	sprintf(buffer, "%s/%s", rootPath, scanTargetDir);
	curRootPath = buffer;
	curRootPathLength = (int)strlen(buffer);
	old_fileScanAllDataDirsRecurseHelper(buffer, pUserData);

	// Always continue through the string table.
	return 1;
}


void old_fileScanAllDataDirs(char* dir, FileScanProcessor processor_slow, FolderNodeOpEx processor_fast, void *pUserData)
{
	FileScanProcessor old_slow = scanAllDataDirsProcessor_slow; // Push on the stack
	FolderNodeOpEx old_fast = scanAllDataDirsProcessor_fast; // Push on the stack
	scanTargetDir = dir;

	scanAllDataDirsProcessor_slow = processor_slow;
	scanAllDataDirsProcessor_fast = processor_fast;

	// Scan all known data directories.
	{
		const char * const * dataDirs = fileGetGameDataDirs();
		int i;
		for (i=0; i<eaSize(&dataDirs); i++) {
			old_fileScanAllDataDirsHelper(dataDirs[i], pUserData);
		}
	}
	scanAllDataDirsProcessor_slow = old_slow; // Pop off the stack
	scanAllDataDirsProcessor_fast = old_fast;
}



//////////////////////////////////////////////////////////////////////////
// New (folder cache) version

extern FolderCache *folder_cache;
static int devel_dynamic=0;

typedef struct FSADDHData {
	void* pUserData;
	char filename[CRYPTIC_MAX_PATH];
} FSADDHData;

static FileScanAction fileScanAllDataDirsHelper_slow(	const char *dir,
														FolderNode *node,
														FileScanProcessor proc,
														FSADDHData* d)
{
	struct _finddata32_t fileInfo;
	FileScanAction action = FSA_EXPLORE_DIRECTORY;
	char* lastSlash;

	if (node->is_dir) {
		fileInfo.attrib = _A_SUBDIR;
	} else {
		if (devel_dynamic && !node->seen_in_fs) // This should catch files only in pigs when doing the development_dynamic mode
			return FSA_NO_EXPLORE_DIRECTORY;
		fileInfo.attrib = _A_NORMAL;
	}

	strcpy(d->filename, dir);	
	lastSlash = strrchr(d->filename, '/');
	strcpy(fileInfo.name, lastSlash ? lastSlash + 1 : d->filename);
	fileInfo.size = (int)node->size;
	fileInfo.time_access = fileInfo.time_create = fileInfo.time_write = node->timestamp;

	PERFINFO_AUTO_START("callback", 1);
	if(lastSlash){
		*lastSlash = 0;
	}
	action = proc(lastSlash ? d->filename : "", &fileInfo, d->pUserData);
	PERFINFO_AUTO_STOP();

	return action;
}

static FileScanAction fileScanAllDataDirsHelper_fast(	const char *dir,
														FolderNode *node,
														FolderNodeOpEx proc,
														void *pUserData_Data)
{
	if (!node->is_dir) {
		if (devel_dynamic && !node->seen_in_fs) // This should catch files only in pigs when doing the development_dynamic mode
			return FSA_NO_EXPLORE_DIRECTORY;
	}

	return proc(dir, node, NULL, pUserData_Data);
}


int quickload = 0;

static CRITICAL_SECTION fileScanAllDataDirs_critsec;
static bool fileScanAllDataDirs_critsec_inited=false;

AUTO_RUN;
void fileScanAllDataDirsInitCriticalSection(void)
{
	if (!fileScanAllDataDirs_critsec_inited) {
		InitializeCriticalSection(&fileScanAllDataDirs_critsec);
		fileScanAllDataDirs_critsec_inited = true;
	}
}


static char *good_dirs = "texture_library tricks fx sound sequencers object_library"; // Well-behaved directories that do not look outside if their own folder
static char *quickload_dirs = "texture_library";

void fileScanAllDataDirsEx(const char* _dir, FileScanProcessor processor_slow, FolderNodeOpEx processor_fast, void *pUserData)
{
	FolderNode *node;
	FolderNode *node_contents;
	char dir[CRYPTIC_MAX_PATH];
	bool doing_quickload=false;
	int pushCount=0;

	PERFINFO_AUTO_START_FUNC();

	// (FIND_DATA_DIRS_TO_CACHE)
	// Uncomment the printfColor line to get red output any time a directory is scanned
	// This can be used to update the pre-cache list of folders for various processes
	//printfColor(COLOR_BRIGHT|COLOR_RED, "FILE SCANNING '%s'\n", _dir);

	fileLoadGameDataDirAndPiggs();

	assert(fileScanAllDataDirs_critsec_inited);
	// to protect devel_dynamic global
	EnterCriticalSection(&fileScanAllDataDirs_critsec);

	strcpy(dir, _dir);

	if (fileIsAbsolutePath(dir))
	{
		scanAllDataDirsProcessor_slow = processor_slow;
		scanAllDataDirsProcessor_fast = processor_fast;
		curRootPath = dir;
		curRootPathLength = (int)strlen(dir);
		old_fileScanAllDataDirsRecurseHelper(dir, pUserData);
		LeaveCriticalSection(&fileScanAllDataDirs_critsec);
		PERFINFO_AUTO_STOP();
		return;
	}

	// This just scans the FolderCache
	if (!folder_cache) {
		// This should only happen in utils, not in the game/mapserver!
		//printf("Warning, no FolderCache, using file system to scan '%s' instead.\n", dir);
		old_fileScanAllDataDirs(dir, processor_slow, processor_fast, pUserData);
		LeaveCriticalSection(&fileScanAllDataDirs_critsec);
		PERFINFO_AUTO_STOP();
		return;
	}
	forwardSlashes(dir);
	if (dir[strlen(dir)-1]=='/') {
		dir[strlen(dir)-1]='\0';
	}
	if (quickload && strstr(quickload_dirs, _dir)!=0) {
		// Do not scan the tree, just rely on the pigs!
		pushCount++;
		FolderCacheModePush(FOLDER_CACHE_MODE_I_LIKE_PIGS);
		doing_quickload = true;
	} else {
		// Scan the directory so that the whole tree is in RAM
		// Truncate to the parent folder (so we don't scan the same folders more than once
		char *relpath = dir, *s, path[CRYPTIC_MAX_PATH];
		while (relpath[0]=='/') relpath++;
		strcpy(path, relpath);
		relpath = path;
		if (!strStartsWith(relpath, "server")) {
			if (s=strchr(relpath, '/')) {
				*s=0;
			}
		}
		FolderCacheRequestTree(folder_cache, relpath); // If we're in dynamic mode, this will load the tree!
	}
	if (FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC)
		devel_dynamic = 1;
	if (!doing_quickload && (strstr(good_dirs, _dir)!=0 || strStartsWith(_dir, "texts"))) {
		pushCount++;
		FolderCacheModePush(FOLDER_CACHE_MODE_DEVELOPMENT); // Another hack for dynamic mode
	}
	node = FolderNodeFind(folder_cache->root, dir);

	if (!node && strcmp(dir, "")==0)
		node_contents = folder_cache->root;
	else if (node)
		node_contents = node->contents;
	else
		node_contents = NULL;

//    printf("\nscanning '%s'", dir);

	// This will happen if we're running  ParserLoadFiles on a folder that's been .bined and the source folder doesn't exist (i.e. production version)
	if (!node) {
		//assert(!"Someone passed a bad path to fileScanallDataDirs, it doesn't exist (check your Game Data Dir)!");
		if (FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC ||
			FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT)
		{
			//verbose_printf("Warning: fileScanallDataDirs called on non-existent path (%s)!", _dir);
		}
	}

	if (!node_contents) {
		// Scanning a folder with nothing in it?
	} else {
		if (processor_slow)
		{
			FSADDHData d = {0};
			PERFINFO_AUTO_START("FolderNodeRecurseEx (slow)", 1);
			d.pUserData = pUserData;
			FolderNodeRecurseEx(node_contents, fileScanAllDataDirsHelper_slow, processor_slow, &d, dir, false, false);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			PERFINFO_AUTO_START("FolderNodeRecurseEx (fast)", 1);
			FolderNodeRecurseEx(node_contents, fileScanAllDataDirsHelper_fast, processor_fast, pUserData, dir, false, false);
			PERFINFO_AUTO_STOP();
		}
	}
	devel_dynamic = 0;
	// Restore old modes
	if (!doing_quickload && (strstr(good_dirs, _dir)!=0 || strStartsWith(_dir, "texts")))
	{
		FolderCacheModePop();
		pushCount--;
	}
	if (doing_quickload)
	{
		FolderCacheModePop();
		pushCount--;
	}
	assert(!pushCount);
	LeaveCriticalSection(&fileScanAllDataDirs_critsec);
	PERFINFO_AUTO_STOP();
}

void fileScanAllDataDirs(const char* dir, FileScanProcessor processor, void *pUserData)
{
	fileScanAllDataDirsEx(dir, processor, NULL, pUserData);
}

void fileScanAllDataDirs2(const char* dir, FolderNodeOpEx processor, void *pUserData)
{
	fileScanAllDataDirsEx(dir, NULL, processor, pUserData);
}


void fileScanAllDataDirsMultiRoot(const char* dir, FileScanProcessor processor, const char** roots, void *pUserData) {
    int rootsSize = eaSize( &roots );
    char dirSansRoot[ CRYPTIC_MAX_PATH ];

    {
        int it;

        for( it = 0; it != rootsSize; ++it ) {
            if( strStartsWith( dir, roots[ it ])) {
                strcpy( dirSansRoot, dir + strlen( roots[ it ]));
                break;
            }
        }
    }

    {
        int it;

        for( it = 0; it != rootsSize; ++it ) {
            char rootLocalDir[ CRYPTIC_MAX_PATH ];

            sprintf( rootLocalDir, "%s%s", roots[ it ], dirSansRoot );
            if( dirExists( rootLocalDir )) {
                fileScanAllDataDirs( rootLocalDir, processor, pUserData );
            }
        }
    }
}

void fileCacheDirectories(const char* paths[], int numpaths)
{
	int i;

	for(i = 0; i < numpaths; i++)
	{
		const char* curPath = paths[i];
		loadstart_printf("Precaching %s...", curPath);
		FolderCacheRequestTree(folder_cache, curPath);
		loadend_printf("done.");
	}
}





static char *  //returns the long format of file/folder name alone (not the preceeding path)
makeLongName(char *anyName, char *name, size_t name_size)//accepts full path, can be long or short
{
	struct _finddata32i64_t fileinfo;
	intptr_t handle = findfirst32i64_SAFE(anyName,&fileinfo);

	if(handle>0)
	{
		strcpy_s(name, name_size, fileinfo.name);
		_findclose(handle);
		return name;
		/*If you want to convert long names to short ones, 
		you can return fd.cAlternateFileName */
	}
	else
	{
		// error or file not found, leave it be
		char* retVal = anyName;
		char* cur;

		for(cur = anyName; *cur; cur++){
			if(	*cur == '/' ||
				*cur == '\\')
			{
				retVal = cur + 1;
			}
		}

		return retVal;
	}
}



char * //returns the complete pathname in long format.
// pass a buffer to be filled in as sout.  this is also what is returned by the function.
makeLongPathName_safe(char * anyPath, char *sout, int sout_size) //accepts both UNC and traditional paths, both long and short
{
	char long_name[CRYPTIC_MAX_PATH];
	char temp[CRYPTIC_MAX_PATH];
	char *token;
	char *strtokparam=NULL;
	assert(anyPath != sout);
	assert(sout_size>0);
	sout[0]=0;
	if (strncmp(anyPath,"\\\\",2)==0) {
		if ( sout_size > 2 ) {
			strcat_s(sout, sout_size, "//");
		}
	}
	token = strtok_r(anyPath, "\\/", &strtokparam);
	while (token!=NULL) {
		strcpy(temp, sout);
		strcat(temp, token);
		if (strlen(temp)<=2) { // X: or . or ..
			if ( sout_size > (int)strlen(sout) + (int)strlen(temp) )
				strcpy_s(sout, sout_size, temp);
		} else {
			if (stricmp(token, "..")==0)
			{
				// Eat the preivous folder name
				char *eatslash = strrchr(sout, '/');
				if (eatslash)
					*eatslash = '\0';
				eatslash = strrchr(sout, '/');
				if (eatslash)
					*eatslash = '\0';
			} else {
				char *toappend = makeLongName(temp, SAFESTR(long_name));
				if ( sout_size > (int)strlen(sout) + (int)strlen(toappend) )
					strcat_s(sout, sout_size, toappend);
			}
		}
		token = strtok_r(NULL, "\\/", &strtokparam);
		if (token!=NULL) {
			if ( sout_size > (int)strlen(sout) + 1 )
				strcat_s(sout, sout_size, "/");
		}
	}
	// Remove all /./, since they are redundant, and confuse gimme
	while (token = strstr(sout, "/./")) {
		strcpy_unsafe(token, token + 2);
	}
	return sout;
}

// for communication to DateCheckCallback
const char* g_filemask;
__time32_t g_lasttime;

// store latest time in g_lasttime
static FileScanAction DateCheckCallback(char* dir, struct _finddata32_t* data, void *pUserData)
{
	int len, masklen;
	int isok = 0;

	if (data->attrib & _A_SUBDIR) {
		// If it's a subdirectory, we don't care about checking times, etc
		if (data->name[0]=='_') {
			return FSA_NO_EXPLORE_DIRECTORY;
		} else {
			return FSA_EXPLORE_DIRECTORY;
		}
	}

	masklen = (int)strlen(g_filemask);
	len = (int)strlen(data->name);
	if (!masklen) isok = 1;
	else if( len > masklen && !stricmp(g_filemask, data->name+len-masklen) )
		isok = 1;

	if(!(data->name[0] == '_') && isok )
	{
		if (data->time_write > g_lasttime) {
			g_lasttime = data->time_write;
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}

// comes from old textparser.ParserIsPersistNewer
int IsFileNewerThanDir(const char* dir, const char* filemask, const char* persistfile)
{
	__time32_t lasttime;
	g_filemask = filemask;

	// check dates
	lasttime = fileLastChanged(persistfile);
	if (lasttime <= 0) // file doesn't exist
		return 0;

	if (dir)
	{
		g_lasttime = lasttime;
		fileScanAllDataDirs(dir, DateCheckCallback, NULL);

		if (g_lasttime == lasttime)
			return 1; // no files later
		else return 0;
	}
	else
	{
		__time32_t text_lasttime = fileLastChanged(filemask);
		if (text_lasttime<=0) {
			return 1;	// couldn't find text file, use bin
		}
		if (text_lasttime > lasttime)
			return 0; // text file newer
		else return 1; // bin file newer
	}
}

//Checks to see if a file has been updated.
//Also updates the age that this was last checked
int fileHasBeenUpdated(const char * fname, int *age)
{
	int	time;

	if(fname && fname[0] && fname[0] != '0')
	{
		time = fileLastChanged(fname);

		if(time > *age)
		{
			*age = time;
			return time;
		}	
	}
	return 0;
}

int fileCompare(const char *fname0, const char *fname1) // Returns 0 if equal
{
	FILE *f0, *f1;
	char buf0[65536], buf1[65536];
	int ret=0;
	size_t i, count0, count1;

	// Todo: possibly check filesize first?  Right now this is only called from Gimme, which already checks this

	f0 = fileOpen(fname0, "rb");
	f1 = fileOpen(fname1, "rb");
	if (f0==NULL && f1==NULL) {
		ret = 0;
		goto end;
	} else if (f0==NULL) {
		ret = -1;
		goto end;
	} else if (f1==NULL) {
		ret = 1;
		goto end;
	}
	// Both files are there
	do {
		count0 = fread(buf0, 1, ARRAY_SIZE(buf0), f0);
		count1 = fread(buf1, 1, ARRAY_SIZE(buf1), f1);
		if (count0!=count1) {
			ret = (int)count0-(int)count1;
			goto end;
		}
		for (i=0; i<count0; i++) {
			if (buf0[i]!=buf1[i]) {
				ret = buf0[i] - buf1[i];
				goto end;
			}
		}
	} while (count0>0);
	ret = 0;

end:
	if (f0) {
		fileClose(f0);
	}
	if (f1) {
		fileClose(f1);
	}
	return ret;

}

// Not using the one in file.c because it calls FolderCache stuff which might crash if memory is corrupted
static int fexist(const char *fname)
{
	struct _stat64 status;
	if(!_stat64(fname, &status)){
		if(status.st_mode & _S_IFREG)
			return 1;
	}
	return 0;
}

static char *findZip()
{
	static char *zip_locs[] = {"zip.exe","bin\\zip.exe","src\\util\\zip.exe","\\game\\tools\\util\\zip.exe","\\bin\\zip.exe"};
	static char *ziploc=NULL;
	if (!ziploc) {
		int i;
		for (i=0; i<ARRAY_SIZE(zip_locs); i++) {
			if (fexist(zip_locs[i])) {
				ziploc = zip_locs[i];
				break;
			}
		}
	}
	if (!ziploc) {
		ziploc = "zip"; // Hope it's in the path
	}
	return ziploc;
}

char *findUserDump()
{
	static char *zip_locs[] = {"userdump.exe","bin\\userdump.exe","src\\util\\userdump.exe","\\game\\tools\\util\\userdump.exe","\\bin\\userdump.exe"};
	static char *ziploc=NULL;
	if (!ziploc) {
		int i;
		for (i=0; i<ARRAY_SIZE(zip_locs); i++) {
			if (fexist(zip_locs[i])) {
				ziploc = zip_locs[i];
				break;
			}
		}
	}
	if (!ziploc) {
		ziploc = "userdump"; // Hope it's in the path
	}
	return ziploc;
}

static char *findGZip()
{
	static char *zip_locs[] = {"gzip.exe","bin\\gzip.exe","src\\util\\gzip.exe","\\game\\tools\\util\\gzip.exe","\\bin\\gzip.exe"};
	static char *ziploc=NULL;
	if (!ziploc) {
		int i;
		for (i=0; i<ARRAY_SIZE(zip_locs); i++) {
			if (fexist(zip_locs[i])) {
				ziploc = zip_locs[i];
				break;
			}
		}
	}
	if (!ziploc) {
		ziploc = "gzip"; // Hope it's in the path
	}
	return ziploc;
}


static void fileZipHelper(const char *filename, const char *exename, char *extension)
{
	char backupName[CRYPTIC_MAX_PATH];
	char zipName[CRYPTIC_MAX_PATH];
	char command[1024];

	// Backup old file
	strcpy(zipName, filename);
	strcat(zipName, extension);
	if (fexist(zipName)) {
		strcpy(backupName, filename);
		strcat(backupName, extension);
		strcat(backupName, ".bak");
		if (fexist(backupName)) {
			fileForceRemove(backupName);
		}
		rename(zipName, backupName);
	}
	sprintf(command, "%s \"%s\"", exename, filename);
#if PLATFORM_CONSOLE
	// can't use system on xbox
	assert( 0 );
#else
	system(command);
#endif
}

void fileGZip(const char *filename)
{
	fileZipHelper(filename, findGZip(), ".gz");
}

void fileGunZip(const char *filename)
{
#if PLATFORM_CONSOLE
	// can't use system on xbox
	assert( 0 );
#else
	char gunzip[CRYPTIC_MAX_PATH];
	sprintf(gunzip, "%s -d ", findGZip());
	
	if( fileExists( filename ))
	{
		strcat(gunzip, filename);
		system(gunzip);
	}
#endif
}


void fileZip(const char *filename)
{
	fileZipHelper(filename, findZip(), ".zip");
}

bool fileCanGetExclusiveAccess(const char *filename)
{
	static bool file_didnt_exist=0; // For debugging
	int handle;
	char fullpath[CRYPTIC_MAX_PATH];
	file_didnt_exist = false;
	fileLocateWrite(filename, fullpath);
	if (_wsopen_s_UTF8(&handle, fullpath, _O_RDONLY, _SH_DENYRW, _S_IREAD) ) {
		if (!fileExists(filename)) {
			file_didnt_exist=true;
			return false;
		}
		return false;
	}
	_close(handle); 
	return true;
}

void fileWaitForExclusiveAccess(const char *filename)
{
	static int file_didnt_exist=0; // For debugging
	int error_displayed=0; // To prevent consolespam
	errno_t err;
	int handle;
	char fullpath[CRYPTIC_MAX_PATH];
	file_didnt_exist = false;
	fileLocateWrite(filename, fullpath);
	backSlashes(fullpath);
	while (err = _wsopen_s_UTF8(&handle, fullpath, _O_RDONLY, _SH_DENYRW, _S_IREAD)) {
		if (!fileExists(fullpath)) {
			if (file_didnt_exist==16)
				return;
			// Try again, it may have been renamed to .bak in order to update it or something
			file_didnt_exist++;
		}
		if (err == EACCES && !error_displayed) {
			printf("\nUnable to access file %s (if using EditPlus, make sure File > Lock Files is not checked)\n", fullpath);
			error_displayed = true;
		}
		Sleep(1);
	}
	_close(handle); 
}

char *pwd(void)
{
	static char path[CRYPTIC_MAX_PATH];
	fileGetcwd(path, ARRAY_SIZE(path));
	return path;
}


bool fileAttemptNetworkReconnect(const char *filename)
{
#if !PLATFORM_CONSOLE
	char old_path[CRYPTIC_MAX_PATH];
	unsigned char drive[5];
	int ret;
	int olddrive;
	int newdrive;
	if (filename[1]!=':')
		return false;
	if (filename[0]=='C')
		return false;
	strncpy_s(drive, ARRAY_SIZE(drive), filename, ARRAY_SIZE(drive)-1);
	backSlashes(drive);
	olddrive = _getdrive();
	fileGetcwd(old_path, ARRAY_SIZE(old_path)-1);
	ret = _chdrive(toupper(drive[0]) - 'A' + 1);
	if (ret!=0)
		return false;
	newdrive = _getdrive();
	if (olddrive)
		_chdrive(olddrive);
	_chdir(old_path);
	return olddrive != newdrive;
#else
	return false;
#endif
}

void requireDataVersion(int version)
{
#if !PLATFORM_CONSOLE
	static int data_version=-1;
	if (isProductionMode())
		return;
	if (data_version==-1) {
		// get it
		char *data = fileAlloc("DataVersion.txt", NULL);
		if (data) {
			int ret = sscanf(data, "%d", &data_version);
			if (ret != 1) {
				data_version = -2;
			}
			free(data);
		} else {
			data_version = -2;
		}
	}
	if (data_version < version) {
		char buf[2048];
		char title[2048];
		int ret;
		sprintf(title, "%s: Old Data", getExecutableName());
		sprintf(buf, "You are running code which requires new data.\nIf you are a programmer, you must get latest data before running this code,\nif not, please get latest and try again, and if this error persists, contact someone in software, they probably forgot to check something in.\n\nYour version=%d\nRequired version=%d\n\nLots of errors and crashes may occur if you continue.  Do you want to conintue anyway?", data_version, version);
		ret = MessageBox_UTF8(NULL, buf, title, MB_YESNO|MB_ICONSTOP|MB_SYSTEMMODAL);
		if (ret == IDYES) {
			pushDontReportErrorsToErrorTracker(true);
		} else {
			exit(-1);
		}
	}
#endif
}

/// Internal function used by IsWildcardMatch().
bool isWildcardMatch1(const char *wildcardString, const char *stringToCheck,
                      bool caseSensitive, bool junkInFront, bool junkInBack)
{
	char wcChar;
	char strChar;
	// use the starMatchesZero variable to determine whether an asterisk
	// matches zero or more characters (TRUE) or one or more characters
	// (FALSE)
	int starMatchesZero = 1;

    if( !junkInFront ) {
        strChar = *stringToCheck;
        wcChar = *wildcardString;
    } else {
        strChar = *stringToCheck;
        wcChar = '*';
        --wildcardString;
    }

    while (strChar && wcChar)
    {
		// we only want to advance the pointers if we successfully assigned
		// both of our char variables, so we'll do it here rather than in the
		// loop condition itself
		*stringToCheck++;
		*wildcardString++;

		// if this isn't a case-sensitive match, make both chars uppercase
		// (thanks to David John Fielder (Konan) at http://innuendo.ev.ca
		// for pointing out an error here in the original code)
		if (!caseSensitive)
		{
			wcChar = toupper(wcChar);
			strChar = toupper(strChar);
		}

		// check the wcChar against our wildcard list
		switch (wcChar)
		{
			// an asterisk matches zero or more characters
			case '*' :
				// do a recursive call against the rest of the string,
				// until we've either found a match or the string has
				// ended
				if (starMatchesZero)
					*stringToCheck--;

				while (*stringToCheck)
				{
					if (isWildcardMatch1(wildcardString, stringToCheck++, caseSensitive, false, junkInBack))
						return true;
				}

				break;

			// a question mark matches any single character
			case '?' :
				break;

			// if we fell through, we want an exact match
			default :
				if (wcChar != strChar)
					return false;
				break;
		}

        strChar = *stringToCheck;
        wcChar = *wildcardString;
	}

	// if we have any asterisks left at the end of the wildcard string, we can
	// advance past them if starMatchesZero is TRUE (so "blah*" will match "blah")
	while ((*wildcardString) && (starMatchesZero))
	{
		if (*wildcardString == '*')
			wildcardString++;
		else
			break;
	}

    // a partial match... if we got to the end but there's no stuff
	// left in either of our strings, an exact match; otherwise, we
	// have a partial match
    return *wildcardString == '\0' && (junkInBack || *stringToCheck == '\0');
}

/// See if a string matches a wildcard specification that uses * or ?
/// (like "*.txt"), and return TRUE or FALSE, depending on the result.
/// There's also a TRUE/FALSE parameter you use to indicate whether
/// the match should be case-sensitive or not.
bool isWildcardMatch( const char* wildcard, const char* string, bool caseSensitive, bool onlyExactMatch )
{
    return isWildcardMatch1( wildcard, string, caseSensitive, !onlyExactMatch, !onlyExactMatch );
}


void fseek_and_set(FILE *pFile, size_t iOffset, char c)
{
	size_t iCurOffset = ftell(pFile);
	size_t iDiff = iCurOffset - iOffset;
	
	assertmsgf(iCurOffset > iOffset, "Trying to seek from %lu to %lu in %s", iCurOffset, iOffset, pFile->nameptr);

	fseek(pFile, iOffset, SEEK_SET);

	while (iDiff)
	{
		fwrite(&c, 1, 1, pFile);
		iDiff--;
	}

	fseek(pFile, iOffset, SEEK_SET);
}


typedef struct FileServerFile
{
	NetLink *link;
	char remote_path[MAX_PATH];
	char how[16];
	U64 remote_handle;
	S64 int_response;
	void *buf;
	size_t buf_size;
	WIN32_FIND_DATAA *wfd;
	bool ready;
	NetComm *fileserver_client_comm;
} FileServerFile;
//static CRITICAL_SECTION fileserver_client_cs;
//static StashTable fileserver_client_links;
// AUTO_RUN;
// void fileServerClientInit(void)
// {
// 	InitializeCriticalSection(&fileserver_client_cs);
// }

static void destroyFileServerFile(FileServerFile *file_data)
{
// 	EnterCriticalSection(&fileserver_client_cs);
 	linkFlushAndClose(&file_data->link, "Destroying");
	commDestroy(&file_data->fileserver_client_comm); // Note: does nothing
// 	LeaveCriticalSection(&fileserver_client_cs);
	free(file_data);
}

enum {
	FILESERVER_CONNECT = 1, // From Worker to Server
	FILESERVER_FOPEN, // From Worker to Server
	FILESERVER_FOPEN_RESPONSE, // From Server to Worker
	FILESERVER_FCLOSE, // From Worker to Server
	FILESERVER_FSEEK, // From Worker to Server
	FILESERVER_FTELL, // From Worker to Server
	FILESERVER_INT_RESPONSE, // From Server to Worker, generic single integer (S64) response
	FILESERVER_FREAD,
	FILESERVER_FREAD_RESPONSE,
	FILESERVER_FINDFIRSTFILE,
	FILESERVER_FINDNEXTFILE,
	FILESERVER_FINDNEXTFILE_RESPONSE,
	FILESERVER_FINDCLOSE,
	FILESERVER_SHUTDOWN,

};


static bool g_file_server_run;
typedef struct FileServerClient
{
	NetLink *link;
	char name[256];
} FileServerClient;

static void fileServerMsg(Packet *pkt,int cmd,NetLink *link, FileServerClient **client_p)
{
	FileServerClient	*client=0;

	if (client_p)
	{
		client = *client_p;
		if (!client)
		{
			client = *client_p = calloc(sizeof(FileServerClient), 1);
			client->link = link;
		}
	}
	assert(client);
 	switch(cmd)
 	{
		xcase FILESERVER_CONNECT:
			strcpy(client->name, pktGetStringTemp(pkt));
		xcase FILESERVER_FOPEN:
		{
			char filename[MAX_PATH];
			char how[16];
			FILE *f;
			pktGetString(pkt, SAFESTR(filename));
			pktGetString(pkt, SAFESTR(how));
			f = fopen(filename, how);
			pkt = pktCreate(link, FILESERVER_FOPEN_RESPONSE);
			pktSendBits64(pkt, 64, (U64)(ptrdiff_t)f);
			pktSend(&pkt);
		}
		xcase FILESERVER_FCLOSE:
		{
			FILE *f = (FILE*)(intptr_t)pktGetBits64(pkt, 64);
			fclose(f);
		}
		xcase FILESERVER_FSEEK:
		{
			FILE *f = (FILE*)(intptr_t)pktGetBits64(pkt, 64);
			S64 dist = pktGetBits64(pkt, 64);
			U32 whence = pktGetBitsAuto(pkt);
			S64 ret = fseek(f, dist, whence);
			pkt = pktCreate(link, FILESERVER_INT_RESPONSE);
			pktSendBits64(pkt, 64, ret);
			pktSend(&pkt);
		}
		xcase FILESERVER_FTELL:
		{
			FILE *f = (FILE*)(intptr_t)pktGetBits64(pkt, 64);
			S64 ret = ftell(f);
			pkt = pktCreate(link, FILESERVER_INT_RESPONSE);
			pktSendBits64(pkt, 64, ret);
			pktSend(&pkt);
		}
		xcase FILESERVER_FREAD:
		{
			FILE *f = (FILE*)(intptr_t)pktGetBits64(pkt, 64);
			S64 size = pktGetBits64(pkt, 64);
			void *buf = calloc(size, 1);
			S64 ret = fread(buf, 1, size, f);

			pkt = pktCreate(link, FILESERVER_FREAD_RESPONSE);
			pktSendBits64(pkt, 64, ret);
			pktSendBytes(pkt, ret, buf);
			pktSend(&pkt);
			SAFE_FREE(buf);
		}
		xcase FILESERVER_FINDFIRSTFILE:
		{
			U32 handle=0;
			S32 ret;
			char filespec[MAX_PATH];
			WIN32_FIND_DATAA wfd;
			pktGetString(pkt, SAFESTR(filespec));
			
			ret = fwFindFirstFile(&handle, filespec, &wfd);
			pkt = pktCreate(link, FILESERVER_FINDNEXTFILE_RESPONSE);
			pktSendBits64(pkt, 64, handle);
			pktSendBits64(pkt, 64, (U32)ret);
			if (ret)
			{
				pktSendString(pkt, wfd.cFileName);
				pktSendBits(pkt, 32, wfd.ftLastWriteTime.dwHighDateTime);
				pktSendBits(pkt, 32, wfd.ftLastWriteTime.dwLowDateTime);
				pktSendBits(pkt, 32, wfd.nFileSizeLow);
				pktSendBits(pkt, 32, wfd.nFileSizeHigh);
				pktSendBits(pkt, 32, wfd.dwFileAttributes);
			}
			pktSend(&pkt);
		}
		xcase FILESERVER_FINDNEXTFILE:
		{
			S32 ret;
			WIN32_FIND_DATAA wfd;
			U32 handle = pktGetBits64(pkt, 64);

			ret = fwFindNextFile(handle, &wfd);
			pkt = pktCreate(link, FILESERVER_FINDNEXTFILE_RESPONSE);
			pktSendBits64(pkt, 64, handle);
			pktSendBits64(pkt, 64, (U32)ret);
			if (ret)
			{
				pktSendString(pkt, wfd.cFileName);
				pktSendBits(pkt, 32, wfd.ftLastWriteTime.dwHighDateTime);
				pktSendBits(pkt, 32, wfd.ftLastWriteTime.dwLowDateTime);
				pktSendBits(pkt, 32, wfd.nFileSizeLow);
				pktSendBits(pkt, 32, wfd.nFileSizeHigh);
				pktSendBits(pkt, 32, wfd.dwFileAttributes);
			}
			pktSend(&pkt);
		}
		xcase FILESERVER_FINDCLOSE:
		{
			U32 handle = pktGetBits64(pkt, 64);
			fwFindClose(handle);
		}
		xcase FILESERVER_SHUTDOWN:
		{
			g_file_server_run = false;
		}
		xdefault:
			assert(0);
 	}
}

static void fileServerConnect(NetLink *link,FileServerClient **client_p)
{
	//linkSetKeepAliveSeconds(link, PING_TIME);
	printf("Client connected\n");
}

static void fileServerDisconnect(NetLink *link,FileServerClient **client_p)
{
	FileServerClient	*client;

	if (!client_p)
		return;
	client = *client_p;
	if (!client)
		return;

	printf("Disconnect from %s [%p]\n",client->name,link);
}

static const int strlen_net_cryptic = 13; // strlen("net:/cryptic/");
bool fileIsFileServerPath(const char *path)
{
	static const char *s0="net:/cryptic/";
	static const char *s1="NET:\\cryptic\\";
	int i;
	for (i=0; i<strlen_net_cryptic; i++, path++)
		if (!(*path==s0[i] || *path==s1[i]))
			return false;
	return true;
}

void fileServerRun(void)
{
	NetComm *comm;
	NetListen *listener;
	g_file_server_run = true;
	comm = commCreate(20,0);
	listener = commListen(comm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS, XBOX_FILESERVER_PORT,fileServerMsg,fileServerConnect,fileServerDisconnect,sizeof(FileServerClient *));
	if (listener) {
		printf("Running as file server.\n");
	} else {
		printf("Failed to create listener.\n");
	}
	while (g_file_server_run)
	{
		commMonitor(comm);
	}
}

static void fileServerClientDisconnect(NetLink *link, void *user_data)
{
	//Nothing?
}

static void fileServerClientMsg(Packet *pak, int cmd, NetLink* link, void *user_data)
{
	FileServerFile *file_data = user_data;
	assert(file_data->link == link);
	switch(cmd)
 	{
 		xcase FILESERVER_FOPEN_RESPONSE:
			file_data->remote_handle = pktGetBits64(pak, 64);
			file_data->ready = true;
		xcase FILESERVER_INT_RESPONSE:
			file_data->int_response = pktGetBits64(pak, 64);
			file_data->ready = true;
		xcase FILESERVER_FREAD_RESPONSE:
			assert(!file_data->ready);
			file_data->int_response = pktGetBits64(pak, 64);
			assert(file_data->int_response <= (S64)file_data->buf_size);
			pktGetBytes(pak, file_data->int_response, file_data->buf);
			file_data->ready = true;
		xcase FILESERVER_FINDNEXTFILE_RESPONSE:
			file_data->remote_handle = pktGetBits64(pak, 64);
			file_data->int_response = pktGetBits64(pak, 64);
			if (file_data->int_response)
			{
				pktGetString(pak, SAFESTR(file_data->wfd->cFileName));
				file_data->wfd->ftLastWriteTime.dwHighDateTime = pktGetBits(pak, 32);
				file_data->wfd->ftLastWriteTime.dwLowDateTime = pktGetBits(pak, 32);
				file_data->wfd->nFileSizeLow = pktGetBits(pak, 32);
				file_data->wfd->nFileSizeHigh = pktGetBits(pak, 32);
				file_data->wfd->dwFileAttributes = pktGetBits(pak, 32);
			}
			file_data->ready = true;
		xdefault:
			assert(0);
 	}
}


static FileServerFile *fileServerClientConnect(const char *_name)
{
	Packet	*pkt;
	FileServerFile *file_data;
	char name[MAX_PATH];
	char addr[MAX_PATH];
	char *s;
	NetLink *link;

	strcpy(name, _name);
	forwardSlashes(name);

	strcpy(addr, name + strlen_net_cryptic);
	s = strchr(addr, '/');
	if (!s)
		return NULL;
	*s = '\0';
	file_data = callocStruct(FileServerFile);
	strcpy(file_data->remote_path, s+1);

	//EnterCriticalSection(&fileserver_client_cs);
	if (1) //!fileserver_client_comm)
	{
		file_data->fileserver_client_comm = commCreate(20,0);
		//fileserver_client_links = stashTableCreateWithStringKeys(4, StashDeepCopyKeys_NeverRelease);
	}
	if (1) //!stashFindPointer(fileserver_client_links, addr, &link))
	{
		link = commConnectWait(file_data->fileserver_client_comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH|LINK_NO_COMPRESS,addr,XBOX_FILESERVER_PORT,
			fileServerClientMsg,NULL,fileServerClientDisconnect, 0, 10.f);
		if (link)
		{
			//verify(stashAddPointer(fileserver_client_links, addr, link, false));
		}
	}
	if (!link || !linkConnected(link))
	{
		destroyFileServerFile(file_data);
		//LeaveCriticalSection(&fileserver_client_cs);
		return NULL;
	}
	file_data->link = link;
	linkSetUserData(file_data->link, file_data);

	pkt = pktCreate(file_data->link, FILESERVER_CONNECT);
	pktSendString(pkt,getHostName());
	pktSend(&pkt);

	//LeaveCriticalSection(&fileserver_client_cs);
	return file_data;
}

static FileServerFile *fileServerClient_fopenInternal(FileServerFile *file_data, const char *how)
{
	Packet	*pkt;

	if (!file_data)
		return NULL;

	if (stricmp(how, "shutdown")==0)
	{
		//EnterCriticalSection(&fileserver_client_cs);
		linkSetUserData(file_data->link, file_data);

		pkt = pktCreate(file_data->link, FILESERVER_SHUTDOWN);
		pktSendString(pkt, file_data->remote_path);
		pktSendString(pkt, file_data->how);
		pktSend(&pkt);
		destroyFileServerFile(file_data);
		//LeaveCriticalSection(&fileserver_client_cs);
		return NULL;
	}

	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	file_data->ready = false; // Wait for response
	strcpy(file_data->how, how);
	pkt = pktCreate(file_data->link, FILESERVER_FOPEN);
	pktSendString(pkt, file_data->remote_path);
	pktSendString(pkt, file_data->how);
	pktSend(&pkt);

	while (linkConnected(file_data->link) && !file_data->ready)
		commMonitor(file_data->fileserver_client_comm);
	//LeaveCriticalSection(&fileserver_client_cs);
	if (file_data->ready && file_data->remote_handle)
	{
		return file_data;
	} else {
		destroyFileServerFile(file_data);
		return NULL;
	}
}

S64 fileServerClient_fseek(FileServerFile *file_data, S64 dist, int whence)
{
	Packet	*pkt;
	S64 ret;
	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	pkt = pktCreate(file_data->link, FILESERVER_FSEEK);
	pktSendBits64(pkt, 64, file_data->remote_handle);
	pktSendBits64(pkt, 64, dist);
	pktSendBitsAuto(pkt, whence);
	pktSend(&pkt);

	file_data->ready = false;
	file_data->int_response = -1;
	while (linkConnected(file_data->link) && !file_data->ready)
		commMonitor(file_data->fileserver_client_comm);
	ret = file_data->int_response;
	//LeaveCriticalSection(&fileserver_client_cs);
	return ret;
}

S64 fileServerClient_ftell(FileServerFile *file_data)
{
	Packet	*pkt;
	S64 ret;
	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	pkt = pktCreate(file_data->link, FILESERVER_FTELL);
	pktSendBits64(pkt, 64, file_data->remote_handle);
	pktSend(&pkt);

	file_data->ready = false;
	file_data->int_response = -1;
	while (linkConnected(file_data->link) && !file_data->ready)
		commMonitor(file_data->fileserver_client_comm);
	ret = file_data->int_response;
	//LeaveCriticalSection(&fileserver_client_cs);
	return ret;
}

int fileServerClient_fclose(FileServerFile *file_data)
{
	Packet	*pkt;
	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	pkt = pktCreate(file_data->link, FILESERVER_FCLOSE);
	pktSendBits64(pkt, 64, file_data->remote_handle);
	pktSend(&pkt);
	destroyFileServerFile(file_data);
	//LeaveCriticalSection(&fileserver_client_cs);
	return 0;
}

FileServerFile *fileServerClient_fopen(const char *fname, const char *how)
{
	FileServerFile *file_data;
	file_data = fileServerClientConnect(fname);
	file_data = fileServerClient_fopenInternal(file_data, how);
	return file_data;
}


size_t fileServerClient_fread(void *buf, U64 size, FileServerFile *file_data)
{
	size_t ret=0;
	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	while (size>0)
	{
		U64 buf_size = MIN(4*1024*1024, size);
		Packet	*pkt;
		pkt = pktCreate(file_data->link, FILESERVER_FREAD);
		pktSendBits64(pkt, 64, file_data->remote_handle);
		pktSendBits64(pkt, 64, buf_size);
		pktSend(&pkt);

		file_data->ready = false;
		file_data->buf = buf;
		file_data->buf_size = buf_size;
		file_data->int_response = -1;
		while (linkConnected(file_data->link) && !file_data->ready)
			commMonitor(file_data->fileserver_client_comm);
		ret += file_data->int_response;
		buf = ((char*)buf) + buf_size;
		size -= buf_size;
	}
	//LeaveCriticalSection(&fileserver_client_cs);
	return ret;
}


S32 fileServerFindFirstFile(void **handleOut, const char* fileSpec, WIN32_FIND_DATAA* wfd)
{
	Packet *pkt;
	FileServerFile *file_data;

	file_data = fileServerClientConnect(fileSpec);
	if (!file_data)
	{
		*handleOut = NULL;
		return 0;
	}

	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	file_data->ready = false; // Wait for response
	file_data->wfd = wfd;
	pkt = pktCreate(file_data->link, FILESERVER_FINDFIRSTFILE);
	pktSendString(pkt, file_data->remote_path);
	pktSend(&pkt);

	while (linkConnected(file_data->link) && !file_data->ready)
		commMonitor(file_data->fileserver_client_comm);
	if (file_data->ready && file_data->remote_handle)
	{
		*handleOut = file_data;
		//LeaveCriticalSection(&fileserver_client_cs);
		return 1;
	} else {
		destroyFileServerFile(file_data);
		*handleOut = NULL;
		//LeaveCriticalSection(&fileserver_client_cs);
		return 0;
	}
}

S32 fileServerFindNextFile(void *handle, WIN32_FIND_DATAA* wfd)
{
	FileServerFile *file_data = handle;
	Packet *pkt;
	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	file_data->ready = false; // Wait for response
	file_data->wfd = wfd;
	pkt = pktCreate(file_data->link, FILESERVER_FINDNEXTFILE);
	pktSendBits64(pkt, 64, file_data->remote_handle);
	pktSend(&pkt);

	while (linkConnected(file_data->link) && !file_data->ready)
		commMonitor(file_data->fileserver_client_comm);
	if (file_data->ready && file_data->int_response)
	{
		//LeaveCriticalSection(&fileserver_client_cs);
		return 1;
	} else {
		//LeaveCriticalSection(&fileserver_client_cs);
		return 0;
	}
}

S32 fileServerFindClose(void *handle)
{
	FileServerFile *file_data = handle;
	Packet *pkt;
	//EnterCriticalSection(&fileserver_client_cs);
	linkSetUserData(file_data->link, file_data);

	pkt = pktCreate(file_data->link, FILESERVER_FINDCLOSE);
	pktSendBits64(pkt, 64, file_data->remote_handle);
	pktSend(&pkt);
	destroyFileServerFile(file_data);
	//LeaveCriticalSection(&fileserver_client_cs);
	return 0;
}

char *fileGetFilename_s(const char *fFullPath, char *dest, size_t dest_size)
{
	int pathLen = (int) strlen(fFullPath);
	char *pNoPathExecutable = alloca(pathLen + 1);
	strcpy_s(pNoPathExecutable, pathLen + 1, fFullPath);
	backSlashes(pNoPathExecutable);
	pNoPathExecutable += pathLen;

	while (pathLen && (*(pNoPathExecutable-1) !=  '\\'))
	{
		pathLen--;
		pNoPathExecutable--;
	}
	if (pathLen)
	{
		if (strcpy_s(dest, dest_size, pNoPathExecutable) == 0)
			return dest;
	}
	else
	{
		if (strcpy_s(dest, dest_size, fFullPath) == 0)
			return dest;
	}
	return NULL;
}

void fileSplitFilepathAndExt_s(const char *fullPath, char *fname, size_t fname_size, char *ext, size_t ext_size)
{
	char *extLoc = (char*) strrchr(fullPath, '.');
	if (extLoc)
	{
		strcpy_s(ext, ext_size, extLoc);
		*extLoc = '\0';
		strcpy_s(fname, fname_size, fullPath);
		*extLoc = '.';
	}
	else
	{
		if (ext_size)
			ext[0] = '\0';
		strcpy_s(fname, fname_size, fullPath);
	}
}

// reverse strpbrk
static const char *strrpbrk(const char *dest, const char *delims)
{
	size_t dest_length = strlen(dest);
	const char * last_test_char = dest + dest_length;
	while (last_test_char > dest)
	{
		--last_test_char;
		if (strchr(delims, *last_test_char))
			return (char*)last_test_char;
	}

	return NULL;
}

const char *fileGetFilenameSubstrPtr(const char *filenameOrFullPath)
{
	const char * fileNameStart = strrpbrk(filenameOrFullPath, "/\\");
	if (fileNameStart)
		++fileNameStart;
	else
		fileNameStart = filenameOrFullPath;
	return fileNameStart;
}

char *fileStripFileExtension(char *filenameOrFullPath)
{
	char *fileNameStart = (char*)fileGetFilenameSubstrPtr(filenameOrFullPath);
	char *fileExtensionStart = strrchr(fileNameStart, '.');
	if (fileExtensionStart)
		*fileExtensionStart = '\0';
	return fileExtensionStart;
}


bool FilenamesMatchFrankenbuildAware(const char *pName1, const char *pName2)
{
	char *pShortName1 = NULL;
	char *pShortName2 = NULL;
	bool bRetVal;

	estrStackCreate(&pShortName1);
	estrStackCreate(&pShortName2);

	estrGetDirAndFileName(pName1, NULL, &pShortName1);
	estrGetDirAndFileName(pName2, NULL, &pShortName2);

	estrTruncateAtLastOccurrence(&pShortName1, '.');
	estrTruncateAtLastOccurrence(&pShortName2, '.');

	estrTruncateAtLastOccurrence(&pShortName1, '_');
	estrTruncateAtLastOccurrence(&pShortName2, '_');

	if (strEndsWith(pShortName1, "X64FD"))
	{
		estrSetSize(&pShortName1, estrLength(&pShortName1) - 5);
	}
	else if (strEndsWith(pShortName1, "X64"))
	{
		estrSetSize(&pShortName1, estrLength(&pShortName1) - 3);
	}
	else if (strEndsWith(pShortName1, "FD"))
	{
		estrSetSize(&pShortName1, estrLength(&pShortName1) - 2);
	}

	if (strEndsWith(pShortName2, "X64FD"))
	{
		estrSetSize(&pShortName2, estrLength(&pShortName2) - 5);
	}
	else if (strEndsWith(pShortName2, "X64"))
	{
		estrSetSize(&pShortName2, estrLength(&pShortName2) - 3);
	}
	else if (strEndsWith(pShortName2, "FD"))
	{
		estrSetSize(&pShortName2, estrLength(&pShortName2) - 2);
	}

	bRetVal = (stricmp(pShortName1, pShortName2) == 0);

	estrDestroy(&pShortName1);
	estrDestroy(&pShortName2);

	return bRetVal;
}

void humanBytes(S64 bytes, F32 *num, char **units, U32 *prec)
{
	static char *unit[] = { "", "KB", "MB", "GB", "TB" };
	static U32  precs[] = {  0,    0,    1,    2,    2 };
	int i;
	*num = bytes;
	*units = unit[0];
	if (prec)
		*prec = 0;
	for(i = 0; i < 4 && *num > 9999; i++)
	{
		*num /= 1024;
		*units = unit[i+1];
		if (prec)
			*prec = precs[i+1];
	}
}
