#include "beaconClient.h"

#include "beaconClientServerPrivate.h"
//#include "comm_backend.h"
#include "winutil.h"
#include <time.h>
#include "structNet.h"
#include "net/netpacketutil.h"
#include "WorldGrid.h"
#include "SharedMemory.h"
#include "cpu_count.h"
#include "sock.h"
#include "ControllerLink.h"
#include "cmdparse.h"
#include "PhysicsSDK.h"
#include "process_util.h"
#include "StringCache.h"
#include "beaconClient_h_ast.h"
#include "beaconClientServerPrivate_h_ast.h"
#include "utilitiesLib.h"
#include "logging.h"
#include "UTF8.h"
#include "timing_profiler_interface.h"

#include "wlBeacon_h_ast.h"

static char* beaconClientExeName = "BeaconClient.exe";
NetLink* beacon_debug_link;

static S32 hideClient = 1;

typedef struct WorldColl WorldColl;

#if !PLATFORM_CONSOLE

typedef struct BeaconSentryClientData {
	HANDLE	hPipe;
	U32		myServerID;
	char*	serverUID;
	U32		lastSentTime;
	HANDLE	processHandle;
	PROCESS_INFORMATION pi;
	
	char*	crashText;
	U32		forcedInactive	: 1;
	U32		usedInactive	: 1;
} BeaconSentryClientData;

char* beaconClientMyArgs(void);

#endif

BeaconClient beacon_client;

#if !PLATFORM_CONSOLE

AUTO_COMMAND ACMD_NAME(beaconworkduringuseractivity) ACMD_SERVERONLY ACMD_HIDE;
void beaconInitWorkDuringUserActivity(int on)
{
	beacon_client.workDuringUserActivity = !!on;
}

S32 beaconClientIsSentry(void){
	return beacon_client.hMutexSentry ? 1 : 0;
}

NetComm *beacon_comm;

static void beaconClientUpdateTitle(const char* format, ...){
	char buffer[1000];
	char clientTitle[100];
	
	if(beaconClientIsSentry()){
		S32 clientCount = eaSize(&beacon_client.sentryClients);
		STR_COMBINE_BEGIN(clientTitle);
		STR_COMBINE_CAT("Sentry-BeaconClient(");
		STR_COMBINE_CAT_D(clientCount);
		STR_COMBINE_CAT(" client");
		if(clientCount != 1){
			STR_COMBINE_CAT("s");
		}
		STR_COMBINE_CAT(")");
		STR_COMBINE_END(clientTitle);
	}else{
		STR_COMBINE_BEGIN(clientTitle);
		STR_COMBINE_CAT("Worker-BeaconClient");
		if(!beacon_client.hPipeToSentry){
			STR_COMBINE_CAT("(No Sentry)");
		}
		STR_COMBINE_END(clientTitle);
	}

	STR_COMBINE_BEGIN(buffer);
	STR_COMBINE_CAT(clientTitle);
	STR_COMBINE_CAT(":");
	STR_COMBINE_CAT_D(_getpid());
	STR_COMBINE_CAT(" ");
	if(beaconClientIsSentry()){
		if(!beacon_client.userInactive){
			STR_COMBINE_CAT("(User Is Active) ");
		}
		else if(beacon_client.workDuringUserActivity){
			STR_COMBINE_CAT("(Work During User Activity) ");
		}
	}
	if(linkConnected(beacon_client.masterLink)){
		STR_COMBINE_CAT("Connected!");
	}else{
		STR_COMBINE_CAT("Not Connected!");
	}
	STR_COMBINE_END(clientTitle);
	
	if(format){
		char* pos = buffer + strlen(buffer);
		
		strcpy_s(pos, buffer + sizeof(buffer) - pos, " (");
		pos += 2;
		
		VA_START(argptr, format);
		pos += vsprintf_s(pos, buffer + sizeof(buffer) - pos, format, argptr);
		VA_END();
		
		strcpy_s(pos, buffer + sizeof(buffer) - pos, ")");
		pos++;
	}

	setConsoleTitle(buffer);
}

Packet*	beaconClientCreatePacket(NetLink* link, const char* textCmd){
	if(link)
	{
		Packet* pak = pktCreate(link, BMSG_C2S_TEXT_CMD);
		pktSendString(pak, textCmd);
		return pak;
	}
	else
	{
		return NULL;
	}
}

void beaconClientSendPacket(NetLink* link, Packet** pak){
	pktSend(pak);
}

static void sendReadyMessage(NetLink *link){
	beaconPrintf(COLOR_GREEN, "Sending ready message!\n");
	
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(link, BMSG_C2ST_READY_TO_WORK);
	BEACON_CLIENT_PACKET_SEND();
	
	beacon_client_conn.readyToWork = 1;
	beacon_client.userInactive = 0;
}

static void sendMapDataIsLoaded(void){
	BEACON_CLIENT_PACKET_CREATE(BMSG_C2ST_MAP_DATA_IS_LOADED);
		pktSendBits(pak, 32, beacon_client.mapDataCRC);
	BEACON_CLIENT_PACKET_SEND();
}

static void beaconClientReceiveLegalAreas(Packet* pak, BeaconDiskSwapBlock* block){
	S32 i;
	S32 count;
	
	if(pktGetBits(pak, 1) == 1){
		U32 surfaceCRC = pktGetBits(pak, 32);
		
		if(block->foundCRC){
			assert(block->surfaceCRC == surfaceCRC);
			block->verifiedCRC = 1;
		}else{
			block->foundCRC = 1;
			block->surfaceCRC = surfaceCRC;
		}
	}	
	
	while(block->legalCompressed.areasHead){
		BeaconLegalAreaCompressed* next = block->legalCompressed.areasHead->next;
		destroyBeaconLegalAreaCompressed(block->legalCompressed.areasHead);
		block->legalCompressed.areasHead = next;
		block->legalCompressed.totalCount--;
	}
	
	assert(!block->legalCompressed.totalCount);
	
	count = pktGetBitsPack(pak, 16);
	
	beaconLegalAreaCompressedResetBuffer();
	for(i = 0; i < count; i++){
		BeaconLegalAreaCompressed* area = beaconAddLegalAreaCompressed(block);
		
		area->x = pktGetBits(pak, 8);
		area->z = pktGetBits(pak, 8);
		area->checked = pktGetBits(pak, 1);
		
		if(pktGetBits(pak, 1)){
			area->isIndex = 1;
			area->y_index = pktGetBitsPack(pak, 5);
		}else{
			area->isIndex = 0;
			area->y_coord = pktGetF32(pak);
		}
		
		beaconReceiveColumnAreas(pak, area);
		
		#if BEACONGEN_STORE_AREA_CREATOR
			area->areas.cx = pktGetBitsPack(pak, 1);
			area->areas.cz = pktGetBitsPack(pak, 1);
			area->areas.ip = pktGetBits(pak, 32);
		#endif
	}
	
	assert(block->legalCompressed.totalCount == count);

	beaconCheckDuplicates(block);
}

static void beaconClientPrintColumnAreaError(	BeaconLegalAreaCompressed* legalArea,
												BeaconGenerateColumn* column,
												BeaconDiskSwapBlock* block)
{
	BeaconGenerateColumnArea* curArea;
	
	printf(	"\n\nMismatched area count (me: %d, received: %d) at block (%d,%d)... point (%d,%d)\n\n",
			column->areaCount,
			legalArea->areas.count,
			block->x / BEACON_GENERATE_CHUNK_SIZE,
			block->z / BEACON_GENERATE_CHUNK_SIZE,
			block->x + legalArea->x,
			block->z + legalArea->z);
	
	#if BEACONGEN_STORE_AREAS
	{
		S32 i;
		
		#if BEACONGEN_STORE_AREA_CREATOR
			printf("Legal area areas (%d,%d from %s):\n", legalArea->areas.cx, legalArea->areas.cz, makeIpStr(legalArea->areas.ip));
		#endif

		for(i = 0; i < legalArea->areas.count; i++){
			printf("  area: %6.8f - %6.8f\n", legalArea->areas.areas[i].y_min, legalArea->areas.areas[i].y_max);

			#if BEACONGEN_CHECK_VERTS
			{
				S32 j;
			
				printf("    def: %s\n", legalArea->areas.areas[i].defName);

				for(j = 0; j < 3; j++){
					printf("      (%1.8f, %1.8f, %1.8f)\n", vecParamsXYZ(legalArea->areas.areas[i].triVerts[j]));
				}
			}
			#endif
		}
	}
	#endif
		
	printf("\n\nColumn area areas:\n");

	for(curArea = column->areas; curArea; curArea = curArea->nextColumnArea){
		printf("  area: %6.8f - %6.8f\n", curArea->y_min, curArea->y_max);
		
		#if BEACONGEN_CHECK_VERTS
		{
			S32 i;
			WorldCollStoredModelData *smd = curArea->triVerts.smd;
		
			printf("    smd: %s\n", smd->name);
			
			for(i = 0; i < 3; i++){
				printf("      (%1.8f, %1.8f, %1.8f)\n", vecParamsXYZ(curArea->triVerts.verts[i]));
			}
		}
		#endif
	}
				
	#if BEACONGEN_CHECK_VERTS
	{
		S32 i;
		
		printf("\n\nLegal area tris:\n");
		
		for(i = 0; i < legalArea->tris.count; i++){
			S32 j;
			
			printf("  tri: %6.8f - %6.8f\n", legalArea->tris.tris[i].y_min, legalArea->tris.tris[i].y_max);
			
			printf("    def: %s\n", legalArea->tris.tris[i].defName);
			
			for(j = 0; j < 3; j++){
				printf("      (%1.8f, %1.8f, %1.8f)\n", vecParamsXYZ(legalArea->tris.tris[i].verts[j]));
			}
		}
		
		printf("\n\nColumn tris:\n");

		for(i = 0; i < column->tris.count; i++){
			S32 j;
			
			printf("  tri: %6.8f - %6.8f\n", column->tris.tris[i].y_min, column->tris.tris[i].y_max);
			
			printf("    def: %s\n", column->tris.tris[i].smd->name);
			
			for(j = 0; j < 3; j++){
				printf("      (%1.8f, %1.8f, %1.8f)\n", vecParamsXYZ(column->tris.tris[i].verts[j]));
			}
		}
	}
	#endif

	printf("\n\n");
				
	assert(0);
}

static void beaconClientApplyCompressedLegalAreas(BeaconDiskSwapBlock* block){
	BeaconLegalAreaCompressed* legalArea;
	
	for(legalArea = block->legalCompressed.areasHead; legalArea; legalArea = legalArea->next){
		BeaconGenerateColumn* column = block->chunk->columns + BEACON_GEN_COLUMN_INDEX(legalArea->x, legalArea->z);
		BeaconGenerateColumnArea* area;

		if(legalArea->areas.count && legalArea->areas.count != column->areaCount){
			beaconClientPrintColumnAreaError(legalArea, column, block);
		}
		
		if(legalArea->isIndex){
			S32 j = legalArea->y_index;
			
			assert(j >= 0 && j < column->areaCount);
			
			area = column->areas;
			
			while(j--){
				area = area->nextColumnArea;
			}
			
			assert(area);
		}else{
			area = beaconGetColumnAreaFromYPos(column, legalArea->y_coord);
		}

		if(area){
			area->inCompressedLegalList = 1;
			
			beaconMakeLegalColumnArea(column, area, legalArea->checked);
		}
	}
}

static void beaconClientCheckWindow(void){
	if(!IsWindowVisible(beaconGetConsoleWindow())){
		#define KEY_IS_DOWN(key) (0x8000 & GetKeyState(key) ? 1 : 0)
		S32 lShift		= KEY_IS_DOWN(VK_LSHIFT);
		S32 rShift		= KEY_IS_DOWN(VK_RSHIFT);
		S32 lControl	= KEY_IS_DOWN(VK_LCONTROL);
		S32 rControl	= KEY_IS_DOWN(VK_RCONTROL);
		S32 lAlt		= KEY_IS_DOWN(VK_LMENU);
		S32 rAlt		= KEY_IS_DOWN(VK_RMENU);
		#undef KEY_IS_DOWN
		
		if(	(lShift || rShift) &&
			(lControl || rControl) &&
			(lAlt || rAlt))
		{
			RECT rectDesktop;
			POINT pt;

			if(	GetWindowRect(GetDesktopWindow(), &rectDesktop) &&
				GetCursorPos(&pt) &&
				SQR(pt.x - rectDesktop.left) + SQR(pt.y - rectDesktop.top) < SQR(10))
			{
				ShowWindow(beaconGetConsoleWindow(), SW_RESTORE);
				
				SetForegroundWindow(beaconGetConsoleWindow());
			}
		}
	}
	else if(!beaconIsProductionMode()){
		WINDOWPLACEMENT wp = {0};
		
		wp.length = sizeof(wp);
		GetWindowPlacement(beaconGetConsoleWindow(), &wp);
		
		switch(wp.showCmd){
			xcase SW_SHOWMINIMIZED:
			case SW_MINIMIZE:{
				if(hideClient){
					ShowWindow(beaconGetConsoleWindow(), SW_HIDE);
				}
			}
		}
	}
}

