#include "beaconClientServerPrivate.h"
#include "beaconServerPrivate.h"

//#include "baseserver.h"
//#include "comm_backend.h"
#include "winutil.h"
#include "gimmeDLLWrapper.h"
#include "fileutil2.h"
#include "WorldGrid.h"
#include "StringCache.h"
#include "net/netpacketutil.h"
#include "net/netsmtp.h"
#include "sock.h"
#include "MemoryPool.h"
#include "SharedMemory.h"
#include "worldlib.h"
#include "utilitiesLib.h"
#include "AutoStartupSupport.h"
#include "BeaconClientServerPrivate_h_ast.h"
#include "ControllerLink.h"
#include "logging.h"
#include "wlCommandParse.h"
#include "PhysicsSDK.h"
#include "GenericMesh.h"
#include "qsortG.h"
#include "rand.h"
#include "Organization.h"
#include "structNet.h"
#include "bounds.h"
#include "wlVolumes.h"

#include "wlBeacon_h_ast.h"
#include "beaconFile_h_ast.h"
#include "beaconServer_c_ast.h"
#include "beaconServerPrivate_h_ast.h"
#include "UTF8.h"

AUTO_STRUCT;
typedef struct BeaconServerHistory
{
	const char* machineName;				AST(NAME(MachineName))
	int branch;
	int lastSeen;

	BeaconServerClientData **current;		NO_AST
} BeaconServerHistory;

AUTO_STRUCT;
typedef struct BeaconProject
{
	const char* projName;		// E.g., Fightclub, Star Trek, etc.

	BeaconServerHistory** histories;
} BeaconProject;

static StashTable g_BeaconProjects;

static WorldVolumeEntry **playableEnts = 0;
extern NetComm *beacon_comm;

#if !PLATFORM_CONSOLE
#define BEACON_SERVER_PROTOCOL_VERSION	(3)
#define BEACON_SERVER_CLIENT_PROTOCOL_VERSION (1)
#define BEACON_MASTER_SERVER_ICON_COLOR	(0xffff00)
#define BEACON_SERVER_ICON_COLOR		(0xff8000)
#define BEACON_SPAWN_OFFSET				5

#define BEACON_OPTIONAL_DIST_MULT		3

BeaconServer beacon_server;

static BeaconServerClientData** availableClients;
NetLink *g_debugger_link;

static const char* beaconServerExeName = "BeaconServer.exe";

static void beaconServerUpdateData(void);
void beaconServerPCLWait(PCL_Client *client);
void beaconServerCreateSpaceBeacons(void);
BeaconServerMachineData* beaconServerFindMachine(NetLink *link, int create);
void beaconServerMachineDelClient(BeaconServerMachineData *machine, BeaconServerClientData *client);
void beaconServerUnassignClients(BeaconServerClientData *server);
void beaconServerSendMapCompleted(void);

static BeaconProject* beaconMasterGetProject(const char* name)
{
	BeaconProject *proj = NULL;
	if(!g_BeaconProjects)
		g_BeaconProjects = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);

	if(!stashFindPointer(g_BeaconProjects, name, &proj))
	{
		proj = callocStruct(BeaconProject);

		proj->projName = strdup(name);
		stashAddPointer(g_BeaconProjects, name, proj, true);
	}

	return proj;
}

static BeaconServerHistory* beaconProjectGetHistory(BeaconProject *proj, int branch)
{
	BeaconServerHistory *hist = NULL;

	FOR_EACH_IN_EARRAY(proj->histories, BeaconServerHistory, histSearch)
	{
		if(histSearch->branch == branch)
		{
			hist = histSearch;
			break;
		}
	}
	FOR_EACH_END

		if(!hist)
		{
			hist = callocStruct(BeaconServerHistory);
			hist->branch = branch;

			eaPush(&proj->histories, hist);
		}

		return hist;
}

char* beaconServerGetMapName()
{
	static char mapname[MAX_PATH];
	char *zonemap;

	strcpy(mapname, zmapGetFilename(NULL));
	zonemap = strrchr(mapname, '.');
	if(zonemap)
	{
		zonemap[0] = '\0';

		zonemap = strrchr(mapname, '/');
		if(zonemap)
		{
			zonemap[0] = 0;
			return zonemap+1;
		}
	}

	return mapname;
}

char* beaconServerGetPatchTime(void)
{
	return beacon_server.patchTime;
}

void beaconServerSetDataToolsRootPath(const char* dataToolsRootPath){
	//bool addAppropriateDataDirs(const char *path);
	//
	//addAppropriateDataDirs(dataToolsRootPath);
					
	estrCopy2(&beacon_server.dataToolsRootPath, dataToolsRootPath);

	forwardSlashes(beacon_server.dataToolsRootPath);

	// Remove trailing slashes.

	while(	beacon_server.dataToolsRootPath[0] &&
			strEndsWith(beacon_server.dataToolsRootPath, "/"))
	{
		estrRemove(	&beacon_server.dataToolsRootPath,
					estrLength(&beacon_server.dataToolsRootPath) - 1,
					1);
	}
}

const char* beaconServerGetDataToolsRootPath(void){
	if(!beacon_server.dataToolsRootPath)
	{
		estrPrintf(&beacon_server.dataToolsRootPath, "%s", fileBaseDir());
	}
	return beacon_server.dataToolsRootPath ? beacon_server.dataToolsRootPath : "c:\\fightclub";
}

const char* beaconServerGetDataPath(void){
	if(!beacon_server.dataPath){
		estrPrintf(&beacon_server.dataPath, "%s\\data", beaconServerGetDataToolsRootPath());
	}

	return beacon_server.dataPath;
}

const char* beaconServerGetToolsPath(void){
	if(!beacon_server.dataPath){
		estrPrintf(&beacon_server.dataPath, "%s\\tools", beaconServerGetDataToolsRootPath());
	}

	return beacon_server.dataPath;
}

void beaconServerSetClientState(BeaconServerClientData* client, BeaconClientState state){
	if(client->state != state){
		BeaconProcessPhase phase;

		if(!beacon_server.isMasterServer && beacon_process.mapMetaData && client->stateBeginTime)
		{
			switch(client->state) 
			{
				xcase BCS_READY_TO_CONNECT_BEACONS:
				acase BCS_PIPELINE_CONNECT_BEACONS: 
				acase BCS_CONNECTING_BEACONS: {
					phase = BPP_CONNECT;
				}
				xcase BCS_READY_TO_GENERATE:
				acase BCS_GENERATING: {
					phase = BPP_GENERATE;
				}
				xcase BCS_NOT_CONNECTED: {
					phase = -1;
				}
				xdefault: {
					phase = BPP_RECV_LOAD_MAP;
				}
			}

			if(phase>=0)
				beacon_server.clientTicks[phase] += timerCpuTicks64() - client->stateBeginTicks;
		}

		client->state = state;
		client->stateBeginTime = beaconGetCurTime();		
		client->stateBeginTicks = timerCpuTicks64();
	}
}

static void beaconServerSetClientType(BeaconServerClientData* client, BeaconClientType clientType){
	if(client->clientType != clientType){
		BeaconServerClientList* clientList;
		S32 i;

		if(client->clientType > BCT_NOT_SET && client->clientType < BCT_COUNT){
			clientList = beacon_server.clientList + client->clientType;

			for(i = 0; i < clientList->count; i++){
				if(clientList->clients[i] == client){
					clientList->count--;
					CopyStructsFromOffset(clientList->clients + i, 1, clientList->count - i);
					break;
				}
			}

			assert(i <= clientList->count);
		}

		client->clientType = clientType;

		if(clientType > BCT_NOT_SET && clientType < BCT_COUNT){
			clientList = beacon_server.clientList + clientType;

			dynArrayAddp(clientList->clients, clientList->count, clientList->maxCount, client);
		}
	}
}

static char* getClientIPStr(BeaconServerClientData* client){
	return client->clientIPStr;
}

static void beaconClientVprintf(BeaconServerClientData* client, S32 color, const char* format, va_list argptr){
	char socket[50];
	consoleSetColor(COLOR_RED, 0);
	printf("%s ", beaconCurTimeString(0));
	switch(client->clientType){
		xcase BCT_SENTRY:
			consoleSetColor(COLOR_BRIGHT|COLOR_YELLOW, 0);
		xcase BCT_REQUESTER:
			consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
		xdefault:
			consoleSetColor(COLOR_BRIGHT|COLOR_GREEN|COLOR_BLUE, 0);
	}

	if(linkGetSocket(client->link)>=0)
		sprintf(socket, "%4d", linkGetSocket(client->link));
	else
		sprintf(socket, "dced");

	printf("%-15s-%s", getClientIPStr(client), socket);
	consoleSetDefaultColor();
	printf(" : ");
	if(color){
		consoleSetColor(color, 0);
	}
	vprintf(format, argptr);
	consoleSetDefaultColor();
}

void beaconClientPrintf(BeaconServerClientData* client, S32 color, const char* format, ...){
	va_list argptr;

	if(	/*beaconIsSharded() &&
		strnicmp(format, "WARNING:", 8) &&
		strnicmp(format, "ERROR:", 6)*/ 0)
	{
		return;
	}

	va_start(argptr, format);
	beaconClientVprintf(client, COLOR_BRIGHT|color, format, argptr);
	va_end(argptr);
}

static void beaconClientPrintfDim(BeaconServerClientData* client, S32 color, const char* format, ...){
	va_list argptr;

	va_start(argptr, format);
	beaconClientVprintf(client, color, format, argptr);
	va_end(argptr);
}

static HANDLE	hConsoleBack;
static HANDLE	hConsoleMain;
static S32		backConsoleRefCount;

static void beaconEnableBackgroundConsole(void){
	if(!hConsoleMain){
		CONSOLE_CURSOR_INFO info = {0};
		COORD backSize = { 200, 9999 };

		hConsoleMain = GetStdHandle(STD_OUTPUT_HANDLE);

		hConsoleBack = CreateConsoleScreenBuffer(GENERIC_READ|GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);

		info.dwSize = 1;
		info.bVisible = 0;
		SetConsoleCursorInfo(hConsoleBack, &info);

		SetConsoleScreenBufferSize(hConsoleBack, backSize);
	}

	if(!backConsoleRefCount++){
		SetConsoleActiveScreenBuffer(hConsoleBack);

		SetStdHandle(STD_OUTPUT_HANDLE, hConsoleBack);
	}
}

static void beaconDisableBackgroundConsole(void){
	assert(backConsoleRefCount > 0);

	if(!--backConsoleRefCount){
		SetConsoleActiveScreenBuffer(hConsoleMain);

		SetStdHandle(STD_OUTPUT_HANDLE, hConsoleMain);
	}
}

static void beaconConsoleVprintf(U32 color, const char* format, va_list argptr){
	char buffer[1000];
	S32 length;
	S32 outLength;

	length = _vsnprintf(buffer, 1000, format, argptr);
	beaconEnableBackgroundConsole();
	if(color){
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
	}
	WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, &outLength, NULL);
	if(color){
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), COLOR_WHITE);
	}
	beaconDisableBackgroundConsole();
}

static void beaconConsolePrintf(U32 color, const char* format, ...){
	va_list argptr;

	va_start(argptr, format);
	beaconConsoleVprintf(color | COLOR_BRIGHT, format, argptr);
	va_end(argptr);
}

static void beaconConsolePrintfDim(U32 color, const char* format, ...){
	va_list argptr;

	va_start(argptr, format);
	beaconConsoleVprintf(color & ~COLOR_BRIGHT, format, argptr);
	va_end(argptr);
}

static void beaconServerSetSendStatus(void){
	if(!beacon_server.status.send){
		beacon_server.status.send = 1;
		beacon_server.status.acked = 0;
		beacon_server.status.sendUID++;
		beacon_server.status.timeSent = 0;
	}
}

static void removeClientFromBlock(BeaconServerClientData* client){
	if(client->assigned.block){
		BeaconDiskSwapBlock* block = client->assigned.block;
		S32 i;

		for(i = 0; i < block->clients.count; i++){
			if(block->clients.clients[i] == client){
				block->clients.count--;
				block->clients.clients[i] = block->clients.clients[block->clients.count];
				block->clients.clients[block->clients.count] = NULL;
				client->assigned.block = NULL;
				break;
			}
		}

		assert(i <= block->clients.count);
	}
}

static void addClientToGroup(BeaconConnectBeaconGroup* group, BeaconServerClientData* client){
	if(!eaSize(&group->clients.clients)){
		group->clients.assignedTime = timerCpuTicks();
	}
	eaPushUnique(&group->clients.clients, client);
}

static void beaconServerAssignGroup(BeaconServerClientData *client, BeaconConnectBeaconGroup *group, const char* file, int line)
{
	if(group)
		devassert(!client->assigned.group || client->assigned.group == group);

	if(client->assigned.group!=group)
	{ 
		filelog_printf("beaconassign.txt", "G - %s:%d (%p) %p->%p %d-%d -> %d-%d %s:%d", 
											client->computerName, 
											linkGetPort(client->link), 
											client,
											client->assigned.group,
											group, 
											client->assigned.group ? client->assigned.group->lo : -1,
											client->assigned.group ? client->assigned.group->hi : -1,
											group ? group->lo : -1,
											group ? group->hi : -1,
											file, 
											line);

		if(group)
			addClientToGroup(group, client);
		client->assigned.group = group;
	}
}

static void beaconServerAssignPipeline(BeaconServerClientData *client, BeaconConnectBeaconGroup *pipeline, const char* file, int line)
{
	if(pipeline)
		devassert(!client->assigned.pipeline || client->assigned.pipeline == pipeline);

	if(pipeline!=client->assigned.pipeline)
	{
		filelog_printf("beaconassign.txt", "P - %s:%d (%p) %p->%p %d-%d -> %d-%d %s:%d", 
											client->computerName, 
											linkGetPort(client->link), 
											client,
											client->assigned.pipeline,
											pipeline, 
											client->assigned.pipeline ? client->assigned.pipeline->lo : -1,
											client->assigned.pipeline ? client->assigned.pipeline->hi : -1,
											pipeline ? pipeline->lo : -1,
											pipeline ? pipeline->hi : -1,
											file,
											line);

		if(pipeline)
			addClientToGroup(pipeline, client);
		client->assigned.pipeline = pipeline;
	}
}

static void beaconServerRemoveClientFromConnectGroup(BeaconServerClientData *client, BeaconConnectBeaconGroup *group, int print)
{
	S32 idx;

	if(!group)
		return;

	if(print)
		beaconClientPrintf(client, COLOR_YELLOW, "Removing from group %6d-%6d (%s)\n", group->lo, group->hi, client->assigned.group==group ? "g" : "p");

	assert((client->assigned.group==group) ^ (client->assigned.pipeline==group));

	idx = eaFindAndRemoveFast(&group->clients.clients, client);
	assertmsg(idx!=-1, "Group unaware of client assignment");

	if(client->assigned.group==group)
		beaconServerAssignGroup(client, NULL, __FILE__, __LINE__);
	else if(client->assigned.pipeline==group)
		beaconServerAssignPipeline(client, NULL, __FILE__, __LINE__);
	

	if(!group->finished && !eaSize(&group->clients.clients)){
		group->next = beacon_server.beaconConnect.groups.availableHead;
		beacon_server.beaconConnect.groups.availableHead = group;
		if(!beacon_server.beaconConnect.groups.availableTail){
			assert(!group->next);
			beacon_server.beaconConnect.groups.availableTail = group;
		}
	}
}

static void removeClientFromGroup(BeaconServerClientData* client){
	if(client->assigned.group || client->assigned.pipeline){	
		beaconServerRemoveClientFromConnectGroup(client, client->assigned.group, false);
		beaconServerRemoveClientFromConnectGroup(client, client->assigned.pipeline, false);
	}
}

static void printClients(char* prefix, S32 line, BeaconServerClientData* sentry){
	return;
}

#define FOR_CLIENTS_LINK(i, clientVar, linkVar)							\
	{																	\
	S32 i;																\
	for(i = 0; i < listenCount(beacon_server.clients); i++){				\
		NetLink* linkVar = beacon_server.clients->links[i];		\
		BeaconServerClientData* clientVar = linkVar->user_data;

#define FOR_CLIENTS(i, clientVar)										\
	FOR_CLIENTS_LINK(i, clientVar, linkVar__)

#define END_FOR }}

static const char* getClientTypeName(BeaconServerClientData* client){
	switch(client->clientType){
		xcase BCT_WORKER:
			return "WORKER";
		xcase BCT_SENTRY:
			return "SENTRY";
		xcase BCT_SERVER:
			return "SERVER";
		xcase BCT_REQUESTER:
			return "REQUESTER";
		xdefault:
			return "NO_CLIENT_TYPE";
	}
}

static Packet* createServerToClientPacket(BeaconServerClientData* client, const char* textCmd, const char* file, int line){
	Packet* pak;

	assert(client->link);
	pak = pktCreate_dbg(client->link, BMSG_S2C_TEXT_CMD, NULL, file, line);
	pktSendString(pak, textCmd);

	return pak;
}

static Packet* createServerToClientGalaxyGroupCountPacket(BeaconServerClientData* client, const char* file, int line)
{
	Packet* pak;

	assert(client->link);
	pak = pktCreate_dbg(client->link, BMSG_SET_GALAXY_GROUP_COUNT_CMD, NULL, file, line);
	beaconPrintf(COLOR_BLUE, "Sending Galaxy Group Count: %d\n", gConf.uBeaconizerJumpHeight);
	pktSendBits(pak, 32, gConf.uBeaconizerJumpHeight);

	return pak;
}

static void sendServerToClientPacket(BeaconServerClientData* client, Packet** pak){
	pktSend(pak);
}

// Macros to create server-to-client packets.

#define BEACON_SERVER_PACKET_CREATE_BASE(pak, client, textCmd){										\
	Packet* pak;																					\
	Packet* serverPacket__ = pak = createServerToClientPacket(client, textCmd, __FILE__, __LINE__);	\
	BeaconServerClientData* serverPacketClient__ = client
#define BEACON_SERVER_PACKET_CREATE(textCmd)									\
	BEACON_SERVER_PACKET_CREATE_BASE(pak, client, textCmd)
#define BEACON_SERVER_PACKET_SEND()												\
	sendServerToClientPacket(serverPacketClient__, &serverPacket__);}
#define BEACON_SERVER_PACKET_SEND_WAIT(client)									\
	sendServerToClientPacket(serverPacketClient__, &serverPacket__);			\
	linkFlush(client->link);													\
	while(linkPendingSends(client->link)); }

#define BEACON_SERVER_PACKET_CREATE_VS(client, textCmd)							\
	createServerToClientPacket(client, textCmd, __FILE__, __LINE__)
#define BEACON_SERVER_PACKET_SEND_WAIT_VS(pak, client)							\
	sendServerToClientPacket(client, &pak);										\
	linkFlush(client->link);													\
	while(linkPendingSends(client->link));

static void beaconServerSendLoadMapReply(BeaconServerClientData* client, S32 good){
	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_MAP_DATA_LOADED_REPLY);
		pktSendBits(pak, 1, good ? 1 : 0);
		if(good)
		{
			pktSendStruct(pak, beaconServerGetProcessConfig(), parse_BeaconProcessConfig);
		}
	BEACON_SERVER_PACKET_SEND();
}

static void beaconServerSendMapDataToRequestServer(BeaconServerClientData* client){
	BeaconProcessQueueNode* node = client->requestServer.processNode;

	if(	!client->server.isRequestServer ||
		!node ||
		!beaconMapDataPacketIsFullyReceived(node->mapData) ||
		beaconMapDataPacketIsFullySent(	node->mapData,
										client->requestServer.sentByteCount))
	{
		return;
	}

	linkCompress(client->link, 0);

	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_MAP_DATA);

		pktSendBitsPack(pak, 1, node->uid);
		pktSendString(pak, node->uniqueStorageName);

		pktSendBits(pak, 1, client->requestServer.sentByteCount ? 0 : 1);

		beaconMapDataPacketSendChunk(	pak,
										node->mapData,
										&client->requestServer.sentByteCount);

	BEACON_SERVER_PACKET_SEND();

	linkCompress(client->link, 1);
}

void beaconServerAssignProcessNodeToRequestServer(	BeaconProcessQueueNode* processNode,
												BeaconServerClientData* requestServer)
{
	BeaconServerClientData* oldRequestServer;

	if(!processNode){
		return;
	}

	oldRequestServer = processNode->requestServer;

	// Clear out the old request server information.

	if(oldRequestServer){
		assert(oldRequestServer->requestServer.processNode == processNode);
		oldRequestServer->requestServer.processNode = NULL;
		processNode->requestServer = NULL;
	}

	processNode->requestServer = requestServer;

	// Set the new request server information.

	if(requestServer){
		beaconServerAssignProcessNodeToRequestServer(requestServer->requestServer.processNode, NULL);

		assert(!requestServer->requestServer.processNode);

		requestServer->requestServer.processNode = processNode;
		requestServer->requestServer.sentByteCount = 0;

		beaconServerSendMapDataToRequestServer(requestServer);
	}
}

static U32 freshCRC(void* dataParam, U32 length){
	cryptAdler32Init();

	return cryptAdler32((U8*)dataParam, length);
}

void beaconServerSetRequestCacheDir(const char* cacheDir){
	estrCopy2(&beacon_server.requestCacheDir, cacheDir);

	forwardSlashes(beacon_server.requestCacheDir);

	while(	estrLength(&beacon_server.requestCacheDir) &&
			beacon_server.requestCacheDir[estrLength(&beacon_server.requestCacheDir) - 1] == '/')
	{
		estrSetSize(&beacon_server.requestCacheDir, estrLength(&beacon_server.requestCacheDir) - 1);
	}
}

void beaconServerSetSymStore(void){
	beacon_server.symStore = 1;
}

static S32 beaconServerConnectCallback(NetLink* link, BeaconServerClientData*	client)
{
	beaconServerSetSendStatus();

	client->link = link;
	client->connectTime = beaconGetCurTime();
	client->uid = beacon_server.nextClientUID++;

	client->clientIPStr = (char*)malloc(50);
	linkGetIpStr(link, client->clientIPStr, 50);

	if(	!beacon_server.localOnly ||
		!stricmp(beaconGetLinkIPStr(link), makeIpStr(getHostLocalIp())))
	{
		beaconServerSetClientState(client, BCS_NOT_CONNECTED);
	}else{
		beaconServerSetClientState(client, BCS_ERROR_NONLOCAL_IP);
	}

	if(!beacon_server.minimalPrinting)
	{
		beaconClientPrintf(	client,
							client->state == BCS_NOT_CONNECTED ? COLOR_GREEN : COLOR_RED,
							"NetLink connected!%s\n",
							client->state == BCS_ERROR_NONLOCAL_IP ? "  Non-local IP!" : "");
	}

	if (!beacon_server.isMasterServer)
	{
		Packet *pkt = createServerToClientGalaxyGroupCountPacket(client, __FILE__, __LINE__);
		pktSend(&pkt);
		pktFree(&pkt);
	}

	return 1;
}

static int beaconServerDisconnectCallbackSelectedHelper(NetLink* link, int index, void* link_data, void* func_data)
{
	S32 i = linkID(link);
	NetLink *curLink = func_data;
	
	if(link == curLink)
	{
		if(i < beacon_server.selectedClient)
		{
			beacon_server.selectedClient--;
			return 0;
		}
	}
	return 1;
}

static void beaconMasterRemoveFromProject(BeaconServerClientData *client)
{
	BeaconProject *proj = NULL;
	BeaconServerHistory *hist = NULL;

	if(client->clientType!=BCT_SERVER)
		return;

	if(client->server.protocolVersion<3)
	{
		proj = beaconMasterGetProject("Protocol < 3");
		if(!proj)
			return;
		hist = beaconProjectGetHistory(proj, -1);
		if(!hist)
			return;
	}
	else
	{
		proj = beaconMasterGetProject(client->server.status.project);
		if(!proj)
			return;

		hist = beaconProjectGetHistory(proj, client->server.status.branch);
		if(!hist)
			return;
	}

	hist->lastSeen = timeSecondsSince2000();
	eaFindAndRemove(&hist->current, client);
}

static S32 beaconServerDisconnectCallback(NetLink* link, BeaconServerClientData* client){
	S32 isUpdating = client->state == BCS_NEEDS_MORE_EXE_DATA ||
					 client->state == BCS_RECEIVING_EXE_DATA;
	BeaconServerMachineData *machine = beaconServerFindMachine(link, false);

	if(machine)
		beaconServerMachineDelClient(machine, client);

	beaconMasterRemoveFromProject(client);

	beaconServerUnassignClients(client);
	eaDestroy(&client->server.clients);

	beaconServerSetSendStatus();

	if(!beacon_server.minimalPrinting)
	{
		beaconClientPrintf(	client,
							COLOR_RED,
							"%s%s Disconnected! (%s/%s)\n",
							isUpdating ? "UPDATING " : "",
							getClientTypeName(client),
							client->computerName ? client->computerName : "...",
							client->userName ? client->userName : "...");
	}

	// Free client processing data.

	removeClientFromBlock(client);

	removeClientFromGroup(client);

	SAFE_FREE(client->userName);
	SAFE_FREE(client->computerName);
	
	// Free the server data.

	estrDestroy(&client->server.mapName);

	// Free the requester data.

	SAFE_FREE(client->requester.dbServerIP);

	beaconServerDetachClientFromLoadRequest(client);

	beaconServerDetachRequesterFromProcessNode(client);

	assert(!client->requester.loadRequest);
	assert(!client->requester.processNode);

	// Free the request server data.

	if(client->requestServer.processNode){
		beaconServerAssignProcessNodeToRequestServer(client->requestServer.processNode, NULL);
	}

	// Reset the client type.
	beaconServerSetClientType(client, BCT_NOT_SET);

	return 1;
}

static void beaconServerSendConnectReply(BeaconServerClientData* client, S32 good, S32 sendExeData){
	Packet* pak = pktCreate(client->link, BMSG_S2C_CONNECT_REPLY);

	if(good){
		pktSendBits(pak, 1, 1);
		pktSendBits(pak, 32, client->uid);
		pktSendString(pak, beacon_server.serverUID);

		if(client->protocolVersion>=5)
			pktSendBitsAuto(pak, BEACON_SERVER_CLIENT_PROTOCOL_VERSION);
	}else{
		beaconServerSetClientState(client, BCS_NEEDS_MORE_EXE_DATA);

		pktSendBits(pak, 1, 0);
	}

	pktSend(&pak);
}

typedef struct TempLegalArea {
	struct TempLegalArea*			nextInAll;
	struct TempLegalArea*			nextInColumn;
	BeaconLegalAreaCompressed*		area;
} TempLegalArea;

TempLegalArea* allTempAreas;

MP_DEFINE(TempLegalArea);

static void insertTempArea(TempLegalArea** cell, BeaconLegalAreaCompressed* area){
	TempLegalArea* newTemp;

	MP_CREATE(TempLegalArea, 256);

	newTemp = MP_ALLOC(TempLegalArea);

	newTemp->nextInAll = allTempAreas;
	allTempAreas = newTemp;

	newTemp->nextInColumn = *cell;
	newTemp->area = area;

	*cell = newTemp;
}

static void integrateNewLegalAreas(	BeaconServerClientData* client,
									Packet* pak,
									BeaconDiskSwapBlock* block,
									S32 checkedAreas,
									S32 beaconCount)
{
	static TempLegalArea* (*areaGrid)[BEACON_GENERATE_CHUNK_SIZE];

	S32 client_grid_x = client->assigned.block->x / BEACON_GENERATE_CHUNK_SIZE;
	S32 client_grid_z = client->assigned.block->z / BEACON_GENERATE_CHUNK_SIZE;
	BeaconLegalAreaCompressed* area;
	S32 receiveCount;
	S32 i;
	S32 addedCount = 0;
	S32 preExistCount = 0;

	if(!areaGrid){
		areaGrid = calloc(sizeof(areaGrid[0]) * BEACON_GENERATE_CHUNK_SIZE, 1);
		
		assert(areaGrid);
	}

	ZeroStructs(areaGrid, BEACON_GENERATE_CHUNK_SIZE);

	for(area = block->legalCompressed.areasHead; area; area = area->next){
		insertTempArea(&areaGrid[area->z][area->x], area);

		area->foundInReceiveList = 0;
	}

	receiveCount = pktGetBitsPack(pak, 5);

	beaconVerifyUncheckedCount(block);

	for(i = 0; i < receiveCount; i++){
		U8	x = pktGetBits(pak, 8);
		U8	z = pktGetBits(pak, 8);
		S32 isIndex = pktGetBits(pak, 1);
		S32 y_index = isIndex ? pktGetBitsPack(pak, 5) : 0;
		F32	y_coord = isIndex ? 0 : pktGetF32(pak);
		TempLegalArea* cur;
		S32 found = 0;

		beaconVerifyUncheckedCount(block);

		for(cur = areaGrid[z][x]; cur; cur = cur->nextInColumn){
			area = cur->area;

			if(area->isIndex == isIndex){
				if(	isIndex && area->y_index == y_index ||
					!isIndex && area->y_coord == y_coord)
				{
					beaconReceiveColumnAreas(pak, area);

					#if BEACONGEN_STORE_AREA_CREATOR
						area->areas.cx = client_grid_x;
						area->areas.cz = client_grid_z;
						area->areas.ip = linkGetIp(client->link);;
					#endif

					beaconVerifyUncheckedCount(block);

					if(checkedAreas){
						beaconVerifyUncheckedCount(block);

						if(!area->checked){
							area->checked = 1;

							assert(block->legalCompressed.uncheckedCount > 0);

							block->legalCompressed.uncheckedCount--;

							beaconVerifyUncheckedCount(block);
						}
					}

					found = 1;

					if(!area->foundInReceiveList){
						area->foundInReceiveList = 1;
						preExistCount++;
					}

					break;
				}
			}
		}

		if(found){
			beaconVerifyUncheckedCount(block);

			continue;
		}

		beaconVerifyUncheckedCount(block);

		area = beaconAddLegalAreaCompressed(block);
		area->x = x;
		area->z = z;
		area->isIndex = isIndex;

		if(isIndex){
			area->y_index = y_index;
		}else{
			area->y_coord = y_coord;
		}

		if(checkedAreas){
			area->checked = 1;
		}else{
			area->checked = 0;

			block->legalCompressed.uncheckedCount++;
		}

		beaconVerifyUncheckedCount(block);

		insertTempArea(&areaGrid[z][x], area);

		addedCount++;

		beaconVerifyUncheckedCount(block);

		beaconReceiveColumnAreas(pak, area);

		#if BEACONGEN_STORE_AREA_CREATOR
			area->areas.cx = client_grid_x;
			area->areas.cz = client_grid_z;
			area->areas.ip = linkGetIp(client->link);
		#endif
	}

	if(!beacon_server.minimalPrinting){
		beaconClientPrintf(	client, 0,
							"Added %4d legal areas to (%4d, %4d), %4d unchecked, %4d received, %4d pre-existed",
							addedCount,
							block->x / BEACON_GENERATE_CHUNK_SIZE,
							block->z / BEACON_GENERATE_CHUNK_SIZE,
							block->legalCompressed.uncheckedCount,
							receiveCount,
							preExistCount);

		if(checkedAreas){
			printf(", %4d beacons", beaconCount);
		}

		printf(".\n");
	}

	while(allTempAreas){
		TempLegalArea* next = allTempAreas->nextInAll;
		MP_FREE(TempLegalArea, allTempAreas);
		allTempAreas = next;
	}
}

static void beaconServerSetReachedFromValid(Beacon* beacon){
	if(!beacon->wasReachedFromValid){
		beacon->wasReachedFromValid = 1;

		assert(ea32Find(&beacon_server.beaconConnect.legalBeacons.indices, beacon->globalIndex)==-1);

		ea32Push(&beacon_server.beaconConnect.legalBeacons.indices, beacon->globalIndex);
	}
}

typedef void (*BeaconForEachClientCallbackFunction)(BeaconServerClientData* client, S32 index, void* userData);

typedef struct BeaconServerForEachClientCallbackStruct
{
	BeaconForEachClientCallbackFunction func;
	void *func_data;
	
} BeaconServerForEachClientCallbackStruct;

static int beaconServerForEachClientHelper(NetLink *link, int index, void* link_data, void* func_data)
{
	BeaconServerForEachClientCallbackStruct *s = func_data;
	BeaconServerClientData *client = link_data;
	s->func(client, index, s->func_data);
	return 1;
}

static void beaconServerForEachClient(BeaconForEachClientCallbackFunction callbackFunction, void* userData){
	BeaconServerForEachClientCallbackStruct s;
	s.func = callbackFunction;
	s.func_data = userData;

	linkIterate2(beacon_server.clients, beaconServerForEachClientHelper, &s);
}

static void readConnectHeader(BeaconServerClientData* client, Packet* pak){
	client->exeCRC = pktGetBits(pak, 32);

	if(!client->exeCRC){
		// This a new client.

		client->protocolVersion = pktGetBitsPack(pak, 1);
		client->exeCRC = pktGetBits(pak, 32);

		if(client->protocolVersion>=2)
			client->client.procVersion = pktGetBits(pak, 32);

		client->client.mapDataVersion = 1;
	}

	client->userName = pktMallocString(pak);
	client->computerName = pktMallocString(pak);
}

static BeaconServerMachineData* beaconServerFindMachine(NetLink *link, int create)
{
	U32 ip = linkGetIp(link);
	BeaconServerMachineData *ret = NULL;

	FOR_EACH_IN_EARRAY(beacon_server.machines, BeaconServerMachineData, machine)
	{
		if(machine->ip == ip)
			return machine;
	}
	FOR_EACH_END

	if(!create)
		return NULL;

	ret = calloc(1, sizeof(BeaconServerMachineData));

	ret->ip = ip;
	eaPush(&beacon_server.machines, ret);

	return ret;
}

static void beaconServerMachineAddClient(BeaconServerMachineData *machine, BeaconServerClientData *client)
{
	client->machine = machine;
	if(client->clientType==BCT_SENTRY)
	{
		machine->sentry = client;
	}
	else if(client->clientType==BCT_WORKER)
	{
		eaPushUnique(&machine->clients, client);
	}
}

static void beaconServerMachineDelClient(BeaconServerMachineData *machine, BeaconServerClientData *client)
{
	client->machine = NULL;

	if(machine->sentry==client)
		machine->sentry = NULL;
	else
		eaFindAndRemoveFast(&machine->clients, client);
}

static void processClientMsgConnect(BeaconServerClientData* client, Packet* pak){
	switch(client->state){
		xcase BCS_ERROR_NONLOCAL_IP:{
			readConnectHeader(client, pak);
		}

		xcase BCS_NOT_CONNECTED:{
			int good = true;

			readConnectHeader(client, pak);

			if(beaconIsSharded())
			{
				if(beacon_server.isMasterServer && client->client.procVersion!=beaconFileGetProcVersion())
				{
					beaconClientPrintf(client, 
										COLOR_RED,
										"ERROR: Client processing version is different! Expected: %d - Received: %d | (%s/%s)\n",
										beaconFileGetProcVersion(),
										client->client.procVersion,
										client->computerName,
										client->userName);

					good = false;
				}
			}
			else if(!beacon_server.allowCRCMismatch)
			{
				if(beacon_server.isMasterServer && client->exeCRC != beacon_server.exeClient.crc){
					beaconClientPrintf(	client,
										COLOR_YELLOW,
										"WARNING: Executable CRC is different! E(0x%x)/R(0x%x) (%s/%s)\n",
										beacon_server.exeClient.crc,
										client->exeCRC,
										client->computerName,
										client->userName);

					good = false;
				}
			}
			
			if(good)
			{
				BeaconServerMachineData *machine = NULL;
				S32 isSentry = pktGetBits(pak, 1);

				machine = beaconServerFindMachine(pktLink(pak), true);

				beaconServerSetClientType(client, isSentry ? BCT_SENTRY : BCT_WORKER);
				beaconServerSetClientState(client, isSentry ? BCS_SENTRY : BCS_CONNECTED);

				beaconServerMachineAddClient(machine, client);

				if(!beacon_server.minimalPrinting)
					beaconClientPrintf(client, COLOR_GREEN, "Executable CRC matches. (%s/%s)!\n", client->computerName, client->userName);

				beaconServerSendConnectReply(client, 1, 0);
			}
			else
				beaconServerSendConnectReply(client, 0, 1);
		}

		xdefault:{
			if(!beacon_server.minimalPrinting)
				beaconClientPrintf(client, COLOR_RED, "FATAL!  Sent client connect message while connected.\n");

			beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
		}
	}
}

static void beaconServerSendExeUpdate(BeaconServerClientData* client, const char* reason){
	beaconClientPrintf(	client,
						COLOR_YELLOW,
						"WARNING: Sending update to: %s/%s.  Reason: %s\n",
						client->computerName,
						client->userName,
						reason);

	beaconServerSendConnectReply(client, 0, client->server.protocolVersion < 2);
}

static void processClientMsgServerConnect(BeaconServerClientData* client, Packet* pak){
	beaconServerSetClientType(client, BCT_SERVER);

	switch(client->state){
		xcase BCS_ERROR_NONLOCAL_IP:{
			client->server.protocolVersion = pktGetBitsPack(pak, 1);
			pktGetBits(pak, 32);
			client->userName = pktMallocString(pak);
			client->computerName = pktMallocString(pak);
			client->server.patchTime = pktGetBitsPack(pak, 32);
		}

		xcase BCS_NOT_CONNECTED:{
			S32 crcMatches;

			client->server.protocolVersion = pktGetBitsPack(pak, 1);

			if(	client->server.protocolVersion < 0 ||
				client->server.protocolVersion > BEACON_SERVER_PROTOCOL_VERSION)
			{
				beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
				break;
			}

			client->exeCRC = pktGetBits(pak, 32);

			crcMatches = client->exeCRC == beacon_server.exeClient.crc;

			client->userName = pktMallocString(pak);
			client->computerName = pktMallocString(pak);
			client->server.port = pktGetBitsPack(pak, 1);
			client->server.patchTime = pktGetBitsPack(pak, 32);

			if(client->server.protocolVersion >= 1 &&
				client->server.protocolVersion <= 2){
				client->server.isRequestServer = pktGetBits(pak, 1);
			}

			if(client->server.protocolVersion >= 3)
			{
				client->server.type = pktGetU32(pak);

				if(client->server.type==BEACONIZER_TYPE_REQUEST_SERVER)
					client->server.isRequestServer = true;
			}

			if(client->server.protocolVersion >= 3)
				ParserRecv(parse_BeaconServerStatus, pak, &client->server.status, 0);

			beaconServerSetClientState(client, BCS_SERVER);

			beaconClientPrintf(	client,
								crcMatches ? COLOR_GREEN : COLOR_YELLOW,
								"Server connected - CRC 0x%x, %s/%s, Protocol %d\n",
								client->exeCRC,
								client->computerName,
								client->userName,
								client->server.protocolVersion);

			beaconServerSendConnectReply(client, 1, 0);

			if(client->server.protocolVersion >= 3)
			{
				BeaconProject *proj = beaconMasterGetProject(client->server.status.project);
				BeaconServerHistory *hist = beaconProjectGetHistory(proj, client->server.status.branch);

				SAFE_FREE(hist->machineName);
				hist->machineName = strdup(client->computerName);

				eaPush(&hist->current, client);

				// Write updated projects here?
			}
			else
			{
				BeaconProject *proj = beaconMasterGetProject("Protocol < 3");
				BeaconServerHistory *hist = beaconProjectGetHistory(proj, -1);

				SAFE_FREE(hist->machineName);
				hist->machineName = strdup(client->computerName);

				eaPush(&hist->current, client);
			}
		}

		xdefault:{
			beaconClientPrintf(client, COLOR_RED, "FATAL!  Sent server connect message while connected.\n");

			beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
		}
	}
}

