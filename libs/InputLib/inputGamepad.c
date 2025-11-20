/***************************************************************************



***************************************************************************/
#include "input.h"
#include "timing.h"
#include "inputGamepad.h"
#include "timedcallback.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define JOYSTICK_DEADZONE .01f

static bool s_rumbleEnabled = true;

// When this is set to -1, all controller inputs are respected
static S32 s_iMasterGamepadIndex = -1;

bool gamepadGetEnabled(void)
{
	return gInput->GamePadsDisabled == 0;
}

void gamepadSetEnabled(bool enabled)
{
	gInput->GamePadsDisabled = enabled ? 0 : 1;
}

// Sets the game pad index to poll for events. If the index is set to -1 all gamepads are polled.
bool gamePadSetMasterGamePad(S32 gamePadIndex)
{
	if (gamePadIndex < -1 || gamePadIndex >= MAX_CONTROLLERS)
		return false;

	s_iMasterGamepadIndex = gamePadIndex;

	return true;
}

// Returns the master game pad index
S32 gamePadGetMasterGamePad(void)
{
	return s_iMasterGamepadIndex;
}

void gamepadEnableRumble(bool enable)
{
	s_rumbleEnabled = enable;
}

bool gamepadRumbleEnabled()
{
	return s_rumbleEnabled;
}


void debug_GamePad( InputGamepad* pGamePad, int x, int y )
{
	int i = 0;

	//if(!pGamePad->bConnected)
	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Controller Not Connected" );
	//else
	//{
	//	if( pGamePad->caps.Flags & XINPUT_CAPS_VOICE_SUPPORTED )
	//		prnt( x, y + 15*i++, 100, 1.f, 1.f, "Voice Supported" );
	//	else
	//		prnt( x, y + 15*i++, 100, 1.f, 1.f, "Voice Not Supported" );

	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Left Motor Speed: %i", pGamePad->caps.Vibration.wLeftMotorSpeed );
	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Right Motor Speed: %i", pGamePad->caps.Vibration.wRightMotorSpeed );

	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Left Trigger: %i (%i -- %f)", pGamePad->bPressedLeftTrigger, pGamePad->bLeftTrigger, pGamePad->fLeftTrigger );
	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Right Trigger: %i (%i -- %f)", pGamePad->bPressedLeftTrigger, pGamePad->bRightTrigger, pGamePad->fRightTrigger );

	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Left Thumbstick: X -- %f | Y -- %f ", pGamePad->fThumbLX, pGamePad->fThumbLY );
	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Right Thumbstick: X -- %f | Y -- %f ", pGamePad->fThumbRX, pGamePad->fThumbRY );

	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Left Thumbstick: X -- %i | Y -- %i ", pGamePad->sThumbLX, pGamePad->sThumbLY );
	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "Right Thumbstick: X -- %i | Y -- %i ", pGamePad->sThumbRX, pGamePad->sThumbRY );

#if !_PS3
	//	prnt( x, y + 15*i++, 100, 1.f, 1.f, "wButtons (%i)", pGamePad->wButtons );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_DPAD_UP )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "DPAD UP" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "DPAD DOWN" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "DPAD LEFT" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "DPAD RIGHT" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_START )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "START" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_BACK )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "BACK" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_LEFT_THUMB )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "LEFT THUMB" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "RIGHT THUMB" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "LEFT SHOULDER" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "RIGHT SHOULDER" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_A )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "A BUTTON" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_B )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "B BUTTON" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_X )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "X BUTTON" );
	//	if( pGamePad->wButtons & XINPUT_GAMEPAD_Y )
	//		prnt( x+20, y + 15*i++, 100, 1.f, 1.f, "Y BUTTON" );
#endif //!_PS3
	//}
}

