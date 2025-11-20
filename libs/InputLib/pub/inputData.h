/***************************************************************************



***************************************************************************/

/* This file contains the data and structure definitions for the input library 
   that we want to export to other programs */

GCC_SYSTEM
#pragma once

#include "inputLibEnums.h"

typedef struct RdrDevice RdrDevice;

typedef U16 InpKeyCode;

/* This is the list of scancodes and their names */

// Originally these had to fit into a 256 element array. That restriction no longer
// exists, and these could probably be renumbered to make more sense (maybe even
// using the VK_ values directly) since we're no longer using DirectX for keyboard input.

#define INP_ESCAPE          0x01
#define INP_1               0x02
#define INP_2               0x03
#define INP_3               0x04
#define INP_4               0x05
#define INP_5               0x06
#define INP_6               0x07
#define INP_7               0x08
#define INP_8               0x09
#define INP_9               0x0A
#define INP_0               0x0B
#define INP_MINUS           0x0C    /* - on main keyboard */
#define INP_EQUALS          0x0D
#define INP_BACK            0x0E    /* backspace */
#define INP_TAB             0x0F
#define INP_Q               0x10
#define INP_W               0x11
#define INP_E               0x12
#define INP_R               0x13
#define INP_T               0x14
#define INP_Y               0x15
#define INP_U               0x16
#define INP_I               0x17
#define INP_O               0x18
#define INP_P               0x19
#define INP_LBRACKET        0x1A
#define INP_RBRACKET        0x1B
#define INP_RETURN          0x1C    /* Enter on main keyboard */
#define INP_LCONTROL        0x1D
#define INP_A               0x1E
#define INP_S               0x1F
#define INP_D               0x20
#define INP_F               0x21
#define INP_G               0x22
#define INP_H               0x23
#define INP_J               0x24
#define INP_K               0x25
#define INP_L               0x26
#define INP_SEMICOLON       0x27
#define INP_APOSTROPHE      0x28
#define INP_GRAVE           0x29    /* accent grave, tilde */
#define INP_LSHIFT          0x2A
#define INP_BACKSLASH       0x2B
#define INP_Z               0x2C
#define INP_X               0x2D
#define INP_C               0x2E
#define INP_V               0x2F
#define INP_B               0x30
#define INP_N               0x31
#define INP_M               0x32
#define INP_COMMA           0x33
#define INP_PERIOD          0x34    /* . on main keyboard */
#define INP_SLASH           0x35    /* / on main keyboard */
#define INP_RSHIFT          0x36
#define INP_MULTIPLY        0x37    /* * on numeric keypad */
#define INP_LMENU           0x38    /* left Alt */
#define INP_SPACE           0x39
#define INP_CAPITAL         0x3A
#define INP_F1              0x3B
#define INP_F2              0x3C
#define INP_F3              0x3D
#define INP_F4              0x3E
#define INP_F5              0x3F
#define INP_F6              0x40
#define INP_F7              0x41
#define INP_F8              0x42
#define INP_F9              0x43
#define INP_F10             0x44
#define INP_NUMLOCK         0x45
#define INP_SCROLL          0x46    /* Scroll Lock */
#define INP_NUMPAD7         0x47
#define INP_NUMPAD8         0x48
#define INP_NUMPAD9         0x49
#define INP_SUBTRACT        0x4A    /* - on numeric keypad */
#define INP_NUMPAD4         0x4B
#define INP_NUMPAD5         0x4C
#define INP_NUMPAD6         0x4D
#define INP_ADD             0x4E    /* + on numeric keypad */
#define INP_NUMPAD1         0x4F
#define INP_NUMPAD2         0x50
#define INP_NUMPAD3         0x51
#define INP_NUMPAD0         0x52
#define INP_DECIMAL         0x53    /* . on numeric keypad */
#define INP_OEM_102         0x56    /* <> or \| on RT 102-key keyboard (Non-U.S.) */
#define INP_F11             0x57
#define INP_F12             0x58
#define INP_F13             0x64    /*                     (NEC PC98) */
#define INP_F14             0x65    /*                     (NEC PC98) */
#define INP_F15             0x66    /*                     (NEC PC98) */
#define INP_KANA            0x70    /* (Japanese keyboard)            */
#define INP_ABNT_C1         0x73    /* /? on Brazilian keyboard */
#define INP_CONVERT         0x79    /* (Japanese keyboard)            */
#define INP_NOCONVERT       0x7B    /* (Japanese keyboard)            */
#define INP_YEN             0x7D    /* (Japanese keyboard)            */
#define INP_ABNT_C2         0x7E    /* Numpad . on Brazilian keyboard */
#define INP_NUMPADEQUALS    0x8D    /* = on numeric keypad (NEC PC98) */
#define INP_PREVTRACK       0x90    /* Previous Track (INP_CIRCUMFLEX on Japanese keyboard) */
#define INP_AT              0x91    /*                     (NEC PC98) */
#define INP_COLON           0x92    /*                     (NEC PC98) */
#define INP_UNDERLINE       0x93    /*                     (NEC PC98) */
#define INP_KANJI           0x94    /* (Japanese keyboard)            */
#define INP_STOP            0x95    /*                     (NEC PC98) */
#define INP_AX              0x96    /*                     (Japan AX) */
#define INP_UNLABELED       0x97    /*                        (J3100) */
#define INP_NEXTTRACK       0x99    /* Next Track */
#define INP_NUMPADENTER     0x9C    /* Enter on numeric keypad */
#define INP_RCONTROL        0x9D
#define INP_MUTE            0xA0    /* Mute */
#define INP_CALCULATOR      0xA1    /* Calculator */
#define INP_PLAYPAUSE       0xA2    /* Play / Pause */
#define INP_MEDIASTOP       0xA4    /* Media Stop */
#define INP_VOLUMEDOWN      0xAE    /* Volume - */
#define INP_VOLUMEUP        0xB0    /* Volume + */
#define INP_WEBHOME         0xB2    /* Web home */
#define INP_NUMPADCOMMA     0xB3    /* , on numeric keypad (NEC PC98) */
#define INP_DIVIDE          0xB5    /* / on numeric keypad */
#define INP_SYSRQ           0xB7
#define INP_RMENU           0xB8    /* right Alt */
#define INP_PAUSE           0xC5    /* Pause */
#define INP_HOME            0xC7    /* Home on arrow keypad */
#define INP_UP              0xC8    /* UpArrow on arrow keypad */
#define INP_PRIOR           0xC9    /* PgUp on arrow keypad */
#define INP_LEFT            0xCB    /* LeftArrow on arrow keypad */
#define INP_RIGHT           0xCD    /* RightArrow on arrow keypad */
#define INP_END             0xCF    /* End on arrow keypad */
#define INP_DOWN            0xD0    /* DownArrow on arrow keypad */
#define INP_NEXT            0xD1    /* PgDn on arrow keypad */
#define INP_INSERT          0xD2    /* Insert on arrow keypad */
#define INP_DELETE          0xD3    /* Delete on arrow keypad */
#define INP_LWIN            0xDB    /* Left Windows key */
#define INP_RWIN            0xDC    /* Right Windows key */
#define INP_APPS            0xDD    /* AppMenu key */
#define INP_POWER           0xDE    /* System Power */
#define INP_SLEEP           0xDF    /* System Sleep */
#define INP_WAKE            0xE3    /* System Wake */
#define INP_WEBSEARCH       0xE5    /* Web Search */
#define INP_WEBFAVORITES    0xE6    /* Web Favorites */
#define INP_WEBREFRESH      0xE7    /* Web Refresh */
#define INP_WEBSTOP         0xE8    /* Web Stop */
#define INP_WEBFORWARD      0xE9    /* Web Forward */
#define INP_WEBBACK         0xEA    /* Web Back */
#define INP_MYCOMPUTER      0xEB    /* My Computer */
#define INP_MAIL            0xEC    /* Mail */
#define INP_MEDIASELECT     0xED    /* Media Select */

