/***************************************************************************



***************************************************************************/

#include "inputJoystick.h"
#include "inputJoystickInternal.h"
#include "inputGamepad.h"
#include "earray.h"
#include "EString.h"
#include "timing.h"
#include "structDefines.h"
#include "StringCache.h"
#include "StashTable.h"
#include "utils.h"
#include "GlobalTypes.h"
#include "UTF8.h"

#if !PLATFORM_CONSOLE
#include <Setupapi.h>
#include <Dbt.h>
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct JoystickEnumContext {
	LPDIRECTINPUTDEVICE8	*JoystickDevs;
	InputJoystick			**Joysticks;
	LPDIRECTINPUTDEVICE8	JoystickDev;
	InputJoystick			*Joystick;
} JoystickEnumContext;

bool g_bDebugDirectInputJoystick = false;
AUTO_CMD_INT(g_bDebugDirectInputJoystick, Input_DIJDebug) ACMD_CATEGORY(Debug);
bool g_bDebugDirectInputLock = false;
AUTO_CMD_INT(g_bDebugDirectInputLock, Input_DIJLock) ACMD_CATEGORY(Debug);

typedef struct CachedJoystickMapping {
	const char *pchGuidDevice;
	InputJoystick *pJoystick;
	InputJoystickPhysical ePhysicalInput;
	InputJoystickLogical eLogicalOutput;
} CachedJoystickMapping;

typedef struct CachedLogicalState {
	F32 fAnalogValue;
	InputJoystick *pSource;
	bool bAnalog : 1;
	bool bDigitalValue : 1;
} CachedLogicalState;

static InputJoystick s_AnyJoystick;
#define IsAnyJoystick(pJoystick) ((pJoystick) == &s_AnyJoystick)

static int s_iMappingProfiles;
static CachedJoystickMapping *s_aMapping;
static bool s_bProcessingEnabled = true;
static S32 s_iJoystickWasToldToTryAgain = 0;
static joystickCrashHandlerCB s_cbCrashHandler;

static CachedLogicalState s_LogicalState[kInputJoystickLogical_MAX];

// Read up on HID Class Definitions at http://www.usb.org/developers/hidpage
// Currently only used for debugging, but could possibly be used for InputJoystickProfile

AUTO_ENUM;
typedef enum HIDUsagePage {
	kHIDUsagePage_GenericDesktopControls				= 0x01,
	kHIDUsagePage_SimulationControls					= 0x02,
	kHIDUsagePage_GameControls							= 0x05,
	kHIDUsagePage_Button								= 0x09,
} HIDUsagePage;

AUTO_ENUM;
typedef enum HIDGenericDesktopControlsUsage {
	kHIDGenericDesktopControlsUsage_Pointer				= 0x01,
	kHIDGenericDesktopControlsUsage_Mouse				= 0x02,
	kHIDGenericDesktopControlsUsage_Joystick			= 0x04,
	kHIDGenericDesktopControlsUsage_GamePad				= 0x05,
	kHIDGenericDesktopControlsUsage_Keyboard			= 0x06,
	kHIDGenericDesktopControlsUsage_Keypad				= 0x07,
	kHIDGenericDesktopControlsUsage_MultiAxis			= 0x08,
	kHIDGenericDesktopControlsUsage_TabletPC			= 0x09,

	kHIDGenericDesktopControlsUsage_AxisX				= 0x30,
	kHIDGenericDesktopControlsUsage_AxisY				= 0x31,
	kHIDGenericDesktopControlsUsage_AxisZ				= 0x32,
	kHIDGenericDesktopControlsUsage_AxisRx				= 0x33,
	kHIDGenericDesktopControlsUsage_AxisRy				= 0x34,
	kHIDGenericDesktopControlsUsage_AxisRz				= 0x35,
	kHIDGenericDesktopControlsUsage_Slider				= 0x36,
	kHIDGenericDesktopControlsUsage_Dial				= 0x37,
	kHIDGenericDesktopControlsUsage_Wheel				= 0x38,
	kHIDGenericDesktopControlsUsage_Dpad				= 0x39,
	kHIDGenericDesktopControlsUsage_Start				= 0x3D,
	kHIDGenericDesktopControlsUsage_Select				= 0x3E,

	kHIDGenericDesktopControlsUsage_DpadUp				= 0x90,
	kHIDGenericDesktopControlsUsage_DpadDown			= 0x91,
	kHIDGenericDesktopControlsUsage_DpadRight			= 0x92,
	kHIDGenericDesktopControlsUsage_DpadLeft			= 0x93,
} HIDGenericDesktopControlsUsage;

AUTO_ENUM;
typedef enum HIDSimulationControlsUsage {
	kHIDSimulationControlsUsage_FlightSimulation		= 0x01,
	kHIDSimulationControlsUsage_AutoSimulation			= 0x02,
	kHIDSimulationControlsUsage_TankSimulation			= 0x03,
	kHIDSimulationControlsUsage_SpaceShipSimulation		= 0x04,
	kHIDSimulationControlsUsage_SubmarineSimulation		= 0x05,
	kHIDSimulationControlsUsage_SailingSimulation		= 0x06,
	kHIDSimulationControlsUsage_MotorcycleSimulation	= 0x07,
	kHIDSimulationControlsUsage_SportsSimulation		= 0x08,
	kHIDSimulationControlsUsage_AirplaneSimulation		= 0x09,
	kHIDSimulationControlsUsage_HelicopterSimulation	= 0x0A,
	kHIDSimulationControlsUsage_MagicCarpetSimulation	= 0x0B,
	kHIDSimulationControlsUsage_BicycleSimulation		= 0x0C,

	kHIDSimulationControlsUsage_FlightControlStick		= 0x20,
	kHIDSimulationControlsUsage_FlightStick				= 0x21,
	kHIDSimulationControlsUsage_CyclicControl			= 0x22,
	kHIDSimulationControlsUsage_CyclicTrim				= 0x23,
	kHIDSimulationControlsUsage_FlightYoke				= 0x24,

	kHIDSimulationControlsUsage_Elevator				= 0xB8,
	kHIDSimulationControlsUsage_ElevatorTrim			= 0xB9,
	kHIDSimulationControlsUsage_Rudder					= 0xBA,
	kHIDSimulationControlsUsage_Throttle				= 0xBB,

	kHIDSimulationControlsUsage_Trigger					= 0xC0,

	kHIDSimulationControlsUsage_Accelerator				= 0xC4,
	kHIDSimulationControlsUsage_Brake					= 0xC5,
	kHIDSimulationControlsUsage_Shifter					= 0xC7,
	kHIDSimulationControlsUsage_Steering				= 0xC8,
} HIDSimulationControlsUsage;

AUTO_ENUM;
typedef enum HIDGameControlsUsage {
	kHIDGameControlsUsage_AxisRx						= 0x21, // Turn Right/Left (Yaw)
	kHIDGameControlsUsage_AxisRy						= 0x22, // Pitch Forward/Backward (Pitch)
	kHIDGameControlsUsage_AxisRz						= 0x23, // Roll Right/Left (Roll)
	kHIDGameControlsUsage_AxisX							= 0x24, // Move Right/Left (Strafe)
	kHIDGameControlsUsage_AxisY							= 0x25, // Move Forward/Backward
	kHIDGameControlsUsage_AxisZ							= 0x26, // Move Up/Down
	kHIDGameControlsUsage_AxisLeanX						= 0x27, // Lean Right/Left
	kHIDGameControlsUsage_AxisLeanY						= 0x28, // Lean Forward/Backward
	kHIDGameControlsUsage_HeightPOV						= 0x29, // Stand/Crouch/Tiptoes/Flying

	kHIDGameControlsUsage_Button						= 0x37,
	kHIDGameControlsUsage_Trigger						= 0x39,
} HIDGameControlsUsage;

AUTO_ENUM;
typedef enum HIDButtonUsage {
	kHIDButtonUsage_AB									= 0x01,
	kHIDButtonUsage_BB									= 0x02,
	kHIDButtonUsage_XB									= 0x03,
	kHIDButtonUsage_YB									= 0x04,
} HIDButtonUsage;

extern StaticDefineInt HIDUsagePageEnum[];
extern StaticDefineInt HIDGenericDesktopControlsUsageEnum[];
extern StaticDefineInt HIDSimulationControlsUsageEnum[];
extern StaticDefineInt HIDGameControlsUsageEnum[];
extern StaticDefineInt HIDButtonUsageEnum[];

static GUID s_HIDClass;
static HDEVNOTIFY s_hNotification;
static bool s_bDeviceChangeNotify;
static bool s_bForceEnumDevices;
static bool s_bHardwareChanged;
static const char **s_eaHIDDevices;
static HWND s_hwndNotifyWindow;

