#include "gslSocialUtils.h"
#include "HttpClient.h"
#include "url.h"
#include "EString.h"
#include "earray.h"
#include "utils.h"
#include "net.h"
#include "rand.h"
#include "Entity.h"
#include "Player.h"
#include "gslSendToClient.h"
#include "Organization.h"

char *suXmlParse(const char *data, const char *tag)
{
	char *val=NULL, *start, *end;
	start = strstri(data, STACK_SPRINTF("<%s>", tag));
	if(!start)
		return NULL;
	start += strlen(tag) + 2;
	end = strstri(data, STACK_SPRINTF("</%s>", tag));
	if(!end)
		return NULL;
	estrConcat(&val, start, end-start);
	return val;
}

char *suXmlParseAttr(const char *data, const char *tag, const char *attr)
{
	char *val=NULL, *start, *end;
	start = strstri(data, STACK_SPRINTF("<%s ", tag));
	if(!start)
		return NULL;
	start += strlen(tag) + 2;
	start = strstri(start, STACK_SPRINTF("%s=\"", attr));
	if(!start)
		return NULL;
	start += strlen(attr) + 2;
	end = strchr(start, '"');
	if(!end)
		return NULL;
	estrConcat(&val, start, end-start);
	return val;
}

// Debugging callback for suRequest to just print the response
void suPrintCB(Entity *ent, const char *response, int response_code, void *userdata)
{
	if(ent && ent->pPlayer && ent->pPlayer->accessLevel >= ACCESS_GM)
		gslSendPrintf(ent, "HTTP %i\n%s\n\n", response_code, response);
}

// HTTP request utilities
typedef struct suCallbackData
{
	char *request;
	EntityRef entref;
	suCallback cb;
	suTimeout timeout_cb;
	void *userdata;
} suCallbackData;

static void suRequestConnectedCallback(HttpClient *client, suCallbackData *pending)
{
	httpClientSendBytesRaw(client, pending->request, estrLength(&pending->request));
	estrDestroy(&pending->request);
}

static void suRequestRunCallback(HttpClient *client, const char *data, int len, suCallbackData *pending)
{
	Entity *ent = entFromEntityRefAnyPartition(pending->entref);
	if(ent)
	{
		if(pending->cb)
			pending->cb(ent, data, httpClientGetResponseCode(client), pending->userdata);
	}
	else
	{
		if(pending->timeout_cb)
			pending->timeout_cb(ent, pending->userdata);
		else if(pending->userdata)
			free(pending->userdata);
	}
	httpClientDestroy(&client);
	free(pending);
}

static void suRequestTimeout(HttpClient *client, suCallbackData *pending)
{
	if(pending->timeout_cb)
		pending->timeout_cb(entFromEntityRefAnyPartition(pending->entref), pending->userdata);
	else if(pending->userdata)
		free(pending->userdata);
	httpClientDestroy(&client);
	estrDestroy(&pending->request);
	free(pending);
}

void suRequest(Entity *ent, UrlArgumentList *args, suCallback cb, suTimeout timeout_cb, void *userdata)
{
	char *request=NULL;
	char *url_tmp, *host, *host_tmp, *port_tmp, *path;
	int port = 80;
	HttpClient *client;
	suCallbackData *pending;

	// Parse the URL
	url_tmp = strdup(args->pBaseURL);
	host_tmp = strchr(url_tmp, '/');
	host_tmp += 2;
	path = strchr(host_tmp, '/');
	*path = '\0';
	host = strdup(host_tmp);
	*path = '/';
	port_tmp = strchr(host, ':');
	if(port_tmp)
	{
		*port_tmp = '\0';
		port_tmp += 1;
		port = atoi(port_tmp);
	}

	// Set the MIME type to encoded form
	estrCopy2(&args->pMimeType, MIMETYPE_FORM_ENCODED);
	urlCreateHTTPRequest(&request,  ORGANIZATION_NAME_SINGLEWORD " HttpClient/suRequest", host, path, args);

	// Create the callback data
	pending = calloc(1, sizeof(suCallbackData));
	pending->cb = cb;
	pending->entref = entGetRef(ent);
	pending->request = request;
	pending->userdata = userdata;

	// Send the HTTP request
	client = httpClientConnect(host, port, suRequestConnectedCallback, NULL, suRequestRunCallback, suRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	if(client)
		httpClientSetUserData(client, pending);
	else if(timeout_cb)
		timeout_cb(ent, userdata);
	else if(userdata)
		free(userdata);

	// Cleanup
	SAFE_FREE(host);
	SAFE_FREE(url_tmp);
	urlDestroy(&args);
}