/*
*  Alternate names for keys, to facilitate transition from DOS.
*/
#define INP_BACKSPACE       INP_BACK            /* backspace */
#define INP_NUMPADSTAR      INP_MULTIPLY        /* * on numeric keypad */
#define INP_LALT            INP_LMENU           /* left Alt */
#define INP_CAPSLOCK        INP_CAPITAL         /* CapsLock */
#define INP_NUMPADMINUS     INP_SUBTRACT        /* - on numeric keypad */
#define INP_NUMPADPLUS      INP_ADD             /* + on numeric keypad */
#define INP_NUMPADPERIOD    INP_DECIMAL         /* . on numeric keypad */
#define INP_NUMPADSLASH     INP_DIVIDE          /* / on numeric keypad */
#define INP_RALT            INP_RMENU           /* right Alt */
#define INP_UPARROW         INP_UP              /* UpArrow on arrow keypad */
#define INP_PGUP            INP_PRIOR           /* PgUp on arrow keypad */
#define INP_LEFTARROW       INP_LEFT            /* LeftArrow on arrow keypad */
#define INP_RIGHTARROW      INP_RIGHT           /* RightArrow on arrow keypad */
#define INP_DOWNARROW       INP_DOWN            /* DownArrow on arrow keypad */
#define INP_PGDN            INP_NEXT            /* PgDn on arrow keypad */
#define INP_TILDE			INP_GRAVE			/* Because nobody knows what an "INP_GRAVE" is. */