static void beaconClientWindowThread(void* unused){
	U32 ticksPerSecond = timerCpuSpeed();
	U32 fastCheckTime = 4 * ticksPerSecond;
	U16 oldKeyStates[256];
	U16 keyStates[256];
	S32 i;
	
	for(i = 0; i < 256; i++){
		oldKeyStates[i] = GetKeyState(i);
	}
	
	while(1){
		beaconClientCheckWindow();
		
		if(!beaconClientIsSentry() && (beacon_client.timeSinceUserActivity > fastCheckTime || beacon_client.workDuringUserActivity)){
			//printf("time: %d\n", beacon_client.timeUntilAwake);
			
			for(i = 0; i < 256; i++){
				keyStates[i] = GetKeyState(i);
			}
			
			if(memcmp(keyStates, oldKeyStates, sizeof(keyStates))){
				//printf("Keyboard pressed.\n");
				beacon_client.keyboardPressed = 1;
			}else{
				//printf("No key changes.\n");
			}
			
			memcpy(oldKeyStates, keyStates, sizeof(keyStates));
			
			Sleep(10);
		}else{
			//printf("time: %1.2f\n", timerSeconds(beacon_client.timeUntilAwake));
			Sleep(500);
		}
	}
}

static void beaconProcessLegalAreas(int iPartitionIdx, BeaconDiskSwapBlock* block){
	S32 grid_x = block->x / BEACON_GENERATE_CHUNK_SIZE;
	S32 grid_z = block->z / BEACON_GENERATE_CHUNK_SIZE;
	
	beaconClearNonAdjacentSwapBlocks(NULL);

	printf("Extract");
	
	assert(beacon_client.world_coll);
	beaconExtractSurfaces(beacon_client.world_coll, grid_x - 1, grid_z - 1, 3, 3, 1);
	
	assert(block->chunk);
	
	//printf(", Legalize(%4d)", block->legalCompressed.totalCount);
	
	//assert(!(grid_x == -18 && grid_z == -4));

	beaconClientApplyCompressedLegalAreas(block);
	
	//assert(block->isLegal);
	
	printf(", Surface");
	
	beaconPropagateLegalColumnAreas(block);
	
	printf(", Generate");
	
	beaconMakeBeaconsInChunk(iPartitionIdx, block->chunk, 1);
	
	printf("(%d)", bp_blocks.generatedBeacons.count);
}

static void beaconClientSendLegalAreas(S32 center_grid_x, S32 center_grid_z){
	BEACON_CLIENT_PACKET_CREATE(BMSG_C2ST_GENERATE_FINISHED)

		BeaconDiskSwapBlock* cur;
		
		for(cur = bp_blocks.list; cur; cur = cur->nextSwapBlock){
			if(cur->addedLegal){
				BeaconLegalAreaCompressed* area;
				S32 grid_x = cur->x / BEACON_GENERATE_CHUNK_SIZE;
				S32 grid_z = cur->z / BEACON_GENERATE_CHUNK_SIZE;
				
				beaconCheckDuplicates(cur);

				pktSendBits(pak, 1, 1);
				
				pktSendBitsPack(pak, 1, grid_x);
				pktSendBitsPack(pak, 1, grid_z);
				
				if(grid_x == center_grid_x && grid_z == center_grid_z){
					S32 i;
					
					// Send stuff related to the block this client was assigned to process.
					
					assert(cur->foundCRC);
					pktSendBits(pak, 32, cur->surfaceCRC);
					
					// Send the beacons.
					
					pktSendBitsPack(pak, 1, bp_blocks.generatedBeacons.count);
					
					for(i = 0; i < bp_blocks.generatedBeacons.count; i++){
						pktSendVec3(pak, bp_blocks.generatedBeacons.beacons[i].pos);
						pktSendBits(pak, 1, bp_blocks.generatedBeacons.beacons[i].noGroundConnections);
					}
				}
							
				pktSendBitsPack(pak, 5, cur->legalCompressed.totalCount);
				
				for(area = cur->legalCompressed.areasHead; area; area = cur->legalCompressed.areasHead){
					S32 columnIndex = BEACON_GEN_COLUMN_INDEX(area->x, area->z);
					BeaconGenerateColumn* column = cur->chunk->columns + columnIndex;
					
					pktSendBits(pak, 8, area->x);
					pktSendBits(pak, 8, area->z);
					
					if(area->isIndex){
						pktSendBits(pak, 1, 1);
						pktSendBitsPack(pak, 5, area->y_index);

						assert(	column->isIndexed &&
								area->y_index >= 0 &&
								(S32)area->y_index < column->areaCount);
					}else{
						pktSendBits(pak, 1, 0);
						pktSendF32(pak, area->y_coord);
					}
					
					pktSendBitsPack(pak, 1, column->areaCount);

					#if BEACONGEN_STORE_AREAS
					{
						BeaconGenerateColumnArea* columnArea;
						
						for(columnArea = column->areas;
							columnArea;
							columnArea = columnArea->nextColumnArea)
						{
							pktSendF32(pak, columnArea->y_min);
							pktSendF32(pak, columnArea->y_max);
							
							#if BEACONGEN_CHECK_VERTS
							{
								S32 i;
								
								for(i = 0; i < 3; i++){
									pktSendF32(pak, columnArea->triVerts.verts[i][0]);
									pktSendF32(pak, columnArea->triVerts.verts[i][1]);
									pktSendF32(pak, columnArea->triVerts.verts[i][2]);
								}
								
								assert(columnArea->triVerts.smd);
								
								pktSendString(pak, columnArea->triVerts.smd->name);
							}
							#endif
						}
					}
					#endif
					
					#if BEACONGEN_CHECK_VERTS
					{
						S32 i;
						
						for(i = 0; i < column->tris.count; i++){
							S32 j;
							pktSendBits(pak, 1, 1);
							pktSendString(pak, column->tris.tris[i].smd->name);
							pktSendF32(pak, column->tris.tris[i].y_min);
							pktSendF32(pak, column->tris.tris[i].y_max);
							for(j = 0; j < 3; j++){
								pktSendF32(pak, column->tris.tris[i].verts[j][0]);
								pktSendF32(pak, column->tris.tris[i].verts[j][1]);
								pktSendF32(pak, column->tris.tris[i].verts[j][2]);
							}
						}

						pktSendBits(pak, 1, 0);
					}
					#endif

					cur->legalCompressed.areasHead = area->next;
					destroyBeaconLegalAreaCompressed(area);
					cur->legalCompressed.totalCount--;
				}
			}
			
			assert(!cur->legalCompressed.totalCount);
		}
		
		// Send terminator bit.

		pktSendBits(pak, 1, 0);
		
	BEACON_CLIENT_PACKET_SEND();
}

static void beaconClientSendNeedMoreMapData(void){
	BEACON_CLIENT_PACKET_CREATE(BMSG_C2ST_NEED_MORE_MAP_DATA);
		
		beaconMapDataPacketSendChunkAck(pak, beacon_client.mapData);
		
	BEACON_CLIENT_PACKET_SEND();
}

static void beaconClientReceiveMapData(Packet* pak){
	S32 doNotCalculateCRC = pktGetBits(pak, 1);

	if(!beacon_client.mapData){
		beaconMapDataPacketCreate(&beacon_client.mapData);
	}
	
	beaconMapDataPacketReceiveChunk(pak, beacon_client.mapData);

	beaconPrintf(	COLOR_GREEN,
					"Received map data: %s/%s bytes!\n",
					getCommaSeparatedInt(beaconMapDataPacketGetReceivedSize(beacon_client.mapData)),
					getCommaSeparatedInt(beaconMapDataPacketGetSize(beacon_client.mapData)));

	beaconResetReceivedMapData();
	
	if(beaconMapDataPacketIsFullyReceived(beacon_client.mapData)){
		if(beaconMapDataPacketToMapData(beacon_client.mapData, &beacon_client.world_coll)){			
			beacon_client.mapDataCRC = beaconCalculateGeoCRC(beaconGetWorldColl(NULL), false);
			
			beaconCurTimeString(1);
			
			printf("Map CRC: 0x%8.8x\n", beacon_client.mapDataCRC);

			sendMapDataIsLoaded();
			
			beaconInitGenerating(beacon_client.world_coll, 0);

			printf("Done: %s\n", beaconCurTimeString(0));
		}
		else if(linkConnected(beacon_client.serverLink)){
			linkRemove(&beacon_client.serverLink);
		}
	}else{
		beaconClientSendNeedMoreMapData();
	}
}

static void beaconClientConnectBeacon(Packet* pak, S32 index, S64 *groundTicks, S64 *raisedTicks){
	Beacon* b;
	BeaconProcessInfo* info;
	S32 j;
	S32 groundCount = 0;
	S32 raisedCount = 0;
	
	assert(index >= 0 && index < combatBeaconArray.size);
	
	b = combatBeaconArray.storage[index];
	info = beacon_process.infoArray + index;

	beaconProcessCombatBeacon(BEACONCLIENT_PARTITION, b, groundTicks, raisedTicks);
	
	pktSendBitsPack(pak, 1, index);
	pktSendBitsPack(pak, 1, info->beaconCount);
	
	for(j = 0; j < info->beaconCount; j++){
		S32 k;
		
		pktSendBitsPack(pak, 1, info->beacons[j].targetIndex);
		pktSendBits(pak, 1, info->beacons[j].reachedByGround);
		if(info->beacons[j].reachedByGround)
		{
			pktSendBitsPack(pak, 1, info->beacons[j].optionalWalkCheck);
			pktSendBitsPack(pak, 1, info->beacons[j].bidirWalkCheck);
		}
		pktSendBitsPack(pak, 1, info->beacons[j].raisedCount);
		
		for(k = 0; k < info->beacons[j].raisedCount; k++){
			pktSendF32(pak, info->beacons[j].raisedConns[k].minHeight - vecY(b->pos));
			pktSendF32(pak, info->beacons[j].raisedConns[k].maxHeight - vecY(b->pos));
		}
	
		groundCount += info->beacons[j].reachedByGround;
		raisedCount += info->beacons[j].raisedCount;
		
		if(info->beacons[j].raisedCount){
			SAFE_FREE(info->beacons[j].raisedConns);
		}
	}
	
	info->beaconCount = 0;
	SAFE_FREE(info->beacons);
}

static void beaconClientSetPriorityLevel(S32 level){
	S32 priorityClass = NORMAL_PRIORITY_CLASS;
	S32 threadPriority = THREAD_PRIORITY_NORMAL;

	switch(level){
		xcase 0:{
			priorityClass = IDLE_PRIORITY_CLASS;
			threadPriority = THREAD_PRIORITY_IDLE;
		}
	}

	SetPriorityClass(GetCurrentProcess(), priorityClass);
	SetThreadPriority(GetCurrentThread(), threadPriority);
}

static S32 beaconClientAdvancePipeline(void)
{
	beacon_client.connect.group.lo = -1;
	beacon_client.connect.group.hi = -1;
	ea32ClearFast(&beacon_client.connect.group.indices);

	if(ea32Size(&beacon_client.connect.pipeline.indices))
	{
		beacon_client.connect.group.lo = beacon_client.connect.pipeline.lo;
		beacon_client.connect.group.hi = beacon_client.connect.pipeline.hi;
		ea32Copy(&beacon_client.connect.group.indices, &beacon_client.connect.pipeline.indices);
		ea32ClearFast(&beacon_client.connect.pipeline.indices);

		return true;
	}

	return false;
}

