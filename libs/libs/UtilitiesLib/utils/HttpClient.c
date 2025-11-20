#include "HttpClient.h"
#include "net.h"
#include "netreceive.h"
#include "utils.h"
#include "url.h"
#include "EString.h"
#include "earray.h"
#include "textparser.h"
#include "timing_profiler.h"
#include "windefinclude.h"
#include "AutoGen/HttpClient_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

// ---------------------------------------------------------------------------------
// HttpClient code

typedef struct HttpClient
{
	NetComm *comm;
	NetLink *link;
	bool  responded;
	bool statusDone;
	bool headersDone;
	bool bodyDone;
	bool chunked;
	int remaining;
	void *pUserData;
	char *buffer;
	char *chunked_buffer;
	time_t last_packet_time;
	U32 timeout;
	char **estring;  // Used in httpClientWaitForResponseText()
	int responseCode;
	httpConnected httpConnectedCB;
	httpReceivedHeader httpReceivedHeaderCB;
	httpReceivedBody httpReceivedBodyCB;
	httpTimeout httpTimeoutCB;
} HttpClient;

static void receiveResponseData(Packet *pkt,int cmd, NetLink *link, HttpClient *pClient);
static void openResponseConnection(NetLink* link, HttpClient *pClient);
static void closeResponseConnection(NetLink* link, HttpClient *pClient);
static NetComm *httpComm = NULL;
static HttpClient **g_clients = NULL;

