#ifdef WIN32

#include "cpu_count.h"

#include "cmdParse.h"

#ifndef _M_X64
void cpuid32(CPUID_ARGS* p) {
	__asm {
		mov	edi, p
			mov eax, [edi].eax
			mov ecx, [edi].ecx // for functions such as eax=4
			cpuid
			mov [edi].eax, eax
			mov [edi].ebx, ebx
			mov [edi].ecx, ecx
			mov [edi].edx, edx
	}
}
#endif

#define HT_BIT        0x10000000  // EDX[28] - Bit 28 set indicates 
						// Hyper-Threading Technology is supported 
						// in hardware.
#define FAMILY_ID     0x00000f00      // EAX[11:8] - Bit 11 thru 8 contains family 
						// processor id
#define EXT_FAMILY_ID 0x0f00000 // EAX[23:20] - Bit 23 thru 20 contains 
						// extended family  processor id
#define EXT_MODEL_ID  0x00f0000 // EAX[19:16] - Extended model ID
#define MODEL_ID      0x00000f0 // EAX[7:4]
#define PENTIUM4_ID   0x0f00      // Pentium 4 family processor id
// Returns non-zero if Hyper-Threading Technology is supported on 
// the processors and zero if not.  This does not mean that 
// Hyper-Threading Technology is necessarily enabled.
unsigned int HTSupported(void)
{
	int CPUInfo[4];
	int vendor_id[4] = {0}; 
	__try {            // verify cpuid instruction is supported
		__cpuid(vendor_id,0); // vendor id string (only check HT on Intel)
		__cpuid(CPUInfo, 1); // capabilities		
	}
#pragma warning(suppress:6320)		//Exception-filter is the constant...
	__except (EXCEPTION_EXECUTE_HANDLER ) {
		return 0;   // CPUID is not supported and so Hyper-Threading Technology
		// is not supported
	}
	
	//  Check to see if this is a Pentium 4 or later processor
	if (((CPUInfo[0] & FAMILY_ID) ==  PENTIUM4_ID) || (CPUInfo[0] & EXT_FAMILY_ID) || (CPUInfo[0] & EXT_MODEL_ID)) // i7s have family ID of 6 (Pentium Pro, etc), but have an extended model ID set
		if (vendor_id[1] == 'uneG') 
			if (vendor_id[3] == 'Ieni')
				if (vendor_id[2] == 'letn')
					return (CPUInfo[3] & HT_BIT);  // Hyper-Threading Technology
	return 0;  // This is not a genuine Intel processor.
}

#define NUM_LOGICAL_BITS	0x00FF0000 // EBX[23:16] indicate number of
										// logical processors per package
// Returns the number of logical processors per physical processors.
static U8 LogicalProcessorsPerPackage(void)
{
	int CPUInfo[4];
	if (!HTSupported()) return (U8) 1;
	__cpuid(CPUInfo, 1);
	return (U8) ((CPUInfo[1] & NUM_LOGICAL_BITS) >> 16);
}

#define NUM_CORE_BITS 0xFC000000

// Assumptions prior to calling:
// - CPUID instruction is available
// - We have already used CPUID to verify that this in an Intel® processor
// Code from http://softwarecommunity.intel.com/articles/eng/2669.htm
static U8 CoresPerPackage(void)
{
	// Is explicit cache info available?
	int nCaches=0;
	U8 coresPerPackage=1; // Assume 1 core per package if info not available 
	DWORD t;
	int cacheIndex;
	CPUID_ARGS ca;

	ca.eax = 0;
	ca.ecx = 0;
	_CPUID(&ca);
	t = ca.eax;
	if ((t > 3) && (t < 0x80000000)) { 
		for (cacheIndex=0; ; cacheIndex++) {
			ca.eax = 4;
			ca.ecx = cacheIndex;
			_CPUID(&ca);
			t = ca.eax;
			if ((t & 0x1F) == 0)
				break;
			nCaches++;
		}
	}

	if (nCaches > 0) {
		ca.eax = 4;
		ca.ecx = 0; // first explicit cache
		_CPUID(&ca);
		coresPerPackage = ((ca.eax >> 26) & 0x3F) + 1; // 31:26
	}
	return coresPerPackage;
}

#define INITIAL_APIC_ID_BITS	0xFF000000 // EBX[31:24] unique APIC ID
// Returns the 8-bit unique Initial APIC ID for the processor this
// code is actually running on. The default value returned is 0xFF if
// Hyper-Threading Technology is not supported.
U8 GetAPIC_ID (void)
{
	int CPUInfo[4];
	if (!HTSupported()) return (U8) -1;
	__cpuid(CPUInfo, 1);
	return (U8) ((CPUInfo[1] & INITIAL_APIC_ID_BITS) >> 24);
}

