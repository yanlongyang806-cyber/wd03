#ifndef BEACONSERVERPRIVATE_H
#define BEACONSERVERPRIVATE_H
GCC_SYSTEM

typedef struct BeaconProcessDebugState BeaconProcessDebugState;
typedef struct BeaconProcessConfig BeaconProcessConfig;
typedef const void* DictionaryHandle;

AUTO_STRUCT;
typedef struct BeaconServerMapStatus {
	const char* mapName;

	BeaconMapMetaData *fileData;		AST(NAME(FileData))
	BeaconMapMetaData *mapData;

	U32 needsProcess : 1;
	U32 forceProcess : 1;
	U32 failedToLoad : 1;
} BeaconServerMapStatus;

AUTO_STRUCT;
typedef struct BeaconServerStateDetails {
	int curMap;
	int lastCheckedIn;
	int lastPing;
} BeaconServerStateDetails;

AUTO_STRUCT;
typedef struct BeaconServerStatus {
	char *project;
	int gimmeTime;
	int branch;
	BeaconServerMapStatus				**mapStatus;
} BeaconServerStatus;

// ---[ Common stuff ]-----------------------------------------------------------------------------

typedef enum BeaconClientState {
	BCS_NOT_CONNECTED = 0,
	BCS_CONNECTED,
	BCS_READY_TO_WORK,
	BCS_RECEIVING_MAP_DATA,
	BCS_NEEDS_MORE_MAP_DATA,
	BCS_READY_TO_GENERATE,
	BCS_GENERATING,
	BCS_READY_TO_CONNECT_BEACONS,
	BCS_PIPELINE_CONNECT_BEACONS,
	BCS_CONNECTING_BEACONS,
	BCS_CLIENT_COUNT,
	
	// Other states.

	BCS_SENTRY,
	BCS_SERVER,
	BCS_NEEDS_MORE_EXE_DATA,
	BCS_RECEIVING_EXE_DATA,
	
	// Requester states.
	
	BCS_REQUESTER,
	
	// Request server states.
	
	BCS_REQUEST_SERVER_IDLE,
	BCS_REQUEST_SERVER_PROCESSING,
	
	// Error states.
	BCS_ERROR_DATA,
	BCS_ERROR_PROTOCOL,
	BCS_ERROR_NONLOCAL_IP,
	
	BCS_COUNT,
} BeaconClientState;

typedef enum BeaconServerState {
	BSS_NOT_STARTED,
	BSS_GENERATING,
	BSS_CONNECT_BEACONS,
	BSS_WRITE_FILE,
	BSS_SEND_BEACON_FILE,
	BSS_DONE,
} BeaconServerState;

typedef enum BeaconClientType {
	BCT_NOT_SET,
	BCT_WORKER,
	BCT_SENTRY,
	BCT_SERVER,
	BCT_REQUESTER,
	BCT_COUNT,
} BeaconClientType;

typedef enum BeaconServerDisplayTab {
	BSDT_MAP,
	BSDT_SENTRIES,
	BSDT_SERVERS,
	BSDT_WORKERS,
	BSDT_REQUESTERS,
	BSDT_COUNT,
} BeaconServerDisplayTab;

typedef enum BeaconProcessNodeState {
	BPNS_WAITING_FOR_LOAD_REQUEST,
	BPNS_WAITING_FOR_MAP_DATA_FROM_CLIENT,
	BPNS_WAITING_TO_REQUEST_MAP_DATA_FROM_CLIENT,
	BPNS_WAITING_FOR_WRITE_TO_DISK,
	BPNS_WAITING_FOR_MAP_DATA_FROM_DISK,
	BPNS_WAITING_FOR_REQUEST_SERVER,
	BPNS_WAITING_FOR_CLIENT_TO_RECEIVE_BEACON_FILE,
} BeaconProcessNodeState;

typedef struct BeaconConnectBeaconGroup			BeaconConnectBeaconGroup;
typedef struct BeaconServerClientData			BeaconServerClientData;
typedef struct BeaconServerSentryClientData		BeaconServerSentryClientData;
typedef struct BeaconFileLoadRequest			BeaconFileLoadRequest;
typedef struct BeaconProcessQueueNode			BeaconProcessQueueNode;
typedef struct BeaconMapMetaData				BeaconMapMetaData;

typedef struct BeaconServerMachineData {
	BeaconServerClientData *sentry;
	BeaconServerClientData **clients;

	U32 ip;
} BeaconServerMachineData;

