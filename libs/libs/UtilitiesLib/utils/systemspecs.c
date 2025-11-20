#include "sysutil.h"
#include "systemspecs.h"
#include "mathutil.h"
#include "timing.h"
#include "cpu_count.h"
#include "VideoMemory.h"
#include "MemReport.h"
#include "file.h"
#include "trivia.h"
#include "AutoGen/systemspecs_h_ast.c"
#include "utilitiesLib.h"
#include "process_util.h"
#include "osdependent.h"
#include "winutil.h"
#include "RegistryReader.h"
#include "ssemath.h"
#include "ContinuousBuilderSupport.h"
#include "AppLocale.h"
#include "UnitSpec.h"
#include "SimpleParser.h"
#include "ScratchStack.h"
#include "StringUtil.h"
#include "Prefs.h"
#include "VirtualMemory.h"
#include "UTF8.h"

#if !PLATFORM_CONSOLE
#include <psapi.h>
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

SystemSpecs	system_specs;
static TriviaList *sysspecs_other_trivia = NULL;

bool nv_api_avail = false;
bool is_old_ati_catalyst = false;

#if !PLATFORM_CONSOLE

#include "../GLRenderLib/glh/NvPanelApi.h"
#include "../../3rdparty/NVPerfSDK/nvapi.h"

#ifndef _CODE_COVERAGE
// These libraries cause problems for VS 2010 Ultimate code coverage, in vsinstr:
//   Warning VSP2005 : Internal instrumentation warning : The object '\bld\r185_00\drivers\nvapi\_out\winxp_x86_release\nvlink_gen.obj' must be rebuilt with approved tools. [C:\src\Core\CrypticLauncher\CrypticLauncher.vcxproj]
#ifdef _M_X64
#pragma comment(lib, "nvapi64.lib")
#else
#pragma comment(lib, "nvapi.lib")
#endif
#endif

int getDriverVersion( char * version, int version_size, char * dllName )
{
	char *pWinDirName = NULL;
	char dllPathName[1024];
	VS_FIXEDFILEINFO fileInfo;
	DWORD dwDummyHandle; // will always be set to zero
	DWORD len;
	LPVOID lpvi;
	UINT iLen;
	char * m_pVersionInfo;	

	if (!strchr(dllName,'/'))
	{
		estrStackCreate(&pWinDirName);
		GetSystemDirectory_UTF8(&pWinDirName);

		sprintf_s( SAFESTR(dllPathName), "%s\\%s", pWinDirName, dllName );
		estrDestroy(&pWinDirName);
	}
	else
	{
		sprintf_s( SAFESTR(dllPathName), "%s/%s", "c:/program files", dllName );
	}
	memset(&fileInfo, 0, sizeof(VS_FIXEDFILEINFO));

	// read file version info
	len = GetFileVersionInfoSize_UTF8( dllPathName, &dwDummyHandle );
	if (len <= 0)
	{
		strcpy_s( version, version_size, "0.0.0.0" );
		return 0;
	}

	m_pVersionInfo = calloc( 1, len ); // allocate version info
	if (!GetFileVersionInfo_UTF8( dllPathName, 0, len, m_pVersionInfo))
	{
		strcpy_s( version, version_size, "0.0.0.0" );
		free(m_pVersionInfo);
		return 0;
	}

	if (!VerQueryValue(m_pVersionInfo, L"\\", &lpvi, &iLen))
	{
		strcpy_s( version, version_size, "0.0.0.0" );
		free(m_pVersionInfo);
		return 0;
	}

	// copy fixed info to myself, which am derived from VS_FIXEDFILEINFO
	fileInfo = *(VS_FIXEDFILEINFO*)lpvi;

	sprintf_s( SAFESTR2(version), "%d.%d.%d.%d", 
		((fileInfo.dwProductVersionMS >> 16) & 0xFFFF),
		(fileInfo.dwProductVersionMS & 0xFFFF),
		((fileInfo.dwProductVersionLS >> 16) & 0xFFFF),
		(fileInfo.dwProductVersionLS & 0xFFFF) );

	free(m_pVersionInfo);

	return 1;
}

NV_DISPLAY_DRIVER_MEMORY_INFO nv_startup_memory_info;

// The nVidia libraries are not available when Code Coverage is on.  See above.
#ifdef _CODE_COVERAGE

static void getNVidiaSystemSpecs(void)
{
}

#else  // _CODE_COVERAGE

static void getNVidiaSystemSpecs(void)
{
	HINSTANCE hLib;
	fNvCplGetDataInt NvCplGetDataInt;
	fNvGetDisplayInfo NvGetDisplayInfo;

	if (NVAPI_OK == NvAPI_Initialize())
	{
		NvPhysicalGpuHandle nvGPUHandlePhysical[NVAPI_MAX_PHYSICAL_GPUS];
		NvU32 numPhysicalGPUs;
		NvLogicalGpuHandle nvGPUHandleLogical[NVAPI_MAX_LOGICAL_GPUS];
		NvU32 numLogicalGPUs;
		if (NVAPI_OK == NvAPI_EnumPhysicalGPUs(nvGPUHandlePhysical, &numPhysicalGPUs))
		{
			if (NVAPI_OK == NvAPI_EnumLogicalGPUs(nvGPUHandleLogical, &numLogicalGPUs))
			{
				if (numLogicalGPUs < numPhysicalGPUs)
				{
					system_specs.nvidiaSLIGPUCount = numPhysicalGPUs - numLogicalGPUs + 1;
				}
			}
		}

		nv_startup_memory_info.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER_2;
		NvAPI_GPU_GetMemoryInfo(NVAPI_DEFAULT_HANDLE, &nv_startup_memory_info);

		nv_api_avail = true;
	}

	hLib = LoadLibrary(L"NVCPL.dll");
	if (hLib) {
		NvCplGetDataInt = (fNvCplGetDataInt)GetProcAddress(hLib, "NvCplGetDataInt");
		if (NvCplGetDataInt) {
			DWORD value;
			if (NvCplGetDataInt(NVCPL_API_VIDEO_RAM_SIZE, &value)) {
				system_specs.videoMemory = value;
			}
		}
		NvGetDisplayInfo = (fNvGetDisplayInfo)GetProcAddress(hLib, "NvGetDisplayInfo");
		if (NvGetDisplayInfo) {
			NVDISPLAYINFO display_info={0};
			display_info.cbSize = sizeof(display_info);
			display_info.dwInputFields1 = 0xffffffff;
			display_info.dwInputFields2 = 0xffffffff;
			if (NvGetDisplayInfo("0", &display_info)) {
				if (display_info.nDisplayMode == NVDISPLAYMODE_STANDARD ||
					display_info.nDisplayMode == NVDISPLAYMODE_NONE)
				{
					// Single monitor
				} else {
					// Assume problems on old drivers!
					system_specs.numMonitors = 2;
				}
			}
		}

		FreeLibrary(hLib);
	}
}

