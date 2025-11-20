#include "MemoryBudget.h"
#include "MemoryMonitor.h"
#include "EString.h"
#include "StringCache.h"
#include "StashTable.h"
#include "UnitSpec.h"
#include "File.h"
#include "MemoryBudget_h_ast.c"
#include "wininclude.h"
#include "sysutil.h"
#include "timing.h"
#include "utilitiesLib.h"
#include "MemReport.h"
#include "ContinuousBuilderSupport.h"
#include "cmdparse.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
AUTO_RUN_ANON(memBudgetAddMapping("autogen", BUDGET_EngineMisc););

static MemBudgetRequirement mem_budget_requirement = MemBudget_NotRequired;
static bool mem_budget_inited=false;
void memBudgetSetRequirement(MemBudgetRequirement req)
{
	mem_budget_requirement = req;
}

static MemoryBudget default_memory_budget = { "default" }; // Used before memory budgets are loaded


// Some utils (moved from MemoryMonitor)

void defaultHandler(char *appendMe, void *junk)
{
	printf("%s", appendMe);
}

#undef handlerPrintf
void handlerPrintf(OutputHandler handler, void *userdata, const char *fmt, ...)
{
	va_list ap;
	char buf[2048];

	va_start(ap, fmt);
	vsprintf(buf,fmt,ap);
	buf[sizeof(buf)-1] = '\0';
	va_end(ap);
	handler(buf, userdata);
}

void estrConcatHandler(char *appendMe, char **estrBuffer)
{
	estrConcat(estrBuffer, appendMe, (int)strlen(appendMe));
}

typedef struct BudgetDirFilter
{
	char *expr;
	const char *module;
} BudgetDirFilter;

MemoryBudget budgets_root;
StashTable stBudgets;
StashTable stMappings; // Mapping between file names and budget modules
StashTable stStructMappings; // Mapping between struct names and file names
static const char *budget_config_file;
static BudgetDirFilter **eaDirFilters;

#define MB *1024*1024

// Loads a new memory budget file, overwriting the budget values currently loaded
AUTO_COMMAND;
void memBudgetOverrideBudgets(const char *config_file)
{
	int i;
	MemoryBudget budgets_temp = {0};

	assert(budgets_root.subBudgets); // Did we get called before budgets were initialized?  Just do nothing?

	ParserLoadFiles(NULL, config_file, NULL, PARSER_OPTIONALFLAG, parse_MemoryBudget, &budgets_temp);
	for (i=0; i<eaSize(&budgets_temp.subBudgets); i++) {
		int j, k;
		MemoryBudget *old_budget=NULL;
		MemoryBudget *budget = budgets_temp.subBudgets[i];
		if (stashFindPointer(stBudgets, budget->module, &old_budget)) {
			old_budget->allowed = budget->allowed_mb MB;
			old_budget->allowed_mb = budget->allowed_mb;
			old_budget->allowed_secondary = budget->allowed_mb_secondary MB;
			old_budget->allowed_mb_secondary = budget->allowed_mb_secondary;
			for (j=0; j<eaSize(&budget->subBudgets); j++) {
				bool bFoundOne=false;
				for (k=0; k<eaSize(&old_budget->subBudgets); k++)
				{
					if (stricmp(old_budget->subBudgets[k]->module, budget->subBudgets[j]->module)==0)
					{
						bFoundOne = true;
						old_budget->subBudgets[k]->allowed = budget->subBudgets[j]->allowed_mb MB;
						old_budget->subBudgets[k]->allowed_mb = budget->subBudgets[j]->allowed_mb;
						old_budget->subBudgets[k]->allowed_secondary = budget->subBudgets[j]->allowed_mb_secondary MB;
						old_budget->subBudgets[k]->allowed_mb_secondary = budget->subBudgets[j]->allowed_mb_secondary;
					}
				}
				if (!bFoundOne)
				{
					Errorf("Loading new budget file which has a mapping for sub-budget \"%s\":\"%s\" which did not exist in the original budget file.",
						budget->module, budget->subBudgets[j]->module);
				}
			}
		} else {
			Errorf("Loading new budget file which has a mapping for \"%s\" which did not exist in the original budget file.",
				budget->module);
		}
	}
	StructDeInit(parse_MemoryBudget, &budgets_temp);	
}

