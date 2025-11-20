/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameClientLib.h"

#include "file.h"
#include "sysutil.h"
#include "hoglib.h"
#include "EventTimingLog.h"
#include "FolderCache.h"
#include "logging.h"

#include "inputKeyBind.h"
#include "gfxdebug.h"

#include "GameAccountDataCommon.h"
#include "gclCamera.h"
#include "uimission.h"
#include "Character_tick.h"
#include "gclEntity.h"
#include "gclCombatAdvantage.h"
#include "EntityIterator.h"
#include "entity/EntityClient.h"
#include "Combat/ClientTargeting.h"
#if _XBOX
#include "XLiveLib.h"
#endif
#include "gclClickToMove.h"
#include "GlobalTypes.h"
#include "GfxConsole.h"
#include "GfxLCD.h"
#include "GameStringFormat.h"
#include "Character.h"
#include "InteractionUI.h"
#include "Player.h"
#include "gclSpectator.h"


#include "gclDumpSending.h"
#include "gclUtils.h"


#define	PHYSX_SRC_FOLDER "../../3rdparty"
#include "PhysicsSDK.h"

#include "EditLib.h"
#include "winutil.h"

#include "dynFxDamage.h"
#include "gclSteam.h"
#include "wlPerf.h"

extern ControlScheme g_CurrentScheme;
static bool bSendDumpsImmediately = false;
AUTO_CMD_INT(bSendDumpsImmediately, SendDumpsImmediately) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

extern ParseTable parse_PowerActivation[];
#define TYPE_parse_PowerActivation PowerActivation


#if _XBOX
//hooks for XLive login
void (*XLive_login_init_hook)(void) = NULL;
#endif


AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_CLIENT);
	FolderCacheAllowCoreExclusions(true);
}

static void setupConsolePalette(void)
{
#if !_PS3
	U32 palette[16] = {
		0x000000, // Default console background color
		0x803f3f,
		0x3f803f,
		0x80803f,
		0x3f3f80,
		0x803f80,
		0x3fa0ff, // Used for Errorf printouts
		0xc0c0c0, // Used for normal text
		0x808080,
		0xff7f7f,
		0x7fff7f, // Used for fast loadastrt/end_printfs
		0xffff7f,
		0x3f3fff,
		0xff7fff,
		0x7fffff, // Used for slow loadstart/end_printfs
		0xffffff,
	};
	consoleSetPalette(palette);
#endif
}

// Enables the debug console
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void console(int enable)
{
#if !PLATFORM_CONSOLE
	if (enable)
	{
		setupConsolePalette();
		newConsoleWindow();
		showConsoleWindow();
	}
#endif
}

// Enables the debug console
AUTO_COMMAND ACMD_NAME(console) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void consoleAtRuntime(void)
{
#if !PLATFORM_CONSOLE
	// In development, also allow opening the Win32 console
	if (isDevelopmentMode() || !keybind_GetProfileCount()) // Or at startup
	{
		setupConsolePalette();
		newConsoleWindow();
		showConsoleWindow();
	}
#endif
	if (keybind_GetProfileCount()) // Past startup
	{
		gfxConsoleAllow(1);
		gfxConsoleEnable(1);
	}
}


gclMainFunction pOverrideMain;
gclPreMainFunction pOverridePreMain;

#include "gimmeDLLWrapper.h"

#if _PS3
int ps3_main(int argc_in, const char** argv_in)
#elif _XBOX
int main(int argc_in, const char** argv_in)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
#if PLATFORM_CONSOLE
	HINSTANCE hInstance = 0;
	LPSTR lpCmdLine = GetCommandLine();
#endif

#if _XBOX && defined(PROFILE)
	PIXNameThread( "GameClient main" );
#endif

	EXCEPTION_HANDLER_BEGIN
 
	void *logo_data = NULL;
	U32 logo_size = 0;

	WAIT_FOR_DEBUGGER_LPCMDLINE

	loadstart_printf("DO_AUTO_RUNS...");

	g_do_not_try_to_load_folder_cache = true; // Cannot access filesystem on the client before the loading screen is displayed!

	//start out in this mode in case we get crashes during auto run time. Set to a saner mode during gclPreMain for 
	//dev mode, etc.
	setProductionClientAssertMode();

	DO_AUTO_RUNS;

#if _XBOX
	if (isProductionMode() &&
			fileSize("game:/piggs/data.hogg") == fileSize("devkit:/FightClub/piggs/data.hogg") &&
			fileSize("game:/piggs/xboxData.hogg") == fileSize("devkit:/FightClub/piggs/xboxData.hogg"))
		hogAddPathRedirect("game:/", "devkit:/FightClub/"); // Matches up with what's in the PatchClient

	//Initialize XLive hooks into the system
	InitXliveLib();

	{
		int cmdLineLength = strlen(lpCmdLine);
		if (cmdLineLength - (strlen(getExecutableName()) - strlen("game:\\")) >= 256)
		{
			msgAlert(NULL, "Command line too long, Xbox has truncated it.  Use cmdline.txt instead.");
		}
	}
