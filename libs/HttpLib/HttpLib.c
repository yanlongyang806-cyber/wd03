#include "../../3rdparty/zlib/zlib.h"
#include "sock.h"
#include "net.h"
#include "timing.h"
#include "netreceive.h"

#include "netlink.h"
#include "netipfilter.h"
#include "file.h"
#include "estring.h"
#include "HttpLib.h"
#include "zutils.h"
#include "HttpLib_h_ast.h"
#include "HttpLib_c_ast.h"
#include "StringUtil.h"
#include "mathutil.h"
#include "crypt.h"
#include "accountnet.h"
#include "StashTable.h"
#include "XMLRPC.h"
#include "logging.h"
#include "utilitiesLib.h"
#include "autogen/AccountNet_h_ast.h"
#include "httputil.h"
#include "UTF8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

extern ParseTable parse_AccountTicket[];
#define TYPE_parse_AccountTicket AccountTicket
extern ParseTable parse_AccountTicketSigned[];
#define TYPE_parse_AccountTicketSigned AccountTicketSigned

const char * DEFAULT_HTTP_CATEGORY_FILTER = "XMLRPC";

static void DefaultXmlrpcHookCallback(U32 uIp, const char *pXml);

// User hooks for XML-RPC requests and responses
// These are set by HookReceivedXmlrpcRequests() and HookSentXmlrpcResponses().
static HttpXmlrpcHook XmlrpcRequestHook = DefaultXmlrpcHookCallback;
static HttpXmlrpcHook XmlrpcResponseHook = DefaultXmlrpcHookCallback;



// -----------------------------------------------------------------------------------

static HttpPostHandler defaultPostHandler = NULL;
static HttpGetHandler defaultGetHandler = NULL;

void setHttpDefaultPostHandler (HttpPostHandler httpPostHandler)
{
	defaultPostHandler = httpPostHandler;
}
void setHttpDefaultGetHandler (HttpGetHandler httpGetHandler)
{
	defaultGetHandler = httpGetHandler;
}

// Note: This must remain a valid HttpReadyCallback signature
static void httpDefaultReadyCallback(NetLink *link, HttpClientStateDefault *pClientState, char *data, HttpAuthenticationResult result)
{
	if(result == HAR_FAILURE)
	{
		httpSendAuthRequiredHeader(link);
	}
	else
	{
		if(estrLength(&pClientState->pPostDataString) > 0)
		{
			if (defaultPostHandler)
				defaultPostHandler(link, pClientState);
		}
		else
		{
			if (defaultGetHandler)
			{
				defaultGetHandler(data, link, pClientState);
			}
		}
	}

	estrClear(&pClientState->pPostDataString);
	estrClear(&pClientState->pPostHeaderString);
	pClientState->bWaitingForPostData = false;
}

// -----------------------------------------------------------------------------------

typedef struct HttpPerPortAuthSettings
{
	int port;
	bool enabled;
	char *productName;
	char **commandCategories;
} HttpPerPortAuthSettings;

static HttpPerPortAuthSettings ** sppHttpPerPortAuthSettings = NULL;

static HttpPerPortAuthSettings * httpFindPerPortAuthSettings(int port)
{
	int i;
	int iCount = eaSize(&sppHttpPerPortAuthSettings);
	for(i=0; i<iCount; i++)
	{
		HttpPerPortAuthSettings *pSettings = sppHttpPerPortAuthSettings[i];
		if(pSettings->port == port)
		{
			return pSettings;
		}
	}
	return NULL;
}

void httpEnableAuthentication(int port, const char *pProductName, const char *xmlrpcCommandCategories)
{
#ifndef _XBOX
	HttpPerPortAuthSettings *pSettings = httpFindPerPortAuthSettings(port);
	if(!pSettings)
	{
		pSettings = calloc(1, sizeof(HttpPerPortAuthSettings));
		eaPush(&sppHttpPerPortAuthSettings, pSettings);

		pSettings->port = port;
	}

	pSettings->enabled = true;
	estrCopy2(&pSettings->productName, pProductName);

	if(xmlrpcCommandCategories)
	{
		eaDestroyEString(&pSettings->commandCategories);
		cmdFilterAppendCategories(xmlrpcCommandCategories, &pSettings->commandCategories);
	}
#endif
}


// -----------------------------------------------------------------------------------

// Time in seconds between requests before the authenticated session times out
#define MAX_SESSION_AGE (60 * 20)

AUTO_STRUCT;
typedef struct HttpSessionStruct
{
	char *key;       AST(ESTRING)
	ParseTable *pti; NO_AST
	void *ptr;       NO_AST
} HttpSessionStruct;

AUTO_FIXUPFUNC;
TextParserResult httpsessionstruct_Fixup(HttpSessionStruct *pStruct, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if(pStruct->pti && pStruct->ptr)
		{
			StructDestroyVoid(pStruct->pti, pStruct->ptr);
			pStruct->pti = NULL;
			pStruct->ptr = NULL;
		}
	}

	return true;
}

AUTO_STRUCT;
typedef struct HttpAuthenticationInfo
{
	char *pAuthUserName; AST(ESTRING)
	U32 iAuthStringCRC;

	char *ticket;            AST(ESTRING)
	HttpSessionStruct **ppSessionStructs;
	int port;
	U32 access_level;
	U32 time_last_used;
} HttpAuthenticationInfo;

static HttpAuthenticationInfo ** sppHttpAuthenticationInfos = NULL;

typedef struct PendingAuthentication
{
	HttpClientStateDefault *pClientState;
	AccountValidator *pValidator;
	char *data;

	char *pAuthUserName; AST(ESTRING)
	U32 iAuthStringCRC;

	NetLink *link;
	U32 startTicks;
} PendingAuthentication;

static PendingAuthentication ** sppPendingAuthentications = NULL;

static void httpCleanupPendingAuthentication(PendingAuthentication *pPendingAuth)
{
	accountValidatorDestroy(pPendingAuth->pValidator);
	estrDestroy(&pPendingAuth->pAuthUserName);
	pPendingAuth->iAuthStringCRC = 0;
	estrDestroy(&pPendingAuth->data);
}

