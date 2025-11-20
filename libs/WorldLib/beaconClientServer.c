#if !PLATFORM_CONSOLE

#include "beaconClientServerPrivate.h"
//#include "Entity.h"
//#include "bases.h"
//#include "basedata.h"
#include "WorldGrid.h"
#include "MemoryPool.h"
#include "sock.h"
#include "PhysicsSDK.h"
#include "cmdparse.h"
#include "wlState.h"
#include "netpacketutil.h"
#include "logging.h"
#include "StringCache.h"
#include "utilitiesLib.h"
#include "wlCommandParse.h"
#include "bounds.h"
#include "fastAtoi.h"
#include "UTF8.h"

// Client-To-Server Messages.

const char* BMSG_C2ST_READY_TO_WORK				= "ReadyToWork";
const char* BMSG_C2ST_NEED_MORE_MAP_DATA		= "NeedMoreMapData";
const char* BMSG_C2ST_MAP_DATA_IS_LOADED		= "MapDataIsLoaded";
const char* BMSG_C2ST_NEED_MORE_EXE_DATA		= "NeedMoreExeData";
const char* BMSG_C2ST_GENERATE_FINISHED			= "GenerateFinished";
const char* BMSG_C2ST_BEACON_CONNECTIONS		= "BeaconConnections";
const char* BMSG_C2ST_SERVER_STATUS				= "ServerStatus";
const char* BMSG_C2ST_REQUESTER_MAP_DATA		= "RequesterMapData";
const char* BMSG_C2ST_REQUESTER_CANCEL			= "RequesterCancel";
const char* BMSG_C2ST_USER_INACTIVE				= "UserInactive";
const char* BMSG_C2ST_BEACON_FILE				= "BeaconFile";
const char* BMSG_C2ST_NEED_MORE_BEACON_FILE		= "NeedMoreBeaconFile";
const char* BMSG_C2ST_REQUESTED_MAP_LOAD_FAILED	= "RequestedMapLoadFailed";
const char* BMSG_C2ST_MAP_COMPLETED				= "MapCompleted";
const char*	BMSG_C2ST_UNASSIGN					= "Unassign";
const char* BMSG_C2ST_PING						= "Ping";
const char* BMSG_C2ST_DEBUG_MSG					= "Debug";
const char* BMSG_C2ST_SENTRY_CLIENT_COUNT		= "SentryClientCount";

// Server-To-Client Messages.

const char* BMSG_S2CT_KILL_PROCESSES			= "KillProcesses";
const char* BMSG_S2CT_MAP_DATA					= "MapData";
const char* BMSG_S2CT_MAP_DATA_LOADED_REPLY		= "MapDataLoadedReply";
const char* BMSG_S2CT_EXE_DATA					= "ExeData";
const char* BMSG_S2CT_NEED_MORE_BEACON_FILE		= "NeedMoreBeaconFile";
const char* BMSG_S2CT_PROCESS_LEGAL_AREAS		= "ProcessLegalAreas";
const char* BMSG_S2CT_BEACON_LIST				= "BeaconList";
const char* BMSG_S2CT_BEACON_CONNECTIONS		= "BeaconConns";
const char* BMSG_S2CT_CONNECT_BEACONS			= "ConnectBeacons";
const char* BMSG_S2CT_CONNECTION_INFO			= "ConnectionInfo";		// inform client of new connections
const char* BMSG_S2CT_TRANSFER_TO_SERVER		= "TransferToServer";
const char* BMSG_S2CT_CLIENT_CAP				= "ClientCap";
const char* BMSG_S2CT_STATUS_ACK				= "StatusAck";
const char* BMSG_S2CT_REQUEST_CHUNK_RECEIVED	= "RequestChunkReceived";
const char* BMSG_S2CT_REQUEST_ACCEPTED			= "RequestAccepted";
const char* BMSG_S2CT_PROCESS_REQUESTED_MAP		= "ProcessRequestedMap";
const char* BMSG_S2CT_EXECUTE_COMMAND			= "ExecuteCommand";
const char* BMSG_S2CT_BEACON_FILE				= "BeaconFile";
const char* BMSG_S2CT_REGENERATE_MAP_DATA		= "RegenerateMapData";
const char* BMSG_S2CT_PING						= "Ping";
const char* BMSG_S2CT_DEBUGSTATE				= "DebugState";

// The global structure for storing and transferring map data.

typedef struct BeaconMapDataPacket {
	struct {
		S32						previousSentByteCount;
		S32						currentByteCount;
	} header;
	
	struct {
		U8*						data;
		U32						byteCount;
		U32						crc;
	} compressed;
	
	U32							uncompressedBitCount;
	U32							uncompressedByteCount;
	
	U32							receivedByteCount;
} BeaconMapDataPacket;

// Other globals.

BeaconClientConnection beacon_client_conn;

static struct {
	BeaconizerType	clientServerType;
	int		pseudoauto;
	char*	masterServerName;
	char*	subServerName;
	S32		noNetStart;
	char*	beaconRequestCacheDir;
} beaconizerInit = {0};	

BeaconCommon beacon_common;

static void beaconCollObjectMsgHandler(const WorldCollObjectMsg* msg);

HWND beaconGetConsoleWindow(void){
	return compatibleGetConsoleWindow();
}

MP_DEFINE(BeaconLegalAreaCompressed);

static int g_BLACCount = 0;
static int g_BLACMax = SQR(BEACON_GENERATE_CHUNK_SIZE)*9*32;  // 9 = 9 blocks on client, 32 = decent buffer
static BeaconLegalAreaCompressed* g_BLACBuffer = NULL;

void beaconLegalAreaCompressedResetBuffer(void)
{
	if(!g_BLACBuffer)
		g_BLACBuffer = calloc(g_BLACMax, sizeof(BeaconLegalAreaCompressed));

#if BEACONGEN_STORE_AREAS || BEACONGEN_CHECK_VERTS
	{
		int i;
		for(i=0; i<g_BLACCount; i++)
			destroyBeaconLegalAreaCompressed(&g_BLACBuffer[i]);
	}
#endif
	ZeroStructs(g_BLACBuffer, g_BLACCount);
	g_BLACCount = 0;
}

BeaconLegalAreaCompressed* createBeaconLegalAreaCompressed(void){

	if(beaconIsClient())
	{
		assertmsg(g_BLACCount<g_BLACMax, "Not enough preallocated BeaconLegalAreaCompressed.  Programmer needs to inrease.");
		return &g_BLACBuffer[g_BLACCount++];		
	}
	else
	{
		MP_CREATE(BeaconLegalAreaCompressed, 10000);
		
		return MP_ALLOC(BeaconLegalAreaCompressed);
	}
}

void destroyBeaconLegalAreaCompressed(BeaconLegalAreaCompressed* area){
	S32 i = 0;
	
	if(!area){
		return;
	}
	
	#if BEACONGEN_STORE_AREAS
		#if BEACONGEN_CHECK_VERTS
			for(i = 0; i < area->areas.count; i++){
				beaconMemFree(&area->areas.areas[i].defName);
			}
		#endif
		
		SAFE_FREE(area->areas.areas);
		
		ZeroStruct(&area->areas);
	#endif
	
	#if BEACONGEN_CHECK_VERTS
		for(i = 0; i < area->tris.count; i++){
			beaconMemFree(&area->tris.tris[i].defName);
		}
		
		SAFE_FREE(area->tris.tris);
		
		ZeroStruct(&area->tris);
	#endif
	
	if(!beaconIsClient())
		MP_FREE(BeaconLegalAreaCompressed, area);
}

BeaconLegalAreaCompressed* beaconAddLegalAreaCompressed(BeaconDiskSwapBlock* block){
	BeaconLegalAreaCompressed* legalArea = createBeaconLegalAreaCompressed();

	legalArea->next = block->legalCompressed.areasHead;
	block->legalCompressed.areasHead = legalArea;
	block->legalCompressed.totalCount++;
	
	return legalArea;
}

void beaconVprintf(S32 color, const char* format, va_list argptr){
	if(color){
		consoleSetColor(color, 0);
	}
	vprintf(format, argptr);
	consoleSetDefaultColor();
}

void beaconPrintfDim(S32 color, const char* format, ...){
	va_list argptr;
	
	va_start(argptr, format);
	beaconVprintf(color, format, argptr);
	va_end(argptr);
}

void beaconPrintf(S32 color, const char* format, ...){
	va_list argptr;
	
	va_start(argptr, format);
	beaconVprintf(COLOR_BRIGHT|color, format, argptr);
	va_end(argptr);
}

AUTO_COMMAND ACMD_NAME(beaconserver) ACMD_HIDE;
void beaconInitServer(int not_used)
{
	devassertmsg(!beaconizerInit.clientServerType, "Cannot specify multiple types of beacon app");
	beaconizerInit.clientServerType = BEACONIZER_TYPE_SERVER;

	globCmdParsef("loadallusernamespaces 1");
	globCmdParsef("beaconlf 1");
}

AUTO_COMMAND ACMD_NAME(beaconmasterserver) ACMD_HIDE;
void beaconInitMaster(int not_used)
{
	devassertmsg(!beaconizerInit.clientServerType, "Cannot specify multiple types of beacon app");
	beaconizerInit.clientServerType = BEACONIZER_TYPE_MASTER_SERVER;

	globCmdParsef("-beginstatusreporting MasterBeaconServer newfcdev 80");
}

AUTO_COMMAND ACMD_NAME(beaconautoserver) ACMD_HIDE;
void beaconInitAuto(int not_used)
{
	devassertmsg(!beaconizerInit.clientServerType, "Cannot specify multiple types of beacon app");
	beaconizerInit.clientServerType = BEACONIZER_TYPE_AUTO_SERVER;

	globCmdParsef("-beginstatusreporting %sBeaconizer newfcdev 80", GetProductName());
	globCmdParsef("loadallusernamespaces 1");
	globCmdParsef("beaconlf 1");
}

AUTO_COMMAND ACMD_NAME(beaconrequestserver) ACMD_HIDE;
void beaconInitRequest(int not_used)
{
	devassertmsg(!beaconizerInit.clientServerType, "Cannot specify multiple types of beacon app");
	beaconizerInit.clientServerType = BEACONIZER_TYPE_REQUEST_SERVER;

	globCmdParsef("beaconlf 1");
}

AUTO_COMMAND ACMD_NAME(beaconpseudoauto) ACMD_HIDE;
void beaconInitPseudo(int not_used)
{
	devassertmsg(!beaconizerInit.clientServerType, "Cannot specify multiple types of beacon app");
	beaconizerInit.clientServerType = BEACONIZER_TYPE_SERVER;
	beaconizerInit.pseudoauto = 1;

	globCmdParsef("loadallusernamespaces 1");
	globCmdParsef("beaconlf 1");
}

AUTO_COMMAND ACMD_NAME(beaconclient) ACMD_HIDE;
void beaconInitClient(int not_used)
{
	devassertmsg(!beaconizerInit.clientServerType, "Cannot specify multiple types of beacon app");
	beaconizerInit.clientServerType = BEACONIZER_TYPE_CLIENT;

	globCmdParsef("beaconlf 1");
}

AUTO_COMMAND ACMD_NAME(beaconmastername) ACMD_HIDE;
void beaconInitMasterServerName(char* servername)
{
	beaconizerInit.masterServerName = strdup(servername);
}

AUTO_COMMAND ACMD_NAME(beaconservername) ACMD_HIDE;
void beaconInitSubServerName(char* servername)
{
	beaconizerInit.subServerName = strdup(servername);
}

