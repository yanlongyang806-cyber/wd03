#include "TaskServerClientInterface.h"
#include "error.h"
#include "timing.h"
#include "ThreadManager.h"
#include "MemoryPool.h"
#include "net.h"
#include "sysutil.h"
#include "mathutil.h"
#include "file.h"
#include "fileutil.h"
#include "logging.h"
#include "earray.h"
#include "textparser.h"
#include "Organization.h"
#include "structHist.h"
#include "structNet.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:taskServerCommThread", BUDGET_Renderer););

U32 giTaskServerPort = MIN_TASKSERVER_PORT;
AUTO_COMMAND ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void TaskServerPort(int iPort)
{
	assertmsgf(iPort >= MIN_TASKSERVER_PORT && iPort < MAX_TASKSERVER_PORT, "Trying to set task server port to %u... legal values are %u to %u\n",
		iPort, MIN_TASKSERVER_PORT, MAX_TASKSERVER_PORT);
	giTaskServerPort = iPort;
}


static CRITICAL_SECTION taskServerCriticalSection;
static bool taskServerInited;
static ManagedThread *taskServerThread;
static volatile bool taskServerConnected;
static TaskServerClientConnection clientConn;

typedef struct TaskServerThreadRequest
{
	TaskClientTaskPacket task_request;
	ParseTable *type_TaskDataPacket;
	void * task_data_packet;

	TaskServerCallback callback;
	void *userData;
} TaskServerThreadRequest;

MP_DEFINE(TaskServerThreadRequest);

static TaskServerThreadRequest **taskServerRequests; // Request thread(s) -> worker thread communication
static TaskServerThreadRequest **activeRequests=NULL; // These have been sent to the TaskServer

char taskServerAddr[1024];
// Sets the address of the TaskServer to connect to
AUTO_CMD_STRING(taskServerAddr, taskServerAddr) ACMD_CMDLINE;

AUTO_RUN;
void taskServerCriticalSectionInit(void)
{
	InitializeCriticalSection(&taskServerCriticalSection);
	if (!taskServerAddr[0])
	{
		strcpy(taskServerAddr, "RemeshServer");
	}
}


static void taskServerClientConnect(NetLink *link,void *user_data)
{
	Packet	*pkt;
	assert(!taskServerConnected);
	taskLogPrintf("Connected to TaskServer\n");
	pkt = pktCreate(link,TASKSERVER_CLIENT_CONNECT);
	pktSendString(pkt,getComputerName());
	pktSendBitsAuto(pkt, TASKSERVER_VERSION);
	pktSend(&pkt);
}

