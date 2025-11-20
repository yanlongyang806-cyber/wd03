/***************************************************************************



***************************************************************************/

#include "earray.h"
#include "inputLib.h"
#include "input.h"
#include "inputRaw.h"
#include "inputMouse.h"
#include "inputJoystickInternal.h"

#include <ctype.h>

#include "StashTable.h"
#include "inputCommandParse.h"
#include "inputKeyBind.h"
#include "RenderLib.h"
#include "WorkerThread.h"
#include "ThreadManager.h"

// to get GetCPUTicks64 - weird place for that function to be?
#include "TaskProfile.h"
#include "memlog.h"
#include "file.h"

#include "sysutil.h"

#if _XBOX
	#include "x360/inputKeyboard360.h"
#else
	#include <windows.h>
	#include "winuser.h"
	#pragma comment(lib, "..\\..\\3rdparty\\DirectX\\Lib\\Xinput.lib")
	#pragma comment(lib, "..\\..\\3rdparty\\DirectX\\Lib\\dinput8.lib")
#endif

//#define INPUT_DEBUG

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define EXTRA_PROFILING 1

static bool s_bPrintKeys = false;

// Print all keypresses to the console.
AUTO_CMD_INT(s_bPrintKeys, InputPrintKeys) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

//////////////////////////////////////////////////////////////////////////

__forceinline static DWORD GetTickCount_timed(void)
{
	DWORD count=0;
	PERFINFO_AUTO_START("GetTickCount",1);
	if (!input_state.skipMousePolling)
		count = GetTickCount();
	PERFINFO_AUTO_STOP();
	return count;
}

__forceinline static UINT GetDoubleClickTime_timed(void)
{
	UINT dtime=100;
	PERFINFO_AUTO_START("GetDoubleClickTime",1);
	if (!input_state.skipMousePolling)
		dtime = GetDoubleClickTime();
	PERFINFO_AUTO_STOP();
	return dtime;
}

__forceinline static int GetCursorPos_timed(POINT* p)
{
	int ret=0;
	PERFINFO_AUTO_START("GetCursorPos",1);
	if (!input_state.skipMousePolling)
		ret = GetCursorPos(p);
	PERFINFO_AUTO_STOP();
	return ret;
}

__forceinline static int SetCursorPos_timed(int x, int y)
{
	int ret=0;
	PERFINFO_AUTO_START("SetCursorPos",1);
	if (!input_state.skipMousePolling)
#if _XBOX
	{
		SetCursorPos(x,y);
		ret = 1;
	}
#else
		ret = SetCursorPos(x,y);
#endif
	PERFINFO_AUTO_STOP();
	return ret;
}



__forceinline static BOOL ClientToScreen_timed(HWND hwnd, POINT* p)
{
	BOOL ret=TRUE;
	PERFINFO_AUTO_START("ClientToScreen",1);
	if (!input_state.skipMousePolling)
		ret = ClientToScreen(hwnd, p);
	PERFINFO_AUTO_STOP();
	return ret;
}

#if EXTRA_PROFILING
#define GetTickCount			GetTickCount_timed
#define GetDoubleClickTime		GetDoubleClickTime_timed
#define GetCursorPos			GetCursorPos_timed
#define SetCursorPos			SetCursorPos_timed
#define ClientToScreen			ClientToScreen_timed
#endif

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

WinDXInpDev *gInput;

#define KEYBOARD_BUFFER_SIZE 128
#define MOUSE_BUFFER_SIZE  256

static ConsoleInputProcessor inpConsoleProcessInput;

int GetScancodeFromVirtualKey(WPARAM wParam, LPARAM lParam);

void inpSetConsoleInputProcessor(ConsoleInputProcessor processor)
{
	inpConsoleProcessInput = processor;
}

void KeyboardShutdown(){
	if(gInput->KeyboardDev){ 
		gInput->KeyboardDev->lpVtbl->Unacquire(gInput->KeyboardDev); 
		gInput->KeyboardDev->lpVtbl->Release(gInput->KeyboardDev);
		gInput->KeyboardDev = NULL; 
	} 
}

int inpKeyboardAcquire(){
	HRESULT hr;

	if(gInput->KeyboardDev){
		hr = gInput->KeyboardDev->lpVtbl->Acquire(gInput->KeyboardDev); 
		if(FAILED(hr)){
			return 0;
		}

		return 1;
	}

	return 0;
}

int KeyboardStartup(){
	HRESULT               hr; 
	
	if (!input_state.bEnableDirectInputKeyboard)
		return 0;

	if (!gInput->DirectInput8)
		goto KeyboardStartupError;

	hr = gInput->DirectInput8->lpVtbl->CreateDevice(gInput->DirectInput8, &GUID_SysKeyboard, &gInput->KeyboardDev, NULL); 
	if(FAILED(hr))
		goto KeyboardStartupError;


	hr = gInput->KeyboardDev->lpVtbl->SetDataFormat(gInput->KeyboardDev, &c_dfDIKeyboard);  
	if(FAILED(hr))
		goto KeyboardStartupError;

	// Set the cooperative level.
	//	By default:
	//		1.  The input system stops receiving keyboard input when the game window lose focus.
	//		2.  The input system allows windows messages for the keyboard to be generated.
	//		3.  The input system disables the windows key. (Cannot escape app using the windows key)
	hr = gInput->KeyboardDev->lpVtbl->SetCooperativeLevel(gInput->KeyboardDev, gInput->hwnd, 
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);// | DISCL_NOWINKEY);
	if(FAILED(hr))
		goto KeyboardStartupError;

	// Put the DirectInput keyboard into buffered mode.
	{
		DIPROPDWORD propWord;

		// JS:	This is totally dumb.  Why do I have to prepare this header for DirectInput
		//		if it *must* be these values when I am setting the buffer size property?
		propWord.diph.dwSize =  sizeof(DIPROPDWORD);
		propWord.diph.dwHeaderSize = sizeof(DIPROPHEADER);
		propWord.diph.dwObj = 0;
		propWord.diph.dwHow = DIPH_DEVICE;
		propWord.dwData = KEYBOARD_BUFFER_SIZE;

		gInput->KeyboardDev->lpVtbl->SetProperty(gInput->KeyboardDev, DIPROP_BUFFERSIZE, &propWord.diph);
	}
	inpKeyboardAcquire();

	return 0;
KeyboardStartupError:
	KeyboardShutdown();
	return 1;
}

static int inpMouseAcquire( void )
{
#if !_PS3
	PERFINFO_AUTO_START_FUNC();

	if(gInput->MouseDev)
	{
		HRESULT hr = gInput->MouseDev->lpVtbl->Acquire(gInput->MouseDev); 
		if( SUCCEEDED(hr))
		{
			PERFINFO_AUTO_STOP();
			return 1;
		}
	}

	PERFINFO_AUTO_STOP();
#endif
	return 0;
}

int MouseStartup( void )
{
#if !_PS3
	static DIPROPDWORD dpd;
	HRESULT               hr; 
	
	if (!gInput->DirectInput8)
		return 1;

	hr = gInput->DirectInput8->lpVtbl->CreateDevice(gInput->DirectInput8, &GUID_SysMouse, &gInput->MouseDev, NULL); 
	if( FAILED( hr ) )
		return 1;

	hr = gInput->MouseDev->lpVtbl->SetDataFormat( gInput->MouseDev, &c_dfDIMouse2 );  
	if( FAILED( hr ) )
		return 1;

	hr = gInput->MouseDev->lpVtbl->SetCooperativeLevel( gInput->MouseDev, gInput->hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE );
	if( FAILED(hr) )
		return 1;

    dpd.diph.dwSize       = sizeof(DIPROPDWORD);
    dpd.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dpd.diph.dwObj        = 0;
    dpd.diph.dwHow        = DIPH_DEVICE;

    // the data
    dpd.dwData            = MOUSE_BUFFER_SIZE;

	hr = gInput->MouseDev->lpVtbl->SetProperty( gInput->MouseDev, DIPROP_BUFFERSIZE, &dpd.diph);
 
	if ( FAILED(hr) ) 
		 return 1;

	inpMouseAcquire();

	inpMousePos( &gInput->mouseInpCur.x, &gInput->mouseInpCur.y );

#endif
	return 0;
}