void httpCancelPendingAuthentication(HttpClientStateDefault *pClientState)
{
	int i;
	int iCount;

	iCount = eaSize(&sppPendingAuthentications);
	for(i=0; i<iCount; i++)
	{
		PendingAuthentication *pPendingAuth = sppPendingAuthentications[i];
		if(pPendingAuth->pClientState == pClientState)
		{
			httpCleanupPendingAuthentication(pPendingAuth);
			eaRemoveFast(&sppPendingAuthentications, i);
			return;
		}
	}
}

void httpCleanupClientState(HttpClientStateDefault *pClientState)
{
	if(pClientState)
	{
		httpCancelPendingAuthentication(pClientState);

		estrZeroAndDestroy(&pClientState->pPostHeaderString);
		estrDestroy(&pClientState->pPostDataString);
	}
}

const char * httpFindAuthenticationUsernameFromAuthString(const char *pAuthString)
{
	static char szDecodedText[2048] = "";

	if (pAuthString)
	{
		int iDecodedLen = decodeBase64String(pAuthString, strlen(pAuthString), SAFESTR(szDecodedText));

		if(iDecodedLen > 2) // Min length is 3, due to something like "a:b"
		{
			char *pPassword;

			szDecodedText[iDecodedLen] = 0;
			pPassword = strstr(szDecodedText, ":");
			if(pPassword)
			{
				*pPassword = 0;
				return szDecodedText;
			}
		}
	}

	return NULL;
}

const char * httpFindAuthenticationUsername(HttpClientStateDefault *pClientState)
{
	return pClientState->pAuthUserName;
}

const char * httpFindAuthenticationUsernameAndIP(NetLink *link, HttpClientStateDefault *pClientState)
{
	static char *pRetString = NULL;
	const char *pUserName = httpFindAuthenticationUsername(pClientState);
	estrPrintf(&pRetString, "%s(%s)", pUserName ? pUserName : "(unauth)", makeIpStr(linkGetIp(link)));
	return pRetString;
}


static HttpAuthenticationInfo * httpFindAuthenticationInfo(int port, U32 iAuthStringCRC)
{
	int i;
	int iCount = eaSize(&sppHttpAuthenticationInfos);

	for(i=0; i<iCount; i++)
	{
		HttpAuthenticationInfo *pAuthInfo = sppHttpAuthenticationInfos[i];
		if((iAuthStringCRC == pAuthInfo->iAuthStringCRC)
		&& (pAuthInfo->port == port))
		{
			pAuthInfo->time_last_used = timeSecondsSince2000();
			return pAuthInfo;
		}
	}

	return NULL;
}

int httpFindAuthenticationAccessLevel(NetLink *link, U32 iAuthStringCRC, UrlArgumentList *pUrlArgs)
{
	bool bIsTrustedRequest = ipfIsTrustedIp(linkGetIp(link));
	HttpPerPortAuthSettings *pSettings = httpFindPerPortAuthSettings(linkGetListenPort(link));
	int iAccessLevelFromArgs = 9;

	if (pUrlArgs)
	{
		int iTemp;
		if (urlFindInt(pUrlArgs, "accessLevel", &iTemp) == 1)
		{
			iAccessLevelFromArgs = iTemp;
		}

	}
	
	if(!pSettings || bIsTrustedRequest)
	{
		// [!pSettings]        Allows all authentication-disabled ports to have access to everything.
		// [bIsTrustedRequest] Allows all trusted IPs to have full access to everything.
		return MIN(9, iAccessLevelFromArgs);
	}
	else
	{
		HttpAuthenticationInfo *pAuthInfo = httpFindAuthenticationInfo(linkGetListenPort(link), iAuthStringCRC);
		if(pAuthInfo)
			return MIN((int)(pAuthInfo->access_level), iAccessLevelFromArgs);
	}

	return -1;
}

void * httpGetSessionStruct(NetLink *link, const char *authString, const char *key, ParseTable *pti)
{
	// This is to allow developer AccountServers to use session structs without requiring a login
	static HttpAuthenticationInfo sDevTimeAuthInfo = {0}; 

	HttpSessionStruct *pSessionStruct;
	HttpAuthenticationInfo *pAuthInfo = httpFindAuthenticationInfo(linkGetListenPort(link), cryptPasswordHashString(authString));

	if(!pAuthInfo)
	{
		if(isProductionMode())
		{
			return NULL;
		}
		else
		{
			static bool warned = false;
			if(!warned)
			{
				printf("Warning: Using static dev-time-only auth session struct. Consider logging in.\n");
				warned = true;
			}
			
			pAuthInfo = &sDevTimeAuthInfo;
		}
	}

	EARRAY_FOREACH_BEGIN(pAuthInfo->ppSessionStructs, i);
	{
		pSessionStruct = pAuthInfo->ppSessionStructs[i];
		if(!strcmp(pSessionStruct->key, key))
		{
			assertmsg(pSessionStruct->pti == pti, "HttpLib: Re-using session struct key for different ParseTables!");
			return pSessionStruct->ptr;
		}
	}
	EARRAY_FOREACH_END;

	pSessionStruct = StructCreate(parse_HttpSessionStruct);
	estrCopy2(&pSessionStruct->key, key);
	pSessionStruct->pti = pti;
	pSessionStruct->ptr = StructCreateVoid(pti);
	eaPush(&pAuthInfo->ppSessionStructs, pSessionStruct);

	return pSessionStruct->ptr;
}

char ** httpGetCommandCategories(NetLink *link)
{
	HttpPerPortAuthSettings *pSettings = httpFindPerPortAuthSettings(linkGetListenPort(link));
	if(pSettings)
	{
		return pSettings->commandCategories;
	}

	return NULL;
}

