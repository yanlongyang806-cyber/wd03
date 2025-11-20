/***************************************************************************



***************************************************************************/
#if !_PS3
#include "inputLib.h"
#include "input.h"
#include "timing.h"
#include "timedcallback.h"

#if _XBOX
const GUID FAR  GUID_SysKeyboard;
const GUID FAR  GUID_SysMouse;
const GUID FAR  IID_IDirectInput8;
#endif

#ifndef _XBOX

void inpShowVirtualKeyboard(const char *pchType, const char *pchTitle, const char *pchPrompt,
								InputKeyboardCallback callback, UserData pData,
								const char *pchDefault, S32 iMaxLength)
{
}


bool gamepadSetRumble(U16 lspeed, U16 rspeed)
{
	return false;
}

void gamepadStopRumble()
{
}

bool gamepadRumble(U16 lspeed, U16 rspeed, int time)
{
	return false;
}

#else

#include <Xtl.h>
#include "StringUtil.h"

typedef struct XOverlappedKeyboardWrapper
{
	InputKeyboardCallback callback;
	LPWSTR wstrResult;
	LPWSTR wstrTitle;
	LPWSTR wstrPrompt;
	LPWSTR wstrDefault;
	S32 iMaxLength;
	UserData pData;
} XOverlappedKeyboardWrapper;

static XOverlappedKeyboardWrapper *s_Pending;
static XOVERLAPPED *s_Overlapped;

void inpXboxUpdate(void)
{
	static char *pchResult = NULL;
	static S32 iResultLength = 0;
	if (s_Pending && s_Overlapped && XHasOverlappedIoCompleted(s_Overlapped))
	{
		S32 iNewLength = WideToUTF8StrConvert(s_Pending->wstrResult, NULL, 0) + 1;
		if (iNewLength > iResultLength)
		{
			pchResult = realloc(pchResult, iNewLength);
			iResultLength = iNewLength;
		}
		WideToUTF8StrConvert(s_Pending->wstrResult, pchResult, iResultLength);
		s_Pending->callback(pchResult, s_Overlapped->dwExtendedError == ERROR_SUCCESS, s_Pending->pData);
		SAFE_FREE(s_Pending->wstrResult);
		SAFE_FREE(s_Pending->wstrTitle);
		SAFE_FREE(s_Pending->wstrPrompt);
		SAFE_FREE(s_Pending->wstrDefault);
		SAFE_FREE(s_Pending);
		SAFE_FREE(s_Overlapped);
	}
}

static struct 
{
	DWORD eType;
	const char *pchName;
} s_aKeyboardTypes[] = {
	{ VKBD_DEFAULT, "Default" },
	{ VKBD_LATIN_EMAIL, "Email" },
	{ VKBD_LATIN_GAMERTAG, "Username" },
	{ VKBD_LATIN_PASSWORD, "Password" },
	{ VKBD_LATIN_PHONE, "Phone" },
	{ VKBD_LATIN_NUMERIC, "Numeric" },
};