bool gamepadGetLeftStick( float *x, float * y )
{
	// input filter?

	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if(gInput && gInput->GamePads[i].bConnected && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i) &&
			(fabs(gInput->GamePads[i].fThumbLX) > JOYSTICK_DEADZONE || fabs(gInput->GamePads[i].fThumbLY) > JOYSTICK_DEADZONE))
		{
			if (x)
				*x = gInput->GamePads[i].fThumbLX;
			if (y)
				*y = gInput->GamePads[i].fThumbLY;

			return 1;
		}
	}

	if (s_iMasterGamepadIndex == -1 && gInput && gInput->JoystickGamePad.bConnected)
	{
		if (x)
			*x = gInput->JoystickGamePad.fThumbLX;
		if (y)
			*y = gInput->JoystickGamePad.fThumbLY;

		return 1;
	}

	if (x)
		*x = 0;
	if (y)
		*y = 0;

	return 0;
}


bool gamepadGetRightStick( float * x, float * y )
{
	// input filter?

	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if(gInput && gInput->GamePads[i].bConnected && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i) &&
			(fabs(gInput->GamePads[i].fThumbRX) > JOYSTICK_DEADZONE || fabs(gInput->GamePads[i].fThumbRY) > JOYSTICK_DEADZONE))
		{
			if (x)
				*x = gInput->GamePads[i].fThumbRX;
			if (y)
				*y = gInput->GamePads[i].fThumbRY;

			return 1;
		}
	}

	if (s_iMasterGamepadIndex == -1 && gInput && gInput->JoystickGamePad.bConnected)
	{
		if (x)
			*x = gInput->JoystickGamePad.fThumbRX;
		if (y)
			*y = gInput->JoystickGamePad.fThumbRY;

		return 1;
	}

	if (x)
		*x = 0;
	if (y)
		*y = 0;

	return 0;
}

void gamepadCaptureRightStick(void)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if(gInput && gInput->GamePads[i].bConnected && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
		{
			gInput->GamePads[i].fThumbRX = 0;
			gInput->GamePads[i].fThumbRY = 0;
		}
	}
	if (s_iMasterGamepadIndex == -1 && gInput && gInput->JoystickGamePad.bConnected)
	{
		gInput->JoystickGamePad.fThumbLX = 0;
		gInput->JoystickGamePad.fThumbLY = 0;
	}
	inpCapture(INP_JOYSTICK2_UP);
	inpCapture(INP_JOYSTICK2_DOWN);
	inpCapture(INP_JOYSTICK2_LEFT);
	inpCapture(INP_JOYSTICK2_RIGHT);
}

bool gamepadGetTriggerValues( float *left, float * right )
{
	// input filter?
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if(gInput && gInput->GamePads[i].bConnected && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i) &&
			(fabs(gInput->GamePads[i].fLeftTrigger) > JOYSTICK_DEADZONE || fabs(gInput->GamePads[i].fRightTrigger) > JOYSTICK_DEADZONE))
		{
			if (left)
				*left = gInput->GamePads[i].fLeftTrigger;
			if (right)
				*right = gInput->GamePads[i].fRightTrigger;

			return 1;
		}
	}

	if (s_iMasterGamepadIndex == -1 && gInput && gInput->JoystickGamePad.bConnected)
	{
		if (left)
			*left = gInput->JoystickGamePad.fLeftTrigger;
		if (right)
			*right = gInput->JoystickGamePad.fRightTrigger;

		return 1;
	}

	if (left)
		*left = 0;
	if (right)
		*right = 0;

	return 0;
}


int GamepadButtonToInpKeyMapping[][2] = 
{
	{INP_JOY1,INP_DJOY1},
	{INP_JOY2,INP_DJOY2},
	{INP_JOY3,INP_DJOY3},
	{INP_JOY4,INP_DJOY4},
	{INP_JOY5,INP_DJOY5},
	{INP_JOY6,INP_DJOY6},
	{0, 0}, // triggers already handled in analog section
	{0, 0},
	{INP_JOY9,INP_DJOY9},
	{INP_JOY10,INP_DJOY10},
	{INP_JOY11,INP_DJOY11},
	{INP_JOY12,INP_DJOY12},
	{INP_JOY13,INP_DJOY13},
	{INP_JOY14,INP_DJOY14},
	{INP_JOY15,INP_DJOY15},
	{INP_JOY16,INP_DJOY16},
	{INP_JOY17,INP_DJOY17},
	{INP_JOY18,INP_DJOY18},
	{INP_JOY19,INP_DJOY19},
	{INP_JOY20,INP_DJOY20},
	{INP_JOY21,INP_DJOY21},
	{INP_JOY22,INP_DJOY22},
	{INP_JOY23,INP_DJOY23},
	{INP_JOY24,INP_DJOY24},
	{INP_JOY25,INP_DJOY25},
};