static void httpAddAuthenticationInfo(int port, const char *pAuthUserName, U32 iAuthStringCRC, const char *pAccountTicketString)
{
	HttpPerPortAuthSettings *pPerPortAuthSettings = httpFindPerPortAuthSettings(port);
	AccountTicketSigned *pSignedTicket = StructCreate(parse_AccountTicketSigned);
	AccountTicket *pTicket = StructCreate(parse_AccountTicket);

	// Create our new info
	HttpAuthenticationInfo *pAuthInfo = StructCreate(parse_HttpAuthenticationInfo);
	pAuthInfo->time_last_used = timeSecondsSince2000();
	pAuthInfo->port = port;
	pAuthInfo->iAuthStringCRC = iAuthStringCRC;
	estrCopy2(&pAuthInfo->pAuthUserName, pAuthUserName);
	estrCopy2(&pAuthInfo->ticket, pAccountTicketString);
	eaPush(&sppHttpAuthenticationInfos, pAuthInfo);

	// Now, parse our ticket and find our access level.
	pAuthInfo->access_level = -1;

	devassertmsg(pPerPortAuthSettings, "You shouldn't be able to add authentication for an http client if auth was never enabled on this port.");

	ParserReadText(pAccountTicketString, parse_AccountTicketSigned, pSignedTicket, 0);
	if (pSignedTicket->ticketText)
	{
		AccountPermissionStruct *pShardPermissions = NULL;
		AccountPermissionStruct *pAllPermissions = NULL;

		pTicket = StructCreate(parse_AccountTicket);
		ParserReadTextSafe(pSignedTicket->ticketText, pSignedTicket->strTicketTPI, pSignedTicket->uTicketCRC,
			parse_AccountTicket, pTicket, 0);

		pShardPermissions = findGamePermissionsByShard(pTicket, pPerPortAuthSettings->productName, GetShardCategoryFromShardInfoString());
		pAllPermissions = findGamePermissionsByShard(pTicket, pPerPortAuthSettings->productName, "all");

		if (pShardPermissions)
		{
			pAuthInfo->access_level = pShardPermissions->iAccessLevel;
		}
		else if (pAllPermissions)
		{
			pAuthInfo->access_level = pAllPermissions->iAccessLevel;
		}
	}

	StructDestroy(parse_AccountTicketSigned, pSignedTicket);
	StructDestroy(parse_AccountTicket, pTicket);

	log_printf(LOG_SERVERMONITOR, "Added auth info with access level %d, user name %s. Account ticket string: %s", 
		pAuthInfo->access_level, pAuthUserName, pAccountTicketString);
}

void httpProcessAuthentications()
{
	int i;
	int iCount;
	U32 uTime = timeSecondsSince2000();
	AccountValidatorResult result;

	if(eaSize(&sppHttpPerPortAuthSettings) < 1)
		return;

	PERFINFO_AUTO_START_FUNC();

	// Cleanup old Cached Auths
	iCount = eaSize(&sppHttpAuthenticationInfos);
	for(i=0; i<iCount; i++)
	{
		HttpAuthenticationInfo *pAuthInfo = sppHttpAuthenticationInfos[i];
		U32 uAge = uTime - pAuthInfo->time_last_used;
		if(uAge > MAX_SESSION_AGE)
		{
			eaRemoveFast(&sppHttpAuthenticationInfos, i);
			StructDestroy(parse_HttpAuthenticationInfo, pAuthInfo);
			iCount--;
			i--;
		}
	}

	// Process Pending Auths
	commMonitor(accountCommDefault());
	iCount = eaSize(&sppPendingAuthentications);
	for(i=0; i<iCount; i++)
	{
		bool bRemovePendingAuth = false;
		PendingAuthentication *pPendingAuth = sppPendingAuthentications[i];
#pragma warning(suppress:6001) // /analyze " Using uninitialized memory '*pPendingAuth'"
		HttpReadyCallback   readyCallback = (pPendingAuth->pClientState->readyCallback) ? pPendingAuth->pClientState->readyCallback : httpDefaultReadyCallback;
		accountValidatorTick(pPendingAuth->pValidator);

		result = accountValidatorGetResult(pPendingAuth->pValidator);
		switch(result)
		{
		xcase ACCOUNTVALIDATORRESULT_STILL_PROCESSING:
			{
				// TODO: Timeout code
			}
		xcase ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT:
		case ACCOUNTVALIDATORRESULT_FAILED_GENERIC:
		case ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_FAILED:
			{
				HttpRequest *pRequest = NULL;
				const char *pUserAgent = NULL;
				char *pUserAgentSnippet = NULL;

				PERFINFO_AUTO_START("FailureLog", 1);
				if (pPendingAuth->data)
				{
					pRequest = hrParse(pPendingAuth->data);

					if (pRequest)
					{
						pUserAgent = hrFindHeader(pRequest, "user-agent");

						if (pUserAgent)
						{
							estrAppendEscaped(&pUserAgentSnippet, pUserAgent);
							estrInsertf(&pUserAgentSnippet, 0, " UserAgent \"");
							estrConcatf(&pUserAgentSnippet, "\"");
						}
					}
				}

			
				servLog(LOG_LOGIN, "HttpAuthFailure", "Username %s IP %s Reason %s%s",
					pPendingAuth->pAuthUserName, 
					pPendingAuth->link ? makeIpStr(linkGetIp(pPendingAuth->link)) : "(UNKNOWN)",
					StaticDefineInt_FastIntToString( AccountValidatorResultEnum, result),
					pUserAgentSnippet ? pUserAgentSnippet : "");

				estrDestroy(&pUserAgentSnippet);
				StructDestroySafe(parse_HttpRequest, &pRequest);
				PERFINFO_AUTO_STOP();

				PERFINFO_AUTO_START("Failure Callback", 1);
				readyCallback(pPendingAuth->link, pPendingAuth->pClientState, pPendingAuth->data, HAR_FAILURE);
				PERFINFO_AUTO_STOP();
				estrDestroy(&pPendingAuth->pClientState->pAuthUserName);
				pPendingAuth->pClientState->iAuthStringCRC = 0;
				bRemovePendingAuth = true;
			}
		xcase ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_SUCCESS:
			{
				char *ticket = NULL;
				U32 ms_taken = GetTickCount() - pPendingAuth->startTicks;

				accountValidatorGetTicket(pPendingAuth->pValidator, &ticket);
				if (linkConnected(pPendingAuth->link))
					httpAddAuthenticationInfo(linkGetListenPort(pPendingAuth->link), pPendingAuth->pAuthUserName, pPendingAuth->iAuthStringCRC, ticket);
				estrDestroy(&ticket);

				estrCopy(&pPendingAuth->pClientState->pAuthUserName, &pPendingAuth->pAuthUserName);
				pPendingAuth->pClientState->iAuthStringCRC = pPendingAuth->iAuthStringCRC;

				PERFINFO_AUTO_START("Success Callback", 1);
				readyCallback(pPendingAuth->link, pPendingAuth->pClientState, pPendingAuth->data, HAR_SUCCESS);
				PERFINFO_AUTO_STOP();
				bRemovePendingAuth = true;
			}
		};

		if(bRemovePendingAuth)
		{
			httpCleanupPendingAuthentication(pPendingAuth);
			eaRemoveFast(&sppPendingAuthentications, i);
			free(pPendingAuth);
			iCount--;
			i--;
		}
	}
	PERFINFO_AUTO_STOP();
}

