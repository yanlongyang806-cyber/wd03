#pragma once

typedef enum JsonRPCErrorCode
{
	// The following are from the JSON-RPC spec
	JSONRPCE_PARSE_ERROR = -32700,
	JSONRPCE_INVALID_REQUEST = -32600,
	JSONRPCE_METHOD_NOT_FOUND = -32601,
	JSONRPCE_INVALID_PARAMS = -32602,
	JSONRPCE_INTERNAL_ERROR = -32603,

	// The following are used to interpret results
	JSONRPCE_SUCCESS = 0, // Interpret the result string as a string
	JSONRPCE_SUCCESS_RAW = 1, // Interpret the result string as if it were the entire response
} JsonRPCErrorCode;

typedef struct CmdContext CmdContext;
typedef struct CmdSlowReturnForServerMonitorInfo CmdSlowReturnForServerMonitorInfo;

//returns true if it successfully set a meaningful json return string in pContext->outputmsg
bool CmdParseJsonRPC(char *pJsonString, CmdContext *pContext);


//DO NOT CALL THIS, it get called automatically when needed
void DoSlowCmdReturn_JsonRPC(const char *pRetString, ParseTable *pTPI, void *pStruct, S64 iRetValInt, CmdSlowReturnForServerMonitorInfo *pSlowInfo);

//given a Json command string, which is needed to get the ID and version, generate a formatted JSON RPC error
//string with the given error message
//
//returns a static string
char *GetJsonRPCErrorString(char *pJsonString, FORMAT_STR const char *pFmt, ...);

//takes a JSON command, and adds a given named string argument to it, overwriting any one that is already there
bool AddArgumentToJsonCommand(char **ppOutCmdString, char *pInCmdString, char *pArgName, char *pArgValue);

typedef void (*CmdParseJsonMissingCallback)(CmdContext *pContext, const char *pJsonString);

//register a callback to be called if a JSON-RPC method is not found
void RegisterJsonRPCMissingCallback(CmdParseJsonMissingCallback callback);