#endif  // _CODE_COVERAGE

static void getATISystemSpecs(void)
{
	typedef INT (*ATIQUERYMGPUCOUNT)();
	ATIQUERYMGPUCOUNT AtiQueryMgpuCount;
	// Load library and get function pointer
	HINSTANCE lib = LoadLibrary(L"ATIMGPUD.DLL");
	if (!lib)
		return;
	
	AtiQueryMgpuCount = (ATIQUERYMGPUCOUNT)GetProcAddress(lib, "AtiQueryMgpuCount");
	if (AtiQueryMgpuCount)
	{
		// Query number of adapters in Multi-GPU setup
		system_specs.atiCrossfireGPUCount = AtiQueryMgpuCount();
	}
	FreeLibrary(lib);
}

#endif

static bool isUsingD3DDebug(void)
{
#if PLATFORM_CONSOLE
	{
		return false;
	}
#else
	{
		RegReader rr = createRegReader();
		int value = 0;
		if( initRegReader(rr, "HKEY_CURRENT_USER\\Software\\Microsoft\\Direct3D")) {
			rrReadInt(rr, "LoadDebugRuntime", &value);
		} else if( initRegReader(rr, "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Direct3D")) {
			rrReadInt(rr, "LoadDebugRuntime", &value);
		}
		destroyRegReader(rr);
		return value != 0;
	}
#endif
}

int systemSpecsVideoCardIdentification(char *deviceString, int bufSize, int *vendorID, int *deviceID, int *numeric, bool *isMobile)
{
#if _PS3
	strcpy_s( deviceString, bufSize, "PS3");
	*vendorID = VENDOR_PS3;
	*deviceID = 0;
	*numeric = 0;
	*isMobile = 0;
	return 1;
#elif _XBOX
	strcpy_s( deviceString, bufSize, "XBox360");
	*vendorID = VENDOR_XBOX360;
	*deviceID = 0;
	*numeric = 0;
	*isMobile = 0;
	return 1;
#else
	DISPLAY_DEVICE dd;
	int i = 0;
	char *pIDEString = NULL;
	estrStackCreate(&pIDEString);

	//Dumb: Apparently windows doesn't know the size of its own structure.  
	//(But it does know the offsets of all elements in the structure)
	dd.cb = sizeof(DISPLAY_DEVICE);

	// locate primary display device
	while ( EnumDisplayDevices(NULL, i, &dd, 0) )
	{
		if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
		{
			if (dd.DeviceID)
			{
				UTF16ToEstring(dd.DeviceID, 0, &pIDEString);
			}
			break;
		}

		i++;
	}

	if ( estrLength(&pIDEString) == 0 ) 
	{
		*vendorID = 0;
		*deviceID = 0;
		*numeric = 0;
		deviceString[0] = 0;
		estrDestroy(&pIDEString);
		return 0;
	}

	// If we're under WINE, switch us to a vendor ID that'll let us specifically handle
	// the DX/GL wrapper's weirdness.
	if(getWineVersion() && !getIsTransgaming()) {
		*vendorID = VENDOR_WINE;
		*deviceID = 1;
		strcpy_s(deviceString, bufSize, "WINE Graphics Driver");
		estrDestroy(&pIDEString);
		return 1;
	}

#ifdef UNICODE
	WideToUTF8StrConvert(dd.DeviceString, deviceString, bufSize);
#else
	ACPToUTF8(dd.DeviceString, deviceString, bufSize);
#endif
	removeTrailingWhiteSpaces(deviceString);
	// get vendor ID
	{
		char vendorIDStr[5];
		char deviceIDStr[5];
		char end[1];
		char * endptr;

		end[0] = 0;
		endptr = end;

		strncpy_s( vendorIDStr, sizeof(vendorIDStr), &pIDEString[8], 4 );
		strncpy_s( deviceIDStr, sizeof(deviceIDStr), &pIDEString[17], 4 );
		vendorIDStr[4] = 0;
		deviceIDStr[4] = 0;

		*vendorID = (int)strtol( vendorIDStr, &endptr, 16 );
		*deviceID = (int)strtol( deviceIDStr, &endptr, 16 );
	}

	// Try to parse device name into the numeric version of the card
	{
		char *s = deviceString;
		*numeric = 0;
		// Look for the longest string of digits
		while (*s)
		{
			int v = atoi(s);
			if (v > 100 && v > *numeric)
			{
				*numeric = v;
				*isMobile = strEndsWith(s, "M");
			}
			s++;
		}
		if (strstri(deviceString, "Mobil") || strstri(deviceString, " Go "))
			*isMobile = true;
	}

	estrDestroy(&pIDEString);
	return 1;
#endif
}

