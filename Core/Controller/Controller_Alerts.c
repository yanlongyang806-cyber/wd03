#include "controller.h"
#include "alerts.h"
#include "estring.h"
#include "sock.h"
#include "timing.h"
#include "file.h"
#include "ServerLib.h"
#include "StatusReporting.h"
#include "GlobalTypes.h"
#include "utils.h"
#include "ContinuousBuilderSupport.h"
#include "Controller_Alerts_c_ast.h"
#include "svrGlobalInfo_h_Ast.h"
#include "ControllerPub_h_ast.h"
#include "Logging.h"
#include "utilitiesLib.h"
#include "controller_utils.h"
#include "cmdparse.h"
#include "alerts_h_ast.h"
#include "TimedCallback.h"
#include "ResourceInfo.h"
#include "Controller_ClusterController.h"
#include "SentryServerComm.h"
#include "StringCache.h"

//note that the terse comments actually attached to the commands are in the format netops likes, since they
//end up in controllerAutoSettings

//if a gameserver says it's dying gracefully, but then is still around after this many seconds, something is wrong...
//alert.

//__CATEGORY Code relating to gameserver stalls at startup and while running
//(Seconds) ALERT - Game Server stall after a graceful shutdown
int gDelayBeforeAlertingGraceful = 600;
AUTO_CMD_INT(gDelayBeforeAlertingGraceful, DelayBeforeAlertingGraceful) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);



//if a gameserver says it's dying gracefully, but then is still around after this many seconds, something is wrong...
//kill it.

//(Seconds) KILL - Game Server stall after a graceful shutdown
int gDelayBeforeKillingGraceful = 1200;
AUTO_CMD_INT(gDelayBeforeKillingGraceful, DelayBeforeKillingGraceful) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);


//if a gameserver stalls for this many seconds, kill it

//(Seconds) KILL - Game Server stall (ignore/leaves the first one killed per machine)
int gDelayBeforeKillingStalledGameserver = 60;
AUTO_CMD_INT(gDelayBeforeKillingStalledGameserver, DelayBeforeKillingStalledGameserver) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);

//If nonzero, leave one (per machine) stalled gameserver running this long so it can be debugged
int giHoursToLeaveStalledGameServerRunning = 0;
AUTO_CMD_INT(giHoursToLeaveStalledGameServerRunning, HoursToLeaveStalledGameServerRunning);



//if a gameserver starts up and then doesn't contact the controller for this many seconds,
//kill it (alerts when half this time has expired)

//(Seconds) KILL - ALERT at half time - Game Server startup timer
int gDelayWhenStartingGameServerBeforeFail = 1200;
AUTO_CMD_INT(gDelayWhenStartingGameServerBeforeFail, DelayWhenStartingGameServerBeforeFail) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);


//after a gameserver has successfully handshaked with the mapmanager, if it goes this many seconds without
//contacting us, kill it

//(Seconds) KILL - Game Server stall after a successful Map Manager handshake
int gDelayAfterHandshakingWithMapManagerBeforeFail = 1200;
AUTO_CMD_INT(gDelayAfterHandshakingWithMapManagerBeforeFail, DelayAfterHandshakingWithMapManagerBeforeFail) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);


//When a gameserver is stalled and goin gto be killed, should we get a dump first?


//(Seconds) after requesting a stalled GS dump on a machine, don't request another one on that machine for this long
int giDelayBetweenStalledGSDumpsOneMachine = 180;
AUTO_CMD_INT(giDelayBetweenStalledGSDumpsOneMachine, DelayBetweenStalledGSDumpsOneMachine) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);

//(Boolean) Collect a dump from stalled Game Servers before killing?
bool gbGetDumpsFromStalledGameServers = true;
AUTO_CMD_INT(gbGetDumpsFromStalledGameServers, GetDumpsFromStalledGameServers) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);


//alert whenever a gameserver lags this many seconds

//(Seconds) ALERT - Game Server stall
int gDelayBeforeNonRespondingAlert = 15;
AUTO_CMD_INT(gDelayBeforeNonRespondingAlert, DelayBeforeNonRespondingAlert) ACMD_CONTROLLER_AUTO_SETTING(GS_STALLS);


//when we get a bunch of GS crashes all at once, generate a special alert. A bunch of settings to tune that

//__CATEGORY Settings for generating the MANY_GS_CRASHES alert on this shard
//when we get this many GS crashes, generate MANY_GS_CRASHES alert. 0 = disable
static int siNumGSCrashesForManyGSCrashesAlert = 0;
AUTO_CMD_INT(siNumGSCrashesForManyGSCrashesAlert, NumGSCrashesForManyGSCrashesAlert) ACMD_CONTROLLER_AUTO_SETTING(MANY_GS_CRASHES_ALERT) ACMD_CALLBACK(ManyGSCrashEventsCB);

//if the crashes happen within this many seconds, generate MANY_GS_CRASHES alert
static int siSecondsForManyGSCrashesAlert = 300;
AUTO_CMD_INT(siSecondsForManyGSCrashesAlert, SecondsForManyGSCrashesAlert) ACMD_CONTROLLER_AUTO_SETTING(MANY_GS_CRASHES_ALERT) ACMD_CALLBACK(ManyGSCrashEventsCB);

