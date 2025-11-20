
#include <process.h>
#include <io.h>
#include <direct.h>
#include "beaconPrivate.h"
#include "beaconConnection.h"
#include "beaconPath.h"
#include "utils.h"
#include "EString.h"
#include "strings_opt.h"
#include "beaconGenerate.h"
#include "crypt.h"
#include "earray.h"
#include "fileutil.h"
#include "beaconFile.h"
#include "utils.h"
#include "FolderCache.h"
#include "MemoryMonitor.h"
#include "net/net.h"
#include "../../3rdparty/zlib/zlib.h"
#include "RegistryReader.h"
#include "sysutil.h"
#include "WorldColl.h"
#include "wlBeacon.h"

GCC_SYSTEM

#define BEACON_SERVER_PORT					(0xBEAC) // == 48812 :P
#define BEACON_MASTER_SERVER_PORT			(BEACON_SERVER_PORT + 1)
#define BEACON_SERVER_DEBUG_PORT			(BEACON_SERVER_PORT - 1)
#define BEACON_CLIENT_PROTOCOL_VERSION		(5)
#define BEACON_CLIENT_MAPDATA_VERSION		(2)

#define BEACON_DEFAULT_SERVER "blade10rack1"
#define BEACON_DEFAULT_PATCHSERVER "assetmaster"

// MUST MATCH THAT WHICH IS IN the config file for the server
#define BEACON_PATCH_SERVER_PORT 7255

AUTO_STRUCT;
typedef struct ASTColor {
	U32 c;
} ASTColor;

AUTO_STRUCT;
typedef struct ASTPoint {
	Vec3 p;
	U32 c;
} ASTPoint;

AUTO_STRUCT;
typedef struct ASTLine {
	Vec3 p1;
	Vec3 p2;
	U32 c;
} ASTLine;

AUTO_STRUCT;
typedef struct ASTTri {
	Vec3 p1;
	Vec3 p2;
	Vec3 p3;
	U32 c;
	bool filled;
} ASTTri;

AUTO_STRUCT;
typedef struct ASTQuad {
	Vec3 p1;
	Vec3 p2;
	Vec3 p3;
	Vec3 p4;
	U32 c;
} ASTQuad;

AUTO_STRUCT;
typedef struct ASTBox {
	Vec3 local_min;
	Vec3 local_max;
	Mat4 world_mat;
	U32 c;
} ASTBox;

AUTO_STRUCT;
typedef struct ASTPath {
	ASTLine **lines;
} ASTPath;

typedef enum BeaconizerType {
	BEACONIZER_TYPE_NONE = 0,
	BEACONIZER_TYPE_CLIENT,
	BEACONIZER_TYPE_MASTER_SERVER,
	BEACONIZER_TYPE_AUTO_SERVER,
	BEACONIZER_TYPE_SERVER,
	BEACONIZER_TYPE_REQUEST_SERVER,
} BeaconizerType;

typedef enum BeaconMsgClientToServer {
	BMSG_C2S_FIRST_CMD = COMM_MAX_CMD,
	BMSG_C2S_CONNECT,
	BMSG_C2S_SERVER_CONNECT,
	BMSG_C2S_REQUESTER_CONNECT,
	BMSG_C2S_TEXT_CMD,
} BeaconMsgClientToServer;

extern const char* BMSG_C2ST_READY_TO_WORK;
extern const char* BMSG_C2ST_NEED_MORE_MAP_DATA;
extern const char* BMSG_C2ST_MAP_DATA_IS_LOADED;
extern const char* BMSG_C2ST_NEED_MORE_EXE_DATA;
extern const char* BMSG_C2ST_GENERATE_FINISHED;
extern const char* BMSG_C2ST_BEACON_CONNECTIONS;
extern const char* BMSG_C2ST_SERVER_STATUS;
extern const char* BMSG_C2ST_REQUESTER_MAP_DATA;
extern const char* BMSG_C2ST_REQUESTER_CANCEL;
extern const char* BMSG_C2ST_USER_INACTIVE;
extern const char* BMSG_C2ST_BEACON_FILE;
extern const char* BMSG_C2ST_NEED_MORE_BEACON_FILE;
extern const char* BMSG_C2ST_REQUESTED_MAP_LOAD_FAILED;
extern const char* BMSG_C2ST_MAP_COMPLETED;
extern const char* BMSG_C2ST_UNASSIGN;
extern const char* BMSG_C2ST_PING;
extern const char* BMSG_C2ST_DEBUG_MSG;