void inpShowVirtualKeyboard(const char *pchType,
								const char *pchTitle, const char *pchPrompt,
								InputKeyboardCallback callback, UserData pData,
								const char *pchDefault, S32 iMaxLength)
{
	if (!pchType)
		pchType = "Default";
	if (!s_Pending)
	{
		LPWSTR pwTitle = NULL, pwPrompt = NULL, pwDefault = NULL;
		LPWSTR pwResult = calloc(iMaxLength + 1, sizeof(WCHAR));
		S32 status;
		S32 i;
		DWORD eType = VKBD_DEFAULT;
		DWORD dwPlayerIndex = gamePadGetMasterGamePad();
		s_Pending = calloc(1, sizeof(XOverlappedKeyboardWrapper));
		s_Overlapped = calloc(1, sizeof(XOVERLAPPED));

		for (i = 0; i < ARRAY_SIZE_CHECKED(s_aKeyboardTypes); i++)
		{
			if (!stricmp(s_aKeyboardTypes[i].pchName, pchType))
			{
				eType = s_aKeyboardTypes[i].eType;
				break;
			}
		}

		// All these functions 
		pwTitle = pchTitle ? UTF8ToWideStrConvertAndRealloc(pchTitle, pwTitle, NULL) : NULL;
		pwPrompt = pchPrompt ? UTF8ToWideStrConvertAndRealloc(pchPrompt, pwPrompt, NULL) : NULL;
		pwDefault = pchDefault ? UTF8ToWideStrConvertAndRealloc(pchDefault, pwDefault, NULL) : NULL;

		if (dwPlayerIndex == -1)
			dwPlayerIndex = 0;
		status = XShowKeyboardUI(dwPlayerIndex, eType, pwDefault, pwTitle, pwPrompt, pwResult, iMaxLength, s_Overlapped);
		devassertmsg(status == ERROR_IO_PENDING, "Error opening the virtual keyboard.");

		s_Pending->iMaxLength = iMaxLength;
		s_Pending->wstrResult = pwResult;
		s_Pending->callback = callback;
		s_Pending->wstrTitle = pwTitle;
		s_Pending->wstrPrompt = pwPrompt;
		s_Pending->wstrDefault = pwDefault;
		s_Pending->pData = pData;
	}
	else
		devassertmsg(false, "Tried to open the virtual keyboard when one was already open.");
}

bool gamepadSetRumble(U16 lspeed, U16 rspeed)
{
	if (gamepadRumbleEnabled())
		return inpGamepadSetRumble(0, lspeed, rspeed);
	return true;
}

void gamepadStopRumble()
{
	if (gamepadRumbleEnabled())
		inpGamepadStopRumble(0);
}

bool gamepadRumble(U16 lspeed, U16 rspeed, int time)
{
	if (gamepadRumbleEnabled())
		return inpGamepadRumble(0, lspeed, rspeed, time);
	return true;
}


#endif