AUTO_RUN_EARLY;
void systemSpecsInitEarly(void)
{
	system_specs.material_supported_features = SGFEAT_ALL;
	// Enable by default, require manual disable
	system_specs.isDx11Enabled = 1;
	// Enable by default now, allowed to be disabled from command 
	// line for testing/stability in case it causes problems
	system_specs.isDx9ExEnabled = 1;
}

U32 getCPUCacheSize(void)
{
#if PLATFORM_CONSOLE
	return 0;
#else
	CPUID_ARGS args = {0};
	int cache_query=0;
	U32 ret=0;
	do {
		int cache_type;
		int ways, partitions, line_size, sets;
		args.eax = 4;
		args.ecx = cache_query;
		_CPUID(&args);
#define BITMASK(val, start, end) (((val) >> (end)) & ((1<<((start) - (end))) - 1))
		cache_type = BITMASK(args.eax, 4, 0);
		if (!cache_type)
			break;
		cache_query++;

		ways = BITMASK(args.ebx, 31, 22) + 1;
		partitions = BITMASK(args.ebx, 21, 12) + 1;
		line_size = BITMASK(args.ebx, 11, 0) + 1;
		sets = args.ecx + 1;
#undef BITMASK
		if (0)
		{
			switch (cache_type)
			{
				xcase 1:
					printf("data cache ");
				xcase 2:
					printf("instruction cache ");
				xcase 3:
					printf("unified cache ");
			}
			printf("  %s\n", friendlyBytes(ways*partitions*line_size*sets));
		}
		ret += ways*partitions*line_size*sets;
	} while (cache_query < 10); // Should only ever hit around 3, then breka above
	return ret;
#endif
}

// Does a RAM speed test and resturns the result in MB/s
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
F32 RAMSpeedTest(void)
{
	U32 size = 30*1024*1024;
	U32 wordsize = size>>2;
	int totaltimer = timerAlloc();
	U32 *data = malloc(size);
	U32 *data2 = malloc(size);
	int timer = timerAlloc();
	U32 i;
	int count=0;
	F32 ret;

	for (i=0; i<wordsize; i++)
		data[i] = i;

	timerStart(timer);
	while (count < 3 && timerElapsed(timer)<0.5)
	{
		memcpy(data2, data, size);
		count++;
	}

	ret = size*(F32)count / timerElapsed(timer) * (1.f/(1024*1024));

	//printf("%d runs in %fs\n", count, timerElapsed(timer));

	free(data);
	free(data2);
	timerFree(timer);
	//printf("Total test time: %fs\n", timerElapsed(totaltimer));
	timerFree(totaltimer);
	return ret;
}

F32 RAMSpeedCached(void)
{
#if _PS3
	return 1800.0f;
#else
	RegReader rr = createRegReader();
	F32 ret=0;
	char buf[1024];
	if (initRegReader(rr, "HKEY_CURRENT_USER\\SOFTWARE\\RaGEZONE\\Core"))
	{
		int v;
		time_t now = time(NULL);
		if (!rrReadInt(rr, "RAMSpeedTestAge", &v))
			v = 0;
		if (now - v < 0 || now - v > 60*60*24)
			v = 0;
		if (v)
		{
			rrReadString(rr, "RAMSpeedTestValue", SAFESTR(buf));
			ret = atof(buf);
		}
		if (!v || ret<=0)
		{
			ret = RAMSpeedTest();
			sprintf(buf, "%f", ret);
			rrWriteString(rr, "RAMSpeedTestValue", buf);
			rrWriteInt(rr, "RAMSpeedTestAge", now);
		}
	}
	destroyRegReader(rr);
	if (!ret)
		ret = RAMSpeedTest();
	return ret;
#endif
}

bool IsOldIntelDriverNoD3D11(void)
{
	// Old versions of the Intel driver crash in D3D11 even if you're just asking it if D3D11 is supported or not :(
	systemSpecsVideoCardIdentification(system_specs.videoCardName, ARRAY_SIZE_CHECKED(system_specs.videoCardName), &system_specs.videoCardVendorID, &system_specs.videoCardDeviceID, &system_specs.videoCardNumeric, &system_specs.videoCardIsMobile);
	if (system_specs.videoCardVendorID == VENDOR_INTEL)
	{
		char version[1024];
		if (getDriverVersion(SAFESTR(version), "igd10umd32.dll"))
		{
			if (version[0] == '7' || version[0] == '6')
				return true;
		}
	}
	return false;
}