void MouseShutdown( void )
{
	if(gInput->MouseDev)
	{ 
		gInput->MouseDev->lpVtbl->Unacquire( gInput->MouseDev ); 
		gInput->MouseDev->lpVtbl->Release( gInput->MouseDev );
		gInput->MouseDev = NULL; 
    } 
}

AUTO_RUN;
int inputAutoStartup(void)
{
	// Happens before getting into main()  CANNOT call any file access functions here!	
	cmdSetGlobalCmdParseFunc(keybind_CmdParse);
	// This will get overridden by the gameclient

	if(getWineVersion()) {
		// Running under WINE, which only introduced raw input support as of 1.5.13...
		input_state.bEnableRawInputSupport = getWineVersionOrLater(1, 5, 13);
	} else {
		input_state.bEnableRawInputSupport = true;
	}

	input_state.bEnableRawInputManualCharTranslation = false;
	return 1;
}

const ManagedThread *inpGetDeviceRenderThread(const WinDXInpDev *pInput)
{
	const WorkerThread *renderWorkerThread = pInput->dev.render->worker_thread;
	return wtGetWorkerThreadPtr(renderWorkerThread);
}

bool inpIsDeviceRenderThreaded(const WinDXInpDev *pInput)
{
	return inpGetDeviceRenderThread(pInput) != NULL;
}

int InputStartup()
{
#if _XBOX 
	return 0;
#elif _PS3
	inpStartupGamepadPs3();
	inpStartupKeyboardPs3();
	return 0;
#else
	{
		bool bRawStartupSuccess = false;
		HRESULT hr;

		if (input_state.bEnableRawInputSupport)
		{
			const ManagedThread *inputThread = inpGetDeviceRenderThread(gInput);
			if (!inputThread)
				inputThread = tmGetMainThread();
			bRawStartupSuccess = RawInputStartup(inputThread);

			memlog_printf(NULL, "Raw input startup %s\n", bRawStartupSuccess ? "succeeded" : "failed");
			if (!bRawStartupSuccess)
			{
				// else fall through to other input system
				input_state.bEnableRawInputSupport = false;
				input_state.bEnableRawInputManualCharTranslation = false;
			}
		}

		// Initialize gInput->DirectInput8.
		hr = DirectInput8Create(gInput->hInstance, DIRECTINPUT_VERSION, &IID_IDirectInput8, (void**)&gInput->DirectInput8, NULL);

		if(FAILED(hr)) 
			goto fail;

		// If both the keyboard and the mouse starts up successfully,
		// the input system started up correctly.
		if(!bRawStartupSuccess && !KeyboardStartup() && !MouseStartup() )
		{
			JoystickStartup();
			return 0;
		}
		else if (bRawStartupSuccess)
		{
			JoystickStartup();
			return 0;
		}
		
		// Otherwise, the input system was not started up correctly.
		// Shutdown to release all acquired resources.
		InputShutdown();
	fail:
		return 1;
	}
#endif
}


void InputShutdown()
{
#if _PS3
	inpShutdownGamepadPs3();
	inpShutdownKeyboardPs3();
#else
	RawInputShutdown();


	KeyboardShutdown();
	MouseShutdown();
	JoystickShutdown();

	if (gInput->DirectInput8){ 
		gInput->DirectInput8->lpVtbl->Release(gInput->DirectInput8);
        gInput->DirectInput8 = NULL; 
	}
#endif
}

__forceinline static int inpLevel2(int idx1, int idx2)
{
	if (!gInput)
		return 0;
	if (baIsSetInline(gInput->inp_captured, idx1) || baIsSetInline(gInput->inp_captured, idx2))
		return 0;
	baSetBit(gInput->inp_captured, idx1);
	baSetBit(gInput->inp_captured, idx2);
	return baIsSetInline(gInput->inp_levels, idx1) || baIsSetInline(gInput->inp_levels, idx2);
}

int inpLevel(int idx)
{
	if (!gInput)
		return 0;

	switch(idx){
	case INP_CONTROL:
		return inpLevel2(INP_RCONTROL, INP_LCONTROL);
	case INP_SHIFT:
		return inpLevel2(INP_RSHIFT, INP_LSHIFT);
	case INP_ALT:
		return inpLevel2(INP_RMENU, INP_LMENU);
	default:
		if (baIsSetInline(gInput->inp_captured, idx))
			return 0;
		baSetBit(gInput->inp_captured, idx);
		return baIsSetInline(gInput->inp_levels, idx);
	}
}

int inpLevelPeek(int idx)
{
	if (!gInput)
		return 0;

	switch(idx){
	case INP_CONTROL:
		return baIsSetInline(gInput->inp_levels, INP_RCONTROL) || baIsSetInline(gInput->inp_levels, INP_LCONTROL);
	case INP_SHIFT:
		return baIsSetInline(gInput->inp_levels, INP_RSHIFT) || baIsSetInline(gInput->inp_levels, INP_LSHIFT);
	case INP_ALT:
		return baIsSetInline(gInput->inp_levels, INP_RMENU) || baIsSetInline(gInput->inp_levels, INP_LMENU);
	default:
		return baIsSetInline(gInput->inp_levels, idx);
	}
}

__forceinline static int inpEdge2(int idx1, int idx2)
{
	if (!gInput)
		return 0;
	if (baIsSetInline(gInput->inp_captured, idx1) || baIsSetInline(gInput->inp_captured, idx2))
		return 0;
	baSetBit(gInput->inp_captured, idx1);
	baSetBit(gInput->inp_captured, idx2);
	return baIsSetInline(gInput->inp_edges, idx1) || baIsSetInline(gInput->inp_edges, idx2);
}

int inpEdge(int idx)
{
	if (!gInput)
		return 0;
	switch(idx){
	case INP_CONTROL:
		return inpEdge2(INP_RCONTROL, INP_LCONTROL);
	case INP_SHIFT:
		return inpEdge2(INP_RSHIFT, INP_LSHIFT);
	case INP_ALT:
		return inpEdge2(INP_RMENU, INP_LMENU);
	default:
		if (inpIsCaptured(idx))
			return 0;
		inpCapture(idx);
		return baIsSetInline(gInput->inp_edges, idx);
	}
}

int inpEdgePeek(int idx)
{
	if (!gInput)
		return 0;
	switch(idx){
	case INP_CONTROL:
		return baIsSetInline(gInput->inp_edges, INP_RCONTROL) || baIsSetInline(gInput->inp_edges, INP_LCONTROL);
	case INP_SHIFT:
		return (baIsSetInline(gInput->inp_edges, INP_RSHIFT) || baIsSetInline(gInput->inp_edges, INP_LSHIFT));
	case INP_ALT:
		return baIsSetInline(gInput->inp_edges, INP_RMENU) || baIsSetInline(gInput->inp_edges, INP_LMENU);
	default:
		return baIsSetInline(gInput->inp_edges, idx);
	}
}

void inpCapture(int idx)
{
	if (!gInput)
		return;
	baSetBit(gInput->inp_captured, idx);
}

bool inpIsCaptured(int idx)
{
	if (!gInput)
		return 0;
	return baIsSetInline(gInput->inp_captured, idx);
}

bool inpEdgeThisFrame(void)
{
	if (!gInput)
		return 0;
	return !!baCountSetBits(gInput->inp_edges);
}

void inpKeyAddBuf(KeyInputType type, S32 scancode, S32 character, KeyInputAttrib attrib, S32 vkey)
{
	int len;

	// Please don't try to trick the code into adding an invalid entry
	// into the keyboard buffer.
	assert(KIT_None != type);

	if (!gInput)
		return;

	// High quality solutions to finding the end of array.
	for(len = 0; len < ARRAY_SIZE(gInput->textKeyboardBuffer); len++){
		if(KIT_None == gInput->textKeyboardBuffer[len].type)
			break;
	}

	if (len > ARRAY_SIZE(gInput->textKeyboardBuffer) - 1){
		printf("Text keyboard buffer overflow!");
		return;
	}

#if defined(INPUT_DEBUG)
	printf("  Adding keyboard input message to buffer... type = %d, scancode = %d, character = %c\n", type, scancode, character);
#endif

	gInput->textKeyboardBuffer[len].type = type;
	gInput->textKeyboardBuffer[len].scancode = scancode;
	gInput->textKeyboardBuffer[len].character = character;
	gInput->textKeyboardBuffer[len].attrib = attrib;
	gInput->textKeyboardBuffer[len].vkey = vkey;

	if(len + 1 < ARRAY_SIZE(gInput->textKeyboardBuffer))
		gInput->textKeyboardBuffer[len+1].type = KIT_None;
}