void httpAuthenticateClientAsync(NetLink *link, HttpClientStateDefault *pClientState, const char *data, const char *pEncodedAuthString, const char *pLogin, const char *pPassword)
{
	PendingAuthentication *pPendingAuth = NULL;

	PERFINFO_AUTO_START_FUNC();
	pPendingAuth = calloc(1, sizeof(PendingAuthentication));
	pPendingAuth->link = link;
	pPendingAuth->pClientState = pClientState;
	estrCopy2(&pPendingAuth->data, data);
	pPendingAuth->iAuthStringCRC = cryptPasswordHashString(pEncodedAuthString);
	estrCopy2(&pPendingAuth->pAuthUserName, httpFindAuthenticationUsernameFromAuthString(pEncodedAuthString));
	pPendingAuth->pValidator = accountValidatorCreate();
	pPendingAuth->startTicks = GetTickCount();
	eaPush(&sppPendingAuthentications, pPendingAuth);

	accountValidatorRequestTicket(pPendingAuth->pValidator, pLogin, pPassword);
	PERFINFO_AUTO_STOP();
}



void httpDefaultMsgHandler(Packet *pkt,int cmd,NetLink *link,HttpClientStateDefault *pClientState)
{
	httpProcessPostDataAuthenticated(link, pClientState, pkt, httpDefaultReadyCallback);
}

static const char * findAuthString(char *header)
{
	static char retAuthString[2048] = {0};
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

void httpProcessPostDataAuthenticated(NetLink *link, HttpClientStateDefault *pClientState, Packet *pak, HttpReadyCallback readyCallback)
{
	int port = linkGetListenPort(link);
	int rawDataRemaining = linkGetRawDataRemaining(link);
	HttpPerPortAuthSettings *pPerPortAuthSettings = NULL;
	bool bIsTrustedRequest;
	bool bAuthRequired = false;
	bool bIsPostData = false;
	char *data = pktGetStringRaw(pak);

	// -----------------------------------------------------------------------------------
	// Process POST Data
	if(!pClientState)
		return;

	PERFINFO_AUTO_START_FUNC();

	if(pClientState->bWaitingForPostData)
	{
		estrConcat(&pClientState->pPostDataString, data, pktGetSize(pak));
		if(rawDataRemaining == 0)
		{
			pClientState->bWaitingForPostData = false;
			bIsPostData = true;

			data = pClientState->pPostDataString; // When we finish a POST, send the actual data to the ready callback, not this incomplete string
		}
		else
		{
			// We don't have all of the POST data yet. Wait for it!
			PERFINFO_AUTO_STOP();
			return;
		}
	}
	else if(rawDataRemaining > 0)
	{
		// New POST coming in. Stash off the raw header, and then wait for more data.
		estrZeroAndClear(&pClientState->pPostHeaderString);
		estrConcat(&pClientState->pPostHeaderString, data, pktGetSize(pak));
		estrClear(&pClientState->pPostDataString);
		pClientState->bWaitingForPostData = true;
		PERFINFO_AUTO_STOP();
		return;
	}

	// -----------------------------------------------------------------------------------
	// Process Authentication (if enabled)

	pClientState->requestTime = timerCpuTicks64();
	pPerPortAuthSettings = httpFindPerPortAuthSettings(port);
	bIsTrustedRequest = ipfIsTrustedIp(linkGetIp(link));

	if(pPerPortAuthSettings && pPerPortAuthSettings->enabled)
	{
		bAuthRequired = !bIsTrustedRequest;
		
		// Add reasons to disable authentication for this request here
	}

	if(bAuthRequired)
	{
		const char *pAuthString = NULL;
		bool bAuthProvided = false;

		if(bIsPostData)
			pAuthString = findAuthString(pClientState->pPostHeaderString);
		else
			pAuthString = findAuthString(data);

		if(pAuthString)
		{
			char szDecodedText[2048] = {0};
			int iDecodedLen;

			if(httpFindAuthenticationInfo(port, cryptPasswordHashString(pAuthString)))
			{
				bAuthProvided = true;
				estrCopy2(&pClientState->pAuthUserName, httpFindAuthenticationUsernameFromAuthString(pAuthString));
				pClientState->iAuthStringCRC = cryptPasswordHashString(pAuthString);
				PERFINFO_AUTO_START("Callback", 1);
				readyCallback(link, pClientState, data, HAR_SUCCESS);
				PERFINFO_AUTO_STOP();
			}
			else
			{
				iDecodedLen = decodeBase64String(pAuthString, strlen(pAuthString), SAFESTR(szDecodedText));
				if(iDecodedLen > 2) // Min length is 3, due to something like "a:b"
				{
					char *pLogin = szDecodedText;
					char *pPassword;

					szDecodedText[iDecodedLen] = 0;
					pPassword = strstr(szDecodedText, ":");
					if(pPassword)
					{
						*pPassword = 0;
						pPassword++;

						bAuthProvided = true;
						pClientState->readyCallback = readyCallback;
						httpAuthenticateClientAsync(link, pClientState, data, pAuthString, pLogin, pPassword);
					}
					memset(szDecodedText, 0, iDecodedLen);
				}
			}

			memset((char*)pAuthString, ' ', strlen(pAuthString));
		}
		
		if(!bAuthProvided)
		{
			PERFINFO_AUTO_START("Auth failed", 1);
			readyCallback(link, pClientState, data, HAR_FAILURE);
			PERFINFO_AUTO_STOP();
			estrDestroy(&pClientState->pAuthUserName);
			pClientState->iAuthStringCRC = 0;
		}

		PERFINFO_AUTO_STOP();
		return; // Don't respond if we are waiting on authentication, our pending auth queue will fire the callbacks
	}

	readyCallback(link, pClientState, data, HAR_NONE);
	PERFINFO_AUTO_STOP();
}

int httpDefaultConnectHandler(NetLink* link, HttpClientStateDefault *pClientState)
{
	// This makes it so our flow control in updateLinkStates() doesn't immediately break
	linkSetTimeout(link, 0);
	return 0;
}

int httpDefaultDisconnectHandler(NetLink* link, HttpClientStateDefault *pClientState)
{
	httpCleanupClientState(pClientState);
	return 0;
}





void httpSendBytes(NetLink *link,void *data,U32 len)
{
	Packet	*pak = pktCreateRaw(link);
	char	length_buf[100];

	PERFINFO_AUTO_START_FUNC();
	sprintf(length_buf,"Content-Length: %d\r\n",len);

	pktSendStringRaw(pak,"HTTP/1.1 200 OK\r\n");
	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"\r\n");
	pktSendBytesRaw(pak,data,len);
	pktSendRaw(&pak);

	httpSendComplete(link);
	PERFINFO_AUTO_STOP();
}


