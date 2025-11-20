#include "AutoGen/gimmeDLLPublicInterface_h_ast.h"
#include "gimmeDLLWrapper.h"
#include "error.h"
#include "file.h"
#include "winutil.h"
#include <sys/stat.h>
#include "utils.h"
#include "RegistryReader.h"
#include "earray.h"
#include "MemoryBudget.h"
#include "utilitiesLib.h"
#include "timing.h"
#include "fileutil2.h"
#include "timing_profiler_interface.h"

// for ManifestInfo
#include "crypt.h"
#include "trivia.h"
#include "stashtable.h"
#include "StringCache.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

static CRITICAL_SECTION g_gimmedll_cs;

static void gimmeDllInitCS(void)
{	
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&g_gimmedll_cs);
	}
	ATOMIC_INIT_END;
}

#if _PS3
#else

static bool bGimmeDLLInited=false;
static bool bGimmeDLLEnabled=true;
static bool bGimmeDLLEnabledForce=false;
static bool bGimmeDLLDemoMode=false;

static HMODULE hGimmeDLL;

#define GIMME_EXT_DECL(fn) \
	tpfn##fn pfn##fn=NULL;\
	bool pfn##fn##_inited=false;

GIMME_EXT_DECL(GimmeDoOperation);
GIMME_EXT_DECL(GimmeDoOperations);
GIMME_EXT_DECL(GimmeSetDefaultCheckinComment);
GIMME_EXT_DECL(GimmeQueryIsFileLocked);
GIMME_EXT_DECL(GimmeQueryIsFileLockedByMeOrNew);
GIMME_EXT_DECL(GimmeQueryIsFileMine);
GIMME_EXT_DECL(GimmeQueryLastAuthor);
GIMME_EXT_DECL(GimmeQueryIsFileLatest);
GIMME_EXT_DECL(GimmeQueryUserName);
GIMME_EXT_DECL(GimmeQueryAvailable);
GIMME_EXT_DECL(GimmeQueryBranchName);
GIMME_EXT_DECL(GimmeQueryBranchNumber);
GIMME_EXT_DECL(GimmeQueryCoreBranchNumForDir);
GIMME_EXT_DECL(GimmeGetErrorString);
GIMME_EXT_DECL(GimmeDoCommand);
GIMME_EXT_DECL(GimmeBlockFile);
GIMME_EXT_DECL(GimmeUnblockFile);
GIMME_EXT_DECL(GimmeQueryIsFileBlocked);
GIMME_EXT_DECL(GimmeQueryGroupListForUser);
GIMME_EXT_DECL(GimmeQueryGroupList);
GIMME_EXT_DECL(GimmeQueryFullGroupList);
GIMME_EXT_DECL(GimmeQueryFullUserList);
GIMME_EXT_DECL(GimmeSetVprintfFunc);
GIMME_EXT_DECL(GimmeSetCrashStateFunc);
GIMME_EXT_DECL(GimmeSetMemoryAllocators);
GIMME_EXT_DECL(GimmeSetAutoTimer);
GIMME_EXT_DECL(GimmeForceManifest);
GIMME_EXT_DECL(GimmeCreateConsoleHidden);
GIMME_EXT_DECL(GimmeForceDirtyBit);