int xPovDefines[] = 
{
	INP_JOYPAD_UP,INP_JOYPAD_DOWN,INP_JOYPAD_LEFT,INP_JOYPAD_RIGHT,
};

// These keys will be added to the buffered input list and repeat if held.
static S32 s_RepeatCheck[] = {
	INP_LSTICK_UP, INP_LSTICK_DOWN, INP_LSTICK_LEFT, INP_LSTICK_RIGHT,
	INP_RSTICK_UP, INP_RSTICK_DOWN, INP_RSTICK_LEFT, INP_RSTICK_RIGHT,
	INP_JOYPAD_UP, INP_JOYPAD_DOWN, INP_JOYPAD_LEFT, INP_JOYPAD_RIGHT,
};

#define JOYSTICK_REPEAT_DELAY 0.4f
#define JOYSTICK_REPEAT_INTERVAL 0.1f

static U32 s_iTimer = 0;

static float gamePadGetThumbLX(InputGamepad* pGamePad)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && fabs(pGamePad[i].fThumbLX) > JOYSTICK_DEADZONE && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			return pGamePad[i].fThumbLX;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		return gInput->JoystickGamePad.fThumbLX;
	return 0.0f;
}

static float gamePadGetThumbLY(InputGamepad* pGamePad)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && fabs(pGamePad[i].fThumbLY) > JOYSTICK_DEADZONE && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			return pGamePad[i].fThumbLY;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		return gInput->JoystickGamePad.fThumbLY;
	return 0.0f;
}

static float gamePadGetThumbRX(InputGamepad* pGamePad)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && fabs(pGamePad[i].fThumbRX) > JOYSTICK_DEADZONE && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			return pGamePad[i].fThumbRX;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		return gInput->JoystickGamePad.fThumbRX;
	return 0.0f;
}

static float gamePadGetThumbRY(InputGamepad* pGamePad)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && fabs(pGamePad[i].fThumbRY) > JOYSTICK_DEADZONE && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			return pGamePad[i].fThumbRY;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		return gInput->JoystickGamePad.fThumbRY;
	return 0.0f;
}

static bool gamePadPressedLeftTrigger(InputGamepad* pGamePad)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && pGamePad[i].bPressedLeftTrigger && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			return true;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		return gInput->JoystickGamePad.bPressedLeftTrigger;
	return false;
}

static bool gamePadPressedRightTrigger(InputGamepad* pGamePad)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && pGamePad[i].bPressedRightTrigger && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			return true;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		return gInput->JoystickGamePad.bPressedRightTrigger;
	return false;
}