static void processClientMsgRequesterConnect(BeaconServerClientData* client, Packet* pak){
	beaconServerSetClientType(client, BCT_REQUESTER);

	switch(client->state){
		xcase BCS_ERROR_NONLOCAL_IP:{
			client->server.protocolVersion = pktGetBitsPack(pak, 1);
			client->userName = pktMallocString(pak);
			client->computerName = pktMallocString(pak);
			client->requester.dbServerIP = pktMallocString(pak);
		}

		xcase BCS_NOT_CONNECTED:{
			client->server.protocolVersion = pktGetBitsPack(pak, 1);
			client->userName = pktMallocString(pak);
			client->computerName = pktMallocString(pak);
			client->requester.dbServerIP = pktMallocString(pak);

			if(	client->server.protocolVersion < 0 ||
				client->server.protocolVersion > BEACON_SERVER_PROTOCOL_VERSION)
			{
				beaconClientPrintf(	client,
									COLOR_RED,
									"ERROR: Protocol %d is out of range [0,%d].\n",
									client->server.protocolVersion,
									BEACON_SERVER_PROTOCOL_VERSION);

				beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
			}else{
				beaconServerSetClientState(client, BCS_REQUESTER);
			}
		}

		xdefault:{
			beaconClientPrintf(client, COLOR_RED, "FATAL!  Sent requester connect message while connected.\n");

			beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
		}
	}
}

static void processClientMsgGenerateFinished(BeaconServerClientData* client, Packet* pak){
	if(client->state != BCS_GENERATING){
		if(!beacon_server.minimalPrinting)
			beaconClientPrintf(client, COLOR_YELLOW, "WARNING: Sent legal areas when not generating.\n");
	}
	else if(!client->assigned.block){
		if(!beacon_server.minimalPrinting)
			beaconClientPrintf(client, COLOR_YELLOW, "Finished generating, but not first.\n");
		beaconServerSetClientState(client, BCS_READY_TO_GENERATE);
	}
	else{
		BeaconDiskSwapBlock* clientBlock = client->assigned.block;
		S32 client_grid_x = clientBlock->x / BEACON_GENERATE_CHUNK_SIZE;
		S32 client_grid_z = clientBlock->z / BEACON_GENERATE_CHUNK_SIZE;
		S32 i;

		if(!beacon_server.minimalPrinting)
			beaconClientPrintf(client, 0, "Receiving legal areas (%sb).\n", getCommaSeparatedInt(pktGetSize(pak)));

		while(pktGetBits(pak, 1) == 1){
			S32 grid_x = pktGetBitsPack(pak, 1);
			S32 grid_z = pktGetBitsPack(pak, 1);
			S32 dx = grid_x - client_grid_x;
			S32 dz = grid_z - client_grid_z;

			if(abs(dx) <= 1 && abs(dz) <= 1){
				BeaconDiskSwapBlock* block = beaconGetDiskSwapBlockByGrid(grid_x, grid_z);
				S32 beaconCount = 0;

				if(!block){
					if(!beacon_server.minimalPrinting)
					{
						beaconClientPrintf(	client,
											COLOR_RED,
											"ERROR: Sent nonexistent block (%d,%d) when assigned block (%d,%d).\n",
											grid_x,
											grid_z,
											client_grid_x,
											client_grid_z);
					}

					beaconServerSetClientState(client, BCS_ERROR_DATA);

					return;
				}

				if(client_grid_x == grid_x && client_grid_z == grid_z){
					U32 surfaceCRC = pktGetBits(pak, 32);

					if(block->foundCRC){
						assert(surfaceCRC == block->surfaceCRC);
					}else{
						block->foundCRC = 1;
						block->surfaceCRC = surfaceCRC;
					}

					beaconCount = pktGetBitsPack(pak, 1);

					for(i = 0; i < beaconCount; i++){
						S32 j = block->generatedBeacons.count;

						dynArrayAddStruct(	block->generatedBeacons.beacons,
											block->generatedBeacons.count,
											block->generatedBeacons.maxCount);

						pktGetVec3(pak, block->generatedBeacons.beacons[j].pos);
						block->generatedBeacons.beacons[j].noGroundConnections = pktGetBits(pak, 1);
					}
				}

				integrateNewLegalAreas(client, pak, block, !dx && !dz, beaconCount);
			}else{
				if(!beacon_server.minimalPrinting)
				{
					beaconClientPrintf(	client,
										COLOR_RED,
										"ERROR: Was processing (%d,%d), but sent (%d,%d)!\n",
										client_grid_x,
										client_grid_z,
										grid_x,
										grid_z);
				}

				beaconServerSetClientState(client, BCS_ERROR_DATA);

				return;
			}
		}

		for(i = 0; i < clientBlock->clients.count; i++){
			assert(clientBlock->clients.clients[i]->assigned.block == clientBlock);
			clientBlock->clients.clients[i]->assigned.block = NULL;
			clientBlock->clients.clients[i] = NULL;
		}

		clientBlock->clients.count = 0;

		beaconServerSetClientState(client, BCS_READY_TO_GENERATE);

		client->completed.blockCount++;
	}
}

static int beaconCmpIndex(const BeaconConnection** lhs, const BeaconConnection** rhs)
{
	const BeaconConnection *l = *lhs, *r = *rhs;

	return l->destBeacon->globalIndex - r->destBeacon->globalIndex;
}

static void beaconMergeConnections(Array *beaconConns, Array *newConns)
{
	int i, curIdx, beaconConnSize;
	static BeaconConnection **connsToCopy;

	if(!newConns->size)
		return;

	if(!beaconConns->size)
	{
		beaconInitCopyArray(beaconConns, newConns);
		return;
	}

	qsort(beaconConns->storage, beaconConns->size, sizeof(BeaconConnection*), beaconCmpIndex);
	qsort(newConns->storage, newConns->size, sizeof(BeaconConnection*), beaconCmpIndex);

	curIdx = 0;
	eaClearFast(&connsToCopy);
	beaconConnSize = beaconConns->size;

	for(i=0; i<newConns->size; i++)
	{
		BeaconConnection *conn = NULL;
		BeaconConnection *newConn = newConns->storage[i];

		if(curIdx<beaconConnSize)
		{
			conn = beaconConns->storage[curIdx];

			while(conn && conn->destBeacon->globalIndex<newConn->destBeacon->globalIndex)
			{
				curIdx++;

				if(curIdx>=beaconConns->size)
				{
					conn = NULL;
					break;
				}

				conn = beaconConns->storage[curIdx];
			}
		}

		if(!conn || newConn->destBeacon->globalIndex<conn->destBeacon->globalIndex)
		{
			assert(newConn);
			arrayPushBack(beaconConns, newConn);
		}
		else if(newConn->destBeacon->globalIndex==conn->destBeacon->globalIndex)
			destroyBeaconConnection(newConn);
	}
}

static void beaconServerInformClientsOfConnections(S32 *beaconList)
{
	S32 i;
	BeaconServerClientData *client = NULL; 
	Packet *tmp = NULL; 

	if(combatBeaconArray.size>100000)
		return;

	if(beacon_server.clientList[BCT_WORKER].count<=0)
		return;

	client = beacon_server.clientList[BCT_WORKER].clients[0];
	tmp = pktCreateTemp(client->link);

	pktSendBitsAuto(tmp, ea32Size(&beaconList));
	for(i=0; i<ea32Size(&beaconList); i++)
	{
		int j;
		Beacon *b;
		int idx = beaconList[i];

		assert(idx>=0 && idx<combatBeaconArray.size);
		b = combatBeaconArray.storage[idx];

		pktSendBitsAuto(tmp, b->globalIndex);

		pktSendBitsAuto(tmp, b->gbConns.size);
		for(j=0; j<b->gbConns.size; j++)
		{
			BeaconConnection *conn = b->gbConns.storage[j];
			pktSendBitsAuto(tmp, conn->destBeacon->globalIndex);
		}

		pktSendBitsAuto(tmp, b->rbConns.size);
		for(j=0; j<b->rbConns.size; j++)
		{
			BeaconConnection *conn = b->rbConns.storage[j];
			pktSendBitsAuto(tmp, conn->destBeacon->globalIndex);
		}
	}

	for(i=0; i<beacon_server.clientList[BCT_WORKER].count; i++)
	{
		Packet *pak = NULL;
		
		client = beacon_server.clientList[BCT_WORKER].clients[i];

		if(client->state==BCS_CONNECTING_BEACONS ||
			client->state==BCS_READY_TO_CONNECT_BEACONS ||
			client->state==BCS_PIPELINE_CONNECT_BEACONS)
		{
			pak = BEACON_SERVER_PACKET_CREATE_VS(client, BMSG_S2CT_BEACON_CONNECTIONS);

			pktSendPacket(pak, tmp);

			BEACON_SERVER_PACKET_SEND_WAIT_VS(pak, client);
		}
	}
	
	pktFree(&tmp);
}

static void beaconServerAdvanceClientPipeline(BeaconServerClientData* client)
{
	if(client->assigned.pipeline)
	{
		BeaconConnectBeaconGroup *group = client->assigned.pipeline;
		beaconServerRemoveClientFromConnectGroup(client, client->assigned.group, false);
		client->assigned.pipeline = NULL;
		client->assigned.group = group;
		beaconServerSetClientState(client, BCS_PIPELINE_CONNECT_BEACONS);
	}
	else
	{
		beaconServerRemoveClientFromConnectGroup(client, client->assigned.group, false);
		beaconServerSetClientState(client, BCS_READY_TO_CONNECT_BEACONS);
	}
}

static void processClientMsgBeaconConnections(BeaconServerClientData* client, Packet* pak){
	if(client->state != BCS_CONNECTING_BEACONS && client->state != BCS_PIPELINE_CONNECT_BEACONS){
		if(!beacon_server.minimalPrinting)
			beaconClientPrintf(client, COLOR_YELLOW, "WARNING: Sent beacon connections when not connecting.\n");
	}
	else if(!client->assigned.group){
		if(!beacon_server.minimalPrinting)
			beaconClientPrintf(client, COLOR_YELLOW, "Finished connecting, but not first.\n");

		beaconServerAdvanceClientPipeline(client);
	}
	else
	{
		BeaconConnectBeaconGroup* group = client->assigned.group;
		S32 lo;
		S32 hi;
		S32 clientLo;
		S32 clientHi;
		S32 i;
		static S32 *connChanges = NULL;

		assert(group);

		lo = group->lo;
		hi = group->hi;

		if(client->protocolVersion>=4)
		{
			clientLo = pktGetBitsPack(pak, 1);
			clientHi = pktGetBitsPack(pak, 1);

			if(lo!=clientLo)
			{
				assert(hi!=clientHi);
				beaconClientPrintf(client, COLOR_YELLOW, "Finished connecting, but not first.\n");

				beaconServerAdvanceClientPipeline(client);

				return;
			}
		}

		if(!beacon_server.minimalPrinting)
			beaconClientPrintf(client, COLOR_BLUE, "Receiving  %3d beacons [%6d-%6d]\n", hi - lo + 1, lo, hi);

		ea32ClearFast(&connChanges);
		for(i = lo; i <= hi; i++){
			static Array tempGroundConns;
			static Array tempRaisedConns;

			S32 index = pktGetBitsPack(pak, 1);
			S32 beaconCount;
			Beacon* b;
			S32 j;

			ea32PushUnique(&connChanges, index);

			assert(index>=0 && index<combatBeaconArray.size);
			
			if(client->protocolVersion>=4)
				assert(index==beacon_server.beaconConnect.legalBeacons.indices[i]);
			else if(index!=beacon_server.beaconConnect.legalBeacons.indices[i])
			{
				beaconClientPrintf(client, COLOR_RED, "Error in beacon connecting.\n");

				beaconServerAdvanceClientPipeline(client);
				return;
			}
			
			b = combatBeaconArray.storage[index];

			if(beacon_server.debug_state &&
				!vec3IsZero(beacon_server.debug_state->debug_pos) &&
				distance3(beacon_server.debug_state->debug_pos, b->pos) < 5)
			{
				printf("");
			}

			assert(b->globalIndex==index);

			beaconCount = pktGetBitsPack(pak, 1);

			tempGroundConns.size = 0;
			tempRaisedConns.size = 0;

			for(j = 0; j < beaconCount; j++){
				Beacon* targetBeacon;
				BeaconConnection* conn;
				S32 targetIndex = pktGetBitsPack(pak, 1);
				S32 raisedCount;
				S32 k;

				assert(targetIndex >= 0 && targetIndex < combatBeaconArray.size);

				targetBeacon = combatBeaconArray.storage[targetIndex];

				beaconServerSetReachedFromValid(targetBeacon);

				if(pktGetBits(pak, 1)){
					S32 optional = pktGetBitsPack(pak, 1);
					conn = createBeaconConnection();
					conn->destBeacon = targetBeacon;
					conn->gflags.optional = !!optional;
					assert(conn);
					arrayPushBack(&tempGroundConns, conn);

					if(client->protocolVersion>=3)
					{
						int bidir = pktGetBitsPack(pak, 1);

						if(bidir && !beaconFindConnection(targetBeacon, b, 0))
						{							
							conn = createBeaconConnection();
							conn->destBeacon = b;
							conn->gflags.optional = !!optional;

							ea32PushUnique(&connChanges, targetIndex);

							assert(conn);
							if(!targetBeacon->gbConns.size)
								beaconInitArray(&targetBeacon->gbConns, 10);
							arrayPushBack(&targetBeacon->gbConns, conn);
						}
					}
				}

				raisedCount = pktGetBitsPack(pak, 1);
				for(k = 0; k < raisedCount; k++){
					conn = createBeaconConnection();
					conn->destBeacon = targetBeacon;
					conn->minHeight = pktGetF32(pak);
					conn->maxHeight = pktGetF32(pak);
					assert(conn);
					arrayPushBack(&tempRaisedConns, conn);

					if(client->protocolVersion>=3)
					{
						int bidir = true;

						if(bidir && !beaconFindConnection(targetBeacon, b, 1))
						{
							BeaconConnection *revConn = createBeaconConnection();
							revConn->destBeacon = b;

							ea32PushUnique(&connChanges, targetIndex);

							revConn->minHeight = 0;
							if(targetBeacon->pos[1] < b->pos[1])
								revConn->minHeight = b->pos[1] - targetBeacon->pos[1];

							revConn->maxHeight = revConn->minHeight + conn->maxHeight;

							if(revConn->minHeight < 1)
								revConn->minHeight = 1;

							assert(revConn);
							if(!targetBeacon->rbConns.size)
								beaconInitArray(&targetBeacon->rbConns, 10);
							arrayPushBack(&targetBeacon->rbConns, revConn);
						}
					}
				}
			}

			beaconMergeConnections(&b->gbConns, &tempGroundConns);
			beaconMergeConnections(&b->rbConns, &tempRaisedConns);
		}

		group->finished = 1;
		for(i = eaSize(&group->clients.clients)-1; i >= 0 ; i--)
		{
			BeaconServerClientData *displaced = group->clients.clients[i];

			if(displaced->assigned.group == group)
				beaconServerAdvanceClientPipeline(displaced);
			else
			{
				devassert(displaced->assigned.pipeline==group);
				beaconServerRemoveClientFromConnectGroup(displaced, group, false);
			}
		}
		
		assert(eaSize(&group->clients.clients)==0);

		beacon_server.beaconConnect.finishedCount++;
		beacon_server.beaconConnect.finishedBeacons += hi-lo+1;

		client->completed.beaconCount += hi - lo + 1;

		beaconServerInformClientsOfConnections(connChanges);
	}
}

// This just screams hideous to me
typedef struct ClientPlusUID {
	U32 uid;
	BeaconServerClientData *clientOut;
} ClientPlusUID;

static int findClientByUIDHelper(NetLink* link, int index, void* link_data, void* func_data)
{
	BeaconServerClientData* client = link_data;
	ClientPlusUID *cpu = func_data;
	if(client->uid == cpu->uid)
	{
		cpu->clientOut = client;
		return 0;
	}
	return 1;
}

static BeaconServerClientData* findClientByUID(U32 clientUID){
	ClientPlusUID cpu = {clientUID, NULL};
	linkIterate2(beacon_server.clients, findClientByUIDHelper, &cpu);

	return cpu.clientOut;
}

static void beaconServerSendCrashEmail(BeaconServerClientData *sentry, const char *crashText)
{
	char *msg = NULL;
	char *systemstr = NULL;
	char to[] = "beaconizer-users@" ORGANIZATION_DOMAIN;
	char from[] = "beaconizerNoReply@" ORGANIZATION_DOMAIN;
	char title[] = "Beaconizer Client Crash";

	if(beacon_server.noGimmeUsage)
	{
		return;
	}
	return;

	estrStackCreate(&msg);
	estrStackCreate(&systemstr);
	
	estrPrintf(&msg, "Client Crashed: %s\\%s: \n%s", sentry->computerName, sentry->userName, crashText);

	estrPrintf(&systemstr, "n:\\bin\\bmail.exe -s universe -t \"%s\" -f \"%s\" -a \"%s\" -b \"%s\"",
		to, from, title, msg);

	system_detach(systemstr, 1, 1);
	estrDestroy(&msg);
	estrDestroy(&systemstr);
}

static void beaconServerSendStatusAck(BeaconServerClientData* client){
	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_STATUS_ACK);
		pktSendBitsPack(pak, 1, client->server.sendStatusUID);
	BEACON_SERVER_PACKET_SEND();
}

static void processClientMsgServerStatus(BeaconServerClientData* client, Packet* pak){
	if(client->state != BCS_SERVER){
		beaconClientPrintf(client, COLOR_YELLOW, "Sending server status when not connected!\n");
	}else{
		estrCopy2(&client->server.mapName, pktGetStringTemp(pak));

		client->server.clientCount =		pktGetBitsPack(pak, 1);
		client->server.state =				pktGetBitsPack(pak, 1);
		client->server.sendStatusUID =		pktGetBitsPack(pak, 1);
		client->server.sendClientsToMe =	pktGetBits(pak, 1);

		if(client->server.protocolVersion>=3)
		{
			U32 statusChanged = pktGetBits(pak, 1);

			if(statusChanged)
				ParserRecv(parse_BeaconServerStatus, pak, &client->server.status, 0);
		}

		beaconServerSendStatusAck(client);
	}
}

void beaconServerSendRequestChunkReceived(BeaconProcessQueueNode* node){
	if(!node->requester){
		return;
	}

	node->requester->requester.toldToSendRequestTime = beaconGetCurTime();

	node->state = BPNS_WAITING_FOR_MAP_DATA_FROM_CLIENT;

	BEACON_SERVER_PACKET_CREATE_BASE(pak, node->requester, BMSG_S2CT_REQUEST_CHUNK_RECEIVED);
	BEACON_SERVER_PACKET_SEND();
}

static void beaconServerSendRequestAccepted(BeaconServerClientData* client){
	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_REQUEST_ACCEPTED);

		pktSendBitsPack(pak, 1, client->requester.uid);

	BEACON_SERVER_PACKET_SEND();
}

static void processClientMsgRequesterMapData(BeaconServerClientData* client, Packet* pak){
	if(client->clientType != BCT_REQUESTER){
		beaconClientPrintf(client, COLOR_YELLOW, "Sending map data from non-requester client.");
	}else{
		// Requester is sending the map data.

		U32 					uid = pktGetBitsPack(pak, 1);
		char					uniqueStorageName[1000];
		BeaconMapDataPacket*	tempMapData;

		client->requester.uid = uid;

		Strncpyt(uniqueStorageName, pktGetStringTemp(pak));

		beaconMapDataPacketCreate(&tempMapData);

		beaconMapDataPacketReceiveChunkHeader(pak, tempMapData);

		if(beaconMapDataPacketIsFirstChunk(tempMapData)){
			beaconClientPrintf(	client,
								COLOR_GREEN,
								"Adding request: \"%s:%d\", crc 0x%8.8x\n",
								uniqueStorageName,
								uid,
								beaconMapDataPacketGetCRC(tempMapData));

			// Receord the time that this request was first received.

			client->requester.startedRequestTime = beaconGetCurTime();

			// Add me to the process queue.

			beaconServerAddRequesterToProcessQueue(	client,
													uniqueStorageName,
													beaconMapDataPacketGetCRC(tempMapData));

			// Create the load request.

			beaconServerAddToBeaconFileLoadQueue(client, beaconMapDataPacketGetCRC(tempMapData));

			// Tell the requester that the request was accepted.

			beaconServerSendRequestAccepted(client);
		}else{
			//beaconPrintf(COLOR_GREEN, "Received next chunk\n");

			if(	client->requester.processNode &&
				client->requester.processNode->state == BPNS_WAITING_FOR_MAP_DATA_FROM_CLIENT)
			{
				client->requester.processNode->state = BPNS_WAITING_TO_REQUEST_MAP_DATA_FROM_CLIENT;
			}
		}

		if(client->requester.processNode){
			switch(client->requester.processNode->state){
				xcase	BPNS_WAITING_FOR_LOAD_REQUEST:
				case	BPNS_WAITING_FOR_MAP_DATA_FROM_CLIENT:
				case	BPNS_WAITING_TO_REQUEST_MAP_DATA_FROM_CLIENT:
				{
					// Copy the data if it hasn't been fully received or gotten a successful load yet.

					assert(!beaconMapDataPacketIsFullyReceived(client->requester.processNode->mapData));

					beaconMapDataPacketCopyHeader(client->requester.processNode->mapData, tempMapData);

					beaconMapDataPacketReceiveChunkData(pak, client->requester.processNode->mapData);
				}
			}
		}

		beaconMapDataPacketDestroy(&tempMapData);
	}
}

static void processClientMsgUserInactive(BeaconServerClientData* client, Packet* pak){
	char* reason;

	client->forcedInactive = pktGetBits(pak, 1);
	reason = pktMallocString(pak);

	if(client->forcedInactive){
		beaconClientPrintf(	client,
							COLOR_GREEN,
							"User inactive (%s/%s)!  Reason: %s\n",
							client->computerName,
							client->userName,
							reason);
	}else{
		beaconClientPrintf(	client,
							COLOR_YELLOW,
							"User active   (%s/%s)!  Reason: %s\n",
							client->computerName,
							client->userName,
							reason);
	}

	SAFE_FREE(reason);
}

static void processClientMsgReadyToWork(BeaconServerClientData* client, Packet* pak){
	switch(client->state){
		xcase BCS_CONNECTED:{
			if(!beacon_server.minimalPrinting)
			{
				beaconClientPrintf(	client, COLOR_GREEN, "Ready!  (%s/%s)\n", 
					client->computerName, 
					client->userName);
			}

			if(client->client.assignedTo)
				eaFindAndRemoveFast(&client->client.assignedTo->server.clients, client);
			client->client.assignedTo = NULL;
			beaconServerSetClientState(client, BCS_READY_TO_WORK);
			client->readyForCommands = 1;
		}

		xcase BCS_SENTRY:{
			beaconClientPrintf(	client, COLOR_GREEN, "Sentry Ready!\n");
		}

		xdefault:{
			beaconClientPrintf(client, COLOR_RED, "FATAL!  Tried to be ready before connecting!\n");
			beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
		}
	}
}

static void beaconServerSendMapDataToWorker(BeaconServerClientData* client){
	BeaconMapDataPacket* mapData = beacon_server.mapData;

	if(!mapData){
		return;
	}

	if(beaconMapDataPacketIsFullySent(mapData, client->mapData.sentByteCount)){
		beaconClientPrintf(	client,
							COLOR_RED,
							"ERROR: Client's sent byte count (%d) is higher than total (%d)!\n",
							client->mapData.sentByteCount,
							beaconMapDataPacketGetSize(mapData));

		beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
		return;
	}

	client->mapData.lastCommTime = beaconGetCurTime();

	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_MAP_DATA);

		// Send a bit to indicate whether the server cares about the map CRC.

		pktSendBits(pak, 1, beacon_server.isRequestServer ? 1 : 0);

		beaconMapDataPacketSendChunk(pak, mapData, &client->mapData.sentByteCount);

	BEACON_SERVER_PACKET_SEND();

	beaconServerSetClientState(client, BCS_RECEIVING_MAP_DATA);
}

static void processClientMsgNeedMoreMapData(BeaconServerClientData* client, Packet* pak){
	switch(client->clientType){
		xcase BCT_SERVER:{
			beaconServerSendMapDataToRequestServer(client);
		}

		xcase BCT_WORKER:{
			if(client->state == BCS_RECEIVING_MAP_DATA){
				U32 receivedByteCount;

				if(!beaconMapDataPacketReceiveChunkAck(	pak,
														client->mapData.sentByteCount,
														&receivedByteCount))
				{
					beaconClientPrintf(	client,
										COLOR_RED,
										"ERROR: Client says %d bytes received, server says %d!\n",
										receivedByteCount,
										client->mapData.sentByteCount);

					beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
					return;
				}

				beaconServerSetClientState(client, BCS_NEEDS_MORE_MAP_DATA);

				client->mapData.lastCommTime = beaconGetCurTime();
				
				beacon_server.noNetWait = 1;
			}
		}
	}
}

static void beaconServerSendNextExeChunk(BeaconServerClientData* client){
	if(	!client->exeData.shouldBeSent ||
		client->exeData.sentByteCount >= beacon_server.exeFile.size)
	{
		return;
	}

	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_EXE_DATA);

		const U32 maxBytesToSend = 32 * 1024;
		U32 bytesRemaining = beacon_server.exeFile.size - client->exeData.sentByteCount;
		U32 bytesToSend = min(bytesRemaining, maxBytesToSend);

		pktSendBitsPack(pak, 1, client->exeData.sentByteCount);
		pktSendBitsPack(pak, 1, beacon_server.exeFile.size);
		pktSendBitsPack(pak, 1, bytesToSend);
		pktSendBytes(pak, bytesToSend, beacon_server.exeFile.data + client->exeData.sentByteCount);

		client->exeData.sentByteCount += bytesToSend;

		if(0){
			beaconClientPrintf(	client,
								COLOR_GREEN,
								"Sent exe: %s/%s bytes.\n",
								getCommaSeparatedInt(client->exeData.sentByteCount),
								getCommaSeparatedInt(beacon_server.exeFile.size));
		}

	BEACON_SERVER_PACKET_SEND();

	beaconServerSetClientState(client, BCS_RECEIVING_EXE_DATA);

	client->exeData.lastCommTime = beaconGetCurTime();
}

static void processClientMsgNeedMoreExeData(BeaconServerClientData* client, Packet* pak){
	switch(client->state){
		xcase BCS_RECEIVING_EXE_DATA:{
			S32 receivedByteCount = pktGetBitsPack(pak, 1);

			if(receivedByteCount != client->exeData.sentByteCount){
				beaconClientPrintf(	client,
									COLOR_RED,
									"ERROR: Client says %d exe bytes received, server says %d!\n",
									receivedByteCount,
									client->exeData.sentByteCount);

				beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
				return;
			}

			beaconServerSetClientState(client, BCS_NEEDS_MORE_EXE_DATA);

			client->exeData.lastCommTime = beaconGetCurTime();

			beacon_server.noNetWait = 1;
		}
	}
}

static void beaconServerSendProcessRequestedMap(BeaconServerClientData* client){
	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_PROCESS_REQUESTED_MAP);
	BEACON_SERVER_PACKET_SEND();
}

static void processClientMsgMapDataIsLoaded(BeaconServerClientData* client, Packet* pak){
	switch(client->clientType){
		xcase BCT_SERVER:{
			if(	client->server.isRequestServer &&
				client->requestServer.processNode)
			{
				// This request server has successfully loaded the map file.

				BeaconProcessQueueNode* node = client->requestServer.processNode;
				U32 nodeUID;
				char uniqueStorageName[1000];

				nodeUID = pktGetBitsPack(pak, 1);

				if(node->uid != nodeUID){
					return;
				}

				Strncpyt(uniqueStorageName, pktGetStringTemp(pak));

				if(stricmp(uniqueStorageName, node->uniqueStorageName)){
					return;
				}

				if(beaconMapDataPacketIsFullySent(	node->mapData,
													client->requestServer.sentByteCount))
				{
					beaconServerSendProcessRequestedMap(client);
				}
			}
		}

		xcase BCT_WORKER:{
			if(client->state != BCS_RECEIVING_MAP_DATA){
				return;
			}
			else if(!beaconMapDataPacketIsFullySent(beacon_server.mapData, client->mapData.sentByteCount)){
				beaconClientPrintf(	client,
									COLOR_RED,
									"ERROR: Client sent %s when %s/%s data was sent.\n",
									BMSG_C2ST_MAP_DATA_IS_LOADED,
									getCommaSeparatedInt(client->mapData.sentByteCount),
									getCommaSeparatedInt(beaconMapDataPacketGetSize(beacon_server.mapData)));

				beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);

				beaconServerSendLoadMapReply(client, 1);
			}
			else{
				U32 geoCRC = pktGetBits(pak, 32);

				if(geoCRC == beacon_server.mapDataCRC){
					if(!beacon_server.minimalPrinting)
						beaconClientPrintf(client, COLOR_GREEN, "Good map CRC! (%s/%s)\n", client->computerName, client->userName);

					beaconServerSetClientState(client, BCS_READY_TO_GENERATE);

					beaconServerSendLoadMapReply(client, 1);
				}else{
					if(geoCRC != beacon_server.mapDataCRC)
					{
						beaconClientPrintf(	client,
											COLOR_RED,
											"FATAL!  Bad geo CRC!  (Good: 0x%8.8x, Sent: 0x%8.8x)\n",
											beacon_process.mapMetaData->geoCRC,
											geoCRC);
					}

					if(!beacon_server.fullInfoOnce)
					{
						beacon_server.fullInfoOnce = 1;
						beaconLogCRCInfo(worldGetActiveColl(worldGetAnyCollPartitionIdx()));
					}

					beaconServerSetClientState(client, BCS_ERROR_DATA);

					beaconServerSendLoadMapReply(client, 0);
				}
			}
		}
	}
}

void beaconServerSendBeaconFileToRequester(BeaconServerClientData* client, S32 start){
	BeaconProcessQueueNode* node = client ? client->requester.processNode : NULL;

	if(	!node ||
		!estrLength(&node->uniqueStorageName))
	{
		return;
	}

	if(start){
		node->beaconFile.sentByteCount = 0;
	}

	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_BEACON_FILE);

		U32 remaining = node->beaconFile.byteCount - node->beaconFile.sentByteCount;
		U32 bytesToSend = min(remaining, 64 * 1024);

		pktSendBitsPack(pak, 1, client->requester.uid);
		pktSendString(pak, node->uniqueStorageName);
		pktSendBitsPack(pak, 1, node->beaconFile.byteCount);
		pktSendBitsPack(pak, 1, node->beaconFile.sentByteCount);
		pktSendBitsPack(pak, 1, bytesToSend);
		pktSendBitsPack(pak, 1, node->beaconFile.uncompressedByteCount);

		pktSendBytes(	pak,
						bytesToSend,
						node->beaconFile.data + node->beaconFile.sentByteCount);

		node->beaconFile.sentByteCount += bytesToSend;

	BEACON_SERVER_PACKET_SEND();
}

static void beaconServerSendNeedMoreBeaconFile(BeaconServerClientData* client){
	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_NEED_MORE_BEACON_FILE);
	BEACON_SERVER_PACKET_SEND();
}

static void processClientMsgBeaconFile(BeaconServerClientData* client, Packet* pak){
	BeaconProcessQueueNode* node = client->requestServer.processNode;
	U32 nodeUID;
	char uniqueStorageName[1000];
	U32 readByteCount;

	// Verify that this is a request server.

	if(!client->server.isRequestServer){
		//beaconClientPrintf(	client,
		//					COLOR_RED,
		//					"ERROR: Received beacon file from non-request-server client!\n");

		beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
		return;
	}

	// Verify that there is a process node.

	if(!node){
		//beaconClientPrintf(	client,
		//					COLOR_YELLOW,
		//					"Received beacon file when no process node is present!\n");
		return;
	}

	// Read the packet.

	nodeUID = pktGetBitsPack(pak, 1);
	Strncpyt(uniqueStorageName, pktGetStringTemp(pak));

	if(	node->uid != nodeUID ||
		!node->uniqueStorageName ||
		stricmp(node->uniqueStorageName, uniqueStorageName))
	{
		//beaconClientPrintf(	client,
		//					COLOR_YELLOW,
		//					"Received beacon file for client, but the request has since changed.\n");

		return;
	}

	node->beaconFile.receivedByteCount = pktGetBitsPack(pak, 1);
	readByteCount = pktGetBitsPack(pak, 1);
	node->beaconFile.byteCount = pktGetBitsPack(pak, 1);
	node->beaconFile.uncompressedByteCount = pktGetBitsPack(pak, 1);
	node->beaconFile.crc = pktGetBitsPack(pak, 1);

	if(!node->beaconFile.receivedByteCount){
		SAFE_FREE(node->beaconFile.data);

		node->beaconFile.data = malloc(node->beaconFile.byteCount);
	}

	pktGetBytes(pak, readByteCount, node->beaconFile.data + node->beaconFile.receivedByteCount);

	node->beaconFile.receivedByteCount += readByteCount;

	//beaconClientPrintf(	client,
	//					COLOR_GREEN,
	//					"Received %s/%s bytes of beacon file.\n",
	//					getCommaSeparatedInt(node->beaconFile.receivedByteCount),
	//					getCommaSeparatedInt(node->beaconFile.byteCount));

	if(node->beaconFile.receivedByteCount == node->beaconFile.byteCount){
		// The whole beacon file has been received, so save it and send to the client.

		beaconServerWriteRequestedBeaconFile(node);

		if(node->requester){
			beaconServerSendBeaconFileToRequester(node->requester, 1);

			beaconServerAssignProcessNodeToRequestServer(node, NULL);

			node->state = BPNS_WAITING_FOR_CLIENT_TO_RECEIVE_BEACON_FILE;
		}else{
			beaconServerCancelProcessNode(node);
		}
	}else{
		beaconServerSendNeedMoreBeaconFile(client);
	}
}

static void processClientMsgNeedMoreBeaconFile(BeaconServerClientData* client, Packet* pak){
	if(	client->requester.processNode &&
		client->requester.processNode->state == BPNS_WAITING_FOR_CLIENT_TO_RECEIVE_BEACON_FILE)
	{
		beaconServerSendBeaconFileToRequester(client, 0);
	}
}

static void beaconServerSendRegenerateRequest(BeaconServerClientData* client){
	if(!client){
		return;
	}

	// Tell the requester that the request needs to be regenerated.

	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_REGENERATE_MAP_DATA);
	BEACON_SERVER_PACKET_SEND();
}

static void processClientMsgRequestedMapLoadFailed(BeaconServerClientData* client, Packet* pak){
	BeaconProcessQueueNode* node = client->requestServer.processNode;
	U32 nodeUID;
	const char* uniqueStorageName;

	if(	!client->server.isRequestServer ||
		!node)
	{
		return;
	}

	// Check the node's UID.

	nodeUID = pktGetBitsPack(pak, 1);

	if(nodeUID != node->uid){
		return;
	}

	// Check the node's uniqueStorageName.

	uniqueStorageName = pktGetStringTemp(pak);

	if(	!node->uniqueStorageName ||
		stricmp(node->uniqueStorageName, uniqueStorageName))
	{
		return;
	}

	// Tell the requester to recreate the map data packet.

	beaconServerSendRegenerateRequest(node->requester);

	// Kill the process node.

	beaconServerCancelProcessNode(node);
}

static void processClientMsgPing(BeaconServerClientData* client, Packet* pak){
	if(client->clientType==BCT_SERVER)
	{
		if(client->server.protocolVersion>=3)
		{
			StructReset(parse_BeaconServerStateDetails, &client->server.stateDetails);
			ParserRecv(parse_BeaconServerStateDetails, pak, &client->server.stateDetails, 0);
		}
	}
}

static void processClientMsgDebug(BeaconServerClientData* client, Packet* pak){
	Packet *pkt;

	if(pktEnd(pak))
	{
		return;
	}

	if(g_debugger_link)
	{
		pkt = pktCreate(g_debugger_link, BDMSG_DATA);
		if(pkt)
		{
			pktSendPacket(pkt, pak);
			pktSend(&pkt);
			linkFlush(g_debugger_link);
		}
	}
}

static void beaconServerUnassignClients(BeaconServerClientData *server)
{
	if(server->clientType != BCT_SERVER)
		return;

	FOR_EACH_IN_EARRAY(server->server.clients, BeaconServerClientData, worker)
	{
		worker->client.assignedTo = NULL;
	}
	FOR_EACH_END

	eaClear(&server->server.clients);
}

static void processClientMsgMapCompleted(BeaconServerClientData *client, Packet *pak)
{
	beaconServerUnassignClients(client);
//	beaconServerCullExtraneousBeacons();
}

static void processClientMsgUnassign(BeaconServerClientData *client, Packet* pak)
{
	if(client->clientType!=BCT_WORKER)
		return;

	if(client->client.assignedTo)
		eaFindAndRemoveFast(&client->client.assignedTo->server.clients, client);
	client->client.assignedTo = NULL;	
}