KeyInput *inpGetKeyBuf()
{
	if (!gInput || KIT_None == gInput->textKeyboardBuffer[0].type)
		return NULL;
	return gInput->textKeyboardBuffer;
}

void inpGetNextKey(KeyInput** input){
	
	// Invalid parameters.
	if(!input || !*input)
		return;

	// Range check.  Is the caller trying to read a random piece of memory as a KeyInput?
	assert(*input >= gInput->textKeyboardBuffer && *input <= (gInput->textKeyboardBuffer + ARRAY_SIZE(gInput->textKeyboardBuffer)));
	
	// If the caller is already pointing to the last element (very unlikely), reply that there are no more inputs.
	if(*input >= gInput->textKeyboardBuffer + ARRAY_SIZE(gInput->textKeyboardBuffer) - 1){
		*input = NULL;
		return;
	}
	
	// If there are no more valid inputs, say so.
	if(KIT_None == ((*input)+1)->type){
		*input = NULL;
		return;
	}

	(*input)++;
}

bool inpMousePosOnScreen(int x, int y)
{
 	if (x < 0 || y < 0 || x >= gInput->screenWidth || y >= gInput->screenHeight)
		return false;

	return true;
}

int inpMousePos(int *xp,int *yp)
{
#if PLATFORM_CONSOLE
	inpLastMousePos(xp, yp);
	return 0;
#else
	POINT	pCursor = {0}, pClient = {0, 0};
	int		x, y, outside=0, size;

	if (!gInput)
		return 1;

	PERFINFO_AUTO_START_FUNC();

	GetCursorPos(&pCursor);

	size = ClientToScreen( gInput->hwnd, &pClient );
 
	x = pCursor.x - pClient.x;
	y = pCursor.y - pClient.y;	

	transformMousePos(x, y, xp, yp);

	if (x < 0 || y < 0 || x >= gInput->screenWidth || y >= gInput->screenHeight)
		outside = 1;

	PERFINFO_AUTO_STOP();

	return outside;
#endif
}

void inpLastMousePos(int *xp, int *yp)
{
	if(gInput) 
	{
		transformMousePos(gInput->mouseInpCur.x, gInput->mouseInpCur.y, xp, yp);
    } 
	else 
	{
        *xp = 0;
        *yp = 0;
    }
}

int inpFocus()
{
	int x, y;
	if (!gInput)
		return 0;
	if (inpIsInactiveApp(&gInput->dev))
		return 0;
	return !inpMousePos(&x, &y);
}

void inpClear()
{
	int i;
	const S32 curTime = inpGetTime();

	if (!gInput)
		return;
		
	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < INPARRAY_SIZE; i++)
		inpUpdateKeyClearIfActive(i, curTime);
	baClearAllBits(gInput->inp_levels);
	baClearAllBits(gInput->inp_edges);
	baClearAllBits(gInput->inp_captured);
	memset(gInput->textKeyboardBuffer, 0, sizeof(gInput->textKeyboardBuffer));
	memset(gInput->buttons, 0, sizeof(gInput->buttons));
	memset(gInput->mouseInpBuf, 0, sizeof(gInput->mouseInpBuf));
	memset(&gInput->mouseInpCur.states, 0, sizeof(gInput->mouseInpCur.states));
	gInput->mouseInpLast = gInput->mouseInpCur;
	gInput->mouseInpSaved = gInput->mouseInpSaved;
	gInput->mouseBufSize = 0;
	gInput->altKeyState = 0;
	gInput->ctrlKeyState = 0;
	gInput->shiftKeyState = 0;

	PERFINFO_AUTO_STOP();
}

static char scan2ascii(DWORD scancode)
{
	UINT vk;
	static HKL layout;
	static unsigned char State[256];
	unsigned short result = 0;

	layout = GetKeyboardLayout(0);
	if (GetKeyboardState(State) == FALSE)
		return 0;
	vk = MapVirtualKeyEx(scancode,1,layout);
	ToAsciiEx(vk,scancode,State,&result,0,layout);
	return (char)result;
}

#define ASCIIENTRY(c) { #c , INP_##c }
#define ASCIIENTRY2(c, key) { c, key }
static struct  
{
	char ascii[4];
	int key;
} ascii_conversion_table[] = 
{
	ASCIIENTRY(0),
	ASCIIENTRY(1),
	ASCIIENTRY(2),
	ASCIIENTRY(3),
	ASCIIENTRY(4),
	ASCIIENTRY(5),
	ASCIIENTRY(6),
	ASCIIENTRY(7),
	ASCIIENTRY(8),
	ASCIIENTRY(9),
	ASCIIENTRY2('!', INP_1),
	ASCIIENTRY2('@', INP_2),
	ASCIIENTRY2('#', INP_3),
	ASCIIENTRY2('$', INP_4),
	ASCIIENTRY2('%', INP_5),
	ASCIIENTRY2('^', INP_6),
	ASCIIENTRY2('&', INP_7),
	ASCIIENTRY2('*', INP_8),
	ASCIIENTRY2('(', INP_9),
	ASCIIENTRY2(')', INP_0),
	ASCIIENTRY(A),
	ASCIIENTRY(B),
	ASCIIENTRY(C),
	ASCIIENTRY(D),
	ASCIIENTRY(E),
	ASCIIENTRY(F),
	ASCIIENTRY(G),
	ASCIIENTRY(H),
	ASCIIENTRY(I),
	ASCIIENTRY(J),
	ASCIIENTRY(K),
	ASCIIENTRY(L),
	ASCIIENTRY(M),
	ASCIIENTRY(N),
	ASCIIENTRY(O),
	ASCIIENTRY(P),
	ASCIIENTRY(Q),
	ASCIIENTRY(R),
	ASCIIENTRY(S),
	ASCIIENTRY(T),
	ASCIIENTRY(U),
	ASCIIENTRY(V),
	ASCIIENTRY(W),
	ASCIIENTRY(X),
	ASCIIENTRY(Y),
	ASCIIENTRY(Z),
	ASCIIENTRY2("-", INP_MINUS),
	ASCIIENTRY2("_", INP_MINUS),
	ASCIIENTRY2("+", INP_EQUALS),
	ASCIIENTRY2("=", INP_EQUALS),
	ASCIIENTRY2("[", INP_LBRACKET),
	ASCIIENTRY2("{", INP_LBRACKET),
	ASCIIENTRY2("]", INP_RBRACKET),
	ASCIIENTRY2("}", INP_RBRACKET),
	ASCIIENTRY2(";", INP_SEMICOLON),
	ASCIIENTRY2(":", INP_SEMICOLON),
	ASCIIENTRY2("\'", INP_APOSTROPHE),
	ASCIIENTRY2("\"", INP_APOSTROPHE),
	ASCIIENTRY2("`", INP_GRAVE),
	ASCIIENTRY2("~", INP_GRAVE),
	ASCIIENTRY2("\\", INP_BACKSLASH),
	ASCIIENTRY2("|", INP_BACKSLASH),
	ASCIIENTRY2(",", INP_COMMA),
	ASCIIENTRY2("<", INP_COMMA),
	ASCIIENTRY2(".", INP_PERIOD),
	ASCIIENTRY2(">", INP_PERIOD),
	ASCIIENTRY2("/", INP_SLASH),
	ASCIIENTRY2("?", INP_SLASH),
	ASCIIENTRY2(" ", INP_SPACE),
};

static StashTable ascii_conversion;
int inpAsciiToKey(char c)
{
	int key;
	if (!ascii_conversion)
	{
		int i;
		ascii_conversion = stashTableCreateInt(128);
		for (i = 0; i < ARRAY_SIZE(ascii_conversion_table); i++)
		{
			stashIntAddInt(ascii_conversion, ascii_conversion_table[i].ascii[0], ascii_conversion_table[i].key, true);
		}
	}

	c = toupper(c);
	if (stashIntFindInt(ascii_conversion, c, &key))
		return key;
	return c;
}

