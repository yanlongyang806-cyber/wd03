#include "rt_xShaderServerInterface.h"
#include "ShaderServerInterface.h"
#include "ThreadManager.h"
#include "MemoryPool.h"
#include "net.h"
#include "sysutil.h"
#include "file.h"
#include "XboxThreads.h"
#include "earray.h"
#include "textparser.h"
#include "Organization.h"
#include "crypticPorts.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:shaderServerCommThread", BUDGET_Renderer););

static CRITICAL_SECTION shaderServerCriticalSection;
static bool shaderServerInited;
static ManagedThread *shaderServerThread;
static bool shaderServerConnected;

typedef struct ShaderServerThreadRequest
{
	ShaderCompileRequestData request;
	ShaderServerCallback callback;
	void *userData;
} ShaderServerThreadRequest;

MP_DEFINE(ShaderServerThreadRequest);

static ShaderServerThreadRequest **shaderServerRequests; // Request thread(s) -> worker thread communication
static ShaderServerThreadRequest **activeRequests=NULL; // These have been sent to the ShaderServer

char shaderServerAddr[1024];
// Sets the address of the ShaderServer to connect to
AUTO_CMD_STRING(shaderServerAddr, shaderServerAddr) ACMD_CMDLINE;

AUTO_RUN;
void shaderServerCriticalSectionInit(void)
{
	InitializeCriticalSection(&shaderServerCriticalSection);
	if (!shaderServerAddr[0])
	{
		strcpy(shaderServerAddr, "ShaderServer");
	}
}


static void shaderServerClientConnect(NetLink *link,void *user_data)
{
	Packet	*pkt;
	assert(!shaderServerConnected);
	//verbose_printf("Connected to ShaderServer\n");
	pkt = pktCreate(link,SHADERSERVER_CLIENT_CONNECT);
	pktSendString(pkt,getComputerName());
	pktSendBitsAuto(pkt, SHADERSERVER_VERSION);
	pktSend(&pkt);
}

static void shaderServerClientGotConnectAck(Packet *pak)
{
	int ok = pktGetBits(pak, 1);
	int server_version = pktGetBitsAuto(pak);
	assert(!shaderServerConnected);
	if (!ok) {
		verbose_printf("Failed to connect to ShaderServer, client version : %d, server version : %d\n", SHADERSERVER_VERSION, server_version);
	} else {
		//verbose_printf("ShaderServer ready to receive requests\n");
		shaderServerConnected = true;
	}
}

static int getFreeActiveRequestIndex(void)
{
	int index;
	for (index=0; index < eaSize(&activeRequests); index++)
		if (!activeRequests[index])
			break;
	if (index == eaSize(&activeRequests))
		eaPush(&activeRequests, NULL);
	return index;
}

static void shaderServerClientSendRequest(NetLink *server_link, ShaderServerThreadRequest *request)
{
	Packet *pak = pktCreate(server_link, SHADERSERVER_CLIENT_REQUEST);
	int index = getFreeActiveRequestIndex();
	activeRequests[index] = request;
	request->request.request_id = index;
	pktSendBitsAuto(pak, request->request.request_id);
	pktSendBitsAuto(pak, request->request.target);
	pktSendString(pak, request->request.programText);
	pktSendString(pak, request->request.entryPointName);
	pktSendString(pak, request->request.shaderModel);
	pktSendBitsAuto(pak, request->request.compilerFlags);
	pktSendBitsAuto(pak, request->request.otherFlags);
	pktSendBits(pak, 1, 0); // terminator

	pktSend(&pak);
}

static void shaderServerClientGotResponse(Packet *pkt)
{
	ShaderCompileResponseData response = {0};
	ShaderServerThreadRequest *request;
	int terminator;

	response.request_id = pktGetBitsAuto(pkt);
	response.compiledResultSize = pktGetBitsAuto(pkt);
	response.updbDataSize = pktGetBitsAuto(pkt);
	response.errorMessage = strdup(pktGetStringTemp(pkt));
	response.updbPath = strdup(pktGetStringTemp(pkt));

	assert(eaSize(&activeRequests));
	assert(response.request_id < eaSize(&activeRequests));
	request = activeRequests[response.request_id];
	activeRequests[response.request_id] = NULL;
	if (response.compiledResultSize) {
		response.compiledResult = malloc(response.compiledResultSize);
		pktGetBytes(pkt, response.compiledResultSize, response.compiledResult);
	}
	if (response.updbDataSize) {
		response.updbData = malloc(response.updbDataSize);
		pktGetBytes(pkt, response.updbDataSize, response.updbData);
	}
	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);

	request->callback(SHADER_SERVER_GOT_RESPONSE, &response, request->userData);
	SAFE_FREE(response.compiledResult);
	SAFE_FREE(response.updbData);
	StructDeInit(parse_ShaderCompileResponseData, &response);
	EnterCriticalSection(&shaderServerCriticalSection);
	MP_FREE(ShaderServerThreadRequest, request);
	LeaveCriticalSection(&shaderServerCriticalSection);
}

