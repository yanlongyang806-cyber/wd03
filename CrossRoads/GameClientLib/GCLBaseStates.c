/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclBaseStates.h"

#include "StringUtil.h"
#include "adebug.h"
#include "AutoStartupSupport.h"
#include "BulletinUI.h"
#include "Character.h"
#include "crypt.h"
#include "dynfxmanager.h"
#include "editlib.h"
#include "EditorManager.h"
#include "UGCEditorMain.h"
#include "CostumeCommonEntity.h"
#include "entitylib.h"
#include "fileutil.h"
#include "foldercache.h"
#include "gclAIDebugUI.h"
#include "gclBasicOptions.h"
#include "gclCamera.h"
#include "chat/gclChatLog.h"
#include "gclClass.h"
#include "gclCombatCallbacks.h"
#include "gclCombatDeathPrediction.h"
#include "gclcommandparse.h"
#include "gclCostumeOnly.h"
#include "gclCursorMode.h"
#include "gclCutscene.h"
#include "gclDebugDrawPrimitives.h"
#include "gclDeadBodies.h"
#include "gclDemo.h"
#include "gclDumpSending.h"
#include "gclEntity.h"
#include "gclEntityNet.h"
#include "gclextern.h"
#include "gclHandleMsg.h"
#include "gclDynamicsDebugUI.h"
#include "gclFriendsIgnore.h"
#include "gclLogin.h"
#include "gclOptions.h"
#include "gclPatching.h"
#include "gclPatchStreaming.h"
#include "gclPlayerControl.h"
#include "gclPlayVideo.h"
#include "gclReticle.h"
#include "gclQuickPlay.h"
#include "gclsendtoserver.h"
#include "script/gclScript.h"
#include "gclTransformation.h"
#include "gclutils.h"
#include "gclWorldDebug.h"
#include "gclLightDebugger.h"
#include "gclInterior.h"
#include "gclLoading.h"
#include "gclCostumeUI.h"
#include "gclDirectionalIndicatorFX.h"
#include "gDebug.h"
#include "GfxConsole.h"
#include "GfxDebug.h"
#include "GfxHeadshot.h"
#include "GfxSpriteText.h"
#include "GfxPrimitive.h"
#include "GlobalStateMachine.h"
#include "GuildUI.h"
#include "HTMLViewer.h"
#include "inputlib.h"
#include "logging.h"
#include "PVPScoreboardUI.h"
#include "gclPowersAEDebug.h"
#include "RenderLib.h"
#include "RegionRules.h"
#include "sndVoice.h"
#include "Sound_common.h"
#include "Soundlib.h"
#include "sysutil.h"
#include "TeamUI.h"
#include "FCItemUpgradeUI.h"
#include "ThreadManager.h"
#include "timing.h"
#include "trivia.h"
#include "fileCache.h"
#include "UIGenWidget.h"
#include "uiinternal.h"
#include "UtilitiesLib.h"
#include "RdrState.h"
#if _XBOX
#include "voice.h"
#include "xbox\XSession.h"
#include "xbox\XStore.h"
#include "xbox\XCommon.h"
#include "gclVoiceChatOptions.h"
#endif
#include "wltime.h"
#include "worldgrid.h"
#include "worldlib.h"
#include "winutil.h"
#include "sock.h"
#include "ConsoleDebug.h"
#include "ResourceCommands.h"
#include "EntityClient.h"
#include "TimedCallback.h"
#include "uiTray.h"
#include "Guild.h"
#include "GameClientLib.h"
#include "gclControlScheme.h"
#include "ClientTargeting.h"
#include "gclKeyBind.h"
#include "gclHUDOptions.h"
#include "gclChatOptions.h"
#include "gclNotify.h"
#include "Player.h"
#include "testclient_comm.h"
#include "gclTestClient.h"
#include "mission_common.h"
#include "ResourceManager.h"
#include "gclSocial.h"
#include "wlUGC.h"
#include "gclUGC.h"
#include "GameAccountDataCommon.h"
#include "gclSteam.h"
#include "gclUIGenPaperdollExpr.h"
#include "gclPetUI.h"
#include "AuctionUI.h"
#include "CharacterCreationUI.h"
#include "wlPerf.h"
#include "FCInventoryUI.h"
#include "gclGoldenPath.h"
#include "gclSimpleCpuUsage.h"
#include "gclPVP.h"
#include "cmdclient.h"

#include "Expression.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/ChatRelay_autogen_GenericServerCmdWrappers.h"
#include "Autogen/NotifyEnum_h_ast.h"

#if _XBOX && defined( PROFILE )
#include "xtl.h"
#include "tracerecording.h"
#pragma comment( lib, "tracerecording.lib" )
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
static int gNetGraph, gBitGraph, gAllowTestClients, gMinimalRendering, gTestClientIsController, gMakeMemdumpBetweenMaps, gValidateDepCheck;
static int gPrintTacticalTimes;
static bool noDrawWorld;
extern int giTestClientPort;
// Disables drawing the world in the primary action
AUTO_CMD_INT(noDrawWorld, noDrawWorld) ACMD_CMDLINE;

//while doing makebins, report status to testclient
bool gbReportMakebinsToTestClient = false;
AUTO_CMD_INT(gbReportMakebinsToTestClient, ReportMakebinsToTestClient) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

//always listen for simple commands from local host, for things like cryptic URL handler
bool gbAlwaysOpenSimpleCommandPort = false;
AUTO_CMD_INT(gbAlwaysOpenSimpleCommandPort, AlwaysOpenSimpleCommandPort) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

extern GuildEmblemList g_GuildEmblems;

static float sfElapsed;
static Vec4 sClearColor = {0.1f, 0.1f, 0.1f, 1.0f};

static int client_startup_timer;

static void gclGameplay_SetUpUI(void);
static void gclGameplay_UIOncePerFrame(void);
static void gclGameplay_DestroyUI(void);
void gclCombatDebugMeters_Tick();
static void gclPostCameraUpdates_OncePerFrame(F32 fElapsedTime);


//----------------stuff relating to GCL_INIT

AUTO_RUN;
void initClientStartupTimer(void)
{
	client_startup_timer = timerAlloc();
}

static void gclRequestQuit(void) {
	if (!ugcEditorQueryLogout(true, false))
		return;

   	utilitiesLibSetShouldQuit(true);
}

static bool gclUIPlayAudio(const char *pchSound, const char *pchFileContext)
{
	SoundSource *soundSource = sndPlayUIAudio(pchSound, pchFileContext);
	return !!soundSource;
}

bool gclUIValidateAudio(const char *pchSound, const char *pchFileContext)
{
	return sndEventExists(pchSound);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncPlayUISoundCheck(ExprContext *pContext, const char *pchSound, ACMD_EXPR_ERRSTRING errEstr)
{
	if(stricmp("DummyMultiValString", pchSound) == 0 || gclUIValidateAudio(pchSound, NULL))
	{
		return ExprFuncReturnFinished;
	}
	else
	{
		estrPrintf(errEstr, "Invalid UI sound %s", pchSound);
		return ExprFuncReturnError;
	}
}

// play a UI sound event
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayUISound) ACMD_EXPR_STATIC_CHECK(exprFuncPlayUISoundCheck);
ExprFuncReturnVal exprFuncPlayUISound(ExprContext *pContext, const char *pchSound, ACMD_EXPR_ERRSTRING errEstr)
{
	if(gclUIValidateAudio(pchSound, NULL))
	{
		gclUIPlayAudio(pchSound, exprContextGetBlameFile(pContext));
		return ExprFuncReturnFinished;
	}
	else
	{
		estrPrintf(errEstr, "Invalid UI sound %s", pchSound);
		return ExprFuncReturnError;
	}
}

// stop a UI sound event
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StopUISound) ACMD_EXPR_STATIC_CHECK(exprFuncPlayUISoundCheck);
ExprFuncReturnVal exprFuncStopUISound(ExprContext *pContext, const char *pchSound, ACMD_EXPR_ERRSTRING errEstr)
{
	sndStopUIAudio(pchSound);
	return ExprFuncReturnFinished;
}

AUTO_STARTUP(GameClient) ASTRT_DEPS(GraphicsLib,
									WorldLibZone,
									Entity,
									Combat,
									ContactHeadshotStyles,
									AttribStatsPresets,
									EntityCostumes,
									GameUI,
									QuickPlay,
									Items,
									ItemGems,
									ItemVars,
									ClientReward,
									Voice,
									BugReport,
									UIGen,
									CritterFactions,
									Dynamics,
									StoreCategories,
									Officers,
									AS_ControlSchemes,
									HUDOptionsDefaults,
									RegionRulesMinimal,
									Guilds,
									AS_TextFilter,
									PetContactLists,
									PetStore,
									ContactAudioPhrases,
									ContactConfig,
									ItemGen,
									AlgoPet,
									Chat,
									ResourceTags,
									Species,
									LoadingScreens,
									BindCommands,
									PlayerStatEnums,
									Notify,
									ItemTagInfo,
									ProjectGameClientConfig,
									GlobalExpressions,
									UnlockedAllegianceFlags,
									UnlockedCreateFlags,
									ResourceOverlayDef,
									DiaryDefs,
									AS_ActivityLogConfig,
									ShardVariables,
									XCommon,
									XSession,
									XStore,
									NameGen,
									WarpRestrictions,
									PlayerDifficulty,
									ClientInteraction,
									TailorWeaponStance,
									CameraSettings,
									AS_GuildRecruitParam,
									MicroTransactions,
									Joystick,
									Minigames,
									Leaderboard,
									EncounterDifficulties,
									AS_GuildThemes,
									Queues
									Expression,
									GclAuctionLoadConfig,
									UGC,
									UGCReporting,
									UGCSearchCache,
									MissionTags,
									MissionUITypes,
									ClientTutorialScreenRegions,
									MissionWarpCostsClient,
									AS_GameProgression,
									ItemAssignmentsMinimal,
									CharacterCombat,
									GameStringFormat,
									RewardValTables,
									BasicOptions,
									CharacterClassesClient,
									Keybinds,
									ActivitiesClient,
									DirectionalIndicatorFX,
									GAMESPECIFIC,
									ContactDialogFormatters,
									GameAccountNumericPurchase,
                                    NumericConversion,
									CurrencyExchangeConfig,
									UGCTips,
									SmartAds,
									MovementManagerConfig,
									CutscenesClient,
									ItemUpgrade,
									SuperCritterPet,
									GoldenPath,
									HTMLViewer,
									UGCAchievements,
									PlayerEmail,
									GroupProjectLevelTreeDef
								);
