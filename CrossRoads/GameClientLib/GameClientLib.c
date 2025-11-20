/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameClientLib.h"
#include "crypt.h"
#include "sysutil.h"
#include "logging.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "BitStream.h"
#include "SharedMemory.h"
#include <conio.h>
#include "WorldLib.h"
#include "inputLib.h"
#include "fileCache.h"
#include "RegistryReader.h"
#include "dynAnimInterface.h"
#include "dynFxInterface.h"
#include "GfxDebug.h"
#include "GfxSprite.h"
#include "version/AppRegCache.h"
#include "EntityLib.h"
#include "EditLib.h"
#include "utilitiesLib.h"
#include "gclCommandParse.h"
#include "gclSendToServer.h"
#include "gclEntity.h"
#include "gclTestCollision.h"
#include "WorldColl.h"
#include "EntityIterator.h"
#include "entdebugmenu.h"
#include "MemoryBudget.h"
#include "DirMonitor.h"
#include "RdrState.h"
#include "RdrShader.h"
#include "testclient_comm.h"
#include "gclControlScheme.h"
#include "gclCursorMode.h"
#include "gclDynDebug.h"
#include "trivia.h"
#include "RdrDevice.h"
#include "net/accountNet.h"
#include "Player.h"
#include "WorldVariable.h"
#include "EditorManager.h"
#include "gclBaseStates.h"
#include "gclutils.h"
#include "gclPatching.h"
#include "gclPatchStreaming.h"
#include "globalstatemachine.h"
#include "gclDemo.h"
#include "winutil.h"
#include "sock.h"
#include "gclMediaControl.h"
#include "worldgrid.h"
#include "CostumeCommonEntity.h"
#include "EntityClient.h"
#include "gclDialogBox.h"
#include "UIGen.h"
#include "gclMapState.h"
#include "gclLogin.h"
#include "Team.h"
#include "soundLib.h"
#include "wlPerf.h"
#include "UGCEditorMain.h"
#include "dynfxinfo.h"
#include "gclPlayerControl.h"
#include "mission_common.h"
#include "mechanics_common.h"

// Only needed for UGC HACK
#include "ResourceManager.h"
#include "CostumeCommonLoad.h"

#define	PHYSX_SRC_FOLDER "../../3rdparty"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/GameClientLib_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("SharedStringCache", BUDGET_FileSystem););
AUTO_RUN_ANON(memBudgetAddMapping("ClientSharedStringCache", BUDGET_FileSystem););

GameClientLibState gGCLState = {0};
ProjectGameClientConfig gProjectGameClientConfig = {0};
extern char gLocalHost[32];

AUTO_RUN_ANON(memBudgetSetConfigFile("client/budgets/BudgetsGameClient.txt"););

//hooks for XLive login
void (*XLive_login_loop_hook)(void) = NULL;
void (*XLive_login_exit_hook)(void) = NULL;
int (*XLive_login_check_hook)(void) = NULL;
#if !_XBOX
void (*XLive_login_init_hook)(void) = NULL;
#endif

#define CLIENT_PROD_STRINGCACHE_SIZE 1050*1024
#define CLIENT_DEV_STRINGCACHE_SIZE 1024*1024*2

__forceinline static int GetClientStringCacheSize(void)
{
#if PLATFORM_CONSOLE
	if (1)
#else
	if (!fileIsUsingDevData())
#endif
		return CLIENT_PROD_STRINGCACHE_SIZE;
	else
		return CLIENT_DEV_STRINGCACHE_SIZE;
}

AUTO_RUN_FIRST;
int InitializeStringCache(void)
{
	stringCacheSetInitialSize(GetClientStringCacheSize());

#if !_PS3
	g_ccase_string_cache = true;
#endif
	return 1;
}

AUTO_RUN_SECOND;
int InitializeStringCacheTestClient(void)
{
	if (strstri(GetCommandLine(), "nographics")	&& !strstri(GetCommandLine(), "NoSharedMemory"))
	{
		//ABW changed this because Ben Z told me to... client and server seemed to be stomping 
		//each others' string caches.
		stringCacheInitializeShared("ClientSharedStringCache", 1024*1024*50, GetClientStringCacheSize(), true);
#if !_PS3
		g_ccase_string_cache = true;
#endif
	}																				
	return 1;
}

AUTO_RUN_EARLY;
void enableMemBudget(void)
{
	memBudgetSetRequirement(MemBudget_Required_PopupForProgrammers);
}

// AUTO_RUN_EARLY;
void disallowLoadingServerData(void)
{
	FolderCacheAddIgnore("server"); // Do not allow or waste time loading anything from the server/ folder
}

AUTO_RUN_EARLY;
void disallowLoadingMiscData(void)
{
	if (fileIsUsingDevData()) // these things don't exist in production anyway
	{
		extern bool gbProductionModeBins;
#if PLATFORM_CONSOLE
		FolderCacheAddIgnore("server"); // Do not allow or waste time loading anything from the server/ folder
		if (gbProductionModeBins) // must be in development running with -productionmodebins
		{
			FolderCacheAddIgnore("defs"); // Should be all binned
			FolderCacheAddIgnore("dyn"); // Should be all binned
		}
#else
		if (gbProductionModeBins) // must be in development running with -productionmodebins
		{
			FolderCacheAddIgnore("server"); // Do not allow or waste time loading anything from the server/ folder
			FolderCacheAddIgnore("defs"); // Should be all binned
			FolderCacheAddIgnore("dyn"); // Should be all binned
		}
#endif
		FolderCacheAddIgnoreExt(".timestamp"); // Just needed by GetVRML
	}
}

AUTO_RUN_EARLY;
void setGraphicsSettingsForGameClient(void)
{
	gfxInitGlobalSettingsForGameClient();
}

AUTO_RUN;
void disableShaderProfilingToStopCrash(void)
{
	rdr_state.disableShaderProfiling = 1;
}

AUTO_RUN;
void initGCLState(void)
{
	// Don't do this, it happens after early command line parsing!  memset(&gGCLState, 0, sizeof(GameClientLibState));
	gGCLState.gameServerPort = STARTING_GAMESERVER_PORT;

	// Needs to happen before the log thread is created
	if (isProductionMode())
		dontLogErrors(true);
	if(isProductionMode())
		logSetMsgQueueSize(8192);
	else
		logSetMsgQueueSize(65536);
}

//////////////////////////////////////////////////////////////////////////

static void clientProductionCrashCallback(char *errMsg)
{
	// This is called in production when the client crashes.  Set a field in the registry to tell it to re-verify all files
	registryWriteInt(regGetAppKey(), "VerifyOnNextUpdate", errorIsDuringDataLoadingGet()?2:1);
	// Hide the client window so we can see the crash dialog
	gclHideWindowOnCrash();
}

int GameClientGetAcessLevel(void)
{
	return GameClientAccessLevel(entActivePlayerPtr(), 0);
}