static void beaconServerProcessClientMsgTextCmd(BeaconServerClientData* client, const char* textCmd, Packet* pak){
	#define BEGIN_HANDLERS()	if(0){
	#define HANDLER(x, y)		}else if(!stricmp(textCmd, x)){y(client, pak)
	#define END_HANDLERS()		}

	BEGIN_HANDLERS();
		HANDLER(BMSG_C2ST_READY_TO_WORK,				processClientMsgReadyToWork				);
		HANDLER(BMSG_C2ST_NEED_MORE_MAP_DATA,			processClientMsgNeedMoreMapData			);
		HANDLER(BMSG_C2ST_MAP_DATA_IS_LOADED,			processClientMsgMapDataIsLoaded			);
		HANDLER(BMSG_C2ST_NEED_MORE_EXE_DATA,			processClientMsgNeedMoreExeData			);
		HANDLER(BMSG_C2ST_GENERATE_FINISHED,			processClientMsgGenerateFinished		);
		HANDLER(BMSG_C2ST_BEACON_CONNECTIONS,			processClientMsgBeaconConnections		);
		HANDLER(BMSG_C2ST_SERVER_STATUS,				processClientMsgServerStatus			);
		HANDLER(BMSG_C2ST_REQUESTER_MAP_DATA,			processClientMsgRequesterMapData		);
		HANDLER(BMSG_C2ST_USER_INACTIVE,				processClientMsgUserInactive			);
		HANDLER(BMSG_C2ST_BEACON_FILE,					processClientMsgBeaconFile				);
		HANDLER(BMSG_C2ST_NEED_MORE_BEACON_FILE,		processClientMsgNeedMoreBeaconFile		);
		HANDLER(BMSG_C2ST_REQUESTED_MAP_LOAD_FAILED,	processClientMsgRequestedMapLoadFailed	);
		HANDLER(BMSG_C2ST_MAP_COMPLETED,				processClientMsgMapCompleted			);
		HANDLER(BMSG_C2ST_UNASSIGN,						processClientMsgUnassign				);
		HANDLER(BMSG_C2ST_PING,							processClientMsgPing					);
		HANDLER(BMSG_C2ST_DEBUG_MSG,					processClientMsgDebug					);
	END_HANDLERS();

	#undef BEGIN_HANDLERS
	#undef HANDLER
	#undef END_HANDLERS
}

static void beaconServerProcessClientMsg(Packet* pak, S32 cmd, NetLink* link, BeaconServerClientData* client){

	client->receivedPingTime = beaconGetCurTime();

	switch(cmd){
		xcase BMSG_C2S_CONNECT:{
			processClientMsgConnect(client, pak);
		}

		xcase BMSG_C2S_SERVER_CONNECT:{
			processClientMsgServerConnect(client, pak);
		}

		xcase BMSG_C2S_REQUESTER_CONNECT:{
			processClientMsgRequesterConnect(client, pak);
		}

		xcase BMSG_C2S_TEXT_CMD:{
			char textCmd[1000];

			Strncpyt(textCmd, pktGetStringTemp(pak));

			beaconServerProcessClientMsgTextCmd(client, textCmd, pak);
		}

		xdefault:{
			beaconClientPrintf(client, COLOR_RED, "ERROR: Client sent unknown cmd(%d).\n", cmd);
			beaconServerSetClientState(client, BCS_ERROR_PROTOCOL);
		}
	}
}

int beaconServerDebugCombatPlace(void)
{
	return beacon_server.debug_state && beacon_server.debug_state->send_combat;
}

void beaconServerSendDebugPoint(int msg, const Vec3 p, int ARGB)
{
	if(g_debugger_link)
	{
		Packet *pak = pktCreate(g_debugger_link, BDMSG_DATA);

		pktSendBits(pak, 32, msg);
		SEND_POINT(p, ARGB);
		
		pktSend(&pak);
	}
}

void beaconServerSendDebugLine(int msg, Vec3 p1, Vec3 p2, int ARGB)
{
	if(g_debugger_link)
	{
		Packet *pak = pktCreate(g_debugger_link, BDMSG_DATA);

		pktSendBits(pak, 32, msg);
		SEND_LINE_VEC3(p1, p2, ARGB);
		
		pktSend(&pak);
	}
}

BeaconProcessDebugState* beaconServerGetProcessDebugState(void)
{
	return beacon_server.debug_state;
}

static int beaconResetClient(BeaconServerClientData* client, S32 index, void* unused){
	removeClientFromBlock(client);
	removeClientFromGroup(client);

	client->completed.beaconCount = 0;
	client->completed.blockCount = 0;

	switch(client->state){
		case BCS_READY_TO_WORK:
		case BCS_RECEIVING_MAP_DATA:
		case BCS_READY_TO_GENERATE:
		case BCS_GENERATING:
		case BCS_READY_TO_CONNECT_BEACONS:
		case BCS_PIPELINE_CONNECT_BEACONS:
		case BCS_CONNECTING_BEACONS:
			beaconServerSetClientState(client, BCS_CONNECTED);
			beaconServerSendConnectReply(client, 1, 0);
	}

	return 1;
}

//typedef void (*WorldCollObjectTraverseCB)(void* userPointer,
//					const WorldCollObjectTraverseParams* params);


static void wcoCounter(void *userdata, const WorldCollObjectTraverseParams* params)
{
	(*(U32*)userdata)++;
}

static void beaconServerSaveLocation(const char* mapFileName)
{
	char mapnameFile[MAX_PATH];
	FILE* f;

	sprintf(mapnameFile, "%slastmap.txt", beaconGetExeDirectory());

	f = fopen(mapnameFile, "w");

	if(!f)
		return;

	fwrite(mapFileName, 1, strlen(mapFileName), f);

	fclose(f);
}

ZoneMapInfo* zmapInfoFind(const char* fileOrPublic)
{
	RefDictIterator iter;
	ZoneMapInfo *zminfo;

	worldGetZoneMapIterator(&iter);
	while (zminfo = worldGetNextZoneMap(&iter))
	{
		if(!stricmp(fileOrPublic, zmapInfoGetPublicName(zminfo)))
			return zminfo;
		if(!stricmp(fileOrPublic, zmapInfoGetFilename(zminfo)))
			return zminfo;
	}

	return NULL;
}

static void beaconServerReadLocationFile(void)
{
	int i;
	char mapnameFile[MAX_PATH];
	char mapname[MAX_PATH];
	ZoneMapInfo *zmFile;
	FILE* f;

	sprintf(mapnameFile, "%slastmap.txt", beaconGetExeDirectory());

	f = fopen(mapnameFile, "r");

	if(!f)
		return;

	fgets(mapname, ARRAY_SIZE_CHECKED(mapname), f);

	zmFile = zmapInfoFind(mapname);
	if(!zmFile)
	{
		fclose(f);
		return;
	}

	for(i=0; i<eaSize(&beacon_server.queueList); i++)
	{
		ZoneMapInfo *zmQueue = zmapInfoFind(beacon_server.queueList[i]);

		if(zmQueue==zmFile)
		{
			beacon_server.curMapIndex = i;
			fclose(f);
			return;
		}
	}

	fclose(f);
}

static void beaconServerClearSavedLocation(void)
{
	char filename[MAX_PATH];

	sprintf(filename, "%slastmap.txt", beaconGetExeDirectory());
	fileForceRemove(filename);
}

static void beaconServerLoadMap(const char* mapFileName){
	U32 cnt =0;
	U32 layercount;
	int i;
	int iPartitionIdx = worldGetAnyCollPartitionIdx();

	beaconDestroyObjects();
	worldResetWorldGrid();
	eaClear(&beacon_server.statusMessages);
	eaClear(&playableEnts);

	wcTraverseObjects(worldGetActiveColl(iPartitionIdx), wcoCounter, &cnt, NULL, NULL, /*unique=*/1, WCO_TRAVERSE_STATIC);

	layercount = zmapGetLayerCount(worldGetActiveMap());
	for(i=0; i<(int)layercount; i++)
	{
		assert(zmapGetLayerTracker(worldGetActiveMap(), i)==NULL);
	}

	assert(cnt==0);

	wcStoredModelDataDestroyAll();

	if(beacon_server.isAutoServer || beacon_server.isPseudoAuto)
		beaconServerSaveLocation(mapFileName);

	mpCompactPools();

	if(fileExistsInList(beacon_server.skipList, mapFileName, NULL)){
		beaconPrintf(COLOR_YELLOW|COLOR_BRIGHT, "Map skipped: %s\n", mapFileName);
		return;
	}

	if(!worldLoadZoneMapByNameSyncWithPatching(mapFileName))
		return;

	beacon_server.fileBeaconCount = combatBeaconArray.size;
	beacon_server.fileConnectionCount = 0;
	for(i=0; i<combatBeaconArray.size; i++)
	{
		Beacon *b = (Beacon*)combatBeaconArray.storage[i];
		beacon_server.fileConnectionCount += b->gbConns.size;
		beacon_server.fileConnectionCount += b->rbConns.size;
	}

	beaconClearBeaconData(); // All I really wanted was the counts...
	beaconClearCRCData();

	wcForceSimulationUpdate();
	eaClear(&encounterBeaconArray);
	eaClear(&invalidEncounterArray);

	if(eaSize(&playableEnts))
	{
		eaPush(&beacon_server.statusMessages, "Used playable volumes");
	}

	beaconPreProcessMap();

	if(beacon_process.isSpaceMap)
	{
		beacon_server.defConfig->FlatAreaCircleCutoffMin = 160;
		beacon_server.defConfig->FlatAreaCircleCutoffMax = 512;

		combatBeaconGridBlockSize = 1024;
	}
	else
	{
		beacon_server.defConfig->FlatAreaCircleCutoffMin = beacon_server.defCutoffMin;
		beacon_server.defConfig->FlatAreaCircleCutoffMax = beacon_server.defCutoffMax;

		combatBeaconGridBlockSize = 256;
	}

	beaconGatherSpawnPositions(0);
	beaconServerCreateSpaceBeacons();
	beaconFileGatherMetaData(iPartitionIdx, 1, beacon_server.fullCRCInfo);

	beaconCurTimeString(1);

	//worldCreatePartition(0, true);
}

static void beaconServerResetMapData(void){
	beaconServerForEachClient(beaconResetClient, NULL);

	beaconResetMapData();

	beaconClearBeaconData();

	beaconMapDataPacketDestroy(&beacon_server.mapData);

	beacon_server.fullInfoOnce = 0;
}

static const char* beaconServerGetPathAtMaps(const char* mapFileName){
	const char* fileNameAtMaps = strstriConst(mapFileName, "maps/");

	if(!fileNameAtMaps){
		fileNameAtMaps = strstriConst(mapFileName, "maps\\");
	}

	if(	fileNameAtMaps &&
		(	fileNameAtMaps == mapFileName ||
			fileNameAtMaps[-1] == '/' ||
			fileNameAtMaps[-1] == '\\'))
	{
		return fileNameAtMaps;
	}else{
		return mapFileName;
	}
}

static void beaconServerGetBeaconLogFile(	char* fileNameOut,
											S32 fileNameOut_size,
											const char* dir,
											const char* fileName)
{
	char	buffer[1000];

	if(fileName){
		strcpy(buffer, fileName);
	}else{
		const char*	fileNameAtMaps = beaconServerGetPathAtMaps(beacon_server.curMapName);
		char*		s;

		strcpy(buffer, fileNameAtMaps);

		forwardSlashes(buffer);

		for(s = buffer; *s; s++){
			if(*s == '/' || *s == '\\' || *s == ':'){
				*s = '.';
			}
		}
	}

	sprintf_s(	SAFESTR2(fileNameOut),
				"c:/beaconizer/info/branch%d/%s/%s",
				beacon_server.gimmeBranchNum,
				dir,
				buffer);
}

static StashTable openMapLogFileHandles;

static void beaconServerDeleteMapLogFile(const char* dir){
	char fileName[MAX_PATH];
	
	beaconServerGetBeaconLogFile(SAFESTR(fileName), dir, NULL);
	
	DeleteFile_UTF8(fileName);
}

static FILE* beaconServerGetMapLogFileHandle(const char* dir){
	StashElement element;
	FILE* f;
	
	if(!openMapLogFileHandles){
		openMapLogFileHandles = stashTableCreateWithStringKeys(100, StashDeepCopyKeys_NeverRelease);
	}
	
	if(stashFindElementConst(openMapLogFileHandles, dir, &element)){
		f = stashElementGetPointer(element);
		
		assert(f);
	}else{
		char fileName[MAX_PATH];
		
		beaconServerGetBeaconLogFile(SAFESTR(fileName), dir, NULL);
		
		makeDirectoriesForFile(fileName);
		
		f = fopen(fileName, "wt");
		
		if(!f){
			return NULL;
		}
		
		fprintf(f, "loadmap %s\n", beaconServerGetPathAtMaps(beacon_server.curMapName));

		stashAddPointer(openMapLogFileHandles, dir, f, 0);
	}
	
	return f;
}

static void beaconServerCloseMapLogFileHandle(FILE* f){
	fclose(f);
}

static void beaconServerCloseMapLogFileHandles(void){
	if(!openMapLogFileHandles){
		return;
	}
	
	stashTableClearEx(openMapLogFileHandles, NULL, beaconServerCloseMapLogFileHandle);
}

static void beaconServerWriteMapLog(const char* dir, const char* format, ...){
	FILE* f = beaconServerGetMapLogFileHandle(dir);
	
	if(!f){
		return;
	}

	VA_START(argptr, format);
	vfprintf(f, format, argptr);
	VA_END();
}

void beaconServerSetGimmeUsage(S32 on){
	beacon_server.noGimmeUsage = on ? 0 : 1;
}

static void beaconServerSendDebugReset(void)
{
	if(g_debugger_link)
	{
		Packet *pak = pktCreate(g_debugger_link, BDMSG_RESET);
		if(pak)
		{
			pktSend(&pak);
		}
	}
}

static S32 beaconServerBeginMapProcess(char* mapFileName){
	S32 canceled = 0;

	ZeroStruct(beacon_server.clientTicks);

	beaconServerResetMapData();

	beaconServerLoadMap(mapFileName);

	if(!worldGetActiveMap())
	{
		canceled = 1;
	}
	else 
	{
		// Create the filenames.
		beaconFileMakeFilenames(NULL, beaconFileGetCurVersion());

		if(!beacon_server.isRequestServer)
		{
			// Ensure the beacon files exist / get latest.
			if(	!beacon_server.noGimmeUsage &&
				!beaconEnsureFilesExist(beacon_server.forceRebuild))
			{
				canceled = 1;
			}
		}
		else if(beacon_server.isRequestServer)
		{
			if(beaconFileIsUpToDate(0))
			{
				if(beacon_server.request.allow_skip)
					beacon_server.skip_processing = true;
				else
				{
					beaconPrintf(COLOR_GREEN, "CANCELED: Map is up to date");
					canceled = 1;
				}
			}
		}
	}

	if(beacon_server.checkEncounterPositionsOnly){
		canceled = 1;

		beaconPrintf(COLOR_RED, "CANCELED: Check Encounter Positions Only is ON!\n");
	}

	if(canceled){
		return 0;
	}

	beaconServerSendDebugReset();

	if(!beacon_server.minimalPrinting)
	{
		printf("Map CRC: 0x%8.8x 0x%8.8x 0x%8.8x\n", 
			beacon_process.mapMetaData->geoCRC,
			beacon_process.mapMetaData->cfgCRC,
			beacon_process.mapMetaData->encCRC);
	}

	beacon_server.mapDataCRC = beaconCalculateGeoCRC(worldGetActiveColl(worldGetAnyCollPartitionIdx()), false);
	beaconMapDataPacketFromMapData(worldGetActiveColl(worldGetAnyCollPartitionIdx()), &beacon_server.mapData, beacon_server.fullCRCInfo);

	return 1;
}

S32 fileExistsInList(const char** mapList, const char* findName, S32* index){
	S32 i;
	S32 size = eaSize(&mapList);
	for(i = 0; i < size; i++){
		if(!stricmp(mapList[i], findName)){
			if(index){
				*index = i;
			}
			return 1;
		}
	}
	return 0;
}

static S32 zmapInfoIsPublicOrHasGroupPrivacy(ZoneMapInfo *zminfo)
{
	const char **privateTo = zmapInfoGetPrivacy(zminfo);
	if(!eaSize(&privateTo))
	{
		return 1;
	}
	else
	{
		int i;

		for(i=0; i<eaSize(&privateTo); i++)
		{
			if(IsGroupName(privateTo[i]))
			{
				return 1;
			}
		}
	}

	return 0;
}

static void beaconServerReadFileList(const char*** fileList, const char *fileName)
{
	eaClear(fileList);
	if(fileExists(fileName))
	{
		int file_len = 0;
		FILE *file = fileOpen(fileName, "r");

		if(file)
		{
			char line[MAX_PATH];

			while(fgets(line, ARRAY_SIZE(line), file))
			{
				ZoneMapInfo *zminfo;
				char *str = NULL;
				str = estrCreateFromStr(line);
				estrTrimLeadingAndTrailingWhitespace(&str);
				if((zminfo = zmapInfoFind(str)) && !zmapInfoGetNoBeacons(zminfo))
				{
					if(zmapInfoIsPublicOrHasGroupPrivacy(zminfo))
						eaPush(fileList, zmapInfoGetPublicName(zminfo));
					else
						eaPush(fileList, zmapInfoGetFilename(zminfo));
				}
			}
		}
	}
}

static void beaconServerLoadMapListFile(char* fileName, S32 addToQueue)
{
	if(fileExists(fileName))
	{
		int file_len = 0;
		FILE *file = fileOpen(fileName, "r");

		if(file)
		{
			char line[MAX_PATH];

			while(fgets(line, ARRAY_SIZE(line), file))
			{
				char *str = NULL;
				ZoneMapInfo *zminfo = NULL;
				str = estrCreateFromStr(line);
				estrTrimLeadingAndTrailingWhitespace(&str);
				zminfo = zmapInfoFind(str);
				if(zminfo)
				{
					if(zmapInfoGetNoBeacons(zminfo))
						continue;

					if(!zmapInfoIsPublicOrHasGroupPrivacy(zminfo))
						continue;

					beaconServerQueueMapFile(zmapInfoGetPublicName(zminfo));
				}
			}
		}
	}
}

static void beaconServerLoadLocalMapList(void)
{
	char* mapListLocal = "c:/maps_local.txt";

	beaconServerLoadMapListFile(mapListLocal, 0);
}

static char* beaconServerMakeNetworkListName(const char* fileType)
{
	static char fileName[MAX_PATH];

	sprintf(fileName, "N:/Beaconizer/%s/%s.txt", GetProductName(), fileType);

	return fileName;
}

static void beaconServerLoadPrivateMapList(void)
{
	beaconServerLoadMapListFile(beaconServerMakeNetworkListName("mapList"), 0);
}

static void beaconServerLoadMapFileList(S32 justLocal){
	char* mapListFileDir = "server/maps/beaconprocess";

	beaconServerReadFileList(&beacon_server.skipList, beaconServerMakeNetworkListName("skipList"));

	beaconServerLoadLocalMapList();

	if(!justLocal){
		RefDictIterator iter;
		ZoneMapInfo *zminfo;
		printf("Reading map file names from spec files:\n");
		beaconServerLoadPrivateMapList();

		worldGetZoneMapIterator(&iter);
		while (zminfo = worldGetNextZoneMap(&iter))
		{
			if(!stricmp(zmapInfoGetPublicName(zminfo), "EmptyMap"))
				continue;

			if(beaconHasSpaceRegion(zminfo))
				continue;

			if(beaconHasRegionType(zminfo, WRT_CharacterCreator))
				continue;

			if(!zmapInfoGetNoBeacons(zminfo) && zmapInfoIsPublicOrHasGroupPrivacy(zminfo))
				beaconServerQueueMapFile(zmapInfoGetPublicName(zminfo));
		}
	}

	beacon_server.curMapIndex = 0;

	eaQSort(beacon_server.queueList, strCmp);

	printf("Added %d map files to the list.\n", eaSize(&beacon_server.queueList));
}

static const char* getClientStateName(BeaconClientState state){
	static S32 init;

	if(!init){
		S32 i;

		init = 1;

		for(i = 0; i < BCS_COUNT; i++){
			getClientStateName(i);
		}
	}

	switch(state){
		#define CASE(x) xcase x:return #x + 4
		CASE(BCS_NOT_CONNECTED);
		CASE(BCS_CONNECTED);
		CASE(BCS_READY_TO_WORK);
		CASE(BCS_RECEIVING_MAP_DATA);
		CASE(BCS_NEEDS_MORE_MAP_DATA);
		CASE(BCS_READY_TO_GENERATE);
		CASE(BCS_GENERATING);
		CASE(BCS_READY_TO_CONNECT_BEACONS);
		CASE(BCS_PIPELINE_CONNECT_BEACONS);
		CASE(BCS_CONNECTING_BEACONS);
		CASE(BCS_CLIENT_COUNT);

		CASE(BCS_SENTRY);
		CASE(BCS_SERVER);
		CASE(BCS_NEEDS_MORE_EXE_DATA);
		CASE(BCS_RECEIVING_EXE_DATA);

		CASE(BCS_REQUESTER);

		CASE(BCS_REQUEST_SERVER_IDLE);
		CASE(BCS_REQUEST_SERVER_PROCESSING);

		CASE(BCS_ERROR_DATA);
		CASE(BCS_ERROR_PROTOCOL);
		CASE(BCS_ERROR_NONLOCAL_IP);
		#undef CASE
		xdefault:{
			assert(0);
			return "UNKNOWN";
		}
	}
}

static U32 getClientStateColor(BeaconClientState state){
	switch(state){
		case BCS_ERROR_DATA:
		case BCS_ERROR_PROTOCOL:
			return COLOR_BRIGHT|COLOR_RED;
		case BCS_NEEDS_MORE_EXE_DATA:
		case BCS_RECEIVING_MAP_DATA:
		case BCS_SENTRY:
			return COLOR_BRIGHT|COLOR_RED|COLOR_GREEN;
		case BCS_ERROR_NONLOCAL_IP:
			return COLOR_RED|COLOR_GREEN;
		case BCS_SERVER:
			return COLOR_BRIGHT|COLOR_GREEN;
		default:
			return COLOR_RED|COLOR_GREEN|COLOR_BLUE;
	}
}

static void beaconServerCheckWindow(void){
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
			POINT	pt;
			RECT	rectDesktop;

			if(	GetWindowRect(GetDesktopWindow(), &rectDesktop) &&
				GetCursorPos(&pt) &&
				SQR(pt.x - rectDesktop.right) + SQR(pt.y - rectDesktop.top) < SQR(10))
			{
				ShowWindow(beaconGetConsoleWindow(), SW_RESTORE);
			}
		}
	}
}

static void beaconServerWindowThread(void* unused){
	while(1){
		beaconServerCheckWindow();
		Sleep(500);
	}
}

static void beaconServerSetIcon(U8 letter, U32 colorRGB){
	if(letter){
		beacon_server.icon.letter = letter;
	}
	
	if(colorRGB){
		beacon_server.icon.color = colorRGB;
	}
	
	if(beacon_server.icon.letter){
		setWindowIconColoredLetter(	beaconGetConsoleWindow(),
									beacon_server.icon.letter,
									beacon_server.icon.color);
	}
}

static void beaconServerGetLatestData(void){
	S32 error;
	const char* cmd1 =	"gimme"
						" -ignoreerrors"
						" -nodaily"
						" -nocomments"
						" -glvfold c:/Night/tools";

	char cmd2[1000];
	
	sprintf(cmd2,
			"gimme"
			" -ignoreerrors"
			" -nodaily"
			" -nocomments"
			" -undofold %s/server/maps"
			" -glvfold %s"
			" -glvfold %s",
			beaconServerGetDataPath(),
			beaconServerGetToolsPath(),
			beaconServerGetDataPath());

	beaconPrintf(COLOR_GREEN, "Getting latest data:\n  Cmd1: %s\n  Cmd2: %s\n\n", cmd1, cmd2);

	{S32 unused=_chdir("c:\\game\\");}

	error = system(cmd1);

	assertmsg(!error, "Gimme returned an error!");

	error = system(cmd2);

	assertmsg(!error, "Gimme returned an error!");
	
	// Restore the window icon.
	
	beaconServerSetIcon(0, 0);

	beaconPrintf(COLOR_GREEN, "\nDone getting latest data!\n\n");
}

void beaconServerReleaseMasterMutex(void)
{
	beaconReleaseAndCloseMutex(&beacon_server.mutexMaster);
}

static S32 beaconServerAcquireMasterMutex(void)
{
	beacon_server.mutexMaster = CreateMutex(NULL, 0, L"Global\\CrypticBeaconMasterServer");

	assert(beacon_server.mutexMaster);

	if(beaconAcquireMutex(beacon_server.mutexMaster)){
		return 1;
	}

	beaconServerReleaseMasterMutex();

	return 0;
}

static void beaconServerInitNetwork(void)
{
	if(beaconIsMasterServer() && !beaconServerAcquireMasterMutex())
	{
		printfColor(COLOR_RED, "Failed to acquire master server mutex");
		beacon_server.dontRestartOnAssert = true;
		assert(0);
	}

	if(!beacon_comm)
	{
		S32 basePort = beacon_server.isMasterServer ? BEACON_MASTER_SERVER_PORT : BEACON_SERVER_PORT;
		S32 portRange = beacon_server.isMasterServer ? 1 : 101;
		S32 i;

		beacon_comm = commCreate(0,3);
		//commSetSendTimeout(beacon_comm, 1000);
		beaconPrintf(COLOR_GREEN, "Initializing network: ");

		if(beacon_server.isMasterServer && !beaconIsSharded())
		{
			beaconServerMakeNewExecutable();
		}

		// Initialize networking.

		for(i = 0; i < portRange; i++){
			S32 portToTry = basePort + i;

			if(	!beacon_server.isMasterServer &&
				portToTry == BEACON_MASTER_SERVER_PORT)
			{
				continue;
			}

			beacon_server.clients = commListenIp(	beacon_comm, LINKTYPE_FLAG_DISCONNECT_ON_FULL | LINKTYPE_FLAG_RESIZE_AND_WARN | LINKTYPE_SIZE_1MEG, 
													LINK_FORCE_FLUSH, 
													portToTry, 
													beaconServerProcessClientMsg, 
													beaconServerConnectCallback, 
													beaconServerDisconnectCallback, 
													sizeof(BeaconServerClientData),
													getHostLocalIp());
			commSetSendTimeout(beacon_comm, 60);
			if(beacon_server.clients){
				break;
			}
		}

		if(i == portRange){
			beaconPrintf(	COLOR_RED,
							"ERROR: Can't bind any ports in range %d-%d!\n",
							basePort,
							basePort + portRange - 1);
			beacon_server.dontRestartOnAssert = true;
			assert(0);
		}

		beacon_server.port = basePort + i;

		beaconPrintf(COLOR_GREEN, "  Done (port %d)!\n", beacon_server.port);
	}
}

#define BUFSIZE 1000
static char* beaconMyCmdLineParams(S32 noNetStart){
	static char* buffer;

	if(!buffer){
		buffer = beaconMemAlloc("buffer", BUFSIZE);
		
		assert(buffer);
	}

	STR_COMBINE_BEGIN_S(buffer, BUFSIZE);
	if(beacon_server.isMasterServer){
		STR_COMBINE_CAT(" -beaconmasterserver");

		if(noNetStart){
			STR_COMBINE_CAT(" -beaconnonetstart");
		}

		if(estrLength(&beacon_server.requestCacheDir)){
			STR_COMBINE_CAT(" -beaconrequestcachedir ");
			STR_COMBINE_CAT(beacon_server.requestCacheDir);
		}
	}
	else if(beacon_server.isAutoServer){
		STR_COMBINE_CAT(" -beaconautoserver");

		if(beacon_common.masterServerName){
			STR_COMBINE_CAT(" -useMasterS ");
			STR_COMBINE_CAT(beacon_common.masterServerName);
		}
	}
	else if(beacon_server.isRequestServer){
		STR_COMBINE_CAT(" -beaconrequestserver");

		if(beacon_common.masterServerName){
			STR_COMBINE_CAT(" ");
			STR_COMBINE_CAT(beacon_common.masterServerName);
		}
	}
	else{
		STR_COMBINE_CAT(" -beaconserver");

		if(beacon_common.masterServerName){
			STR_COMBINE_CAT(" -useMasterS ");
			STR_COMBINE_CAT(beacon_common.masterServerName);
		}
	}
	if(	!beacon_server.isMasterServer &&
		beacon_server.noGimmeUsage)
	{
		STR_COMBINE_CAT(" -beaconnogimme");
	}
	if(beacon_server.dataToolsRootPath){
		STR_COMBINE_CAT(" -beacondatatoolsrootpath \"");
		STR_COMBINE_CAT(beacon_server.dataToolsRootPath);
		STR_COMBINE_CAT("\"");
	}

	if(beacon_server.useLocalSrc)
	{
		STR_COMBINE_CAT(" -useLocalSrc");
	}

	STR_COMBINE_END(buffer);

	beaconGetCommonCmdLine(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer));

	return buffer;
}
#undef BUFSIZE

static void beaconServerInstall(void){
	HANDLE hMutex;
	DWORD result;

	if(beaconIsSharded()){
		return;
	}

	return;

	hMutex = CreateMutex(NULL, 0, L"Global\\CrypticBeaconServerInstall");

	assert(hMutex);

	WaitForSingleObjectWithReturn(hMutex, 0, result);

	if(result == WAIT_OBJECT_0 || result == WAIT_ABANDONED){
		char buffer[1000];

		sprintf(buffer, "c:/Beaconizer/%s", beaconServerExeName);

		if(stricmp(buffer, beaconGetExeFileName())){
			beaconCreateNewExe(buffer, beacon_server.exeFile.data, beacon_server.exeFile.size);
		}
	}

	beaconReleaseAndCloseMutex(&hMutex);
}

static void beaconServerAddToSymStore(const char* fileName){
	char buffer[1000];
	char fileNameBackSlashes[1000];
	strcpy(fileNameBackSlashes, fileName);
	backSlashes(fileNameBackSlashes);
	beaconPrintf(COLOR_YELLOW, "Adding \"%s\" to symstore (\\\\somnus\\data\\symserv\\dataCryptic)...\n", fileName);
	sprintf(buffer, "CrypticSymStore add \\\\somnus\\data\\symserv\\dataCryptic \"%s\"", fileNameBackSlashes);
	beaconPrintfDim(COLOR_YELLOW, "Command: \"%s\"\n", buffer);
	system(buffer);
}

static void beaconServerStartupSetServerType(BeaconizerType beaconizerType, int pseudo){
	beacon_server.type = beaconizerType;
	switch(beaconizerType){
		xcase BEACONIZER_TYPE_MASTER_SERVER:
			beaconServerSetIcon('M', BEACON_MASTER_SERVER_ICON_COLOR);
			beacon_server.isMasterServer = 1;

		xcase BEACONIZER_TYPE_AUTO_SERVER:
			beaconServerSetIcon('A', BEACON_SERVER_ICON_COLOR);
			beacon_server.isAutoServer = 1;

		xcase BEACONIZER_TYPE_SERVER:
			beaconServerSetIcon('S', BEACON_SERVER_ICON_COLOR);
			beacon_server.isPseudoAuto = pseudo;

		xcase BEACONIZER_TYPE_REQUEST_SERVER:
			beaconServerSetIcon('R', BEACON_SERVER_ICON_COLOR);
			beacon_server.isRequestServer = 1;

		xdefault:
			assertmsg(0, "Bad beacon server type!");
	}

}

static void beaconConfigChangedCallback(const char *relpath, int when)
{
	if(!strstri(relpath, "server/beaconizer"))
		return;

	//ParserReloadFileToDictionary(relpath, beacon_server.mapConfigDict);
}

BeaconProcessConfig* beaconServerGetProcessConfig(void)
{
	const char *public_name = zmapInfoGetPublicName(NULL);
	BeaconProcessConfig *config = RefSystem_ReferentFromString(beacon_server.mapConfigDict, public_name);

	if(config)
		return config;

	return beacon_server.defConfig;
}

static char* beaconServerGetServerType(BeaconizerType type)
{
	switch(type)
	{
		xcase BEACONIZER_TYPE_MASTER_SERVER: return "Master";
		xcase BEACONIZER_TYPE_REQUEST_SERVER: return "Request";
		xcase BEACONIZER_TYPE_AUTO_SERVER: return "AutoSub";
		xcase BEACONIZER_TYPE_SERVER: return "ManualSub";
	}
	
	return "Auto?";
}

static void beaconServerRestartAutoServer(char* commandLine)
{
	char exe[1000];

	sprintf(exe, "cmd /c start /D%s %s %s", backSlashes(beaconGetExeDirectory()), backSlashes(beaconGetExeFileName()), commandLine);

	system_detach(exe, 1, 0);
}

static void beaconServerDisconnectClientCallback(BeaconServerClientData* client, S32 index, char* reason){
	beaconServerDisconnectClient(client, reason);
}

static void beaconServerRestartAutoServerAndDie(const char* msg, int clearSaved)
{
	int i;
	beaconPrintf(COLOR_YELLOW, msg);
	beaconPrintf(COLOR_YELLOW, "Restarting self and exiting in: ");

	if(clearSaved)
		beaconServerClearSavedLocation();

	beaconServerRestartAutoServer(beaconMyCmdLineParams(0));

	beaconServerForEachClient(beaconServerDisconnectClientCallback, (char*)msg);

	for(i = 5; i; i--){
		beaconPrintf(COLOR_GREEN, "%d, ", i);

		Sleep(1000);
	}

	beaconPrintf(COLOR_GREEN, "BYE!\n\n\n\n\n");
#undef exit		// I am tired of setProgramIsShuttingDown not working.  Just die.
	exit(0);
}

static void beaconServerRestartMasterServer(char* commandLine)
{
	char exe[MAX_PATH];

	beaconServerReleaseMasterMutex();

	sprintf(exe, "cmd /c start /D%s %s %s", backSlashes(beaconGetExeDirectory()), backSlashes(beaconGetExeFileName()), commandLine);
	
	system_detach(exe, 1, 0);
}

static void beaconServerRestartMasterServerAndDie(const char* msg)
{
	int i;
	beaconPrintf(COLOR_YELLOW, msg);
	beaconPrintf(COLOR_YELLOW, "Restarting self and exiting in: ");

	beaconServerRestartMasterServer(beaconMyCmdLineParams(0));

	for(i = 5; i; i--){
		beaconPrintf(COLOR_GREEN, "%d, ", i);

		Sleep(1000);
	}

	beaconPrintf(COLOR_GREEN, "BYE!\n\n\n\n\n");

#undef exit		// I am tired of setProgramIsShuttingDown not working.  Just die.
	exit(0);
}

static void beaconServerAssertCallback(char *errMsg)
{
	char *title = NULL;
	char *msg = NULL;

	estrStackCreate(&title);
	estrStackCreate(&msg);
	estrPrintf(&msg, "%s", errMsg);

	estrPrintf(&title, "CRASH: %s Branch %d - Server %s, Type %s",
		GetProductName(),
		beacon_server.gimmeBranchNum,
		getComputerName(),
		beaconServerGetServerType(beacon_server.type));

	beaconServerSendEmailMsg(title, &msg, "beaconizer-users@" ORGANIZATION_DOMAIN); // this one is an error so it goes to the human beaconizer list.

	estrDestroy(&title);
	estrDestroy(&msg);

	if(beacon_server.dontRestartOnAssert)
		return;

	if(beacon_server.isAutoServer)
		beaconServerRestartAutoServerAndDie("Map crashed...", 0);
	else if(beacon_server.isMasterServer)
		beaconServerRestartMasterServerAndDie("Crashed...");
}

static void beaconServerCreatePCLClient(const char *path)
{
	beacon_common.ccfunc(	&beacon_server.pcl_client, 
							beacon_server.patchServerName ? 
								beacon_server.patchServerName : 
								BEACON_DEFAULT_PATCHSERVER,
							BEACON_PATCH_SERVER_PORT, 
							300,
							beacon_comm,
							path,
							NULL,
							NULL,
							NULL,
							NULL);
}

void beaconServerChangeMaster(void)
{
	linkRemove_wReason(&beacon_server.master_link, "Master changed");
}

S32 beaconServerUpdateStatus(const char* mapname)
{
	BeaconServerMapStatus *stat = StructCreate(parse_BeaconServerMapStatus);

	if(!worldLoadZoneMapByName(mapname))
	{
		stat->failedToLoad = true;
		stat->mapName = mapname;
	}
	else
	{
		stat->mapName = StructAllocString(zmapInfoGetPublicName(NULL));

		stat->fileData = StructCreate(parse_BeaconMapMetaData);
		stat->mapData = StructCreate(parse_BeaconMapMetaData);
		beaconFileGatherMetaData(1, true, false);

		if (!beacon_process.fileMetaData)
		{
			beaconReadDateFile(NULL);
		}
		assert(beacon_process.fileMetaData);
		StructCopyAll(parse_BeaconMapMetaData, beacon_process.fileMetaData, stat->fileData);
		StructCopyAll(parse_BeaconMapMetaData, beacon_process.mapMetaData, stat->mapData);

		if(!beaconFileMetaDataMatch(stat->fileData, stat->mapData, true))
			stat->needsProcess = true;
	}

	eaPush(&beacon_server.serverStatus.mapStatus, stat);

	beacon_server.statusDirty = true;

	return stat->needsProcess;
}

