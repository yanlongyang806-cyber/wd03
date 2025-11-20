#include "voice.h"
#include "earray.h"
#include "error.h"
#include "endian.h"
#include "utils.h"
#include "timing.h"

#define XBOX_INCLUDE // to prevent wininclude from creating macros that break xbox stuff

C_DECLARATIONS_BEGIN

#include "net/net.h"
#include "gclEntity.h"
#include "team.h"
#include "Player.h"
#include "GameClientLib.h"
#include "xbox\XSession.h"
#include "xbox\XCommon.h"
#include "XUtil.h"

C_DECLARATIONS_END

#if _PS3

#elif _XBOX
	#include <xaudio2.h>
	#include <xhv2.h>
	#include <xbox.h>
	#include <xtl.h>
	#pragma comment (lib, "xaudio2.lib")
	#ifdef _DEBUG
		#pragma comment (lib, "xhvd2.lib")
	#else
		#pragma comment (lib, "xhv2.lib")
	#endif
	#pragma comment (lib, "XOnline.lib")

C_DECLARATIONS_BEGIN
extern XNADDR gxnaddr;
extern XUID gxuid;
C_DECLARATIONS_END

#else
	#include "wininclude.h"
	#include "winxnet.h"
	#include <windows.h>
	#include "xam.h"
	#include "dsound.h"
	#pragma comment (lib, "../../3rdparty/DirectX/lib/dsound.lib")
#define XUSER_MAX_COUNT 1
#endif

#if _PS3

int voiceInit(void) {
	return 1;
}

void voiceProcess(void) {
}

void voiceCmdRegisterRemote(U64 xuid, ContainerID entID, U32 teamID) {
}

int voiceUnregisterRemote(U64 xuid) {
    return 1;
}

void voiceCmdLeaveVoiceChat(ContainerID leaderID) {
}

#elif _XBOX

#include "sock.h"

typedef struct VoiceData
{
	XUID remoteUID;
	char *packetData;
	U32 packetSize;
} VoiceData;

typedef struct EntIdent
{
	XUID xuid;
	ContainerID cid;
	bool isHost;
	IN_ADDR inAddr;
} EntIdent;

typedef struct VoiceMsg
{
	U64 xuid;
	ContainerID cid;
	XNADDR addr;
} VoiceMsg;


typedef enum 
{
	VOICE_MSG_NONE,
	VOICE_MSG_JOIN,
	VOICE_MSG_ADD_USER,
	VOICE_MSG_REMOVE_USER,
	VOICE_MSG_BECOME_HOST,
	VOICE_MSG_RECEIVE_HOST_INFO,
};

#define MAX_REMOTE_TALKERS 8

// Global pointer to the XHV2 (Voice) engine 
PIXHV2ENGINE g_pXHV2 = NULL;

// Global pointer to the XAudio2 engine
IXAudio2 *g_pXAudio2 = NULL;

// Bit flags indicating the voice registration of local players
static U8 g_fLocalTalkerRegistrationStatus = 0;

HANDLE  g_hWorkerThread;
XSESSION_INFO g_SessionInfo;
HANDLE g_SessionHandle = 0;
U64 g_SessionNonce;
bool g_bSessionStarted = false;
bool g_bIsSessionHost = false;

// the voice data to be sent
VoiceData **g_VoiceData = NULL;

//#else
//	typedef U64 XUID;
//#endif

#define VOICEDATA_BUFF_SIZE (1<<13)
#define VOICE_PORT 1000
SOCKET g_VoiceSock;

// because C++ hates earray
#define EAHANDLE(ea) ((EArrayHandle*)(void***)&(ea))
#define CEAHANDLE(ea) ((cEArrayHandle*)(void***)&(ea))
#define CCEAHANDLE(ea) ((ccEArrayHandle*)(void***)&(ea))
#define EAPUSH(ea, structptr) eaPush(CEAHANDLE(ea), (structptr))
#define EAPOP(ea) eaPop(CEAHANDLE(ea))
#define EASIZE(ea) eaSize(CEAHANDLE(ea))
#define EAFIND(ea, structptr), eaFind(CCEAHANDLE(ea), (structptr))
#define EAREMOVE(ea, idx) eaRemove(CEAHANDLE(ea), (idx))

