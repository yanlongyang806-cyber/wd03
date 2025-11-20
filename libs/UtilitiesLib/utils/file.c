
/***************************************************************************
 
 
 
 */
#include "file.h"
#include "resourcemanager.h"
#include "wininclude.h"

#if _PS3
#include <unistd.h>
#include <cell/cell_fs.h>
#else
#include <sys/stat.h>
#if !_XBOX
#include <shlobj.h>
#endif
#endif

#include "utils.h"
#include "fileutil.h"
#include "PigFileWrapper.h"
#include "FolderCache.h"
#include "DirMonitor.h"
#include "timing.h"
#include "EString.h"
#include "winfiletime.h"
#include "sysutil.h"
#include "MemoryPool.h"
#include "MemoryMonitor.h"
#include "osdependent.h"
#include "memlog.h"
#include "StringUtil.h"
#include "earray.h"
#include "gimmeDLLWrapper.h"
#include "fileWatch.h"
#include "fileutil2.h"
#include "StringCache.h"
#include "hoglib.h"
#include "ThreadManager.h"
#include <share.h>
#include <errno.h>
#include "../../3rdparty/zlib/zlib.h"
#include "trivia.h"
#include "RegistryReader.h"
#include "utilitiesLib.h"
#include "GlobalTypes.h"
#include "StashTable.h"
#include "strings_opt.h"
#include "error.h"
#include "AutoGen/file_h_ast.c"
#include "UTF8.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#if _XBOX
	// Basic libraries required to link on the Xbox at all
	#pragma comment(lib, "xbdm.lib")
	#ifdef PROFILE
 		#pragma comment(lib, "d3d9i.lib")
	#else
		#pragma comment(lib, "d3d9.lib")
	#endif
#endif
bool g_xbox_production_hack=false;
#if _PS3
// JE: Defaulting to on for now, though should default off if other issues are fixed
bool g_xbox_local_hoggs_hack=true;
#else
bool g_xbox_local_hoggs_hack=false;
#endif

bool g_PatchingStreamingOn=false;
bool isPatchStreamingOn(void)
{
	return g_PatchingStreamingOn;
}


typedef struct RAMCachedFile
{
	FileWrapper *base_file;
	U8 *buffer; // Buffered data
	size_t pos;
	size_t bufferlen; // Amount of data in the buffer
	U32 assert_on_write:1;
	U32 buffer_owned:1;
} RAMCachedFile;

// A table of possible strings to try when using relative paths.
const char **eaGameDataDirs = NULL;
const char **eaGameDataDirNames = NULL;
const char **eaExtraGameDataDirs = NULL;
const char **eaExtraGameDataDirNames = NULL;
const char **eaNameSpaceNames = NULL;
static int loadedGameDataDirs = 0;
static int addSearchPath = 1;
const char *mainGameDataDir = NULL;
FolderCache *folder_cache=NULL;
static char **gameDataDirOverrides = NULL;
bool g_force_absolute_paths=false;

FileStats g_file_stats = {0};

static char g_cwdAtFileDataDirTime[MAX_PATH];

static int file_disable_winio=0;

// Open files with Windows backup semantics.
bool file_windows_backup_semantics = false;

void initFileWrappers(void);



static int LoadAllUserNamespaces_Counter = 0;
static FileScanAction LoadAllUserNamespaces_CB(const char* path, FolderNode *node, void *unused, void *pUserData)
{
	char buffer[MAX_PATH];

	if (node->name[0] != '_') 
	{ 
		if(strEndsWith(node->name, ".namespace"))
		{
			getFileNameNoExt(buffer, path);
			resNameSpaceGetOrCreate(buffer);
			if(LoadAllUserNamespaces_Counter < 5)
				printf("  %s\n", buffer);
			else
				verbose_printf("  %s\n", buffer);
			LoadAllUserNamespaces_Counter += 1;
		}

		if (stricmp(node->name, ".svn")==0)
			return FSA_NO_EXPLORE_DIRECTORY;
		else
			return FSA_EXPLORE_DIRECTORY; 
	}
	else
	{
		return FSA_NO_EXPLORE_DIRECTORY; 
	}
}

AUTO_COMMAND ACMD_NAME(LoadAllUserNamespaces) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void fileLoadAllUserNamespaces(int ignored)
{
	LoadAllUserNamespaces_Counter = 0;
	printf("\nUserNamespaces:\n");
	fileScanAllDataDirs2(NAMESPACE_PATH, LoadAllUserNamespaces_CB, NULL);
}

static FileScanAction FillUserNamespaceNamesList_CB(const char* path, FolderNode *node, void *unused, void *pUserData)
{
	char buffer[MAX_PATH];

	if (node->name[0] != '_') 
	{ 
		if(strEndsWith(node->name, ".namespace"))
		{
			getFileNameNoExt(buffer, path);
			eaPush(&eaNameSpaceNames, allocAddString(buffer));
		}

		if (stricmp(node->name, ".svn")==0)
			return FSA_NO_EXPLORE_DIRECTORY;
		else
			return FSA_EXPLORE_DIRECTORY; 
	}
	else
	{
		return FSA_NO_EXPLORE_DIRECTORY; 
	}
}

static void FillUserNamespaceNamesList()
{
	char main_dir[MAX_PATH];
	loadstart_printf("Loading Name Space Names...");
	eaClear(&eaNameSpaceNames);
	sprintf(main_dir, "%s/%s", fileDataDir(), "ns");
	fileScanFoldersToDepth(main_dir, 1, FillUserNamespaceNamesList_CB, NULL);
	loadend_printf("done. (%d total)", eaSize(&eaNameSpaceNames));
}

const char** fileGetNameSpaceNameList()
{
	if(!eaNameSpaceNames)
		FillUserNamespaceNamesList();
	return eaNameSpaceNames;
}

AUTO_COMMAND ACMD_NAME(LoadUserNamespaces) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void fileLoadUserNamespaces(char *pchNamespaces)
{
	char *last, *str, *pchTemp = NULL;

	estrStackCreate(&pchTemp);

	estrCopy2(&pchTemp, pchNamespaces);

	printf("UserNamespaces:\n");
	str = strtok_r(pchTemp, ",", &last);
	while (str)
	{
		resNameSpaceGetOrCreate(str);
		printf("  %s\n", str);
		str = strtok_r(NULL, ",", &last);		
	}

	estrDestroy(&pchTemp);
}

void fileLoadGameDataDirAndPiggs(void)
{
	fileDataDir();

	DO_AUTO_RUNS_FILE
}

static int fileAddSearchPath(const char* path, bool newMainPath, const char *pathName)
{
	const char *newPath;
	char fixed_path[CRYPTIC_MAX_PATH];
	if(
        stricmp(path, "./")!=0 && 
#if _PS3
        stricmp(path, "/app_home/data")!=0 && 
#else
		stricmp(path, "./data")!=0 && 
#endif
#if _XBOX
        stricmp(path, "game:/")!=0 && 
		stricmp(path, "data:/")!=0 && 
#endif
        !dirExists(path)
    ) {
		if (
#if _PS3
            stricmp(path, "/app_home/piggs")!=0 && 
            stricmp(path, "/app_home/data")!=0 && 
            stricmp(path, "/app_home/localdata")!=0
#else
            stricmp(path, "./piggs")!=0 && 
            stricmp(path, "./data")!=0 &&
            stricmp(path, "./localdata")!=0
#endif
        )
			printf("Failed to add GameDataDir: %s (it does not exist or could not be accessed)\n", path);
		return 0;
	}

	// The last seen game data directory will be used as the
	// "main" game data directory.  All other directories will
	// be considered "local".
	strcpy(fixed_path, path);
	forwardSlashes(fixed_path);
	if (strEndsWith(fixed_path, "/")) {
		fixed_path[strlen(fixed_path)-1] = '\0';
	}
	newPath = allocAddFilename(fixed_path);
	eaPush(&eaGameDataDirs, newPath);
	eaPush(&eaGameDataDirNames, allocAddFilename(pathName));
	if (newMainPath) 
		mainGameDataDir = newPath;

	return 1;
}

const char * const * fileGetGameDataDirs(void)
{
	return eaGameDataDirs;
}

void fileAddNameSpace( char * path, char * path_name)
{
	assertmsg(path_name[1] != ':', "No one character path names allowed");
	eaPush(&eaExtraGameDataDirs, allocAddFilename(path));
	eaPush(&eaExtraGameDataDirNames, allocAddFilename(path_name));
}

bool addAppropriateDataDirs(const char *path) // Input is like "C:/game"
{
	static char fn[CRYPTIC_MAX_PATH+20];
	
	sprintf(fn, "%s/data", path);
	if (!dirExists(fn)) {
		return false;
	}
	sprintf(fn, "%s/tools", path);
	if (!dirExists(fn)) {
		return false;
	}
	// Found it!
	verbose_printf("AutoDataDir: Found relative data folder %s/data\n", path);

	sprintf(fn, "%s/localdata", path); //This will be tried first
	if (dirExists(fn))
		fileAddSearchPath(fn, false, "localdata");
	sprintf(fn, "%s/serverdata", path); //This is the backup
	if (dirExists(fn))
		fileAddSearchPath(fn, false, "localdata");

	sprintf(fn, "%s/data", path);
	assert(eaSize(&gameDataDirOverrides) == 0);
	eaPush(&gameDataDirOverrides, fn);
	return true;
}

static bool g_use_fix_dir=false;
// Use the "fix" version of the data files regardless of whether or not we look like we're in fix
AUTO_CMD_INT(g_use_fix_dir, fix) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

// Recursively searches up from a given path for a likely data dir
// Destroys path input
static bool searchForDataDir(char *path)
{
	const char* slashBin = "/bin";
	forwardSlashes(path);
	while (true) {
		char *s;
		if (addAppropriateDataDirs(path)) {
			return true;
		}
		if (strEndsWith(path, slashBin)){
			path[strlen(path) - strlen(slashBin)] = 0;
			
			if(s = strrchr(path, '/')){
				char *s2;
				char buf2[CRYPTIC_MAX_PATH];
				char buf[CRYPTIC_MAX_PATH];
					
				s++;
				
				// If path has "X:/foo/*/bin/" in it, use X:/*.

				sprintf(buf, "%c:/%s%s", path[0], s, (g_use_fix_dir||strstri(path, "fix")) ? "fix" : "");
				
				if(addAppropriateDataDirs(buf)){
					// Also add C:/src/data if it exists
					strcpy(buf, path);
					s = strrchr(buf, '/');
					*s='\0';
					strcat(buf, "/data");
					if (dirExists(buf)) {
						fileAddSearchPath(buf, true, NULL);
					}
					return true;
				}

				// If path has "X:/foo/*/bin/" in it, use X:/foo/*.
				strcpy(buf2, path);
				s2 = strrchr(buf2, '/');
				if (s2)
				{
					*s2 = '\0';
					s2 = strrchr(buf2, '/');
				}
				if (s2)
				{
					*s2 = '\0';
					sprintf(buf, "%s/%s%s", buf2, s, (g_use_fix_dir||strstri(path, "fix")) ? "fix" : "");

					if(addAppropriateDataDirs(buf)){
						// Also add C:/src/data if it exists
						strcpy(buf, path);
						s = strrchr(buf, '/');
						*s='\0';
						strcat(buf, "/data");
						if (dirExists(buf)) {
							fileAddSearchPath(buf, true, NULL);
						}
						return true;
					}
				}

				// Try C: instead
				sprintf(buf, "c:/%s%s", s, (g_use_fix_dir||strstri(path, "fix")) ? "fix" : "");

				if(addAppropriateDataDirs(buf)){
					// Also add C:/src/data if it exists
					strcpy(buf, path);
					s = strrchr(buf, '/');
					*s='\0';
					strcat(buf, "/data");
					if (dirExists(buf)) {
						fileAddSearchPath(buf, true, NULL);
					}
					return true;
				}
			}
			
			path[strlen(path)] = '/';
		}
		if (s = strrchr(path, '/')) {
			*s = '\0';
		} else {
			break;
		}
	}
	return false;
}

bool file_disable_auto_data_dir=false;
void fileDisableAutoDataDir(void)
{
	gbFolderCacheModeChosen = true;
	file_disable_auto_data_dir = true;
}

// Rules:
//  1) gamedatadir.txt in current, non-root, folder overrides everything
//  2) look for a data/ folder in one of our parent folders (only for art tools)
//  3) look for a data/ folder in one of the partent folders of where
//        this .exe is located (general case for art/design)
//  4) use hard-coded programmer lookup table
//  5) use old c:\gamedatadir.txt (should no longer be used)
static void findAutoDataDir(bool searchByPath)
{
#if _PS3
	if (fileExists("/app_home/gamedatadir_ps3.txt"))
	{
		return;
	}
	addAppropriateDataDirs("/app_home/");
#elif _XBOX
	if (fileExists("game:/gamedatadir.txt"))
	{
		return;
	}
	addAppropriateDataDirs("game:/");
#else
	char path[CRYPTIC_MAX_PATH];
	path[0] = 0;
	if (!fileIsUsingDevData() || file_disable_auto_data_dir)
		return;
	fileGetcwd(path, ARRAY_SIZE(path)-2);
	if (fileExists("./gamedatadir.txt") && !strEndsWith(path, ":\\")) {
		// Load normally
		verbose_printf("AutoDataDir: Using ./gamedatadir.txt in current folder\n");
	} else {
		bool foundIt=false;
		// Try to find data dir via. current path
		if (searchByPath) {
			foundIt = searchForDataDir(path);
		}
		if (!foundIt) {
			strcpy(path, getExecutableName());
			foundIt = searchForDataDir(path);
		}
#if 0 // This stuff is all ignored because of searchForDataDir, I think.
		if (!foundIt) {
			int i;
			static struct {
				char *src;
				char *data;
			} programmer_paths[] = {
				{"C:/src/Utilities/bin", "C:/game"},
				{"C:/src/TestSuites/bin", "C:/FightClub"},
				{"C:/src/*/bin", "C:/%s"},
				{"C:/fix/*/bin", "C:/%sFix"},
			};
			strcpy(path, getExecutableName());
			forwardSlashes(path);
			getDirectoryName(path);
			for (i=0; i<ARRAY_SIZE(programmer_paths) && !foundIt; i++) {
				if (simpleMatch(programmer_paths[i].src, path)) {
					static char buf[CRYPTIC_MAX_PATH];
					sprintf(buf, programmer_paths[i].data, getLastMatch());
					foundIt = addAppropriateDataDirs(buf);
					if (foundIt) {
						char *s;
						// Also add C:/src/data if it exists
						strcpy(buf, path);
						if (s = strrchr(buf, '/'))
							*s='\0';
						if (s = strrchr(buf, '/'))
							*s='\0';
						strcat(buf, "/data");
						if (dirExists(buf)) {
							fileAddSearchPath(buf, true);
						}
					}
				}
			}
		}
#endif
		if (!foundIt) {
			verbose_printf("Could not find appropriate game data dir");
		}
	}
#endif
}

//code to check that AUTO_RUN is working, to ensure that no project doesn't have StructParser set up
static bool sbIsAutoRunWorking = false;

AUTO_RUN_FIRST;
int AutoRunAssertThingThatHadDarnWellBetterBeRun(void)
{
	sbIsAutoRunWorking = true;
	return 0;
}


#if _XBOX
void initXboxDrives(void)
{
	static bool inited=false;
	if (!inited) {
		int file_cache_size = 128*1024; // 2 clusters.  Tweak this for better performance?  Do we need caching at all with the way we read?
		DWORD ret;
		HRESULT result;
		
		// No threads, so this is good enough
		inited = true;

		// Mount the utility drive for use with cache files
		memMonitorTrackUserMemory("cache:/ file cache", 1, file_cache_size + 4032, MM_ALLOC);
		ret = XMountUtilityDrive(FALSE, 64*1024, file_cache_size);
		if (ret != ERROR_SUCCESS) {
			devassert(!"Failed to mount cache:/ partition");
		}

		// Mount the devkit drive
		result = DmMapDevkitDrive();
		memMonitorTrackUserMemory("devkit:/ drive", 1, 4096, MM_ALLOC);
		if (result != S_OK) {
			devassert(!"Failed to mount devkit:/ partition");
		}

		// Mount the main data drive
		if(isProductionMode() && (GetAppGlobalType() == GLOBALTYPE_CLIENT || GetAppGlobalType() || GLOBALTYPE_XBOXPATCHER))
		{
			DWORD err, bytes_needed, count, disp, license;
			ULARGE_INTEGER size;
			XCONTENTDEVICEID devID = 0;
			XOVERLAPPED ol = {0};
			HANDLE contentEnum;
			void *buf;
			XCONTENT_DATA data = {0};
			bool found_data = false;

			err = XContentCreateEnumerator(XUSER_INDEX_NONE, XCONTENTDEVICE_ANY, XCONTENTTYPE_SAVEDGAME, XCONTENTFLAG_NONE, 1, &bytes_needed, &contentEnum);
			if(err == ERROR_NO_MORE_FILES)
			{
				// XXX: This should show a message box to users. <NPK 2009-11-19>
				assert(0);
			}
			assert(err == ERROR_SUCCESS);
			buf = malloc(bytes_needed);
			while(XEnumerate(contentEnum, buf, bytes_needed, &count, NULL) == ERROR_SUCCESS)
			{
				XCONTENT_DATA *d = buf;
				if(stricmp(d->szFileName, "fcdata")==0)
				{
					memcpy(&data, d, sizeof(XCONTENT_DATA));
					found_data = true;
				}
			}
			XCloseHandle(contentEnum);
			free(buf);

			// No existing data file found, need to make a new one so we need a device ID
			if(!found_data)
			{
				size.QuadPart = 4LL*1024LL*1024LL*1024LL;
				ol.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
				err = XShowDeviceSelectorUI(0, XCONTENTTYPE_SAVEDGAME, XCONTENTFLAG_NONE, size, &devID, &ol);
				assert(err == ERROR_IO_PENDING);
				// Using NULL for the ol in XShowDeviceSelectorUI doesn't work (asserts) so this is basically the same thing.
				while(!XHasOverlappedIoCompleted(&ol)) { Sleep(100); }
				err = XGetOverlappedResult(&ol, NULL, FALSE);
				assert(err == ERROR_SUCCESS);
				CloseHandle(ol.hEvent);

				data.DeviceID = devID;
				data.dwContentType = XCONTENTTYPE_SAVEDGAME;
				wcscpy(data.szDisplayName, L"Champions Online Game Data");
				strcpy(data.szFileName , "fcdata");
			}

			err = XContentCreate(XUSER_INDEX_NONE, "data", &data, XCONTENTFLAG_OPENALWAYS, &disp, &license, NULL);
			assert(err == ERROR_SUCCESS);
			//eaPush(&gameDataDirOverrides, "data:/");
		}
	}
}
#endif

#if !PLATFORM_CONSOLE
void fileGetCrypticSettingsDir(char* dirOut, S32 dirSize)
{
	char *pDir = NULL;
	
	if(!GetEnvironmentVariable_UTF8("crypticsettingsdir", &pDir)){
		if(!GetEnvironmentVariable_UTF8("windir", &pDir)){
			estrCopy2(&pDir, "c:/CrypticSettings");
		}else{
			assert(estrLength(&pDir) >= 3);
			pDir[0] = tolower(pDir[0]);
			estrSetSize(&pDir, 3);
			assert(pDir[0] >= 'a' && pDir[0] <= 'z' && pDir[1] == ':' && isSlash(pDir[2]));
			pDir[2] = '/';
			estrConcatf(&pDir, "CrypticSettings");
		}
	}
	
	forwardSlashes(pDir);
	
	while(strEndsWith(pDir, "/")){
		pDir[strlen(pDir) - 1] = 0;
	}
	
	strcpy_s(dirOut, dirSize, pDir);
	estrDestroy(&pDir);
}

void fileGetCrypticSettingsFilePath(char* pathOut, S32 pathSize, const char* fileName)
{
	char settingsDir[MAX_PATH];
	char wrongPath[MAX_PATH];
	char path[MAX_PATH];
	
	fileGetCrypticSettingsDir(SAFESTR(settingsDir));
	
	sprintf(wrongPath, "c:/%s", fileName);
	
	sprintf(path, "%s/%s", settingsDir, fileName);
	
	if(	fileExists(wrongPath) &&
		!fileExists(path))
	{
		printfColor(COLOR_RED|COLOR_BRIGHT, "Copying %s to %s\n", wrongPath, path);
	
		makeDirectories(settingsDir);
		
		fileCopy(wrongPath, path);
	}
	
	strcpy_s(pathOut, pathSize, path);
}
#endif

const char *fileCoreDataDir(void)
{
	static char coreDataFolder[CRYPTIC_MAX_PATH];
	static bool inited=false;
	if (!fileIsUsingDevData()) {
		return NULL; //Nothing special here!
	}

	if (!inited) {
		int i;
		bool found = false;

		inited = true;

		for (i = 0; i < eaSize(&eaGameDataDirs); ++i) {
			if (strEndsWith(eaGameDataDirs[i], "Core/data") || strEndsWith(eaGameDataDirs[i], "/CoreData")) {
				assert(!found);
				strcpy(coreDataFolder, eaGameDataDirs[i]);
				found = true;
			}
		}

		if (!found) {
			strcpy(coreDataFolder, "");
		}

#if !PLATFORM_CONSOLE
		if (!found)
		{
			int coreBranch;
			char projectCoreDataFolder1[CRYPTIC_MAX_PATH];
			char projectCoreDataFolder2[CRYPTIC_MAX_PATH];
			char globalCoreDataFolder[CRYPTIC_MAX_PATH];
			char buf[MAX_PATH];
			char *s;

			triviaPrintf("GimmeBranchNumber", "%d", gimmeDLLQueryBranchNumber(mainGameDataDir));

			sprintf(buf, "%s/piggs/no_core.txt", mainGameDataDir);
			if (fileExists(buf))
			{
				// Do not want any kind of Core, it has been copied into the project
				triviaPrintf("GimmeCoreBranchNumber", "N/A");
			} else {

				// Find the right Core folder
				// Use a config file to determine the right branch
				coreBranch = gimmeDLLQueryCoreBranchNumForDir(mainGameDataDir);
				triviaPrintf("GimmeCoreBranchNumber", "%d", coreBranch);
				// Check C:\Core\data and C:\Project\CoreData\ 
				strcpy(globalCoreDataFolder, mainGameDataDir);
				forwardSlashes(globalCoreDataFolder);
				s = strrchr(globalCoreDataFolder, '/');
				if (s)
					*s = '\0';
				strcpy(projectCoreDataFolder1, globalCoreDataFolder);
				s = strrchr(globalCoreDataFolder, '/');
				if (s)
					*s = '\0';
				strcat(globalCoreDataFolder, "/Core/data");
				strcpy(projectCoreDataFolder2, projectCoreDataFolder1);
				strcat(projectCoreDataFolder2, "Core/data");
				strcat(projectCoreDataFolder1, "/CoreData");

				if (strstri(mainGameDataDir, "fix/") && dirExists(projectCoreDataFolder2) && gimmeDLLQueryBranchNumber(projectCoreDataFolder2)==coreBranch)
				{
					// We're running from c:\ProjectFix and there's a C:\ProjectFixCore\, use it!
					strcpy(coreDataFolder, projectCoreDataFolder2);
				} else if (dirExists(globalCoreDataFolder) && (coreBranch == GIMME_BRANCH_UNKNOWN ||
					gimmeDLLQueryBranchNumber(globalCoreDataFolder)==coreBranch))
				{
					// Good!  Or we want the tip and this must be it
					strcpy(coreDataFolder, globalCoreDataFolder);
				} else if (dirExists(projectCoreDataFolder2) && gimmeDLLQueryBranchNumber(projectCoreDataFolder2)==coreBranch)
				{
					// C:\ProjectCore\Data is good!
					strcpy(coreDataFolder, projectCoreDataFolder2);
				} else if (dirExists(projectCoreDataFolder1) && gimmeDLLQueryBranchNumber(projectCoreDataFolder1)==coreBranch)
				{
					// C:\Project\CoreData is good!
					strcpy(coreDataFolder, projectCoreDataFolder1);
				} else if (coreBranch == GIMME_BRANCH_UNKNOWN || !gimmeDLLQueryAvailable()) {
					// No Gimme
					if (dirExists(projectCoreDataFolder1)) {
						strcpy(coreDataFolder, projectCoreDataFolder1);
					} else if (dirExists(projectCoreDataFolder2)) {
						strcpy(coreDataFolder, projectCoreDataFolder2);
					} else if (dirExists(globalCoreDataFolder)) {
						strcpy(coreDataFolder, globalCoreDataFolder);
					} else {
						// No core data folder, no gimme.
						strcpy(coreDataFolder, "");
					}
				} else {
					// Bad! (and they have gimme, must be internal!)
					if (isDevelopmentMode() && !g_force_absolute_paths) {
						FatalErrorf("Could not find appropriate Core data folder!  One of\n \"%s\" or\n \"%s\" or\n \"%s\" \nneeds to be on branch #%d\nPlease run the appropriate branch setup script in /tools/Branches.",
							globalCoreDataFolder, projectCoreDataFolder1, projectCoreDataFolder2, coreBranch);
					}
					strcpy(coreDataFolder, "");
				}
				forwardSlashes(coreDataFolder);
			}
		}
#endif
	}
	return coreDataFolder[0]?coreDataFolder:NULL;
}