void beaconServerStartup(	BeaconizerType beaconizerType,
							const char* masterServerName,
							S32 noNetStart,
							S32 pseudo)
{
	S32 processPriority = NORMAL_PRIORITY_CLASS;
	S32 threadPriority	= THREAD_PRIORITY_NORMAL;

	pushDontReportErrorsToErrorTracker(1);

	// Minimize the window.
	
	ShowWindow(beaconGetConsoleWindow(), SW_MINIMIZE);

	// Make sure the state list is complete.

	getClientStateName(0);

	sharedMemorySetMode(SMM_DISABLED);

	if(beaconIsProductionMode()){
		beaconPrintf(COLOR_GREEN, "BeaconServer is in PRODUCTION MODE.\n");
	}

	beaconServerStartupSetServerType(beaconizerType, pseudo);

	if(!beacon_server.isMasterServer){
		if(masterServerName){
			beacon_common.masterServerName = strdup(masterServerName);

			beaconPrintf(COLOR_GREEN, "Using master server:   %s\n", masterServerName);
		}
	
		beaconPrintf(COLOR_GREEN, "Using data/tools root: %s\n", beaconServerGetDataToolsRootPath());
		beaconPrintf(COLOR_GREEN, "Data path:             %s\n", fileDataDir());
	}
	
	if(!beaconIsSharded())
	{
		setAssertCallback(beaconServerAssertCallback);
	}

	printf("Server IP: %s\n", makeIpStr(getHostLocalIp()));

	srand(time(NULL) + _getpid());

	estrPrintf(	&beacon_server.serverUID,
				"ip%s_pid%d_time%"FORM_LL"d_rand%d",
				makeIpStr(getHostLocalIp()),
				(int)_getpid(),
				time(NULL),
				(int)rand() % 1000);

	printf("ServerUID: %s\n", beacon_server.serverUID);

	// Initialize networking.

	if(!noNetStart){
		beaconServerInitNetwork();
	}

	beacon_server.noGimmeUsage = 0;

	if(beacon_server.isMasterServer){
		// Set the default request cache directory if it isn't set from the command line.

		if(!estrLength(&beacon_server.requestCacheDir)){
			beaconServerSetRequestCacheDir("c:\\beaconizer\\requestcache\\");
		}

		beaconPrintf(COLOR_GREEN, "Using beacon request cache dir: %s\n", beacon_server.requestCacheDir);

		// Start the background loader thread.

		beaconServerInitLoadRequests();

		// Start the background request writer/reader thread.

		beaconServerInitProcessQueue();

	}else{
		if(!beacon_server.isRequestServer){
			if(!beaconIsSharded()){
				int branch;
				S32 defaultBranch;
				beacon_server.gimmeBranchNum = gimmeDLLQueryBranchNumber(beaconServerGetDataPath());
				beacon_common.cvfunc(beaconServerGetDataToolsRootPath(), 0, 0, &beacon_server.patcherTime, &branch, 0, 0, 0, 0);

				beaconServerCreatePCLClient(beaconServerGetDataToolsRootPath());
				beaconServerPCLWait(beacon_server.pcl_client);
				
				beacon_common.dvfunc(beacon_server.pcl_client, GetProductName(), 0, NULL, NULL);
				beaconServerPCLWait(beacon_server.pcl_client);
				beacon_common.gbfunc(beacon_server.pcl_client, &defaultBranch);

				if(branch!=beacon_server.gimmeBranchNum)
				{
					beacon_server.patcherTime = 0;		// Patch time is invalid
				}

				if(defaultBranch == beacon_server.gimmeBranchNum)
					beacon_server.allowPrivateMaps = 1;

				beaconPrintf(COLOR_GREEN, "Gimme branch: %d\n", beacon_server.gimmeBranchNum);

				beacon_common.ddfunc(beacon_server.pcl_client);
			}

			beaconPrintf(COLOR_GREEN, "Gimme enabled: ");

			if(beacon_server.noGimmeUsage){
				beaconPrintf(COLOR_RED, "NO!\n");
			}else{
				beaconPrintf(COLOR_GREEN, "YES\n");
			}

			beacon_server.loadMaps = 1;
		}
		else
		{
			if(beacon_server.request.projectName)
				beacon_server.loadMaps = 1;
			beacon_server.noGimmeUsage = 1;
		}

		processPriority = BELOW_NORMAL_PRIORITY_CLASS;
	}

	SetPriorityClass(GetCurrentProcess(), processPriority);
	SetThreadPriority(GetCurrentThread(), threadPriority);

	_beginthread(beaconServerWindowThread, 0, NULL);

	// CRC the executable.

	beacon_server.exeFile.crc = beaconGetExeCRC(beaconGetExeFileName(), &beacon_server.exeFile.data, &beacon_server.exeFile.size);
	assert(beacon_server.exeFile.data);
	printf("CRC of \"%s\" = 0x%8.8x\n", beaconGetExeFileName(), beacon_server.exeFile.crc);

	// Init common stuff for client & server.

	beaconInitCommon();

	if(!beacon_server.isMasterServer)
	{
		if(beacon_server.isPseudoAuto || beacon_server.isAutoServer)
		{
			beaconServerUpdateData();  // Must update data before worldlib loads or bad stuff happens
		}
		
		beacon_server.defConfig = StructCreate(parse_BeaconProcessConfig);
		ParserReadTextFile("server/beaconizer/defaultprocess.cfg", parse_BeaconProcessConfig, beacon_server.defConfig, 0);
		beacon_server.mapConfigDict = RefSystem_RegisterSelfDefiningDictionary("MapProcessConfigs", false, parse_BeaconProcessConfig, true, true, NULL);
		ParserLoadFilesToDictionary("server/beaconizer/MapConfigs", ".cfg", "server/MapConfigs.bin", PARSER_OPTIONALFLAG, beacon_server.mapConfigDict);

		beacon_server.defCutoffMin = beacon_server.defConfig->FlatAreaCircleCutoffMin;
		beacon_server.defCutoffMax = beacon_server.defConfig->FlatAreaCircleCutoffMax;

		if(beacon_server.isRequestServer)
			beacon_server.defConfig->angleProcessIncrement = 10;  // Request maps are less well connected, but much faster
		
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "*.cfg", beaconConfigChangedCallback);
		FolderCacheSetManualCallbackMode(1);

		beaconInitCommonWorld();

		wlSetLoadFlags(WL_NO_LOAD_COSTUMES|WL_NO_LOAD_DYNFX|WL_NO_LOAD_DYNANIMATIONS);
		wlSetIsServer(true);

		AutoStartup_SetTaskIsOn("WorldLib", 1);
		AutoStartup_SetTaskIsOn("Beaconizer", 1);
		//AutoStartup_SetTaskIsOn("PetContactLists", 0);
		AutoStartup_RemoveDependency("WorldLibZone", "PetContactLists");
		AutoStartup_RemoveAllDependenciesOn("Messages");
		AutoStartup_RemoveAllDependenciesOn("AnimLists");
		loadstart_printf("Starting BeaconServer...");
		DoAutoStartup();
		loadend_printf("done");

		resFinishLoading();
		beaconInitCommonPostWorld();
	}

	if(!beacon_server.isMasterServer){
		int i;
		beacon_server.stateDetails.lastCheckedIn = -1;
		beacon_server.curMapIndex = 0;

		if(beacon_server.isAutoServer){
			beaconServerLoadMapFileList(0);

			beaconServerReadLocationFile();
		}
		else if(!beacon_server.isRequestServer){
			// Load local map list.

			beaconServerLoadMapFileList(1);
		}
		else if(beacon_server.isRequestServer && beacon_server.request.projectName){
			// Scan project
			ZoneMapInfo **zminfos = NULL;
			worldGetZoneMapsInNamespace(beacon_server.request.projectName, &zminfos);

			for(i=0; i<eaSize(&zminfos); i++)
			{
				ZoneMapInfo *zminfo = zminfos[i];

				if(zmapInfoHasSpaceRegion(zminfo))
					continue;

				beaconServerQueueMapFile(zmapInfoGetPublicName(zminfo));
			}
			eaDestroy(&zminfos);
		}
	}

	// Enable folder cache callbacks so that getting latest version at runtime will work properly.

	FolderCacheEnableCallbacks(1);

	if(	!beacon_server.isMasterServer &&
		!beaconIsSharded())
	{
		beaconProcessSetConsoleCtrlHandler(1);
	}

	if(!beacon_server.isMasterServer)
	{
		beacon_common.onMasterChangeFunc = beaconServerChangeMaster;
	}

	// Disable beacon loading popup errors.

	beaconReaderDisablePopupErrors(1);

	printf(	"\n\n[ÄÄÄÄÄÄÄÄÄÄÄÄ BEACON %sSERVER RUNNING %sÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ]\n",
			beacon_server.isMasterServer ? "MASTER " :
				beacon_server.isRequestServer ? "REQUEST " :
					"",
			beacon_server.isMasterServer ? "" : "ÄÄÄÄÄÄÄ");

	beaconCurTimeString(1);
}

static int beaconServerGetLongestRequesterWaitHelper(NetLink *link, int index, void *user_data, void *func_data)
{
	BeaconServerClientData *client = user_data;
	U32 *longest = func_data;

	if(	client->clientType == BCT_REQUESTER &&
		client->requester.startedRequestTime)
	{
		U32 cur = beaconTimeSince(client->requester.startedRequestTime);

		*longest = MAX(cur, *longest);
	}

	return 1;
}

static U32 beaconServerGetLongestRequesterWait(void){
	U32 longest = 0;

	linkIterate2(beacon_server.clients, beaconServerGetLongestRequesterWaitHelper, &longest);

	return longest;
}

typedef struct BeaconServerUpdateTitleInfoStruct
{
	S32 		serverCount;
	S32			requestServerCount;
	S32 		userInactiveCount;
	S32 		userActiveCount;
	S32 		sentryCount;
	S32 		requesterCount;
	S32 		noSentryCount;
	S32 		noClientCount;
	S32 		crashedCount;
	S32			updatingCount;
} BeaconServerUpdateTitleInfoStruct;

static int beaconServerUpdateTitleHelper(NetLink *link, int index, void *link_data, void *func_data)
{
	BeaconServerClientData *client = link_data;
	BeaconServerUpdateTitleInfoStruct *info = func_data;
	BeaconServerMachineData *machine = beaconServerFindMachine(link, false);

	if(	client->state == BCS_NEEDS_MORE_EXE_DATA ||
		client->state == BCS_RECEIVING_EXE_DATA)
	{
		info->updatingCount++;
	}

	switch(client->clientType)
	{
		xcase BCT_SERVER:{
			if(client->server.isRequestServer){
				info->requestServerCount++;
			}else{
				info->serverCount++;
			}
		}

		xcase BCT_SENTRY:{
			info->sentryCount++;
		}

		xcase BCT_WORKER:{
			if(!client->client.assignedTo && machine->sentry && machine->sentry->forcedInactive){
				info->userInactiveCount++;
			}else{
				info->userActiveCount++;
			}
		}
	}

	return 1;
}

static int cmp8(U8 *v0, U8 *v1, int len) 
{
	int i, ret = 0;
	for (i = 0; i < len && ret == 0; i++)
	{
		ret = v0[i] - v1[i];
	}
	return ret;
}

U32 compareAndCopyInfos(BeaconServerUpdateTitleInfoStruct *infoNew, BeaconServerUpdateTitleInfoStruct *infoOld)
{
	U32 changed = cmp8((U8*)infoNew, (U8*)infoOld, sizeof(BeaconServerUpdateTitleInfoStruct));

	memmove(infoOld, infoNew, sizeof(BeaconServerUpdateTitleInfoStruct));

	return changed;
}

static void beaconServerUpdateTitle(const char* format, ...){
	//const char* timeString = beaconCurTimeString(0);
	char		buffer[1000];
	char*		pos = buffer;
	char		countBuffer[1000] = "";
	char*		countPos = countBuffer;
	U32			longestWait;
	BeaconServerUpdateTitleInfoStruct info = {0};
	BeaconServerUpdateTitleInfoStruct lastinfo = {0};

	/*
	while(timeString[4] && (timeString[0] == '0' || timeString[0] == ':')){
		timeString++;
	}
	*/

	linkIterate2(beacon_server.clients, beaconServerUpdateTitleHelper, &info);

	/*
	if(!changed && !compareAndCopyInfos(&info, &lastinfo))
	{
		return;
	}
	*/

	// Paused.

	if(beacon_server.paused){
		pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, "[PAUSED] ");
	}

	// Status ACK

	if(linkConnected(beacon_server.master_link) && !beacon_server.status.acked){
		pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, "[Status Not Acked!]");
	}

	// Server type.

	pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, "%s-BeaconServer:",
					beacon_server.isMasterServer ? "Master" :
						beacon_server.isAutoServer ? "Auto" :
							beacon_server.isRequestServer ? "Request" :
								"Manual");

	// PID.

	pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, "%d - [", _getpid());

	// Time.

	//pos += sprintf_s(pos, buffer + sizeof(buffer) - pos, " (Time %s): [", timeString);

	// Master server client counts.

	if(beacon_server.isMasterServer){
		pos += snprintf_s(pos, buffer + sizeof(buffer) - pos,
						"%dlr,%dss,%ds,%dr,%drs,%dpn,",
						beaconServerGetLoadRequestCount(),
						info.serverCount,
						info.sentryCount,
						beacon_server.clientList[BCT_REQUESTER].count,
						info.requestServerCount,
						beacon_server.processQueue.count);
	}

	// Inactive clients.

	pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, "%di,", info.userInactiveCount);

	// Active clients.

	pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, "%da]", info.userActiveCount);

	// Requester wait time.

	longestWait = beaconServerGetLongestRequesterWait();

	if(longestWait){
		pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, " (Longest wait: %d:%2.2d)", longestWait / 60, longestWait % 60);
	}

	// Crashes.

	if(info.crashedCount){
		countPos += snprintf_s(countPos, countBuffer + sizeof(countBuffer) - countPos,
							"%s%d CRASHED!!!",
							countBuffer[0] ? ", " : "",
							info.crashedCount);
	}

	// Count of sentries that are active but have no clients.
	
	if(info.noClientCount){
		countPos += snprintf_s(countPos, countBuffer + sizeof(countBuffer) - countPos,
							"%s%d uncliented",
							countBuffer[0] ? ", " : "",
							info.noClientCount);
	}
	
	// Count of clients that are updating.
	
	if(info.updatingCount){
		countPos += snprintf_s(countPos, countBuffer + sizeof(countBuffer) - countPos,
							"%s%d updating",
							countBuffer[0] ? ", " : "",
							info.updatingCount);
	}

	if(countBuffer[0]){
		pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, " (%s)", countBuffer);
	}

	if(format){
		va_list argptr;

		pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, " (");
		va_start(argptr, format);
		pos += vsprintf_s(pos, buffer + sizeof(buffer) - pos, format, argptr);
		va_end(argptr);
		pos += snprintf_s(pos, buffer + sizeof(buffer) - pos, ")");
	}

	setConsoleTitle(buffer);
}

static int dynArrayFind(void **basePtr, int count, void *target)
{
	int i;

	for(i=0; i<count; i++)
		if(basePtr[i]==target)
			return i;
	
	return -1;
}

static BeaconDiskSwapBlock* beaconGetLegalBlockWithLeastClients(BeaconServerClientData *client){
	BeaconDiskSwapBlock* block;
	BeaconDiskSwapBlock* leastClients = NULL;

	if(beacon_server.debug_state && beacon_server.debug_state->debug_dist)
	{
		// Seek out blocks close to debug pos
		BeaconDiskSwapBlock *closest = NULL;
		int min_dist;
		int blockTarget[2];

		blockTarget[0] = beacon_server.debug_state->debug_pos[0]/BEACON_GENERATE_CHUNK_SIZE;
		blockTarget[1] = beacon_server.debug_state->debug_pos[2]/BEACON_GENERATE_CHUNK_SIZE;

		for(block=bp_blocks.list; block; block = block->nextSwapBlock){
			if(block->legalCompressed.uncheckedCount > 0){
				int gx = block->x / BEACON_GENERATE_CHUNK_SIZE;
				int gz = block->z / BEACON_GENERATE_CHUNK_SIZE;
				int dist = abs(gx-blockTarget[0]) + abs(gz-blockTarget[1]);

				if(dynArrayFind(block->clients.clients, block->clients.count, client)!=-1)
					continue;
				
				if(!closest)
				{
					min_dist = dist;
					closest = block;
				}
				else
				{
					if(dist < min_dist && block->clients.count==0)
					{
						min_dist = dist;
						closest = block;
					} else if(dist <= min_dist && timerSeconds(timerCpuTicks() - block->clients.assignedTime) > 45) {
						min_dist = dist;
						closest = block;
					}
				}
			}
		}

		return closest;
	}

	for(block = bp_blocks.list; block; block = block->nextSwapBlock){
		if(dynArrayFind(block->clients.clients, block->clients.count, client)!=-1)
			continue;

		if(block->legalCompressed.uncheckedCount > 0){
			if(!block->clients.count){
				return block;
			}
			else if(timerSeconds(timerCpuTicks() - block->clients.assignedTime) > 5.0){
				if(!leastClients || block->clients.count < leastClients->clients.count){
					leastClients = block;
				}
			}
		}
	}

	return leastClients;
}

static void beaconServerSendLegalAreas(Packet* pak, BeaconDiskSwapBlock* block){
	BeaconLegalAreaCompressed* area;

	beaconCheckDuplicates(block);

	pktSendBits(pak, 1, block->foundCRC);

	if(block->foundCRC){
		pktSendBits(pak, 32, block->surfaceCRC);
	}

	pktSendBitsPack(pak, 16, block->legalCompressed.totalCount);

	for(area = block->legalCompressed.areasHead; area; area = area->next){
		pktSendBits(pak, 8, area->x);
		pktSendBits(pak, 8, area->z);
		pktSendBits(pak, 1, area->checked);

		if(area->isIndex){
			pktSendBits(pak, 1, 1);
			pktSendBitsPack(pak, 5, area->y_index);
		}else{
			pktSendBits(pak, 1, 0);
			pktSendF32(pak, area->y_coord);
		}

		pktSendBitsPack(pak, 1, area->areas.count);

		#if BEACONGEN_STORE_AREAS
		{
			S32 i;
			for(i = 0; i < area->areas.count; i++){
				pktSendF32(pak, area->areas.areas[i].y_min);
				pktSendF32(pak, area->areas.areas[i].y_max);

				#if BEACONGEN_CHECK_VERTS
				{
					S32 j;

					for(j = 0; j < 3; j++){
						pktSendF32(pak, area->areas.areas[i].triVerts[j][0]);
						pktSendF32(pak, area->areas.areas[i].triVerts[j][1]);
						pktSendF32(pak, area->areas.areas[i].triVerts[j][2]);
					}

					pktSendString(pak, area->areas.areas[i].defName);
				}
				#endif
			}
		}
		#endif

		#if BEACONGEN_CHECK_VERTS
		{
			S32 i;

			for(i = 0; i < area->tris.count; i++){
				S32 j;
				pktSendBits(pak, 1, 1);
				pktSendString(pak, area->tris.tris[i].defName);
				pktSendF32(pak, area->tris.tris[i].y_min);
				pktSendF32(pak, area->tris.tris[i].y_max);
				for(j = 0; j < 3; j++){
					pktSendF32(pak, area->tris.tris[i].verts[j][0]);
					pktSendF32(pak, area->tris.tris[i].verts[j][1]);
					pktSendF32(pak, area->tris.tris[i].verts[j][2]);
				}
			}

			pktSendBits(pak, 1, 0);
		}
		#endif

		#if BEACONGEN_STORE_AREA_CREATOR
			pktSendBitsPack(pak, 1, area->areas.cx);
			pktSendBitsPack(pak, 1, area->areas.cz);
			pktSendBits(pak, 32, area->areas.ip);
		#endif
	}
}

static void beaconAssignBlockToClient(BeaconServerClientData* client){
	BeaconDiskSwapBlock* block = beaconGetLegalBlockWithLeastClients(client);

	if(block){
		BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_PROCESS_LEGAL_AREAS);
			S32 grid_x = block->x / BEACON_GENERATE_CHUNK_SIZE;
			S32 grid_z = block->z / BEACON_GENERATE_CHUNK_SIZE;

			if(!beacon_server.minimalPrinting)
			{
				beaconClientPrintf(	client, 0,
									"Assigning to block (%4d,%4d), %d legal areas, %d unchecked.\n",
									grid_x,
									grid_z,
									block->legalCompressed.totalCount,
									block->legalCompressed.uncheckedCount);
			}

			beaconCheckDuplicates(block);

			pktSendBitsPack(pak, 1, grid_x);
			pktSendBitsPack(pak, 1, grid_z);
			beaconServerSendLegalAreas(pak, block);

		BEACON_SERVER_PACKET_SEND();

		if(!block->clients.count){
			block->clients.assignedTime = timerCpuTicks();
		}

		dynArrayAddp(block->clients.clients, block->clients.count, block->clients.maxCount, client);

		client->assigned.block = block;

		beaconServerSetClientState(client, BCS_GENERATING);
	}
}

static void beaconSendBeaconsToClient(BeaconServerClientData* client)
{
	Packet *pak = BEACON_SERVER_PACKET_CREATE_VS(client, BMSG_S2CT_BEACON_LIST);
	S32 i;

	pktSendBitsPack(pak, 1, combatBeaconArray.size);

	for(i = 0; i < combatBeaconArray.size; i++){
		Beacon* b = combatBeaconArray.storage[i];

		devassert(b->globalIndex==i);
		pktSendVec3(pak, b->pos);

		pktSendBits(pak, 1, b->noGroundConnections);
	}

	if(client->protocolVersion >= 5)
	{
		if(combatBeaconArray.size>100000)
			pktSendBitsAuto(pak, 0);
		else
			pktSendBitsAuto(pak, 1);
	}

	if(client->protocolVersion <= 4 || client->protocolVersion>=5 && combatBeaconArray.size<100000)
	{
		for(i=0; i<combatBeaconArray.size; i++)
		{
			int j;
			Beacon* b = combatBeaconArray.storage[i];

			pktSendBitsAuto(pak, b->gbConns.size);
			for(j=0; j<b->gbConns.size; j++)
			{
				BeaconConnection *conn = b->gbConns.storage[j];
				pktSendBitsAuto(pak, conn->destBeacon->globalIndex);
			}

			pktSendBitsAuto(pak, b->rbConns.size);
			for(j=0; j<b->rbConns.size; j++)
			{
				BeaconConnection *conn = b->rbConns.storage[j];
				pktSendBitsAuto(pak, conn->destBeacon->globalIndex);
			}
		}
	}

	BEACON_SERVER_PACKET_SEND_WAIT_VS(pak, client);

	beaconServerSetClientState(client, BCS_READY_TO_CONNECT_BEACONS);
}

MP_DEFINE(BeaconConnectBeaconGroup);

BeaconConnectBeaconGroup* createBeaconConnectBeaconGroup(void){
	MP_CREATE(BeaconConnectBeaconGroup, 100);
	return MP_ALLOC(BeaconConnectBeaconGroup);
}

void destroyBeaconConnectBeaconGroup(BeaconConnectBeaconGroup* group){
	MP_FREE(BeaconConnectBeaconGroup, group);
}

static void beaconServerAddBeaconGroupTail(BeaconConnectBeaconGroup* group){
	if(!beacon_server.beaconConnect.groups.availableHead){
		beacon_server.beaconConnect.groups.availableHead = group;
	}else{
		beacon_server.beaconConnect.groups.availableTail->next = group;
	}

	group->next = NULL;
	beacon_server.beaconConnect.groups.availableTail = group;
	beacon_server.beaconConnect.groups.count++;
}

typedef struct GetClientStateCountStruct
{
	S32 count;
	BeaconClientState state;
} GetClientStateCountStruct;

static int getClientStateCountHelper(NetLink *link, int index, void *link_data, void *func_data)
{
	BeaconServerClientData *client = link_data;
	GetClientStateCountStruct *s = func_data;

	if(client->state == s->state)
	{
		s->count++;
	}

	return 1;
}

static S32 getClientStateCount(BeaconClientState state){
	GetClientStateCountStruct s;
	s.count = 0;
	s.state = state;

	linkIterate2(beacon_server.clients, getClientStateCountHelper, &s);

	return s.count;
}

static void beaconServerCreateBeaconGroups(S32 forceGroup){
	int groupSize = 5;
	U32 groupFound = 0;

	while(beacon_server.beaconConnect.legalBeacons.groupedCount <= ea32Size(&beacon_server.beaconConnect.legalBeacons.indices) - groupSize){
		BeaconConnectBeaconGroup* group = createBeaconConnectBeaconGroup();

		group->lo = beacon_server.beaconConnect.legalBeacons.groupedCount;
		group->hi = group->lo + groupSize - 1;
		beacon_server.beaconConnect.legalBeacons.groupedCount = group->hi + 1;

		beaconServerAddBeaconGroupTail(group);

		groupFound = 1;
	}

	if(	!groupFound &&
		forceGroup &&
		beacon_server.beaconConnect.legalBeacons.groupedCount < ea32Size(&beacon_server.beaconConnect.legalBeacons.indices))
	{
		S32 waitingCount = getClientStateCount(BCS_READY_TO_CONNECT_BEACONS) + getClientStateCount(BCS_PIPELINE_CONNECT_BEACONS);
		S32 remaining = ea32Size(&beacon_server.beaconConnect.legalBeacons.indices) - beacon_server.beaconConnect.legalBeacons.groupedCount;

		groupSize = remaining / waitingCount;

		if(groupSize <= 0){
			groupSize = 1;
		}

		while(remaining){
			BeaconConnectBeaconGroup* group = createBeaconConnectBeaconGroup();

			group->lo = beacon_server.beaconConnect.legalBeacons.groupedCount;
			group->hi = group->lo + groupSize - 1;
			if(group->hi >= ea32Size(&beacon_server.beaconConnect.legalBeacons.indices)){
				group->hi = ea32Size(&beacon_server.beaconConnect.legalBeacons.indices) - 1;
			}
			beacon_server.beaconConnect.legalBeacons.groupedCount += group->hi - group->lo + 1;
			remaining -= group->hi - group->lo + 1;

			beaconServerAddBeaconGroupTail(group);
		}
	}
}

static int getUnfinishedGroupWithLeastClientsHelper(NetLink *link, int index, void* link_data, void* func_data)
{
	BeaconServerClientData *client = link_data;
	BeaconConnectBeaconGroup **leastClients = func_data;
	BeaconConnectBeaconGroup *group = client->assigned.group;

	if(group)
		assert(!group->finished);

	if(group && timerSeconds(timerCpuTicks() - group->clients.assignedTime) > 5.0)
	{
		if(!*leastClients || eaSize(&group->clients.clients) < eaSize(&(*leastClients)->clients.clients))
		{
			*leastClients = group;
		}
	}

	return 1;
}

static BeaconConnectBeaconGroup* getUnfinishedGroupWithLeastClients(void){
	BeaconConnectBeaconGroup* leastClients = NULL;

	linkIterate2(beacon_server.clients, getUnfinishedGroupWithLeastClientsHelper, &leastClients);

	return leastClients;
}

static void beaconAssignBeaconGroupToClient(BeaconServerClientData* client){
	S32 count;
	S32 i;
	BeaconConnectBeaconGroup *group = NULL;

	if(!beacon_server.beaconConnect.groups.availableHead){
		beaconServerCreateBeaconGroups(1);
	}

	assert(!client->assigned.group || !client->assigned.pipeline);

	if(beacon_server.beaconConnect.groups.availableHead){
		group = beacon_server.beaconConnect.groups.availableHead;
		assert(!group->finished);
		beacon_server.beaconConnect.groups.availableHead = group->next;
		if(!beacon_server.beaconConnect.groups.availableHead){
			beacon_server.beaconConnect.groups.availableTail = NULL;
		}
	}else{
		group = getUnfinishedGroupWithLeastClients();
	}

	if(group){
		group->next = NULL;
	}

	if(!group){
		return;
	}

	if(group==client->assigned.group || group==client->assigned.pipeline)
		return;

	assert(!group->finished);

	if(!beacon_server.minimalPrinting)
	{
		beaconClientPrintf(	client,
							COLOR_BLUE,
							"%s %3d beacons [%6d-%6d]\n",
							client->state==BCS_READY_TO_CONNECT_BEACONS ? "Assigning " : "Pipelining",
							group->hi - group->lo + 1,
							group->lo,
							group->hi);
	}

	// Send the packet to the client.
	if(client->state==BCS_READY_TO_CONNECT_BEACONS)
	{
		beaconServerAssignGroup(client, group, __FILE__, __LINE__);
	}
	else if(client->state==BCS_PIPELINE_CONNECT_BEACONS)
	{
		beaconServerAssignPipeline(client, group, __FILE__, __LINE__);
	}
	else
		assert(0);

	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_CONNECT_BEACONS);
		count = group->hi - group->lo + 1;

		if(client->protocolVersion>=4)
		{
			if(client->state==BCS_READY_TO_CONNECT_BEACONS)
				pktSendBitsPack(pak, 1, 0);
			else
				pktSendBitsPack(pak, 1, 1);

			pktSendBitsPack(pak, 1, group->lo);
			pktSendBitsPack(pak, 1, group->hi);
		}
		pktSendBitsPack(pak, 1, count);

		for(i = group->lo; i <= group->hi; i++){
			S32 index = beacon_server.beaconConnect.legalBeacons.indices[i];
			pktSendBitsPack(pak, 1, index);
		}
	BEACON_SERVER_PACKET_SEND();

	if(client->state==BCS_READY_TO_CONNECT_BEACONS)
		beaconServerSetClientState(client, BCS_PIPELINE_CONNECT_BEACONS);
	else
		beaconServerSetClientState(client, BCS_CONNECTING_BEACONS);
}

void beaconServerDisconnectClient(BeaconServerClientData* client, const char* reason){
	if(	client &&
		client->link &&
		!linkDisconnected(client->link) &&
		linkConnected(client->link) &&
		!client->disconnected)
	{
		client->disconnected = 1;
		beaconServerSetClientState(client, BCS_NOT_CONNECTED);

		if(reason){
			if(!beacon_server.minimalPrinting)
				beaconClientPrintf(client, COLOR_RED, "Disconnecting: %s\n", reason);
		}

		linkRemove(&client->link);
	}
}

void beaconServerCountAssigned(BeaconServerClientData* client)
{
	if(beacon_server.state==BSS_CONNECT_BEACONS)
	{
		if(client->assigned.group)
			beacon_server.beaconConnect.assignedCount++;
		if(client->assigned.pipeline)
			beacon_server.beaconConnect.assignedCount++;
	}
}

static void beaconServerProcessClientPaused(BeaconServerClientData* client,
											S32 index,
											BeaconServerProcessClientsData* pcd)
{
	beaconServerCountAssigned(client);

	switch(client->state){
		xcase BCS_ERROR_NONLOCAL_IP:{
			if(!beacon_server.localOnly){
				beaconServerDisconnectClient(client, "Local IP is no longer required.");
			}
		}
	}
}

static S32 beaconServerNeedsSomeClients(BeaconServerClientData* subServer){
	return	estrLength(&subServer->server.mapName) &&
			subServer->server.sendClientsToMe;
}

static S32 beaconServerRequestServerIsAvailable(BeaconServerClientData* requestServer){
	return	requestServer->server.isRequestServer &&
			!requestServer->requestServer.processNode;
}

static int beaconServerGetAvailableRequestServerHelper(NetLink* link, int index, void* link_data, void* func_data)
{
	BeaconServerClientData *client = link_data;
	BeaconServerClientData **dataOut = func_data;
	if(beaconServerRequestServerIsAvailable(client)){
		*dataOut = client;
		return 0;
	}

	return 1;
}

static BeaconServerClientData* beaconServerGetAvailableRequestServer(void){
	BeaconServerClientData *d = NULL;
	
	linkIterate2(beacon_server.clients, beaconServerGetAvailableRequestServerHelper, &d);

	return d;
}

static void beaconServerPingClient(BeaconServerClientData* client, S32 index, void* userData){
	if(beaconTimeSince(client->sentPingTime) > 5)
	{
		client->sentPingTime = beaconGetCurTime();

		BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_PING);
		BEACON_SERVER_PACKET_SEND();

		if(beacon_server.debug_state)
		{
			BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_DEBUGSTATE);
				pktSendStruct(pak, beacon_server.debug_state, parse_BeaconProcessDebugState);
			BEACON_SERVER_PACKET_SEND();
		}
	}
}

static void beaconServerProcessClient(	BeaconServerClientData* client,
										S32 index,
										BeaconServerProcessClientsData* pcd)
{
	beaconServerProcessClientPaused(client, index, pcd);

	beaconServerPingClient(client, index, NULL);

	switch(client->state){
		xcase BCS_NEEDS_MORE_EXE_DATA:{
			if(pcd->sentExeDataCount < 5){
				pcd->sentExeDataCount++;

				beaconServerSendNextExeChunk(client);
			}
		}

		xcase BCS_RECEIVING_EXE_DATA:{
			if(beaconTimeSince(client->exeData.lastCommTime) < 3){
				pcd->sentExeDataCount++;
			}
		}

		xcase BCS_READY_TO_WORK:{
			//fixme2 newnet pktSetDebugInfo();

			client->mapData.sentByteCount = 0;

			beaconServerSendMapDataToWorker(client);
		}

		xcase BCS_READY_TO_GENERATE:{
			switch(beacon_server.state){
				xcase BSS_GENERATING:{
					beaconAssignBlockToClient(client);
				}

				xcase BSS_CONNECT_BEACONS:{
					beaconSendBeaconsToClient(client);
				}
			}
		}

		xcase BCS_READY_TO_CONNECT_BEACONS:
		acase BCS_PIPELINE_CONNECT_BEACONS: {
			if(beacon_server.state == BSS_CONNECT_BEACONS){
				assert(!client->assigned.group || !client->assigned.pipeline);
				beaconAssignBeaconGroupToClient(client);
			}
		}

		xcase BCS_RECEIVING_MAP_DATA:{
			break;
			if(beaconTimeSince(client->mapData.lastCommTime) < 3){
				pcd->sentMapDataCount++;
			}
		}

		xcase BCS_NEEDS_MORE_MAP_DATA:{
			break;
			if(pcd->sentMapDataCount < 20){
				pcd->sentMapDataCount++;
				
				beaconServerSendMapDataToWorker(client);
			}
		}
	}
}

static void beaconServerDisconnectErrorNonLocalIP(BeaconServerClientData* client, S32 index, void* userData){
	if(client->state == BCS_ERROR_NONLOCAL_IP){
		beaconServerDisconnectClient(client, "Local IP is no longer required.");
	}
}

static S32 clientStateIsVisible(BeaconServerClientData* client){
	return 0;
}

static const char* beaconGetServerStateName(BeaconServerState state){
	switch(state){
		case BSS_NOT_STARTED:
			return "NOT_STARTED";
		case BSS_GENERATING:
			return "GENERATING";
		case BSS_CONNECT_BEACONS:
			return "CONNECT_BEACONS";
		case BSS_WRITE_FILE:
			return "WRITE_FILE";
		case BSS_SEND_BEACON_FILE:
			return "SEND_BEACON_FILE";
		case BSS_DONE:
			return "DONE";
		default:
			return "UNKNOWN";
	}
}

static void printClientState(BeaconServerClientData* client, S32 index, void* userData)
{
	void* ptrs[2] = {client, NULL};
	char buffer[1000];
	U32 elapsedTime;
	S32 bgcolor = 0;
	char* crashText = NULL;
	S32 isCrashed = 0;
	S32 crashedCount = 0;
	S32 notConnectedCount = 0;
	S32 userInactiveCount = 0;
	S32 i;
	BeaconServerMachineData *machine = beaconServerFindMachine(client->link, false);

	if(!clientStateIsVisible(client)){
		return;
	}

	for(i = 0; i < eaSize(&machine->clients); i++){
		BeaconServerClientData *sub = machine->clients[i];

		if(sub->crashText){
			crashedCount++;
		}

		if(sub->forcedInactive){
			userInactiveCount++;
		}
	}

	printf("ÃÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ\n");

	printf("³%s", index == beacon_server.selectedClient ? ">" : " ");

	
	if(client->crashText)
	{
		crashText = client->crashText;
		bgcolor = COLOR_RED;
	}

	if(index == beacon_server.selectedClient){
		bgcolor = COLOR_BLUE;
	}

	switch(client->clientType){
		xcase BCT_SERVER:
			consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, bgcolor);
		xcase BCT_SENTRY:
			consoleSetColor(COLOR_BRIGHT|COLOR_GREEN|COLOR_BLUE, bgcolor);
		xcase BCT_WORKER:
			consoleSetColor(COLOR_YELLOW, bgcolor);
		xcase BCT_REQUESTER:
			consoleSetColor(COLOR_BRIGHT|COLOR_YELLOW, bgcolor);
	}

	printf("%-17s", getClientIPStr(client));

	consoleSetColor(getClientStateColor(client->state), bgcolor);

	printf("%-25s", getClientStateName(client->state));

	consoleSetDefaultColor();

	switch(client->clientType){
		xcase BCT_SERVER:
			printf("Protocol: %d", client->server.protocolVersion);
			beaconPrintf(COLOR_GREEN, " | ");
			printf("Clients: %d", client->server.clientCount);
			beaconPrintf(COLOR_GREEN, " | ");
			printf("State: %s", beaconGetServerStateName(client->server.state));

		xdefault:
			consoleSetColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN|COLOR_BLUE, bgcolor);

			buffer[0] = 0;

			if(crashText){
				sprintf(buffer, "CRASHED!!!");
			}
			else if(client->assigned.block){
				sprintf(buffer,
						"(%d,%d)",
						client->assigned.block->x / BEACON_GENERATE_CHUNK_SIZE,
						client->assigned.block->z / BEACON_GENERATE_CHUNK_SIZE);
			}
			else if(client->assigned.group){
				sprintf(buffer,
						"(%d-%d)",
						client->assigned.group->lo,
						client->assigned.group->hi);
			}
			
			printf("%-18s", buffer[0] ? buffer : "");

			consoleSetColor(COLOR_BRIGHT|COLOR_RED|COLOR_GREEN|COLOR_BLUE, 0);

			if(client->completed.blockCount){
				printf("%-12d", client->completed.blockCount);
			}else{
				printf("%12s", "");
			}

			if(client->completed.beaconCount){
				printf("%-12d", client->completed.beaconCount);
			}else{
				printf("%12s", "");
			}
	}

	// Next line.

	consoleSetDefaultColor();

	printf("\n³%s", index == beacon_server.selectedClient ? ">" : " ");

	consoleSetColor(COLOR_GREEN|COLOR_BLUE, bgcolor);
	elapsedTime = beaconGetCurTime() - client->connectTime;
	sprintf(buffer, "[%d:%2.2d:%2.2d]", elapsedTime / 3600, (elapsedTime / 60) % 60, elapsedTime % 60);
	strcatf(buffer, " %s/%s", client->computerName, client->userName);
	printf("%-42s", buffer);

	consoleSetColor(COLOR_BRIGHT|COLOR_BLUE|COLOR_GREEN, bgcolor);
	elapsedTime = beaconGetCurTime() - client->stateBeginTime;
	sprintf(buffer, "%d:%2.2d:%2.2d", elapsedTime / 3600, (elapsedTime / 60) % 60, elapsedTime % 60);
	printf("%-10s", buffer);

	consoleSetDefaultColor();

	//fixme2 newnet printf("%d pkts.  ", qGetSize(client->link->sendQueue2));

	switch(client->clientType){
		xcase BCT_SERVER:{
			printf("Map: %s", estrLength(&client->server.mapName) ? getFileName(client->server.mapName) : "NONE");
		}

		xdefault:{
			if(crashedCount){
				beaconPrintf(COLOR_RED, "%d CRASHED!  ", crashedCount);
			}

			if(notConnectedCount){
				beaconPrintf(COLOR_YELLOW, "%d Not Connected!  ", notConnectedCount);
			}

			if(userInactiveCount){
				beaconPrintf(COLOR_YELLOW, "%d Inactive!  ", userInactiveCount);
			}
		}
	}

	consoleSetDefaultColor();

	printf("\n");

	switch(client->clientType){
		xcase BCT_SERVER:{
			if(	client->server.isRequestServer &&
				client->requestServer.processNode)
			{
				printf("³%s", index == beacon_server.selectedClient ? ">" : " ");

				beaconPrintfDim(COLOR_GREEN,
								"Assigned map: %s\n",
								client->requestServer.processNode->uniqueFileName);
			}
		}
	}
}