int GameClientGetAcessLevel_IgnoreUgcModifications(void)
{
	return GameClientAccessLevel(entActivePlayerPtr(), IGNORE_UGC_MODIFICATIONS);
}

int GameClientGetAcessLevel_ZeroIfNoEntityOrDemo(void)
{
	if (entActivePlayerPtr() && !demo_playingBack() )
	{
		return GameClientAccessLevel(entActivePlayerPtr(), IGNORE_UGC_MODIFICATIONS);
	}

	return 0;
}

bool GameClientIsInTailor(void)
{
	static char *state = NULL;
	if (ui_GenInGlobalState(ui_GenGetState("State_Tailor_Main")))
		return true;
	if (ui_GenInGlobalState(ui_GenGetState("CharacterMenuTailor")))
		return true;

	GSM_PutFullStateStackIntoEString(&state);
	return !!strstri(state, "character");
}

void gclCreateDeviceAndShowLogo(const void *logo_jpg_data, int logo_data_size)
{
	bool bDisplayProgressBarAndLogos = true;

	//if (!gGCLState.bMakeBinsAndExit) // Need to create a device while making bins because we compile shaders now
	{
		if (!gbNoGraphics)
		{

			gclCheckSystemSpecsEarly();

			// Two chances, in case the machine was locked when the client started.
			if (!gclCreatePrimaryDevice(gGCLState.hInstance) &&
				!gclCreatePrimaryDevice(gGCLState.hInstance))
			{
				triviaPrintf("CrashWasVideoRelated", "1");
				FatalErrorf("Unable to create DirectX render device!  One of the following may help you resolve this issue:  restart your computer, ensure your video drivers are up to date, contact technical support.");
			}

			gclCheckSystemSpecsLate();

#if _XBOX
			// Do not display the logo and progress bar in patcher mode
			bDisplayProgressBarAndLogos = !gGCLState.bXBoxPatchModeEnabled;
#endif

			if (bDisplayProgressBarAndLogos)
			{
				loadstart_printf("Displaying Logo/splash screen...");
#if _PS3
				{
					int i;
					for(i=0; i<4; i++)
						gfxDisplayLogoProgress(gGCLState.pPrimaryDevice->device, logo_jpg_data, logo_data_size, "Cryptic Studios", -1, 0, false);
				}
#else
				gfxDisplayLogoProgress(gGCLState.pPrimaryDevice->device, logo_jpg_data, logo_data_size, NULL, -1, 0, false);
#endif
				loadend_printf(" done.");
			}
		}
		else
		{
			gclCreateDummyPrimaryDevice(gGCLState.hInstance);
#if _PS3
			gclRegisterPrimaryInputDevice(gGCLState.hInstance);
#endif
			gfxDebugDisableAccessLevelWarnings(true);
		}

		//gfxDisplayLogoProgress(NULL,NULL,0,NULL,0,0,true); // free logo resources
	}
}

// Search for (FIND_DATA_DIRS_TO_CACHE) to find a place 
// you can uncomment a line to verify if this is the correct list of folders to cache
const char* clientPrecachePaths[] = {
	"animation_library",
	"autobinds",
	"bin",
	"character_library",
	"defs",
	"dyn",
	"environment",		// environment/skies, environment/LodTemplates, environment/water
	"fonts",
	"genesis",
	"keybinds",
	"maps",
	"materials",
	"messages",
	"object_library",
	"powerart",
	"shaders",
	"sound",			// sound/win32, sound/dsps
	"texts",
	"texture_library",
	"ui",
	"world"				// world/PhysicalProperties
};

// Search for (FIND_HOGGS_TO_IGNORE) to find related code
// Most hoggs
const char* clientHoggIgnores[] = {
	"ns.hogg",
	"server.hogg"
};

bool gclPreMain(const char* pcAppName, const void *logo_jpg_data, int logo_data_size)
{
#define LOADSTART_TAG "gclPreMain initial..."
	loadstart_report_unaccounted(true);
	//setLoadTimingPrecistion(5);
	//loadstart_printf(LOADSTART_TAG); // Was 0 seconds, disabled

	// First init's
	cmdSetGlobalCmdParseFunc(GameClientParseActive);
	gfxSetGetAccessLevelFunc(GameClientGetAcessLevel_IgnoreUgcModifications);
	gfxSetGetAccessLevelForDisplayFunc(GameClientGetAcessLevel_ZeroIfNoEntityOrDemo);
	gfxSetIsInTailorFunc(GameClientIsInTailor);

#if !PLATFORM_CONSOLE
	if (!isProductionMode())
		console(1);
	//printf(LOADSTART_TAG); // Because we didn't have a console window when it was first printed
	showConsoleWindow();
	preloadDLLs(0);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'C', 0x00ff00);

	if (0) { // Fixes were made elsewhere so a production mode client can survive this if it manages to install, also this would need to be translated
		char cwd[MAX_PATH];
		if (strlen(fileGetcwd(SAFESTR(cwd))) > 100)
		{
			// Installation path is very long, will probably crash!
			errorDialog(NULL, "Warning: installation path is very long, some path names may exceed maximum path length limits and may cause crashes.  If you experience frequent crashes, please re-install to a different folder.",
				"Invalid Installation Path", NULL, 0);
		}
	}
#endif

	winCheckAccessibilityShortcuts(CheckWhen_Startup);

	if (isDevelopmentMode()) {
		// Search for (FIND_HOGGS_TO_IGNORE) to find related code
		FolderCacheAddIgnores(clientHoggIgnores, ARRAY_SIZE(clientHoggIgnores));
	}

	if (!regAppNameSet())
		regSetAppName(pcAppName);
	FolderCacheChooseMode();

	dirMonSetBufferSize(NULL, (gbNoGraphics || isProductionMode())?4*1024:512*1024);

	//loadend_printf(" done."); // Was 0 seconds, disabled

#if PLATFORM_CONSOLE // Win32 does this later after acquiring default settings
	loadstart_printf("Displaying logo...");
	gclCreateDeviceAndShowLogo(logo_jpg_data, logo_data_size);
	loadend_printf(" done.");
