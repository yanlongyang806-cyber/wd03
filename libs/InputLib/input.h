#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

/* This has the basic input data and functions that are needed internally for the library */
#ifndef _INPUT_H
#define _INPUT_H

#include "BitArray.h"
#include "inputData.h"
#include "inputLib.h"

#define DIRECTINPUT_VERSION 0x0800

#include "inputGamepad.h"
#include "inputJoystick.h"
#if _XBOX
	#include "x360/inputXboxStub.h"
	#include "x360/inputGamepad360.h"
#else
	#include "wininclude.h"
	#include <Dinput.h>
	#include "x360/inputGamepad360.h"
#endif

#include "inputText.h"

typedef struct uiIMEState uiIMEState;
typedef struct ManagedThread ManagedThread;

// This is just a guess
#define MOUSE_INPUT_SIZE 256

// Windows 7 allows finer granularity than 120
#define MOUSEWHEEL_THRESHOLD 120 //Must be 120 right now, needs fixing

// Defines the current state of the mouse
typedef struct mouse_input
{
	int x;
	int y;
	int z;				// mousewheel
	mouseState states[MS_MAXBUTTON];
} mouse_input;

typedef struct KeyPressEvent
{
	S32 scancode;
	bool state;
	U32 timestamp;
} KeyPressEvent;

__forceinline U32 inpGetTime()
{
	return timeGetTime();
}

// We have to queue KEYDOWN/KEYUP events rather than process them directly in
// the Win32 message loop. It can be called when the render device is locked,
// in which case keybinds that change the graphics state will cause a crash.
// The queue is processed in inpKeyboardUpdate. -- jfw
extern KeyPressEvent **g_KeyEventQueue;
extern CRITICAL_SECTION g_Mutex_KeyEventQueue;

// The low-level state of a certain mouse button
typedef struct bState
{
	mouseState state;
	int downx;
	int downy;
	int curx;	
	int cury;
	int clickx;
	int clicky;
	DWORD mbtime;
	DWORD dctime;
	int drag;
} bState;

typedef struct WinDXInpDev
{
	InputDevice dev; //the public data

	HWND					hwnd; //Windows window and program instance
	HINSTANCE				hInstance;

	LPDIRECTINPUT8			DirectInput8; 
	LPDIRECTINPUTDEVICE8	KeyboardDev;
	LPDIRECTINPUTDEVICE8	MouseDev;
	// Tracking mouse wheel movement, for carry of high-precision motion between frames
	int						iMouseWheelDelta;

	bool					JoystickEnabled;
	LPDIRECTINPUTDEVICE8	*eaJoystickDevs;
	InputJoystick			**eaJoysticks;
	InputGamepad			JoystickGamePad;

	uiIMEState *IME;

	U32	mouseDoubleClickTime;

	mouse_input mouseInpBuf[MOUSE_INPUT_SIZE];
	mouse_input mouseInpCur;
	mouse_input mouseInpLast;
	mouse_input mouseInpSaved;

	int mouseBufSize;

	bool mouse_lock_this_frame;
	bool mouse_lock_last_frame;

	int	mouse_dx, mouse_dy;
	S64 lastMouseTime;
	F32 fMouseTimeDeltaMS;

	int key_repeat, lost_focus, firstFrame;

	DWORD lastInpEdgeTime;

	
	int screenWidth; //The screen width and height. We need this for mouse input
	int screenHeight;

	int altKeyState;
	int ctrlKeyState;
	int shiftKeyState;

	int inactiveApp; // Whether or not the app is in the foreground (note: inaccurate if there are multiple threads with Win32 windows, use gfxIsInactiveApp instead)
	int inactiveWindow; // Whether or not the window this input device is attached to is in the foreground

	BitArray	inp_levels, inp_edges, inp_captured;
	KeyInput textKeyboardBuffer[64];
	
	bState buttons[3];

	KeyPressEvent **keyEventQueue;

	bool		 GamePadsDisabled;
	InputGamepad GamePads[MAX_CONTROLLERS];

} WinDXInpDev;

extern WinDXInpDev *gInput;

#define MouseInputIsAllowed() (gInput && !gInput->mouse_lock_this_frame && !gInput->mouse_lock_last_frame && !((InputDevice *)gInput)->inputMouseHandled)
#define MouseScrollInputIsAllowed() (gInput && !gInput->mouse_lock_this_frame && !gInput->mouse_lock_last_frame && !((InputDevice *)gInput)->inputMouseScrollHandled)

void inpKeyAddBuf(KeyInputType type, S32 scancode, S32 character, KeyInputAttrib attrib, S32 vkey);

const ManagedThread *inpGetDeviceRenderThread(const WinDXInpDev *pInput);
bool inpIsDeviceRenderThreaded(const WinDXInpDev *pInput);

int InputStartup(void);
void InputShutdown(void);

void inpUpdateInternal(void);

void inpUpdateKey(int keyIndex, int keyState, int timeStamp);
void inpUpdateKeyClearIfActive(int keyIndex, int timeStamp);

void inpDisableAltTab(void);
void inpEnableAltTab(void);

void inpMouseClick(DIDEVICEOBJECTDATA *didod, S32 button, S32 clickScancode, S32 doubleClickScancode);

void inpMousePosDelta(int mouse_dx, int mouse_dy);
int inpMouseWheelDelta(int wheel_delta);
void inpMouseConvertMouseEventToLogicalKeyInput(int keyIndex, bool bPressed, int iMouseDeltaForThisFrame, int timeStamp);
bool inpMouseProcessChordsAsKeyInput(int keyIndex, int timeStamp);

void inpMouseChordsKeepAlive(int updatedMouseChord, S32 curTime);
void inpMouseConvertMouseEventsToLogicalDragInput(S32 curTime);

int inpMousePos(int *xp,int *yp);
void inpLastMousePos(int *xp, int *yp);

#endif
