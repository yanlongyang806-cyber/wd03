#include "timing.h"
#include "inputRaw.h"
#include "input.h"
#include "inputMouse.h"
#include "EArray.h"
#include "ThreadManager.h"
#include "memlog.h"
#include "utils.h"
#include "UTF8.h"

// to get GetCPUTicks64 - weird place for that function to be?
#include "TaskProfile.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:RawInputBackgroundThread", BUDGET_Editors););

#define RAWINPUT_MAX_DEBUG_TRACE_LEVEL 6

#define ENABLE_TRACE_INPUT 1

#if ENABLE_TRACE_INPUT
void rawInputTrace(const char * format, ...);
#else
#define rawInputTrace(fmt, ...)
#endif
#define rawInputTraceL3(fmt, ...) if (rawInputManager.traceLevel>=3) rawInputTrace(fmt, __VA_ARGS__)

#pragma pack(push, 8)
#define ALIGN64BIT __ALIGN(8)

//
// WARNING! This structure must match WinUser.h. However, 32-bit processes on 64-bit versions of Windows receive
// a Raw Input packet via GetRawInputBuffer that does not match the structure declaration in the header file.
// Instead, there are eight bytes of padding or alignment between the header and data fields. See the reservedPad1
// and reservedPad2 members. See the current (2013) MSDN document "GetRawInputBuffer function (Windows)" here: 
// http://msdn.microsoft.com/en-us/library/windows/desktop/ms645595(v=vs.85).aspx
// "To ensure GetRawInputBuffer behaves properly on WOW64, you must align the RAWINPUT structure by 8 bytes. The 
// following code shows how to align RAWINPUT for WOW64. Note the explicit field offsets.
//
//	[StructLayout(LayoutKind.Explicit)]
//	internal struct RAWINPUT
//	{
//		[FieldOffset(0)]
//		public RAWINPUTHEADER header;
//
//		[FieldOffset(16+8)]
//		public RAWMOUSE mouse;
//
//		[FieldOffset(16+8)]
//		public RAWKEYBOARD keyboard;
//
//		[FieldOffset(16+8)]
//		public RAWHID hid;
//	}"

/*
 * RAWINPUT data structure.
 */
typedef struct ALIGN64BIT tagRAWINPUT_WOW64 {
    RAWINPUTHEADER header;
	// See the above warning about use of these padding members.
	ULONG reservedPad1;
	ULONG reservedPad2;
    union {
        RAWMOUSE    mouse;
        RAWKEYBOARD keyboard;
        RAWHID      hid;
    } data;
} RAWINPUT_WOW64, *PRAWINPUT_WOW64, *LPRAWINPUT_WOW64;

#pragma pack(pop)

// From the current (2013) doc "GetRawInputBuffer function (Windows)": 
// http://msdn.microsoft.com/en-us/library/windows/desktop/ms645595(v=vs.85).aspx
// "Note  To get the correct size of the raw input buffer, do not use *pcbSize, use *pcbSize * 8 instead."
// See the use of the constant in .
static const int MSDN_DOC_RAWINPUT_BUFFER_SIZE_FACTOR = 8;



#define MAX_RAW_EVENTS_PERFRAME 256

typedef struct RawInputPacket
{
	int nInputEvents;
	U32 inpTimestamp;
	RAWINPUT *inputEventsOverflow;
	int nNumOverflowEvents, nMaxOverflowEvents;
	RAWINPUT inputEvents[ MAX_RAW_EVENTS_PERFRAME ];
} RawInputPacket;

#define RAW_INPUT_MAX_SIZE_BYTES 1024

typedef struct ALIGN64BIT RawInputPacketQueue
{
	U8 rawInputBuffer[RAW_INPUT_MAX_SIZE_BYTES];
	CRITICAL_SECTION rawInputQueueCS;
	bool bIsWOW64;
	RawInputPacket queues[2]; // background thread raw input processor builds in index 0, FG reads in index 1
#if ENABLE_RAWINPUT_OVERRUN_TEST
	bool mineField[ 1024 * 12 ];
#endif
} RawInputPacketQueue;

typedef struct RawInputProcessingState
{
	bool bMouseEventRecv;
	bool bMousePosRecv;
	bool bMouseDeltaRecv;
	bool bMouseWheelDeltaRecv;
	bool bMouseVirtualDesktop;
	bool bUpdatedMouseChord;
	int iMouseDeltaForThisFrame;
	int lLastX;
	int lLastY;
	UINT inpTimestamp;
} RawInputProcessingState;

typedef struct RawInputSystem
{
	volatile bool bRawInputStartupComplete;	// true => Input thread is ready to run.
	volatile bool bRawInputStartupSuccess;	// true => Raw Input initialization succeeded.
	volatile bool bCloseRawInput;
	volatile bool bMainThreadIgnoringInput;
	ManagedThread * rawInputThread;
	WNDCLASSEXW wcRawInput;
	HWND hwndRawInput;
	DWORD inputLocaleSourceThreadId;
	RawInputProcessingState processingState;
	RawInputPacketQueue rawInputQueue;
	U8 showInputWindow;
	U8 traceLevel;
	U8 bDebugDisableWMINPUT;
	MemLog memlog;
} RawInputSystem;

static RawInputSystem rawInputManager = { 0 };

AUTO_CMD_INT(rawInputManager.showInputWindow, showRawInputWindow) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(rawInputManager.traceLevel, enableRawInputTrace) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(rawInputManager.bDebugDisableWMINPUT, disableRawInputWMINPUT) ACMD_CMDLINEORPUBLIC ACMD_HIDE ACMD_ACCESSLEVEL(0);

#if ENABLE_TRACE_INPUT
void rawInputTrace(const char * format, ...)
{
	va_list va;
	if (rawInputManager.traceLevel)
	{
		va_start(va, format);
		memlog_vprintf(&rawInputManager.memlog, format, va);
		if (rawInputManager.traceLevel > 1)
			vprintf(format, va);
		va_end(va);
	}
}
#else
#define rawInputTrace(fmt, ...)
#endif

#if ENABLE_RAWINPUT_OVERRUN_TEST
static bool rawInputQueueMinefieldIsClear(const RawInputPacketQueue *rawInputQueue)
{
	int a_nMF;
	for (a_nMF = 0; a_nMF < 1024; ++a_nMF)
	{
		if (rawInputQueue->mineField[a_nMF] != 0)
			return false;
	}
	return true;
}

static void rawInputQueueMinefieldFill(RawInputPacketQueue *rawInputQueue)
{
	memset(rawInputQueue->mineField, 0, sizeof(rawInputQueue->mineField));
}
#endif