//after generating MANY_GS_CRASHES alert, wait this many seconds before generating it again
static int siSecondsBetweenManyGSCrashesAlerts = 900;
AUTO_CMD_INT(siSecondsBetweenManyGSCrashesAlerts, SecondsBetweenManyGSCrashesAlerts) ACMD_CONTROLLER_AUTO_SETTING(MANY_GS_CRASHES_ALERT) ACMD_CALLBACK(ManyGSCrashEventsCB);



//always send alerts with sendalert.exe (for testing purposes)
static bool sbForceUseSendAlert = false;
AUTO_CMD_INT(sbForceUseSendAlert, ForceUseSendAlert);

static bool sbGSCrashesSettingsHaveChanged = false;

static SimpleEventCounter *spManyGSCrashesCounter = NULL;
static char *spManyGSCrashesString = NULL;


static bool sbAlertsWhichTriggerXperfChanged = true;
static char *spAlertsWhichTriggerXperf = NULL;

//Comma-separated list of alert keys. When any of these alert keys happens, trigger xperf on that machine
AUTO_CMD_ESTRING(spAlertsWhichTriggerXperf, AlertsWhichTriggerXperf) ACMD_CALLBACK(AlertsWhichTriggerXperfCB ) ACMD_CONTROLLER_AUTO_SETTING(MiscAlerts);


void AlertsWhichTriggerXperfCB(CMDARGS)
{
	sbAlertsWhichTriggerXperfChanged = true;
}

static char **sppAlertsWhichTriggerXperfEarray = NULL;

