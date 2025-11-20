GCC_SYSTEM

#include "VirtualMemory.h"
#include "MemReport.h"
#include "MemTrack.h"
#include "earray.h"
#include "stashtable.h"
#include "MemoryMonitor.h"
#include "AutoGen/MemoryMonitor_h_ast.c"
#include "sysutil.h"
#include "MemoryPool.h"
#include "MemAlloc.h"
#include "DebugState.h"
#include "ScratchStack.h"
#include "UnitSpec.h"
#include "LinearAllocator.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "file.h"
#include "windefinclude.h"
#include "utf8.h"

#if !PLATFORM_CONSOLE
#include <Psapi.h>
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#ifdef _XBOX
//#define TRACK_HEAP_OVERHEAD (18+4)
#define TRACK_HEAP_OVERHEAD 18 // Using DL malloc gets us down by about 4 bytes per alloc
#else
#ifdef _M_X64
#define TRACK_HEAP_OVERHEAD 27 // Actual struct is 10, the additional is from empirical evidence
#else
#define TRACK_HEAP_OVERHEAD 18 // Actual struct is 6 or 10, the additional is from empirical evidence
#endif
#endif

static OutputHandler mem_output_handler = defaultHandler;
static void *mem_userdata = NULL;

CRITICAL_SECTION memReportCS;

typedef struct FileMemInfo
{
	ModuleMemOperationStats	*stats;
	AllocTracker			tracker;
} FileMemInfo;

static struct {
	size_t unused;
	size_t used;
} memPoolTotals;

typedef struct BigAllocTracker
{
	S64	bytes;
	MemTrackType	num_allocs;
	MemTrackType	total_allocs;
} BigAllocTracker;


static int fileWidth;
static int poolPrintIndex;

static void printMemoryPoolInfoHelper(MemoryPool pool, void *userData)
{
	int bgColor;
	int brightness;
	char fmt[128];
	char fileNameBuffer[1000];
	char* fileName = fileNameBuffer;
	size_t usedMemory = mpGetAllocatedCount(pool) * mpStructSize(pool);
	F32 percent;
	
	bgColor = (poolPrintIndex % 5) ? 0 : COLOR_BLUE;
	brightness = COLOR_BRIGHT;
	
	poolPrintIndex++;

	memPoolTotals.used += usedMemory;
	memPoolTotals.unused += mpGetAllocatedChunkMemory(pool) - usedMemory;

	if(mpGetAllocatedChunkMemory(pool)){
		percent = (F32)(100.0 * (F32)(usedMemory) / mpGetAllocatedChunkMemory(pool));
	}else{
		percent = 100;
	}
	
	if(percent < 10){
		consoleSetColor(brightness|COLOR_RED, bgColor);
	}
	else if(percent < 25){
		consoleSetColor(brightness|COLOR_RED|COLOR_GREEN, bgColor);
	}
	else if(percent < 50){
		consoleSetColor(brightness|COLOR_GREEN|COLOR_BLUE, bgColor);
	}
	else{
		consoleSetColor(brightness|COLOR_GREEN, bgColor);
	}
		
	if(mpGetFileName(pool)){
		strcpy_s(fileName, ARRAY_SIZE_CHECKED(fileNameBuffer), mpGetFileName(pool));

		fileName = getFileName(fileNameBuffer);
		
		strcatf(fileNameBuffer, ":%-4d", mpGetFileLine(pool));
		if (strlen(fileName) > (size_t)fileWidth) {
			strcpy_s(fileName+2, ARRAY_SIZE_CHECKED(fileNameBuffer) - (fileName+2-fileNameBuffer), fileName + strlen(fileName)-(fileWidth - 2));
			fileName[0]=fileName[1]='.';
		}
	}else{
		strcpy_s(fileName, ARRAY_SIZE_CHECKED(fileNameBuffer), "");
	}
	
	sprintf(fmt, "%%-30s:%%%ds  %%9s %%9s %%12s %%12s  %%5.1f%%%%\n", fileWidth);
	handlerPrintf(mem_output_handler, mem_userdata, FORMAT_OK(fmt),
			FIRST_IF_SET(mpGetName(pool), "???"),
			fileName,
			getCommaSeparatedInt(mpGetAllocatedCount(pool)),
			getCommaSeparatedInt(mpStructSize(pool)),
			getCommaSeparatedInt(mpGetAllocatedChunkMemory(pool)),
			getCommaSeparatedInt(mpGetAllocatedChunkMemory(pool) - usedMemory),
			percent);
	
	consoleSetDefaultColor();
}

AUTO_COMMAND;
void printMemoryPoolInfo()
{
	char fmt[256];
	
	fileWidth = 40;
	
	ZeroStruct(&memPoolTotals);

	sprintf(fmt, "--- MEMORY POOLS: -------Name-%%%ds--Allocated--StructSize----Memory--------Unused----Used--\n", fileWidth);
	handlerPrintf(mem_output_handler, mem_userdata, FORMAT_OK(fmt), "File");
	poolPrintIndex = 0;
	mpForEachMemoryPool(printMemoryPoolInfoHelper, NULL);
	handlerPrintf(mem_output_handler, mem_userdata, "Total Used:   %s bytes (%1.2f%%)\n"
			"Total Unused: %s bytes\n",
			getCommaSeparatedInt(memPoolTotals.used),
			100.0 * (F32)memPoolTotals.used / (memPoolTotals.used + memPoolTotals.unused),
			getCommaSeparatedInt(memPoolTotals.unused));
	handlerPrintf(mem_output_handler, mem_userdata, "------------------------------------------------------------------------------\n");
}

bool filterVideoMemory(const char *moduleName)
{
#if _XBOX
	// On Xbox, none of these take additional system memory (tracked through XMemAlloc:D3D:Physical instead)
	if (strStartsWith(moduleName, "VideoMemory:") ||
		strStartsWith(moduleName, "rxbx:") ||
		strStartsWith(moduleName, "Textures:") ||
		strStartsWith(moduleName, "Geo:"))
	{
		return true;
	}
#else
	// On PC the managed copy of textures, etc all take system memory, only
	// the non-managed things (flagged with VideoMemory:) do not take system memory
	if (strStartsWith(moduleName, "VideoMemory:"))
		return true;
#endif
	return false;
}


__forceinline static void accumTracker(AllocTracker *tot,AllocTracker *tracker)
{
	tot->bytes += tracker->bytes;
	tot->num_allocs += tracker->num_allocs;
	tot->total_allocs += tracker->total_allocs;
}

__forceinline static void accumBigTracker(BigAllocTracker *tot,AllocTracker *tracker)
{
	tot->bytes += tracker->bytes;
	tot->num_allocs += tracker->num_allocs;
	tot->total_allocs += tracker->total_allocs;
}