unsigned char cur_key_states[256]; //debug only
void inpKeyboardUpdate(void)
{
	HRESULT hr;
	DIDEVICEOBJECTDATA keyboardData[KEYBOARD_BUFFER_SIZE];
	int keyDataSize = KEYBOARD_BUFFER_SIZE;

	if (!gInput)
		return;
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (input_state.bEnableRawInputSupport)
		RawInputUpdate();


	if (gInput->KeyboardDev)
	{
		// Standard keyboard
		int i;

		// Empty the keyboard buffer.
		hr = gInput->KeyboardDev->lpVtbl->GetDeviceData(gInput->KeyboardDev, sizeof(DIDEVICEOBJECTDATA), keyboardData, &keyDataSize, 0);

		// If the attempt was unsuccessful, the most likely reason is because
		// the keyboard has become unacquired.
		// 
		// Acquire the keyboard and try to get the keyboard state again.
		if(FAILED(hr)){
			inpKeyboardAcquire();
			hr = gInput->KeyboardDev->lpVtbl->GetDeviceData(gInput->KeyboardDev, sizeof(DIDEVICEOBJECTDATA), keyboardData, &keyDataSize, 0);

			if(FAILED(hr))
			{
				inpClear();
				PERFINFO_AUTO_STOP();
				return;
			}
		}
		//gfxXYprintf(10, 9, "raw Dx keys: %s", inpDisplayState(cur_key_states));
		//gInput->textKeyboardBuffer[0].type = KIT_None;

		// Playback the buffered data to find out what the accumulated keyboard state
		// since the last time the keyboard was checked.

		/* DJR the input filter feature is gone. Leaving code to minimize diff for the initial restore of the DInput keyboard input processing
		if(gInput->dev.inputFilter && !gInput->dev.inputFilter(INP_MOUSE_BUTTONS,0))
		{
			baClearBitRange(gInput->inp_edges, 0, INP_MOUSE_BUTTONS);
		}
		*/

		for(i = 0; i < keyDataSize; i++)
		{
			inpUpdateKey(keyboardData[i].dwOfs, keyboardData[i].dwData, keyboardData[i].dwTimeStamp);
		}
	}

#if _XBOX
	inpKeyboardUpdate360();
#elif _PS3
	inpKeyboardUpdatePs3();
#else
	{
#if defined(INPUT_DEBUG)
		if(eaSize(&gInput->keyEventQueue))
			printf("\nPlaying back key events queued by input message handling...\n");
#endif

		// Play back queued events, then free them.
		EARRAY_CONST_FOREACH_BEGIN(gInput->keyEventQueue, i, isize);
			inpUpdateKey(gInput->keyEventQueue[i]->scancode, gInput->keyEventQueue[i]->state, gInput->keyEventQueue[i]->timestamp);
		EARRAY_FOREACH_END;
		eaDestroyEx(&gInput->keyEventQueue, NULL);
	}
#endif

    PERFINFO_AUTO_STOP();
}

int dxMouseToInpKeyMapping[][2] = {
	{DIMOFS_BUTTON1, INP_LBUTTON},
	{DIMOFS_BUTTON2, INP_MBUTTON},
	{DIMOFS_BUTTON0, INP_RBUTTON},
	{DIMOFS_BUTTON3, INP_BUTTON4},
	{DIMOFS_BUTTON4, INP_BUTTON5},
	{DIMOFS_BUTTON5, INP_BUTTON6},
	{DIMOFS_BUTTON6, INP_BUTTON7},
	{DIMOFS_BUTTON7, INP_BUTTON8},
	{DIMOFS_Z, INP_MOUSEWHEEL},
};

int inpKeyFromDxMouseKey(int dxMouseKey)
{
	int mouseKeyCursor;

	for(mouseKeyCursor = 0; mouseKeyCursor < ARRAY_SIZE(dxMouseToInpKeyMapping); mouseKeyCursor++)
	{
		if(dxMouseToInpKeyMapping[mouseKeyCursor][0] == dxMouseKey)
		{
			return dxMouseToInpKeyMapping[mouseKeyCursor][1];
		}
	}
	return 0;
}


unsigned int gClickTime = 250;
int gClickSize = 8;
int gDoubleClickSize = 2;

AUTO_CMD_INT(gClickTime, InputClickTime) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
AUTO_CMD_INT(gClickSize, InputClickSize) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
AUTO_CMD_INT(gDoubleClickSize, InputDoubleClickSize) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0) ACMD_HIDE;

///////////////////////////////////////////////////////////////////////////////////////////
// SubFunctions for handling mouse clicks
///////////////////////////////////////////////////////////////////////////////////////////

void inpMouseClick(DIDEVICEOBJECTDATA *didod, S32 button, S32 clickScancode, S32 doubleClickScancode)
{
	bool click = false, double_click = false;

	if (!gInput)
		return;

	if ( (didod->dwData & 0x80) && gInput->mouseInpCur.states[button] == MS_NONE )
	{
		if (inpMousePosOnScreen(gInput->mouseInpCur.x, gInput->mouseInpCur.y))
		{
			gInput->mouseInpBuf[gInput->mouseBufSize].states[button] = MS_DOWN;
			gInput->mouseInpBuf[gInput->mouseBufSize].x = gInput->mouseInpCur.x;
			gInput->mouseInpBuf[gInput->mouseBufSize].y = gInput->mouseInpCur.y;
			gInput->mouseInpCur.states[button] = MS_DOWN;
			gInput->mouseBufSize++;

			// doing these double-click messages as additive on down/up
			if (gInput->buttons[button].mbtime != gInput->buttons[button].dctime && // last click wasn't a double-click event
				didod->dwTimeStamp - gInput->buttons[button].mbtime < gInput->mouseDoubleClickTime && 
				(ABS(gInput->buttons[button].clickx-gInput->mouseInpCur.x) <= gDoubleClickSize) &&
				(ABS(gInput->buttons[button].clicky-gInput->mouseInpCur.y) <= gDoubleClickSize) )
			{
				gInput->mouseInpBuf[gInput->mouseBufSize].states[button] = MS_DBLCLICK;
				gInput->mouseInpBuf[gInput->mouseBufSize].x = gInput->mouseInpCur.x;
				gInput->mouseInpBuf[gInput->mouseBufSize].y = gInput->mouseInpCur.y;
				gInput->mouseBufSize++;
				gInput->buttons[button].dctime = didod->dwTimeStamp;
				inpUpdateKey(doubleClickScancode, true, didod->dwTimeStamp);
				double_click = true;
			}

			gInput->buttons[button].state = MS_DOWN;
			gInput->buttons[button].curx = gInput->buttons[button].downx = gInput->mouseInpCur.x;
			gInput->buttons[button].cury = gInput->buttons[button].downy = gInput->mouseInpCur.y;
			gInput->buttons[button].mbtime = didod->dwTimeStamp; 
		}
	}
	else if ( gInput->mouseInpCur.states[button] == MS_DOWN && !(didod->dwData & 0x80) )
	{
		gInput->mouseInpBuf[gInput->mouseBufSize].states[button] = MS_UP;
		gInput->mouseInpBuf[gInput->mouseBufSize].x = gInput->mouseInpCur.x;
		gInput->mouseInpBuf[gInput->mouseBufSize].y = gInput->mouseInpCur.y;
		gInput->mouseInpCur.states[button] = MS_NONE;
		gInput->mouseBufSize++;
		gInput->buttons[button].drag = false;

		// decide if we have a click
		if (gInput->buttons[button].state == MS_DOWN)
		{
			if (gInput->buttons[button].mbtime != gInput->buttons[button].dctime &&
				(ABS(gInput->buttons[button].downx - gInput->buttons[button].curx) <= gClickSize) &&
				(ABS(gInput->buttons[button].downy - gInput->buttons[button].cury) <= gClickSize) &&
				((gInput->buttons[button].mbtime - didod->dwTimeStamp <= gClickTime) ||
				(didod->dwTimeStamp - gInput->buttons[button].mbtime <= gClickTime)))
			{
				gInput->mouseInpBuf[gInput->mouseBufSize].states[button] = MS_CLICK;
				gInput->mouseInpBuf[gInput->mouseBufSize].x = gInput->buttons[button].downx;
				gInput->mouseInpBuf[gInput->mouseBufSize].y = gInput->buttons[button].downy;
				gInput->buttons[button].clickx = gInput->buttons[button].downx;
				gInput->buttons[button].clicky = gInput->buttons[button].downy;
				gInput->mouseBufSize++;
				inpUpdateKey(clickScancode, true, didod->dwTimeStamp);
				click = true;
			}
			gInput->buttons[button].state = MS_NONE;
		}
	}

	if (!click)
		inpUpdateKeyClearIfActive(clickScancode, didod->dwTimeStamp );
	if (!double_click)
		inpUpdateKeyClearIfActive(doubleClickScancode, didod->dwTimeStamp );
}

