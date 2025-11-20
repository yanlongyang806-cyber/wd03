#include "ssemath.h"

int sseAvailable = 0;
int sse2Available = 0;
static int sse_init_firsttime = 1;

#if !PLATFORM_CONSOLE

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void noSSE2(int param)
{
	isSSE2available();
	if (param)
		sse2Available = 0;
}

#endif


#if !PLATFORM_CONSOLE


static int isSSEavailable_internal(void)
{
#ifdef _WIN64
	return 1;
#else
	int CPUInfo[4];
	__cpuid(CPUInfo, 1);
	if ((CPUInfo[3] & (1 << 25))) 
		return 1;
	return 0;
#endif
}

static int isSSE2available_internal(void)
{
#ifdef _WIN64
	return 1;
#else
	int CPUInfo[4];
	__cpuid(CPUInfo, 1);
	if ((CPUInfo[3] & (1 << 26))) 
		return 1;
	return 0;
#endif
}

static int isSSE3available_internal(void)
{
#ifdef _WIN64
	return 1;
#else
	int CPUInfo[4];
	__cpuid(CPUInfo, 1);
	if ((CPUInfo[2] & (1 << 0))) 
		return 1;
	return 0;
#endif
}

static int isSSE4_1available_internal(void)
{
#ifdef _WIN64
	return 1;
#else
	int CPUInfo[4];
	__cpuid(CPUInfo, 1);
	if ((CPUInfo[2] & (1 << 19))) 
		return 1;
	return 0;
#endif
}

static int isSSE4_2available_internal(void)
{
#ifdef _WIN64
	return 1;
#else
	int CPUInfo[4];
	__cpuid(CPUInfo, 1);
	if ((CPUInfo[2] & (1 << 20))) 
		return 1;
	return 0;
#endif
}

int isSSEavailable(void)
{

	if (sse_init_firsttime)
	{
		sse_init_firsttime = 0;
		sseAvailable = isSSEavailable_internal();
		sse2Available = isSSE2available_internal();
	}

	return sseAvailable;
}

int isSSE2available(void)
{

	if (sse_init_firsttime)
	{
		sse_init_firsttime = 0;
		sseAvailable = isSSEavailable_internal();
		sse2Available = isSSE2available_internal();
	}

	return sse2Available;
}

int isSSE3available(void)
{
	return isSSE3available_internal();
}

int isSSE4available(void)
{
	return isSSE4_1available_internal();
}

#else

int isSSEavailable(void)
{
	return 0;
}

int isSSE2available(void)
{
	return 0;
}

int isSSE3available(void)
{
	return 0;
}

int isSSE4available(void)
{
	return 0;
}

#endif