#endif

	loadend_printf(" done.");

	loadstart_printf("Loading logo...");
	// load the cryptic logo from a resource
#if _PS3
	{
		extern U8 _binary_Cryptic_Logo_jpg_start;
		extern U8 _binary_Cryptic_Logo_jpg_end;
		logo_data = &_binary_Cryptic_Logo_jpg_start;
		logo_size = &_binary_Cryptic_Logo_jpg_end - &_binary_Cryptic_Logo_jpg_start;
	}
#elif _XBOX
	if (!XGetModuleSection(GetModuleHandle(NULL), "CRYPLOGO", &logo_data, &logo_size))
		logo_data = NULL; // just in case
#else
	// load bitmap and get size
	{
		HRSRC rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(gGCLState.logoResource), "JPG");
		if (rsrc)
		{
			HGLOBAL gptr = LoadResource(GetModuleHandle(NULL), rsrc);
			if (gptr)
			{
				logo_data = LockResource(gptr); // no need to unlock this ever, it gets unloaded with the program
				logo_size = SizeofResource(GetModuleHandle(NULL), rsrc);
			}
		}
	}
#endif
	loadend_printf(" done.");

	// First, call the universal setup gameclientlib stuff
	if (pOverridePreMain)
	{
		if (!pOverridePreMain(hInstance, lpCmdLine))
			return 0;
	}
	else
	{
		if (!gclPreMain(GetProjectName(), logo_data, (int)logo_size))
			return 0;
	}

#if _XBOX
	//if Xlive Login is hooked in, call the init routine
	if ( XLive_login_init_hook != NULL ) XLive_login_init_hook();
#endif

	//this should be as firstest as possible, because if things crash before it, we can't get the dump
#if _XBOX
	if (bSendDumpsImmediately)
	{
		gclSendDeferredDumps_NoGraphics();
		exit(0);
	}
#endif

	if (pOverrideMain)
	{
		pOverrideMain(hInstance, lpCmdLine, logo_data, (int)logo_size);
	}
	else
	{
		// Call the game client loop. Note, we'll either need to pass some game-specific callbacks in here or restructure this
		gclMain(hInstance, lpCmdLine, logo_data, (int)logo_size);
	}

	EXCEPTION_HANDLER_END
	gclAboutToExit();
	logWaitForQueueToEmpty();
	return 0;
}

void gclExternInitCamera(GfxCameraController *camera)
{
	gfxInitCameraController(camera, gclDefaultCameraFunc, NULL);
	camera->do_shadow_scaling = 1;
}

void gclInteract_Tick(void)
{
	Entity *ePlayer = entActivePlayerPtr();

	if (!ePlayer)
		return;

	if ( ePlayer->pPlayer )
	{
		gclInteract_FindBest(ePlayer);

		entity_UpdateTargetableNodes(ePlayer);
	}
}

LATELINK;
void gclUpdateLCD(void);

void DEFAULT_LATELINK_gclUpdateLCD(void)
{
	static char *s_pch;
	Entity *pEnt;
	if(!gfxLCDIsEnabled()){
		return;
	}
	pEnt = entActivePlayerPtr();
	if(!SAFE_MEMBER2(pEnt, pChar, pattrBasic)){
		return;
	}
	PERFINFO_AUTO_START_FUNC();
	// The first thing added is shown at the bottom of the LCD

	if (gfxLCDIsQVGA())
	{
		// Can add up to 5 additional lines here
	}

	estrClear(&s_pch);
	FormatGameMessageKey(&s_pch, "LCD_Power",
		STRFMT_INT("Current", pEnt->pChar->pattrBasic->fPower),
		STRFMT_INT("Max", pEnt->pChar->pattrBasic->fPowerMax),
		STRFMT_END);
	gfxLCDAddMeter(s_pch, (S32)pEnt->pChar->pattrBasic->fPower, 0, (S32)pEnt->pChar->pattrBasic->fPowerMax, 0xFFFFFFff, 0xFF0000ff, 0xFFFF00ff, 0x00FF00ff);
	estrClear(&s_pch);
	FormatGameMessageKey(&s_pch, "LCD_HP",
		STRFMT_INT("Current", pEnt->pChar->pattrBasic->fHitPoints),
		STRFMT_INT("Max", pEnt->pChar->pattrBasic->fHitPointsMax),
		STRFMT_END);
	gfxLCDAddMeter(s_pch, (S32)pEnt->pChar->pattrBasic->fHitPoints, 0, (S32)pEnt->pChar->pattrBasic->fHitPointsMax, 0xFFFFFFff, 0xFF0000ff, 0xFFFF00ff, 0x00FF00ff);
	gfxLCDAddText(entGetLocalName(pEnt), 0x8080FFff);
	PERFINFO_AUTO_STOP();
}

void DEFAULT_LATELINK_gclOncePerFrame_GameSpecific(F32 elapsed)
{
	//Does nothing. Overridden per-project.
	return;
}

