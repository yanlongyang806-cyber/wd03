#include "systemspecs.h"
#include "winutil.h"
#include "unitspec.h"
#include "utils.h"
#include "VirtualMemory.h"
#include "memlog.h"

VirtualMemStats g_vmStats;

#define MAX_VM_REGIONS 16384

struct {
	struct {
		size_t lo;
		size_t hi;
		char* flag;
	} regions[MAX_VM_REGIONS];
	
	U32		regionCount;
	char*	bitNames[2][32];
} virtualMemory;

static int getBitNumber(U32 x){
	int i;
	
	for(i = 0; x & ~1; x >>= 1, i++);
	
	return i;
}

int getVirtualMemoryRegion(void* mem)
{
	U32 i;
	for(i = 0; i < virtualMemory.regionCount; i++)
	{
		if((uintptr_t)mem >= virtualMemory.regions[i].lo && (uintptr_t)mem < virtualMemory.regions[i].hi)
		{
			return i;
		}
	}
	return -1;
}

void incorporateMemBlockStats(VirtualMemStats * stats, const MEMORY_BASIC_INFORMATION  * mbi, uintptr_t mask)
{
#if !PLATFORM_CONSOLE
	if (mbi->Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
	{
		stats->executableMem += mbi->RegionSize;
		++stats->executableCount;
	}

	if(mbi->State == MEM_RESERVE)
	{
		stats->reservedMem += mbi->RegionSize;
		++stats->reservedCount;
	}
	else
	if(mbi->State == MEM_FREE)
	{
		PVOID allocationBase = mbi->BaseAddress;
		intptr_t allocationSize = mbi->RegionSize;
		if ((uintptr_t)allocationBase & mask) {
			intptr_t diff;
			allocationBase = (PVOID)((uintptr_t)((char*)allocationBase + mask) & ~mask);
			diff = (char*)allocationBase - (char*)mbi->BaseAddress;
			if (diff > allocationSize)
				diff = allocationSize;
			allocationSize -= diff;
			stats->wastedCount++;
			stats->wastedMem+=diff;
		}

		if (allocationSize > 0)
		{
			int i;

			stats->freecount++;
			stats->unmappedMem += mbi->RegionSize;
			stats->largestUnmappedBlocks[0] = mbi->RegionSize;
			for (i=0; i<ARRAY_SIZE(stats->largestUnmappedBlocks)-1; i++) 
				if (stats->largestUnmappedBlocks[i] > stats->largestUnmappedBlocks[i+1]) {
					U64 t = stats->largestUnmappedBlocks[i];
					stats->largestUnmappedBlocks[i] = stats->largestUnmappedBlocks[i+1];
					stats->largestUnmappedBlocks[i+1] = t;
				}
		} else {
		}
	} else {
		stats->mappedMem += mbi->RegionSize;
		++stats->mappedCount;
	}
#endif
}

void generateVirtualMemoryLayout()
{
#if !PLATFORM_CONSOLE
	#define FLAG_MASK (	PAGE_EXECUTE|\
						PAGE_EXECUTE_READ|\
						PAGE_EXECUTE_READWRITE|\
						PAGE_EXECUTE_WRITECOPY|\
						PAGE_NOACCESS|\
						PAGE_READONLY|\
						PAGE_READWRITE|\
						PAGE_WRITECOPY)

	SYSTEM_INFO sysinfo;
	char* lastAddress = (char*)0;
	char* curAddress = (char*)0x10016;  // Everything below this value is off limits.
	int	sum=0;
	uintptr_t mask;
		
	virtualMemory.regionCount = 0;
	
	memset(virtualMemory.bitNames, 0, sizeof(virtualMemory.bitNames));
	
	#define SET_NAME(x) virtualMemory.bitNames[0][getBitNumber(x)] = #x
	SET_NAME(PAGE_EXECUTE);
	SET_NAME(PAGE_EXECUTE_READ);
	SET_NAME(PAGE_EXECUTE_READWRITE);
	SET_NAME(PAGE_EXECUTE_WRITECOPY);
	SET_NAME(PAGE_NOACCESS);
	SET_NAME(PAGE_READONLY);
	SET_NAME(PAGE_READWRITE);
	SET_NAME(PAGE_WRITECOPY);
	SET_NAME(PAGE_GUARD);
	SET_NAME(PAGE_NOCACHE);
	#undef SET_NAME

	#define SET_NAME(x) virtualMemory.bitNames[1][getBitNumber(x)] = "guard|"#x
	SET_NAME(PAGE_EXECUTE);
	SET_NAME(PAGE_EXECUTE_READ);
	SET_NAME(PAGE_EXECUTE_READWRITE);
	SET_NAME(PAGE_EXECUTE_WRITECOPY);
	SET_NAME(PAGE_NOACCESS);
	SET_NAME(PAGE_READONLY);
	SET_NAME(PAGE_READWRITE);
	SET_NAME(PAGE_WRITECOPY);
	SET_NAME(PAGE_GUARD);
	SET_NAME(PAGE_NOCACHE);
	#undef SET_NAME

	GetSystemInfo(&sysinfo);
	mask = sysinfo.dwAllocationGranularity - 1;
	memset(&g_vmStats, 0, sizeof(g_vmStats));

	while(1)
	{
		MEMORY_BASIC_INFORMATION mbi;
		
		VirtualQuery(curAddress, &mbi, sizeof(mbi));
		
		if(mbi.BaseAddress == lastAddress)
		{
			break;
		}
		
		lastAddress = mbi.BaseAddress;
		
		incorporateMemBlockStats(&g_vmStats, &mbi, mask);

		if (virtualMemory.regionCount < ARRAY_SIZE(virtualMemory.regions))
		{
			if(mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS))
			{
				virtualMemory.regions[virtualMemory.regionCount].lo = (uintptr_t)mbi.BaseAddress;
				virtualMemory.regions[virtualMemory.regionCount].hi = virtualMemory.regions[virtualMemory.regionCount].lo + mbi.RegionSize;
				virtualMemory.regions[virtualMemory.regionCount].flag = virtualMemory.bitNames[(mbi.Protect & PAGE_GUARD) ? 1 : 0][getBitNumber(mbi.Protect & FLAG_MASK)];
				
				if(++virtualMemory.regionCount < ARRAY_SIZE(virtualMemory.regions))
					sum += mbi.RegionSize;
			}
		}
				
		curAddress += mbi.RegionSize;
	}