HttpClient * httpClientConnect(const char *location, int port, 
							   httpConnected httpConnectedCB,
							   httpReceivedHeader httpReceivedHeaderCB,
							   httpReceivedBody httpReceivedBodyCB,
							   httpTimeout httpTimeoutCB,
							   NetComm *comm, bool bWaitForConnection, U32 timeout)
{
	HttpClient *pClient = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pClient = calloc(sizeof(HttpClient), 1);
	pClient->httpConnectedCB = httpConnectedCB;
	pClient->httpReceivedHeaderCB = httpReceivedHeaderCB;
	pClient->httpReceivedBodyCB = httpReceivedBodyCB;
	pClient->httpTimeoutCB = httpTimeoutCB;

	if (comm)
	{
		pClient->comm = comm;
	}
	else
	{
		if (httpComm)
		{
			pClient->comm = httpComm;
		}
		else
		{
			pClient->comm = httpComm = commCreate(0,0);
		}
	}
	pClient->link = commConnect(pClient->comm, 
		LINKTYPE_UNSPEC, 
		LINK_RAW, 
		location, 
		port, 
		receiveResponseData, 
		openResponseConnection, 
		closeResponseConnection, 
		0);
	if(!pClient->link)
	{
		httpClientDestroy(&pClient);
		PERFINFO_AUTO_STOP();
		return NULL;
	}
	pClient->last_packet_time = time(NULL);
	pClient->timeout = timeout;
	linkSetUserData(pClient->link, pClient);
	eaPush(&g_clients, pClient);

	if(bWaitForConnection)
	{
		if (!linkConnectWait(&pClient->link,2.f))
		{
			httpClientDestroy(&pClient);
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}

	PERFINFO_AUTO_STOP();
	return pClient;
}

void httpClientDestroy(HttpClient **ppClient)
{
	HttpClient *pClient = *ppClient;

	if(pClient == NULL)
		return;

	PERFINFO_AUTO_START_FUNC();

	*ppClient = NULL;

	if(pClient->link)
	{
		linkSetUserData(pClient->link, NULL);
		linkRemove_wReason(&pClient->link, "httpClientDstroy");
	}
	eaFindAndRemoveFast(&g_clients, pClient);

	estrDestroy(&pClient->buffer);
	estrDestroy(&pClient->chunked_buffer);
	free(pClient);
	PERFINFO_AUTO_STOP();
}

void* httpClientGetUserData(HttpClient *pClient)
{
	return pClient->pUserData;
}

void httpClientSetUserData(HttpClient *pClient, void *userData)
{
	pClient->pUserData = userData;
}

static void receiveResponseData(Packet *pkt,int cmd, NetLink *link, HttpClient *pClient)
{
	int len = pktGetSize(pkt);
	char *response = pktGetStringRaw(pkt);
	pClient->last_packet_time = time(NULL);

	estrConcat(&pClient->buffer, response, len);

	while(!pClient->bodyDone)
	{
		if(!pClient->statusDone)
		{
			// Try to parse the HTTP status line
			if(strstri(pClient->buffer, "\r\n")!=NULL)
			{
				pClient->responseCode = atoi(pClient->buffer + 9); // strlen("HTTP/1.1 ")==9
				estrRemove(&pClient->buffer, 0, strchr(pClient->buffer, '\n')-pClient->buffer+1);
				pClient->statusDone = true;
			}
			else
				break;

		}
		else if(!pClient->headersDone)
		{
			// Try to parse headers
			if(strStartsWith(pClient->buffer, "\r\n"))
			{
				// End of headers
				estrRemove(&pClient->buffer, 0, 2);
				pClient->headersDone = true;
			}
			else if(strstri(pClient->buffer, "\r\n")!=NULL)
			{
				char *key, *value, *end;
				key = pClient->buffer;
				value = strchr(key, ':');
				assert(value);
				*value = '\0';
				value += 1;
				while(*value == ' ' || *value == '\t')
					value += 1;
				end = strchr(value, '\r');
				*end = '\0';
				if(pClient->httpReceivedHeaderCB)
					pClient->httpReceivedHeaderCB(pClient, key, value, pClient->pUserData);
				if(stricmp(key, "Connection")==0 && stricmp(value, "Close")==0)
					pClient->remaining = -1;
				else if(stricmp(key, "Content-Length")==0 && pClient->remaining == 0)
					pClient->remaining = atoi(value);
				else if(stricmp(key, "Transfer-Encoding")==0 && stricmp(value, "Chunked")==0)
					pClient->chunked = true;
				estrRemove(&pClient->buffer, 0, (end+1)-pClient->buffer+1);
			}
			else
				break;
		}
		else
		{
			// Read the body until it is done
			if(pClient->chunked)
			{
				if(pClient->remaining == 0 || pClient->remaining == -1)
				{
					// Look for a new chunk length
					if(strstri(pClient->buffer, "\r\n")!=NULL)
					{
						pClient->remaining = strtol(pClient->buffer, NULL, 16);
						estrRemove(&pClient->buffer, 0, strchr(pClient->buffer, '\n')-pClient->buffer+1);
					}
					else
						break;
					if(pClient->remaining == 0)
					{
						// Got a 0-length chunk, we are done
						pClient->responded = true;
						pClient->bodyDone = true;
						if(pClient->httpReceivedBodyCB)
							pClient->httpReceivedBodyCB(pClient, pClient->chunked_buffer, estrLength(&pClient->chunked_buffer), pClient->pUserData);
						//linkFlushAndClose(&pClient->link, "HTTP response finished");
						break;
					}
				}
				else
				{
					// Wait for this many bytes to be available to complete the chunk
					if((int)estrLength(&pClient->buffer) >= pClient->remaining)
					{
						estrConcat(&pClient->chunked_buffer, pClient->buffer, pClient->remaining);
						estrRemove(&pClient->buffer, 0, pClient->remaining);
						pClient->remaining = 0;
					}
					else
						break;
				}

			}
			else if(pClient->remaining > 0 && (int)estrLength(&pClient->buffer) >= pClient->remaining)
			{
				pClient->responded = true;
				pClient->bodyDone = true;
				if(pClient->httpReceivedBodyCB)
					pClient->httpReceivedBodyCB(pClient, pClient->buffer, estrLength(&pClient->buffer), pClient->pUserData);
				//linkFlushAndClose(&pClient->link, "HTTP response finished");
				break;
			}
			else
				break;
		}
	}
}

static void openResponseConnection(NetLink* link, HttpClient *pClient)
{
	pClient->last_packet_time = time(NULL);
	if(pClient->httpConnectedCB)
		pClient->httpConnectedCB(pClient, pClient->pUserData);
}

static void closeResponseConnection(NetLink* link, HttpClient *pClient)
{
	if(!pClient)
		return;
	pClient->last_packet_time = time(NULL);
	if(pClient->remaining <= 0 && !pClient->chunked)
	{
		// Don't set responded if we haven't at least seen a response code
		if(pClient->responseCode)
			pClient->responded = true;
		if(pClient->headersDone)
		{
			pClient->bodyDone = true;
			if(pClient->httpReceivedBodyCB)
				pClient->httpReceivedBodyCB(pClient, pClient->buffer, estrLength(&pClient->buffer), pClient->pUserData);
			
		}
	}
}

bool httpClientResponded(HttpClient *pClient)
{
	return pClient->responded;
}

bool httpClientWaitForResponse(HttpClient *pClient)
{
	int iTimeoutCount  = 0;

	while(!pClient->responded)
	{
		commMonitor(pClient->comm);
		Sleep(10);

		if(iTimeoutCount++ > 300)
		{
			return false;
		}
	}

	return true;
}

void httpStockTextDataCB(HttpClient *pClient, const char *data, int len, void *userdata)
{
	estrConcat(pClient->estring, data, len);
}

bool httpClientWaitForResponseText(HttpClient *pClient, char **estring)
{
	httpReceivedHeader origHeaderCB = pClient->httpReceivedHeaderCB;
	httpReceivedBody origBodyCB = pClient->httpReceivedBodyCB;
	bool bRet;

	// Temporarily hijack these vars
	pClient->httpReceivedHeaderCB = NULL;
	pClient->httpReceivedBodyCB = httpStockTextDataCB;

	// Only used by this function, no need to stash
	pClient->estring = estring;
	estrClear(estring);

	bRet = httpClientWaitForResponse(pClient);

	// Put them back
	pClient->httpReceivedHeaderCB = origHeaderCB;
	pClient->httpReceivedBodyCB = origBodyCB;

	return bRet;
}

NetLink * httpClientGetLink(HttpClient *pClient)
{
	return pClient->link;
}

int httpClientGetResponseCode(HttpClient *pClient)
{
	return pClient->responseCode;
}

U32 httpClientGetTimeout(HttpClient *pClient)
{
	return pClient->timeout;
}

bool httpResponseCodeIsSuccess(int responseCode)
{
	return (responseCode >= 200) && (responseCode <= 299);
}

void httpClientSetEString(HttpClient *pClient, char **estr)
{
	pClient->estring = estr;
}

void httpClientSendBytesRaw(HttpClient *pClient,void *data,U32 len)
{
	Packet	*pak = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pak = pktCreateRaw(pClient->link);
	pktSendBytesRaw(pak,data,len);
	pktSendRaw(&pak);
	PERFINFO_AUTO_STOP();
}

int httpBasicGetText(const char *url, char **estring) // Returns response code, or zero for complete failure
{
	HttpClient *pClient   = NULL;
	char *pHTTPHeader     = NULL;
	int retCode = 0;

	char pServer[512];
	int  port = 80;
	char *pSlashLoc;
	char *pColonLoc;
	const char *pPath;

	// --------------------------------------------------
	// Do some really lame URL parsing.

	if(strStartsWith(url, "http://"))
	{
		url += (int)strlen("http://");
	}

	strncpy(pServer, url, 511);
	pServer[511] = 0;

	if(pSlashLoc = strchr(pServer, '/'))
	{
		*pSlashLoc = 0;
	}

	if(pColonLoc = strchr(pServer, ':'))
	{
		*pColonLoc = 0;
		pColonLoc++;
		port = atoi(pColonLoc);
	}

	pPath = strchr(url, '/');
	if(!pPath)
	{
		pPath = "/";
	}

	// --------------------------------------------------

	estrClear(estring);

	pClient = httpClientConnect(pServer, port, NULL, NULL, NULL, NULL, NULL, true, HTTPCLIENT_DEFAULT_TIMEOUT);
	if(!pClient)
	{
		return false;
	}

	estrStackCreate(&pHTTPHeader);

	estrPrintf(&pHTTPHeader,
		"GET %s HTTP/1.0\r\n"
		"Host: %s:%d\r\n"
		"User-Agent: Cryptic Lib\r\n"
		"\r\n",

		pPath,
		pServer,
		port);

	httpClientSendBytesRaw(pClient, pHTTPHeader,  (int)strlen(pHTTPHeader));

	if(httpClientWaitForResponseText(pClient, estring))
	{
		retCode = httpClientGetResponseCode(pClient);
	}

	estrDestroy(&pHTTPHeader);
	httpClientDestroy(&pClient);
	return retCode;
}

//---------------------------
// Cookies

void httpSendBasicHeaderPlusCookies(NetLink *link, int iTotalLength, const char *pContentType, CookieList *pList)
{
	Packet *pak = NULL;
	char length_buf[128];
	char content_buf[128];
	char cookie_buf[128];
	int i;

	pak = pktCreateRaw(link);
	sprintf(length_buf,"Content-Length: %d\r\n",iTotalLength);

	pktSendStringRaw(pak,"HTTP/1.1 200 OK\r\n");
	if(pContentType)
	{
		sprintf(content_buf,"Content-Type: %s\r\n", pContentType);
		pktSendStringRaw(pak,content_buf);
	}
	for (i=0; i<eaSize(&pList->ppCookies); i++)
	{
		sprintf(cookie_buf, "Set-Cookie: %s=%s\r\n", pList->ppCookies[i]->pName, pList->ppCookies[i]->pValue);
		pktSendStringRaw(pak, cookie_buf);
	}
	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"\r\n");
	pktSendRaw(&pak);
}