typedef struct BeaconServerClientData {
	NetLink*							link;

	U32									protocolVersion;
	U32									exeCRC;
	U32									connectTime;
	U32									sentPingTime;
	U32									receivedPingTime;
	U32									uid;

	BeaconClientState					state;
	U32									stateBeginTime;
	S64									stateBeginTicks;

	BeaconClientType					clientType;
	
	struct {
		BeaconDiskSwapBlock*			block;
		BeaconConnectBeaconGroup*		group;
		BeaconConnectBeaconGroup*		pipeline;
	} assigned;
	
	struct {
		S32								blockCount;
		S32								beaconCount;
	} completed;
	
	struct {
		U32								sentByteCount;
		U32								lastCommTime;
	} mapData;
	
	struct {
		U32								lastCommTime;
		U32								sentByteCount;
		U32								shouldBeSent		: 1;
	} exeData;
	
	char*								userName;
	char*								computerName;
	BeaconServerMachineData				*machine;
	
	U32									forcedInactive		: 1;
	U32									readyForCommands	: 1;
	U32									transferred			: 1;
	U32									disconnected		: 1;
	
	struct {
		BeaconizerType					type;							// PROT >= 3
		U32								protocolVersion;
		BeaconServerState				state;
		BeaconServerStatus				status;
		BeaconServerStateDetails		stateDetails;
		char*							mapName;
		S32								clientCount;
		S32								port;
		U32								patchTime;
		U32								sendStatusUID;
		S64								timeMapStarted;
		U32								numClientsRequested;
		BeaconServerClientData**		clients;
		U32								sendClientsToMe		: 1;
		U32								isRequestServer		: 1;		// PROT<=2
	} server;
	
	struct {
		U32								uid;
		char*							dbServerIP;
		BeaconFileLoadRequest*			loadRequest;
		BeaconProcessQueueNode*			processNode;
		U32								startedRequestTime;
		U32								toldToSendRequestTime;
	} requester;

	struct {
		S32								procVersion;
		S32								mapDataVersion;
		BeaconServerClientData			*assignedTo;
	} client;
	
	struct {
		BeaconProcessQueueNode*			processNode;
		U32								sentByteCount;
	} requestServer;

	char*								clientIPStr;
	char*								crashText;
} BeaconServerClientData;

typedef struct BeaconServerSentryClientData {
	BeaconServerClientData*				client;
	U32									pid;
	char*								crashText;
	
	U32									updated			: 1;
	U32									forcedInactive	: 1;
} BeaconServerSentryClientData;

typedef struct BeaconConnectBeaconGroup {
	BeaconConnectBeaconGroup*			next;
	S32									lo;
	S32									hi;
	S32									finished;

	struct {
		BeaconServerClientData**		clients;
		U32								assignedTime;
	} clients;
} BeaconConnectBeaconGroup;

typedef struct BeaconServerClientList {
	BeaconServerClientData**			clients;
	S32									count;
	S32									maxCount;
} BeaconServerClientList;

typedef struct BeaconProcessQueueNode {
	struct {
		BeaconProcessQueueNode*			next;
		BeaconProcessQueueNode*			prev;
	} sibling;
	
	BeaconProcessNodeState				state;
	
	BeaconServerClientData*				requester;
	BeaconServerClientData*				requestServer;
	BeaconMapDataPacket*				mapData;
	char*								uniqueStorageName;
	char*								uniqueFileName;
	
	U32									uid;
	U32									timeStamp;
	
	U32									isInHashTable	: 1;

	struct {
		U8*								data;
		U32								byteCount;
		U32								uncompressedByteCount;
		U32								receivedByteCount;
		U32								sentByteCount;
		U32								crc;
	} beaconFile;
} BeaconProcessQueueNode;

AUTO_STRUCT;
typedef struct BeaconProcessConfigList {
	BeaconProcessConfig **configs;
} BeaconProcessConfigList;

