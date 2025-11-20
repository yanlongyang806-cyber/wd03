/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#if _XBOX

#include "XCommon.h"
#include "XUtil.h"
#include "windefinclude.h"
#include <xam.h>
#include <xonline.h>
#include "GameClientLib.h"
#include "gclEntity.h"
#include "gclBaseStates.h"
#include "GlobalStateMachine.h"
#include "gclLogin.h"
#include "inputGamepad.h"

#define SAFE_XCLOSE_HANDLE(handle)	{												\
										if (handle != INVALID_HANDLE_VALUE)			\
										{											\
											XCloseHandle(handle);					\
											handle = INVALID_HANDLE_VALUE;			\
										}											\
									}

// Friend list related stuff
static XONLINE_FRIEND *pFriendList = NULL;
static U64 iPlayerXuidForFriendList = XUSER_INDEX_NONE;
static U32 iFriendListSize = 0;
static XOVERLAPPED overlappedEnumFriendList;

// Sets the index of the current player in xutil and input libraries. Pass XUSER_INDEX_NONE for no active player. 
void xCommon_SetCurrentPlayerIndex(U32 iPlayerIndex)
{
	// Validate the index
	devassert(iPlayerIndex < XUSER_MAX_COUNT || iPlayerIndex == XUSER_INDEX_NONE);
	if (iPlayerIndex >= XUSER_MAX_COUNT && iPlayerIndex != XUSER_INDEX_NONE)
		return;

	xUtil_SetCurrentPlayerIndex(iPlayerIndex);
	gamePadSetMasterGamePad(iPlayerIndex == XUSER_INDEX_NONE ? -1 : iPlayerIndex);
}


// Forces the active player to log out of the game
static void xCommon_ForceLogOut(void)
{
	Entity *pEntity = NULL;

	// Do not log the user out in the following cases:
	// demo playback, quick login set, disable xbox title menu is enabled
	if (GSM_IsStateActiveOrPending(GCL_DEMO_LOADING) || 
		GSM_IsStateActiveOrPending(GCL_DEMO_PLAYBACK) ||
		g_iQuickLogin ||
		gbSkipAccountLogin ||
		gGCLState.bDisableXBoxTitleMenu)
	{
		return;
	}

	// Set the current player index
	xCommon_SetCurrentPlayerIndex(XUSER_INDEX_NONE);

	pEntity = entActivePlayerPtr();
	if (pEntity)
		ClientLogOut();
}