void gclStartup(void)
{
	gclLDStartup();
	gclDynamicsDebugStartup();

	if (isDevelopmentMode()) {
		gclDebugDrawPrimitive_Initialize();
		gclPowersAEDebug_Init();
		TimedCallback_Add(resourcePeriodicUpdate, NULL, 5.0f);
	}
}

AUTO_STARTUP(GAMESPECIFIC);
void gclEmptyGamespecificStartup(void)
{
	//Empty method, without this projects that don't have a GAMESPECIFIC autostartup will assert due to an unrecognized task.
}

AUTO_STARTUP(GameClientDev) ASTRT_DEPS(GameClient, Critters, SampleGraphs);
void gclDevStartup(void)
{

}

AUTO_STARTUP(GameClientNoGraphics) ASTRT_DEPS(	WorldLibZone,
												Entity,
												WorldLib,
												Entity,
												Combat,
												EntityCostumes,
												GameUI,
												QuickPlay,
												Items,
												ClientReward,
												Voice,
												BugReport,
												UIGen,
												RegionRulesMinimal,
												Notify,
												Officers,
												AS_ControlSchemes,
												PetStore,
												XCommon,
												XSession,
												XStore,
												CameraSettings);
void gclNoGraphicsStartup(void)
{
	mmSetLocalProcessing(0);
}

char* gclGetNameFromAccount(U32 id)
{
	Entity *e = NULL;
	
	e = entFromAccountID(id);

	if(e)
		return (char*)entGetAccountOrLocalName(e);

	return NULL;
}

AUTO_STARTUP(Voice) ASTRT_DEPS(Sound);
void voiceStartup(void)
{
	svIgnoreSetCallbacks(ClientChat_IsIgnoredAccount);
	svSetNameCallback(gclGetNameFromAccount);
	svSetLoggingFuncs(	ServerCmd_sndServerVoiceFirstSpoken, 
						ServerCmd_sndServerVoiceFirstListened,
						ServerCmd_sndServerVoiceAdPlayed);
	svSetNotifyFuncs(	gclSvNotifyJoin,
						gclSvNotifyLeave,
						gclSvNotifyFailure);
#if _XBOX
	voiceInit();
#endif
}

AUTO_STARTUP(XCommon);
void xCommonStartup(void)
{
#if _XBOX
	xCommon_Init();
#endif
}

AUTO_STARTUP(XSession);
void xSessionStartup(void)
{
#if _XBOX
	xSession_Init();
#endif
}

AUTO_STARTUP(XStore);
void xStoreStartup(void)
{
#if _XBOX
	xStore_Init();
#endif
}


AUTO_STARTUP(GCLSound);
void gclSoundInit(void)
{
	sndCommonSetEventExistsFunc(sndEventExists);
}

void gclInit_GoToNext(void);

void gclInit_BeginFrame(void)
{
	GfxPerDeviceState *gfx_primary_device = gfxGetPrimaryGfxDevice();
	RdrDevice *gcl_primary_device = gfxGetPrimaryDevice();

	// This has to happen here because we now accumulate CPU timings in this structure.
	gfxResetFrameCounters();

	wlPerfStartMiscBudget();

	if (gfxSettingsLoad(gfx_primary_device))
	{
		gfxSettingsApplyInitialSettings(gcl_primary_device, true);
		gclGraphicsOptions_Init();

		gclInit_GoToNext();
	}
	else
		Sleep(16);

	// gfxOncePerFrame manages some things that need to be run every
	// frame even if graphics are off, such as the FPS timer locks and
	// making sure the string memory cache doesn't get too large. It handles
	// gbNoGraphics itself, so it should be called here.
	gfxOncePerFrame(gGCLState.frameElapsedTime, gGCLState.frameElapsedTimeReal, emIsEditorActive() || ugcEditorIsActive(), true);

	gfxStartMainFrameAction(false, false, true, true, true);
}

void gclInit_EndFrame(void)
{
	gfxSetActiveDevice(gGCLState.pPrimaryDevice->device);	
	inpUpdateEarly(gfxGetActiveInputDevice());
	inpUpdateLate(gfxGetActiveInputDevice());

	gfxDrawFrame();
	wlPerfEndMiscBudget();
}

void gclInit_Enter(void)
{
	bool bInactiveDuringStartup = false;

	if(gAllowTestClients)
		assertForceServerMode(1);

	wlSetLoadFlags(0);

	mmCreateWorldCollIntegration();

	dynFxSetScreenShakeFunc(gclCamera_Shake);
	dynFxSetCameraFOVFunc(gclCamera_SetFOV);
	dynFxSetCameraDelayFunc(gclCamera_EnableFocusOverride);
	dynFxSetCameraLookAtFunc(gclCamera_SetLookatOverride);
	dynFxSetCameraMatrixOverrideFunc(gclCamera_SetFxCameraMatrixOverride);
	dynFxSetWaterAgitateFunc(gfxWaterAgitate);
	dynFxSetSkyVolumeFunctions(gfxSkyVolumePush, gfxSkyVolumeListOncePerFrame);

	gclInitCombatCallbacks();

	keybind_Init(conPrintf, gclCmdSetTimeStamp, gclRequestQuit, NULL);
	keybind_LoadUserBinds(NULL);

	if(gbNoGraphics)
	{
		AutoStartup_SetTaskIsOn("GameClientNoGraphics", 1);
	}
	else if (isDevelopmentMode())
	{
		AutoStartup_SetTaskIsOn("GameClientDev", 1);
	}
	else
	{
		AutoStartup_SetTaskIsOn("GameClient", 1);
	}

	gclCommandAliasLoad();

	loadstart_printf("Running Auto Startup...");
    // The Client no longer loads GamePermissions directly.  They are sent from the login server upon first connection.
    AutoStartup_RemoveAllDependenciesOn("GamePermissions");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	gclRegisterPrimaryRenderingDevice(gGCLState.hInstance);

	// Send any lingering dump data to the error tracker
	gclSendDeferredDumps();

	//ParseInfoWriteTextFile("C:\\entschema.txt",parse_Entity);

	gclLoadCmdConfig();

	gclLoadLoginConfig();

	if (!gGCLState.loginName[0])
	{
		sprintf(gGCLState.loginName,"%s",getUserName());
		sprintf(gGCLState.loginCharacterName,"%s",getUserName());

		if (gAllowTestClients && !gTestClientIsController)
		{
			char tempStr[10];
			sprintf(tempStr, "_%d", giTestClientPort);
			strcat(gGCLState.loginName, tempStr);
			strcat(gGCLState.loginCharacterName, tempStr);
		}
	}




	if (eaSize(&gGCLState.ppLoginServerNames) == 0 && eaSize(&gGCLState.eaLoginServers) == 0)
	{
		eaPush(&gGCLState.ppLoginServerNames, "localhost");
	}

	if ( !gGCLState.accountServerName[0] )
	{
		// TODO
		// currently just uses same address as login server; assumes both are at same address
		strcpy(gGCLState.accountServerName, gGCLState.ppLoginServerNames[0]);
	}


	// FolderCacheSetMode call should be last (otherwise file loading is slower)
	if (gGCLState.bDisableFolderCacheAfterLoad)
		FolderCacheSetMode(FOLDER_CACHE_MODE_FILESYSTEM_ONLY); // CD: folder cache does not get updated on xbox yet
	FolderCacheEnableCallbacks(!gbNoGraphics);
	FolderCacheDoNotWarnOnOverruns(false);
	fileCacheFreeze(false);

	editLibStartup(TOSERVER_EDIT_MSG);

	if(isDevelopmentMode())
	{
		sndSetEditorActiveFunc(emIsEditorActive);
	}

#if _PS3
    {
        extern U8 _binary_linksign_txt_start;
        extern U8 _binary_linksign_txt_end;
        gGCLState.executableCRC = cryptAdler32(&_binary_linksign_txt_start, &_binary_linksign_txt_end - (U8 *)&_binary_linksign_txt_start);
    }
#else
	gGCLState.executableCRC = cryptAdlerFile(getExecutableName());
#endif

	if (gAllowTestClients)
	{
		InitTestClientCommunication();
		gclScript_Init();
	}

	if (isDevelopmentMode() || gTestClientIsController)
	{
		InitSimpleCommandPort();
	}

	if (resAnyCommandsPending())
	{
		resExecuteAllPendingCommands();
		GSM_Quit("resAnyCommandsPending() was true. Ask Ben Z");
		return;
	}
}

// Make bins and exit for a single namespace
AUTO_COMMAND ACMD_CMDLINE;
void MakeBinsAndExitForNamespace(char *ns)
{
	gbMakeBinsAndExit = true;
	ParserForceBinCreation(true);
	gpcMakeBinsAndExitNamespace = strdup(ns);
}