typedef enum BeaconMsgServerToClient {
	BMSG_S2C_FIRST_CMD = COMM_MAX_CMD,
	BMSG_S2C_CONNECT_REPLY,
	BMSG_S2C_TEXT_CMD,
	BMSG_SET_GALAXY_GROUP_COUNT_CMD,
} BeaconMsgServerToClient;

extern const char* BMSG_S2CT_KILL_PROCESSES;
extern const char* BMSG_S2CT_MAP_DATA;
extern const char* BMSG_S2CT_MAP_DATA_LOADED_REPLY;
extern const char* BMSG_S2CT_EXE_DATA;
extern const char* BMSG_S2CT_NEED_MORE_BEACON_FILE;
extern const char* BMSG_S2CT_PROCESS_LEGAL_AREAS;
extern const char* BMSG_S2CT_BEACON_LIST;
extern const char* BMSG_S2CT_BEACON_CONNECTIONS;
extern const char* BMSG_S2CT_CONNECT_BEACONS;
extern const char* BMSG_S2CT_TRANSFER_TO_SERVER;
extern const char* BMSG_S2CT_CLIENT_CAP;
extern const char* BMSG_S2CT_STATUS_ACK;
extern const char* BMSG_S2CT_REQUEST_CHUNK_RECEIVED;
extern const char* BMSG_S2CT_REQUEST_ACCEPTED;
extern const char* BMSG_S2CT_PROCESS_REQUESTED_MAP;
extern const char* BMSG_S2CT_EXECUTE_COMMAND;
extern const char* BMSG_S2CT_BEACON_FILE;
extern const char* BMSG_S2CT_REGENERATE_MAP_DATA;
extern const char* BMSG_S2CT_PING;
extern const char* BMSG_S2CT_DEBUGSTATE;

typedef struct BeaconClientConnection {
	U32			timeHeardFromServer;
	U32			readyToWork				: 1;
} BeaconClientConnection;

extern BeaconClientConnection beacon_client_conn;

typedef struct BeaconCommon {
	struct {
		struct {
			S32						bitCount;
			S32						hitCount;
		}	defNames,
			defDimensions,
			modelData,
			defs,
			fullVec3,
			indexVec3,
			modelInfo,
			fullDefChild,
			indexDefChild,
			defs2;
	} sent;

	PCLConnectCreateFunc			ccfunc;
	PCLDisconnectAndDestroyFunc		ddfunc;
	PCLSetViewFunc					svfunc;
	PCLSetDefaultViewFunc			dvfunc;
	PCLGetAllFunc					gafunc;
	PCLProcessFunc					pfunc;
	PCLCheckViewFunc				cvfunc;
	PCLNeedsRestartFunc				nrfunc;
	PCLForceFilesFunc				fffunc;
	PCLSetProcessCBFunc				spfunc;
	PCLSetUploadCBFunc				ufunc;
	PCLGetBranchFunc				gbfunc;

	WCICreateFunc					wcicfunc;

	BeaconGetServerListFunc			gslfunc;
	BeaconGetServerNameFunc			gsnfunc;

	char							*masterServerName;
	U32								masterServerIp;
	S64								lastMasterCheck;
	voidVoidFunc					onMasterChangeFunc;

	U32								mapDataLoadedFromPacket : 1;
	U32								productionMode			: 1;
	U32								allowNovodex			: 1;

	U32								connectedToMasterOnce	: 1;
	U32								isSharded				: 1;
	U32								waitingForTransReply	: 1;
} BeaconCommon;

extern BeaconCommon beacon_common;

typedef struct BeaconMapDataPacket BeaconMapDataPacket;
typedef struct WorldCollObjectInfo WorldCollObjectInfo;
typedef struct WorldCollObject WorldCollObject;
typedef struct WorldColl WorldColl;
typedef struct PSDKCookedMesh PSDKCookedMesh;

extern WorldCollObjectInfo **beacon_infos;

