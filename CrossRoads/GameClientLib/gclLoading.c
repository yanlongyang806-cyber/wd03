/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclLoading.h"
#include "gclBaseStates.h"
#include "gclPatchStreaming.h"
#include "timing.h"
#include "globalstatemachine.h"
#include "net/net.h"
#include "sock.h"
#include "gameclientlib.h"
#include "MapDescription.h"
#include "structnet.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "gclHandleMsg.h"
#include "gclEntity.h"
#include "gclSendToServer.h"
#include "gclCommandParse.h"
#include "inputKeyBind.h"
#include "hoglib.h"
#include "gclLogin.h"
#include "gclDemo.h"
#include "gclUtils.h"
#include "EditLib.h"
#include "GfxLoadScreens.h"
#include "Expression.h"
#include "sysutil.h"
#include "logging.h"
#include "gclDialogBox.h"
#include "Player.h"
#include "ResourceInfo.h"
#include "gclPatching.h"
#include "wlState.h"
#include "wlPerf.h"
#include "contact_common.h"
#include "contactui_eval.h"
#include "appRegCache.h"
#include "logincommon.h"
#include "UGCEditorMain.h"
#include "UGCCommon.h"
#include "MicroTransactions.h"
#include "gclSmartAd.h"
#include "gclSteam.h"
#include "Login2Common.h"
#if _XBOX
#include "xbox\XStore.h"
#endif

#include "AutoGen/MapDescription_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#define LOADING_GAMESERVER_WAITING_TIMEOUT 480.0f
#define LOADING_GAMESERVER_CONNECTING_TIMEOUT 10.0f

// start by assuming we're going to load this many...
#define LOADING_GRAPHICS_INITIAL_GUESS 130000000
// but no matter how many we load, assume this many more.
#define LOADING_GRAPHICS_ALWAYS_MORE (LOADING_GRAPHICS_INITIAL_GUESS / 5)

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static ReturnedGameServerAddress *s_ReturnedServerAddress = NULL;
static KeyBindProfile s_LoadingBinds;

// Timers/counts/whatever for progress bar handling
static F32 s_fServerWaitingPercent;
static F32 s_fServerConnectingPercent;
static F32 s_fGraphicsLoadingPercent;
static S32 s_iTotalGraphicsLoadCount;
static bool s_bForcedLoadScreen = false;

static bool s_bPressAnyKey = true;
// enable/disable "press any key", useful to disable when testing loading screen UI.
AUTO_CMD_INT(s_bPressAnyKey, PressAnyKey) ACMD_HIDE;

static void SwitchToGameplayState(S32 iKey)
{
	if (!s_bPressAnyKey)
		return;
	else if (GSM_AreAnyStateChangesRequested())
		return;
	else if (GSM_IsStateActive(GCL_GAMEPLAY))
		GSM_SwitchToState_Complex(GCL_GAMEPLAY);
	else if (GSM_IsStateActive(GCL_LOADING))
		GSM_SwitchToState_Complex(GCL_LOADING "/../" GCL_GAMEPLAY);
	else
		devassertmsg(0, "Tried to switch to gameplay state from non-substate/non-loading state");
}

static int initial_load_timer=0;

AUTO_RUN_EARLY;
void gclLoadingInitTimer(void)
{
	initial_load_timer = timerAlloc();
	s_LoadingBinds.pchName = "Loading Screen Key Binds";
	s_LoadingBinds.cbAllOtherKeys = NULL;
}

void HandleReturnedServerAddress(Packet *pak)
{
	if (GSM_IsStateActiveOrPending(GCL_LOADING_WAITING_FOR_ADDRESS))
	{
		if (pktGetBits(pak, 1))
		{
			s_ReturnedServerAddress = StructCreate(parse_ReturnedGameServerAddress);
			ParserRecv(parse_ReturnedGameServerAddress, pak, s_ReturnedServerAddress, 0);
		}
		else
		{
			gclLoginFail("Couldn't get GameServer address");
		}
	}
}

