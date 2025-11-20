/***************************************************************************



***************************************************************************/

/* This is where all of the functions that define the functions needed
   for this to act like a library should go */

#include "inputLib.h"
#include "input.h"
#include "RdrDevice.h"
#include "timing.h"
#include "inputKeyBind.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "strings_opt.h"
#include "EArray.h"
#include "Prefs.h"
#include "utils.h"
#include "inputJoystickInternal.h"
#include "inputRaw.h"

//#define INPUTLIB_DEBUG

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

InputState input_state;

extern int GetScancodeFromVirtualKey(WPARAM wParam, LPARAM lParam);

void inpPrefsChanged(void)
{
	GamePrefStoreInt("InvertX", input_state.invertX);
	GamePrefStoreInt("InvertY", input_state.invertY);
	GamePrefStoreInt("invertUpDown", input_state.invertUpDown);
}

// Reverse the left and right mouse buttons
AUTO_CMD_INT(input_state.reverseMouseButtons, reverseMouseButtons) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(inpPrefsChanged);

// Invert the horizontal axis for movement controls
AUTO_CMD_INT(input_state.invertX, invertX) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(inpPrefsChanged);

// Invert the vertical axis for movement controls
AUTO_CMD_INT(input_state.invertY, invertY) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(inpPrefsChanged);

// Inverts the InvertibleUp and InvertibleDown commands
AUTO_CMD_INT(input_state.invertUpDown, invertUpDown) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CALLBACK(inpPrefsChanged);

// Disable mouse input polling
AUTO_CMD_INT(input_state.skipMousePolling, skipMousePolling) ACMD_CATEGORY(Debug);

#if !_PS3
// This should probably move somewhere else, but I dont know where
static int s_CodePageCurLang()
{
	int res = CP_ACP; // default to ASCII	
	HKL hkl = GetKeyboardLayout(0); //ab: is this a leak? I suppose it'll never be released anyway...

	// see http://msdn.microsoft.com/library/default.asp?url=/library/en-us/intl/nls_238z.asp	
	switch (LOWORD(hkl))
	{
		// Traditional Chinese 
	case MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_TRADITIONAL):
		res = 950; // TRADITIONAL CHINESE
		break;

		// Japanese
	case MAKELANGID(LANG_JAPANESE,SUBLANG_DEFAULT):
		res = 932; // JAPANESE
		break;

		// Korean
	case MAKELANGID(LANG_KOREAN,SUBLANG_DEFAULT):
		res = 949; // KOREAN
		break;

		// Simplified Chinese
	case MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED):
		res = 936;
		break;

	default:
		res = CP_ACP;
		break;
	};

	// --------------------
	// finally

	return res;
}
#endif

// Close the window.
AUTO_COMMAND ACMD_NAME(quit) ACMD_CATEGORY(Standard) ACMD_ACCESSLEVEL(0);
void inpSendCloseWindowMsg(void)
{
	if (gInput)
		gInput->dev.keyBindExec(INP_CLOSEWINDOW, 1, inpGetTime());
}

#define ENABLE_TRACE_INPUT 0
#if ENABLE_TRACE_INPUT
void wmInputTrace(const char * format, ...)
{
	va_list va;
	va_start(va, format);
	vprintf(format, va);
	va_end(va);
}
#else
#define wmInputTrace(fmt, ...)
#endif