#endif
	g_do_not_try_to_load_folder_cache = false; // Okay to access filesystem now, logo has been displayed

	loadstart_printf("FileSystem startup...");
	fileLoadGameDataDirAndPiggs();
	if (!checkForRequiredClientFiles())
		return false;
	loadend_printf(" done.");

	if (giMakeOneLocaleOPFilesAndExit)
	{
		msgRemoveServerOnlyFlagsFromMsgTPI();
		msgLoadAllMessages();
		assert(0); //if we hit this, something is weird because we shoudl have called exit() inside msgLoadAllMessages();
	}

	// Search for (FIND_DATA_DIRS_TO_CACHE) to find places related to this caching 
	loadstart_printf("Caching folders...");
	fileCacheDirectories(clientPrecachePaths, ARRAY_SIZE(clientPrecachePaths));
	loadend_printf(" done.");

	// !!!: Disabled pending further investigation of performance issues with large numbers of namespaces in StarTrek <NPK 2010-06-02>
	//if (isDevelopmentMode())
	//{
	//	loadstart_printf("Loading user namespaces...");
	//	fileLoadAllUserNamespaces(0);
	//	loadend_printf(" done.");
	//}

	ErrorfSetCallback(gclErrorfCallback, NULL);
	FatalErrorfSetCallback(gclFatalErrorfCallback, NULL);

	logSetDir("GameClient");


	memBudgetStartup();

	//FolderCacheEnableCallbacks(0);
	fileCacheFreeze(true);
	FolderCacheSetManualCallbackMode(1);
	FolderCacheStartMonitorThread();
	FolderCacheDoNotWarnOnOverruns(true);
	bsAssertOnErrors(true);

	if (isProductionMode())
	{
		setProductionClientAssertMode();
		setAssertCallback(clientProductionCrashCallback);
	}
	else
		setDefaultAssertMode();
	if (isProductionMode() && !gGCLState.bAllowSharedMemory)
		sharedMemorySetMode(SMM_DISABLED);

	errorLogStart();
	cryptAdler32Init();
	return true;
}

char * g_CmdLine = NULL;


#if DEBUG_CRC
static U32 test_crc;
extern void (*pUpdateCRCFromParseInfoCB)(ParseTable pti[]);
static U32 id_UpdateCRCFromParseInfo;
U32 *cryptAdler32GetPtr();

static void UpdateCRCFromParseInfoCB(ParseTable pti[]) {
    if(
        1
        //!(id_UpdateCRCFromParseInfo&0x0f) && 
        //id_UpdateCRCFromParseInfo>0x00001200 && id_UpdateCRCFromParseInfo<0x00001380
    ) {
        U32 *p = cryptAdler32GetPtr();
        U32 v = *p;
        static U32 oldv;
        if(oldv != v) {
            oldv = v;
#if _PS3
            v = (v>>16)|(v<<16);
#endif
            if(!(id_UpdateCRCFromParseInfo&0xff))
            printf("%08x %08x\n", id_UpdateCRCFromParseInfo, v);
        }
    }
    id_UpdateCRCFromParseInfo++;
}
#endif

static int load_screen_timer=0;

void gclLoadingDone(void)
{
	if (load_screen_timer)
	{
		timerFree(load_screen_timer);
		load_screen_timer = 0;
		// Make sure logo is freed
		gfxDisplayLogoProgress(NULL,NULL,0,NULL,-1,0,true);
	}
}

//#define DEBUG_LOADSCREEN
static int loadend_call_count;
void gclLoadEndCallbackInternal(bool is_fake, int fake_count)
{
	bool bDisplayLogoProgress = true;
#if _XBOX
	bDisplayLogoProgress = !gGCLState.bXBoxPatchModeEnabled;
#endif

	if (load_screen_timer && gGCLState.pPrimaryDevice && gGCLState.pPrimaryDevice->device && !gGCLState.pPrimaryDevice->device->is_locked_nonthread && !gbMakeBinsAndExit)
	{
		static bool bSwitched=false;
		static bool bWasFake=false;
		static int fake_start;
		int max_count = (isProductionMode()?400:850) * (errorGetVerboseLevel()?2:1);
		int switch_count = (int)(max_count * 0.35f);
		F32 ratio;

		if (!bWasFake && is_fake)
		{
			fake_start = loadend_call_count;
			bWasFake = true;
		}
		if (bWasFake && !is_fake)
			bWasFake = false;
		if (!is_fake)
			loadend_call_count++;
		if (is_fake && fake_count >=0)
			loadend_call_count = fake_start + fake_count;
		if (loadend_call_count > max_count) {
			loadend_call_count = 0;
			printfColor(COLOR_RED|COLOR_BRIGHT, "client loadend_printf count exceeds current estimation, please increase max_count at %s(%d)\n", __FILE__, __LINE__-17);
		}

#ifndef DEBUG_LOADSCREEN
		if (timerElapsed(load_screen_timer) < 0.030)
			return; // Only once per 30fps
#endif
		timerStart(load_screen_timer);
		ratio = loadend_call_count / (float)max_count;

		// ratio = -1; // This would hide the progress bar

#ifdef DEBUG_LOADSCREEN
		bSwitched = false; // reload image in every callback to force worst case
		switch_count = 0;
#endif
		if (bDisplayLogoProgress)
		{
			if (!bSwitched && loadend_call_count > switch_count)
			{
				int logo_data_size;
				// TODO: scan this directory and display all contents in sequence at vaguely regular intervals
				void *logo_jpg_data = fileAlloc("texture_library/UI/StartupScreens/secondary_logo.jpg", &logo_data_size);
				if (logo_jpg_data)
				{
					assert(logo_jpg_data);
					gfxDisplayLogoProgress(NULL,NULL,0,NULL,-1,0,false); // Free old logo, but not loading image
					gfxDisplayLogoProgress(gGCLState.pPrimaryDevice->device, logo_jpg_data, logo_data_size, NULL, ratio, 0.5, true);
					fileFree(logo_jpg_data);
				}
				bSwitched = true;
			} else {
				gfxDisplayLogoProgress(gGCLState.pPrimaryDevice->device, NULL, 0, NULL, ratio, 0.5, true);
			}
		}
	}
}

static void gclLoadEndCallback(void)
{
	gclLoadEndCallbackInternal(false, 0);
}

static void gclPrintSystemSpecs(void)
{
	static const char *important_keys[] = {
		"LastVerifyCancel",
		"LastVerifyComplete",
		"LastVerifyStart",
		"LastVerifySkip",
		"VerifyOnNextUpdate"
	};
	char sysspecs[2048];
	int i;
	loadstart_printf("Gathering system specs...");
	for (i=0; i<ARRAY_SIZE(important_keys); i++)
	{
		int v = regGetAppInt(important_keys[i], 0);
		char key[1024];
		sprintf(key, "Verify_%s", important_keys[i]);
		triviaPrintf(key, "%d", v);
	}
	triviaPrintf("Verify_ClientStartTime", "%d", timeSecondsSince2000());
	systemSpecsGetString(SAFESTR(sysspecs));
	loadend_printf("done.");
	printf("%s\n", sysspecs);
}