#endif
}

void virtualMemoryAnalyzeStats(VirtualMemStats *vm_stats)
{
#if !PLATFORM_CONSOLE
	SYSTEM_INFO sysinfo;
	char* lastAddress = (char*)-1;
	char* curAddress = (char*)0;
	VirtualMemStats stats = {0};

	uintptr_t mask;
	GetSystemInfo(&sysinfo);
	mask = sysinfo.dwAllocationGranularity - 1;

	while(1)
	{
		MEMORY_BASIC_INFORMATION mbi;

		VirtualQuery(curAddress, &mbi, sizeof(mbi));

		if(mbi.BaseAddress == lastAddress)
		{
			break;
		}

		lastAddress = mbi.BaseAddress;

		incorporateMemBlockStats(&stats, &mbi, mask);

		curAddress += mbi.RegionSize;
	}

	*vm_stats = stats;
#endif
}

void virtualMemoryUpdateDebugStats()
{
	virtualMemoryAnalyzeStats(&g_vmStats);
}

void virtualMemoryMakeStatsString(const VirtualMemStats *vm_stats, char *strStats, size_t strStats_size)
{
	int i;
	sprintf_s(SAFESTR2(strStats), "  Mapped memory:          %10"FORM_LL"d (%s) in %d blocks\n", vm_stats->mappedMem, friendlyBytes(vm_stats->mappedMem), vm_stats->mappedCount);
	strcatf_s(SAFESTR2(strStats), "  Unmapped (free) memory: %10"FORM_LL"d (%s) in %d blocks\n", vm_stats->unmappedMem, friendlyBytes(vm_stats->unmappedMem), vm_stats->freecount);
	strcatf_s(SAFESTR2(strStats), "  Free between allocs:    %10"FORM_LL"d (%s) in %d blocks\n", vm_stats->wastedMem, friendlyBytes(vm_stats->wastedMem), vm_stats->wastedCount);
	strcatf_s(SAFESTR2(strStats), "  Executable:             %10"FORM_LL"d (%s) in %d blocks\n", vm_stats->executableMem, friendlyBytes(vm_stats->executableMem), vm_stats->executableCount);
	strcatf_s(SAFESTR2(strStats), "  Reserved:               %10"FORM_LL"d (%s) in %d blocks\n", vm_stats->reservedMem, friendlyBytes(vm_stats->reservedMem), vm_stats->reservedCount);
	strcatf_s(SAFESTR2(strStats), "  Largest free:\n");
	for (i=1; i<ARRAY_SIZE(vm_stats->largestUnmappedBlocks); i++)
		strcatf_s(SAFESTR2(strStats), "    %10"FORM_LL"d (%s)\n", vm_stats->largestUnmappedBlocks[i], friendlyBytes(vm_stats->largestUnmappedBlocks[i]));
}