void inpReadMouse( void )
{

    DIDEVICEOBJECTDATA didod[MOUSE_BUFFER_SIZE];  // Receives buffered data 
    DWORD dwElements = MOUSE_BUFFER_SIZE;
    U32 i;
    HRESULT hr;
	int updatedMouseChord = 0;
	const S32 curTime = inpGetTime();
	S64 curCpuTime;

	int iMouseDeltaForThisFrame = 0;

	PERFINFO_AUTO_START_FUNC();

	if (!gInput) {
		PERFINFO_AUTO_STOP();
		return;
	}

	gInput->mouseInpLast = gInput->mouseInpCur;
	if (!gInput->mouseDoubleClickTime) {
		PERFINFO_AUTO_START("GetDoubleClickTime",1);
		gInput->mouseDoubleClickTime = GetDoubleClickTime(); // Calling this every frame sometimes creates large (10s of ms) stalls, although not calling it seems to just move the stalls elsewhere...
		PERFINFO_AUTO_STOP();
	}
	if (input_state.reverseMouseButtons) {
		dxMouseToInpKeyMapping[0][0] = DIMOFS_BUTTON1;
		dxMouseToInpKeyMapping[2][0] = DIMOFS_BUTTON0;
	} else {
		dxMouseToInpKeyMapping[0][0] = DIMOFS_BUTTON0;
		dxMouseToInpKeyMapping[2][0] = DIMOFS_BUTTON1;
	}
#ifdef _XBOX
    if( NULL == gInput->MouseDev )
	{

		inpMouseUpdate360();
		PERFINFO_AUTO_STOP();
		return;
	}
#endif

	PERFINFO_AUTO_START("inpMouseAcquire",1);

	PERFINFO_AUTO_STOP_START("inpUpdateKeyClearIfActive",1);

	// We clear these every time we read the mouse because we don't want them to appear to be "held" [RMARR - 9/30/11]
	if (baIsSetInline(gInput->inp_levels, INP_MOUSEWHEEL_FORWARD))
		baClearBit(gInput->inp_levels, INP_MOUSEWHEEL_FORWARD);
	if (baIsSetInline(gInput->inp_levels, INP_MOUSEWHEEL_BACKWARD))
		baClearBit(gInput->inp_levels, INP_MOUSEWHEEL_BACKWARD);

	baClearBit(gInput->inp_levels, INP_LCLICK);
	baClearBit(gInput->inp_levels, INP_MCLICK);
	baClearBit(gInput->inp_levels, INP_RCLICK);
	baClearBit(gInput->inp_levels, INP_LDBLCLICK);
	baClearBit(gInput->inp_levels, INP_MDBLCLICK);
	baClearBit(gInput->inp_levels, INP_RDBLCLICK);

	PERFINFO_AUTO_STOP_START("clear old buffer",1);
	//clear old buffer
 	memset( &gInput->mouseInpBuf, 0, sizeof(mouse_input) * MOUSE_INPUT_SIZE );
	gInput->mouseBufSize = 0;
	gInput->mouseInpCur.z = 0;

	if(!inpMouseAcquire()) // need to clear input even if mouse isn't acquired 
	{
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}
	
	curCpuTime = GetCPUTicks64();
	gInput->fMouseTimeDeltaMS = (curCpuTime-gInput->lastMouseTime)*GetCPUTicksMsScale();
	gInput->lastMouseTime = curCpuTime;

	PERFINFO_AUTO_STOP_START("GetDeviceData",1);
	hr = gInput->MouseDev->lpVtbl->GetDeviceData( gInput->MouseDev, sizeof(DIDEVICEOBJECTDATA), didod, &dwElements, 0 );
	PERFINFO_AUTO_STOP_START("Handle events", 1);

    // Study each of the buffer elements and process them.
    for( i = 0; i < dwElements && i < MOUSE_INPUT_SIZE; i++ ) 
    {
		int keyIndex = 0;

		switch( didod[ i ].dwOfs )
        {
			// register mouse downs or mouse ups with their coordinates
            case DIMOFS_BUTTON0: // left mouse button pressed or released
			case DIMOFS_BUTTON1: // right mouse button
				if (!!(didod[ i ].dwOfs == DIMOFS_BUTTON0) ^ !!input_state.reverseMouseButtons)
					inpMouseClick(&(didod[i]), MS_LEFT, INP_LCLICK, INP_LDBLCLICK);
				else
					inpMouseClick(&(didod[i]), MS_RIGHT, INP_RCLICK, INP_RDBLCLICK);
				break;
			case DIMOFS_BUTTON2: // middle button
				inpMouseClick(&(didod[i]), MS_MID, INP_MCLICK, INP_MDBLCLICK);
				break;
				
			case DIMOFS_X:	
				inpMousePosDelta(didod[ i ].dwData, 0);
				break;
			case DIMOFS_Y:
				inpMousePosDelta(0, didod[ i ].dwData);
				break;

			case DIMOFS_Z:
				iMouseDeltaForThisFrame = inpMouseWheelDelta(didod[i].dwData);
				break;
        }

		keyIndex = inpKeyFromDxMouseKey(didod[ i ].dwOfs);
		if (keyIndex)
		{
			inpMouseConvertMouseEventToLogicalKeyInput(keyIndex, didod[ i ].dwData & 0x80 ? true : false, iMouseDeltaForThisFrame, didod[i].dwTimeStamp);
			updatedMouseChord = inpMouseProcessChordsAsKeyInput(keyIndex, didod[i].dwTimeStamp);
		}
	}
	PERFINFO_AUTO_STOP();

	if (i && gInput)
		gInput->dev.inputActive = true;

	if (!updatedMouseChord)
		inpMouseChordsKeepAlive(updatedMouseChord, curTime);
	inpMouseConvertMouseEventsToLogicalDragInput(curTime);

	// poll for mouse coordinates, so we have a decent starting point next time
	inpMousePos(&gInput->mouseInpCur.x, &gInput->mouseInpCur.y);

	PERFINFO_AUTO_STOP();
}

void inpMousePosDelta(int mouse_dx, int mouse_dy)
{
	int j = 0;

	gInput->mouseInpCur.x += mouse_dx;
	gInput->mouseInpCur.y += mouse_dy;
	gInput->mouse_dx += mouse_dx;
	gInput->mouse_dy += mouse_dy;
	for (j = 0; j < ARRAY_SIZE_CHECKED(gInput->buttons); j++)
	{
		gInput->buttons[j].curx += mouse_dx;
		gInput->buttons[j].cury += mouse_dy;
	}
}