void gclMain(HINSTANCE hInstance, LPSTR lpCmdLine, const void *logo_jpg_data, int logo_data_size)
{
	int okToQuit = 0;
	
	int argc = 0, oldargc;
	char *args[1000];
	char **argv = args;
	char buf[1000]={0};

	//duplicate the command line, because this is actually the same memory pointer that
	//is returned by GetCommandLine, so if we munge it here, random code later on
	//will mysteriously fail
	lpCmdLine = strdup(lpCmdLine);

	g_CmdLine = lpCmdLine;

	loadCmdline("./cmdline.txt",buf,sizeof(buf));

#if _XBOX || _PS3
    oldargc = tokenize_line_quoted_safe(lpCmdLine,&args[0],ARRAY_SIZE(args),0);
    argc = oldargc + tokenize_line_quoted_safe(buf,&args[oldargc],ARRAY_SIZE(args)-oldargc,0);
#else
    // doesnt have program name on the command line
    args[0] = getExecutableName();
	oldargc = 1 + tokenize_line_quoted_safe(buf,&args[1],ARRAY_SIZE(args)-1,0);
	argc = oldargc + tokenize_line_quoted_safe(lpCmdLine,&args[oldargc],ARRAY_SIZE(args)-oldargc,0);
#endif

	gGCLState.hInstance = hInstance;
	
	gclUpdateThreadPriority();

	winSetHInstance(hInstance);

	printf("Parsing command line...\n");
	loadstart_printf("");
	cmdParseCommandLine(argc, argv);
	gfxDebugClearAccessLevelCmdWarnings(); // Don't show warnings about command line arguments
	loadend_printf("Done parsing command line.");

	gclPrintSystemSpecs();

	loadstart_printf("UtilitiesLib startup...");
	if (!gGCLState.bDoNotQueueErrors) // After cmdParseCommandLine
		setErrorDialogCallback(gclQueueError, NULL);
	utilitiesLibStartup();
	loadend_printf(" done.");



#if DEBUG_CRC
    {
        extern ParseTable parse_MaterialProfileData[];
        ParseTable *pp = parse_MaterialProfileData;

        test_crc = ParseTableCRC(pp, NULL, 0);
        printf("%08x\n", test_crc);

        pUpdateCRCFromParseInfoCB = UpdateCRCFromParseInfoCB;
        test_crc = ParseTableCRC(pp, NULL, 0);
        pUpdateCRCFromParseInfoCB = 0;
        printf("%08x\n", test_crc);

        DebugBreak();
    }
#endif

#if !PLATFORM_CONSOLE
	gclCreateDeviceAndShowLogo(logo_jpg_data, logo_data_size);
#endif

	if (!gbNoGraphics)
	{
		load_screen_timer = timerAlloc();
		loadend_setCallback(gclLoadEndCallback);
	}

	if(isPatchStreamingOn())
	{
		loadstart_printf("Patching required files and acquiring manifest...");
		GSM_Execute(GCL_PATCHSTREAMING);
		//if(gcl_PatchRestart())
		//	return;
		gcl_PatchStreamingFinish();
		loadend_printf(" done.");
	}


	if (!gbNoGraphics)
	{
		rdrShaderLibInit(); // Used to happen automatically with device creation, moved out to be after filesystem startup

		gclRegisterPrimaryInputDevice(gGCLState.hInstance);

		loadstart_printf("Preloading vertex, hull, and domain shaders (async)...");
		rdrLockActiveDevice(gGCLState.pPrimaryDevice->device, false);
		rdrPreloadVertexShaders(gGCLState.pPrimaryDevice->device);
		rdrPreloadHullShaders(gGCLState.pPrimaryDevice->device);
		rdrPreloadDomainShaders(gGCLState.pPrimaryDevice->device);
		rdrUnlockActiveDevice(gGCLState.pPrimaryDevice->device, false, false, false);
		loadend_printf("done.");

#if !_PS3
#if !PLATFORM_CONSOLE
		//if Xlive Login is hooked in, call the init routine
		if ( XLive_login_init_hook != NULL )
			XLive_login_init_hook();
#endif

		//if Xlive Login is not hooked in, or if it is already logged in
		if ( XLive_login_check_hook && !XLive_login_check_hook() )
		{
			//This is the hook for the login to windows live 
			//we need this to happen as early as possible in the init after the D3D window is up
			//this is about the earliest
			//it will stay in this state until Live is logged in, maybe forever
			for( ; !XLive_login_check_hook(); XLive_login_loop_hook())
			{
				//update the input while in this loop
				inpUpdateEarly(gGCLState.pPrimaryDevice->inp_device);
				inpUpdateLate(gGCLState.pPrimaryDevice->inp_device);

				gfxDisplayLogoProgress(gGCLState.pPrimaryDevice->device, logo_jpg_data, logo_data_size, "Cryptic Studios", -1, 0, false);

				Sleep(16);
			}
		}

		//if Xlive Login is hooked in, call the exit routine
		if ( XLive_login_exit_hook != NULL )
			XLive_login_exit_hook();
#endif
	}

	// On Cryptic machines, if we can, run xperf
	xperfEnsureRunning();

	gGCLState.bRunning = true;
	//loadstart_printf("GSM startup..."); // Match in gclInit_BeginFrame

#if _XBOX
	if (gGCLState.bXBoxPatchModeEnabled)
	{
		// Set the No 3D graphics to true
		gbNo3DGraphics = true;

		// Patcher mode
		GSM_Execute(GCL_XBOX_PATCH);
	}
	else
	{
		// Game mode
		GSM_Execute(GCL_INIT);
	}
#else
	GSM_Execute(GCL_INIT);
#endif
	gGCLState.bRunning = false;

	gclPatching_RunPrePatch();

	errorShutdown();
	gclMediaControlShutdown();
}

//////////////////////////////////////////////////////////////////////////
// free camera

static KeyBindProfile freecam_keybinds;

static void bindFreeCameraKeybinds(void)
{
	static int inited = 0;
	if (!inited)
	{
		freecam_keybinds.pchName = "Free Camera Commands";
		freecam_keybinds.bTrickleCommands = 1;
		freecam_keybinds.bTrickleKeys = 1;
		keybind_CopyBindsFromName("FreeCamera", &freecam_keybinds);
		inited = 1;
	}

	keybind_PushProfileEx(&freecam_keybinds, InputBindPriorityCamera);
}

static void unbindFreeCameraKeybinds(void)
{
	keybind_PopProfileEx(&freecam_keybinds, InputBindPriorityCamera);
}

void gclSetFreeCameraActive(void)
{
	DeviceDesc* d = gGCLState.pPrimaryDevice;

	gclPlayerControl_SuspendMouseLook();

	if (gGCLState.bAudioListenerModeShortBoom)
	{
		g_audio_state.d_cameraPos = 1;
	}

	if (d->activecamera == &d->freecamera)
		return;

	d->activecamera = &d->freecamera;
	gfxCameraHalt(d->activecamera);

	gfxCameraControllerCopyPosPyr(demo_playingBack()?&d->democamera:&d->gamecamera, &d->freecamera);

	if (!emIsEditorActive())
		bindFreeCameraKeybinds();
}