static void shaderServerClientMsg(Packet *pak, int cmd, NetLink* link, void *user_data)
{
 	switch(cmd)
 	{
		xcase SHADERSERVER_CLIENT_CONNECT_ACK:
			shaderServerClientGotConnectAck(pak);
 		xcase SHADERSERVER_CLIENT_RESPONSE:
 			shaderServerClientGotResponse(pak);
		xdefault:
			assert(0);
 	}
}

static void shaderServerClientDisconnect(NetLink *link,void *user_data)
{
	// Free all active requests and let the requesters know about it
	ShaderCompileResponseData response = {0};
	response.errorMessage = StructAllocString("dummy failure");
	FOR_EACH_IN_EARRAY(activeRequests, ShaderServerThreadRequest, request)
	{
		if (request) {
			request->callback(SHADER_SERVER_NOT_RUNNING, &response, request->userData);
			EnterCriticalSection(&shaderServerCriticalSection);
			MP_FREE(ShaderServerThreadRequest, request);
			LeaveCriticalSection(&shaderServerCriticalSection);
		}
	}
	FOR_EACH_END;
	eaDestroy(&activeRequests);
	StructDeInit(parse_ShaderCompileResponseData, &response);
}

static DWORD WINAPI shaderServerCommThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
	PERFINFO_AUTO_START("shaderServerCommThread", 1);
	{
		NetComm *comm;
		NetLink *server_link=NULL;
		int not_connected;
		comm = commCreate(100, 0);
		not_connected = timerAlloc();
		for(;;)
		{
			ShaderServerThreadRequest *request;
			commMonitor(comm);
			SleepEx(100, TRUE);

			// Try to connect if we're not connected
			if (linkDisconnected(server_link) || (!linkConnected(server_link) && timerElapsed(not_connected) > 20.0))
			{
				shaderServerConnected = false;
				linkRemove(&server_link);
			}
			if (!server_link) {
				shaderServerConnected = false;
				server_link = commConnect(comm,LINKTYPE_UNSPEC|LINKTYPE_FLAG_NO_ALERTS_ON_FULL_SEND_BUFFER, LINK_FORCE_FLUSH,shaderServerAddr,SHADERSERVER_PORT,shaderServerClientMsg,shaderServerClientConnect,shaderServerClientDisconnect,0);
				// Don't ever send warnings about this link resizing, it's supposed to resize a lot in a small number of cases
				linkNoWarnOnResize(server_link);
				timerStart(not_connected);
			}
			if (linkConnected(server_link))
			{
				timerStart(not_connected);
			}

			// Check for requests from the main thread
			EnterCriticalSection(&shaderServerCriticalSection);
			while (request = eaPop(&shaderServerRequests)) {
				if (shaderServerConnected) {
					// Send a request and queue into active
					shaderServerClientSendRequest(server_link, request);
				} else {
					// TODO: Queue these for X seconds in case this is the first attempt and we are not yet connected, or pre-connect?
					ShaderCompileResponseData response = {0};
					response.errorMessage = StructAllocString("dummy failure");
					request->callback(SHADER_SERVER_NOT_RUNNING, &response, request->userData);
					StructDeInit(parse_ShaderCompileResponseData, &response);
					MP_FREE(ShaderServerThreadRequest, request);
				}
			}
			LeaveCriticalSection(&shaderServerCriticalSection);
		}
		// These never get called:
		linkRemove(&server_link);
		timerFree(not_connected);
		commDestroy(&comm);
	}
	PERFINFO_AUTO_STOP();
	EXCEPTION_HANDLER_END
	return 0; 
}

static void shaderServerInit(void)
{
	if (shaderServerInited)
		return;
	EnterCriticalSection(&shaderServerCriticalSection);
	if (!shaderServerInited) {
		shaderServerInited = true;

		shaderServerThread = tmCreateThreadEx(shaderServerCommThread, NULL, 0, THREADINDEX_MISC);
		MP_CREATE(ShaderServerThreadRequest, 16);
	}
	LeaveCriticalSection(&shaderServerCriticalSection);
}

void shaderServerRequestCompile(ShaderCompileRequestData *request, ShaderServerCallback callback, void *userData)
{
	ShaderServerThreadRequest *request_data;
	shaderServerInit();
	EnterCriticalSection(&shaderServerCriticalSection);
	request_data = MP_ALLOC(ShaderServerThreadRequest);
	request_data->request = *request;
	request_data->callback = callback;
	request_data->userData = userData;
	eaPush(&shaderServerRequests, request_data);
	LeaveCriticalSection(&shaderServerCriticalSection);
}