void memBudgetSetConfigFile(const char *config_file)
{
	assertmsg(!budget_config_file, "Budget config file already set, or memBudgetStartup() already called.");
	budget_config_file = config_file;
}

#define MAPPINGS_TABLE_SIZE 4096
static void memBudgetInit(void)
{
	if (!stBudgets) {
		stBudgets = stashTableCreateWithStringKeys(64, StashDefault);
		stashTableSetThreadSafeLookups(stBudgets, true);
	}
	if (!stMappings) {
		stMappings = stashTableCreateWithStringKeys(MAPPINGS_TABLE_SIZE, StashDefault);
		stashTableSetThreadSafeLookups(stMappings, true);
	}
	if (!stStructMappings) {
		stStructMappings = stashTableCreateWithStringKeys(64, StashDefault);
		stashTableSetThreadSafeLookups(stStructMappings, true);
	}
}

AUTO_RUN_LATE;
void memBudgetAfterAutoRuns(void)
{
	// These stash tables are accessed in other threads, we cannot resize them
	// In practice it should be safe to read from them while the main thread is
	//   writing to them because the only thread that writes is the main thread,
	//   and the background threads should only ever get hits in the table
	//   (never possibly reading from the same slot that is being written to)
	//   though there is a possibility with the special code for physx libraries
	//   that this assumption is not true.
	if (stashGetOccupiedSlots(stBudgets) > MAPPINGS_TABLE_SIZE / 2)
		printf("Warning: MemoryBudget.c:stMappings might get full and cannot resize, increase initial size\n");
	stashTableSetCantResize(stBudgets, true);
	stashTableSetCantResize(stMappings, true);
}

// Parameter must be a static string
void memBudgetAddBudget(const char *module, size_t allowed)
{
	MemoryBudget *budget;
	memBudgetInit();
	if (!stashFindPointer(stBudgets, module, &budget)) {
		budget = calloc(sizeof(*budget), 1);
		budget->allowed = allowed;
		budget->current = 0;
		budget->module = allocAddStaticString(module);
		eaPush(&budgets_root.subBudgets, budget);
		stashAddPointer(stBudgets, budget->module, budget, false);
	} else {
		assert(budget->allowed == allowed);
	}
}

// Assumes both strings are persistent/static, if not, call allocAddString on them first!
// If they are static, they'll seed the stringcache
// If this is called at run-time, the strings must actually be in the string cache
void memBudgetAddMapping(const char *filename, const char *moduleName)
{
	char buf[MAX_PATH];
	bool bRet;
	const char *s;
	memBudgetInit();
	getFileNameNoExt(buf, filename);
	if (stricmp(buf, filename)!=0) {
		// Short version is not the same, perhaps this was called with __FILE__
		filename = allocAddString(buf);
	}
	if ((s=allocFindString(moduleName)))
		moduleName = s;
	else
		moduleName = allocAddStaticString(moduleName);
	// This happens because of load/init order, and because we're using #defines, this should be safe to ignore now:
	//   assertmsg(stashFindPointer(stBudgets, moduleName, NULL), "Budget added for module which does not exist");
	if(stricmp(filename, "cannotLockStringCache"))
	{
		bRet = stashAddPointer(stMappings, filename, (char*)moduleName, false); // Otherwise a duplicate?
		if (!bRet)
			printf("Duplicate memory budget for %s->%s\n", filename, moduleName);
		ignorableAssert(bRet);
	}
}

// Assumes both strings are persistent/static, if not, call allocAddString on them first!
void memBudgetAddStructMapping(const char *structname, const char *filename)
{
	char *value;
	char buf1[100];
	char buf2[100];
	memBudgetInit();
	if (stashFindPointer(stStructMappings, structname, &value)) {
		getFileNameNoExt(buf1, filename);
		getFileNameNoExt(buf2, value);
		assertmsgf(stricmp(buf1, buf2)==0, "Same struct (%s) exists in two different files (%s and %s)", structname, value, buf1);
	} else {
		verify(stashAddPointer(stStructMappings, structname, (char*)filename, false)==true);
	}
}