void gclInit_GoToNext(void)
{
	if (!gbMakeBinsAndExit)
	{
		printf("Client ready (%1.2fs load time).\n", timerElapsed(client_startup_timer));
		GSM_SwitchToSibling(GCL_BASE, false);
	}
	else
	{
		gfxPreloadCheckStartGlobal();

		gclMakeAllOtherBins();


		worldGridMakeAllBins(false, gMakeMemdumpBetweenMaps, gValidateDepCheck);

		if (gConf.bUserContent)
			ugcEditorStartup();

		zmapForceBinEncounterInfo();

		if (nullStr(gpcMakeBinsAndExitNamespace))
			ugcTakeObjectPreviewPhotos();

		if( worldGridShouldDeleteUntouchedFiles() )
		{
			worldResetWorldGrid();
			binReadTouchedFileList();
			binDeleteUntouchedFiles();
		}
		else
		{
			printf( "Warning: Skipping deleting untouched files.  This can happen if you use -OnlyMapToBin.\n" );
		}

		if (gbReportMakebinsToTestClient)
		{	
			SendCommandStringToTestClient("ClientMakeBinsSucceeded 1");
		}

		GSM_Quit("MakeBinsAndExit");
	}
}

AUTO_RUN;
void gcl_SetUpUI(void)
{
	ui_GenInitPointerVar("Player", parse_Entity);
	ui_GenInitPointerVar("Target", parse_Entity);
	ui_GenInitPointerVar("TargetAssist", parse_Entity);
	ui_GenInitPointerVar("TargetDual", parse_Entity);
	ui_GenInitPointerVar("TargetOfTarget", parse_Entity);
	ui_GenInitPointerVar("TargetOfTargetDual", parse_Entity);
	ui_GenInitPointerVar("TargetFocus", parse_Entity);
	ui_GenInitPointerVar("MouseOverEntity", parse_Entity);
	ui_SetPlayAudioFunc(gclUIPlayAudio, gclUIValidateAudio);
	ui_SetPostCameraUpdateFunc(gclPostCameraUpdates_OncePerFrame);
	ui_SetBeforeMainDraw(gclDrawStuffOverEntities);
	ui_GenAddExprFuncs("entityutil");
	ui_GenInitFloatVar("TotalTime");
	ui_GenInitStaticDefineVars(NotifyTypeEnum, "Notify");
}

void gclGameplayUpdateUIVars(void)
{
	Entity *e =  entActivePlayerPtr();
	Entity *target = entity_GetTarget(e);
	Entity *targetAssist = entity_GetAssistTarget(e);
	Entity *targetdual = entity_GetTargetDual(e);
	Entity *targetOfTarget = entity_GetTarget(target);
	Entity *targetOfTargetDual = entity_GetTarget(targetdual);
	Entity *targetFocus = entity_GetFocusTarget(e);

	// getEntityUnderMouse() may still be too slow, if it is, then
	// it could cache for 1/10th second + invalidation on mouse move
	// or when the cached entity moves. The 1/10th second is so that
	// if a new entity moves on top of the cached entity, the new
	// entity is eventually selected.
	
	// exclude the player if we are using bMouseLookHardTarget and are mouse looking
	// this is needed if the targeting reticule is positioned so it intersects the player
	bool bExcludePlayer = (g_CurrentScheme.bMouseLookHardTarget && gclPlayerControl_IsMouseLooking());
	Entity *mouseOver = getEntityUnderMouse(bExcludePlayer);

	PERFINFO_AUTO_START_FUNC_PIX();

	ui_GenSetPointerVar("Player", e, parse_Entity);
	ui_GenSetPointerVar("Target", target, parse_Entity);
	ui_GenSetPointerVar("TargetAssist", targetAssist, parse_Entity);
	ui_GenSetPointerVar("TargetDual", targetdual, parse_Entity);
	ui_GenSetPointerVar("TargetOfTarget", targetOfTarget, parse_Entity);
	ui_GenSetPointerVar("TargetOfTargetDual", targetOfTargetDual, parse_Entity);
	ui_GenSetPointerVar("TargetFocus", targetFocus, parse_Entity);
	ui_GenSetPointerVar("MouseOverEntity", mouseOver, parse_Entity);

	ui_GenSetFloatVar("TotalTime", gGCLState.totalElapsedTimeMs / 1000.0);

	gclSetUserEntities();

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void gclGameplayClearUIVars(void)
{
	ui_GenSetPointerVar("Player", NULL, parse_Entity);
	ui_GenSetPointerVar("Target", NULL, parse_Entity);
	ui_GenSetPointerVar("TargetAssist", NULL, parse_Entity);
	ui_GenSetPointerVar("TargetDual", NULL, parse_Entity);
	ui_GenSetPointerVar("TargetOfTarget", NULL, parse_Entity);
	ui_GenSetPointerVar("TargetOfTargetDual", NULL, parse_Entity);
	ui_GenSetPointerVar("TargetFocus", NULL, parse_Entity);
	ui_GenSetPointerVar("MouseOverEntity", NULL, parse_Entity);
	gclClearUserEntities();
}

/////---------------stuff relating to GCL_BASE

static gclGhostDrawFunc *s_eacbGhostDraw;

void gclRegisterGhostDrawFunc(gclGhostDrawFunc cbDraw)
{
	eaPush((void ***)&s_eacbGhostDraw, cbDraw);
}

bool gclRemoveGhostDrawFunc(gclGhostDrawFunc cbDraw)
{
	return (eaFindAndRemove((void ***)&s_eacbGhostDraw, cbDraw) >= 0);
}

static BOOL s_bScreenSaverWasOn;

static void gclDisableScreensaver(void)
{
#if !PLATFORM_CONSOLE
	SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, &s_bScreenSaverWasOn, 0);
	SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, 0, NULL, 0);
#endif
}

static void gclEnableScreensaver(void)
{
#if !PLATFORM_CONSOLE
	SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, s_bScreenSaverWasOn, NULL, 0);
#endif
}

static void HandleSimpleCommandMessage(Packet *pak,int cmd, NetLink *link, void *userdata)
{
	switch(cmd)
	{
	xcase FROM_TESTCLIENT_CMD_SENDCOMMAND:
		{
			char *pProductName = pktGetStringTemp(pak);
			char *pCommand = pktGetStringTemp(pak);

			if (stricmp(pProductName, "all") == 0 || stricmp(pProductName, GetProductName()) == 0)
			{
				globCmdParse(pCommand);
			}
		}
	}
}

void InitSimpleCommandPort(void)
{
	static bool bInitted = false;

	if (bInitted)
	{
		return;
	}

	if(gTestClientIsController)
	{
		bInitted = true;
		commListen(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH|LINK_SMALL_LISTEN, CLIENT_SIMPLE_COMMAND_PORT, HandleSimpleCommandMessage, NULL, NULL, 0);
	}
	else
	{
#if !PLATFORM_CONSOLE
		if (GameClientAccessLevel(NULL, 0) > 0 || isDevelopmentMode() || gbAlwaysOpenSimpleCommandPort)
		{
			bInitted = true;
			commListenIp(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH|LINK_SMALL_LISTEN, CLIENT_SIMPLE_COMMAND_PORT, HandleSimpleCommandMessage, NULL, NULL, 0, htonl(INADDR_LOOPBACK));
		}
#endif
	}
}

void gclBase_Enter(void)
{
	frameLockedTimerCreate(&gGCLState.frameLockedTimer, 3000, 3000 / 60);

	gfxClearGraphicsData();
	gclDisableScreensaver();
	gclLoadingDone();

	wlPerfSetAssertOnMisnestedBudgets(true);

	commSetMinReceiveTimeoutMS(commDefault(), 0);

	gclStopIgnoringInput();

	if(demo_playingBack())
		GSM_AddChildState(GCL_DEMO_LOADING, false);
	else
	{
		if (gConf.pchLaunchVideoPath && gConf.pchLaunchVideoPath[0])
		{
			gclPlayVideo_Start(gConf.pchLaunchVideoPath, 0, true);
		}
		else
		{
			GSM_AddChildState(GCL_LOGIN, false);
		}
	}
}

static void gclSetConsoleTitle(void)
{
#if !PLATFORM_CONSOLE
    int pid = 0;
	char title[256];
    pid = _getpid();
	sprintf(title, "%s %d", gGCLState.loginCharacterName,pid);
	setConsoleTitle(title);
#endif
}

static int curTimer = -1;
static S64 timeArray[10];

AUTO_COMMAND;
void gclStartTimer(void)
{
	if(curTimer>=(int)ARRAY_SIZE(timeArray)-1)
		return;

	curTimer++;
	timeArray[curTimer] = ABS_TIME;
}

AUTO_COMMAND;
void gclStopTimer(void)
{
	if(curTimer<=-1)
		return;
	curTimer--;
}

AUTO_COMMAND;
void gclClearTimers(void)
{
	int i;

	for(i=0; i<curTimer; i++)
		timeArray[i] = 0;

	curTimer = -1;
}

static void gclStartNewTimerFrame(F32 timeStepScale){
	frameLockedTimerStartNewFrame(	gGCLState.frameLockedTimer,
									timeStepScale);

	frameLockedTimerGetPrevTimes(	gGCLState.frameLockedTimer,
									&gGCLState.frameElapsedTime,
									&gGCLState.totalElapsedTimeMs,
									NULL);

	frameLockedTimerGetPrevTimesReal(	gGCLState.frameLockedTimer,
										&gGCLState.frameElapsedTimeReal);

	frameLockedTimerGetCurTimesReal(gGCLState.frameLockedTimer,
									&gGCLState.frameElapsedTimeRealNext);
}