//Joystick buttons
// Annoyingly there are a billion joysitck buttons that have no spot, so I'm just going to cram them 
// into this table whereever there are spaces.
#define INP_JOY1	0x5A
#define INP_JOY2	0x5B
#define INP_JOY3	0x5C
#define INP_JOY4	0x5D
#define INP_JOY5	0x5E
#define INP_JOY6	0x5F
#define INP_JOY7	0x60
#define INP_JOY8	0x61
#define INP_JOY9	0x62
#define INP_JOY10	0x63

#define INP_JOY11	0x7F
#define INP_JOY12	0x80
#define INP_JOY13	0x81
#define INP_JOY14	0x82
#define INP_JOY15	0x83
#define INP_JOY16	0x84
#define INP_JOY17	0x85
#define INP_JOY18	0x86
#define INP_JOY19	0x87
#define INP_JOY20	0x88
#define INP_JOY21	0x89
#define INP_JOY22	0x8A
#define INP_JOY23	0x8B
#define INP_JOY24	0x8C 
#define INP_JOY25	0x59// ok technically directX can handle 31 button joysticks, but I'm gonna say 24 is enough

// double button taps
#define INP_DJOY1	0xA3
#define INP_DJOY2	0xA9
#define INP_DJOY3	0xAA
#define INP_DJOY4	0xAB
#define INP_DJOY5	0xAC
#define INP_DJOY6	0xAD
#define INP_DJOY7	0xAF
#define INP_DJOY8	0xB1
#define INP_DJOY9	0xB4
#define INP_DJOY10	0xB6

#define INP_DJOY11	0xC1
#define INP_DJOY12	0xC2
#define INP_DJOY13	0xC3
#define INP_DJOY14	0xC4
#define INP_DJOY15	0xC6
#define INP_DJOY16	0xCA
#define INP_DJOY17	0xCC
#define INP_DJOY18	0xCE
#define INP_DJOY19	0xD4
#define INP_DJOY20	0xD5
#define INP_DJOY21	0xD6
#define INP_DJOY22	0xD7
#define INP_DJOY23	0xD8
#define INP_DJOY24	0xD9 
#define INP_DJOY25	0xE0// ok technically directX can handle 31 button joysticks, but I'm gonna say 24 is enough

// pov[0]
#define INP_JOYPAD_UP		0xB9
#define INP_JOYPAD_DOWN		0xBA
#define INP_JOYPAD_LEFT		0xBB
#define INP_JOYPAD_RIGHT	0xBC

// pov[1] - pov[3]
#define INP_POV1_UP		0x67
#define INP_POV1_DOWN	0x68
#define INP_POV1_LEFT	0x69
#define INP_POV1_RIGHT	0x71