int runBeaconApp()
{
	return !!beaconizerInit.clientServerType;
}

void beaconInitCommon(void){
	CONSOLE_CURSOR_INFO info = {0};

	// Make the blinking cursor big, for olde timey fun.
	
	info.dwSize = 100;
	info.bVisible = 1;
	SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
	timeSecondsSince2000EnableCache(0);
}

// Function for initializing world stuff that the server and client need but master server and sentry do not.
void beaconInitCommonWorld(void)
{
}

void beaconInitCommonPostWorld(void)
{
	beacon_common.wcicfunc();
}

void beaconCheckDuplicates(BeaconDiskSwapBlock* block){
	BeaconLegalAreaCompressed* area;
	BeaconLegalAreaCompressed* area2;
	
	return;
	
	for(area = block->legalCompressed.areasHead; area; area = area->next){
		for(area2 = block->legalCompressed.areasHead; area2; area2 = area2->next){
			if(area == area2){
				continue;
			}
			
			if(	area->isIndex == area2->isIndex &&
				area->x == area2->x &&
				area->z == area2->z &&
				(	area->isIndex && area->y_index == area2->y_index ||
					!area->isIndex && area->y_coord == area2->y_coord))
			{
				assert(0);
			}
		}		
	}
}

void beaconVerifyUncheckedCount(BeaconDiskSwapBlock* block){
	BeaconLegalAreaCompressed* area;
	S32 count = 0;
	
	return;
	
	for(area = block->legalCompressed.areasHead; area; area = area->next){
		if(!area->checked){
			count++;
		}
	}
	
	assert(count == block->legalCompressed.uncheckedCount);
	
	beaconCheckDuplicates(block);
}

void beaconInitGenerating(WorldColl *wc, S32 quiet){
	beaconClearBeaconData();
	
	// Initialize generator.
	
	beaconResetGenerator();

	// Measure everything.

	beaconMeasureWorld(wc, quiet);

	beaconGenerateInitBlocks(quiet);
}

void beaconReceiveColumnAreas(Packet* pak, BeaconLegalAreaCompressed* area){
	area->areas.count = pktGetBitsPack(pak, 1);
	
	#if BEACONGEN_STORE_AREAS
	{
		S32 newCount = 0;
		S32 i;
		
		dynArrayAddStructs(area->areas.areas, newCount, area->areas.maxCount, area->areas.count);
			
		for(i = 0; i < newCount; i++){
			area->areas.areas[i].y_min = pktGetF32(pak);
			area->areas.areas[i].y_max = pktGetF32(pak);

			#if BEACONGEN_CHECK_VERTS
			{
				S32 j;
				
				for(j = 0; j < 3; j++){
					area->areas.areas[i].triVerts[j][0] = pktGetF32(pak);
					area->areas.areas[i].triVerts[j][1] = pktGetF32(pak);
					area->areas.areas[i].triVerts[j][2] = pktGetF32(pak);
				}
				
				beaconMemFree(&area->areas.areas[i].defName);
				
				area->areas.areas[i].defName = beaconStrdup(pktGetStringTemp(pak));
			}
			#endif
		}

		#if BEACONGEN_CHECK_VERTS
		{
			area->tris.count = 0;
			
			while(pktGetBits(pak, 1) == 1){
				S32 j;
				i = area->tris.count;
			
				dynArrayAddStruct(area->tris.tris, area->tris.count, area->tris.maxCount);
				
				beaconMemFree(&area->tris.tris[i].defName);
				area->tris.tris[i].defName = beaconStrdup(pktGetStringTemp(pak));

				area->tris.tris[i].y_min = pktGetF32(pak);
				area->tris.tris[i].y_max = pktGetF32(pak);

				for(j = 0; j < 3; j++){
					area->tris.tris[i].verts[j][0] = pktGetF32(pak);
					area->tris.tris[i].verts[j][1] = pktGetF32(pak);
					area->tris.tris[i].verts[j][2] = pktGetF32(pak);
				}
			}
		}
		#endif
	}
	#endif
}

char* beaconGetLinkIPStr(NetLink* link){
	static char buf[100];

	return linkGetIpStr(link,buf,sizeof(buf));
}

//void beaconTestCollision(void){
//	S32 x;
//	S32 z;
//	
//	printf("\n\n");
//
//	for(z = bp_blocks.grid_max_xyz[2]; z >= bp_blocks.grid_min_xyz[2]; z--){
//		for(x = bp_blocks.grid_min_xyz[0]; x <= bp_blocks.grid_max_xyz[0]; x++){
//			BeaconDiskSwapBlock* block = beaconGetDiskSwapBlockByGrid(x, z);
//			
//			if(!block){
//				printf("%6s", "");
//			}else{
//				Vec3 pos = {x * BEACON_GENERATE_CHUNK_SIZE + BEACON_GENERATE_CHUNK_SIZE / 2,
//							beacon_process.world_max_xyz[1],
//							z * BEACON_GENERATE_CHUNK_SIZE + BEACON_GENERATE_CHUNK_SIZE / 2};
//				Vec3 pos2;
//				CollInfo coll;
//
//				copyVec3(pos, pos2);
//				
//				pos2[1] = beacon_process.world_min_xyz[1];
//				
//				if(block){
//					if(!block->geoRefs.count){
//						consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
//					}
//					else if(block->clients.count){
//						consoleSetColor(COLOR_BRIGHT|COLOR_GREEN|COLOR_BLUE, 0);
//					}
//					else if(block->legalCompressed.uncheckedCount){
//						consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
//					}
//					else if(block->legalCompressed.totalCount){
//						consoleSetColor(COLOR_BRIGHT|COLOR_GREEN|COLOR_BLUE, 0);
//					}
//					else{
//						consoleSetColor(COLOR_GREEN, 0);
//					}
//				}else{
//					consoleSetColor(COLOR_RED, 0);
//				}
//
//				if(collGrid(NULL, pos, pos2, &coll, 0, COLL_NOTSELECTABLE | COLL_DISTFROMSTART | COLL_BOTHSIDES)){
//					printf("%6.1f", coll.mat[3][1]);
//				}else{
//					printf("%6s", "[----]");
//				}
//			}
//		}
//		
//		printf("\n");
//	}
//
//	printf("\n\n");
//
//	consoleSetDefaultColor();
//}