void systemSpecsInit(void)
{
	char *s=NULL;
	if (system_specs.isFilledIn==1)
		return;

	system_specs.numMonitors = 1;

#if _PS3
    {
    }
#elif !_XBOX
    {
	OSVERSIONINFOEX osinfo;
	// OS version:
	ZeroMemory(&osinfo, sizeof(osinfo));
	osinfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if (GetVersionEx((LPOSVERSIONINFO) &osinfo))
	{
		if ((osinfo.dwPlatformId == VER_PLATFORM_WIN32_NT) &&
			(osinfo.dwMajorVersion >= 5))
		{
			// get extended version info for 2000 and later
			ZeroMemory(&osinfo, sizeof(osinfo));
			osinfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
			GetVersionEx((LPOSVERSIONINFO) &osinfo);
		}
	}
	system_specs.highVersion = osinfo.dwMajorVersion;
	system_specs.lowVersion = osinfo.dwMinorVersion;
	system_specs.build = osinfo.dwBuildNumber;
	system_specs.servicePackMajor = osinfo.wServicePackMajor;
	system_specs.servicePackMinor = osinfo.wServicePackMinor;
	system_specs.isRunningNortonAV = !!ProcessCount("Rtvscan.exe", false);
	system_specs.isX64 = IsUsingX64();
	system_specs.isVista = IsUsingVista();
    }
	// Get the host OS we're running on (different if any compatibility mode bits are set)
	sprintf(system_specs.hostOSversion, "%d.%d.%d", system_specs.highVersion, system_specs.lowVersion, system_specs.build);
	{
		struct LANGANDCODEPAGE {
			WORD wLanguage;
			WORD wCodePage;
		} *lpTranslate;
		
		char *pBuf = NULL;

		DWORD ignored;
		U32 size;
		void *data;
		U32 i;
		GetSystemDirectory_UTF8(&pBuf);
		estrConcatf(&pBuf, "\\kernel32.dll");
		size = GetFileVersionInfoSize_UTF8(pBuf, &ignored);
		data = ScratchAlloc(size);
		if (GetFileVersionInfo_UTF8(pBuf, ignored, size, data))
		{
			// Read the list of languages and code pages.
			VerQueryValue(data, L"\\VarFileInfo\\Translation", &lpTranslate, &size);

			// Read the file description for each language and code page.
			for( i=0; i < (size/sizeof(struct LANGANDCODEPAGE)); i++ )
			{
				char *ver;
				U32 len;
				estrPrintf(&pBuf, "\\StringFileInfo\\%04x%04x\\ProductVersion",
					lpTranslate[i].wLanguage,
					lpTranslate[i].wCodePage);

				// Retrieve file description for language and code page "i". 
				if (VerQueryValue_UTF8(data, 
					pBuf, 
					&ver, 
					&len))
				{
					strcpy(system_specs.hostOSversion, ver);
					break;
				}
			}
		}

		
		estrDestroy(&pBuf);
		ScratchFree(data);
	}

	// Bandwidth (from updater)
//	Strncpyt(system_specs.bandwidth, regGetPatchBandwidth());
#endif

	system_specs.hasSSE = isSSEavailable();
	system_specs.hasSSE2 = isSSE2available();
	system_specs.hasSSE3 = isSSE3available();
	system_specs.hasSSE4 = isSSE4available();

	system_specs.SVNBuildNumber = gBuildVersion;

	systemSpecsVideoCardIdentification(system_specs.videoCardName, ARRAY_SIZE_CHECKED(system_specs.videoCardName), &system_specs.videoCardVendorID, &system_specs.videoCardDeviceID, &system_specs.videoCardNumeric, &system_specs.videoCardIsMobile);

	getPhysicalMemory64(&system_specs.physicalMemoryMax, &system_specs.physicalMemoryAvailable);
	system_specs.virtualAddressSpace = getVirtualAddressSize();

	system_specs.CPUSpeed = (F32)timeGetCPUCyclesPerSecond();
	system_specs.numVirtualCPUs = getNumVirtualCpus();
	system_specs.numRealCPUs = getNumRealCpus();
	system_specs.RAMSpeedGBs = RAMSpeedCached()/1024.f; // Takes a measurable amount of time (0.1s to 1s), use cached value instead

#if _PS3
    strcpy(system_specs.cpuIdentifier, "CELL");
#elif _XBOX
	strcpy(system_specs.cpuIdentifier, "XBox360");
#else
	_dupenv_s(&s, NULL, "PROCESSOR_IDENTIFIER");
	if (s)
		Strncpyt(system_specs.cpuIdentifier, s);
	else
		system_specs.cpuIdentifier[0] = 0;
	crt_free(s);
	s = NULL;
#endif

	system_specs.cpuCacheSize = getCPUCacheSize();

#if _PS3
    sprintf(system_specs.videoDriverVersion, "GCM_PS3");
#elif _XBOX
	sprintf(system_specs.videoDriverVersion, "XBox360");
#else

	if(system_specs.videoCardVendorID == VENDOR_NV) {
		int ret=0;
		if (!ret)
			ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), "nvapi.dll" );
		if (!ret)
			ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), "nv4_disp.dll" );
		if (!ret)
			ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), "nvdisp.drv" );
		if (!ret)
			ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), "nvogl32.dll" );
		if (!ret)
			ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), "nvopengl.dll" );
	} else if(system_specs.videoCardVendorID == VENDOR_ATI ) {
		int ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), "ati3duag.dll" );
		if (!ret)
			ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), "atiumdag.dll" );
	} else if(system_specs.videoCardVendorID == VENDOR_INTEL ) {
		char *names[] = {
			"ig4icd32.dll",
			"ig4dev32.dll",
			"ig4icd64.dll",
			"ig4dev64.dll",
			"ialmgdev.dll",
			"igldev32.dll",
			"iglicd32.dll",
			"igldev64.dll",
			"iglicd64.dll",
		};
		int i;
		for (i=0; i<ARRAY_SIZE(names); i++) {
			int ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), names[i]);
			if (ret)
				break;
		}
	} else if( system_specs.videoCardVendorID == VENDOR_S3G ) {
		char *names[] = {
			"s3g700.dll",
			"s3g700.sys",
			"S3DDX9L_32.dll",
			"S3DDX9L_64.dll",
		};
		int i;
		for (i=0; i<ARRAY_SIZE(names); i++) {
			int ret = getDriverVersion(SAFESTR(system_specs.videoDriverVersion), names[i]);
			if (ret)
				break;
		}
	} else
		sprintf_s( SAFESTR(system_specs.videoDriverVersion), "UnknownVendor");