void httpSendBasicHeaderClearCookies(NetLink *link, int iTotalLength, const char *pContentType, CookieList *pList)
{
	Packet *pak = NULL;
	char length_buf[128];
	char content_buf[128];
	char cookie_buf[128];
	int i;

	pak = pktCreateRaw(link);
	sprintf(length_buf,"Content-Length: %d\r\n",iTotalLength);

	pktSendStringRaw(pak,"HTTP/1.1 200 OK\r\n");
	if(pContentType)
	{
		sprintf(content_buf,"Content-Type: %s\r\n", pContentType);
		pktSendStringRaw(pak,content_buf);
	}
	for (i=0; i<eaSize(&pList->ppCookies); i++)
	{
		// Arbitrary past expiration date = 1/1/2001
		sprintf(cookie_buf, "Set-Cookie: %s=%s; expires=Mon, 1-Jan-2001 00:00:00 GMT\r\n", pList->ppCookies[i]->pName, pList->ppCookies[i]->pValue);
		pktSendStringRaw(pak, cookie_buf);
	}
	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"\r\n");
	pktSendRaw(&pak);
}

bool hasCookie(CookieList *pList, const char *pCookieName)
{
	return findCookie(pList, pCookieName) != NULL;
}

Cookie * findCookie(CookieList *pList, const char *pCookieName)
{
	int i;
	if (!pList || !pCookieName)
		return NULL;
	for (i=0; i<eaSize(&pList->ppCookies); i++)
	{
		if (strcmpi(pList->ppCookies[i]->pName, pCookieName) == 0)
			return pList->ppCookies[i];
	}
	return NULL;
}