static void beaconClientConnectBeacons(void)
{
	S64 ground = 0, raised = 0;
	if(!ea32Size(&beacon_client.connect.group.indices))
		return;

	BEACON_CLIENT_PACKET_CREATE(BMSG_C2ST_BEACON_CONNECTIONS)
		S32 i;
		S32 conns;
		S32 count = ea32Size(&beacon_client.connect.group.indices);

		beaconClientSetPriorityLevel(0);

		pktSendBitsPack(pak, 1, beacon_client.connect.group.lo);
		pktSendBitsPack(pak, 1, beacon_client.connect.group.hi);

		printf("Connecting %3d [%6d-%6d]: ", count, beacon_client.connect.group.lo, beacon_client.connect.group.hi);

		for(i = 0; i < count; i++){
			S32 index = beacon_client.connect.group.indices[i];

			beaconClientConnectBeacon(pak, index, &ground, &raised);

			printf(".");
		}

		conns = beaconConnectionGetNumWalksProcessed();
		printf(" : Conns: %05d [", conns);

		if (conns)
		{
			for(i=0; i<BEACON_WALK_RESULTS; i++)
			{
				F32 res = beaconConnectionGetNumResults(i);
				int color = (i==BEACON_WALK_SUCCESS ? COLOR_GREEN : COLOR_RED);

				if(i==BEACON_WALK_RESULTS-1)
				{
					beaconPrintf(color, "%0.5f", res/conns);
				}
				else
				{
					beaconPrintf(color, "%0.5f,",res/conns);
				}
			}
		}

		printf("] (%.2f, %.2f)\n", timerSeconds64(ground), timerSeconds64(raised));

	BEACON_CLIENT_PACKET_SEND();

	ea32ClearFast(&beacon_client.connect.group.indices);
	beaconClientAdvancePipeline();

	beaconClientSetPriorityLevel(1);
}

static void beaconClientFlushPipeline(void)
{
	while(beaconClientAdvancePipeline());
}

static void beaconClientProcessMsgBeaconConnectGroup(Packet* pakIn){
	S32 count = 0;
	S32 lo, hi;
	S32 i;
	U32 pipelineServer = pktGetBitsPack(pakIn, 1);

	lo = pktGetBitsPack(pakIn, 1);
	hi = pktGetBitsPack(pakIn, 1);

	count = pktGetBitsPack(pakIn, 1);

	if(pipelineServer)  
	{
		// Someone else finished our group, advance
		if(ea32Size(&beacon_client.connect.pipeline.indices))
			beaconClientAdvancePipeline();
	}
	else				// Someone else finished both our group and our pipeline, flush
		beaconClientFlushPipeline();

	if(pipelineServer)
	{
		beacon_client.connect.pipeline.hi = hi;
		beacon_client.connect.pipeline.lo = lo;
	}
	else
	{
		beacon_client.connect.group.hi = hi;
		beacon_client.connect.group.lo = lo;
	}

	for(i=0; i<count; i++)
	{
		S32 index = pktGetBitsPack(pakIn, 1);

		if(!pipelineServer)
			ea32Push(&beacon_client.connect.group.indices, index);
		else
			ea32Push(&beacon_client.connect.pipeline.indices, index);
	}

	if(!ea32Size(&beacon_client.connect.group.indices) && ea32Size(&beacon_client.connect.pipeline.indices))
		beaconClientAdvancePipeline();
}

static void beaconClientClosePipeToSentry(void){
	if(beacon_client.hPipeToSentry){
		CloseHandle(beacon_client.hPipeToSentry);
		beacon_client.hPipeToSentry = NULL;
		beacon_client.sentServerID = 0;
	}
}

static void createNewSentryClient(void){
	DWORD dwMode = PIPE_READMODE_MESSAGE | PIPE_NOWAIT;
	BeaconSentryClientData *client;

	client = callocStruct(BeaconSentryClientData);

	beacon_client.timeStartedClient = beaconGetCurTime();

	// Start a new client.
	if(!beaconClientRunExe(&client->pi))
	{
		free(client);
		return;
	}
	
	client->hPipe = CreateNamedPipe(L"\\\\.\\pipe\\CrypticBeaconClientPipe", 
									PIPE_ACCESS_DUPLEX,
									PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT,
									PIPE_UNLIMITED_INSTANCES,
									10000,
									10000,
									0,
									NULL);

	if(client->hPipe == INVALID_HANDLE_VALUE){
		printf("Failed to create pipe!\n");
		assert(0);
		
		return;
	}
	
	printf("Created pipe %d: %p\n", eaSize(&beacon_client.sentryClients)-1, client->hPipe);
							
	if(!SetNamedPipeHandleState(client->hPipe, &dwMode, NULL, NULL)){
		printf("mode failed: %d\n", GetLastError());
	}
	
	beacon_client.sentSentryClients = 0;
	
	eaPush(&beacon_client.sentryClients, client);
}

static S32 writeDataToPipe(HANDLE hPipe, const char* data, size_t length){
	S32 bytesWritten;
	
	if(WriteFile(hPipe, data, (DWORD)length, &bytesWritten, 0)){
		return 1;
	}else{
		printf("pipe write failed: h=%d, error=%d\n", (intptr_t)hPipe, GetLastError());
		
		if(hPipe == beacon_client.hPipeToSentry){
			beaconClientClosePipeToSentry();
		}
		
		return 0;
	}
}

static S32 writeStringToPipe(HANDLE hPipe, const char* text){
	return writeDataToPipe(hPipe, text, strlen(text));
}

static S32 printfPipe(HANDLE hPipe, const char* format, ...){
	char buffer[10000];
	va_list argptr;
	
	if(!hPipe){
		hPipe = beacon_client.hPipeToSentry;
	}
	
	if(!hPipe){
		return 0;
	}	
	
	va_start(argptr, format);
	_vsnprintf(buffer, ARRAY_SIZE(buffer) - 100, format, argptr);
	va_end(argptr);
	
	return writeStringToPipe(hPipe, buffer);
}

static char* findNonSpace(char* text){
	while(*text && isspace((unsigned char)*text)){
		text++;
	}
	
	return text;
}

static void beaconClientParseSentryClientCmd(BeaconSentryClientData* client, char* cmdName, char* value){
	#define IF_CMD(cmdstring) else if(!stricmp(cmdName, cmdstring))
	
	if(!*cmdName){
		return;
	}
	IF_CMD("MyServerID"){
		client->myServerID = atoi(value);
		beacon_client.sentSentryClients = 0;
		beaconPrintf(COLOR_GREEN, "Client %p:%d: MyServerID = %d\n", client->hPipe, client->pi.dwProcessId, client->myServerID);
	}
	IF_CMD("ServerUID"){
		estrCopy2(&client->serverUID, value);
		beacon_client.sentSentryClients = 0;
		beaconPrintf(COLOR_GREEN, "Client %p:%d: ServerUID = %s\n", client->hPipe, client->pi.dwProcessId, client->serverUID);
	}
	IF_CMD("Time"){
		client->lastSentTime = atoi(value);
	}
	IF_CMD("ForcedInactive"){
		client->forcedInactive = atoi(value) ? 1 : 0;
		beacon_client.sentSentryClients = 0;
		beaconPrintf(COLOR_GREEN, "Client %p:%d: ForcedInactive = %d\n", client->hPipe, client->pi.dwProcessId, client->forcedInactive);
	}
	IF_CMD("Crash"){
		//{
		//	char computerName[100];
		//	S32 size = ARRAY_SIZE(computerName);
		//	GetComputerName(computerName, &size);
		//	if(!stricmp(computerName, "beacon2")){
		//		S32* x = NULL;
		//		//*x = 10;
		//		//assert(0);
		//	}
		//}
		
		estrCopy2(&client->crashText, value);
		beacon_client.sentSentryClients = 0;

		beaconPrintf(COLOR_RED, "Client %p:%p:%d: \n%s\n", client, client->hPipe, client->pi.dwProcessId, client->crashText);
	}
	else{
		beaconPrintf(COLOR_RED|COLOR_GREEN,
					"Client %p:%d: Unknown cmd: %s = %s\n",
					client->hPipe,
					client->pi.dwProcessId,
					cmdName,
					value);
	}
	
	#undef IF_CMD
}

static void beaconClientOpenPipeToSentry(void){
	if(!beacon_client.hPipeToSentry){
		beacon_client.hPipeToSentry = CreateFile(L"\\\\.\\pipe\\CrypticBeaconClientPipe", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);

		if(beacon_client.hPipeToSentry != INVALID_HANDLE_VALUE){
			printf("Connected to sentry with pipe: %p\n", beacon_client.hPipeToSentry);
			printfPipe(0, "ProcessID %d\nForcedInactive %d", _getpid(), beacon_client.userInactive);
		}else{
			//printf("CreateFile failed: %d\n", GetLastError());
			beacon_client.hPipeToSentry = NULL;
		}
	}
}

void beaconClientSetWorkDuringUserActivity(S32 set){
	beacon_client.workDuringUserActivity = set ? 1 : 0;
}

void beaconClientReleaseSentryMutex(void){
	beaconReleaseAndCloseMutex(&beacon_client.hMutexSentry);
}

static S32 beaconClientAcquireSentryMutex(void){
	beacon_client.hMutexSentry = CreateMutex(NULL, 0, L"Global\\CrypticBeaconClientSentry");

	assert(beacon_client.hMutexSentry);

	if(beaconAcquireMutex(beacon_client.hMutexSentry)){
		return 1;
	}

	beaconClientReleaseSentryMutex();

	return 0;
}

static char* beaconClientGetCmdLine(S32 useMaster,
									S32 useSub,
									S32 useWorkDuringUserActivity)
{
	static char* buffer = NULL;
	
	char cmdLine[1000];
	cmdLine[0] = 0;

	estrClear(&buffer);
	
	estrConcatf(&buffer, " -beaconclient");
	
	if(	useMaster &&
		beacon_common.masterServerName)
	{
		estrConcatf(&buffer, " %s", beacon_common.masterServerName);
	}
	
	if(	useSub &&
		beacon_client.subServerName)
	{
		estrConcatf(&buffer, " -beaconclientsubserver %s", beacon_client.subServerName);
		
		if(	beacon_client.subServerPort &&
			!strchr(beacon_client.subServerName, ':'))
		{
			estrConcatf(&buffer, ":%d", beacon_client.subServerPort);
		}
	}
	
	if(	useWorkDuringUserActivity &&
		beacon_client.workDuringUserActivity)
	{
		estrConcatf(&buffer, " -beaconworkduringuseractivity");
	}
	
	beaconGetCommonCmdLine(SAFESTR(cmdLine));
	
	estrConcat(&buffer, cmdLine, (S32)strlen(cmdLine));
	
	return buffer;
}

static void beaconClientCheckForSentry(void){
	static S32 lastTotalTime = -1;
	static U32 lastTime;
	static U32 totalTime;
	static HANDLE hMutex;

	if(isOSShuttingDown())
		return;

	if(beacon_client.hPipeToSentry){
		lastTime = 0;
		return;
	}
	
	if(!hMutex){
		hMutex = CreateMutex(NULL, 0, L"Global\\CrypticBeaconClientSentryCheck");
	}

	assert(hMutex);

	// Just have one client do a keep-alive/restart check, will lose mutex on death
	if(beaconAcquireMutex(hMutex)){
		U32 curTime = time(NULL);
		
		if(!lastTime){
			lastTime = curTime;
		}
		
		if(beaconClientAcquireSentryMutex()){
			totalTime += curTime - lastTime;
			
			beaconClientReleaseSentryMutex();
			
			if(totalTime >= 10){
				printf("Starting a new sentry!\n");
				
				beaconStartNewExe(	beaconClientExeName,
									beacon_client.exeData,
									beacon_client.exeSize,
									beaconClientGetCmdLine(1, 0, 1),
									0, 0,
									hideClient);
				
				totalTime = 0;
				lastTime = 0;
			}else{
				if(lastTotalTime != totalTime){
					beaconPrintf(COLOR_RED|COLOR_GREEN, "Starting new sentry in %d seconds.\n", 10 - totalTime);
					lastTotalTime = totalTime;
				}
			}
		}else{
			totalTime = 0;
			lastTotalTime = -1;
		}

		lastTime = curTime;
	}
}

static void beaconClientParseSentryClientCmds(BeaconSentryClientData* client, char* cmds){
	char* cmd;
	char delim;
	BeaconSentryClientData* clientBackup = client;
	
	for(cmd = strsep2(&cmds, "\n", &delim); cmd; cmd = strsep2(&cmds, "\n", &delim)){
		char* cmdName;

		for(cmdName = cmd = findNonSpace(cmd); *cmd && !isspace((unsigned char)*cmd); cmd++);
		if(*cmd){
			*cmd++ = 0;
		}
		cmd = findNonSpace(cmd);
		
		if(!stricmp(cmd, "<<") && delim){
			char* start = cmd + 3;
			
			for(cmd = strsep2(&cmds, "\n", &delim); cmd; cmd = strsep2(&cmds, "\n", &delim)){
				if(!stricmp(cmd, ">>")){
					break;
				}
			}

			if(!cmd){
				cmd = cmds;
			}
			
			{
				char* cur = start;
				
				while(cur != cmd){
					if(!*cur){
						*cur = '\n';
					}
					cur++;
				}
				
				cur[-1] = 0;
			}
			
			assert(client == clientBackup);
			beaconClientParseSentryClientCmd(client, cmdName, start);
		}else{
			assert(client == clientBackup);
			beaconClientParseSentryClientCmd(client, cmdName, cmd);
		}
	}
}