const char *fileCoreSrcDir(void)
{
	static char coreSrcFolder[CRYPTIC_MAX_PATH] = "";
	static bool inited=false;
	if (!inited) {
		inited = true;
		if (fileCoreDataDir()) {
			strcpy(coreSrcFolder, fileCoreDataDir());
			strcat(coreSrcFolder, "/");
			strstriReplace(coreSrcFolder, "/data/", "/src/");
			strstriReplace(coreSrcFolder, "/CoreData/", "/CoreSrc/");
			while (strEndsWith(coreSrcFolder, "/")) {
				coreSrcFolder[strlen(coreSrcFolder)-1] = '\0';
			}
		}
	}
	return coreSrcFolder[0]?coreSrcFolder:NULL;
}

const char *fileCoreToolsBinDir(void)

{
      static char coreToolsBinFolder[CRYPTIC_MAX_PATH] = "";
      static bool inited=false;

      if (!inited) {
            inited = true;
            if (fileCoreDataDir()) {
                  strcpy(coreToolsBinFolder, fileCoreDataDir());
                  strcat(coreToolsBinFolder, "/");
                  strstriReplace(coreToolsBinFolder, "/data/", "/tools/bin/");
                  strstriReplace(coreToolsBinFolder, "Core/Data/", "Core/Tools/bin/");
                  while (strEndsWith(coreToolsBinFolder, "/")) {
                        coreToolsBinFolder[strlen(coreToolsBinFolder)-1] = '\0';
                  }
			} else {
				char *s;
				strcpy(coreToolsBinFolder, fileDataDir());
				s = strchr(coreToolsBinFolder, '/');
				if (!s)
				{
					coreToolsBinFolder[0] = 0;
				} else {
					s++;
					*s = 0;
					strcat(coreToolsBinFolder, "Core/tools/bin");
					if (!dirExists(coreToolsBinFolder))
					{
						coreToolsBinFolder[0] = 0;
					}
				}
			}
      }
      return coreToolsBinFolder[0]?coreToolsBinFolder:fileToolsBinDir();
}

const char *fileCrypticToolsBinDir(void)
{
	static char crypticToolsBinFolder[CRYPTIC_MAX_PATH] = "";

	if (!crypticToolsBinFolder[0])
	{
		char *pTempEString = NULL;
		estrStackCreate(&pTempEString);
		estrCopy2(&pTempEString, fileCoreToolsBinDir());
		estrReplaceOccurrences_CaseInsensitive(&pTempEString, "core", "cryptic");
		strcpy(crypticToolsBinFolder, pTempEString);
		estrDestroy(&pTempEString);
	}

	return crypticToolsBinFolder;
}

 


// Adds the appropriate Core data folder based on what branch we're on
void fileAddCoreDataFolder(void)
{
	int i;

	// First, check to see if there's already a Core in there, if so, do nothing
	for (i=eaSize(&eaGameDataDirs)-1; i>=0; i--) {
		if (strstriConst(eaGameDataDirs[i], "Core")) {
			return;
		}
	}

	// If not, we want to insert Core as the bottommost (easiest to override) in
	//  the list.
	if (fileCoreDataDir()) {
		// Now, add it to the list
		fileAddSearchPath(fileCoreDataDir(), false, "Core");
	}
}

bool hasGameDataDirWithCoreHoggs(void)
{
	// Checks for a core hogg file so apps like the outsource build don't get confused looking for a second version of core
	FOR_EACH_IN_EARRAY(eaGameDataDirs, const char, datadir)
	{
		char buf[MAX_PATH];
		FWStatType sbuf;
		sprintf(buf, "%s/piggs/core.hogg", datadir);
		if (fwStat(buf, &sbuf)==0)
			return true;
		sprintf(buf, "%s/piggs/coreData.hogg", datadir);
		if (fwStat(buf, &sbuf)==0)
			return true;
		sprintf(buf, "%s/piggs/coreXboxCoreData.hogg", datadir);
		if (fwStat(buf, &sbuf)==0)
			return true;
	}
	FOR_EACH_END;
	return false;
}

/* Function fileLoadFolderCache
 *  Loads the FolderCache for real.
 */
void fileLoadFolderCache()
{
	int i;

	if(!folder_cache)
	{
		folder_cache = FolderCacheCreate();
	}

	if (!g_force_absolute_paths)
	{
		for (i=eaSize(&eaGameDataDirs)-1; i>=0; i--) {
			// Insert in reverse order, since we want the priority to be higher for the
			// first entries in gamedatadir.txt
			FolderCacheAddFolder(folder_cache, eaGameDataDirs[i], eaSize(&eaGameDataDirs)-1-i, eaGameDataDirNames[i],
				fileCoreDataDir()?(stricmp(fileCoreDataDir(), eaGameDataDirs[i])==0):false);
		}

		if (FolderCacheGetMode() != FOLDER_CACHE_MODE_FILESYSTEM_ONLY) {
			loadstart_printf("Loading hoggs...");
			loadstart_report_unaccounted(true);
		}
		FolderCacheAddAllPigs(folder_cache); // internally checks FolderCacheGetMode() != FOLDER_CACHE_MODE_FILESYSTEM_ONLY
		if (FolderCacheGetMode() != FOLDER_CACHE_MODE_FILESYSTEM_ONLY)
			loadend_printf(" done (%d hoggs)", PigSetGetNumPigsNoLoad());
	}
}

//can be useful at startup time to determine whether file access is legal or not yet
bool fileLoadedDataDirs(void)
{
	return loadedGameDataDirs;
}

/* Function fileLoadFolderCacheShared
 *	Loads the FolderCache and data dirs from shared memory if available. Then copies
 *  the data dir names from that. Also stores the name of the main data dir, so we
 *  don't have to guess which one it was.
 */
bool fileLoadFolderCacheShared()
{
	SharedMemoryHandle *sm_handle = NULL;
	SM_AcquireResult sm_result;
	const char **sm_chunk;
	int i;
	const char *cache_name = STACK_SPRINTF("%sSharedFolderCache", GlobalTypeToName(GetAppGlobalType()));

	// Call PigSetInit to load all pigs into/from shared memory before doing this
	PigSetInit();

	// Now acquire the chunk - we should be able to safely load the FolderCache after doing this
	sm_result = sharedMemoryAcquire(&sm_handle, cache_name);

	if(sm_result == SMAR_FirstCaller)
	{
		// Load the FolderCache - there should be no horrible shared memory errors here
		fileLoadFolderCache();

		// Now we set our size to the exact size of the FolderCache
		sm_chunk = sharedMemorySetSize(sm_handle, FolderCacheGetMemoryUsage(folder_cache)+sizeof(char *));

		// Unregister from DirMon, because we can't handle any callbacks
		for(i=eaSize(&folder_cache->gamedatadirs)-1; i>=0; --i)
		{
			dirMonRemoveDirectory(NULL, folder_cache->gamedatadirs[i]);
		}

		// Store the main data dir for future reference
		sm_chunk[0] = mainGameDataDir;
		sharedMemorySetBytesAlloced(sm_handle, sizeof(sm_chunk[0]));

		// Move the FolderCache
		FolderCacheMoveToShared(folder_cache, sm_handle);

		// Success
		sharedMemoryUnlock(sm_handle);
		return true;
	}
	else if(sm_result == SMAR_DataAcquired)
	{
		FolderCache *sm_cache;
		assert(!folder_cache);

		// Load the main data dir from shared memory
		sm_chunk = sharedMemoryGetDataPtr(sm_handle);
		mainGameDataDir = sm_chunk[0];
		loadedGameDataDirs = 1;
		sm_cache = (FolderCache *)(&sm_chunk[1]);

		// Copy the FolderCache out and initialize non-shared stuff
		folder_cache = calloc(1, sizeof(FolderCache));
		memcpy(folder_cache, sm_cache, sizeof(FolderCache));
		if(folder_cache->htAllFiles)
		{
			folder_cache->htAllFiles = calloc(1, stashGetTableImpSize());
			memcpy(folder_cache->htAllFiles, sm_cache->htAllFiles, stashGetTableImpSize());
		}
		InitializeCriticalSection(&folder_cache->critSecAllFiles);

		// Read lists of data dirs out of FolderCache here
		// This step should now be unnecessary
// 		for(i=eaSize(&folder_cache->gamedatadirs)-1; i>=0; --i)
// 		{
// 			eaPush(&eaGameDataDirs, folder_cache->gamedatadirs[i]);
// 			eaPush(&eaGameDataDirNames, folder_cache->gamedatadir_names[i]);
// 		}

		// Success
		return true;
	}
	else
	{
		// Fall through to the normal way of doing it
		return false;
	}
}

/* Function fileLoadDataDirs
 *	Loads a list of possible directories from c:\gamedatadir.txt.  It is assumed
 *	that each directory occupies an entire line.  This list is passed to the
 *  FolderCache.  The last directory listed in the file is
 *	assumed to be the main game directory, which will be returned by fileDataDir()
 *	to maintain compatibility for some utility programs.  All other directories
 *	are considered "local".
 *
 *	VAS 07/25/09 - Moved the actual data dir parsing into a separate function for
 *	easier use by the shared FolderCache loading.
 *	
 *	Note that the environmental variables are now ignored.  While it is possible
 *	to parse a list of directory names that are all concatenated on the same line,
 *	it is not implemented.
 */
