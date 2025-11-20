#ifndef WLBEACON_H
#define WLBEACON_H
GCC_SYSTEM

#include "stdtypes.h"
#include "WorldColl.h"

#include "../PatchClientLib/pcl_typedefs.h"

C_DECLARATIONS_BEGIN

typedef struct PCL_Client PCL_Client;
typedef struct PCLFileSpec PCLFileSpec;
typedef struct XferStateInfo XferStateInfo;
typedef struct NetComm NetComm;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct ZoneMap ZoneMap;

typedef U32 ContainerID;
typedef int GlobalType;

typedef struct DoorConn {
	WorldInteractionEntry *interactionEntry;
	Vec3 src;
	Vec3 dst;
} DoorConn;

typedef enum BeaconWalkResult {
	BEACON_WALK_UNFINISHED = -1,
	BEACON_WALK_SUCCESS,
	BEACON_WALK_TOO_HIGH,
	BEACON_WALK_STUCK,
	BEACON_WALK_DIVERT,
	BEACON_WALK_GRADE,
	BEACON_WALK_STEPS,
	BEACON_WALK_RESULTS,
} BeaconWalkResult;

typedef struct BeaconWalkState {
	S32 processCount;

	Vec3 lastPos;
	Vec3 startPos;
	Vec3 targetPos;
	F32 bestDistToTargetSQR;
	Vec3 srcToDstLineDir;
	F32 srcToDstLineLen;
	U32 unmoving;

	U32 optional	: 1;
	U32 bidir		: 1;
} BeaconWalkState;

AUTO_STRUCT;
typedef struct BeaconProcessSlopeConfig {
	F32 slope_limit;
} BeaconProcessSlopeConfig;

AUTO_STRUCT;
typedef struct BeaconProcessConfig {
	const char *public_name;				AST(KEY)
	BeaconProcessSlopeConfig **slopes;
	F32 MovementPosTol;	
	F32 MovementMinHeight;
	F32 MovementMaxHeight;
	F32 MovementDivertTol;
	F32 MovementDivertOpt;
	F32 MovementStuckTol;
	F32 MaxFlatYDiff;
	S16 MaxYDiffS16;
	F32 MaxYDiff;
	F32 BetterSlopeYDiff;
	F32 FlatAreaCircleCutoffMin;
	F32 FlatAreaCircleCutoffMax;

	S32 numProcessesPerLoop;				AST(DEFAULT(2))
	S32 angleProcessIncrement;				AST(DEFAULT(2))

	U32 noGroundConns : 1;  // For space maps/flight-only maps
} BeaconProcessConfig;

AUTO_STRUCT;
typedef struct BeaconServerInfo {
	char *pCSS;			AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
	char *pHtml;		AST(ESTRING, FORMATSTRING(HTML=1, HTML_NO_HEADER=1))
} BeaconServerInfo;

extern ParseTable parse_BeaconServerInfo[];
#define TYPE_parse_BeaconServerInfo BeaconServerInfo

#define BM_MAX_COUNT 500

void beaconInit(void);
void beaconReload(void);
void beaconMapLoad(ZoneMap *zmap, S32 fullInit);
void beaconMapUnload(void);
void beaconPartitionLoad(int partitionIdx);
void beaconPartitionUnload(int partitionIdx);
void beaconOncePerFrame(void);
void beaconPauseDynConnQueueCheck(int pause);

int runBeaconApp(void);

U32 beaconIsBeaconizer(void);
int	beaconIsClient(void);
U32 beaconIsMasterServer(void);
U32 beaconIsRequestServer(void);
U32 beaconRequestServerIsComplete(void);
const char* beaconRequestServerGetNamespace(void);
F32 beaconRequestServerGetCompletion(char **statusEstr);
void beaconRequestServerGetFilenames(char ***fileList);
void beaconServerGetServerInfo(BeaconServerInfo *info);

WorldColl *beaconGetWorldColl(WorldColl *wc);

// Returns an EArray of invalid spawn strings to be parsed by encounter system.
char ***beaconGetInvalidSpawns(void);

void beaconReadInvalidSpawnFile(void);

// Callback for partitioning
typedef void (*BeaconPartitionCallback)(int partitionIdx);
typedef void (*BeaconPerPartitionCallback)(BeaconPartitionCallback func);

void beaconSetPartitionCallbacks(BeaconPerPartitionCallback func);

// Callbacks for movement
typedef void	(*BeaconConnMovementStartCallback)(int iPartitionIdx, Vec3 src, Vec3 dest, int count);
typedef int		(*BeaconConnMovementIsFinishedCallback)(void);
typedef void	(*BeaconConnMovementResultCallback)(int *success, S32 *optional, F32 *dist);
void beaconSetMMMovementCallbacks(	BeaconConnMovementStartCallback start,
									BeaconConnMovementIsFinishedCallback isfinished,
									BeaconConnMovementResultCallback result);