AUTO_STRUCT;
typedef struct BeaconProcessDebugState {
	Vec3 debug_pos;						// Only send things debug_dist away or less from this	
	F32 debug_dist;
	U32 send_gen_lines		: 1;
	U32 send_gen_edge		: 1;
	U32 send_combat			: 1;
	U32 send_walk_res		: 1;
	U32 send_prune			: 1;
	U32 send_pre_rebuild	: 1;
	U32 send_post_rebuild	: 1;
} BeaconProcessDebugState;

extern StashTable beaconGeoProximityStash;
extern StashTable beaconGeoProximityStashProcess;

#define BEACONCLIENT_PARTITION 0

// beaconClientServer.c ---------------------------------------------------------------------------

// Useful structures for gathering world info
typedef struct WorldCollObjectInfo WorldCollObjectInfo;

typedef struct WorldCollObjectInstance
{
	WorldCollObjectInfo *info;
	int my_index;
	int my_id;
	Mat4 mat;
	U32 matCRC;
	Mat4 matRound;
	U32 matRoundCRC;
	WorldCollObject *wco;

	int		modelGotten;
	int		shapeGotten;
	U32		instGather				: 1;
	U32		matMatched				: 1;
	U32		noGroundConnections		: 1;
} WorldCollObjectInstance;

typedef struct WorldCollObjectInfo
{
	WorldCollObjectInstance **instances;
	WorldCollStoredModelData *smd;
	PSDKCookedMesh *mesh;
	F32 radius;
	U32 crc;

	U32 prepped : 1;
} WorldCollObjectInfo;

#define COLOR_YELLOW	(COLOR_RED|COLOR_GREEN)
#define COLOR_WHITE		(COLOR_RED|COLOR_GREEN|COLOR_BLUE)

BeaconizerType beaconGetBeaconizerType(void);
int			beaconIsClient(void);
int			beaconIsClientSentry(void);
int			beaconDoShardStuff(void);
bool		beaconCommonCheckMasterName(void);

S32			beaconAcquireMutex(HANDLE hMutex);

