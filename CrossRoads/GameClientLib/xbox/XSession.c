/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#if _XBOX

#include "XSession.h"
#include "windefinclude.h"
#include <xtl.h>
#include <xonline.h>
#include "XBoxStructs.h"
#include "XBoxStructs_h_ast.h"
#include "Entity.h"
#include "Player.h"
#include "Team.h"
#include "gclEntity.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

#define XSESSION_NUM_PUBLIC_SLOTS 4
#define XSESSION_NUM_PRIVATE_SLOTS 4

// The players who had requested to be joined to the session
static U32 *pJoinPendingPlayers	= NULL;

// The players who have joined to the session
static U32 *pXSessionJoinedPlayers	= NULL;

// The current gaming session info
static XSESSION_INFO currentSessionInfo;

// The gaming session handle
static HANDLE currentSessionHandle = 0;

// Session nonce
static ULONGLONG currentSessionNonce = 0;

// Indicates if the client has an active gaming session
static bool bHasActiveSession = false;

// XOVERLAPPED structs for async calls
static XOVERLAPPED createSessionOverlapped = { 0 };
static XOVERLAPPED createSessionForPeerOverlapped = { 0 };
static XOVERLAPPED deleteSessionOverlapped = { 0 };
static XOVERLAPPED joinSessionOverlapped = { 0 };

static bSessionCreateIsInProgress = false;
static bSessionCreateForPeerIsInProgress = false;
static bSessionDeleteIsInProgress = false;
static bSessionJoinIsInProgress = false;

bool xSession_HasActiveSession(void)
{
	return bHasActiveSession;
}

bool xSession_IsInSession(U32 entId)
{
	if (!bHasActiveSession)
		return false;

	if (ea32Size(&pXSessionJoinedPlayers) <= 0)
		return false;

	return ea32Find(&pXSessionJoinedPlayers, entId) != -1;
}