static bool AlertKeyTriggersXperf(const char *pKey)
{
	int i;
	
	if (sbAlertsWhichTriggerXperfChanged)
	{
		eaDestroyEx(&sppAlertsWhichTriggerXperfEarray, NULL);
		DivideString(spAlertsWhichTriggerXperf, ",", &sppAlertsWhichTriggerXperfEarray, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
		sbAlertsWhichTriggerXperfChanged = false;
	}

	for (i = 0; i < eaSize(&sppAlertsWhichTriggerXperfEarray); i++)
	{
		if (strstri(sppAlertsWhichTriggerXperfEarray[i], pKey))
		{
			return true;
		}
	}
		
	return false;
}


void ManyGSCrashEventsCB(CMDARGS)
{
	sbGSCrashesSettingsHaveChanged = true;
}

static char *GetManyGSCrashEventString(void)
{
	if (!spManyGSCrashesString)
	{
		char *pDuration1 = NULL;
		char *pDuration2 = NULL;
		timeSecondsDurationToPrettyEString(siSecondsForManyGSCrashesAlert, &pDuration1);
		timeSecondsDurationToPrettyEString(siSecondsBetweenManyGSCrashesAlerts, &pDuration2);
		estrPrintf(&spManyGSCrashesString, "Got %d GS crashes in last %s. (Alert won't repeated for %s)",
			siNumGSCrashesForManyGSCrashesAlert, pDuration1, pDuration2);
		estrDestroy(&pDuration1);
		estrDestroy(&pDuration2);
	}

	return spManyGSCrashesString;
}

static SimpleEventCounter *GetManyGSCrashEventCounter(void)
{
	if (sbGSCrashesSettingsHaveChanged)
	{
		sbGSCrashesSettingsHaveChanged = false;
		SimpleEventCounter_Destroy(&spManyGSCrashesCounter);
		estrDestroy(&spManyGSCrashesString);
	}

	if (!siNumGSCrashesForManyGSCrashesAlert)
	{
		return NULL;
	}

	if (!spManyGSCrashesCounter)
	{
		spManyGSCrashesCounter = SimpleEventCounter_Create(siNumGSCrashesForManyGSCrashesAlert, siSecondsForManyGSCrashesAlert,
			siSecondsBetweenManyGSCrashesAlerts);
	}

	return spManyGSCrashesCounter;
}


TrackedMachineState *GetMachineFromAlert(Alert *pAlert)
{
	TrackedServerState *pServer;

	//first check the object to see if it's a machine
	if (pAlert->eContainerTypeOfObject == GLOBALTYPE_MACHINE)
	{
		return &gTrackedMachines[pAlert->iIDOfObject];
	}

	//now check to see if the server type is GLOBALTYPE_CONTAINER. If so, this alert was generated
	//on the controller itself and the server type we are really interested in is the object of the alert, not the server
	if (pAlert->eContainerTypeOfServer == GLOBALTYPE_CONTROLLER)
	{
		pServer = FindServerFromID(pAlert->eContainerTypeOfObject, pAlert->iIDOfObject);

		if (pServer)
		{
			return pServer->pMachine;
		}

		return spLocalMachine;
	}

	//try the actual claimed server type
	pServer = FindServerFromID(pAlert->eContainerTypeOfServer, pAlert->iIDOfServer);

	if (pServer)
	{
		return pServer->pMachine;
	}

	return NULL;
}


void Controller_FixupNewAlert(Alert *pAlert)
{

	TrackedMachineState *pMachine = GetMachineFromAlert(pAlert); 
	TrackedServerState *pServer = NULL;
	
	if (pAlert->eContainerTypeOfObject < 0 || pAlert->eContainerTypeOfObject >= GLOBALTYPE_MAXTYPES)
	{
		ErrorOrAlertDeferred(false, "BAD_ALERT", "The controller has gotten an alert with string %s from an invalid server type",
			pAlert->pString);
	}
	else
	{
		pServer = FindServerFromID( pAlert->eContainerTypeOfObject, pAlert->iIDOfObject);
	}

	if (pMachine)
	{
		estrPrintf(&pAlert->pVNC, "<a href=\"cryptic://vnc/%s\">VNC</a>", pMachine->VNCString);

		if (!(pAlert->pMachineName && pAlert->pMachineName[0]))
		{
			SAFE_FREE(pAlert->pMachineName);
			pAlert->pMachineName = strdup(pMachine->machineName);
		}
	}

	if (pServer && pServer->eContainerType == GLOBALTYPE_GAMESERVER && pServer->pGameServerSpecificInfo)
	{
		pAlert->pMapName = strdup(pServer->pGameServerSpecificInfo->mapName);
	}


	if (pAlert->iErrorID && pAlert->iErrorID != -1)
	{
		estrPrintf(&pAlert->pErrorLink, "<a href=\"http://%s/detail?id=%u\">ErrorTracker</a>",
			getErrorTracker(), pAlert->iErrorID);
	}

	if (!pAlert->iLifespan)
	{
		if (strstri(pAlert->pKey, CRASHEDORASSERTED))
		{
			estrPrintf(&pAlert->pCommand1, "AcknowledgeAndKill %u %u %u $NORETURN $COMMANDNAME(AcknowledgeAndKill)",
				pAlert->iAlertUID, pAlert->eContainerTypeOfServer, pAlert->iIDOfServer);

			//generate special alert if many gameservers have crashed
			if (pAlert->eContainerTypeOfServer == GLOBALTYPE_GAMESERVER)
			{
				SimpleEventCounter *pCounter = GetManyGSCrashEventCounter();
				if (pCounter && SimpleEventCounter_ItHappened(pCounter, timeSecondsSince2000()))
				{
					CRITICAL_NETOPS_ALERT("MANY_GS_CRASHES", "%s", GetManyGSCrashEventString());
				}
			}
		}
		else
		{
			estrPrintf(&pAlert->pCommand1, "Acknowledge %u $NORETURN $COMMANDNAME(Acknowledge)",
				pAlert->iAlertUID);
			estrPrintf(&pAlert->pCommand2, "AcknowledgeAll %s $CONFIRM(really acknowledge all alerts of type %s?) $NORETURN $COMMANDNAME(AcknolwedgeAll)",
				pAlert->pKey, pAlert->pKey);
		}
	}
	else if (pAlert->pKey == ALERTKEY_GAMESERVERNOTRESPONDING)
	{
		estrPrintf(&pAlert->pCommand1, "AcknowledgeAndKill %u %u %u $NORETURN $COMMANDNAME(AcknowledgeAndKill)",
			pAlert->iAlertUID, pAlert->eContainerTypeOfServer, pAlert->iIDOfServer);
	}


	if (pAlert->iLifespan != 0)
	{
		if (pAlert->eContainerTypeOfObject == GLOBALTYPE_MACHINE)
		{
			gTrackedMachines[pAlert->iIDOfObject].iNumActiveStateBasedAlerts++;
		}
		else
		{
			if (pServer)
			{
				pServer->iNumActiveStateBasedAlerts++;
			}
		}
	}

	if (pServer)
	{
		pAlert->iPidOfServer = pServer->PID;
	}

	if (sbForceUseSendAlert || (isProductionMode() && StatusReporting_GetState() != STATUSREPORTING_CONNECTED
		&& pAlert->eLevel == ALERTLEVEL_CRITICAL && pAlert->eCategory == ALERTCATEGORY_NETOPS
		&& pAlert->iLifespan == 0 && !g_isContinuousBuilder))
	{
		static SimpleEventCounter *pMessageBoxEventCounter = NULL; 
	
		static bool bSuppressAll = false;

		if (!pMessageBoxEventCounter)
		{
			pMessageBoxEventCounter = SimpleEventCounter_Create(10, 60, 600);
		}



		if (SimpleEventCounter_ItHappened(pMessageBoxEventCounter, timeSecondsSince2000()))
		{
			if (!bSuppressAll)
			{
				bSuppressAll = true;

				Controller_MessageBoxError("TOO_MANY_ALERTS", "Alerts are coming in faster than we feel comfortable creating message boxes. You will not see this warning or any alerts again. Look at the controller in servermonitor to see all alerts");
			}
		}
		
		if (!bSuppressAll)
		{
			Controller_MessageBoxError(pAlert->pKey, pAlert->pString);


			//if we WANT To be doing status reporting but just aren't connected yet, use the external SendAlert utility to
			//try to send this alert
			if (StatusReporting_GetState() == STATUSREPORTING_NOT_CONNECTED || sbForceUseSendAlert )
			{
				char *pCmdLine = NULL;
				char *pFullAlertString = NULL;
				char *pFullAlertStringSuperSec = NULL;

				estrStackCreate(&pCmdLine);
				estrStackCreate(&pFullAlertString);
				estrStackCreate(&pFullAlertStringSuperSec);

				ParserWriteText(&pFullAlertString, parse_Alert, pAlert, 0, 0, 0);
				estrSuperEscapeString(&pFullAlertStringSuperSec, pFullAlertString);

				estrPrintf(&pCmdLine, "sendAlert -controllerTrackerName %s -criticalSystemName %s -SuperEscapedFullAlert %s",
					StatusReporting_GetControllerTrackerName(), StatusReporting_GetMyName(), pFullAlertStringSuperSec);

				system_detach(pCmdLine, true, true);


				estrDestroy(&pCmdLine);
				estrDestroy(&pFullAlertString);
				estrDestroy(&pFullAlertStringSuperSec);
			}
		}
	}

	if (pServer)
	{
		char *pServerString = NULL;
		ParserWriteText(&pServerString, parse_TrackedServerState, pServer, 0, 0, TOK_USEROPTIONBIT_1);
		objLog(LOG_SERVERALERTS, pServer->eContainerType, pServer->iContainerID, 0, NULL, NULL, NULL, pAlert->pKey, NULL, "%s", pServerString);
		estrDestroy(&pServerString);
	}

	if (AlertKeyTriggersXperf(pAlert->pKey))
	{
		Controller_DoXperfDumpOnMachine(pServer->pMachine, "Alert_%s", pAlert->pKey);
	}
}

bool Controller_AuxAlertCB(const char *pKey, const char *pString, enumAlertLevel eLevel, enumAlertCategory eCategory, int iLifespan,
	  GlobalType eContainerTypeOfObject, ContainerID iIDOfObject, GlobalType eContainerTypeOfServer, ContainerID iIDOfServer, const char *pMachineName,
	  int iErrorID )
{
	TrackedServerState *pServer;


	pServer = FindServerFromID( eContainerTypeOfServer, iIDOfServer);
	if (pServer)
	{
		if (pServer->bAlertsMuted)
		{
			return true;
		}
	}
	



	return false;
}

void Controller_StaticAlertOffCB(Alert *pAlert)
{
	if (pAlert->eContainerTypeOfObject == GLOBALTYPE_MACHINE)
	{
		TrackedMachineState *pMachine = &gTrackedMachines[pAlert->iIDOfObject];

		if (pMachine->iNumActiveStateBasedAlerts)
		{
			pMachine->iNumActiveStateBasedAlerts--;
		}
	}
	else
	{
		TrackedServerState *pServer = FindServerFromID( pAlert->eContainerTypeOfObject, pAlert->iIDOfObject);
		if (pServer && pServer->iNumActiveStateBasedAlerts)
		{
			pServer->iNumActiveStateBasedAlerts--;
		}
	}
}

AUTO_RUN;
void Controller_InitAlerts(void)
{
	AddFixupAlertCB(Controller_FixupNewAlert);
	AddAlertRedirectionCB(Controller_AuxAlertCB);
	SetStateBasedAlertOffCB(Controller_StaticAlertOffCB);
}


AUTO_COMMAND;
void AcknowledgeAndKill(U32 iAlertID, GlobalType eType, ContainerID iID)
{
	TrackedServerState *pServer;
	AcknowledgeAlertByUID(iAlertID);

	pServer = FindServerFromID(eType, iID);

	if (pServer)
	{
		pServer->bKilledIntentionally = true;
		pServer->bRecreateDespiteIntentionalKill = true;
		KillServer(pServer, STACK_SPRINTF("Alert %u being acknowledged", iAlertID));
	}
}

AUTO_COMMAND;
void Acknowledge(U32 iAlertID)
{
	AcknowledgeAlertByUID(iAlertID);
}

AUTO_COMMAND;
void AcknowledgeAll(char *pKey)
{
	AcknowledgeAllAlertsByKey(pKey);

}

#define GS_PLAYERS(pServer) ( pServer->pGameServerSpecificInfo ? pServer->pGameServerSpecificInfo->iNumPlayers : 0)

typedef enum MadeGSDumpState
{
	NODUMP_NOTPRODMODE,
	NODUMP_FAILED,
	NODUMP_FEATUREOFF,
	NODUMP_NOTFIRSTTIME,
	NODUMP_THROTTLED,
	MADEDUMP
} MadeGSDumpState;


void CheckGameServerInfoForAlerts(TrackedServerState *pServer)
{
	U32 iCurTimeToUse = GetCurTimeToUseForStallChecking(pServer->pMachine);

	ConditionChecker_BetweenConditionUpdates(pServer->pAlertsConditionChecker);

	if (pServer->bKilledGracefully)
	{
		if (pServer->pGameServerSpecificInfo && pServer->pGameServerSpecificInfo->iLastContact)
		{
			U32 iLastContact = pServer->pGameServerSpecificInfo->iLastContact;
			if (isProductionMode() && iLastContact > 0 && iLastContact < iCurTimeToUse - gDelayBeforeKillingGraceful - pServer->pMachine->iLastLauncherLag_ToUse)
			{
				if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker, "GS_GRACEFUL_SLOW_KILL"))
				{
					Controller_DoXperfDumpOnMachine(pServer->pMachine, "GSDieGracefulKill");
					TriggerAlert("GS_GRACEFUL_SLOW_KILL", STACK_SPRINTF("GameServer (%d) (machine %s) said it was dying gracefully, but was still here after %d seconds. Killing it", 
						pServer->iContainerID, pServer->pMachine->machineName, gDelayBeforeKillingGraceful),
						ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);

						//using "KillServerbyIDs" rather than "KillServer", because we don't want side effects to get confusing
						KillServerByIDs(pServer->pMachine, pServer->eContainerType, pServer->iContainerID, "Died gracefully, then never really died");
						StopTrackingServer(pServer, "Died gracefully, then never really died", true, true);

						return;		
				}
			}
			else if (isProductionMode() && iLastContact > 0 && iLastContact < iCurTimeToUse - gDelayBeforeAlertingGraceful - pServer->pMachine->iLastLauncherLag_ToUse && !pServer->bAlreadyAlertedSlowGracefulDeath)
			{
				if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker, "GS_GRACEFUL_SLOW_ALERT"))
				{			
					Controller_DoXperfDumpOnMachine(pServer->pMachine, "GSDieGraceful");
					TriggerAlert("GS_GRACEFUL_SLOW_ALERT", STACK_SPRINTF("AUTO_EMAIL:ALERTS_TIME_SENSITIVE GameServer (%d) (machine %s) said it was dying gracefully, but was still here after %d seconds. Not killing it yet... please investigate", 
						pServer->iContainerID, pServer->pMachine->machineName, gDelayBeforeAlertingGraceful),
						ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);

					pServer->bAlreadyAlertedSlowGracefulDeath = true;
				}
			}
		}

		return;
	}

	if (pServer->iNoLagOrInactivityAlertsUntilTime)
	{
		if (pServer->iNoLagOrInactivityAlertsUntilTime >  timeSecondsSince2000())
		{
			return;
		}
		else
		{
			if (pServer->pGameServerSpecificInfo)
			{
				pServer->pGameServerSpecificInfo->iLastContact = timeSecondsSince2000();
			}

			pServer->iNoLagOrInactivityAlertsUntilTime = 0;
		}
	}

	if (pServer->pGameServerSpecificInfo && pServer->pGameServerSpecificInfo->iLastContact)
	{
		U32 iLastContact = pServer->pGameServerSpecificInfo->iLastContact;

		if (isProductionMode() && iLastContact > 0 && iLastContact < iCurTimeToUse - gDelayBeforeKillingStalledGameserver - pServer->pMachine->iLastLauncherLag_ToUse)
		{
			if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker,ALERTKEY_KILLINGNONRESPONDINGGAMESERVER))
			{			
			
				
				if (!giHoursToLeaveStalledGameServerRunning || pServer->pMachine->iPIDOfIgnoredServer)
				{
					char *pErrorString = NULL;
					estrPrintf(&pErrorString, "Gameserver (%d) (players %d) (machine %s) was non-responsive for > %d seconds. %s. Recent Activity:\n%s", 
						pServer->iContainerID, GS_PLAYERS(pServer), pServer->pMachine->machineName, gDelayBeforeKillingStalledGameserver, 
						!giHoursToLeaveStalledGameServerRunning ? "To leave stalled servers running so they can be debugged, set controller command HoursToLeaveStalledGameServerRunning" : "A stalled server on that machine was already left running, only one allowed per machine",
						SimpleServerLog_GetLogString(pServer));

					Controller_DoXperfDumpOnMachine(pServer->pMachine, "KillingNonResponsiveGS");
						TriggerAlert(ALERTKEY_KILLINGNONRESPONDINGGAMESERVER, pErrorString,
							ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);
						pServer->bKilledIntentionally = true;
						KillServerByIDs(pServer->pMachine, pServer->eContainerType, pServer->iContainerID, "KillingNonResponsiveGS");
						StopTrackingServer(pServer, pErrorString, true, true);
						estrDestroy(&pErrorString);
				}
				else
				{
	
					char *pErrorString = NULL;
					char *pDurationString = NULL;
					int iIgnoreTime = giHoursToLeaveStalledGameServerRunning * 60 * 60;
					timeSecondsDurationToPrettyEString(iIgnoreTime, &pDurationString);
					estrPrintf(&pErrorString, "Gameserver (%d) (players %d) (machine %s) was non-responsive for > %d seconds. We're leaving it running but ignored/suspended for up to %s so it can be debugged (only one at a time per machine). Recent Activity:\n%s", 
						pServer->iContainerID, GS_PLAYERS(pServer), pServer->pMachine->machineName, gDelayBeforeKillingStalledGameserver, pDurationString, SimpleServerLog_GetLogString(pServer));

					Controller_DoXperfDumpOnMachine(pServer->pMachine, "NonResponsiveGS_Ignoring");
						TriggerAlert("IGNORING_NONRESPONDING_GS", pErrorString,
						ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);
					pServer->bKilledIntentionally = true;
					TellLauncherToIgnoreServer(pServer, iIgnoreTime);
					StopTrackingServer(pServer, pErrorString, true, true);
					estrDestroy(&pErrorString);
					estrDestroy(&pDurationString);
				}
				

				
			}
		}
		else if (iLastContact > 0 && iLastContact < iCurTimeToUse - gDelayBeforeNonRespondingAlert - pServer->pMachine->iLastLauncherLag_ToUse)
		{
			if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker,ALERTKEY_GAMESERVERNOTRESPONDING))
			{	
				int iOutOfContactTime = iCurTimeToUse - iLastContact;
				char *pDumpString = NULL;
				char *pAlertString = NULL;
				char *pMadeDumpComment = NULL;

				MadeGSDumpState eMadeDumpState = NODUMP_NOTPRODMODE;



				if (iOutOfContactTime > giLongestDelayedGameServerTime)
				{
					giLongestDelayedGameServerTime = iOutOfContactTime;
				}

				if ( isProductionMode())
				{
					if (!gbGetDumpsFromStalledGameServers)
					{
						eMadeDumpState = NODUMP_FEATUREOFF;
					}
					else if (pServer->pMachine->iLastTimeStalledGSDump > timeSecondsSince2000() - giDelayBetweenStalledGSDumpsOneMachine)
					{
						eMadeDumpState = NODUMP_THROTTLED;
					}
					else if (pServer->bAlreadyDumpedForStalling)
					{
						eMadeDumpState = NODUMP_NOTFIRSTTIME;
					}
					else if (!(pServer->PID && pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER] && pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink))
					{
						eMadeDumpState = NODUMP_FAILED;
					}
					else
					{
						Packet *pPak;
						pServer->pMachine->iLastTimeStalledGSDump = timeSecondsSince2000();
						estrPrintf(&pDumpString, "GSNotResponding_%s_%u_%u", pServer->pMachine->machineName, pServer->iContainerID, timeSecondsSince2000());
						pPak = pktCreate(pServer->pMachine->pServersByType[GLOBALTYPE_LAUNCHER]->pLink, LAUNCHERQUERY_GETAMANUALDUMP);
						pktSendBits(pPak, 32, pServer->PID);
						pktSendString(pPak, pDumpString);
						if (gServerTypeInfo[pServer->eContainerType].executableName64_original[0])
						{
							pktSendBits(pPak, 1, 0);
						}
						else
						{
							pktSendBits(pPak, 1, 1);
						}
						pktSend(&pPak);
						pServer->bAlreadyDumpedForStalling = true;
						eMadeDumpState = MADEDUMP;
					}
				}


				if (!RetriggerAlertIfActive(ALERTKEY_GAMESERVERNOTRESPONDING, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID))
				{
					switch (eMadeDumpState)
					{
					xcase NODUMP_NOTPRODMODE:
						estrPrintf(&pMadeDumpComment, "No dump, not production mode");
					xcase NODUMP_FAILED:
						estrPrintf(&pMadeDumpComment, "Wanted to make a dump but couldn't... This is very odd, did the launcher just crash or something?");
					xcase NODUMP_FEATUREOFF:
						estrPrintf(&pMadeDumpComment, "No dump made, GetDumpsFromStalledGameServers is OFF. Turn it on through controllerAutoSettings if desired");
					xcase NODUMP_NOTFIRSTTIME:
						estrPrintf(&pMadeDumpComment, "No dump made, one was already made for this specific gameserver this run. Should be called GSNotResponding_%s_%u_...", pServer->pMachine->machineName, pServer->iContainerID);
					xcase MADEDUMP:
						estrPrintf(&pMadeDumpComment, "Attempted to generate manual dump file on %s... %s", pServer->pMachine->machineName, pDumpString);
					xcase NODUMP_THROTTLED:
						estrPrintf(&pMadeDumpComment, "No dump made, one was made within last %d seconds on that machine", giDelayBetweenStalledGSDumpsOneMachine);
					}

					estrPrintf(&pAlertString, "GameServer (%d) (players %d) (machine %s) appears not to be responding ( > %d seconds ) (%s). Recent activity:\n%s", 
						pServer->iContainerID, GS_PLAYERS(pServer), pServer->pMachine->machineName, gDelayBeforeNonRespondingAlert, pMadeDumpComment, SimpleServerLog_GetLogString(pServer));

					Controller_DoXperfDumpOnMachine(pServer->pMachine, "NonResponsiveGS");
					TriggerAlert(ALERTKEY_GAMESERVERNOTRESPONDING, pAlertString,
						ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 3, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);
				}

				estrDestroy(&pAlertString);
				estrDestroy(&pDumpString);
				estrDestroy(&pMadeDumpComment);
			}
		}
	}
	else if (isProductionMode() && (pServer->iCreationTime < iCurTimeToUse - gDelayWhenStartingGameServerBeforeFail  - pServer->pMachine->iLastLauncherLag_ToUse))
	{
		if (pServer->pGameServerSpecificInfo && pServer->pGameServerSpecificInfo->iLastHandhakingWithMapManagerTime)
		{
			if (pServer->pGameServerSpecificInfo->iLastHandhakingWithMapManagerTime < iCurTimeToUse - gDelayAfterHandshakingWithMapManagerBeforeFail)
			{
				if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker, "GS_HANDSHAKE_MM_THEN_NEVER_START"))
				{
					Controller_DoXperfDumpOnMachine(pServer->pMachine, "GSNeverContactMM");
					TriggerAlert("GS_HANDSHAKE_MM_THEN_NEVER_START", STACK_SPRINTF("GameServer (%d) (machine %s) was handshaking with the mapamanager, then never contacted controller ( > %d seconds )", pServer->iContainerID, pServer->pMachine->machineName, gDelayAfterHandshakingWithMapManagerBeforeFail),
						ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);

						//using "KillServerbyIDs" rather than "KillServer", because we don't want side effects to get confusing
					KillServerByIDs(pServer->pMachine, pServer->eContainerType, pServer->iContainerID, "MM Handshake then fail");
					StopTrackingServer(pServer, "MM Handshake then fail", true, true);
				}
			}
		}
		else
		{
			if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker, ALERTKEY_GAMESERVERNEVERSTARTED))
			{
				Controller_DoXperfDumpOnMachine(pServer->pMachine, "GSNeverContact");
				TriggerAlert(ALERTKEY_GAMESERVERNEVERSTARTED, STACK_SPRINTF("GameServer (%d) (machine %s) never contaced the controller ( > %d seconds )", pServer->iContainerID, pServer->pMachine->machineName, gDelayWhenStartingGameServerBeforeFail),
					ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);
				KillServerByIDs(pServer->pMachine, pServer->eContainerType, pServer->iContainerID, "Never Started");
				StopTrackingServer(pServer, "Never Started", true, true);
			}
		}
	}
	else if (isProductionMode() && (pServer->iCreationTime < iCurTimeToUse - gDelayWhenStartingGameServerBeforeFail / 2  - pServer->pMachine->iLastLauncherLag_ToUse))
	{
		if (!pServer->pGameServerSpecificInfo)
		{
			pServer->pGameServerSpecificInfo = StructCreate(parse_GameServerGlobalInfo);
		}

		if (!pServer->pGameServerSpecificInfo->bSentNeverStartWarning)
		{
			if (ConditionChecker_CheckCondition(pServer->pAlertsConditionChecker, "GAMESERVERMAYNEVERSTART"))
			{
				pServer->pGameServerSpecificInfo->bSentNeverStartWarning = true;
				Controller_DoXperfDumpOnMachine(pServer->pMachine, "GSNotContact");
				TriggerAlert("GAMESERVERMAYNEVERSTART", STACK_SPRINTF("AUTO_EMAIL:ALERTS_TIME_SENSITIVE GameServer (%d) (machine %s) has not yet contacted controller, half way to being killed ( > %d seconds )", pServer->iContainerID, pServer->pMachine->machineName, gDelayWhenStartingGameServerBeforeFail/2),
					ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, GLOBALTYPE_GAMESERVER, pServer->iContainerID, pServer->pMachine->machineName, 0);
			}
		}
		
	}
}