void gclSetOverrideCameraActive(GfxCameraController *pCamera, GfxCameraView *pCameraView)
{
	DeviceDesc* d = gGCLState.pPrimaryDevice;

	if (d)
	{
		if (d->activecamera)
		{
			d->activecamera->zoom = false;
			d->activecamera->rotate = false;
			d->activecamera->pan = false;
			d->activecamera->locked = false;
			mouseLock(0);
		} 
		d->activecamera = pCamera;
		d->overrideCameraView = pCameraView;
	}


	if (!emIsEditorActive())
		unbindFreeCameraKeybinds();
}

void gclSetGameCameraActive(void)
{
	DeviceDesc* d = gGCLState.pPrimaryDevice;

	if (gGCLState.bAudioListenerModeShortBoom)
	{
		g_audio_state.d_cameraPos = 0;
	}

	if (demo_playingBack()) {
		gclSetDemoCameraActive();
		return;
	}

	d->overrideCameraView = NULL;

	if (d->activecamera == &d->gamecamera)
		return;

	if (!emIsEditorActive())
		unbindFreeCameraKeybinds();

	// The game camera may have been rotating when the cutscene/freecam started.  Reset this state
	gfxResetCameraControllerState(&d->gamecamera);

	// Switch back from either free camera or cutscene camera
	if(d->activecamera == &d->freecamera)
	{
		F32 dist = d->gamecamera.camdist;
		gfxCameraControllerCopyPosPyr(&d->freecamera, &d->gamecamera);
		d->gamecamera.camdist = dist;
	}
/*	else if(d->activecamera == &d->cutscenecamera)
	{
		F32 dist = d->gamecamera.camdist;
		gfxCameraControllerCopyPosPyr(&d->cutscenecamera, &d->gamecamera);
		d->gamecamera.camdist = dist;
	}
	*/

	d->activecamera = &d->gamecamera;
}

void gclSetEditorCameraActive(int is_active)
{
	DeviceDesc* d = gGCLState.pPrimaryDevice;

	if (is_active)
	{
		emSetDefaultCameraPosPyr(d->activecamera);

		if (d->activecamera == &d->freecamera)
			unbindFreeCameraKeybinds();
	}
	else if (d->activecamera == &d->freecamera)
	{
		bindFreeCameraKeybinds();
	}
}

void gclSetDemoCameraActive(void)
{
	DeviceDesc* d = gGCLState.pPrimaryDevice;

	if (d->activecamera == &d->democamera)
		return;

	if (!emIsEditorActive())
		unbindFreeCameraKeybinds();

	d->activecamera = &d->democamera;
}

//stuff related to TestClient communication
NetLink *spLinkToTestClient = NULL;
NetListen *testClientLinks;
bool gbQuitOnTestClientDisconnect = false;

AUTO_CMD_INT(gbQuitOnTestClientDisconnect, QuitOnTestClientDisconnect) ACMD_CMDLINE;

NetLink *gclGetLinkToTestClient(void)
{
	return spLinkToTestClient;
}

int TestClientConnect(NetLink* link,void *userdata)
{
	assertmsg(spLinkToTestClient == NULL, "Two testclients trying to connect to same client");
	spLinkToTestClient = link;
	linkSetKeepAliveSeconds(link, 1.0f);
	return 1;
}

extern int giHeadshotDebugMode;
int TestClientDisconnect(NetLink* link,void *userdata)
{
	assertmsg(link == spLinkToTestClient, "TestClient link corrupted or duplicated or something");
	spLinkToTestClient = NULL;

	if (gbQuitOnTestClientDisconnect)
	{
		if(giHeadshotDebugMode)
		{
			assertmsg(0, "HeadshotServer connection lost!");
		}
		else
		{
			exit(-1);
		}
	}
	return 1;
}

static void HandleTestClientMessage(Packet *pak,int cmd, NetLink *link, void *userdata)
{
	U32 iTestClientCmdID;
	char *pCommandString;

	switch(cmd)
	{
	xcase FROM_TESTCLIENT_CMD_SENDCOMMAND:
		iTestClientCmdID = pktGetU32(pak);

		if(!iTestClientCmdID)
		{
			return;
		}

		pCommandString = pktGetStringTemp(pak);
		HandleCommandRequestFromTestClient(pCommandString, iTestClientCmdID, link);		
	xdefault:
		printf("Unknown command %d\n",cmd);		
	}
}

//specifies the test client link number
int giTestClientPort = 0;
AUTO_CMD_INT(giTestClientPort, TestClientPort) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

#define NUM_TESTCLIENT_NETINIT_TRIES 10
void InitTestClientCommunication(void)
{
	spLinkToTestClient = commConnect(commDefault(), LINKTYPE_DEFAULT, LINK_FORCE_FLUSH, gLocalHost, giTestClientPort, HandleTestClientMessage, NULL, TestClientDisconnect, 0);
	if(!spLinkToTestClient)
	{
		Errorf("Failed to connect to Client Controller!");
		return;
	}

	if(!linkConnectWait(&spLinkToTestClient, 10.0f))
	{
		Errorf("Timed out connecting to Client Controller!");
		exit(-1);
	}
}

void SendCommandStringToTestClient(const char *pString)
{
	NetLink *pLink = gclGetLinkToTestClient();

	if (pLink)
	{
		Packet *pPak = pktCreate(pLink, TO_TESTCLIENT_CMD_COMMAND);
		pktSendString(pPak, pString);
		pktSend(&pPak);
	}
}

void SendCommandStringToTestClientfEx(const char *pFmt, ...)
{
	char *pcCmdStr = NULL;
	estrGetVarArgs(&pcCmdStr, pFmt);
	SendCommandStringToTestClient(pcCmdStr);
	estrDestroy(&pcCmdStr);
}

static void gclSwapSimulation(void)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
	mmCreateWorldCollIntegration();
	wcSwapSimulation(gGCLState.frameLockedTimer);
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

static S32 canCreateAnotherSkeletonThisFrame(Entity* e){
	return	GET_REF(e->hCreatorNode) ||
			REF_IS_SET_BUT_ABSENT(e->hCreatorNode) ||
			entClientCostumeSkeletonCreationsThisFrame < entClientMaxCostumeSkeletonCreationsPerFrame ||
			entClientMaxCostumeSkeletonCreationsPerFrame == -1;
}

