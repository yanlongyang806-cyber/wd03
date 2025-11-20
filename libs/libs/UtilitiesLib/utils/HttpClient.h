// HTTP Client
#pragma once
GCC_SYSTEM

#define HTTPCLIENT_DEFAULT_TIMEOUT 300

typedef struct NetLink NetLink;
typedef struct NetComm NetComm;
typedef struct HttpClient HttpClient;

typedef void (*httpConnected)(HttpClient *client, void *userdata);
typedef void (*httpReceivedHeader)(HttpClient *client, const char *header, const char *value, void *userdata);
typedef void (*httpReceivedBody)(HttpClient *client, const char *data, int len, void *userdata);
typedef void (*httpTimeout)(HttpClient *client, void *userdata);


HttpClient * httpClientConnect(const char *location, int port, 
							   httpConnected httpConnectCB,
							   httpReceivedHeader httpReceivedHeaderCB,
							   httpReceivedBody httpReceivedBodyCB, 
							   httpTimeout httpTimeoutCB,
							   NetComm *comm, bool bWaitForConnection, U32 timeout);

void httpClientDestroy(HttpClient **ppClient);
void* httpClientGetUserData(HttpClient *pClient);
void httpClientSetUserData(HttpClient *pClient, void *userData);
void httpClientSendBytesRaw(HttpClient *pClient, void *data, U32 len);
bool httpClientWaitForResponse(HttpClient *pClient);
bool httpClientResponded(HttpClient *pClient);
bool httpClientWaitForResponseText(HttpClient *pClient, char **estring);
NetLink * httpClientGetLink(HttpClient *pClient);
int  httpClientGetResponseCode(HttpClient *pClient);
U32 httpClientGetTimeout(HttpClient *pClient);
bool httpResponseCodeIsSuccess(int responseCode);

// Stock callbacks for HttpClient's estring populating text responses, and indirect access to the HttpClient's estring ptr
void httpStockTextDataCB(HttpClient *pClient, const char *data, int len, void *userdata);
void httpClientSetEString(HttpClient *pClient, char **estr);

// HTTP "Basic" (wrappers for HTTP Client code)

int httpBasicGetText(const char *url, char **estring); // Returns response code, or zero for complete failure

// Cookies
AUTO_STRUCT;
typedef struct Cookie 
{
	char *pName;
	char *pValue;
} Cookie;

AUTO_STRUCT;
typedef struct CookieList
{
	Cookie **ppCookies;
} CookieList;


void httpSendBasicHeaderClearCookies(NetLink *link, int iTotalLength, const char *pContentType, CookieList *pList);
void httpSendBasicHeaderPlusCookies(NetLink *link, int iTotalLength, const char *pContentType, CookieList *pList);

bool hasCookie(CookieList *pList, const char *pCookieName);
Cookie * findCookie(CookieList *pList, const char *pCookieName);
const char *findCookieValue(CookieList *pList, const char *pCookieName);
const char *findCookieSafeValue(CookieList *pList, const char *pCookieName);
void httpParseCookies(char *header, CookieList* pList);
Cookie * removeCookie(CookieList *pList, const char *pCookieName);



void httpParseHeader(char *header, char *urlType, char **pUrl, char **pReferer, CookieList *pList);
void httpParseHeaderAuth(char *header, char *urlType, char **pUrl, char **pReferer, CookieList *pList, char **pEncodedAuthString);

void appendToURL(char **estr, const char *var, const char *valformat, ...);
void appendToURLIfNotNull(char **estr, const char *name, const char *format, char *str);

void httpClientProcessTimeouts(void);