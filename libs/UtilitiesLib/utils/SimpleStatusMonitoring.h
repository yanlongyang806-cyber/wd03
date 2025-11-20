#pragma once

#include "statusReporting.h"

typedef struct NetLink NetLink;

AUTO_ENUM;
typedef enum SimpleStatusMonitoringFlags
{
	SSMFLAG_FORWARD_STATUSES_TO_OVERLORD = 1 << 0,

	SSMFLAG_LISTEN_FOR_FORWARDED_STATUSES_AS_OVERLORD = 1 << 1,
} SimpleStatusMonitoringFlags;

//simple status monitoring is a simplified version of the critical system monitoring that the ControllerTracker does, using the
//same internal protocol. It does not send emails or do anything with alerts, but provides simple connected/disconnect status,
//most recent contact, and the ability to issue some simple commands

//port defaults to the CT port (CONTROLLERTRACKER_CRITICAL_SYSTEM_INFO_PORT)
void SimpleStatusMonitoring_Begin(int iPort, SimpleStatusMonitoringFlags eFlags);



extern StashTable gSimpleMonitoringStatusByName;

AUTO_ENUM;
typedef enum SimpleMonitoringState
{
	SIMPLEMONITORING_CONNECTED,
	
	SIMPLEMONITORING_STALLED, //still connected, but haven't heard from for > 30 seconds, will
		//hang around potentially forever

	SIMPLEMONITORING_DISCONNECTED, //if we are doing SentryServer queries, will hang around
		//forever potentially, otherwise for 30 seconds

	SIMPLEMONITORING_GONE, //will stay around for 30 seconds
	SIMPLEMONITORING_CRASHED, //will stay around for 30 seconds

	SIMPLEMONITORING_FORWARDING_LOST, //we were forwarded information about this server
		//from some other server doing the actual status monitoring, and we lost our connection to that
		//server. (will stay around for 5 minutes)
} SimpleMonitoringState;

AUTO_STRUCT;
typedef struct SimpleMonitoringStatus_Log
{
	U32 iTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
	char *pStr;
} SimpleMonitoringStatus_Log;

#define MAX_LOGS_PER_SYSTEM 50

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "ConnectionState, LastActivityTime, status.status.fps, status.status.State");
typedef struct SimpleMonitoringStatus
{
	char *pName; AST(KEY)
	SimpleMonitoringState eConnectionState;

	//last time we got a packet, OR time we got disconnected, OR time we crashed, OR time 
	//SentryServer said we no longer existed
	U32 iLastActivityTime; AST(FORMATSTRING(HTML_SECS_AGO=1))

	StatusReporting_Wrapper status;
	NetLink *pLink; NO_AST
	U32 iIP;

	//when we start monitoring a system without the system actually existing yet, we can
	//set this machine name. Don't use it for anything when possible, as .status has the actual machine name
	//as reported by the system
	char *pAssumedMachineName;


	char *pMyOverlord; AST(POOL_STRING)

	SimpleMonitoringStatus_Log **ppLogs;

	//some statuses are just forwarded to us from other machines. Presumably (writing this long before any of this is
	//put into effect) machinestatus.exe will monitor all the exes on a local machine, and forward this summary
	//to the overlord. If so, the overlord marks each forwarded status with what machine it got it from, so it knows
	//that it doesn't need to do any of the timeout logic, etc.
	const char *pMachineNameForwardedFrom; AST(POOL_STRING, NO_NETSEND)
} SimpleMonitoringStatus;

//send a command requesting that a system shut down... will only work on systems that have 
//latelinked shutdown commands
//
//returns simple status string
char *SimpleStatusMonitoring_TellSystemToShutDown(char *pName);

//will only do anything if the restarting batch file is set, and if SentryServer comm is active
char *SimpleStatusMonitoring_RestartSystem(char *pName);

SimpleMonitoringStatus *SimpleStatusMonitoring_FindStatusByName(char *pName);

//only returns systems that are CONNECTED
SimpleMonitoringStatus *SimpleStatusMonitoring_FindConnectedStatusByName(char *pName);

const char *SimpleMonitoringStatus_GetMachineName(SimpleMonitoringStatus *pStatus);