static void InpHandleWinMsg(WinDXInpDev *inpdev, WinMsg *msg)
{
	UINT uMsg = msg->uMsg;
	WPARAM wParam = msg->wParam;
	LPARAM lParam = msg->lParam;
	static WPARAM s_LastWParam = 0;

	PERFINFO_AUTO_START_FUNC();
	switch (uMsg)
	{
		xcase WM_CLOSE:
		{
			// We closed the window, generate a fake key event
			if (gInput)
				gInput->dev.keyBindExec(INP_CLOSEWINDOW, 1, inpGetTime());
			inpdev->dev.inputActive = true;
		}

		xcase WM_CHAR:
		if (!input_state.bEnableRawInputManualCharTranslation)
		{
			S32 scancode = GetScancodeFromVirtualKey( s_LastWParam, lParam );
			KeyInputAttrib attrib = 0;
			wchar_t wChar;

			if(inpdev->ctrlKeyState)
				attrib |= KIA_CONTROL;
			if(inpdev->altKeyState)
				attrib |= KIA_ALT;
			if(inpdev->shiftKeyState)
				attrib |= KIA_SHIFT;

#if !_PS3
			if (wParam >= 32)
			{
				if (!inpdev->dev.bUnicodeRendererWindow)
				{
					MultiByteToWideChar(s_CodePageCurLang(), 0, (char *)&wParam, 1, &wChar, 2);
					inpKeyAddBuf(KIT_Text, scancode, wChar, attrib, wParam);
				}
				else
					inpKeyAddBuf(KIT_Text, scancode, wParam, attrib, wParam);
			}
#endif
			inpdev->dev.inputActive = true;
		}

		xcase WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		if (!input_state.bEnableDirectInputKeyboard && !input_state.bEnableRawInputSupport)
		{			
			S32 scancode = GetScancodeFromVirtualKey( wParam, lParam );
			KeyPressEvent *event = calloc(1, sizeof(KeyPressEvent));
			KeyInputAttrib attrib = 0;

			wmInputTrace("MT Key input, %d 0 %x %x %c\n", msg->timeStamp, scancode, wParam, wParam);

			if(globMovementLogIsEnabled){
				U32 msCurTime = inpGetTime();
			
				globMovementLog("[input] inputlib WM_%sKEYDOWN[%u] %dms late, %ums, real %ums",
								uMsg == WM_SYSKEYDOWN ? "SYS" : "",
								scancode,
								msCurTime - msg->timeStamp,
								msg->timeStamp,
								msCurTime);
			}

#if !_PS3   
			if(wParam == VK_CONTROL)
				inpdev->ctrlKeyState = 1;
			if(wParam == VK_MENU)
				inpdev->altKeyState = 1;
			if(wParam == VK_SHIFT)
				inpdev->shiftKeyState = 1;
			s_LastWParam = wParam;

			if(inpdev->ctrlKeyState)
				attrib |= KIA_CONTROL;
			if(inpdev->altKeyState)
				attrib |= KIA_ALT;
			if(inpdev->shiftKeyState)
				attrib |= KIA_SHIFT;

			// If numlock is on, numpad keys that have editing effects should
			// not be added to the EditKey queue. They'll be added to text
			// queue when the WM_CHAR event shows up. If numlock is off, we
			// don't even get these keys - we just get normal Home/Left/etc.
			if ((wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) || wParam == VK_DECIMAL)
				attrib |= KIA_NUMLOCK;

			inpKeyAddBuf(KIT_EditKey, scancode, 0, attrib, wParam);

			event->scancode = scancode;
			event->timestamp = msg->timeStamp;
			event->state = true;
			eaPush(&inpdev->keyEventQueue, event);
			inpdev->dev.inputActive = true;
#endif
		}

		xcase WM_KEYUP:
		case WM_SYSKEYUP:
		if (!input_state.bEnableDirectInputKeyboard && !input_state.bEnableRawInputSupport)
		{
			S32 scancode = 0;
			KeyPressEvent *event = calloc(1, sizeof(KeyPressEvent));

			scancode = GetScancodeFromVirtualKey( wParam, lParam );

			wmInputTrace("MT Key input, %d 0 %x %x %c\n", msg->timeStamp, scancode, wParam, wParam);

			if(globMovementLogIsEnabled){
				U32 msCurTime = inpGetTime();

				globMovementLog("[input] inputlib WM_%sKEYUP[%u] %dms late, %ums, real %ums",
								uMsg == WM_SYSKEYUP ? "SYS" : "",
								scancode,
								msCurTime - msg->timeStamp,
								msg->timeStamp,
								msCurTime);
			}

#if !_PS3 			
			if(wParam == VK_CONTROL)
				inpdev->ctrlKeyState = 0;
			else if(wParam == VK_MENU)
				inpdev->altKeyState = 0;
			else if(wParam == VK_SHIFT)
				inpdev->shiftKeyState = 0;
			else if (wParam == VK_SNAPSHOT)
			{
				// Windows doesn't give you keydowns for printkey
				KeyPressEvent *downEvent = calloc(1, sizeof(KeyPressEvent));
				scancode = INP_SYSRQ;
				downEvent->scancode = scancode;
				downEvent->timestamp = msg->timeStamp;
				downEvent->state = true;
				eaPush(&inpdev->keyEventQueue, downEvent);
			}

			event->scancode = scancode;
			event->timestamp = msg->timeStamp;
			event->state = false;
			eaPush(&inpdev->keyEventQueue, event);
			inpdev->dev.inputActive = true;
#endif
		}

		xcase WM_DEADCHAR:
		case WM_SYSCHAR:
		case WM_SYSDEADCHAR:
		case WM_KEYLAST:
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			inpdev->dev.inputActive = true;

		xcase WM_DEVICECHANGE:
			JoystickMonitorDeviceChange(wParam, (DEV_BROADCAST_DEVICEINTERFACE *) lParam);
	}

	PERFINFO_AUTO_STOP();
}