// Assumes both strings are persistent/static, if not, call allocAddString on them first!
void memBudgetAddStructMappingIfNotMapped(const char *structname, const char *filename)
{
	char *value;
	memBudgetInit();
	if (stashFindPointer(stStructMappings, structname, &value)) {
		// Already mapped so do nothing.
	} else {
		verify(stashAddPointer(stStructMappings, structname, (char*)filename, false)==true);
	}
}

// Assumes module string is persistent/static, if not, call allocAddString on it first!
void memBudgetAddDirFilterMapping(const char *expr, const char *moduleName)
{
	BudgetDirFilter *filter = calloc(1, sizeof(*filter));
	memBudgetInit();
	filter->expr = strdup(expr);
	filter->module = moduleName;
	eaPush(&eaDirFilters, filter);
}


char *filenameWithStructMappingInFixedSizeBuffer(const char *filenameOrStructname, int strwidth, char *buf, int buf_size)
{
	char *filename;
	if (stashFindPointer(stStructMappings, filenameOrStructname, &filename)) {
		int len;
		char filenameBuf[MAX_PATH];
		char *s;
		int filenameStrwidth;
		sprintf_s(SAFESTR2(buf), "%s (", filenameOrStructname);
		len = (int)strlen(buf);
		filenameStrwidth = strwidth - (len + 1);
		if (filenameStrwidth < 5)
			filenameStrwidth = 5;
		assert(filenameStrwidth < ARRAY_SIZE(filenameBuf));
		filenameInFixedSizeBuffer(filename, filenameStrwidth, SAFESTR(filenameBuf), true);
		for (s=filenameBuf; *s==' '; s++);
		strcat_s(SAFESTR2(buf), s);
		strcat_s(SAFESTR2(buf), ")");
		len = (int)strlen(buf);
		if (len < strwidth) {
			strcpy(filenameBuf, buf);
			s = buf;
			while (len < strwidth) {
				*s = ' ';
				s++;
				len++;
			}
			strcpy_s(s, buf_size - (s - buf), filenameBuf);
		}
		return buf;
	}
	return filenameInFixedSizeBuffer(filenameOrStructname, strwidth, SAFESTR2(buf), true);
}

static void memBudgetAddDefaultBudgets(void)
{
	//memBudgetAddBudget(BUDGET_Textures_World, 53 MB);
	//memBudgetAddBudget(BUDGET_Textures_Character, 36 MB);
	//memBudgetAddBudget(BUDGET_Textures_FX, 6 MB);
	memBudgetAddBudget(BUDGET_Textures_Art, (53+36+6) MB);
	memBudgetAddBudget(BUDGET_Textures_Misc, 0 MB);
	memBudgetAddBudget(BUDGET_Materials, 0 MB);
	//memBudgetAddBudget(BUDGET_Geometry_World, 23 MB);
	//memBudgetAddBudget(BUDGET_Geometry_Character, 10 MB);
	//memBudgetAddBudget(BUDGET_Geometry_FX, 2 MB);
	memBudgetAddBudget(BUDGET_Geometry_Art, (23+10+2) MB);
	memBudgetAddBudget(BUDGET_Geometry_Misc, 0 MB);
	memBudgetAddBudget(BUDGET_Terrain_Art, 11 MB);
	memBudgetAddBudget(BUDGET_Terrain_System, 1 MB);
	memBudgetAddBudget(BUDGET_Framebuffers, 48 MB);
	memBudgetAddBudget(BUDGET_Animation, 40 MB);
	memBudgetAddBudget(BUDGET_Physics, 15 MB);
	memBudgetAddBudget(BUDGET_FXSystem, 10 MB);
	memBudgetAddBudget(BUDGET_Audio, 20 MB);
	memBudgetAddBudget(BUDGET_World, 30 MB);
	memBudgetAddBudget(BUDGET_FileSystem, 20 MB);
	memBudgetAddBudget(BUDGET_GameSystems, 35 MB);
	memBudgetAddBudget(BUDGET_EXE, 20 MB);
	memBudgetAddBudget(BUDGET_SystemReserved, 32 MB);
	memBudgetAddBudget(BUDGET_Slack, 90 MB);
	memBudgetAddBudget(BUDGET_Unsorted, 0 MB);
	memBudgetAddBudget(BUDGET_Renderer, 0 MB);
	memBudgetAddBudget(BUDGET_Fonts, 0 MB);
	memBudgetAddBudget(BUDGET_Networking, 0 MB);
	memBudgetAddBudget(BUDGET_EngineMisc, 0 MB);
	memBudgetAddBudget(BUDGET_UISystem, 0 MB);
	memBudgetAddBudget(BUDGET_Unknown, 10 MB);
}