#define INP_POV2_UP		0x72
#define INP_POV2_DOWN	0x74
#define INP_POV2_LEFT	0x75
#define INP_POV2_RIGHT	0x76

#define INP_POV3_UP		0x77
#define INP_POV3_DOWN	0x7A
#define INP_POV3_LEFT	0x7C
#define INP_POV3_RIGHT	0x8E

// XY axis
#define INP_JOYSTICK1_UP	0xBD
#define INP_JOYSTICK1_DOWN	0xBE
#define INP_JOYSTICK1_LEFT	0xBF
#define INP_JOYSTICK1_RIGHT	0xC0

// Z, Zrot Axis
#define INP_JOYSTICK2_UP	0xA5
#define INP_JOYSTICK2_DOWN	0xA6
#define INP_JOYSTICK2_LEFT	0xA7
#define INP_JOYSTICK2_RIGHT	0xA8

// Xrot, Yrot Axis
#define INP_JOYSTICK3_UP	0x8F
#define INP_JOYSTICK3_DOWN	0x98
#define INP_JOYSTICK3_LEFT	0x9A
#define INP_JOYSTICK3_RIGHT	0x9B

#define INP_KEY_LAST		0xE4

// redfines for convienence
#define INP_AB		INP_JOY9
#define INP_BB		INP_JOY10
#define INP_XB		INP_JOY11
#define INP_YB		INP_JOY12
#define INP_RB		INP_JOY6
#define INP_LB		INP_JOY5
#define INP_RSTICK	INP_JOY4
#define INP_LSTICK  INP_JOY3
#define INP_START   INP_JOY1
#define INP_SELECT  INP_JOY2

#define INP_LTRIGGER INP_JOY7
#define INP_RTRIGGER INP_JOY8

#define INP_DAB		INP_DJOY9
#define INP_DBB		INP_DJOY10
#define INP_DXB		INP_DJOY11
#define INP_DYB		INP_DJOY12
#define INP_DRB		INP_DJOY6
#define INP_DLB		INP_DJOY5
#define INP_DRSTICK	INP_DJOY4
#define INP_DLSTICK  INP_DJOY3
#define INP_DSTART   INP_DJOY1
#define INP_DSELECT  INP_DJOY2

#define INP_DLTRIGGER INP_DJOY7
#define INP_DRTRIGGER INP_DJOY8


#define INP_LSTICK_UP		INP_JOYSTICK1_UP
#define INP_LSTICK_DOWN		INP_JOYSTICK1_DOWN
#define INP_LSTICK_LEFT		INP_JOYSTICK1_LEFT
#define INP_LSTICK_RIGHT	INP_JOYSTICK1_RIGHT
#define INP_RSTICK_UP		INP_JOYSTICK2_UP
#define INP_RSTICK_DOWN		INP_JOYSTICK2_DOWN
#define INP_RSTICK_LEFT		INP_JOYSTICK2_LEFT
#define INP_RSTICK_RIGHT	INP_JOYSTICK2_RIGHT


// Mouse buttons
//	Although DirectX 8.1 returns 256 key scan codes, only 237 keys are defined.
//	This map the mouse buttons to right after INP_MEDIASELECT.
#define INP_MOUSE_BUTTONS	0xEE
#define INP_LBUTTON			(INP_MOUSE_BUTTONS + 0)
#define INP_MBUTTON			(INP_MOUSE_BUTTONS + 1)
#define INP_RBUTTON			(INP_MOUSE_BUTTONS + 2)
#define INP_BUTTON4			(INP_MOUSE_BUTTONS + 3)
#define INP_BUTTON5			(INP_MOUSE_BUTTONS + 4)
#define INP_BUTTON6			(INP_MOUSE_BUTTONS + 5)
#define INP_BUTTON7			(INP_MOUSE_BUTTONS + 6)
#define INP_BUTTON8			(INP_MOUSE_BUTTONS + 7)
#define INP_MOUSEWHEEL		(INP_MOUSE_BUTTONS + 8)