typedef struct BeaconServer {
	HANDLE								mutexMaster;
	BeaconizerType						type;

	NetComm								*comm;
	NetListen							*clients;
	NetLink								*master_link;
	NetLink								*db_link;
	PCL_Client							*pcl_client;
	
	NetListen							*debug_listen;
	BeaconProcessDebugState				*debug_state;

	char*								dataToolsRootPath;
	char*								dataPath;
	char*								toolsPath;
	
	char*								patchServerName;

	char**								skipList;
	const char**						queueList;
	S32									curMapIndex;
	char*								curMapName;
	char*								nextMapName;
	char*								serverUID;
	U32									nextClientUID;
	U32									gimmeBranchNum;
	char*								requestCacheDir;
	U32									sentPingTime;
	U32									recvPingTime;
	
	U32									useful_count;
	U32									spawn_count;
	U32									reject_spawn_count;
	U32									reject_useful_count;
	U32									generate_count;
	U32									optional;
	U32									optional_pruned;
	U32									is_new_file : 1;
	
	BeaconMapDataPacket*				mapData;
	U32									mapDataCRC;
	BeaconProcessConfig					*defConfig;
	DictionaryHandle					mapConfigDict;

	// Hackery
	F32									defCutoffMin;
	F32									defCutoffMax;
	
	BeaconServerClientList				clientList[BCT_COUNT];
	BeaconServerMachineData				**machines;
	
	S32									selectedClient;
	S64									clientTicks[BPP_COUNT];
	
	U32									sendClientsToMe		: 1;
	U32									noNetWait			: 1;
	U32									minimalPrinting		: 1;
	U32									printTiming			: 1;
	U32									dontRestartOnAssert : 1;

	struct {
		U32								sendUID;
		U32								ackedUID;
		U32								timeSent;
		
		U32								send				: 1;
		U32								acked				: 1;
	} status;
		
	struct {
		U32								crc;
		U8*								data;
		U32								size;
	} exeFile;

	struct {
		U32								crc;
		U8*								data;
		U32								size;
	} exeClient;
	
	struct {
		U32								nodeUID;
		char*							uniqueStorageName;
		char**							fileNames;
		char*							projectName;
		U32								completed_project	: 1;
		U32								allow_skip			: 1;
	} request;
	
	BeaconServerState					state;
	U32									setStateTime;
	
	S32									port;
	S32									paused;
	S32									localOnly;
	S32									loadMaps;
	S32									isMasterServer;
	S32									isAutoServer;
	S32									isPseudoAuto;
	S32									isRequestServer;
	S32									forceRebuild;
	S32									fullCRCInfo;
	S32									generateOnly;
	S32									checkEncounterPositionsOnly;
	S32									testRequestClient;
	S32									noGimmeUsage;
	S32									symStore;
	S32									writeCurMapData;
	S32									noUpdate;
	S32									useLocalSrc;
	S32									noPatchServer;
	S32									fullInfoOnce;
	U32									patcherTime;
	char								patchTime[128];
	S32									force_state_advance;
	S32									allowPrivateMaps;
	S32									skip_processing			: 1;
	S32									allowCRCMismatch		: 1;

	int									fileBeaconCount;
	int									fileConnectionCount;
	char								**statusMessages;

	// The server loads each map and gathers meta data at startup
	BeaconServerStatus					serverStatus;
	BeaconServerStateDetails			stateDetails;
	U32									statusDirty : 1;
	
	struct {
		U8								letter;
		U32								color;
	} icon;
	
	struct {
		struct {
			S32							count;
			BeaconConnectBeaconGroup*	availableHead;
			BeaconConnectBeaconGroup*	availableTail;
			BeaconConnectBeaconGroup*	finished;
		} groups;
		
		struct {
			S32							*indices;
			S32							groupedCount;
		} legalBeacons;
		
		S32								assignedCount;
		S32								finishedCount;
		S32								finishedBeacons;

		U32								startTime;
	} beaconConnect;

	struct {
		S32 totalBlocks;
		S32 closedBlocks;
	} beaconGenerate;
	
	struct {
		BeaconServerDisplayTab			curTab;
		U32								backBufferEnabled	: 1;
		U32								updated				: 1;
		
		struct {
			U32							updated				: 1;
		} mapTab;
		
		struct {
			U32							updated				: 1;
		} sentriesTab;
	} display;
	
	struct {
		struct {
			U8*							data;
			U32							byteCount;
			U32							maxByteCount;
		} compressed, uncompressed;
		
		U32								sentByteCount;
		U32								crc;
	} beaconFile;
	
	struct {
		CRITICAL_SECTION				cs;
		StashTable						htFileNameToRequestNode;
		
		BeaconProcessQueueNode*			head;
		BeaconProcessQueueNode*			tail;
		U32								count;
	} processQueue;
} BeaconServer;

typedef struct BeaconServerProcessClientsData {
	U32			sentMapDataCount;
	U32			sentExeDataCount;
} BeaconServerProcessClientsData;

// ---[ beaconServer.c ]---------------------------------------------------------------------------

extern BeaconServer beacon_server;

void	beaconServerDisconnectClient(BeaconServerClientData* client, const char* reason);
void	beaconServerSendBeaconFileToRequester(BeaconServerClientData* client, int start);
void	beaconServerSetClientState(BeaconServerClientData* client, BeaconClientState state);
void	beaconServerSendRequestChunkReceived(BeaconProcessQueueNode* node);
void	beaconServerAssignProcessNodeToRequestServer(	BeaconProcessQueueNode* processNode,
													BeaconServerClientData* requestServer);
BeaconProcessConfig* beaconServerGetProcessConfig(void);

// ---[ beaconServerFileThread.c ]-----------------------------------------------------------------

void 	beaconServerDetachClientFromLoadRequest(BeaconServerClientData* client);
void 	beaconServerAddToBeaconFileLoadQueue(BeaconServerClientData* client, U32 crc);
void 	beaconServerWriteRequestedBeaconFile(BeaconProcessQueueNode* node);
U32		beaconServerGetLoadRequestCount(void);
S32		beaconServerIsLoadRequestFinished(BeaconServerClientData* client);
S32		beaconServerSendSuccessfulLoadRequest(BeaconServerClientData* client);
void	beaconServerInitLoadRequests(void);

void	beaconServerUpdateProcessNodes(void);
void	beaconServerDetachRequesterFromProcessNode(BeaconServerClientData* requester);
void	beaconServerAddRequesterToProcessQueue(	BeaconServerClientData* requester,
												const char* uniqueStorageName,
												U32 crc);
void	beaconServerCancelProcessNode(BeaconProcessQueueNode* node);
S32		beaconServerGetProcessNodeForProcessing(BeaconProcessQueueNode** nodeOut);
void	beaconServerInitProcessQueue(void);
S32		beaconServerQueueMapFile(const char* newMapFile);

// ---[ End ]--------------------------------------------------------------------------------------

#endif   // #if BEACONSERVERPRIVATE_H