void memBudgetMoveFromDefault(void)
{
	ModuleMemOperationStats *stat;
	while (stat = eaPop(&default_memory_budget.stats))
	{
		MemoryBudget *budget = memBudgetGetByFilename(stat->moduleName);
		assert(budget && budget != &default_memory_budget);
		default_memory_budget.current -= stat->size;
		default_memory_budget.count -= stat->count;
		default_memory_budget.traffic -= stat->countTraffic;
		budget->current += stat->size;
		budget->count += stat->count;
		budget->traffic += stat->countTraffic;
		stat->parentBudget = budget;
		eaPush(&budget->stats, stat);
	}
	eaDestroy(&default_memory_budget.stats);
}

AUTO_RUN;
void memBudgetAddHardcodedMappings(void)
{
	// Mappings for things without files

	memBudgetAddMapping("stringtable", BUDGET_Unsorted); // Shouldn't have any at all!
	memBudgetAddMapping("stashtable", BUDGET_Unsorted); // Shouldn't have any at all!
// 	memBudgetAddMapping("earray", BUDGET_Unsorted); // Shouldn't have any at all!
	memBudgetAddMapping("tokenstore", BUDGET_Unsorted); // Shouldn't have any at all!

	memBudgetAddMapping("Textures:Misc", BUDGET_Textures_Misc);
	memBudgetAddMapping("Textures:World", BUDGET_Textures_Art);
	memBudgetAddMapping("Textures:Character", BUDGET_Textures_Art);
	memBudgetAddMapping("Textures:Terrain", BUDGET_Terrain_Art);
	memBudgetAddMapping("Textures:FX", BUDGET_Textures_Art);
	memBudgetAddMapping("Textures:UI", BUDGET_Textures_Art);
	memBudgetAddMapping("Textures:Fonts", BUDGET_Textures_Art);
	memBudgetAddMapping("VideoMemory:SurfaceTexture", BUDGET_Framebuffers);
	memBudgetAddMapping("Systemreserved", BUDGET_SystemReserved);
	memBudgetAddMapping("Sound-PhysicalAlloc", BUDGET_Audio);
	memBudgetAddMapping("StartupSize", BUDGET_EXE);
	memBudgetAddMapping("VideoMemory:RenderTargetDepth", BUDGET_Framebuffers);
	memBudgetAddMapping("VideoMemory:RenderTarget", BUDGET_Framebuffers);
	memBudgetAddMapping("VideoMemory:TempVBO", BUDGET_Renderer);
	memBudgetAddMapping("VideoMemory:TempIBO", BUDGET_Renderer);
	memBudgetAddMapping("VideoMemory:QuadIBO", BUDGET_Renderer);
	memBudgetAddMapping("VideoMemory:SpriteIndexBuffer", BUDGET_Renderer);
	memBudgetAddMapping(" file cache", BUDGET_FileSystem);
	memBudgetAddMapping("file cache", BUDGET_FileSystem);
	memBudgetAddMapping(" drive", BUDGET_FileSystem); // devkit:/ drive
	memBudgetAddMapping("drive", BUDGET_FileSystem);
	memBudgetAddMapping("Fmod untracked", BUDGET_Audio);
	memBudgetAddMapping("Unused Small Alloc", BUDGET_Slack);
	memBudgetAddMapping("Unknown", BUDGET_Unknown); // Unknown caller, unknown budget

#if _PS3
    memBudgetAddMapping("VideoMemory:Sound", BUDGET_Audio);
    memBudgetAddMapping("VideoMemory:Buffers", BUDGET_Renderer);
    memBudgetAddMapping("VideoMemory:Misc", BUDGET_Unsorted);
    memBudgetAddMapping("VideoMemory:Anim", BUDGET_Animation);

    memBudgetAddMapping("Main:IO", BUDGET_Unsorted);
	memBudgetAddMapping("Main:Aligned", BUDGET_Unsorted);
    memBudgetAddMapping("Main:ZOcclusion", BUDGET_Unsorted);
    memBudgetAddMapping("Main:DynWind", BUDGET_FXSystem);

    memBudgetAddMapping("Main:Shaders", BUDGET_Unsorted);
#else
	memBudgetAddMapping("crtdbg.h", BUDGET_Unknown);
	memBudgetAddMapping("XLive startup size", BUDGET_SystemReserved);

	memBudgetAddMapping("VideoMemory:Backbuffer", BUDGET_Framebuffers);
	memBudgetAddMapping("VideoMemory:Depthbuffer", BUDGET_Framebuffers);

    memBudgetAddMapping("rxbx:Textures", BUDGET_Unsorted); // Shouldn't have any at all!
	memBudgetAddMapping("rxbx:RingBuffer", BUDGET_Framebuffers);
#endif
	//memBudgetAddMapping("testharness", BUDGET_); // Don't load?
}

