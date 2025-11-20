/***************************************************************************



***************************************************************************/

#if _XBOX

#include "XUtil.h"
#include "windefinclude.h"
#include "StringUtil.h"
#include <xam.h>
#include <xonline.h>

#define MAX_GAMERTAG_LENGTH 128

static char sGamerTag[MAX_GAMERTAG_LENGTH] = "";

// Current player index
static U32 s_iCurrentPlayerIndex = XUSER_INDEX_NONE;

// Sets the index of the current player. Pass XUSER_INDEX_NONE for no active player. 
void xUtil_SetCurrentPlayerIndex(U32 iPlayerIndex)
{
	// Validate the index
	devassert(iPlayerIndex < XUSER_MAX_COUNT || iPlayerIndex == XUSER_INDEX_NONE);
	if (iPlayerIndex >= XUSER_MAX_COUNT && iPlayerIndex != XUSER_INDEX_NONE)
		return;

	s_iCurrentPlayerIndex = iPlayerIndex;
}

// Returns the index of the first signed in player. If XUSER_INDEX_NONE is returned it means that no one is signed in.
U32 xUtil_GetCurrentPlayerIndex(void)
{
	return s_iCurrentPlayerIndex;
}

// Returns the XUID for the given player index. 0 means that player is not signed in.
U64 xUtil_GetXuid(U32 iPlayerIndex)
{
	XUID xuid = 0;
	return ERROR_SUCCESS == XUserGetXUID(iPlayerIndex, &xuid) ? xuid : 0;
}

// Returns the XUID for the first signed in player. 0 means that no one is signed in.
U64 xUtil_GetCurrentPlayerXuid(void)
{
	U32 iFirstPlayerIndex = xUtil_GetCurrentPlayerIndex();
	return iFirstPlayerIndex == XUSER_INDEX_NONE ? 0 : xUtil_GetXuid(iFirstPlayerIndex);
}

// Returns true if at least one player is signed into the console
bool xUtil_HasSignedInUser(void)
{
	XUSER_SIGNIN_STATE eState;
	S32 iPlayerIndex;
	for (iPlayerIndex = 0; iPlayerIndex < XUSER_MAX_COUNT; iPlayerIndex++)
	{
		// Get the player state
		eState = XUserGetSigninState(iPlayerIndex);

		// See if the player is signed in to Live
		if (eState == eXUserSigninState_SignedInToLive)
		{
			XINPUT_STATE xinputState;

			// Get the controller state
			DWORD dwError = XInputGetState(iPlayerIndex, &xinputState);

			// Is the controller connected
			if (dwError == ERROR_SUCCESS)
				return true;
		}
	}

	return xUtil_GetCurrentPlayerIndex() != XUSER_INDEX_NONE;
}

// Fills in the buffer with the first signed in user's gamertag
const char * xUtil_GetCurrentPlayerGamerTag()
{
	U32 iCurrentPlayerIndex = xUtil_GetCurrentPlayerIndex();

	if (iCurrentPlayerIndex == XUSER_INDEX_NONE)
	{
		sGamerTag[0] = 0;
		return sGamerTag;
	}

	if (ERROR_SUCCESS != XUserGetName(iCurrentPlayerIndex, sGamerTag, MAX_GAMERTAG_LENGTH))
	{
		sGamerTag[0] = 0;
		return sGamerTag;
	}

	return sGamerTag;
}

// Opens up the XBOX sign in dialog
bool xUtil_ShowSigninUI()
{
	return ERROR_SUCCESS == XShowSigninUI(1, XSSUI_FLAGS_SHOWONLYONLINEENABLED);
}

// Validates a string for XBOX live usage
bool xUtil_IsValidXLiveString(const char *pString)
{
	WCHAR *pWideString = NULL;
	U32 iBufSizeForWideString;
	STRING_DATA stringData;
	STRING_VERIFY_RESPONSE *pStringVerifyResponse;
	DWORD dwResult;
	DWORD dwNumStrings = 1;
	DWORD cbResults;
	HRESULT hrStringResult;

	if (pString == NULL)
		return true;

	if (pString[0] == 0)
		return true;

	iBufSizeForWideString = sizeof(WCHAR) * (strlen(pString) + 1);

	// Convert the UTF8 into WideString
	pWideString = calloc(iBufSizeForWideString, 1);
	pWideString = UTF8ToWideStrConvertAndRealloc(pString, pWideString, &iBufSizeForWideString);
	stringData.pszString = pWideString;
	stringData.wStringSize = strlen(pString) + 1;

	// Allocate enough data to hold the returned results	
	cbResults = sizeof(STRING_VERIFY_RESPONSE) + (sizeof(HRESULT) * dwNumStrings);
	pStringVerifyResponse = (STRING_VERIFY_RESPONSE* )calloc(cbResults, 1);

	dwResult = XStringVerify(
		0,
		"en-us",
		dwNumStrings,
		&stringData,
		cbResults,
		pStringVerifyResponse,
		NULL);

	if (dwResult != ERROR_SUCCESS)
	{
		// clean up
		free(pWideString);
		free(pStringVerifyResponse);
		return false;
	}

	hrStringResult = pStringVerifyResponse->pStringResult[0];

	// clean up
	free(pWideString);
	free(pStringVerifyResponse);

	return hrStringResult == S_OK;
}

#endif