U8* beaconFileAlloc(const char* fileName, U32* fileSize){
	HANDLE h;
	DWORD size;
	U8* data;
	DWORD outSize;
	
	h = CreateFile_UTF8(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
	if(h == INVALID_HANDLE_VALUE){
		printf("Can't open file to read: %d\n", GetLastError());
		return NULL;
	}
	
	size = GetFileSize(h, NULL);
	data = beaconMemAlloc("exeFile", size);

	if(fileSize){
		*fileSize = size;
	}
	
	if(!ReadFile(h, data, size, &outSize, NULL)){
		printf("Can't read from file: %d\n", GetLastError());
		return NULL;
	}
	
	CloseHandle(h);
	
	return data;
}

static U32 freshCRC(void* dataParam, U32 length){
	cryptAdler32Init();
	
	return cryptAdler32((U8*)dataParam, length);
}

U32 beaconGetExeCRC(const char* fileName, U8** outFileData, U32* outFileSize){
	S32 fileSize;
	U8* fileData = beaconFileAlloc(fileName, &fileSize);
	U32 crc;
	
	if(!fileData){
		outFileData = NULL;
		return 0;
	}
	
	crc = fileData ? freshCRC(fileData, fileSize) : 0;
	
	if(outFileData){
		*outFileData = fileData;
		*outFileSize = fileSize;
	}else{
		beaconMemFree((void**)&fileData);
	}
	
	return crc;
}
	
static void beaconFreeUnusedMemoryPoolsHelper(MemoryPool pool, void *unused_param){
	if(!mpGetAllocatedCount(pool) && mpGetAllocatedChunkMemory(pool)){
		char fileName[1000];
		
		strcpy(fileName, mpGetFileName(pool) ? mpGetFileName(pool) : "nofile");
		
		if(0){
			beaconPrintf(COLOR_RED|COLOR_GREEN,
						"Freeing memory pool %s(%s:%d): %s bytes.\n",
						mpGetName(pool) ? mpGetName(pool) : "???",
						getFileName(fileName),
						mpGetFileLine(pool),
						getCommaSeparatedInt(mpGetAllocatedChunkMemory(pool)));
		}
		
		mpFreeAllocatedMemory(pool);
	}
}

void beaconFreeUnusedMemoryPools(void){
	mpForEachMemoryPool(beaconFreeUnusedMemoryPoolsHelper,NULL);
}

void beaconResetMapData(void){
	if(beacon_common.mapDataLoadedFromPacket){
		// Only do this stuff for clients.
		
		beacon_common.mapDataLoadedFromPacket = 0;
	}else{
	}
	
	beaconClearBeaconData();
	
	SAFE_FREE(beacon_process.infoArray);
	
	beaconResetGenerator();

	beaconFreeUnusedMemoryPools();
}

S32 beaconCreateNewExe(const char* path, U8* data, U32 size){
	char buffer[1000];
	DWORD outSize;
	HANDLE h;
	S32 i;
	
	makeDirectoriesForFile(path);

	// Create the new file.
	
	for(i = 0;; i++){
		S32 error;
		
		h = CreateFile_UTF8(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
		
		if(h != INVALID_HANDLE_VALUE){
			break;
		}
		
		error = GetLastError();
		printf("%2d. Failed to open file (error %d): %s\n", i + 1, error, path);
		Sleep(500);

		if(i == 99){
			sprintf(buffer, "Can't open file (error %d): %s\n\nPress IGNORE to continue trying.\n", error, path);
			printf("%s\n", buffer);
			assertmsg(0, buffer);
			i = -1;
		}
	}
			
	if(!WriteFile(h, data, size, &outSize, NULL)){
		sprintf(buffer, "Can't write to file (error %d): %s\n", GetLastError(), path);
		printf("%s\n", buffer);
		assertmsg(0, buffer);
		CloseHandle(h);
		return 0;
	}
	
	CloseHandle(h);
	
	return 1;
}

BeaconizerType beaconGetBeaconizerType(void)
{
	return beaconizerInit.clientServerType;
}

int beaconIsClient(void)
{
	return beaconizerInit.clientServerType==BEACONIZER_TYPE_CLIENT;
}

int beaconIsClientSentry(void)
{
	return beaconizerInit.clientServerType==BEACONIZER_TYPE_CLIENT &&
			beaconClientIsSentry();
}

int	beaconDoShardStuff(void)
{
	return beaconIsSharded() && (!beaconIsClient() || beaconIsClientSentry());
}

S32 beaconDeleteOldExes(const char* exeName, S32* attemptCount){
	intptr_t p;
	struct _finddata_t info;
	S32 deletedFiles = 0;
	char buffer[1000];
	
	if(attemptCount){
		attemptCount[0] = 0;
	}

	sprintf(buffer, "c:/beaconizer/beaconcopy*");
	
	p = findfirst_SAFE(buffer, &info);
	
	if(p != -1){
		do{
			char* fileName;
			strcpy(buffer, exeName);
			fileName = getFileName(buffer);

			if(attemptCount){
				attemptCount[0]++;
			}
			
			// Delete the file.
			
			sprintf(buffer, "c:/beaconizer/%s/%s", info.name, exeName);
			beaconPrintf(COLOR_RED, "Deleting: %s\n", buffer);
			
			if(!DeleteFile_UTF8(buffer)){
				beaconPrintf(COLOR_YELLOW, "  FAILED!!!\n");
			}else{
				beaconPrintf(COLOR_GREEN, "  DONE!!!\n");
				deletedFiles++;
			}

			// Delete the minidump if it exists.
			
			sprintf(buffer, "c:/beaconizer/%s/%s.mdmp", info.name, exeName);
			beaconPrintf(COLOR_RED, "Deleting: %s\n", buffer);
			
			if(!DeleteFile_UTF8(buffer)){
				beaconPrintf(COLOR_YELLOW, "  FAILED!!!\n");
			}else{
				beaconPrintf(COLOR_GREEN, "  DONE!!!\n");
				deletedFiles++;
			}

			// Delete the folder.
			
			sprintf(buffer, "c:/beaconizer/%s", info.name);
			beaconPrintf(COLOR_RED, "Deleting: %s\n", buffer);

			if(!RemoveDirectory_UTF8(buffer)){
				beaconPrintf(COLOR_YELLOW, "  FAILED!!!\n");
			}else{
				beaconPrintf(COLOR_GREEN, "  DONE!!!\n");
				deletedFiles++;
			}
		}while(!findnext_SAFE(p, &info));
		
		_findclose(p);
	}
	
	sprintf(buffer, "c:/beaconizer/beacon*.exe");
	
	p = findfirst_SAFE(buffer, &info);
	
	if(p != -1){
		do{
			if(	stricmp(info.name, "BeaconServer.exe") &&
				stricmp(info.name, "BeaconClient.exe"))
			{
				// Delete the file.
				
				sprintf(buffer, "c:/beaconizer/%s", info.name);
				beaconPrintf(COLOR_RED, "Deleting: %s\n", buffer);
				
				if(!DeleteFile_UTF8(buffer)){
					beaconPrintf(COLOR_YELLOW, "  FAILED!!!\n");
				}else{
					beaconPrintf(COLOR_GREEN, "  DONE!!!\n");
					deletedFiles++;
				}
			}
		}while(!findnext_SAFE(p, &info));
	}
	
	return deletedFiles;
}

S32 beaconStartNewExe(	const char* exeName,
						U8* data,
						U32 size,
						const char* cmdLineParams,
						S32 exitWhenDone,
						S32 earlyMutexRelease,
						S32 hideNewWindow)
{
	beaconClientRunExe(NULL);
	
	return 1;
}

void beaconHandleNewExe(Packet* pak,
						const char* exeName,
						const char* cmdLineParams,
						S32 earlyMutexRelease,
						S32 hideNewWindow)
{
	U32 size;
	U8*	data;

	consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
	printf("Server sent new exe file.\n");

	consoleSetDefaultColor();
	size = pktGetBitsPack(pak, 1);
	data = beaconMemAlloc("newExe", size);
	pktGetBytes(pak, size, data);

	//beaconStartNewExe(exeName, data, size, cmdLineParams, 1, earlyMutexRelease, hideNewWindow);
}

S32 beaconAcquireMutex(HANDLE hMutex)
{
	DWORD result;
	WaitForSingleObjectWithReturn(hMutex, 0, result);

	return	result == WAIT_OBJECT_0 ||
		result == WAIT_ABANDONED;
}

void beaconReleaseAndCloseMutex(HANDLE* mutexPtr){
	if(*mutexPtr){
		ReleaseMutex(*mutexPtr);
		CloseHandle(*mutexPtr);
		*mutexPtr = NULL;
	}
}

static StashTable htBeaconMem;

typedef struct BeaconMemoryModule {
	struct BeaconMemoryModule*	self;
	const char*					name;
	U32							count;
	U32							size;
	U32							opCount;
} BeaconMemoryModule;

static BeaconMemoryModule* beaconUpdateMemory(const char* module, S32 create, U32 size){
	StashElement el;
	BeaconMemoryModule* bmm;
	
	if(!htBeaconMem){
		htBeaconMem = stashTableCreateWithStringKeys(100, StashDeepCopyKeys_NeverRelease);
	}
	
	stashFindElement(htBeaconMem, module, &el);
	
	if(!el){
		bmm = calloc(sizeof(*bmm), 1);
		bmm->self = bmm;
		bmm->name = module;
		stashAddPointer(htBeaconMem, module, bmm, false);
	}else{
		bmm = stashElementGetPointer(el);
		assert(bmm && bmm->self == bmm);
	}
	
	bmm->opCount++;

	if(create){
		//printf("alloc[%20s]: %10d 0x%8.8x\n", module, size);
		
		bmm->count++;
		bmm->size += size;
	}else{
		//printf("free [%20s]: %10d 0x%8.8x\n", module, size);

		assert(bmm->count > 0);
		bmm->count--;
		bmm->size -= size;
	}
	
	return bmm;
}

void beaconPrintMemory(void){
	memMonitorDisplayStats();

	if(htBeaconMem){
		StashTableIterator it;
		StashElement el;
		
		stashGetIterator(htBeaconMem, &it);
		
		while(stashGetNextElement(&it, &el)){
			BeaconMemoryModule* bmm = stashElementGetPointer(el);
			
			printf("%20s: %10d %10d bytes\n", bmm->name, bmm->count, bmm->size);
		}
	}
}

void* beaconMemAlloc(const char* module, U32 size){
	void** mem = calloc(size + 2 * sizeof(*mem), 1);
	
	mem[0] = (void*)(intptr_t)size;
	mem[1] = beaconUpdateMemory(module, 1, size);

	return mem + 2;
}

void beaconMemFree(void** memVoid){
	if(*memVoid){
		BeaconMemoryModule** mem = *memVoid;
		BeaconMemoryModule* bmm = (BeaconMemoryModule*)*--mem;
		
		assert(bmm && bmm->self == bmm);
		
		beaconUpdateMemory(bmm->name, 0, (intptr_t)*--mem);
		
		free(mem);
		
		*memVoid = NULL;
	}
}

char* beaconStrdup(const char* str){
	char* mem = beaconMemAlloc("strdup", (S32)strlen(str) + 1);
	
	strcpy_s(mem, strlen(str)+1, str);
	
	return mem;
}

S32 beaconIsGoodStringChar(char c){
	return	isalnum(c) ||
			strchr(" !@#$%^&*()-_=_[{]}\\|;:'\",<.>/?`~", c);
}

S32 beaconEnterString(char* buffer, S32 maxLength){
	S32 length = 0;
	
	buffer[length] = 0;
	
	while(1){
		S32 key = _getch();

		switch(key){
			xcase 0:
			case 224:{
				S32 unused = _getch();
			}
			
			xcase 27:{
				while(length > 0){
					backSpace(1, 1);
					buffer[--length] = 0;
				}
				return 0;
			}
			
			xcase 13:{
				return 1;
			}
			
			xcase 8:{
				if(length > 0){
					backSpace(1, 1);
					buffer[--length] = 0;
				}
			}
			
			xcase 22:{
				if(OpenClipboard(beaconGetConsoleWindow())){
					HANDLE handle = GetClipboardData(CF_TEXT);

					if(handle){
						char* data = GlobalLock(handle);
						
						if(data){
							while(	*data &&
									length < maxLength &&
									beaconIsGoodStringChar(*data))
							{
								buffer[length++] = *data;
								buffer[length] = 0;
								printf("%c", *data);
								
								data++;
							}
							
							GlobalUnlock(handle);
						}
					}

					CloseClipboard();
				}
			}
			
			xdefault:{
				if(	length < maxLength &&
					beaconIsGoodStringChar(key))
				{
					buffer[length++] = key;
					buffer[length] = 0;
					printf("%c", key);
				}
			}
		}
	}
}

U32 beaconGetCurTime(void){
	return timeSecondsSince2000();
}

U32 beaconTimeSince(U32 startTime){
	return beaconGetCurTime() - startTime;
}

#define START_BYTES(pak) {S32 serverStartBits = pak->stream.bitLength;
#define STOP_BYTES(pak, var) beacon_common.sent.var.hitCount++;beacon_common.sent.var.bitCount += pak->stream.bitLength - serverStartBits;}

void beaconMapDataPacketCreate(BeaconMapDataPacket** mapData){
	*mapData = calloc(1, sizeof(BeaconMapDataPacket));
}

void beaconMapDataPacketDestroy(BeaconMapDataPacket** mapData){
	if(!mapData || !*mapData){
		return;
	}

	SAFE_FREE((*mapData)->compressed.data);
	ZeroStruct(*mapData);
	SAFE_FREE(*mapData);
}

typedef struct InitialBeacon {
	Vec3	pos;
	U32		isValid : 1;
} InitialBeacon;

struct {
	InitialBeacon*	beacons;
	S32				count;
	S32				maxCount;
} initial_beacons;

void beaconMapDataPacketClearInitialBeacons(void){
	SAFE_FREE(initial_beacons.beacons);
	ZeroStruct(&initial_beacons);
}

void beaconMapDataPacketAddInitialBeacon(Vec3 pos, S32 isValidStartingPoint){
	InitialBeacon* beacon = dynArrayAddStruct(	initial_beacons.beacons,
												initial_beacons.count,
												initial_beacons.maxCount);
										
	copyVec3(pos, beacon->pos);
	beacon->isValid = isValidStartingPoint ? 1 : 0;
}

void beaconMapDataPacketInitialBeaconsToRealBeacons(void){
	S32 i;
	
	for(i = 0; i < initial_beacons.count; i++){
		InitialBeacon* initialBeacon = initial_beacons.beacons + i;
		Beacon* beacon = addCombatBeacon(initialBeacon->pos, 1, 1, 1, 0);
		
		if(beacon){
			beacon->isValidStartingPoint = initialBeacon->isValid ? 1 : 0;
		}
	}
}

void beaconGatherStoredData(void* userPointer,
							const WorldCollObjectTraverseParams* params)
{
	WorldCollStoredModelData *smd = NULL;
	WorldCollModelInstanceData *instInfo = NULL;
	WorldCollObjectInfo ***ea = userPointer;
	static StashTable smdNames = NULL;

	if(!smdNames)
	{
		smdNames = stashTableCreateWithStringKeys(30, StashDefault);
	}

	if(params->first)
	{
		stashTableClear(smdNames);
	}

	if(wcoGetStoredModelData(&smd, &instInfo, params->wco, WC_FILTER_BIT_MOVEMENT))
	{
		if(!instInfo->transient)
		{
			WorldCollObjectInfo *info = NULL;
			WorldCollObjectInstance *inst = NULL;

			inst = callocStruct(WorldCollObjectInstance);
			copyMat4(instInfo->world_mat, inst->mat);
			inst->noGroundConnections = instInfo->noGroundConnections;

			inst->wco = params->wco;
			if(beaconIsClient())
			{
				//WorldCollObjectInstance *other;
				//
				//if(!wcoGetUserPointer(params->wco, beaconCollObjectMsgHandler, &other)){
				//	assert(0);
				//}
				//ASSERT_FALSE_AND_SET(other->instGather);
			}

			if(stashFindPointer(smdNames, smd->name, &info))
			{
				assert(info);
				assert(info->smd==smd);
			}
			else
			{
				info = callocStruct(WorldCollObjectInfo);
				info->smd = smd;

				eaPush(ea, info);

				stashAddPointer(smdNames, smd->name, info, 1);
			}

			eaPush(&info->instances, inst);
		}
	}
	else
	{
		if(beaconIsClient())
		{
			assert(0);
		}
	}

	SAFE_FREE(instInfo);
}

WorldCollObjectInfo **beacon_infos = NULL;
WorldCollObjectInfo*** beaconGatherObjects(WorldColl *wc)
{
	if(!beacon_infos)
	{
		wcTraverseObjects(wc, beaconGatherStoredData, (void*)&beacon_infos, NULL, NULL, /*unique=*/1, WCO_TRAVERSE_STATIC);
	}

	return &beacon_infos;
}

void destroyBeaconObjectInfo(WorldCollObjectInfo *info)
{
	// Everything else should be taken care of by destroying the SMD itself.
	eaDestroyEx(&info->instances, NULL);
	free(info);
}

U32 beaconCRCMat4(Mat4 mat)
{
	return freshCRC((U8*)mat, sizeof(mat));
}

S32	beaconU32Cmp(U32 a, U32 b)
{
	return a > b ? 1 : a==b ? 0 : -1;
}

S32 beaconObjInstCrcCmp(const WorldCollObjectInstance **inst1, const WorldCollObjectInstance **inst2)
{
	if((*inst1)->matCRC == (*inst2)->matCRC)
		return beaconU32Cmp((*inst1)->matRoundCRC, (*inst2)->matRoundCRC);

	return beaconU32Cmp((*inst1)->matCRC, (*inst2)->matCRC);
}

void beaconPrintObject(WorldCollObjectInfo *info, const char *logfile, U32 rounded)
{
	int i;
	char nameline[MAX_PATH];
	char minmax[MAX_PATH];
	char trivertline[MAX_PATH];

	beaconObjectPrep(info);
	info->crc = beaconCRCObject(info, rounded);

	sprintf(nameline, "Detail: %s | CRC: %x | Insts: %d | verts %d | tris %d\n", 
			info->smd->detail, info->crc, eaSize(&info->instances), 
			info->smd->tri_count, info->smd->vert_count);
	if(logfile)
	{
		filelog_printf(logfile, "%s", nameline);
	}
	else
	{
		printf("%s", nameline);
	}
	sprintf(minmax, "Min: %.3f, %.3f, %.3f | Max: %.3f, %.3f, %.3f\n", vecParamsXYZ(info->smd->min), vecParamsXYZ(info->smd->max));
	if(logfile)
	{
		filelog_printf(logfile, "%s", minmax);
	}
	else
	{
		printf("%s", minmax);
	}
	for(i=0; i<eaSize(&info->instances); i++)
	{
		int j;
		char instline[MAX_PATH];
		WorldCollObjectInstance *inst = info->instances[i];
		Mat4 copy;

		// Can't save it because these matrices are sent to the beacon clients
		copyMat4(inst->mat, copy);

		for(j=0; j<sizeof(Mat4)/sizeof(F32); j++)
		{
			F32 *f = &((F32*)copy)[j];

			if(j<9)		// Rotation bits are more important, since they affect large objects heavily
				*f = roundFloatWithPrecision(*f, 0.0001);
			else
				*f = roundFloatWithPrecision(*f, 0.01);
		}

		sprintf(instline, "inst %d %x: %08x, %08x, %08x | %08x, %08x, %08x | %08x, %08x, %08x | %08x, %08x, %08x\n", 
							i, inst->matCRC,
							vecParamsXYZHex(copy[0]), vecParamsXYZHex(copy[1]),
							vecParamsXYZHex(copy[2]), vecParamsXYZHex(copy[3]));

		if(logfile)
		{
			filelog_printf(logfile, "%s", instline);
		}
		else
		{
			printf("%s", instline);
		}
	}
	sprintf(trivertline, "Tris: %x | Verts: %x\n", 
							freshCRC((U8*)info->smd->tris, 3*info->smd->tri_count*sizeof(info->smd->tris[0])),
							freshCRC((U8*)info->smd->verts, info->smd->vert_count*sizeof(info->smd->verts[0])));

	if(logfile)
	{
		filelog_printf(logfile, "%s", trivertline);
	}
	else
	{
		printf("%s", trivertline);
	}
}

void beaconObjectPrep(WorldCollObjectInfo *info)
{
	int i;

	if(info->prepped)
		return;

	for(i=0; i<eaSize(&info->instances); i++)
	{
		int j;
		WorldCollObjectInstance *inst = info->instances[i];

		copyMat4(inst->mat, inst->matRound);
		for(j=0; j<sizeof(Mat4)/sizeof(F32); j++)
		{
			F32 *f = &((F32*)inst->matRound)[j];

			if(j<9)		// Rotation bits are more important, since they affect large objects heavily
				*f = roundFloatWithPrecision(*f, 0.0001);
			else
				*f = roundFloatWithPrecision(*f, 0.01);
		}
	}

	info->prepped = true;
}

U32 beaconCRCObject(WorldCollObjectInfo *info, U32 rounded)
{
	int i;

	for(i=0; i<eaSize(&info->instances); i++)
	{
		WorldCollObjectInstance *inst = info->instances[i];

		inst->matCRC = freshCRC((U8*)inst->mat, sizeof(inst->mat));
		inst->matRoundCRC = freshCRC((U8*)inst->matRound, sizeof(inst->mat));
	}

	eaQSort(info->instances, beaconObjInstCrcCmp);

	cryptAdler32Init();
	for(i=0; i<eaSize(&info->instances); i++)
	{
		WorldCollObjectInstance *inst = info->instances[i];

		if(rounded)
			cryptAdler32Update((U8*)&inst->matRoundCRC, sizeof(U32));
		else
			cryptAdler32Update((U8*)&inst->matCRC, sizeof(U32));
	}
	if(info->smd->tri_count)
	{
		cryptAdler32Update((U8*)info->smd->tris, 3*info->smd->tri_count*sizeof(info->smd->tris[0]));
		cryptAdler32Update((U8*)info->smd->verts, info->smd->vert_count*sizeof(info->smd->verts[0]));
	}
	if(info->smd->map_size)
	{
		cryptAdler32Update((U8*)info->smd->heights, SQR(info->smd->map_size)*sizeof(F32));
		cryptAdler32Update((U8*)info->smd->holes, SQR(info->smd->map_size)*sizeof(bool));
	}
	
	return cryptAdler32Final();
}

#define BEACON_MAP_DATA_VERSION 2

void beaconMapDataPacketFromMapData(WorldColl *wc, BeaconMapDataPacket** mapDataIn, S32 fullCRCInfo){
	#define WRITE_CHECK(s) pktSendString(pak, s);
	
	BeaconMapDataPacket* mapData;
	NetLink* link = linkCreateFakeLink();
	Packet* pak = pktCreateTemp(link); 
	S32 i;
	
	S32 entityCollisionCount = 0;
	
	beaconMapDataPacketDestroy(mapDataIn);
	beaconMapDataPacketCreate(mapDataIn);
	mapData = *mapDataIn;
	
	ZeroStruct(&beacon_common.sent);
	
	// Send the MapDataPacket version

	pktSendBitsPack(pak, 1, BEACON_MAP_DATA_VERSION);

#if BEACON_MAP_DATA_VERSION >= 2
	pktSendBits(pak, 1, fullCRCInfo);
#endif

	// Tack on the initial beacon list.
	
	WRITE_CHECK("beacons");

	pktSendBitsPack(pak, 1, initial_beacons.count);
	
	for(i = 0; i < initial_beacons.count; i++){
		InitialBeacon* beacon = initial_beacons.beacons + i;
		
		pktSendF32(pak, beacon->pos[0]);
		pktSendF32(pak, beacon->pos[1]);
		pktSendF32(pak, beacon->pos[2]);
		
		pktSendBits(pak, 1, beacon->isValid);
	}	

	pktSendString(pak, beaconServerGetMapName());
	pktSendString(pak, beaconServerGetPatchTime());

	WRITE_CHECK("smds")

	{
		WorldCollObjectInfo **infos = *beaconGatherObjects(wc);

		pktSendBits(pak, 32, eaSize(&infos));

		for(i=0; i<eaSize(&infos); i++)
		{
			int j;
			WorldCollObjectInfo *info = infos[i];

			WRITE_CHECK("smd");

			if(fullCRCInfo)
				beaconPrintObject(info, beaconFileGetLogFile("mapdata"), false);

			info->crc = beaconCRCObject(info, false);
			pktSendBits(pak, 32, info->crc);

			pktSendBits(pak, 32, eaSize(&info->instances));
			for(j=0; j<eaSize(&info->instances); j++)
			{
				WorldCollObjectInstance *inst = info->instances[j];

				pktSendMat4Full(pak, inst->mat);
				pktSendBits(pak, 1, inst->noGroundConnections);
			}

			pktSendVec3(pak, info->smd->min);
			pktSendVec3(pak, info->smd->max);
			pktSendString(pak, info->smd->name);
#if BEACON_MAP_DATA_VERSION >= 2
			pktSendString(pak, info->smd->detail);
#endif
			pktSendBits(pak, 32, info->smd->tri_count);
			if(info->smd->tri_count)
			{
				for(j=0; j<info->smd->tri_count; j++)
				{
					pktSendBits(pak, 32, *(info->smd->tris+3*j+0));
					pktSendBits(pak, 32, *(info->smd->tris+3*j+1));
					pktSendBits(pak, 32, *(info->smd->tris+3*j+2));

					pktSendVec3(pak, info->smd->norms[j]);
				}

				pktSendBits(pak, 32, info->smd->vert_count);
				for(j=0; j<info->smd->vert_count; j++)
				{
					pktSendVec3(pak, info->smd->verts[j]);
				}
			}

			pktSendBits(pak, 32, info->smd->map_size);
			if(info->smd->map_size)
			{
				pktSendBits(pak, 32, info->smd->grid_size);

				pktSendBytes(pak, info->smd->map_size*info->smd->map_size*sizeof(F32), info->smd->heights);
				pktSendBytes(pak, info->smd->map_size*info->smd->map_size*sizeof(bool), info->smd->holes);
			}
		}

		wcStoredModelDataDestroyAll();
	}
	
	WRITE_CHECK("end");

	{
		U32 packetByteCount = pktGetSize(pak);
		U32 ret;
		U8 *stream = malloc(packetByteCount);
		
		mapData->compressed.byteCount = packetByteCount * 1.0125 + 12;

		SAFE_FREE(mapData->compressed.data);
		
		mapData->compressed.data = malloc(mapData->compressed.byteCount);

		pktReset(pak);

		pktGetBytes(pak, packetByteCount, stream);
					
		ret = compress2(mapData->compressed.data,
						&mapData->compressed.byteCount,
						stream,
						packetByteCount,
						5);
						
		//if(0){
		//	// Malform the packet to test beaconserver robustness.
		//
		//	int i;
		//	for(i = 0; i < 100; i++){
		//		mapData->compressed.data[rand() % mapData->compressed.byteCount] = rand() % 256;
		//	}
		//}
						
		assert(ret == Z_OK);

		mapData->uncompressedBitCount = packetByteCount*8;
		free(stream);
	}
	
	mapData->compressed.crc = freshCRC(mapData->compressed.data, mapData->compressed.byteCount);

	pktFree(&pak);
	free(link);
}

U32 beaconMapDataPacketGetCRC(BeaconMapDataPacket* mapData){
	if(!mapData){
		return 0;
	}
	
	return mapData->compressed.crc;
}

S32 beaconMapDataPacketIsSame(BeaconMapDataPacket* mapData1, BeaconMapDataPacket* mapData2){
	if((mapData1 ? 1 : 0) != (mapData2 ? 1 : 0)){
		return 0;
	}
	
	if(!mapData1){
		// And by implication, !mapData2.
		return 0;
	}
	
	if(	mapData1->compressed.byteCount != mapData2->compressed.byteCount ||
		mapData1->uncompressedBitCount != mapData2->uncompressedBitCount ||
		memcmp(mapData1->compressed.data, mapData2->compressed.data, mapData1->compressed.byteCount))
	{
		return 0;
	}
	
	return 1;
}

void beaconResetReceivedMapData()
{
	beaconResetMapData();
}

void beaconMapDataPacketSendChunk(Packet* pak, BeaconMapDataPacket* mapData, U32* sentByteCount){
	S32 byteCountRemaining = mapData->compressed.byteCount - *sentByteCount;
	S32 byteCountToSend = min(byteCountRemaining, 256 * 1024);
	
	assert(byteCountToSend >= 0);
	
	if(*sentByteCount == 0){
		// If this is the first packet, then send a single byte to
		//   start the send but let the server throttle the receive.
	
		byteCountToSend = min(1, byteCountToSend);
	}
	
	pktSendBitsPack(pak, 1, *sentByteCount);
	pktSendBitsPack(pak, 1, byteCountToSend);
	pktSendBitsPack(pak, 1, mapData->compressed.byteCount);
	pktSendBitsPack(pak, 1, mapData->uncompressedBitCount);
	pktSendBits(pak, 32, mapData->compressed.crc);
	
	pktSendBytes(	pak,
					byteCountToSend,
					mapData->compressed.data + *sentByteCount);
						
	*sentByteCount += byteCountToSend;
}

static PSDKCookedMesh* beaconCookMesh(WorldCollObjectInfo *info)
{
#if !PSDK_DISABLED
	char			buffer[200];
	PSDKMeshDesc	meshDesc = {0};
	Vec3			boxSize;
	PSDKCookedMesh  *mesh = NULL;
	PSDKHeightFieldDesc heightDesc = {0};

	if(!info->mesh)
	{
		strcpy(buffer, info->smd->name);

		info->radius = distance3(info->smd->min, info->smd->max)/2+0.001;

		// Now cook the stream

		if(info->smd->map_size)
		{
			setVec2same(heightDesc.worldSize, info->smd->grid_size);
			setVec2same(heightDesc.gridSize, info->smd->map_size);
			heightDesc.height = info->smd->heights;
			heightDesc.holes = info->smd->holes;
			psdkCookedHeightFieldCreate(&info->mesh, &heightDesc);
		}
		else
		{
			subVec3(info->smd->max, info->smd->min, boxSize);

			meshDesc.name = buffer;
			meshDesc.vertArray = info->smd->verts;
			meshDesc.vertCount = info->smd->vert_count;
			meshDesc.triArray = info->smd->tris;
			meshDesc.triCount = info->smd->tri_count;
			meshDesc.sphereRadius = info->radius;
			copyVec3(boxSize, meshDesc.boxSize);

			psdkCookedMeshCreate(&info->mesh, &meshDesc);
		}
	}

	return info->mesh;
#else
	return NULL;
#endif
}

int g_wco_count = 0;
int g_wco_max = 0;

static void beaconCollObjectMsgHandler(const WorldCollObjectMsg* msg)
{
	WorldCollObjectInstance *inst = msg->userPointer;
	WorldCollObjectInstance *other;

	if(!wcoGetUserPointer(msg->wco, beaconCollObjectMsgHandler, &other)){
		assert(0);
	}

	assert(inst->wco==msg->wco);
	assert(inst->wco==other->wco);

	switch(msg->msgType)
	{
		xcase WCO_MSG_GET_DEBUG_STRING:
		{
			snprintf_s(	msg->in.getDebugString.buffer,
						msg->in.getDebugString.bufferLen,
						"Inst: id: %08d smd: %s mat: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f ",
						inst->my_id, inst->info->smd->name, vecParamsXYZ(inst->mat[0]), vecParamsXYZ(inst->mat[1]), 
						vecParamsXYZ(inst->mat[2]), vecParamsXYZ(inst->mat[3]));
		}

		xcase WCO_MSG_DESTROYED:
		{
			WorldCollObjectInfo* info = inst->info;
			
			eaFindAndRemoveFast(&info->instances, inst);
			if(eaSize(&info->instances)==0)
			{
				eaFindAndRemoveFast(&beacon_infos, info);
				if(info->mesh)
				{
#if !PSDK_DISABLED
					psdkCookedMeshDestroy(&info->mesh);
#endif
				}
				if(info->smd)
				{
					wcStoredModelDataDestroy(&info->smd);
				}
				eaDestroy(&info->instances);
				free(info);
			}
			free(inst);
		}

		xcase WCO_MSG_GET_SHAPE:
		{
			WorldCollObjectMsgGetShapeOut*	getShape = msg->out.getShape;
			WorldCollObjectMsgGetShapeOutInst* shapeInst;

			if(!inst->info->mesh)
			{
				g_wco_count++;
				printf("Cooked %d/%d meshes\r", g_wco_count, g_wco_max);
			}

			wcoAddShapeInstance(getShape, &shapeInst);
			shapeInst->mesh = beaconCookMesh(inst->info);
			// Shapeinst defaults to unitmat and is relative to getshape mat
			//copyMat4(inst->mat, shapeInst->mat);  
			copyMat4(inst->mat, getShape->mat);
			shapeInst->filter.filterBits = WC_FILTER_BITS_WORLD_STANDARD;
			shapeInst->filter.shapeGroup = WC_SHAPEGROUP_WORLD_BASIC;

			assert(!inst->shapeGotten || inst->shapeGotten==other->my_id);
			inst->shapeGotten = other->my_id;
		}

		xcase WCO_MSG_GET_MODEL_DATA:
		{
			WorldCollModelInstanceData *instData = NULL;

			instData = callocStruct(WorldCollModelInstanceData);
			copyMat4(inst->mat, instData->world_mat);
			instData->noGroundConnections = inst->noGroundConnections;

			// Fill out result
			msg->out.getModelData->modelData = inst->info->smd;
			msg->out.getModelData->instData = instData;
		}
	}
}

S32 mat4IsEqual(Mat4 a, Mat4 b)
{
	int i, j;

	for(i=0; i<4; i++)
	{
		for(j=0; j<3; j++)
		{
			if(a[i][j]!=b[i][j])
			{
				return 0;
			}
		}
	}

	return 1;
}

S32 beaconObjInstCmp(const WorldCollObjectInstance **inst1, const WorldCollObjectInstance **inst2)
{
	return (*inst1)->my_id - (*inst2)->my_id;
}


void beaconCalcSMDMatMinMaxSlow(const WorldCollStoredModelData *smd, Mat4 world_mat, Vec3 minOut, Vec3 maxOut)
{
	// Just brute force it!
	int i;
	int once = 1;

	if(smd->tri_count)
	{
		for(i = 0; i < smd->tri_count; i++){
			S32*	tri = smd->tris + i * 3;
			Vec3 	verts_world[3];
			S32		j;

			for(j = 0; j < 3; j++){
				mulVecMat4(smd->verts[tri[j]], world_mat, verts_world[j]);

				if(once)
				{
					once = 0;
					copyVec3(verts_world[j], minOut);
					copyVec3(verts_world[j], maxOut);
				}
				else
				{
					MINVEC3(verts_world[j], minOut, minOut);
					MAXVEC3(verts_world[j], maxOut, maxOut);
				}
			}
		}
	}
	else if(smd->map_size)
	{
		mulBoundsAA(smd->min, smd->max, world_mat, minOut, maxOut);
	}
}

void beaconDestroySMDs(void)
{
	// Must manually destroy SMDs on client
	int i;

	if(!beaconIsClient())
	{
		return;
	}

	for(i=0; i<eaSize(&beacon_infos); i++)
	{
		WorldCollObjectInfo *info = beacon_infos[i];

		wcStoredModelDataDestroy(&info->smd);
	}
}

void beaconDestroyObjects(void)
{
	eaDestroyEx(&beacon_infos, destroyBeaconObjectInfo);
}

S32 beaconMapDataPacketToMapData(BeaconMapDataPacket* mapData, WorldColl **wcInOut){
	#define READ_CHECK(s) {																			\
		char *rs = pktGetStringTemp(pak);																\
		if(strcmp(rs, s)){																		\
			beaconPrintf(COLOR_RED, "ERROR: READ_CHECK failed: \"%s\" != \"%s\".\n", rs, s);	\
			free(data);																				\
			return 0;																				\
		}																							\
	}
	
	Packet* pak;
	S32 i;
	int count;
	S32 ret;
	S32 uncompressedByteCount;
	S32 mapDataPacketVersion;
	U32 crc;
	int instids = 0;
	U8 *data;
	char *mapname;
	U32 fullCRCInfo = 0;

	pak = pktCreateTemp(linkCreateFakeLink());
	
	// Check if the packet has been fully received.
	
	if(!beaconMapDataPacketIsFullyReceived(mapData)){
		beaconPrintf(	COLOR_RED,
						"ERROR: Map data packet isn't fully received (%s/%s bytes).\n",
						getCommaSeparatedInt(mapData ? mapData->receivedByteCount : 0),
						getCommaSeparatedInt(mapData ? mapData->compressed.byteCount : 0));
						
		return 0;
	}
	
	crc = freshCRC(mapData->compressed.data, mapData->compressed.byteCount);
	
	if(crc != mapData->compressed.crc){
		beaconPrintf(	COLOR_RED,
						"ERROR: Map data packet CRC doesn't match: 0x%8.8x != 0x%8.8x.\n",
						crc,
						mapData->compressed.crc);

		return 0;
	}

	wcStoredModelDataDestroyAll();
	
	beacon_common.mapDataLoadedFromPacket = 1;
	
	beaconPrintf(	COLOR_GREEN,
					"Decompressing map data (%s to %s bytes): %s\n",
					getCommaSeparatedInt(mapData->compressed.byteCount),
					getCommaSeparatedInt(mapData->uncompressedByteCount),
					beaconCurTimeString(0));
					
	data = malloc(mapData->uncompressedByteCount);
	uncompressedByteCount = mapData->uncompressedByteCount;
	mapData->uncompressedBitCount;

	ret = uncompress(	data,
						&uncompressedByteCount,
						mapData->compressed.data,
						mapData->compressed.byteCount);

	pktSetBuffer(pak, data, uncompressedByteCount);
						
	if(ret != Z_OK){
		beaconPrintf(COLOR_RED, "ERROR: Map data failed to decompress!\n");
		
		SAFE_FREE(data);
		
		return 0;
	}
	
	if(uncompressedByteCount != mapData->uncompressedByteCount){
		beaconPrintf(COLOR_RED, "ERROR: Map data decompressed to the wrong size!\n");
		
		SAFE_FREE(data);
		
		return 0;
	}

	// Get the MapDataPacket version.
	
	mapDataPacketVersion = pktGetBitsPack(pak, 1);

	// Make sure we understand the packet
	assert(mapDataPacketVersion<=BEACON_MAP_DATA_VERSION);

	if(mapDataPacketVersion>=2)
		fullCRCInfo = pktGetBits(pak, 1);
	
	// Get the initial beacons.
	
	READ_CHECK("beacons");
	
	beaconMapDataPacketClearInitialBeacons();
	
	initial_beacons.count = pktGetBitsPack(pak, 1);
	
	dynArrayFitStructs(	&initial_beacons.beacons,
						&initial_beacons.maxCount,
						initial_beacons.count);
				
	for(i = 0; i < initial_beacons.count; i++){
		InitialBeacon* beacon = initial_beacons.beacons + i;
		
		beacon->pos[0] = pktGetF32(pak);
		beacon->pos[1] = pktGetF32(pak);
		beacon->pos[2] = pktGetF32(pak);
		beacon->isValid = pktGetBits(pak, 1);
	}
	
#if !PSDK_DISABLED
	psdkDisableThreadedCooking();
#endif
	if(*wcInOut)
	{
		wcDestroy(wcInOut);
	}
	beaconDestroySMDs();
	beaconDestroyObjects();
	wcCreate(wcInOut);
	mapname = pktGetStringTemp(pak);
	beaconClientSetMapName(mapname);
	beaconClientSetPatchTime(pktGetStringTemp(pak));

	READ_CHECK("smds");
	{
		int numSmds = pktGetBits(pak, 32);

		for(i=0; i<numSmds; i++)
		{
			int j;
			WorldCollObjectInfo *info = NULL;
			int numInsts;

			READ_CHECK("smd");

			info = callocStruct(WorldCollObjectInfo);

			eaPush(&beacon_infos, info);

			info->crc = pktGetBits(pak, 32);

			numInsts = pktGetBits(pak, 32);
			for(j=0; j<numInsts; j++)
			{
				WorldCollObjectInstance *inst;

				inst = callocStruct(WorldCollObjectInstance);

				pktGetMat4Full(pak, inst->mat);
				if(mapDataPacketVersion)
					inst->noGroundConnections = pktGetBits(pak, 1);
				eaPush(&info->instances, inst);
				inst->my_index = j;
				inst->my_id = instids;
				inst->info = info;
				instids++;
			}

			info->smd = callocStruct(WorldCollStoredModelData);
			pktGetVec3(pak, info->smd->min);
			pktGetVec3(pak, info->smd->max);
			info->smd->name = strdup(pktGetStringTemp(pak));

			if(mapDataPacketVersion>=2)
				info->smd->detail = strdup(pktGetStringTemp(pak));

			info->smd->tri_count = pktGetBits(pak, 32);
			if(info->smd->tri_count)
			{
				info->smd->tris = calloc(3*info->smd->tri_count, sizeof(S32));
				info->smd->norms = calloc(info->smd->tri_count, sizeof(Vec3));

				for(j=0; j<info->smd->tri_count; j++)
				{
					*(info->smd->tris+3*j+0) = pktGetBits(pak, 32);
					*(info->smd->tris+3*j+1) = pktGetBits(pak, 32);
					*(info->smd->tris+3*j+2) = pktGetBits(pak, 32);

					pktGetVec3(pak, info->smd->norms[j]);
				}

				info->smd->vert_count = pktGetBits(pak, 32);
				info->smd->verts = calloc(info->smd->vert_count, sizeof(Vec3));
				for(j=0; j<info->smd->vert_count; j++)
				{
					pktGetVec3(pak, info->smd->verts[j]);
				}
			}

			info->smd->map_size = pktGetBits(pak, 32);
			if(info->smd->map_size)
			{
				info->smd->grid_size = pktGetBits(pak, 32);

				info->smd->heights = callocStructs(F32, info->smd->map_size*info->smd->map_size);
				pktGetBytes(pak, info->smd->map_size*info->smd->map_size*sizeof(F32), info->smd->heights);

				info->smd->holes = callocStructs(bool, info->smd->map_size*info->smd->map_size);
				pktGetBytes(pak, info->smd->map_size*info->smd->map_size*sizeof(bool), info->smd->holes);
			}

			{
				U32 temp = info->crc;
				if(fullCRCInfo)
					beaconPrintObject(info, beaconFileGetLogFile("mapdata"), false);
				assert(temp==beaconCRCObject(info, false));
			}

		}
	}

	READ_CHECK("end");

	//Create wcos
	count = 0;
	for(i=0; i<eaSize(&beacon_infos); i++)
	{
		int j;
		WorldCollObjectInfo *info = beacon_infos[i];

		for(j=0; j<eaSize(&info->instances); j++)
		{
			WorldCollObjectInstance *inst = info->instances[j];
			Vec3 newmin, newmax;
			int result;
			count++;

			beaconCalcSMDMatMinMaxSlow(info->smd, inst->mat, newmin, newmax);

			result = wcoCreate(	&inst->wco,
								*wcInOut,
								beaconCollObjectMsgHandler,
								inst,
								newmin,
								newmax,
								0,
								0);

			assert(result);
			assert(inst->wco);
			printf("WCO %d/%d inited\r", count, instids);
		}
	}
	printf("\n");

	g_wco_max = eaSize(&beacon_infos);
	g_wco_count = 0;
	wcCreateAllScenes(*wcInOut);
	printf("\n Map loaded \n");
	//wcSwapSimulation(wc, NULL, NULL, NULL, NULL, NULL);

	if(0)
	{
		for(i=0; i<eaSize(&beacon_infos); i++)
		{
			int j;
			WorldCollObjectInfo *info = beacon_infos[i];

			assert(info->mesh);

			for(j=0; j<eaSize(&info->instances); j++)
			{
				WorldCollObjectInstance *inst = info->instances[j];

				assert(inst->wco);
				assert(inst->shapeGotten==inst->my_id);
				//assert(wcoGetFirstActor(inst->wco, NULL));
			}
		}
	}

	if(0)
	{
		StashTable stash = stashTableCreateWithStringKeys(100, StashDefault);
		WorldCollObjectInfo **ea = NULL;
		ea = *beaconGatherObjects(*wcInOut);

		for(i=0; i<eaSize(&beacon_infos); i++)
		{
			assert(stashAddPointer(stash, beacon_infos[i]->smd->name, beacon_infos[i], 0));
		}

		for(i=0; i<eaSize(&ea); i++)
		{
			int j;
			WorldCollObjectInfo *found = NULL;
			WorldCollObjectInfo *info = ea[i];

			assert(stashFindPointer(stash, info->smd->name, &found));

			assert(found->smd==info->smd);
			assert(eaSize(&found->instances)==eaSize(&info->instances));

			eaQSort(found->instances, beaconObjInstCmp);
			eaQSort(info->instances, beaconObjInstCmp);
			
			for(j=0; j<eaSize(&found->instances); j++)
			{
				WorldCollObjectInstance *inst1 = found->instances[j];
				int matched = 0, k;

				for(k=0; k<eaSize(&info->instances); k++)
				{
					WorldCollObjectInstance *inst2 = info->instances[k];

					if(!inst2->matMatched && mat4IsEqual(inst2->mat, inst1->mat))
					{
						matched = 1;
						inst2->matMatched = 1;
					}
				}
				assert(matched);
			}
		}
	}
	
	//printf(	"Got map data (%s defs, %s refs).\n",
	//		getCommaSeparatedInt(groupDefs.count),
	//		getCommaSeparatedInt(group_info.ref_count));
			
	SAFE_FREE(data);

	return 1;
	
	#undef READ_CHECK
}

void beaconMapDataPacketReceiveChunkHeader(Packet* pak, BeaconMapDataPacket* mapData){
	mapData->header.previousSentByteCount = pktGetBitsPack(pak, 1);
	mapData->header.currentByteCount = pktGetBitsPack(pak, 1);

	mapData->compressed.byteCount = pktGetBitsPack(pak, 1);

	mapData->uncompressedBitCount = pktGetBitsPack(pak, 1);
	mapData->uncompressedByteCount = (mapData->uncompressedBitCount + 7) / 8;

	mapData->compressed.crc = pktGetBits(pak, 32);
}

S32 beaconMapDataPacketIsFirstChunk(BeaconMapDataPacket* mapData){
	return mapData ? !mapData->header.previousSentByteCount : 0;
}

void beaconMapDataPacketCopyHeader(BeaconMapDataPacket* to, const BeaconMapDataPacket* from){
	#define COPY_FIELD(x) to->x = from->x
	
	COPY_FIELD(header.previousSentByteCount);
	COPY_FIELD(header.currentByteCount);
	COPY_FIELD(compressed.byteCount);
	COPY_FIELD(uncompressedBitCount);
	COPY_FIELD(uncompressedByteCount);
	COPY_FIELD(compressed.crc);
	
	#undef COPY_FIELD
}

void beaconMapDataPacketReceiveChunkData(Packet* pak, BeaconMapDataPacket* mapData){
	if(!mapData->header.previousSentByteCount){
		SAFE_FREE(mapData->compressed.data);
		
		mapData->compressed.data = malloc(mapData->compressed.byteCount);
	}

	assert(	!mapData->header.previousSentByteCount ||
			mapData->receivedByteCount == mapData->header.previousSentByteCount);

	pktGetBytes(pak,
				mapData->header.currentByteCount,
				(U8*)mapData->compressed.data + mapData->header.previousSentByteCount);
					
	mapData->receivedByteCount = mapData->header.previousSentByteCount + mapData->header.currentByteCount;
}

void beaconMapDataPacketReceiveChunk(Packet* pak, BeaconMapDataPacket* mapData){
	beaconMapDataPacketReceiveChunkHeader(pak, mapData);
	beaconMapDataPacketReceiveChunkData(pak, mapData);
}

void beaconMapDataPacketSendChunkAck(Packet* pak, BeaconMapDataPacket* mapData){
	pktSendBitsPack(pak, 1, mapData->receivedByteCount);
}

S32 beaconMapDataPacketReceiveChunkAck(Packet* pak, U32 sentByteCount, U32* receivedByteCount){
	*receivedByteCount = pktGetBitsPack(pak, 1);
	
	return *receivedByteCount == sentByteCount;
}

S32 beaconMapDataPacketIsFullyReceived(BeaconMapDataPacket* mapData){
	return mapData ? mapData->compressed.byteCount && mapData->compressed.byteCount == mapData->receivedByteCount : 0;
}

S32 beaconMapDataPacketIsFullySent(BeaconMapDataPacket* mapData, U32 sentByteCount){
	return mapData->compressed.byteCount == sentByteCount;
}

U32 beaconMapDataPacketGetSize(BeaconMapDataPacket* mapData){
	return mapData ? mapData->compressed.byteCount : 0;
}

U8* beaconMapDataPacketGetData(BeaconMapDataPacket* mapData){
	return mapData ? mapData->compressed.data : NULL;
}

U32 beaconMapDataPacketGetReceivedSize(BeaconMapDataPacket* mapData){
	return mapData ? mapData->receivedByteCount : 0;
}

void beaconMapDataPacketDiscardData(BeaconMapDataPacket* mapData){
	if(!mapData){
		return;
	}
	
	SAFE_FREE(mapData->compressed.data);
	
	mapData->receivedByteCount = 0;
}

void beaconMapDataPacketWriteFile(	BeaconMapDataPacket* mapData,
									const char* fileName,
									const char* uniqueStorageName,
									U32 timeStamp)
{
	FILE* f = fopen(fileName, "wb");
	
	if(!f){
		return;
	}
	
	#define FWRITE_SIZE(x, size)	fwrite(x, size, 1, f)
	#define FWRITE(x)				FWRITE_SIZE(&x, sizeof(x))
	#define FWRITE_U32(x)			{U32 x_=x;FWRITE(x_);}

	// Write the file version.
	
	FWRITE_U32(0);
	
	// Write the CRC.
	
	FWRITE_U32(mapData->compressed.crc);
	
	// Write the unique storage name.
	
	FWRITE_U32((U32)strlen(uniqueStorageName));
	FWRITE_SIZE(uniqueStorageName, strlen(uniqueStorageName));
	
	// Write the timestamp.
	
	FWRITE_U32(timeStamp);
	
	// Write the compressed data size.
	
	FWRITE_U32(mapData->compressed.byteCount);
	
	// Write the uncompressed bit count.
	
	FWRITE_U32(mapData->uncompressedBitCount);

	// Write the data.
	
	FWRITE_SIZE(mapData->compressed.data, mapData->compressed.byteCount);
	
	// Close the file.
	
	fclose(f);

	// Done!

	#undef FWRITE_SIZE
	#undef FWRITE
	#undef FWRITE_U32
}

S32 beaconMapDataPacketReadFile(BeaconMapDataPacket* mapData,
								const char* fileName,
								char** uniqueStorageName,
								U32* timeStamp,
								S32 headerOnly)
{
	FILE* f = fopen(fileName, "rb");
	U32 fileVersion;
	U32 storageNameLength;
	char tempStorageName[1000];
	
	if(!f){
		return 0;
	}

	#define FAIL				{fclose(f);return 0;}
	#define CHECK(x)			if(!(x)){FAIL;}
	#define FREAD_SIZE(x, size)	CHECK(fread(x, size, 1, f))
	#define FREAD(x)			FREAD_SIZE(&x, sizeof(x))
	#define FREAD_U32(x)		{U32 x_;FREAD(x_);x = x_;}

	// Read the file version.
	
	FREAD_U32(fileVersion);

	CHECK(fileVersion <= 0);

	// Read the CRC.
	
	FREAD_U32(mapData->compressed.crc);
	
	// Read the unique storage name.
	
	FREAD_U32(storageNameLength);
	CHECK(storageNameLength < 1000);
	FREAD_SIZE(tempStorageName, storageNameLength);
	tempStorageName[storageNameLength] = 0;
	CHECK(strlen(tempStorageName) == storageNameLength);
	estrCopy2(uniqueStorageName, tempStorageName);
	
	// Read the timestamp.
	
	assert(timeStamp);
	FREAD_U32(*timeStamp);
	
	// Read the compressed data size.
	
	FREAD_U32(mapData->compressed.byteCount);
		
	// Read the uncompressed bit count.
	
	FREAD_U32(mapData->uncompressedBitCount);

	mapData->uncompressedByteCount = (mapData->uncompressedBitCount + 7) / 8;	
		
	// Verify that there is enough data left in the file.
		
	CHECK(mapData->compressed.byteCount <= (U32)(fileGetSize(f) - ftell(f)));

	// Read the data.
		
	if(!headerOnly){
		SAFE_FREE(mapData->compressed.data);
		
		mapData->compressed.data = malloc(mapData->compressed.byteCount);
		
		FREAD_SIZE(mapData->compressed.data, mapData->compressed.byteCount);

		mapData->receivedByteCount = mapData->compressed.byteCount;
	}else{
		mapData->receivedByteCount = 0;
	}
		
	// Close the file.
	
	fclose(f);

	// Done!

	#undef FREAD
	#undef FREAD_U32
	
	return 1;
}

static S32 cmdLineHasParams(S32 argc, char** argv, S32 cur, S32 count){
	S32 i;
	
	if(cur + count > argc){
		return 0;
	}
	
	for(i = cur; i < cur + count; i++){
		if(!argv[i][0] || argv[i][0] == '-'){
			return 0;
		}
	}
	
	return 1;
}

void beaconHandleCmdLine(S32 argc, char** argv){
	S32		i;

	// This function is only allowed to do things that don't do much of anything.

	for(i=1;i<argc;i++)
	{
		S32 handled = 1;
		S32 start_i = i;

		#define HANDLERS_BEGIN	if(0){
		#define HANDLER(x)		}else if(!stricmp(argv[i],x)){
		#define HANDLERS_END	}else{handled = 0;}
		#define HAS_PARAMS(x)	cmdLineHasParams(argc, argv, i + 1, x)
		#define HAS_PARAM		HAS_PARAMS(1)
		#define GET_NEXT_PARAM	(argv[++i])

		HANDLERS_BEGIN
			HANDLER("-beaconclient"){
				assert(!beaconizerInit.clientServerType);
				
				beaconizerInit.clientServerType = BEACONIZER_TYPE_CLIENT;
				
				// Get the master server name.
				
				if(HAS_PARAM){
					beaconizerInit.masterServerName = strdup(GET_NEXT_PARAM);
				}
			}
			HANDLER("-beaconclientsubserver"){
				// Get the subserver name.
				
				if(HAS_PARAM){
					beaconizerInit.subServerName = strdup(GET_NEXT_PARAM);
				}
			}
			HANDLER("-beaconserver"){
				assert(!beaconizerInit.clientServerType);
				
				beaconizerInit.clientServerType = BEACONIZER_TYPE_SERVER;

				// Get the master server name.
				
				if(HAS_PARAM){
					beaconizerInit.masterServerName = strdup(GET_NEXT_PARAM);
				}
			}
			HANDLER("-beaconautoserver"){
				assert(!beaconizerInit.clientServerType);
				
				beaconizerInit.clientServerType = BEACONIZER_TYPE_AUTO_SERVER;

				// Get the master server name.
				
				if(HAS_PARAM){
					beaconizerInit.masterServerName = strdup(GET_NEXT_PARAM);
				}
			}
			HANDLER("-beaconmasterserver"){
				assert(!beaconizerInit.clientServerType);
				
				beaconizerInit.clientServerType = BEACONIZER_TYPE_MASTER_SERVER;
			}
			HANDLER("-beaconrequestserver"){
				assert(!beaconizerInit.clientServerType);
				
				beaconizerInit.clientServerType = BEACONIZER_TYPE_REQUEST_SERVER;

				// Get the master server name.
				
				if(HAS_PARAM){
					beaconizerInit.masterServerName = strdup(GET_NEXT_PARAM);
				}
			}
			HANDLER("-beaconusemasterserver"){
				if(HAS_PARAM){
					beaconRequestSetMasterServerAddress(GET_NEXT_PARAM);
				}else{
					Errorf("No address specified for -beaconusemasterserver!");
				}
			}
			HANDLER("-beaconrequestcachedir"){
				if(HAS_PARAM){
					beaconServerSetRequestCacheDir(GET_NEXT_PARAM);
				}
			}
			HANDLER("-beaconproductionmode"){
				beacon_common.productionMode = 1;
			}
			HANDLER("-beaconallownovodex"){
				beacon_common.allowNovodex = 1;
			}
			HANDLER("-beaconsymstore"){
				beaconServerSetSymStore();
			}
			HANDLER("-beaconworkduringuseractivity"){
				beaconClientSetWorkDuringUserActivity(1);
			}
			HANDLER("-beaconnonetstart"){
				beaconizerInit.noNetStart = 1;
			}
			HANDLER("-beaconnogimme"){
				beaconServerSetGimmeUsage(0);
			}
			HANDLER("-beacondatatoolsrootpath"){
				if(HAS_PARAM){
					beaconServerSetDataToolsRootPath(GET_NEXT_PARAM);
				}
			}
			HANDLER("-beacon_galaxy_group_count")
			{
				if (HAS_PARAM)
				{
					beacon_galaxy_group_count = atoi_fast(GET_NEXT_PARAM);
					printfColor(COLOR_BLUE, "New Beacon Galaxy Group Count Value is: %d\n", beacon_galaxy_group_count);
				}
				else
				{
					printfColor(COLOR_BLUE, "New Beacon Galaxy Group Count Value not set\n");
				}
			}
		HANDLERS_END

		#undef HANDLERS_BEGIN
		#undef HANDLER
		#undef HANDLERS_END
		#undef HAS_PARAMS
		#undef HAS_PARAM
		#undef GET_NEXT_PARAM

		if(handled){
			// Invalidate handled parameters.

			while(start_i <= i){
				argv[start_i++][0] = 0;
			}
		}
	}
}

S32 beaconIsProductionMode(void){
	return beacon_common.productionMode || isProductionMode();
}

S32	beaconIsSharded(void){
	return beacon_common.isSharded;
}

void beaconGetCommonCmdLine(char* buffer, size_t bufferLen){
	if(beacon_common.productionMode){
		strcatf_s(buffer, bufferLen, " -beaconproductionmode");
	}
	
	if(beacon_common.allowNovodex){
		strcatf_s(buffer, bufferLen, " -beaconallownovodex");
	}
}

S32 beaconizerIsStarting()
{
	return beaconizerInit.clientServerType ? 1 : 0;
}

void beaconizerInitConsoleWindow(void){
	if(!beaconizerInit.clientServerType){
		return;
	}
	
	consoleUpSize(120, 9999);

	switch(beaconizerInit.clientServerType){
		xcase BEACONIZER_TYPE_CLIENT:
			setConsoleTitle("BeaconClient Starting...");
		xcase BEACONIZER_TYPE_SERVER:
			setConsoleTitle("Manual-BeaconServer Starting...");
		xcase BEACONIZER_TYPE_AUTO_SERVER:
			setConsoleTitle("Auto-BeaconServer Starting...");
		xcase BEACONIZER_TYPE_MASTER_SERVER:
			setConsoleTitle("Master-BeaconServer Starting...");
		xcase BEACONIZER_TYPE_REQUEST_SERVER:
			setConsoleTitle("Request-BeaconServer Starting...");
		xdefault:
			assert(0);
	}
}

void beaconErrorCallback(ErrorMessage *errMsg, void *userdata)
{
	// This reports to errortracker... how silly is that?
	char *errString = errorFormatErrorMessage(errMsg);
}

void beaconizerStartup(void)
{
	if(!beaconizerInit.clientServerType){
		return;
	}

	stringCacheFinalizeShared();
	ErrorfSetCallback(beaconErrorCallback, NULL);

#if NOVODEX
	if(	!beaconIsSharded() &&
		!beacon_common.allowNovodex)
	{
		FatalErrorf("Beaconizer must have NovodeX disabled!");
	}
#endif

	beaconizerInitConsoleWindow();

	switch(beaconizerInit.clientServerType){
		xcase	BEACONIZER_TYPE_CLIENT:{
			beaconClientStartup(beaconizerInit.masterServerName,
								beaconizerInit.subServerName);
		}

		xcase	BEACONIZER_TYPE_SERVER:
		case	BEACONIZER_TYPE_MASTER_SERVER:
		case	BEACONIZER_TYPE_AUTO_SERVER:
		case	BEACONIZER_TYPE_REQUEST_SERVER:	{
			beaconServerStartup(beaconizerInit.clientServerType,
								beaconizerInit.masterServerName,
								beaconizerInit.noNetStart,
								beaconizerInit.pseudoauto);
		}
	}
}

void beaconizerRun(void)
{
	switch(beaconizerInit.clientServerType){
		xcase	BEACONIZER_TYPE_CLIENT:{
			beaconClientOncePerFrame();
		}

		xcase	BEACONIZER_TYPE_SERVER:
		case	BEACONIZER_TYPE_MASTER_SERVER:
		case	BEACONIZER_TYPE_AUTO_SERVER:
		case	BEACONIZER_TYPE_REQUEST_SERVER:
		{
			beaconServerOncePerFrame();
		}
	}
}

void beaconSetMMMovementCallbacks(BeaconConnMovementStartCallback start,
								  BeaconConnMovementIsFinishedCallback isfinished,
								  BeaconConnMovementResultCallback result)
{
	beaconClientSetMMMovementCallbacks(start, isfinished, result);
}

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
						   PCLGetBranchFunc gbfunc)
{
	beacon_common.ccfunc = ccfunc;
	beacon_common.ddfunc = ddfunc;
	beacon_common.fffunc = fffunc;
	beacon_common.svfunc = svfunc;
	beacon_common.dvfunc = dvfunc;
	beacon_common.gafunc = gafunc;
	beacon_common.pfunc = pfunc;
	beacon_common.nrfunc = nrfunc;
	beacon_common.cvfunc = cvfunc;
	beacon_common.spfunc = spfunc;
	beacon_common.ufunc = ufunc;
	beacon_common.gbfunc = gbfunc;
}