#endif

	if( strlen(system_specs.videoDriverVersion) )
	{
		char driverString[256];
		char * driverVersionSubstr;
		strcpy( driverString, system_specs.videoDriverVersion );
		driverVersionSubstr = strrchr(driverString, '.');
		if( driverVersionSubstr )
		{
			*driverVersionSubstr='\0';
			driverVersionSubstr++;
			system_specs.videoDriverVersionNumber = atoi( driverVersionSubstr );
			driverVersionSubstr = strrchr(driverString, '.');
			if( driverVersionSubstr )
			{
				int majorVersion;
				driverVersionSubstr++;
				majorVersion = atoi(driverVersionSubstr);
				if (system_specs.videoCardVendorID == VENDOR_NV && majorVersion > 10) {
					system_specs.videoDriverVersionNumber += 10000 * ((majorVersion & 0xff) % 10);
				} else if (system_specs.videoCardVendorID == VENDOR_ATI) {
					system_specs.videoDriverVersionNumber += 10000 * majorVersion;
				}
			}
		}
	}
#if _XBOX
	{
		DM_SYSTEM_INFO si;
		si.SizeOfStruct = sizeof(si);
		DmGetSystemInfo(&si);
		system_specs.videoDriverVersionNumber = si.BaseKernelVersion.Build;
	}
#endif

	system_specs.videoMemory = getVideoMemoryMBs();
#if !PLATFORM_CONSOLE
	system_specs.numMonitors = multiMonGetNumMonitors();

	if (system_specs.videoCardVendorID == VENDOR_NV) {

		getNVidiaSystemSpecs();
		// Depth artifacts fixed in 177.99 debug driver, change this when released publicly
		// Disabling error on CBs for now, since it just fixes a graphical artifact, remove check if a crash is fixed in a new driver
		if (!g_isContinuousBuilder)
		{
			if (system_specs.videoCardNumeric >= 5000 && system_specs.videoCardNumeric <= 5999)
			{
				// FX cards, newest driver is 175.19
				if (system_specs.videoDriverVersionNumber < 17519)
				{
					system_specs.videoDriverState = VIDEODRIVERSTATE_OLD;
				}
			}
			else if (system_specs.videoCardNumeric >= 6000 && system_specs.videoCardNumeric <= 7999)
			{
				// GeForce 7 Go cards, newest driver is 179.48, the 180 fix is only really needed for GF8 cards.
				if (system_specs.videoDriverVersionNumber < 17948)
				{
					system_specs.videoDriverState = VIDEODRIVERSTATE_OLD;
				}
			}
			else
			{
				// GeForce 8000+ and GTX ###
				// Below 180.48 has problems
				if (system_specs.videoDriverVersionNumber < 18048)
				{
					system_specs.videoDriverState = VIDEODRIVERSTATE_OLD;
				}
				// Some range between 182 and 186 had the Driver Internal Error problems, I think, might want to skip this on mobile?
				if (system_specs.videoDriverVersionNumber < 18600)
				{
					system_specs.videoDriverState = VIDEODRIVERSTATE_OLD;
				}
				// 190.62 and older, 196.21 and newer are good (in between had stuttering problems)
				// But, new drivers are not yet out for mobile cards, so don't warn for them
				if (system_specs.videoDriverVersionNumber > 19062 && system_specs.videoDriverVersionNumber < 19621 &&
					!system_specs.videoCardIsMobile)
				{
					// These driver versions have significant stuttering issues on our engine
					system_specs.videoDriverState = VIDEODRIVERSTATE_OLD;
				}
				if (system_specs.videoDriverVersionNumber == 25896 )
				{
					// flickering in postprocessing
					system_specs.videoDriverState = VIDEODRIVERSTATE_KNOWNBUGS;
				}
			}
		}
	}
	else if (system_specs.videoCardVendorID == VENDOR_ATI)
	{
		getATISystemSpecs();
		if (!g_isContinuousBuilder)
		{
			//Catalyst 9.6 adds depth resolve support
			if (system_specs.videoDriverVersionNumber < 100671)
			{
				//we cant just system_specs.videoOldDriver = 1; since we dont want to nag
				//on non-SM3 cards since there is no newer driver available from ati
				is_old_ati_catalyst = true;
			}
		}
	}
#endif

	system_specs.isUsingD3DDebug = isUsingD3DDebug();

	strcpy_trunc(system_specs.computerName, getComputerName());
	
	system_specs.material_supported_features = SGFEAT_ALL;
	// Will be filled in later, by renderer
	if (system_specs.material_hardware_override)
		system_specs.material_hardware_supported_features = SGFEAT_ALL;
	else
		system_specs.material_hardware_supported_features = 0;

#if !PLATFORM_CONSOLE
	// Get disk space data
	{
		ULARGE_INTEGER diskFree, diskTotal;
		assert(GetDiskFreeSpaceEx(NULL, &diskFree, &diskTotal, NULL));
		system_specs.diskFree = diskFree.QuadPart;
		system_specs.diskTotal = diskTotal.QuadPart;
	}
#endif

	// Check if this is Wine
	{
		const char *winever = getWineVersion();
		system_specs.isWine = (bool)!!winever;
		strcpy(system_specs.wineVersion, NULL_TO_EMPTY(winever));

		system_specs.isTransgaming = getIsTransgaming();
		if(system_specs.isTransgaming) {
			const char *tgInfoStr = getTransgamingInfo();
			if(tgInfoStr) {
				strcpy(system_specs.transgamingInfo, tgInfoStr);
			}
		}
	}

	system_specs.isFilledIn = 1;

	systemSpecsUpdateString();

}

