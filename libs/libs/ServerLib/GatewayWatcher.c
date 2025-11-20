/***************************************************************************
 
 
 
 *
 ***************************************************************************/
#include "alerts.h"
#include "CrypticPorts.h"
#include "EString.h"
#include "EArray.h"
#include "GatewayUtil.h"
#include "objTransactions.h"
#include "process_util.h" // for processExists
#include "sock.h"         // makeIpStr
#include "textparser.h"
#include "TimedCallback.h"
#include "utils.h"
#include "utilitieslib.h" // for GetUsefulVersionString
#include "file.h"         // for isProductionMode

#include "../../core/controller/pub/ControllerPub.h"
#include "./AutoGen/Controller_autogen_RemoteFuncs.h"

#include "AutoGen/GatewayWatcher_c_ast.h"
#include "AutoGen/GatewayWatcher_h_ast.h"

#include "GatewayWatcher.h"

// Use a persistent cmd.exe to run node. This will leave the app up for a bit after it dies so
//   it can be debugged.
int g_bGatewayUseCmdToLaunch = 0;
AUTO_CMD_INT(g_bGatewayUseCmdToLaunch, GatewayUseCmdToLaunch);

// Turn on node debugging (for using node-inspector, for example)
int g_bGatewayInspector = 0;
AUTO_CMD_INT(g_bGatewayInspector, GatewayInspector);

// Instead of killing itself, the launching server will instead just try to restart
//   the node application.
int g_bGatewayRestartSlaveIfDead = 0;
AUTO_CMD_INT(g_bGatewayRestartSlaveIfDead, GatewayRestartSlaveIfDead);

// Extra options to pass through to the node application.
char *g_estrGatewayOptions = NULL;
AUTO_CMD_ESTRING(g_estrGatewayOptions, GatewayOptions);

static U32 *s_eaLoginServers;

void CheckProcessExists(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData); // forward decl
void StartWatcher(GatewayWatcher *pWatcher); // forward decl


AUTO_STRUCT;
typedef struct GatewayWatcher
{
	// Defines a bundle of information needed to start up and watch a nodejs
	// based script.

	char *estrName; AST(ESTRING)
		// Name used in logging, alerts, and printfs
	char *estrScript; AST(ESTRING)
		// Relative path to the script (from the Gateway directory)
	char *estrAddToCommandLine; AST(ESTRING)
		// Command line parameters to add to the script.
		// Watcher automatically adds general parameters like --slave and --locked.

	char *estrCommandLine; AST(ESTRING)
		// The final command line used to start the process.

	char *estrShutdownMessage; AST(ESTRING)
		// The most recently provided shutdown message. Only used if the node
		//   process dies.

	GatewayWatcherFlags eFlags;
		// Modify behavior of the Watcher. See GatewayWatcher.h for flags.
	
	// NetLink information. If provided, the Watcher will create a link
	//   and also wait for the process to connect in a reasonable time. If it
	//   doesn't, it is killed and restarted.
	const char *pchOptionNameForIPAndPort;
	int portStart;	// The first port to listen on
	int portEnd;	// The last port to listen on
	int port;		// The port actually assigned.
	PacketCallback *packet_cb; NO_AST
	LinkCallback *connect_cb; NO_AST
	LinkCallback *disconnect_cb; NO_AST
	int user_data_size;

	QueryableProcessHandle *hProc; NO_AST
		// The handle of the process (once it's running)

	bool bConnected;
		// true if we have a NetLink connected to the process.

	// Internal timers to track all the asynchronous stuff
	TimedCallback *timer; NO_AST
	TimedCallback *timerKill; NO_AST

} GatewayWatcher;



////////////////////////////////////////////////////////////////////////
// Helpers for getting the list of LoginServers.
//

static void OnGetServerListResponse(TransactionReturnVal *returnVal, void *userData)
{
	Controller_ServerList *pLoginServerList;
	enumTransactionOutcome eOutcome;

	eOutcome = RemoteCommandCheck_GetServerList(returnVal, &pLoginServerList);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		ea32ClearFast(&s_eaLoginServers);
		EARRAY_FOREACH_BEGIN(pLoginServerList->ppServers, i);
		{
			Controller_SingleServerInfo *serverInfo = pLoginServerList->ppServers[i];
			ea32PushUnique(&s_eaLoginServers, serverInfo->iIP);
		}
		EARRAY_FOREACH_END;

		if(ea32Size(&s_eaLoginServers) == 0)
		{
			ErrorOrAlert("GATEWAY_STARTUP", "No Login Servers found ready.");
		}

		StructDestroy(parse_Controller_ServerList, pLoginServerList);
	}
	else if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		ErrorOrAlert("GATEWAY_STARTUP", "Failure getting list of Login Servers from Controller.");
	}
}

