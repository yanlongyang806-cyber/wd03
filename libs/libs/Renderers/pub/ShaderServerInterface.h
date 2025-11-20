#pragma once
GCC_SYSTEM



#define SHADERSERVER_VERSION 20091118 // current version

enum {
	SHADERSERVER_WORKER_CONNECT = 1, // From Worker to Server
	SHADERSERVER_CLIENT_REQUEST, // From Client to Server
	SHADERSERVER_WORKER_ASSIGNMENT_START, // From Server to Worker (same data as SHADERSERVER_CLIENT_REQUEST)
	SHADERSERVER_WORKER_ASSIGNMENT_DONE, // From Worker to Server
	SHADERSERVER_CLIENT_RESPONSE, // From Server to Client (same data as SHADERSERVER_WORKER_ASSIGNMENT_DONE)
	SHADERSERVER_SELFPATCH, // From Server to worker, new .exe to use
	SHADERSERVER_CLIENT_CONNECT, // From Client to Server
	SHADERSERVER_CLIENT_CONNECT_ACK, // Form Server to Client
};

// otherFlags member
enum {
	SHADERSERVER_FLAGS_VERSION_MASK = 0xff,
};

typedef enum ShaderTargetVersion
{
	SHADERTARGETVERSION_Default=0,
	SHADERTARGETVERSION_D3DX9_37=0, // same as default
	SHADERTARGETVERSION_D3DCompiler_42,

	SHADERTARGETVERSION_Max,
} ShaderTargetVersion;
STATIC_ASSERT(SHADERTARGETVERSION_Max <= SHADERSERVER_FLAGS_VERSION_MASK);

AUTO_ENUM;
typedef enum ShaderTarget {
	SHADERTARGET_PC = 0,
	SHADERTARGET_XBOX,
	SHADERTARGET_XBOX_UPDB,
	SHADERTARGET_PS3,
} ShaderTarget;

AUTO_STRUCT;
typedef struct ShaderCompileRequestData
{
	int request_id; // Used by client to match up requests with results
	ShaderTarget target;
	const char *programText;
	const char *entryPointName;
	const char *shaderModel;
	int compilerFlags;
	int otherFlags; // Not yet used
} ShaderCompileRequestData;

AUTO_STRUCT;
typedef struct ShaderCompileResponseData
{
	int request_id;
	int compiledResultSize; // Data follows this struct, 0 indicates failure
	int updbDataSize; // Data follows this struct + compiledResult
	char *errorMessage;
	char *updbPath;
	
	// Used in the client for handing things around, not serialized, needs to be freed manually:
	void *compiledResult; NO_AST
	void *updbData; NO_AST
} ShaderCompileResponseData;

extern ParseTable parse_ShaderCompileRequestData[];
#define TYPE_parse_ShaderCompileRequestData ShaderCompileRequestData
extern ParseTable parse_ShaderCompileResponseData[];
#define TYPE_parse_ShaderCompileResponseData ShaderCompileResponseData

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););