static int cmpUsedEntry(const int *a,const int *b)
{
	S64 ta=0,tb=0,na=0,nb=0;
	int i;

	for(i=0;i<memtrack_thread_count;i++)
	{
		ta += memtrack_counts[i][*a].bytes;
		na += memtrack_counts[i][*a].num_allocs;
		tb += memtrack_counts[i][*b].bytes;
		nb += memtrack_counts[i][*b].num_allocs;
	}
	ta = ABS(ta) + (na > 0 ? na * TRACK_HEAP_OVERHEAD : 0);
	tb = ABS(tb) + (nb > 0 ? nb * TRACK_HEAP_OVERHEAD : 0);

	if (ta<tb)
		return -1;
	if (ta>tb)
		return 1;
	i = stricmp(memtrack_names_pooled[memtrack_names[*a].pooled_index].filename, memtrack_names_pooled[memtrack_names[*b].pooled_index].filename);
	if(i)
		return i;
	if(memtrack_names[*a].line < memtrack_names[*b].line)
		return -1;
	if(memtrack_names[*a].line > memtrack_names[*b].line)
		return 1;
	return 0;
}

static int cmpFileMemInfo(const FileMemInfo **pa, const FileMemInfo **pb)
{
	S64 ta=0,tb=0;

	ta = ABS((*pa)->tracker.bytes) + MAX(0,(*pa)->tracker.num_allocs) * TRACK_HEAP_OVERHEAD;
	tb = ABS((*pb)->tracker.bytes) + MAX(0,(*pb)->tracker.num_allocs) * TRACK_HEAP_OVERHEAD;

	if (ta<tb)
		return -1;
	if (ta>tb)
		return 1;

	return stricmp((*pa)->stats->moduleName, (*pb)->stats->moduleName);
}

static StashTable	filename_hashes; // Guarded by memReportCS
static FileMemInfo **file_mem_infos; // Guarded by memReportCS

#define MILLION 1048576.0

static int s_bZeroTotalAllocs = false;
AUTO_CMD_INT(s_bZeroTotalAllocs,MemReportZeroTotalAllocs);

static void printMemLine(OutputHandler cb, void *user,AllocTracker *tracker,const char *filename,int line,int shared)
{
	char		numbuf[20] = "      ";
	char		sizebuf[64];
	S64			total;
	char		buf[64];
	MemTrackType total_allocs = s_bZeroTotalAllocs ? 0 : tracker->total_allocs;

	filenameWithStructMappingInFixedSizeBuffer(filename, 44, SAFESTR(buf));

	switch(line){
		xcase 0:
			// Ignored.
		xcase LINENUM_FOR_STRUCTS:
			sprintf(numbuf, "%-6s", "STRUCT");
		xcase LINENUM_FOR_STRINGS:
			sprintf(numbuf, "%-6s", "STRING");
		xcase LINENUM_FOR_EARRAYS:
			sprintf(numbuf, "%-6s", "EARRAY");
		xcase LINENUM_FOR_POOLED_STRUCTS:
			sprintf(numbuf, "%-6s", "PLSTRU");
		xcase LINENUM_FOR_TS_POOLED_STRUCTS:
			sprintf(numbuf, "%-6s", "TSMPST");
		xcase LINENUM_FOR_POOLED_STRINGS:
			sprintf(numbuf, "%-6s", "PSTRNG");
		xdefault:
			sprintf(numbuf,"%-6d",line);
	}
	total = tracker->bytes;
	if (total > 0)
		total += TRACK_HEAP_OVERHEAD * MAX(0, tracker->num_allocs);

#if MEMTRACK_64BIT
	handlerPrintf(cb,user,"%44.44s:%s %10s%c %7"FORM_LL"d %7"FORM_LL"d\n",buf,numbuf, friendlyLazyBytesBuf(total, sizebuf),shared?'*':' ',tracker->num_allocs,total_allocs);
#else
	handlerPrintf(cb,user,"%44.44s:%s %10s%c %7d %7d\n",buf,numbuf, friendlyLazyBytesBuf(total, sizebuf),shared?'*':' ',tracker->num_allocs,total_allocs);
#endif
}

static void printBigMemLine(OutputHandler cb, void *user,BigAllocTracker *tracker,const char *filename,int line,int shared)
{
	char		numbuf[20] = "      ";
	char		sizebuf[64];
	S64			total;
	char		buf[64];
	MemTrackType total_allocs = s_bZeroTotalAllocs ? 0 : tracker->total_allocs;

	filenameWithStructMappingInFixedSizeBuffer(filename, 44, SAFESTR(buf));

	switch(line){
		xcase 0:
			// Ignored.
		xcase LINENUM_FOR_STRUCTS:
			sprintf(numbuf, "%-6s", "STRUCT");
		xcase LINENUM_FOR_STRINGS:
			sprintf(numbuf, "%-6s", "STRING");
		xcase LINENUM_FOR_EARRAYS:
			sprintf(numbuf, "%-6s", "EARRAY");
		xdefault:
			sprintf(numbuf,"%-6d",line);
	}
	total = tracker->bytes;
	if (total > 0)
		total += TRACK_HEAP_OVERHEAD * MAX(0, tracker->num_allocs);

#if MEMTRACK_64BIT
	handlerPrintf(cb,user,"%44.44s:%s %10s%c %7"FORM_LL"d %7"FORM_LL"d\n",buf,numbuf, friendlyLazyBytesBuf(total, sizebuf),shared?'*':' ',tracker->num_allocs,total_allocs);
#else
	handlerPrintf(cb,user,"%44.44s:%s %10s%c %7d %7d\n",buf,numbuf, friendlyLazyBytesBuf(total, sizebuf),shared?'*':' ',tracker->num_allocs,total_allocs);
#endif
}

static void printTotals(OutputHandler cb, void *user,BigAllocTracker *total,BigAllocTracker *shared,BigAllocTracker *video)
{
	MEMORYSTATUSEX memStats = { 0 };
	handlerPrintf(cb,user,"--------------------------------------------------\n");
	memStats.dwLength = sizeof(memStats);
	GlobalMemoryStatusEx(&memStats);
	printBigMemLine(cb,user,total,"TOTAL",0,0);
	if (video && video->bytes)
		printBigMemLine(cb,user,video,"VIDEO",0,0);
	printBigMemLine(cb,user,shared,"SHARED",0,0);
	handlerPrintf(cb,user,"OS reports %6.2fMB, %"FORM_LL"dK virtual free\n", getProcessPageFileUsage() / MILLION, memStats.ullAvailVirtual / 1024);
}