static void beaconSentryDestroyClientData(BeaconSentryClientData* client){
	DisconnectNamedPipe(client->hPipe);
	CloseHandle(client->hPipe);
	
	estrDestroy(&client->crashText);
	
	estrDestroy(&client->serverUID);
}

static void beaconClientCheckSentry(void){
	if(beaconClientIsSentry()){
		S32 needNewSentryClient = 0;
		S32 bytesRead;
		S32 i;

		for(i=eaSize(&beacon_client.sentryClients)-1; i>=0; i--)
		{
			BeaconSentryClientData *client = beacon_client.sentryClients[i];
			HANDLE handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, client->pi.dwProcessId);

			if(!handle)
			{
				beaconPrintf(COLOR_YELLOW, "Client closed unexpectedly!");
				eaRemoveFast(&beacon_client.sentryClients, i);
				free(client);
			}
			else
			{
				CloseHandle(handle);
			}
		}
		
		if(	beacon_client.userInactive &&
			(!beacon_client.timeStartedClient ||
			eaSize(&beacon_client.sentryClients) == beacon_client.cpuCount))
		{
			// Reset the timer whenever there's enough clients.
			
			beacon_client.timeStartedClient = beaconGetCurTime();
		}
		else if(beacon_client.userInactive && eaSize(&beacon_client.sentryClients) < beacon_client.cpuCount){
			// Not enough clients.
			
			if(	beaconTimeSince(beacon_client.timeStartedClient) > 5 &&
				!beacon_client.needsSentryUpdate)
			{
				createNewSentryClient();
			}
		}
		else{
			// Too many clients!!!
			
			S32 toKillCount = eaSize(&beacon_client.sentryClients) - beacon_client.cpuCount;

			if(!beacon_client.userInactive)
				toKillCount = eaSize(&beacon_client.sentryClients);
			
			beacon_client.timeStartedClient = beaconGetCurTime();

			// Kill extra processes.			
			for(i = eaSize(&beacon_client.sentryClients)-1; toKillCount && i>=0; i--){
				BeaconSentryClientData* client = beacon_client.sentryClients[i];
				
				if(client->pi.dwProcessId && !client->crashText){
					HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, client->pi.dwProcessId);
					
					if(hProcess){
						toKillCount--;
						beaconPrintf(COLOR_RED, "Terminating extra process: %d\n", client->pi.dwProcessId);
						TerminateProcess(hProcess, 0);
						CloseHandle(hProcess);

						eaRemoveFast(&beacon_client.sentryClients, i);
						free(client);
					}
				}
			}
		}
		
		for(i = eaSize(&beacon_client.sentryClients)-1; i>=0; i--){
			BeaconSentryClientData* client = beacon_client.sentryClients[i];
			BeaconSentryClientData* clientBackup = client;
			S32 readCount = 0;
			char inBuffer[10000];
			
			while(readCount < 10 && ReadFile(client->hPipe, inBuffer, ARRAY_SIZE(inBuffer) - 1, &bytesRead, NULL)){
				readCount++;
				
				assert(client == clientBackup);
				
				assert(bytesRead < ARRAY_SIZE(inBuffer));
				
				if(bytesRead){
					S32 wasCrashed = client->crashText ? 1 : 0;
					
					inBuffer[bytesRead] = 0;
					
					beaconClientParseSentryClientCmds(client, inBuffer);
					
					if(!wasCrashed && client->crashText){
						beaconPrintf(COLOR_GREEN,
									"client %p:%p:%d\nCRASH!!!!!!!!!!!!!!!!!!!!!!\n%s\n",
									client,
									client->hPipe,
									client->pi.dwProcessId,
									client->crashText);
					}

					if(	readCount == 1 &&
						i == eaSize(&beacon_client.sentryClients) - 1)
					{
						needNewSentryClient = 1;
					}
				}
			}

			if(readCount < 10){
				S32 error = GetLastError();
				
				switch(error){
					xcase ERROR_BROKEN_PIPE:
					//case ERROR_PIPE_BUSY:
					case ERROR_BAD_PIPE:{
						beaconPrintf(COLOR_RED,
									"Client disconnected: pid %d, pipe %p, error %d\n",
									client->pi.dwProcessId, client->hPipe, error);
						
						beaconSentryDestroyClientData(client);
						
						eaRemoveFast(&beacon_client.sentryClients, i);
						free(client);
								
						beacon_client.sentSentryClients = 0;
					}
					
					xcase ERROR_PIPE_LISTENING:
					case ERROR_NO_DATA:{
						// These are normal messages for an open pipe.
					}
					
					xdefault:{
						printf("Unhandled error on pipe %d, handle %p: %d\n", i, client->hPipe, error);
					}
				}
			}
		}
	}else{
		static U32 checkSentryTime;
		
		if(timerSeconds(timerCpuTicks() - checkSentryTime) > 0.5){
			S32 sendProcessID = !beacon_client.hPipeToSentry;

			beaconClientOpenPipeToSentry();

			if(beacon_client.hPipeToSentry){
				if(!beacon_client.sentServerID){
					beacon_client.sentServerID = 1;
					
					printfPipe(0, "MyServerID %d\nServerUID %s", beacon_client.myServerID, beacon_client.serverUID);
				}else{
					if (!printfPipe(0, "Time %d", time(NULL)))
					{
						//This is a worker that the sentry stopped talking to that has no pipe to the sentry.
						//Suicide for no longer being useful
						exit(0);
					}
				}
			}
			
			if(!beaconIsSharded()){
				beaconClientCheckForSentry();
			}
		}
	}
}

static void beaconClientPrintSentryClients(void){
	S32 i;

	if(!beaconClientIsSentry()){
		return;
	}

	beaconPrintf(	COLOR_GREEN,
					"\n\n"
					"%-12s%-12s%-12s%-1s\n",
					"PID",
					"TimeDelta",
					"MyServerID",
					"ServerUID");
	
	for(i = 0; i < eaSize(&beacon_client.sentryClients); i++){
		BeaconSentryClientData* client = beacon_client.sentryClients[i];
		S32 state;
		S32 curInstances;
		S32 maxCollectionCount;
		S32 collectDataTimeout;
		char *pUserName = NULL;
		char timeString[100];
		
		sprintf(timeString, "%"FORM_LL"ds", time(NULL) - client->lastSentTime);
		
		estrStackCreate(&pUserName);

		printf("%-12d%-12s%-12d%-1s\n", client->pi.dwProcessId, timeString, client->myServerID, client->serverUID);
		
		GetNamedPipeHandleState_UTF8(client->hPipe,
								&state,
								&curInstances,
								&maxCollectionCount,
								&collectDataTimeout,
								&pUserName);

		estrDestroy(&pUserName);
	}

	printf("\n\n");
}

static void beaconSentryKillClient(BeaconSentryClientData *client)
{
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, client->pi.dwProcessId);

	if(hProcess){
		beaconPrintf(COLOR_RED, "Terminating process: %d\n", client->pi.dwProcessId);
		TerminateProcess(hProcess, 0);
		CloseHandle(hProcess);

		eaFindAndRemoveFast(&beacon_client.sentryClients, client);
	}
}

static void beaconClientKillSentryProcesses(S32 killActive, S32 killCrashed){
	S32 i;

	if(!beaconClientIsSentry()){
		exit(0);		// MS told me to die
		return;
	}

	for(i = eaSize(&beacon_client.sentryClients)-1; i>=0; i--){
		BeaconSentryClientData* client = beacon_client.sentryClients[i];

		if(	killCrashed &&
			client->crashText
			||
			killActive &&
			!client->crashText)
		{
			beaconSentryKillClient(client);
		}
	}
}

static void beaconClientSetForcedInactive(S32 on, char* reason){
	on = on ? 1 : 0;
	
	if(beacon_client.userInactive != on){
		beaconPrintf(	COLOR_GREEN,
						"Switched to %s: %s\n",
						on ? "INACTIVE" : "ACTIVE",
						reason);

		BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_client.masterLink, BMSG_C2ST_USER_INACTIVE);
		
			pktSendBits(pak, 1, on);
			pktSendString(pak, reason);

		BEACON_CLIENT_PACKET_SEND();
			
		beacon_client.userInactive = on;

		printfPipe(0, "ForcedInactive %d", on);

		if(on)
		{
			beacon_client.sentSentryClients = 0;
		}

		if(!on)
		{
			if(!beaconIsSharded()){
				beaconClientKillSentryProcesses(1, 0);
			}
		}	
	}
}

static void setEnvironmentVar(StashTable st, const char* name, const char* valueParam, int concatPath){
	StashElement	element;
	char *pValue2 = NULL;
	char*			value = malloc(10000);
	
	strcpy_s(value, 10000, valueParam);
	
	if(stashFindElement(st, name, &element)){
		char* oldValue = stashElementGetPointer(element);
		
		if(	!stricmp(name, "path") &&
			concatPath)
		{
			size_t oldLen = strlen(oldValue);
			
			memmove(value + oldLen + 1, value, strlen(value) + 1);
			
			strcpy_s(value, 10000, oldValue);
			
			value[oldLen] = ';';
		}
						
		free(oldValue);
	}

	ExpandEnvironmentStrings_UTF8(value, &pValue2);

	stashAddPointer(st, name, strdup(pValue2), true);

	//printfColor(COLOR_BRIGHT|COLOR_GREEN, "%s=%s\n", name, value2);
	
	estrDestroy(&pValue2);
	SAFE_FREE(value);
}

static void getDefaultEnvironmentHelper(StashTable st, const char* keyName){
	RegReader	rr = createRegReader();
	S32			i;

	initRegReader(rr, keyName);
	
	for(i = 0;; i++){
		char	name[1000];
		S32		nameLen = sizeof(name);
		char	value[10000];
		S32		valueLen = sizeof(value);
		S32		retVal;
		
		retVal = rrEnumStrings(rr, i, name, &nameLen, value, &valueLen);
		
		if(retVal < 0){
			break;
		}
		else if(retVal > 0){
			setEnvironmentVar(st, name, value, 1);
		}
	}
	
	destroyRegReader(rr);
}

static void freeValue(void* value){
	free(value);
}