void fileLoadDataDirs(int forceReload)
{
	static bool did_auto_data_dir=false;
	FILE *file;
	int i;
	char	buffer[1024];
	int verbose = 0;
	
	PERFINFO_AUTO_START_FUNC();

	fileGetcwd(SAFESTR(g_cwdAtFileDataDirTime));
	
	if (!did_auto_data_dir && !eaSize(&gameDataDirOverrides)) {
		did_auto_data_dir = true;
		findAutoDataDir(true);
		if (loadedGameDataDirs) {
			if (FolderCacheGetMode()!=FOLDER_CACHE_MODE_FILESYSTEM_ONLY)
				filePrintDataDirs();
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	// Forcing reload?
	if(forceReload && eaSize(&eaGameDataDirs)){
		// Destroy all previously loaded directory names.
		eaSetSize(&eaGameDataDirs, 0);
		eaSetSize(&eaGameDataDirNames, 0);
	}

	assert(!loadedGameDataDirs);

	// Moving this up here so devkit reference can use it, and so the devkit doesn't get mapped twice
#if _XBOX
	initXboxDrives();
#endif
	if (eaSize(&gameDataDirOverrides))
	{
		size_t	len;
		for (i=0; i<eaSize(&gameDataDirOverrides); i++) 
		{
			strcpy(buffer, gameDataDirOverrides[i]);

			forwardSlashes(buffer);
			len = strlen(buffer) - 1;
			while (buffer[len]=='/' || buffer[len]==' ' || buffer[len]=='\t') buffer[len--]=0;
			if (strEndsWith(buffer, ":"))
				strcat(buffer, "/"); // drive:/ needs the trailing slash!

			fileAddSearchPath(buffer, true, (i==eaSize(&gameDataDirOverrides)-1)?"data":"override");
		}
	}
	else
	{
		if(fileIsUsingDevData() && !file_disable_auto_data_dir)
		{
			int		count = 0;
			// If not, assume that we're in development mode and
			// try to load a list data directory names from some text file.
#if _PS3
			file = fopen("/app_home/gamedatadir_ps3.txt","rt~");

			// Could set this to 1 if we want a printout about what's going on
			verbose = 1;

#elif !_XBOX
			file = fopen("./gamedatadir.txt","rt~");

			if (file) // New scheme, look for gamedatadir.txt in current directory
				verbose = 0; // Could set this to 1 if we want a printout about what's going on
			else
				file = fopen("c:/gamedatadir.txt","rt~");
#else
			file = fopen("game:/gamedatadir.txt","rt~");
			if (!file) {
				file = fopen("DEVKIT:/gamedatadir.txt","rt~");
			}
#endif

			if (file)
			{
				size_t	len;
				bool bMainIsCore=false;

				// Extract all directory names and add them to the string table.
				while(fgets(buffer,sizeof(buffer),file)){
					bool bIsCore = false;
					// Reformat the directory name.
					len = strlen(buffer) - 1;
					if (len <= 0 || buffer[0] == '#' || buffer[0] == '\n')
						continue;
					while (len > 0 && (buffer[len] == '\n' || buffer[len] == '\r'))
						buffer[len--] = 0;
					if (len <= 0)
						continue; 

					forwardSlashes(buffer);
					while (buffer[len]=='/' || buffer[len]==' ' || buffer[len]=='\t') buffer[len--]=0;

					if (strstri(buffer, "Core"))
						bIsCore = true;
					if (bIsCore && strstri(buffer, "localdata"))
						bMainIsCore = true;

					count += fileAddSearchPath(buffer, (bIsCore && eaSize(&eaGameDataDirs)!=0 && !bMainIsCore || strStartsWith(buffer, "//") && mainGameDataDir)?false:true, bIsCore?"core":"data");
				}
				fclose(file);
			} 

			// If the game data dir file was not found or if all paths listed
			// in the file is invalid...
			if(!count) {
#if _PS3
				fileAddSearchPath("./", true, "data");
#elif _XBOX
				fileAddSearchPath("game:/", true, "data");
#else
				fileAddSearchPath("./", true, "data");
#endif
			}
		}

		if (!eaSize(&eaGameDataDirs))
		{
#if _PS3
			fileAddSearchPath("/app_home/localdata", false, "localdata");
			fileAddSearchPath("/app_home/data", true, "data");
#else
			fileAddSearchPath("./localdata", false, "localdata");
			fileAddSearchPath("./data", true, "data");
#endif
		}

		if(!eaSize(&eaGameDataDirs)){
#if _PS3
			fileAddSearchPath("./", true, "data");
#elif _XBOX
			fileAddSearchPath("game:/", true, "data");
#else
			fileAddSearchPath("./", true, "data");
#endif
		}
	}

	if (!file_disable_auto_data_dir)
	{
		if (!hasGameDataDirWithCoreHoggs())
			fileAddCoreDataFolder();
		else
			printf("Skipping looking for Core folder: found a Core hogg in primary data dir.\n");
	}

	loadedGameDataDirs = 1; // So fileSpecialDir will work

	// Add in the extra game data dirs
	for (i=0; i <eaSize(&eaExtraGameDataDirs); i++) {
		char tmp_dir[MAX_PATH]={0};

		fileSpecialDir(eaExtraGameDataDirs[i], SAFESTR(tmp_dir));

		eaPush(&eaGameDataDirs, allocAddFilename(tmp_dir));
		eaPush(&eaGameDataDirNames, eaExtraGameDataDirNames[i]);
	}

#if !PLATFORM_CONSOLE
	if(!(/*IsClient() ||*/ GetAppGlobalType() == GLOBALTYPE_TESTCLIENT) || fileIsUsingDevData() || !stringCacheSharingEnabled() || !fileLoadFolderCacheShared())
#endif
	{
		fileLoadFolderCache();
	}

	// Initialize various file related code (non-thread safe inits)
	initPigFileHandles();
	initFileWrappers();

	//because this function is used by pretty much every project, it's a good place to put a check to
	//ensure that auto-run is working, ie, StructParser is set up for the current project, and the
	//project is calling DO_AUTO_RUNS
	assertmsg(sbIsAutoRunWorking, "AUTORUN is not working. This probably means that you need to call DO_AUTO_RUNS at application startup, or that StructParser is not turned on for the current project. Please turn it on. See confluence for details.");

	if (FolderCacheGetMode()!=FOLDER_CACHE_MODE_FILESYSTEM_ONLY)
		filePrintDataDirs();

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(xbProductionHack) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void xbProductionHack(int value)
{
#if _XBOX
	if (value) {
		char oldpath[MAX_PATH];
		char path[MAX_PATH];
		char *s;
		FILE *file = fopen("game:/gamedatadir.txt","rt~");
		oldpath[0] = 0;
		if (file)
		{
			size_t	len;
			int		count = 0;
			char buffer[MAX_PATH];
			while(fgets(buffer,sizeof(buffer),file)){
				// Reformat the directory name.
				len = strlen(buffer) - 1;
				if (len <= 0 || buffer[0] == '#' || buffer[0] == '\n')
					continue;
				if (len >= 0 && buffer[len] == '\n')
					buffer[len--] = 0;

				forwardSlashes(buffer);
				while (buffer[len]=='/' || buffer[len]==' ' || buffer[len]=='\t') buffer[len--]=0;
				if (!strstri(buffer, "Core") || !oldpath[0])
					strcpy(oldpath, buffer);
			}
			fclose(file);

			assert(oldpath[0]);

			g_xbox_production_hack = true;
			s = strrchr(oldpath, '/');
			assert(s);
			*s = '\0';
			sprintf(path, "%s/production_test", oldpath);
			assert(!eaSize(&gameDataDirOverrides));
			eaPush(&gameDataDirOverrides, strdup(path));
			//FolderCacheSetMode( FOLDER_CACHE_MODE_I_LIKE_PIGS );
			//fileLoadDataDirs(0);
		} else {
			printf("-xbProductionHack doesn't work without a gamedatadir.txt, which should have been automatically generated...\n");
		}
	}
#else
	assert(0);
#endif
}

AUTO_COMMAND ACMD_NAME(xbUseLocalHoggs) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void xbUseLocalHoggs(int value)
{
#if _XBOX
	if (value) {
		char oldpath[MAX_PATH];
		FILE *file = fopen("game:/gamedatadir.txt","rt~");
		oldpath[0] = 0;
		if (file)
		{
			size_t	len;
			int		count = 0;
			char buffer[MAX_PATH];
			assert(!eaSize(&gameDataDirOverrides));
			while(fgets(buffer,sizeof(buffer),file)){
				// Reformat the directory name.
				len = strlen(buffer) - 1;
				if (len <= 0 || buffer[0] == '#' || buffer[0] == '\n')
					continue;
				if (len >= 0 && buffer[len] == '\n')
					buffer[len--] = 0;

				forwardSlashes(buffer);
				while (buffer[len]=='/' || buffer[len]==' ' || buffer[len]=='\t') buffer[len--]=0;
				if (!strstri(buffer, "Core"))
				{
					eaPush(&gameDataDirOverrides, strdup(buffer));
					strcpy(oldpath, buffer);
				}
			}
			fclose(file);

			assert(oldpath[0]);
			if (eaSize(&gameDataDirOverrides)) {
				if (!strstri(gameDataDirOverrides[eaSize(&gameDataDirOverrides)-1], "/src"))
					free(eaPop(&gameDataDirOverrides)); // Remove data/ folder, leave overrides
			}

			g_xbox_local_hoggs_hack = true;
			eaPush(&gameDataDirOverrides, "game:/");
			//FolderCacheSetMode( FOLDER_CACHE_MODE_I_LIKE_PIGS );
			//fileLoadDataDirs(0);
		} else {
			printf("-xbUseLocalHoggs doesn't work without a gamedatadir.txt, which should have been automatically generated...\n");
		}
	}
#else
	assert(0);
#endif
}
void filePrintDataDirs(void)
{
	static bool printedOnce=false;
	if (printedOnce)
		return;
	printedOnce = true;
	FolderCache_PrintGDDs(folder_cache);
}

// New scheme: ignore c:\gamedatadir.txt and just using the current directory
//   to figure it out
// This is different than the standard behavior because it uses the working directory
//   instead of the path to the .exe
void fileAutoDataDir(void)
{
	assert(!loadedGameDataDirs);
	findAutoDataDir(true);
	// Auto failed, or gameDataDirOverrides was set above
	fileLoadDataDirs(0);
#if !PLATFORM_CONSOLE
	if (!gbSurpressStartupMessages)
		printf("Using game data dir: %s\n", mainGameDataDir);
#endif
}

// Returns the "main" game data directory if multiple are defined
const char *fileDataDir()
{
	if (!loadedGameDataDirs)
		fileLoadDataDirs(0);
	
	return mainGameDataDir;
}

int fileGetGimmeDataBranchNumber(void)
{
	return gimmeDLLQueryBranchNumber(fileDataDir());
}

const char *fileGetGimmeDataBranchName(void)
{
	return gimmeDLLQueryBranchName(fileDataDir());
}


void fileSpecialDir(const char *name, char *dest, size_t dest_size)
{
	char		*s;

	strcpy_s(dest, dest_size, fileDataDir()); 

	s = strrchr(dest,'/');
	if (s) // If not fileDataDir must be "."
		*s = 0;
	strcat_s(dest, dest_size, "/");
	strcat_s(dest, dest_size, name);
	mkdirtree(dest);
	forwardSlashes(dest);
}

bool fileMakeSpecialDir(bool makeIt, const char *name, char *dest, size_t dest_size)
{
	char		*s;
	char temp[CRYPTIC_MAX_PATH];

	strcpy_s(dest, dest_size, fileDataDir()); 

	s = strrchr(dest,'/');
	if (s) // If not fileDataDir must be "."
		*s = 0;
	strcat_s(dest, dest_size, "/");
	strcat_s(dest, dest_size, name);

	if (makeIt)
	{
		sprintf(temp, "%s/foo.txt", dest);
		mkdirtree(temp);
	}
	
	if (!dirExists(dest))
	{	//directory doesn't exist!
		return false;
	}

	forwardSlashes(dest);
	return true;
}

const char *fileBaseDir(void)
{
	static char base_dir[MAX_PATH] = {0};
	
	if(!base_dir[0])
	{
		char *s;

		strcpy(base_dir, fileDataDir()); 

		s = strrchr(base_dir,'/');
		if (s) // If not fileDataDir must be "."
			*s = 0;
		forwardSlashes(base_dir);
	}

	return base_dir;
}

const char *fileTempDir()
{
	static char tmp_dir[200]={0};
	if (!tmp_dir[0]) {
		fileSpecialDir("tmp", SAFESTR(tmp_dir));
	}
	return tmp_dir;
}

const char *fileLogDir(void)
{
	static char tmp_dir[200]={0};
	if (!tmp_dir[0]) {
		fileSpecialDir("logs", SAFESTR(tmp_dir));
	}
	return tmp_dir;
}

const char *fileSrcDir()
{
	static char tmp_dir[200]={0};
	if (!tmp_dir[0]) {
		fileSpecialDir("src", SAFESTR(tmp_dir));
	}
	return tmp_dir;
}

// Not a regular special dir, when running with -xbUseLocalHoggs, still load demos/save results to PC
const char *fileDemoDir(void)
{
	static char tmp_dir[200]={0};
	if (!tmp_dir[0]) {
		char		*s;
		strcpy(tmp_dir, fileLocalDataDir()); 

		s = strrchr(tmp_dir,'/');
		if (s) // If not fileDataDir must be "."
			*s = 0;
		strcat(tmp_dir, "/");
		strcat(tmp_dir, "demos");
		mkdirtree(tmp_dir);
		forwardSlashes(tmp_dir);
	}
	return tmp_dir;
}

const char *fileCacheDir()
{
#if _XBOX
	return "cache:";
	//return "hdd:";
#else
	static char tmp_dir[200];
	if (!tmp_dir[0]) {
		fileSpecialDir("cache", SAFESTR(tmp_dir));
		if (!dirExists(tmp_dir))
		{
			makeDirectories(tmp_dir);
			if (!dirExists(tmp_dir))
			{
				// Use something else
#if !_PS3
				char *pPath = NULL;
				verify(SHGetSpecialFolderPath_UTF8(NULL, &pPath, CSIDL_APPDATA, TRUE));
				sprintf(tmp_dir, "%s/%s/cache", pPath, GetProductName());
				estrDestroy(&pPath);

				forwardSlashes(tmp_dir);
				makeDirectories(tmp_dir);
				if (!dirExists(tmp_dir))
				{
					FatalErrorf("Error creating cache directory, perhaps installation folder is read-only or you are not running as an administrator");
				}
#else
				assert(0);
#endif
			}
		}
	}
	return tmp_dir;
#endif
}

char *fileToolsBinDir(void)
{
	static char tmp_dir[200];
	if (!tmp_dir[0]) {
		if (!fileIsUsingDevData()) {
			getExecutableDir(tmp_dir);
		} else {
			fileSpecialDir("tools/bin", SAFESTR(tmp_dir));
		}
	}
	return tmp_dir;
}

char *fileToolsDir(void)
{
	static char tmp_dir[200];
	if (!tmp_dir[0]) {
		if (!fileIsUsingDevData()) {
			getExecutableDir(tmp_dir);
		} else {
			fileSpecialDir("tools", SAFESTR(tmp_dir));
		}
	}
	return tmp_dir;
}

char *fileExecutableDir(void)
{
#if PLATFORM_CONSOLE
	return fileToolsBinDir();
#else
	static char path[CRYPTIC_MAX_PATH]="";
	if (!fileIsUsingDevData())
	{
		return fileToolsBinDir();
	}
	
	fileGetcwd(path, ARRAY_SIZE(path));

	if (!simpleMatch("*/tools/bin",path))
	{
		// Programmers
		return path;
	}
	else
	{
		// Art/Design/C:\Project\tools\bin\ 
		return fileToolsBinDir();
	}
#endif
}


char *fileCoreExecutableDir(void)
{
	static char path[CRYPTIC_MAX_PATH]="";
	if (!fileIsUsingDevData())
	{
		return fileToolsBinDir();
	}

	getExecutableDir(path);
	
	if (simpleMatch("*/Core/bin",path))
	{
		// This is already in core
		return path;
	}

	if (!simpleMatch("*/tools/bin",path))
	{
		strcat(path,"/../../Core/bin");
		return path;
	}
	else
	{
		return fileToolsBinDir();
	}
}



const char *fileLocalDataDir(void)
{
	int i;

	for (i=eaSize(&eaGameDataDirs)-1; i>=0; i--) {
		if (strstriConst(eaGameDataDirs[i], "localdata") || strstriConst(eaGameDataDirs[i], "serverdata")) // override named localdata
		{
			return eaGameDataDirs[i];
		}
	}

	{
		static char tmp_dir[200];
		if (!tmp_dir[0]) {
			fileSpecialDir("localdata", SAFESTR(tmp_dir));
		}
		return tmp_dir;
	}
}

char *fileFixUpName(const char *src,char * tgt)
{
const char	*s;
char    *t;

	for(s=src, t=tgt ; *s ; s++,t++)
	{
		*t = toupper(*s);
		if (*s == '\\')
			*t = '/';
	}
	*t = 0;
	if(src[0] == '/')
		return tgt + 1;
	else
		return tgt;
}

// If data is NULL, it loads the data from disk
// If data is NULL and the file does not exist on disk, it will remove it from the hogg
// Takes ownership of data
void fileUpdateHoggAfterWrite(const char *relpath, void *data, U32 data_len)
{
#if !PLATFORM_CONSOLE
	HogFile *hog_file;
	char fullpath[MAX_PATH];
	assert(!fileIsAbsolutePath(relpath));
	assert(strStartsWith(relpath, "bin/") || strStartsWith(relpath, "server/bin/"));
	fileLocateWrite("piggs/bin.hogg", fullpath);
	// Not actually opening/closing a hogg, just getting a reference to it.
	hog_file = hogFileRead(fullpath, NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	if (hog_file) 
	{
		if (!data) {
			FolderCacheForceUpdate(folder_cache, relpath);
			data = fileAlloc(relpath, &data_len);
			// Might be NULL, in which case it's removed
		}
		hogFileModifyUpdateNamed(hog_file, relpath, data, data_len, fileLastChanged(relpath), NULL);
		hogFileDestroy(hog_file, true);
	} else {
		assertmsg(0, "Error opening bin.hogg");
	}
#else
	free(data);
#endif
}


char* fileFixName(char* src, char* tgt)
{
	char	*s;
	char    *t;

	for(s=src, t=tgt ; *s ; s++,t++)
	{
		if (*s == '\\')
			*t = '/';
	}
	*t = 0;
	if(src[0] == '/')
		return tgt + 1;
	else
		return tgt;
}

// Pigged paths look like "c:/game/data/piggs/file.pigg:/myfile.txt" or "./piggs/file.pigg:/myfile.txt"
// Or #file.hogg#file.ext for "dynamic" hoggs
int is_pigged_path(const char *path) {
	if (!path) return 0;
	if (path[0] == '#') {
		return true;
    }
#if _PS3
    if(path[0]=='.' || path[1]==':' && (path[0]=='c' || path[0]=='C'))
        path++;
    return strchr(path, ':')!=0;
#else
	if (path[0]=='.') {
		return strchr(path, ':')!=0;
	} else {
		return strchr(path, ':')!=strrchr(path, ':');
	}
#endif
}

// Returns dest
// Removes any absolute pathing if it's one of the game data dirs
// If not an absolute path, simply cleans up the path name
// Result will not start with a slash
char *fileRelativePath_s(const char *fname, char *dest, size_t dest_size)
{
	int i;
	const char *s;
	// Taking an absolute path, remove whatever gameDataDir prefix it might have and get to a relative path
	strcpy_s(dest, dest_size, fname);
	forwardSlashes(dest);
	if(g_force_absolute_paths)
		return dest;

	if (!fileIsAbsolutePath(dest)) {
		s = dest;
		while (*s=='/' || *s=='\\')
			s++;
		strcpy_s(dest, dest_size, s);
	} else {
		for (i=eaSize(&eaGameDataDirs)-1; i>=0; i--) {
			if (strEndsWith(eaGameDataDirs[i], "localdata"))
				continue;
			if (strStartsWith(dest, eaGameDataDirs[i])) {
				s = dest + strlen(eaGameDataDirs[i]);
				while (*s=='/' || *s=='\\')
					s++;
				strcpy_s(dest, dest_size, s);
			}
		}
	}
	if (s = strstriConst(dest, ".pigg:/")) {
		strcpy_s(dest, dest_size, s + strlen(".pigg:/"));
	}
	return dest;
}

bool fileIsInDataDirs(const char *fname)
{
	int i;
	char local_fname[CRYPTIC_MAX_PATH];
	if (!fileIsAbsolutePath(fname))
		return true;

	strcpy(local_fname, fname);
	forwardSlashes(local_fname);

	for (i=eaSize(&eaGameDataDirs)-1; i>=0; i--) {
		if (strStartsWith(local_fname, eaGameDataDirs[i])) {
			return true;
		}
	}
	return false;
}

void fileAllPathsAbsolute(bool newvalue)
{
	g_force_absolute_paths = newvalue;
	gbFolderCacheModeChosen = true;
}

// Override this to get notification of relative paths.  This is mainly for debugging use.
LATELINK;
void fileHandleRelativePath(const char *path);
void DEFAULT_LATELINK_fileHandleRelativePath(const char *path)
{
}

int fileIsAbsolutePath(const char *path)
{
	int absolute;
	if (g_force_absolute_paths)
		return 1;
	absolute = fileIsAbsolutePathInternal(path);
	if (!absolute)
		fileHandleRelativePath(path);
	return absolute;
}

int fileIsAbsolutePathInternal(const char *path)
{
	if (!path)
		return 0;
	if(	path[0] &&
#if _PS3
        path[0]=='/'
#else
		(	path[1]==':' ||
			(	path[0]=='.' &&
				!isalnum(path[1])) || // ./ .\\ .. .\0
			(	path[0]=='\\' &&
				path[1]=='\\') ||
			(	path[0]=='/' &&
				path[1]=='/') ||
			path[0] == '#' ||
			path[0] == '@'			// Marker specifically used to indicate a fake file
		)
#endif
    ) {
		return 1;
	}
#if _XBOX
	//Xbox has multi-letter drives
	{
		char *colon = strchr(path,':');
		char *bs = strchr(path,'\\');
		if (colon && (!bs || colon < bs))
		{
			// If the first colon is before the first slash, it's probably a drive
			return 1;
		}
	}
#endif
	return 0;
}

int fileGetNameSpacePath_s(const char *path, char *ns, size_t ns_size, char *relpath, size_t relpath_size)
{
#if PLATFORM_CONSOLE
	// No namespaces paths on xbox
	return 0;
#else
	{
		char *colon = strchr(path,':');

		if (colon && (colon == path || colon == path + 1))
		{
			// no one letter or 0 letter namespaces, they look like drives
			return 0;
		}
		else if(colon && strStartsWith(path, "net:"))
		{
			// also net:/ for network paths (net:/smb/ and net:/cryptic/)
			return 0;
		}
		// colon followed by slash, it's a namespace
		else if (colon && (colon[1] == '/' || colon[1] == '\\'))
		{
			if (ns)
				strncpy_s(SAFESTR2(ns), path, colon - path);
			if (relpath)
				strcpy_s(SAFESTR2(relpath), colon + 1);
			return 1;
		}
		else if(strStartsWith(path, NAMESPACE_PATH))
		{
			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			// This will not work with a namespace containing a / character
			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			char *slash, *backslash;
			path += strlen(NAMESPACE_PATH);
			slash = strchr(path, '/');
			backslash = strchr(path, '\\');
			if(!slash || (backslash && backslash < slash))
				slash = backslash;
			if(slash)
			{
				if (ns)
					strncpy_s(SAFESTR2(ns), path, slash - path);
				if (relpath)
					strcpy_s(SAFESTR2(relpath), slash);
				return 1;
			}
		}
	}
	return 0;
#endif
}

// convert a relative path to a namespace relative path.
char *fileNameSpacePath_s(const char *path, const char *ns, char *out, size_t out_size)
{
    
#if PLATFORM_CONSOLE
	// No namespaces paths on xbox
	out[0] = '\0';
	return 0;
#endif
	assert(!fileIsAbsolutePath(path));
	assert(!fileIsNameSpacePath(path));
	sprintf_s(SAFESTR2(out), NAMESPACE_PATH"%s/%s", ns, path);
	return out;
}


static int fileFixup(const char *fname, char *dest, size_t dest_size)
{
    int fix = 0;
	// fname can be the same as dest
#if _PS3
	if (fname[0] == '.' && (fname[1] == '/' || fname[1] == '\\'))
        fix = 2;
    else if (strStartsWith(fname, "C:\\") || strStartsWith(fname, "C:/"))
        fix = 3;

	if(fix) {
		char name2[CRYPTIC_MAX_PATH];
		strcpy(name2, "/app_home/");
		strcat(name2, fname+fix);
		strcpy_s(dest, dest_size, name2);
	}
#elif _XBOX
	if (fname[0] == '.' && (fname[1] == '/' || fname[1] == '\\'))
        fix = 2;
    else if (strStartsWith(fname, "C:\\") || strStartsWith(fname, "C:/"))
        fix = 3;

	if(fix) {
		char name2[CRYPTIC_MAX_PATH];
        strcpy(name2, "game:/");
		strcat(name2, fname+fix);
		strcpy_s(dest, dest_size, name2);
	}
#endif
	return fix;
}

/* Function fileLocate()
*	Locates a file with the relative path.  It is possible for the game directory
*	to exist in multiple locations.  This function searches through all possible
*	locations in the order specified in c:\gamedatadir.txt for the file specified
*	by the given relative path.  It will also search in Pig files.  All of this
*  now goes through the FolderCache module.  If an absolute path is given, the
*  path is returned unaltered.
*
*
*/
static int filelocateread_return_spot=0;
char *fileLocateRead_s(const char *fname_in, char *dest, size_t dest_size) // Was fileLocateExists
{
	char fname[CRYPTIC_MAX_PATH];
	char temp[CRYPTIC_MAX_PATH];
	char ns[CRYPTIC_MAX_PATH];

	PERFINFO_AUTO_START("fileLocateRead", 1);
	
	//memlog_printf(NULL, "fileLocateRead(%s)", fname);
	

	if (!loadedGameDataDirs && !g_force_absolute_paths) 
		fileLoadDataDirs(0);

	if (!fname_in || !fname_in[0]) {
		filelocateread_return_spot = 1;
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	strcpy(fname, fname_in);

	if (fileGetNameSpacePath(fname, ns, temp))
	{
		char *fname_noslash = temp;
		if (fname_noslash[0]=='/') {
			fname_noslash++;
		}
		sprintf(fname, NAMESPACE_PATH"%s/%s", ns, fname_noslash);
	}

	if (fileIsAbsolutePath(fname) && !g_force_absolute_paths) {
		fileRelativePath(fname, temp);
		strcpy(fname, temp);
	}

#if PLATFORM_CONSOLE
	if (fileFixup(fname, SAFESTR(temp)))
		strcpy(fname, temp);
#endif

	// Is the given filename still an absolute path?
	//	Consider relative paths of the form "./bla bla" and "../blabla"
	//	"absolute" paths also.  In these cases, the filename is being explicitly
	//	specified and does not need "gameDataDirs" to resolve it into a real filename.
	if (fileIsAbsolutePath(fname))
	{
		strcpy_s(SAFESTR2(dest), fname);
		filelocateread_return_spot = 2;
		PERFINFO_AUTO_STOP();
		return dest;
	}
	else
	{
		// filename is a relative path
		FolderNode * node;

		PERFINFO_AUTO_START("FolderCacheQuery", 1);
			node = FolderCacheQueryEx(folder_cache, fname, false, true);
		PERFINFO_AUTO_STOP();
		
		if (node==NULL) { // not found in filesystem or pigs
			filelocateread_return_spot = 4;
			dest = NULL;
		}
		else
		{
			filelocateread_return_spot = 5;
			PERFINFO_AUTO_START("FolderCacheGetRealPath1", 1);
				FolderCacheGetRealPath(folder_cache, node, SAFESTR2(dest));
			PERFINFO_AUTO_STOP();
		}

		FolderNodeLeaveCriticalSection();
	}
	PERFINFO_AUTO_STOP();
	return dest;
}

// Will always return a path into the filesystem, *never* into a pig file
char *fileLocateWrite_s(const char *_fname, char *dest, size_t dest_size)
{
	char *path;
	char fname[CRYPTIC_MAX_PATH];
	char ns[CRYPTIC_MAX_PATH];
	char temp[CRYPTIC_MAX_PATH];
	char *fname_noslash;

	PERFINFO_AUTO_START("fileLocateWrite", 1);

	if(!_fname[0]){
		devassertmsg(0, "Empty filename passed to fileLocateWrite");
		strcpy_s(SAFESTR2(dest), "");
		PERFINFO_AUTO_STOP();
		return dest;
	}

	Strncpyt(fname, _fname); // in case _fname==dest
	if (fileGetNameSpacePath(fname, ns, temp))
	{
		fname_noslash = temp;
		if (fname_noslash[0]=='/') {
			fname_noslash++;
		}
		sprintf(fname, NAMESPACE_PATH"%s/%s", ns, fname_noslash);
	}
	

	path = fileLocateRead_s(fname, SAFESTR2(dest));

	if (path && !is_pigged_path(path)) {
		// Found an on-disk path (c:\game\data\..., c:\game\serverdata\..., etc)
		PERFINFO_AUTO_STOP();
		return dest;
	}

	// Relative path not found, make a path
	if (fileIsAbsolutePath(fname)) {
		// Absolute path, just return it
		strcpy_s(SAFESTR2(dest), fname);
		PERFINFO_AUTO_STOP();
		return dest;
	} else if (path) {
		// Found a relative path in a pig, get the on-disk location if possible
		FolderNode * node;

		PERFINFO_AUTO_START("FolderCacheQuery", 1);
		node = FolderCacheQuery(folder_cache, fname);
		PERFINFO_AUTO_STOP();
		assert(node); // Otherwise how did fileLocateRead return successfully?

		if (node)
		{
			char *ret;
			PERFINFO_AUTO_START("FolderCacheGetRealPath1", 1);
			ret = FolderCacheGetOnDiskPath(folder_cache, node, SAFESTR2(dest));
			PERFINFO_AUTO_STOP();
			if (ret) {
				FolderNodeLeaveCriticalSection();
				PERFINFO_AUTO_STOP();
				return dest;
			} else {
				if (FolderCacheGetMode()==FOLDER_CACHE_MODE_FILESYSTEM_ONLY || FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT || FolderCacheGetMode()==FOLDER_CACHE_MODE_DEVELOPMENT_DYNAMIC)
					assert(0);
				// Fall through, shouldn't happen?
				// *Will* happen in production mode, or maybe when running in I_LIKE_PIGS mode
			}
		} else {
			// Handle error case?  Just let it fall through?
		}
		FolderNodeLeaveCriticalSection();
	}
	fname_noslash = fname;
	if (fname_noslash[0]=='/') {
		fname_noslash++;
	}
	sprintf_s(SAFESTR2(dest), "%s/%s", mainGameDataDir, fname_noslash);
	PERFINFO_AUTO_STOP();
	return dest;	
}

char *fileLocateReadBin_s(const char *fname, char *dest, size_t dest_size)
{
	char *retval;
	assert(dest);

	PERFINFO_AUTO_START_FUNC();

	if (fileIsAbsolutePath(fname))
	{
#if _PS3
        assert(strStartsWith(fname, "/app_home/TestTextParser"));
#elif _XBOX
		assert(strStartsWith(fname, "devkit:\\TestTextParser"));
#else
		assert(strStartsWith(fname, "c:\\temp") || strStartsWith(fname, "c:/temp") || strStartsWith(fname, fileTempDir()));
#endif
		retval = fileLocateWrite_s(fname, SAFESTR2(dest));
		PERFINFO_AUTO_STOP();
		return retval;
	}

	if (isDevelopmentMode()) {
		// Only load from the project folder, no core!
		char *readpath, ns[MAX_PATH], base[MAX_PATH];
		const char *fname_noslash;

		readpath = fileLocateRead_s(fname, SAFESTR2(dest));
		if (readpath)
		{
			ANALYSIS_ASSUME(readpath);
			if (!strstri(readpath, "core"))
			{
				retval = readpath;
				PERFINFO_AUTO_STOP();
				return retval;
			}
		}

		if (fileGetNameSpacePath(fname, ns, base))
		{
			fname_noslash = base;
			if (fname_noslash[0]=='/') {
				fname_noslash++;
			}
			sprintf_s(SAFESTR2(dest), "%s/"NAMESPACE_PATH"%s/%s", mainGameDataDir, ns, fname_noslash);
		}
		else
		{
			fname_noslash = fname;
			if (fname_noslash[0]=='/') {
				fname_noslash++;
			}
			sprintf_s(SAFESTR2(dest), "%s/%s", mainGameDataDir, fname_noslash);
		}

		if (fileExists(dest)) {
			PERFINFO_AUTO_STOP();
			return dest;
		} else
			PERFINFO_AUTO_STOP();
			return NULL;
	} else {
		retval = fileLocateRead_s(fname, SAFESTR2(dest));
		PERFINFO_AUTO_STOP();
		return retval;
	}
}

char *fileLocateWriteBin_s(const char *fname, char *dest, size_t dest_size)
{
	char basename[CRYPTIC_MAX_PATH];
	char ns[CRYPTIC_MAX_PATH];
	assert(dest);

	if (fileIsAbsolutePath(fname))
	{
#if _PS3
        assert(strStartsWith(fname, "/app_home/TestTextParser"));
#elif _XBOX
		assert(strStartsWith(fname, "devkit:\\TestTextParser"));
#else
		assert(strStartsWith(fname, "c:\\temp") || strStartsWith(fname, "c:/temp") || strStartsWith(fname, fileTempDir()));
#endif
		return fileLocateWrite_s(fname, SAFESTR2(dest));
	}

	if (isDevelopmentMode() && !fileGetNameSpacePath(fname, ns, basename)) {
		// Only load from the project folder, no core!
		const char *fname_noslash = fname;
		if (fname_noslash[0]=='/') {
			fname_noslash++;
		}
		sprintf_s(SAFESTR2(dest), "%s/%s", mainGameDataDir, fname_noslash);
		return dest;
	} else {
		return fileLocateWrite_s(fname, SAFESTR2(dest));
	}
}

FILE *fileOpenBin_dbg(const char *fname, const char *how, const char *caller_fname, int line)
{
	FILE*	file = NULL;
	char*	realFilename;
	char	buf[CRYPTIC_MAX_PATH];
	
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	
		if (strcspn(how, "zwWaA+~")!=strlen(how)) {
			realFilename = fileLocateWriteBin(fname, buf);
		} else {
			realFilename = fileLocateReadBin(fname, buf);
		}

		if(realFilename && realFilename[0])
		{
			file = x_fopen(realFilename, how, caller_fname, line);
		}
	
	PERFINFO_AUTO_STOP();
	
	return file;
}

FILE *fileOpen_dbg(const char *fname, const char *how, const char *caller_fname, int line)
{
	FILE*	file = NULL;
	char*	realFilename;
	char	buf[CRYPTIC_MAX_PATH];
	
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	
		if (strcspn(how, "zwWaA+~")!=strlen(how)) {
			realFilename = fileLocateWrite(fname, buf);
		} else {
			realFilename = fileLocateRead(fname, buf);
		}

		if(realFilename && realFilename[0])
		{
			file = x_fopen(realFilename, how, caller_fname, line);
		}
	
	PERFINFO_AUTO_STOP();
	
	return file;
}

/* Function fileOpenEx
 *	Constructs a relative filename using the given formating information
 *	before attempting to locate and open the file.
 *
 *	Users no longer have to manually construct the relative path before
 *	calling fileOpen.
 */
FILE *fileOpenEx_dbg(const char *fnameFormat, const char *caller_fname, int line, const char *how, ...)
{
va_list va;
char buf[1024];

	// Construct a relative path according to the given filename format.
	va_start(va, how);
	vsprintf(buf, fnameFormat, va);
	va_end(va);

	return fileOpen(buf, how);
}

FILE *fileOpenTemp_dbg(const char *fname, const char *how, const char *caller_fname, int line)
{
char	buf[1000];

	sprintf(buf,"%s/%s",fileTempDir(),fname);
	return x_fopen(buf, how, caller_fname, line);
}

FILE *fileOpenStuffBuff_dbg(StuffBuff *sb, const char *caller_fname, int line)
{
	return x_fopen((char*)sb, "StuffBuff", caller_fname, line);
}

FILE *fileOpenEString_dbg(char **estr, const char *caller_fname, int line)
{
	FILE *f = x_fopen((char*)estr, "EString", caller_fname, line);
	assert(f);
	return f;
}

void fileFree(void *mem)
{
	free(mem);
}

static int filealloc_failure_spot=0;
void *fileAlloc_dbg(const char *fname,int *lenp, const char *caller_fname, int line)
{
	FILE	*file;
	intptr_t	total=0,bytes_read,orig_total;
	char	*mem=0,*located_name, buf[CRYPTIC_MAX_PATH];

	PERFINFO_AUTO_START("fileAlloc", 1);
		if (!fname)
		{
			filealloc_failure_spot = 1;
			PERFINFO_AUTO_STOP();
			return 0;
		}

		// First, special case for files in dynamic hoggs
		if (fname[0]=='#')
		{
			char hogname[MAX_PATH];
			char *s;
			HogFile *hog_handle;
			HogFileIndex file_index;

			strcpy(hogname, fname+1);
			s = strchr(hogname, '#');
			*s = '\0';
			strcpy(buf, s+1);
			hog_handle = hogFileRead(hogname, NULL, PIGERR_ASSERT, NULL, HOG_READONLY|HOG_NOCREATE);
			if (hog_handle)
			{
				file_index = hogFileFind(hog_handle, buf);
				if (file_index != HOG_INVALID_INDEX)
				{
					U32 count;
					mem = hogFileExtract(hog_handle, file_index, &count, NULL);
					if (lenp)
						*lenp = count;
					hogFileDestroy(hog_handle, true);
					PERFINFO_AUTO_STOP();
					return mem;
				}
				hogFileDestroy(hog_handle, true);
			}
			if (lenp)
				*lenp = 0;
			PERFINFO_AUTO_STOP();
			return NULL;
		}
		PERFINFO_AUTO_START("fileLocateRead", 1);

			if(!fileIsAbsolutePath(fname))
			{
				located_name = fileLocateRead(fname, buf);
			}
			else
			{
				strcpy(buf, fname);
				located_name = buf;
			}
			if (!located_name)
			{
				filealloc_failure_spot = 1;
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return 0;
			}
			
		PERFINFO_AUTO_STOP_START("fileSize", 1);
		
			orig_total = total = fileSize(fname);
			if (total == -1)
			{
				filealloc_failure_spot = 2;
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return 0;
			}
			
		PERFINFO_AUTO_STOP_START("fopen", 1);
		
			file = x_fopen(located_name, "rb", caller_fname, line);
			
		PERFINFO_AUTO_STOP();
		
		located_name[0]=0; // clear locatebuff just to be safe
		if (!file)
		{
			filealloc_failure_spot = 3;
			PERFINFO_AUTO_STOP();
			return 0;
		}
		assert(-1!=total);

		mem = smalloc(total+1);
		bytes_read = fread(mem,1,total+1,file);
		// On the Xbox, fileSize() is *not* absolute, check for a changed file if we
		//  didn't read the expected number of bytes.
		// Also, on PC, this will fix the issue if FileWatcher is confused somehow
		if (bytes_read != total) {
			// File size on disk did not match!  Re-query the folder cache
			if (fileIsAbsolutePath(fname)) {
				// Not in the folder cache, but FileWatcher may still be at fault
				struct _stat32i64 sbuf;
				cryptic_stat32i64_utc(fname, &sbuf);
				total = sbuf.st_size;
			} else {
				FolderCacheForceUpdate(folder_cache, fname);
				// Re-open file (might no longer be in a pigg) and re-query the file size (may have been written to)
				fclose(file);
				located_name = fileLocateRead(fname, buf);
				file = x_fopen(located_name, "rb", caller_fname, line);
				assert(file); // Maybe the file disappeared, could add code to handle this gracefully.
				total = fileSize(located_name);
			}
#if !PLATFORM_CONSOLE
			if (total != orig_total) {
				printf("FolderCache file size was incorrect on \"%s\", thought %d, actual %d.  Sudden change, or FileWatcher gone bad?\n",
					fname, orig_total, total);
			}
#endif
			mem = srealloc(mem, total+1);
			fseek(file, 0, SEEK_SET);
			bytes_read = fread(mem,1,total+1,file);
		}
		assert(bytes_read==total);
		fclose(file);
		mem[bytes_read] = 0;
		if (lenp)
			*lenp = bytes_read;

	PERFINFO_AUTO_STOP();

	return mem;
}

void *fileAllocWithCRCCheck_dbg(const char *fname,int *lenp, bool *checksum_valid, const char *caller_fname, int line)
{
	char buf[MAX_PATH];
	const char *located_name;
	*checksum_valid = true;
	if (!fname)
		return NULL;
	if (fname[0] == '#') // Can't handle this
		return fileAlloc_dbg(fname, lenp, caller_fname, line);
	if (!fileIsAbsolutePath(fname))
	{
		located_name = fileLocateRead(fname, buf);
	} else {
		located_name = fname;
	}
	if (!located_name || !is_pigged_path(located_name))
		return fileAlloc_dbg(fname, lenp, caller_fname, line);

	// We have a pigged path, extract it with checksum checking
	{
		FolderNode * node;
		HogFile *hog_file;
		char name[MAX_PATH];
		int virtual_location;
		int file_index;
		strcpy(name, strrchr(located_name, ':')+2); // remove the path to the pigg...
		PERFINFO_AUTO_START("FolderCacheQuery", 1);
		node = FolderCacheQuery(folder_cache, name);
		PERFINFO_AUTO_STOP();
		assert(node);
		if (node->needs_patching && folder_cache->patchCallback)
		{
			// Patch and re-query for node
			// Callback *must* release the critical section
			PERFINFO_AUTO_START("patchCallback", 1);
			folder_cache->patchCallback(folder_cache, node, name, NULL, false);
			PERFINFO_AUTO_STOP_START("FolderCacheQuery", 1);
			node = FolderCacheQuery(folder_cache, name);
			PERFINFO_AUTO_STOP();
			assert(node);
		}
		assert(node->virtual_location<0);
		virtual_location = node->virtual_location;
		file_index = node->file_index;
		FolderNodeLeaveCriticalSection();

		hog_file = PigSetGetHogFile(VIRTUAL_LOCATION_TO_PIG_INDEX(virtual_location));
		assert(hog_file);
		assert(stricmp(name, hogFileGetFileName(hog_file, file_index))==0);
		return hogFileExtract(hog_file, file_index, lenp, checksum_valid);
	}
}

void *fileAllocWithRetries_dbg(const char *fname,int *lenp, int iCount, const char *caller_fname, int line)
{
	int i;
	void *pRetVal;

	for (i = 0; i < iCount; i++)
	{
		pRetVal = fileAlloc_dbg(fname, lenp MEM_DBG_PARMS_CALL);
		if (pRetVal)
		{
			return pRetVal;
		}
		Sleep(1);
	}

	*lenp = 0;
	return NULL;
}



void *fileAllocEx_dbg(const char *fname,int *lenp, const char *mode, FileProcessCallback callback MEM_DBG_PARMS)
{
	FILE	*file;
	intptr_t total=0,bytes_read=0, last_read, zipped, eof=0, original_total;
	char	*mem=0,*located_name, buf[CRYPTIC_MAX_PATH];
	int		chunk_size=65536;


	located_name = fileLocateRead(fname, buf);
	if (!located_name)
		return 0;

	original_total = total = fileSize(located_name);

	zipped = 0!=strchr(mode, 'z');
	if (zipped) {
		total = total * 11; // Estimate
	}

	file = x_fopen(located_name, mode, caller_fname, line);
	located_name[0]=0; // clear locatebuff just to be safe
	if (!file)
		return 0;

	mem = smalloc(total+1);
	assert(mem);
	if (callback) {
		if (!callback(bytes_read, total)) {
			free(mem);
			return NULL;
		}
	}
	while ((!zipped && bytes_read < total) || (zipped && !eof)) {
		if (total - bytes_read < chunk_size) {
			if (!zipped) { // Just read what's left
				chunk_size = total - bytes_read;
			} else { // We will need to realloc
				total = (int)(total*1.5);
				mem = srealloc(mem, total+1);
			}
		}
		last_read = fread(mem+bytes_read,1,chunk_size,file);
		if (last_read!=chunk_size) {
			if (!zipped) {
				// This should not happen.  Ever.  Well, unless a file is modified during the read
				//   process, or pigs are modifed while the game is running
				assert(!"Error reading from file, it's not as big as fileSize said it was!");
				free(mem);
				return NULL;
			} else {
				// Size mismatch on read
				if (last_read == 0) {
					eof = 1;
				} else if (last_read == -1) {
					// Error!
					free(mem);
					return NULL;
				}
			}
		}
		if (last_read > 0) {
			bytes_read+=last_read;
			if (callback) {
				if (!callback(bytes_read, total)) {
					free(mem);
					return NULL;
				}
			}
		}
	}
	if (!zipped) {
		assert(bytes_read==total);
	}
	fclose(file);
	mem[bytes_read] = 0;
	if (lenp)
		*lenp = bytes_read;
	return mem;
}

int printPercentage(int bytes_processed, int total) // Sample FileProcessCallback
{
	if (total) {
		int numprinted=0;
		numprinted+=printf("%5.1lf%% ", 100.f*bytes_processed / (float)total);
		numprinted+=printPercentageBar(10*bytes_processed / total, 10);
		backSpace(numprinted, total==bytes_processed);
	}
	return 1;
}



int fileRenameToBak(const char *fname) {
	char backfn[CRYPTIC_MAX_PATH];
	int i;

	sprintf(backfn, "%s.bak", fname);
	// delete old .bak if there
	if ( fileExists(backfn) && fileForceRemove(backfn) != 0 )
	{
		// search for a .bak name that isnt in use
		for ( i = 0; i < 5; ++i )
		{
			sprintf(backfn, "%s.%d.bak", fname, i);
			if ( !fileExists(backfn) || fileForceRemove(backfn) == 0 )
				break;
		}
	}
	// rename
	chmod(fname, _S_IREAD | _S_IWRITE);
	return rename(fname, backfn);
}


#if !PLATFORM_CONSOLE
int fileMoveToRecycleBin(const char *filename)
{
	char buffer[CRYPTIC_MAX_PATH] = {0};
	SHFILEOPSTRUCT shfos;
	int ret;
	shfos.hwnd = compatibleGetConsoleWindow();
	shfos.wFunc = FO_DELETE;
	// Needs to be doubly terminated
// 	strncpy(buffer, filename, ARRAY_SIZE(buffer)-2);
 	strncpy_s(buffer, ARRAY_SIZE_CHECKED(buffer), filename, ARRAY_SIZE(buffer)-2);
	buffer[strlen(buffer)+1]=0;
	backSlashes(buffer);
	shfos.pFrom = UTF8_To_UTF16_malloc(buffer);;
	shfos.pTo = shfos.pFrom;
	shfos.fFlags = FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT;
	shfos.hNameMappings = NULL;
	shfos.lpszProgressTitle = L"Deleting files...";

	ret = SHFileOperation(&shfos);
	if (ret==0) {
		// Succeeded call, did anything get cancelled?
		ret = shfos.fAnyOperationsAborted;
	}

	SAFE_FREE(shfos.pFrom);

	return ret;

}
#endif

int fileForceRemove(const char *_fname) {
	int ret;
	char fname[CRYPTIC_MAX_PATH];

	fileLocateWrite(_fname, fname);

#if PLATFORM_CONSOLE
	fileFixup(fname, SAFESTR(fname));
#endif
#if _XBOX
	backSlashes(fname);
#endif

	ret = chmod(fname, _S_IREAD | _S_IWRITE);

	if (ret==-1 
#if _PS3
#else
        && (GetLastError() != 5 // not access denied
#if _XBOX
		&& errno != EINVAL // Xbox gives this for some reason even though the file exists, maybe smb issue?
#endif
		)
#endif
        )
	{
		// File doesn't exist
		return ret;
	}
	if (!strEndsWith(fname, ".bak") && !fileCanGetExclusiveAccess(fname))
	{
		char temp[CRYPTIC_MAX_PATH];
		// Someone else has a handle to this file, hopefully with SHARE_DELETE permissions!
		// Anyway, rename it first, then issue the delete, since the delete will not finalize
		// until after the other app closes it's handle.
		fileRenameToBak(fname);
		strcpy(temp, fname);
		sprintf(fname, "%s.bak", temp); // Instead, remove the newly renamed .bak files
	}
//	ret = fileMoveToRecycleBin(fname);
//	if (ret!=0) {
		ret = remove(fname);
//	}
#if _XBOX
	if (strStartsWith(fname, "cache:"))
		XFlushUtilityDrive();
#endif
	return ret;
}

// Copy a file and may sync the timestamp (if dest is network drive, timestamp may not be synced), overwriting read-only files
// Returns 0 upon success
int fileCopy(const char *_src, const char *_dst)
{
	int ret;
	char src[MAX_PATH], dst[MAX_PATH];

	strcpy(src, _src);
	strcpy(dst, _dst);
	backSlashes(src);
	backSlashes(dst);

#if PLATFORM_CONSOLE
	chmod(dst, _S_IREAD | _S_IWRITE);
	ret = CopyFile(src, dst, FALSE) == TRUE;
#if !_PS3
	if ( !ret )
	{
		// Try again!
		printf("Attempt to copy over an executable (%s) while running, ranaming old .exe to .bak and trying again...\n", getFileName(dst));
		fileRenameToBak(dst);
		ret = CopyFile( src, dst, TRUE ) == TRUE;
	}
#endif
#else
	{
		char buf[1024];

		chmod(dst, _S_IREAD | _S_IWRITE);
		sprintf(buf, "copy \"%s\" \"%s\">nul", src, dst);
		ret = system(buf);
		if (ret!=0 && (strEndsWith(dst, ".exe") || strEndsWith(dst, ".dll") || strEndsWith(dst, ".pdb"))) {
			// Try again!
			printf("Attempt to copy over an executable (%s) while running, ranaming old .exe to .bak and trying again...\n", getFileName(dst));
			fileRenameToBak(dst);
			ret = system(buf);
		}
	}
#endif

	return ret;
}

int fileMove(const char * _src, const char * _dst)
{
	int ret;
	char src[MAX_PATH], dst[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	strcpy(src, _src);
	strcpy(dst, _dst);
	backSlashes(src);
	backSlashes(dst);

#if PLATFORM_CONSOLE
	chmod(dst, _S_IREAD | _S_IWRITE);
	ret = MoveFile(src, dst) == TRUE;
#else

	if(getWineVersion()) {

		ret = !(MoveFile_UTF8(src, dst) == TRUE);

	} else {

		char buf[1024];

		chmod(dst, _S_IREAD | _S_IWRITE);
		sprintf(buf, "move \"%s\" \"%s\">nul", src, dst);
		ret = system(buf);
	}

#endif
	PERFINFO_AUTO_STOP();

	return ret;
}

static const char* findEditPlus(void)
{
	const char *edit_prog="notepad";
	static char buffer[CRYPTIC_MAX_PATH];
	bool found_one=false;

	// Look for EditPlus
	if (!found_one) {
		RegReader rr = createRegReader();
		initRegReader(rr, "HKEY_CLASSES_ROOT\\Applications\\editplus.exe\\shell\\open\\command");
		if (rrReadString(rr, "", buffer, ARRAY_SIZE(buffer))) {
			if (strEndsWith(buffer, " \"%1\"")) {
				*strrchr(buffer, ' ')=0;
			}
			// Strip quotes, not allowed
			if (buffer[0] == '\"')
				strcpy_unsafe(buffer, buffer+1);
			if (buffer[strlen(buffer)-1] == '\"')
				buffer[strlen(buffer)-1] = '\0';
			if (fileExists(buffer)) {
				edit_prog = buffer;
				found_one = true;
			}
		}
		rrClose(rr);
		destroyRegReader(rr);
	}
	{
		static const char *paths[] = {"C:\\Program Files\\EditPlus 2\\editplus.exe",
			"C:\\Program Files\\Edit Plus 2\\editplus.exe",
			"C:\\Program Files (x86)\\EditPlus 2\\editplus.exe",
			"C:\\Program Files (x86)\\Edit Plus 2\\editplus.exe"
		};
		int i;
		for (i=0; !found_one && i<ARRAY_SIZE(paths); i++) 
		{
			if (fileExists(paths[i])) {
				edit_prog = paths[i];
				found_one = true;
			}
		}
	}
	return edit_prog;
}

// This callback gets executed by the renderthread, so is message-safe. We therefore call ShellExecute directly.
#if !PLATFORM_CONSOLE
void fileOpenWithEditorShellExecuteCallback(int ret, HWND hwnd, const char* lpOperation, const char* localfname, const char* lpParameters, const char* lpDirectory, int nShowCmd)
{
	S16 *pWideLocalName = UTF8_To_UTF16_malloc(localfname);

	bool isTxt = strrchr(localfname, '.') && strstriConst(".txt", strrchr(localfname, '.'));
	if (ret <= 32) {
		// There was a problem, try Edit on it
		ret = (int)(intptr_t)ShellExecute( NULL, isTxt ?L"open":L"edit", pWideLocalName, NULL, NULL, SW_SHOW);
		if (ret <= 32) {
			// There was a problem, EditPlus?
			ret = (int)(intptr_t)ShellExecute ( NULL, L"EditPlus", pWideLocalName, NULL, NULL, SW_SHOW);
			if (ret <= 32) {
				// And *that* failed, try manually launching EditPlus
				char cmd[1024];
				sprintf(cmd, "%s \"%s\"", findEditPlus(), localfname);
				system_detach(cmd, 0, 0);
			}
		}
	}

	SAFE_FREE(pWideLocalName);
}
#endif

void fileOpenWithEditor(const char *localfname)
{
#if !PLATFORM_CONSOLE
	bool isTxt = strrchr(localfname, '.') && strstriConst(".txt", strrchr(localfname, '.'));
	static char *skip_exts = ".exe .bat .reg .3ds am.max .wrl .lnk .com"; // what *not* to run "open" on
	if (!(strrchr(localfname, '.') && strstriConst(skip_exts, strrchr(localfname, '.')))) {
		// Try "edit" first on text files, to try and get EditPlus on Woomer's machine
		ulShellExecuteWithCallback( NULL, isTxt?"edit":"open", localfname, NULL, NULL, SW_SHOW, fileOpenWithEditorShellExecuteCallback);
	} else {
		static char *edit_exts = ".bat .reg"; // what *to* run "edit" on (must also be in the above list)
		if (!(strrchr(localfname, '.') && !strstri(edit_exts, strrchr(localfname, '.')))) {
			ulShellExecuteWithCallback( NULL, "edit", localfname, NULL, NULL, SW_SHOW, fileOpenWithEditorShellExecuteCallback);
		}
	}
#endif
}

const char* fileDetectDiffProgram(const char *fname1, const char *fname2)
{
	const char *diff_prog="CMD /C echo Error finding Diff program!";
#ifndef PLATFORM_CONSOLE
	static char buffer[CRYPTIC_MAX_PATH];
	bool found_one=false;
	bool use_bc_plugins = false;
	FileContentType type, type1 = FileContentType_Empty, type2 = FileContentType_Empty;

	// Guess file types.
	if (fname1)
		type1 = fileGuessFileContentType(fname1);
	if (fname2)
		type2 = fileGuessFileContentType(fname2);

	// Determine type to treat files as.
	if (type1 == FileContentType_Empty)
		type = type2;
	else if (type2 == FileContentType_Empty)
		type = type1;
	else if (type1 == type2)
		type = type1;
	else
		type = FileContentType_Binary;

	// Handle hogs.
	if (!found_one && type == FileContentType_Hog) {
		diff_prog = "pig ip";
		found_one = true;
	}

	// Look for beyond compare
	if (!found_one) {
		RegReader rr = createRegReader();
		initRegReader(rr, "HKEY_CLASSES_ROOT\\BeyondCompare.Snapshot\\DefaultIcon");
		if (rrReadString(rr, "", buffer, ARRAY_SIZE(buffer))) {
			if (strEndsWith(buffer, ",0")) {
				*strrchr(buffer, ',')=0;
			}
			if (fileExists(buffer)) {
				diff_prog = buffer;
				found_one = true;
				use_bc_plugins = true;
			}
		}
		rrClose(rr);
		destroyRegReader(rr);
	}
	if (!found_one && fileExists("C:/Program Files (x86)/Beyond Compare 3/BComp.EXE")) {
		strcpy(buffer, "\"C:/Program Files (x86)/Beyond Compare 3/BComp.EXE\"");
		diff_prog = buffer;
		found_one = true;
		use_bc_plugins = true;
	}
	if (!found_one && fileExists("C:/Program Files/Beyond Compare 2/BC2.EXE")) {
		strcpy(buffer, "\"C:/Program Files/Beyond Compare 2/BC2.EXE\"");
		diff_prog = buffer;
		found_one = true;
		use_bc_plugins = true;
	}
	if (!found_one && fileExists("C:/bin/diff.exe")) {
		showConsoleWindow();
		diff_prog = "C:/bin/diff.exe";
		found_one = true;
	}
	if (!found_one && fileExists("C:/WINDOWS/System32/fc.exe")) {
		showConsoleWindow();
		diff_prog = "C:/WINDOWS/System32/fc.exe";
		found_one = true;
	}

	// Add extra options for the file content type.
	if (found_one && use_bc_plugins)
	{
		switch (type)
		{
			case FileContentType_Binary:
				strcat(buffer, " /fv=\"Hex Viewer\"");
				break;

			case FileContentType_Generic_Narrow_Text:
			case FileContentType_Generic_Wide_Text:
				strcat(buffer, " /fv=\"File Viewer\"");
				break;

			case FileContentType_Generic_Image:
				strcat(buffer, " /fv=\"Picture Viewer\"");
				break;

			case FileContentType_Windows_Executable:
				strcat(buffer, " /fv=\"Version Viewer\"");
				break;

			case FileContentType_Windows_Icon:
			case FileContentType_Windows_Cursor:
				strcat(buffer, " /fv=\"Symbol Viewer\"");
				break;

			case FileContentType_Csv:
				strcat(buffer, " /fv=\"Data Viewer\"");
		}
	}

#endif
	return diff_prog;
}

int fileLaunchDiffProgram(const char *fname1, const char *fname2)
{
#if PLATFORM_CONSOLE
	return 0;
#else
	char buf[MAX_PATH*3];
	sprintf(buf, "%s \"%s\" \"%s\"", fileDetectDiffProgram(fname1, fname2), fname1, fname2);
	_flushall();
	return system_detach(buf, 0, false);
#endif
}

FileContentType fileGuessFileContentType(const char *fname)
{
	char data[4096];
	size_t data_len = 0;
	FILE *infile;
	S64 size;
	static const char hogg_magic[] = {0x0d, 0xf0, 0xad, 0xde};
	static const char *image_extensions[] = {
		".bmp",
		".bw",
		".cel",
		".cut",
		".dib",
		".emf",
		".eps",
		".fax",
		".gif",
		".icb",
		".jfif",
		".jpe",
		".jpeg",
		".jpg",
		".pbm",
		".pcc",
		".pcd",
		".pcx",
		".pgm",
		".pic",
		".png",
		".ppd",
		".ppm",
		".psd",
		".psp",
		".rgb",
		".rgba",
		".rla",
		".rle",
		".rpf",
		".sgi",
		".tga",
		".tif",
		".tiff",
		".vda",
		".vst",
		".win",
		".wmf",
		NULL
	};
	static const char *exe_extensions[] = {
		".386",
		".com",
		".dll",
		".drv",
		".exe",
		".ocx",
		".scr",
		".sys",
		".vxd",
		NULL
	};

	PERFINFO_AUTO_START_FUNC();

	// Try to read some data from the beginning of the file.
	size = fileSize64(fname);
	infile = fopen(fname, "r");
	if (infile)
	{
		data_len = fread(data, 1, sizeof(data), infile);
		fclose(infile);
	}

	// If the file is empty, it doesn't have a type, even if it has an extension.
	if (!data_len)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return FileContentType_Empty;
	}

	// Detect hog magic and reasonable version.
	if (data_len > 2048 && !memcmp(data, hogg_magic, sizeof(hogg_magic)) && data[4] > 0 && data[5] == 0)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return FileContentType_Hog;
	}

	// Check for absence of zero bytes.
	if (data_len && !memchr(data, 0, data_len))
	{
		if (strEndsWith(fname, ".csv"))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return FileContentType_Csv;
		}
		else
		{
			PERFINFO_AUTO_STOP_FUNC();
			return FileContentType_Generic_Narrow_Text;
		}
	}

	// Try to detect obvious UTF-16.  First, check for even file size and at least 4 bytes long.
	if (!(size & 1) && data_len >= 4)
	{
		if (data[0] == '\xfe' && data[1] == '\xff')
			return FileContentType_Generic_Wide_Text;
		if (data[0] == '\xff' && data[1] == '\xfe')
			return FileContentType_Generic_Wide_Text;
	}

	// Check for icon or cursor extension.
	if (strEndsWith(fname, ".ico"))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return FileContentType_Windows_Icon;
	}
	if (strEndsWith(fname, ".cur"))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return FileContentType_Windows_Cursor;
	}

	// Check for image extension.
	if (strEndsWithAnyStatic(fname, image_extensions))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return FileContentType_Generic_Image;
	}

	// Check for executable extension.
	if (strEndsWithAnyStatic(fname, exe_extensions))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return FileContentType_Windows_Executable;
	}

	// If nothing matches, say it's binary.
	PERFINFO_AUTO_STOP_FUNC();
	return FileContentType_Binary;
}

