#pragma once
GCC_SYSTEM
#ifndef _BEACON_CLIENT_H_
#define _BEACON_CLIENT_H_

#include "WorldColl.h"

typedef struct BeaconMapDataPacket BeaconMapDataPacket;
typedef struct BeaconProcessConfig BeaconProcessConfig;
typedef struct BeaconProcessDebugState BeaconProcessDebugState;
typedef struct BeaconSentryClientData BeaconSentryClientData;
typedef struct FolderCache FolderCache;
typedef struct FrameLockedTimer FrameLockedTimer;
typedef struct NetLink NetLink;
typedef struct PCL_Client PCL_Client;
typedef struct WorldColl WorldColl;
typedef void* HANDLE;

AUTO_STRUCT;
typedef struct BeaconSentryConfig {
	int ActiveState;					// 0 = never, 1 = normal, 2 = always
} BeaconSentryConfig;

AUTO_STRUCT;
typedef struct BeaconClientHighPriExes {
	char **exes;
} BeaconClientHighPriExes;

typedef struct BeaconClient{
	HANDLE						hMutexSentry;
	FolderCache					*foldercache;
	WorldColl					*world_coll;
	char						*active_map;

	PCL_Client					*pcl_sentry;

	BeaconProcessDebugState		*debug_state;

	BeaconSentryClientData		**sentryClients;

	struct {
		U8*						data;
		S32						receivedByteCount;
		S32						maxTotalByteCount;
	} newExeData;

	U32							gimmeBranchNum;

	HANDLE						hPipeToSentry;

	NetLink						*serverLink;
	NetLink						*masterLink;

	char*						patchServerName;
	int							masterServerIp;
	int							subServerIp;

	char*						subServerName;
	S32							subServerPort;

	BeaconMapDataPacket*		mapData;
	U32							mapDataCRC;

	U32							executableCRC;
	U32							myServerID;
	char*						serverUID;
	S32							serverProtocolVersion;

	S32							cpuCount;
	S32							cpuLimit;
	S32							cpuMax;
	U32							timeStartedClient;
	U32							timeSinceUserActivity;
	U32							timeOfLastCommand;

	char*						exeData;
	U32							exeSize;

	struct {
		struct {
			S32 lo;
			S32 hi;
			S32 *indices;
		} group;

		struct {
			S32 lo;
			S32 hi;
			S32 *indices;
		} pipeline;
	} connect;

	S32							keyboardPressed;

	U32							sentServerID			: 1;
	U32							sentSentryClients		: 1;
	U32							workDuringUserActivity	: 1;
	U32							userInactive			: 1;
	U32							connectedToSubServer	: 1;
	U32							useLocalData			: 1;
	U32							noPatchSync				: 1;
	U32							needsExeUpdate			: 1;
	U32							useGimme				: 1;
	U32							isUserMachine			: 1;
	U32							ignoreInputs			: 1;
	U32							needsSentryUpdate		: 1;
	U32							destroySentryPCL		: 1;
	char						patchTime[MAX_PATH];

	BeaconSentryConfig			config;
	BeaconProcessConfig			*process_config;
	BeaconClientHighPriExes		highproc;

	struct {
		FrameLockedTimer*			flt;
	} wcSwapInfo;
} BeaconClient;

extern BeaconClient beacon_client;

#define BEACONCONFIGVAR(varName) beacon_client.process_config->varName
#define BEACONSLOPELIMIT(slope)	slope < eaSize(&beacon_client.process_config->slopes) ? beacon_client.process_config->slopes[slope]->slope_limit : 2

#endif