static void memBudgetAddDefaultMappings(void)
{
	static bool done_once=false;
	if (done_once)
		return;
	done_once = true;

	stashTableSetCantResize(stBudgets, false);

	if (!budget_config_file || !fileExists(budget_config_file)) {
		// Defaults
		memBudgetAddDefaultBudgets();
		budget_config_file = "Default";
	} else {
		int i;
		memBudgetInit();
		// Load from file
		ParserLoadFiles(NULL, budget_config_file, NULL, PARSER_OPTIONALFLAG, parse_MemoryBudget, &budgets_root);
		for (i=0; i<eaSize(&budgets_root.subBudgets); i++) {
			int j;
			MemoryBudget *old_budget=NULL;
			budgets_root.subBudgets[i]->allowed = budgets_root.subBudgets[i]->allowed_mb MB;
			budgets_root.subBudgets[i]->allowed_secondary = budgets_root.subBudgets[i]->allowed_mb_secondary MB;
			if (!stashFindPointer(stBudgets, budgets_root.subBudgets[i]->module, &old_budget)) {
				stashAddPointer(stBudgets, budgets_root.subBudgets[i]->module, budgets_root.subBudgets[i], false);
			} else {
				assert(old_budget->allowed == budgets_root.subBudgets[i]->allowed);
			}
			for (j=0; j<eaSize(&budgets_root.subBudgets[i]->subBudgets); j++) {
				budgets_root.subBudgets[i]->subBudgets[j]->allowed = budgets_root.subBudgets[i]->subBudgets[j]->allowed_mb MB;
				budgets_root.subBudgets[i]->subBudgets[j]->allowed_secondary = budgets_root.subBudgets[i]->subBudgets[j]->allowed_mb_secondary MB;
			}
		}
	}

	mem_budget_inited = true;
	stashTableSetCantResize(stBudgets, true);

	// Move things from default_memory_budget into these budgets
	memBudgetMoveFromDefault();
}

// Must be called before crashed!
void memBudgetStartup(void)
{
	memBudgetAddDefaultMappings();
}

// TLS slot for per-thread recursion count.
static int memBudgetRecurseIndex = TLS_OUT_OF_INDEXES;