int inpMouseWheelDelta(int wheel_delta)
{
	// Mouse wheel input. One click of the wheel seems to result in Z changes of +/- 120,
	// although since dwData is unsigned, it's actually a change of 120 or 4 billion.
	// Keep a running count of the z this frame in mouseInpCur.z, and also generate
	// a click event for the appropriate wheel button.
	//S32 sDwData = (S32)didod[i].dwData;
	int iMouseWheelDelta = 0;
	int iMouseDeltaForThisFrame = 0;

	iMouseWheelDelta = gInput->iMouseWheelDelta + wheel_delta;
	iMouseDeltaForThisFrame = iMouseWheelDelta;
	{
		int iNumTicks;
		// reset the counter if you reversed directions
		if (iMouseWheelDelta < 0 && wheel_delta > 0
			|| iMouseWheelDelta > 0 && wheel_delta < 0)
		{
			iMouseWheelDelta = 0;
		}

		iNumTicks = ABS(iMouseWheelDelta) / MOUSEWHEEL_THRESHOLD;

		if (iMouseWheelDelta >= MOUSEWHEEL_THRESHOLD)
		{
			iMouseWheelDelta -= iNumTicks * MOUSEWHEEL_THRESHOLD;
		}
		else if (iMouseWheelDelta <= -MOUSEWHEEL_THRESHOLD)
		{
			iMouseWheelDelta += iNumTicks * MOUSEWHEEL_THRESHOLD;
		}

		iMouseDeltaForThisFrame -= iMouseWheelDelta;
	}

	gInput->iMouseWheelDelta = iMouseWheelDelta;
	gInput->mouseInpCur.z += iMouseDeltaForThisFrame / 120; //This number controls the granularity of the mouse wheel, needs to be fixed
	gInput->mouseInpBuf[gInput->mouseBufSize].states[iMouseDeltaForThisFrame > 0 ? MS_WHEELUP : MS_WHEELDOWN] = MS_CLICK;
	gInput->mouseInpBuf[gInput->mouseBufSize].x = gInput->mouseInpCur.x;
	gInput->mouseInpBuf[gInput->mouseBufSize].y = gInput->mouseInpCur.y;
	gInput->mouseBufSize++;

	return iMouseDeltaForThisFrame;
}

void inpMouseConvertMouseEventToLogicalKeyInput(int keyIndex, bool bPressed, int iMouseDeltaForThisFrame, int timeStamp)
{
	int keyState = 0;
	// If we don't know how to process this key, do nothing.
	if (keyIndex == INP_MOUSEWHEEL)
	{
		if (iMouseDeltaForThisFrame > 0)
		{
			keyState = 1;
		}
		else if (iMouseDeltaForThisFrame < 0)
		{
			keyState = -1;
		}
		else
			keyState = 0;

		if (keyState > 0)
			inpUpdateKey(INP_MOUSEWHEEL_FORWARD, 1, timeStamp);
		else if (keyState < 0)
			inpUpdateKey(INP_MOUSEWHEEL_BACKWARD, 1, timeStamp);
	}
	else
		keyState = bPressed ? 1 : 0;

	// Limit mouse button down events to stuff within the screen.
	if( !((gInput->mouseInpCur.x < 0 || gInput->mouseInpCur.x > gInput->screenWidth ||
			gInput->mouseInpCur.y < 0 || gInput->mouseInpCur.y > gInput->screenHeight) && keyState)) {
		inpUpdateKey(keyIndex, keyState, timeStamp);
	}
}

bool inpMouseProcessChordsAsKeyInput(int keyInput, int timeStamp)
{
	bool bUpdateChord = false;
	// whenever we get edges on these, update the chord state
 	if (keyInput == INP_LBUTTON || keyInput == INP_RBUTTON) 
	{
 		bUpdateChord = false;
		if (gInput->mouseInpCur.states[MS_LEFT] == MS_DOWN && gInput->mouseInpCur.states[MS_RIGHT] == MS_DOWN /*&& !mouseDragging()*/ )
			inpUpdateKey(INP_MOUSE_CHORD, 1, timeStamp);
		else
			inpUpdateKeyClearIfActive(INP_MOUSE_CHORD, timeStamp);
	}
	return bUpdateChord;
}

void inpMouseChordsKeepAlive(int updatedMouseChord, S32 curTime)
{
	// check for chord buttons being down - have to keep faking key signals if held
	if (!updatedMouseChord)
	{
		if (gInput->mouseInpCur.states[MS_LEFT] == MS_DOWN && gInput->mouseInpCur.states[MS_RIGHT] == MS_DOWN /*&& !mouseDragging()*/) 
			inpUpdateKey(INP_MOUSE_CHORD, 1, curTime);
		else
			inpUpdateKeyClearIfActive(INP_MOUSE_CHORD, curTime);
	}
}

void inpMouseConvertMouseEventsToLogicalDragInput(S32 curTime)
{
	int i = 0;

	// Check for dragging. Dragging occurs either when the mouse moves
	// more than gClickSize while being held down, or is held down for
	// longer than gClickTime.
	for (i = 0; i < ARRAY_SIZE_CHECKED(gInput->buttons); i++)
	{
		if (((ABS(gInput->buttons[i].downx - gInput->buttons[i].curx) > gClickSize) ||
			 (ABS(gInput->buttons[i].downy - gInput->buttons[i].cury) > gClickSize) ||
			 (ABS((int)gInput->buttons[i].mbtime - curTime) > (int)gClickTime)) &&
			 (gInput->buttons[i].mbtime < (DWORD)curTime) && // Don't drag if mbtime is big
			!gInput->buttons[i].drag && gInput->buttons[i].state == MS_DOWN)
		{
			gInput->mouseInpBuf[gInput->mouseBufSize].states[i] = MS_DRAG;
			gInput->mouseInpBuf[gInput->mouseBufSize].x = gInput->buttons[i].downx;
			gInput->mouseInpBuf[gInput->mouseBufSize].y = gInput->buttons[i].downy;
			gInput->mouseBufSize++;
			gInput->buttons[i].drag = true;
		}

		// WARNING: This assumes that LDRAG, MDRAG, and RDRAG are contiguous.
		if (gInput->buttons[i].drag)
			inpUpdateKey(INP_LDRAG + i, 1, curTime);
		else
			inpUpdateKeyClearIfActive(INP_LDRAG + i, curTime);
	}
}

// use to prevent joysticks from executing commands if sitting in idle state
void inpUpdateKeyClearIfActive(int keyIndex, int timeStamp)
{
	if (!gInput)
		return;
	if (baIsSetInline(gInput->inp_levels, keyIndex))
		inpUpdateKey(keyIndex, 0, timeStamp);
}

static int just_lost_focus_debug=0;

static void inpQueueKeyBind(InputDevice *pDev, S32 iKey, bool bState, U32 uiTime)
{
	S32 iQueue = pDev->keyBindQueueLength;
	// Assert disabled for now - this never "really" triggered, but people seem to constantly
	// click their mouse when the game is loading/saving something, which generates events
	// really fast (down/up/click/down/doubleclick/up for two clicks).
	if (iQueue < ARRAY_SIZE_CHECKED(pDev->keyBindQueue)) // devassertmsg(iQueue < ARRAY_SIZE_CHECKED(pDev->keyBindQueue), "The keybind queue is full, make it bigger"))
	{
		globMovementLog("[input] Key[%u] = %u, %ums, real %ums", iKey, bState, uiTime, inpGetTime());

#if defined(INPUT_DEBUG)
		printf("  Queuing key bind... iKey = %d, bState = %d\n", iKey, bState);
#endif

		pDev->keyBindQueue[iQueue].iKey = iKey;
		pDev->keyBindQueue[iQueue].bState = bState;
		pDev->keyBindQueue[iQueue].uiTime = uiTime;
		pDev->keyBindQueueLength++;
	}
}

// Update the state of a single key.
void inpUpdateKey(int keyIndex, int keyState, int timeStamp)
{
	if (!gInput)
		return;
	// If the key is being pressed...
	if (keyState)
	{
		inpQueueKeyBind(&gInput->dev, keyIndex, keyState, timeStamp);

		// Is this the first time the key is being pressed?
		// If so, mark it as a keyboard edge.
		if (!baIsSetInline(gInput->inp_levels, keyIndex))
		{
			baSetBit(gInput->inp_edges, keyIndex);
            baSetBit(gInput->inp_levels, keyIndex);
		}
		// We missed the up event because of our debugger hack
		else if (just_lost_focus_debug)
			baSetBit(gInput->inp_edges, keyIndex);

		if (s_bPrintKeys && g_inpPrintf)
		{
			g_inpPrintf("Key Press: 0x%02X (%s)", keyIndex, keybind_GetKeyName(keyIndex, KeyboardLocale_Current));
		}
	}
	else
	{
		if (baIsSetInline(gInput->inp_levels, keyIndex))
        {
			inpQueueKeyBind(&gInput->dev, keyIndex, keyState, timeStamp);
		    baClearBit(gInput->inp_levels, keyIndex);
        }

		if (s_bPrintKeys && g_inpPrintf)
		{
			g_inpPrintf("Key Release: 0x%02X (%s)", keyIndex, keybind_GetKeyName(keyIndex, KeyboardLocale_Current));
		}


		// Do not zero out the input edge for the key because it is 
		// possible that the key was pressed and released since the
		// keyboard was checked last.
		//
		// In this case, we want to retain the edge signal so that
		// we know the key was at least pressed.
		//
		// Note that this means we must clean up the edge signals elsewhere,
		// before the keyboard data is processed.
	}
}