static void gclSetWindowTitle(void){
	if (isDevelopmentMode())
	{
		char *tempTitleString = NULL;
		char fullString[1024];
		
		PERFINFO_AUTO_START("GSM_SetTitle", 1);
		
		estrStackCreate(&tempTitleString);

		if (emIsEditorActive())
			sprintf(fullString, "GameClient %s", zmapGetFilename(NULL));
		else
		{
			GSM_PutFullStateStackIntoEString(&tempTitleString);
			sprintf(fullString, "GameClient %s", tempTitleString);
		}

		gfxSetTitle(fullString);

		estrDestroy(&tempTitleString);
		
		PERFINFO_AUTO_STOP();
	}
}

static void gclDisplayDemoRecording(void){
	if (demo_recording())
		gfxXYprintfColor(5, 30, 255, 0, 0, 255, "DEMO RECORDING");
}

static void gclDisplayTacticalTimes(void){
	Entity* e;
	
	if(!gPrintTacticalTimes){
		return;
	}
	
	if(e = entActivePlayerPtr()){
		U8	r = 255;
		U8	g = 255;
		U8	b = 255;
		F32 used;
		F32 total;
		S32 sprintUsesFuel;
		F32 sprintFuel;

		// Print the sprint progress.

		entGetSprintTimes(e, &used, &total, &sprintUsesFuel, &sprintFuel);
		if(sprintUsesFuel){
			gfxXYprintfColor(	5, 5, r, g, b, 255,
								"Sprint fuel seconds: %1.3f / %1.3f (%1.2f%%)",
								sprintFuel,
								total,
								total > 0 ? 100.f * sprintFuel / total : 0.f);
		}
		else if(entIsSprinting(e)){
			gfxXYprintfColor(	5, 5, r, g, b, 255,
								"Sprint time: %1.3f / %1.3f (%1.2f%%)",
								used,
								total,
								total > 0 ? 100.f * used / total : 0.f);
		}
		
		// Print the sprint cooldown.
		
		entGetSprintCooldownTimes(e, &used, &total);
		if(used != total){
			gfxXYprintfColor(	5, 6, r, g, b, 255,
								"Sprint cooldown time: %1.3f / %1.3f (%1.2f%%)",
								used,
								total,
								total > 0 ? 100.f * used / total : 0.f);
		}
		
		// Print the roll cooldown.
		
		entGetRollCooldownTimes(e, &used, &total);
		if(used != total){
			gfxXYprintfColor(	5, 7, r, g, b, 255,
								"Roll cooldown time: %1.3f / %1.3f (%1.2f%%)",
								used,
								total,
								total > 0 ? 100.f * used / total : 0.f);
		}
	}
}

static void gclXboxTick(void){
#if _XBOX
	xCommon_Tick();
	xSession_Tick();
	xStore_Tick();

	if(!gbNoGraphics)
	{
		voiceProcess();
	}
#endif
}

void gclBase_BeginFrame(void)
{
	F32 timeStepScale = wlTimeGetStepScale();

	// This has to happen here because we now accumulate CPU timings in this structure.
	gfxResetFrameCounters();

	wlPerfStartMiscBudget();

	gclSetWindowTitle();
    gclSetConsoleTitle();    

	gclStartNewTimerFrame(timeStepScale);
	gclUpdateThreadPriority();
	winCheckAccessibilityShortcuts(gfxIsInactiveApp()?CheckWhen_RunTimeInactive:CheckWhen_RunTimeActive);
	FolderCacheDoCallbacks();
	utilitiesLibOncePerFrame(gGCLState.frameElapsedTime, timeStepScale);
	gclTestClientOncePerFrame();
	gclScript_Tick();

	gclPatchStreamingProcess();

	// gfxOncePerFrame manages some things that need to be run every
	// frame even if graphics are off, such as the FPS timer locks and
	// making sure the string memory cache doesn't get too large. It handles
	// gbNoGraphics itself, so it should be called here.
	gfxOncePerFrame(gGCLState.frameElapsedTime, gGCLState.frameElapsedTimeReal, emIsEditorActive() || ugcEditorIsActive(), true);
	
	gclUGC_CacheOncePerFrame();

	if(!gbNoGraphics)
	{
		gclDisplayDemoRecording();
		gclDisplayTacticalTimes();
		aDebugOncePerFrame(gGCLState.frameElapsedTime);
		gDebugOncePerFrame();
		sndLibOncePerFrame(gGCLState.frameElapsedTime);

		if (SAFE_MEMBER(gGCLState.pPrimaryDevice, activecamera))
		{
			copyVec4(sClearColor, gGCLState.pPrimaryDevice->activecamera->clear_color);
		}

		hv_Update();
	}

	if(GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING) || GSM_IsStateActive(GCL_LOGIN_NEW_CHARACTER_CREATION) || GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_CHARACTER))
	{
		CharacterCreation_ManageFreeCharacterPathList();
	}
	cmdParseOncePerFrame();
	gclXboxTick();
	gclUpdateControllerConnection();
	resExecuteAllPendingCommands();
	gclNotifyUpdate(gGCLState.frameElapsedTimeReal);
}

AUTO_CMD_INT(gNetGraph,netgraph) ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(gBitGraph,bitgraph) ACMD_HIDE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(gMinimalRendering,minimalrendering) ACMD_COMMANDLINE;

//allows test client connections
AUTO_CMD_INT(gAllowTestClients,allow_testclients) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

//if true, the "test client" is actually the controller
AUTO_CMD_INT(gTestClientIsController, TestClientIsController) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// if true and MakeBinsAndExit is set, write a memory dump between each map binning
AUTO_CMD_INT(gMakeMemdumpBetweenMaps, WriteMemdumpBetweenMaps) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// if true and MakeBinsAndExit is set, validate the fast dependency checker before binning
AUTO_CMD_INT(gValidateDepCheck, ValidateDependencyCheck) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// Print tactical timers (run and roll).
AUTO_CMD_INT(gPrintTacticalTimes, printTacticalTimes);

//makes all bins, then exits
AUTO_COMMAND ACMD_CMDLINE;
void MakeBinsAndExit(bool bSet)
{
	gbMakeBinsAndExit = bSet;
	stringCacheDisableWarnings();

	if (!WorldGrid_DoingMultiplexedMakeBinsAsSlave())
	{
		ParserForceBinCreation(bSet);
		gfxSetCompileAllShaderTypes(CompileShaderType_PC); // just PC and xbox for now
	}
	
}

bool gclGetDrawWorldThisFrame(bool reset)
{
	bool ret = gGCLState.bDrawWorldThisFrame;
	gGCLState.bDrawWorldThisFrame = true;
	return ret;
}

extern WorldRegion *gcl_camera_last_region;

static bool gclNeedLoading(GfxCameraController *active_camera)
{
	static Vec3 last_cam_pos = {0,0,0};
	WorldRegion *cur_region;
	Vec3 cur_cam_pos;
	bool ret = false;

	copyVec3(active_camera->camfocus, cur_cam_pos);

	cur_region = worldGetWorldRegionByPos(cur_cam_pos);
	if (gcl_camera_last_region && gcl_camera_last_region != cur_region)// || distance3(cur_cam_pos, last_cam_pos) > 256.f)
		ret = true;

	copyVec3(cur_cam_pos, last_cam_pos);
	gcl_camera_last_region = cur_region;
	
	return ret;
}

static void gclDisplayLinkWarning(void){
    if( !gclHasReturnedServerAddress() &&
		gclServerTimeSinceRecv() > 5.f)
    {
	    int width, height;
	    char message[100];

	    gfxfont_SetFontEx(&g_font_Sans, 0, 1, 1, 0, 0xBB0000FF, 0xBB0000FF);
	    gfxGetActiveSurfaceSize(&width, &height);
	    
		sprintf(message, "SERVER NOT RESPONDING (%1.2fs)", gclServerTimeSinceRecv());

	    gfxfont_Printf(	width / 2,
						100,
						100,
						1,
						1,
						CENTER_X,
						"%s",
						message);
    }
}

static void gclDisplayEntityDebugFlags(void){
	Entity* ePlayer;
	
	if(!showDevUIProd()){
		return;
	}
	
	ePlayer = entActivePlayerPtr();

	if( ePlayer &&
		GameClientAccessLevel(ePlayer, 0)>ACCESS_UGC &&
		!demo_playingBack())
	{
		int width, height, y = 115;
		int i;
		gfxfont_SetFontEx(&g_font_Sans, 0, 1, 0, 0, 0xBB00BBFF, 0xBB0000FF);
		gfxGetActiveSurfaceSize(&width, &height);

		if(entCheckFlag(ePlayer, ENTITYFLAG_UNTARGETABLE))
		{
			gfxfont_Printf(width / 2, y, 100, 1, 1, CENTER_X, "YOU ARE UNTARGETABLE");
			y += 15;
		}
		if(entCheckFlag(ePlayer, ENTITYFLAG_UNSELECTABLE))
		{
			gfxfont_Printf(width / 2, y, 100, 1, 1, CENTER_X, "YOU ARE UNSELECTABLE");
			y += 15;
		}
		if(entCheckFlag(ePlayer, ENTITYFLAG_DONOTSEND))
		{
			gfxfont_Printf(width / 2, y, 100, 1, 1, CENTER_X, "YOU ARE INVISIBLE (DONOTSEND)");
			y += 15;
		}
		if(entCheckFlag(ePlayer, ENTITYFLAG_DONOTDRAW))
		{
			gfxfont_Printf(width / 2, y, 100, 1, 1, CENTER_X, "YOU ARE INVISIBLE (DONOTDRAW)");
			y += 15;
		}

		for(i=0; i<=curTimer; i++)
		{
			gfxfont_Printf(width / 2, y, 100, 1, 1, CENTER_X, "Time: %.2f", ABS_TIME_TO_SEC(ABS_TIME_SINCE(timeArray[i])));
			y += 15;
		}
	}
}