// Return false if we are recursing, and should not enter.
// This function is thread-safe, but not re-entrant, so it can't call any other Cryptic functions.
static bool memBudgetEnter()
{
	int recurse;
	bool tls_success;

	// Allocate TLS slot, if necessary.
	// Note: This can't use STATIC_THREAD_ALLOC because STATIC_THREAD_ALLOC() uses the allocator, and the allocator uses MemoryBudget.
	// See stdtypes.h for why we can't use __declspec(thread), even though that would be so much easier.
	ATOMIC_INIT_BEGIN;
	memBudgetRecurseIndex = TlsAlloc();
	ATOMIC_INIT_END;
	assert(memBudgetRecurseIndex != TLS_OUT_OF_INDEXES);

	// Increment TLS slot value.
	recurse = (int)TlsGetValue(memBudgetRecurseIndex);
	assert(recurse || GetLastError() == ERROR_SUCCESS);
	++recurse;
	tls_success = TlsSetValue(memBudgetRecurseIndex, (void *)recurse);
	assert(tls_success);

	return recurse == 1;
}

static void memBudgetExit()
{
	int recurse;
	bool tls_success;

	assert(memBudgetRecurseIndex != TLS_OUT_OF_INDEXES);

		// Decrement TLS slot value.
	recurse = (int)TlsGetValue(memBudgetRecurseIndex);
	assert(recurse || GetLastError() == ERROR_SUCCESS);
	--recurse;
	tls_success = TlsSetValue(memBudgetRecurseIndex, (void *)recurse);
	assert(tls_success);
}

MemoryBudget *memBudgetGetByFilename(const char *filename)
{
	char buf[MAX_PATH];
	char *moduleName=NULL;
	MemoryBudget *budget=NULL;

	// In case of re-entrancy, fall back to the default budget, to prevent infinite recursion.
	if (!memBudgetEnter())
		return &default_memory_budget;

	// Look up the module that goes with this filename.
	getFileNameNoExt(buf, filename);
	if (!strEndsWith(filename, ".c") && !strEndsWith(filename, ".h")) // Don't let ZoneMap.c get converted to struct ZoneMap converted to WorldGrid.c
		if (stashFindPointer(stStructMappings, buf, &moduleName))
			getFileNameNoExt(buf, moduleName);
	if (stMappings && !stashFindPointer(stMappings, buf, &moduleName)) {
		int i;
		if (!stashFindPointer(stMappings, filename, &moduleName))
		{
			//if (amount > 4096) {
			//	printf("memBudgetAddMapping(\"%s\", \"\");\n", buf);
			//}
			for (i = 0; i < eaSize(&eaDirFilters); ++i)
				if (simpleMatch(eaDirFilters[i]->expr, filename))
					moduleName = (char *)eaDirFilters[i]->module;
		}
	}

	// Handle the case where an allocation doesn't seem to have a budget.
	if (!moduleName) {
		if (mem_budget_inited) {
			if (mem_budget_requirement == MemBudget_Required_Assert) {
				char msg[1024];
				sprintf(msg, "Memory allocated from %s, which does not belong to any budget.  Allocations must be assigned to budgets.  Please attach and debug/fix, program will now assert.", filename);
				printf("%s\n", msg);
#if !PLATFORM_CONSOLE
				MessageBox_UTF8(NULL, msg, "Untracked memory allocation", MB_OK | MB_TASKMODAL); 
				// This'll probably hang for a couple minutes because the StackWalker doesn't handle threads having the heap locked well...
				ignorableAssertmsg(0, msg);
#endif
			} else if (mem_budget_requirement == MemBudget_Required_LogToN || mem_budget_requirement == MemBudget_Required_PopupForProgrammers) {
				char msg[1024];

				sprintf(msg, "Memory allocated from %s, which does not belong to any budget.  Allocations must be assigned to budgets. See MemoryBudget.c(" LINE_STR ") for details", filename);
				printf("%s\n", msg);


				if (g_isContinuousBuilder)
				{
					assertmsg(0, msg);
				}

#if !PLATFORM_CONSOLE
                if (mem_budget_requirement == MemBudget_Required_PopupForProgrammers && IsDebuggerPresent()) {

					//MessageBox(NULL, msg, "Untracked memory allocation", MB_OK | MB_TASKMODAL | MB_ICONEXCLAMATION); 
					_DbgBreak(); // Please add the appropriate file to a budget!  See the console.
					//////////////////////////////////////////////////////////////////////////
					// You add a file's allocations to a budget by inserting a line like this:
					//   AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
					// This line should be near the top of the file (after the #includes).
					// All budget categories are listed in MemoryBudgets.h
					// If you are on a game team, and this is your memory allocations, the
					//   budget should most likely be BUDGET_GameSystems.
					// Note: .cpp files are not StructParse'd, so put this in the .h instead
					//   or a related .c file (and changed __FILE__ to the actual filename).
					//////////////////////////////////////////////////////////////////////////

				}


				if (UserIsInGroup("Software"))
					Alertf("%s", msg);
				if (!g_disableLastAuthor) { // Probably no N: access
					FILE *f;
					char fn[MAX_PATH];
					sprintf(fn, "N:/game/logs/%s.log", getComputerName());
					mkdirtree(fn);
					f = fopen(fn, "a");
					if (f) {
						fprintf(f, "%s %d %s\n", timeGetLocalDateString(), gBuildVersion, filename);
						fclose(f);
					}
				}
#endif
			}
		}
		moduleName = "Unknown";
	}
	if (!stBudgets || !stashFindPointer(stBudgets, moduleName, &budget)) {
		bool bRet = stBudgets && stashFindPointer(stBudgets, "Unknown", &budget);
		if (!bRet) {
			// No budgets initialized yet, otherwise, there needs to be an Unknown
			assert(!stBudgets || !stashGetCount(stBudgets));
			memBudgetExit();
			return &default_memory_budget;
		}
	}

	assert(budget);
	memBudgetExit();
	return budget;
}