// FIXME: None of this initialization is thread-safe.
#define GIMME_DLL_LAZY_INIT_EX(fn, errorStmt, ...)	\
	if (!bGimmeDLLInited)			\
		gimmeDLLInit();				\
	if (!bGimmeDLLEnabled) {		\
		errorStmt;					\
		PERFINFO_AUTO_STOP_FUNC();	\
		return __VA_ARGS__;			\
	}								\
	if (!pfn##fn##_inited) {		\
		pfn##fn = (tpfn##fn)GetProcAddress(hGimmeDLL, #fn);			\
		pfn##fn##_inited = true;	\
		if (!pfn##fn)				\
			printf("GimmeDLL: GetProcAddress failed on " #fn ".");	\
	}								\
	if (!pfn##fn) {					\
		errorStmt;					\
		PERFINFO_AUTO_STOP_FUNC();	\
		return __VA_ARGS__;			\
	}

#define GIMME_DLL_LAZY_INIT(fn, ...)	\
	GIMME_DLL_LAZY_INIT_EX(fn, ;, __VA_ARGS__)
	
static HANDLE loadGimmeDLL(const char* fileName)
{
	HANDLE h = LoadLibrary_UTF8(fileName);
	
	if(h && !gbSurpressStartupMessages){
		printf("Loaded DLL: %s\n", fileName);
	}
	
	return h;
}

void gimmeDLLInit(void);

bool gimmeDLLSetVprintfFunc(VprintfFunc func)
{
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeSetVprintfFunc, false);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	pfnGimmeSetVprintfFunc(func);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

bool gimmeDLLSetCrashStateFunc(crashStateFunc func)
{
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeSetCrashStateFunc, false);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	pfnGimmeSetCrashStateFunc(func);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

static CRITICAL_SECTION gimmeDLLBudgetMapCS;
static bool gimmeDLLBudgetMapCS_inited;

bool gimmeDLLSetMemoryAllocators(CRTMallocFunc m, CRTCallocFunc c, CRTReallocFunc r, CRTFreeFunc f)
{
	PERFINFO_AUTO_START_FUNC();
	if (!gimmeDLLBudgetMapCS_inited)
	{
		InitializeCriticalSection(&gimmeDLLBudgetMapCS);
		gimmeDLLBudgetMapCS_inited = true;
	}
	GIMME_DLL_LAZY_INIT(GimmeSetMemoryAllocators, false);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	pfnGimmeSetMemoryAllocators(m, c, r, f);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

static const char *gimmeDLLBudgetMap(const char *filename)
{
	static StashTable gimme_budgets_mapped;
	char buf[MAX_PATH];
	StashElement elem;
	strcpy(buf, "GimmeDLL_");
	getFileNameNoExt_s(buf + 9, ARRAY_SIZE(buf)-9, filename);
	EnterCriticalSection(&gimmeDLLBudgetMapCS);
	if (!gimme_budgets_mapped)
		gimme_budgets_mapped = stashTableCreateWithStringKeys(64, StashDefault);
	if (!stashFindElement(gimme_budgets_mapped, buf, &elem))
	{
		filename = allocAddString_dbg(buf, false, false, false, __FILE__, __LINE__);
		memBudgetAddMapping(filename, BUDGET_EngineMisc);
		if(stricmp(filename, "cannotLockStringCache"))
		{
			verify(stashAddInt(gimme_budgets_mapped, filename, 1, false));
		}
	} else {
		filename = stashElementGetStringKey(elem);
	}
	LeaveCriticalSection(&gimmeDLLBudgetMapCS);
	return filename;
}

static void* gimme_malloc_timed(size_t size, int blockType, const char *filename, int linenumber)
{
	filename = gimmeDLLBudgetMap(filename);
	return malloc_timed(size, blockType, filename, linenumber);
}
static void* gimme_calloc_timed(size_t num, size_t size, int blockType, const char *filename, int linenumber)
{
	filename = gimmeDLLBudgetMap(filename);
	return calloc_timed(num, size, blockType, filename, linenumber);
}
static void* gimme_realloc_timed(void *userData, size_t newSize, int blockType, const char *filename, int linenumber)
{
	filename = gimmeDLLBudgetMap(filename);
	return realloc_timed(userData, newSize, blockType, filename, linenumber);
}
static void gimme_free_timed(void *userData, int blockType)
{
	free_timed(userData, blockType);
}

bool gimmeDLLSetAutoTimer(const AutoTimerData *data)
{
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeSetAutoTimer, false);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	pfnGimmeSetAutoTimer(data);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

void gimmeDLLInit(void)
{
	char default_dll_path[2048];
	if (bGimmeDLLInited)
		return;

	if (!bGimmeDLLEnabledForce && !fileIsUsingDevData()) { // Can't be isProducitonMode, because then running with -productionMode on dev data will not find the right Core folder
		gimmeDLLDisable(true);
		return;
	}

	bGimmeDLLInited = true;

	// Try and load GimmeDLL to whichever is currently registered
	{
		RegReader reader;
		bool bFoundIt=false;
		char key[1024];
		struct _stat64 sbuf;

		// New way
		reader = createRegReader();
		sprintf(key, "HKEY_LOCAL_MACHINE\\Software\\RaGEZONE\\Gimme");
		initRegReader(reader, key);
		if (rrReadString(reader, "Registered", SAFESTR(default_dll_path))) {
			changeFileExt(default_dll_path, "DLL.dll", default_dll_path);
			strstriReplace(default_dll_path, "FDDLL", "DLLFD");
			strstriReplace(default_dll_path, "CCDLL", "DLLCC");
			if (0==_stat64(default_dll_path, &sbuf))
				bFoundIt = true;
		}
		destroyRegReader(reader);

		// Old way
		if(!bFoundIt)
		{
			reader = createRegReader();
			sprintf(key, "HKEY_CLASSES_ROOT\\*\\shell\\Gimme Checkout\\command");
			initRegReader(reader, key);
			if (rrReadString(reader, "", SAFESTR(default_dll_path))) {
				changeFileExt(default_dll_path, "DLL.dll", default_dll_path);
				strstriReplace(default_dll_path, "FDDLL", "DLLFD");
				if (0==_stat64(default_dll_path, &sbuf))
					bFoundIt = true;
			}
			destroyRegReader(reader);
		}

		if (!bFoundIt)
			strcpy(default_dll_path, "GimmeDLL.dll");
	}

	hGimmeDLL = loadGimmeDLL(default_dll_path);
	if (!hGimmeDLL && isDevelopmentMode() && fileExists("c:/Night/tools/bin/GimmeDLL.dll") &&
		winExistsInRegPath("C:/Night/tools/bin") && !winExistsInEnvPath("C:/Night/tools/bin"))
	{
		// PATH was just recently modified, not in this process's environment
		hGimmeDLL = loadGimmeDLL("C:\\Night\\tools\\bin\\GimmeDLL.dll");
	}
	if (!hGimmeDLL) {
		if (isDevelopmentMode()) {
			printf("GimmeDLL: Failed to load GimmeDLL.dll\n");
			if (!fileExists("c:/Night/tools/bin/GimmeDLL.dll")) {
				// Mis-matched setup?
				printf("c:/Night/tools/bin/GimmeDLL.dll does not exist.\n");
			} else {
				if (!winExistsInRegPath("C:/Night/tools/bin")) {
					printf("C:\\Night\\tools\\bin does not exist in the PATH.  Run \"gimme -register\" to add it.\n");
				} else if (!winExistsInEnvPath("C:/Night/tools/bin")) {
					// In reg but not ENV
					printf("C:\\Night\\tools\\bin has been added to the path, but this process does not have it.  A reboot may be required.\n");
				} else {
					// PATH looks fine!
					printf("Unknown reason why GimmeDLL.dll would fail to load. %s\n", lastWinErr());
				}
			}
		} else {
			verbose_printf("GimmeDLL: Failed to load GimmeDLL.dll\n");
		}
		bGimmeDLLEnabled = false;
	} else {
#if !_XBOX
		if (GetAsyncKeyState(VK_CONTROL) & 0x8000000 &&
			GetAsyncKeyState(VK_SHIFT) & 0x8000000 )
		{
			char *pFileName = NULL;
			GetModuleFileName_UTF8(hGimmeDLL, &pFileName);
			printf("GimmeDLL Using: %s\n", pFileName);
			estrDestroy(&pFileName);
		}
#endif
	}

	gimmeDLLSetMemoryAllocators(gimme_malloc_timed, gimme_calloc_timed, gimme_realloc_timed, gimme_free_timed);
	gimmeDLLSetVprintfFunc(vprintf_timed);
	gimmeDLLSetCrashStateFunc(crashState);
	gimmeDLLSetAutoTimer(autoTimerGet());
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void gimmeDLLDisable(int disable)
{
	bGimmeDLLEnabled = !disable;
	if (!disable)
		bGimmeDLLEnabledForce = false;
	else
		bGimmeDLLInited = true;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void gimmeDLLDemoMode(int demoMode)
{
	bGimmeDLLDemoMode = !!demoMode;
	gimmeDLLDisable(bGimmeDLLDemoMode);
}


GimmeErrorValue gimmeDLLDoOperation(const char *relpath, GIMMEOperation op, GimmeQuietBits quiet)
{
	char fullpath[CRYPTIC_MAX_PATH];
	GimmeErrorValue ret;
	PERFINFO_AUTO_START_FUNC();
	fileLocateWrite(relpath, fullpath);
#if !PLATFORM_CONSOLE
	// If not on the Xbox, and we're checking out, and we don't have the GimmeDLL, just make it writeable (for Outsourcing)
	if (op == GIMME_CHECKOUT) {
		GIMME_DLL_LAZY_INIT_EX(GimmeDoOperation, chmod(fullpath, _S_IREAD | _S_IWRITE), bGimmeDLLDemoMode?GIMME_NO_ERROR:GIMME_ERROR_NO_DLL);
	}
	else
#endif
	{
		GIMME_DLL_LAZY_INIT(GimmeDoOperation, bGimmeDLLDemoMode?GIMME_NO_ERROR:GIMME_ERROR_NO_DLL);
	}
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeDoOperation(fullpath, op, quiet);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

GimmeErrorValue gimmeDLLDoOperationsInitSub(void)
{
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeDoOperations, GIMME_ERROR_NO_DLL);
	PERFINFO_AUTO_STOP_FUNC();
	return GIMME_NO_ERROR;
}

GimmeErrorValue gimmeDLLDoOperations(const char **relpaths, GIMMEOperation op, GimmeQuietBits quiet)
{
	char **gameDirs=NULL;
	char **gameDataDirs=NULL;
	char ***fullpaths=NULL;
	int i,j;
	GimmeErrorValue final_ret;

	// Fast out
	if(!eaSize(&relpaths))
		return GIMME_NO_ERROR;

	gameDataDirs = (char**)fileGetGameDataDirs();
	eaCopy(&gameDirs, &gameDataDirs);
	for (i=0; i<eaSize(&gameDirs); i++)
	{
		char* lastSlash = NULL;
		gameDirs[i] = strdup(gameDirs[i]);
		forwardSlashes(gameDirs[i]);
		lastSlash = strrchr(gameDirs[i], '/');
		if (lastSlash)
			*lastSlash = '\0';
	}
	eaSetSize((void***)&fullpaths, eaSize(&gameDirs) + 1);
	
	for (i=0; i<eaSize(&relpaths); i++)
	{
		char buf[MAX_PATH];
		fileLocateWrite(relpaths[i], buf);
		for (j=0; j<eaSize(&gameDirs); j++)
		{
			if (strStartsWith(buf, gameDirs[j]))
			{
				eaPush(&fullpaths[j], strdup(buf));
				break;
			}
		}
		if (j==eaSize(&gameDirs))
		{
			eaPush(&fullpaths[j], strdup(buf));
		}
	}
	eaDestroyEx(&gameDirs, NULL);
	
	if (gimmeDLLDoOperationsInitSub()!=GIMME_NO_ERROR) {
		// Error logic
#if !PLATFORM_CONSOLE
		// If not on the Xbox, and we're checking out, and we don't have the GimmeDLL, just make it writeable (for Outsourcing)
		if (op == GIMME_CHECKOUT) {
			for (i=0; i<eaSize((void***)&fullpaths); i++) {
				for (j=0; j<eaSize(&fullpaths[i]); j++)
					chmod(fullpaths[i][j], _S_IREAD | _S_IWRITE);
			}
		}
#endif
		for (i=0; i<eaSize((void***)&fullpaths); i++)
			eaDestroyEx(&fullpaths[i], NULL);
		eaDestroy((void***)&fullpaths);
		return bGimmeDLLDemoMode?GIMME_NO_ERROR:GIMME_ERROR_NO_DLL;
	}

	final_ret = GIMME_NO_ERROR;
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	for (i=0; i!=eaSize((void***)&fullpaths); i++)
	{
		if (eaSize(&fullpaths[i]) > 0)
		{
			GimmeErrorValue error = pfnGimmeDoOperations(fullpaths[i], eaSize(&fullpaths[i]), op, quiet);
			if (final_ret == GIMME_NO_ERROR)
				final_ret = error;
		}
	}
	LeaveCriticalSection(&g_gimmedll_cs);
	for (i=0; i<eaSize((void***)&fullpaths); i++)
		eaDestroyEx(&fullpaths[i], NULL);
	eaDestroy((void***)&fullpaths);
	return final_ret;
}

GimmeErrorValue gimmeDLLDoOperationsDir(const char *dir, GIMMEOperation op, GimmeQuietBits quiet)
{
	char fullPath[ MAX_PATH ];
	char** files;

	fileLocateWrite( dir, fullPath );
	files = fileScanDir( fullPath );

	{
		GimmeErrorValue err = gimmeDLLDoOperations( files, op, quiet );
		eaDestroyEx( &files, NULL );
		return err;
	}
}

GimmeErrorValue gimmeDLLDoOperationsDirs(const char **dirs, GIMMEOperation op, GimmeQuietBits quiet)
{
	char** files = NULL;
	{
		int it;
		for( it = 0; it != eaSize( &dirs ); ++it ) {
			char fullPath[ MAX_PATH ];
			char** dirFiles;

			fileLocateWrite( dirs[ it ], fullPath );
			dirFiles = fileScanDir( fullPath );
			eaPushEArray( &files, &dirFiles );
			eaDestroy( &dirFiles );
		}
	}

	{
		GimmeErrorValue err = gimmeDLLDoOperations( files, op, quiet );
		eaDestroyEx( &files, NULL );
		return err;
	}
}

// Set the default checkin comment, when doing a GIMME_CHECKIN with GIMME_QUIET.
GimmeErrorValue gimmeDLLSetDefaultCheckinComment(const char *comment)
{
	GimmeErrorValue ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeSetDefaultCheckinComment, GIMME_ERROR_NO_DLL);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeSetDefaultCheckinComment(comment);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

const char *gimmeDLLQueryIsFileLocked(const char *relpath) // Returns username of who has a file checked out, may be the current user
{
	char fullpath[CRYPTIC_MAX_PATH];
	const char *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryIsFileLocked, "Failed to load GimmeDLL.DLL");
	fileLocateWrite(relpath, fullpath);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryIsFileLocked(fullpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

int gimmeDLLQueryIsFileLockedByMeOrNew(const char *relpath)
{
	char fullpath[CRYPTIC_MAX_PATH];
	int ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryIsFileLockedByMeOrNew, 1);
	fileLocateWrite(relpath, fullpath);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryIsFileLockedByMeOrNew(fullpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

int gimmeDLLQueryIsFileMine(const char *relpath)
{
	PERFINFO_AUTO_START_FUNC();
	if (g_disableLastAuthor) {
		PERFINFO_AUTO_STOP_FUNC();
		return 1;
	} else {
		char fullpath[CRYPTIC_MAX_PATH];
		int ret;
		GIMME_DLL_LAZY_INIT(GimmeQueryIsFileMine, 1);
		fileLocateWrite(relpath, fullpath);
		if (!fullpath[0])
		{
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
		gimmeDllInitCS();
		EnterCriticalSection(&g_gimmedll_cs);
		ret = pfnGimmeQueryIsFileMine(fullpath);
		LeaveCriticalSection(&g_gimmedll_cs);
		PERFINFO_AUTO_STOP_FUNC();
		return ret;
	}
}

const char *gimmeDLLQueryLastAuthor(const char *relpath)
{
	PERFINFO_AUTO_START_FUNC();
	if (g_disableLastAuthor) {
		PERFINFO_AUTO_STOP_FUNC();
		return "UNKNOWN";
	} else {
		char fullpath[CRYPTIC_MAX_PATH];
		const char *ret;
		GIMME_DLL_LAZY_INIT(GimmeQueryLastAuthor, "Source Control not available");
		fileLocateWrite(relpath, fullpath);
		if (!fullpath[0])
		{
			PERFINFO_AUTO_STOP_FUNC();
			return "UNKNOWN";
		}
		gimmeDllInitCS();
		EnterCriticalSection(&g_gimmedll_cs);
		ret = pfnGimmeQueryLastAuthor(fullpath);
		LeaveCriticalSection(&g_gimmedll_cs);
		PERFINFO_AUTO_STOP_FUNC();
		return ret;
	}
}

GimmeFileStatus gimmeDLLQueryFileStatus(const char *relpath)
{
	const char *lastAuthor = gimmeDLLQueryLastAuthor(relpath);
	if (stricmp(lastAuthor,"UNKNOWN") == 0 || strstri(lastAuthor, "Source Control not available"))
	{
		return GIMME_STATUS_UNKNOWN;
	}
	if (stricmp(lastAuthor,"You have this file checked out") == 0)
	{
		return GIMME_STATUS_CHECKED_OUT_BY_ME;
	}
	if (stricmp(lastAuthor,"Not in database") == 0)
	{
		return GIMME_STATUS_NEW_FILE;
	}
	return GIMME_STATUS_NOT_CHECKED_OUT_BY_ME;
}

int gimmeDLLQueryIsFileLatest(const char *relpath)
{
	char fullpath[CRYPTIC_MAX_PATH];
	int ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryIsFileLatest, 1);
	fileLocateWrite(relpath, fullpath);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryIsFileLatest(fullpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

const char *gimmeDLLQueryUserName(void)
{
	const char *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryUserName, "Failed to load GimmeDLL.DLL");
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryUserName();
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

int gimmeDLLQueryAvailable(void) // Whether or not source control is available
{
	int ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryAvailable, 0);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryAvailable();
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

int gimmeDLLQueryExists(void) // Whether or not the DLL is there (regardless of whether it can get to revision control or not, etc)
{
	if (!bGimmeDLLInited)
		gimmeDLLInit();
	if (!bGimmeDLLEnabled) {
		return 0;
	}
	return 1;
}

const char *gimmeDLLQueryBranchName(const char *localpath)
{
	const char *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryBranchName, "Failed to load GimmeDLL.DLL");
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryBranchName(localpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

int gimmeDLLQueryBranchNumber(const char *localpath)
{
	char fullpath[CRYPTIC_MAX_PATH];
	int ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryBranchNumber, -1);
	strcpy(fullpath, localpath);
	forwardSlashes(fullpath);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryBranchNumber(fullpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

int gimmeDLLQueryCoreBranchNumForDir(const char *localpath)
{
	int ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryCoreBranchNumForDir, GIMME_BRANCH_UNKNOWN);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryCoreBranchNumForDir(localpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

const char *gimmeDLLGetErrorString(GimmeErrorValue error)
{
	const char *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeGetErrorString, "Failed to load GimmeDLL.DLL");
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeGetErrorString(error);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

GimmeErrorValue gimmeDLLDoCommand(const char *cmdline)
{
	GimmeErrorValue ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeDoCommand, GIMME_ERROR_NO_DLL);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeDoCommand(cmdline);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

GimmeErrorValue gimmeDLLBlockFile(const char *relpath, const char *block_string)
{
	char fullpath[CRYPTIC_MAX_PATH];
	GimmeErrorValue ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeBlockFile, bGimmeDLLDemoMode?GIMME_NO_ERROR:GIMME_ERROR_NO_DLL);
	fileLocateWrite(relpath, fullpath);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeBlockFile(fullpath, block_string);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

GimmeErrorValue gimmeDLLUnblockFile(const char *relpath)
{
	char fullpath[CRYPTIC_MAX_PATH];
	GimmeErrorValue ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeUnblockFile, bGimmeDLLDemoMode?GIMME_NO_ERROR:GIMME_ERROR_NO_DLL);
	fileLocateWrite(relpath, fullpath);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeUnblockFile(fullpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

int gimmeDLLQueryIsFileBlocked(const char *relpath)
{
	char fullpath[CRYPTIC_MAX_PATH];
	int ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryIsFileBlocked, 0);
	fileLocateWrite(relpath, fullpath);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryIsFileBlocked(fullpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

const char *const *gimmeDLLQueryGroupListForUser(const char *username)
{
	const char *const *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryGroupListForUser, NULL);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryGroupListForUser(username);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

const char *const *gimmeDLLQueryGroupList(void)
{
	const char *const *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryGroupList, NULL);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryGroupList();
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

const char *const *gimmeDLLQueryFullGroupList(void)
{
	const char *const *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryFullGroupList, NULL);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryFullGroupList();
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

const char *const *gimmeDLLQueryFullUserList(void)
{
	const char *const *ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeQueryFullUserList, NULL);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeQueryFullUserList();
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

bool gimmeDLLForceManifest(bool force)
{
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeForceManifest, false);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	pfnGimmeForceManifest(force);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

bool gimmeDLLCreateConsoleHidden(bool hidden)
{
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeCreateConsoleHidden, false);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	pfnGimmeCreateConsoleHidden(hidden);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}



bool gimmeDLLForceDirtyBit(const char *fullpath)
{
	bool ret;
	PERFINFO_AUTO_START_FUNC();
	GIMME_DLL_LAZY_INIT(GimmeForceDirtyBit, false);
	gimmeDllInitCS();
	EnterCriticalSection(&g_gimmedll_cs);
	ret = pfnGimmeForceDirtyBit(fullpath);
	LeaveCriticalSection(&g_gimmedll_cs);
	PERFINFO_AUTO_STOP_FUNC();
	return ret;
}

#endif

typedef struct ManifestCache
{
	U32 timestamp;
	U32 crc;
	U32 size;
} ManifestCache;

StashTable gimmeDLLCacheManifestBinFiles(const char *example_file)
{
	char triviapath[MAX_PATH], rootpath[MAX_PATH], tmppath[MAX_PATH];
	char *slash;

	StashTable cache = stashTableCreateWithStringKeys(10240, StashDefault);

	// Find the trivia file
	fileLocateWrite(example_file, rootpath);
	forwardSlashes(rootpath);
	if(strEndsWith(rootpath, "/")){
		rootpath[strlen(rootpath) - 1] = 0;
	}
	if (strchr(rootpath, ':') != strrchr(rootpath, ':')) // invalid path, will cause assert in file layer (thinks it's a pigged path)
		return cache;

	for(;;)
	{
		sprintf(triviapath, "%s/%s/%s", rootpath, ".patch", "patch_trivia.txt"); // Secret knowledge
		if(fileExists(triviapath))
			break;
		else 
		{
			strcat(triviapath, ".old");
			if(fileExists(triviapath))
				break;
			else {
				sprintf(triviapath, "%s/%s/", rootpath, ".patch");
				if(dirExists(triviapath)) {
					strcat(triviapath, "patch_trivia.txt");
					break;
				}
			}
		}

		slash = strrchr(rootpath, '/');
		if(!slash)
		{
			rootpath[0] = '\0';
			return cache;
		}
		*slash = '\0';
	}

	// Parse the trivia file to get the project name, and therefore the manifest path
	// FIXME: This belongs in patchtrivia.c.
	{
		TriviaMutex mutex = triviaAcquireDumbMutex(triviapath);
		TriviaList *list = triviaListCreateFromFile(triviapath);
		triviaReleaseDumbMutex(mutex);
		if(list)
		{
			const char *val = triviaListGetValue(list, "PatchProject");
			if(val)
			{
				sprintf(tmppath, "%s/.patch/%s.manifest", rootpath, val);
			}
			else
			{
				triviaListDestroy(&list);
				return cache;
			}
			triviaListDestroy(&list);
		}
		else
		{
			return cache;
		}
	}

	// Parse the manifest
	{
		FILE *manifest;
		char *line;
		U32 tmp_timestamp, tmp_crc, tmp_filesize;

		manifest = fopen(tmppath, "r");
		if(!manifest)
			return cache;
		line = malloc(1024);
		fgets(line, 1024, manifest);
		while(fgets(line, 1024, manifest))
		{
			int n = sscanf_s(line, "%[^\t] %u %u %u", SAFESTR(tmppath), &tmp_timestamp, &tmp_filesize, &tmp_crc);
			ManifestCache *entry;
			const char *filepath;
			char *path;

			if (!strstri(tmppath, "/bin/") && !strstri(tmppath, "/tmpbin/") && !strstri(tmppath, "/HeatMapTemplates/"))
				continue;

			// Cache this entry
			entry = callocStruct(ManifestCache);
			entry->timestamp = tmp_timestamp;
			entry->crc = tmp_crc;
			entry->size = tmp_filesize;

			path = strstri(tmppath, "data/");
			if (path)
				path = path + 5;
			else
				path = tmppath;
			filepath = allocAddFilename(path);

			if(!stashAddPointer(cache, filepath, entry, false))
				free(entry);
		}
		fclose(manifest);
		free(line);
	}

	return cache;
}

bool gimmeDLLCheckBinFileMatchesManifest(StashTable cache, const char *file, U32 *out_timestamp)
{
	ManifestCache *entry;
	U32	md5_buf[4];
	size_t len;
	char buf[1024];
	char filename[MAX_PATH];
	FILE *f;
	
	strcpy(filename, file);
	forwardSlashes(filename);
	fixDoubleSlashes(filename);

	if(!stashFindPointer(cache, filename, &entry))
		return false;

	*out_timestamp = entry->timestamp;

	len = fileSize(filename);
	if(len != entry->size)
		return false;

	// Calculate the CRC of the real file
	fileLocateWrite(filename, buf);
	f = fopen(buf, "rb");
	if(!f)
		return false;
	while(len)
	{
		size_t bytes = len;
		if (bytes > ARRAY_SIZE(buf))
			bytes = ARRAY_SIZE(buf);
		if (bytes = fread(buf, 1, bytes, f))
			cryptMD5Update(buf, (U32)bytes);
		else
			break;
		len -= bytes;
	}
	fclose(f);
	cryptMD5Final(md5_buf);
	
	return md5_buf[0] == entry->crc;
}

#include "AutoGen/gimmeDLLPublicInterface_h_ast.c"