void httpSendBytesWith404Error(NetLink *link,void *data,U32 len)
{
	Packet	*pak = pktCreateRaw(link);
	char	length_buf[100];

	PERFINFO_AUTO_START_FUNC();
	sprintf(length_buf,"Content-Length: %d\r\n",len);

    pktSendStringRaw(pak,"HTTP/1.1 404 Not Found\r\n");
	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"\r\n");
	pktSendBytesRaw(pak,data,len);
	pktSendRaw(&pak);

	httpSendComplete(link);
	PERFINFO_AUTO_STOP();
}

// Initiates FIN handshake, shuts down connection gracefully
// Warning: This will prevent the link from receiving any additional data.  Never call this as a client, ever.
void httpSendComplete(NetLink *link)
{
	// this initiates a FIN handshake, which makes older HTTP clients behave properly
	// instead of waiting forever for the server to drop the connection (even though
	// it has received its entire Content-Length worth of data).
	linkShutdown(&link);
}

void httpSendBytesRaw(NetLink *link,void *data,U32 len)
{
	Packet	*pak = pktCreateRaw(link);
	pktSendBytesRaw(pak,data,len);
	pktSendRaw(&pak);
}

void httpSendStr(NetLink *link,char *str)
{
	httpSendBytes(link,str,(U32)strlen(str));
}

void httpRedirect(NetLink *link, const char *pURL)
{
	Packet	*pak = pktCreateRaw(link);

	pktSendStringRaw(pak,"HTTP/1.1 302 Moved Temporarily\r\n");

	pktSendStringRaw(pak,"Content-Length: 0\r\n");
	pktSendStringRaw(pak,"Location: ");
	pktSendStringRaw(pak,pURL);
	pktSendStringRaw(pak,"\r\n");

	pktSendStringRaw(pak,"\r\n");
	pktSendRaw(&pak);

	httpSendComplete(link);
}

void httpRedirectf(NetLink *link, char *pURLFormat, ...)
{
	char *pFullURL = NULL;

	va_list ap;

	va_start(ap, pURLFormat);
		estrConcatfv(&pFullURL, pURLFormat, ap);
	va_end(ap);

	httpRedirect(link, pFullURL);

	estrDestroy(&pFullURL);
}

static void httpSendError(NetLink *link, const char *pError, const char *pHeader)
{
	Packet *pak = NULL;
	char length_buf[100];

	PERFINFO_AUTO_START_FUNC();
	pak = pktCreateRaw(link);

	sprintf(length_buf,"Content-Length: %d\r\n",strlen(pError));

	pktSendStringRaw(pak, (char*)pHeader);
	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"\r\n");
	pktSendStringRaw(pak,(char*)pError);
	pktSendRaw(&pak);

	httpSendComplete(link);
	PERFINFO_AUTO_STOP();
}

void httpSendClientError(NetLink *link, const char *pError)
{
	httpSendError(link, pError, "HTTP/1.1 400 Bad Request\r\n");
}

void httpSendServerError(NetLink *link, const char *pError)
{
	httpSendError(link, pError, "HTTP/1.1 500 Internal Error\r\n");
}

void httpSendServiceUnavailable(NetLink *link, const char *pError)
{
	httpSendError(link, pError, "HTTP/1.1 503 Service Unavailable\r\n");
}

void httpSendFileNotFoundError(NetLink *link, const char *pError)
{
	httpSendError(link, pError, "HTTP/1.1 404 File Not Found\r\n");
}

void httpSendPermissionDeniedError(NetLink *link, const char *pError)
{
	httpSendError(link, pError, "HTTP/1.1 403 Permission Denied\r\n");
}

void httpSendMethodNotAllowedError(NetLink *link, const char *pError)
{
	httpSendError(link, pError, "HTTP/1.1 405 Method Not Allowed\r\n");
}

void httpSendAuthRequiredHeader(NetLink *link)
{
	Packet *pak = NULL;
	char length_buf[128];
	const char *pRealm = "Cryptic Unknown";

	char *pUnauthorizedString1 = "Cryptic - Insufficient Access Level to [";
	char *pUnauthorizedString2 = "]. Please contact NetOps if this is unexpected.";
	int iUnauthorizedLen;

	HttpPerPortAuthSettings *pSettings = httpFindPerPortAuthSettings(linkGetListenPort(link));
	
	if(pSettings && pSettings->productName && *pSettings->productName)
		pRealm = pSettings->productName;		
	
	iUnauthorizedLen = (int)strlen(pUnauthorizedString1)
					 + (int)strlen(pUnauthorizedString2)
					 + (int)strlen(pRealm);

	pak = pktCreateRaw(link);

	sprintf(length_buf,"Content-Length: %d\r\n",iUnauthorizedLen);

	pktSendStringRaw(pak,"HTTP/1.1 401 Unauthorized\r\n");
	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"WWW-Authenticate: Basic realm=\"");
	pktSendStringRaw(pak,pRealm);
	pktSendStringRaw(pak,"\"\r\n");
	pktSendStringRaw(pak,"\r\n");
	pktSendStringRaw(pak,pUnauthorizedString1);
	pktSendStringRaw(pak,pRealm);
	pktSendStringRaw(pak,pUnauthorizedString2);
	pktSendRaw(&pak);

	httpSendComplete(link);
}