void gclDisplayHeadshotDebug(void){
	Entity* ePlayer = entActivePlayerPtr();

	if(ePlayer){
		// headshot debug stuff
		gfxHeadshotDebugOncePerFrame(	costumeEntity_GetWLCostume( ePlayer ),
										gGCLState.frameElapsedTimeReal );
	}
}

static void gclLogCameraAndDrawMovementDebug(void){
	const GfxCameraController* c = gGCLState.pPrimaryDevice->activecamera;

	if(c){
		gclMovementDebugDraw3D(c->last_camera_matrix);

		
		if(globMovementLogIsEnabled){
			if(c->last_view){
				globMovementLogCamera(	"camera.lastView_newFrustum",
										c->last_view->new_frustum.cammat);

				globMovementLogCamera(	"camera.lastView_frustum",
										c->last_view->frustum.cammat);
			}

			globMovementLogCamera(	"camera.last_camera",
									c->last_camera_matrix);
		}
	}
}

static void gclDisplayNetGraph(	const NetLink* link,
								int netgraph_type)
{
	int		i,xpos;
	F32		height;
	int		w, h;
	const PacketHistory *hist;
	const LinkStats *stats = linkStats(link);

	if (!stats)
		return;

	gfxGetActiveSurfaceSize(&w, &h);

	for(i=0;i<ARRAY_SIZE(stats->history);i++)
	{
		int argb;

		hist = &stats->history[i];

		if (netgraph_type & 1)
		{
			height = hist->elapsed * 40;
			if (height > 40)
				height = 40;
		}
		else
		{
			height = hist->curr_real_recv / 10;
			if (height <= 0)
				height = 1;
		}

		argb = 0xff00ff00;

		xpos = ARRAY_SIZE(stats->history) - (ARRAY_SIZE(stats->history) - 1 + i - stats->last_recv_idx) % ARRAY_SIZE(stats->history);

		gfxDrawLineARGB(w - xpos, h, 2000, w - xpos + 1, h - height, argb);
	}
	hist = &stats->history[stats->last_recv_idx];
	gfxXYprintf(10,TEXT_JUSTIFY+52,"PING: %d",(int)(hist->elapsed * 1000));
	gfxXYprintf(10,TEXT_JUSTIFY+53,"SEND: %-5d  (%d unpacked)",hist->sent_bytes,hist->sent_bytes);
	gfxXYprintf(10,TEXT_JUSTIFY+54,"RECV: %-5d  (%d unpacked)",hist->curr_real_recv,hist->curr_recv);

	if (netgraph_type > 10)
	{
		printf("id %d   ping %d   sent ?/%d  recv %d/%d\n",hist->real_id,(int)(hist->elapsed * 1000),
			hist->sent_bytes,hist->curr_real_recv,hist->curr_recv);
	}
}

extern unsigned int g_packetsizes_one[33];
extern unsigned int g_packetsizes_success[33];
extern unsigned int g_packetsizes_failed[33];

static void showPackedArray(unsigned int array[33], int off, char* name)
{
	int i, max = 0;
	char buf[20];
	F32 height, height2;

	gfxXYprintfColor(0, off, 0, 255, 0, 255, "%s", name);
	for (i = 0; i < 33; i++)
		if (array[i] > (U32)max) max = array[i];
	for (i = 0; i < 33; i++)
	{
		sprintf(buf, "%i", i);
		height = 100.0 * (F32)array[i] / max;
		height2 = height * i;
		if (height2 > 100.0)
			height2 = 100.0;

		gfxDrawQuadARGB(i*10, off+10, i*10+3, height+off+10, 1, 0xff00ff00);
		if (height2 > height)
			gfxDrawQuadARGB(i*10, height+off+10, i*10+3, height2+off+10, 1, 0xffffff00);
		if (i % 2)
			gfxXYprintfColor(i*10-3, off, 0, 255, 0, 255, "%s", buf);
	}
}

static void gclDisplayBitGraph(void)
{
	showPackedArray(g_packetsizes_success, 0, "success");
	showPackedArray(g_packetsizes_failed, 120, "failure");
	showPackedArray(g_packetsizes_one, 240, "pack(1)");
}

static void print(S32 x, S32 y, U32 argb, FORMAT_STR const char* format, ...)
{
	char buffer[1000];
	
	VA_START(va, format);
	vsprintf(buffer, format, va);
	VA_END();
	
	gfxXYprintfColor(	x,
						y,
						(argb & 0xff0000) >> 16,
						(argb & 0xff00) >> 8,
						argb & 0xff,
						(argb & 0xff000000) >> 24,
						"%s",
						buffer);
}

static void gclDisplayServerConnInfo(	const GCLServerConnectionInfo* info,
										const char* title,
										S32 x,
										S32 y,
										S32 argb,
										S32 forceDisconnectError)
{
	char disconnectMsg[100];

	if(!info){
		return;
	}

	disconnectMsg[0] = 0;

	print(	x, y++, argb,
			"%s:",
			title);

	print(	x + 1, y++, argb,
			"ip/port:    %s:%d",
			makeIpStr(info->ip), info->port);

	print(	x + 1, y++, argb,
			"bytes sent: %s / %s",
			getCommaSeparatedInt(info->bytes.sent.compressed),
			getCommaSeparatedInt(info->bytes.sent.uncompressed));
				
	print(	x + 1, y++, argb,
			"bytes recv: %s / %s",
			getCommaSeparatedInt(info->bytes.received.compressed),
			getCommaSeparatedInt(info->bytes.received.uncompressed));

	if(info->disconnectErrorCode){
		sprintf(disconnectMsg,
				"%d (%s)",
				info->disconnectErrorCode,
				sockGetReadableError(info->disconnectErrorCode));
	}
	else if(forceDisconnectError){
		strcpy(disconnectMsg, "normal");
	}
	
	if(disconnectMsg[0]){
		print(	x + 1, y++, argb,
				"disconnect: %s%s",
				disconnectMsg,
				info->flags.disconnectedFromClient ? " (client forced disconnect)" : "");
	}
}

static int netDebugEnabled;
AUTO_CMD_INT(netDebugEnabled, netDebug) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE;

static void gclDisplayNetDebug(void)
{
	const GCLServerConnectionInfo* infoCur = NULL;
	const GCLServerConnectionInfo* infoPrev = NULL;
	
	gclServerGetConnectionInfo(&infoCur, &infoPrev);

	if(	infoCur ||
		infoPrev)
	{
		U32 x = 0;
		U32 width = 450;
		U32 y = 120;
		U32 height = 180;
		gfxDrawQuadARGB(x, y, x + width, y + height, 10000, 0xa0000000);
		gclDisplayServerConnInfo(infoCur, "Current Server", 2, 12, 0xff40ff40, 0);
		gclDisplayServerConnInfo(infoPrev, "Previous Server", 2, 18, 0xffff4040, 1);
	}
}

F32 gclGetWorldCellScale(Vec3 pos)
{
	RegionRules *rules = RegionRulesFromVec3NoOverride(pos);
	F32 scale = 1;

	if(rules)
		scale = rules->fWorldCellDistanceScale;

	return scale;
}