void gclFixEntCostumes(void)
{
	if(gbNoGraphics)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC_PIX();
	{
		static Entity** eaEntsToFixComplete = NULL;
		static Entity** eaEntsToFixIncomplete = NULL;
		EntityIterator *iter = entGetIteratorAllTypesAllPartitions(0,0);
		Entity *pent;

		while(pent = EntityIteratorGetNext(iter))
		{
			//per entity ticks, such as costume setting
			WLCostume* pCostume = GET_REF(pent->hWLCostume);
			if (!pCostume)
			{
				costumeEntity_RegenerateCostume(pent);
			}
			else if (!pent->dyn.guidSkeleton)
			{
				if (pent->pPlayer)
					entClientCreateSkeleton(pent);
				else if (pCostume->bComplete)
					eaPush(&eaEntsToFixComplete, pent);
				else
					eaPush(&eaEntsToFixIncomplete, pent);
			}
		}
		EntityIteratorRelease(iter);

		// Do complete costumes first
		FOR_EACH_IN_EARRAY_FORWARDS(eaEntsToFixComplete, Entity, pEntity)
		{
			if(canCreateAnotherSkeletonThisFrame(pEntity)){
				entClientCreateSkeleton(pEntity);
			}
		}
		FOR_EACH_END;
		eaClear(&eaEntsToFixComplete);

		// Then incompletes
		FOR_EACH_IN_EARRAY_FORWARDS(eaEntsToFixIncomplete, Entity, pEntity)
		{
			if(canCreateAnotherSkeletonThisFrame(pEntity)){
				entClientCreateSkeleton(pEntity);
			}
		}
		FOR_EACH_END;
		eaClear(&eaEntsToFixIncomplete);

	}
	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void gclLibsOncePerFrame(void)
{
	PerfInfoGuard* piGuard = NULL;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
		entityLibOncePerFrame(gGCLState.frameElapsedTime);
		worldLibOncePerFrame(gGCLState.frameElapsedTime);
		editLibOncePerFrame(gGCLState.frameElapsedTime);
		gclMediaControlTick();
		mapState_OncePerFrame();
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

// Called once per frame, when either in full gameplay, or last step of loading
void gclConnectedToGameServerOncePerFrame(void)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_PIX_GUARD(&piGuard);

	wlPerfStartNetBudget();
	gclUtil_UpdatePlayerMovement(gGCLState.frameLockedTimer);
	wlPerfEndNetBudget();

	mmProcessRawInputOnClientFG(gGCLState.frameLockedTimer);

	if (gGCLState.bGotLoginSuccess)
	{
		gclServerSendInputPacket(gGCLState.frameElapsedTimeRealNext);
	}
	
	gclServerMonitorConnection();

	gclSwapSimulation();

	gclUpdateRoamingCell();	

	gclDynDebugOncePerFrame();

#if defined ENABLE_CLIENT_TEST_COLLISION
	gclTestCollision_Update();
#endif

	gclLibsOncePerFrame();
	
	//gclSetCameraTarget();

	PERFINFO_AUTO_STOP_PIX_GUARD(&piGuard);
}

void gclSetCameraTarget(Entity* pEnt)
{
	// Set camera target.

	PERFINFO_AUTO_START_FUNC();

	if (pEnt)
	{
		Vec3 pos;
		dtFxManSetDebugFlag(pEnt->dyn.guidFxMan);
		if(gfxWillWaitForZOcclusion())
			entGetPos(pEnt,pos);
		else
			entGetPosForNextFrame(pEnt, pos);
		gfxCameraControllerSetTarget(&gGCLState.pPrimaryDevice->gamecamera, pos);
	}
	
	PERFINFO_AUTO_STOP_FUNC();
}

void CommandFreeCameraCB(void);
void gclSetFreeCamera(int bFreeCamera)
{
	gGCLState.bUseFreeCamera = bFreeCamera;
	CommandFreeCameraCB();
}

// iIndex: Which set of Interaction Properties to use
// eTeammateType/uTeammateID: The ContainerType/ID of the teammate whose Interact Option was selected (for per-player doors)
bool gclHandleInteractTarget(Entity *ent, Entity *entTarget, WorldInteractionNode *nodeTarget, const char *pcVolumeName, int iIndex, GlobalType eTeammateType, ContainerID uTeammateID, U32 uNodeInteractDist, Vec3 vNodePosFallback, F32 fNodeRadiusFallback, bool bClientValidatesInteract)
{
	// People click repeatedly on the loading screen and may accidentally click on a door
	// even after it's done. So prevent all client-initiated interactions for a short
	// period after zoning in.
	if (GSM_TimeInState(GCL_GAMEPLAY) > 1 && GSM_FramesInState(GCL_GAMEPLAY) > 20 && ent->pPlayer)
	{
		if (gbNoGraphics || pcVolumeName || !bClientValidatesInteract || entity_VerifyInteractTarget(PARTITION_CLIENT, ent,entTarget,nodeTarget,uNodeInteractDist,vNodePosFallback,fNodeRadiusFallback,false, NULL) || (iIndex < 0))
		{
			EntityRef erTarget = entTarget ? entGetRef(entTarget) : 0;
			const char* pchNodeKey = nodeTarget ? wlInteractionNodeGetKey(nodeTarget) : NULL;
			ServerCmd_gslInteract(erTarget, pchNodeKey, pcVolumeName, iIndex, eTeammateType, uTeammateID);
			return true;
		}
	}

	return false;
}

// Gets rid of the logout menu ASAP or players will just jam on the logout button.
static void ClientLogOutSpamHelper(void)
{
	if (GSM_IsStateActive(GCL_GAME_MENU))
		GSM_SwitchToState_Complex(GCL_GAME_MENU "/..");
}

// Shared logout code for all types of logging out: logout, gotoCharacterSelect, and gotoCharacterSelectAndChoosePreviousCharacter. Returns true if the caller should do perform
// the actual logout using a Server Cmd, plus any other state changes desired.
static bool ClientLogOutHelper(void)
{
	if (!GSM_IsStateActive(GCL_LOGIN))
	{
		gclCursorMode_ChangeToDefault();

		if (!gclServerIsConnected() || gclServerTimeSinceRecv() > 5.f) 
		{
			GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN);
			gGCLState.bAttemptingLogout = false;
			return false;
		} 
		else if (!gGCLState.bAttemptingLogout) 
		{
			g_bChoosePreviousCharacterForUGC = false; // protect against getting to choose character screen with this inadvertently set... ugh.
			gGCLState.bAttemptingLogout = true;
			gGCLState.logoutElapsedTime = timeSecondsSince2000();
			ClientLogOutSpamHelper();
			return true; // the only case where we tell the caller to invoke the Server Cmd to logout
		}
	}
	return false;
}

// Log out the current character.
AUTO_COMMAND ACMD_NAME(logout) ACMD_ACCESSLEVEL(0);
void ClientLogOut(void)
{
	if (!ugcEditorQueryLogout(false, false))
	{
		ClientLogOutSpamHelper();
		return;
	}

	if (ClientLogOutHelper())
		ServerCmd_CommandPreLogOutPlayer( schemes_UpdateCurrentStoredScheme() );
}

AUTO_COMMAND ACMD_NAME(ForceLogOut) ACMD_ACCESSLEVEL(0);
void ClientForceLogOut(void)
{
	if (gGCLState.bAttemptingLogout)
	{
		GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN);
		gGCLState.bAttemptingLogout = false;
	}
	else
	{
		ClientLogOut();
	}
}