void httpSendBasicHeader(NetLink *link, size_t iTotalLength, const char *pContentType)
{
	if (pContentType)
		httpSendHeader(link, iTotalLength, "Content-Type", pContentType, NULL);
	else
		httpSendHeader(link, iTotalLength, NULL);
}

void httpSendHeader(NetLink *link, size_t iTotalLength, ...)
{
	Packet *pak = NULL;
	char *headers = NULL;
	va_list args;
	const char *header;

	PERFINFO_AUTO_START_FUNC();

	// Write HTTP status.
	pak = pktCreateRaw(link);
	pktSendStringRaw(pak,"HTTP/1.1 200 OK\r\n");

	// Start headers with Content-Length.
	estrStackCreate(&headers);
	estrConcatf(&headers, "Content-Length: %u\r\n", iTotalLength);

	// Add additional headers.
	va_start(args, iTotalLength);
	for (header = va_arg(args, const char *); header; header = va_arg(args, const char *))
	{
		const char *value = va_arg(args, const char *);
		estrConcatf(&headers, "%s: %s\r\n", header, value);
	}
	va_end(args);

	// Send headers.
	pktSendStringRaw(pak, headers);
	estrDestroy(&headers);
	pktSendStringRaw(pak, "\r\n");
	pktSendRaw(&pak);
	PERFINFO_AUTO_STOP();
}

void httpSendAttachmentHeader(NetLink *link, const char *pSaveFilename, int iTotalLength, const char *pContentType)
{
	Packet *pak = NULL;
	char length_buf[128];
	char content_buf[128];

	pak = pktCreateRaw(link);
	sprintf(length_buf,"Content-Length: %d\r\n",iTotalLength);

	pktSendStringRaw(pak,"HTTP/1.1 200 OK\r\n");
	if(pContentType)
	{
		sprintf(content_buf,"Content-Type: %s\r\n", pContentType);
		pktSendStringRaw(pak,content_buf);
	}
	sprintf(content_buf,"Content-Disposition: attachment%s%s\r\n", 
		pSaveFilename ? "; filename=" : "", pSaveFilename ? pSaveFilename : "");
	pktSendStringRaw(pak, content_buf);

	pktSendStringRaw(pak,length_buf);
	pktSendStringRaw(pak,"\r\n");
	pktSendRaw(&pak);
}

void httpSendAttachment(NetLink *link, const char *name, void *data, U32 len)
{
	char buf[16];
	Packet *pak = pktCreateRaw(link);
	pktSendStringRaw(pak,"HTTP/1.1 200 OK\r\n");

	pktSendStringRaw(pak, "Content-Type: application/octet-stream\r\n");

	pktSendStringRaw(pak, "Content-Disposition: attachment; filename=\"");
	pktSendStringRaw(pak, name);
	pktSendStringRaw(pak, "\"\r\n");

	pktSendStringRaw(pak, "Content-Length: ");
	sprintf(buf, "%d", len);
	pktSendStringRaw(pak, buf);
	pktSendStringRaw(pak, "\r\n");

	pktSendStringRaw(pak, "\r\n");
	pktSendBytesRaw(pak, data, len);
	pktSendRaw(&pak);

	httpSendComplete(link);
}

void httpSendFileRaw(NetLink *link, const char *filename)
{
	Packet *pak = NULL;
	unsigned char read_buffer[1024];
	size_t iBytesRead;
	FILE *pFile;

	pFile = fileOpen(filename, "rb");

	if(pFile)
	{
		while( (iBytesRead = fread(read_buffer, 1, 1024, pFile)) > 0 )
		{
			pak = pktCreateRaw(link);
			pktSendBytesRaw(pak,read_buffer, (U32)iBytesRead);
			pktSendRaw(&pak);
		}

		fclose(pFile);
	}
}

void httpSendFile(NetLink *link, const char *filename, const char *pContentType)
{
	Packet *pak = NULL;
	int len = -1;
	bool bFileExists = fileExists(filename);


	if(!bFileExists)
	{
		httpSendFileNotFoundError(link, filename);
		return;
	}

	if (!pContentType)
	{
		pContentType = httpChooseSensibleContentTypeForFileName(filename);
	}

	len = fileSize(filename);
	httpSendBasicHeader(link, len, pContentType);
	httpSendFileRaw(link, filename);
}

void httpSendZipFileRaw(NetLink *link, const char *filename)
{
	Packet *pak = NULL;
	unsigned char read_buffer[1024];
	U32 iBytesRead;
	FILE *pFile;

	pFile = fileOpen(filename, "rbz");

	if(pFile)
	{
		while( (iBytesRead = (U32)fread(read_buffer, 1, 1024, pFile)) > 0 )
		{
			pak = pktCreateRaw(link);
			pktSendBytesRaw(pak,read_buffer,iBytesRead);
			pktSendRaw(&pak);
		}

		fclose(pFile);
	}
}

void httpSendImageFile(NetLink *link, const char *filename, const char *pImageType)
{
	FILE *pFile;

	pFile = fileOpen(filename, "rb");
	if (pFile)
	{
		U32 iSize = fileGetSize(pFile);
		char *buffer = malloc(iSize);
		fread(buffer, sizeof(char), iSize, pFile);
		fclose(pFile);

		httpSendBasicHeader(link, iSize, STACK_SPRINTF("image/%s", pImageType));
		httpSendBytesRaw(link, buffer, iSize);
		free(buffer);
	}
}

void httpSendZipFile(NetLink *link, const char *filename, const char *pContentType)
{
	Packet *pak = NULL;
	size_t len = 0;
	bool bFileExists = fileExists(filename);

	if(!bFileExists)
	{
		httpSendFileNotFoundError(link, filename);
		return;
	}

	len = calcZipFileSize(filename);
	httpSendBasicHeader(link, len, pContentType);
	httpSendZipFileRaw(link, filename);
}

void httpSendDirIndex(NetLink *link, const char *webPath, const char *path)
{
	char szPath[MAX_PATH];
	char *s = NULL;
	WIN32_FIND_DATAA wfd;
	HANDLE h;

	estrStackCreate(&s);

	estrPrintf(&s, "Directory Listing: %s<br>\n<hr size=1><br>\n", webPath);
#if !_PS3
	strcpy(szPath, path);
	strcat(szPath, "/*.*");
	h = FindFirstFile_UTF8(szPath, &wfd);
	if(h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				estrConcatf(&s, "* <a href=\"%s\">%s</a><br>\n", wfd.cFileName, wfd.cFileName);

			}
		}
		while(FindNextFile_UTF8(h, &wfd));
		FindClose(h);
	}
