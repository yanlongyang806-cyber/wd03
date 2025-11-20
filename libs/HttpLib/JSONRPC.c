#include "JSONRPC.h"
#include "JSONRPC_h_ast.h"
#include "JSONRPC_c_ast.h"

#include "error.h"
#include "HttpClient.h"
#include "net.h"
#include "oauth.h"
#include "Organization.h"
#include "textparser.h"
#include "url.h"
#include "timing.h"

#define JSONRPC_TIMEOUT_SEC (MINUTES(2) + SECONDS(30))

static char * spConsumer_key = NULL;
static char * spConsumer_secret = NULL;

void jsonrpcSetConsumerPartner(const char *key, const char *secret)
{
	if (key && *key)
		estrCopy2(&spConsumer_key, key);
	else
		estrDestroy(&spConsumer_key);

	if (secret && *secret)
		estrCopy2(&spConsumer_secret, secret);
	else
		estrDestroy(&spConsumer_secret);
}

// ---------------------------------------------------------------------------

// This makes a placeholder ParseTable that will be replaced in a dupe of ResponseTemplate's ParseTable during makeResponseTemplate
AUTO_STRUCT;
typedef struct Placeholder
{
	int a;
} Placeholder;

AUTO_STRUCT;
typedef struct ResponseTemplate
{
	Placeholder *result;
	JSONRPCErrorDetails *error;
} ResponseTemplate;

static int sResponseTemplateSize = 0;

AUTO_RUN;
void initJSONRPC(void)
{
	ParseTable *pt = parse_ResponseTemplate;
	sResponseTemplateSize = 1; // I want to count the last entry as well
	while(pt->name && *pt->name)
	{
		sResponseTemplateSize++;
		pt++;
	}
}

ParseTable* makeResponseTemplate(ParseTable *result_pt)
{
	ParseTable *ret = calloc(1, sizeof(ParseTable) * sResponseTemplateSize);
	ParseTable *walk = ret;

	memcpy(ret, parse_ResponseTemplate, sizeof(ParseTable) * sResponseTemplateSize);
	while(stricmp(walk->name, "result"))
		walk++;
	walk->subtable = result_pt;
	return ret;
}

// ---------------------------------------------------------------------------

static void jsonrpcBuildResponse(JSONRPCState *state)
{
	char *result = NULL;
	ParseTable *newtemplate = makeResponseTemplate(state->result_pt);

	ResponseTemplate *parsedResponse = ParserReadJSON(state->rawResponse, newtemplate, &result);
	if(parsedResponse)
	{
		if(parsedResponse->error)
		{
			// Claim ownership of this substruct
			state->errorDetails = parsedResponse->error;
			parsedResponse->error = NULL;

			estrCopy2(&state->error, state->errorDetails->message);
		}
		if(parsedResponse->result)
		{
			// Claim ownership of this substruct
			state->result = parsedResponse->result;
			parsedResponse->result = NULL;
		}
		StructDestroy(parse_ResponseTemplate, parsedResponse);
	}
	else
	{
		estrPrintf(&state->error, "Failed to parse response (reason: %s)",
			(result) ? result : "unknown");
	}
	estrDestroy(&result);
	free(newtemplate);
}

// ---------------------------------------------------------------------------

extern ParseTable parse_UrlArgumentList[];
#define TYPE_parse_UrlArgumentList UrlArgumentList
static void onJSONRPCClientConnect(HttpClient *client, JSONRPCState *state)
{
	char *req = NULL;
	UrlArgumentList args = {0};

	estrCopy2(&args.pMimeType, MIMETYPE_JSON);
	estrPrintf(&args.pBaseURL, "http://%s%s", state->host, state->path);
	urlAddValue(&args, state->rawRequest, "", HTTPMETHOD_JSON);
	if (spConsumer_key && spConsumer_secret)
	{
		const char *oauth_header;
		oauthSign(&args, HTTPMETHOD_POST, spConsumer_key, spConsumer_secret, NULL, NULL);
		oauth_header = urlFindValue(&args, "Authorization");
		if (devassert(*oauth_header))
		{
			char *oauth_header_copy = NULL;
			estrCopy2(&oauth_header_copy, oauth_header);
			estrReplaceOccurrences(&oauth_header_copy, "\r\n", "");
			urlRemoveValue(&args, "Authorization");
			urlAddValue(&args, "Authorization", oauth_header_copy, HTTPMETHOD_HEADER);
			estrDestroy(&oauth_header_copy);
		}
	}

	urlCreateHTTPRequest(&req, ORGANIZATION_NAME_SINGLEWORD " JSONRPC", state->host, state->path, &args);
	StructDeInit(parse_UrlArgumentList, &args);
	httpClientSendBytesRaw(client, req, estrLength(&req));

	estrDestroy(&req);
}