static FileMemInfo *getOrAddFilename(const char *filename, bool add) // Must be called inside of memReportCS
{
	FileMemInfo	*info;

	if (!filename_hashes)
		filename_hashes = stashTableCreateWithStringKeys(50,StashDefault);

	if (stashFindPointer(filename_hashes,filename,&info))
		return info;
	else if (add)
	{
		ModuleMemOperationStats	*stats;

		info = calloc(sizeof(FileMemInfo),1);
		info->stats = stats = calloc(1, sizeof(ModuleMemOperationStats));
		stats->moduleName = filename;
		stats->parentBudget = memBudgetGetByFilename(filename);
		eaPush(&file_mem_infos,info);
		stashAddPointer(filename_hashes,filename,info,0);
		eaPush(&stats->parentBudget->stats, stats);
		return info;
	} else
		return NULL;
}

int updateMemoryBudgets()
{
	int						i,j;
	AllocTracker			tot = { 0 };
	MemoryBudget			*budget;
	ModuleMemOperationStats	*stats;
	static U32				frame_id;
	static int				timer;

	if (!timer)
		timer = timerAlloc();
	else if (timerElapsed(timer) < 1.0)
		return 1;
	timerStart(timer);

	EnterCriticalSection(&memReportCS);

	frame_id++;
	for(i=0;i<memtrack_total;i++)
	{
		AllocTracker	curr = {0};
		FileMemInfo		*info;

		stats = memtrack_names_pooled[memtrack_names[i].pooled_index].budget_stats;
		if (!stats)
		{
			info = getOrAddFilename(memtrack_names_pooled[memtrack_names[i].pooled_index].filename, true);
			stats = memtrack_names_pooled[memtrack_names[i].pooled_index].budget_stats = info->stats;
		}

		budget = stats->parentBudget;
		if (budget->frame_id != frame_id)
		{
			budget->frame_id = frame_id;
			budget->lastTraffic = budget->traffic;
			budget->count = 0;
			budget->current = 0;
			budget->traffic = 0;
			budget->workingSetCount = 0;
			budget->workingSetSize = 0;
		}
		if (stats->frame_id != (frame_id&1))
		{
			stats->frame_id = frame_id&1;
			stats->count = stats->countTraffic = stats->size = stats->sizeTraffic = 0;

			// Only filled in if memTrackUpdateWorkingSet() has been called
			budget->workingSetSize += stats->workingSetSize;
			budget->workingSetCount += stats->workingSetCount;
		}

		for(j=0;j<memtrack_thread_count;j++)
			accumTracker(&curr,&memtrack_counts[j][i]);

		budget->count += curr.num_allocs;
		budget->current += curr.bytes;
		budget->traffic += curr.total_allocs;
		if (curr.num_allocs>0 && curr.bytes>0) // 0-byte allocs show up from RefDict tracking, not actually taking memory/overhead/allocs
			budget->current += curr.num_allocs * TRACK_HEAP_OVERHEAD;

		stats->count+=curr.num_allocs;
		stats->size+=curr.bytes;
		if (curr.num_allocs>0 && curr.bytes>0)
			stats->size += curr.num_allocs * TRACK_HEAP_OVERHEAD;
		stats->countTraffic += curr.total_allocs;
		stats->sizeTraffic = 0; // FIXME
	}
	LeaveCriticalSection(&memReportCS);
	return 1;
}

void memMonitorDisplayStatsInternal(OutputHandler cb, void *user, int maxlines)
{
	int				i,j,num;
	FileMemInfo		*info;
	BigAllocTracker	tot = {0},shared_tot = {0}, video_tot = {0};

	allocAddStringFlushAccountingCache();

	EnterCriticalSection(&memReportCS);
	handlerPrintf(cb,user,"--------------------------------------------------\n");

	for(i=0;i<eaSize(&file_mem_infos);i++)
		ZeroStruct(&file_mem_infos[i]->tracker);
	for(i=0;i<memtrack_total;i++)
	{
		AllocName			*id = &memtrack_names[i];
		AllocNamePooled		*idp = &memtrack_names_pooled[id->pooled_index];
		AllocTracker		curr = {0};

		info = getOrAddFilename(idp->filename, true);
		for(j=0;j<memtrack_thread_count;j++)
			accumTracker(&curr,&memtrack_counts[j][i]);
		accumTracker(&info->tracker,&curr);
		if (id->video_mem)
			accumBigTracker(&video_tot,&curr);
		else
			accumBigTracker(&tot,&curr);
		if (id->shared_mem)
			accumBigTracker(&shared_tot,&curr);
	}
	num = eaSize(&file_mem_infos);
	handlerPrintf(cb,user,"%d files tracked\n",num);
	eaQSort(file_mem_infos,cmpFileMemInfo);
	handlerPrintf(cb,user,"Video Memory:\n");
	for(i=0;i<num;i++)
	{
		ANALYSIS_ASSUME(file_mem_infos);
		info = file_mem_infos[i];
		if (filterVideoMemory(info->stats->moduleName))
			printMemLine(cb,user,&info->tracker,info->stats->moduleName,0,info->stats->bShared);
	}
	handlerPrintf(cb,user,"System Memory:\n");
	for(i=MAX(0,num-maxlines);i<num;i++)
	{
		ANALYSIS_ASSUME(file_mem_infos);
		info = file_mem_infos[i];
		if (!filterVideoMemory(info->stats->moduleName))
			printMemLine(cb,user,&info->tracker,info->stats->moduleName,0,info->stats->bShared);
	}

	printTotals(cb,user,&tot,&shared_tot,&video_tot);

	if (maxlines >= 1000)
		handlerPrintf(cb,user, "heap is %s\n",memTrackValidateHeap() ? "valid" : "CORRUPTED");

	memBudgetDisplay(cb, user, errorGetVerboseLevel());

	LeaveCriticalSection(&memReportCS);
}

AUTO_COMMAND;
void conMemBudgets()
{
	memBudgetDisplay(defaultHandler, NULL, errorGetVerboseLevel());
}

AUTO_COMMAND ACMD_NAME(memMonitorDisplayStats, mmds);
void memMonitorDisplayStats()
{
	memMonitorDisplayStatsInternal(defaultHandler, NULL, 100000);
}

AUTO_COMMAND;
void mmdsShort(void)
{
	memMonitorDisplayStatsInternal(defaultHandler, NULL, 50);
}