#endif
	estrConcatf(&s, "<br>\n<hr size=1>\n");

	httpSendBasicHeader(link, estrLength(&s), "text/html");
	httpSendBytesRaw(link, s, estrLength(&s));

	estrDestroy(&s);
}

// -------------------------------------------------------------------------------------
// File sending

static LinkState **ppLinkStates = NULL;

LinkState * findLinkState(NetLink *pLink)
{
	int i;
	for(i=0; i<eaSize(&ppLinkStates); i++)
	{
		if(ppLinkStates[i]->pLink == pLink)
		{
			return ppLinkStates[i];
		}
	}

	return NULL;
}

void addLinkState(NetLink *link, FileWrapper *file)
{
	LinkState *pLinkState = calloc(sizeof(LinkState), 1);
	pLinkState->pLink = link;
	pLinkState->pFile = file;
	eaPush(&ppLinkStates, pLinkState);
	servLog(LOG_DUMPDATA, "LinkStateAdded", "Sending file %s to link %d", file->nameptr, linkID(link));
}

void removeLinkState(LinkState *pLinkState)
{
	if (pLinkState->pFile)
	{
		fclose(pLinkState->pFile);
		if (pLinkState->pFilename)
		{
			fileForceRemove(pLinkState->pFilename);
			free(pLinkState->pFilename);
		}
	}
	eaFindAndRemoveFast(&ppLinkStates, pLinkState);
}

void cleanupLinkStates(void)
{
	FOR_EACH_IN_EARRAY(ppLinkStates, LinkState, pLinkState)
	{
		servLog(LOG_DUMPDATA, "LinkStateRemoved", "Cleaning up file %s to link %d", pLinkState->pFile->nameptr, linkID(pLinkState->pLink));
		removeLinkState(pLinkState);
		linkRemove(&pLinkState->pLink);
		free(pLinkState);
	}
	FOR_EACH_END;
}

void updateLinkStates(void)
{
	Packet *pak;
	unsigned char read_buffer[1024];
	U32 iBytesRead;

	PERFINFO_AUTO_START_FUNC();

	// Go through all file sends and update the links
	FOR_EACH_IN_EARRAY(ppLinkStates, LinkState, pLinkState)
	{
		if(linkSendBufWasFull(pLinkState->pLink))
		{
			linkClearSendBufWasFull(pLinkState->pLink);
			continue;
		}

		while( (iBytesRead = (U32)fread(read_buffer, 1, 1024, pLinkState->pFile)) > 0 )
		{
			pak = pktCreateRaw(pLinkState->pLink);
			pktSendBytesRaw(pak,read_buffer,iBytesRead);
			pktSendRaw(&pak);

			if(linkSendBufWasFull(pLinkState->pLink))
			{
				linkClearSendBufWasFull(pLinkState->pLink);
				break;
			}
		}

		if(iBytesRead == 0)
		{
			// We left the previous while loop because the file is empty. Cleanup!
			printf("File transfer complete to link 0x%p\n", pLinkState->pLink);
			servLog(LOG_DUMPDATA, "LinkStateRemoved", "Finished sending file %s to link %d", pLinkState->pFile->nameptr, linkID(pLinkState->pLink));
			removeLinkState(pLinkState);
			free(pLinkState);
		}
	}
	FOR_EACH_END;
	PERFINFO_AUTO_STOP();
}

void sendTextFileToLink(NetLink* link, const char *pFilename, const char *pSaveFilename, const char *pContentType, bool bDeleteAfterSend)
{
	int len;
	FILE *pFile;
	LinkState *pLinkState;

	bool bFileExists = fileExists(pFilename);
	if(!bFileExists)
	{
		httpSendFileNotFoundError(link, pFilename);
		return;
	}

	len = fileSize(pFilename);

	httpSendAttachmentHeader(link, pSaveFilename, len, pContentType);

	if(len < WOULD_FIT_IN_SEND_BUFFER)
	{
		// Just send it immediately
		printf("Immediately sending text file %s to link 0x%p\n", pFilename, link);
		httpSendFileRaw(link, pFilename);
		if (bDeleteAfterSend)
		{
			fileForceRemove(pFilename);
		}
		return;
	}
	
	pFile = fileOpen(pFilename, "rb");

	if(!pFile)
	{
		httpSendFileNotFoundError(link, pFilename);
		return;
	}

	// Add a new LinkState for this
	printf("Sending text file %s to link 0x%p\n", pFilename, link);

	pLinkState = calloc(sizeof(LinkState), 1);
	pLinkState->pLink = link;
	pLinkState->pFile = pFile;
	if (bDeleteAfterSend)
		pLinkState->pFilename = strdup(pFilename);
	eaPush(&ppLinkStates, pLinkState);
	servLog(LOG_DUMPDATA, "LinkStateAdded", "Sending text file %s to link %d", pFile->nameptr, linkID(link));
}

void sendFileToLink(NetLink* link, const char *pFilename, const char *pContentType, bool bZipped)
{
	size_t len;
	FILE *pFile;

	bool bFileExists = fileExists(pFilename);
	if(!bFileExists)
	{
		httpSendFileNotFoundError(link, pFilename);
		return;
	}

	if(bZipped)
	{
		len = calcZipFileSize(pFilename);
	}
	else
	{
		len = fileSize(pFilename);
	}

	httpSendBasicHeader(link, len, pContentType);
	
	if(len < WOULD_FIT_IN_SEND_BUFFER)
	{
		// Just send it immediately
		//printf("Immediately sending file %s to link 0x%p\n", pFilename, link);
		httpSendFileRaw(link, pFilename);
		return;
	}
	
	pFile = fileOpen(pFilename, (bZipped) ? "rbz" : "rb");

	if(!pFile)
	{
		httpSendFileNotFoundError(link, pFilename);
		return;
	}

	// Add a new LinkState for this
	printf("Sending large file %s to link 0x%p\n", pFilename, link);

	addLinkState(link, pFile);
}