static void* getDefaultEnvironment(void){
	StashTable			st = stashTableCreateWithStringKeys(100, StashDeepCopyKeys_NeverRelease);
	StashTableIterator	it;
	StashElement		element;
	char*				buffer = NULL;
	S32					bufferLen = 0;
	S32					bufferMaxLen = 0;
	char*				newLine;
	char*				curEnv = NULL;
	char*				curEnvEnd;
	char				line[10000];
	
	// Start with all the current environment strings.
	
	GetEnvironmentStrings_UTF8(&curEnv);

	curEnvEnd = curEnv;

	while(curEnvEnd[0]){
		char* start = curEnvEnd;
		char* equal;
		
		curEnvEnd += strlen(curEnvEnd) + 1;
		
		strcpy(line, start);
		
		equal = strstr(line, "=");
		
		if(equal && line[0]){
			*equal = 0;
			
			setEnvironmentVar(st, line, equal + 1, 0);
		}
	}
	
	estrDestroy(&curEnv);
	
	// Now copy over with the default environment.
	
	getDefaultEnvironmentHelper(st, "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
	getDefaultEnvironmentHelper(st, "HKEY_CURRENT_USER\\Environment");
	
	for(stashGetIterator(st, &it); stashGetNextElement(&it, &element); ){
		sprintf(line, "%s=%s", (char*)stashElementGetKey(element), (char*)stashElementGetPointer(element));
		
		//printf("%s\n", line);
		
		newLine = dynArrayAddStructs(buffer, bufferLen, bufferMaxLen, (S32)strlen(line) + 1);
		
		memmove(newLine, line, strlen(line) + 1);
	}
	
	newLine = dynArrayAddStructs(buffer, bufferLen, bufferMaxLen, 1);
	
	newLine[0] = 0;
	
	stashTableDestroyEx(st, NULL, freeValue);
	
	return buffer;
}

static void beaconClientStartGimmeExe(void){
	#if 1
		void* environment = getDefaultEnvironment();
		STARTUPINFO si = {0};
		PROCESS_INFORMATION pi = {0};

		si.cb = sizeof(si);
		
		CreateProcess(	
						#if 0
							L"c:\\test.bat",
						#else
							L"c:\\game\\tools\\util\\gimme.exe", 
						#endif
						NULL,
						NULL,
						NULL,
						FALSE,
						CREATE_NEW_CONSOLE, 
						environment,
						NULL,
						&si,
						&pi);
						
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
						
		SAFE_FREE(environment);
	#else	
		ulShellExecute(NULL, "open", "cmd", "/c start /i c:\\game\\tools\\util\\gimme.exe", "", SW_NORMAL);
	#endif
}

static void beaconClientRunGimmeAtNight(void){
	static struct tm prevTmTime;
	
	time_t curTime = time(NULL);
	struct tm tmTime;
	
	localtime_s(&tmTime, &curTime);
	
	if(!prevTmTime.tm_year){
		prevTmTime = tmTime;
	}
	
	if(	prevTmTime.tm_hour != 4 &&
		tmTime.tm_hour == 4)
	{
		char timeString[100];
		
		asctime_s(SAFESTR(timeString), &tmTime);
	
		timeString[strlen(timeString) - 1] = 0;
		
		printf("%s: Running gimme.exe!\n", timeString);
		
		beaconClientStartGimmeExe();
	}
	
	prevTmTime = tmTime;
}

char* beaconIsHighPriorityProcRunning(void)
{
	int i;
	FILE *task_file;
	char curtask[MAX_PATH];
	char tasklist[] = "tasklist > tasks.txt";
	char **tasks = NULL;

	for(i=0; i<eaSize(&beacon_client.highproc.exes); i++)
	{
		if(ProcessCount(beacon_client.highproc.exes[i], true)>0)
		{
			return beacon_client.highproc.exes[i];
		}
	}

	system(tasklist);
	task_file = fopen("tasks.txt", "r");

	while(fgets(curtask, MAX_PATH, task_file))
	{
		char *space = strchr(curtask, ' ');
		if(!space)
		{
			continue;
		}
		space[0] = '\0';

		for(i=0; i<eaSize(&beacon_client.highproc.exes); i++)
		{
			if(!stricmp(curtask, beacon_client.highproc.exes[i]))
			{
				fclose(task_file);
				return beacon_client.highproc.exes[i];
			}
		}
	}

	if(task_file)
	{
		fclose(task_file);
	}

	return 0;
}

static void beaconClientCheckForUserActivity(void){
	static U32 goActiveTime;

	if(!goActiveTime){
		goActiveTime = timerCpuSpeed() * 5 * 60;
		beacon_client.timeSinceUserActivity = 0;
	}

	if(!beacon_client_conn.readyToWork && !beaconClientIsSentry()){
		return;
	}
	
	if(beacon_client.workDuringUserActivity){
		beaconClientSetForcedInactive(1, "Work during user activity is enabled.");
		beacon_client.timeSinceUserActivity = max(goActiveTime, beacon_client.timeSinceUserActivity);
	}
	else if(beaconClientIsSentry()){
		// Only sentries will monitor for activity.
		
		static S32		beenHereBefore;
		static POINT	ptLast;
		static U32		lastTime;
		SYSTEMTIME		systime;
		
		POINT ptCur = {0};
		U32 curTime = timerCpuTicks();
		
		//beaconClientRunGimmeAtNight();

		GetCursorPos(&ptCur);
		
		if(!beenHereBefore){
			beenHereBefore = 1;
			ptLast = ptCur;
			lastTime = curTime;
		}

		GetLocalTime(&systime);

		switch(beacon_client.config.ActiveState)
		{
			xcase 0: {		// Default
				U32 userInput = !beacon_client.ignoreInputs && 
								(beacon_client.keyboardPressed ||
								abs(ptCur.x - ptLast.x) > 10 ||
								abs(ptCur.y - ptLast.y) > 10);
				if(	userInput )
				{
					// A key was pressed or the mouse was moved.

					beaconClientSetForcedInactive(0, beacon_client.keyboardPressed ? "Keyboard pressed." : "Mouse moved.");

					ptLast = ptCur;
					beacon_client.timeSinceUserActivity = 0;
					beacon_client.keyboardPressed = 0;
				}else{
					U32 delta = curTime - lastTime;
					char *highPriorityProc = beaconIsHighPriorityProcRunning();

					if( highPriorityProc ) {
						char buffer[MAX_PATH];
						sprintf(buffer, "High priority process detected: %s", highPriorityProc);
						beaconClientSetForcedInactive(0, buffer);
					}
					else
					{
						beacon_client.timeSinceUserActivity += delta;

						if(beacon_client.timeSinceUserActivity >= goActiveTime){
							if(!beacon_client.isUserMachine || systime.wHour <= 6 || systime.wHour >= 22 || systime.wDayOfWeek<1 || systime.wDayOfWeek>=6)
							{
								beaconClientSetForcedInactive(1, "No user activity for a while.");
							}
							else
							{
								beaconClientSetForcedInactive(0, "During work hours.");
							}

						}
						else{
							beaconClientSetForcedInactive(0, "Timer still going down.");
						}
					}
				}

				lastTime = curTime;
			}
			xcase 1: {
				beaconClientSetForcedInactive(0, "Config requires activity.");

				if(!beaconIsSharded()){
					beaconClientKillSentryProcesses(1, 1);
				}
			}
			xcase 2:{
				beaconClientSetForcedInactive(1, "Config forced inactivity");
			}
		}

		if(eaSize(&beacon_client.sentryClients) && !beacon_client.userInactive)
		{
			beaconClientKillSentryProcesses(1, 0);
		}
	}
}

static void beaconClientConnectIdleCallback(int iPartitionIdx, F32 timeLeft){
	if(timeLeft){
		//beaconClientUpdateTitle("Connecting...", timeLeft);
	}
	
	beaconClientCheckForUserActivity();
	
	//printf("waiting %1.2f\n", timeLeft);
	
	if(IsWindowVisible(beaconGetConsoleWindow()) && _kbhit()){
		U8 theChar;
		
		theChar = _getch();
		switch(tolower(theChar)){
			xcase 27:{
				printf(	"\n"
						"a : Toggle work during user activity.\n"
						"c : Print client list.\n"
						"d : Foribly disconnect.\n"
						"g : Test nightly gimme execution.\n"
						"h : Hide window (also hides when minimized).\n"
						"m : Print memory usage.\n"
						"t : Run collision test.\n"
						"x : Prompted to cause intentional crash.\n"
						". : Increase CPU limit by 1\n"
						", : Decrease CPU limit by 1\n"
						"p : Print full CRC info of map\n"
						"r : Run command\n"
						"\n");
			}
			
			xcase 'a':{
				if(beaconClientIsSentry()){
					S32 set = beacon_client.workDuringUserActivity = !beacon_client.workDuringUserActivity;
					
					beaconPrintf(	COLOR_GREEN,
									"Setting work during user activity: %s\n",
									set ? "ON" : "OFF");
				}					
			}

			xcase 'p':{
				if(beaconClientIsSentry())
				{
					break;
				}
				beaconLogCRCInfo(worldGetActiveColl(iPartitionIdx));
			}
			
			xcase 'c':{
				beaconClientPrintSentryClients();
			}
			
			xcase 'd':{
				if(linkConnected(beacon_client.serverLink)){
					printf("Disconnecting because of keypress: ");
					linkRemove(&beacon_client.serverLink);
					printf("Done!\n");
				}
			}
		
			xcase 'g':{
				beaconClientStartGimmeExe();
			}
			
			xcase 'h':{
				if(!beaconIsProductionMode()){
					ShowWindow(beaconGetConsoleWindow(), SW_HIDE);
				}
			}
			
			xcase 'm':{
				beaconPrintMemory();
			}

			xcase 't':{
				//beaconTestCollision();
			}
			
			xcase 'x':{
				char buffer[6];
				
				beaconPrintf(COLOR_YELLOW, "Enter \"CRASH\" to crash: ");
				
				if(beaconEnterString(buffer, 5) && !strncmp(buffer, "CRASH", 5)){
					beaconPrintf(COLOR_RED, "\nCrashing intentionally!\n\n");
					assertmsg(0, "Intentional beacon client crash!");
					beaconPrintf(COLOR_RED, "\nDone crashing intentionally, continuing execution.\n\n");
				}else{
					printf("Canceled!\n\n");
				}
			}
			xcase 'r':{
				char buffer[MAX_PATH];

				beaconPrintf(COLOR_YELLOW, "Enter command: ");
				if(beaconEnterString(buffer, MAX_PATH))
				{
					globCmdParsef("%s", buffer);
				}
				printf("\n");
			}
			xcase ',':{
				beacon_client.cpuCount--;
				beacon_client.cpuCount = MAX(beacon_client.cpuCount, 0);
				beaconPrintf(COLOR_GREEN, "\nDecreasing CPU count to: %d\n", beacon_client.cpuCount);
			}
			xcase '.':{
				beacon_client.cpuCount++;
				beacon_client.cpuCount = MIN(beacon_client.cpuCount, beacon_client.cpuMax);
				beaconPrintf(COLOR_GREEN, "\nIncreasing CPU count to: %d\n", beacon_client.cpuCount);
			}
		}
	}

	beaconClientCheckSentry();
}

static void beaconClientSetServerID(U32 clientUID, const char* serverUID){
	if(	beacon_client.myServerID != clientUID ||
		!beacon_client.serverUID ||
		stricmp(beacon_client.serverUID, serverUID))
	{
		beacon_client.myServerID = clientUID;
		estrCopy2(&beacon_client.serverUID, serverUID);
		
		printf("New server ID: %s:%d\n", serverUID, beacon_client.myServerID);

		beacon_client.sentServerID = 0;
	}
}

static void beaconClientProcessMsgLegalAreas(Packet* pak){
	S32 grid_x = pktGetBitsPack(pak, 1);
	S32 grid_z = pktGetBitsPack(pak, 1);
	BeaconDiskSwapBlock* block = beaconGetDiskSwapBlockByGrid(grid_x, grid_z);
	BeaconDiskSwapBlock* cur;
	
	assert(block);
	
	beaconClientReceiveLegalAreas(pak, block);
	
	// Clear the addedLegal flag.
	
	for(cur = bp_blocks.list; cur; cur = cur->nextSwapBlock){
		cur->addedLegal = 0;
	}
	
	printf(	"Processing %4d legal areas in (%4d, %4d): ",
			block->legalCompressed.totalCount,
			grid_x,
			grid_z);
	
	beaconClientUpdateTitle("Processing block (%d, %d)", grid_x, grid_z);

	beaconClientSetPriorityLevel(0);
	beaconProcessLegalAreas(BEACONCLIENT_PARTITION, block);
	beaconClientSetPriorityLevel(1);
	
	block->addedLegal = 1;
	
	printf(", Done!\n");
	
	beaconClientSendLegalAreas(grid_x, grid_z);
	
	beaconClearNonAdjacentSwapBlocks(NULL);
}

static void beaconClientProcessMsgBeaconList(Packet* pak){
	S32 count = pktGetBitsPack(pak, 1);
	S32 i;
	S32 getConns = 1;
	
	printf("Receiving beacon list (%s bytes).\n", getCommaSeparatedInt(pktGetSize(pak)));
	
	beaconFreeUnusedMemoryPools();

	assert(!combatBeaconArray.size);
	
	for(i = 0; i < count; i++){
		Beacon* b;
		Vec3 pos;
		
		pktGetVec3(pak, pos);
		
		b = addCombatBeacon(pos, 1, 0, 0, 0);
		
		assert(b);
		
		b->userInt = i;
		b->globalIndex = i;
		b->noGroundConnections = pktGetBits(pak, 1);
	}
	assert(combatBeaconArray.size == count);

	for(i=0; i<BEACON_MAX_DBG_IDS; i++)
		beacon_state.debugBeacons[i] = NULL;

	for(i=0; i<combatBeaconArray.size; i++)
	{
		int j;
		Beacon *b = combatBeaconArray.storage[i];

		for(j=0; j<BEACON_MAX_DBG_IDS; j++)
		{
			if(!vec3IsZero(beacon_state.debugBeaconPos[j]))
			{
				if(!beacon_state.debugBeacons[j])
					beacon_state.debugBeacons[j] = b;
				else if(distance3Squared(beacon_state.debugBeaconPos[j], b->pos) <
						distance3Squared(beacon_state.debugBeaconPos[j], beacon_state.debugBeacons[j]->pos))
				{
					beacon_state.debugBeacons[j] = b;
				}
			}
		}
	}

#if BEACON_CLIENT_PROTOCOL_VERSION >= 5
	if(beacon_client.serverProtocolVersion >= 1)
		getConns = pktGetBitsAuto(pak);
#endif

	if(getConns)
	{
		for(i=0; i<count; i++)
		{
			Beacon* b;
			int connCount;
			int j;

			b = combatBeaconArray.storage[i];

			connCount = pktGetBitsAuto(pak);
			for(j=0; j<connCount; j++)
			{
				BeaconConnection *conn = createBeaconConnection();
				Beacon *dst = NULL;
				int index = pktGetBitsAuto(pak);

				assert(index>=0 && index<combatBeaconArray.size);

				conn->destBeacon = combatBeaconArray.storage[index];
				arrayPushBack(&b->gbConns, conn);
			}

			connCount = pktGetBitsAuto(pak);
			for(j=0; j<connCount; j++)
			{
				BeaconConnection *conn = createBeaconConnection();
				Beacon *dst = NULL;
				int index = pktGetBitsAuto(pak);

				assert(index>=0 && index<combatBeaconArray.size);

				conn->destBeacon = combatBeaconArray.storage[index];
				arrayPushBack(&b->rbConns, conn);
			}
		}
	}
	
	beacon_process.infoArray = beaconAllocateMemory(combatBeaconArray.size * sizeof(*beacon_process.infoArray));

	beaconTestGeoProximity(BEACONCLIENT_PARTITION);
}

static void beaconClientProcessMsgTransferToServer(Packet* pak){
	char serverName[100];
	
	pktGetString(pak,serverName,sizeof(serverName));
	
	SAFE_FREE(beacon_client.subServerName);
	
	beacon_client.subServerName = pktMallocString(pak);
	beacon_client.subServerPort = pktGetBitsPack(pak, 1);

	if(linkConnected(beacon_client.serverLink)){
		beaconPrintf(	COLOR_GREEN,
						"Transferring to subserver: %s/%s:%d\n",
						serverName,
						beacon_client.subServerName,
						beacon_client.subServerPort);
	}
}

// HEh, all typos copied from function definition in pcl_typedefs
bool beaconSentryPatchProcess(PatchProcessStats *stats, void * userData)
{
	float fProgress = (stats->total > 0 && stats->total_files > 0)
		? ((float)stats->received/stats->total + (float)stats->received_files/stats->total_files)/2.f // average of file and byte progress
		: -1; 

	printf("Progress: %.3f | Received: %.3f/%.3f MB (%u/%u Files) | Rate: %.3f MB/s\r",
			fProgress, stats->received / (1024.0 * 1024), stats->total / (1024.0 * 1024), stats->received_files, 
			stats->total_files, stats->seconds ? (stats->received / (stats->seconds * 1024.0 * 1024)) : 0);

	return false;
}

static void beaconClientProcessMsgKillProcesses(Packet* pak){
	S32 justCrashed = pktGetBits(pak, 1);
	
	beaconClientKillSentryProcesses(!justCrashed, 1);
}

static void beaconClientProcessMsgMapData(Packet* pak){
	beaconClientSetPriorityLevel(0);

	beaconClientReceiveMapData(pak);

	beaconClientSetPriorityLevel(1);
}

static void beaconClientSendNeedMoreExeData(S32 size){
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_client.masterLink, BMSG_C2ST_NEED_MORE_EXE_DATA);

		pktSendBitsPack(pak, 1, size);

	BEACON_CLIENT_PACKET_SEND();
}

