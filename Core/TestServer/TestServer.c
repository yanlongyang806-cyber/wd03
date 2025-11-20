/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "FolderCache.h"
#include "GenericHttpServing.h"
#include "GlobalTypes.h"
#include "objTransactions.h"
#include "ServerLib.h"
#include "sysutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "winutil.h"

#include "TestServerExternal.h"
#include "TestServerGlobal.h"
#include "TestServerHttp.h"
#include "TestServerLua.h"
#include "TestServerReport.h"
#include "TestServerSchedule.h"
#include "TestServerSharded.h"

static bool bContinue = true;

static BOOL consoleCtrlHandler(DWORD fdwCtrlType)
{
	switch(fdwCtrlType)
	{
	case CTRL_CLOSE_EVENT: 
	case CTRL_LOGOFF_EVENT: 
	case CTRL_SHUTDOWN_EVENT: 
		bContinue = false;
		while(1) Sleep(1);
		return TRUE;
		break;

	case CTRL_BREAK_EVENT: 
	case CTRL_C_EVENT: 
		bContinue = false;
		return TRUE;
		break;

	default:
		return FALSE;
	}
}

int main(int argc, char** argv)
{
	int timer;
	F32 fTotalElapsed = 0.0f, fElapsed;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	RegisterGenericGlobalTypes(); // We need to call these now, so the parsing works
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_TESTSERVER);

	DO_AUTO_RUNS;

	setConsoleTitle(GlobalTypeToName(GetAppGlobalType()));
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x800000);
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, TRUE);

	FolderCacheChooseMode();
	FolderCacheEnableCallbacks(1);
	FolderCacheStartMonitorThread();

	utilitiesLibStartup();
	serverLibStartup(argc, argv);

	if(stricmp(GetShardInfoString(), "none"))
	{
		TestServer_InitSharded();
	}
	else
	{
		ErrorfPopCallback();
		popErrorDialogCallback();
	}

	// Things we want to do:
	// Init various modules
	TestServer_InitExternal();
	TestServer_InitGlobals();
	TestServer_InitReports();
	TestServer_InitSchedules();
	TestServer_ReadInGlobals();

	// Set the product name and short product name globals
	TestServer_SetGlobal_String("Config", "ProductName", -1, GetProductName());
	TestServer_SetGlobal_String("Config", "ShortProductName", -1, GetShortProductName());

	// Open the web interface
	TestServer_StartWebInterface();

	// Start the Lua thread
	TestServer_StartLuaThread();
	
	// Validate Test Server scripts
	TestServer_VerifyScripts();

	if(TestServer_IsSharded())
	{
		TestServer_ShardedReady();
	}
	else
	{
		TestServer_ConsolePrintf("Ready.\n");
	}

	timer = timerAlloc();

	// Main loop
	while(bContinue)
	{
		autoTimerThreadFrameBegin("main");

		fElapsed = timerElapsedAndStart(timer);
		fTotalElapsed += fElapsed;

		commMonitor(commDefault());
		utilitiesLibOncePerFrame(fElapsed, 1.0f);
		serverLibOncePerFrame();

		GenericHttpServing_Tick();

		TestServer_MonitorLuaThread();
		TestServer_CheckReports();
		TestServer_ScheduleTick();

		if(TestServer_IsSharded())
		{
			TestServer_ShardedTick(fTotalElapsed, fElapsed);
		}
		
		autoTimerThreadFrameEnd();
	}

	// Write out the globals here
	TestServer_WriteOutGlobals();

	EXCEPTION_HANDLER_END
	return 0;
}