static void beaconServerMoveSelection(S32 delta){
#if 0
fixme newnet
	NetLink** links = (NetLink**)beacon_server.clients.links->storage;
	S32 size = beacon_server.clients.links->size;
	BeaconServerClientData* client;

	if(!size){
		return;
	}

	delta = delta > 0 ? 1 : delta < 0 ? -1 : 0;

	if(delta){
		while(1){
			beacon_server.selectedClient += delta;

			if(beacon_server.selectedClient < 0){
				beacon_server.selectedClient = 0;
				break;
			}
			else if(beacon_server.selectedClient >= size){
				beacon_server.selectedClient = size - 1;
				break;
			}
			else{
				client = links[beacon_server.selectedClient]->userData;

				if(clientStateIsVisible(client)){
					break;
				}
			}
		}
	}else{
		delta = -1;
	}

	delta = -delta;

	if(beacon_server.selectedClient < 0){
		beacon_server.selectedClient = 0;
	}
	else if(beacon_server.selectedClient >= size){
		beacon_server.selectedClient = size - 1;
	}

	while(1){
		client = links[beacon_server.selectedClient]->userData;

		if(clientStateIsVisible(client)){
			break;
		}

		beacon_server.selectedClient += delta;

		if(beacon_server.selectedClient < 0){
			beacon_server.selectedClient = 0;
			break;
		}
		else if(beacon_server.selectedClient >= size){
			beacon_server.selectedClient = size - 1;
			break;
		}
	}

	delta = -delta;

	while(1){
		client = links[beacon_server.selectedClient]->userData;

		if(clientStateIsVisible(client)){
			break;
		}

		beacon_server.selectedClient += delta;

		if(beacon_server.selectedClient < 0){
			beacon_server.selectedClient = 0;
			break;
		}
		else if(beacon_server.selectedClient >= size){
			beacon_server.selectedClient = size - 1;
			break;
		}
	}
#endif

	return;
}

static void beaconServerPrintClientStates(void){
	consoleSetDefaultColor();

	printf(	"\n"
			"       ÚÄÄÄÄÄÄÄÄÄÄÄÄÄ¿\n"
			"       ³ ");

	beaconPrintf(COLOR_RED|COLOR_GREEN|COLOR_BLUE, "Client Info");

	printf(	" ³\n"
			"ÚÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÄÄÄÄÄÁÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ\n"
			"³ ");

	beaconPrintf(COLOR_GREEN,
				"%-17s%-25s%-18s%-12s%-12s\n",
				"IP",
				"State",
				"StateInfo",
				"Blocks",
				"Beacons");

	if(listenCount(beacon_server.clients)){
		beaconServerMoveSelection(0);
		beaconServerForEachClient(printClientState, NULL);
	}else{
		printf(	"ÃÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ\n"
				"³ No Clients Connected!\n");
	}

	printf(	"ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ\n"
			"\n\n");
}

static S32 beaconServerHasMapLoaded(void){
	if(	estrLength(&beacon_server.curMapName) &&
		(	!beacon_server.isRequestServer || beacon_server.request.projectName ||
			beaconMapDataPacketIsFullyReceived(beacon_server.mapData)))
	{
		return 1;
	}

	return 0;
}

static S32 beaconGetSearchString(char* buffer, S32 maxLength){
	beaconPrintf(	COLOR_GREEN|COLOR_BLUE,
					"Current (L: %d/%d): %s\n\n",
					beacon_server.curMapIndex,
					eaSize(&beacon_server.queueList),
					beaconServerHasMapLoaded() ? beacon_server.curMapName : "NONE!");

	beaconPrintf(COLOR_GREEN, "Enter search: ");

	if(!beaconEnterString(buffer, maxLength)){
		beaconPrintf(COLOR_RED, "CANCELED!\n");
		return 0;
	}

	if(!buffer[0]){
		beaconPrintf(COLOR_YELLOW, "No Search String!");
	}

	printf("\n");

	return 1;
}

static void beaconServerPrintQueueList(void){
	S32 i;
	char search[1000];

	if(!eaSize(&beacon_server.queueList)){
		return;
	}

	if(!beaconGetSearchString(search, ARRAY_SIZE(search) - 1)){
		return;
	}

	printf(	"\n"
			"---------------------------------------------------------------------------------------\n"
			"Queue list:\n");

	for(i = 0; i < eaSize(&beacon_server.queueList); i++){
		const char* name = beacon_server.queueList[i];
		char* nameAtMaps = strstri(name, "maps/");

		if(search[0] && !strstri(name, search)){
			continue;
		}

		if(	i == beacon_server.curMapIndex - 1 &&
			beaconServerHasMapLoaded() &&
			!stricmp(name, beacon_server.curMapName))
		{
			consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
		}

		printf("  %4d: %s\n", i, nameAtMaps ? nameAtMaps : name);

		consoleSetDefaultColor();
	}

	printf(	"---------------------------------------------------------------------------------------\n"
			"\n"
			"\n");
}

static void beaconServerPrintMapList(void){
	S32 i;
	char search[1000];

	if(!eaSize(&beacon_server.queueList)){
		return;
	}

	if(!beaconGetSearchString(search, ARRAY_SIZE(search) - 1)){
		return;
	}

	printf(	"\n"
			"---------------------------------------------------------------------------------------\n"
			"Queue list:\n");

	for(i = 0; i < eaSize(&beacon_server.queueList); i++){
		const char* name = beacon_server.queueList[i];
		char* nameAtMaps = strstri(name, "maps/");

		if(search[0] && !strstri(name, search)){
			continue;
		}

		if(	beaconServerHasMapLoaded() &&
			!stricmp(name, beacon_server.curMapName))
		{
			consoleSetColor(COLOR_BRIGHT|COLOR_GREEN, 0);
		}

		printf("  %4d: %s\n", i, nameAtMaps ? nameAtMaps : name);

		consoleSetDefaultColor();
	}

	printf(	"---------------------------------------------------------------------------------------\n"
			"\n"
			"\n");
}

static void displayBlockInfo(void){
	S32 x;
	S32 z;

	if(!beacon_server.curMapName){
		return;
	}

	beaconConsolePrintf(0, "\nMap: %s\n\n", beacon_server.curMapName);

	for(z = bp_blocks.grid_max_xyz[2]; z >= bp_blocks.grid_min_xyz[2]; z--){
		beaconConsolePrintf((z % 5) ? COLOR_RED|COLOR_GREEN|COLOR_BLUE : COLOR_BRIGHT|COLOR_GREEN, "%4d: ", z);

		for(x = bp_blocks.grid_min_xyz[0]; x <= bp_blocks.grid_max_xyz[0]; x++){
			BeaconDiskSwapBlock* block = beaconGetDiskSwapBlockByGrid(x, z);

			if(block){
				if(block->clients.count){
					beaconConsolePrintf(COLOR_GREEN|COLOR_BLUE, block->clients.count < 16 ? "%x" : "X", block->clients.count);
				}
				else if(block->legalCompressed.uncheckedCount){
					beaconConsolePrintf(COLOR_GREEN, "Û");
				}
				else if(block->legalCompressed.totalCount){
					beaconConsolePrintf(COLOR_GREEN|COLOR_BLUE, "Û");
				}
				else{
					beaconConsolePrintfDim(COLOR_GREEN, "Û");
				}
			}else{
				beaconConsolePrintfDim(COLOR_RED, "Û");
			}
		}

		beaconConsolePrintf(0, "\n");
	}

	beaconConsolePrintf(0, "\n      ");

	for(x = bp_blocks.grid_min_xyz[0]; x < (bp_blocks.grid_min_xyz[0] / 5) * 5; x++){
		beaconConsolePrintf(COLOR_WHITE, ".");
	}

	for(x = (bp_blocks.grid_min_xyz[0] / 5) * 5; x <= bp_blocks.grid_max_xyz[0]; x += 10){
		char buffer[100];
		S32 i;

		sprintf(buffer, "%d", x);

		beaconConsolePrintf(COLOR_GREEN, "%s", buffer);

		for(i = (S32)strlen(buffer); i % 10 && x + i <= bp_blocks.grid_max_xyz[0]; i++){
			beaconConsolePrintfDim(COLOR_RED|COLOR_GREEN|COLOR_BLUE, (i % 5) ? "." : "|");
		}
	}

	beaconConsolePrintf(0, "\n\n");
}

static U32 beaconServerPointIsPlayable(const Vec3 pos)
{
	int i;

	if(!eaSize(&playableEnts))
	{
		return 1;
	}

	for(i=0; i<eaSize(&playableEnts); i++)
	{
		WorldVolumeEntry *ent = playableEnts[i];
		int result;
		int j;

		for (j=0;j<eaSize(&ent->elements);j++)
		{
			WorldVolumeElement * pElement = ent->elements[j];
			
			if (pElement->volume_shape == WL_VOLUME_BOX)
			{
				result = sphereOrientBoxCollision(pos, 0, pElement->local_min, pElement->local_max,
						 								  pElement->world_mat, NULL);
				if(result)
				{
					return 1;
				}
			}
			else
			{
				// fall back to a gross check, since some designer made his playable volume a room, and rooms automatically convert their boxes to meshes.
				result = sphereOrientBoxCollision(pos, 0, ent->base_entry.shared_bounds->local_min, ent->base_entry.shared_bounds->local_max,
						 								  ent->base_entry.bounds.world_matrix, NULL);

				if(result)
				{
					return 1;
				}
				else
				{
					// we're done with this WorldVolumeEntry, since we did a gross check
					break;
				}
			}
		}
	}

	return 0;
}

static WorldRegion **spawnableSpaceRegions = NULL;

void beaconAddSpawnLocation(const Vec3 pos, int noGroundConnections, U32 isSpecial)
{
	int color = 0xFF00FF00;

	if(beaconServerPointIsPlayable(pos))
	{
		if(beaconIsBeaconizer())
		{
			Beacon* b = addCombatBeacon(pos, 0, 1, 1, isSpecial);
			WorldRegion* region = NULL;

			if(!b)
			{
				Vec3 higherpos;

				copyVec3(pos, higherpos);
				higherpos[1] += BEACON_SPAWN_OFFSET;

				b = addCombatBeacon(higherpos, 0, 1, 1, isSpecial);
			}

			if(b)
			{
				b->isValidStartingPoint = 1;
				b->noGroundConnections = noGroundConnections;

				beacon_server.spawn_count++;

				beaconFileAddCRCPos(pos);
			}

			if(!b)
			{
				region = worldGetWorldRegionByPos(pos);
				if(region && worldRegionGetType(region)==WRT_Space)
					eaPush(&spawnableSpaceRegions, region);
			}

			if(!b)
				color = 0xFF0000FF;
		}
	}
	else
	{
		color = 0xFFFF0000;
		beacon_server.reject_spawn_count++;
	}

	if(beaconServerDebugCombatPlace())
	{
		beaconServerSendDebugPoint(BDO_COMBAT, pos, color);
	}
}


void beaconAddCritterSpawn(const Vec3 pos, char* encounterStr)
{
	if(beaconServerPointIsPlayable(pos))
	{
		if(beaconIsBeaconizer())
		{
			Beacon* b = addCombatBeacon(pos, 0, 1, 1, 1);

			if(b)
			{
				b->encounterStr = strdup(encounterStr);
				eaPush(&encounterBeaconArray, b);

				if(b)
				{
					beacon_server.useful_count++;
				}
			}
		}
	}
	else
	{
		beacon_server.reject_useful_count++;
	}
}

void beaconAddUsefulPoint(const Vec3 pos, U32 isSpecial)
{
	if(beaconServerPointIsPlayable(pos))
	{
		if(beaconIsBeaconizer())
		{
			Beacon *b = addCombatBeacon(pos, 0, 1, 1, isSpecial);

			if(b)
			{
				beacon_server.useful_count++;
			}
		}
	}
	else
	{
		beacon_server.reject_useful_count++;
	}
}

void beaconServerCreateSpaceBeacons(void)
{
	int i;

	for(i=0; i<eaSize(&playableEnts); i++)
	{
		Vec3 	world_min, world_max;
		WorldVolumeEntry *ent = playableEnts[i];
		WorldRegion *reg;

		mulVecMat4(ent->base_entry.shared_bounds->local_min, ent->base_entry.bounds.world_matrix, world_min);
		mulVecMat4(ent->base_entry.shared_bounds->local_max, ent->base_entry.bounds.world_matrix, world_max);

		world_min[0] += (world_max[0]-world_min[0])/2;
		world_min[1] += 3;
		world_min[2] += (world_max[2]-world_min[2])/2;

		reg = worldGetWorldRegionByPos(world_min);
		if(reg && worldRegionGetType(reg)==WRT_Space && eaFind(&spawnableSpaceRegions, reg)!=-1)
			beaconAddSpawnLocation(world_min, 1, 0);
	}

	eaClear(&spawnableSpaceRegions);
}

static void beaconInsertLegalBeacons(void){
	S32 i;
	S32 legalBlockCount = 0;

	//if(devassert(addSpawnLocationCallback))
	//	addSpawnLocationCallback();

	eaClear(&encounterBeaconArray);
	eaClear(&invalidEncounterArray);
	wcForceSimulationUpdate();

	beaconClearCRCData();
	beacon_server.useful_count = 0;
	beacon_server.spawn_count = 0;
	beacon_server.reject_spawn_count = 0;
	beacon_server.reject_useful_count = 0;
	beaconGatherSpawnPositions(0);		// Determines which space regions have a spawn point
	beaconServerCreateSpaceBeacons();	// For spawnable space regions, add start beacons

	if(!beacon_server.minimalPrinting)
		printf("Placing %d starting positions: ", combatBeaconArray.size);

	for(i = 0; i < combatBeaconArray.size; i++){
		Beacon* b = combatBeaconArray.storage[i];

		if(b->isValidStartingPoint){
			S32 x = vecX(b->pos);
			S32 z = vecZ(b->pos);
			BeaconDiskSwapBlock* block = beaconGetDiskSwapBlock(x, z, 0);
			BeaconLegalAreaCompressed* area;

			if(!block){
				printf("WARNING: Found legal beacon in empty block at (%1.1f, %1.1f, %1.1f).\n", vecParamsXYZ(b->pos));
				continue;
			}

			if(!block->legalCompressed.totalCount){
				legalBlockCount++;
			}

			for(area = block->legalCompressed.areasHead; area; area = area->next){
				if(	!area->isIndex &&
					area->x == x - block->x &&
					area->z == z - block->z &&
					area->y_coord == vecY(b->pos))
				{
					break;
				}
			}

			if(area){
				continue;
			}

			beaconVerifyUncheckedCount(block);

			area = beaconAddLegalAreaCompressed(block);

			area->isIndex = 0;
			area->x = x - block->x;
			area->z = z - block->z;
			area->y_coord = vecY(b->pos);
			area->checked = 0;

			block->legalCompressed.uncheckedCount++;

			beaconVerifyUncheckedCount(block);

			beaconCheckDuplicates(block);
		}
	}

	if(!beacon_server.minimalPrinting)
		printf("Done!  Used %d blocks.\n", legalBlockCount);
}

static void beaconServerAddAllMadeBeacons(void){
	BeaconDiskSwapBlock* block;
	S32 count = 0;
	S32 i;
	WorldRegion *region;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);

	//beaconPrintDebugInfo();

	if(!beacon_server.minimalPrinting)
		printf("Adding beacons: ");

	beaconTestProcessGeoProximity();
	for(block = bp_blocks.list; block; block = block->nextSwapBlock){
		if(block->generatedBeacons.count){
			if(!beacon_server.minimalPrinting)
				printf("%d, ", block->generatedBeacons.count);

			count += block->generatedBeacons.count;

			for(i = 0; i < block->generatedBeacons.count; i++){
				BeaconGeneratedBeacon* madeBeacon = block->generatedBeacons.beacons + i;
				Beacon* nearestBeacon = NULL;
				Beacon* beacon;
				F32 bestDistSQR = FLT_MAX;

				if(!stashAddressFindInt(beaconGeoProximityStashProcess, block, NULL))
				{
					int dx, dz;
					Vec3 pos;

					copyVec3(madeBeacon->pos, pos);
					beaconMakeGridBlockCoords(pos);

					for(dx=-1; dx<=1; dx++)
					{
						for(dz=-1; dz<=1; dz++)
						{
							BeaconBlock* neighbor = beaconGetGridBlockByCoords(partition, pos[0]+dx, pos[1], pos[2]+dz, 0);

							if(neighbor)
							{
								int j;

								for(j=0; j<neighbor->beaconArray.size; j++)
								{
									F32 distSQR;
									beacon = neighbor->beaconArray.storage[j];

									distSQR = distance3SquaredXZ(beacon->pos, madeBeacon->pos);
									if(distSQR<bestDistSQR)
									{
										bestDistSQR = distSQR;
										nearestBeacon = beacon;
									}
								}
							}
						}
					}

					if(nearestBeacon && bestDistSQR<SQR(20))
						continue;
				}

				if(beacon_server.debug_state &&
					!vec3IsZero(beacon_server.debug_state->debug_pos) &&
					distance3(beacon_server.debug_state->debug_pos, madeBeacon->pos) < 5)
				{
					printf("");
				}

				beacon = addCombatBeacon(madeBeacon->pos, 1, 1, 1, 0);

				if(beacon){
					beacon->noGroundConnections = madeBeacon->noGroundConnections;

					if(!beacon->noGroundConnections)
					{
						region = worldGetWorldRegionByPos(beacon->pos);
						if(worldRegionGetType(region)==WRT_Space)
							beacon->noGroundConnections = true;
					}
				}
			}
		}
	}

	// Do any sort of pre-connect pruning here

	// End pruning

	//for(i = 0; i < BEACON_MAX_DBG_IDS; i++)
	//	beacon_state.debugBeacons[i] = NULL;

	ea32ClearFast(&beacon_server.beaconConnect.legalBeacons.indices);
	for(i = 0; i < combatBeaconArray.size; i++){
		Beacon* b = combatBeaconArray.storage[i];

		b->userInt = i;
		b->globalIndex = i;

		if(b->isValidStartingPoint){
			b->wasReachedFromValid = 1;

			ea32Push(&beacon_server.beaconConnect.legalBeacons.indices, b->globalIndex);
		}
	}

	beacon_server.beaconConnect.legalBeacons.groupedCount = 0;

	if(!beacon_server.minimalPrinting)
		printf("Done!\nAdded %d beacons.\n", count);
	beacon_server.generate_count = count;

	beacon_server.beaconConnect.assignedCount = 0;
	beacon_server.beaconConnect.finishedCount = 0;
	beacon_server.beaconConnect.groups.count = 0;
	beacon_server.beaconConnect.finishedBeacons = 0;
	beacon_server.beaconConnect.startTime = beaconGetCurTime();

	while(beacon_server.beaconConnect.groups.availableHead){
		BeaconConnectBeaconGroup* next = beacon_server.beaconConnect.groups.availableHead->next;

		SAFE_FREE(beacon_server.beaconConnect.groups.availableHead->clients.clients);
		destroyBeaconConnectBeaconGroup(beacon_server.beaconConnect.groups.availableHead);
		beacon_server.beaconConnect.groups.availableHead = next;
	}

	beacon_server.beaconConnect.groups.availableTail = NULL;

	while(beacon_server.beaconConnect.groups.finished){
		BeaconConnectBeaconGroup* next = beacon_server.beaconConnect.groups.finished->next;

		SAFE_FREE(beacon_server.beaconConnect.groups.finished->clients.clients);
		destroyBeaconConnectBeaconGroup(beacon_server.beaconConnect.groups.finished);
		beacon_server.beaconConnect.groups.finished = next;
	}

	beaconServerCreateBeaconGroups(0);
}

static void sendKillToClient(BeaconServerClientData* client, U32 sentryOnly, S32 justCrashed){
	if(!sentryOnly || client->clientType == BCT_SENTRY){
		BEACON_SERVER_PACKET_CREATE_BASE(pak, client, BMSG_S2CT_KILL_PROCESSES);

			pktSendBits(pak, 1, justCrashed ? 1 : 0);

		BEACON_SERVER_PACKET_SEND();
	}
}

UINT_PTR CALLBACK getFileNameDialogHook(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam){
	switch(uiMsg){
		xcase WM_SHOWWINDOW:{
			BringWindowToTop(beaconGetConsoleWindow());
			BringWindowToTop(hdlg);
		}
	}

	return FALSE;
}

#define FILENAME_STRING_SIZE 20000

S32 getFileNameDialog(char* fileMask, char* initialDir, S32 initialDirLen, char*** multiSelectArray){
	static WCHAR fileName[20000] = {0};
	OPENFILENAME	theFileInfo;
	S32				ret;
	char *pNarrowPath = NULL;
	char *pNarrowFile = NULL;

	memset(fileName, 0, ARRAY_SIZE(fileName));

	ZeroStruct(&theFileInfo);
	theFileInfo.lStructSize = sizeof(OPENFILENAME);
	theFileInfo.hwndOwner = beaconGetConsoleWindow();
	theFileInfo.lpstrFilter = UTF8_To_UTF16_DoubleTerminated_malloc(fileMask);
	theFileInfo.lpstrFile = fileName;
	theFileInfo.lpstrInitialDir = UTF8_To_UTF16_malloc(initialDir);
	theFileInfo.nMaxFile = ARRAY_SIZE(fileName) - 1;
	theFileInfo.lpstrTitle = L"Choose map files to beaconize:";
	theFileInfo.Flags = OFN_EXPLORER |
						OFN_LONGNAMES |
						OFN_OVERWRITEPROMPT |
						OFN_ENABLEHOOK |
						OFN_ENABLESIZING |
						OFN_HIDEREADONLY |
						(multiSelectArray ? OFN_ALLOWMULTISELECT : 0)
						// | OFN_PATHMUSTEXIST
						;
	theFileInfo.lpstrDefExt = L".zone";
	theFileInfo.lpfnHook = getFileNameDialogHook;

	ret = GetOpenFileName(&theFileInfo);

	SAFE_FREE(theFileInfo.lpstrFilter);
	SAFE_FREE(theFileInfo.lpstrInitialDir);

	

	if(!ret && (GetLastError() || CommDlgExtendedError())){
		printf("GetOpenFileName Error: %d, %d (", GetLastError(), CommDlgExtendedError());

		switch(CommDlgExtendedError()){
			#define CASE(x) xcase x: printf(#x)
			CASE(CDERR_DIALOGFAILURE);
			CASE(CDERR_FINDRESFAILURE);
			CASE(CDERR_NOHINSTANCE);
			CASE(CDERR_INITIALIZATION);
			CASE(CDERR_NOHOOK);
			CASE(CDERR_LOCKRESFAILURE);
			CASE(CDERR_NOTEMPLATE);
			CASE(CDERR_LOADRESFAILURE);
			CASE(CDERR_STRUCTSIZE);
			CASE(CDERR_LOADSTRFAILURE);
			CASE(FNERR_BUFFERTOOSMALL);
			CASE(CDERR_MEMALLOCFAILURE);
			CASE(FNERR_INVALIDFILENAME);
			CASE(CDERR_MEMLOCKFAILURE);
			CASE(FNERR_SUBCLASSFAILURE);
			#undef CASE
			xdefault:printf("Unknown");
		}

		printf(")\n");
	}

	if(ret)
	{
		if(multiSelectArray)
		{
			eaClearEx(multiSelectArray, NULL);


			//this indicates that there are multiple filenames in filename, crammed in like this:
			//path\0file1\0file2\0file3\0\0, with theFileInfo.nFileOffset pointing to file1  
			if(theFileInfo.nFileOffset > wcslen(fileName))
			{
				WCHAR *pCurFileWide = fileName + theFileInfo.nFileOffset;

				UTF16ToEstring(fileName, 0, &pNarrowPath);
				forwardSlashes(pNarrowPath);

				while(*pCurFileWide)
				{
					UTF16ToEstring(pCurFileWide, 0, &pNarrowFile);
					estrInsertf(&pNarrowFile, 0, "%s/", pNarrowPath);
					eaPush(multiSelectArray, strdup(pNarrowFile));

					pCurFileWide += wcslen(pCurFileWide) + 1;
				}
			}
			else
			{
				UTF16ToEstring(fileName, 0, &pNarrowPath);

				if(strstri(pNarrowPath, "data/ns/") || strStartsWith(pNarrowPath, "ns/"))
					eaPush(multiSelectArray, strdup(strstri(pNarrowPath, "ns/")));
				else 
					eaPush(multiSelectArray, strdup(strstri(pNarrowPath, "maps/")));			
			}
		}
		else
		{
			UTF16_to_UTF8_Static(fileName, initialDir, initialDirLen);
			forwardSlashes(initialDir);
		}
	}

	estrDestroy(&pNarrowFile);
	estrDestroy(&pNarrowPath);

	return ret ? 1 : 0;

}

static void beaconServerQueueMapListFile(void){
	char buffer[1000];
	
	STR_COMBINE_SS(buffer, beaconServerGetDataPath(), "\\maps\\enter a map list filename");

	if(getFileNameDialog("*.txt", SAFESTR(buffer), NULL)){
		beaconServerLoadMapListFile(buffer, 1);
	}
}

S32 beaconServerQueueMapFile(const char* newMapFile){
	if(fileExistsInList(beacon_server.queueList, newMapFile, NULL)){
		beaconPrintf(COLOR_YELLOW, "Map already in queue: %s\n", newMapFile);
	}
	else{
		S32 index;

		if(fileExistsInList(beacon_server.queueList, newMapFile, &index)){
			beaconPrintf(	COLOR_GREEN,
							"Queueing map %d/%d in list: %s\n",
							index + 1,
							eaSize(&beacon_server.queueList),
							strstr(newMapFile, "maps/"));
		}else{
			beaconPrintf(	COLOR_YELLOW,
							"Queueing map not in list: %s\n",
							newMapFile);
		}

		eaPush(&beacon_server.queueList, allocAddString(newMapFile));

		return 1;
	}

	return 0;
}

static void beaconServerQueueMapFiles(void){
	
	char** newMaps = NULL;
	char buffer[1000];
	
	STR_COMBINE_SS(buffer, beaconServerGetDataPath(), "\\");

	if(estrLength(&beacon_server.curMapName)){
		strcpy(buffer, beacon_server.curMapName);
		backSlashes(buffer);
	}

	if(getFileNameDialog("Maps (.zone)\0*.zone\0\0", SAFESTR(buffer), &newMaps))
	{
		S32 i;

		for(i = 0; i < eaSize(&newMaps); i++)
		{
			ZoneMapInfo *zminfo = zmapInfoFind(newMaps[i]);
			if(!stricmp(zmapInfoGetPublicName(zminfo), "EmptyMap"))
				continue;

			if(zminfo && (!zmapInfoGetNoBeacons(zminfo) || beacon_server.forceRebuild))
			{
				if(beaconHasSpaceRegion(zminfo))
					continue;
				
				if(zmapInfoIsPublicOrHasGroupPrivacy(zminfo))
					beaconServerQueueMapFile(zmapInfoGetPublicName(zminfo));
				else
					beaconServerQueueMapFile(zmapInfoGetFilename(zminfo));
			}
		}
	}

	eaClearEx(&newMaps, NULL);
	eaDestroy(&newMaps);
}

static void beaconServerQueueMainListMaps(void){
	char search[1000];
	char confirm[4];
	S32 i;

	if(!eaSize(&beacon_server.queueList)){
		beaconPrintf(COLOR_YELLOW, "No maps in main list.\n\n");
		return;
	}

	beaconPrintf(COLOR_GREEN, "Enter search string: ");

	if(!beaconEnterString(search, ARRAY_SIZE(search) - 1) || !search[0]){
		beaconPrintf(COLOR_RED, "Canceled!\n");
		return;
	}

	printf("\n");

	for(i = 0; i < eaSize(&beacon_server.queueList); i++){
		if(strstri(beacon_server.queueList[i], search)){
			printf("%6d: %s\n", i, beacon_server.queueList[i]);
		}
	}

	beaconPrintf(COLOR_GREEN, "\nQueue these maps? (type 'yes'): ");

	if(!beaconEnterString(confirm, 3) || stricmp(confirm, "yes")){
		beaconPrintf(COLOR_RED, "Canceled!\n");
		return;
	}

	printf("\n");

	for(i = 0; i < eaSize(&beacon_server.queueList); i++){
		if(strstri(beacon_server.queueList[i], search)){
			beaconServerQueueMapFile(beacon_server.queueList[i]);
		}
	}

	printf("Done!\n\n");
}

static void beaconServerMapListMenu(void){
	S32 printMenu = 1;
	char confirm[4];

	// Load the file list.

	beaconServerUpdateTitle("Map List Menu");

	while(1){
		S32 done = 0;
		U8 theChar;

		if(printMenu){
			beaconPrintf(COLOR_GREEN,
						"-------------------------------------------------------------------------------------------------\n"
						"Map list menu:\n"
						"\n"
						"\tESC : Exit this menu\n"
						"\t1   : Reload map list from spec files and \"%s/server/maps/beaconprocess/maps*.txt\"\n"
						"\t2   : Open map list directory\n"
						"\t3   : Clear the map list\n"
						"\t4   : View the map list (%d maps)\n"
						"\t5   : View the queue list (%d maps)\n"
						"\t6   : Add maps to the queue\n"
						"\t7   : Add a map list file to the queue\n"
						"\t8   : Restart the queue\n"
						"\t9   : Add some maps from the main list to the queue\n"
						"\t0   : Clear the queue\n"
						"\n"
						"Enter selection: ",
						beaconServerGetDataPath(),
						eaSize(&beacon_server.queueList),
						eaSize(&beacon_server.queueList));
		}

		printMenu = 1;

		theChar = _getch();
		switch(tolower(theChar)){
			xcase 0:
			case 224:{
				{S32 unused = _getch();}
				done = 0;
				printMenu = 0;
			}

			xcase 27:{
				printf("Exiting map list menu\n\n");
				done = 1;
			}

			xcase '1':{
				printf("Loading map list\n\n");
				beaconServerLoadMapFileList(0);
			}

			xcase '2':{
				char buffer[1000];
				STR_COMBINE_SS(buffer, beaconServerGetDataPath(), "\\server\\maps\\beaconprocess\\");
				printf("Opening map list directory\n\n");
				ulShellExecute(NULL, "explore", buffer, "", "", SW_SHOW);
			}

			xcase '3':{
				printf("Clearing map list\n\n");
				printf("Type 'yes': ");
				if(beaconEnterString(confirm, 3) && !strcmp(confirm, "yes")){
					printf("\nClearing the map list!");
					eaClear(&beacon_server.queueList);
					beacon_server.curMapIndex = 0;
				}
				printf("\n");
			}

			xcase '4':{
				printf("Printing map list\n\n");
				beaconServerPrintMapList();
			}

			xcase '5':{
				printf("Printing queue list\n\n");
				beaconServerPrintQueueList();
			}

			xcase '6':{
				printf("Queueing map files\n\n");
				beaconServerQueueMapFiles();
			}

			xcase '7':{
				printf("Queueing map list file\n\n");
				beaconServerQueueMapListFile();
			}

			xcase '8':{
				printf("Starting at beginning of queue\n\n");
				beacon_server.curMapIndex = 0;
			}

			xcase '9':{
				printf("Queueing main-list maps\n\n");
				beaconServerQueueMainListMaps();
			}

			xcase '0':{
				printf("Clearing map queue!\n");
				if(beaconEnterString(confirm, 3) && !strcmp(confirm, "yes")){
					printf("\nClearing the map queue!");
					eaClear(&beacon_server.queueList);
					beacon_server.curMapIndex = 0;
				}
				printf("\n");
			}

			xdefault:{
				printMenu = 0;
			}
		}

		if(done){
			break;
		}
	}
}

static void beaconServerDebugMenu(void) {
	S32 printMenu = 1;

	// Load the file list.

	beaconServerUpdateTitle("Debug Menu");

	if(!beacon_server.debug_state)
	{
		beacon_server.debug_state = StructAlloc(parse_BeaconProcessDebugState);
	}

	while(1){
		S32 done = 0;
		U8 theChar;

		if(printMenu){
			beaconPrintf(COLOR_BLUE,
				"-------------------------------------------------------------------------------------------------\n"
				"Map list menu:\n"
				"\n"
				"\tESC : Exit this menu\n"
				"\t1   : Debug generate edges(%s)\n"
				"\t2   : Debug generate beacons(%s)\n"
				"\t3   : Debug addcombat(%s)\n"
				"\t4   : Debug walkresults(%s)\n"
				"\t5   : Debug pruning(%s)\n"
				"\t6   : Debug pre rebuild(%s)\n"
				"\t7   : Debug post rebuild(%s)\n"
				"\n"
				"Enter selection: ",
				beacon_server.debug_state->send_gen_edge ? "ON" : "OFF",
				beacon_server.debug_state->send_gen_lines ? "ON" : "OFF",
				beacon_server.debug_state->send_combat ? "ON" : "OFF",
				beacon_server.debug_state->send_walk_res ? "ON" : "OFF",
				beacon_server.debug_state->send_prune ? "ON" : "OFF",
				beacon_server.debug_state->send_pre_rebuild ? "ON" : "OFF",
				beacon_server.debug_state->send_post_rebuild ? "ON" : "OFF");
		}

		printMenu = 1;

		theChar = _getch();
		switch(tolower(theChar)){
			xcase 27:{
				printf("Exiting debug menu\n\n");
				done = 1;
			}

			xcase '1': {
				beacon_server.debug_state->send_gen_edge = !beacon_server.debug_state->send_gen_edge;
			}

			xcase '2': {
				beacon_server.debug_state->send_gen_lines = !beacon_server.debug_state->send_gen_lines;
			}

			xcase '3': {
				beacon_server.debug_state->send_combat = !beacon_server.debug_state->send_combat;
			}

			xcase '4': {
				beacon_server.debug_state->send_walk_res = !beacon_server.debug_state->send_walk_res;
			}

			xcase '5': {
				beacon_server.debug_state->send_prune = !beacon_server.debug_state->send_prune;
			}

			xcase '6': {
				beacon_server.debug_state->send_pre_rebuild = !beacon_server.debug_state->send_pre_rebuild;
			}

			xcase '7': {
				beacon_server.debug_state->send_post_rebuild = !beacon_server.debug_state->send_post_rebuild;
			}

			xdefault:{
				printMenu = 0;
			}
		}

		if(done){
			break;
		}
	}
}

static void beaconServerSentryMenu(void){
}

static void sendKillAllClientsToSentry(BeaconServerClientData* client, S32 index, void* userData){
	if(client->clientType == BCT_SENTRY){
		sendKillToClient(client, 1, 0);
	}
}

static void sendKillCrashedClientsToSentry(BeaconServerClientData* client, S32 index, void* userData){
	if(client->clientType == BCT_SENTRY){
		sendKillToClient(client, 1, 1);
	}
}

static void beaconServerKillClientsMenu(void){
	S32 killAll;
	U8 theChar;

	beaconPrintf(COLOR_RED, "Kill ALL or CRASHED clients (A/C)? ");

	theChar = _getch();
	switch(tolower(theChar)){
		xcase 'a':
			beaconPrintf(COLOR_GREEN, "ALL!\n\n");
			killAll = 1;
		xcase 'c':
			beaconPrintf(COLOR_GREEN, "CRASHED!\n\n");
			killAll = 0;
		xdefault:
			beaconPrintf(COLOR_RED, "CANCELED!\n\n");
			return;
	}

	beaconPrintf(COLOR_RED, "Are you sure you want to kill all clients (yes/no)?  ");

	theChar = _getch();
	if(tolower(theChar) == 'y'){
		beaconPrintf(COLOR_GREEN, "Y");

		theChar = _getch();
		if(tolower(theChar) == 'e'){
			beaconPrintf(COLOR_GREEN, "E");

			theChar = _getch();
			if(tolower(theChar) == 's'){
				beaconPrintf(COLOR_GREEN, "S!!!\n\n");

				beaconServerForEachClient(killAll ? sendKillAllClientsToSentry : sendKillCrashedClientsToSentry, NULL);

				return;
			}
		}
	}

	while(_kbhit()){
		{S32 unused = _getch();}
	}

	beaconPrintf(COLOR_RED, " -- Canceled!!!\n\n");
}

static void beaconServerSetState(BeaconServerState state){
	if(beacon_server.state != state){
		beacon_server.state = state;

		beacon_server.setStateTime = beaconGetCurTime();

		beaconServerSetSendStatus();
	}
}

static void beaconServerSetCurMap(const char* newMapFile){
	if(!newMapFile){
		newMapFile = "";
	}

	if(beacon_server.curMapName && !stricmp(beacon_server.curMapName, newMapFile)){
		return;
	}
	
	beaconServerCloseMapLogFileHandles();

	beaconProcessUndoCheckouts();

	beacon_server.sendClientsToMe = 1;

	beaconServerSetSendStatus();

	estrCopy2(&beacon_server.curMapName, newMapFile);

	beaconServerSetState(BSS_NOT_STARTED);

	if(estrLength(&beacon_server.curMapName)){
		forwardSlashes(beacon_server.curMapName);

		beaconServerUpdateTitle("Starting map: %s", beacon_server.curMapName);

		if(!beacon_server.minimalPrinting)
		{
			beaconPrintf(	COLOR_GREEN|COLOR_BLUE,
							"---L: %d/%d)----------------------------------------------------------------------------------------------\n",
							beacon_server.curMapIndex,
							eaSize(&beacon_server.queueList));

			beaconPrintf(	COLOR_GREEN,
							"STARTING PROCESS: ");

			beaconPrintf(	COLOR_GREEN|COLOR_BLUE,
							"%s\n",
							beacon_server.curMapName);
		}

		beacon_server.optional = 0;
		beacon_server.optional_pruned = 0;
	}
}

typedef struct ExecuteCommandParams {
	const char* commandText;
	S32			useActive;
	S32			showWindowType;
} ExecuteCommandParams;

static void beaconServerSendCommandToExecute(BeaconServerClientData* client, S32 index, ExecuteCommandParams* params){
	if(client->state != BCS_SENTRY){
		return;
	}

	if(	!params->useActive &&
		!client->forcedInactive)
	{
		return;
	}

	beaconClientPrintf(	client,
						COLOR_YELLOW,
						"(%s/%s) Running command: \"%s\"\n",
						client->computerName,
						client->userName,
						params->commandText);

	BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_EXECUTE_COMMAND);

	pktSendString(pak, params->commandText);
	pktSendBitsPack(pak, 1, params->showWindowType);

	BEACON_SERVER_PACKET_SEND();
}

