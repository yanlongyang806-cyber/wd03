#include "UtilitiesLib.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "logging.h"
#include <stdio.h>
#include <conio.h>
#include "MemoryMonitor.h"

#include "file.h"
#include "FolderCache.h"

#include "sysutil.h"
#include <math.h>
#include "gimmeDLLWrapper.h"

#include "winutil.h"

#include "ServerLib.h"

#include "Multiplexer.h"
#include "ControllerLink.h"
#include "GlobalTypes.h"
#include "TimedCallback.h"
#include "ThreadManager.h"
#include "TestServerIntegration.h"

bool gbMainLoopStarted = false;

bool gbTryToReconnectToLogServer = false;
bool gbTryToReconnectToTestServer = false;

bool gbStandaloneMultiplex = false;
AUTO_CMD_INT(gbStandaloneMultiplex, StandaloneMultiplex);

AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_MULTIPLEXER);
}


void UpdateMultiplexerTitle(void)
{
	char buf[200];
	sprintf(buf, "Multiplexer (%d connections, %d messages relayed)", GetNumMultiplexerConnections(), GetNumMultiplexerMessagesRelayed());
	setConsoleTitle(buf);
}




void MultiplexerAuxControllerMessageHandler(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	GlobalType eType;

	switch (cmd)
	{
//special case... the controller informs the transaction server that a server has crashed
//or asserted, so the transaction server can disconnect all logical connections to it, even 
//though the tcp link might not die
	xcase FROM_CONTROLLER__SERVERSPECIFIC__SERVER_RESTARTED:
		eType = GetContainerTypeFromPacket(pkt);

		if (eType == GLOBALTYPE_LOGSERVER)
		{
			gbTryToReconnectToLogServer = true;
		}
		else if (eType == GLOBALTYPE_TESTSERVER)
		{
			gbTryToReconnectToTestServer = gbIsTestServerHostSet;
		}
	}
}

void multiplexerPeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	gbTryToReconnectToLogServer = true;
	gbTryToReconnectToTestServer = gbIsTestServerHostSet;
}




int main(int argc,char **argv)
{
	int	i,frameTimer;

	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER
	DO_AUTO_RUNS
	setDefaultAssertMode();
	memMonitorInit();
	for(i = 0; i < argc; i++){
		printf("%s ", argv[i]);
	}
	printf("\n");

	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x8080ff);

	FolderCacheChooseModeNoPigsInDevelopment();

	preloadDLLs(0);

	if (fileIsUsingDevData()) {
	} else {
		gimmeDLLDisable(1);
	}

	srand((unsigned int)time(NULL));
	consoleUpSize(110,128);


	logSetDir(GlobalTypeToName(GetAppGlobalType()));
	serverLibStartup(argc, argv);

	UpdateMultiplexerTitle();

	loadstart_printf("Beginning Multiplexing");
	
	CreateMultiplexer(GetMultiplexerListenPort());

	loadend_printf("");

	//multiplexers survive controller disconnect and will eventually be killed by the launcher, so that
	//they can keep relaying message until the bitter end
	AttemptToConnectToController(false, MultiplexerAuxControllerMessageHandler, true);

	if(!gbStandaloneMultiplex)
	{
		loadstart_printf("Connecting to Transaction Server...");
		while(!Multiplexer_ConnectToServer(gServerLibState.transactionServerHost, gServerLibState.transactionServerPort,
			MULTIPLEX_CONST_ID_TRANSACTION_SERVER, "Link for multiplexed messages to transaction server", true))
		{
		}
		loadend_printf("...done.");

		loadstart_printf("Connecting to Log Server...");
		if(Multiplexer_ConnectToServer(gServerLibState.logServerHost, DEFAULT_LOGSERVER_PORT,
			MULTIPLEX_CONST_ID_LOG_SERVER, "Link for multiplexed messages to log server", false))
		{
			loadend_printf("...succeeded.");
		}
		else
		{
			loadend_printf("...failed. (Will continue attempting.)");
		}
	}	
	
	if(gbIsTestServerHostSet)
	{
		loadstart_printf("Connecting to Test Server... ");
		if(Multiplexer_ConnectToServer(gTestServerHost, gTestServerPort, MULTIPLEX_CONST_ID_TEST_SERVER,
			"Link for multiplexed messages to test server", false))
		{
			loadend_printf("...succeeded.");
		}
		else
		{
			loadend_printf("...failed. (Will continue attempting.)");
		}
	}

	DirectlyInformControllerOfState("ready");

	frameTimer = timerAlloc();

	TimedCallback_Add(multiplexerPeriodicUpdate, 0, 5.0f);


	if (isProductionMode())
	{
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
		tmSetGlobalThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL);
	}

	for(;;)
	{	
		F32 frametime;
		
		autoTimerThreadFrameBegin(__FUNCTION__);
		
		frametime = timerElapsedAndStart(frameTimer);
		//Sleep(DEFAULT_SERVER_SLEEP_TIME);
		utilitiesLibOncePerFrame(frametime, 1);

		Multiplexer_Update();


		//always try to reconnect to transaction server, if we're not currently connected
		if (!Multiplexer_IsServerConnected(MULTIPLEX_CONST_ID_TRANSACTION_SERVER))
		{
			log_printf(LOG_CRASH, "Launcher lost connection to Transaction Server... exiting");
			svrExit(-1);
		}

		UpdateMultiplexerTitle();
		serverLibOncePerFrame();

		if (gbTryToReconnectToLogServer)
		{
			if (Multiplexer_IsServerConnected(MULTIPLEX_CONST_ID_LOG_SERVER))
			{
				gbTryToReconnectToLogServer = false;
			}
			else
			{
				if (Multiplexer_ConnectToServer(gServerLibState.logServerHost, DEFAULT_LOGSERVER_PORT,
					MULTIPLEX_CONST_ID_LOG_SERVER, "Link for multiplexed messages to log server", false))
				{
					gbTryToReconnectToLogServer = false;
				}
			}
		}

		if (gbTryToReconnectToTestServer)
		{
			if (Multiplexer_IsServerConnected(MULTIPLEX_CONST_ID_TEST_SERVER))
			{
				gbTryToReconnectToTestServer = false;
			}
			else
			{
				if (Multiplexer_ConnectToServer(gTestServerHost, gTestServerPort, MULTIPLEX_CONST_ID_TEST_SERVER, "Link for multiplexed messages to Test Server", false))
				{
					gbTryToReconnectToTestServer = false;
				}
			}
		}

		autoTimerThreadFrameEnd();
	}
	EXCEPTION_HANDLER_END
}
