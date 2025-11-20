/***************************************************************************



***************************************************************************/

#pragma once
#if _XBOX

// Sets the index of the current player. Pass XUSER_INDEX_NONE for no active player. 
void xUtil_SetCurrentPlayerIndex(U32 iPlayerIndex);

// Returns the index of the current player. If XUSER_INDEX_NONE is returned it means that no one is signed in.
U32 xUtil_GetCurrentPlayerIndex(void);

// Returns the XUID for the given player index. 0 means that player is not signed in.
U64 xUtil_GetXuid(U32 iPlayerIndex);

// Returns the XUID for the first signed in player. 0 means that no one is signed in.
U64 xUtil_GetCurrentPlayerXuid(void);

// Returns true if at least one player is signed into the console
bool xUtil_HasSignedInUser(void);

// Returns first signed in user's gamertag. If there is no user signed in returns an empty string
const char * xUtil_GetCurrentPlayerGamerTag();

// Opens up the XBOX sign in dialog
bool xUtil_ShowSigninUI();

// Validates a string for XBOX live usage
bool xUtil_IsValidXLiveString(const char *pString);

#endif