static void FindLoginServers(void)
{
	RemoteCommand_GetServerListEx(objCreateManagedReturnVal(OnGetServerListResponse, NULL), GLOBALTYPE_CONTROLLER, 0, 
		GLOBALTYPE_LOGINSERVER, "ready");
}

////////////////////////////////////////////////////////////////////////

//
// ClearTimers
//
// Shut off and null out the watcher's timers. This should stop any future
// queued calls.
//
static void ClearTimers(GatewayWatcher *pWatcher)
{
	if(pWatcher->timer)
	{
		TimedCallback_Remove(pWatcher->timer);
		pWatcher->timer = NULL;
	}
	if(pWatcher->timerKill)
	{
		TimedCallback_Remove(pWatcher->timerKill);
		pWatcher->timerKill = NULL;
	}
}

//
// TryAgain
//
// Starts another attempt to start the process.
//
static void TryAgain(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	GatewayWatcher *pWatcher = (GatewayWatcher *)userData;
	StartWatcher(pWatcher);
}

//
// CheckProcessConnection
//
// Called every second while waiting for process to connect.
//
static void CheckProcessConnection(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	GatewayWatcher *pWatcher = (GatewayWatcher *)userData;

	if(pWatcher->bConnected)
	{
		ClearTimers(pWatcher);
		pWatcher->timer = TimedCallback_Add(CheckProcessExists, pWatcher, 5.0);
	}
	else
	{
		printf("WARNING: %s started, but hasn't phoned home yet.\n", pWatcher->estrName);
	}
}

//
// KillProcessIfNotConnected
//
// Used to kill the process if we're not connected to a started process after
// a reasonable time (like 10 seconds).
//
static void KillProcessIfNotConnected(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	GatewayWatcher *pWatcher = (GatewayWatcher *)userData;

	ClearTimers(pWatcher);

	if(pWatcher->bConnected)
	{
		// We actually are connected. Yay!
		pWatcher->timer = TimedCallback_Add(CheckProcessExists, pWatcher, 5.0);
		return;
	}

	// This has taken too long. Kill off the stuck process and
	//   clean up to try again.
	if(!QueryableProcessComplete(&pWatcher->hProc, NULL))
	{
		KillQueryableProcess(&pWatcher->hProc);
	}

	if(pWatcher->eFlags & kGatewayWatcherFlags_RestartProcessIfLost)
	{
		CRITICAL_NETOPS_ALERT("GATEWAY_PROCESS", "%s started, but didn't connect back to GatewayServer. Starting a new one in 5 seconds...", pWatcher->estrName);
		TimedCallback_Run(TryAgain, pWatcher, 5.0);
	}
	else
	{
		CRITICAL_NETOPS_ALERT("GATEWAY_PROCESS", "%s started, but didn't connect back to GatewayServer. Killing myself...", pWatcher->estrName);
		exit(-1);
	}
}

//
// CheckProcessExists
//
// Called every 5 seconds for the life of the program to make sure that the
// process still exists. If it doesn't. then a new one is started (or this
// program exits depending on eFlags).
//
static void CheckProcessExists(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	GatewayWatcher *pWatcher = (GatewayWatcher *)userData;

	if(pWatcher->hProc)
	{
		if(QueryableProcessComplete(&pWatcher->hProc, NULL))
		{
			if(pWatcher->eFlags & kGatewayWatcherFlags_RestartProcessIfLost)
			{
				CRITICAL_NETOPS_ALERT("GATEWAY_PROCESS", "%s process vanished. Starting a new one in 5 seconds. (%s)", pWatcher->estrName, pWatcher->estrShutdownMessage);
				TimedCallback_Run(TryAgain, pWatcher, 5.0);
			}
			else
			{
				CRITICAL_NETOPS_ALERT("GATEWAY_PROCESS", "%s process vanished. Killing myself... (%s)", pWatcher->estrName, pWatcher->estrShutdownMessage);
				exit(-1);
			}
		}
	}
}