static void beaconServerRunCommandOnSentry(void){
	ExecuteCommandParams	params = {0};

	char					buffer[1000];
	S32						ret;
	U8						theChar;

	beaconPrintf(COLOR_BRIGHT|COLOR_GREEN, "Enter command: ");

	ret = beaconEnterString(buffer, ARRAY_SIZE(buffer) - 1);

	if(!ret){
		beaconPrintf(COLOR_RED, "Canceled!");
		return;
	}

	printf("\n");

	printf("Apply to active clients (instead of just inactive ones)? (y/n) ");

	theChar = _getch();
	switch(tolower(theChar)){
		xcase 'y':
			beaconPrintf(COLOR_GREEN, "yes!\n");
			params.useActive = 1;
		xcase 'n':
			beaconPrintf(COLOR_RED, "no!\n");
		xdefault:
			beaconPrintf(COLOR_RED, "Canceled!\n");
			return;
	}

	printf("Show window: (N)ormal, (M)inimized, (H)idden? ");

	theChar = _getch();
	switch(tolower(theChar)){
		xcase 'n':
			beaconPrintf(COLOR_GREEN, "normal\n");
			params.showWindowType = SW_NORMAL;
		xcase 'm':
			beaconPrintf(COLOR_GREEN, "minimized\n");
			params.showWindowType = SW_MINIMIZE;
		xcase 'h':
			beaconPrintf(COLOR_GREEN, "hidden\n");
			params.showWindowType = SW_HIDE;
		xdefault:
			beaconPrintf(COLOR_RED, "Canceled!\n");
			return;
	}

	printf("Are you sure? (y/n) ");

	theChar = _getch();
	if(tolower(theChar) == 'y'){
		beaconPrintf(COLOR_GREEN, "yes!\n");
	}else{
		beaconPrintf(COLOR_RED, "no!\n");
		return;
	}

	printf("\n");

	params.commandText = buffer;

	beaconServerForEachClient(beaconServerSendCommandToExecute, &params);
}

//typedef void PacketCallback(Packet *pkt,int cmd,NetLink* link,void *user_data);
void beaconServerDebugHandleMessage(Packet *pkt, int cmd, NetLink* link, void *user_data)
{
	switch(cmd)
	{
		xcase BDMSG_POS: {
			pktGetVec3(pkt, beacon_server.debug_state->debug_pos);
			beacon_server.debug_state->debug_dist = 1000;
		}
	}
}

//typedef void LinkCallback(NetLink* link,void *user_data);
void beaconServerDebugHandleConnect(NetLink* link, void *user_data)
{
	char ip[MAX_PATH];
	linkFlushAndClose(&g_debugger_link, "New connection");
	g_debugger_link = link;
	linkGetIpStr(g_debugger_link, SAFESTR(ip));
	printf("Connection from: %s\n", ip);
}

//typedef void LinkCallback(NetLink* link,void *user_data);
void beaconServerDebugHandleDisconnect(NetLink* link, void *user_data)
{
	if(g_debugger_link==link)
	{
		g_debugger_link = NULL;
	}
	printf("Debugger gone :(!\n");
}

void beaconServerOpenDebugConnection(void)
{
	if(!beacon_comm)
	{
		return;
	}
	if(beacon_server.debug_listen)
	{
		return;
	}
	beacon_server.debug_listen = commListen(beacon_comm, LINKTYPE_DEFAULT, LINK_PACKET_VERIFY, BEACON_SERVER_DEBUG_PORT, beaconServerDebugHandleMessage, 
											beaconServerDebugHandleConnect, beaconServerDebugHandleDisconnect, 0);
	commSetSendTimeout(beacon_comm, 60);

	if(!beacon_server.debug_state)
	{
		beacon_server.debug_state = StructAlloc(parse_BeaconProcessDebugState);
	}
}

static void beaconServerMainMenu(void){
	S32 startTime = time(NULL);
	S32 printMenu = 1;

	// Load the file list.

	while(1){
		S32 done = 0;
		S32 keyIsPressed = _kbhit();
		U8 theChar;

		beaconServerUpdateTitle("Main Menu (Exit in %ds)", startTime + 10 - time(NULL));

		if(!printMenu){
			if(time(NULL) - startTime >= 10){
				done = 1;
			}
		}else{
			startTime = time(NULL);

			printf(	"\n\n"
					"---- Main Menu ---------------------------------------\n"
					" 1. Memory Dump\n"
					" 2. Toggle Local IPs Only:................ %3s\n"
					" 3. Network Initialized:.................. %3s\n"
					" 4. Toggle Loading Maps:.................. %3s\n"
					" 5. Toggle Forced Rebuild:................ %3s\n"
					" 6. Toggle Full CRC Info:................. %3s\n"
					" 7. Toggle Generate Only:................. %3s\n"
					" 8. Toggle Check Encounter Positions Only: %3s\n"
					" 9. Toggle Testing Request Client\n"
					" 0. Kill Clients On All Sentries\n"
					"-----------------------------------------------------\n"
					" C. Print client states.\n"
					" G. Toggle gimme usage.\n"
					" W. Write current map data.\n"
					" L. Map list submenu.\n"
					" N. Cancel current map.\n"
					" P. Toggle pause.\n"
					" R. Run a command on inactive sentries.\n"
					" U. Toggle use new positions (%s).\n"
					" B. Build new executable for distribution\n"
					" S. Skip bin file generation (implies above)\n"
					" T. Test server restart\n"
					" F. Force state advance (DANGEROUS)\n"
					" O. Open debug connection\n"
					" D. Open debug menu\n"
					" M. Run a command on server\n"
					" I. Map Timing Mode (prints only perf data)\n"
					"-----------------------------------------------------\n"
					" ESC.    Exit this menu.\n"
					"-----------------------------------------------------\n"
					"\n\n"
					"Enter selection: ",
					beacon_server.localOnly						? "ON" : "OFF",
					beacon_server.clients						? "YES" : "NO", 
					beacon_server.loadMaps						? "ON" : "OFF",
					beacon_server.forceRebuild					? "ON" : "OFF",
					beacon_server.fullCRCInfo					? "ON" : "OFF",
					beacon_server.generateOnly					? "ON" : "OFF",
					beacon_server.checkEncounterPositionsOnly	? "ON" : "OFF",
					beaconGenerateUseNewPositions(-1)			? "ON" : "OFF");
		}

		printMenu = 1;

		theChar = keyIsPressed ? _getch() : -1;
		switch(keyIsPressed ? tolower(theChar) : -1){
			xcase 0:
			case 224:{
				if(_kbhit()){
					{S32 unused = _getch();}
					done = 0;
				}
				printMenu = 0;
			}

			xcase 27:{
				done = 1;
			}

			xcase '1':{
				beaconPrintf(COLOR_YELLOW, "Printing memory usage...\n\n");
				beaconPrintMemory();
			}

			xcase '2':{
				S32 on = beacon_server.localOnly = !beacon_server.localOnly;
				beaconPrintf(COLOR_YELLOW, "Local IPs Only: %s\n\n", on ? "ON" : "OFF");
			}

			xcase '3':{
				beaconPrintf(COLOR_YELLOW, "Init network...\n\n");
				beaconServerInitNetwork();
			}

			xcase '4':{
				S32 on = beacon_server.loadMaps = !beacon_server.loadMaps;

				beaconPrintf(COLOR_YELLOW, "Load Maps: %s\n\n", on ? "ON" : "OFF");
			}

			xcase '5':{
				S32 on = beacon_server.forceRebuild = !beacon_server.forceRebuild;

				beaconPrintf(COLOR_YELLOW, "Forced Rebuild: %s\n\n", on ? "ON" : "OFF");
			}

			xcase '6':{
				S32 on = beacon_server.fullCRCInfo = !beacon_server.fullCRCInfo;

				beaconPrintf(COLOR_YELLOW, "Full CRC Info: %s\n\n", on ? "ON" : "OFF");
			}

			xcase '7':{
				S32 on = beacon_server.generateOnly = !beacon_server.generateOnly;

				beaconPrintf(COLOR_YELLOW, "Generate Only: %s\n\n", beacon_server.generateOnly ? "ON" : "OFF");
			}

			xcase '8':{
				S32 on = beacon_server.checkEncounterPositionsOnly = !beacon_server.checkEncounterPositionsOnly;

				beaconPrintf(COLOR_YELLOW, "Check Encounter Positions Only: %s\n\n", on ? "ON" : "OFF");
			}

			xcase '9':{
				beaconPrintf(COLOR_YELLOW, "Sending beaconizing request...\n\n");
				beacon_server.testRequestClient = 1;
				beacon_server.sendClientsToMe = 0;
			}

			xcase '0':{
				beaconPrintf(COLOR_YELLOW, "Kill clients...\n\n");
				beaconServerKillClientsMenu();
			}

			xcase 'c':{
				beaconPrintf(COLOR_YELLOW, "Client list...\n\n");
				beaconServerPrintClientStates();
			}

			xcase 'f':{
				beacon_server.force_state_advance = 1;
			}

			xcase 'g':{
				S32 on = beacon_server.noGimmeUsage = !beacon_server.noGimmeUsage;

				beaconPrintf(COLOR_YELLOW, "Gimme usage: %s\n\n", on ? "OFF" : "ON");
			}

			xcase 'w':{
				S32 on = beacon_server.writeCurMapData = !beacon_server.writeCurMapData;

				beaconPrintf(COLOR_YELLOW, "Write current map data: %s\n\n", on ? "ON" : "OFF");
			}

			xcase 'l':{
				if(!beacon_server.isMasterServer){
					beaconPrintf(COLOR_YELLOW, "Map list menu...\n\n");
					beaconServerMapListMenu();
				}else{
					startTime = time(NULL);
					printMenu = 0;
				}
			}

			xcase 'n':{
				beaconServerSetCurMap(NULL);
			}

			xcase 'p':{
				beacon_server.paused = !beacon_server.paused;
			}

			xcase 'r':{
				beaconPrintf(COLOR_YELLOW, "Running command on all inactive sentries...\n\n");

				beaconServerRunCommandOnSentry();
			}

			xcase 't':{
				beaconServerRestartAutoServer(beaconMyCmdLineParams(0));
			}
			
			xcase 'u':{
				S32 on = beaconGenerateUseNewPositions(!beaconGenerateUseNewPositions(-1));
				
				beaconPrintf(COLOR_YELLOW, "Use new positions: %s\n\n", on ? "ON" : "OFF");
			}

			xcase 'i': {
				beacon_server.minimalPrinting = !beacon_server.minimalPrinting;
				beacon_server.printTiming = beacon_server.minimalPrinting;
				loadstart_suppress_printing(true);
			}

			xcase 'd':{
				/*
				beacon_server.noUpdate = !beacon_server.noUpdate;
				
				beaconPrintf(COLOR_GREEN, "Skip Update for Patch: %s\n\n", !beacon_server.noUpdate ? "On" : "Off");
				*/
				beaconServerDebugMenu();
			}

			xcase 's':{
				
			}

			xcase 'b':{
				beaconServerMakeNewExecutable();
			}

			xcase 'm':{
				char buffer[MAX_PATH];

				beaconPrintf(COLOR_YELLOW, "Enter command: ");
				if(beaconEnterString(buffer, MAX_PATH))
				{
					globCmdParsef("%s", buffer);
				}
			}

			xcase 'o':{
				beaconServerOpenDebugConnection();
			}

			xcase 13:{
				beaconPrintf(COLOR_YELLOW, "Showing sentry menu...\n\n");
				beaconServerSentryMenu();
			}

			xdefault:{
				if(keyIsPressed){
					startTime = time(NULL);
				}

				printMenu = 0;
			}
		}

		if(done){
			break;
		}

		Sleep(10);
	}

	printf("Exiting main menu\n\n");
}

static void setConsolePosition(S32 x, S32 y){
	COORD pt = {x, y};

	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pt);
}

static const char* beaconServerGetTabName(BeaconServerDisplayTab tab){
	switch(tab){
		xcase BSDT_MAP:			return "Map";
		xcase BSDT_SENTRIES:	return "Sentries";
		xcase BSDT_SERVERS:		return "Servers";
		xcase BSDT_WORKERS:		return "Workers";
		xcase BSDT_REQUESTERS:	return "Requesters";
		xdefault:				return "Unknown";
	}
}

static void beaconServerSetCurTab(BeaconServerDisplayTab tab){
	if(tab < 0){
		tab = 0;
	}
	else if(tab >= BSDT_COUNT){
		tab = BSDT_COUNT - 1;
	}

	if(tab != beacon_server.display.curTab){
		beacon_server.display.curTab = tab;
		beacon_server.display.updated = 0;
	}
}

static void beaconServerUpdateDisplay(void){
	static struct {
		S32							init;

		BeaconServerDisplayTab		tab;

		char*						mapName;
	} last;

	if(beacon_server.display.updated || !backConsoleRefCount){
		return;
	}

	beacon_server.display.updated = 1;

	if(!last.init){
		last.tab = -1;
	}

	if(	beacon_server.curMapName &&
		(	!last.mapName ||
			stricmp(last.mapName, beacon_server.curMapName)))
	{
		estrCopy2(&last.mapName, beacon_server.curMapName);

		setConsolePosition(0, 0);

		beaconConsolePrintf(0, "Map: %s\nStuff: %s\n", beacon_server.curMapName, "lksjdf");
	}

	if(last.tab != beacon_server.display.curTab){
		S32 i;

		last.tab = beacon_server.display.curTab;

		setConsolePosition(0, 4);

		for(i = 0; i < BSDT_COUNT; i++){
			beaconConsolePrintf(i == beacon_server.display.curTab ? COLOR_GREEN : COLOR_WHITE, "%s%s", i ? "   " : "", beaconServerGetTabName(i));
		}
	}

	switch(beacon_server.display.curTab){
		xcase BSDT_MAP:{
			if(0) if(beacon_server.display.mapTab.updated){
				break;
			}

			beacon_server.display.mapTab.updated = 1;

			setConsolePosition(0, 6);

			displayBlockInfo();
		}

		xcase BSDT_SENTRIES:{
		}

		xcase BSDT_SERVERS:{
		}

		xcase BSDT_WORKERS:{
		}

		xcase BSDT_REQUESTERS:{
		}
	}
}

static void beaconServerCheckInput(void){
	U8 theChar;

	if(!IsWindowVisible(beaconGetConsoleWindow()) || !_kbhit()){
		return;
	}

	theChar = _getch();
	theChar = tolower(theChar);

	switch(theChar){
		xcase 0:{
			theChar = _getch();
			switch(theChar){
				xdefault:{
					beaconPrintf(COLOR_YELLOW, "unhandled extended key:   0+%d\n", theChar);
				}
			}
		}

		xcase 224:{
			theChar = _getch();
			switch(theChar){
				xcase 72:
					// Up.

					beaconServerMoveSelection(-1);
					beaconServerPrintClientStates();

				xcase 75:{
					// Left.

					beaconServerSetCurTab(beacon_server.display.curTab - 1);
				}

				xcase 77:{
					// Right.

					beaconServerSetCurTab(beacon_server.display.curTab + 1);
				}

				xcase 80:{
					// Down.

					beaconServerMoveSelection(1);
					beaconServerPrintClientStates();
				}

				xdefault:{
					beaconPrintf(COLOR_YELLOW, "unhandled extended key: 224+%d\n", theChar);
				}
			}
		}

		xcase 27:
		case 'm':{
			beaconServerMainMenu();
		}

		xcase 'h':{
			if(!beaconIsProductionMode()){
				ShowWindow(beaconGetConsoleWindow(), SW_HIDE);
			}
		}

		xcase '\t':{
			static S32 on;
			on = !on;
			if(on){
				beaconEnableBackgroundConsole();
			}else{
				beaconDisableBackgroundConsole();
			}
		}

		xdefault:{
			beaconPrintf(COLOR_YELLOW, "unhandled key: %d\n", theChar);
		}
	}
}

static void beaconServerUpdateData(void)
{
	if(!beacon_server.noUpdate && !beacon_server.noGimmeUsage)
	{
		char getlatest[MAX_PATH];
		int i;
		const char *const *dataDirs = NULL;

		strcpy(beacon_server.patchTime, timeGetLocalDateString());
		beacon_server.patcherTime = timeSecondsSince2000() + MAGIC_SS2000_TO_FILETIME;
		beacon_process.latestDataFileTime = timeSecondsSince2000();

		dataDirs = fileGetGameDataDirs();

		for(i=0; i<eaSize(&dataDirs); i++)
		{
			if(!strstr(dataDirs[i], "local") && !strstr(dataDirs[i], "src"))
			{
				sprintf(getlatest, "-glvfold %s -nopause", dataDirs[i]);
				gimmeDLLDoCommand(getlatest);
			}
		}

		//worldGridDeleteAllBins();
	}
}

static void beaconServerCheckForNewMap(void)
{
	int i;
	int needsProcessCount = 0;
	if(	(beacon_server.isRequestServer && !beacon_server.request.projectName) ||
		!beacon_server.loadMaps ||
		beaconServerHasMapLoaded())
	{
		return;
	}

	if(beacon_server.curMapIndex < 0)
		beacon_server.curMapIndex = 0;

	for(i=beacon_server.curMapIndex; i<eaSize(&beacon_server.serverStatus.mapStatus); i++)
	{
		BeaconServerMapStatus *stat = beacon_server.serverStatus.mapStatus[i];

		if(stat->needsProcess)
			needsProcessCount++;

		if(needsProcessCount > 20)
			break;
	}

	if(needsProcessCount <= 10)
	{
		for(i=eaSize(&beacon_server.serverStatus.mapStatus);
			i<eaSize(&beacon_server.queueList) && needsProcessCount < 20; 
			i++)
		{
			if(beaconServerUpdateStatus(beacon_server.queueList[i]))
				needsProcessCount++;
		}
	}

	// Find a new map to load, and load it.
	if(beacon_server.curMapIndex >= 0 && beacon_server.curMapIndex < eaSize(&beacon_server.queueList))
	{
		beaconServerSetCurMap(beacon_server.queueList[beacon_server.curMapIndex]);
		beacon_server.curMapIndex++;
	}

	if(!beaconServerHasMapLoaded())
	{
		if(	beacon_server.isAutoServer &&
			beacon_server.curMapIndex >= eaSize(&beacon_server.queueList))
		{
			beaconServerRestartAutoServerAndDie("Done processing maps.", 1);
		}

		if(beacon_server.isRequestServer &&
			beacon_server.curMapIndex >= eaSize(&beacon_server.queueList))
		{
			beacon_server.request.completed_project = true;
		}

		if( beacon_server.isPseudoAuto && beacon_server.curMapIndex >= eaSize(&beacon_server.queueList))
		{
			if(eaSize(&beacon_server.queueList)>0)
			{
				beaconServerForEachClient(beaconServerDisconnectClientCallback, "All maps are done processing.");

				//beaconServerUpdateData();

				printf("Starting at beginning of queue\n\n");
				beacon_server.curMapIndex = 0;
			}
		}

		if(beacon_server.curMapIndex < eaSize(&beacon_server.queueList)){
			beaconServerSetCurMap(beacon_server.queueList[beacon_server.curMapIndex]);
		}
	}

	if(beaconServerHasMapLoaded()){
		if(!beaconServerBeginMapProcess(beacon_server.curMapName)){
			beaconServerSetCurMap(NULL);
			beaconPrintf(COLOR_RED, "CANCELING PROCESS!\n");
		}
	}
}

static void beaconServerDoStateNotStarted(void){
	beaconServerSetState(BSS_GENERATING);

	beaconServerUpdateTitle("Init generator: %s", beacon_server.curMapName);

	beaconInitGenerating(worldGetAnyActiveColl(), 0);

	if(	beacon_process.world_min_xyz[0] >= beacon_process.world_max_xyz[0] ||
		//beacon_process.world_min_xyz[1] >= beacon_process.world_max_xyz[1] ||
		beacon_process.world_min_xyz[2] >= beacon_process.world_max_xyz[2] )
	{
		return;
	}

	// Insert the legal beacons into their blocks.

	beaconInsertLegalBeacons();

	if(!beacon_server.minimalPrinting)
		beaconPrintf(COLOR_GREEN, "Ready to generate!\n");
}

static void beaconServerDoStateGenerating(void){
	BeaconDiskSwapBlock* block;
	S32 assignedCount = 0;
	S32 availableCount = 0;
	S32 closedCount = 0;
	S32 unused = 0;

	beacon_server.beaconGenerate.totalBlocks = 0;
	for(block = bp_blocks.list; block; block = block->nextSwapBlock){
		if(block->clients.count){
			assignedCount++;
		}
		else if(block->legalCompressed.uncheckedCount){
			availableCount++;
		}
		else if(block->legalCompressed.totalCount){
			closedCount++;
			beacon_server.beaconGenerate.closedBlocks = closedCount;
		}
		else{
			unused++;
		}
		beacon_server.beaconGenerate.totalBlocks++;
	}

	beaconServerUpdateTitle("Generate: %d assigned, %d available, %d closed, %d unused",
							assignedCount,
							availableCount,
							closedCount,
							unused);

	if((!availableCount && !assignedCount) || beacon_server.force_state_advance){
		beacon_server.force_state_advance = 0;

		beaconServerSetState(BSS_CONNECT_BEACONS);

		beaconServerAddAllMadeBeacons();
	}
}

static void beaconServerDoStateConnecting(void){
	S32 done = 1;
	static lastAvail = 0, lastUngroup = 0, lastUnreach = 0;

	if(!beacon_server.generateOnly){
		U32 update = 0;
		
		S32 availableCount =	beacon_server.beaconConnect.groups.count -
								beacon_server.beaconConnect.assignedCount -
								beacon_server.beaconConnect.finishedCount;

		S32 ungroupedCount =	ea32Size(&beacon_server.beaconConnect.legalBeacons.indices) -
								beacon_server.beaconConnect.legalBeacons.groupedCount;

		S32 unreachedCount =	combatBeaconArray.size -
								ea32Size(&beacon_server.beaconConnect.legalBeacons.indices);

		update = lastAvail!=availableCount || lastUngroup!=ungroupedCount || lastUnreach!=unreachedCount;

		if(update)
		{
			beaconServerUpdateTitle("Connect: %d dist, %d todo, %d done, %d ungrouped, %s / %s unreached",
									beacon_server.beaconConnect.assignedCount,
									availableCount,
									beacon_server.beaconConnect.finishedBeacons,
									ungroupedCount,
									getCommaSeparatedInt(unreachedCount),
									getCommaSeparatedInt(combatBeaconArray.size));
		}

		lastAvail = availableCount;
		lastUngroup = ungroupedCount;
		lastUnreach = unreachedCount;

		done =	!availableCount &&
				!ungroupedCount &&
				!beacon_server.beaconConnect.assignedCount;
	}

	if(done)
	{
		// And we're done connecting beacons.

		if(!beacon_server.minimalPrinting)
			printf("\n\nFinished connecting beacons: %s\n\n\n", beaconCurTimeString(0));

		beaconServerSetState(BSS_WRITE_FILE);

		beacon_server.sendClientsToMe = 0;

		beaconServerSetSendStatus();
	}
}

void beaconServerWriteFileCallback(const void* data, U32 size){
	dynArrayFitStructs(	&beacon_server.beaconFile.uncompressed.data,
						&beacon_server.beaconFile.uncompressed.maxByteCount,
						beacon_server.beaconFile.uncompressed.byteCount + size);

	memcpy(beacon_server.beaconFile.uncompressed.data + beacon_server.beaconFile.uncompressed.byteCount, data, size);

	beacon_server.beaconFile.uncompressed.byteCount += size;
}

static void beaconServerCompressBeaconFile(void){
	S32 ret;

	beacon_server.beaconFile.compressed.byteCount = beacon_server.beaconFile.uncompressed.byteCount * 1.0125 + 12;

	dynArrayFitStructs(	&beacon_server.beaconFile.compressed.data,
						&beacon_server.beaconFile.compressed.maxByteCount,
						beacon_server.beaconFile.compressed.byteCount);

	ret = compress2(beacon_server.beaconFile.compressed.data,
					&beacon_server.beaconFile.compressed.byteCount,
					beacon_server.beaconFile.uncompressed.data,
					beacon_server.beaconFile.uncompressed.byteCount,
					5);

	assert(ret == Z_OK);

	beacon_server.beaconFile.crc = freshCRC(beacon_server.beaconFile.compressed.data,
											beacon_server.beaconFile.compressed.byteCount);
}

static void beaconServerCreateMsgHeader(char **msg)
{
	estrPrintf(msg, "Computer: %s - User: %s\n", getComputerName(), getUserName());
	estrConcatf(msg, "Project: %s - Branch: %d\nCurrent gimme-time: %s\n", 
		GetProductName(), beacon_server.gimmeBranchNum, beacon_server.patchTime);
	estrConcatf(msg, "Processing Version: %d\n", beaconFileGetProcVersion());
	estrConcatf(msg, "Beaconizer SVN: %d (excluding local modifications)\n", gBuildVersion);
	estrConcatf(msg, "Beaconizer CRC: %x\n", beacon_server.exeFile.crc);
}

static void beaconCountConnections(int *gbcount, int *rbcount)
{
	int i;

	for(i=0; i<combatBeaconArray.size; i++)
	{
		Beacon *b = combatBeaconArray.storage[i];

		*gbcount += b->gbConns.size;
		*rbcount += b->rbConns.size;
	}
}

static int beaconServerCreateStatusEmailMessage(char **msg, char **title)
{
	int i;
	int galaxy_count = 0;
	int gbcount = 0, rbcount = 0;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);
	
	for(i = 0; i < beacon_galaxy_group_count; i++)
	{
		galaxy_count += partition->combatBeaconGalaxyArray[i].size;
	}

	estrPrintf(title, "Beaconized: %s Branch %d - %s",
		GetProductName(),
		beacon_server.gimmeBranchNum,
		beacon_server.curMapName);

	if(beacon_server.spawn_count==0 && strstri(zmapInfoGetFilename(NULL), "TestMaps"))
		return 0;

	beaconCountConnections(&gbcount, &rbcount);
	
	if(beacon_server.spawn_count==0)
	{
		estrConcatf(title, " - Warning 0 Spawn Points");
	}
	else if(combatBeaconArray.size==0)
	{
		estrConcatf(title, " - Warning 0 Beacons");
	}
	else if(beacon_server.fileBeaconCount)
	{
		F32 beaconRatio;

		beaconRatio = (combatBeaconArray.size-beacon_server.fileBeaconCount)/beacon_server.fileBeaconCount;
		if(beaconRatio>0.3 || beaconRatio<-0.2)
			estrConcatf(title, " - Warning %.2f Change in Beacons", beaconRatio);
		else if(beacon_server.fileConnectionCount)
		{
			F32 connRatio;

			connRatio = (rbcount+gbcount-beacon_server.fileConnectionCount)/beacon_server.fileConnectionCount;
			if(connRatio>0.4 || connRatio<-0.2)
				estrConcatf(title, " - Warning %.2f Change in Conns", connRatio);
		}
	}

	estrConcatf(msg, "Map completed: %s\n", beacon_server.curMapName);
	estrConcatf(msg, "Full filename: %s\n", zmapInfoGetFilename(NULL));
	estrConcatf(msg, "Map status messages:\n");
	for(i=0; i<eaSize(&beacon_server.statusMessages); i++)
	{
		estrConcatf(msg, "\t%s\n", beacon_server.statusMessages[i]);
	}
	estrConcatf(msg, "Map Statistics:\n"
					 "\tBeacons:\n"
					 "\t\tSpawn Positions: %d\n"
					 "\t\tUseful Positions: %d\n"
					 "\t\tGenerated Beacons: %d\n"
					 "\t\tPost-Connect Beacons: %d\n"
					 "\tBeacon Blocks:\n"
					 "\t\tBlocks: %d\n"
					 "\t\tGalaxies: %d\n"
					 "\t\tClusters: %d\n"
					 "\tConnections:\n"
					 "\t\tGround Connections: %d - Pruned: %d/%d\n"
					 "\t\tRaised Connections: %d\n"
					 "\tGeoCRC: %x   CfgCRC: %x   EncCRC: %x\n\n",
					 beacon_server.spawn_count, beacon_server.useful_count, beacon_server.generate_count,
					 combatBeaconArray.size, partition->combatBeaconGridBlockArray.size, galaxy_count, 
					 partition->combatBeaconClusterArray.size, gbcount, beacon_server.optional_pruned, 
					 beacon_server.optional, rbcount, beacon_process.mapMetaData->geoCRC,
					 beacon_process.mapMetaData->cfgCRC, beacon_process.mapMetaData->encCRC);
	estrConcatf(msg, "Maps currently on queue:\n");

	if(beacon_server.curMapIndex>0)
	{
		for(i=beacon_server.curMapIndex; i<eaSize(&beacon_server.queueList); i++)
		{
			estrConcatf(msg, "\t%s\n", beacon_server.queueList[i]);
		}
	}

	return 1;
}

static const char* getMailServerIP(void)
{
	const char* smtpAnswer = smtpGetMailServer();
	return smtpAnswer ? smtpAnswer : SMTP_DEFAULT_SERVER;
}

void beaconServerSendEmailMsg(const char *title, char **msg, const char *to)
{
	char *systemstr = NULL;
	char from[] = "beaconizerNoReply@" ORGANIZATION_DOMAIN;
	static char *msgAndHeader = NULL;

	if(beacon_server.noGimmeUsage && !beacon_server.minimalPrinting)
	{
		printf("%s", *msg);
		return;
	}

	estrStackCreate(&systemstr);

	estrPrintf(&systemstr, "n:\\bin\\bmail.exe -s %s -t \"%s\" -f \"%s\" -a \"%s\"",
							getMailServerIP(), to, from, title);

	estrClear(&msgAndHeader);
	beaconServerCreateMsgHeader(&msgAndHeader);
	estrConcatf(&msgAndHeader, "\n%s", msg ? *msg : "");

	if(estrLength(&msgAndHeader)<4096)
	{
		estrConcatf(&systemstr, " -b \"%s\"", msgAndHeader);
	}
	else
	{
		char bodyfilename[MAX_PATH];
		FILE *f;

		sprintf(bodyfilename, "%semailtmp.txt", beaconGetExeDirectory());

		makeDirectoriesForFile(bodyfilename);
		f = fopen(bodyfilename, "w");
		fwrite(msgAndHeader, estrLength(&msgAndHeader), 1, f);
		fclose(f);

		estrConcatf(&systemstr, " -m %s -c", bodyfilename);
	}

	system_detach(systemstr, 1, 1);
	estrDestroy(&systemstr);
}

static void beaconServerEmailMapComplete(void)
{
	char *msg = NULL;
	char *title = NULL;

	estrStackCreate(&msg);
	estrStackCreate(&title);

	if(beaconServerCreateStatusEmailMessage(&msg, &title))
		beaconServerSendEmailMsg(title, &msg, "beaconizer@" ORGANIZATION_DOMAIN); // this one is considered spammy so it goes to the regular 'non-human' beaconizer list.

	estrDestroy(&msg);
	estrDestroy(&title);
}

void beaconServerEmailMapFailure(BeaconMapFailureReason reason, char *failMsg)
{
	char *title = NULL;
	char *msg = NULL;

	if(beacon_process.fileMetaData->failureReason & reason)
		return;

	estrStackCreate(&title);
	estrStackCreate(&msg);

	estrPrintf(&title, "ERROR while beaconizing: %s Branch %d - %s",
		GetProductName(),
		beacon_server.gimmeBranchNum,
		beacon_server.curMapName);

	estrPrintf(&msg, "%s", failMsg);
	beaconServerSendEmailMsg(title, &msg, "beaconizer-users@" ORGANIZATION_DOMAIN); // this one is an error so it goes to the human beaconizer list.

	estrDestroy(&title);
	estrDestroy(&msg);

	StructReset(parse_BeaconMapMetaData, beacon_process.mapMetaData);
	StructCopy(parse_BeaconMapMetaData, beacon_process.fileMetaData, beacon_process.mapMetaData, 0, 0, 0);
	beacon_process.mapMetaData->failureReason |= reason;
	beaconWriteDateFile(!beacon_server.noGimmeUsage);
	beaconCheckinDateFile();
}

typedef struct BeaconDynConnDeleteData {
	Beacon *b;
	S32 *raised;
	S32 *index;
} BeaconDynConnDeleteData;

static void freeBeaconDynConnDef(BeaconDynConnDeleteData *def)
{
	free(def);
}

static void beaconServerProcessDynConns(void *unused, Beacon *b, BeaconConnection *conn, S32 raised, S32 index)
{
	Array *a = raised ? &b->rbConns : &b->gbConns;
	conn = a->storage[index];
	devassert(!unused);
	arrayRemoveAndFill(a, index);

	destroyBeaconConnection(conn);
}

static void beaconServerFindDynConns(void* userPointer,
									 const WorldCollObjectTraverseParams* params)
{
	WorldCollStoredModelData *smd = NULL;
	WorldCollModelInstanceData *inst = NULL;

	wcoGetStoredModelData(&smd, &inst, params->wco, WC_FILTER_BIT_MOVEMENT);
	if(smd && inst && inst->transient)
	{
		Vec3 world_min, world_max;
		mulBoundsAA(smd->min, smd->max, inst->world_mat, world_min, world_max);
		beaconDynConnProcessMesh(	smd->verts,
									smd->vert_count,
									smd->tris,
									smd->tri_count,
									smd->min,
									smd->max,
									inst->world_mat, 
									beaconServerProcessDynConns, 
									userPointer);
	}

	SAFE_FREE(inst);
}

static void beaconServerPreDeleteDynConns(int iPartitionIdx)
{
	if(beaconDynConnAllowedToSet())
		return;

	wcTraverseObjects(worldGetActiveColl(iPartitionIdx), beaconServerFindDynConns, NULL, NULL, NULL, /*unique=*/1, WCO_TRAVERSE_STATIC);
}

static void beaconServerPruneOptionalConns(int iPartitionIdx)
{
	int i;

	beaconRebuildBlocks(!beacon_server.generateOnly, beacon_server.isRequestServer || beacon_server.minimalPrinting, 0);
	
	beaconSetPathFindEntityAsParameters(0, 0, 0, 1, 0, 0);

	for(i=0; i<combatBeaconArray.size; i++)
	{
		int j;
		Beacon *b = combatBeaconArray.storage[i];

		for(j=0; j<b->gbConns.size; j++)
		{
			BeaconConnection *conn = b->gbConns.storage[j];

			if(conn->gflags.optional)
			{
				// Pathfind from b to conn->target
				NavSearchResultType result;
				NavPath path = {0};

				result = beaconPathFindBeacon(iPartitionIdx, &path, b, conn->destBeacon);

				beacon_server.optional++;

				if(result == NAV_RESULT_SUCCESS)
				{
					// Found a path without optional conns
					// Determine distance.  If short enough, remove this conn
					int k;
					F32 distance = 0;
					int pruned = 0;

					Beacon *last_beacon = b;

					for(k=0; k<eaSize(&path.waypoints); k++)
					{
						NavPathWaypoint *wp = path.waypoints[k];

						if(!wp->connectionToMe)
						{
							assert(wp->beacon == b && k==0);
						}
						else
						{
							distance += distance3(last_beacon->pos, wp->beacon->pos);

							last_beacon = wp->beacon;
						}
					}

					pruned = distance < distance3(b->pos, conn->destBeacon->pos) * BEACON_OPTIONAL_DIST_MULT;

					if(beacon_server.debug_state && beacon_server.debug_state->send_prune)
					{
						beaconServerSendDebugLine(BDO_PRUNE, b->pos, conn->destBeacon->pos, 
													pruned ? 0xFFFF0000 : 0xFF00FF00);
					}

					if(pruned)
					{
						arrayRemoveAndFill(&b->gbConns, j);
						j--;

						beacon_server.optional_pruned++;
					}
				}
				eaDestroyExFileLine(&path.waypoints, destroyNavPathWaypointEx);
			}
		}
	}
}

static int beaconCmpDestYawCached(const Beacon *b, const BeaconConnection **left, const BeaconConnection **right)
{
	F32 left_yaw, right_yaw;
	Vec3 diff;
	subVec3((*left)->destBeacon->pos, b->pos, diff);
	left_yaw = getVec3Yaw(diff);
	subVec3((*right)->destBeacon->pos, b->pos, diff);
	right_yaw = getVec3Yaw(diff);

	(*left)->destBeacon->userFloat = left_yaw;
	(*right)->destBeacon->userFloat = right_yaw;

	return left_yaw > right_yaw ? -1 : (left_yaw < right_yaw ? 1 : 0);
}

static void beaconServerPruneRedundantConns(void)
{
	int i;

	for(i=0; i<combatBeaconArray.size; i++)
	{
		int j;
		Beacon *b = combatBeaconArray.storage[i];

		qsort_s(b->gbConns.storage, b->gbConns.size, sizeof(BeaconConnection*), beaconCmpDestYawCached, b);

		for(j=0; j<b->gbConns.size; j++)
		{
			int k;
			BeaconConnection *conn = b->gbConns.storage[j];
			F32 yaw = asinf(conn->destBeacon->userFloat);
			for(k=1; k<b->gbConns.size; k++)
			{
				BeaconConnection *other = b->gbConns.storage[(j+k)%b->gbConns.size];
				F32 diff;

				if(other->destBeacon!=conn->destBeacon)
					continue;

				diff = asinf(other->destBeacon->userFloat);
				diff = subAngle(yaw, diff);
				if(fabs(diff)>RAD(20))
					continue;

				// Remove one!?
			}
		}

		qsort_s(b->rbConns.storage, b->rbConns.size, sizeof(BeaconConnection*), beaconCmpDestYawCached, b);
		for(j=0; j<b->rbConns.size; j++)
		{
			int k;
			BeaconConnection *conn = b->rbConns.storage[j];
			F32 yaw = asinf(conn->destBeacon->userFloat);
			for(k=1; k<b->rbConns.size; k++)
			{
				BeaconConnection *other = b->rbConns.storage[(j+k)%b->rbConns.size];
				F32 diff;

				if(other->destBeacon!=conn->destBeacon)
					continue;

				diff = asinf(other->destBeacon->userFloat);
				diff = subAngle(yaw, diff);
				if(fabs(diff)>RAD(20))
					continue;

				// Remove one!?
			}
		}
	}
}