static void inpProcessMessages(void)
{
	PERFINFO_AUTO_START_FUNC();
    if(gInput->dev.render)
	    rdrProcessMessages(gInput->dev.render);
	PERFINFO_AUTO_STOP();
}

// This is for the joystick notification window, for example, and anything else that uses
// the thread message queue or hidden windows in the main thread.
static void inpProcessMainThreadMessages()
{
	MSG msg = {0};
	BOOL bPeekResult = PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
	if (bPeekResult)
	{
		BOOL nGetMessageResult = GetMessage(&msg, NULL, 0, 0);
		if (nGetMessageResult == (UINT)-1)
		{
			DWORD nGLE = GetLastError();
			memlog_printf(NULL, "Main thread GetMessage failed, GLE() = %u", nGLE);
		}
		else
		if (nGetMessageResult)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

void inpUpdateInternal(void)
{
	int		i,j=0;
	POINT	point = {0};
	F32		key_rpt_times[2] = { 0.25,0.05 };
	bool	bJoystick;
	bool	prevMouseLock;

	if (!gInput)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (inpIsDeviceRenderThreaded(gInput))
		inpProcessMainThreadMessages();

	// Special key cleanup.
	// Always reset the mousewheel state, otherwise, continuous mousewheel inputs
	// will not be detected.
 	baClearBit(gInput->inp_levels, INP_MOUSEWHEEL);
	baClearBit(gInput->inp_edges, INP_MOUSEWHEEL);
	baClearBit(gInput->inp_captured, INP_MOUSEWHEEL);
	gInput->mouse_dx = 0;
	gInput->mouse_dy = 0;

	gInput->dev.inputActive = false; //Nothing has happened yet
	gInput->dev.inputMouseHandled = false;
	gInput->dev.inputMouseScrollHandled = false;
	gInput->inactiveWindow = gInput->inactiveApp = (GetForegroundWindow() != gInput->hwnd);

	if (inpIsInactiveApp(&gInput->dev))
	{
		if (!gInput->lost_focus)
		{
			if (gInput->MouseDev)
 				gInput->MouseDev->lpVtbl->Unacquire( gInput->MouseDev );
			mouseClear();
			gInput->lost_focus = 1;
			GetCursorPos(&point);
			inpClear();
		}

		inpProcessMessages();

		PERFINFO_AUTO_STOP();
		//return; // must still process (and clear) input, so no return
	}
	else if (gInput->lost_focus)
	{
		gInput->lost_focus = 0;
		mouseClear();
		inpClear();
	}

	if (just_lost_focus_debug)
		just_lost_focus_debug--;


	baClearAllBits(gInput->inp_edges);
	baClearAllBits(gInput->inp_captured);

	inpProcessMessages();

	inpReadMouse();
	inpKeyboardUpdate();

	inpJoystickState();
	bJoystick = gInput->JoystickGamePad.bConnected;
	
	for( i = 0; i < MAX_CONTROLLERS && !gInput->GamePadsDisabled; i++ )
	{
		inpGamepadUpdate(i, &gInput->GamePads[i], true);
		bJoystick = bJoystick && !gInput->GamePads[i].bConnected;
	}

	PERFINFO_AUTO_START("gamePadUpdateCommands", 1);
	if (bJoystick)
		gamePadUpdateCommands(&gInput->JoystickGamePad);
	else
		gamePadUpdateCommands(gInput->GamePads);
	PERFINFO_AUTO_STOP();

	if( inpEdgeThisFrame() ) {
		gInput->lastInpEdgeTime = inpGetTime();
	}

	if (mouseIsLocked() && !ClientWindowIsBeingMovedOrResized())
	{
		gInput->mouseInpCur.x = gInput->mouseInpLast.x;
		gInput->mouseInpCur.y = gInput->mouseInpLast.y;
 		if(gInput->firstFrame)
			gInput->firstFrame--;
		else
		{
			POINT	pCursor = {0};

			pCursor.x = gInput->screenWidth / 2;
			pCursor.y = gInput->screenHeight / 2;

			ClientToScreen(gInput->hwnd,&pCursor);

			SetCursorPos(pCursor.x,pCursor.y);
		}
	}
	else
 		gInput->firstFrame = 2;

	if (inpConsoleProcessInput)
	{
		PERFINFO_AUTO_START("console", 1);
		inpConsoleProcessInput();
		PERFINFO_AUTO_STOP();
	}
	prevMouseLock = mouseIsLocked();	
	gInput->mouse_lock_last_frame = gInput->mouse_lock_this_frame;
	gInput->mouse_lock_this_frame = false;
	if( prevMouseLock && !mouseIsLocked() ) {
		POINT	pCursor = {0};
		pCursor.x = gInput->mouseInpCur.x = gInput->mouseInpLast.x = gInput->mouseInpSaved.x;
		pCursor.y = gInput->mouseInpCur.y = gInput->mouseInpLast.y = gInput->mouseInpSaved.y;
		ClientToScreen(gInput->hwnd, &pCursor);
		SetCursorPos(pCursor.x, pCursor.y);
	}

	PERFINFO_AUTO_STOP();
}




void inpClearEdge(int idx)
{
	if (!gInput)
		return;
	baClearBit(gInput->inp_edges, idx);
}

static HHOOK hHook;

static LRESULT CALLBACK LowLevelKeyboardProc (INT nCode, WPARAM wParam, LPARAM lParam)
{
    // By returning a non-zero value from the hook procedure, the
    // message does not get passed to the target window
    KBDLLHOOKSTRUCT *pkbhs = (KBDLLHOOKSTRUCT *) lParam;
    BOOL bControlKeyDown = 0;

    switch (nCode)
    {
        case HC_ACTION:
        {
            // Check to see if the CTRL key is pressed
            bControlKeyDown = GetAsyncKeyState (VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);
            
            // Disable CTRL+ESC
            if (pkbhs->vkCode == VK_ESCAPE && bControlKeyDown)
                return 1;

            // Disable ALT+TAB
            if (pkbhs->vkCode == VK_TAB && pkbhs->flags & LLKHF_ALTDOWN)
                return 1;

            // Disable ALT+ESC
            if (pkbhs->vkCode == VK_ESCAPE && pkbhs->flags & LLKHF_ALTDOWN)
                return 1;

#if !PLATFORM_CONSOLE
			 // Disable the WINDOWS key 
			if (pkbhs->vkCode == VK_LWIN || pkbhs->vkCode == VK_RWIN) 
				return 1; 
#endif
           break;
        }

        default:
            break;
    }
    return CallNextHookEx (hHook, nCode, wParam, lParam);
} 
#if !_PS3
void setNT4KeyHooks(int set)
{
	if (set)
		hHook = SetWindowsHookEx(WH_KEYBOARD_LL,LowLevelKeyboardProc,gInput->hInstance,0);
	else if (hHook)
		UnhookWindowsHookEx(hHook);
}


void setWin95TaskDisable(int disable)
{
static UINT nPreviousState;

	SystemParametersInfo (SPI_SETSCREENSAVERRUNNING, disable, &nPreviousState, 0);
}
#endif

void inpDisableAltTab()
{
BOOL	m_isKeyRegistered;
int		m_nHotKeyID = 100, m_nHotKeyID2 = 101, m_nHotKeyID3 = 103;

#if !_PS3
	setNT4KeyHooks(1);
	setWin95TaskDisable(1);
#endif
	m_isKeyRegistered = RegisterHotKey(gInput->hwnd, m_nHotKeyID, MOD_ALT, VK_TAB);
	m_isKeyRegistered = RegisterHotKey(gInput->hwnd, m_nHotKeyID2, MOD_ALT, VK_ESCAPE);

	//RegisterHotKey(gInput->hwnd, m_nHotKeyID3, MOD_WIN, 'A');
}

void inpEnableAltTab()
{
#if !_PS3
	setWin95TaskDisable(0);
	setNT4KeyHooks(0);
#endif
}

typedef struct ScancodeVkPair
{
	int vkey;
	int scanCode;
	bool override;
} ScancodeVkPair;

static StashTable scancodeFromVk = NULL;
static StashTable vkFromScancode = NULL;

static void initScancodeLookups(void)
{
	int i;
	static struct ScancodeVkPair codes[] = 
	{
#ifdef _XBOX
		{VK_OEM_3, INP_TILDE, true},
		{VK_BACK, INP_BACK, true},
		{'\n', INP_RETURN, true},
		{VK_RETURN, INP_RETURN, true},
		{VK_LEFT, INP_LEFT, true},
		{VK_RIGHT, INP_RIGHT, true},
		{VK_UP, INP_UP, true},
		{VK_DOWN, INP_DOWN, true},
		{VK_HOME, INP_HOME, true},
		{VK_END, INP_END, true},
		{VK_INSERT, INP_INSERT},
		{VK_DELETE, INP_DELETE, true},
		{VK_ESCAPE, INP_ESCAPE, true},
		{VK_LCONTROL, INP_LCONTROL, true},
		{VK_RCONTROL, INP_RCONTROL, true},
		{VK_LSHIFT, INP_LSHIFT, true},
		{VK_RSHIFT, INP_RSHIFT, true},

		// Pressing either control seems to actually send
		// VK_CONTROL rather than LCONTROL or RCONTROL.
		{VK_CONTROL, INP_LCONTROL, true},
		{VK_SHIFT, INP_LSHIFT, true},

		{VK_TAB, INP_TAB, true},
		{'A', INP_A, true},
		{'B', INP_B, true},
		{'C', INP_C, true},
		{'D', INP_D, true},
		{'E', INP_E, true},
		{'F', INP_F, true},
		{'G', INP_G, true},
		{'H', INP_H, true},
		{'I', INP_I, true},
		{'J', INP_J, true},
		{'K', INP_K, true},
		{'L', INP_L, true},
		{'M', INP_M, true},
		{'N', INP_N, true},
		{'O', INP_O, true},
		{'P', INP_P, true},
		{'Q', INP_Q, true},
		{'R', INP_R, true},
		{'S', INP_S, true},
		{'T', INP_T, true},
		{'U', INP_U, true},
		{'V', INP_V, true},
		{'W', INP_W, true},
		{'X', INP_X, true},
		{'Y', INP_Y, true},
		{'Z', INP_Z, true},
#else
		// MapVirtualKeyEx maps VK_NUMLOCK to INP_PAUSE.
		// VK_PAUSE does not generate anything.
		{VK_PAUSE, INP_PAUSE, true},
		{VK_NUMLOCK, INP_NUMLOCK, true},
		{VK_BACK, INP_BACK, true},
		{VK_RETURN, INP_RETURN, true},
#endif
	};

	scancodeFromVk = stashTableCreateInt( (3*ARRAY_SIZE(codes))/2 ); //arbitrary pad. pack it pretty tight, and don't waste too much space
	vkFromScancode = stashTableCreateInt( (3*ARRAY_SIZE(codes))/2 ); //arbitrary pad. pack it pretty tight, and don't waste too much space

	for( i = 0; i < ARRAY_SIZE( codes ); ++i ) 
	{
		verify(stashIntAddPointer( scancodeFromVk, codes[i].vkey, &codes[i], false ));
		verify(stashIntAddPointer( vkFromScancode, codes[i].scanCode, &codes[i], false ));
	}
}

//----------------------------------------
//  Helper for getting the scancode from a virtual key. 
//  Lots of special cases are needed depending on key / platform.
// ADD NEW KEYS HERE AS THEY ARE NEEDED.
//----------------------------------------
int GetScancodeFromVirtualKey(WPARAM wParam, LPARAM lParam)
{
	int scancode = MapVirtualKeyEx(wParam, 0, GetKeyboardLayout(0));
	if(lParam & 1 << 24)
		scancode |= 1 << 7;

	if (scancode == INP_LSHIFT && (lParam >> 16 & 0xFF) == INP_RSHIFT)
		scancode = INP_RSHIFT;
	
	// ab: special case for korean windows. if the
	// scancode is zero try a custom mapping. the reason for this is that VK_BACK doesn't map to anything on korean win98
	//if( !scancode )
	// we have some values to override here
	{
		ScancodeVkPair *scancodeTmp = 0;
		
		if( !scancodeFromVk )
		{
			OSVERSIONINFO vi = { sizeof(vi) };
			GetVersionEx(&vi);

			initScancodeLookups();
		}
		
		if( stashIntFindPointer( scancodeFromVk, wParam, &scancodeTmp ) && (!scancode || scancodeTmp->override ))
		{
			scancode = scancodeTmp->scanCode;
		}
	}
	return scancode;
}

//----------------------------------------
//  Helper for getting the scancode from a virtual key. 
//  Lots of special cases are needed depending on key / platform.
// ADD NEW KEYS HERE AS THEY ARE NEEDED.
//----------------------------------------
int GetVirtualKeyFromScanCode(int scancode)
{
	ScancodeVkPair *vkTmp = 0;

	if( !vkFromScancode )
	{
		OSVERSIONINFO vi = { sizeof(vi) };
		GetVersionEx(&vi);

		initScancodeLookups();
	}

	if( stashIntFindPointer( vkFromScancode, scancode, &vkTmp ) && (!scancode || vkTmp->override ))
	{
		scancode = vkTmp->vkey;
	}
	return scancode;
}

void inpScrollHandled(void)
{
	if (gInput)
		gInput->dev.inputMouseScrollHandled = true;
}

int g_traceInput;
AUTO_COMMAND ACMD_NAME("Input.Trace");
void inpTraceCmd(int enabled)
{
	g_traceInput = !!enabled;
}

void inpHandledEx(const char* widgetname, const char* file, int line, const char* reason)
{
	if (gInput)
	{
		if (!gInput->dev.inputMouseHandled)
		{
			// Capture all standard mouse-related keycodes; since something handled
			// the mouse, we shouldn't run any mouse-related keybinds. Don't capture
			// non-standard buttons (left/middle/right) since users will rebind those.
			S32 i;
			for (i = INP_MOUSE_BUTTONS; i < INP_BUTTON4; i++)
				inpCapture(i);
			for (i = INP_BUTTON8 + 1; i < INP_MOUSE_BUTTONS_END; i++)
				inpCapture(i);
			gInput->dev.inputMouseHandled = true;

			if(g_traceInput)
			{
				const char* name = "Unnamed Widget ";

				if(widgetname && widgetname[0])
					name = widgetname;

				printf("InpHandled: %s (%s:%d) claimed input because: %s\n", name, file, line, reason);
			}
		}
	}
}

void inpResetMouseTime(void)
{
	if (gInput)
	{
		// By setting time to max time, no click event will follow
		gInput->buttons[0].mbtime = -1;
		gInput->buttons[1].mbtime = -1;
		gInput->buttons[2].mbtime = -1;
	}
}

bool inpCheckHandled(void)
{
	return gInput->dev.inputMouseHandled;
}

bool inpCheckScrollHandled(void)
{
	return gInput->dev.inputMouseScrollHandled;
}

bool inpHasGamepad(void)
{
#if _XBOX
	return true;
#else
	if (gInput && (gInput->GamePads[0].bConnected || gInput->JoystickGamePad.bConnected))
		return true;
	else
		return false;
#endif
}

bool inpHasKeyboard(void)
{
#if _PS3
    extern uint8_t keyboardStatus;
    return keyboardStatus;
#elif _XBOX
	return false;
#else
	return true;
#endif
}

bool inpDidAnything(void)
{
	F32 a, b;
	if (inpEdgeThisFrame() || mouseDidAnything() || gInput->textKeyboardBuffer[0].type != KIT_None)
		return true;
	gamepadGetTriggerValues(&a, &b);
	if (a || b)
		return true;
	gamepadGetLeftStick(&a, &b);
	if (a || b)
		return true;
	gamepadGetRightStick(&a, &b);
	if (a || b)
		return true;
	return false;
}