//
// StartWatcher
//
// Begins the process of starting a process and connecting to it. Sets up the
// command line, spawns the process, opens a port and starts waiting for the
// connection (flags permitting).
//
static void StartWatcher(GatewayWatcher *pWatcher)
{
	int i;
	char *estrPath = NULL;
	char *estrScriptPath = NULL;

	TimedCallback_RemoveByFunction(TryAgain); // Just in case someone else has queued up another try.

	if(pWatcher->hProc)
		return;

	// Find a port to listen on and start listening.
	if(!pWatcher->port)
	{
		NetListen *listen = NULL;
		int port;
		for(port = pWatcher->portStart; listen == NULL && port <= pWatcher->portEnd; port++)
		{
			listen = commListen(commDefault(),
				LINKTYPE_UNSPEC, // Maybe should be LINKTYPE_SHARD_NONCRITICAL_500K
				LINK_NO_COMPRESS|LINK_FORCE_FLUSH,
				port,
				pWatcher->packet_cb,
				pWatcher->connect_cb,
				pWatcher->disconnect_cb,
				pWatcher->user_data_size);

			if(listen)
			{
				printf("Listening for proxies on port %d.", port);
				pWatcher->port = port;
			}
		}

		if(!listen)
		{
			CRITICAL_NETOPS_ALERT("GATEWAY_PROCESS",
				"Unable to find an available port between %d and %d (inclusive) to listen on.  Killing myself...", GATEWAYSERVER_PORT_START, GATEWAYSERVER_PORT_END);
			exit(-1);
		}
	}

	estrCopy2(&pWatcher->estrShutdownMessage, "No reason given."); // Clear out old shutdown message (for restarts)

	if(pWatcher->eFlags & kGatewayWatcherFlags_AddLoginServers)
	{
		if(ea32Size(&s_eaLoginServers) == 0)
		{
			// We need at least one login server to do anything. Look them up and
			//   try to start again in a second.
			FindLoginServers();
			TimedCallback_Run(TryAgain, pWatcher, 1.0);
			return;
		}
	}

	if(!gateway_FindDeployDir(&estrPath))
	{
		CRITICAL_NETOPS_ALERT("GATEWAY_STARTUP",
			"Unable to find Gateway deployment directory here [%s]. Trying again in 60 seconds...", estrPath);
		TimedCallback_Run(TryAgain, pWatcher, 60);
		return;
	}

	if(!gateway_FindScriptDir(&estrScriptPath))
	{
		CRITICAL_NETOPS_ALERT("GATEWAY_STARTUP",
			"Unable to find Gateway script directory here [%s]. Trying again in 60 seconds...", estrScriptPath);
		TimedCallback_Run(TryAgain, pWatcher, 60);
		return;
	}

	// Build the command line
	estrClear(&pWatcher->estrCommandLine);

	if(pWatcher->eFlags & kGatewayWatcherFlags_UseShellToLaunch)
		estrConcatf(&pWatcher->estrCommandLine, "cmd /k ");

	estrConcatf(&pWatcher->estrCommandLine, "%s\\%s %s %s\\%s",
		estrPath, "bin\\node.exe",
		g_bGatewayInspector ? "--debug" : "",
		estrScriptPath, pWatcher->estrScript);

	if(pWatcher->eFlags & kGatewayWatcherFlags_AddLoginServers)
	{
		for(i = ea32Size(&s_eaLoginServers)-1; i >= 0; i--)
		{
			estrConcatf(&pWatcher->estrCommandLine, " --ipLoginServer %s --portLoginServer %d",
				makeIpStr(s_eaLoginServers[i]), DEFAULT_LOGINSERVER_GATEWAY_LOGIN_PORT);
		}
	}

	if(pWatcher->eFlags & kGatewayWatcherFlags_AddShardName)
	{
		estrConcatf(&pWatcher->estrCommandLine, " --shardName %s", GetShardNameFromShardInfoString());
	}
	
	if(gateway_IsLocked())
	{
		estrConcatf(&pWatcher->estrCommandLine, " --locked");
	}

	estrConcatf(&pWatcher->estrCommandLine, " --slave");

	estrConcatf(&pWatcher->estrCommandLine, " --ip%s %s --port%s %d",
		pWatcher->pchOptionNameForIPAndPort, makeIpStr(getHostLocalIp()),
		pWatcher->pchOptionNameForIPAndPort, pWatcher->port);

	estrConcatf(&pWatcher->estrCommandLine, " --buildVersion \"%s\"", GetUsefulVersionString());

	if(pWatcher->estrAddToCommandLine)
	{
		estrConcatf(&pWatcher->estrCommandLine, " %s", pWatcher->estrAddToCommandLine);
	}

	// Leave this last so options can be overridden.
	if(g_estrGatewayOptions)
	{
		estrConcatf(&pWatcher->estrCommandLine, " %s", g_estrGatewayOptions);
	}

	printf("Starting new %s.\n", pWatcher->estrName);
	printf("%s\n", pWatcher->estrCommandLine);
	pWatcher->hProc = StartQueryableProcess(pWatcher->estrCommandLine, NULL, false, true, false, NULL);


	if(pWatcher->hProc && pWatcher->connect_cb)
	{
		if(pWatcher->eFlags & kGatewayWatcherFlags_WatchConnection)
		{
			pWatcher->timer = TimedCallback_Add(CheckProcessConnection, pWatcher, 1.0);
			pWatcher->timerKill = TimedCallback_Run(KillProcessIfNotConnected, pWatcher, 10.0);

 			printf("Waiting for %s to connect.\n", pWatcher->estrName);
		}
		else
		{
			pWatcher->timer = TimedCallback_Add(CheckProcessExists, pWatcher, 5.0);
		}
	}
	else if(pWatcher->hProc)
	{
		pWatcher->timer = TimedCallback_Add(CheckProcessExists, pWatcher, 5.0);
	}
	else
	{
		CRITICAL_NETOPS_ALERT("GATEWAY_STARTUP",
			"Unable to launch %s with command [%s]. Trying again in 5 seconds.", pWatcher->estrName, pWatcher->estrCommandLine);
		TimedCallback_Run(TryAgain, pWatcher, 5.0);
	}
}

