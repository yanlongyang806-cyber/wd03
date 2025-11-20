#include "httpAsync.h"
#include "HttpClient.h"
#include "url.h"
#include "EString.h"
#include "earray.h"
#include "utils.h"
#include "net.h"
#include "rand.h"
#include "Organization.h"
#include "logging.h"

// HTTP request utilities
typedef struct haCallbackData
{
	char *request;
	haCallback cb;
	haTimeout timeout_cb;
	void *userdata;
	U32 id;
	bool secure;
	bool log;
} haCallbackData;

static void haRequestConnectedCallback(HttpClient *client, haCallbackData *pending)
{
	if(pending->log)
		SERVLOG_PAIRS(LOG_HTTP, "HTTPAsyncRequest", ("requestid", "%u", pending->id) ("secure", "%d", !!pending->secure) ("sentbody", "%s", pending->request));
	httpClientSendBytesRaw(client, pending->request, estrLength(&pending->request));
	estrDestroy(&pending->request);
}

static void haRequestRunCallback(HttpClient *client, const char *data, int len, haCallbackData *pending)
{
	if(pending->log)
		SERVLOG_PAIRS(LOG_HTTP, "HTTPAsyncResponse", ("requestid", "%u", pending->id) ("secure", "%d", !!pending->secure) ("code", "%d", httpClientGetResponseCode(client)) ("receivedbody", "%s", data));
	if(pending->cb)
		pending->cb(data, len, httpClientGetResponseCode(client), pending->userdata);
	httpClientDestroy(&client);
	free(pending);
}

static void haRequestTimeoutCallback(HttpClient *client, haCallbackData *pending)
{
	if(pending->log)
		SERVLOG_PAIRS(LOG_HTTP, "HTTPAsyncTimeout", ("requestid", "%u", pending->id) ("secure", "%d", !!pending->secure));
	if(pending->timeout_cb)
		pending->timeout_cb(pending->userdata);
	httpClientDestroy(&client);
	estrDestroy(&pending->request);
	free(pending);
}

// Returns a request ID that can be used for logging, if you want
static U32 haRequestInternal(SA_PARAM_OP_VALID NetComm *comm,
							  SA_PARAM_NN_STR const char *host,
							  U32 port,
							  SA_PARAM_OP_STR const char *tunnel_host,
							  U32 tunnel_port,
							  SA_PARAM_NN_STR const char *path,
							  SA_PRE_NN_VALID SA_POST_P_FREE UrlArgumentList **argsIn,
							  SA_PARAM_NN_VALID haCallback cb,
							  SA_PARAM_NN_VALID haTimeout timeout_cb,
							  int timeout,
							  SA_PARAM_OP_VALID void *userdata,
							  bool bLogRequest)
{
	static U32 request_id = 0;
	UrlArgumentList *args = *argsIn;
	haCallbackData *pending = NULL;
	HttpClient *client = NULL;
	char *request = NULL;

	// Set the MIME type to encoded form
	estrCopy2(&args->pMimeType, MIMETYPE_FORM_ENCODED);
	urlCreateHTTPRequest(&request,  ORGANIZATION_NAME_SINGLEWORD " HttpClient/haRequest", host, path, args);

	// Create the callback data
	pending = calloc(1, sizeof(haCallbackData));
	pending->cb = cb;
	pending->timeout_cb = timeout_cb;
	pending->request = request;
	pending->userdata = userdata;
	pending->id = ++request_id;
	pending->secure = tunnel_host ? 1 : 0;
	pending->log = bLogRequest;

	// Send the HTTP request
	client = httpClientConnect(tunnel_host ? tunnel_host : host,
								tunnel_host ? tunnel_port : port,
								haRequestConnectedCallback,
								NULL,
								haRequestRunCallback,
								haRequestTimeoutCallback,
								comm ? comm : commDefault(),
								false,
								timeout ? timeout : HTTPCLIENT_DEFAULT_TIMEOUT);
	if(client)
		httpClientSetUserData(client, pending);
	else if(timeout_cb)
	{
		pending->id = 0;
		timeout_cb(userdata);
	}

	urlDestroy(argsIn);
	return pending->id;
}