void HandleServerConnectSuccess(Packet *pak)
{
	bool bProductionEdit = pktGetBits(pak,1);
	if (bProductionEdit)
	{
		UGCProjectData *project_data = pktGetStruct(pak, parse_UGCProjectData);
		UGCProjectAutosaveData *autosave_data = pktGetStruct(pak, parse_UGCProjectAutosaveData);
		ugcEditorSetStartupData(project_data, autosave_data);
		StructDestroy(parse_UGCProjectData, project_data);
		StructDestroy(parse_UGCProjectAutosaveData, autosave_data);
	}
	if (gGCLState.bGotLoginSuccess)
	{
		gclLoginFail("Got duplicate LOGIN_SUCCESS");
		return;
	}

	if (!GSM_IsStateActive(GCL_LOADING_GAMEPLAY))
	{
		gclLoginFail("States out of sync on LOGIN_SUCCESS");
		return;
	}

	gGCLState.bGotLoginSuccess = true;
	gclSetProductionEdit(bProductionEdit);

	resClientCancelAnyPendingRequests();
	loadstart_printf("Syncing Referents To Server...");
	resSyncAllDictionariesToServer();
#if !PLATFORM_CONSOLE
	if (areEditorsAllowed() && gGCLState.bPreLoadEditData)
	{
		resSubscribeToAllInfoIndicesOnce();
	}
#endif
	loadend_printf(" done.");
}

void HandleServerConnectFailure(Packet *pak)
{
	char msg[1024],*err;

	err = pktGetStringTemp(pak);
	sprintf(msg,"Server Login Failed: %s",err);
	gclLoginFail(msg);
}

void HandleStartTransfer(Packet *pak)
{
	DynamicPatchInfo *patchInfo = NULL;
	gppLastLink = &gServerLink;

	if(pktGetBool(pak))
	{
		patchInfo = StructCreate(parse_DynamicPatchInfo);
		ParserRecv(parse_DynamicPatchInfo, pak, patchInfo, 0);
	}

	gclPatching_SetPatchInfo(patchInfo);

	if(patchInfo && gclPatching_DynamicPatchingEnabled())
	{
		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOADING "/" GCL_LOADING_WAITING_FOR_ADDRESS "/" GCL_PATCH);
	}
	else
	{
		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOADING "/" GCL_LOADING_WAITING_FOR_ADDRESS);
	}

	StructDestroySafe(parse_DynamicPatchInfo, &patchInfo);
}


static void gclLoading_Enter(void)
{
	loadstart_printf("Loading...");
	gclBeginIgnoringInput();

	// Reset to game camera if contact camera is in use
	contactui_ResetToGameCamera();

	if (initial_load_timer)
		verbose_printf("Took %1.3fs from process startup to starting loading.\n", timerElapsed(initial_load_timer));
	gclPatchStreamingDebugLoadingStarted();

	fileDiskAccessAllowedInMainThread(true);
	gfxNotifyStartingLoading();

	keybind_PushProfileEx(&s_LoadingBinds, InputBindPriorityUI);

	s_LoadingBinds.cbAllOtherKeys = NULL;
	gGCLState.bGotWorldUpdate = false;
	gGCLState.bGotLoginSuccess = false;
	gGCLState.bGotGeneralUpdate = false;
	s_fServerWaitingPercent = 0;
	s_fServerConnectingPercent = 0;
	s_fGraphicsLoadingPercent = 0;
	s_iTotalGraphicsLoadCount = LOADING_GRAPHICS_INITIAL_GUESS;

	gfxLoadingStartWaiting();
}

static void GuessGraphicsPercent(void)
{
	F32 fLoads = gfxLoadingGetFinishedLoadCount();
	MAX1(s_iTotalGraphicsLoadCount, fLoads + LOADING_GRAPHICS_ALWAYS_MORE);
	MAX1(s_fGraphicsLoadingPercent, fLoads / s_iTotalGraphicsLoadCount);
}

static void gclLoading_Leave(void)
{
	gclStopIgnoringInput();

	keybind_PopProfileEx(&s_LoadingBinds, InputBindPriorityUI);
	s_LoadingBinds.cbAllOtherKeys = NULL;
	gfxLoadingFinishWaiting();
	editLibBudgetsReset();
	fileDiskAccessAllowedInMainThread(false);
	gfxNotifyDoneLoading();
	gclPatchStreamingDebugLoadingFinished();
	loadend_printf("done.");
	if (initial_load_timer) {
		gGCLState.startupTime = timerElapsed(initial_load_timer);
		verbose_printf("Took %1.3fs from process startup to done loading.\n", gGCLState.startupTime);
		timerFree(initial_load_timer);
		initial_load_timer = 0;
	}
}