void onJSONRPCClientTimeout(HttpClient *client, JSONRPCState *state)
{
	estrPrintf(&state->error, "Timeout");

	if(state->httpClient)
		httpClientDestroy(&state->httpClient);

	if(state->cb)
		state->cb(state, state->userData);
	jsonrpcDestroy(state);
}

void onJSONRPCClientResponse(HttpClient *client, const char *data, int len, JSONRPCState *state)
{
	estrSetSize(&state->rawResponse, len);
	memcpy(state->rawResponse, data, len);
	state->rawResponse[len] = 0;
	state->responseCode = httpClientGetResponseCode(client);

	if(state->httpClient)
		httpClientDestroy(&state->httpClient);

	jsonrpcBuildResponse(state);

	if(state->cb)
		state->cb(state, state->userData);
	jsonrpcDestroy(state);
}

// ---------------------------------------------------------------------------

JSONRPCState * jsonrpcCreate(NetComm *comm, const char *host,  int port, const char *path,
							 // TODO: Additional headers?
							 jsonrpcFinishedCB cb, void *userData,
							 ParseTable *result_pt, const char *method, int argCount, ...)
{
	char *tempStr = NULL;
	int i;
	JSONRPCState *state = StructCreate(parse_JSONRPCState);

	estrCopy2(&state->method, method);
	state->result_pt = result_pt;
	estrCopy2(&state->host, host);
	state->port = port;
	estrCopy2(&state->path, path);
	state->cb = cb;
	state->userData = userData;

	estrPrintf(&state->rawRequest, "{\"method\":\"%s\",\"id\":\"jsonrpc\",\"params\":[", state->method);
	VA_START(args, argCount);
	for(i=0; i<argCount; i++)
	{
		int type = va_arg(args, int);
		if(i) estrConcatf(&state->rawRequest, ",");

		switch(type)
		{
		case JT_OBJECT:
			{
				ParseTable *pti = va_arg(args, void*);
				void *structptr = va_arg(args, void*);
				ParserWriteJSON(&state->rawRequest, pti, structptr, 0, 0, 0);
			}
			break;

		case JT_STRING:
			{
				char *text = va_arg(args, char*);
				estrCopyWithJSONEscaping(&tempStr, text);
				estrConcatf(&state->rawRequest, "\"%s\"", tempStr);
			}
			break;
		case JT_INTEGER:
			{
				int value = va_arg(args, int);
				estrConcatf(&state->rawRequest, "%d", value);
			}
			break;
		}
	}
	VA_END();
	estrConcatf(&state->rawRequest, "]}");

	state->httpClient = httpClientConnect(host, port, 
										  onJSONRPCClientConnect, 
										  NULL, 
										  onJSONRPCClientResponse, 
										  onJSONRPCClientTimeout, 
										  comm, 
										  false, 
										  JSONRPC_TIMEOUT_SEC);

	if (state->httpClient)
	{
		httpClientSetEString(state->httpClient, &state->rawResponse);
		httpClientSetUserData(state->httpClient, state);
	}
	else
	{
		StructDestroy(parse_JSONRPCState, state);
		state = NULL;
	}

	estrDestroy(&tempStr);
	return state;
}

void jsonrpcDestroy(JSONRPCState *state)
{
	if(state->httpClient)
		httpClientDestroy(&state->httpClient);

	if(state->result_pt && state->result)
	{
		StructDestroyVoid(state->result_pt, state->result);
		state->result = NULL;
	}

	StructDestroy(parse_JSONRPCState, state);
}

bool jsonrpcIsActive(JSONRPCState *state)
{
	return (state->httpClient != NULL);
}

#include "JSONRPC_h_ast.c"
#include "JSONRPC_c_ast.c"