void memMonitorPerLineStatsInternal(OutputHandler cb, void *user, const char *search, int maxlines, int skip_user_alloc)
{
	int				i,j,curr_total;
	BigAllocTracker	tot = {0}, shared_tot = {0};
	int				*indices;

	allocAddStringFlushAccountingCache();

	if (!maxlines)
		maxlines = 1000000;
	curr_total = memtrack_total;
	indices = ScratchAlloc(sizeof(indices[0]) * curr_total);
	for(i=0, j=0;i<curr_total;i++)
		if (!filterVideoMemory(memtrack_names_pooled[memtrack_names[i].pooled_index].filename))
			indices[j++] = i;
	curr_total = j;
	qsort(indices,curr_total,sizeof(indices[0]),cmpUsedEntry);

    handlerPrintf(cb,user,"--------------------------------------------------\n");

	for(i=0;i<curr_total;i++)
	{
		AllocName		*id = &memtrack_names[indices[i]];
		AllocNamePooled	*idp = &memtrack_names_pooled[id->pooled_index];
		AllocTracker	curr = {0};

		if(skip_user_alloc && memtrack_names[indices[i]].user_alloc)
			continue;

		if (search && search[0] && !strstri(idp->filename, search))
			continue;

		for(j=0;j<memtrack_thread_count;j++)
			accumTracker(&curr,&memtrack_counts[j][indices[i]]);
		accumBigTracker(&tot,&curr);
		if (id->shared_mem)
			accumBigTracker(&shared_tot,&curr);
		if (curr_total - i < maxlines)
			printMemLine(cb,user,&curr,idp->filename,id->line,id->shared_mem);
	}
	printTotals(cb,user,&tot,&shared_tot,NULL);
	ScratchFree(indices);
}

// Displays the top 50 memory allocations per line
AUTO_COMMAND;
void mmplShort(void)
{
	memMonitorPerLineStatsInternal(defaultHandler, NULL, NULL, 50, 0);
}