// gzip a file on disk
bool fileGzip(const char *fname)
{
	char outfilename[CRYPTIC_MAX_PATH];
	char buffer[512];
	char error[512];
	FILE *infile;
	FILE *outfile;
	size_t len;
	int result;
	bool ok = true;

	// Get output filename.
	if (strlen(fname) + 4 >= CRYPTIC_MAX_PATH)
	{
		Errorf("fileGzip(): Filename length overflow for %s", fname);
		return false;
	}
	sprintf(outfilename, "%s.gz", fname);

	// Open input and output files.
	infile = fopen(fname, "rb");
	if (!infile)
	{
		strerror_s(SAFESTR(error), errno);
		Errorf("fileGzip(): Unable to open input file %s: %s (%d)", fname, error, errno);
		return false;
	}
	outfile = fopen(outfilename, "wbz");
	if (!infile)
	{
		strerror_s(SAFESTR(error), errno);
		Errorf("fileGzip(): Unable to open output file %s: %s (%d)", outfilename, error, errno);
		return false;
	}

	// Copy the file data.
	errno = 0;
	while ((len = fread(buffer, 1, sizeof(buffer), infile)) > 0)
	{
		size_t wrote = fwrite(buffer, 1, len, outfile);
		if (wrote != len)
		{
			strerror_s(SAFESTR(error), errno);
			Errorf("fileGzip(): Incomplete write to outfile %s: %s (%d)", outfilename, error, errno);
			ok = false;
			goto close;
		}
	}
	if (errno)
	{
		strerror_s(SAFESTR(error), errno);
		Errorf("fileGzip(): Read error on infile %s: %s (%d)", fname, error, errno);
		ok = false;
	}

	// Close files.
	close:
	result = fclose(infile);
	if (result)
	{
		strerror_s(SAFESTR(error), errno);
		Errorf("fileGzip(): Error closing input file %s: %s (%d)", fname, error, errno);
		ok = false;
	}
	result = fclose(outfile);
	if (result)
	{
		strerror_s(SAFESTR(error), errno);
		Errorf("fileGzip(): Error closing output file %s: %s (%d)", outfilename, error, errno);
		ok = false;
	}

	// Remove original file.
	if (ok)
	{
		result = remove(fname);
		if (result)
		{
			Errorf("fileGzip(): Error removing input file %s", fname);
			ok = false;
		}
	}

	return ok;
}