static void beaconClientProcessMsgExeData(Packet* pak){
	U32 curReceived = pktGetBitsPack(pak, 1);
	U32 totalBytes = pktGetBitsPack(pak, 1);
	U32 curBytes = pktGetBitsPack(pak, 1);

	dynArrayFitStructs(	&beacon_client.newExeData.data,
						&beacon_client.newExeData.maxTotalByteCount,
						totalBytes);

	beaconPrintf(	COLOR_GREEN,
					"Receiving %s/%s bytes of new exe.\n",
					getCommaSeparatedInt(curReceived + curBytes),
					getCommaSeparatedInt(totalBytes));
	
	pktGetBytes(pak, curBytes, (U8*)beacon_client.newExeData.data + curReceived);
	
	if(curReceived + curBytes == totalBytes){
		beaconPrintf(COLOR_GREEN, "Starting new exe!\n");
		
		beaconStartNewExe(	beaconClientExeName,
							beacon_client.newExeData.data,
							totalBytes,
							beaconClientGetCmdLine(1, 1, 1), 
							1,
							!beaconClientIsSentry(), hideClient);
	}else{
		beaconClientSendNeedMoreExeData(curReceived + curBytes);
	}
}

static void beaconClientProcessMsgMapDataLoadedReply(Packet* pak){
	S32 good = pktGetBits(pak, 1);
	
	if(good){
		printf("Server accepted map CRC, the fool!\n");

		if(!beacon_client.process_config)
			beacon_client.process_config = StructCreate(parse_BeaconProcessConfig);

		StructReset(parse_BeaconProcessConfig, beacon_client.process_config);
		ParserRecv(parse_BeaconProcessConfig, pak, beacon_client.process_config, 0);
	}else{
		printf("Server rejected map CRC, the fool!\n");

		beaconLogCRCInfo(beaconGetActiveWorldColl(BEACONCLIENT_PARTITION));
		logWaitForQueueToEmpty();
	}
}