// Displays all memory allocations per line, optional search param
AUTO_COMMAND ACMD_NAME(mmpl);
void mmplSearch(const char *search)
{
	memMonitorPerLineStatsInternal(defaultHandler, NULL, search, 100000, 0);
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(mmpl);
void mmpl(void)
{
	memMonitorPerLineStatsInternal(defaultHandler, NULL, NULL, 100000, 0);
}

AUTO_COMMAND;
void mmplNoUser(int count)
{
	memMonitorPerLineStatsInternal(defaultHandler, NULL, NULL, count, 1);
}

void memMonitorInit(void)
{
	static int inited;
	size_t staticSize;
	size_t sharedSize;
	if (inited)
		return;
	inited = true;

	// Call this before we allocate anything.
	staticSize = getProcessPageFileUsage();
	sharedSize = getProcessImageSize();

	InitializeCriticalSection(&memReportCS);
	initMemTrack();

	// Add initial memory usage of program
	{
		if (staticSize) {
#ifdef _XBOX
			memTrackUpdateStatsByName("SystemReserved", 1, xboxGetSystemReservedSize(), MM_ALLOC);
			staticSize -= xboxGetSystemReservedSize();
#endif
			memTrackUpdateStatsByName("StartupSize", 1, staticSize, MM_ALLOC);
		}
	}
}

// Show memory stats
AUTO_COMMAND ACMD_NAME(mmdsXBOX, memMonitorDisplayXMemStats) ACMD_CATEGORY(Profile, Standard);
void memMonitorDisplayXMemStats(void)
{
#if _XBOX
	DM_MEMORY_STATISTICS sysMemStats; 
	sysMemStats.cbSize = sizeof(sysMemStats);

	DmQueryMemoryStatistics(&sysMemStats);
	OutputDebugStringf("TotalPages:            %d\n", sysMemStats.TotalPages);
	OutputDebugStringf("AvailablePages:        %d\n", sysMemStats.AvailablePages);
	OutputDebugStringf("StackPages:            %d\n", sysMemStats.StackPages);
	OutputDebugStringf("VirtualPageTablePages: %d\n", sysMemStats.VirtualPageTablePages);
	OutputDebugStringf("SystemPageTablePages:  %d\n", sysMemStats.SystemPageTablePages);
	OutputDebugStringf("PoolPages:             %d\n", sysMemStats.PoolPages);
	OutputDebugStringf("VirtualMappedPages:    %d\n", sysMemStats.VirtualMappedPages);
	OutputDebugStringf("ImagePages:            %d\n", sysMemStats.ImagePages);
	OutputDebugStringf("FileCachePages:        %d\n", sysMemStats.FileCachePages);
	OutputDebugStringf("ContiguousPages:       %d\n", sysMemStats.ContiguousPages);
	OutputDebugStringf("DebuggerPages:         %d\n", sysMemStats.DebuggerPages);
#endif
}



static void getMemTotals(AllocTracker *total)
{
	int i, j;
	AllocTracker	tot = {0};

	for(i=0;i<memtrack_total;i++)
	{
		AllocName			*id = &memtrack_names[i];
		AllocTracker		curr = {0};

		for(j=0;j<memtrack_thread_count;j++)
			accumTracker(&curr,&memtrack_counts[j][i]);
		if (!id->video_mem)
			accumTracker(&tot,&curr);
	}

	*total = tot;
}

static void getMemTotalsEstimate(AllocTracker *total)
{
	int i;
	AllocTracker	tot = {0};

	// This function erroneously includes video memory on the PC
	for(i=0;i<memtrack_thread_count;i++)
		accumTracker(&tot,&memtrack_counts[i][MEMTRACK_MAXENTRIES]);

	*total = tot;
}

size_t memMonitorGetTotalTrackedMemory(void)
{
	AllocTracker	total;
	getMemTotals(&total);
	return total.bytes + total.num_allocs * TRACK_HEAP_OVERHEAD;
}

size_t memMonitorGetTotalTrackedMemoryEstimate(void)
{
	AllocTracker	total;
	getMemTotalsEstimate(&total);
	return total.bytes + total.num_allocs * TRACK_HEAP_OVERHEAD;
}

static AllocTracker	lastFrameTraffic, lastFrameTotals;

void memMonitorResetFrameCounters(void)
{
	AllocTracker	total;

	PERFINFO_AUTO_START_FUNC();
	
	getMemTotalsEstimate(&total);
	lastFrameTraffic.bytes			= total.bytes - lastFrameTotals.bytes;
	lastFrameTraffic.num_allocs		= total.num_allocs - lastFrameTotals.num_allocs;
	lastFrameTraffic.total_allocs	= total.total_allocs - lastFrameTotals.total_allocs;
	
	PERFINFO_AUTO_STOP();
}

AUTO_RUN;
void memMonitorRegisterDebugWatches(void)
{
	dbgAddIntWatch("memTraffic", lastFrameTraffic.total_allocs);
	dbgAddIntWatch("memAllocs", lastFrameTraffic.num_allocs);
	dbgAddIntWatch("memGrowth", lastFrameTraffic.bytes);
}

char *memory_dump_string;

// See allocEmergencyMemory() for an explanation of these.
static void *emergency_memory_small;  // The values below this are assumed to be set if emergency_memory_small is set.
static void *emergency_memory_large;
static void *emergency_memory_reserved;

#pragma optimize("", off)
void defaultOutOfMemoryCallback(void)
{
	static U32 calling_OutOfMemory;

	// Only run the out-of-memory callback once.
	if (InterlockedIncrement(&calling_OutOfMemory) != 1)
		return;

	if (emergency_memory_small)
	{
		char *tok;
		char *context=NULL;
		char context_char=0;
		char *str;

		// Free emergency memory.
		if (emergency_memory_small != (void *)-1)
		{
			SAFE_FREE(emergency_memory_small);
			SAFE_FREE(emergency_memory_large);
			VirtualFree(emergency_memory_reserved, 0, MEM_RELEASE);
			emergency_memory_reserved = NULL;
		}

		// Print a memory report.
		memMonitorPerLineStatsInternal(estrConcatHandler, &memory_dump_string, NULL, 50, 0);
		//memMonitorDisplayStatsInternal(estrConcatHandler, &memory_dump_string, 50);
		// printf on the xbox doesn't handle large strings well, call it multiple
		// times with smaller strings to make it more likely to show up in the debug window.
		str = memory_dump_string;
		while (tok = strtok_nondestructive(str, "\n", &context, &context_char))
		{
			str = NULL;
			printf("%s\n", tok);
		}
	}
}
#pragma optimize("", on)

// Note, only use this function during crash handling, as it may only be called once.
void includeMemoryReportInCrashDump()
{
	defaultOutOfMemoryCallback();
}

// Reserve memory to be used for essential crash handling activities, such as mmpl.
AUTO_RUN_EARLY;
void allocEmergencyMemory(void)
{
#if !PLATFORM_CONSOLE
	extern CRTMallocFunc g_malloc_func;
	// In DLLs like Gimme, where we pass our allocation off to a different module, don't allocate an emergency buffer
	if (!emergency_memory_small && emergency_memory_small != (void *)-1 && !g_OutOfMemoryCallback && !g_malloc_func)
#else
	if (!emergency_memory_small && !g_OutOfMemoryCallback)
#endif
	{
		memBudgetAddMapping("Slack", BUDGET_Slack);

		// The following memory regions are designed to maximize our chances of having enough memory to do what we need to do, post-crash.
		
		// We allocate a small chunk, which we expect to fit into the segments of the Windows heap, so we are more likely to be able to do
		// small allocations even if we can't do virtual allocations for some reason.
		emergency_memory_small = malloc_timed(512 * 1024, _NORMAL_BLOCK, "Slack", __LINE__); // 512 KiB

		// We allocate a large chunk, which will be a virtual allocation, and provide bulk memory if we need it.
		emergency_memory_large = malloc_timed(1024 * 1024 * 4, _NORMAL_BLOCK, "Slack", __LINE__); // 4 MiB

		// Reserve a memory range, so we can unfragment the memory space enough to get RAM if the problem is just fragmentation.
		// This does not actually allocate any physical RAM, and we don't add it to MemTrack.
		emergency_memory_reserved = VirtualAlloc(NULL, 128*1024*1024, MEM_RESERVE, PAGE_READWRITE); // 128 MiB

		setOutOfMemoryCallback(defaultOutOfMemoryCallback);
	}
}

void freeEmergencyMemory(void)
{
	// make sure it doesn't get allocated later either
	if (emergency_memory_small && emergency_memory_small != (void *)-1)
	{
		free(emergency_memory_small);
	}
	emergency_memory_small = (void *)-1;
}


S32 memMonitorGetVideoMemUseEstimate(void)
{
	int				i, j;
	AllocTracker	video_tot = {0};

	for(i=0;i<memtrack_total;i++)
	{
		const AllocName			*id = &memtrack_names[i];

		if (id->video_mem)
		{
			AllocTracker		curr = {0};
			for(j=0;j<memtrack_thread_count;j++)
				accumTracker(&curr,&memtrack_counts[j][i]);
			accumTracker(&video_tot,&curr);
		}
	}
	return video_tot.bytes;
}

S32 memMonitorPhysicalUntracked(void)
{
	int				i,j;
	BigAllocTracker	tot = {0},shared_tot = {0}, video_tot = {0};

	for(i=0;i<memtrack_total;i++)
	{
		AllocName			*id = &memtrack_names[i];
		AllocTracker		curr = {0};

		for(j=0;j<memtrack_thread_count;j++)
			accumTracker(&curr,&memtrack_counts[j][i]);
		if (id->video_mem)
			accumBigTracker(&video_tot,&curr);
		else
			accumBigTracker(&tot,&curr);
		if (id->shared_mem)
			accumBigTracker(&shared_tot,&curr);
	}

	{
		MemTrackType unaccounted;
		size_t total;
		S64 overhead;
#ifdef _XBOX
		U32 maxphys, availphys;
		getPhysicalMemory(&maxphys, &availphys);
		total = maxphys - availphys;
#else
		total = getProcessPageFileUsage();
#endif
		overhead = ((S64)tot.num_allocs) * TRACK_HEAP_OVERHEAD;
		unaccounted = ((S64)total) - tot.bytes - overhead;
		OutputDebugStringf("Tot: %d   untracked: %d   count: %d   overhead: %"FORM_LL"d   video: %"FORM_LL"d   untracked %%:%1.2f\n",
			total, unaccounted, tot.num_allocs, overhead, video_tot.bytes, 100.f*unaccounted/(F32)(total));
		return unaccounted;
	}
}




typedef struct PageUser PageUser;
typedef struct PageUser
{
	PageUser *next_per_page;
	PageUser *next_per_user;
	int name_idx;
	int count; // Count of allocations from this user in this page
	size_t size; // Size of all allocations from this user in this page
	size_t VirtualPage;
};
typedef struct PerVirtualPage
{
	PageUser *users; // Linked list of people referencing this page
	size_t VirtualPage;
	bool inWorkingSet;
} PerVirtualPage;
typedef struct UserName
{
	PageUser *pages; // Linked list of pages this user references
	const char *filename;
	int linenum;
	U64 countPagesTouched;
	U64 countAllocs;
	size_t size;
	U64 totalCount; // Including non-working-set
	size_t totalSize; // Including non-working-set
} UserName;
typedef struct WorkingSetData
{
	LinearAllocator *allocator;
	size_t page_size;
	PerVirtualPage *per_page_list; // sorted list
	size_t num_pages;
	UserName users[MEMTRACK_MAXENTRIES];
	int num_users;
	int max_name_idx;
	U64 totalWorkingSetCount;
	size_t totalWorkingSetSize;
	U64 totalWorkingSetPages;
	U64 totalWorkingSetTrackedPages;
	U64 totalCountAllocs; // Total including *non*-working set allocs
	size_t totalCountSize; // Total including *non*-working set allocs
	U32 totalCountModules; // Total size of all moduels investigated including non-working set memory
	size_t totalCountModulesSize; // Total size of all moduels investigated including non-working set memory
} WorkingSetData;

static WorkingSetData *g_workingset_data;

static int cmpUserName(const void *a, const void *b)
{
	const UserName *ua = (const UserName*)a;
	const UserName *ub = (const UserName*)b;
	if (ua->size != ub->size)
		return (ua->size>ub->size)?-1:1;
	if (ua->countAllocs != ub->countAllocs)
		return (ua->countAllocs>ub->countAllocs)?-1:1;
	if (ua->filename != ub->filename)
		return stricmp(ua->filename, ub->filename);
	if (ua->linenum != ub->linenum)
		return (ua->linenum > ub->linenum)?-1:1;
	return 0;
}

static int cmpPerVirtualPage(const void *a, const void *b)
{
	const PerVirtualPage *pa = (const PerVirtualPage*)a;
	const PerVirtualPage *pb = (const PerVirtualPage*)b;
	if (pa->VirtualPage > pb->VirtualPage)
		return 1;
	return -1;
}

static PerVirtualPage *workingSetFindPage(size_t pagenum)
{
	size_t mn = 0;
	size_t mx = g_workingset_data->num_pages-1;
	size_t md = mx / 2;
	if (!g_workingset_data->num_pages)
		return NULL;
	if (pagenum < g_workingset_data->per_page_list[0].VirtualPage ||
		pagenum > g_workingset_data->per_page_list[mx].VirtualPage)
	{
		return NULL;
	}
	do {
		if (g_workingset_data->per_page_list[md].VirtualPage == pagenum)
			return &g_workingset_data->per_page_list[md];
		if (g_workingset_data->per_page_list[md].VirtualPage > pagenum)
		{
			mx = md-1;
			md = (mx - mn)/2 + mn;
		} else {
			mn = md+1;
			md = (mx - mn)/2 + mn;
		}
	} while (mx >= mn);
	return NULL;
}

static void workingSetMapRange(void *userData, void *mem, size_t size, const char *filename, int linenum, int name_idx)
{
	UserName *user;
	uintptr_t i;
	uintptr_t firstPage, lastPage;
	bool bCounted=false;
	assert(name_idx < ARRAY_SIZE(g_workingset_data->users));
	user = &g_workingset_data->users[name_idx];
	if (!user->filename)
	{
		user->filename = filename;
		user->linenum = linenum;
	} else {
		assert(user->filename == filename);
		assert(user->linenum == linenum);
	}

	user->totalCount++;
	user->totalSize+=size;

	MAX1(g_workingset_data->max_name_idx, name_idx);

	// update data structures, fun!
	firstPage = ((uintptr_t)mem)/g_workingset_data->page_size;
	lastPage = ((uintptr_t)mem+size-1)/g_workingset_data->page_size;
	for (i=firstPage; i<=lastPage; i++)
	{
		size_t thisSize;
		PerVirtualPage *p;
		if (i == firstPage)
		{
			if (i == lastPage)
			{
				thisSize = size;
			} else {
				thisSize = g_workingset_data->page_size - ((uintptr_t)mem - firstPage * g_workingset_data->page_size);
			}
		} else if (i == lastPage)
		{
			thisSize = (uintptr_t)mem + size - lastPage * g_workingset_data->page_size;
		} else {
			thisSize= g_workingset_data->page_size;
		}

		//PERFINFO_AUTO_START("workingSetFindPage", 1);
		p = workingSetFindPage(i);// &g_workingset_data->per_page[i];
		//PERFINFO_AUTO_STOP();
		if (!p || !p->inWorkingSet)
		{
			// This page is not in the working set, we are not collecting information on it
			continue;
		} else {
			PageUser *pu = p->users;
			while (pu)
			{
				if (pu->name_idx == name_idx)
					break;
				pu = pu->next_per_page;
			}
			if (pu) {
				// Already recorded
				pu->count++;
				pu->size += thisSize;
			} else {
				// Need to record
				pu = linAlloc(g_workingset_data->allocator, sizeof(PageUser));
				pu->count = 1;
				pu->name_idx = name_idx;
				pu->size = thisSize;
				pu->VirtualPage = i;
				// Add to linked lists
				pu->next_per_page = p->users;
				p->users = pu;
				pu->next_per_user = user->pages;
				user->pages = pu;
				user->countPagesTouched++;
			}
			if (!bCounted)
			{
				user->countAllocs++;
				bCounted=true;
			}
			user->size += thisSize;
		}
	}
}


#if !PLATFORM_CONSOLE
#if _MSC_VER < 1600 // VS10 includes this
typedef union _PSAPI_WORKING_SET_BLOCK {
	ULONG_PTR Flags;
	struct {
		ULONG_PTR Protection  :5;
		ULONG_PTR ShareCount  :3;
		ULONG_PTR Shared  :1;
		ULONG_PTR Reserved  :3;
#ifdef _M_X64
		ULONG_PTR VirtualPage  :52;
#else
		ULONG_PTR VirtualPage  :20;
#endif
	} ;
}PSAPI_WORKING_SET_BLOCK, *PPSAPI_WORKING_SET_BLOCK;

typedef struct _PSAPI_WORKING_SET_INFORMATION {
	ULONG_PTR               NumberOfEntries;
	PSAPI_WORKING_SET_BLOCK WorkingSetInfo[1];
}PSAPI_WORKING_SET_INFORMATION, *PPSAPI_WORKING_SET_INFORMATION;
#endif

static void updateWorkingSetCallback(void *userData, void *mem, size_t size, const char *filename, int linenum, int name_idx, ModuleMemOperationStats *stats)
{
	//PERFINFO_AUTO_START_FUNC();
	MAX1(g_workingset_data->max_name_idx, name_idx);
	// Add in overhead
	size += TRACK_HEAP_OVERHEAD;
	mem = (char*)mem - (TRACK_HEAP_OVERHEAD-4);
	g_workingset_data->totalCountAllocs++;
	g_workingset_data->totalCountSize += size;
	workingSetMapRange(userData, mem, size, filename, linenum, name_idx);
	//PERFINFO_AUTO_STOP();
}

void memTrackUpdateWorkingSet(void)
{
	int retries;
	// Get the working set
	PSAPI_WORKING_SET_INFORMATION wsinfo_query = {0};
	static PSAPI_WORKING_SET_INFORMATION *wsinfo = NULL; // Keeping this around so successive queries don't jump around in the working set
	static DWORD wsinfo_size = 0;
	BOOL bRet = QueryWorkingSet(GetCurrentProcess(), &wsinfo_query, sizeof(wsinfo_query));
	assert(!bRet); // Should "fail" the first time, just return the size

	PERFINFO_AUTO_START_FUNC();

	EnterCriticalSection(&memReportCS);

	PERFINFO_AUTO_START("QueryWorkingSet", 1);
	do 
	{
		size_t stcb = sizeof(PSAPI_WORKING_SET_BLOCK) * wsinfo_query.NumberOfEntries + sizeof(PSAPI_WORKING_SET_INFORMATION);
		DWORD cb = (DWORD)stcb;
		assert(cb == stcb);
		if (cb > wsinfo_size)
		{
			// Add room for an extra 1000 pages (the size this allocation might be and add room to grow)
			stcb = sizeof(PSAPI_WORKING_SET_BLOCK) * (wsinfo_query.NumberOfEntries + 1000) + sizeof(PSAPI_WORKING_SET_INFORMATION);
			cb = (DWORD)stcb;
			assert(cb == stcb);
			wsinfo = realloc(wsinfo, cb);
			wsinfo_size = cb;
		}
		assert(wsinfo);
		bRet = QueryWorkingSet(GetCurrentProcess(), wsinfo, wsinfo_size);
		if (!bRet)
		{
			PERFINFO_AUTO_STOP_START("Retry", 1);
			wsinfo_query.NumberOfEntries = wsinfo->NumberOfEntries;
		}
	} while (!bRet);

	PERFINFO_AUTO_STOP_START("Setup", 1);

	// Set up workingset data
	if (!g_workingset_data)
	{
		g_workingset_data = callocStruct(WorkingSetData);
		g_workingset_data->allocator = linAllocCreate(1024*1024, true);
	} else {
		LinearAllocator *la = g_workingset_data->allocator;
		linAllocClear(la);
		ZeroStruct(g_workingset_data);
		g_workingset_data->allocator = la;
	}

	{
		SYSTEM_INFO sysinfo = {0};
		unsigned int i;
		size_t max_virtual_page=0;
		size_t stalloc_size;
		U32 alloc_size;
		GetSystemInfo(&sysinfo);
		g_workingset_data->page_size = sysinfo.dwPageSize;
		g_workingset_data->num_pages = wsinfo->NumberOfEntries;
		stalloc_size = g_workingset_data->num_pages * sizeof(g_workingset_data->per_page_list[0]);
		alloc_size = (U32)stalloc_size;
		if (alloc_size != stalloc_size || stalloc_size > (1<<31))
		{
			// Too big for linear allocator, allocate it from the heap
			static void *static_per_page_list=NULL;
			static_per_page_list = realloc(static_per_page_list, stalloc_size);
			g_workingset_data->per_page_list = static_per_page_list;
		} else {
			g_workingset_data->per_page_list = linAlloc(g_workingset_data->allocator, alloc_size);
		}
		PERFINFO_AUTO_STOP_START("Fill", 1);
		for (i=0; i<wsinfo->NumberOfEntries; i++)
		{
			g_workingset_data->per_page_list[i].VirtualPage = wsinfo->WorkingSetInfo[i].VirtualPage;
			g_workingset_data->per_page_list[i].inWorkingSet = true;
		}
		PERFINFO_AUTO_STOP_START("Sort", 1);
		qsort(g_workingset_data->per_page_list, g_workingset_data->num_pages, sizeof(g_workingset_data->per_page_list[0]), cmpPerVirtualPage);
	}

	PERFINFO_AUTO_STOP_START("HeapWalk", 1);
	// Enumerate allocs and categorize
	retries = 5;
	do {
		bRet = memTrackForEachAlloc(NULL, updateWorkingSetCallback, NULL, 0, true, 0);
		if (bRet)
			break;
		PERFINFO_AUTO_STOP_START("Retry", 1);
		retries--;
	} while (retries > 0);
	if (bRet)
	{
		int i;
		HMODULE hMods[1024];
		DWORD cbNeeded;

		PERFINFO_AUTO_STOP_START("EnumProcessModules", 1);
		// Enumerate modules and categorize
		if( EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded))
		{
			for ( i = 0; i < (int)(cbNeeded / sizeof(HMODULE)); i++ )
			{
				char *pModName = NULL;
				estrStackCreate(&pModName);

				if ( GetModuleFileNameEx_UTF8(GetCurrentProcess(), hMods[i], &pModName))
				{
					MODULEINFO mod_info = {0};
					if (GetModuleInformation(GetCurrentProcess(), hMods[i], &mod_info, sizeof(mod_info)))
					{
						const char *modName = allocAddCaseSensitiveString(pModName);
						workingSetMapRange(NULL, mod_info.lpBaseOfDll, mod_info.SizeOfImage, modName, 0, ++g_workingset_data->max_name_idx);
						g_workingset_data->totalCountModules++;
						g_workingset_data->totalCountModulesSize += mod_info.SizeOfImage;
					}
				}

				estrDestroy(&pModName);
			}
		}

		// Clear budget data

		PERFINFO_AUTO_STOP_START("Clear Budgets", 1);
		for(i=0;i<memtrack_total;i++)
		{
			AllocTracker	curr = {0};
			FileMemInfo		*info;
			ModuleMemOperationStats *stats;

			stats = memtrack_names_pooled[memtrack_names[i].pooled_index].budget_stats;
			if (!stats)
			{
				info = getOrAddFilename(memtrack_names_pooled[memtrack_names[i].pooled_index].filename, true);
				stats = memtrack_names_pooled[memtrack_names[i].pooled_index].budget_stats = info->stats;
			}
			stats->workingSetCount = 0;
			stats->workingSetSize = 0;
		}

		PERFINFO_AUTO_STOP_START("Report", 1);
		// Analyze and display results
		for (i=0; i<(int)g_workingset_data->num_pages; i++)
			if (g_workingset_data->per_page_list[i].users)
				g_workingset_data->totalWorkingSetTrackedPages++;
		g_workingset_data->num_users = 0;
		g_workingset_data->totalWorkingSetSize = 0;
		g_workingset_data->totalWorkingSetCount = 0;
		g_workingset_data->totalWorkingSetPages = wsinfo->NumberOfEntries;
		for (i=0; i<ARRAY_SIZE(g_workingset_data->users); i++)
		{
			FileMemInfo		*info;
			UserName *user = &g_workingset_data->users[i];
			if (!user->countAllocs)
				continue;
			g_workingset_data->totalWorkingSetCount += user->countAllocs;
			g_workingset_data->totalWorkingSetSize += user->size;
			info = getOrAddFilename(user->filename, false);
			if (!info && (strEndsWith(user->filename, ".exe") || 
				strEndsWith(user->filename, ".dll")))
			{
				info = getOrAddFilename("StartupSize", false);
			}
			if (info)
			{
				ModuleMemOperationStats *stats = info->stats;
				if (stats)
				{
					stats->workingSetSize += user->size;
					stats->workingSetCount += user->countAllocs;
				}
			}
			if (i!=g_workingset_data->num_users)
			{
				g_workingset_data->users[g_workingset_data->num_users] = g_workingset_data->users[i];
			}
			g_workingset_data->num_users++;
		}
	} else {
		printf("Walking heap FAILED.");
	}

	PERFINFO_AUTO_STOP_START("EmptyWorkingSet", 1);
	// Reset working set
	EmptyWorkingSet(GetCurrentProcess());
	PERFINFO_AUTO_STOP();

	LeaveCriticalSection(&memReportCS);
	PERFINFO_AUTO_STOP();
}