void gclBase_EndFrame(void)
{
	bool bDrawWorld = (GSM_IsStateActive(GCL_GAMEPLAY) || gclGetDrawWorldThisFrame(true)) && !gGCLState.bHideWorld && !emEditorHidingWorld();
	bool bLoading = GSM_IsStateActive(GCL_LOADING);
	bool bZOcclusion = bDrawWorld && !bLoading;

	if ( !gbNoGraphics ) 
	{
		PERFINFO_AUTO_START_PIX("Primary Device Drawing",1);

		gfxSetActiveDevice(gGCLState.pPrimaryDevice->device);	
		inpUpdateEarly(gfxGetActiveInputDevice());
	}
	else
	{
		gfxSetActiveCameraView(gGCLState.pPrimaryDevice->activecamera->last_view, true);
		gfxSetActiveCameraController(gGCLState.pPrimaryDevice->activecamera, true);
	}

	if ( gbNoGraphics )
		emSetActiveCamera(gGCLState.pPrimaryDevice->activecamera, gGCLState.pPrimaryDevice->overrideCameraView);

	if ( !gbNoGraphics ) 
	{
		gfxDisplayDebugInterface2D(emIsEditorActive() || ugcEditorIsActive());

		gfxCheckForMakeCubeMap();
	}

	gclExternOncePerFrame(gGCLState.frameElapsedTime, bDrawWorld);
	gclPetUI_OncePerFrame();

	CostumeOnly_OncePerFrame();
	Costume_OncePerFrame();

	ItemUpgradeUI_Update(entActivePlayerPtr());

	gclPVP_Tick();

	if( !gbNoGraphics )
	{
		emRunQueuedFunctions();

		emSetActiveCamera(gGCLState.pPrimaryDevice->activecamera, gGCLState.pPrimaryDevice->overrideCameraView);

		gclCombatDebugMeters_Tick();

		if( !gMinimalRendering ) {
			wlPerfStartUIBudget();
			g_ui_State.bEarlyZOcclusion = bZOcclusion; // Set here on UI state rather than passed through
			ui_OncePerFramePerDevice(gGCLState.frameElapsedTimeReal, gGCLState.pPrimaryDevice->device);
			wlPerfEndUIBudget();
		}

		if (emEditorMain() || utilitiesLibShouldQuit())
		{
			GSM_Quit("emEditorMain or utilitiesLibShouldQuit()");
		}

		// This stuff now lives in ui_OncePerFramePerDevice(), but we still want to
		//  call it if that doesn't get called.
		if(gMinimalRendering)
		{
			inpUpdateLate(gfxGetActiveInputDevice());

			// Needs to run after ui_OncePerFramePerDevice so the UI can intercept
			// input events related to the camera.
			// Also needs to run after inpUpdateLate so keybind-based camera input
			// can affect the camera in the same frame.
			gfxRunActiveCameraController(-1, NULL);

			if(bZOcclusion)
				gfxStartEarlyZOcclusionTest();
		}

		if (gclNeedLoading(gGCLState.pPrimaryDevice->activecamera))
		{
			globCmdParse("gfxUnloadAllNotUsedThisFrame 1");
			// TODO switch to loading state?
		}

		gfxTellWorldLibCameraPosition(); // Call this only on the primary camera


		// moved gclExternOncePerFrame above ui_OncePerFramePerDevice because it was creating problems: 
		// gens were being updated before gclInteract_Tick which caused entity data to be corrupted
		//gclExternOncePerFrame(gGCLState.frameElapsedTime, bDrawWorld);

		if (gNetGraph){
			gclDisplayNetGraph(gServerLink, gNetGraph);
		}
		
		if (gBitGraph){
			gclDisplayBitGraph();
		}
			
		if(netDebugEnabled){
			gclDisplayNetDebug();
		}

		gclDisplayLinkWarning();
		gclDisplayEntityDebugFlags();
		gclDisplayHeadshotDebug();

		gfxStartMainFrameAction(emIsEditorActive(), bZOcclusion, !bDrawWorld, bLoading, true);

		if (!gMinimalRendering && !gfxIsHookedFrame()) {
			if (!bDrawWorld && gGCLState.pPrimaryDevice->activecamera)
			{
				// prevent the world from unloading things around the game camera while in the editor
				F32 scale = 1.0;
				WorldRegion *region = worldGetWorldRegionByPos(gGCLState.pPrimaryDevice->activecamera->camfocus);
				RegionRules *rules = RegionRulesFromVec3NoOverride(gGCLState.pPrimaryDevice->activecamera->camfocus);

				if(rules)
					scale = rules->fWorldCellDistanceScale;

				worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, region, gGCLState.pPrimaryDevice->activecamera->last_camera_matrix[3], 1, NULL, false, false, gfxGetDrawHighDetailSetting(), gfxGetDrawHighFillDetailSetting(), gfxGetFrameCount(), scale);
			}

			if (gGCLState.pPrimaryDevice->activecamera && (gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->cutscenecamera || gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->contactcamera))
			{
				// prevent the world from unloading things around the game camera while in a cutscene
				F32 scale = 1.0;
				WorldRegion *region = worldGetWorldRegionByPos(gGCLState.pPrimaryDevice->gamecamera.camfocus);
				RegionRules *rules = RegionRulesFromVec3NoOverride(gGCLState.pPrimaryDevice->gamecamera.camfocus);

				if(rules)
					scale = rules->fWorldCellDistanceScale;

				worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, region, gGCLState.pPrimaryDevice->gamecamera.last_camera_matrix[3], 1, NULL, false, false, gfxGetDrawHighDetailSetting(), gfxGetDrawHighFillDetailSetting(), gfxGetFrameCount(), scale);
			}

			{
				S32 i;
				for (i = 0; i < eaSize((void***)&s_eacbGhostDraw); i++)
					s_eacbGhostDraw[i]();
			}

			gclDebugDrawPrimitive_Update(gGCLState.frameElapsedTime);
			gclLogCameraAndDrawMovementDebug();
			gclSimpleCpu_DrawFrames();

			mmLogAllSkeletons();
			gDebugDraw();
			aDebugDraw();
			gclWorldDebugOncePerFrame();
			gclAIDebugOncePerFrame();
			editLibDrawGhosts();
			gfxFillDrawList(!noDrawWorld, gclGetWorldCellScale);

			emEditorDrawGhosts();

			gfxCheckPerRegionBudgets();
		}

		gclLDOncePerFrame();

		gfxDrawFrame();

		PERFINFO_AUTO_STOP_PIX();

		PERFINFO_AUTO_START("Aux Device Drawing",1);
			gfxRunAuxDevices();
		PERFINFO_AUTO_STOP();

		// end drawing

		gclProcessQueuedErrors(!gfxDelayingErrorf() && 
			(!g_iQuickLogin || GSM_IsStateActive(GCL_GAMEPLAY) || GSM_IsStateActive(GCL_DEMO_PLAYBACK)) && 
			!GSM_IsStateActive(GCL_PLAY_VIDEO));
	}
	else
	{
		// Graphics are disabled. However, the UI main loop still needs to run
		// once per frame in order to manage widget memory.
		wlPerfStartUIBudget();
		g_ui_State.bEarlyZOcclusion = bZOcclusion; // Set here on UI state rather than passed through
		ui_OncePerFramePerDevice(gGCLState.frameElapsedTimeReal, gGCLState.pPrimaryDevice->device);
		wlPerfEndUIBudget();

		gfxRunActiveCameraController(-1, NULL);

		if(!gGCLState.bSkipWorldUpdate)
		{
			Entity *pEnt = entActivePlayerPtr();
			Vec3 vPos;
			WorldRegion *region;

			if(pEnt)
			{
				F32 scale = 1.0;
				RegionRules *rules = NULL;
				entGetPos(pEnt, vPos);
				region = worldGetWorldRegionByPos(vPos);
				rules = RegionRulesFromVec3NoOverride(vPos);
				worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, region, vPos, 1, NULL, false, false, gfxGetDrawHighDetailSetting(), gfxGetDrawHighFillDetailSetting(), gfxGetFrameCount(), scale);
			}
		}
    }

	gclPaperdollFlushCostumeGenCache();
	gclInventoryOncePerFrame();

	gfxOncePerFrameEnd(true);

	gclServerMonitorConnection();

	gclTransformation_OncePerFrame();
	gclDeadBodies_OncePerFrame();
	gclCombatDeathPrediction_OncePerFrame();

	gclUtilsOncePerFrame();

	wlPerfEndMiscBudget();
}

static void gclExitAnimation(void)
{
}

void gclBase_Leave(void)
{
	if(!gbNoGraphics)
	{
		gfxSettingsSave(gGCLState.pPrimaryDevice->device);

		gclExitAnimation();

		gfxUnregisterDevice(gGCLState.pPrimaryDevice->device);
		rdrDestroyDevice(gGCLState.pPrimaryDevice->device);
		gGCLState.pPrimaryDevice->device = NULL;
	}
	
	sndShutdown();
	hv_Shutdown();
	worldLibShutdown();

	frameLockedTimerDestroy(&gGCLState.frameLockedTimer);
	gclEnableScreensaver();
}


/////-------------stuff relating to GCL_GAMEPLAY
static int siWorldUpdateTimer;
static int siPeriodicUpdateTimer;
static int siSettingsUpdateTimer;
static bool bMouseParticles = false;

AUTO_COMMAND;
void MouseParticles( bool enableFlag )
{
    if( !enableFlag ) {
        dynFxManStopUsingFxInfo( dynFxGetUiManager(false), "TestFade", TRUE );
    } else {
        if( enableFlag && !bMouseParticles && GSM_IsStateActive( GCL_GAMEPLAY )) {
            ui_MouseAttachFx( "TestFade", NULL );
        }
    }

    bMouseParticles = enableFlag;
}

static char*	runOnConnectString;
static char*	runOnDisconnectString;
static S32		doRunOnConnect;

//specifies a command to run on connection to a game server
AUTO_COMMAND ACMD_NAME(runOnConnect) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmdRunOnConnect(const char* str)
{
	estrCopy2(&runOnConnectString, str);
}

const char *gclGetRunOnConnectString(void)
{
	return runOnConnectString;
}