#define INP_MOUSE_CHORD		(INP_MOUSE_BUTTONS + 9)

// mouse wheel, drags, and clicks will send additional signals for fine tuning of input with binds
// Several places in input.c assume these are left, middle, right order, and contiguous.
// e.g. INP_LCLICK + 2 needs to give INP_RCLICK.
#define INP_LCLICK					(INP_MOUSE_BUTTONS + 10)
#define INP_MCLICK					(INP_MOUSE_BUTTONS + 11)
#define INP_RCLICK					(INP_MOUSE_BUTTONS + 12)
#define	INP_LDRAG					(INP_MOUSE_BUTTONS + 13)
#define INP_MDRAG					(INP_MOUSE_BUTTONS + 14)
#define INP_RDRAG					(INP_MOUSE_BUTTONS + 15)
#define INP_MOUSEWHEEL_FORWARD		(INP_MOUSE_BUTTONS + 16)
#define INP_MOUSEWHEEL_BACKWARD		(INP_MOUSE_BUTTONS + 17)
#define INP_LDBLCLICK				(INP_MOUSE_BUTTONS + 18)
#define INP_MDBLCLICK				(INP_MOUSE_BUTTONS + 19)
#define INP_RDBLCLICK				(INP_MOUSE_BUTTONS + 20)

#define INP_MOUSE_BUTTONS_END		(INP_MOUSE_BUTTONS + 21)

// Size of the 	input levels, edges, and captured bit arrays
// This must be at least the size of everything we expect to capture.
#define INPARRAY_SIZE (INP_MOUSE_BUTTONS_END + 1)

// Special aliases
// These are only meaningful for use with inpLevel() and inpEdge().
#define INP_SPECIAL_ALIASES	0x800
#define INP_CONTROL			(INP_SPECIAL_ALIASES + 1)
#define INP_SHIFT			(INP_SPECIAL_ALIASES + 2)
#define INP_ALT				(INP_SPECIAL_ALIASES + 3)

// Other special input signals, starting with 0x9F
#define INP_CLOSEWINDOW				(0x9F)

enum
{
	DXDIR_UP,
	DXDIR_DOWN,
	DXDIR_LEFT,
	DXDIR_RIGHT,
	DXDIR_NUM,
};

// The status of a mouse button
typedef enum
{
	MS_NONE,
	MS_DOWN,
	MS_UP,
	MS_CLICK,
	MS_DRAG,
	MS_DBLCLICK,
} mouseState;

// Takes in a scancode, state, timestamp, and mouse position and executes the 
// correct command
typedef bool (*KeyBindExec)(S32 keyScanCode, bool state, U32 timeStamp);

typedef struct KeyBindQueue
{
	InpKeyCode iKey;
	bool bState;
	U32 uiTime;
} KeyBindQueue;

// The public members of the input device 
typedef struct InputDevice
{
	RdrDevice *render; // The render context associated with this input device
	bool inputActive; // Did something touch the input this frame?
	bool inputMouseHandled;
	// So that scroll areas are not effected by other forms of mouse activity
	// (for example, denied the ability to scroll because a widget handled input due to
	// a mouse hover event) we have a separate flag for mouse scrolling.
	bool inputMouseScrollHandled;
	bool bUnicodeRendererWindow;
	bool bIgnoringInput;

	// key binds need to be queued rather than immediately executed. Input needs to be
	// processed before the UI runs (or there's another frame of input delay), but
	// keybinds need to happen after the UI runs, since the UI can change the keybind stack.
	S32 keyBindQueueLength;
	KeyBindQueue keyBindQueue[200];
	KeyBindExec keyBindExec; // A callback that gets called when it's time to activate a keybind

} InputDevice;

typedef struct InputState
{
	int reverseMouseButtons;
	int invertX;
	int invertY;
	int skipMousePolling;
	int invertUpDown;
	bool bEnableDirectInputKeyboard;
	bool bEnableRawInputSupport;
	bool bEnableRawInputManualCharTranslation;
	bool bEnableIME;
} InputState;

extern InputState input_state;