static WORD gamePadGetButtons(InputGamepad* pGamePad)
{
	WORD wButtons = 0;
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && (s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			wButtons |= pGamePad[i].wButtons;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		wButtons |= gInput->JoystickGamePad.wButtons;

	return wButtons;
}

static WORD gamePadGetLastButtons(InputGamepad* pGamePad)
{
	WORD wLastButtons = 0;
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && (s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			wLastButtons |= pGamePad[i].wLastButtons;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		wLastButtons |= gInput->JoystickGamePad.wLastButtons;

	return wLastButtons;
}

static WORD gamePadGetPressedButtons(InputGamepad* pGamePad)
{
	WORD wPressedButtons = 0;
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && (s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
			wPressedButtons |= pGamePad[i].wPressedButtons;
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		wPressedButtons |= gInput->JoystickGamePad.wPressedButtons;

	return wPressedButtons;
}

static U32 gamePadGetButtonTimeStamp(InputGamepad* pGamePad, S32 iButtonNum)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && pGamePad[i].buttonTimeStamps[iButtonNum] && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
		{
			return pGamePad[i].buttonTimeStamps[iButtonNum];
		}
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		return gInput->JoystickGamePad.buttonTimeStamps[iButtonNum];

	return 0;
}

static void gamePadSetButtonTimeStamp(InputGamepad* pGamePad, S32 iButtonNum, U32 iTimeStamp)
{
	U32 i;
	for (i = 0; i < MAX_CONTROLLERS; i++)
	{
		if (pGamePad[i].bConnected && 
			(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == i))
		{
			pGamePad[i].buttonTimeStamps[iButtonNum] = iTimeStamp;
		}
	}
	if (s_iMasterGamepadIndex == -1 && gInput->JoystickGamePad.bConnected)
		gInput->JoystickGamePad.buttonTimeStamps[iButtonNum] = iTimeStamp;
}


void gamePadUpdateCommands( InputGamepad* pGamePad )
{
	static F32 s_StartedRepeating = 0, s_LastRepeat = 0;
	int i, button_num = 0;
	DWORD curTime = inpGetTime();
	float joystick_sensitivity = .5f;
	bool bAnyKeyDown = false;
	F32 fTime;

	if (!s_iTimer)
	{
		s_iTimer = timerAlloc();
		timerStart(s_iTimer);
	}

	fTime = timerElapsed(s_iTimer);

	// Left Stick
	if( gamePadGetThumbLX(pGamePad) < -joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK1_LEFT, 1, curTime);
	else 
		inpUpdateKeyClearIfActive(INP_JOYSTICK1_LEFT, curTime);

	if( gamePadGetThumbLX(pGamePad) > joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK1_RIGHT, 1, curTime);
	else 
		inpUpdateKeyClearIfActive(INP_JOYSTICK1_RIGHT, curTime);

	if( gamePadGetThumbLY(pGamePad) > joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK1_UP, 1, curTime);
	else
		inpUpdateKeyClearIfActive(INP_JOYSTICK1_UP, curTime);

	if( gamePadGetThumbLY(pGamePad) < -joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK1_DOWN, 1, curTime);
	else
		inpUpdateKeyClearIfActive(INP_JOYSTICK1_DOWN, curTime);

	// Right Stick
	if( gamePadGetThumbRX(pGamePad) < -joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK2_LEFT, 1, curTime);
	else 
		inpUpdateKeyClearIfActive(INP_JOYSTICK2_LEFT, curTime);

	if( gamePadGetThumbRX(pGamePad) > joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK2_RIGHT, 1, curTime);
	else 
		inpUpdateKeyClearIfActive(INP_JOYSTICK2_RIGHT, curTime);

	if( gamePadGetThumbRY(pGamePad) > joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK2_UP, 1, curTime);
	else
		inpUpdateKeyClearIfActive(INP_JOYSTICK2_UP, curTime);

	if( gamePadGetThumbRY(pGamePad) < -joystick_sensitivity )
		inpUpdateKey(INP_JOYSTICK2_DOWN, 1, curTime);
	else
		inpUpdateKeyClearIfActive(INP_JOYSTICK2_DOWN, curTime);

	// Triggers
	if( gamePadPressedLeftTrigger(pGamePad) )
	{
		inpUpdateKey(INP_LTRIGGER, 1, curTime);
		if (inpEdgePeek(INP_LTRIGGER))
			inpKeyAddBuf(KIT_EditKey, INP_LTRIGGER, 0, 0, VK_PAD_LTRIGGER);
	}
	else
		inpUpdateKeyClearIfActive(INP_LTRIGGER, curTime);

	if( gamePadPressedRightTrigger(pGamePad) )
	{
		inpUpdateKey(INP_RTRIGGER, 1, curTime);
		if (inpEdgePeek(INP_RTRIGGER))
			inpKeyAddBuf(KIT_EditKey, INP_RTRIGGER, 0, 0, VK_PAD_RTRIGGER);
	}
	else
		inpUpdateKeyClearIfActive(INP_RTRIGGER, curTime);

	// DPad 
	if( gamePadGetButtons(pGamePad) & XINPUT_GAMEPAD_DPAD_UP  )
		inpUpdateKey(xPovDefines[DXDIR_UP], 1, curTime);
	else 
		inpUpdateKeyClearIfActive(xPovDefines[DXDIR_UP], curTime);

	if( gamePadGetButtons(pGamePad) & XINPUT_GAMEPAD_DPAD_RIGHT )
		inpUpdateKey(xPovDefines[DXDIR_RIGHT], 1, curTime);
	else 
		inpUpdateKeyClearIfActive(xPovDefines[DXDIR_RIGHT], curTime);

	if( gamePadGetButtons(pGamePad) & XINPUT_GAMEPAD_DPAD_DOWN )
		inpUpdateKey(xPovDefines[DXDIR_DOWN], 1, curTime);
	else 
		inpUpdateKeyClearIfActive(xPovDefines[DXDIR_DOWN], curTime);

	if( gamePadGetButtons(pGamePad) & XINPUT_GAMEPAD_DPAD_LEFT )
		inpUpdateKey(xPovDefines[DXDIR_LEFT], 1, curTime);
	else
		inpUpdateKeyClearIfActive(xPovDefines[DXDIR_LEFT], curTime);

	// Buttons
 	for( i = XINPUT_GAMEPAD_START; i <= XINPUT_GAMEPAD_Y && button_num < 25; i = (i << 1))
	{
		if(GamepadButtonToInpKeyMapping[button_num][0])
		{
			if( gamePadGetButtons(pGamePad) & i )
			{
				if( !(gamePadGetLastButtons(pGamePad) & i) &&  (curTime - gamePadGetButtonTimeStamp(pGamePad, button_num)) < (U32)GetDoubleClickTime() )
				{
		 			inpUpdateKeyClearIfActive(GamepadButtonToInpKeyMapping[button_num][0], curTime);
					inpUpdateKey(GamepadButtonToInpKeyMapping[button_num][1], 1, curTime);
					gamePadSetButtonTimeStamp(pGamePad, button_num, 0);
				}
				else
				{
					inpUpdateKey(GamepadButtonToInpKeyMapping[button_num][0], 1, curTime);
					gamePadSetButtonTimeStamp(pGamePad, button_num, curTime);
					if (gamePadGetPressedButtons(pGamePad) & i)
						inpKeyAddBuf(KIT_EditKey, GamepadButtonToInpKeyMapping[button_num][0], 0, 0, 0);
				}
				gInput->dev.inputActive = true;
			}
			else
			{
				inpUpdateKeyClearIfActive(GamepadButtonToInpKeyMapping[button_num][0], curTime);
				inpUpdateKeyClearIfActive(GamepadButtonToInpKeyMapping[button_num][1], curTime);
			}
		}

		button_num++;
	}

	for (i = 0; i < ARRAY_SIZE_CHECKED(s_RepeatCheck); i++)
	{
		if (inpLevelPeek(s_RepeatCheck[i]))
		{
			if ((fTime > s_StartedRepeating + JOYSTICK_REPEAT_DELAY && fTime > s_LastRepeat + JOYSTICK_REPEAT_INTERVAL))
			{
				s_LastRepeat = fTime;
				if (s_StartedRepeating == 0)
					s_StartedRepeating = fTime;
				inpKeyAddBuf(KIT_EditKey, s_RepeatCheck[i], 0, 0, 0);
			}
			bAnyKeyDown = true;
		}
	}

	if (!bAnyKeyDown)
	{
		s_LastRepeat = 0;
		s_StartedRepeating = 0;
	}
}

bool gamepadGetButtonValue( int button )
{
	int i, val = XINPUT_GAMEPAD_START;

	for( i= ARRAY_SIZE(GamepadButtonToInpKeyMapping)-1; i >= 0; i-- )
	{
		if( GamepadButtonToInpKeyMapping[i][0] == button )
		{
			U32 j;
			for (j = 0; j < MAX_CONTROLLERS; j++)
			{
				if(gInput->GamePads[j].bConnected && gInput->GamePads[j].wButtons & (val << i) && 
					(s_iMasterGamepadIndex == -1 || s_iMasterGamepadIndex == j))
					return true;
			}
		}
	}
	return 0;
}