void beaconSetGetServerListCallback(BeaconGetServerListFunc func)
{
	beacon_common.gslfunc = func;
}

void beaconSetGetServerNameCallback(BeaconGetServerNameFunc func)
{
	beacon_common.gsnfunc = func;
}

void beaconSetWCICallbacks(WCICreateFunc cfunc)
{
	beacon_common.wcicfunc = cfunc;
}

void beaconCommonChangeMaster(const char* newMaster, const char* ipStr)
{
	if(!beacon_common.connectedToMasterOnce || !beacon_common.masterServerName || !beacon_common.masterServerName[0])
	{
		printf("Master server is: ");  
		printfColor(COLOR_GREEN, "%s\n", newMaster);
		beacon_common.masterServerName = strdup(newMaster);

		if(beacon_common.onMasterChangeFunc)
			beacon_common.onMasterChangeFunc();
	}
	else if(stricmp(newMaster, beacon_common.masterServerName))
	{
		printf("Master server changed ");
		printfColor(COLOR_GREEN, "from %s to %s(%s)\n", beacon_common.masterServerName, newMaster, ipStr ? ipStr : "ByName");

		SAFE_FREE(beacon_common.masterServerName);
		beacon_common.masterServerName = strdup(newMaster);

		if(beacon_common.onMasterChangeFunc)
			beacon_common.onMasterChangeFunc();
	}
}