AUTO_EXPR_FUNC(alerts);
int NumServersOfType(GlobalType eType)
{
	if (eType > 0 && eType < GLOBALTYPE_MAXTYPES)
	{
		return giTotalNumServersByType[eType];
	}

	return 0;
}

AUTO_STRUCT;
typedef struct AlertCountByMachine
{
	char *pMachineName; AST(POOL_STRING, KEY)
	int iCount;
} AlertCountByMachine;



AUTO_STRUCT;
typedef struct ServerSpecificAlertListStuff
{
	AlertCountByMachine **ppMachineCounts;
} ServerSpecificAlertListStuff;


void OVERRIDE_LATELINK_AlertWasJustAddedToList(Alert *pAlert)
{
	AlertCountByMachine *pCount;
	TrackedMachineState *pMachine = GetMachineFromAlert(pAlert);

	if (!pMachine)
	{
		return;
	}

	if (!pAlert->pList->pExtraStuff)
	{
		pAlert->pList->pExtraStuff = StructCreate(parse_ServerSpecificAlertListStuff);
	}

	pCount = eaIndexedGetUsingString(&pAlert->pList->pExtraStuff->ppMachineCounts, pMachine->machineName);
	if (pCount)
	{
		pCount->iCount++;
		return;
	}

	pCount = StructCreate(parse_AlertCountByMachine);
	pCount->pMachineName = pMachine->machineName;
	pCount->iCount++;
	eaPush(&pAlert->pList->pExtraStuff->ppMachineCounts, pCount);
}