char *httpChooseSensibleContentTypeForFileName(const char *pFilename)
{
	if (strEndsWith(pFilename, ".txt"))
	{
		return "text/plain";
	}
	if (strEndsWith(pFilename, ".jpg") || strEndsWith(pFilename, ".jpeg"))
	{
		return "image/jpeg";
	}
	if (strEndsWith(pFilename, ".gif"))
	{
		return "image/gif";
	}
	if (strEndsWith(pFilename, ".png"))
	{
		return "image/png";
	}
	if (strEndsWith(pFilename, ".tif") || strEndsWith(pFilename, ".tiff"))
	{
		return "image/tiff";
	}
	if (strEndsWith(pFilename, ".css"))
	{
		return "text/css";
	}
	if (strEndsWith(pFilename, ".js"))
	{
		return "application/javascript";
	}
	if (strEndsWith(pFilename, ".ico"))
	{
		return "image/x-icon";
	}
	if (strEndsWith(pFilename, ".html"))
	{
		return "text/html";
	}

	return "application/octet-stream";
}


extern StashTable sXMLRPCReturnLinks;

void httpSlowCmdReturnCallbackFuncForXMLRPC(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData)
{
	PendingXMLRPCRequest *req = NULL;
	if (!stashIntRemovePointer(sXMLRPCReturnLinks, iRequestID, &req))
	{
		Errorf("No return link found for returning remote XMLRPC request. Link may have timed out.");
		return;
	}

	if (linkConnected(req->pLink) && !linkDisconnected(req->pLink))
	{
		httpSendStr(req->pLink, pMessageString);
	}
	StructDestroy(parse_PendingXMLRPCRequest, req);
}

void httpProcessXMLRPC(NetLink *link, HttpClientStateDefault *pClientState, char *pDataString, HttpAuthenticationResult result)
{
	PendingXMLRPCRequest *req = pClientState->req;
	CmdSlowReturnForServerMonitorInfo slowReturnInfo = {0};
#ifndef _XBOX

	PERFINFO_AUTO_START_FUNC();

	// Call user hook.
	XmlrpcRequestHook(linkGetIp(link), pDataString);

	if(result == HAR_FAILURE)
	{
		char *out = NULL;
		XMLMethodResponse *response = NULL;

		estrStackCreate(&out);
		response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_AUTHFAILURE, "Authentication failed.");
		XMLRPC_WriteOutMethodResponse(response, &out);
		httpSendStr(link, out);
		estrDestroy(&out);
		StructDestroy(parse_XMLMethodResponse, response);
	}
	else
	{
		int accesslevel = httpFindAuthenticationAccessLevel(link, pClientState->iAuthStringCRC, NULL);
		char clientname[16];
		XMLParseInfo* info = XMLRPC_Parse(pDataString,linkGetIpStr(link, clientname, 16));
		XMLMethodResponse *response = NULL;
		char *out = NULL;
		estrStackCreate(&out);
		if (info)
		{
			XMLMethodCall *method = NULL;
			if (method = XMLRPC_GetMethodCall(info))
			{	
				if (req)
				{
					slowReturnInfo.iClientID = req->iclientID;
					slowReturnInfo.iCommandRequestID = req->iRequestID;
					slowReturnInfo.iMCPID = req->iMCPID;
					slowReturnInfo.pSlowReturnCB = httpSlowCmdReturnCallbackFuncForXMLRPC;

					response = XMLRPC_ConvertAndExecuteCommand(method, accesslevel, httpGetCommandCategories(link), httpFindAuthenticationUsernameAndIP(link, pClientState), &slowReturnInfo);
				}
				else
				{
					response = XMLRPC_ConvertAndExecuteCommand(method, accesslevel, httpGetCommandCategories(link), httpFindAuthenticationUsernameAndIP(link, pClientState),  NULL);
				}
			}
			else
			{
				estrPrintf(&out, "XMLRPC Request contained an error: %s", info->error);
			}
			//clean up the parse info (including the methodcall)
			StructDestroy(parse_XMLParseInfo, info);
		}
		else
		{
			estrPrintf(&out, "Error generating XMLRPC request.");
		}

		if (!response)
		{
			response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_BADPARAMS, out);
			estrClear(&out);
		}

		devassertmsg(response->slowID == 0, "Cannot do slow response xmlrpc commands from the normal http dispatch.");

		if (!slowReturnInfo.bDoingSlowReturn)
		{
			XMLRPC_WriteOutMethodResponse(response, &out);

			// Call user hook.
			XmlrpcResponseHook(linkGetIp(link), pDataString);

			httpSendStr(link, out);

			//cleanup the pending request
			if (req)
			{
				stashIntRemovePointer(sXMLRPCReturnLinks, req->iRequestID,NULL);
				StructDestroy(parse_PendingXMLRPCRequest, req);
			}
		}
		
		estrDestroy(&out);

		//clean up the response.
		StructDestroy(parse_XMLMethodResponse, response);
	}
	PERFINFO_AUTO_STOP();
#endif
}

// Do nothing.
static void DefaultXmlrpcHookCallback(U32 uIp, const char *pXml)
{
}

// Hook XML-RPC requests.
HttpXmlrpcHook HookReceivedXmlrpcRequests(HttpXmlrpcHook fpHook)
{
	HttpXmlrpcHook old = XmlrpcRequestHook;
	XmlrpcRequestHook = fpHook;
	return old;
}

// Hook XML-RPC responses.
HttpXmlrpcHook HookSentXmlrpcResponses(HttpXmlrpcHook fpHook)
{
	HttpXmlrpcHook old = XmlrpcResponseHook;
	XmlrpcResponseHook = fpHook;
	return old;
}

void DEFAULT_LATELINK_FlushAuthCache_DoExtraStuff(CmdContext *pContext)
{

}

AUTO_COMMAND ACMD_CATEGORY(admin);
void FlushHttpAuthenticationCache(CmdContext *pContext)
{
	eaDestroyStruct(&sppHttpAuthenticationInfos, parse_HttpAuthenticationInfo);

	//on the controller, this flushes all MCPs
	FlushAuthCache_DoExtraStuff(pContext);
}


#include "HttpLib_h_ast.c"
#include "HttpLib_c_ast.c"