//
// gateway_CreateAndStartWatcher
//
// Creates and starts a new Watcher with all the parameters given.
//
GatewayWatcher *gateway_CreateAndStartWatcher(char *pchName,
	char *pchScript, char *pchAddToCommandLine, int eGatewayWatcherFlags,
	const char *pchOptionNameForIPAndPort, int portStart, int portEnd,
	PacketCallback packet_cb, LinkCallback connect_cb, LinkCallback disconnect_cb,
	int user_data_size)
{
	GatewayWatcher *pWatcher = StructAlloc(parse_GatewayWatcher);

	estrCopy2(&pWatcher->estrName, pchName);
	estrCopy2(&pWatcher->estrScript, pchScript);
	estrCopy2(&pWatcher->estrAddToCommandLine, pchAddToCommandLine);
	estrCopy2(&pWatcher->estrShutdownMessage, "No reason given.");

	pWatcher->eFlags = eGatewayWatcherFlags;
	if(g_bGatewayUseCmdToLaunch)
		pWatcher->eFlags |= kGatewayWatcherFlags_UseShellToLaunch;
	if(g_bGatewayRestartSlaveIfDead)
		pWatcher->eFlags |= kGatewayWatcherFlags_RestartProcessIfLost;

	pWatcher->pchOptionNameForIPAndPort = pchOptionNameForIPAndPort;
	pWatcher->portStart = portStart;
	pWatcher->portEnd = portEnd;
	pWatcher->port = 0;

	pWatcher->packet_cb = packet_cb;
	pWatcher->connect_cb = connect_cb;
	pWatcher->disconnect_cb = disconnect_cb;
	pWatcher->user_data_size = user_data_size;

	StartWatcher(pWatcher);

	return pWatcher;
}

//
// gateway_DestroyWatcher
//
// Destroys the given watcher, killing the watched process.
// There's not much use for this function in general use.
//
void gateway_DestroyWatcher(GatewayWatcher *pWatcher)
{
	if(pWatcher)
	{
		ClearTimers(pWatcher);

		if(!QueryableProcessComplete(&pWatcher->hProc, NULL))
		{
			KillQueryableProcess(&pWatcher->hProc);
		}

		StructDestroy(parse_GatewayWatcher, pWatcher);
	}
}

//
// gateway_WatcherConnected
//
// MUST be called by the caller when the process finally makes a connection.
// Probably the first or second line in the caller's connection handler.
//
void gateway_WatcherConnected(GatewayWatcher *pWatcher)
{
	pWatcher->bConnected = true;
}

//
// gateway_WatcherDisconnected
//
// MUST be called by the caller when the process disconnects.
// Probably the first or second line in the caller's disconnect handler.
// Will attempt to start the process back up again, or exit as appropriate
//
void gateway_WatcherDisconnected(GatewayWatcher *pWatcher)
{
	if(pWatcher->bConnected)
	{
		// We've been disconnected.
		ClearTimers(pWatcher);

		if(pWatcher->hProc && !QueryableProcessComplete(&pWatcher->hProc, NULL))
		{
			KillQueryableProcess(&pWatcher->hProc);
		}

		printf("Disconnect Reason: %s\n", pWatcher->estrShutdownMessage);

		pWatcher->bConnected = false;
	}

	if(pWatcher->eFlags & kGatewayWatcherFlags_RestartProcessIfLost)
	{
		CRITICAL_NETOPS_ALERT("GATEWAY_PROCESS", "%s disconnected. Starting a new one... (%s)", pWatcher->estrName, pWatcher->estrShutdownMessage);
		TimedCallback_Run(TryAgain, pWatcher, 0.0);
	}
	else
	{
		CRITICAL_NETOPS_ALERT("GATEWAY_PROCESS", "%s disconnected. Killing myself... (%s)", pWatcher->estrName, pWatcher->estrShutdownMessage);
		exit(-1);
	}
}

//
// gateway_SetShutdownMessage
//
// Sets a message which will be reported to the monitor if the process dies.
//
void gateway_SetShutdownMessage(GatewayWatcher *pWatcher, const char *pch)
{
	estrCopy2(&pWatcher->estrShutdownMessage, pch);
}


#include "AutoGen/GatewayWatcher_c_ast.c"
#include "AutoGen/GatewayWatcher_h_ast.c"

/* End of File */