HRESULT inpGamepadUpdate( DWORD dwPort, InputGamepad* pGamePad, bool bThumbstickDeadZone)
{
	XINPUT_STATE InputState = {0};
	DWORD dwResult = 0;
	BOOL bWasConnected;
	bool bPressed;
	SHORT sThumbLX;
	SHORT sThumbLY;
	SHORT sThumbRX;
	SHORT sThumbRY;

	if( dwPort >= MAX_CONTROLLERS || pGamePad == NULL )
		return E_FAIL;

	PERFINFO_AUTO_START_FUNC();

	if (!pGamePad->bConnected && pGamePad->chUntilInsertedCheck >= 0)
	{
		pGamePad->chUntilInsertedCheck--;
		PERFINFO_AUTO_STOP();
		return S_OK;
	}

	PERFINFO_AUTO_START("XInputGetState", 1);
		dwResult = XInputGetState( dwPort, &InputState );
	PERFINFO_AUTO_STOP();

	// Track insertion and removals
	bWasConnected = pGamePad->bConnected;
	pGamePad->bConnected = ( dwResult == ERROR_SUCCESS);
	pGamePad->bRemoved   = (  bWasConnected && !pGamePad->bConnected );
	pGamePad->bInserted  = ( !bWasConnected &&  pGamePad->bConnected );

	// Don't update rest of the state if not connected
	if (!pGamePad->bConnected)
	{
		ZeroStruct( pGamePad );
		pGamePad->chUntilInsertedCheck = INPUT_FRAMES_BETWEEN_INSERTED_CHECKS;
		PERFINFO_AUTO_STOP();
		return S_OK;
	}

	// Store the capabilities of the device
	if( pGamePad->bInserted )
	{
		ZeroStruct( pGamePad );
		pGamePad->bConnected = true;
		pGamePad->bInserted  = true;
		PERFINFO_AUTO_START("XInputGetCapabilities", 1);
			XInputGetCapabilities( dwPort, XINPUT_DEVTYPE_GAMEPAD, &pGamePad->caps );
		PERFINFO_AUTO_STOP();
	}

	// Copy gamepad to local structure (assumes that XINPUT_GAMEPAD at the front in CONTROLER_STATE)
	memcpy( pGamePad, &InputState.Gamepad, sizeof(XINPUT_GAMEPAD) );

	sThumbLX = pGamePad->sThumbLX;
	sThumbLY = pGamePad->sThumbLY;
	sThumbRX = pGamePad->sThumbRX;
	sThumbRY = pGamePad->sThumbRY;

	if (bThumbstickDeadZone)
	{
		handleDeadZone(&sThumbLX, &sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
		handleDeadZone(&sThumbRX, &sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
	}

	//// Convert [-1,+1] range
	pGamePad->fThumbLX = (sThumbLX / 32767.0f);
	pGamePad->fThumbLY = (sThumbLY / 32767.0f);
	pGamePad->fThumbRX = (sThumbRX / 32767.0f);
	pGamePad->fThumbRY = (sThumbRY / 32767.0f);

	pGamePad->fLeftTrigger = pGamePad->bLeftTrigger  / 255.0f;
	pGamePad->fRightTrigger = pGamePad->bRightTrigger / 255.0f;

	// Get the boolean buttons that have been pressed since the last call. 
	// Each button is represented by one bit.
	pGamePad->wPressedButtons = ( pGamePad->wLastButtons ^ pGamePad->wButtons ) & pGamePad->wButtons;
	pGamePad->wLastButtons    = pGamePad->wButtons;

	// Figure out if the left trigger has been pressed or released
	bPressed = (pGamePad->bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
	pGamePad->bLastLeftTrigger = pGamePad->bPressedLeftTrigger;
	pGamePad->bPressedLeftTrigger = bPressed;

	// Figure out if the right trigger has been pressed or released
	bPressed = (pGamePad->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
	pGamePad->bLastRightTrigger = pGamePad->bPressedRightTrigger;
	pGamePad->bPressedRightTrigger = bPressed;

	if (sThumbLX || sThumbLY || sThumbRX || sThumbRY || pGamePad->bLeftTrigger || pGamePad->bRightTrigger)
		gInput->dev.inputActive = true;

	PERFINFO_AUTO_STOP();

	return S_OK;
}


void handleDeadZone(SHORT* xInOut, SHORT* yInOut, SHORT deadZoneValue)
{
	F32 		x = *xInOut / 32767.0f;
	F32 		y = *yInOut / 32767.0f;
	F32 		lenSQR = SQR(x) + SQR(y);
	const F32	deadZone = deadZoneValue / 32767.0f;
	const F32	deadZoneSQR = SQR(deadZone);

	// Apply deadzone if centered
	if( lenSQR <= deadZoneSQR )
	{   
		*xInOut = 0;
		*yInOut = 0;
	}
	else
	{
		F32 len = sqrt(lenSQR);
		F32 newLen = (len - deadZone) / (1.f - deadZone);
		F32 scaleX = x * newLen / len;
		F32 scaleY = y * newLen / len;
		
		MINMAX1(scaleX, -1.f, 1.f);
		MINMAX1(scaleY, -1.f, 1.f);
		
		*xInOut = 32767.f * scaleX;
		*yInOut = 32767.f * scaleY;
	}
}

bool inpGamepadSetRumble(DWORD dwPort, WORD lspeed, WORD rspeed)
{
	XINPUT_VIBRATION vibration = {lspeed, rspeed};
	if (XInputSetState(dwPort, &vibration) != ERROR_SUCCESS)
		return false;
	return true;
}

void inpGamepadStopRumble(DWORD dwPort)
{
	XINPUT_VIBRATION vibration = {0, 0};
	while(XInputSetState(dwPort, &vibration) != ERROR_SUCCESS);
}

void inpGamepadStopRumbleCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	DWORD dwPort = (DWORD)((intptr_t)userData);
	inpGamepadStopRumble(dwPort);
}

bool inpGamepadRumble(DWORD dwPort, WORD lspeed, WORD rspeed, U32 time)
{
	XINPUT_VIBRATION vibration = {lspeed, rspeed};

	if (XInputSetState(dwPort, &vibration) != ERROR_SUCCESS)
		return false;

	TimedCallback_Run(inpGamepadStopRumbleCallback, (void*)((intptr_t)dwPort), (F32)time/1000.0f);
	return true;
}


#endif // !_PS3