#if !PLATFORM_CONSOLE
static void logAccess(char *fname)
{
	static	void	*file;
	char	buf[1000];
	char	*s;

	if (!loadedGameDataDirs)
		return;
	if (!file)
	{

		sprintf(buf,"%s/file_access.log",fileLogDir());
		file = fopen(buf,"wt");
	}
	s = fileRelativePath(fname, buf);
	if (fileIsAbsolutePath(s))
		return;
	fprintf(file,"%s\n",s);
	fflush(file);
}
#endif

bool fgetEString(char **string, FILE *file)
{
	const char *newline = NULL;
	char buf[1024 + 1];
	bool added = false;

	estrClear(string);

	if(!file)
		return false;

	do 
	{
		if(fgets(buf, sizeof(buf), file))
		{
			newline = strchr_fast(buf, '\n');
			estrAppend2(string, buf);
			added = true;
		}
		else
		{
			return added;
		}
	} while (!newline && !feof(file->fptr));
	return true;
}

#undef fgetc
#undef fputc
#undef fseek
#undef fopen
#undef fclose
#undef ftell
#undef fread
#undef fwrite
#undef fgets
#undef fprintf
#undef fflush
#undef setvbuf
#undef FILE


typedef struct FileWritingBuffer FileWritingBuffer;

//NOTE NOTE make sure to keep these in sync with the actual functions in fileutil_opt.c
FileWritingBuffer *FileWritingBuffer_Create(void);
void FileWritingBuffer_Destroy(FileWritingBuffer **ppBuffer);
void FileWritingBuffer_FlushOldestFullBuffer(FileWritingBuffer *pBuffer, FILE *pFile);
void FileWritingBuffer_Flush(FileWritingBuffer *pBuffer, FILE *pFile);
void FileWritingBuffer_MaybeJustFilledABlock(FileWritingBuffer *pBuffer, FILE *pFile);
int FileWritingBuffer_PutC(FileWritingBuffer *pBuffer, FILE *pFile, char c);
S64 FileWritingBuffer_Tell(FileWritingBuffer *pBuffer);
S64 FileWritingBuffer_Seek(FileWritingBuffer *pBuffer, FILE *pFile, S64 dist,int whence);
intptr_t FileWritingBuffer_Fwrite(FileWritingBuffer *pBuffer, FILE *pFile, const char *pBufferToWrite, size_t size1, size_t size2);




MP_DEFINE(FileWrapper);

static CRITICAL_SECTION fileWrapperCritsec;
void initFileWrappers(void)
{
	static bool inited=false;
	if (!inited) {
		inited = true;
		InitializeCriticalSection(&fileWrapperCritsec);
	}
}

static FileWrapper* createFileWrapper(){
	FileWrapper	*fw = NULL;

	initFileWrappers();

	EnterCriticalSection(&fileWrapperCritsec);
	MP_CREATE(FileWrapper, 32);
	fw = MP_ALLOC(FileWrapper);
	LeaveCriticalSection(&fileWrapperCritsec);
	return fw;
}

static void destroyFileWrapper(FileWrapper *fw)
{
	if (fw->pWritingBuffer)
	{
		FileWritingBuffer_Destroy(&fw->pWritingBuffer);
	}

	EnterCriticalSection(&fileWrapperCritsec);
	MP_FREE(FileWrapper, fw);
	LeaveCriticalSection(&fileWrapperCritsec);
}

int glob_assert_file_err;

static int log_file_access=0;

int fileSetLogging(int on)
{
	int		ret = log_file_access;

	log_file_access = on;
	return ret;
}

void fileDisableWinIO(int disable) // Disables use of Windows ReadFile/CreateFile instead of CRT for binary files
{
	file_disable_winio = disable;
}

int stuffbuff_getc(StuffBuff *sb)
{
	if (sb->idx >= sb->size)
		return EOF;
	return sb->buff[sb->idx++];
}

long stuffbuff_fread(StuffBuff *sb, void *buf, int size)
{
	int sizeleft = sb->size - sb->idx;
	if (size > sizeleft)
		size = sizeleft;
	if (size <= 0)
		return 0;
	memcpy(buf, &(sb->buff[sb->idx]), size);
	sb->idx += size;
	return size;
}

static bool g_async_fopen;
static DWORD g_async_fopen_main_thread_id;
static ManagedThread *g_async_fopen_thread;

static DWORD WINAPI AsyncFopenThread(LPVOID lpParam)
{
	while (true)
		SleepEx(INFINITE, TRUE);
}

typedef struct AsyncFopenData {
	const char *name;
	const char *how;
	const char *caller_fname;
	int line;
	FileWrapper *ret;
	U32 lock;
	HANDLE eventDone;
} AsyncFopenData;

static void WINAPI asyncFopen(ULONG_PTR dwParam)
{
	AsyncFopenData *data = (AsyncFopenData*)dwParam;
	data->ret = x_fopen(data->name, data->how, data->caller_fname, data->line);
	if(_InterlockedOr(&data->lock, 1)){
		// The other thread is waiting.
		SetEvent(data->eventDone);
	}
}

static void fileStartFopenThread(void)
{
	ATOMIC_INIT_BEGIN;
	{
		assert(!g_async_fopen_thread);
		g_async_fopen_thread = tmCreateThread(AsyncFopenThread, 0);
		g_async_fopen_main_thread_id = GetCurrentThreadId();
	}
	ATOMIC_INIT_END;
}

void fileSetAsyncFopen(bool bAsync)
{
	if (bAsync == g_async_fopen)
		return;
	g_async_fopen = bAsync;
	if (g_async_fopen) {
		fileStartFopenThread();
	}
}

void *x_fopen_async(const char *name, const char *how, const char *caller_fname, int line)
{
	HANDLE* eventDone;
	AsyncFopenData data = {0};
	U32 msTimeStart = timeGetTime();
	bool displayedMessage = false;

	STATIC_THREAD_ALLOC_TYPE(eventDone, HANDLE);
	
	PERFINFO_AUTO_START_FUNC();

	data.name = name;
	data.how = how;
	data.caller_fname = caller_fname;
	data.line = line;
	if(!*eventDone){
		*eventDone = CreateEvent(NULL, FALSE, FALSE, NULL);
	}
	data.eventDone = *eventDone;
	tmQueueUserAPC(asyncFopen, g_async_fopen_thread, (ULONG_PTR)&data);
	while(1) {
		U32 result;
		if(_InterlockedOr(&data.lock, 2) == 1){
			// Done opening the handle, and this is the first check.  Subsequent checks have to
			// wait for the event so that it gets reset.
			break;
		}
		WaitForSingleObjectWithReturn(*eventDone, displayedMessage ? 100 : 3000, result);
		if(result == WAIT_OBJECT_0){
			break;
		}
		if (FALSE_THEN_SET(displayedMessage)){
			printf("Waiting to access %s (network drive is not responding)...", name);
		} else {
			static int index=0;
			char chars[] = "\\|/-";
			index = (index + 1) % ARRAY_SIZE(chars);
			printf("%c%c", 8/*backspace*/, chars[index]);
		}
		msTimeStart = timeGetTime();
	}
	if (displayedMessage)
		printf("%cdone.\n", 8);

	PERFINFO_AUTO_STOP();
	
	return data.ret;
}

FileWrapper *x_fopen(const char *_name, const char *how, const char *caller_fname, int line)
{
	// Check for invalid filenames.
	const char *filename = getFileNameConst(_name);
	if (strStartsWith(filename, "con.") || stricmp(filename, "con")==0 ||
		strStartsWith(filename, "nul.") || stricmp(filename, "nul")==0)
	{
		Errorf("Attempted to open a file named \"con\" or \"nul\", this is not allowed by the OS.");
		memlog_printf(NULL, "Attempted to open a file named \"con\", this is not allowed by the OS, denying request.");
		return NULL;
	}
	
	// Check for '?' which specifically indicates a file that is not valid on disk.
	if (_name[0] == '?')
		return NULL;

	if (g_async_fopen && GetCurrentThreadId() == g_async_fopen_main_thread_id && !isCrashed()) {
		// Only queue up async fopens from the main thread, assume background threads
		//  are expecting slower opens, and don't let one slow fopen stall all
		//  simultaneous fopens!
		return x_fopen_async(_name, how, caller_fname, line);
	} else {
	IOMode		iomode=IO_CRT;
	FileWrapper	*fw=0;
	char		fopenMode[128];
	char*		modeCursor;
	char		name[CRYPTIC_MAX_PATH];
	PigFileDescriptor pfd;
	bool		no_winio=false;
	bool		header_only=false;
	int			retries;
	bool		retry;

	g_file_stats.fopen_count++;

	glob_assert_file_err = 1;

	// Find a free file_wrapper
	fw = createFileWrapper();
	assert(fw);

	if (fileIsFileServerPath(_name))
	{
		iomode = IO_NETFILE;
		strcpy(name, _name);
		forwardSlashes(name);
	}
	else if (stricmp(how, "StuffBuff")==0) 
	{
		iomode = IO_STUFFBUFF;
	}
	else if (stricmp(how,"EString") == 0)
	{
		iomode = IO_ESTRING;
	}
	else
	{
		assert(strlen(_name) < CRYPTIC_MAX_PATH);
		strcpy(name, _name); // in case we destroy name along the way (i.e. call fileLocate)

#if PLATFORM_CONSOLE
		fileFixup(name, SAFESTR(name));
#endif
		assert(strlen(how) < ARRAY_SIZE(fopenMode));
		strcpy(fopenMode, how);

		// h indicates we are only going to read the header of the file (which is pre-cached in hoggs)
		modeCursor = strchr(fopenMode, 'h');
		if(modeCursor){
			size_t modeLen;
			// Remove ! from the fopen mode string to be given to the real fopen.
			modeLen = strlen(fopenMode);
			memmove(modeCursor, modeCursor + 1, modeLen - (modeCursor - fopenMode));
			header_only = true;
		}

		if (log_file_access && !header_only)
		{
#if PLATFORM_CONSOLE
			assert(0);
#else
			char	*s = strrchr(name,'.');

			if (!s || stricmp(s,".pigg")!=0)
				logAccess(name);
#endif
		}

#if !_PS3
        {
		    // Make sure we can open enough files.
    	    static int	maxStreamSet = 0;
		    if(!maxStreamSet){
			    _setmaxstdio(2048); // This is the CRT maximum!
			    maxStreamSet = 1;
		    }
        }
#endif

		// ! indicates not to use Win IO and just use the CRT instead (slow)
		modeCursor = strchr(fopenMode, '!');
		if(modeCursor){
			size_t modeLen;
			// Remove ! from the fopen mode string to be given to the real fopen.
			modeLen = strlen(fopenMode);
			memmove(modeCursor, modeCursor + 1, modeLen - (modeCursor - fopenMode));
			no_winio = true;
		}

		// If the character 'z' is used to open the file, use zlib for file IO.
		modeCursor = strchr(fopenMode, 'z');
		if(modeCursor){
			size_t modeLen;

			// Remove z from the fopen mode string to be given to the real fopen.
			modeLen = strlen(fopenMode);
			memmove(modeCursor, modeCursor + 1, modeLen - (modeCursor - fopenMode));

			iomode = IO_GZ;

			assertmsgf(strchr(fopenMode, 'b') || strchr(fopenMode, 'B'), "File %s being opened with zip option in non-binary mode. This will not work", _name);

			// printf("found gz file\n");
		} else {
			// Check for specifically not looking in the pigg files
			modeCursor = strchr(fopenMode, '~');
			if(modeCursor){ // If it specifically says not to use Piglib, or if it's a write-access request
				size_t modeLen;

				// Remove ~ from the fopen mode string to be given to the real fopen.
				modeLen = strlen(fopenMode);
				memmove(modeCursor, modeCursor + 1, modeLen - (modeCursor - fopenMode));

				iomode = IO_CRT;
				// printf("found ~, normal file\n");
			} else if (strcspn(how, "wWaA+")!=strlen(how)) {
				iomode = IO_CRT;
				assert(!is_pigged_path(name));
				// printf("found wWaA+, normal file\n");
			} else {
				// If we got here, we're doing a normal open for reading,
				// Maybe we should do a double-check to see if this is a path relative to the game data dir, and if so,
				//   re-locate it with fileLocateRead, hoping to find it in a pig file (this will
				//   happen in the code that calls fopen without calling fileLocate first)
				// check to see if the path has two ':'s, and if so, we will open from a pig file
				if (is_pigged_path(name)) {
					if (name[0] == '#')
					{
						// Dynamic hog
						char hogname[MAX_PATH];
						char *s;
						iomode = IO_PIG;

						// Get hog name and file name
						strcpy(hogname, name+1);
						s = strchr(hogname, '#');
						*s = '\0';
						strcpy(name, s+1);
						pfd = PigSetGetFileInfoFake(hogname, name);
						assertmsg(pfd.parent_hog, "fopen() called on a file in a dynamic hogg which does not exist");
					} else {
						// Filesystem hogg
						FolderNode * node;
						bool pfd_filled=false;
						iomode = IO_PIG;
						strcpy(name, strrchr(name, ':')+2); // remove the path to the pigg...
						PERFINFO_AUTO_START("FolderCacheQuery", 1);
						node = FolderCacheQuery(folder_cache, name);
						PERFINFO_AUTO_STOP();
						assert(node);

						// ensure patched
						if (node->needs_patching && folder_cache->patchCallback)
						{
							// Patch and re-query for node
							// Callback *must* release the critical section
							PERFINFO_AUTO_START("patchCallback", 1);
							pfd_filled = folder_cache->patchCallback(folder_cache, node, name, &pfd, header_only);
							PERFINFO_AUTO_STOP_START("FolderCacheQuery", 1);
							node = FolderCacheQuery(folder_cache, name);
							PERFINFO_AUTO_STOP();
							assert(node);
						}

						if (!pfd_filled)
						{
							if (node->virtual_location>=0 && FolderCacheGetMode()== FOLDER_CACHE_MODE_FILESYSTEM_ONLY) {
								// We thought it was in a pig, but now it's not, and we're in FILESYSTEM_ONLY mode,
								//  so pigs must have been disabled in another thread during this call or something.
								// Requery
								FolderCacheGetRealPath(folder_cache, node, SAFESTR(name));
								iomode = IO_CRT;
								FolderNodeLeaveCriticalSection();
							} else {
								int virtual_location = node->virtual_location;
								int file_index = node->file_index;
								assert(virtual_location<0);
								assert(file_index>=0);
								FolderNodeLeaveCriticalSection(); // Need to leave this before calling PigSetGetFileInfo to avoid possible deadlock
								pfd = PigSetGetFileInfo(VIRTUAL_LOCATION_TO_PIG_INDEX(virtual_location), file_index, name);
								assert(pfd.parent_hog);
								// printf("found pigged path, pig file\n");
							}
						} else {
							FolderNodeLeaveCriticalSection();
						}
					}
				} else {
					iomode = IO_CRT;
					// printf("default case, normal file\n");
				}
			}
		}
	}

	if (iomode == IO_CRT) {
		// Make sure we're not writing out extraneous files in Production Edit mode
		if (isProductionEditMode() && !isDevelopmentMode() && strchr(fopenMode, 'w')!=0 &&
			fileIsInDataDirs(name))
		{
			char ns_dir[MAX_PATH], full_path[MAX_PATH];
			sprintf(ns_dir, "%s/ns", fileDataDir());
			fileLocateWrite(name, full_path);
			if (!strStartsWith(full_path, fileLocalDataDir()) &&
				!strStartsWith(full_path, ns_dir))
			{
				if (IsServer())
					AssertOrAlert("PRODUCTION_WRITE_TO_DATA", "Attempted to write to data file %s in Production Edit mode!", name);
				else
					Errorf("Attempted to write to data file %s in Production Edit mode!", name);
			}
		}

		// If this is a binary file, and not append mode, open it with WINIO instead
		if (!file_disable_winio && strchr(fopenMode, 'b')!=0 && !strchr(fopenMode, 'a') && !no_winio)
			iomode = IO_WINIO;
	}

	fw->iomode = iomode;
	fw->caller_fname = caller_fname;
	fw->line = line;
	retries=0;
	retry=true;
	while (retry && retries < 2)
	{
		retry = false;
		retries++;
		switch(iomode) {
			xcase IO_GZ:
				fileDiskAccessCheck();
				PERFINFO_AUTO_START("x_fopen:gzopen", 1);
				//printf("opening gz file\n");
				fw->fptr = gzopen(name,fopenMode);
				if (!fw->fptr) {
					fileAttemptNetworkReconnect(name);
					retry = true;
				}
			xcase IO_PIG:
				PERFINFO_AUTO_START("x_fopen:pig_fopen_pfd", 1);
				//printf("opening pigged file\n");
				fw->fptr = pig_fopen_pfd(&pfd, fopenMode);
			xcase IO_STUFFBUFF:
				PERFINFO_AUTO_START("x_fopen:StuffBuff", 1);
				fw->fptr = (void *)_name;
			xcase IO_ESTRING:
				PERFINFO_AUTO_START("x_fopen:EString", 1);
				fw->fptr = (void *)_name;
			xcase IO_CRT:
				fileDiskAccessCheck();
				//printf("opening normal file\n");
				if (!name || !name[0]) // to avoid dumb assert in msoft's debug c lib
				{
					PERFINFO_AUTO_START("x_fopen:badfilename", 1);
					fw->fptr = 0;
					//printf("empty filename\n");
				}
				else
				{
					char *pOpenModeCopy = NULL;
					estrStackCreate(&pOpenModeCopy);
					estrCopy2(&pOpenModeCopy, fopenMode);

					if (strchr(fopenMode, 'm'))
					{
						assertmsgf(strchr(fopenMode, 'w') && strchr(fopenMode, 'b'), "Trying to open %s with 'm' but not 'w', this is illegal", name);

						fw->pWritingBuffer = FileWritingBuffer_Create();
						estrReplaceOccurrences(&pOpenModeCopy, "m", "");
					}
						

#if _XBOX 
					backSlashes(name);
#endif
					PERFINFO_AUTO_START_BLOCKING("x_fopen:fopen", 1);
#if _PS3
                    fw->fptr = fopen(name, pOpenModeCopy);
#else
					fw->fptr = _wfsopen_UTF8(name, pOpenModeCopy, _SH_DENYNO);
#endif
					if (!fw->fptr) {
						fileAttemptNetworkReconnect(name);
						retry = true;
					}

					estrDestroy(&pOpenModeCopy);
				}
			xcase IO_WINIO:
				fileDiskAccessCheck();
#if _PS3
                {
                    int e, fd;
                    int flags = 0;
                    char *c;

					for (c=fopenMode; *c; c++) {
						switch (*c) {
							xcase 'r':
								if (c[1]=='+')
									flags |= CELL_FS_O_RDWR;
                                else
                                    flags |= CELL_FS_O_RDONLY;
							xcase 'w':
								flags |= CELL_FS_O_CREAT;
								if (c[1]=='+')
									flags |= CELL_FS_O_RDWR;
								else
									flags |= CELL_FS_O_WRONLY|CELL_FS_O_TRUNC;
							xcase 'b':
								// already handled
							xcase 'c':
								// Commit flag
								//   Doesn't seem to change performance, is it doing anything?
							xcase '+':
							xdefault:
								assertmsg(0, "Bad file open mode passed to fopen");
						}
					}

                    e = cellFsOpen(name, flags, &fd, 0, 0);
                    if(!e) {
                        fw->fptr = (void*)fd;
                    } else {
                        fw->fptr = 0;
                    }
					if (!fw->fptr) {
						fileAttemptNetworkReconnect(name);
						retry = true;
					}
                }
#else
				{
					char *c;
					DWORD accessMode = 0, creationDisposition = 0;
					DWORD sharingMode = FILE_SHARE_READ|FILE_SHARE_WRITE; // Let anyone else open this file for any purpose
					DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
					PERFINFO_AUTO_START("x_fopen:CreateFile", 1);
#if _XBOX 
					backSlashes(name);
#endif
					if (isDevelopmentMode())
						sharingMode|=FILE_SHARE_DELETE;
					for (c=fopenMode; *c; c++) {
						switch (*c) {
							xcase 'r':
								accessMode |= GENERIC_READ;
								creationDisposition = OPEN_EXISTING;
								if (c[1]=='+')
									accessMode |= GENERIC_WRITE;
							xcase 'w':
								accessMode |= GENERIC_WRITE;
								creationDisposition = CREATE_ALWAYS;
								if (c[1]=='+')
									accessMode |= GENERIC_READ;
								else
									sharingMode = FILE_SHARE_READ;  // Exclusive writing, or do we want exclusive everything?
							xcase 'b':
								// already handled
							xcase 'c':
								// Commit flag
								//   Doesn't seem to change performance, is it doing anything?
								dwFlagsAndAttributes |= FILE_FLAG_WRITE_THROUGH;
							xcase '+':
							xdefault:
								assertmsg(0, "Bad file open mode passed to fopen");
						}
					}
					// FILE_SHARE_DELETE is not supported under Windows 95/98/ME

					if ( !IsUsingWin2kOrXp() && !IsUsingXbox() && sharingMode & FILE_SHARE_DELETE )
						sharingMode &= ~FILE_SHARE_DELETE;

					// Use Windows backup semantics, if requested, generally to bypass read ACLs.
					if (file_windows_backup_semantics)
						dwFlagsAndAttributes |= FILE_FLAG_BACKUP_SEMANTICS;

					PERFINFO_AUTO_START_BLOCKING("CreateFile", 1);
					fw->fptr = CreateFile_UTF8(name, accessMode, sharingMode, NULL, creationDisposition, dwFlagsAndAttributes, NULL);
					PERFINFO_AUTO_STOP();
					
					if (fw->fptr == INVALID_HANDLE_VALUE)
					{
						fw->fptr = NULL;
					}
					if (!fw->fptr) {
						fileAttemptNetworkReconnect(name);
						retry = true;
					} else {
						x_fseek(fw, 0, SEEK_SET);
					}
				}
#endif
			xcase IO_NETFILE:
			{
				PERFINFO_AUTO_START("x_fopen:NetFile", 1);
				fw->fptr = fileServerClient_fopen(name, how);
			}
		}
		PERFINFO_AUTO_STOP();
	}
	
	if (fw->fptr)
	{
		if (fw->iomode == IO_ESTRING || fw->iomode == IO_STUFFBUFF)
		{
			fw->nameptr = NULL;
		}
		else if (fw->nameptr = allocFindString(name))
		{
			fw->nameptr_needs_freeing = 0;
		}
		else 
		{
			fw->nameptr = strdup(name);
			fw->nameptr_needs_freeing = 1;
		}
		glob_assert_file_err = 0;

		return fw;
	}
//	printf("filewrapper's file pointer is not valid\n");
	destroyFileWrapper(fw);
	glob_assert_file_err = 0;
	return 0;
	}
}