void xSession_Init(void)
{
	// Create the events required by xoverlapped structs
	createSessionOverlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	createSessionForPeerOverlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	deleteSessionOverlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	joinSessionOverlapped.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

// Checks if the session creation is in progress and
// when it's finalized it sends the session info to the server
void xSession_HandleCreateSessionForHost(Team *pTeam)
{
	static NOCONST(CrypticXSessionInfo) sessionInfo;
	DWORD dwResult = 0;
	BYTE invalidSessionId[8] = { 0 }; 

	if (bSessionCreateIsInProgress && 
		XHasOverlappedIoCompleted(&createSessionOverlapped))
	{		
		XGetOverlappedResult(&createSessionOverlapped, &dwResult, TRUE);

		// Did the session create request succeed?
		if(dwResult == ERROR_SUCCESS && memcmp(currentSessionInfo.sessionID.ab, invalidSessionId, sizeof(invalidSessionId)) != 0) // Successful
		{			
			xBoxStructConvertToCrypticXSessionInfo(&currentSessionInfo, &sessionInfo, currentSessionNonce);
			
			// Let the server know about the session info
			ServerCmd_Team_SetXSessionInfo((const CrypticXSessionInfo *)&sessionInfo);

			bHasActiveSession = true;
		}

		// Session create request is complete
		bSessionCreateIsInProgress = false;
	}
}

void xSession_HandleCreateSessionForPeer(Team *pTeam)
{
	DWORD dwResult;

	// We need to have a valid team and session info
	if (pTeam == NULL || pTeam->pXSessionInfo == NULL || pTeam->pXSessionInfo->sessionID == NULL)
	{
		return;
	}

	if (bSessionCreateForPeerIsInProgress)
	{
		if (XHasOverlappedIoCompleted(&createSessionForPeerOverlapped))
		{		
			XGetOverlappedResult(&createSessionForPeerOverlapped, &dwResult, TRUE);

			// Did the session create request succeed?
			if(dwResult == ERROR_SUCCESS && pTeam != NULL) // Successful and we're still in a team
			{
				bHasActiveSession = true;
			}

			// Session create request is complete
			bSessionCreateForPeerIsInProgress = false;
		}
	}
	else
	{		
		// Did the session change?
		if (!bHasActiveSession || 
			memcmp(currentSessionInfo.sessionID.ab, pTeam->pXSessionInfo->sessionID->xnkid, sizeof(currentSessionInfo.sessionID.ab)) != 0)
		{
			// Kill the existing session
			xSession_KillExistingSession();

			// Copy the session info from the server
			xBoxStructConvertToXSESSION_INFO(pTeam->pXSessionInfo, &currentSessionInfo);
			currentSessionNonce = pTeam->pXSessionInfo->sessionNonce;

			// Create a new session
			dwResult = XSessionCreate(XSESSION_CREATE_USES_PEER_NETWORK | XSESSION_CREATE_USES_MATCHMAKING, 
				0, XSESSION_NUM_PUBLIC_SLOTS, XSESSION_NUM_PRIVATE_SLOTS, &currentSessionNonce, &currentSessionInfo, &createSessionForPeerOverlapped, &currentSessionHandle);

			if (dwResult == ERROR_IO_PENDING)
			{
				// Session create request is initiated successfully
				bSessionCreateForPeerIsInProgress = true;
			}
		}
	}
}

// Handles session joins
void xSession_HandleJoinSession(Entity *pCurrentPlayer, Team *pTeam)
{
	DWORD dwlocalUserIndex = 0;
	BOOL bOccupiesPrivateSlot = FALSE;
	DWORD dwResult = 0;
	S32 i = 0;
	S32 cAllMembers = 0;
	S32 cMembersToJoin = 0;
	bool bAddLocalPlayer = false;
	static XUID arrPlayersToJoin[TEAM_MAX_SIZE];
	static BOOL arrPlayersToJoinPrivateSlotStatus[TEAM_MAX_SIZE] = { TRUE };
	Entity *pEnt = NULL;

	// We need to have a valid team and session info
	if (pTeam == NULL || pTeam->pXSessionInfo == NULL || !bHasActiveSession)
	{
		return;
	}

	if (bSessionJoinIsInProgress)
	{
		if (XHasOverlappedIoCompleted(&joinSessionOverlapped))
		{		
			XGetOverlappedResult(&joinSessionOverlapped, &dwResult, TRUE);

			// Did the session join request succeed?
			if(dwResult == ERROR_SUCCESS) // Successful
			{
				if (ea32Size(&pJoinPendingPlayers) <= 0) // Local join successful
				{
					// Add the current player to the joined players list
					ea32Push(&pXSessionJoinedPlayers, entGetContainerID(pCurrentPlayer));
				}
				else // Remote join successful
				{
					// Transfer all pending players to the joined players array
					for (i = ea32Size(&pJoinPendingPlayers) - 1; i >= 0; i--)
					{
						ea32Push(&pXSessionJoinedPlayers, pJoinPendingPlayers[i]);
						ea32Remove(&pJoinPendingPlayers, i);
					}
				}
			}

			// Session join request is complete
			bSessionJoinIsInProgress = false;
		}
	}
	else
	{
		// We need to make sure everyone joins the session including ourselves
		cAllMembers = eaSize(&pTeam->eaMembers);

		if (cAllMembers > 0)
		{
			// Clean up the list
			ea32Destroy(&pJoinPendingPlayers);
			cMembersToJoin = 0;

			// Iterate thru all members
			for (i = 0; i < cAllMembers; i++)
			{
				pEnt = GET_REF(pTeam->eaMembers[i]->hEnt);
				if (pEnt != NULL && 
					pEnt->pPlayer->pXBoxSpecificData != NULL &&
					pEnt->pPlayer->pXBoxSpecificData->xuid > 0 &&
					ea32Find(&pXSessionJoinedPlayers, entGetContainerID(pEnt)) == -1)
				{
					// Is this player the current player?
					if (entGetContainerID(pCurrentPlayer) == entGetContainerID(pEnt))
					{
						// Delete anything we added so far, those players will be processed later on
						ea32Destroy(&pJoinPendingPlayers);
						bAddLocalPlayer = true;
						break;
					}
					else
					{
						// This player has not joined, add to the pending player list
						ea32Push(&pJoinPendingPlayers, entGetContainerID(pEnt));
						// Set the array element also for API call
						arrPlayersToJoin[cMembersToJoin++] = pEnt->pPlayer->pXBoxSpecificData->xuid;
					}
				}
			}

			// Will there be any join operations
			if (bAddLocalPlayer || cMembersToJoin > 0)
			{
				if (bAddLocalPlayer)
				{
					// Join the session
					dwResult = XSessionJoinLocal(
						currentSessionHandle,
						1,
						&dwlocalUserIndex,
						&bOccupiesPrivateSlot,
						&joinSessionOverlapped);

				}
				else if (cMembersToJoin > 0)
				{
					// Join remote players
					dwResult = XSessionJoinRemote(
						currentSessionHandle,
						cMembersToJoin,
						arrPlayersToJoin,
						arrPlayersToJoinPrivateSlotStatus,
						&joinSessionOverlapped);
				}

				if (dwResult == ERROR_IO_PENDING)
				{
					bSessionJoinIsInProgress = true;
				}
			}
		}
	}
}

void xSession_Tick(void)
{
	XUID playerXUID;

	// Team
	Team *pTeam = NULL;

	// Get the current player
	Entity *pEnt = entActivePlayerPtr();

	// Continue only if the user is signed in
	if (XUserGetXUID(0, &playerXUID) != ERROR_SUCCESS)
	{
		return;
	}

	// Get the current team
	pTeam = team_GetTeam(pEnt);

	// Handle session creation for host
	xSession_HandleCreateSessionForHost(pTeam);

	// We are no longer in a team
	if (pTeam == NULL)
	{
		// Kill the existing session
		xSession_KillExistingSession();
		return;
	}

	// Handle session creation for peers
	xSession_HandleCreateSessionForPeer(pTeam);

	// Handle session joins
	xSession_HandleJoinSession(pEnt, pTeam);
}

// Kills the existing gaming session
void xSession_KillExistingSession(void)
{
	if (bHasActiveSession)
	{
		bHasActiveSession = false;

		if (currentSessionHandle != NULL)
		{
			// Delete the session
			XSessionDelete(currentSessionHandle, &deleteSessionOverlapped);

			// Reset the session info
			currentSessionHandle = NULL;
		}
		currentSessionNonce = 0;
		ZeroMemory(&currentSessionInfo, sizeof(currentSessionInfo));

		ea32Destroy(&pXSessionJoinedPlayers);
	}
}

// Begins a create session operation
void xSession_CreateSession(U32 iTeamId)
{
	DWORD dwResult;

	XUID playerXUID;

	// Continue only if the user is signed in
	if (XUserGetXUID(0, &playerXUID) != ERROR_SUCCESS)
	{
		return;
	}

	// We have to wait until delete/create session operations finish
	if (bSessionCreateIsInProgress || bSessionDeleteIsInProgress)
	{
		return;
	}

	// We need to kill the previous session first before creating a new session
	xSession_KillExistingSession();

	// Create a new session
	dwResult = XSessionCreate(XSESSION_CREATE_HOST | XSESSION_CREATE_USES_PEER_NETWORK | XSESSION_CREATE_USES_MATCHMAKING, 
		0, XSESSION_NUM_PUBLIC_SLOTS, XSESSION_NUM_PRIVATE_SLOTS, &currentSessionNonce, &currentSessionInfo, &createSessionOverlapped, &currentSessionHandle);

	if (dwResult == ERROR_IO_PENDING)
	{
		// Session create request is initiated successfully
		bSessionCreateIsInProgress = true;
	}
}

#endif