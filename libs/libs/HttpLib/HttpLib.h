#pragma once
GCC_SYSTEM

#include "url.h"
#include "AutoGen/url_h_ast.h"

typedef struct NetLink NetLink;
typedef struct Packet Packet;

#define DEFAULT_HTTPGET_PARAMS NetLink *link, HttpClientStateDefault *pClientState, int count, char **args, char **values, const char *pReferer, CookieList *pCookies
#define DEFAULT_HTTPPOST_PARAMS NetLink *link, HttpClientStateDefault *pClientState, UrlArgumentList *args, const char *pReferer, CookieList *pCookies

extern const char * DEFAULT_HTTP_CATEGORY_FILTER;

typedef enum HttpAuthenticationResult
{
	HAR_NONE = 0, // Auth disabled at the port level, didn't even try to auth
	HAR_SUCCESS,
	HAR_FAILURE,
	HAR_COUNT
} HttpAuthenticationResult;

typedef void (*HttpReadyCallback)(NetLink *link, void *pClientState, char *data, HttpAuthenticationResult result);
typedef void (*HttpXmlrpcHook)(U32 uIp, const char *pXml);

AUTO_STRUCT;
typedef struct PendingXMLRPCRequest
{
	U64 iRequestTimeClockTicks;
	int iRequestID;
	U32 iMCPID;
	int iclientID;
	F32 requestTimeout;		AST(DEFAULT(60.0))
	NetLink *pLink; NO_AST
		int iErrorAsText;
} PendingXMLRPCRequest;

// Generic httpDefaultMsgHandler
AUTO_STRUCT;
typedef struct HttpClientStateDefault
{
	char *pPostHeaderString; AST(ESTRING)
	char *pPostDataString; AST(ESTRING)
	char *pAuthUserName; AST(ESTRING)
	U32 iAuthStringCRC;
	bool bWaitingForPostData;
	U64 requestTime;
	PendingXMLRPCRequest *req;
	HttpReadyCallback readyCallback; NO_AST
	void *pLinkUserData; NO_AST
} HttpClientStateDefault;

void httpCleanupClientState(HttpClientStateDefault *pClientState);

//typedef void (*HttpPostHandler)(char *header, char *data, NetLink *link);
//typedef void (*HttpGetHandler) (char *data, NetLink *link);
typedef void (*HttpPostHandler)(NetLink *link, HttpClientStateDefault *pClientState);
typedef void (*HttpGetHandler) (char *data, NetLink *link, HttpClientStateDefault *pClientState);

void setHttpDefaultPostHandler(HttpPostHandler httpPostHandler);
void setHttpDefaultGetHandler (HttpGetHandler httpGetHandler);

void httpDefaultMsgHandler(Packet *pkt,int cmd,NetLink *link,HttpClientStateDefault *pClientState);
int httpDefaultConnectHandler(NetLink* link, HttpClientStateDefault *pClientState);
int httpDefaultDisconnectHandler(NetLink* link, HttpClientStateDefault *pClientState);

void httpCancelPendingAuthentication(HttpClientStateDefault *pClientState);
void httpEnableAuthentication(int port, const char *pProductName, const char *xmlrpcCommandCategories); // category can be NULL, or a space delimited list

//passing in urlargs, because you can always put "accesslevel=n" to request a LOWER access level for testing purposes
//(urlargs can be NULL)
int httpFindAuthenticationAccessLevel(NetLink *link, U32 iAuthStringCRC, UrlArgumentList *pUrlArgs);
char ** httpGetCommandCategories(NetLink *link); // If it returns NULL, all commands are allowed
void httpProcessAuthentications();

// Session data (requires authentication)
void * httpGetSessionStruct(NetLink *link, const char *authString, const char *key, ParseTable *pti);

// Performs standard POSTDATA gathering into the client state, then asynchronously performs auth (if enabled on the port), then call callback afterwards
void httpProcessPostDataAuthenticated(NetLink *link, HttpClientStateDefault *pClientState, Packet *pak, HttpReadyCallback readyCallback);