void beaconCommonGetMasterServer(U32 *ipList, void *userdata)
{
	beacon_common.waitingForTransReply = 0;
	if(!ea32Size(&ipList))
	{
		if(!beacon_common.connectedToMasterOnce)
			printfColor(COLOR_RED, "Failed to find beacon master in shard.  Falling back to localhost");
		beacon_common.masterServerName = strdup("localhost");
		return;
	}

	beaconCommonChangeMaster(makeHostNameStr(ipList[0]), makeIpStr(ipList[0]));
}

void beaconCommonGetMasterServerName(const char *name, void* userdata)
{
	beacon_common.waitingForTransReply = 0;

	if(!name || !name[0])
	{
		if(!beacon_common.connectedToMasterOnce)
			printfColor(COLOR_YELLOW, "Failed to find beacon server by name.  Falling back to any master server in shard.");
		beacon_common.gslfunc(GLOBALTYPE_BCNMASTERSERVER, beaconCommonGetMasterServer, NULL);
		beacon_common.waitingForTransReply = 1;
		return;
	}
	
	beaconCommonChangeMaster(name, NULL);
}

bool beaconCommonCheckMasterName(void)
{
	if(beaconIsSharded() && !beacon_common.waitingForTransReply && 
		ABS_TIME_SINCE(beacon_common.lastMasterCheck)>SEC_TO_ABS_TIME(10))
	{
		beacon_common.lastMasterCheck = ABS_TIME;
		beacon_common.gsnfunc(beaconCommonGetMasterServerName, NULL);
		beacon_common.waitingForTransReply = 1;
	}

	return beacon_common.connectedToMasterOnce;
}