void gclExternOncePerFrame(F32 elapsed, bool drawWorld)
{
	Entity *ePlayer = entActivePlayerPtr();
	F32 fTickTime = 0.0f;
	PlayerDebug* pDebug = ePlayer ? entGetPlayerDebug(ePlayer, false) : NULL;

	PERFINFO_AUTO_START_FUNC_PIX();
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);

	character_AccumulateTickTime(elapsed, &fTickTime);

	gclOncePerFrame_GameSpecific(elapsed);

	gclCombatAdvantage_OncePerFrame(elapsed);

	if(ePlayer && ePlayer->pChar)
	{
		character_CheckQueuedTargetUpdate(entGetPartitionIdx(ePlayer),ePlayer->pChar);
	}

	if(fTickTime > 0.0f)
	{
		if(ePlayer && ePlayer->pChar && !entCheckFlag(ePlayer,ENTITYFLAG_PUPPETPROGRESS))
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(ePlayer);
			int iPartitionIdx = entGetPartitionIdx(ePlayer);
			Entity *eMount = NULL;
			character_TickPhaseOne(iPartitionIdx,ePlayer->pChar,fTickTime,pExtract);
			eMount = entGetMount(ePlayer);
			if (eMount && eMount->pChar)
			{
				character_TickPhaseOne(iPartitionIdx,eMount->pChar,fTickTime,pExtract);
			}

			character_TickPhaseTwoClient(ePlayer->pChar,fTickTime);
			if (eMount && eMount->pChar)
			{
				character_TickPhaseTwoClient(eMount->pChar,fTickTime);
			}
		}
	}
	// Once per frame debugging stuff
	if(pDebug)
	{
		if (pDebug->showServerFPS)
		{
			if(pDebug->clientsNotLoggedInCount)
			{
				gfxDebugPrintfQueue("%2.2f (svr fps, %d clients, %d logging in)",
									pDebug->currServerFPS,
									pDebug->clientsLoggedInCount,
									pDebug->clientsNotLoggedInCount);
			}
			else
			{
				gfxDebugPrintfQueue("%2.2f (svr fps, %d clients)",
									pDebug->currServerFPS,
									pDebug->clientsLoggedInCount);
			}
		}
		// TODO: This doesn't really need to be once per frame
		editLibSetEncounterBudgets(pDebug->numSpawnedEnts,
				pDebug->numRunningEncs, pDebug->numTotalEncs,
				pDebug->spawnedFSMCost, pDebug->potentialFSMCost);
	}

	
	{
		Vec3 vCamPos = {0, 0, 0};

		PERFINFO_AUTO_START("EntityClientTick",1);

		if(!gbNoGraphics)
		{	
			gfxGetActiveCameraPos(vCamPos);
		}

		{
			EntityIterator*	iter = entGetIteratorAllTypesAllPartitions(0,0);
			Entity*			e;
			while(e = EntityIteratorGetNext(iter))
			{
				gclExternEntityDoorSequenceTick(e, elapsed);

				if ( entCheckFlag(e,ENTITYFLAG_IGNORE) )
					continue;

				if(	ePlayer &&
					e != ePlayer &&
					fTickTime > 0.0f)
				{
					gclExternEntityDetectable(ePlayer, e);
				}
				
				gclEntityTick(vCamPos,e,NULL,elapsed);
			}
			EntityIteratorRelease(iter);
		}

		
		{
			U32 iter;
			
			if(gclClientOnlyEntityIterCreate(&iter)){
				ClientOnlyEntity* coe;
				while(coe = gclClientOnlyEntityIterGetNext(iter)){
					if(gclEntityTick(vCamPos,coe->entity,coe,elapsed)){
						// coe was destroyed.
						
						coe = NULL;
					}
				}
				gclClientOnlyEntityIterDestroy(&iter);
			}
		}
		
		PERFINFO_AUTO_STOP();
	}

	//this was originally only happening if drawWorld was set to true
	//this has to take place every frame because the data must be updated for 
	//gens and other processes that are dependent on interaction data
	gclInteract_Tick();

	gclSpectator_UpdateLocalPlayer();

	gclPet_UpdateLocalPlayerPetInfo();

	gclCheckPendingLootGlowEnts();

	if (drawWorld)
	{
		wlPerfStartUIBudget();
		PERFINFO_AUTO_START("target_Tick", 1);
		if (ePlayer && !entCheckFlag(ePlayer,ENTITYFLAG_PUPPETPROGRESS))
		{
			clientTarget_tick(elapsed);
		}
		if (ePlayer && ePlayer->pChar && (g_CurrentScheme.bEnableClickToMove))
		{
			PERFINFO_AUTO_STOP_START("clickToMoveTick", 1);
			clickToMoveTick();
		}
		PERFINFO_AUTO_STOP();
		wlPerfEndUIBudget();
		
		encounterdebug_DrawBeacons();

		// massive blocking hack
		//hackyBlockCheck(e);
	}

	gclUpdateLCD();
	
	dynFxDamageResetReloadedFlag(); //LDM: the world lib update is too early for this so it goes here

	gclSteamOncePerFrame();

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP_FUNC_PIX();
}


//#include "..\TestClient\Autogen\FCTestClient_autogen_TestClientCmds.c"