void rawInputQueueInit(RawInputPacketQueue *rawInputQueue)
{
	BOOL bIsWow64 = FALSE;
	IsWow64Process(GetCurrentProcess(), &bIsWow64);
	rawInputQueue->bIsWOW64 = bIsWow64 ? true : false;
	InitializeCriticalSection(&rawInputQueue->rawInputQueueCS);
#if ENABLE_RAWINPUT_OVERRUN_TEST
	rawInputQueueMinefieldFill(rawInputQueue);
#endif
}

void rawInputQueueCleanup(RawInputPacketQueue *rawInputQueue)
{
	int nQueue = 0;
	for (nQueue; nQueue < ARRAY_SIZE(rawInputQueue->queues); ++nQueue)
		SAFE_FREE(rawInputQueue->queues[nQueue].inputEventsOverflow);
	DeleteCriticalSection(&rawInputQueue->rawInputQueueCS);
}

void rawInputQueuePushEntries(RawInputPacketQueue *rawInputQueue, int nDestQueue, RawInputPacket *pSourceEvents)
{
	int nNumTotalEvents = 0;
	int nNumNewOverflowEvents = 0;
	RawInputPacket *pDestEvents = NULL;
	EnterCriticalSection(&rawInputQueue->rawInputQueueCS);

	assert(nDestQueue >= 0 && nDestQueue < 2);
	pDestEvents = &rawInputQueue->queues[nDestQueue];

	nNumTotalEvents = pDestEvents->nInputEvents + pSourceEvents->nInputEvents;
	
	// 0 ... 255 256 ... N
	// | normal | overflow ...
	
	// is there room for more "ordinary" events?
	if (pDestEvents->nInputEvents < MAX_RAW_EVENTS_PERFRAME)
	{
		int nNumNewOrdinaryEvents = pSourceEvents->nInputEvents;
		if (nNumTotalEvents > MAX_RAW_EVENTS_PERFRAME)
		{
			nNumNewOrdinaryEvents = MAX_RAW_EVENTS_PERFRAME - pDestEvents->nInputEvents;
			nNumNewOverflowEvents = nNumTotalEvents - MAX_RAW_EVENTS_PERFRAME;
		}
		// else nNumNewOverflowEvents = 0
		memcpy(&pDestEvents->inputEvents[pDestEvents->nInputEvents], 
			pSourceEvents->inputEvents, sizeof(RAWINPUT) * nNumNewOrdinaryEvents);
		pDestEvents->nInputEvents += nNumNewOrdinaryEvents;
	}
	else
		nNumNewOverflowEvents = pSourceEvents->nInputEvents;

	if (nNumNewOverflowEvents)
	{
		int nDestOverflowEvent = pDestEvents->nNumOverflowEvents;
		dynArrayAddStructs( pDestEvents->inputEventsOverflow, pDestEvents->nNumOverflowEvents, pDestEvents->nMaxOverflowEvents, nNumNewOverflowEvents);
		memcpy(&pDestEvents->inputEventsOverflow[nDestOverflowEvent], 
			pSourceEvents->inputEvents + pSourceEvents->nInputEvents - nNumNewOverflowEvents, sizeof(RAWINPUT) * nNumNewOverflowEvents);
	}

	pDestEvents->inpTimestamp = pSourceEvents->inpTimestamp;

#if ENABLE_RAWINPUT_OVERRUN_TEST
	assert(rawInputQueueMinefieldIsClear(rawInputQueue));
#endif

	pSourceEvents->nInputEvents = 0;
	pSourceEvents->nNumOverflowEvents = 0;

	LeaveCriticalSection(&rawInputQueue->rawInputQueueCS);
}

void rawInputQueueMoveEntries(RawInputPacketQueue *rawInputQueue, int nDestQueue, RawInputPacket *pSourceEvents)
{
	RawInputPacket *pDestEvents = NULL;
	EnterCriticalSection(&rawInputQueue->rawInputQueueCS);

	assert(nDestQueue >= 0 && nDestQueue < 2);
	pDestEvents = &rawInputQueue->queues[nDestQueue];

	pDestEvents->nInputEvents = pSourceEvents->nInputEvents;
	pDestEvents->nNumOverflowEvents = 0;
	if (pSourceEvents->nInputEvents)
		memcpy(&pDestEvents->inputEvents[0], 
			pSourceEvents->inputEvents, sizeof(RAWINPUT) * pSourceEvents->nInputEvents);
	if (pSourceEvents->nNumOverflowEvents)
	{
		dynArrayAddStructs(pDestEvents->inputEventsOverflow, pDestEvents->nNumOverflowEvents, pDestEvents->nMaxOverflowEvents, pSourceEvents->nNumOverflowEvents);
		memcpy(&pDestEvents->inputEventsOverflow[0], 
			pSourceEvents->inputEventsOverflow, sizeof(RAWINPUT) * pSourceEvents->nNumOverflowEvents);
	}

	pDestEvents->inpTimestamp = pSourceEvents->inpTimestamp;

#if ENABLE_RAWINPUT_OVERRUN_TEST
	assert(rawInputQueueMinefieldIsClear(rawInputQueue));
#endif

	pSourceEvents->nInputEvents = 0;
	pSourceEvents->nNumOverflowEvents = 0;

	LeaveCriticalSection(&rawInputQueue->rawInputQueueCS);
}

void rawInputQueuePushEntriesBG(RawInputPacketQueue *rawInputQueue, RawInputPacket *pSourceEvents)
{
	rawInputQueuePushEntries(rawInputQueue, 0, pSourceEvents);
}

void rawInputQueueReadFG(RawInputPacketQueue *rawInputQueue)
{
	rawInputQueueMoveEntries(rawInputQueue, 1, &rawInputQueue->queues[0]);
}