static void beaconClientProcessMsgExecuteCommand(Packet* pak){
	char commandText[1000];
	U32 showWindowType;

	pktGetString(pak,commandText,sizeof(commandText));
	showWindowType = pktGetBitsPack(pak, 1);
	
	printf("Running command from server: %s\n", commandText);
	
	if(0){
		ulShellExecute(NULL, "open", commandText, "", "", showWindowType);
	}else{
		STARTUPINFO si = {0};
		PROCESS_INFORMATION pi;
		BOOL ret;
		
		si.cb = sizeof(si);
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = showWindowType;
		
		ret = CreateProcess_UTF8(NULL,
							commandText,
							NULL,
							NULL,
							FALSE,
							NORMAL_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE,
							NULL,
							NULL,
							&si,
							&pi);
							
		if(!ret){
			beaconPrintf(COLOR_RED, "Failed %d!\n", GetLastError());
		}else{
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
	}
		
	printf("Finished running command from server: %s\n", commandText);
}

static void beaconClientProcessMsgPing(Packet* pak){
}

static void beaconClientProcessMsgDebugState(Packet* pak) {
	void *debug_state = pktGetStruct(pak, parse_BeaconProcessDebugState);

	if(!debug_state)
	{
		return;
	}

	if(!beacon_client.debug_state)
	{
		beacon_client.debug_state = StructAlloc(parse_BeaconProcessDebugState);
	}

	StructCopyAll(parse_BeaconProcessDebugState, debug_state, beacon_client.debug_state);
}

static void beaconClientProcessMsgBeaconConnections(Packet* pak)
{
	int numB = pktGetBitsAuto(pak);
	int i;

	//BEACONPERF(Adam):Could probably cancel group assignment and move to pipelined if any assigned appears here
	for(i=0; i<numB; i++)
	{
		Beacon* b;
		int numC;
		int j;
		S32 index;

		index = pktGetBitsAuto(pak);
		assert(index>=0 && index<combatBeaconArray.size);

		b = combatBeaconArray.storage[index];

		numC = pktGetBitsAuto(pak);
		for(j=0; j<numC; j++)
		{
			BeaconConnection *conn = NULL;
			S32 target = pktGetBitsAuto(pak);
			Beacon *targetB;

			assert(target>=0 && target<combatBeaconArray.size);
			targetB = combatBeaconArray.storage[target];

			if(!beaconFindConnection(b, targetB, 0))
			{
				conn = createBeaconConnection();
				conn->destBeacon = targetB;

				arrayPushBack(&b->gbConns, conn);
			}
		}

		numC = pktGetBitsAuto(pak);
		for(j=0; j<numC; j++)
		{
			BeaconConnection *conn = NULL;
			S32 target = pktGetBitsAuto(pak);
			Beacon *targetB;

			assert(target>=0 && target<combatBeaconArray.size);
			targetB = combatBeaconArray.storage[target];

			if(!beaconFindConnection(b, targetB, 1))
			{
				conn = createBeaconConnection();
				conn->destBeacon = targetB;

				arrayPushBack(&b->rbConns, conn);
			}
		}
	}
}

static void beaconClientProcessMsgTextCmd(const char* textCmd, Packet* pak){
	#define BEGIN_HANDLERS()		if(0){
	#define HANDLER(x, y)			}else if(!stricmp(textCmd, x)){y(pak);beacon_client.timeOfLastCommand = timerCpuTicks()
	#define HANDLER_NO_TIME(x, y)	}else if(!stricmp(textCmd, x)){y(pak)
	#define END_HANDLERS()			}

	BEGIN_HANDLERS();
		HANDLER(BMSG_S2CT_KILL_PROCESSES,			beaconClientProcessMsgKillProcesses		);
		HANDLER(BMSG_S2CT_MAP_DATA,					beaconClientProcessMsgMapData			);
		HANDLER(BMSG_S2CT_MAP_DATA_LOADED_REPLY,	beaconClientProcessMsgMapDataLoadedReply);
		HANDLER(BMSG_S2CT_EXE_DATA,					beaconClientProcessMsgExeData			);
		HANDLER(BMSG_S2CT_PROCESS_LEGAL_AREAS,		beaconClientProcessMsgLegalAreas		);
		HANDLER(BMSG_S2CT_BEACON_LIST,				beaconClientProcessMsgBeaconList		);
		HANDLER(BMSG_S2CT_CONNECT_BEACONS,			beaconClientProcessMsgBeaconConnectGroup);
		HANDLER(BMSG_S2CT_BEACON_CONNECTIONS,		beaconClientProcessMsgBeaconConnections	);
		HANDLER(BMSG_S2CT_TRANSFER_TO_SERVER,		beaconClientProcessMsgTransferToServer	);
		HANDLER(BMSG_S2CT_EXECUTE_COMMAND,			beaconClientProcessMsgExecuteCommand	);
		HANDLER_NO_TIME(BMSG_S2CT_DEBUGSTATE,		beaconClientProcessMsgDebugState		);
		HANDLER_NO_TIME(BMSG_S2CT_PING,				beaconClientProcessMsgPing				);
	END_HANDLERS();

	#undef BEGIN_HANDLERS
	#undef HANDLER
	#undef END_HANDLERS
}

static void beaconClientHandleMsg(Packet* pak, S32 cmd, NetLink* link, void *user_data){
	beacon_client_conn.timeHeardFromServer = timerCpuTicks();

	switch(cmd){
		xcase BMSG_S2C_CONNECT_REPLY:{
			if(pktGetBits(pak, 1) == 1){
				// Connection accepted.
				
				if(!beacon_client_conn.readyToWork){
					U32 clientUID = pktGetBits(pak, 32);
					const char* serverUID = pktGetStringTemp(pak);

					beacon_client.serverProtocolVersion = 0;
#if BEACON_CLIENT_PROTOCOL_VERSION >= 5
					if(!pktEnd(pak))		// Old servers won't have sent this
						beacon_client.serverProtocolVersion = pktGetBitsAuto(pak);
#endif
					
					beaconClientSetServerID(clientUID, serverUID);
					
					printf("Connected!  Server: %s (prot: %d)\n", beaconGetLinkIPStr(link), beacon_client.serverProtocolVersion);
				}
			
				sendReadyMessage(link);
			}else{
				beacon_client.needsSentryUpdate = 1;  // This only matters on the Sentry, but if sentry matches, worker should too
			}
		}
		
		xcase BMSG_S2C_TEXT_CMD:{
			char textCmd[100];
			pktGetString(pak,textCmd,sizeof(textCmd));
			
			beaconClientProcessMsgTextCmd(textCmd, pak);
		}

		xcase BMSG_SET_GALAXY_GROUP_COUNT_CMD:
		{
			beacon_galaxy_group_count = (U32)pktGetBits(pak, 32);
			beaconPrintf(COLOR_BLUE, "New Galaxy Group Count Set: %d\n", beacon_galaxy_group_count);
		}
		
		xdefault:{
			beaconPrintf(COLOR_RED, "Unknown command from server %d\n", cmd);
		}
	}
	
	beacon_client_conn.timeHeardFromServer = timerCpuTicks();
}

char* beaconGetExeFileName(void){
	static char *pFileName = NULL;;
	
	if(!pFileName)
	{
		GetModuleFileName_UTF8(NULL, &pFileName);
		forwardSlashes(pFileName);
	}
	return pFileName;
}

char* beaconGetPdbFileName(void)
{
	static char *pPDBName = NULL;
	estrCopy2(&pPDBName, beaconGetExeFileName());
	estrReplaceOccurrences(&pPDBName, ".exe", ".pdb");

	return pPDBName;
}

char* beaconGetExeDirectory(void)
{
	static char *pDir;

	if(!pDir)
	{
		estrGetDirAndFileName(beaconGetExeFileName(), &pDir, NULL);
	}

	return pDir;
}

static void beaconClientSendConnect(NetLink *link){
	Packet* pak = pktCreate(link, BMSG_C2S_CONNECT);
	
	pktSendBits(pak, 32, 0);
	
	// Send the protocol version.
	
	pktSendBitsPack(pak, 1, BEACON_CLIENT_PROTOCOL_VERSION);
	
	pktSendBits(pak, 32, beacon_client.executableCRC);

#if BEACON_CLIENT_PROTOCOL_VERSION >= 2
	pktSendBits(pak, 32, beaconFileGetProcVersion());
#endif

	pktSendString(pak, getUserName());
	pktSendString(pak, getComputerName());
	
	pktSendBits(pak, 1, beaconClientIsSentry() ? 1 : 0);
	
	pktSend(&pak);
}

static U32 fakeCRC(char* data, S32 length){
	U32 acc = 0;
	while(length){
		acc += ((*data + length) * length) + *data;
		data++;
		length--;
	}
	return acc;
}

static void printFlags(U32 flags){
	#define CASE(x) if(flags & x){printf("%s,", #x + 5);}else{U32 i;for(i=0;0&&i<=strlen(#x+5);i++)printf(" ");}
	CASE(PAGE_EXECUTE);
	CASE(PAGE_EXECUTE_READ);
	CASE(PAGE_EXECUTE_READWRITE);
	CASE(PAGE_EXECUTE_WRITECOPY);
	CASE(PAGE_GUARD);
	CASE(PAGE_NOACCESS);
	CASE(PAGE_NOCACHE);
	CASE(PAGE_READONLY);
	CASE(PAGE_READWRITE);
	CASE(PAGE_WRITECOMBINE);
	CASE(PAGE_WRITECOPY);
	#undef CASE
}

static void beaconClientAssertCallback(char* errMsg){
	S32 sentStuff = 0;
	S32 done = 0;
	S32 printMenu = 1;
	
	beaconPrintf(COLOR_RED, "Crash:\n%s\n", errMsg);

	if(beacon_client.serverLink && !commIsMonitoring(beacon_comm))
	{
		linkRemove_wReason(&beacon_client.serverLink, "Asserted");
		commMonitor(beacon_comm);
	}

	if(beacon_client.masterLink && !commIsMonitoring(beacon_comm))
	{
		linkRemove_wReason(&beacon_client.masterLink, "Asserted");
		commMonitor(beacon_comm);
	}

	while(!done){
		Sleep(1000);
		
		beaconClientCheckSentry();
		
		if(!beacon_client.hPipeToSentry){
			sentStuff = 0;
		}else{
			if(!sentStuff){
				sentStuff = 1;
				
				printfPipe(0, "ProcessID %d\nCrash <<\n%s\n>>", _getpid(), errMsg);
			}
		}
		
		if(printMenu){
			printMenu = 0;
			
			beaconPrintf(COLOR_GREEN,
						"\n\nCrash menu:\n"
						"\n"
						"m    : Memory display.\n"
						"d    : Open crash dialog.\n"
						"\n"
						"Enter selection: ");
		}
#undef _kbhit
#undef _getch
		if(_kbhit()){
			switch(_getch()){
				xcase 0:
				case 224:{
					int nothing = _getch();
				}
				
				xcase 'm':{
					beaconPrintf(COLOR_YELLOW, "Memory stats...\n");
					beaconPrintMemory();
					printMenu = 1;
				}
				
				xcase 'd':{
					beaconPrintf(COLOR_YELLOW, "Showing window and opening assert dialog box...\n");
					ShowWindow(beaconGetConsoleWindow(), SW_RESTORE);
					done = 1;
				}
			}
		}
	}
}

static void beaconClientInstall(void){

	return;
}

static void beaconClientDetermineUserMachine(void)
{
	beacon_client.isUserMachine = !fileExists("c:\\server.txt") && stricmp(getUserName(),"crypticdmzuser");
}

//typedef void (*FolderCacheCallback)(const char *relpath, int when);
static void beaconConfigChangedCallback(const char *relpath, int when)
{
	char fullpath[MAX_PATH];

	// I'm assuming it'll be in fcbeaconizerpatch here as well
	sprintf(fullpath, "c:/fcbeaconizerpatch/%s", relpath);
	if(!stricmp("beacon.cfg", relpath))
	{
		ParserReadTextFile(fullpath, parse_BeaconSentryConfig, &beacon_client.config, 0);
	}
	else
	{
		//badness, but oh well
	}
}

//typedef void (*FolderCacheCallback)(const char *relpath, int when);
static void beaconPriExeChangedCallback(const char *relpath, int when)
{
	char fullpath[MAX_PATH];

	// I'm assuming it'll be in fcbeaconizerpatch here as well
	sprintf(fullpath, "c:/fcbeaconizerpatch/%s", relpath);
	if(!stricmp("hipri.cfg", relpath))
	{
		ParserReadTextFile(fullpath, parse_BeaconClientHighPriExes, &beacon_client.highproc, 0);
	}
	else
	{
		//badness, but oh well
	}
}

void beaconClientChangeMaster(void)
{
	beaconClientKillSentryProcesses(1, 1);

	linkRemove_wReason(&beacon_client.masterLink, "Master Server changed");
}

void beaconClientStartup(const char* masterServerName, const char* subServerName)
{
	pushDontReportErrorsToErrorTracker(true);
	
	beacon_client.foldercache = FolderCacheCreate();

	// Hide if not in production mode.
	
	if(!beaconIsProductionMode()){
		ShowWindow(beaconGetConsoleWindow(), hideClient ? SW_HIDE : SW_SHOW);
	}

	beaconClientDetermineUserMachine();

	sharedMemorySetMode(SMM_DISABLED);

	// Check production mode.
	
	printf("Production mode: ");
	
	if(beaconIsSharded()){
		U32 ips[2] = {0};
		
		beacon_client.workDuringUserActivity = 1;
		
		beaconPrintf(COLOR_GREEN, "YES\n");
	}else{
		beaconPrintf(COLOR_RED, "NO\n");
	}
	
	// Check master server.
	
	if(masterServerName){
		beacon_common.masterServerName = strdup(masterServerName);
		
		beaconPrintf(COLOR_GREEN, "Using master server: %s\n", masterServerName);
	}
	
	// Check sub server.
	
	if(subServerName){
		beacon_client.subServerName = strdup(subServerName);

		beaconPrintf(COLOR_GREEN, "Using sub server: %s\n", subServerName);
	}

	// Figure out if I'm the sentry.	
	if(beaconClientAcquireSentryMutex()){
		//SYSTEM_INFO si;
		
		SAFE_FREE(beacon_client.subServerName);
		
		// I'm the sentry!
		
		setWindowIconColoredLetter(beaconGetConsoleWindow(), 'S', 0x30ff30);
		
		//GetSystemInfo(&si);

		beacon_client.cpuMax = getNumRealCpus();

		if(beacon_client.cpuLimit > 0)
		{
			beacon_client.cpuCount = beacon_client.cpuLimit;
		}
		else
		{
			beacon_client.cpuCount = beacon_client.cpuMax;

			if(beacon_client.cpuLimit < 0)
				beacon_client.cpuCount += beacon_client.cpuLimit;

		}
		
		consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
		printf("I'm the sentry!!!  (%d CPU%s)\n", beacon_client.cpuCount, beacon_client.cpuCount > 1 ? "s" : "");
		consoleSetDefaultColor();
	}else{
		setWindowIconColoredLetter(beaconGetConsoleWindow(), 'W', 0x30ff30);

		consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
		printf("I'm not the sentry!!!\n");
		consoleSetDefaultColor();
	}
	
	if(beaconClientIsSentry())
	{
		ParserReadTextFile("c:/fcbeaconizerpatch/beacon.cfg", parse_BeaconSentryConfig, &beacon_client.config, 0);
		ParserReadTextFile("c:/fcbeaconizerpatch/hipri.cfg", parse_BeaconClientHighPriExes, &beacon_client.highproc, 0);

		FolderCacheAddFolder(beacon_client.foldercache, "c:/fcbeaconizerpatch/", 0, NULL, false);

		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "beacon.cfg", beaconConfigChangedCallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "hipri.cfg", beaconPriExeChangedCallback);
		FolderCacheSetManualCallbackMode(1);
	}

	beaconClientSetPriorityLevel(1);

	_beginthread(beaconClientWindowThread, 0, NULL);
	
	// Disable the assert dialog box for clients.
	
	if(	!beaconIsSharded() &&
		!beaconClientIsSentry())
	{
		setAssertMode(getAssertMode() | ASSERTMODE_NOERRORTRACKER);
		setAssertCallback(beaconClientAssertCallback);
	}

	beaconInitCommon();

	if(!beaconClientIsSentry())
	{
		beaconInitCommonWorld();
#if !PSDK_DISABLED
		psdkInit(1);
#endif
		beaconInitCommonPostWorld();
	}

	#if 0
	//beacon_process.entity = beaconCreatePathCheckEnt();
	#endif

	beacon_client.executableCRC = beaconGetExeCRC(beaconGetExeFileName(), NULL, NULL);
	
	printf("CRC of \"%s\" = 0x%8.8x\n", beaconGetExeFileName(), beacon_client.executableCRC);

	printf(	"\n\n[컴컴컴컴컴컴 BEACON %s RUNNING 컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴컴�]\n",
			beaconClientIsSentry() ? "SENTRY" : "CLIENT");

	beacon_client_conn.timeHeardFromServer = timerCpuTicks();

	if(beaconClientIsSentry())
	{
		//beacon_common.bdfunc(0);

		beacon_common.onMasterChangeFunc = beaconClientChangeMaster;
	}		
}

static void beaconClientCheckMaster(void)
{
	char serverName[1000];
	int port = BEACON_MASTER_SERVER_PORT;
	int ip;

	if(linkConnected(beacon_client.masterLink))
		return;

	if (!beacon_comm)
	{
		beacon_comm = commCreate(0,1);
		commSetSendTimeout(beacon_comm, 1000);
	}

	if(beacon_common.masterServerName){
		strcpy(serverName, beacon_common.masterServerName);
	}
	else if(!beaconIsSharded()){
		strcpy(serverName, BEACON_DEFAULT_SERVER);
	}
	else
		return;
	ip = ipLocalFromString(serverName);

	beaconClientUpdateTitle("Connecting (%s:%d)...", serverName, port);
	linkRemove(&beacon_client.masterLink);
	beacon_client.masterLink = commConnectIP(beacon_comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, ip, port, beaconClientHandleMsg, 0, 0, 0);
	if (linkConnectWait(&beacon_client.masterLink,beaconIsProductionMode() ? 3 : 30)) 
	{
		beacon_common.connectedToMasterOnce = true;

		beaconClientUpdateTitle("Connected!!!");
		beaconClientSendConnect(beacon_client.masterLink);

		beacon_client_conn.timeHeardFromServer = timerCpuTicks();
		beacon_client.timeOfLastCommand = timerCpuTicks();
	}
}

static void beaconClientConnectToServer(void){
	static S32 wasConnected = 0;
	
	char serverName[1000];
	char* portString;
	int ip;
	S32 port;
	
	if(linkConnected(beacon_client.serverLink)){
		return;
	}

	if (!beacon_comm)
	{
		beacon_comm = commCreate(0,1);
		commSetSendTimeout(beacon_comm, 1000);
	}
	
	beaconClientSetPriorityLevel(1);

	beaconClientSetServerID(0, "NoServer");

	beacon_client_conn.readyToWork = 0;
	beacon_client.sentSentryClients = 0;
	
	if(wasConnected){
		beaconResetReceivedMapData();
	}
	
	// Clear out the subserver name if necessary.
	
	if(beacon_client.connectedToSubServer){
		beacon_client.connectedToSubServer = 0;
		
		SAFE_FREE(beacon_client.subServerName);
	}
	
	// Get the server name.
	
	if(beacon_client.subServerName){
		strcpy(serverName, beacon_client.subServerName);
	}
	else {
		return;
	}
	ip = ipLocalFromString(serverName);
	
	// Get the port.
	if(portString = strstr(serverName, ":")){
		*portString++ = 0;
	}
	
	port = portString ? atoi(portString) : beacon_client.subServerPort;
	
	// Connect!

	beaconClientUpdateTitle("Connecting (%s:%d)...", serverName, port);
	linkRemove(&beacon_client.serverLink);
	beacon_client.serverLink = commConnectIP(beacon_comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, ip, port, beaconClientHandleMsg, 0, 0, 0);
	if (linkConnectWait(&beacon_client.serverLink,beaconIsProductionMode() ? 3 : 30)) // fixme2 newnet beaconClientConnectIdleCallback
	{
		wasConnected = 1;
		
		beacon_client.connectedToSubServer = 1;
		
		beaconClientUpdateTitle("Connected!!!");
		beaconClientSendConnect(beacon_client.serverLink);
		
		beacon_client_conn.timeHeardFromServer = timerCpuTicks();
		beacon_client.timeOfLastCommand = timerCpuTicks();
	}else{
		wasConnected = 0;

		SAFE_FREE(beacon_client.subServerName);
		
		beaconClientUpdateTitle("Connect failed!!!");
	}
}