static void dumpHeapCallback(void *userData, void *mem, size_t size, const char *filename, int linenum, int name_idx, ModuleMemOperationStats *stats)
{
	FILE *f = (FILE*)userData;
	fprintf(f, "%p, %8Id, \"%s\", %d\n", mem, size, filename, linenum);
}

// Dumps the main Cryptic heap to c:\memlog.csv
AUTO_COMMAND;
void memTrackDumpHeap(void)
{
	int retries;
	bool bRet=false;
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&memReportCS);

	retries = 5;
	do {
		FILE *f = fopen("C:/memlog.csv", "w");
		if (!f)
		{
			printf("Failed to open c:/memlog.csv.\n");
			break;
		}
		bRet = memTrackForEachAlloc(NULL, dumpHeapCallback, f, 0, true, 0);
		fclose(f);
		if (bRet)
			break;
		retries--;
	} while (retries > 0);
	if (bRet)
	{
		// success
	} else {
		printf("Walking heap FAILED.\n");
	}

	LeaveCriticalSection(&memReportCS);
	PERFINFO_AUTO_STOP();
}


#else
void memTrackUpdateWorkingSet(void)
{
}
#endif

static void memTrackPrintWorkingSetInternal(int numlines, bool update, const char *search)
{
	char sizebuf[64];
	char sizebuf2[64];
	int i;
	EnterCriticalSection(&memReportCS);
	if (update)
		memTrackUpdateWorkingSet();

	qsort(g_workingset_data->users, g_workingset_data->num_users, sizeof(g_workingset_data->users[0]), cmpUserName);
	printf("--------------------------------------------------\n");
	for (i=MIN(numlines, g_workingset_data->num_users-1); i>=0; i--)
	{
		char		numbuf[20] = "      ";
		char		buf[64];
		UserName *user = &g_workingset_data->users[i];
		if (search && !strstri(user->filename, search))
			continue;
		filenameWithStructMappingInFixedSizeBuffer(user->filename, 44, SAFESTR(buf));
		if (user->linenum)
			sprintf(numbuf,"%-6d",user->linenum);
		printf("%44.44s:%s %10s %7"FORM_LL"d  of %10s %7"FORM_LL"d\n",buf, numbuf,
			friendlyLazyBytesBuf(user->size, sizebuf), user->countAllocs,
			friendlyLazyBytesBuf(user->totalSize, sizebuf2), user->totalCount);
	}
	printf("--------------------------------------------------\n");
	printf("        Total %s tracked in %"FORM_LL"d allocs touching %"FORM_LL"d pages\n",
		friendlyLazyBytesBuf(g_workingset_data->totalWorkingSetSize, sizebuf),
		g_workingset_data->totalWorkingSetCount,
		g_workingset_data->totalWorkingSetTrackedPages);
	printf("        Process total %s in %"FORM_LL"d pages\n",
		friendlyLazyBytesBuf(g_workingset_data->totalWorkingSetPages*g_workingset_data->page_size, sizebuf),
		g_workingset_data->totalWorkingSetPages);
	printf("        Examined %s in %"FORM_LL"d allocs and %s in %d modules\n",
		friendlyLazyBytesBuf(g_workingset_data->totalCountSize, sizebuf),
		g_workingset_data->totalCountAllocs,
		friendlyLazyBytesBuf(g_workingset_data->totalCountModulesSize, sizebuf2),
		g_workingset_data->totalCountModules);

	LeaveCriticalSection(&memReportCS);
}

