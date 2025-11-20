#pragma once

#include "pub\NewControllerTracker_pub.h"

typedef struct AccountTicket AccountTicket;



typedef struct ShardConnectionUserData
{
	U32 iLastUpdateTime;
	ShardInfo_Full *pShardInfo;
} ShardConnectionUserData;

typedef struct
{
	const char *pProductName; //poolstring
	const char *pCategoryName; //poolstring
	ShardInfo_Full **ppShards;
} ShardCategory;



typedef struct MCPConnectionUserData
{
	AccountTicket *pTicket;
} MCPConnectionUserData;


extern ShardCategory **gppShardCategories;
extern StashTable gShardsByID;
extern StashTable gShardsByName;

void InitShardCom(void);
void InitMCPCom(void);

void NewControllerTracker_InitShardDictionary(void);

void ShardComPeriodicUpdate(void);
void TicketValidationOncePerFrame(void);



typedef struct NetComm NetComm;

extern NetComm *gpShardComm;


//basically says "For shard ShardName, as long as the current version is curVersion, then the prepatch
//version is NewVersion. If the cur version is anything else, then forget this"
AUTO_STRUCT;
typedef struct PrePatchInfo
{
	char *pShardOrClusterName; AST(ADDNAMES(ShardName))
	char *pCurVersionCmdLine;
	char *pNewVersionCmdLine;
} PrePatchInfo;

//persistent data that is stored in localdata. (Persists, but will be lost to things like disk crashes, so not
//super-high-security)
AUTO_STRUCT;
typedef struct ControllerTrackerStaticData
{
	PrePatchInfo **ppPrePatchInfos;
	ShardInfo_Perm **ppPermanentShards;
} ControllerTrackerStaticData;

ControllerTrackerStaticData gStaticData;

void LoadControllerTrackerStaticData(void);

void SaveControllerTrackerStaticData(void);

void UpdateAllShardPrepatchCommandLines(void);



#define CONTROLLERTRACKER_HTTP_PORT 8080