int x_fclose(FileWrapper *fw)
{
	void *fptr;
	IOMode iomode;
	static int zero=0;
	int ret=-1;

	if(!fw)
		return 1;

	fptr = fw->fptr;
	iomode = fw->iomode;

	if (fw->nameptr_needs_freeing) {
        SAFE_FREE(*(char**)&fw->nameptr);
	}
	fw->nameptr = NULL;
	fw->nameptr_needs_freeing = 0;
	fw->fptr = 0;
	switch(iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fclose:gzclose", 1);
			ret = gzclose(fptr);
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_fclose:pig_fclose", 1);
			ret = pig_fclose(fptr);
		xcase IO_STUFFBUFF:
			PERFINFO_AUTO_START("x_fclose:addBinaryDataToStuffBuff", 1);
			addBinaryDataToStuffBuff((StuffBuff*)fptr, (char*)&zero, 1);
			ret = 0;
		xcase IO_ESTRING:
			PERFINFO_AUTO_START("x_fclose:EString", 1);
			ret = 0;
		xcase IO_CRT:
			PERFINFO_AUTO_START_BLOCKING("x_fclose:CRT_fclose", 1);
			if (fw->pWritingBuffer)
			{
				FileWritingBuffer_Flush(fw->pWritingBuffer, fptr);
			}
			ret = fclose(fptr);
		xcase IO_NETFILE:
			PERFINFO_AUTO_START("x_fclose:netfile", 1);
			ret = fileServerClient_fclose(fptr);
		xcase IO_WINIO:
			PERFINFO_AUTO_START_BLOCKING("x_fclose:CloseHandle", 1);
#if _PS3
            {
                int e, fd = (int)fptr;
                e = cellFsClose(fd);
                if(!e)
                    ret = 0;
            }
#else
			ret = !CloseHandle(fptr);
#endif
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fptr;
			PERFINFO_AUTO_START_BLOCKING("x_fclose:RAMCached", 1);
			if (rcf->buffer_owned)
			{
				SAFE_FREE(rcf->buffer);
				x_fclose(rcf->base_file);
			}
			free(rcf);
		}
		xdefault:
			PERFINFO_AUTO_START("x_fclose:default", 1);
	}
	destroyFileWrapper(fw);
	PERFINFO_AUTO_STOP();
	return ret;
}

int x_fgetc(FileWrapper *fw)
{
	int ret=-1;

    if(fw->iomode == IO_WINIO) {
        U8 byte;
        if(x_fread(&byte,1,1,fw) == 1)
            return byte;
        return -1;
    }

    switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fgetc:gzgetc", 1);
			ret = gzgetc(fw->fptr);
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_fgetc:pig_fgetc", 1);
			ret = pig_fgetc(fw->fptr, fw->caller_fname, fw->line);
		xcase IO_STUFFBUFF:
		case IO_ESTRING:
			PERFINFO_AUTO_START("x_fgetc:error", 1);
			assertmsg(0,"String Wrapper Files are write-only");
			ret = 0;
		xcase IO_CRT:
			PERFINFO_AUTO_START("x_fgetc:fgetc", 1);
			ret = fgetc(fw->fptr);
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
			PERFINFO_AUTO_START("x_fgetc:RAMCached", 1);
			if (rcf->buffer)
			{
				if (rcf->pos >= rcf->bufferlen) {
					ret = EOF;
				} else {
					ret = rcf->buffer[rcf->pos++];
				}
			} else {
				ret = x_fgetc(rcf->base_file);
			}
		}	
		xdefault:
			PERFINFO_AUTO_START("x_fgetc:default", 1);
			assert(0);
	}
	PERFINFO_AUTO_STOP();
	return ret;
}

int x_fputc(int c, FileWrapper *fw)
{
	int ret=-1;

    if(fw->iomode == IO_WINIO) {
        U8 byte = (U8)c;
        if(x_fwrite(&byte,1,1,fw) == 1)
            return byte;
        return -1;
    }
	
	switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fputc:gzputc", 1);
			ret = gzputc(fw->fptr, c);
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_fputc:pigassert", 1);
			assert(false);
		xcase IO_STUFFBUFF:
			PERFINFO_AUTO_START("x_fputc:addBinaryDataToStuffBuff", 1);
			addBinaryDataToStuffBuff((StuffBuff*)fw->fptr, (char *)&c, 1);
			ret = 1;
		xcase IO_ESTRING:
			PERFINFO_AUTO_START("x_fputc:estrConcatChar",1);
			estrConcatChar_dbg((char **)fw->fptr, c, 1, fw->caller_fname, fw->line);
			ret = 1;
		xcase IO_CRT:
			PERFINFO_AUTO_START("x_fputc:fputc", 1);
			if (fw->pWritingBuffer)
			{
				ret = FileWritingBuffer_PutC(fw->pWritingBuffer, fw->fptr, c);
			}
			else
			{
				ret = fputc(c,fw->fptr);
			}
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
			PERFINFO_AUTO_START("x_fputc:RAMCached", 1);
			assert(!rcf->assert_on_write);
			assert(!rcf->buffer); // We're failing to update the buffer and buffer pos, but we could implement this
			ret = x_fputc(c, rcf->base_file);
		}	
		xdefault:
			PERFINFO_AUTO_START("x_fputc:default", 1);
			assert(0);
	}
	PERFINFO_AUTO_STOP();
	return ret;
}

S64 x_fseek(FileWrapper *fw, S64 dist,int whence)
{
	S64 ret=-1;
	switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fseek:gzseek", 1);
			ret = gzseek(fw->fptr,(long)dist,whence);
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_fseek:pig_fseek", 1);
			ret = pig_fseek(fw->fptr,(long)dist,whence);
		xcase IO_ESTRING:
			PERFINFO_AUTO_START("x_fseek:EString", 1);
			switch (whence)
			{
			xcase SEEK_SET:
				estrSetSize((char **)fw->fptr, dist);
				break;
			xcase SEEK_CUR:
			xcase SEEK_END:
				estrSetSize((char **)fw->fptr, estrLength((char **)fw->fptr) + dist);
				break;
			}
			ret = estrLength((char **)fw->fptr);

		xcase IO_STUFFBUFF:
			PERFINFO_AUTO_START("x_fseek:stuffbuff", 1);
			assertmsg(0,"String Wrapper Files are write-only");
			ret = 0;
		xcase IO_CRT:
			PERFINFO_AUTO_START("x_fseek:fseek", 1);
#if _PS3
            if(!(dist>>30)) {
                ret = fseek(fw->fptr, (int)dist, whence);
            } else {
                assert(0);
                /*
                int fd = fileno(fw->fptr);
                fflush(fw->fptr);
                if(lseek64(fd, dist, whence) >= 0)
                    ret = 0;
                */
            }
#else
			if (fw->pWritingBuffer)
			{
				ret = FileWritingBuffer_Seek(fw->pWritingBuffer, fw->fptr, dist, whence);
			}
			else
			{
				switch (whence) {
				xcase SEEK_SET:
					//dist = dist;
				xcase SEEK_CUR:
				{
					S64 oldpos;
					fgetpos(fw->fptr, &oldpos);
					dist += oldpos;
				}
				xcase SEEK_END:
					dist = _filelength(_fileno(fw->fptr)) + dist;
				xdefault:
					assert(0);
				}
				ret = fsetpos(fw->fptr,&dist);
			}
#endif

		xcase IO_NETFILE:
			PERFINFO_AUTO_START("x_fseek:netfile", 1);
			ret = fileServerClient_fseek(fw->fptr, dist, whence);

		xcase IO_WINIO:
			PERFINFO_AUTO_START_BLOCKING("x_fseek:SetFilePointer", 1);
#if _PS3
            {
                uint64_t pos;
                int e, fd = (int)fw->fptr;
                e = cellFsLseek(fd, dist, whence, &pos);
                if(!e)
                    ret = 0;
            }
#else
			{
				LARGE_INTEGER offs, newoffs;
				offs.QuadPart = dist;
				switch(whence)
				{
					xcase SEEK_SET:
						ret = !SetFilePointerEx(fw->fptr, offs, &newoffs, FILE_BEGIN);
					xcase SEEK_END:
						ret = !SetFilePointerEx(fw->fptr, offs, &newoffs, FILE_END);
					xcase SEEK_CUR:
						ret = !SetFilePointerEx(fw->fptr, offs, &newoffs, FILE_CURRENT);
					xdefault:
						assert(0);
				}
			}
#endif
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
			PERFINFO_AUTO_START("x_fseek:RAMCached", 1);
			if (rcf->buffer)
			{
				size_t newpos = rcf->pos;
				switch(whence) {
				xcase SEEK_SET:
					assert(dist>=0);
					newpos = dist;
				xcase SEEK_CUR:
					newpos += dist;
				xcase SEEK_END:
					newpos = rcf->bufferlen + dist;
				xdefault:
					assert(false);
				}
				if (newpos > rcf->bufferlen) {
					rcf->pos = rcf->bufferlen;
					ret = -1;
				} else if (newpos < 0) {
					rcf->pos = 0;
					ret = -1;
				} else {
					rcf->pos = newpos;
					ret = 0;
				}
			} else {
				ret = x_fseek(rcf->base_file, dist, whence);
			}
		}

		xdefault:
			PERFINFO_AUTO_START("x_fseek:default", 1);
	}
	PERFINFO_AUTO_STOP();
	return ret;
}

#if !_PS3
// Functionally identical to SetFilePointerEx, but runs on Win98
BOOL SetFilePointerExWin98(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
{
	LARGE_INTEGER li;

	li.QuadPart = liDistanceToMove.QuadPart;

	li.LowPart = SetFilePointer (hFile, li.LowPart, &li.HighPart, dwMoveMethod);

	if (li.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		// Failure
		li.QuadPart = -1;
		return FALSE;
	}

	lpNewFilePointer->QuadPart = li.QuadPart;
	return TRUE;
}
#endif

S64 x_ftell_internal(FileWrapper *fw)
{
	S64 ret=-1;
	switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_ftell:gztell", 1);
			ret = gztell(fw->fptr);
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_ftell:pig_ftell", 1);
			ret = pig_ftell(fw->fptr);
		xcase IO_STUFFBUFF:
			PERFINFO_AUTO_START("x_ftell:stuffBuff", 1);
			ret = ((StuffBuff*)fw->fptr)->idx;
		xcase IO_ESTRING:
			PERFINFO_AUTO_START("x_ftell:estring", 1);
			ret = estrLength((char **)fw->fptr);
		xcase IO_CRT:
			PERFINFO_AUTO_START("x_ftell:ftell", 1);
#if _PS3
            {
                return ftell(fw->fptr);
                /*
                int fd = fileno(fw->fptr);
                fflush(fw->fptr);
                return lseek64(fd, 0, SEEK_CUR);
                */
            }
#else
			if (fw->pWritingBuffer)
			{
				return FileWritingBuffer_Tell(fw->pWritingBuffer);
			}
			else
			{
				if (fgetpos(fw->fptr, &ret))
					ret = -1;
			}
#endif
		xcase IO_NETFILE:
			PERFINFO_AUTO_START("x_ftell:netfile", 1);
			ret = fileServerClient_ftell(fw->fptr);

		xcase IO_WINIO:
			PERFINFO_AUTO_START("x_ftell:SetFilePointer", 1);
#if _PS3
            {
                uint64_t pos;
                int e, fd = (int)fw->fptr;
                e = cellFsLseek(fd, 0, CELL_FS_SEEK_CUR, &pos);
                if(!e)
                    ret = pos;
            }
#else
			{
				LARGE_INTEGER zero = {0};
				LARGE_INTEGER newpos;
				if (SetFilePointerExWin98(fw->fptr,zero,&newpos,FILE_CURRENT))
					ret = newpos.QuadPart;
			}
#endif
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
			PERFINFO_AUTO_START("x_ftell:RAMCached", 1);
			if (rcf->buffer)
			{
				ret = rcf->pos;
			} else {
				ret = x_ftell_internal(rcf->base_file);
			}
		}	
		xdefault:
			PERFINFO_AUTO_START("x_ftell:default", 1);
	}
	PERFINFO_AUTO_STOP();
	return ret;
}

size_t x_fread(void *buf,size_t size1,size_t size2,FileWrapper *fw)
{
	intptr_t ret=-1;
	if (!size1 || !size2)
		return 0;
	assert(buf);

	switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fread:gzread", 1);
			fileDiskAccessCheck();
			ret = gzread(fw->fptr, buf, (unsigned int)(size1*size2)) / size1;
			if(ret < 0) ret = 0; // fread() is not supposed to return a negative value
			g_file_stats.fread_count++;
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_fread:pig_fread", 1);
			ret = pig_fread(fw->fptr, buf, (long)(size1*size2), fw->caller_fname, fw->line) / size1;
		xcase IO_STUFFBUFF:
		case IO_ESTRING:
			PERFINFO_AUTO_START("x_fread:error", 1);
			assertmsg(0,"String Wrapper Files are write-only");
			ret = 0;
		xcase IO_CRT:
			PERFINFO_AUTO_START_BLOCKING("x_fread:fread", 1);
			fileDiskAccessCheck();
			ret = fread(buf,size1,size2,fw->fptr);
			g_file_stats.fread_count++;
		xcase IO_NETFILE:
			PERFINFO_AUTO_START("x_fread:fileServerClient_fread", 1);
			fileDiskAccessCheck();
			ret = fileServerClient_fread(buf,size1*size2,fw->fptr);
			g_file_stats.fread_count++;

		xcase IO_WINIO:
			PERFINFO_AUTO_START_BLOCKING("x_fread:ReadFile", 1);
			fileDiskAccessCheck();
			g_file_stats.fread_count++;
#if _PS3
            {
                uint64_t count;
                int e, fd = (int)fw->fptr;
                e = cellFsRead(fd, buf, (size1*size2), &count);
                if(!e)
                    ret = count/size1;
            }
#else
            {
				U8 * start = buf;
				size_t bytesLeft = size1*size2;
				DWORD bytesToRead;
				DWORD bytesRead;
				BOOL result;
				do {
					bytesToRead = (DWORD)MIN(bytesLeft, S32_MAX);
					result = ReadFile(fw->fptr, buf, bytesToRead, &bytesRead, NULL);
					bytesLeft -= bytesRead;
					buf = (U8*)buf + bytesRead;
				} while (result && bytesLeft && bytesRead == bytesToRead);

				ret = ((U8*)buf-start)/size1;
            }
#endif
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
			size_t bytes = size1*size2;
			PERFINFO_AUTO_START("x_fread:RAMCached", 1);
			if (rcf->buffer)
			{
				assert(rcf->pos < rcf->bufferlen);
				if (bytes > rcf->bufferlen - rcf->pos)
				{
					size2 = (rcf->bufferlen - rcf->pos) / size1;
					bytes = size1*size2;
				}
				memcpy(buf, rcf->buffer + rcf->pos, bytes);
				rcf->pos += bytes;
				ret = size2;
			} else {
				ret = x_fread(buf, size1, size2, rcf->base_file);
			}
		}	
		xdefault:
			PERFINFO_AUTO_START("x_fread:default", 1);
	}
	ADD_MISC_COUNT(size1 * size2, "bytes_read");

#if PROFILE_PERF
	if ( timeGetCurrProcessor() == 0 )
	{
		PERF_LOG_ON();
	}
#endif
	PERFINFO_AUTO_STOP();
#if PROFILE_PERF
	if ( timeGetCurrProcessor() == 0 )
	{
		OutputDebugStringf( "%s\n", fw->nameptr );
		PERF_LOG_OFF();
	}
#endif
	
	return ret;
}

size_t x_fwrite_internal(const void *buf,size_t size1,size_t size2,FileWrapper *fw)
{
	intptr_t ret=-1;
	
	if (!size1 || !size2) return 0;
	
	switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fwrite:gzwrite", 1);
			ret = gzwrite(fw->fptr,(void*)buf,(unsigned int)(size1 * size2)) / size1;
			g_file_stats.fwrite_count++;
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_fwrite:assert", 1);
			assert(false);
			ret = 0;
		xcase IO_STUFFBUFF:
			PERFINFO_AUTO_START("x_fwrite:addBinaryDataToStuffBuff", 1);
			addBinaryDataToStuffBuff((StuffBuff*)fw->fptr, buf, (int)(size1*size2));
			ret = size1*size2;
		xcase IO_ESTRING:
			PERFINFO_AUTO_START("x_fwrite:estrConcat",1);
			estrConcat_dbg((char **)fw->fptr,buf,(int)(size1*size2), fw->caller_fname, fw->line);
			ret = size1*size2;
		xcase IO_CRT:
			PERFINFO_AUTO_START_BLOCKING("x_fwrite:fwrite", 1);
			if (fw->pWritingBuffer)
			{
				ret = FileWritingBuffer_Fwrite(fw->pWritingBuffer, fw->fptr, buf, size1, size2);
			}
			else
			{
				ret = fwrite(buf,size1,size2,fw->fptr);
			}

			g_file_stats.fwrite_count++;
			
		xcase IO_WINIO:
			PERFINFO_AUTO_START_BLOCKING("x_fwrite:WriteFile", 1);
#if _PS3
            {
                uint64_t count;
                int e, fd = (int)fw->fptr;
                e = cellFsWrite(fd, buf, (size1*size2), &count);
                if(!e)
                    ret = count/size1;
            }
#else
            {
                DWORD count;
			    ret = WriteFile(fw->fptr, buf, (DWORD)(size1*size2), &count, NULL);
				if (count != size1*size2)
					fw->error = true;
			    if (ret)
				    ret = count/size1;
            }
#endif
			g_file_stats.fwrite_count++;
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
			PERFINFO_AUTO_START("x_fwrite:RAMCached", 1);
			assert(!rcf->assert_on_write);
			assert(!rcf->buffer); // Would need to write into buffer
			ret = x_fwrite(buf, size1, size2, rcf->base_file);
		}

		xdefault:
			PERFINFO_AUTO_START("x_fwrite:default", 1);
	}

	ADD_MISC_COUNT(size1 * size2, "bytes_attempted");
	ADD_MISC_COUNT(ret, "bytes_written");
	PERFINFO_AUTO_STOP();
	return ret;
}

char *x_fgets(char *buf,int len,FileWrapper *fw)
{
	char* ret=NULL;
	switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fgets:gzgets", 1);
			ret = gzgets(fw->fptr,buf,len);
		xcase IO_PIG:
			PERFINFO_AUTO_START("x_fgets:pig_fgets", 1);
			ret = pig_fgets(fw->fptr,buf,len, fw->caller_fname, fw->line);
		xcase IO_STUFFBUFF:
		case IO_ESTRING:
			PERFINFO_AUTO_START("x_fgets:error", 1);
			assertmsg(0,"String Wrapper Files are write-only");
			ret = 0;
		xcase IO_CRT:
			PERFINFO_AUTO_START("x_fgets:fgets", 1);
			ret = fgets(buf,len,fw->fptr);
        xcase IO_WINIO:
			PERFINFO_AUTO_START("x_fgets:assert", 1);
			assertmsg(0, "fgets not supported on binary files");
			ret=0;
        xcase IO_RAMCACHED:
			PERFINFO_AUTO_START("x_fgets:assert", 1);
			assertmsg(0, "fgets not supported on binary files");
			ret=0;
		xdefault:
			PERFINFO_AUTO_START("x_fgets:default", 1);
	}
	PERFINFO_AUTO_STOP();
	return ret;
}

int g_fflush_count;