static void beaconCheckForbiddenPositions(int iPartitionIdx)
{
	int i;
	static Beacon **startBeacons = NULL;
	static WorldForbiddenPosition **forbiddenPositions;
	WorldRegion **regions = worldGetAllWorldRegions();

	eaClear(&startBeacons);
	for(i=0; i<combatBeaconArray.size; i++)
	{
		Beacon *b = combatBeaconArray.storage[i];

		if(b->isValidStartingPoint)
			eaPush(&startBeacons, b);
	}

	eaClear(&forbiddenPositions);
	FOR_EACH_IN_EARRAY(regions, WorldRegion, r)
	{
		WorldForbiddenPosition **regionFPs = worldRegionGetForbiddenPositions(r);

		eaPushEArray(&forbiddenPositions, &regionFPs);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(forbiddenPositions, WorldForbiddenPosition, wfp)
	{
		FOR_EACH_IN_EARRAY(startBeacons, Beacon, b)
		{
			NavSearchResultType result;
			NavPath path = {0};

			result = beaconPathFind(iPartitionIdx, &path, b->pos, wfp->position, NULL);

			if(result == NAV_RESULT_SUCCESS)
			{
				SimplePath *sp = StructCreate(parse_SimplePath);

				FOR_EACH_IN_EARRAY(path.waypoints, NavPathWaypoint, wp)
				{
					SimpleWaypoint *swp = StructCreate(parse_SimpleWaypoint);

					copyVec3(wp->pos, swp->pos);

					eaPush(&sp->waypoints, swp);
				}
				FOR_EACH_END;

				eaPush(&beacon_process.mapMetaData->invalidPaths, sp);
				pushDontReportErrorsToErrorTracker(0);
				globCmdParsef("ignoreallerrors 0");
				ErrorFilenamef(zmapGetFilename(NULL), 
							"Map has invalid path from "LOC_PRINTF_STR" to "LOC_PRINTF_STR,
							vecParamsXYZ(b->pos), vecParamsXYZ(wfp->position));
				globCmdParsef("ignoreallerrors 1");
				popDontReportErrorsToErrorTracker();
				break;
			}

			navPathClear(&path);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

void beaconServerCompileStats(void)
{
	beacon_process.mapMetaData->beaconCount = combatBeaconArray.size;
	beaconCountConnections(&beacon_process.mapMetaData->beaconGroundCount, &beacon_process.mapMetaData->beaconRaisedCount);
}

static bool checkForSpecial(BeaconBlock *block)
{
	int i;
	if (block->galaxyPrunerHasSpecial)
	{
		return true;
	}
	block->galaxyPruneChecking = 1;
	for(i = 0; i < block->beaconArray.size; i++)
	{
		if (((Beacon*)block->beaconArray.storage[i])->isSpecial)
		{
			block->galaxyPrunerHasSpecial = 1;
			block->galaxyPruneChecking = 0;
			return true;
		}
	}
	for(i = 0; i < block->subBlockArray.size; i++)
	{
		BeaconBlock *subBlock = block->subBlockArray.storage[i];
		if ((!subBlock->galaxyPruneChecking) && (checkForSpecial(subBlock)))
		{
			block->galaxyPrunerHasSpecial = 1;
			block->galaxyPruneChecking = 0;
			return true;
		}
	}
	block->galaxyPruneChecking = 0;
	return false;
}

static void removeArrayIndex(Array *array, int i)
{
	assert(i >= 0 && i < array->size);
	array->storage[i] = array->storage[--array->size];
}

static void DestroyGalaxyContents(BeaconBlock *block, BeaconStatePartition *partition)
{
	int i;
	BeaconBlockConnection *connB, *revConnB;
	BeaconBlock *gridBlock;
	
	if (block->beaconArray.size)
	{
		Beacon *beacon, *beacon2;
		BeaconConnection *conn;
		int j;
		
		while (block->beaconArray.size)
		{
			beacon = block->beaconArray.storage[0];
			for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++)
			{
				gridBlock = partition->combatBeaconGridBlockArray.storage[i];
				if (arrayFindAndRemoveFast(&gridBlock->beaconArray, beacon) != -1)
				{
					break;
				}
			}
			arrayFindAndRemoveFast(&combatBeaconArray, beacon);
			for(i = 0; i < combatBeaconArray.size; i++)
			{
				beacon2 = combatBeaconArray.storage[i];
				for (j = 0; j < beacon2->gbConns.size; j++)
				{
					conn = beacon2->gbConns.storage[j];
					if (conn->destBeacon == beacon)
					{
						arrayFindAndRemoveFast(&beacon2->gbConns, conn);
						destroyBeaconConnection(conn);
						j--;
					}
				}
				for (j = 0; j < beacon2->rbConns.size; j++)
				{
					conn = beacon2->rbConns.storage[j];
					if (conn->destBeacon == beacon)
					{
						arrayFindAndRemoveFast(&beacon2->rbConns, conn);
						destroyBeaconConnection(conn);
						j--;
					}
				}
			}
			arrayFindAndRemoveFast(&block->beaconArray, beacon);
			destroyCombatBeacon(beacon);
			beaconPrintf(COLOR_BLUE, ".");
		}
	}
	while (block->gbbConns.size)
	{
		connB = block->gbbConns.storage[0];
		revConnB = beaconBlockGetConnection(connB->destBlock, connB->srcBlock, false);
		if (revConnB)
		{
			arrayFindAndRemoveFast(&revConnB->srcBlock->gbbConns, revConnB);
			beaconBlockConnectionDestroy(revConnB);
		}
		arrayFindAndRemoveFast(&block->gbbConns, connB);
		beaconBlockConnectionDestroy(connB);
	}
	while (block->rbbConns.size)
	{
		connB = block->rbbConns.storage[0];
		revConnB = beaconBlockGetConnection(connB->destBlock, connB->srcBlock, true);
		if (revConnB)
		{
			arrayFindAndRemoveFast(&revConnB->srcBlock->rbbConns, revConnB);
			beaconBlockConnectionDestroy(revConnB);
		}
		arrayFindAndRemoveFast(&block->rbbConns, connB);
		beaconBlockConnectionDestroy(connB);
	}
	if (block->isGalaxy)
	{
		BeaconBlock *subBlock;
		bool subIsGalaxy;

		while (block->subBlockArray.size)
		{
			subBlock = block->subBlockArray.storage[0];
			subIsGalaxy = subBlock->isGalaxy;
			DestroyGalaxyContents(subBlock, partition);

			if (subIsGalaxy)
			{
				for(i = 0; i < beacon_galaxy_group_count; i++)
				{
					if (arrayFindAndRemoveFast(&partition->combatBeaconGalaxyArray[i], subBlock) != -1)
					{
						break;
					}
				}
			}
			else
			{
				for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++)
				{
					gridBlock = partition->combatBeaconGridBlockArray.storage[i];
					if (arrayFindAndRemoveFast(&gridBlock->subBlockArray, subBlock) != -1)
					{
						break;
					}
				}
			}
			arrayFindAndRemoveFast(&block->subBlockArray, subBlock);
		}
		beaconGalaxyDestroy(block);
		return;
	}
	else if (block->isSubBlock)
	{
		beaconSubBlockDestroy(block);
		return;
	}
}

static void beaconServerDoStateWriteFile(void)
{
	int i;
	int iPartitionIdx = worldGetAnyCollPartitionIdx();

	beaconServerForEachClient(beaconServerDisconnectClientCallback, "Server is done processing.");

	// Don't do the writing until all clients have disconnected.

	if(	listenCount(beacon_server.clients) &&
		beaconTimeSince(beacon_server.setStateTime) < 10)
	{
		beaconServerUpdateTitle("Waiting for %d clients to disconnect: %ds",
								listenCount(beacon_server.clients),
								10 - beaconTimeSince(beacon_server.setStateTime));
		return;
	}

	if(!beacon_server.status.acked){
		beaconServerUpdateTitle("Waiting for master server to ACK my status");

		if(beacon_server.status.timeSent && 
			beaconTimeSince(beacon_server.status.timeSent)>5)
		{
			beaconServerSetSendStatus();
		}

		return;
	}

	for(i = 0; i < BPP_COUNT; i++)
	{
		beacon_process.mapMetaData->beaconClientSeconds[i] = timerSeconds64(beacon_server.clientTicks[i]);
	}

	beaconServerPreDeleteDynConns(worldGetAnyCollPartitionIdx());

	beaconServerPruneOptionalConns(iPartitionIdx);

	beaconCheckInvalidSpawns();
	beaconCheckForbiddenPositions(iPartitionIdx);

	if(beacon_server.debug_state && beacon_server.debug_state->send_pre_rebuild)
	{
		for(i = 0; i < combatBeaconArray.size; i++)
		{
			Beacon *b = combatBeaconArray.storage[i];
			beaconServerSendDebugPoint(BDO_PRE_REBUILD, b->pos, 0xFF0000FF);
		}
	}

	if (beacon_galaxy_group_count < MAX_BEACON_GALAXY_GROUP_COUNT)
	{
		int j;
		Beacon *beacon;
		BeaconBlock *block;
		BeaconStatePartition *partition = beaconStatePartitionGet(0, false);
		beaconPrintf(COLOR_BLUE, "Pruning beacons\n");
		i = beacon_galaxy_group_count - 1;
		for(j = partition->combatBeaconGalaxyArray[i].size - 1; j >= 0; j--)
		{
			block = partition->combatBeaconGalaxyArray[i].storage[j];
			if (!checkForSpecial(block))
			{
				DestroyGalaxyContents(block, partition);
				removeArrayIndex(&partition->combatBeaconGalaxyArray[i], j--);
			}
		}
		beaconPrintf(COLOR_BLUE, "\nPruning complete\n");
		for(i = 0; i < combatBeaconArray.size; i++)
		{
			beacon = combatBeaconArray.storage[i];
			beacon->globalIndex = i;
		}
	}
	
	beaconRebuildBlocks(!beacon_server.generateOnly, beacon_server.isRequestServer || beacon_server.minimalPrinting, 0);

	if(beacon_server.debug_state && beacon_server.debug_state->send_post_rebuild)
	{
		for(i = 0; i < combatBeaconArray.size; i++)
		{
			Beacon *b = combatBeaconArray.storage[i];
			beaconServerSendDebugPoint(BDO_POST_REBUILD, b->pos, 0xFF0000FF);
		}
	}

	if(beacon_server.spawn_count==0)
		beacon_process.mapMetaData->mapWarning |= BCN_MAPWARN_NO_SPAWNS;
	else
		beacon_process.mapMetaData->mapWarning &= ~BCN_MAPWARN_NO_SPAWNS;

	// Before we clear data, let's compile some stats
	beaconServerCompileStats();

	if(beacon_server.fileBeaconCount && !combatBeaconArray.size)
	{
		beaconServerEmailMapFailure(BCN_MAPFAIL_NO_BEACONS, "No beacons on map after beaconizing, but beacons were there before.  Probably just need to rebuild the beaconizer.");
		beaconServerSetState(BSS_DONE);
		return;
	}

	if(beacon_process.fileMetaData->mapWarning & BCN_MAPWARN_NO_SPAWNS && beacon_server.spawn_count==0)
	{
		beaconServerSetState(BSS_DONE);
		return;
	}

	if(!beacon_process.is_new_file && !beacon_server.fileBeaconCount && !combatBeaconArray.size)
	{
		beaconServerEmailMapComplete();
		beaconServerSetState(BSS_DONE);
		return;
	}

	beacon_server.beaconFile.uncompressed.byteCount = 0;

	beaconServerUpdateTitle("Writing beacon file to disk!");
	writeBeaconFileCallback(beaconServerWriteFileCallback, 1);

	beaconServerEmailMapComplete();
	
	// It's already written and the file can take a bit
	beaconClearBeaconData();

	if(!beacon_server.noGimmeUsage)
		beaconCheckoutBeaconFiles(beacon_server.forceRebuild, false);

	beaconServerCompressBeaconFile();

	beacon_process.mapMetaData->zippedSize = beacon_server.beaconFile.compressed.byteCount;
	beacon_process.mapMetaData->unzippedSize = beacon_server.beaconFile.uncompressed.byteCount;

	if(!beacon_server.isRequestServer || beacon_server.request.projectName){
		beaconServerUpdateTitle("Writing beacon file to disk!");
		beaconWriteCompressedFile(beacon_process.beaconFileName, 
									beacon_server.beaconFile.compressed.data,
									beacon_server.beaconFile.compressed.byteCount,
									!beacon_server.noGimmeUsage,
									STACK_SPRINTF("Beacon Files for %s", beacon_server.curMapName));

		if(!beacon_server.minimalPrinting)
			printf("Done!\n");

		if(beacon_server.isRequestServer)
		{
			eaPush(&beacon_server.request.fileNames, strdup(beacon_process.beaconFileName));
			eaPush(&beacon_server.request.fileNames, strdup(beacon_process.beaconDateFileName));
			eaPush(&beacon_server.request.fileNames, strdup(beacon_process.beaconInvalidFileName));
		}
	}else{
		beaconPrintf(	COLOR_GREEN,
						"Beacon file: %s bytes\n",
						getCommaSeparatedInt(beacon_server.beaconFile.compressed.byteCount));
	}

	beacon_server.stateDetails.lastCheckedIn = beacon_server.curMapIndex;
	beacon_server.statusDirty = true;

	if(!beacon_server.minimalPrinting)
	{
		beaconPrintf(	COLOR_GREEN,
						"Done (%s), Map: %s\n",
						beaconCurTimeString(0),
						beacon_server.curMapName);
	}

	beaconServerSetState(BSS_DONE);
}

static void beaconServerSendBeaconFileToMaster(void){
	U32 remaining = beacon_server.beaconFile.compressed.byteCount - beacon_server.beaconFile.sentByteCount;
	U32 bytesToSend = min(remaining, 64 * 1024);

	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_server.master_link, BMSG_C2ST_BEACON_FILE);

		pktSendBitsPack(pak, 1, beacon_server.request.nodeUID);
		pktSendString(pak, beacon_server.request.uniqueStorageName);
		pktSendBitsPack(pak, 1, beacon_server.beaconFile.sentByteCount);
		pktSendBitsPack(pak, 1, bytesToSend);
		pktSendBitsPack(pak, 1, beacon_server.beaconFile.compressed.byteCount);
		pktSendBitsPack(pak, 1, beacon_server.beaconFile.uncompressed.byteCount);
		pktSendBitsPack(pak, 1, beacon_server.beaconFile.crc);

		pktSendBytes(	pak,
						bytesToSend,
						beacon_server.beaconFile.compressed.data + beacon_server.beaconFile.sentByteCount);

		beacon_server.beaconFile.sentByteCount += bytesToSend;

	BEACON_CLIENT_PACKET_SEND();

	if(beacon_server.beaconFile.sentByteCount == beacon_server.beaconFile.compressed.byteCount){
		estrDestroy(&beacon_server.request.uniqueStorageName);
		ZeroStruct(&beacon_server.request);
		beaconServerSetState(BSS_DONE);
	}else{
		beaconServerSetState(BSS_SEND_BEACON_FILE);
	}
}

static void beaconServerCountClientPackets(BeaconServerClientData* client, S32 index, S32* curSendQueue){
	*curSendQueue += 0;//fixme newnet qGetSize(client->link->sendQueue2);
}

static void printClientSendQueue(BeaconServerClientData* client, S32 index, void* userData){
	printf(	"%20s%20s%20s:%d\n",
			getClientIPStr(client),
			client->computerName,
			client->userName,
			0); //fixme newnet qGetSize(client->link->sendQueue2));
}

static void beaconServerGatherMapRequests(	BeaconServerClientData* client,
											S32 index,
											BeaconServerClientData ***clients)
{
	if(client->state==BCS_NEEDS_MORE_MAP_DATA || client->state==BCS_RECEIVING_MAP_DATA)
	{
		eaPush(clients, client);
	}
}

static S32 mapDataCmp(const BeaconServerClientData **client1, const BeaconServerClientData **client2)
{
	return (*client2)->mapData.sentByteCount - (*client1)->mapData.sentByteCount;
}

static void beaconServerProcessClients(void){
	BeaconServerProcessClientsData pcd = {0};

	beacon_server.beaconConnect.assignedCount = 0;

	beaconServerForEachClient(beacon_server.paused ? beaconServerProcessClientPaused : beaconServerProcessClient, &pcd);
	if(!beacon_server.paused)
	{
		int i;
		BeaconServerClientData **clients = NULL;
		beaconServerForEachClient(beaconServerGatherMapRequests, &clients);

		eaQSort(clients, mapDataCmp);

		for(i=0; i<40 && i<eaSize(&clients); i++)
		{
			BeaconServerClientData *client = clients[i];

			if(client->state==BCS_NEEDS_MORE_MAP_DATA)
			{
				beaconServerSendMapDataToWorker(client);
			}
		}
	}
}

static void beaconServerProcessCurrentMap(void){
	if(beaconServerHasMapLoaded()){
		// Process all the clients.

		beaconServerProcessClients();

		switch(beacon_server.state){
			xcase BSS_NOT_STARTED:{
				beaconServerDoStateNotStarted();
			}

			xcase BSS_GENERATING:{
				beaconServerDoStateGenerating();
			}

			xcase BSS_CONNECT_BEACONS:{
				beaconServerDoStateConnecting();
			}

			xcase BSS_WRITE_FILE:{
				beaconServerDoStateWriteFile();
			}

			xcase BSS_SEND_BEACON_FILE:{
				beaconServerForEachClient(beaconServerDisconnectClientCallback, "Server isn't processing.");

				beaconServerUpdateTitle("Sending beacon file: %s/%s bytes",
										getCommaSeparatedInt(beacon_server.beaconFile.sentByteCount),
										getCommaSeparatedInt(beacon_server.beaconFile.compressed.byteCount));
			}

			xcase BSS_DONE:{
				beaconServerSendMapCompleted();

				if(beacon_server.isRequestServer && beacon_server.request.projectName)
				{
					beaconServerSetCurMap(NULL);
					beaconServerResetMapData();
				}
				else if(estrLength(&beacon_server.request.uniqueStorageName)){
					beacon_server.beaconFile.sentByteCount = 0;

					beaconServerSendBeaconFileToMaster();
				}else{
					if(beacon_server.printTiming)
					{
						int i;
						Vec3 extents;
						BeaconMapMetaData *meta = beacon_process.mapMetaData;
						subVec3(meta->maxXYZ, meta->minXYZ, extents);

						printf("%s: %d,%d,%d,%.2f,%.2f,%.2f", beacon_server.curMapName, meta->beaconCount, meta->beaconGroundCount, meta->beaconRaisedCount, vecParamsXYZ(extents));

						for(i=0; i<BPP_COUNT; i++)
							printf(",%.2f", beacon_process.mapMetaData->beaconClientSeconds[i]);

						printf("\n");
					}

					beaconServerUpdateTitle(NULL);
					beaconServerSetCurMap(NULL);
					beaconServerResetMapData();
				}
			}

			xdefault:{
				beaconServerUpdateTitle("Unknown Mode: %d!", beacon_server.state);
			}
		}

		// This is a shortcut to allow rapid testing of post-write checkins
		if(beacon_server.skip_processing)
		{
			beacon_server.skip_processing = false;

			if(beacon_server.isRequestServer)
			{
				eaPush(&beacon_server.request.fileNames, strdup(beacon_process.beaconFileName));
				eaPush(&beacon_server.request.fileNames, strdup(beacon_process.beaconDateFileName));
				eaPush(&beacon_server.request.fileNames, strdup(beacon_process.beaconInvalidFileName));
			}

			beacon_server.state = BSS_DONE;
		}
	}else{
		beaconServerForEachClient(beaconServerPingClient, NULL);

		beaconServerUpdateTitle(beacon_server.isMasterServer ? NULL : "No Map Loaded");

		if(!beacon_server.isMasterServer){
			beaconServerForEachClient(beaconServerDisconnectClientCallback, "There is no map loaded.");
		}else{
			if(!beacon_server.localOnly){
				beaconServerForEachClient(beaconServerDisconnectErrorNonLocalIP, NULL);
			}
		}
	}
}

static struct {
	const char* name;
	S32			port;
} connectMasterServer;

void beaconServerConnectIdleCallback(F32 timeLeft){
	if(timeLeft){
		beaconServerUpdateTitle("Connecting to master server (%s:%d) %1.1f...",
								connectMasterServer.name,
								connectMasterServer.port,
								timeLeft);
	}
}

static void beaconServerFillStateDetails(BeaconServerStateDetails *details)
{
	if(beaconServerHasMapLoaded())
		details->curMap = beacon_server.curMapIndex-1;
	else
		details->curMap = -1;
}

static void beaconServerSendMasterConnect(void){
	Packet* pak = pktCreate(beacon_server.master_link, BMSG_C2S_SERVER_CONNECT);

	pktSendBitsPack(pak, 1, BEACON_SERVER_PROTOCOL_VERSION);

	pktSendBits(pak, 32, beacon_server.exeFile.crc);

	pktSendString(pak, getUserName());
	pktSendString(pak, getComputerName());

	pktSendBitsPack(pak, 1, beacon_server.port);

	//pktSendBitsPack(pak, 32, beacon_server.gimmeBranchNum);
	pktSendBitsPack(pak, 32, beacon_server.patcherTime);
	//pktSendString(pak, GetProductName());

	#if BEACON_SERVER_PROTOCOL_VERSION >= 1 && BEACON_SERVER_PROTOCOL_VERSION <= 2
		pktSendBits(pak, 1, beacon_server.isRequestServer ? 1 : 0);
	#elif BEACON_SERVER_PROTOCOL_VERSION >= 3
		pktSendU32(pak, beacon_server.type);
	#endif

	#if BEACON_SERVER_PROTOCOL_VERSION >= 3
		beacon_server.statusDirty = false;
		beacon_server.serverStatus.project = GetProductName();
		beacon_server.serverStatus.branch = beacon_server.gimmeBranchNum;
		beacon_server.serverStatus.gimmeTime = beacon_server.patcherTime;

		pktSendStruct(pak, &beacon_server.serverStatus, parse_BeaconServerStatus);
	#endif

	pktSend(&pak);
}

static void beaconServerSendServerStatus(void){
	if(!beacon_server.status.send || !beacon_client_conn.readyToWork){
		return;
	}

	beacon_server.status.send = 0;
	beacon_server.status.timeSent = beaconGetCurTime();

	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_server.master_link, BMSG_C2ST_SERVER_STATUS);

		pktSendString(pak, beacon_server.curMapName ? beacon_server.curMapName : "");
		pktSendBitsPack(pak, 1, listenCount(beacon_server.clients));
		pktSendBitsPack(pak, 1, beacon_server.state);
		pktSendBitsPack(pak, 1, beacon_server.status.sendUID);
		pktSendBits(pak, 1, beacon_server.sendClientsToMe);

#if BEACON_SERVER_PROTOCOL_VERSION >= 3
		pktSendBits(pak, 1, beacon_server.statusDirty);
		if(beacon_server.statusDirty)
			pktSendStruct(pak, &beacon_server.serverStatus, parse_BeaconServerStatus);
		beacon_server.statusDirty = 0;
#endif

	BEACON_CLIENT_PACKET_SEND();
}

static int processBeaconMasterServerMsgClientCapHelper(NetLink *link, int index, void* link_data, void* func_data)
{
	S32 maxAllowed = *((S32*)func_data);
	if(index>=maxAllowed)
	{
		BeaconServerClientData* client = link_data;
		char buffer[1000];

		sprintf(buffer, "I have %d clients and the cap is %d.", listenCount(beacon_server.clients), maxAllowed);

		beaconServerDisconnectClient(client, buffer);
	}

	return 1;
}

static void processBeaconMasterServerMsgClientCap(Packet* pak){
	S32 maxAllowed = pktGetBitsPack(pak, 1);
	S32 i;

	{
		char* timeString = pktGetStringTemp(pak);
	}

	for(i=0; i<listenCount(beacon_server.clients)-maxAllowed; i++)
	{

	}

	linkIterate2(beacon_server.clients, processBeaconMasterServerMsgClientCapHelper, &maxAllowed);
}

static void processBeaconMasterServerMsgStatusAck(Packet* pak){
	U32 ackedStatusUID = pktGetBitsPack(pak, 1);

	if(ackedStatusUID == beacon_server.status.sendUID){
		beacon_server.status.acked = 1;
	}
}

static void beaconServerWriteCurMapData(void){
	#if 0
	static S32 count;

	char fileName[MAX_PATH];
	Vec3 pyr;
	FILE* f;
	S32 i;
	S32 j;
	S32 k;

	strcpy(fileName, "c:\\beaconizer\\requestmaps");

	makeDirectories(fileName);

	sprintf(fileName, "c:\\beaconizer\\requestmaps\\%s.%d.txt", beacon_server.serverUID, count++);

	f = fopen(fileName, "wt");

	if(!f){
		return;
	}

	for(i = 0; i < sergroup_info.file_count; i++){
		GroupFile* file = group_info.files[i];

		fprintf(f, "Group File: %s\n", file->fullname);

		for(j = 0; j < file->count; j++){
			GroupDef* def = file->defs[j];

			fprintf(f, "Def: %s\n", def ? def->name : "NONE");

			if(!def){
				continue;
			}

			fprintf(f, "  NoBeaconGroundConnections: %d\n", def->no_beacon_ground_connections);

			if(def->model){
				fprintf(f, "  model->ctriflags_setonall:  0x%8.8x\n", def->model->ctriflags_setonall);
				fprintf(f, "  model->ctriflags_setonsome: 0x%8.8x\n", def->model->ctriflags_setonsome);
			}

			for(k = 0; k < def->count; k++){
				GroupChild* child = def->entries + k;

				getMat3YPR(child->mat, pyr);

				fprintf(f, "    Child: %s\n", child->def->name);
				fprintf(f, "      pos: %f %f %f\n", posParamsXYZ(child));
				fprintf(f, "      pyr: %f %f %f\n", vecParamsXYZ(pyr));
				fprintf(f, "\n");
			}
		}
	}

	for(i = 0; i < group_info.ref_count; i++){
		DefTracker* ref = group_info.refs[i];

		fprintf(f, "Ref: %s", ref->def ? ref->def->name : "NONE");

		if(!ref->def){
			continue;
		}

		getMat3YPR(ref->mat, pyr);

		fprintf(f, "  pos: %f %f %f\n", posParamsXYZ(ref));
		fprintf(f, "  pyr: %f %f %f\n", vecParamsXYZ(pyr));
		fprintf(f, "\n");
	}

	fprintf(f, "Starting beacons:\n");

	for(i = 0; i < combatBeaconArray.size; i++){
		Beacon* b = combatBeaconArray.storage[i];

		fprintf(f, "Beacon: %f %f %f\n", posParamsXYZ(b));
	}

	fprintf(f, "\nScene Info:\n");
	fprintf(f, "MaxHeight: %f\n", scene_info.maxHeight);

	fprintf(f, "\nEnd of map dump\n\n");

	fclose(f);
	#endif
}

static void beaconServerSendNeedMoreMapData(void){
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_server.master_link, BMSG_C2ST_NEED_MORE_MAP_DATA);
	BEACON_CLIENT_PACKET_SEND();
}

static void beaconServerSendMapDataIsLoaded(void){
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_server.master_link, BMSG_C2ST_MAP_DATA_IS_LOADED);

		pktSendBitsPack(pak, 1, beacon_server.request.nodeUID);
		pktSendString(pak, beacon_server.request.uniqueStorageName);

	BEACON_CLIENT_PACKET_SEND();
}

static void processBeaconMasterServerMsgMapData(Packet* pak){
	char uniqueStorageName[1000];

	beacon_server.request.nodeUID = pktGetBitsPack(pak, 1);
	Strncpyt(uniqueStorageName, pktGetStringTemp(pak));

	estrCopy2(&beacon_server.request.uniqueStorageName, uniqueStorageName);

	if(pktGetBits(pak, 1)){
		beaconServerResetMapData();
		beaconServerSetCurMap(NULL);
		beaconServerSetState(BSS_NOT_STARTED);
		beacon_server.sendClientsToMe = 0;
		beaconServerSetSendStatus();
	}

	if(!beacon_server.mapData){
		beaconMapDataPacketCreate(&beacon_server.mapData);
	}

	beaconMapDataPacketReceiveChunk(pak, beacon_server.mapData);

	//if(newMap){
	//	beaconPrintf(	COLOR_GREEN,
	//					"Receiving new map (%s:%d): %s bytes.\n",
	//					beacon_server.request.uniqueStorageName,
	//					beacon_server.request.uid,
	//					getCommaSeparatedInt(beaconMapDataPacketGetSize(beacon_server.mapData)));
	//}

	//if(0){
	//	beaconPrintf(	COLOR_GREEN,
	//					"Received data for map (%s): %s/%s bytes.\n",
	//					beacon_server.request.uniqueStorageName,
	//					getCommaSeparatedInt(beaconMapDataPacketGetReceivedSize(beacon_server.mapData)),
	//					getCommaSeparatedInt(beaconMapDataPacketGetSize(beacon_server.mapData)));
	//}

	if(!beaconMapDataPacketIsFullyReceived(beacon_server.mapData)){
		beaconServerSendNeedMoreMapData();
	}else{
		beaconPrintf(	COLOR_GREEN,
						"Received map: %s.\n",
						beacon_server.request.uniqueStorageName);

		beaconServerSendMapDataIsLoaded();
	}
}

static void processBeaconMasterServerMsgNeedMoreBeaconFile(Packet* pak){
	if(beacon_server.state == BSS_SEND_BEACON_FILE){
		beaconServerSendBeaconFileToMaster();
	}
}

static void beaconServerSendRequestedMapLoadFailed(void){
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_server.master_link, BMSG_C2ST_REQUESTED_MAP_LOAD_FAILED);

		pktSendBitsPack(pak, 1, beacon_server.request.nodeUID);
		pktSendString(pak, beacon_server.request.uniqueStorageName);

	BEACON_CLIENT_PACKET_SEND();
}

static void beaconServerSendMapCompleted(void)
{
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_server.master_link, BMSG_C2ST_MAP_COMPLETED);
	BEACON_CLIENT_PACKET_SEND();
}

static void processBeaconMasterServerMsgProcessRequestedMap(Packet* pak){
	//beaconPrintf(COLOR_GREEN, "Loading map!\n");

	assert(0);		// Not supported

	return;

	if(beaconMapDataPacketToMapData(beacon_server.mapData, NULL)){
		int iPartitionIdx = worldGetAnyCollPartitionIdx();

		beaconPrintf(COLOR_GREEN, "Map loaded!\n");

		estrCopy(&beacon_server.curMapName, &beacon_server.request.uniqueStorageName);

		beaconServerSetState(BSS_GENERATING);

		beaconInitGenerating(worldGetActiveColl(iPartitionIdx), 1);

		beacon_server.sendClientsToMe = 1;
		beaconServerSetSendStatus();

		beaconCurTimeString(1);

		beaconMapDataPacketInitialBeaconsToRealBeacons();

		beaconInsertLegalBeacons();

		if(beacon_server.writeCurMapData){
			beaconServerWriteCurMapData();
		}
	}
	else if(linkConnected(beacon_server.master_link)){
		beaconServerSendRequestedMapLoadFailed();
	}
}

static void processBeaconMasterServerPing(Packet *pak)
{
	beacon_server.recvPingTime = beaconGetCurTime(); //vl
}

static void processBeaconMasterServerMsgTextCmd(const char* textCmd, Packet* pak){
	#define BEGIN_HANDLERS()	if(0){
	#define HANDLER(x, y)		}else if(!stricmp(textCmd, x)){y(pak)
	#define END_HANDLERS()		}

	BEGIN_HANDLERS();
		HANDLER(BMSG_S2CT_CLIENT_CAP,				processBeaconMasterServerMsgClientCap			);
		HANDLER(BMSG_S2CT_STATUS_ACK,				processBeaconMasterServerMsgStatusAck			);
		HANDLER(BMSG_S2CT_MAP_DATA,					processBeaconMasterServerMsgMapData				);
		HANDLER(BMSG_S2CT_NEED_MORE_BEACON_FILE,	processBeaconMasterServerMsgNeedMoreBeaconFile	);
		HANDLER(BMSG_S2CT_PROCESS_REQUESTED_MAP,	processBeaconMasterServerMsgProcessRequestedMap	);
		HANDLER(BMSG_S2CT_PING,						processBeaconMasterServerPing					);
	END_HANDLERS();

	#undef BEGIN_HANDLERS
	#undef HANDLER
	#undef END_HANDLERS
}

static void beaconServerHandleMasterMsg(Packet* pak, S32 cmd, NetLink* link, void *user_data){
	beacon_client_conn.timeHeardFromServer = timerCpuTicks();

	switch(cmd){
		xcase BMSG_S2C_CONNECT_REPLY:{
			if(pktGetBits(pak, 1) == 1){
				beacon_client_conn.readyToWork = 1;
			}else{
				// Get a new executable.
				beaconPrintf(COLOR_RED, "Bad CRC.\n");
				beaconServerGetNewExe();
			}
		}

		xcase BMSG_S2C_TEXT_CMD:{
			char textCmd[100];

			strncpyt(textCmd, pktGetStringTemp(pak), ARRAY_SIZE(textCmd) - 1);

			processBeaconMasterServerMsgTextCmd(textCmd, pak);
		}

		xdefault:{
			beaconPrintf(COLOR_RED, "ERROR: Bad cmd received from server: %d!\n", cmd);
		}
	}
}

static void beaconServerPingMaster(void)
{
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_server.master_link, BMSG_C2ST_PING);
		beaconServerFillStateDetails(&beacon_server.stateDetails);
		pktSendStruct(pak, &beacon_server.stateDetails, parse_BeaconServerStateDetails);
	BEACON_CLIENT_PACKET_SEND();
}

static void beaconServerMonitorMasterConnection(void){
	if(beacon_server.isMasterServer){
		return;
	}

	if(!linkConnected(beacon_server.master_link) || linkDisconnected(beacon_server.master_link))
	{
		linkRemove(&beacon_server.master_link);
	}

	if(!linkConnected(beacon_server.master_link)){
		beacon_client_conn.readyToWork = 0;
		beacon_server.status.sendUID = 0;
		beacon_server.status.ackedUID = 0;
		beacon_server.status.acked = 0;
		beacon_server.sendClientsToMe = 1;

		connectMasterServer.name = beacon_common.masterServerName ? beacon_common.masterServerName : "localhost";
		connectMasterServer.port = BEACON_MASTER_SERVER_PORT;

		beacon_server.master_link = commConnectIP(beacon_comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,ipLocalFromString(connectMasterServer.name),connectMasterServer.port,beaconServerHandleMasterMsg,0,0,0);
		if (linkConnectWait(&beacon_server.master_link, beaconIsProductionMode() ? 5 : 30))
		{
			beaconServerSetSendStatus();

			beaconServerSendMasterConnect();

			beacon_server.recvPingTime = beaconGetCurTime();

			beacon_common.connectedToMasterOnce = true;
		}
	}

	if(linkConnected(beacon_server.master_link)){
		if(beaconTimeSince(beacon_server.sentPingTime) > 5){
			beacon_server.sentPingTime = beaconGetCurTime();

			beaconServerPingMaster();
		}

		commMonitor(beacon_comm);
		beaconServerSendServerStatus();
	}
}

static void beaconServerTransferClient(BeaconServerClientData* transferClient, BeaconServerClientData* subServer){
	transferClient->transferred = 1;

	beaconClientPrintf(	transferClient,
						COLOR_RED,
						"Transferring %s/%s to server %s/%s.\n",
						transferClient->computerName,
						transferClient->userName,
						subServer->computerName,
						subServer->userName);

	BEACON_SERVER_PACKET_CREATE_BASE(pak, transferClient, BMSG_S2CT_TRANSFER_TO_SERVER);

		pktSendString(pak, subServer->computerName);
		pktSendString(pak, getClientIPStr(subServer));
		pktSendBitsPack(pak, 1, subServer->server.port);

	BEACON_SERVER_PACKET_SEND();

	transferClient->client.assignedTo = subServer;
	eaPush(&subServer->server.clients, transferClient);
}

struct {
	S32 neededCount;
	S32 clientCount;
	S32 maxAllowed;
	S32 minAllowed;
} clientStats;

static void beaconMasterServerProcessClient(BeaconServerClientData* client,
											S32 index,
											BeaconServerProcessClientsData* pcd)
{
	NetLink* link = client->link;
	BeaconServerMachineData *machine = NULL;

	switch(client->state){
		xcase BCS_NEEDS_MORE_EXE_DATA:{
			if(pcd->sentExeDataCount < 5){
				pcd->sentExeDataCount++;

				beaconServerSendNextExeChunk(client);
			}else{
				beacon_server.noNetWait = 1;
			}
		}

		xcase BCS_RECEIVING_EXE_DATA:{
			if(beaconTimeSince(client->exeData.lastCommTime) < 3){
				pcd->sentExeDataCount++;
			}
		}
	}

	switch(client->clientType){
		xcase BCT_SERVER:{
			BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_PING);		// Need to ping it so it knows when we die.
			BEACON_SERVER_PACKET_SEND();
			if(	client->server.isRequestServer &&
				beaconTimeSince(client->receivedPingTime) >= 60)
			{
				beaconServerDisconnectClient(client, "Server took more than 60 seconds to respond.");
				break;
			}

			// Send me some clients.

			if(beaconServerNeedsSomeClients(client)){
				clientStats.neededCount++;
			}

			// Find a new requester for this request server.

			if(beaconServerRequestServerIsAvailable(client)){
				BeaconProcessQueueNode* node;

				if(	beaconServerGetProcessNodeForProcessing(&node) &&
					beaconMapDataPacketIsFullyReceived(node->mapData))
				{
					//beaconClientPrintf(	client,
					//					COLOR_GREEN,
					//					"Assigning to request server %s (%s/%s).\n",
					//					getClientIPStr(requestServer),
					//					requestServer->computerName,
					//					requestServer->userName);

					beaconServerAssignProcessNodeToRequestServer(node, client);
				}
			}
		}

		xcase BCT_SENTRY:{
			
		}

		xcase BCT_WORKER:{
			machine = beaconServerFindMachine(client->link, false);

			if(	machine->sentry &&
				machine->sentry->forcedInactive)
			{
				clientStats.clientCount++;
				if(	client->state == BCS_READY_TO_WORK &&
					!client->client.assignedTo)
				{
					eaPush(&availableClients, client);
				}
			}
		}

		xcase BCT_REQUESTER:{
			if(beaconTimeSince(client->receivedPingTime) >= 60){
				beaconServerDisconnectClient(client, "Requester took more than 60 seconds to respond.");
				break;
			}
		}
	}
}