static void taskServerClientGotConnectAck(Packet *pak)
{
	int ok = pktGetBits(pak, 1);
	int server_version = pktGetBitsAuto(pak);
	assert(!taskServerConnected);
	if (!ok) {
		taskLogPrintf("Failed to connect to TaskServer, client version : %d, server version : %d\n", TASKSERVER_VERSION, server_version);
	} else {
		taskLogPrintf("TaskServer ready to receive requests\n");
		taskServerConnected = true;
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

void taskHeaderAttachFile(TaskClientTaskPacket *taskHeader, const char *fileName)
{
	taskHeaderRemoveAttachedFile(taskHeader);
	taskHeader->fileAttachment = StructAllocString(fileName);
}

void taskHeaderRemoveAttachedFile(TaskClientTaskPacket *taskHeader)
{
	if (taskHeader->fileAttachment)
	{
		StructFreeString(taskHeader->fileAttachment);
		taskHeader->fileAttachment = NULL;
	}
}

void taskHeaderLeaveResultAttachmentFile(TaskClientTaskPacket *taskHeader)
{
	strcpy(taskHeader->fileAttachmentFull, "");
}

bool taskServerSendFile(NetLink *link, char *fileName, U32 *fileSizeBytes)
{
	char fileNamePart[MAX_PATH];
	bool bSendSuccess = false;

	if (fileSizeBytes)
		*fileSizeBytes = fileSize(fileName);
	fileGetFilename(fileName, fileNamePart);
	taskLogPrintf("\tSending \"%s\"...\n", fileNamePart);
	bSendSuccess = linkFileSendingMode_SendFileBlocking(link, TASKSERVER_FILE_RECEIVED, fileName, fileNamePart);
	taskLogPrintf("\tSending %s.\n", bSendSuccess ? "succeeded" : "failed");
	return bSendSuccess;
}

static void taskServerClientSendRequest(NetLink *server_link, TaskServerThreadRequest *request)
{
	Packet *pak = pktCreate(server_link, TASKSERVER_CLIENT_REQUEST);
	int index = getFreeActiveRequestIndex();
	U32 packetSendBytes = 0;

	activeRequests[index] = request;

	if (request->task_request.fileAttachment)
	{
		taskStartTimer(&request->task_request.sendStats);
		if (!taskServerSendFile(server_link, request->task_request.fileAttachmentFull, &packetSendBytes))
			taskHeaderRemoveAttachedFile(&request->task_request);
		taskStopTimer(&request->task_request.sendStats);
	}

	request->task_request.request_id = index;
	ParserSendStructSafe(parse_TaskClientTaskPacket, pak, &request->task_request);
	ParserSendStructSafe(request->type_TaskDataPacket, pak, request->task_data_packet);
	pktSendBits(pak, 1, 0); // terminator
	packetSendBytes += pktGetSize(pak);
	pktSend(&pak);

	taskAttributeWriteIO(&request->task_request.sendStats, packetSendBytes);
}

typedef struct TaskClient_TaskDef
{
	ParseTable * type_taskRequestData;
	ParseTable * type_taskResultData;
	void (*onCompleteTask)(TaskClientTaskPacket * taskResponse, TaskServerRequestStatus serverResponse, void *taskResultData, void *userData);
} TaskClient_TaskDef;

void handleServerShaderCompileResponse(TaskClientTaskPacket * taskResponse, TaskServerRequestStatus serverResponse, void *taskResultData, void *userData)
{
	taskLogPrintf("Client rcvd server response to TASKSERVER_TASK_COMPILE_SHADER\n");
}

void handleServerRemeshResponse(TaskClientTaskPacket * taskResponse, TaskServerRequestStatus serverResponse, void *taskResultData, void *userData)
{
	taskLogPrintf("Client rcvd server response to TASKSERVER_TASK_REMESH_CLUSTER\n");
}

TaskClient_TaskDef shaderClientTaskTypes[TASKSERVER_TASK_MAX] = 
{
	{ // client connect dummy task
		NULL,
		NULL,
		NULL,
	},
	{
		parse_ShaderCompileTaskRequestData,
		parse_ShaderCompileTaskResponseData,
		handleServerShaderCompileResponse
	},
	{
		parse_SpawnRequestData,
		parse_SpawnRequestData,
		handleServerRemeshResponse
	},
};

TaskClient_TaskDef * getTaskClientDataType(int taskType)
{
	assert(taskType >= TASKSERVER_TASK_CONNECT_TO_SERVER && taskType < TASKSERVER_TASK_MAX);
	return shaderClientTaskTypes + taskType;
}


static void taskServerClientGotResponse(Packet *pkt)
{
	TaskClientTaskPacket * responsePacket = NULL;
	TaskServerThreadRequest *request;
	int response_request_id = 0;
	int terminator;
	TaskClient_TaskDef * taskDef = NULL;
	void * taskResultData = NULL;

	responsePacket = ParserRecvStructSafe_Create(parse_TaskClientTaskPacket, pkt);

	response_request_id = responsePacket->request_id;

	assert(eaSize(&activeRequests));
	assert(response_request_id < eaSize(&activeRequests));
	request = activeRequests[response_request_id];
	activeRequests[response_request_id] = NULL;

	taskDef = getTaskClientDataType(responsePacket->task_type);
	taskResultData = ParserRecvStructSafe_Create(taskDef->type_taskResultData, pkt);

	if (responsePacket->fileAttachment)
	{
		sprintf(responsePacket->fileAttachmentFull, "%s/%s", fileTempDir(), responsePacket->fileAttachment);

		// SIMPLYGON TODO demote this assert to return an error code, to allow graceful retry or reporting
		assert(fileExists(responsePacket->fileAttachmentFull));
	}

	terminator = pktGetBits(pkt, 1);
	assert(terminator == 0);

	taskAttributeReadIO(&responsePacket->recvStats, fileSize(responsePacket->fileAttachmentFull));
	responsePacket->sendStats = request->task_request.sendStats;

	taskDef->onCompleteTask(responsePacket, TASKSERVER_GOT_RESPONSE, taskResultData, request->userData);
	request->callback(TASKSERVER_GOT_RESPONSE, responsePacket, taskResultData, request->userData);

	if (responsePacket->fileAttachment)
		fileForceRemove(responsePacket->fileAttachmentFull);


	StructDeInit(parse_TaskClientTaskPacket, responsePacket);

	EnterCriticalSection(&taskServerCriticalSection);
	MP_FREE(TaskServerThreadRequest, request);
	LeaveCriticalSection(&taskServerCriticalSection);
}

static void taskServerClientMsg(Packet *pak, int cmd, NetLink* link, void *user_data)
{
	if (linkFileSendingMode_ReceiveHelper(link, cmd, pak))
		return;

 	switch(cmd)
 	{
		xcase TASKSERVER_CLIENT_CONNECT_ACK:
			taskServerClientGotConnectAck(pak);
 		xcase TASKSERVER_CLIENT_RESPONSE:
 			taskServerClientGotResponse(pak);
		xdefault:
			assert(0);
 	}
}

static void taskServerClientDisconnect(NetLink *link,void *user_data)
{
	char *pDisconnectReason = NULL;
	linkGetDisconnectReason(link, &pDisconnectReason);
	taskLogPrintf("Disconnect from server; reason %s\n", pDisconnectReason);
	estrDestroy(&pDisconnectReason);
	// Free all active requests and let the requesters know about it
	FOR_EACH_IN_EARRAY(activeRequests, TaskServerThreadRequest, request)
	{
		if (request) {
			request->callback(TASKSERVER_NOT_RUNNING, &request->task_request, NULL, request->userData);
			EnterCriticalSection(&taskServerCriticalSection);
			MP_FREE(TaskServerThreadRequest, request);
			LeaveCriticalSection(&taskServerCriticalSection);
		}
	}
	FOR_EACH_END;
	eaDestroy(&activeRequests);
}

void taskClientHandleFileSendingErrors(char *errorMsg)
{
	taskLogPrintf("\tClient file sending layer error: %s", errorMsg);
}

static void taskClientLogReceivedFile(int iCmd, char *pFileName)
{
	taskLogPrintf("\tClient received file: %s\n", pFileName);
}

void taskServerConnectClientToServer(TaskServerClientConnection *pClient)
{
	pClient->server_link = commConnect(pClient->comm,LINKTYPE_UNSPEC|LINKTYPE_FLAG_NO_ALERTS_ON_FULL_SEND_BUFFER, LINK_FORCE_FLUSH,taskServerAddr,giTaskServerPort,taskServerClientMsg,taskServerClientConnect,taskServerClientDisconnect,0);
	linkSetTimeout(pClient->server_link, 0.0f);
	linkSetKeepAlive(pClient->server_link);
	linkFileSendingMode_InitSending(pClient->server_link);
	linkFileSendingMode_InitReceiving(pClient->server_link, taskClientHandleFileSendingErrors);
	linkFileSendingMode_RegisterCallback(pClient->server_link, TASKSERVER_FILE_RECEIVED, fileTempDir(), taskClientLogReceivedFile);
	// Don't ever send warnings about this link resizing, it's supposed to resize a lot in a small number of cases
	linkNoWarnOnResize(pClient->server_link);
}

void taskServerInitClientConnection(TaskServerClientConnection *pClient)
{
	pClient->comm = commCreate(100, 0);
	pClient->server_link = NULL;
	pClient->not_connected = 0;
	taskServerConnectClientToServer(pClient);
}


static DWORD WINAPI taskServerCommThread( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
	TaskServerClientConnection *pClient = (TaskServerClientConnection*)lpParam;
	PERFINFO_AUTO_START("taskServerCommThread", 1);
	{
		if (!pClient->comm)
			taskServerInitClientConnection(pClient);
		pClient->not_connected = timerAlloc();
		for(;;)
		{
			TaskServerThreadRequest *request;
			commMonitor(pClient->comm);
			SleepEx(100, TRUE);

			// Try to connect if we're not connected
			if (linkDisconnected(pClient->server_link) || (!linkConnected(pClient->server_link) && timerElapsed(pClient->not_connected) > 20.0f))
			{
				taskServerConnected = false;
				linkRemove(&pClient->server_link);
			}
			if (!pClient->server_link) {
				taskServerConnected = false;
				taskServerConnectClientToServer(pClient);
				timerStart(pClient->not_connected);
			}
			if (linkConnected(pClient->server_link))
			{
				timerStart(pClient->not_connected);
			}

			// Check for requests from the main thread
			EnterCriticalSection(&taskServerCriticalSection);
			while (request = eaPop(&taskServerRequests)) {
				if (request->task_request.task_type == TASKSERVER_TASK_CONNECT_TO_SERVER)
				{
					MP_FREE(TaskServerThreadRequest, request);
					continue;
				}
				if (taskServerConnected) {
					// Send a request and queue into active
					taskServerClientSendRequest(pClient->server_link, request);
				} else {
					// TODO: Queue these for X seconds in case this is the first attempt and we are not yet connected, or pre-connect?
					
					request->callback(TASKSERVER_NOT_RUNNING, &request->task_request, NULL, request->userData);

					MP_FREE(TaskServerThreadRequest, request);
				}
			}
			LeaveCriticalSection(&taskServerCriticalSection);
		}
		// These never get called:
		linkRemove(&pClient->server_link);
		timerFree(pClient->not_connected);
		commDestroy(&pClient->comm);
	}
	PERFINFO_AUTO_STOP();
	EXCEPTION_HANDLER_END
	return 0; 
}

bool taskServerWaitForConnection()
{
	while (!taskServerConnected)
		Sleep( 5 );
	return taskServerConnected;
}

void taskServerClientConnectToServer();

static void taskServerInit(void)
{
	bool didTaskServerInit = false;
	if (taskServerInited)
		return;
	EnterCriticalSection(&taskServerCriticalSection);
	if (!taskServerInited) {
		taskServerInited = true;

		taskServerThread = tmCreateThreadEx(taskServerCommThread, &clientConn, 0, ~0);
		MP_CREATE(TaskServerThreadRequest, 16);

		taskServerClientConnectToServer();
		didTaskServerInit = true;

		taskServerSetupLog();
	}
	LeaveCriticalSection(&taskServerCriticalSection);
	if (didTaskServerInit)
		taskServerWaitForConnection();
}

void taskServerSendTask(int taskType, int worker_version, const char * dataFile, void *taskRequestData, TaskServerCallback callback, void *userData)
{
	TaskServerThreadRequest *request_data;
	TaskClient_TaskDef *taskDef = getTaskClientDataType(taskType);

	taskServerInit();
	EnterCriticalSection(&taskServerCriticalSection);
	request_data = MP_ALLOC(TaskServerThreadRequest);
	request_data->task_request.task_type = taskType;
	request_data->task_request.worker_version = worker_version;
	if (dataFile)
	{
		fileGetFilename(dataFile, request_data->task_request.fileAttachmentFull);
		request_data->task_request.fileAttachment = StructAllocString(request_data->task_request.fileAttachmentFull);
		strcpy(request_data->task_request.fileAttachmentFull, dataFile);
	}
	request_data->type_TaskDataPacket = taskDef->type_taskRequestData;
	request_data->task_data_packet = taskRequestData;
	request_data->callback = callback;
	request_data->userData = userData;
	eaPush(&taskServerRequests, request_data);
	LeaveCriticalSection(&taskServerCriticalSection);
}

void taskServerClientConnectToServer()
{
	taskServerSendTask(TASKSERVER_TASK_CONNECT_TO_SERVER, 0, NULL, NULL, NULL, NULL);
}

void taskServerRequestCompile(ShaderCompileTaskRequestData *request, ShaderTaskTargetVersion worker_version, TaskServerCallback callback, void *userData)
{
	taskServerSendTask(TASKSERVER_TASK_COMPILER_SHADER, worker_version, NULL, request, callback, userData);
}

void taskServerRequestExec(SpawnRequestData *request, const char * dataFile, TaskServerCallback callback, void *userData)
{
	taskServerSendTask(TASKSERVER_TASK_REMESH_CLUSTER, 0, dataFile, request, callback, userData);
}


char taskServerLogFile[MAX_PATH];

void taskServerSetupLog()
{
	sprintf(taskServerLogFile, "TaskServer_PID%u.log", GetCurrentProcessId());
}

int taskLogPrintf(FORMAT_STR char const *fmt, ...)
{
	int result;
	va_list ap;
	char *msg = NULL;
	estrStackCreate(&msg);

	va_start(ap, fmt);
	result = filelog_vprintf_echo(true, taskServerLogFile, fmt, ap);
	va_end(ap);

	return result;
}

#include "TaskServerClientInterface_h_ast.c"