void inpMsgHandler(RdrDevice *device, void *userdata, RdrMsgType type, void *data, WTCmdPacket *packet)
{
	WinDXInpDev *inpdev = userdata;
	if (!inpdev)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
#if _PS3
#else
	switch (type)
	{

	xcase RDRMSG_WINMSG:
	{
		InpHandleWinMsg(inpdev,(WinMsg*)data);
	}

	xcase RDRMSG_DESTROY:
	{
		// destroy input device
		inpDestroyInputDevice((InputDevice *)inpdev);
	}

	xcase RDRMSG_SIZE:
	{
		int *size = data;
		inpdev->screenWidth = size[0];
		inpdev->screenHeight = size[1];
	}

	}
#endif

	PERFINFO_AUTO_STOP();
}

void inpWinMsgsHandler(RdrDevice *device, void *userdata, WinMsg **msgs)
{
	int i,s;
	WinDXInpDev *inpdev = userdata;
	if (!inpdev)
		return;

	s = eaSize(&msgs);

#if defined(INPUTLIB_DEBUG)
	if(s)
		printf("\nProcessing input messages...\n");
#endif

	for(i=0; i<s; i++)
		InpHandleWinMsg(inpdev, msgs[i]);
}

static FileScanAction scanAutoBinds(char *dir, struct _finddata32_t *data, void *pUserData)
{
	const char *ext = ".binds";
	int ext_len = (int)strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR)
		return FSA_EXPLORE_DIRECTORY;
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0)
		return FSA_EXPLORE_DIRECTORY;

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// Do actual processing
	keybind_LoadUserBinds(filename);


	return FSA_EXPLORE_DIRECTORY;
}

static void inpAutoBindsReloadCallback(const char *relpath, int when)
{
	if (strstr(relpath, "/_")) {
		return;
	}

	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	keybind_LoadUserBinds(relpath);
}

static void inpPrepareAutoBinds()
{
	//dynAnimLoadFilesInDir(pcDir, pcFileType, )
	fileScanAllDataDirs("autobinds", scanAutoBinds, NULL);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "autobinds/*.binds", inpAutoBindsReloadCallback);
}

bool ClientWindowIsBeingMovedOrResized()
{
	GUITHREADINFO info;
	info.cbSize = sizeof(GUITHREADINFO);
	GetGUIThreadInfo(0, &info);
	return info.flags & GUI_INMOVESIZE;
}

InputDevice *inpCreateInputDevice(RdrDevice *rdr,HINSTANCE hInstance, KeyBindExec bind, bool bUnicodeRendererWindow)
{
	WinDXInpDev *inp = calloc(sizeof(WinDXInpDev),1);
	HWND hwnd = (HWND)rdrGetWindowHandle(rdr);
#if !_PS3
	inp->hwnd = hwnd;
	inp->hInstance = hInstance;
#endif
	inp->firstFrame = 1;
	inp->dev.render = rdr;
	inp->dev.inputMouseHandled = false;
	inp->dev.inputMouseScrollHandled = false;
	inp->dev.keyBindExec = bind;
	inp->dev.bUnicodeRendererWindow = bUnicodeRendererWindow;
	inp->inp_captured = baCreate(INPARRAY_SIZE);
	inp->inp_edges = baCreate(INPARRAY_SIZE);
	inp->inp_levels = baCreate(INPARRAY_SIZE);
    if(rdr)
	    rdrSetMsgHandler(rdr, inpMsgHandler, inpWinMsgsHandler, (InputDevice *)inp);
	gInput = inp;
	InputStartup();
	inpPrepareAutoBinds(); // Is there a better place to put this?
    if(rdr)
	    rdrGetDeviceSize(rdr, NULL, NULL, &inp->screenWidth, &inp->screenHeight, NULL, NULL, NULL, NULL);
    else {
        inp->screenWidth = 640;
        inp->screenHeight = 480;
    }

	// If we have IME working, this is where we set it up
	return (InputDevice *)inp;
}