// Callbacks for pcl functions
typedef void (*PCLConnectCallback)(PCL_Client * client, bool updated, PCL_ErrorCode error, const char * error_details, void * userData);
typedef void (*PCLSetViewCallback)(PCL_Client * client, PCL_ErrorCode error, const char * error_details, void * userData);
typedef void (*PCLGetBatchCallback)(PCL_Client * client, PCL_ErrorCode error, const char * error_details, void * userData);
typedef void (*PCLCheckinCallback)(int rev, U32 time, PCL_ErrorCode error, const char * error_details, void * userData);
typedef bool (*PCLProcessCallback)(PatchProcessStats *stats, void *userData);
typedef bool (*PCLUploadCallback)(S64 sent, S64 total, F32 elapsed, PCL_ErrorCode error, const char * error_details, void * userData);
// Pcl functions as callbacks
typedef PCL_ErrorCode (*PCLConnectCreateFunc)(PCL_Client ** client, char * serverName, int port, F32 timeout,
									NetComm * comm, const char * rootFolder, const char *autoupdate_token,
									const char *autoupdate_path, PCLConnectCallback callback, void * userData);
typedef PCL_ErrorCode (*PCLSetViewFunc)(	PCL_Client * client, const char * project, int branch,
											const char * sandbox, bool getManifest, bool saveTrivia,
											PCLSetViewCallback callback, void * userData);
typedef PCL_ErrorCode (*PCLSetDefaultViewFunc) (PCL_Client * client, const char * project, bool getManifest,
												PCL_SetViewCallback callback, void * userData);
typedef PCL_ErrorCode (*PCLGetAllFunc)(PCL_Client * client, PCLGetBatchCallback callback, void * userData, const PCLFileSpec * filespec);
typedef PCL_ErrorCode (*PCLProcessFunc)(SA_PARAM_NN_VALID PCL_Client * client);
typedef PCL_ErrorCode (*PCLNeedsRestartFunc)(PCL_Client * client, bool * needs_restart);
typedef PCL_ErrorCode (*PCLCheckViewFunc)(const char * dir, char * label_buf, int label_buf_size, 
								U32 * view_time, int * branch, char * sandbox_buf, 
								int sandbox_buf_size, char * view_dir_buf, int view_dir_buf_size);
typedef PCL_ErrorCode (*PCLGetBranchFunc)(PCL_Client * client, S32 *branchOut);
typedef PCL_ErrorCode (*PCLForceFilesFunc)(	SA_PARAM_NN_VALID PCL_Client * client,
										   const char*const* dirNames,
										   const char*const* countsAsDir,
										   const int* recurse,
										   int count,
										   const char*const* hide_paths,
										   int hide_count,
										   const char* comment,
										   bool matchesonly,
										   PCLCheckinCallback callback,
										   void * userData);
typedef PCL_ErrorCode (*PCLSetProcessCBFunc)(PCL_Client * client, PCLProcessCallback callback, void * userData);
typedef PCL_ErrorCode (*PCLSetUploadCBFunc)(PCL_Client * client, PCLUploadCallback callback, void * userData);
typedef PCL_ErrorCode (*PCLDisconnectAndDestroyFunc)(PCL_Client * client);
void beaconSetPCLCallbacks(PCLConnectCreateFunc ccfunc,
						   PCLDisconnectAndDestroyFunc ddfunc,
						   PCLForceFilesFunc fffunc,
						   PCLSetViewFunc svfunc,
						   PCLSetDefaultViewFunc dvfunc,
						   PCLGetAllFunc gafunc,
						   PCLProcessFunc pfunc,
						   PCLNeedsRestartFunc nrfunc,
						   PCLCheckViewFunc cvfunc,
						   PCLSetProcessCBFunc spfunc,
						   PCLSetUploadCBFunc ufunc,
						   PCLGetBranchFunc gbfunc);

typedef void (*BeaconServerListCallback)(U32 *ipList, void *userData);
typedef void (*BeaconGetServerListFunc)(GlobalType gtype, BeaconServerListCallback cb, void* data);
void beaconSetGetServerListCallback(BeaconGetServerListFunc func);

typedef void (*BeaconServerNameCallback)(const char* name, void* userData);
typedef void (*BeaconGetServerNameFunc)(BeaconServerNameCallback cb, void* data);
void beaconSetGetServerNameCallback(BeaconGetServerNameFunc func);

typedef void (*WCICreateFunc)(void);
void beaconSetWCICallbacks(WCICreateFunc cfunc);

void beaconGatherDoors(DoorConn ***doors);

typedef bool (*BeaconPathDoorCallback)(Vec3 pos, Vec3 posOut);
void beaconPathSetDoorCallback(BeaconPathDoorCallback cb);

U32 beaconFileGetFileCRC(const char* beaconDateFile, S32 getTime, S32 *procVersion);
U32 beaconFileGetCRC(S32 *procVersion);
S32	beaconFileGetProcVersion(void);
void beaconClearCRCData(void);
void beaconFileGatherMetaData(int iPartitionIdx, U32 quiet, U32 logFullInfo);

void beaconWalkStateInit(SA_PARAM_NN_VALID BeaconWalkState *state, int processCount, Vec3 startPos, Vec3 targetPos);
int	 beaconCheckWalkState(SA_PARAM_NN_VALID BeaconWalkState* state, Vec3 curPos, F32 maxSpeed, F32 *speedOut);

void beaconizerStartup(void);
void beaconizerRun(void);

//void RemoteCommand_InformControllerOfServerState( GlobalType gServerType, ContainerID gServerID, int eGlobalType, U32 iContainerID, const char* pStateString);
typedef void (*InformControllerOfServerStateFunc)( GlobalType gServerType, ContainerID gServerID, int eGlobalType, U32 iContainerID, const char* pStateString);
void beaconSetInformControllerOfStateCallback(InformControllerOfServerStateFunc func);

void bcnSetRebuildFlag(int partitionIdx);

C_DECLARATIONS_END

#endif