static void beaconClientSendUnassign(void)
{
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_client.masterLink, BMSG_C2ST_UNASSIGN);
	BEACON_CLIENT_PACKET_SEND();
}

void beaconClientHandleSubServerDisconnect(void)
{
	ea32Clear(&beacon_client.connect.group.indices);
	ea32Clear(&beacon_client.connect.pipeline.indices);

	if(linkConnected(beacon_client.masterLink))
		beaconClientSendUnassign();
}

static void beaconClientMonitorConnection(void){
	if(beaconIsSharded()){
		commMonitor(beacon_comm);
	}
	
	if(linkConnected(beacon_client.masterLink)){
		F32 timeSince = timerSeconds(timerCpuTicks() - beacon_client.timeOfLastCommand);
		
		if(timeSince <= 10){
			beaconClientUpdateTitle("%1.1fs", timeSince);
		}else{
			beaconClientUpdateTitle("Idling");
			
			Sleep(300);
		}
		
		commMonitor(beacon_comm);
	}else{
		beaconClientUpdateTitle(NULL);
	}

	if(!linkConnected(beacon_client.serverLink) || linkDisconnected(beacon_client.serverLink))
	{
		linkRemove(&beacon_client.serverLink);

		beaconClientHandleSubServerDisconnect();
	}

	if(!linkConnected(beacon_client.masterLink) || linkDisconnected(beacon_client.masterLink))
	{
		linkRemove(&beacon_client.masterLink);

		//beaconClientHandleDisconnect();
	}
}

#if 0
static S32 showOptionsWindow()
{
	SUIMainWindow* mw;
	SUIWindow* w;
	
	if(suiMainWindowCreate(&mw, "Test")){
		if(suiWindowCreate(&w, NULL, NULL)){
			suiAddMainChildWindow(&mw, &w);
			suiSetWindowPos(&w, 10, 10);
			suiSetWindowSize(&w, 500, 300);
		}
		
		if(suiWindowCreate(&w, NULL, NULL)){
			suiAddMainChildWindow(&mw, &w);
			suiSetWindowPos(&w, 300, 250);
			suiSetWindowSize(&w, 400, 600);
		}

		if(suiWindowCreate(&w, NULL, NULL)){
			suiAddMainChildWindow(&mw, &w); 
			suiSetWindowPos(&w, 150, 50);
			suiSetWindowSize(&w, 700, 400);
		}
	}

	suiProcessUntilDone(&mw);
	
	return 1;
}
#endif

void beaconClientSetMMMovementCallbacks(BeaconConnMovementStartCallback start,
										BeaconConnMovementIsFinishedCallback isfinished,
										BeaconConnMovementResultCallback result)
{
	beaconConnSetMovementStartCallback(start);
	beaconConnSetMovementIsFinishedCallback(isfinished);
	beaconConnSetMovementResultCallback(result);
}

static void beaconSentrySelfUpdateCB(PCL_Client * client, PCL_ErrorCode error, const char * error_details, void * userData)
{
	if(error)
	{
		beaconPrintf(COLOR_RED, "\nSentry patch failed: %d\n", error);
	}
	else
	{
		bool restart;
		beaconPrintf(COLOR_GREEN, "\nSentry patch completed successfully\n");

		beacon_common.nrfunc(beacon_client.pcl_sentry, &restart);
		if(restart)
		{
			STARTUPINFO sinfo = {0};
			PROCESS_INFORMATION pinfo = {0};
			char patchexe[] = "C:\\FCBeaconizerPatch\\Beaconizer.exe";
			char patchargs[MAX_PATH];
			char patchpath[] = "C:\\FCBeaconizerPatch";
			int i;
			int ret;

			sprintf(patchargs, "%s", beaconClientMyArgs());

			beaconClientReleaseSentryMutex();

			ret = CreateProcess_UTF8(patchexe, 
								patchargs, 
								NULL, 
								NULL, 
								0, 
								NORMAL_PRIORITY_CLASS | CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE, 
								NULL, 
								patchpath, 
								&sinfo, 
								&pinfo);

			if(ret)
			{
				CloseHandle(pinfo.hProcess);
				CloseHandle(pinfo.hThread);
			}

			printf("Closing in...");
			for(i=5; i>=1; i--)
			{
				printf("%d...", i);

				Sleep(1000);
			}

			printf("Goodbye\n");

			exit(0);
		}
		else
		{
			beacon_client.needsSentryUpdate = 1;
			beacon_client.destroySentryPCL = 1;
		}
	}
}

static void beaconSentryStartSelfPatch(void)
{
	beaconClientKillSentryProcesses(1, 1);

	beacon_common.ccfunc(	&beacon_client.pcl_sentry, 
							beacon_client.patchServerName ? 
								beacon_client.patchServerName : 
								BEACON_DEFAULT_PATCHSERVER,
							BEACON_PATCH_SERVER_PORT, 
							300,
							beacon_comm,
							beaconGetExeDirectory(),
							NULL,
							NULL,
							NULL,
							NULL);

	// Set view to current project and branch number and time 0 because servertime is authoritative and unknown
	commMonitor(beacon_comm);
	while(beacon_common.pfunc(beacon_client.pcl_sentry))
	{
		commMonitor(beacon_comm);
	}
	beacon_common.svfunc(	beacon_client.pcl_sentry,
							"Sentry",
							1,
							NULL,
							true,
							true,
							NULL,
							NULL);

	commMonitor(beacon_comm);
	while(beacon_common.pfunc(beacon_client.pcl_sentry))
	{
		commMonitor(beacon_comm);
	}

	// Get files
	beacon_common.gafunc(beacon_client.pcl_sentry, beaconSentrySelfUpdateCB, NULL, NULL);
	beacon_common.spfunc(beacon_client.pcl_sentry, beaconSentryPatchProcess, NULL);

	commMonitor(beacon_comm);
}

void beaconClientSetPatchTime(char *patchTime)
{
	strcpy(beacon_client.patchTime, patchTime);
}

U32 beaconClientCheckPatchState()
{
	if(!beaconClientIsSentry() || beaconIsSharded())
	{
		return 1;
	}

	if(!beacon_client.noPatchSync && beacon_client.userInactive)
	{
		if(beacon_client.needsSentryUpdate)
		{
			if(beacon_client.pcl_sentry)
			{
				beacon_common.pfunc(beacon_client.pcl_sentry);
			}
			else
			{
				beaconSentryStartSelfPatch();
			}
		}

		if(beacon_client.destroySentryPCL)
		{
			beacon_common.ddfunc(beacon_client.pcl_sentry);
			beacon_client.pcl_sentry = NULL;
		}

		return beacon_client.needsSentryUpdate;
	}

	return 0;
}

void beaconClientOncePerFrame(void)
{
	if(isOSShuttingDown())
		exit(0);

	if(beaconDoShardStuff())
		DirectlyInformControllerOfState("bcnSentryRunning");
	
	FolderCacheDoCallbacks();

	beaconClientConnectIdleCallback(BEACONCLIENT_PARTITION, 0);
	
	beaconClientCheckMaster();

	beaconClientConnectToServer();

	beaconClientConnectBeacons();

	if(beaconClientIsSentry() && !beaconCommonCheckMasterName())
		return;
	
	beaconClientMonitorConnection();

	if(beaconIsSharded() && !beaconClientIsSentry() && !beacon_client.hPipeToSentry)
	{
		printfColor(COLOR_RED, "No pipe to sentry.  Killing self.");
		exit(-1);
	}

	if(beaconClientIsSentry())
	{
		if(!beaconClientCheckPatchState())
		{
			Sleep(250);
		}
	}
	else
	{
		if(!beacon_client.connectedToSubServer)
		{
			Sleep(250);
		}
		else
		{
			Sleep(0);
		}
	}
}

char* beaconClientMyArgs(void)
{
	static char *args = NULL;

	if(!args)
		estrCreate(&args);
	else
		estrClear(&args);

	estrPrintf(&args, "Beaconizer.exe -beaconClient");

	if(beacon_common.masterServerName && beacon_common.masterServerName[0] && !strstri(args, "-beaconmasterserver"))
		estrConcatf(&args, " -useMasterC %s", beacon_common.masterServerName);

	if(beacon_common.isSharded)
		estrConcatf(&args, " -bcSharded");

	if(!hideClient)
		estrConcatf(&args, " -bcHide 0");

	if (sharedMemoryGetMode() == SMM_DISABLED)
		estrConcatf(&args, " -NoSharedMemory");

	return args;
}

int beaconClientRunExe(PROCESS_INFORMATION *pi)
{
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pinfo = {0};

	if(!pi)
		pi = &pinfo;
	else
		ZeroStruct(pi);

	si.dwFlags = STARTF_USESHOWWINDOW;
	if(beaconIsProductionMode())
		si.wShowWindow = SW_SHOWMINNOACTIVE;
	else
		si.wShowWindow = SW_SHOWNOACTIVATE;

	return CreateProcess_UTF8(	beaconGetExeFileName(), 
							beaconClientMyArgs(), 
							NULL, 
							NULL, 
							0, 
							CREATE_NEW_PROCESS_GROUP | CREATE_NEW_CONSOLE, 
							NULL, 
							NULL, 
							&si, 
							pi);
}

#endif

char *beaconClientGetMapname(void)
{
#if !PLATFORM_CONSOLE
	return beacon_client.active_map;
#else
	return NULL;
#endif
}

void beaconClientSetMapName(char *name)
{
#if !PLATFORM_CONSOLE
	SAFE_FREE(beacon_client.active_map);

	beacon_client.active_map = strdup(name);
#endif
}

BeaconProcessDebugState* beaconClientGetProcessDebugState(void)
{
	return beacon_client.debug_state;
}

S32	beaconClientDebugSendWalkResults(void)
{
	return beacon_client.debug_state && beacon_client.debug_state->send_walk_res;
}

NetLink* beaconClientGetServerLink(void)
{
	return beacon_client.serverLink;
}

WorldColl* beaconClientGetWorldColl(void)
{
#if !PLATFORM_CONSOLE
	return beacon_client.world_coll;
#else
	return NULL;
#endif
}

NetLink* beaconClientGetLink(void)
{
#if !PLATFORM_CONSOLE
	return beacon_client.serverLink;
#else
	return NULL;
#endif
}

#if !PLATFORM_CONSOLE

AUTO_COMMAND ACMD_NAME(beaconUseLocal) ACMD_HIDE;
void beaconClientUseLocalData(int d)
{
	beacon_client.useLocalData = !!d;
	beacon_client.noPatchSync = !!d;
}

AUTO_COMMAND ACMD_NAME(noPatchSync) ACMD_HIDE;
void beaconClientDisablePatch(int d)
{
	beacon_client.noPatchSync = !!d;
}

AUTO_COMMAND ACMD_NAME(bcUseGimme) ACMD_HIDE;
void beaconClientUseGimme(int d)
{
	beacon_client.useGimme = !!d;
}

AUTO_COMMAND ACMD_NAME(useMasterC) ACMD_HIDE;
void beaconClientUseMasterServer(char* mastername)
{
	beacon_common.masterServerName = strdup(mastername);
}

AUTO_COMMAND ACMD_NAME(usePatchC) ACMD_HIDE;
void beaconClientUsePatchServer(char *patchname)
{
	beacon_client.patchServerName = strdup(patchname);
}

AUTO_COMMAND ACMD_NAME(bcCPULimit) ACMD_HIDE;
void beaconClientUseCPULimit(int limit)
{
	beacon_client.cpuLimit = limit;
}

AUTO_COMMAND ACMD_NAME(bcHide) ACMD_HIDE;
void beaconClientHideClient(int hide)
{
	hideClient = !!hide;
}

AUTO_COMMAND ACMD_NAME(bcIgnoreInputs) ACMD_HIDE;
void beaconClientIgnoreInputs(int unused)
{
	beacon_client.ignoreInputs = 1;
}
#endif

#include "beaconClient_h_ast.c"
#include "beaconClientServerPrivate_h_ast.c"