void inpDestroyInputDevice(InputDevice *inp)
{
	WinDXInpDev *dev = (WinDXInpDev *)inp;
	gInput = dev;
	InputShutdown();
	gInput = 0;
	// If we have IME working, this is where we destroy it
	baDestroy(dev->inp_captured);
	baDestroy(dev->inp_levels);
	baDestroy(dev->inp_edges);
	SAFE_FREE(dev);
}

InputDevice *inpGetActive(void)
{
	return (InputDevice *)gInput;
}
void inpSetActive(InputDevice *inp)
{
	PERFINFO_AUTO_START_FUNC();
	gInput = (WinDXInpDev *)inp;
	if (gInput && gInput->dev.render) {
		rdrGetDeviceSize(gInput->dev.render, NULL, NULL, &gInput->screenWidth, &gInput->screenHeight, NULL, NULL, NULL, NULL);
	}
	PERFINFO_AUTO_STOP();
}

void inpUpdateEarly(InputDevice *inp)
{
	PERFINFO_AUTO_START_FUNC_PIX();
	inpSetActive(inp);
	inpUpdateInternal();
	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void inpUpdateLate(InputDevice *inp)
{
	PERFINFO_AUTO_START_FUNC_PIX();
    if(inp)
    {
	    if (inp->keyBindExec)
	    {
		    S32 i;

#if defined(INPUTLIB_DEBUG)
			if(inp->keyBindQueueLength)
				printf("\nHandling late keybind queue...\n");
#endif

		    for (i = 0; i < inp->keyBindQueueLength; i++)
		    {
				bool edge_peek = inpEdgePeek(inp->keyBindQueue[i].iKey);
				bool is_captured = inpIsCaptured(inp->keyBindQueue[i].iKey);

#if defined(INPUTLIB_DEBUG)
				printf("  bState = %d, iKey = %d, edge_peek = %d, is_captured = %d\n", inp->keyBindQueue[i].bState, inp->keyBindQueue[i].iKey, edge_peek, is_captured);
#endif

			    // Don't run keybinds for things the UI just processed
			    if (!inp->keyBindQueue[i].bState || (edge_peek && !is_captured))
				    inp->keyBindExec(inp->keyBindQueue[i].iKey, inp->keyBindQueue[i].bState, inp->keyBindQueue[i].uiTime);
		    }
		    // because two keybinds could be set for a single key in one frame, we must clear the edges after executing all keybinds
		    for (i = 0; i < inp->keyBindQueueLength; i++)
		    {
			    inpEdge(inp->keyBindQueue[i].iKey);
		    }
	    }
	    inp->keyBindQueueLength = 0;
    }
	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void inpClearBuffer(InputDevice *inp)
{
	gInput->textKeyboardBuffer[0].type = KIT_None;
}

bool inpIsIgnoringInput()
{
	if (!gInput)
		return false;
	return gInput->dev.bIgnoringInput;
}

void inpBeginIgnoringInput()
{
	if (!gInput)
		return;
	gInput->dev.bIgnoringInput = true;
	if (input_state.bEnableRawInputSupport)
		RawInputBeginIgnoringInput();
	inpClear();
}

void inpStopIgnoringInput()
{
	if (!gInput)
		return;
	gInput->dev.bIgnoringInput = false;
	if (input_state.bEnableRawInputSupport)
		RawInputStopIgnoringInput();
	inpClear();
}

bool inpIsInactiveApp(InputDevice *inp)
{
	WinDXInpDev *input_device = (WinDXInpDev *)inp;
	return input_device->inactiveApp;
}

bool inpIsInactiveWindow(InputDevice *inp)
{
	WinDXInpDev *input_device = (WinDXInpDev *)inp;
	return input_device->inactiveWindow;
}

U32 inpLastInputEdgeTime(void)
{
	return gInput->lastInpEdgeTime;
}

U32 inpDeltaTimeToLastInputEdge(void)
{
	return inpGetTime() - gInput->lastInpEdgeTime;
}

void inpGetDeviceScreenSize(int * size)
{
	if (gInput)
	{
		size[0] = gInput->screenWidth;
		size[1] = gInput->screenHeight;
	}
	else
	{
		size[0] = 0;
		size[1] = 0;
	}
}