// Log out the current character.
AUTO_COMMAND ACMD_NAME(gotoCharacterSelect) ACMD_ACCESSLEVEL(0);
void ClientGoToCharacterSelect(void)
{
	if (!ugcEditorQueryLogout(false, false))
	{
		ClientLogOutSpamHelper(); // for if we ever made a button the end user clicks to return to character select screen
		return;
	}

	if (ClientLogOutHelper())
		ServerCmd_CommandPreGoToCharacterSelectPlayer( schemes_UpdateCurrentStoredScheme() );
}

// Log out the current character and automatically choose the previously chosen character for UGC
void ClientGoToCharacterSelectAndChoosePreviousForUGC(void)
{
	if (!ugcEditorQueryLogout(false, true))
		return;

	if (ClientLogOutHelper())
	{
		ServerCmd_CommandPreGoToCharacterSelectPlayer( schemes_UpdateCurrentStoredScheme() );
		g_bChoosePreviousCharacterForUGC = true;
	}
}

void CancelLogOut()
{
	gGCLState.bAttemptingLogout = false;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void CancelLogOutCmd(LogoffCancelType eType)
{
	Entity *pEnt = entActivePlayerPtr();
	const char *pchMessageKey = NULL;
	const char *pchTranslatedMessage = NULL;
	
	CancelLogOut();

	switch (eType)
	{
		xcase kLogoffCancel_Movement:
		{
			pchMessageKey = "Logout.CanceledByMovement";
			pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);
		} 
		xcase kLogoffCancel_CombatDamage:
		{
			pchMessageKey = "Logout.CanceledByCombatDamage";
			pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);
		}
		xcase kLogoffCancel_CombatState:
		{
			pchMessageKey = "Logout.CanceledByCombatState";
			pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);
		}
		xcase kLogoffCancel_Interact:
		{
			pchMessageKey = "Logout.CanceledByInteract";
			pchTranslatedMessage = TranslateMessageKeySafe (pchMessageKey);
		}
	}

	if (pchMessageKey != pchTranslatedMessage)
		notify_NotifySend(pEnt, kNotifyType_LogoutCancel, pchTranslatedMessage, NULL, NULL);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void FinishLogOut(U32 iAuthTicket)
{
	GSM_SwitchToState_Complex(GCL_BASE "/" GCL_LOGIN);
	gGCLState.bAttemptingLogout = false;
	if (iAuthTicket)
	{
		gclSetAuthTicketNew(gGCLState.accountID, iAuthTicket);
	}
}

static DebugMenuItem** s_TSGroupStack = NULL;

static FileScanAction TestScriptsFindFiles(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char* currDirName;
	DebugMenuItem* rootGroup;

	// Make sure we pop groups off the stack til we find the appropriate one for this directory
	// There should always be at least one group on the stack, so never clear it
	currDirName = getFileName(dir);
	assertmsg((eaSize(&s_TSGroupStack) >= 1), "There should always be at least one thing on the stack");
	while ((rootGroup = s_TSGroupStack[eaSize(&s_TSGroupStack) - 1]) && (eaSize(&s_TSGroupStack) > 1))
	{
		if (!stricmp(rootGroup->displayText, currDirName))
			break;
		eaPop(&s_TSGroupStack);
	}
	assert(rootGroup);

	// Sub directory means creates a new group, file means parse out the name and create a command
	if (data->attrib & _A_SUBDIR)
	{
		DebugMenuItem* newGroup = debugmenu_AddNewCommandGroup(rootGroup, data->name, NULL, false);
		eaPush(&s_TSGroupStack, newGroup);
	}
	else
	{
		char cmdStr[1024];
		char dispStr[1024];
		sprintf(cmdStr, "exec %s/%s", dir, data->name);
		getFileNameNoExt(dispStr, data->name);
		strchrReplace(dispStr, '_', ' ');
		debugmenu_AddNewCommand(rootGroup, dispStr, cmdStr);
	}
	return FSA_EXPLORE_DIRECTORY;
}

static void TestScriptsDebugMenu(Entity* playerEnt, DebugMenuItem* groupRoot)
{
	PERFINFO_AUTO_START_FUNC();
	// Start with just the true root on the stack, then recurse through the directory structure and add everything
	eaClear(&s_TSGroupStack);
	eaPush(&s_TSGroupStack, groupRoot);
	fileScanAllDataDirs("TestScripts", TestScriptsFindFiles, NULL);
	PERFINFO_AUTO_STOP();
}