// Initialization code for XCommon
void xCommon_Init(void)
{
	// Create the events for the XOVERLAPPED structs
	ZeroMemory(&overlappedEnumFriendList, sizeof(overlappedEnumFriendList));
	overlappedEnumFriendList.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

// Per tick processing for XCommon
void xCommon_Tick(void)
{
	static bool bInitialFriendListRetrieved = false;
	static bool bEnumerateFriendsAgain = false;
	static HANDLE hFriendListEnumerator = INVALID_HANDLE_VALUE;

	U64 iPlayerXuid = xUtil_GetCurrentPlayerXuid();

	// The listener
	static HANDLE hNotificationListener = INVALID_HANDLE_VALUE;
	DWORD dwMsg = 0;
	U64 newPlayerXuid = 0;
	

	if (hNotificationListener == INVALID_HANDLE_VALUE)
	{
		// Listener is not created successfully
		// Try to create a new one
		hNotificationListener = XNotifyCreateListener(XNOTIFY_SYSTEM | XNOTIFY_FRIENDS);
	}

	if (hNotificationListener == INVALID_HANDLE_VALUE)
	{
		// Bail out for this frame
		return;
	}

	// Do we have a sign in change?
	if (XNotifyGetNext(hNotificationListener, XN_SYS_SIGNINCHANGED, &dwMsg, NULL))
	{
		// Log the user out if the user is not signed in
		if (xUtil_GetCurrentPlayerIndex() != XUSER_INDEX_NONE &&	// We have a current player
			xUtil_GetCurrentPlayerXuid() == 0)						// Current player is not signed in to Live
		{
			xCommon_ForceLogOut();
		}		
	}

	// Any friends are added or removed?
	if (XNotifyGetNext(hNotificationListener, XN_FRIENDS_FRIEND_ADDED, &dwMsg, NULL) ||
		XNotifyGetNext(hNotificationListener, XN_FRIENDS_FRIEND_REMOVED, &dwMsg, NULL))
	{
		bEnumerateFriendsAgain = true;
	}

	// See if the current user is still valid
	if (pFriendList &&										// We have a friend list
		hFriendListEnumerator == INVALID_HANDLE_VALUE &&	// Get operation is complete
		iPlayerXuidForFriendList != iPlayerXuid)			// First signed in player is not the same anymore
	{
		bEnumerateFriendsAgain = true;
	}

	// Create the friend list enumerator on demand
	if ((!bInitialFriendListRetrieved || bEnumerateFriendsAgain) && 
		iPlayerXuid != 0 &&
		hFriendListEnumerator == INVALID_HANDLE_VALUE)
	{
		DWORD dwStatus = 0;
		DWORD dwBufferSizeNeeded = 0;
		

		bEnumerateFriendsAgain = false;

		// Create an enumerator
		dwStatus = XFriendsCreateEnumerator
		(
			xUtil_GetCurrentPlayerIndex(),	// The user index of the user whose Friends list to retrieve.
			0,								// The 0-based index of the first friend in the list.
			100,							// Maximum number of friends to be returned. A maximum of 100 friends can be returned.
			&dwBufferSizeNeeded,			// Pointer to a DWORD variable that will receive the size, in bytes, of the buffer needed to contain the enumeration results.
			&hFriendListEnumerator			// Pointer to a variable that will receive the needed enumeration HANDLE.
		);

		if (ERROR_SUCCESS == dwStatus)
		{			
			// Release the old friend list if any
			if (pFriendList)
			{
				free(pFriendList);
			}
			// Allocate the buffer for the friend list
			iPlayerXuidForFriendList = iPlayerXuid;
			iFriendListSize = dwBufferSizeNeeded / sizeof(XONLINE_FRIEND);
			pFriendList = calloc(1, dwBufferSizeNeeded);
			
			// Start enumeration
			dwStatus = XEnumerate(hFriendListEnumerator, pFriendList, dwBufferSizeNeeded, NULL, &overlappedEnumFriendList);

			if (ERROR_IO_PENDING != dwStatus)
			{
				SAFE_XCLOSE_HANDLE(hFriendListEnumerator);
			}
		}
		else
		{
			SAFE_XCLOSE_HANDLE(hFriendListEnumerator);
		}
	}

	// See if we are enumerating the friend list
	if (hFriendListEnumerator != INVALID_HANDLE_VALUE)
	{
		DWORD dwStatus = 0;
		DWORD dwResult = 0;

		if (XHasOverlappedIoCompleted(&overlappedEnumFriendList))
		{
			// Check if the operation is complete
			dwStatus = XGetOverlappedResult(&overlappedEnumFriendList, &dwResult, FALSE);

			if (ERROR_SUCCESS == dwStatus)
			{
				bInitialFriendListRetrieved = true;
			}

			// Close the enumerator handle
			SAFE_XCLOSE_HANDLE(hFriendListEnumerator);
		}

	}
}

// Returns true if the user with the given xuid is a friend of the user for the first signed in profile
bool xCommon_IsFriend(U64 xuid)
{
	U32 i = 0;

	if (pFriendList == NULL)
		return false;

	// List is not valid anymore
	if (iPlayerXuidForFriendList != xUtil_GetCurrentPlayerXuid())
		return false;

	for (i = 0; i < iFriendListSize && pFriendList[i].xuid; i++)
	{
		if (pFriendList[i].xuid == xuid)
			return true;
	}

	return false;
}


#endif