char *OVERRIDE_LATELINK_GetMachineNameForAlert(Alert *pAlert)
{
	TrackedMachineState *pMachine = GetMachineFromAlert(pAlert);

	if (pMachine)
	{
		return pMachine->machineName;
	}

	return (char*)getHostName();
}

//when a state based alert triggers, we optionally doing exhaustive logging of its object, ie, full dump of the object ever second for the next minute.

//__CATEGORY Stuff relating to the repeated logging of an object that happens after a state based alert triggers
//How frequently to dump full logs of objects
static float sfPostAlertLoggingInterval = 1.0f;
AUTO_CMD_FLOAT(sfPostAlertLoggingInterval, PostAlertLoggingInterval) ACMD_CONTROLLER_AUTO_SETTING(POST_ALERT_LOGGING);

//how many times to dump a full log of the object after an alert
static int siPostAlertLoggingCount = 120;
AUTO_CMD_INT(siPostAlertLoggingCount, PostAlertLoggingCount) ACMD_CONTROLLER_AUTO_SETTING(POST_ALERT_LOGGING);

//how many objects can be have postAlert logs going at once. Set to zero to disable system
static int siPostAlertLoggingMaxSimul = 5;
AUTO_CMD_INT(siPostAlertLoggingMaxSimul, PostAlertLoggingMaxSimul) ACMD_CONTROLLER_AUTO_SETTING(POST_ALERT_LOGGING);

