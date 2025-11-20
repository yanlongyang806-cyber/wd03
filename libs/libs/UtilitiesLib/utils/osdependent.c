#if !PLATFORM_CONSOLE

#include <windows.h>
#include "osdependent.h"

int g_isUsingWin2korXP = -1;
int g_isUsingWin7 = -1;
int g_isUsingVista = -1;
int g_isUsingWin8 = -1;
int g_isUsingX64 = -1;

int IsUsingWin2kOrXp(void)
{
	if (g_isUsingWin2korXP == -1)
		InitOSInfo();
	return g_isUsingWin2korXP;
}

int IsUsingWin9x(void)
{
	return !IsUsingWin2kOrXp();
}

int IsUsingWin7(void)
{
	if (g_isUsingWin7 == -1)
		InitOSInfo();
	return g_isUsingWin7;
}

int IsUsingVista(void)
{
	if (g_isUsingVista == -1)
		InitOSInfo();
	return g_isUsingVista;
}

int IsUsingWin8(void)
{
	if (g_isUsingWin8 == -1)
		InitOSInfo();
	return g_isUsingWin8;
}

int IsUsingX64(void)
{
	if (g_isUsingX64 == -1)
		InitOSInfo();
	return g_isUsingX64;
}


int IsUsingXbox(void)
{
	return false;
}

int IsUsingPS3(void)
{
	return false;
}

void InitOSInfo(void)
{
	OSVERSIONINFO os_version_info;
	SYSTEM_INFO system_info;

	os_version_info.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

	GetVersionEx( &os_version_info );

	g_isUsingWin2korXP = (os_version_info.dwPlatformId == VER_PLATFORM_WIN32_NT);
	g_isUsingVista = (os_version_info.dwMajorVersion >= 6);
	g_isUsingWin7 = (os_version_info.dwMajorVersion > 6) || ((os_version_info.dwMajorVersion == 6) && (os_version_info.dwMinorVersion >= 1));
	g_isUsingWin8 = (os_version_info.dwMajorVersion > 6) || ((os_version_info.dwMajorVersion == 6) && (os_version_info.dwMinorVersion >= 2)); 

	GetNativeSystemInfo( &system_info );
	// "_INTEL" means "x86", includes AMD
	g_isUsingX64 = (system_info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL);
}

#else


int IsUsingWin2kOrXp(void)
{
	return false;
}

int IsUsingWin9x(void)
{
	return false;
}

int IsUsingVista(void)
{
	return false;
}

int IsUsingWin8(void)
{
	return false;
}

int IsUsingXbox(void)
{
	return true;
}

int IsUsingPS3(void)
{
	return false;
}

int IsUsingX64(void)
{
	return false;
}

void InitOSInfo(void)
{
	
}

#endif