bool RawInputRegisterDevices(HWND hwndInputTarget)
{
	RAWINPUTDEVICE Rid[3];
	bool bRegisterSuccess = false;

	Rid[0].usUsagePage = RawInput_GenericDesktopPage; 
	Rid[0].usUsage = RIGDPU_Mouse; 
	// Warning: RIDEV_DEVNOTIFY causes startup failure on XP, since it's supported only on Vista and newer OSes.
	Rid[0].dwFlags = 0;//RIDEV_NOLEGACY;   // adds HID mouse and also ignores legacy mouse messages
	Rid[0].hwndTarget = hwndInputTarget;

	Rid[1].usUsagePage = RawInput_GenericDesktopPage; 
	Rid[1].usUsage = RIGDPU_Keyboard; 
	Rid[1].dwFlags = (input_state.bEnableRawInputManualCharTranslation ? RIDEV_NOLEGACY: 0);   // adds HID keyboard and also ignores legacy keyboard messages
	Rid[1].hwndTarget = hwndInputTarget;

	Rid[2].usUsagePage = RawInput_GenericDesktopPage; 
	Rid[2].usUsage = RIGDPU_Joystick; 
	Rid[2].dwFlags = 0;
	Rid[2].hwndTarget = hwndInputTarget;

	if (RegisterRawInputDevices(Rid, 2, sizeof(Rid[0])) == FALSE) {
		//registration failed. Call GetLastError for the cause of the error
		rawInputTrace("RawInput startup failed, GLE() = %d\n", GetLastError());
	}
	else
		bRegisterSuccess = true;

#if ENABLE_TRACE_INPUT
	if (rawInputManager.traceLevel > 1)
	{
		UINT numRawDevs = 0;
		PRAWINPUTDEVICELIST pRawDeviceList = NULL;
		if (GetRawInputDeviceList(NULL, &numRawDevs, sizeof(RAWINPUTDEVICELIST)))
			rawInputTrace("Raw devices query failed\n");
		else
		{
			rawInputTrace("%d Raw Devices\n", numRawDevs);
			pRawDeviceList = malloc(sizeof(RAWINPUTDEVICELIST) * numRawDevs);
			if (GetRawInputDeviceList(pRawDeviceList, &numRawDevs, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1)
				rawInputTrace("Raw devices query failed\n");
			else
			{
				UINT nDev = 0;
				for (nDev = 0; nDev < numRawDevs; ++nDev)
				{
					RAWINPUTDEVICELIST *pDev = &pRawDeviceList[nDev];
					if (pDev)
					{
						RID_DEVICE_INFO devInfo = { 0 };
						char strDevName[256];
						UINT nRetCode = 0;
						UINT nDevInfoSize = 0;

						strcpy(strDevName, "");
						rawInputTrace("%d %p\n", pDev->dwType, pDev->hDevice);
						nDevInfoSize = ARRAY_SIZE(strDevName);
						nRetCode = GetRawInputDeviceInfo(pDev->hDevice, RIDI_DEVICENAME, strDevName, &nDevInfoSize);
						if (nRetCode && nRetCode != (UINT)-1)
							rawInputTrace("%s\n", strDevName);
						nDevInfoSize = sizeof(devInfo);
						nRetCode = GetRawInputDeviceInfo(pDev->hDevice, RIDI_DEVICEINFO, &devInfo, &nDevInfoSize);
						if (nRetCode && nRetCode != (UINT)-1)
						{
							if (pDev->dwType == RIM_TYPEMOUSE)
								rawInputTrace("ID %u Btn %u DPI %u HrzWhl %u\n", devInfo.mouse.dwId, devInfo.mouse.dwNumberOfButtons, devInfo.mouse.dwSampleRate, devInfo.mouse.fHasHorizontalWheel);
							else
							if (pDev->dwType == RIM_TYPEKEYBOARD)
								rawInputTrace("Type %u Subtype %u KeybdMode %u NumFKey %u NumInd %u NumKeysTotal %u\n", 
									devInfo.keyboard.dwType, devInfo.keyboard.dwSubType, devInfo.keyboard.dwKeyboardMode, devInfo.keyboard.dwNumberOfFunctionKeys,
									devInfo.keyboard.dwNumberOfIndicators, devInfo.keyboard.dwNumberOfKeysTotal);
							else
							if (pDev->dwType == RIM_TYPEHID)
								rawInputTrace("VendorID %u ProdID %u Version %u UsagePage %u Usage %u\n", 
									devInfo.hid.dwVendorId, devInfo.hid.dwProductId, devInfo.hid.dwVersionNumber, devInfo.hid.usUsagePage, devInfo.hid.usUsage);
						}
					}
				}
			}
			free(pRawDeviceList);
		}
	}
#endif

	// initialize the current cursor pos
	if (gInput)
		inpMousePos(&gInput->mouseInpCur.x,&gInput->mouseInpCur.y);

	return bRegisterSuccess;
}

static void RawInputProcessInputBuffer(bool bInWMInputHandler)
{
    UINT cbSize = 0;
	DWORD gribResult = 0;
	PRAWINPUT pRawInput = NULL; 
	U32 currentTime = timerCpuMs();
	RawInputPacket sourceEvents = { 0 };
	PRAWINPUT paRawInput[MAX_RAW_EVENTS_PERFRAME] = { 0 };

    gribResult = GetRawInputBuffer(NULL, &cbSize, /*0,*/sizeof(RAWINPUTHEADER));
	if (gribResult != 0)
	{
		rawInputTrace("GetRawInputBuffer Error %d, GLE() = %d", gribResult, GetLastError());
		return;
	}

	// If no size returned, choose the largest input struct size, so we can attempt to provide a sufficient
	// buffer. This is a speculative attempt to prevent the second GetRawInputBuffer from failing with error 
	// 122, or ERROR_INSUFFICIENT_BUFFER.
	if (!cbSize)
		cbSize = rawInputManager.rawInputQueue.bIsWOW64 ? sizeof(RAWINPUT_WOW64) : sizeof(RAWINPUT);
	// This is based on the documentation, not the sample code, which uses a factor of 16 here, commented as "a wild guess."
	// See the comments with the declaration of MSDN_DOC_RAWINPUT_BUFFER_SIZE_FACTOR.
	cbSize *= MSDN_DOC_RAWINPUT_BUFFER_SIZE_FACTOR;            
	assert(cbSize <= RAW_INPUT_MAX_SIZE_BYTES);
    pRawInput = (PRAWINPUT)rawInputManager.rawInputQueue.rawInputBuffer;
	assert(IS_ALIGNED((uintptr_t)rawInputManager.rawInputQueue.rawInputBuffer, sizeof(U64)));

	for (;;) 
    {
		RawInputPacket *pPacket = NULL;
		UINT cbSizeT = cbSize;
        UINT nInput = GetRawInputBuffer(pRawInput, &cbSizeT, /*0, 
                      */sizeof(RAWINPUTHEADER));
		PRAWINPUT pri = NULL;
		UINT i = 0;

#if ENABLE_TRACE_INPUT >= RAWINPUT_MAX_DEBUG_TRACE_LEVEL
		if (cbSize || cbSizeT || nInput)
			rawInputTrace("RI %d %d %d\n", cbSize, cbSizeT, nInput);
#endif

		if (nInput == (UINT)-1) 
		{
			rawInputTrace("RI Error %d\n", GetLastError());
			break;
		}
#if ENABLE_TRACE_INPUT >= RAWINPUT_MAX_DEBUG_TRACE_LEVEL
		if (bInWMInputHandler)
			rawInputTrace("RI in WM_INPUT %d %d %d\n", cbSize, cbSizeT, nInput);
#endif
		//Log(_T("nInput = %d"), nInput);
        if (nInput == 0) 
        {
            break;
        }
        assert(nInput > 0 && nInput <= MAX_RAW_EVENTS_PERFRAME);

		pri = pRawInput;
        for (i = 0; i < nInput; ++i) 
        { 
			PRAWINPUT destEvt = sourceEvents.inputEvents + i;
            paRawInput[i] = pri;
			if (!rawInputManager.rawInputQueue.bIsWOW64)
				memcpy(sourceEvents.inputEvents + i, pri, sizeof(RAWINPUT));
			else
			{
				// decode the misaligned/misdeclared WOW64 version of the struct
				PRAWINPUT_WOW64 priW64 = (PRAWINPUT_WOW64)pri;
				memcpy(&destEvt->header, &priW64->header, sizeof(destEvt->header));
				if (priW64->header.dwType == RIM_TYPEHID)
					memcpy(&destEvt->data.hid, &priW64->data.hid, sizeof(destEvt->data.hid));
				else
				if (priW64->header.dwType == RIM_TYPEKEYBOARD)
					memcpy(&destEvt->data.keyboard, &priW64->data.keyboard, sizeof(destEvt->data.keyboard));
				else
				if (priW64->header.dwType == RIM_TYPEMOUSE)
					memcpy(&destEvt->data.mouse, &priW64->data.mouse, sizeof(destEvt->data.mouse));
			}

#if ENABLE_TRACE_INPUT
			if (pri->header.dwType == RIM_TYPEHID)
				rawInputTrace("HID input[%d] = @%p\n", i, pri);
			else
			if (pri->header.dwType == RIM_TYPEKEYBOARD)
			{
				rawInputTrace("Key input[%d] = @%p, %x %x %x %x %c %x %x\n", i, pri, destEvt->data.keyboard.MakeCode, 
					destEvt->data.keyboard.Flags, destEvt->data.keyboard.Reserved, 
					destEvt->data.keyboard.VKey, destEvt->data.keyboard.VKey, destEvt->data.keyboard.Message, 
					destEvt->data.keyboard.ExtraInformation);
			}
			else
			if (pri->header.dwType == RIM_TYPEMOUSE)
			{
				rawInputTraceL3("Mouse input[%d] = @%p %x %x %x %d %d %x\n", i, pri, 
					destEvt->data.mouse.usFlags, 
					destEvt->data.mouse.ulButtons, 
					destEvt->data.mouse.ulRawButtons,
					destEvt->data.mouse.lLastX, destEvt->data.mouse.lLastY,
					destEvt->data.mouse.ulExtraInformation);
			}
#endif

			pri = NEXTRAWINPUTBLOCK(pri);
        }
        // to clean the buffer
        DefRawInputProc(paRawInput, nInput, sizeof(RAWINPUTHEADER)); 

		if (!rawInputManager.bMainThreadIgnoringInput)
		{
			sourceEvents.nInputEvents = nInput;
			sourceEvents.inpTimestamp = currentTime;
			rawInputQueuePushEntriesBG(&rawInputManager.rawInputQueue, &sourceEvents);
		}
    }
}

static void ProcessWMINPUT(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	UINT cbSize = 0, cbActualGetSize = 0;
	PRAWINPUT pRawInput = NULL;
	GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &cbSize, sizeof(RAWINPUTHEADER));
	assert(cbSize <= RAW_INPUT_MAX_SIZE_BYTES);
	cbActualGetSize = cbSize;
	pRawInput = (PRAWINPUT)rawInputManager.rawInputQueue.rawInputBuffer; 

	if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, pRawInput, &cbActualGetSize, sizeof(RAWINPUTHEADER)) != cbSize)
	{
		rawInputTrace("GetRawInputData failed: GetLastError() = %d, sizes: %u, %u\n", GetLastError(), cbSize, cbActualGetSize);
	}
	else
	{
#if ENABLE_TRACE_INPUT
		if (pRawInput->header.dwType == RIM_TYPEKEYBOARD && (pRawInput->data.keyboard.Flags & RI_KEY_BREAK))
			rawInputTrace("RI Stuck key %x\n", pRawInput->data.keyboard.MakeCode);
#endif

		if (!rawInputManager.bDebugDisableWMINPUT)
		{
			if (!rawInputManager.bMainThreadIgnoringInput)
			{
				U32 currentTime = timerCpuMs();
				RawInputPacket sourceEvents = { 0 };
				RAWINPUT *destEvt = &sourceEvents.inputEvents[0];
				memcpy(destEvt, pRawInput, sizeof(RAWINPUT));

#if ENABLE_TRACE_INPUT
				if (destEvt->header.dwType == RIM_TYPEHID)
					rawInputTrace("HID input[%d] = @%p\n", 0, destEvt);
				else
				if (destEvt->header.dwType == RIM_TYPEKEYBOARD)
					rawInputTrace("Key input[%d] = @%p, %x %x %x %x %c %x %x\n", 0, destEvt, destEvt->data.keyboard.MakeCode, 
						destEvt->data.keyboard.Flags, destEvt->data.keyboard.Reserved, 
						destEvt->data.keyboard.VKey, destEvt->data.keyboard.VKey, destEvt->data.keyboard.Message, 
						destEvt->data.keyboard.ExtraInformation);
				else
				if (destEvt->header.dwType == RIM_TYPEMOUSE)
					rawInputTrace("Mouse input[%d] = @%p %x %x %x %d %d %x\n", 0, destEvt, 
						destEvt->data.mouse.usFlags, 
						destEvt->data.mouse.ulButtons, 
						destEvt->data.mouse.ulRawButtons,
						destEvt->data.mouse.lLastX, destEvt->data.mouse.lLastY,
						destEvt->data.mouse.ulExtraInformation);
#endif

				sourceEvents.nInputEvents = 1;
				sourceEvents.inpTimestamp = currentTime;
				rawInputQueuePushEntriesBG(&rawInputManager.rawInputQueue, &sourceEvents);
			}
		}
	}
}