//specifies a command to run on disconnection from a game server
AUTO_COMMAND ACMD_NAME(runOnDisconnect) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);
void cmdRunOnDisconnect(const char* str)
{
	estrCopy2(&runOnDisconnectString, str);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
 void gclSendSpecsToServer(void)
{
	char tempbuf[2048];
	char *pEscaped = NULL;

	estrCopy2(&pEscaped, "clientSystemSpecsLog ");
	estrAppend2(&pEscaped, " \"");
	systemSpecsGetNameValuePairString(SAFESTR(tempbuf));
	estrAppendEscaped(&pEscaped, tempbuf);
	estrAppend2(&pEscaped, "\" ");
	cmdSendCmdClientToServer(pEscaped, true, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
}

static void gclLogPerformance(TimedCallback *callback_UNUSED, F32 seconds_UNUSED, UserData userData_UNUSED)
{
	char *pTemp = NULL;
	char *pEscaped = NULL;
	FrameCountsHist hist;
	static bool bSentSpecs=false;

	PERFINFO_AUTO_START_FUNC();
	
	if (!gclServerIsConnected())
	{
		bSentSpecs = false;
		PERFINFO_AUTO_STOP();
		return;
	}
	gfxGetFrameCountsHistAndReset(&hist);

	if (hist.invalid)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	estrStackCreateSize(&pTemp, 16*1024);
	estrStackCreateSize(&pEscaped, 16*1024);

	if (!bSentSpecs)
	{
		char tempbuf[2048];
		GfxSettings settings;
		estrCopy2(&pEscaped, "clientPerfLog SystemSpecs ");
		estrAppend2(&pEscaped, " \"");
		systemSpecsGetNameValuePairString(SAFESTR(tempbuf));
		estrAppendEscaped(&pEscaped, tempbuf);
		estrAppend2(&pEscaped, "\" ");
		cmdSendCmdClientToServer(pEscaped, true, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);

		estrCopy2(&pEscaped, "clientPerfLog GfxSettings ");
		estrAppend2(&pEscaped, " \"");
		gfxGetSettings(&settings);
		ParserWriteText(&pTemp, gfx_settings_parseinfo, &settings, 0, 0, 0);
		estrAppendEscaped(&pEscaped, pTemp);
		estrAppend2(&pEscaped, "\" ");
		cmdSendCmdClientToServer(pEscaped, true, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);

		bSentSpecs = true;
	}

	// Add the ip and ping.
	{
		const LinkStats* stats = linkStats(gServerLink);
		
		hist.ping = stats->history[stats->last_recv_idx].elapsed * 1000;
		hist.ip = gclServerMyIPOnTheServer();
	}

	// Make short version
	{
		FrameCountsHistReported hist2 = {0};
		GfxSettings settings_temp;
		gfxGetSettings(&settings_temp);
		hist2.fps = hist.avg.fps;
		hist2.ping = hist.ping;
		hist2.ip = hist.ip;
		hist2.rendererPerf = settings_temp.last_recorded_perf;
		ParserWriteText(&pTemp, parse_FrameCountsHistReported, &hist2, 0, 0, 0);
	}
	//ParserWriteText(&pTemp, parse_FrameCountsHist, &hist, 0, 0, 0);

	// log it!
	//ServerCmd_clientPerfLog(pTemp); // Was allocating heap memory - call manually instead
	estrCopy2(&pEscaped, "clientPerfLog PerfLog ");
	estrAppend2(&pEscaped, " \"");
	estrAppendEscaped(&pEscaped, pTemp);
	estrAppend2(&pEscaped, "\" ");

	cmdSendCmdClientToServer(pEscaped, true, CMD_CONTEXT_FLAG_ALL_STRINGS_ESCAPED, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);

	estrDestroy(&pTemp);
	estrDestroy(&pEscaped);

	PERFINFO_AUTO_STOP();
}

static TimedCallback *performance_logging_callback;
static void gclStartLoggingPerformance(void)
{
	if (!performance_logging_callback)
		performance_logging_callback = TimedCallback_Add(gclLogPerformance, NULL, 60.f);
}

static void gclStopLoggingPerformance(void)
{
	if (performance_logging_callback)
	{
		TimedCallback_Remove(performance_logging_callback);
		performance_logging_callback = NULL;
	}
}

void gclGameplay_InitCharacterOptions(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{	
		if (pEnt->pPlayer->pUI && pEnt->pPlayer->pUI->pSchemes)
		{
			schemes_initOptions(pEnt);
		}
		gclBasicOptions_InitPlayerOptions(pEnt);
	}
}

static void gclGameplay_HandleAutoHolster(Entity* e)
{
	bool bUnholster = false;
	if (gConf.bAutoUnholsterWeapons)
	{
		bUnholster = gclPlayerControl_ShouldAutoUnholster(false);
	}
	GameSpecific_HolsterRequest(e, NULL, bUnholster);
}

void gclGamePlay_Enter(void)
{
	Entity *pEnt = entActivePlayerPtr();
	int i;
	Packet *pak;

	GSM_ResetStateTimers(GCL_GAMEPLAY);

	siWorldUpdateTimer = timerAlloc();
	timerStart(siWorldUpdateTimer);
	siPeriodicUpdateTimer = timerAlloc();
	timerStart(siPeriodicUpdateTimer);
	siSettingsUpdateTimer = timerAlloc();
	timerStart(siSettingsUpdateTimer);

	if (isProductionMode())
	{
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->accountAccessLevel > 0)
		{
			setAssertMode(getAssertMode() & (~ASSERTMODE_NODEBUGBUTTONS));
		}
		else
		{
			setAssertMode(getAssertMode() | ASSERTMODE_NODEBUGBUTTONS);
		}
		if (pEnt && pEnt->pPlayer)
		{
			assertSetAccountName(pEnt->pPlayer->privateAccountName);
		}
	}

	editLibSetLink(&gServerLink);

	pak = gclServerCreateRealPacket(TOSERVER_DONE_LOADING);
	if(pak){
		pktSend(&pak);
	}

	sndSetVelCallback(gclSndVelCB);
	sndSetEntPositionCallback(gclSndGetEntPositionByRef);
	sndSetGetEntityNameCallback(gclSndGetEntNameByRef);
	sndSetEntityTalkingCallback(gclSndSetTalkingBit);
	sndSetAccountIDToEntRefCallback(gclSndAcctToEntref);
	sndSetVoiceVerifyCallback(gclSndVerifyVoice);
	sndSetActiveOpenMissionCallback(gclSndGetActiveOpenMission);
	sndSetPlayerInCombatCallback(gclSndPlayerInCombat);

	sndSetCameraMatCallback(gclSndCameraMatCB);
	sndSetPlayerExistsCallback(gclSndPlayerExistsCB);
	sndSetPlayerVoiceInfoCallback(gclSndPlayerVoiceCB);
	sndSetCutsceneActiveCallback(gclCutsceneActiveCB);
	sndSetPlayerMatCallback(gclSndGetPlayerMatCB);
	sndGamePlayEnter();
	if(areEditorsAllowed())
	{
		sndSetEditorActiveFunc(emIsEditorActive);
	}

	if(!gGCLState.bSkipWorldUpdate && !isProductionMode() && !isProductionEditMode())
	{
		ServerCmd_beaconIsCurrentMapUptoDateRemote();
	}

	gfxClearGraphicsData();

	gclCamera_Reset();

	for (i = 0; i < entNumLocalPlayers(); i++)
	{
		Entity *pPlayer = entPlayerPtr(i);
		if (pPlayer)
		{
			// Clear this a bit early on the client, so we can start moving around
			entClearCodeFlagBits(pPlayer,ENTITYFLAG_IGNORE);
			entClientRegionRules(pPlayer);
		}
	}

	if(demo_recording())
		demo_startRecording();

    if( bMouseParticles ) {
        ui_MouseAttachFx( "TestFade", NULL );
    }

	//timerRecordStart( "auto_profile" );

	gclQuickPlay_PushBinds();
	gclQuickPlay_ExecuteScripts();
	gclQuickPlay_Reset();

	if ((pEnt && entGetAccessLevel(pEnt) >= 9) || isDevelopmentMode())
		keybind_PushProfileName("DevelopmentKeyBinds");
		
	doRunOnConnect = 1;

	gclStartLoggingPerformance();

	gclGameplay_SetUpUI();
	gclBulletin_Init();

	ServerCmd_characterclasses_SendClassList();

	if(gConf.bUpdateCurrentMapMissions)
		mission_gclUpdateCurrentMapMissions(pEnt);

	schemes_SaveStoredSchemes();
	if (pEnt)
	{
		gclGameplay_InitCharacterOptions(pEnt);	
		gclGameplay_HandleAutoHolster(pEnt);
	}
	gclKeyBindFillFromEntity(pEnt);
	gclNotifySettingsFillFromEntity(pEnt);
#ifndef _XBOX 
	if ( !gclKeyBindIsEnabled() )
	{
		gclKeyBindEnable();
	}
#endif
	// this does some things that are redundant with the calls above, for NW, where it actually does things.  Needs to be cleaned up.
	gclKeyBindEnterGameplay();
	gclHUDOptionsEnable();
	gclChatOptionsEnable();

#if _XBOX
	gclVoiceChatOptionsEnable();
#endif

	gclInterior_GameplayEnter();
	gclGuild_GameplayEnter();
	gclSteam_PurchaseActive(false);
#if _PS3 && _DEBUG
    BindKeyCommand("0x3a", "ec me sethealth 100");
    BindKeyCommand("0x3b", "forward 1");
    BindKeyCommand("0x3c", "forward 0");
#endif

	InitSimpleCommandPort();

#ifndef NO_EDITORS
	if (isProductionEditMode())
	{
		ugcEditMode(1);
	}
#endif

#ifdef USE_CHATRELAY
	if (gclChatIsConnected())
		GServerCmd_crUserChangeLanguage(GLOBALTYPE_CHATRELAY, entGetLanguage(pEnt));
	else
		gclChatConnect_RetryImmediate();
#endif
}

// Called once per frame, when actually in full gameplay state
void gclGamePlay_BeginFrame(void)
{
	PerfInfoGuard* piGuard;

	if(	TRUE_THEN_RESET(doRunOnConnect) &&
		estrLength(&runOnConnectString))
	{
		globCmdParse(runOnConnectString);
	}

	// Update social services cache if we haven't already
	gclSocialRequestServiceCache();

	if (timerElapsed(siWorldUpdateTimer) > 0.5f)
	{
		timerStart(siWorldUpdateTimer);
		PERFINFO_AUTO_START_GUARD("worldUpdate", 1, &piGuard);
			gclServerRequestWorldUpdate(0);
			gclServerSendLockedUpdates();
		PERFINFO_AUTO_STOP_GUARD(&piGuard);
	}

	if (timerElapsed(siPeriodicUpdateTimer) > 10.f)
	{
		timerStart(siPeriodicUpdateTimer);
		gclPeriodicGameplayUpdate();
	}

	if (timerElapsed(siSettingsUpdateTimer) > 10.f)
	{
		timerStart(siSettingsUpdateTimer);
		gclSettingsUpdate();
	}

	// Updates client costumes if assets changed in this tick
	costumeEntity_TickCheckEntityCostumes();

	if (isDevelopmentMode())
	{
		if (gclHeadshotHasPendingCostumeScreenshots())
		{
			gclHeadshotAttemptSaveScreenshotsForCostumes();
		}
	}

	gclConnectedToGameServerOncePerFrame();
	gclGameplayUpdateUIVars();

	PERFINFO_AUTO_START("updatePlayerPosTrivia", 1);
		triviaPrintf("playerPos", "%s", gclGetDebugPosString());
	PERFINFO_AUTO_STOP();

	gfxCheckAutoFrameRateStabilizer();
	demo_RecordCamera();

#ifdef USE_CHATRELAY
	gclChatConnect_Tick();
#endif
}

void gclGamePlay_Leave(void)
{
	gclKeyBindExitGameplay();

	DirectionalIndicatorFX_Destroy();
	if(estrLength(&runOnDisconnectString)){
		globCmdParse(runOnDisconnectString);
	}
	keybind_PopProfileName("DevelopmentKeyBinds");

	gclStopLoggingPerformance();

    dynFxManStopUsingFxInfo( dynFxGetUiManager(false), "TestFade", TRUE );
	gclQuickPlay_PopBinds();
	gclGameplayClearUIVars();
	gclCutsceneEndOnClient(true);

	if ( !GSM_IsStateActiveOrPending(GCL_LOGIN) )
	{
		ServerCmd_schemes_SetDetails(schemes_UpdateCurrentStoredScheme());
	}

	sndSetVelCallback(NULL);  // Leaving these here just for matching gameplayenter
	sndSetCameraMatCallback(NULL);
	sndSetPlayerExistsCallback(NULL);
	sndSetPlayerVoiceInfoCallback(NULL);
	sndSetPlayerMatCallback(NULL);
	sndGamePlayLeave();

	if(demo_recording())
		demo_record_stop(NULL);

	gfxHeadshotReleaseAll(true);
	gclDeleteAllClientOnlyEntities();
	objDestroyAllContainers();	

	gclPlayerControl_Reset();
	gclScoreboard_ClearScores();

	entClearLocalPlayers();
	entityLibResetState();

	if (!utilitiesLibShouldQuit())
		worldLoadEmptyMap(); // Manually load empty map for now

	timerFree(siWorldUpdateTimer);
	timerFree(siPeriodicUpdateTimer);
	timerFree(siSettingsUpdateTimer);
	//Reset timestep scale
	wlTimeSetStepScaleDebug(wlTimeGetStepScaleDebug()); 
	wlTimeSetStepScaleGame(1.0f);

	triviaRemoveEntry("playerPos");
	triviaRemoveEntry("WorldGrid");

	resClientCancelAnyPendingRequests();
	gclGameplay_DestroyUI();
	AuctionUI_Reset();
	gclBulletin_DeInit();
	ui_SetCurrentDefaultCursor("Default");
	ui_SetCursorByName("Default");

	gclTurnOffAllControlBits();
	schemes_Cleanup();
	gclResetMouseLookFlags();

	// Reset the assert mode flags
	if (isProductionMode())
	{
		setProductionClientAssertMode();
		assertClearAccountName();
	}

	schemes_ClearLocal();
	clientTarget_ResetClientTarget();
	clientTarget_ResetClientFocusTarget();
	gclKeyBindClear();
#ifndef _XBOX 
	gclKeyBindDisable();
#endif	
	gclHUDOptionsDisable();
	gclChatOptionsDisable();
#if _XBOX
	gclVoiceChatOptionsDisable();
#endif

	gclSetProductionEdit(false);

	gclInterior_GameplayLeave();
	gclGuild_GameplayLeave();
	gclSteam_PurchaseActive(false);

	gclAIDebugGameplayLeave();
}

static void gclStateEnterLeaveCB(bool bEnter, const char *pchState)
{
	if ( !gbNoGraphics )
	{
		const char *pchCleanState = strStartsWith(pchState, "gcl") ? (pchState + 3) : pchState;
		char achJoystick[1000];
		KeyBindProfile *pProfile = keybind_FindProfile(pchCleanState);
		UIGenState eGenState = ui_GenGetState(pchCleanState);
		sprintf(achJoystick, "%sJoystick", pchCleanState);
		if (!pProfile)
			pProfile = keybind_FindProfile(pchState);
		if (pProfile)
		{
			if (bEnter)	
				keybind_PushProfile(pProfile);
			else
				keybind_PopProfile(pProfile);
		}
		if (bEnter)	
			keybind_PushProfileName(achJoystick);
		else
			keybind_PopProfileName(achJoystick);
		if (eGenState >= 0)
			ui_GenSetGlobalState(eGenState, bEnter);
	}
}

AUTO_RUN;
void GCL_InitStates(void)
{
	GSM_AddGlobalState(GCL_PATCHSTREAMING);
	GSM_AddGlobalStateCallbacks(GCL_PATCHSTREAMING, gcl_PatchStreamingEnter, NULL, gcl_PatchStreamingFrame, gcl_PatchStreamingLeave);

	GSM_AddGlobalState(GCL_INIT);
	GSM_AddGlobalStateCallbacks(GCL_INIT, gclInit_Enter, gclInit_BeginFrame, gclInit_EndFrame, NULL);

	GSM_AddGlobalState(GCL_BASE);
	GSM_AddGlobalStateCallbacks(GCL_BASE, gclBase_Enter, gclBase_BeginFrame, gclBase_EndFrame, gclBase_Leave);

	GSM_AddGlobalState(GCL_PLAY_VIDEO);
	GSM_AddGlobalStateCallbacks(GCL_PLAY_VIDEO, gclPlayVideo_Enter, gclPlayVideo_BeginFrame, gclPlayVideo_EndFrame, gclPlayVideo_Leave);

	GSM_AddGlobalState(GCL_GAMEPLAY);
	GSM_AddGlobalStateCallbacks(GCL_GAMEPLAY, gclGamePlay_Enter, gclGamePlay_BeginFrame, gclGameplay_UIOncePerFrame, gclGamePlay_Leave);

	GSM_AddGlobalState(GCL_DEMO_PLAYBACK);
	GSM_AddGlobalStateCallbacks(GCL_DEMO_PLAYBACK, gclDemoPlayback_Enter, gclDemoPlayback_BeginFrame, gclGameplay_UIOncePerFrame, gclDemoPlayback_Leave);

	GSM_AddGlobalState(GCL_GAME_MENU);

	GSM_SetIndividualStateEnterLeaveCB(gclStateEnterLeaveCB);
}

// From anywhere in the character creation / login process, go back.
// Where you go back to depends on where you are.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_NAME(Login_Back);
void Login_Back(void)
{
	if (GSM_IsStateActive(GCL_ACCOUNT_SERVER_LOGIN))
    {
		return;
    }
	else if (GSM_IsStateActive(GCL_LOGIN_SELECT_PLAYERTYPE))
    {
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_SERVER_LOGIN);
    }
	else if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_EXISTING))
	{
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_SERVER_LOGIN);
	}
	else if (GSM_IsStateActive(GCL_LOGIN_INVALID_DISPLAY_NAME))
	{
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_SERVER_LOGIN);
	}
	else if (GSM_IsStateActive(GCL_LOGIN_WAITING_FOR_GAMESERVER_LIST))
	{
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_ACCOUNT_SERVER_LOGIN);
	}
	else if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_GAMESERVER))
    {
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_CHARACTER);
    }
	else if (GSM_IsStateActive(GCL_LOGIN_NEW_CHARACTER_CREATION))
	{
		GSM_SwitchToState_Complex(GCL_LOGIN_USER_CHOOSING_CHARACTER "/" GCL_LOGIN_USER_CHOOSING_EXISTING);
	}
	else if (GSM_IsStateActive(GCL_LOGIN_USER_CHOOSING_UGC_PROJECT))
    {
		GSM_SwitchToState_Complex(GCL_LOGIN "/" GCL_LOGIN_USER_CHOOSING_CHARACTER);
    }
}