// For RAM and HD
static UnitSpec mem_buckets[] = {
	UNITSPEC("<1MB", 1, 0),
	UNITSPEC("1MB-10MB", 1*1024*1024, 1*1024*1024),
	UNITSPEC("10MB-100MB", 10*1024*1024, 10*1024*1024),
	UNITSPEC("100MB-500MB", 100*1024*1024, 100*1024*1024),
	UNITSPEC("500MB-700MB", 500*1024*1024, 500*1024*1024),
	UNITSPEC("700MB-900MB", 700*1024*1024, 700*1024*1024),
	UNITSPEC("900MB-1.4GB", 900*1024*1024, 900*1024*1024),
	UNITSPEC("1.4GB-1.9GB", 1400*1024*1024, 1400*1024*1024),
	UNITSPEC("1.9GB-2.9GB", 1900LL*1024*1024, 1900LL*1024*1024),
	UNITSPEC("2.9GB-5GB", 2900LL*1024*1024, 2900LL*1024*1024),
	UNITSPEC("5GB-10GB", 5LL*1024*1024*1024, 5LL*1024*1024*1024),
	UNITSPEC("10GB-100GB", 10LL*1024*1024*1024, 10LL*1024*1024*1024),
	UNITSPEC(">100GB", 100LL*1024*1024*1024, 100LL*1024*1024*1024),
	{0}
};

// For Kernel memory amounts and limits
static UnitSpec kmem_buckets[] = {
	UNITSPEC("<1MB", 1, 0),
	UNITSPEC("1MB-10MB", 1*1024*1024, 1*1024*1024),
	UNITSPEC("10MB-20MB", 10*1024*1024, 10*1024*1024),
	UNITSPEC("20MB-50MB", 20*1024*1024, 20*1024*1024),
	UNITSPEC("50MB-100MB", 50*1024*1024, 50*1024*1024),
	UNITSPEC("100MB-200MB", 100*1024*1024, 100*1024*1024),
	UNITSPEC("200MB-250MB", 200*1024*1024, 200*1024*1024),
	UNITSPEC("250MB-300MB", 250*1024*1024, 250*1024*1024),
	UNITSPEC(">300MB", 300*1024*1024, 300*1024*1024),
	{0}
};

void systemSpecsUpdateMemTrivia(int bExact)
{
#if !PLATFORM_CONSOLE
	RegReader sysCCS_C_SM_MM;
	PERFORMANCE_INFORMATION perf_stats;
	U32 poolsize;
	S32 video_mem_estimate;
	U32 availableMem;
	// these specs only indicate kernel limits on 32-bit OSs
	sysCCS_C_SM_MM = createRegReader();
	initRegReader(sysCCS_C_SM_MM, "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management");

	if (!rrReadInt(sysCCS_C_SM_MM, "NonPagedPoolSize", &poolsize))
		poolsize = 0;
	triviaPrintf("SystemSpecs:NonPagedPoolSize", "%d", poolsize);

	if (!rrReadInt(sysCCS_C_SM_MM, "PagedPoolSize", &poolsize))
		poolsize = 0;
	triviaPrintf("SystemSpecs:PagedPoolSize", "%d", poolsize);

	destroyRegReader(sysCCS_C_SM_MM);
	sysCCS_C_SM_MM = NULL;

	perf_stats.cb = sizeof(perf_stats);
	GetPerformanceInfo(&perf_stats, sizeof(perf_stats));

	if (bExact)
	{
		triviaPrintf("KernelPaged", "%"FORM_LL"d", (S64)perf_stats.KernelPaged);
		triviaPrintf("KernelNonpaged", "%"FORM_LL"d", (S64)perf_stats.KernelNonpaged);
	}
	else
	{
		triviaPrintf("KernelPaged", "%s", usFindProperUnitSpec(kmem_buckets, perf_stats.KernelPaged*perf_stats.PageSize)->unitName);
		triviaPrintf("KernelNonpaged", "%s", usFindProperUnitSpec(kmem_buckets, perf_stats.KernelNonpaged*perf_stats.PageSize)->unitName);
	}
	triviaPrintf("PageSize", "%"FORM_LL"d", (S64)perf_stats.PageSize);

	triviaPrintf("HandleCount", "%d", RoundUpToGranularity(perf_stats.HandleCount, 256));
	{
		DWORD nProcHandles = 0;
		GetProcessHandleCount(GetCurrentProcess(), &nProcHandles);
		triviaPrintf("HandleCountProc", "%d", RoundUpToGranularity(nProcHandles, 256));
	}

	video_mem_estimate = memMonitorGetVideoMemUseEstimate();
	triviaPrintf("VideoMemEstimate", "%d", video_mem_estimate / 1024);

	getPhysicalMemory(NULL, &availableMem);
	triviaPrintf("SystemSpecs:physicalMemoryAvailable", "%s", usFindProperUnitSpec(mem_buckets, system_specs.physicalMemoryAvailable)->unitName);

	if (nv_api_avail)
	{

// The nVidia libraries are not available when Code Coverage is on.  See above.
#ifndef _CODE_COVERAGE

		NV_DISPLAY_DRIVER_MEMORY_INFO memory_info = {0};
		memory_info.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER_2;
		if (NVAPI_OK == NvAPI_GPU_GetMemoryInfo(NVAPI_DEFAULT_HANDLE, &memory_info))
		{
			if (bExact)
			{
				triviaPrintf("VideoMemUsedNV", "%"FORM_LL"d", (memory_info.dedicatedVideoMemory - memory_info.curAvailableDedicatedVideoMemory)*1024LL);
				triviaPrintf("VideoMemFreeNV", "%"FORM_LL"d", (memory_info.curAvailableDedicatedVideoMemory)*1024LL);
			} else {
				triviaPrintf("VideoMemUsedNV", "%s", usFindProperUnitSpec(mem_buckets, (memory_info.dedicatedVideoMemory - memory_info.curAvailableDedicatedVideoMemory)*1024LL)->unitName);
				triviaPrintf("VideoMemFreeNV", "%s", usFindProperUnitSpec(mem_buckets, (memory_info.curAvailableDedicatedVideoMemory)*1024LL)->unitName);
			}
		}

#endif  // _CODE_COVERAGE

	}


	virtualMemoryAnalyzeStats(&g_vmStats);
#endif
}