static LONG WINAPI RawInputBackgroundThreadWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
#if ENABLE_TRACE_INPUT
	char *pName = NULL;

	if (msg < 0xc000 || msg > 0xffff || !GetClipboardFormatName_UTF8(msg, &pName))
		estrCopy2(&pName, "");
	rawInputTrace("RI Wnd %p %u '%s' %x %x\n", hwnd, msg, pName, wParam, lParam);
	estrDestroy(&pName);
#endif
	if (msg == WM_INPUT_DEVICE_CHANGE)
	{
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	else
	if (msg == WM_SETTINGCHANGE)
	{
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	else
	if (msg == WM_INPUT)
	{
		ProcessWMINPUT(hwnd, msg, wParam, lParam);

		return 0;
	}
	else
	if (msg == WM_DESTROY)
	{
		PostQuitMessage( 0 );
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

#if ENABLE_TRACE_INPUT
static U32 nIterationsBeforeLogQueueStatus = 1;
#endif

static void RawInputLogQueueStatusAndDevices()
{
	RAWINPUTDEVICE riDevs[2];
	UINT riNumDevs = 2;
	UINT riGRRIDResult = 0;
	DWORD dwQueueStatus = GetQueueStatus(QS_ALLINPUT);

	OutputDebugStringf("RI QS: New %u Current %u\n", LOWORD(dwQueueStatus), HIWORD(dwQueueStatus));

	riGRRIDResult = GetRegisteredRawInputDevices(riDevs, &riNumDevs, sizeof(RAWINPUTDEVICE));
	if (riGRRIDResult > 0)
	{
		UINT riDevIndex = 0;
		for (riDevIndex = 0; riDevIndex < riNumDevs; ++riDevIndex)
		{
			OutputDebugStringf("RI Dev %u: %u %u %x %p\n", riDevIndex, riDevs[riDevIndex].usUsagePage, riDevs[riDevIndex].usUsage,
				riDevs[riDevIndex].dwFlags, riDevs[riDevIndex].hwndTarget);
		}
	}
}

DWORD WINAPI RawInputBackgroundThread(LPVOID lpParam)
{
	EXCEPTION_HANDLER_BEGIN

	BOOL bAttachedToInputThread = FALSE;
	ATOM wndClassAtom = 0;
	MSG msg = { 0 };

	rawInputManager.wcRawInput.cbSize			= sizeof(rawInputManager.wcRawInput);
	rawInputManager.wcRawInput.style			= CS_OWNDC | CS_DBLCLKS;
	rawInputManager.wcRawInput.lpfnWndProc		= RawInputBackgroundThreadWndProc;
	rawInputManager.wcRawInput.cbClsExtra		= 0;
	rawInputManager.wcRawInput.cbWndExtra		= 0;
	rawInputManager.wcRawInput.hInstance		= GetModuleHandleW( NULL );
	rawInputManager.wcRawInput.hCursor			= 0;
	rawInputManager.wcRawInput.hbrBackground	= CreateSolidBrush(RGB(0,0,0));
	rawInputManager.wcRawInput.lpszMenuName		= NULL;
	rawInputManager.wcRawInput.lpszClassName	= L"CrypticRawInput";
	wndClassAtom = RegisterClassExW(&rawInputManager.wcRawInput);
	if (!wndClassAtom)
		return 1;

	rawInputManager.hwndRawInput = CreateWindowW(rawInputManager.wcRawInput.lpszClassName,
		L"CrypticRawInputBackgroundHandler",
		WS_OVERLAPPEDWINDOW | (rawInputManager.showInputWindow ? WS_VISIBLE : WS_DISABLED),
		0, 0,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		rawInputManager.wcRawInput.hInstance,
		NULL);
	assert(rawInputManager.hwndRawInput);

	if (!RawInputRegisterDevices(rawInputManager.hwndRawInput))
	{
		rawInputManager.bRawInputStartupSuccess = false;
		rawInputManager.bRawInputStartupComplete = true;
	}
	else
	{
		rawInputManager.bRawInputStartupSuccess = true;
		rawInputManager.bRawInputStartupComplete = true;
	
		// TODO DJR maybe collapse the raw input reading thread into the main thread?
		// attach input state of main thread to raw input reading thread, so we can access key state 
		// for raw key input translation to character/text input

		// This is disabled because I suspect complications with attaching the main thread's input state:
		// I've discovered the main thread creates a hidden window to receive the device state change
		// notifications from Windows, but without pumping the main thread message queue. This seems like
		// an error. Additionally, I had only attached the input state in the hopes of using the keyboard state,
		// or perhaps to get the WM_CHAR messages to come to the raw input window as well, to fix double input 
		// problems, and we're not currently using that strategy, so attaching is unnecessary.
		//bAttachedToInputThread = AttachThreadInput(GetCurrentThreadId(), tmGetThreadId(tmGetMainThread()), TRUE);
		//memlog_printf(NULL, "Raw Input %s to input thread", bAttachedToInputThread ? "attached" : "did not attach");

		while (!rawInputManager.bCloseRawInput)
		{
			// This message loop is a bit non-standard. This is due to requirements when reading raw input with 
			// GetRawInputBuffer, as diagnosed by Timothy Lottes of NVIDIA. The GetRawInputBuffer API requires the WM_INPUT 
			// message to not have been removed from the message queue. Therefore we call GetRawInputBuffer outside the
			// "normal" message loop, which we include so the message queue properly empties. We use PeekMessage to gate 
			// processing any other messages, because GetMessage sometimes blocks even when there is raw input in the queue. 
			// Experiments with logging GetQueueStatus while debugging [NNO-15256] show that the queue can contain raw 
			// input, yet PeekMessage filtered to WM_INPUT will fail. See the comments for revisions for additional info.

			const DWORD nPollingThrottleSleepDurationMS = 2;
			const BOOL bSleepAlterableAndWakeOnInput = TRUE;
			BOOL bPeekResult = FALSE;

			// First, if any raw input is pending, process that so we don't remove WM_INPUT
			RawInputProcessInputBuffer(false);
			// Process any messages
			bPeekResult = PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
			if (bPeekResult)
			{
				BOOL nGetMessageResult = GetMessageW(&msg, NULL, 0, 0);
				if (nGetMessageResult == (UINT)-1)
				{
					DWORD nGLE = GetLastError();
					rawInputTrace(NULL, "RI GetMessage failed, GLE() = %u", nGLE);
				}
				else
				if (nGetMessageResult)
				{
					TranslateMessage(&msg);
					DispatchMessageW(&msg);
				}
			}

			SleepEx(nPollingThrottleSleepDurationMS, bSleepAlterableAndWakeOnInput);

	#if ENABLE_TRACE_INPUT > 1
			{
				--nIterationsBeforeLogQueueStatus;
				if (!nIterationsBeforeLogQueueStatus)
				{
					RawInputLogQueueStatusAndDevices();

					nIterationsBeforeLogQueueStatus = 800;
				}
			}
	#endif
		}
	}

	if (!DestroyWindow(rawInputManager.hwndRawInput))
		rawInputTrace("DestroyWindow failed with GetLastError() = %d", GetLastError());

	if (!UnregisterClassW(rawInputManager.wcRawInput.lpszClassName, rawInputManager.wcRawInput.hInstance))
		rawInputTrace("UnregisterClass failed with GetLastError() = %d", GetLastError());

	return 0;

	EXCEPTION_HANDLER_END
}

void RawInputBeginIgnoringInput()
{
	memlog_printf(NULL, "Ignoring raw input begin.\n");
	rawInputTrace("Ignoring raw input begin.\n");
	rawInputManager.bMainThreadIgnoringInput = true;
}

void RawInputStopIgnoringInput()
{
	memlog_printf(NULL, "Ignoring raw input end.\n");
	rawInputTrace("Ignoring raw input end.\n");
	rawInputManager.bMainThreadIgnoringInput = false;
}

bool RawInputStartup(const ManagedThread *pInputLocaleSourceThread)
{
	rawInputQueueInit(&rawInputManager.rawInputQueue);
	rawInputManager.inputLocaleSourceThreadId = tmGetThreadId(pInputLocaleSourceThread);
	rawInputManager.rawInputThread = tmCreateThreadEx(RawInputBackgroundThread, NULL, 4 * 1024, 0);

	while (!rawInputManager.bRawInputStartupComplete)
	{
		rawInputTrace( "RI MT waiting for RI BT startup\n");
		Sleep(4);
	}
	rawInputTrace( "RI MT done waiting for RI BT startup; startup %s\n", rawInputManager.bRawInputStartupSuccess ? "succeeded" : "failed");

	if (!rawInputManager.bRawInputStartupSuccess)
		rawInputManager.rawInputThread = NULL;

	return rawInputManager.rawInputThread != NULL;
}

// PS/2 Scancodes (Versus, say AT or XT keyboard types)
#define SCAN_USB_ENTER			0x1C
#define SCAN_USB_CTRL			0x1D
#define SCAN_USB_DIVIDE			0x35	/* / on numeric keypad */
#define SCAN_USB_PRINTSCREEN	0x37
#define SCAN_USB_ALT			0x38
#define SCAN_USB_NUM_LOCK		0x45
#define SCAN_USB_SCROLL_LOCK	0x46
#define SCAN_USB_HOME			0x47
#define SCAN_USB_UP				0x48
#define SCAN_USB_PRIOR			0x49
#define SCAN_USB_SUBTRACT		0x4A    /* - on numeric keypad */
#define SCAN_USB_LEFT			0x4B
#define SCAN_USB_NUMPAD5		0x4C
#define SCAN_USB_RIGHT			0x4D
#define SCAN_USB_ADD			0x4E    /* + on numeric keypad */
#define SCAN_USB_END			0x4F
#define SCAN_USB_DOWN			0x50
#define SCAN_USB_NEXT			0x51
#define SCAN_USB_INSERT			0x52
#define SCAN_USB_DELETE			0x53    /* . on numeric keypad */

// Ignored system keys
#define SCAN_USB_LWINDOW		0x5B
#define SCAN_USB_RWINDOW		0x5C
#define SCAN_USB_APPLICATION	0x5D
#define SCAN_USB_ACPI_POWER		0x5E
#define SCAN_USB_ACPI_SLEEP		0x5F

static DWORD ConvertScanCodeToDIKCode(DWORD ScanCode)
{
#define SCAN_CONVERT_CASE_STATEMENT(Code) xcase SCAN_USB_##Code: ScanCode = INP_##Code
	switch (ScanCode)
	{
	xcase SCAN_USB_PRINTSCREEN: ScanCode = INP_SYSRQ;
	SCAN_CONVERT_CASE_STATEMENT(DIVIDE);
	SCAN_CONVERT_CASE_STATEMENT(HOME);
	SCAN_CONVERT_CASE_STATEMENT(UP);
	SCAN_CONVERT_CASE_STATEMENT(PRIOR);
	SCAN_CONVERT_CASE_STATEMENT(SUBTRACT);
	SCAN_CONVERT_CASE_STATEMENT(LEFT);
	SCAN_CONVERT_CASE_STATEMENT(NUMPAD5);
	SCAN_CONVERT_CASE_STATEMENT(RIGHT);
	SCAN_CONVERT_CASE_STATEMENT(ADD);
	SCAN_CONVERT_CASE_STATEMENT(END);
	SCAN_CONVERT_CASE_STATEMENT(DOWN);
	SCAN_CONVERT_CASE_STATEMENT(NEXT);
	SCAN_CONVERT_CASE_STATEMENT(INSERT);
	SCAN_CONVERT_CASE_STATEMENT(DELETE);
	}
#undef SCAN_CONVERT_CASE_STATEMENT
	return ScanCode;
}

bool IsNumPadKeyScanCode(DWORD scanCode)
{
	return scanCode >= SCAN_USB_HOME && scanCode <= SCAN_USB_DELETE;
}

bool IsShiftedNumPadEvent(DWORD scanCode, DWORD virtualKeyCode)
{
	return IsNumPadKeyScanCode(scanCode) && virtualKeyCode == VK_SHIFT;
}

bool ShouldIgnoreKey(DWORD scanCode)
{
	return scanCode >= SCAN_USB_LWINDOW && scanCode <= SCAN_USB_ACPI_SLEEP;
}

static void RawInputProcessEvents(RawInputProcessingState *pProcessingState, const RAWINPUT *pInputEvents, int nInputEvents);

// This value is a reserved virtual key code. See the comment before its use in RawInputUpdate.
static const DWORD VK_RESERVED_EXTENDED_SCAN = 0xff;

static void rawInputBeginProcessing(RawInputProcessingState *pProcessingState)
{
	pProcessingState->bMouseEventRecv = false;
	pProcessingState->bMouseEventRecv = false;
	pProcessingState->bMousePosRecv = false;
	pProcessingState->bMouseDeltaRecv = false;
	pProcessingState->bMouseWheelDeltaRecv = false;
	pProcessingState->bMouseVirtualDesktop = false;
	pProcessingState->bUpdatedMouseChord = false;
}

void RawInputUpdate()
{
	const RawInputPacket *pPacket = &rawInputManager.rawInputQueue.queues[1];
	const S32 curTime = inpGetTime();
	S64 curCpuTime = 0;

	rawInputQueueReadFG(&rawInputManager.rawInputQueue);
	rawInputBeginProcessing(&rawInputManager.processingState);

	// this must remain consistent with inpReadMouse line 1000
	curCpuTime = GetCPUTicks64();
	gInput->fMouseTimeDeltaMS = (curCpuTime-gInput->lastMouseTime)*GetCPUTicksMsScale();
	gInput->lastMouseTime = curCpuTime;
	gInput->dev.inputActive = pPacket->nInputEvents != 0;

	rawInputManager.processingState.inpTimestamp = pPacket->inpTimestamp;

	RawInputProcessEvents(&rawInputManager.processingState, pPacket->inputEvents, pPacket->nInputEvents);
	if (pPacket->nNumOverflowEvents)
		RawInputProcessEvents(&rawInputManager.processingState, pPacket->inputEventsOverflow, pPacket->nNumOverflowEvents);

	if (!rawInputManager.processingState.bUpdatedMouseChord)
		inpMouseChordsKeepAlive(rawInputManager.processingState.bUpdatedMouseChord, curTime);
	inpMouseConvertMouseEventsToLogicalDragInput(curTime);

	if (rawInputManager.processingState.bMouseEventRecv)
	{
		rawInputTraceL3("MT Mouse state = %d %d %d %d\n", gInput->mouseInpCur.x, gInput->mouseInpCur.y);
	}

	// poll for mouse coordinates, so we have a decent starting point next time
	inpMousePos(&gInput->mouseInpCur.x, &gInput->mouseInpCur.y);
}

static void RawInputTranslateKeystateToChars(RawInputSystem *pRIManager, DWORD virtualKeyCode, DWORD scanCode, int attrib)
{
	HKL layout = INVALID_HANDLE_VALUE;
	unsigned char State[256] = { 0 };
	unsigned short result[4] = { 0 };
	int nResults = 0;
	int nInputChar = 0;

	layout = GetKeyboardLayout(pRIManager->inputLocaleSourceThreadId);
	if (GetKeyboardState(State) == FALSE)
		return;
	nResults = ToUnicodeEx(virtualKeyCode, scanCode, State, result, 4, 0, layout);
	if (nResults < 0)
	{
		// Some MS devs (see M. Kaplan's blog) recommend flushing some undocumented kernel keyboard buffer state here, to
		// handle so-called dead keys, which, when typed before a letter key, add a diacritical mark on the character. See
		// the French keyboard layout circumflex (^) character to the right of the P key, for example.
		//
		// Flushing does not appear to work; it actually seems to break composition. It also does not appear necessary once 
		// we have disabled legacy Windows messages in the registered Raw Input keyboard device.
		//nResults = ToUnicodeEx((UINT)VK_SPACE, MapVirtualKeyEx((UINT)VK_SPACE, 0, layout), State, result, 4, 0, layout);

		rawInputTrace("MT char composition (dead keys) = %c %u %x\n", result[0], result[0], result[0]);
	}
	else
	if (nResults)
	{
		// Handle all input characters. This may include ligatures, or multi-char letters. I'm not sure how to type these.
		for (nInputChar = 0; nInputChar < nResults; ++nInputChar)
		{
			UINT charInput = result[nInputChar];
			// skip input converted to ASCII control characters, as this is control, not text input
			if (charInput >= ' ')
			{
				rawInputTrace("MT char input = %c %u %x\n", charInput, charInput, charInput);
				inpKeyAddBuf(KIT_Text, scanCode, charInput, attrib, virtualKeyCode);
			}
		}
	}
}

static void RawInputProcessEvents(RawInputProcessingState *pProcessingState, const RAWINPUT *pInputEvents, int nInputEvents)
{
	int nInput = 0;

	for (nInput = 0; nInput < nInputEvents; ++nInput)
	{
		const RAWINPUT *pri = pInputEvents + nInput;

		if (pri->header.dwType == RIM_TYPEHID)
		{
			rawInputTrace("MT HID input[%d] = @%p\n", nInput, pri);
		}
		else
		if (pri->header.dwType == RIM_TYPEKEYBOARD)
		{
			DWORD scanCode = pri->data.keyboard.MakeCode;
			DWORD virtualKeyCode = pri->data.keyboard.VKey;
			DWORD directInputKeyCode = scanCode;
			bool bPressed = pri->data.keyboard.Flags & RI_KEY_BREAK ? false : true;
			KeyInputAttrib attrib = 0;

			if (ShouldIgnoreKey(scanCode))
				continue;

			// If numlock is on, numpad keys that have editing effects should
			// not be added to the EditKey queue. They'll be added to text
			// queue when the WM_CHAR event shows up. If numlock is off, we
			// don't even get these keys - we just get normal Home/Left/etc.
			if ((virtualKeyCode >= VK_NUMPAD0 && virtualKeyCode <= VK_NUMPAD9) || virtualKeyCode == VK_DECIMAL)
				attrib |= KIA_NUMLOCK;
			else
			if (pri->data.keyboard.Flags & (RI_KEY_E0|RI_KEY_E1))
			{
				// Handle how extended keys share scan codes between the navigation cluster keys and 
				// numpad keys without numlock.
				// The Cryptic Engine needs navigation cluster keys mapped to DirectInput "scan" codes, not
				// USB scan codes. Raw Input denotes the extended keys on 101-key enhanced keyboards with
				// either flag RI_KEY_E0 or RI_KEY_E1.
				// Translate the scan code for the navigation clusters to a DirectInput scan code.
				if (scanCode == SCAN_USB_ALT)
					directInputKeyCode = INP_RMENU;
				else
					if (scanCode == SCAN_USB_CTRL)
					{
						if(pri->data.keyboard.Flags & RI_KEY_E0)
							directInputKeyCode = INP_RCONTROL;
						else
							directInputKeyCode = INP_PAUSE;
					}
				else
					if (scanCode == SCAN_USB_ENTER && pri->data.keyboard.Flags & RI_KEY_E0)
					{
						directInputKeyCode = INP_NUMPADENTER;
					}
				else
					directInputKeyCode = ConvertScanCodeToDIKCode(scanCode);
			}

			rawInputTrace("MT Key input[%d] = @%p, %d %x %x %x %x %x %x\n", nInput, pri, pProcessingState->inpTimestamp, directInputKeyCode, 
				pri->data.keyboard.Flags, pri->data.keyboard.Reserved, 
				pri->data.keyboard.VKey, pri->data.keyboard.Message, 
				pri->data.keyboard.ExtraInformation);

			// Reserved VK_ code indicates the scan code from the keyboard is part of an extended, multi-byte scan code.
			// This happens for extended keyboard arrow navigation keys, which may be (or always are) emulated with 
			// automatically-shifted numeric keypad keys. See the Microsoft white paper "Windows Platform Design Notes,"
			// available on MSDN web page, "Archive: Key Support, Keyboard Scan Codes, and Windows," 
			// http://msdn.microsoft.com/en-us/windows/hardware/gg463372.aspx#EPB, accessed Feb. 9, 2013.
			if (virtualKeyCode != VK_RESERVED_EXTENDED_SCAN && !IsShiftedNumPadEvent(scanCode, virtualKeyCode))
			{
				if (scanCode == INP_LCONTROL || scanCode == INP_RCONTROL)
					gInput->ctrlKeyState = bPressed;
				if (scanCode == INP_LMENU || scanCode == INP_RMENU)
					gInput->altKeyState = bPressed;
				if (scanCode == INP_LSHIFT || scanCode == INP_RSHIFT)
					gInput->shiftKeyState = bPressed;

				if (gInput->ctrlKeyState)
					attrib |= KIA_CONTROL;
				if (gInput->altKeyState)
					attrib |= KIA_ALT;
				if (gInput->shiftKeyState)
					attrib |= KIA_SHIFT;

				if (bPressed)
				{
					inpKeyAddBuf(KIT_EditKey, directInputKeyCode, 0, attrib, virtualKeyCode);
					if (input_state.bEnableRawInputManualCharTranslation)
						// Convert raw key input into case-aware, locale-translated character input
						RawInputTranslateKeystateToChars(&rawInputManager, virtualKeyCode, bPressed ? scanCode : (0x8000 | scanCode), attrib);
				}
				inpUpdateKey(directInputKeyCode, bPressed, pProcessingState->inpTimestamp);
			}
			else
			{
				if (input_state.bEnableRawInputManualCharTranslation)
					// Convert raw key input into case-aware, locale-translated character input
					RawInputTranslateKeystateToChars(&rawInputManager, virtualKeyCode, bPressed ? scanCode : (0x8000 | scanCode), attrib);
			}
		}
		else
		if (pri->header.dwType == RIM_TYPEMOUSE)
		{
			pProcessingState->bMouseEventRecv = true;

			rawInputTraceL3("MT Mouse input[%d] = @%p %u %x %x %x %d %d %x\n", nInput, pri, pProcessingState->inpTimestamp,
				pri->data.mouse.usFlags, 
				pri->data.mouse.ulButtons, 
				pri->data.mouse.ulRawButtons,
				pri->data.mouse.lLastX, pri->data.mouse.lLastY,
				pri->data.mouse.ulExtraInformation);

			if (pri->data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP)
				pProcessingState->bMouseVirtualDesktop = true;
			else
				pProcessingState->bMouseVirtualDesktop = false;

			if (!(pri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
			{
				pProcessingState->bMouseDeltaRecv = true;
				inpMousePosDelta(pri->data.mouse.lLastX, pri->data.mouse.lLastY);
			}
			else
			{
				// Translate absolute input (i.e. in Remote Desktop or VNC, we get absolute positions from Raw Input)
				// into deltas, with a sensitivity scale.
				static const int ABSOLUTE_COORDS_DELTA_SENSITIVITY_SCALE = 8;
				inpMousePosDelta(
					(pri->data.mouse.lLastX - pProcessingState->lLastX) / ABSOLUTE_COORDS_DELTA_SENSITIVITY_SCALE,
					(pri->data.mouse.lLastY - pProcessingState->lLastY) / ABSOLUTE_COORDS_DELTA_SENSITIVITY_SCALE);
				pProcessingState->lLastX = pri->data.mouse.lLastX;
				pProcessingState->lLastY = pri->data.mouse.lLastY;
			}
			if (pri->data.mouse.usButtonFlags == RI_MOUSE_WHEEL)
			{
				int keyIndex = INP_MOUSEWHEEL;
				pProcessingState->bMouseWheelDeltaRecv = true;

				pProcessingState->iMouseDeltaForThisFrame = inpMouseWheelDelta((S16)pri->data.mouse.usButtonData);

				inpMouseConvertMouseEventToLogicalKeyInput(keyIndex, false, pProcessingState->iMouseDeltaForThisFrame, pProcessingState->inpTimestamp);
				if (inpMouseProcessChordsAsKeyInput(keyIndex, pProcessingState->inpTimestamp))
					pProcessingState->bUpdatedMouseChord = true;
			}

			if (pri->data.mouse.usButtonFlags)
			{
				// convert to DInput format
				typedef struct RIMouseButtonDesc
				{
					USHORT pressFlag;
					USHORT releaseFlag;
					USHORT inpKeyIndex;
					USHORT inpMouseButton;
					USHORT inpMouseClick;
					USHORT inpMouseDblClick;
				} RIMouseButtonDesc;
				RIMouseButtonDesc riMouseButtonDescs[] =
				{
					{ RI_MOUSE_LEFT_BUTTON_DOWN,	RI_MOUSE_LEFT_BUTTON_UP,	INP_LBUTTON, MS_LEFT,	INP_LCLICK, INP_LDBLCLICK },
					{ RI_MOUSE_RIGHT_BUTTON_DOWN,	RI_MOUSE_RIGHT_BUTTON_UP,	INP_RBUTTON, MS_RIGHT,	INP_RCLICK, INP_RDBLCLICK },
					{ RI_MOUSE_MIDDLE_BUTTON_DOWN,	RI_MOUSE_MIDDLE_BUTTON_UP,	INP_MBUTTON, MS_MID,	INP_MCLICK, INP_MDBLCLICK },

					{ RI_MOUSE_BUTTON_4_DOWN,		RI_MOUSE_BUTTON_4_UP,		INP_BUTTON4, 0,			0,			0 },
					{ RI_MOUSE_BUTTON_5_DOWN,		RI_MOUSE_BUTTON_5_UP,		INP_BUTTON5, 0,			0,			0 },
				};
				DIDEVICEOBJECTDATA didod = { 0 };
				int buttonIndex = 0;
				didod.dwTimeStamp = pProcessingState->inpTimestamp;

				if (input_state.reverseMouseButtons)
				{
					// swap right & left
					riMouseButtonDescs[0].pressFlag = RI_MOUSE_RIGHT_BUTTON_DOWN;
					riMouseButtonDescs[0].releaseFlag = RI_MOUSE_RIGHT_BUTTON_UP;
					riMouseButtonDescs[1].pressFlag = RI_MOUSE_LEFT_BUTTON_DOWN;
					riMouseButtonDescs[1].releaseFlag = RI_MOUSE_LEFT_BUTTON_UP;
				}

				for (buttonIndex = 0; buttonIndex < ARRAY_SIZE(riMouseButtonDescs); ++buttonIndex)
				{
					// DJR test button reverse
					const RIMouseButtonDesc *pButtonDesc = &riMouseButtonDescs[buttonIndex];
					USHORT buttonInpFlags = pri->data.mouse.usButtonFlags;
					bool bKeyPressed = false;

					if (buttonInpFlags & (pButtonDesc->pressFlag | pButtonDesc->releaseFlag))
					{
						bKeyPressed = pri->data.mouse.usButtonFlags & pButtonDesc->pressFlag ? true : false;
						
						if (pButtonDesc->inpMouseClick)
						{
							didod.dwData = bKeyPressed ? 0x80 : 0x00;
							inpMouseClick(&didod, pButtonDesc->inpMouseButton, pButtonDesc->inpMouseClick, pButtonDesc->inpMouseDblClick);
						}

						if (pButtonDesc->inpKeyIndex)
						{
							int keyIndex = pButtonDesc->inpKeyIndex;
							inpMouseConvertMouseEventToLogicalKeyInput(keyIndex, bKeyPressed, pProcessingState->iMouseDeltaForThisFrame, pProcessingState->inpTimestamp);
							if (inpMouseProcessChordsAsKeyInput(keyIndex, pProcessingState->inpTimestamp))
								pProcessingState->bUpdatedMouseChord = true;
						}
					}
				}
			}
		}
	}
}

void RawInputShutdown()
{
	if (input_state.bEnableRawInputSupport)
	{
		rawInputManager.bCloseRawInput = true;
		CloseWindow(rawInputManager.hwndRawInput);
		tmDestroyThread(rawInputManager.rawInputThread, true);
		rawInputQueueCleanup(&rawInputManager.rawInputQueue);
		rawInputManager.rawInputThread = NULL;
		rawInputManager.bCloseRawInput = false;
	}
}