// Prints a summary of the working set of each module since the last query
AUTO_COMMAND ACMD_NAME(memTrackPrintWorkingSet, mmpws);
void memTrackPrintWorkingSetSearch(const char *search)
{
	memTrackPrintWorkingSetInternal(50000, true, search);
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(memTrackPrintWorkingSet);
void memTrackPrintWorkingSet(void)
{
	memTrackPrintWorkingSetInternal(50000, true, NULL);
}

// Prints a short summary of the working set of each module since the last query
AUTO_COMMAND ACMD_NAME(memTrackPrintWorkingSetShort, mmpwsShort);
void memTrackPrintWorkingSetShort(void)
{
	memTrackPrintWorkingSetInternal(50, true, NULL);
}

// Prints a summary of the working set of each module without updating
AUTO_COMMAND ACMD_NAME(memTrackPrintWorkingSetNoUpdate, mmpwsnu);
void memTrackPrintWorkingSetNoUpdateSearch(const char *search)
{
	memTrackPrintWorkingSetInternal(50000, false, search);
}
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(memTrackPrintWorkingSetNoUpdate);
void memTrackPrintWorkingSetNoUpdate(void)
{
	memTrackPrintWorkingSetInternal(50000, false, NULL);
}

// Prints a short summary of the working set of each module without updating
AUTO_COMMAND ACMD_NAME(memTrackPrintWorkingSetNoUpdateShort, mmpwsnuShort);
void memTrackPrintWorkingSetNoUpdateShort(void)
{
	memTrackPrintWorkingSetInternal(50, false, NULL);
}

AUTO_COMMAND ACMD_NAME(memTrackAddFakeMemoryUsage);
void memTrackAddFakeMemoryUsage(S64 amount)
{
	memTrackUpdateStatsByName("FakeMemoryUsage", 1, amount, 1);
}




int some_global_that_the_compiler_thinks_might_be_non_zero=0;


// junkyard
	// we ensure it by crashing if you do this now
void ensureNoMemoryAllocations(void) {}

/* TODO

find out the 25000 malloc(0) source

*/