void x_fflush(FileWrapper *fw)
{
	g_fflush_count++;
	switch(fw->iomode) {
		xcase IO_GZ:
			PERFINFO_AUTO_START("x_fflush:gzflush", 1);
			gzflush(fw->fptr,0);
		xcase IO_PIG:
		case IO_STUFFBUFF:
		case IO_ESTRING:
			// These don't need to flush, but it is harmless
			return;
		xcase IO_CRT:
			PERFINFO_AUTO_START("x_fflush:fflush", 1);
			if (fw->pWritingBuffer)
			{
				FileWritingBuffer_Flush(fw->pWritingBuffer, fw->fptr);
			}
			fflush(fw->fptr);
        xcase IO_WINIO:
#if _PS3
            {
                int e, fd = (int)fw->fptr;
                e = cellFsFsync(fd);
            }
#else
			PERFINFO_AUTO_START("x_fflush:FlushFileBuffers", 1);
			FlushFileBuffers(fw->fptr);
#endif
		xcase IO_RAMCACHED:
		{
			RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
			PERFINFO_AUTO_START("x_fflush:RAMCached", 1);
			assert(!rcf->assert_on_write);
			assert(!rcf->buffer); // Would need to write into buffer
			x_fflush(rcf->base_file);
		}	

		xdefault:
			PERFINFO_AUTO_START("x_fflush:default", 1);
	}
	PERFINFO_AUTO_STOP();
}

int x_setvbuf(FileWrapper* fw, char *buffer, int mode, size_t size)
{
    if(fw->iomode == IO_CRT)
        return setvbuf(fw->fptr, buffer, mode, size);
	else if (fw->iomode == IO_PIG)
		return pig_setvbuf(fw->fptr, buffer, mode, size);

	return 0; // ignore
}

int x_vfprintf(FileWrapper *fw, const char *format, va_list va)
{
	char* buf = NULL;
	int len;
	int ret;

	estrStackCreate(&buf);
	len = estrConcatfv(&buf, (char*)format, va);
    ret = (int)x_fwrite(buf,1,len,fw);
	estrDestroy(&buf);
	return ret;
}


int x_fprintf(FileWrapper *fw, const char *format, ...)
{
	int ret;

    VA_START(va, format);
	ret = x_vfprintf(fw, format, va);
    VA_END();
    
    return ret;
}

int x_ferror(FileWrapper* fw)
{
	if (fw->error)
		return 1;

	switch(fw->iomode)
	{
	xcase IO_CRT:
#if _PS3
        return ferror(fw->fptr);
#else
#undef FILE
		// WARNING!!! MS CRT specifc.
		return ((FILE*)fw->fptr)->_flag & _IOERR;
#define FILE FileWrapper
#endif
	xcase IO_GZ:
		return gzflush(fw->fptr,0);
	}
	return 0;
}

FileWrapper *fileOpenRAMCached_dbg(const char *fname, const char *how, const char *caller_fname, int line)
{
	FileWrapper *fw;
	RAMCachedFile *rcf;
	FileWrapper *f_internal = x_fopen(fname, how, caller_fname, line);
	size_t r;
	S64 iFileSize;

	if (!f_internal)
		return NULL;
	fw = createFileWrapper();
	rcf = calloc(sizeof(*rcf),1);
	rcf->base_file = f_internal;
	fw->fptr = rcf;
	fw->caller_fname = caller_fname;
	fw->line = line;
	fw->iomode = IO_RAMCACHED;
	fw->nameptr = strdup(fname);
	fw->nameptr_needs_freeing = 1;
	rcf->assert_on_write = 1;
	rcf->buffer_owned = 1;

	iFileSize = fileGetSize64(f_internal);
	assert(iFileSize <= (size_t)-1);
	rcf->bufferlen = (size_t)iFileSize;
	rcf->buffer = malloc(rcf->bufferlen);
	r = x_fread(rcf->buffer, 1, rcf->bufferlen, rcf->base_file);
	assert(r == rcf->bufferlen);
	return fw;
}

FileWrapper *fileOpenRAMCachedPreallocated_dbg(const char *buffer, size_t buffer_size, const char *caller_fname, int line)
{
	FileWrapper *fw;
	RAMCachedFile *rcf;

	assert(buffer);

	fw = createFileWrapper();
	rcf = calloc(sizeof(*rcf),1);
	fw->fptr = rcf;
	fw->caller_fname = caller_fname;
	fw->line = line;
	fw->iomode = IO_RAMCACHED;
	rcf->assert_on_write = 1;

	assert(buffer_size <= (size_t)-1);
	rcf->bufferlen = buffer_size;
	rcf->buffer = (char *)buffer;			// Remove const, but we won't overwrite it.
	return fw;
}

// unsafe to call this if we still have a buffer
void fileRAMCachedAssertOnWrite(FileWrapper *f, bool do_assert)
{
	assert(f->iomode == IO_RAMCACHED);
	((RAMCachedFile*)f->fptr)->assert_on_write = do_assert;
}

void fileRAMCachedFreeCache(FileWrapper *f)
{
	RAMCachedFile *rcf = (RAMCachedFile*)f->fptr;
	assert(f->iomode == IO_RAMCACHED);
	assert(rcf->buffer);

	assertmsg(rcf->buffer_owned, "RAMCached: We can only switch to read/write mode if we own the buffer, since without one, there's no file to fall back to.");

	if (rcf->buffer)
	{
		x_fseek(rcf->base_file, rcf->pos, SEEK_SET);
		rcf->assert_on_write = 0;
		SAFE_FREE(rcf->buffer);
		rcf->bufferlen = 0;
		rcf->pos = 0;
	}
}


int fileTruncate(FileWrapper *fw, U64 newsize)
{
	switch(fw->iomode)
	{
	xcase IO_CRT:
#if _PS3
		if (newsize > 0x7ffffffLL) {
			// Too large of a value!
			if (isDevelopmentMode())
				assertmsg(0, "Too large of a file!");
			return 1;
		} else {
            int fd = fileno(fw->fptr);
            fflush(fw->fptr);
            return ftruncate64(fd, newsize);
        }
#else
		if (newsize > 0x7ffffffLL) {
			// Too large of a value!
			if (isDevelopmentMode())
				assertmsg(0, "Too large of a file!");
			return 1;
		} else {
			return _chsize(_fileno(fw->fptr), (long)newsize);
		}
#endif
	xcase IO_WINIO:
#if _PS3
        {
            uint64_t pos;
            int e, fd = (int)fw->fptr;
            e = cellFsFtruncate(fd, newsize);
            if(!e) {
                e = cellFsLseek(fd, newsize, SEEK_SET, &pos);
                if(!e)
                    return 0;
            }
            return 1;
        }
#else
		x_fseek(fw, newsize, SEEK_SET);
		SetEndOfFile(fw->fptr);
		return 0;
#endif
	xcase IO_RAMCACHED:
	{
		RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
		assert(!rcf->assert_on_write);
		assert(!rcf->buffer); // Would need to write into buffer
		return fileTruncate(rcf->base_file, newsize);
	}
	}
	assertmsg(0, "Not supported");
	return 1;
}


/* Function x_fscanf()
 *	A very basic scanf to be used with FileWrapper.  This function always extracts
 *	one string from the file no matter what "format" says.  In addition, this function
 *	will only compile against VC++, since it relies on one of the VC stdio internal 
 *	functions, _input(), and the structure _iobuf.
 *
 *
 */
/*
extern int __cdecl _input(FILE *, const unsigned char *, va_list);
int x_fscanf(FileWrapper* fw, const char* format, ...){
va_list va;
#define bufferSize 1024
char buffer[bufferSize];
int retval;

	va_start(va, format);

	
	x_fgets(buffer, bufferSize, fw);

	// Adopted from VC++ 6's sscanf.
	{
		struct iobuf str;
		struct iobuf *infile = &str;
		
		_ASSERTE(buffer != NULL);
		_ASSERTE(format != NULL);
		
		infile->_flag = _IOREAD|_IOSTRG|_IOMYBUF;
		infile->_ptr = infile->_base = (char *) buffer;
		infile->_cnt = strlen(buffer);
		
		retval = (_input(infile,format, va));
	}
	
	va_end(va);
	return(retval);
}
*/


FileWrapper *fileGetStdout(void) {
	static FileWrapper wrapper;
	wrapper.iomode = IO_CRT;
	wrapper.fptr = stdout;
	wrapper.nameptr = "stdout";
	wrapper.nameptr_needs_freeing = 0;
	return &wrapper;
}

FileWrapper *fileGetStderr(void) {
	static FileWrapper wrapper;
	wrapper.iomode = IO_CRT;
	wrapper.fptr = stderr;
	wrapper.nameptr = "stderr";
	wrapper.nameptr_needs_freeing = 0;
	return &wrapper;
}

HANDLE fileDupHandle(FileWrapper *fw)
{
	if (!fw)
		return INVALID_HANDLE_VALUE;

	switch (fw->iomode) {
		xcase IO_PIG:
		{
			return pig_duphandle(fw->fptr);
		}
		xcase IO_WINIO:
		{
			//if (DuplicateHandle(GetCurrentProcess(), fw->fptr, GetCurrentProcess(), &ret, 0, FALSE, DUPLICATE_SAME_ACCESS))
			return CreateFile_UTF8(fw->nameptr, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		}
	}
	assert(0);
	return INVALID_HANDLE_VALUE;;
}

void *fileRealPointer(FileWrapper *fw)
{
	if (!fw)
		return 0;

	switch (fw->iomode) {
		xcase IO_CRT:
			return fw->fptr;
		xcase IO_GZ:
			return fw->fptr;
		xcase IO_PIG:
		case IO_STUFFBUFF:
		case IO_ESTRING:
			return 0;
	}
	assert(0);
	return 0;
}

FileWrapper *fileWrap(void *real_file_pointer)
{
	static FileWrapper fw;
	fw.iomode = IO_CRT;
	fw.fptr = real_file_pointer;
	return &fw;
}

int fileGetSize(FileWrapper* fw)
{
	S64 iSize = fileGetSize64(fw);
	if (iSize > INT_MAX)
	{
		ATOMIC_INIT_BEGIN;
		ErrorFilenameDeferredf(fw->nameptr, "File size too large to be returned by fileGetSize: %" FORM_LL "d", iSize);
		ATOMIC_INIT_END;
	}
	return (int)iSize; // Do this unsafe thing, to preserve behavior of already broken code
}

S64 fileGetSize64(FileWrapper* fw)
{
	switch (fw->iomode) {
	xcase IO_PIG:
		return pig_filelength(fw->fptr);
	xcase IO_WINIO:
#if _PS3
        {
            CellFsStat s;
            int e, fd = (int)fw->fptr;
            e = cellFsFstat(fd, &s);
            if(!e)
                return s.st_size;
            return 0;
        }
#else
		{
			DWORD dwFileSizeHigh;
			DWORD dwFileSizeLow = GetFileSize(fw->fptr, &dwFileSizeHigh);
			return ((S64)dwFileSizeHigh << (sizeof(DWORD)*8)) | dwFileSizeLow;
		}
#endif
	xcase IO_CRT:
    case IO_GZ:
#if _PS3
    {
        struct stat s;
        int fd = fileno(fw->fptr);
        fflush(fw->fptr);
        if(fstat(fd, &s))
            return 0;
        return s.st_size;
    }
#else
		return _filelength(_fileno(fileRealPointer(fw)));
#endif
	xcase IO_RAMCACHED:
	{
		RAMCachedFile *rcf = (RAMCachedFile*)fw->fptr;
		if (rcf->buffer)
		{
			return rcf->bufferlen;
		} else {
			return fileGetSize(rcf->base_file);
		}
	}	
	}
	assert(0);
	return 0;
}

int fileSetTimestamp(const char* fullFilename, U32 timestamp){
#if _PS3
    assert(0);
#else
#if 0
	struct _utimbuf	utb;
	utb.actime = utb.modtime = timestamp;
	_utime(fullFilename, &utb);
	t = fileLastChanged(fullFilename);
	dt = t - timestamp;
	printf("odt: %d   %s\n",dt,fullFilename);
#else
	int ret;
	FWStatType s;
	fwStat(fullFilename, &s);
	fwChmod(fullFilename, _S_IREAD | _S_IWRITE);
	ret = _SetUTCFileTimesCMA(fullFilename,0, timestamp,timestamp);
	fwChmod(fullFilename, s.st_mode & (_S_IREAD | _S_IWRITE));
	return ret;
#endif
#endif
}

void fileSetTimestampSS2000(const char* fullFilename, U32 iTime)
{
	char outFileName[CRYPTIC_MAX_PATH];

	fileLocateWrite(fullFilename, outFileName);
	backSlashes(outFileName);
	fileSetTimestamp(outFileName, timeSecondsSince2000ToPatchFileTime(iTime));
}

void fileSetModificationTimesSS2000(const char* file, U32 iCreationTime, U32 iModifiedTime, U32 iAccessedTime)
{
#if _PS3
    assert(0);
#else
	char outFileName[CRYPTIC_MAX_PATH];
	FWStatType s;

	if (iCreationTime)
	{
		iCreationTime = timeSecondsSince2000ToPatchFileTime(iCreationTime);
	}

	if (iModifiedTime)
	{
		iModifiedTime = timeSecondsSince2000ToPatchFileTime(iModifiedTime);
	}

	if (iAccessedTime)
	{
		iAccessedTime = timeSecondsSince2000ToPatchFileTime(iAccessedTime);
	}

	fileLocateWrite(file, outFileName);
	backSlashes(outFileName);

	fwStat(outFileName, &s);
	fwChmod(outFileName, _S_IREAD | _S_IWRITE);
	_SetUTCFileTimesCMA(outFileName,iCreationTime, iModifiedTime, iAccessedTime);
	fwChmod(outFileName, s.st_mode & (_S_IREAD | _S_IWRITE));

#endif
}

int fileMakeLocalBackup(const char *_fname, int time_to_keep) {
#if _PS3
    return 0;
#else
	// Make backup
	char backupPathBase[CRYPTIC_MAX_PATH];
	char backupPath[CRYPTIC_MAX_PATH];
	char temp[CRYPTIC_MAX_PATH];
	char fname[CRYPTIC_MAX_PATH];
	char dir[CRYPTIC_MAX_PATH];
	int backup_num=0;
	struct _finddata32i64_t fileinfo;
	int i;
	intptr_t handle;

	if (!_fname)
		return 0;
	strcpy(fname, _fname);
	if (fileIsAbsolutePath(fname)) {
		fileRelativePath(fname, temp);
		if (fileIsAbsolutePath(temp)) {
			// Couldn't find relative path
			sprintf(backupPathBase, "%s.", fname);
		} else {
			sprintf(backupPathBase, "%c:/BACKUPS/%s.", fname[0], temp);
		}
	} else { // relative
		sprintf(backupPathBase, "%c:/BACKUPS/%s.", fileDataDir()[0], fname);
	}
	forwardSlashes(backupPathBase);
	strcpy(dir, backupPathBase);
	*(strrchr(dir, '/')+1)=0; // truncate before the file name
	mkdirtree(dir);
	sprintf(backupPath, "%s*", backupPathBase);
	handle = findfirst32i64_SAFE(backupPath, &fileinfo);
	backup_num=0;
	if (handle==-1) {
		// Error or no file currently exists
		// will default to backup_num of 0, that's OK
	} else {
		// 1 or more files already exist
		do {
			char *s;
			if (stricmp(fileinfo.name, "con")==0 || stricmp(fileinfo.name, "nul")==0)
			{
				Errorf("Attempted to open a file named \"con\" or \"nul\", this is not allowed by the OS.");
				memlog_printf(NULL, "Attempted to open a file named \"con\", this is not allowed by the OS, denying request.");
				return 0;
			}
			s = strrchr(fileinfo.name, '.')+1;
			// Check to see if it's a backup
			if (!isdigit((unsigned char)*s))
				continue;
			// check to see if the backup number on this file is newer than backup_num
			i = atoi(s);
			if (i>backup_num)
				backup_num = i;
			// check to see if file is old
			if (time_to_keep!=-1 && (fileinfo.time_write < time(NULL) - time_to_keep)) {
				// old file!  Delete it!
				sprintf(temp, "%s%s", dir, fileinfo.name);
				remove(temp);
			}
		} while( findnext32i64_SAFE( handle, &fileinfo ) == 0 );
		backup_num++;
		_findclose(handle);

	}
	sprintf(backupPath, "%s%d", backupPathBase, backup_num);
	return fileCopy(fileLocateWrite(fname, temp), backupPath);
#endif
}

bool fileExponentialBackupRec( char *fn, char *fnStem, int version, __time32_t interval, U32 doublingPeriod );

bool fileExponentialBackup( char *fn, U32 intervalSeconds, U32 doublingPeriod)
{ 
	int version = 1;

	// win32 needs it in 100-nanosecond periods 
	// = 100 * 10^-9 = 10^-7 = 1/10000000
	//__time32_t interval = intervalSeconds * 10000000L;

	bool res = false;

	if (fileExists(fn))
	{
		fileWaitForExclusiveAccess(fn);
		res = fileExponentialBackupRec( fn, fn, version, intervalSeconds, doublingPeriod );
	}
	
	// --------------------
	// finally

	return res;
}

bool fileExponentialBackupRec( char *fn, char *fnStem, int version, __time32_t interval, U32 doublingPeriod )
{
#if _PS3
    return 0;
#else
	bool res = true;
	
	__time32_t ft = 0;
	if( verify( _GetUTCFileTimes( fn, NULL, &ft, NULL ) ) )
	{
		char fnBak[CRYPTIC_MAX_PATH];
		
		// get the time after which no backups should be made
		__time32_t tPrev = ft - interval;		

		// --------------------
		// delete existing candidates that fall in the interval, find
		// a goal backup.

		{
			__time32_t tBak = 0;
			
			sprintf(fnBak, "%s~%.3d~", fnStem, version );

			// if the file exists, see if it is a valid backup, or should be deleted
			if( fileExists(fnBak) && verify(_GetUTCFileTimes(fnBak, NULL, &tBak, NULL )))
			{
				if( tBak > tPrev && verify( tBak <= ft ) )
				{
					// --------------------
					// file is too new, delete it

					int i;

					// delete the file
					DeleteFile_UTF8( fnBak );

					// move down the files with higher version numbers
					// (no gaps)
					for( i = version + 1;; ++i ) 
					{
						char fnTmp[CRYPTIC_MAX_PATH];
						sprintf(fnTmp, "%s~%.3d~", fnStem, i);

						if( fileExists( fnTmp ))
						{
							if(!verify(MoveFileEx_UTF8( fnTmp, fnBak, MOVEFILE_REPLACE_EXISTING )))
							{
								// something's wrong
								res = false;
								break;
							}
							strcpy( fnBak, fnTmp );
						}
						else
						{
							// all done
							break;
						}
					}
				}
				else
				{
					// the file is valid, 

					// check if interval should be adjusted
					// this number is arbitrary, but shouldn't result in too many backups
					if( version%doublingPeriod == 0)
					{
						interval *= 2;
					}

					//recursive backup call for the one overwritten at this level
					// and this one can be safely overwritten
					res = fileExponentialBackupRec( fnBak, fnStem, version + 1, interval, doublingPeriod );
				}
			}
			else
			{
				// no file found
			}
		}


		// --------------------
		// finally

		res = CopyFile_UTF8( fn, fnBak, FALSE );
	}

	// --------------------
	// finally

	return res;
#endif
}

S64 fileSize64(const char *fname)  // Slow!  Does not use FileWatcher.
{
	struct _stat64 status;
	char buf[CRYPTIC_MAX_PATH];

	if (fileIsFileServerPath(fname))
	{
		// slow hack
		FileWrapper *f = x_fopen(fname, "rb", __FILE__, __LINE__);
		S64 ret;
		if (!f)
			return -1;
		x_fseek(f, 0, SEEK_END);
		ret = x_ftell(f);
		x_fclose(f);
		return ret;
	}

	if (fileIsAbsolutePath(fname))
		strcpy(buf, fname);
	else
		fileLocateWrite(fname, buf);

#if PLATFORM_CONSOLE
	fileFixup(buf, SAFESTR(buf));
#endif
#if _XBOX
	backSlashes(buf);
#endif

	if(!_stat64(buf, &status)){
		if(!(status.st_mode & _S_IFREG)) {
			return -1;
		}
	} else {
		return -1;
	}
	return status.st_size;
}

intptr_t fileSizeEx(const char *fname, bool do_hog_reload)
{
	FWStatType status;


	if (fileIsFileServerPath(fname))
	{
		// slow hack
		FileWrapper *f = x_fopen(fname, "rb", __FILE__, __LINE__);
		U64 ret;
		if (!f)
			return -1;
		x_fseek(f, 0, SEEK_END);
		ret = x_ftell(f);
		x_fclose(f);
		if (ret > INT_MAX)
			return 0; // Hopefully caller (hogFileReadInternal) deals with it
		return ret;
	}

	if (fname[0] == '#' && is_pigged_path(fname))
	{
		// Dynamic hog
		char hogname[MAX_PATH];
		char *s;
		PigFileDescriptor pfd;

		// Get hog name and file name
		strcpy(hogname, fname+1);
		s = strchr(hogname, '#');
		*s = '\0';
		pfd = PigSetGetFileInfoFake(hogname, s+1);
		if (!pfd.parent_hog)
		{
			Errorf("fileSize() called on a file in a dynamic hogg which does not exist : %s", fname);
			return -1;
		} else {
			U32 ret = hogFileGetFileSize(pfd.parent_hog, pfd.file_index);
			hogFileDestroy(pfd.parent_hog, true);
			return ret;
		}
	} else {
		assert(!is_pigged_path(fname)); // expects /bin/tricks.bin, instead of ./piggs/bin.pigg:/bin/tricks.bin, for example
	}

	if (!fileIsAbsolutePath(fname)) { // Relative path
		FolderNode *ret;
		intptr_t size=-1;

		if (!loadedGameDataDirs) 
			fileLoadDataDirs(0);

		ret = FolderCacheQueryEx(folder_cache, fname, do_hog_reload, true);
		if (ret)
			size = ret->size;
		FolderNodeLeaveCriticalSection();
		return size;
	}

	{
#if PLATFORM_CONSOLE
		char fullname[CRYPTIC_MAX_PATH];
		if (fileFixup(fname, SAFESTR(fullname)))
			fname = fullname;
#endif

		// Absolute path:
		if( !fwStat(fname, &status) &&
			status.st_mode & _S_IFREG)
		{
			return status.st_size;
		}
		
		// The file doesn't exist.
		return -1;
	}
}

int fileExistsEx(const char *fname, bool do_hog_reload)
{
	FWStatType status;

	if(!fname)
		return 0;

	if (fileIsFileServerPath(fname))
	{
		// slow hack
		FileWrapper *f = x_fopen(fname, "rb", __FILE__, __LINE__);
		if (!f)
			return 0;
		x_fclose(f);
		return 1;
	}

	if (!fileIsAbsolutePath(fname))
	{
		int			exists=0;
		FolderNode *ret;

		if (!loadedGameDataDirs)
			fileLoadDataDirs(0); // Don't do this outside of the if statement to allow fileAutoDataDir to work

		ret = FolderCacheQueryEx(folder_cache, fname, do_hog_reload, true);

		if (ret)
		{
			if (!ret->is_dir)
				exists = 1;
			//FolderNodeDestroy(ret);
		}
		FolderNodeLeaveCriticalSection();
		return exists;
	}

	{
#if PLATFORM_CONSOLE
		char fullname[CRYPTIC_MAX_PATH];
		if (fileFixup(fname, SAFESTR(fullname)))
			fname = fullname;
#endif

		if(!fwStat(fname, &status)){
			if(status.st_mode & _S_IFREG)
				return 1;
		}
		
		return 0;
	}
}

bool fileNeedsPatching(const char *fname)
{
	bool bRet=false;
	FolderNode *node;
	
	node = FolderCacheQuery(folder_cache, fname);
	if (node)
	{
		bRet = node->needs_patching;
	}
	else
	{
		// I need to not issue these details if the assert is not going to fire.
		ErrorDetailsf("fileNeedsPatching: Couldn't find node for \"%s\"",fname);
		devassert(node);
	}
	FolderNodeLeaveCriticalSection();
	return bRet;
}

bool wfileExists(const wchar_t *fname)
{
	bool res = fileExists( WideToUTF8StrTempConvert( fname, NULL ));

#if !_PS3
	// try the wstat
	if( !res )
	{	
		struct _stat64 status = {0};

		if(!_wstat64(fname, &status)){
			if(status.st_mode & _S_IFREG)
				res = true;
		}
	}
#endif

	// ----------
	// finally

	return res;
}

int dirExists(const char *dirname){
	FWStatType status;
	char	*s,*s2,buf[1000];

	//if (!loadedGameDataDirs) 
	//	fileLoadDataDirs(0);

	strcpy(buf,dirname);
	if(strlen(buf) > 2)
	{
		s = &buf[strlen(buf)-1];
		s2 = &buf[strlen(buf)-2];
		if ((*s == '/' || *s=='\\') && *s2 != ':')
			*s = 0;
	}

	if (loadedGameDataDirs && !fileIsAbsolutePath(buf)) {
		FolderNode *ret;
		int finalret;
		ret = FolderCacheQuery(folder_cache, buf);
		finalret = ret && ret->is_dir;
		FolderNodeLeaveCriticalSection();
		return finalret;
	}

#if _XBOX
	backSlashes(buf);
#endif

	if(!fwStat(buf, &status)){
		if(status.st_mode & _S_IFDIR)
			return 1;
	}
	
	return 0;
}

int dirExistsMultiRoot(const char* dir, const char** roots) {
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
                return true;
            }
        }

        return false;
    }
}