BeaconConnection* beaconFindConnection(Beacon *b, Beacon *t, int raised)
{
	int i;

	if(raised==0 || raised<0)
	{
		for(i=0; i<b->gbConns.size; i++)
		{
			BeaconConnection *c = b->gbConns.storage[i];

			if(c->destBeacon==t)
				return c;
		}
	}

	if(raised==1 || raised<0)
	{
		for(i=0; i<b->rbConns.size; i++)
		{
			BeaconConnection *c = b->rbConns.storage[i];

			if(c->destBeacon==t)
				return c;
		}
	}

	return NULL;
}

WorldColl *beaconGetActiveWorldColl(int iPartitionIdx)
{
	WorldColl *wc = NULL;
	if(beaconIsClient())
	{
		wc = beaconClientGetWorldColl();
	}
	if(!wc)
	{
		if (worldIsValidPartitionIdx(iPartitionIdx))
			wc = worldGetActiveColl(iPartitionIdx);
		else
			wc = worldGetActiveColl(worldGetAnyCollPartitionIdx()); // TODO_PARTITION: Need to check this fall-back
	}
	assert(wc);
	return wc;
}

StashTable beaconGeoProximityStash = 0;
StashTable beaconGeoProximityStashProcess = 0;

void beaconLocalizeTraverseHelper(BeaconBlock* block, void* userdata)
{
	stashAddressAddInt(beaconGeoProximityStash, block, 1, 1);
}