#if !PLATFORM_CONSOLE
static BOOL (WINAPI *pSetupDiEnumDeviceInterfaces)(HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA DeviceInfoData, const GUID *InterfaceClassGuid, DWORD MemberIndex, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData);
static HDEVINFO (WINAPI *pSetupDiGetClassDevs)(const GUID *ClassGuid, PCSTR Enumerator, HWND hwndParent, DWORD Flags);
static BOOL (WINAPI *pSetupDiDestroyDeviceInfoList)(HDEVINFO DeviceInfoSet);
static BOOL (WINAPI *pSetupDiGetDeviceInterfaceDetail)(HDEVINFO DeviceInfoSet, PSP_DEVICE_INTERFACE_DATA DeviceInterfaceData, PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData, DWORD DeviceInterfaceDetailDataSize, PDWORD RequiredSize, PSP_DEVINFO_DATA DeviceInfoData);
#endif

#if !PLATFORM_CONSOLE
static LRESULT WINAPI JoystickNotify(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
#if 0
	char szName[50];
	if (Msg < 0xc000 || Msg > 0xffff || !GetClipboardFormatName(Msg, szName, sizeof(szName)))
		strcpy(szName, "");
	OutputDebugStringf("CJ Wnd %p 0x%04x '%s' %x %x\n", hWnd, Msg, szName, wParam, lParam);
#endif

	switch (Msg)
	{
	xcase WM_DEVICECHANGE:
		JoystickMonitorDeviceChange(wParam, (DEV_BROADCAST_DEVICEINTERFACE *) lParam);
		return 0;
	}

	return DefWindowProc(hWnd, Msg, wParam, lParam);
}
#endif

static bool CreateNotifyWindow(void)
{
	static bool s_bRegistered = false;
	WNDCLASS wndclass = {0};
	wndclass.lpfnWndProc = JoystickNotify;
	wndclass.lpszClassName = L"cryptic-joystick";

#if !PLATFORM_CONSOLE
	if (!s_bRegistered)
	{
		if (RegisterClass(&wndclass) == 0)
		{
			return false;
		}
		s_bRegistered = true;
	}

	if (!s_hwndNotifyWindow)
	{
		s_hwndNotifyWindow = CreateWindow(
			L"cryptic-joystick",
			L"cryptic-joystick",
			WS_OVERLAPPEDWINDOW | WS_DISABLED,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			NULL,
			NULL,
			NULL,
			NULL);
	}
#endif

	return s_hwndNotifyWindow != NULL;
}

void JoystickMonitorDeviceChange(WPARAM wParam, DEV_BROADCAST_DEVICEINTERFACE *pDev)
{
	if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE || wParam == DBT_DEVNODES_CHANGED)
	{
		// A new device was detached/attached, make sure to reenumerate the devices.
		s_bForceEnumDevices = true;
		s_bHardwareChanged = true;
	}

	if (wParam != DBT_DEVICEARRIVAL && wParam != DBT_DEVICEREMOVECOMPLETE)
		return;

	if (!pDev || pDev->dbcc_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
		return;

	if (g_bDebugDirectInputJoystick)
	{
		char *pTempDbccName = NULL;
		UTF16ToEstring(pDev->dbcc_name, 0, &pTempDbccName);
		printf("HIDClass: %s: %s\n", (wParam == DBT_DEVICEARRIVAL ? "Attach" : wParam == DBT_DEVICEREMOVECOMPLETE ? "Detach" : "Unknown"), pTempDbccName);
		estrDestroy(&pTempDbccName);
	}
}

void JoystickStartup(void)
{
	// Get the HIDClass GUID
	{
		void (WINAPI *pHidD_GetHidGuid)(LPGUID HidGuid);
		HMODULE hHIDdll = LoadLibrary(L"Hid.dll");
		if (hHIDdll)
		{
			ANALYSIS_ASSUME(hHIDdll);
			*(FARPROC *)&pHidD_GetHidGuid = GetProcAddress(hHIDdll, "HidD_GetHidGuid");
			if (pHidD_GetHidGuid)
				pHidD_GetHidGuid(&s_HIDClass);
			FreeLibrary(hHIDdll);
		}
	}

	// Get setup API functions
#if !PLATFORM_CONSOLE
	{
		HMODULE hSetupAPIdll = LoadLibrary(L"Setupapi.dll");

		if (hSetupAPIdll)
		{
			ANALYSIS_ASSUME(hSetupAPIdll);
			*(FARPROC *)&pSetupDiEnumDeviceInterfaces = GetProcAddress(hSetupAPIdll, "SetupDiEnumDeviceInterfaces");
			*(FARPROC *)&pSetupDiGetClassDevs = GetProcAddress(hSetupAPIdll, "SetupDiGetClassDevsA");
			*(FARPROC *)&pSetupDiDestroyDeviceInfoList = GetProcAddress(hSetupAPIdll, "SetupDiDestroyDeviceInfoList");
			*(FARPROC *)&pSetupDiGetDeviceInterfaceDetail = GetProcAddress(hSetupAPIdll, "SetupDiGetDeviceInterfaceDetailA");
		}
	}
#endif

	// Monitor for attached/detached joysticks
	if (s_HIDClass.Data1)
	{
		DEV_BROADCAST_DEVICEINTERFACE dev;
		ZeroMemory(&dev, sizeof(dev));
		dev.dbcc_size = sizeof(dev);
		dev.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		dev.dbcc_classguid = s_HIDClass;

		if (CreateNotifyWindow())
			s_hNotification = RegisterDeviceNotification(s_hwndNotifyWindow, &dev, DEVICE_NOTIFY_WINDOW_HANDLE);
	}

	// Make sure to enumerate the devices as soon as possible
	s_bForceEnumDevices = true;
	s_bHardwareChanged = true;

	// Load HID tables here?
}

void JoystickShutdown(void)
{
	if (s_hNotification)
	{
		UnregisterDeviceNotification(s_hNotification);
		s_hNotification = NULL;
	}
}

