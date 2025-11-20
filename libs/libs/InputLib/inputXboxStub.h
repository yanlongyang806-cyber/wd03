#pragma once
GCC_SYSTEM

#ifndef _INPUTXBOXSTUB_H
#define _INPUTXBOXSTUB_H

typedef struct VTBL
{
	HRESULT (*Acquire)();
	HRESULT (*Unacquire)();
	HRESULT (*Release)();
	HRESULT (*CreateDevice)();
	HRESULT (*SetDataFormat)();
	HRESULT (*SetProperty)();
	HRESULT (*SetCooperativeLevel)();
	HRESULT (*Poll)();
	HRESULT (*EnumDevices)();
	HRESULT (*EnumObjects)();
	HRESULT (*GetDeviceData)();
	HRESULT (*GetDeviceState)();
}VTBL, *LPVTBL;

typedef struct DIRECTINPUT8
{
	LPVTBL lpVtbl;
}DIRECTINPUT8, *LPDIRECTINPUT8;

typedef struct DIRECTINPUTDEVICE8
{
	LPVTBL lpVtbl;
}DIRECTINPUTDEVICE8, *LPDIRECTINPUTDEVICE8;

typedef struct _DIDATAFORMAT {
	void * empty;
} DIDATAFORMAT, *LPDIDATAFORMAT;

typedef struct DIPROPHEADER {
	DWORD   dwSize;
	DWORD   dwHeaderSize;
	DWORD   dwObj;
	DWORD   dwHow;
} DIPROPHEADER, *LPDIPROPHEADER;

typedef struct DIPROPDWORD {
	DIPROPHEADER diph;
	DWORD   dwData;
} DIPROPDWORD, *LPDIPROPDWORD;

typedef struct DIDEVICEINSTANCE
{
	GUID    guidInstance;
	GUID    guidProduct;
}DIDEVICEINSTANCE;

typedef struct DIDEVICEOBJECTINSTANCE
{
	DWORD   dwType;
}DIDEVICEOBJECTINSTANCE;

typedef struct DIDEVICEOBJECTDATA
{
	DWORD   dwOfs;
	DWORD   dwData;
	DWORD   dwTimeStamp;
}DIDEVICEOBJECTDATA;

typedef struct DIPROPRANGE {
	DIPROPHEADER diph;
	LONG    lMin;
	LONG    lMax;
} DIPROPRANGE, *LPDIPROPRANGE;

typedef struct DIJOYSTATE2 {
	LONG    lX;                     /* x-axis position              */
	LONG    lY;                     /* y-axis position              */
	LONG    lZ;                     /* z-axis position              */
	LONG    lRx;                    /* x-axis rotation              */
	LONG    lRy;                    /* y-axis rotation              */
	LONG    lRz;                    /* z-axis rotation              */
	LONG    rglSlider[2];           /* extra axes positions         */
	DWORD   rgdwPOV[4];             /* POV directions               */
	BYTE    rgbButtons[128];        /* 128 buttons                  */
	LONG    lVX;                    /* x-axis velocity              */
	LONG    lVY;                    /* y-axis velocity              */
	LONG    lVZ;                    /* z-axis velocity              */
	LONG    lVRx;                   /* x-axis angular velocity      */
	LONG    lVRy;                   /* y-axis angular velocity      */
	LONG    lVRz;                   /* z-axis angular velocity      */
	LONG    rglVSlider[2];          /* extra axes velocities        */
	LONG    lAX;                    /* x-axis acceleration          */
	LONG    lAY;                    /* y-axis acceleration          */
	LONG    lAZ;                    /* z-axis acceleration          */
	LONG    lARx;                   /* x-axis angular acceleration  */
	LONG    lARy;                   /* y-axis angular acceleration  */
	LONG    lARz;                   /* z-axis angular acceleration  */
	LONG    rglASlider[2];          /* extra axes accelerations     */
	LONG    lFX;                    /* x-axis force                 */
	LONG    lFY;                    /* y-axis force                 */
	LONG    lFZ;                    /* z-axis force                 */
	LONG    lFRx;                   /* x-axis torque                */
	LONG    lFRy;                   /* y-axis torque                */
	LONG    lFRz;                   /* z-axis torque                */
	LONG    rglFSlider[2];          /* extra axes forces            */
} DIJOYSTATE2, *LPDIJOYSTATE2;

typedef struct KBDLLHOOKSTRUCT
{
	DWORD vkCode;
	DWORD flags;
}KBDLLHOOKSTRUCT;

typedef struct OSVERSIONINFO 
{
	DWORD dwMajorVersion;
}OSVERSIONINFO;

DIDATAFORMAT c_dfDIKeyboard;
DIDATAFORMAT c_dfDIJoystick2;
DIDATAFORMAT c_dfDIMouse2;

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
	EXTERN_C const GUID FAR name