void virtualMemoryMakeShortStatsString(const VirtualMemStats *vm_stats, char *strStats, size_t strStats_size)
{
	int i;
	sprintf_s(SAFESTR2(strStats), "Used\t%10"FORM_LL"d\t%s\t%d blocks\n", vm_stats->mappedMem, friendlyBytes(vm_stats->mappedMem), vm_stats->mappedCount);
	strcatf_s(SAFESTR2(strStats), "Free\t%10"FORM_LL"d\t%s\t%d\n", vm_stats->unmappedMem, friendlyBytes(vm_stats->unmappedMem), vm_stats->freecount);
	strcatf_s(SAFESTR2(strStats), "Wast\t%10"FORM_LL"d\t%s\t%d\n", vm_stats->wastedMem, friendlyBytes(vm_stats->wastedMem), vm_stats->wastedCount);
	strcatf_s(SAFESTR2(strStats), "EXE\t%10"FORM_LL"d\t%s\t%d\n", vm_stats->executableMem, friendlyBytes(vm_stats->executableMem), vm_stats->executableCount);
	strcatf_s(SAFESTR2(strStats), "Resrv\t%10"FORM_LL"d\t%s\t%d\n", vm_stats->reservedMem, friendlyBytes(vm_stats->reservedMem), vm_stats->reservedCount);
	strcatf_s(SAFESTR2(strStats), "Top free\t");
	for (i=ARRAY_SIZE(vm_stats->largestUnmappedBlocks)-1; i>=1; --i)
		strcatf_s(SAFESTR2(strStats), "\t%10"FORM_LL"d %s\t", vm_stats->largestUnmappedBlocks[i], friendlyBytes(vm_stats->largestUnmappedBlocks[i]));
}

void virtualMemoryMemlogStats(MemLog *destMemLog, const char *strMarker, VirtualMemStats *vm_stats)
{
	char strStats[VIRTUAL_MEMORY_STATS_STRING_MAX_LENGTH];
	virtualMemoryMakeShortStatsString(vm_stats, SAFESTR(strStats));
	memlog_printf(destMemLog, "VM stats: \"%s\"\n %s", strMarker, strStats);
}

void virtualMemoryAnalyzeAndMemlogStats(MemLog *destMemLog, const char *strMarker, VirtualMemStats *vm_stats)
{
	VirtualMemStats tempStats;
	VirtualMemStats *destStats = vm_stats ? vm_stats : &tempStats;

	virtualMemoryAnalyzeStats(destStats);
	virtualMemoryMemlogStats(destMemLog, strMarker, destStats);
}

// Display some stats about virtual memory usage to the Win32 console
AUTO_COMMAND ACMD_CATEGORY(Debug);
void virtualMemoryAnalyze()
{
#if !PLATFORM_CONSOLE
	VirtualMemStats vm_stats = {0};
	char strStats[VIRTUAL_MEMORY_STATS_STRING_MAX_LENGTH];

	virtualMemoryAnalyzeStats(&vm_stats);
	virtualMemoryMakeStatsString(&vm_stats, SAFESTR(strStats));
	printf("Virtual memory analysis:\n");
	printf("%s", strStats);
#endif
}