static void beaconServerAssignClientsToNeedyServer(BeaconServerClientData* client, S32 index, void* userData){
	S32 sentCount = 0;

	if(	client->clientType != BCT_SERVER ||	!beaconServerNeedsSomeClients(client))
	{
		return;
	}

	if(clientStats.neededCount >= 2){
		BEACON_SERVER_PACKET_CREATE(BMSG_S2CT_CLIENT_CAP);

			char buffer[100];
			pktSendBitsPack(pak, 1, clientStats.maxAllowed);
			sprintf(buffer, "time: %"FORM_LL"d", time(0));
			pktSendString(pak, buffer);

		BEACON_SERVER_PACKET_SEND();
	}

	while(	eaSize(&availableClients) &&
			(clientStats.neededCount < 2 || client->server.clientCount + sentCount < clientStats.minAllowed))
	{
		BeaconServerClientData* transferClient = availableClients[eaSize(&availableClients) - 1];

		if(transferClient->clientType == BCT_SENTRY)
		{
			assert(0);
		}
		else if(transferClient->clientType == BCT_WORKER)
		{
			sentCount++;

			beaconServerTransferClient(transferClient, client);

			eaSetSize(&availableClients, eaSize(&availableClients) - 1);
		}
		else
		{
			assert(0);
		}
	}
}

static void beaconServerAssignRemainingClientsToNeedyServer(BeaconServerClientData* client, S32 index, void* userData){
	if(	client->clientType == BCT_SERVER &&
		estrLength(&client->server.mapName) &&
		eaSize(&availableClients) &&
		client->server.clientCount < clientStats.maxAllowed)
	{
		BeaconServerClientData* transferClient = availableClients[eaSize(&availableClients) - 1];

		beaconServerTransferClient(transferClient, client);

		eaSetSize(&availableClients, eaSize(&availableClients) - 1);
	}
}

static U32 beaconServerHasTimePassed(U32* previousTime, U32 seconds){
	U32 curTime = beaconGetCurTime();

	if((U32)(curTime - *previousTime) < seconds){
		return 0;
	}

	*previousTime = curTime;

	return 1;
}

static void beaconMasterServerProcessClients(void){
	BeaconServerProcessClientsData pcd = {0};

	beaconServerForEachClient(beaconMasterServerProcessClient, &pcd);
}

static void beaconServerDoMasterServerStuff(void){
	static U32 lastClientCapCheck;

	beaconServerMonitorMasterConnection();

	eaSetSize(&availableClients, 0);

	if(!beacon_server.isMasterServer){
		return;
	}

	if(	!beacon_server.noNetWait &&
		!beaconServerHasTimePassed(&lastClientCapCheck, 1))
	{
		return;
	}

	ZeroStruct(&clientStats);
	
	beaconMasterServerProcessClients();

	beaconServerUpdateProcessNodes();

	if(!clientStats.neededCount){
		return;
	}

	clientStats.minAllowed = clientStats.clientCount / clientStats.neededCount;
	clientStats.maxAllowed = clientStats.minAllowed + ((clientStats.clientCount % clientStats.neededCount) ? 1 : 0);
	MAX1(clientStats.maxAllowed, 5);

	// Send the client cap and/or client transfers.

	beaconServerForEachClient(beaconServerAssignClientsToNeedyServer, NULL);

	// Transfer remaining available clients to whatever server needs some.

	if(eaSize(&availableClients)){
		beaconServerForEachClient(beaconServerAssignRemainingClientsToNeedyServer, NULL);
	}
}

static void beaconServerMonitorNetworkOrSleep(void){
	// Check on the dbserver link.

	if(beaconIsSharded()){
		commMonitor(beacon_comm);

		if(beacon_server.isMasterServer){
			static S32 lastTimeSent = 0;

			if(!lastTimeSent){
				lastTimeSent = beaconGetCurTime();
			}
		}
	}

	if(beacon_server.clients)
	{
		S32 noNetWait = beacon_server.noNetWait;

		beacon_server.noNetWait = 0;

		commMonitor(beacon_comm);
	}else{
		Sleep(100);
	}
}

static void beaconServerTestRequestClient(void){
	if(beacon_server.testRequestClient){
		beacon_server.testRequestClient = 0;

		if(beaconServerHasMapLoaded()){
			beaconRequestBeaconizing("flarp");
		}
	}

	beaconRequestUpdate();
}

void beaconServerKeepControllerInformed(void)
{
	UpdateControllerConnection();

	if(beaconIsSharded())
	{
		DirectlyInformControllerOfState("bcnRunning");
	}
}
 
void beaconServerOncePerFrame(void)
{
	beaconServerKeepControllerInformed();

	if(!beacon_server.isMasterServer)
		FolderCacheDoCallbacks();
	beaconServerMonitorNetworkOrSleep();
	beaconServerDoMasterServerStuff();

	if(!beacon_server.isMasterServer && !beaconCommonCheckMasterName()) 
		return;

	beaconServerCheckInput();
	beaconServerCheckForNewMap();
	beaconServerProcessCurrentMap();
	beaconServerTestRequestClient();
	beaconServerUpdateDisplay();

	if(g_debugger_link)
	{
		linkFlush(g_debugger_link);
	}

	Sleep(100);
}

// Beaconizer Live Request Stuff -------------------------------------------------------------------------------

typedef enum BeaconizerRequestState {
	BRS_INACTIVE,
	BRS_REQUEST_NOT_SENT,
	BRS_REQUEST_SENDING,
	BRS_REQUEST_ACCEPTED,
} BeaconizerRequestState;

struct {
	BeaconizerRequestState				state;
	NetLink								*link;
	char*								masterServerAddress;
	U32									sentPingTime;

	U32									wasConnected;

	char*								uniqueStorageName;

	BeaconMapDataPacket*				mapData;

	U32									sentByteCount;
	U32									uid;

	char*								createNewRequest;

	U32									lastCreateRequestTime;

	struct {
		struct {
			U8*							data;
			U32							byteCount;
		} compressed, uncompressed;

		U32								receivedByteCount;
		U32								readCursor;
	} beaconFile;
} beacon_request;

static void beaconRequestSendMasterConnect(void){
	#if 0
	Packet* pak = pktCreate(&beacon_request.link, BMSG_C2S_REQUESTER_CONNECT);

	pktSendBitsPack(pak, 1, BEACON_SERVER_PROTOCOL_VERSION);

	pktSendString(pak, getUserName());
	pktSendString(pak, getComputerName());
	pktSendString(pak, beaconGetLinkIPStr(&db_comm_link));

	pktSend(&pak, &beacon_request.link);
	#endif
}

static S32 beaconRequestConnectToServer(void){
	static void beaconRequestHandleMsg(Packet* pak, S32 cmd, NetLink* link,void *user_data);
	if(!linkConnected(beacon_request.link)){
		static F32 curConnectTryTime = 0.05;
		static int failureCount = 0;

		const char* serverAddress = NULL;

		if(beacon_request.wasConnected){
			beacon_request.wasConnected = 0;

			// Force a re-send of the request if the connection was broken.

			switch(beacon_request.state){
				xcase BRS_INACTIVE:
				case BRS_REQUEST_NOT_SENT:
					// Ignored.
				xdefault:
					beacon_request.state = BRS_REQUEST_NOT_SENT;
					beacon_request.uid++;
			}
		}

		if(estrLength(&beacon_request.masterServerAddress)){
			serverAddress = beacon_request.masterServerAddress;
		}
		else if(isDevelopmentMode()){
			serverAddress = BEACON_DEFAULT_SERVER;
		}

		if(serverAddress && failureCount < 5){
			beacon_request.link = commConnectIP(beacon_comm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,ipLocalFromString(serverAddress),BEACON_MASTER_SERVER_PORT,beaconRequestHandleMsg,0,0,0);
			if (linkConnectWait(&beacon_request.link, curConnectTryTime))
			{
				curConnectTryTime = 0.05;

				beacon_request.wasConnected = 1;

				beaconRequestSendMasterConnect();
			}else{
				if (isDevelopmentMode())
				{
					failureCount++;
				}

				curConnectTryTime += 0.05;

				if(curConnectTryTime > 0.25){
					curConnectTryTime = 0.05;
				}
			}
		}
	}

	return linkConnected(beacon_request.link);
}

void beaconRequestBeaconizing(const char* uniqueStorageName){
	estrCopy2(&beacon_request.createNewRequest, uniqueStorageName);
	beacon_request.lastCreateRequestTime = beaconGetCurTime();
}

static void beaconRequestSendNextRequestChunk(void){
	if(!linkConnected(beacon_request.link)){
		return;
	}

	if(beacon_request.state == BRS_REQUEST_NOT_SENT){
		beacon_request.sentByteCount = 0;
		beacon_request.state = BRS_REQUEST_SENDING;
	}

	if(beaconMapDataPacketIsFullySent(beacon_request.mapData, beacon_request.sentByteCount)){
		beaconPrintf(COLOR_RED, "ERROR: Already sent the whole BeaconMapDataPacket.\n");
		return;
	}

	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_request.link, BMSG_C2ST_REQUESTER_MAP_DATA);

		pktSendBitsPack(pak, 1, beacon_request.uid);
		pktSendString(pak, beacon_request.uniqueStorageName);

		beaconMapDataPacketSendChunk(	pak,
										beacon_request.mapData,
										&beacon_request.sentByteCount);

		if(0){
			beaconPrintf(	COLOR_GREEN,
							"Sent beacon request: %s/%s bytes.\n",
							getCommaSeparatedInt(beacon_request.sentByteCount),
							getCommaSeparatedInt(beaconMapDataPacketGetSize(beacon_request.mapData)));
		}

	BEACON_CLIENT_PACKET_SEND();

	if(beaconMapDataPacketIsFullySent(	beacon_request.mapData,
										beacon_request.sentByteCount))
	{
		beaconPrintf(	COLOR_GREEN,
						"Sent beacon request: %s bytes.\n",
						getCommaSeparatedInt(beacon_request.sentByteCount));
	}
}

static void beaconRequestProcessMsgRequestChunkReceived(Packet* pak){
	beaconRequestSendNextRequestChunk();
}

static void beaconRequestProcessMsgRequestAccepted(Packet* pak){
	U32 uid = pktGetBitsPack(pak, 1);

	if(uid == beacon_request.uid){
		beaconPrintf(	COLOR_GREEN,
						"Beacon request accepted: %s\n",
						beacon_request.uniqueStorageName);

		beacon_request.state = BRS_REQUEST_ACCEPTED;
	}
}

static S32 beaconReaderCallbackReadRequestFile(void* data, U32 size){
	if(size > beacon_request.beaconFile.uncompressed.byteCount - beacon_request.beaconFile.readCursor){
		return 0;
	}

	memcpy(	data,
			beacon_request.beaconFile.uncompressed.data + beacon_request.beaconFile.readCursor,
			size);

	beacon_request.beaconFile.readCursor += size;

	return 1;
}

static void beaconRequestUncompressBeaconFile(void){
	//void aiClearBeaconReferences();

	S32 ret;
	U32 uncompressedByteCount = beacon_request.beaconFile.uncompressed.byteCount;

	SAFE_FREE(beacon_request.beaconFile.uncompressed.data);
	beacon_request.beaconFile.uncompressed.data = malloc(beacon_request.beaconFile.uncompressed.byteCount);

	ret = uncompress(	beacon_request.beaconFile.uncompressed.data,
						&uncompressedByteCount,
						beacon_request.beaconFile.compressed.data,
						beacon_request.beaconFile.compressed.byteCount);

	assert(ret == Z_OK);

	assert(uncompressedByteCount == beacon_request.beaconFile.uncompressed.byteCount);

	SAFE_FREE(beacon_request.beaconFile.compressed.data);
	ZeroStruct(&beacon_request.beaconFile.compressed);

	beacon_request.beaconFile.readCursor = 0;

	beaconPrintf(	COLOR_GREEN,
					"Reading beacon file: %s bytes\n",
					getCommaSeparatedInt(beacon_request.beaconFile.uncompressed.byteCount));

	//aiClearBeaconReferences();

	readBeaconFileCallback(beaconReaderCallbackReadRequestFile);

	beaconRebuildBlocks(0, 1, 0);

	SAFE_FREE(beacon_request.beaconFile.uncompressed.data);
	ZeroStruct(&beacon_request.beaconFile.uncompressed);
}

static void beaconRequestSendNeedMoreBeaconFile(void){
	BEACON_CLIENT_PACKET_CREATE_TO_LINK(beacon_request.link, BMSG_C2ST_NEED_MORE_BEACON_FILE);

		pktSendBitsPack(pak, 1, beacon_request.beaconFile.receivedByteCount);

	BEACON_CLIENT_PACKET_SEND();
}

static void beaconRequestProcessMsgBeaconFile(Packet* pak){
	if(beacon_request.state == BRS_REQUEST_ACCEPTED){
		U32 uid;
		char uniqueStorageName[1000];
		U32 bytesToRead;

		if(!estrLength(&beacon_request.uniqueStorageName)){
			beaconPrintf(COLOR_YELLOW, "WARNING: Receiving beacon file when no request is active.\n");
			return;
		}

		uid = pktGetBitsPack(pak, 1);

		if(uid != beacon_request.uid){
			beaconPrintf(	COLOR_YELLOW,
							"WARNING: Receiving old beacon file (old:%d, new:%d)\n",
							uid,
							beacon_request.uid);
			return;
		}

		Strncpyt(uniqueStorageName, pktGetStringTemp(pak));

		if(stricmp(uniqueStorageName, beacon_request.uniqueStorageName)){
			beaconPrintf(	COLOR_YELLOW,
							"WARNING: Receiving beacon file for other request (other:%s, me:%s)\n",
							uniqueStorageName,
							beacon_request.uniqueStorageName);
			return;
		}

		beacon_request.beaconFile.compressed.byteCount = pktGetBitsPack(pak, 1);
		beacon_request.beaconFile.receivedByteCount = pktGetBitsPack(pak, 1);
		bytesToRead = pktGetBitsPack(pak, 1);
		beacon_request.beaconFile.uncompressed.byteCount = pktGetBitsPack(pak, 1);

		if(!beacon_request.beaconFile.receivedByteCount){
			SAFE_FREE(beacon_request.beaconFile.compressed.data);

			beacon_request.beaconFile.compressed.data = malloc(beacon_request.beaconFile.compressed.byteCount);
		}

		pktGetBytes(pak,
					bytesToRead,
					beacon_request.beaconFile.compressed.data + beacon_request.beaconFile.receivedByteCount);

		beacon_request.beaconFile.receivedByteCount += bytesToRead;

		if(beacon_request.beaconFile.receivedByteCount == beacon_request.beaconFile.compressed.byteCount){
			beacon_request.state = BRS_INACTIVE;

			beaconRequestUncompressBeaconFile();

			SAFE_FREE(beacon_request.beaconFile.compressed.data);
			ZeroStruct(&beacon_request.beaconFile.compressed);
		}else{
			beaconRequestSendNeedMoreBeaconFile();
		}
	}
}

static void beaconRequestProcessMsgRegenerateMapData(Packet* pak){
	if(estrLength(&beacon_request.uniqueStorageName)){
		beaconPrintf(	COLOR_YELLOW,
						"WARNING: Master-BeaconServer says my map data packet is bad.  Regenerating.\n");

		beaconRequestBeaconizing(beacon_request.uniqueStorageName);
	}
}

static void beaconRequestProcessMsgTextCmd(const char* textCmd, Packet* pak){
	#define BEGIN_HANDLERS	if(0){
	#define HANDLER(x, y)	}else if(!stricmp(textCmd, x)){y(pak)
	#define END_HANDLERS	}else{beaconPrintf(COLOR_RED, "Unknown text cmd: %s!\n", textCmd);}

	BEGIN_HANDLERS
		HANDLER(BMSG_S2CT_REQUEST_CHUNK_RECEIVED,	beaconRequestProcessMsgRequestChunkReceived	);
		HANDLER(BMSG_S2CT_REQUEST_ACCEPTED,			beaconRequestProcessMsgRequestAccepted		);
		HANDLER(BMSG_S2CT_BEACON_FILE,				beaconRequestProcessMsgBeaconFile			);
		HANDLER(BMSG_S2CT_REGENERATE_MAP_DATA,		beaconRequestProcessMsgRegenerateMapData	);
	END_HANDLERS

	#undef BEGIN_HANDLERS
	#undef HANDLER
	#undef END_HANDLERS
}

static void beaconRequestHandleMsg(Packet* pak, S32 cmd, NetLink* link,void *user_data){
	switch(cmd){
		xcase BMSG_S2C_TEXT_CMD:{
			char textCmd[100];

			Strncpyt(textCmd, pktGetStringTemp(pak));

			beaconRequestProcessMsgTextCmd(textCmd, pak);
		}

		xdefault:{
			beaconPrintf(COLOR_YELLOW, "Unknown cmd from server: %d.\n", cmd);
		}
	}
}

static void beaconRequestCreateRequest(void){
	BeaconMapDataPacket* newMapData = NULL;
	Vec3 safeEntrancePos;

	if(	!beacon_request.createNewRequest ||
		(U32)(beaconGetCurTime() - beacon_request.lastCreateRequestTime) < 5)
	{
		return;
	}

	beacon_request.lastCreateRequestTime = beaconGetCurTime();

	PERFINFO_AUTO_START("createRequest", 1);

	beaconMapDataPacketClearInitialBeacons();

	if(0){//getSafeEntrancePos(NULL, safeEntrancePos, 0, NULL)){
		beaconMapDataPacketAddInitialBeacon(safeEntrancePos, 1);
	}

	beacon_server.mapDataCRC = beaconCalculateGeoCRC(worldGetActiveColl(worldGetAnyCollPartitionIdx()), false);
	beaconMapDataPacketFromMapData(worldGetActiveColl(worldGetAnyCollPartitionIdx()), &newMapData, beacon_server.fullCRCInfo);

	if(	beacon_request.uniqueStorageName &&
		!stricmp(beacon_request.uniqueStorageName, beacon_request.createNewRequest) &&
		beaconMapDataPacketIsSame(newMapData, beacon_request.mapData))
	{
		// Destroy the new packet.

		beaconMapDataPacketDestroy(&newMapData);

		// The same request is currently in some state of pending.

		estrDestroy(&beacon_request.createNewRequest);

		//beaconPrintf(COLOR_YELLOW, "Duplicate beacon request made while previous request is still pending.\n");

		PERFINFO_AUTO_STOP();

		return;
	}

	beaconMapDataPacketDestroy(&beacon_request.mapData);
	beacon_request.mapData = newMapData;
	estrCopy(&beacon_request.uniqueStorageName, &beacon_request.createNewRequest);
	estrDestroy(&beacon_request.createNewRequest);
	beacon_request.uid++;

	beacon_request.state = BRS_REQUEST_NOT_SENT;

	PERFINFO_AUTO_STOP();
}

void beaconRequestUpdate(void){
	PERFINFO_AUTO_START("beaconRequestUpdate", 1);

	beaconRequestCreateRequest();

	if(beacon_request.state == BRS_INACTIVE){
		if(linkConnected(beacon_request.link)){
			linkRemove(&beacon_request.link);
			commMonitor(beacon_comm);
		}
		PERFINFO_AUTO_STOP();
		return;
	}

	if(!beaconRequestConnectToServer()){
		PERFINFO_AUTO_STOP();
		return;
	}

	if(beaconTimeSince(beacon_request.sentPingTime)){
		beacon_request.sentPingTime = beaconGetCurTime();

		beaconServerPingMaster();
	}

	if(beacon_request.state == BRS_REQUEST_NOT_SENT){
		beaconRequestSendNextRequestChunk();
	}

	PERFINFO_AUTO_STOP();
}

void beaconRequestSetMasterServerAddress(const char* address){
	if(linkConnected(beacon_request.link)){
		linkRemove(&beacon_request.link);
	}

	if(!address){
		estrDestroy(&beacon_request.masterServerAddress);
	}else{
		estrCopy2(&beacon_request.masterServerAddress, address);
	}
}

static int g_patching = 0;
static void beaconServerCheckinFinished(int rev, U32 timestamp, PCL_ErrorCode error, const char * error_details, void * userData)
{
	printf("Patch finished %s.\n", !error ? "successfully" : "with error %d");

	if(!error)
	{
		beacon_server.patcherTime = timestamp;

		g_patching = 0;
	}
}

static bool beaconServerProcessStatus(S64 sent, S64 total, F32 elapsed, PCL_ErrorCode error, const char * error_details, void * userData)
{
	static last_elapsed = 0;
	float fProgress = (total > 0) ? ((float)sent/total)	: -1; 

	if(!last_elapsed) last_elapsed = elapsed;
	if(elapsed - last_elapsed >= 5)
	{
		last_elapsed = elapsed;

		printf("Progress: %.3f | Transferred: %.3f MB | Total: %.3f MB | Rate: %.3f MB/s\n",
				fProgress, sent / (1024.0 * 1024), total / (1024.0*1024), 
				elapsed ? (sent / (elapsed * 1024.0 * 1024)) : 0);
	}

	return false;
}

void beaconServerUploadPatch(void)
{
	
}

void beaconServerPCLWait(PCL_Client *client)
{
	// Set view to current project and branch number and time 0
	commMonitor(beacon_comm);
	while(beacon_common.pfunc(beacon_server.pcl_client)==PCL_WAITING)
	{
		commMonitor(beacon_comm);
	}
}

static const char* beaconServerGetClientExePath(void)
{
	return "c:/BeaconClient/gameserver.exe";
}

static const char* beaconServerGetClientPdbPath(void)
{
	return "c:/BeaconClient/gameserver.pdb";
}

void beaconServerPrepareExes(void)
{
	char beaconexe[MAX_PATH];
	char beaconpdb[MAX_PATH];

	sprintf(beaconexe, "%sbeaconizer.exe", beaconGetExeDirectory());
	sprintf(beaconpdb, "%sbeaconizer.pdb", beaconGetExeDirectory());

	if(!fileCopy(beaconServerGetClientExePath(), beaconexe))
	{
		beaconServerAddToSymStore(beaconexe);
	}
	if(!fileCopy(beaconServerGetClientPdbPath(), beaconpdb))
	{
		beaconServerAddToSymStore(beaconServerGetClientPdbPath());
	}

	fileCopy(beaconexe, "n:/beaconizer/");
	fileCopy(beaconpdb, "n:/beaconizer/");
}

void beaconServerCRCClient(void)
{
	if(!strstri(beaconGetExeDirectory(), "c:/beaconmasterserver"))
	{
		beacon_server.exeClient.crc = beaconGetExeCRC(beaconGetExeFileName(), &beacon_server.exeClient.data, &beacon_server.exeClient.size);
		return;
	}
	
	beacon_server.exeClient.crc = beaconGetExeCRC(beaconServerGetClientExePath(), &beacon_server.exeClient.data, &beacon_server.exeClient.size);
}

void beaconServerMakeNewExecutable(void)
{
	int i;
	char **folderNames = NULL;
	char **countsNames = NULL;
	char folders[][MAX_PATH] = {"beaconizer.exe","beaconizer.pdb","*.dll"};
	int recurse[] = {0, 0, 1};

	beaconServerCRCClient();

	if(beacon_server.noPatchServer)
	{
		return;
	}

	beaconServerPrepareExes();

	beacon_common.ccfunc(	&beacon_server.pcl_client, 
							beacon_server.patchServerName ? 
								beacon_server.patchServerName : 
								BEACON_DEFAULT_PATCHSERVER,
							BEACON_PATCH_SERVER_PORT, 
							300,
							beacon_comm,
							beaconGetExeDirectory(),
							NULL,
							NULL,
							NULL,
							NULL);

	beaconServerPCLWait(beacon_server.pcl_client);

	beacon_common.svfunc(   beacon_server.pcl_client,
							"Sentry",
							1,
							NULL,
							true,
							true,
							NULL,
							NULL);

	// Tell files to be uploaded
	beaconServerPCLWait(beacon_server.pcl_client);

	// Build directories
	for(i=0; i<ARRAY_SIZE_CHECKED(folders); i++)
	{
		char *folder = NULL;
		char *counts = NULL;
		estrCreate(&folder);
		estrCreate(&counts);
		estrPrintf(&folder, "%s\\%s", beaconGetExeDirectory(), folders[i]);
		estrPrintf(&counts, "%s", folders[i]);

		eaPush(&folderNames, folder);
		eaPush(&countsNames, counts);
	}

	{
		char *folder = NULL;
		char *counts = NULL;
		
		estrCreate(&folder);
		estrCreate(&counts);
		estrPrintf(&folder, "n:\\fightclub\\beaconizer\\hipri.cfg");
		estrPrintf(&counts, "hipri.cfg");

		eaPush(&folderNames, folder);
		eaPush(&countsNames, counts);
	}

	g_patching = 1;
	beacon_common.fffunc(	beacon_server.pcl_client, 
							folderNames,
							countsNames,
							recurse,
							eaSize(&folderNames),
							NULL,
							0,
							NULL,
							0,
							beaconServerCheckinFinished,
							NULL);
	beacon_common.ufunc(beacon_server.pcl_client, beaconServerProcessStatus, NULL);
	
	while(g_patching)
	{
		commMonitor(beacon_comm);

		beacon_common.pfunc(beacon_server.pcl_client);
	}

	beacon_common.ddfunc(beacon_server.pcl_client);
	beacon_server.pcl_client = NULL;
}

U32 beaconRequestServerIsComplete(void)
{
	return beacon_server.request.completed_project || eaSize(&beacon_server.queueList)==0;
}

const char* beaconRequestServerGetNamespace(void)
{
	return beacon_server.request.projectName;
}

F32 beaconRequestServerGetCompletion_internal(char **statusEstr)
{
	F32 complete, mapCountComplete, curMapComplete = 0;
	S32 mapIndex = beacon_server.curMapIndex - 1;

	if(eaSize(&beacon_server.queueList)<=0)
	{
		estrPrintf(statusEstr, "No maps to beaconize");
		return 1.0f;
	}

	if(mapIndex < 0)
	{
		estrPrintf(statusEstr, "Not started yet");
		return 0.0f;
	}

	if(mapIndex==eaSize(&beacon_server.queueList))
	{
		estrPrintf(statusEstr, "All maps finished");
		return 1.0f;
	}

	mapCountComplete = 1.0*mapIndex/eaSize(&beacon_server.queueList);
	switch(beacon_server.state)
	{
		xcase BSS_NOT_STARTED: {
			curMapComplete = 0;
		}
		xcase BSS_GENERATING: {
			F32 untouched = beacon_server.beaconGenerate.totalBlocks - beacon_server.beaconGenerate.closedBlocks;
			curMapComplete = 0.05;
			
			if(beacon_server.beaconGenerate.totalBlocks)
				curMapComplete += 0.25*(1-untouched/beacon_server.beaconGenerate.totalBlocks);
		}
		xcase BSS_CONNECT_BEACONS: {
			F32 unreached = combatBeaconArray.size - ea32Size(&beacon_server.beaconConnect.legalBeacons.indices);

			if(combatBeaconArray.size)
				curMapComplete = 0.3+0.6*(1-unreached/combatBeaconArray.size);
		}
		xcase BSS_WRITE_FILE: {
			curMapComplete = 0.9;
		}
		xcase BSS_DONE: {
			curMapComplete = 0.95;
		}
		xcase BSS_SEND_BEACON_FILE: {
			curMapComplete = 0.95;
		}
		xdefault: {
			curMapComplete = 0;
		}
	}
	
	mapCountComplete = CLAMPF32(mapCountComplete, 0, 1.0f);
	curMapComplete = CLAMPF32(curMapComplete, 0, 1.0f);
	complete = mapCountComplete + curMapComplete / eaSize(&beacon_server.queueList);
	estrPrintf(statusEstr, "Working on %s, map %d of %d, state %s", 
				zmapInfoGetPublicName(NULL), 
				mapIndex+1, 
				eaSize(&beacon_server.queueList), 
				beaconGetServerStateName(beacon_server.state));

	complete = CLAMPF32(complete, 0, 1.0f);
	return complete;
}


F32 beaconRequestServerGetCompletion(char **statusEstr)
{
	static float sLastVal = -1.0f;
	static char *spLastRetString = NULL;
	
	float fRetVal = beaconRequestServerGetCompletion_internal(statusEstr);

	if (fRetVal < sLastVal)
	{
//		AssertOrAlert("BAR_GOING_BACKWARDS", "beacon completion now %f, was %f. Old status string: <<%s>>. New status string: <<%s>>",
//			fRetVal, sLastVal, spLastRetString, *statusEstr);
//		Adam says this still happens in some corner cases, whatevs

		estrCopy(&spLastRetString, statusEstr);
		return sLastVal;
	}
	else
	{
		sLastVal = fRetVal;
		estrCopy(&spLastRetString, statusEstr);
	}

	return fRetVal;
}



void beaconRequestServerGetFilenames(char ***files)
{
	int i;
	char ns_path[MAX_PATH];

	for(i=0; i<eaSize(&beacon_server.request.fileNames); i++)
	{
		char* filename = beacon_server.request.fileNames[i];

		fileLocateWrite(filename, ns_path);
		eaPush(files, strdup(ns_path));
	}
	eaClearEx(&beacon_server.request.fileNames, NULL);
}

void beaconServerSetCSS(BeaconServerInfo *info)
{
	if(!info->pCSS)
	{
		estrPrintf(&info->pCSS,	"<style>"
								".serverTitle {"
								"	text-align: center;"
								"	font: verdana 30pt bold;"
								"}"
								"</style>");
	}
}

S32 beaconServerGetClientCount(BeaconClientType bct)
{
	return beacon_server.clientList[bct].count;
}

AUTO_COMMAND;
void beaconTestCommand(void)
{
	printf("hi");
}

void beaconServerGetExtendedStatus(char **estrOut, BeaconServerClientData *client)
{
	int i, count;
	BeaconServerStatus *stat = &client->server.status;
	BeaconServerStateDetails *details = &client->server.stateDetails;
		
	estrPrintf(estrOut,	"<tr class='server'>\n<td class='serverTitle'>%sServer</div>\n", beaconServerGetServerType(client->server.type));
	estrConcatf(estrOut, "<td>%s</td><td>%s</td>", client->computerName, client->clientIPStr);

	if(client->server.protocolVersion >= 3)
	{
		estrConcatf(estrOut, "<td>%s</td>",
							timeGetLocalDateStringFromSecondsSince2000(stat->gimmeTime - MAGIC_SS2000_TO_FILETIME));

		if(eaGet(&stat->mapStatus,details->curMap))
		{
			estrConcatf(estrOut,	"<td>%s</td>\n", 
									stat->mapStatus[details->curMap]->mapName);
		}
		else
		{
			// <a href='/directcommand?command=beaconTestCommand'>Test</a>
			estrConcatf(estrOut,	"<td>No map loaded</td>\n");
		}

		if(eaGet(&stat->mapStatus, details->lastCheckedIn))
		{
			estrConcatf(estrOut,	"<td>%s</td>\n", 
									stat->mapStatus[details->lastCheckedIn]->mapName);
		}
		else
		{
			estrConcatf(estrOut,	"<td>%s</td>\n", 
									"None");
		}

		estrConcatf(estrOut, "<td>\n");
		if(details->curMap+1<eaSize(&stat->mapStatus))
		{
			estrConcatf(estrOut, "<ol>\n");
			for(i=details->curMap+1, count=0; i<eaSize(&stat->mapStatus) && count<10; i++, count++)
			{
				if(!stat->mapStatus[i]->needsProcess)
					continue;

				ANALYSIS_ASSUME(stat->mapStatus);
				estrConcatf(estrOut, "<li><div class='mapListRow'>%s</div></li>\n",
					stat->mapStatus[i]->mapName);
			}
			estrConcatf(estrOut, "</ol>\n");
		}
		else
			estrConcatf(estrOut, "No more maps");
		estrConcatf(estrOut, "</td>\n");
	}
	else
	{
		estrConcatf(estrOut, "<td colspan='6'>Old Protocol</td>\n");
	}

	estrConcatf(estrOut, "</tr>\n");
}

void beaconMasterGetProjectInfo(char** estrOut, BeaconProject *proj)
{
	estrPrintf(estrOut, "<tr><td>%s</td><td>", proj->projName);
	estrConcatf(estrOut, "<table><tr><td>Branch</td><td></td></tr>");
	FOR_EACH_IN_EARRAY_FORWARDS(proj->histories, BeaconServerHistory, hist)
	{
		estrConcatf(estrOut, "<tr><td>%d</td><td>", hist->branch);
		if(eaSize(&hist->current))
		{
			estrConcatf(estrOut, "<table>\n");
			FOR_EACH_IN_EARRAY(hist->current, BeaconServerClientData, client)
			{
				char *estr = NULL;
				beaconServerGetExtendedStatus(&estr, client);
				
				estrAppend2(estrOut, "\n");
				estrAppend(estrOut, &estr);

				estrDestroy(&estr);
			}
			FOR_EACH_END
			estrConcatf(estrOut, "</table>\n");
		}
		else
		{
			estrConcatf(estrOut, "Last seen %s on %s", timeGetLocalDateStringFromSecondsSince2000(hist->lastSeen), hist->machineName);
		}
		estrConcatf(estrOut, "</tr></td>");
	}
	FOR_EACH_END

	estrConcatf(estrOut, "</table></td></tr>\n");
}

void beaconServerGetMasterServerInfo(BeaconServerInfo *info)
{
	int numProjects = g_BeaconProjects ? stashGetCount(g_BeaconProjects) : 0;
	estrPrintf(&info->pHtml,	"<div class='serverTitle'>MasterServer</div>\n"
								"<div>Num clients %d</div>\n", beaconServerGetClientCount(BCT_SENTRY));
	
	if(numProjects)
	{
		StashTableIterator iter;
		StashElement elem;
		estrConcatf(&info->pHtml, "<div>Known Projects</div>");
		estrConcatf(&info->pHtml, "<table><tr><td>Project</td><td></td></tr>\n");
		
		stashGetIterator(g_BeaconProjects, &iter);
		while(stashGetNextElement(&iter, &elem))
		{
			BeaconProject *proj = stashElementGetPointer(elem);
			char *estr = NULL;
			
			beaconMasterGetProjectInfo(&estr, proj);

			estrAppend(&info->pHtml, &estr);

			estrDestroy(&estr);
		}
		estrConcatf(&info->pHtml, "</table>");
	}
}

void beaconServerGetServerInfo(BeaconServerInfo *info)
{
	static S64 absTime = 0;

	if(ABS_TIME_SINCE(absTime)<SEC_TO_ABS_TIME(10))
		return;

	absTime = ABS_TIME;

	beaconServerSetCSS(info);

	if(beacon_server.isMasterServer)
		beaconServerGetMasterServerInfo(info);
}

static void beaconServerGetNewExe(void)
{
	
}

AUTO_COMMAND ACMD_NAME(bcnRequestAllowBeaconSkip);
void beaconServerAllowBeaconizingSkipIfFilesExist(int d)
{
	beacon_server.request.allow_skip = 1;
}

AUTO_COMMAND ACMD_NAME(useLocalSrc) ACMD_HIDE;
void beaconServerUseLocalSrc(int d)
{
	beacon_server.useLocalSrc = !!d;
}

AUTO_COMMAND ACMD_NAME(useMasterS) ACMD_HIDE;
void beaconServerUseMasterServer(char* mastername)
{
	beacon_common.masterServerName = strdup(mastername);
}

AUTO_COMMAND ACMD_NAME(usePatchS) ACMD_HIDE;
void beaconServerUsePatchServer(char *patchname)
{
	beacon_server.patchServerName = strdup(patchname);
}

AUTO_COMMAND ACMD_NAME(bcNoUpdate) ACMD_HIDE;
void beaconServerDisableUpdateForPatch(int d)
{
	beacon_server.noUpdate = !!d;
}

AUTO_COMMAND ACMD_NAME(bcNoPS) ACMD_HIDE;
void beaconServerDisablePatchServer(int d)
{
	beacon_server.noPatchServer = !!d;
}

AUTO_COMMAND ACMD_HIDE ACMD_NAME(bcnReqProcessProject) ACMD_CATEGORY(Beaconizer);
void beaconRequestServerBeaconizeProject(char *projectName)
{
	beacon_server.request.projectName = strdup(projectName);
}

AUTO_COMMAND ACMD_NAME(testBladeFile) ACMD_HIDE;
void beaconServerTestFileWrite(int unused)
{
	char filename[] = "server/maps/missions/mid/lizard_lair.worldgrid.v8.bcn";
	char absoluteFileName[1000];
	FILE* handle;
	fileLocateWrite(filename, absoluteFileName);
	
	handle = fileOpen(filename, "wb");

	if(!handle)
	{
		chmod(absoluteFileName, _S_IREAD|_S_IWRITE);
		handle = fileOpen(filename, "wb");
	}

	assert(handle);

	printfColor(COLOR_GREEN, "File opened successfully.\n");

	fileClose(handle);
}

#endif

static void beaconServerPlayableCreate(WorldVolumeEntry *ent)
{
	eaPush(&playableEnts, ent);
}

static void beaconServerPlayableDestroy(WorldVolumeEntry *ent)
{
	eaFindAndRemoveFast(&playableEnts, ent);
}

AUTO_STARTUP(Beaconizer) ASTRT_DEPS(WorldLibMain, WorldLibZone);
void BeaconizerStartupCommon(void)
{
	worldLibSetPlayableFunctions(beaconServerPlayableCreate, beaconServerPlayableDestroy);
}

AUTO_COMMAND ACMD_NAME(BcnSetDefaultAngleDivisions);
void beaconServerSetDefaultAngleDivisions(int d)
{
	beacon_server.defConfig->angleProcessIncrement = d;
}

AUTO_COMMAND ACMD_NAME(BSAllowCRCMismatch);
void beaconServerAllowCRCMismatch(int d)
{
	beacon_server.allowCRCMismatch = !!d;
}

AUTO_COMMAND ACMD_NAME(BSForceRebeaconize);
void beaconServerForceBeaconizeGround(int groundOnly, int spaceOnly, int includeNS)
{
	ZoneMapInfo *zmi = NULL;
	RefDictIterator iter = {0};
	beacon_server.forceRebuild = true;

	if(groundOnly && spaceOnly)
		printfColor(COLOR_RED, "You can't run groundOnly and spaceOnly\n");

	worldGetZoneMapIterator(&iter);
	while(zmi = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(groundOnly && beaconHasSpaceRegion(zmi))
			continue;

		if(spaceOnly && !beaconHasSpaceRegion(zmi))
			continue;

		if(!includeNS && resHasNamespace(zmapInfoGetPublicName(zmi)))
			continue;

		beaconServerQueueMapFile(zmapInfoGetPublicName(zmi));
	}
}

#include "beaconServer_c_ast.c"
#include "beaconServerPrivate_h_ast.c"
#include "wlBeacon_h_ast.c"