// Sets protocol, host, path to be EStrings - must be cleaned up immediately
static void haParseURLParts(SA_PARAM_NN_VALID UrlArgumentList **argsIn,
							SA_PARAM_NN_VALID char **protocol,
							SA_PARAM_NN_VALID char **host,
							SA_PARAM_NN_VALID U32 *port,
							SA_PARAM_NN_VALID char **path)
{
	char *url_tmp, *protocol_tmp, *host_tmp, *port_tmp, *path_tmp;
	UrlArgumentList *args = *argsIn;

	url_tmp = estrStackCreateFromStr(args->pBaseURL);

	// Find the first colon, and verify that it is followed by a slash - this would indicate the protocol
	if ((protocol_tmp = strchr(url_tmp, ':')) &&
		*(protocol_tmp + 1) == '/')
	{
		*protocol_tmp = '\0';
		*protocol = estrStackCreateFromStr(url_tmp);
		*protocol_tmp = ':';
	}
	else
	{
		*protocol = estrStackCreateFromStr("http");
	}
	
	// Find the first slash, iterate until the next non-slash - this is the first character of the host
	if (host_tmp = strchr(url_tmp, '/'))
	{
		while (*(++host_tmp) == '/');
	}
	else
	{
		host_tmp = url_tmp;
	}

	// Find the next slash, if there is one - it delineates the end of the host:port locator
	if (path_tmp = strchr(host_tmp, '/'))
	{
		*path_tmp = '\0';
		*host = estrStackCreateFromStr(host_tmp);
		*path_tmp = '/';
		*path = estrStackCreateFromStr(path_tmp);
	}
	else
	{
		*host = estrStackCreateFromStr(host_tmp);
		*path = estrStackCreateFromStr("/");
	}
	
	// Find a colon in the host name - this would indicate a port override
	if(port_tmp = strchr(*host, ':'))
	{
		*port_tmp = '\0';
		++port_tmp;
		*port = atoi(port_tmp);
	}
	else
	{
		if(!stricmp(*protocol, "https"))
			*port = 443;
		else
			*port = 80;
	}

	estrDestroy(&url_tmp);
}

// Please see comments in httpAsync.h
U32 haSecureRequestEx(NetComm *comm,
					 const char *tunnel_host,
					 U32 tunnel_port,
					 UrlArgumentList **argsIn,
					 haCallback cb,
					 haTimeout timeout_cb,
					 int timeout,
					 void *userdata,
					 bool bLogRequest)
{
	UrlArgumentList *args = *argsIn;
	char *protocol = NULL, *host = NULL, *path = NULL;
	U32 port = 80;
	U32 id = 0;

	haParseURLParts(argsIn, &protocol, &host, &port, &path);

	if (devassert(!stricmp(protocol, "https")))
	{
		id = haRequestInternal(comm, host, port, tunnel_host, tunnel_port, path, argsIn, cb, timeout_cb, timeout, userdata, bLogRequest);
	}

	estrDestroy(&protocol);
	estrDestroy(&host);
	estrDestroy(&path);
	return id;
}

// Please see comments in httpAsync.h
U32 haRequestEx(NetComm *comm,
			   UrlArgumentList **argsIn,
			   haCallback cb,
			   haTimeout timeout_cb,
			   int timeout,
			   void *userdata,
			   bool bLogRequest)
{
	UrlArgumentList *args = *argsIn;
	char *protocol = NULL, *host = NULL, *path = NULL;
	U32 port = 80;
	U32 id = 0;

	haParseURLParts(argsIn, &protocol, &host, &port, &path);

	if (devassert(!stricmp(protocol, "http")))
	{
		id = haRequestInternal(comm, host, port, NULL, 0, path, argsIn, cb, timeout_cb, timeout, userdata, bLogRequest);
	}

	estrDestroy(&protocol);
	estrDestroy(&host);
	estrDestroy(&path);
	return id;
}