int fileIsReadOnly(const char *fname){
	FWStatType status;

	if(!fname)
		return 0;

	if (!fileIsAbsolutePath(fname))
	{
		int			writeable=1;
		FolderNode *ret;

		if (!loadedGameDataDirs)
			fileLoadDataDirs(0); // Don't do this outside of the if statement to allow fileAutoDataDir to work

		ret = FolderCacheQuery(folder_cache, fname);
		if (ret)
		{
			if (!ret->writeable)
				writeable = 0;
			//FolderNodeDestroy(ret);
		}
		FolderNodeLeaveCriticalSection();
		return !writeable;
	}

	{
#if PLATFORM_CONSOLE
		char fullname[CRYPTIC_MAX_PATH];
		if (fileFixup(fname, SAFESTR(fullname)))
			fname = fullname;
#endif

		if(!fwStat(fname, &status)){
			if(!(status.st_mode & _S_IWRITE))
				return 1;
		}

		return 0;
	}
}

bool fileIsReadOnlyDoNotTrustFolderCache(const char *relpath)
{
    char fullpath[MAX_PATH];
    bool bFC = fileIsReadOnly(relpath);
	static int count=0;
    fileLocateWrite(relpath, fullpath);
	if (fileExists(fullpath)) // May exist in the folder cache but not on disk if it is not mirrored
	{
		bool bDisk = fileIsReadOnly(fullpath);
		if (bFC != bDisk) {
			// May have been changed while calling fileLocateWrite/fileExists/fileIsReadOnly on a full path, which is slow
			bool bFC2 = fileIsReadOnly(relpath);
			if (bFC2 != bDisk)
			{
				count++;
				if (count >= 10)
					Errorf("%s: FolderCache repeatedly reported wrong writable state, contact Jimb to debug (disk:%d cache:%d)", fullpath, bDisk, bFC2);
			}
		}
		return bDisk;
	}
	return bFC;
}

/* is refname older than testname?
*/
int fileNewer(const char *refname,const char *testname)
{
	__time32_t ref, test;
	__time32_t t;

	ref = fileLastChanged(refname);
	test = fileLastChanged(testname);

   	t = test - ref;
	return t > 0;
}

/* is refname older than testname?
*/
int fileNewerAbsolute(const char *refname,const char *testname)
{
	__time32_t ref, test;
	__time32_t t;

	ref = fileLastChangedAbsolute(refname);
	test = fileLastChangedAbsolute(testname);

	t = test - ref;
	return t > 0;
}

int fileNewerOrEqual(const char *refname,const char *testname)
{
	__time32_t ref, test;
	__time32_t t;

	ref = fileLastChanged(refname);
	test = fileLastChanged(testname);

   	t = test - ref;
	return t >= 0;
}

__time32_t fileLastChangedAltStat(const char *refname)
{
#if _PS3
    struct _stat32 st;
    if(_stat32(refname, &st))
        return 0;
    return st.st_mtime;
#else
	__time32_t	atime,mtime,ctime;

	if (!_GetUTCFileTimes(refname,&atime,&mtime,&ctime))
		return 0;
	return mtime;
#endif
}

__time32_t fileLastChangedAbsolute(const char *refname)
{
	FWStatType sbuf;
	__time32_t ret;

	assert(fileIsAbsolutePath(refname));

	PERFINFO_AUTO_START("fwStat", 1);
	if (fwStat(refname, &sbuf)) {
		PERFINFO_AUTO_STOP();
		return 0;
	}
	ret = sbuf.st_mtime;
	PERFINFO_AUTO_STOP();
	return ret;
}

AUTO_COMMAND;
U32 fileLastChangedSS2000(const char *refname)
{
	return timeGetSecondsSince2000FromWindowsTime32(fileLastChanged(refname));
}

U32 fileLastChangedSS2000AltStat(const char *refname)
{
	char nameToUse[CRYPTIC_MAX_PATH];
	fileLocateWrite(refname, nameToUse);
	return timeGetSecondsSince2000FromWindowsTime32(fileLastChangedAltStat(nameToUse));
}

FileTimestampFunction *lastChangedNonExistentFuncs;

void fileRegisterLastChangedNonExistentFunction(FileTimestampFunction func)
{
	eaPushUnique((cEArrayHandle*)&lastChangedNonExistentFuncs, func);
}

__time32_t fileLastChangedEx(const char *refname, bool do_hog_reload)
{
	FolderNode *node;
	char temp[CRYPTIC_MAX_PATH];
	__time32_t timestamp=-1;

	if (!refname)
		return 0;

	PERFINFO_AUTO_START("fileLastChanged", 1);

	if (!loadedGameDataDirs) 
		fileLoadDataDirs(0);

	strcpy(temp, refname);
	forwardSlashes(temp);
	refname = temp;

	if (is_pigged_path(refname)) {
		printf("Pigged file passed to fileLastChanged, need to implement support for this\n");
		PERFINFO_AUTO_STOP();
		return 0;
	}

	refname = fileRelativePath(refname, temp);

	if (fileIsAbsolutePath(refname)) {
		FWStatType sbuf;
		__time32_t ret;
		PERFINFO_AUTO_START("fwStat", 1);
		if (fwStat(refname, &sbuf)) {
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return 0;
		}
		ret = sbuf.st_mtime;
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return ret;
	}
	node = FolderCacheQueryEx(folder_cache, refname, do_hog_reload, true);

	if (node)
		timestamp = node->timestamp;
	FolderNodeLeaveCriticalSection();

	PERFINFO_AUTO_STOP();

	if (!node) {
		int i;
		for (i=0; i<eaSizeUnsafe(&lastChangedNonExistentFuncs); i++) 
		{
			if (timestamp = lastChangedNonExistentFuncs[i](refname))
			{
				return timestamp;
			}
		}
		return 0;
	}
	return timestamp;
}

FileTimestampFunction *lastSavedFuncs;

__time32_t fileLastSaved(const char *refname)
{
	__time32_t timestamp;
	int i;
	for (i = 0; i < eaSizeUnsafe(&lastSavedFuncs); i++)
	{
		if (timestamp = lastSavedFuncs[i](refname))
		{
			return timestamp;
		}
	}
	return 0;
}

void fileRegisterLastSavedFunction(FileTimestampFunction func)
{
	eaPushUnique((cEArrayHandle *)&lastSavedFuncs,func);
}


void *fileLockRealPointer(FileWrapper *fw) { // This implements support for Pig files, and must be followed by an unlock when done
	if (fw->iomode==IO_PIG)
		return pig_lockRealPointer(fw->fptr);
	return fileRealPointer(fw);
}

void fileUnlockRealPointer(FileWrapper *fw) {
	if (fw->iomode==IO_PIG)
		pig_unlockRealPointer(fw->fptr);
}

static int fileIsUsingDevDataInternal(void) {
	struct _stat64 statbuf;
#if _PS3
	return (-1==stat("/app_home/piggs/data.hogg", &statbuf));
#elif _XBOX
	return (-1==stat("game:\\piggs\\data.hogg", &statbuf));
#else
	return (-1==_stat64("./piggs/data.hogg", &statbuf));
#endif
}

int fileIsUsingDevData(void) { // Returns 1 if we're in development mode, 0 if in production
	static int cached=-1;
	if (cached==-1)
		cached = fileIsUsingDevDataInternal();
	return cached && !g_xbox_production_hack;
}

// Production Edit mode is a special mode that allows limited editing in development mode

bool g_production_edit_mode = 0;
AUTO_COMMAND ACMD_NAME(ProductionEdit) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setProductionEditMode(bool enable)
{
	if (gConf.bUserContent)
	{
		g_production_edit_mode = enable;
	}
	else
	{
		g_production_edit_mode = 0;
	}
	
}

int isProductionEditMode(void)
{
	return g_production_edit_mode;
}

int isProductionEditAvailable(void)
{
	return gConf.bUserContent;
}

// Note: we do not want to cache these because they may be called before
// and after command line parameters are parsed which will change the 
// value of g_force_production_mode.

// Forcing production mode status, usually with -productionMode.
//  0: Force production mode off
//  1: Force production mode on
// -1: Use default production mode value
static int g_force_production_mode = -1;
AUTO_CMD_INT(g_force_production_mode, productionMode) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(forceReCheckIsProductionMode);

// The default production mode status, if not forced to be on or off.  Set by setDefaultProductionMode().
//  0: Default production mode off
//  1: Default production mode on
// -1: Check hoggs to determine if this is production mode
static int g_production_mode_default = -1;

// If set, don't show dev ui (budgets window, etc)
bool g_hide_dev_ui = 0;
AUTO_CMD_INT(g_hide_dev_ui, hideDevUI) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0) ACMD_HIDE;

// If set, disable editor setup and access
bool g_disable_editors = 0;
AUTO_CMD_INT(g_disable_editors, disableEditors) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void disableEditors(bool disable)
{
	g_disable_editors = disable;
}

bool editorsManuallyDisabled(void)
{
	return g_disable_editors;
}

bool showDevUI(void)
{
	return isDevelopmentMode() && !g_hide_dev_ui;
}

bool showDevUIProd(void)
{
	return !g_hide_dev_ui;
}

int gbCachedIsProductionMode = -1;

void forceReCheckIsProductionMode(void)
{
	gbCachedIsProductionMode = -1;
}

int isDevelopmentModeInternal(void)
{
	gbCachedIsProductionMode = isProductionModeInternal();
	return !gbCachedIsProductionMode;
}

int isProductionModeInternal(void)
{
	// If production mode has been forced, use that.
	if (g_force_production_mode != -1)
	{
		assert(g_force_production_mode == 0 || g_force_production_mode == 1);
		gbCachedIsProductionMode = g_force_production_mode;
	}

	// Otherwise, if a production mode default has been set, use that.
	else if (g_production_mode_default != -1)
	{
		assert(g_production_mode_default == 0 || g_production_mode_default == 1);
		gbCachedIsProductionMode = g_production_mode_default;
	}

	// Otherwise, check hoggs.
	else
		gbCachedIsProductionMode = !fileIsUsingDevData();

	return gbCachedIsProductionMode;
}

// Set the default production mode status, used when there are no hoggs and it has not been forced.
void setDefaultProductionMode(bool enable)
{
	assert(enable == 0 || enable == 1);
	forceReCheckIsProductionMode();
	g_production_mode_default = enable;
}

int hasServerDir_NotASecurityCheck(void)
{
	static int cached=-1;
	if (cached==-1)
	{
		assert(loadedGameDataDirs);
		cached = dirExists("server");
	}
	return isDevelopmentMode() || cached;
}


int filePathCompare(const char* path1, const char* path2)
{
	char p1[CRYPTIC_MAX_PATH];
	char p2[CRYPTIC_MAX_PATH];

	if(!path1)
		return -1;
	if(!path2)
		return 1;

	fileFixUpName(path1, p1);
	fileFixUpName(path2, p2);
	return strcmp(p1, p2);
}

int filePathBeginsWith(const char* fullPath, const char* prefix)
{
	char p1[CRYPTIC_MAX_PATH];
	char p2[CRYPTIC_MAX_PATH];

	fileFixUpName(fullPath, p1);
	fileFixUpName(prefix, p2);
	return strncmp(p1, p2, strlen(p2));
}

U64 fileGetFreeDiskSpace(const char* fullPath)
{
#if _PS3
    assert(0);
    return 0;
#else
	ULARGE_INTEGER freeBytesAvailableToUser;
	ULARGE_INTEGER totalBytesOnDisk;
	char path[CRYPTIC_MAX_PATH];

	strcpy(path, fullPath);
	getDirectoryName(path);

	if(!GetDiskFreeSpaceEx_UTF8(path, &freeBytesAvailableToUser, &totalBytesOnDisk, NULL))
		return 0;
	
	return freeBytesAvailableToUser.QuadPart;
#endif
}

void fileFreeOldZippedBuffers(void)
{
	pig_freeOldBuffers();
}

void fileFreeZippedBuffer(FileWrapper *fw)
{
	if (fw->iomode == IO_PIG) {
		pig_freeBuffer(fw->fptr);
	} else if (fw->iomode == IO_RAMCACHED) {
		fileRAMCachedFreeCache(fw);
	}
}

#if _XBOX
void debugXboxRestart(const char * command)
{
	char buf[1024], *found, *dir = NULL, *executable = NULL, *cmdline = NULL;

	if(command)
	{
		strcpy(buf, command);
		backSlashes(buf);
		dir = buf;

		while(IS_WHITESPACE(*dir))
			dir++;

		if(*dir == '\"')
		{
			dir++;
			found = strchr(dir, '\"');
			if(found && (!found[1] || IS_WHITESPACE(found[1])))
			{
				*found = '\0';
				cmdline = found+1;
			}
			else
				dir = NULL; // give up on parsing
		}
		else
		{
			found = strchr(dir, ' ');
			if(found)
			{
				*found = '\0';
				cmdline = found+1;
			}
			else
				cmdline = "";
		}

		if(dir)
		{
			found = strrchr(dir, '\\');
			if(found)
			{
				*found = '\0';
				executable = found + 1;
			}
			else
				executable = dir;

			while(IS_WHITESPACE(*cmdline))
				cmdline++;
		}
	}

	// Doc says set title should come after DmReboot, but it doesn't seem to work
	if(executable)
		DmSetTitle(dir, executable, cmdline);
	DmReboot(DMBOOT_WAIT | DMBOOT_TITLE);
	if(executable)
		DmSetTitle(dir, executable, cmdline);
}
#endif

#if !PLATFORM_CONSOLE

void *fileAlloc_NetworkDriveSafe(const char *fname, int *lenp, int iFailureTime)
{
	char systemString[MAX_PATH * 3];
	char *pTempFileName = NULL;
	char *pRetBuf;

	estrStackCreate(&pTempFileName);

	if (!GetTempFileName_UTF8("c:\\", "SPR", 0, &pTempFileName))
	{
		estrDestroy(&pTempFileName);
		return NULL;
	}

	sprintf(systemString, "copy %s %s", fname, pTempFileName);


	if (system_w_timeout(systemString, NULL, iFailureTime) != 0)
	{
		sprintf(systemString, "erase %s", pTempFileName);
		system(systemString);

		estrDestroy(&pTempFileName);
		return NULL;
	}

	pRetBuf = fileAlloc(pTempFileName, lenp);

	sprintf(systemString, "erase %s", pTempFileName);
	system(systemString);
	
	estrDestroy(&pTempFileName);
	
	return pRetBuf;
}
#endif

bool g_disk_access_allowed_stack[1];
int g_disk_access_allowed_stack_depth=0;
bool g_disk_access_allowed=true;
// Sets a flag saying whether or not disk access should be allowed from the
// primary thread (for performance debugging)
AUTO_CMD_INT(g_disk_access_allowed, diskAccessAllowed) ACMD_CATEGORY(Performance);

bool fileDiskAccessAllowedInMainThread(bool bAllowed)
{
	bool ret = g_disk_access_allowed;
	g_disk_access_allowed = bAllowed;
	return ret;
}

void filePushDiskAccessAllowedInMainThread(bool bDiskAccessAllowed)
{
	assert(g_disk_access_allowed_stack_depth < ARRAY_SIZE(g_disk_access_allowed_stack) && g_disk_access_allowed_stack_depth>= 0);
	g_disk_access_allowed_stack[g_disk_access_allowed_stack_depth++] = g_disk_access_allowed;
	g_disk_access_allowed = bDiskAccessAllowed;
}

void filePopDiskAccessAllowedInMainThread(void)
{
	assert(g_disk_access_allowed_stack_depth > 0 && g_disk_access_allowed_stack_depth <= ARRAY_SIZE(g_disk_access_allowed_stack));
	g_disk_access_allowed_stack_depth--;
	g_disk_access_allowed = g_disk_access_allowed_stack[g_disk_access_allowed_stack_depth];
}


void fileDiskAccessCheck(void)
{
	static DWORD mainThreadID;
	if (!mainThreadID) {
		mainThreadID = GetCurrentThreadId();
	}
	if (GetCurrentThreadId() == mainThreadID) {
		if (!g_disk_access_allowed) {
			//_DbgBreak(); // Or change this to a printf() for easier breakpoint setting
		}
	}
}

AUTO_COMMAND;
void PrintDataDirs(void)
{
	if (folder_cache)
	{
		int i;
		printf("Folders:\n");
		for (i = 0; i<eaSize(&folder_cache->gamedatadirs); i++)
		{
			if (!folder_cache->gamedatadirs[i])
			{
				continue;
			}
			printf("%s %s\n", folder_cache->gamedatadirs[i], folder_cache->gamedatadir_names[i]?folder_cache->gamedatadir_names[i]:"NONE");
		}
		printf("Piggs:\n");
		for (i = 0; i<eaSize(&folder_cache->piggs); i++)
		{
			if (!folder_cache->piggs[i])
			{
				continue;
			}
			printf("%s %s\n", folder_cache->piggs[i], folder_cache->pigg_names[i]?folder_cache->pigg_names[i]:"NONE");
		}
	}
}
	

void fileGetStats(FileStats *file_stats)
{
	*file_stats = g_file_stats;
}

void fileGetStatsAndClear(FileStats *file_stats)
{
	*file_stats = g_file_stats;
	ZeroStruct(&g_file_stats);
}


// _findfirst and _findnext wrappers below.  The point of the functions is to adjust for daylight savings which neither do appropriately on their own.

#undef _findfirst32i64
#undef _findnext32i64
#undef _findfirst32
#undef _findnext32
#undef _findfirst
#undef _findnext

#ifdef _USE_32BIT_TIME_T

#define _findfirst      _findfirst32
#define _findnext       _findnext32

#else  /* _USE_32BIT_TIME_T */

#define _findfirst      _findfirst64i32
#define _findnext       _findnext64i32

#endif  /* _USE_32BIT_TIME_T */

//Ben H's safe findNext stuff needs to stash the path and filename that the findNext was called with, 
//which made it non-threadsafe when they were just sitting in static variables. I'm putting them in TLS, 
//so it will be threadsafe but not reentrant
typedef struct PathAndFileNameForFindNext
{
	char findnextpath[MAX_PATH];
	char findnextfilename[MAX_PATH];
} PathAndFileNameForFindNext;

PathAndFileNameForFindNext *GetThreadLocalPathAndFileNameForFindNext(void)
{
	PathAndFileNameForFindNext **paths=0;
	STATIC_THREAD_ALLOC_TYPE(paths,PathAndFileNameForFindNext **);

	if (!*paths)
		*paths = calloc(sizeof(PathAndFileNameForFindNext), 1);

	return *paths;
}

// just use strrchr
static __forceinline void setFindNextPath(const char* filename, PathAndFileNameForFindNext *pPathAndName) {
	char* i;
	strcpy(pPathAndName->findnextpath,filename);
	i = strrchr(pPathAndName->findnextpath,'/');
	if (i) {
		i[1] = '\0';
	} else {
		pPathAndName->findnextpath[0] = '\0';
	}
}

// Macro adjusts the access, creation, and modify stat times of files according to daylight savings time.
// The use of findnextfilename must also be used within findfirst to eliminate any wildcards being used from the filename parameter
#define setAdjustedStatTime(pfd)								\
{																\
	STR_COMBINE_SS(pPathAndFileName->findnextfilename,pPathAndFileName->findnextpath,pfd->name);	\
	if (cryptic_stat32i64_utc(pPathAndFileName->findnextfilename, &tempStatInfo2) == 0) {		\
		pfd->time_access = tempStatInfo2.st_atime;				\
		pfd->time_create = tempStatInfo2.st_ctime;				\
		pfd->time_write = tempStatInfo2.st_mtime;				\
	}															\
}

intptr_t findfirst32i64_SAFE(const char* filename, struct _finddata32i64_t * pfd)
{
	intptr_t	retval = _wfindfirst32i64_UTF8(filename, pfd);
	struct _stat32i64 tempStatInfo2;
	PathAndFileNameForFindNext *pPathAndFileName = GetThreadLocalPathAndFileNameForFindNext();

	if (retval != -1) {
		setFindNextPath(filename, pPathAndFileName);
		setAdjustedStatTime(pfd);
	}
	return retval;
}

int findnext32i64_SAFE(intptr_t filehandle, struct _finddata32i64_t * pfd)
{
	int	retval = _wfindnext32i64_UTF8(filehandle, pfd);
	struct _stat32i64 tempStatInfo2;
	PathAndFileNameForFindNext *pPathAndFileName = GetThreadLocalPathAndFileNameForFindNext();

	if (retval != -1) {
		setAdjustedStatTime(pfd);
	}
	return retval;
}

intptr_t findfirst32_SAFE(const char* filename, struct _finddata32_t * pfd)
{
	intptr_t	retval = _wfindfirst32_UTF8(filename, pfd);
	struct _stat32i64 tempStatInfo2;
	PathAndFileNameForFindNext *pPathAndFileName = GetThreadLocalPathAndFileNameForFindNext();

	if (retval != -1) {
		setFindNextPath(filename, pPathAndFileName);
		setAdjustedStatTime(pfd);
	}
	return retval;
}

int findnext32_SAFE(intptr_t filehandle, struct _finddata32_t * pfd)
{
	int	retval = _wfindnext32_UTF8(filehandle, pfd);
	struct _stat32i64 tempStatInfo2;
	PathAndFileNameForFindNext *pPathAndFileName = GetThreadLocalPathAndFileNameForFindNext();

	if (retval != -1) {
		setAdjustedStatTime(pfd);
	}
	return retval;
}

intptr_t findfirst_SAFE(const char* filename, struct _finddata_t * pfd)
{
	intptr_t	retval = _wfindfirst_UTF8(filename, pfd);
	struct _stat32i64 tempStatInfo2;
	PathAndFileNameForFindNext *pPathAndFileName = GetThreadLocalPathAndFileNameForFindNext();

	if (retval != -1) {
		setFindNextPath(filename, pPathAndFileName);
		setAdjustedStatTime(pfd);
	}
	return retval;
}

int findnext_SAFE(intptr_t filehandle, struct _finddata_t * pfd)
{
	int	retval = _wfindnext_UTF8(filehandle, pfd);
	struct _stat32i64 tempStatInfo2;
	PathAndFileNameForFindNext *pPathAndFileName = GetThreadLocalPathAndFileNameForFindNext();

	if (retval != -1) {
		setAdjustedStatTime(pfd);
	}
	return retval;
}