Cookie * removeCookie(CookieList *pList, const char *pCookieName)
{
	int i;
	if (!pList || !pCookieName)
		return NULL;
	for (i=0; i<eaSize(&pList->ppCookies); i++)
	{
		if (strcmpi(pList->ppCookies[i]->pName, pCookieName) == 0)
		{
			Cookie *cookie = pList->ppCookies[i];
			eaRemove(&pList->ppCookies, i);
			return cookie;
		}
	}
	return NULL;
}

const char *findCookieValue(CookieList *pList, const char *pCookieName)
{
	int i;
	if (!pList || !pCookieName)
		return NULL;
	for (i=0; i<eaSize(&pList->ppCookies); i++)
	{
		if (strcmpi(pList->ppCookies[i]->pName, pCookieName) == 0)
			return pList->ppCookies[i]->pValue;
	}
	return NULL;
}

const char *findCookieSafeValue(CookieList *pList, const char *pCookieName)
{
	const char *val = findCookieValue(pList, pCookieName);
	if (val)
		return val;
	return "";
}

void httpParseCookies(char *header, CookieList *pList)
{
	static const char* pCookieDelim = "Cookie: ";
	static const char* sDelimiters = ";";
	char *pCookieLine = NULL;
	char *pCookieString = NULL;
	char *pEndCookieLine = NULL;
	char *pNameValueSplit = NULL;
	char *pTemp = NULL;
	Cookie *pNewCookie;

	if (!header || !pList)
		return;

	eaClear(&pList->ppCookies);
	pCookieLine = strstr(header, pCookieDelim);
	if (pCookieLine)
	{
		pCookieLine += strlen(pCookieDelim);
		pEndCookieLine = strchr(pCookieLine, '\r');
		if (pEndCookieLine)
			*pEndCookieLine = 0;

		pCookieString = strtok_s(pCookieLine, sDelimiters, &pTemp);
		while (pCookieString)
		{
			while (*pCookieString == ' ')
				pCookieString++;
			pNameValueSplit = strchr(pCookieString, '=');
			if (pNameValueSplit)
			{
				*pNameValueSplit++ = 0;
			}
			pNewCookie = StructCreate(parse_Cookie);
			pNewCookie->pName = strdup(pCookieString);
			pNewCookie->pValue = strdup(pNameValueSplit);

			eaPush(&pList->ppCookies, pNewCookie);
			pCookieString = strtok_s(NULL, sDelimiters, &pTemp);
		}
	}
}

