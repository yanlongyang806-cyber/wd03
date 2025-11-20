#pragma once
GCC_SYSTEM

#include "textparserUtils.h"
#include "GlobalComm.h"
#include "TaskProfile.h"

typedef struct NetComm NetComm; 
typedef struct NetLink NetLink; 

typedef struct TaskServerClientConnection
{
	NetComm *comm;
	NetLink *server_link;
	int not_connected;
} TaskServerClientConnection;

// Note, this must not conflict with ShaderServer!
//#define TASKSERVER_PORT 9102
extern U32 giTaskServerPort;

#define TASKSERVER_VERSION 20120822 // current version

enum {
	TASKSERVER_WORKER_CONNECT = DEPRECATED_SHAREDCMD_MAX, // From Worker to Server
	TASKSERVER_CLIENT_REQUEST, // From Client to Server
	TASKSERVER_WORKER_ASSIGNMENT_START, // From Server to Worker (same data as TASKSERVER_CLIENT_REQUEST)
	TASKSERVER_WORKER_ASSIGNMENT_DONE, // From Worker to Server
	TASKSERVER_CLIENT_RESPONSE, // From Server to Client (same data as TASKSERVER_WORKER_ASSIGNMENT_DONE)
	TASKSERVER_SELFPATCH, // From Server to worker, new .exe to use
	TASKSERVER_CLIENT_CONNECT, // From Client to Server
	TASKSERVER_CLIENT_CONNECT_ACK, // Form Server to Client

	TASKSERVER_FILE_RECEIVED
};

// otherFlags member
enum {
	TASKSERVER_FLAGS_VERSION_MASK = 0xff,
};

typedef enum TaskServerTaskType
{
	TASKSERVER_TASK_CONNECT_TO_SERVER,

	TASKSERVER_TASK_COMPILER_SHADER,
	TASKSERVER_TASK_REMESH_CLUSTER,

	TASKSERVER_TASK_MAX
} TaskServerTaskType;

AUTO_STRUCT;
typedef struct TaskClientTaskPacket
{
	int request_id;
	int task_type;
	int worker_version;

	char *fileAttachment;
	char fileAttachmentFull[MAX_PATH];	NO_AST

	TaskProfile sendStats;
	TaskProfile recvStats;

	char *requestCacheKey;	NO_AST
} TaskClientTaskPacket;

extern ParseTable parse_TaskClientTaskPacket[];
#define TYPE_parse_TaskClientTaskPacket TaskClientTaskPacket


typedef enum ShaderTaskTargetVersion
{
	SHADERTASKTARGETVERSION_Default=0,
	SHADERTASKTARGETVERSION_D3DX9_37=0, // same as default
	SHADERTASKTARGETVERSION_D3DCompiler_42,

	SHADERTASKTARGETVERSION_Max,
} ShaderTaskTargetVersion;

AUTO_ENUM;
typedef enum ShaderTaskTarget {
	SHADERTASKTARGET_PC = 0,
	SHADERTASKTARGET_XBOX,
	SHADERTASKTARGET_XBOX_UPDB,
	SHADERTASKTARGET_PS3,
} ShaderTaskTarget;

AUTO_STRUCT;
typedef struct ShaderCompileTaskRequestData
{
	int request_id; // Used by client to match up requests with results
	ShaderTaskTarget target;
	const char *programText;
	const char *entryPointName;
	const char *shaderModel;
	int compilerFlags;
	int otherFlags; // Not yet used
} ShaderCompileTaskRequestData;

AUTO_STRUCT;
typedef struct ShaderCompileTaskResponseData
{
	int compiledResultSize; // Data follows this struct, 0 indicates failure
	int updbDataSize; // Data follows this struct + compiledResult
	char *errorMessage;
	char *updbPath;

	TextParserBinaryBlock compiledResult;
	TextParserBinaryBlock updbData;
} ShaderCompileTaskResponseData;

extern ParseTable parse_ShaderCompileTaskRequestData[];
#define TYPE_parse_ShaderCompileTaskRequestData ShaderCompileTaskRequestData
extern ParseTable parse_ShaderCompileTaskResponseData[];
#define TYPE_parse_ShaderCompileTaskResponseData ShaderCompileTaskResponseData


AUTO_ENUM;
typedef enum RemeshWorkerVersion {
	REMESHWORKERVERSION_CURRENT = 0,
	REMESHWORKERVERSION_Max,
} RemeshWorkerVersion;

AUTO_STRUCT;
typedef struct SpawnRequestData
{
	int request_id; // Used by client to match up requests with results
	const char *label;
	int remeshSystemExitCode;
	int remeshSystemErrnoCode;
	TextParserBinaryBlock data_block;
} SpawnRequestData;


extern ParseTable parse_SpawnRequestData[];
#define TYPE_parse_SpawnRequestData SpawnRequestData


typedef enum TaskServerRequestStatus {
	TASKSERVER_NOT_RUNNING,
	TASKSERVER_GOT_RESPONSE,
} TaskServerRequestStatus;

typedef void (*TaskServerCallback)(TaskServerRequestStatus status, TaskClientTaskPacket * task, void *response, void *userData);

bool taskServerWaitForConnection();

void taskHeaderAttachFile(TaskClientTaskPacket *taskHeader, const char *fileName);
void taskHeaderRemoveAttachedFile(TaskClientTaskPacket *taskHeader);
void taskHeaderLeaveResultAttachmentFile(TaskClientTaskPacket *taskHeader);
bool taskServerSendFile(NetLink *link, char *fileName, U32 *fileSizeBytes);

// Uses a reference to the data pointed to in request, caller must not free until the callback is called
void taskServerRequestCompile(ShaderCompileTaskRequestData *request, ShaderTaskTargetVersion worker_version, 
	TaskServerCallback callback, void *userData);
void taskServerRequestExec(SpawnRequestData *request, const char * dataFile, TaskServerCallback callback, void *userData);

void taskServerSetupLog();
int taskLogPrintf(FORMAT_STR char const *fmt, ...);
