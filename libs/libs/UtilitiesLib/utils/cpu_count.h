#pragma once
GCC_SYSTEM

// Code based on http://softwarecommunity.intel.com/articles/eng/2669.htm
#include "wininclude.h"

typedef struct cpuid_args_s {
	DWORD eax;
	DWORD ebx;
	DWORD ecx;
	DWORD edx;
} CPUID_ARGS;

#if _MSC_VER >= 1600
	// Intrinsic on all platforms
#	define _CPUID(p) __cpuidex((int*)(p), (p)->eax, (p)->ecx);
#elif _M_X64 // For 64-bit apps
	void cpuid64(CPUID_ARGS* p);
#	define _CPUID cpuid64
#else // For 32-bit apps
	void cpuid32(CPUID_ARGS* p);
#	define _CPUID cpuid32
#endif

int HyperThreadingEnabled(void);
int getNumVirtualCpus(void);
int getNumRealCpus(void);