//alert keys that should NOT trigger PostAlert logs
static char *spAlertsToIgnoreForPostAlertLogging = NULL;
AUTO_CMD_ESTRING(spAlertsToIgnoreForPostAlertLogging, AlertsToIgnoreForPostAlertLogging) ACMD_CONTROLLER_AUTO_SETTING(POST_ALERT_LOGGING);

AUTO_STRUCT;
typedef struct PostAlertLoggingHandle
{
	//the type name and object name concatted together for uniqueness
	char *pTypeAndName; AST(KEY ESTRING)
	int iCountRemaining;
	const char *pTypeName; AST(POOL_STRING)
	const char *pObjName;
} PostAlertLoggingHandle;

static StashTable sPostAlertLoggingHandles = NULL;
static int siCurPostAlertLoggingObjCount = 0;


//returns false if the object doesn't exist
bool DoPostAlertObjDumpNow(const char *pTypeName, const char *pObjName)
{
	void *pObj = resGetObject(pTypeName, pObjName);
	ParseTable *pTPI;
	char *pObjString = NULL;
	
	if (!pObj)
	{
		log_printf(LOG_POSTALERTOBJDUMPS, "Obj %s (type %s) doesn't seem to exist", pObjName, pTypeName);
		return false;
	}

	pTPI = resDictGetParseTable(pTypeName);

	if (!pTPI)
	{
		AssertOrAlert("RES_DICT_CORRUPTION", "Can't find tpi for dictionary %s", pTypeName);
		return false;
	}

	ParserWriteText(&pObjString, pTPI, pObj, 0, 0, 0);
	log_printf(LOG_POSTALERTOBJDUMPS, "%s %s: %s", pTypeName, pObjName, pObjString);

	estrDestroy(&pObjString);

	return true;
}


void PostAlertLoggingCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	FOR_EACH_IN_STASHTABLE(sPostAlertLoggingHandles, PostAlertLoggingHandle, pHandle)
	{
		bool bResult = DoPostAlertObjDumpNow(pHandle->pTypeName, pHandle->pObjName);
		if (!bResult || --pHandle->iCountRemaining <= 0)
		{
			stashRemovePointer(sPostAlertLoggingHandles, pHandle->pTypeAndName, NULL);
			StructDestroy(parse_PostAlertLoggingHandle, pHandle);
		}
	}
	FOR_EACH_END;

	TimedCallback_Run(PostAlertLoggingCB, NULL, sfPostAlertLoggingInterval);
}

static void PostAlertLoggingLazyInit(void)
{
	ATOMIC_INIT_BEGIN;

	sPostAlertLoggingHandles = stashTableCreateWithStringKeys(4, StashDefault);
	TimedCallback_Run(PostAlertLoggingCB, NULL, sfPostAlertLoggingInterval);

	ATOMIC_INIT_END;
}

static bool sbTestPostAlertLogging = false;
AUTO_CMD_INT(sbTestPostAlertLogging, TestPostAlertLogging);

void OVERRIDE_LATELINK_StateBasedAlertJustTriggered(StateBasedAlert *pStateBasedAlert, const char *pDescription, const char *pTypeName, const char *pName)
{
	static char *pTypeAndName = NULL;

	if (!isProductionMode() && !sbTestPostAlertLogging)
	{
		return;
	}

	if (spAlertsToIgnoreForPostAlertLogging && strstri(spAlertsToIgnoreForPostAlertLogging, pStateBasedAlert->pAlertKey))
	{
		return;
	}

	PostAlertLoggingLazyInit();

	estrPrintf(&pTypeAndName, "%s %s", pTypeName, pName);

	//if we're already logging this guy, do nothing (don't even increase his count, so he doesn't hog the simul count
	if (stashFindPointer(sPostAlertLoggingHandles, pTypeAndName, NULL))
	{
		return;
	}

	if (siCurPostAlertLoggingObjCount >= siPostAlertLoggingMaxSimul)
	{
		log_printf(LOG_POSTALERTOBJDUMPS, "Would begin post-alert logging of %s because of %s, but already doing post-alert logging for %d objects",
			pTypeAndName, pDescription, siCurPostAlertLoggingObjCount);
		return;
	}

	log_printf(LOG_POSTALERTOBJDUMPS, "Beginning post-alert logging of %s because: %s", pTypeAndName, pDescription);
	if (DoPostAlertObjDumpNow(pTypeName, pName))
	{
		PostAlertLoggingHandle *pHandle = StructCreate(parse_PostAlertLoggingHandle);
		pHandle->iCountRemaining = siPostAlertLoggingCount;
		pHandle->pObjName = strdup(pName);
		pHandle->pTypeName = pTypeName;
		pHandle->pTypeAndName = pTypeAndName;
		pTypeAndName = NULL;
		stashAddPointer(sPostAlertLoggingHandles, pHandle->pTypeAndName, pHandle, false);
		siCurPostAlertLoggingObjCount++;
	}
}


const char *GetCrashedOrAssertedKey(GlobalType eServerType)
{
	char temp[128];

	if (eServerType < 0 || eServerType >= GLOBALTYPE_LAST)
	{
		eServerType = 0;
	}

	if (!gServerTypeInfo[eServerType].pCrashOrAssertAlertKey)
	{
		sprintf(temp, "%s_%s", GlobalTypeToName(eServerType), CRASHEDORASSERTED);
		gServerTypeInfo[eServerType].pCrashOrAssertAlertKey = allocAddString(temp);
	}

	return gServerTypeInfo[eServerType].pCrashOrAssertAlertKey;
}






#include "Controller_Alerts_c_ast.c"