AUTO_RUN;
void TestScriptsRegisterDebugMenu(void)
{
	debugmenu_RegisterNewGroup("Test Scripts", TestScriptsDebugMenu);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void dtAddFxToEntSimple(EntityRef ref, const char* fxName, const char* filename)
{
	Entity* be = entFromEntityRefAnyPartition(ref);

	if(!dynFxInfoExists(fxName))
	{
		ErrorFilenamef(filename, "FX did not exist: %s", fxName);
		return;
	}

	if(be)
		dtAddFx(be->dyn.guidFxMan, fxName, NULL, 0, 0, 0, 0, NULL, eDynFxSource_Expression, NULL, NULL);
}

AUTO_COMMAND;
void wcPrintActorsC(Entity *pEnt, S32 cellx, S32 cellz)
{
	wcPrintActors(entGetPartitionIdx(pEnt), cellx, cellz);
}

//stuff that happens every 10 seconds during gameplay
void gclPeriodicGameplayUpdate(void)
{
	U32 uiLastInput;
	static U32 s_uiLastPoked;

	if(gbNoGraphics)
	{
		ServerCmd_gslAwayPoke();
		return;
	}

	uiLastInput = inpLastInputEdgeTime();

	if (uiLastInput != s_uiLastPoked && gclServerIsConnected())
	{
		s_uiLastPoked = uiLastInput;
		ServerCmd_gslAwayPoke();
	}
}

//Saves out client settings if necessary every 10 seconds during gameplay
void gclSettingsUpdate(void)
{
	PeriodicStoredSchemesUpdate();
}

void ScreenshotForServerMonitorCB(char *pFileName, void *pUserData)
{
	TextParserBinaryBlock *pBlock;


	pBlock = TextParserBinaryBlock_CreateFromFile(pFileName, false);

	if (!pBlock)
	{
		pBlock = TextParserBinaryBlock_CreateFromMemory(NULL, 0, false);
	}

	ServerCmd_HereIsScreenshotForServerMonitor(pBlock, (int)((intptr_t)pUserData));

	StructDestroy(parse_TextParserBinaryBlock, pBlock);
}



AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void ScreenshotForServerMonitor(int iRequestID)
{
	gfxRequestScreenshot(NULL, true, ScreenshotForServerMonitorCB, (void*)((intptr_t)iRequestID));
}


AUTO_STARTUP(UIGen) ASTRT_DEPS(ItemQualities UILib PowerTrees CraftingUI GCLSound ClientCivilian VisionModeEffects gclTransformation PlayerStats EntityUI ItemAssignmentUI GroupProjectUI Reticle MapNotificationsLoad);
void gclGenStartup(void)
{
	if(!gbNoGraphics)
	{
		ui_LoadGens();
	}
}

AUTO_STARTUP(ClientTutorialScreenRegions) ASTRT_DEPS(UIGen);
void gclClientTutorialScreens(void)
{
	TutorialScreenRegionsLoad();
}

AUTO_COMMAND ACMD_CMDLINE;
void noworld(int d)
{
	gGCLState.bSkipWorldUpdate = !!d;

	globCmdParse("Physics.Predict 0");
}

AUTO_CMD_INT(gGCLState.bForceLoadCostumes, ForceLoadCostumes) ACMD_COMMANDLINE;
AUTO_CMD_INT(gGCLState.bInitialPopUp, InitialPopup) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

typedef struct CBStruct
{
	gclSetEditMode cbFunc;
} CBStruct;

CBStruct **ppEditChangeCallbacks;


void gclRegisterEditChangeCallback(gclSetEditMode cbChange)
{
	CBStruct *pStruct = calloc(sizeof(CBStruct),1);
	pStruct->cbFunc = cbChange;
	eaPush(&ppEditChangeCallbacks, pStruct);
}

void gclSetProductionEdit(int enable)
{
	int i;
	if (isProductionEditMode() == enable)
	{
		return;
	}
	if( !enable) {
		ugcEditorShutdown();
	}
	setProductionEditMode(enable);

	// REMOVEME: UGC HACK
	//
	// To prevent costumes from getting unloaded, we disable the
	// unloading of unreferenced resources while in ProductionEdit
	// mode.
	resDictSetMaxUnreferencedResources( g_hPlayerCostumeDict, (enable ? 0 : 30) );		

	for (i = 0; i < eaSize(&ppEditChangeCallbacks); i++)
	{
		ppEditChangeCallbacks[i]->cbFunc();
	}
}


#define PROJECT_GAMECLIENT_CONFIG_FILENAME	"server/ProjectGameClientConfig.txt"

static void ProjectGameClientConfigPostLoad()
{
	if ((gProjectGameClientConfig.bUseCapsuleBounds || gProjectGameClientConfig.bUseFixedOverHead) &&
		gProjectGameClientConfig.pScreenBoundingAccelConfig)
	{
		ErrorFilenamef(PROJECT_GAMECLIENT_CONFIG_FILENAME, "ScreenBoundingAccelConfig conflicts with the UseCapsuleBounds and UseFixedOverHead options.");
	}
}

// Reload CombatConfig top level callback, not particularly safe/correct
static void ProjectGameClientConfigReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading ProjectGameClientConfig...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	StructInit(parse_ProjectGameClientConfig, &gProjectGameClientConfig);
	ParserLoadFiles(NULL, PROJECT_GAMECLIENT_CONFIG_FILENAME, "GameClientConfig.bin", PARSER_OPTIONALFLAG, parse_ProjectGameClientConfig, &gProjectGameClientConfig);
	ProjectGameClientConfigPostLoad();
	loadend_printf(" done.");
}

AUTO_STARTUP(ProjectGameClientConfig);
void gclLoadProjectGameClientConfig(void)
{
	// Read in project game client specific data
	ParserLoadFiles(NULL, PROJECT_GAMECLIENT_CONFIG_FILENAME, "GameClientConfig.bin", PARSER_OPTIONALFLAG, parse_ProjectGameClientConfig, &gProjectGameClientConfig);
	ProjectGameClientConfigPostLoad();

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, PROJECT_GAMECLIENT_CONFIG_FILENAME, ProjectGameClientConfigReload);
	}
}

// Enable a game event.
AUTO_COMMAND ACMD_NAME(ShardWideEvent) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_HIDE;
void gclCmdShardWideEvent(const char *pchEvent)
{
	pchEvent = allocAddString(pchEvent);
	if (eaFind(&gGCLState.shardWideEvents, pchEvent) < 0)
		eaPush(&gGCLState.shardWideEvents, pchEvent);
}

// Disable a game event.
AUTO_COMMAND ACMD_NAME(ShardWideEventDisable) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclCmdDisableShardWideEvent(const char *pchEvent)
{
	pchEvent = allocFindString(pchEvent);
	eaFindAndRemove(&gGCLState.shardWideEvents, pchEvent);
}

void OVERRIDE_LATELINK_netreceive_socksRecieveError(NetLink *link, U8 code)
{
	GameDialogGenericMessage("Proxy server error","An error has occurred while connecting to the proxy server, if this happens again it may be helpful to try disabling the proxy and restarting the game client.");
}

static char *spServerAndPartitionInfoDebugString = NULL;
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SetServerAndPartitionInfoDebugString(char *pStr)
{
	estrCopy2(&spServerAndPartitionInfoDebugString, pStr);
}

//current instance index of the partition you're logged into
static int siInstanceIndexNum = 0;

int gclGetCurrentInstanceIndex(void)
{
	return siInstanceIndexNum;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SetInstanceIndex(int iIndex)
{
	siInstanceIndexNum = iIndex;
}


//did the game server tell us that instance switching is allowed
static bool sbInstanceSwitchingAllowed = false;

bool gclGetInstanceSwitchingAllowed(void)
{
	return sbInstanceSwitchingAllowed;
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void SetInstanceSwitchingAllowed(bool bSet)
{
	sbInstanceSwitchingAllowed = bSet;
}




const char *OVERRIDE_LATELINK_GetGameServerIDAndPartitionString(void)
{
	if (gclServerIsConnected())
	{
		return spServerAndPartitionInfoDebugString;
	}
	else
	{
		return "Disconnected";
	}
}

// Indicates whether any of the cut-scene cameras are active
bool gclAnyCutsceneCameraActive(void)
{
	return	gGCLState.pPrimaryDevice && 
			(	gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->contactcamera ||
				gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->cutscenecamera);
}

// Indicates wheter the contact dialog camera is active
bool gclContactDialogCameraActive(void)
{
	return	gGCLState.pPrimaryDevice &&
			gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->contactcamera;
}


#include "autogen/GameClientLib_autogen_ClientCmdWrappers.c"
#include "autogen/GameServerLib_autogen_ServerCmdWrappers.c"
#include "autogen/AILib_autogen_ServerCmdWrappers.c"

#include "mapDescription.h"
#include "autogen/GlobalEnums_h_ast.c"
#include "autogen/MapDescription_h_ast.c"
#include "Autogen/gameclientlib_h_ast.c"
#include "UGCprojectCommon.h"
#include "UGCProjectCommon_h_ast.c"
#include "ResourceDBUtils.h"
#include "Autogen/ResourceDBUtils_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping("UGCProject.c", BUDGET_GameSystems););