DEFINE_GUID(GUID_SysKeyboard,	0x6F1D2B61,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_SysMouse,		0x6F1D2B60,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(IID_IDirectInput8,  0xBF798031,0x483A,0x4DA2,0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00);

#define DIPH_DEVICE         0
#define DIPROP_BUFFERSIZE	0
#define DISCL_EXCLUSIVE     0
#define DISCL_NONEXCLUSIVE  0
#define DISCL_FOREGROUND    0
#define DISCL_BACKGROUND    0
#define DISCL_NOWINKEY      0
#define DIENUM_STOP         0
#define DIENUM_CONTINUE     1
#define DIDFT_AXIS          0
#define DIPROP_RANGE		0
#define DIPH_BYID           0
#define DIDFT_ALL           0
#define DIERR_INPUTLOST     0

#define DIEDFL_ATTACHEDONLY     0
#define DI8DEVCLASS_GAMECTRL    0

#define DIJOFS_BUTTON0     0
#define DIJOFS_BUTTON1     0
#define DIJOFS_BUTTON2     0
#define DIJOFS_BUTTON3     0
#define DIJOFS_BUTTON4     0
#define DIJOFS_BUTTON5     0
#define DIJOFS_BUTTON6     0
#define DIJOFS_BUTTON7     0
#define DIJOFS_BUTTON8     0
#define DIJOFS_BUTTON9     0
#define DIJOFS_BUTTON10    0
#define DIJOFS_BUTTON11    0
#define DIJOFS_BUTTON12    0
#define DIJOFS_BUTTON13    0
#define DIJOFS_BUTTON14    0
#define DIJOFS_BUTTON15    0
#define DIJOFS_BUTTON16    0
#define DIJOFS_BUTTON17    0
#define DIJOFS_BUTTON18    0
#define DIJOFS_BUTTON19    0
#define DIJOFS_BUTTON20    0
#define DIJOFS_BUTTON21    0
#define DIJOFS_BUTTON22    0
#define DIJOFS_BUTTON23    0
#define DIJOFS_BUTTON24    0
#define DIJOFS_BUTTON25    0

#define DIMOFS_BUTTON0 0
#define DIMOFS_BUTTON1 1
#define DIMOFS_BUTTON2 2
#define DIMOFS_BUTTON3 3
#define DIMOFS_BUTTON4 4
#define DIMOFS_BUTTON5 5
#define DIMOFS_BUTTON6 6
#define DIMOFS_BUTTON7 7

#define DIMOFS_X        8
#define DIMOFS_Y        9
#define DIMOFS_Z        10
#define HC_ACTION       0
#define LLKHF_ALTDOWN   0
#define WH_KEYBOARD_LL	0
#define SPI_SETSCREENSAVERRUNNING 0
#define MOD_ALT 0

enum
{
	WM_CLOSE,
	WM_CHAR,
	WM_KEYDOWN,
	WM_KEYUP,
	WM_SYSKEYUP,
	WM_SYSKEYDOWN,
	WM_DEADCHAR,
	WM_SYSCHAR,
	WM_KEYLAST,
	WM_LBUTTONDOWN,
	WM_MBUTTONDOWN,
	WM_RBUTTONDOWN,
	WM_LBUTTONDBLCLK,
	WM_SYSDEADCHAR,
};

__forceinline static HRESULT DirectInput8Create(void * hinst, DWORD dwVersion, const void *riidltf, void *ppvOut, void * punkOuter){ return S_FALSE; }
__forceinline static void SetCursorPos( int x, int y ){ return; }
__forceinline static int GetCursorPos( POINT* p ){ return 0; }
__forceinline static void ShowCursor( int bShow ){ return; }
__forceinline static int ClientToScreen( HWND hwnd, POINT * p ){ return 0; }
__forceinline static int GetFocus(){ return 0; }
__forceinline static HKL GetKeyboardLayout(int x){return 0;}
__forceinline static int GetKeyboardState(char * state){return 0;}
__forceinline static int MapVirtualKeyEx(int x, int y, HKL h){return x;}
__forceinline static int ToAsciiEx(int uVirtKey, int uScanCode,  const BYTE *lpKeyState, short *lpChar, int uFlags, HKL dwhkl){return 0;}
__forceinline static int GetDoubleClickTime(){ return 0; }
__forceinline static HWND GetForegroundWindow() { return 0; }
__forceinline static void ClipCursor(LPRECT rect){ return; }
__forceinline static int GetAsyncKeyState( int x ){ return 0;}
__forceinline static int CallNextHookEx( HHOOK hHook, int nCode, int wParam, int lParam){ return 0; }
__forceinline static HHOOK SetWindowsHookEx(int x, void * y, HINSTANCE h, DWORD word ){ return 0; }
__forceinline static bool UnhookWindowsHookEx(HHOOK hk){ return 0; }
__forceinline static void SystemParametersInfo( int x, int y, void * p, int f ){ return; }
__forceinline static int RegisterHotKey(HWND h, int i, int f, int v ){ return 0; }
__forceinline static void GetVersionEx( OSVERSIONINFO * os){ return; }
__forceinline static bool uiIME_MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) { return 0; }

#endif