HWND		beaconGetConsoleWindow(void);
void 		beaconCheckDuplicates(BeaconDiskSwapBlock* block);
void 		beaconInitCommon(void);
void		beaconInitCommonWorld(void);
void		beaconInitCommonPostWorld(void);
void 		beaconInitGenerating(WorldColl *wc, S32 quiet);
//void 		beaconTestCollision(void);
char*		beaconGetLinkIPStr(NetLink* link);
void 		beaconVerifyUncheckedCount(BeaconDiskSwapBlock* block);
void		beaconLegalAreaCompressedResetBuffer(void);
void 		beaconReceiveColumnAreas(Packet* pak, BeaconLegalAreaCompressed* area);
void 		beaconResetMapData(void);
void		beaconVprintf(S32 color, FORMAT_STR const char* format, va_list argptr);
void 		beaconPrintfDim(S32 color, FORMAT_STR const char* format, ...);
void 		beaconPrintf(S32 color, FORMAT_STR const char* format, ...);
char*		beaconGetExeFileName(void);
char*		beaconGetPdbFileName(void);
char*		beaconGetExeDirectory(void);
S32 		checkForCorrectExePath(const char* exePrefix, const char* cmdLineParams, S32 earlyMutexRelease, S32 hideNewWindow);
U32 		beaconGetExeCRC(const char* fileName, U8** outFileData, U32* outFileSize);
U8* 		beaconFileAlloc(const char* fileName, U32* fileSize);
S32 		beaconDeleteOldExes(const char* exePrefix, S32* attemptCount);
S32 		beaconStartNewExe(const char* exePrefix, U8* data, U32 size, const char* cmdLineParams, S32 exitWhenDone, S32 earlyMutexRelease, S32 hideNewWindow);
void 		beaconHandleNewExe(Packet* pak, const char* exeName, const char* cmdLineParams, S32 earlyMutexRelease, S32 hideNewWindow);
void 		beaconFreeUnusedMemoryPools(void);
S32			beaconCreateNewExe(const char* path, U8* data, U32 size);
void 		beaconReleaseAndCloseMutex(HANDLE* mutexPtr);
void 		beaconPrintMemory(void);
SA_RET_NN_VALID void* beaconMemAlloc(const char* module, U32 size);
void		beaconMemFree(void** memVoid);
char*		beaconStrdup(const char* str);
S32			beaconEnterString(char* buffer, S32 maxLength);
U32			beaconGetCurTime(void);
U32			beaconTimeSince(U32 startTime);
void		beaconResetReceivedMapData(void);
void		beaconMapDataPacketSendChunk(Packet* pak, BeaconMapDataPacket* mapData, U32* sentByteCount);
void		beaconMapDataPacketReceiveChunkHeader(Packet* pak, BeaconMapDataPacket* mapData);
S32			beaconMapDataPacketIsFirstChunk(BeaconMapDataPacket* mapData);
void		beaconMapDataPacketCopyHeader(BeaconMapDataPacket* to, const BeaconMapDataPacket* from);
void		beaconMapDataPacketReceiveChunkData(Packet* pak, BeaconMapDataPacket* mapData);
void		beaconMapDataPacketReceiveChunk(Packet* pak, BeaconMapDataPacket* mapData);
void		beaconMapDataPacketSendChunkAck(Packet* pak, BeaconMapDataPacket* mapData);
S32			beaconMapDataPacketReceiveChunkAck(Packet* pak, U32 sentByteCount, U32* receivedByteCount);
S32			beaconMapDataPacketIsFullyReceived(BeaconMapDataPacket* mapData);
S32			beaconMapDataPacketIsFullySent(BeaconMapDataPacket* mapData, U32 sentByteCount);
U32			beaconMapDataPacketGetSize(BeaconMapDataPacket* mapData);
U8*			beaconMapDataPacketGetData(BeaconMapDataPacket* mapData);
void		beaconMapDataPacketWriteFile(BeaconMapDataPacket* mapData, const char* fileName, const char* uniqueStorageName, U32 timeStamp);
S32			beaconMapDataPacketReadFile(BeaconMapDataPacket* mapData, const char* fileName, char** uniqueStorageName, U32* timeStamp, S32 headerOnly);
U32			beaconMapDataPacketGetReceivedSize(BeaconMapDataPacket* mapData);
void		beaconMapDataPacketDiscardData(BeaconMapDataPacket* mapData);
void		beaconMapDataPacketCreate(BeaconMapDataPacket** mapData);
void		beaconMapDataPacketDestroy(BeaconMapDataPacket** mapData);
S32			beaconMapDataPacketToMapData(BeaconMapDataPacket* mapData, WorldColl **wcInOut);
void		beaconMapDataPacketFromMapData(WorldColl* wc, BeaconMapDataPacket** mapData, S32 fullCRCInfo);
U32			beaconMapDataPacketGetCRC(BeaconMapDataPacket* mapData);
S32			beaconMapDataPacketIsSame(BeaconMapDataPacket* mapData1, BeaconMapDataPacket* mapData2);
void		beaconMapDataPacketClearInitialBeacons(void);
void		beaconMapDataPacketAddInitialBeacon(Vec3 pos, S32 isValidStartingPoint);
void		beaconMapDataPacketInitialBeaconsToRealBeacons(void);
void		beaconHandleCmdLine(S32 argc, char** argv);
S32			beaconizerIsStarting(void);
S32			beaconIsProductionMode(void);
S32			beaconIsSharded(void);
void		beaconGetCommonCmdLine(char* buffer, size_t bufferLen);
// Used to determine what worldcoll to use
WorldColl*	beaconGetActiveWorldColl(int iPartitionIdx);
WorldCollObjectInfo ***beaconGatherObjects(WorldColl *wc);
void		beaconObjectPrep(WorldCollObjectInfo *info);
U32			beaconCRCObject(WorldCollObjectInfo *info, U32 rounded);
S32			beaconU32Cmp(U32 a, U32 b);
S32			beaconObjInstCrcCmp(const WorldCollObjectInstance **inst1, const WorldCollObjectInstance **inst2);
void		beaconPrintObject(WorldCollObjectInfo *info, const char *logfile, U32 rounded);
void		beaconCalcSMDMatMinMaxSlow(const WorldCollStoredModelData *smd, Mat4 world_mat, Vec3 minOut, Vec3 maxOut);
void		destroyBeaconObjectInfo(WorldCollObjectInfo *info);
void		beaconDestroyObjects(void);
BeaconConnection* beaconFindConnection(Beacon *b, Beacon *t, int raised);

// beaconClient.c ---------------------------------------------------------------------------------

