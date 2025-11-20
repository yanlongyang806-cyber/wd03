#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#if _PS3
#elif _XBOX
	#include "windefinclude.h"
#else
	#include "windefinclude.h"
	#include "..\..\3rdParty\DirectX\Include\Xinput.h"
#endif

bool gamepadGetEnabled(void);
void gamepadSetEnabled(bool enabled);

bool gamepadGetLeftStick( float *x, float * y );
bool gamepadGetRightStick( float * x, float * y );

void gamepadCaptureRightStick(void);

bool gamepadGetTriggerValues( float *left, float * right );
bool gamepadGetButtonValue( int button );

// Returns the master game pad index
S32 gamePadGetMasterGamePad(void);

// Sets the game pad index to poll for events. If the index is set to -1 all gamepads are polled.
bool gamePadSetMasterGamePad(S32 gamePadIndex);


// Enable/disable vibration for the xbox controller
void gamepadEnableRumble(bool enable);
// Returns true if vibration is enabled
bool gamepadRumbleEnabled(void);

// Vibrate the controller at the given speeds.  The controller will not stop vibrating until gamepadStopRumble is called.
// lspeed and rspeed are the speeds of the left and right motors.
bool gamepadSetRumble(U16 lspeed, U16 rspeed);
// Stop all vibrations on the controller.
void gamepadStopRumble(void);
// Vibrate the controller the given speeds for an amount of time in milliseconds.
// lspeed and rspeed are the speeds of the left and right motors.
bool gamepadRumble(U16 lspeed, U16 rspeed, int time);


// A callback with this signature is called when the user dismisses the
// Xbox virtual keyboard. pchResult is a UTF-8 string containing the text
// that was entered with the user dismissed the keyboard. bAccepted is true
// if the user dismissed it by pressing Start/Enter/etc., or false if it was
// dismissed by pressing Guide/Esc/etc. pchResult will not be valid after
// the callback returns.
typedef void (*InputKeyboardCallback)(const char *pchResult, bool bAccepted, UserData pData);

// Show the virtual keyboard on the Xbox. pchTitle is like a window title,
// displayed at the top. pchPrompt is displayed directly above the entry.
// pchDefault is the default text. iMaxLength is the maximum length the
// user can enter. The callback is called when the keyboard is dismissed.
// This will assert if the virtual keyboard is already up. As far as I can
// tell there's no function to hide the virtual keyboard. On Win32, this does
// nothing.
void inpShowVirtualKeyboard(SA_PARAM_OP_STR const char *pchType,
								SA_PARAM_OP_STR const char *pchTitle,
								SA_PARAM_OP_STR const char *pchPrompt,
								SA_PARAM_NN_VALID InputKeyboardCallback callback, UserData pData,
								SA_PARAM_OP_STR const char *pchDefault,
								S32 iMaxLength);

// XInput handles up to 4 controllers,
// but atm we really only care about one
#if _XBOX
#define MAX_CONTROLLERS XUSER_MAX_COUNT
#else
#define MAX_CONTROLLERS 1
#endif

typedef struct InputGamepad
{
	// From XINPUT_GAMEPAD
	WORD    wButtons;
	BYTE    bLeftTrigger;
	BYTE    bRightTrigger;

	SHORT   sThumbLX;
	SHORT   sThumbLY;
	SHORT   sThumbRX;
	SHORT   sThumbRY;

	bool    bConnected; // If the controller is currently connected
	bool    bInserted;  // If the controller was inserted this frame
	bool    bRemoved;   // If the controller was removed this frame

	// Thumb stick values converted to range [-1,+1]
	float   fThumbRX;
	float   fThumbRY;
	float   fThumbLX;
	float   fThumbLY;
	float	fLeftTrigger;
	float	fRightTrigger;

	// Records which buttons were pressed this frame.
	// These are only set on the first frame that the button is pressed
	WORD    wPressedButtons;
	bool    bPressedLeftTrigger;
	bool    bPressedRightTrigger;

	// Last state of the buttons
	WORD    wLastButtons;
	bool    bLastLeftTrigger;
	bool    bLastRightTrigger;

	U32		buttonTimeStamps[25];

	S8 chUntilInsertedCheck;

	// device specific
#if !_PS3
	XINPUT_CAPABILITIES caps;
#endif

}InputGamepad;


// XInputGetState takes about 10x its normal CPU time when called and there
// is not a controller plugged in, and is taking about 2-3% CPU time on my
// PC. If a controller is not present, we should wait some time before
// checking again. -- jfw
#define INPUT_FRAMES_BETWEEN_INSERTED_CHECKS 10

// update function, called regularly, each platform has its own implementation of this function
HRESULT inpGamepadUpdate( DWORD dwPort, InputGamepad* pGamePad, bool bThumbstickDeadZone);

// internal function to parse raw gamepad data (ie. button presses) and convert to messages
void gamePadUpdateCommands( InputGamepad* pGamePad );

// Vibrate the controller at the given speeds.  The controller will not stop vibrating until inpGamepadStopRumble is called.
bool inpGamepadSetRumble(DWORD dwPort, WORD lspeed, WORD rspeed);
// Stop all vibrations on the controller.
void inpGamepadStopRumble(DWORD dwPort);
// Vibrate the controller the given speeds for an amount of time in milliseconds.
bool inpGamepadRumble( DWORD dwPort, WORD lspeed, WORD rspeed, U32 time );
