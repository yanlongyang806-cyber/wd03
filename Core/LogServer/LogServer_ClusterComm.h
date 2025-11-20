#pragma once

#include "GlobalTypes.h"
#include "loggingEnums.h"


/*
An in-shard logserver can be configured to send some subset of its logs up to a cluster-level logServer. This file
contains headers and so forth for both sides of that of that communication. General overview:
-When log messages show up on the logserver via LOGCLIENT_LOGPRINTF, they are removed from the packet and stuck into a
bunch of lists and so forth for purposes of writing out and interleaving, etc. (handleLogPrintf). Eventually we are convinced that
we have waited long enough taht everything will properly be sorted, so we go ahead and write to disk. At this point, we
also create a packet and send it off to the cluster-level log parser, if there is one, after filtering
for categories it's interested in. This way we know that the cluster-level logparser will always get things
that are already sorted
-So the cluster-level logserver just gets packets that are in precisely the same format as normal LOGCLIENT_LOGPRINTF packets,
so it can use literally identical code to handle these messages.
-If the in-shard logserver loses its connection to the cluster level logserver, we buffer up the messages for a while, but then
we just throw them out, because after we're always logging them all locally because we are a log server, so there's no point
in logging another copy of them. But make sure to trigger useful alerts
*/

void LogServer_ClusterTick(void);
extern bool gbSendLogsToClusterLevelLogServer;

void SendLogToClusterLevelLogServer(GlobalType eSourceServerType, U32 iTime, enumLogCategory eCategory, const char *pMessageString);
bool ClusterLevelLogServerIsInterested(GlobalType eSourceServerType, enumLogCategory eCategory);

static __forceinline void MaybeSendLogToClusterLevelLogServer(GlobalType eSourceServerType, U32 iTime, enumLogCategory eCategory, const char *pMessageString)
{
	if (gbSendLogsToClusterLevelLogServer && ClusterLevelLogServerIsInterested(eSourceServerType, eCategory))
	{
		SendLogToClusterLevelLogServer(eSourceServerType, iTime, eCategory, pMessageString);
	}
}

void LogServer_ClusterFlush(void);

//in logserver.c
extern __int64 gTimeToDie;


AUTO_STRUCT;
typedef struct LogServerClusterCommFilterConfigList
{
	GlobalType *pServerTypes; AST(NAME(ServerType))
	enumLogCategory *pLogCategoryTypes; AST(NAME(LogCategory))

	//"gameserver/CLICKABLE_EVENTS"
	char **ppServerCategoryPairs; AST(NAME(ServerAndCategory))

} LogServerClusterCommFilterConfigList;

//config file which defines what categories/server types should be forwarded up to the cluster-level log server
//
AUTO_STRUCT;
typedef struct LogServerClusterCommFilterConfig
{
	//ONE OF THESE TWO MUST BE SET
	bool bIncludeAllOthers;
	bool bExcludeAllOthers; 

	LogServerClusterCommFilterConfigList *pWhiteList;
	LogServerClusterCommFilterConfigList *pBlackList;
} LogServerClusterCommFilterConfig;