S32			beaconClientIsSentry(void);
BeaconProcessDebugState* beaconClientGetProcessDebugState(void);
S32			beaconClientDebugSendWalkResults(void);
NetLink*	beaconClientGetServerLink(void);
WorldColl*	beaconClientGetWorldColl(void);
void		beaconClientReleaseSentryMutex(void);
void		beaconClientSetWorkDuringUserActivity(S32 set);
void		beaconClientStartup(const char* masterServerName, const char* subServerName);
void		beaconClientOncePerFrame(void);
Packet*		beaconClientCreatePacket(NetLink* link, const char* textCmd);
void		beaconClientSendPacket(NetLink* link, Packet** pak);

void		beaconClientSetMMMovementCallbacks(	BeaconConnMovementStartCallback start,
												BeaconConnMovementIsFinishedCallback isfinished,
												BeaconConnMovementResultCallback result);

void		beaconClientGetPatch(void);
char*		beaconClientGetMapname(void);
void		beaconClientSetMapName(char *name);

#define BEACON_CLIENT_PACKET_CREATE_BASE(pak, link, textCmd){							\
			Packet* pak;																\
			Packet* clientPacket__ = pak = beaconClientCreatePacket(link, textCmd);		\
			NetLink* clientNetLink__ = link;											\
			if(pak) {
#define BEACON_CLIENT_PACKET_CREATE_TO_LINK(link, textCmd)								\
			BEACON_CLIENT_PACKET_CREATE_BASE(pak, link, textCmd)
#define BEACON_CLIENT_PACKET_CREATE(textCmd)											\
			BEACON_CLIENT_PACKET_CREATE_TO_LINK(beaconClientGetServerLink(), textCmd)
#define BEACON_CLIENT_PACKET_SEND()														\
			beaconClientSendPacket(clientNetLink__, &clientPacket__);					\
			}}

#define BEACON_CLIENT_PACKET_CREATE_VS(textCmd) beaconClientCreatePacket(beaconClientGetServerLink(), textCmd)
#define BEACON_CLIENT_PACKET_SEND_VS(pak) beaconClientSendPacket(beaconClientGetServerLink(), &(pak))

NetLink*	beaconClientGetServerLink(void);
void		beaconClientUseLocalData(int d);
#if !PLATFORM_CONSOLE
int			beaconClientRunExe(PROCESS_INFORMATION *pi);
#endif
void		beaconClientGetPatch(void);
void		beaconClientSetPatchTime(char *patchTime);

// beaconServer.c ---------------------------------------------------------------------------------

void		beaconServerSetDataToolsRootPath(const char* dataToolsRootPath);
const char* beaconServerGetDataToolsRootPath(void);
const char* beaconServerGetDataPath(void);
const char* beaconServerGetToolsPath(void);
void		beaconServerSetGimmeUsage(S32 on);
void		beaconServerSetRequestCacheDir(const char* cacheDir);
void		beaconServerSetSymStore(void);
void		beaconServerStartup(BeaconizerType beaconizerType, const char* masterServerName, S32 noNetStart, S32 pseudo);
void		beaconServerOncePerFrame(void);
void		beaconRequestBeaconizing(const char* uniqueStorageName);
void		beaconRequestUpdate(void);
void		beaconRequestSetMasterServerAddress(const char* address);
char*		beaconServerGetMapName(void);
char*		beaconServerGetPatchTime(void);
void		beaconServerUploadPatch(void);
void		beaconServerMakeNewExecutable(void);
void		beaconServerGetNewExe(void);
void		beaconServerUseLocalData(int d);
void		beaconServerSendDebugPoint(int msg, const Vec3 p, int ARGB);
void		beaconServerSendDebugLine(int msg, Vec3 p1, Vec3 p2, int ARGB);
BeaconProcessDebugState* beaconServerGetProcessDebugState(void);
int			beaconServerDebugCombatPlace(void);
void		beaconServerEmailMapFailure(BeaconMapFailureReason reason, char *msg);
void		beaconServerSendEmailMsg(const char *title, char **msg, const char *to);
void		beaconServerWriteFileCallback(const void* data, U32 size);
void		beaconTestGeoProximity(int iPartitionIdx);
void		beaconTestProcessGeoProximity(void);

// ------------------------------------------------------------------------------------------------