#undef systemSpecsTriviaPrintf
void systemSpecsTriviaPrintf(const char *key, FORMAT_STR const char *format, ...)
{
	char buf[1024];

	if (!sysspecs_other_trivia)
		sysspecs_other_trivia = triviaListCreate();

	VA_START(args, format);
	vsnprintf(buf, _TRUNCATE, format, args);
	VA_END();

	triviaListPrintf(sysspecs_other_trivia, key, "%s", buf);
}

void systemSpecsUpdateString(void)
{
	char buf[1024];
	triviaPrintStruct("SystemSpecs:", parse_SystemSpecs, &system_specs);
	
	if (sysspecs_other_trivia)
	{
		EARRAY_CONST_FOREACH_BEGIN(sysspecs_other_trivia->triviaDatas, i, n);
		{
			if (sprintf(buf, "SystemSpecs:%s", sysspecs_other_trivia->triviaDatas[i]->pKey) > 0)
				triviaPrintf(buf, "%s", sysspecs_other_trivia->triviaDatas[i]->pVal);
		}
		EARRAY_FOREACH_END;
	}

	// Update those values which we want bucketed
	triviaPrintf("SystemSpecs:CPUSpeed", "%1.1f", system_specs.CPUSpeed / 1000000000.f);
	triviaPrintf("SystemSpecs:RAMSpeedGBs", "%1.1f", system_specs.RAMSpeedGBs);
	triviaPrintf("SystemSpecs:physicalMemoryAvailable", "%s", usFindProperUnitSpec(mem_buckets, system_specs.physicalMemoryAvailable)->unitName);
	triviaPrintf("SystemSpecs:physicalMemoryMax", "%s", usFindProperUnitSpec(mem_buckets, system_specs.physicalMemoryMax)->unitName);
	triviaPrintf("SystemSpecs:diskFree", "%s", usFindProperUnitSpec(mem_buckets, system_specs.diskFree)->unitName);
	triviaPrintf("SystemSpecs:diskTotal", "%s", usFindProperUnitSpec(mem_buckets, system_specs.diskTotal)->unitName);

	systemSpecsUpdateMemTrivia(0);

#if !PLATFORM_CONSOLE
	// Store the system specs into the GamePrefs so that when users submit this we get more information
	GamePrefStoreStruct("SystemSpecs", parse_SystemSpecs, &system_specs);
#endif

	systemSpecsGetString(SAFESTR(buf));
	setAssertExtraInfo2(buf);
}

void systemSpecsGetString(char *buf, int buf_size)
{
	systemSpecsInit();

	buf[0] = 0;

	if( system_specs.CPUSpeed )
		strcatf_s( buf, buf_size, "CPU: %0.0f Mhz (%d/%d cores) / ", (system_specs.CPUSpeed / 1000000.0),
			getNumRealCpus(), getNumVirtualCpus());
	else
		strcatf_s( buf, buf_size, "CPU: Unable to determine / ");

	if( system_specs.physicalMemoryMax )
		strcatf_s( buf, buf_size, "RAM: %u MBs (%u free) / ",  (int)(system_specs.physicalMemoryMax / (1024*1024)), (int)(system_specs.physicalMemoryAvailable / (1024*1024)) );
	else
		strcatf_s( buf, buf_size, "RAM: Unable to determine / " );

	if( system_specs.videoCardName[0] )
		strcatf_s( buf, buf_size, "Video Card: %s / ", system_specs.videoCardName );
	else
		strcatf_s( buf, buf_size, "Video Card: Unknown / " );

	if( system_specs.videoDriverVersion[0] )
		strcatf_s( buf, buf_size, "Driver Ver: %s / ", system_specs.videoDriverVersion );

	if( system_specs.virtualAddressSpace && !IsUsingX64() )
		strcatf_s( buf, buf_size, "UserVA: %1.1fGB / ",  ( system_specs.virtualAddressSpace/ (1024.f*1024*1024) ) );

	strcatf_s( buf, buf_size, "OS Ver: %d.%d.%d / ", system_specs.highVersion, system_specs.lowVersion, system_specs.build);

	if( system_specs.videoMemory)
		strcatf_s( buf, buf_size, "VidMem: %u MBs / ",  system_specs.videoMemory );
	else
		strcatf_s( buf, buf_size, "VidMem: Unknown / " );

	if (system_specs.supportedDXVersion)
		strcatf_s( buf, buf_size, "D3D11: %1.1f", system_specs.supportedDXVersion );
	else
		strcatf_s( buf, buf_size, "D3D11: Unknown" );

	strcatf_s( buf, buf_size, "\n");

	if (system_specs.videoDriverState != VIDEODRIVERSTATE_OK)
		strcat_s(buf, buf_size, "OUTDATED VIDEO CARD DRIVER\n"); // Do not change this string, it is hardcoded in CrypticCrashRpt.dll
	else
		strcat_s(buf, buf_size, "Acceptable video card driver\n");

}