#define EA64HANDLE(ea) ((EArray64Handle*)(U64*)&(ea))
#define CEA64HANDLE(ea) ((cEArray64Handle*)(U64*)&(ea))
#define EA64PUSH(ea, val) eai64Push(CEA64HANDLE(ea), (val))
#define EA64POP(ea) eai64Pop(EA64HANDLE(ea))
#define EA64SIZE(ea) eai64Size(CEA64HANDLE(ea))
#define EA64FINDANDREMOVE(ea, val) eai64FindAndRemove(EA64HANDLE(ea), (val))

void voiceReceive(XUID xuid, char *packetData, U32 packetSize)
{
	// TODO: remove allocations
	VoiceData *vd = (VoiceData*)calloc(1, sizeof(VoiceData));
	vd->remoteUID = xuid;
	vd->packetData = (char*)malloc(packetSize);
	memcpy(vd->packetData, packetData, packetSize);
	vd->packetSize = packetSize;
	EAPUSH(g_VoiceData, vd);
}

void voiceAddUserToSession(XUID xuid, ContainerID cid, IN_ADDR addr, bool isHost);

void endianSwapVoiceMsg(VoiceMsg *msg)
{
	if (!msg || isBigEndian())
		return;

	msg->xuid = endianSwapU64(msg->xuid);
	msg->cid = endianSwapU32(msg->cid);
}

char *voiceHandleMessage(char *pktData, int *pktSize, IN_ADDR addr)
{
	WORD gameDataSize = *((WORD*)(&pktData[0]));
	pktData += sizeof(WORD);

	(*pktSize) -= sizeof(WORD) + gameDataSize;
	return pktData + gameDataSize;
}

bool voiceGetXuidByInAddr(Entity *pEnt, Team *pTeam, SA_PARAM_NN_VALID IN_ADDR *inAddr, SA_PARAM_NN_VALID PXUID pXuid)
{
	assert(inAddr);
	assert(pXuid);

	int i;
	XNKID xnkId;
	XNADDR currentMemberXnAddr;
	IN_ADDR currentTeamMemberInAddr;

	Entity *pEntTeamMember = NULL;

	if (pEnt == NULL)
	{
		return false;
	}

	// Get the player's team
	pTeam = team_GetTeam(pEnt);

	// Do we have any team/team members
	if (pTeam == NULL || pTeam->pXSessionInfo == NULL || eaSize(&pTeam->eaMembers) == 0)
	{
		return false;
	}

	// Get the session ID
	xBoxStructConvertToXNKID(pTeam->pXSessionInfo->sessionID, &xnkId);

	// Iterate thru all team members and find the matching member
	for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
	{
		pEntTeamMember = GET_REF(pTeam->eaMembers[i]->hEnt);

		if (pEntTeamMember != NULL &&
			entGetContainerID(pEntTeamMember) != entGetContainerID(pEnt) && // skip self
			pEntTeamMember->pPlayer != NULL &&
			pEntTeamMember->pPlayer->pXBoxSpecificData != NULL &&
			pEntTeamMember->pPlayer->pXBoxSpecificData->pXnAddr != NULL &&
			pEntTeamMember->pPlayer->pXBoxSpecificData->xuid != 0)
		{
			// Get the XNADDR
			xBoxStructConvertToXNADDR(pEntTeamMember->pPlayer->pXBoxSpecificData->pXnAddr, &currentMemberXnAddr);

			// Convert to IN_ADDR
			if (XNetXnAddrToInAddr(&currentMemberXnAddr, &xnkId, &currentTeamMemberInAddr) == 0)
			{
				// Finally compare
				if (currentTeamMemberInAddr.s_addr == inAddr->s_addr)
				{
					// Set the XUID
					*pXuid = pEntTeamMember->pPlayer->pXBoxSpecificData->xuid;

					// Set the time of last voice packet receive
					pTeam->eaMembers[i]->iLastVoicePacketRcvTime = GetTickCount();
					return true;
				}
			}
		}
	}

	return false;
}

void voiceNetRecv(Entity *pEnt, Team *pTeam)
{
	XUID xuid;
	static sockaddr_in addr;
	static char buf[VOICEDATA_BUFF_SIZE] = {0};
	static int size = sizeof(SOCKADDR_IN);
	int bytes = 0;

	for (;;)
	{
		int lastError;

		bytes = recvfrom(g_VoiceSock, buf, VOICEDATA_BUFF_SIZE, 0, (SOCKADDR*)&addr, &size);

		lastError = WSAGetLastError();
		if (bytes == 0 || bytes == SOCKET_ERROR)
			break;

		char *voiceData = voiceHandleMessage(buf, &bytes, addr.sin_addr);
		if (bytes > 0)
		{
			if (voiceGetXuidByInAddr(pEnt, pTeam, &addr.sin_addr, &xuid) && 
				xuid != xUtil_GetCurrentPlayerXuid())
				voiceReceive(xuid, voiceData, bytes);
		} 
	}
}