// The following three functions are to work around a crash that
// a particular device causes in EnumDevices()
//
// Device ID:	0D2F:0001 "Andamiro Andamiro Pump it up!"
// See: http://discussms.hosting.lsoft.com/SCRIPTS/WA-MSD.EXE?A2=ind0805C&L=directxdev&F=&S=&P=81
//
// The workaround is to manually enumerate devices, then lock them
// to prevent DirectInput from looking at them.
//
// However, if those devices aren't the cause, then this information
// can also be used to hunt down other problematic devices.
static bool JoystickEnumDevices(S32 iEnumLimit)
{
	S32 iEnumerated = 0;

#if !PLATFORM_CONSOLE
	static HDEVINFO s_hDevInfo;
	static S32 s_iNumDevice;
	S32 i;

	// Make sure all the functions were found...
	if (!(pSetupDiEnumDeviceInterfaces && pSetupDiGetClassDevs &&
		pSetupDiDestroyDeviceInfoList && pSetupDiGetDeviceInterfaceDetail))
	{
		return false;
	}

	PERFINFO_AUTO_START_FUNC();

	if (!s_hDevInfo)
		s_hDevInfo = pSetupDiGetClassDevs(&s_HIDClass, NULL, NULL, (DIGCF_DEVICEINTERFACE | DIGCF_PRESENT));

	for (i = 0; i < iEnumLimit && s_hDevInfo; i++)
	{
		SP_DEVICE_INTERFACE_DATA DIData;
		ZeroMemory(&DIData, sizeof(DIData));
		DIData.cbSize = sizeof(DIData);

		if (pSetupDiEnumDeviceInterfaces(s_hDevInfo, NULL, &s_HIDClass, s_iNumDevice++, &DIData))
		{
			DWORD dwSize = 0;

			pSetupDiGetDeviceInterfaceDetail(s_hDevInfo, &DIData, NULL, 0, &dwSize, NULL);
			if (dwSize > 0)
			{
				SP_INTERFACE_DEVICE_DETAIL_DATA *pIDDetail = (SP_INTERFACE_DEVICE_DETAIL_DATA *)malloc(dwSize);
				SP_DEVINFO_DATA DevData;

				if (pIDDetail)
				{
					ZeroMemory(pIDDetail, dwSize);
					ZeroMemory(&DevData, sizeof(DevData));
					pIDDetail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
					DevData.cbSize = sizeof(DevData);

					if (pSetupDiGetDeviceInterfaceDetail(s_hDevInfo, &DIData, pIDDetail, dwSize, NULL, &DevData))
					{
						const char *pchDevicePath = allocAddString_UTF16ToUTF8(pIDDetail->DevicePath);
						eaPushUnique(&s_eaHIDDevices, pchDevicePath);
						if (g_bDebugDirectInputJoystick)
							printf("HIDClass: %s\n", pchDevicePath);
						iEnumerated++;
					}

					free(pIDDetail);
				}
			}
		}
		else
		{
			if (GetLastError() != ERROR_NO_MORE_ITEMS && g_bDebugDirectInputJoystick)
			{
				devassertmsgf(GetLastError() != ERROR_NO_MORE_ITEMS, "SetupDiEnumDeviceInterfaces() failed: %d\n", GetLastError());
			}

			// hit the end, restart the enumeration
			pSetupDiDestroyDeviceInfoList(s_hDevInfo);
			s_hDevInfo = NULL;
			s_iNumDevice = 0;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
#endif
	return iEnumerated > 0;
}

static HANDLE JoystickLockDevice(const char *pchDevicePath)
{
	HANDLE hDevice = NULL;

#if !PLATFORM_CONSOLE
	S32 iVID = 0, iPID = 0;
	char *pch;

	// Check for VID_0D2F&PID_0001
	pch = strstri(pchDevicePath, "vid_");
	if (pch) sscanf(pch + 4, "%04X", &iVID);
	pch = strstri(pchDevicePath, "pid_");
	if (pch) sscanf(pch + 4, "%04X", &iPID);

	if ((iVID == 0x0D2F && iPID == 0x0001) ||

		// If debugging, lock out the Logitech RumblePad 2
		(g_bDebugDirectInputLock && iVID == 0x046D && iPID == 0xC218))
	{
		// Lock device
		hDevice = CreateFile_UTF8(pchDevicePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (hDevice == INVALID_HANDLE_VALUE)
			return NULL;
	}

#endif

	return hDevice;
}

static void JoystickUnlockDevice(HANDLE hDevice)
{
#if !PLATFORM_CONSOLE
	CloseHandle(hDevice);
#endif
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void Input_DIJEnumDevices(void)
{
	JoystickEnumDevices(INT_MAX);
}

//-----------------------------------------------------------------------------
// Enum each PNP device using WMI and check each device ID to see if it contains 
// "IG_" (ex. "VID_045E&PID_028E&IG_00").  If it does, then it's an XInput device
// Unfortunately this information can not be found by just using DirectInput 
//-----------------------------------------------------------------------------
//

#define SAFE_RELEASE(p) {if(p){(p)->lpVtbl->Release(p);(p)=NULL;}}

bool IsXInputDevice( const GUID* pGuidProductFromDirectInput )
{
#if PLATFORM_CONSOLE
	return false;
#else
#pragma comment(lib, "wbemuuid.lib" )
#include <wbemidl.h>
#include <wbemcli.h>

	IWbemLocator*           pIWbemLocator  = NULL;
	IEnumWbemClassObject*   pEnumDevices   = NULL;
	IWbemClassObject*       pDevices[20]   = {0};
	IWbemServices*          pIWbemServices = NULL;
	BSTR                    bstrNamespace  = NULL;
	BSTR                    bstrDeviceID   = NULL;
	BSTR                    bstrClassName  = NULL;
	DWORD                   uReturned      = 0;
	bool                    bIsXinputDevice= false;
	UINT                    iDevice        = 0;
	VARIANT                 var;
	HRESULT                 hr;
	bool					bCleanupCOM;

	PERFINFO_AUTO_START_FUNC();

	// CoInit if needed
	hr = CoInitialize(NULL);
	bCleanupCOM = SUCCEEDED(hr);

	// Create WMI
	// IWbemLocator "dc12a687-737f-11cf-884d-00aa004b2e24"
	// WbemLocator  "4590f811-1d3a-11d0-891f-00aa004b2e24"
	hr = CoCreateInstance( &CLSID_WbemLocator,NULL,CLSCTX_INPROC_SERVER,&IID_IWbemLocator,(LPVOID*) &pIWbemLocator);
	if( FAILED(hr) || pIWbemLocator == NULL )
		goto LCleanup;

	bstrNamespace = SysAllocString( L"\\\\.\\root\\cimv2" );if( bstrNamespace == NULL ) goto LCleanup;        
	bstrClassName = SysAllocString( L"Win32_PNPEntity" );   if( bstrClassName == NULL ) goto LCleanup;        
	bstrDeviceID  = SysAllocString( L"DeviceID" );          if( bstrDeviceID == NULL )  goto LCleanup;        

	// Connect to WMI 
	hr = pIWbemLocator->lpVtbl->ConnectServer( pIWbemLocator, bstrNamespace, NULL, NULL, 0L, 0L, NULL, NULL, &pIWbemServices );
	if( FAILED(hr) || pIWbemServices == NULL )
		goto LCleanup;

	// Switch security level to IMPERSONATE. 
	hr = CoSetProxyBlanket( (IUnknown*)pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, 
		RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE );                    
	if( FAILED(hr) )
		goto LCleanup;

	hr = pIWbemServices->lpVtbl->CreateInstanceEnum( pIWbemServices, bstrClassName, 0, NULL, &pEnumDevices ); 
	if( FAILED(hr) || pEnumDevices == NULL )
		goto LCleanup;

	// Loop over all devices
	for( ;; )
	{
		// Get 20 at a time
		hr = pEnumDevices->lpVtbl->Next( pEnumDevices, 10000, 20, pDevices, &uReturned );
		if( FAILED(hr) )
			goto LCleanup;
		if( uReturned == 0 )
			break;

		for( iDevice=0; iDevice<uReturned; iDevice++ )
		{
			// For each device, get its device ID
			if (!pDevices[iDevice])
				continue;
			hr = pDevices[iDevice]->lpVtbl->Get( pDevices[iDevice], bstrDeviceID, 0L, &var, NULL, NULL );
			if( SUCCEEDED( hr ) && var.vt == VT_BSTR && var.bstrVal != NULL )
			{
				// Check if the device ID contains "IG_".  If it does, then it's an XInput device
				// This information can not be found from DirectInput 
				if( wcsstr( var.bstrVal, L"IG_" ) )
				{
					// If it does, then get the VID/PID from var.bstrVal
					DWORD dwPid = 0, dwVid = 0, dwVidPid;
					WCHAR *strPid, *strVid;

					strVid = wcsstr( var.bstrVal, L"VID_" );
					if( strVid && swscanf( strVid, L"VID_%4X", &dwVid ) != 1 )
						dwVid = 0;
					strPid = wcsstr( var.bstrVal, L"PID_" );
					if( strPid && swscanf( strPid, L"PID_%4X", &dwPid ) != 1 )
						dwPid = 0;

					// Compare the VID/PID to the DInput device
					dwVidPid = MAKELONG( dwVid, dwPid );
					if( dwVidPid == pGuidProductFromDirectInput->Data1 )
					{
						bIsXinputDevice = true;
						goto LCleanup;
					}
				}
			}
			SAFE_RELEASE( pDevices[iDevice] );
		}
	}

LCleanup:
	if(bstrNamespace)
		SysFreeString(bstrNamespace);
	if(bstrDeviceID)
		SysFreeString(bstrDeviceID);
	if(bstrClassName)
		SysFreeString(bstrClassName);
	for( iDevice=0; iDevice<20; iDevice++ )
		SAFE_RELEASE( pDevices[iDevice] );
	SAFE_RELEASE( pEnumDevices );
	SAFE_RELEASE( pIWbemLocator );
	SAFE_RELEASE( pIWbemServices );

	if( bCleanupCOM )
		CoUninitialize();

	PERFINFO_AUTO_STOP();

	return bIsXinputDevice;
#endif
}

static BOOL CALLBACK EnumJoysticksCallback( const DIDEVICEINSTANCE* pdidInstance,	JoystickEnumContext* pContext )
{
	HRESULT hr;
	int i;
	InputJoystick *pJoystick = NULL;
	LPDIRECTINPUTDEVICE8 pDevice = NULL;
	char achGuidDevice[42];
	const char *pchGuidDevice = NULL;
	static StashTable pTable = NULL;
	int iXInputDevice = false;

	if (!pContext || !gInput->DirectInput8)
		return DIENUM_STOP;

	sprintf(achGuidDevice, "{%08X-%04X-%04x-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		pdidInstance->guidInstance.Data1,
		pdidInstance->guidInstance.Data2,
		pdidInstance->guidInstance.Data3,
		pdidInstance->guidInstance.Data4[0],
		pdidInstance->guidInstance.Data4[1],
		pdidInstance->guidInstance.Data4[2],
		pdidInstance->guidInstance.Data4[3],
		pdidInstance->guidInstance.Data4[4],
		pdidInstance->guidInstance.Data4[5],
		pdidInstance->guidInstance.Data4[6],
		pdidInstance->guidInstance.Data4[7]);
	pchGuidDevice = allocAddString(achGuidDevice);

	if (!pTable)
		pTable = stashTableCreateWithStringKeys(1, StashDefault);

	if (!gConf.bXboxControllersAreJoysticks)
	{
		if (!stashFindInt(pTable, pchGuidDevice, &iXInputDevice))
		{
			iXInputDevice = IsXInputDevice(&pdidInstance->guidProduct);
			stashAddInt(pTable, pchGuidDevice, iXInputDevice, false);
		}
	}

	if (iXInputDevice)
	{
		return DIENUM_CONTINUE;
	}
	else
	{
		// Device is verified not XInput, so add it to the list of DInput devices
		for (i = eaSize(&gInput->eaJoysticks) - 1; i >= 0; i--)
		{
			if (gInput->eaJoysticks[i]->pchGuidDevice == pchGuidDevice)
			{
				pJoystick = gInput->eaJoysticks[i];
				pDevice = gInput->eaJoystickDevs[i];
				break;
			}
		}

		if (!pDevice)
		{
			// Obtain an interface to the enumerated joystick.
			hr = IDirectInput8_CreateDevice( gInput->DirectInput8, &pdidInstance->guidInstance, &pDevice, NULL );
			if (FAILED(hr))
				return DIENUM_CONTINUE;
		}

		if (!pJoystick)
		{
			pJoystick = calloc(1, sizeof(InputJoystick));
			pJoystick->pchGuidDevice = pchGuidDevice;
#if !PLATFORM_CONSOLE
			pJoystick->pchName = allocAddString_UTF16ToUTF8(pdidInstance->tszProductName);
#else
			pJoystick->pchName = allocAddString("Console Platform Joystick");
#endif
		}

		eaPush(&pContext->JoystickDevs, pDevice);
		eaPush(&pContext->Joysticks, pJoystick);

		if (g_bDebugDirectInputJoystick)
		{
			printf("Added Device: %s\n", achGuidDevice);
		}

		// Find all Joysticks
		return DIENUM_CONTINUE;
	}
}

static BOOL CALLBACK EnumObjectsCallback( const DIDEVICEOBJECTINSTANCE* pdidoi,  JoystickEnumContext* pContext )
{
	// For axes that are returned, set the DIPROP_RANGE property for the
	// enumerated axis in order to scale min/max values.
	if( pdidoi->dwType & DIDFT_AXIS )
	{
		DIPROPRANGE diprg; 
		diprg.diph.dwSize       = sizeof(DIPROPRANGE); 
		diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER); 
		diprg.diph.dwHow        = DIPH_BYID; 
		diprg.diph.dwObj        = pdidoi->dwType; // Specify the enumerated axis
		diprg.lMin              = -32767; 
		diprg.lMax              = +32767; 

		// Set the range for the axis
		if( FAILED( IDirectInputDevice8_SetProperty( pContext->JoystickDev, DIPROP_RANGE, &diprg.diph ) ) ) 
			return DIENUM_STOP;
	}

	if (g_bDebugDirectInputJoystick)
	{
		static char *estrObjectInstance = NULL;
		char pchName[512] = {0};
		StaticDefineInt *pList = NULL;
#if !PLATFORM_CONSOLE
		if (StaticDefineIntRevLookup(HIDUsagePageEnum, pdidoi->wUsagePage))
		{
			sprintf(pchName, "HID%sUsage", StaticDefineIntRevLookup(HIDUsagePageEnum, pdidoi->wUsagePage));
			pList = FindNamedStaticDefine(pchName);
		}
		estrClear(&estrObjectInstance);
		estrConcatf(&estrObjectInstance, "<GUID> = ");
		if (!memcmp(&pdidoi->guidType, &GUID_XAxis, sizeof(GUID))) estrConcatf(&estrObjectInstance, "X Axis ");
		if (!memcmp(&pdidoi->guidType, &GUID_YAxis, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Y Axis ");
		if (!memcmp(&pdidoi->guidType, &GUID_ZAxis, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Z Axis ");
		if (!memcmp(&pdidoi->guidType, &GUID_RxAxis, sizeof(GUID))) estrConcatf(&estrObjectInstance, "X Rotation ");
		if (!memcmp(&pdidoi->guidType, &GUID_RyAxis, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Y Rotation ");
		if (!memcmp(&pdidoi->guidType, &GUID_RzAxis, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Z Rotation ");
		if (!memcmp(&pdidoi->guidType, &GUID_Slider, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Slider ");
		if (!memcmp(&pdidoi->guidType, &GUID_Button, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Button ");
		if (!memcmp(&pdidoi->guidType, &GUID_Key, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Key ");
		if (!memcmp(&pdidoi->guidType, &GUID_POV, sizeof(GUID))) estrConcatf(&estrObjectInstance, "POV ");
		if (!memcmp(&pdidoi->guidType, &GUID_Unknown, sizeof(GUID))) estrConcatf(&estrObjectInstance, "Unknown ");
		{
			char *pTempName = NULL;
			UTF16ToEstring(pdidoi->tszName, 0, &pTempName);
			estrConcatf(&estrObjectInstance, " (Name: %s)\n", pTempName);
			estrDestroy(&pTempName);
		}
		estrConcatf(&estrObjectInstance, "\tdwType = ");
		if (pdidoi->dwType & DIDFT_ABSAXIS) estrConcatf(&estrObjectInstance, "DIDFT_ABSAXIS(%d) ", DIDFT_GETINSTANCE(pdidoi->dwType));
		if (pdidoi->dwType & DIDFT_ALIAS) estrConcatf(&estrObjectInstance, "DIDFT_ALIAS ");
		if (pdidoi->dwType & DIDFT_COLLECTION) estrConcatf(&estrObjectInstance, "DIDFT_COLLECTION(%d) ", DIDFT_GETINSTANCE(pdidoi->dwType));
		if (pdidoi->dwType & DIDFT_FFACTUATOR) estrConcatf(&estrObjectInstance, "DIDFT_FFACTUATOR ");
		if (pdidoi->dwType & DIDFT_FFEFFECTTRIGGER) estrConcatf(&estrObjectInstance, "DIDFT_FFEFFECTTRIGGER ");
		if (pdidoi->dwType & DIDFT_POV) estrConcatf(&estrObjectInstance, "DIDFT_POV(%d) ", DIDFT_GETINSTANCE(pdidoi->dwType));
		if (pdidoi->dwType & DIDFT_PSHBUTTON) estrConcatf(&estrObjectInstance, "DIDFT_PSHBUTTON(%d) ", DIDFT_GETINSTANCE(pdidoi->dwType));
		if (pdidoi->dwType & DIDFT_TGLBUTTON) estrConcatf(&estrObjectInstance, "DIDFT_TGLBUTTON(%d) ", DIDFT_GETINSTANCE(pdidoi->dwType));
		if (pdidoi->dwType & DIDFT_RELAXIS) estrConcatf(&estrObjectInstance, "DIDFT_RELAXIS(%d) ", DIDFT_GETINSTANCE(pdidoi->dwType));
		estrConcatf(&estrObjectInstance, "0x%08X\n", pdidoi->dwType);
		estrConcatf(&estrObjectInstance, "\twDesignatorIndex = %02X\n", pdidoi->wDesignatorIndex);
		estrConcatf(&estrObjectInstance, "\tPage/Usage = %02X / %02X : %s / %s\n",
			pdidoi->wUsagePage, pdidoi->wUsage,
			// Known labels for the types usage information
			StaticDefineIntRevLookup(HIDUsagePageEnum, pdidoi->wUsagePage),
			pList ? StaticDefineIntRevLookup(pList, pdidoi->wUsage) : NULL);
		estrConcatf(&estrObjectInstance, "\tCollection = %02X\n", pdidoi->wCollectionNumber);
		printf("%s", estrObjectInstance);
#endif
	}

	return DIENUM_CONTINUE;
}

static void JoystickDisconnect(LPDIRECTINPUTDEVICE8 JoystickDev)
{
	if(JoystickDev)
	{
		// Unacquire the device one last time just in case 
		IDirectInputDevice8_Unacquire(JoystickDev);

		// Release DirectInput objects.
		IDirectInputDevice8_Release(JoystickDev);
	}
}

static InputJoystick *JoystickGetByGUID(const char *pchGUID)
{
	int j;
	if (pchGUID && *pchGUID)
	{
		for (j = 0; j < eaSize(&gInput->eaJoysticks); j++)
			if (pchGUID == gInput->eaJoysticks[j]->pchGuidDevice)
				return gInput->eaJoysticks[j];
		for (j = 0; j < eaSize(&gInput->eaJoysticks); j++)
			if (!stricmp(pchGUID, gInput->eaJoysticks[j]->pchGuidDevice))
				return gInput->eaJoysticks[j];
		return NULL;
	}
	return &s_AnyJoystick;
}

static void JoystickUpdateCache(void)
{
	int i;

	// Update the references to an actual InputJoystick
	for (i = 0; i < s_iMappingProfiles; i++)
		s_aMapping[i].pJoystick = JoystickGetByGUID(s_aMapping[i].pchGuidDevice);
}

int dxJoystickToInpKeyMapping[][3] = {
	{DIJOFS_BUTTON1, INP_JOY1, INP_DJOY1},
	{DIJOFS_BUTTON2, INP_JOY2, INP_DJOY2},
	{DIJOFS_BUTTON3, INP_JOY3, INP_DJOY3},
	{DIJOFS_BUTTON4, INP_JOY4, INP_DJOY4},
	{DIJOFS_BUTTON5, INP_JOY5, INP_DJOY5},
	{DIJOFS_BUTTON6, INP_JOY6, INP_DJOY6},
	{DIJOFS_BUTTON7, INP_JOY7, INP_DJOY7},
	{DIJOFS_BUTTON8, INP_JOY8, INP_DJOY8},
	{DIJOFS_BUTTON9, INP_JOY9, INP_DJOY9},
	{DIJOFS_BUTTON10, INP_JOY10, INP_DJOY10},
	{DIJOFS_BUTTON11, INP_JOY11, INP_DJOY11},
	{DIJOFS_BUTTON12, INP_JOY12, INP_DJOY12},
	{DIJOFS_BUTTON13, INP_JOY13, INP_DJOY13},
	{DIJOFS_BUTTON14, INP_JOY14, INP_DJOY14},
	{DIJOFS_BUTTON15, INP_JOY15, INP_DJOY15},
	{DIJOFS_BUTTON16, INP_JOY16, INP_DJOY16},
	{DIJOFS_BUTTON17, INP_JOY17, INP_DJOY17},
	{DIJOFS_BUTTON18, INP_JOY18, INP_DJOY18},
	{DIJOFS_BUTTON19, INP_JOY19, INP_DJOY19},
	{DIJOFS_BUTTON20, INP_JOY20, INP_DJOY20},
	{DIJOFS_BUTTON21, INP_JOY21, INP_DJOY21},
	{DIJOFS_BUTTON22, INP_JOY22, INP_DJOY22},
	{DIJOFS_BUTTON23, INP_JOY23, INP_DJOY23},
	{DIJOFS_BUTTON24, INP_JOY24, INP_DJOY24},
	{DIJOFS_BUTTON25, INP_JOY25, INP_DJOY25},
};

int dxPovToInpKeyMapping[][1] = {
	{INP_JOYPAD_UP},
	{INP_JOYPAD_DOWN},
	{INP_JOYPAD_LEFT},
	{INP_JOYPAD_RIGHT},
	{INP_POV1_UP},
	{INP_POV1_DOWN},
	{INP_POV1_LEFT},
	{INP_POV1_RIGHT},
	{INP_POV2_UP},
	{INP_POV2_DOWN},
	{INP_POV2_LEFT},
	{INP_POV2_RIGHT},
	{INP_POV3_UP},
	{INP_POV3_DOWN},
	{INP_POV3_LEFT},
	{INP_POV3_RIGHT},
};

static HRESULT inpJoystickUpdate(LPDIRECTINPUTDEVICE8 JoystickDev, InputJoystick *pJoystick)
{
	HRESULT     hr;
	DIJOYSTATE2 js;           // DInput joystick state 
	SHORT sX;
	SHORT sY;
	int i;
	const S32 curTime = inpGetTime();

	static int joystick_sensitivity = 16383;

	PERFINFO_AUTO_START_FUNC();

	// Poll the device to read the current state
	PERFINFO_AUTO_START("Poll",1);
		hr = IDirectInputDevice8_Poll(JoystickDev); 
	PERFINFO_AUTO_STOP();

	if(FAILED(hr))
	{
		// DInput is telling us that the input stream has been
		// interrupted. We aren't tracking any state between polls, so
		// we don't have any special reset that needs to be done. We
		// just re-acquire and try again.
		PERFINFO_AUTO_START("Acquire",1);
		hr = IDirectInputDevice8_Acquire(JoystickDev);
		PERFINFO_AUTO_STOP();

		if (FAILED(hr))
		{
			// hr may be DIERR_OTHERAPPHASPRIO or other errors.  This
			// may occur when the app is minimized or in the process of 
			// switching.
			PERFINFO_AUTO_STOP();
			return hr == DIERR_OTHERAPPHASPRIO ? S_OK : hr;
		}

		// Try polling the device again
		PERFINFO_AUTO_START("Poll",1);
			hr = IDirectInputDevice8_Poll(JoystickDev); 
		PERFINFO_AUTO_STOP();

		if (FAILED(hr))
		{
			// If Poll failed again, then disconnect the Joystick.
			PERFINFO_AUTO_STOP();
			return hr;
		}
	}

	// Get the input's device state
	PERFINFO_AUTO_START("GetDeviceState",1);
		hr = IDirectInputDevice8_GetDeviceState(JoystickDev, sizeof(DIJOYSTATE2), &js);
	PERFINFO_AUTO_STOP();

	if(FAILED(hr))
	{
		PERFINFO_AUTO_STOP();
		return S_OK; // The device should have been acquired during the Poll()
	}

	pJoystick->bActive = false;

	// First set of axes
	sX = (SHORT)js.lX;
	sY = -(SHORT)js.lY;
	handleDeadZone(&sX, &sY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
	pJoystick->fAxis[kInputJoystickPhysical_X - kInputJoystickPhysical_X] = sX / 32767.0f;
	pJoystick->fAxis[kInputJoystickPhysical_Y - kInputJoystickPhysical_X] = sY / 32767.0f;
	pJoystick->bActive = pJoystick->bActive || sX != 0 || sY != 0;

	// Second set of axes
	sX = (SHORT)js.lRx;
	sY = -(SHORT)js.lRy;
	handleDeadZone(&sX, &sY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
	pJoystick->fAxis[kInputJoystickPhysical_Rx - kInputJoystickPhysical_X] = sX / 32767.0f;
	pJoystick->fAxis[kInputJoystickPhysical_Ry - kInputJoystickPhysical_X] = sY / 32767.0f;
	pJoystick->bActive = pJoystick->bActive || sX != 0 || sY != 0;

	// Third set of axes
	sX = (SHORT)js.lZ;
	sY = -(SHORT)js.lRz;
	handleDeadZone(&sX, &sY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
	pJoystick->fAxis[kInputJoystickPhysical_Z - kInputJoystickPhysical_X] = sX / 32767.0f;
	pJoystick->fAxis[kInputJoystickPhysical_Rz - kInputJoystickPhysical_X] = sY / 32767.0f;
	pJoystick->bActive = pJoystick->bActive || sX != 0 || sY != 0;

	// Sliders
	for (i = kInputJoystickPhysical_Slider0; i <= kInputJoystickPhysical_SliderLast; i++)
	{
		pJoystick->fAxis[i - kInputJoystickPhysical_X] = js.rglSlider[i - kInputJoystickPhysical_Slider0] / 32767.0f;
		pJoystick->bActive = pJoystick->bActive || js.rglSlider[i - kInputJoystickPhysical_Slider0] != 0;
	}

	// POV buttons
	// 0 = up, 9000 = right, 18000 = down, 27000 = left
	pJoystick->sPOV = 0;
	for (i = 0; i + kInputJoystickPhysical_PovUp0 <= kInputJoystickPhysical_PovUpLast; i++)
	{
		if (((int)js.rgdwPOV[i] >= 0 && (int)js.rgdwPOV[i] <= 4500) || ((int)js.rgdwPOV[i] >= 31500))
			pJoystick->sPOV |= 1 << (i + 4 * DXDIR_UP);
		if ((int)js.rgdwPOV[i] >= 4500 && (int)js.rgdwPOV[i] <= 13500)
			pJoystick->sPOV |= 1 << (i + 4 * DXDIR_RIGHT);
		if ((int)js.rgdwPOV[i] >= 13500 && (int)js.rgdwPOV[i] <= 22500)
			pJoystick->sPOV |= 1 << (i + 4 * DXDIR_DOWN);
		if ((int)js.rgdwPOV[i] >= 22500)
			pJoystick->sPOV |= 1 << (i + 4 * DXDIR_LEFT);
	}
	pJoystick->sPressedPOV = ( pJoystick->sLastPOV ^ pJoystick->sPOV ) & pJoystick->sPOV;
	pJoystick->sLastPOV    = pJoystick->sPOV;
	pJoystick->bActive = pJoystick->bActive || pJoystick->sPOV != 0;

	// Buttons
	pJoystick->uButtons = 0;
	for (i = 0; i + kInputJoystickPhysical_Button0 <= kInputJoystickPhysical_ButtonLast; i++)
		pJoystick->uButtons |= (js.rgbButtons[i] & 0x80) ? 1 << i : 0;
	pJoystick->uPressedButtons	= ( pJoystick->uLastButtons ^ pJoystick->uButtons ) & pJoystick->uButtons;
	pJoystick->uLastButtons		= pJoystick->uButtons;
	pJoystick->bActive = pJoystick->bActive || pJoystick->uButtons != 0;

	PERFINFO_AUTO_STOP();

	return S_OK;
}

static void JoystickSetInputs(void)
{
	int i, j, k;
	U32 uCapturedButtons = 0;
	U32 uCapturedPOV = 0;
	U32 uCapturedAnalog = 0;
	U32 uButtons = 0;
	U32 uPressedButtons = 0;
	U32 uLastButtons = 0;
	U32 uPOV = 0;
	U32 uPressedPOV = 0;
	U32 uLastPOV = 0;
	F32 fAxis[8] = {0};
	U32 uButtonTimes[25] = {0};
	U32 uPOVTimes[16] = {0};
	DWORD curTime = inpGetTime();

	PERFINFO_AUTO_START_FUNC();

	memset(s_LogicalState, 0, sizeof(s_LogicalState));

	// Fill in the logical state
	for (i = kInputJoystickLogical_None + 1; i < kInputJoystickLogical_MAX && s_bProcessingEnabled; i++)
	{
		for (j = 0; j < s_iMappingProfiles && !s_LogicalState[i].pSource; j++)
		{
			if (s_aMapping[j].eLogicalOutput == i && s_aMapping[j].pJoystick)
			{
				if (inpJoystickIsInputAnalog(s_aMapping[j].ePhysicalInput))
				{
					F32 fValue = 0;
					if (IsAnyJoystick(s_aMapping[j].pJoystick))
					{
						for (k = 0; k < eaSize(&gInput->eaJoysticks) && fValue == 0; k++)
							fValue = gInput->eaJoysticks[k]->fAxis[s_aMapping[j].ePhysicalInput - kInputJoystickPhysical_X];
					}
					else
					{
						fValue = s_aMapping[j].pJoystick->fAxis[s_aMapping[j].ePhysicalInput - kInputJoystickPhysical_X];
					}
					if (fValue != 0)
					{
						s_LogicalState[i].fAnalogValue = fValue;
						s_LogicalState[i].bDigitalValue = ABS(fValue) > 0.5f ? 1 : 0;
						s_LogicalState[i].bAnalog = true;
						s_LogicalState[i].pSource = s_aMapping[j].pJoystick;
						uCapturedAnalog |= 1 << (s_aMapping[j].ePhysicalInput - kInputJoystickPhysical_X);
					}
				}
				else if (kInputJoystickPhysical_PovUp0 <= s_aMapping[j].ePhysicalInput && s_aMapping[j].ePhysicalInput <= kInputJoystickPhysical_PovRightLast)
				{
					U32 uBit = 1 << (s_aMapping[j].ePhysicalInput - kInputJoystickPhysical_PovUp0);
					SHORT sState = 0;
					if (IsAnyJoystick(s_aMapping[j].pJoystick))
					{
						for (k = 0; k < eaSize(&gInput->eaJoysticks) && (sState & uBit) == 0; k++)
							sState = gInput->eaJoysticks[k]->sPOV;
					}
					else
					{
						sState = s_aMapping[j].pJoystick->sPOV;
					}
					if (sState & uBit)
					{
						s_LogicalState[i].fAnalogValue = sState & uBit ? 1 : 0;
						s_LogicalState[i].bDigitalValue = sState & uBit ? 1 : 0;
						s_LogicalState[i].bAnalog = false;
						s_LogicalState[i].pSource = s_aMapping[j].pJoystick;
						uCapturedPOV |= uBit;
					}
				}
				else if (kInputJoystickPhysical_Button0 <= s_aMapping[j].ePhysicalInput && s_aMapping[j].ePhysicalInput <= kInputJoystickPhysical_ButtonLast)
				{
					U32 uBit = 1 << (s_aMapping[j].ePhysicalInput - kInputJoystickPhysical_Button0);
					U32 uState = 0;
					if (IsAnyJoystick(s_aMapping[j].pJoystick))
					{
						for (k = 0; k < eaSize(&gInput->eaJoysticks) && (uState & uBit) == 0; k++)
							uState = gInput->eaJoysticks[k]->uButtons;
					}
					else
					{
						uState = s_aMapping[j].pJoystick->uButtons;
					}
					if (uState & uBit)
					{
						s_LogicalState[i].fAnalogValue = uState & uBit ? 1 : 0;
						s_LogicalState[i].bDigitalValue = uState & uBit ? 1 : 0;
						s_LogicalState[i].bAnalog = false;
						s_LogicalState[i].pSource = s_aMapping[j].pJoystick;
						uCapturedButtons |= uBit;
					}
				}
			}
		}
	}

	// Fill in the GamePad state from the logical state
	gInput->JoystickGamePad.sThumbLX = (SHORT)(s_LogicalState[kInputJoystickLogical_MovementX].fAnalogValue * 32767.0f);
	gInput->JoystickGamePad.sThumbLY = (SHORT)(s_LogicalState[kInputJoystickLogical_MovementY].fAnalogValue * 32767.0f);
	gInput->JoystickGamePad.sThumbRX = (SHORT)(s_LogicalState[kInputJoystickLogical_CameraX].fAnalogValue * 32767.0f);
	gInput->JoystickGamePad.sThumbRY = (SHORT)(s_LogicalState[kInputJoystickLogical_CameraY].fAnalogValue * 32767.0f);
	gInput->JoystickGamePad.fThumbLX = s_LogicalState[kInputJoystickLogical_MovementX].fAnalogValue;
	gInput->JoystickGamePad.fThumbLY = s_LogicalState[kInputJoystickLogical_MovementY].fAnalogValue;
	gInput->JoystickGamePad.fThumbRX = s_LogicalState[kInputJoystickLogical_CameraX].fAnalogValue;
	gInput->JoystickGamePad.fThumbRY = s_LogicalState[kInputJoystickLogical_CameraY].fAnalogValue;

	gInput->JoystickGamePad.bLeftTrigger = (BYTE)(s_LogicalState[kInputJoystickLogical_LeftTrigger].fAnalogValue * 255.0f);
	gInput->JoystickGamePad.bRightTrigger = (BYTE)(s_LogicalState[kInputJoystickLogical_RightTrigger].fAnalogValue * 255.0f);
	gInput->JoystickGamePad.fLeftTrigger = s_LogicalState[kInputJoystickLogical_LeftTrigger].fAnalogValue;
	gInput->JoystickGamePad.fRightTrigger = s_LogicalState[kInputJoystickLogical_RightTrigger].fAnalogValue;

	gInput->JoystickGamePad.wButtons = 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_JoypadUp].bDigitalValue ? XINPUT_GAMEPAD_DPAD_UP : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_JoypadDown].bDigitalValue ? XINPUT_GAMEPAD_DPAD_DOWN : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_JoypadLeft].bDigitalValue ? XINPUT_GAMEPAD_DPAD_LEFT : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_JoypadRight].bDigitalValue ? XINPUT_GAMEPAD_DPAD_RIGHT : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_Start].bDigitalValue ? XINPUT_GAMEPAD_START : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_Select].bDigitalValue ? XINPUT_GAMEPAD_BACK : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_LStick].bDigitalValue ? XINPUT_GAMEPAD_LEFT_THUMB : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_RStick].bDigitalValue ? XINPUT_GAMEPAD_RIGHT_THUMB : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_LB].bDigitalValue ? XINPUT_GAMEPAD_LEFT_SHOULDER : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_RB].bDigitalValue ? XINPUT_GAMEPAD_RIGHT_SHOULDER : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_AB].bDigitalValue ? XINPUT_GAMEPAD_A : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_BB].bDigitalValue ? XINPUT_GAMEPAD_B : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_XB].bDigitalValue ? XINPUT_GAMEPAD_X : 0;
	gInput->JoystickGamePad.wButtons |= s_LogicalState[kInputJoystickLogical_YB].bDigitalValue ? XINPUT_GAMEPAD_Y : 0;

	gInput->JoystickGamePad.bLastLeftTrigger		= gInput->JoystickGamePad.bPressedLeftTrigger;
	gInput->JoystickGamePad.bPressedLeftTrigger		= s_LogicalState[kInputJoystickLogical_LeftTrigger].bDigitalValue;
	gInput->JoystickGamePad.bLastRightTrigger		= gInput->JoystickGamePad.bPressedRightTrigger;
	gInput->JoystickGamePad.bPressedRightTrigger	= s_LogicalState[kInputJoystickLogical_RightTrigger].bDigitalValue;
	gInput->JoystickGamePad.wPressedButtons			= ( gInput->JoystickGamePad.wLastButtons ^ gInput->JoystickGamePad.wButtons ) & gInput->JoystickGamePad.wButtons;
	gInput->JoystickGamePad.wLastButtons			= gInput->JoystickGamePad.wButtons;

#if 0
	if (gInput->JoystickEnabled)
	{
		// Send inputs for un-captured buttons/POV/AXIS
		for (i = 0; i < eaSize(&gInput->eaJoysticks); i++)
		{
			for (j = 0; j < 25; j++)
				if ((gInput->eaJoysticks[i]->uButtons ^ uButtons) & (1 << j))
					uButtonTimes[j] = gInput->eaJoysticks[i]->buttonTimeStamps[j];
			for (j = 0; j < 16; j++)
				if ((gInput->eaJoysticks[i]->sPOV ^ uPOV) & (1 << j))
					uPOVTimes[j] = gInput->eaJoysticks[i]->povTimeStamps[j];
			uLastButtons |= gInput->eaJoysticks[i]->uLastButtons & ~uButtons;
			uPressedButtons |= gInput->eaJoysticks[i]->uPressedButtons & ~uButtons;
			uButtons |= gInput->eaJoysticks[i]->uButtons & ~uButtons;
			uLastPOV |= gInput->eaJoysticks[i]->sLastPOV & ~uPOV;
			uPressedPOV |= gInput->eaJoysticks[i]->sPressedPOV & ~uPOV;
			uPOV |= gInput->eaJoysticks[i]->sPOV & ~uPOV;
			for (j = 0; j < 8; j++)
				if (!fAxis[j])
					fAxis[j] = gInput->eaJoysticks[i]->fAxis[j];
		}

		uButtons &= ~uCapturedButtons;
		uLastButtons &= ~uCapturedButtons;
		uPOV &= ~uCapturedPOV;
		uLastPOV &= ~uCapturedPOV;

		// Set the un-captured states (note: if the GamePad is active, then it'll overwrite values set here)
		for (i = 0; i < 25 && uButtons; i++)
		{
			if (uButtons & 1)
			{
				if (!(uLastButtons & 1) && (curTime - uButtonTimes[i]) < GetDoubleClickTime())
				{
					// Double tap
					inpUpdateKeyClearIfActive(dxJoystickToInpKeyMapping[i][1], curTime);
					inpUpdateKey(dxJoystickToInpKeyMapping[i][2], 1, curTime);
				}
				else
				{
					// First tap
					inpUpdateKey(dxJoystickToInpKeyMapping[i][1], 1, curTime);
					if (uPressedButtons & 1)
						inpKeyAddBuf(KIT_EditKey, dxJoystickToInpKeyMapping[i][1], 0, 0, 0);
				}
				uButtonTimes[i] = curTime;
			}
			else if (~uCapturedButtons & 1)
			{
				inpUpdateKeyClearIfActive(dxJoystickToInpKeyMapping[i][1], curTime);
				inpUpdateKeyClearIfActive(dxJoystickToInpKeyMapping[i][2], curTime);
			}

			uButtons >>= 1;
			uPressedButtons >>= 1;
			uLastButtons >>= 1;
			uCapturedButtons >>= 1;
		}
		for (i = 0; i < 16 && uPOV; i++)
		{
			if (uPOV & 1)
			{
				// Add double-tap for POV?
				inpUpdateKey(dxPovToInpKeyMapping[i][0], 1, curTime);
				if (uPressedButtons & 1)
					inpKeyAddBuf(KIT_EditKey, dxPovToInpKeyMapping[i][0], 0, 0, 0);
				uPOVTimes[i] = curTime;
			}
			else if (~uCapturedPOV & 1)
			{
				inpUpdateKeyClearIfActive(dxPovToInpKeyMapping[i][0], curTime);
			}

			uPOV >>= 1;
			uPressedPOV >>= 1;
			uLastPOV >>= 1;
			uCapturedPOV >>= 1;
		}

		// TODO(jm): add button repeating

		// Update times
		for (i = 0; i < eaSize(&gInput->eaJoysticks); i++)
		{
			memcpy(gInput->eaJoysticks[i]->buttonTimeStamps, uButtonTimes, sizeof(U32) * 25);
			memcpy(gInput->eaJoysticks[i]->povTimeStamps, uPOVTimes, sizeof(U32) * 16);
		}
	}
#endif

	PERFINFO_AUTO_STOP();
}

void inpJoystickState()
{
	HRESULT hr;
	int i, j;
	bool bWasConnected;
	bool bUpdatedLists = false;
	const U32 uNow = inpGetTime();
	static JoystickEnumContext context;

	PERFINFO_AUTO_START_FUNC();

	if (!gInput || !gInput->DirectInput8)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	bWasConnected = gInput->JoystickGamePad.bConnected;

	// Check to see if the Joysticks are not enabled
	if (!gInput->JoystickEnabled)
	{
		// Cleanup Joystick
		while (eaSize(&gInput->eaJoystickDevs))
		{
			LPDIRECTINPUTDEVICE8 JoystickDev = eaPop(&gInput->eaJoystickDevs);
			InputJoystick *pJoystick = eaPop(&gInput->eaJoysticks);
			JoystickDisconnect(JoystickDev);
			free(pJoystick);
			bUpdatedLists = true;
		}

		// Update the mapping cache
		if (bUpdatedLists)
			JoystickUpdateCache();

		ZeroStruct(&gInput->JoystickGamePad);
		gInput->JoystickGamePad.bConnected = false;
		gInput->JoystickGamePad.bRemoved = bWasConnected && !gInput->JoystickGamePad.bConnected;
		s_bForceEnumDevices = true;
		s_bHardwareChanged = true;
		PERFINFO_AUTO_STOP();
		return;
	}

	if (s_bForceEnumDevices)
	{
		HANDLE ahLockedDevices[10] = {0};
		char *apchDeviceList[10] = {0};
		S32 iNumLockedDevices = 0;
		S32 iNumDeviceList = 0;

		// Don't reenumerate the devices again
		s_bForceEnumDevices = false;

		PERFINFO_AUTO_START("Find Joysticks",1);

		// Manually scan the devices if there was a hardware change to get VID's & PID's
		if (s_bHardwareChanged)
		{
			s_bHardwareChanged = false;
			if (!JoystickEnumDevices(INT_MAX))
			{
				// If no devices were enumerated here, then why would IDirectInput8_EnumDevices do anything below
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return;
			}
		}

		// Lock the naughty devices
		for (i = 0; i < eaSize(&s_eaHIDDevices) && iNumLockedDevices < ARRAY_SIZE(ahLockedDevices); i++)
		{
			HANDLE hLock = JoystickLockDevice(s_eaHIDDevices[i]);
			if (hLock)
				ahLockedDevices[iNumLockedDevices++] = hLock;
			// This local copy of the device list is a total hack to make it so that
			// the list of HID devices appears in a minidump since I believe that a
			// minidump more or less only contains the stack information.
			if (iNumDeviceList < ARRAY_SIZE(apchDeviceList))
				strdup_alloca(apchDeviceList[iNumDeviceList], s_eaHIDDevices[i]);
			// Keep count of how many devices there are
			iNumDeviceList++;
		}

		// Enumerate the devices
		PERFINFO_AUTO_START("EnumDevices",1);
			__try
			{
				hr = IDirectInput8_EnumDevices(gInput->DirectInput8, DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, &context, DIEDFL_ATTACHEDONLY);
			}
			__except(s_cbCrashHandler && s_cbCrashHandler() ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
			{
				// The crash handler wants us to try continuing, this probably will leave
				// the IDirectInput8 object in an inconsistent/invalid state. So it might
				// not crash in EnumDevices again.
				s_iJoystickWasToldToTryAgain++;
			}
		PERFINFO_AUTO_STOP();

		// Unlock any devices locked above
		while (iNumLockedDevices > 0)
		{
			JoystickUnlockDevice(ahLockedDevices[--iNumLockedDevices]);
		}

		// Remove old devices
		for (i = eaSize(&gInput->eaJoystickDevs) - 1; i >= 0; i--)
		{
			if ((j = eaFind(&context.JoystickDevs, gInput->eaJoystickDevs[i])) < 0)
			{
				JoystickDisconnect(eaRemove(&gInput->eaJoystickDevs, i));
				free(eaRemove(&gInput->eaJoysticks, i));
				bUpdatedLists = true;
			}
			else
			{
				eaRemove(&context.JoystickDevs, j);
				eaRemove(&context.Joysticks, j);
			}
		}

		// Configure new devices
		PERFINFO_AUTO_START("Configure Joysticks",1);
		for (i = eaSize(&context.JoystickDevs) - 1; i >= 0; i--)
		{
			context.JoystickDev = eaRemove(&context.JoystickDevs, i);
			context.Joystick = eaRemove(&context.Joysticks, i);

			// Set the data format to "simple joystick" - a predefined data format 
			//
			// A data format specifies which controls on a device we are interested in,
			// and how they should be reported. This tells DInput that we will be
			// passing a DIJOYSTATE2 structure to IDirectInputDevice::GetDeviceState().
			if (FAILED(hr = IDirectInputDevice8_SetDataFormat(context.JoystickDev, &c_dfDIJoystick2)))
			{
				JoystickDisconnect(context.JoystickDev);
				free(context.Joystick);
				continue;
			}

			// Set the cooperative level to let DInput know how this device should
			// interact with the system and with other DInput applications.
			if (FAILED(hr = IDirectInputDevice8_SetCooperativeLevel(context.JoystickDev, gInput->hwnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND)))
			{
				JoystickDisconnect(context.JoystickDev);
				free(context.Joystick);
				continue;
			}

			// Enumerate the joystick objects. The callback function enabled user
			// interface elements for objects that are found, and sets the min/max
			// values property for discovered axes.
			if (FAILED(hr = IDirectInputDevice8_EnumObjects(context.JoystickDev, EnumObjectsCallback, &context, DIDFT_ALL)))
			{
				JoystickDisconnect(context.JoystickDev);
				free(context.Joystick);
				continue;
			}

			eaPush(&gInput->eaJoystickDevs, context.JoystickDev);
			eaPush(&gInput->eaJoysticks, context.Joystick);
			bUpdatedLists = true;
		}
		PERFINFO_AUTO_STOP();

		context.JoystickDev = NULL;
		context.Joystick = NULL;

		// Track insertion and removals
		gInput->JoystickGamePad.bConnected = (eaSize(&gInput->eaJoystickDevs) > 0);
		gInput->JoystickGamePad.bRemoved   = ( bWasConnected && !gInput->JoystickGamePad.bConnected);
		gInput->JoystickGamePad.bInserted  = (!bWasConnected &&  gInput->JoystickGamePad.bConnected);

		// Don't update rest of the state if not connected
		if (!gInput->JoystickGamePad.bConnected)
		{
			ZeroStruct(&gInput->JoystickGamePad);
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return;
		}

		if (gInput->JoystickGamePad.bInserted)
		{
			ZeroStruct(&gInput->JoystickGamePad.caps);
			gInput->JoystickGamePad.caps.Type = XINPUT_DEVTYPE_GAMEPAD;
			gInput->JoystickGamePad.caps.SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
		}

		PERFINFO_AUTO_STOP();
	}

	// Track insertion and removals
	gInput->JoystickGamePad.bConnected = (eaSize(&gInput->eaJoystickDevs) > 0);
	gInput->JoystickGamePad.bRemoved   = ( bWasConnected && !gInput->JoystickGamePad.bConnected);
	gInput->JoystickGamePad.bInserted  = (!bWasConnected &&  gInput->JoystickGamePad.bConnected);

	if (gInput->JoystickGamePad.bConnected)
	{
		for (i = eaSize(&gInput->eaJoystickDevs) - 1; i >= 0; i--)
		{
			if (FAILED(inpJoystickUpdate(gInput->eaJoystickDevs[i], gInput->eaJoysticks[i])))
			{
				// If it failed, to update, then remove the Joystick
				LPDIRECTINPUTDEVICE8 JoystickDev = eaRemove(&gInput->eaJoystickDevs, i);
				InputJoystick *pJoystick = eaRemove(&gInput->eaJoysticks, i);

				JoystickDisconnect(JoystickDev);
				free(pJoystick);
				bUpdatedLists = true;
			}
		}

		// Update the mapping cache
		if (bUpdatedLists)
			JoystickUpdateCache();
	}

	// Always update the inputs
	JoystickSetInputs();

	PERFINFO_AUTO_STOP();
}

void joystickSetEnabled(bool bEnabled)
{
	gInput->JoystickEnabled = bEnabled ? 1 : 0;
}

bool joystickGetEnabled(void)
{
	return gInput->JoystickEnabled != 0;
}

void joystickSetProcessingEnabled(bool bEnabled)
{
	s_bProcessingEnabled = bEnabled;
}

bool joystickGetProcessingEnabled(void)
{
	return s_bProcessingEnabled;
}

bool joystickLogicalState(InputJoystickLogical eLogicalInput)
{
	return eLogicalInput > kInputJoystickLogical_None && eLogicalInput < kInputJoystickLogical_MAX ? s_LogicalState[eLogicalInput].bDigitalValue : false;
}

float joystickLogicalValue(InputJoystickLogical eLogicalInput)
{
	return eLogicalInput > kInputJoystickLogical_None && eLogicalInput < kInputJoystickLogical_MAX ? s_LogicalState[eLogicalInput].fAnalogValue : 0.0f;
}

bool joystickLogicalIsAnalog(InputJoystickLogical eLogicalInput)
{
	return eLogicalInput > kInputJoystickLogical_None && eLogicalInput < kInputJoystickLogical_MAX ? s_LogicalState[eLogicalInput].bAnalog : false;
}

#if 0
int joystickGetJoysticksEx(InputJoystick ***peaJoysticks, bool bExcludeActive)
{
	int i, count = 0;

	if (peaJoysticks)
		eaClearFast(peaJoysticks);

	for (i = 0; i < eaSize(&gInput->eaJoysticks); i++)
	{
		if (bExcludeActive && gInput->eaJoysticks[i]->bActive)
			continue;
		if (peaJoysticks)
			eaPush(peaJoysticks, gInput->eaJoysticks[i]);
		count++;
	}

	return count;
}
#endif

void joystickSetProfile(InputJoystickProfile *pProfile)
{
	int i;
	SAFE_FREE(s_aMapping);
	if (pProfile && eaSize(&pProfile->eaMapping))
	{
		s_iMappingProfiles = eaSize(&pProfile->eaMapping);
		s_aMapping = calloc(s_iMappingProfiles, sizeof(CachedJoystickMapping));
		for (i = 0; i < s_iMappingProfiles; i++)
		{
			s_aMapping[i].eLogicalOutput = pProfile->eaMapping[i]->eLogicalOutput;
			s_aMapping[i].ePhysicalInput = pProfile->eaMapping[i]->ePhysicalInput;
			s_aMapping[i].pchGuidDevice = pProfile->eaMapping[i]->pchGuidDevice;
		}
	}
	else
	{
		s_iMappingProfiles = 0;
	}
	JoystickUpdateCache();
}

const char *joystickGetName(const char *pchIdentifier)
{
	int i;

	if (!pchIdentifier || !*pchIdentifier)
		return "";

	for (i = 0; i < eaSize(&gInput->eaJoysticks); i++)
		if (!stricmp(gInput->eaJoysticks[i]->pchGuidDevice, pchIdentifier))
			return gInput->eaJoysticks[i]->pchName;

	// TODO: Translate Joystick.Disconnected?
	return "";
}

bool joystickGetActiveInput(const char **ppchIdentifier, InputJoystickPhysical *peInput)
{
	int i, j, uBits, uMask;
	InputJoystickPhysical eRef;
	float fBestAxis = 0;

	for (i = 0; i < eaSize(&gInput->eaJoysticks); i++)
	{
		InputJoystick *pJoystick = gInput->eaJoysticks[i];
		if (ppchIdentifier && *ppchIdentifier && **ppchIdentifier
			&& stricmp(*ppchIdentifier, pJoystick->pchGuidDevice) != 0)
			continue;

		if (pJoystick->bActive)
		{
			// Find active axis
			for (j = 0; j < ARRAY_SIZE(pJoystick->fAxis); j++)
			{
				if (pJoystick->fAxis[j] != 0 && ABS(pJoystick->fAxis[j]) > fBestAxis)
				{
					if (ppchIdentifier && !*ppchIdentifier)
						*ppchIdentifier = pJoystick->pchGuidDevice;
					if (peInput)
						*peInput = kInputJoystickPhysical_X + j;
					fBestAxis = ABS(pJoystick->fAxis[j]);
					continue;
				}
			}
			if (fBestAxis > 0)
				return true;

			// Find active button
			if (pJoystick->uButtons)
			{
				eRef = kInputJoystickPhysical_Button0;
				uBits = pJoystick->uButtons;
			}
			else if (pJoystick->sPOV)
			{
				eRef = kInputJoystickPhysical_PovUp0;
				uBits = pJoystick->sPOV;
			}

			// Find the active button
			if (uBits)
			{
				j = 0;
				uMask = 1;
				while (j < 16 && !(uBits & uMask))
				{
					uMask <<= 1;
					j++;
				}

				if (ppchIdentifier && !*ppchIdentifier)
					*ppchIdentifier = pJoystick->pchGuidDevice;
				if (peInput)
					*peInput = eRef + j;
				return true;
			}
		}
		else if (ppchIdentifier && *ppchIdentifier && **ppchIdentifier)
		{
			return false;
		}
	}

	return false;
}

void joystickSetCrashHandler(joystickCrashHandlerCB cbCrashHandler)
{
	s_cbCrashHandler = cbCrashHandler;
}

#include "AutoGen/inputJoystick_h_ast.c"
#include "AutoGen/inputJoystick_c_ast.c"