void systemSpecsGetCSVString(char *buf, int buf_size)
{
	systemSpecsInit();

	buf[0] = 0;

	strcatf_s( SAFESTR2(buf), "Memory,%"FORM_LL"u, ",  system_specs.physicalMemoryMax / (1024*1024) );
	strcatf_s( SAFESTR2(buf), "AvailableMemory,%"FORM_LL"u, ",  system_specs.physicalMemoryAvailable / (1024*1024) );
	strcatf_s( SAFESTR2(buf), "UserVA,%"FORM_LL"u, ",  system_specs.virtualAddressSpace / (1024*1024) );
	strcatf_s( SAFESTR2(buf), "VideoCard,\"%s\", ", system_specs.videoCardName );
	strcatf_s( SAFESTR2(buf), "VideoCardVendorID,%d, ", system_specs.videoCardVendorID );
	strcatf_s( SAFESTR2(buf), "VideoCardDeviceID,%d, ", system_specs.videoCardDeviceID );
	strcatf_s( SAFESTR2(buf), "CPU,%0.0f, ", system_specs.CPUSpeed / 1000000.0 );
	strcatf_s( SAFESTR2(buf), "RAMSpeed,%0.1f, ", system_specs.RAMSpeedGBs );
	strcatf_s( SAFESTR2(buf), "NumCPUs,%d, ", system_specs.numVirtualCPUs );
	strcatf_s( SAFESTR2(buf), "NumRealCPUs,%d, ", system_specs.numRealCPUs );
	strcatf_s( SAFESTR2(buf), "CPUCacheSize,%d, ", system_specs.cpuCacheSize );
	strcatf_s( SAFESTR2(buf), "CPUIdentifier,\"%s\", ", system_specs.cpuIdentifier );
	strcatf_s( SAFESTR2(buf), "VideoDriverVersion,\"%s\", ", system_specs.videoDriverVersion );
	strcatf_s( SAFESTR2(buf), "VideoMemory,%d, ", system_specs.videoMemory );
	strcatf_s( SAFESTR2(buf), "OutdatedVideoCardDriver,%d, ", system_specs.videoDriverState );
	strcatf_s( SAFESTR2(buf), "IsUsingD3DDebug,%d, ", system_specs.isUsingD3DDebug );
	strcatf_s( SAFESTR2(buf), "OSVersion,\"%d.%d.%d\", ", system_specs.highVersion, system_specs.lowVersion, system_specs.build );
	strcatf_s( SAFESTR2(buf), "ServicePack,\"%d.%d\", ", system_specs.servicePackMajor, system_specs.servicePackMinor );
	strcatf_s( SAFESTR2(buf), "HostOSVersion,\"%s\", ", system_specs.hostOSversion);
//	strcatf_s( SAFESTR2(buf), "Bandwidth,\"%s\", ", system_specs.bandwidth );
	strcatf_s( SAFESTR2(buf), "AudioOutput,\"%s\", ", system_specs.audioDriverOutput );
	strcatf_s( SAFESTR2(buf), "NumMonitors,%d, ", system_specs.numMonitors );
	strcatf_s( SAFESTR2(buf), "GPUCount,%d, ", MAX(system_specs.atiCrossfireGPUCount, system_specs.nvidiaSLIGPUCount) );
	strcatf_s( SAFESTR2(buf), "IsRunningNortonAV,%d, ", system_specs.isRunningNortonAV);
	strcatf_s( SAFESTR2(buf), "IsX64,%d, ", system_specs.isX64 );
	strcatf_s( SAFESTR2(buf), "IsVista,%d, ", system_specs.isVista );
	strcatf_s( SAFESTR2(buf), "HasSSE3,%d, ", system_specs.hasSSE3 );
	strcatf_s( SAFESTR2(buf), "HasSSE4,%d, ", system_specs.hasSSE4 );
	strcatf_s( SAFESTR2(buf), "D3D11,%1.1f, ", system_specs.supportedDXVersion );
	strcatf_s( SAFESTR2(buf), "DiskFree,%"FORM_LL"u, ", system_specs.diskFree );
	strcatf_s( SAFESTR2(buf), "DiskTotal,%"FORM_LL"u, ", system_specs.diskTotal );
	strcatf_s( SAFESTR2(buf), "IsWine,%d, ", system_specs.isWine );
	if(system_specs.isWine)
		strcatf_s( SAFESTR2(buf), "WineVersion,%s, ", system_specs.wineVersion );
	if(sysspecs_other_trivia)
	{
		FOR_EACH_IN_EARRAY(sysspecs_other_trivia->triviaDatas, TriviaData, data)
			strcatf_s( SAFESTR2(buf), "%s,\"%s\", ", data->pKey, data->pVal );
		FOR_EACH_END
	}
	strcatf_s( SAFESTR2(buf), "Locale,\"%s\" ", locGetName(getCurrentLocale()) );
}


void systemSpecsGetNameValuePairString(char *buf, int buf_size)
{
	systemSpecsGetCSVString(buf, buf_size);
	strchrReplace(buf, ',', ' ');
}


// Saves the system specs string to logs/systemspecs.log
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE ACMD_ACCESSLEVEL(9) ACMD_APPSPECIFICACCESSLEVEL(GLOBALTYPE_CLIENT, 0);
void saveSystemSpecs(void)
{
	char filename[MAX_PATH];
	FILE *f;

	sprintf(filename, "%s/systemspecs.log", fileLogDir());
	f = fopen(filename, "wt");
	if (f)
	{
		char specs[2048];
		systemSpecsGetCSVString(SAFESTR(specs));
		fprintf(f, "%s\n", specs);
		fclose(f);
	}
}

AUTO_CMD_INT(system_specs.isDx11Enabled, d3d11Enable) ACMD_CMDLINE ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(system_specs.isDx9ExEnabled, d3d9exEnable) ACMD_CMDLINE ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0);