int HyperThreadingEnabled(void)
{
	// Check to see if Hyper-Threading Technology is available 
	static S32 HT_Enabled = -1;
	U8 Logical_Per_Package; 
	HANDLE hCurrentProcessHandle;
	DWORD_PTR dwProcessAffinity;
	DWORD_PTR dwSystemAffinity;
	DWORD dwAffinityMask;
	BOOL bRet;
	U8 i;
	U8 PHY_ID_MASK;
	U8 PHY_ID_SHIFT;
	U8 Cores_Per_Package;
	U8 Logical_Per_Core;

	if(HT_Enabled >= 0){
		return HT_Enabled;
	}

	PERFINFO_AUTO_START_FUNC();

	HT_Enabled = 0;

	if (!HTSupported()) {  // Bit 28 set indicated Hyper-Threading Technology
		PERFINFO_AUTO_STOP();
		return 0;
	}

	Logical_Per_Package = LogicalProcessorsPerPackage();

	// Just because logical processors is > 1 
	// does not mean that Hyper-Threading Technology is enabled.
	if (Logical_Per_Package <= 1) {
		PERFINFO_AUTO_STOP();
		return 0;
	}
		
	PERFINFO_AUTO_START("top", 1);

	// Physical processor ID and Logical processor IDs are derived
	// from the APIC ID.  We'll calculate the appropriate shift and
	// mask values knowing the number of logical processors per
	// physical processor package and cores per package.
	i = 1;
	PHY_ID_MASK = 0xFF;
	PHY_ID_SHIFT = 0;
	Cores_Per_Package = CoresPerPackage();
	Logical_Per_Core = Logical_Per_Package / Cores_Per_Package;
		
	while (i < Logical_Per_Core){
		i *= 2;
		PHY_ID_MASK <<= 1;
		PHY_ID_SHIFT++;
	}
		
	// The OS may limit the processors that this process may run on.
	hCurrentProcessHandle = GetCurrentProcess();
	bRet = GetProcessAffinityMask(hCurrentProcessHandle, &dwProcessAffinity,
							&dwSystemAffinity);

	PERFINFO_AUTO_STOP();

	if (!bRet) {
		devassertmsgf(bRet, "GetProcessAffinityMask in HyperThreadingEnabled() failed because of: %s", lastWinErr());
		PERFINFO_AUTO_STOP();
		return 0;
	}
		
	// If our available process affinity mask does not equal the 
	// available system affinity mask, then we may not be able to 
	// determine if Hyper-Threading Technology is enabled.
	if (dwProcessAffinity != dwSystemAffinity)
		;//printf ("This process can not utilize all processors.\n"); 
		
	dwAffinityMask = 1;
	PERFINFO_AUTO_START("cpu scan", 1);
	while (!HT_Enabled && dwAffinityMask != 0 && dwAffinityMask <= dwProcessAffinity) {
		// Check to make sure we can utilize this processor first.
		if (dwAffinityMask & dwProcessAffinity) {
			S32 set;
			U32 error = 0;

			PERFINFO_AUTO_START("SetProcessAffinityMask (cpu 0)", 1);
			set = SetProcessAffinityMask(hCurrentProcessHandle, dwAffinityMask);
			if(!set){
				error = GetLastError();
			}
			PERFINFO_AUTO_STOP();

			if (set) {
				U8 APIC_ID;
				U8 LOG_ID;
				U8 PHY_ID;
					
				Sleep(0);  // We may not be running on the cpu that we
				// just set the affinity to.  Sleep gives the OS
				// a chance to switch us to the desired cpu.
					
				APIC_ID = GetAPIC_ID();
				LOG_ID = APIC_ID & ~PHY_ID_MASK;
				PHY_ID = APIC_ID >> PHY_ID_SHIFT;
					
				if (LOG_ID!=0)
					HT_Enabled = 1;
			}
			else {
				// This shouldn't happen since we check to make sure we
				// can utilize this processor before trying to set 
				// affinity mask.
				//printf ("Set Affinity Mask Error Code: %d\n", error);
			}
		}
		dwAffinityMask = dwAffinityMask << 1;
	}
	PERFINFO_AUTO_STOP();

	// Don't forget to reset the processor affinity if you use this code
	// in an application.
	PERFINFO_AUTO_START("SetProcessAffinityMask (original)", 1);
	SetProcessAffinityMask(hCurrentProcessHandle, dwProcessAffinity);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP();
	return HT_Enabled;
}

#if !PLATFORM_CONSOLE
int num_cores=0;
// Sets the number of cores/CPUs to be used
AUTO_CMD_INT(num_cores, num_cores) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(numCoresCallback);
void numCoresCallback(Cmd *cmd, CmdContext *cmd_context)
{
	SetProcessAffinityMask(GetCurrentProcess(), (1 << num_cores)-1);
}
#endif

int getNumVirtualCpus(void)
{
	static int		cpu_count;
	DWORD_PTR		process=0, system=0;
	int				i;

	if (cpu_count)
		return cpu_count;
	GetProcessAffinityMask(GetCurrentProcess(), &process, &system);
	for(i=0;i<32;i++)
	{
		if (process & ((DWORD_PTR)1<<i))
			++cpu_count;
	}
	return cpu_count;
}

#endif

#if _PS3

int getNumVirtualCpus(void)
{
	return 2;
}

#elif _XBOX

int HyperThreadingEnabled(void)
{
	return 1;
}

int getNumVirtualCpus(void)
{
	return 6;
}

#endif

int getNumRealCpus(void)
{
#if _PS3
    return 1;
#else
	static int		cpu_count;

	if (cpu_count)
		return cpu_count;
	cpu_count = getNumVirtualCpus();
	if (HyperThreadingEnabled() && cpu_count > 1)
		cpu_count/=2;
	return cpu_count;
#endif
}