//typedef void (*WorldCollObjectTraverseCB)(void* userPointer, const WorldCollObjectTraverseParams* params);
void beaconLocalizeTraverse(void* unused, const WorldCollObjectTraverseParams *params)
{
#if !PSDK_DISABLED
	if(params->wco)
	{
		PSDKActor *actor;
		Vec3 bMin = {0}, bMax = {0};
		int validObject = 0;
		WorldCollObjectInstance *inst = NULL;
		BeaconStatePartition *partition = beaconStatePartitionGet(0, true);
		Vec3 sceneOffset;

		if(wcoGetUserPointer(params->wco, entryCollObjectMsgHandler, NULL))
			validObject = true;
		else if(wcoGetUserPointer(params->wco, beaconCollObjectMsgHandler, &inst) && !inst->noGroundConnections)
			validObject = true;
		if(	validObject && 
			wcoGetActor(params->wco, &actor, sceneOffset) && 
			psdkActorGetBounds(actor, bMin, bMax))
		{
			subVec3(bMin, sceneOffset, bMin);
			subVec3(bMax, sceneOffset, bMax);
			beaconTraverseBlocks(partition, bMin, bMax, 100, beaconLocalizeTraverseHelper, NULL);
		}
	}
#endif
}

void beaconTestGeoProximity(int iPartitionIdx)
{
	if(!beaconGeoProximityStash)
		beaconGeoProximityStash = stashTableCreateAddress(20);

	stashTableClear(beaconGeoProximityStash);
	wcTraverseObjects(beaconGetActiveWorldColl(iPartitionIdx), beaconLocalizeTraverse, NULL, NULL, NULL, /*unique=*/1, WCO_TRAVERSE_STATIC);
}