// Warning: Modifies header!
static const char * findReferer(char *header)
{
	char *pReferer = strstr(header, "Referer: ");
	if(pReferer)
	{
		char *tmp;
		pReferer += (int)strlen("Referer: ");
		tmp = pReferer;
		while(*tmp && (*tmp != '\r') && (*tmp != '\n'))
			tmp++;
		*tmp = 0;
	}
	else
	{
		pReferer = "";
	}

	return pReferer;
}

static const char * findAuthString(char *header)
{
	static char retAuthString[512];
	char *pOutput = retAuthString;

	char *pAuth = strstr(header, "Authorization: Basic ");
	if(pAuth)
	{
		char *tmp;
		tmp = pAuth + (int)strlen("Authorization: Basic ");
		while(*tmp && (*tmp != '\r') && (*tmp != '\n'))
		{
			*pOutput = *tmp;
			pOutput++;
			tmp++;
		}
		*pOutput = 0;
	}
	else
	{
		return NULL;
	}

	return retAuthString;
}

void httpParseHeaderAuth(char *header, char *urlType, char **pUrl, char **pReferer, CookieList *pList, char **pEncodedAuthString)
{
	size_t headerLen = strlen(header)+ 1;
	char *pHeaderCopy = malloc(headerLen);
	char *pRefererStart;
	char *pCookieStart;
	const char *pAuth = findAuthString(header);
	char *url = NULL;

	if(pAuth && pAuth[0])
	{
		estrCopy2(pEncodedAuthString, pAuth);
	}

	memcpy(pHeaderCopy, header, headerLen);
	pRefererStart = strstr(pHeaderCopy, "Referer: ");
	pCookieStart = strstr(pHeaderCopy, "Cookie: ");

	if (pRefererStart < pCookieStart)
	{
		httpParseCookies(pHeaderCopy, pList);
		if (pReferer)
			estrCopy2(pReferer, findReferer(pHeaderCopy));
	}
	else
	{
		if (pReferer)
			estrCopy2(pReferer, findReferer(pHeaderCopy));
		httpParseCookies(pHeaderCopy, pList);
	}
	url = urlFind(header, urlType);
	estrCopy2(pUrl, url ? url : "");
	free(pHeaderCopy);
}

void httpParseHeader(char *header, char *urlType, char **pUrl, char **pReferer, CookieList *pList)
{
	char *pEncodedAuthString = NULL;
	httpParseHeaderAuth(header, urlType, pUrl, pReferer, pList, &pEncodedAuthString);
	estrDestroy(&pEncodedAuthString);
}

void appendToURL(char **estr, const char *var, const char *valformat, ...)
{
	char hugeBuffer[2048];
	char *hugeEscapedBuffer = NULL;

	VA_START(args, valformat);
	vsprintf_s(hugeBuffer, 2048, valformat, args);
	VA_END();

	estrStackCreate(&hugeEscapedBuffer);

	urlEscape(hugeBuffer, &hugeEscapedBuffer, true, false);
	estrConcatf(estr, "%s=%s&", var, hugeEscapedBuffer);

	estrDestroy(&hugeEscapedBuffer);
}

void appendToURLIfNotNull(char **estr, const char *name, const char *format, char *str)
{
	if (str)
		appendToURL(estr, name, format, str);
}

void httpClientProcessTimeouts(void)
{
	int i;
	time_t now = time(NULL);
	for(i=eaSize(&g_clients)-1; i>=0; i--)
	{
		if(	g_clients[i]->timeout && 
			g_clients[i]->httpTimeoutCB && 
			!g_clients[i]->responded &&
			now - g_clients[i]->last_packet_time >= g_clients[i]->timeout)
		{
			g_clients[i]->httpTimeoutCB(g_clients[i], g_clients[i]->pUserData);
		}
	}
}

#include "AutoGen/HttpClient_h_ast.c"