void httpProcessXMLRPC(NetLink *link, HttpClientStateDefault *pClientState, char *pDataString, HttpAuthenticationResult result);

// HTTP server

void httpSendBytes(NetLink *link,void *data,U32 len);
void httpSendBytesRaw(NetLink *link,void *data,U32 len);
void httpSendStr(NetLink *link,char *str);
void httpRedirect(NetLink *link, const char *pURL);					   // "302 Moved Temporarily" your basic redirect.
void httpRedirectf(NetLink *link, char *pURLFormat, ...);
void httpSendClientError(NetLink *link, const char *pError);       // "400 Bad Request"
void httpSendServerError(NetLink *link, const char *pError);       // "500 Internal Error"
void httpSendServiceUnavailable(NetLink *link, const char *pError);// "503 Service Unavailable"
void httpSendFileNotFoundError(NetLink *link, const char *pError); // "404 File Not Found"
void httpSendPermissionDeniedError(NetLink *link, const char *pError); // "403 Permission Denied"
void httpSendMethodNotAllowedError(NetLink *link, const char *pError); // "405 Method Not Allowed"
void httpSendBasicHeader(NetLink *link, size_t iTotalLength, const char *pContentType); // pContentType can be NULL
void httpSendHeader(NetLink *link, size_t iTotalLength, ...);		// Key, value, NULL-terminated
void httpSendAuthRequiredHeader(NetLink *link);
void httpSendFile(NetLink *link, const char *filename, const char *pContentType);    // DOES send HTTP header
void httpSendFileRaw(NetLink *link, const char *filename);                           // Does NOT send HTTP header
void httpSendImageFile(NetLink *link, const char *filename, const char *pImageType); // DOES send HTTP header
void httpSendZipFile(NetLink *link, const char *filename, const char *pContentType); // DOES send HTTP header, decompresses on the fly
void httpSendZipFileRaw(NetLink *link, const char *filename);                        // Does NOT send HTTP header, decompresses on the fly
void httpSendDirIndex(NetLink *link, const char *webPath, const char *path);         // Assumes path is a valid directory
void httpSendAttachment(NetLink *link, const char *name, void *data, U32 len); // sends header and data, suggests filename to client
void httpSendBytesWith404Error(NetLink *link,void *data,U32 len);
void httpSendComplete(NetLink *link);	// Initiates FIN handshake, shuts down connection gracefully
										// Warning: This will prevent the link from receiving any additional data.  Never call this as a client, ever.

// Retrieves the URL of the referring page and the cookies from the HTTP header
const char * httpFindAuthenticationUsername(HttpClientStateDefault *pClientState);
const char * httpFindAuthenticationUsernameFromAuthString(const char *pAuthString);

//pastes username and IP together
const char * httpFindAuthenticationUsernameAndIP(NetLink *link, HttpClientStateDefault *pClientState);

#define WOULD_FIT_IN_SEND_BUFFER (1 * 1024 * 1024)

typedef struct LinkState
{
	NetLink *pLink;
	FILE    *pFile;
	char *pFilename;
} LinkState;

LinkState * findLinkState(NetLink *pLink);
void cleanupLinkStates(void);
void addLinkState(NetLink *link, FILE *file);
void removeLinkState(LinkState *pLinkState);
void updateLinkStates(void);
void sendTextFileToLink(NetLink* link, const char *pFilename, const char *pSaveFilename, const char *pContentType, bool bDeleteAfterSend);
void sendFileToLink(NetLink* link, const char *pFilename, const char *pContentType, bool bZipped);


char *httpChooseSensibleContentTypeForFileName(const char *pFilename);

// Hook XML-RPC requests and responses.
HttpXmlrpcHook HookReceivedXmlrpcRequests(HttpXmlrpcHook fpHook);
HttpXmlrpcHook HookSentXmlrpcResponses(HttpXmlrpcHook fpHook);

typedef struct CmdContext CmdContext;

LATELINK;
void FlushAuthCache_DoExtraStuff(CmdContext *pContext);