static void gclGameplay_SetUpUI(void)
{
#if _XBOX
	// Move the Xbox notification window so that it covers the minimap instead of
	// the in-world player.
	XNotifyPositionUI(XNOTIFYUI_POS_TOPRIGHT);
#endif

	// Clear flag to show away team picker or map transfer window
	teamHideMapTransferChoice(NULL);
}

AUTO_COMMAND;
void MouseTray(S32 bEnable)
{
#if !PLATFORM_CONSOLE
	if(bEnable)
	{
		globCmdParse("genaddwindow HUD_Traybox_AllPowers");
	}
	else
	{
		globCmdParse("genremovewindow HUD_Traybox_AllPowers");
	}
#endif

}

static void gclGameplay_DestroyUI(void)
{
	mouseLock(0);

	if (!gfxGetFullscreen())
	{
		ClipCursor(NULL);
	}

#if _XBOX
	XNotifyPositionUI(XNOTIFYUI_POS_BOTTOM);
#endif
}

static void gclGameplay_UIOncePerFrame(void)
{
	DirectionalIndicatorFX_OncePerFrame();
	if (gConf.bEnableGoldenPath)
		goldenPath_OncePerFrame();
	WaypointFXOncePerFrame();
	ChatLog_OncePerFrame();
	AuctionUI_OncePerFrame();
}

static void gclPostCameraUpdates_OncePerFrame(F32 fElapsedTime)
{
	gclReticle_OncePerFrame(fElapsedTime);
	gclCursorMode_OncePerFrame();
}


void DEFAULT_LATELINK_gclMakeAllOtherBins(void);
void OVERRIDE_LATELINK_gclMakeAllOtherBins(void)
{
	DEFAULT_LATELINK_gclMakeAllOtherBins();

	//add calls here which create bin files other than terrain bins and auto-at-startup-bins
	//(purely for makeBinsAndExit mode)
}