int voiceNetInit()
{
	SOCKADDR_IN addr;
	int ret = 0;
	DWORD dwNonblocking = 1;

	sockStart();

	addr.sin_family = AF_INET;
#ifdef _XBOX
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(VOICE_PORT);

	g_VoiceSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_VDP);

	if (g_VoiceSock == INVALID_SOCKET || 
		bind(g_VoiceSock, (const sockaddr*)(&addr), sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
		return 0;

	if (ioctlsocket(g_VoiceSock, FIONBIO, &dwNonblocking) == SOCKET_ERROR)
		return 0;
#else
	addr.sin_addr.s_addr = XSocketHTONL(INADDR_ANY);
	addr.sin_port = XSocketHTONS(VOICE_PORT);

	g_VoiceSock = XSocketCreate(AF_INET, SOCK_DGRAM, IPPROTO_VDP);

	if (g_VoiceSock == INVALID_SOCKET || 
		XSocketBind(g_VoiceSock, (const sockaddr*)(&addr), sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
		return 0;

	if (XSocketIOCTLSocket(g_VoiceSock, FIONBIO, &dwNonblocking) == SOCKET_ERROR)
		return 0;
#endif

	return 1;
}


void voiceNetSend(SA_PARAM_NN_VALID XNADDR *pXnAddr, SA_PARAM_NN_VALID XNKID *pXnkId, U64 xuid, SA_PARAM_NN_VALID char *pktData, U32 pktSize)
{
	IN_ADDR inAddr;
	SOCKADDR_IN addr;
	int bytesSent = 0;
	int error = 0;

	if (pktSize == 0)
	{
		return;
	}

	// Convert the XNADDR to IN_ADDR
	if (XNetXnAddrToInAddr(pXnAddr, pXnkId, &inAddr))
	{
		return;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inAddr.s_addr;
	addr.sin_port = htons(VOICE_PORT);

	bytesSent = sendto(g_VoiceSock, pktData, pktSize, 0, (SOCKADDR*)&addr, sizeof(SOCKADDR_IN));

	error = WSAGetLastError();
}

C_DECLARATIONS_BEGIN
extern HWND g_VoiceHWND;
int initDirectSound();
C_DECLARATIONS_END

int voiceInit(void)
{
	loadstart_printf("Initializing voice... "); 

	DWORD ret;
	
#ifdef _XBOX

	XHV_INIT_PARAMS xhvParams = {0};
	XHV_PROCESSING_MODE LocalModes[]  = { XHV_VOICECHAT_MODE/*, XHV_LOOPBACK_MODE*/ }; 
    XHV_PROCESSING_MODE RemoteModes[] = { XHV_VOICECHAT_MODE };
	IXAudio2MasteringVoice *pMasteringVoice = NULL;
	HRESULT hr;

	// Initialize XAudio2
	hr = XAudio2Create(&g_pXAudio2, 0);
	if(FAILED(hr) || g_pXAudio2 == NULL)
		return 0;

	// Create a mastering voice	
	hr = g_pXAudio2->CreateMasteringVoice(&pMasteringVoice);
	if(FAILED(hr) || pMasteringVoice == 0)
		return 0;
#else
	initDirectSound();
	xhvParams.hwndFocus						= g_VoiceHWND;
#endif

	// Set up parameters for the voice chat engine
	xhvParams.dwMaxRemoteTalkers            = MAX_REMOTE_TALKERS;
	xhvParams.dwMaxLocalTalkers             = XUSER_MAX_COUNT;
	xhvParams.localTalkerEnabledModes		= LocalModes;
    xhvParams.remoteTalkerEnabledModes		= RemoteModes;
	xhvParams.dwNumLocalTalkerEnabledModes  = ARRAY_SIZE(LocalModes);
	xhvParams.dwNumRemoteTalkerEnabledModes = ARRAY_SIZE(RemoteModes);
	xhvParams.pXAudio2						= g_pXAudio2;

	// Create the engine
	if (FAILED(ret = XHV2CreateEngine(&xhvParams, &g_hWorkerThread, &g_pXHV2)))
	{
		loadend_printf("Failed to create XHV engine.");
		return 0;
	}
	else
	{
		voiceNetInit(); 
		XUserSetContext(0, X_CONTEXT_GAME_TYPE, X_CONTEXT_GAME_TYPE_STANDARD);
		XUserSetContext(0, X_CONTEXT_GAME_MODE, gGCLState.gameModeResource);
	}
	loadend_printf("done.");

	return 1;
}

int voiceRegisterLocal(Entity *pEnt, Team *pTeam)
{
	XUID  localXuid = {0};
	XHV_PROCESSING_MODE localProcModes[] = {XHV_VOICECHAT_MODE/*, XHV_LOOPBACK_MODE*/};
	DWORD localPlayer;

	// No player
	if (pEnt == NULL)
	{
		return 0;
	}

	// No team or no voice engine
	if (pTeam == NULL || g_pXHV2 == NULL)
	{
		return 0;
	}

	for(localPlayer = 0; localPlayer < XUSER_MAX_COUNT; localPlayer++)
	{
		// Is the player already registered
		if ((g_fLocalTalkerRegistrationStatus & (1 << localPlayer)) > 0)
		{
			return 0;
		}

		if(XUserGetXUID(localPlayer, &localXuid) == ERROR_SUCCESS)
		{
			BOOL result = FALSE;
			DWORD error = XUserCheckPrivilege( localPlayer, XPRIVILEGE_COMMUNICATIONS, &result );

			if( ( error == ERROR_SUCCESS ) && ( result == FALSE ) )
			{
				error = XUserCheckPrivilege(
					localPlayer, XPRIVILEGE_COMMUNICATIONS_FRIENDS_ONLY, &result );
			}

			if( ( error == ERROR_SUCCESS ) && ( result == TRUE ) )
			{
				if( g_pXHV2->RegisterLocalTalker( localPlayer ) == S_OK )
				{
					g_pXHV2->StartLocalProcessingModes( localPlayer, localProcModes, ARRAY_SIZE(localProcModes) );

					// Set the player as registered
					g_fLocalTalkerRegistrationStatus |= (1 << localPlayer);
				}
			}
		}
	}

	return 1;
}

// Unregisters all local talkers
void voiceUnregisterAllLocalTalkers(void)
{
	if (g_pXHV2 == NULL)
		return;

	DWORD local_player = 0;
	for(local_player = 0; local_player < XUSER_MAX_COUNT; local_player++)
	{
		// Is the local talker registered
		if ((g_fLocalTalkerRegistrationStatus & (1 << local_player)) > 0)
		{
			g_pXHV2->UnregisterLocalTalker(local_player);

			// Unregister the local talker
			g_fLocalTalkerRegistrationStatus &= ~(1 << local_player);
		}		
	}
}

// Unregisters all remote talkers
void voiceUnregisterAllRemoteTalkers(void)
{
	DWORD i = 0;

	// Number of remote talkers
	DWORD dwRemoteTalkersCount = 0;

	// Remote talkers array
	XUID xuidRemoteTalkers[MAX_REMOTE_TALKERS];

	// Get all remote talkers
	if (g_pXHV2->GetRemoteTalkers(&dwRemoteTalkersCount, xuidRemoteTalkers) != S_OK || dwRemoteTalkersCount <= 0)
	{
		return;
	}

	// Unregister everyone
	for (i = 0; i < dwRemoteTalkersCount; i++)
	{
		g_pXHV2->UnregisterRemoteTalker(xuidRemoteTalkers[i]);
	}
}

// Determines if the xuid is registered as a remote talker
bool voiceIsRemoteTalker(PXUID pXuid)
{
	assert(pXuid);

	DWORD i = 0;

	// Number of remote talkers
	DWORD dwRemoteTalkersCount = 0;

	// Remote talkers array
	XUID xuidRemoteTalkers[MAX_REMOTE_TALKERS];

	// Get all remote talkers
	if (g_pXHV2->GetRemoteTalkers(&dwRemoteTalkersCount, xuidRemoteTalkers) != S_OK || dwRemoteTalkersCount <= 0)
	{
		return false;
	}

	// See if the pXuid is a registered remote talker
	for (i = 0; i < dwRemoteTalkersCount; i++)
	{
		if (*pXuid == xuidRemoteTalkers[i])
		{
			return true;
		}		
	}
	return false;
}

// This method registers/unregisters team members as
// remote talkers if necessary
void voiceHandleTeamMemberVoiceChatRegistration(Entity *pEnt, Team *pTeam)
{	
	bool bIsRemoteTalker = false;
	XUID xuid = 0;
	int i = 0;
	Entity *pEntTeamMember;

	if (pEnt == NULL)
	{
		return;
	} 

	// Do we have any team members
	if (pTeam == NULL || eaSize(&pTeam->eaMembers) == 0)
	{
		// If we do not have a team or any team member unregister all local/remote talkers
		voiceUnregisterAllRemoteTalkers();
		voiceUnregisterAllLocalTalkers();
		return;
	}

	// Iterate thru all team members and register/unregister
	for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
	{
		// Get the entity for the team member
		pEntTeamMember = GET_REF(pTeam->eaMembers[i]->hEnt);
		if (pEntTeamMember == NULL  ||
			pEntTeamMember->pPlayer == NULL ||
			pEntTeamMember->pPlayer->pXBoxSpecificData == NULL)
		{
			continue;
		}

		// Is this the current player
		if (pTeam->eaMembers[i]->iEntID == entGetContainerID(pEnt))
		{
			if (pTeam->eaMembers[i]->bJoinedVoiceChat)
				voiceRegisterLocal(pEnt, pTeam);
			else
				voiceUnregisterAllLocalTalkers();
		}

		else
		{		
			// Set the xuid
			xuid = pEntTeamMember->pPlayer->pXBoxSpecificData->xuid;

			// See if this player is already registered as a remote talker
			bIsRemoteTalker = voiceIsRemoteTalker(&xuid);

			if (pTeam->eaMembers[i]->bJoinedVoiceChat && !bIsRemoteTalker)
			{
				// This player should be registered as a remote talker
				if (xSession_IsInSession(pTeam->eaMembers[i]->iEntID) && g_pXHV2->RegisterRemoteTalker(xuid, NULL, NULL, NULL) == S_OK)
				{
					g_pXHV2->StartRemoteProcessingModes(xuid, PXHV_PROCESSING_MODE(&XHV_VOICECHAT_MODE), 1); 
				}
			}
			else if (!pTeam->eaMembers[i]->bJoinedVoiceChat && bIsRemoteTalker)
			{
				// This player should be unregistered as a remote talker
				g_pXHV2->UnregisterRemoteTalker(xuid);
			}
		}
	}
}

// Checks if a player is muted
bool voiceIsPlayerMuted(PXUID pXuid)
{
	BOOL bMuted = FALSE;
	DWORD dwLocalPlayer;
	DWORD dwResult;	
	BOOL bHasPrivilege = FALSE;
	BOOL bFriendOnly = false;

	// First check the communication privileges
	dwResult = XUserCheckPrivilege(0, XPRIVILEGE_COMMUNICATIONS, &bHasPrivilege);

	if (!bHasPrivilege)
	{
		dwResult = XUserCheckPrivilege(0, XPRIVILEGE_COMMUNICATIONS_FRIENDS_ONLY, &bHasPrivilege);
		if (bHasPrivilege)
		{
			// Gamer is allowed to talk only to members of the gamer's friends list.
			bFriendOnly = true;
		}
		else
		{
			// Gamer is not allowed to talk to anyone
			return true;
		}
	}


	// See if this player is muted
	for(dwLocalPlayer = 0; dwLocalPlayer < XUSER_MAX_COUNT; dwLocalPlayer++)
	{
		dwResult = XUserMuteListQuery(dwLocalPlayer, 
			*pXuid, 
			&bMuted);
		if (ERROR_SUCCESS == dwResult && bMuted)
		{
			return true;
		}
	}

	if (bFriendOnly)
		return !xCommon_IsFriend(*pXuid);
	else
		return false;
}

void voiceSendVoiceDataToActiveTeamMembers(Entity *pEnt, Team *pTeam, char *pktData, U32 pktSize)
{
	S32 i;

	XNADDR xnAddr;
	XNKID xnkId;

	// Team member
	Entity *pEntTeamMember = NULL;

	// Validate the player
	if (pEnt == NULL)
	{
		return;
	}

	// Do we have any team members
	if (pTeam == NULL || eaSize(&pTeam->eaMembers) == 0)
	{
		return;
	}

	// Iterate thru all team members
	for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
	{
		if (!pTeam->eaMembers[i]->bJoinedVoiceChat)
		{
			continue;
		}

		// Get the entity for the team member
		pEntTeamMember = GET_REF(pTeam->eaMembers[i]->hEnt);

		if (pEntTeamMember == NULL  ||
			entGetContainerID(pEnt) == entGetContainerID(pEntTeamMember) || // Self
			pEntTeamMember->pPlayer == NULL ||
			pEntTeamMember->pPlayer->pXBoxSpecificData == NULL ||
			pEntTeamMember->pPlayer->pXBoxSpecificData->xuid == 0 ||
			pEntTeamMember->pPlayer->pXBoxSpecificData->pXnAddr == NULL ||
			pTeam->pXSessionInfo == NULL ||
			voiceIsPlayerMuted((PXUID)&pEntTeamMember->pPlayer->pXBoxSpecificData->xuid))
		{
			continue;
		}

		// Convert the CrypticXnAddr to XNADDR
		xBoxStructConvertToXNADDR(pEntTeamMember->pPlayer->pXBoxSpecificData->pXnAddr, &xnAddr);

		// Convert the CrypticXnkId to XNKID
		xBoxStructConvertToXNKID(pTeam->pXSessionInfo->sessionID, &xnkId);

		// Send the voice data
		voiceNetSend(&xnAddr, 
			&xnkId, 
			pEntTeamMember->pPlayer->pXBoxSpecificData->xuid, 
			pktData, 
			pktSize);
	}
}

void voiceProcess(void)
{
	// Iterator
	S32 i;
	S32 cPackets;

	// Team
	Team *pTeam = NULL;

	// Get the current player
	Entity *pEnt = entActivePlayerPtr();

	bool bVoiceBufferFull = false;

	HRESULT hrSubmitIncomingChatResult;

	// Start the profiler
	PERFINFO_AUTO_START_FUNC();

	// Validate the player
	if (pEnt == NULL)
	{
		return;
	}

	// Get the player's team
	pTeam = team_GetTeam(pEnt);

	if (!g_pXHV2 || xUtil_GetCurrentPlayerXuid() == 0)
	{
		// Stop the profiler
		PERFINFO_AUTO_STOP();
		return;
	}
	
	// Register/unregister team members
	voiceHandleTeamMemberVoiceChatRegistration(pEnt, pTeam);

	// We don't need to do any work if there is no team or online session id
	if (pTeam == NULL || pTeam->pXSessionInfo == NULL || !xSession_HasActiveSession())
	{
		// Stop the profiler
		PERFINFO_AUTO_STOP();
		return;
	}

	// receive all the network data from remote talkers
	voiceNetRecv(pEnt, pTeam);

	// send outgoing voice chat
	for(i = 0; i < XUSER_MAX_COUNT; i++)
	{
		DWORD localPlayer = i;
		XUID  localXuid = {0};
		int result = XUserGetXUID(localPlayer, &localXuid);
		if (result == ERROR_SUCCESS && g_pXHV2->IsLocalTalking(localPlayer))
		{
			char buf[VOICEDATA_BUFF_SIZE] = {0};
			DWORD size = VOICEDATA_BUFF_SIZE;
			DWORD numPackets = 0;
			if (g_pXHV2->GetLocalChatData(localPlayer, (PBYTE)(buf + sizeof(WORD)), // the first byte is the size of the game data portion of the packet, which is always 0
				&size, &numPackets) == S_OK)
			{
				voiceSendVoiceDataToActiveTeamMembers(pEnt, pTeam, buf, size + sizeof(WORD));
			}
		}
	}

	// process incoming voice chat
	cPackets = EASIZE(g_VoiceData);
	for (i = cPackets - 1; i >= 0; i--)
	{
		if (!bVoiceBufferFull && 
			g_VoiceData[i]->remoteUID != 0 && 
			!voiceIsPlayerMuted(&g_VoiceData[i]->remoteUID))
		{
			hrSubmitIncomingChatResult = g_pXHV2->SubmitIncomingChatData(g_VoiceData[i]->remoteUID, (BYTE*)g_VoiceData[i]->packetData, (PDWORD)&g_VoiceData[i]->packetSize);
			if (hrSubmitIncomingChatResult != S_OK)
			{
				// voice buffer is probably full at this moment. Wait until the next game loop.
				bVoiceBufferFull = true;
			}
		}

		// Delete the voice packet
		free(g_VoiceData[i]->packetData);
		free(g_VoiceData[i]);

		// Delete the array item
		EAPOP(g_VoiceData);
	}

	// Stop the profiler
	PERFINFO_AUTO_STOP();
}

#endif