static void gclLoading_EndFrame(void)
{
	if (isDevelopmentMode() && !gbNoGraphics)
		gfxLoadingDisplayScreen(true);
}

//------------------stuff for GCL_LOADING_WAITING_FOR_ADDRESS


#define PACKET_CASE(cmdName, packet)	xcase cmdName: PERFINFO_AUTO_START(#cmdName, 1); START_BIT_COUNT(packet, "recv:" #cmdName);
#define PACKET_CASE_END(packet) STOP_BIT_COUNT(packet); PERFINFO_AUTO_STOP()

int gclLoadingHandlePacket(Packet* pak, int cmd, NetLink* link,void *user_data)
{
	int ret = 1;

	PERFINFO_AUTO_START_FUNC();

	switch (cmd)
	{		
		PACKET_CASE(TOCLIENT_GAME_SERVER_ADDRESS, pak);
		{
			HandleReturnedServerAddress(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_CONNECT_SUCCESS, pak);
		{
			HandleServerConnectSuccess(pak);
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_MICROTRANSACTION_CATEGORY, pak);
		{
			MicroTrans_SetShardCategory(pktGetU32(pak));
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_CONNECT_FAILURE, pak);
		{
			HandleServerConnectFailure(pak);
		}
		PACKET_CASE_END(pak);
		
		PACKET_CASE(LOGINSERVER_TO_CLIENT_LOGIN_FAILED, pak);
		if (!GSM_IsStateActive(GCL_LOGIN_FAILED))
		{
			gclLoginFail(pktGetStringTemp(pak));
		}
		PACKET_CASE_END(pak);

		PACKET_CASE(TOCLIENT_GAME_TRANSFER_FAILED, pak);
		if (!GSM_IsStateActive(GCL_LOGIN_FAILED))
		{
			gclLoginFail(pktGetStringTemp(pak));
		}
		PACKET_CASE_END(pak);		

		PACKET_CASE(TO_CLIENT_DEBUG_MESSAGE, pak);
		{
			GameDialogGenericMessage("Login Debug Message", pktGetStringTemp(pak));
		}
		PACKET_CASE_END(pak);

        PACKET_CASE(LOGIN2_TO_CLIENT_LOGIN_FAILED, pak);
        {
            if (!GSM_IsStateActive(GCL_LOGIN_FAILED))
            {
                const char *errorMessageKey = NULL;
                const char *translatedError = NULL;

                errorMessageKey = pktGetStringTemp(pak);
                if ( errorMessageKey )
                {
                    translatedError = TranslateMessageKey(errorMessageKey);
                }
                gclLoginFail(translatedError);
            }
        }
        PACKET_CASE_END(pak);
	}

	PERFINFO_AUTO_STOP();

	return ret;
}

static void gclLoading_TellRegistryLoginBegan(void)
{
    regPutAppInt(GetLoginBeganKeyName(gclLoginGetChosenCharacterName()), 1);
	
}

static void gclLoadingWaitingForAddress_Enter(void)
{
	static char loadingMsg[100];


	gclLoading_TellRegistryLoginBegan();


	s_fServerWaitingPercent = 0.0;

	if(s_ReturnedServerAddress){
		sprintf(loadingMsg, "Already received new GameServer address");
	}else{
		char		ipString[100];
		const char*	serverTypeName = gppLastLink == &gServerLink ?
										"GameServer" :
										"LoginServer";

		linkGetIpListenPortStr(*gppLastLink, SAFESTR(ipString));

		sprintf(loadingMsg, "Waiting for new GameServer address from %s %s", serverTypeName, ipString);

		linkChangeCallback(*gppLastLink,gclLoadingHandlePacket);
	}

	gfxLoadingSetLoadingMessage(loadingMsg);
	loadstart_printf("%s...", loadingMsg);
}

static void gclLoadingWaitingForAddress_BeginFrame(void)
{
	gfxResetFrameRateStabilizerCounts();

	if (GSM_TimeInState(NULL) >= LOADING_GAMESERVER_WAITING_TIMEOUT && !gGCLState.bNoTimeout)
	{
		gclLoginFail("Timeout while waiting for GameServer address");
		return;
	}

	// listen to the old server for updates	
	gclServerMonitorConnection();

	if(!GSM_IsStateActiveOrPending(GCL_PATCH)){
		if(s_ReturnedServerAddress){
			char ipbuf[32];

			gGCLState.gameServerPort = s_ReturnedServerAddress->iPortNum;

			//determine which game server IP to connect to
			//this is based on which Game Server IP has the most matching octets with the login server IP
			strcpy(gGCLState.gameServerIP, 
				   GetIpStr(ChooseIP(	gGCLState.connectedLoginServerIP,
										s_ReturnedServerAddress->iIPs[0],
										s_ReturnedServerAddress->iIPs[1]),
							SAFESTR(ipbuf)));

			gGCLState.iGameServerContainerID = s_ReturnedServerAddress->iContainerID;
			gGCLState.loginCookie = s_ReturnedServerAddress->iCookie;
			GSM_SwitchToSibling(GCL_LOADING_GAMESERVER_CONNECT, false);
		}
		else if(linkDisconnected(*gppLastLink)){
			char ipString[100];
			char failMsg[100];

			linkGetIpListenPortStr(*gppLastLink, SAFESTR(ipString));
			sprintf(failMsg, "Login server disconnected: %s", ipString);
			gclLoginFail(failMsg);
			return;
		}
	}

	s_fServerWaitingPercent += gGCLState.frameElapsedTime / LOADING_GAMESERVER_WAITING_TIMEOUT;
	MIN1(s_fServerWaitingPercent, 1.0);
}

static void gclLoadingWaitingForAddress_Leave(void)
{
	gfxLoadingSetLoadingMessage(NULL);
	StructDestroySafe(parse_ReturnedGameServerAddress, &s_ReturnedServerAddress);
	loadend_printf(" done.");
	s_fServerWaitingPercent = 1.0;
}

void gclLoadingHandleDisconnect(S32 isUnexpectedDisconnect)
{
	if (!GSM_IsStateActiveOrPending(GCL_LOADING) && !GSM_IsStateActiveOrPending(GCL_LOGIN))
	{
		// Disconnected during normal gameplay
		gclLoginFailNotDuringNormalLogin("Disconnected from Server");
	}
	else if(isUnexpectedDisconnect &&
			!s_ReturnedServerAddress)
	{
		gclLoginFail("Disconnected from Server");
	}
}

//--------------stuff for GCL_LOADING_GAMESERVER_CONNECT
static void gclLoadingGameServerConnect_Enter(void)
{
	const char* serverName = gGCLState.gameServerIP[0] ? gGCLState.gameServerIP : "localhost";

	loadstart_printf(	"Connecting to GameServer %s:%d...",
						serverName,
						gGCLState.gameServerPort);

	if(gclServerConnect(serverName, gGCLState.gameServerPort)){
		s_fServerConnectingPercent = 0.0;
		gfxLoadingSetLoadingMessage("Waiting for server");
	}
}

static void gclLoadingGameServerConnect_BeginFrame(void)
{
	gclServerMonitorConnection();

	if(linkDisconnected(*gppLastLink)){
		gclLoginFail("Login server disconnected");
		return;
	}

	gfxResetFrameRateStabilizerCounts();

	if(gclServerIsConnected()){
		GSM_SwitchToSibling(GCL_LOADING_GAMEPLAY, false);
		return;
	}
	
	if(	!gGCLState.bNoTimeout &&
		GSM_TimeInState(NULL) >= LOADING_GAMESERVER_CONNECTING_TIMEOUT)
	{
		gclLoginFail("Timed out connecting to GameServer");
		return;
	}

	s_fServerConnectingPercent += gGCLState.frameElapsedTime / LOADING_GAMESERVER_CONNECTING_TIMEOUT;
	MIN1(s_fServerConnectingPercent, 1.0);
}

static void gclLoadingGameServerConnect_Leave(void)
{
	gfxLoadingSetLoadingMessage(NULL);
	if(!gclServerIsConnected())
	{
		gclServerForceDisconnect("Leaving gclLoadingGameServerConnect without connecting");
	}

	loadend_printf(" done.");
	s_fServerConnectingPercent = 1.0;
}

//--------------stuff for GCL_LOADING_GAMEPLAY
static void gclSendGameLoginPacket(void){
	Packet* pak = gclServerCreateRealPacket(TOSERVER_GAME_LOGIN);
	
	if(!pak){
		return;
	}
	
	pktSendString(pak,gGCLState.loginCharacterName);
	pktSendBitsPack(pak,4,gGCLState.loginCookie);
	PutContainerIDIntoPacket(pak, gGCLState.loginCharacterID);
	#if PLATFORM_CONSOLE
		pktSendBits(pak, 1, 1);
	#else
		pktSendBits(pak, 1, 0);
	#endif

	pktSendBits(pak,1, gGCLState.bNoTimeout);
	pktSendU32(pak, getCurrentLocale());
	
	if(isProductionMode()){
		pktSendBits(pak, 1, 0);
	}else{
		char	buffer[1000];
		U32		pid = 0;
		
		#if !PLATFORM_CONSOLE
			pid = _getpid();
		#endif
		
		quick_sprintf(SAFESTR(buffer),
				"User: \"%s\"/\"%s\", PID: %d, Time: %"FORM_LL"d",
				getComputerName(),
				getUserName(),
				pid,
				time(NULL));

		pktSendBits(pak, 1, 1);
		pktSendString(pak, buffer);
	}

#if _XBOX
	// XBOX Specific Network Information
	pktSendBits(pak, 1, 1);

	// Initialize values
	xuid = 0;
	ZeroMemory(&xnAddr, sizeof(xnAddr));

	// Get the XUID
	for (localPlayer = 0; localPlayer < XUSER_MAX_COUNT; localPlayer++)
	{
		if (XUserGetXUID(0, &xuid) == ERROR_SUCCESS)
		{
			break;
		}
	}

	// Get the XNADDR
	if (XNetGetTitleXnAddr(&xnAddr) == XNET_GET_XNADDR_PENDING)
	{
		// This should never happen, method should always return XNET_GET_XNADDR_ONLINE
		// because the user can only talk to our servers if they are connected to XBOX Live
		ZeroMemory(&xnAddr, sizeof(xnAddr));
	}

	// Send the XUID
	pktSendBits64(pak, 64, xuid);
	// Send the XNADDR information
	pktSendBytes(pak, sizeof(xnAddr.abEnet), &xnAddr.abEnet);
	pktSendBytes(pak, sizeof(xnAddr.abOnline), &xnAddr.abOnline);
	pktSendU32(pak, xnAddr.ina.s_addr);
	pktSendU32(pak, xnAddr.inaOnline.s_addr);
	pktSendBits(pak, 16, xnAddr.wPortOnline);
#else
	// We don't have XBOX network information
	pktSendBits(pak, 1, 0);
#endif


	pktSend(&pak);
}

static void gclLoadingGameplay_Enter(void)
{
#if _XBOX
	XNADDR xnAddr;
	XUID xuid;
	S32 localPlayer;
#endif

	//gfxStatusPrintf("START loading data...");

	linkChangeCallback(gServerLink,gclHandlePacketFromGameServer);

	if (isDevelopmentMode())
	{
		NameList_CmdList_SetAccessLevel(pGlobCmdListNames, GameClientAccessLevel(NULL, 0));
	}

	gGCLState.bGotWorldUpdate = false;
	gGCLState.bGotLoginSuccess = false;
	gGCLState.bGotGeneralUpdate = false;
	gGCLState.bGotGameAccount = false;
	gGCLState.bReadyForGeneralUpdates = false;

	gclSendGameLoginPacket();

	loadend_printf("Done.");

#if _XBOX
	// Let the game server know about all purchases
	xStore_BeginPurchaseNotification();
#endif

	gfxLoadingSetLoadingMessage("Loading data");
}

static S32 shouldSkipPressAnyKey(void)
{
	return	!gGCLState.bForcePromptToStartLevel &&
			(	gConf.bSkipPressAnyKey ||
				gGCLState.bSkipPreload ||
				g_iQuickLogin ||
				gGCLState.bSkipPressAnyKey ||
				!gfxIsInactiveApp());
}

static void gclLoadingGameplay_BeginFrame(void)
{
	float fTimeInState = GSM_TimeInState(NULL);
	bool bStillLoadingData = true;
	static U32 uPatchTm = 0;
	static U32 uLoadTimeStart = 0;
	static bool bSentError = false;

	if (gGCLState.bGotWorldUpdate && !gGCLState.bReadyForGeneralUpdates && !worldZoneMapPatching(true))
	{
		Packet *pak = gclServerCreateRealPacket(TOSERVER_READY_FOR_GENERAL_UPDATES);
		if(pak){
			pktSend(&pak);
		}
		gGCLState.bReadyForGeneralUpdates = true;
	}
	else if(worldZoneMapPatching(true) && timeSecondsSince2000() > uPatchTm + 1)
	{
		// only send around once per two seconds
		Packet *pak = gclServerCreateRealPacket(TOSERVER_CLIENT_PATCHING_WORLD);
		if(pak)
		{
			pktSend(&pak);
		}
		uPatchTm = timeSecondsSince2000();
	}

	if (gGCLState.bGotGeneralUpdate)
	{
		gclConnectedToGameServerOncePerFrame();		
		gGCLState.bDrawWorldThisFrame = true;
		uLoadTimeStart = 0;
		bSentError = false;
	}
	else if(isProductionMode())
	{
		if(uLoadTimeStart == 0)
		{
			uLoadTimeStart = timeSecondsSince2000();
		}
		else if(!bSentError && timeSecondsSince2000() > uLoadTimeStart + 2 * SECONDS_PER_MINUTE)
		{
			// send error as this client took too long to get to map
			Entity* pEnt = entActivePlayerPtr();

			bSentError = true;
			if(pEnt)
			{
				ErrorDetailsf("%s GotWorld %d, ReadyForGeneral %d, Patching %d", ENTDEBUGNAME(pEnt), gGCLState.bGotWorldUpdate, gGCLState.bReadyForGeneralUpdates, worldZoneMapPatching(true));
			}
			else
			{
				ErrorDetailsf("GotWorld %d, ReadyForGeneral %d, Patching %d", gGCLState.bGotWorldUpdate, gGCLState.bReadyForGeneralUpdates, worldZoneMapPatching(true));
			}
			Errorf("Client possibly stuck in gclLoadingGameplay_BeginFrame.");
		}
		gclServerMonitorConnection();
	}

	if(gclServerIsDisconnected())
	{
		gclLoginFail("Lost server connection while loading");
		return;
	}

	if (!gfxLoadingIsStillLoading())
	{
		if (bStillLoadingData)
			gfxLoadingSetLoadingMessage("Waiting for server");
		bStillLoadingData = false;
	}

	if(!gGCLState.bGotGameAccount)
	{
		Entity* e = entActivePlayerPtr();
		Player* p = SAFE_MEMBER(e, pPlayer);

		if(	SAFE_MEMBER(p, pPlayerAccountData) &&
			IS_HANDLE_ACTIVE(p->pPlayerAccountData->hData))
		{
			gGCLState.bGotGameAccount = !!GET_REF(p->pPlayerAccountData->hData);

			if(gGCLState.bGotGameAccount)
			{
				gclSmartAds_EvaulateAds();
			}

			if(gclSteamID())
			{
				ServerCmd_gslSteamEnabled();
			}
		}
	}

	if( (	!bStillLoadingData ||
			gGCLState.bSkipPreload) && 
		gGCLState.bGotLoginSuccess &&
		gGCLState.bGotWorldUpdate &&
		gGCLState.bGotGeneralUpdate &&
		(	gGCLState.bGotGameAccount ||
			gbSkipAccountLogin ||
			g_iQuickLogin))
	{
		gclStopIgnoringInput();
		if(shouldSkipPressAnyKey()){
			SwitchToGameplayState(0);
		}else{
			GSM_SwitchToSibling(GCL_LOADING_PRESS_ANY_KEY, false);
			s_LoadingBinds.cbAllOtherKeys = SwitchToGameplayState;
		}
	}	

	wlPerfEndMiscBudget();
	gfxMaterialPreloadOncePerFrame(false);
	wlPerfStartMiscBudget();
}

static void gclLoadingGameplay_Leave(void)
{
	gfxLoadingSetLoadingMessage(NULL);
}

static bool started_map_patch=false;
static void gclDemoLoading_Enter(void)
{
	const char* zoneName = NULL;

	loadstart_printf("Loading...");

	if (initial_load_timer)
		verbose_printf("Took %1.3fs from process startup to starting loading.\n", timerElapsed(initial_load_timer));

	demo_saveMemoryUsage(NULL, 0, "PRELOAD");

	gfxPreloadCheckStartGlobal();
	gfxClearGraphicsData();
	gfxNotifyStartingLoading();

	s_LoadingBinds.pchName = "Loading Screen Key Binds";
	s_LoadingBinds.cbAllOtherKeys = NULL;
	keybind_PushProfileEx(&s_LoadingBinds, InputBindPriorityUI);

	// Load demo stuff (camera position, server-to-client messages that happened during loading
	demo_LoadReplay();
	
	if (isPatchStreamingOn())
	{
		demo_startMapPatching();
	}
	started_map_patch = true;

	gfxLoadingStartWaiting();

	keybind_PushProfileName("DevelopmentKeyBinds");

	gfxLoadingSetLoadingMessage("Loading data");
}

static void gclDemoLoading_EndFrame(void)
{
	gGCLState.bDrawWorldThisFrame = true;

	worldLibOncePerFrame(gGCLState.frameElapsedTime);

	if (started_map_patch && !worldZoneMapPatching(false))
	{
		// Done patching (or no dynamic patching at all), load it!
		started_map_patch  = false;
		demo_loadMap();
		gfxPreloadCheckStartMapSpecific(false);
	}

	gfxMaterialPreloadOncePerFrame(false);

	if (isDevelopmentMode())
		gfxLoadingDisplayScreen(false);

	if (!gfxLoadingIsStillLoading())
		GSM_SwitchToState_Complex(GCL_DEMO_LOADING "/../" GCL_DEMO_PLAYBACK);
}

static void gclDemoLoading_Leave(void)
{
	keybind_PopProfileEx(&s_LoadingBinds, InputBindPriorityUI);

	gfxLoadingFinishWaiting();
	editLibBudgetsReset();
	fileDiskAccessAllowedInMainThread(false);
	gfxNotifyDoneLoading();
	loadend_printf("done.");

	if (initial_load_timer) {
		gGCLState.startupTime = timerElapsed(initial_load_timer);
		verbose_printf("Took %1.3fs from process startup to done loading.\n", gGCLState.startupTime);
		timerFree(initial_load_timer);
		initial_load_timer = 0;
	}

}

extern void ugcLoadEditingData(Entity *pEnt);
static void gclLoadingPressAnyKey_Enter(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if( isProductionEditMode() ) {
		ugcLoadEditingData(pEnt);
	}
}

static void gclLoadingPressAnyKey_BeginFrame(void)
{
	gclConnectedToGameServerOncePerFrame();
	gfxResetFrameRateStabilizerCounts();
}

static void gclLoadingWithinMap_Enter(void)
{
	s_LoadingBinds.cbAllOtherKeys = NULL;
	keybind_PushProfileEx(&s_LoadingBinds, InputBindPriorityUI);
	gfxLoadingStartWaiting();
	gfxLoadingSetLoadingMessage("Loading data");

	s_fServerWaitingPercent = 1.0;
	s_fServerConnectingPercent = 1.0;
	s_fGraphicsLoadingPercent = 0;
	s_iTotalGraphicsLoadCount = LOADING_GRAPHICS_INITIAL_GUESS;
}

static void gclLoadingWithinMap_EndFrame(void)
{
	if (gfxLoadingIsStillLoading())
	{
		wlPerfEndMiscBudget();
		gfxMaterialPreloadOncePerFrame(false);
		wlPerfStartMiscBudget();
	}
	else if (s_bForcedLoadScreen)
	{
		gfxLoadingSetLoadingMessage("Loading data");
	}
	else
	{
		if(shouldSkipPressAnyKey()){
			SwitchToGameplayState(0);
		}else{
			gfxLoadingSetLoadingMessage("Press Any Key");
			s_LoadingBinds.cbAllOtherKeys = SwitchToGameplayState;
		}
	}
}

static void gclLoadingWithinMap_Leave(void)
{
	Packet *pak;
	gfxLoadingSetLoadingMessage(NULL);
	gfxLoadingFinishWaiting();
	pak = gclServerCreateRealPacket(TOSERVER_DONE_LOADING);
	if(pak){
		pktSend(&pak);
	}
	keybind_PopProfileEx(&s_LoadingBinds, InputBindPriorityUI);
	s_LoadingBinds.cbAllOtherKeys = NULL;
	s_bForcedLoadScreen = false;
}

bool gclLoadingIsStillLoading(void)
{
	if (s_bForcedLoadScreen)
	{
		return true;
	}
	else if(!shouldSkipPressAnyKey())
	{
		// There's a couple different ways to signify that loading is done (LoadingGameplay,
		// LoadingWithinMap), but they all set this keybind profile's callback when
		// finished, and unset it it when starting.
		return !s_LoadingBinds.cbAllOtherKeys;
	}
	else
	{
		return (GSM_IsStateActive(GCL_LOADING_WAITING_FOR_ADDRESS)
			|| GSM_IsStateActive(GCL_LOADING_GAMESERVER_CONNECT)
			|| !gGCLState.bGotLoginSuccess
			|| !gGCLState.bGotWorldUpdate
			|| !gGCLState.bGotGeneralUpdate
			|| !GSM_IsStateActive(GCL_LOADING_PRESS_ANY_KEY)); // Not done loading until we are told to get into this state
	}
}

// Check to see if we're still loading data and should wait.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Loading_IsStillLoading");
bool gclExprLoadingIsStillLoading(void)
{
	return gclLoadingIsStillLoading();
}

F32 gclLoadingScreenGetProgress(void)
{
	if (!gclLoadingIsStillLoading())
		return 1.0;
	else
	{
		GuessGraphicsPercent();
		return (s_fServerConnectingPercent * 0.1
			+ s_fServerWaitingPercent * 0.3
			+ s_fGraphicsLoadingPercent * 0.6);
	}
}

// Check to see if we're still loading data and should wait.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LoadingScreenGetProgress");
F32 gclExprLoadingScreenGetProgress(void)
{
	return gclLoadingScreenGetProgress();
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclLoading_Respawned(void)
{
	s_bForcedLoadScreen = false;
	gclTurnOffAllControlBits();
	if (GSM_IsStateActive(GCL_GAMEPLAY))
		GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_LOADING_WITHIN_MAP);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclLoading_SetForcedLoading(void)
{
	s_bForcedLoadScreen = true;
	gclTurnOffAllControlBits();
	if (GSM_IsStateActive(GCL_GAMEPLAY))
		GSM_SwitchToState_Complex(GCL_GAMEPLAY "/" GCL_LOADING_WITHIN_MAP);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclLoading_ClearForcedLoading(void)
{
	s_bForcedLoadScreen = false;
}

AUTO_RUN;
void gclLoadingGameplay_AutoRegister(void)
{
	GSM_AddGlobalState(GCL_LOADING);
	GSM_AddGlobalStateCallbacks(GCL_LOADING, gclLoading_Enter, NULL, gclLoading_EndFrame, gclLoading_Leave);

	GSM_AddGlobalState(GCL_LOADING_GAMEPLAY);
	GSM_AddGlobalStateCallbacks(GCL_LOADING_GAMEPLAY, gclLoadingGameplay_Enter, gclLoadingGameplay_BeginFrame, NULL, gclLoadingGameplay_Leave);

	GSM_AddGlobalState(GCL_LOADING_WAITING_FOR_ADDRESS);
	GSM_AddGlobalStateCallbacks(GCL_LOADING_WAITING_FOR_ADDRESS, gclLoadingWaitingForAddress_Enter, 
		gclLoadingWaitingForAddress_BeginFrame, NULL, gclLoadingWaitingForAddress_Leave);

	GSM_AddGlobalState(GCL_LOADING_GAMESERVER_CONNECT);
	GSM_AddGlobalStateCallbacks(GCL_LOADING_GAMESERVER_CONNECT, gclLoadingGameServerConnect_Enter, 
		gclLoadingGameServerConnect_BeginFrame, NULL, gclLoadingGameServerConnect_Leave);

	GSM_AddGlobalState(GCL_DEMO_LOADING);
	GSM_AddGlobalStateCallbacks(GCL_DEMO_LOADING, gclDemoLoading_Enter, NULL, gclDemoLoading_EndFrame, gclDemoLoading_Leave);

	GSM_AddGlobalState(GCL_LOADING_PRESS_ANY_KEY);
	GSM_AddGlobalStateCallbacks(GCL_LOADING_PRESS_ANY_KEY, gclLoadingPressAnyKey_Enter, gclLoadingPressAnyKey_BeginFrame, NULL, NULL);

	GSM_AddGlobalState(GCL_LOADING_WITHIN_MAP);
	GSM_AddGlobalStateCallbacks(GCL_LOADING_WITHIN_MAP, gclLoadingWithinMap_Enter, NULL, gclLoadingWithinMap_EndFrame, gclLoadingWithinMap_Leave);

}

S32 gclHasReturnedServerAddress(void){
	return !!s_ReturnedServerAddress;
}
