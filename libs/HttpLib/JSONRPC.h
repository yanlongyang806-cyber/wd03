#pragma once

/*********************************************************
JSON-RPC Notes:
The return value of any JSON-RPCs called through this library MUST be a dictionary (or object).
Primitive type result responses will fail to parse (integers, strings).
*********************************************************/

#include "HttpLib.h"
#include "HttpClient.h"

// Note: This callback depends on the comm's commMonitor() being called regularly, along with a call
//       to httpClientProcessTimeouts(), which is called inside of utilitiesLibOncePerFrame(). 
typedef void (*jsonrpcFinishedCB)(struct JSONRPCState *state, void * userData);

AUTO_STRUCT;
typedef struct JSONRPCErrorDetails
{
	int code;
	char *name;      AST(ESTRING)
	char *message;   AST(ESTRING)
	char *stack;     AST(ESTRING)
} JSONRPCErrorDetails;

typedef enum JSONRPCType
{
	JT_OBJECT = 0,     // [ParseTable*, void*]
	JT_STRING,         // [char*]
	JT_INTEGER,        // [int]

	JT_COUNT
} JSONRPCType;

AUTO_STRUCT;
typedef struct JSONRPCState
{
	char *host;        AST(ESTRING)
	int   port;        AST(ESTRING)
	char *path;        AST(ESTRING)
	char *method;      AST(ESTRING)
	char *error;       AST(ESTRING)

	JSONRPCErrorDetails *errorDetails;

	jsonrpcFinishedCB cb;  NO_AST
	void *userData;        NO_AST
	ParseTable *result_pt; NO_AST
	void *result;          NO_AST

	// HTTP request related guts
	HttpClient *httpClient; NO_AST
	int responseCode;
	char *rawRequest;  AST(ESTRING)
	char *rawResponse; AST(ESTRING)
} JSONRPCState;

// each arg should be a JSONRPCType followed by the appropriate values listed next to the above type
// Performs a POST to http://host:port/path which calls result_pt = method([varargs]), then calls cb
JSONRPCState * jsonrpcCreate(NetComm *comm, const char *host,  int port, const char *path,
							 // TODO: Additional headers?
							 jsonrpcFinishedCB cb, void *userData,
							 ParseTable *result_pt, const char *method, int argCount, ...);

void jsonrpcDestroy(JSONRPCState *state);
bool jsonrpcIsActive(JSONRPCState *state); // returns true if it is in the process of communicating with a remote server
void jsonrpcSetConsumerPartner(const char *key, const char *secret);