//typedef void (*WorldCollObjectTraverseCB)(void* userPointer, const WorldCollObjectTraverseParams* params);
void beaconLocalizeProcessTraverse(void* unused, const WorldCollObjectTraverseParams *params)
{
#if !PSDK_DISABLED
	if(params->wco)
	{
		PSDKActor *actor;
		Vec3 bMin = {0}, bMax = {0};
		int validObject = 0;
		WorldCollObjectInstance *inst = NULL;
		Vec3 sceneOffset;

		if(wcoGetUserPointer(params->wco, entryCollObjectMsgHandler, NULL))
			validObject = true;
		else if(wcoGetUserPointer(params->wco, beaconCollObjectMsgHandler, &inst) && !inst->noGroundConnections)
			validObject = true;
		if(	validObject && 
			wcoGetActor(params->wco, &actor, sceneOffset) && 
			psdkActorGetBounds(actor, bMin, bMax))
		{
			int x, z;
			int minx, minz, maxx, maxz;

			subVec3(bMin, sceneOffset, bMin);
			subVec3(bMax, sceneOffset, bMax);

			minx = floor(bMin[0] / BEACON_GENERATE_CHUNK_SIZE); 
			MAX1(minx, bp_blocks.grid_min_xyz[0]);
			minz = floor(bMin[2] / BEACON_GENERATE_CHUNK_SIZE); 
			MAX1(minz, bp_blocks.grid_min_xyz[2]);
			maxx = floor(bMax[0] / BEACON_GENERATE_CHUNK_SIZE); 
			MIN1(maxx, bp_blocks.grid_max_xyz[0]);
			maxz = floor(bMax[2] / BEACON_GENERATE_CHUNK_SIZE);
			MIN1(maxz, bp_blocks.grid_max_xyz[2]);

			for(x = minx; x <= maxx; x++){
				for(z = minz; z <= maxz; z++){
					BeaconDiskSwapBlock *block = beaconGetDiskSwapBlock(x * BEACON_GENERATE_CHUNK_SIZE, z * BEACON_GENERATE_CHUNK_SIZE, 0);

					if(block)
					{
						stashAddressAddInt(beaconGeoProximityStashProcess, block, 1, 1);
					}
				}
			}
		}
	}
#endif
}

void beaconTestProcessGeoProximity(void)
{
	if(!beaconGeoProximityStashProcess)
		beaconGeoProximityStashProcess = stashTableCreateAddress(20);

	stashTableClear(beaconGeoProximityStashProcess);
	wcTraverseObjects(beaconGetActiveWorldColl(worldGetAnyCollPartitionIdx()), beaconLocalizeProcessTraverse, NULL, NULL, NULL, /*unique=*/1, WCO_TRAVERSE_STATIC);
}

// Tells the beacon client it's sharded, but doesn't give it ids and stuff
AUTO_COMMAND ACMD_HIDE ACMD_NAME(bcSharded);
void beaconSetSharded(int d)
{
	beacon_common.isSharded = !!d;
}

// Tells the beaconserver not to start/use patch services and run clients from src
AUTO_COMMAND ACMD_HIDE ACMD_NAME(beaconLocal);
void beaconUseLocalData(int d)
{
	switch(beaconizerInit.clientServerType)
	{
	case BEACONIZER_TYPE_NONE:{}
	xcase BEACONIZER_TYPE_CLIENT:{
		beaconClientUseLocalData(d);
	}
	default:
		break;
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_NAME(beaconLF) ACMD_CATEGORY(Beaconizer);
void beaconSetLoadFlags(int d)
{
	wlSetLoadFlags(WL_NO_LOAD_COSTUMES|WL_NO_LOAD_DYNFX|WL_NO_LOAD_DYNANIMATIONS);

	globCmdParsef("allowAllPrivateMaps 1");
	globCmdParsef("dontloadextern 1");
	globCmdParsef("bcnNoEncError 1");
	globCmdParsef("ignoreAllErrors 1");
	globCmdParsef("wcThreaded 0");
	globCmdParsef("EnglishOnly");
}

#endif

char ***beaconGetInvalidSpawns(void)
{
#if !PLATFORM_CONSOLE
	return &invalidEncounterArray;
#else
	return NULL;
#endif
}

U32 beaconIsBeaconizer(void)
{
#if !PLATFORM_CONSOLE
	return beaconizerInit.clientServerType != BEACONIZER_TYPE_NONE;
#else
	return 0;
#endif
}

U32 beaconIsMasterServer(void)
{
#if !PLATFORM_CONSOLE
	return beaconizerInit.clientServerType == BEACONIZER_TYPE_MASTER_SERVER;
#else
	return 0;
#endif
}

U32 beaconIsRequestServer(void)
{
#if !PLATFORM_CONSOLE
	return beaconizerInit.clientServerType == BEACONIZER_TYPE_REQUEST_SERVER;
#else
	return 0;
#endif
}
