#pragma once
#include "GlobalTYpes.h"
#include "nameValuePair.h"

typedef struct NameValuePairList NameValuePairList;

AUTO_STRUCT;
typedef struct ClusterServerStatus
{
	ContainerID iContainerID;
	U32 iIP; AST(FORMAT_IP)
	char *pStateString;
} ClusterServerStatus;

//all servers of a type are grouped together, so we can easily eaIndexed search for servers of a type
AUTO_STRUCT;
typedef struct ClusterServerTypeStatus
{
	GlobalType eType; AST(KEY)
	ClusterServerStatus **ppServers;
} ClusterServerTypeStatus;


//any frequently-changing dynamic stuff should go in here. This is updated every 10 seconds 
//(SHARCLUSTER_PERIODICSTATUS_UPDATE_FREQ)
//as opposed to the other stuff, which is updated as it changes
AUTO_STRUCT;
typedef struct ClusterShardPeriodicStatus
{
	int iNumPlayers;
	int iNumGameServers;
	int iNumInMainQueue;
	int iNumInVIPQueue;
	int iNumLoggingIn;
} ClusterShardPeriodicStatus;

#define SHARDCLUSTER_PERIODICSTATUS_UPDATE_FREQ 10


//this is the struct that is sent around from shard to shard
AUTO_STRUCT;
typedef struct ClusterShardStatus
{
	char *pShardName;
	bool bShardIsLocked;

//	int iNumGameServers; 
	char *pVersion;
	ClusterShardType eShardType;

	ClusterServerTypeStatus **ppServersByType;

	ClusterShardPeriodicStatus periodicStatus;
} ClusterShardStatus;



AUTO_ENUM;
typedef enum ClusterShardState
{
	CLUSTERSHARDSTATE_NEVER_CONNECTED,
	CLUSTERSHARDSTATE_DISCONNECTED,
	CLUSTERSHARDSTATE_CONNECTED,
	CLUSTERSHARDSTATE_THATS_ME,
} ClusterShardState;

//each shard keeps a summary of every other shard
AUTO_STRUCT   AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "ShardName, ShardType, ControllerHostName, State");
typedef struct ClusterShardSummary
{
	char *pShardName; AST(KEY)
	ClusterShardType eShardType;
	char *pControllerHostName;
	U32 iControllerIP;
	ClusterShardState eState;
	ClusterShardStatus *pMostRecentStatus;

	//the controller uses this internally to track connection state and so forth
	void *pUserData; NO_AST

	//these are purely local so that a shard can locally set and remember information about other shards, ie, 
	//"have I sent a UGC update to this shard recently", etc.
	NameValuePairList *pLocalNameValuePairs; AST(NO_NETSEND)

} ClusterShardSummary;

//the controller generates ones of these, other servers can get a copy of it via remote command
AUTO_STRUCT;
typedef struct Cluster_Overview
{
	ClusterShardSummary **ppShards;
} Cluster_Overview;


//on any server other than the controller, this is guaranteed to return something
//as long as InformMeOfShardClusterSummary is set for that server type in controllerServerSetup.txt, 
//and as long as you're in a cluster. If you're not in a cluster, it returns NULL.
//(if you're ever tempted to set that to true for gameservers, talk to Alex and figure out whether
//there's a better solution)
Cluster_Overview *GetShardClusterOverview_IfInCluster(void);

//similar to the above, but returns something even if you're not in a cluster, with just info about yourself.
//Note that this will still return NULL on servers without InformMeOfShardClusterSummary set
Cluster_Overview *GetShardClusterOverview_EvenIfNotInCluster(void);

//don't call this, it happens automatically
LATELINK;
void SetShardClusterOverview(Cluster_Overview *pOverview);

//called by the internal system to alert you that something is different, if you care. Note that it's
//not guaranteed that something actually changed when this is called, but it is guaranteed that this
//is called whenever the overview changes
LATELINK;
void ShardClusterOverviewChanged(void);

//this will be available on all servers in the shard, is set separately
ClusterShardType ShardCluster_GetShardType(void);