static int cmpBudgetStat(const void *a, const void *b)
{
	const ModuleMemOperationStats *stat1 = *(const ModuleMemOperationStats **)a;
	const ModuleMemOperationStats *stat2 = *(const ModuleMemOperationStats **)b;
	if (stat1->size > stat2->size)
		return 1;
	if (stat1->size < stat2->size)
		return -1;
	return 0;
}

void memBudgetDisplay(OutputHandler handler, void *userdata, bool verbose)
{
	int i;
	if (!eaSize(&budgets_root.subBudgets))
		return;

	updateMemoryBudgets();

	handlerPrintf(handler, userdata, "------------------------------------------------------------------------------\n");
	handlerPrintf(handler, userdata, "Memory Budget Summary:\n");
	for (i=0; i<eaSize(&budgets_root.subBudgets); i++) {
		MemoryBudget *budget = budgets_root.subBudgets[i];
		char buf1[128];
		char buf2[128];
		const char *status = 
            (budget->allowed==0)?"NoBudget":
            (budget->current > (U64)budget->allowed)?"OVER":
            (budget->current > (U64)(0.75 * budget->allowed))?"Close":
            "ok";
		handlerPrintf(handler, userdata, " %9s %16s : %12s of %12s\n", status, budget->module, friendlyBytesAlignedBuf(budget->current, buf1), friendlyBytesAlignedBuf(budget->allowed, buf2));
		if (verbose) {
			int j;
			eaQSort(budget->stats, cmpBudgetStat);
			for (j=0; j<eaSize(&budget->stats); j++) {
				if (budget->stats[j]->size > 1024 || eaSize(&budget->stats)<5) {
					handlerPrintf(handler, userdata, "           %s : %s\n", filenameWithStructMappingInFixedSizeBuffer(budget->stats[j]->moduleName, 50, SAFESTR(buf2)), friendlyBytesBuf(budget->stats[j]->size, buf1));
				}
			}
			handlerPrintf(handler, userdata, "\n");
		}
	}
}

MemoryBudget **memBudgetGetBudgets(void)
{
	return budgets_root.subBudgets;
}

MemoryBudget *memBudgetGetBudgetByName(const char *moduleName)
{
	MemoryBudget *budget=NULL;
	//memBudgetAddDefaultMappings();
	if (!stashFindPointer(stBudgets, moduleName, &budget)) {
		return NULL;
	}
	return budget;
}
